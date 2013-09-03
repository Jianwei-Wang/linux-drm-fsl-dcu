/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alon Levy
 */
#ifndef VIRGL_OBJECT_H
#define VIRGL_OBJECT_H

#include "virgl_drv.h"

static inline int virgl_bo_reserve(struct virgl_bo *bo, bool no_wait)
{
	int r;

	r = ttm_bo_reserve(&bo->tbo, true, no_wait, false, 0);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS) {
			struct virgl_device *qdev = (struct virgl_device *)bo->gem_base.dev->dev_private;
			dev_err(qdev->dev, "%p reserve failed\n", bo);
		}
		return r;
	}
	return 0;
}

static inline void virgl_bo_unreserve(struct virgl_bo *bo)
{
	ttm_bo_unreserve(&bo->tbo);
}

static inline u64 virgl_bo_gpu_offset(struct virgl_bo *bo)
{
	return bo->tbo.offset;
}

static inline unsigned long virgl_bo_size(struct virgl_bo *bo)
{
	return bo->tbo.num_pages << PAGE_SHIFT;
}

static inline u64 virgl_bo_mmap_offset(struct virgl_bo *bo)
{
	return bo->tbo.addr_space_offset;
}

extern int virgl_bo_create(struct virgl_device *qdev,
			 unsigned long size,
			 bool kernel, u32 domain,
			 struct virgl_bo **bo_ptr);
extern int virgl_bo_kmap(struct virgl_bo *bo, void **ptr);
extern void virgl_bo_kunmap(struct virgl_bo *bo);
extern struct virgl_bo *virgl_bo_ref(struct virgl_bo *bo);
extern void virgl_bo_unref(struct virgl_bo **bo);
extern int virgl_bo_pin(struct virgl_bo *bo, u32 domain, u64 *gpu_addr);
extern int virgl_bo_unpin(struct virgl_bo *bo);
extern void virgl_ttm_placement_from_domain(struct virgl_bo *qbo, u32 domain);
extern bool virgl_ttm_bo_is_virgl_bo(struct ttm_buffer_object *bo);

int virgl_bo_get_sg_table(struct virgl_device *qdev,
		     struct virgl_bo *bo);
void virgl_bo_free_sg_table(struct virgl_bo *bo);
#endif
