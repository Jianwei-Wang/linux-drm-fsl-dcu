#include "drmP.h"
#include "theatre.h"
#include "radeon_vip.h"

static bool theatre_read(struct theatre *t, uint32_t reg, uint32_t *data)
{
	return radeon_vip_read(t->rdev, ((t->theatre_num & 0x03) << 14) | reg, 4,
			       (uint8_t *)data);
}

static bool theatre_write(struct theatre *t, uint32_t reg, uint32_t data)
{
	return radeon_vip_write(t->rdev, ((t->theatre_num & 0x03) << 14) | reg, 4,
				(uint8_t *) &data);
}

static void rt_setcombfilter(struct theatre *t, uint16_t standard, uint16_t connector);
enum {
	fld_tmpReg1=0,
	fld_tmpReg2,
	fld_tmpReg3,
	fld_LP_CONTRAST,
	fld_LP_BRIGHTNESS,
	fld_CP_HUE_CNTL,
	fld_LUMA_FILTER,
	fld_H_SCALE_RATIO,
	fld_H_SHARPNESS,
	fld_V_SCALE_RATIO,
	fld_V_DEINTERLACE_ON,
	fld_V_BYPSS,
	fld_V_DITHER_ON,
	fld_EVENF_OFFSET,
	fld_ODDF_OFFSET,
	fld_INTERLACE_DETECTED,
	fld_VS_LINE_COUNT,
	fld_VS_DETECTED_LINES,
	fld_VS_ITU656_VB,
	fld_VBI_CC_DATA,
	fld_VBI_CC_WT,
	fld_VBI_CC_WT_ACK,
	fld_VBI_CC_HOLD,
	fld_VBI_DECODE_EN,
	fld_VBI_CC_DTO_P,
	fld_VBI_20BIT_DTO_P,
	fld_VBI_CC_LEVEL,
	fld_VBI_20BIT_LEVEL,
	fld_VBI_CLK_RUNIN_GAIN,
	fld_H_VBI_WIND_START,
	fld_H_VBI_WIND_END,
	fld_V_VBI_WIND_START,
	fld_V_VBI_WIND_END,
	fld_VBI_20BIT_DATA0,
	fld_VBI_20BIT_DATA1,
	fld_VBI_20BIT_WT,
	fld_VBI_20BIT_WT_ACK,
	fld_VBI_20BIT_HOLD,
	fld_VBI_CAPTURE_ENABLE,
	fld_VBI_EDS_DATA,
	fld_VBI_EDS_WT,
	fld_VBI_EDS_WT_ACK,
	fld_VBI_EDS_HOLD,
	fld_VBI_SCALING_RATIO,
	fld_VBI_ALIGNER_ENABLE,
	fld_H_ACTIVE_START,
	fld_H_ACTIVE_END,
	fld_V_ACTIVE_START,
	fld_V_ACTIVE_END,
	fld_CH_HEIGHT,
	fld_CH_KILL_LEVEL,
	fld_CH_AGC_ERROR_LIM,
	fld_CH_AGC_FILTER_EN,
	fld_CH_AGC_LOOP_SPEED,
	fld_HUE_ADJ,
	fld_STANDARD_SEL,
	fld_STANDARD_YC,
	fld_ADC_PDWN,
	fld_INPUT_SELECT,
	fld_ADC_PREFLO,
	fld_H_SYNC_PULSE_WIDTH,
	fld_HS_GENLOCKED,
	fld_HS_SYNC_IN_WIN,
	fld_VIN_ASYNC_RST,
	fld_DVS_ASYNC_RST,
	fld_VIP_VENDOR_ID,
	fld_VIP_DEVICE_ID,
	fld_VIP_REVISION_ID,
	fld_BLACK_INT_START,
	fld_BLACK_INT_LENGTH,
	fld_UV_INT_START,
	fld_U_INT_LENGTH,
	fld_V_INT_LENGTH,
	fld_CRDR_ACTIVE_GAIN,
	fld_CBDB_ACTIVE_GAIN,
	fld_DVS_DIRECTION,
	fld_DVS_VBI_UINT8_SWAP,
	fld_DVS_CLK_SELECT,
	fld_CONTINUOUS_STREAM,
	fld_DVSOUT_CLK_DRV,
	fld_DVSOUT_DATA_DRV,
	fld_COMB_CNTL0,
	fld_COMB_CNTL1,
	fld_COMB_CNTL2,
	fld_COMB_LENGTH,
	fld_SYNCTIP_REF0,
	fld_SYNCTIP_REF1,
	fld_CLAMP_REF,
	fld_AGC_PEAKWHITE,
	fld_VBI_PEAKWHITE,
	fld_WPA_THRESHOLD,
	fld_WPA_TRIGGER_LO,
	fld_WPA_TRIGGER_HIGH,
	fld_LOCKOUT_START,
	fld_LOCKOUT_END,
	fld_CH_DTO_INC,
	fld_PLL_SGAIN,
	fld_PLL_FGAIN,
	fld_CR_BURST_GAIN,
	fld_CB_BURST_GAIN,
	fld_VERT_LOCKOUT_START,
	fld_VERT_LOCKOUT_END,
	fld_H_IN_WIND_START,
	fld_V_IN_WIND_START,
	fld_H_OUT_WIND_WIDTH,
	fld_V_OUT_WIND_WIDTH,
	fld_HS_LINE_TOTAL,
	fld_MIN_PULSE_WIDTH,
	fld_MAX_PULSE_WIDTH,
	fld_WIN_CLOSE_LIMIT,
	fld_WIN_OPEN_LIMIT,
	fld_VSYNC_INT_TRIGGER,
	fld_VSYNC_INT_HOLD,
	fld_VIN_M0,
	fld_VIN_N0,
	fld_MNFLIP_EN,
	fld_VIN_P,
	fld_REG_CLK_SEL,
	fld_VIN_M1,
	fld_VIN_N1,
	fld_VIN_DRIVER_SEL,
	fld_VIN_MNFLIP_REQ,
	fld_VIN_MNFLIP_DONE,
	fld_TV_LOCK_TO_VIN,
	fld_TV_P_FOR_WINCLK,
	fld_VINRST,
	fld_VIN_CLK_SEL,
	fld_VS_FIELD_BLANK_START,
	fld_VS_FIELD_BLANK_END,
	fld_VS_FIELD_IDLOCATION,
	fld_VS_FRAME_TOTAL,
	fld_SYNC_TIP_START,
	fld_SYNC_TIP_LENGTH,
	fld_GAIN_FORCE_DATA,
	fld_GAIN_FORCE_EN,
	fld_I_CLAMP_SEL,
	fld_I_AGC_SEL,
	fld_EXT_CLAMP_CAP,
	fld_EXT_AGC_CAP,
	fld_DECI_DITHER_EN,
	fld_ADC_PREFHI,
	fld_ADC_CH_GAIN_SEL,
	fld_HS_PLL_SGAIN,
	fld_NREn,
	fld_NRGainCntl,
	fld_NRBWTresh,
	fld_NRGCTresh,
	fld_NRCoefDespeclMode,
	fld_GPIO_5_OE,
	fld_GPIO_6_OE,
	fld_GPIO_5_OUT,
	fld_GPIO_6_OUT,

	regRT_MAX_REGS
};

struct rtregmap {
	uint8_t size;
	uint32_t fld_id;
	uint32_t reg_addr_lsb;
	uint32_t fld_offset_lsb;
	uint32_t mask_lsb;
	uint32_t addr2;
	uint32_t offs2;
	uint32_t mask2;
	uint32_t curr_value;
	uint32_t rw;
};

#define READONLY 1
#define WRITEONLY 2
#define READWRITE 3

