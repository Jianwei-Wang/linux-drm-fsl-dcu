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
	switch (qxl_alloc->type) {
	case QXL_ALLOC_TYPE_SURFACE_PRIMARY:
		/*
		 * TODO: would be nice if the primary always had a handle of 1,
		 * but for that we would need to change drm gem code a little,
		 * so probably not worth it.  Note: The size is a actually
		 * ignored here, we return a handle to the primary surface BO
		 */
		ret = qxl_get_handle_for_primary_fb(qdev, file_priv,
						    &qxl_alloc->handle);
		if (ret) {
			DRM_ERROR("%s: failed to allocate handle for primary"
				  "fb gem object %p\n", __func__,
				  qdev->fbdev_qfb->obj);
		}
		break;
	case QXL_ALLOC_TYPE_SURFACE:
	case QXL_ALLOC_TYPE_DATA: {
		struct qxl_bo *qobj;
		uint32_t handle;
		u32 domain = qxl_alloc->type == QXL_ALLOC_TYPE_SURFACE ?
			     QXL_GEM_DOMAIN_SURFACE : QXL_GEM_DOMAIN_VRAM;
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
		break;
	}
	default:
		DRM_ERROR("%s: unexpected alloc type %d\n", __func__,
			  qxl_alloc->type);
		return -EINVAL;
		break;
	}
	return 0;
}

int qxl_incref_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_qxl_incref *incref = data;
	struct drm_gem_object *gobj;

	/* this takes a reference */
	gobj = drm_gem_object_lookup(dev, file_priv, incref->handle);

	if (!gobj) {
		DRM_ERROR("%s: invalid handle %u\n", __func__, incref->handle);
		return -EINVAL;
	}
	return 0;
}

int qxl_decref_ioctl(struct drm_device *dev, void *data,
		     struct drm_file *file_priv)
{
	struct drm_qxl_decref *decref = data;
	struct drm_gem_object *gobj;

	gobj = drm_gem_object_lookup(dev, file_priv, decref->handle);
	if (!gobj) {
		DRM_ERROR("%s: invalid handle %u\n", __func__, decref->handle);
		return -EINVAL;
	}
	/* remove reference taken by lookup */
	drm_gem_object_unreference_unlocked(gobj);

	drm_gem_object_unreference_unlocked(gobj);
	return 0;
}

int qxl_map_ioctl(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_map *qxl_map = data;

	DRM_INFO("%s: handle in %d\n", __func__, qxl_map->handle);
	return qxl_mode_dumb_mmap(file_priv, qdev->ddev, qxl_map->handle,
				  &qxl_map->offset);
}

/*
 * dst must be validated, i.e. whole bo on vram/surfacesram (right now all bo's
 * are on vram).
 * *(src + src_off) = qxl_bo_physical_address(dst, dst_off)
 */
static void
apply_reloc(struct qxl_device *qdev, struct qxl_bo *src, uint64_t src_off,
	    struct qxl_bo *dst, uint64_t dst_off)
{
	*(uint64_t *)(src->kptr + src_off) = qxl_bo_physical_address(qdev,
			dst, dst_off, qdev->main_mem_slot);
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
		qxl_io_log(qdev, "%s: type %d, size %d, #relocs %d\n",
			   __func__, user_cmd.type,
			   user_cmd.command_size, user_cmd.relocs_num);
		for (i = 0 ; i < user_cmd.relocs_num; ++i) {
			if (DRM_COPY_FROM_USER(&reloc,
				&((struct drm_qxl_reloc *)user_cmd.relocs)[i],
				sizeof(reloc)))
				return -EFAULT;
			qxl_io_log(qdev, "%s: r#%d: %d+%d->%d+%d\n",
				   __func__, i, reloc.src_handle,
				   reloc.src_offset, reloc.dst_handle,
				   reloc.dst_offset);
			reloc_src_bo =
				qxlhw_handle_to_bo(qdev, file_priv,
						   reloc.src_handle, cmd_bo);
			reloc_dst_bo =
				qxlhw_handle_to_bo(qdev, file_priv,
						   reloc.dst_handle, cmd_bo);
			if (!reloc_src_bo || !reloc_dst_bo)
				return -EINVAL;
			apply_reloc(qdev, reloc_src_bo, reloc.src_offset,
				    reloc_dst_bo, reloc.dst_offset);

			if (reloc_dst_bo != cmd_bo) {
				qxl_release_add_res(qdev, release, qxl_bo_ref(reloc_dst_bo));
				drm_gem_object_unreference_unlocked(&reloc_dst_bo->gem_base);
			}

			if (reloc_src_bo != cmd_bo)
				drm_gem_object_unreference_unlocked(&reloc_src_bo->gem_base);
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
			  struct drm_file *file_priv)
{
	struct qxl_device *qdev = dev->dev_private;
	struct drm_qxl_update_area *update_area = data;
	struct qxl_rect area = {.left = update_area->left,
				.top = update_area->top,
				.right = update_area->right,
				.bottom = update_area->bottom};

	if (update_area->surface_id > NUM_SURFACES ||
	    update_area->left >= update_area->right ||
	    update_area->top >= update_area->bottom)
		return -EINVAL;

	qxl_io_update_area(qdev, update_area->surface_id, &area);
	return 0;
}

struct drm_ioctl_desc qxl_ioctls[] = {
	DRM_IOCTL_DEF_DRV(QXL_ALLOC, qxl_alloc_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(QXL_INCREF, qxl_incref_ioctl, DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(QXL_DECREF, qxl_decref_ioctl, DRM_AUTH|DRM_UNLOCKED),

	/* NB: QXL_MAP doesn't really map, it is similar to DUMPMAP in that it
	 * provides the caller (userspace) with an offset to give to the mmap
	 * system call, which ends up in qxl_mmap, which calls ttm_bo_mmap,
	 * which actually mmaps. */
	DRM_IOCTL_DEF_DRV(QXL_MAP, qxl_map_ioctl, DRM_AUTH|DRM_UNLOCKED),

	DRM_IOCTL_DEF_DRV(QXL_EXECBUFFER, qxl_execbuffer_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(QXL_UPDATE_AREA, qxl_update_area_ioctl,
							DRM_AUTH|DRM_UNLOCKED),
};

int qxl_max_ioctls = DRM_ARRAY_SIZE(qxl_ioctls);
