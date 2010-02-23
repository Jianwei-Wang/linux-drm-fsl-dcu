#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"

#include <linux/videodev2.h>
#include <media/tuner.h>
#include <media/v4l2-chip-ident.h>
#include <media/i2c-addr.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/videobuf-dma-sg.h>

#define DRIVER_NAME		"radeon"

struct radeon_video_fh {
	struct radeon_device *rdev;
	enum v4l2_buf_type type;

	unsigned int width, height;
//	struct videobuf_queue vidq;
};

void radeon_parse_multimedia_table(struct radeon_device *rdev,
				   uint16_t offset)
{
	uint8_t tuner, audio_chip;
	uint8_t decoder_type, decoder_config;
	char *dec_s = NULL;

	tuner = RBIOS8(offset);
	tuner &= 0x1f;
	if (tuner == 0x00 || tuner == 0x1f)
		rdev->mm.tuner_type = RADEON_TUNER_NONE;
	else if (tuner <= 0x10) /* lies */
		rdev->mm.tuner_type = RADEON_TUNER_FI1236;
	else if (tuner <= 0x1e) /* less lies */
		rdev->mm.tuner_type = RADEON_TUNER_FRONT_BACK_9885;

	audio_chip = RBIOS8(offset + 1) & 0xf;
	rdev->mm.audio_type = RADEON_AUDIO_NONE;
	if (audio_chip == 0x2 || audio_chip == 0x6)
		rdev->mm.audio_type = RADEON_AUDIO_TDA9850;
	else if (audio_chip == 0x8 || audio_chip == 0x9)
		rdev->mm.audio_type = RADEON_AUDIO_MSP34XX;

	decoder_type = RBIOS8(offset + 5) & 0xf;
	if (decoder_type == 0x6)
		DRM_INFO("rage theater decoder reported in BIOS\n");

	decoder_config = RBIOS8(offset + 6) & 0x7;
	switch(decoder_config) {
	case 0:	dec_s = "I2C";	break;
	case 1: dec_s = "MPP"; break;
	case 2: dec_s = "VIP 2-bit"; break;
	case 3: dec_s = "VIP 4-bit"; break;
	case 4: dec_s = "VIP 8-bit"; break;
	case 7: dec_s = "PCI"; break;
	default:
		break;
	}

	if (dec_s) {
		DRM_INFO("Decoder configuration is %s device\n", dec_s);
	}
}

static LIST_HEAD(radeon_devlist);

static int video_open(struct file *file)
{
	int minor = video_devdata(file)->minor;
	struct radeon_video_fh *fh;
	enum v4l2_buf_type type = 0;
	struct radeon_device *rdev = NULL, *d;

	list_for_each_entry(d, &radeon_devlist, mm.devlist) {
		if (d->mm.video_dev->minor == minor) {
			rdev = d;
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		}
	}
	if (rdev == NULL) {
		return -ENODEV;
	}

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (fh == NULL) {
		return -ENOMEM;
	}

	file->private_data = fh;
	fh->rdev = rdev;
	      
	return 0;
}

static ssize_t
video_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct radeon_video_fh *fh = file->private_data;
	switch (fh->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
//		if (res_locked(fh->rdev->mm.video_dev, RESOURCE_VIDEO))
//			return -EBUSY;

//		return videobuf_read_one();
		break;
	default:
		break;
	}
	return -ENOSYS;
}

static unsigned int
video_poll(struct file *file, struct poll_table_struct *wait)
{
	unsigned int rc = POLLERR;

	rc = 0;
	return rc;
}

static int video_release(struct file *file)
{
	struct radeon_video_fh *fh = file->private_data;

	file->private_data = NULL;
	kfree(fh);
	return 0;
}

static int
video_mmap(struct file *file, struct vm_area_struct * vma)
{
	return -ENOSYS;
}

static int vidioc_querycap (struct file *file, void  *priv,
			    struct v4l2_capability *cap)
{
	struct radeon_device *rdev = ((struct radeon_video_fh *)priv)->rdev;
	strcpy(cap->driver, "radeon");
	strcpy(cap->card, "radeon");

	cap->version = 0;
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_READWRITE |
		V4L2_CAP_STREAMING;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
}

static const struct v4l2_file_operations video_fops = {
	.owner = THIS_MODULE,
	.open = video_open,
	.release = video_release,
	.read = video_read,
	.poll = video_poll,
	.mmap = video_mmap,
	.ioctl = video_ioctl2,
};

static int vidioc_enum_fmt_vid_cap (struct file *file, void  *priv,
				    struct v4l2_fmtdesc *f)
{
	if (unlikely(f->index >= ARRAY_SIZE(formats)))
		return -EINVAL;

	strlcpy(f->description,formats[f->index].name,sizeof(f->description));
	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct radeon_video_fh *fh = priv;

	f->fmt.pix.width = fh->width;
	f->fmt.pix.height = fh->height;
	f->fmt.pix.field = fh->vidq.field;
	f->fmt.pix.pixelformat = fh->fmt->fourcc;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fh->fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}


