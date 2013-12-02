#include "virtgpu_drv.h"

static void virtgpu_ttm_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct virtgpu_object *bo;
	struct virtgpu_device *vgdev;

	bo = container_of(tbo, struct virtgpu_object, tbo);
	vgdev = (struct virtgpu_device *)bo->gem_base.dev->dev_private;

	//	if (bo->res_handle)
	//		virtgpu_resource_unref(vgdev, bo->res_handle);
	//	if (bo->pages)
	//		virtgpu_object_free_sg_table(bo);
	drm_gem_object_release(&bo->gem_base);
	kfree(bo);
}

bool virtgpu_ttm_bo_is_virtgpu_object(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &virtgpu_ttm_bo_destroy)
		return true;
	return false;
}

void virtgpu_ttm_placement_from_domain(struct virtgpu_object *qbo, u32 domain)
{
	u32 c = 1;

	qbo->placement.fpfn = 0;
	qbo->placement.lpfn = 0;
	qbo->placement.placement = &qbo->placement_code;
	qbo->placement.busy_placement = &qbo->placement_code;
	qbo->placement_code = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	qbo->placement.num_placement = c;
	qbo->placement.num_busy_placement = c;
}
