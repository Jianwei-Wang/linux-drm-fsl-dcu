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

static void convert_to_hw_box(struct virgl_box *dst,
			      const struct drm_virgl_3d_box *src)
			      
{
	/* for now just memcpy this may change */
	memcpy(dst, src, sizeof(struct virgl_box));
}

static int virgl_alloc_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv)
{
	struct virgl_device *qdev = dev->dev_private;
	struct drm_virgl_alloc *virgl_alloc = data;
	int ret;
	struct virgl_bo *qobj;
	uint32_t handle;

	if (virgl_alloc->size == 0) {
		DRM_ERROR("invalid size %d\n", virgl_alloc->size);
		return -EINVAL;
	}
	ret = virgl_gem_object_create_with_handle(qdev, file_priv,
						  0,
						  virgl_alloc->size,
						  &qobj, &handle);
	if (ret) {
		DRM_ERROR("%s: failed to create gem ret=%d\n",
			  __func__, ret);
		return -ENOMEM;
	}
	virgl_alloc->handle = handle;
	return 0;
}

int virgl_map_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct virgl_device *qdev = dev->dev_private;
	struct drm_virgl_map *virgl_map = data;

	return virgl_mode_dumb_mmap(file_priv, qdev->ddev, virgl_map->handle,
				  &virgl_map->offset);
}


/*
 * Usage of execbuffer:
 * Relocations need to take into account the full VIRGLDrawable size.
 * However, the command as passed from user space must *not* contain the initial
 * VIRGLReleaseInfo struct (first XXX bytes)
 */
int virgl_execbuffer_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_virgl_execbuffer *execbuffer = data;
	return virgl_execbuffer(dev, execbuffer, file_priv);
}


static int virgl_getparam_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	return -EINVAL;
}

static int virgl_resource_create_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct virgl_device *qdev = dev->dev_private;
	struct drm_virgl_3d_resource_create *rc = data;
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;
	int ret;
	uint32_t res_id;

	ret = virgl_resource_id_get(qdev, &res_id);
	if (ret)
		return ret;

	cmd_p = virgl_alloc_cmd(qdev, NULL, false, NULL, 0, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));
	cmd_p->type = VIRGL_CMD_CREATE_RESOURCE;
	cmd_p->u.res_create.handle = res_id;
	cmd_p->u.res_create.target = rc->target;
	cmd_p->u.res_create.format = rc->format;
	cmd_p->u.res_create.bind = rc->bind;
	cmd_p->u.res_create.width = rc->width;
	cmd_p->u.res_create.height = rc->height;
	cmd_p->u.res_create.depth = rc->depth;
	cmd_p->u.res_create.array_size = rc->array_size;
	cmd_p->u.res_create.last_level = rc->last_level;
	cmd_p->u.res_create.nr_samples = rc->nr_samples;

	virgl_queue_cmd_buf(qdev, vbuf);

	rc->res_handle = res_id;
	return 0;
}

int virgl_resource_unref(struct virgl_device *qdev, uint32_t res_handle)
{
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;

	cmd_p = virgl_alloc_cmd(qdev, NULL, false, NULL, 0, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));
	cmd_p->type = VIRGL_RESOURCE_UNREF;
	cmd_p->u.res_unref.res_handle = res_handle;
	
	virgl_queue_cmd_buf(qdev, vbuf);

	virgl_resource_id_put(qdev, res_handle);
}

static int virgl_resource_unref_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *file_priv)
{
	struct virgl_device *qdev = dev->dev_private;
	struct drm_virgl_3d_resource_unref *ru = data;

	virgl_resource_unref(qdev, ru->res_handle);
	return 0;
}
	