/* Rage Theatre's Register Mappings, including the default values: */
static struct rtregmap rt_regmap[regRT_MAX_REGS]={
/*
  {size, fidname, AddrOfst, Ofst, Mask, Addr, Ofst, Mask, Cur, R/W
*/
	{32 , fld_tmpReg1       ,0x151                          , 0, 0x0, 0, 0,0, 0,READWRITE },
	{1  , fld_tmpReg2       ,VIP_VIP_SUB_VENDOR_DEVICE_ID   , 3, 0xFFFFFFFF, 0, 0,0, 0,READWRITE },
	{1  , fld_tmpReg3       ,VIP_VIP_COMMAND_STATUS         , 3, 0xFFFFFFFF, 0, 0,0, 0,READWRITE },
	{8  , fld_LP_CONTRAST   ,VIP_LP_CONTRAST            ,  0, 0xFFFFFF00, 0, 0,0, fld_LP_CONTRAST_def       ,READWRITE  },
	{14 , fld_LP_BRIGHTNESS ,VIP_LP_BRIGHTNESS          ,  0, 0xFFFFC000, 0, 0,0, fld_LP_BRIGHTNESS_def     ,READWRITE  },
	{8  , fld_CP_HUE_CNTL   ,VIP_CP_HUE_CNTL            ,  0, 0xFFFFFF00, 0, 0,0, fld_CP_HUE_CNTL_def       ,READWRITE  },
	{1  , fld_LUMA_FILTER   ,VIP_LP_BRIGHTNESS          , 15, 0xFFFF7FFF, 0, 0,0, fld_LUMA_FILTER_def       ,READWRITE  },
	{21 , fld_H_SCALE_RATIO ,VIP_H_SCALER_CONTROL       ,  0, 0xFFE00000, 0, 0,0, fld_H_SCALE_RATIO_def     ,READWRITE  },
	{4  , fld_H_SHARPNESS   ,VIP_H_SCALER_CONTROL       , 25, 0xE1FFFFFF, 0, 0,0, fld_H_SHARPNESS_def       ,READWRITE  },
	{12 , fld_V_SCALE_RATIO ,VIP_V_SCALER_CONTROL       ,  0, 0xFFFFF000, 0, 0,0, fld_V_SCALE_RATIO_def     ,READWRITE  },
	{1  , fld_V_DEINTERLACE_ON,VIP_V_SCALER_CONTROL     , 12, 0xFFFFEFFF, 0, 0,0, fld_V_DEINTERLACE_ON_def  ,READWRITE  },
	{1  , fld_V_BYPSS       ,VIP_V_SCALER_CONTROL       , 14, 0xFFFFBFFF, 0, 0,0, fld_V_BYPSS_def           ,READWRITE  },
	{1  , fld_V_DITHER_ON   ,VIP_V_SCALER_CONTROL       , 15, 0xFFFF7FFF, 0, 0,0, fld_V_DITHER_ON_def       ,READWRITE  },
	{11 , fld_EVENF_OFFSET  ,VIP_V_DEINTERLACE_CONTROL  ,  0, 0xFFFFF800, 0, 0,0, fld_EVENF_OFFSET_def      ,READWRITE  },
	{11 , fld_ODDF_OFFSET   ,VIP_V_DEINTERLACE_CONTROL  , 11, 0xFFC007FF, 0, 0,0, fld_ODDF_OFFSET_def       ,READWRITE  },
	{1  , fld_INTERLACE_DETECTED    ,VIP_VS_LINE_COUNT  , 15, 0xFFFF7FFF, 0, 0,0, fld_INTERLACE_DETECTED_def,READONLY   },
	{10 , fld_VS_LINE_COUNT     ,VIP_VS_LINE_COUNT      ,  0, 0xFFFFFC00, 0, 0,0, fld_VS_LINE_COUNT_def     ,READONLY   },
	{10 , fld_VS_DETECTED_LINES ,VIP_VS_LINE_COUNT      , 16, 0xFC00FFFF, 0, 0,0, fld_VS_DETECTED_LINES_def ,READONLY   },
	{1  , fld_VS_ITU656_VB  ,VIP_VS_LINE_COUNT          , 13, 0xFFFFDFFF, 0, 0,0, fld_VS_ITU656_VB_def  ,READONLY   },
	{16 , fld_VBI_CC_DATA   ,VIP_VBI_CC_CNTL            ,  0, 0xFFFF0000, 0, 0,0, fld_VBI_CC_DATA_def       ,READWRITE  },
	{1  , fld_VBI_CC_WT     ,VIP_VBI_CC_CNTL            , 24, 0xFEFFFFFF, 0, 0,0, fld_VBI_CC_WT_def         ,READWRITE  },
	{1  , fld_VBI_CC_WT_ACK ,VIP_VBI_CC_CNTL            , 25, 0xFDFFFFFF, 0, 0,0, fld_VBI_CC_WT_ACK_def     ,READONLY   },
	{1  , fld_VBI_CC_HOLD   ,VIP_VBI_CC_CNTL            , 26, 0xFBFFFFFF, 0, 0,0, fld_VBI_CC_HOLD_def       ,READWRITE  },
	{1  , fld_VBI_DECODE_EN ,VIP_VBI_CC_CNTL            , 31, 0x7FFFFFFF, 0, 0,0, fld_VBI_DECODE_EN_def     ,READWRITE  },
	{16 , fld_VBI_CC_DTO_P  ,VIP_VBI_DTO_CNTL           ,  0, 0xFFFF0000, 0, 0,0, fld_VBI_CC_DTO_P_def      ,READWRITE  },
	{16 ,fld_VBI_20BIT_DTO_P,VIP_VBI_DTO_CNTL           , 16, 0x0000FFFF, 0, 0,0, fld_VBI_20BIT_DTO_P_def   ,READWRITE  },
	{7  ,fld_VBI_CC_LEVEL   ,VIP_VBI_LEVEL_CNTL         ,  0, 0xFFFFFF80, 0, 0,0, fld_VBI_CC_LEVEL_def      ,READWRITE  },
	{7  ,fld_VBI_20BIT_LEVEL,VIP_VBI_LEVEL_CNTL         ,  8, 0xFFFF80FF, 0, 0,0, fld_VBI_20BIT_LEVEL_def   ,READWRITE  },
	{9  ,fld_VBI_CLK_RUNIN_GAIN,VIP_VBI_LEVEL_CNTL      , 16, 0xFE00FFFF, 0, 0,0, fld_VBI_CLK_RUNIN_GAIN_def,READWRITE  },
	{11 ,fld_H_VBI_WIND_START,VIP_H_VBI_WINDOW          ,  0, 0xFFFFF800, 0, 0,0, fld_H_VBI_WIND_START_def  ,READWRITE  },
	{11 ,fld_H_VBI_WIND_END,VIP_H_VBI_WINDOW            , 16, 0xF800FFFF, 0, 0,0, fld_H_VBI_WIND_END_def    ,READWRITE  },
	{10 ,fld_V_VBI_WIND_START,VIP_V_VBI_WINDOW          ,  0, 0xFFFFFC00, 0, 0,0, fld_V_VBI_WIND_START_def  ,READWRITE  },
	{10 ,fld_V_VBI_WIND_END,VIP_V_VBI_WINDOW            , 16, 0xFC00FFFF, 0, 0,0, fld_V_VBI_WIND_END_def    ,READWRITE  }, /* CHK */
	{16 ,fld_VBI_20BIT_DATA0,VIP_VBI_20BIT_CNTL         ,  0, 0xFFFF0000, 0, 0,0, fld_VBI_20BIT_DATA0_def   ,READWRITE  },
	{4  ,fld_VBI_20BIT_DATA1,VIP_VBI_20BIT_CNTL         , 16, 0xFFF0FFFF, 0, 0,0, fld_VBI_20BIT_DATA1_def   ,READWRITE  },
	{1  ,fld_VBI_20BIT_WT   ,VIP_VBI_20BIT_CNTL         , 24, 0xFEFFFFFF, 0, 0,0, fld_VBI_20BIT_WT_def      ,READWRITE  },
	{1  ,fld_VBI_20BIT_WT_ACK   ,VIP_VBI_20BIT_CNTL     , 25, 0xFDFFFFFF, 0, 0,0, fld_VBI_20BIT_WT_ACK_def  ,READONLY   },
	{1  ,fld_VBI_20BIT_HOLD ,VIP_VBI_20BIT_CNTL         , 26, 0xFBFFFFFF, 0, 0,0, fld_VBI_20BIT_HOLD_def    ,READWRITE  },
	{2  ,fld_VBI_CAPTURE_ENABLE ,VIP_VBI_CONTROL        ,  0, 0xFFFFFFFC, 0, 0,0, fld_VBI_CAPTURE_ENABLE_def,READWRITE  },
	{16 ,fld_VBI_EDS_DATA   ,VIP_VBI_EDS_CNTL           ,  0, 0xFFFF0000, 0, 0,0, fld_VBI_EDS_DATA_def      ,READWRITE  },
	{1  ,fld_VBI_EDS_WT     ,VIP_VBI_EDS_CNTL           , 24, 0xFEFFFFFF, 0, 0,0, fld_VBI_EDS_WT_def        ,READWRITE  },
	{1  ,fld_VBI_EDS_WT_ACK ,VIP_VBI_EDS_CNTL           , 25, 0xFDFFFFFF, 0, 0,0, fld_VBI_EDS_WT_ACK_def    ,READONLY   },
	{1  ,fld_VBI_EDS_HOLD   ,VIP_VBI_EDS_CNTL           , 26, 0xFBFFFFFF, 0, 0,0, fld_VBI_EDS_HOLD_def      ,READWRITE  },
	{17 ,fld_VBI_SCALING_RATIO  ,VIP_VBI_SCALER_CONTROL ,  0, 0xFFFE0000, 0, 0,0, fld_VBI_SCALING_RATIO_def ,READWRITE  },
	{1  ,fld_VBI_ALIGNER_ENABLE ,VIP_VBI_SCALER_CONTROL , 17, 0xFFFDFFFF, 0, 0,0, fld_VBI_ALIGNER_ENABLE_def,READWRITE  },
	{11 ,fld_H_ACTIVE_START ,VIP_H_ACTIVE_WINDOW        ,  0, 0xFFFFF800, 0, 0,0, fld_H_ACTIVE_START_def    ,READWRITE  },
	{11 ,fld_H_ACTIVE_END   ,VIP_H_ACTIVE_WINDOW        , 16, 0xF800FFFF, 0, 0,0, fld_H_ACTIVE_END_def      ,READWRITE  },
	{10 ,fld_V_ACTIVE_START ,VIP_V_ACTIVE_WINDOW        ,  0, 0xFFFFFC00, 0, 0,0, fld_V_ACTIVE_START_def    ,READWRITE  },
	{10 ,fld_V_ACTIVE_END   ,VIP_V_ACTIVE_WINDOW        , 16, 0xFC00FFFF, 0, 0,0, fld_V_ACTIVE_END_def      ,READWRITE  },
	{8  ,fld_CH_HEIGHT          ,VIP_CP_AGC_CNTL        ,  0, 0xFFFFFF00, 0, 0,0, fld_CH_HEIGHT_def         ,READWRITE  },
	{8  ,fld_CH_KILL_LEVEL      ,VIP_CP_AGC_CNTL        ,  8, 0xFFFF00FF, 0, 0,0, fld_CH_KILL_LEVEL_def     ,READWRITE  },
	{2  ,fld_CH_AGC_ERROR_LIM   ,VIP_CP_AGC_CNTL        , 16, 0xFFFCFFFF, 0, 0,0, fld_CH_AGC_ERROR_LIM_def  ,READWRITE  },
	{1  ,fld_CH_AGC_FILTER_EN   ,VIP_CP_AGC_CNTL        , 18, 0xFFFBFFFF, 0, 0,0, fld_CH_AGC_FILTER_EN_def  ,READWRITE  },
	{1  ,fld_CH_AGC_LOOP_SPEED  ,VIP_CP_AGC_CNTL        , 19, 0xFFF7FFFF, 0, 0,0, fld_CH_AGC_LOOP_SPEED_def ,READWRITE  },
	{8  ,fld_HUE_ADJ            ,VIP_CP_HUE_CNTL        ,  0, 0xFFFFFF00, 0, 0,0, fld_HUE_ADJ_def           ,READWRITE  },
	{2  ,fld_STANDARD_SEL       ,VIP_STANDARD_SELECT    ,  0, 0xFFFFFFFC, 0, 0,0, fld_STANDARD_SEL_def      ,READWRITE  },
	{1  ,fld_STANDARD_YC        ,VIP_STANDARD_SELECT    ,  2, 0xFFFFFFFB, 0, 0,0, fld_STANDARD_YC_def       ,READWRITE  },
	{1  ,fld_ADC_PDWN           ,VIP_ADC_CNTL           ,  7, 0xFFFFFF7F, 0, 0,0, fld_ADC_PDWN_def          ,READWRITE  },
	{3  ,fld_INPUT_SELECT       ,VIP_ADC_CNTL           ,  0, 0xFFFFFFF8, 0, 0,0, fld_INPUT_SELECT_def      ,READWRITE  },
	{2  ,fld_ADC_PREFLO         ,VIP_ADC_CNTL           , 24, 0xFCFFFFFF, 0, 0,0, fld_ADC_PREFLO_def        ,READWRITE  },
	{8  ,fld_H_SYNC_PULSE_WIDTH ,VIP_HS_PULSE_WIDTH     ,  0, 0xFFFFFF00, 0, 0,0, fld_H_SYNC_PULSE_WIDTH_def,READONLY   },
	{1  ,fld_HS_GENLOCKED       ,VIP_HS_PULSE_WIDTH     ,  8, 0xFFFFFEFF, 0, 0,0, fld_HS_GENLOCKED_def      ,READONLY   },
	{1  ,fld_HS_SYNC_IN_WIN     ,VIP_HS_PULSE_WIDTH     ,  9, 0xFFFFFDFF, 0, 0,0, fld_HS_SYNC_IN_WIN_def    ,READONLY   },
	{1  ,fld_VIN_ASYNC_RST      ,VIP_MASTER_CNTL        ,  5, 0xFFFFFFDF, 0, 0,0, fld_VIN_ASYNC_RST_def     ,READWRITE  },
	{1  ,fld_DVS_ASYNC_RST      ,VIP_MASTER_CNTL        ,  7, 0xFFFFFF7F, 0, 0,0, fld_DVS_ASYNC_RST_def     ,READWRITE  },
	{16 ,fld_VIP_VENDOR_ID      ,VIP_VIP_VENDOR_DEVICE_ID, 0, 0xFFFF0000, 0, 0,0, fld_VIP_VENDOR_ID_def     ,READONLY   },
	{16 ,fld_VIP_DEVICE_ID      ,VIP_VIP_VENDOR_DEVICE_ID,16, 0x0000FFFF, 0, 0,0, fld_VIP_DEVICE_ID_def     ,READONLY   },
	{16 ,fld_VIP_REVISION_ID    ,VIP_VIP_REVISION_ID    ,  0, 0xFFFF0000, 0, 0,0, fld_VIP_REVISION_ID_def   ,READONLY   },
	{8  ,fld_BLACK_INT_START    ,VIP_SG_BLACK_GATE      ,  0, 0xFFFFFF00, 0, 0,0, fld_BLACK_INT_START_def   ,READWRITE  },
	{4  ,fld_BLACK_INT_LENGTH   ,VIP_SG_BLACK_GATE      ,  8, 0xFFFFF0FF, 0, 0,0, fld_BLACK_INT_LENGTH_def  ,READWRITE  },
	{8  ,fld_UV_INT_START       ,VIP_SG_UVGATE_GATE     ,  0, 0xFFFFFF00, 0, 0,0, fld_UV_INT_START_def      ,READWRITE  },
	{4  ,fld_U_INT_LENGTH       ,VIP_SG_UVGATE_GATE     ,  8, 0xFFFFF0FF, 0, 0,0, fld_U_INT_LENGTH_def      ,READWRITE  },
	{4  ,fld_V_INT_LENGTH       ,VIP_SG_UVGATE_GATE     , 12, 0xFFFF0FFF, 0, 0,0, fld_V_INT_LENGTH_def      ,READWRITE  },
	{10 ,fld_CRDR_ACTIVE_GAIN   ,VIP_CP_ACTIVE_GAIN     ,  0, 0xFFFFFC00, 0, 0,0, fld_CRDR_ACTIVE_GAIN_def  ,READWRITE  },
	{10 ,fld_CBDB_ACTIVE_GAIN   ,VIP_CP_ACTIVE_GAIN     , 16, 0xFC00FFFF, 0, 0,0, fld_CBDB_ACTIVE_GAIN_def  ,READWRITE  },
	{1  ,fld_DVS_DIRECTION      ,VIP_DVS_PORT_CTRL      ,  0, 0xFFFFFFFE, 0, 0,0, fld_DVS_DIRECTION_def     ,READWRITE  },
	{1  ,fld_DVS_VBI_UINT8_SWAP  ,VIP_DVS_PORT_CTRL      ,  1, 0xFFFFFFFD, 0, 0,0, fld_DVS_VBI_UINT8_SWAP_def ,READWRITE  },
	{1  ,fld_DVS_CLK_SELECT     ,VIP_DVS_PORT_CTRL      ,  2, 0xFFFFFFFB, 0, 0,0, fld_DVS_CLK_SELECT_def    ,READWRITE  },
	{1  ,fld_CONTINUOUS_STREAM  ,VIP_DVS_PORT_CTRL      ,  3, 0xFFFFFFF7, 0, 0,0, fld_CONTINUOUS_STREAM_def ,READWRITE  },
	{1  ,fld_DVSOUT_CLK_DRV     ,VIP_DVS_PORT_CTRL      ,  4, 0xFFFFFFEF, 0, 0,0, fld_DVSOUT_CLK_DRV_def    ,READWRITE  },
	{1  ,fld_DVSOUT_DATA_DRV    ,VIP_DVS_PORT_CTRL      ,  5, 0xFFFFFFDF, 0, 0,0, fld_DVSOUT_DATA_DRV_def   ,READWRITE  },
	{32 ,fld_COMB_CNTL0         ,VIP_COMB_CNTL0         ,  0, 0x00000000, 0, 0,0, fld_COMB_CNTL0_def        ,READWRITE  },
	{32 ,fld_COMB_CNTL1         ,VIP_COMB_CNTL1         ,  0, 0x00000000, 0, 0,0, fld_COMB_CNTL1_def        ,READWRITE  },
	{32 ,fld_COMB_CNTL2         ,VIP_COMB_CNTL2         ,  0, 0x00000000, 0, 0,0, fld_COMB_CNTL2_def        ,READWRITE  },
	{32 ,fld_COMB_LENGTH        ,VIP_COMB_LINE_LENGTH   ,  0, 0x00000000, 0, 0,0, fld_COMB_LENGTH_def       ,READWRITE  },
	{8  ,fld_SYNCTIP_REF0       ,VIP_LP_AGC_CLAMP_CNTL0 ,  0, 0xFFFFFF00, 0, 0,0, fld_SYNCTIP_REF0_def      ,READWRITE  },
	{8  ,fld_SYNCTIP_REF1       ,VIP_LP_AGC_CLAMP_CNTL0 ,  8, 0xFFFF00FF, 0, 0,0, fld_SYNCTIP_REF1_def      ,READWRITE  },
	{8  ,fld_CLAMP_REF          ,VIP_LP_AGC_CLAMP_CNTL0 , 16, 0xFF00FFFF, 0, 0,0, fld_CLAMP_REF_def          ,READWRITE  },
	{8  ,fld_AGC_PEAKWHITE      ,VIP_LP_AGC_CLAMP_CNTL0 , 24, 0x00FFFFFF, 0, 0,0, fld_AGC_PEAKWHITE_def     ,READWRITE  },
	{8  ,fld_VBI_PEAKWHITE      ,VIP_LP_AGC_CLAMP_CNTL1 ,  0, 0xFFFFFF00, 0, 0,0, fld_VBI_PEAKWHITE_def     ,READWRITE  },
	{11 ,fld_WPA_THRESHOLD      ,VIP_LP_WPA_CNTL0       ,  0, 0xFFFFF800, 0, 0,0, fld_WPA_THRESHOLD_def     ,READWRITE  },
	{10 ,fld_WPA_TRIGGER_LO     ,VIP_LP_WPA_CNTL1       ,  0, 0xFFFFFC00, 0, 0,0, fld_WPA_TRIGGER_LO_def    ,READWRITE  },
	{10 ,fld_WPA_TRIGGER_HIGH   ,VIP_LP_WPA_CNTL1       , 16, 0xFC00FFFF, 0, 0,0, fld_WPA_TRIGGER_HIGH_def  ,READWRITE  },
	{10 ,fld_LOCKOUT_START      ,VIP_LP_VERT_LOCKOUT    ,  0, 0xFFFFFC00, 0, 0,0, fld_LOCKOUT_START_def     ,READWRITE  },
	{10 ,fld_LOCKOUT_END        ,VIP_LP_VERT_LOCKOUT    , 16, 0xFC00FFFF, 0, 0,0, fld_LOCKOUT_END_def       ,READWRITE  },
	{24 ,fld_CH_DTO_INC         ,VIP_CP_PLL_CNTL0       ,  0, 0xFF000000, 0, 0,0, fld_CH_DTO_INC_def        ,READWRITE  },
	{4  ,fld_PLL_SGAIN          ,VIP_CP_PLL_CNTL0       , 24, 0xF0FFFFFF, 0, 0,0, fld_PLL_SGAIN_def         ,READWRITE  },
	{4  ,fld_PLL_FGAIN          ,VIP_CP_PLL_CNTL0       , 28, 0x0FFFFFFF, 0, 0,0, fld_PLL_FGAIN_def         ,READWRITE  },
	{9  ,fld_CR_BURST_GAIN      ,VIP_CP_BURST_GAIN      ,  0, 0xFFFFFE00, 0, 0,0, fld_CR_BURST_GAIN_def     ,READWRITE  },
	{9  ,fld_CB_BURST_GAIN      ,VIP_CP_BURST_GAIN      , 16, 0xFE00FFFF, 0, 0,0, fld_CB_BURST_GAIN_def     ,READWRITE  },
	{10 ,fld_VERT_LOCKOUT_START ,VIP_CP_VERT_LOCKOUT    ,  0, 0xFFFFFC00, 0, 0,0, fld_VERT_LOCKOUT_START_def,READWRITE  },
	{10 ,fld_VERT_LOCKOUT_END   ,VIP_CP_VERT_LOCKOUT    , 16, 0xFC00FFFF, 0, 0,0, fld_VERT_LOCKOUT_END_def  ,READWRITE  },
	{11 ,fld_H_IN_WIND_START    ,VIP_SCALER_IN_WINDOW   ,  0, 0xFFFFF800, 0, 0,0, fld_H_IN_WIND_START_def   ,READWRITE  },
	{10 ,fld_V_IN_WIND_START    ,VIP_SCALER_IN_WINDOW   , 16, 0xFC00FFFF, 0, 0,0, fld_V_IN_WIND_START_def   ,READWRITE  },
	{10 ,fld_H_OUT_WIND_WIDTH   ,VIP_SCALER_OUT_WINDOW ,  0, 0xFFFFFC00, 0, 0,0, fld_H_OUT_WIND_WIDTH_def   ,READWRITE  },
	{9  ,fld_V_OUT_WIND_WIDTH   ,VIP_SCALER_OUT_WINDOW , 16, 0xFE00FFFF, 0, 0,0, fld_V_OUT_WIND_WIDTH_def   ,READWRITE  },
	{11 ,fld_HS_LINE_TOTAL      ,VIP_HS_PLINE          ,  0, 0xFFFFF800, 0, 0,0, fld_HS_LINE_TOTAL_def      ,READWRITE  },
	{8  ,fld_MIN_PULSE_WIDTH    ,VIP_HS_MINMAXWIDTH    ,  0, 0xFFFFFF00, 0, 0,0, fld_MIN_PULSE_WIDTH_def    ,READWRITE  },
	{8  ,fld_MAX_PULSE_WIDTH    ,VIP_HS_MINMAXWIDTH    ,  8, 0xFFFF00FF, 0, 0,0, fld_MAX_PULSE_WIDTH_def    ,READWRITE  },
	{11 ,fld_WIN_CLOSE_LIMIT    ,VIP_HS_WINDOW_LIMIT   ,  0, 0xFFFFF800, 0, 0,0, fld_WIN_CLOSE_LIMIT_def    ,READWRITE  },
	{11 ,fld_WIN_OPEN_LIMIT     ,VIP_HS_WINDOW_LIMIT   , 16, 0xF800FFFF, 0, 0,0, fld_WIN_OPEN_LIMIT_def     ,READWRITE  },
	{11 ,fld_VSYNC_INT_TRIGGER  ,VIP_VS_DETECTOR_CNTL   ,  0, 0xFFFFF800, 0, 0,0, fld_VSYNC_INT_TRIGGER_def ,READWRITE  },
	{11 ,fld_VSYNC_INT_HOLD     ,VIP_VS_DETECTOR_CNTL   , 16, 0xF800FFFF, 0, 0,0, fld_VSYNC_INT_HOLD_def        ,READWRITE  },
	{11 ,fld_VIN_M0             ,VIP_VIN_PLL_CNTL      ,  0, 0xFFFFF800, 0, 0,0, fld_VIN_M0_def             ,READWRITE  },
	{11 ,fld_VIN_N0             ,VIP_VIN_PLL_CNTL      , 11, 0xFFC007FF, 0, 0,0, fld_VIN_N0_def             ,READWRITE  },
	{1  ,fld_MNFLIP_EN          ,VIP_VIN_PLL_CNTL      , 22, 0xFFBFFFFF, 0, 0,0, fld_MNFLIP_EN_def          ,READWRITE  },
	{4  ,fld_VIN_P              ,VIP_VIN_PLL_CNTL      , 24, 0xF0FFFFFF, 0, 0,0, fld_VIN_P_def              ,READWRITE  },
	{2  ,fld_REG_CLK_SEL        ,VIP_VIN_PLL_CNTL      , 30, 0x3FFFFFFF, 0, 0,0, fld_REG_CLK_SEL_def        ,READWRITE  },
	{11 ,fld_VIN_M1             ,VIP_VIN_PLL_FINE_CNTL  ,  0, 0xFFFFF800, 0, 0,0, fld_VIN_M1_def            ,READWRITE  },
	{11 ,fld_VIN_N1             ,VIP_VIN_PLL_FINE_CNTL  , 11, 0xFFC007FF, 0, 0,0, fld_VIN_N1_def            ,READWRITE  },
	{1  ,fld_VIN_DRIVER_SEL     ,VIP_VIN_PLL_FINE_CNTL  , 22, 0xFFBFFFFF, 0, 0,0, fld_VIN_DRIVER_SEL_def    ,READWRITE  },
	{1  ,fld_VIN_MNFLIP_REQ     ,VIP_VIN_PLL_FINE_CNTL  , 23, 0xFF7FFFFF, 0, 0,0, fld_VIN_MNFLIP_REQ_def    ,READWRITE  },
	{1  ,fld_VIN_MNFLIP_DONE    ,VIP_VIN_PLL_FINE_CNTL  , 24, 0xFEFFFFFF, 0, 0,0, fld_VIN_MNFLIP_DONE_def   ,READONLY   },
	{1  ,fld_TV_LOCK_TO_VIN     ,VIP_VIN_PLL_FINE_CNTL  , 27, 0xF7FFFFFF, 0, 0,0, fld_TV_LOCK_TO_VIN_def    ,READWRITE  },
	{4  ,fld_TV_P_FOR_WINCLK    ,VIP_VIN_PLL_FINE_CNTL  , 28, 0x0FFFFFFF, 0, 0,0, fld_TV_P_FOR_WINCLK_def   ,READWRITE  },
	{1  ,fld_VINRST             ,VIP_PLL_CNTL1          ,  1, 0xFFFFFFFD, 0, 0,0, fld_VINRST_def            ,READWRITE  },
	{1  ,fld_VIN_CLK_SEL        ,VIP_CLOCK_SEL_CNTL     ,  7, 0xFFFFFF7F, 0, 0,0, fld_VIN_CLK_SEL_def       ,READWRITE  },
	{10 ,fld_VS_FIELD_BLANK_START,VIP_VS_BLANKING_CNTL  ,  0, 0xFFFFFC00, 0, 0,0, fld_VS_FIELD_BLANK_START_def  ,READWRITE  },
	{10 ,fld_VS_FIELD_BLANK_END,VIP_VS_BLANKING_CNTL    , 16, 0xFC00FFFF, 0, 0,0, fld_VS_FIELD_BLANK_END_def    ,READWRITE  },
	{9  ,fld_VS_FIELD_IDLOCATION,VIP_VS_FIELD_ID_CNTL   ,  0, 0xFFFFFE00, 0, 0,0, fld_VS_FIELD_IDLOCATION_def   ,READWRITE  },
	{10 ,fld_VS_FRAME_TOTAL     ,VIP_VS_FRAME_TOTAL     ,  0, 0xFFFFFC00, 0, 0,0, fld_VS_FRAME_TOTAL_def    ,READWRITE  },
	{11 ,fld_SYNC_TIP_START     ,VIP_SG_SYNCTIP_GATE    ,  0, 0xFFFFF800, 0, 0,0, fld_SYNC_TIP_START_def    ,READWRITE  },
	{4  ,fld_SYNC_TIP_LENGTH    ,VIP_SG_SYNCTIP_GATE    , 12, 0xFFFF0FFF, 0, 0,0, fld_SYNC_TIP_LENGTH_def   ,READWRITE  },
	{12 ,fld_GAIN_FORCE_DATA    ,VIP_CP_DEBUG_FORCE     ,  0, 0xFFFFF000, 0, 0,0, fld_GAIN_FORCE_DATA_def   ,READWRITE  },
	{1  ,fld_GAIN_FORCE_EN      ,VIP_CP_DEBUG_FORCE     , 12, 0xFFFFEFFF, 0, 0,0, fld_GAIN_FORCE_EN_def ,READWRITE  },
	{2  ,fld_I_CLAMP_SEL        ,VIP_ADC_CNTL           ,  3, 0xFFFFFFE7, 0, 0,0, fld_I_CLAMP_SEL_def   ,READWRITE  },
	{2  ,fld_I_AGC_SEL          ,VIP_ADC_CNTL           ,  5, 0xFFFFFF9F, 0, 0,0, fld_I_AGC_SEL_def     ,READWRITE  },
	{1  ,fld_EXT_CLAMP_CAP      ,VIP_ADC_CNTL           ,  8, 0xFFFFFEFF, 0, 0,0, fld_EXT_CLAMP_CAP_def ,READWRITE  },
	{1  ,fld_EXT_AGC_CAP        ,VIP_ADC_CNTL           ,  9, 0xFFFFFDFF, 0, 0,0, fld_EXT_AGC_CAP_def       ,READWRITE  },
	{1  ,fld_DECI_DITHER_EN     ,VIP_ADC_CNTL           , 12, 0xFFFFEFFF, 0, 0,0, fld_DECI_DITHER_EN_def ,READWRITE },
	{2  ,fld_ADC_PREFHI         ,VIP_ADC_CNTL           , 22, 0xFF3FFFFF, 0, 0,0, fld_ADC_PREFHI_def        ,READWRITE  },
	{2  ,fld_ADC_CH_GAIN_SEL    ,VIP_ADC_CNTL           , 16, 0xFFFCFFFF, 0, 0,0, fld_ADC_CH_GAIN_SEL_def   ,READWRITE  },
	{4  ,fld_HS_PLL_SGAIN       ,VIP_HS_PLLGAIN         ,  0, 0xFFFFFFF0, 0, 0,0, fld_HS_PLL_SGAIN_def      ,READWRITE  },
	{1  ,fld_NREn               ,VIP_NOISE_CNTL0        ,  0, 0xFFFFFFFE, 0, 0,0, fld_NREn_def      ,READWRITE  },
	{3  ,fld_NRGainCntl         ,VIP_NOISE_CNTL0        ,  1, 0xFFFFFFF1, 0, 0,0, fld_NRGainCntl_def        ,READWRITE  },
	{6  ,fld_NRBWTresh          ,VIP_NOISE_CNTL0        ,  4, 0xFFFFFC0F, 0, 0,0, fld_NRBWTresh_def     ,READWRITE  },
	{5  ,fld_NRGCTresh          ,VIP_NOISE_CNTL0       ,  10, 0xFFFF83FF, 0, 0,0, fld_NRGCTresh_def     ,READWRITE  },
	{1  ,fld_NRCoefDespeclMode  ,VIP_NOISE_CNTL0       ,  15, 0xFFFF7FFF, 0, 0,0, fld_NRCoefDespeclMode_def     ,READWRITE  },
	{1  ,fld_GPIO_5_OE      ,VIP_GPIO_CNTL      ,  5, 0xFFFFFFDF, 0, 0,0, fld_GPIO_5_OE_def     ,READWRITE  },
	{1  ,fld_GPIO_6_OE      ,VIP_GPIO_CNTL      ,  6, 0xFFFFFFBF, 0, 0,0, fld_GPIO_6_OE_def     ,READWRITE  },
	{1  ,fld_GPIO_5_OUT     ,VIP_GPIO_INOUT    ,   5, 0xFFFFFFDF, 0, 0,0, fld_GPIO_5_OUT_def        ,READWRITE  },
	{1  ,fld_GPIO_6_OUT     ,VIP_GPIO_INOUT    ,   6, 0xFFFFFFBF, 0, 0,0, fld_GPIO_6_OUT_def        ,READWRITE  },
};

