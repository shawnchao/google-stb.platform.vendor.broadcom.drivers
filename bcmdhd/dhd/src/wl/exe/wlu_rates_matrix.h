/*
 * This module does provides various mappings to or from the CLM rate indexes.
 *
 * Copyright (C) 2018, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 */
/*FILE-CSTYLED*/

#ifndef _WLU_RATES_MATRIX_H_
#define _WLU_RATES_MATRIX_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <bcmwifi_rates.h>
#include <wlioctl.h>
#include <typedefs.h>
#include <epivers.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmwifi_channels.h>
#include <wlc_ppr.h>

#include <inttypes.h>
#include <miniopt.h>

#define WL_UNSUPPORTED_IDX 0xFFF

#define WLC_MAXMCS	102	/* Maximum valid mcs index */

#define IS_PROPRIETARY_11N_MCS(mcs) \
	((mcs) == 87 || (mcs) == 88 || (mcs) == 99 || (mcs) == 100 || (mcs) == 101 || (mcs) == 102)

typedef enum clm_rate_group_id {
	RATE_GROUP_ID_DSSS = 0,
	RATE_GROUP_ID_OFDM,
	RATE_GROUP_ID_MCS0_7,
	RATE_GROUP_ID_VHT8_9SS1,
	RATE_GROUP_ID_VHT10_11SS1,
	RATE_GROUP_ID_DSSS_MULTI1,
	RATE_GROUP_ID_OFDM_CDD1,
	RATE_GROUP_ID_MCS0_7_CDD1,
	RATE_GROUP_ID_VHT8_9SS1_CDD1,
	RATE_GROUP_ID_VHT10_11SS1_CDD1,
	RATE_GROUP_ID_MCS0_7_STBC,
	RATE_GROUP_ID_VHT8_9SS1_STBC,
	RATE_GROUP_ID_VHT10_11SS1_STBC,
	RATE_GROUP_ID_MCS8_15,
	RATE_GROUP_ID_VHT8_9SS2,
	RATE_GROUP_ID_VHT10_11SS2,
	RATE_GROUP_ID_DSSS_MULTI2,
	RATE_GROUP_ID_OFDM_CDD2,
	RATE_GROUP_ID_MCS0_7_CDD2,
	RATE_GROUP_ID_VHT8_9SS1_CDD2,
	RATE_GROUP_ID_VHT10_11SS1_CDD2,
	RATE_GROUP_ID_MCS0_7_STBC_SPEXP1,
	RATE_GROUP_ID_VHT8_9SS1_STBC_SPEXP1,
	RATE_GROUP_ID_VHT10_11SS1_STBC_SPEXP1,
	RATE_GROUP_ID_MCS8_15_SPEXP1,
	RATE_GROUP_ID_VHT8_9SS2_SPEXP1,
	RATE_GROUP_ID_VHT10_11SS2_SPEXP1,
	RATE_GROUP_ID_MCS16_23,
	RATE_GROUP_ID_VHT8_9SS3,
	RATE_GROUP_ID_VHT10_11SS3,
	RATE_GROUP_ID_DSSS_MULTI3,
	RATE_GROUP_ID_OFDM_CDD3,
	RATE_GROUP_ID_MCS0_7_CDD3,
	RATE_GROUP_ID_VHT8_9SS1_CDD3,
	RATE_GROUP_ID_VHT10_11SS1_CDD3,
	RATE_GROUP_ID_MCS0_7_STBC_SPEXP2,
	RATE_GROUP_ID_VHT8_9SS1_STBC_SPEXP2,
	RATE_GROUP_ID_VHT10_11SS1_STBC_SPEXP2,
	RATE_GROUP_ID_MCS8_15_SPEXP2,
	RATE_GROUP_ID_VHT8_9SS2_SPEXP2,
	RATE_GROUP_ID_VHT10_11SS2_SPEXP2,
	RATE_GROUP_ID_MCS16_23_SPEXP1,
	RATE_GROUP_ID_VHT8_9SS3_SPEXP1,
	RATE_GROUP_ID_VHT10_11SS3_SPEXP1,
	RATE_GROUP_ID_MCS24_31,
	RATE_GROUP_ID_VHT8_9SS4,
	RATE_GROUP_ID_VHT10_11SS4,
	RATE_GROUP_ID_OFDM_TXBF1,
	RATE_GROUP_ID_MCS0_7_TXBF1,
	RATE_GROUP_ID_VHT8_9SS1_TXBF1,
	RATE_GROUP_ID_VHT10_11SS1_TXBF1,
	RATE_GROUP_ID_MCS8_15_TXBF0,
	RATE_GROUP_ID_VHT8_9SS2_TXBF0,
	RATE_GROUP_ID_VHT10_11SS2_TXBF0,
	RATE_GROUP_ID_OFDM_TXBF2,
	RATE_GROUP_ID_MCS0_7_TXBF2,
	RATE_GROUP_ID_VHT8_9SS1_TXBF2,
	RATE_GROUP_ID_VHT10_11SS1_TXBF2,
	RATE_GROUP_ID_MCS8_15_TXBF1,
	RATE_GROUP_ID_VHT8_9SS2_TXBF1,
	RATE_GROUP_ID_VHT10_11SS2_TXBF1,
	RATE_GROUP_ID_MCS16_23_TXBF0,
	RATE_GROUP_ID_VHT8_9SS3_TXBF0,
	RATE_GROUP_ID_VHT10_11SS3_TXBF0,
	RATE_GROUP_ID_OFDM_TXBF3,
	RATE_GROUP_ID_MCS0_7_TXBF3,
	RATE_GROUP_ID_VHT8_9SS1_TXBF3,
	RATE_GROUP_ID_VHT10_11SS1_TXBF3,
	RATE_GROUP_ID_MCS8_15_TXBF2,
	RATE_GROUP_ID_VHT8_9SS2_TXBF2,
	RATE_GROUP_ID_VHT10_11SS2_TXBF2,
	RATE_GROUP_ID_MCS16_23_TXBF1,
	RATE_GROUP_ID_VHT8_9SS3_TXBF1,
	RATE_GROUP_ID_VHT10_11SS3_TXBF1,
	RATE_GROUP_ID_MCS24_31_TXBF0,
	RATE_GROUP_ID_VHT8_9SS4_TXBF0,
	RATE_GROUP_ID_VHT10_11SS4_TXBF0,
	RATE_GROUP_ID_COUNT
} clm_rate_group_id_t;

