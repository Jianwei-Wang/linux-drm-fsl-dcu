/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <drm/drmP.h>
#include <drm/drm_fb_cma_helper.h>

#include "fsl_dcu_drm_drv.h"

/* initialize fbdev helper */
void fsl_dcu_fbdev_init(struct drm_device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev_get_drvdata(dev->dev);

	fsl_dev->fbdev = drm_fbdev_cma_init(dev, 24, 1, 1);
}
