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

#include <linux/backlight.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <video/of_display_timing.h>

#include "fsl_dcu_drm_drv.h"
#include "fsl_dcu_drm_connector.h"

static void fsl_dcu_drm_encoder_dpms(struct drm_encoder *encoder, int mode)
{
}

static void fsl_dcu_drm_encoder_mode_prepare(struct drm_encoder *encoder)
{
}

static void fsl_dcu_drm_encoder_mode_set(struct drm_encoder *encoder,
					 struct drm_display_mode *mode,
					 struct drm_display_mode *adjusted_mode)
{
}

static void fsl_dcu_drm_encoder_mode_commit(struct drm_encoder *encoder)
{
}

static void fsl_dcu_drm_encoder_disable(struct drm_encoder *encoder)
{
}

static int
fsl_dcu_drm_encoder_atomic_check(struct drm_encoder *encoder,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	return 0;
}

static void fsl_dcu_drm_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.dpms = fsl_dcu_drm_encoder_dpms,
	.prepare = fsl_dcu_drm_encoder_mode_prepare,
	.commit = fsl_dcu_drm_encoder_mode_commit,
	.mode_set = fsl_dcu_drm_encoder_mode_set,
	.disable = fsl_dcu_drm_encoder_disable,
	.atomic_check = fsl_dcu_drm_encoder_atomic_check,
};

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = fsl_dcu_drm_encoder_destroy,
};

int fsl_dcu_drm_encoder_create(struct fsl_dcu_drm_device *fsl_dev,
			       struct drm_crtc *crtc)
{
	struct drm_encoder *encoder = &fsl_dev->encoder;
	int ret;

	encoder->possible_crtcs = 1;
	ret = drm_encoder_init(fsl_dev->ddev, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_LVDS);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);
	encoder->crtc = crtc;

	return 0;
}

#define to_fsl_dcu_connector(connector) \
	container_of(connector, struct fsl_dcu_drm_connector, connector)

static int fsl_dcu_drm_connector_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct device_node *display_np, *np = dev->dev->of_node;
	struct drm_display_mode *mode = drm_mode_create(connector->dev);
	int num_modes = 0;

	if (np) {
		display_np = of_parse_phandle(np, "display", 0);
		if (!display_np) {
			dev_err(dev->dev, "failed to find display phandle\n");
			return num_modes;
		}
		of_get_drm_display_mode(display_np, mode, OF_USE_NATIVE_MODE);
		mode->type |= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
		drm_mode_probed_add(connector, mode);
		num_modes++;
	}

	return num_modes;
}

static int fsl_dcu_drm_connector_mode_valid(struct drm_connector *connector,
					    struct drm_display_mode *mode)
{
	return MODE_OK;
}

static struct drm_encoder *
fsl_dcu_drm_connector_best_encoder(struct drm_connector *connector)
{
	struct fsl_dcu_drm_connector *fsl_con = to_fsl_dcu_connector(connector);

	return fsl_con->encoder;
}

static void fsl_dcu_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
fsl_dcu_drm_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs fsl_dcu_drm_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.detect = fsl_dcu_drm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = fsl_dcu_drm_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.get_modes = fsl_dcu_drm_connector_get_modes,
	.mode_valid = fsl_dcu_drm_connector_mode_valid,
	.best_encoder = fsl_dcu_drm_connector_best_encoder,
};

int fsl_dcu_drm_connector_create(struct fsl_dcu_drm_device *fsl_dev,
				 struct drm_encoder *encoder)
{
	struct drm_connector *connector = &fsl_dev->connector.connector;
	int ret;

	fsl_dev->connector.encoder = encoder;

	connector->display_info.width_mm = 0;
	connector->display_info.height_mm = 0;

	ret = drm_connector_init(fsl_dev->ddev, connector,
				 &fsl_dcu_drm_connector_funcs,
				 DRM_MODE_CONNECTOR_LVDS);
	if (ret < 0)
		return ret;

	connector->dpms = DRM_MODE_DPMS_OFF;
	drm_connector_helper_add(connector, &connector_helper_funcs);
	ret = drm_connector_register(connector);
	if (ret < 0)
		goto err_cleanup;

	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		goto err_sysfs;

	connector->encoder = encoder;

	drm_object_property_set_value
		(&connector->base, fsl_dev->ddev->mode_config.dpms_property,
		DRM_MODE_DPMS_OFF);

	return 0;

err_sysfs:
	drm_connector_unregister(connector);
err_cleanup:
	drm_connector_cleanup(connector);
	return ret;
}
