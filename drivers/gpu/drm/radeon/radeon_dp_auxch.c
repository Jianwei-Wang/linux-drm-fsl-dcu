#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon.h"

#define DP_AUX_CONTROL 0x6200

#define DP_AUX_CONTROL_AUX_EN (1 << 0)
#define DP_AUX_CONTROL_AUX_LS_READ_EN (1 << 8)
#define DP_AUX_CONTROL_AUX_LS_UPDATE_DISABLE(x) (((x) & 0x1) << 12)
#define DP_AUX_CONTROL_AUX_HPD_DISCON(x) (((x) & 0x1) << 16)
#define DP_AUX_CONTROL_AUX_DET_EN (1 << 18)
#define DP_AUX_CONTROL_AUX_HPD_SEL(x) (((x) & 0x7) << 20)
#define DP_AUX_CONTROL_AUX_IMPCAL_REQ_EN(x) (((x) & 0x1) << 24)
#define DP_AUX_CONTROL_AUX_TEST_MODE(x) (((x) & 0x1) << 28)
#define DP_AUX_CONTROL_AUX_DEGLITCH_EN(x) (((x) & 0x1) << 28)

#define DP_AUX_SW_CONTROL 0x6204

#define DP_AUX_SW_GO (1 << 0)
#define DP_AUX_LS_READ_TRIG(x) (((x) & 0x1) << 2)
#define DP_AUX_SW_START_DELAY(x) (((x) & 0xf) << 4)
#define DP_AUX_SW_WR_BYTES(x) (((x) & 0x1f) << 16)

#define DP_AUX_SW_INTERRUPT_CONTROL 0x620c
#define DP_AUX_SW_DONE_INT (1 << 0)
#define DP_AUX_SW_DONE_ACK (1 << 1)
#define DP_AUX_SW_DONE_MASK (1 << 2)
#define DP_AUX_SW_LS_DONE_INT (1 << 4)
#define DP_AUX_SW_LS_DONE_MASK (1 << 6)

#define DP_AUX_SW_STATUS 0x6210
#define DP_AUX_SW_DONE (1 << 0)
#define DP_AUX_SW_REQ (1 << 1)
#define DP_AUX_SW_RX_TIMEOUT_STATE(x) (((x) & 0x7) << 4)
#define DP_AUX_SW_RX_TIMEOUT (1 << 7)
#define DP_AUX_SW_RX_OVERFLOW (1 << 8)
#define DP_AUX_SW_RX_HPD_DISCON (1 << 9)

#define DP_AUX_SW_DATA 0x6218
#define DP_AUX_SW_DATA_RW (1 << 0)
#define DP_AUX_SW_DATA_MASK(x) (((x) & 0xff) << 8)
#define DP_AUX_SW_DATA_INDEX(x) (((x) & 0x1f) << 16)
#define DP_AUX_SW_AUTOINCREMENT_DISABLE (1 << 31)

static uint32_t dce5_dp_base[6] = { 0x6200,
				    0x6250,
				    0x62a0,
				    0x6300,
				    0x6350,
				    0x63a0 };

/* native implementation of AUXCH for DCE5 */
#define DPREG(x) (((x) - 0x6200) + offset)

#define DBG_WREG32(reg, val) do {		\
		DRM_DEBUG_KMS("writing %08x: %08x\n", reg, val); \
		WREG32(reg, val);				 \
	} while(0)
