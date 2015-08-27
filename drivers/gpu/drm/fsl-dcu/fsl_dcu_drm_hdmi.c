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

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/fsl_devices.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/backlight.h>
#include <video/videomode.h>
#include <video/of_display_timing.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

#include "fsl_dcu_drm_drv.h"
#include "fsl_dcu_drm_output.h"

#define SII902X_INPUT_BUS_FMT	0x08
#define SII902X_TPI_AVI_INPUT_FMT	0x09
#define SII902X_TPI_AVI_OUTPUT_FMT	0x0A
#define SII902X_SYS_CONTROL	0x1A
#define SII902X_SYS_CTR_DDC_REQ	BIT(2)
#define SII902X_SYS_CTR_DDC_BUS_AVAI	(BIT(2) | BIT(1))
#define SII902X_TPI_FAMILY_DEV_ID	0x1B
#define SII902X_TPI_DEV_REV_ID	0x1C
#define SII902X_TPI_REV_LEVEL_ID	0x1D
#define SII902X_POWER_STATE	0x1E
#define SII902X_TPI_AUDIO_CFG0	0x24
#define SII902X_TPI_AUDIO_CFG1	0x25
#define SII902X_TPI_AUDIO_CFG2	0x26
#define SII902X_TPI_AUDIO_CFG3	0x27
#define SII902X_TPI_HDCP_REV	0x30
#define SII902X_TPI_INT_ENABLE	0x3C
#define SII902X_TPI_INT_STATUS	0x3D
#define SII902X_TPI_INT_PLUG_IN	BIT(2)
#define SII902X_GENERAL_PURPOSE_IO0	0xBC
#define SII902X_GENERAL_PURPOSE_IO1	0xBD
#define SII902X_GENERAL_PURPOSE_IO2	0xBE
#define SII902X_TRANS_MODE_DIFF	0xC7

bool g_enable_hdmi;

struct sii902x_data {
	struct i2c_client *client;
	struct delayed_work det_work;
	struct fb_info *fbi;
	struct fsl_dcu_drm_hdmicon *hdmicon;
} *sii902x;

static struct i2c_client *sii902x_to_i2c(struct sii902x_data *sii902x)
{
	return sii902x->client;
}

static s32 sii902x_write(const struct i2c_client *client,
			 u8 command, u8 value)
{
	return i2c_smbus_write_byte_data(client, command, value);
}

static s32 sii902x_read(const struct i2c_client *client, u8 command)
{
	int val;

	val = i2c_smbus_read_word_data(client, command);

	return val & 0xff;
}

static void sii902x_power_up_tx(struct sii902x_data *sii902x)
{
	struct i2c_client *client = sii902x_to_i2c(sii902x);
	int val;

	val = sii902x_read(client, SII902X_POWER_STATE);
	val &= ~0x3;
	sii902x_write(client, SII902X_POWER_STATE, val);
}

static int sii902x_get_edid_preconfig(void)
{
	int old, dat, ret = 0, cnt = 100;

	old = sii902x_read(sii902x->client, SII902X_SYS_CONTROL);

	sii902x_write(sii902x->client, SII902X_SYS_CONTROL,
		      old | SII902X_SYS_CTR_DDC_REQ);
	do {
		cnt--;
		msleep(20);
		dat = sii902x_read(sii902x->client, SII902X_SYS_CONTROL);
	} while ((!(dat & 0x2)) && cnt);

	if (!cnt) {
		ret = -1;
		goto done;
	}

	sii902x_write(sii902x->client, SII902X_SYS_CONTROL,
		      old | SII902X_SYS_CTR_DDC_BUS_AVAI);

done:
	sii902x_write(sii902x->client, SII902X_SYS_CONTROL, old);
	return ret;
}

struct edid *sii902x_drm_get_edid(struct drm_connector *connector,
				  struct i2c_adapter *adapter)
{
	int old, dat, cnt = 100;
	struct edid *edid;

	old = sii902x_read(sii902x->client, SII902X_SYS_CONTROL);

