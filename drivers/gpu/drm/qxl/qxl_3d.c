
#include "qxl_drv.h"
#include "qxl_object.h"

void qxl_3d_irq_set_mask(struct qxl_device *qdev)
{
	uint32_t *rmap = qdev->regs_3d_map;
	rmap[0] = 0x5;
}

void qxl_3d_ping(struct qxl_device *qdev)
{
	uint32_t *rmap = qdev->regs_3d_map;
	rmap[4] = 0x1;
	rmap[5] = 0x1;
}

irqreturn_t qxl_3d_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct qxl_device *qdev = (struct qxl_device *)dev->dev_private;
	uint32_t *rmap = qdev->regs_3d_map;
	uint32_t pending;
	uint32_t val;
	pending = rmap[1];
	if (pending) {
		rmap[1] = 0;
		val = xchg(&qdev->q3d_info.ram_3d_header->pad, 0);
	
		atomic_inc(&qdev->irq_received_3d);
		printk("got 3d irq %08x %08x\n", pending, val);
		if (val & 0x1) {
			atomic_inc(&qdev->irq_received_3d);
			wake_up_all(&qdev->q3d_event);
		}
		if (val & 0x4) {
			qxl_3d_fence_process(qdev);
		}
		qxl_3d_irq_set_mask(qdev);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}


int qxl_3d_resource_id_get(struct qxl_device *qdev, uint32_t *resid)
{
	int handle;
	int idr_ret = -ENOMEM;
again:
	if (idr_pre_get(&qdev->q3d_info.resource_idr, GFP_KERNEL) == 0) {
		goto fail;
	}
	spin_lock(&qdev->q3d_info.resource_idr_lock);
	idr_ret = idr_get_new_above(&qdev->q3d_info.resource_idr, NULL, 1, &handle);
	spin_unlock(&qdev->q3d_info.resource_idr_lock);
	if (idr_ret == -EAGAIN)
		goto again;

	*resid = handle;
fail:
	return idr_ret;
}

static u32 qxl_3d_fence_read(struct qxl_device *qdev)
{
	return qdev->q3d_info.ram_3d_header->last_fence;
}

void qxl_fini_3d(struct qxl_device *qdev)
{
	free_irq(qdev->ivdev->irq, qdev->ddev);
	qxl_ring_free(qdev->q3d_info.iv3d_ring);

	qxl_bo_unref(&qdev->q3d_info.ringbo);
}

int qxl_init_3d(struct qxl_device *qdev)
{
	/* create an object for the 3D ring and pin it at 0. */
	int ret;

	ret = request_irq(qdev->ivdev->irq, qxl_3d_irq_handler, IRQF_SHARED,
			  "qxl3d", qdev->ddev);
	if (ret < 0) {
		DRM_INFO("failed to setup 3D irq handler\n");
	}

	init_waitqueue_head(&qdev->q3d_info.fence_queue);
	idr_init(&qdev->q3d_info.resource_idr);
	spin_lock_init(&qdev->q3d_info.resource_idr_lock);

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
					  sizeof(struct qxl_3d_command),
					  QXL_3D_COMMAND_RING_SIZE,
					  -1,
					  false,
						   &qdev->q3d_event, qdev->regs_3d_map + 12);

	DRM_INFO("3d version is %d %d\n", qdev->q3d_info.ram_3d_header->version, sizeof(struct qxl_3d_command));

	/* push something misc into the ring */
	{
		struct qxl_3d_command cmd;
		cmd.type = 0xdeadbeef;
		qxl_ring_push(qdev->q3d_info.iv3d_ring, &cmd, false);
	}

	qxl_bo_unreserve(qdev->q3d_info.ringbo);

	qxl_3d_irq_set_mask(qdev);
	return 0;
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
	struct qxl_device *qdev = dev->dev_private;	
	struct drm_qxl_command *commands =
		(struct drm_qxl_command *)execbuffer->commands;
	struct drm_qxl_command user_cmd;
	struct drm_gem_object *gobj;
	struct qxl_3d_fence *fence;
	struct qxl_bo *qobj;
	void *optr;
	void *osyncobj;
	int ret;

	if (execbuffer->commands_num > 1)
		return -EINVAL;

	if (DRM_COPY_FROM_USER(&user_cmd, &commands[0],
			       sizeof(user_cmd)))
		return -EFAULT;
	/* allocate a command bo */
	
	printk("user cmd size %d\n", user_cmd.command_size);

	ret = qxl_gem_object_create(qdev, user_cmd.command_size,
				    0, QXL_GEM_DOMAIN_3D, false,
				    true, NULL, &gobj);

	if (ret)
		return ret;

	qobj = gem_to_qxl_bo(gobj);

	ret = qxl_bo_reserve(qobj, false);
	if (ret)
		goto out_free;

	ret = qxl_bo_pin(qobj, QXL_GEM_DOMAIN_3D, NULL);
	if (ret)
		goto out_unresv;

	ret = qxl_bo_kmap(qobj, &optr);
	if (ret)
		goto out_unresv;

	if (DRM_COPY_FROM_USER(optr, (void *)(unsigned long)user_cmd.command,
			       user_cmd.command_size)) {
		ret = -EFAULT;
		goto out_kunmap;
	}

	qxl_bo_kunmap(qobj);

	{
		struct qxl_3d_command cmd;
		cmd.type = QXL_3D_CMD_SUBMIT;
		cmd.u.cmd_submit.phy_addr = qxl_3d_bo_addr(qobj, 0);
		cmd.u.cmd_submit.size = user_cmd.command_size;
		qxl_ring_push(qdev->q3d_info.iv3d_ring, &cmd, true);
	}


	ret = qxl_3d_fence_emit(qdev, &fence);

	spin_lock(&qobj->tbo.glob->lru_lock);
	spin_lock(&qobj->tbo.bdev->fence_lock);
	osyncobj = qobj->tbo.sync_obj;
	qobj->tbo.sync_obj = qdev->mman.bdev.driver->sync_obj_ref(fence);

	spin_unlock(&qobj->tbo.bdev->fence_lock);
	spin_unlock(&qobj->tbo.glob->lru_lock);
	qxl_bo_unreserve(qobj);
	/* fence the command bo */
	drm_gem_object_unreference_unlocked(gobj);
	if (osyncobj)
		qdev->mman.bdev.driver->sync_obj_ref(osyncobj);
	return 0;
