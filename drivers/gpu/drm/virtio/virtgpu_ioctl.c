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
#include <drm/drmP.h>
#include "virtgpu_drv.h"
#include "ttm/ttm_execbuf_util.h"

#if 0
static void convert_to_hw_box(struct virtgpu_box *dst,
			      const struct drm_virtgpu_3d_box *src)
			      
{
	/* for now just memcpy this may change */
	memcpy(dst, src, sizeof(struct virtgpu_box));
}

static int virtgpu_alloc_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_alloc *virtgpu_alloc = data;
	int ret;
	struct virtgpu_bo *qobj;
	uint32_t handle;

	if (virtgpu_alloc->size == 0) {
		DRM_ERROR("invalid size %d\n", virtgpu_alloc->size);
		return -EINVAL;
	}
	ret = virtgpu_gem_object_create_with_handle(vgdev, file_priv,
						  0,
						  virtgpu_alloc->size,
						  &qobj, &handle);
	if (ret) {
		DRM_ERROR("%s: failed to create gem ret=%d\n",
			  __func__, ret);
		return -ENOMEM;
	}
	virtgpu_alloc->handle = handle;
	return 0;
}

int virtgpu_map_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_map *virtgpu_map = data;

	return virtgpu_mode_dumb_mmap(file_priv, vgdev->ddev, virtgpu_map->handle,
				  &virtgpu_map->offset);
}


/*
 * Usage of execbuffer:
 * Relocations need to take into account the full VIRTGPUDrawable size.
 * However, the command as passed from user space must *not* contain the initial
 * VIRTGPUReleaseInfo struct (first XXX bytes)
 */
int virtgpu_execbuffer_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_virtgpu_execbuffer *execbuffer = data;
	return virtgpu_execbuffer(dev, execbuffer, file_priv);
}


static int virtgpu_getparam_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	return -EINVAL;
}

static int virtgpu_resource_create_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_resource_create *rc = data;
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;
	int ret;
	uint32_t res_id;
	struct virtgpu_bo *qobj;
	uint32_t handle = 0;
	uint32_t size, pg_size;
	struct virtgpu_bo *pg_bo = NULL;
	void *optr;
	int si;
	struct scatterlist *sg;
	struct list_head validate_list;
	struct ttm_validate_buffer mainbuf, page_info_buf;
	struct virtgpu_fence *fence;
	struct ww_acquire_ctx ticket;

	INIT_LIST_HEAD(&validate_list);
	memset(&mainbuf, 0, sizeof(struct ttm_validate_buffer));
	memset(&page_info_buf, 0, sizeof(struct ttm_validate_buffer));

	ret = virtgpu_resource_id_get(vgdev, &res_id);
	if (ret) 
		return ret;

	size = rc->size;

	/* allocate a single page size object */
	if (size == 0)
		size = PAGE_SIZE;

	ret = virtgpu_gem_object_create_with_handle(vgdev, file_priv,
						  0,
						  size,
						  &qobj, &handle);
	if (ret)
		goto fail_id;

	/* use a gem reference since unref list undoes them */
	drm_gem_object_reference(&qobj->gem_base);
	mainbuf.bo = &qobj->tbo;
	list_add(&mainbuf.head, &validate_list);

	if (virtgpu_create_sg == 1) {
		ret = virtgpu_bo_get_sg_table(vgdev, qobj);
		if (ret)
			goto fail_obj;

		pg_size = sizeof(struct virtgpu_iov_entry) * qobj->sgt->nents;

		ret = virtgpu_bo_create(vgdev, pg_size, true, 0, &pg_bo);
		if (ret)
			goto fail_unref;

		drm_gem_object_reference(&pg_bo->gem_base);
		page_info_buf.bo = &pg_bo->tbo;
		list_add(&page_info_buf.head, &validate_list);
	}

	ret = virtgpu_bo_list_validate(&ticket, &validate_list);
	if (ret) {
		printk("failed to validate\n");
		goto fail_unref;
	}

	if (virtgpu_create_sg == 1) {
		ret = virtgpu_bo_kmap(pg_bo, &optr);
		for_each_sg(qobj->sgt->sgl, sg, qobj->sgt->nents, si) {
			struct virtgpu_iov_entry *iov = ((struct virtgpu_iov_entry *)optr) + si;
			iov->addr = sg_phys(sg);
			iov->length = sg->length;
			iov->pad = 0;
		}
		virtgpu_bo_kunmap(pg_bo);

		qobj->is_res_bound = true;
	}

	cmd_p = virtgpu_alloc_cmd(vgdev, pg_bo, false, NULL, 0, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));
	cmd_p->type = VIRTGPU_CMD_CREATE_RESOURCE;
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
	cmd_p->u.res_create.nr_sg_entries = qobj->sgt ? qobj->sgt->nents : 0;
	cmd_p->u.res_create.flags = rc->flags;

	ret = virtgpu_fence_emit(vgdev, cmd_p, &fence);

	virtgpu_queue_cmd_buf(vgdev, vbuf);

	ttm_eu_fence_buffer_objects(&ticket, &validate_list, fence);

	qobj->res_handle = res_id;
	qobj->stride = rc->stride;
	rc->res_handle = res_id; /* similiar to a VM address */
	rc->bo_handle = handle;

	virtgpu_unref_list(&validate_list);
	if (virtgpu_create_sg == 1)
		virtgpu_bo_unref(&pg_bo);

	return 0;
