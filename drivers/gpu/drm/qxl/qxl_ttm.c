
#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_module.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/qxl_drm.h>
#include "qxl_drv.h"

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

static struct qxl_device *qxl_get_qdev(struct ttm_bo_device *bdev)
{
	struct qxl_mman *mman;
	struct qxl_device *qdev;

	mman = container_of(bdev, struct qxl_mman, bdev);
	qdev = container_of(mman, struct qxl_device, mman);
	return qdev;
}
static int qxl_ttm_mem_global_init(struct ttm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void qxl_ttm_mem_global_release(struct ttm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

static int qxl_ttm_global_init(struct qxl_device *qdev)
{
	struct ttm_global_reference *global_ref;
	int r;

	qdev->mman.mem_global_referenced = false;
	global_ref = &qdev->mman.mem_global_ref;
	global_ref->global_type = TTM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &qxl_ttm_mem_global_init;
	global_ref->release = &qxl_ttm_mem_global_release;

	r = ttm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM memory accounting "
			  "subsystem.\n");
		return r;
	}

	qdev->mman.bo_global_ref.mem_glob =
		qdev->mman.mem_global_ref.object;
	global_ref = &qdev->mman.bo_global_ref.ref;
	global_ref->global_type = TTM_GLOBAL_TTM_BO;
	global_ref->size = sizeof(struct ttm_bo_global);
	global_ref->init = &ttm_bo_global_init;
	global_ref->release = &ttm_bo_global_release;
	r = ttm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed setting up TTM BO subsystem.\n");
		ttm_global_item_unref(&qdev->mman.mem_global_ref);
		return r;
	}

	qdev->mman.mem_global_referenced = true;
	return 0;
}

static void qxl_ttm_global_fini(struct qxl_device *qdev)
{
	if (qdev->mman.mem_global_referenced) {
		ttm_global_item_unref(&qdev->mman.bo_global_ref.ref);
		ttm_global_item_unref(&qdev->mman.mem_global_ref);
		qdev->mman.mem_global_referenced = false;
	}
}


static int qxl_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	return 0;
}

static int qxl_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
			     struct ttm_mem_type_manager *man)
{
	struct qxl_device *qdev;

	qdev = qxl_get_qdev(bdev);

	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
		/* "On-card" video ram */
		man->gpu_offset = 0;
		man->flags = TTM_MEMTYPE_FLAG_FIXED |
			     TTM_MEMTYPE_FLAG_NEEDS_IOREMAP |
			     TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		man->io_addr = NULL;
		man->io_offset = qdev->vram_base;
		man->io_size = qdev->vram_size;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static void qxl_evict_flags(struct ttm_buffer_object *bo,
				struct ttm_placement *placement)
{
	struct qxl_bo *rbo;
	static u32 placements = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;

#if 0
	if (!qxl_ttm_bo_is_qxl_bo(bo)) {
		placement->fpfn = 0;
		placement->lpfn = 0;
		placement->placement = &placements;
		placement->busy_placement = &placements;
		placement->num_placement = 1;
		placement->num_busy_placement = 1;
		return;
	}
	rbo = container_of(bo, struct qxl_bo, tbo);
	qxl_ttm_placement_from_domain(rbo, QXL_GEM_DOMAIN_CPU);
	*placement = rbo->placement;
#endif

}

static int qxl_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	return 0;
}


static struct ttm_bo_driver qxl_bo_driver = {
//	.create_ttm_backend_entry = &qxl_create_ttm_backend_entry,
	.invalidate_caches = &qxl_invalidate_caches,
	.init_mem_type = &qxl_init_mem_type,
	.evict_flags = &qxl_evict_flags,
//	.move = &qxl_bo_move,
	.verify_access = &qxl_verify_access,
#if 0
	.sync_obj_signaled = &qxl_sync_obj_signaled,
	.sync_obj_wait = &qxl_sync_obj_wait,
	.sync_obj_flush = &qxl_sync_obj_flush,
	.sync_obj_unref = &qxl_sync_obj_unref,
	.sync_obj_ref = &qxl_sync_obj_ref,
	.move_notify = &qxl_bo_move_notify,
	.fault_reserve_notify = &qxl_bo_fault_reserve_notify,
#endif
};

int qxl_ttm_init(struct qxl_device *qdev)
{
	int r;

	r = qxl_ttm_global_init(qdev);
	if (r) {
		return r;
	}
	/* No others user of address space so set it to 0 */
	r = ttm_bo_device_init(&qdev->mman.bdev,
			       qdev->mman.bo_global_ref.ref.object,
			       &qxl_bo_driver, DRM_FILE_PAGE_OFFSET, 0);
	if (r) {
		DRM_ERROR("failed initializing buffer object driver(%d).\n", r);
		return r;
	}
	r = ttm_bo_init_mm(&qdev->mman.bdev, TTM_PL_VRAM,
			   qdev->vram_size >> PAGE_SHIFT);
	if (r) {
		DRM_ERROR("Failed initializing VRAM heap.\n");
		return r;
	}
	DRM_INFO("qxl: %uM of VRAM memory ready\n",
		 (unsigned)qdev->vram_size / (1024 * 1024));
	if (unlikely(qdev->mman.bdev.dev_mapping == NULL)) {
		qdev->mman.bdev.dev_mapping = qdev->ddev->dev_mapping;
	}
}

void qxl_ttm_fini(struct qxl_device *qdev)
{
	int r;

	ttm_bo_clean_mm(&qdev->mman.bdev, TTM_PL_VRAM);
	ttm_bo_device_release(&qdev->mman.bdev);
	qxl_ttm_global_fini(qdev);
	DRM_INFO("qxl: ttm finalized\n");
}

static struct ttm_backend_func qxl_backend_func = {
//	.populate = &qxl_ttm_backend_populate,
//	.clear = &qxl_ttm_backend_clear,
//	.bind = &qxl_ttm_backend_bind,
//	.unbind = &qxl_ttm_backend_unbind,
//	.destroy = &qxl_ttm_backend_destroy,
};

