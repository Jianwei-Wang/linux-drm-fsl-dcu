#include <drm/drmP.h>
#include <drm/drm_fb_helper.h>
#include "virtgpu_drv.h"

struct virtgpu_fbdev {
	struct drm_fb_helper helper;
	struct virtgpu_framebuffer	vgfb;
	struct list_head	fbdev_list;
	struct virtgpu_device	*vgdev;
	struct delayed_work work;
};

static struct fb_ops virtgpufb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par, /* TODO: copy vmwgfx */
//	.fb_fillrect = virgl_3d_fillrect,
///	.fb_copyarea = virgl_3d_copyarea,
//	.fb_imageblit = virgl_3d_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};
 
static int virtgpufb_create(struct drm_fb_helper *helper,
			struct drm_fb_helper_surface_size *sizes)
{
	struct virtgpu_fbdev *vfbdev =
		container_of(helper, struct virtgpu_fbdev, helper);
	struct drm_device *dev = helper->dev;
	struct fb_info *info;
	struct drm_framebuffer *fb;
	struct drm_mode_fb_cmd2 mode_cmd = {};
	struct virtgpu_object *obj;
	struct device *device = &dev->pdev->dev;
	int ret;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	mode_cmd.pitches[0] = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);


	info = framebuffer_alloc(0, device);
	if (!info) {
		ret = -ENOMEM;
		goto fail;
	}

	info->par = helper;
	
	ret = virtgpu_framebuffer_init(dev, &vfbdev->vgfb, &mode_cmd, &obj->gem_base);
	if (ret)
		goto fail;

	fb = &vfbdev->vgfb.base;

	vfbdev->helper.fb = fb;
	vfbdev->helper.fbdev = info;
	
	strcpy(info->fix.id, "virtiodrmfb");
	info->flags = FBINFO_DEFAULT;
	info->fbops = &virtgpufb_ops;
	
	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret) {
		ret = -ENOMEM;
		goto fail;
	}

	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
	drm_fb_helper_fill_var(info, &vfbdev->helper, sizes->fb_width, sizes->fb_height);

	return 0;
fail:

	return -EINVAL;
}

static int virtgpu_fbdev_destroy(struct drm_device *dev, struct virtgpu_fbdev *vgfbdev)
{
	struct fb_info *info;
	struct virtgpu_framebuffer *vgfb = &vgfbdev->vgfb;

	if (vgfbdev->helper.fbdev) {
		info = vgfbdev->helper.fbdev;

		unregister_framebuffer(info);
		framebuffer_release(info);
	}
	if (vgfb->obj) {
		vgfb->obj = NULL;
	}
	drm_fb_helper_fini(&vgfbdev->helper);
	drm_framebuffer_cleanup(&vgfb->base);

	return 0;
}
static struct drm_fb_helper_funcs virtgpu_fb_helper_funcs = {
	.fb_probe = virtgpufb_create,
};

int virtgpu_fbdev_init(struct virtgpu_device *vgdev)
{
	struct virtgpu_fbdev *vgfbdev;
	int bpp_sel = 32; /* TODO: parameter from somewhere? */
	int ret;

	vgfbdev = kzalloc(sizeof(struct virtgpu_fbdev), GFP_KERNEL);
	if (!vgfbdev)
		return -ENOMEM;

	vgfbdev->vgdev = vgdev;
	vgdev->vgfbdev = vgfbdev;
	vgfbdev->helper.funcs = &virtgpu_fb_helper_funcs;
	//	INIT_DELAYED_WORK(&vgfbdev->work, virtgpu_fb_dirty_work);

	ret = drm_fb_helper_init(vgdev->ddev, &vgfbdev->helper,
				 1 /* num_crtc - VIRTGPU supports just 1 */,
				 VIRTGPUFB_CONN_LIMIT);
	if (ret) {
		kfree(vgfbdev);
		return ret;
	}

	drm_fb_helper_single_add_all_connectors(&vgfbdev->helper);
	drm_fb_helper_initial_config(&vgfbdev->helper, bpp_sel);
	return 0;
}

void virtgpu_fbdev_fini(struct virtgpu_device *vgdev)
{
	if (!vgdev->vgfbdev)
		return;

	virtgpu_fbdev_destroy(vgdev->ddev, vgdev->vgfbdev);
	kfree(vgdev->vgfbdev);
	vgdev->vgfbdev = NULL;
}
