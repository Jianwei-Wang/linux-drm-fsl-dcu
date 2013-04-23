/* fbcon support for 3d engine */
/*
 * Copyright © 2013 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *     David Airlie
 */
#include <linux/module.h>
#include <linux/fb.h>

#include "drmP.h"
#include "drm/drm.h"
#include "drm/drm_crtc.h"
#include "drm/drm_crtc_helper.h"
#include "qxl_drv.h"

#include "qxl_object.h"
#include "qxl_3d.h"
#include "drm_fb_helper.h"

struct qxl_fbdev {
	struct drm_fb_helper helper;
	struct qxl_framebuffer	qfb;
	struct list_head	fbdev_list;
	struct qxl_device	*qdev;

	/* dirty memory logging */
	struct {
		spinlock_t lock;
		bool active;
		unsigned x1;
		unsigned y1;
		unsigned x2;
		unsigned y2;
	} dirty;
};

#define DL_ALIGN_UP(x, a) ALIGN(x, a)
#define DL_ALIGN_DOWN(x, a) ALIGN(x-(a-1), a)

static int qxl_dirty_update(struct qxl_framebuffer *fb,
			     int x, int y, int width, int height)
{
	struct drm_device *dev = fb->base.dev;
	struct qxl_device *qdev = dev->dev_private;
	bool store_for_later = false;
	int aligned_x;
	int bpp = (fb->base.bits_per_pixel / 8);
	int x2, y2;
	unsigned long flags;
	struct qxl_bo *qobj = gem_to_qxl_bo(fb->obj);
	int size;
	aligned_x = DL_ALIGN_DOWN(x, sizeof(unsigned long));
	width = DL_ALIGN_UP(width + (x-aligned_x), sizeof(unsigned long));
	x = aligned_x;

	if ((width <= 0) ||
	    (x + width > fb->base.width) ||
	    (y + height > fb->base.height))
		return -EINVAL;

	/* if we are in atomic just store the info
	   can't test inside spin lock */
	if (in_atomic())
		store_for_later = true;

	x2 = x + width - 1;
	y2 = y + height - 1;

	spin_lock_irqsave(&fb->dirty_lock, flags);

	if (fb->y1 < y)
		y = fb->y1;
	if (fb->y2 > y2)
		y2 = fb->y2;
	if (fb->x1 < x)
		x = fb->x1;
	if (fb->x2 > x2)
		x2 = fb->x2;

	if (store_for_later) {
		fb->x1 = x;
		fb->x2 = x2;
		fb->y1 = y;
		fb->y2 = y2;
		spin_unlock_irqrestore(&fb->dirty_lock, flags);
		return 0;
	}

	fb->x1 = fb->y1 = INT_MAX;
	fb->x2 = fb->y2 = 0;

	spin_unlock_irqrestore(&fb->dirty_lock, flags);

	{
		struct qxl_3d_command cmd;
		uint32_t offset;
		cmd.type = QXL_3D_TRANSFER_PUT;
		cmd.u.transfer_put.res_handle = fb->res_3d_handle;

		cmd.u.transfer_put.dst_box.x = x;
		cmd.u.transfer_put.dst_box.y = y;
		cmd.u.transfer_put.dst_box.z = 0;
		cmd.u.transfer_put.dst_box.w = x2 - x + 1;
		cmd.u.transfer_put.dst_box.h = y2 - y + 1;
		cmd.u.transfer_put.dst_box.d = 1;
		
		cmd.u.transfer_put.dst_level = 0;
		cmd.u.transfer_put.src_stride = fb->base.pitches[0];

		offset = (y * fb->base.pitches[0]) + x * bpp;
		cmd.u.transfer_put.data = qxl_3d_bo_addr(qobj, offset);

		qxl_ring_push(qdev->q3d_info.iv3d_ring, &cmd, false);

	}
	qxl_3d_dirty_front(qdev, fb, x, y, x2 - x + 1, y2 - y + 1);
	
	return 0;
}

static int qxl_create_3d_fb_res(struct qxl_device *qdev, struct qxl_framebuffer *fb)
{
	int ret;
	uint32_t res_id;
	struct qxl_3d_command cmd;

	ret = qxl_3d_resource_id_get(qdev, &res_id);
	if (ret)
		return ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.type = QXL_3D_CMD_CREATE_RESOURCE;
	cmd.u.res_create.handle = res_id;
	cmd.u.res_create.target = 2;
	cmd.u.res_create.format = 2;
	cmd.u.res_create.bind = (1 << 1) | (1 << 14);
	cmd.u.res_create.width = fb->base.width;
	cmd.u.res_create.height = fb->base.height;
	cmd.u.res_create.depth = 1;
	qxl_ring_push(qdev->q3d_info.iv3d_ring, &cmd, true);
	
	fb->res_3d_handle = res_id;
	return 0;
}

