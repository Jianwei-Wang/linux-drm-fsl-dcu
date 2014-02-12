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
#include <drm/virtgpu_drm.h>
#include "ttm/ttm_execbuf_util.h"

static void convert_to_hw_box(struct virtgpu_box *dst,
			      const struct drm_virtgpu_3d_box *src)
			      
{
	/* for now just memcpy this may change */
	memcpy(dst, src, sizeof(struct virtgpu_box));
}

int virtgpu_map_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_map *virtgpu_map = data;

	return virtgpu_mode_dumb_mmap(file_priv, vgdev->ddev, virtgpu_map->handle,
				  &virtgpu_map->offset);
}

int virtgpu_object_list_validate(struct ww_acquire_ctx *ticket,
			   struct list_head *head)
{
	struct ttm_validate_buffer *buf;
	struct ttm_buffer_object *bo;
	struct virtgpu_object *qobj;
	int ret;
	
	ret = ttm_eu_reserve_buffers(ticket, head);
	if (ret != 0)
		return ret;

	list_for_each_entry(buf, head, head) {
		bo = buf->bo;
		qobj = container_of(bo, struct virtgpu_object, tbo);
//		virtgpu_ttm_placement_from_domain(qobj, qobj->type);
		ret = ttm_bo_validate(bo, &qobj->placement, false, false);
		if (ret)
			return ret;
	}
	return 0;
}

void virtgpu_unref_list(struct list_head *head)
{
	struct ttm_validate_buffer *buf;
	struct ttm_buffer_object *bo;
	struct virtgpu_object *qobj;
	list_for_each_entry(buf, head, head) {
		bo = buf->bo;
		qobj = container_of(bo, struct virtgpu_object, tbo);

		drm_gem_object_unreference_unlocked(&qobj->gem_base);
	}
}
	  
int virtgpu_execbuffer(struct drm_device *dev,
		     struct drm_virtgpu_execbuffer *execbuffer,
		     struct drm_file *drm_file)
{
	struct virtgpu_device *vgdev = dev->dev_private;	
	struct virtgpu_fpriv *vfpriv = drm_file->driver_priv;
	struct drm_gem_object *gobj;
	struct virtgpu_fence *fence;
	struct virtgpu_object *qobj;
	int ret;
	uint32_t *bo_handles = NULL;
	struct list_head validate_list;
	struct ttm_validate_buffer *buflist = NULL;
	struct ttm_validate_buffer cmdbuffer;
	int i;
	struct ww_acquire_ctx ticket;
	void *cmdbuf;
	//printk("user cmd size %d\n", user_cmd.command_size);

	if (vgdev->has_virgl_3d == false)
		return -ENOSYS;

	memset(&cmdbuffer, 0, sizeof(struct ttm_validate_buffer));
	INIT_LIST_HEAD(&validate_list);
	if (execbuffer->num_bo_handles) {

		bo_handles = drm_malloc_ab(execbuffer->num_bo_handles, sizeof(uint32_t));
		buflist = drm_calloc_large(execbuffer->num_bo_handles, sizeof(struct ttm_validate_buffer));
		if (!bo_handles || !buflist) {
			drm_free_large(bo_handles);
			drm_free_large(buflist);
			return -ENOMEM;
		}

		if (copy_from_user(bo_handles, (void __user *)(uintptr_t)execbuffer->bo_handles, execbuffer->num_bo_handles * sizeof(uint32_t))) {
			ret = -EFAULT;
			drm_free_large(bo_handles);
			return ret;
		}

		for (i = 0; i < execbuffer->num_bo_handles; i++) {
			gobj = drm_gem_object_lookup(dev,
						     drm_file, bo_handles[i]);
			if (!gobj) {
				drm_free_large(bo_handles);
				drm_free_large(buflist);
				return -ENOENT;
			}

			qobj = gem_to_virtgpu_obj(gobj);
			buflist[i].bo = &qobj->tbo;

			list_add(&buflist[i].head, &validate_list);
		}
		drm_free_large(bo_handles);
	}

	cmdbuf = drm_malloc_ab(1, execbuffer->size);
	if (!cmdbuf) {
		ret = -ENOMEM;
		goto out_free;
	}

	ret = virtgpu_object_list_validate(&ticket, &validate_list);
	if (ret)
		goto out_free_cmd;

	if (DRM_COPY_FROM_USER(cmdbuf, (void *)(unsigned long)execbuffer->command,
			       execbuffer->size)) {
		ret = -EFAULT;
		goto out_unresv;
	}

	virtgpu_cmd_submit(vgdev, cmdbuf, execbuffer->size,
			   vfpriv->ctx_id, &fence);