fail_unref:
	virtgpu_unref_list(&validate_list);
fail_pg_obj:
	if (virtgpu_create_sg == 1)
		if (pg_bo)
			virtgpu_bo_unref(&pg_bo);
fail_obj:
	drm_gem_object_handle_unreference_unlocked(&qobj->gem_base);
fail_id:
	virtgpu_resource_id_put(vgdev, res_id);
	return ret;
}

static int virtgpu_resource_info_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file_priv)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_resource_info *ri = data;
	struct drm_gem_object *gobj = NULL;
	struct virtgpu_bo *qobj = NULL;

	gobj = drm_gem_object_lookup(dev, file_priv, ri->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtgpu_bo(gobj);

	ri->size = qobj->gem_base.size;
	ri->res_handle = qobj->res_handle;
	ri->stride = qobj->stride;
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

int virtgpu_resource_unref(struct virtgpu_device *vgdev, uint32_t res_handle)
{
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;

	cmd_p = virtgpu_alloc_cmd(vgdev, NULL, false, NULL, 0, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));
	cmd_p->type = VIRTGPU_RESOURCE_UNREF;
	cmd_p->u.res_unref.res_handle = res_handle;
	
	virtgpu_queue_cmd_buf(vgdev, vbuf);

	virtgpu_resource_id_put(vgdev, res_handle);
	return 0;
}

