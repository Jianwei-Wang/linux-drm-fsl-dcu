#include <drm/drmP.h>
#include "virtio_drv.h"

int virtio_gpu_max_ioctls;

int virtio_gpu_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct virtio_gpu_device *vgdev;

	vgdev = kzalloc(sizeof(struct virtio_gpu_device), GFP_KERNEL);
	if (!vgdev)
		return -ENOMEM;

	vgdev->ddev = dev;
	dev->dev_private = vgdev;

	return 0;
}

int virtio_gpu_driver_unload(struct drm_device *dev)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;

	kfree(vgdev);
	return 0;
}
