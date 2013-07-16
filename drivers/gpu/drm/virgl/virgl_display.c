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


#include "linux/crc32.h"

#include "virgl_drv.h"
#include "virgl_object.h"
#include "drm_crtc_helper.h"

static int virgl_add_common_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode = NULL;
	int i;
	struct mode_size {
		int w;
		int h;
	} common_modes[] = {
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

		mode = drm_cvt_mode(dev, common_modes[i].w, common_modes[i].h,
				    60, false, false, false);
		if (common_modes[i].w == 1024 && common_modes[i].h == 768)
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
	}
	return i - 1;
}

static void virgl_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
			       u16 *blue, uint32_t start, uint32_t size)
{
	/* TODO */
}

static void virgl_crtc_destroy(struct drm_crtc *crtc)
{
	struct virgl_crtc *virgl_crtc = to_virgl_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(virgl_crtc);
}

static void
virgl_hide_cursor(struct virgl_device *qdev)
{
	iowrite32(0, qdev->ioaddr + VIRTIO_VIRGL_CURSOR_ID);
}

static int virgl_crtc_cursor_set(struct drm_crtc *crtc,
			       struct drm_file *file_priv,
			       uint32_t handle,
			       uint32_t width,
			       uint32_t height)
{
	struct virgl_device *qdev = crtc->dev->dev_private;
	struct drm_gem_object *gobj = NULL;
	struct virgl_bo *qobj = NULL;
	int ret = 0;
	if (handle == 0) {
		virgl_hide_cursor(qdev);
		return 0;
	}

	/* lookup the cursor */
	gobj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virgl_bo(gobj);

	if (!qobj->res_handle) {
		ret = -EINVAL;
		goto out;
	}

	{
		struct virgl_command *cmd_p;
		struct virgl_vbuffer *vbuf;
		uint32_t offset = 0;

		cmd_p = virgl_alloc_cmd(qdev, qobj, false, &offset, 0, &vbuf);
		if (IS_ERR(cmd_p)) {
			printk("failed to allocate cmd for transfer\n");
			return -EINVAL;
		}
		
		cmd_p->type = VIRGL_TRANSFER_PUT;
		cmd_p->u.transfer_put.res_handle = qobj->res_handle;
		cmd_p->u.transfer_put.ctx_id = 0;
		cmd_p->u.transfer_put.dst_box.x = 0;
		cmd_p->u.transfer_put.dst_box.y = 0;
		cmd_p->u.transfer_put.dst_box.z = 0;
		cmd_p->u.transfer_put.dst_box.w = 64;
		cmd_p->u.transfer_put.dst_box.h = 64;
		cmd_p->u.transfer_put.dst_box.d = 1;
		
		cmd_p->u.transfer_put.dst_level = 0;
		cmd_p->u.transfer_put.src_stride = 0;

		cmd_p->u.transfer_put.data = offset;

		virgl_queue_cmd_buf(qdev, vbuf);
	}

	iowrite32(qobj->res_handle, qdev->ioaddr + VIRTIO_VIRGL_CURSOR_ID);
	iowrite32(0, qdev->ioaddr + VIRTIO_VIRGL_CURSOR_HOT_X_Y);

out:
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int virgl_crtc_cursor_move(struct drm_crtc *crtc,
				int x, int y)
{
	struct virgl_device *qdev = crtc->dev->dev_private;
	iowrite32(x, qdev->ioaddr + VIRTIO_VIRGL_CURSOR_CUR_X);
	iowrite32(y, qdev->ioaddr + VIRTIO_VIRGL_CURSOR_CUR_Y);
	return 0;
}


static const struct drm_crtc_funcs virgl_crtc_funcs = {
	.cursor_set = virgl_crtc_cursor_set,
	.cursor_move = virgl_crtc_cursor_move,
	.gamma_set = virgl_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = virgl_crtc_destroy,
};

static void virgl_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct virgl_framebuffer *virgl_fb = to_virgl_framebuffer(fb);

	if (virgl_fb->obj)
		drm_gem_object_unreference_unlocked(virgl_fb->obj);
	drm_framebuffer_cleanup(fb);
	kfree(virgl_fb);
}

int virgl_framebuffer_surface_dirty(struct drm_framebuffer *fb,
				  struct drm_file *file_priv,
				  unsigned flags, unsigned color,
				  struct drm_clip_rect *clips,
				  unsigned num_clips)
{
	return virgl_3d_surface_dirty(to_virgl_framebuffer(fb), clips, num_clips);

}

static const struct drm_framebuffer_funcs virgl_fb_funcs = {
	.destroy = virgl_user_framebuffer_destroy,
	.dirty = virgl_framebuffer_surface_dirty,
};

