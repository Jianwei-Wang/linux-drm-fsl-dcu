/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <linux/regmap.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_atomic_helper.h>

#include "fsl_dcu_drm_drv.h"
#include "fsl_dcu_drm_kms.h"
#include "fsl_dcu_drm_plane.h"

#define to_fsl_dcu_plane(plane) \
	container_of(plane, struct fsl_dcu_drm_plane, plane)

static int
fsl_dcu_drm_plane_prepare_fb(struct drm_plane *plane,
			     struct drm_framebuffer *fb,
			     const struct drm_plane_state *new_state)
{
	return 0;
}

static void
fsl_dcu_drm_plane_cleanup_fb(struct drm_plane *plane,
			     struct drm_framebuffer *fb,
			     const struct drm_plane_state *new_state)
{
}

static int fsl_dcu_drm_plane_atomic_check(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	return 0;
}

static void fsl_dcu_drm_plane_atomic_disable(struct drm_plane *plane,
					     struct drm_plane_state *old_state)
{
}

void fsl_dcu_drm_plane_atomic_update(struct drm_plane *plane,
				     struct drm_plane_state *old_state)
{
	struct fsl_dcu_drm_device *fsl_dev = plane->dev->dev_private;
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = plane->state->fb;
	u32 index, alpha, bpp;
	struct drm_gem_cma_object *gem;
	struct fsl_dcu_drm_plane *fsl_plane = to_fsl_dcu_plane(plane);

	index = fsl_plane->index;
	index = 4 - index;
	printk("[fsl]----------layer index = %d\n", index);
	if (!fb)	
		return;
	gem = drm_fb_cma_get_gem_obj(fb, 0);

	switch (fb->pixel_format) {
	case DRM_FORMAT_RGB565:
		bpp = FSL_DCU_RGB565;
		alpha = 0xff;
		break;
	case DRM_FORMAT_RGB888:
		bpp = FSL_DCU_RGB888;
		alpha = 0xff;
		break;
	case DRM_FORMAT_ARGB8888:
		bpp = FSL_DCU_ARGB8888;
		alpha = 0xff;
		break;
	case DRM_FORMAT_BGRA4444:
		bpp = FSL_DCU_ARGB4444;
		alpha = 0xff;
		break;
	case DRM_FORMAT_ARGB1555:
		bpp = FSL_DCU_ARGB1555;
		alpha = 0xff;
		break;
	case DRM_FORMAT_YUV422:
		bpp = FSL_DCU_YUV422;
		alpha = 0xff;
		break;
	default:
		return;
	}

	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_1(index),
		     DCU_CTRLDESCLN_1_HEIGHT(state->crtc_h) |
		     DCU_CTRLDESCLN_1_WIDTH(state->crtc_w));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_2(index),
		     DCU_CTRLDESCLN_2_POSY(state->crtc_y) |
		     DCU_CTRLDESCLN_2_POSX(state->crtc_x));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_3(index), gem->paddr);
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_4(index),
		     DCU_CTRLDESCLN_4_EN |
		     DCU_CTRLDESCLN_4_TRANS(alpha) |
		     DCU_CTRLDESCLN_4_BPP(bpp) |
		     DCU_CTRLDESCLN_4_AB(0));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_5(index),
		     DCU_CTRLDESCLN_5_CKMAX_R(0xFF) |
		     DCU_CTRLDESCLN_5_CKMAX_G(0xFF) |
		     DCU_CTRLDESCLN_5_CKMAX_B(0xFF));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_6(index),
		     DCU_CTRLDESCLN_6_CKMIN_R(0) |
		     DCU_CTRLDESCLN_6_CKMIN_G(0) |
		     DCU_CTRLDESCLN_6_CKMIN_B(0));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_7(index), 0);
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_8(index),
		     DCU_CTRLDESCLN_8_FG_FCOLOR(0));
	regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_9(index),
		     DCU_CTRLDESCLN_9_BG_BCOLOR(0));
	if (of_device_is_compatible(fsl_dev->np, "fsl,ls1021a-dcu"))
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_10(index),
			     DCU_CTRLDESCLN_10_POST_SKIP(0) |
			     DCU_CTRLDESCLN_10_PRE_SKIP(0));
	regmap_update_bits(fsl_dev->regmap, DCU_DCU_MODE,
			   DCU_MODE_DCU_MODE_MASK,
			   DCU_MODE_DCU_MODE(DCU_MODE_NORMAL));
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE, DCU_UPDATE_MODE_READREG);
}

int fsl_dcu_drm_plane_disable(struct drm_plane *plane)
{
	return 0;
}

void fsl_dcu_drm_plane_destroy(struct drm_plane *plane)
{
	fsl_dcu_drm_plane_disable(plane);
	drm_plane_cleanup(plane);
}

static const uint32_t fsl_dcu_drm_plane_formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_YUV422,
};

static const struct drm_plane_funcs fsl_dcu_drm_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = fsl_dcu_drm_plane_destroy,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
	.reset = drm_atomic_helper_plane_reset,
};

static const struct drm_plane_helper_funcs fsl_dcu_drm_plane_helper_funcs = {
	.prepare_fb = fsl_dcu_drm_plane_prepare_fb,
	.cleanup_fb = fsl_dcu_drm_plane_cleanup_fb,
	.atomic_check = fsl_dcu_drm_plane_atomic_check,
	.atomic_update = fsl_dcu_drm_plane_atomic_update,
	.atomic_disable = fsl_dcu_drm_plane_atomic_disable,
};

struct drm_plane *fsl_dcu_drm_primary_create_plane(struct drm_device *dev)
{
	struct drm_plane *primary;
	struct fsl_dcu_drm_plane *fsl_plane[3];
	int i, ret;

	primary = kzalloc(sizeof(*primary), GFP_KERNEL);
	if (!primary) {
		DRM_DEBUG_KMS("Failed to allocate primary plane\n");
		return NULL;
	}

	/* possible_crtc's will be filled in later by crtc_init */
	ret = drm_universal_plane_init(dev, primary, 0,
				       &fsl_dcu_drm_plane_funcs,
				       fsl_dcu_drm_plane_formats,
				       ARRAY_SIZE(fsl_dcu_drm_plane_formats),
				       DRM_PLANE_TYPE_PRIMARY);
	if (ret) {
		kfree(primary);
		primary = NULL;
	}
	drm_plane_helper_add(primary, &fsl_dcu_drm_plane_helper_funcs);

	for(i = 0; i < 3; i++)
	{
		fsl_plane[i] = kzalloc(sizeof(struct fsl_dcu_drm_plane), GFP_KERNEL);
		if (!fsl_plane[i]) {
			DRM_DEBUG_KMS("Failed to allocate primary plane\n");
			return NULL;
		}
		fsl_plane[i]->index = 1 + i;

		ret = drm_universal_plane_init(dev, &fsl_plane[i]->plane, 1,
					       &fsl_dcu_drm_plane_funcs,
					       fsl_dcu_drm_plane_formats,
					       ARRAY_SIZE(fsl_dcu_drm_plane_formats),
					       DRM_PLANE_TYPE_OVERLAY);
		if (ret) {
			kfree(fsl_plane[i]);
			fsl_plane[i] = NULL;
		}
		drm_plane_helper_add(&fsl_plane[i]->plane, &fsl_dcu_drm_plane_helper_funcs);
	}

	return primary;
}
