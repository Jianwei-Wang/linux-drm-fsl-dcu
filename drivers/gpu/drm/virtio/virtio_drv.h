/*
 * Copyright (C) 2012 Red Hat
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef VIRTIO_DRV_H
#define VIRTIO_DRV_H

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_module.h>

#define DRIVER_NAME "virtio-gpu"
#define DRIVER_DESC "virtio GPU"
#define DRIVER_DATE ""

#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 1

#define VIRTGPU_NUM_OUTPUTS 1

void virtio_set_driver_bus(struct drm_driver *driver);

struct virtgpu_bo {
	struct drm_gem_object gem_base;
	struct ttm_buffer_object tbo;
	struct ttm_bo_kmap_obj kmap;
	struct ttm_placement placement;
	u32 placements[3];
	uint32_t hw_res_handle;
};
#define gem_to_virtgpu_bo(gobj) container_of((gobj), struct virtgpu_bo, gem_base)
 
struct virtgpu_crtc {
	struct drm_crtc base;
	int cur_x;
	int cur_y;
};

struct virtgpu_output {
	int index;
	struct drm_connector base;
	struct drm_encoder enc;
};

struct virtgpu_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
	int x1, y1, x2, y2; /* dirty rect */
	spinlock_t dirty_lock;
	uint32_t hw_res_handle;
};
#define to_virtgpu_crtc(x) container_of(x, struct virtgpu_crtc, base)
#define drm_connector_to_virtgpu_output(x) container_of(x, struct virtgpu_output, base)
#define drm_encoder_to_virtgpu_output(x) container_of(x, struct virtgpu_output, base)
#define to_virtgpu_framebuffer(x) container_of(x, struct virtgpu_framebuffer, base)

struct virtgpu_mman {
	struct ttm_bo_global_ref        bo_global_ref;
	struct drm_global_reference	mem_global_ref;
	bool				mem_global_referenced;
	struct ttm_bo_device		bdev;
};

struct virtgpu_fbdev;

struct virtgpu_device {
	struct device *dev;
	struct drm_device *ddev;

	struct virtio_device *vdev;

	struct virtgpu_mman mman;

	/* pointer to fbdev info structure */
	struct virtgpu_fbdev *vgfbdev;
	
	struct virtqueue *ctrlq;
};

int virtgpu_driver_load(struct drm_device *dev, unsigned long flags);
int virtgpu_driver_unload(struct drm_device *dev);

/* virtio_gem.c */
int virtgpu_gem_object_init(struct drm_gem_object *obj);
void virtgpu_gem_object_free(struct drm_gem_object *gobj);
int virtgpu_gem_init(struct virtgpu_device *qdev);
void virtgpu_gem_fini(struct virtgpu_device *qdev);

/* virtio_ttm.c */
int virtgpu_ttm_init(struct virtgpu_device *vgdev);
void virtgpu_ttm_fini(struct virtgpu_device *vgdev);

/* virtio_object */
extern void virtgpu_bo_unref(struct virtgpu_bo **bo);

/* virtio_fb */
#define VIRTGPUFB_CONN_LIMIT 1
int virtgpu_fbdev_init(struct virtgpu_device *vgdev);
void virtgpu_fbdev_fini(struct virtgpu_device *vgdev);
#endif
