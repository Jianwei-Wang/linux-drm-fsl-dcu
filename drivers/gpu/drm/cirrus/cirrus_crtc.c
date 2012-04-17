/*
 * Copyright 2000,2001 Sven Luther.
 * Copyright 2010 Matt Turner.
 * Copyright 2011 Red Hat <mjg@redhat.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *			Matt Turner
 *			Sven Luther
 *			Thomas Witzel
 *			Alan Hourihane
 *
 * Portions of this code derived from cirrusfb.c:
 * drivers/video/cirrusfb.c - driver for Cirrus Logic chipsets
 *
 * Copyright 1999-2001 Jeff Garzik <jgarzik@pobox.com>
 */
#include "drmP.h"
#include "drm.h"
#include "drm_crtc_helper.h"

#include <video/cirrus.h>

#include "cirrus.h"
#include "cirrus_drv.h"
#include "cirrus_mode.h"

#define CIRRUS_LUT_SIZE 256

#define PALETTE_INDEX 0x8
#define PALETTE_DATA 0x9

/*
 * This file contains setup code for the CRTC.
 */

static void cirrus_crtc_load_lut(struct drm_crtc *crtc)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct cirrus_device *cdev = dev->dev_private;
	int i;

	if (!crtc->enabled)
		return;

	for (i = 0; i < CIRRUS_LUT_SIZE; i++) {
		/* VGA registers */
		WREG8(PALETTE_INDEX, i);
		WREG8(PALETTE_DATA, cirrus_crtc->lut_r[i]);
		WREG8(PALETTE_DATA, cirrus_crtc->lut_g[i]);
		WREG8(PALETTE_DATA, cirrus_crtc->lut_b[i]);
	}
}

/*
 * The DRM core requires DPMS functions, but they make little sense in our
 * case and so are just stubs
 */

static void cirrus_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

/*
 * The core passes the desired mode to the CRTC code to see whether any
 * CRTC-specific modifications need to be made to it. We're in a position
 * to just pass that straight through, so this does nothing
 */
static bool cirrus_crtc_mode_fixup(struct drm_crtc *crtc,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	return true;
}

/*
 * The meat of this driver. The core passes us a mode and we have to program
 * it. The modesetting here is the bare minimum required to satisfy the qemu
 * emulation of this hardware, and running this against a real device is
 * likely to result in an inadequately programmed mode. We've already had
 * the opportunity to modify the mode, so whatever we receive here should
 * be something that can be correctly programmed and displayed
 */
static int cirrus_crtc_mode_set(struct drm_crtc *crtc,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode,
				int x, int y, struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct cirrus_device *cdev = dev->dev_private;
	int hsyncstart, hsyncend, htotal, hdispend;
	int vtotal, vdispend;
	int tmp;

	htotal = mode->htotal / 8;
	hsyncend = mode->hsync_end / 8;
	hsyncstart = mode->hsync_start / 8;
	hdispend = mode->hdisplay / 8;

	vtotal = mode->vtotal;
	vdispend = mode->vdisplay;

	vdispend -= 1;
	vtotal -= 2;

	htotal -= 5;
	hdispend -= 1;
	hsyncstart += 1;
	hsyncend += 1;

	WREG_CRT(VGA_CRTC_V_SYNC_END, 0x20);
	WREG_CRT(VGA_CRTC_H_TOTAL, htotal);
	WREG_CRT(VGA_CRTC_H_DISP, hdispend);
	WREG_CRT(VGA_CRTC_H_SYNC_START, hsyncstart);
	WREG_CRT(VGA_CRTC_H_SYNC_END, hsyncend);
	WREG_CRT(VGA_CRTC_V_TOTAL, vtotal & 0xff);
	WREG_CRT(VGA_CRTC_V_DISP_END, vdispend & 0xff);

	tmp = 0x40;
	if ((vdispend + 1) & 512)
		tmp |= 0x20;
	WREG_CRT(VGA_CRTC_MAX_SCAN, tmp);

	/*
	 * Overflow bits for values that don't fit in the standard registers
	 */
	tmp = 16;
	if (vtotal & 256)
		tmp |= 1;
	if (vdispend & 256)
		tmp |= 2;
	if ((vdispend + 1) & 256)
		tmp |= 8;
	if (vtotal & 512)
		tmp |= 32;
	if (vdispend & 512)
		tmp |= 64;
	WREG_CRT(VGA_CRTC_OVERFLOW, tmp);

	tmp = 0;

	/* More overflow bits */

	if ((htotal + 5) & 64)
		tmp |= 16;
	if ((htotal + 5) & 128)
		tmp |= 32;
	if (vtotal & 256)
		tmp |= 64;
	if (vtotal & 512)
		tmp |= 128;

	WREG_CRT(CL_CRT1A, tmp);

	/* Disable Hercules/CGA compatibility */
	WREG_CRT(VGA_CRTC_MODE, 0x03);

	return 0;
}

