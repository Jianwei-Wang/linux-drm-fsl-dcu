/*
 * Copyright 2011 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software")
 * to deal in the software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * them Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTIBILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Adam Jackson <ajax@redhat.com>
 *	Ben Widawsky <ben@bwidawsk.net>
 */

/**
 * This is vgem, a (non-hardware-backed) GEM service.  This is used by Mesa's
 * software renderer and the X server for efficient buffer sharing.
 */

#include "drmP.h"
#include "drm.h"
#include <linux/module.h>
#include <linux/ramfs.h>
#include <linux/shmem_fs.h>
#include "vgem_drv.h"

#define DRIVER_NAME	"vgem"
#define DRIVER_DESC	"Virtual GEM provider"
#define DRIVER_DATE	"20120112"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static int vgem_load(struct drm_device *dev, unsigned long flags)
{
	struct drm_vgem_private *dev_priv;

	dev_priv = kzalloc(sizeof(*dev_priv), GFP_KERNEL);
	if (!dev_priv)
		return -ENOMEM;

	dev->dev_private = dev_priv;
	dev_priv->dev = dev;

	return 0;
}

static int vgem_unload(struct drm_device *dev)
{
	kfree(dev->dev_private);
	return 0;
}

static int vgem_gem_init_object(struct drm_gem_object *obj)
{
	return 0;
}

void vgem_gem_put_pages(struct drm_vgem_gem_object *obj)
{
	int num_pages = obj->base.size / PAGE_SIZE;
	int i;

	for (i = 0; i < num_pages; i++) {
		if (obj->pages[i] == NULL)
			break;
		page_cache_release(obj->pages[i]);
	}

	drm_free_large(obj->pages);
	obj->pages = NULL;
}

static void vgem_gem_free_object(struct drm_gem_object *obj)
{
	struct drm_vgem_gem_object *vgem_obj = to_vgem_bo(obj);

	if (obj)
		drm_gem_free_mmap_offset(obj);

	drm_gem_object_release(obj);

	if (vgem_obj->pages)
		vgem_gem_put_pages(vgem_obj);

	vgem_obj->pages = NULL;

	kfree(vgem_obj);
}

int vgem_gem_get_pages(struct drm_vgem_gem_object *obj)
{
	struct address_space *mapping;
	gfp_t gfpmask = __GFP_NORETRY | __GFP_NOWARN;
	int num_pages, i, ret = 0;

	if (obj->pages)
		return 0;

	num_pages = obj->base.size / PAGE_SIZE;

	obj->pages = drm_malloc_ab(num_pages, sizeof(struct page *));
	if (obj->pages == NULL)
		return -ENOMEM;

	mapping = obj->base.filp->f_path.dentry->d_inode->i_mapping;
	gfpmask |= mapping_gfp_mask(mapping);

	for (i = 0; i < num_pages; i++) {
		struct page *page;
		page = shmem_read_mapping_page_gfp(mapping, i, gfpmask);
		if (IS_ERR(page)) {
			ret = PTR_ERR(page);
			goto err_out;
		}
		obj->pages[i] = page;
	}

	return ret;

err_out:
	vgem_gem_put_pages(obj);
	return ret;
}

static int vgem_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_vgem_gem_object *obj = to_vgem_bo(vma->vm_private_data);
	loff_t num_pages;
	pgoff_t page_offset;
	int ret;

	/* We don't use vmf->pgoff since that has the fake offset */
	page_offset = ((unsigned long)vmf->virtual_address - vma->vm_start) >>
		PAGE_SHIFT;

	num_pages = obj->base.size / PAGE_SIZE;

	if (WARN_ON(page_offset > num_pages))
		return VM_FAULT_SIGBUS;

	ret = vgem_gem_get_pages(obj);
	if (ret)
		return ret;

	ret = vm_insert_page(vma, (unsigned long)vmf->virtual_address,
			     obj->pages[page_offset]);

	/* Pretty dumb handler for now */
	switch (ret) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		return VM_FAULT_NOPAGE;
	default:
		return VM_FAULT_SIGBUS;
	}
}

