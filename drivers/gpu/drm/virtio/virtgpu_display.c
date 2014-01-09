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

#include "virtgpu_drv.h"
#include "drm_crtc_helper.h"

static int virtgpu_add_common_modes(struct drm_connector *connector, unsigned pwidth,
				    unsigned pheight)
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
		if (common_modes[i].w == pwidth && common_modes[i].h == pheight)
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
	}
	return i - 1;
}

static void virtgpu_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
			       u16 *blue, uint32_t start, uint32_t size)
{
	/* TODO */
}

static void virtgpu_crtc_destroy(struct drm_crtc *crtc)
{
	struct virtgpu_crtc *virtgpu_crtc = to_virtgpu_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(virtgpu_crtc);
}

static void
virtgpu_hide_cursor(struct virtgpu_device *vgdev)
{
	{
		struct virtgpu_hw_cursor_page *cursor_page = &vgdev->cursor_info;
		cursor_page->cursor_id = 0;
		cursor_page->generation_count++;
	}
	virtgpu_cursor_ping(vgdev);
}

static int virtgpu_crtc_cursor_set(struct drm_crtc *crtc,
				   struct drm_file *file_priv,
				   uint32_t handle,
				   uint32_t width,
				   uint32_t height, int32_t hot_x, int32_t hot_y)
{
	struct virtgpu_device *vgdev = crtc->dev->dev_private;
	struct drm_gem_object *gobj = NULL;
	struct virtgpu_object *qobj = NULL;
	struct virtgpu_fence *fence = NULL;
	int ret = 0;
	if (handle == 0) {
		virtgpu_hide_cursor(vgdev);
		return 0;
	}

	/* lookup the cursor */
	gobj = drm_gem_object_lookup(crtc->dev, file_priv, handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtgpu_obj(gobj);

	if (!qobj->hw_res_handle) {
		ret = -EINVAL;
		goto out;
	}

	ret = virtgpu_cmd_transfer_to_host_2d(vgdev, qobj->hw_res_handle,
					      0, 64, 64, 0, 0, vgdev->has_fence ? &fence : NULL);
	if (!ret && vgdev->has_fence) {
		qobj->tbo.sync_obj = vgdev->mman.bdev.driver->sync_obj_ref(fence);
		virtgpu_object_wait(qobj, false);
	}
	{
		struct virtgpu_hw_cursor_page *cursor_page = &vgdev->cursor_info;
		cursor_page->cursor_id = qobj->hw_res_handle;
		cursor_page->cursor_hot_x = hot_x;
		cursor_page->cursor_hot_y = hot_y;
		cursor_page->generation_count++;
	}
	virtgpu_cursor_ping(vgdev);
out:
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int virtgpu_crtc_cursor_move(struct drm_crtc *crtc,
				int x, int y)
{
	struct virtgpu_device *vgdev = crtc->dev->dev_private;

	{
		struct virtgpu_hw_cursor_page *cursor_page = &vgdev->cursor_info;
		cursor_page->cursor_x = x;
		cursor_page->cursor_y = y;
	}
	virtgpu_cursor_ping(vgdev);
	return 0;
}

static int virtgpu_crtc_page_flip(struct drm_crtc *crtc,
				struct drm_framebuffer *fb,
				  struct drm_pending_vblank_event *event, uint32_t flags)
{
	return -EINVAL;
}


static const struct drm_crtc_funcs virtgpu_crtc_funcs = {
	.cursor_set2 = virtgpu_crtc_cursor_set,
	.cursor_move = virtgpu_crtc_cursor_move,
	.gamma_set = virtgpu_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.page_flip = virtgpu_crtc_page_flip,
	.destroy = virtgpu_crtc_destroy,
};

static void virtgpu_user_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct virtgpu_framebuffer *virtgpu_fb = to_virtgpu_framebuffer(fb);

	if (virtgpu_fb->obj)
		drm_gem_object_unreference_unlocked(virtgpu_fb->obj);
	drm_framebuffer_cleanup(fb);
	kfree(virtgpu_fb);
}

int virtgpu_framebuffer_surface_dirty(struct drm_framebuffer *fb,
				  struct drm_file *file_priv,
				  unsigned flags, unsigned color,
				  struct drm_clip_rect *clips,
				  unsigned num_clips)
{
	return virtgpu_surface_dirty(to_virtgpu_framebuffer(fb), clips, num_clips);
}

static const struct drm_framebuffer_funcs virtgpu_fb_funcs = {
	.destroy = virtgpu_user_framebuffer_destroy,
	.dirty = virtgpu_framebuffer_surface_dirty,
};

int
virtgpu_framebuffer_init(struct drm_device *dev,
			 struct virtgpu_framebuffer *vgfb,
			 struct drm_mode_fb_cmd2 *mode_cmd,
			 struct drm_gem_object *obj)
{
	int ret;
	struct virtgpu_object *bo;
	vgfb->obj = obj;

	bo = gem_to_virtgpu_obj(obj);

	ret = drm_framebuffer_init(dev, &vgfb->base, &virtgpu_fb_funcs);
	if (ret) {
		vgfb->obj = NULL;
		return ret;
	}
	drm_helper_mode_fill_fb_struct(&vgfb->base, mode_cmd);

	spin_lock_init(&vgfb->dirty_lock);
	vgfb->x1 = vgfb->y1 = INT_MAX;
	vgfb->x2 = vgfb->y2 = 0;
	return 0;
}

static void virtgpu_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

static bool virtgpu_crtc_mode_fixup(struct drm_crtc *crtc,
				  const struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int virtgpu_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct virtgpu_device *vgdev = dev->dev_private;
	struct virtgpu_framebuffer *vgfb;
	struct virtgpu_object *bo, *old_bo = NULL;
	struct virtgpu_crtc *vgcrtc = to_virtgpu_crtc(crtc);

	if (!crtc->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}

	if (old_fb) {
		vgfb = to_virtgpu_framebuffer(old_fb);
		old_bo = gem_to_virtgpu_obj(vgfb->obj);
	}
	vgfb = to_virtgpu_framebuffer(crtc->fb);
	bo = gem_to_virtgpu_obj(vgfb->obj);
	DRM_DEBUG("+%d+%d (%d,%d) => (%d,%d)\n",
		  x, y,
		  mode->hdisplay, mode->vdisplay,
		  adjusted_mode->hdisplay,
		  adjusted_mode->vdisplay);

	virtgpu_cmd_set_scanout(vgdev, vgcrtc->idx, bo->hw_res_handle,
				mode->hdisplay, mode->vdisplay, x, y);

	return 0;
}

static void virtgpu_crtc_prepare(struct drm_crtc *crtc)
{
	DRM_DEBUG("current: %dx%d+%d+%d (%d).\n",
		  crtc->mode.hdisplay, crtc->mode.vdisplay,
		  crtc->x, crtc->y, crtc->enabled);
}

static void virtgpu_crtc_commit(struct drm_crtc *crtc)
{
	DRM_DEBUG("\n");
}

void virtgpu_crtc_load_lut(struct drm_crtc *crtc)
{
	DRM_DEBUG("\n");
}

void virtgpu_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct virtgpu_device *vgdev = dev->dev_private;
	struct virtgpu_crtc *vgcrtc = to_virtgpu_crtc(crtc);
	virtgpu_cmd_set_scanout(vgdev, vgcrtc->idx, 0, 0, 0, 0, 0);
}

