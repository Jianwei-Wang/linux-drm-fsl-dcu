
#include "qxl_drv.h"
#include "qxl_object.h"
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

bool qxl_3d_fence_signaled(struct qxl_3d_fence *fence)
{
	if (!fence)
		return true;

	return false;
}

int qxl_3d_fence_wait(struct qxl_3d_fence *fence, bool intr)
{
	return 0;

}
