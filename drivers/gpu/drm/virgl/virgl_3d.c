#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include "virgl_drv.h"
#include "virgl_object.h"

/* virtio config->get_features() implementation */
static u32 vp_get_features(struct virtio_device *vdev)
{
	struct virgl_device *qdev = vdev_to_virgl_dev(vdev);
	u32 ret;
	ret =  ioread32(qdev->ioaddr + VIRTIO_PCI_HOST_FEATURES);
	/* When someone needs more than 32 feature bits, we'll need to
	 * steal a bit to indicate that the rest are somewhere else. */
	return ret;
}

/* virtio config->finalize_features() implementation */
static void vp_finalize_features(struct virtio_device *vdev)
{
	struct virgl_device *qdev = vdev_to_virgl_dev(vdev);

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	/* We only support 32 feature bits. */
	BUILD_BUG_ON(ARRAY_SIZE(vdev->features) != 1);
	iowrite32(vdev->features[0], qdev->ioaddr+VIRTIO_PCI_GUEST_FEATURES);
}

/* virtio config->get() implementation */
static void vp_get(struct virtio_device *vdev, unsigned offset,
		   void *buf, unsigned len)
{
	struct virgl_device *qdev = vdev_to_virgl_dev(vdev);
	void __iomem *ioaddr = qdev->ioaddr +
				VIRTIO_PCI_CONFIG(qdev->pdev) + offset;
	u8 *ptr = buf;
	int i;

	for (i = 0; i < len; i++)
		ptr[i] = ioread8(ioaddr + i);
}

/* the config->set() implementation.  it's symmetric to the config->get()
 * implementation */
static void vp_set(struct virtio_device *vdev, unsigned offset,
		   const void *buf, unsigned len)
{
	struct virgl_device *qdev = vdev_to_virgl_dev(vdev);
	void __iomem *ioaddr = qdev->ioaddr +
				VIRTIO_PCI_CONFIG(qdev->pdev) + offset;
	const u8 *ptr = buf;
	int i;

	for (i = 0; i < len; i++)
		iowrite8(ptr[i], ioaddr + i);
}

/* config->{get,set}_status() implementations */
static u8 vp_get_status(struct virtio_device *vdev)
{
	struct virgl_device *qdev = vdev_to_virgl_dev(vdev);
	return ioread8(qdev->ioaddr + VIRTIO_PCI_STATUS);
}

static void vp_set_status(struct virtio_device *vdev, u8 status)
{
	struct virgl_device *qdev = vdev_to_virgl_dev(vdev);
	/* We should never be setting status to 0. */
	BUG_ON(status == 0);
	iowrite8(status, qdev->ioaddr + VIRTIO_PCI_STATUS);
}

static void vp_reset(struct virtio_device *vdev)
{
	struct virgl_device *qdev = vdev_to_virgl_dev(vdev);

	/* 0 status means a reset. */
	iowrite8(0, qdev->ioaddr + VIRTIO_PCI_STATUS);
	ioread8(qdev->ioaddr + VIRTIO_PCI_STATUS);
}

static const struct virtio_config_ops virtio_virgl_config_ops = {
	.get		= vp_get,
	.set		= vp_set,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	 .reset		= vp_reset,
	 //	.find_vqs	= vp_find_vqs,
	 //	.del_vqs	= vp_del_vqs,
	.get_features	= vp_get_features,
	.finalize_features = vp_finalize_features,
	 //	.bus_name	= vp_bus_name,
	 //	.set_vq_affinity = vp_set_vq_affinity,
};

static irqreturn_t vp_vring_interrupt(int irq, struct virgl_device *qdev)
{
	irqreturn_t ret = IRQ_NONE;

        if (vring_interrupt(irq, qdev->cmdq) == IRQ_HANDLED) {
		ret = IRQ_HANDLED;
	}
	return ret;
}

irqreturn_t virgl_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct virgl_device *qdev = (struct virgl_device *)dev->dev_private;
	int retval = IRQ_NONE;
	u8 isr;
	
