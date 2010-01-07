
#include "qxl_drv.h"

static void qxl_dump_mode(struct qxl_device *qdev, void *p)
{
	struct qxl_mode *m = p;
	DRM_INFO("%d: %dx%d %d bits, stride %d, %dmm x %dmm, orientation %d\n",
		 m->id, m->x_res, m->y_res, m->bits, m->stride, m->x_mili,
		 m->y_mili, m->orientation);
}

static bool qxl_check_device(struct qxl_device *qdev)
{
	struct qxl_rom *rom = qdev->rom;
	int mode_offset;
	int i;

	if (rom->magic != 0x4f525851) {
		DRM_ERROR("bad rom signature %x\n", rom->magic);
		return false;
	}

	DRM_INFO("Device Version %d.%d\n", rom->id, rom->update_id);
	DRM_INFO("Compression level %d log level %d\n", rom->compression_level,
		 rom->log_level);
	DRM_INFO("Currently using mode #%d, list at 0x%x\n",
		 rom->mode, rom->modes_offset);
	DRM_INFO("%d io pages at 0x%x\n",
		 rom->num_io_pages, rom->pages_offset);
	DRM_INFO("%d byte draw area at 0x%x\n",
		 rom->draw_area_size, rom->draw_area_offset);

	qdev->vram_size = rom->draw_area_size;
	DRM_INFO("RAM header offset: 0x%x\n", rom->ram_header_offset);

	mode_offset = rom->modes_offset / 4;
	qdev->mode_info.num_modes = ((u32 *)rom)[mode_offset];
	DRM_INFO("rom modes offset 0x%x for %d modes\n", rom->modes_offset, qdev->mode_info.num_modes);
	qdev->mode_info.modes = (void *)((uint32_t *)rom + mode_offset + 1);
	for (i = 0; i < qdev->mode_info.num_modes; i++)
		qxl_dump_mode(qdev, qdev->mode_info.modes + i);
	return true;
}

int qxl_device_init(struct qxl_device *qdev,
		    struct drm_device *ddev,
		    struct pci_dev *pdev,
		    unsigned long flags)
{
	int r;
	DRM_INFO("qxl: Initialising\n");

	qdev->dev = &pdev->dev;
	qdev->ddev = ddev;
	qdev->pdev = pdev;
	qdev->flags = flags;

	mutex_init(&qdev->gem.mutex);
//	rwlock_init(&qdev->fence_drv.lock);
	INIT_LIST_HEAD(&qdev->gem.objects);

	qdev->rom_base = drm_get_resource_start(qdev->ddev, 2);
	qdev->rom_size = drm_get_resource_len(qdev->ddev, 2);
	qdev->vram_base = drm_get_resource_start(qdev->ddev, 0);
	qdev->io_base = drm_get_resource_start(qdev->ddev, 3);

	qdev->rom = ioremap(qdev->rom_base, qdev->rom_size);
	if (!qdev->rom){
		printk(KERN_ERR "Unable to ioremap ROM\n");
		return -ENOMEM;
	}

	qxl_check_device(qdev);

	qdev->ram_header = ioremap(qdev->vram_base + qdev->rom->ram_header_offset,
				   drm_get_resource_len(qdev->ddev, 0) - qdev->rom->ram_header_offset);

	qdev->command_ring = qxl_ring_create(&(qdev->ram_header->cmd_ring_hdr),
					     sizeof(struct qxl_command),
					     32, qdev->io_base + QXL_IO_NOTIFY_CMD);

	qdev->release_ring = qxl_ring_create(&(qdev->ram_header->release_ring_hdr),
					     sizeof(uint64_t),
					     8, 0);
#if 0
	r = qxl_fence_driver_init(qdev);
	if (r) {
		DRM_ERROR("fence init failed %d\n", r);
		return r;
	}
#endif
	r = qxl_bo_init(qdev);
	if (r) {
		DRM_ERROR("bo init failed %d\n", r);
		return r;
	}
	return 0;
}

void qxl_device_fini(struct qxl_device *qdev)
{
	qxl_ring_free(qdev->command_ring);
	qxl_ring_free(qdev->release_ring);
//	qxl_fence_driver_fini(qdev);
	qxl_bo_fini(qdev);
	iounmap(qdev->ram_header);
	iounmap(qdev->rom);
	qdev->rom = NULL;
	qdev->mode_info.modes = NULL;
	qdev->mode_info.num_modes = 0;
}

int qxl_driver_unload(struct drm_device *dev)
{
	struct qxl_device *qdev = dev->dev_private;
	
	if (qdev == NULL)
		return 0;
	qxl_modeset_fini(qdev);
	qxl_device_fini(qdev);

	kfree(qdev);
	dev->dev_private = NULL;
	return 0;
}

int qxl_driver_load(struct drm_device *dev, unsigned long flags)
{
	struct qxl_device *qdev;
	int r;

	qdev = kzalloc(sizeof(struct qxl_device), GFP_KERNEL);
	if (qdev == NULL) 
		return -ENOMEM;

	dev->dev_private = qdev;


	r = qxl_device_init(qdev, dev, dev->pdev, flags);
	if (r)
		goto out;
	r = qxl_modeset_init(qdev);


	if (r)
		qxl_driver_unload(dev);
out:
	return r;
}

struct drm_ioctl_desc qxl_ioctls[] = {
	
};

int qxl_max_ioctls = DRM_ARRAY_SIZE(qxl_ioctls);