/* Rage Theatre's register fields default values: */
static uint32_t rt_regdef[regRT_MAX_REGS] = {
	fld_tmpReg1_def,
	fld_tmpReg2_def,
	fld_tmpReg3_def,
	fld_LP_CONTRAST_def,
	fld_LP_BRIGHTNESS_def,
	fld_CP_HUE_CNTL_def,
	fld_LUMA_FILTER_def,
	fld_H_SCALE_RATIO_def,
	fld_H_SHARPNESS_def,
	fld_V_SCALE_RATIO_def,
	fld_V_DEINTERLACE_ON_def,
	fld_V_BYPSS_def,
	fld_V_DITHER_ON_def,
	fld_EVENF_OFFSET_def,
	fld_ODDF_OFFSET_def,
	fld_INTERLACE_DETECTED_def,
	fld_VS_LINE_COUNT_def,
	fld_VS_DETECTED_LINES_def,
	fld_VS_ITU656_VB_def,
	fld_VBI_CC_DATA_def,
	fld_VBI_CC_WT_def,
	fld_VBI_CC_WT_ACK_def,
	fld_VBI_CC_HOLD_def,
	fld_VBI_DECODE_EN_def,
	fld_VBI_CC_DTO_P_def,
	fld_VBI_20BIT_DTO_P_def,
	fld_VBI_CC_LEVEL_def,
	fld_VBI_20BIT_LEVEL_def,
	fld_VBI_CLK_RUNIN_GAIN_def,
	fld_H_VBI_WIND_START_def,
	fld_H_VBI_WIND_END_def,
	fld_V_VBI_WIND_START_def,
	fld_V_VBI_WIND_END_def,
	fld_VBI_20BIT_DATA0_def,
	fld_VBI_20BIT_DATA1_def,
	fld_VBI_20BIT_WT_def,
	fld_VBI_20BIT_WT_ACK_def,
	fld_VBI_20BIT_HOLD_def,
	fld_VBI_CAPTURE_ENABLE_def,
	fld_VBI_EDS_DATA_def,
	fld_VBI_EDS_WT_def,
	fld_VBI_EDS_WT_ACK_def,
	fld_VBI_EDS_HOLD_def,
	fld_VBI_SCALING_RATIO_def,
	fld_VBI_ALIGNER_ENABLE_def,
	fld_H_ACTIVE_START_def,
	fld_H_ACTIVE_END_def,
	fld_V_ACTIVE_START_def,
	fld_V_ACTIVE_END_def,
	fld_CH_HEIGHT_def,
	fld_CH_KILL_LEVEL_def,
	fld_CH_AGC_ERROR_LIM_def,
	fld_CH_AGC_FILTER_EN_def,
	fld_CH_AGC_LOOP_SPEED_def,
	fld_HUE_ADJ_def,
	fld_STANDARD_SEL_def,
	fld_STANDARD_YC_def,
	fld_ADC_PDWN_def,
	fld_INPUT_SELECT_def,
	fld_ADC_PREFLO_def,
	fld_H_SYNC_PULSE_WIDTH_def,
	fld_HS_GENLOCKED_def,
	fld_HS_SYNC_IN_WIN_def,
	fld_VIN_ASYNC_RST_def,
	fld_DVS_ASYNC_RST_def,
	fld_VIP_VENDOR_ID_def,
	fld_VIP_DEVICE_ID_def,
	fld_VIP_REVISION_ID_def,
	fld_BLACK_INT_START_def,
	fld_BLACK_INT_LENGTH_def,
	fld_UV_INT_START_def,
	fld_U_INT_LENGTH_def,
	fld_V_INT_LENGTH_def,
	fld_CRDR_ACTIVE_GAIN_def,
	fld_CBDB_ACTIVE_GAIN_def,
	fld_DVS_DIRECTION_def,
	fld_DVS_VBI_UINT8_SWAP_def,
	fld_DVS_CLK_SELECT_def,
	fld_CONTINUOUS_STREAM_def,
	fld_DVSOUT_CLK_DRV_def,
	fld_DVSOUT_DATA_DRV_def,
	fld_COMB_CNTL0_def,
	fld_COMB_CNTL1_def,
	fld_COMB_CNTL2_def,
	fld_COMB_LENGTH_def,
	fld_SYNCTIP_REF0_def,
	fld_SYNCTIP_REF1_def,
	fld_CLAMP_REF_def,
	fld_AGC_PEAKWHITE_def,
	fld_VBI_PEAKWHITE_def,
	fld_WPA_THRESHOLD_def,
	fld_WPA_TRIGGER_LO_def,
	fld_WPA_TRIGGER_HIGH_def,
	fld_LOCKOUT_START_def,
	fld_LOCKOUT_END_def,
	fld_CH_DTO_INC_def,
	fld_PLL_SGAIN_def,
	fld_PLL_FGAIN_def,
	fld_CR_BURST_GAIN_def,
	fld_CB_BURST_GAIN_def,
	fld_VERT_LOCKOUT_START_def,
	fld_VERT_LOCKOUT_END_def,
	fld_H_IN_WIND_START_def,
	fld_V_IN_WIND_START_def,
	fld_H_OUT_WIND_WIDTH_def,
	fld_V_OUT_WIND_WIDTH_def,
	fld_HS_LINE_TOTAL_def,
	fld_MIN_PULSE_WIDTH_def,
	fld_MAX_PULSE_WIDTH_def,
	fld_WIN_CLOSE_LIMIT_def,
	fld_WIN_OPEN_LIMIT_def,
	fld_VSYNC_INT_TRIGGER_def,
	fld_VSYNC_INT_HOLD_def,
	fld_VIN_M0_def,
	fld_VIN_N0_def,
	fld_MNFLIP_EN_def,
	fld_VIN_P_def,
	fld_REG_CLK_SEL_def,
	fld_VIN_M1_def,
	fld_VIN_N1_def,
	fld_VIN_DRIVER_SEL_def,
	fld_VIN_MNFLIP_REQ_def,
	fld_VIN_MNFLIP_DONE_def,
	fld_TV_LOCK_TO_VIN_def,
	fld_TV_P_FOR_WINCLK_def,
	fld_VINRST_def,
	fld_VIN_CLK_SEL_def,
	fld_VS_FIELD_BLANK_START_def,
	fld_VS_FIELD_BLANK_END_def,
	fld_VS_FIELD_IDLOCATION_def,
	fld_VS_FRAME_TOTAL_def,
	fld_SYNC_TIP_START_def,
	fld_SYNC_TIP_LENGTH_def,
	fld_GAIN_FORCE_DATA_def,
	fld_GAIN_FORCE_EN_def,
	fld_I_CLAMP_SEL_def,
	fld_I_AGC_SEL_def,
	fld_EXT_CLAMP_CAP_def,
	fld_EXT_AGC_CAP_def,
	fld_DECI_DITHER_EN_def,
	fld_ADC_PREFHI_def,
	fld_ADC_CH_GAIN_SEL_def,
	fld_HS_PLL_SGAIN_def,
	fld_NREn_def,
	fld_NRGainCntl_def,
	fld_NRBWTresh_def,
	fld_NRGCTresh_def,
	fld_NRCoefDespeclMode_def,
	fld_GPIO_5_OE_def,
	fld_GPIO_6_OE_def,
	fld_GPIO_5_OUT_def,
	fld_GPIO_6_OUT_def,
};

