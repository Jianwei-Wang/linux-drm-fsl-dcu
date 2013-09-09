#include <drm/drmP.h>
#include "virtio_drv.h"

static int drm_virtio_get_irq(struct drm_device *dev)
{
	return 0;
}

static const char *drm_virtio_get_name(struct drm_device *dev)
{
	return "VIRTIO";
}

static int drm_virtio_set_busid(struct drm_device *dev,
			       struct drm_master *master)
{
	return 0;
}

static struct drm_bus drm_virtio_bus = {
	.bus_type = DRIVER_BUS_VIRTIO,
	.get_irq = drm_virtio_get_irq,
	.get_name = drm_virtio_get_name,
	.set_busid = drm_virtio_set_busid,
};

void virtio_set_driver_bus(struct drm_driver *driver)
{
	driver->bus = &drm_virtio_bus;
}