int
virgl_framebuffer_init(struct drm_device *dev,
		     struct virgl_framebuffer *qfb,
		     struct drm_mode_fb_cmd2 *mode_cmd,
		       struct drm_gem_object *obj)
{
	int ret;
	struct virgl_bo *bo;
	qfb->obj = obj;

	bo = gem_to_virgl_bo(obj);
	qfb->res_3d_handle = bo->res_handle;

	ret = drm_framebuffer_init(dev, &qfb->base, &virgl_fb_funcs);
	if (ret) {
		qfb->obj = NULL;
		return ret;
	}
	drm_helper_mode_fill_fb_struct(&qfb->base, mode_cmd);

	qfb->x1 = qfb->y1 = INT_MAX;
	qfb->x2 = qfb->y2 = 0;
	return 0;
}

static void virgl_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

static bool virgl_crtc_mode_fixup(struct drm_crtc *crtc,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	struct drm_device *dev = crtc->dev;
	struct virgl_device *qdev = dev->dev_private;

	return true;
}

static int virgl_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct virgl_device *qdev = dev->dev_private;
	struct virgl_framebuffer *qfb;
	struct virgl_bo *bo, *old_bo = NULL;
	int ret;

	if (!crtc->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	if (old_fb) {
		qfb = to_virgl_framebuffer(old_fb);
		old_bo = gem_to_virgl_bo(qfb->obj);
	}
	qfb = to_virgl_framebuffer(crtc->fb);
	bo = gem_to_virgl_bo(qfb->obj);
	DRM_DEBUG("+%d+%d (%d,%d) => (%d,%d)\n",
		  x, y,
		  mode->hdisplay, mode->vdisplay,
		  adjusted_mode->hdisplay,
		  adjusted_mode->vdisplay);

	virgl_3d_set_front(qdev, qfb, x, y, mode->hdisplay, mode->vdisplay);

	return 0;
}

static void virgl_crtc_prepare(struct drm_crtc *crtc)
{
	DRM_DEBUG("current: %dx%d+%d+%d (%d).\n",
		  crtc->mode.hdisplay, crtc->mode.vdisplay,
		  crtc->x, crtc->y, crtc->enabled);
}

static void virgl_crtc_commit(struct drm_crtc *crtc)
{
	DRM_DEBUG("\n");
}

void virgl_crtc_load_lut(struct drm_crtc *crtc)
{
	DRM_DEBUG("\n");
}

static const struct drm_crtc_helper_funcs virgl_crtc_helper_funcs = {
	.dpms = virgl_crtc_dpms,
	.mode_fixup = virgl_crtc_mode_fixup,
	.mode_set = virgl_crtc_mode_set,
	.prepare = virgl_crtc_prepare,
	.commit = virgl_crtc_commit,
	.load_lut = virgl_crtc_load_lut,
};

int qdev_crtc_init(struct drm_device *dev, int num_crtc)
{
	struct virgl_crtc *virgl_crtc;

	virgl_crtc = kzalloc(sizeof(struct virgl_crtc), GFP_KERNEL);
	if (!virgl_crtc)
		return -ENOMEM;

	drm_crtc_init(dev, &virgl_crtc->base, &virgl_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&virgl_crtc->base, 256);
	drm_crtc_helper_add(&virgl_crtc->base, &virgl_crtc_helper_funcs);
	return 0;
}

static void virgl_enc_dpms(struct drm_encoder *encoder, int mode)
{
	DRM_DEBUG("\n");
}

static bool virgl_enc_mode_fixup(struct drm_encoder *encoder,
			       const struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG("\n");
	return true;
}

static void virgl_enc_prepare(struct drm_encoder *encoder)
{
	DRM_DEBUG("\n");
}

static void virgl_enc_commit(struct drm_encoder *encoder)
{
	DRM_DEBUG("\n");
}

static void virgl_enc_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG("\n");
}

static int virgl_conn_get_modes(struct drm_connector *connector)
{
	int ret = 0;
	struct virgl_device *qdev = connector->dev->dev_private;

	ret += virgl_add_common_modes(connector);
	return ret;
}

static int virgl_conn_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	return MODE_OK;
}

struct drm_encoder *virgl_best_encoder(struct drm_connector *connector)
{
	struct virgl_output *virgl_output =
		drm_connector_to_virgl_output(connector);

	DRM_DEBUG("\n");
	return &virgl_output->enc;
}


static const struct drm_encoder_helper_funcs virgl_enc_helper_funcs = {
	.dpms = virgl_enc_dpms,
	.mode_fixup = virgl_enc_mode_fixup,
	.prepare = virgl_enc_prepare,
	.mode_set = virgl_enc_mode_set,
	.commit = virgl_enc_commit,
};

static const struct drm_connector_helper_funcs virgl_connector_helper_funcs = {
	.get_modes = virgl_conn_get_modes,
	.mode_valid = virgl_conn_mode_valid,
	.best_encoder = virgl_best_encoder,
};