virt_retry:
	/* virt io first */
	isr = ioread8(qdev->ioaddr + VIRTIO_PCI_ISR);
	if (!isr)
		return retval;
	if (isr & 0x1) {
		atomic_inc(&qdev->irq_count_vbuf);
		retval = vp_vring_interrupt(irq, qdev);
	}
	if (isr & 0x20) {
		atomic_inc(&qdev->irq_count_fence);
		virgl_fence_process(qdev);
		retval = IRQ_HANDLED;
	}
	goto virt_retry;
}


int virgl_resource_id_get(struct virgl_device *qdev, uint32_t *resid)
{
	int handle;
	int idr_ret = -ENOMEM;
again:
	if (idr_pre_get(&qdev->resource_idr, GFP_KERNEL) == 0) {
		goto fail;
	}
	spin_lock(&qdev->resource_idr_lock);
	idr_ret = idr_get_new_above(&qdev->resource_idr, NULL, 1, &handle);
	spin_unlock(&qdev->resource_idr_lock);
	if (idr_ret == -EAGAIN)
		goto again;

	*resid = handle;
fail:
	return idr_ret;
}

u32 virgl_fence_read(struct virgl_device *qdev)
{
	return ioread32(qdev->ioaddr + 20);
}

void virgl_virtio_fini(struct virgl_device *qdev)
{
}


/* the notify function used when creating a virt queue */
static void vp_notify(struct virtqueue *vq)
{
	struct virgl_device *qdev = vdev_to_virgl_dev(vq->vdev);

	/* we write the queue's selector into the notification register to
	 * signal the other end */
	iowrite16(vq->index, qdev->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY);
}

static struct virtqueue *setup_cmdq(struct virgl_device *qdev,
				    void (*callback)(struct virtqueue *vq))
{
	struct virtqueue *vq;
	u16 num;
	unsigned long size;

	spin_lock_init(&qdev->cmdq_lock);

	iowrite16(0, qdev->ioaddr + VIRTIO_PCI_QUEUE_SEL);
	num = ioread16(qdev->ioaddr + VIRTIO_PCI_QUEUE_NUM);
	if (!num || ioread32(qdev->ioaddr + VIRTIO_PCI_QUEUE_PFN)) {
		printk("queue setup failed %d\n", num);
		return ERR_PTR(-ENOENT);
	}

	size = PAGE_ALIGN(vring_size(num, VIRTIO_PCI_VRING_ALIGN));
	qdev->cmdqueue = alloc_pages_exact(size, GFP_KERNEL|__GFP_ZERO);
	if (qdev->cmdqueue == NULL) {
		return ERR_PTR(-ENOMEM);
	}

	iowrite32(virt_to_phys(qdev->cmdqueue) >> VIRTIO_PCI_QUEUE_ADDR_SHIFT, qdev->ioaddr + VIRTIO_PCI_QUEUE_PFN);

	vq = vring_new_virtqueue(0, num, VIRTIO_PCI_VRING_ALIGN, &qdev->vdev,
				 true, qdev->cmdqueue, vp_notify,
				 callback, "virglvirt");
	if (!vq) {
		printk("new virtqueue failed\n");
		return ERR_PTR(-ENOMEM);
	}

	DRM_INFO("virtqueue setup %d\n", num);
	return vq;
}

static void qdev_virq_cb(struct virtqueue *vq)
{
	struct virgl_device *qdev = vdev_to_virgl_dev(vq->vdev);

	schedule_work(&qdev->dequeue_work);
}

static void free_vbuf(struct virgl_vbuffer *vbuf)
{
	if (vbuf->bo)
		virgl_bo_unref(&vbuf->bo);

	kfree(vbuf);
}