static void qxl_3d_fillrect(struct fb_info *info,
			    const struct fb_fillrect *rect)
{
	struct qxl_fbdev *qfbdev = info->par;
	sys_fillrect(info, rect);
	qxl_dirty_update(&qfbdev->qfb, rect->dx, rect->dy, rect->width,
			 rect->height);
}

static void qxl_3d_copyarea(struct fb_info *info,
			    const struct fb_copyarea *area)
{
	struct qxl_fbdev *qfbdev = info->par;
	sys_copyarea(info, area);
	qxl_dirty_update(&qfbdev->qfb, area->dx, area->dy,
			 area->width, area->height);
}

static void qxl_3d_imageblit(struct fb_info *info,
			     const struct fb_image *image)
{
	struct qxl_fbdev *qfbdev = info->par;
	sys_imageblit(info, image);
	qxl_dirty_update(&qfbdev->qfb, image->dx, image->dy,
			 image->width, image->height);
}

int qxl_3d_fb_init(struct qxl_device *qdev)
{
	return 0;
}

static struct fb_ops qxlfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par, /* TODO: copy vmwgfx */
	.fb_fillrect = qxl_3d_fillrect,
	.fb_copyarea = qxl_3d_copyarea,
	.fb_imageblit = qxl_3d_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

static void qxlfb_destroy_pinned_object(struct drm_gem_object *gobj)
{
	struct qxl_bo *qbo = gem_to_qxl_bo(gobj);
	int ret;

	ret = qxl_bo_reserve(qbo, false);
	if (likely(ret == 0)) {
		qxl_bo_kunmap(qbo);
		qxl_bo_unpin(qbo);
		qxl_bo_unreserve(qbo);
	}
	drm_gem_object_unreference_unlocked(gobj);
}

static int qxlfb_create_pinned_object(struct qxl_fbdev *qfbdev,
				      struct drm_mode_fb_cmd2 *mode_cmd,
				      struct drm_gem_object **gobj_p)
{
	struct qxl_device *qdev = qfbdev->qdev;
	struct drm_gem_object *gobj = NULL;
	struct qxl_bo *qbo = NULL;
	int ret;
	int aligned_size, size;
	int height = mode_cmd->height;
	int bpp;
	int depth;

	drm_fb_get_bpp_depth(mode_cmd->pixel_format, &bpp, &depth);

	size = mode_cmd->pitches[0] * height;
	aligned_size = ALIGN(size, PAGE_SIZE);
	/* TODO: unallocate and reallocate surface0 for real. Hack to just
	 * have a large enough surface0 for 1024x768 Xorg 32bpp mode */
	ret = qxl_gem_object_create(qdev, aligned_size, 0,
				    QXL_GEM_DOMAIN_3D,
				    false, /* is discardable */
				    false, /* is kernel (false means device) */
				    NULL,
				    &gobj);
	if (ret) {
		pr_err("failed to allocate framebuffer (%d)\n",
		       aligned_size);
		return -ENOMEM;
	}
	qbo = gem_to_qxl_bo(gobj);

	ret = qxl_bo_reserve(qbo, false);
	if (unlikely(ret != 0))
		goto out_unref;
	ret = qxl_bo_pin(qbo, QXL_GEM_DOMAIN_3D, NULL);
	if (ret) {
		qxl_bo_unreserve(qbo);
		goto out_unref;
	}
	ret = qxl_bo_kmap(qbo, NULL);
	qxl_bo_unreserve(qbo); /* unreserve, will be mmaped */
	if (ret)
		goto out_unref;

	*gobj_p = gobj;
	return 0;
out_unref:
	qxlfb_destroy_pinned_object(gobj);
	*gobj_p = NULL;
	return ret;
}

