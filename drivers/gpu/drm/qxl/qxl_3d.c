
#include "qxl_drv.h"
#include "qxl_object.h"

static u32 qxl_3d_fence_read(struct qxl_device *qdev)
{
	return qdev->q3d_info.ram_3d_header->last_fence;
}

void qxl_fini_3d(struct qxl_device *qdev)
{
	qxl_ring_free(qdev->q3d_info.iv3d_ring);

	qxl_bo_unref(&qdev->q3d_info.ringbo);
}

int qxl_init_3d(struct qxl_device *qdev)
{
	/* create an object for the 3D ring and pin it at 0. */
	int ret;

	ret = qxl_bo_create(qdev, sizeof(struct qxl_3d_ram), true,
			    QXL_GEM_DOMAIN_3D, NULL, &qdev->q3d_info.ringbo);

	if (ret)
		return ret;

	ret = qxl_bo_reserve(qdev->q3d_info.ringbo, false);
	if (unlikely(ret != 0))
		goto out_unref;

	ret = qxl_bo_pin(qdev->q3d_info.ringbo, QXL_GEM_DOMAIN_3D, NULL);
	if (ret)
		goto out_unreserve;

	ret = qxl_bo_kmap(qdev->q3d_info.ringbo, (void **)&qdev->q3d_info.ram_3d_header);
	if (ret)
		goto out_unreserve;

	qdev->q3d_info.iv3d_ring = qxl_ring_create(&(qdev->q3d_info.ram_3d_header->cmd_ring_hdr),
					  sizeof(struct qxl_command),
					  QXL_COMMAND_RING_SIZE,
					  0, 
					  false,
					  &qdev->display_event);

out_unreserve:
	qxl_bo_unreserve(qdev->q3d_info.ringbo);

out_unref:
	qxl_bo_unref(&qdev->q3d_info.ringbo);
	return ret;
}
	  
int qxl_execbuffer_3d(struct drm_device *dev,
		      struct drm_qxl_execbuffer *execbuffer,
		      struct drm_file *drm_file)
{
	return 0;
}

static void qxl_3d_fence_destroy(struct kref *kref)
{
	struct qxl_3d_fence *fence;

	fence = container_of(kref, struct qxl_3d_fence, kref);
	kfree(fence);
}

struct qxl_3d_fence *qxl_3d_fence_ref(struct qxl_3d_fence *fence)
{
	kref_get(&fence->kref);
	return fence;
}

void qxl_3d_fence_unref(struct qxl_3d_fence **fence)
{
	struct qxl_3d_fence *tmp = *fence;

	*fence = NULL;
	if (tmp) {
		kref_put(&tmp->kref, qxl_3d_fence_destroy);
	}
}

static bool qxl_3d_fence_seq_signaled(struct qxl_device *qdev, u64 seq)
{
	if (atomic64_read(&qdev->q3d_info.fence_drv.last_seq) >= seq)
		return true;

	qxl_3d_fence_process(qdev);

	if (atomic64_read(&qdev->q3d_info.fence_drv.last_seq) >= seq)
		return true;
	return false;
}

static int qxl_3d_fence_wait_seq(struct qxl_device *qdev, u64 target_seq,
				 bool intr)
{
	return 0;
}

bool qxl_3d_fence_signaled(struct qxl_3d_fence *fence)
{
	if (!fence)
		return true;

	if (fence->seq == QXL_3D_FENCE_SIGNALED_SEQ)
		return true;

	if (qxl_3d_fence_seq_signaled(fence->qdev, fence->seq)) {
		fence->seq = QXL_3D_FENCE_SIGNALED_SEQ;
		return true;
	}
	return false;
}

int qxl_3d_fence_wait(struct qxl_3d_fence *fence, bool intr)
{
	int r;

	if (fence == NULL)
		return -EINVAL;

	r = qxl_3d_fence_wait_seq(fence->qdev, fence->seq,
				  intr);
	if (r)
		return r;

	fence->seq = QXL_3D_FENCE_SIGNALED_SEQ;

	return 0;

}

int qxl_3d_fence_emit(struct qxl_device *qdev,
		      struct qxl_3d_fence **fence)
{
	*fence = kmalloc(sizeof(struct qxl_3d_fence), GFP_KERNEL);
	if ((*fence) == NULL)
		return -ENOMEM;

	kref_init(&((*fence)->kref));
	(*fence)->qdev = qdev;
	(*fence)->seq = ++qdev->q3d_info.fence_drv.sync_seq;
	return 0;
}
		     
void qxl_3d_fence_process(struct qxl_device *qdev)
{
	bool wake = false;
	u64 last_seq, last_emitted, seq;
	unsigned count_loop = 0;

	last_seq = atomic64_read(&qdev->q3d_info.fence_drv.last_seq);
	do {
		last_emitted = qdev->q3d_info.fence_drv.sync_seq;
		seq = qxl_3d_fence_read(qdev);
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
	} while (atomic64_xchg(&qdev->q3d_info.fence_drv.last_seq, seq) > seq);

	if (wake) {
		qdev->q3d_info.fence_drv.last_activity = jiffies;
		wake_up_all(&qdev->q3d_info.fence_queue);
	}
	
}