static int virgl_transfer_get_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file)
{
	struct virgl_device *qdev = dev->dev_private;
	struct drm_virgl_3d_transfer_get *args = data;
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;
	struct drm_gem_object *gobj = NULL;
	struct virgl_bo *qobj = NULL;
	struct virgl_fence *fence;
	int ret;
	u32 offset = args->dst_offset;

	gobj = drm_gem_object_lookup(dev, file, args->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virgl_bo(gobj);

	ret = virgl_bo_reserve(qobj, false);
	if (ret)
		goto out;

	virgl_ttm_placement_from_domain(qobj, qobj->type);
	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
	if (unlikely(ret))
		goto out_unres;

	cmd_p = virgl_alloc_cmd(qdev, qobj, true, &offset, 0, &vbuf);

	cmd_p->type = VIRGL_TRANSFER_GET;

	cmd_p->u.transfer_get.res_handle = args->res_handle;
	convert_to_hw_box(&cmd_p->u.transfer_get.box, &args->box);
	cmd_p->u.transfer_get.level = args->level;
	cmd_p->u.transfer_get.data = offset;
	cmd_p->u.transfer_get.transfer_flags = args->transfer_flags;

	ret = virgl_fence_emit(qdev, cmd_p, &fence);

	virgl_queue_cmd_buf(qdev, vbuf);

	qobj->tbo.sync_obj = qdev->mman.bdev.driver->sync_obj_ref(fence);

out_unres:
	virgl_bo_unreserve(qobj);
 out:
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

static int virgl_transfer_put_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file)
{
	struct virgl_device *qdev = dev->dev_private;
	struct drm_virgl_3d_transfer_put *args = data;
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;
	struct drm_gem_object *gobj = NULL;
	struct virgl_bo *qobj = NULL;
	struct virgl_fence *fence;
	int ret;
	u32 offset = args->src_offset;
	u32 max_size = 0;

	gobj = drm_gem_object_lookup(dev, file, args->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virgl_bo(gobj);

	ret = virgl_bo_reserve(qobj, false);
	if (ret)
		goto out;

	virgl_ttm_placement_from_domain(qobj, qobj->type);
	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
	if (unlikely(ret))
		goto out_unres;

	if (args->dst_box.h == 1 && args->dst_box.d == 1 &&
	    args->dst_box.y == 0 && args->dst_box.z == 0) {
		max_size = args->dst_box.w;
	}
	cmd_p = virgl_alloc_cmd(qdev, qobj, false, &offset, max_size, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));
	cmd_p->type = VIRGL_TRANSFER_PUT;
	cmd_p->u.transfer_put.res_handle = args->res_handle;
	convert_to_hw_box(&cmd_p->u.transfer_put.dst_box, &args->dst_box);
	cmd_p->u.transfer_put.dst_level = args->dst_level;
	cmd_p->u.transfer_put.src_stride = args->src_stride;
	cmd_p->u.transfer_put.data = offset;
	cmd_p->u.transfer_put.transfer_flags = args->transfer_flags;

	ret = virgl_fence_emit(qdev, cmd_p, &fence);
	virgl_queue_cmd_buf(qdev, vbuf);

	qobj->tbo.sync_obj = qdev->mman.bdev.driver->sync_obj_ref(fence);

out_unres:
	virgl_bo_unreserve(qobj);
 out:
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

static int virgl_wait_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file)
{
	struct drm_virgl_3d_wait *args = data;
	struct drm_gem_object *gobj = NULL;
	struct virgl_bo *qobj = NULL;
	int ret;
	bool nowait = false;

	gobj = drm_gem_object_lookup(dev, file, args->handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virgl_bo(gobj);

	if (args->flags & VIRGL_WAIT_NOWAIT)
		nowait = true;
	ret = virgl_wait(qobj, nowait);

	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

struct drm_ioctl_desc virgl_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VIRGL_ALLOC, virgl_alloc_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_MAP, virgl_map_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_EXECBUFFER, virgl_execbuffer_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIRGL_GETPARAM, virgl_getparam_ioctl,
							DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_RESOURCE_CREATE, virgl_resource_create_ioctl, DRM_AUTH|DRM_UNLOCKED),

	/* make transfer async to the main ring? - no sure, can we
	   thread these in the underlying GL */
	DRM_IOCTL_DEF_DRV(VIRGL_TRANSFER_GET, virgl_transfer_get_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIRGL_TRANSFER_PUT, virgl_transfer_put_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_RESOURCE_UNREF, virgl_resource_unref_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_WAIT, virgl_wait_ioctl, DRM_AUTH|DRM_UNLOCKED),

};

int virgl_max_ioctls = DRM_ARRAY_SIZE(virgl_ioctls);
