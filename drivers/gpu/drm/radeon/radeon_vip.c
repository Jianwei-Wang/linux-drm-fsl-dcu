#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"

#include <linux/videodev2.h>
#include <media/tuner.h>
#include <media/v4l2-chip-ident.h>
#include <media/i2c-addr.h>

#define VIP_VIP_VENDOR_DEVICE_ID                   0x0000
#define VIP_VIP_SUB_VENDOR_DEVICE_ID               0x0004
#define VIP_VIP_COMMAND_STATUS                     0x0008
#define VIP_VIP_REVISION_ID                        0x000c

#define RT100_ATI_ID 0x4D541002
#define RT200_ATI_ID 0x4d4a1002

/* Status defines */
#define VIP_BUSY  0
#define VIP_IDLE  1
#define VIP_RESET 2
/* Video Input BUS */

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

static uint32_t radeon_vip_idle(struct radeon_device *rdev)
{
	u32 timeout;

	timeout = RREG32(RADEON_VIPH_TIMEOUT_STAT);
	if (timeout & RADEON_VIPH_TIMEOUT_STAT__VIPH_REG_STAT) {
		r100_rbbm_fifo_wait_for_entry(rdev, 2);
		WREG32(RADEON_VIPH_TIMEOUT_STAT, (timeout & 0xffffff00) |
		       RADEON_VIPH_TIMEOUT_STAT__VIPH_REG_AK);

		r100_gui_wait_for_idle(rdev);
		return (RREG32(RADEON_VIPH_CONTROL) & 0x2000) ? VIP_BUSY : VIP_RESET;
	}
	r100_gui_wait_for_idle(rdev);
	return (RREG32(RADEON_VIPH_CONTROL) & 0x2000) ? VIP_BUSY : VIP_IDLE;
}

static uint32_t radeon_vip_fifo_idle(struct radeon_device *rdev, u8 channel)
{
	uint32_t timeout;

	timeout = RREG32(RADEON_VIPH_TIMEOUT_STAT);
	if ((timeout & 0x0000000f) & channel) {
		DRM_INFO("RADEON_fifo file\n");
		r100_rbbm_fifo_wait_for_entry(rdev, 2);
		WREG32(RADEON_VIPH_TIMEOUT_STAT, (timeout & 0xfffffff0) | channel);
		r100_gui_wait_for_idle(rdev);
		return (RREG32(RADEON_VIPH_CONTROL) & 0x2000) ? VIP_BUSY : VIP_RESET;
	}
	r100_gui_wait_for_idle(rdev);
	return (RREG32(RADEON_VIPH_CONTROL) & 0x2000) ? VIP_BUSY : VIP_IDLE;
}

static inline uint32_t vip_wait_for_idle(struct radeon_device *rdev)
{
	int i2ctries = 0;
	uint32_t status;
	while (i2ctries < 10) {
		status = radeon_vip_idle(rdev);
		if (status == VIP_BUSY) {
			mdelay(1);
			i2ctries++;
		} else
			return status;
	}
	return status;
}
	
bool radeon_vip_read(struct radeon_device *rdev, uint32_t address,
		     uint32_t count, uint8_t *buffer)
{
	uint32_t status, tmp;
	if (count != 1 && count != 2 && count !=4) {
		DRM_ERROR("Attempt to use VIP bus with non-standard transaction size\n");
		return false;
	}

	r100_rbbm_fifo_wait_for_entry(rdev, 2);
	WREG32(RADEON_VIPH_REG_ADDR, address | 0x2000);
	mb();
	status = vip_wait_for_idle(rdev);
	if (status != VIP_IDLE)
		return false;

	r100_gui_wait_for_idle(rdev);
	tmp = RREG32(RADEON_VIPH_TIMEOUT_STAT);
	tmp &= (0xffffff00 & ~RADEON_VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);
	WREG32(RADEON_VIPH_TIMEOUT_STAT, tmp);
	mb();
       
	r100_gui_wait_for_idle(rdev);
	RREG32(RADEON_VIPH_REG_DATA);
	
	status = vip_wait_for_idle(rdev);
	if (status != VIP_IDLE)
		return false;

	r100_gui_wait_for_idle(rdev);
	tmp = RREG32(RADEON_VIPH_TIMEOUT_STAT);
	WREG32(RADEON_VIPH_TIMEOUT_STAT, (tmp & 0xffffff00) |
	       RADEON_VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);

	mb();
	r100_gui_wait_for_idle(rdev);
	tmp = RREG32(RADEON_VIPH_REG_DATA);
	switch (count) {
	case 1:
		buffer[0] = tmp & 0xff;
		break;
	case 2:
		buffer[0] = tmp & 0xff;
		buffer[1] = (tmp >> 8) & 0xff;
		break;
	case 4:
		*(uint32_t *)buffer = le32_to_cpu(tmp);
		break;
	}
	status = vip_wait_for_idle(rdev);
	if (status != VIP_IDLE)
		return false;

	tmp = (RREG32(RADEON_VIPH_TIMEOUT_STAT) & 0xffffff00) |
		RADEON_VIPH_TIMEOUT_STAT__VIPH_REGR_DIS;
	WREG32(RADEON_VIPH_TIMEOUT_STAT, tmp);
	mb();
	return true;
}