typedef enum reg_rate_index {
	NO_RATE = -1,
	DSSS1, DSSS2, DSSS5, DSSS11,
	OFDM6, OFDM9, OFDM12, OFDM18,
	OFDM24, OFDM36, OFDM48, OFDM54,
	MCS0, MCS1, MCS2, MCS3,
	MCS4, MCS5, MCS6, MCS7,
	VHT8SS1, VHT9SS1,VHT10SS1, VHT11SS1,
	DSSS1_MULTI1, DSSS2_MULTI1, DSSS5_MULTI1, DSSS11_MULTI1,
	OFDM6_CDD1, OFDM9_CDD1, OFDM12_CDD1, OFDM18_CDD1,
	OFDM24_CDD1, OFDM36_CDD1, OFDM48_CDD1, OFDM54_CDD1,
	MCS0_CDD1, MCS1_CDD1, MCS2_CDD1, MCS3_CDD1,
	MCS4_CDD1, MCS5_CDD1, MCS6_CDD1, MCS7_CDD1,
	VHT8SS1_CDD1, VHT9SS1_CDD1, VHT10SS1_CDD1, VHT11SS1_CDD1,
	MCS0_STBC, MCS1_STBC, MCS2_STBC, MCS3_STBC,
	MCS4_STBC, MCS5_STBC, MCS6_STBC, MCS7_STBC,
	VHT8SS1_STBC, VHT9SS1_STBC, VHT10SS1_STBC, VHT11SS1_STBC,
	MCS8, MCS9, MCS10, MCS11,
	MCS12, MCS13, MCS14, MCS15,
	VHT8SS2, VHT9SS2, VHT10SS2, VHT11SS2,
	DSSS1_MULTI2, DSSS2_MULTI2, DSSS5_MULTI2, DSSS11_MULTI2,
	OFDM6_CDD2, OFDM9_CDD2, OFDM12_CDD2, OFDM18_CDD2,
	OFDM24_CDD2, OFDM36_CDD2, OFDM48_CDD2, OFDM54_CDD2,
	MCS0_CDD2, MCS1_CDD2, MCS2_CDD2, MCS3_CDD2,
	MCS4_CDD2, MCS5_CDD2, MCS6_CDD2, MCS7_CDD2,
	VHT8SS1_CDD2, VHT9SS1_CDD2, VHT10SS1_CDD2, VHT11SS1_CDD2,
	MCS0_STBC_SPEXP1, MCS1_STBC_SPEXP1, MCS2_STBC_SPEXP1, MCS3_STBC_SPEXP1,
	MCS4_STBC_SPEXP1, MCS5_STBC_SPEXP1, MCS6_STBC_SPEXP1, MCS7_STBC_SPEXP1,
	VHT8SS1_STBC_SPEXP1, VHT9SS1_STBC_SPEXP1, VHT10SS1_STBC_SPEXP1, VHT11SS1_STBC_SPEXP1,
	MCS8_SPEXP1, MCS9_SPEXP1, MCS10_SPEXP1, MCS11_SPEXP1,
	MCS12_SPEXP1, MCS13_SPEXP1, MCS14_SPEXP1, MCS15_SPEXP1,
	VHT8SS2_SPEXP1, VHT9SS2_SPEXP1, VHT10SS2_SPEXP1, VHT11SS2_SPEXP1,
	MCS16, MCS17, MCS18, MCS19,
	MCS20, MCS21, MCS22, MCS23,
	VHT8SS3, VHT9SS3, VHT10SS3, VHT11SS3,
	DSSS1_MULTI3, DSSS2_MULTI3, DSSS5_MULTI3, DSSS11_MULTI3,
	OFDM6_CDD3, OFDM9_CDD3, OFDM12_CDD3, OFDM18_CDD3,
	OFDM24_CDD3, OFDM36_CDD3, OFDM48_CDD3, OFDM54_CDD3,
	MCS0_CDD3, MCS1_CDD3, MCS2_CDD3, MCS3_CDD3,
	MCS4_CDD3, MCS5_CDD3, MCS6_CDD3, MCS7_CDD3,
	VHT8SS1_CDD3, VHT9SS1_CDD3, VHT10SS1_CDD3, VHT11SS1_CDD3,
	MCS0_STBC_SPEXP2, MCS1_STBC_SPEXP2, MCS2_STBC_SPEXP2, MCS3_STBC_SPEXP2,
	MCS4_STBC_SPEXP2, MCS5_STBC_SPEXP2, MCS6_STBC_SPEXP2, MCS7_STBC_SPEXP2,
	VHT8SS1_STBC_SPEXP2, VHT9SS1_STBC_SPEXP2, VHT10SS1_STBC_SPEXP2, VHT11SS1_STBC_SPEXP2,
	MCS8_SPEXP2, MCS9_SPEXP2, MCS10_SPEXP2, MCS11_SPEXP2,
	MCS12_SPEXP2, MCS13_SPEXP2, MCS14_SPEXP2, MCS15_SPEXP2,
	VHT8SS2_SPEXP2, VHT9SS2_SPEXP2, VHT10SS2_SPEXP2, VHT11SS2_SPEXP2,
	MCS16_SPEXP1, MCS17_SPEXP1, MCS18_SPEXP1, MCS19_SPEXP1,
	MCS20_SPEXP1, MCS21_SPEXP1, MCS22_SPEXP1, MCS23_SPEXP1,
	VHT8SS3_SPEXP1, VHT9SS3_SPEXP1, VHT10SS3_SPEXP1, VHT11SS3_SPEXP1,
	MCS24, MCS25, MCS26, MCS27,
	MCS28, MCS29, MCS30, MCS31,
	VHT8SS4, VHT9SS4, VHT10SS4, VHT11SS4,
	OFDM6_TXBF1, OFDM9_TXBF1, OFDM12_TXBF1, OFDM18_TXBF1,
	OFDM24_TXBF1, OFDM36_TXBF1, OFDM48_TXBF1, OFDM54_TXBF1,
	MCS0_TXBF1, MCS1_TXBF1, MCS2_TXBF1, MCS3_TXBF1,
	MCS4_TXBF1, MCS5_TXBF1, MCS6_TXBF1, MCS7_TXBF1,
	VHT8SS1_TXBF1, VHT9SS1_TXBF1, VHT10SS1_TXBF1, VHT11SS1_TXBF1,
	MCS8_TXBF0, MCS9_TXBF0, MCS10_TXBF0, MCS11_TXBF0,
	MCS12_TXBF0, MCS13_TXBF0, MCS14_TXBF0, MCS15_TXBF0,
	VHT8SS2_TXBF0, VHT9SS2_TXBF0, VHT10SS2_TXBF0, VHT11SS2_TXBF0,
	OFDM6_TXBF2, OFDM9_TXBF2, OFDM12_TXBF2, OFDM18_TXBF2,
	OFDM24_TXBF2, OFDM36_TXBF2, OFDM48_TXBF2, OFDM54_TXBF2,
	MCS0_TXBF2, MCS1_TXBF2, MCS2_TXBF2, MCS3_TXBF2,
	MCS4_TXBF2, MCS5_TXBF2, MCS6_TXBF2, MCS7_TXBF2,
	VHT8SS1_TXBF2, VHT9SS1_TXBF2, VHT10SS1_TXBF2, VHT11SS1_TXBF2,
	MCS8_TXBF1, MCS9_TXBF1, MCS10_TXBF1, MCS11_TXBF1,
	MCS12_TXBF1, MCS13_TXBF1, MCS14_TXBF1, MCS15_TXBF1,
	VHT8SS2_TXBF1, VHT9SS2_TXBF1, VHT10SS2_TXBF1, VHT11SS2_TXBF1,
	MCS16_TXBF0, MCS17_TXBF0, MCS18_TXBF0, MCS19_TXBF0,
	MCS20_TXBF0, MCS21_TXBF0, MCS22_TXBF0, MCS23_TXBF0,
	VHT8SS3_TXBF0, VHT9SS3_TXBF0, VHT10SS3_TXBF0, VHT11SS3_TXBF0,
	OFDM6_TXBF3, OFDM9_TXBF3, OFDM12_TXBF3, OFDM18_TXBF3,
	OFDM24_TXBF3, OFDM36_TXBF3, OFDM48_TXBF3, OFDM54_TXBF3,
	MCS0_TXBF3, MCS1_TXBF3, MCS2_TXBF3, MCS3_TXBF3,
	MCS4_TXBF3, MCS5_TXBF3, MCS6_TXBF3, MCS7_TXBF3,
	VHT8SS1_TXBF3, VHT9SS1_TXBF3, VHT10SS1_TXBF3, VHT11SS1_TXBF3,
	MCS8_TXBF2, MCS9_TXBF2, MCS10_TXBF2, MCS11_TXBF2,
	MCS12_TXBF2, MCS13_TXBF2, MCS14_TXBF2, MCS15_TXBF2,
	VHT8SS2_TXBF2, VHT9SS2_TXBF2, VHT10SS2_TXBF2, VHT11SS2_TXBF2,
	MCS16_TXBF1, MCS17_TXBF1, MCS18_TXBF1, MCS19_TXBF1,
	MCS20_TXBF1, MCS21_TXBF1, MCS22_TXBF1, MCS23_TXBF1,
	VHT8SS3_TXBF1, VHT9SS3_TXBF1, VHT10SS3_TXBF1, VHT11SS3_TXBF1,
	MCS24_TXBF0, MCS25_TXBF0, MCS26_TXBF0, MCS27_TXBF0,
	MCS28_TXBF0, MCS29_TXBF0, MCS30_TXBF0, MCS31_TXBF0,
	VHT8SS4_TXBF0, VHT9SS4_TXBF0, VHT10SS4_TXBF0, VHT11SS4_TXBF0,
} reg_rate_index_t;