struct virgl_vbuffer *allocate_vbuf(struct virgl_device *qdev,
				     struct virgl_bo *bo,
				     int size, bool inout, u32 *base_offset, u32 max_bo_len)
{
	struct virgl_vbuffer *vbuf;
	int sgpages = bo ? bo->tbo.num_pages : 0;
	int ret;

	sgpages++;
	
	vbuf = kmalloc(sizeof(*vbuf) + sizeof(struct scatterlist) * sgpages + size, GFP_KERNEL);
	if (!vbuf)
		goto fail;

	vbuf->buf = (void *)vbuf + sizeof(*vbuf) + sizeof(struct scatterlist) * sgpages;
	vbuf->inout = inout;
	vbuf->sgpages = sgpages;
	vbuf->size = size;

	if (bo) {
		struct scatterlist *sg;
		unsigned int i;
		uint32_t offset = 0;
		vbuf->bo = virgl_bo_ref(bo);
		vbuf->bo_max_len = max_bo_len;
		/* TODO work out offset */
		ret = virgl_bo_get_sg_table(qdev, bo);
		if (ret || !bo->sgt) {
			DRM_ERROR("failed to allocate sgt %d\n", ret);
			goto fail;
		}

		if (base_offset) {
			vbuf->bo_start_offset = *base_offset;

			/* find sgt offset */
			/* work out how far into the SG mapping we actually need to send */
			offset = bo->sgt->sgl->offset;
			for_each_sg(bo->sgt->sgl, sg, bo->sgt->nents, i) {
				if (offset + sg->length > vbuf->bo_start_offset)
					break;
				offset += sg->length;
			}
			if (i > bo->sgt->nents)
				return ERR_PTR(-EINVAL);
			*base_offset -= offset;
			vbuf->bo_user_offset = *base_offset;
			vbuf->bo_start_offset = offset;
		} else
			vbuf->bo_start_offset = 0;
	} else {
		vbuf->bo = NULL;
		vbuf->bo_max_len = 0;
		vbuf->bo_start_offset = 0;
	}
	return vbuf;
fail:
	kfree(vbuf);
	return ERR_PTR(-ENOMEM);
}

struct virgl_command *virgl_alloc_cmd_buf(struct virgl_device *qdev,
					     struct virgl_bo *qobj,
					     bool inout,
					     u32 *base_offset,
					     u32 max_bo_len,
					     struct virgl_vbuffer **vbuffer_p)
{
	struct virgl_vbuffer *vbuf;

	vbuf = allocate_vbuf(qdev, qobj, sizeof(struct virgl_command), inout, base_offset, max_bo_len);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virgl_command *)vbuf->buf;
}

static int reclaim_vbufs(struct virtqueue *vq)
{
	struct virgl_vbuffer *vbuf;
	unsigned int len;
	int freed = 0;
	while ((vbuf = virtqueue_get_buf(vq, &len))) {
		free_vbuf(vbuf);
		freed++;
	}
	return freed;
}

void virgl_dequeue_work_func(struct work_struct *work)
{
	struct virgl_device *qdev = container_of(work, struct virgl_device,
					       dequeue_work);
	int ret;
	spin_lock(&qdev->cmdq_lock);
	do {
		virtqueue_disable_cb(qdev->cmdq);
		ret = reclaim_vbufs(qdev->cmdq);
		if (ret == 0)
			printk("cleaned 0 buffers wierd\n");
		qdev->num_freed += ret;
	} while (!virtqueue_enable_cb(qdev->cmdq));
	spin_unlock(&qdev->cmdq_lock);
	wake_up(&qdev->cmd_ack_queue);
}

static void virt_map_sgt(struct scatterlist *sg, struct virgl_vbuffer *buf,
			 unsigned int *p_idx)
{
	struct scatterlist *sg_elem;
	int i;
	int idx = *p_idx;
	uint32_t offset = buf->bo->sgt->sgl->offset;
	for_each_sg(buf->bo->sgt->sgl, sg_elem, buf->bo->sgt->nents, i) {
		if (offset + sg_elem->length <= buf->bo_start_offset)
			goto skip;
		sg[idx++] = *sg_elem;
		if (buf->bo_max_len)
			if (offset + sg_elem->length > buf->bo_start_offset + buf->bo_user_offset + buf->bo_max_len)
				break;
	skip:
		offset += sg_elem->length;
	}

	*p_idx = idx;
}

int virgl_queue_cmd_buf(struct virgl_device *qdev,
			struct virgl_vbuffer *buf)
{
	struct virtqueue *vq = qdev->cmdq;
	struct scatterlist *sg = buf->sg;
	int ret;
	int idx = 0, outcnt = 0, incnt = 0, old_idx;

