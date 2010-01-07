/* QXL cmd/ring handling */

#include "qxl_drv.h"
#include "qxl_object.h"
struct ring {
	struct qxl_ring_header      header;
	uint8_t                     elements[0];
};

struct qxl_ring {
	volatile struct ring *ring;
	int			element_size;
	int			n_elements;
	int			prod_notify;
};

void qxl_ring_free(struct qxl_ring *ring)
{
	kfree(ring);
}

struct qxl_ring *
qxl_ring_create (struct qxl_ring_header *header,
		 int element_size,
		 int n_elements,
		 int prod_notify)
{
	struct qxl_ring *ring;

	ring = kmalloc (sizeof(*ring), GFP_KERNEL);
	if (!ring)
		return NULL;

	ring->ring = (volatile struct ring *)header;
	ring->element_size = element_size;
	ring->n_elements = n_elements;
	ring->prod_notify = prod_notify;
    
	return ring;
}

void qxl_ring_push (struct qxl_ring *ring,
		    const void *new_elt)
{
	volatile struct qxl_ring_header *header = &(ring->ring->header);
	volatile uint8_t *elt;
	int idx;

	while (header->prod - header->cons == header->num_items) {
		header->notify_on_cons = header->cons + 1;
		mb();
	}

	idx = header->prod & (ring->n_elements - 1);
	elt = ring->ring->elements + idx * ring->element_size;

	memcpy((void *)elt, new_elt, ring->element_size);

	header->prod++;
	
	mb();

	if (header->prod == header->notify_on_prod)
		outb (0, ring->prod_notify);
}

bool qxl_ring_pop (struct qxl_ring *ring,
		   void *element)
{
	volatile struct qxl_ring_header *header = &(ring->ring->header);
	volatile uint8_t *ring_elt;
	int idx;

	if (header->cons == header->prod)
		return false;

	idx = header->cons & (ring->n_elements - 1);
	ring_elt = ring->ring->elements + idx * ring->element_size;

	memcpy (element, (void *)ring_elt, ring->element_size);

	header->cons++;

	return true;
}

void qxl_ring_wait_idle (struct qxl_ring *ring)
{
	while (ring->ring->header.cons != ring->ring->header.prod)
	{
		msleep (1);
		mb();
	}
}

void qxl_bo_free(struct qxl_bo *bo)
{
	int ret;
	ret = qxl_bo_reserve(bo, false);
	if (!ret) {
		qxl_bo_kunmap(bo);
		qxl_bo_unpin(bo);
		qxl_bo_unreserve(bo);
	}
	qxl_bo_unref(&bo);
}

static int qxl_garbage_collect(struct qxl_device *qdev)
{
	uint64_t id;
	int i = 0;
    
	while (qxl_ring_pop (qdev->release_ring, &id)) {
		while (id) {
			/* We assume that there the two low bits of a pointer are
			 * available. If the low one is set, then the command in
			 * question is a cursor command
			 */
#define POINTER_MASK ((1 << 2) - 1)
			
			struct qxl_bo *bo = (void *)(id & ~POINTER_MASK);
			union qxl_release_info *info = bo->kptr;
			struct qxl_cursor_cmd *cmd = (struct qxl_cursor_cmd *)info;
			struct qxl_drawable *drawable = (struct qxl_drawable *)info;
			bool is_cursor = false;

			if ((id & POINTER_MASK) == 1)
				is_cursor = true;

#if 0
			if (is_cursor && cmd->type == QXL_CURSOR_SET) {
				struct qxl_cursor *cursor = (void *)qxl_virtual_address
					(qdev, (void *)cmd->u.set.shape);
				qxl_free(qdev->mem, cursor);
			} else if (!is_cursor && drawable->type == QXL_DRAW_COPY) {
				struct qxl_image *image = qxl_virtual_address(qdev, 
									      (void *)drawable->u.copy.src_bitmap);
				qxl_image_destroy (qdev, image);
			}
#endif
			
			id = info->next;
			qxl_bo_free(bo);
			//		qxl_free(qdev->mem, info);
		}

	}

	return i > 0;
}

/* create and pin bo */
struct qxl_bo *qxl_allocnf(struct qxl_device *qdev, unsigned long size)
{
	struct qxl_bo *bo;
	int ret;
	void *result;
	int n_attempts = 0;
	static int nth_oom = 1;

	qxl_garbage_collect(qdev);
    
	ret = qxl_bo_create(qdev, NULL, size, true,
			   QXL_GEM_DOMAIN_IO, &bo);
	if (ret) {
		DRM_ERROR("failed to allocate IO BO\n");
		return NULL;
	}

	ret = qxl_bo_reserve(bo, false);
	if (unlikely(ret != 0))
		goto out_unref;

	ret = qxl_bo_pin(bo, QXL_GEM_DOMAIN_IO, NULL);
	if (ret) {
		DRM_ERROR("failed to pin IO BO %d\n", ret);
		goto out_unref;
	}

	ret = qxl_bo_kmap(bo, NULL);
	qxl_bo_unreserve(bo);
	if (ret)
		goto out_unref;
	return bo;
out_unref:
	qxl_bo_unref(&bo);
	return NULL;
}


#if 0
	while (!(result = qxl_alloc (qdev->mem, size)))
	{
		struct qxl_ram_header *ram_header = (void *)((unsigned long)qdev->ram +
							     qdev->rom->ram_header_offset);
        
		/* Rather than go out of memory, we simply tell the
		 * device to dump everything
		 */
		ram_header->update_area.top = 0;
		ram_header->update_area.bottom = 1280;
		ram_header->update_area.left = 0;
		ram_header->update_area.right = 800;
        
		outb (0, qdev->io_base + QXL_IO_UPDATE_AREA);
        
		printk(KERN_ERR "eliminated memory (%d)\n", nth_oom++);

		outb (0, qdev->io_base + QXL_IO_NOTIFY_OOM);

		msleep_interruptible(10);
        
		if (qxl_garbage_collect(qdev)) {
			n_attempts = 0;
		}
		else if (++n_attempts == 1000)
		{
//			qxl_mem_dump_stats (qdev->mem, "Out of mem - stats\n");
			BUG();
		}
	}
	return result;
}
#endif

void qxl_push_update_area(struct qxl_device *qdev, const struct qxl_rect *area)
{
	struct qxl_update_cmd *update;
	struct qxl_bo *cmd_bo;
	struct qxl_command cmd;
	int ret;
	
	cmd_bo = qxl_allocnf(qdev, sizeof(*update));
	
	update = cmd_bo->kptr;
	update->release_info.id = (uint64_t)cmd_bo;
	update->area = *area;
	update->update_id = 0;
//	qxl_bo_kunmap(cmd_bo);
	cmd.type = QXL_CMD_UPDATE;
	cmd.data = qxl_bo_gpu_offset(cmd_bo) + qdev->vram_base + qdev->rom->pages_offset;

	DRM_DEBUG("push ring %llx %x\n", qxl_bo_gpu_offset(cmd_bo), cmd.data);

	if (qdev->mode_set == false) {
		DRM_ERROR("ring called before mode set\n");
		qxl_bo_free(cmd_bo);
	}
	else
		qxl_ring_push(qdev->command_ring, &cmd);
}

void qxl_push_screen(struct qxl_device *qdev)
{
	struct qxl_rect area;

	area.left = area.top = 0;
	area.right = 1920;
	area.bottom = 1200;

	qxl_push_update_area(qdev, &area);
}
