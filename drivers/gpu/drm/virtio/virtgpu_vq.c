#include <drm/drmP.h>
#include "virtgpu_drv.h"
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>


int virtgpu_resource_id_get(struct virtgpu_device *vgdev, uint32_t *resid)
{
	int handle;

	idr_preload(GFP_KERNEL);
	spin_lock(&vgdev->resource_idr_lock);
	handle = idr_alloc(&vgdev->resource_idr, NULL, 1, 0, GFP_NOWAIT);
	spin_unlock(&vgdev->resource_idr_lock);
	idr_preload_end();
	*resid = handle;
	return 0;
}

void virtgpu_resource_id_put(struct virtgpu_device *vgdev, uint32_t id)
{
	spin_lock(&vgdev->resource_idr_lock);
	idr_remove(&vgdev->resource_idr, id);
	spin_unlock(&vgdev->resource_idr_lock);	
}

void virtgpu_ctrl_ack(struct virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtgpu_device *vgdev = dev->dev_private;
	schedule_work(&vgdev->ctrlq.dequeue_work);
}

void virtgpu_cursor_ack(struct virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtgpu_device *vgdev = dev->dev_private;
	schedule_work(&vgdev->cursorq.dequeue_work);
}

void virtgpu_fence_ack(struct virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtgpu_device *vgdev = dev->dev_private;
	schedule_work(&vgdev->fenceq.dequeue_work);
}

static struct virtgpu_vbuffer *virtgpu_allocate_vbuf(struct virtgpu_device *vgdev,
						     int size, int resp_size,
						     virtgpu_resp_cb resp_cb)
{
	struct virtgpu_vbuffer *vbuf;

	vbuf = kmalloc(sizeof(*vbuf) + size + resp_size, GFP_KERNEL);
	if (!vbuf)
		goto fail;

	vbuf->buf = (void *)vbuf + sizeof(*vbuf);
	vbuf->size = size;
	vbuf->vaddr = NULL;

	vbuf->resp_cb = resp_cb;
	if (resp_size) {
		vbuf->resp_buf = (void *)vbuf->buf + size;
	} else
		vbuf->resp_buf = NULL;
	vbuf->resp_size = resp_size;

	return vbuf;
fail:
	kfree(vbuf);
	return ERR_PTR(-ENOMEM);
}

struct virtgpu_command *virtgpu_alloc_cmd(struct virtgpu_device *vgdev,
					  struct virtgpu_vbuffer **vbuffer_p)
{
	struct virtgpu_vbuffer *vbuf;

	vbuf = virtgpu_allocate_vbuf(vgdev, sizeof(struct virtgpu_command), 0, NULL);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virtgpu_command *)vbuf->buf;
}

struct virtgpu_hw_cursor_page *virtgpu_alloc_cursor(struct virtgpu_device *vgdev,
					     struct virtgpu_vbuffer **vbuffer_p)
{
	struct virtgpu_vbuffer *vbuf;

	vbuf = virtgpu_allocate_vbuf(vgdev, sizeof(struct virtgpu_hw_cursor_page), 0, NULL);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virtgpu_hw_cursor_page *)vbuf->buf;
}

struct virtgpu_command *virtgpu_alloc_cmd_resp(struct virtgpu_device *vgdev,
					       virtgpu_resp_cb cb,
					       struct virtgpu_vbuffer **vbuffer_p)
{
	struct virtgpu_vbuffer *vbuf;

	vbuf = virtgpu_allocate_vbuf(vgdev, sizeof(struct virtgpu_command), sizeof(struct virtgpu_response), cb);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virtgpu_command *)vbuf->buf;
}

static void free_vbuf(struct virtgpu_device *vgdev, struct virtgpu_vbuffer *vbuf)
{
	if (vbuf->vaddr)
		drm_free_large(vbuf->vaddr);

	kfree(vbuf);
}

static int reclaim_vbufs(struct virtqueue *vq, struct list_head *reclaim_list)
{
	struct virtgpu_vbuffer *vbuf;
	unsigned int len;
	int freed = 0;
	while ((vbuf = virtqueue_get_buf(vq, &len))) {
		list_add(&vbuf->destroy_list, reclaim_list);
		freed++;
	}
	return freed;
}
	