	/* always put the command into in */
	sg_set_buf(&sg[idx++], buf->buf, buf->size);
	if (buf->inout == true)
		outcnt = idx;
	if (buf->bo) {
		old_idx = idx;
		virt_map_sgt(sg, buf, &idx);
	}
	if (buf->inout == true)
		incnt = idx - outcnt;
	else
		outcnt = idx;

	spin_lock(&qdev->cmdq_lock);
 retry:
	ret = virtqueue_add_buf(vq, sg, outcnt, incnt, buf, GFP_ATOMIC);
	if (ret == -ENOSPC) {
		virtqueue_kick(vq);
		spin_unlock(&qdev->cmdq_lock);
		wait_event(qdev->cmd_ack_queue, vq->num_free);
		spin_lock(&qdev->cmdq_lock);
		goto retry;
	} else {
		qdev->num_alloc++;
		virtqueue_kick(vq);
	}

	spin_unlock(&qdev->cmdq_lock);

	if (!ret)
		ret = vq->num_free;
	
	return ret;
}

static void virtio_virgl_release_dev(struct device *_d)
{
	/*
	 * No need for a release method as we allocate/free
	 * all devices together with the pci devices.
	 * Provide an empty one to avoid getting a warning from core.
	 */
}

static int virgl_init_vring(struct virgl_device *qdev)
{
	struct virgl_vbuffer *tbuf;
	uint32_t *dwp;
	qdev->cmdq = setup_cmdq(qdev, qdev_virq_cb);
	printk("cmdq at %p\n", qdev->cmdq);

	tbuf = allocate_vbuf(qdev, NULL, sizeof(struct virgl_command), false, 0, 0);

	dwp = (uint32_t *)tbuf->buf;
	dwp[0] = 0xdeadbeef;

	virgl_queue_cmd_buf(qdev, tbuf);
	return 0;
}

static int virgl_virtio_probe(struct virtio_device *vdev)
{
	printk("%s\n", __func__);
	return 0;
}
static void virgl_virtio_remove(struct virtio_device *vdev)
{
	printk("%s\n", __func__);
}

static void virgl_virtio_config_changed(struct virtio_device *vdev)
{

}

#ifdef CONFIG_PM
static int virgl_virtio_freeze(struct virtio_device *vdev)
{
	return 0;
}

static int virgl_virtio_restore(struct virtio_device *vdev)
{
	return 0;
}
#endif

static struct virtio_device_id id_table[] = {
	{ 0x3d, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver virgl_virtio_driver = {
	.feature_table = NULL,
	.feature_table_size = 0,
	.id_table = id_table,
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.feature_table_size = 0,
	.probe = virgl_virtio_probe,
	.remove = virgl_virtio_remove,
	.config_changed = virgl_virtio_config_changed,
#ifdef CONFIG_PM
	.freeze = virgl_virtio_freeze,
	.restore = virgl_virtio_restore,
#endif
};
	
int virgl_virtio_init(struct virgl_device *qdev)
{
	/* create an object for the 3D ring and pin it at 0. */
	int ret;

	qdev->vdev.dev.parent = &qdev->pdev->dev;
	qdev->vdev.dev.release = virtio_virgl_release_dev;
	qdev->vdev.config = &virtio_virgl_config_ops;
	qdev->vdev.id.vendor = 0x1af4;
	qdev->vdev.id.device = 0x3d;
	register_virtio_driver(&virgl_virtio_driver);

	pci_msi_off(qdev->pdev);
	pci_set_master(qdev->pdev);

	ret = register_virtio_device(&qdev->vdev);
	if (ret)
		printk("error registering virtio %d\n", ret);

	ret = virgl_init_vring(qdev);
	if (ret)
		goto fail;

	return 0;
fail:
	return ret;
}
	  
int virgl_execbuffer(struct drm_device *dev,
		     struct drm_virgl_execbuffer *execbuffer,
		     struct drm_file *drm_file)
{
	struct virgl_device *qdev = dev->dev_private;	
	struct drm_gem_object *gobj;
	struct virgl_fence *fence;
	struct virgl_bo *qobj;
	void *optr;
	void *osyncobj;
	int ret;

