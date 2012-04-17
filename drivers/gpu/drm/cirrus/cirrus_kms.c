/*
 * Copyright 2010 Matt Turner.
 * Copyright 2011 Red Hat <mjg@redhat.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *			Matt Turner
 */
#include "drmP.h"
#include "drm.h"

#include "cirrus.h"
#include "cirrus_drv.h"

/*
 * Functions here will be called by the core once it's bound the driver to
 * a PCI device
 */

int cirrus_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct cirrus_device *cdev;
	int r;

	cdev = kzalloc(sizeof(struct cirrus_device), GFP_KERNEL);
	if (cdev == NULL)
		return -ENOMEM;
	dev->dev_private = (void *)cdev;

	r = cirrus_device_init(cdev, dev, dev->pdev, flags);
	if (r) {
		dev_err(&dev->pdev->dev, "Fatal error during GPU init: %d\n", r);
		goto out;
	}

	r = cirrus_modeset_init(cdev);
	if (r)
		dev_err(&dev->pdev->dev, "Fatal error during modeset init: %d\n", r);
out:
	if (r)
		cirrus_driver_unload(dev);
	return r;
}

int cirrus_driver_unload(struct drm_device *dev)
{
	struct cirrus_device *cdev = dev->dev_private;

	if (cdev == NULL)
		return 0;
	cirrus_modeset_fini(cdev);
	cirrus_device_fini(cdev);
	kfree(cdev);
	dev->dev_private = NULL;
	return 0;
}
