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
#include "ttm/ttm_execbuf_util.h"

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
	struct drm_virgl_resource_create *rc = data;
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;
	int ret;
	uint32_t res_id;
	struct virgl_bo *qobj;
	uint32_t handle = 0;
	uint32_t size, pg_size;
	struct virgl_bo *pg_bo = NULL;
	void *optr;
	int si;
	struct scatterlist *sg;
	struct list_head validate_list;
	struct ttm_validate_buffer mainbuf, page_info_buf;
	struct virgl_fence *fence;
	struct ww_acquire_ctx ticket;

	INIT_LIST_HEAD(&validate_list);
	memset(&mainbuf, 0, sizeof(struct ttm_validate_buffer));
	memset(&page_info_buf, 0, sizeof(struct ttm_validate_buffer));

	ret = virgl_resource_id_get(qdev, &res_id);
	if (ret) 
		return ret;

	size = rc->size;

	/* allocate a single page size object */
	if (size == 0)
		size = PAGE_SIZE;

	ret = virgl_gem_object_create_with_handle(qdev, file_priv,
						  0,
						  size,
						  &qobj, &handle);
	if (ret)
		goto fail_id;

	/* use a gem reference since unref list undoes them */
	drm_gem_object_reference(&qobj->gem_base);
	mainbuf.bo = &qobj->tbo;
	list_add(&mainbuf.head, &validate_list);

	if (virgl_create_sg == 1) {
		ret = virgl_bo_get_sg_table(qdev, qobj);
		if (ret)
			goto fail_obj;

		pg_size = sizeof(struct virgl_iov_entry) * qobj->sgt->nents;

		ret = virgl_bo_create(qdev, pg_size, true, 0, &pg_bo);
		if (ret)
			goto fail_unref;

		drm_gem_object_reference(&pg_bo->gem_base);
		page_info_buf.bo = &pg_bo->tbo;
		list_add(&page_info_buf.head, &validate_list);
	}

	ret = virgl_bo_list_validate(&ticket, &validate_list);
	if (ret) {
		printk("failed to validate\n");
		goto fail_unref;
	}

	if (virgl_create_sg == 1) {
		ret = virgl_bo_kmap(pg_bo, &optr);
		for_each_sg(qobj->sgt->sgl, sg, qobj->sgt->nents, si) {
			struct virgl_iov_entry *iov = ((struct virgl_iov_entry *)optr) + si;
			iov->addr = sg_phys(sg);
			iov->length = sg->length;
			iov->pad = 0;
		}
		virgl_bo_kunmap(pg_bo);

		qobj->is_res_bound = true;
	}

	cmd_p = virgl_alloc_cmd(qdev, pg_bo, false, NULL, 0, &vbuf);
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
	cmd_p->u.res_create.nr_sg_entries = qobj->sgt ? qobj->sgt->nents : 0;

	ret = virgl_fence_emit(qdev, cmd_p, &fence);

	virgl_queue_cmd_buf(qdev, vbuf);

	ttm_eu_fence_buffer_objects(&ticket, &validate_list, fence);

	qobj->res_handle = res_id;
	qobj->stride = rc->stride;
	rc->res_handle = res_id; /* similiar to a VM address */
	rc->bo_handle = handle;

	virgl_unref_list(&validate_list);
	if (virgl_create_sg == 1)
		virgl_bo_unref(&pg_bo);

	return 0;
fail_unref:
	virgl_unref_list(&validate_list);
fail_pg_obj:
	if (virgl_create_sg == 1)
		if (pg_bo)
			virgl_bo_unref(&pg_bo);
fail_obj:
	drm_gem_object_handle_unreference_unlocked(&qobj->gem_base);
fail_id:
	virgl_resource_id_put(qdev, res_id);
	return ret;
}

