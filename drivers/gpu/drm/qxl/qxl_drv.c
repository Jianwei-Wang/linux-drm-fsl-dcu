/* vim: set ts=8 sw=8 tw=78 ai noexpandtab */
/* qxl_drv.c -- QXL driver -*- linux-c -*-
 *
 * Copyright 2011 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Dave Airlie <airlie@redhat.com>
 *    Alon Levy <alevy@redhat.com>
 */

#include <linux/module.h>
#include <linux/console.h>

#include "drmP.h"
#include "drm/drm.h"
#include "drm_crtc_helper.h"
#include "qxl_drv.h"
#include "qxl_object.h"

extern int qxl_max_ioctls;
static DEFINE_PCI_DEVICE_TABLE(pciidlist) = {
	{ 0x1b36, 0x100, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_VGA << 8,
	  0xffff00, 0 },
	{ 0x1b36, 0x100, PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY_OTHER << 8,
	  0xffff00, 0 },
	{ 0, 0, 0 },
};
MODULE_DEVICE_TABLE(pci, pciidlist);

static int qxl_modeset = -1;
int qxl_num_crtc = 4;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, qxl_modeset, int, 0400);

MODULE_PARM_DESC(num_heads, "Number of virtual crtcs to expose (default 4)");
module_param_named(num_heads, qxl_num_crtc, int, 0400);

static struct drm_driver qxl_driver;
static struct pci_driver qxl_pci_driver;

static int
qxl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct qxl_device *qdev;
	int ret;

	if (pdev->revision < 4) {
		DRM_ERROR("qxl too old, doesn't support client_monitors_config,"
			  " use xf86-video-qxl in user mode");
		return -EINVAL; /* TODO: ENODEV ? */
	}

	qdev = kzalloc(sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;

	ret = drm_dev_init(&qdev->ddev, &qxl_driver, &pdev->dev);
	if (ret)
		goto err_free;

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_free;

	qdev->pdev = pdev;

	pci_set_drvdata(pdev, qdev);


	mutex_lock(&drm_global_mutex);

	ret = drm_register_minors(&qdev->ddev);
	if (ret)
		goto err_unlock;

	ret = qxl_driver_load(qdev, ent->driver_data);
	if (ret)
		goto err_unlock;

	/* setup grouping for legacy outputs */
	ret = drm_mode_group_init_legacy_group(&qdev->ddev,
					       &qdev->ddev.primary->mode_group);

err_unlock:
	mutex_unlock(&drm_global_mutex);

	return ret;
err_pci:
	pci_disable_device(pdev);
err_free:
	kfree(qdev);
	return ret;
}

static void
qxl_pci_remove(struct pci_dev *pdev)
{
	struct qxl_device *qdev = pci_get_drvdata(pdev);
	struct drm_device *dev = &qdev->ddev;

	qxl_driver_unload(qdev);
	drm_dev_unregister(dev);

	drm_dev_fini(dev);
	kfree(qdev);
}

static const struct file_operations qxl_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.poll = drm_poll,
	.mmap = qxl_mmap,
};

static int qxl_drm_freeze(struct qxl_device *qdev)
{
	struct pci_dev *pdev = qdev->ddev.pdev;
	struct drm_crtc *crtc;

	drm_kms_helper_poll_disable(&qdev->ddev);

	console_lock();
	qxl_fbdev_set_suspend(qdev, 1);
	console_unlock();

	/* unpin the front buffers */
	list_for_each_entry(crtc, &qdev->ddev.mode_config.crtc_list, head) {
		struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
		if (crtc->enabled)
			(*crtc_funcs->disable)(crtc);
	}

	qxl_destroy_monitors_object(qdev);
	qxl_surf_evict(qdev);
	qxl_vram_evict(qdev);

	while (!qxl_check_idle(qdev->command_ring));
	while (!qxl_check_idle(qdev->release_ring))
		qxl_queue_garbage_collect(qdev, 1);

	pci_save_state(pdev);

	return 0;
}

static int qxl_drm_resume(struct qxl_device *qdev, bool thaw)
{
	qdev->ram_header->int_mask = QXL_INTERRUPT_MASK;
	if (!thaw) {
		qxl_reinit_memslots(qdev);
		qxl_ring_init_hdr(qdev->release_ring);
	}

	qxl_create_monitors_object(qdev);
	drm_helper_resume_force_mode(&qdev->ddev);

	console_lock();
	qxl_fbdev_set_suspend(qdev, 0);
	console_unlock();

	drm_kms_helper_poll_enable(&qdev->ddev);
	return 0;
}

static int qxl_pm_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct qxl_device *qdev = pci_get_drvdata(pdev);
	int error;

	error = qxl_drm_freeze(qdev);
	if (error)
		return error;

	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);
	return 0;
}

static int qxl_pm_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct qxl_device *qdev = pci_get_drvdata(pdev);

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	if (pci_enable_device(pdev)) {
		return -EIO;
	}

	return qxl_drm_resume(qdev, false);
}

static int qxl_pm_thaw(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct qxl_device *qdev = pci_get_drvdata(pdev);

	return qxl_drm_resume(qdev, true);
}

static int qxl_pm_freeze(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct qxl_device *qdev = pci_get_drvdata(pdev);

	return qxl_drm_freeze(qdev);
}

static int qxl_pm_restore(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct qxl_device *qdev = pci_get_drvdata(pdev);

	qxl_io_reset(qdev);
	return qxl_drm_resume(qdev, false);
}

static const struct dev_pm_ops qxl_pm_ops = {
	.suspend = qxl_pm_suspend,
	.resume = qxl_pm_resume,
	.freeze = qxl_pm_freeze,
	.thaw = qxl_pm_thaw,
	.poweroff = qxl_pm_freeze,
	.restore = qxl_pm_restore,
};
static struct pci_driver qxl_pci_driver = {
	 .name = DRIVER_NAME,
	 .id_table = pciidlist,
	 .probe = qxl_pci_probe,
	 .remove = qxl_pci_remove,
	 .driver.pm = &qxl_pm_ops,
};

static struct drm_driver qxl_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET |
			   DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED,

	.dumb_create = qxl_mode_dumb_create,
	.dumb_map_offset = qxl_mode_dumb_mmap,
	.dumb_destroy = drm_gem_dumb_destroy,
#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = qxl_debugfs_init,
	.debugfs_cleanup = qxl_debugfs_takedown,
#endif
	.gem_free_object = qxl_gem_object_free,
	.gem_open_object = qxl_gem_object_open,
	.gem_close_object = qxl_gem_object_close,
	.fops = &qxl_fops,
	.ioctls = qxl_ioctls,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = 0,
	.minor = 1,
	.patchlevel = 0,
};

static int __init qxl_init(void)
{
#ifdef CONFIG_VGA_CONSOLE
	if (vgacon_text_force() && qxl_modeset == -1)
		return -EINVAL;
#endif

	if (qxl_modeset == 0)
		return -EINVAL;
	qxl_driver.num_ioctls = qxl_max_ioctls;

	drm_pci_driver_init(&qxl_driver, &qxl_pci_driver);

	return pci_register_driver(&qxl_pci_driver);
}

static void __exit qxl_exit(void)
{
	pci_unregister_driver(&qxl_pci_driver);
}

module_init(qxl_init);
module_exit(qxl_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
