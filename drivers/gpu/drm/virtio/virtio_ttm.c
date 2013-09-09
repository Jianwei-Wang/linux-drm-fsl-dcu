#include <drm/drmP.h>
#include <drm/drm.h>

#include "virtio_drv.h"


#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

static struct virtgpu_device *virtgpu_get_dev(struct ttm_bo_device *bdev)
{
	struct virtgpu_mman *mman;
	struct virtgpu_device *vdev;

	mman = container_of(bdev, struct virtgpu_mman, bdev);
	vdev = container_of(mman, struct virtgpu_device, mman);
	return vdev;
}


static int virtgpu_ttm_mem_global_init(struct drm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void virtgpu_ttm_mem_global_release(struct drm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

static int virtgpu_ttm_global_init(struct virtgpu_device *vgdev)
{
	struct drm_global_reference *global_ref;
	int r;

	vgdev->mman.mem_global_referenced = false;
	global_ref = &vgdev->mman.mem_global_ref;
	global_ref->global_type = DRM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &virtgpu_ttm_mem_global_init;
	global_ref->release = &virtgpu_ttm_mem_global_release
;
	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM memory accounting "
			  "subsystem.\n");
		return r;
	}

	vgdev->mman.bo_global_ref.mem_glob =
		vgdev->mman.mem_global_ref.object;
	global_ref = &vgdev->mman.bo_global_ref.ref;
	global_ref->global_type = DRM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;
	r = drm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM BO subsystem.\n");
		drm_global_item_unref(&vgdev->mman.mem_global_ref);
		return r;
	}

	vgdev->mman.mem_global_referenced = true;
	return 0;
}

static void virtgpu_ttm_global_fini(struct virtgpu_device *vgdev)
{
	if (vgdev->mman.mem_global_referenced) {
		drm_global_item_unref(&vgdev->mman.bo_global_ref.ref);
		drm_global_item_unref(&vgdev->mman.mem_global_ref);
		vgdev->mman.mem_global_referenced = false;
	}
}

static int virtgpu_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
			       struct ttm_mem_type_manager *man)
{
	struct virtgpu_device *vgdev;

	vgdev = virtgpu_get_dev(bdev);

	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static struct ttm_bo_driver virtgpu_bo_driver = {
	.init_mem_type = &virtgpu_init_mem_type,
};

int virtgpu_ttm_init(struct virtgpu_device *vgdev)
{
	int ret;

	ret = virtgpu_ttm_global_init(vgdev);
	if (ret)
		return ret;

	ret = ttm_bo_device_init(&vgdev->mman.bdev,
				 vgdev->mman.bo_global_ref.ref.object,
				 &virtgpu_bo_driver, DRM_FILE_PAGE_OFFSET, 0);
	if (ret)
		return ret;
	return 0;
}

void virtgpu_ttm_fini(struct virtgpu_device *vgdev)
{
	ttm_bo_device_release(&vgdev->mman.bdev);
	virtgpu_ttm_global_fini(vgdev);
}