typedef enum ppr_rate_type {
	PPR_RATE_DSSS,
	PPR_RATE_OFDM,
	PPR_RATE_HT,
	PPR_RATE_VHT,
} ppr_rate_type_t;

typedef struct ppr_tbl {
	char label[23];
	clm_rate_group_id_t id;
//	clm_rates_t         rate;
} ppr_tbl_t;

typedef struct ppr_group {
	clm_rate_group_id_t	id;
	wl_tx_chains_t		chain;
	wl_tx_mode_t		mode;
	wl_tx_nss_t		nss;
	ppr_rate_type_t		rate_type;
	reg_rate_index_t	first_rate;
} ppr_group_t;

static const ppr_group_t ppr_group_table[] = {
	/*group id				chains			mode			nss		rate type		offset-first*/
	{RATE_GROUP_ID_DSSS,			WL_TX_CHAINS_1,		WL_TX_MODE_NONE,	WL_TX_NSS_1,	PPR_RATE_DSSS,		DSSS1},
	{RATE_GROUP_ID_OFDM,			WL_TX_CHAINS_1,		WL_TX_MODE_NONE,	WL_TX_NSS_1,	PPR_RATE_OFDM,		OFDM6},
	{RATE_GROUP_ID_MCS0_7,			WL_TX_CHAINS_1,		WL_TX_MODE_NONE,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0},
	{RATE_GROUP_ID_VHT8_9SS1,		WL_TX_CHAINS_1,		WL_TX_MODE_NONE,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0},
	{RATE_GROUP_ID_VHT10_11SS1,		WL_TX_CHAINS_1,		WL_TX_MODE_NONE,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0},
	{RATE_GROUP_ID_DSSS_MULTI1,		WL_TX_CHAINS_2,		WL_TX_MODE_NONE,	WL_TX_NSS_1,	PPR_RATE_DSSS,		DSSS1_MULTI1},
	{RATE_GROUP_ID_OFDM_CDD1,		WL_TX_CHAINS_2,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_OFDM,		OFDM6_CDD1},
	{RATE_GROUP_ID_MCS0_7_CDD1,		WL_TX_CHAINS_2,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_CDD1},
	{RATE_GROUP_ID_VHT8_9SS1_CDD1,		WL_TX_CHAINS_2,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_CDD1},
	{RATE_GROUP_ID_VHT10_11SS1_CDD1,	WL_TX_CHAINS_2,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_CDD1},
	{RATE_GROUP_ID_MCS0_7_STBC,		WL_TX_CHAINS_2,		WL_TX_MODE_STBC,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS0_STBC},
	{RATE_GROUP_ID_VHT8_9SS1_STBC,		WL_TX_CHAINS_2,		WL_TX_MODE_STBC,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS0_STBC},
	{RATE_GROUP_ID_VHT10_11SS1_STBC,	WL_TX_CHAINS_2,		WL_TX_MODE_STBC,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS0_STBC},
	{RATE_GROUP_ID_MCS8_15,			WL_TX_CHAINS_2,		WL_TX_MODE_NONE,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8},
	{RATE_GROUP_ID_VHT8_9SS2,		WL_TX_CHAINS_2,		WL_TX_MODE_NONE,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8},
	{RATE_GROUP_ID_VHT10_11SS2,		WL_TX_CHAINS_2,		WL_TX_MODE_NONE,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8},
	{RATE_GROUP_ID_DSSS_MULTI2,		WL_TX_CHAINS_3,		WL_TX_MODE_NONE,	WL_TX_NSS_1,	PPR_RATE_DSSS,		DSSS1_MULTI2},
	{RATE_GROUP_ID_OFDM_CDD2,		WL_TX_CHAINS_3,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_OFDM,		OFDM6_CDD2},
	{RATE_GROUP_ID_MCS0_7_CDD2,		WL_TX_CHAINS_3,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_CDD2},
	{RATE_GROUP_ID_VHT8_9SS1_CDD2,		WL_TX_CHAINS_3,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_CDD2},
	{RATE_GROUP_ID_VHT10_11SS1_CDD2,	WL_TX_CHAINS_3,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_CDD2},
	{RATE_GROUP_ID_MCS0_7_STBC_SPEXP1,	WL_TX_CHAINS_3,		WL_TX_MODE_STBC,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS0_STBC_SPEXP1},
	{RATE_GROUP_ID_VHT8_9SS1_STBC_SPEXP1,	WL_TX_CHAINS_3,		WL_TX_MODE_STBC,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS0_STBC_SPEXP1},
	{RATE_GROUP_ID_VHT10_11SS1_STBC_SPEXP1,	WL_TX_CHAINS_3,		WL_TX_MODE_STBC,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS0_STBC_SPEXP1},
	{RATE_GROUP_ID_MCS8_15_SPEXP1,		WL_TX_CHAINS_3,		WL_TX_MODE_NONE,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_SPEXP1},
	{RATE_GROUP_ID_VHT8_9SS2_SPEXP1,	WL_TX_CHAINS_3,		WL_TX_MODE_NONE,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_SPEXP1},
	{RATE_GROUP_ID_VHT10_11SS2_SPEXP1,	WL_TX_CHAINS_3,		WL_TX_MODE_NONE,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_SPEXP1},
	{RATE_GROUP_ID_MCS16_23,		WL_TX_CHAINS_3,		WL_TX_MODE_NONE,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16},
	{RATE_GROUP_ID_VHT8_9SS3,		WL_TX_CHAINS_3,		WL_TX_MODE_NONE,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16},
	{RATE_GROUP_ID_VHT10_11SS3,		WL_TX_CHAINS_3,		WL_TX_MODE_NONE,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16},
	{RATE_GROUP_ID_DSSS_MULTI3,		WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_1,	PPR_RATE_DSSS,		DSSS1_MULTI3},
	{RATE_GROUP_ID_OFDM_CDD3,		WL_TX_CHAINS_4,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_OFDM,		OFDM6_CDD3},
	{RATE_GROUP_ID_MCS0_7_CDD3,		WL_TX_CHAINS_4,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_CDD3},
	{RATE_GROUP_ID_VHT8_9SS1_CDD3,		WL_TX_CHAINS_4,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_CDD3},
	{RATE_GROUP_ID_VHT10_11SS1_CDD3,	WL_TX_CHAINS_4,		WL_TX_MODE_CDD,		WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_CDD3},
	{RATE_GROUP_ID_MCS0_7_STBC_SPEXP2,	WL_TX_CHAINS_4,		WL_TX_MODE_STBC,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS0_STBC_SPEXP2},
	{RATE_GROUP_ID_VHT8_9SS1_STBC_SPEXP2,	WL_TX_CHAINS_4,		WL_TX_MODE_STBC,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS0_STBC_SPEXP2},
	{RATE_GROUP_ID_VHT10_11SS1_STBC_SPEXP2,	WL_TX_CHAINS_4,		WL_TX_MODE_STBC,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS0_STBC_SPEXP2},
	{RATE_GROUP_ID_MCS8_15_SPEXP2,		WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_SPEXP2},
	{RATE_GROUP_ID_VHT8_9SS2_SPEXP2,	WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_SPEXP2},
	{RATE_GROUP_ID_VHT10_11SS2_SPEXP2,	WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_SPEXP2},
	{RATE_GROUP_ID_MCS16_23_SPEXP1,		WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16_SPEXP1},
	{RATE_GROUP_ID_VHT8_9SS3_SPEXP1,	WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16_SPEXP1},
	{RATE_GROUP_ID_VHT10_11SS3_SPEXP1,	WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16_SPEXP1},
	{RATE_GROUP_ID_MCS24_31,		WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_4,	PPR_RATE_VHT,		MCS24},
	{RATE_GROUP_ID_VHT8_9SS4,		WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_4,	PPR_RATE_VHT,		MCS24},
	{RATE_GROUP_ID_VHT10_11SS4,		WL_TX_CHAINS_4,		WL_TX_MODE_NONE,	WL_TX_NSS_4,	PPR_RATE_VHT,		MCS24},
	{RATE_GROUP_ID_OFDM_TXBF1,		WL_TX_CHAINS_2,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_OFDM,		OFDM6_TXBF1},
	{RATE_GROUP_ID_MCS0_7_TXBF1,		WL_TX_CHAINS_2,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_TXBF1},
	{RATE_GROUP_ID_VHT8_9SS1_TXBF1,		WL_TX_CHAINS_2,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_TXBF1},
	{RATE_GROUP_ID_VHT10_11SS1_TXBF1,	WL_TX_CHAINS_2,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_TXBF1},
	{RATE_GROUP_ID_MCS8_15_TXBF0,		WL_TX_CHAINS_2,		WL_TX_MODE_TXBF,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_TXBF0},
	{RATE_GROUP_ID_VHT8_9SS2_TXBF0,		WL_TX_CHAINS_2,		WL_TX_MODE_TXBF,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_TXBF0},
	{RATE_GROUP_ID_VHT10_11SS2_TXBF0,	WL_TX_CHAINS_2,		WL_TX_MODE_TXBF,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_TXBF0},
	{RATE_GROUP_ID_OFDM_TXBF2,		WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_OFDM,		OFDM6_TXBF2},
	{RATE_GROUP_ID_MCS0_7_TXBF2,		WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_TXBF2},
	{RATE_GROUP_ID_VHT8_9SS1_TXBF2,		WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_TXBF2},
	{RATE_GROUP_ID_VHT10_11SS1_TXBF2,	WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_TXBF2},
	{RATE_GROUP_ID_MCS8_15_TXBF1,		WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_TXBF1},
	{RATE_GROUP_ID_VHT8_9SS2_TXBF1,		WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_TXBF1},
	{RATE_GROUP_ID_VHT10_11SS2_TXBF1,	WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_TXBF1},
	{RATE_GROUP_ID_MCS16_23_TXBF0,		WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16_TXBF0},
	{RATE_GROUP_ID_VHT8_9SS3_TXBF0,		WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16_TXBF0},
	{RATE_GROUP_ID_VHT10_11SS3_TXBF0,	WL_TX_CHAINS_3,		WL_TX_MODE_TXBF,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16_TXBF0},
	{RATE_GROUP_ID_OFDM_TXBF3,		WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_OFDM,		OFDM6_TXBF3},
	{RATE_GROUP_ID_MCS0_7_TXBF3,		WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_TXBF3},
	{RATE_GROUP_ID_VHT8_9SS1_TXBF3,		WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_TXBF3},
	{RATE_GROUP_ID_VHT10_11SS1_TXBF3,	WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_1,	PPR_RATE_VHT,		MCS0_TXBF3},
	{RATE_GROUP_ID_MCS8_15_TXBF2,		WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_TXBF2},
	{RATE_GROUP_ID_VHT8_9SS2_TXBF2,		WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_TXBF2},
	{RATE_GROUP_ID_VHT10_11SS2_TXBF2,	WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_2,	PPR_RATE_VHT,		MCS8_TXBF2},
	{RATE_GROUP_ID_MCS16_23_TXBF1,		WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16_TXBF1},
	{RATE_GROUP_ID_VHT8_9SS3_TXBF1,		WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16_TXBF1},
	{RATE_GROUP_ID_VHT10_11SS3_TXBF1,	WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_3,	PPR_RATE_VHT,		MCS16_TXBF1},
	{RATE_GROUP_ID_MCS24_31_TXBF0,		WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_4,	PPR_RATE_VHT,		MCS24_TXBF0},
	{RATE_GROUP_ID_VHT8_9SS4_TXBF0,		WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_4,	PPR_RATE_VHT,		MCS24_TXBF0},
	{RATE_GROUP_ID_VHT10_11SS4_TXBF0,	WL_TX_CHAINS_4,		WL_TX_MODE_TXBF,	WL_TX_NSS_4,	PPR_RATE_VHT,		MCS24_TXBF0},
};