	ttm_eu_fence_buffer_objects(&ticket, &validate_list, fence);

	/* fence the command bo */
	virtgpu_unref_list(&validate_list);
	drm_free_large(buflist);
	return 0;
out_unresv:
	ttm_eu_backoff_reservation(&ticket, &validate_list);
out_free_cmd:
	drm_free_large(cmdbuf);
out_free:
	virtgpu_unref_list(&validate_list);
	drm_free_large(buflist);
	return ret;
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
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_getparam *param = data;
	int value;

	switch (param->param) {
 	case VIRTGPU_PARAM_3D_FEATURES:
		value = vgdev->has_virgl_3d == true ? 1 :0;
		break;
	default:
		return -EINVAL;
	}
	if (DRM_COPY_TO_USER((void *)(unsigned long)param->value, &value, sizeof(int))) {
		return -EFAULT;
	}
	return 0;
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
	struct virtgpu_object *qobj;
	struct drm_gem_object *obj;
	uint32_t handle = 0;
	uint32_t size;
	void *optr;
	int si;
	struct scatterlist *sg;
	struct list_head validate_list;
	struct ttm_validate_buffer mainbuf;
	struct virtgpu_fence *fence = NULL;
	struct ww_acquire_ctx ticket;
	struct virtgpu_resource_create_3d rc_3d;

	if (vgdev->has_virgl_3d == false) {
		if (rc->depth > 1)
			return -EINVAL;
		if (rc->nr_samples > 1)
			return -EINVAL;
		if (rc->last_level > 1)
			return -EINVAL;
		if (rc->target != 2)
			return -EINVAL;
		if (rc->array_size > 1)
			return -EINVAL;
	}

	INIT_LIST_HEAD(&validate_list);
	memset(&mainbuf, 0, sizeof(struct ttm_validate_buffer));

	ret = virtgpu_resource_id_get(vgdev, &res_id);
	if (ret) 
		return ret;

	size = rc->size;

	/* allocate a single page size object */
	if (size == 0)
		size = PAGE_SIZE;

	ret = virtgpu_gem_create(file_priv, dev,
				 size,
				 &obj, &handle);
	if (ret)
		goto fail_id;

	qobj = gem_to_virtgpu_obj(obj);

	if (!vgdev->has_virgl_3d) {
		ret = virtgpu_cmd_create_resource(vgdev, res_id,
						  rc->format, rc->width, rc->height);

		ret = virtgpu_object_attach(vgdev, qobj, res_id, NULL);
	} else {
		/* use a gem reference since unref list undoes them */
		drm_gem_object_reference(&qobj->gem_base);
		mainbuf.bo = &qobj->tbo;
		list_add(&mainbuf.head, &validate_list);

		ret = virtgpu_object_list_validate(&ticket, &validate_list);
		if (ret) {
			printk("failed to validate\n");
			goto fail_unref;
		}

		rc_3d.resource_id = res_id;
		rc_3d.target = rc->target;
		rc_3d.format = rc->format;
		rc_3d.bind = rc->bind;
		rc_3d.width = rc->width;
		rc_3d.height = rc->height;
		rc_3d.depth = rc->depth;
		rc_3d.array_size = rc->array_size;
		rc_3d.last_level = rc->last_level;
		rc_3d.nr_samples = rc->nr_samples;
		rc_3d.flags = rc->flags;

		ret = virtgpu_cmd_resource_create_3d(vgdev, &rc_3d, NULL);
		if (ret)
			goto fail_unref;

		ret = virtgpu_object_attach(vgdev, qobj, res_id, &fence);
		if (ret)
			goto fail_unref;
		ttm_eu_fence_buffer_objects(&ticket, &validate_list, fence);
	}

	qobj->hw_res_handle = res_id;
//	qobj->stride = rc->stride;
	rc->res_handle = res_id; /* similiar to a VM address */
	rc->bo_handle = handle;

	if (vgdev->has_virgl_3d)
		virtgpu_unref_list(&validate_list);


	return 0;
fail_unref:
	if (vgdev->has_virgl_3d)
		virtgpu_unref_list(&validate_list);
fail_obj:
//	drm_gem_object_handle_unreference_unlocked(obj);
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
	struct virtgpu_object *qobj = NULL;

