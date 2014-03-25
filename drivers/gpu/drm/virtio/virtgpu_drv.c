/*
 * 2011 Red Hat, Inc.
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
 *    Dave Airlie <airlied@redhat.com>
 */

#include <linux/module.h>
#include <linux/console.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include "drmP.h"
#include "drm/drm.h"

#include "virtgpu_drv.h"
static struct drm_driver driver;

int virtgpu_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, virtgpu_modeset, int, 0400);

extern int virtgpu_max_ioctls;

static int virtgpu_probe(struct virtio_device *vdev)
{
	struct drm_device *dev;
	int ret;

#ifdef CONFIG_VGA_CONSOLE
	if (vgacon_text_force() && virtgpu_modeset == -1)
		return -EINVAL;
#endif

	if (virtgpu_modeset == 0)
		return -EINVAL;
	driver.num_ioctls = virtgpu_max_ioctls;

	virtio_set_driver_bus(&driver);
	INIT_LIST_HEAD(&driver.device_list);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	dev->dev = &vdev->dev;
	dev->virtdev = vdev;
	vdev->priv = dev;
	mutex_lock(&drm_global_mutex);

	ret = drm_fill_in_dev(dev, NULL, &driver);

	ret = drm_get_minor(dev, &dev->control, DRM_MINOR_CONTROL);
	if (ret)
		goto err_g1;

	ret = drm_get_minor(dev, &dev->primary, DRM_MINOR_LEGACY);
	if (ret)
		goto err_g2;

	if (dev->driver->load) {
		ret = dev->driver->load(dev, 0);
		if (ret)
			goto err_g3;
	}

	/* setup the grouping for the legacy output */
	ret = drm_mode_group_init_legacy_group(dev,
					       &dev->primary->mode_group);
	if (ret)
		goto err_g3;

	list_add_tail(&dev->driver_item, &driver.device_list);

	mutex_unlock(&drm_global_mutex);

	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n",
		 driver.name, driver.major, driver.minor, driver.patchlevel,
		 driver.date, dev->primary->index);

	return 0;

err_g3:
	drm_put_minor(&dev->primary);
err_g2:
	drm_put_minor(&dev->control);
err_g1:
	kfree(dev);
	mutex_unlock(&drm_global_mutex);

	return 0;
}

static void virtgpu_remove(struct virtio_device *vdev)
{
	struct drm_device *dev = vdev->priv;
	drm_put_dev(dev);
}

static void virtgpu_config_changed(struct virtio_device *vdev)
{
	struct drm_device *dev = vdev->priv;
	DRM_ERROR("virtgpu config changed\n");
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GPU, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = { VIRTIO_GPU_F_FENCE, VIRTIO_GPU_F_VIRGL };
static struct virtio_driver virtgpu_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.driver.owner = THIS_MODULE,
	.id_table = id_table,
	.probe = virtgpu_probe,
	.remove = virtgpu_remove,
	.config_changed = virtgpu_config_changed
};

module_virtio_driver(virtgpu_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio GPU driver");
MODULE_LICENSE("GPL");

static const struct file_operations virtgpu_driver_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.mmap = virtgpu_mmap,
	.poll = drm_poll,
	.read = drm_read,
	.unlocked_ioctl	= drm_ioctl,
	.release = drm_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.llseek = noop_llseek,
};


static struct drm_driver driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM,
	.load = virtgpu_driver_load,
	.unload = virtgpu_driver_unload,
	.open = virtgpu_driver_open,
	.postclose = virtgpu_driver_postclose,

	.dumb_create = virtgpu_mode_dumb_create,
	.dumb_map_offset = virtgpu_mode_dumb_mmap,
	.dumb_destroy = virtgpu_mode_dumb_destroy,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = virtgpu_debugfs_init,
	.debugfs_cleanup = virtgpu_debugfs_takedown,
#endif

	.gem_init_object = virtgpu_gem_init_object,
	.gem_free_object = virtgpu_gem_free_object,
	.gem_open_object = virtgpu_gem_object_open,
	.gem_close_object = virtgpu_gem_object_close,
	.fops = &virtgpu_driver_fops,

	.ioctls = virtgpu_ioctls,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};