void virtgpu_dequeue_ctrl_func(struct work_struct *work)
{
	struct virtgpu_device *vgdev = container_of(work, struct virtgpu_device,
						    ctrlq.dequeue_work);
	int ret;
	struct list_head reclaim_list;
	struct virtgpu_vbuffer *entry, *tmp;
	
	INIT_LIST_HEAD(&reclaim_list);
	spin_lock(&vgdev->ctrlq.qlock);
	do {
		virtqueue_disable_cb(vgdev->ctrlq.vq);
		ret = reclaim_vbufs(vgdev->ctrlq.vq, &reclaim_list);
		if (ret == 0)
			printk("cleaned 0 buffers wierd\n");

	} while (!virtqueue_enable_cb(vgdev->ctrlq.vq));
	spin_unlock(&vgdev->ctrlq.qlock);

	list_for_each_entry_safe(entry, tmp, &reclaim_list, destroy_list) {
		if (entry->resp_cb)
			entry->resp_cb(vgdev, entry);

		list_del(&entry->destroy_list);
		free_vbuf(vgdev, entry);
	}
	wake_up(&vgdev->ctrlq.ack_queue);
}

void virtgpu_dequeue_cursor_func(struct work_struct *work)
{
	struct virtgpu_device *vgdev = container_of(work, struct virtgpu_device,
						    cursorq.dequeue_work);
	struct virtqueue *vq = vgdev->cursorq.vq;
	struct list_head reclaim_list;
	struct virtgpu_vbuffer *entry, *tmp;
	unsigned int len;
	int ret;

	INIT_LIST_HEAD(&reclaim_list);
	spin_lock(&vgdev->cursorq.qlock);
	do {
		virtqueue_disable_cb(vgdev->cursorq.vq);
		ret = reclaim_vbufs(vgdev->cursorq.vq, &reclaim_list);
		if (ret == 0)
			printk("cleaned 0 buffers wierd\n");
		while (virtqueue_get_buf(vq, &len)) {
		}
	} while (!virtqueue_enable_cb(vgdev->cursorq.vq));
	spin_unlock(&vgdev->cursorq.qlock);

	list_for_each_entry_safe(entry, tmp, &reclaim_list, destroy_list) {
		list_del(&entry->destroy_list);
		free_vbuf(vgdev, entry);
	}
	wake_up(&vgdev->cursorq.ack_queue);
}

static int add_fence_inbuf(struct virtgpu_device *vgdev,
			   struct virtqueue *vq)
{
	struct scatterlist sg[1];
	int ret;

	sg_init_one(sg, vgdev->fence_page, PAGE_SIZE);

	ret = virtqueue_add_inbuf(vq, sg, 1, vgdev, GFP_ATOMIC);
	virtqueue_kick(vq);
	if (!ret)
		ret = vq->num_free;
	return ret;
}

void virtgpu_dequeue_fence_func(struct work_struct *work)
{
	struct virtgpu_device *vgdev = container_of(work, struct virtgpu_device,
						    fenceq.dequeue_work);
	struct virtqueue *vq = vgdev->fenceq.vq;
	struct virtgpu_vbuffer *vbuf;
	unsigned int len;

	spin_lock(&vgdev->fenceq.qlock);
	do {
		virtqueue_disable_cb(vgdev->fenceq.vq);
		while ((vbuf = virtqueue_get_buf(vq, &len))) {

			add_fence_inbuf(vgdev, vgdev->fenceq.vq);
		}

	} while (!virtqueue_enable_cb(vgdev->fenceq.vq));
	spin_unlock(&vgdev->fenceq.qlock);

	virtgpu_fence_process(vgdev);
}

int virtgpu_queue_ctrl_buffer(struct virtgpu_device *vgdev,
			      struct virtgpu_vbuffer *vbuf)
{
	struct virtqueue *vq = vgdev->ctrlq.vq;
	struct scatterlist *sgs[2], vcmd, vout, vresp;
	int outcnt, incnt = 0;
	int ret;
	struct sg_table sgt;

	sg_init_one(&vcmd, vbuf->buf, vbuf->size);
	sgs[0] = &vcmd;
	outcnt = 1;
       
