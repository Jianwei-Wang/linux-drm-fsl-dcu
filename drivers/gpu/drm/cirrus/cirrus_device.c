/*
 * Copyright 2010 Matt Turner.
 * Copyright 2011 Red Hat <mjg@redhat.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License version 2. See the file COPYING in the main
 * directory of this archive for more details.
 *
 * Authors: Matthew Garrett
 *			Matt Turner
 */
#include "drmP.h"
#include "drm.h"

#include "cirrus.h"
#include "cirrus_drv.h"

/* Unmap the framebuffer from the core and release the memory */
static void cirrus_vram_fini(struct cirrus_device *cdev)
{
	iounmap(cdev->rmmio);
	cdev->rmmio = NULL;
	if (cdev->framebuffer)
		drm_rmmap(cdev->ddev, cdev->framebuffer);
	if (cdev->mc.vram_base)
		release_mem_region(cdev->mc.vram_base, cdev->mc.vram_size);
}

/* Map the framebuffer from the card and configure the core */
static int cirrus_vram_init(struct cirrus_device *cdev)
{
	int ret;

	/* BAR 0 is VRAM */
	cdev->mc.vram_base = pci_resource_start(cdev->ddev->pdev, 0);
	/* We have 4MB of VRAM */
	cdev->mc.vram_size = 4 * 1024 * 1024;

	if (!request_mem_region(cdev->mc.vram_base, cdev->mc.vram_size,
				"cirrusdrmfb_vram")) {
		CIRRUS_ERROR("can't reserve VRAM\n");
		return -ENXIO;
	}

	/*
	 * Tell the kernel that vram is available to userspace. This driver
	 * provides no acceleration and doesn't support any off-screen buffers
	 * so we can just map the entirity of vram as a framebuffer.
	 */
	ret = drm_addmap(cdev->ddev, cdev->mc.vram_base, cdev->mc.vram_size,
			 _DRM_FRAME_BUFFER, _DRM_WRITE_COMBINING,
			 &cdev->framebuffer);

	if (ret) {
		cirrus_vram_fini(cdev);
		return ret;
	}

	return 0;
}

/*
 * Our emulated hardware has two sets of memory. One is video RAM and can
 * simply be used as a linear framebuffer - the other provides mmio access
 * to the display registers. The latter can also be accessed via IO port
 * access, but we map the range and use mmio to program them instead
 */

int cirrus_device_init(struct cirrus_device *cdev,
		       struct drm_device *ddev,
		       struct pci_dev *pdev, uint32_t flags)
{
	int ret;

	cdev->dev = &pdev->dev;
	cdev->ddev = ddev;
	cdev->pdev = pdev;
	cdev->flags = flags;

	/* Hardcode the number of CRTCs to 1 */
	cdev->num_crtc = 1;

	/* BAR 0 is the framebuffer, BAR 1 contains registers */
	cdev->rmmio_base = pci_resource_start(cdev->ddev->pdev, 1);
	cdev->rmmio_size = pci_resource_len(cdev->ddev->pdev, 1);

	if (!request_mem_region(cdev->rmmio_base, cdev->rmmio_size,
				"cirrusdrmfb_mmio")) {
		CIRRUS_ERROR("can't reserve mmio registers\n");
		return -ENOMEM;
	}

	cdev->rmmio = ioremap(cdev->rmmio_base, cdev->rmmio_size);

	if (cdev->rmmio == NULL)
		return -ENOMEM;

	ret = cirrus_vram_init(cdev);
	if (ret) {
		release_mem_region(cdev->rmmio_base, cdev->rmmio_size);
		return ret;
	}

	return 0;
}

void cirrus_device_fini(struct cirrus_device *cdev)
{
	release_mem_region(cdev->rmmio_base, cdev->rmmio_size);
	cirrus_vram_fini(cdev);
}