	//printk("user cmd size %d\n", user_cmd.command_size);

	ret = virgl_gem_object_create(qdev, execbuffer->size + 4,
				    0, 0, false,
				    true, &gobj);

	if (ret)
		return ret;

	qobj = gem_to_virgl_bo(gobj);

	ret = virgl_bo_reserve(qobj, false);
	if (ret)
		goto out_free;

	virgl_ttm_placement_from_domain(qobj, qobj->type);
	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
	if (ret)
		goto out_unresv;

	ret = virgl_bo_kmap(qobj, &optr);
	if (ret)
		goto out_unresv;

	*(uint32_t *)optr = 0;
	if (DRM_COPY_FROM_USER(optr + 4, (void *)(unsigned long)execbuffer->command,
			       execbuffer->size)) {
		ret = -EFAULT;
		goto out_kunmap;
	}

	virgl_bo_kunmap(qobj);

	{
		struct virgl_command *cmd_p;
		struct virgl_vbuffer *vbuf = NULL;

		cmd_p = virgl_alloc_cmd(qdev, qobj, false, NULL, 0, &vbuf);
		if (IS_ERR(cmd_p))
			goto out_unresv;

		cmd_p->type = VIRGL_CMD_SUBMIT;
		cmd_p->u.cmd_submit.size = execbuffer->size;

		cmd_p->u.cmd_submit.phy_addr = 0;
		ret = virgl_fence_emit(qdev, cmd_p, &fence);

		virgl_queue_cmd_buf(qdev, vbuf);
	}

	spin_lock(&qobj->tbo.glob->lru_lock);
	spin_lock(&qobj->tbo.bdev->fence_lock);
	osyncobj = qobj->tbo.sync_obj;
	qobj->tbo.sync_obj = qdev->mman.bdev.driver->sync_obj_ref(fence);

