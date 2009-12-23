
#include "qxl_drv.h"

int qxl_device_init(struct qxl_device *qdev,
		    struct drm_device *ddev,
		    struct pci_dev *pdev,
		    unsigned long flags)
{
	DRM_INFO("qxl: Initialising\n");

	qdev->dev = &pdev->dev;
	qdev->ddev = ddev;
	qdev->pdev = pdev;
	qdev->flags = flags;

//	INIT_LIST_HEAD(&rdev->gem.objects);

	qdev->rom_base = drm_get_resource_start(qdev->ddev, 2);
	qdev->rom_size = drm_get_resource_len(qdev->ddev, 2);

	qdev->rom = ioremap(qdev->rom_base, qdev->rom_size);
	if (!qdev->rom){
		printk(KERN_ERR "Unable to ioremap ROM\n");
		return -ENOMEM;
	}
	return 0;
}

int qxl_device_fini(struct qxl_device *qdev)
{
	iounmap(qdev->rom);
	qdev->rom = NULL;
}

int qxl_driver_unload(struct drm_device *dev)
{
	struct qxl_device *qdev = dev->dev_private;
	
	if (qdev == NULL)
		return 0;
	qxl_modeset_fini(qdev);
	qxl_device_fini(qdev);

	kfree(qdev);
	dev->dev_private = NULL;
	return 0;
}

int qxl_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct qxl_device *qdev;
	int r;

	qdev = kzalloc(sizeof(struct qxl_device), GFP_KERNEL);
	if (qdev == NULL) 
		return -ENOMEM;

	dev->dev_private = qdev;


#if 0
	r = qxl_device_init(qdev, dev, dev->pdev, flags);
	if (r)
		goto out;

	r = qxl_modeset_init(qdev);
out:
	if (r)
		qxl_driver_unload_kms(dev);
#endif
	return r;
}

struct drm_ioctl_desc qxl_ioctls[] = {
	
};

int qxl_max_ioctl = DRM_ARRAY_SIZE(qxl_ioctls);