bool radeon_vip_fifo_read(struct radeon_device *rdev, uint32_t address,
			  uint32_t count, uint8_t *buffer)
{
	uint32_t status, tmp;
	if (count != 1) {
		DRM_ERROR("VIP fifo bus with non 1 count\n");
		return false;
	}

	r100_rbbm_fifo_wait_for_entry(rdev, 2);
	WREG32(RADEON_VIPH_REG_ADDR, address | 0x3000);
	wmb();
	while (VIP_BUSY == (status = radeon_vip_fifo_idle(rdev, 0xff)));
	if (VIP_IDLE != status)
		return false;

	r100_gui_wait_for_idle(rdev);
	tmp = RREG32(RADEON_VIPH_TIMEOUT_STAT);
	tmp &= (0xffffff00 & ~RADEON_VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);
	WREG32(RADEON_VIPH_TIMEOUT_STAT, tmp);
	wmb();

	r100_gui_wait_for_idle(rdev);
	RREG32(RADEON_VIPH_REG_DATA);

	while(VIP_BUSY == (status = radeon_vip_fifo_idle(rdev, 0xff)));
	if (status != VIP_IDLE)
		return false;

	r100_gui_wait_for_idle(rdev);
	tmp = RREG32(RADEON_VIPH_TIMEOUT_STAT);
	tmp &= (0xffffff00 & ~RADEON_VIPH_TIMEOUT_STAT__VIPH_REGR_DIS);
	WREG32(RADEON_VIPH_TIMEOUT_STAT, tmp);
	wmb();

	r100_gui_wait_for_idle(rdev);
	tmp = RREG32(RADEON_VIPH_REG_DATA);
	*buffer = tmp & 0xff;

	while(VIP_BUSY == (status & radeon_vip_fifo_idle(rdev, 0xff)));
	if (status != VIP_IDLE)
		return false;

	tmp = RREG32(RADEON_VIPH_TIMEOUT_STAT);
	tmp &= 0xffffff00;
	tmp |= RADEON_VIPH_TIMEOUT_STAT__VIPH_REGR_DIS;

	WREG32(RADEON_VIPH_TIMEOUT_STAT, tmp);
	wmb();
	return true;
}

bool radeon_vip_write(struct radeon_device *rdev, uint32_t address,
		      uint32_t count, uint8_t *buffer)
{
	uint32_t status;
	if (count != 4)
		return false;

	r100_rbbm_fifo_wait_for_entry(rdev, 2);
	WREG32(RADEON_VIPH_REG_ADDR, address & ~0x2000);
	while (VIP_BUSY == (status = radeon_vip_idle(rdev)));

	if (status != VIP_IDLE)
		return false;

	r100_rbbm_fifo_wait_for_entry(rdev, 2);
	WREG32(RADEON_VIPH_REG_DATA, *(uint32_t *)buffer);
	wmb();

	while (VIP_BUSY == (status = radeon_vip_idle(rdev)));
	if (status != VIP_IDLE)
		return false;
	return true;
}

bool radeon_vip_fifo_write(struct radeon_device *rdev, uint32_t address,
			   uint32_t count, uint8_t *buffer)
{
	uint32_t status;
	int i;

	r100_rbbm_fifo_wait_for_entry(rdev, 2);
	WREG32(RADEON_VIPH_REG_ADDR, (address & ~0x2000) | 0x1000);
	while (VIP_BUSY == (status = radeon_vip_fifo_idle(rdev, 0x0f)));	

	if (status != VIP_IDLE)
		return false;

	r100_rbbm_fifo_wait_for_entry(rdev, 2);
	for (i = 0; i < count; i += 4) {
		WREG32(RADEON_VIPH_REG_DATA, *(uint32_t *)(buffer + i));
		wmb();
		while (VIP_BUSY == (status == radeon_vip_fifo_idle(rdev, 0x0f)));
		if (status != VIP_IDLE) {
			DRM_ERROR("cannot write to VIPH_REG_DATA\n");
			return false;
		}
	}
	return true;
}

