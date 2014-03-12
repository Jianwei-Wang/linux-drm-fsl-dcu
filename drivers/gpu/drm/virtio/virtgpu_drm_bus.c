#include <drm/drmP.h>
#include <linux/pci.h>

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
	struct pci_dev *pdev = dev->pdev;
	int len;

	if (pdev) {
		/* mimic drm_pci_set_busid() */
		master->unique_len = 40;
		master->unique_size = master->unique_len;
		master->unique = kmalloc(master->unique_size, GFP_KERNEL);
		if (master->unique == NULL)
			return -ENOMEM;
		len = snprintf(master->unique, master->unique_len,
			       "pci:%04x:%02x:%02x.%d",
			       pci_domain_nr(pdev->bus),
			       pdev->bus->number,
			       PCI_SLOT(pdev->devfn),
			       PCI_FUNC(pdev->devfn));
		if (len >= master->unique_len) {
			DRM_ERROR("buffer overflow");
			return -EINVAL;
		}
		master->unique_len = len;

		dev->devname =
			kmalloc(strlen("virtio") +
				master->unique_len + 2, GFP_KERNEL);
		if (dev->devname == NULL)
			return -ENOMEM;
		sprintf(dev->devname, "%s@%s", "virtio",
			master->unique);
	}
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
