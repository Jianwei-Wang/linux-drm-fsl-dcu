#include <drm/drmP.h>
#include "virtgpu_drv.h"
#include <linux/virtio.h>
#include <linux/virtio_ring.h>

void virtgpu_ctrl_ack(struct virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtgpu_device *vgdev = dev->dev_private;
	schedule_work(&vgdev->dequeue_work);
}

struct virtgpu_vbuffer *virtgpu_allocate_vbuf(struct virtgpu_device *vgdev,
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