	sii902x_write(sii902x->client, SII902X_SYS_CONTROL,
		      old | SII902X_SYS_CTR_DDC_REQ);
	do {
		cnt--;
		msleep(20);
		dat = sii902x_read(sii902x->client, SII902X_SYS_CONTROL);
	} while ((!(dat & 0x2)) && cnt);

	if (!cnt) {
		edid = NULL;
		goto done;
	}

	sii902x_write(sii902x->client, SII902X_SYS_CONTROL,
		      old | SII902X_SYS_CTR_DDC_BUS_AVAI);

	/* edid reading */
	edid = drm_get_edid(connector, adapter);

	cnt = 100;
	do {
		cnt--;
		sii902x_write(sii902x->client, SII902X_SYS_CONTROL,
			      old & ~SII902X_SYS_CTR_DDC_BUS_AVAI);
		msleep(20);
		dat = sii902x_read(sii902x->client, SII902X_SYS_CONTROL);
	} while ((dat & 0x6) && cnt);

	if (!cnt)
		edid = NULL;

done:
	sii902x_write(sii902x->client, SII902X_SYS_CONTROL, old);
	return edid;
}

static void det_worker(struct work_struct *work)
{
	struct fb_info *fbi = sii902x->fbi;
	struct fsl_dcu_drm_hdmicon *hdmicon = sii902x->hdmicon;
	struct drm_device *dev = hdmicon->connector.dev;
	int val;

	val = sii902x_read(sii902x->client, SII902X_TPI_INT_STATUS);
	if (!(val & 0x1) && !g_enable_hdmi)
		goto err;

	/* cable connection changes */
	if (val & SII902X_TPI_INT_PLUG_IN || g_enable_hdmi) {
		hdmicon->status = connector_status_connected;

		/* make sure fb is powerdown */
		console_lock();
		fb_blank(fbi, FB_BLANK_POWERDOWN);
		console_unlock();

		console_lock();
		fb_blank(fbi, FB_BLANK_UNBLANK);
		console_unlock();
	} else {
		console_lock();
		fb_blank(fbi, FB_BLANK_POWERDOWN);
		console_unlock();
	}
	drm_helper_hpd_irq_event(dev);

err:
	sii902x_write(sii902x->client, SII902X_TPI_INT_STATUS, val);
}

static irqreturn_t sii902x_detect_handler(int irq, void *data)
{
	if (g_enable_hdmi)
		g_enable_hdmi = false;

	if (sii902x->fbi)
		schedule_delayed_work(&sii902x->det_work,
				      msecs_to_jiffies(20));
	return IRQ_HANDLED;
}

static void sii902x_poweron(void)
{
	/* Turn on DVI or HDMI */
	sii902x_write(sii902x->client, SII902X_SYS_CONTROL, 0x00);
}

static void sii902x_poweroff(void)
{
	/* disable tmds before changing resolution */
	sii902x_write(sii902x->client, SII902X_SYS_CONTROL, 0x10);
}

static int sii902x_fb_event(struct notifier_block *nb,
			    unsigned long val, void *v)
{
	struct fb_event *event = v;
	struct fb_info *fbi = event->info;

	switch (val) {
	case FB_EVENT_FB_REGISTERED:
		if (sii902x->fbi)
			break;
		sii902x->fbi = fbi;
		if (g_enable_hdmi && sii902x->fbi) {
			schedule_delayed_work(&sii902x->det_work,
					      msecs_to_jiffies(20));
		}
		break;
	case FB_EVENT_MODE_CHANGE:
		break;
	case FB_EVENT_BLANK:
		if (*((int *)event->data) == FB_BLANK_UNBLANK)
			sii902x_poweron();
		else
			sii902x_poweroff();
		break;
	}

	return 0;
}

