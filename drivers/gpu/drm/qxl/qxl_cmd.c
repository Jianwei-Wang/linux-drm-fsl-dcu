/* QXL cmd/ring handling */

#include "qxl_drv.h"
#include "qxl_object.h"

static int qxl_reap_surface_id(struct qxl_device *qdev, int max_to_reap);

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
	
int qxl_ring_push(struct qxl_ring *ring,
		  const void *new_elt, bool interruptible)
{
	struct qxl_ring_header *header = &(ring->ring->header);
	uint8_t *elt;
	int idx, ret;
	unsigned long flags;
	spin_lock_irqsave(&ring->lock, flags);
	if (header->prod - header->cons == header->num_items) {
		header->notify_on_cons = header->cons + 1;
		mb();
		spin_unlock_irqrestore(&ring->lock, flags);
		if (in_atomic()) {
			while (!qxl_check_header(ring))
				udelay(1);
		} else {
			if (interruptible) {
				ret = wait_event_interruptible(*ring->push_event,
							       qxl_check_header(ring));
				if (ret)
					return ret;
			} else {
				wait_event(*ring->push_event,
					   qxl_check_header(ring));
			}

		}	

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
	return 0;
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

int qxl_garbage_collect(struct qxl_device *qdev)
{
	struct drm_qxl_release *release;
	uint64_t id, next_id;
	int i = 0;
	int ret;
	union qxl_release_info *info;
	void *ptr;
	struct qxl_bo *bo;

	while (qxl_ring_pop(qdev->release_ring, &id)) {
		QXL_INFO(qdev, "popped %lld\n", id);
		while (id) {
			release = qxl_release_from_id_locked(qdev, id);
			if (release == NULL)
				break;
			bo = release->bos[0];

			ret = qxl_bo_reserve(bo, false);
			if (ret) {
				DRM_ERROR("failed to reserve release\n");
				return ret;
			}
			
			ptr = qxl_bo_kmap_atomic_page(qdev, bo, release->release_offset & PAGE_SIZE);
			info = ptr + (release->release_offset & ~PAGE_SIZE);
			next_id = info->next;
			qxl_bo_kunmap_atomic_page(qdev, bo, ptr);

			qxl_bo_unreserve(bo);
			QXL_INFO(qdev, "popped %lld, next %lld\n", id,
				next_id);

			switch (release->type) {
			case QXL_RELEASE_DRAWABLE:
			case QXL_RELEASE_SURFACE_CMD:
			case QXL_RELEASE_CURSOR_CMD:
				break;
			default:
				DRM_ERROR("unexpected release type\n");
				break;
			}
			id = next_id;

			qxl_release_free(qdev, release);
			++i;
		}
	}

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
			    QXL_GEM_DOMAIN_VRAM, NULL, &bo);
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
	int ret;

	mutex_lock(&qdev->async_io_mutex);

	if (qdev->last_sent_io_cmd > irq_num) {
	restart:
		ret = wait_event_interruptible(qdev->io_cmd_event,
					       atomic_read(&qdev->irq_received_io_cmd) > irq_num);
		if (ret == -ERESTARTSYS)
			goto restart;
			
	}
	outb(val, addr);
	qdev->last_sent_io_cmd = irq_num + 1;
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
	mutex_unlock(&qdev->async_io_mutex);
}

static int wait_for_io_cmd_user(struct qxl_device *qdev, uint8_t val, long port)
{
	int irq_num = atomic_read(&qdev->irq_received_io_cmd);
	long addr = qdev->io_base + port;
	int ret;

	mutex_lock(&qdev->async_io_mutex);
	if (qdev->last_sent_io_cmd > irq_num) {
		ret = wait_event_interruptible(qdev->io_cmd_event,
					       atomic_read(&qdev->irq_received_io_cmd) > irq_num);
		if (ret)
			goto out;
	}
	outb(val, addr);
	qdev->last_sent_io_cmd = irq_num + 1;
	ret = wait_event_interruptible(qdev->io_cmd_event,
				       atomic_read(&qdev->irq_received_io_cmd) > irq_num);
out:
	mutex_unlock(&qdev->async_io_mutex);
	return ret;
}

int qxl_io_update_area(struct qxl_device *qdev, struct qxl_bo *surf,
			const struct qxl_rect *area)
{
	int surface_id;
	uint32_t surface_width, surface_height;
	int ret;
	if (surf->is_primary)
		surface_id = 0;
	else
		surface_id = surf->surface_id;
	surface_width = surf->surf.width;
	surface_height = surf->surf.height;

	if (area->left < 0 || area->top < 0 ||
	    area->right > surface_width || area->bottom > surface_height) {
		qxl_io_log(qdev, "%s: not doing area update for "
			   "%d, (%d,%d,%d,%d) (%d,%d)\n", __func__, surface_id, area->left,
			   area->top, area->right, area->bottom, surface_width, surface_height);
		return -EINVAL;
	}
	mutex_lock(&qdev->update_area_mutex);
	qdev->ram_header->update_area = *area;
	qdev->ram_header->update_surface = surface_id;
	ret = wait_for_io_cmd_user(qdev, 0, QXL_IO_UPDATE_AREA_ASYNC);
	mutex_unlock(&qdev->update_area_mutex);
	return ret;
}

void qxl_io_notify_oom(struct qxl_device *qdev)
{
	outb(0, qdev->io_base + QXL_IO_NOTIFY_OOM);
}

void qxl_io_destroy_primary(struct qxl_device *qdev)
{
//	if (!qdev->primary_created)
//		return;
	wait_for_io_cmd(qdev, 0, QXL_IO_DESTROY_PRIMARY_ASYNC);
//	qdev->primary_created = 0;
}

void qxl_io_create_primary(struct qxl_device *qdev, unsigned width,
			   unsigned height, unsigned offset, struct qxl_bo *bo)
{
	struct qxl_surface_create *create;

	QXL_INFO(qdev, "%s: qdev %p, ram_header %p\n", __func__, qdev,
		 qdev->ram_header);
	create = &qdev->ram_header->create_surface;
	create->format = bo->surf.format;
	create->width = width;
	create->height = height;
	create->stride = bo->surf.stride;
	create->mem = qxl_bo_physical_address(qdev, bo, offset);

	QXL_INFO(qdev, "%s: mem = %llx, from %p\n", __func__, create->mem,
		 bo->kptr);

	create->flags = QXL_SURF_FLAG_KEEP_DATA;
	create->type = QXL_SURF_TYPE_PRIMARY;

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

int qxl_surface_id_alloc(struct qxl_device *qdev,
		      struct qxl_bo *surf)
{
	uint32_t handle = -ENOMEM;
	int idr_ret;
	int count = 0;
again:
	if (idr_pre_get(&qdev->surf_id_idr, GFP_ATOMIC) == 0) {
		DRM_ERROR("Out of memory for surf idr\n");
		kfree(surf);
		goto alloc_fail;
	}

	spin_lock(&qdev->surf_id_idr_lock);
	idr_ret = idr_get_new_above(&qdev->surf_id_idr, surf, 1, &handle);
	spin_unlock(&qdev->surf_id_idr_lock);

	if (idr_ret == -EAGAIN)
		goto again;

	if (handle >= qdev->rom->n_surfaces) {
		int res;
		count++;
		spin_lock(&qdev->surf_id_idr_lock);
		idr_remove(&qdev->surf_id_idr, handle);
		spin_unlock(&qdev->surf_id_idr_lock);
		/* deallocate some surfaces */
		if (count == 1)
			qxl_garbage_collect(qdev);
		else if (count < 4) {
			qxl_io_notify_oom(qdev);
			res = qxl_garbage_collect(qdev);
			if (res == 0)
				mdelay(10);
		} else {
			qxl_reap_surface_id(qdev, 20);
		}
		goto again;
	}
	surf->surface_id = handle;

	spin_lock(&qdev->surf_id_idr_lock);
	qdev->last_alloced_surf_id = handle;
	spin_unlock(&qdev->surf_id_idr_lock);
 alloc_fail:
	return 0;
}

void qxl_surface_id_dealloc(struct qxl_device *qdev,
			    struct qxl_bo *surf)
{
	spin_lock(&qdev->surf_id_idr_lock);
	idr_remove(&qdev->surf_id_idr, surf->surface_id);
	spin_unlock(&qdev->surf_id_idr_lock);
	surf->surface_id = 0;
}

int qxl_hw_surface_alloc(struct qxl_device *qdev,
			 struct qxl_bo *surf,
			 struct ttm_mem_reg *new_mem)
{
	struct qxl_surface_cmd *cmd;
	struct qxl_bo *cmd_bo;
	struct drm_qxl_release *release;
	void *ptr;
	int ret;

	if (surf->hw_surf_alloc)
		return 0;

	ret = qxl_alloc_release_reserved(qdev, sizeof(*cmd), QXL_RELEASE_SURFACE_CMD,
					 &release, &cmd_bo);
	if (ret)
		return ret;

	ptr = qxl_bo_kmap_atomic_page(qdev, cmd_bo, (release->release_offset & PAGE_SIZE));
	cmd = ptr + (release->release_offset & ~PAGE_SIZE);
	
	cmd->type = QXL_SURFACE_CMD_CREATE;
	cmd->u.surface_create.format = surf->surf.format;
	cmd->u.surface_create.width = surf->surf.width;
	cmd->u.surface_create.height = surf->surf.height;
	cmd->u.surface_create.stride = surf->surf.stride;
	if (new_mem) {
		int slot_id = surf->type == QXL_GEM_DOMAIN_VRAM ? qdev->main_mem_slot : qdev->surfaces_mem_slot;
		struct qxl_memslot *slot = &(qdev->mem_slots[slot_id]);

		/* TODO - need to hold one of the locks to read tbo.offset */
		cmd->u.surface_create.data = slot->high_bits;

		cmd->u.surface_create.data |= (new_mem->start << PAGE_SHIFT) + surf->tbo.bdev->man[new_mem->mem_type].gpu_offset;
	} else
		cmd->u.surface_create.data = qxl_bo_physical_address(qdev, surf, 0);
	cmd->surface_id = surf->surface_id;
	qxl_bo_kunmap_atomic_page(qdev, cmd_bo, ptr);
	/* no need to add a release to the fence for this bo,
	   since it is only released when we ask to destroy the surface
	   and it would never signal otherwise */
	qxl_push_command_ring_release(qdev, release, QXL_CMD_SURFACE, false);
	qxl_bo_unreserve(cmd_bo);

	surf->hw_surf_alloc = true;
	return 0;
}

int qxl_hw_surface_dealloc(struct qxl_device *qdev,
			   struct qxl_bo *surf)
{
	struct qxl_surface_cmd *cmd;
	struct qxl_bo *cmd_bo;
	struct drm_qxl_release *release;
	int ret;
	void *ptr;

	if (!surf->hw_surf_alloc)
		return 0;

	ret = qxl_alloc_release_reserved(qdev, sizeof(*cmd), QXL_RELEASE_SURFACE_CMD,
					 &release, &cmd_bo);
	if (ret)
		return ret;

	ptr = qxl_bo_kmap_atomic_page(qdev, cmd_bo, (release->release_offset & PAGE_SIZE));
	cmd = ptr + (release->release_offset & ~PAGE_SIZE);
	cmd->type = QXL_SURFACE_CMD_DESTROY;
	cmd->surface_id = surf->surface_id;
	qxl_bo_kunmap_atomic_page(qdev, cmd_bo, ptr);

	qxl_push_command_ring_release(qdev, release, QXL_CMD_SURFACE, false);

	qxl_fence_releaseable(qdev, release);
	qxl_bo_unreserve(cmd_bo);
	surf->hw_surf_alloc = false;
	return 0;
}

void qxl_surface_evict(struct qxl_device *qdev, struct qxl_bo *surf)
{
	struct qxl_rect rect;

	/* if we are evicting, we need to make sure the surface is up
	   to date */
	rect.left = 0;
	rect.right = surf->surf.width;
	rect.top = 0;
	rect.bottom = surf->surf.height;
	qxl_io_update_area(qdev, surf, &rect);

	/* nuke the surface id at the hw */
	qxl_hw_surface_dealloc(qdev, surf);
	qxl_surface_id_dealloc(qdev, surf);
}

static int qxl_reap_surf(struct qxl_device *qdev, struct qxl_bo *surf, bool stall)
{
	int ret;

	ret = qxl_bo_reserve(surf, true);
	if (ret == -EBUSY)
		return -EBUSY;

	if (surf->fence.num_releases > 0 && stall == false) {
		qxl_bo_unreserve(surf);
		return -EBUSY;
	}

	spin_lock(&surf->tbo.bdev->fence_lock);
	ret = ttm_bo_wait(&surf->tbo, true, true, !stall);
	spin_unlock(&surf->tbo.bdev->fence_lock);
	if (ret == -EBUSY) {
		qxl_bo_unreserve(surf);
		return -EBUSY;
	}

	qxl_surface_evict(qdev, surf);
	qxl_bo_unreserve(surf);
	return 0;
}

static int qxl_reap_surface_id(struct qxl_device *qdev, int max_to_reap)
{
	int num_reaped = 0;
	int i, ret;
	bool stall = false;
	int start = 0;
again:

	spin_lock(&qdev->surf_id_idr_lock);
	if (qdev->last_alloced_surf_id + 1 + 2 * max_to_reap > 1024)
		start = 0;
	else
		start = qdev->last_alloced_surf_id + 1;
	spin_unlock(&qdev->surf_id_idr_lock);

	for (i = start; i < start + 2 * max_to_reap; i++) {
		void *objptr;

		spin_lock(&qdev->surf_id_idr_lock);
		objptr = idr_find(&qdev->surf_id_idr, i);
		spin_unlock(&qdev->surf_id_idr_lock);

		if (!objptr)
			continue;

		ret = qxl_reap_surf(qdev, objptr, stall);
		if (ret == 0)
			num_reaped++;
	}
	if (num_reaped == 0 && stall == false) {
		stall = true;
		goto again;
	}
	return 0;
}
