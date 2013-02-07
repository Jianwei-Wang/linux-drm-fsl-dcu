/*
 * Copyright 2011 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "qxl_drv.h"
#include "qxl_object.h"

/*
 * drawable cmd cache - allocate a bunch of VRAM pages, suballocate
 * into 256 byte chunks for now - gives 16 cmds per page.
 *
 * use an ida to index into the chunks?
 */
/* manage releaseables */
/* stack them 16 high for now -drawable object is 191 */
#define RELEASES_PER_BO (4096 / 256)

uint64_t
qxl_release_alloc(struct qxl_device *qdev, int type,
		  struct drm_qxl_release **ret)
{
	struct drm_qxl_release *release;
	int handle = 0;
	size_t size = sizeof(*release);
	int idr_ret;

	release = kmalloc(size, GFP_KERNEL);
	if (!release) {
		DRM_ERROR("Out of memory\n");
		return 0;
	}
	release->type = type;
	release->bo_count = 0;
	release->release_offset = 0;
again:
	if (idr_pre_get(&qdev->release_idr, GFP_KERNEL) == 0) {
		DRM_ERROR("Out of memory for release idr\n");
		kfree(release);
		goto release_fail;
	}
	spin_lock(&qdev->release_idr_lock);
	idr_ret = idr_get_new_above(&qdev->release_idr, release, 1, &handle);
	spin_unlock(&qdev->release_idr_lock);
	if (idr_ret == -EAGAIN)
		goto again;
	if (ret)
		*ret = release;
	QXL_INFO(qdev, "allocated release %lld\n", handle);
	release->id = handle;
release_fail:

	return handle;
}

void
qxl_release_free(struct qxl_device *qdev,
		 struct drm_qxl_release *release)
{
	int i;

	QXL_INFO(qdev, "release %d, type %d, %d bos\n", release->id,
		 release->type, release->bo_count);
	for (i = 0 ; i < release->bo_count; ++i) {
		QXL_INFO(qdev, "release %llx\n",
			release->bos[i]->tbo.addr_space_offset
						- DRM_FILE_OFFSET);
		qxl_fence_remove_release(&release->bos[i]->fence, release->id);
		qxl_bo_unref(&release->bos[i]);
	}
	spin_lock(&qdev->release_idr_lock);
	idr_remove(&qdev->release_idr, release->id);
	spin_unlock(&qdev->release_idr_lock);
	kfree(release);
}

void
qxl_release_add_res(struct qxl_device *qdev, struct drm_qxl_release *release,
		    struct qxl_bo *bo)
{
	int i;
	for (i = 0; i < release->bo_count; i++)
		if (release->bos[i] == bo)
			return;

	if (release->bo_count >= QXL_MAX_RES) {
		DRM_ERROR("exceeded max resource on a drm_qxl_release item\n");
		return;
	}
	release->bos[release->bo_count++] = bo;
}

int qxl_release_bo_alloc(struct qxl_device *qdev,
			 struct qxl_bo **bo)
{
	int ret;
	ret = qxl_bo_create(qdev, PAGE_SIZE, false, QXL_GEM_DOMAIN_VRAM, NULL,
			    bo);
	return ret;
}

int qxl_alloc_release_reserved(struct qxl_device *qdev, unsigned long size,
			       int type, struct drm_qxl_release **release,
			       struct qxl_bo **rbo)
{
	struct qxl_bo *bo;
	int idr_ret;
	void *ptr;
	int ret;
	union qxl_release_info *info;
	int cur_idx;

	if (type == QXL_RELEASE_DRAWABLE)
		cur_idx = 0;
	else if (type == QXL_RELEASE_SURFACE_CMD)
		cur_idx = 1;
	else {
		DRM_ERROR("got illegal type: %d\n", type);
		return -EINVAL;
	}

	idr_ret = qxl_release_alloc(qdev, type, release);

	mutex_lock(&qdev->release_mutex);
	if (qdev->current_release_bo_offset[cur_idx] + 1 >= RELEASES_PER_BO) {
		qxl_bo_unref(&qdev->current_release_bo[cur_idx]);
		qdev->current_release_bo_offset[cur_idx] = 0;
		qdev->current_release_bo[cur_idx] = NULL;
	}
	if (!qdev->current_release_bo[cur_idx]) {
		ret = qxl_release_bo_alloc(qdev, &qdev->current_release_bo[cur_idx]);
		if (ret) {
			mutex_unlock(&qdev->release_mutex);
			return ret;
		}
	}

	bo = qxl_bo_ref(qdev->current_release_bo[cur_idx]);
	ret = qxl_bo_reserve(bo, false);
	if (ret) {
		mutex_unlock(&qdev->release_mutex);
		goto out_unref;
	}

	(*release)->release_offset = qdev->current_release_bo_offset[cur_idx] * 256;
	qdev->current_release_bo_offset[cur_idx]++;

	*rbo = bo;

	mutex_unlock(&qdev->release_mutex);	

	ptr = qxl_bo_kmap_atomic_page(qdev, bo, 0);
	info = ptr + (*release)->release_offset;
	info->id = idr_ret;	
	qxl_bo_kunmap_atomic_page(qdev, bo, ptr);

	qxl_release_add_res(qdev, *release, bo);


	return 0;
out_unref:
	qxl_bo_unref(&bo);
	return ret;
}

void *qxl_alloc_releasable(struct qxl_device *qdev, unsigned long size,
			   int type, struct drm_qxl_release **release,
			   struct qxl_bo **bo)
{
	int idr_ret;
	void *ret;

	idr_ret = qxl_release_alloc(qdev, type, release);
	ret = qxl_allocnf(qdev, size, *release);
	((union qxl_release_info *)ret)->id = idr_ret;
	*bo = (*release)->bos[(*release)->bo_count - 1];
	return ret;
}

int qxl_fence_releaseable(struct qxl_device *qdev,
			  struct drm_qxl_release *release)
{
	int i, ret;
	for (i = 0; i < release->bo_count; i++) {
		if (!release->bos[i]->tbo.sync_obj)
			release->bos[i]->tbo.sync_obj = &release->bos[i]->fence;
		ret = qxl_fence_add_release(&release->bos[i]->fence, release->id);
		if (ret)
			return ret;
	}
	return 0;
}

struct drm_qxl_release *qxl_release_from_id_locked(struct qxl_device *qdev,
						   uint64_t id)
{
	struct drm_qxl_release *release;

	spin_lock(&qdev->release_idr_lock);
	release = idr_find(&qdev->release_idr, id);
	spin_unlock(&qdev->release_idr_lock);
	if (!release) {
		DRM_ERROR("failed to find id in release_idr\n");
		return NULL;
	}
	if (release->bo_count < 1) {
		DRM_ERROR("read a released resource with 0 bos\n");
		return NULL;
	}
	return release;
}
