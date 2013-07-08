/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alon Levy
 */


#ifndef VIRGL_DRV_H
#define VIRGL_DRV_H

/*
 * Definitions taken from spice-protocol, plus kernel driver specific bits.
 */

#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>

#include "drmP.h"
#include "drm_crtc.h"
#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_module.h>

#include <drm/virgl_drm.h>

#include "virgl_hw.h"

#define DRIVER_AUTHOR		"Dave Airlie"

#define DRIVER_NAME		"virgl"
#define DRIVER_DESC		"RH VIRGL"
#define DRIVER_DATE		"20120117"

#define DRIVER_MAJOR 0
#define DRIVER_MINOR 1
#define DRIVER_PATCHLEVEL 0

#define VIRGL_NUM_OUTPUTS 1

#define VIRGL_DEBUGFS_MAX_COMPONENTS		32

struct virgl_bo {
	/* Protected by gem.mutex */
	struct list_head		list;
	/* Protected by tbo.reserved */
	u32				placements[3];
	struct ttm_placement		placement;
	struct ttm_buffer_object	tbo;
	struct ttm_bo_kmap_obj		kmap;
	unsigned			pin_count;
	void				*kptr;
	int                             type;
	/* Constant after initialization */
	struct drm_gem_object		gem_base;
	struct sg_table *sgt;
};
#define gem_to_virgl_bo(gobj) container_of((gobj), struct virgl_bo, gem_base)

struct virgl_gem {
	spinlock_t lock;
	struct list_head	objects;
};

struct virgl_crtc {
	struct drm_crtc base;
	int cur_x;
	int cur_y;
};

struct virgl_output {
	int index;
	struct drm_connector base;
	struct drm_encoder enc;
};

struct virgl_framebuffer {
	struct drm_framebuffer base;
	struct drm_gem_object *obj;
	int x1, y1, x2, y2; /* dirty rect */
	spinlock_t dirty_lock;
	uint32_t res_3d_handle;
};

#define to_virgl_crtc(x) container_of(x, struct virgl_crtc, base)
#define drm_connector_to_virgl_output(x) container_of(x, struct virgl_output, base)
#define drm_encoder_to_virgl_output(x) container_of(x, struct virgl_output, base)
#define to_virgl_framebuffer(x) container_of(x, struct virgl_framebuffer, base)

struct virgl_fence_driver {
	atomic64_t last_seq;
	uint64_t last_activity;
	bool initialized;
	uint64_t			sync_seq;
};

struct virgl_fence {
	struct virgl_device *qdev;
	struct kref kref;
	uint64_t seq;
};

struct virgl_vbuffer {
	char *buf;
	size_t size;

	size_t bo_max_len;
	size_t bo_start_offset;
	size_t bo_user_offset;
	struct virgl_bo *bo;

	bool inout;
	int sgpages;
	int firstsg;
	struct scatterlist sg[0];
};

struct virgl_mman {
	struct ttm_bo_global_ref        bo_global_ref;
	struct drm_global_reference	mem_global_ref;
	bool				mem_global_referenced;
	struct ttm_bo_device		bdev;
};

struct virgl_mode_info {
	int num_modes;
	struct virgl_mode *modes;
	bool mode_config_initialized;

	/* pointer to fbdev info structure */
	struct virgl_fbdev *qfbdev;
};

struct virgl_fb_image {
	struct virgl_device *qdev;
	uint32_t pseudo_palette[16];
	struct fb_image fb_image;
	uint32_t visual;
};

/*
 * Debugfs
 */
struct virgl_debugfs {
	struct drm_info_list	*files;
	unsigned		num_files;
};

int virgl_debugfs_add_files(struct virgl_device *rdev,
			     struct drm_info_list *files,
			     unsigned nfiles);
int virgl_debugfs_fence_init(struct virgl_device *rdev);

struct virgl_device {
	struct device			*dev;
	struct drm_device		*ddev;
	struct pci_dev			*pdev;
	unsigned long flags;

	struct virtio_device vdev;

	struct virgl_mode *modes;

	int io_base;
	struct virgl_mman		mman;
	struct virgl_gem		gem;
	struct virgl_mode_info mode_info;

	struct fb_info			*fbdev_info;
	struct virgl_framebuffer	*fbdev_qfb;

	atomic_t irq_count_vbuf;
	atomic_t irq_count_fence;

	/* debugfs */
	struct virgl_debugfs	debugfs[VIRGL_DEBUGFS_MAX_COMPONENTS];
	unsigned		debugfs_count;

	struct virgl_fence_driver fence_drv;
	wait_queue_head_t		fence_queue;

	struct idr	resource_idr;
	spinlock_t resource_idr_lock;

	/* virt io info */
	void __iomem *ioaddr;
	struct virtqueue *cmdq;
	int cmd_num;
	void *cmdqueue;
	spinlock_t cmdq_lock;
	wait_queue_head_t cmd_ack_queue;
	struct work_struct dequeue_work;

	uint32_t num_alloc, num_freed;
};

#define vdev_to_virgl_dev(virt) container_of((virt), struct virgl_device, vdev)

extern struct drm_ioctl_desc virgl_ioctls[];
extern int virgl_max_ioctl;

int virgl_driver_load(struct drm_device *dev, unsigned long flags);
int virgl_driver_unload(struct drm_device *dev);

int virgl_modeset_init(struct virgl_device *qdev);
void virgl_modeset_fini(struct virgl_device *qdev);

int virgl_bo_init(struct virgl_device *qdev);
void virgl_bo_fini(struct virgl_device *qdev);

/* virgl_fb.c */
#define VIRGLFB_CONN_LIMIT 1

