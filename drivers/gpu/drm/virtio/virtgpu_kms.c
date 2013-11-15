#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <drm/drmP.h>
#include "virtgpu_drv.h"

int virtgpu_max_ioctls;

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
	struct virtqueue *vqs[3];
	vq_callback_t *callbacks[] = { virtgpu_ctrl_ack, virtgpu_cursor_ack, virtgpu_event_ack };
	const char *names[] = { "control", "cursor", "event" };
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
	init_waitqueue_head(&vgdev->resp_wq);
	virtgpu_init_vq(&vgdev->ctrlq, virtgpu_dequeue_ctrl_func);
	virtgpu_init_vq(&vgdev->cursorq, virtgpu_dequeue_cursor_func);
	virtgpu_init_vq(&vgdev->eventq, virtgpu_dequeue_event_func);

	vgdev->cursor_page = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!vgdev->cursor_page) {
		kfree(vgdev);
		return -ENOMEM;
	}

	nvqs = 3;

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

        /* get display info */
	virtgpu_cmd_get_display_info(vgdev);
	ret = wait_event_timeout(vgdev->resp_wq, vgdev->display_info.num_scanouts > 0, 5 * HZ);
	if (ret == 0) {
		DRM_ERROR("failed to get display info resp from hw in 5s\n");
		kfree(vgdev);
		return ret;
	}
	
	vgdev->num_hw_scanouts = vgdev->display_info.num_scanouts;
	DRM_INFO("got %d outputs\n", vgdev->display_info.num_scanouts);
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

	kfree(vgdev->cursor_page);
	kfree(vgdev);
	return 0;
}