static void virgl_conn_save(struct drm_connector *connector)
{
	DRM_DEBUG("\n");
}

static void virgl_conn_restore(struct drm_connector *connector)
{
	DRM_DEBUG("\n");
}

static enum drm_connector_status virgl_conn_detect(
			struct drm_connector *connector,
			bool force)
{
	struct virgl_output *output =
		drm_connector_to_virgl_output(connector);
	struct drm_device *ddev = connector->dev;
	struct virgl_device *qdev = ddev->dev_private;
	int connected;

	return connector_status_connected;
}

static int virgl_conn_set_property(struct drm_connector *connector,
				   struct drm_property *property,
				   uint64_t value)
{
	DRM_DEBUG("\n");
	return 0;
}

static void virgl_conn_destroy(struct drm_connector *connector)
{
	struct virgl_output *virgl_output =
		drm_connector_to_virgl_output(connector);

	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(virgl_output);
}

static const struct drm_connector_funcs virgl_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = virgl_conn_save,
	.restore = virgl_conn_restore,
	.detect = virgl_conn_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = virgl_conn_set_property,
	.destroy = virgl_conn_destroy,
};

static void virgl_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs virgl_enc_funcs = {
	.destroy = virgl_enc_destroy,
};

int qdev_output_init(struct drm_device *dev, int num_output)
{
	struct virgl_output *virgl_output;
	struct drm_connector *connector;
	struct drm_encoder *encoder;

	virgl_output = kzalloc(sizeof(struct virgl_output), GFP_KERNEL);
	if (!virgl_output)
		return -ENOMEM;

	virgl_output->index = num_output;

	connector = &virgl_output->base;
	encoder = &virgl_output->enc;
	drm_connector_init(dev, &virgl_output->base,
			   &virgl_connector_funcs, DRM_MODE_CONNECTOR_VIRTUAL);

	drm_encoder_init(dev, &virgl_output->enc, &virgl_enc_funcs,
			 DRM_MODE_ENCODER_VIRTUAL);

	encoder->possible_crtcs = 1 << num_output;
	drm_mode_connector_attach_encoder(&virgl_output->base,
					  &virgl_output->enc);
	drm_encoder_helper_add(encoder, &virgl_enc_helper_funcs);
	drm_connector_helper_add(connector, &virgl_connector_helper_funcs);

	drm_sysfs_connector_add(connector);
	return 0;
}

static struct drm_framebuffer *
virgl_user_framebuffer_create(struct drm_device *dev,
			    struct drm_file *file_priv,
			    struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj = NULL;
	struct virgl_framebuffer *virgl_fb;
	struct virgl_device *qdev = dev->dev_private;
	int ret;

	/* lookup object associated with res handle */
	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);
	if (!obj)
		return PTR_ERR(-EINVAL);

	virgl_fb = kzalloc(sizeof(*virgl_fb), GFP_KERNEL);
	if (virgl_fb == NULL)
		return PTR_ERR(-ENOMEM);

	ret = virgl_framebuffer_init(dev, virgl_fb, mode_cmd, obj);
	if (ret) {
		kfree(virgl_fb);
		if (obj)
			drm_gem_object_unreference_unlocked(obj);
		return NULL;
	}

	return &virgl_fb->base;
}

static const struct drm_mode_config_funcs virgl_mode_funcs = {
	.fb_create = virgl_user_framebuffer_create,
};

int virgl_modeset_init(struct virgl_device *qdev)
{
	int i;
	int ret;
	struct drm_gem_object *gobj;
	int max_allowed = VIRGL_NUM_OUTPUTS;

	drm_mode_config_init(qdev->ddev);
	qdev->ddev->mode_config.funcs = (void *)&virgl_mode_funcs;

	/* modes will be validated against the framebuffer size */
	qdev->ddev->mode_config.min_width = 320;
	qdev->ddev->mode_config.min_height = 200;
	qdev->ddev->mode_config.max_width = 8192;
	qdev->ddev->mode_config.max_height = 8192;

//	qdev->ddev->mode_config.fb_base = qdev->vram_base;
	for (i = 0 ; i < VIRGL_NUM_OUTPUTS; ++i) {
		qdev_crtc_init(qdev->ddev, i);
		qdev_output_init(qdev->ddev, i);
	}

	qdev->mode_info.mode_config_initialized = true;

	/* primary surface must be created by this point, to allow
	 * issuing command queue commands and having them read by
	 * spice server. */
	virgl_fbdev_init(qdev);
	return 0;
}

void virgl_modeset_fini(struct virgl_device *qdev)
{
	virgl_fbdev_fini(qdev);
	if (qdev->mode_info.mode_config_initialized) {
		drm_mode_config_cleanup(qdev->ddev);
		qdev->mode_info.mode_config_initialized = false;
	}
}
