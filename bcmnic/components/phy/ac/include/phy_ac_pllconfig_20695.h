/*
 * ACPHY 20695 Radio PLL configuration
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2016,
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom.
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: phy_ac_pllconfig_20695.h 625800 2016-03-17 20:12:06Z lut $
 */

#ifndef _PHY_AC_20695_PLLCONFIG_H
#define _PHY_AC_20695_PLLCONFIG_H

/* Fixed point constant defines */
/* 2^-13 in 1.15.16 */
#define FIX_2POW_MIN13					0x8
/* 2.0 in 1.15.16 */
#define RFPLL_VCOCAL_DELAYEND_FX			0x20000
/* 10.0 in 1.15.16 */
#define RFPLL_VCOCAL_DELAYSTARTCOLD_FX			0xA0000
/* 0.5 in 1.15.16 */
#define RFPLL_VCOCAL_DELAYSTARTWARM_FX			0x8000
/* 1.0 in 1.15.16 */
#define RFPLL_VCOCAL_PAUSECNT_FX			0x10000
/* 2.33 in 1.15.16 */
#define T_OFF_FX					0x2547B
/* 1.0 in 1.15.16 */
#define FX_ONE						0x10000
/* ((1000 * 2 * pi) / f3db_ovr_fn) in 1.23.8 */
#define WN_CONST_FX					0x9E31A
/* 800 in 1.15.16 */
#define CONST_800_FX					0x3200000
/* 400 in 1.15.16 */
#define CONST_400_FX					0x1900000
/* 3200 in 1.15.16 */
#define CONST_3200_FX					0xC800000
/* 1600 in 1.15.16 */
#define CONST_1600_FX					0x6400000
/* 5.4 in 1.15.16 */
#define CONST_5P4_FX					0x56666
/* 0.9 in 1.15.16 */
#define CONST_P9_FX					0xE666
/* 3.4 in 1.15.16 */
#define CONST_3P4_FX					0x36666
/* 0.6 in 1.15.16 */
#define CONST_P6_FX					0x999A
/* (CAP_MULTIPLIER_RATIO_PLUS_ONE / CAP_MULTIPLIER_RATIO) in 1.15.16 */
#define RF_CONST_FX					0x1199A

/* Band dependent constants */
#define VCO_CAL_CAP_BITS_5G				12
#define VCO_CAL_CAP_BITS_2G				9
/* damp = 1 */
/* f3db_ovr_fn = sqrt(1 + 2 * damp^2 + sqrt(2 + 4 * damp^2 + 4 * damp^4)); */
/* f3db_ovr_fn = 2.4823935345082 */
/* pi = 3.14159265358979 */
/* ((icp * kvco * f3db_ovr_fn^2) / (4 * pi^2)) * 1e9 in 1.32.16 */
#define C1_PASSIVE_LF_CONST_FX_5G			0x323D9C4DB8E95ULL
#define C1_PASSIVE_LF_CONST_FX_2G			0xDF4AB6AEC40DULL
/* (2 * damp) / (icp * kvco) in 0.0.32 */
#define R1_PASSIVE_LF_CONST_FX_5G			0x5ED097B
#define R1_PASSIVE_LF_CONST_FX_2G			0x15555555
/* (R_CONSTANT * damp) / (icp * kvco) in 1.0.32 */
#define RFPLL_LF_LF_R2_REF_CONST_FX_5G			0x1F9ADD4
#define RFPLL_LF_LF_R2_REF_CONST_FX_2G			0x71C71C7
#define KVCO_CODE_FX_5G					0x8
#define KVCO_CODE_FX_2G					0xb
#define TEMP_CODE_FX_5G					0x8
#define TEMP_CODE_FX_2G					0xb
/* 0.9 in 1.15.16 */
#define ICP_FX_5G					0xE666
/* 0.4 in 1.15.16 */
#define ICP_FX_2G					0x6666
/* (icp * 125) in 1.32.0 */
/* For 5G it is hardcoded to 0xFF */
#define RFPLL_CP_KPD_SCALE_DEC_FX_5G			0xFF
#define RFPLL_CP_KPD_SCALE_DEC_FX_2G			0x32

