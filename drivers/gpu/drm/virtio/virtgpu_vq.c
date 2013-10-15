#include <drm/drmP.h>
#include "virtgpu_drv.h"
#include <linux/virtio.h>
#include <linux/virtio_ring.h>
#include "virtio_hw.h"

int virtgpu_resource_id_get(struct virtgpu_device *vgdev, uint32_t *resid)
{
	int handle;
	int idr_ret = -ENOMEM;
again:
	if (idr_pre_get(&vgdev->resource_idr, GFP_KERNEL) == 0) {
		goto fail;
	}
	spin_lock(&vgdev->resource_idr_lock);
	idr_ret = idr_get_new_above(&vgdev->resource_idr, NULL, 1, &handle);
	spin_unlock(&vgdev->resource_idr_lock);
	if (idr_ret == -EAGAIN)
		goto again;

	*resid = handle;
fail:
	return idr_ret;
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
	schedule_work(&vgdev->dequeue_work);
}

static struct virtgpu_vbuffer *virtgpu_allocate_vbuf(struct virtgpu_device *vgdev,
						     int size)
{
	struct virtgpu_vbuffer *vbuf;

	vbuf = kmalloc(sizeof(*vbuf) + size, GFP_KERNEL);
	if (!vbuf)
		goto fail;

	vbuf->buf = (void *)vbuf + sizeof(*vbuf);
	vbuf->size = size;

	return vbuf;
fail:
	kfree(vbuf);
	return ERR_PTR(-ENOMEM);
}

struct virtgpu_command *virtgpu_alloc_cmd(struct virtgpu_device *vgdev,
					  struct virtgpu_vbuffer **vbuffer_p)
{
	struct virtgpu_vbuffer *vbuf;

	vbuf = virtgpu_allocate_vbuf(vgdev, sizeof(struct virtgpu_command));
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virtgpu_command *)vbuf->buf;
}

static void free_vbuf(struct virtgpu_vbuffer *vbuf)
{
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
	
void virtgpu_dequeue_work_func(struct work_struct *work)
{
	struct virtgpu_device *vgdev = container_of(work, struct virtgpu_device,
					       dequeue_work);
	int ret;
	struct list_head reclaim_list;
	struct virtgpu_vbuffer *entry, *tmp;
	
	INIT_LIST_HEAD(&reclaim_list);
	spin_lock(&vgdev->ctrlq_lock);
	do {
		virtqueue_disable_cb(vgdev->ctrlq);
		ret = reclaim_vbufs(vgdev->ctrlq, &reclaim_list);
		if (ret == 0)
			printk("cleaned 0 buffers wierd\n");

	} while (!virtqueue_enable_cb(vgdev->ctrlq));
	spin_unlock(&vgdev->ctrlq_lock);

	list_for_each_entry_safe(entry, tmp, &reclaim_list, destroy_list) {
		list_del(&entry->destroy_list);
		free_vbuf(entry);
	}
	wake_up(&vgdev->ctrl_ack_queue);
}
			
int virtgpu_queue_ctrl_buffer(struct virtgpu_device *vgdev,
			      struct virtgpu_vbuffer *vbuf)
{
	struct virtqueue *vq = vgdev->ctrlq;
	struct scatterlist *sgs[2], vcmd;
	int outcnt, incnt = 0;
	int ret;

	sg_init_one(&vcmd, vbuf->buf, vbuf->size);
	sgs[0] = &vcmd;
	outcnt = 1;
       
	spin_lock(&vgdev->ctrlq_lock);
retry:
	ret = virtqueue_add_sgs(vq, sgs, outcnt, incnt, vbuf, GFP_ATOMIC);
	if (ret == -ENOSPC) {
		spin_unlock(&vgdev->ctrlq_lock);
		wait_event(vgdev->ctrl_ack_queue, vq->num_free);
		spin_lock(&vgdev->ctrlq_lock);
		goto retry;
	} else {
		virtqueue_kick(vq);
	}

	spin_unlock(&vgdev->ctrlq_lock);

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

/* we want to attach backing store and invalidate it for GEM objects
   only so pass the GEM object then internally allocate pages to store
   the sg lists to give to the host */
int virtgpu_cmd_attach_status_page(struct virtgpu_device *vgdev,
				   uint64_t page_addr)
{

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
			       uint32_t width, uint32_t height,
			       uint32_t x, uint32_t y)
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

int virtgpu_cmd_transfer_send_2d(struct virtgpu_device *vgdev,
				 uint32_t resource_id, uint32_t offset,
				 uint32_t width, uint32_t height,
				 uint32_t x, uint32_t y)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->type = VIRTGPU_CMD_TRANSFER_SEND_2D;
	cmd_p->u.transfer_send_2d.resource_id = resource_id;
	cmd_p->u.transfer_send_2d.offset = offset;
	cmd_p->u.transfer_send_2d.width = width;
	cmd_p->u.transfer_send_2d.height = height;
	cmd_p->u.transfer_send_2d.x = x;
	cmd_p->u.transfer_send_2d.y = y;

	virtgpu_queue_ctrl_buffer(vgdev, vbuf);
	       
	return 0;
}

int virtgpu_cmd_resource_attach_backing(struct virtgpu_device *vgdev)
{


}


