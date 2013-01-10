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
	if (qfence->num_releases + 1 > qfence->num_alloc_releases) {
		qfence->num_alloc_releases += 4;
		qfence->release_ids = krealloc(qfence->release_ids, qfence->num_alloc_releases, GFP_KERNEL);
		if (!qfence->release_ids) {
			qfence->num_alloc_releases -= 4;
			return -ENOMEM;
		}
	}
	qfence->release_ids[qfence->num_releases++] = rel_id;
	return 0;
}

int qxl_fence_remove_release(struct qxl_fence *qfence, uint32_t rel_id)
{
	int i;

	for (i = 0; i < qfence->num_releases; i++)
		if (qfence->release_ids[i] == rel_id)
			break;

	if (i == qfence->num_releases)
		return -ENOENT;

	if (i < qfence->num_releases)
		memcpy(&qfence->release_ids[i], &qfence->release_ids[i+1], sizeof(uint32_t)*(qfence->num_releases-i));
	qfence->num_releases--;
	return 0;
}