/* Constants */
#define VCO_CAL_AUX_CAP_BITS				7
#define UP_CONVERSION_RATIO				2
#define RFPLL_VCOCAL_TARGETCOUNTBASE			0
#define RFPLL_VCOCAL_OFFSETIN				0
#define RFPLL_VCOCAL_UPDATESEL_DEC			1
#define RFPLL_VCOCAL_CALCAPRBMODE_DEC			3
#define RFPLL_VCOCAL_TESTVCOCNT_DEC			0
#define RFPLL_VCOCAL_FORCE_CAPS_OVR_DEC			0
#define RFPLL_VCOCAL_FORCE_CAPS_OVRVAL_DEC		0
#define RFPLL_VCOCAL_FAST_SETTLE_OVR_DEC		0
#define RFPLL_VCOCAL_FAST_SETTLE_OVRVAL_DEC		0
#define RFPLL_VCOCAL_FORCE_VCTRL_OVR_DEC		0
#define RFPLL_VCOCAL_FORCE_VCTRL_OVRVAL_DEC		0
#define RFPLL_VCOCAL_ENABLECOUPLING_DEC			0
#define RFPLL_VCOCAL_COUPLINGMODE_DEC			1
#define RFPLL_VCOCAL_SWAPVCO12_DEC			0
#define RFPLL_VCOCAL_COUPLTHRES_DEC			6
#define RFPLL_VCOCAL_COUPLTHRES2_DEC			12
#define RFPLL_VCOCAL_SECONDMESEN_DEC			3
#define RFPLL_VCOCAL_UPDATESELCOUP_DEC			1
#define RFPLL_VCOCAL_COUPLINGIN_DEC			0
#define RFPLL_VCOCAL_FORCE_CAPS2_OVR_DEC		0
#define RFPLL_VCOCAL_FORCE_CAPS2_OVRVAL_DEC		0
#define RFPLL_VCOCAL_FORCE_AUX1_OVR_DEC			0
#define RFPLL_VCOCAL_FORCE_AUX1_OVRVAL_DEC		0
#define RFPLL_VCOCAL_FORCE_AUX2_OVR_DEC			0
#define RFPLL_VCOCAL_FORCE_AUX2_OVRVAL_DEC		0
#define C_CONSTANT					30
#define CAP_MULTIPLIER_RATIO				10
#define CAP_MULTIPLIER_RATIO_PLUS_ONE			11

#define LOOP_BW_5G					550
#define LOOP_BW_2G					550
/* zero will take default value */
#define LOOP_BW						0
#define USE_DOUBLER					1
#define LOGEN_MODE					0

#define USE_5G_PLL_FOR_2G				0

/* No of fraction bits */
#define NF0	0
#define NF6	6
#define NF8	8
#define NF16	16
#define NF20	20
#define NF24	24
#define NF32	32

/* WAR Related constants defines */
#define RFPLL_LF_LF_C1_CH2412		0x1f
#define RFPLL_LF_LF_C2_CH2412		0x1f
#define RFPLL_LF_LF_C3_CH2412		0x1f
#define RFPLL_LF_LF_C4_CH2412		0x1f
#define RFPLL_LF_LF_R2_CH2412		0x1
#define RFPLL_LF_LF_R3_CH2412		0x1
#define RFPLL_LF_LF_RF_CM_CH2412	0x9
#define RFPLL_LF_LF_RS_CM_CH2412	0x1a