static const struct drm_crtc_helper_funcs virtgpu_crtc_helper_funcs = {
	.disable = virtgpu_crtc_disable,
	.dpms = virtgpu_crtc_dpms,
	.mode_fixup = virtgpu_crtc_mode_fixup,
	.mode_set = virtgpu_crtc_mode_set,
	.prepare = virtgpu_crtc_prepare,
	.commit = virtgpu_crtc_commit,
	.load_lut = virtgpu_crtc_load_lut,
};

int vgdev_crtc_init(struct drm_device *dev, int num_crtc)
{
	struct virtgpu_crtc *virtgpu_crtc;

	virtgpu_crtc = kzalloc(sizeof(struct virtgpu_crtc), GFP_KERNEL);
	if (!virtgpu_crtc)
		return -ENOMEM;

	drm_crtc_init(dev, &virtgpu_crtc->base, &virtgpu_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&virtgpu_crtc->base, 256);
	drm_crtc_helper_add(&virtgpu_crtc->base, &virtgpu_crtc_helper_funcs);

	virtgpu_crtc->idx = num_crtc;
	return 0;
}

static void virtgpu_enc_dpms(struct drm_encoder *encoder, int mode)
{
	DRM_DEBUG("\n");
}

static bool virtgpu_enc_mode_fixup(struct drm_encoder *encoder,
			       const struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG("\n");
	return true;
}