	if (vbuf->vaddr) {
		if (is_vmalloc_addr(vbuf->vaddr)) {
			struct vm_struct *vma = find_vm_area(vbuf->vaddr);
			if (!vma)
				return -EINVAL;
			ret = sg_alloc_table_from_pages(&sgt, vma->pages, vma->nr_pages,
							0, vma->nr_pages << PAGE_SHIFT, GFP_KERNEL);
			if (ret)
				return ret;
			sgs[1] = sgt.sgl;
			outcnt++;
		} else {
			sg_init_one(&vout, vbuf->vaddr, vbuf->vaddr_len);
			sgs[1] = &vout;
			outcnt++;
		}
	} else if (vbuf->resp_buf) {
		sg_init_one(&vresp, vbuf->resp_buf, vbuf->resp_size);
		sgs[1] = &vresp;
		incnt++;
	}
	spin_lock(&vgdev->ctrlq.qlock);
retry:
	ret = virtqueue_add_sgs(vq, sgs, outcnt, incnt, vbuf, GFP_ATOMIC);
	if (ret == -ENOSPC) {
		spin_unlock(&vgdev->ctrlq.qlock);
		wait_event(vgdev->ctrlq.ack_queue, vq->num_free);
		spin_lock(&vgdev->ctrlq.qlock);
		goto retry;
	} else {
		virtqueue_kick(vq);
	}

	spin_unlock(&vgdev->ctrlq.qlock);

	if (!ret)
		ret = vq->num_free;
	return ret;
}

int virtgpu_queue_cursor(struct virtgpu_device *vgdev,
			 struct virtgpu_vbuffer *vbuf)
{
	struct virtqueue *vq = vgdev->cursorq.vq;
	struct scatterlist *sgs[1], ccmd;
	int ret;
	int outcnt;

	sg_init_one(&ccmd, vbuf->buf, vbuf->size);
	sgs[0] = &ccmd;
	outcnt = 1;

	spin_lock(&vgdev->cursorq.qlock);
retry:
	ret = virtqueue_add_sgs(vq, sgs, outcnt, 0, vbuf, GFP_ATOMIC);
	if (ret == -ENOSPC) {
		spin_unlock(&vgdev->cursorq.qlock);
		wait_event(vgdev->cursorq.ack_queue, vq->num_free);
		spin_lock(&vgdev->cursorq.qlock);
		goto retry;
	} else {
		virtqueue_kick(vq);
	}

	spin_unlock(&vgdev->cursorq.qlock);

	if (!ret)
		ret = vq->num_free;
	return ret;
}

/* just create gem objects for userspace and long lived objects,
   just use dma_alloced pages for the queue objects? */

/* create a basic resource */
int virtgpu_cmd_create_resource(struct virtgpu_device *vgdev,
				uint32_t resource_id,
				uint32_t format,
				uint32_t width,
				uint32_t height)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_RESOURCE_CREATE_2D;
	cmd_p->u.resource_create_2d.resource_id = resource_id;
	cmd_p->u.resource_create_2d.format = format;
	cmd_p->u.resource_create_2d.width = width;
	cmd_p->u.resource_create_2d.height = height;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

int virtgpu_cmd_unref_resource(struct virtgpu_device *vgdev,
			       uint32_t resource_id)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_RESOURCE_UNREF;
	cmd_p->u.resource_unref.resource_id = resource_id;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_resource_inval_backing(struct virtgpu_device *vgdev,
				       uint32_t resource_id)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_RESOURCE_INVAL_BACKING;
	cmd_p->u.resource_inval_backing.resource_id = resource_id;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

int virtgpu_cmd_set_scanout(struct virtgpu_device *vgdev,
			    uint32_t scanout_id, uint32_t resource_id,
			    uint32_t width, uint32_t height,
			    uint32_t x, uint32_t y)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_SET_SCANOUT;
	cmd_p->u.set_scanout.resource_id = resource_id;
	cmd_p->u.set_scanout.scanout_id = scanout_id;
	cmd_p->u.set_scanout.width = width;
	cmd_p->u.set_scanout.height = height;
	cmd_p->u.set_scanout.x = x;
	cmd_p->u.set_scanout.y = y;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_resource_flush(struct virtgpu_device *vgdev,
			       uint32_t resource_id,
			       uint32_t x, uint32_t y,
			       uint32_t width, uint32_t height)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_RESOURCE_FLUSH;
	cmd_p->u.resource_flush.resource_id = resource_id;
	cmd_p->u.resource_flush.width = width;
	cmd_p->u.resource_flush.height = height;
	cmd_p->u.resource_flush.x = x;
	cmd_p->u.resource_flush.y = y;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);

	return 0;
}