static void rt_write_fld1(struct theatre *t, uint32_t reg, uint32_t data)
{
	uint32_t result = 0;
	uint32_t value = 0;

	if (theatre_read(t, rt_regmap[reg].reg_addr_lsb, &result) == true) {
		value = (result & rt_regmap[reg].mask_lsb) |
			(data << rt_regmap[reg].fld_offset_lsb);

		if (theatre_write(t, rt_regmap[reg].reg_addr_lsb, value) == true) {
			rt_regmap[reg].curr_value = data;
		}
	}
	return;
}

static uint32_t rt_read_fld1(struct theatre *t, uint32_t reg)
{
	uint32_t result;

	if (theatre_read(t, rt_regmap[reg].reg_addr_lsb, &result) == true) {
		rt_regmap[reg].curr_value = ((result & ~rt_regmap[reg].mask_lsb) >>
					     rt_regmap[reg].fld_offset_lsb);
		return rt_regmap[reg].curr_value;
	} else {
		return 0xffffffff;
	}
}

static void rt_setvinclock(struct theatre *t, uint16_t standard)
{
	uint32_t m0 = 0, n0 = 0, p = 0;
	uint8_t ref_freq;
	
	ref_freq = (t->video_decoder_type & 0xf0) >> 4;

	switch(standard & 0xff) {
	case DEC_NTSC:
		switch (standard & 0xff00) {
		case extNONE:
		case extNTSC:
		case extNTSC_J:
			if (ref_freq == RT_FREF_2950) {
				m0 = 0x39;
				n0 = 0x14c;
				p = 0x6;
			} else {
				m0 = 0x0b;
				n0 = 0x46;
				p = 0x6;
			}
			break;
		case extNTSC_443:
			if (ref_freq == RT_FREF_2950) {
				m0 = 0x23;
				n0 = 0x88;
				p = 0x7;
			} else {
				m0 = 0x2c;
				n0 = 0x121;
				p = 0x5;
			}
			break;
		case extPAL_M:
			if (ref_freq == RT_FREF_2950) {
				m0 = 0x2c;
				n0 = 0x12b;
				p = 0x7;
			} else {
				m0 = 0xb;
				n0 = 0x46;
				p = 0x6;
			}
			break;

		default:
			return;
		}
		break;
	case DEC_PAL:
		switch (standard & 0xff00) {
		case extPAL:
		case extPAL_N:
		case extPAL_BGHI:
		case extPAL_60:
			if (ref_freq == RT_FREF_2950) {
				m0 = 0x0E;
				n0 = 0x65;
				p  = 0x6;
			} else {
				m0 = 0x2C;
				n0 = 0x0121;
				p  = 0x5;
			}
			break;
                case extPAL_NCOMB:
			if (ref_freq == RT_FREF_2950) {
				m0 = 0x23;
				n0 = 0x88;
				p  = 0x7;
			} else {
				m0 = 0x37;
				n0 = 0x1D3;
				p  = 0x8;
			}
			break;
                default:
			return;
		}
		break;
        case DEC_SECAM:
		if (ref_freq == RT_FREF_2950) {
			m0 =  0xE;
			n0 =  0x65;
			p  =  0x6;
		} else {
			m0 =  0x2C;
			n0 =  0x121;
			p  =  0x5;
		}
		break;
	}
	

	/* VIN_PLL_CNTL */
	rt_write_fld1(t, fld_VIN_M0, m0);
	rt_write_fld1(t, fld_VIN_N0, n0);
	rt_write_fld1(t, fld_VIN_P, p);
}

