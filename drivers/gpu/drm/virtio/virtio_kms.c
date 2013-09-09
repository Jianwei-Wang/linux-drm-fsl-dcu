#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <drm/drmP.h>
#include "virtio_drv.h"

int virtgpu_max_ioctls;


int virtgpu_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct virtgpu_device *vgdev;
	/* this will expand later */
	struct virtqueue *vqs[1];
	vq_callback_t *callbacks[] = { virtgpu_ctrl_ack };
	const char *names[] = { "control" };
	int nvqs;
	int ret;

	vgdev = kzalloc(sizeof(struct virtgpu_device), GFP_KERNEL);
	if (!vgdev)
		return -ENOMEM;

	vgdev->ddev = dev;
	dev->dev_private = vgdev;
	vgdev->vdev = dev->virtdev;

	init_waitqueue_head(&vgdev->ctrl_ack_queue);
	INIT_WORK(&vgdev->dequeue_work, virtgpu_dequeue_work_func);

	nvqs = 1;

	ret = vgdev->vdev->config->find_vqs(vgdev->vdev, nvqs, vqs, callbacks, names);
	if (ret) {
		DRM_ERROR("failed to find virt queues\n");
		kfree(vgdev);
		return ret;
	}

	vgdev->ctrlq = vqs[0];

	return 0;
}

int virtgpu_driver_unload(struct drm_device *dev)
{
	struct virtgpu_device *vgdev = dev->dev_private;

	kfree(vgdev);
	return 0;
}