int virtgpu_cmd_transfer_to_host_2d(struct virtgpu_device *vgdev,
				    uint32_t resource_id, uint32_t offset,
				    uint32_t width, uint32_t height,
				    uint32_t x, uint32_t y,
				    struct virtgpu_fence **fence)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_TRANSFER_TO_HOST_2D;
	cmd_p->u.transfer_to_host_2d.resource_id = resource_id;
	cmd_p->u.transfer_to_host_2d.offset = offset;
	cmd_p->u.transfer_to_host_2d.width = width;
	cmd_p->u.transfer_to_host_2d.height = height;
	cmd_p->u.transfer_to_host_2d.x = x;
	cmd_p->u.transfer_to_host_2d.y = y;

	if (fence)
		virtgpu_fence_emit(vgdev, cmd_p, fence);

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

static int virtgpu_cmd_resource_attach_backing(struct virtgpu_device *vgdev, uint32_t resource_id, uint32_t nents,
					       void *vaddr, uint32_t vsize, struct virtgpu_fence **fence)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	vbuf->vaddr = vaddr;
	vbuf->vaddr_len = vsize;
	
	cmd_p->type = VIRTGPU_CMD_RESOURCE_ATTACH_BACKING;
	cmd_p->u.resource_attach_backing.resource_id = resource_id;
	cmd_p->u.resource_attach_backing.nr_entries = nents;
	if (fence)
		virtgpu_fence_emit(vgdev, cmd_p, fence);
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

static void virtgpu_cmd_get_display_info_cb(struct virtgpu_device *vgdev,
					    struct virtgpu_vbuffer *vbuf)
{
	struct virtgpu_response *resp = (struct virtgpu_response *)vbuf->resp_buf;
	spin_lock(&vgdev->display_info_lock);
	memcpy(&vgdev->display_info, &resp->u.display_info, sizeof(struct virtgpu_display_info));
	spin_unlock(&vgdev->display_info_lock);
	wake_up(&vgdev->resp_wq);
}

static void virtgpu_cmd_get_caps_info_cb(struct virtgpu_device *vgdev,
					 struct virtgpu_vbuffer *vbuf)
{
	struct virtgpu_response *resp = (struct virtgpu_response *)vbuf->resp_buf;
	spin_lock(&vgdev->display_info_lock);
	memcpy(&vgdev->caps, &resp->u.caps, sizeof(vgdev->caps));
	spin_unlock(&vgdev->display_info_lock);
	wake_up(&vgdev->resp_wq);
}

int virtgpu_cmd_get_display_info(struct virtgpu_device *vgdev)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd_resp(vgdev, &virtgpu_cmd_get_display_info_cb, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_GET_DISPLAY_INFO;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_get_3d_caps(struct virtgpu_device *vgdev)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd_resp(vgdev, &virtgpu_cmd_get_caps_info_cb, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_GET_CAPS;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;

}

int virtgpu_cmd_context_create(struct virtgpu_device *vgdev, uint32_t id,
			       uint32_t nlen, const char *name)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_CTX_CREATE;
	cmd_p->u.ctx_create.ctx_id = id;
	cmd_p->u.ctx_create.nlen = nlen;
	strncpy(cmd_p->u.ctx_create.debug_name, name, 63);
	cmd_p->u.ctx_create.debug_name[63] = 0;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_context_destroy(struct virtgpu_device *vgdev, uint32_t id)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_CTX_DESTROY;
	cmd_p->u.ctx_destroy.ctx_id = id;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_context_attach_resource(struct virtgpu_device *vgdev, uint32_t ctx_id,
					uint32_t resource_id)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_CTX_ATTACH_RESOURCE;
	cmd_p->u.ctx_resource.ctx_id = ctx_id;
	cmd_p->u.ctx_resource.resource_id = resource_id;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;

}