void rt_set_tint (struct theatre *t, int hue)
{
	uint32_t nhue = 0;

	t->hue=hue;
	/* Scale hue value from -1000<->1000 to -180<->180 */
	/* oh pain no floats here */
	//hue = (double)(hue+1000) * 0.18 - 180;
	
	/* Validate Hue level */
	if (hue < -180)
		hue = -180;
	else if (hue > 180)
		hue = 180;

	/* save the "validated" hue, but scale it back up to -1000<->1000 */
	t->hue = 0;//(double)hue/0.18;

	switch (t->standard & 0x00FF) {
	case DEC_NTSC: /* original ATI code had _empty_ section for PAL/SECAM... which did not work,
			  obviously */
	case DEC_PAL:
	case DEC_SECAM:
		if (hue >= 0)
			nhue = (uint32_t) (256 * hue)/360;
		else
			nhue = (uint32_t) (256 * (hue + 360))/360;
		break;
	default:
		break;
	}
	
	rt_write_fld1(t, fld_CP_HUE_CNTL, hue);
}

void rt_set_saturation(struct theatre *t, int saturation)
{
	uint16_t   saturation_v, saturation_u;
//	double dbSaturation = 0, dbCrGain = 0, dbCbGain = 0;

	/* VALIDATE SATURATION LEVEL */
	if (saturation < -1000L)
		saturation = -1000;
	else if (saturation > 1000L)
		saturation = 1000;

	t->saturation = saturation;

#if 0 // TODO
	if (saturation > 0) 
		/* Scale saturation up, to use full allowable register width */
		saturation = (double)(Saturation) * 4.9;

	dbSaturation = (double) (Saturation+1000.0) / 1000.0;

	CalculateCrCbGain (t, &dbCrGain, &dbCbGain, t->wStandard);

	saturation_u = (uint16_t) ((dbCrGain * dbSaturation * 128.0) + 0.5);
	saturation_v = (uint16_t) ((dbCbGain * dbSaturation * 128.0) + 0.5);

	/* SET SATURATION LEVEL */
	rt_write_fld1(t, fld_CRDR_ACTIVE_GAIN, wSaturation_U);
	rt_write_fld1(t, fld_CBDB_ACTIVE_GAIN, wSaturation_V);

#endif
	t->saturation_u = saturation_u;
	t->saturation_v = saturation_v;

	return;
} /* RT_SetSaturation ()...*/

/****************************************************************************
 * RT_SetBrightness (int Brightness)                                        *
 *  Function: sets the brightness level for the Rage Theatre video in       *
 *    Inputs: int Brightness - the brightness value to be set.              *
 *   Outputs: NONE                                                          *
 ****************************************************************************/
void rt_set_brightness(struct theatre *t, int brightness)
{
#if 0 // TODO
    double dbSynctipRef0=0, dbContrast=1;

    double dbYgain=0;
    double dbBrightness=0;
    double dbSetup=0;
    uint16_t   wBrightness=0;

    /* VALIDATE BRIGHTNESS LEVEL */
    if (Brightness < -1000)
    {
        Brightness = -1000;
    }
    else if (Brightness > 1000)
    {
        Brightness = 1000;
    }

    /* Save value */
    t->iBrightness = Brightness;

    t->dbBrightnessRatio =  (double) (Brightness+1000.0) / 10.0;

    dbBrightness = (double) (Brightness)/10.0;

    dbSynctipRef0 = ReadRT_fld (fld_SYNCTIP_REF0);

    if(t->dbContrast == 0)
    {
        t->dbContrast = 1.0; /*NTSC default; */
    }

    dbContrast = (double) t->dbContrast;

    /* Use the following formula to determine the brightness level */
    switch (t->standard & 0x00FF) {
    case DEC_NTSC:
            if ((t->standard & 0xFF00) == extNTSC_J)
		    dbYgain = 219.0 / ( 100.0 * (double)(dbSynctipRef0) /40.0);
            else
	    {
		    dbSetup = 7.5 * (double)(dbSynctipRef0) / 40.0;
                dbYgain = 219.0 / (92.5 * (double)(dbSynctipRef0) / 40.0);
            }
            break;
    case DEC_PAL:
    case DEC_SECAM:
		dbYgain = 219.0 / ( 100.0 * (double)(dbSynctipRef0) /43.0);
		break;
    default:
            break;
    }

    brightness = (uint16_t) (16.0 * ((dbBrightness-dbSetup) + (16.0 / (dbContrast * dbYgain))));

    rt_write_fld1(t, fld_LP_BRIGHTNESS, brightness);

    /*RT_SetSaturation (t->iSaturation); */

    return;
#endif
} /* RT_SetBrightness ()... */


/****************************************************************************
 * RT_SetSharpness (uint16_t wSharpness)                                        *
 *  Function: sets the sharpness level for the Rage Theatre video in        *
 *    Inputs: uint16_t wSharpness - the sharpness value to be set.              *
 *   Outputs: NONE                                                          *
 ****************************************************************************/
void rt_set_sharpness(struct theatre *t, uint16_t sharpness)
{
    switch (sharpness) {
    case DEC_SMOOTH :
            rt_write_fld1(t, fld_H_SHARPNESS, RT_NORM_SHARPNESS);
            t->sharpness = RT_NORM_SHARPNESS;
            break;
    case DEC_SHARP  :
            rt_write_fld1(t, fld_H_SHARPNESS, RT_HIGH_SHARPNESS);
            t->sharpness = RT_HIGH_SHARPNESS;
            break;
    default:
            break;
    }
    return;

} /* RT_SetSharpness ()... */


