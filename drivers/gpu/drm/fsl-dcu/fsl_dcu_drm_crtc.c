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

#include <linux/regmap.h>
#include <linux/clk.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>

#include "fsl_dcu_drm_crtc.h"
#include "fsl_dcu_drm_drv.h"
#include "fsl_dcu_drm_plane.h"

#define to_fsl_dcu_crtc(c)	container_of(c, struct fsl_dcu_drm_crtc, crtc)

static void fsl_dcu_drm_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	struct drm_display_mode *mode = &crtc->state->mode;
	uint32_t hbp, hfp, hsw, vbp, vfp, vsw, div, index;

	DBG(": set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
	    mode->base.id, mode->name,
	    mode->vrefresh, mode->clock,
	    mode->hdisplay, mode->hsync_start,
	    mode->hsync_end, mode->htotal,
	    mode->vdisplay, mode->vsync_start,
	    mode->vsync_end, mode->vtotal,
	    mode->type, mode->flags);

	index = drm_crtc_index(crtc);
	div = (uint32_t)clk_get_rate(fsl_dev->clk) / mode->clock / 1000;

	/* Configure timings: */
	hbp = mode->htotal - mode->hsync_end;
	hfp = mode->hsync_start - mode->hdisplay;
	hsw = mode->hsync_end - mode->hsync_start;
	vbp = mode->vtotal - mode->vsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vsw = mode->vsync_end - mode->vsync_start;

	regmap_write(fsl_dev->regmap, DCU_HSYN_PARA,
		     DCU_HSYN_PARA_BP(hbp) |
		     DCU_HSYN_PARA_PW(hsw) |
		     DCU_HSYN_PARA_FP(hfp));
	regmap_write(fsl_dev->regmap, DCU_VSYN_PARA,
		     DCU_VSYN_PARA_BP(vbp) |
		     DCU_VSYN_PARA_PW(vsw) |
		     DCU_VSYN_PARA_FP(vfp));
	regmap_write(fsl_dev->regmap, DCU_DISP_SIZE,
		     DCU_DISP_SIZE_DELTA_Y(mode->vdisplay) |
		     DCU_DISP_SIZE_DELTA_X(mode->hdisplay));
	regmap_write(fsl_dev->regmap, DCU_DIV_RATIO, div);
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE, DCU_UPDATE_MODE_READREG);
}

static bool fsl_dcu_drm_crtc_mode_fixup(struct drm_crtc *crtc,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void fsl_dcu_drm_crtc_prepare(struct drm_crtc *crtc)
{
}

/* Now enable the clocks, plane, pipe, and connectors that we set up. */
static void fsl_dcu_drm_crtc_mode_commit(struct drm_crtc *crtc)
{
}

static int fsl_dcu_drm_crtc_atomic_check(struct drm_crtc *crtc,
					 struct drm_crtc_state *state)
{
	return 0;
}

static void fsl_dcu_drm_crtc_atomic_begin(struct drm_crtc *crtc)
{
}

static void fsl_dcu_drm_crtc_atomic_flush(struct drm_crtc *crtc)
{
}

static void fsl_dcu_drm_disable_crtc(struct drm_crtc *crtc)
{
}

static void fsl_dcu_drm_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

static const struct drm_crtc_funcs fsl_dcu_drm_crtc_funcs = {
	.page_flip = drm_atomic_helper_page_flip,
	.set_config = drm_atomic_helper_set_config,
	.destroy = drm_crtc_cleanup,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_crtc_helper_funcs fsl_dcu_drm_crtc_helper_funcs = {
	.disable = fsl_dcu_drm_disable_crtc,
	.mode_fixup = fsl_dcu_drm_crtc_mode_fixup,
	.mode_set = drm_helper_crtc_mode_set,
	.mode_set_nofb = fsl_dcu_drm_crtc_mode_set_nofb,
	.mode_set_base = drm_helper_crtc_mode_set_base,
	.prepare = fsl_dcu_drm_crtc_prepare,
	.commit = fsl_dcu_drm_crtc_mode_commit,
	.atomic_check = fsl_dcu_drm_crtc_atomic_check,
	.atomic_begin = fsl_dcu_drm_crtc_atomic_begin,
	.atomic_flush = fsl_dcu_drm_crtc_atomic_flush,
	.dpms = fsl_dcu_drm_crtc_dpms,
};

int fsl_dcu_drm_crtc_create(struct fsl_dcu_drm_device *fsl_dev)
{
	struct drm_plane *primary;
	struct drm_crtc *crtc;
	int i, ret;

	crtc = devm_kzalloc(fsl_dev->dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	primary = fsl_dcu_drm_primary_create_plane(fsl_dev->ddev);
	ret = drm_crtc_init_with_planes(fsl_dev->ddev, crtc, primary, NULL,
					&fsl_dcu_drm_crtc_funcs);
	if (ret < 0)
		return ret;

	drm_crtc_helper_add(crtc, &fsl_dcu_drm_crtc_helper_funcs);

	for (i = 0; i < DCU_TOTAL_LAYER_NUM; i++) {
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_1(i), 0);
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_2(i), 0);
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_3(i), 0);
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_4(i), 0);
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_5(i), 0);
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_6(i), 0);
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_7(i), 0);
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_8(i), 0);
		regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_9(i), 0);
		if (of_device_is_compatible(fsl_dev->np, "fsl,ls1021a-dcu"))
			regmap_write(fsl_dev->regmap, DCU_CTRLDESCLN_10(i), 0);
	}
	regmap_write(fsl_dev->regmap, DCU_SYN_POL,
		     DCU_SYN_POL_INV_VS_LOW | DCU_SYN_POL_INV_HS_LOW);
	regmap_write(fsl_dev->regmap, DCU_BGND, DCU_BGND_R(0) |
		     DCU_BGND_G(0) | DCU_BGND_B(0));
	regmap_write(fsl_dev->regmap, DCU_DCU_MODE,
		     DCU_MODE_BLEND_ITER(1) | DCU_MODE_RASTER_EN);
	regmap_write(fsl_dev->regmap, DCU_THRESHOLD,
		     DCU_THRESHOLD_LS_BF_VS(BF_VS_VAL) |
		     DCU_THRESHOLD_OUT_BUF_HIGH(BUF_MAX_VAL) |
		     DCU_THRESHOLD_OUT_BUF_LOW(BUF_MIN_VAL));
	regmap_update_bits(fsl_dev->regmap, DCU_DCU_MODE,
			   DCU_MODE_DCU_MODE_MASK,
			   DCU_MODE_DCU_MODE(DCU_MODE_OFF));
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE, DCU_UPDATE_MODE_READREG);

	return 0;
}
