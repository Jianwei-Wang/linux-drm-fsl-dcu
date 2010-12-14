#include "drmP.h"
#include "drm_crtc.h"
#include "drm_edid.h"
#include "drm_crtc_helper.h"
#include "udl_drv.h"

/* dummy connector to just get EDID,
   all UDL appear to have a DVI-D */

static u8 *udl_get_edid(struct udl_device *udl)
{
	u8 *block;
	char rbuf[3];
	int ret, i;

	if ((block = kmalloc(EDID_LENGTH, GFP_KERNEL)) == NULL)
		return NULL;

	for (i = 0; i < EDID_LENGTH; i++) {
		ret = usb_control_msg(udl->udev,
				      usb_rcvctrlpipe(udl->udev, 0), (0x02),
				      (0x80 | (0x02 << 5)), i << 8, 0xA1, rbuf, 2,
				      HZ);
		if (ret < 1) {
			DRM_ERROR("Read EDID byte %d failed err %x\n", i, ret);
			i--;
			goto error;
		}
		block[i] = rbuf[1];
	}

	return block;

error:
	kfree(block);
	return NULL;
}

static int udl_get_modes(struct drm_connector *connector)
{
	struct udl_device *udl = connector->dev->dev_private;
	struct edid *edid;
	int ret;

	edid = (struct edid *)udl_get_edid(udl);

	connector->display_info.raw_edid = (char *)edid;

	drm_mode_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	connector->display_info.raw_edid = NULL;
	kfree(edid);
	return ret;
}

static int udl_mode_valid(struct drm_connector *connector,
			  struct drm_display_mode *mode)
{
	return 0;
}

static enum drm_connector_status
udl_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

struct drm_encoder *udl_best_single_encoder(struct drm_connector *connector)
{
	int enc_id = connector->encoder_ids[0];
	struct drm_mode_object *obj;
	struct drm_encoder *encoder;

	obj = drm_mode_object_find(connector->dev, enc_id, DRM_MODE_OBJECT_ENCODER);
	if (!obj)
	  return NULL;
	encoder = obj_to_encoder(obj);
	return encoder;
}

int udl_connector_set_property(struct drm_connector *connector, struct drm_property *property,
			       uint64_t val)
{
	return 0;
}

static void udl_connector_destroy(struct drm_connector *connector)
{
	drm_sysfs_connector_remove(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

struct drm_connector_helper_funcs udl_connector_helper_funcs = {
	.get_modes = udl_get_modes,
	.mode_valid = udl_mode_valid,
	.best_encoder = udl_best_single_encoder,
};

struct drm_connector_funcs udl_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = udl_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = udl_connector_destroy,
	.set_property = udl_connector_set_property,
};

int udl_connector_init(struct drm_device *dev, struct drm_encoder *encoder)
{
	struct drm_connector *connector;

	connector = kzalloc(sizeof(struct drm_connector), GFP_KERNEL);
	if (!connector)
		return -ENOMEM;

	drm_connector_init(dev, connector, &udl_connector_funcs, DRM_MODE_CONNECTOR_DVII);
	drm_connector_helper_add(connector, &udl_connector_helper_funcs);

	drm_sysfs_connector_add(connector);
	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}
