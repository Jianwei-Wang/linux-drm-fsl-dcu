#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <drm/drmP.h>
#include "virtgpu_drv.h"

static int virtgpu_ctx_id_get(struct virtgpu_device *vgdev, uint32_t *resid)
{
	int handle;

	idr_preload(GFP_KERNEL);
	spin_lock(&vgdev->ctx_id_idr_lock);
	handle = idr_alloc(&vgdev->ctx_id_idr, NULL, 1, 0, 0);
	spin_unlock(&vgdev->ctx_id_idr_lock);
	idr_preload_end();
	*resid = handle;
	return 0;
}

static void virtgpu_ctx_id_put(struct virtgpu_device *vgdev, uint32_t id)
{
	spin_lock(&vgdev->ctx_id_idr_lock);
	idr_remove(&vgdev->ctx_id_idr, id);
	spin_unlock(&vgdev->ctx_id_idr_lock);	
}

static int virtgpu_context_create(struct virtgpu_device *vgdev, uint32_t nlen,
				   const char *name, uint32_t *ctx_id)
{
	int ret;

	ret = virtgpu_ctx_id_get(vgdev, ctx_id);
	if (ret)
		return ret;

	ret = virtgpu_cmd_context_create(vgdev, *ctx_id, nlen, name);
	if (ret) {
		virtgpu_ctx_id_put(vgdev, *ctx_id);
		return ret;
	}
	return 0;
}

static int virtgpu_context_destroy(struct virtgpu_device *vgdev, uint32_t ctx_id)
{
	int ret;

	ret = virtgpu_cmd_context_destroy(vgdev, ctx_id);
	virtgpu_ctx_id_put(vgdev, ctx_id);
	return ret;
}

static void virtgpu_init_vq(struct virtgpu_queue *vgvq, void (*work_func)(struct work_struct *work))
{
	spin_lock_init(&vgvq->qlock);
	init_waitqueue_head(&vgvq->ack_queue);
	INIT_WORK(&vgvq->dequeue_work, work_func);
}

