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

#include "virgl_drv.h"
#include "virgl_object.h"

#include <linux/io-mapping.h>

int virgl_device_init(struct virgl_device *qdev,
		    struct drm_device *ddev,
		    struct pci_dev *pdev,
		    unsigned long flags)
{
	int r;

	qdev->dev = &pdev->dev;
	qdev->ddev = ddev;
	qdev->pdev = pdev;
	qdev->flags = flags;
	qdev->ioaddr = pci_iomap(qdev->pdev, 0, 0);

	spin_lock_init(&qdev->gem.lock);
	INIT_LIST_HEAD(&qdev->gem.objects);
	INIT_WORK(&qdev->dequeue_work, virgl_dequeue_work_func);
	init_waitqueue_head(&qdev->fence_queue);
	init_waitqueue_head(&qdev->cmd_ack_queue);
	idr_init(&qdev->resource_idr);
	spin_lock_init(&qdev->resource_idr_lock);
	idr_init(&qdev->ctx_id_idr);
	spin_lock_init(&qdev->ctx_id_idr_lock);

	r = pci_enable_msi(qdev->pdev);
	if (!r) {
	  dev_info(qdev->dev,"3D device MSI enabled\n");
	}

	r = virgl_bo_init(qdev);
	if (r) {
		DRM_ERROR("bo init failed %d\n", r);
		return r;
	}

	/* must initialize irq before first async io - slot creation */
	r = virgl_irq_init(qdev);
	if (r)
		return r;


	r = virgl_virtio_init(qdev);
	if (r) {
		DRM_INFO("3D failed to init %d\n", r);
		return r;
	}

	r = virgl_fb_init(qdev);
	if (r)
		return r;

	return 0;
}

void virgl_device_fini(struct virgl_device *qdev)
{
	virgl_virtio_fini(qdev);

	virgl_bo_fini(qdev);
}

int virgl_driver_unload(struct drm_device *dev)
{
	struct virgl_device *qdev = dev->dev_private;

	if (qdev == NULL)
		return 0;
	virgl_modeset_fini(qdev);
	virgl_device_fini(qdev);

	kfree(qdev);
	dev->dev_private = NULL;
	return 0;
}

int virgl_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct virgl_device *qdev;
	int r;

	/* require kms */
	if (!drm_core_check_feature(dev, DRIVER_MODESET))
		return -ENODEV;

	qdev = kzalloc(sizeof(struct virgl_device), GFP_KERNEL);
	if (qdev == NULL)
		return -ENOMEM;

	dev->dev_private = qdev;

	r = virgl_device_init(qdev, dev, dev->pdev, flags);
	if (r)
		goto out;

	r = virgl_modeset_init(qdev);
	if (r) {
		virgl_driver_unload(dev);
		goto out;
	}

	return 0;
out:
	kfree(qdev);
	return r;
}


int virgl_driver_open(struct drm_device *dev, struct drm_file *fpriv)
{
	struct virgl_device *qdev = dev->dev_private;
	struct virgl_fpriv *vfpriv;
	uint32_t id;
	int ret;

	/* allocate a virt GPU context for this opener */
	vfpriv = kzalloc(sizeof(*fpriv), GFP_KERNEL);
	if (!vfpriv)
		return -ENOMEM;

	ret = virgl_context_create(qdev, &id);
	if (ret) {
		kfree(vfpriv);
		return ret;
	}

	vfpriv->ctx_id = id;
	fpriv->driver_priv = vfpriv;
	return 0;
}

void virgl_driver_preclose(struct drm_device *dev, struct drm_file *fpriv)
{
}

void virgl_driver_postclose(struct drm_device *dev, struct drm_file *fpriv)
{
	struct virgl_device *qdev = dev->dev_private;
	struct virgl_fpriv *vfpriv;

	vfpriv = fpriv->driver_priv;

	virgl_context_destroy(qdev, vfpriv->ctx_id);
	kfree(vfpriv);
	fpriv->driver_priv = NULL;
}