static void sii902x_chip_id(struct sii902x_data *sii902x)
{
	struct i2c_client *client = sii902x_to_i2c(sii902x);
	int val;

	/* read device ID */
	val = sii902x_read(client, SII902X_TPI_FAMILY_DEV_ID);
	pr_info("Sii902x: read id = 0x%02X", val);
	val = sii902x_read(client, SII902X_TPI_DEV_REV_ID);
	pr_info("-0x%02X", val);
	val = sii902x_read(client, SII902X_TPI_REV_LEVEL_ID);
	pr_info("-0x%02X", val);
	val = sii902x_read(client, SII902X_TPI_HDCP_REV);
	pr_info("-0x%02X\n", val);
}

static int sii902x_initialize(struct sii902x_data *sii902x)
{
	struct i2c_client *client = sii902x_to_i2c(sii902x);
	int ret, cnt;

	for (cnt = 0; cnt < 5; cnt++) {
		/* Set 902x in hardware TPI mode on and jump out of D3 state */
		ret = sii902x_write(client, SII902X_TRANS_MODE_DIFF, 0x00);
		if (ret < 0)
			break;
	}
	if (0 != ret)
		dev_err(&client->dev, "cound not find device\n");

	return ret;
}

static void sii902x_enable_source(struct sii902x_data *sii902x)
{
	struct i2c_client *client = sii902x_to_i2c(sii902x);
	int val;

	sii902x_write(client, SII902X_GENERAL_PURPOSE_IO0, 0x01);
	sii902x_write(client, SII902X_GENERAL_PURPOSE_IO1, 0x82);
	val = sii902x_read(client, SII902X_GENERAL_PURPOSE_IO2);
	val |= 0x1;
	sii902x_write(client, SII902X_GENERAL_PURPOSE_IO2, val);
}

static struct notifier_block nb = {
	.notifier_call = sii902x_fb_event,
};

static int sii902x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adap = to_i2c_adapter(client->dev.parent);
	int ret, err;

	if (!g_enable_hdmi)
		return -EPERM;

	if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_BYTE)) {
		dev_err(&client->dev, "i2c_check_functionality error\n");
		return -ENODEV;
	}

	sii902x = devm_kzalloc(&client->dev, sizeof(*sii902x), GFP_KERNEL);
	if (!sii902x)
		return -ENOMEM;

	sii902x->client = client;
	i2c_set_clientdata(client, sii902x);

	err = sii902x_initialize(sii902x);
	if (err)
		return err;

	sii902x_chip_id(sii902x);
	sii902x_power_up_tx(sii902x);
	sii902x_enable_source(sii902x);

	if (sii902x_get_edid_preconfig() < 0)
		dev_warn(&client->dev, "Read edid preconfig failed\n");

	if (client->irq) {
		ret = devm_request_irq(&client->dev, client->irq,
				       sii902x_detect_handler, 0,
				       "SII902x_det", sii902x);
		if (ret < 0)
			dev_warn(&client->dev,
				 "cound not request det irq %d\n",
				 client->irq);
		else {
			INIT_DELAYED_WORK(&sii902x->det_work, det_worker);
			/*enable cable hot plug irq*/
			sii902x_write(client, SII902X_TPI_INT_ENABLE, 0x01);
		}
	}

	fb_register_client(&nb);

	return 0;
}

static int sii902x_remove(struct i2c_client *client)
{
	fb_unregister_client(&nb);
	sii902x_poweroff();
	return 0;
}