/****************************************************************************
 * RT_SetContrast (int Contrast)                                            *
 *  Function: sets the contrast level for the Rage Theatre video in         *
 *    Inputs: int Contrast - the contrast value to be set.                  *
 *   Outputs: NONE                                                          *
 ****************************************************************************/
void rt_set_contrast (struct theatre * t, int contrast)
{
#if 0
    double dbSynctipRef0=0, dbContrast=0;
    double dbYgain=0;
    uint8_t   bTempContrast=0;

    /* VALIDATE CONTRAST LEVEL */
    if (Contrast < -1000)
    {
        Contrast = -1000;
    }
    else if (Contrast > 1000)
    {
        Contrast = 1000;
    }

    /* Save contrast value */
    t->iContrast = Contrast;

    dbSynctipRef0 = ReadRT_fld (fld_SYNCTIP_REF0);
    dbContrast = (double) (Contrast+1000.0) / 1000.0;

    switch (t->wStandard & 0x00FF)
    {
        case (DEC_NTSC):
            if ((t->wStandard & 0xFF00) == (extNTSC_J))
            {
                dbYgain = 219.0 / ( 100.0 * (double)(dbSynctipRef0) /40.0);
            }
            else
            {
                dbYgain = 219.0 / ( 92.5 * (double)(dbSynctipRef0) /40.0);
            }
            break;
        case (DEC_PAL):
        case (DEC_SECAM):
            dbYgain = 219.0 / ( 100.0 * (double)(dbSynctipRef0) /43.0);
            break;
        default:
            break;
    }

    bTempContrast = (uint8_t) ((dbContrast * dbYgain * 64) + 0.5);

    rt_write_fld1(t, fld_LP_CONTRAST, (uint32_t)bTempContrast);

    /* Save value for future modification */
    t->dbContrast = dbContrast;
#endif
    return;

} /* RT_SetContrast ()... */

/****************************************************************************
 * RT_SetInterlace (uint8_t bInterlace)                                        *
 *  Function: to set the interlacing pattern for the Rage Theatre video in  *
 *    Inputs: uint8_t bInterlace                                               *
 *   Outputs: NONE                                                          *
 ****************************************************************************/
void rt_set_interlace (struct theatre *t, bool interlace)
{
	if (interlace) {
		rt_write_fld1(t, fld_V_DEINTERLACE_ON, 0x1);
		t->interlaced = (uint16_t) RT_DECINTERLACED;
	} else {
		rt_write_fld1(t, fld_V_DEINTERLACE_ON, RT_DECNONINTERLACED);
		t->interlaced = (uint16_t) RT_DECNONINTERLACED;
	}
	
} /* RT_SetInterlace ()... */