static void radeon_vip_reset(struct radeon_device *rdev)
{
	u32 tmp;

	r100_gui_wait_for_idle(rdev);
	switch (rdev->family) {
	case CHIP_RV250:
	case CHIP_RV350:
	case CHIP_R350:
	case CHIP_R300:
	case CHIP_R580:
		WREG32(RADEON_VIPH_CONTROL, 0x003f0009);
		tmp = RREG32(RADEON_VIPH_TIMEOUT_STAT);
		tmp &= 0xffffff00;
		tmp |= RADEON_VIPH_TIMEOUT_STAT__VIPH_REGR_DIS;
		WREG32(RADEON_VIPH_TIMEOUT_STAT, tmp);
		WREG32(RADEON_VIPH_DV_LAT, 0x444400ff);
		WREG32(RADEON_VIPH_BM_CHUNK, 0x0);
		tmp = RREG32(RADEON_TEST_DEBUG_CNTL);
		tmp &= ~RADEON_TEST_DEBUG_CNTL__TEST_DEBUG_OUT_EN;
		WREG32(RADEON_TEST_DEBUG_CNTL, tmp);
		break;
	case CHIP_RV380:
		WREG32(RADEON_VIPH_CONTROL, 0x003f000d);
		tmp = RREG32(RADEON_VIPH_TIMEOUT_STAT);
		tmp &= 0xffffff00;
		tmp |= RADEON_VIPH_TIMEOUT_STAT__VIPH_REGR_DIS;
		WREG32(RADEON_VIPH_TIMEOUT_STAT, tmp);
		WREG32(RADEON_VIPH_DV_LAT, 0x444400ff);
		WREG32(RADEON_VIPH_BM_CHUNK, 0x0);
		tmp = RREG32(RADEON_TEST_DEBUG_CNTL);
		tmp &= ~RADEON_TEST_DEBUG_CNTL__TEST_DEBUG_OUT_EN;
		WREG32(RADEON_TEST_DEBUG_CNTL, tmp);
		break;
	default:
		WREG32(RADEON_VIPH_CONTROL, 0x003f0004);
		tmp = RREG32(RADEON_VIPH_TIMEOUT_STAT);
		tmp &= 0xffffff00;
		tmp |= RADEON_VIPH_TIMEOUT_STAT__VIPH_REGR_DIS;
		WREG32(RADEON_VIPH_TIMEOUT_STAT, tmp);
		WREG32(RADEON_VIPH_DV_LAT, 0x444400ff);
		WREG32(RADEON_VIPH_BM_CHUNK, 0x151);
		tmp = RREG32(RADEON_TEST_DEBUG_CNTL);
		tmp &= ~RADEON_TEST_DEBUG_CNTL__TEST_DEBUG_OUT_EN;
		WREG32(RADEON_TEST_DEBUG_CNTL, tmp);
		break;
	}

}

void radeon_vip_init(struct radeon_device *rdev)
{
	radeon_vip_reset(rdev);
}

bool radeon_theatre_detect(struct radeon_device *rdev)
{
	char s[20];
	uint32_t val;
	int theatre_num = -1;
	u32 theatre_id = 0;
	int i;

	radeon_vip_read(rdev, VIP_VIP_VENDOR_DEVICE_ID, 4, (uint8_t *)&val);
	for (i = 0; i < 4; i++) {
		if (radeon_vip_read(rdev, ((i & 0x03) << 14) | VIP_VIP_VENDOR_DEVICE_ID, 4, (uint8_t *)&val)) {
			if (val)
				DRM_INFO("Device %d on VIP bus ids as 0x%08x\n", i, (unsigned)val);

			if (theatre_num >= 0)
				continue;

			switch (val) {
			case RT100_ATI_ID:
				theatre_num = i;
				theatre_id = RT100_ATI_ID;
				break;
			case RT200_ATI_ID:
				theatre_num = i;
				theatre_id = RT200_ATI_ID;
				break;
			}
		} else {
			DRM_INFO("No response from device %d on VIP bus\n", i);
		}
	}

	if (theatre_num >= 0) {
		DRM_INFO("Detected Rage Theatre as device %d on VIP bus with id 0x%08x\n",
			 theatre_num, theatre_id);
		return true;
	}
	return false;
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
	radeon_theatre_detect(rdev);

	if (rdev->is_atom_bios)
		ret = atombios_add_mm_i2c_bus(rdev);
	else
		ret = radeon_combios_add_mm_i2c_bus(rdev);
	if (ret == false)
		return false;
#define DRIVER_NAME		"radeon"
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
		
	rdev->mm.initialised = true;
	return true;
}


void radeon_multimedia_fini(struct radeon_device *rdev)
{
	if (rdev->mm.initialised == false)
		return;

	if (rdev->mm.i2c_bus)
		radeon_i2c_destroy(rdev->mm.i2c_bus);

	v4l2_device_unregister(&rdev->mm.v4l2_dev);
}