int virgl_fbdev_init(struct virgl_device *qdev);
void virgl_fbdev_fini(struct virgl_device *qdev);
int virgl_get_handle_for_primary_fb(struct virgl_device *qdev,
				  struct drm_file *file_priv,
				  uint32_t *handle);

/* virgl_display.c */
int
virgl_framebuffer_init(struct drm_device *dev,
		     struct virgl_framebuffer *rfb,
		     struct drm_mode_fb_cmd2 *mode_cmd,
		     struct drm_gem_object *obj, uint32_t res_handle);

/* virgl_gem.c */
int virgl_gem_init(struct virgl_device *qdev);
void virgl_gem_fini(struct virgl_device *qdev);
int virgl_gem_object_create(struct virgl_device *qdev, int size,
			  int alignment, int initial_domain,
			  bool discardable, bool kernel,
			  struct drm_gem_object **obj);
int virgl_gem_object_pin(struct drm_gem_object *obj, uint32_t pin_domain,
			  uint64_t *gpu_addr);
void virgl_gem_object_unpin(struct drm_gem_object *obj);
int virgl_gem_object_create_with_handle(struct virgl_device *qdev,
				      struct drm_file *file_priv,
				      u32 domain,
				      size_t size,
				      struct virgl_bo **qobj,
				      uint32_t *handle);
int virgl_gem_object_init(struct drm_gem_object *obj);
void virgl_gem_object_free(struct drm_gem_object *gobj);
int virgl_gem_object_open(struct drm_gem_object *obj, struct drm_file *file_priv);
void virgl_gem_object_close(struct drm_gem_object *obj,
			  struct drm_file *file_priv);
void virgl_bo_force_delete(struct virgl_device *qdev);
int virgl_bo_kmap(struct virgl_bo *bo, void **ptr);

/* virgl_dumb.c */
int virgl_mode_dumb_create(struct drm_file *file_priv,
			 struct drm_device *dev,
			 struct drm_mode_create_dumb *args);
int virgl_mode_dumb_destroy(struct drm_file *file_priv,
			  struct drm_device *dev,
			  uint32_t handle);
int virgl_mode_dumb_mmap(struct drm_file *filp,
		       struct drm_device *dev,
		       uint32_t handle, uint64_t *offset_p);


/* virgl ttm */
int virgl_ttm_init(struct virgl_device *qdev);
void virgl_ttm_fini(struct virgl_device *qdev);
int virgl_mmap(struct file *filp, struct vm_area_struct *vma);

int virgl_debugfs_init(struct drm_minor *minor);
void virgl_debugfs_takedown(struct drm_minor *minor);

/* virgl_irq.c */
int virgl_irq_init(struct virgl_device *qdev);
irqreturn_t virgl_irq_handler(DRM_IRQ_ARGS);

/* virgl_fb.c */
int virgl_fb_init(struct virgl_device *qdev);

int virgl_debugfs_add_files(struct virgl_device *qdev,
			  struct drm_info_list *files,
			  unsigned nfiles);

int virgl_execbuffer(struct drm_device *dev,
		     struct drm_virgl_execbuffer *execbuffer,
		     struct drm_file *drm_file);
int virgl_virtio_init(struct virgl_device *qdev);
void virgl_virtio_fini(struct virgl_device *qdev);
int virgl_fence_emit(struct virgl_device *qdev,
		      struct virgl_command *cmd,
		      struct virgl_fence **fence);
int virgl_wait(struct virgl_bo *bo, bool no_wait);
int virgl_resource_id_get(struct virgl_device *qdev, uint32_t *resid);
void virgl_resource_id_put(struct virgl_device *qdev, uint32_t id);

int virgl_3d_fbdev_init(struct virgl_device *qdev);
void virgl_3d_fbdev_fini(struct virgl_device *qdev);
int virgl_3d_set_front(struct virgl_device *qdev,
		     struct virgl_framebuffer *fb, int x, int y,
		     int width, int height);
int virgl_3d_dirty_front(struct virgl_device *qdev,
		       struct virgl_framebuffer *fb, int x, int y,
		       int width, int height);
int virgl_3d_surface_dirty(struct virgl_framebuffer *qfb, struct drm_clip_rect *clips,
			 unsigned num_clips);


struct virgl_command *virgl_alloc_cmd_buf(struct virgl_device *qdev,
					  struct virgl_bo *qobj,
					  bool inout,
					  u32 *base_offset,
					  u32 max_bo_len,
					  struct virgl_vbuffer **vbuffer_p);
int virgl_queue_cmd_buf(struct virgl_device *qdev, struct virgl_vbuffer *buf);

static inline struct virgl_command *virgl_alloc_cmd(struct virgl_device *qdev,
						    struct virgl_bo *bo,
						    bool inout,
						    u32 *base_offset, /* can be modified */
						    u32 max_bo_len,
						    struct virgl_vbuffer **vbuf)
{
	struct virgl_command *cmd = virgl_alloc_cmd_buf(qdev, bo, inout, base_offset, max_bo_len, vbuf);
	if (!IS_ERR(cmd)) {
		memset(cmd, 0, sizeof(struct virgl_command));
	}
	return cmd;
}

extern struct virgl_bo *virgl_bo_ref(struct virgl_bo *bo);

/* virgl fence */
struct virgl_fence *virgl_fence_ref(struct virgl_fence *fence);
void virgl_fence_unref(struct virgl_fence **fence);
 
bool virgl_fence_signaled(struct virgl_fence *fence);
int virgl_fence_wait(struct virgl_fence *fence, bool interruptible);
void virgl_fence_process(struct virgl_device *qdev);

#define VIRGL_FENCE_SIGNALED_SEQ 0LL
#define VIRGL_FENCE_JIFFIES_TIMEOUT		(HZ / 2)


u32 virgl_fence_read(struct virgl_device *qdev);

void virgl_dequeue_work_func(struct work_struct *work);
#endif
