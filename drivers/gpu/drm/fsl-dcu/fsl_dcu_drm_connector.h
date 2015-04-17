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

#ifndef __FSL_DCU_DRM_CONNECTOR_H__
#define __FSL_DCU_DRM_CONNECTOR_H__

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include "fsl_dcu_drm_crtc.h"

struct fsl_dcu_drm_device;
struct fsl_dcu_drm_connector {
	struct drm_connector connector;
	struct drm_encoder *encoder;
};

int fsl_dcu_drm_encoder_create(struct fsl_dcu_drm_device *fsl_dev,
			       struct drm_crtc *crtc);
int fsl_dcu_drm_connector_create(struct fsl_dcu_drm_device *fsl_dev,
				 struct drm_encoder *encoder);

#endif /* __FSL_DCU_DRM_CONNECTOR_H__ */
