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

static struct fb_ops virglfb_ops = {
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
 
static int virtgpufb_create(struct virtgpu_fbdev *qfbdev,
			struct drm_fb_helper_surface_size *sizes)
{
	return -EINVAL;
}

static int virtgpu_fb_find_or_create_single(
		struct drm_fb_helper *helper,
		struct drm_fb_helper_surface_size *sizes)
{
	struct virtgpu_fbdev *vgfbdev = (struct virtgpu_fbdev *)helper;
	int new_fb = 0;
	int ret;

	if (!helper->fb) {
		ret = virtgpufb_create(vgfbdev, sizes);
		if (ret)
			return ret;
		new_fb = 1;
	}
	return new_fb;
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
	.fb_probe = virtgpu_fb_find_or_create_single,
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