void rt_set_standard(struct theatre *t, uint16_t standard)
{
//    double dbFsamp=0, dbLPeriod=0, dbFPeriod=0;
	uint16_t   frame_total = 0;
//    double dbSPPeriod = 4.70;
    
	DRM_DEBUG_KMS("Rage theatre setting standard 0x%04x\n", standard);
	t->standard = standard;
    	
	/* Get the constants for the given standard. */    
//    GetStandardConstants (&dbLPeriod, &dbFPeriod, &dbFsamp, wStandard);

//    frame_total = (uint16_t) (((2.0 * dbFPeriod) * 1000 / dbLPeriod) + 0.5);

	/* Procedures before setting the standards: */
	rt_write_fld1(t, fld_VIN_CLK_SEL, RT_REF_CLK);
	rt_write_fld1(t, fld_VINRST, RT_VINRST_RESET);

	rt_setvinclock(t, standard);

	rt_write_fld1(t, fld_VINRST, RT_VINRST_ACTIVE);
	rt_write_fld1(t, fld_VIN_CLK_SEL, RT_PLL_VIN_CLK);
	
	/* Program the new standards: */
	switch (standard & 0x00FF) {
	case DEC_NTSC: /*NTSC GROUP - 480 lines */
		rt_write_fld1(t, fld_STANDARD_SEL,     RT_NTSC);
		rt_write_fld1(t, fld_SYNCTIP_REF0,     RT_NTSCM_SYNCTIP_REF0);
		rt_write_fld1(t, fld_SYNCTIP_REF1,     RT_NTSCM_SYNCTIP_REF1);
		rt_write_fld1(t, fld_CLAMP_REF,         RT_NTSCM_CLAMP_REF);
		rt_write_fld1(t, fld_AGC_PEAKWHITE,    RT_NTSCM_PEAKWHITE);
		rt_write_fld1(t, fld_VBI_PEAKWHITE,    RT_NTSCM_VBI_PEAKWHITE);
		rt_write_fld1(t, fld_WPA_THRESHOLD,    RT_NTSCM_WPA_THRESHOLD);
		rt_write_fld1(t, fld_WPA_TRIGGER_LO,   RT_NTSCM_WPA_TRIGGER_LO);
		rt_write_fld1(t, fld_WPA_TRIGGER_HIGH, RT_NTSCM_WPA_TRIGGER_HIGH);
		rt_write_fld1(t, fld_LOCKOUT_START,    RT_NTSCM_LP_LOCKOUT_START);
		rt_write_fld1(t, fld_LOCKOUT_END,      RT_NTSCM_LP_LOCKOUT_END);
		rt_write_fld1(t, fld_CH_DTO_INC,       RT_NTSCM_CH_DTO_INC);
		rt_write_fld1(t, fld_PLL_SGAIN,        RT_NTSCM_CH_PLL_SGAIN);
		rt_write_fld1(t, fld_PLL_FGAIN,        RT_NTSCM_CH_PLL_FGAIN);

		rt_write_fld1(t, fld_CH_HEIGHT,        RT_NTSCM_CH_HEIGHT);
		rt_write_fld1(t, fld_CH_KILL_LEVEL,    RT_NTSCM_CH_KILL_LEVEL);

		rt_write_fld1(t, fld_CH_AGC_ERROR_LIM, RT_NTSCM_CH_AGC_ERROR_LIM);
		rt_write_fld1(t, fld_CH_AGC_FILTER_EN, RT_NTSCM_CH_AGC_FILTER_EN);
		rt_write_fld1(t, fld_CH_AGC_LOOP_SPEED,RT_NTSCM_CH_AGC_LOOP_SPEED);

		rt_write_fld1(t, fld_VS_FIELD_BLANK_START,  RT_NTSCM_VS_FIELD_BLANK_START);
		rt_write_fld1(t, fld_VS_FIELD_BLANK_END,   RT_NTSCM_VS_FIELD_BLANK_END);

		rt_write_fld1(t, fld_H_ACTIVE_START,   RT_NTSCM_H_ACTIVE_START);
		rt_write_fld1(t, fld_H_ACTIVE_END,   RT_NTSCM_H_ACTIVE_END);

		rt_write_fld1(t, fld_V_ACTIVE_START,   RT_NTSCM_V_ACTIVE_START);
		rt_write_fld1(t, fld_V_ACTIVE_END,   RT_NTSCM_V_ACTIVE_END);

		rt_write_fld1(t, fld_H_VBI_WIND_START,   RT_NTSCM_H_VBI_WIND_START);
		rt_write_fld1(t, fld_H_VBI_WIND_END,   RT_NTSCM_H_VBI_WIND_END);

		rt_write_fld1(t, fld_V_VBI_WIND_START,   RT_NTSCM_V_VBI_WIND_START);
		rt_write_fld1(t, fld_V_VBI_WIND_END,   RT_NTSCM_V_VBI_WIND_END);

//TODO            rt_write_fld1(t, fld_UV_INT_START,   (uint8_t)((0.10 * dbLPeriod * dbFsamp / 2.0) + 0.5 - 32));

		rt_write_fld1(t, fld_VSYNC_INT_TRIGGER , (uint16_t) RT_NTSCM_VSYNC_INT_TRIGGER);
		rt_write_fld1(t, fld_VSYNC_INT_HOLD, (uint16_t) RT_NTSCM_VSYNC_INT_HOLD);
	    
		switch (standard & 0xFF00) {
		case extPAL_M:
		case extNONE:
		case extNTSC:
			rt_write_fld1(t, fld_CR_BURST_GAIN,        RT_NTSCM_CR_BURST_GAIN);
			rt_write_fld1(t, fld_CB_BURST_GAIN,        RT_NTSCM_CB_BURST_GAIN);
		    
			rt_write_fld1(t, fld_CRDR_ACTIVE_GAIN,     RT_NTSCM_CRDR_ACTIVE_GAIN);
			rt_write_fld1(t, fld_CBDB_ACTIVE_GAIN,     RT_NTSCM_CBDB_ACTIVE_GAIN);
		    
			rt_write_fld1(t, fld_VERT_LOCKOUT_START,   RT_NTSCM_VERT_LOCKOUT_START);
			rt_write_fld1(t, fld_VERT_LOCKOUT_END,     RT_NTSCM_VERT_LOCKOUT_END);
		    
			break;
		case extNTSC_J:
			rt_write_fld1(t, fld_CR_BURST_GAIN,        RT_NTSCJ_CR_BURST_GAIN);
			rt_write_fld1(t, fld_CB_BURST_GAIN,        RT_NTSCJ_CB_BURST_GAIN);

			rt_write_fld1(t, fld_CRDR_ACTIVE_GAIN,     RT_NTSCJ_CRDR_ACTIVE_GAIN);
			rt_write_fld1(t, fld_CBDB_ACTIVE_GAIN,     RT_NTSCJ_CBDB_ACTIVE_GAIN);

			rt_write_fld1(t, fld_CH_HEIGHT,            RT_NTSCJ_CH_HEIGHT);
			rt_write_fld1(t, fld_CH_KILL_LEVEL,        RT_NTSCJ_CH_KILL_LEVEL);

			rt_write_fld1(t, fld_CH_AGC_ERROR_LIM,     RT_NTSCJ_CH_AGC_ERROR_LIM);
			rt_write_fld1(t, fld_CH_AGC_FILTER_EN,     RT_NTSCJ_CH_AGC_FILTER_EN);
			rt_write_fld1(t, fld_CH_AGC_LOOP_SPEED,    RT_NTSCJ_CH_AGC_LOOP_SPEED);

			rt_write_fld1(t, fld_VERT_LOCKOUT_START,   RT_NTSCJ_VERT_LOCKOUT_START);
			rt_write_fld1(t, fld_VERT_LOCKOUT_END,     RT_NTSCJ_VERT_LOCKOUT_END);
		    
			break;
		default:
			break;
		}
		break;
	case DEC_PAL:  /*PAL GROUP  - 525 lines */
		rt_write_fld1(t, fld_STANDARD_SEL,     RT_PAL);
		rt_write_fld1(t, fld_SYNCTIP_REF0,     RT_PAL_SYNCTIP_REF0);
		rt_write_fld1(t, fld_SYNCTIP_REF1,     RT_PAL_SYNCTIP_REF1);
		
		rt_write_fld1(t, fld_CLAMP_REF,         RT_PAL_CLAMP_REF);
		rt_write_fld1(t, fld_AGC_PEAKWHITE,    RT_PAL_PEAKWHITE);
		rt_write_fld1(t, fld_VBI_PEAKWHITE,    RT_PAL_VBI_PEAKWHITE);

		rt_write_fld1(t, fld_WPA_THRESHOLD,    RT_PAL_WPA_THRESHOLD);
		rt_write_fld1(t, fld_WPA_TRIGGER_LO,   RT_PAL_WPA_TRIGGER_LO);
		rt_write_fld1(t, fld_WPA_TRIGGER_HIGH, RT_PAL_WPA_TRIGGER_HIGH);

		rt_write_fld1(t, fld_LOCKOUT_START,RT_PAL_LP_LOCKOUT_START);
		rt_write_fld1(t, fld_LOCKOUT_END,  RT_PAL_LP_LOCKOUT_END);
		rt_write_fld1(t, fld_CH_DTO_INC,       RT_PAL_CH_DTO_INC);
		rt_write_fld1(t, fld_PLL_SGAIN,        RT_PAL_CH_PLL_SGAIN);
		rt_write_fld1(t, fld_PLL_FGAIN,        RT_PAL_CH_PLL_FGAIN);

		rt_write_fld1(t, fld_CR_BURST_GAIN,    RT_PAL_CR_BURST_GAIN);
		rt_write_fld1(t, fld_CB_BURST_GAIN,    RT_PAL_CB_BURST_GAIN);

		rt_write_fld1(t, fld_CRDR_ACTIVE_GAIN, RT_PAL_CRDR_ACTIVE_GAIN);
		rt_write_fld1(t, fld_CBDB_ACTIVE_GAIN, RT_PAL_CBDB_ACTIVE_GAIN);

		rt_write_fld1(t, fld_CH_HEIGHT,        RT_PAL_CH_HEIGHT);
		rt_write_fld1(t, fld_CH_KILL_LEVEL,    RT_PAL_CH_KILL_LEVEL);

		rt_write_fld1(t, fld_CH_AGC_ERROR_LIM, RT_PAL_CH_AGC_ERROR_LIM);
		rt_write_fld1(t, fld_CH_AGC_FILTER_EN, RT_PAL_CH_AGC_FILTER_EN);
		rt_write_fld1(t, fld_CH_AGC_LOOP_SPEED,RT_PAL_CH_AGC_LOOP_SPEED);

		rt_write_fld1(t, fld_VERT_LOCKOUT_START,   RT_PAL_VERT_LOCKOUT_START);
		rt_write_fld1(t, fld_VERT_LOCKOUT_END, RT_PAL_VERT_LOCKOUT_END);
		rt_write_fld1(t, fld_VS_FIELD_BLANK_START,  (uint16_t)RT_PALSEM_VS_FIELD_BLANK_START);

		rt_write_fld1(t, fld_VS_FIELD_BLANK_END,   RT_PAL_VS_FIELD_BLANK_END);

		rt_write_fld1(t, fld_H_ACTIVE_START,   RT_PAL_H_ACTIVE_START);
		rt_write_fld1(t, fld_H_ACTIVE_END,   RT_PAL_H_ACTIVE_END);

		rt_write_fld1(t, fld_V_ACTIVE_START,   RT_PAL_V_ACTIVE_START);
		rt_write_fld1(t, fld_V_ACTIVE_END,   RT_PAL_V_ACTIVE_END);

		rt_write_fld1(t, fld_H_VBI_WIND_START,   RT_PAL_H_VBI_WIND_START);
		rt_write_fld1(t, fld_H_VBI_WIND_END,   RT_PAL_H_VBI_WIND_END);

		rt_write_fld1(t, fld_V_VBI_WIND_START,   RT_PAL_V_VBI_WIND_START);
		rt_write_fld1(t, fld_V_VBI_WIND_END,   RT_PAL_V_VBI_WIND_END);

		/* Magic 0.10 is correct - according to Ivo. Also see SECAM code below */
/*            rt_write_fld1(t, fld_UV_INT_START,   (uint8_t)( (0.12 * dbLPeriod * dbFsamp / 2.0) + 0.5 - 32 )); */
//TODO            rt_write_fld1(t, fld_UV_INT_START,   (uint8_t)( (0.10 * dbLPeriod * dbFsamp / 2.0) + 0.5 - 32 ));

		rt_write_fld1(t, fld_VSYNC_INT_TRIGGER , (uint16_t) RT_PALSEM_VSYNC_INT_TRIGGER);
		rt_write_fld1(t, fld_VSYNC_INT_HOLD, (uint16_t) RT_PALSEM_VSYNC_INT_HOLD);

		break;
	case DEC_SECAM:  /*PAL GROUP*/
		rt_write_fld1(t, fld_STANDARD_SEL,     RT_SECAM);
		rt_write_fld1(t, fld_SYNCTIP_REF0,     RT_SECAM_SYNCTIP_REF0);
		rt_write_fld1(t, fld_SYNCTIP_REF1,     RT_SECAM_SYNCTIP_REF1);
		rt_write_fld1(t, fld_CLAMP_REF,         RT_SECAM_CLAMP_REF);
		rt_write_fld1(t, fld_AGC_PEAKWHITE,    RT_SECAM_PEAKWHITE);
		rt_write_fld1(t, fld_VBI_PEAKWHITE,    RT_SECAM_VBI_PEAKWHITE);

		rt_write_fld1(t, fld_WPA_THRESHOLD,    RT_SECAM_WPA_THRESHOLD);

		rt_write_fld1(t, fld_WPA_TRIGGER_LO,   RT_SECAM_WPA_TRIGGER_LO);
		rt_write_fld1(t, fld_WPA_TRIGGER_HIGH, RT_SECAM_WPA_TRIGGER_HIGH);

		rt_write_fld1(t, fld_LOCKOUT_START,RT_SECAM_LP_LOCKOUT_START);
		rt_write_fld1(t, fld_LOCKOUT_END,  RT_SECAM_LP_LOCKOUT_END);

		rt_write_fld1(t, fld_CH_DTO_INC,       RT_SECAM_CH_DTO_INC);
		rt_write_fld1(t, fld_PLL_SGAIN,        RT_SECAM_CH_PLL_SGAIN);
		rt_write_fld1(t, fld_PLL_FGAIN,        RT_SECAM_CH_PLL_FGAIN);

		rt_write_fld1(t, fld_CR_BURST_GAIN,    RT_SECAM_CR_BURST_GAIN);
		rt_write_fld1(t, fld_CB_BURST_GAIN,    RT_SECAM_CB_BURST_GAIN);

		rt_write_fld1(t, fld_CRDR_ACTIVE_GAIN, RT_SECAM_CRDR_ACTIVE_GAIN);
		rt_write_fld1(t, fld_CBDB_ACTIVE_GAIN, RT_SECAM_CBDB_ACTIVE_GAIN);

		rt_write_fld1(t, fld_CH_HEIGHT,        RT_SECAM_CH_HEIGHT);
		rt_write_fld1(t, fld_CH_KILL_LEVEL,    RT_SECAM_CH_KILL_LEVEL);

		rt_write_fld1(t, fld_CH_AGC_ERROR_LIM, RT_SECAM_CH_AGC_ERROR_LIM);
		rt_write_fld1(t, fld_CH_AGC_FILTER_EN, RT_SECAM_CH_AGC_FILTER_EN);
		rt_write_fld1(t, fld_CH_AGC_LOOP_SPEED,RT_SECAM_CH_AGC_LOOP_SPEED);

		rt_write_fld1(t, fld_VERT_LOCKOUT_START,   RT_SECAM_VERT_LOCKOUT_START);  /*Might not need */
		rt_write_fld1(t, fld_VERT_LOCKOUT_END, RT_SECAM_VERT_LOCKOUT_END);  /* Might not need */

		rt_write_fld1(t, fld_VS_FIELD_BLANK_START,  (uint16_t)RT_PALSEM_VS_FIELD_BLANK_START);
		rt_write_fld1(t, fld_VS_FIELD_BLANK_END,   RT_PAL_VS_FIELD_BLANK_END);

		rt_write_fld1(t, fld_H_ACTIVE_START,   RT_PAL_H_ACTIVE_START);
		rt_write_fld1(t, fld_H_ACTIVE_END,   RT_PAL_H_ACTIVE_END);

		rt_write_fld1(t, fld_V_ACTIVE_START,   RT_PAL_V_ACTIVE_START);
		rt_write_fld1(t, fld_V_ACTIVE_END,   RT_PAL_V_ACTIVE_END);

		rt_write_fld1(t, fld_H_VBI_WIND_START,   RT_PAL_H_VBI_WIND_START);
		rt_write_fld1(t, fld_H_VBI_WIND_END,   RT_PAL_H_VBI_WIND_END);

		rt_write_fld1(t, fld_V_VBI_WIND_START,   RT_PAL_V_VBI_WIND_START);
		rt_write_fld1(t, fld_V_VBI_WIND_END,   RT_PAL_V_VBI_WIND_END);

		rt_write_fld1(t, fld_VSYNC_INT_TRIGGER , (uint16_t) RT_PALSEM_VSYNC_INT_TRIGGER);
		rt_write_fld1(t, fld_VSYNC_INT_HOLD, (uint16_t) RT_PALSEM_VSYNC_INT_HOLD);

/*            rt_write_fld1(t, fld_UV_INT_START,   (uint8_t)( (0.12 * dbLPeriod * dbFsamp / 2.0) + 0.5 - 32 )); */
//TODO            rt_write_fld1(t, fld_UV_INT_START,   (uint8_t)( (0.10 * dbLPeriod * dbFsamp / 2.0) + 0.5 - 32 ));

		break;
	default:
		break;
	}

	if (t->connector == DEC_SVIDEO)
		rt_setcombfilter(t, standard, RT_SVIDEO);
	else
		rt_setcombfilter(t, standard, RT_COMPOSITE);

#if 0 // TODO
	/* Set the following values according to the formulas */
	rt_write_fld1(t, fld_HS_LINE_TOTAL, (uint16_t)((dbLPeriod * dbFsamp / 2.0) +0.5));
	/* According to Ivo PAL/SECAM needs different treatment */
	switch(standard & 0x00FF) {
	case DEC_PAL:
	case DEC_SECAM:
		rt_write_fld1(t, fld_MIN_PULSE_WIDTH, (uint8_t)(0.5 * dbSPPeriod * dbFsamp/2.0));
		rt_write_fld1(t, fld_MAX_PULSE_WIDTH, (uint8_t)(1.5 * dbSPPeriod * dbFsamp/2.0));
		rt_write_fld1(t, fld_WIN_OPEN_LIMIT, (uint16_t)(((dbLPeriod * dbFsamp / 4.0) + 0.5) - 16));
		rt_write_fld1(t, fld_WIN_CLOSE_LIMIT, (uint16_t)(2.39 * dbSPPeriod * dbFsamp / 2.0));
		/*    	rt_write_fld1(t, fld_VS_FIELD_IDLOCATION,   (uint16_t)RT_PAL_FIELD_IDLOCATION); */
		/*      According to docs the following value will work right, though the resulting stream deviates
			slightly from CCIR..., in particular the value that was before will do nuts to VCRs in
			pause/rewind state. */
		rt_write_fld1(t, fld_VS_FIELD_IDLOCATION,   (uint16_t)0x01);
		rt_write_fld1(t, fld_HS_PLL_SGAIN, 2);
		break;
	case DEC_NTSC:
		rt_write_fld1(t, fld_MIN_PULSE_WIDTH, (uint8_t)(0.75 * dbSPPeriod * dbFsamp/2.0));
		rt_write_fld1(t, fld_MAX_PULSE_WIDTH, (uint8_t)(1.25 * dbSPPeriod * dbFsamp/2.0));
		rt_write_fld1(t, fld_WIN_OPEN_LIMIT, (uint16_t)(((dbLPeriod * dbFsamp / 4.0) + 0.5) - 16));
		rt_write_fld1(t, fld_WIN_CLOSE_LIMIT, (uint16_t)(1.15 * dbSPPeriod * dbFsamp / 2.0));
		/*	rt_write_fld1(t, fld_VS_FIELD_IDLOCATION,   (uint16_t)fld_VS_FIELD_IDLOCATION_def);*/
		/*      I think the default value was the same as the one here.. does not hurt to hardcode it */
		rt_write_fld1(t, fld_VS_FIELD_IDLOCATION,   (uint16_t)0x01);
	    
	}

	rt_write_fld1(t, fld_VS_FRAME_TOTAL,   (uint16_t)(frame_total) + 10);
//    rt_write_fld1(t, fld_BLACK_INT_START,   (uint8_t)((0.09 * dbLPeriod * dbFsamp / 2.0) - 32 ));
//    rt_write_fld1(t, fld_SYNC_TIP_START,   (uint16_t)((dbLPeriod * dbFsamp / 2.0 + 0.5) - 28 ));
#endif
    return;

} /* RT_SetStandard ()... */

