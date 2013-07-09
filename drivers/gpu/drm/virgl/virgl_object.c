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

#include <linux/io-mapping.h>
static void virgl_ttm_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct virgl_bo *bo;
	struct virgl_device *qdev;

	bo = container_of(tbo, struct virgl_bo, tbo);
	qdev = (struct virgl_device *)bo->gem_base.dev->dev_private;

	if (bo->res_handle)
		virgl_resource_unref(qdev, bo->res_handle);
	if (bo->sgt)
		virgl_bo_free_sg_table(bo);
	spin_lock(&qdev->gem.lock);
	list_del_init(&bo->list);
	spin_unlock(&qdev->gem.lock);
	drm_gem_object_release(&bo->gem_base);
	kfree(bo);
}

bool virgl_ttm_bo_is_virgl_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &virgl_ttm_bo_destroy)
		return true;
	return false;
}

void virgl_ttm_placement_from_domain(struct virgl_bo *qbo, u32 domain)
{
	u32 c = 0;

	qbo->placement.fpfn = 0;
	qbo->placement.lpfn = 0;
	qbo->placement.placement = qbo->placements;
	qbo->placement.busy_placement = qbo->placements;
	qbo->placements[c++] = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	qbo->placement.num_placement = c;
	qbo->placement.num_busy_placement = c;
}


int virgl_bo_create(struct virgl_device *qdev,
		  unsigned long size, bool kernel, u32 domain,
		  struct virgl_bo **bo_ptr)
{
	struct virgl_bo *bo;
	enum ttm_bo_type type;
	int r;

	if (unlikely(qdev->mman.bdev.dev_mapping == NULL))
		qdev->mman.bdev.dev_mapping = qdev->ddev->dev_mapping;
	if (kernel)
		type = ttm_bo_type_kernel;
	else
		type = ttm_bo_type_device;
	*bo_ptr = NULL;
	bo = kzalloc(sizeof(struct virgl_bo), GFP_KERNEL);
	if (bo == NULL)
		return -ENOMEM;
	size = roundup(size, PAGE_SIZE);
	r = drm_gem_object_init(qdev->ddev, &bo->gem_base, size);
	if (unlikely(r)) {
		kfree(bo);
		return r;
	}
	bo->gem_base.driver_private = NULL;
	bo->type = domain;
	bo->pin_count = 0;
	INIT_LIST_HEAD(&bo->list);

	virgl_ttm_placement_from_domain(bo, domain);

	r = ttm_bo_init(&qdev->mman.bdev, &bo->tbo, size, type,
			&bo->placement, 0, !kernel, NULL, size,
			NULL, &virgl_ttm_bo_destroy);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			dev_err(qdev->dev,
				"object_init failed for (%lu, 0x%08X)\n",
				size, domain);
		return r;
	}
	*bo_ptr = bo;
	return 0;
}

int virgl_bo_kmap(struct virgl_bo *bo, void **ptr)
{
	bool is_iomem;
	int r;

	if (bo->kptr) {
		if (ptr)
			*ptr = bo->kptr;
		return 0;
	}
	r = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages, &bo->kmap);
	if (r)
		return r;
	bo->kptr = ttm_kmap_obj_virtual(&bo->kmap, &is_iomem);
	if (ptr)
		*ptr = bo->kptr;
	return 0;
}

void virgl_bo_kunmap(struct virgl_bo *bo)
{
	if (bo->kptr == NULL)
		return;
	bo->kptr = NULL;
	ttm_bo_kunmap(&bo->kmap);
}

void virgl_bo_unref(struct virgl_bo **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;
	tbo = &((*bo)->tbo);
	ttm_bo_unref(&tbo);
	if (tbo == NULL)
		*bo = NULL;
}

struct virgl_bo *virgl_bo_ref(struct virgl_bo *bo)
{
	ttm_bo_reference(&bo->tbo);
	return bo;
}

int virgl_bo_pin(struct virgl_bo *bo, u32 domain, u64 *gpu_addr)
{
	struct virgl_device *qdev = (struct virgl_device *)bo->gem_base.dev->dev_private;
	int r, i;

	bo->pin_count++;
	if (gpu_addr)
	  *gpu_addr = NULL;
	return 0;
}

int virgl_bo_unpin(struct virgl_bo *bo)
{
	struct virgl_device *qdev = (struct virgl_device *)bo->gem_base.dev->dev_private;
	int r, i;

	if (!bo->pin_count) {
		dev_warn(qdev->dev, "%p unpin not necessary\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;
	return 0;
}

void virgl_bo_force_delete(struct virgl_device *qdev)
{
	struct virgl_bo *bo, *n;

	if (list_empty(&qdev->gem.objects))
		return;
	dev_err(qdev->dev, "Userspace still has active objects !\n");
	list_for_each_entry_safe(bo, n, &qdev->gem.objects, list) {
		mutex_lock(&qdev->ddev->struct_mutex);
		dev_err(qdev->dev, "%p %p %lu %lu force free\n",
			&bo->gem_base, bo, (unsigned long)bo->gem_base.size,
			*((unsigned long *)&bo->gem_base.refcount));
		spin_lock(&qdev->gem.lock);
		list_del_init(&bo->list);
		spin_unlock(&qdev->gem.lock);
		/* this should unref the ttm bo */
		drm_gem_object_unreference(&bo->gem_base);
		mutex_unlock(&qdev->ddev->struct_mutex);
	}
}

int virgl_bo_init(struct virgl_device *qdev)
{
	return virgl_ttm_init(qdev);
}

void virgl_bo_fini(struct virgl_device *qdev)
{
	virgl_ttm_fini(qdev);
}

int virgl_bo_get_sg_table(struct virgl_device *qdev,
		      struct virgl_bo *bo)
{
	struct sg_table *sg = NULL;
	int i;
	int ret;
	struct page **pages = bo->tbo.ttm->pages;
	int nr_pages = bo->tbo.num_pages;

	/* wtf swapping */
	if (bo->sgt)
		return 0;

	if (bo->tbo.ttm->state == tt_unpopulated)
		bo->tbo.ttm->bdev->driver->ttm_tt_populate(bo->tbo.ttm);
	bo->sgt = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!bo->sgt)
		goto out;

	ret = sg_alloc_table_from_pages(bo->sgt, pages, nr_pages, 0,
					nr_pages << PAGE_SHIFT, GFP_KERNEL);
	if (ret)
		goto out;
	return 0;
out:
	kfree(bo->sgt);
	bo->sgt = NULL;
	return -ENOMEM;
}

void virgl_bo_free_sg_table(struct virgl_bo *bo)
{
	sg_free_table(bo->sgt);
	kfree(bo->sgt);
	bo->sgt = NULL;
}
