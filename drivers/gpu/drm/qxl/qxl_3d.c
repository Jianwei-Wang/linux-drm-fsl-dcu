#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include "qxl_drv.h"
#include "qxl_object.h"

extern int qxl_3d_use_vring;
/* virtio config->get_features() implementation */
static u32 vp_get_features(struct virtio_device *vdev)
{
	struct qxl_device *qdev = vdev_to_qxl_dev(vdev);
	u32 ret;
	ret =  ioread32(qdev->q3d_info.ioaddr + VIRTIO_PCI_HOST_FEATURES);
	/* When someone needs more than 32 feature bits, we'll need to
	 * steal a bit to indicate that the rest are somewhere else. */
	return ret;
}

/* virtio config->finalize_features() implementation */
static void vp_finalize_features(struct virtio_device *vdev)
{
	struct qxl_device *qdev = vdev_to_qxl_dev(vdev);

	/* Give virtio_ring a chance to accept features. */
	vring_transport_features(vdev);

	/* We only support 32 feature bits. */
	BUILD_BUG_ON(ARRAY_SIZE(vdev->features) != 1);
	iowrite32(vdev->features[0], qdev->q3d_info.ioaddr+VIRTIO_PCI_GUEST_FEATURES);
}

/* virtio config->get() implementation */
static void vp_get(struct virtio_device *vdev, unsigned offset,
		   void *buf, unsigned len)
{
	struct qxl_device *qdev = vdev_to_qxl_dev(vdev);
	void __iomem *ioaddr = qdev->q3d_info.ioaddr +
				VIRTIO_PCI_CONFIG(qdev->ivdev) + offset;
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
	struct qxl_device *qdev = vdev_to_qxl_dev(vdev);
	void __iomem *ioaddr = qdev->q3d_info.ioaddr +
				VIRTIO_PCI_CONFIG(qdev->ivdev) + offset;
	const u8 *ptr = buf;
	int i;

	for (i = 0; i < len; i++)
		iowrite8(ptr[i], ioaddr + i);
}

/* config->{get,set}_status() implementations */
static u8 vp_get_status(struct virtio_device *vdev)
{
	struct qxl_device *qdev = vdev_to_qxl_dev(vdev);
	return ioread8(qdev->q3d_info.ioaddr + VIRTIO_PCI_STATUS);
}

static void vp_set_status(struct virtio_device *vdev, u8 status)
{
	struct qxl_device *qdev = vdev_to_qxl_dev(vdev);
	/* We should never be setting status to 0. */
	BUG_ON(status == 0);
	iowrite8(status, qdev->q3d_info.ioaddr + VIRTIO_PCI_STATUS);
}

static void vp_reset(struct virtio_device *vdev)
{
	struct qxl_device *qdev = vdev_to_qxl_dev(vdev);

	/* 0 status means a reset. */
	iowrite8(0, qdev->q3d_info.ioaddr + VIRTIO_PCI_STATUS);
	ioread8(qdev->q3d_info.ioaddr + VIRTIO_PCI_STATUS);
}

