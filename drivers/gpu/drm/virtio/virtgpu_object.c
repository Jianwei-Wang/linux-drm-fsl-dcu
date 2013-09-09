#include <drm/drmP.h>
#include "virtgpu_drv.h"

static void virtgpu_ttm_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct virtgpu_bo *bo;

	bo = container_of(tbo, struct virtgpu_bo, tbo);

	drm_gem_object_release(&bo->gem_base);
	kfree(bo);
}

bool virtgpu_ttm_bo_is_virtgpu_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &virtgpu_ttm_bo_destroy)
		return true;
	return false;
}

void virtgpu_bo_unref(struct virtgpu_bo **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;
	tbo = &((*bo)->tbo);
	ttm_bo_unref(&tbo);
	if (tbo == NULL)
		*bo = NULL;
}