static void virtgpu_enc_prepare(struct drm_encoder *encoder)
{
	DRM_DEBUG("\n");
}

static void virtgpu_enc_commit(struct drm_encoder *encoder)
{
	DRM_DEBUG("\n");
}

static void virtgpu_enc_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG("\n");
}

static int virtgpu_add_display_info_modes(struct drm_connector *connector,
					  unsigned *pwidth, unsigned *pheight)
{
	struct virtgpu_output *output = drm_connector_to_virtgpu_output(connector);
	struct drm_device *dev = connector->dev;
	struct virtgpu_device *vgdev = connector->dev->dev_private;
	struct drm_display_mode *mode = NULL;
	int idx = output->index;

	if (!vgdev->display_info.pmodes[idx].enabled)
		return 0;

	mode = drm_cvt_mode(dev, vgdev->display_info.pmodes[idx].width,
			    vgdev->display_info.pmodes[idx].height, 60, false, false,
			    false);
	mode->type |= DRM_MODE_TYPE_PREFERRED;
	*pwidth = vgdev->display_info.pmodes[idx].width;
	*pheight = vgdev->display_info.pmodes[idx].height;
	drm_mode_probed_add(connector, mode);
	return 1;
}

static int virtgpu_conn_get_modes(struct drm_connector *connector)
{
	int ret = 0;
	struct virtgpu_device *vgdev = connector->dev->dev_private;
	struct virtgpu_output *output = drm_connector_to_virtgpu_output(connector);
	int pwidth = 0, pheight = 0;
	if (vgdev->display_info.pmodes[output->index].enabled) {
		ret = virtgpu_add_display_info_modes(connector, &pwidth, &pheight);
	}

	ret += virtgpu_add_common_modes(connector, pwidth, pheight);
	return ret;
}

static int virtgpu_conn_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	return MODE_OK;
}

struct drm_encoder *virtgpu_best_encoder(struct drm_connector *connector)
{
	struct virtgpu_output *virtgpu_output =
		drm_connector_to_virtgpu_output(connector);

	DRM_DEBUG("\n");
	return &virtgpu_output->enc;
}


static const struct drm_encoder_helper_funcs virtgpu_enc_helper_funcs = {
	.dpms = virtgpu_enc_dpms,
	.mode_fixup = virtgpu_enc_mode_fixup,
	.prepare = virtgpu_enc_prepare,
	.mode_set = virtgpu_enc_mode_set,
	.commit = virtgpu_enc_commit,
};

static const struct drm_connector_helper_funcs virtgpu_connector_helper_funcs = {
	.get_modes = virtgpu_conn_get_modes,
	.mode_valid = virtgpu_conn_mode_valid,
	.best_encoder = virtgpu_best_encoder,
};

static void virtgpu_conn_save(struct drm_connector *connector)
{
	DRM_DEBUG("\n");
}

static void virtgpu_conn_restore(struct drm_connector *connector)
{
	DRM_DEBUG("\n");
}

static enum drm_connector_status virtgpu_conn_detect(
			struct drm_connector *connector,
			bool force)
{
	struct virtgpu_output *output =
		drm_connector_to_virtgpu_output(connector);
	struct drm_device *ddev = connector->dev;
	struct virtgpu_device *vgdev = ddev->dev_private;

	if (vgdev->display_info.pmodes[output->index].enabled)
		return connector_status_connected;
	else
		return connector_status_disconnected;
}

static int virtgpu_conn_set_property(struct drm_connector *connector,
				   struct drm_property *property,
				   uint64_t value)
{
	DRM_DEBUG("\n");
	return 0;
}

static void virtgpu_conn_destroy(struct drm_connector *connector)
{
	struct virtgpu_output *virtgpu_output =
		drm_connector_to_virtgpu_output(connector);

	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(virtgpu_output);
}

