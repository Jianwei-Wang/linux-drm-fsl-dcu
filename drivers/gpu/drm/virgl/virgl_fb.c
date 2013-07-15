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
#include "virgl_drv.h"

#include "virgl_object.h"
#include "drm_fb_helper.h"

struct virgl_fbdev {
	struct drm_fb_helper helper;
	struct virgl_framebuffer	qfb;
	struct list_head	fbdev_list;
	struct virgl_device	*qdev;
};

#define DL_ALIGN_UP(x, a) ALIGN(x, a)
#define DL_ALIGN_DOWN(x, a) ALIGN(x-(a-1), a)

static int virgl_dirty_update(struct virgl_framebuffer *fb,
			     int x, int y, int width, int height)
{
	struct drm_device *dev = fb->base.dev;
	struct virgl_device *qdev = dev->dev_private;
	bool store_for_later = false;
	int aligned_x;
	int bpp = (fb->base.bits_per_pixel / 8);
	int x2, y2;
	unsigned long flags;
	struct virgl_bo *qobj = gem_to_virgl_bo(fb->obj);
	int size;
	aligned_x = DL_ALIGN_DOWN(x, sizeof(unsigned long));
	width = DL_ALIGN_UP(width + (x-aligned_x), sizeof(unsigned long));
	x = aligned_x;

	if ((width <= 0) ||
	    (x + width > fb->base.width) ||
	    (y + height > fb->base.height)) {
		printk("values out of range %d %d %dx%d %dx%d\n", x, y, width, height, fb->base.width, fb->base.height);
		return -EINVAL;
	}

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
		struct virgl_command *cmd_p;
		struct virgl_vbuffer *vbuf;
		uint32_t offset;
		uint32_t max_len;
		uint32_t w = x2 - x + 1;
		uint32_t h = y2 - y + 1;

		max_len = w * bpp + h * fb->base.pitches[0];
		offset = (y * fb->base.pitches[0]) + x * bpp;
		cmd_p = virgl_alloc_cmd(qdev, qobj, false, &offset, max_len, &vbuf);
		if (IS_ERR(cmd_p)) {
			printk("failed to allocate cmd for transfer\n");
			return -EINVAL;
		}

		cmd_p->type = VIRGL_TRANSFER_PUT;
		cmd_p->u.transfer_put.res_handle = fb->res_3d_handle;

		cmd_p->u.transfer_put.dst_box.x = x;
		cmd_p->u.transfer_put.dst_box.y = y;
		cmd_p->u.transfer_put.dst_box.z = 0;
		cmd_p->u.transfer_put.dst_box.w = w;
		cmd_p->u.transfer_put.dst_box.h = h;
		cmd_p->u.transfer_put.dst_box.d = 1;
		
		cmd_p->u.transfer_put.dst_level = 0;
		cmd_p->u.transfer_put.src_stride = fb->base.pitches[0];

		cmd_p->u.transfer_put.data = offset;

		virgl_queue_cmd_buf(qdev, vbuf);
	}
	virgl_3d_dirty_front(qdev, fb, x, y, x2 - x + 1, y2 - y + 1);
	
	return 0;
}

int virgl_3d_surface_dirty(struct virgl_framebuffer *qfb, struct drm_clip_rect *clips,
				unsigned num_clips)
{
	struct virgl_device *qdev = qfb->base.dev->dev_private;

	struct drm_clip_rect norect;
	struct drm_clip_rect *clips_ptr;
	int left, right, top, bottom;
	int i;
	int inc = 1;
	if (!num_clips) {
		num_clips = 1;
		clips = &norect;
		norect.x1 = norect.y1 = 0;
		norect.x2 = qfb->base.width;
		norect.y2 = qfb->base.height;
	}
	left = clips->x1;
	right = clips->x2;
	top = clips->y1;
	bottom = clips->y2;

	/* skip the first clip rect */
	for (i = 1, clips_ptr = clips + inc;
	     i < num_clips; i++, clips_ptr += inc) {
		left = min_t(int, left, (int)clips_ptr->x1);
		right = max_t(int, right, (int)clips_ptr->x2);
		top = min_t(int, top, (int)clips_ptr->y1);
		bottom = max_t(int, bottom, (int)clips_ptr->y2);
	}

	if (qfb->obj)
		virgl_dirty_update(qfb, left, top, right - left, bottom - top);
	else
		virgl_3d_dirty_front(qdev, qfb, left, top, right - left, bottom - top);

	return 0;
}

