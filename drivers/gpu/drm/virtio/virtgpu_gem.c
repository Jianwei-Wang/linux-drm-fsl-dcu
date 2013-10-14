
#include <drm/drmP.h>
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
