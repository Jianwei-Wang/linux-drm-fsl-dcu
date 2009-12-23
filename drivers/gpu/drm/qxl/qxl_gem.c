#include "drmP.h"
#include "drm.h"
#include "qxl_drm.h"
#include "qxl_drv.h"
#include "qxl_object.h"

int qxl_gem_object_init(struct drm_gem_object *obj)
{
	/* we do nothings here */
	return 0;
}

void qxl_gem_object_free(struct drm_gem_object *gobj)
{
	struct qxl_bo *robj = gobj->driver_private;

	gobj->driver_private = NULL;
	if (robj) {
		qxl_bo_unref(&robj);
	}
}

int qxl_gem_object_create(struct qxl_device *rdev, int size,
				int alignment, int initial_domain,
				bool discardable, bool kernel,
				struct drm_gem_object **obj)
{
	struct drm_gem_object *gobj;
	struct qxl_bo *robj;
	int r;

	*obj = NULL;
	gobj = drm_gem_object_alloc(rdev->ddev, size);
	if (!gobj) {
		return -ENOMEM;
	}
	/* At least align on page size */
	if (alignment < PAGE_SIZE) {
		alignment = PAGE_SIZE;
	}
	r = qxl_bo_create(rdev, gobj, size, kernel, initial_domain, &robj);
	if (r) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to allocate GEM object (%d, %d, %u, %d)\n",
				  size, initial_domain, alignment, r);
		mutex_lock(&rdev->ddev->struct_mutex);
		drm_gem_object_unreference(gobj);
		mutex_unlock(&rdev->ddev->struct_mutex);
		return r;
	}
	gobj->driver_private = robj;
	*obj = gobj;
	return 0;
}

int qxl_gem_object_pin(struct drm_gem_object *obj, uint32_t pin_domain,
			  uint64_t *gpu_addr)
{
	struct qxl_bo *robj = obj->driver_private;
	int r;

	r = qxl_bo_reserve(robj, false);
	if (unlikely(r != 0))
		return r;
	r = qxl_bo_pin(robj, pin_domain, gpu_addr);
	qxl_bo_unreserve(robj);
	return r;
}

void qxl_gem_object_unpin(struct drm_gem_object *obj)
{
	struct qxl_bo *robj = obj->driver_private;
	int r;

	r = qxl_bo_reserve(robj, false);
	if (likely(r == 0)) {
		qxl_bo_unpin(robj);
		qxl_bo_unreserve(robj);
	}
}

int qxl_gem_set_domain(struct drm_gem_object *gobj,
			  uint32_t rdomain, uint32_t wdomain)
{
	struct qxl_bo *robj;
	uint32_t domain;
	int r;

	/* FIXME: reeimplement */
	robj = gobj->driver_private;
	/* work out where to validate the buffer to */
	domain = wdomain;
	if (!domain) {
		domain = rdomain;
	}
	if (!domain) {
		/* Do nothings */
		printk(KERN_WARNING "Set domain withou domain !\n");
		return 0;
	}
	if (domain == QXL_GEM_DOMAIN_CPU) {
		/* Asking for cpu access wait for object idle */
		r = qxl_bo_wait(robj, NULL, false);
		if (r) {
			printk(KERN_ERR "Failed to wait for object !\n");
			return r;
		}
	}
	return 0;
}

int qxl_gem_init(struct qxl_device *rdev)
{
	INIT_LIST_HEAD(&rdev->gem.objects);
	return 0;
}

void qxl_gem_fini(struct qxl_device *rdev)
{
  //	qxl_bo_force_delete(rdev);
}