static int qxlfb_create(struct qxl_fbdev *qfbdev,
			struct drm_fb_helper_surface_size *sizes)
{
	struct qxl_device *qdev = qfbdev->qdev;
	struct fb_info *info;
	struct drm_framebuffer *fb = NULL;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_gem_object *gobj = NULL;
	struct qxl_bo *qbo = NULL;
	struct device *device = &qdev->pdev->dev;
	int ret;
	int size;
	int bpp = sizes->surface_bpp;
	int depth = sizes->surface_depth;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width * ((bpp + 1) / 8), 64);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(bpp, depth);

	ret = qxlfb_create_pinned_object(qfbdev, &mode_cmd, &gobj);
	qbo = gem_to_qxl_bo(gobj);

	size = mode_cmd.pitches[0] * mode_cmd.height;

	info = framebuffer_alloc(0, device);
	if (info == NULL) {
		ret = -ENOMEM;
		goto out_unref;
	}

	info->par = qfbdev;

	qxl_framebuffer_init(qdev->ddev, &qfbdev->qfb, &mode_cmd, gobj);

	fb = &qfbdev->qfb.base;

	ret = qxl_create_3d_fb_res(qdev, &qfbdev->qfb);

	/* setup helper with fb data */
	qfbdev->helper.fb = fb;
	qfbdev->helper.fbdev = info;
	strcpy(info->fix.id, "qxldrmfb");

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);

	info->flags = FBINFO_DEFAULT;
	info->fbops = &qxlfb_ops;

	/*
	 * TODO: using gobj->size in various places in this function. Not sure
	 * what the difference between the different sizes is.
	 */
	info->screen_base = qbo->kptr;
	info->screen_size = gobj->size;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	drm_fb_helper_fill_var(info, &qfbdev->helper, sizes->fb_width,
			       sizes->fb_height);

	/* setup aperture base/size for vesafb takeover */
	info->apertures = alloc_apertures(1);
	if (!info->apertures) {
		ret = -ENOMEM;
		goto out_unref;
	}
	info->apertures->ranges[0].base = qdev->ddev->mode_config.fb_base;
	info->apertures->ranges[0].size = qdev->vram_size;

	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;

	if (info->screen_base == NULL) {
		ret = -ENOSPC;
		goto out_unref;
	}

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto out_unref;
	}

	qdev->fbdev_info = info;
	qdev->fbdev_qfb = &qfbdev->qfb;
	DRM_INFO("fb mappable at 0x%lX, size %lu\n",  info->fix.smem_start, (unsigned long)info->screen_size);
	DRM_INFO("fb: depth %d, pitch %d, width %d, height %d\n", fb->depth, fb->pitches[0], fb->width, fb->height);
	return 0;

out_unref:
	if (fb && ret) {
		drm_framebuffer_cleanup(fb);
		kfree(fb);
	}
	return ret;
}

static int qxl_fb_find_or_create_single(
		struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	struct qxl_fbdev *qfbdev = (struct qxl_fbdev *)helper;
	int new_fb = 0;
	int ret;

	if (!helper->fb) {
		ret = qxlfb_create(qfbdev, sizes);
		if (ret)
			return ret;
		new_fb = 1;
	}
	return new_fb;
}

static int qxl_fbdev_destroy(struct drm_device *dev, struct qxl_fbdev *qfbdev)
{
	struct fb_info *info;
	struct qxl_framebuffer *qfb = &qfbdev->qfb;

	if (qfbdev->helper.fbdev) {
		info = qfbdev->helper.fbdev;

		unregister_framebuffer(info);
		framebuffer_release(info);
	}
	if (qfb->obj) {
		qfb->obj = NULL;
	}
	drm_fb_helper_fini(&qfbdev->helper);
	drm_framebuffer_cleanup(&qfb->base);

	return 0;
}

static struct drm_fb_helper_funcs qxl_fb_helper_funcs = {
	/* TODO
	.gamma_set = qxl_crtc_fb_gamma_set,
	.gamma_get = qxl_crtc_fb_gamma_get,
	*/
	.fb_probe = qxl_fb_find_or_create_single,
};


int qxl_3d_fbdev_init(struct qxl_device *qdev)
{
	struct qxl_fbdev *qfbdev;
	int bpp_sel = 32; /* TODO: parameter from somewhere? */
	int ret;

	qfbdev = kzalloc(sizeof(struct qxl_fbdev), GFP_KERNEL);
	if (!qfbdev)
		return -ENOMEM;

	qfbdev->qdev = qdev;
	qdev->mode_info.qfbdev = qfbdev;
	qfbdev->helper.funcs = &qxl_fb_helper_funcs;

	ret = drm_fb_helper_init(qdev->ddev, &qfbdev->helper,
				 1 /* num_crtc - QXL supports just 1 */,
				 QXLFB_CONN_LIMIT);
	if (ret) {
		kfree(qfbdev);
		return ret;
	}

	drm_fb_helper_single_add_all_connectors(&qfbdev->helper);
	drm_fb_helper_initial_config(&qfbdev->helper, bpp_sel);
	return 0;
}

void qxl_3d_fbdev_fini(struct qxl_device *qdev)
{
	if (!qdev->mode_info.qfbdev)
		return;

	qxl_fbdev_destroy(qdev->ddev, qdev->mode_info.qfbdev);
	kfree(qdev->mode_info.qfbdev);
	qdev->mode_info.qfbdev = NULL;
}