	gobj = drm_gem_object_lookup(dev, file_priv, ri->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtgpu_obj(gobj);

	ri->size = qobj->gem_base.size;
	ri->res_handle = qobj->hw_res_handle;
//	ri->stride = qobj->stride;
	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

static int virtgpu_transfer_from_host_ioctl(struct drm_device *dev, void *data,
					    struct drm_file *file)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct virtgpu_fpriv *vfpriv = file->driver_priv;
	struct drm_virtgpu_3d_transfer_from_host *args = data;
	struct drm_gem_object *gobj = NULL;
	struct virtgpu_object *qobj = NULL;
	struct virtgpu_fence *fence;
	int ret;
	u32 offset = args->offset;
	struct virtgpu_box box;

	if (vgdev->has_virgl_3d == false)
		return -ENOSYS;
		
	gobj = drm_gem_object_lookup(dev, file, args->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtgpu_obj(gobj);

	ret = virtgpu_object_reserve(qobj, false);
	if (ret)
		goto out;

	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
	if (unlikely(ret))
		goto out_unres;

	convert_to_hw_box(&box, &args->box);
	ret = virtgpu_cmd_transfer_from_host_3d(vgdev, qobj->hw_res_handle,
						vfpriv->ctx_id, offset,
						args->level, &box, vgdev->has_fence ? &fence : NULL);

	if (!ret && vgdev->has_fence)
		qobj->tbo.sync_obj = vgdev->mman.bdev.driver->sync_obj_ref(fence);
	
out_unres:
	virtgpu_object_unreserve(qobj);
 out:
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int virtgpu_transfer_to_host_ioctl(struct drm_device *dev, void *data,
					  struct drm_file *file)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct virtgpu_fpriv *vfpriv = file->driver_priv;
	struct drm_virtgpu_3d_transfer_to_host *args = data;
	struct drm_gem_object *gobj = NULL;
	struct virtgpu_object *qobj = NULL;
	struct virtgpu_fence *fence;
	struct virtgpu_box box;
	int ret;
	u32 offset = args->offset;

	gobj = drm_gem_object_lookup(dev, file, args->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtgpu_obj(gobj);

	ret = virtgpu_object_reserve(qobj, false);
	if (ret)
		goto out;

	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
	if (unlikely(ret))
		goto out_unres;

	convert_to_hw_box(&box, &args->box);
	if (!vgdev->has_virgl_3d) {
		ret = virtgpu_cmd_transfer_to_host_2d(vgdev, qobj->hw_res_handle,
						      offset,
						      box.w, box.h,
						      box.x, box.y);
	} else {
		ret = virtgpu_cmd_transfer_to_host_3d(vgdev, qobj->hw_res_handle,
						      vfpriv ? vfpriv->ctx_id : 0, offset,
						      args->level, &box, &fence);
		if (!ret)
			qobj->tbo.sync_obj = vgdev->mman.bdev.driver->sync_obj_ref(fence);
	}

out_unres:
	virtgpu_object_unreserve(qobj);
 out:
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int virtgpu_wait_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_3d_wait *args = data;
	struct drm_gem_object *gobj = NULL;
	struct virtgpu_object *qobj = NULL;
	int ret;
	bool nowait = false;

	if (!vgdev->has_fence)
		return 0;

	gobj = drm_gem_object_lookup(dev, file, args->handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtgpu_obj(gobj);

	if (args->flags & VIRTGPU_WAIT_NOWAIT)
		nowait = true;
	ret = virtgpu_object_wait(qobj, nowait);

	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int virtgpu_get_caps_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_get_caps *args = data;	
	int size;

	if (!vgdev->has_virgl_3d)
		return -ENOSYS;

	size = sizeof(union virtgpu_caps);
	if (args->size < sizeof(union virtgpu_caps))
		size = args->size;

	if (DRM_COPY_TO_USER((void *)(unsigned long)args->addr, &vgdev->caps, size))
		return -EFAULT;

	return 0;
}


struct drm_ioctl_desc virtgpu_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VIRTGPU_MAP, virtgpu_map_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_EXECBUFFER, virtgpu_execbuffer_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIRTGPU_GETPARAM, virtgpu_getparam_ioctl,
							DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_RESOURCE_CREATE, virtgpu_resource_create_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_RESOURCE_INFO, virtgpu_resource_info_ioctl, DRM_AUTH|DRM_UNLOCKED),
	/* make transfer async to the main ring? - no sure, can we
	   thread these in the underlying GL */
	DRM_IOCTL_DEF_DRV(VIRTGPU_TRANSFER_FROM_HOST, virtgpu_transfer_from_host_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(VIRTGPU_TRANSFER_TO_HOST, virtgpu_transfer_to_host_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_WAIT, virtgpu_wait_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(VIRTGPU_GET_CAPS, virtgpu_get_caps_ioctl, DRM_AUTH|DRM_UNLOCKED),

};

int virtgpu_max_ioctls = DRM_ARRAY_SIZE(virtgpu_ioctls);


