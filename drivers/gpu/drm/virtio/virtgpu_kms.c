#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <drm/drmP.h>
#include "virtgpu_drv.h"

int virtgpu_max_ioctls;

static void virtgpu_init_vq(struct virtgpu_queue *vgvq, void (*work_func)(struct work_struct *work))
{
	spin_lock_init(&vgvq->qlock);
	init_waitqueue_head(&vgvq->ack_queue);
	INIT_WORK(&vgvq->dequeue_work, work_func);
}

int virtgpu_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct virtgpu_device *vgdev;
	/* this will expand later */
	struct virtqueue *vqs[2];
	vq_callback_t *callbacks[] = { virtgpu_ctrl_ack, virtgpu_cursor_ack };
	const char *names[] = { "control", "cursor" };
	int nvqs;
	int ret;
	vgdev = kzalloc(sizeof(struct virtgpu_device), GFP_KERNEL);
	if (!vgdev)
		return -ENOMEM;

	vgdev->ddev = dev;
	dev->dev_private = vgdev;
	vgdev->vdev = dev->virtdev;
	vgdev->dev = dev->dev;

	virtgpu_init_vq(&vgdev->ctrlq, virtgpu_dequeue_ctrl_func);
	virtgpu_init_vq(&vgdev->cursorq, virtgpu_dequeue_cursor_func);
	
	vgdev->cursor_page = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!vgdev->cursor_page) {
		kfree(vgdev);
		return -ENOMEM;
	}

	nvqs = 2;

	ret = vgdev->vdev->config->find_vqs(vgdev->vdev, nvqs, vqs, callbacks, names);
	if (ret) {
		DRM_ERROR("failed to find virt queues\n");
		kfree(vgdev);
		return ret;
	}

	vgdev->ctrlq.vq = vqs[0];
	vgdev->cursorq.vq = vqs[1];

	ret = virtgpu_modeset_init(vgdev);
	if (ret) {
		kfree(vgdev);
		return ret;
	}

	return 0;
}

int virtgpu_driver_unload(struct drm_device *dev)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	int ret;

	virtgpu_modeset_fini(vgdev);

	kfree(vgdev->cursor_page);
	kfree(vgdev);
	return 0;
}
