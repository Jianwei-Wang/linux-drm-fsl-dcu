/*
 * Copyright (C) 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef VIRTIO_DRV_H
#define VIRTIO_DRV_H

#define DRIVER_NAME "virtio-gpu"
#define DRIVER_DESC "virtio GPU"
#define DRIVER_DATE ""

#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 1

void virtio_set_driver_bus(struct drm_driver *driver);

struct virtio_gpu_device {
	struct device *dev;
	struct drm_device *ddev;
};

int virtio_gpu_driver_load(struct drm_device *dev, unsigned long flags);
int virtio_gpu_driver_unload(struct drm_device *dev);
#endif
