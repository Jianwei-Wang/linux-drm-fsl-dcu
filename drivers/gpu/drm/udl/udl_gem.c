#include "drmP.h"
#include "udl_drv.h"

struct udl_gem_object *udl_gem_alloc_object(struct drm_device *dev,
					    size_t size)
{
	struct udl_gem_object *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (obj == NULL)
		return NULL;

	if (drm_gem_object_init(dev, &obj->base, size) != 0) {
		kfree(obj);
		return NULL;
	}

	return obj;
}

static int
udl_gem_create(struct drm_file *file,
	       struct drm_device *dev,
	       uint64_t size,
	       uint32_t *handle_p)
{
	struct udl_gem_object *obj;
	int ret;
	u32 handle;

	size = roundup(size, PAGE_SIZE);

	obj = udl_gem_alloc_object(dev, size);
	if (obj == NULL)
		return -ENOMEM;

	ret = drm_gem_handle_create(file, &obj->base, &handle);
	if (ret) {
		drm_gem_object_release(&obj->base);
		kfree(obj);
		return ret;
	}

	drm_gem_object_unreference(&obj->base);
	*handle_p = handle;
	return 0;
}

int udl_dumb_create(struct drm_file *file,
		    struct drm_device *dev,
		    struct drm_mode_create_dumb *args)
{
	args->pitch = args->width * ((args->bpp + 1) / 8);
	args->size = args->pitch * args->height;
	return udl_gem_create(file, dev,
			      args->size, &args->handle);
}

int udl_dumb_destroy(struct drm_file *file, struct drm_device *dev,
		     uint32_t handle)
{
	return drm_gem_handle_delete(file, handle);
}

static int
udl_gem_create_mmap_offset(struct udl_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_gem_mm *mm = dev->mm_private;
	struct drm_map_list *list;
	struct drm_local_map *map;
	int ret;

	list = &obj->base.map_list;
	list->map = kzalloc(sizeof(struct drm_map_list), GFP_KERNEL);
	if (!list->map)
		return -ENOMEM;

	map = list->map;
	map->type = _DRM_GEM;
	map->size = obj->base.size;
	map->handle = obj;


	list->file_offset_node = drm_mm_search_free(&mm->offset_manager,
						    obj->base.size / PAGE_SIZE,
						    0, 0);
	if (!list->file_offset_node) {
		ret = -ENOSPC;
		goto out_free_list;
	}
	list->file_offset_node = drm_mm_get_block(list->file_offset_node,
						  obj->base.size / PAGE_SIZE,
						  0);
	if (!list->file_offset_node) {
		ret = -ENOMEM;
		goto out_free_list;
	}

	list->hash.key = list->file_offset_node->start;

	ret = drm_ht_insert_item(&mm->offset_hash, &list->hash);
	if (ret) {
		DRM_ERROR("Failed to add map hash\n");
		goto out_free_mm;
	}
	return 0;

out_free_mm:
	drm_mm_put_block(list->file_offset_node);
out_free_list:
	kfree(list->map);
	list->map = NULL;
	return ret;
}

int udl_mmap_gtt(struct drm_file *file, struct drm_device *dev,
		 uint32_t handle, uint64_t *offset)

{
	struct udl_gem_object *obj;
	int ret;

	mutex_lock(&dev->struct_mutex);

	obj = to_udl_bo(drm_gem_object_lookup(dev, file, handle));
	if (obj == NULL) {
		ret = -ENOENT;
		goto unlock;

	}

	if (!obj->base.map_list.map) {
		ret = udl_gem_create_mmap_offset(obj);
		if (ret)
			goto out;
	}

	*offset = (u64)obj->base.map_list.hash.key << PAGE_SHIFT;
out:
	drm_gem_object_unreference(&obj->base);
unlock:
	mutex_unlock(&dev->struct_mutex);
	return ret;
}

int udl_gem_init_object(struct drm_gem_object *obj)
{
	BUG();

	return 0;
}

void udl_gem_free_object(struct drm_gem_object *gem_obj)
{
}

