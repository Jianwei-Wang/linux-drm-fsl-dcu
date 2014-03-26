#include <drm/drmP.h>
#include "virtgpu_drv.h"

struct virtgpu_fence_event {

	struct drm_device *dev;
	struct drm_file *file;
	struct virtgpu_fence *fence;
	struct list_head head;

	void (*seq_done)(struct virtgpu_fence_event *event);
	void (*cleanup)(struct virtgpu_fence_event *event);
	struct list_head fpriv_head;
	struct drm_pending_vblank_event *event;
};

u32 virtgpu_fence_read(struct virtgpu_device *vgdev)
{
	u32 fence_val = *(uint32_t *)(vgdev->fence_page);
	return fence_val;
}

static void virtgpu_fence_destroy(struct kref *kref)
{
	struct virtgpu_fence *fence;

	fence = container_of(kref, struct virtgpu_fence, kref);
	kfree(fence);
}

struct virtgpu_fence *virtgpu_fence_ref(struct virtgpu_fence *fence)
{
	kref_get(&fence->kref);
	return fence;
}

void virtgpu_fence_unref(struct virtgpu_fence **fence)
{
	struct virtgpu_fence *tmp = *fence;

	*fence = NULL;
	if (tmp) {
		kref_put(&tmp->kref, virtgpu_fence_destroy);
	}
}

static bool virtgpu_fence_seq_signaled(struct virtgpu_device *vgdev, u64 seq, bool process)
{
	if (atomic64_read(&vgdev->fence_drv.last_seq) >= seq)
		return true;

	if (process)
		virtgpu_fence_process(vgdev);

	if (atomic64_read(&vgdev->fence_drv.last_seq) >= seq)
		return true;
	return false;
}

static int virtgpu_fence_wait_seq(struct virtgpu_device *vgdev, u64 target_seq,
				 bool intr)
{
  	uint64_t last_activity;
	uint64_t seq;
	unsigned long timeout;
	bool signaled;
	int r;

	while (target_seq > atomic64_read(&vgdev->fence_drv.last_seq)) {

		timeout = jiffies - VIRTGPU_FENCE_JIFFIES_TIMEOUT;
		if (time_after(vgdev->fence_drv.last_activity, timeout)) {
			/* the normal case, timeout is somewhere before last_activity */
			timeout = vgdev->fence_drv.last_activity - timeout;
		} else {
			/* either jiffies wrapped around, or no fence was signaled in the last 500ms
			 * anyway we will just wait for the minimum amount and then check for a lockup
			 */
			timeout = 1;
		}
		seq = atomic64_read(&vgdev->fence_drv.last_seq);
		/* Save current last activity valuee, used to check for GPU lockups */
		last_activity = vgdev->fence_drv.last_activity;

		if (intr) {
			r = wait_event_interruptible_timeout(vgdev->fence_queue,
							     (signaled = virtgpu_fence_seq_signaled(vgdev, target_seq, true)),
				timeout);
                } else {
			r = wait_event_timeout(vgdev->fence_queue,
					       (signaled = virtgpu_fence_seq_signaled(vgdev, target_seq, true)),
				timeout);
		}
		if (unlikely(r < 0)) {
			return r;
		}

		if (unlikely(!signaled)) {
			/* we were interrupted for some reason and fence
			 * isn't signaled yet, resume waiting */
			if (r) {
				continue;
			}

			/* check if sequence value has changed since last_activity */
			if (seq != atomic64_read(&vgdev->fence_drv.last_seq)) {
				continue;
			}

			/* test if somebody else has already decided that this is a lockup */
			if (last_activity != vgdev->fence_drv.last_activity) {
				continue;
			}

		}
	}
	return 0;
}

bool virtgpu_fence_signaled(struct virtgpu_fence *fence, bool process)
{
	if (!fence)
		return true;

	if (fence->seq == VIRTGPU_FENCE_SIGNALED_SEQ)
		return true;

	if (virtgpu_fence_seq_signaled(fence->vgdev, fence->seq, process)) {
		fence->seq = VIRTGPU_FENCE_SIGNALED_SEQ;
		return true;
	}
	return false;
}

int virtgpu_fence_wait(struct virtgpu_fence *fence, bool intr)
{
	int r;

	if (fence == NULL)
		return -EINVAL;

	r = virtgpu_fence_wait_seq(fence->vgdev, fence->seq,
				  intr);
	if (r)
		return r;

	fence->seq = VIRTGPU_FENCE_SIGNALED_SEQ;

	return 0;

}

