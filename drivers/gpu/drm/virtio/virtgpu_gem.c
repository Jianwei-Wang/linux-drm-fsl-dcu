
#include <drm/drmP.h>
#include <linux/shmem_fs.h>
#include "virtgpu_drv.h"

static void *virtgpu_gem_object_alloc(struct drm_device *dev)
{
	return kzalloc(sizeof(struct virtgpu_object), GFP_KERNEL);
}

static void virtgpu_gem_object_free(struct virtgpu_object *obj)
{
	kfree(obj);
}

int virtgpu_gem_init_object(struct drm_gem_object *obj)
{
	/* we do nothings here */
	return 0;
}

void virtgpu_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct virtgpu_object *obj = gem_to_virtgpu_obj(gem_obj);

	/* put pages */
	drm_gem_object_release(&obj->gem_base);
	virtgpu_gem_object_free(obj);
}

int virtgpu_gem_init(struct virtgpu_device *qdev)
{
	return 0;
}

void virtgpu_gem_fini(struct virtgpu_device *qdev)
{
}

		      
struct virtgpu_object *virtgpu_alloc_object(struct drm_device *dev,
					    size_t size)
{
	struct virtgpu_object *obj;

	obj = virtgpu_gem_object_alloc(dev);
	if (obj == NULL)
		return NULL;

	if (drm_gem_object_init(dev, &obj->gem_base, size) != 0) {
		virtgpu_gem_object_free(obj);
		return NULL;
	}
	
	return obj;
}

int
virtgpu_gem_create(struct drm_file *file,
		   struct drm_device *dev,
		   uint64_t size,
		   struct drm_gem_object **obj_p,
		   uint32_t *handle_p)
{
	struct virtgpu_object *obj;
	int ret;
	u32 handle;

	obj = virtgpu_alloc_object(dev, size);
	if (obj == NULL)
		return -ENOMEM;

	ret = drm_gem_handle_create(file, &obj->gem_base, &handle);
	if (ret) {
		drm_gem_object_release(&obj->gem_base);
		virtgpu_gem_object_free(obj);
		return ret;
	}

	*obj_p = &obj->gem_base;

	*handle_p = handle;
	return 0;
}

int
virtgpu_gem_object_get_pages(struct virtgpu_object *obj)
{
	struct virtgpu_device *vgdev = obj->gem_base.dev->dev_private;
	int page_count, i;
	struct address_space *mapping;
	struct sg_table *st;
	struct scatterlist *sg;
	struct sg_page_iter sg_iter;
	struct page *page;
	unsigned long last_pfn = 0;	/* suppress gcc warning */
	gfp_t gfp;

	st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (st == NULL)
		return -ENOMEM;

	page_count = obj->gem_base.size / PAGE_SIZE;
	if (sg_alloc_table(st, page_count, GFP_KERNEL)) {
		sg_free_table(st);
		kfree(st);
		return -ENOMEM;
	}

	/* Get the list of pages out of our struct file.  They'll be pinned
	 * at this point until we release them.
	 *
	 * Fail silently without starting the shrinker
	 */
	mapping = file_inode(obj->gem_base.filp)->i_mapping;
	gfp = mapping_gfp_mask(mapping);
	gfp |= __GFP_NORETRY | __GFP_NOWARN | __GFP_NO_KSWAPD;
	gfp &= ~(__GFP_IO | __GFP_WAIT);
	sg = st->sgl;
	st->nents = 0;
	for (i = 0; i < page_count; i++) {
		page = shmem_read_mapping_page_gfp(mapping, i, gfp);
		if (IS_ERR(page)) {
		  //	i915_gem_purge(dev_priv, page_count);
			page = shmem_read_mapping_page_gfp(mapping, i, gfp);
		}
		if (IS_ERR(page)) {
			/* We've tried hard to allocate the memory by reaping
			 * our own buffer, now let the real VM do its job and
			 * go down in flames if truly OOM.
			 */
			gfp &= ~(__GFP_NORETRY | __GFP_NOWARN | __GFP_NO_KSWAPD);
			gfp |= __GFP_IO | __GFP_WAIT;

			//			i915_gem_shrink_all(dev_priv);
			page = shmem_read_mapping_page_gfp(mapping, i, gfp);
			if (IS_ERR(page))
				goto err_pages;

			gfp |= __GFP_NORETRY | __GFP_NOWARN | __GFP_NO_KSWAPD;
			gfp &= ~(__GFP_IO | __GFP_WAIT);
		}
#ifdef CONFIG_SWIOTLB
		if (swiotlb_nr_tbl()) {
			st->nents++;
			sg_set_page(sg, page, PAGE_SIZE, 0);
			sg = sg_next(sg);
			continue;
		}
#endif
		if (!i || page_to_pfn(page) != last_pfn + 1) {
			if (i)
				sg = sg_next(sg);
			st->nents++;
			sg_set_page(sg, page, PAGE_SIZE, 0);
		} else {
			sg->length += PAGE_SIZE;
		}
		last_pfn = page_to_pfn(page);
	}
#ifdef CONFIG_SWIOTLB
	if (!swiotlb_nr_tbl())
#endif
		sg_mark_end(sg);
	obj->pages = st;

	return 0;

err_pages:
	sg_mark_end(sg);
	for_each_sg_page(st->sgl, &sg_iter, st->nents, 0)
		page_cache_release(sg_page_iter_page(&sg_iter));
	sg_free_table(st);
	kfree(st);
	return PTR_ERR(page);
}

