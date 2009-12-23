#include "qxl_drv.h"
#include "drm_crtc_helper.h"

void radeon_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			      u16 *blue, int regno)
{
}
static void qxl_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				  u16 *blue, uint32_t size)
{
}
static void qxl_crtc_destroy(struct drm_crtc *crtc)
{
	struct qxl_crtc *qxl_crtc = to_qxl_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(qxl_crtc);
}

static const struct drm_crtc_funcs qxl_crtc_funcs = {
//	.cursor_set = qxl_crtc_cursor_set,
//	.cursor_move = qxl_crtc_cursor_move,
	.gamma_set = qxl_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = qxl_crtc_destroy,
};

static void qxl_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct qxl_framebuffer *qxl_fb = to_qxl_framebuffer(fb);
	struct drm_device *dev = fb->dev;

	if (fb->fbdev)
		qxlfb_remove(dev, fb);

	if (qxl_fb->obj) {
//		qxl_gem_object_unpin(qxl_fb->obj);
		mutex_lock(&dev->struct_mutex);
		drm_gem_object_unreference(qxl_fb->obj);
		mutex_unlock(&dev->struct_mutex);
	}
	drm_framebuffer_cleanup(fb);
	kfree(qxl_fb);
}

static const struct drm_framebuffer_funcs qxl_fb_funcs = {
	.destroy = qxl_user_framebuffer_destroy,
//	.create_handle = qxl_user_framebuffer_create_handle,
};

struct drm_framebuffer *
qxl_framebuffer_create(struct drm_device *dev,
		       struct drm_mode_fb_cmd *mode_cmd,
		       struct drm_gem_object *obj)
{
	struct qxl_framebuffer *qxl_fb;

	qxl_fb = kzalloc(sizeof(*qxl_fb), GFP_KERNEL);
	if (qxl_fb == NULL) {
		return NULL;
	}
	drm_framebuffer_init(dev, &qxl_fb->base, &qxl_fb_funcs);
	drm_helper_mode_fill_fb_struct(&qxl_fb->base, mode_cmd);
	qxl_fb->obj = obj;
	return &qxl_fb->base;
}

int qdev_crtc_init(struct drm_device *dev, int num_crtc)
{
	struct qdev_device *qdev = dev->dev_private;
	struct qxl_crtc *qxl_crtc;

	qxl_crtc = kzalloc(sizeof(struct qxl_crtc), GFP_KERNEL);
	if (!qxl_crtc)
		return -ENOMEM;

	drm_crtc_init(dev, &qxl_crtc->base, &qxl_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&qxl_crtc->base, 256);
}

static struct drm_framebuffer *
qxl_user_framebuffer_create(struct drm_device *dev,
			    struct drm_file *file_priv,
			    struct drm_mode_fb_cmd *mode_cmd)
{
	struct drm_gem_object *obj;
	
	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handle);

	return qxl_framebuffer_create(dev, mode_cmd, obj);
}

static const struct drm_mode_config_funcs qxl_mode_funcs = {
	.fb_create = qxl_user_framebuffer_create,
	.fb_changed = qxlfb_probe,
};

int qxl_modeset_init(struct qxl_device *qdev)
{
	drm_mode_config_init(qdev->ddev);
	
	qdev->ddev->mode_config.funcs = &qxl_mode_funcs;

	qdev->ddev->mode_config.max_width = 2048;
	qdev->ddev->mode_config.max_height = 2048;

	qdev_crtc_init(qdev->ddev, 1);

//	qdev_output_init(qdev->ddev, 1);

	drm_helper_initial_config(qdev->ddev);
	return 0;
}

int qxl_modeset_fini(struct qxl_device *qdev)
{
	if (qdev->mode_info.mode_config_initialized) {
		drm_mode_config_cleanup(qdev->ddev);
		qdev->mode_info.mode_config_initialized = false;
	}
}