int virtgpu_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct virtgpu_device *vgdev;
	/* this will expand later */
	struct virtqueue *vqs[4];
	vq_callback_t *callbacks[] = { virtgpu_ctrl_ack, virtgpu_cursor_ack, virtgpu_event_ack, virtgpu_fence_ack };
	const char *names[] = { "control", "cursor", "event", "fence" };
	int nvqs;
	int ret;

	vgdev = kzalloc(sizeof(struct virtgpu_device), GFP_KERNEL);
	if (!vgdev)
		return -ENOMEM;

	vgdev->ddev = dev;
	dev->dev_private = vgdev;
	vgdev->vdev = dev->virtdev;
	vgdev->dev = dev->dev;

	spin_lock_init(&vgdev->display_info_lock);
	spin_lock_init(&vgdev->ctx_id_idr_lock);
	idr_init(&vgdev->ctx_id_idr);
	spin_lock_init(&vgdev->resource_idr_lock);
	idr_init(&vgdev->resource_idr);
	init_waitqueue_head(&vgdev->resp_wq);
	virtgpu_init_vq(&vgdev->ctrlq, virtgpu_dequeue_ctrl_func);
	virtgpu_init_vq(&vgdev->cursorq, virtgpu_dequeue_cursor_func);
	virtgpu_init_vq(&vgdev->eventq, virtgpu_dequeue_event_func);
	virtgpu_init_vq(&vgdev->fenceq, virtgpu_dequeue_fence_func);

	spin_lock_init(&vgdev->fence_drv.event_lock);
	INIT_LIST_HEAD(&vgdev->fence_drv.event_list);
	init_waitqueue_head(&vgdev->fence_queue);

	if (virtio_has_feature(vgdev->vdev, VIRTIO_GPU_F_FENCE)) {
		vgdev->has_fence = true;
		if (virtio_has_feature(vgdev->vdev, VIRTIO_GPU_F_VIRGL)) {
			vgdev->has_virgl_3d = true;
		}
	}

	if (vgdev->has_fence) {
		vgdev->fence_page = kzalloc(PAGE_SIZE, GFP_KERNEL);
		if (!vgdev->fence_page) {
			kfree(vgdev);
			return -ENOMEM;
		}
	}

	nvqs = 3 + (vgdev->has_fence ? 1 : 0);

	ret = vgdev->vdev->config->find_vqs(vgdev->vdev, nvqs, vqs, callbacks, names);
	if (ret) {
		DRM_ERROR("failed to find virt queues\n");
		kfree(vgdev);
		return ret;
	}

	vgdev->ctrlq.vq = vqs[0];
	vgdev->cursorq.vq = vqs[1];
	vgdev->eventq.vq = vqs[2];
	virtgpu_fill_event_vq(vgdev, 64);

	if (vgdev->has_fence) {
		vgdev->fenceq.vq = vqs[3];
		virtgpu_fill_fence_vq(vgdev, 64);
	}

	ret = virtgpu_ttm_init(vgdev);
	if (ret) {
		DRM_ERROR("failed to init ttm %d\n", ret);
		kfree(vgdev);
		return ret;
	}

        /* get display info */
	virtgpu_cmd_get_display_info(vgdev);
	ret = wait_event_timeout(vgdev->resp_wq, vgdev->display_info.num_scanouts > 0, 5 * HZ);
	if (ret == 0) {
		DRM_ERROR("failed to get display info resp from hw in 5s\n");
		kfree(vgdev);
		return -EINVAL;
	}
	vgdev->num_hw_scanouts = vgdev->display_info.num_scanouts;
	DRM_INFO("got %d outputs\n", vgdev->display_info.num_scanouts);

	if (vgdev->has_virgl_3d) {
		virtgpu_cmd_get_3d_caps(vgdev);
		ret = wait_event_timeout(vgdev->resp_wq, vgdev->caps.max_version > 0, 5 * HZ);
		if (ret == 0) {
			DRM_ERROR("failed to get 3d caps resp from hw in 5s\n");
			kfree(vgdev);
			return -EINVAL;
		}
	}

	ret = virtgpu_modeset_init(vgdev);
	if (ret) {
		kfree(vgdev);
		return ret;
	}

	return 0;
}

int virtgpu_driver_unload(struct drm_device *dev)
{
	struct virtgpu_device *vgdev = dev->dev_private;

	virtgpu_modeset_fini(vgdev);

	virtgpu_ttm_fini(vgdev);
	kfree(vgdev->fence_page);
	kfree(vgdev);
	return 0;
}

int virtgpu_driver_open(struct drm_device *dev, struct drm_file *file)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct virtgpu_fpriv *vfpriv;
	uint32_t id;
	int ret;
	char dbgname[64], tmpname[TASK_COMM_LEN];
	
	/* can't create contexts without 3d renderer */
	if (!vgdev->has_virgl_3d)
		return 0;

	get_task_comm(tmpname, current);
	snprintf(dbgname, sizeof(dbgname), "%s", tmpname);
	dbgname[63] = 0;
	/* allocate a virt GPU context for this opener */
	vfpriv = kzalloc(sizeof(*vfpriv), GFP_KERNEL);
	if (!vfpriv)
		return -ENOMEM;

	ret = virtgpu_context_create(vgdev, strlen(dbgname), dbgname, &id);
	if (ret) {
		kfree(vfpriv);
		return ret;
	}

	vfpriv->ctx_id = id;
	file->driver_priv = vfpriv;
	return 0;
}

void virtgpu_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct virtgpu_device *vgdev = dev->dev_private;
	struct virtgpu_fpriv *vfpriv;

	if (!vgdev->has_virgl_3d)
		return;

	vfpriv = file->driver_priv;

	virtgpu_context_destroy(vgdev, vfpriv->ctx_id);
	kfree(vfpriv);
	file->driver_priv = NULL;
}