static int virgl_resource_info_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file_priv)
{
	struct virgl_device *qdev = dev->dev_private;
	struct drm_virgl_resource_info *ri = data;
	struct drm_gem_object *gobj = NULL;
	struct virgl_bo *qobj = NULL;

	gobj = drm_gem_object_lookup(dev, file_priv, ri->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virgl_bo(gobj);

	ri->size = qobj->gem_base.size;
	ri->res_handle = qobj->res_handle;
	ri->stride = qobj->stride;
	drm_gem_object_unreference_unlocked(gobj);
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
	return 0;
}

static int virgl_transfer_get_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file)
{
	struct virgl_device *qdev = dev->dev_private;
	struct virgl_fpriv *vfpriv = file->driver_priv;
	struct drm_virgl_3d_transfer_get *args = data;
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;
	struct drm_gem_object *gobj = NULL;
	struct virgl_bo *qobj = NULL;
	struct virgl_fence *fence;
	int ret;
	u32 offset = args->offset;

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

	cmd_p->u.transfer_get.res_handle = qobj->res_handle;
	convert_to_hw_box(&cmd_p->u.transfer_get.box, &args->box);
	cmd_p->u.transfer_get.level = args->level;
	cmd_p->u.transfer_get.data = offset;
	cmd_p->u.transfer_get.ctx_id = vfpriv->ctx_id;
	cmd_p->u.transfer_get.stride = args->stride;
	cmd_p->u.transfer_get.layer_stride = args->layer_stride;
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
	struct virgl_fpriv *vfpriv = file->driver_priv;
	struct drm_virgl_3d_transfer_put *args = data;
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;
	struct drm_gem_object *gobj = NULL;
	struct virgl_bo *qobj = NULL;
	struct virgl_fence *fence;
	int ret;
	u32 offset = args->offset;
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

	if (args->box.h == 1 && args->box.d == 1 &&
	    args->box.y == 0 && args->box.z == 0) {
		max_size = args->box.w;
	}
	cmd_p = virgl_alloc_cmd(qdev, qobj, false, &offset, max_size, &vbuf);
	memset(cmd_p, 0, sizeof(*cmd_p));
	cmd_p->type = VIRGL_TRANSFER_PUT;
	cmd_p->u.transfer_put.res_handle = qobj->res_handle;
	convert_to_hw_box(&cmd_p->u.transfer_put.box, &args->box);
	cmd_p->u.transfer_put.level = args->level;
	cmd_p->u.transfer_put.stride = args->stride;
	cmd_p->u.transfer_put.layer_stride = args->layer_stride;
	cmd_p->u.transfer_put.data = offset;
	cmd_p->u.transfer_put.ctx_id = vfpriv->ctx_id;
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

static int virgl_get_caps_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file)
{
	struct virgl_device *vdev = dev->dev_private;
	struct drm_virgl_get_caps *args = data;	
	struct drm_gem_object *gobj = &vdev->caps_bo->gem_base;
	uint32_t handle;
	int r;

	r = drm_gem_handle_create(file, gobj, &handle);
	if (r)
		return r;

	args->handle = handle;
	return 0;
}

struct drm_ioctl_desc virgl_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VIRGL_ALLOC, virgl_alloc_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_MAP, virgl_map_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_EXECBUFFER, virgl_execbuffer_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIRGL_GETPARAM, virgl_getparam_ioctl,
							DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_RESOURCE_CREATE, virgl_resource_create_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_RESOURCE_INFO, virgl_resource_info_ioctl, DRM_AUTH|DRM_UNLOCKED),
	/* make transfer async to the main ring? - no sure, can we
	   thread these in the underlying GL */
	DRM_IOCTL_DEF_DRV(VIRGL_TRANSFER_GET, virgl_transfer_get_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIRGL_TRANSFER_PUT, virgl_transfer_put_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_WAIT, virgl_wait_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRGL_GET_CAPS, virgl_get_caps_ioctl, DRM_AUTH|DRM_UNLOCKED),

};

int virgl_max_ioctls = DRM_ARRAY_SIZE(virgl_ioctls);