static const struct drm_connector_funcs virtgpu_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.save = virtgpu_conn_save,
	.restore = virtgpu_conn_restore,
	.detect = virtgpu_conn_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = virtgpu_conn_set_property,
	.destroy = virtgpu_conn_destroy,
};

static void virtgpu_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs virtgpu_enc_funcs = {
	.destroy = virtgpu_enc_destroy,
};

int vgdev_output_init(struct drm_device *dev, int num_output)
{
	struct virtgpu_output *virtgpu_output;
	struct drm_connector *connector;
	struct drm_encoder *encoder;

	virtgpu_output = kzalloc(sizeof(struct virtgpu_output), GFP_KERNEL);
	if (!virtgpu_output)
		return -ENOMEM;

	virtgpu_output->index = num_output;

	connector = &virtgpu_output->base;
	encoder = &virtgpu_output->enc;
	drm_connector_init(dev, &virtgpu_output->base,
			   &virtgpu_connector_funcs, DRM_MODE_CONNECTOR_VIRTUAL);
	connector->polled = DRM_CONNECTOR_POLL_HPD;
	drm_encoder_init(dev, &virtgpu_output->enc, &virtgpu_enc_funcs,
			 DRM_MODE_ENCODER_VIRTUAL);

	encoder->possible_crtcs = 1 << num_output;
	drm_mode_connector_attach_encoder(&virtgpu_output->base,
					  &virtgpu_output->enc);
	drm_encoder_helper_add(encoder, &virtgpu_enc_helper_funcs);
	drm_connector_helper_add(connector, &virtgpu_connector_helper_funcs);

	drm_sysfs_connector_add(connector);
	return 0;
}

static struct drm_framebuffer *
virtgpu_user_framebuffer_create(struct drm_device *dev,
			    struct drm_file *file_priv,
			    struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_gem_object *obj = NULL;
	struct virtgpu_framebuffer *virtgpu_fb;
	int ret;

	/* lookup object associated with res handle */
	obj = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);
	if (!obj)
		return ERR_PTR(-EINVAL);

	virtgpu_fb = kzalloc(sizeof(*virtgpu_fb), GFP_KERNEL);
	if (virtgpu_fb == NULL)
		return ERR_PTR(-ENOMEM);

	ret = virtgpu_framebuffer_init(dev, virtgpu_fb, mode_cmd, obj);
	if (ret) {
		kfree(virtgpu_fb);
		if (obj)
			drm_gem_object_unreference_unlocked(obj);
		return NULL;
	}

	return &virtgpu_fb->base;
}

static const struct drm_mode_config_funcs virtgpu_mode_funcs = {
	.fb_create = virtgpu_user_framebuffer_create,
};

int virtgpu_modeset_init(struct virtgpu_device *vgdev)
{
	int i;
	int ret;

	drm_mode_config_init(vgdev->ddev);
	vgdev->ddev->mode_config.funcs = (void *)&virtgpu_mode_funcs;

	/* modes will be validated against the framebuffer size */
	vgdev->ddev->mode_config.min_width = 320;
	vgdev->ddev->mode_config.min_height = 200;
	vgdev->ddev->mode_config.max_width = 8192;
	vgdev->ddev->mode_config.max_height = 8192;

//	vgdev->ddev->mode_config.fb_base = vgdev->vram_base;
	for (i = 0 ; i < vgdev->num_hw_scanouts; ++i) {
		vgdev_crtc_init(vgdev->ddev, i);
		vgdev_output_init(vgdev->ddev, i);
	}

	/* primary surface must be created by this point, to allow
	 * issuing command queue commands and having them read by
	 * spice server. */
	ret = virtgpu_fbdev_init(vgdev);
	if (ret)
		return ret;

	ret = drm_vblank_init(vgdev->ddev, vgdev->num_hw_scanouts);
	
	drm_kms_helper_poll_init(vgdev->ddev);
	return ret;
}

void virtgpu_modeset_fini(struct virtgpu_device *vgdev)
{
	virtgpu_fbdev_fini(vgdev);
	drm_mode_config_cleanup(vgdev->ddev);
}
