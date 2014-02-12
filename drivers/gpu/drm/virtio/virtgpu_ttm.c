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

#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_page_alloc.h>
#include <ttm/ttm_module.h>
#include <drm/drmP.h>
#include <drm/drm.h>
#include <drm/virtgpu_drm.h>
#include "virtgpu_drv.h"

#include <linux/delay.h>

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

static struct virtgpu_device *virtgpu_get_vgdev(struct ttm_bo_device *bdev)
{
	struct virtgpu_mman *mman;
	struct virtgpu_device *vgdev;

	mman = container_of(bdev, struct virtgpu_mman, bdev);
	vgdev = container_of(mman, struct virtgpu_device, mman);
	return vgdev;
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
	global_ref->release = &virtgpu_ttm_mem_global_release;

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

static struct vm_operations_struct virtgpu_ttm_vm_ops;
static const struct vm_operations_struct *ttm_vm_ops;

static int virtgpu_ttm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct ttm_buffer_object *bo;
	struct virtgpu_device *vgdev;
	int r;

	bo = (struct ttm_buffer_object *)vma->vm_private_data;
	if (bo == NULL)
		return VM_FAULT_NOPAGE;
	vgdev = virtgpu_get_vgdev(bo->bdev);
	r = ttm_vm_ops->fault(vma, vmf);
	return r;
}

int virtgpu_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct virtgpu_device *vgdev;
	int r;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET)) {
		pr_info("%s: vma->vm_pgoff (%ld) < DRM_FILE_PAGE_OFFSET\n",
			__func__, vma->vm_pgoff);
		return drm_mmap(filp, vma);
	}

	file_priv = filp->private_data;
	vgdev = file_priv->minor->dev->dev_private;
	if (vgdev == NULL) {
		DRM_ERROR(
		 "filp->private_data->minor->dev->dev_private == NULL\n");
		return -EINVAL;
	}
	r = ttm_bo_mmap(filp, vma, &vgdev->mman.bdev);
	if (unlikely(r != 0))
		return r;
	if (unlikely(ttm_vm_ops == NULL)) {
		ttm_vm_ops = vma->vm_ops;
		virtgpu_ttm_vm_ops = *ttm_vm_ops;
		virtgpu_ttm_vm_ops.fault = &virtgpu_ttm_fault;
	}
	vma->vm_ops = &virtgpu_ttm_vm_ops;
	return 0;
}

static int virtgpu_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	return 0;
}

static int virtgpu_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
			     struct ttm_mem_type_manager *man)
{
	struct virtgpu_device *vgdev;

	vgdev = virtgpu_get_vgdev(bdev);

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

static void virtgpu_evict_flags(struct ttm_buffer_object *bo,
				struct ttm_placement *placement)
{
	static u32 placements = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;

	placement->fpfn = 0;
	placement->lpfn = 0;
	placement->placement = &placements;
	placement->busy_placement = &placements;
	placement->num_placement = 1;
	placement->num_busy_placement = 1;
	return;
}

static int virtgpu_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	return 0;
}

static int virtgpu_ttm_io_mem_reserve(struct ttm_bo_device *bdev,
				  struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];

	mem->bus.addr = NULL;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	mem->bus.is_iomem = false;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
		/* system memory */
		return 0;
	default:
		return -EINVAL;
	}
	return 0;
}

static void virtgpu_ttm_io_mem_free(struct ttm_bo_device *bdev,
				struct ttm_mem_reg *mem)
{
}

/*
 * TTM backend functions.
 */
struct virtgpu_ttm_tt {
	struct ttm_dma_tt		ttm;
	struct virtgpu_device		*vgdev;
	u64				offset;
};

static int virtgpu_ttm_backend_bind(struct ttm_tt *ttm,
				struct ttm_mem_reg *bo_mem)
{
	struct virtgpu_ttm_tt *gtt = (void *)ttm;

	gtt->offset = (unsigned long)(bo_mem->start << PAGE_SHIFT);
	if (!ttm->num_pages) {
		WARN(1, "nothing to bind %lu pages for mreg %p back %p!\n",
		     ttm->num_pages, bo_mem, ttm);
	}
	/* Not implemented */
	return -1;
}

static int virtgpu_ttm_backend_unbind(struct ttm_tt *ttm)
{
	/* Not implemented */
	return -1;
}

static void virtgpu_ttm_backend_destroy(struct ttm_tt *ttm)
{
	struct virtgpu_ttm_tt *gtt = (void *)ttm;

	ttm_dma_tt_fini(&gtt->ttm);
	kfree(gtt);
}

static struct ttm_backend_func virtgpu_backend_func = {
	.bind = &virtgpu_ttm_backend_bind,
	.unbind = &virtgpu_ttm_backend_unbind,
	.destroy = &virtgpu_ttm_backend_destroy,
};

static int virtgpu_ttm_tt_populate(struct ttm_tt *ttm)
{
	int r;

	if (ttm->state != tt_unpopulated)
		return 0;

	r = ttm_pool_populate(ttm);
	if (r)
		return r;

	return 0;
}

