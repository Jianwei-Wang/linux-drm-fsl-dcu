#include "qxl_drv.h"
#include "qxl_object.h"

/*
 * TODO: allocating a new gem(in qxl_bo) for each request.
 * This is wasteful since bo's are page aligned.
 */
int qxl_alloc_ioctl(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_alloc *qxl_alloc = data;
	int ret;
	/*
	 * TODO: actually take note of the drm_qxl_alloc->type flag, except for
	 * the primary
	 * surface creation (i.e. use the surfaces bar)
	 */

	if (qxl_alloc->size == 0) {
		DRM_ERROR("invalid size %d\n", qxl_alloc->size);
		return -EINVAL;
	}
	{
		struct qxl_bo *qobj;
		uint32_t handle;
		u32 domain = QXL_GEM_DOMAIN_VRAM;
		ret = qxl_gem_object_create_with_handle(qdev, file_priv,
							domain,
							qxl_alloc->size,
							&qobj, &handle);
		if (ret) {
			DRM_ERROR("%s: failed to create gem ret=%d\n",
				  __func__, ret);
			return -ENOMEM;
		}
		qxl_alloc->handle = handle;
	}
	return 0;
}

int qxl_map_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_map *qxl_map = data;

	return qxl_mode_dumb_mmap(file_priv, qdev->ddev, qxl_map->handle,
				  &qxl_map->offset);
}

/*
 * dst must be validated, i.e. whole bo on vram/surfacesram (right now all bo's
 * are on vram).
 * *(dst + dst_off) = qxl_bo_physical_address(src, src_off)
 */
static void
apply_reloc(struct qxl_device *qdev, struct qxl_bo *dst, uint64_t dst_off,
	    struct qxl_bo *src, uint64_t src_off)
{
	qxl_bo_kmap(dst, NULL);
	*(uint64_t *)(dst->kptr + dst_off) = qxl_bo_physical_address(qdev,
								     src, src_off);
	qxl_bo_kunmap(dst);
}

static void
apply_surf_reloc(struct qxl_device *qdev, struct qxl_bo *dst, uint64_t dst_off,
		 struct qxl_bo *src)
{
	uint32_t id = 0;

	qxl_bo_kmap(dst, NULL);
	if (src && !src->is_primary)
		id = src->surface_id;
	*(uint32_t *)(dst->kptr + dst_off) = id;
	qxl_bo_kunmap(dst);
}

/* return holding the reference to this object */
struct qxl_bo *qxlhw_handle_to_bo(struct qxl_device *qdev,
		struct drm_file *file_priv, uint64_t handle,
		struct qxl_bo *handle_0_bo)
{
	struct drm_gem_object *gobj;
	struct qxl_bo *qobj;

	if (handle == 0)
		return handle_0_bo;
	gobj = drm_gem_object_lookup(qdev->ddev, file_priv, handle);
	if (!gobj) {
		DRM_ERROR("bad bo handle %lld\n", handle);
		return NULL;
	}
	qobj = gem_to_qxl_bo(gobj);
	return qobj;
}

/*
 * Usage of execbuffer:
 * Relocations need to take into account the full QXLDrawable size.
 * However, the command as passed from user space must *not* contain the initial
 * QXLReleaseInfo struct (first XXX bytes)
 */
int qxl_execbuffer_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_execbuffer *execbuffer = data;
	struct drm_qxl_command user_cmd;
	int cmd_num;
	struct qxl_bo *reloc_src_bo;
	struct qxl_bo *reloc_dst_bo;
	struct drm_qxl_reloc reloc;
	void *fb_cmd;
	int i;

	for (cmd_num = 0; cmd_num < execbuffer->commands_num; ++cmd_num) {
		struct drm_qxl_release *release;
		struct qxl_bo *cmd_bo;
		int release_type;
		int is_cursor;
		struct drm_qxl_command *commands =
			(struct drm_qxl_command *)execbuffer->commands;

		if (DRM_COPY_FROM_USER(&user_cmd, &commands[cmd_num],
				       sizeof(user_cmd)))
			return -EFAULT;
		switch (user_cmd.type) {
		case QXL_CMD_DRAW:
			release_type = QXL_RELEASE_DRAWABLE;
			is_cursor = 0;
			break;
		case QXL_CMD_SURFACE:
			release_type = QXL_RELEASE_SURFACE_CMD;
			is_cursor = 0;
			break;
		case QXL_CMD_CURSOR:
			release_type = QXL_RELEASE_CURSOR_CMD;
			is_cursor = 1;
			break;
		default:
			qxl_io_log(qdev,
				   "%s: bad command %d not in {%d, %d, %d}\n",
				   __func__, user_cmd.type,
				   QXL_CMD_DRAW, QXL_CMD_SURFACE,
				   QXL_CMD_CURSOR);
			return -EFAULT;
		}
		fb_cmd = qxl_alloc_releasable(qdev,
					      sizeof(union qxl_release_info) +
					      user_cmd.command_size,
					      release_type,
					      &release,
					      &cmd_bo);
		if (DRM_COPY_FROM_USER(fb_cmd + sizeof(union qxl_release_info),
					(void *)user_cmd.command,
					user_cmd.command_size))
			return -EFAULT;
#if 0
		qxl_io_log(qdev, "%s: type %d, size %d, #relocs %d\n",
			   __func__, user_cmd.type,
			   user_cmd.command_size, user_cmd.relocs_num);
#endif
		for (i = 0 ; i < user_cmd.relocs_num; ++i) {
			if (DRM_COPY_FROM_USER(&reloc,
				&((struct drm_qxl_reloc *)user_cmd.relocs)[i],
				sizeof(reloc)))
				return -EFAULT;
#if 0
			qxl_io_log(qdev, "%s: r#%d: %d+%d->%d+%d\n",
				   __func__, i, reloc.src_handle,
				   reloc.src_offset, reloc.dst_handle,
				   reloc.dst_offset);
#endif
			reloc_dst_bo =
				qxlhw_handle_to_bo(qdev, file_priv,
						   reloc.dst_handle, cmd_bo);
			if (!reloc_dst_bo)
				return -EINVAL;

			if (reloc.reloc_type == QXL_RELOC_TYPE_BO || reloc.src_handle > 0) {
				reloc_src_bo =
					qxlhw_handle_to_bo(qdev, file_priv,
							   reloc.src_handle, cmd_bo);
				if (!reloc_src_bo)
					return -EINVAL;
			} else
				reloc_src_bo = NULL;
			if (reloc.reloc_type == QXL_RELOC_TYPE_BO) {
				apply_reloc(qdev, reloc_dst_bo, reloc.dst_offset,
					    reloc_src_bo, reloc.src_offset);
			} else if (reloc.reloc_type == QXL_RELOC_TYPE_SURF) {
				apply_surf_reloc(qdev, reloc_dst_bo, reloc.dst_offset, reloc_src_bo);
			} else {
				DRM_ERROR("unknown reloc type %d\n", reloc.reloc_type);
				return -EINVAL;
			}
				
			if (reloc_src_bo && reloc_src_bo != cmd_bo) {
				qxl_release_add_res(qdev, release, qxl_bo_ref(reloc_src_bo));
				drm_gem_object_unreference_unlocked(&reloc_src_bo->gem_base);
			}

			if (reloc_dst_bo != cmd_bo)
				drm_gem_object_unreference_unlocked(&reloc_dst_bo->gem_base);
		}
		/* TODO: multiple commands in a single push (introduce new
		 * QXLCommandBunch ?) */
		if (is_cursor)
			qxl_push_cursor_ring(qdev, cmd_bo, user_cmd.type);
		else
			qxl_push_command_ring(qdev, cmd_bo, user_cmd.type);
	}
	return 0;
}