static const ppr_tbl_t ppr_table[] = {
	/* Label			Rate Group ID */
	{"DSSS1",			RATE_GROUP_ID_DSSS},
	{"DSSS2",			RATE_GROUP_ID_DSSS},
	{"DSSS5",			RATE_GROUP_ID_DSSS},
	{"DSSS11",			RATE_GROUP_ID_DSSS},
	{"OFDM6",			RATE_GROUP_ID_OFDM},
	{"OFDM9",			RATE_GROUP_ID_OFDM},
	{"OFDM12",			RATE_GROUP_ID_OFDM},
	{"OFDM18",			RATE_GROUP_ID_OFDM},
	{"OFDM24",			RATE_GROUP_ID_OFDM},
	{"OFDM36",			RATE_GROUP_ID_OFDM},
	{"OFDM48",			RATE_GROUP_ID_OFDM},
	{"OFDM54",			RATE_GROUP_ID_OFDM},
	{"MCS0",			RATE_GROUP_ID_MCS0_7},
	{"MCS1",			RATE_GROUP_ID_MCS0_7},
	{"MCS2",			RATE_GROUP_ID_MCS0_7},
	{"MCS3",			RATE_GROUP_ID_MCS0_7},
	{"MCS4",			RATE_GROUP_ID_MCS0_7},
	{"MCS5",			RATE_GROUP_ID_MCS0_7},
	{"MCS6",			RATE_GROUP_ID_MCS0_7},
	{"MCS7",			RATE_GROUP_ID_MCS0_7},
	{"VHT8SS1",			RATE_GROUP_ID_VHT8_9SS1},
	{"VHT9SS1",			RATE_GROUP_ID_VHT8_9SS1},
	{"VHT10SS1",			RATE_GROUP_ID_VHT10_11SS1},
	{"VHT11SS1",			RATE_GROUP_ID_VHT10_11SS1},
	{"DSSS1_MULTI1",		RATE_GROUP_ID_DSSS_MULTI1},
	{"DSSS2_MULTI1",		RATE_GROUP_ID_DSSS_MULTI1},
	{"DSSS5_MULTI1",		RATE_GROUP_ID_DSSS_MULTI1},
	{"DSSS11_MULTI1",		RATE_GROUP_ID_DSSS_MULTI1},
	{"OFDM6_CDD1",			RATE_GROUP_ID_OFDM_CDD1},
	{"OFDM9_CDD1",			RATE_GROUP_ID_OFDM_CDD1},
	{"OFDM12_CDD1",			RATE_GROUP_ID_OFDM_CDD1},
	{"OFDM18_CDD1",			RATE_GROUP_ID_OFDM_CDD1},
	{"OFDM24_CDD1",			RATE_GROUP_ID_OFDM_CDD1},
	{"OFDM36_CDD1",			RATE_GROUP_ID_OFDM_CDD1},
	{"OFDM48_CDD1",			RATE_GROUP_ID_OFDM_CDD1},
	{"OFDM54_CDD1",			RATE_GROUP_ID_OFDM_CDD1},
	{"MCS0_CDD1",			RATE_GROUP_ID_MCS0_7_CDD1},
	{"MCS1_CDD1",			RATE_GROUP_ID_MCS0_7_CDD1},
	{"MCS2_CDD1",			RATE_GROUP_ID_MCS0_7_CDD1},
	{"MCS3_CDD1",			RATE_GROUP_ID_MCS0_7_CDD1},
	{"MCS4_CDD1",			RATE_GROUP_ID_MCS0_7_CDD1},
	{"MCS5_CDD1",			RATE_GROUP_ID_MCS0_7_CDD1},
	{"MCS6_CDD1",			RATE_GROUP_ID_MCS0_7_CDD1},
	{"MCS7_CDD1",			RATE_GROUP_ID_MCS0_7_CDD1},
	{"VHT8SS1_CDD1",		RATE_GROUP_ID_VHT8_9SS1_CDD1},
	{"VHT9SS1_CDD1",		RATE_GROUP_ID_VHT8_9SS1_CDD1},
	{"VHT10SS1_CDD1",		RATE_GROUP_ID_VHT10_11SS1_CDD1},
	{"VHT11SS1_CDD1",		RATE_GROUP_ID_VHT10_11SS1_CDD1},
	{"MCS0_STBC",			RATE_GROUP_ID_MCS0_7_STBC},
	{"MCS1_STBC",			RATE_GROUP_ID_MCS0_7_STBC},
	{"MCS2_STBC",			RATE_GROUP_ID_MCS0_7_STBC},
	{"MCS3_STBC",			RATE_GROUP_ID_MCS0_7_STBC},
	{"MCS4_STBC",			RATE_GROUP_ID_MCS0_7_STBC},
	{"MCS5_STBC",			RATE_GROUP_ID_MCS0_7_STBC},
	{"MCS6_STBC",			RATE_GROUP_ID_MCS0_7_STBC},
	{"MCS7_STBC",			RATE_GROUP_ID_MCS0_7_STBC},
	{"VHT8SS1_STBC",		RATE_GROUP_ID_VHT8_9SS1_STBC},
	{"VHT9SS1_STBC",		RATE_GROUP_ID_VHT8_9SS1_STBC},
	{"VHT10SS1_STBC",		RATE_GROUP_ID_VHT10_11SS1_STBC},
	{"VHT11SS1_STBC",		RATE_GROUP_ID_VHT10_11SS1_STBC},
	{"MCS8",			RATE_GROUP_ID_MCS8_15},
	{"MCS9",			RATE_GROUP_ID_MCS8_15},
	{"MCS10",			RATE_GROUP_ID_MCS8_15},
	{"MCS11",			RATE_GROUP_ID_MCS8_15},
	{"MCS12",			RATE_GROUP_ID_MCS8_15},
	{"MCS13",			RATE_GROUP_ID_MCS8_15},
	{"MCS14",			RATE_GROUP_ID_MCS8_15},
	{"MCS15",			RATE_GROUP_ID_MCS8_15},
	{"VHT8SS2",			RATE_GROUP_ID_VHT8_9SS2},
	{"VHT9SS2",			RATE_GROUP_ID_VHT8_9SS2},
	{"VHT10SS2",			RATE_GROUP_ID_VHT10_11SS2},
	{"VHT11SS2",			RATE_GROUP_ID_VHT10_11SS2},
	{"DSSS1_MULTI2",		RATE_GROUP_ID_DSSS_MULTI2},
	{"DSSS2_MULTI2",		RATE_GROUP_ID_DSSS_MULTI2},
	{"DSSS5_MULTI2",		RATE_GROUP_ID_DSSS_MULTI2},
	{"DSSS11_MULTI2",		RATE_GROUP_ID_DSSS_MULTI2},
	{"OFDM6_CDD2",			RATE_GROUP_ID_OFDM_CDD2},
	{"OFDM9_CDD2",			RATE_GROUP_ID_OFDM_CDD2},
	{"OFDM12_CDD2",			RATE_GROUP_ID_OFDM_CDD2},
	{"OFDM18_CDD2",			RATE_GROUP_ID_OFDM_CDD2},
	{"OFDM24_CDD2",			RATE_GROUP_ID_OFDM_CDD2},
	{"OFDM36_CDD2",			RATE_GROUP_ID_OFDM_CDD2},
	{"OFDM48_CDD2",			RATE_GROUP_ID_OFDM_CDD2},
	{"OFDM54_CDD2",			RATE_GROUP_ID_OFDM_CDD2},
	{"MCS0_CDD2",			RATE_GROUP_ID_MCS0_7_CDD2},
	{"MCS1_CDD2",			RATE_GROUP_ID_MCS0_7_CDD2},
	{"MCS2_CDD2",			RATE_GROUP_ID_MCS0_7_CDD2},
	{"MCS3_CDD2",			RATE_GROUP_ID_MCS0_7_CDD2},
	{"MCS4_CDD2",			RATE_GROUP_ID_MCS0_7_CDD2},
	{"MCS5_CDD2",			RATE_GROUP_ID_MCS0_7_CDD2},
	{"MCS6_CDD2",			RATE_GROUP_ID_MCS0_7_CDD2},
	{"MCS7_CDD2",			RATE_GROUP_ID_MCS0_7_CDD2},
	{"VHT8SS1_CDD2",		RATE_GROUP_ID_VHT8_9SS1_CDD2},
	{"VHT9SS1_CDD2",		RATE_GROUP_ID_VHT8_9SS1_CDD2},
	{"VHT10SS1_CDD2",		RATE_GROUP_ID_VHT10_11SS1_CDD2},
	{"VHT11SS1_CDD2",		RATE_GROUP_ID_VHT10_11SS1_CDD2},
	{"MCS0_STBC_SPEXP1",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP1},
	{"MCS1_STBC_SPEXP1",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP1},
	{"MCS2_STBC_SPEXP1",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP1},
	{"MCS3_STBC_SPEXP1",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP1},
	{"MCS4_STBC_SPEXP1",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP1},
	{"MCS5_STBC_SPEXP1",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP1},
	{"MCS6_STBC_SPEXP1",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP1},
	{"MCS7_STBC_SPEXP1",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP1},
	{"VHT8SS1_STBC_SPEXP1",		RATE_GROUP_ID_VHT8_9SS1_STBC_SPEXP1},
	{"VHT9SS1_STBC_SPEXP1",		RATE_GROUP_ID_VHT8_9SS1_STBC_SPEXP1},
	{"VHT10SS1_STBC_SPEXP1",	RATE_GROUP_ID_VHT10_11SS1_STBC_SPEXP1},
	{"VHT11SS1_STBC_SPEXP1",	RATE_GROUP_ID_VHT10_11SS1_STBC_SPEXP1},
	{"MCS8_SPEXP1",			RATE_GROUP_ID_MCS8_15_SPEXP1},
	{"MCS9_SPEXP1",			RATE_GROUP_ID_MCS8_15_SPEXP1},
	{"MCS10_SPEXP1",		RATE_GROUP_ID_MCS8_15_SPEXP1},
	{"MCS11_SPEXP1",		RATE_GROUP_ID_MCS8_15_SPEXP1},
	{"MCS12_SPEXP1",		RATE_GROUP_ID_MCS8_15_SPEXP1},
	{"MCS13_SPEXP1",		RATE_GROUP_ID_MCS8_15_SPEXP1},
	{"MCS14_SPEXP1",		RATE_GROUP_ID_MCS8_15_SPEXP1},
	{"MCS15_SPEXP1",		RATE_GROUP_ID_MCS8_15_SPEXP1},
	{"VHT8SS2_SPEXP1",		RATE_GROUP_ID_VHT8_9SS2_SPEXP1},
	{"VHT9SS2_SPEXP1",		RATE_GROUP_ID_VHT8_9SS2_SPEXP1},
	{"VHT10SS2_SPEXP1",		RATE_GROUP_ID_VHT10_11SS2_SPEXP1},
	{"VHT11SS2_SPEXP1",		RATE_GROUP_ID_VHT10_11SS2_SPEXP1},
	{"MCS16",			RATE_GROUP_ID_MCS16_23},
	{"MCS17",			RATE_GROUP_ID_MCS16_23},
	{"MCS18",			RATE_GROUP_ID_MCS16_23},
	{"MCS19",			RATE_GROUP_ID_MCS16_23},
	{"MCS20",			RATE_GROUP_ID_MCS16_23},
	{"MCS21",			RATE_GROUP_ID_MCS16_23},
	{"MCS22",			RATE_GROUP_ID_MCS16_23},
	{"MCS23",			RATE_GROUP_ID_MCS16_23},
	{"VHT8SS3",			RATE_GROUP_ID_VHT8_9SS3},
	{"VHT9SS3",			RATE_GROUP_ID_VHT8_9SS3},
	{"VHT10SS3",			RATE_GROUP_ID_VHT10_11SS3},
	{"VHT11SS3",			RATE_GROUP_ID_VHT10_11SS3},
	{"DSSS1_MULTI3",		RATE_GROUP_ID_DSSS_MULTI3},
	{"DSSS2_MULTI3",		RATE_GROUP_ID_DSSS_MULTI3},
	{"DSSS5_MULTI3",		RATE_GROUP_ID_DSSS_MULTI3},
	{"DSSS11_MULTI3",		RATE_GROUP_ID_DSSS_MULTI3},
	{"OFDM6_CDD3",			RATE_GROUP_ID_OFDM_CDD3},
	{"OFDM9_CDD3",			RATE_GROUP_ID_OFDM_CDD3},
	{"OFDM12_CDD3",			RATE_GROUP_ID_OFDM_CDD3},
	{"OFDM18_CDD3",			RATE_GROUP_ID_OFDM_CDD3},
	{"OFDM24_CDD3",			RATE_GROUP_ID_OFDM_CDD3},
	{"OFDM36_CDD3",			RATE_GROUP_ID_OFDM_CDD3},
	{"OFDM48_CDD3",			RATE_GROUP_ID_OFDM_CDD3},
	{"OFDM54_CDD3",			RATE_GROUP_ID_OFDM_CDD3},
	{"MCS0_CDD3",			RATE_GROUP_ID_MCS0_7_CDD3},
	{"MCS1_CDD3",			RATE_GROUP_ID_MCS0_7_CDD3},
	{"MCS2_CDD3",			RATE_GROUP_ID_MCS0_7_CDD3},
	{"MCS3_CDD3",			RATE_GROUP_ID_MCS0_7_CDD3},
	{"MCS4_CDD3",			RATE_GROUP_ID_MCS0_7_CDD3},
	{"MCS5_CDD3",			RATE_GROUP_ID_MCS0_7_CDD3},
	{"MCS6_CDD3",			RATE_GROUP_ID_MCS0_7_CDD3},
	{"MCS7_CDD3",			RATE_GROUP_ID_MCS0_7_CDD3},
	{"VHT8SS1_CDD3",		RATE_GROUP_ID_VHT8_9SS1_CDD3},
	{"VHT9SS1_CDD3",		RATE_GROUP_ID_VHT8_9SS1_CDD3},
	{"VHT10SS1_CDD3",		RATE_GROUP_ID_VHT10_11SS1_CDD3},
	{"VHT11SS1_CDD3",		RATE_GROUP_ID_VHT10_11SS1_CDD3},
	{"MCS0_STBC_SPEXP2",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP2},
	{"MCS1_STBC_SPEXP2",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP2},
	{"MCS2_STBC_SPEXP2",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP2},
	{"MCS3_STBC_SPEXP2",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP2},
	{"MCS4_STBC_SPEXP2",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP2},
	{"MCS5_STBC_SPEXP2",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP2},
	{"MCS6_STBC_SPEXP2",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP2},
	{"MCS7_STBC_SPEXP2",		RATE_GROUP_ID_MCS0_7_STBC_SPEXP2},
	{"VHT8SS1_STBC_SPEXP2",		RATE_GROUP_ID_VHT8_9SS1_STBC_SPEXP2},
	{"VHT9SS1_STBC_SPEXP2",		RATE_GROUP_ID_VHT8_9SS1_STBC_SPEXP2},
	{"VHT10SS1_STBC_SPEXP2",	RATE_GROUP_ID_VHT10_11SS1_STBC_SPEXP2},
	{"VHT11SS1_STBC_SPEXP2",	RATE_GROUP_ID_VHT10_11SS1_STBC_SPEXP2},
	{"MCS8_SPEXP2",			RATE_GROUP_ID_MCS8_15_SPEXP2},
	{"MCS9_SPEXP2",			RATE_GROUP_ID_MCS8_15_SPEXP2},
	{"MCS10_SPEXP2",		RATE_GROUP_ID_MCS8_15_SPEXP2},
	{"MCS11_SPEXP2",		RATE_GROUP_ID_MCS8_15_SPEXP2},
	{"MCS12_SPEXP2",		RATE_GROUP_ID_MCS8_15_SPEXP2},
	{"MCS13_SPEXP2",		RATE_GROUP_ID_MCS8_15_SPEXP2},
	{"MCS14_SPEXP2",		RATE_GROUP_ID_MCS8_15_SPEXP2},
	{"MCS15_SPEXP2",		RATE_GROUP_ID_MCS8_15_SPEXP2},
	{"VHT8SS2_SPEXP2",		RATE_GROUP_ID_VHT8_9SS2_SPEXP2},
	{"VHT9SS2_SPEXP2",		RATE_GROUP_ID_VHT8_9SS2_SPEXP2},
	{"VHT10SS2_SPEXP2",		RATE_GROUP_ID_VHT10_11SS2_SPEXP2},
	{"VHT11SS2_SPEXP2",		RATE_GROUP_ID_VHT10_11SS2_SPEXP2},
	{"MCS16_SPEXP1",		RATE_GROUP_ID_MCS16_23},
	{"MCS17_SPEXP1",		RATE_GROUP_ID_MCS16_23},
	{"MCS18_SPEXP1",		RATE_GROUP_ID_MCS16_23},
	{"MCS19_SPEXP1",		RATE_GROUP_ID_MCS16_23},
	{"MCS20_SPEXP1",		RATE_GROUP_ID_MCS16_23},
	{"MCS21_SPEXP1",		RATE_GROUP_ID_MCS16_23},
	{"MCS22_SPEXP1",		RATE_GROUP_ID_MCS16_23},
	{"MCS23_SPEXP1",		RATE_GROUP_ID_MCS16_23},
	{"VHT8SS3_SPEXP1",		RATE_GROUP_ID_VHT8_9SS3},
	{"VHT9SS3_SPEXP1",		RATE_GROUP_ID_VHT8_9SS3},
	{"VHT10SS3_SPEXP1",		RATE_GROUP_ID_VHT10_11SS3},
	{"VHT11SS3_SPEXP1",		RATE_GROUP_ID_VHT10_11SS3},
	{"MCS24",			RATE_GROUP_ID_MCS24_31},
	{"MCS25",			RATE_GROUP_ID_MCS24_31},
	{"MCS26",			RATE_GROUP_ID_MCS24_31},
	{"MCS27",			RATE_GROUP_ID_MCS24_31},
	{"MCS28",			RATE_GROUP_ID_MCS24_31},
	{"MCS29",			RATE_GROUP_ID_MCS24_31},
	{"MCS30",			RATE_GROUP_ID_MCS24_31},
	{"MCS31",			RATE_GROUP_ID_MCS24_31},
	{"VHT8SS4",			RATE_GROUP_ID_VHT8_9SS4},
	{"VHT9SS4",			RATE_GROUP_ID_VHT8_9SS4},
	{"VHT10SS4",			RATE_GROUP_ID_VHT10_11SS4},
	{"VHT11SS4",			RATE_GROUP_ID_VHT10_11SS4},
	{"OFDM6_TXBF1",			RATE_GROUP_ID_OFDM_TXBF1},
	{"OFDM9_TXBF1",			RATE_GROUP_ID_OFDM_TXBF1},
	{"OFDM12_TXBF1",		RATE_GROUP_ID_OFDM_TXBF1},
	{"OFDM18_TXBF1",		RATE_GROUP_ID_OFDM_TXBF1},
	{"OFDM24_TXBF1",		RATE_GROUP_ID_OFDM_TXBF1},
	{"OFDM36_TXBF1",		RATE_GROUP_ID_OFDM_TXBF1},
	{"OFDM48_TXBF1",		RATE_GROUP_ID_OFDM_TXBF1},
	{"OFDM54_TXBF1",		RATE_GROUP_ID_OFDM_TXBF1},
	{"MCS0_TXBF1",			RATE_GROUP_ID_MCS0_7_TXBF1},
	{"MCS1_TXBF1",			RATE_GROUP_ID_MCS0_7_TXBF1},
	{"MCS2_TXBF1",			RATE_GROUP_ID_MCS0_7_TXBF1},
	{"MCS3_TXBF1",			RATE_GROUP_ID_MCS0_7_TXBF1},
	{"MCS4_TXBF1",			RATE_GROUP_ID_MCS0_7_TXBF1},
	{"MCS5_TXBF1",			RATE_GROUP_ID_MCS0_7_TXBF1},
	{"MCS6_TXBF1",			RATE_GROUP_ID_MCS0_7_TXBF1},
	{"MCS7_TXBF1",			RATE_GROUP_ID_MCS0_7_TXBF1},
	{"VHT8SS1_TXBF1",		RATE_GROUP_ID_VHT8_9SS1_TXBF1},
	{"VHT9SS1_TXBF1",		RATE_GROUP_ID_VHT8_9SS1_TXBF1},
	{"VHT10SS1_TXBF1",		RATE_GROUP_ID_VHT10_11SS1_TXBF1},
	{"VHT11SS1_TXBF1",		RATE_GROUP_ID_VHT10_11SS1_TXBF1},
	{"MCS8_TXBF0",			RATE_GROUP_ID_MCS8_15_TXBF0},
	{"MCS9_TXBF0",			RATE_GROUP_ID_MCS8_15_TXBF0},
	{"MCS10_TXBF0",			RATE_GROUP_ID_MCS8_15_TXBF0},
	{"MCS11_TXBF0",			RATE_GROUP_ID_MCS8_15_TXBF0},
	{"MCS12_TXBF0",			RATE_GROUP_ID_MCS8_15_TXBF0},
	{"MCS13_TXBF0",			RATE_GROUP_ID_MCS8_15_TXBF0},
	{"MCS14_TXBF0",			RATE_GROUP_ID_MCS8_15_TXBF0},
	{"MCS15_TXBF0",			RATE_GROUP_ID_MCS8_15_TXBF0},
	{"",				RATE_GROUP_ID_VHT8_9SS2_TXBF0},
	{"",				RATE_GROUP_ID_VHT8_9SS2_TXBF0},
	{"",				RATE_GROUP_ID_VHT10_11SS2_TXBF0},
	{"",				RATE_GROUP_ID_VHT10_11SS2_TXBF0},
	{"OFDM6_TXBF2",			RATE_GROUP_ID_OFDM_TXBF2},
	{"OFDM9_TXBF2",			RATE_GROUP_ID_OFDM_TXBF2},
	{"OFDM12_TXBF2",		RATE_GROUP_ID_OFDM_TXBF2},
	{"OFDM18_TXBF2",		RATE_GROUP_ID_OFDM_TXBF2},
	{"OFDM24_TXBF2",		RATE_GROUP_ID_OFDM_TXBF2},
	{"OFDM36_TXBF2",		RATE_GROUP_ID_OFDM_TXBF2},
	{"OFDM48_TXBF2",		RATE_GROUP_ID_OFDM_TXBF2},
	{"OFDM54_TXBF2",		RATE_GROUP_ID_OFDM_TXBF2},
	{"MCS0_TXBF2",			RATE_GROUP_ID_MCS0_7_TXBF2},
	{"MCS1_TXBF2",			RATE_GROUP_ID_MCS0_7_TXBF2},
	{"MCS2_TXBF2",			RATE_GROUP_ID_MCS0_7_TXBF2},
	{"MCS3_TXBF2",			RATE_GROUP_ID_MCS0_7_TXBF2},
	{"MCS4_TXBF2",			RATE_GROUP_ID_MCS0_7_TXBF2},
	{"MCS5_TXBF2",			RATE_GROUP_ID_MCS0_7_TXBF2},
	{"MCS6_TXBF2",			RATE_GROUP_ID_MCS0_7_TXBF2},
	{"MCS7_TXBF2",			RATE_GROUP_ID_MCS0_7_TXBF2},
	{"VHT8SS1_TXBF2",		RATE_GROUP_ID_VHT8_9SS1_TXBF2},
	{"VHT9SS1_TXBF2",		RATE_GROUP_ID_VHT8_9SS1_TXBF2},
	{"VHT10SS1_TXBF2",		RATE_GROUP_ID_VHT10_11SS1_TXBF2},
	{"VHT11SS1_TXBF2",		RATE_GROUP_ID_VHT10_11SS1_TXBF2},
	{"MCS8_TXBF1",			RATE_GROUP_ID_MCS8_15_TXBF1},
	{"MCS9_TXBF1",			RATE_GROUP_ID_MCS8_15_TXBF1},
	{"MCS10_TXBF1",			RATE_GROUP_ID_MCS8_15_TXBF1},
	{"MCS11_TXBF1",			RATE_GROUP_ID_MCS8_15_TXBF1},
	{"MCS12_TXBF1",			RATE_GROUP_ID_MCS8_15_TXBF1},
	{"MCS13_TXBF1",			RATE_GROUP_ID_MCS8_15_TXBF1},
	{"MCS14_TXBF1",			RATE_GROUP_ID_MCS8_15_TXBF1},
	{"MCS15_TXBF1",			RATE_GROUP_ID_MCS8_15_TXBF1},
	{"VHT8SS2_TXBF1",		RATE_GROUP_ID_VHT8_9SS2_TXBF1},
	{"VHT9SS2_TXBF1",		RATE_GROUP_ID_VHT8_9SS2_TXBF1},
	{"VHT10SS2_TXBF1",		RATE_GROUP_ID_VHT10_11SS2_TXBF1},
	{"VHT11SS2_TXBF1",		RATE_GROUP_ID_VHT10_11SS2_TXBF1},
	{"MCS16_TXBF0",			RATE_GROUP_ID_MCS16_23_TXBF0},
	{"MCS17_TXBF0",			RATE_GROUP_ID_MCS16_23_TXBF0},
	{"MCS18_TXBF0",			RATE_GROUP_ID_MCS16_23_TXBF0},
	{"MCS19_TXBF0",			RATE_GROUP_ID_MCS16_23_TXBF0},
	{"MCS20_TXBF0",			RATE_GROUP_ID_MCS16_23_TXBF0},
	{"MCS21_TXBF0",			RATE_GROUP_ID_MCS16_23_TXBF0},
	{"MCS22_TXBF0",			RATE_GROUP_ID_MCS16_23_TXBF0},
	{"MCS23_TXBF0",			RATE_GROUP_ID_MCS16_23_TXBF0},
	{"",				RATE_GROUP_ID_VHT8_9SS3_TXBF0},
	{"",				RATE_GROUP_ID_VHT8_9SS3_TXBF0},
	{"",				RATE_GROUP_ID_VHT10_11SS3_TXBF0},
	{"",				RATE_GROUP_ID_VHT10_11SS3_TXBF0},
	{"OFDM6_TXBF3",			RATE_GROUP_ID_OFDM_TXBF3},
	{"OFDM9_TXBF3",			RATE_GROUP_ID_OFDM_TXBF3},
	{"OFDM12_TXBF3",		RATE_GROUP_ID_OFDM_TXBF3},
	{"OFDM18_TXBF3",		RATE_GROUP_ID_OFDM_TXBF3},
	{"OFDM24_TXBF3",		RATE_GROUP_ID_OFDM_TXBF3},
	{"OFDM36_TXBF3",		RATE_GROUP_ID_OFDM_TXBF3},
	{"OFDM48_TXBF3",		RATE_GROUP_ID_OFDM_TXBF3},
	{"OFDM54_TXBF3",		RATE_GROUP_ID_OFDM_TXBF3},
	{"MCS0_TXBF3",			RATE_GROUP_ID_MCS0_7_TXBF3},
	{"MCS1_TXBF3",			RATE_GROUP_ID_MCS0_7_TXBF3},
	{"MCS2_TXBF3",			RATE_GROUP_ID_MCS0_7_TXBF3},
	{"MCS3_TXBF3",			RATE_GROUP_ID_MCS0_7_TXBF3},
	{"MCS4_TXBF3",			RATE_GROUP_ID_MCS0_7_TXBF3},
	{"MCS5_TXBF3",			RATE_GROUP_ID_MCS0_7_TXBF3},
	{"MCS6_TXBF3",			RATE_GROUP_ID_MCS0_7_TXBF3},
	{"MCS7_TXBF3",			RATE_GROUP_ID_MCS0_7_TXBF3},
	{"VHT8SS1_TXBF3",		RATE_GROUP_ID_VHT8_9SS1_TXBF3},
	{"VHT9SS1_TXBF3",		RATE_GROUP_ID_VHT8_9SS1_TXBF3},
	{"VHT10SS1_TXBF3",		RATE_GROUP_ID_VHT10_11SS1_TXBF3},
	{"VHT11SS1_TXBF3",		RATE_GROUP_ID_VHT10_11SS1_TXBF3},
	{"MCS8_TXBF2",			RATE_GROUP_ID_MCS8_15_TXBF2},
	{"MCS9_TXBF2",			RATE_GROUP_ID_MCS8_15_TXBF2},
	{"MCS10_TXBF2",			RATE_GROUP_ID_MCS8_15_TXBF2},
	{"MCS11_TXBF2",			RATE_GROUP_ID_MCS8_15_TXBF2},
	{"MCS12_TXBF2",			RATE_GROUP_ID_MCS8_15_TXBF2},
	{"MCS13_TXBF2",			RATE_GROUP_ID_MCS8_15_TXBF2},
	{"MCS14_TXBF2",			RATE_GROUP_ID_MCS8_15_TXBF2},
	{"MCS15_TXBF2",			RATE_GROUP_ID_MCS8_15_TXBF2},
	{"VHT8SS2_TXBF2",		RATE_GROUP_ID_VHT8_9SS2_TXBF2},
	{"VHT9SS2_TXBF2",		RATE_GROUP_ID_VHT8_9SS2_TXBF2},
	{"VHT10SS2_TXBF2",		RATE_GROUP_ID_VHT10_11SS2_TXBF2},
	{"VHT11SS2_TXBF2",		RATE_GROUP_ID_VHT10_11SS2_TXBF2},
	{"MCS16_TXBF1",			RATE_GROUP_ID_MCS16_23_TXBF1},
	{"MCS17_TXBF1",			RATE_GROUP_ID_MCS16_23_TXBF1},
	{"MCS18_TXBF1",			RATE_GROUP_ID_MCS16_23_TXBF1},
	{"MCS19_TXBF1",			RATE_GROUP_ID_MCS16_23_TXBF1},
	{"MCS20_TXBF1",			RATE_GROUP_ID_MCS16_23_TXBF1},
	{"MCS21_TXBF1",			RATE_GROUP_ID_MCS16_23_TXBF1},
	{"MCS22_TXBF1",			RATE_GROUP_ID_MCS16_23_TXBF1},
	{"MCS23_TXBF1",			RATE_GROUP_ID_MCS16_23_TXBF1},
	{"VHT8SS3_TXBF1",		RATE_GROUP_ID_VHT8_9SS3_TXBF1},
	{"VHT9SS3_TXBF1",		RATE_GROUP_ID_VHT8_9SS3_TXBF1},
	{"VHT10SS3_TXBF1",		RATE_GROUP_ID_VHT10_11SS3_TXBF1},
	{"VHT11SS3_TXBF1",		RATE_GROUP_ID_VHT10_11SS3_TXBF1},
	{"MCS24_TXBF0",			RATE_GROUP_ID_MCS24_31_TXBF0},
	{"MCS25_TXBF0",			RATE_GROUP_ID_MCS24_31_TXBF0},
	{"MCS26_TXBF0",			RATE_GROUP_ID_MCS24_31_TXBF0},
	{"MCS27_TXBF0",			RATE_GROUP_ID_MCS24_31_TXBF0},
	{"MCS28_TXBF0",			RATE_GROUP_ID_MCS24_31_TXBF0},
	{"MCS29_TXBF0",			RATE_GROUP_ID_MCS24_31_TXBF0},
	{"MCS30_TXBF0",			RATE_GROUP_ID_MCS24_31_TXBF0},
	{"MCS31_TXBF0",			RATE_GROUP_ID_MCS24_31_TXBF0},
	{"",				RATE_GROUP_ID_VHT8_9SS4_TXBF0},
	{"",				RATE_GROUP_ID_VHT8_9SS4_TXBF0},
	{"",				RATE_GROUP_ID_VHT10_11SS4_TXBF0},
	{"",				RATE_GROUP_ID_VHT10_11SS4_TXBF0},
};

#define MHZ_TO_HALF_MHZ 2

const char *get_clm_rate_group_label(int rategroup);
const char *get_reg_rate_string_from_ratespec(int ratespec);
reg_rate_index_t get_reg_rate_index_from_ratespec(int ratespec);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _WLU_RATES_MATRIX_H_ */
