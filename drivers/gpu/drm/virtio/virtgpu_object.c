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

int virtgpu_object_create(struct virtgpu_device *qdev,
		  unsigned long size, bool kernel, u32 domain,
		  struct virtgpu_object **bo_ptr)
{
	struct virtgpu_object *bo;
	enum ttm_bo_type type;
	size_t acc_size;
	int r;

	if (unlikely(qdev->mman.bdev.dev_mapping == NULL))
		qdev->mman.bdev.dev_mapping = qdev->ddev->dev_mapping;
	if (kernel)
		type = ttm_bo_type_kernel;
	else
		type = ttm_bo_type_device;
	*bo_ptr = NULL;

	acc_size = ttm_bo_dma_acc_size(&qdev->mman.bdev, size,
				       sizeof(struct virtgpu_object));

	bo = kzalloc(sizeof(struct virtgpu_object), GFP_KERNEL);
	if (bo == NULL)
		return -ENOMEM;
	size = roundup(size, PAGE_SIZE);
	r = drm_gem_object_init(qdev->ddev, &bo->gem_base, size);
	if (unlikely(r)) {
		kfree(bo);
		return r;
	}
	bo->gem_base.driver_private = NULL;
	bo->dumb = false;

	virtgpu_ttm_placement_from_domain(bo, domain);

	r = ttm_bo_init(&qdev->mman.bdev, &bo->tbo, size, type,
			&bo->placement, 0, !kernel, NULL, acc_size,
			NULL, &virtgpu_ttm_bo_destroy);
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

int virtgpu_object_kmap(struct virtgpu_object *bo, void **ptr)
{
	bool is_iomem;
	int r;

	if (bo->vmap) {
		if (ptr)
			*ptr = bo->vmap;
		return 0;
	}
	r = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages, &bo->kmap);
	if (r)
		return r;
	bo->vmap = ttm_kmap_obj_virtual(&bo->kmap, &is_iomem);
	if (ptr)
		*ptr = bo->vmap;
	return 0;
}

void virtgpu_object_kunmap(struct virtgpu_object *bo)
{
	if (bo->vmap == NULL)
		return;
	bo->vmap = NULL;
	ttm_bo_kunmap(&bo->kmap);
}

#if 0
void virtgpu_object_force_delete(struct virtgpu_device *qdev)
{
	struct virtgpu_object *bo, *n;


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
#endif