static void virtgpu_ttm_tt_unpopulate(struct ttm_tt *ttm)
{
	ttm_pool_unpopulate(ttm);
}

struct ttm_tt *virtgpu_ttm_tt_create(struct ttm_bo_device *bdev,
				 unsigned long size, uint32_t page_flags,
				 struct page *dummy_read_page)
{
	struct virtgpu_device *vgdev;
	struct virtgpu_ttm_tt *gtt;

	vgdev = virtgpu_get_vgdev(bdev);
	gtt = kzalloc(sizeof(struct virtgpu_ttm_tt), GFP_KERNEL);
	if (gtt == NULL)
		return NULL;
	gtt->ttm.ttm.func = &virtgpu_backend_func;
	gtt->vgdev = vgdev;
	if (ttm_dma_tt_init(&gtt->ttm, bdev, size, page_flags,
			    dummy_read_page)) {
		kfree(gtt);
		return NULL;
	}
	return &gtt->ttm.ttm;
}

static void virtgpu_move_null(struct ttm_buffer_object *bo,
			     struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;

	BUG_ON(old_mem->mm_node != NULL);
	*old_mem = *new_mem;
	new_mem->mm_node = NULL;
}

static int virtgpu_bo_move(struct ttm_buffer_object *bo,
		       bool evict, bool interruptible,
		       bool no_wait_gpu,
		       struct ttm_mem_reg *new_mem)
{
	virtgpu_move_null(bo, new_mem);
	return 0;
}


static int virtgpu_sync_obj_wait(void *sync_obj,
			     bool lazy, bool interruptible)
{
	return virtgpu_fence_wait((struct virtgpu_fence *)sync_obj, interruptible);
}

static int virtgpu_sync_obj_flush(void *sync_obj)
{
	return 0;
}

static void virtgpu_sync_obj_unref(void **sync_obj)
{
	virtgpu_fence_unref((struct virtgpu_fence **)sync_obj);
}

static void *virtgpu_sync_obj_ref(void *sync_obj)
{
	return virtgpu_fence_ref((struct virtgpu_fence *)sync_obj);
}

static bool virtgpu_sync_obj_signaled(void *sync_obj)
{
	return virtgpu_fence_signaled((struct virtgpu_fence *)sync_obj, true);
}

static void virtgpu_bo_move_notify(struct ttm_buffer_object *bo,
			       struct ttm_mem_reg *new_mem)
{
	return;
}

static void virtgpu_bo_swap_notify(struct ttm_buffer_object *tbo)
{
	struct virtgpu_object *bo;
	struct virtgpu_device *vgdev;
	bo = container_of(tbo, struct virtgpu_object, tbo);
	vgdev = (struct virtgpu_device *)bo->gem_base.dev->dev_private;

	virtgpu_cmd_resource_inval_backing(vgdev, bo->hw_res_handle);
	if (bo->pages) {
		virtgpu_object_free_sg_table(bo);
	}
	printk(KERN_ERR "swapping bo %p\n", bo);
	/* should we invalidate the mapping? */
}

static struct ttm_bo_driver virtgpu_bo_driver = {
	.ttm_tt_create = &virtgpu_ttm_tt_create,
	.ttm_tt_populate = &virtgpu_ttm_tt_populate,
	.ttm_tt_unpopulate = &virtgpu_ttm_tt_unpopulate,
	.invalidate_caches = &virtgpu_invalidate_caches,
	.init_mem_type = &virtgpu_init_mem_type,
	.evict_flags = &virtgpu_evict_flags,
	.move = &virtgpu_bo_move,
	.verify_access = &virtgpu_verify_access,
	.io_mem_reserve = &virtgpu_ttm_io_mem_reserve,
	.io_mem_free = &virtgpu_ttm_io_mem_free,
	.sync_obj_signaled = &virtgpu_sync_obj_signaled,
	.sync_obj_wait = &virtgpu_sync_obj_wait,
	.sync_obj_flush = &virtgpu_sync_obj_flush,
	.sync_obj_unref = &virtgpu_sync_obj_unref,
	.sync_obj_ref = &virtgpu_sync_obj_ref,
	.move_notify = &virtgpu_bo_move_notify,
	.swap_notify = &virtgpu_bo_swap_notify,
};

int virtgpu_ttm_init(struct virtgpu_device *vgdev)
{
	int r;

	r = virtgpu_ttm_global_init(vgdev);
	if (r)
		return r;
	/* No others user of address space so set it to 0 */
	r = ttm_bo_device_init(&vgdev->mman.bdev,
			       vgdev->mman.bo_global_ref.ref.object,
			       &virtgpu_bo_driver, DRM_FILE_PAGE_OFFSET, 0);
	if (r) {
		DRM_ERROR("failed initializing buffer object driver(%d).\n", r);
		return r;
	}

	return 0;
}

void virtgpu_ttm_fini(struct virtgpu_device *vgdev)
{
	ttm_bo_device_release(&vgdev->mman.bdev);
	virtgpu_ttm_global_fini(vgdev);
	DRM_INFO("virtgpu: ttm finalized\n");
}
