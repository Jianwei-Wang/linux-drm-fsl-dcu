#include "qxl_drv.h"
#include "drm_crtc_helper.h"

static int qxl_add_common_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode = NULL;
	int i;
	struct mode_size {
		int w;
		int h;
	} common_modes[17] = {
		{ 640,  480},
		{ 720,  480},
		{ 800,  600},
		{ 848,  480},
		{1024,  768},
		{1152,  768},
		{1280,  720},
		{1280,  800},
		{1280,  854},
		{1280,  960},
		{1280, 1024},
		{1440,  900},
		{1400, 1050},
		{1680, 1050},
		{1600, 1200},
		{1920, 1080},
		{1920, 1200}
	};

	for (i = 0; i < ARRAY_SIZE(common_modes); i++) {
		if (common_modes[i].w < 320 || common_modes[i].h < 200)
			continue;

		mode = drm_cvt_mode(dev, common_modes[i].w, common_modes[i].h, 60, false, false, false);
		drm_mode_probed_add(connector, mode);
	}
	return i - 1;
}

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
		qxl_gem_object_unpin(qxl_fb->obj);
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

static void qxl_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

static bool qxl_crtc_mode_fixup(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int qxl_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_mode *m = (void *)mode->private;

	if (!m)
		return -1;

	DRM_ERROR("%dx%d: qxl id %d\n", mode->hdisplay, mode->vdisplay, m->id);
	

	outb(0, qdev->io_base + QXL_IO_RESET);
	outb(m->id, qdev->io_base + QXL_IO_SET_MODE);
	return 0;
}

static int
qxl_crtc_set_base(struct drm_crtc *crtc, int x, int y,
		  struct drm_framebuffer *old_fb)
{
	return 0;
}

static void qxl_crtc_prepare (struct drm_crtc *crtc)
{
}

static void qxl_crtc_commit (struct drm_crtc *crtc)
{
}

void qxl_crtc_load_lut(struct drm_crtc *crtc)
{
}

static const struct drm_crtc_helper_funcs qxl_crtc_helper_funcs = {
	.dpms = qxl_crtc_dpms,
	.mode_fixup = qxl_crtc_mode_fixup,
	.mode_set = qxl_crtc_mode_set,
	.mode_set_base = qxl_crtc_set_base,
	.prepare = qxl_crtc_prepare,
	.commit = qxl_crtc_commit,
	.load_lut = qxl_crtc_load_lut,
};

int qdev_crtc_init(struct drm_device *dev, int num_crtc)
{
	struct qdev_device *qdev = dev->dev_private;
	struct qxl_crtc *qxl_crtc;

	qxl_crtc = kzalloc(sizeof(struct qxl_crtc), GFP_KERNEL);
	if (!qxl_crtc)
		return -ENOMEM;

	drm_crtc_init(dev, &qxl_crtc->base, &qxl_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&qxl_crtc->base, 256);
	drm_crtc_helper_add(&qxl_crtc->base, &qxl_crtc_helper_funcs);
	return 0;
}

static void qxl_enc_dpms(struct drm_encoder* encoder, int mode)
{
}

static bool qxl_enc_mode_fixup(struct drm_encoder *encoder,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void qxl_enc_prepare(struct drm_encoder *encoder)
{
}

static void qxl_enc_commit( struct drm_encoder *encoder)
{
}

static void qxl_enc_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	DRM_ERROR("setting mode\n");
}

static struct qxl_mode *
qxl_find_native_mode(struct qxl_device *qdev, struct drm_display_mode *mode,
		     int bpp)
{
	int i;

	for (i = 0; i < qdev->mode_info.num_modes; i++) {
		struct qxl_mode *m = qdev->mode_info.modes + i;
		if (m->x_res == mode->hdisplay &&
		    m->y_res == mode->vdisplay &&
		    m->bits == bpp)
			return m;
	}
	return NULL;
}

static int qxl_conn_get_modes(struct drm_connector *connector)
{
	int ret;
	ret = qxl_add_common_modes(connector);
	return ret;
}

static int qxl_conn_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	struct drm_device *dev = connector->dev;
	struct qxl_device *qdev = dev->dev_private;

	mode->private = (int *)qxl_find_native_mode(qdev, mode, 32);
	if (!mode->private)
		return MODE_NOMODE;

	return MODE_OK;
}

struct drm_encoder *qxl_best_encoder(struct drm_connector *connector)
{
	struct qxl_output *qxl_output = to_qxl_output(connector);

	return &qxl_output->enc;
}


static const struct drm_encoder_helper_funcs qxl_enc_helper_funcs = {
	.dpms = qxl_enc_dpms,
	.mode_fixup = qxl_enc_mode_fixup,
	.prepare = qxl_enc_prepare,
	.mode_set = qxl_enc_mode_set,
	.commit = qxl_enc_commit,	
};

static const struct drm_connector_helper_funcs qxl_connector_helper_funcs = {
	.get_modes = qxl_conn_get_modes,
	.mode_valid = qxl_conn_mode_valid,
	.best_encoder = qxl_best_encoder,
};

static void qxl_conn_save(struct drm_connector *connector)
{
}

static void qxl_conn_restore(struct drm_connector *connector)
{
}

static enum drm_connector_status qxl_conn_detect(struct drm_connector *connector)
{
	return connector_status_connected;
}

static int qxl_conn_set_property(struct drm_connector *connector,
				   struct drm_property *property,
				   uint64_t value)
{
	return 0;
}

static void qxl_conn_destroy(struct drm_connector *connector)
{
	struct qxl_output *qxl_output = to_qxl_output(connector);

	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(qxl_output);
}

static const struct drm_connector_funcs qxl_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = qxl_conn_save,
	.restore = qxl_conn_restore,
	.detect = qxl_conn_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = qxl_conn_set_property,
	.destroy = qxl_conn_destroy,
};

static void qxl_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs qxl_enc_funcs = {
	.destroy = qxl_enc_destroy,
};

int qdev_output_init(struct drm_device *dev, int num_output)
{
	struct qxl_output *qxl_output;
	struct drm_connector *connector;
	struct drm_encoder *encoder;

	qxl_output = kzalloc(sizeof(struct qxl_output), GFP_KERNEL);
	if (!qxl_output)
		return -ENOMEM;

	connector = &qxl_output->base;
	encoder = &qxl_output->enc;
	drm_connector_init(dev, &qxl_output->base,
			   &qxl_connector_funcs, DRM_MODE_CONNECTOR_LVDS);

	drm_encoder_init(dev, &qxl_output->enc, &qxl_enc_funcs,
			 DRM_MODE_ENCODER_LVDS);

	encoder->possible_crtcs = 0x1;
	drm_mode_connector_attach_encoder(&qxl_output->base,
					  &qxl_output->enc);
	drm_encoder_helper_add(encoder, &qxl_enc_helper_funcs);
	drm_connector_helper_add(connector, &qxl_connector_helper_funcs);
       
	drm_sysfs_connector_add(connector);
	return 0;
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

	qdev->ddev->mode_config.fb_base = qdev->vram_base;
	qdev_crtc_init(qdev->ddev, 1);

	qdev_output_init(qdev->ddev, 1);

	qdev->mode_info.mode_config_initialized = true;

	drm_helper_initial_config(qdev->ddev);

	return 0;
}

void qxl_modeset_fini(struct qxl_device *qdev)
{
	if (qdev->mode_info.mode_config_initialized) {
		drm_mode_config_cleanup(qdev->ddev);
		qdev->mode_info.mode_config_initialized = false;
	}
	
}