static struct vm_operations_struct vgem_gem_vm_ops = {
	.fault = vgem_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

/* ioctls */

static struct drm_gem_object *vgem_gem_create(struct drm_device *dev,
					      struct drm_file *file,
					      unsigned int *handle,
					      unsigned long size)
{
	struct drm_vgem_gem_object *obj;
	struct drm_gem_object *gem_object;
	int err;

	size = roundup(size, PAGE_SIZE);

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	gem_object = &obj->base;

	err = drm_gem_object_init(dev, gem_object, size);
	if (err)
		goto out;

	err = drm_gem_create_mmap_offset(gem_object);
	if (err)
		goto mmap_out;

	err = drm_gem_handle_create(file, gem_object, handle);
	if (err)
		goto handle_out;

	drm_gem_object_unreference_unlocked(gem_object);

	return gem_object;

handle_out:
	drm_gem_free_mmap_offset(gem_object);

mmap_out:
	drm_gem_object_release(gem_object);

out:
	kfree(gem_object);

	return ERR_PTR(err);
}

static int vgem_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
				struct drm_mode_create_dumb *args)
{
	struct drm_gem_object *gem_object;
	uint64_t size;

	size = args->height * args->width * args->bpp;

	gem_object = vgem_gem_create(dev, file, &args->handle, size);

	if (IS_ERR(gem_object)) {
		DRM_DEBUG_DRIVER("object creation failed\n");
		return PTR_ERR(gem_object);
	}

	args->size = size;
	args->pitch = args->width;

	DRM_DEBUG_DRIVER("Created object of size %lld\n", size);

	return 0;
}

int vgem_gem_dumb_map(struct drm_file *file, struct drm_device *dev,
		      uint32_t handle, uint64_t *offset)
{
	struct drm_gem_object *obj;

	obj = drm_gem_object_lookup(dev, file, handle);
	if (!obj)
		return -ENOENT;

	obj->filp->private_data = obj;

	BUG_ON(!obj->map_list.map);

	*offset = (uint64_t)obj->map_list.hash.key << PAGE_SHIFT;

	drm_gem_object_unreference_unlocked(obj);

	return 0;
}

static const struct file_operations vgem_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.mmap		= drm_gem_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
	.unlocked_ioctl = drm_ioctl,
	.release	= drm_release,
	.fasync		= drm_fasync,
	.llseek		= noop_llseek,
};

static struct drm_driver vgem_driver = {
	.driver_features	= DRIVER_BUS_PLATFORM | DRIVER_GEM,
	.load			= vgem_load,
	.unload			= vgem_unload,

	.gem_init_object	= vgem_gem_init_object,
	.gem_free_object	= vgem_gem_free_object,
	.gem_vm_ops		= &vgem_gem_vm_ops,

	.fops			= &vgem_driver_fops,

	.dumb_create		= vgem_gem_dumb_create,
	.dumb_map_offset	= vgem_gem_dumb_map,

	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

static int vgem_platform_probe(struct platform_device *pdev)
{
	return drm_platform_init(&vgem_driver, pdev);
}

static int vgem_platform_remove(struct platform_device *pdev)
{
	drm_platform_exit(&vgem_driver, pdev);
	return 0;
}

static struct platform_driver vgem_platform_driver = {
	.probe		= vgem_platform_probe,
	.remove		= __devexit_p(vgem_platform_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DRIVER_NAME,
	},
};

static struct platform_device *vgem_device;

static int __init vgem_init(void)
{
	int ret;

	ret = platform_driver_register(&vgem_platform_driver);
	if (ret)
		return ret;

	vgem_device = platform_device_alloc("vgem", -1);
	if (!vgem_device) {
		ret = -ENOMEM;
		goto out;
	}

	ret = platform_device_add(vgem_device);
	if (!ret)
		return 0;

out:
	platform_device_put(vgem_device);
	platform_driver_unregister(&vgem_platform_driver);

	return ret;
}

static void __exit vgem_exit(void)
{
	platform_device_unregister(vgem_device);
	platform_driver_unregister(&vgem_platform_driver);
}

module_init(vgem_init);
module_exit(vgem_exit);

MODULE_AUTHOR("Red Hat, Inc.");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