/****************************************************************************
 * RT_SetCombFilter (uint16_t wStandard, uint16_t wConnector)                       *
 *  Function: sets the input comb filter based on the standard and          *
 *            connector being used (composite vs. svideo)                   *
 *    Inputs: uint16_t wStandard - input standard (NTSC, PAL, SECAM)            *
 *            uint16_t wConnector - COMPOSITE, SVIDEO                           *
 *   Outputs: NONE                                                          *
 ****************************************************************************/
static void rt_setcombfilter(struct theatre *t, uint16_t standard, uint16_t connector)
{
	uint32_t comb_cntl_0 = 0;
	uint32_t comb_cntl_1 = 0;
	uint32_t comb_cntl_2 = 0;
	uint32_t comb_line_length = 0;

	switch (connector) {
        case RT_COMPOSITE:
                switch (standard & 0x00FF) {
		case DEC_NTSC:
                        switch (standard & 0xFF00) {
			case extNONE:
			case extNTSC:
			case extNTSC_J:
                                comb_cntl_0 = RT_NTSCM_COMB_CNTL0_COMPOSITE;
                                comb_cntl_1 = RT_NTSCM_COMB_CNTL1_COMPOSITE;
                                comb_cntl_2 = RT_NTSCM_COMB_CNTL2_COMPOSITE;
                                comb_line_length = RT_NTSCM_COMB_LENGTH_COMPOSITE;
                                break;
			case  extPAL_M:
                                comb_cntl_0 = RT_PALM_COMB_CNTL0_COMPOSITE;
                                comb_cntl_1 = RT_PALM_COMB_CNTL1_COMPOSITE;
                                comb_cntl_2 = RT_PALM_COMB_CNTL2_COMPOSITE;
                                comb_line_length = RT_PALM_COMB_LENGTH_COMPOSITE;
                                break;
                            default:
                                return;
                        }
                        break;
                    case (DEC_PAL):
                        switch (standard & 0xFF00)
                        {
                            case extNONE:
                            case extPAL:
                                comb_cntl_0 = RT_PAL_COMB_CNTL0_COMPOSITE;
                                comb_cntl_1 = RT_PAL_COMB_CNTL1_COMPOSITE;
                                comb_cntl_2 = RT_PAL_COMB_CNTL2_COMPOSITE;
                                comb_line_length = RT_PAL_COMB_LENGTH_COMPOSITE;
                                break;
                            case  (extPAL_N):
                                comb_cntl_0 = RT_PALN_COMB_CNTL0_COMPOSITE;
                                comb_cntl_1 = RT_PALN_COMB_CNTL1_COMPOSITE;
                                comb_cntl_2 = RT_PALN_COMB_CNTL2_COMPOSITE;
                                comb_line_length = RT_PALN_COMB_LENGTH_COMPOSITE;
                                break;
                            default:
                                return;
                        }
                        break;
                    case DEC_SECAM:
                        comb_cntl_0 = RT_SECAM_COMB_CNTL0_COMPOSITE;
                        comb_cntl_1 = RT_SECAM_COMB_CNTL1_COMPOSITE;
                        comb_cntl_2 = RT_SECAM_COMB_CNTL2_COMPOSITE;
                        comb_line_length = RT_SECAM_COMB_LENGTH_COMPOSITE;
                        break;
                    default:
                        return;
                }
            break;
        case RT_SVIDEO:
                switch (standard & 0x00FF) {
		case DEC_NTSC:
			switch (standard & 0xFF00) {
			case extNONE:
			case extNTSC:
				comb_cntl_0 = RT_NTSCM_COMB_CNTL0_SVIDEO;
				comb_cntl_1 = RT_NTSCM_COMB_CNTL1_SVIDEO;
				comb_cntl_2 = RT_NTSCM_COMB_CNTL2_SVIDEO;
				comb_line_length = RT_NTSCM_COMB_LENGTH_SVIDEO;
				break;
			case extPAL_M:
				comb_cntl_0 = RT_PALM_COMB_CNTL0_SVIDEO;
				comb_cntl_1 = RT_PALM_COMB_CNTL1_SVIDEO;
				comb_cntl_2 = RT_PALM_COMB_CNTL2_SVIDEO;
				comb_line_length = RT_PALM_COMB_LENGTH_SVIDEO;
				break;
			default:
                                return;
                        }
                        break;
		case DEC_PAL:
                        switch (standard & 0xFF00)
                        {
			case extNONE:
			case extPAL:
                                comb_cntl_0 = RT_PAL_COMB_CNTL0_SVIDEO;
                                comb_cntl_1 = RT_PAL_COMB_CNTL1_SVIDEO;
                                comb_cntl_2 = RT_PAL_COMB_CNTL2_SVIDEO;
                                comb_line_length = RT_PAL_COMB_LENGTH_SVIDEO;
                                break;
			case extPAL_N:
                                comb_cntl_0=   RT_PALN_COMB_CNTL0_SVIDEO;
                                comb_cntl_1=   RT_PALN_COMB_CNTL1_SVIDEO;
                                comb_cntl_2=   RT_PALN_COMB_CNTL2_SVIDEO;
                                comb_line_length=  RT_PALN_COMB_LENGTH_SVIDEO;
                                break;
			default:
                                return;
                        }
                        break;
		case DEC_SECAM:
			comb_cntl_0 = RT_SECAM_COMB_CNTL0_SVIDEO;
			comb_cntl_1 = RT_SECAM_COMB_CNTL1_SVIDEO;
			comb_cntl_2 = RT_SECAM_COMB_CNTL2_SVIDEO;
			comb_line_length = RT_SECAM_COMB_LENGTH_SVIDEO;
			break;
		default:
                        return;
                }
		break;
        default:
		return;
	}

	rt_write_fld1(t, fld_COMB_CNTL0, comb_cntl_0);
	rt_write_fld1(t, fld_COMB_CNTL1, comb_cntl_1);
	rt_write_fld1(t, fld_COMB_CNTL2, comb_cntl_2);
	rt_write_fld1(t, fld_COMB_LENGTH, comb_line_length);

	return;
}

void theatre_init(struct theatre *t)
{
	uint32_t data;

	theatre_shutdown(t);
	mdelay(10);

	t->mode=MODE_INITIALIZATION_IN_PROGRESS;
	/* 1. Set the VIN_PLL to NTSC value */
	rt_setvinclock(t, RT_NTSC);

	/* Take VINRST and L54RST out of reset */
	theatre_read(t, VIP_PLL_CNTL1, &data);
	theatre_write(t, VIP_PLL_CNTL1, data & ~((RT_VINRST_RESET << 1) | (RT_L54RST_RESET << 3)));
	theatre_read(t, VIP_PLL_CNTL1, &data);
	/* Set VIN_CLK_SEL to PLL_VIN_CLK */
	theatre_read(t, VIP_CLOCK_SEL_CNTL, &data);
	theatre_write(t, VIP_CLOCK_SEL_CNTL, data | (RT_PLL_VIN_CLK << 7));
	theatre_read(t, VIP_CLOCK_SEL_CNTL, &data);

	/* 2. Set HW_DEBUG to 0xF000 before setting the standards registers */
	theatre_write(t, VIP_HW_DEBUG, 0x0000F000);
    
	/* wait for things to settle */
	mdelay(100);
    
	rt_set_standard(t, t->standard);

	/* 3. Set DVS port to OUTPUT */
	theatre_read(t, VIP_DVS_PORT_CTRL, &data);
	theatre_write(t, VIP_DVS_PORT_CTRL, data | RT_DVSDIR_OUT);
	theatre_read(t, VIP_DVS_PORT_CTRL, &data);

	/* 4. Set default values for ADC_CNTL */
	theatre_write(t, VIP_ADC_CNTL, RT_ADC_CNTL_DEFAULT);

	/* 5. Clear the VIN_ASYNC_RST bit */
	theatre_read(t, VIP_MASTER_CNTL, &data);
	theatre_write(t, VIP_MASTER_CNTL, data & ~0x20);
	theatre_read(t, VIP_MASTER_CNTL, &data);

	/* Clear the DVS_ASYNC_RST bit */
	theatre_read(t, VIP_MASTER_CNTL, &data);
	theatre_write(t, VIP_MASTER_CNTL, data & ~(RT_DVS_ASYNC_RST));
	theatre_read(t, VIP_MASTER_CNTL, &data);

	/* Set the GENLOCK delay */
	theatre_write(t, VIP_HS_GENLOCKDELAY, 0x10);

	theatre_read(t, fld_DVS_DIRECTION, &data);
	theatre_write(t, fld_DVS_DIRECTION, data & RT_DVSDIR_OUT);

	t->mode=MODE_INITIALIZED_FOR_TV_IN;
}

void theatre_shutdown(struct theatre *t)
{
	rt_write_fld1(t, fld_VIN_ASYNC_RST, RT_ASYNC_DISABLE);
	rt_write_fld1(t, fld_VINRST       , RT_VINRST_RESET);
	rt_write_fld1(t, fld_ADC_PDWN     , RT_ADC_DISABLE);
	rt_write_fld1(t, fld_DVS_DIRECTION, RT_DVSDIR_IN);
	t->mode = MODE_UNINITIALIZED;
}
				
void theatre_reset_regs_for_no_tv_out(struct theatre *t)
{
	theatre_write(t, VIP_CLKOUT_CNTL, 0x0); 
	theatre_write(t, VIP_HCOUNT, 0x0); 
	theatre_write(t, VIP_VCOUNT, 0x0); 
	theatre_write(t, VIP_DFCOUNT, 0x0); 
#if 0
	theatre_write(t, VIP_CLOCK_SEL_CNTL, 0x2b7);  /* versus 0x237 <-> 0x2b7 */
	theatre_write(t, VIP_VIN_PLL_CNTL, 0x60a6039);
#endif
	theatre_write(t, VIP_FRAME_LOCK_CNTL, 0x0);
}

void theatre_reset_regs_for_tvout(struct theatre *t)
{
	theatre_write(t, VIP_CLKOUT_CNTL, 0x29); 

	theatre_write(t, VIP_HCOUNT, 0x1d1); 
	theatre_write(t, VIP_VCOUNT, 0x1e3); 

	theatre_write(t, VIP_DFCOUNT, 0x01); 
	theatre_write(t, VIP_CLOCK_SEL_CNTL, 0x2b7);  /* versus 0x237 <-> 0x2b7 */
	theatre_write(t, VIP_VIN_PLL_CNTL, 0x60a6039);
	theatre_write(t, VIP_FRAME_LOCK_CNTL, 0x0f);
}