ssize_t
radeon_dp_aux_transfer_native(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct radeon_i2c_chan *chan =
		container_of(aux, struct radeon_i2c_chan, aux);
	struct drm_device *dev = chan->dev;
	struct radeon_device *rdev = dev->dev_private;
	int ret = 0, i;
	uint32_t tmp, ack = 0;
	uint32_t offset = dce5_dp_base[chan->rec.i2c_id & 0xf];
	u8 byte;
	u8 *buf;
	int retry_count = 0;
	int nbytes;
	int wbytes = 3;
	int msize;
	bool is_write = false;
	if (WARN_ON(msg->size > 16))
		return -E2BIG;

	DRM_DEBUG_KMS("%d/%d: native transfer request addr:%x size:%ld req:%x\n", chan->rec.i2c_id & 0xf, chan->rec.hpd, msg->address, msg->size, msg->request);
	mutex_lock(&chan->mutex);

	DBG_WREG32(0x6430, 0xf010000);
	tmp = RREG32(DPREG(DP_AUX_CONTROL));

	tmp &= DP_AUX_CONTROL_AUX_HPD_SEL(0x7);
	tmp |= DP_AUX_CONTROL_AUX_HPD_SEL(chan->rec.hpd);

	tmp |= DP_AUX_CONTROL_AUX_EN | DP_AUX_CONTROL_AUX_LS_READ_EN;

	DBG_WREG32(DPREG(DP_AUX_CONTROL), tmp);

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
		is_write = true;
		break;
	default:
		break;
	}

	msize = msg->size ? msg->size - 1 : 0;
	wbytes += is_write ? msg->size : 1;

	DBG_WREG32(DPREG(DP_AUX_SW_CONTROL),
	       DP_AUX_SW_WR_BYTES(wbytes));
	DBG_WREG32(DPREG(DP_AUX_SW_CONTROL),
	       DP_AUX_SW_WR_BYTES(wbytes));
	/* write the data headers into the registers */
	byte = (msg->request << 4);
	DBG_WREG32(DPREG(DP_AUX_SW_DATA),
	       DP_AUX_SW_DATA_MASK(byte) | DP_AUX_SW_AUTOINCREMENT_DISABLE);
	byte = (msg->address >> 8) & 0xff;
	DBG_WREG32(DPREG(DP_AUX_SW_DATA),
	       DP_AUX_SW_DATA_MASK(byte));
	byte = msg->address & 0xff;
	DBG_WREG32(DPREG(DP_AUX_SW_DATA),
	       DP_AUX_SW_DATA_MASK(byte));
	byte = msize;
	DBG_WREG32(DPREG(DP_AUX_SW_DATA),
	       DP_AUX_SW_DATA_MASK(byte));

	if (is_write) {
		buf = msg->buffer;
		for (i = 0; i < msg->size; i++) {
			DBG_WREG32(DPREG(DP_AUX_SW_DATA),
			       DP_AUX_SW_DATA_MASK(buf[i]));
		}
	}

	DBG_WREG32(DPREG(DP_AUX_SW_INTERRUPT_CONTROL), DP_AUX_SW_DONE_ACK);

	DBG_WREG32(DPREG(DP_AUX_SW_CONTROL),
	       DP_AUX_SW_WR_BYTES(wbytes) | DP_AUX_SW_GO);

	do {
		tmp = RREG32(DPREG(DP_AUX_SW_STATUS));
		if (tmp & DP_AUX_SW_DONE) {
			DRM_DEBUG_KMS("got aux done %08x\n", tmp);
			break;
		}
		usleep_range(100, 200);
	} while (retry_count++ < 1000);

	if (retry_count >= 1000) {
		DRM_ERROR("retried a lot and failed %x: %d\n", tmp, (tmp >> 24) & 0x1f);
		ret = -EIO;
		goto done;
	}

	if (tmp & (0x1 << 7)) {
		DRM_DEBUG_KMS("dp_aux_ch timed out\n");
		ret = -ETIMEDOUT;
		goto done;
	}
	if (tmp & ((0xf << 8))) {
		DRM_DEBUG_KMS("dp_aux_ch flags not zero\n");
		ret = -EIO;
		goto done;
	}

	nbytes = (tmp >> 24) & 0x1f;
	if (nbytes) {
		buf = msg->buffer;	  

		DRM_DEBUG_KMS("nbytes reported %d\n", nbytes);

		WREG32(DPREG(DP_AUX_SW_DATA),
		       DP_AUX_SW_DATA_RW | DP_AUX_SW_AUTOINCREMENT_DISABLE);
		tmp = RREG32(DPREG(DP_AUX_SW_DATA));
		ack = (tmp >> 8) & 0xff;
		DRM_DEBUG_KMS("status byte: %08x %x\n", tmp, ack);
		for (i = 0; i < nbytes - 1; i++) {
			tmp = RREG32(DPREG(DP_AUX_SW_DATA));
			DRM_DEBUG_KMS("byte %d: %08x\n", i + 1, tmp);
			if (buf)
				buf[i] = (tmp >> 8) & 0xff;
		}
		if (buf)
			ret = nbytes - 1;
	}

	DBG_WREG32(DPREG(DP_AUX_SW_INTERRUPT_CONTROL), DP_AUX_SW_DONE_ACK);

	if (is_write)
		ret = msg->size;
done:
	mutex_unlock(&chan->mutex);


	if (ret >= 0)
		msg->reply = ack >> 4;
	return ret;
}
