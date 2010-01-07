
static inline int qxl_bo_reserve(struct qxl_bo *bo, bool no_wait)
{
	int r;

	r = ttm_bo_reserve(&bo->tbo, true, no_wait, false, 0);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			dev_err(bo->qdev->dev, "%p reserve failed\n", bo);
		return r;
	}
	return 0;
}

static inline void qxl_bo_unreserve(struct qxl_bo *bo)
{
	ttm_bo_unreserve(&bo->tbo);
}

static inline u64 qxl_bo_gpu_offset(struct qxl_bo *bo)
{
	return bo->tbo.offset;
}

static inline unsigned long qxl_bo_size(struct qxl_bo *bo)
{
	return bo->tbo.num_pages << PAGE_SHIFT;
}

static inline bool qxl_bo_is_reserved(struct qxl_bo *bo)
{
	return !!atomic_read(&bo->tbo.reserved);
}

static inline u64 qxl_bo_mmap_offset(struct qxl_bo *bo)
{
	return bo->tbo.addr_space_offset;
}

static inline int qxl_bo_wait(struct qxl_bo *bo, u32 *mem_type,
			      bool no_wait)
{
	int r;

	r = ttm_bo_reserve(&bo->tbo, true, no_wait, false, 0);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			dev_err(bo->qdev->dev, "%p reserve failed for wait\n", bo);
		return r;
	}
	spin_lock(&bo->tbo.lock);
	if (mem_type)
		*mem_type = bo->tbo.mem.mem_type;
	if (bo->tbo.sync_obj)
		r = ttm_bo_wait(&bo->tbo, true, true, no_wait);
	spin_unlock(&bo->tbo.lock);
	ttm_bo_unreserve(&bo->tbo);
	return r;
}

extern int qxl_bo_create(struct qxl_device *qdev,
			    struct drm_gem_object *gobj, unsigned long size,
			    bool kernel, u32 domain,
			    struct qxl_bo **bo_ptr);
extern int qxl_bo_kmap(struct qxl_bo *bo, void **ptr);
extern void qxl_bo_kunmap(struct qxl_bo *bo);
extern void qxl_bo_unref(struct qxl_bo **bo);
extern int qxl_bo_pin(struct qxl_bo *bo, u32 domain, u64 *gpu_addr);
extern int qxl_bo_unpin(struct qxl_bo *bo);
extern void qxl_ttm_placement_from_domain(struct qxl_bo *qbo, u32 domain);
extern bool qxl_ttm_bo_is_qxl_bo(struct ttm_buffer_object *bo);