#ifdef BCMDBG_PLL
#define PRINT_PLL_CONFIG(struct_name, var_name, mask) \
		printf("%s = 0x%x\n", #var_name, (struct_name->var_name & mask))
#define PRINT_VAR_VALUE(var_name) printf("%s = %d\n", #var_name, var_name)

#define PRINT_PLL_CONFIG_FLAG 0
#endif

#define PLL_CONFIG_20695_VAL_ENTRY(pll_struct, offset, val) \
	pll_struct->reg_field_val[IDX_20695_##offset] = val

#define PLL_CONFIG_20695_REG_INFO_ENTRY(pi, pll_str, offset, regpfx, reg2g, reg5g, fld2g, fld5g) \
	pll_str->reg_addr_2g[IDX_20695_##offset] = RADIO_REG_20695(pi, RFP, reg2g, 0); \
	pll_str->reg_addr_5g[IDX_20695_##offset] = RADIO_REG_20695(pi, RFP, reg5g, 0); \
	pll_str->reg_field_mask_2g[IDX_20695_##offset] = \
			RF_##20695##_##reg2g##_##fld2g##_MASK(pi->pubpi->radiorev); \
	pll_str->reg_field_mask_5g[IDX_20695_##offset] = \
			RF_##20695##_##reg5g##_##fld5g##_MASK(pi->pubpi->radiorev); \
	pll_str->reg_field_shift_2g[IDX_20695_##offset] = \
			RF_##20695##_##reg2g##_##fld2g##_SHIFT(pi->pubpi->radiorev); \
	pll_str->reg_field_shift_5g[IDX_20695_##offset] = \
			RF_##20695##_##reg5g##_##fld5g##_SHIFT(pi->pubpi->radiorev) \

/* structure to hold computed PLL config values */
typedef struct {
	uint32 xtal_fx;
	uint32 loop_bw;
	uint8 use_doubler;
	uint8 logen_mode;
	uint16 *reg_addr_2g;
	uint16 *reg_addr_5g;
	uint16 *reg_field_mask_2g;
	uint16 *reg_field_mask_5g;
	uint8 *reg_field_shift_2g;
	uint8 *reg_field_shift_5g;
	uint16 *reg_field_val;
} pll_config_20695_tbl_t;

typedef enum {
	IDX_20695_OVR_RFPLL_VCOCAL_OFFSETIN,
	IDX_20695_OVR_RFPLL_VCOCAL_COUPLINGIN,
	IDX_20695_OVR_MUX_XT_DEL,
	IDX_20695_OVR_MUX_SEL_BAND,
	IDX_20695_XTALCNT_2G,
	IDX_20695_XTALCNT_5G,
	IDX_20695_RFPLL_VCOCAL_DELAYEND,
	IDX_20695_RFPLL_VCOCAL_DELAYSTARTCOLD,
	IDX_20695_RFPLL_VCOCAL_DELAYSTARTWARM,
	IDX_20695_RFPLL_VCOCAL_XTALCOUNT,
	IDX_20695_RFPLL_VCOCAL_CALCAPRBMODE,
	IDX_20695_RFPLL_VCOCAL_DELTAPLLVAL,
	IDX_20695_RFPLL_VCOCAL_ERRORTHRES,
	IDX_20695_RFPLL_VCOCAL_INITCAPA,
	IDX_20695_RFPLL_VCOCAL_INITCAPB,
	IDX_20695_RFPLL_VCOCAL_NORMCOUNTLEFT,
	IDX_20695_RFPLL_VCOCAL_NORMCOUNTRIGHT,
	IDX_20695_RFPLL_VCOCAL_PAUSECNT,
	IDX_20695_RFPLL_VCOCAL_ROUNDLSB,
	IDX_20695_RFPLL_VCOCAL_UPDATESEL,
	IDX_20695_RFPLL_VCOCAL_TESTVCOCNT,
	IDX_20695_RFPLL_VCOCAL_FAST_SETTLE_OVR,
	IDX_20695_RFPLL_VCOCAL_FAST_SETTLE_OVRVAL,
	IDX_20695_RFPLL_VCOCAL_FORCE_VCTRL_OVR,
	IDX_20695_RFPLL_VCOCAL_FORCE_VCTRL_OVRVAL,
	IDX_20695_RFPLL_VCOCAL_TARGETCOUNTBASE,
	IDX_20695_RFPLL_VCOCAL_TARGETCOUNTCENTER,
	IDX_20695_RFPLL_VCOCAL_OFFSETIN,
	IDX_20695_RFPLL_VCOCAL_PLL_VAL,
	IDX_20695_RFPLL_VCOCAL_FORCE_CAPS_OVR,
	IDX_20695_RFPLL_VCOCAL_FORCE_CAPS_OVRVAL,
	IDX_20695_RFPLL_FRCT_WILD_BASE_HIGH,
	IDX_20695_RFPLL_FRCT_WILD_BASE_LOW,
	IDX_20695_RFPLL_VCOCAL_ENABLECOUPLING,
	IDX_20695_RFPLL_VCOCAL_COUPLINGMODE,
	IDX_20695_RFPLL_VCOCAL_SWAPVCO12,
	IDX_20695_RFPLL_VCOCAL_MIDCODESEL,
	IDX_20695_RFPLL_VCOCAL_SECONDMESEN,
	IDX_20695_RFPLL_VCOCAL_UPDATESELCOUP,
	IDX_20695_RFPLL_VCOCAL_COUPLTHRES,
	IDX_20695_RFPLL_VCOCAL_COUPLTHRES2,
	IDX_20695_RFPLL_VCOCAL_COUPLINGIN,
	IDX_20695_RFPLL_VCOCAL_FORCE_CAPS2_OVR,
	IDX_20695_RFPLL_VCOCAL_FORCE_CAPS2_OVRVAL,
	IDX_20695_RFPLL_VCOCAL_FORCE_AUX1_OVR,
	IDX_20695_RFPLL_VCOCAL_FORCE_AUX1_OVRVAL,
	IDX_20695_RFPLL_VCOCAL_FORCE_AUX2_OVR,
	IDX_20695_RFPLL_VCOCAL_FORCE_AUX2_OVRVAL,
	IDX_20695_RFPLL_LF_LF_R2,
	IDX_20695_RFPLL_LF_LF_R3,
	IDX_20695_RFPLL_LF_LF_RS_CM,
	IDX_20695_RFPLL_LF_LF_RF_CM,
	IDX_20695_RFPLL_LF_LF_C1,
	IDX_20695_RFPLL_LF_LF_C2,
	IDX_20695_RFPLL_LF_LF_C3,
	IDX_20695_RFPLL_LF_LF_C4,
	IDX_20695_RFPLL_CP_IOFF,
	IDX_20695_RFPLL_CP_KPD_SCALE,
	IDX_20695_RFPLL_RFVCO_KVCO_CODE,
	IDX_20695_RFPLL_RFVCO_TEMP_CODE,
	IDX_20695_OVR_RFPLL_RST_N,
	IDX_20695_RFPLL_RST_N_1,
	IDX_20695_RFPLL_RST_N_2,
	PLL_CONFIG_20695_ARRAY_SIZE
} pll_config_20695_offset_t;

#endif /* _PHY_AC_20695_PLLCONFIG_H */
