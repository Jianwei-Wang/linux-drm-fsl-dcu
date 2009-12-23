#include "drmP.h"
#include "drm.h"

#include "qxl_drv.h"

extern int qxl_num_ioctls;
static struct pci_device_id pciidlist[] = {
  { 0x1b36, 0x100, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8,
    0xffff00, 0 },
};

MODULE_DEVICE_TABLE(pci, pciidlist);

static struct drm_driver qxl_driver;

static int __devinit
qxl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return drm_get_dev(pdev, ent, &qxl_driver);
}

static void
qxl_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_put_dev(dev);
}

static struct drm_driver qxl_driver = {
	.driver_features = DRIVER_GEM,
	.dev_priv_size = 0,
	.load = qxl_driver_load,
	.unload = qxl_driver_unload,
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
		.unlocked_ioctl = drm_ioctl,
		.poll = drm_poll,
		.fasync = drm_fasync,
	},

	.pci_driver = {
		 .name = DRIVER_NAME,
		 .id_table = pciidlist,
		 .probe = qxl_pci_probe,
		 .remove = qxl_pci_remove,
	 },

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = 0,
	.minor = 1,
	.patchlevel = 0,
};

static struct drm_driver *driver;

static int __init qxl_init(void)
{
	driver = &qxl_driver;
	driver->num_ioctls = qxl_num_ioctls;
	return drm_init(driver);
}

static void __exit qxl_exit(void)
{
	drm_exit(driver);
}

module_init(qxl_init);
module_exit(qxl_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
