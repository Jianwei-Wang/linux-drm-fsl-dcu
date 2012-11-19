/* QXL cmd/ring handling */

#include "qxl_drv.h"
#include "qxl_object.h"

struct ring {
	struct qxl_ring_header      header;
	uint8_t                     elements[0];
};

struct qxl_ring {
	struct ring	       *ring;
	int			element_size;
	int			n_elements;
	int			prod_notify;
	wait_queue_head_t      *push_event;
	spinlock_t             lock;
};

void qxl_ring_free(struct qxl_ring *ring)
{
	kfree(ring);
}

struct qxl_ring *
qxl_ring_create(struct qxl_ring_header *header,
		int element_size,
		int n_elements,
		int prod_notify,
		wait_queue_head_t *push_event)
{
	struct qxl_ring *ring;

	ring = kmalloc(sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return NULL;

	ring->ring = (struct ring *)header;
	ring->element_size = element_size;
	ring->n_elements = n_elements;
	ring->prod_notify = prod_notify;
	ring->push_event = push_event;

	spin_lock_init(&ring->lock);
	return ring;
}

static int qxl_check_header(struct qxl_ring *ring)
{
	int ret;
	struct qxl_ring_header *header = &(ring->ring->header);
	unsigned long flags;
	spin_lock_irqsave(&ring->lock, flags);
	ret = header->prod - header->cons < header->num_items;
	spin_unlock_irqrestore(&ring->lock, flags);
	return ret;
}

static int qxl_check_idle(struct qxl_ring *ring)
{
	int ret;
	struct qxl_ring_header *header = &(ring->ring->header);
	unsigned long flags;
	spin_lock_irqsave(&ring->lock, flags);
	ret = header->prod == header->cons;
	spin_unlock_irqrestore(&ring->lock, flags);
	return ret;
}
	
void qxl_ring_push(struct qxl_ring *ring,
		   const void *new_elt)
{
	struct qxl_ring_header *header = &(ring->ring->header);
	uint8_t *elt;
	int idx;
	unsigned long flags;
	spin_lock_irqsave(&ring->lock, flags);
	if (header->prod - header->cons == header->num_items) {
		header->notify_on_cons = header->cons + 1;
		mb();
		spin_unlock_irqrestore(&ring->lock, flags);
		wait_event_interruptible(*ring->push_event,
					 qxl_check_header(ring));
		spin_lock_irqsave(&ring->lock, flags);
	}

	idx = header->prod & (ring->n_elements - 1);
	elt = ring->ring->elements + idx * ring->element_size;

	memcpy((void *)elt, new_elt, ring->element_size);

	header->prod++;

	mb();

	if (header->prod == header->notify_on_prod)
		outb(0, ring->prod_notify);

	spin_unlock_irqrestore(&ring->lock, flags);
}

bool qxl_ring_pop(struct qxl_ring *ring,
		  void *element)
{
	volatile struct qxl_ring_header *header = &(ring->ring->header);
	volatile uint8_t *ring_elt;
	int idx;
	unsigned long flags;
	spin_lock_irqsave(&ring->lock, flags);
	if (header->cons == header->prod) {
		spin_unlock_irqrestore(&ring->lock, flags);
		return false;
	}

	idx = header->cons & (ring->n_elements - 1);
	ring_elt = ring->ring->elements + idx * ring->element_size;

	memcpy(element, (void *)ring_elt, ring->element_size);

	header->cons++;

	spin_unlock_irqrestore(&ring->lock, flags);
	return true;
}

void qxl_ring_wait_idle(struct qxl_ring *ring)
{
	struct qxl_ring_header *header = &(ring->ring->header);
	unsigned long flags;

	spin_lock_irqsave(&ring->lock, flags);
	if (ring->ring->header.cons < ring->ring->header.prod) {
		header->notify_on_cons = header->prod;
		mb();
		spin_unlock_irqrestore(&ring->lock, flags);
		wait_event_interruptible(*ring->push_event,
					 qxl_check_idle(ring));
		spin_lock_irqsave(&ring->lock, flags);
	}
	spin_unlock_irqrestore(&ring->lock, flags);
}

struct drm_qxl_release *qxl_release_from_id_locked(struct qxl_device *qdev,
						   uint64_t id)
{
	struct drm_qxl_release *release;

	release = idr_find(&qdev->release_idr, id);
	if (!release) {
		DRM_ERROR("failed to find id in release_idr\n");
		return NULL;
	}
	if (release->bo_count < 1) {
		DRM_ERROR("read a released resource with 0 bos\n");
		return NULL;
	}
	return release;
}

int qxl_garbage_collect(struct qxl_device *qdev)
{
	struct drm_qxl_release *release;
	uint64_t id;
	int i = 0;
	union qxl_release_info *info;

	mutex_lock(&qdev->release_idr_mutex);
	while (qxl_ring_pop(qdev->release_ring, &id)) {
		QXL_INFO(qdev, "popped %lld\n", id);
		while (id) {
			release = qxl_release_from_id_locked(qdev, id);
			if (release == NULL)
				break;
			info = (union qxl_release_info *)release->bos[0]->kptr;
			QXL_INFO(qdev, "popped %lld, next %lld\n", id,
				 info->next);

			switch (release->type) {
			case QXL_RELEASE_DRAWABLE:
			case QXL_RELEASE_SURFACE_CMD:
			case QXL_RELEASE_CURSOR_CMD:
				break;
			default:
				DRM_ERROR("unexpected release type\n");
				break;
			}
			id = info->next;
			qxl_release_free_locked(qdev, release);
			++i;
		}
	}
	mutex_unlock(&qdev->release_idr_mutex);
	QXL_INFO(qdev, "%s: %lld\n", __func__, i);

	return i;
}

/* create and pin bo */
static struct qxl_bo *qxl_create_pinned_bo(struct qxl_device *qdev,
					   unsigned long size)
{
	struct qxl_bo *bo;
	int ret;

	ret = qxl_bo_create(qdev, size, false /* not kernel - device */,
			   QXL_GEM_DOMAIN_VRAM, &bo);
	if (ret) {
		DRM_ERROR("failed to allocate VRAM BO\n");
		return NULL;
	}
	ret = qxl_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		goto out_unref;

	ret = qxl_bo_pin(bo, QXL_GEM_DOMAIN_VRAM, NULL);
	if (ret) {
		DRM_ERROR("failed to pin VRAM BO %d\n", ret);
		goto out_unref;
	}

	ret = qxl_bo_kmap(bo, NULL);
	qxl_bo_unreserve(bo); /* this memory will be reserved via mmap */
	if (ret)
		goto out_unref;
	return bo;
out_unref:
	qxl_bo_unref(&bo);
	return NULL;
}

void *qxl_allocnf(struct qxl_device *qdev, unsigned long size,
		  struct drm_qxl_release *release)
{
	struct qxl_bo *bo;

	qxl_garbage_collect(qdev);
	bo = qxl_create_pinned_bo(qdev, size);
	qxl_release_add_res(qdev, release, bo);
	return bo->kptr;
}

static void wait_for_io_cmd(struct qxl_device *qdev, uint8_t val, long port)
{
	int irq_num = atomic_read(&qdev->irq_received_io_cmd);
	long addr = qdev->io_base + port;
	int num_restart = 0;

	mutex_lock(&qdev->async_io_mutex);
	outb(val, addr);
	for (; 1 ; ++num_restart) {
		switch (wait_event_interruptible(qdev->io_cmd_event,
			atomic_read(&qdev->irq_received_io_cmd) > irq_num)) {
		case 0:
			goto done;
			break;
		case -ERESTARTSYS:
			continue;
			break;
		}
	}
done:
	qxl_io_log(qdev, "%s: async %d completed after %d wakes\n",
		   __func__, port, num_restart);
	mutex_unlock(&qdev->async_io_mutex);
}

void qxl_io_update_area(struct qxl_device *qdev, uint32_t surface_id,
			const struct qxl_rect *area)
{
	unsigned surface_width = qxl_surface_width(qdev, surface_id);
	unsigned surface_height = qxl_surface_height(qdev, surface_id);

	if (area->left < 0 || area->top < 0 ||
	    area->right > surface_width || area->bottom > surface_height) {
		qxl_io_log(qdev, "%s: not doing area update for "
			   "%d, (%d,%d,%d,%d)\n", surface_id, area->left,
			   area->top, area->right, area->bottom);
		return;
	}
	qdev->ram_header->update_area = *area;
	qdev->ram_header->update_surface = surface_id;
	wait_for_io_cmd(qdev, 0, QXL_IO_UPDATE_AREA_ASYNC);
}

void qxl_io_notify_oom(struct qxl_device *qdev)
{
	outb(0, qdev->io_base + QXL_IO_NOTIFY_OOM);
}

void qxl_io_update_screen(struct qxl_device *qdev)
{
	struct qxl_rect area;
	u32 height, width;

	height = qdev->fbdev_qfb->base.height;
	width = qdev->fbdev_qfb->base.width;
	QXL_INFO(qdev, "%s: bad bad bad %dx%d\n", __func__,
		 width, height);
	area.left = area.top = 0;
	area.right = width;
	area.bottom = height;

	qxl_io_update_area(qdev, 0, &area);
}

void qxl_io_destroy_primary(struct qxl_device *qdev)
{
	if (!qdev->primary_created)
		return;
	wait_for_io_cmd(qdev, 0, QXL_IO_DESTROY_PRIMARY_ASYNC);
	qdev->primary_created = 0;
}

void qxl_io_create_primary(struct qxl_device *qdev, unsigned width,
			   unsigned height)
{
	struct qxl_surface_create *create;
	int32_t stride = width * 4;

	QXL_INFO(qdev, "%s: qdev %p, ram_header %p\n", __func__, qdev,
		 qdev->ram_header);
	create = &qdev->ram_header->create_surface;
	create->format = SPICE_SURFACE_FMT_32_xRGB;
	create->width = width;
	create->height = height;
	create->stride = stride;
	create->mem = qxl_bo_physical_address(qdev,
					      qdev->surface0_bo, 0,
					      qdev->main_mem_slot);
	QXL_INFO(qdev, "%s: mem = %llx, from %p\n", __func__, create->mem,
		 qdev->surface0_bo->kptr);

	create->flags = 0;
	create->type = QXL_SURF_TYPE_PRIMARY;

	qdev->primary_created = 1;
	qdev->primary_width = width;
	qdev->primary_height = height;

	wait_for_io_cmd(qdev, 0, QXL_IO_CREATE_PRIMARY_ASYNC);
}

void qxl_io_memslot_add(struct qxl_device *qdev, uint8_t id)
{
	QXL_INFO(qdev, "qxl_memslot_add %d\n", id);
	wait_for_io_cmd(qdev, id, QXL_IO_MEMSLOT_ADD_ASYNC);
}

void qxl_io_log(struct qxl_device *qdev, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(qdev->ram_header->log_buf, QXL_LOG_BUF_SIZE, fmt, args);
	va_end(args);
	/*
	 * DO not do a DRM output here - this will call printk, which will
	 * call back into qxl for rendering (qxl_fb)
	 */
	outb(0, qdev->io_base + QXL_IO_LOG);
}

void qxl_io_reset(struct qxl_device *qdev)
{
	outb(0, qdev->io_base + QXL_IO_RESET);
}

void qxl_io_monitors_config(struct qxl_device *qdev)
{
	qxl_io_log(qdev, "%s: %d [%dx%d+%d+%d]\n", __func__,
		   qdev->monitors_config ?
		   qdev->monitors_config->count : -1,
		   qdev->monitors_config && qdev->monitors_config->count ?
		   qdev->monitors_config->heads[0].width : -1,
		   qdev->monitors_config && qdev->monitors_config->count ?
		   qdev->monitors_config->heads[0].height : -1,
		   qdev->monitors_config && qdev->monitors_config->count ?
		   qdev->monitors_config->heads[0].x : -1,
		   qdev->monitors_config && qdev->monitors_config->count ?
		   qdev->monitors_config->heads[0].y : -1
		   );

	wait_for_io_cmd(qdev, 0, QXL_IO_MONITORS_CONFIG_ASYNC);
}