static const struct virtio_config_ops virtio_qxl_config_ops = {
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

static irqreturn_t vp_vring_interrupt(int irq, struct qxl_device *qdev)
{
	irqreturn_t ret = IRQ_NONE;

        if (vring_interrupt(irq, qdev->q3d_info.cmdq) == IRQ_HANDLED) {
		ret = IRQ_HANDLED;
	}
	return ret;
}

void qxl_3d_irq_set_mask(struct qxl_device *qdev)
{
  volatile uint32_t *rmap = qdev->regs_3d_map;
	rmap[0] = 0x5;
}

void qxl_3d_ping(struct qxl_device *qdev)
{
  volatile uint32_t *rmap = qdev->regs_3d_map;
  static uint32_t ping_val;
	rmap[4] = 0x1;
	rmap[5] = ping_val++;
}

irqreturn_t qxl_3d_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct qxl_device *qdev = (struct qxl_device *)dev->dev_private;
	volatile uint32_t *rmap = qdev->regs_3d_map;
	uint32_t pending;
	uint32_t val;
	int retval = IRQ_NONE;
	u8 isr;
	
	if (qxl_3d_use_vring) {
virt_retry:
		/* virt io first */
		isr = ioread8(qdev->q3d_info.ioaddr + VIRTIO_PCI_ISR);
		if (!isr)
			return retval;

		if (isr & 0x1)
			retval = vp_vring_interrupt(irq, qdev);
		if (isr & 0x10)
			wake_up_all(&qdev->q3d_event);
		if (isr & 0x20)
			qxl_3d_fence_process(qdev);
		retval = IRQ_HANDLED;
		goto virt_retry;
	} else {

 retry:
		pending = rmap[1];
		if (pending) {
			rmap[1] = 0;
			val = xchg(&qdev->q3d_info.ram_3d_header->pad, 0);
			atomic_inc(&qdev->irq_received_3d);
			//	printk("got 3d irq %08x %08x\n", pending, val);
			if (val & 0x1) {
				wake_up_all(&qdev->q3d_event);
			}
			if (val & 0x4) {
				qxl_3d_fence_process(qdev);
			}
			//qxl_3d_irq_set_mask(qdev);
			retval = IRQ_HANDLED;
			goto retry;
		}
	}
	return retval;
		
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

u32 qxl_3d_fence_read(struct qxl_device *qdev)
{
	if (!qxl_3d_use_vring)
		return qdev->q3d_info.ram_3d_header->last_fence;
	
	return ioread32(qdev->q3d_info.ioaddr + 20);
}

void qxl_fini_3d(struct qxl_device *qdev)
{
	free_irq(qdev->ivdev->irq, qdev->ddev);

	if (!qxl_3d_use_vring) {
		qxl_ring_free(qdev->q3d_info.iv3d_ring);
		qxl_bo_unref(&qdev->q3d_info.ringbo);
	}
}


/* the notify function used when creating a virt queue */
static void vp_notify(struct virtqueue *vq)
{
	struct qxl_device *qdev = vdev_to_qxl_dev(vq->vdev);

	/* we write the queue's selector into the notification register to
	 * signal the other end */
	iowrite16(vq->index, qdev->q3d_info.ioaddr + VIRTIO_PCI_QUEUE_NOTIFY);
}

static struct virtqueue *setup_cmdq(struct qxl_device *qdev,
				    void (*callback)(struct virtqueue *vq))
{
	struct virtqueue *vq;
	u16 num;
	unsigned long size;

	iowrite16(0, qdev->q3d_info.ioaddr + VIRTIO_PCI_QUEUE_SEL);
	num = ioread16(qdev->q3d_info.ioaddr + VIRTIO_PCI_QUEUE_NUM);
	if (!num || ioread32(qdev->q3d_info.ioaddr + VIRTIO_PCI_QUEUE_PFN)) {
		printk("queue setup failed %d\n", num);
		return ERR_PTR(-ENOENT);
	}

	size = PAGE_ALIGN(vring_size(num, VIRTIO_PCI_VRING_ALIGN));
	qdev->q3d_info.cmdqueue = alloc_pages_exact(size, GFP_KERNEL|__GFP_ZERO);
	if (qdev->q3d_info.cmdqueue == NULL) {
		printk("cq alloc failed %d\n", size);
		return ERR_PTR(-ENOMEM);
	}

	iowrite32(virt_to_phys(qdev->q3d_info.cmdqueue) >> VIRTIO_PCI_QUEUE_ADDR_SHIFT, qdev->q3d_info.ioaddr + VIRTIO_PCI_QUEUE_PFN);

	vq = vring_new_virtqueue(0, num, VIRTIO_PCI_VRING_ALIGN, &qdev->vdev,
				 true, qdev->q3d_info.cmdqueue, vp_notify,
				 callback, "qxlvirt");
	if (!vq) {
		printk("new virtqueue failed\n");
		return ERR_PTR(-ENOMEM);
	}

	DRM_INFO("virtqueue setup %d\n", num);
	return vq;
}

static void qdev_virq_cb(struct virtqueue *vq)
{

}

static void free_vbuf(struct qxl_3d_vbuffer *vbuf)
{
	if (vbuf->bo)
		qxl_bo_unref(&vbuf->bo);

	kfree(vbuf);
}

struct qxl_3d_vbuffer *allocate_vbuf(struct qxl_device *qdev,
				     struct qxl_bo *bo,
				     int size, bool inout, u32 *base_offset, u32 max_bo_len)
{
	struct virtqueue *vq = qdev->q3d_info.cmdq;
	struct qxl_3d_vbuffer *vbuf = NULL;
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
		vbuf->bo = qxl_bo_ref(bo);
		vbuf->bo_max_len = max_bo_len;
		/* TODO work out offset */
		ret = qxl_bo_get_sg_table(qdev, bo);
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

struct qxl_3d_command *qxl_3d_valloc_cmd_buf(struct qxl_device *qdev,
					     struct qxl_bo *qobj,
					     bool inout,
					     u32 *base_offset,
					     u32 max_bo_len,
					     struct qxl_3d_vbuffer **vbuffer_p)
{
	struct qxl_3d_vbuffer *vbuf;

	vbuf = allocate_vbuf(qdev, qobj, sizeof(struct qxl_3d_command), inout, base_offset, max_bo_len);
	if (IS_ERR(vbuf))
		return ERR_CAST(vbuf);
	*vbuffer_p = vbuf;
	return (struct qxl_3d_command *)vbuf->buf;
}

static void reclaim_vbufs(struct virtqueue *vq)
{
	struct qxl_3d_vbuffer *vbuf;
	unsigned int len;
	while ((vbuf = virtqueue_get_buf(vq, &len))) {
		free_vbuf(vbuf);
	}
}

static void virt_map_sgt(struct scatterlist *sg, struct qxl_3d_vbuffer *buf,
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

static int vadd_buf(struct virtqueue *vq, struct qxl_3d_vbuffer *buf)
{
	struct scatterlist *sg = buf->sg;
	int ret;
	int idx = 0, outcnt = 0, incnt = 0, old_idx;

	/* always put the command into in */
	sg_set_buf(&sg[idx++], buf->buf, buf->size);
	if (buf->inout == true)
		incnt = idx;
	if (buf->bo) {
		old_idx = idx;
		virt_map_sgt(sg, buf, &idx);

	}
	if (buf->inout == true)
		outcnt = idx - incnt;
	else
		incnt = idx;
 retry:
	ret = virtqueue_add_buf(vq, sg, outcnt, incnt, buf, GFP_ATOMIC);

	if (ret == -ENOSPC) {
	  reclaim_vbufs(vq);
	  //	  usleep_range(50, 500);
	  goto retry;
	}
	if (vq->num_free < 8)
	  virtqueue_kick(vq);
	if (!ret)
		ret = vq->num_free;
	return ret;
}

int qxl_3d_vadd_cmd_buf(struct qxl_device *qdev, struct qxl_3d_vbuffer *buf)
{
	reclaim_vbufs(qdev->q3d_info.cmdq);
	return vadd_buf(qdev->q3d_info.cmdq, buf);
}

static void virtio_qxl_release_dev(struct device *_d)
{
	/*
	 * No need for a release method as we allocate/free
	 * all devices together with the pci devices.
	 * Provide an empty one to avoid getting a warning from core.
	 */
}

static int qxl_init_3d_vring(struct qxl_device *qdev)
{
	struct qxl_3d_vbuffer *tbuf;
	uint32_t *dwp;
	qdev->q3d_info.cmdq = setup_cmdq(qdev, qdev_virq_cb);
	printk("cmdq at %p\n", qdev->q3d_info.cmdq);

	tbuf = allocate_vbuf(qdev, NULL, sizeof(struct qxl_3d_command), false, 0, 0);

	dwp = tbuf->buf;
	dwp[0] = 0xdeadbeef;

	vadd_buf(qdev->q3d_info.cmdq, tbuf);
	return 0;
}

static int qxl_init_3d_qring(struct qxl_device *qdev)
{
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
					  sizeof(struct qxl_3d_command_ring),
					  QXL_3D_COMMAND_RING_SIZE,
					  -1,
					  true,
						   &qdev->q3d_event, qdev->regs_3d_map + 12);

	DRM_INFO("3d version is %d %d\n", qdev->q3d_info.ram_3d_header->version, sizeof(struct qxl_3d_command));

	/* push something misc into the ring */
	{
		struct qxl_3d_command cmd;
		cmd.type = 0xdeadbeef;
		qxl_ring_push(qdev->q3d_info.iv3d_ring, &cmd, false);
		if (qxl_3d_only)
		  qxl_3d_ping(qdev);
	}

	qxl_bo_unreserve(qdev->q3d_info.ringbo);
	return 0;
out_unreserve:
	qxl_bo_unreserve(qdev->q3d_info.ringbo);
out_unref:
	qxl_bo_unref(&qdev->q3d_info.ringbo);
	return ret;
}

static int qxl_virtio_probe(struct virtio_device *vdev)
{
	printk("%s\n", __func__);
	return 0;
}
static void qxl_virtio_remove(struct virtio_device *vdev)
{
	printk("%s\n", __func__);
}

static void qxl_virtio_config_changed(struct virtio_device *vdev)
{

}

#ifdef CONFIG_PM
static int qxl_virtio_freeze(struct virtio_device *vdev)
{
	return 0;
}

static int qxl_virtio_restore(struct virtio_device *vdev)
{
	return 0;
}
#endif

static struct virtio_device_id id_table[] = {
	{ 0x3d, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct virtio_driver qxl_virtio_driver = {
	.feature_table = NULL,
	.feature_table_size = 0,
	.id_table = id_table,
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.feature_table_size = 0,
	.probe = qxl_virtio_probe,
	.remove = qxl_virtio_remove,
	.config_changed = qxl_virtio_config_changed,
#ifdef CONFIG_PM
	.freeze = qxl_virtio_freeze,
	.restore = qxl_virtio_restore,
#endif
};
	
int qxl_init_3d(struct qxl_device *qdev)
{
	/* create an object for the 3D ring and pin it at 0. */
	int ret;

	qdev->vdev.dev.parent = &qdev->ivdev->dev;
	qdev->vdev.dev.release = virtio_qxl_release_dev;
	qdev->vdev.config = &virtio_qxl_config_ops;
	qdev->vdev.id.vendor = 0x1af4;
	qdev->vdev.id.device = 0x3d;
	register_virtio_driver(&qxl_virtio_driver);
	/* grab bar 3 */
	qdev->q3d_info.ioaddr = pci_iomap(qdev->ivdev, 3, 0);

	pci_msi_off(qdev->ivdev);
	pci_set_master(qdev->ivdev);

	ret = register_virtio_device(&qdev->vdev);
	if (ret)
		printk("error registering virtio %d\n", ret);

	if (!qxl_3d_only) {
		ret = request_irq(qdev->ivdev->irq, qxl_3d_irq_handler, IRQF_SHARED,
				  "qxl3d", qdev->ddev);
		if (ret < 0) {
			DRM_INFO("failed to setup 3D irq handler\n");
		}
	}

	init_waitqueue_head(&qdev->q3d_info.fence_queue);
	idr_init(&qdev->q3d_info.resource_idr);
	spin_lock_init(&qdev->q3d_info.resource_idr_lock);

	if (!qxl_3d_use_vring) {
		ret = qxl_init_3d_qring(qdev);
		if (ret)
			goto fail;
		qxl_3d_irq_set_mask(qdev);
	} else {
		ret = qxl_init_3d_vring(qdev);
		if (ret)
			goto fail;
	}

	return 0;
fail:
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
	
	//printk("user cmd size %d\n", user_cmd.command_size);

	ret = qxl_gem_object_create(qdev, user_cmd.command_size,
				    0, QXL_GEM_DOMAIN_3D, false,
				    true, NULL, &gobj);

	if (ret)
		return ret;

	qobj = gem_to_qxl_bo(gobj);

	ret = qxl_bo_reserve(qobj, false);
	if (ret)
		goto out_free;

	qxl_ttm_placement_from_domain(qobj, qobj->type);
	ret = ttm_bo_validate(&qobj->tbo, &qobj->placement,
			      true, false);
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
		struct qxl_3d_command cmd, *cmd_p;
		struct qxl_3d_vbuffer *vbuf;

		cmd_p = qxl_3d_alloc_cmd(qdev, qobj, false, NULL, 0, &cmd, &vbuf);
		cmd_p->type = QXL_3D_CMD_SUBMIT;
		cmd_p->u.cmd_submit.size = user_cmd.command_size;

		qxl_3d_set_data(vbuf, qobj, 0, &cmd_p->u.cmd_submit.phy_addr);
		ret = qxl_3d_fence_emit(qdev, cmd_p, &fence);

		qxl_3d_send_cmd(qdev, cmd_p, vbuf, true);
	}



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

	if (qxl_3d_use_vring)
		virtqueue_kick(fence->qdev->q3d_info.cmdq);
	r = qxl_3d_fence_wait_seq(fence->qdev, fence->seq,
				  intr);
	if (r)
		return r;

	fence->seq = QXL_3D_FENCE_SIGNALED_SEQ;

	return 0;

}

int qxl_3d_fence_emit(struct qxl_device *qdev,
		      struct qxl_3d_command *cmd,
		      struct qxl_3d_fence **fence)
{
	*fence = kmalloc(sizeof(struct qxl_3d_fence), GFP_KERNEL);
	if ((*fence) == NULL)
		return -ENOMEM;

	kref_init(&((*fence)->kref));
	(*fence)->qdev = qdev;
	(*fence)->type = 1;
	(*fence)->seq = ++qdev->q3d_info.fence_drv.sync_seq;

	cmd->flags |= QXL_3D_COMMAND_EMIT_FENCE;
	cmd->fence_id = (*fence)->seq;

	return 0;
}
	
int qxl_3d_set_front(struct qxl_device *qdev,
		     struct qxl_framebuffer *fb, int x, int y,
		     int width, int height)
{
	struct qxl_3d_command cmd, *cmd_p;
	struct qxl_3d_vbuffer *vbuf;

	cmd_p = qxl_3d_alloc_cmd(qdev, NULL, false, NULL, 0, &cmd, &vbuf);
	memset(cmd_p, 0, sizeof(cmd));
	cmd_p->type = QXL_3D_SET_SCANOUT;
	cmd_p->u.set_scanout.res_handle = fb->res_3d_handle;
	cmd_p->u.set_scanout.box.x = x;
	cmd_p->u.set_scanout.box.y = y;
	cmd_p->u.set_scanout.box.w = width;
	cmd_p->u.set_scanout.box.h = height;
	qxl_3d_send_cmd(qdev, cmd_p, vbuf, true);
	return 0;
}

int qxl_3d_dirty_front(struct qxl_device *qdev,
		       struct qxl_framebuffer *fb, int x, int y,
		       int width, int height)
{
	struct qxl_3d_command cmd, *cmd_p;
	struct qxl_3d_vbuffer *vbuf;

	cmd_p = qxl_3d_alloc_cmd(qdev, NULL, false, NULL, 0, &cmd, &vbuf);
	memset(cmd_p, 0, sizeof(cmd));
	cmd_p->type = QXL_3D_FLUSH_BUFFER;
	cmd_p->u.flush_buffer.res_handle = fb->res_3d_handle;
	cmd_p->u.flush_buffer.box.x = x;
	cmd_p->u.flush_buffer.box.y = y;
	cmd_p->u.flush_buffer.box.w = width;
	cmd_p->u.flush_buffer.box.h = height;
	qxl_3d_send_cmd(qdev, cmd_p, vbuf, true);
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