out_kunmap:
	qxl_bo_kunmap(qobj);
out_unresv:
	qxl_bo_unreserve(qobj);
out_free:
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
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
  	unsigned long timeout, last_activity;
	uint64_t seq;
	unsigned i;
	bool signaled;
	int r;

	while (target_seq > atomic64_read(&qdev->q3d_info.fence_drv.last_seq)) {

		timeout = jiffies - QXL_3D_FENCE_JIFFIES_TIMEOUT;
		if (time_after(qdev->q3d_info.fence_drv.last_activity, timeout)) {
			/* the normal case, timeout is somewhere before last_activity */
			timeout = qdev->q3d_info.fence_drv.last_activity - timeout;
		} else {
			/* either jiffies wrapped around, or no fence was signaled in the last 500ms
			 * anyway we will just wait for the minimum amount and then check for a lockup
			 */
			timeout = 1;
		}
		seq = atomic64_read(&qdev->q3d_info.fence_drv.last_seq);
		/* Save current last activity valuee, used to check for GPU lockups */
		last_activity = qdev->q3d_info.fence_drv.last_activity;

		//		radeon_irq_kms_sw_irq_get(rdev, ring);
		if (intr) {
			r = wait_event_interruptible_timeout(qdev->q3d_info.fence_queue,
				(signaled = qxl_3d_fence_seq_signaled(qdev, target_seq)),
				timeout);
                } else {
			r = wait_event_timeout(qdev->q3d_info.fence_queue,
				(signaled = qxl_3d_fence_seq_signaled(qdev, target_seq)),
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
			if (seq != atomic64_read(&qdev->q3d_info.fence_drv.last_seq)) {
				continue;
			}

			/* test if somebody else has already decided that this is a lockup */
			if (last_activity != qdev->q3d_info.fence_drv.last_activity) {
				continue;
			}

		}
	}
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
	(*fence)->type = 1;
	(*fence)->seq = ++qdev->q3d_info.fence_drv.sync_seq;

	{
	  struct qxl_3d_command cmd;
	  memset(&cmd, 0, sizeof(cmd));
	  cmd.type = QXL_3D_FENCE;
	  cmd.u.fence_id = (*fence)->seq;
	  qxl_ring_push(qdev->q3d_info.iv3d_ring, &cmd, true);
	}

	return 0;
}
	
int qxl_3d_set_front(struct qxl_device *qdev,
		     struct qxl_framebuffer *fb, int x, int y,
		     int width, int height)
{
	struct qxl_3d_command cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.type = QXL_3D_SET_SCANOUT;
	cmd.u.set_scanout.res_handle = fb->res_3d_handle;
	cmd.u.set_scanout.box.x = x;
	cmd.u.set_scanout.box.y = y;
	cmd.u.set_scanout.box.w = width;
	cmd.u.set_scanout.box.h = height;
	qxl_ring_push(qdev->q3d_info.iv3d_ring, &cmd, true);
}

int qxl_3d_dirty_front(struct qxl_device *qdev,
		       struct qxl_framebuffer *fb, int x, int y,
		       int width, int height)
{
	struct qxl_3d_command cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.type = QXL_3D_FLUSH_BUFFER;
	cmd.u.flush_buffer.res_handle = fb->res_3d_handle;
	cmd.u.flush_buffer.box.x = x;
	cmd.u.flush_buffer.box.y = y;
	cmd.u.flush_buffer.box.w = width;
	cmd.u.flush_buffer.box.h = height;
	qxl_ring_push(qdev->q3d_info.iv3d_ring, &cmd, false);

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

int qxl_3d_wait(struct qxl_bo *bo, bool no_wait)
{
	int r;

	r = ttm_bo_reserve(&bo->tbo, true, no_wait, false, 0);
	if (unlikely(r != 0))
		return r;
	spin_lock(&bo->tbo.bdev->fence_lock);
	if (bo->tbo.sync_obj)
		r = ttm_bo_wait(&bo->tbo, true, true, no_wait);
	spin_unlock(&bo->tbo.bdev->fence_lock);
	ttm_bo_unreserve(&bo->tbo);
	return r;
}