static int virtgpu_transfer_get_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct virtgpu_fpriv *vfpriv = file->driver_priv;
	struct drm_virtgpu_3d_transfer_get *args = data;
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;
	struct drm_gem_object *gobj = NULL;
	struct virtgpu_bo *qobj = NULL;
	struct virtgpu_fence *fence;
	int ret;
	u32 offset = args->offset;

	gobj = drm_gem_object_lookup(dev, file, args->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtgpu_bo(gobj);

	ret = virtgpu_bo_reserve(qobj, false);
	if (ret)
		goto out;

	virtgpu_ttm_placement_from_domain(qobj, qobj->type);
	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
	if (unlikely(ret))
		goto out_unres;

	cmd_p = virtgpu_alloc_cmd(vgdev, qobj, true, &offset, 0, &vbuf);

	cmd_p->type = VIRTGPU_TRANSFER_GET;

	cmd_p->u.transfer_get.res_handle = qobj->res_handle;
	convert_to_hw_box(&cmd_p->u.transfer_get.box, &args->box);
	cmd_p->u.transfer_get.level = args->level;
	cmd_p->u.transfer_get.data = offset;
	cmd_p->u.transfer_get.ctx_id = vfpriv->ctx_id;
	cmd_p->u.transfer_get.stride = args->stride;
	cmd_p->u.transfer_get.layer_stride = args->layer_stride;
	ret = virtgpu_fence_emit(vgdev, cmd_p, &fence);

	virtgpu_queue_cmd_buf(vgdev, vbuf);

	qobj->tbo.sync_obj = vgdev->mman.bdev.driver->sync_obj_ref(fence);

out_unres:
	virtgpu_bo_unreserve(qobj);
 out:
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

static int virtgpu_transfer_put_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct virtgpu_fpriv *vfpriv = file->driver_priv;
	struct drm_virtgpu_3d_transfer_put *args = data;
	struct virtgpu_command *cmd_p;
	struct virtgpu_vbuffer *vbuf;
	struct drm_gem_object *gobj = NULL;
	struct virtgpu_bo *qobj = NULL;
	struct virtgpu_fence *fence;
	int ret;
	u32 offset = args->offset;
	u32 max_size = 0;

	gobj = drm_gem_object_lookup(dev, file, args->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtgpu_bo(gobj);

	ret = virtgpu_bo_reserve(qobj, false);
	if (ret)
		goto out;

	virtgpu_ttm_placement_from_domain(qobj, qobj->type);
	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
	if (unlikely(ret))
		goto out_unres;

	if (args->box.h == 1 && args->box.d == 1 &&
	    args->box.y == 0 && args->box.z == 0) {
		max_size = args->box.w;
	}
	cmd_p = virtgpu_alloc_cmd(vgdev, qobj, false, &offset, max_size, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));
	cmd_p->type = VIRTGPU_TRANSFER_PUT;
	cmd_p->u.transfer_put.res_handle = qobj->res_handle;
	convert_to_hw_box(&cmd_p->u.transfer_put.box, &args->box);
	cmd_p->u.transfer_put.level = args->level;
	cmd_p->u.transfer_put.stride = args->stride;
	cmd_p->u.transfer_put.layer_stride = args->layer_stride;
	cmd_p->u.transfer_put.data = offset;
	cmd_p->u.transfer_put.ctx_id = vfpriv->ctx_id;
	ret = virtgpu_fence_emit(vgdev, cmd_p, &fence);
	virtgpu_queue_cmd_buf(vgdev, vbuf);

	qobj->tbo.sync_obj = vgdev->mman.bdev.driver->sync_obj_ref(fence);

out_unres:
	virtgpu_bo_unreserve(qobj);
 out:
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

static int virtgpu_wait_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct drm_virtgpu_3d_wait *args = data;
	struct drm_gem_object *gobj = NULL;
	struct virtgpu_bo *qobj = NULL;
	int ret;
	bool nowait = false;

	gobj = drm_gem_object_lookup(dev, file, args->handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtgpu_bo(gobj);

	if (args->flags & VIRTGPU_WAIT_NOWAIT)
		nowait = true;
	ret = virtgpu_wait(qobj, nowait);

	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int virtgpu_get_caps_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file)
{
	struct virtgpu_device *vdev = dev->dev_private;
	struct drm_virtgpu_get_caps *args = data;	
	struct drm_gem_object *gobj = &vdev->caps_bo->gem_base;
	uint32_t handle;
	int r;

	r = drm_gem_handle_create(file, gobj, &handle);
	if (r)
		return r;

	args->handle = handle;
	return 0;
}

struct drm_ioctl_desc virtgpu_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VIRTGPU_ALLOC, virtgpu_alloc_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_MAP, virtgpu_map_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_EXECBUFFER, virtgpu_execbuffer_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIRTGPU_GETPARAM, virtgpu_getparam_ioctl,
							DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_RESOURCE_CREATE, virtgpu_resource_create_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_RESOURCE_INFO, virtgpu_resource_info_ioctl, DRM_AUTH|DRM_UNLOCKED),
	/* make transfer async to the main ring? - no sure, can we
	   thread these in the underlying GL */
	DRM_IOCTL_DEF_DRV(VIRTGPU_TRANSFER_GET, virtgpu_transfer_get_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIRTGPU_TRANSFER_PUT, virtgpu_transfer_put_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_WAIT, virtgpu_wait_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_GET_CAPS, virtgpu_get_caps_ioctl, DRM_AUTH|DRM_UNLOCKED),

};

int virtgpu_max_ioctls = DRM_ARRAY_SIZE(virtgpu_ioctls);
#endif