int virtgpu_fence_emit(struct virtgpu_device *vgdev,
		      struct virtgpu_cmd_hdr *cmd_hdr,
		      struct virtgpu_fence **fence)
{
	*fence = kmalloc(sizeof(struct virtgpu_fence), GFP_KERNEL);
	if ((*fence) == NULL)
		return -ENOMEM;

	kref_init(&((*fence)->kref));
	(*fence)->vgdev = vgdev;
	(*fence)->seq = ++vgdev->fence_drv.sync_seq;

	cmd_hdr->flags |= VIRTGPU_COMMAND_EMIT_FENCE;
	cmd_hdr->fence_id = (*fence)->seq;

	return 0;
}

static void virtgpu_fence_event_cleanup(struct virtgpu_fence_event *event)
{
	kfree(event);
}

static void virtgpu_fence_event_signaled(struct virtgpu_fence_event *fence_event)
{
	struct drm_device *dev = fence_event->dev;
	unsigned long irq_flags;

	spin_lock_irqsave(&dev->event_lock, irq_flags);

	drm_send_vblank_event(dev, -1, fence_event->event);

	spin_unlock_irqrestore(&dev->event_lock, irq_flags);
}

static void virtgpu_fence_event_add(struct virtgpu_fence *fence,
				  struct virtgpu_fence_event *event)
{
	struct virtgpu_device *vdev = fence->vgdev;

	unsigned long irq_flags;
	/* need to check if the fence has signaled already before adding
	   it to the list */
	if (virtgpu_fence_signaled(fence, true)) {
		DRM_DEBUG_KMS("event signaled already\n");
		event->seq_done(event);
		event->cleanup(event);
	} else {
		DRM_DEBUG_KMS("event not signaled add to q\n");
		spin_lock_irqsave(&vdev->fence_drv.event_lock, irq_flags);
		list_add_tail(&event->head, &vdev->fence_drv.event_list);
		spin_unlock_irqrestore(&vdev->fence_drv.event_lock, irq_flags);
	}
}
				  
int virtgpu_fence_event_queue(struct drm_file *file,
			    struct virtgpu_fence *fence,
			    struct drm_pending_vblank_event *event)
{
	struct virtgpu_fence_event *fence_event;

	fence_event = kzalloc(sizeof(*fence_event), GFP_KERNEL);
	if (event == NULL)
		return -ENOMEM;

	fence_event->event = event;
	fence_event->file = file;
	fence_event->dev = fence->vgdev->ddev;
	fence_event->event = event;
	fence_event->seq_done = virtgpu_fence_event_signaled;
	fence_event->cleanup = virtgpu_fence_event_cleanup;
	fence_event->fence = fence;

	virtgpu_fence_event_add(fence, fence_event);
	return 0;
}

void virtgpu_fence_event_process(struct virtgpu_device *vdev, u64 last_seq)
{
	unsigned long irq_flags;
	struct virtgpu_fence_event *fe, *tmp;

	spin_lock_irqsave(&vdev->fence_drv.event_lock, irq_flags);
	list_for_each_entry_safe(fe, tmp, &vdev->fence_drv.event_list, head) {
		if (virtgpu_fence_signaled(fe->fence, false)) {
			DRM_DEBUG_KMS("event signaled in irq\n");
			fe->seq_done(fe);
			list_del(&fe->head);
			fe->cleanup(fe);
		}	       
	}
	spin_unlock_irqrestore(&vdev->fence_drv.event_lock, irq_flags);
}

void virtgpu_fence_process(struct virtgpu_device *vgdev)
{
	bool wake = false;
	u64 last_seq, last_emitted, seq;
	unsigned count_loop = 0;

	last_seq = atomic64_read(&vgdev->fence_drv.last_seq);
	do {
		last_emitted = vgdev->fence_drv.sync_seq;
		seq = virtgpu_fence_read(vgdev);
		seq |= last_seq & 0xffffffff00000000LL;
		if (seq < last_seq) {
			seq &= 0xffffffff;
			seq |= last_emitted & 0xffffffff00000000LL;
		}
		if (seq <= last_seq || seq > last_emitted) {
			break;
		}
		wake = true;
		last_seq = seq;
		if ((count_loop++) > 10) {
			break;

		}
	} while (atomic64_xchg(&vgdev->fence_drv.last_seq, seq) > seq);

	virtgpu_fence_event_process(vgdev, last_seq);
	if (wake) {
		vgdev->fence_drv.last_activity = jiffies;
		wake_up_all(&vgdev->fence_queue);
	}
	
}