int virgl_create_3d_fb_res(struct virgl_device *qdev, int width, int height, uint32_t *handle)
{
	int ret;
	uint32_t res_id;
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;

	ret = virgl_resource_id_get(qdev, &res_id);
	if (ret)
		return ret;

	cmd_p = virgl_alloc_cmd(qdev, NULL, false, NULL, 0, &vbuf);
	cmd_p->type = VIRGL_CMD_CREATE_RESOURCE;
	cmd_p->u.res_create.handle = res_id;
	cmd_p->u.res_create.target = 2;
	cmd_p->u.res_create.format = 2;
	cmd_p->u.res_create.bind = (1 << 1) | (1 << 14);
	cmd_p->u.res_create.width = width;
	cmd_p->u.res_create.height = height;
	cmd_p->u.res_create.depth = 1;
	virgl_queue_cmd_buf(qdev, vbuf);
	*handle = res_id;
	return 0;
}

static void virgl_3d_fillrect(struct fb_info *info,
			    const struct fb_fillrect *rect)
{
	struct virgl_fbdev *qfbdev = info->par;
	sys_fillrect(info, rect);
	virgl_dirty_update(&qfbdev->qfb, rect->dx, rect->dy, rect->width,
			 rect->height);
}

static void virgl_3d_copyarea(struct fb_info *info,
			    const struct fb_copyarea *area)
{
	struct virgl_fbdev *qfbdev = info->par;
	sys_copyarea(info, area);
	virgl_dirty_update(&qfbdev->qfb, area->dx, area->dy,
			 area->width, area->height);
}

static void virgl_3d_imageblit(struct fb_info *info,
			     const struct fb_image *image)
{
	struct virgl_fbdev *qfbdev = info->par;
	sys_imageblit(info, image);
	virgl_dirty_update(&qfbdev->qfb, image->dx, image->dy,
			 image->width, image->height);
}

int virgl_fb_init(struct virgl_device *qdev)
{
	return 0;
}

static struct fb_ops virglfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par, /* TODO: copy vmwgfx */
	.fb_fillrect = virgl_3d_fillrect,
	.fb_copyarea = virgl_3d_copyarea,
	.fb_imageblit = virgl_3d_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

static void virglfb_destroy_pinned_object(struct drm_gem_object *gobj)
{
	struct virgl_bo *qbo = gem_to_virgl_bo(gobj);
	int ret;

	ret = virgl_bo_reserve(qbo, false);
	if (likely(ret == 0)) {
		virgl_bo_kunmap(qbo);
		virgl_bo_unpin(qbo);
		virgl_bo_unreserve(qbo);
	}
	drm_gem_object_unreference_unlocked(gobj);
}

static int virglfb_create_pinned_object(struct virgl_fbdev *qfbdev,
				      struct drm_mode_fb_cmd2 *mode_cmd,
				      struct drm_gem_object **gobj_p)
{
	struct virgl_device *qdev = qfbdev->qdev;
	struct drm_gem_object *gobj = NULL;
	struct virgl_bo *qbo = NULL;
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
	ret = virgl_gem_object_create(qdev, aligned_size, 0,
				    0,
				    false, /* is discardable */
				    false, /* is kernel (false means device) */
				    &gobj);
	if (ret) {
		pr_err("failed to allocate framebuffer (%d)\n",
		       aligned_size);
		return -ENOMEM;
	}
	qbo = gem_to_virgl_bo(gobj);

	ret = virgl_bo_reserve(qbo, false);
	if (unlikely(ret != 0))
		goto out_unref;
	ret = virgl_bo_pin(qbo, 0, NULL);
	if (ret) {
		virgl_bo_unreserve(qbo);
		goto out_unref;
	}
	ret = virgl_bo_kmap(qbo, NULL);
	virgl_bo_unreserve(qbo); /* unreserve, will be mmaped */
	if (ret)
		goto out_unref;

