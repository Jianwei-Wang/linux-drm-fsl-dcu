
#include <drm/drmP.h>
#include "virtgpu_drv.h"

int virtgpu_gem_object_init(struct drm_gem_object *obj)
{
	/* we do nothings here */
	return 0;
}

void virtgpu_gem_object_free(struct drm_gem_object *gobj)
{
	struct virtgpu_bo *qobj = gem_to_virtgpu_bo(gobj);

	if (qobj)
		virtgpu_bo_unref(&qobj);
}

int virtgpu_gem_init(struct virtgpu_device *qdev)
{
	return 0;
}

void virtgpu_gem_fini(struct virtgpu_device *qdev)
{
}