static const struct v4l2_ioctl_ops video_ioctl_ops = {
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
#if 0
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_g_fmt_vbi_cap     = cx8800_vbi_fmt,
	.vidioc_try_fmt_vbi_cap   = cx8800_vbi_fmt,
	.vidioc_s_fmt_vbi_cap     = cx8800_vbi_fmt,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_s_std         = vidioc_s_std,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
	.vidiocgmbuf          = vidiocgmbuf,
#endif
	.vidioc_g_tuner       = vidioc_g_tuner,
	.vidioc_s_tuner       = vidioc_s_tuner,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_s_frequency   = vidioc_s_frequency,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register    = vidioc_g_register,
	.vidioc_s_register    = vidioc_s_register,
#endif
#endif
};

static struct video_device radeon_video_template = {
	.name = "radeon-aiw",
	.fops = &video_fops,
	.minor = -1,
	.ioctl_ops = &video_ioctl_ops,
	.tvnorms = V4L2_STD_ALL,
	.current_norm = V4L2_STD_NTSC_M,
};

struct video_device *radeon_vdev_init(struct radeon_device *rdev,
				      struct video_device *template,
				      char *type)
{
	struct video_device *vfd;

	vfd = video_device_alloc();
	if (vfd == NULL)
		return NULL;
	*vfd = *template;
	vfd->v4l2_dev = &rdev->mm.v4l2_dev;
	vfd->parent = &rdev->pdev->dev;
	vfd->release = video_device_release;
	snprintf(vfd->name, sizeof(vfd->name), "%s %s (%s)",
		 "radeon", type, "radeon");
	return vfd;
}

int radeon_v4l2_init(struct radeon_device *rdev)
{
	int err;
	rdev->mm.video_dev = radeon_vdev_init(rdev, &radeon_video_template,
					      "video");
	err = video_register_device(rdev->mm.video_dev, VFL_TYPE_GRABBER, 0);
	if (err < 0) {
		printk(KERN_ERR "%s/0: can't register video device\n",
		       "radeon");
		goto fail_unreg;

	}
	printk(KERN_INFO "%s/0: register device video%d [v4l2]\n",
	       "radeon", rdev->mm.video_dev->num);

	list_add_tail(&rdev->mm.devlist, &radeon_devlist);
	return 0;
fail_unreg:
	return err;
}

void radeon_v4l2_fini(struct radeon_device *rdev)
{
	if (rdev->mm.video_dev) {
		if (rdev->mm.video_dev->minor != -1)
			video_unregister_device(rdev->mm.video_dev);
		else
			video_device_release(rdev->mm.video_dev);
		rdev->mm.video_dev = NULL;
	}
	list_del(&rdev->mm.devlist);
}

/* setup mm i2c and VIP buses and detect */
bool radeon_multimedia_init(struct radeon_device *rdev)
{
	bool ret;
	bool has_demod = false;
	struct v4l2_subdev *tuner, *audio;
	if (rdev->is_atom_bios)
		ret = radeon_atom_get_multimedia(rdev);
	else
		ret = radeon_combios_get_multimedia(rdev);

	if (ret == false)
		return false;

	DRM_INFO("BIOS has multimedia table\n");

	radeon_vip_init(rdev);
	radeon_vip_theatre_detect(rdev);

	if (rdev->is_atom_bios)
		ret = atombios_add_mm_i2c_bus(rdev);
	else
		ret = radeon_combios_add_mm_i2c_bus(rdev);
	if (ret == false)
		return false;

	/* register V4l devices */
	strcpy(rdev->mm.v4l2_dev.name, DRIVER_NAME);
	if (v4l2_device_register(NULL, &rdev->mm.v4l2_dev)) {
		return false;
	}

	if (rdev->mm.tuner_type == RADEON_TUNER_FRONT_BACK_9885) {
		tuner = v4l2_i2c_new_subdev(&rdev->mm.v4l2_dev,
				    &rdev->mm.i2c_bus->adapter,
				    "tuner", "tuner",
				    0, v4l2_i2c_tuner_addrs(ADDRS_DEMOD));

		if (!tuner)
			DRM_ERROR("Unable to find tuner\n");
		has_demod = true;
	}

	if (rdev->mm.tuner_type != RADEON_TUNER_NONE) {
		tuner = v4l2_i2c_new_subdev(&rdev->mm.v4l2_dev,
					    &rdev->mm.i2c_bus->adapter,
					    "tuner", "tuner",
					    0, v4l2_i2c_tuner_addrs(has_demod ? ADDRS_TV_WITH_DEMOD : ADDRS_TV));
		if (!tuner)
			DRM_ERROR("Unable to find tuner\n");
	}

	if (rdev->mm.audio_type == RADEON_AUDIO_MSP34XX) {
		static const unsigned short addrs[] = {
			I2C_ADDR_MSP3400 >> 1,
			I2C_ADDR_MSP3400_ALT >> 1,
			I2C_CLIENT_END
		};
		audio = v4l2_i2c_new_subdev(&rdev->mm.v4l2_dev,
					    &rdev->mm.i2c_bus->adapter,
					    "msp3400", "msp3400", 0, addrs);
		if (!audio) {
			DRM_ERROR("Unable to locate msp3400\n");
		}
	}

	ret = radeon_v4l2_init(rdev);
		
	rdev->mm.initialised = true;
	return true;
}


void radeon_multimedia_fini(struct radeon_device *rdev)
{
	if (rdev->mm.initialised == false)
		return;

	radeon_v4l2_fini(rdev);

	if (rdev->mm.i2c_bus)
		radeon_i2c_destroy(rdev->mm.i2c_bus);

	v4l2_device_unregister(&rdev->mm.v4l2_dev);
}