	spin_unlock(&qobj->tbo.bdev->fence_lock);
	spin_unlock(&qobj->tbo.glob->lru_lock);
	virgl_bo_unreserve(qobj);
	/* fence the command bo */
	drm_gem_object_unreference_unlocked(gobj);
	if (osyncobj)
		qdev->mman.bdev.driver->sync_obj_ref(osyncobj);
	return 0;
out_kunmap:
	virgl_bo_kunmap(qobj);
out_unresv:
	virgl_bo_unreserve(qobj);
out_free:
	drm_gem_object_unreference_unlocked(gobj);
	return ret;
}

static void virgl_fence_destroy(struct kref *kref)
{
	struct virgl_fence *fence;

	fence = container_of(kref, struct virgl_fence, kref);
	kfree(fence);
}

struct virgl_fence *virgl_fence_ref(struct virgl_fence *fence)
{
	kref_get(&fence->kref);
	return fence;
}

void virgl_fence_unref(struct virgl_fence **fence)
{
	struct virgl_fence *tmp = *fence;

	*fence = NULL;
	if (tmp) {
		kref_put(&tmp->kref, virgl_fence_destroy);
	}
}

static bool virgl_fence_seq_signaled(struct virgl_device *qdev, u64 seq)
{
	if (atomic64_read(&qdev->fence_drv.last_seq) >= seq)
		return true;

	virgl_fence_process(qdev);

	if (atomic64_read(&qdev->fence_drv.last_seq) >= seq)
		return true;
	return false;
}

static int virgl_fence_wait_seq(struct virgl_device *qdev, u64 target_seq,
				 bool intr)
{
  	unsigned long timeout, last_activity;
	uint64_t seq;
	bool signaled;
	int r;

	while (target_seq > atomic64_read(&qdev->fence_drv.last_seq)) {

		timeout = jiffies - VIRGL_FENCE_JIFFIES_TIMEOUT;
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
				(signaled = virgl_fence_seq_signaled(qdev, target_seq)),
				timeout);
                } else {
			r = wait_event_timeout(qdev->fence_queue,
				(signaled = virgl_fence_seq_signaled(qdev, target_seq)),
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

bool virgl_fence_signaled(struct virgl_fence *fence)
{
	if (!fence)
		return true;

	if (fence->seq == VIRGL_FENCE_SIGNALED_SEQ)
		return true;

	if (virgl_fence_seq_signaled(fence->qdev, fence->seq)) {
		fence->seq = VIRGL_FENCE_SIGNALED_SEQ;
		return true;
	}
	return false;
}

int virgl_fence_wait(struct virgl_fence *fence, bool intr)
{
	int r;

	if (fence == NULL)
		return -EINVAL;

	r = virgl_fence_wait_seq(fence->qdev, fence->seq,
				  intr);
	if (r)
		return r;

	fence->seq = VIRGL_FENCE_SIGNALED_SEQ;

	return 0;

}

int virgl_fence_emit(struct virgl_device *qdev,
		      struct virgl_command *cmd,
		      struct virgl_fence **fence)
{
	*fence = kmalloc(sizeof(struct virgl_fence), GFP_KERNEL);
	if ((*fence) == NULL)
		return -ENOMEM;

	kref_init(&((*fence)->kref));
	(*fence)->qdev = qdev;
	(*fence)->seq = ++qdev->fence_drv.sync_seq;

	cmd->flags |= VIRGL_COMMAND_EMIT_FENCE;
	cmd->fence_id = (*fence)->seq;

	return 0;
}
	
int virgl_3d_set_front(struct virgl_device *qdev,
		     struct virgl_framebuffer *fb, int x, int y,
		     int width, int height)
{
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;

	cmd_p = virgl_alloc_cmd(qdev, NULL, false, NULL, 0, &vbuf);
	if (IS_ERR(cmd_p))
		return PTR_ERR(cmd_p);
	cmd_p->type = VIRGL_SET_SCANOUT;
	cmd_p->u.set_scanout.res_handle = fb->res_3d_handle;
	cmd_p->u.set_scanout.box.x = x;
	cmd_p->u.set_scanout.box.y = y;
	cmd_p->u.set_scanout.box.w = width;
	cmd_p->u.set_scanout.box.h = height;
	virgl_queue_cmd_buf(qdev, vbuf);
	return 0;
}

int virgl_3d_dirty_front(struct virgl_device *qdev,
		       struct virgl_framebuffer *fb, int x, int y,
		       int width, int height)
{
	struct virgl_command *cmd_p;
	struct virgl_vbuffer *vbuf;

	cmd_p = virgl_alloc_cmd(qdev, NULL, false, NULL, 0, &vbuf);
	if (IS_ERR(cmd_p))
		return PTR_ERR(cmd_p);
	cmd_p->type = VIRGL_FLUSH_BUFFER;
	cmd_p->u.flush_buffer.res_handle = fb->res_3d_handle;
	cmd_p->u.flush_buffer.box.x = x;
	cmd_p->u.flush_buffer.box.y = y;
	cmd_p->u.flush_buffer.box.w = width;
	cmd_p->u.flush_buffer.box.h = height;
	virgl_queue_cmd_buf(qdev, vbuf);
	return 0;
}


void virgl_fence_process(struct virgl_device *qdev)
{
	bool wake = false;
	u64 last_seq, last_emitted, seq;
	unsigned count_loop = 0;

	last_seq = atomic64_read(&qdev->fence_drv.last_seq);
	do {
		last_emitted = qdev->fence_drv.sync_seq;
		seq = virgl_fence_read(qdev);
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
	} while (atomic64_xchg(&qdev->fence_drv.last_seq, seq) > seq);

	if (wake) {
		qdev->fence_drv.last_activity = jiffies;
		wake_up_all(&qdev->fence_queue);
	}
	
}

int virgl_wait(struct virgl_bo *bo, bool no_wait)
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


int virgl_irq_init(struct virgl_device *qdev)
{
	int ret;

	atomic_set(&qdev->irq_count_vbuf, 0);
	atomic_set(&qdev->irq_count_fence, 0);

	ret = drm_irq_install(qdev->ddev);
	if (unlikely(ret != 0)) {
                DRM_ERROR("Failed installing irq: %d\n", ret);
                return 1;
        }
        return 0;

}