/* TODO: this should be defined in ram or rom */
#define NUM_SURFACES 1024

int qxl_update_area_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_update_area *update_area = data;
	struct qxl_rect area = {.left = update_area->left,
				.top = update_area->top,
				.right = update_area->right,
				.bottom = update_area->bottom};
	int ret;
	struct drm_gem_object *gobj = NULL;
	struct qxl_bo *qobj = NULL;

	if (update_area->left >= update_area->right ||
	    update_area->top >= update_area->bottom)
		return -EINVAL;

	if (update_area->handle) {
		gobj = drm_gem_object_lookup(dev, file, update_area->handle);
		if (gobj == NULL)
			return -ENOENT;

		qobj = gem_to_qxl_bo(gobj);
	}
	ret = qxl_io_update_area(qdev, qobj, &area);

out:
	if (gobj)
		drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static int qxl_getparam_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_getparam *param = data;

	switch (param->param) {
	case QXL_PARAM_NUM_SURFACES:
		param->value = qdev->rom->n_surfaces;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int qxl_clientcap_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_clientcap *param = data;
	int byte, idx;

	byte = param->index / 8;
	idx = param->index % 8;

	if (qdev->pdev->revision < 4)
	    return -ENOSYS;

	if (byte > 58)
	    return -ENOSYS;

	if (qdev->rom->client_capabilities[byte] & (1 << idx))
	    return 0;
	return -ENOSYS;
}

static int qxl_alloc_surf_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_alloc_surf *param = data;
	struct qxl_bo *qobj;
	int handle;
	int ret;
	int size, actual_stride;
	
	/* work out size allocate bo with handle */
	actual_stride = param->stride < 0 ? -param->stride : param->stride;
	size = actual_stride * param->height + actual_stride;

	ret = qxl_gem_object_create_with_handle(qdev, file,
						QXL_GEM_DOMAIN_SURFACE,
						size,
						&qobj, &handle);
	if (ret) {
		DRM_ERROR("%s: failed to create gem ret=%d\n",
			  __func__, ret);
		return -ENOMEM;
	}

	ret = qxl_surface_id_alloc(qdev, qobj);
	if (ret)
		goto fail;

	qobj->surf.format = param->format;
	qobj->surf.width = param->width;
	qobj->surf.height = param->height;
	qobj->surf.stride = param->stride;

	ret = qxl_hw_surface_alloc(qdev, qobj);
	if (ret)
		goto fail_surf;
	param->handle = handle;
	return ret;
fail_surf:
	qxl_surface_id_dealloc(qdev, qobj);
fail:
	drm_gem_object_handle_unreference_unlocked(&qobj->gem_base);
	return ret;
}

struct drm_ioctl_desc qxl_ioctls[] = {
	DRM_IOCTL_DEF_DRV(QXL_ALLOC, qxl_alloc_ioctl, DRM_AUTH|DRM_UNLOCKED),

	/* NB: QXL_MAP doesn't really map, it is similar to DUMPMAP in that it
	 * provides the caller (userspace) with an offset to give to the mmap
	 * system call, which ends up in qxl_mmap, which calls ttm_bo_mmap,
	 * which actually mmaps. */
	DRM_IOCTL_DEF_DRV(QXL_MAP, qxl_map_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(QXL_EXECBUFFER, qxl_execbuffer_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(QXL_UPDATE_AREA, qxl_update_area_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(QXL_GETPARAM, qxl_getparam_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(QXL_CLIENTCAP, qxl_clientcap_ioctl,
							DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(QXL_ALLOC_SURF, qxl_alloc_surf_ioctl,
			  DRM_AUTH|DRM_UNLOCKED),
};

int qxl_max_ioctls = DRM_ARRAY_SIZE(qxl_ioctls);
