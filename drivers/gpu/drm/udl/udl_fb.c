/*
 * Copyright © 2010 David Airlie
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
#include <linux/slab.h>
#include <linux/fb.h>

#include "drmP.h"
#include "drm.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "udl_drv.h"

#include "drm_fb_helper.h"

struct udl_fbdev {
	struct drm_fb_helper helper;
	struct drm_framebuffer fb;
	struct list_head fbdev_list;
	void *vmalloc;
};

#define BPP                     2
#define DL_ALIGN_UP(x, a) ALIGN(x, a)
#define DL_ALIGN_DOWN(x, a) ALIGN(x-(a-1), a)


int udl_handle_damage(struct udl_fbdev *ufbdev, int x, int y,
	       int width, int height, char *data)
{
	struct drm_device *dev = ufbdev->fb.dev;
	struct udl_device *udl = dev->dev_private;
	struct fb_info *info = 	ufbdev->helper.fbdev;
	int i, ret;
	char *cmd;
	cycles_t start_cycles, end_cycles;
	int bytes_sent = 0;
	int bytes_identical = 0;
	struct urb *urb;
	int aligned_x;

	start_cycles = get_cycles();

	aligned_x = DL_ALIGN_DOWN(x, sizeof(unsigned long));
	width = DL_ALIGN_UP(width + (x-aligned_x), sizeof(unsigned long));
	x = aligned_x;

	if ((width <= 0) ||
	    (x + width > info->var.xres) ||
	    (y + height > info->var.yres))
		return -EINVAL;

//	if (!atomic_read(&dev->usb_active))
//		return 0;

	urb = udl_get_urb(dev);
	if (!urb)
		return 0;
	cmd = urb->transfer_buffer;

	for (i = y; i < y + height ; i++) {
		const int line_offset = info->fix.line_length * i;
		const int byte_offset = line_offset + (x * BPP);

		if (udl_render_hline(dev, &urb,
				      (char *) info->fix.smem_start,
				      &cmd, byte_offset, width * BPP,
				      &bytes_identical, &bytes_sent))
			goto error;
	}

	if (cmd > (char *) urb->transfer_buffer) {
		/* Send partial buffer remaining before exiting */
		int len = cmd - (char *) urb->transfer_buffer;
		ret = udl_submit_urb(dev, urb, len);
		bytes_sent += len;
	} else
		udl_urb_completion(urb);

error:
	atomic_add(bytes_sent, &udl->bytes_sent);
	atomic_add(bytes_identical, &udl->bytes_identical);
	atomic_add(width*height*2, &udl->bytes_rendered);
	end_cycles = get_cycles();
	atomic_add(((unsigned int) ((end_cycles - start_cycles)
		    >> 10)), /* Kcycles */
		   &udl->cpu_kcycles_used);

	return 0;
}


static void udl_fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct udl_fbdev *ufbdev = info->par;

	sys_fillrect(info, rect);

	udl_handle_damage(ufbdev, rect->dx, rect->dy, rect->width,
			  rect->height, info->screen_base);
}

static void udl_fb_copyarea(struct fb_info *info, const struct fb_copyarea *region)
{
	struct udl_fbdev *ufbdev = info->par;

	sys_copyarea(info, region);

	udl_handle_damage(ufbdev, region->dx, region->dy, region->width,
			  region->height, info->screen_base);
}

static void udl_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct udl_fbdev *ufbdev = info->par;

	sys_imageblit(info, image);

	udl_handle_damage(ufbdev, image->dx, image->dy, image->width,
			  image->height, info->screen_base);
}

static struct fb_ops udlfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = udl_fb_fillrect,
	.fb_copyarea = udl_fb_copyarea,
	.fb_imageblit = udl_fb_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

void udl_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			   u16 blue, int regno)
{
}

void udl_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			     u16 *blue, int regno)
{
	*red = 0;
	*green = 0;
	*blue = 0;
}

static void udl_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	drm_framebuffer_cleanup(fb);
	kfree(fb);
}

static const struct drm_framebuffer_funcs udlfb_funcs = {
	.destroy = udl_user_framebuffer_destroy,
	.create_handle = NULL,
};

