#include "qxl_drv.h"
/* QXL fencing

   When we submit operations to the GPU we pass a release reference to the GPU
   with them, the release reference is then added to the release ring when
   the GPU is finished with that particular operation and has removed it from
   its tree.

   So we have can have multiple outstanding non linear fences per object.

   From a TTM POV we only care if the object has any outstanding releases on
   it.

   we wait until all outstanding releases are processeed.

   sync object is just a list of release ids that represent that fence on
   that buffer (??) to add a new fence though 

   okay sync objects we don't use TTM EU and we don't use move accel cleanup
   so we should be able to just implement a sync object hierarchy somehow.

   we just add new releases onto the sync object attached to the object.

*/


int qxl_fence_add_release(struct qxl_fence *qfence, uint32_t rel_id)
{
	spin_lock(&qfence->qdev->fence_lock);
	if (qfence->num_used_releases + 1 > qfence->num_alloc_releases) {
		qfence->num_alloc_releases += 4;
		qfence->release_ids = krealloc(qfence->release_ids, sizeof(uint32_t)*qfence->num_alloc_releases, GFP_ATOMIC);
		if (!qfence->release_ids) {
			qfence->num_alloc_releases -= 4;
			spin_unlock(&qfence->qdev->fence_lock);
			return -ENOMEM;
		}
	}
	qfence->release_ids[qfence->num_used_releases++] = rel_id;
	qfence->num_active_releases++;
	spin_unlock(&qfence->qdev->fence_lock);
	return 0;
}

int qxl_fence_remove_release(struct qxl_fence *qfence, uint32_t rel_id)
{
	int i;

	spin_lock(&qfence->qdev->fence_lock);
	for (i = 0; i < qfence->num_used_releases; i++)
		if (qfence->release_ids[i] == rel_id)
			break;

	if (i == qfence->num_used_releases) {
		spin_unlock(&qfence->qdev->fence_lock);
		return -ENOENT;
	}

	qfence->release_ids[i] = 0;
	qfence->num_active_releases--;
	spin_unlock(&qfence->qdev->fence_lock);
	return 0;
}


int qxl_fence_init(struct qxl_device *qdev, struct qxl_fence *qfence)
{
	qfence->qdev = qdev;
	qfence->num_alloc_releases = 0;
	qfence->num_used_releases = 0;
	qfence->num_active_releases = 0;
	qfence->release_ids = NULL;
	return 0;
}

void qxl_fence_fini(struct qxl_fence *qfence)
{
	kfree(qfence->release_ids);
	qfence->num_alloc_releases = 0;
	qfence->num_used_releases = 0;
	qfence->num_active_releases = 0;
}
