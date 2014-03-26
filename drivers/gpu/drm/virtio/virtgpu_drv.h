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

#include "virtgpu_hw.h"
#define DRIVER_NAME "virtio-gpu"
#define DRIVER_DESC "virtio GPU"
#define DRIVER_DATE "0"

#define DRIVER_MAJOR 0
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 1

void virtio_set_driver_bus(struct drm_driver *driver);

struct virtgpu_object {
	struct drm_gem_object gem_base;
	uint32_t hw_res_handle;

	struct sg_table *pages;
	void *vmap;
	bool dumb;
	u32				placement_code;
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
};
#define gem_to_virtgpu_obj(gobj) container_of((gobj), struct virtgpu_object, gem_base)

struct virtgpu_vbuffer;
struct virtgpu_device;

typedef void (*virtgpu_resp_cb)(struct virtgpu_device *vgdev,
				struct virtgpu_vbuffer *vbuf);

#define VIRTGPU_FENCE_SIGNALED_SEQ 0LL
#define VIRTGPU_FENCE_JIFFIES_TIMEOUT		(HZ / 2)

struct virtgpu_fence_driver {
	atomic64_t last_seq;
        unsigned long last_activity;
	bool initialized;
	uint64_t			sync_seq;
       
	spinlock_t event_lock;
	struct list_head event_list;
	uint64_t first_seq_event_list;
};

struct virtgpu_fence {
	struct virtgpu_device *vgdev;
	struct kref kref;
	uint64_t seq;
};

struct virtgpu_vbuffer {
	char *buf;
	int size;

	int resp_size;
	char *resp_buf;

	virtgpu_resp_cb resp_cb;

	void *vaddr;
	uint32_t vaddr_len;
	struct list_head destroy_list;
};

struct virtgpu_crtc {
	struct drm_crtc base;
	int idx;
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

struct virtgpu_queue {
	struct virtqueue *vq;
	spinlock_t qlock;
      	wait_queue_head_t ack_queue;
	struct work_struct dequeue_work;
};

struct virtgpu_device {
	struct device *dev;
	struct drm_device *ddev;

	struct virtio_device *vdev;

	struct virtgpu_mman mman;

	/* pointer to fbdev info structure */
	struct virtgpu_fbdev *vgfbdev;

	struct virtgpu_queue ctrlq;
	struct virtgpu_queue cursorq;
	struct virtgpu_queue fenceq;

	void *fence_page;
	struct idr	resource_idr;
	spinlock_t resource_idr_lock;

	int num_outputs;

	wait_queue_head_t resp_wq;
	/* current display info */
	spinlock_t display_info_lock;
	struct virtgpu_display_info display_info;

	int num_hw_scanouts;

	struct virtgpu_fence_driver fence_drv;
	wait_queue_head_t		fence_queue;

	struct idr	ctx_id_idr;
	spinlock_t ctx_id_idr_lock;

	union virtgpu_caps caps;
	bool has_fence;
	bool has_virgl_3d;

	struct virtgpu_hw_cursor_page cursor_info;

