#include <drm/drmP.h>
#include "virtio_drv.h"

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

static bool virtgpu_fence_seq_signaled(struct virtgpu_device *qdev, u64 seq, bool process)
{
	if (atomic64_read(&qdev->fence_drv.last_seq) >= seq)
		return true;

	if (process)
		virtgpu_fence_process(qdev);

	if (atomic64_read(&qdev->fence_drv.last_seq) >= seq)
		return true;
	return false;
}

static int virtgpu_fence_wait_seq(struct virtgpu_device *qdev, u64 target_seq,
				 bool intr)
{
  	uint64_t timeout, last_activity;
	uint64_t seq;
	bool signaled;
	int r;

	while (target_seq > atomic64_read(&qdev->fence_drv.last_seq)) {

		timeout = jiffies - VIRTGPU_FENCE_JIFFIES_TIMEOUT;
		if (time_after(qdev->fence_drv.last_activity, timeout)) {
			/* the normal case, timeout is somewhere before last_activity */
			timeout = qdev->fence_drv.last_activity - timeout;
		} else {
			/* either jiffies wrapped around, or no fence was signaled in the last 500ms
			 * anyway we will just wait for the minimum amount and then check for a lockup
			 */
			timeout = 1;
		}
		seq = atomic64_read(&qdev->fence_drv.last_seq);
		/* Save current last activity valuee, used to check for GPU lockups */
		last_activity = qdev->fence_drv.last_activity;

		//		radeon_irq_kms_sw_irq_get(rdev, ring);
		if (intr) {
			r = wait_event_interruptible_timeout(qdev->fence_queue,
							     (signaled = virtgpu_fence_seq_signaled(qdev, target_seq, true)),
				timeout);
                } else {
			r = wait_event_timeout(qdev->fence_queue,
					       (signaled = virtgpu_fence_seq_signaled(qdev, target_seq, true)),
				timeout);
		}
		//		radeon_irq_kms_sw_irq_put(rdev, ring);
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
			if (seq != atomic64_read(&qdev->fence_drv.last_seq)) {
				continue;
			}

			/* test if somebody else has already decided that this is a lockup */
			if (last_activity != qdev->fence_drv.last_activity) {
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

	if (virtgpu_fence_seq_signaled(fence->qdev, fence->seq, process)) {
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

	r = virtgpu_fence_wait_seq(fence->qdev, fence->seq,
				  intr);
	if (r)
		return r;

	fence->seq = VIRTGPU_FENCE_SIGNALED_SEQ;

	return 0;

}

int virtgpu_fence_emit(struct virtgpu_device *qdev,
		       struct virtgpu_command *cmd,
		       struct virtgpu_fence **fence)
{
	*fence = kmalloc(sizeof(struct virtgpu_fence), GFP_KERNEL);
	if ((*fence) == NULL)
		return -ENOMEM;

	kref_init(&((*fence)->kref));
	(*fence)->qdev = qdev;
	(*fence)->seq = ++qdev->fence_drv.sync_seq;

	cmd->flags |= VIRTGPU_COMMAND_EMIT_FENCE;
	cmd->fence_id = (*fence)->seq;

	return 0;
}