static const struct i2c_device_id sii902x_id[] = {
	{ "sii902x", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sii902x_id);

static const struct of_device_id sii902x_dt_ids[] = {
	{ .compatible = "sii902x", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sii902x_dt_ids);

static struct i2c_driver sii902x_i2c_driver = {
	.driver = {
		.name = "sii902x",
		.owner = THIS_MODULE,
		.of_match_table = sii902x_dt_ids,
	},
	.probe = sii902x_probe,
	.remove = sii902x_remove,
	.id_table = sii902x_id,
};

static int __init enable_hdmi_setup(char *str)
{
	g_enable_hdmi = true;

	return 1;
}
__setup("hdmi", enable_hdmi_setup);

static int
fsl_dcu_drm_hdmienc_atomic_check(struct drm_encoder *encoder,
				 struct drm_crtc_state *crtc_state,
				 struct drm_connector_state *conn_state)
{
	return 0;
}

static void fsl_dcu_drm_hdmienc_disable(struct drm_encoder *encoder)
{
}

static void fsl_dcu_drm_hdmienc_enable(struct drm_encoder *encoder)
{
}

static void fsl_dcu_drm_hdmienc_mode_set(struct drm_encoder *encoder,
					 struct drm_display_mode *mode,
					 struct drm_display_mode *adjusted_mode)
{
	struct videomode vm;
	u16 data[4];
	u32 refresh;
	u8 *tmp;
	int i;

	/* Power up */
	sii902x_power_up_tx(sii902x);

	drm_display_mode_to_videomode(mode, &vm);
	data[0] = PICOS2KHZ(vm.pixelclock) / 10;
	data[2] = vm.hsync_len + vm.hback_porch +
		  vm.hactive + vm.hfront_porch;
	data[3] = vm.vsync_len + vm.vfront_porch +
		  vm.vactive + vm.vback_porch;
	refresh = data[2] * data[3];
	refresh = (PICOS2KHZ(vm.pixelclock) * 1000) / refresh;
	data[1] = refresh * 100;

	tmp = (u8 *)data;
	for (i = 0; i < 8; i++)
		sii902x_write(sii902x->client, i, tmp[i]);

	/* input bus/pixel: full pixel wide (24bit), rising edge */
	sii902x_write(sii902x->client, SII902X_INPUT_BUS_FMT, 0x70);
	/* Set input format to RGB */
	sii902x_write(sii902x->client, SII902X_TPI_AVI_INPUT_FMT, 0x00);
	/* set output format to RGB */
	sii902x_write(sii902x->client, SII902X_TPI_AVI_OUTPUT_FMT, 0x00);
	/* audio setup */
	sii902x_write(sii902x->client, SII902X_TPI_AUDIO_CFG1, 0x00);
	sii902x_write(sii902x->client, SII902X_TPI_AUDIO_CFG2, 0x40);
	sii902x_write(sii902x->client, SII902X_TPI_AUDIO_CFG3, 0x00);
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.atomic_check = fsl_dcu_drm_hdmienc_atomic_check,
	.disable = fsl_dcu_drm_hdmienc_disable,
	.enable = fsl_dcu_drm_hdmienc_enable,
	.mode_set = fsl_dcu_drm_hdmienc_mode_set,
};

static void fsl_dcu_drm_hdmienc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = fsl_dcu_drm_hdmienc_destroy,
};

int fsl_dcu_drm_hdmienc_create(struct fsl_dcu_drm_device *fsl_dev,
			       struct drm_crtc *crtc)
{
	struct drm_encoder *encoder;
	int ret;

	encoder = devm_kzalloc(fsl_dev->dev, sizeof(*encoder), GFP_KERNEL);
	if (!encoder)
		return -1;

	encoder->possible_crtcs = 1;
	ret = drm_encoder_init(fsl_dev->drm, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_TMDS);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);
	encoder->crtc = crtc;

	return 0;
}

	struct fsl_dcu_drm_hdmicon *hdmicon;
#define to_fsl_dcu_drm_hdmicon(connector) \
	container_of(connector, struct fsl_dcu_drm_hdmicon, connector)

static struct drm_encoder *fsl_dcu_drm_hdmi_find_encoder(struct drm_device *dev)
{
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_TMDS)
			return encoder;
	}

	return NULL;
}

static void fsl_dcu_drm_hdmicon_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static enum drm_connector_status
fsl_dcu_drm_hdmicon_detect(struct drm_connector *connector, bool force)
{
	struct fsl_dcu_drm_hdmicon *hdmicon = to_fsl_dcu_drm_hdmicon(connector);

	if (hdmicon->status == connector_status_disconnected)
		return connector_status_disconnected;
	else
		return connector_status_connected;
}

