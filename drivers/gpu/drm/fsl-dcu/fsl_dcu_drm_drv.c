/*
 * Copyright 2015 Freescale Semiconductor, Inc.
 *
 * Freescale DCU drm device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/clk-provider.h>

#include <drm/drmP.h>

#include "fsl_dcu_drm_drv.h"
#include "fsl_dcu_drm_crtc.h"
#include "fsl_dcu_drm_kms.h"

static int fsl_dcu_unload(struct drm_device *dev)
{
	drm_mode_config_cleanup(dev);
	drm_vblank_cleanup(dev);
	drm_irq_uninstall(dev);

	dev->dev_private = NULL;

	return 0;
}

static struct regmap_config fsl_dcu_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
};

static int fsl_dcu_bypass_tcon(struct fsl_dcu_drm_device *fsl_dev,
			       struct device_node *np)
{
	struct device_node *tcon_np;
	struct platform_device *pdev;
	struct clk *tcon_clk;
	struct resource *res;
	void __iomem *base;

	tcon_np = of_parse_phandle(np, "tcon-controller", 0);
	if (!tcon_np)
		return -EINVAL;

	pdev = of_find_device_by_node(tcon_np);
	if (!pdev)
		return -EINVAL;

	tcon_clk = devm_clk_get(&pdev->dev, "tcon");
	if (IS_ERR(tcon_clk))
		return PTR_ERR(tcon_clk);
	clk_prepare_enable(tcon_clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	fsl_dev->tcon_regmap = devm_regmap_init_mmio(&pdev->dev,
			base, &fsl_dcu_regmap_config);
	if (IS_ERR(fsl_dev->tcon_regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(fsl_dev->tcon_regmap);
	}

	regmap_write(fsl_dev->tcon_regmap, TCON_CTRL1, TCON_BYPASS_ENABLE);
	return 0;
}

static int pixclk_register(struct fsl_dcu_drm_device *fsl_dev,
			   struct device_node *np)
{
	struct device_node *scfg_np;
	struct platform_device *pdev;
	struct resource *res;
	void __iomem *base;

	scfg_np = of_parse_phandle(np, "scfg-controller", 0);
	if (!scfg_np)
		return 0;

	pdev = of_find_device_by_node(scfg_np);
	if (!pdev)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	fsl_dev->pixclk = clk_register_gate(fsl_dev->dev,
			"pixclk", NULL, 0,base + SCFG_PIXCLKCR,
			7, 0, NULL);
	return 0;
}

static int fsl_dcu_drm_irq_init(struct drm_device *dev)
{
	struct platform_device *pdev = dev->platformdev;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	unsigned int int_mask;
	int ret;

	ret = drm_irq_install(dev, platform_get_irq(dev->platformdev, 0));
	if (ret < 0)
		dev_err(&pdev->dev, "failed to install IRQ handler\n");

	dev->irq_enabled = true;
	dev->vblank_disable_allowed = true;

	regmap_write(fsl_dev->regmap, DCU_INT_STATUS, 0);
	regmap_read(fsl_dev->regmap, DCU_INT_MASK, &int_mask);
	regmap_write(fsl_dev->regmap, DCU_INT_MASK, int_mask &
		     ~DCU_INT_MASK_VBLANK);
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE, DCU_UPDATE_MODE_READREG);

	return 0;
}

static int fsl_dcu_load(struct drm_device *dev, unsigned long flags)
{
	struct platform_device *pdev = dev->platformdev;
	struct fsl_dcu_drm_device *fsl_dev;
	struct resource *res;
	void __iomem *base;
	int ret;

	fsl_dev = devm_kzalloc(&pdev->dev, sizeof(*fsl_dev), GFP_KERNEL);
	if (!fsl_dev)
		return -ENOMEM;

	fsl_dev->dev = &pdev->dev;
	fsl_dev->ddev = dev;
	fsl_dev->np = pdev->dev.of_node;
	dev->dev_private = fsl_dev;
	dev_set_drvdata(dev->dev, fsl_dev);
	drm_dev_set_unique(dev, dev_name(dev->dev));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "could not get memory IO resource\n");
		return -ENODEV;
	}

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		return ret;
	}

	fsl_dev->clk = devm_clk_get(&pdev->dev, "dcu");
	if (IS_ERR(fsl_dev->clk)) {
		ret = PTR_ERR(fsl_dev->clk);
		dev_err(&pdev->dev, "could not get clock\n");
		return ret;
	}
	clk_prepare_enable(fsl_dev->clk);
	fsl_dev->regmap = devm_regmap_init_mmio(&pdev->dev, base,
			&fsl_dcu_regmap_config);
	if (IS_ERR(fsl_dev->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(fsl_dev->regmap);
	}

	/* Put TCON in bypass mode, so the input signals from DCU are passed
	 * through TCON unchanged */
	fsl_dcu_bypass_tcon(fsl_dev, fsl_dev->np);

	if (of_device_is_compatible(fsl_dev->np, "fsl,ls1021a-dcu")) {
		pixclk_register(fsl_dev, fsl_dev->np);
		clk_prepare(fsl_dev->pixclk);
		clk_enable(fsl_dev->pixclk);
	}

	ret = fsl_dcu_drm_modeset_init(fsl_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize mode setting\n");
		return ret;
	}

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to initialize vblank\n");
		goto done;
	}

	ret = fsl_dcu_drm_irq_init(dev);
	if (ret < 0)
		goto done;

	fsl_dcu_fbdev_init(dev);

	return 0;
