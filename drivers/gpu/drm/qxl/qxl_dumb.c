#include "qxl_drv.h"
#include "qxl_object.h"

/* dumb ioctls implementation */

int qxl_mode_dumb_create(struct drm_file *file_priv,
			    struct drm_device *dev,
			    struct drm_mode_create_dumb *args)
{
	struct qxl_device *qdev = dev->dev_private;
	struct qxl_bo *qobj;
	uint32_t handle;
	int r;

	args->pitch = args->width * ((args->bpp + 1) / 8);
	args->size = args->pitch * args->height;
	args->size = ALIGN(args->size, PAGE_SIZE);

	r = qxl_gem_object_create_with_handle(qdev, file_priv,
					      QXL_GEM_DOMAIN_VRAM,
					      args->size, &qobj,
					      &handle);
	DRM_INFO("%s: width %d, height %d, bpp %d, pitch %d, size %lld, %s\n",
		 __func__, args->width, args->height, args->bpp,
		 args->pitch, args->size, r ? "failed" : "success");
	if (r)
		return r;
	args->handle = handle;
	DRM_INFO("%s: kptr %p\n", __func__, qobj->kptr);
	return 0;
}

int qxl_mode_dumb_destroy(struct drm_file *file_priv,
			     struct drm_device *dev,
			     uint32_t handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}

int qxl_mode_dumb_mmap(struct drm_file *file_priv,
		       struct drm_device *dev,
		       uint32_t handle, uint64_t *offset_p)
{
	struct drm_gem_object *gobj;
	struct qxl_bo *qobj;

	BUG_ON(!offset_p);
	gobj = drm_gem_object_lookup(dev, file_priv, handle);
	DRM_INFO("%s: %d, %s\n", __func__, handle, gobj ? "success" : "failed");
	if (gobj == NULL)
		return -ENOENT;
	qobj = gem_to_qxl_bo(gobj);
	*offset_p = qxl_bo_mmap_offset(qobj);
	DRM_INFO("%s: %p, %lld| %lld, %ld, %ld\n", __func__, gobj, *offset_p,
		qobj->tbo.addr_space_offset,
		qobj->tbo.vm_node ?
		qobj->tbo.vm_node->start : -1,
		qobj->tbo.num_pages);
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}