	struct work_struct config_changed_work;
};

struct virtgpu_fpriv {
	uint32_t ctx_id;
};

extern struct drm_ioctl_desc virtgpu_ioctls[];

int virtgpu_driver_load(struct drm_device *dev, unsigned long flags);
int virtgpu_driver_unload(struct drm_device *dev);
int virtgpu_driver_open(struct drm_device *dev, struct drm_file *file);
void virtgpu_driver_postclose(struct drm_device *dev, struct drm_file *file);
/* virtio_gem.c */
int virtgpu_gem_init_object(struct drm_gem_object *obj);
void virtgpu_gem_free_object(struct drm_gem_object *gem_obj);
int virtgpu_gem_init(struct virtgpu_device *vgdev);
void virtgpu_gem_fini(struct virtgpu_device *vgdev);
int virtgpu_gem_create(struct drm_file *file,
		       struct drm_device *dev,
		       uint64_t size,
		       struct drm_gem_object **obj_p,
		       uint32_t *handle_p);
int virtgpu_gem_object_open(struct drm_gem_object *obj,
			    struct drm_file *file);
void virtgpu_gem_object_close(struct drm_gem_object *obj,
			      struct drm_file *file);
struct virtgpu_object *virtgpu_alloc_object(struct drm_device *dev,
					    size_t size, bool kernel, bool pinned);
int virtgpu_mode_dumb_create(struct drm_file *file_priv,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args);
int virtgpu_mode_dumb_destroy(struct drm_file *file_priv,
			      struct drm_device *dev,
			      uint32_t handle);
int virtgpu_mode_dumb_mmap(struct drm_file *file_priv,
			   struct drm_device *dev,
			   uint32_t handle, uint64_t *offset_p);

/* virtio_fb */
#define VIRTGPUFB_CONN_LIMIT 1
int virtgpu_fbdev_init(struct virtgpu_device *vgdev);
void virtgpu_fbdev_fini(struct virtgpu_device *vgdev);
int virtgpu_surface_dirty(struct virtgpu_framebuffer *qfb,
			  struct drm_clip_rect *clips,
			  unsigned num_clips);
/* virtio vg */
int virtgpu_resource_id_get(struct virtgpu_device *vgdev, uint32_t *resid);
void virtgpu_resource_id_put(struct virtgpu_device *vgdev, uint32_t id);
int virtgpu_cmd_create_resource(struct virtgpu_device *vgdev,
				uint32_t resource_id,
				uint32_t format,
				uint32_t width,
				uint32_t height);
int virtgpu_cmd_unref_resource(struct virtgpu_device *vgdev,
			       uint32_t resource_id);
int virtgpu_cmd_transfer_to_host_2d(struct virtgpu_device *vgdev,
				 uint32_t resource_id, uint32_t offset,
				 uint32_t width, uint32_t height,
				    uint32_t x, uint32_t y, struct virtgpu_fence **fence);
int virtgpu_cmd_resource_flush(struct virtgpu_device *vgdev,
			       uint32_t resource_id,
			       uint32_t x, uint32_t y,
			       uint32_t width, uint32_t height);
int virtgpu_cmd_set_scanout(struct virtgpu_device *vgdev,
			    uint32_t scanout_id, uint32_t resource_id,
			    uint32_t width, uint32_t height,
			    uint32_t x, uint32_t y);
int virtgpu_object_attach(struct virtgpu_device *vgdev, struct virtgpu_object *obj, uint32_t resource_id, struct virtgpu_fence **fence);
int virtgpu_attach_status_page(struct virtgpu_device *vgdev);
int virtgpu_detach_status_page(struct virtgpu_device *vgdev);
void virtgpu_cursor_ping(struct virtgpu_device *vgdev);
int virtgpu_cmd_get_display_info(struct virtgpu_device *vgdev);
int virtgpu_cmd_get_3d_caps(struct virtgpu_device *vgdev);
int virtgpu_fill_fence_vq(struct virtgpu_device *vgdev, int entries);
int virtgpu_cmd_context_create(struct virtgpu_device *vgdev, uint32_t id,
			       uint32_t nlen, const char *name);
int virtgpu_cmd_context_destroy(struct virtgpu_device *vgdev, uint32_t id);
int virtgpu_cmd_context_attach_resource(struct virtgpu_device *vgdev, uint32_t ctx_id,
					uint32_t resource_id);
int virtgpu_cmd_context_detach_resource(struct virtgpu_device *vgdev, uint32_t ctx_id,
					uint32_t resource_id);
int virtgpu_cmd_submit(struct virtgpu_device *vgdev, void *vaddr,
		       uint32_t size, uint32_t ctx_id,
		       struct virtgpu_fence **fence);
int virtgpu_cmd_transfer_from_host_3d(struct virtgpu_device *vgdev, uint32_t resource_id,
				      uint32_t ctx_id, uint64_t offset, uint32_t level, struct virtgpu_box *box,
				      struct virtgpu_fence **fence);
int virtgpu_cmd_transfer_to_host_3d(struct virtgpu_device *vgdev, uint32_t resource_id,
				    uint32_t ctx_id,
				    uint64_t offset, uint32_t level, struct virtgpu_box *box,
				    struct virtgpu_fence **fence);
int virtgpu_cmd_resource_create_3d(struct virtgpu_device *vgdev,
				   struct virtgpu_resource_create_3d *rc_3d,
				   struct virtgpu_fence **fence);
int virtgpu_cmd_resource_inval_backing(struct virtgpu_device *vgdev,
				       uint32_t resource_id);
void virtgpu_ctrl_ack(struct virtqueue *vq);
void virtgpu_cursor_ack(struct virtqueue *vq);
void virtgpu_fence_ack(struct virtqueue *vq);
void virtgpu_dequeue_ctrl_func(struct work_struct *work);
void virtgpu_dequeue_cursor_func(struct work_struct *work);
void virtgpu_dequeue_fence_func(struct work_struct *work);

/* virtgpu_display.c */
int virtgpu_framebuffer_init(struct drm_device *dev,
			     struct virtgpu_framebuffer *vgfb,
			     struct drm_mode_fb_cmd2 *mode_cmd,
			     struct drm_gem_object *obj);
int virtgpu_modeset_init(struct virtgpu_device *vgdev);
void virtgpu_modeset_fini(struct virtgpu_device *vgdev);

/* virtgpu_ttm.c */
int virtgpu_ttm_init(struct virtgpu_device *vgdev);
void virtgpu_ttm_fini(struct virtgpu_device *vgdev);
bool virtgpu_ttm_bo_is_virtgpu_object(struct ttm_buffer_object *bo);
int virtgpu_mmap(struct file *filp, struct vm_area_struct *vma);

/* virtgpu_fence.c */
int virtgpu_fence_wait(struct virtgpu_fence *fence, bool intr);
int virtgpu_fence_emit(struct virtgpu_device *vgdev,
		      struct virtgpu_cmd_hdr *cmd_hdr,
		       struct virtgpu_fence **fence);
void virtgpu_fence_process(struct virtgpu_device *vgdev);
void virtgpu_fence_unref(struct virtgpu_fence **fence);
struct virtgpu_fence *virtgpu_fence_ref(struct virtgpu_fence *fence);
bool virtgpu_fence_signaled(struct virtgpu_fence *fence, bool process);
u32 virtgpu_fence_read(struct virtgpu_device *vgdev);

/* virtgpu_object */
int virtgpu_object_create(struct virtgpu_device *vgdev,
			  unsigned long size, bool kernel, bool pinned,
			  struct virtgpu_object **bo_ptr);
int virtgpu_object_kmap(struct virtgpu_object *bo, void **ptr);
int virtgpu_object_get_sg_table(struct virtgpu_device *qdev,
				struct virtgpu_object *bo);
void virtgpu_object_free_sg_table(struct virtgpu_object *bo);
int virtgpu_object_wait(struct virtgpu_object *bo, bool no_wait);
static inline struct virtgpu_object *virtgpu_object_ref(struct virtgpu_object *bo)
{
	ttm_bo_reference(&bo->tbo);
	return bo;
}

static inline void virtgpu_object_unref(struct virtgpu_object **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;
	tbo = &((*bo)->tbo);
	ttm_bo_unref(&tbo);
	if (tbo == NULL)
		*bo = NULL;
}

static inline u64 virtgpu_object_mmap_offset(struct virtgpu_object *bo)
{
        return drm_vma_node_offset_addr(&bo->tbo.vma_node);
}

static inline int virtgpu_object_reserve(struct virtgpu_object *bo, bool no_wait)
{
	int r;

	r = ttm_bo_reserve(&bo->tbo, true, no_wait, false, 0);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS) {
			struct virtgpu_device *qdev = (struct virtgpu_device *)bo->gem_base.dev->dev_private;
			dev_err(qdev->dev, "%p reserve failed\n", bo);
		}
		return r;
	}
	return 0;
}

static inline void virtgpu_object_unreserve(struct virtgpu_object *bo)
{
	ttm_bo_unreserve(&bo->tbo);
}

/* virgl debufs */
int virtgpu_debugfs_init(struct drm_minor *minor);
void virtgpu_debugfs_takedown(struct drm_minor *minor);

#define VIRTIO_GPU_F_FENCE 0 /* the host has fencing available for accelerators */
#define VIRTIO_GPU_F_VIRGL 1 /* the host has the virgl 3d accelerator available */
#endif