done:
	if (ret)
		fsl_dcu_unload(dev);

	return ret;
}

static void fsl_dcu_drm_preclose(struct drm_device *dev, struct drm_file *file)
{
}

static irqreturn_t fsl_dcu_drm_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct fsl_dcu_drm_device *fsl_dev = dev->dev_private;
	unsigned int int_status;

	regmap_read(fsl_dev->regmap, DCU_INT_STATUS, &int_status);
	if (int_status & DCU_INT_STATUS_VBLANK)
		drm_handle_vblank(dev, 0);

	regmap_write(fsl_dev->regmap, DCU_INT_STATUS, 0xffffffff);
	regmap_write(fsl_dev->regmap, DCU_UPDATE_MODE, DCU_UPDATE_MODE_READREG);

	return IRQ_HANDLED;
}

static int fsl_dcu_drm_enable_vblank(struct drm_device *dev, int crtc)
{
	return 0;
}

static void fsl_dcu_drm_disable_vblank(struct drm_device *dev, int crtc)
{
}

static const struct file_operations fsl_dcu_drm_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.mmap		= drm_gem_cma_mmap,
};

static struct drm_driver fsl_dcu_drm_driver = {
	.driver_features	= DRIVER_HAVE_IRQ | DRIVER_GEM | DRIVER_MODESET
				| DRIVER_PRIME,
	.load			= fsl_dcu_load,
	.unload			= fsl_dcu_unload,
	.preclose		= fsl_dcu_drm_preclose,
	.irq_handler		= fsl_dcu_drm_irq,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= fsl_dcu_drm_enable_vblank,
	.disable_vblank		= fsl_dcu_drm_disable_vblank,
	.gem_free_object	= drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.fops			= &fsl_dcu_drm_fops,
	.name			= "fsl-dcu-drm",
	.desc			= "Freescale DCU DRM",
	.date			= "20150213",
	.major			= 1,
	.minor			= 0,
};

#ifdef CONFIG_PM_SLEEP
static int fsl_dcu_drm_pm_suspend(struct device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev_get_drvdata(dev);

	if (of_device_is_compatible(fsl_dev->np, "fsl,ls1021a-dcu"))
		clk_disable(fsl_dev->pixclk);

	drm_kms_helper_poll_disable(fsl_dev->ddev);
	regcache_cache_only(fsl_dev->regmap, true);
	regcache_mark_dirty(fsl_dev->regmap);
	clk_disable_unprepare(fsl_dev->clk);

	if (fsl_dev->tcon_regmap) {
		regcache_cache_only(fsl_dev->tcon_regmap, true);
		regcache_mark_dirty(fsl_dev->tcon_regmap);
		clk_disable_unprepare(fsl_dev->tcon_clk);
	}

	return 0;
}

static int fsl_dcu_drm_pm_resume(struct device *dev)
{
	struct fsl_dcu_drm_device *fsl_dev = dev_get_drvdata(dev);

	/* Enable clocks and restore all registers */
	if (fsl_dev->tcon_regmap) {
		clk_prepare_enable(fsl_dev->tcon_clk);
		regcache_cache_only(fsl_dev->tcon_regmap, false);
		regcache_sync(fsl_dev->tcon_regmap);
	}

	clk_prepare_enable(fsl_dev->clk);
	drm_kms_helper_poll_enable(fsl_dev->ddev);
	regcache_cache_only(fsl_dev->regmap, false);
	regcache_sync(fsl_dev->regmap);

	if (of_device_is_compatible(fsl_dev->np, "fsl,ls1021a-dcu"))
		clk_enable(fsl_dev->pixclk);

	return 0;
}
#endif

static const struct dev_pm_ops fsl_dcu_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fsl_dcu_drm_pm_suspend, fsl_dcu_drm_pm_resume)
};

static int fsl_dcu_drm_probe(struct platform_device *pdev)
{
	return drm_platform_init(&fsl_dcu_drm_driver, pdev);
}

static int fsl_dcu_drm_remove(struct platform_device *pdev)
{
	struct fsl_dcu_drm_device *fsl_dev = platform_get_drvdata(pdev);

	drm_put_dev(fsl_dev->ddev);

	return 0;
}

static const struct of_device_id fsl_dcu_of_match[] = {
		{ .compatible = "fsl,ls1021a-dcu", },
		{ .compatible = "fsl,vf610-dcu", },
		{ },
};
MODULE_DEVICE_TABLE(of, fsl_dcu_of_match);

static struct platform_driver fsl_dcu_drm_platform_driver = {
	.probe		= fsl_dcu_drm_probe,
	.remove		= fsl_dcu_drm_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "fsl,dcu",
		.pm	= &fsl_dcu_drm_pm_ops,
		.of_match_table = fsl_dcu_of_match,
	},
};

module_platform_driver(fsl_dcu_drm_platform_driver);

MODULE_ALIAS("platform:fsl-dcu-drm");
MODULE_DESCRIPTION("Freescale DCU DRM Driver");
MODULE_LICENSE("GPL");