static int udlfb_create(struct udl_fbdev *ufbdev,
			struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = ufbdev->helper.dev;
	struct fb_info *info;
	struct device *device = &dev->usbdev->dev;
	struct drm_framebuffer *fb;
	struct drm_mode_fb_cmd mode_cmd;
	uint32_t size;
	int ret = 0;

	if (sizes->surface_bpp == 24)
		sizes->surface_bpp = 32;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.bpp = sizes->surface_bpp;
	mode_cmd.pitch = mode_cmd.width * ((mode_cmd.bpp + 1) / 8);
	mode_cmd.depth = sizes->surface_depth;

	size = mode_cmd.pitch * mode_cmd.height;
	size = ALIGN(size, PAGE_SIZE);

	ufbdev->vmalloc = vmalloc(size);
	if (!ufbdev->vmalloc)
		goto out;

	ret = drm_framebuffer_init(dev, &ufbdev->fb, &udlfb_funcs);
	if (ret)
		goto out_vfree;

	drm_helper_mode_fill_fb_struct(&ufbdev->fb, &mode_cmd);

	fb = &ufbdev->fb;

	info = framebuffer_alloc(0, device);
	if (!info) {
		ret = -ENOMEM;
		goto out_vfree;
	}

	info->par = ufbdev;
	ufbdev->helper.fb = fb;
	ufbdev->helper.fbdev = info;

	strcpy(info->fix.id, "udldrmfb");

	info->screen_base = ufbdev->vmalloc;
	info->fix.smem_len = size;
	info->fix.smem_start = (unsigned long)ufbdev->vmalloc;

	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT;
	info->fbops = &udlfb_ops;
	drm_fb_helper_fill_fix(info, fb->pitch, fb->depth);
	drm_fb_helper_fill_var(info, &ufbdev->helper, sizes->fb_width, sizes->fb_height);

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto out_vfree;
	}


	DRM_DEBUG_KMS("allocated %dx%d vmal %p\n",
		      fb->width, fb->height,
		      ufbdev->vmalloc);;

	return ret;
out_vfree:
	vfree(ufbdev->vmalloc);
out:
	return ret;
}

static int udl_fb_find_or_create_single(struct drm_fb_helper *helper,
					  struct drm_fb_helper_surface_size *sizes)
{
	struct udl_fbdev *ufbdev = (struct udl_fbdev *)helper;
	int new_fb = 0;
	int ret;

	if (!helper->fb) {
		ret = udlfb_create(ufbdev, sizes);
		if (ret)
			return ret;

		new_fb = 1;
	}
	return new_fb;
}

static struct drm_fb_helper_funcs udl_fb_helper_funcs = {
	.gamma_set = udl_crtc_fb_gamma_set,
	.gamma_get = udl_crtc_fb_gamma_get,
	.fb_probe = udl_fb_find_or_create_single,
};

static void udl_fbdev_destroy(struct drm_device *dev,
			      struct udl_fbdev *ufbdev)
{
	struct fb_info *info;
	if (ufbdev->helper.fbdev) {
		info = ufbdev->helper.fbdev;
		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}
	drm_fb_helper_fini(&ufbdev->helper);
	drm_framebuffer_cleanup(&ufbdev->fb);
	vfree(ufbdev->vmalloc);
}

int udl_fbdev_init(struct drm_device *dev)
{
	struct udl_device *udl = dev->dev_private;
	int bpp_sel = 16;
	struct udl_fbdev *ufbdev;
	int ret;

	ufbdev = kzalloc(sizeof(struct udl_fbdev), GFP_KERNEL);
	if (!ufbdev)
		return -ENOMEM;

	udl->fbdev = ufbdev;
	ufbdev->helper.funcs = &udl_fb_helper_funcs;

	ret = drm_fb_helper_init(dev, &ufbdev->helper,
				 1, 1);
	if (ret) {
		kfree(ufbdev);
		return ret;

	}

	drm_fb_helper_single_add_all_connectors(&ufbdev->helper);
	drm_fb_helper_initial_config(&ufbdev->helper, bpp_sel);
	return 0;	
}

void udl_fbdev_cleanup(struct drm_device *dev)
{
	struct udl_device *udl = dev->dev_private;
	if (!udl->fbdev)
		return;

	udl_fbdev_destroy(dev, udl->fbdev);
	kfree(udl->fbdev);
	udl->fbdev = NULL;
}
