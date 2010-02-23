#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"


#define VIP_VIP_VENDOR_DEVICE_ID                   0x0000
#define VIP_VIP_SUB_VENDOR_DEVICE_ID               0x0004
#define VIP_VIP_COMMAND_STATUS                     0x0008
#define VIP_VIP_REVISION_ID                        0x000c

/* Status defines */
#define VIP_BUSY  0
#define VIP_IDLE  1
#define VIP_RESET 2
/* Video Input BUS */
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

bool radeon_vip_theatre_detect(struct radeon_device *rdev)
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
		rdev->mm.theatre_num = theatre_num;
		rdev->mm.theatre_id = theatre_id;
		return true;
	}
	return false;
}