static int virtgpu_create_mmap_offset(struct virtgpu_object *obj)
{
	int ret;
	if (obj->gem_base.map_list.map)
		return 0;

	ret = drm_gem_create_mmap_offset(&obj->gem_base);
	if (ret)
		return ret;
	return 0;
}

static int virtgpu_free_mmap_offset(struct virtgpu_object *obj)
{
	if (!obj->gem_base.map_list.map)
		return;

	drm_gem_free_mmap_offset(&obj->gem_base);
}

int virtgpu_mode_dumb_create(struct drm_file *file_priv,
			     struct drm_device *dev,
			     struct drm_mode_create_dumb *args)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_gem_object *gobj;
	struct virtgpu_object *obj;
	uint32_t handle;
	int ret;
	uint32_t pitch;
	uint32_t resid;

	pitch = args->width * ((args->bpp + 1) / 8);
	args->size = pitch * args->height;
	args->size = ALIGN(args->size, PAGE_SIZE);

	ret = virtgpu_gem_create(file_priv, dev, args->size, &gobj,
				 &args->handle);
	if (ret)
		goto fail;

	ret = virtgpu_resource_id_get(vgdev, &resid);
	if (ret)
		goto fail;

	ret = virtgpu_cmd_create_resource(vgdev, resid,
					  2, args->width, args->height);
	if (ret)
		goto fail;

	/* attach the object to the resource */
	obj = gem_to_virtgpu_obj(gobj);
	ret = virtgpu_object_attach(vgdev, obj, resid);
	if (ret)
		goto fail;


	drm_gem_object_unreference(&obj->gem_base);
	return ret;
fail:
	return ret;
}

int virtgpu_mode_dumb_destroy(struct drm_file *file_priv,
			      struct drm_device *dev,
			      uint32_t handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}

int virtgpu_mode_dumb_mmap(struct drm_file *file_priv,
			   struct drm_device *dev,
			   uint32_t handle, uint64_t *offset_p)
{
	struct drm_gem_object *gobj;
	struct virtgpu_object *obj;
	int ret;
	BUG_ON(!offset_p);
	gobj = drm_gem_object_lookup(dev, file_priv, handle);
	if (gobj == NULL)
		return -ENOENT;
	obj = gem_to_virtgpu_obj(gobj);

	ret = virtgpu_create_mmap_offset(obj);
	if (ret)
		goto out;
	*offset_p = (u64)obj->gem_base.map_list.map << PAGE_SHIFT;

out:
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}