	*gobj_p = gobj;
	return 0;
out_unref:
	virglfb_destroy_pinned_object(gobj);
	*gobj_p = NULL;
	return ret;
}

static int virglfb_create(struct virgl_fbdev *qfbdev,
			struct drm_fb_helper_surface_size *sizes)
{
	struct virgl_device *qdev = qfbdev->qdev;
	struct fb_info *info;
	struct drm_framebuffer *fb = NULL;
	struct drm_mode_fb_cmd2 mode_cmd;
	struct drm_gem_object *gobj = NULL;
	struct virgl_bo *qbo = NULL;
	struct device *device = &qdev->pdev->dev;
	int ret;
	int size;
	int bpp = sizes->surface_bpp;
	int depth = sizes->surface_depth;
	uint32_t res_handle;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;

	mode_cmd.pitches[0] = ALIGN(mode_cmd.width * ((bpp + 1) / 8), 64);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(bpp, depth);

	ret = virglfb_create_pinned_object(qfbdev, &mode_cmd, &gobj);
	qbo = gem_to_virgl_bo(gobj);

	size = mode_cmd.pitches[0] * mode_cmd.height;

	info = framebuffer_alloc(0, device);
	if (info == NULL) {
		ret = -ENOMEM;
		goto out_unref;
	}

	info->par = qfbdev;

	ret = virgl_create_3d_fb_res(qdev, mode_cmd.width, mode_cmd.height, &qbo->res_handle);

	if (ret) {
		goto out_unref;
	}
	virgl_framebuffer_init(qdev->ddev, &qfbdev->qfb, &mode_cmd, gobj, 0);

	fb = &qfbdev->qfb.base;


	/* setup helper with fb data */
	qfbdev->helper.fb = fb;
	qfbdev->helper.fbdev = info;
	strcpy(info->fix.id, "virgldrmfb");

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);

	info->flags = FBINFO_DEFAULT;
	info->fbops = &virglfb_ops;

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
	info->apertures->ranges[0].size = gobj->size;

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

static int virgl_fb_find_or_create_single(
		struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	struct virgl_fbdev *qfbdev = (struct virgl_fbdev *)helper;
	int new_fb = 0;
	int ret;

	if (!helper->fb) {
		ret = virglfb_create(qfbdev, sizes);
		if (ret)
			return ret;
		new_fb = 1;
	}
	return new_fb;
}

static int virgl_fbdev_destroy(struct drm_device *dev, struct virgl_fbdev *qfbdev)
{
	struct fb_info *info;
	struct virgl_framebuffer *qfb = &qfbdev->qfb;

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

static struct drm_fb_helper_funcs virgl_fb_helper_funcs = {
	/* TODO
	.gamma_set = virgl_crtc_fb_gamma_set,
	.gamma_get = virgl_crtc_fb_gamma_get,
	*/
	.fb_probe = virgl_fb_find_or_create_single,
};


int virgl_fbdev_init(struct virgl_device *qdev)
{
	struct virgl_fbdev *qfbdev;
	int bpp_sel = 32; /* TODO: parameter from somewhere? */
	int ret;

	qfbdev = kzalloc(sizeof(struct virgl_fbdev), GFP_KERNEL);
	if (!qfbdev)
		return -ENOMEM;

	qfbdev->qdev = qdev;
	qdev->mode_info.qfbdev = qfbdev;
	qfbdev->helper.funcs = &virgl_fb_helper_funcs;

	ret = drm_fb_helper_init(qdev->ddev, &qfbdev->helper,
				 1 /* num_crtc - VIRGL supports just 1 */,
				 VIRGLFB_CONN_LIMIT);
	if (ret) {
		kfree(qfbdev);
		return ret;
	}

	drm_fb_helper_single_add_all_connectors(&qfbdev->helper);
	drm_fb_helper_initial_config(&qfbdev->helper, bpp_sel);
	return 0;
}

void virgl_fbdev_fini(struct virgl_device *qdev)
{
	if (!qdev->mode_info.qfbdev)
		return;

	virgl_fbdev_destroy(qdev->ddev, qdev->mode_info.qfbdev);
	kfree(qdev->mode_info.qfbdev);
	qdev->mode_info.qfbdev = NULL;
}

