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

#include "drmP.h"
#include "drm/drm.h"
#include "virgl_drv.h"
#include "virgl_object.h"

int virgl_gem_object_init(struct drm_gem_object *obj)
{
	/* we do nothings here */
	return 0;
}

void virgl_gem_object_free(struct drm_gem_object *gobj)
{
	struct virgl_bo *qobj = gem_to_virgl_bo(gobj);

	if (qobj)
		virgl_bo_unref(&qobj);
}

int virgl_gem_object_create(struct virgl_device *qdev, int size,
			  int alignment, int initial_domain,
			  bool discardable, bool kernel,
			  struct drm_gem_object **obj)
{
	struct virgl_bo *qbo;
	int r;

	*obj = NULL;
	/* At least align on page size */
	if (alignment < PAGE_SIZE)
		alignment = PAGE_SIZE;
	r = virgl_bo_create(qdev, size, kernel, initial_domain, &qbo);
	if (r) {
		if (r != -ERESTARTSYS)
			DRM_ERROR(
			"Failed to allocate GEM object (%d, %d, %u, %d)\n",
				  size, initial_domain, alignment, r);
		return r;
	}
	*obj = &qbo->gem_base;

	spin_lock(&qdev->gem.lock);
	list_add_tail(&qbo->list, &qdev->gem.objects);
	spin_unlock(&qdev->gem.lock);

	return 0;
}

int virgl_gem_object_create_with_handle(struct virgl_device *qdev,
				      struct drm_file *file_priv,
				      u32 domain,
				      size_t size,
				      struct virgl_bo **qobj,
				      uint32_t *handle)
{
	struct drm_gem_object *gobj;
	int r;

	BUG_ON(!qobj);
	BUG_ON(!handle);

	r = virgl_gem_object_create(qdev, size, 0,
				  domain,
				  false, false,
				  &gobj);
	if (r)
		return -ENOMEM;
	r = drm_gem_handle_create(file_priv, gobj, handle);
	if (r)
		return r;
	/* drop reference from allocate - handle holds it now */
	*qobj = gem_to_virgl_bo(gobj);
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

int virgl_gem_object_pin(struct drm_gem_object *obj, uint32_t pin_domain,
			  uint64_t *gpu_addr)
{
	struct virgl_bo *qobj = obj->driver_private;
	int r;

	r = virgl_bo_reserve(qobj, false);
	if (unlikely(r != 0))
		return r;
	r = virgl_bo_pin(qobj, pin_domain, gpu_addr);
	virgl_bo_unreserve(qobj);
	return r;
}

void virgl_gem_object_unpin(struct drm_gem_object *obj)
{
	struct virgl_bo *qobj = obj->driver_private;
	int r;

	r = virgl_bo_reserve(qobj, false);
	if (likely(r == 0)) {
		virgl_bo_unpin(qobj);
		virgl_bo_unreserve(qobj);
	}
}

int virgl_gem_object_open(struct drm_gem_object *obj,
			  struct drm_file *file)
{
	struct virgl_device *qdev = (struct virgl_device *)obj->dev->dev_private;
	struct virgl_fpriv *vfpriv = file->driver_priv;
	struct virgl_bo *qobj = gem_to_virgl_bo(obj);
	int r;

	r = virgl_bo_reserve(qobj, false);
	if (r)
		return r;

	r = virgl_context_bind_resource(qdev, vfpriv->ctx_id, qobj->res_handle);
	virgl_bo_unreserve(qobj);
	return r;
}

void virgl_gem_object_close(struct drm_gem_object *obj,
			    struct drm_file *file)
{
	struct virgl_device *qdev = (struct virgl_device *)obj->dev->dev_private;
	struct virgl_fpriv *vfpriv = file->driver_priv;
	struct virgl_bo *qobj = gem_to_virgl_bo(obj);
	int r;

	r = virgl_bo_reserve(qobj, false);
	if (r)
		return r;

	r = virgl_context_unbind_resource(qdev, vfpriv->ctx_id, qobj->res_handle);
	virgl_bo_unreserve(qobj);
	return r;
}

int virgl_gem_init(struct virgl_device *qdev)
{
	INIT_LIST_HEAD(&qdev->gem.objects);
	return 0;
}

void virgl_gem_fini(struct virgl_device *qdev)
{
	virgl_bo_force_delete(qdev);
}
