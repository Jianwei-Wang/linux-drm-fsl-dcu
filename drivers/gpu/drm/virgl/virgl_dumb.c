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

#include "virgl_drv.h"
#include "virgl_object.h"

/* dumb ioctls implementation */

int virgl_mode_dumb_create(struct drm_file *file_priv,
			    struct drm_device *dev,
			    struct drm_mode_create_dumb *args)
{
	struct virgl_device *qdev = dev->dev_private;
	struct virgl_bo *qobj;
	uint32_t handle;
	int r;
	uint32_t pitch;

	pitch = args->width * ((args->bpp + 1) / 8);
	args->size = pitch * args->height;
	args->size = ALIGN(args->size, PAGE_SIZE);

	r = virgl_gem_object_create_with_handle(qdev, file_priv, 0,
					      args->size, &qobj,
					      &handle);
	if (r)
		return r;

	r = virgl_create_3d_fb_res(qdev, args->width, args->height, &qobj->res_handle);
	if (r)
		return r;

	args->pitch = pitch;
	args->handle = handle;
	return 0;
}

int virgl_mode_dumb_destroy(struct drm_file *file_priv,
			     struct drm_device *dev,
			     uint32_t handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}

int virgl_mode_dumb_mmap(struct drm_file *file_priv,
		       struct drm_device *dev,
		       uint32_t handle, uint64_t *offset_p)
{
	struct drm_gem_object *gobj;
	struct virgl_bo *qobj;

	BUG_ON(!offset_p);
	gobj = drm_gem_object_lookup(dev, file_priv, handle);
	if (gobj == NULL)
		return -ENOENT;
	qobj = gem_to_virgl_bo(gobj);
	*offset_p = virgl_bo_mmap_offset(qobj);
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}
