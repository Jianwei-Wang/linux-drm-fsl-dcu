#include "drmP.h"
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "udl_drv.h"

/* dummy encoder */
void udl_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static void udl_encoder_disable(struct drm_encoder *encoder)
{
}

static bool udl_mode_fixup(struct drm_encoder *encoder,
			   struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void udl_encoder_prepare(struct drm_encoder *encoder)
{
}

static void udl_encoder_commit(struct drm_encoder *encoder)
{
}

static void udl_encoder_mode_set(struct drm_encoder *encoder,
				 struct drm_display_mode *mode,
				 struct drm_display_mode *adjusted_mode)
{
}

static void
udl_encoder_dpms(struct drm_encoder *encoder, int mode)
{
}

static const struct drm_encoder_helper_funcs udl_helper_funcs = {
	.dpms = udl_encoder_dpms,
	.mode_fixup = udl_mode_fixup,
	.prepare = udl_encoder_prepare,
	.mode_set = udl_encoder_mode_set,
	.commit = udl_encoder_commit,
	.disable = udl_encoder_disable,
};

static const struct drm_encoder_funcs udl_enc_funcs = {
	.destroy = udl_enc_destroy,
};

struct drm_encoder *udl_encoder_init(struct drm_device *dev)
{
	struct drm_encoder *encoder;

	encoder = kzalloc(sizeof(struct drm_encoder), GFP_KERNEL);
	if (!encoder)
		return NULL;

	drm_encoder_init(dev, encoder, &udl_enc_funcs, DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &udl_helper_funcs);
	encoder->possible_crtcs = 1;
	return encoder;
}