int virtgpu_cmd_context_detach_resource(struct virtgpu_device *vgdev, uint32_t ctx_id,
					uint32_t resource_id)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_CTX_DETACH_RESOURCE;
	cmd_p->u.ctx_resource.ctx_id = ctx_id;
	cmd_p->u.ctx_resource.resource_id = resource_id;
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_resource_create_3d(struct virtgpu_device *vgdev,
				   struct virtgpu_resource_create_3d *rc_3d,
				   struct virtgpu_fence **fence)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_RESOURCE_CREATE_3D;
	cmd_p->u.resource_create_3d = *rc_3d;
	if (fence)
		virtgpu_fence_emit(vgdev, cmd_p, fence);
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_transfer_to_host_3d(struct virtgpu_device *vgdev, uint32_t resource_id,
				    uint32_t ctx_id,
				    uint64_t offset, uint32_t level, struct virtgpu_box *box,
				    struct virtgpu_fence **fence)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_TRANSFER_TO_HOST_3D;
	cmd_p->u.transfer_to_host_3d.ctx_id = ctx_id;
	cmd_p->u.transfer_to_host_3d.resource_id = resource_id;
	cmd_p->u.transfer_to_host_3d.box = *box;
	cmd_p->u.transfer_to_host_3d.data = offset;
	cmd_p->u.transfer_to_host_3d.level = level;
	if (fence)
		virtgpu_fence_emit(vgdev, cmd_p, fence);
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_transfer_from_host_3d(struct virtgpu_device *vgdev, uint32_t resource_id,
				      uint32_t ctx_id, uint64_t offset, uint32_t level, struct virtgpu_box *box,
				      struct virtgpu_fence **fence)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_TRANSFER_FROM_HOST_3D;
	cmd_p->u.transfer_from_host_3d.ctx_id = ctx_id;
	cmd_p->u.transfer_from_host_3d.resource_id = resource_id;
	cmd_p->u.transfer_from_host_3d.box = *box;
	cmd_p->u.transfer_from_host_3d.data = offset;
	cmd_p->u.transfer_from_host_3d.level = level;
	if (fence)
		virtgpu_fence_emit(vgdev, cmd_p, fence);
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_cmd_submit(struct virtgpu_device *vgdev, void *vaddr,
		       uint32_t size, uint32_t ctx_id,
		       struct virtgpu_fence **fence)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	vbuf->vaddr = vaddr;
	vbuf->vaddr_len = size;

	cmd_p->type = VIRTGPU_CMD_SUBMIT_3D;
	cmd_p->u.cmd_submit.phy_addr = 0;
	cmd_p->u.cmd_submit.size = size;
	cmd_p->u.cmd_submit.ctx_id = ctx_id;
	if (fence)
		virtgpu_fence_emit(vgdev, cmd_p, fence);
	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtgpu_object_attach(struct virtgpu_device *vgdev, struct virtgpu_object *obj, uint32_t resource_id, struct virtgpu_fence **fence)
{
	uint32_t sz;
	void *vaddr;
	int si;
	struct scatterlist *sg;

	if (!obj->pages) {
		int ret;
		ret = virtgpu_object_get_sg_table(vgdev, obj);
		if (ret)
			return ret;
	}

	sz = obj->pages->nents * sizeof(struct virtgpu_mem_entry);
	
	/* gets freed when the ring has consumed it */
	vaddr = drm_calloc_large(obj->pages->nents, sizeof(struct virtgpu_mem_entry));
	if (!vaddr) {
		printk("failed to allocate dma %d\n", sz);
		return -ENOMEM;
	}

	for_each_sg(obj->pages->sgl, sg, obj->pages->nents, si) {
		struct virtgpu_mem_entry *ent = ((struct virtgpu_mem_entry *)vaddr) + si;

		ent->addr = sg_phys(sg);
		ent->length = sg->length;
		ent->pad = 0;
	}

	virtgpu_cmd_resource_attach_backing(vgdev, resource_id, obj->pages->nents, vaddr, sz, fence);

	obj->hw_res_handle = resource_id;
	return 0;
}

void virtgpu_cursor_ping(struct virtgpu_device *vgdev)
{
	struct virtgpu_vbuffer *vbuf;
	struct virtgpu_hw_cursor_page *cur_p;

	cur_p = virtgpu_alloc_cursor(vgdev, &vbuf);

	memcpy(cur_p, &vgdev->cursor_info, sizeof(struct virtgpu_hw_cursor_page));
	virtgpu_queue_cursor(vgdev, vbuf);
}

int virtgpu_fill_fence_vq(struct virtgpu_device *vgdev, int entries)
{
	int i;
	int ret;
	for (i = 0; i < entries; i++) {
		spin_lock_irq(&vgdev->fenceq.qlock);
		
		ret = add_fence_inbuf(vgdev, vgdev->fenceq.vq);
		if (ret < 0) {
			spin_unlock_irq(&vgdev->fenceq.qlock);
			break;
		}
		spin_unlock_irq(&vgdev->fenceq.qlock);
	}
	return i;
		
}