static const struct drm_connector_funcs fsl_dcu_drm_connector_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.destroy = fsl_dcu_drm_hdmicon_destroy,
	.detect = fsl_dcu_drm_hdmicon_detect,
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.reset = drm_atomic_helper_connector_reset,
};

static struct drm_encoder *
fsl_dcu_drm_hdmicon_best_encoder(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;

	return fsl_dcu_drm_hdmi_find_encoder(dev);
}

static int fsl_dcu_drm_hdmicon_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct i2c_adapter *adap = to_i2c_adapter(sii902x->client->dev.parent);
	struct edid *edid;
	struct drm_display_mode *mode;
	int ret;

	edid = sii902x_drm_get_edid(connector, adap);
	if (edid) {
		drm_mode_connector_update_edid_property(connector, edid);
		ret = drm_add_edid_modes(connector, edid);
		kfree(edid);
	} else {
		dev_dbg(dev->dev, "failed to get edid\n");
	}

	list_for_each_entry(mode, &connector->probed_modes, head) {
		if (mode->hdisplay == 1024 || mode->vdisplay == 768)
			mode->type |= DRM_MODE_TYPE_PREFERRED;
		else
			mode->type &= ~DRM_MODE_TYPE_PREFERRED;
	}

	return ret;
}

static int fsl_dcu_drm_hdmicon_mode_valid(struct drm_connector *connector,
					  struct drm_display_mode *mode)
{
	if (mode->hdisplay > 1024)
		return MODE_VIRTUAL_X;
	else if	(mode->vdisplay > 768)
		return MODE_VIRTUAL_Y;

	return MODE_OK;
}

static const struct drm_connector_helper_funcs connector_helper_funcs = {
	.best_encoder = fsl_dcu_drm_hdmicon_best_encoder,
	.get_modes = fsl_dcu_drm_hdmicon_get_modes,
	.mode_valid = fsl_dcu_drm_hdmicon_mode_valid,
};

int fsl_dcu_drm_hdmicon_create(struct fsl_dcu_drm_device *fsl_dev)
{
	struct drm_device *dev = fsl_dev->drm;
	struct fsl_dcu_drm_hdmicon *hdmicon;
	struct drm_connector *connector;
	struct drm_encoder *encoder;
	int ret;

	ret = i2c_add_driver(&sii902x_i2c_driver);
	if (!ret) {
		dev_err(fsl_dev->dev, "Register i2c driver failed\n");
		return ret;
	}

	hdmicon = devm_kzalloc(fsl_dev->dev,
			       sizeof(struct fsl_dcu_drm_hdmicon), GFP_KERNEL);
	if (!hdmicon)
		return -ENOMEM;

	connector = &hdmicon->connector;
	sii902x->hdmicon = hdmicon;

	connector->display_info.width_mm = 0;
	connector->display_info.height_mm = 0;
	connector->polled = DRM_CONNECTOR_POLL_HPD;

	ret = drm_connector_init(fsl_dev->drm, connector,
				 &fsl_dcu_drm_connector_funcs,
				 DRM_MODE_CONNECTOR_HDMIA);
	if (ret < 0)
		return ret;

	connector->dpms = DRM_MODE_DPMS_OFF;
	drm_connector_helper_add(connector, &connector_helper_funcs);
	ret = drm_connector_register(connector);
	if (ret < 0)
		goto err_cleanup;

	encoder = fsl_dcu_drm_hdmi_find_encoder(dev);
	if (!encoder)
		goto err_cleanup;
	ret = drm_mode_connector_attach_encoder(connector, encoder);
	if (ret < 0)
		goto err_sysfs;

	connector->encoder = encoder;

	drm_object_property_set_value
		(&connector->base, fsl_dev->drm->mode_config.dpms_property,
		DRM_MODE_DPMS_OFF);

	return 0;

err_sysfs:
	drm_connector_unregister(connector);
err_cleanup:
	drm_connector_cleanup(connector);
	return ret;
}

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("SII902x DVI/HDMI driver");
MODULE_LICENSE("GPL");