/*
 * This is called before a mode is programmed. A typical use might be to
 * enable DPMS during the programming to avoid seeing intermediate stages,
 * but that's not relevant to us
 */
static void cirrus_crtc_prepare(struct drm_crtc *crtc)
{
}

/*
 * This is called after a mode is programmed. It should reverse anything done
 * by the prepare function
 */
static void cirrus_crtc_commit(struct drm_crtc *crtc)
{
}

/*
 * The core can pass us a set of gamma values to program. We actually only
 * use this for 8-bit mode so can't perform smooth fades on deeper modes,
 * but it's a requirement that we provide the function
 */
static void cirrus_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green,
				  u16 *blue, uint32_t start, uint32_t size)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);
	int i;

	if (size != CIRRUS_LUT_SIZE)
		return;

	for (i = 0; i < CIRRUS_LUT_SIZE; i++) {
		cirrus_crtc->lut_r[i] = red[i];
		cirrus_crtc->lut_g[i] = green[i];
		cirrus_crtc->lut_b[i] = blue[i];
	}
	cirrus_crtc_load_lut(crtc);
}

/* Simple cleanup function */
static void cirrus_crtc_destroy(struct drm_crtc *crtc)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(cirrus_crtc);
}

/* These provide the minimum set of functions required to handle a CRTC */
static const struct drm_crtc_funcs cirrus_crtc_funcs = {
	.gamma_set = cirrus_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = cirrus_crtc_destroy,
};

static const struct drm_crtc_helper_funcs cirrus_helper_funcs = {
	.dpms = cirrus_crtc_dpms,
	.mode_fixup = cirrus_crtc_mode_fixup,
	.mode_set = cirrus_crtc_mode_set,
	.prepare = cirrus_crtc_prepare,
	.commit = cirrus_crtc_commit,
	.load_lut = cirrus_crtc_load_lut,
};

/* CRTC setup */
void cirrus_crtc_init(struct drm_device *dev)
{
	struct cirrus_device *cdev = dev->dev_private;
	struct cirrus_crtc *cirrus_crtc;
	int i;

	cirrus_crtc = kzalloc(sizeof(struct cirrus_crtc) +
			      (CIRRUSFB_CONN_LIMIT * sizeof(struct drm_connector *)),
			      GFP_KERNEL);

	if (cirrus_crtc == NULL)
		return;

	drm_crtc_init(dev, &cirrus_crtc->base, &cirrus_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&cirrus_crtc->base, CIRRUS_LUT_SIZE);
	cdev->mode_info.crtc = cirrus_crtc;

	for (i = 0; i < CIRRUS_LUT_SIZE; i++) {
		cirrus_crtc->lut_r[i] = i;
		cirrus_crtc->lut_g[i] = i;
		cirrus_crtc->lut_b[i] = i;
	}

	drm_crtc_helper_add(&cirrus_crtc->base, &cirrus_helper_funcs);
}

/** Sets the color ramps on behalf of fbcon */
void cirrus_crtc_fb_gamma_set(struct drm_crtc *crtc, u16 red, u16 green,
			      u16 blue, int regno)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);

	cirrus_crtc->lut_r[regno] = red;
	cirrus_crtc->lut_g[regno] = green;
	cirrus_crtc->lut_b[regno] = blue;
}

/** Gets the color ramps on behalf of fbcon */
void cirrus_crtc_fb_gamma_get(struct drm_crtc *crtc, u16 *red, u16 *green,
			      u16 *blue, int regno)
{
	struct cirrus_crtc *cirrus_crtc = to_cirrus_crtc(crtc);

	*red = cirrus_crtc->lut_r[regno];
	*green = cirrus_crtc->lut_g[regno];
	*blue = cirrus_crtc->lut_b[regno];
}
