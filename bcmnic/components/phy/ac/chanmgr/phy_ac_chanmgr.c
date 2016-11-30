/*
 * ACPHY Channel Manager module implementation
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2016,
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: phy_ac_chanmgr.c 662160 2016-09-28 16:13:41Z vinliu $
 */

#include <wlc_cfg.h>
#if (ACCONF != 0) || (ACCONF2 != 0)
#include <phy_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <phy_dbg.h>
#include <phy_mem.h>
#include <qmath.h>
#include "phy_type_chanmgr.h"
#include <phy_chanmgr_api.h>
#include <phy_ac.h>
#include <phy_ac_chanmgr.h>
#include <phy_ac_antdiv.h>
#include <phy_ac_tpc.h>
#include <phy_papdcal.h>

/* ************************ */
/* Modules used by this module */
/* ************************ */
#include <wlc_radioreg_20691.h>
#include <wlc_radioreg_20693.h>
#include <wlc_radioreg_20694.h>
#include <wlc_radioreg_20695.h>
#include <wlc_radioreg_20696.h>
#include <wlc_phy_radio.h>
#include <wlc_phy_shim.h>
#include <wlc_phyreg_ac.h>
#include <wlc_phytbl_20691.h>
#include <wlc_phytbl_20693.h>
#include <wlc_phytbl_20694.h>
#include <wlc_phytbl_20696.h>
#include <wlc_phytbl_ac.h>
#include <wlc_phy_ac_gains.h>
#include <phy_btcx.h>
#include <phy_tpc.h>
#include <phy_ac_dccal.h>
#include <phy_ac_noise.h>
#include <phy_ac_hirssi.h>
#include <phy_rxgcrs_api.h>
#include <phy_rxiqcal.h>
#include <phy_txiqlocal.h>
#include <phy_ac_rxiqcal.h>
#include <phy_ac_txiqlocal.h>
#include <hndpmu.h>
#include <sbchipc.h>
#include <phy_utils_reg.h>
#include <phy_utils_channel.h>
#include <phy_utils_math.h>
#include <phy_utils_var.h>
#include <phy_ac_info.h>
#ifdef WL_ETMODE
#include <phy_ac_et.h>
#endif /* WL_ETMODE */
#include "phy_ac_nap.h"

#ifdef WL_DSI
#include "phy_ac_dsi.h"
#endif /* WL_DSI */

#include <phy_rstr.h>

#ifdef WLC_SW_DIVERSITY
#include "phy_ac_antdiv.h"
#endif

#ifndef ALL_NEW_PHY_MOD
/* < TODO: all these are going away... */
#include <wlc_phy_int.h>
/* TODO: all these are going away... > */
#endif
#include <bcmdevs.h>

#ifdef PHYWAR_43012_HW43012_211_RF_SW_CTRL
static void phy_ac_WAR_43012_rf_sw_ctrl_pinmux(phy_info_t *pi);
#endif /* PHYWAR_43012_HW43012_211_RF_SW_CTRL */

/* module private states */
typedef struct phy_ac_chanmgr_config_info {
	uint8	srom_tssisleep_en; /* TSSI sleep enable */
	uint8	srom_txnoBW80ClkSwitch; /* 5G Tx BW80 AFE CLK switch */
	uint8	vlinpwr2g_from_nvram;
	uint8	vlinpwr5g_from_nvram;
	int8	srom_papdwar; /* papd war enable and threshold */
	bool	srom_paprdis; /* papr disable */
	bool	LTEJ_WAR_en;
} phy_ac_chanmgr_config_info_t;

struct phy_ac_chanmgr_info {
	phy_info_t		*pi;
	phy_ac_info_t		*aci;
	phy_chanmgr_info_t	*cmn_info;
	/* shared data */
	phy_ac_chanmgr_data_t data;
	phy_ac_chanmgr_config_info_t cfg;
	/* add other variable size variables here at the end */
	uint8	acphy_cck_dig_filt_type;
	uint8	chsm_en, ml_en;
	uint8	use_fast_adc_20_40;
	uint8	acphy_enable_smth;
	uint8	acphy_smth_dump_mode;
	bool	vco_12GHz;
	bool	FifoReset; /* flag to hold FifoReset val */
	chanspec_t	radio_chanspec_sc;	/* 3x3_1x1 mode, setting */
	bool	mutx_war_on;	        /* Flag to check MU tx is on/off */
};

/* local functions */
static void phy_ac_chanmgr_write_tx_farrow_tiny(phy_info_t *pi, chanspec_t chanspec,
	chanspec_t chanspec_sc, int sc_mode);
static void phy_ac_chanmgr_chanspec_set(phy_type_chanmgr_ctx_t *ctx, chanspec_t chanspec);
static void phy_ac_chanmgr_upd_interf_mode(phy_type_chanmgr_ctx_t *ctx, chanspec_t chanspec);
static void phy_ac_chanmgr_preempt(phy_type_chanmgr_ctx_t *ctx, bool enable_preempt,
    bool EnablePostRxFilter_Proc);
static int phy_ac_chanmgr_get_chanspec_bandrange(phy_type_chanmgr_ctx_t *ctx, chanspec_t chanspec);
static bool phy_ac_chanmgr_is_txbfcal(phy_type_chanmgr_ctx_t *ctx);
static void wlc_tiny_setup_coarse_dcc(phy_info_t *pi);
#if defined(WLTEST)
static int phy_ac_chanmgr_get_smth(phy_type_chanmgr_ctx_t *ctx, int32* ret_int_ptr);
static int phy_ac_chanmgr_set_smth(phy_type_chanmgr_ctx_t *ctx, int8 int_val);
#endif /* defined(WLTEST) */
static void phy_ac_chanmgr_set_spexp_matrix(phy_info_t *pi);

static void femctrl_clb_4347(phy_info_t *pi, int band_is_2g, int slice);

static const char BCMATTACHDATA(rstr_VlinPwr2g_cD)[]                  = "VlinPwr2g_c%d";
static const char BCMATTACHDATA(rstr_VlinPwr5g_cD)[]                  = "VlinPwr5g_c%d";
static const char BCMATTACHDATA(rstr_Vlinmask2g_cD)[]                 = "Vlinmask2g_c%d";
static const char BCMATTACHDATA(rstr_Vlinmask5g_cD)[]                 = "Vlinmask5g_c%d";
static const char BCMATTACHDATA(rstr_AvVmid_cD)[]                     = "AvVmid_c%d";

/* function to read femctrl params from nvram */
static void BCMATTACHFN(phy_ac_chanmgr_nvram_attach)(phy_ac_chanmgr_info_t *ac_info);
static void phy_ac_chanmgr_std_params_attach(phy_ac_chanmgr_info_t *ac_info);
static int phy_ac_chanmgr_init_chanspec(phy_type_chanmgr_ctx_t *ctx);

/* register phy type specific implementation */
phy_ac_chanmgr_info_t *
BCMATTACHFN(phy_ac_chanmgr_register_impl)(phy_info_t *pi, phy_ac_info_t *aci,
	phy_chanmgr_info_t *cmn_info)
{
	phy_ac_chanmgr_info_t *ac_info;
	phy_type_chanmgr_fns_t fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* allocate all storage together */
	if ((ac_info = phy_malloc(pi, sizeof(phy_ac_chanmgr_info_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc failed\n", __FUNCTION__));
		goto fail;
	}

	/* Initialize params */
	ac_info->pi = pi;
	ac_info->aci = aci;
	ac_info->cmn_info = cmn_info;
	ac_info->radio_chanspec_sc = pi->radio_chanspec;

	if ((pi->paprrmcsgain2g = phy_malloc(pi, (sizeof(*pi->paprrmcsgain2g) *
		NUM_MCS_PAPRR_GAMMA))) == NULL) {
		PHY_ERROR(("%s: paprrmcsgain2g malloc failed\n", __FUNCTION__));
		goto fail;
	}
	if ((pi->paprrmcsgain5g20 = phy_malloc(pi,
			(sizeof(*pi->paprrmcsgain5g20) * NUM_MCS_PAPRR_GAMMA))) == NULL) {
		PHY_ERROR(("%s: paprrmcsgain5g20 malloc failed\n", __FUNCTION__));
		goto fail;
	}
	if ((pi->paprrmcsgain5g40 = phy_malloc(pi,
			(sizeof(*pi->paprrmcsgain5g40) * NUM_MCS_PAPRR_GAMMA))) == NULL) {
		PHY_ERROR(("%s: paprrmcsgain5g40 malloc failed\n", __FUNCTION__));
		goto fail;
	}
	if ((pi->paprrmcsgain5g80 = phy_malloc(pi,
			(sizeof(*pi->paprrmcsgain5g80) * NUM_MCS_PAPRR_GAMMA))) == NULL) {
		PHY_ERROR(("%s: paprrmcsgain5g80 malloc failed\n", __FUNCTION__));
		goto fail;
	}
	if ((pi->paprrmcsgamma2g = phy_malloc(pi, (sizeof(*pi->paprrmcsgamma2g) *
		NUM_MCS_PAPRR_GAMMA))) == NULL) {
		PHY_ERROR(("%s: paprrmcsgamma2g malloc failed\n", __FUNCTION__));
		goto fail;
	}
	if ((pi->paprrmcsgamma5g20 = phy_malloc(pi,
			(sizeof(*pi->paprrmcsgamma5g20) * NUM_MCS_PAPRR_GAMMA))) == NULL) {
		PHY_ERROR(("%s: paprrmcsgamma5g20 malloc failed\n", __FUNCTION__));
		goto fail;
	}
	if ((pi->paprrmcsgamma5g40 = phy_malloc(pi,
			(sizeof(*pi->paprrmcsgamma5g40) * NUM_MCS_PAPRR_GAMMA))) == NULL) {
		PHY_ERROR(("%s: paprrmcsgamma5g40 malloc failed\n", __FUNCTION__));
		goto fail;
	}
	if ((pi->paprrmcsgamma5g80 = phy_malloc(pi,
			(sizeof(*pi->paprrmcsgamma5g80) * NUM_MCS_PAPRR_GAMMA))) == NULL) {
		PHY_ERROR(("%s: paprrmcsgamma5g80 malloc failed\n", __FUNCTION__));
		goto fail;
	}

	/* register PHY type specific implementation */
	bzero(&fns, sizeof(fns));
	fns.init_chanspec = phy_ac_chanmgr_init_chanspec;
	fns.is_txbfcal = phy_ac_chanmgr_is_txbfcal;
	fns.get_bandrange = phy_ac_chanmgr_get_chanspec_bandrange;
	fns.chanspec_set = phy_ac_chanmgr_chanspec_set;
	fns.interfmode_upd = phy_ac_chanmgr_upd_interf_mode;
	fns.preempt = phy_ac_chanmgr_preempt;
#if defined(WLTEST)
	fns.get_smth = phy_ac_chanmgr_get_smth;
	fns.set_smth = phy_ac_chanmgr_set_smth;
#endif /* defined(WLTEST) */
	fns.ctx = ac_info;

	/* Read VLIN signal parameters from NVRAM */
	if (!TINY_RADIO(pi))
		wlc_phy_nvram_vlin_params_read(pi);
	/* Read AVVMID signal from NVARM */
	wlc_phy_nvram_avvmid_read(pi);

	/* set up srom cfg */
	phy_ac_chanmgr_nvram_attach(ac_info);
	phy_ac_chanmgr_std_params_attach(ac_info);

	if (phy_chanmgr_register_impl(cmn_info, &fns) != BCME_OK) {
		PHY_ERROR(("%s: phy_chanmgr_register_impl failed\n", __FUNCTION__));
		goto fail;
	}

	return ac_info;

	/* error handling */
fail:

	phy_ac_chanmgr_unregister_impl(ac_info);
	return NULL;
}

void
BCMATTACHFN(phy_ac_chanmgr_unregister_impl)(phy_ac_chanmgr_info_t *ac_info)
{
	phy_info_t *pi;
	phy_chanmgr_info_t *cmn_info;

	if (ac_info == NULL)
		return;

	pi = ac_info->pi;
	cmn_info = ac_info->cmn_info;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* unregister from common */
	phy_chanmgr_unregister_impl(cmn_info);

	if (pi->paprrmcsgamma5g80 != NULL) {
			phy_mfree(pi, pi->paprrmcsgamma5g80, (sizeof(*pi->paprrmcsgamma5g80) *
			NUM_MCS_PAPRR_GAMMA));
	}
	if (pi->paprrmcsgamma5g40 != NULL) {
			phy_mfree(pi, pi->paprrmcsgamma5g40, (sizeof(*pi->paprrmcsgamma5g40) *
			NUM_MCS_PAPRR_GAMMA));
	}
	if (pi->paprrmcsgamma5g20 != NULL) {
			phy_mfree(pi, pi->paprrmcsgamma5g20, (sizeof(*pi->paprrmcsgamma5g20) *
			NUM_MCS_PAPRR_GAMMA));
	}
	if (pi->paprrmcsgamma2g != NULL) {
			phy_mfree(pi, pi->paprrmcsgamma2g, (sizeof(*pi->paprrmcsgamma2g) *
			NUM_MCS_PAPRR_GAMMA));
	}
	if (pi->paprrmcsgain5g80 != NULL) {
			phy_mfree(pi, pi->paprrmcsgain5g80, (sizeof(*pi->paprrmcsgain5g80) *
			NUM_MCS_PAPRR_GAMMA));
	}
	if (pi->paprrmcsgain5g40 != NULL) {
			phy_mfree(pi, pi->paprrmcsgain5g40, (sizeof(*pi->paprrmcsgain5g40) *
			NUM_MCS_PAPRR_GAMMA));
	}
	if (pi->paprrmcsgain5g20 != NULL) {
			phy_mfree(pi, pi->paprrmcsgain5g20, (sizeof(*pi->paprrmcsgain5g20) *
			NUM_MCS_PAPRR_GAMMA));
	}
	if (pi->paprrmcsgain2g != NULL) {
			phy_mfree(pi, pi->paprrmcsgain2g, (sizeof(*pi->paprrmcsgain2g) *
			NUM_MCS_PAPRR_GAMMA));
	}

	phy_mfree(pi, ac_info, sizeof(phy_ac_chanmgr_info_t));
}

static void
BCMATTACHFN(phy_ac_chanmgr_std_params_attach)(phy_ac_chanmgr_info_t *chanmgri)
{
	phy_info_t *pi = chanmgri->pi;
	chanmgri->data.curr_band2g = CHSPEC_IS2G(chanmgri->pi->radio_chanspec);
	chanmgri->aci->curr_bw = CHSPEC_BW(chanmgri->pi->radio_chanspec);
	chanmgri->aci->ulp_tx_mode = 1;
	chanmgri->data.fast_adc_en = 0;
	chanmgri->use_fast_adc_20_40 = 0;
	chanmgri->mutx_war_on = FALSE;
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		chanmgri->aci->ulp_adc_mode = (uint8)PHY_GETINTVAR_DEFAULT(pi, rstr_ulpadc, 1);
	} else {
		chanmgri->aci->ulp_adc_mode = 0;
	}
	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_3(pi->pubpi->phy_rev)) {
		if (chanmgri->chsm_en) {
			chanmgri->acphy_enable_smth = SMTH_ENABLE;
		} else {
			chanmgri->acphy_enable_smth = SMTH_DISABLE;
		}
	} else if ((ACMAJORREV_32(pi->pubpi->phy_rev) ||
	            ACMAJORREV_33(pi->pubpi->phy_rev) ||
	            ACMAJORREV_37(pi->pubpi->phy_rev))) {
		chanmgri->acphy_enable_smth = SMTH_DISABLE;

		if (ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			/* channel smoothing is not supported in 80p80 */
			if (!PHY_AS_80P80(pi, pi->radio_chanspec))
				chanmgri->acphy_enable_smth = SMTH_ENABLE;
		}
	}
	chanmgri->acphy_smth_dump_mode = SMTH_NODUMP;
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		/* 4349A0 uses 12Ghz VCO */
		chanmgri->vco_12GHz = TRUE;
	} else {
		/* Disable 12Ghz VCO for all other chips */
		chanmgri->vco_12GHz = FALSE;
	}
}

static int
WLBANDINITFN(phy_ac_chanmgr_init_chanspec)(phy_type_chanmgr_ctx_t *ctx)
{
	phy_ac_chanmgr_info_t *chanmgri = (phy_ac_chanmgr_info_t *)ctx;
	phy_info_acphy_t *pi_ac = chanmgri->aci;
	phy_info_t *pi = chanmgri->pi;

	pi_ac->both_txchain_rxchain_eq_1 = FALSE;

	/* indicate chanspec control flow to follow init path */
	mboolset(pi_ac->CCTrace, CALLED_ON_INIT);
	wlc_phy_chanspec_set_acphy(pi, pi->radio_chanspec);
	mboolclr(pi_ac->CCTrace, CALLED_ON_INIT);
	chanmgri->data.init_done = TRUE;

	if (!(ACMAJORREV_4(pi->pubpi->phy_rev))) {
		if (TINY_RADIO(pi)) {
			phy_ac_rfseq_mode_set(pi, 1);
		}
		wlc_phy_txpwrctrl_idle_tssi_meas_acphy(pi);
		if (TINY_RADIO(pi)) {
			phy_ac_rfseq_mode_set(pi, 0);
		}
	}

	return BCME_OK;
}

/* Internal data api between ac modules */
phy_ac_chanmgr_data_t *
phy_ac_chanmgr_get_data(phy_ac_chanmgr_info_t *ci)
{
	return &ci->data;
}

#ifdef WL_PROXDETECT
void
phy_ac_chanmgr_save_smoothing(phy_ac_chanmgr_info_t *ci, uint8 *enable, uint8 *dump_mode)
{
	*enable = ci->acphy_enable_smth;
	*dump_mode = ci->acphy_smth_dump_mode;
}
#endif /* WL_PROXDETECT */
/* ********************************************* */
/*				Internal Definitions					*/
/* ********************************************* */
#define TXMAC_IFHOLDOFF_DEFAULT		0x12	/* 9.0us */
#define TXMAC_MACDELAY_DEFAULT		0x2a8	/* 8.5us */
#define TXDELAY_BW80		3	/* 3.0us */

#define TXMAC_IFHOLDOFF_43012A0		0x1a	/* 13.0us */
#define TXMAC_MACDELAY_43012A0		0x3e8	/* 12.5us */

#define ACPHY_VCO_2P5V	1
#define ACPHY_VCO_1P35V	0

#define WLC_TINY_GI_MULT_P12		4096U
#define WLC_TINY_GI_MULT_TWEAK_P12	4096U
#define WLC_TINY_GI_MULT			WLC_TINY_GI_MULT_P12

#define MAX_VALID_EDTHRESH		(-15) /* max level of valid rssi */


typedef struct _chan_info_common {
	uint16 chan;		/* channel number */
	uint16 freq;		/* in Mhz */
} chan_info_common_t;

static const uint16 qt_rfseq_val1[] = {0x8b5, 0x8b5, 0x8b5};
static const uint16 qt_rfseq_val2[] = {0x0, 0x0, 0x0};
static const uint16 rfseq_reset2rx_dly[] = {12, 2, 2, 4, 4, 6, 1, 4, 1, 2, 1, 1, 1, 1, 1, 1};
static const uint16 rfseq_updl_lpf_hpc_ml[] = {0x0aaa, 0x0aaa};
static const uint16 rfseq_updl_tia_hpc_ml[] = {0x0222, 0x0222};
static const uint16 rfseq_reset2rx_cmd[] = {0x4, 0x3, 0x6, 0x5, 0x2, 0x1, 0x8,
            0x2a, 0x2b, 0xf, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};

uint16 const rfseq_rx2tx_cmd[] =
{0x0, 0x1, 0x2, 0x8, 0x5, 0x0, 0x6, 0x3, 0xf, 0x4, 0x0, 0x35, 0xf, 0x0, 0x36, 0x1f};
static uint16 rfseq_rx2tx_dly_epa1_20[] =
	{0x8, 0x6, 0x4, 0x4, 0x6, 0x2, 0x10, 60, 0x2, 0x5, 0x1, 0x4, 0xe4, 0xfa, 0x2, 0x1};
static uint16 rfseq_rx2tx_dly_epa1_40[] =
	{0x8, 0x6, 0x4, 0x4, 0x6, 0x2, 0x10, 30, 0x2, 0xd, 0x1, 0x4, 0xfa, 0xfa, 0x2, 0x1};
static uint16 rfseq_rx2tx_dly_epa1_80[] =
	{0x8, 0x6, 0x4, 0x4, 0x6, 0x2, 0x10, 20, 0x2, 0x17, 0x1, 0x4, 0xfa, 0xfa, 0x2, 0x1};

static uint16 rfseq_rx2tx_cmd_noafeclkswitch[] =
        {0x0, 0x1, 0x5, 0x8, 0x2, 0x3d, 0x6, 0x3, 0xf, 0x4, 0x3e, 0x35, 0xf, 0x0, 0x36, 0x1f};
static uint16 rfseq_rx2tx_cmd_noafeclkswitch_dly[] =
        {0x8, 0x6, 0x6, 0x4, 0x4, 0x2, 0x10, 0x26, 0x2, 0x5, 0x1, 0x4, 0xfa, 0xfa, 0x2, 0x1};
static uint16 rfseq_rx2tx_cmd_afeclkswitch[] =
        {0x0, 0x3d, 0x3e, 0x1, 0x5, 0x8, 0x2, 0x6, 0x3, 0xf, 0x4, 0x35, 0xf, 0x0, 0x36, 0x1f};
static uint16 rfseq_rx2tx_cmd_afeclkswitch_dly[] =
        {0x2, 0x8, 0x1, 0xd, 0x6, 0x4, 0x4, 0x10, 0x24, 0x2, 0x5, 0x4, 0xfa, 0xfa, 0x2, 0x1};

static uint16 rfseq_tx2rx_cmd_noafeclkswitch[] =
  {0x00, 0x04, 0x03, 0x06, 0xb3, 0x02, 0x3d, 0x05, 0xb3, 0x01, 0x08, 0x2a, 0x3e, 0x0f, 0x2b, 0x1f};
static uint16 rfseq_tx2rx_dly_noafeclkswitch[] =
  {0x02, 0x01, 0x07, 0x04, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x0a, 0x0a, 0x04, 0x02, 0x01};

static uint16 rfseq_rx2tx_cmd_withtssisleep[] =
{0x0000, 0x0001, 0x0005, 0x0008, 0x0002, 0x0006, 0x0003, 0x000f, 0x0004, 0x0035,
0x000f, 0x0000, 0x0000, 0x0036, 0x0080, 0x001f};
static uint16 rfseq_rx2tx_dly_withtssisleep[] =
{0x0008, 0x0006, 0x0006, 0x0004, 0x0006, 0x0010, 0x0026, 0x0002, 0x0006, 0x0004,
0x00ff, 0x00ff, 0x00a8, 0x0004, 0x0001, 0x0001};
static uint16 rfseq_rx2tx_cmd_rev15_ipa[] =
        {0x0, 0x1, 0x5, 0x8, 0x2, 0x6, 0x35, 0x3, 0xf, 0x4, 0x0f, 0x0, 0x0, 0x36, 0x00, 0x1f};
static uint16 rfseq_rx2tx_cmd_rev15_ipa_withtssisleep[] =
        {0x0, 0x1, 0x5, 0x8, 0x2, 0x6, 0x35, 0x3, 0xf, 0x4, 0x0f, 0x0, 0x0, 0x36, 0x80, 0x1f};
static uint16 rfseq_rx2tx_dly_rev15_ipa20[] =
	{0x8, 0x6, 0x6, 0x4, 0x6, 0x10, 40, 0x26, 0x2, 0x6, 0xff, 0xff, 0x56, 0x4, 0x1, 0x1};
static uint16 rfseq_rx2tx_dly_rev15_ipa40[] =
	{0x8, 0x6, 0x6, 0x4, 0x6, 0x10, 16, 0x26, 0x2, 0x6, 0xff, 0xff, 0x6e, 0x4, 0x1, 0x1};

static const uint16 rfseq_tx2rx_cmd[] =
{0x4, 0x3, 0x6, 0x5, 0x0, 0x2, 0x1, 0x8, 0x2a, 0xf, 0x0, 0xf, 0x2b, 0x1f, 0x1f, 0x1f};
static uint16 rfseq_tx2rx_dly[] =
  {0x08, 0x4, 0x2, 0x2, 0x1, 0x3, 0x4, 0x6, 0x04, 0xa, 0x4, 0x2, 0x01, 0x01, 0x01, 0x1};

static const uint16 rf_updh_cmd_clamp[] = {0x2a, 0x07, 0x0a, 0x00, 0x08, 0x2b, 0x1f, 0x1f};
static const uint16 rf_updh_dly_clamp[] = {0x01, 0x02, 0x02, 0x02, 0x10, 0x01, 0x01, 0x01};
static const uint16 rf_updl_cmd_clamp[] = {0x2a, 0x07, 0x08, 0x0c, 0x0e, 0x2b, 0x1f, 0x1f};
static const uint16 rf_updl_dly_clamp[] = {0x01, 0x06, 0x12, 0x08, 0x10, 0x01, 0x01, 0x01};
static const uint16 rf_updu_cmd_clamp[] = {0x2a, 0x07, 0x08, 0x0e, 0x2b, 0x1f, 0x1f, 0x1f};
static const uint16 rf_updu_dly_clamp[] = {0x01, 0x06, 0x1e, 0x1c, 0x01, 0x01, 0x01, 0x01};

static const uint16 rf_updh_cmd_adcrst[] = {0x07, 0x0a, 0x00, 0x08, 0xb0, 0xb1, 0x1f, 0x1f};
static uint16 rf_updh_dly_adcrst[] = {0x02, 0x02, 0x02, 0x01, 0x0a, 0x01, 0x01, 0x01};
static const uint16 rf_updl_cmd_adcrst[] = {0x07, 0x08, 0x0c, 0x0e, 0xb0, 0xb2, 0x1f, 0x1f};
static uint16 rf_updl_dly_adcrst[] = {0x06, 0x12, 0x08, 0x01, 0x0a, 0x01, 0x01, 0x01};
static const uint16 rf_updu_cmd_adcrst[] = {0x07, 0x08, 0x0e, 0xb0, 0xb1, 0x1f, 0x1f, 0x1f};
static const uint16 rf_updu_dly_adcrst[] = {0x06, 0x1e, 0x1c, 0x0a, 0x01, 0x01, 0x01, 0x01};
static const uint16 rf_updl_dly_dvga[] =  {0x01, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01};

static const uint8 avvmid_set[25][5][6] = {{{2, 1, 2,   107, 150, 110},  /* pdet_id = 0 */
			       {2, 2, 1,   157, 153, 160},
			       {2, 2, 1,   157, 153, 161},
			       {2, 2, 0,   157, 153, 186},
			       {2, 2, 0,   157, 153, 187}},
			       {{1, 0, 1,   159, 174, 161},  /* pdet_id = 1 */
			       {1, 0, 1,   160, 185, 156},
			       {1, 0, 1,   163, 185, 162},
			       {1, 0, 1,   169, 187, 167},
			       {1, 0, 1,   152, 188, 160}},
			       {{1, 1, 1,   159, 166, 166},  /* pdet_id = 2 */
			       {2, 2, 4,   140, 151, 100},
			       {2, 2, 3,   143, 153, 116},
			       {2, 2, 2,   143, 153, 140},
			       {2, 2, 2,   145, 160, 154}},
			       {{1, 1, 2,   130, 131, 106},  /* pdet_id = 3 */
			       {1, 1, 2,   130, 131, 106},
			       {1, 1, 2,   128, 127, 97},
			       {0, 1, 3,   159, 137, 75},
			       {0, 0, 3,   164, 162, 76}},
			       {{1, 1, 1,   156, 160, 158},  /* pdet_id = 4 */
			       {1, 1, 1,   156, 160, 158},
			       {1, 1, 1,   156, 160, 158},
			       {1, 1, 1,   156, 160, 158},
			       {1, 1, 1,   156, 160, 158}},
			       {{2, 2, 2,   104, 108, 106},  /* pdet_id = 5 */
			       {2, 2, 2,   104, 108, 106},
			       {2, 2, 2,   104, 108, 106},
			       {2, 2, 2,   104, 108, 106},
			       {2, 2, 2,   104, 108, 106}},
			       {{2, 0, 2,   102, 170, 104},  /* pdet_id = 6 */
			       {3, 4, 3,    82, 102,  82},
			       {1, 3, 1,   134, 122, 136},
			       {1, 3, 1,   134, 124, 136},
			       {2, 3, 2,   104, 122, 108}},
			       {{0, 0, 0,   180, 180, 180},  /* pdet_id = 7 */
			       {0, 0, 0,   180, 180, 180},
			       {0, 0, 0,   180, 180, 180},
			       {0, 0, 0,   180, 180, 180},
			       {0, 0, 0,   180, 180, 180}},
			       {{2, 1, 2,   102, 138, 104},  /* pdet_id = 8 */
			       {3, 5, 3,    82, 100,  82},
			       {1, 4, 1,   134, 116, 136},
			       {1, 3, 1,   134, 136, 136},
			       {2, 3, 2,   104, 136, 108}},
			       {{3, 2, 3,    90, 106,  86},  /* pdet_id = 9 */
			       {3, 1, 3,    90, 158,  90},
			       {2, 1, 2,   114, 158, 112},
			       {2, 1, 1,   116, 158, 142},
			       {2, 1, 1,   116, 158, 142}},
			       {{2, 2, 2,   152, 156, 156},  /* pdet_id = 10 */
			       {2, 2, 2,   152, 156, 156},
			       {2, 2, 2,   152, 156, 156},
			       {2, 2, 2,   152, 156, 156},
			       {2, 2, 2,   152, 156, 156}},
			       {{1, 1, 1,   134, 134, 134},  /* pdet_id = 11 */
			       {1, 1, 1,   136, 136, 136},
			       {1, 1, 1,   136, 136, 136},
			       {1, 1, 1,   136, 136, 136},
			       {1, 1, 1,   136, 136, 136}},
			       {{3, 3, 3,    90,  92,  86},  /* pdet_id = 12 */
			       {3, 3, 3,    90,  86,  90},
			       {2, 3, 2,   114,  86, 112},
			       {2, 2, 1,   116, 109, 142},
			       {2, 2, 1,   116, 110, 142}},
			       {{2, 2, 2,   112, 114, 112},  /* pdet_id = 13 */
			       {2, 2, 2,   114, 114, 114},
			       {2, 2, 2,   114, 114, 114},
			       {2, 2, 2,   113, 114, 112},
			       {2, 2, 2,   113, 114, 112}},
			       {{1, 1, 1,   134, 134, 134},  /* pdet_id = 14 */
			       {0, 0, 0,   168, 168, 168},
			       {0, 0, 0,   168, 168, 168},
			       {0, 0, 0,   168, 168, 168},
			       {0, 0, 0,   168, 168, 168}},
			       {{0, 0, 0,   172, 172, 172},  /* pdet_id = 15 */
			       {0, 0, 0,   168, 168, 168},
			       {0, 0, 0,   168, 168, 168},
			       {0, 0, 0,   168, 168, 168},
			       {0, 0, 0,   168, 168, 168}},
			       {{3, 2, 3,    90, 106,  86},  /* pdet_id = 16 */
			       {3, 0, 3,    90, 186,  90},
			       {2, 0, 2,   114, 186, 112},
			       {2, 0, 1,   116, 186, 142},
			       {2, 0, 1,   116, 186, 142}},
			       {{4, 4, 4,   50,  45,  50},  /* pdet_id = 17 */
			       {3, 3, 3,    82,  82, 82},
			       {3, 3, 3,    82,  82, 82},
			       {3, 3, 3,    82,  82, 82},
			       {3, 3, 3,    82,  82, 82}},
			       {{5, 5, 5,   61,  61,  61},  /* pdet_id = 18 */
			       {2, 2, 2,   122, 122, 122},
			       {2, 2, 2,   122, 122, 122},
			       {2, 2, 2,   122, 122, 122},
			       {2, 2, 2,   122, 122, 122}},
			       {{2, 2, 2,  152, 156, 156},  /* pdet_id = 19 */
			       {1, 1, 1,   165, 165, 165},
			       {1, 1, 1,   160, 160, 160},
			       {1, 1, 1,   152, 150, 160},
			       {1, 1, 1,   152, 150, 160}},
                       {{3, 3, 3,  108, 108, 108},  /* pdet_id = 20 */
			       {1, 1, 1,   160, 160, 160},
			       {1, 1, 1,   160, 160, 160},
			       {1, 1, 1,   160, 160, 160},
			       {1, 1, 1,   160, 160, 160}},
                       {{2, 2, 2,  110, 110, 110},  /* pdet_id = 21 */
			       {0, 0, 0,   168, 168, 168},
			       {0, 0, 0,   168, 168, 168},
			       {0, 0, 0,   168, 168, 168},
			       {0, 0, 0,   168, 168, 168}},
			       {{6, 6, 6,   40,  40,  40},  /* pdet_id = 22 */
			       {2, 2, 1,   115, 115, 142},
			       {1, 2, 1,   142, 115, 142},
			       {1, 1, 1,   142, 142, 142},
			       {1, 1, 1,   142, 142, 142}},
			       {{1, 1, 1,  156, 160, 158},  /* pdet_id = 23 */
			       {6, 6, 6,    47,  45,  48},
			       {1, 1, 1,   147, 146, 148},
			       {1, 1, 1,   146, 146, 152},
			       {1, 1, 1,   146, 146, 152}},
			       {{2, 2, 2,   120, 120, 120}, /* pdet_id =24 */
			       {2, 2, 2,   120, 120, 120},
			       {2, 2, 2,   120, 120, 120},
			       {2, 2, 2,   120, 120, 120},
			       {2, 2, 2,   120, 120, 120}}
};

static const uint8 avvmid_set1[16][5][2] = {
	{{1, 154}, {0, 168}, {0, 168}, {0, 168}, {0, 168}},  /* pdet_id = 0 */
	{{1, 145}, {1, 145}, {1, 145}, {1, 145}, {1, 145}},  /* pdet_id = 1 WLBGA */
	{{6,  76}, {1, 160}, {6,  76}, {6,  76}, {6,  76}},  /* pdet_id = 2 */
	{{1, 156}, {1, 152}, {1, 152}, {1, 152}, {1, 152}},  /* pdet_id = 3 */
	{{1, 152}, {1, 152}, {1, 152}, {1, 152}, {1, 152}},  /* pdet_id = 4 WLCSP */
	{{3, 100}, {3,  75}, {3,  75}, {3,  75}, {3,  75}},  /* pdet_id = 5 WLCSP TM */
	{{1, 152}, {0, 166}, {0, 166}, {0, 166}, {0, 166}},  /* pdet_id = 6 WLCSP HK */
	{{1, 145}, {3, 120}, {3, 120}, {3, 120}, {3, 125}},  /* pdet_id = 7 WLiPA */
	{{1, 145}, {1, 155}, {1, 155}, {1, 155}, {1, 155}},  /* pdet_id = 8 WLBGA C0 */
	{{1, 135}, {1, 165}, {1, 165}, {1, 165}, {1, 165}}   /* pdet_id = 9 WLBGA RR FEM */
};
static const uint8 avvmid_set2[16][5][4] = {
	{
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{1, 1, 145, 145}},  /* pdet_id = 0 */
	{
		{3, 3, 100, 100},
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{1, 1, 145, 145}},  /* pdet_id = 1 */
	{
		{4, 4,  95,  95},
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{1, 1, 145, 145}},  /* pdet_id = 2 */
	{
		{1, 1, 145, 145},
		{3, 3,  90,  90},
		{3, 3,  92,  92},
		{2, 3, 110,  90},
		{2, 3, 110,  93}},  /* pdet_id = 3 */
	{
		{2, 2, 140, 140},
		{2, 2, 145, 145},
		{2, 2, 145, 145},
		{2, 2, 145, 145},
		{2, 2, 145, 145}},  /* pdet_id = 4 */
	{
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{1, 1, 145, 145},
		{2, 2, 110, 110}},  /* pdet_id = 5 */
	{
		{3, 3, 98, 98},
		{2, 1, 122, 150},
		{2, 1, 122, 150},
		{2, 2, 122, 122},
		{2, 2, 122, 122}}  /* pdet_id = 6 */
};

static const uint8 avvmid_set3[16][5][2] = {
	{{1, 115}, {2, 90}, {2, 90}, {2, 90}, {2, 90}},  /* pdet_id = 0 4345 TC */
	{{0, 131}, {0, 134}, {0, 134}, {0, 134}, {0, 134}},  /* pdet_id = 1 4345TC FCBGA EPA */
	{{4, 132}, {4, 127}, {4, 127}, {4, 127}, {4, 127}},  /* pdet_id = 2 4345A0 fcbusol */
	{{0, 150}, {2, 97}, {2, 97}, {2, 97}, {2, 97}},  /* pdet_id = 3 4345A0 fcpagb ipa */
};
static const uint8 avvmid_set4[1][5][4] = {
	{
		{2, 2, 130, 130},
		{2, 2, 130, 130},
		{2, 2, 130, 130},
		{2, 2, 130, 130},
		{2, 2, 130, 130}},  /* pdet_id = 0 */
};

static const uint8 avvmid_set32[6][5][8] = {
	{
		{1, 1, 1, 1, 148, 148, 148, 148},
		{1, 1, 1, 1, 148, 148, 148, 148},
		{1, 1, 1, 1, 148, 148, 148, 148},
		{1, 1, 1, 1, 148, 148, 148, 148},
		{1, 1, 1, 1, 148, 148, 148, 148}},  /* pdet_id = 0 */
	{
		{1, 1, 1, 1, 148, 148, 148, 148},
		{1, 1, 1, 1, 148, 148, 148, 148},
		{1, 1, 1, 1, 148, 148, 148, 148},
		{1, 1, 1, 1, 148, 148, 148, 148},
		{1, 1, 1, 1, 148, 148, 148, 148}},  /* pdet_id = 1 */
	{
		{2, 2, 2, 2, 110, 110, 110, 110},
		{2, 2, 2, 2, 116, 116, 116, 116},
		{2, 2, 2, 2, 116, 116, 116, 116},
		{2, 2, 2, 2, 116, 116, 116, 116},
		{2, 2, 2, 2, 116, 116, 116, 116}},  /* pdet_id = 2 */
	{
		{1, 1, 1, 1, 154, 154, 154, 156},
		{2, 2, 2, 2, 136, 136, 136, 136},
		{2, 2, 2, 2, 136, 136, 136, 136},
		{2, 2, 2, 2, 136, 136, 136, 136},
		{2, 2, 2, 2, 136, 136, 136, 136}},  /* pdet_id = 3 */
	{
		{2, 2, 2, 2, 128, 128, 128, 128},
		{2, 2, 2, 2, 128, 128, 128, 128},
		{2, 2, 2, 2, 128, 128, 128, 128},
		{2, 2, 2, 2, 128, 128, 128, 128},
		{2, 2, 2, 2, 128, 128, 128, 128}},  /* pdet_id = 4 */
	{
		{2, 2, 2, 2, 110, 110, 110, 110},
		{2, 2, 2, 2, 116, 116, 116, 116},
		{2, 2, 2, 2, 116, 116, 116, 116},
		{2, 2, 2, 2, 116, 116, 116, 116},
		{2, 2, 2, 2, 116, 116, 116, 116}},  /* pdet_id = 5 */
};

static const uint32 rfseqext_2g_rev40[11][2] = {
	{0x006A, 0x39FE8843}, {0x002A, 0x18F68423}, {0x000A, 0x18EE8003},
	{0x0028, 0x19FE8043}, {0x002A, 0x18F68023}, {0x0028, 0x18EE8443},
	{0x006A, 0x39FC8843}, {0x002A, 0x18F48423}, {0x000A, 0x18EC8003},
	{0x0000, 0x00000000}, {0x0000, 0x00000000}};
static const uint32 rfseqext_5g_rev40[11][2] = {
	{0x016A, 0x39FE8843}, {0x012A, 0x18F68423}, {0x010A, 0x18EE8003},
	{0x012A, 0x19FE8043}, {0x012A, 0x18F68023}, {0x012A, 0x19FE8443},
	{0x016A, 0x39FC8843}, {0x012A, 0x18F48423}, {0x010A, 0x18EC8003},
	{0x0000, 0x00000000}, {0x0000, 0x00000000}};

#define NUM_SUBBANDS_AV_VMID		(5)
#define NUM_VALUES_AV_VMID			(2)
#define MAX_NUM_OF_CORES_AV_VMID	(3)

static uint8
avvmid_set_maj36[][NUM_SUBBANDS_AV_VMID][MAX_NUM_OF_CORES_AV_VMID * NUM_VALUES_AV_VMID] = {
	{
	/* pdet_id = 0 */
		{5, 0, 0, 100, 0, 0},
		{3, 0, 0, 141, 0, 0},
		{3, 0, 0, 141, 0, 0},
		{3, 0, 0, 141, 0, 0},
		{3, 0, 0, 141, 0, 0}},
};
static uint8
avvmid_set_maj40[][NUM_SUBBANDS_AV_VMID][MAX_NUM_OF_CORES_AV_VMID * NUM_VALUES_AV_VMID] = {
	{
	/* pdet_id = 0 */
		{4, 4, 0, 125, 125, 0},
		{4, 4, 0, 125, 125, 0},
		{4, 4, 0, 125, 125, 0},
		{4, 4, 0, 125, 125, 0},
		{4, 4, 0, 125, 125, 0}},
};

uint8 *BCMRAMFN(get_avvmid_set_36)(phy_info_t *pi, uint16 pdet_range_id, uint16 subband_id);

uint8 *BCMRAMFN(get_avvmid_set_40)(phy_info_t *pi, uint16 pdet_range_id, uint16 subband_id);

uint16 const rfseq_majrev3_reset2rx_dly[] = {12, 2, 2, 4, 4, 6, 1, 4, 1, 2, 1, 1, 1, 1, 1, 1};

uint16 const rfseq_rx2tx_dly[] =
	{0x8, 0x6, 0x6, 0x4, 0x4, 0x2, 0x10, 0x26, 0x2, 0x5, 0x1, 0x4, 0xfa, 0xfa, 0x2, 0x1};
uint16 const tiny_rfseq_rx2tx_cmd[] =
	{0x42, 0x1, 0x2, 0x8, 0x5, 0x6, 0x3, 0xf, 0x4, 0x35, 0xf, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
uint16 const tiny_rfseq_rx2tx_dly[] =
	{0x8, 0x6, 0x6, 0x4, 0x4, 0x10, 0x26, 0x2, 0x5, 0x4, 0xFA, 0xFA, 0x1, 0x1, 0x1, 0x1};
uint16 const tiny_rfseq_rx2tx_tssi_sleep_cmd[] =
	{0x42, 0x1, 0x2, 0x8, 0x5, 0x6, 0x3, 0xf, 0x4, 0x35, 0xf, 0x00, 0x00, 0x36, 0x1f, 0x1f};
uint16 const tiny_rfseq_rx2tx_tssi_sleep_dly[] =
	{0x8, 0x6, 0x6, 0x4, 0x4, 0x10, 0x26, 0x2, 0x5, 0x4, 0xFA, 0xFA, 0x88, 0x1, 0x1, 0x1};
uint16 const tiny_rfseq_tx2rx_cmd[] =
	{0x4, 0x3, 0x6, 0x5, 0x85, 0x2, 0x1, 0x8, 0x2a, 0xf, 0x0, 0xf, 0x2b, 0x43, 0x1F};
static uint16 tiny_rfseq_tx2rx_dly[] =
	{0x8, 0x4, 0x2, 0x2, 0x1, 0x3, 0x4, 0x6, 0x4, 0x1, 0x2, 0x1, 0x1, 0x1, 0x1, 0x1};

/* tiny major rev4 RF Sequences : START */

/* Reset2Rx */
/* changing RF sequencer to add DCC reset */
static const uint16 rfseq_majrev4_reset2rx_cmd[] = {0x84, 0x4, 0x3, 0x6, 0x5, 0x2, 0x1, 0x8,
	0x2a, 0x2b, 0xf, 0x0, 0x0, 0x85, 0x41, 0x1f};
uint16 const rfseq_majrev4_reset2rx_dly[] = {10, 12, 2, 2, 4, 4, 6, 1, 4, 1, 2, 10, 1, 1, 1, 1};

/* Tx2Rx */
uint16 const rfseq_majrev4_tx2rx_cmd[] =
	{0x84, 0x4, 0x3, 0x6, 0x5, 0x85, 0x2, 0x1, 0x8, 0x2a, 0xf, 0x0, 0xf, 0x2b, 0x43, 0x1F};
uint16 const rfseq_majrev4_tx2rx_dly[] =
	{0x8, 0x8, 0x4, 0x2, 0x2, 0x1, 0x3, 0x4, 0x6, 0x4, 0x1, 0x2, 0x1, 0x1, 0x1, 0x1, 0x1};

/* Rx2Tx */
/* Refer to tiny_rfseq_rx2tx_cmd */

/* Rx2Tx -- Cal */
uint16 const rfseq_majrev4_rx2tx_cal_cmd[] =
	{0x84, 0x1, 0x2, 0x8, 0x5, 0x3d, 0x85, 0x6, 0x3, 0xf, 0x4, 0x3e, 0x35, 0xf, 0x36, 0x1f};
uint16 const rfseq_majrev4_rx2tx_cal_dly[] =
	{0x8, 0x6, 0x6, 0x4, 0x4, 0x2, 0x12, 0x10, 0x26, 0x2, 0x5, 0x1, 0x4, 0xfa, 0x2, 0x1};
/* tiny major rev4 RF Sequences : END */

// rev32 rfseq
uint16 const rfseq_majrev32_rx2tx_cal_cmd[] =
	{0x84, 0x1, 0x2, 0x8, 0x5, 0x0, 0x85, 0x6, 0x3, 0xf, 0x4, 0x0, 0x35, 0xf, 0x36, 0x1f};
uint16 const rfseq_majrev32_rx2tx_cal_dly[] =
	{0x8, 0x6, 0x6, 0x4, 0x4, 0x2, 0x12, 0x10, 0x26, 0x17, 0x5, 0x1, 0x4, 0xfa, 0x2, 0x1};
uint16 const rfseq_majrev32_rx2tx_cmd[]    = {0x42, 0x01, 0x02, 0x08, 0x05, 0x06, 0x03, 0x0f,
	0x04, 0x35, 0x0f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
uint16 const rfseq_majrev32_tx2rx_cmd[]    = {0x84, 0x04, 0x03, 0x06, 0x05, 0x85, 0x02, 0x01,
	0x08, 0x2a, 0x0f, 0x00, 0x0f, 0x2b, 0x43, 0x1F};
uint16 const rfseq_majrev32_reset2rx_cmd[] = {0x0, 0x04, 0x03, 0x06, 0x05, 0x02, 0x01, 0x08,
	0x2a, 0x2b, 0x0f, 0x00, 0x00, 0x0, 0x1f, 0x1f};
// change PA PU delay to 0x17 from 0x2 to resolve the LOFT issues
uint16 const rfseq_majrev32_rx2tx_dly[]    = {0x08, 0x06, 0x06, 0x04, 0x04, 0x10, 0x26, 0x17,
	0x05, 0x04, 0xFA, 0xFA, 0x01, 0x01, 0x01, 0x01};
uint16 const rfseq_majrev32_tx2rx_dly[]    = {0x08, 0x08, 0x04, 0x02, 0x02, 0x01, 0x03, 0x04,
	0x06, 0x04, 0x01, 0x02, 0x01, 0x01, 0x01, 0x01};
uint16 const rfseq_majrev32_reset2rx_dly[] = {0x0A, 0x0C, 0x02, 0x02, 0x04, 0x04, 0x06, 0x01,
	0x04, 0x01, 0x02, 0x0A, 0x01, 0x01, 0x01, 0x01};

/* Major rev36 RF Sequences : START */
/* Reset2Rx */
static const uint16 rfseq_majrev36_reset2rx_cmd[] =
	{0x0, 0x4, 0x3, 0x6, 0x5, 0x2, 0x1, 0x8, 0x0, 0x0, 0xf, 0x3d, 0x3e, 0x0, 0xbf, 0x1f};

static const uint16 rfseq_majrev36_reset2rx_dly[] =
	{0xA, 0xC, 0x2, 0x2, 0x4, 0x4, 0x6, 0x1, 0x4, 0x1, 0x2, 0xA, 0x1, 0x1, 0x01, 0x01};

/* Tx2Rx */
static const uint16 rfseq_majrev36_tx2rx_cmd[] =
	{0x85, 0x4, 0x3, 0x6, 0x5, 0x3d, 0x3e, 0x2, 0x1, 0x8, 0xbf, 0xf, 0xf, 0x0, 0x0, 0x1F};

static const uint16 rfseq_majrev36_tx2rx_dly[] =
	{0x8, 0x8, 0x4, 0x2, 0x2, 0x2, 0x2, 0x3, 0x4, 0x6, 0x4, 0x1, 0x1, 0x1, 0x1, 0x1};

/* Rx2Tx */
const uint16 rfseq_majrev36_rx2tx_cmd[] =
   {0x85, 0x1,  0x2, 0x8, 0x5, 0x3d, 0x3e, 0x6, 0x3, 0xf, 0x4, 0x35, 0xf, 0xbe, 0x1f, 0x1f};

const uint16 rfseq_majrev36_rx2tx_dly[] =
	{0x8, 0x6, 0x6, 0x4, 0x4, 0x2, 0x4, 0xe, 0x26, 0x2, 0x5, 0x4, 0xFA, 0xFA, 0x1, 0x1};

/* Rx2Tx -- Cal */
const uint16 rfseq_majrev36_rx2tx_cal_cmd[] =
	{0x84, 0x1, 0x2, 0x8, 0x5, 0x3d, 0x3e, 0x6, 0x3, 0xf, 0x4, 0x85, 0x35, 0xf, 0x0, 0x1f};
const uint16 rfseq_majrev36_rx2tx_cal_dly[] =
	{0x8, 0x6, 0x6, 0x4, 0x4, 0x2, 0x12, 0x10, 0x26, 0x2, 0x5, 0x4, 0x4, 0xfa, 0x2, 0x1};

/* Rx2Tx -- TSSI sleep */
const uint16 rfseq_majrev36_rx2tx_tssi_sleep_cmd[] =
	{0x85, 0x1, 0x2, 0x5, 0x3d, 0x3e, 0x6, 0x3, 0xf, 0x4, 0x35, 0xf, 0x0, 0x86, 0x36, 0x1f};
const uint16 rfseq_majrev36_rx2tx_tssi_sleep_dly[] =
	{0x2, 0x6, 0xa, 0x4, 0x2, 0x4, 0xe, 0x26, 0x2, 0x5, 0x4, 0xfa, 0xfa, 0x88, 0x1, 0x1};
/* Major rev36 RF Sequences : END */

/* Major rev37 RF Sequences : START */
/* Reset2Rx */
static const uint16 rfseq_majrev37_reset2rx_cmd[] =
	{0x4, 0x3, 0x6, 0x2, 0x5, 0x1, 0x8, 0x2a, 0x2b, 0xf, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
static const uint16 rfseq_majrev37_reset2rx_dly[] =
	{0xc, 0x2, 0x2, 0x4, 0x4, 0x6, 0x1, 0x4, 0x1, 0x2, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1};
/* Tx2Rx */
static const uint16 rfseq_majrev37_tx2rx_cmd[] =
	{0x4, 0x3, 0x6, 0x5, 0x3d, 0x2, 0x1, 0x8, 0x2a, 0xf, 0x3e, 0xf, 0x2b, 0x1f, 0x1f, 0x1f};
static const uint16 rfseq_majrev37_tx2rx_dly[] =
	{0x8, 0x4, 0x2, 0x2, 0x1, 0x3, 0x4, 0x6, 0x4, 0xa, 0x4, 0x2, 0x1, 0x1, 0x1, 0x1};
/* Rx2Tx */
static const uint16 rfseq_majrev37_rx2tx_cmd[] =
	{0x0, 0x1, 0x2, 0x8, 0x5, 0x3d, 0x6, 0x3, 0xf, 0x4, 0x3e, 0x35, 0xf, 0x0, 0x36, 0x1f};
static const uint16 rfseq_majrev37_rx2tx_dly[] =
	{0x8, 0x6, 0x6, 0x4, 0x4, 0x2, 0x10, 0x26, 0x2, 0x5, 0x1, 0x4, 0xfa, 0xfa, 0x2, 0x1};
/* Major rev37 RF Sequences : END */

/* Major rev40 RF Sequences : START */
/* Reset2Rx */
static const uint16 rfseq_majrev40_reset2rx_cmd[] =
	{0x4, 0x3, 0x6, 0x2, 0x5, 0x1, 0x8, 0x2a, 0x2b, 0xf, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f, 0x1f};
static const uint16 rfseq_majrev40_reset2rx_dly[] =
	{0xc, 0x2, 0x2, 0x4, 0x4, 0x6, 0x1, 0x4, 0x1, 0x2, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1};
/* Tx2Rx */
static const uint16 rfseq_majrev40_tx2rx_cmd[] =
	{0x4, 0x3, 0x6, 0x5, 0x3d, 0x2, 0x1, 0x8, 0x2a, 0xf, 0x3e, 0xf, 0x2b, 0x1f, 0x1f, 0x1f};
static const uint16 rfseq_majrev40_tx2rx_dly[] =
	{0x8, 0x4, 0x2, 0x2, 0x1, 0x3, 0x4, 0x6, 0x4, 0xa, 0x4, 0x2, 0x1, 0x1, 0x1, 0x1};
/* Rx2Tx */
static const uint16 rfseq_majrev40_rx2tx_cmd[] =
	{0x0, 0x1, 0x2, 0x8, 0x5, 0x3d, 0x6, 0x3, 0xf, 0x4, 0x3e, 0x35, 0xf, 0x0, 0x36, 0x1f};
static const uint16 rfseq_majrev40_rx2tx_dly[] =
	{0x8, 0x6, 0x6, 0x4, 0x4, 0x2, 0x10, 0x26, 0x2, 0x5, 0x1, 0x4, 0xfa, 0xfa, 0x2, 0x1};
/* Major rev40 RF Sequences : END */

/* Channel smoothing MTE filter image */
#define CHANSMTH_FLTR_LENGTH 64
static CONST uint16 acphy_Smth_tbl_4349[] = {
	0x4a5c, 0xdba7, 0x1672,
	0xb167, 0x742d, 0xa5ca,
	0x4afe, 0x4aa6, 0x14f3,
	0x4176, 0x6f25, 0xa75a,
	0x7aca, 0xeca4, 0x1e94,
	0xf177, 0x4e27, 0xa7fa,
	0x0b46, 0xcead, 0x270c,
	0x3169, 0x4f1d, 0xa70b,
	0xda4e, 0xcb35, 0x1431,
	0xd1d2, 0x572e, 0xae6b,
	0x8a4b, 0x68bc, 0x1f62,
	0x81f6, 0xc826, 0xa4bb,
	0x2add, 0x6b37, 0x1d42,
	0xcaff, 0xdd9e, 0x0c6a,
	0xd0c6, 0xecad, 0xaff9,
	0xbad8, 0xe69d, 0x173a,
	0x20d1, 0xf5b7, 0xa579,
	0x6b71, 0xdb9c, 0x156a,
	0x60d6, 0xf345, 0xa6f9,
	0x0b42, 0xc6a6, 0x1f5a,
	0xb0d4, 0xe22e, 0x9c19,
	0x4bc4, 0x5aaf, 0x1c6b,
	0xc0cc, 0xc326, 0x9c49,
	0x1cf1, 0xddb7, 0x243b,
	0xe17a, 0xe21c, 0xa75a,
	0x6a50, 0xcb35, 0x1441,
	0xb1d3, 0x5d2e, 0xae4b,
	0x8a4b, 0x67bd, 0x1f72,
	0x71f7, 0xd826, 0xa4bb,
	0xfade, 0x6b36, 0x1d42,
	0xe153, 0xcf96, 0x0fc8,
	0xf0fc, 0x6e8c, 0x1539,
	0xd1fd, 0x7d94, 0x0da9,
	0xd047, 0xc08c, 0x1578,
	0x41c9, 0x4c9d, 0x1679,
	0xe043, 0x7696, 0x1459,
	0xf2f7, 0x7faf, 0x1d1a,
	0xd0e4, 0x4c9c, 0x1c49,
	0xe37e, 0xca9c, 0x1782,
	0x31ff, 0x7ba4, 0x2f1a,
	0xd243, 0xe69d, 0x16ba,
	0x616b, 0xddae, 0x2439,
	0xdc69, 0x46ae, 0x1fb2,
	0xf0c9, 0x5a97, 0x0658,
	0xa065, 0x7f85, 0x0c99,
	0xd174, 0x4a95, 0x0508,
	0x2074, 0xce86, 0x0d38,
	0xb152, 0xea9f, 0x0f08,
	0xd078, 0xd785, 0x0d38,
	0x71e3, 0xc29c, 0x0c48,
	0xc06e, 0xd684, 0x0c88,
	0x4262, 0x42a4, 0x1439,
	0x0058, 0xc78e, 0x1658,
	0xb2c4, 0x5cb5, 0x25da,
	0x60f5, 0x5694, 0x1dd9,
	0x02c6, 0xc39d, 0x1792,
	0x61ff, 0x7ba4, 0x2f3a,
	0xf246, 0xee9d, 0x16ca,
	0xe16c, 0xdfae, 0x2469,
	0x2c4d, 0x44af, 0x1fd2,
	0x0bcd, 0x4faf, 0x1c5b,
	0x30cb, 0x7e27, 0x9c6a,
	0xec42, 0xd3b6, 0x243b,
	0x0179, 0xd81d, 0xa77a
 };
static CONST uint16 acphy_Smth_tbl_tiny[] = {
	0x5fd2,	0x16fc,	0x0ce0,
	0x60ce,	0xc501,	0xfd2f,
	0xefe0,	0x09fc,	0x09e0,
	0x90eb,	0xc802,	0xfc5f,
	0xcfed,	0x01fd,	0x0690,
	0xf0ed,	0xd903,	0xfc0f,
	0xcff7,	0xfefe,	0x037f,
	0x30d2,	0xf605,	0xfc7f,
	0xbfd8,	0x4b00,	0x0860,
	0xb052,	0xf501,	0xfe6f,
	0xbfda,	0x33ff,	0x0750,
	0x3075,	0xfb03,	0xfdaf,
	0xefe8,	0x3500,	0x0530,
	0x4fe0,	0xe8f9,	0x119f,
	0x8119,	0x94fe,	0xfe0f,
	0x5fea,	0xe6fa,	0x0e5f,
	0x1142,	0x8aff,	0xfd4f,
	0xaff1,	0xe9fb,	0x0acf,
	0x2156,	0x8d00,	0xfc8f,
	0xfff7,	0xeefc,	0x075f,
	0xa151,	0x9d01,	0xfbef,
	0x2ffb,	0xf4fe,	0x045f,
	0x612f,	0xbd03,	0xfbbf,
	0x1ffe,	0xfaff,	0x021f,
	0xe0f4,	0xe704,	0xfc5f,
	0xafd8,	0x4b00,	0x0880,
	0xa052,	0xf401,	0xfe7f,
	0xafda,	0x33ff,	0x0770,
	0x3077,	0xfa03,	0xfdaf,
	0xdfe8,	0x3500,	0x0540,
	0x30a5,	0xc5f2,	0x1f1e,
	0x51f1,	0x23ec,	0x0a5f,
	0x607c,	0x06f6,	0x167f,
	0xb236,	0xffec,	0x0ade,
	0xc049,	0x67fa,	0x0cdf,
	0x4214,	0x13f2,	0x089f,
	0x001d,	0xc0fe,	0x051f,
	0x2191,	0x68fb,	0x044f,
	0x100f,	0x12fb,	0x0ef0,
	0xe07f,	0xc2fd,	0x01cf,
	0x2021,	0xe6fa,	0x0d5f,
	0x60d5,	0xa2fe,	0x021f,
	0x4ffc,	0x22fe,	0x07b0,
	0x2125,	0x2cf0,	0x32bd,
	0xc32b,	0x02d2,	0x125f,
	0x50e8,	0xb0f4,	0x27ed,
	0xe3a0,	0xc7ce,	0x14be,
	0x40a3,	0x57f8,	0x1bee,
	0x43bd,	0xa3d1,	0x14ce,
	0x8062,	0xf9fb,	0x10ee,
	0x3370,	0xa7da,	0x11fe,
	0xe030,	0x7cfd,	0x085f,
	0x12c1,	0xe2e8,	0x0c9e,
	0x4010,	0xd1ff,	0x02ef,
	0x41d4,	0x54f7,	0x05df,
	0xf011,	0x10fa,	0x0f10,
	0xd07f,	0xc2fd,	0x01cf,
	0x1023,	0xe4fa,	0x0d7f,
	0x40d7,	0xa1fe,	0x023f,
	0x3ffd,	0x22fe,	0x07c0,
	0x3ffb,	0xf6fe,	0x044f,
	0x912a,	0xc103,	0xfbaf,
	0x2ffd,	0xfaff,	0x021f,
	0xf0f1,	0xe904,	0xfc4f,
};

/* China 40M Spur WAR */
static const uint16 resamp_cnwar_5270[] = {0x4bda, 0x0038, 0x10e0, 0x4bda, 0x0038, 0x10e0,
0xed0e, 0x0068, 0xed0e, 0x0068};
static const uint16 resamp_cnwar_5310[] = {0x0000, 0x00d8, 0x0b40, 0x0000, 0x00d8, 0x0b40,
0x6c79, 0x0045, 0x6c79, 0x0045};

/* TxEvmTbl of 43012 */
static const uint8 tx_evm_tbl_majrev36[] = {
	0x09, 0x0e, 0x11, 0x14, 0x17, 0x1a, 0x1d, 0x20, 0x09, 0x0e, 0x11, 0x14, 0x17, 0x1a,
	0x1d, 0x20, 0x22, 0x24, 0x09, 0x0e, 0x11, 0x14, 0x17, 0x1a, 0x1d, 0x20, 0x22, 0x24,
	0x09, 0x0e, 0x11, 0x14, 0x17, 0x1a, 0x1d, 0x20, 0x22, 0x24
};

/* NvAdjTbl of 43012 */
static const uint32 nv_adj_tbl_majrev36[] = {
	0x00000000, 0x00400844, 0x00300633, 0x00200422, 0x00100211, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000100, 0x00000200, 0x00000311, 0x00000422, 0x00100533, 0x00200644, 0x00300700,
	0x00400800, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00400800, 0x00300700, 0x00200644, 0x00100533, 0x00000422, 0x00000311,
	0x00000200, 0x00000100, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00100211, 0x00200422, 0x00300633,
	0x00400844,
};

/* PhaseTrackTbl_1x1 of 43012 */
static const uint32 phase_track_tbl_majrev36[] = {
	0x06af56cd, 0x059acc7b, 0x04ce6652, 0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819,
	0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819, 0x06af56cd, 0x059acc7b, 0x04ce6652,
	0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819,
	0x02b15819
};

/* scalarTable0 of 43012 */
static const uint32 scalar_table0_majrev36[] = {
	0x0b5e002d, 0x0ae2002f, 0x0a3b0032, 0x09a70035, 0x09220038, 0x08ab003b, 0x081f003f,
	0x07a20043, 0x07340047, 0x06d2004b, 0x067a004f, 0x06170054, 0x05bf0059, 0x0571005e,
	0x051e0064, 0x04d3006a, 0x04910070, 0x044c0077, 0x040f007e, 0x03d90085, 0x03a1008d,
	0x036f0095, 0x033d009e, 0x030b00a8, 0x02e000b2, 0x02b900bc, 0x029200c7, 0x026d00d3,
	0x024900e0, 0x022900ed, 0x020a00fb, 0x01ec010a, 0x01d20119, 0x01b7012a, 0x019e013c,
	0x0188014e, 0x01720162, 0x015d0177, 0x0149018e, 0x013701a5, 0x012601be, 0x011501d8,
	0x010601f4, 0x00f70212, 0x00e90231, 0x00dc0253, 0x00d00276, 0x00c4029b, 0x00b902c3,
	0x00af02ed, 0x00a50319, 0x009c0348, 0x0093037a, 0x008b03af, 0x008303e6, 0x007c0422,
	0x00750460, 0x006e04a3, 0x006804e9, 0x00620533, 0x005d0582, 0x005805d6, 0x0053062e,
	0x004e068c
};

/* sgiAdjustTbl of 43012 */
static const uint32 sgi_adjust_tbl_majrev36[] = {
	0x00100101, 0x10100111, 0x10110010, 0x11010111, 0x10022000, 0x01200021, 0x11202312,
	0x01110100, 0x10111010, 0x00010010, 0x11001010, 0x12111200, 0x21210111, 0x11410200,
	0x00101001, 0x10100001, 0x10011111, 0x11001011, 0x22222001, 0x01112212, 0x11112144,
	0x00110100, 0x01101101, 0x01100000, 0x10110100, 0x00020021, 0x20211022, 0x01103442,
	0x01011110, 0x01110100, 0x10111010, 0x11000000, 0x21000202, 0x21011112, 0x11001314,
	0x11001010, 0x10111110, 0x10010101, 0x02110011, 0x11101022, 0x24211201, 0x00011023,
	0x11100101, 0x10000100, 0x10010101, 0x22210110, 0x21112220, 0x13100222
};

/* 43012 tables to be written on reset */
static acphytbl_info_t tbls_write_on_reset_list_majrev36[] = {
	{tx_evm_tbl_majrev36, ARRAYSIZE(tx_evm_tbl_majrev36), ACPHY_TBL_ID_TXEVMTBL, 0, 8},
	{nv_adj_tbl_majrev36, ARRAYSIZE(nv_adj_tbl_majrev36), ACPHY_TBL_ID_NVADJTBL, 0, 32},
	{phase_track_tbl_majrev36, ARRAYSIZE(phase_track_tbl_majrev36),
	ACPHY_TBL_ID_PHASETRACKTBL_1X1, 0, 32},
	{scalar_table0_majrev36, ARRAYSIZE(scalar_table0_majrev36),
	ACPHY_TBL_ID_SCALARTABLE0, 0, 32},
	{sgi_adjust_tbl_majrev36, ARRAYSIZE(sgi_adjust_tbl_majrev36),
	ACPHY_TBL_ID_SGIADJUSTTBL, 0, 32},
	{NULL, 0, 0, 0, 0}
};

static acphytbl_info_t *BCMRAMFN(phy_ac_get_tbls_write_on_reset_list)(phy_info_t *pi);
static void wlc_phy_set_noise_var_shaping_acphy(phy_info_t *pi,
	uint8 noise_var[][ACPHY_SPURWAR_NV_NTONES],	int8 *tone_id, uint8 *core_nv);
static void phy_ac_chanmgr_papr_iir_filt_reprog(phy_info_t *pi);
static void phy_ac_chanmgr_preempt_postfilter_reg_tbl(phy_info_t *pi, bool enable);
static void chanspec_setup_papr(phy_info_t *pi,
	int8 papr_final_clipping, int8 papr_final_scaling);
static void wlc_phy_spurwar_nvshp_acphy(phy_info_t *pi, bool bw_chg,
	bool spurwar, bool nvshp);
static void wlc_phy_write_rx_farrow_acphy(phy_ac_chanmgr_info_t *ci, chanspec_t chanspec);

#ifndef WL_FDSS_DISABLED
static void wlc_phy_fdss_init(phy_info_t *pi);
static void wlc_phy_set_fdss_table(phy_info_t *pi);
#endif

extern void wlc_phy_ulb_feature_flag_set(wlc_phy_t *pih);
static void phy_ac_lp_enable(phy_info_t *pi);
static void wlc_acphy_load_4349_specific_tbls(phy_info_t *pi);
static void wlc_acphy_load_radiocrisscross_phyovr_mode(phy_info_t *pi);
static void wlc_acphy_load_logen_tbl(phy_info_t *pi);
static void wlc_phy_set_regtbl_on_band_change_acphy_20693(phy_info_t *pi);
static void wlc_phy_load_channel_smoothing_tiny(phy_info_t *pi);
static void wlc_phy_set_reg_on_reset_acphy_20693(phy_info_t *pi);
static void wlc_phy_set_tbl_on_reset_acphy(phy_info_t *pi);
static void wlc_phy_set_regtbl_on_band_change_acphy(phy_info_t *pi);
static void wlc_phy_set_regtbl_on_bw_change_acphy(phy_info_t *pi);
static void wlc_phy_set_sdadc_pd_val_per_core_acphy(phy_info_t *pi);
static void chanspec_setup_regtbl_on_chan_change(phy_info_t *pi);
static void wlc_phy_set_sfo_on_chan_change_acphy(phy_info_t *pi, uint8 ch);
static void wlc_phy_write_sfo_params_acphy(phy_info_t *pi, uint16 fc);
static void wlc_phy_write_sfo_params_acphy_wave2(phy_info_t *pi, const uint16 *val_ptr);
static void wlc_phy_write_sfo_80p80_params_acphy(phy_info_t *pi, const uint16 *val_ptr,
	const uint16 *val_ptr1);
static void wlc_phy_set_reg_on_reset_acphy_20691(phy_info_t *pi);
static void acphy_load_txv_for_spexp(phy_info_t *pi);
static void wlc_phy_cfg_energydrop_timeout(phy_info_t *pi);
static void wlc_phy_set_reg_on_bw_change_acphy(phy_info_t *pi);
static void wlc_phy_set_pdet_on_reset_acphy(phy_info_t *pi);
static void wlc_phy_set_tx_iir_coeffs(phy_info_t *pi, bool cck, uint8 filter_type);
static void wlc_phy_write_regtbl_fc3_sub0(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc3_sub1(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc3_sub2(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc3_sub3(phy_info_t *pi);
static INLINE void wlc_phy_write_regtbl_fc3(phy_info_t *pi, phy_info_acphy_t *pi_ac);
static void wlc_phy_write_regtbl_fc4_sub0(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc4_sub1(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc4_sub2(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc4_sub34(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc4_sub5(phy_info_t *pi);
static INLINE void wlc_phy_write_regtbl_fc4(phy_info_t *pi, phy_info_acphy_t *pi_ac);
static void wlc_phy_write_regtbl_fc10_sub0(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc10_sub1(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc10_sub2(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc10_sub3(phy_info_t *pi);
static void wlc_phy_write_regtbl_fc10_sub4(phy_info_t *pi);
static INLINE void wlc_phy_write_regtbl_fc10(phy_info_t *pi, phy_info_acphy_t *pi_ac);
static void wlc_phy_tx_gm_gain_boost(phy_info_t *pi);
static void wlc_phy_write_rx_farrow_pre_tiny(phy_info_t *pi, chan_info_rx_farrow *rx_farrow,
	chanspec_t chanspec);
static void wlc_phy_set_reg_on_reset_acphy(phy_info_t *pi);
static void wlc_phy_set_analog_tx_lpf(phy_info_t *pi, uint16 mode_mask, int bq0_bw, int bq1_bw,
	int rc_bw, int gmult, int gmult_rc, int core_num);
static void wlc_phy_set_tx_afe_dacbuf_cap(phy_info_t *pi, uint16 mode_mask, int dacbuf_cap,
	int dacbuf_fixed_cap, int core_num);
static void wlc_phy_set_analog_rx_lpf(phy_info_t *pi, uint8 mode_mask, int bq0_bw, int bq1_bw,
	int rc_bw, int gmult, int gmult_rc, int core_num);
#ifndef ACPHY_1X1_ONLY
static void wlc_phy_write_tx_farrow_acphy(phy_ac_chanmgr_info_t *ci, chanspec_t chanspec);
#endif
static void wlc_phy_radio20693_set_reset_table_bits(phy_info_t *pi, uint16 tbl_id, uint16 offset,
	uint16 start, uint16 end, uint16 val, uint8 tblwidth);
static void wlc_acphy_dyn_papd_cfg_20693(phy_info_t *pi);
static void wlc_phy_set_bias_ipa_as_epa_acphy_20693(phy_info_t *pi, uint8 core);

static void wlc_phy_farrow_setup_28nm(phy_info_t *pi, uint16 dac_rate_mode);
/* chanspec handle */
typedef void (*chanspec_module_t)(phy_info_t *pi);
chanspec_module_t * BCMRAMFN(get_chanspec_module_list)(void);

/* setup */
static void chanspec_setup(phy_info_t *pi);
static void chanspec_setup_phy(phy_info_t *pi);
static void chanspec_setup_cmn(phy_info_t *pi);

/* tune */
static void chanspec_tune_phy(phy_info_t *pi);
static void chanspec_tune_txpath(phy_info_t *pi);
static void chanspec_tune_rxpath(phy_info_t *pi);

/* wars & features */
static void chanspec_fw_enab(phy_info_t *pi);

/* cleanup */
static void chanspec_cleanup(phy_info_t *pi);

/* other helper functions */
static void chanspec_regtbl_fc_from_nvram(phy_info_t *pi);
static void chanspec_setup_hirssi_ucode_cap(phy_info_t *pi);
static void chanspec_sparereg_war(phy_info_t *pi);
static void chanspec_prefcbs_init(phy_info_t *pi);
static bool chanspec_papr_enable(phy_info_t *pi);

/* phy setups */
static void chanspec_setup_phy_ACMAJORREV_40(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_37(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_36(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_32(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_25(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_5(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_4(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_3(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_2(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_1(phy_info_t *pi);
static void chanspec_setup_phy_ACMAJORREV_0(phy_info_t *pi);

/* phy tunables */
static void chanspec_tune_phy_ACMAJORREV_40(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_37(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_36(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_32(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_25(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_5(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_4(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_3(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_2(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_1(phy_info_t *pi);
static void chanspec_tune_phy_ACMAJORREV_0(phy_info_t *pi);

void wlc_phy_ac_shared_ant_femctrl_master(phy_info_t *pi);
#if (defined(WL_SISOCHIP) || !defined(SWCTRL_TO_BT_IN_COEX))
static void wlc_phy_ac_femctrl_mask_on_band_change(phy_info_t *pi);
#endif

#ifdef WL11ULB
static void phy_ac_chanmgr_write_sfo_ulb_params_acphy(phy_info_t *pi, int freq);
#endif /* WL11ULB */
static int phy_ac_chanmgr_switch_phymode_acphy(phy_info_t *pi, uint32 phymode);

chanspec_module_t chanspec_module_list[] = {
	chanspec_setup,
	chanspec_setup_tpc,
	chanspec_setup_radio,
	chanspec_setup_phy,
	chanspec_setup_cmn,
	chanspec_noise,
	chanspec_setup_rxgcrs,
	chanspec_tune_radio,
	chanspec_tune_phy,
	chanspec_tune_txpath,
	chanspec_tune_rxpath,
	chanspec_fw_enab,
	chanspec_cleanup,
	chanspec_btcx,
	NULL
};

chanspec_module_t *
BCMRAMFN(get_chanspec_module_list)(void)
{
	return chanspec_module_list;
}

static acphytbl_info_t *
BCMRAMFN(phy_ac_get_tbls_write_on_reset_list)(phy_info_t *pi)
{
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		return tbls_write_on_reset_list_majrev36;
	} else {
		PHY_ERROR(("wl%d: %s: tables write on reset list not know for rev%d\n",
				pi->sh->unit, __FUNCTION__, pi->pubpi->phy_rev));
		return NULL;
	}
}

static void
wlc_phy_config_bias_settings_20693(phy_info_t *pi)
{
	uint8 core;
	ASSERT(RADIOID_IS(pi->pubpi->radioid, BCM20693_ID));

	FOREACH_CORE(pi, core) {

		MOD_RADIO_REG_20693(pi, TRSW2G_CFG1, core, trsw2g_pu, 0);
		MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core, ovr_trsw2g_pu, 1);
		MOD_RADIO_REG_20693(pi, TRSW2G_CFG1, core, trsw2g_bias_pu, 0);
		MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core, ovr_trsw2g_bias_pu, 1);
		MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core, ovr_mx2g_idac_bbdc, 1);
		MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core, ovr_mx5g_idac_bbdc, 1);
		MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core, ovr_pad5g_idac_pmos, 1);
		MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core, ovr_pad5g_idac_gm, 1);
		MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR3, core, ovr_pa5g_bias_filter_main, 1);
	}
	if (ROUTER_4349(pi)) {
		/* 53573A0: 5G Tx Low Power Radio Optimizations */
		/* proc 20693_tx5g_set_bias_ipa_opt_sv */
		if (PHY_IPA(pi)) {
			bool BW40, BW80;

			FOREACH_CORE(pi, core) {
				if (CHSPEC_IS5G(pi->radio_chanspec)) {
					BW40 = (CHSPEC_IS40(pi->radio_chanspec));
					BW80 = (CHSPEC_IS80(pi->radio_chanspec) ||
						CHSPEC_IS8080(pi->radio_chanspec));

					MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR1, core,
						ovr_trsw5g_pu, 0x1);
					MOD_RADIO_REG_20693(pi, TRSW5G_CFG1, core,
						trsw5g_pu, 0x0);
					MOD_RADIO_REG_20693(pi, SPARE_CFG7, core,
						swcap_sec_gate_off_5g, 0xf);
					MOD_RADIO_REG_20693(pi, SPARE_CFG7, core,
						swcap_sec_sd_on_5g, 0x10);
					MOD_RADIO_REG_20693(pi, SPARE_CFG6, core,
						swcap_pri_pd_5g, 0x1);
					MOD_RADIO_REG_20693(pi, SPARE_CFG6, core,
						swcap_pri_5g, 0x0);
					MOD_RADIO_REG_20693(pi, SPARE_CFG6, core,
						swcap_pri_gate_off_5g, 0x0);
					MOD_RADIO_REG_20693(pi, SPARE_CFG6, core,
						swcap_pri_sd_on_5g, 0x0);

					MOD_RADIO_REG_20693(pi, PA5G_CFG3, core,
						pa5g_ptat_slope_main, 0x0);

					MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core,
						ovr_pa5g_idac_incap_compen_main, 0x1);
					MOD_RADIO_REG_20693(pi, PA5G_INCAP, core,
						pa5g_idac_incap_compen_main,
						IS_ACR(pi) ? ((BW40 || BW80) ? 0xc : 0x3a) :
						0x16);
					MOD_RADIO_REG_20693(pi, PA5G_IDAC3, core,
						pa5g_idac_tuning_bias, 0x0);

					MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core,
						ovr_pad5g_idac_gm, 1);
					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG8, core, pad5g_idac_gm,
						IS_ACR(pi) ? ((BW40 || BW80) ? 0x3f : 0x26) :
						0x1a);
					MOD_RADIO_REG_20693(pi, TXGM5G_CFG1, core,
						pad5g_idac_cascode,
						IS_ACR(pi) ? ((BW40 || BW80) ? 0xf : 0xe) :
						0xd);
					MOD_RADIO_REG_20693(pi, SPARE_CFG10, core,
						pad5g_idac_cascode2, 0x0);
					MOD_RADIO_REG_20693(pi, PA5G_INCAP, core, pad5g_idac_pmos,
						(IS_ACR(pi) && (BW40 || BW80)) ? 0xa : 0x1c);

					MOD_RADIO_REG_20693(pi, PA5G_IDAC3, core,
						pad5g_idac_tuning_bias, 0xd);
					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG6, core,
						mx5g_ptat_slope_cascode, 0x0);
					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG6, core,
						mx5g_ptat_slope_lodc, 0x2);
					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG4, core,
						mx5g_idac_cascode, 0xf);
					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG5, core,
						mx5g_idac_lodc, 0xa);
					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG5, core, mx5g_idac_bbdc,
						(BW40 && IS_ACR(pi)) ? 0x2 : 0xc);

					MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core,
						ovr_pa5g_idac_main, 1);
					MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR3, core,
						ovr_pa5g_idac_cas, 1);

					if (BW80) {
						MOD_RADIO_REG_20693(pi, PA5G_IDAC1, core,
							pa5g_idac_main, (IS_ACR(pi)) ? 0x38 : 0x3a);
						MOD_RADIO_REG_20693(pi, PA5G_IDAC1, core,
							pa5g_idac_cas, (IS_ACR(pi)) ? 0x14 : 0x15);
					} else if (BW40) {
						MOD_RADIO_REG_20693(pi, PA5G_IDAC1, core,
							pa5g_idac_main, (IS_ACR(pi)) ? 0x34 : 0x28);
						MOD_RADIO_REG_20693(pi, PA5G_IDAC1, core,
							pa5g_idac_cas, (IS_ACR(pi)) ? 0x14 : 0x13);
					} else {
						MOD_RADIO_REG_20693(pi, PA5G_IDAC1, core,
							pa5g_idac_main, (IS_ACR(pi)) ? 0x20 : 0x1a);
						MOD_RADIO_REG_20693(pi, PA5G_IDAC1, core,
							pa5g_idac_cas, (IS_ACR(pi)) ? 0x12 : 0x11);
					}

					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG3, core,
						mx5g_pu_bleed, 0x0);
					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG1, core,
						mx5g_idac_bleed_bias, 0x0);
					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG4, core,
						mx5g_idac_tuning_bias, 0xd);
				} else {
					/* 2G Tx settings */
					MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
							ovr_pa2g_idac_main, 0x1);
					MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
							ovr_pa2g_idac_cas, 0x1);
					if (CHSPEC_IS40(pi->radio_chanspec)) {
						MOD_RADIO_REG_20693(pi, PA2G_IDAC1, core,
								pa2g_idac_main, 0x1e);
						MOD_RADIO_REG_20693(pi, PA2G_IDAC1, core,
								pa2g_idac_cas, 0x21);
					} else {
						MOD_RADIO_REG_20693(pi, PA2G_IDAC1, core,
								pa2g_idac_main, 0x19);
						MOD_RADIO_REG_20693(pi, PA2G_IDAC1, core,
								pa2g_idac_cas, 0x24);
					}

					MOD_RADIO_REG_20693(pi, SPARE_CFG10, core,
							pa2g_incap_pmos_src_sel_gnd, 0x0);
					MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
							ovr_pa2g_idac_incap_compen_main, 0x1);
					MOD_RADIO_REG_20693(pi, PA2G_INCAP, core,
							pa2g_idac_incap_compen_main, 0x34);
				}
			}
		} if (!PHY_IPA(pi) && CHSPEC_IS5G(pi->radio_chanspec)) {
			/* EVM Ramp: TxBias5G & Pad5G on */
			FOREACH_CORE(pi, core) {
				MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR1, core,
					ovr_tx5g_bias_pu, 0x1);
				MOD_RADIO_REG_20693(pi, TX5G_CFG1, core, tx5g_bias_pu, 0x1);
				MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR3, core,
					ovr_pad5g_bias_cas_pu, 0x1);
				MOD_RADIO_REG_20693(pi, TX5G_CFG1, core, pad5g_bias_cas_pu, 0x1);
				MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core, ovr_pad5g_pu, 0x1);
				MOD_RADIO_REG_20693(pi, TX5G_CFG1, core, pad5g_pu, 0x1);
			}
		}
	}
}

static void
wlc_phy_set_noise_var_shaping_acphy(phy_info_t *pi, uint8 noise_var[][ACPHY_SPURWAR_NV_NTONES],
                                             int8 *tone_id, uint8 *core_nv)
{
	uint8 i;

	/* Starting offset for nvshp */
	i = ACPHY_NV_NTONES_OFFSET;

	/* 4335C0 */
	if (ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) {
		if (!PHY_ILNA(pi)) {
			if (CHSPEC_IS80(pi->radio_chanspec)) {
				static const int8 tone_id_def[] = {-123, -122, -121, -120,
				                                   -119, -118, -117, -116,
				                                   -115, -114, -113, -112,
				                                    112,  113,  114,  115,
				                                    116,  117,  118,  119,
				                                    120,  121,  122,  123};
				static const uint8 noise_var_def[] = {0xF8, 0xF8, 0xF8, 0xF8,
				                                      0xF8, 0xF8, 0xF8, 0xF8,
				                                      0xFA, 0xFA, 0xFC, 0xFE,
				                                      0xFE, 0xFC, 0xFA, 0xFA,
				                                      0xF8, 0xF8, 0xF8, 0xF8,
				                                      0xF8, 0xF8, 0xF8, 0xF8};
				memcpy((tone_id + i), tone_id_def, sizeof(int8)*ACPHY_NV_NTONES);
				memcpy((noise_var[PHY_CORE_0] + i), noise_var_def,
				        sizeof(uint8)*ACPHY_NV_NTONES);
				*core_nv = 1; /* core 0 */
				PHY_INFORM(("wlc_phy_set_noise_var_shaping_acphy:"
				            "applying noise_var shaping for BW 80MHz\n"));
			}
		}
	}
}

/**
 * Whenever the transmit power is less than a certain value, lower PA power consumption can be
 * achieved by selecting lower PA linearity. The VLIN signal towards the FEM is configured to
 * either be driven by the FEM control table or by a chip internal VLIN signal.
 */
void wlc_phy_vlin_en_acphy(phy_info_t *pi)
{
	uint8 band2g_idx, core;
	uint8 stall_val;
	int16 idle_tssi[PHY_CORE_MAX];
	uint16 adj_tssi1[PHY_CORE_MAX];
	uint16 adj_tssi2[PHY_CORE_MAX], adj_tssi3[PHY_CORE_MAX];
	int16 tone_tssi1[PHY_CORE_MAX];
	int16 tone_tssi2[PHY_CORE_MAX], tone_tssi3[PHY_CORE_MAX];
	int16 a1[PHY_CORE_MAX];
	int16 b0[PHY_CORE_MAX];
	int16 b1[PHY_CORE_MAX];
	uint8 pwr1, pwr2, pwr3;
	uint8 txidx1 = 40, txidx2 = 90, txidx3;
	struct _orig_reg_vals {
		uint8 core;
		uint16 orig_OVR3;
		uint16 orig_auxpga_cfg1;
		uint16 orig_auxpga_vmid;
		uint16 orig_iqcal_cfg1;
		uint16 orig_tx5g_tssi;
		uint16 orig_pa2g_tssi;
		uint16 orig_RfctrlIntc;
		uint16 orig_RfctrlOverrideRxPus;
		uint16 orig_RfctrlCoreRxPu;
		uint16 orig_RfctrlOverrideAuxTssi;
		uint16 orig_RfctrlCoreAuxTssi1;
		} orig_reg_vals[PHY_CORE_MAX];
	uint core_count = 0;
	txgain_setting_t curr_gain1, curr_gain2, curr_gain3;
	bool init_adc_inside = FALSE;
	uint16 save_afePuCtrl, save_gpio;
	uint16 orig_use_txPwrCtrlCoefs;
	uint16 fval2g_orig, fval5g_orig, fval2g, fval5g;
	uint32 save_chipc = 0;
	uint16 save_gpioHiOutEn;
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	txgain_setting_t curr_gain4;
	int16 tone_tssi4[PHY_CORE_MAX];
	uint16 adj_tssi4[PHY_CORE_MAX];
	int bbmultcomp;
	uint16 tempmuxTxVlinOnFemCtrl2;
	uint16 txidxval;
	uint16 txgaintemp1[3], txgaintemp1a[3];
	uint16 tempmuxTxVlinOnFemCtrl, globpusmask;
	ASSERT(RADIOID_IS(pi->pubpi->radioid, BCM2069_ID));
	/* prevent crs trigger */
	wlc_phy_stay_in_carriersearch_acphy(pi, TRUE);
	band2g_idx = CHSPEC_IS2G(pi->radio_chanspec);
	if (band2g_idx)	{
		pwr3 = pi_ac->chanmgri->cfg.vlinpwr2g_from_nvram;
		}
	else {
		pwr3 = pi_ac->chanmgri->cfg.vlinpwr5g_from_nvram;
		}
	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);
	/* Turn off epa/ipa and unused rxrf part to prevent energy go into air */
	orig_use_txPwrCtrlCoefs = READ_PHYREGFLD(pi, TxPwrCtrlCmd,
	use_txPwrCtrlCoefs);
	FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
		/* save phy/radio regs going to be touched */
		orig_reg_vals[core_count].orig_RfctrlIntc = READ_PHYREGCE(pi,
		RfctrlIntc, core);
		orig_reg_vals[core_count].orig_RfctrlOverrideRxPus =
			READ_PHYREGCE(pi, RfctrlOverrideRxPus, core);
		orig_reg_vals[core_count].orig_RfctrlCoreRxPu =
			READ_PHYREGCE(pi, RfctrlCoreRxPus, core);
		orig_reg_vals[core_count].orig_RfctrlOverrideAuxTssi =
			READ_PHYREGCE(pi, RfctrlOverrideAuxTssi, core);
		orig_reg_vals[core_count].orig_RfctrlCoreAuxTssi1 =
			READ_PHYREGCE(pi, RfctrlCoreAuxTssi1, core);
		orig_reg_vals[core_count].orig_OVR3 = READ_RADIO_REGC(pi,
			RF, OVR3, core);
		orig_reg_vals[core_count].orig_auxpga_cfg1 =
			READ_RADIO_REGC(pi, RF, AUXPGA_CFG1, core);
		orig_reg_vals[core_count].orig_auxpga_vmid =
			READ_RADIO_REGC(pi, RF, AUXPGA_VMID, core);
		orig_reg_vals[core_count].orig_iqcal_cfg1 =
			READ_RADIO_REGC(pi, RF, IQCAL_CFG1, core);
		orig_reg_vals[core_count].orig_tx5g_tssi = READ_RADIO_REGC(pi,
			RF, TX5G_TSSI, core);
		orig_reg_vals[core_count].orig_pa2g_tssi = READ_RADIO_REGC(pi,
			RF, PA2G_TSSI, core);
		orig_reg_vals[core_count].core = core;
		/* set tssi_range = 0   (it suppose to bypass 10dB attenuation before pdet) */
		MOD_PHYREGCE(pi, RfctrlOverrideAuxTssi,  core, tssi_range, 1);
		MOD_PHYREGCE(pi, RfctrlCoreAuxTssi1,	 core, tssi_range, 0);
		/* turn off lna and other unsed rxrf components */
		WRITE_PHYREGCE(pi, RfctrlOverrideRxPus, core, 0x7CE0);
		WRITE_PHYREGCE(pi, RfctrlCoreRxPus, 	core, 0x0);
		++core_count;
		}
	ACPHY_ENABLE_STALL(pi, stall_val);
	/* tssi loopback setup */
	phy_ac_tssi_loopback_path_setup(pi, LOOPBACK_FOR_TSSICAL);

	if (!init_adc_inside) {
		wlc_phy_init_adc_read(pi, &save_afePuCtrl, &save_gpio,
			&save_chipc, &fval2g_orig, &fval5g_orig,
			&fval2g, &fval5g, &stall_val, &save_gpioHiOutEn);
		}
	wlc_phy_get_paparams_for_band_acphy(pi, a1, b0, b1);
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		if (!init_adc_inside)
			wlc_phy_gpiosel_acphy(pi, 16+core, 1);
		/* Measure the Idle TSSI */
		wlc_phy_poll_samps_WAR_acphy(pi, idle_tssi, TRUE, TRUE, NULL,
		FALSE, init_adc_inside, core, 1);
		MOD_PHYREG(pi, TxPwrCtrlCmd, use_txPwrCtrlCoefs, 0);
		wlc_phy_get_txgain_settings_by_index_acphy(pi, &curr_gain1, txidx1);
		wlc_phy_poll_samps_WAR_acphy(pi, tone_tssi1, TRUE, FALSE,
			&curr_gain1, FALSE, init_adc_inside, core, 1);
		adj_tssi1[core] = 1024+idle_tssi[core]-tone_tssi1[core];
		adj_tssi1[core] = adj_tssi1[core] >> 3;
		pwr1 = wlc_phy_tssi2dbm_acphy(pi, adj_tssi1[core], a1[core], b0[core], b1[core]);
		wlc_phy_get_txgain_settings_by_index_acphy(pi, &curr_gain2, txidx2);
		wlc_phy_poll_samps_WAR_acphy(pi, tone_tssi2, TRUE, FALSE,
			&curr_gain2, FALSE, init_adc_inside, core, 1);
		adj_tssi2[core] = 1024+idle_tssi[core]-tone_tssi2[core];
		adj_tssi2[core] = adj_tssi2[core] >> 3;
		pwr2 = wlc_phy_tssi2dbm_acphy(pi, adj_tssi2[core], a1[core], b0[core], b1[core]);
		if (pwr2 != pwr1) {
			txidx3 = txidx1+(4*pwr3-pwr1) *(txidx2-txidx1)/(pwr2-pwr1);
		} else {
			txidx3 = txidx1;
		}
		wlc_phy_get_txgain_settings_by_index_acphy(pi, &curr_gain3, txidx3);
		wlc_phy_poll_samps_WAR_acphy(pi, tone_tssi3, TRUE, FALSE,
			&curr_gain3, FALSE, init_adc_inside, core, 1);
		adj_tssi3[core] = 1024+idle_tssi[core]-tone_tssi3[core];
		adj_tssi3[core] = adj_tssi3[core] >> 3;
		if (band2g_idx)	{
			globpusmask = 1<<(pi_ac->chanmgri->data.vlinmask2g_from_nvram);
		} else {
			globpusmask = 1<<(pi_ac->chanmgri->data.vlinmask5g_from_nvram);
		}
		tempmuxTxVlinOnFemCtrl = READ_PHYREGFLD(pi, RfctrlCoreGlobalPus,
			muxTxVlinOnFemCtrl);
		tempmuxTxVlinOnFemCtrl2 = (tempmuxTxVlinOnFemCtrl | globpusmask);
		MOD_PHYREG(pi, RfctrlCoreGlobalPus, muxTxVlinOnFemCtrl, tempmuxTxVlinOnFemCtrl2);
		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_GAINCTRLBBMULTLUTS,
			1, txidx3, 48, &txgaintemp1);
		txgaintemp1a[0] = (txgaintemp1[0]|0x8000);
		txgaintemp1a[1] = txgaintemp1[1];
		txgaintemp1a[2] = txgaintemp1[2];
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_GAINCTRLBBMULTLUTS, 1,
			txidx3, 48, txgaintemp1a);
		wlc_phy_get_txgain_settings_by_index_acphy(pi, &curr_gain4, txidx3);
		wlc_phy_poll_samps_WAR_acphy(pi, tone_tssi4, TRUE, FALSE,
			&curr_gain4, FALSE, init_adc_inside, core, 1);
		adj_tssi4[core] = 1024+idle_tssi[core]-tone_tssi4[core];
		adj_tssi4[core] = adj_tssi4[core] >> 3;
		bbmultcomp = (int)((tone_tssi3[core]-tone_tssi4[core])/6);
		pi_ac->chanmgri->data.vlin_txidx = txidx3;
		pi_ac->chanmgri->data.bbmult_comp = bbmultcomp;
		for (txidxval = txidx3; txidxval < 128; txidxval++) {
			wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_GAINCTRLBBMULTLUTS, 1,
				txidxval, 48, &txgaintemp1);
			txgaintemp1a[0] = (txgaintemp1[0]|0x8000)+bbmultcomp;
			txgaintemp1a[1] = txgaintemp1[1];
			txgaintemp1a[2] = txgaintemp1[2];
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_GAINCTRLBBMULTLUTS, 1,
				txidxval, 48, txgaintemp1a);
			}
		if (!init_adc_inside)
			wlc_phy_restore_after_adc_read(pi, &save_afePuCtrl, &save_gpio,
			&save_chipc, &fval2g_orig, &fval5g_orig,
			&fval2g, &fval5g, &stall_val, &save_gpioHiOutEn);
		/* restore phy/radio regs */
		while (core_count > 0) {
			--core_count;
			phy_utils_write_radioreg(pi, RF_2069_OVR3(orig_reg_vals[core_count].core),
				orig_reg_vals[core_count].orig_OVR3);
			phy_utils_write_radioreg(pi,
				RF_2069_AUXPGA_CFG1(orig_reg_vals[core_count].core),
				orig_reg_vals[core_count].orig_auxpga_cfg1);
			phy_utils_write_radioreg(pi,
				RF_2069_AUXPGA_VMID(orig_reg_vals[core_count].core),
				orig_reg_vals[core_count].orig_auxpga_vmid);
			phy_utils_write_radioreg(pi,
				RF_2069_IQCAL_CFG1(orig_reg_vals[core_count].core),
				orig_reg_vals[core_count].orig_iqcal_cfg1);
			phy_utils_write_radioreg(pi,
				RF_2069_TX5G_TSSI(orig_reg_vals[core_count].core),
				orig_reg_vals[core_count].orig_tx5g_tssi);
			phy_utils_write_radioreg(pi,
				RF_2069_PA2G_TSSI(orig_reg_vals[core_count].core),
				orig_reg_vals[core_count].orig_pa2g_tssi);
			WRITE_PHYREGCE(pi, RfctrlIntc, orig_reg_vals[core_count].core,
				orig_reg_vals[core_count].orig_RfctrlIntc);
			WRITE_PHYREGCE(pi, RfctrlOverrideRxPus,
				orig_reg_vals[core_count].core,
				orig_reg_vals[core_count].orig_RfctrlOverrideRxPus);
			WRITE_PHYREGCE(pi, RfctrlCoreRxPus, orig_reg_vals[core_count].core,
				orig_reg_vals[core_count].orig_RfctrlCoreRxPu);
			WRITE_PHYREGCE(pi, RfctrlOverrideAuxTssi,
				orig_reg_vals[core_count].core,
				orig_reg_vals[core_count].orig_RfctrlOverrideAuxTssi);
			WRITE_PHYREGCE(pi, RfctrlCoreAuxTssi1,
				orig_reg_vals[core_count].core,
				orig_reg_vals[core_count].orig_RfctrlCoreAuxTssi1);
			}
		MOD_PHYREG(pi, TxPwrCtrlCmd, use_txPwrCtrlCoefs, orig_use_txPwrCtrlCoefs);
		/* prevent crs trigger */
		wlc_phy_stay_in_carriersearch_acphy(pi, FALSE);
		PHY_TRACE(("======= IQLOCAL PreCalGainControl : END =======\n"));
		}
}

/* customize papr shaping filters */
static void phy_ac_chanmgr_papr_iir_filt_reprog(phy_info_t *pi)
{
	uint16 k;

	// 20_in_20: [b, a] = cheby1(4, 1.7, 5/Fs*2); norm = 1.02
	// filt Fc is smaller, will be fixed in C0
	//
	// 20_40 and 20_80 are copied from 20_20 and 20_40
	// of the 4x filt designs, respectively,
	// [b, a] = cheby1(4, 1, 7/Fs*2);

	uint16 ppr_iir_phyreg_vals_rev32[][2] =
	{{ACPHY_papr_iir_20_20_group_dly(pi->pubpi.phy_rev), 4},
	 {ACPHY_papr_iir_20_20_b10(pi->pubpi.phy_rev), 127},
	 {ACPHY_papr_iir_20_20_b11(pi->pubpi.phy_rev), 254},
	 {ACPHY_papr_iir_20_20_b12(pi->pubpi.phy_rev), 127},
	 {ACPHY_papr_iir_20_20_a11(pi->pubpi.phy_rev), 341},
	 {ACPHY_papr_iir_20_20_a12(pi->pubpi.phy_rev), 109},
	 {ACPHY_papr_iir_20_20_b20(pi->pubpi.phy_rev), 126},
	 {ACPHY_papr_iir_20_20_b21(pi->pubpi.phy_rev), 252},
	 {ACPHY_papr_iir_20_20_b22(pi->pubpi.phy_rev), 126},
	 {ACPHY_papr_iir_20_20_a21(pi->pubpi.phy_rev), 318},
	 {ACPHY_papr_iir_20_20_a22(pi->pubpi.phy_rev), 82},
	 {ACPHY_papr_iir_20_40_group_dly(pi->pubpi.phy_rev), 6},
	 {ACPHY_papr_iir_20_40_b10(pi->pubpi.phy_rev), 66},
	 {ACPHY_papr_iir_20_40_b11(pi->pubpi.phy_rev), 131},
	 {ACPHY_papr_iir_20_40_b12(pi->pubpi.phy_rev), 66},
	 {ACPHY_papr_iir_20_40_a11(pi->pubpi.phy_rev), 308},
	 {ACPHY_papr_iir_20_40_a12(pi->pubpi.phy_rev), 111},
	 {ACPHY_papr_iir_20_40_b20(pi->pubpi.phy_rev), 74},
	 {ACPHY_papr_iir_20_40_b21(pi->pubpi.phy_rev), 149},
	 {ACPHY_papr_iir_20_40_b22(pi->pubpi.phy_rev), 74},
	 {ACPHY_papr_iir_20_40_a21(pi->pubpi.phy_rev), 306},
	 {ACPHY_papr_iir_20_40_a22(pi->pubpi.phy_rev), 88},
	 {ACPHY_papr_iir_20_80_group_dly(pi->pubpi.phy_rev), 6},
	 {ACPHY_papr_iir_20_80_b10(pi->pubpi.phy_rev), 17},
	 {ACPHY_papr_iir_20_80_b11(pi->pubpi.phy_rev), 35},
	 {ACPHY_papr_iir_20_80_b12(pi->pubpi.phy_rev), 17},
	 {ACPHY_papr_iir_20_80_a11(pi->pubpi.phy_rev), 274},
	 {ACPHY_papr_iir_20_80_a12(pi->pubpi.phy_rev), 119},
	 {ACPHY_papr_iir_20_80_b20(pi->pubpi.phy_rev), 20},
	 {ACPHY_papr_iir_20_80_b21(pi->pubpi.phy_rev), 40},
	 {ACPHY_papr_iir_20_80_b22(pi->pubpi.phy_rev), 20},
	 {ACPHY_papr_iir_20_80_a21(pi->pubpi.phy_rev), 280},
	 {ACPHY_papr_iir_20_80_a22(pi->pubpi.phy_rev), 106}};

	/* 20in20: cheby1(4,0.5,8/40*2) */
	uint16 ppr_iir_phyreg_vals_rev33[][2] =
	{{ACPHY_papr_iir_20_20_group_dly(pi->pubpi.phy_rev), 6},
	 {ACPHY_papr_iir_20_20_b10(pi->pubpi.phy_rev), 299},
	 {ACPHY_papr_iir_20_20_b11(pi->pubpi.phy_rev), 598},
	 {ACPHY_papr_iir_20_20_b12(pi->pubpi.phy_rev), 299},
	 {ACPHY_papr_iir_20_20_a11(pi->pubpi.phy_rev), 450},
	 {ACPHY_papr_iir_20_20_a12(pi->pubpi.phy_rev), 92},
	 {ACPHY_papr_iir_20_20_b20(pi->pubpi.phy_rev), 424},
	 {ACPHY_papr_iir_20_20_b21(pi->pubpi.phy_rev), 847},
	 {ACPHY_papr_iir_20_20_b22(pi->pubpi.phy_rev), 424},
	 {ACPHY_papr_iir_20_20_a21(pi->pubpi.phy_rev), 397},
	 {ACPHY_papr_iir_20_20_a22(pi->pubpi.phy_rev), 41},
	 {ACPHY_papr_iir_20_40_group_dly(pi->pubpi.phy_rev), 6},
	 {ACPHY_papr_iir_20_40_b10(pi->pubpi.phy_rev), 66},
	 {ACPHY_papr_iir_20_40_b11(pi->pubpi.phy_rev), 131},
	 {ACPHY_papr_iir_20_40_b12(pi->pubpi.phy_rev), 66},
	 {ACPHY_papr_iir_20_40_a11(pi->pubpi.phy_rev), 308},
	 {ACPHY_papr_iir_20_40_a12(pi->pubpi.phy_rev), 111},
	 {ACPHY_papr_iir_20_40_b20(pi->pubpi.phy_rev), 74},
	 {ACPHY_papr_iir_20_40_b21(pi->pubpi.phy_rev), 149},
	 {ACPHY_papr_iir_20_40_b22(pi->pubpi.phy_rev), 74},
	 {ACPHY_papr_iir_20_40_a21(pi->pubpi.phy_rev), 306},
	 {ACPHY_papr_iir_20_40_a22(pi->pubpi.phy_rev), 88},
	 {ACPHY_papr_iir_20_80_group_dly(pi->pubpi.phy_rev), 6},
	 {ACPHY_papr_iir_20_80_b10(pi->pubpi.phy_rev), 17},
	 {ACPHY_papr_iir_20_80_b11(pi->pubpi.phy_rev), 35},
	 {ACPHY_papr_iir_20_80_b12(pi->pubpi.phy_rev), 17},
	 {ACPHY_papr_iir_20_80_a11(pi->pubpi.phy_rev), 274},
	 {ACPHY_papr_iir_20_80_a12(pi->pubpi.phy_rev), 119},
	 {ACPHY_papr_iir_20_80_b20(pi->pubpi.phy_rev), 20},
	 {ACPHY_papr_iir_20_80_b21(pi->pubpi.phy_rev), 40},
	 {ACPHY_papr_iir_20_80_b22(pi->pubpi.phy_rev), 20},
	 {ACPHY_papr_iir_20_80_a21(pi->pubpi.phy_rev), 280},
	 {ACPHY_papr_iir_20_80_a22(pi->pubpi.phy_rev), 106}};

	if (!(ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	      ACMAJORREV_37(pi->pubpi->phy_rev))) {
		return;
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev)) {
		for (k = 0; k < ARRAYSIZE(ppr_iir_phyreg_vals_rev32); k++)  {
			phy_utils_write_phyreg(pi, ppr_iir_phyreg_vals_rev32[k][0],
					ppr_iir_phyreg_vals_rev32[k][1]);
		}
	} else if  (ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		for (k = 0; k < ARRAYSIZE(ppr_iir_phyreg_vals_rev33); k++)  {
			phy_utils_write_phyreg(pi, ppr_iir_phyreg_vals_rev33[k][0],
					ppr_iir_phyreg_vals_rev33[k][1]);
		}
	}
}

/* preemption table for 4365 post Rx filter detection */
static void phy_ac_chanmgr_preempt_postfilter_reg_tbl(phy_info_t *pi, bool enable)
{
	uint16 k, preempt_en = 0;
	uint16 ppr_preempt_phyreg_vals[][2] = {
		{ACPHY_PREMPT_ofdm_nominal_clip_th0(pi->pubpi->phy_rev), 0xae00},
		{ACPHY_PREMPT_cck_nominal_clip_th0(pi->pubpi->phy_rev), 0x4d00},
		{ACPHY_PREMPT_ofdm_large_gain_mismatch_th0(pi->pubpi->phy_rev), 0x1e},
		{ACPHY_PREMPT_cck_large_gain_mismatch_th0(pi->pubpi->phy_rev), 0x10},
		{ACPHY_PREMPT_ofdm_low_power_mismatch_th0(pi->pubpi->phy_rev), 0x1a},
		{ACPHY_PREMPT_cck_low_power_mismatch_th0(pi->pubpi->phy_rev), 0x1c},
		{ACPHY_PREMPT_ofdm_max_gain_mismatch_pkt_rcv_th0(pi->pubpi->phy_rev), 0x1e},
		{ACPHY_PREMPT_cck_max_gain_mismatch_pkt_rcv_th0(pi->pubpi->phy_rev), 0xa}};

	if (enable) {
		for (k = 0; k < ARRAYSIZE(ppr_preempt_phyreg_vals); k++) {
			/* using broadcast address to take care of all 4 tones */
			phy_utils_write_phyreg(pi,
			ppr_preempt_phyreg_vals[k][0] | ACPHY_REG_BROADCAST(pi),
			ppr_preempt_phyreg_vals[k][1]);
		}
		if ((pi->sh->interference_mode & ACPHY_LPD_PREEMPTION) != 0) {
			preempt_en = 0x1b;
		} else {
			preempt_en = 0x19;
		}
	}

	ACPHYREG_BCAST(pi, PREMPT_per_pkt_en0, preempt_en);
	//wlapi_bmac_write_shm(pi->sh->physhim, M_PHYPREEMPT_VAL(pi), preempt_en);
}

/* PAPRR Functions */
static void chanspec_setup_papr(phy_info_t *pi,
int8 papr_final_clipping, int8 papr_final_scaling)
{
	uint16 lowMcsGamma = 600, highMcsGamma, vhtMcsGamma_c8_c9 = 1100;
	uint16 highMcsGamma_c8_c11 = 1200;
	uint32 gain = 128, gamma;
	uint8 i, j, core;
	bool enable = chanspec_papr_enable(pi);

	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		highMcsGamma = 950;
		vhtMcsGamma_c8_c9 = 950;
		lowMcsGamma = 600;
	} else {
		highMcsGamma = 1100;
		vhtMcsGamma_c8_c9 = 1100;
		if ((ACMAJORREV_32(pi->pubpi->phy_rev) ||
		     ACMAJORREV_33(pi->pubpi->phy_rev) ||
		     ACMAJORREV_37(pi->pubpi->phy_rev)) &&
			(CHSPEC_IS20(pi->radio_chanspec)))
			lowMcsGamma = 700;
		else
			lowMcsGamma = 600;
	}

	if (!PHY_IPA(pi) && ACMAJORREV_2(pi->pubpi->phy_rev)) {
		vhtMcsGamma_c8_c9 = 8191;
	}

	if (enable) {
		MOD_PHYREG(pi, papr_ctrl, papr_blk_en, enable);
		MOD_PHYREG(pi, papr_ctrl, papr_final_clipping_en, papr_final_clipping);
		MOD_PHYREG(pi, papr_ctrl, papr_final_scaling_en, papr_final_scaling);
		MOD_PHYREG(pi, papr_ctrl, papr_override_enable, 0);

		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		    ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			FOREACH_CORE(pi, core) {
				switch (core) {
				case 0:
					MOD_PHYREG(pi, papr_gain_index_p0, papr_enable, 1);
					break;
				case 1:
					MOD_PHYREG(pi, papr_gain_index_p1, papr_enable, 1);
					break;
				case 2:
					MOD_PHYREG(pi, papr_gain_index_p2, papr_enable, 1);
					break;
				case 3:
					MOD_PHYREG(pi, papr_gain_index_p3, papr_enable, 1);
					break;
				default:
					PHY_ERROR(("%s: Max 4 cores only!\n", __FUNCTION__));
					ASSERT(0);
				}
			}
			phy_ac_chanmgr_papr_iir_filt_reprog(pi);
		}

		if (ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
			/* Program enable/gainidx */
			gamma = 0x0; /* not used fields */
			for (i = 1; i <= 3; i++) {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, i,
						32, &gamma);
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, 0x40 + i,
						32, &gamma);
			}
			gamma = 0x80;
			for (i = 4; i <= 33; i++) {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, i,
						32, &gamma);
			}

			/* Program gamma1/gamma */
			for (i = 0x44; i <= 0x40 + 31; i++) {
				if ((i >= 0x44 && i <= 0x47) || (i >= 0x4C && i <= 0x4E) ||
						(i >= 0x54 && i <= 0x56)) {
					gamma = (lowMcsGamma << 13) | lowMcsGamma;
				} else {
					gamma = (highMcsGamma << 13) | highMcsGamma;
					if (i >= 0x5C && i <= 0x5F) {
						gamma = (highMcsGamma_c8_c11 << 13) |
							highMcsGamma_c8_c11;
					}
				}
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, i,
						32, &gamma);
			}

			/* program GammaOffset fields */
			for (i = 0; i <= 2; i++) {
				uint16 gammaOffset[3] = {0, 100, 150};
				gamma = (gammaOffset[i] << 13) | gammaOffset[i];
				j = ((i == 2) ? 0 : (32 + i));
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, (0x40+j),
						32, &gamma);
				gamma = 0x80;
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, j,
						32, &gamma);
			}
		} else {
			for (j = 4; j <= 32; j++) {
				if (j <= 29) {
					/* gain entries for different rates */
					gain = 128;
				} else {
					/* gain offsets */
					gain = 0;
				}
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, j, 32, &gain);
			}
			for (j = 0x44; j <= 0x5D; j++) {
				/* tbl offset starting 0x40 is gamma table */
				if (j >= 0x5C && j <= 0x5D) {
					/* vht rate mcs8 mcs 9 256 QAM */
					gamma = (vhtMcsGamma_c8_c9 << 13) | vhtMcsGamma_c8_c9;
				} else if ((j >= 0x44 && j <= 0x47) || (j >= 0x4c && j <= 0x4e) ||
					(j >= 0x54 && j <= 0x56)) {
					/* All BPSK and QPSK rates */
					gamma = (lowMcsGamma << 13) | lowMcsGamma;
				} else {
					/* ALL 16QAM and 64QAM rates */
					gamma = (highMcsGamma << 13) | highMcsGamma;
				}
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, j, 32, &gamma);
			}
			for (i = 0, j = 0x5E; j <= 0x60; j++, i++) {
				if (ACMAJORREV_32(pi->pubpi->phy_rev)) {
					uint16 gammaOffset[3] = {0, 100, 150};
					gamma = (gammaOffset[i] << 13) | gammaOffset[i];
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, j, 32,
						&gamma);
				} else {
					uint16 gammaOffset[3] = {0, 0, 0};
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PAPR, 1, j, 32,
						&gammaOffset[i]);
				}
			}
		}
	} else {
		MOD_PHYREG(pi, papr_ctrl, papr_blk_en, enable);
	}
}

static void
wlc_phy_spurwar_nvshp_acphy(phy_info_t *pi, bool bw_chg, bool spurwar, bool nvshp)
{
	uint8 i, core;
	uint8 core_nv = 0, core_sp = 0;
	uint8 noise_var[PHY_CORE_MAX][ACPHY_SPURWAR_NV_NTONES];
	int8 tone_id[ACPHY_SPURWAR_NV_NTONES];
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;

	/* Initialize variables */
	for (i = 0; i < ACPHY_SPURWAR_NV_NTONES; i++) {
		tone_id[i]   = 0;
		FOREACH_CORE(pi, core)
			noise_var[core][i] = 0;
	}

	/* Table reset req or not */
	if (nvshp && !bw_chg && !spurwar)
		nvshp = FALSE;

	if (spurwar || nvshp) {
		/* Reset Table */
		wlc_phy_reset_noise_var_shaping_acphy(pi);

		/* Call nvshp */
		if (nvshp)
			wlc_phy_set_noise_var_shaping_acphy(pi, noise_var, tone_id, &core_nv);

		/* Call spurwar */
		if (spurwar)
			phy_ac_spurwar(pi_ac->rxspuri, noise_var, tone_id, &core_sp);

		/* Write table
		 * If both nvshp and spurwar tries to write same tone
		 * priority lies with spurwar
		 */
		wlc_phy_noise_var_shaping_acphy(pi, core_nv, core_sp, tone_id, noise_var, 0);
	}
}


/* Set up rx2tx rfseq tables differently for cal vs. packets for tiny */
/* to avoid problems with AGC lock-up */
void
phy_ac_rfseq_mode_set(phy_info_t *pi, bool cal_mode)
{
	if (cal_mode) {
		if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
				16, rfseq_majrev4_rx2tx_cal_cmd);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 112, 16,
				rfseq_majrev4_rx2tx_cal_dly);
		} else if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
				16, rfseq_majrev32_rx2tx_cal_cmd);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 112, 16,
				rfseq_majrev32_rx2tx_cal_dly);
		} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
				16, rfseq_majrev36_rx2tx_cal_cmd);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 112, 16,
				rfseq_majrev36_rx2tx_cal_dly);
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
				16, rfseq_rx2tx_cmd);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 112, 16,
				rfseq_rx2tx_dly);
		}
	} else {
		if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00, 16,
				tiny_rfseq_rx2tx_tssi_sleep_cmd);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 112, 16,
				tiny_rfseq_rx2tx_tssi_sleep_dly);
		} else if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00, 16,
				rfseq_majrev32_rx2tx_cmd);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70, 16,
				rfseq_majrev32_rx2tx_dly);
		} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x0, 16,
				rfseq_majrev36_rx2tx_cmd);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70, 16,
				rfseq_majrev36_rx2tx_dly);

			if ((CHSPEC_IS2G(pi->radio_chanspec) &&
				(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x1)) ||
				(CHSPEC_IS5G(pi->radio_chanspec) &&
				(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x4))) {

				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x0, 16,
					rfseq_majrev36_rx2tx_tssi_sleep_cmd);
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70, 16,
					rfseq_majrev36_rx2tx_tssi_sleep_dly);
			}
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00, 16,
				tiny_rfseq_rx2tx_cmd);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 112, 16,
				tiny_rfseq_rx2tx_dly);
		}
	}
}

static void
wlc_phy_radio20693_set_reset_table_bits(phy_info_t *pi, uint16 tbl_id, uint16 offset,
	uint16 start, uint16 end, uint16 val, uint8 tblwidth)
{
	uint16 val_shift, mask;
	uint32 data[2];

	val_shift = val << start;
	mask  = ((1 << (end + 1)) - (1 << start));
	wlc_phy_table_read_acphy(pi, tbl_id, 1, offset, tblwidth, &data);

	data[0] = ((data[0] & mask) | val_shift);
	wlc_phy_table_write_acphy(pi, tbl_id, 1, offset, tblwidth, &data);
}

#ifndef ACPHY_1X1_ONLY
static void
wlc_phy_write_tx_farrow_acphy(phy_ac_chanmgr_info_t *ci, chanspec_t chanspec)
{
	uint8	ch = CHSPEC_CHANNEL(chanspec), afe_clk_num, afe_clk_den;
	uint16	a, b, lb_b = 0;
	uint32	fcw, lb_fcw, tmp_low = 0, tmp_high = 0;
	uint32  deltaphase;
	uint16  deltaphase_lo, deltaphase_hi;
	uint16  farrow_downsamp;
	phy_info_t *pi = ci->pi;
	uint32	fc = wf_channel2mhz(ch, CHSPEC_IS2G(pi->radio_chanspec) ? WF_CHAN_FACTOR_2_4_G
	                                                               : WF_CHAN_FACTOR_5_G);

	if (pi->u.pi_acphy->dac_mode == 1) {
		if (CHSPEC_BW_LE20(chanspec)) {
			if (CHSPEC_IS5G(chanspec)) {
				if ((RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
				    !(ISSIM_ENAB(pi->sh->sih)) &&
				    (((((fc == 5180) && (pi->sh->chippkg != 2)) ||
				       ((fc >= 5200) && (fc <= 5320)) ||
				       ((fc >= 5745) && (fc <= 5825))) && !PHY_IPA(pi)))) {
					a = 10;
				} else if (((RADIOMAJORREV(pi) == 2) &&
				            ((fc == 5745) || (fc == 5765) || (fc == 5825 &&
				        !PHY_IPA(pi)))) && !(ISSIM_ENAB(pi->sh->sih))) {
					a = 18;
				} else {
					a = 16;
				}
			} else {
				if ((RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
				    !(ISSIM_ENAB(pi->sh->sih))) {
				    phy_ac_radio_data_t *r = phy_ac_radio_get_data(ci->aci->radioi);
			        if ((r->srom_txnospurmod2g == 0) && !PHY_IPA(pi)) {
						a = 9;
					} else if (((fc != 2412) && (fc != 2467)) ||
						(pi->xtalfreq == 40000000) ||
						(ACMAJORREV_2(pi->pubpi->phy_rev) &&
						(ACMINORREV_1(pi) ||
						ACMINORREV_3(pi) ||
						ACMINORREV_5(pi)) &&
						pi->xtalfreq == 37400000 && PHY_ILNA(pi))) {
						a = 18;
					} else {
						a = 16;
					}
				} else {
					a = 16;
				}
			}
			b = 160;
		} else if (CHSPEC_IS40(chanspec)) {
			if (CHSPEC_IS5G(chanspec)) {
				if ((RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
				       !PHY_IPA(pi) && (fc != 5190)) {
					a = 10;
				} else if ((RADIOMAJORREV(pi) == 2) &&
				       !PHY_IPA(pi) && (fc == 5190)) {
					a = 6;
				} else if (((RADIOMAJORREV(pi) == 2) &&
				     ((fc == 5755) || (fc == 5550 && pi->xtalfreq == 40000000) ||
				      (fc == 5310 && pi->xtalfreq == 37400000 && PHY_IPA(pi)))) &&
				    !(ISSIM_ENAB(pi->sh->sih))) {
					a = 9;
				} else {
					a = 8;
				}
			} else {
				if ((RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
				    !(ISSIM_ENAB(pi->sh->sih))) {
					a = 9;
				} else {
					a = 8;
				}
			}
			b = 320;
		} else {
			a = 6;
			b = 640;
		}
	} else if (pi->u.pi_acphy->dac_mode == 2) {
		a = 6;
		b = 640;
		lb_b = 320;
	} else {
		a = 8;
		b = 320;
		lb_b = 320;
	}

	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		afe_clk_num = 2;
		afe_clk_den = 3;
	} else {
		afe_clk_num = 3;
		afe_clk_den = 2;
		if (fc == 5290 && ACMAJORREV_2(pi->pubpi->phy_rev) &&
		    ((ACMINORREV_1(pi) && pi->sh->chippkg == 2) ||
		     ACMINORREV_3(pi)) && PHY_XTAL_IS37M4(pi)) {
			afe_clk_num = 4;
			afe_clk_den = 3;
		}
	}

	bcm_uint64_multiple_add(&tmp_high, &tmp_low, a * afe_clk_num * b,
		1 << 23, (fc * afe_clk_den) >> 1);
	bcm_uint64_divide(&fcw, tmp_high, tmp_low, fc * afe_clk_den);
	wlc_phy_tx_farrow_mu_setup(pi, fcw & 0xffff, (fcw & 0xff0000) >> 16, fcw & 0xffff,
		(fcw & 0xff0000) >> 16);
	/* DAC MODE 1 lbfarrow setup in rx_farrow_acphy */
	if (pi->u.pi_acphy->dac_mode != 1) {
		bcm_uint64_multiple_add(&tmp_high, &tmp_low, fc * afe_clk_den,
		        1 << 25, 0);
		bcm_uint64_divide(&lb_fcw, tmp_high, tmp_low, a * afe_clk_num * lb_b);
		deltaphase = (lb_fcw - 33554431) >> 1;
		deltaphase_lo = deltaphase & 0xffff;
		deltaphase_hi = (deltaphase >> 16) & 0xff;
		farrow_downsamp = fc * afe_clk_den / (a * afe_clk_num * lb_b);
		WRITE_PHYREG(pi, lbFarrowDeltaPhase_lo, deltaphase_lo);
		WRITE_PHYREG(pi, lbFarrowDeltaPhase_hi, deltaphase_hi);
		WRITE_PHYREG(pi, lbFarrowDriftPeriod, 5120);
		MOD_PHYREG(pi, lbFarrowCtrl, lb_farrow_downsampfactor, farrow_downsamp);
	}
}
#endif /* ACPHY_1X1_ONLY */

static void
wlc_phy_write_rx_farrow_acphy(phy_ac_chanmgr_info_t *ci, chanspec_t chanspec)
{
	uint16 deltaphase_lo, deltaphase_hi;
	uint8 ch = CHSPEC_CHANNEL(chanspec), num, den, bw, M, vco_div;
	uint32 deltaphase, farrow_in_out_ratio, fcw, tmp_low = 0, tmp_high = 0;
	uint16 drift_period, farrow_ctrl;
	uint8 farrow_outsft_reg, dec_outsft_reg, farrow_outscale_reg = 1;
	phy_info_t *pi = ci->pi;
	uint32 fc = wf_channel2mhz(ch, CHSPEC_IS2G(pi->radio_chanspec) ?
	        WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
	if (CHSPEC_IS80(chanspec)) {
		farrow_outsft_reg = 0;
		dec_outsft_reg = 0;
	} else {
		if (((ACMAJORREV_0(pi->pubpi->phy_rev)) && ((ACMINORREV_1(pi)) ||
		    (ACMINORREV_0(pi)))) || ((ACMAJORREV_1(pi->pubpi->phy_rev)) &&
		    (ACMINORREV_1(pi) || ACMINORREV_0(pi)))) {
			farrow_outsft_reg = 2;
		} else {
			farrow_outsft_reg = 0;
		}
		dec_outsft_reg = 3;
	}

	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		num = 3;
		den = 2;
	} else {
		num = 2;
		den = 3;
		if (CHSPEC_IS80(chanspec) && fc == 5290 && ACMAJORREV_2(pi->pubpi->phy_rev) &&
		    ((ACMINORREV_1(pi) && pi->sh->chippkg == 2) ||
		    ACMINORREV_3(pi)) && PHY_XTAL_IS37M4(pi)) {
			num = 3;
			den = 4;
		}
	}

	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		if ((RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) && !(ISSIM_ENAB(pi->sh->sih))) {
			if (CHSPEC_IS40(chanspec)) {
				bw = 40;
				M = 4;
				vco_div = 18;
				drift_period = 1920;
			} else {
				phy_ac_radio_data_t *r = phy_ac_radio_get_data(ci->aci->radioi);
				if ((r->srom_txnospurmod2g == 0) && !PHY_IPA(pi)) {
					bw = 20;
					M = 8;
					vco_div = 9;
					drift_period = 2880;
				} else if ((fc != 2412 && fc != 2467) ||
					(pi->xtalfreq == 40000000) ||
					(ACMAJORREV_2(pi->pubpi->phy_rev) &&
					(ACMINORREV_1(pi) ||
					ACMINORREV_3(pi) ||
					ACMINORREV_5(pi)) &&
					pi->xtalfreq == 37400000 && PHY_ILNA(pi))) {
					bw = 20;
					M = 8;
					vco_div = 18;
					drift_period = 5760;
				} else {
					bw = 20;
					M = 8;
					vco_div = 16;
					drift_period = 5120;
				}
			}
		} else {
			bw = 20;
			M = 8;
			vco_div = 16;
			drift_period = 5120;
		}
	} else {
		if (CHSPEC_IS80(chanspec)) {
			bw = 80;
			M = 4;
			vco_div = 6;
			drift_period = 2880;
			if (fc == 5290 && (RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
			    ((((RADIOMINORREV(pi) == 4) ||
			       (RADIOMINORREV(pi) == 10) ||
			       (RADIOMINORREV(pi) == 11) ||
			       (RADIOMINORREV(pi) == 13)) &&
			      (pi->sh->chippkg == 2)) ||
			     ((RADIOMINORREV(pi) == 7) ||
			     (RADIOMINORREV(pi) == 9) ||
			     (RADIOMINORREV(pi) == 8) ||
			     (RADIOMINORREV(pi) == 12))) &&
			    (pi->xtalfreq == 37400000)) {
				drift_period = 2560;
			}
		} else {
			if (RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) {
				if (CHSPEC_IS20(chanspec) &&
				    (((fc == 5180) && (pi->sh->chippkg != 2)) ||
				     ((fc >= 5200) && (fc <= 5320)) ||
				     ((fc >= 5745) && (fc <= 5825))) &&
				    !PHY_IPA(pi)) {
					bw = 20;
					M = 8;
					vco_div = 10;
					drift_period = 2400;
				} else if (CHSPEC_IS40(chanspec) && !PHY_IPA(pi) && (fc != 5190)) {
					bw = 20;
					M = 8;
					vco_div = 20;
					drift_period = 4800;
				} else if (CHSPEC_IS40(chanspec) && !PHY_IPA(pi) && (fc == 5190)) {
					bw = 20;
					M = 8;
					vco_div = 12;
					drift_period = 2880;
				} else if ((((fc == 5755 || (fc == 5550 &&
					pi->xtalfreq == 40000000) ||
					(fc == 5310 && pi->xtalfreq == 37400000 &&
					PHY_IPA(pi))) && (CHSPEC_IS40(chanspec))) ||
					((fc == 5745 || fc == 5765 ||
					(fc == 5825 && !PHY_IPA(pi))) &&
					(CHSPEC_IS20(chanspec)))) && !(ISSIM_ENAB(pi->sh->sih))) {
					bw = 20;
					M = 8;
					vco_div = 18;
					drift_period = 4320;
				} else {
					bw = 20;
					M = 8;
					vco_div = 16;
					drift_period = 3840;
				}
			} else {
				bw = 20;
				M = 8;
				vco_div = 16;
				drift_period = 3840;
			}
		}
	}
	bcm_uint64_multiple_add(&tmp_high, &tmp_low, fc * num, 1 << 25, 0);
	bcm_uint64_divide(&fcw, tmp_high, tmp_low, (uint32) (den * vco_div * M * bw));

	farrow_in_out_ratio = (fcw >> 25);
	deltaphase = (fcw - 33554431)>>1;
	deltaphase_lo = deltaphase & 0xffff;
	deltaphase_hi = (deltaphase >> 16) & 0xff;
	farrow_ctrl = (dec_outsft_reg & 0x3) | ((farrow_outscale_reg & 0x3) << 2) |
		((farrow_outsft_reg & 0x7) << 4) | ((farrow_in_out_ratio & 0x3) <<7);

	WRITE_PHYREG(pi, rxFarrowDeltaPhase_lo, deltaphase_lo);
	WRITE_PHYREG(pi, rxFarrowDeltaPhase_hi, deltaphase_hi);
	WRITE_PHYREG(pi, rxFarrowDriftPeriod, drift_period);
	WRITE_PHYREG(pi, rxFarrowCtrl, farrow_ctrl);
	MOD_PHYREG(pi, lbFarrowCtrl, lb_farrow_outShift, farrow_outsft_reg);
	MOD_PHYREG(pi, lbFarrowCtrl, lb_decimator_output_shift, dec_outsft_reg);
	MOD_PHYREG(pi, lbFarrowCtrl, lb_farrow_outScale, farrow_outscale_reg);
	/* Use the same settings for the loopback Farrow */
	if (pi->u.pi_acphy->dac_mode == 1) {
		WRITE_PHYREG(pi, lbFarrowDeltaPhase_lo, deltaphase_lo);
		WRITE_PHYREG(pi, lbFarrowDeltaPhase_hi, deltaphase_hi);
		WRITE_PHYREG(pi, lbFarrowDriftPeriod, drift_period);
		MOD_PHYREG(pi, lbFarrowCtrl, lb_farrow_downsampfactor, farrow_in_out_ratio);
	}
}

static void
wlc_phy_radio20695_etdac_pwrdown(phy_info_t *pi)
{
#ifndef WL_ETMODE
	if (!ET_ENAB(pi)) {
	ACPHY_REG_LIST_START
		MOD_RADIO_REG_28NM_ENTRY(pi, RF, AFE_CFG1_OVR2, 0, ovr_etdac_pu_diode, 1)
		MOD_RADIO_REG_28NM_ENTRY(pi, RF, AFE_CFG1_OVR2, 0, ovr_etdac_pu, 1)
		MOD_RADIO_REG_28NM_ENTRY(pi, RF, AFE_CFG1_OVR2, 0, ovr_etdacbuff_pu, 1)
		MOD_RADIO_REG_28NM_ENTRY(pi, RF, AFE_CFG1_OVR2, 0, ovr_etdac_reset, 1)

		MOD_RADIO_REG_28NM_ENTRY(pi, RF, ETDAC_CFG1, 0, etdac_pwrup_diode, 0)
		MOD_RADIO_REG_28NM_ENTRY(pi, RF, ETDAC_CFG1, 0, etdac_pwrup, 0)
		MOD_RADIO_REG_28NM_ENTRY(pi, RF, ETDAC_CFG1, 0, etdac_buf_pu, 0)
		MOD_RADIO_REG_28NM_ENTRY(pi, RF, ETDAC_CFG1, 0, etdac_reset, 0)

	ACPHY_REG_LIST_EXECUTE(pi);
	}
#endif
}

#ifndef WL_FDSS_DISABLED
static void
wlc_phy_fdss_init(phy_info_t *pi)
{
	uint8 core;
	FOREACH_CORE(pi, core) {
		MOD_PHYREGCEE(pi, txfdss_ctrl, core, txfdss_enable, 1);
		MOD_PHYREGCEE(pi, txfdss_ctrl, core, txfdss_interp_enable, pi->fdss_interp_en);
		MOD_PHYREGCEE(pi, txfdss_cfgtbl, core, txfdss_num_20M_tbl, 2);
		MOD_PHYREGCEE(pi, txfdss_cfgtbl, core, txfdss_num_40M_tbl, 2);
		MOD_PHYREGCEE(pi, txfdss_cfgbrkpt0_, core, txfdss_num_20M_breakpoints, 5);
		MOD_PHYREGCEE(pi, txfdss_cfgbrkpt0_, core, txfdss_num_40M_breakpoints, 5);
		MOD_PHYREGCEE(pi, txfdss_cfgbrkpt1_, core, txfdss_num_80M_breakpoints, 5);
		MOD_PHYREGCEE(pi, txfdss_scaleadj_en_, core, txfdss_scale_adj_enable, 7);
	}
}

static void
wlc_phy_set_fdss_table(phy_info_t *pi)
{
	uint8 core;
	uint8 nbkpts = 5;
	uint8 *fdss_tbl = NULL;
	uint8 *bkpt_tbl_20 = NULL;
	uint8 *bkpt_tbl_40 = NULL;
	uint8 *bkpt_tbl_80 = NULL;
	uint8 val = 0;
	uint8 mcstable[71] = {16, 16, 16, 16, 17, 17, 17, 17,
		16, 16, 16, 17, 17, 17, 17, 17,
		16, 16, 16, 17, 17, 17, 17, 17, 17, 17,
		16, 16, 16, 16, 17, 17, 17, 17,
		16, 16, 16, 17, 17, 17, 17, 17,
		16, 16, 16, 17, 17, 17, 17, 17, 17, 17,
		17,
		16, 16, 16, 17, 17, 17, 17, 17,
		16, 16, 16, 17, 17, 17, 17, 17, 17, 17,
		};
	uint8 mcstable_majorrev4[71] = {16, 16, 16, 0, 0, 0, 0, 0,
		16, 16, 16, 0, 0, 0, 0, 0,
		16, 16, 16, 0, 0, 0, 0, 0, 0, 0,
		16, 16, 16, 0, 0, 0, 0, 0,
		16, 16, 16, 0, 0, 0, 0, 0,
		16, 16, 16, 0, 0, 0, 0, 0, 0, 0,
		0,
		16, 16, 16, 0, 0, 0, 0, 0,
		16, 16, 16, 0, 0, 0, 0, 0, 0, 0,
		};
	uint8 mcstable_majorrev4_53574[71] = {16, 16, 16, 16, 16, 16, 0, 0,
		16, 16, 16, 16, 16, 0, 0, 0,
		16, 16, 16, 16, 16, 0, 0, 0, 0, 0,
		16, 16, 16, 16, 16, 16, 0, 0,
		16, 16, 16, 16, 16, 0, 0, 0,
		16, 16, 16, 16, 16, 0, 0, 0, 0, 0,
		16,
		16, 16, 16, 16, 16, 16, 0, 0,
		16, 16, 16, 16, 16, 0, 0, 0, 0, 0,
		};

	uint8 i, fdss_level[2];
	uint8 breakpoint_list_20[5] = {0, 3, 17, 48, 62};
	uint8 breakpoint_list_40[5] = {0, 6, 34, 96, 124};
	uint8 breakpoint_list_80[5] = {0, 12, 68, 192, 248};
	uint8 breakpoint_list_interp_20[2] = {47, 61};
	uint8 breakpoint_list_interp_40[2] = {97, 123};
	uint8 breakpoint_list_interp_80[2] = {191, 247};

	/* introducing new fdss table for 4359 */
	uint8 fdss_scale_level[5][5] = {{128, 128, 128, 128, 128},
		{128, 128, 128, 128, 128},
		{164, 146, 104, 146, 164}, /* Mild, meets older +1, -3 dB flatness limits */
		{180, 128, 72, 128, 180}, /* Extreme, meets older +3, -5 dB flatness limits */
		{170, 146, 85, 146, 170}  /* intermediate fdss coeff for 4359 */		};
	int16 fdss_scale_level_interp_20[5][5] = {{0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0},
		{-683, -338, 0, 338, 683},
		{-2219, -512, 0, 512, 2219},
		{-683, -338, 0, 338, 683}};
	int16 fdss_scale_level_interp_40[5][5] = {{0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0},
		{-341, -169, 0, 169, 341},
		{-1109, -256, 0, 256, 1109},
		{-341, -169, 0, 169, 341}};
	int16 fdss_scale_level_interp_80[5][5] = {{0, 0, 0, 0, 0},
		{0, 0, 0, 0, 0},
		{-171, -86, 0, 86, 171},
		{-555, -128, 0, 128, 555},
		{-171, -86, 0, 86, 171}};
	uint8 fdss_scale_level_adjust_20[5] = {128, 128, 132, 128, 128};
	uint8 fdss_scale_level_adjust_40[5] = {128, 128, 132, 128, 128};
	uint8 fdss_scale_level_adjust_80[5] = {128, 128, 134, 128, 128};
	uint8 fdss_scale_level_adjust_interp_20[5] = {128, 128, 132, 128, 128};
	uint8 fdss_scale_level_adjust_interp_40[5] = {128, 128, 131, 128, 128};
	uint8 fdss_scale_level_adjust_interp_80[5] = {128, 128, 134, 128, 128};

	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		fdss_level[0] = pi->fdss_level_2g[0];
		if (pi->fdss_level_2g[1] ==  -1) {
			fdss_level[1] = 0;
		} else {
			fdss_level[1] = pi->fdss_level_2g[1];
		}
	} else {
		fdss_level[0] = pi->fdss_level_5g[0];
		if (pi->fdss_level_5g[1] ==  -1) {
			fdss_level[1] = 0;
		} else {
			fdss_level[1] = pi->fdss_level_5g[1];
		}
	}
	FOREACH_CORE(pi, core) {
		if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FDSS_MCSINFOTBL(core),
				71, 0, 8, (ROUTER_4349(pi) ?
				mcstable_majorrev4_53574 : mcstable_majorrev4));
		} else {
			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_MCSINFOTBL(core), 71, 0, 8, mcstable);
		}
		bkpt_tbl_20 = breakpoint_list_20;
		bkpt_tbl_40 = breakpoint_list_40;
		bkpt_tbl_80 = breakpoint_list_80;

	/* Populate breakpoint and scale tables with the scale values for each BW */
		for (i = 0; i < 2; i++) {
			fdss_tbl = fdss_scale_level[fdss_level[i]];
			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_BREAKPOINTSTBL(core), nbkpts, nbkpts*i, 8,
				bkpt_tbl_20);
			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_SCALEFACTORSTBL(core), nbkpts, nbkpts*i, 8,
				fdss_tbl);
			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_SCALEADJUSTFACTORSTBL(core), 1, i, 8,
				&fdss_scale_level_adjust_20[fdss_level[i]]);

			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_BREAKPOINTSTBL(core), nbkpts,
				(2 * nbkpts) + nbkpts*i, 8,
				bkpt_tbl_40);
			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_SCALEFACTORSTBL(core), nbkpts,
				(2 * nbkpts) + nbkpts*i, 8,
				fdss_tbl);
			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_SCALEADJUSTFACTORSTBL(core), 1, i+2, 8,
				&fdss_scale_level_adjust_40[fdss_level[i]]);

			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_BREAKPOINTSTBL(core), nbkpts,
				(4 * nbkpts) + nbkpts*i, 8,
				bkpt_tbl_80);
			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_SCALEFACTORSTBL(core), nbkpts,
				(4 * nbkpts) + nbkpts*i, 8,
				fdss_tbl);
			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_FDSS_SCALEADJUSTFACTORSTBL(core), 1, i+4, 8,
				&fdss_scale_level_adjust_80[fdss_level[i]]);
		}

	/* Edit  breakpoint table for interpolation case */
		if (pi->fdss_interp_en) {
			for (i = 0; i < 2; i++) {
				wlc_phy_table_write_acphy(pi,
					ACPHY_TBL_ID_FDSS_BREAKPOINTSTBL(core),
					2, 3+5*i, 8, breakpoint_list_interp_20);
				wlc_phy_table_write_acphy(pi,
					ACPHY_TBL_ID_FDSS_SCALEFACTORSDELTATBL(core),
					5, 5*i, 16, fdss_scale_level_interp_20[fdss_level[i]]);
				wlc_phy_table_write_acphy(pi,
					ACPHY_TBL_ID_FDSS_SCALEADJUSTFACTORSTBL(core),
					1, i, 8, &fdss_scale_level_adjust_interp_20[fdss_level[i]]);

				wlc_phy_table_write_acphy(pi,
					ACPHY_TBL_ID_FDSS_BREAKPOINTSTBL(core),
					2, 13+5*i, 8, breakpoint_list_interp_40);
				wlc_phy_table_write_acphy(pi,
					ACPHY_TBL_ID_FDSS_SCALEFACTORSDELTATBL(core),
					5, 10+5*i, 16, fdss_scale_level_interp_40[fdss_level[i]]);
				wlc_phy_table_write_acphy(pi,
					ACPHY_TBL_ID_FDSS_SCALEADJUSTFACTORSTBL(core),
					1, i+2, 8,
					&fdss_scale_level_adjust_interp_40[fdss_level[i]]);

				wlc_phy_table_write_acphy(pi,
					ACPHY_TBL_ID_FDSS_BREAKPOINTSTBL(core),
					2, 23+5*i, 8, breakpoint_list_interp_80);
				wlc_phy_table_write_acphy(pi,
					ACPHY_TBL_ID_FDSS_SCALEFACTORSDELTATBL(core),
					5, 20+5*i, 16, fdss_scale_level_interp_80[fdss_level[i]]);
				wlc_phy_table_write_acphy(pi,
					ACPHY_TBL_ID_FDSS_SCALEADJUSTFACTORSTBL(core),
					1, i+4, 8,
					&fdss_scale_level_adjust_interp_80[fdss_level[i]]);
			}
		}
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FDSS_BREAKPOINTSTBL(core),
			1, 5*3*2, 8, &val);
		/* 5: table length; 3: 20MHz 40MHz and 80MHz; 2: low rate and high rate */
	}

}
#endif /* WL_FDSS_DISABLED */

static void
wlc_acphy_load_4349_specific_tbls(phy_info_t *pi)
{
	wlc_acphy_load_radiocrisscross_phyovr_mode(pi);
	wlc_acphy_load_logen_tbl(pi);
}

static void
wlc_acphy_load_radiocrisscross_phyovr_mode(phy_info_t *pi)
{
	uint8 core;
	FOREACH_CORE(pi, core) {
		WRITE_PHYREGCE(pi, AfeClkDivOverrideCtrlN, core, 0x0000);
		WRITE_PHYREGCE(pi, RfctrlAntSwLUTIdxN, core, 0x0000);
		WRITE_PHYREGCE(pi, RfctrlCoreTxPus, core,
			(READ_PHYREGCE(pi, RfctrlCoreTxPus, core) & 0x7DFF));
		WRITE_PHYREGCE(pi, RfctrlOverrideTxPus, core,
			(READ_PHYREGCE(pi, RfctrlOverrideTxPus, core) & 0xF3FF));
	}
}


static void wlc_acphy_load_logen_tbl(phy_info_t *pi)
{
	/* 4349BU */
	/* JIRA:SW4349-432 */
	if (ACMAJORREV_4(pi->pubpi->phy_rev))
		return;

	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		if (phy_get_phymode(pi) == PHYMODE_MIMO) {
			/* set logen mimodes pu */
			wlc_phy_radio20693_set_reset_table_bits(pi, ACPHY_TBL_ID_RFSEQ,
				0x14d, 1, 1, 0, 16);
			wlc_phy_radio20693_set_reset_table_bits(pi, ACPHY_TBL_ID_RFSEQ,
				0x15d, 1, 1, 1, 16);
			/* Set logen mimosrc pu */
			wlc_phy_radio20693_set_reset_table_bits(pi, ACPHY_TBL_ID_RFSEQ,
				0x14d, 4, 4, 1, 16);
			wlc_phy_radio20693_set_reset_table_bits(pi, ACPHY_TBL_ID_RFSEQ,
				0x15d, 4, 4, 0, 16);
		} else {
			/* set logen mimodes pu */
			wlc_phy_radio20693_set_reset_table_bits(pi, ACPHY_TBL_ID_RFSEQ,
				0x14d, 1, 1, 0, 16);
			wlc_phy_radio20693_set_reset_table_bits(pi, ACPHY_TBL_ID_RFSEQ,
				0x15d, 1, 1, 0, 16);
			/* Set logen mimosrc pu */
			wlc_phy_radio20693_set_reset_table_bits(pi, ACPHY_TBL_ID_RFSEQ,
				0x14d, 4, 4, 0, 16);
			wlc_phy_radio20693_set_reset_table_bits(pi, ACPHY_TBL_ID_RFSEQ,
				0x15d, 4, 4, 0, 16);
		}
	}
}

static void
wlc_phy_set_regtbl_on_band_change_acphy_20693(phy_info_t *pi)
{

	uint8 core = 0;

	ASSERT(RADIOID_IS(pi->pubpi->radioid, BCM20693_ID));

	if (RADIOMAJORREV(pi) == 3) {
		wlc_phy_radio20693_sel_logen_mode(pi);
		return;
	}

	FOREACH_CORE(pi, core)
	{
		if (CHSPEC_IS2G(pi->radio_chanspec))
		{
			phy_utils_write_radioreg(pi, RADIO_REG_20693(pi,
				TX_TOP_2G_OVR_EAST, core), 0x0);
			phy_utils_write_radioreg(pi, RADIO_REG_20693(pi,
				TX_TOP_2G_OVR1_EAST, core), 0x0);
			phy_utils_write_radioreg(pi, RADIO_REG_20693(pi,
				RX_TOP_2G_OVR_EAST, core), 0x0);
			phy_utils_write_radioreg(pi, RADIO_REG_20693(pi,
				RX_TOP_2G_OVR_EAST2, core), 0x0);
			if (PHY_IPA(pi)) {
				phy_utils_write_radioreg(pi, RADIO_REG_20693(pi,
					BG_TRIM2, core), 0x1937);
			}

			if (RADIOMAJORREV(pi) == 2) {
				MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR2, core,
					ovr_mix5g_lobuf_en, 0);
				MOD_RADIO_REG_20693(pi, LNA5G_CFG3, core, mix5g_lobuf_en, 0);
			}
			MOD_RADIO_REG_20693(pi, TIA_CFG8, core, tia_offset_dac_biasadj, 1);
			MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core, ovr_lna2g_tr_rx_en, 1);
			MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_tr_rx_en, 1);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_lna5g_tr_rx_en, 1);
			MOD_RADIO_REG_20693(pi, LNA5G_CFG1, core, lna5g_tr_rx_en, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core, ovr_gm2g_auxgm_pwrup, 1);
			MOD_RADIO_REG_20693(pi, LNA2G_CFG2, core, gm2g_auxgm_pwrup, 0);
			MOD_RADIO_REG_20693(pi, LOGEN_CFG2, core, logencore_5g_pu, 0);
			MOD_RADIO_REG_20693(pi, LOGEN_OVR1, core, ovr_logencore_5g_pu, 1);
			MOD_RADIO_REG_20693(pi, TX5G_CFG1, core, tx5g_bias_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR1, core, ovr_tx5g_bias_pu, 1);
			MOD_RADIO_REG_20693(pi, TXMIX5G_CFG4, core, mx5g_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR1, core, ovr_mx5g_pu, 1);
			MOD_RADIO_REG_20693(pi, TXMIX5G_CFG4, core, mx5g_pu_lodc_loop, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core, ovr_mx5g_pu_lodc_loop, 1);
			MOD_RADIO_REG_20693(pi, PA5G_CFG1, core, pa5g_bias_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR1, core, ovr_pa5g_bias_pu, 1);
			MOD_RADIO_REG_20693(pi, PA5G_CFG1, core, pa5g_bias_cas_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core, ovr_pa5g_bias_cas_pu, 1);
			MOD_RADIO_REG_20693(pi, PA5G_CFG4, core, pa5g_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR1, core, ovr_pa5g_pu, 1);

			if ((RADIO20693_MAJORREV(pi->pubpi->radiorev) == 1) &&
				(RADIO20693_MINORREV(pi->pubpi->radiorev) == 1)) {
				MOD_RADIO_REG_20693(pi, TRSW5G_CFG1, core, trsw5g_pu, 0);
				MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR1, core, ovr_trsw5g_pu, 1);
			}

			MOD_RADIO_REG_20693(pi, TX_LOGEN5G_CFG1, core, logen5g_tx_enable_5g, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR1, core, ovr_logen5g_tx_enable_5g, 1);
			MOD_RADIO_REG_20693(pi, TX_LOGEN5G_CFG1, core,
				logen5g_tx_enable_5g_low_band, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core,
				ovr_logen5g_tx_enable_5g_low_band, 1);
			MOD_RADIO_REG_20693(pi, LNA5G_CFG1, core, lna5g_lna1_pu, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_lna5g_lna1_pu, 1);
			MOD_RADIO_REG_20693(pi, LNA5G_CFG2, core, lna5g_pu_lna2, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_gm5g_pwrup, 1);
			MOD_RADIO_REG_20693(pi, LNA5G_RSSI1, core, lna5g_dig_wrssi1_pu, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_lna5g_dig_wrssi1_pu, 1);
			MOD_RADIO_REG_20693(pi, LNA5G_CFG2, core, lna5g_pu_auxlna2, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_lna5g_pu_auxlna2, 1);
			MOD_RADIO_REG_20693(pi, LNA5G_CFG3, core, mix5g_en, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_mix5g_en, 1);
			MOD_RADIO_REG_20693(pi, TX_LPF_CFG2, core, lpf_sel_5g_out_gm, 0);
			MOD_RADIO_REG_20693(pi, TX_LPF_CFG3, core, lpf_sel_2g_5g_cmref_gm, 0);
			/* Bimodal settings */
			if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
				MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
					ovr_rxmix2g_pu, 1);
				MOD_RADIO_REG_20693(pi, RXMIX2G_CFG1, core, rxmix2g_pu, 1);
				MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core,
					ovr_rxdiv2g_rs, 1);
				MOD_RADIO_REG_20693(pi, RXRF2G_CFG1, core, rxdiv2g_rs, 0);
				MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core,
					ovr_rxdiv2g_pu_bias, 1);
				MOD_RADIO_REG_20693(pi, RXRF2G_CFG1, core, rxdiv2g_pu_bias, 1);
				/* Turn off 5g overrides */
				MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
					ovr_mix5g_en, 0);
				MOD_RADIO_REG_20693(pi, LNA5G_CFG3, core, mix5g_en, 0);
				if (!(PHY_IPA(pi)) && (RADIO20693REV(pi->pubpi->radiorev) == 13)) {
					wlc_phy_set_bias_ipa_as_epa_acphy_20693(pi, core);
				}
			}
		}
		else
		{
			phy_utils_write_radioreg(pi, RADIO_REG_20693(pi, TX_TOP_5G_OVR1, core), 0);
			phy_utils_write_radioreg(pi, RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core), 0);
			if (PHY_IPA(pi) && !(ROUTER_4349(pi))) {
				phy_utils_write_radioreg(pi, RADIO_REG_20693(pi,
					BG_TRIM2, core), 0x1737);
			}

			if (RADIOMAJORREV(pi) == 2) {
				MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR2, core,
					ovr_mix5g_lobuf_en, 1);
				MOD_RADIO_REG_20693(pi, LNA5G_CFG3, core, mix5g_lobuf_en, 1);
			}
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_lna5g_lna1_pu, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_gm5g_pwrup, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_lna5g_dig_wrssi1_pu, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_lna5g_pu_auxlna2, 1);
			MOD_RADIO_REG_20693(pi, TIA_CFG8, core, tia_offset_dac_biasadj, 1);
			MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core, ovr_lna2g_tr_rx_en, 1);
			MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_tr_rx_en, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_lna5g_tr_rx_en,
				(ROUTER_4349(pi) ? 0 : 1));
			MOD_RADIO_REG_20693(pi, LNA5G_CFG1, core, lna5g_tr_rx_en,
				(ROUTER_4349(pi) ? 0 : 1));
			MOD_RADIO_REG_20693(pi, LOGEN_CFG2, core, logencore_2g_pu, 0);
			MOD_RADIO_REG_20693(pi, LOGEN_OVR1, core, ovr_logencore_2g_pu, 1);
			MOD_RADIO_REG_20693(pi, LNA2G_CFG2, core, gm2g_auxgm_pwrup, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core, ovr_gm2g_auxgm_pwrup, 1);
			MOD_RADIO_REG_20693(pi, LNA2G_CFG2, core, gm2g_pwrup, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core, ovr_gm2g_pwrup, 1);
			MOD_RADIO_REG_20693(pi, TX2G_CFG1, core, tx2g_bias_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR_EAST, core, ovr_tx2g_bias_pu, 1);
			MOD_RADIO_REG_20693(pi, TXMIX2G_CFG2, core, mx2g_bias_en, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR_EAST, core, ovr_mx2g_bias_en, 1);
			MOD_RADIO_REG_20693(pi, PA2G_CFG1, core, pa2g_bias_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR_EAST, core, ovr_pa2g_bias_pu, 1);
			MOD_RADIO_REG_20693(pi, PA2G_CFG1, core, pa2g_2gtx_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR_EAST, core, ovr_pa2g_2gtx_pu, 1);
			MOD_RADIO_REG_20693(pi, PA2G_IDAC2, core, pa2g_bias_cas_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR_EAST, core, ovr_pa2g_bias_cas_pu, 1);
			MOD_RADIO_REG_20693(pi, TX_LOGEN2G_CFG1, core, logen2g_tx_pu_bias, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR_EAST, core,
				ovr_logen2g_tx_pu_bias, 1);
			MOD_RADIO_REG_20693(pi, TX_LOGEN2G_CFG1, core, logen2g_tx_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR_EAST, core, ovr_logen2g_tx_pu, 1);
			MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core, ovr_rxmix2g_pu, 1);
			MOD_RADIO_REG_20693(pi, RXMIX2G_CFG1, core, rxmix2g_pu, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core,
				ovr_lna2g_dig_wrssi1_pu, 1);
			MOD_RADIO_REG_20693(pi, LNA2G_RSSI1, core, lna2g_dig_wrssi1_pu, 0);
			MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core, ovr_lna2g_lna1_pu, 1);
			MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_lna1_pu, 0);
			MOD_RADIO_REG_20693(pi, LNA5G_CFG3, core, mix5g_en, 1);
			MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core, ovr_mix5g_en, 1);
			MOD_RADIO_REG_20693(pi, LOGEN_OVR1, core, ovr_logencore_5g_pu, 0);
			MOD_RADIO_REG_20693(pi, TX_LPF_CFG2, core, lpf_sel_5g_out_gm, 1);
			MOD_RADIO_REG_20693(pi, TX_LPF_CFG3, core, lpf_sel_2g_5g_cmref_gm, 1);
			/* Bimodal settings */
			if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
				MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
					ovr_mix5g_en, 1);
				MOD_RADIO_REG_20693(pi, LNA5G_CFG3, core, mix5g_en, 1);
				/* Turn off 2G overrides */
				MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
					ovr_rxmix2g_pu, 0);
				MOD_RADIO_REG_20693(pi, RXMIX2G_CFG1, core, rxmix2g_pu, 0);
				MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core,
					ovr_rxdiv2g_rs, 0);
				MOD_RADIO_REG_20693(pi, RXRF2G_CFG1, core, rxdiv2g_rs, 0);
				MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core,
					ovr_rxdiv2g_pu_bias, 0);
				MOD_RADIO_REG_20693(pi, RXRF2G_CFG1, core,
					rxdiv2g_pu_bias, 0);
				if (!ROUTER_4349(pi) && PHY_IPA(pi))
					MOD_RADIO_REG_20693(pi, TXMIX5G_CFG8, core,
						pad5g_idac_gm, 58);


			}
		} /* band */
	} /* foreach core */

	/* JIRA: SWWLAN-67220 */
	if (phy_get_phymode(pi) == PHYMODE_MIMO) {
		wlc_phy_radio20693_mimo_core1_pmu_restore_on_bandhcange(pi);
	}

	/* JIRA: SW4349-1279. bg_trim2 settings need to be done before mini pmu cal */
	/* This is applicable only for 4349B0 variants */
	if (PHY_IPA(pi)) {
		if ((RADIO20693REV(pi->pubpi->radiorev) >= 0xE) &&
			(RADIO20693REV(pi->pubpi->radiorev) <= 0x12)) {
			FOREACH_CORE(pi, core) {
				phy_utils_write_radioreg(pi, RADIO_REG_20693(pi, BG_TRIM2, core),
					(CHSPEC_IS2G(pi->radio_chanspec)) ? 0x1937 : 0x1737);
			}
			/* minipmu_cal */
			wlc_phy_tiny_radio_minipmu_cal(pi);
		}
	}

	/* JIRA: SWWLAN-93908 SWWLAN-94230: MiniPMU settings for Rev3xx 53573 boards.
	 * P3xx series boards uses 1.5V input to the MiniPMU against the 1.4V used by the P23x
	 * series boards. This changes is required to align the mini pmu LDO voltages to 1.2V with a
	 * higher input voltage.
	 */
	if (ROUTER_4349(pi) && (((pi->sh->boardrev >> 8) & 0xf) >= 0x3)) {
		FOREACH_CORE(pi, core) {
			MOD_RADIO_REG_20693(pi, BG_TRIM2, core, bg_pmu_vbgtrim, 26);
			MOD_RADIO_REG_20693(pi, PMU_CFG1, core, wlpmu_vrefadj_cbuck, 6);
		}
		/* minipmu_cal */
		wlc_phy_tiny_radio_minipmu_cal(pi);
	}
}

static void
wlc_phy_load_channel_smoothing_tiny(phy_info_t *pi)
{

	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
	    /* set 64 48-bit entries */
		wlc_phy_table_write_tiny_chnsmth(pi,
			ACPHY_TBL_ID_CORE0CHANSMTH_FLTR,
			CHANSMTH_FLTR_LENGTH, 0, 48, acphy_Smth_tbl_4349);
		if (phy_get_phymode(pi) == PHYMODE_MIMO) {
			wlc_phy_table_write_tiny_chnsmth(pi,
				ACPHY_TBL_ID_CORE1CHANSMTH_FLTR,
				CHANSMTH_FLTR_LENGTH, 0, 48, acphy_Smth_tbl_4349);
		}
	} else {
	    const uint16 zero_table[3] = { 0, 0, 0 };
	    acphytbl_info_t tbl;
	    tbl.tbl_id = ACPHY_TBL_ID_CHANNELSMOOTHING_1x1;
		tbl.tbl_ptr = zero_table;
		tbl.tbl_len = 1;
		tbl.tbl_offset = 0;
		tbl.tbl_width = 48;
		/* clear 1st 128 48-bit entries */
		for (tbl.tbl_offset = 0; tbl.tbl_offset < 128; tbl.tbl_offset++) {
			wlc_phy_table_write_ext_acphy(pi, &tbl);
		}

		/* set next 64 48-bit entries */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_CHANNELSMOOTHING_1x1,
		                          CHANSMTH_FLTR_LENGTH, 128,
		                          tbl.tbl_width, acphy_Smth_tbl_tiny);

		/* clear next 64 48-bit entries */
		for (tbl.tbl_offset = 128 + (ARRAYSIZE(acphy_Smth_tbl_tiny) / 3);
		     tbl.tbl_offset < 256;
		     tbl.tbl_offset++) {
			wlc_phy_table_write_ext_acphy(pi, &tbl);
		}
	}
}
static void
wlc_phy_set_reg_on_reset_acphy_20693(phy_info_t *pi)
{
	BCM_REFERENCE(pi);
}

/* Initialize chip regs(RW) that get reset with phy_reset */
static void
wlc_phy_set_reg_on_reset_acphy(phy_info_t *pi)
{
	uint8 core;
	uint16 rxbias, txbias, temp;

	/* IQ Swap (revert swap happening in the radio) */
	if (!(RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) && !(ISSIM_ENAB(pi->sh->sih)) &&
	    !(ACMAJORREV_32(pi->pubpi->phy_rev) ||
	      ACMAJORREV_33(pi->pubpi->phy_rev) ||
	      ACMAJORREV_37(pi->pubpi->phy_rev))) {
		phy_utils_or_phyreg(pi, ACPHY_RxFeCtrl1(pi->pubpi->phy_rev), 7 <<
			ACPHY_RxFeCtrl1_swap_iq0_SHIFT(pi->pubpi->phy_rev));
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		FOREACH_CORE(pi, core) {
			switch (core) {
			case 0:
				MOD_PHYREG(pi, RxFeCtrl1, swap_iq0, 1);
				break;
			case 1:
				MOD_PHYREG(pi, RxFeCtrl1, swap_iq1, 1);
				break;
			case 2:
				MOD_PHYREG(pi, RxFeCtrl1, swap_iq2, 1);
				break;
			case 3:
				MOD_PHYREG(pi, RxFeCtrl1, swap_iq3, 1);
				break;
			default:
				PHY_ERROR(("%s: accessing more than 4 cores!\n",
					__FUNCTION__));
				ASSERT(0);
			}
		}

		MOD_PHYREG(pi, Core1TxControl, iqSwapEnable, 1);
		MOD_PHYREG(pi, Core2TxControl, iqSwapEnable, 1);
		MOD_PHYREG(pi, Core3TxControl, iqSwapEnable, 1);
		MOD_PHYREG(pi, Core4TxControl, iqSwapEnable, 1);
	}

	/* kimmer - add change from 0x667 to x668 very slight improvement */
	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		/* Retain reset value for 43012A0 */
		if (!(ACMAJORREV_36(pi->pubpi->phy_rev))) {
			WRITE_PHYREG(pi, DsssStep, 0x668);
		}
	}

	/* Avoid underflow trigger for loopback Farrow */
	MOD_PHYREG(pi, RxFeCtrl1, en_txrx_sdfeFifoReset, 1);

	if (ACMAJORREV_1(pi->pubpi->phy_rev) && (ACMINORREV_0(pi) || ACMINORREV_1(pi))) {
		MOD_PHYREG(pi, RxFeCtrl1, rxfe_bilge_cnt, 0);
	} else {
		MOD_PHYREG(pi, RxFeCtrl1, rxfe_bilge_cnt, 4);
	}

#ifdef WL_NAP
	/* Enable or disable Napping feature for 43012A0 */
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		if (PHY_NAP_ENAB(pi->sh->physhim)) {
			phy_ac_config_napping_28nm_ulp(pi);
		}
	}
#endif /* WL_NAP */

#ifdef WL_MU_RX
	MOD_PHYREG(pi, miscSigCtrl, mu_enable, 1);
	MOD_PHYREG(pi, miscSigCtrl, check_vht_siga_valid_mu, 0);
#endif /* WL_MU_RX */

	/* JIRA: SW4349-686 */
	MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);
	MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 0);

	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {

		/* JIRA: SW4349-985. SpareReg settings for 4349B0 */
		uint16 spare_reg = READ_PHYREG(pi, SpareReg);

		if (phy_get_phymode(pi) == PHYMODE_MIMO) {
			/* The targeted use case is mimo mode coremask 1 case.
			 * Below settings will turn off some of the blocks for core 1
			 * and thus resulting in current savings
			 */
			if (pi->sh->phyrxchain == 1) {
				/* bit #12: Limit hrp access to core0 alone. Should be
				   made 1 before m aking 1 bits 8,9,13 and should
				   be made 0 only after bits 8,9,13 are made 0.
				   Recommended value: 0x1
				 */
				WRITE_PHYREG(pi, SpareReg, (spare_reg & ~(1 << 12)));
				spare_reg = READ_PHYREG(pi, SpareReg);

				/* bit #8: Use core1 clk for second chain like
				   rsdb except div4 clk
				   Recommended value: 0x1
				 */
				spare_reg &= ~(1 << 8);
				/* bit #9: Turn off core1 divider in phy1rx1 */
				/* Recommended value: 0x1 */
				spare_reg &= ~(1 << 9);
				/* bit #13: Use core1 clk for second chain for div4 clk */
				/* Recommended value: 0x1 */
				spare_reg &= ~(1 << 13);
			}
			/* bit #10: Turn off core1 divider in RX2 */
			/* Recommended value: 0x1 */
			spare_reg &= ~(1 << 10);
		}

		/* bit #6: Duration control of Rx2tx reset to some designs. Enable always */
		spare_reg |= (1 << 6);

		/* bit #11: Turn off RX2 during TX */
		spare_reg |= (1 << 11);

		WRITE_PHYREG(pi, SpareReg, spare_reg);
	}

	/* JIRA: SW4349-712: 11b PER bump 4349A0,A2,B0 */
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, overideDigiGain1, cckdigigainEnCntValue, 0x6E);
	}

	if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
		/* Write 0x0 to RfseqMode to turn off both CoreActv_override */
		WRITE_PHYREG(pi, RfseqMode, 0);
	}

	if (ACMAJORREV_3(pi->pubpi->phy_rev) &&
		ACREV_GE(pi->pubpi->phy_rev, 20)) {
		  /* 4345C0 : temporarily configure rfseq lines as B1 (CRDOT11ACPHY-1004) */
		  MOD_PHYREG(pi, RxFeTesMmuxCtrl, lpf_gain_biq1_from_rfctrl, 0);
		  MOD_PHYREG(pi, AfePuCtrl, lna1_gain_bits_from_rfctrl, 0);
	}

	/* Enable 6-bit Carrier Sense Match Filter Mode for 4335C0 and 43602A0 */
	if ((ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) ||
	    (ACMAJORREV_2(pi->pubpi->phy_rev) && !ACMINORREV_0(pi)) ||
	    ACMAJORREV_3(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, CRSMiscellaneousParam, crsMfMode, 1);
	}

	/* Retain reset value for 43012A0 */
	if (!(ACMAJORREV_36(pi->pubpi->phy_rev))) {
		/* Turn on TxCRS extension.
		 * (Need to eventually make the 1.0 be native TxCRSOff (1.0us))
		 */
		WRITE_PHYREG(pi, dot11acphycrsTxExtension, 200);
	}

	/* Currently PA turns on 1us before first DAC sample. Decrease that gap to 0.5us */
	if ((ACMAJORREV_0(pi->pubpi->phy_rev)) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
	        WRITE_PHYREG(pi, TxRealFrameDelay, 146);
	}

	/* Retain reset value for 43012A0 */
	if (!(ACMAJORREV_36(pi->pubpi->phy_rev))) {
		/* This number combined with MAC RIFS results in 2.0us RIFS air time */
		WRITE_PHYREG(pi, TxRifsFrameDelay, 48);
	}

	si_core_cflags(pi->sh->sih, SICF_MPCLKE, SICF_MPCLKE);
	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
		wlc_phy_force_rfseq_acphy(pi, ACPHY_RFSEQ_RESET2RX);
	}
	/* allow TSSI loopback path to turn off */
	if (ACMAJORREV_1(pi->pubpi->phy_rev) || (ACMAJORREV_2(pi->pubpi->phy_rev) && PHY_IPA(pi))) {
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			if (((CHSPEC_BW_LE20(pi->radio_chanspec)) &&
			  (pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x1)) ||
			  ((CHSPEC_IS40(pi->radio_chanspec)) &&
			  (pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x2))) {
				MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 1);
			} else {
				MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 0);
			}
		} else {
			if (((CHSPEC_BW_LE20(pi->radio_chanspec)) &&
			  (pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x4)) ||
			  ((CHSPEC_IS40(pi->radio_chanspec)) &&
			  (pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x8)) ||
			  ((CHSPEC_IS80(pi->radio_chanspec)) &&
			  (pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x10))) {
				MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 1);
			} else {
				MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 0);
			}
		}
	} else if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 1);
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		if ((CHSPEC_IS2G(pi->radio_chanspec) &&
			(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x1)) ||
			(CHSPEC_IS5G(pi->radio_chanspec) &&
			(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x4))) {

			MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 1);
		} else {
			MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 0);
		}
	} else {
		MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 0);
	}

	/* In event of high power spurs/interference that causes crs-glitches,
	   stay in WAIT_ENERGY_DROP for 1 clk20 instead of default 1 ms.
	   This way, we get back to CARRIER_SEARCH quickly and will less likely to miss
	   actual packets. PS: this is actually one settings for ACI
	*/
	/* WRITE_PHYREG(pi, ACPHY_energydroptimeoutLen, 0x2); */

	/* Upon Reception of a High Tone/Tx Spur, the default 40MHz MF settings causes ton of
	   glitches. Set the MF settings similar to 20MHz uniformly. Provides Robustness for
	   tones (on-chip, on-platform, accidential loft coming from other devices)
	*/
	if (ACREV_GE(pi->pubpi->phy_rev, 32)) {
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		    ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			/*  Increase bphy digigain freeze time to 3 us */
			MOD_PHYREG(pi, overideDigiGain1, cckdigigainEnCntValue, 119);
			FOREACH_CORE(pi, core) {
				MOD_PHYREGCE(pi, crsControll, core, mfLessAve, 0);
				MOD_PHYREGCE(pi, crsControlu, core, mfLessAve, 0);
				MOD_PHYREGCE(pi, crsControllSub1, core, mfLessAve, 0);
				MOD_PHYREGCE(pi, crsControluSub1, core, mfLessAve, 0);
			}
		} else {
			/* Retain reset values for 43012A0 and 4347 */
			if (!(ACMAJORREV_36(pi->pubpi->phy_rev)) &&
					!(ACMAJORREV_40(pi->pubpi->phy_rev))) {
				ACPHY_REG_LIST_START
					MOD_PHYREG_ENTRY(pi, crsControlu0, mfLessAve, 0)
				ACPHY_REG_LIST_EXECUTE(pi);
				if ((wlc_phy_ac_phycap_maxbw(pi) > BW_20MHZ)) {
					ACPHY_REG_LIST_START
						MOD_PHYREG_ENTRY(pi, crsControll0, mfLessAve, 0)
						MOD_PHYREG_ENTRY(pi, crsControllSub10, mfLessAve, 0)
						MOD_PHYREG_ENTRY(pi, crsControluSub10, mfLessAve, 0)
					ACPHY_REG_LIST_EXECUTE(pi);
				}
			}
			if (PHYCORENUM((pi)->pubpi->phy_corenum) >= 2 &&
					!(ACMAJORREV_40(pi->pubpi->phy_rev))) {
				ACPHY_REG_LIST_START
					MOD_PHYREG_ENTRY(pi, crsControlu1, mfLessAve, 0)
				ACPHY_REG_LIST_EXECUTE(pi);
				if ((wlc_phy_ac_phycap_maxbw(pi) > BW_20MHZ)) {
					ACPHY_REG_LIST_START
						MOD_PHYREG_ENTRY(pi, crsControll1, mfLessAve, 0)
						MOD_PHYREG_ENTRY(pi, crsControllSub11, mfLessAve, 0)
						MOD_PHYREG_ENTRY(pi, crsControluSub11, mfLessAve, 0)
					ACPHY_REG_LIST_EXECUTE(pi);
				}
			}
			if (PHYCORENUM((pi)->pubpi->phy_corenum) >= 3) {
				ACPHY_REG_LIST_START
					MOD_PHYREG_ENTRY(pi, crsControlu2, mfLessAve, 0)
				ACPHY_REG_LIST_EXECUTE(pi);
				if ((wlc_phy_ac_phycap_maxbw(pi) > BW_20MHZ)) {
					ACPHY_REG_LIST_START
						MOD_PHYREG_ENTRY(pi, crsControll2, mfLessAve, 0)
						MOD_PHYREG_ENTRY(pi, crsControllSub12, mfLessAve, 0)
						MOD_PHYREG_ENTRY(pi, crsControluSub12, mfLessAve, 0)
					ACPHY_REG_LIST_EXECUTE(pi);
				}
			}
			if (PHYCORENUM((pi)->pubpi->phy_corenum) >= 4) {
				ACPHY_REG_LIST_START
					MOD_PHYREG_ENTRY(pi, crsControlu3, mfLessAve, 0)
				ACPHY_REG_LIST_EXECUTE(pi);
				if ((wlc_phy_ac_phycap_maxbw(pi) > BW_20MHZ)) {
					ACPHY_REG_LIST_START
						MOD_PHYREG_ENTRY(pi, crsControll3, mfLessAve, 0)
						MOD_PHYREG_ENTRY(pi, crsControllSub13, mfLessAve, 0)
						MOD_PHYREG_ENTRY(pi, crsControluSub13, mfLessAve, 0)
					ACPHY_REG_LIST_EXECUTE(pi);
				}
			}
		}
	} else {
		ACPHY_REG_LIST_START
			MOD_PHYREG_ENTRY(pi, crsControll, mfLessAve, 0)
			MOD_PHYREG_ENTRY(pi, crsControlu, mfLessAve, 0)
			MOD_PHYREG_ENTRY(pi, crsControllSub1, mfLessAve, 0)
			MOD_PHYREG_ENTRY(pi, crsControluSub1, mfLessAve, 0)
		ACPHY_REG_LIST_EXECUTE(pi);
	}
	if (!(ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev))) {
		if (ACREV_GE(pi->pubpi->phy_rev, 32)) {
			if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			    ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
				FOREACH_CORE(pi, core) {
					MOD_PHYREGCE(pi, crsThreshold2l, core, peakThresh, 85);
					MOD_PHYREGCE(pi, crsThreshold2u, core, peakThresh, 85);
					MOD_PHYREGCE(pi, crsThreshold2lSub1, core, peakThresh, 85);
					MOD_PHYREGCE(pi, crsThreshold2uSub1, core, peakThresh, 85);
				}
			} else {
				if (!(ACMAJORREV_36(pi->pubpi->phy_rev))) {
					ACPHY_REG_LIST_START
						MOD_PHYREG_ENTRY(pi, crsThreshold2u0,
							peakThresh, 85)
					ACPHY_REG_LIST_EXECUTE(pi);
					if ((wlc_phy_ac_phycap_maxbw(pi) > BW_20MHZ)) {
						ACPHY_REG_LIST_START
							MOD_PHYREG_ENTRY(pi, crsThreshold2l0,
								peakThresh, 85)
							MOD_PHYREG_ENTRY(pi, crsThreshold2lSub10,
								peakThresh, 85)
							MOD_PHYREG_ENTRY(pi, crsThreshold2uSub10,
								peakThresh, 85)
						ACPHY_REG_LIST_EXECUTE(pi);
					}
				}
				if (PHYCORENUM((pi)->pubpi->phy_corenum) >= 2) {
					ACPHY_REG_LIST_START
						MOD_PHYREG_ENTRY(pi, crsThreshold2u1,
							peakThresh, 85)
					ACPHY_REG_LIST_EXECUTE(pi);
					if ((wlc_phy_ac_phycap_maxbw(pi) > BW_20MHZ)) {
						ACPHY_REG_LIST_START
							MOD_PHYREG_ENTRY(pi, crsThreshold2l1,
								peakThresh, 85)
							MOD_PHYREG_ENTRY(pi, crsThreshold2lSub11,
								peakThresh, 85)
							MOD_PHYREG_ENTRY(pi, crsThreshold2uSub11,
								peakThresh, 85)
						ACPHY_REG_LIST_EXECUTE(pi);
					}
				}
				if (PHYCORENUM((pi)->pubpi->phy_corenum) >= 3) {
					ACPHY_REG_LIST_START
						MOD_PHYREG_ENTRY(pi, crsThreshold2u2,
							peakThresh, 85)
					ACPHY_REG_LIST_EXECUTE(pi);
					if ((wlc_phy_ac_phycap_maxbw(pi) > BW_20MHZ)) {
						ACPHY_REG_LIST_START
							MOD_PHYREG_ENTRY(pi, crsThreshold2l2,
								peakThresh, 85)
							MOD_PHYREG_ENTRY(pi, crsThreshold2lSub12,
								peakThresh, 85)
							MOD_PHYREG_ENTRY(pi, crsThreshold2uSub12,
								peakThresh, 85)
						ACPHY_REG_LIST_EXECUTE(pi);
					}
				}
				if (PHYCORENUM((pi)->pubpi->phy_corenum) >= 4) {
					ACPHY_REG_LIST_START
						MOD_PHYREG_ENTRY(pi, crsThreshold2u3,
							peakThresh, 85)
					ACPHY_REG_LIST_EXECUTE(pi);
					if ((wlc_phy_ac_phycap_maxbw(pi) > BW_20MHZ)) {
						ACPHY_REG_LIST_START
							MOD_PHYREG_ENTRY(pi, crsThreshold2l3,
								peakThresh, 85)
							MOD_PHYREG_ENTRY(pi, crsThreshold2lSub13,
								peakThresh, 85)
							MOD_PHYREG_ENTRY(pi, crsThreshold2uSub13,
								peakThresh, 85)
						ACPHY_REG_LIST_EXECUTE(pi);
					}
				}
			}
		} else {
			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, crsThreshold2l, peakThresh, 85)
				MOD_PHYREG_ENTRY(pi, crsThreshold2u, peakThresh, 85)
				MOD_PHYREG_ENTRY(pi, crsThreshold2lSub1, peakThresh, 85)
				MOD_PHYREG_ENTRY(pi, crsThreshold2uSub1, peakThresh, 85)
			ACPHY_REG_LIST_EXECUTE(pi);
		}
	} else {
		if (ACMAJORREV_5(pi->pubpi->phy_rev) && CHSPEC_IS20(pi->radio_chanspec)) {
			WRITE_PHYREG(pi, crsThreshold2u, 0x2055);
			WRITE_PHYREG(pi, crsThreshold2l, 0x2055);
		} else {
			WRITE_PHYREG(pi, crsThreshold2u, 0x204d);
			WRITE_PHYREG(pi, crsThreshold2l, 0x204d);
		}
		WRITE_PHYREG(pi, crsThreshold2lSub1, 0x204d);
		WRITE_PHYREG(pi, crsThreshold2uSub1, 0x204d);
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		FOREACH_CORE(pi, core) {
			MOD_PHYREGCE(pi, crshighlowpowThresholdl, core, low2highpowThresh, 69);
			MOD_PHYREGCE(pi, crshighlowpowThresholdu, core, low2highpowThresh, 69);
			MOD_PHYREGCE(pi, crshighlowpowThresholdlSub1, core, low2highpowThresh, 69);
			MOD_PHYREGCE(pi, crshighlowpowThresholduSub1, core, low2highpowThresh, 69);

			/* need to reduce CRS/FSTR threshold by 4dB to resolve
			   the hump around -80dB~-90dBm
			*/
			if (0) {
				MOD_PHYREGCE(pi, crshighlowpowThresholdl, core,
					low2highpowThresh, 65);
				MOD_PHYREGCE(pi, crshighlowpowThresholdu, core,
					low2highpowThresh, 65);
				MOD_PHYREGCE(pi, crshighlowpowThresholdlSub1, core,
				low2highpowThresh, 65);
				MOD_PHYREGCE(pi, crshighlowpowThresholduSub1, core,
				low2highpowThresh, 65);

				MOD_PHYREGCE(pi, crshighlowpowThresholdl, core,
					high2lowpowThresh, 71);
				MOD_PHYREGCE(pi, crshighlowpowThresholdu, core,
					high2lowpowThresh, 71);
				MOD_PHYREGCE(pi, crshighlowpowThresholdlSub1, core,
				high2lowpowThresh, 71);
				MOD_PHYREGCE(pi, crshighlowpowThresholduSub1, core,
				high2lowpowThresh, 71);
			}

			// This is to save power, the setting is applicable to all chips.
			MOD_PHYREGCE(pi, forceFront, core, freqCor, 0);
			MOD_PHYREGCE(pi, forceFront, core, freqEst, 0);

			// Old DCC related rf regs used for 4349/4345
			if (RADIOID(pi->pubpi->radioid) == BCM20693_ID) {
				MOD_RADIO_REG_20693(pi, RX_BB_2G_OVR_EAST, core,
				              ovr_tia_offset_comp_pwrup, 1);
				MOD_RADIO_REG_20693(pi, TIA_CFG15, core, tia_offset_comp_pwrup, 1);
			}

			/* No limit for now on max analog gain */
			MOD_PHYREGC(pi, HpFBw, core, maxAnalogGaindb, 100);
			MOD_PHYREGC(pi, DSSScckPktGain, core, dsss_cck_maxAnalogGaindb, 100);

			/* SW RSSI report calculation based on variance (DC is removed) */
			MOD_PHYREGCEE(pi, RxStatPwrOffset, core, use_gainVar_for_rssi, 1);

			/* need to reduce CSTR threshold by 4dB to resolve
			   the hump around -80dB~-90dBm
			*/
			if (0) {
				MOD_PHYREGCE(pi, STRPwrThreshL, core, strPwrThresh, 20);
				MOD_PHYREGCE(pi, STRPwrThreshU, core, strPwrThresh, 20);
				MOD_PHYREGCE(pi, STRPwrThreshLSub1, core, strPwrThresh, 20);
				MOD_PHYREGCE(pi, STRPwrThreshUSub1, core, strPwrThresh, 20);
			}
		}

		/* [4365] need to reduce FSTR threshold by 12dB to resolve
		   the hump around -80dB~-90dBm
		*/
		if (1) {
		  MOD_PHYREG(pi, FSTRHiPwrTh, finestr_hiPwrSm_th, 65);
		  MOD_PHYREG(pi, FSTRHiPwrTh, finestr_hiPwr_th, 51);
		}

		// Enable_bcm1_proprietary_256QAM
		MOD_PHYREG(pi, miscSigCtrl, brcm_11n_256qam_support, 0x1);

		// Make this same as TCL
		// 4365 needs to disable bphypredetection,
		// otherwise pktproc stuck at bphy when(AP) 20L is receiving ACK from(STA) 20
		MOD_PHYREG(pi, CRSMiscellaneousParam, bphy_pre_det_en, 0);
		MOD_PHYREG(pi, CRSMiscellaneousParam, b_over_ag_falsedet_en, 0x1);
		MOD_PHYREG(pi, bOverAGParams, bOverAGlog2RhoSqrth, 0x0);

		MOD_PHYREG(pi, CRSMiscellaneousParam, crsInpHold, 1);
		//MOD_PHYREG(pi, RxStatPwrOffset0, use_gainVar_for_rssi0, 1);

		if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
		// Taken from Iguanana rev40
		// temporarily disable dac_reset pulse to avoid OOB 8MHz spur
		// original setting acphy_write_table RFSeq {0x1a} 0xe8
			uint16 val = 0x0a;
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe8, 16, &val);

			val = 0x2c;
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe7, 16, &val);

			val = 0x4;
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x36e, 16,  &val);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x37e, 16,  &val);

			// Swap IQ
			if (!(ISSIM_ENAB(pi->sh->sih))) {
				MOD_PHYREG(pi, Core1TxControl, iqSwapEnable, 1);
				MOD_PHYREG(pi, Core2TxControl, iqSwapEnable, 1);
				// 7271 needs also swap on core 3 and core 4
				if (RADIOID(pi->pubpi->radioid) == BCM20696_ID) {
					MOD_PHYREG(pi, Core3TxControl, iqSwapEnable, 1);
					MOD_PHYREG(pi, Core4TxControl, iqSwapEnable, 1);
				}
			}
		} else {
			txbias = 0x2b; rxbias = 0x28;
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe8, 16,
			                          &txbias);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe7, 16,
			                          &rxbias);
		}

		// Tuned for ofdm PER humps
		WRITE_PHYREG(pi, HTAGCWaitCounters, 0x1020);

		// Linear filter compensation in fine timing:
		//   maps to C-model minkept settings for optimal ofdm sensitivity
		WRITE_PHYREG(pi, FSTRCtrl, 0x7aa);

		// Tiny specific: reason unknown
		MOD_PHYREG(pi, FFTSoftReset, lbsdadc_clken_ovr, 0);

		// Tiny specific: tune conversion gain in 20/40
		MOD_PHYREG(pi, RxSdFeConfig5, rx_farow_scale_value, 7);

		// Tiny specific: disable DVG2 to avoid bphy resampler saturation
		// used to avoid 1mbps performance issues due to DC offset. Required
		MOD_PHYREG(pi, RxSdFeConfig5, tiny_bphy20_ADC10_sel, 0);

		if (!ACMAJORREV_37(pi->pubpi->phy_rev)) {
			// Tiny specific: required for 1mbps performance
			MOD_PHYREG(pi, bphyTest, dccomp, 0);

			// Tiny specific: required for 1mbps performance
			MOD_PHYREG(pi, bphyFiltBypass, bphy_tap_20in20path_from_DVGA_en, 1);
		}

		// TXBF related regs
		//  c_param_wlan_bfe_user_index for implicit TXBF
		WRITE_PHYREG(pi, BfeConfigReg1, 0x1f);
		MOD_PHYREG(pi, BfrMuConfigReg0, bfrMu_delta_snr_mode, 2);

		// PHY capability relate regs
		MOD_PHYREG(pi, RfseqCoreActv2059, DisTx, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, RfseqCoreActv2059, DisRx, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, RfseqCoreActv2059, EnTx, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, RfseqCoreActv2059, EnRx, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, CoreConfig, CoreMask, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, CoreConfig, NumRxCores, pi->pubpi->phy_corenum);
		MOD_PHYREG(pi, HTSigTones, support_max_nss, pi->pubpi->phy_corenum);

		// DCC related regs
		MOD_PHYREG(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl, 0);

		if (ISSIM_ENAB(pi->sh->sih)) {
			MOD_PHYREG(pi, clip_detect_normpwr_var_mux,
			           dont_use_clkdiv4en_for_gaindsmpen, 1);
		}

		/* sparereg_1_for_div_1x1 = mclip_agc_ENABLED */
		MOD_PHYREG(pi, moved_from_sparereg, sparereg_1_for_div_1x1, 0);

		/* Don't scale ADC_pwr with dvga, as its casuing some
		   clip-failures for 80mhz at high pwrs (> -30dBm)
		*/
		MOD_PHYREG(pi, clip_detect_normpwr_var_mux, en_clip_detect_adc, 1);
	}

	if (ACMAJORREV_1(pi->pubpi->phy_rev) || ACMAJORREV_3(pi->pubpi->phy_rev)) {
		ACPHY_REG_LIST_START
			MOD_PHYREG_ENTRY(pi, crsThreshold2l, peakThresh, 77)
			MOD_PHYREG_ENTRY(pi, crsThreshold2u, peakThresh, 77)
			MOD_PHYREG_ENTRY(pi, crsThreshold2lSub1, peakThresh, 77)
			MOD_PHYREG_ENTRY(pi, crsThreshold2uSub1, peakThresh, 77)

			MOD_PHYREG_ENTRY(pi, crsacidetectThreshl, acidetectThresh, 0x80)
			MOD_PHYREG_ENTRY(pi, crsacidetectThreshlSub1, acidetectThresh, 0x80)
			MOD_PHYREG_ENTRY(pi, crsacidetectThreshu, acidetectThresh, 0x80)
			MOD_PHYREG_ENTRY(pi, crsacidetectThreshuSub1, acidetectThresh, 0x80)
			WRITE_PHYREG_ENTRY(pi, initcarrierDetLen,  0x40)
			WRITE_PHYREG_ENTRY(pi, clip1carrierDetLen, 0x5c)
		ACPHY_REG_LIST_EXECUTE(pi);

		if ((CHSPEC_IS2G(pi->radio_chanspec) && BF3_AGC_CFG_2G(pi->u.pi_acphy)) ||
			(CHSPEC_IS5G(pi->radio_chanspec) && BF3_AGC_CFG_5G(pi->u.pi_acphy))) {
			WRITE_PHYREG(pi, clip2carrierDetLen, 0x3a);
			WRITE_PHYREG(pi, defer_setClip1_CtrLen, 20);
		} else {
		        WRITE_PHYREG(pi, clip2carrierDetLen, 0x48);
			WRITE_PHYREG(pi, defer_setClip1_CtrLen, 24);
		}
		ACPHY_REG_LIST_START
			MOD_PHYREG_ENTRY(pi, clip_detect_normpwr_var_mux,
				use_norm_var_for_clip_detect, 0)
			MOD_PHYREG_ENTRY(pi, norm_var_hyst_th_pt8us, cck_gain_pt8us_en, 1)
			MOD_PHYREG_ENTRY(pi, CRSMiscellaneousParam, mf_crs_initgain_only, 1)
			/* disable bphyacidetEn as it is causing random rxper humps */
			MOD_PHYREG_ENTRY(pi, RxControl, bphyacidetEn, 0)

			WRITE_PHYREG_ENTRY(pi, RfseqCoreActv2059, 0x7717)
		ACPHY_REG_LIST_EXECUTE(pi);

		if (!ACMAJORREV_3(pi->pubpi->phy_rev) &&
			(ACMINORREV_0(pi) || ACMINORREV_1(pi))) {
			ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, HTSigTones, 0x9ee1)
				MOD_PHYREG_ENTRY(pi, CRSMiscellaneousParam, bphy_pre_det_en, 0)
				MOD_PHYREG_ENTRY(pi, bOverAGParams, bOverAGlog2RhoSqrth, 0)
				MOD_PHYREG_ENTRY(pi, CRSMiscellaneousParam, b_over_ag_falsedet_en,
					1)
			ACPHY_REG_LIST_EXECUTE(pi);
		} else {
			ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, HTSigTones, 0x9ee9)
				MOD_PHYREG_ENTRY(pi, CRSMiscellaneousParam, bphy_pre_det_en,
						(ACMAJORREV_3(pi->pubpi->phy_rev)) ? 0 : 0)
				MOD_PHYREG_ENTRY(pi, dot11acConfig, bphyPreDetTmOutEn, 0)
				/* digigain is not proper for low power bphy signals */
				/* causes kink near sensitivity region of 11mbps */
				/* fix is to increase cckshiftbitsRefVar by 1.5dB */
				/* WRITE_PHYREG_ENTRY(pi, cckshiftbitsRefVar, 46422) */
			ACPHY_REG_LIST_EXECUTE(pi);
			if (IS_4364_1x1(pi)) {
					/* fix is to increase cckshiftbitsRefVar by -1.5dB */
					WRITE_PHYREG(pi, cckshiftbitsRefVar, 0x4075);
				}
		}
		ACPHY_REG_LIST_START
			MOD_PHYREG_ENTRY(pi, FSTRCtrl, fineStrSgiVldCntVal,  0xb)
			MOD_PHYREG_ENTRY(pi, FSTRCtrl, fineStrVldCntVal, 0xa)

			MOD_PHYREG_ENTRY(pi, musigb2, mu_sigbmcs9, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb2, mu_sigbmcs8, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs7, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs6, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs5, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs4, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs3, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs2, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs1, 0x3)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs0, 0x2)
		ACPHY_REG_LIST_EXECUTE(pi);

	}

	if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		wlc_phy_set_lowpwr_phy_reg_rev3(pi);

		if (ACMAJORREV_5(pi->pubpi->phy_rev)) {
			MOD_PHYREG(pi, CRSMiscellaneousParam, bphy_pre_det_en, 0);
			if (IS_4364_3x3(pi)) {
				/* fix is to increase cckshiftbitsRefVar by +1.5dB */
				WRITE_PHYREG(pi, cckshiftbitsRefVar, 0x5b0c);
			}
		} else {
			ACPHY_REG_LIST_START
				/* Enable BPHY pre-detect */
				MOD_PHYREG_ENTRY(pi, RxControl, preDetOnlyinCS, 1)
				MOD_PHYREG_ENTRY(pi, dot11acConfig, bphyPreDetTmOutEn, 0)
				MOD_PHYREG_ENTRY(pi, CRSMiscellaneousParam, bphy_pre_det_en, 0)
				MOD_PHYREG_ENTRY(pi, bphyPreDetectThreshold0, ac_det_1us_min_pwr_0,
					350)
				WRITE_PHYREG_ENTRY(pi, cckshiftbitsRefVar, 46422)
			ACPHY_REG_LIST_EXECUTE(pi);
		}

		ACPHY_REG_LIST_START
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs6, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs5, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs4, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs3, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs2, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs1, 0x3)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs0, 0x2)
		ACPHY_REG_LIST_EXECUTE(pi);
	}

	WRITE_PHYREG(pi, RfseqMode, 0);

	/* Retain reset values for 43012A0 */
	if (!(ACMAJORREV_36(pi->pubpi->phy_rev))) {
		/* Disable Viterbi cache-hit low power featre for 4360
		 * since it is hard to meet 320 MHz timing
		 */
		MOD_PHYREG(pi, ViterbiControl0, CacheHitEn, ACMAJORREV_0(pi->pubpi->phy_rev)
					? 0 : 1);
	}

	/* Reset pktproc state and force RESET2RX sequence */
	wlc_phy_resetcca_acphy(pi);

	/* Try to fix the Tx2RX turnaround issue */
	if (0) {
		MOD_PHYREG(pi, RxFeStatus, sdfeFifoResetCntVal, 0xF);
		MOD_PHYREG(pi, RxFeCtrl1, resetsdFeInNonActvSt, 0x1);
	}

	/* Make TSSI to select Q-rail */
	if (ACREV_IS(pi->pubpi->phy_rev, 4) && CHSPEC_IS2G(pi->radio_chanspec))
		MOD_PHYREG(pi, TSSIMode, tssiADCSel, 0);
	else
		MOD_PHYREG(pi, TSSIMode, tssiADCSel, 1);

	/* Increase this by 10 ticks helps in getting rid of humps at high SNR, single core runs */
	if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
		WRITE_PHYREG(pi, defer_setClip2_CtrLen, 16);
	}

	MOD_PHYREG(pi, HTSigTones, support_gf, 0);

	/* Retain reset value for 43012A0 */
	if (!(ACMAJORREV_36(pi->pubpi->phy_rev))) {
		/* JIRA-CRDOT11ACPHY-273: SIG errror check For number of VHT symbols calculated */
		MOD_PHYREG(pi, partialAIDCountDown, check_vht_siga_length, 1);
	}

	/* Jira SWWLAN-31299: Disable checking of VHT-SIG-A reserved bits */
	MOD_PHYREG(pi, DmdCtrlConfig, check_vhtsiga_rsvd_bit, 0);

	FOREACH_CORE(pi, core) {
		MOD_PHYREGCE(pi, forceFront, core, freqCor, 1);
		MOD_PHYREGCE(pi, forceFront, core, freqEst, 1);
	}

	if (ROUTER_4349(pi)) {
		/* JIRA: SWWLAN-91943
		 * Issue: pktgainSettleLen of 0x30 (1.2us) is introducing PER humps at ~-62dBm
		 * Fix: Increasing the pktgainSettleLen to 0x40 (1.6us)
		 */
		WRITE_PHYREG(pi, pktgainSettleLen, 0x40);
	} else {
		WRITE_PHYREG(pi, pktgainSettleLen, 0x30);
	}

	if (ACMAJORREV_1(pi->pubpi->phy_rev) || ACMAJORREV_3(pi->pubpi->phy_rev)) {
		WRITE_PHYREG(pi, CoreConfig, 0x29);
		WRITE_PHYREG(pi, RfseqCoreActv2059, 0x1111);

		if (ACMAJORREV_1(pi->pubpi->phy_rev))
			wlc_phy_set_lowpwr_phy_reg(pi);
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev) && (CCT_INIT(pi->u.pi_acphy))) {
		/* Low pwr settings for radio */
		phy_ac_lp_enable(pi);
	}

	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		/* 4347A0 - forcing CoreConfig to 2x2, RTL default is 4x4 */
		WRITE_PHYREG(pi, CoreConfig, 0x43);
		WRITE_PHYREG(pi, RfseqCoreActv2059, 0x3333);
	}

	/* 4335:tkip macdelay & mac holdoff */
	if (!ACMAJORREV_0(pi->pubpi->phy_rev) && !ACMAJORREV_40(pi->pubpi->phy_rev)) {
		if ((ACMAJORREV_36(pi->pubpi->phy_rev))) {
			MOD_PHYREG(pi, TxMacIfHoldOff, holdoffval, TXMAC_IFHOLDOFF_43012A0);
			MOD_PHYREG(pi, TxMacDelay, macdelay, TXMAC_MACDELAY_43012A0);
		} else {
			WRITE_PHYREG(pi, TxMacIfHoldOff, TXMAC_IFHOLDOFF_DEFAULT);
			WRITE_PHYREG(pi, TxMacDelay, TXMAC_MACDELAY_DEFAULT);
		}
	}

	/* tiny radio specific processing */
	if (TINY_RADIO(pi)) {
		if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID))
			wlc_phy_set_reg_on_reset_acphy_20691(pi);
		else if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID))
			wlc_phy_set_reg_on_reset_acphy_20693(pi);
	}

	wlc_phy_mlua_adjust_acphy(pi, phy_btcx_is_btactive(pi->btcxi));
#ifndef WLC_DISABLE_ACI
	/* Setup HW_ACI block */
	if (!ACPHY_ENABLE_FCBS_HWACI(pi)) {
		if (((pi->sh->interference_mode_2G & ACPHY_ACI_HWACI_PKTGAINLMT) != 0) ||
		    ((pi->sh->interference_mode_5G & ACPHY_ACI_HWACI_PKTGAINLMT) != 0) ||
		    ((((pi->sh->interference_mode_2G & ACPHY_HWACI_MITIGATION) != 0) ||
		     ((pi->sh->interference_mode_5G & ACPHY_HWACI_MITIGATION) != 0)) &&
		     !(ACMAJORREV_32(pi->pubpi->phy_rev) ||
		       ACMAJORREV_33(pi->pubpi->phy_rev) ||
		       ACMAJORREV_37(pi->pubpi->phy_rev))))
			wlc_phy_hwaci_setup_acphy(pi, FALSE, TRUE);
		else
			wlc_phy_hwaci_setup_acphy(pi, FALSE, FALSE);
	}
#endif /* !WLC_DISABLE_ACI */

	/* 4335C0: Current optimization */
	if (ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) {
		ACPHY_REG_LIST_START
			WRITE_PHYREG_ENTRY(pi, FFTSoftReset, 0x2)
			WRITE_PHYREG_ENTRY(pi, fineclockgatecontrol, 0x0)
			WRITE_PHYREG_ENTRY(pi, RxFeTesMmuxCtrl, 0x60)
			MOD_PHYREG_ENTRY(pi, forceFront0, freqEst, 0)
			MOD_PHYREG_ENTRY(pi, forceFront0, freqCor, 0)
			MOD_PHYREG_ENTRY(pi, fineRxclockgatecontrol, forcedigigaingatedClksOn, 0)
		ACPHY_REG_LIST_EXECUTE(pi);
	}

	/* 4364_1x1: Current optimization */
	if (IS_4364_1x1(pi)) {
		WRITE_PHYREG(pi, FFTSoftReset, 0x2);
	}

	/* 43602: C-Model Parameters setting */
	if (ACMAJORREV_5(pi->pubpi->phy_rev)) {
		ACPHY_REG_LIST_START
			/* Turn ON 11n 256 QAM in 2.4G */
			WRITE_PHYREG_ENTRY(pi, miscSigCtrl, 0x203)
			WRITE_PHYREG_ENTRY(pi, HTAGCWaitCounters, 0x1028)

			/* WRITE_PHYREG_ENTRY(pi, bfeConfigReg1, 0x8) */

			WRITE_PHYREG_ENTRY(pi, crsThreshold2lSub1, 0x204d)
			WRITE_PHYREG_ENTRY(pi, crsThreshold2uSub1, 0x204d)

			/* Fine timing optimization for linear filter */
			WRITE_PHYREG_ENTRY(pi, FSTRCtrl, 0x7aa)
		ACPHY_REG_LIST_EXECUTE(pi);
	}

	if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		/* Low_power settings */
		WRITE_PHYREG(pi, RxFeTesMmuxCtrl, 0x60);
		/* Commenting out this low-power feature. Seen performance hit because of it.  */
		/* FOREACH_CORE(pi, core) { */
		/* 	MOD_PHYREGCE(pi, forceFront, core, freqCor, 0); */
		/* 	MOD_PHYREGCE(pi, forceFront, core, freqEst, 0); */
		/* } */
	}

	if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
		/* Helps for OFDM high-end hump due to W1 clamping */
		/* WRITE_PHYREG(pi, pktgainSettleLen, 0x33); */
		/* WRITE_PHYREG(pi, defer_setClip2_CtrLen, 13); */
		WRITE_PHYREG(pi, dssscckgainSettleLen, 0x65);
	}

	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		uint16 spare_reg;

		ACPHY_REG_LIST_START
			WRITE_PHYREG_ENTRY(pi, HTAGCWaitCounters, 0x2220)
			WRITE_PHYREG_ENTRY(pi, Core0_TargetVar_log2, 0x1c0)
			WRITE_PHYREG_ENTRY(pi, RxSdFeConfig1, 0x1)

			MOD_PHYREG_ENTRY(pi, dccal_control_10, dcoe_abort_threshold, 25)
			MOD_PHYREG_ENTRY(pi, dccal_control_10, idacc_tia_init_00, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_10, idacc_tia_init_01, 9)

			MOD_PHYREG_ENTRY(pi, dccal_control_20, idacc_tia_init_02, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_20, idacc_tia_init_03, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_20, idacc_tia_init_04, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_20, idacc_tia_init_05, 9)

			MOD_PHYREG_ENTRY(pi, dccal_control_30, idacc_tia_init_06, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_30, idacc_tia_init_07, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_30, idacc_tia_init_08, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_30, idacc_tia_init_09, 9)

			MOD_PHYREG_ENTRY(pi, dccal_control_40, idacc_tia_init_10, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_40, idacc_tia_init_11, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_40, idacc_tia_init_12, 9)
			MOD_PHYREG_ENTRY(pi, dccal_control_40, idacc_tia_init_13, 9)

			WRITE_PHYREG_ENTRY(pi, nvcfg2, 0x48f5)
			WRITE_PHYREG_ENTRY(pi, crsmfminpoweru0, 0x3131)
			WRITE_PHYREG_ENTRY(pi, crsminpoweru0, 0x2c31)
			WRITE_PHYREG_ENTRY(pi, crsminpoweru1, 0x2c2c)
			WRITE_PHYREG_ENTRY(pi, crsThreshold1u, 0xa5eb)
			WRITE_PHYREG_ENTRY(pi, crsminpoweroffset0, 0x505)
			WRITE_PHYREG_ENTRY(pi, crsmfminpoweroffset0, 0x505)
			WRITE_PHYREG_ENTRY(pi, Core0HpFBw, 0x3e9f)
			WRITE_PHYREG_ENTRY(pi, Core0DSSScckPktGain, 0x3f00)
			WRITE_PHYREG_ENTRY(pi, BfeConfigReg1, 0x8)

			MOD_PHYREG_ENTRY(pi, RfseqMode, mixer_first_mask_dis, 1)
			MOD_PHYREG_ENTRY(pi, FSTRCtrl, fineStrSgiVldCntVal, 0xa)
			MOD_PHYREG_ENTRY(pi, bOverAGParams, bOverAGlog2RhoSqrth, 0)
			MOD_PHYREG_ENTRY(pi, DcFiltAddress, dc_accum_wait, 3)
			MOD_PHYREG_ENTRY(pi, bphyTest, dccomp, 0)
			MOD_PHYREG_ENTRY(pi, bphyFiltBypass, bphy_tap_20in20path_from_DVGA_en, 1)
			MOD_PHYREG_ENTRY(pi, ultra_low_pwr, bphy_non_beacon_mode, 0x1)
			MOD_PHYREG_ENTRY(pi, forceFront0, spurcan_clk_en_slms0, 0x0)
			MOD_PHYREG_ENTRY(pi, forceFront0, spurcan_clk_en_slms1, 0x0)
			MOD_PHYREG_ENTRY(pi, forceFront0, spurcan_clk_en_fll, 0x0)
			MOD_PHYREG_ENTRY(pi, miscSigCtrl, brcm_11n_256qam_support, 0x1)
			MOD_PHYREG_ENTRY(pi, forceFront0, freqEst, 0)
			MOD_PHYREG_ENTRY(pi, forceFront0, freqCor, 0)
			MOD_PHYREG_ENTRY(pi, femctrl_override_control_reg,
					femctrl_override_control_reg, 0)
			MOD_PHYREG_ENTRY(pi, crsThreshold2u, peakThresh, 77)
			MOD_PHYREG_ENTRY(pi, crshighlowpowThresholdu, low2highpowThresh, 69)
			MOD_PHYREG_ENTRY(pi, crsminpoweru2, crsminpower4, 0x2c)
			MOD_PHYREG_ENTRY(pi, crsmfminpoweru1, crsmfminpower2, 0x31)
			MOD_PHYREG_ENTRY(pi, crsmfminpoweru1, crsmfminpower3, 0x31)
			MOD_PHYREG_ENTRY(pi, crsmfminpoweru2, crsmfminpower4, 0x31)

			/* Limit TIA index to 5 during packet gain */
			MOD_PHYREG_ENTRY(pi, Core0lpfQ, maxtiagainindx, 0x5)

			/* Bphy PER improvement (SW43012-948) */
			WRITE_PHYREG_ENTRY(pi, cckshiftbitsRefVar, 0x209c)
			WRITE_PHYREG_ENTRY(pi, Core0_BPHY_TargetVar_log2_pt8us, 479)
			MOD_PHYREG_ENTRY(pi, overideDigiGain1, cckdigigainEnCntValue, 119)

			/* NB high and low swap */
			MOD_PHYREG_ENTRY(pi, Core0_RSSIMuxSel2, wrssi3_sel_20, 0x1)
			MOD_PHYREG_ENTRY(pi, Core0_RSSIMuxSel2, wrssi3_sel_10, 0x2)
			MOD_PHYREG_ENTRY(pi, Core0_RSSIMuxSel2, wrssi3_sel_00, 0x3)
			MOD_PHYREG_ENTRY(pi, Core0_RSSIMuxSel2, nrssi_sel_20, 0x1)
			MOD_PHYREG_ENTRY(pi, Core0_RSSIMuxSel2, nrssi_sel_10, 0x2)
			MOD_PHYREG_ENTRY(pi, Core0_RSSIMuxSel2, nrssi_sel_00, 0x3)

			/* Reduce clip1 gain settle counter to avoid timing hit */
			WRITE_PHYREG_ENTRY(pi, clip1gainSettleLen, 40)

			/*
				RTT : bundleDacDiodePwrupEn should be 0 for DAC PU
				to take effect from RF Bundle cmds
			*/
			MOD_PHYREG_ENTRY(pi, Logen_AfeDiv_reset_select,
				bundleDacDiodePwrupEn, 0)
		ACPHY_REG_LIST_EXECUTE(pi);

		if (!ISSIM_ENAB(pi->sh->sih)) {
			MOD_PHYREG(pi, Core1TxControl, iqSwapEnable, 1);
		}

		txbias = 0x2b;
		rxbias = 0x28;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe8, 16,
		  &txbias);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe7, 16,
		  &rxbias);

		/* Switch Off ETDAC */
		wlc_phy_radio20695_etdac_pwrdown(pi);

		if (ACMINORREV_0(pi)) {
			/* Force BPHY clock (SW43012-1050) */
			MOD_PHYREG(pi, dacClkCtrl, forcebphyclkon, 1);
		} else {
			/* BPHY predetect bug fix : CRDOT11ACPHY-2073
			    SpareRegB0(8) = 1

			    ADC PU control from bundle : CRDOT11ACPHY-2071
			    Enable ADC PU from bundle SpareRegB0(9) = 1

			    DCC saturation check fix : CRDOT11ACPHY-2060
			    SpareRegB0(3) = 0
			*/
			spare_reg = (READ_PHYREG(pi, SpareRegB0) & 0xfff7) | (1 << 8) | (1 << 9);
			WRITE_PHYREG(pi, SpareRegB0, spare_reg);

			/* LNA protection override logic : CRDOT11ACPHY-1546
			    SpareReg(14) = 1
			*/
			spare_reg = READ_PHYREG(pi, SpareReg) | (1 << 14);
			WRITE_PHYREG(pi, SpareReg, spare_reg);
		}
	}

	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {

		temp = 0x2c;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe7, 16, &temp);
		temp = 0x0a;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe8, 16, &temp);

		temp = 0x4;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x36e, 16, &temp);
		temp = 0x4;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x37e, 16, &temp);
		if (!ISSIM_ENAB(pi->sh->sih)) {
			MOD_PHYREG(pi, Core1TxControl, iqSwapEnable, 1);
			MOD_PHYREG(pi, Core2TxControl, iqSwapEnable, 1);
		}
	}

	/* enable fix for bphy loft calibration issue CRDOT11ACPHY-378 */
	if (ACREV_GE(pi->pubpi->phy_rev, 6))
		MOD_PHYREG(pi, bphyTest, bphyTxfiltTrunc, 0);

	ACPHY_REG_LIST_START
		/* for: http://jira.broadcom.com/browse/SWWFA-10  */
		WRITE_PHYREG_ENTRY(pi, drop20sCtrl1, 0xc07f)

		/* phyrcs20S drop threshold -110 dBm */
		WRITE_PHYREG_ENTRY(pi, drop20sCtrl2, 0x64)
	ACPHY_REG_LIST_EXECUTE(pi);

		/* phyrcs40S drop threshold -110 dBm */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, drop20sCtrl3, phycrs40SpwrTh, 0x64);
	} else {
		ACPHY_REG_LIST_START
			WRITE_PHYREG_ENTRY(pi, drop20sCtrl3, 0x64)
		ACPHY_REG_LIST_EXECUTE(pi);
	}

	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {

		uint16 phymode = phy_get_phymode(pi);

		ACPHY_REG_LIST_START
			WRITE_PHYREG_ENTRY(pi, miscSigCtrl, 0x003)
			MOD_PHYREG_ENTRY(pi, CRSMiscellaneousParam, crsInpHold, 1)
			MOD_PHYREG_ENTRY(pi, crsThreshold2l, peakThresh, 77)
			MOD_PHYREG_ENTRY(pi, crsThreshold2u, peakThresh, 77)
			MOD_PHYREG_ENTRY(pi, crsThreshold2lSub1, peakThresh, 77)
			MOD_PHYREG_ENTRY(pi, crsThreshold2uSub1, peakThresh, 77)

			MOD_PHYREG_ENTRY(pi, crshighlowpowThresholdl, low2highpowThresh, 69)
			MOD_PHYREG_ENTRY(pi, crshighlowpowThresholdu, low2highpowThresh, 69)
			MOD_PHYREG_ENTRY(pi, crshighlowpowThresholdlSub1, low2highpowThresh, 69)
			MOD_PHYREG_ENTRY(pi, crshighlowpowThresholduSub1, low2highpowThresh, 69)

			MOD_PHYREG_ENTRY(pi, BfeConfigReg1, bfe_nvar_comp, 64)
			MOD_PHYREG_ENTRY(pi, BfeConfigNvarAdjustTblReg0,
				bfe_config_lut_noise_var0, 0)
			MOD_PHYREG_ENTRY(pi, BfeConfigNvarAdjustTblReg1,
				bfe_config_lut_noise_var1, 0)
			MOD_PHYREG_ENTRY(pi, BfeConfigNvarAdjustTblReg2,
				bfe_config_lut_noise_var2, 0)
			MOD_PHYREG_ENTRY(pi, BfeConfigNvarAdjustTblReg3,
				bfe_config_lut_noise_var3, 0)
			MOD_PHYREG_ENTRY(pi, BfeConfigNvarAdjustTblReg4,
				bfe_config_lut_noise_var4, 0)
			MOD_PHYREG_ENTRY(pi, BfeConfigNvarAdjustTblReg5,
				bfe_config_lut_noise_var5, 0)

			MOD_PHYREG_ENTRY(pi, dot11acConfig, bphyPreDetTmOutEn, 0)
			MOD_PHYREG_ENTRY(pi, bOverAGParams, bOverAGlog2RhoSqrth, 0)
			MOD_PHYREG_ENTRY(pi, CRSMiscellaneousParam, b_over_ag_falsedet_en, 1)
			WRITE_PHYREG_ENTRY(pi, cckshiftbitsRefVar, 46422)
			MOD_PHYREG_ENTRY(pi, RxStatPwrOffset0, use_gainVar_for_rssi0, 1)
			WRITE_PHYREG_ENTRY(pi, HTAGCWaitCounters, 0x1020)
			WRITE_PHYREG_ENTRY(pi, FSTRCtrl, 0x7aa)
			MOD_PHYREG_ENTRY(pi, FFTSoftReset, lbsdadc_clken_ovr, 0)
			MOD_PHYREG_ENTRY(pi, RxSdFeConfig5, rx_farow_scale_value, 7)
			MOD_PHYREG_ENTRY(pi, RxSdFeConfig5, tiny_bphy20_ADC10_sel, 0)
		ACPHY_REG_LIST_EXECUTE(pi);

		txbias = 0x2b;
		rxbias = 0x28;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe8, 16,
		                          &txbias);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe7, 16,
		                          &rxbias);

		MOD_PHYREG(pi, bphyTest, dccomp, 0);

		MOD_PHYREG(pi, RfseqCoreActv2059, DisTx, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, RfseqCoreActv2059, DisRx, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, RfseqCoreActv2059, EnTx, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, RfseqCoreActv2059, EnRx, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, CoreConfig, CoreMask, pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, CoreConfig, NumRxCores, pi->pubpi->phy_corenum);
		WRITE_PHYREG(pi, HTSigTones, 0x9ee9);
		MOD_PHYREG(pi, HTSigTones, support_max_nss, pi->pubpi->phy_corenum);
		MOD_PHYREG(pi, bphyFiltBypass, bphy_tap_20in20path_from_DVGA_en, 1);
		WRITE_PHYREG(pi, femctrl_override_control_reg, 0x0);

		FOREACH_CORE(pi, core) {
			MOD_RADIO_REG_20693(pi, RX_BB_2G_OVR_EAST, core,
			                  ovr_tia_offset_comp_pwrup, 1);
			MOD_RADIO_REG_20693(pi, TIA_CFG15, core, tia_offset_comp_pwrup, 1);
		}

		// DCC related regs
		MOD_PHYREG(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl, 1);

		if (phymode == PHYMODE_MIMO) {
			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, BfeConfigReg1,
						bfe_config_legacyUserIndex, 0x8)
				MOD_PHYREG_ENTRY(pi, dot11acConfig, bphyPreDetTmOutEn, 1)
				MOD_PHYREG_ENTRY(pi, CRSMiscellaneousParam, crsMfMode, 1)
				WRITE_PHYREG_ENTRY(pi, fineclockgatecontrol, 0x4000)
			ACPHY_REG_LIST_EXECUTE(pi);
			/* disableML if QT and MIMO mode */
			if (ISSIM_ENAB(pi->sh->sih) || !pi->u.pi_acphy->chanmgri->ml_en) {
				MOD_PHYREG(pi, RxControl, MLenable, 0);
			}
		} else if (phymode == PHYMODE_80P80) {
			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, crshighlowpowThresholdl1, low2highpowThresh,
					69)
				MOD_PHYREG_ENTRY(pi, crshighlowpowThresholdu1, low2highpowThresh,
					69)
				MOD_PHYREG_ENTRY(pi, crshighlowpowThresholdlSub11,
					low2highpowThresh, 69)
				MOD_PHYREG_ENTRY(pi, crshighlowpowThresholduSub11,
					low2highpowThresh, 69)
				MOD_PHYREG_ENTRY(pi, crsThreshold2u1, peakThresh, 77)

				MOD_PHYREG_ENTRY(pi, crsControll1, mfLessAve, 0)
				MOD_PHYREG_ENTRY(pi, crsControlu1, mfLessAve, 0)
				MOD_PHYREG_ENTRY(pi, crsControllSub11, mfLessAve, 0)
				MOD_PHYREG_ENTRY(pi, crsControluSub11, mfLessAve, 0)

				WRITE_PHYREG_ENTRY(pi, fineclockgatecontrol, 0x4000)
				MOD_PHYREG_ENTRY(pi, HTSigTones, support_max_nss, 0x1)
			ACPHY_REG_LIST_EXECUTE(pi);
		} else {
			WRITE_PHYREG(pi, fineclockgatecontrol, 0x0);
		}

		ACPHY_REG_LIST_START
			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, swap_iq1, 1)
			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, swap_iq2, 0)

			/* RfseqMode mixer_1st_dis is set to 1, so mixer 1st is not enabled */
			MOD_PHYREG_ENTRY(pi, RfseqMode, mixer_first_mask_dis, 1)
		ACPHY_REG_LIST_EXECUTE(pi);

		FOREACH_CORE(pi, core) {
			MOD_PHYREGCE(pi, forceFront, core, freqCor, 0);
			MOD_PHYREGCE(pi, forceFront, core, freqEst, 0);
		}
		ACPHY_REG_LIST_START
			/* Doppler related fix in channel update block */
			MOD_PHYREG_ENTRY(pi, ChanestCDDshift, dmd_chupd_use_mod_depend_mu, 1)
			WRITE_PHYREG_ENTRY(pi, chanupsym2, 0x050)
			WRITE_PHYREG_ENTRY(pi, mu_a_mod_ml_4, 0x4400)
			WRITE_PHYREG_ENTRY(pi, mu_a_mod_ml_5, 0x4444)
			/* Doppler related fix in Bphy LMS update block */
			WRITE_PHYREG_ENTRY(pi, CCKLMSStepSize, 0x1)
		ACPHY_REG_LIST_EXECUTE(pi);
		/* BPHY packet gain settle time . Removes Humps. */
		MOD_PHYREG(pi, overideDigiGain1, cckdigigainEnCntValue, 175);
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* phy_tx_active_cores {0 1 2 3} */
		MOD_PHYREG(pi, CoreConfig, CoreMask,   pi->pubpi->phy_coremask);
		MOD_PHYREG(pi, CoreConfig, NumRxCores, pi->pubpi->phy_corenum);

		/* RfseqMode mixer_1st_dis is set to 1, so mixer 1st is not enabled */
		MOD_PHYREG(pi, RfseqMode, mixer_first_mask_dis, 1);
	}

	/* 4360 & 43602 & 4365 & 7271 */
	if (ACMAJORREV_0(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev) ||
	    ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* Increase timing search timeout to reduce number of glitches under 64k,
		   if glitches > 64k, they are wrapped around to a low number. 12 us --> 13 us
		*/
	        WRITE_PHYREG(pi, timingsearchtimeoutLen, 520);   /* 13 / 0.025 */
	}

	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, clip_detect_normpwr_var_mux, dont_use_clkdiv4en_for_gaindsmpen, 1);
		MOD_PHYREG(pi, clip_detect_normpwr_var_mux, use_extclkdiv4en_for_fsen, 1);
		MOD_PHYREG(pi, clip_detect_normpwr_var_mux, en_clip_detect_adc, 1);

		MOD_PHYREG(pi, SlnaControl, inv_btcx_prisel_polarity, 1);

		MOD_PHYREG(pi, dccal_common, dcc_method_select, 0);
		MOD_PHYREG(pi, radio_pu_seq, dcc_tia_dac_method_select, 1);
		FOREACH_CORE(pi, core) {
			MOD_PHYREGCE(pi, dccal_control_16, core, idact_bypass, 1);
			MOD_PHYREGCE(pi, dccal_control_16, core, dcoe_bypass, 1);
			MOD_PHYREGCE(pi, dccal_control_14, core, idacc_bypass, 1);
		}
		MOD_PHYREG(pi, CRSMiscellaneousParam, crsInpHold, 1);

		//MOD_PHYREG(pi, overideDigiGain1, cckdigigainEnCntValue, 200);

	}

	if (IS_4364_3x3(pi) && CHSPEC_IS2G(pi->radio_chanspec))
		MOD_PHYREG(pi, crsThreshold1u, autoThresh, 240);

	if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* JIRA:CRDOT11ACPHY-2437
		   Do not reset ADC clock divider at reset2rx as it can lead to a deadlock, 
		   where Rx FIFO is empty, generating Rx stall and ADC is not filling RxFIFO
		   due to divider held in reset */               
		MOD_PHYREG(pi, Logen_AfeDiv_reset_select, rst2rx_afediv_arst_val, 0);
	}

#ifdef WL_MU_RX
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* MU-MIMO STA related stuff */

		MOD_PHYREG(pi, miscSigCtrl, mu_enable, 1);
		MOD_PHYREG(pi, miscSigCtrl, check_vht_siga_valid_mu, 0);

		WRITE_PHYREG(pi, mu_a_mumimo_ltrn_dependent, 0x0);
	}
#endif /* WL_MU_RX */
}


/* Initialize chip tbls(reg-based) that get reset with phy_reset */
static void
wlc_phy_set_tbl_on_reset_acphy(phy_info_t *pi)
{
	uint8 stall_val;
	phy_info_acphy_t *pi_ac;
	phy_ac_radio_data_t *rdata;
	uint16 adc_war_val = 0x20, pablowup_war_val = 120;
	uint8 core;
	uint16 gmult20, gmult40, gmult80;
	uint16 rfseq_bundle_tssisleep48[3];
	uint16 rfseq_bundle_48[3];
	const void *data, *dly;
	uint32 read_val[2], write_val[2];
	/* uint16 AFEdiv_read_val = 0x0000; */

	bool ext_pa_ana_2g =  ((BOARDFLAGS2(GENERIC_PHY_INFO(pi)->boardflags2) &
		BFL2_SROM11_ANAPACTRL_2G) != 0);
	bool ext_pa_ana_5g =  ((BOARDFLAGS2(GENERIC_PHY_INFO(pi)->boardflags2) &
		BFL2_SROM11_ANAPACTRL_5G) != 0);

	/* DEBUG: TEST CODE FOR SS PTW70 DEBUG */
	uint32 war_val = 0x7ffffff;
	uint8 offset;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);
	pi_ac = pi->u.pi_acphy;
	rdata = phy_ac_radio_get_data(pi_ac->radioi);
	/* Load settings related to antswctrl if not on QT */
	if (!ISSIM_ENAB(pi->sh->sih)) {
		wlc_phy_set_regtbl_on_femctrl(pi);
	}
	/* Quickturn only init */
	if (ISSIM_ENAB(pi->sh->sih)) {
		uint8 ncore_idx;
		uint16 val;
		uint16 init_gain_code_A = 0x16a, init_gain_code_B = 0x24;
		uint16 rfseq_val1 = 0x124d, rfseq_val2 = 0x62;

		if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			val = 64;
		} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			val = 50;
			init_gain_code_A = 0x49a;
			init_gain_code_B = 0x2044;

			if (CHSPEC_BW_LE20(pi->radio_chanspec)) {
				MOD_PHYREG(pi, DcFiltAddress, dcBypass, 1);
			}
		} else {
		   /* changing to TCL value */
		   val = 50;
		   if (CHSPEC_IS20(pi->radio_chanspec)) {
			  MOD_PHYREG(pi, DcFiltAddress, dcBypass, 1);
		   }

		}

		FOREACH_CORE(pi, ncore_idx) {
			wlc_phy_set_tx_bbmult_acphy(pi, &val, ncore_idx);
		}

		/* dummy call to satisfy compiler */
		wlc_phy_get_tx_bbmult_acphy(pi, &val, 0);

		/* on QT: force the init gain to allow noise_var not limiting 256QAM performance */
		ACPHYREG_BCAST(pi, Core0InitGainCodeA, init_gain_code_A);
		ACPHYREG_BCAST(pi, Core0InitGainCodeB, init_gain_code_B);

		if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
				0xf9, 16, &rfseq_val1);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
				0xf6, 16, &rfseq_val2);
		} else {
			FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
					0xf9 + core, 16, &qt_rfseq_val1[core]);
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
					0xf6 + core, 16, &qt_rfseq_val2[core]);
			}
		}
	}

	/* Update gmult, dacbuf after radio init */
	/* Tx Filters */
	if (!(ACMAJORREV_37(pi->pubpi->phy_rev))) {
		/* 7271A0 has these not connected via RFSEQ */
		wlc_phy_set_analog_tx_lpf(pi, 0x1ff, -1, -1, -1, rdata->rccal_gmult,
		                          rdata->rccal_gmult_rc, -1);
		wlc_phy_set_tx_afe_dacbuf_cap(pi, 0x1ff, rdata->rccal_dacbuf, -1, -1);
	}

	/* Rx Filters */
	if (ACMAJORREV_0(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		/* 4360 (tighten rx analog filters). Note than 80mhz filter cutoff
		   was speced at 39mhz (should have been 38.5)
		   C-model desired bw : {9, 18.5, 38.5}  @ 3dB cutoff
		   lab-desired (freq offset + 5%PVT): {9.5, 20, 41}
		   with gmult = 193 (in 2069_procs.tcl), we get {11, 23.9, 48.857}
		   Reduce bw by factor : {9.5/11, 20/23.9, 41/48.857} = {0.863, 0.837, 0.839}
		*/
		gmult20 = (rdata->rccal_gmult * 221) >> 8;     /* gmult * 0.863 */
		gmult40 = (rdata->rccal_gmult * 215) >> 8;     /* gmult * 0.839 (~ 0.837) */
		gmult80 = (rdata->rccal_gmult * 215) >> 8;     /* gmult * 0.839 */
	} else if (ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) {
		/* 4335C0 (tighten rx analog filter for 80mhz only).
		   This is needed to take away
		   hump which comes because of ACI causing false clip_hi
		*/
		gmult20 = rdata->rccal_gmult;
		gmult40 = rdata->rccal_gmult;
		if (!(PHY_ILNA(pi))) {
			gmult80 = (rdata->rccal_gmult * 225) >> 8;     /* gmult * 0.879 */
		} else {
			gmult80 = rdata->rccal_gmult;
		}
	} else {
		gmult20 = rdata->rccal_gmult;
		gmult40 = rdata->rccal_gmult;
		gmult80 = rdata->rccal_gmult;
	}
	wlc_phy_set_analog_rx_lpf(pi, 1, -1, -1, -1, gmult20, rdata->rccal_gmult_rc, -1);
	/* 43012: No need to progam for 40 and 80 MHz */
	if (!(ACMAJORREV_36(pi->pubpi->phy_rev))) {
		wlc_phy_set_analog_rx_lpf(pi, 2, -1, -1, -1, gmult40, rdata->rccal_gmult_rc, -1);
		wlc_phy_set_analog_rx_lpf(pi, 4, -1, -1, -1, gmult80, rdata->rccal_gmult_rc, -1);
	}

	/* Reset2rx sequence */
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		/* JIRA: SW4349-456. 4349: phy stall issue resulting in failure in ibss creation */
		data = rfseq_majrev4_reset2rx_cmd;
		dly = rfseq_majrev4_reset2rx_dly;
	} else if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
		data = rfseq_reset2rx_cmd;
		dly = rfseq_majrev3_reset2rx_dly;
	} else if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
		data = rfseq_majrev32_reset2rx_cmd;
		dly = rfseq_majrev32_reset2rx_dly;
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		data = rfseq_majrev36_reset2rx_cmd;
		dly = rfseq_majrev36_reset2rx_dly;
	} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
		data = rfseq_majrev37_reset2rx_cmd;
		dly = rfseq_majrev37_reset2rx_dly;
	} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		data = rfseq_majrev40_reset2rx_cmd;
		dly = rfseq_majrev40_reset2rx_dly;
	} else {
		data = rfseq_reset2rx_cmd;
		dly = rfseq_reset2rx_dly;
	}
	if (data && dly) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x20, 16, data);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x90, 16, dly);
	}

	if (!(ACMAJORREV_32(pi->pubpi->phy_rev)) &&
	    !(ACMAJORREV_33(pi->pubpi->phy_rev)) &&
	    !(ACMAJORREV_36(pi->pubpi->phy_rev)) &&
	    !(ACMAJORREV_37(pi->pubpi->phy_rev)) &&
	    !(ACMAJORREV_40(pi->pubpi->phy_rev))) {
		/* during updateGainL make sure the lpf/tia hpc corner is set
			properly to optimum setting
		*/
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 2, 0x121, 16,
				rfseq_updl_lpf_hpc_ml);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 2, 0x131, 16,
				rfseq_updl_lpf_hpc_ml);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 2, 0x124, 16,
				rfseq_updl_tia_hpc_ml);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 2, 0x137, 16,
				rfseq_updl_tia_hpc_ml);
	}

	/* tx2rx/rx2tx: Remove SELECT_RFPLL_AFE_CLKDIV/RESUME as we are not in boost mode */
	if (ACMAJORREV_1(pi->pubpi->phy_rev) || (ACMAJORREV_2(pi->pubpi->phy_rev) && PHY_IPA(pi))) {
		if ((CHSPEC_IS2G(pi->radio_chanspec) &&
			(((CHSPEC_IS20(pi->radio_chanspec)) &&
			(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x1)) ||
			((CHSPEC_IS40(pi->radio_chanspec)) &&
			(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x2)))) ||
			(CHSPEC_IS5G(pi->radio_chanspec) &&
			(((CHSPEC_IS20(pi->radio_chanspec)) &&
			(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x4)) ||
			((CHSPEC_IS40(pi->radio_chanspec)) &&
			(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x8)) ||
			((CHSPEC_IS80(pi->radio_chanspec)) &&
			(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x10))))) {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
					16, rfseq_rx2tx_cmd_withtssisleep);
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
					16, rfseq_rx2tx_dly_withtssisleep);
				MOD_PHYREG(pi, RfBiasControl, tssi_sleep_bg_pulse_val, 1);
				MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 1);
				rfseq_bundle_tssisleep48[0] = 0x0000;
				rfseq_bundle_tssisleep48[1] = 0x20;
				rfseq_bundle_tssisleep48[2] = 0x0;
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 0, 48,
					rfseq_bundle_tssisleep48);
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
				16, rfseq_rx2tx_cmd);
		}
	} else if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00, 16,
		                          tiny_rfseq_rx2tx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 112, 16,
		                          tiny_rfseq_rx2tx_dly);
	} else if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00, 16,
		                          tiny_rfseq_rx2tx_tssi_sleep_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 112, 16,
		                          tiny_rfseq_rx2tx_tssi_sleep_dly);
		MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 1);
	} else if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00, 16,
		                          rfseq_majrev32_rx2tx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70, 16,
		                          rfseq_majrev32_rx2tx_dly);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x10, 16,
		                          rfseq_majrev32_tx2rx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x80, 16,
		                          rfseq_majrev32_tx2rx_dly);
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		const uint16 rx_adc = 0xE0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x0, 16,
		                          rfseq_majrev36_rx2tx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70, 16,
		                          rfseq_majrev36_rx2tx_dly);
		if ((CHSPEC_IS2G(pi->radio_chanspec) &&
			(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x1)) ||
			(CHSPEC_IS5G(pi->radio_chanspec) &&
			(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x4))) {

			MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 1);

			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x0, 16,
				rfseq_majrev36_rx2tx_tssi_sleep_cmd);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70, 16,
				rfseq_majrev36_rx2tx_tssi_sleep_dly);
		}
		/* make clkgen pu , ref pu, pu cmbuf to 1 for rx2tx mode. */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x145, 16,
			&rx_adc);
	} else if (ACMAJORREV_2(pi->pubpi->phy_rev) && !(PHY_IPA(pi))) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
			16, rfseq_rx2tx_cmd_noafeclkswitch);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
			16, rfseq_rx2tx_cmd_noafeclkswitch_dly);
	}  else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
			16, rfseq_majrev37_rx2tx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
			16, rfseq_majrev37_rx2tx_dly);
	}  else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
			16, rfseq_majrev40_rx2tx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
			16, rfseq_majrev40_rx2tx_dly);
	} else {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
			16, rfseq_rx2tx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
			16, rfseq_rx2tx_dly);
	}

	if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
		/* Overwrite delay before RX_DC_LOOP_CONT */
		tiny_rfseq_tx2rx_dly[14] = 0x0020;

		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x10, 16,
		                          tiny_rfseq_tx2rx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 128, 16,
		                          tiny_rfseq_tx2rx_dly);
	} else if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x10, 16,
		                          rfseq_majrev4_tx2rx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 128, 16,
		                          rfseq_majrev4_tx2rx_dly);
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x10, 16,
		                          rfseq_majrev36_tx2rx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x80, 16,
		                          rfseq_majrev36_tx2rx_dly);

		rfseq_bundle_48[0] = 0x6000;
		rfseq_bundle_48[1] = 0x0;
		rfseq_bundle_48[2] = 0x0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 4, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x0000;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 5, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x4000;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 6, 48,
		                          rfseq_bundle_48);
	} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x10, 16,
		                          rfseq_majrev37_tx2rx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x80, 16,
		                          rfseq_majrev37_tx2rx_dly);
	} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		/* set rfseq_bundle_tbl {0x01C703C 0x01C7014 0x02C703E 0x02C701C} */
		/* acphy_write_table RfseqBundle $rfseq_bundle_tbl 0 */
		rfseq_bundle_48[0] = 0x703C;
		rfseq_bundle_48[1] = 0x1c;
		rfseq_bundle_48[2] = 0x0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 0, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x7014;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 1, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x700e;
		rfseq_bundle_48[1] = 0x2c;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 2, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x702c;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 3, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x6020;
		rfseq_bundle_48[1] = 0x20;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 4, 48,
		                          rfseq_bundle_48);
	} else if (ACMAJORREV_2(pi->pubpi->phy_rev) && !(PHY_IPA(pi))) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x10,
			16, rfseq_tx2rx_cmd_noafeclkswitch);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x80,
			16, rfseq_tx2rx_dly_noafeclkswitch);
	} else {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x10, 16, rfseq_tx2rx_cmd);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x80, 16, rfseq_tx2rx_dly);
	}

	/* This was to keep the adc-clock buffer powered up even if adc is powered down
	   for non-tiny radio. But for tiny radio this is not required.
	*/
	if (!(ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
	      ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_36(pi->pubpi->phy_rev) ||
	      ACMAJORREV_37(pi->pubpi->phy_rev) || ACMAJORREV_40(pi->pubpi->phy_rev))) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3c6, 16, &adc_war_val);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3c7, 16, &adc_war_val);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3d6, 16, &adc_war_val);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3d7, 16, &adc_war_val);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3e6, 16, &adc_war_val);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3e7, 16, &adc_war_val);
	}

	/* do this during fem table load for 43602a0 */
	if (((CHSPEC_IS2G(pi->radio_chanspec) && ext_pa_ana_2g) ||
	    (CHSPEC_IS5G(pi->radio_chanspec) && ext_pa_ana_5g)) &&
	    !(ACMAJORREV_5(pi->pubpi->phy_rev) && ACMINORREV_0(pi))) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x80, 16, &pablowup_war_val);
	}

	/* 4360 and 43602 */
	if (ACMAJORREV_0(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		/* load the txv for spatial expansion */
		acphy_load_txv_for_spexp(pi);
	}

	if ((RADIOID_IS(pi->pubpi->radioid, BCM2069_ID)) &&
	    ((RADIO2069_MAJORREV(pi->pubpi->radiorev) > 0) ||
			((RADIO2069_MINORREV(pi->pubpi->radiorev) == 16) ||
			(RADIO2069_MINORREV(pi->pubpi->radiorev) == 17)))) {
		/* 11n_20 */
		wlc_phy_set_analog_tx_lpf(pi, 0x2, -1, 5, 5, -1, -1, -1);
		/* 11ag_11ac_20 */
		wlc_phy_set_analog_tx_lpf(pi, 0x4, -1, 5, 5, -1, -1, -1);
		/* 11n_40 */
		wlc_phy_set_analog_tx_lpf(pi, 0x10, -1, 5, 5, -1, -1, -1);
		/* 11ag_11ac_40 */
		wlc_phy_set_analog_tx_lpf(pi, 0x20, -1, 5, 5, -1, -1, -1);
		/* 11n_11ag_11ac_80 */
		wlc_phy_set_analog_tx_lpf(pi, 0x80, -1, 6, 6, -1, -1, -1);
	} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		/* 11b_20 11n_20 11ag_11ac_20 */
		wlc_phy_set_analog_tx_lpf(pi, 0x7, -1, 4, -1, -1, -1, -1);
		/* 11n_40 11ag_11ac_40 */
		wlc_phy_set_analog_tx_lpf(pi, 0x30, -1, 5, -1, -1, -1, -1);
		/* 11n_11ag_11ac_80 */
		wlc_phy_set_analog_tx_lpf(pi, 0x80, -1, 6, -1, -1, -1, -1);
	} else if (RADIOID(pi->pubpi->radioid) == BCM20696_ID) {
		/* "11b_20" "11n_20" "11ag_11ac_20" "samp_play" */
		wlc_phy_set_analog_tx_lpf(pi, 0x107, -1, 4, 4, -1, -1, -1);
		/* "11b_40" "11n_40" "11ag_11ac_40" "samp_play" */
		wlc_phy_set_analog_tx_lpf(pi, 0x138, -1, 5, 5, -1, -1, -1);
		/* "11b_80" "11n_11ag_11ac_80" "samp_play" */
		wlc_phy_set_analog_tx_lpf(pi, 0x1c0, -1, 6, 6, -1, -1, -1);
	}

	/* tiny radio specific processing */
	if (TINY_RADIO(pi)) {
		uint16 regval;
		const uint32 NvAdjTbl[64] = { 0x000000, 0x400844, 0x300633, 0x200422,
			0x100211, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
			0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
			0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000100,
			0x000200, 0x000311, 0x000422, 0x100533, 0x200644, 0x300700,
			0x400800, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
			0x000000, 0x000000, 0x400800, 0x300700, 0x200644, 0x100533,
			0x000422, 0x000311, 0x000200, 0x000100, 0x000000, 0x000000,
			0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
			0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
			0x000000, 0x000000, 0x100211, 0x200422, 0x300633, 0x400844};

		const uint32 phasetracktbl[22] = { 0x06af56cd, 0x059acc7b,
			0x04ce6652, 0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819,
			0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819, 0x06af56cd,
			0x059acc7b, 0x04ce6652, 0x02b15819, 0x02b15819, 0x02b15819,
			0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819, 0x02b15819};

		/* Tiny NvAdjTbl */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_NVADJTBL, 64, 0, 32, NvAdjTbl);

		if (!(ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev))) {
			/* Tiny phasetrack */
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PHASETRACKTBL_1X1, 22, 0, 32,
			phasetracktbl);
		}

		/* Channels Smoothing */
		if ((!ACMINORREV_0(pi) || ACMAJORREV_4(pi->pubpi->phy_rev)) &&
		   (!ACMAJORREV_32(pi->pubpi->phy_rev)) &&
		   (!ACMAJORREV_33(pi->pubpi->phy_rev)))
			wlc_phy_load_channel_smoothing_tiny(pi);

		/* program tx, rx bias reset to avoid clock stalls */
		regval = 0x2b;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe8, 16, &regval);
		regval = 0x28;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe7, 16, &regval);

		/* #Keep lpf_pu @ 0 for rx since lpf_pu controls tx lpf exclusively */
		regval = 0x82c0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x14b, 16, &regval);

		/* Magic rfseqbundle writes to make TX->Rx turnaround work */
		/* set rfseq_bundle_tbl {0x4000 0x0000 } */
		/* acphy_write_table RfseqBundle $rfseq_bundle_tbl 4 */
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
			rfseq_bundle_48[0] = 0x6000;
		} else {
			rfseq_bundle_48[0] = 0x4000;
		}
		rfseq_bundle_48[1] = 0x0;
		rfseq_bundle_48[2] = 0x0;

		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 4, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x0000;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 5, 48,
		                          rfseq_bundle_48);

		/* set rfseq_bundle_tbl {0x3000C 0x20000 0x30034 0x20000} */
		/* acphy_write_table RfseqBundle $rfseq_bundle_tbl 0 */
		rfseq_bundle_48[0] = 0x0000;
		rfseq_bundle_48[1] = 0x2;
		rfseq_bundle_48[2] = 0x0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 0, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x0034;
		rfseq_bundle_48[1] = 0x3;
		rfseq_bundle_48[2] = 0x0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 1, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x0000;
		rfseq_bundle_48[1] = 0x2;
		rfseq_bundle_48[2] = 0x0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 2, 48,
		                          rfseq_bundle_48);
		rfseq_bundle_48[0] = 0x000c;
		rfseq_bundle_48[1] = 0x3;
		rfseq_bundle_48[2] = 0x0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 3, 48,
		                          rfseq_bundle_48);
	}

	if (ACMAJORREV_1(pi->pubpi->phy_rev) || ACMAJORREV_3(pi->pubpi->phy_rev)) {
		uint8 txevmtbl[40] = {0x09, 0x0E, 0x11, 0x14, 0x17, 0x1A, 0x1D, 0x20, 0x09,
			0x0E, 0x11, 0x14, 0x17, 0x1A, 0x1D, 0x20, 0x22, 0x24, 0x09, 0x0E,
			0x11, 0x14, 0x17, 0x1A, 0x1D, 0x20, 0x22, 0x24, 0x09, 0x0E, 0x11,
			0x14, 0x17, 0x1A, 0x1D, 0x20, 0x22, 0x24, 0x0, 0x0};
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_TXEVMTBL, 40, 0, 8, txevmtbl);
	}

	/* 4335: Running phase track loop faster */
	/* Fix for ping issue caused by high phase imbalance */
	if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
		uint32 phasetracktbl_1x1[22] = { 0x6AF5700, 0x59ACC9A,
			0x4CE6666, 0x4422064, 0x4422064, 0x4422064,	0x4422064,
			0x4422064, 0x4422064, 0x4422064, 0x4422064, 0x6AF5700,
			0x59ACC9A, 0x4CE6666, 0x4422064, 0x4422064, 0x4422064,
			0x4422064, 0x4422064, 0x4422064, 0x4422064, 0x4422064};
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PHASETRACKTBL_1X1, 22, 0, 32,
		                          phasetracktbl_1x1);
	}
	/* Increase phase track loop BW to improve PER floor, */
	/*   Phase noise  seems higher. Needs further investigation */
	if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
		uint32 phasetracktbl[22] = { 0x6AF5700, 0x59ACC9A,
			0x4CE6666, 0x4422064, 0x4422064, 0x4422064,	0x4422064,
			0x4422064, 0x4422064, 0x4422064, 0x4422064, 0x6AF5700,
			0x59ACC9A, 0x4CE6666, 0x4422064, 0x4422064, 0x4422064,
			0x4422064, 0x4422064, 0x4422064, 0x4422064, 0x4422064};
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PHASETRACKTBL, 22, 0, 32,
		                          phasetracktbl);
	}
	/* DEBUG: TEST CODE FOR SS PTW70 DEBUG */
	if (ACMAJORREV_1(pi->pubpi->phy_rev) && BF3_PHASETRACK_MAX_ALPHABETA(pi_ac)) {
		for (offset = 0; offset < 22; offset++) {
			wlc_phy_table_write_acphy(pi, 0x1a, 1, offset, 32, &war_val);
		}
	}

	/* initialize the fixed spatial expansion matrix */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		phy_ac_chanmgr_set_spexp_matrix(pi);
	}

	/* To save current, turn off AFEDiv for the unused core, */
	/* Below forces AFEDiv_pu_repeater2_disRX to be 0 when doing TX2RX || reset2RX */
	/* if (ACMAJORREV_2(pi->pubpi->phy_rev)) { */
	/* 	wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe9, 16, &AFEdiv_read_val); */
	/* 	AFEdiv_read_val = (AFEdiv_read_val & 0xfdff); */
	/* 	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0xe9, 16, &AFEdiv_read_val); */
	/* } */

	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		/* Writing tables related to 43012 */
		acphytbl_info_t *tbl = phy_ac_get_tbls_write_on_reset_list(pi);

		while (tbl->tbl_ptr != NULL) {
			wlc_phy_table_write_acphy(pi, tbl->tbl_id, tbl->tbl_len,
					tbl->tbl_offset, tbl->tbl_width, tbl->tbl_ptr);
			tbl++;
		}

		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0, 60,
				&read_val);
		write_val[0] = (read_val[0] | (1<<18));
		write_val[1] = read_val[1];
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0, 60,
				write_val);
		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0, 60,
				&read_val);

		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
				&read_val);
		write_val[0] = (read_val[0] | (1<<18));
		write_val[1] = read_val[1];
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
				write_val);
		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
				&read_val);

		// Bundle cmd: 0xbe adc_en = 1 dac_en = 1
		rfseq_bundle_48[0] = 0x0;
		rfseq_bundle_48[1] = 0x0E20;
		rfseq_bundle_48[2] = 0x0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 62, 48,
				rfseq_bundle_48);

		//  Bundle cmd: 0xbf  only adc_en = 1 reset_enable = 0
		rfseq_bundle_48[0] = 0x0;
		rfseq_bundle_48[1] = 0x0400;
		rfseq_bundle_48[2] = 0x0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 63, 48,
				rfseq_bundle_48);

		if (!ACMINORREV_0(pi)) {
				uint16 rfseq_read_val;
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0, 60,
						&read_val);
				write_val[0] = (read_val[0] | 0x2);
				write_val[1] = read_val[1];
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0, 60,
						write_val);

				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
						&read_val);
				write_val[0] = (read_val[0] & 0xfffffffd);
				write_val[1] = read_val[1];
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
						write_val);

				/* Changes related to SW43012-1540 */
				/* Unset RX_CFG(0) */
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
						&read_val);
				write_val[0] = (read_val[0] & 0xfffffffe);
				write_val[1] = read_val[1];
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
						write_val);

				/* Program tx2rx_lpf_ctl_lut_enrx(40:39) = 3
				    RFSeqTbl[0x360] => tx2rx_lpf_ctl_lut_enrx(0)(43:35)
				*/
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x360, 16,
						&rfseq_read_val);
				rfseq_read_val = rfseq_read_val | (3 << 4);
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x360, 16,
						&rfseq_read_val);

				/* Program tx2rx_lpf_ctl_lut_disrx(41:40) = 0
				    RFSeqTbl[0x361] => tx2rx_lpf_ctl_lut_disrx(0)(44:36)
				*/
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x361, 16,
						&rfseq_read_val);
				rfseq_read_val = rfseq_read_val & 0xffcf;
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x361, 16,
						&rfseq_read_val);

				/* Program rx2tx_lpf_ctl_lut_entx(37:36) = 0
				    RFSeqTbl[0x145] => rx2tx_lpf_ctl_lut_entx(0)(40:33)
				*/
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x145, 16,
						&rfseq_read_val);
				rfseq_read_val = rfseq_read_val & 0xffe7;
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x145, 16,
						&rfseq_read_val);

				/* Program rx2tx_lpf_ctl_lut_distx(28:27) = 0
				    RFSeqTbl[0x36c] => rx2tx_lpf_ctl_lut_distx(0)(31:17)
				*/
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x36c, 16,
						&rfseq_read_val);
				rfseq_read_val = rfseq_read_val & 0xf3ff;
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x36c, 16,
						&rfseq_read_val);
		}
	}

	ACPHY_ENABLE_STALL(pi, stall_val);

}

#define RFSEQEXT_TBL_SZ_PER_CORE_28NM 11
static void
wlc_phy_set_regtbl_on_band_change_acphy(phy_info_t *pi)
{
	uint8 stall_val, core, idx, phy_coremask = pi->pubpi->phy_coremask;
	uint16 bq1_gain_core1 = 0x49;
	uint8 pdet_range_id;
#ifndef WLC_DISABLE_ACI
	bool hwaci_on;
#endif /* !WLC_DISABLE_ACI */
	bool w2_on;
	txcal_coeffs_t txcal_cache[PHY_CORE_MAX];
	rxcal_coeffs_t rxcal_cache[PHY_CORE_MAX];
	uint32 read_val[2], write_val[2];
#ifdef PHYCAL_CACHING
	ch_calcache_t *ctx;
	bool ctx_valid;
#endif /* PHYCAL_CACHING */
	uint sicoreunit;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);

	wlc_phy_cfg_energydrop_timeout(pi);

	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {

		PHY_INFORM(("%s: band=%d coremask = 0x%x\n", __FUNCTION__,
			(CHSPEC_IS2G(pi->radio_chanspec))?2:5, phy_coremask));

		FOREACH_CORE(pi, core) {
			/* update rfseqext table for afediv configuration */
			/* in rfseqext_[25]g_rev40 table, array index 0 is msb */
			for (idx = 0; idx < RFSEQEXT_TBL_SZ_PER_CORE_28NM; idx++) {
				if (CHSPEC_IS5G(pi->radio_chanspec)) {
					write_val[0] = rfseqext_5g_rev40[idx][1];
					write_val[1] = rfseqext_5g_rev40[idx][0];
				} else {
					write_val[0] = rfseqext_2g_rev40[idx][1];
					write_val[1] = rfseqext_2g_rev40[idx][0];
				}
				if ((wlc_phy_ac_phycap_maxbw(pi) > BW_20MHZ)) {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT,
					1, core*RFSEQEXT_TBL_SZ_PER_CORE_28NM+idx, 60, write_val);
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT,
					1, core*RFSEQEXT_TBL_SZ_PER_CORE_28NM+idx, 60, &read_val);
				} else {
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT,
					1, core*RFSEQEXT_TBL_SZ_PER_CORE_28NM+idx, 42, write_val);
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT,
					1, core*RFSEQEXT_TBL_SZ_PER_CORE_28NM+idx, 42, &read_val);
				}
				PHY_INFORM(("band=%d offset=%d write=0x%08x%08x read=0x%08x%08x\n",
					(CHSPEC_IS2G(pi->radio_chanspec))?2:5,
					core*RFSEQEXT_TBL_SZ_PER_CORE_28NM+idx,
					write_val[1], write_val[0], read_val[1], read_val[0]));
			}

			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				MOD_RADIO_REG_20694(pi, RF, RX5G_REG5, core, rx5g_ldo_pu, 0x1);
			} else {
				MOD_RADIO_REG_20694(pi, RF, RX2G_REG4, core, rx2g_ldo_pu, 0x1);
			}

			MOD_PHYREG(pi, dyn_radioa0, dyn_radio_ovr0, 0x1);
			MOD_PHYREG(pi, dyn_radioa1, dyn_radio_ovr1, 0x1);
			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				//set phyreg(dyn_radioa$core.dyn_radio_ovr_val_idac_main$core) 0x2a;
				MOD_PHYREG(pi, dyn_radioa0, dyn_radio_ovr_val_idac_main0, 0x2a);
				MOD_PHYREG(pi, dyn_radioa1, dyn_radio_ovr_val_idac_main1, 0x2a);
				//set phyreg(radio_connection_direct_pad_idac.pad_idac_gm) 0xa;
				MOD_PHYREG(pi, radio_connection_direct_pad_idac, pad_idac_gm, 0xa);
				//set phyreg(radio_connection_direct_mx_bbdc.mx5g_idac_bbdc) 0xa;
				MOD_PHYREG(pi, radio_connection_direct_mx_bbdc,
					mx5g_idac_bbdc, 0xa);
				//set phyreg(radio_connection_direct_pad_idac.pad_idac_pmos) 0x14;
				MOD_PHYREG(pi, radio_connection_direct_pad_idac,
					pad_idac_pmos, 0x14);
				//set phyreg(radio_connection_direct_mx_bbdc.mx2g_idac_bbdc) 0x14;
				MOD_PHYREG(pi, radio_connection_direct_mx_bbdc,
					mx2g_idac_bbdc, 0x14);
			} else {
				//set phyreg(dyn_radioa$core.dyn_radio_ovr_val_idac_main$core) 0x37;
				MOD_PHYREG(pi, dyn_radioa0, dyn_radio_ovr_val_idac_main0, 0x37);
				MOD_PHYREG(pi, dyn_radioa1, dyn_radio_ovr_val_idac_main1, 0x37);
			}

			if (PHY_COREMASK_SISO(phy_coremask)) {
				/* SISO */
				if (CHSPEC_IS5G(pi->radio_chanspec)) {
					PHY_INFORM(("%s: SISO 5G core= %d\n", __FUNCTION__, core));
					WRITE_PHYREGCE(pi, Dac_gain, core, 0xd670);
					WRITE_PHYREGCE(pi, pllLogenMaskCtrl, core, 0x1f);
				} else {
					PHY_INFORM(("%s: SISO 2G core= %d\n", __FUNCTION__, core));
					WRITE_PHYREGCE(pi, Dac_gain, core, 0x7870);
					WRITE_PHYREGCE(pi, pllLogenMaskCtrl, core, 0x3f);
				}
			} else {
				/* MIMO */
				if (core == 1) {
					if (CHSPEC_IS5G(pi->radio_chanspec)) {
						PHY_INFORM(("wl%d %s: MIMO 5G core= %d\n",
							PI_INSTANCE(pi), __FUNCTION__, core));
						sicoreunit = wlapi_si_coreunit(pi->sh->physhim);
						WRITE_PHYREGCE(pi, Dac_gain, core, 0xc770);
						if (sicoreunit == DUALMAC_MAIN) {
							PHY_INFORM(("wl%d %s: MIMO 5G MAIN\n",
								PI_INSTANCE(pi), __FUNCTION__));
							WRITE_PHYREGCE(pi, pllLogenMaskCtrl, core,
								0x2fb);
						} else if (sicoreunit == DUALMAC_AUX) {
							PHY_INFORM(("wl%d %s: MIMO 5G AUX\n",
								PI_INSTANCE(pi), __FUNCTION__));
							WRITE_PHYREGCE(pi, pllLogenMaskCtrl, core,
								0x2ff);
						}
						WRITE_PHYREGCE(pi, Extra2AfeClkDivOverrideCtrl28nm,
							core, 0x20);
					} else {
						PHY_INFORM(("wl%d %s: MIMO 2G core= %d\n",
							PI_INSTANCE(pi), __FUNCTION__, core));
						WRITE_PHYREGCE(pi, Dac_gain, core, 0x6170);
						WRITE_PHYREGCE(pi, pllLogenMaskCtrl, core, 0xff);
						WRITE_PHYREGCE(pi, Extra2AfeClkDivOverrideCtrl28nm,
							core, 0x10);
					}
				} else if (core == 0) {
					if (CHSPEC_IS5G(pi->radio_chanspec)) {
						PHY_INFORM(("wl%d %s: MIMO 5G core= %d\n",
							PI_INSTANCE(pi), __FUNCTION__, core));
						WRITE_PHYREGCE(pi, Dac_gain, core, 0xc770);
						WRITE_PHYREGCE(pi, pllLogenMaskCtrl, core, 0x77f);
						WRITE_PHYREGCE(pi, Extra2AfeClkDivOverrideCtrl28nm,
							core, 0x20);
					} else {
						PHY_INFORM(("wl%d %s: MIMO 2G core= %d\n",
							PI_INSTANCE(pi), __FUNCTION__, core));
						WRITE_PHYREGCE(pi, Dac_gain, core, 0x6170);
						WRITE_PHYREGCE(pi, pllLogenMaskCtrl, core, 0x1ff);
						WRITE_PHYREGCE(pi, Extra2AfeClkDivOverrideCtrl28nm,
							core, 0x10);
					}
				}
			}
		}
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			WRITE_PHYREG(pi, bphymrcCtrl, 0x200);
		} else {
			WRITE_PHYREG(pi, bphymrcCtrl, 0);
		}

		MOD_PHYREG(pi, fineRxclockgatecontrol, EncodeGainClkEn, 1);
	}

	if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_PHYREG(pi, fineRxclockgatecontrol, forcedigigaingatedClksOn, 1);
		} else {
			MOD_PHYREG(pi, fineRxclockgatecontrol, forcedigigaingatedClksOn, 0);
		}

		/* 4335C0: Current optimization */
		if (ACMINORREV_2(pi)) {
			MOD_PHYREG(pi, fineRxclockgatecontrol, forcedigigaingatedClksOn, 0);
		}
	}

	if (ACMAJORREV_36(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev) ||
		ACMAJORREV_2(pi->pubpi->phy_rev) || (ACMAJORREV_1(pi->pubpi->phy_rev) &&
		!(ACMINORREV_0(pi) || ACMINORREV_1(pi)))) {
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_PHYREG(pi, bOverAGParams, bOverAGlog2RhoSqrth, 120);
			MOD_PHYREG(pi, CRSMiscellaneousParam, b_over_ag_falsedet_en, 1);
		} else {
			MOD_PHYREG(pi, bOverAGParams, bOverAGlog2RhoSqrth, 255);
			MOD_PHYREG(pi, CRSMiscellaneousParam, b_over_ag_falsedet_en, 0);
		}
	}

	if (ACMAJORREV_2(pi->pubpi->phy_rev) && !ACMINORREV_0(pi)) {
		FOREACH_CORE(pi, core) {
			/* Reduces 20in80 humps in 5G */
			WRITE_PHYREGC(pi, Clip2Threshold, core, 0xa04e);
			if (CHSPEC_IS2G(pi->radio_chanspec))
			  WRITE_PHYREGC(pi, Clip2Threshold, core, 0x804e);
		}
	}

	if (ACMAJORREV_3(pi->pubpi->phy_rev) || ACMAJORREV_4(pi->pubpi->phy_rev) ||
	    ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, clip_detect_normpwr_var_mux, use_norm_var_for_clip_detect, 1);
	}

	/* Load tx gain table */
	wlc_phy_ac_gains_load(pi);

	if (ACREV_IS(pi->pubpi->phy_rev, 0)) {
		wlc_phy_tx_gm_gain_boost(pi);
	}

	pdet_range_id = phy_tpc_get_5g_pdrange_id(pi->tpci);
	if ((pdet_range_id == 9 || pdet_range_id == 16) && !ACMAJORREV_32(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_33(pi->pubpi->phy_rev) && !ACMAJORREV_37(pi->pubpi->phy_rev)) {
		bq1_gain_core1 = (CHSPEC_IS5G(pi->radio_chanspec))? 0x49 : 0;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x18e, 16, &bq1_gain_core1);
	}

	if (IS_4364_1x1(pi) ||	IS_4364_3x3(pi)) {
		wlc_phy_ac_shared_ant_femctrl_master(pi);
	} else {
#if (!defined(WL_SISOCHIP) && defined(SWCTRL_TO_BT_IN_COEX))
		/* Write FEMCTRL mask to shmem ; let ucode write them to FEMCTRL register */
		wlc_phy_ac_femctrl_mask_on_band_change_btcx(pi->u.pi_acphy->btcxi);
#else
		wlc_phy_ac_femctrl_mask_on_band_change(pi);
#endif
	}


	/* 20691 specific processing, if needed */
	if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID))
		wlc_phy_set_regtbl_on_band_change_acphy_20691(pi);
	else if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID))
		wlc_phy_set_regtbl_on_band_change_acphy_20693(pi);

	/* 2g/5g band can have different aci modes */
	if (!ACPHY_ENABLE_FCBS_HWACI(pi)) {
#ifndef WLC_DISABLE_ACI
		hwaci_on = ((pi->sh->interference_mode & ACPHY_ACI_HWACI_PKTGAINLMT) != 0) ||
		((pi->sh->interference_mode & ACPHY_HWACI_MITIGATION) != 0);
		wlc_phy_hwaci_setup_acphy(pi, hwaci_on, FALSE);
#endif /* !WLC_DISABLE_ACI */
		w2_on = ((pi->sh->interference_mode & ACPHY_ACI_W2NB_PKTGAINLMT) != 0) ||
			((pi->sh->interference_mode & ACPHY_HWACI_MITIGATION) != 0);
		wlc_phy_aci_w2nb_setup_acphy(pi, w2_on);
	}

	if (PHY_PAPDEN(pi)) {

		if (!ACMAJORREV_4(pi->pubpi->phy_rev))
			OSL_DELAY(100);

		if (TINY_RADIO(pi))
		{
#ifdef PHYCAL_CACHING
			ctx = wlc_phy_get_chanctx(pi, pi->radio_chanspec);
			ctx_valid = (ctx != NULL) ? ctx->valid : FALSE;

			/* allow reprogramming rfpwrlut if ctx is not available or
			 * ctx is available but invalid
			 */
			if (!ctx_valid)
#endif /* PHYCAL_CACHING */
				wlc_phy_papd_set_rfpwrlut_tiny(pi);
		} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			wlc_phy_papd_set_rfpwrlut_phymaj_rev36(pi);
		} else {
			wlc_phy_papd_set_rfpwrlut(pi);
		}
	}

	/* For 4350C0, bphy is turned off when in 5G. Need to disable the predetector. */
	if (ACMAJORREV_2(pi->pubpi->phy_rev) && !ACMINORREV_0(pi)) {
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			MOD_PHYREG(pi, CRSMiscellaneousParam, bphy_pre_det_en, 0);
		} else {
			/* Disable BPHY pre-detect JIRA:SWWLAN-45198 jammer/ACI performance
			* takes a hit with predetector enabled
			*/
			MOD_PHYREG(pi, CRSMiscellaneousParam, bphy_pre_det_en, 0);
		}
	}

	/* For 4345, bphy is turned on when in 5G. Need to enable the predetector and timeout to
	 * Effectively disable and reduce average current ~1.5mA and remove 2mA x 20ms humps
	 * See http://jira.broadcom.com/browse/SWWLAN-77067
	 * Missing CRDOT11ACPHY-815
	 */
	if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
		uint16 enablepd = ((CHSPEC_IS5G(pi->radio_chanspec)) ? 1 : 0);
		MOD_PHYREG(pi, CRSMiscellaneousParam, bphy_pre_det_en, enablepd);
		MOD_PHYREG(pi, dot11acConfig, bphyPreDetTmOutEn, enablepd);
	}

	/* Turn ON 11n 256 QAM in 2.4G */
	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		bool enable = (CHSPEC_IS2G(pi->radio_chanspec) && CHSPEC_IS20(pi->radio_chanspec));

		if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
		    !ACMAJORREV_33(pi->pubpi->phy_rev) &&
		    !ACMAJORREV_37(pi->pubpi->phy_rev))
			WRITE_PHYREG(pi, miscSigCtrl, enable ? 0x203 : 0x3);

		wlapi_11n_proprietary_rates_enable(pi->sh->physhim, enable);

		PHY_INFORM(("wl%d %s: 11n turbo QAM %s\n",
			PI_INSTANCE(pi), __FUNCTION__,
			enable ? "enabled" : "disabled"));

		/* Loading Tx specific radio settings  */
		if (ACMAJORREV_4(pi->pubpi->phy_rev) &&
			RADIOID_IS(pi->pubpi->radioid, BCM20693_ID))
			wlc_phy_config_bias_settings_20693(pi);
	}

	/* knoise rxgain override value initializaiton */
	/*
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			wlapi_bmac_write_shm(pi->sh->physhim, M_RXGAIN_HI(pi), 0x5d7);
			wlapi_bmac_write_shm(pi->sh->physhim, M_RXGAIN_LO(pi), 0x457);
		} else {
			wlapi_bmac_write_shm(pi->sh->physhim, M_RXGAIN_HI(pi), 0x1d5);
			wlapi_bmac_write_shm(pi->sh->physhim, M_RXGAIN_LO(pi), 0x55);
		}
	}
	*/
	/* need to zero out cal coeffs on band change */
	bzero(txcal_cache, sizeof(txcal_cache));
	bzero(rxcal_cache, sizeof(rxcal_cache));
	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
	       PHY_ERROR(("FIXME 4347A0: Bypass phy_cal_coeffs_upd in band_change\n"));
	} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
	       PHY_ERROR(("FIXME 7271: Bypass phy_cal_coeffs_upd in band_change\n"));
	} else {
		wlc_phy_txcal_coeffs_upd(pi, txcal_cache);
		wlc_phy_rxcal_coeffs_upd(pi, rxcal_cache);
		if (!ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMAJORREV_33(pi->pubpi->phy_rev)) {
			wlc_acphy_paldo_change(pi);
		}
	}
	ACPHY_ENABLE_STALL(pi, stall_val);

	/* Using different filter settings for 2G and 5G (50MHz notch) for 43012
	 This improves both the Jammer performance and AACI performance for 2G
	 while not degrading the ACI performance. For 5G use default
	 filter settings (40MHz notch)
	*/
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, RfctrlOverrideLpfCT0, lpf_bq2_bw, 1);
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			MOD_PHYREG(pi, RfctrlCoreLpfCT0, lpf_bq2_bw, 0);
		} else {
			MOD_PHYREG(pi, RfctrlCoreLpfCT0, lpf_bq2_bw, 1);
		}
	}
}

static void
wlc_phy_set_regtbl_on_bw_change_acphy(phy_info_t *pi)
{
	int sp_tx_bw = 0;
	uint8 stall_val, core, nbclip_cnt_4360 = 15;
	uint8 rxevm20p[] = {8, 6, 4}, rxevm20n[] = {4, 6, 8};
	uint8 rxevm0[] = {0, 0, 0}, rxevm_len = 3;
	uint32 epa_turnon_time;

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);

	if (BW_RESET == 1)
		wlc_phy_set_reg_on_bw_change_acphy(pi);

	if (CHIPID(pi->sh->chip) == BCM4335_CHIP_ID &&
		pi->sh->chippkg == BCM4335_FCBGA_PKG_ID)
		nbclip_cnt_4360 = 12;

	if (CHSPEC_IS80(pi->radio_chanspec) ||
		PHY_AS_80P80(pi, pi->radio_chanspec)) {
		/* 80mhz */
		if (ACMAJORREV_0(pi->pubpi->phy_rev))
			sp_tx_bw = 5;
		else
			sp_tx_bw = 6;

		nbclip_cnt_4360 *= 4;
	} else if (CHSPEC_IS160(pi->radio_chanspec)) {
		/* true 160mhz */
		sp_tx_bw = 6;
		nbclip_cnt_4360 *= 4;
		ASSERT(0);
	} else if (CHSPEC_IS40(pi->radio_chanspec)) {
		/* 40mhz */
		if (ACMAJORREV_0(pi->pubpi->phy_rev))
			sp_tx_bw = 4;
		else
			sp_tx_bw = 5;

		nbclip_cnt_4360 *= 2;
	} else if (CHSPEC_BW_LE20(pi->radio_chanspec)) {
		/* 20mhz */
		if (ACMAJORREV_0(pi->pubpi->phy_rev))
			sp_tx_bw = 3;
		else if (ACMAJORREV_40(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
			sp_tx_bw = 4;
		} else {
			sp_tx_bw = 5;
		}
	} else {
		PHY_ERROR(("%s: No primary channel settings for bw=%d\n",
		           __FUNCTION__, CHSPEC_BW(pi->radio_chanspec)));
	}

	/* reduce NB clip CNT thresholds */
	FOREACH_CORE(pi, core) {
		if (!ACMAJORREV_1(pi->pubpi->phy_rev) ||
			(CHSPEC_IS2G(pi->radio_chanspec) && BF3_AGC_CFG_2G(pi->u.pi_acphy)) ||
			(CHSPEC_IS5G(pi->radio_chanspec) && BF3_AGC_CFG_5G(pi->u.pi_acphy))) {
			MOD_PHYREGC(pi, FastAgcClipCntTh, core, fastAgcNbClipCntTh,
				nbclip_cnt_4360);
		} else {
			MOD_PHYREGC(pi, FastAgcClipCntTh, core, fastAgcNbClipCntTh, 23);
		}
	}

	wlc_phy_set_analog_tx_lpf(pi, 0x100, -1, sp_tx_bw, sp_tx_bw, -1, -1, -1);
	if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* This is necessary because in 7271 RC filter is not connected to RFSEQ */
		FOREACH_CORE(pi, core) {
			WRITE_RADIO_REG_20696(pi, LPF_GMULT_RC_BW, core, sp_tx_bw);
		}
	}
	/* change the barelyclipgainbackoff to 6 for 80Mhz due to some PER issue for 4360A0 CHIP */
	if (ACREV_IS(pi->pubpi->phy_rev, 0)) {
	  if (CHSPEC_IS80(pi->radio_chanspec)) {
	      ACPHYREG_BCAST(pi, Core0computeGainInfo, 0xcc0);
	  } else {
	      ACPHYREG_BCAST(pi, Core0computeGainInfo, 0xc60);
	  }
	}

#ifndef WL_FDSS_DISABLED
	/* Enable FDSS */
	if (TINY_RADIO(pi) && !ACMAJORREV_32(pi->pubpi->phy_rev) &&
		!ACMAJORREV_33(pi->pubpi->phy_rev) &&
		((CHSPEC_IS2G(pi->radio_chanspec) && (pi->fdss_level_2g[0] != -1)) ||
		(CHSPEC_IS5G(pi->radio_chanspec) && (pi->fdss_level_5g[0] != -1))))  {
		wlc_phy_fdss_init(pi);
		wlc_phy_set_fdss_table(pi);
	}
#endif /* WL_FDSS_DISABLED */

	/* SWWLAN-28943 */
	if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
		MOD_PHYREGC(pi, computeGainInfo, 0, gainBackoffValue, 1);
	}

	if (ACMAJORREV_2(pi->pubpi->phy_rev) && (ACMINORREV_1(pi) || ACMINORREV_3(pi))) {
		FOREACH_CORE(pi, core) {
			/* Reduces 54Mbps humps */
			MOD_PHYREGC(pi, computeGainInfo, core, gainBackoffValue, 1);
			/* Reduces 20in80 humps */
			WRITE_PHYREGC(pi, Clip2Threshold, core, 0xa04e);
		}
	}

	if (IS_4364_3x3(pi)) {
		FOREACH_CORE(pi, core) {
			/* Reduces 54Mbps humps */
			MOD_PHYREGC(pi, computeGainInfo, core, gainBackoffValue, 1);
		}
	}

	/* Shape rxevm table due to hit on near DC_tones */
	if (ACMAJORREV_0(pi->pubpi->phy_rev) || ACMAJORREV_2(pi->pubpi->phy_rev) ||
	    ACMAJORREV_5(pi->pubpi->phy_rev)) {
		if (CHSPEC_IS20(pi->radio_chanspec)) {
			/* Freq Bins {1 2 3} = {8 6 4} dB */
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_NVRXEVMSHAPINGTBL,
			                          rxevm_len, 1, 8, rxevm20p);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_NVRXEVMSHAPINGTBL,
			                          rxevm_len, 64 - rxevm_len, 8, rxevm20n);
		} else {
			/* Reset the 20mhz entries to 0 */
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_NVRXEVMSHAPINGTBL,
			                          rxevm_len, 1, 8, rxevm0);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_NVRXEVMSHAPINGTBL,
			                          rxevm_len, 64 - rxevm_len, 8, rxevm0);
		}
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_iqadc_clamp_en, 1);
			MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqadc_clamp_en, 1);
		}

		/* updategainH : increase clamp_en off delay to 16 */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0x30, 16, rf_updh_cmd_clamp);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xa0, 16, rf_updh_dly_clamp);

		/* updategainL : increase clamp_en off delay to 16 */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0x40, 16, rf_updl_cmd_clamp);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xb0, 16, rf_updl_dly_clamp);

		/* updategainU : increase clamp_en off delay to 16 */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0x50, 16, rf_updu_cmd_clamp);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xc0, 16, rf_updu_dly_clamp);
	}

	/* [4365]resovle DVGA stuck high - htagc and gainreset during wait_energy_drop collides */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev))
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xb0, 16, rf_updl_dly_dvga);

	if (ACMAJORREV_1(pi->pubpi->phy_rev) || TINY_RADIO(pi) ||
			ACMAJORREV_36(pi->pubpi->phy_rev)) {
			if (CHSPEC_BW_LE20(pi->radio_chanspec)) {
				ACPHY_REG_LIST_START
					WRITE_PHYREG_ENTRY(pi, nonpaydecodetimeoutlen, 1)
					MOD_PHYREG_ENTRY(pi, timeoutEn, resetCCAontimeout, 1)
					MOD_PHYREG_ENTRY(pi, timeoutEn, nonpaydecodetimeoutEn, 1)
				ACPHY_REG_LIST_EXECUTE(pi);
			} else {
				ACPHY_REG_LIST_START
					WRITE_PHYREG_ENTRY(pi, nonpaydecodetimeoutlen, 32)
					MOD_PHYREG_ENTRY(pi, timeoutEn, resetCCAontimeout, 0)
					MOD_PHYREG_ENTRY(pi, timeoutEn, nonpaydecodetimeoutEn, 0)
				ACPHY_REG_LIST_EXECUTE(pi);
			}
	}

	/* 4360, 4350. 4335 does its own stuff */
	if (!ACMAJORREV_1(pi->pubpi->phy_rev)) {
		if (CHSPEC_BW_LE20(pi->radio_chanspec)) {
			/* reduce clip2 len, helps with humps due to late clip2 */
			WRITE_PHYREG(pi, defer_setClip1_CtrLen, 20);
			WRITE_PHYREG(pi, defer_setClip2_CtrLen, 16);
		} else {
			/* increase clip1 len. Needed for 20in80, 40in80 cases */
			WRITE_PHYREG(pi, defer_setClip1_CtrLen, 30);
			WRITE_PHYREG(pi, defer_setClip2_CtrLen, 20);
		}
	} else if (ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi) &&
		(!(PHY_ILNA(pi))) && pi->sh->chippkg != BCM4335_FCBGA_PKG_ID) {
		if (CHSPEC_IS80(pi->radio_chanspec)) {
			/* increase clip1 defer  len to make clip gain more accurate */
			/* decrease clip1 carrier blanking length to speedup crs */
			/* this is okay fror 80MHz as the settling is very fast for wider BW */
			ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, defer_setClip1_CtrLen, 36)
				WRITE_PHYREG_ENTRY(pi, defer_setClip2_CtrLen, 16)
				WRITE_PHYREG_ENTRY(pi, clip1carrierDetLen, 77)
				WRITE_PHYREG_ENTRY(pi, clip2carrierDetLen, 72)
			ACPHY_REG_LIST_EXECUTE(pi);
		} else {
		  /* increase defer setclip Gain by 0.1usec */
		  /* reduce clip1 carrier detect blanking by same amount */
		  /* reduce clip2 carrier detect blanking to speedup carrier detect */
		  /* this helps in cleaning the small floor in 4335C0 epa boards */
		  ACPHY_REG_LIST_START
			WRITE_PHYREG_ENTRY(pi, defer_setClip1_CtrLen, 28)
			WRITE_PHYREG_ENTRY(pi, defer_setClip2_CtrLen, 16)
			WRITE_PHYREG_ENTRY(pi, clip1carrierDetLen, 87)
			WRITE_PHYREG_ENTRY(pi, clip2carrierDetLen, 62)
		  ACPHY_REG_LIST_EXECUTE(pi);
		}
	}

	if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
		WRITE_PHYREG(pi, crsThreshold2u, 0x204d);
		WRITE_PHYREG(pi, crsThreshold2l, 0x204d);
		WRITE_PHYREG(pi, crsThreshold2lSub1, 0x204d);
		WRITE_PHYREG(pi, crsThreshold2uSub1, 0x204d);
	}

	if (ACMAJORREV_5(pi->pubpi->phy_rev) ||
		ACMAJORREV_4(pi->pubpi->phy_rev)) {
		/* CRS. 6bit MF */
		/* BPHY pre-detect is disabled by default. No writes here. */
		if (CHSPEC_IS20(pi->radio_chanspec)) {
			WRITE_PHYREG(pi, crsThreshold2u, 0x2055);
			WRITE_PHYREG(pi, crsThreshold2l, 0x2055);
		} else {
			WRITE_PHYREG(pi, crsThreshold2u, 0x204d);
			WRITE_PHYREG(pi, crsThreshold2l, 0x204d);
		}
		WRITE_PHYREG(pi, crsThreshold2lSub1, 0x204d);
		WRITE_PHYREG(pi, crsThreshold2uSub1, 0x204d);
	}

	if (ACMAJORREV_5(pi->pubpi->phy_rev)) {
		/* Spur canceller */
		if (CHSPEC_IS20(pi->radio_chanspec))
			WRITE_PHYREG(pi, spur_can_phy_bw_mhz, 0x14);
		else if (CHSPEC_IS40(pi->radio_chanspec))
			WRITE_PHYREG(pi, spur_can_phy_bw_mhz, 0x280);
		else
			WRITE_PHYREG(pi, spur_can_phy_bw_mhz, 0x50);
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {

		uint16 phymode = phy_get_phymode(pi);

		if (phymode == PHYMODE_MIMO) {
			if (CHSPEC_IS20(pi->radio_chanspec)) {
				MOD_PHYREG(pi, CRSMiscellaneousParam, crsMfFlipCoef, 0);
				ACPHYREG_BCAST(pi, crsThreshold2u0, 0x2055);
				ACPHYREG_BCAST(pi, crsThreshold2l0, 0x2055);
			} else {
				MOD_PHYREG(pi, CRSMiscellaneousParam, crsMfFlipCoef, 1);
			}
		}
	}

	if (PHY_IPA(pi) && ACMAJORREV_2(pi->pubpi->phy_rev) &&
	    (ACMINORREV_3(pi) || ACMINORREV_5(pi))) {
		/* 4354a1_ipa, to decrease LOFT, move TSSI_CONFIG & extra delay before IPA_PU. Need
		   to move in TSSI_CONFIG, otherwise only delaying IPA_PU would delay TSSI_CONFIG
		   ;80MHz alone this change is backed out..
		*/
		if (CHSPEC_IS20(pi->radio_chanspec)) {
			if (((CHSPEC_IS2G(pi->radio_chanspec)) &&
				(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x1)) ||
				((CHSPEC_IS5G(pi->radio_chanspec)) &&
				(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x4))) {
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
						16, rfseq_rx2tx_cmd_rev15_ipa_withtssisleep);
			} else {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
				                 16, rfseq_rx2tx_cmd_rev15_ipa);
			}
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
			                         16, rfseq_rx2tx_dly_rev15_ipa20);
		} else if (CHSPEC_IS40(pi->radio_chanspec)) {
			if (((CHSPEC_IS2G(pi->radio_chanspec)) &&
				(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x2)) ||
				((CHSPEC_IS5G(pi->radio_chanspec)) &&
				(pi->u.pi_acphy->chanmgri->cfg.srom_tssisleep_en & 0x8))) {
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
						16, rfseq_rx2tx_cmd_rev15_ipa_withtssisleep);
			} else {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
				                 16, rfseq_rx2tx_cmd_rev15_ipa);
			}
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
			                          16, rfseq_rx2tx_dly_rev15_ipa40);
		}
	}

	/* R8000 - atlas has different PA turn on timing */
	if (ACMAJORREV_0(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		epa_turnon_time = (BOARDFLAGS(GENERIC_PHY_INFO(pi)->boardflags) &
		                   BFL_SROM11_EPA_TURNON_TIME) >> BFL_SROM11_EPA_TURNON_TIME_SHIFT;
		if (epa_turnon_time == 1) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
			                          16, rfseq_rx2tx_cmd);
			if (CHSPEC_IS20(pi->radio_chanspec)) {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
				                          16, rfseq_rx2tx_dly_epa1_20);
			} else if (CHSPEC_IS40(pi->radio_chanspec)) {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
				                          16, rfseq_rx2tx_dly_epa1_40);
			} else {
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
				                          16, rfseq_rx2tx_dly_epa1_80);
			}
		}
	}

	// Need for Table Read Reliability in 4365
	if ((ACMAJORREV_32(pi->pubpi->phy_rev) ||
	     ACMAJORREV_33(pi->pubpi->phy_rev) ||
	     ACMAJORREV_37(pi->pubpi->phy_rev)) &&
		(CHSPEC_IS20(pi->radio_chanspec) ||
		CHSPEC_ISLE20(pi->radio_chanspec))) {
		MOD_PHYREG(pi, TableAccessCnt, TableAccessCnt_ClkIdle, 15);
	}

	if (ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		if (PHY_AS_80P80(pi, pi->radio_chanspec)) {
			phy_ac_chanmgr_switch_phymode_acphy(pi, PHYMODE_80P80);
		} else {
			phy_ac_chanmgr_switch_phymode_acphy(pi, PHYMODE_MIMO);
		}
	}

	if ((ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) &&
			PHY_AS_80P80(pi, pi->radio_chanspec)) {
		uint32 read_val[22];
		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_PHASETRACKTBL,
			22, 0x0, 32, &read_val);

		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_PHASETRACKTBL_B,
			22, 0x0, 32, &read_val);
	}

	ACPHY_ENABLE_STALL(pi, stall_val);
}
static void
wlc_phy_set_sfo_on_chan_change_acphy(phy_info_t *pi, uint8 ch)
{
	int fc;
	const uint16 *val_ptr = NULL;
	const uint16 *val_ptr1 = NULL;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	if (!TINY_RADIO(pi)) {

		if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			const chan_info_radio20695_rffe_t *chan_info;
			fc = wlc_phy_chan2freq_20695(pi, ch, &chan_info);
		} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
			const chan_info_radio20696_rffe_t *chan_info;
			fc = wlc_phy_chan2freq_20696(pi, ch, &chan_info);
		} else if ((ACMAJORREV_25(pi->pubpi->phy_rev)) ||
				(ACMAJORREV_40(pi->pubpi->phy_rev))) {
			const chan_info_radio20694_rffe_t *chan_info;
			fc = wlc_phy_chan2freq_20694(pi, ch, &chan_info);
		} else {
			const void *chan_info;
			fc = wlc_phy_chan2freq_acphy(pi, ch, &chan_info);
		}

		if (fc >= 0) {
			wlc_phy_write_sfo_params_acphy(pi, (uint16)fc);
		}
	} else {
		if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
			const chan_info_radio20691_t *ci20691;

			fc = wlc_phy_chan2freq_20691(pi, ch, &ci20691);

			if (fc >= 0) {
				wlc_phy_write_sfo_params_acphy(pi, (uint16)fc);
			}
		} else if (ACMAJORREV_4(pi->pubpi->phy_rev) ||
				ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev)) {
			const chan_info_radio20693_pll_t *pll_tbl;
			const chan_info_radio20693_rffe_t *rffe_tbl;
			const chan_info_radio20693_pll_wave2_t *pll_tbl_wave2;

			if (phy_get_phymode(pi) != PHYMODE_80P80) {
				if (PHY_AS_80P80(pi, pi->radio_chanspec)) {
					/* For First freq segment */
					uint8 chans[NUM_CHANS_IN_CHAN_BONDING];
					wf_chspec_get_80p80_channels(pi->radio_chanspec, chans);

					if (wlc_phy_chan2freq_20693(pi, chans[0], &pll_tbl,
							&rffe_tbl, &pll_tbl_wave2) >= 0) {
						val_ptr = &(pll_tbl_wave2->PHY_BW1a);
					}
					/* For second freq segment */
					if (wlc_phy_chan2freq_20693(pi, chans[1], &pll_tbl,
							&rffe_tbl, &pll_tbl_wave2) >= 0) {
						val_ptr1 = &(pll_tbl_wave2->PHY_BW1a);
					}
					if (val_ptr != NULL && val_ptr1 != NULL) {
					wlc_phy_write_sfo_80p80_params_acphy(pi, val_ptr, val_ptr1);
					} else {
						PHY_ERROR(("wl%d: %s: CFO/SFO settings fails!\n",
								pi->sh->unit, __FUNCTION__));
						ASSERT(0);
					}
				} else {
					fc = wlc_phy_chan2freq_20693(pi, ch, &pll_tbl, &rffe_tbl,
						&pll_tbl_wave2);
					if (fc >= 0) {
						if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
							ACMAJORREV_33(pi->pubpi->phy_rev)) {
							val_ptr = &(pll_tbl_wave2->PHY_BW1a);
							wlc_phy_write_sfo_params_acphy_wave2(pi,
								val_ptr);
						} else {
							wlc_phy_write_sfo_params_acphy(pi,
								(uint16)fc);
						}
#ifdef WL11ULB
						phy_ac_chanmgr_write_sfo_ulb_params_acphy(pi, fc);
#endif /* WL11ULB */
					}
				}
			} else {
				/* For First freq segment */
				ch = wf_chspec_primary80_channel(pi->radio_chanspec);
				fc = wlc_phy_chan2freq_20693(pi, ch, &pll_tbl, &rffe_tbl,
					&pll_tbl_wave2);
				if (fc >= 0) {
					if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
						ACMAJORREV_33(pi->pubpi->phy_rev)) {
						val_ptr = &(pll_tbl_wave2->PHY_BW1a);
						wlc_phy_write_sfo_params_acphy_wave2(pi, val_ptr);
					} else {
						wlc_phy_write_sfo_params_acphy(pi, (uint16)fc);
					}
				}
				/* For second freq segment */
				ch = wf_chspec_secondary80_channel(pi->radio_chanspec);
				if (wlc_phy_chan2freq_20693(pi, ch, &pll_tbl, &rffe_tbl,
					&pll_tbl_wave2) >= 0) {
					if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
						ACMAJORREV_33(pi->pubpi->phy_rev)) {
						val_ptr = &(pll_tbl_wave2->PHY_BW1a);
					} else {
						val_ptr = &(pll_tbl->PHY_BW1a);
					}
					wlc_phy_write_sfo_80p80_params_acphy(pi, val_ptr, NULL);
				}
			}
		}
	}

}

static void
wlc_phy_write_sfo_params_acphy(phy_info_t *pi, uint16 fc)
{
	uint16 phy_bw;
	uint32 tmp;
#define SFO_UNITY_FACTOR	2621440UL	/* 2.5 x 2^20 */

	/*
	 * sfo_chan_center_Ts20 = round([ fc-10e6  fc   fc+10e6] / 20e6 * 8), fc in Hz
	 *                      = round([$fc-10   $fc  $fc+10] * 0.4), $fc in MHz
	 */

	if (!TINY_RADIO(pi) && PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
		/* BW1a */
		phy_bw = (((fc + 10) * 4) + 5) / 10;
		WRITE_PHYREG(pi, BW1a, phy_bw);

		/* BW3 */
		phy_bw = (((fc - 10) * 4) + 5) / 10;
		WRITE_PHYREG(pi, BW3, phy_bw);
	}

	/* BW2 */
	phy_bw = ((fc * 4) + 5) / 10;
	WRITE_PHYREG(pi, BW2, phy_bw);

	/*
	 * sfo_chan_center_factor = round(2^17 / ([fc-10e6 fc fc+10e6]/20e6)/(ten_mhz+1)), fc in Hz
	 *                        = round(2621440 ./ [$fc-10 $fc $fc+10]/($ten_mhz+1)), $fc in MHz
	 */

	if (!TINY_RADIO(pi) && PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
		/* BW4 */
		tmp = fc + 10;
		phy_bw = (uint16)((SFO_UNITY_FACTOR + tmp / 2) / tmp);
		WRITE_PHYREG(pi, BW4, phy_bw);

		/* BW6 */
		tmp = fc - 10;
		phy_bw = (uint16)((SFO_UNITY_FACTOR + tmp / 2) / tmp);
		WRITE_PHYREG(pi, BW6, phy_bw);
	}

	/* BW5 */
	tmp = fc;
	phy_bw = (uint16)((SFO_UNITY_FACTOR + tmp / 2) / tmp);
	WRITE_PHYREG(pi, BW5, phy_bw);
}

static void
wlc_phy_write_sfo_params_acphy_wave2(phy_info_t *pi, const uint16 *val_ptr)
{
#ifdef WL11ULB
	if (PHY_ULB_ENAB(pi->sh->physhim)) {
		if (CHSPEC_IS10(pi->radio_chanspec) ||
				CHSPEC_IS5(pi->radio_chanspec) ||
				CHSPEC_IS2P5(pi->radio_chanspec))
			return;
	}
#endif /* WL11ULB */

	ASSERT(val_ptr != NULL);
	if (val_ptr != NULL) {
		/* set SFO parameters */
		WRITE_PHYREG(pi, BW1a, val_ptr[0]);
		WRITE_PHYREG(pi, BW2, val_ptr[1]);
		WRITE_PHYREG(pi, BW3, val_ptr[2]);
		/* Set sfo_chan_center_factor */
		WRITE_PHYREG(pi, BW4, val_ptr[3]);
		WRITE_PHYREG(pi, BW5, val_ptr[4]);
		WRITE_PHYREG(pi, BW6, val_ptr[5]);
	}
}

static void
wlc_phy_write_sfo_80p80_params_acphy(phy_info_t *pi, const uint16 *val_ptr, const uint16 *val_ptr1)
{
	ASSERT(val_ptr != NULL);
	if (val_ptr != NULL) {
		if (ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
			ASSERT(val_ptr1 != NULL);
			/* set SFO parameters */
			WRITE_PHYREG(pi, BW1a0, val_ptr[0]);
			WRITE_PHYREG(pi, BW1a1, val_ptr1[0]);
			WRITE_PHYREG(pi, BW20, val_ptr[1]);
			WRITE_PHYREG(pi, BW21, val_ptr1[1]);
			WRITE_PHYREG(pi, BW30, val_ptr[2]);
			WRITE_PHYREG(pi, BW31, val_ptr1[2]);
			/* Set sfo_chan_center_factor */
			WRITE_PHYREG(pi, BW40, val_ptr[3]);
			WRITE_PHYREG(pi, BW41, val_ptr1[3]);
			WRITE_PHYREG(pi, BW50, val_ptr[4]);
			WRITE_PHYREG(pi, BW51, val_ptr1[4]);
			WRITE_PHYREG(pi, BW60, val_ptr[5]);
			WRITE_PHYREG(pi, BW61, val_ptr1[5]);
		} else {
			/* set SFO parameters */
			WRITE_PHYREG(pi, BW1a1, val_ptr[0]);
			WRITE_PHYREG(pi, BW21, val_ptr[1]);
			WRITE_PHYREG(pi, BW31, val_ptr[2]);
			/* Set sfo_chan_center_factor */
			WRITE_PHYREG(pi, BW41, val_ptr[3]);
			WRITE_PHYREG(pi, BW51, val_ptr[4]);
			WRITE_PHYREG(pi, BW61, val_ptr[5]);
		}
	}
}

#ifdef WL11ULB
static void
phy_ac_chanmgr_write_sfo_ulb_params_acphy(phy_info_t *pi, int freq)
{
	if (PHY_ULB_ENAB(pi->sh->physhim)) {
		uint8 ulb_mode = PMU_ULB_BW_NONE;
		uint8 div = 1;

		if (CHSPEC_IS10(pi->radio_chanspec)) {
			ulb_mode = PMU_ULB_BW_10MHZ;
			div = 2;
		} else if (CHSPEC_IS5(pi->radio_chanspec)) {
			ulb_mode = PMU_ULB_BW_5MHZ;
			div = 4;
		} else if (CHSPEC_IS2P5(pi->radio_chanspec)) {
			ulb_mode = PMU_ULB_BW_2P5MHZ;
			div = 8;
		}

		if (ulb_mode == PMU_ULB_BW_NONE) {
			MOD_PHYREG(pi, PhaseTrackOffset, sfo_corr_ulb, 0x0);
		} else {
			MOD_PHYREG(pi, PhaseTrackOffset, sfo_corr_ulb, 0x1);
			WRITE_PHYREG(pi, BW8, freq * div * 4/10);
			WRITE_PHYREG(pi, BW9, (2621440 + freq*div/2) / (freq*div));
		}
	}
}
#endif /* WL11ULB */

static void
chanspec_setup_regtbl_on_chan_change(phy_info_t *pi)
{
	uint32 rx_afediv_sel, tx_afediv_sel;
	uint32 read_val[2], write_val[2];
	bool suspend = 0;
	uint8 stall_val = 0, orig_rxfectrl1 = 0;
	uint8 bphy_testmode_val, core = 0;
	uint8 ch[NUM_CHANS_IN_CHAN_BONDING];

	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	uint16 rfseq_bundle_adcrst48[3];
	uint16 rfseq_bundle_adcrst49[3];
	uint16 rfseq_bundle_adcrst50[3];

	/* get the center freq */
	int fc = pi_ac->fc;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	/* get the operating channels */
	chanspec_get_operating_channels(pi, ch);

	/* -ve freq means channel not found in tuning table */
	if (fc < 0)
		return;

	PHY_CHANLOG(pi, __FUNCTION__, TS_ENTER, 0);

	/* Setup the Tx/Rx Farrow resampler */
	if (TINY_RADIO(pi)) {
		wlc_phy_farrow_setup_tiny(pi, pi->radio_chanspec);
	} else {
		if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
			wlc_phy_farrow_setup_28nm(pi, 1 /* DAC rate mode */ );
		} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
			wlc_phy_farrow_setup_28nm(pi, 1 /* DAC rate mode */ );
			/* Configure AFE div */
			wlc_phy_radio20696_afe_div_ratio(pi);
		} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			wlc_phy_farrow_setup_28nm_ulp(pi, pi->u.pi_acphy->ulp_tx_mode,
				pi->u.pi_acphy->ulp_adc_mode);
			/* Configure AFE div */
			wlc_phy_radio20695_afe_div_ratio(pi, pi_ac->ulp_tx_mode,
			pi_ac->ulp_adc_mode);
		} else if ((ACMAJORREV_25(pi->pubpi->phy_rev)) ||
				(ACMAJORREV_40(pi->pubpi->phy_rev))) {
			wlc_phy_farrow_setup_20694(pi, pi->u.pi_acphy->ulp_tx_mode,
				pi->u.pi_acphy->ulp_adc_mode);
			/* Configure AFE div */
			wlc_phy_radio20694_afe_div_ratio(pi, pi_ac->ulp_tx_mode,
			pi_ac->ulp_adc_mode);
		} else {
			wlc_phy_farrow_setup_acphy(pi, pi->radio_chanspec);
		}
	}

	/* Load Pdet related settings */
	wlc_phy_set_pdet_on_reset_acphy(pi);

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
		!ACMAJORREV_33(pi->pubpi->phy_rev)) {
		suspend = !(R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC);
		if (!suspend)
			wlapi_suspend_mac_and_wait(pi->sh->physhim);

		/* Disable stalls and hold FIFOs in reset */
		stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
		orig_rxfectrl1 = READ_PHYREGFLD(pi, RxFeCtrl1, soft_sdfeFifoReset);

		ACPHY_DISABLE_STALL(pi);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);
	}

	/* 4350A0 radio */
	if ((RADIOID_IS(pi->pubpi->radioid, BCM2069_ID)) &&
	    (RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
	    !(ISSIM_ENAB(pi->sh->sih))) {

		/* AFE clk and Harmonic of 40 MHz crystal causes a spur at 417 Khz */
		if (CHSPEC_IS20(pi->radio_chanspec)) {
			phy_ac_radio_data_t *radio_data = phy_ac_radio_get_data(pi_ac->radioi);
			if ((radio_data->srom_txnospurmod2g == 0) &&
				(CHSPEC_IS2G(pi->radio_chanspec)) && !PHY_IPA(pi)) {
				/* AFE divider of 4.5 */
				/* i_iqadc_adc_bias 2 */
				/* iqmode 20 */
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
				                         &read_val);
				rx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
				                 ~(0x3) & 0xfffff) |
				        (0x2 << 14 | 0x2 << 11 | 0x3);
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0, 60,
				                         &read_val);
				tx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
				                 ~(0x3) & 0xfffff) |
				        (0x2 << 14 | 0x2 << 11 | 0x3);
			} else if ((CHSPEC_IS2G(pi->radio_chanspec) &&
				((fc != 2412 && fc != 2467) || (pi->xtalfreq == 40000000) ||
				(ACMAJORREV_2(pi->pubpi->phy_rev) &&
				(ACMINORREV_1(pi) ||
				ACMINORREV_3(pi) ||
				ACMINORREV_5(pi)) &&
				pi->xtalfreq == 37400000 && PHY_ILNA(pi)))) ||
				(fc == 5745) || (fc == 5765) || (fc == 5825 && !PHY_IPA(pi)) ||
				((fc == 5180) && ((((RADIOMINORREV(pi) == 4) ||
				(RADIOMINORREV(pi) == 10) ||
				(RADIOMINORREV(pi) == 11) ||
				(RADIOMINORREV(pi) == 13)) &&
				(pi->sh->chippkg == 2)) ||
				(RADIOMINORREV(pi) == 7) ||
				(RADIOMINORREV(pi) == 9) ||
				(RADIOMINORREV(pi) == 8) ||
				(RADIOMINORREV(pi) == 12)) &&
				(pi->xtalfreq == 37400000))) {
				/* if AFE divider of 8 is used for 20 MHz channel 149,153, */
				/* or any channel in 2GHz when xtalfreq=40MHz, */
				/* or any 2Ghz channel except 2467 when xtalfreq=37.4MHz */
				/* so change divider ratio to 9 */
				/* i_iqadc_adc_bias 2 */
				/* iqmode 20 */
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
				                         &read_val);
				rx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
				                 ~(0x3) & 0xfffff) |
				        (0x4 << 14 | 0x2 << 11 | 0x3);
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0, 60,
				                         &read_val);
				tx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
				                 ~(0x3) & 0xfffff) |
				        (0x4 << 14 | 0x2 << 11 | 0x3);
			} else {
				/* AFE divider of 8 */
				/* i_iqadc_adc_bias 2 */
				/* iqmode 20 */
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
				                         &read_val);
				rx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
				                 ~(0x3) & 0xfffff) |
				        (0x3 << 14 | 0x2 << 11 | 0x3);
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0, 60,
				                         &read_val);
				tx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
				                 ~(0x3) & 0xfffff) |
				        (0x3 << 14 | 0x2 << 11 | 0x3);
			}
			if (!PHY_IPA(pi) && CHSPEC_IS5G(pi->radio_chanspec)) {
				if ((((fc == 5180) && (pi->sh->chippkg != 2)) ||
				     ((fc >= 5200) && (fc <= 5320)) ||
				     ((fc >= 5745) && (fc <= 5825)))) {
					/* AFE div 5 for tx/rx  (bits 13:15) */
					/* i_iqadc_adc_bias 0 (bits 11:12) for stability */
					/* iqmode 40 (bits 0:2) to fix TSSI issue */
					wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6,
					                         60, &read_val);
					rx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
					                 ~(0x3) & 0xfffff) |
					        (0x7 << 14 | 0x0 << 11 | 0x2);
					wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0,
					                         60, &read_val);
					tx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
					                 ~(0x3) & 0xfffff) |
					        (0x7 << 14 | 0x0 << 11 | 0x2);
				} else {
					/* AFE div 8 for tx/rx */
					/* i_iqadc_adc_bias 2 */
					/* iqmode 20 */
					wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6,
					                         60, &read_val);
					rx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
					                 ~(0x3) & 0xfffff) |
					        (0x3 << 14 | 0x2 << 11 | 0x3);
					wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0,
					                         60, &read_val);
					tx_afediv_sel = (read_val[0] & ~(0x7 << 14) & ~(0x3 << 11) &
					                 ~(0x3) & 0xfffff) |
					        (0x3 << 14 | 0x2 << 11 | 0x3);
				}
			}
			/* RX_SD_ADC_PU_VAL bw20 */
			write_val[0] = ((rx_afediv_sel & 0xfff) << 20) | rx_afediv_sel;
			write_val[1] = (rx_afediv_sel << 8) | (rx_afediv_sel >> 12);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 6, 60,
			                          write_val);
			/* bw20_HighspeedMode1 */
			write_val[0] = ((tx_afediv_sel & 0xfff) << 20) | tx_afediv_sel;
			write_val[1] = (tx_afediv_sel << 8) | (tx_afediv_sel >> 12);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 0, 60,
			                          write_val);
			wlc_phy_force_rfseq_noLoleakage_acphy(pi);
		} else if (CHSPEC_IS40(pi->radio_chanspec)) {
			/* if AFE divider of 4 is used for 40 MHz channel 151m,
			 * so change divider ratio to 4.5
			 */
			if (CHSPEC_IS5G(pi->radio_chanspec) &&
			    !PHY_IPA(pi) && (fc != 5190)) {
				/* AFE div 5 for tx/rx */
				rx_afediv_sel = (sdadc_cfg40 & ~(0x7 << 14) & 0xfffff) |
				        (0x7 << 14) | (0x3 << 17);
				tx_afediv_sel = (sdadc_cfg40 & ~(0x7 << 14) & 0xfffff) |
				        (0x7 << 14);
			} else if (CHSPEC_IS5G(pi->radio_chanspec) &&
			           !PHY_IPA(pi) && (fc == 5190)) {
				/* AFE div 3 for tx/rx, bw80 mode */
				rx_afediv_sel = (sdadc_cfg80 & ~(0x7 << 14) & 0xfffff) |
				        (0x0 << 14) | (0x3 << 17);
				tx_afediv_sel = (sdadc_cfg80 & ~(0x7 << 14) & 0xfffff) |
				        (0x0 << 14);
			} else if ((CHSPEC_IS2G(pi->radio_chanspec)) || (fc == 5755) ||
				(fc == 5550 && pi->xtalfreq == 40000000) ||
				(fc == 5310 && pi->xtalfreq == 37400000 && PHY_IPA(pi))) {
				/* AFE div 4.5 for tx/rx */
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 7, 60,
				                         &read_val);
				rx_afediv_sel = (read_val[0] & ~(0x7 << 14) & 0xfffff) |
				        (0x2 << 14);
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 1, 60,
				                         &read_val);
				tx_afediv_sel = (read_val[0] & ~(0x7 << 14) & 0xfffff) |
				        (0x2 << 14);
			} else {
				/* AFE div 4 for tx/rx */
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 7, 60,
				                         &read_val);
				rx_afediv_sel = (read_val[0] & ~(0x7 << 14) & 0xfffff) |
				        (0x1 << 14);
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 1, 60,
				                         &read_val);
				tx_afediv_sel = (read_val[0] & ~(0x7 << 14) & 0xfffff) |
				        (0x1 << 14);
			}
			/* RX_SD_ADC_PU_VAL bw40 */
			write_val[0] = ((rx_afediv_sel & 0xfff) << 20) | rx_afediv_sel;
			write_val[1] = (rx_afediv_sel << 8) | (rx_afediv_sel >> 12);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 7, 60,
			                          write_val);
			/* bw40_HighspeedMode1 */
			write_val[0] = ((tx_afediv_sel & 0xfff) << 20) | tx_afediv_sel;
			write_val[1] = (tx_afediv_sel << 8) | (tx_afediv_sel >> 12);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 1, 60,
			                          write_val);
			wlc_phy_force_rfseq_noLoleakage_acphy(pi);
		}
	}

	/* JIRA (HW11ACRADIO-30) - clamp_en needs to be high for ~1us for clipped pkts (80mhz) */
	if ((CHSPEC_IS80(pi->radio_chanspec) ||
	     (CHSPEC_IS40(pi->radio_chanspec) && (RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
	      !PHY_IPA(pi) && (fc == 5190))) &&
	    !TINY_RADIO(pi) && !ACMAJORREV_37(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_40(pi->pubpi->phy_rev)) {
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_iqadc_clamp_en, 1);
			MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqadc_clamp_en, 1);}

		rfseq_bundle_adcrst48[2]  = 0;
		rfseq_bundle_adcrst49[2]  = 0;
		rfseq_bundle_adcrst50[2]  = 0;
		if (CHSPEC_IS20(pi->radio_chanspec)) {
			rfseq_bundle_adcrst48[0] = 0xef52;
			rfseq_bundle_adcrst48[1] = 0x94;
			rfseq_bundle_adcrst49[0] = 0xef42;
			rfseq_bundle_adcrst49[1] = 0x84;
			rfseq_bundle_adcrst50[0] = 0xef52;
			rfseq_bundle_adcrst50[1] = 0x84;
		} else if (CHSPEC_IS40(pi->radio_chanspec)) {
			if ((RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) && !PHY_IPA(pi) &&
			    (fc == 5190)) {
				rfseq_bundle_adcrst48[0] = 0x0fd2;
				rfseq_bundle_adcrst48[1] = 0x96;
				rfseq_bundle_adcrst49[0] = 0x0fc2;
				rfseq_bundle_adcrst49[1] = 0x86;
				rfseq_bundle_adcrst50[0] = 0x0fd2;
				rfseq_bundle_adcrst50[1] = 0x86;
			} else {
				rfseq_bundle_adcrst48[0] = 0x4f52;
				rfseq_bundle_adcrst48[1] = 0x94;
				rfseq_bundle_adcrst49[0] = 0x4f42;
				rfseq_bundle_adcrst49[1] = 0x84;
				rfseq_bundle_adcrst50[0] = 0x4f52;
				rfseq_bundle_adcrst50[1] = 0x84;
			}
		} else {
			rfseq_bundle_adcrst48[0] = 0x0fd2;
			rfseq_bundle_adcrst48[1] = 0x96;
			rfseq_bundle_adcrst49[0] = 0x0fc2;
			rfseq_bundle_adcrst49[1] = 0x86;
			rfseq_bundle_adcrst50[0] = 0x0fd2;
			rfseq_bundle_adcrst50[1] = 0x86;
		}
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 48, 48,
		                          rfseq_bundle_adcrst48);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 49, 48,
		                          rfseq_bundle_adcrst49);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 50, 48,
		                          rfseq_bundle_adcrst50);
		/* reduce the adc reset time from 250ns to 50ns for 43602 as it caused CSTR failure
		* when ADC clips during clip gain
		*/
		if (ACMAJORREV_5(pi->pubpi->phy_rev)) {
			rf_updh_dly_adcrst[4] = 0x2;
			rf_updl_dly_adcrst[4] = 0x2;
		}
		/* updategainH : issue adc reset for 250ns */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0x30, 16, rf_updh_cmd_adcrst);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xa0, 16, rf_updh_dly_adcrst);

		/* updategainL : issue adc reset for 250ns */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0x40, 16, rf_updl_cmd_adcrst);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xb0, 16, rf_updl_dly_adcrst);

		/* updategainU : issue adc reset for 250n */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0x50, 16, rf_updu_cmd_adcrst);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xc0, 16, rf_updu_dly_adcrst);
	} else {
		if (!ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMAJORREV_33(pi->pubpi->phy_rev)) {
			FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
				/* 4360A0 : SD-ADC was not monotonic for 1st revision
				* but is fixed now
				*/
				if (ACREV_IS(pi->pubpi->phy_rev, 0)) {
					MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core,
						afe_iqadc_clamp_en, 0);
				} else {
					MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core,
						afe_iqadc_clamp_en, 1);
				}
				MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqadc_clamp_en, 1);
			}

			/* updategainH : increase clamp_en off delay to 16 */
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0x30, 16,
				rf_updh_cmd_clamp);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xa0, 16,
				rf_updh_dly_clamp);

			/* updategainL : increase clamp_en off delay to 16 */
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0x40, 16,
				rf_updl_cmd_clamp);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xb0, 16,
				rf_updl_dly_clamp);

			/* updategainU : increase clamp_en off delay to 16 */
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0x50, 16,
				rf_updu_cmd_clamp);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 8, 0xc0, 16,
				rf_updu_dly_clamp);
		}
	}

	if (ACMAJORREV_2(pi->pubpi->phy_rev) && !(PHY_IPA(pi))) {
		uint16 rfseq_bundle_adcdacoff51[3];
		/* Add AFE Power down to RFSeq */

		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			rfseq_bundle_adcdacoff51[0] = 0xef72;
			rfseq_bundle_adcdacoff51[1] = 0x84;
		} else {
			if (CHSPEC_IS20(pi->radio_chanspec)) {
				if (((fc >= 5180) && (fc <= 5320)) ||
				((fc >= 5745) && (fc <= 5825))) {
					rfseq_bundle_adcdacoff51[0] = 0x8f72;
					rfseq_bundle_adcdacoff51[1] = 0x84;
				} else {
					rfseq_bundle_adcdacoff51[0] = 0xef72;
					rfseq_bundle_adcdacoff51[1] = 0x84;
				}
			} else if (CHSPEC_IS40(pi->radio_chanspec)) {
				if (fc == 5190) {
					rfseq_bundle_adcdacoff51[0] = 0x0ff2;
					rfseq_bundle_adcdacoff51[1] = 0x86;
				} else {
					rfseq_bundle_adcdacoff51[0] = 0x8f72;
					rfseq_bundle_adcdacoff51[1] = 0x84;
				}
			} else {
				rfseq_bundle_adcdacoff51[0] = 0x0ff2;
				rfseq_bundle_adcdacoff51[1] = 0x86;
			}
		}
		/* Below bundle shuts off all DACs at the beginning of TX2RX sequence */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQBUNDLE, 1, 51, 48,
		rfseq_bundle_adcdacoff51);

		wlc_phy_set_sdadc_pd_val_per_core_acphy(pi);
	}

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMAJORREV_33(pi->pubpi->phy_rev)) {
		if (!suspend)
			wlapi_enable_mac(pi->sh->physhim);

		/* Restore FIFO reset and Stalls */
		ACPHY_ENABLE_STALL(pi, stall_val);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, orig_rxfectrl1);
	}

	wlc_phy_set_sfo_on_chan_change_acphy(pi, ch[0]);

	/* Set the correct primary channel */
	if (CHSPEC_IS8080(pi->radio_chanspec) || (CHSPEC_IS160(pi->radio_chanspec) &&
		(ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)))) {
		/* 80P80 */
		if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_LLL) {
			MOD_PHYREG(pi, ClassifierCtrl_80p80, prim_sel_hi, 0);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 0);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_LLU) {
			MOD_PHYREG(pi, ClassifierCtrl_80p80, prim_sel_hi, 0);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 1);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_LUL) {
			MOD_PHYREG(pi, ClassifierCtrl_80p80, prim_sel_hi, 0);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 2);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_LUU) {
			MOD_PHYREG(pi, ClassifierCtrl_80p80, prim_sel_hi, 0);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 3);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_ULL) {
			MOD_PHYREG(pi, ClassifierCtrl_80p80, prim_sel_hi, 1);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 0);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_ULU) {
			MOD_PHYREG(pi, ClassifierCtrl_80p80, prim_sel_hi, 1);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 1);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_UUL) {
			MOD_PHYREG(pi, ClassifierCtrl_80p80, prim_sel_hi, 1);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 2);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_UUU) {
			MOD_PHYREG(pi, ClassifierCtrl_80p80, prim_sel_hi, 1);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 3);
		} else {
			PHY_ERROR(("%s: No primary channel settings for CTL_SB=%d\n",
			           __FUNCTION__, CHSPEC_CTL_SB(pi->radio_chanspec)));
		}
	} else if (CHSPEC_IS80(pi->radio_chanspec)) {
		/* 80mhz */
		if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_LL) {
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 0);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_LU) {
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 1);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_UL) {
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 2);
		} else if (CHSPEC_CTL_SB(pi->radio_chanspec) == WL_CHANSPEC_CTL_SB_UU) {
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 3);
		} else {
			PHY_ERROR(("%s: No primary channel settings for CTL_SB=%d\n",
			           __FUNCTION__, CHSPEC_CTL_SB(pi->radio_chanspec)));
		}
	} else if (CHSPEC_IS40(pi->radio_chanspec)) {
		/* 40mhz */
		if (CHSPEC_SB_UPPER(pi->radio_chanspec)) {
			MOD_PHYREG(pi, RxControl, bphy_band_sel, 1);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 1);
		} else {
			MOD_PHYREG(pi, RxControl, bphy_band_sel, 0);
			MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 0);
		}
	} else if (CHSPEC_BW_LE20(pi->radio_chanspec)) {
		/* 20mhz */
		MOD_PHYREG(pi, RxControl, bphy_band_sel, 0);
		MOD_PHYREG(pi, ClassifierCtrl2, prim_sel, 0);
	} else {
		PHY_ERROR(("%s: No primary channel settings for bw=%d\n",
		           __FUNCTION__, CHSPEC_BW(pi->radio_chanspec)));
	}

	/* set aci thresholds */
	wlc_phy_set_aci_regs_acphy(pi);

	bzero((uint8 *)pi->u.pi_acphy->phy_noise_all_core,
	      sizeof(pi->u.pi_acphy->phy_noise_all_core));
	bzero((uint8 *)pi->u.pi_acphy->phy_noise_in_crs_min,
	      sizeof(pi->u.pi_acphy->phy_noise_in_crs_min));
	bzero((uint8 *)pi->u.pi_acphy->phy_noise_pwr_array,
	      sizeof(pi->u.pi_acphy->phy_noise_pwr_array));

	/* Debug parameters: printed by 'wl dump phycal' */
	pi->u.pi_acphy->phy_debug_crscal_counter = 0;
	pi->u.pi_acphy->phy_noise_counter = 0;

	/* set the crsmin_th from cache at chan_change */
	wlc_phy_crs_min_pwr_cal_acphy(pi, PHY_CRS_SET_FROM_CACHE);

	/* making IIR filter gaussian like for BPHY to improve ACPR */

	/* set RRC filter alpha
	 FiltSel2 is 11 bit which msb, bphyTest's 6th bit is lsb
	 These 2 bits control alpha
	 bits 11 & 6    Resulting filter
	  -----------    ----------------
	      00         alpha=0.35 - default
	      01         alpha=0.75 - alternate
	      10         alpha=0.2  - for use in Japan on channel 14
	      11         no TX filter
	*/
	if ((fc == 2484) && (!CHSPEC_IS8080(pi->radio_chanspec))) {
		bphy_testmode_val = (0x3F & READ_PHYREGFLD(pi, bphyTest, testMode));
		MOD_PHYREG(pi, bphyTest, testMode, bphy_testmode_val);
		MOD_PHYREG(pi, bphyTest, FiltSel2, 1);
		/* Load default filter */
		wlc_phy_set_tx_iir_coeffs(pi, 1, 0);
	} else {
		if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
			MOD_PHYREG(pi, bphyTest, FiltSel2, 0);
			if (PHY_IPA(pi)) {
				wlc_phy_set_tx_iir_coeffs(pi, 1, 2);
			} else {
				wlc_phy_set_tx_iir_coeffs(pi, 1, 1);
			}
		} else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		           ACMAJORREV_33(pi->pubpi->phy_rev) ||
		           ACMAJORREV_37(pi->pubpi->phy_rev)) {
			MOD_PHYREG(pi, bphyTest, FiltSel2, 0);
			wlc_phy_set_tx_iir_coeffs(pi, 1, 2);
		} else if ACMAJORREV_3(pi->pubpi->phy_rev) {
			MOD_PHYREG(pi, bphyTest, testMode, 0);
			MOD_PHYREG(pi, bphyTest, FiltSel2, 0);
		} else if ACMAJORREV_4(pi->pubpi->phy_rev) {
				if (!PHY_IPA(pi) && !ROUTER_4349(pi)) {
					MOD_PHYREG(pi, bphyTest, testMode,
						(0x3F & READ_PHYREGFLD(pi, bphyTest, testMode)));
					MOD_PHYREG(pi, bphyTest, FiltSel2, 0);
				} else {
					MOD_PHYREG(pi, bphyTest, testMode, 0);
					MOD_PHYREG(pi, bphyTest, FiltSel2, 1);
				}
				wlc_phy_set_tx_iir_coeffs(pi, 1, pi->sromi->cckfilttype);
				wlc_phy_set_tx_iir_coeffs(pi, 0, 0); /* default setting for ofdm */
				/* JIRA: SW4349-1379
				 * FOR ePA chips, to improve CCK spectral mask margins, bphy scale
				 * is increased to 0x5F. this improves SM margins by >2dB, with an
				 * acceptable degradation in the EVM. This change also mandates the
				 * use of separate loop for cck TPC (target power control)
				 */
				if (!PHY_IPA(pi) && !ROUTER_4349(pi)) {
					MOD_PHYREG(pi, BphyControl3, bphyScale20MHz, 0x4d);
				} else if (ROUTER_4349(pi)) {
					/* bphyScale to equalize gain between cck and ofdm frames */
					MOD_PHYREG(pi, BphyControl3, bphyScale20MHz, 0x23);
				} else {
					MOD_PHYREG(pi, BphyControl3, bphyScale20MHz, 0x3b);
				}
				if (((fc == 2412) || (fc == 2462) || (fc == 2467) ||
					(fc == 2472)) &&
					(pi->sromi->ofdmfilttype_2g != 127)) {
					wlc_phy_set_tx_iir_coeffs(pi, 0,
						pi->sromi->ofdmfilttype_2g);
				} else if (((fc == 5240) || (fc == 5260) || (fc == 5580) ||
					(fc == 5660)) &&
					(pi->sromi->ofdmfilttype != 127)) {
					wlc_phy_set_tx_iir_coeffs(pi, 0, pi->sromi->ofdmfilttype);
				}
		} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			bphy_testmode_val = (0x3F & READ_PHYREGFLD(pi, bphyTest, testMode));
			bphy_testmode_val = bphy_testmode_val |
				((pi->u.pi_acphy->chanmgri->acphy_cck_dig_filt_type & 0x2)  << 5);
			MOD_PHYREG(pi, bphyTest, testMode, bphy_testmode_val);
			MOD_PHYREG(pi, bphyTest, FiltSel2,
				((pi->u.pi_acphy->chanmgri->acphy_cck_dig_filt_type & 0x4) >> 2));
			/* Load filter with Gaussian shaping */
			wlc_phy_set_tx_iir_coeffs(pi, 1,
				(pi->u.pi_acphy->chanmgri->acphy_cck_dig_filt_type));
			MOD_PHYREG(pi, BphyControl3, bphyScale20MHz, 0x3b);
		} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		} else {
			bphy_testmode_val = (0x3F & READ_PHYREGFLD(pi, bphyTest, testMode));
			bphy_testmode_val = bphy_testmode_val |
				((pi->u.pi_acphy->chanmgri->acphy_cck_dig_filt_type & 0x2)  << 5);
			MOD_PHYREG(pi, bphyTest, testMode, bphy_testmode_val);
			MOD_PHYREG(pi, bphyTest, FiltSel2,
				((pi->u.pi_acphy->chanmgri->acphy_cck_dig_filt_type & 0x4) >> 2));
			/* Load filter with Gaussian shaping */
			wlc_phy_set_tx_iir_coeffs(pi, 1,
				(pi->u.pi_acphy->chanmgri->acphy_cck_dig_filt_type & 0x1));
		}
		if (ACMAJORREV_1(pi->pubpi->phy_rev) && PHY_IPA(pi)) {
			MOD_PHYREG(pi, bphyTest, testMode, 0);
			MOD_PHYREG(pi, bphyTest, FiltSel2, 0);
			wlc_phy_set_tx_iir_coeffs(pi, 1,
				(pi->u.pi_acphy->chanmgri->acphy_cck_dig_filt_type & 0xF));
		}
	}

#ifdef WL_ETMODE
	if (ET_ENAB(pi)) {
		phy_ac_et(pi);
	}
#endif /* WL_ETMODE */

	/* if it's 2x2 or 3x3 design, populate the reciprocity compensation coeff */
	if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		if (!(ACMAJORREV_33(pi->pubpi->phy_rev) &&
				PHY_AS_80P80(pi, pi->radio_chanspec))) {
			wlc_phy_populate_recipcoeffs_acphy(pi);
		}
	}

	#ifndef MACOSX
	if (ACMAJORREV_0(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev))
		wlc_phy_populate_recipcoeffs_acphy(pi);
	#endif /* MACOSX */

	/* 4335c0 wlipa 2GHz xtal spur war */
	if (ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi) && PHY_ILNA(pi)) {
		ACPHY_REG_LIST_START
			MOD_RADIO_REG_ENTRY(pi, RFP, GE16_OVR27, ovr_xtal_outbufBBstrg, 1)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL4, xtal_outbufBBstrg, 0)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL4, xtal_outbufcalstrg, 0)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL5, xtal_bufstrg_BT, 1)
			MOD_RADIO_REG_ENTRY(pi, RFP, GE16_OVR27, ovr_xtal_xtbufstrg, 1)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL4, xtal_xtbufstrg, 7)
			MOD_RADIO_REG_ENTRY(pi, RFP, GE16_OVR27, ovr_xtal_outbufstrg, 1)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL4, xtal_outbufstrg, 3)
		ACPHY_REG_LIST_EXECUTE(pi);
	}

	/* 4354 wlipa 2GHz xtal spur war */
	if (ACMAJORREV_2(pi->pubpi->phy_rev) && (ACMINORREV_1(pi) ||
		ACMINORREV_3(pi)) && PHY_ILNA(pi)) {
		ACPHY_REG_LIST_START
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL2, xtal_pu_RCCAL1, 0)
			MOD_RADIO_REG_ENTRY(pi, RFP, GE16_OVR27, ovr_xtal_outbufBBstrg, 1)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL4, xtal_outbufBBstrg, 0)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL4, xtal_outbufcalstrg, 0)
			MOD_RADIO_REG_ENTRY(pi, RFP, GE16_OVR27, ovr_xtal_outbufstrg, 1)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL4, xtal_outbufstrg, 2)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL5, xtal_sel_BT, 1)
			MOD_RADIO_REG_ENTRY(pi, RFP, PLL_XTAL5, xtal_bufstrg_BT, 2)
		ACPHY_REG_LIST_EXECUTE(pi);
	}

	/* WAR for JIRA SWWLAN-32607: point fix for MCH5 board bw 80mhz mode */
	if (BFCTL(pi->u.pi_acphy) == 3) {
		if (fc == 5180 || fc == 5190 || fc == 5310 ||
		    fc == 5320 || fc == 5500 || fc == 5510) {
			MOD_RADIO_REG(pi, RFP, PLL_CP4, rfpll_cp_ioff, 0xA0);
		}
	}

	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		femctrl_clb_4347(pi,
			pi->u.pi_acphy->chanmgri->data.curr_band2g,
			wlapi_si_coreunit(pi->sh->physhim) == DUALMAC_AUX ? 1 : 0);
	}

	/* WAR for 4365 */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
		MOD_RADIO_ALLREG_20693(pi, RX_TOP_5G_OVR, ovr_lna5g_nap, 1);
		MOD_RADIO_ALLREG_20693(pi, LNA5G_CFG2, lna5g_nap, 0);
		MOD_RADIO_ALLREG_20693(pi, RX_TOP_2G_OVR_EAST, ovr_lna2g_nap, 1);
		MOD_RADIO_ALLREG_20693(pi, LNA2G_CFG2, lna2g_nap, 0);
	}

	PHY_CHANLOG(pi, __FUNCTION__, TS_EXIT, 0);
}

static void
wlc_phy_set_reg_on_reset_acphy_20691(phy_info_t *pi)
{
	uint16 temp_reg;
	uint32 datapath = pi->u.pi_acphy->sromi->dot11b_opts;

	ASSERT(RADIOID_IS(pi->pubpi->radioid, BCM20691_ID));

	MOD_RADIO_REG_20691(pi, SPARE_CFG1, 0, spare_0, 0xfc00);
	MOD_RADIO_REG_20691(pi, SPARE_CFG2, 0, spare_1, 0x003f);

	if (ACPHY_ENABLE_FCBS_HWACI(pi))
		MOD_PHYREG(pi, FastChanSW_PLLVCOARBITR, arbitrdisable, 1);

	/* CRDOT11ACPHY-566: rx fix for dac rate mode 2 & 3 for >= rev1
	 * i.e. clear the top bit of the work_around_ctrl ACPHY register
	 */
	ACPHY_REG_LIST_START
		MOD_PHYREG_RAW_ENTRY(pi, ACPHY_work_around_ctrl(pi->pubpi->phy_rev), 0x8000, 0)

		MOD_PHYREG_ENTRY(pi, RxStatPwrOffset0, use_gainVar_for_rssi0, 1)
		MOD_PHYREG_ENTRY(pi, ForcePktAbort, dcblk_hpf_bw_en, 1)
		MOD_PHYREG_ENTRY(pi, HTAGCWaitCounters, HTAgcPktgainWait, 34)

		/* CRDOT11ACPHY-601: BPHY-20in20 Tapping via Datapath DC Filter */
		MOD_PHYREG_ENTRY(pi, RxSdFeConfig5, tiny_bphy20_ADC10_sel, 0)
		MOD_PHYREG_ENTRY(pi, RxFeCtrl1, swap_iq1, 1)
		MOD_PHYREG_ENTRY(pi, RxFeCtrl1, swap_iq2, 0)
		MOD_PHYREG_ENTRY(pi, bphyTest, dccomp, 0)

		/* maximum drive strength */
		MOD_RADIO_REG_20691_ENTRY(pi, TIA_CFG8, 0, tia_offset_comp_drive_strength, 1)

		/* DCC FSM Defaults */
		MOD_PHYREG_ENTRY(pi, BBConfig, dcc_wakeup_restart_en, 0)
		MOD_PHYREG_ENTRY(pi, BBConfig, dcc_wakeup_restart_delay, 10)
		MOD_PHYREG_ENTRY(pi, dcc_ctrl_restart_length_grp, dcc_ctrl_restart_length, 0xffff)

		/* Set DCC FSM to run and then stop - i.e  do not idle, */
		MOD_PHYREG_ENTRY(pi, rx_tia_dc_loop_0, en_lock, 1)

		/* Correct sign of loop gain */
		MOD_PHYREG_ENTRY(pi, rx_tia_dc_loop_0, dac_sign, 1)

		/* disable DVG2 to avoid bphy resampler saturation */
		MOD_PHYREG_ENTRY(pi, RxSdFeConfig5, tiny_bphy20_ADC10_sel, 0)

		/* digital-packet gain only */
		MOD_PHYREG_ENTRY(pi, singleShotAgcCtrl, singleShotPktGainElement, 96)

	ACPHY_REG_LIST_EXECUTE(pi);

	if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
		/* SWWLAN-42666  : 0x454b ==> SW4345-514 : 0x424b */
		ACPHY_REG_LIST_START
			WRITE_PHYREG_ENTRY(pi, crshighlowpowThresholdl, 0x424b)
			WRITE_PHYREG_ENTRY(pi, crshighlowpowThresholdu, 0x424b)
			WRITE_PHYREG_ENTRY(pi, crshighlowpowThresholdlSub1, 0x424b)
			WRITE_PHYREG_ENTRY(pi, crshighlowpowThresholduSub1, 0x424b)
		ACPHY_REG_LIST_EXECUTE(pi);
		/*
		BPHY-20in20:Tap main-DC-Filter,
		DVGA2=ON,dvga2maxgain limit disabled(i.e. gain=12dB)
		*/
		/*
		A0 = 001000 = 0x8 (11bhpf on,adc10 off )
		B0 = 000001 = 0x1 (adc10off,dvga tap)
		B0mod = 110111 = 0x37(targetvar, adc10+6dB, 11bhpf-off, adc10 on, dcnotchtap )
		*/
		MOD_PHYREG(pi, RxFeCtrl1, swap_iq1, datapath & 1);
		MOD_PHYREG(pi, RxFeCtrl1, swap_iq2, (datapath>>1) & 1);
		MOD_PHYREG(pi, RxSdFeConfig5, tiny_bphy20_ADC10_sel, (datapath>>2)&1);
		MOD_PHYREG(pi, bphyTest, dccomp, (datapath>>3)&1);
		if ((datapath >> 4) & 1) {
			temp_reg = READ_PHYREG(pi, work_around_ctrl) | (0x1 << 7);
			WRITE_PHYREG(pi, work_around_ctrl, temp_reg);
		}
		if ((datapath >> 5) & 1) {
			MOD_PHYREGC(pi, _BPHY_TargetVar_log2_pt8us, 0,
				bphy_targetVar_log2_pt8us, 479);
		}
		if ((datapath >> 6) & 7) {
			MOD_PHYREG(pi, DigiGainLimit0, minCckDigiGainShift, (datapath >> 6) & 7);
		}
		if ((datapath >> 9) & 0x3ff) {
			MOD_PHYREGC(pi, _BPHY_TargetVar_log2_pt8us, 0, bphy_targetVar_log2_pt8us,
				(datapath >> 9) & 0x3ff);
		}
	}

	ACPHY_REG_LIST_START
		MOD_PHYREG_ENTRY(pi, overideDigiGain1, cckdigigainEnCntValue, 119)

#if defined(ATE_BUILD) && defined(ATE_HTAGC_DISABLED)
		MOD_PHYREG_ENTRY(pi, dot11acConfig, HTAgcPktgainEn, 0)
#endif
	ACPHY_REG_LIST_EXECUTE(pi);

}

/*
gmult_rc (24:17), gmult(16:9), bq1_bw(8:6), rc_bw(5:3), bq0_bw(2:0)
LO: (15:0), HI (24:16)
mode_mask = bits[0:8] = 11b_20, 11n_20, 11ag_11ac_20, 11b_40, 11n_40, 11ag_11ac_40, 11b_80,
11n_11ag_11ac_80, samp_play
*/
static void
wlc_phy_set_analog_tx_lpf(phy_info_t *pi, uint16 mode_mask, int bq0_bw, int bq1_bw,
                       int rc_bw, int gmult, int gmult_rc, int core_num)
{
	uint8 ctr, core, max_modes = 9;
	uint16 addr_lo_offs[] = {0x142, 0x152, 0x162, 0x482};
	uint16 addr_hi_offs[] = {0x362, 0x372, 0x382, 0x552};
	uint16 rxlpfbw[] = {0, 0, 0, 1, 1, 1, 2, 2, 1};
	uint16 addr_lo_base, addr_hi_base, addr_lo, addr_hi;
	uint16 val_lo, val_hi;
	uint32 val;
	uint8 stall_val;
	/* This proc does not impact 4349, so return without doing anything */
	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_36(pi->pubpi->phy_rev)) {
		return;
	}
	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);
	/* core_num = -1 ==> all cores */
	FOREACH_ACTV_CORE(pi, pi->sh->phytxchain, core) {
		if ((core_num == -1) || (core_num == core)) {
			ASSERT(core < ARRAYSIZE(addr_lo_offs));
			addr_lo_base = addr_lo_offs[core];
			addr_hi_base = addr_hi_offs[core];
			for (ctr = 0; ctr < max_modes; ctr++) {
				if ((mode_mask >> ctr) & 1) {
					addr_lo = addr_lo_base + ctr;
					addr_hi = addr_hi_base + ctr;
					wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ,
					                         1, addr_lo, 16, &val_lo);
					wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ,
					                         1, addr_hi, 16, &val_hi);
					val = (val_hi << 16) | val_lo;
					if (ACMAJORREV_40(pi->pubpi->phy_rev) ||
					    ACMAJORREV_37(pi->pubpi->phy_rev)) {
						// Only bq1/bq2 bandwidth is controlled by direct
						// control.
						// The biquad control fields for Rx are in the
						// Rx2Tx table entries and are initialized here too.
						// For Tx bq2 control is in bit 5:2
						// For Rx bq2/bq1 control is in bit 11:8/7:6
						// Note: in this function bq2/bq1 are referred
						//       to as bq1/bq0
						if (bq1_bw >= 0) {
							val = (val & 0x1ffffc3) | (bq1_bw << 2);
						}
						val = (val & 0x1fff03f) | (rxlpfbw[ctr] << 6) |
						(rxlpfbw[ctr] << 8);
					} else {
						if (bq0_bw >= 0) {
							val = (val & 0x1fffff8) | (bq0_bw << 0);
							}
						if (rc_bw >= 0) {
							val = (val & 0x1ffffc7) | (rc_bw << 3);
						}
						if (bq1_bw >= 0) {
							val = (val & 0x1fffe3f) | (bq1_bw << 6);
						}
						if (gmult >= 0) {
							val = (val & 0x1fe01ff) | (gmult << 9);
						}
						if (gmult_rc >= 0) {
							val = (val & 0x001ffff) | (gmult_rc << 17);
						}
					}

					val_lo = val & 0xffff;
					val_hi = (val >> 16) & 0x1ff;
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ,
					                          1, addr_lo, 16, &val_lo);
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ,
					                          1, addr_hi, 16, &val_hi);
				}
			}
		}
	}
	ACPHY_ENABLE_STALL(pi, stall_val);
}

/*
dacbuf_fixed_cap[5], dacbuf_cap[4:0]
mode_mask = bits[0:8] = 11b_20, 11n_20, 11ag_11ac_20, 11b_40, 11n_40, 11ag_11ac_40, 11b_80,
11n_11ag_11ac_80, samp_play
*/
static void
wlc_phy_set_tx_afe_dacbuf_cap(phy_info_t *pi, uint16 mode_mask, int dacbuf_cap,
                           int dacbuf_fixed_cap, int core_num)
{
	uint8 ctr, core, max_modes = 9;
	uint16 core_base[] = {0x3f0, 0x60, 0xd0, 0x570};
	uint8 offset[] = {0xb, 0xb, 0xc, 0xc, 0xe, 0xe, 0xf, 0xf, 0xa};
	uint8 shift[] = {0, 6, 0, 6, 0, 6, 0, 6, 0};
	uint16 addr, read_val, val;
	uint8 stall_val;

	if (ACMAJORREV_36(pi->pubpi->phy_rev))
		return;

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);
	/* core_num = -1 ==> all cores */
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		if ((core_num == -1) || (core_num == core)) {
			for (ctr = 0; ctr < max_modes; ctr++) {
				if ((mode_mask >> ctr) & 1) {
					addr = core_base[core] + offset[ctr];
					wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ,
					                         1, addr, 16, &read_val);
					val = (read_val >> shift[ctr]) & 0x3f;

					if (dacbuf_cap >= 0) {
							val = (val & 0x20) | dacbuf_cap;
					}
					if (dacbuf_fixed_cap >= 0) {
						val = (val & 0x1f) |
						        (dacbuf_fixed_cap << 5);
					}

					if (shift[ctr] == 0) {
						val = (read_val & 0xfc0) | val;
					} else {
						val = (read_val & 0x3f) | (val << 6);
					}

					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ,
					                          1, addr, 16, &val);
				}
			}
		}
	}
	ACPHY_ENABLE_STALL(pi, stall_val);
}

/*
gmult_rc (24:17), rc_bw(16:14), gmult(13:6), bq1_bw(5:3), bq0_bw(2:0)
LO: (15:0), HI (24:16)
mode_mask = bits[0:2] = 20, 40, 80
*/
static void
wlc_phy_set_analog_rx_lpf(phy_info_t *pi, uint8 mode_mask, int bq0_bw, int bq1_bw,
                  int rc_bw, int gmult, int gmult_rc, int core_num)
{
	uint8 ctr, core, max_modes = 3;
	uint16 addr20_lo_offs[] = {0x140, 0x150, 0x160};
	uint16 addr20_hi_offs[] = {0x360, 0x370, 0x380};
	uint16 addr40_lo_offs[] = {0x141, 0x151, 0x161};
	uint16 addr40_hi_offs[] = {0x361, 0x371, 0x381};
	uint16 addr80_lo_offs[] = {0x441, 0x443, 0x445};
	uint16 addr80_hi_offs[] = {0x440, 0x442, 0x444};
	uint16 addr_lo, addr_hi;
	uint16 val_lo, val_hi;
	uint32 val;
	uint8 stall_val;
	/* This proc does not impact 4349, so return without doing anything */
	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_36(pi->pubpi->phy_rev) ||
		ACMAJORREV_37(pi->pubpi->phy_rev) || ACMAJORREV_40(pi->pubpi->phy_rev))
		return;

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);
	/* core_num = -1 ==> all cores */
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		if ((core_num == -1) || (core_num == core)) {
			for (ctr = 0; ctr < max_modes; ctr++) {
				if ((mode_mask >> ctr) & 1) {
					if (ctr == 0) {
						addr_lo = addr20_lo_offs[core];
						addr_hi = addr20_hi_offs[core];
					}
					else if (ctr == 1) {
						addr_lo = addr40_lo_offs[core];
						addr_hi = addr40_hi_offs[core];
					} else {
						addr_lo = addr80_lo_offs[core];
						addr_hi = addr80_hi_offs[core];
					}

					wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ,
					                         1, addr_lo, 16, &val_lo);
					wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ,
					                         1, addr_hi, 16, &val_hi);
					val = (val_hi << 16) | val_lo;

					if (bq0_bw >= 0) {
						val = (val & 0x1fffff8) | (bq0_bw << 0);
					}
					if (bq1_bw >= 0) {
						val = (val & 0x1ffffc7) | (bq1_bw << 3);
					}
					if (gmult >= 0) {
						val = (val & 0x1ffc03f) | (gmult << 6);
					}
					if (rc_bw >= 0) {
						val = (val & 0x1fe3fff) | (rc_bw << 14);
					}
					if (gmult_rc >= 0) {
						val = (val & 0x001ffff) | (gmult_rc << 17);
					}

					val_lo = val & 0xffff;
					val_hi = (val >> 16) & 0x1ff;
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
					                          addr_lo, 16, &val_lo);
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
					                          addr_hi, 16, &val_hi);
				}
			}
		}
	}
	ACPHY_ENABLE_STALL(pi, stall_val);
}

static void
acphy_load_txv_for_spexp(phy_info_t *pi)
{
	uint32 len = 243, offset = 1220;

	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFMUSERINDEX,
	                          len, offset, 32, acphy_txv_for_spexp);
}

static void
wlc_phy_cfg_energydrop_timeout(phy_info_t *pi)
{
	if (CHSPEC_IS5G(pi->radio_chanspec)) {
		/* Fine timing mod to have more overlap(~10dB) between low and high SNR regimes
		 * change to 0x8 to prevent the radar to trigger the fine timing
		 */
		MOD_PHYREG(pi, FSTRMetricTh, hiPwr_min_metric_th, 0x8);
		/* change it to 40000 for radar detection */
		WRITE_PHYREG(pi, energydroptimeoutLen, 0x9c40);
	} else {
		/* Fine timing mod to have more overlap(~10dB) between low and high SNR regimes */
		MOD_PHYREG(pi, FSTRMetricTh, hiPwr_min_metric_th, 0xf);
		/* In event of high power spurs/interference that causes crs-glitches,
		 * stay in WAIT_ENERGY_DROP for 1 clk20 instead of default 1 ms.
		 * This way, we get back to CARRIER_SEARCH quickly and will less likely to miss
		 * actual packets. PS: this is actually one settings for ACI
		 */
		WRITE_PHYREG(pi, energydroptimeoutLen, 0x2);
	}
}

static void
wlc_phy_set_reg_on_bw_change_acphy(phy_info_t *pi)
{
	uint8 core;
	const bool chspec_is20 = CHSPEC_IS20(pi->radio_chanspec);
	const bool chspec_is40 = CHSPEC_IS40(pi->radio_chanspec);

	if ((TINY_RADIO(pi) || ACMAJORREV_36(pi->pubpi->phy_rev) || IS_28NM_RADIO(pi))) {
		MOD_PHYREG(pi, TssiEnRate, StrobeRateOverride, 1);
	}

	MOD_PHYREG(pi, TssiEnRate, StrobeRate, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	0x1 : CHSPEC_IS40(pi->radio_chanspec) ? 0x2 : 0x3);
	MOD_PHYREG(pi, ClassifierCtrl, mac_bphy_band_sel, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	0x1 : CHSPEC_IS40(pi->radio_chanspec) ? 0x0  : 0x0);
	MOD_PHYREG(pi, RxControl, bphy_band_sel, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	0x1 : CHSPEC_IS40(pi->radio_chanspec) ? 0x0 : 0x0);
	MOD_PHYREG(pi, DcFiltAddress, dcCoef0, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	0x15 : CHSPEC_IS40(pi->radio_chanspec) ? 0xb : 0x5);
	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_3(pi->pubpi->phy_rev)) {

		if (ACMAJORREV_3(pi->pubpi->phy_rev))
			MOD_PHYREG(pi, CRSMiscellaneousParam, crsMfFlipCoef,
				CHSPEC_IS20(pi->radio_chanspec) ? 0x0 : 0x1);

		MOD_PHYREG(pi, iqest_input_control, dc_accum_wait_vht,
			chspec_is20 ? 0xc :
			chspec_is40 ? 0x1d : 0x3b);
		MOD_PHYREG(pi, iqest_input_control, dc_accum_wait_mm,
			chspec_is20 ? 0xb :
			chspec_is40 ? 0x1b : 0x37);
		if (ACMAJORREV_4(pi->pubpi->phy_rev))
			MOD_PHYREG(pi, IqestWaitTime, waitTime,
				chspec_is20 ? 0x14  : 0x28);
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, lesiCrsTypRxPowerPerCore, PowerLevelPerCore,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x15b : CHSPEC_IS40(pi->radio_chanspec) ? 0x228 : 0x184);
		MOD_PHYREG(pi, lesiCrsHighRxPowerPerCore, PowerLevelPerCore,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x76e : CHSPEC_IS40(pi->radio_chanspec) ? 0x9c1 : 0x551);
		MOD_PHYREG(pi, lesiCrsMinRxPowerPerCore, PowerLevelPerCore,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x5 : CHSPEC_IS40(pi->radio_chanspec) ? 0x18 : 0x2c);
		MOD_PHYREG(pi, lesiCrs1stDetThreshold_1, crsDetTh1_1Core,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x3b : CHSPEC_IS40(pi->radio_chanspec) ? 0x2a : 0x1d);
		MOD_PHYREG(pi, lesiCrs1stDetThreshold_1, crsDetTh1_2Core,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x2a : CHSPEC_IS40(pi->radio_chanspec) ? 0x1d : 0x15);
		MOD_PHYREG(pi, lesiCrs1stDetThreshold_2, crsDetTh1_3Core,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x22 : CHSPEC_IS40(pi->radio_chanspec) ? 0x18 : 0x11);
		MOD_PHYREG(pi, lesiCrs1stDetThreshold_2, crsDetTh1_4Core,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x1d : CHSPEC_IS40(pi->radio_chanspec) ? 0x15 : 0xe);
		MOD_PHYREG(pi, lesiCrs2ndDetThreshold_1, crsDetTh1_1Core,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x3b : CHSPEC_IS40(pi->radio_chanspec) ? 0x2a : 0x1d);
		MOD_PHYREG(pi, lesiCrs2ndDetThreshold_1, crsDetTh1_2Core,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x2a : CHSPEC_IS40(pi->radio_chanspec) ? 0x1d : 0x15);
		MOD_PHYREG(pi, lesiCrs2ndDetThreshold_2, crsDetTh1_3Core,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x22 : CHSPEC_IS40(pi->radio_chanspec) ? 0x18 : 0x11);
		MOD_PHYREG(pi, lesiCrs2ndDetThreshold_2, crsDetTh1_4Core,
			CHSPEC_IS20(pi->radio_chanspec) ?
			0x1d : CHSPEC_IS40(pi->radio_chanspec) ? 0x15 : 0xe);
		MOD_PHYREG(pi, lesiFstrControl3, cCrsFftInpAdj, CHSPEC_IS20(pi->radio_chanspec) ?
			0x0 : CHSPEC_IS40(pi->radio_chanspec) ? 0x1 : 0x3);
		MOD_PHYREG(pi, lesiFstrControl3, lCrsFftInpAdj, CHSPEC_IS20(pi->radio_chanspec) ?
			9: CHSPEC_IS40(pi->radio_chanspec) ? 19 : 40);
		MOD_PHYREG(pi, lesiFstrClassifierEqualizationFactor0_0, subBand0Factor,
			CHSPEC_IS20(pi->radio_chanspec) ?
			32: CHSPEC_IS40(pi->radio_chanspec) ? 32 : 48);
		MOD_PHYREG(pi, lesiFstrClassifierEqualizationFactor1_0, subBand3Factor,
			CHSPEC_IS20(pi->radio_chanspec) ?
			32: CHSPEC_IS40(pi->radio_chanspec) ? 32 : 48);
		MOD_PHYREG(pi, lesiFstrClassifierEqualizationFactor0_1, subBand0Factor,
			CHSPEC_IS20(pi->radio_chanspec) ?
			32: CHSPEC_IS40(pi->radio_chanspec) ? 32 : 48);
		MOD_PHYREG(pi, lesiFstrClassifierEqualizationFactor1_1, subBand3Factor,
			CHSPEC_IS20(pi->radio_chanspec) ?
			32: CHSPEC_IS40(pi->radio_chanspec) ? 32 : 48);
		MOD_PHYREG(pi, lesiFstrClassifierEqualizationFactor0_2, subBand0Factor,
			CHSPEC_IS20(pi->radio_chanspec) ?
			32: CHSPEC_IS40(pi->radio_chanspec) ? 32 : 48);
		MOD_PHYREG(pi, lesiFstrClassifierEqualizationFactor1_2, subBand3Factor,
			CHSPEC_IS20(pi->radio_chanspec) ?
			32: CHSPEC_IS40(pi->radio_chanspec) ? 32 : 48);
		MOD_PHYREG(pi, lesiFstrClassifierEqualizationFactor0_3, subBand0Factor,
			CHSPEC_IS20(pi->radio_chanspec) ?
			32: CHSPEC_IS40(pi->radio_chanspec) ? 32 : 48);
		MOD_PHYREG(pi, lesiFstrClassifierEqualizationFactor1_3, subBand3Factor,
			CHSPEC_IS20(pi->radio_chanspec) ?
			32: CHSPEC_IS40(pi->radio_chanspec) ? 32 : 48);
		MOD_PHYREG(pi, LesiFstrFdNoisePower0, noi_pow, CHSPEC_IS20(pi->radio_chanspec) ?
			175: CHSPEC_IS40(pi->radio_chanspec) ? 175 : 100);
		MOD_PHYREG(pi, LesiFstrFdNoisePower1, noi_pow, CHSPEC_IS20(pi->radio_chanspec) ?
			175: CHSPEC_IS40(pi->radio_chanspec) ? 175 : 100);
		MOD_PHYREG(pi, LesiFstrFdNoisePower2, noi_pow, CHSPEC_IS20(pi->radio_chanspec) ?
			175: CHSPEC_IS40(pi->radio_chanspec) ? 175 : 100);
		MOD_PHYREG(pi, LesiFstrFdNoisePower3, noi_pow, CHSPEC_IS20(pi->radio_chanspec) ?
			175: CHSPEC_IS40(pi->radio_chanspec) ? 175 : 100);
		MOD_PHYREG(pi, iqest_input_control, dc_accum_wait_vht,
		           CHSPEC_IS20(pi->radio_chanspec) ? 12 :
		           (CHSPEC_IS40(pi->radio_chanspec) ? 29 : 59));
		MOD_PHYREG(pi, iqest_input_control, dc_accum_wait_mm,
		           CHSPEC_IS20(pi->radio_chanspec) ? 11 :
		           (CHSPEC_IS40(pi->radio_chanspec) ? 27 : 55));
		MOD_PHYREG(pi, IqestWaitTime, waitTime,
		           CHSPEC_IS20(pi->radio_chanspec) ? 20 :
		           (CHSPEC_IS40(pi->radio_chanspec) ? 40 : 40));

		WRITE_PHYREG(pi, ACIBrwdfCoef0,
			CHSPEC_IS20(pi->radio_chanspec) ? 0xdc31 :
			CHSPEC_IS40(pi->radio_chanspec) ? 0xc32b : 0xba28);
		WRITE_PHYREG(pi, ACIBrwdfCoef1,
			CHSPEC_IS20(pi->radio_chanspec) ? 0x0000 :
			CHSPEC_IS40(pi->radio_chanspec) ? 0x0000 : 0x00f7);
		WRITE_PHYREG(pi, ACIBrwdfCoef2,
			CHSPEC_IS20(pi->radio_chanspec) ? 0x008d :
			CHSPEC_IS40(pi->radio_chanspec) ? 0xee80 : 0xe179);
	}

	if (!TINY_RADIO(pi) && !IS_28NM_RADIO(pi)) {
		MOD_PHYREG(pi, RxFilt40Num00, RxFilt40Num00, chspec_is20 ?
			0x146 : chspec_is40 ? 0x181 : 0x17a);
		MOD_PHYREG(pi, RxFilt40Num01, RxFilt40Num01, chspec_is20 ?
			0x88 : chspec_is40 ? 0x5a : 0x9e);
		MOD_PHYREG(pi, RxFilt40Num02, RxFilt40Num02, chspec_is20 ?
			0x146 : chspec_is40 ? 0x181 : 0x17a);
		MOD_PHYREG(pi, RxFilt40Den00, RxFilt40Den00, chspec_is20 ?
			0x76e : chspec_is40 ? 0x793 : 0x7ca);
		MOD_PHYREG(pi, RxFilt40Den01, RxFilt40Den01, chspec_is20 ?
			0x1a8 : chspec_is40 ? 0x1b7 : 0x1b2);
		MOD_PHYREG(pi, RxFilt40Num10, RxFilt40Num10, chspec_is20 ?
			0xa3 : chspec_is40 ? 0xc1 : 0xbd);
		MOD_PHYREG(pi, RxFilt40Num11, RxFilt40Num11, chspec_is20 ?
			0xf4 : chspec_is40 ? 0x102 : 0x114);
		MOD_PHYREG(pi, RxFilt40Num12, RxFilt40Num12, chspec_is20 ?
			0xa3 : chspec_is40 ? 0xc1 : 0xbd);
		MOD_PHYREG(pi, RxFilt40Den10, RxFilt40Den10, chspec_is20 ?
			0x684 : chspec_is40 ? 0x6c0 : 0x6d6);
		MOD_PHYREG(pi, RxFilt40Den11, RxFilt40Den11, chspec_is20 ?
			0xad : chspec_is40 ? 0xa9 : 0xa2);
		MOD_PHYREG(pi, RxStrnFilt40Num00, RxStrnFilt40Num00,
			chspec_is20 ? 0xe5 : chspec_is40 ? 0x162 : 0x16c);
		MOD_PHYREG(pi, RxStrnFilt40Num01, RxStrnFilt40Num01,
			chspec_is20 ? 0x68 : chspec_is40 ? 0x42 : 0x6f);
		MOD_PHYREG(pi, RxStrnFilt40Num02, RxStrnFilt40Num02,
			chspec_is20 ? 0xe5 : chspec_is40 ? 0x162 : 0x16c);
		MOD_PHYREG(pi, RxStrnFilt40Den00, RxStrnFilt40Den00,
			chspec_is20 ? 0x6be : chspec_is40 ? 0x75c : 0x793);
		MOD_PHYREG(pi, RxStrnFilt40Den01, RxStrnFilt40Den01,
			chspec_is20 ? 0x19e : chspec_is40 ? 0x1b3 : 0x1b2);
		MOD_PHYREG(pi, RxStrnFilt40Num10, RxStrnFilt40Num10,
			chspec_is20 ? 0x73 : chspec_is40 ? 0xb1 : 0xb6);
		MOD_PHYREG(pi, RxStrnFilt40Num11, RxStrnFilt40Num11,
			chspec_is20 ? 0xb2 : chspec_is40 ? 0xed : 0xff);
		MOD_PHYREG(pi, RxStrnFilt40Num12, RxStrnFilt40Num12,
			chspec_is20 ? 0x73 : chspec_is40 ? 0xb1 : 0xb6);
		MOD_PHYREG(pi, RxStrnFilt40Den10, RxStrnFilt40Den10,
			chspec_is20 ? 0x5fe : chspec_is40 ? 0x692 : 0x6b4);
		MOD_PHYREG(pi, RxStrnFilt40Den11, RxStrnFilt40Den11,
			chspec_is20 ? 0xcc : chspec_is40 ? 0xaf : 0xa8);
	}
	MOD_PHYREG(pi, nvcfg3, noisevar_rxevm_lim_qdb, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	0x97 : CHSPEC_IS40(pi->radio_chanspec) ? 0x8b : 0x97);
	if (CHSPEC_IS5G(pi->radio_chanspec)) {
	    MOD_PHYREG(pi, RadarBlankCtrl, radarBlankingInterval,
	    CHSPEC_BW_LE20(pi->radio_chanspec) ? 0x19 :
	    CHSPEC_IS40(pi->radio_chanspec) ? 0x32 : 0x32);
	    MOD_PHYREG(pi, RadarT3BelowMin, Count, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	    0x14 : CHSPEC_IS40(pi->radio_chanspec) ? 0x28 : 0x28);
	    MOD_PHYREG(pi, RadarT3Timeout, Timeout, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	    0xc8 : CHSPEC_IS40(pi->radio_chanspec) ? 0x190 : 0x190);
	    MOD_PHYREG(pi, RadarResetBlankingDelay, Count, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	    0x19 : CHSPEC_IS40(pi->radio_chanspec) ? 0x32 : 0x32);
	}
	MOD_PHYREG(pi, ClassifierCtrl6, logACDelta2, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	0x13 : CHSPEC_IS40(pi->radio_chanspec) ? 0x13 : 0x9);
	MOD_PHYREG(pi, ClassifierLogAC1, logACDelta1, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	0x13 : CHSPEC_IS40(pi->radio_chanspec) ? 0x13 : 0x9);
	if (CHSPEC_IS2G(pi->radio_chanspec)) {
	  if (ACMAJORREV_1(pi->pubpi->phy_rev) || ACMAJORREV_3(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, bphyPreDetectThreshold6, ac_det_1us_aci_th,
			chspec_is20 ? 0x80 : chspec_is40 ? 0x200 : 0x200);
	  }
	}
	FOREACH_CORE(pi, core) {
	  MOD_PHYREGC(pi, Adcclip, core, adc_clip_cnt_th, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	  0xa : CHSPEC_IS40(pi->radio_chanspec) ? 0x14 : 0x14);
	  MOD_PHYREGC(pi, FastAgcClipCntTh, core, fastAgcNbClipCntTh,
	  CHSPEC_BW_LE20(pi->radio_chanspec) ?
	  0x17 : CHSPEC_IS40(pi->radio_chanspec) ? 0x2a : 0x54);
	  MOD_PHYREGC(pi, FastAgcClipCntTh, core, fastAgcW1ClipCntTh,
	  CHSPEC_BW_LE20(pi->radio_chanspec) ?
	  0xe : CHSPEC_IS40(pi->radio_chanspec) ? 0x16 : 0x2c);
	}
	if (!ACMAJORREV_0(pi->pubpi->phy_rev) &&
	    !(ACMAJORREV_2(pi->pubpi->phy_rev) && ACMINORREV_0(pi))) {
	  MOD_PHYREG(pi, CRSMiscellaneousParam, crsMfFlipCoef, CHSPEC_BW_LE20(pi->radio_chanspec) ?
	    0x0 : 0x1);
	}
	/* FIX ME : Currently setting only for 4350, Other phy revs should
	 * check with RTL folks and set accordingly
	 */
	if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, FSTRCtrl, fineStrSgiVldCntVal, chspec_is20 ?
			0x9 : 0xa);
		MOD_PHYREG(pi, FSTRCtrl, fineStrVldCntVal, chspec_is20 ?
			0x9 : 0xa);
	}
}

/* Load pdet related Rfseq on reset */
static void
wlc_phy_set_pdet_on_reset_acphy(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;
	uint8 core, pdet_range_id, subband_idx, ant, core_freq_segment_map;
	uint16 offset, tmp_val, val_av, val_vmid;
	uint8 bands[NUM_CHANS_IN_CHAN_BONDING];
	uint8 av[4] = {0, 0, 0, 0};
	uint8 vmid[4] = {0, 0, 0, 0};
	uint8 stall_val;

	bool flag2rangeon =
		((CHSPEC_IS2G(pi->radio_chanspec) && phy_tpc_get_tworangetssi2g(pi->tpci)) ||
		(CHSPEC_IS5G(pi->radio_chanspec) && phy_tpc_get_tworangetssi5g(pi->tpci))) &&
		PHY_IPA(pi);

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		pdet_range_id = phy_tpc_get_2g_pdrange_id(pi->tpci);
	} else {
		pdet_range_id = phy_tpc_get_5g_pdrange_id(pi->tpci);
	}

	FOREACH_CORE(pi, core) {
		/* core_freq_segment_map is only required for 80P80 mode
		 For other modes, it is ignored
		*/
		if (PHY_AS_80P80(pi, pi->radio_chanspec)) {
			phy_ac_chanmgr_get_chan_freq_range_80p80(pi, 0, bands);
			if (ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
				subband_idx = (core <= 1) ? bands[0] : bands[1];
			} else {
				subband_idx = bands[0];
				ASSERT(0);
			}
		} else {
			core_freq_segment_map = pi->u.pi_acphy->core_freq_mapping[core];
			subband_idx = phy_ac_chanmgr_get_chan_freq_range(pi, 0,
				core_freq_segment_map);
		}
		ant = phy_get_rsdbbrd_corenum(pi, core);
		if (BF3_AVVMID_FROM_NVRAM(pi->u.pi_acphy)) {
			av[core] = pi_ac->sromi->avvmid_set_from_nvram[ant][subband_idx][0];
			vmid[core] = pi_ac->sromi->avvmid_set_from_nvram[ant][subband_idx][1];
		} else {
			if (ACMAJORREV_0(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
				/* 4360 and 43602 */
				av[core] = avvmid_set[pdet_range_id][subband_idx][ant];
				vmid[core] = avvmid_set[pdet_range_id][subband_idx][ant+3];
			} else if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
				if (core == 0) {
					av[core] = avvmid_set1[pdet_range_id][subband_idx][ant];
					vmid[core] = avvmid_set1[pdet_range_id][subband_idx][ant+1];
				}
			} else if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
				av[core] = avvmid_set2[pdet_range_id][subband_idx][ant];
				vmid[core] = avvmid_set2[pdet_range_id][subband_idx][ant+2];
			} else if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
				if (core == 0) {
					av[core] = avvmid_set3[pdet_range_id][subband_idx][ant];
					vmid[core] =
					        avvmid_set3[pdet_range_id][subband_idx][ant+1];
				}
			} else if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
				av[core] = avvmid_set4[pdet_range_id][subband_idx][ant];
				vmid[core] =
				        avvmid_set4[pdet_range_id][subband_idx][ant+2];
			} else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev)) {
				av[core] = avvmid_set32[pdet_range_id][subband_idx][ant];
				vmid[core] = avvmid_set32[pdet_range_id][subband_idx][ant+4];
			} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
				uint8 *p_avvmid_set =
					get_avvmid_set_36(pi, pdet_range_id, subband_idx);
				av[core] = p_avvmid_set[ant];
				vmid[core] = p_avvmid_set[ant+3];
			} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
				av[core] = avvmid_set32[pdet_range_id][subband_idx][ant];
				vmid[core] = avvmid_set32[pdet_range_id][subband_idx][ant+4];
			} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
				uint8 *p_avvmid_set =
					get_avvmid_set_40(pi, pdet_range_id, subband_idx);
				av[core] = p_avvmid_set[ant];
				vmid[core] = p_avvmid_set[ant+3];
			}
		}
	}
	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);

	FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
		if ((ACMAJORREV_1(pi->pubpi->phy_rev) && (core == 0)) ||
		    !(ACMAJORREV_1(pi->pubpi->phy_rev))) {
			if (core == 3)
				offset = 0x560 + 0xd;
			else
				offset = 0x3c0 + 0xd + core*0x10;
			wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ,
			                         1, offset, 16, &tmp_val);
			val_av = (tmp_val & 0x1ff8) | (av[core]&0x7);
			val_vmid = (val_av & 0x7) | ((vmid[core]&0x3ff)<<3);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ,
			                          1, offset, 16, &val_vmid);

			if (((ACMAJORREV_1(pi->pubpi->phy_rev) ||
				ACMAJORREV_2(pi->pubpi->phy_rev) ||
				ACMAJORREV_4(pi->pubpi->phy_rev)) &&
				BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) ||
				flag2rangeon) {
				offset = 0x3c0 + 0xe + core*0x10;
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ,
				                          1, offset, 16, &val_vmid);
			}
		}
	}
	ACPHY_ENABLE_STALL(pi, stall_val);
}

static void
wlc_phy_set_tx_iir_coeffs(phy_info_t *pi, bool cck, uint8 filter_type)
{
	if (cck == FALSE) {
		if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			/* Default filters */
			if (filter_type == 0) {
				if (CHSPEC_IS2G(pi->radio_chanspec) &&
						!ROUTER_4349(pi) && !PHY_IPA(pi)) {
					ACPHY_REG_LIST_START
					/* Default Chebyshev ~10.5MHz cutoff */
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st0a1, 0x0056)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st0a2, 0x02fb)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st0n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st1a1, 0x0f3d)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st1a2, 0x0169)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st1n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st2a1, 0x0e23)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st2a2, 0x0068)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st2n, 0x0002)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20finescale, 0x00E9)
					ACPHY_REG_LIST_EXECUTE(pi);
				} else {
					ACPHY_REG_LIST_START
					/* Default Chebyshev ~10.5MHz cutoff */
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st0a1, 0x0056)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st0a2, 0x02fb)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st0n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st1a1, 0x0f3d)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st1a2, 0x0169)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st1n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st2a1, 0x0e23)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st2a2, 0x0068)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st2n, 0x0002)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20finescale, 0x00a6)
					ACPHY_REG_LIST_EXECUTE(pi);
				}
			} else if (filter_type == 1) {
				ACPHY_REG_LIST_START
					 /* Chebyshev ~8.8MHz cutoff (FCC -26dBr BW) */
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st0a1, 0x0e73)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st0a2, 0x033d)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st0n, 0x0002)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st1a1, 0x0d5f)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st1a2, 0x0205)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st1n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st2a1, 0x0c39)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st2a2, 0x011e)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20st2n, 0x0002)
					WRITE_PHYREG_ENTRY(pi, txfilt20in20finescale, 0x001a)
				ACPHY_REG_LIST_EXECUTE(pi);
			}
		}
	} else {
		/* Tx filters in PHY REV 3, PHY REV 6 and later operate at 1/2 the sampling
		 * rate of previous revs
		 */
		if ((ACMAJORREV_0(pi->pubpi->phy_rev) && (ACMINORREV_0(pi) || ACMINORREV_1(pi))) ||
		    (ACMAJORREV_1(pi->pubpi->phy_rev) && (ACMINORREV_0(pi) || ACMINORREV_1(pi))) ||
		    (ACMAJORREV_3(pi->pubpi->phy_rev)) || (ACMAJORREV_4(pi->pubpi->phy_rev))) {
	    if (filter_type == 0) {
			ACPHY_REG_LIST_START
				/* Default filter */
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a1, 0x0a94)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a2, 0x0373)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0n, 0x0005)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a1, 0x0a93)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a2, 0x0298)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1n, 0x0004)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a1, 0x0a52)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a2, 0x021d)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2n, 0x0004)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20finescale, 0x0080)
			ACPHY_REG_LIST_EXECUTE(pi);
	    } else if (filter_type == 1) {
			ACPHY_REG_LIST_START
				/* Gaussian  shaping filter */
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a1, 0x0b54)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a2, 0x0290)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0n, 0x0004)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a1, 0x0a40)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a2, 0x0290)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1n, 0x0005)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a1, 0x0a06)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a2, 0x0240)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2n, 0x0005)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20finescale, 0x0080)
			ACPHY_REG_LIST_EXECUTE(pi);
		} else if (filter_type == 4) {
			if (pi->u.pi_acphy->dac_mode == 1) {
				ACPHY_REG_LIST_START
					/* Gaussian shaping filter for TINY_A0, dac_rate_mode 1 */
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a1, -80)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a2, 369)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0n, 3)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a1, -757)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a2, 369)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1n, 3)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a1, -1007)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a2, 256)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2n, 3)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20finescale, 120)
				ACPHY_REG_LIST_EXECUTE(pi);
			} else if (pi->u.pi_acphy->dac_mode == 2) {
				ACPHY_REG_LIST_START
					/* Gaussian shaping filter for TINY_A0, dac_rate_mode 2 */
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80st0a1, -1852)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80st0a2, 892)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80st0n, 7)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80st1a1, -1890)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80st1a2, 892)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80st1n, 7)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80st2a1, -1877)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80st2a2, 860)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80st2n, 7)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in80finescale, 65)
				ACPHY_REG_LIST_EXECUTE(pi);
			} else {
				ACPHY_REG_LIST_START
					/* Gaussian shaping filter for TINY_A0, dac_rate_mode 3 */
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40st0a1, -1714)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40st0a2, 829)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40st0n, 6)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40st1a1, -1796)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40st1a2, 829)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40st1n, 6)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40st2a1, -1790)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40st2a2, 784)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40st2n, 6)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in40finescale, 54)
				ACPHY_REG_LIST_EXECUTE(pi);
			}
	    } else if (filter_type == 5) {
			ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a1, -48)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a2, 1)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0n, 3)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a1, -75)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a2, 23)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1n, 3)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a1, -504)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a2, 64)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2n, 3)
				WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20finescale, 175)
			ACPHY_REG_LIST_EXECUTE(pi);
		}
	} else if ((ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) ||
		ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev) ||
		ACMAJORREV_36(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
			if (filter_type == 0) {
				ACPHY_REG_LIST_START
					/* Default filter */
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a1, 0x0f6b)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a2, 0x0339)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a1, 0x0e29)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a2, 0x01e5)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1n, 0x0002)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a1, 0x0cb2)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a2, 0x00f0)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20finescale, 0x00b3)
				ACPHY_REG_LIST_EXECUTE(pi);
			} else if (filter_type == 1) {
				ACPHY_REG_LIST_START
					/* Gaussian shaping filter (-0.5 dB Tx Power) */
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a1, 0x0edb)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a2, 0x01cb)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a1, 0x0d1d)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a2, 0x0192)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a1, 0x0c33)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a2, 0x00f3)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20finescale, 0x0076)
				ACPHY_REG_LIST_EXECUTE(pi);
			} else if (filter_type == 2) {
				ACPHY_REG_LIST_START
					/* Tweaked Gaussian for 4335 iPA CCk margin */
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a1, 0x0edb)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a2, 0x01ab)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a1, 0x0d1d)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a2, 0x0172)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a1, 0x0c77)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a2, 0x00a9)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20finescale, 0x0082)
				ACPHY_REG_LIST_EXECUTE(pi);
			} else if (filter_type == 4) {
				/* Tweaked Gaussian for 43012 CCk margin */
				ACPHY_REG_LIST_START
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a1, 0x0edb)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a2, 0x01ab)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a1, 0x0d1d)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a2, 0x0172)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a1, 0x0c77)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a2, 0x00a9)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20finescale, 0x0082)
				ACPHY_REG_LIST_EXECUTE(pi);
			}
		} else {
			if (filter_type == 0) {
				ACPHY_REG_LIST_START
					/* Default filter */
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a1, 0x0f6b)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0a2, 0x0339)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st0n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a1, 0x0e29)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1a2, 0x01e5)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st1n, 0x0002)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a1, 0x0cb2)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2a2, 0x00f0)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20st2n, 0x0003)
					WRITE_PHYREG_ENTRY(pi, txfiltbphy20in20finescale, 0x00b3)
				ACPHY_REG_LIST_EXECUTE(pi);
			} else if (filter_type == 1) {
				/* TBD */
			}
		}
	}
}

static void
wlc_phy_set_sdadc_pd_val_per_core_acphy(phy_info_t *pi)
{
	if (ACMAJORREV_2(pi->pubpi->phy_rev) && !(PHY_IPA(pi))) {
		bool suspend;
		uint8 stall_val, orig_rxfectrl1;
		uint16 rx_sd_adc_pd_val[2] = {0x20, 0x20};
		uint16 rx_sd_adc_pd_cfg[2] = {0x00, 0x00};
		uint8 core;

		int fc;
		const void *chan_info = NULL;
		uint8 ch = CHSPEC_CHANNEL(pi->radio_chanspec);
		phy_ac_radio_data_t *rd = phy_ac_radio_get_data(pi->u.pi_acphy->radioi);

		suspend = !(R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC);
		if (!suspend)
			wlapi_suspend_mac_and_wait(pi->sh->physhim);
		/* Disable stalls and hold FIFOs in reset */
		stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
		orig_rxfectrl1 = READ_PHYREGFLD(pi, RxFeCtrl1, soft_sdfeFifoReset);
		if (stall_val == 0)
			ACPHY_DISABLE_STALL(pi);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);

		if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID)) {
			fc = wlc_phy_chan2freq_acphy(pi, ch, &chan_info);
		} else {
			const chan_info_radio20691_t *chan_info_20691;
			fc = wlc_phy_chan2freq_20691(pi, ch, &chan_info_20691);
		}

		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {

			if (CHSPEC_IS2G(pi->radio_chanspec)) {
				if (rd->srom_txnospurmod2g
						== 0) {
					rx_sd_adc_pd_val[core] = 0x3d;
					rx_sd_adc_pd_cfg[core] = 0x1a53;
				} else {
					if ((fc == 2412) || (fc == 2467)) {
						rx_sd_adc_pd_val[core] = 0x3d;
						rx_sd_adc_pd_cfg[core] = 0x1b53;
					} else {
						rx_sd_adc_pd_val[core] = 0x3d;
						rx_sd_adc_pd_cfg[core] = 0x1c53;
					}
				}
			} else {
				if (CHSPEC_IS20(pi->radio_chanspec)) {
					if (((fc >= 5180) && (fc <= 5320)) ||
					((fc >= 5745) && (fc <= 5825))) {
						rx_sd_adc_pd_val[core] = 0x3d;
						rx_sd_adc_pd_cfg[core] = 0x1f12;
					} else {
						rx_sd_adc_pd_val[core] = 0x3d;
						rx_sd_adc_pd_cfg[core] = 0x1B53;
					}
				} else if (CHSPEC_IS40(pi->radio_chanspec)) {
					if (fc == 5190) {
						rx_sd_adc_pd_val[core] = 0x3f;
						rx_sd_adc_pd_cfg[core] = 0x1818;
					} else {
						rx_sd_adc_pd_val[core] = 0x3d;
						rx_sd_adc_pd_cfg[core] = 0x1f12;
					}
				} else {
					rx_sd_adc_pd_val[core] = 0x3f;
					rx_sd_adc_pd_cfg[core] = 0x1818;
				}
			}

		}

		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3c6, 16,
			&rx_sd_adc_pd_val[0]);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3c9, 16,
			&rx_sd_adc_pd_cfg[0]);

		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3d6, 16,
			&rx_sd_adc_pd_val[1]);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x3d9, 16,
			&rx_sd_adc_pd_cfg[1]);

		/* Restore FIFO reset and Stalls */
		if (!suspend)
			wlapi_enable_mac(pi->sh->physhim);
		ACPHY_ENABLE_STALL(pi, stall_val);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, orig_rxfectrl1);

	}

}

static void
wlc_phy_write_regtbl_fc3_sub0(phy_info_t *pi)
{
	uint8 fectrl_mch5_c0_p200_p400[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		2, 4, 3, 11, 2, 4, 3, 11, 0x02, 0x24, 0x03, 0x2d, 0x02, 0x24, 0x03, 0x2d};
	uint8 fectrl_mch5_c1_p200_p400[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		2, 1, 6, 14, 2, 1, 6, 14, 0x02, 0x21, 0x06, 0x2d, 0x02, 0x21, 0x06, 0x2d};
	uint8 fectrl_mch5_c2_p200_p400[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		4, 1, 6, 14, 4, 1, 6, 14, 0x04, 0x21, 0x06, 0x2b, 0x04, 0x21, 0x06, 0x2b};

	si_corereg(pi->sh->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol),
		0xffffff, CCTRL4360_DISCRETE_FEMCTRL_MODE);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32,	0, 8,
		fectrl_mch5_c0_p200_p400);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32, 32, 8,
		fectrl_mch5_c1_p200_p400);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32, 64, 8,
		fectrl_mch5_c2_p200_p400);

}

static void
wlc_phy_write_regtbl_fc3_sub1(phy_info_t *pi)
{
	uint8 fectrl_mch5_c0[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		8, 4, 3, 8, 8, 4, 3, 8, 0x08, 0x24, 0x03, 0x25, 0x08, 0x24, 0x03, 0x25};
	uint8 fectrl_mch5_c1[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		8, 1, 6, 8, 8, 1, 6, 8, 0x08, 0x21, 0x06, 0x25, 0x08, 0x21, 0x06, 0x25};
	uint8 fectrl_mch5_c2[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		8, 1, 6, 8, 8, 1, 6, 8, 0x08, 0x21, 0x06, 0x23, 0x08, 0x21, 0x06, 0x23};

	/* P500+ */
	si_corereg(pi->sh->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol),
		0xffffff, CCTRL4360_DISCRETE_FEMCTRL_MODE);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32,	0, 8,
		fectrl_mch5_c0);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32, 32, 8,
		fectrl_mch5_c1);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32, 64, 8,
		fectrl_mch5_c2);
}

static void
wlc_phy_write_regtbl_fc3_sub2(phy_info_t *pi)
{
	uint8 fectrl_j28[] =  {2, 4, 3, 2, 2, 4, 3, 2, 0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23,
		0x25, 2, 4, 3, 2, 2, 4, 3, 2, 0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25};

	/* J28 */
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32,	0, 8,
		fectrl_j28);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32, 32, 8,
		fectrl_j28);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32, 64, 8,
		fectrl_j28);
}

static void
wlc_phy_write_regtbl_fc3_sub3(phy_info_t *pi)
{
	uint8 fectrl3_sub3_c0[] = {2, 4, 3, 2, 2, 4, 3, 2, 0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23,
		0x25, 2, 4, 3, 2, 2, 4, 3, 2, 0x22, 0x24, 0x23, 0x25, 0x22, 0x24, 0x23, 0x25};
	uint8 fectrl3_sub3_c1[] = {2, 1, 6, 2, 2, 1, 6, 2, 0x22, 0x21, 0x26, 0x25, 0x22, 0x21, 0x26,
		0x25, 2, 1, 6, 2, 2, 1, 6, 2, 0x22, 0x21, 0x26, 0x25, 0x22, 0x21, 0x26, 0x25};
	uint8 fectrl3_sub3_c2[] = {4, 1, 6, 4, 4, 1, 6, 4, 0x24, 0x21, 0x26, 0x23, 0x24, 0x21, 0x26,
		0x23, 4, 1, 6, 4, 4, 1, 6, 4, 0x24, 0x21, 0x26, 0x23, 0x24, 0x21, 0x26, 0x23};

	/* MCH2 */
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32,	0, 8,
		fectrl3_sub3_c0);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32, 32, 8,
		fectrl3_sub3_c1);
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 32, 64, 8,
		fectrl3_sub3_c2);
}

static INLINE void
wlc_phy_write_regtbl_fc3(phy_info_t *pi, phy_info_acphy_t *pi_ac)
{
	switch (BF3_FEMCTRL_SUB(pi_ac)) {
		case 0:
			wlc_phy_write_regtbl_fc3_sub0(pi);
		break;
		case 1:
			wlc_phy_write_regtbl_fc3_sub1(pi);
		break;
		case 2:
			wlc_phy_write_regtbl_fc3_sub2(pi);
		break;
		case 3:
			wlc_phy_write_regtbl_fc3_sub3(pi);
		break;
	}
}

static void
wlc_phy_write_regtbl_fc4_sub0(phy_info_t *pi)
{
	uint16 fectrl_zeroval[] = {0};
	uint16 kk, fem_idx = 0;
	sparse_array_entry_t fectrl_fcbga_epa_elna[] =
		{{2, 264}, {3, 8}, {9, 32}, {18, 5}, {19, 4}, {25, 128}, {130, 64}, {192, 64}};

	for (kk = 0; kk < 256; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_fcbga_epa_elna) &&
			kk == fectrl_fcbga_epa_elna[fem_idx].idx) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				&(fectrl_fcbga_epa_elna[fem_idx].val));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static void
wlc_phy_write_regtbl_fc4_sub1(phy_info_t *pi)
{
	uint16 fectrl_zeroval[] = {0};
	uint16 kk, fem_idx = 0;
	sparse_array_entry_t fectrl_wlbga_epa_elna[] =
	{{2, 3}, {3, 1}, {9, 256}, {18, 20}, {19, 16}, {25, 8}, {66, 3}, {67, 1},
	{73, 256}, {82, 20}, {83, 16}, {89, 8}, {128, 3}, {129, 1}, {130, 3}, {131, 1},
	{132, 1}, {133, 1}, {134, 1}, {135, 1}, {136, 3}, {137, 1}, {138, 3}, {139, 1},
	{140, 1}, {141, 1}, {142, 1}, {143, 1}, {160, 3}, {161, 1}, {162, 3}, {163, 1},
	{164, 1}, {165, 1}, {166, 1}, {167, 1}, {168, 3}, {169, 1}, {170, 3}, {171, 1},
	{172, 1}, {173, 1}, {174, 1}, {175, 1}, {192, 128}, {193, 128}, {196, 128}, {197, 128},
	{200, 128}, {201, 128}, {204, 128}, {205, 128}, {224, 128}, {225, 128}, {228, 128},
	{229, 128}, {232, 128}, {233, 128}, {236, 128}, {237, 128} };
	for (kk = 0; kk < 256; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_wlbga_epa_elna) &&
			kk == fectrl_wlbga_epa_elna[fem_idx].idx) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				&(fectrl_wlbga_epa_elna[fem_idx].val));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static void
wlc_phy_write_regtbl_fc4_sub2(phy_info_t *pi)
{
	uint16 fectrl_zeroval[] = {0};
	uint16 kk, fem_idx = 0;
	sparse_array_entry_t fectrl_fchm_epa_elna[] =
	{{2, 280}, {3, 24}, {9, 48}, {18, 21}, {19, 20}, {25, 144}, {34, 776}, {35, 520},
	{41, 544}, {50, 517}, {51, 516}, {57, 640}, {66, 280}, {67, 24}, {73, 48}, {82, 21},
	{83, 20}, {89, 144}, {98, 776}, {99, 520}, {105, 544}, {114, 517}, {115, 516}, {121, 640},
	{128, 280}, {129, 24}, {130, 280}, {131, 24}, {132, 24}, {133, 24}, {134, 24}, {135, 24},
	{136, 280}, {137, 24}, {138, 280}, {139, 24}, {140, 24}, {141, 24}, {142, 24}, {143, 24},
	{160, 776}, {161, 520}, {162, 776}, {163, 520}, {164, 520}, {165, 520}, {166, 520},
	{167, 520}, {168, 776}, {169, 520}, {170, 776}, {171, 520}, {172, 520}, {173, 520},
	{174, 520}, {175, 520},	{192, 16}, {193, 16}, {196, 16}, {197, 16}, {200, 16}, {201, 16},
	{204, 16}, {205, 16}, {224, 512}, {225, 512}, {228, 512}, {229, 512}, {232, 512},
	{233, 512}, {236, 512}, {237, 512}};
	for (kk = 0; kk < 256; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_fchm_epa_elna) &&
			kk == fectrl_fchm_epa_elna[fem_idx].idx) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				&(fectrl_fchm_epa_elna[fem_idx].val));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static void
wlc_phy_write_regtbl_fc4_sub34(phy_info_t *pi)
{
	uint16 fectrl_zeroval[] = {0};
	uint16 kk, fem_idx = 0;

	sparse_array_entry_t fectrl_wlcsp_epa_elna[] =
		{{2, 34}, {3, 2}, {9, 1}, {18, 80}, {19, 16}, {25, 8}, {66, 34}, {67, 2},
		{73, 1}, {82, 80}, {83, 16}, {89, 8}, {128, 34}, {129, 2}, {130, 34}, {131, 2},
		{132, 2}, {133, 2}, {134, 2}, {135, 2}, {136, 34}, {137, 2}, {138, 34}, {139, 2},
		{140, 2}, {141, 2}, {142, 2}, {143, 2}, {160, 34}, {161, 2}, {162, 34}, {163, 2},
		{164, 2}, {165, 2}, {166, 2}, {167, 2}, {168, 34}, {169, 2}, {170, 34}, {171, 2},
		{172, 2}, {173, 2}, {174, 2}, {175, 2}, {192, 4}, {193, 4}, {196, 4}, {197, 4},
		{200, 4}, {201, 4}, {204, 4}, {205, 4}, {224, 4}, {225, 4}, {228, 4}, {229, 4},
		{232, 4}, {233, 4}, {236, 4}, {237, 4} };
	for (kk = 0; kk < 256; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_wlcsp_epa_elna) &&
			kk == fectrl_wlcsp_epa_elna[fem_idx].idx) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				&(fectrl_wlcsp_epa_elna[fem_idx].val));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static void
wlc_phy_write_regtbl_fc4_sub5(phy_info_t *pi)
{
	uint16 fectrl_zeroval[] = {0};
	uint16 kk, fem_idx = 0;

	sparse_array_entry_t fectrl_fp_dpdt_epa_elna[] =
		{{2, 280}, {3, 24}, {9, 48}, {18, 21}, {19, 20}, {25, 144}, {34, 776},
		{35, 520}, {41, 544}, {50, 517}, {51, 516}, {57, 640}, {130, 80},
		{192, 80}};

	for (kk = 0; kk < 256; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_fp_dpdt_epa_elna) &&
			kk == fectrl_fp_dpdt_epa_elna[fem_idx].idx) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				&(fectrl_fp_dpdt_epa_elna[fem_idx].val));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static INLINE void
wlc_phy_write_regtbl_fc4(phy_info_t *pi, phy_info_acphy_t *pi_ac)
{
	switch (BF3_FEMCTRL_SUB(pi_ac)) {
		case 0:
			wlc_phy_write_regtbl_fc4_sub0(pi);
		break;
		case 1:
			wlc_phy_write_regtbl_fc4_sub1(pi);
		break;
		case 2:
			wlc_phy_write_regtbl_fc4_sub2(pi);
		break;
		case 3:
		case 4:
			wlc_phy_write_regtbl_fc4_sub34(pi);
		break;
		case 5:
			wlc_phy_write_regtbl_fc4_sub5(pi);
		break;
	}
}

static void
wlc_phy_write_regtbl_fc10_sub0(phy_info_t *pi)
{
	uint16 fectrl_fcbga_epa_elna_idx[] = {2, 3, 9, 18, 19, 25, 66, 67, 73, 82, 83, 128,
		129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
		192, 193, 196, 197, 200, 201, 204, 205, 210, 211, 258, 259, 265, 274, 275, 281};
	uint16 fectrl_fcbga_epa_elna_val[] = {96, 32, 8, 6, 2, 1, 96, 32, 8, 6, 2, 96, 32,
		96, 32, 32, 32, 32, 32, 96, 32, 96, 32, 32, 32, 32, 32, 128, 128, 128, 128,
		128, 128, 128, 128, 134, 130, 5, 4, 8, 48, 32, 64 };
	uint16 fectrl_zeroval[] = {0};
	uint kk, fem_idx = 0;
	for (kk = 0; kk < 320; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_fcbga_epa_elna_idx) &&
			kk == fectrl_fcbga_epa_elna_idx[fem_idx]) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
			&(fectrl_fcbga_epa_elna_val[fem_idx]));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static void
wlc_phy_write_regtbl_fc10_sub1(phy_info_t *pi)
{
	uint16 fectrl_wlbga_epa_elna_idx[] = {2, 3, 9, 18, 19, 25, 66, 67, 73, 82, 83, 128,
		129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
		192, 193, 196, 197, 200, 201, 204, 205, 210, 211, 258, 259, 265, 274, 275, 281};
	uint16 fectrl_wlbga_epa_elna_val[] = {48, 32, 8, 6, 2, 1, 48, 32, 8, 6, 2, 48, 32,
	        48, 32, 32, 32, 32, 32, 48, 32, 48, 32, 32, 32, 32, 32, 128, 128, 128, 128,
		128, 128, 128, 128, 134, 130, 48, 32, 8, 6, 2, 1};
	uint16 fectrl_zeroval[] = {0};
	uint kk, fem_idx = 0;
	for (kk = 0; kk < 320; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_wlbga_epa_elna_idx) &&
			kk == fectrl_wlbga_epa_elna_idx[fem_idx]) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
			&(fectrl_wlbga_epa_elna_val[fem_idx]));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static void
wlc_phy_write_regtbl_fc10_sub2(phy_info_t *pi)
{
	uint16 fectrl_wlbga_ipa_ilna_idx[] = {2, 3, 9, 18, 19, 25, 66, 67, 73, 82, 83, 128,
		129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143,
		192, 193, 196, 197, 200, 201, 204, 205, 210, 211, 258, 259, 265, 274, 275, 281};
	uint16 fectrl_wlbga_ipa_ilna_val[] = {48, 32, 8, 6, 2, 1, 48, 32, 8, 6, 2, 48, 32,
	        48, 32, 32, 32, 32, 32, 48, 32, 48, 32, 32, 32, 32, 32, 128, 128, 128, 128,
		128, 128, 128, 128, 134, 130, 48, 32, 8, 6, 2, 1};
	uint16 fectrl_zeroval[] = {0};
	uint kk, fem_idx = 0;
	for (kk = 0; kk < 320; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_wlbga_ipa_ilna_idx) &&
			kk == fectrl_wlbga_ipa_ilna_idx[fem_idx]) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
			&(fectrl_wlbga_ipa_ilna_val[fem_idx]));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static void
wlc_phy_write_regtbl_fc10_sub3(phy_info_t *pi)
{
	uint16 fectrl_43556usb_epa_elna_idx[] = {2, 3, 9, 18, 19, 25, 66, 67, 73, 82, 83, 128,
		129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 192,
		193, 196, 197, 200, 201, 204, 205, 210, 211, 258, 259, 265, 274, 275, 281};
	uint16 fectrl_43556usb_epa_elna_val[] = {96, 32, 8, 6, 2, 1, 96, 32, 8, 6, 2, 96, 32, 96,
		32, 32, 32, 32, 32, 96, 32, 96, 32, 32, 32, 32, 32, 128, 128, 128, 128, 128, 128,
		128, 128, 134, 130, 5, 4, 8, 48, 32, 64};
	uint16 fectrl_zeroval[] = {0};
	uint kk, fem_idx = 0;
	for (kk = 0; kk < 320; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_43556usb_epa_elna_idx) &&
			kk == fectrl_43556usb_epa_elna_idx[fem_idx]) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
			&(fectrl_43556usb_epa_elna_val[fem_idx]));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static void
wlc_phy_write_regtbl_fc10_sub4(phy_info_t *pi)
{
	uint16 fectrl_fcbga_ipa_ilna_idx[] = {2, 3, 9, 18, 19, 25, 66, 67, 73, 82, 83, 128, 129,
		130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 192, 193,
		196, 197, 200, 201, 204, 205, 210, 211, 258, 259, 265, 265, 274, 275, 281, 281};
	uint16 fectrl_fcbga_ipa_ilna_val[] = {128, 32, 32, 8, 1, 1, 128, 32, 32, 8, 8, 128, 32,
		128, 32, 128, 32, 128, 32, 128, 32, 128, 32, 128, 32, 128, 32, 64, 64, 64, 64,
		64, 64, 64, 64, 72, 72, 4, 8, 8, 8, 64, 16, 16, 16};
	uint16 fectrl_zeroval[] = {0};
	uint kk, fem_idx = 0;
	for (kk = 0; kk < 320; kk++) {
		if (fem_idx < ARRAYSIZE(fectrl_fcbga_ipa_ilna_idx) &&
			kk == fectrl_fcbga_ipa_ilna_idx[fem_idx]) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
			&(fectrl_fcbga_ipa_ilna_val[fem_idx]));
			fem_idx++;
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_FEMCTRLLUT, 1, kk, 16,
				fectrl_zeroval);
		}
	}
}

static INLINE void
wlc_phy_write_regtbl_fc10(phy_info_t *pi, phy_info_acphy_t *pi_ac)
{
	switch (BF3_FEMCTRL_SUB(pi_ac)) {
	case 0:
	        wlc_phy_write_regtbl_fc10_sub0(pi);
	        break;
	case 1:
	        wlc_phy_write_regtbl_fc10_sub1(pi);
	        break;
	case 2:
	        wlc_phy_write_regtbl_fc10_sub2(pi);
	        break;
	case 3:
	        wlc_phy_write_regtbl_fc10_sub3(pi);
	        break;
	case 4:
	        wlc_phy_write_regtbl_fc10_sub4(pi);
	        break;
	}
}

static void
wlc_phy_tx_gm_gain_boost(phy_info_t *pi)
{
	uint8 core;

	ASSERT(RADIOID_IS(pi->pubpi->radioid, BCM2069_ID));

	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			MOD_RADIO_REGC(pi, TXGM_CFG1, core, gc_res, 0x1);
		}
	} else {
		if (BF_SROM11_GAINBOOSTA01(pi->u.pi_acphy)) {
			/* Boost A0/1 radio gain */
			FOREACH_CORE(pi, core) {
				MOD_RADIO_REGC(pi, TXMIX5G_CFG1, core, gainboost, 0x6);
				MOD_RADIO_REGC(pi, PGA5G_CFG1, core, gainboost, 0x6);
			}
		}
		if (RADIO2069REV(pi->pubpi->radiorev) <= 3) {
			/* Boost A2 radio gain */
			core = 2;
			MOD_RADIO_REGC(pi, TXMIX5G_CFG1, core, gainboost, 0x6);
			MOD_RADIO_REGC(pi, PGA5G_CFG1, core, gainboost, 0x6);
		}
	}
}

static void
wlc_phy_write_rx_farrow_pre_tiny(phy_info_t *pi, chan_info_rx_farrow *rx_farrow,
	chanspec_t chanspec)
{
	uint16 deltaphase_lo, deltaphase_hi;
	uint16 drift_period, farrow_ctrl;

#ifdef ACPHY_1X1_ONLY
	uint8 channel = CHSPEC_CHANNEL(chanspec);
	uint32 deltaphase;

	if (channel <= 14) {
		if (CHSPEC_IS20(chanspec))
			drift_period = 5120; /* 40x32x4 */
		else if (CHSPEC_IS40(chanspec))
			drift_period = 5120; /* 40x32x4 */
		else
			drift_period = 1280; /* 160x4x2 */
	} else {
		if (CHSPEC_IS20(chanspec))
			drift_period = 3840; /* 40x24x4 */
		else if (CHSPEC_IS40(chanspec))
			drift_period = 3840; /* 40x24x4 */
		else
			drift_period = 2880; /* 160x9x2 */
	}

	if (CHSPEC_IS80(chanspec)) {
		deltaphase = rx_farrow->deltaphase_80;
		farrow_ctrl = rx_farrow->farrow_ctrl_80;
	} else {
		deltaphase = rx_farrow->deltaphase_20_40;
		farrow_ctrl = rx_farrow->farrow_ctrl_20_40;
	}
	if (ACMAJORREV_1(pi->pubpi->phy_rev) && !(ACMINORREV_0(pi) || ACMINORREV_1(pi))) {
		farrow_ctrl = (farrow_ctrl &
			~ACPHY_rxFarrowCtrl_rx_farrow_outShift_MASK(pi->pubpi->phy_rev));
	}
	deltaphase_lo = deltaphase & 0xffff;
	deltaphase_hi = (deltaphase >> 16) & 0xff;


#else  /* ACPHY_1X1_ONLY */
	UNUSED_PARAMETER(chanspec);

	/* Setup the Rx Farrow */
	deltaphase_lo = rx_farrow->deltaphase_lo;
	deltaphase_hi = rx_farrow->deltaphase_hi;
	drift_period = rx_farrow->drift_period;
	farrow_ctrl = rx_farrow->farrow_ctrl;


#endif  /* ACPHY_1X1_ONLY */
	/* Setup the Rx Farrow */
	WRITE_PHYREG(pi, rxFarrowDeltaPhase_lo, deltaphase_lo);
	WRITE_PHYREG(pi, rxFarrowDeltaPhase_hi, deltaphase_hi);
	WRITE_PHYREG(pi, rxFarrowDriftPeriod, drift_period);
	WRITE_PHYREG(pi, rxFarrowCtrl, farrow_ctrl);

	/* Use the same settings for the loopback Farrow */
	WRITE_PHYREG(pi, lbFarrowDeltaPhase_lo, deltaphase_lo);
	WRITE_PHYREG(pi, lbFarrowDeltaPhase_hi, deltaphase_hi);
	WRITE_PHYREG(pi, lbFarrowDriftPeriod, drift_period);
	WRITE_PHYREG(pi, lbFarrowCtrl, farrow_ctrl);
}

#ifdef WL11ULB
/* Function to set/reset 5/10MHz mode (cor. TCL proc is ulb_mode in chipc.tcl) */
/* ulb_mode: 0 - reset to normal mode
 * ulb_mode: 1 - 10MHz mode
 * ulb_mode: 2 - 5MHz mode
 */
void
wlc_phy_ulb_mode(phy_info_t *pi, uint8 ulb_mode)
{
	wlapi_suspend_mac_and_wait(pi->sh->physhim);
	if ((ACMAJORREV_4(pi->pubpi->phy_rev) && (ACMINORREV_2(pi))) ||
	    ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		uint16 dac_rate_mode;
		uint8 orig_soft_sdfeFifoReset_val, orig_disable_stalls_val;
		uint8 orig_forceAfeClocksOff_val;
		uint16 orig_sdfeClkGatingCtrl_val;
		si_t *sih = pi->sh->sih;

		/* Set the PLL */
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			if (pi->prev_pmu_ulbmode != ulb_mode) {
				if (!pi->sh->up) {
					si_pmu_set_ulbmode(sih, pi->sh->osh, ulb_mode);
					pi->prev_pmu_ulbmode = ulb_mode;
				} else {
					PHY_ERROR(("wl%d: %s: si_pmu_set_ulbmode() was bypassed\n",
							pi->sh->unit,	__FUNCTION__));
				}
			}
		} else {
			si_pmu_set_ulbmode(sih, pi->sh->osh, ulb_mode);
		}

		/* Set the filters */
		if (!ISSIM_ENAB(pi->sh->sih)) {
			if (ulb_mode == PMU_ULB_BW_NONE) {
				/* normal mode */
				MOD_PHYREG(pi, UlbCtrl, useFirAB, 0x0);
				MOD_PHYREG(pi, UlbCtrl, useFirBbypA, 0x0);
			} else if (ulb_mode == PMU_ULB_BW_10MHZ) {
				/* 10MHz mode */
				MOD_PHYREG(pi, UlbCtrl, useFirAB, 0x0);
				MOD_PHYREG(pi, UlbCtrl, useFirBbypA, 0x1);
			} else {
				/* 5MHz mode */
				MOD_PHYREG(pi, UlbCtrl, useFirAB, 0x1);
				MOD_PHYREG(pi, UlbCtrl, useFirBbypA, 0x0);
			}

			/* To avoid Rx stalling */
			if (ulb_mode != PMU_ULB_BW_NONE) {
				/* ulb mode */
				MOD_PHYREG(pi, sdfeClkGatingCtrl, txlbclkmode_ovr, 0x1);
				MOD_PHYREG(pi, sdfeClkGatingCtrl, txlbclkmode_ovr_value, 0x1);
			} else {
				/* normal mode */
				MOD_PHYREG(pi, sdfeClkGatingCtrl, txlbclkmode_ovr, 0x1);
				MOD_PHYREG(pi, sdfeClkGatingCtrl, txlbclkmode_ovr_value, 0x0);
				MOD_PHYREG(pi, sdfeClkGatingCtrl, txlbclkmode_ovr, 0x0);
			}
			if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			    ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
				if (ulb_mode != PMU_ULB_BW_NONE) {
					MOD_PHYREG(pi, FFTSoftReset, lbsdadc_clken_ovr, 0x1);
					MOD_PHYREG(pi, FFTSoftReset, lbsdadc_clken_ovr_value, 0x0);
				} else {
					MOD_PHYREG(pi, FFTSoftReset, lbsdadc_clken_ovr, 0x0);
				}
			}
		}

		/* Set the dac rate mode */
		if (ulb_mode == PMU_ULB_BW_NONE) {
			/* normal mode */
			dac_rate_mode = 1;
		} else if (ulb_mode == PMU_ULB_BW_10MHZ) {
			/* 10MHz mode */
			dac_rate_mode = 3;
		} else {
			/* 5MHz mode */
			dac_rate_mode = 2;
		}

		/* Store AFE clocks to PHY */
		orig_soft_sdfeFifoReset_val = READ_PHYREGFLD(pi, RxFeCtrl1, soft_sdfeFifoReset);
		orig_disable_stalls_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
		orig_sdfeClkGatingCtrl_val = READ_PHYREG(pi, sdfeClkGatingCtrl);
		orig_forceAfeClocksOff_val = READ_PHYREGFLD(pi, fineclockgatecontrol,
			forceAfeClocksOff);

		/* Stall AFE clocks to PHY */
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 0x1);
		MOD_PHYREG(pi, RxFeCtrl1, disable_stalls, 0x1);
		WRITE_PHYREG(pi, sdfeClkGatingCtrl, 0xE);
		MOD_PHYREG(pi, fineclockgatecontrol, forceAfeClocksOff, 0x1);

		/* Tx phy toggle */
		si_core_cflags(sih, SICF_DAC, dac_rate_mode << 8);

		/* Restore AFE clocks to PHY */
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, orig_soft_sdfeFifoReset_val);
		MOD_PHYREG(pi, RxFeCtrl1, disable_stalls, orig_disable_stalls_val);
		WRITE_PHYREG(pi, sdfeClkGatingCtrl, orig_sdfeClkGatingCtrl_val);
		MOD_PHYREG(pi, fineclockgatecontrol, forceAfeClocksOff, orig_forceAfeClocksOff_val);
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		/* Set the PLL */
		si_pmu_set_ulbmode(pi->sh->sih, pi->sh->osh, ulb_mode);

		/* Set Backend clks */
		if (ulb_mode == PMU_ULB_BW_NONE) {
			/* normal mode */
			WRITE_PHYREG(pi, UlbCtrl, 0x0);
			WRITE_PHYREG(pi, UlbCtrl_tx, 0x0);
		} else if (ulb_mode == PMU_ULB_BW_10MHZ) {
			/* 10MHz mode */
			WRITE_PHYREG(pi, UlbCtrl, 0x14);
			WRITE_PHYREG(pi, UlbCtrl_tx, 0x5);
		} else if (ulb_mode == PMU_ULB_BW_5MHZ) {
			/* 5MHz mode */
			WRITE_PHYREG(pi, UlbCtrl, 0xc);
			WRITE_PHYREG(pi, UlbCtrl_tx, 0x3);
		} else {
			/* 2.5MHz mode */
			WRITE_PHYREG(pi, UlbCtrl, 0x4);
			WRITE_PHYREG(pi, UlbCtrl_tx, 0x1);
		}
		wlc_phy_resetcca_acphy(pi);
	}
	wlapi_enable_mac(pi->sh->physhim);
}
#endif /* WL11ULB */

static void
BCMATTACHFN(phy_ac_chanmgr_nvram_attach)(phy_ac_chanmgr_info_t *ac_info)
{
	uint8 i;
	uint8 csml;
#ifndef BOARD_FLAGS3
	uint32 bfl3; /* boardflags3 */
#endif
	phy_info_t *pi = ac_info->pi;
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;


	csml = (uint8)PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_csml, 0x11);

	/* PA Mode is set so that NVRAM values are used by default */
	ac_info->aci->pa_mode = AUTO;

	ac_info->ml_en =  (csml & 0xF);
	ac_info->chsm_en =  (csml & 0xF0) >> 4;

	/* low mcs gamma and gain values for PAPRR */
	for (i = 0; i < NUM_MCS_PAPRR_GAMMA; i++) {
		pi->paprrmcsgamma2g[i] = (int16) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_paprrmcsgamma2g, i, -1));
		pi->paprrmcsgain2g[i] = (uint8) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_paprrmcsgain2g, i, 128));
		pi->paprrmcsgamma5g20[i] = (int16) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_paprrmcsgamma5g20, i, -1));
		pi->paprrmcsgain5g20[i] = (uint8) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_paprrmcsgain5g20, i, 128));
		pi->paprrmcsgamma5g40[i] = (int16) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_paprrmcsgamma5g40, i, -1));
		pi->paprrmcsgain5g40[i] = (uint8) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_paprrmcsgain5g40, i, 128));
		pi->paprrmcsgamma5g80[i] = (int16) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_paprrmcsgamma5g80, i, -1));
		pi->paprrmcsgain5g80[i] = (uint8) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_paprrmcsgain5g80, i, 128));
	}

	if ((PHY_GETVAR_SLICE(pi, rstr_cckdigfilttype)) != NULL) {
		ac_info->acphy_cck_dig_filt_type = (uint8)PHY_GETINTVAR_SLICE(pi,
			rstr_cckdigfilttype);
	} else {
		if (ACMAJORREV_1(pi->pubpi->phy_rev) &&
			ACMINORREV_2(pi) &&
			((pi->epagain2g == 2) || (pi->extpagain2g == 2)) &&
			((pi->epagain5g == 2) || (pi->extpagain5g == 2)) &&
			PHY_XTAL_IS40M(pi)) {
			/* 43162yp improving ACPR */
			ac_info->acphy_cck_dig_filt_type = 0x02;
		} else {
			/* bit0 is gaussian shaping and bit1 & 2 are for RRC alpha */
			ac_info->acphy_cck_dig_filt_type = 0x01;
		}
	}

#ifndef WL_FDSS_DISABLED
	pi->fdss_interp_en = (uint8) (PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_fdss_interp_en, 1));
	for (i = 0; i < 2; i++) {
		pi->fdss_level_2g[i] = (int8) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_fdss_level_2g, i, -1));
		pi->fdss_level_5g[i] = (int8) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE(pi,
			rstr_fdss_level_5g, i, -1));
	}
#endif /* !WL_FDSS_DISABLED */
	ac_info->cfg.srom_paprdis = (bool)PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_paprdis, FALSE);
	ac_info->cfg.srom_papdwar = (int8)PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_papdwar, -1);
	ac_info->cfg.srom_txnoBW80ClkSwitch = (uint8)PHY_GETINTVAR_DEFAULT_SLICE(pi,
		rstr_txnoBW80ClkSwitch, 0);

	if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
		ac_info->cfg.srom_tssisleep_en =
			(uint)PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_tssisleep_en, 7);
	} else {
		ac_info->cfg.srom_tssisleep_en =
			(uint)PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_tssisleep_en, 0);
	}

#ifndef BOARD_FLAGS
	BF_SROM11_GAINBOOSTA01(pi_ac) = ((BOARDFLAGS(GENERIC_PHY_INFO(pi)->boardflags) &
		BFL_SROM11_GAINBOOSTA01) != 0);
#endif /* BOARD_FLAGS */

#ifndef BOARD_FLAGS2
	BF2_SROM11_APLL_WAR(pi_ac) = ((BOARDFLAGS2(GENERIC_PHY_INFO(pi)->boardflags2) &
		BFL2_SROM11_APLL_WAR) != 0);
	BF2_2G_SPUR_WAR(pi_ac) = ((BOARDFLAGS2(GENERIC_PHY_INFO(pi)->boardflags2) &
		BFL2_2G_SPUR_WAR) != 0);
	BF2_DAC_SPUR_IMPROVEMENT(pi_ac) = (BOARDFLAGS2(GENERIC_PHY_INFO(pi)->boardflags2) &
		BFL2_DAC_SPUR_IMPROVEMENT) != 0;
#endif /* BOARD_FLAGS2 */

#ifndef BOARD_FLAGS3
	if ((PHY_GETVAR_SLICE(pi, rstr_boardflags3)) != NULL) {
		bfl3 = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_boardflags3);
		BF3_AVVMID_FROM_NVRAM(pi_ac) = (bfl3 & BFL3_AVVMID_FROM_NVRAM)
			>> BFL3_AVVMID_FROM_NVRAM_SHIFT;
		BF3_BBPLL_SPR_MODE_DIS(pi_ac) = ((bfl3 & BFL3_BBPLL_SPR_MODE_DIS) != 0);
		BF3_PHASETRACK_MAX_ALPHABETA(pi_ac) = (bfl3 & BFL3_PHASETRACK_MAX_ALPHABETA) >>
			BFL3_PHASETRACK_MAX_ALPHABETA_SHIFT;
		BF3_ACPHY_LPMODE_2G(pi_ac) = (bfl3 & BFL3_ACPHY_LPMODE_2G) >>
			BFL3_ACPHY_LPMODE_2G_SHIFT;
		BF3_ACPHY_LPMODE_5G(pi_ac) = (bfl3 & BFL3_ACPHY_LPMODE_5G) >>
			BFL3_ACPHY_LPMODE_5G_SHIFT;
		BF3_RSDB_1x1_BOARD(pi_ac) = (bfl3 & BFL3_1X1_RSDB_ANT) >>
			BFL3_1X1_RSDB_ANT_SHIFT;
		BF3_5G_SPUR_WAR(pi_ac) = ((bfl3 & BFL3_5G_SPUR_WAR) != 0);
	} else {
		BF3_BBPLL_SPR_MODE_DIS(pi_ac) = 0;
		BF3_PHASETRACK_MAX_ALPHABETA(pi_ac) = 0;
		BF3_ACPHY_LPMODE_2G(pi_ac) = 0;
		BF3_ACPHY_LPMODE_5G(pi_ac) = 0;
		BF3_RSDB_1x1_BOARD(pi_ac) = 0;
		BF3_5G_SPUR_WAR(pi_ac) = 0;
	}
#endif /* BOARD_FLAGS3 */

	pi->sromi->ofdmfilttype = (uint8)PHY_GETINTVAR_DEFAULT_SLICE(pi,
		rstr_ofdmfilttype_5gbe, 127);
	pi->sromi->ofdmfilttype_2g = (uint8)PHY_GETINTVAR_DEFAULT_SLICE(pi,
		rstr_ofdmfilttype_2gbe, 127);

	if ((PHY_GETVAR_SLICE(pi, ed_thresh2g)) != NULL) {
		pi_ac->sromi->ed_thresh2g = (int32)PHY_GETINTVAR_SLICE(pi, ed_thresh2g);
	} else {
		pi_ac->sromi->ed_thresh2g = 0;
	}

	if ((PHY_GETVAR_SLICE(pi, ed_thresh5g)) != NULL) {
		pi_ac->sromi->ed_thresh5g = (int32)PHY_GETINTVAR_SLICE(pi, ed_thresh5g);
	} else {
		pi_ac->sromi->ed_thresh5g = 0;
	}

	if ((PHY_GETVAR_SLICE(pi, "eu_edthresh2g")) != NULL) {
		pi->srom_eu_edthresh2g = (int8)PHY_GETINTVAR_SLICE(pi, "eu_edthresh2g");
	} else {
		pi->srom_eu_edthresh2g = 0;
	}
	if ((PHY_GETVAR_SLICE(pi, "eu_edthresh5g")) != NULL) {
		pi->srom_eu_edthresh5g = (int8)PHY_GETINTVAR_SLICE(pi, "eu_edthresh5g");
	} else {
		pi->srom_eu_edthresh5g = 0;
	}

	pi->sromi->lpflags = (uint32)PHY_GETINTVAR_DEFAULT(pi, rstr_lpflags, 0);
	pi->sromi->subband5Gver =
		(uint8)PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_subband5gver, PHY_SUBBAND_4BAND);
	ac_info->cfg.LTEJ_WAR_en = (bool)PHY_GETINTVAR_DEFAULT(pi, rstr_LTEJ_WAR_en, 1);
}

/* ********************************************* */
/*				External Definitions					*/
/* ********************************************* */

void
phy_ac_chanmgr_write_rx_farrow_tiny(phy_info_t *pi, chanspec_t chanspec,
                             chanspec_t chanspec_sc, int sc_mode)
{
	uint8 ch, num, den, bw, M, vco_div, core;
	uint8 chans[NUM_CHANS_IN_CHAN_BONDING];
	uint32 fcw, fcw1, tmp_low = 0, tmp_high = 0;
	uint32 fc, fc1;
	chanspec_t chanspec_sel = chanspec;
	bool vco_12GHz = pi->u.pi_acphy->chanmgri->vco_12GHz;
	if (sc_mode == 1) {
		chanspec_sel = chanspec_sc;
	}
	bw = CHSPEC_IS20(chanspec) ? PHYBW_20: CHSPEC_IS40(chanspec) ? PHYBW_40 :
		(CHSPEC_IS80(chanspec) || PHY_AS_80P80(pi, chanspec)) ? PHYBW_80 :
		CHSPEC_IS160(chanspec) ? 0 : 0;

	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		if (!vco_12GHz) {
			num = 3;
			den = 2;
		} else {
			num = 4;
			den = 1;
		}
	} else {
		if (!vco_12GHz) {
			num = 2;
			den = 3;
		} else {
			num = 2;
			den = 1;
		}
	}

	if (vco_12GHz) {
		if ((pi->u.pi_acphy->chanmgri->data.fast_adc_en) ||
			(ACMAJORREV_4(pi->pubpi->phy_rev) && CHSPEC_IS8080(chanspec))) {
			M = SIPO_DIV_FAST * PHYBW_80 / bw;
			vco_div = AFE_DIV_FAST * ADC_DIV_FAST;
		} else {
			M = SIPO_DIV_SLOW;
			vco_div = AFE_DIV_BW(bw) * ADC_DIV_SLOW;
		}
	} else {
		if (CHSPEC_IS20(chanspec_sel)) {
			M = SIPO_DIV_SLOW;
			vco_div = 6;
		} else if (CHSPEC_IS40(chanspec_sel)) {
			M = SIPO_DIV_SLOW;
			vco_div = 3;
		} else if (CHSPEC_IS80(chanspec_sel) ||
				PHY_AS_80P80(pi, chanspec_sel)) {
			M = SIPO_DIV_FAST;
			vco_div = 1;
		} else if (CHSPEC_IS160(chanspec_sel)) {
			M = SIPO_DIV_FAST;
			vco_div = 1;
			ASSERT(0);
		} else {
			M = SIPO_DIV_FAST;
			vco_div = 1;
		}
	}

	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
		const uint8 afeclkdiv_arr[] = {2, 16, 4, 8, 3, 24, 6, 12};
		const uint8 adcclkdiv_arr[] = {1, 2, 3, 6};
		const uint8 sipodiv_arr[] = {12, 8};
		const chan_info_radio20693_altclkplan_t *altclkpln = altclkpln_radio20693;
		int row;
		if (ROUTER_4349(pi)) {
			altclkpln = altclkpln_radio20693_router4349;
		}
		row = wlc_phy_radio20693_altclkpln_get_chan_row(pi);
		if ((row >= 0) && (pi->u.pi_acphy->chanmgri->data.fast_adc_en == 0)) {
			num = CHSPEC_IS2G(pi->radio_chanspec) ? 4 : 2;
			M = sipodiv_arr[altclkpln[row].sipodiv];
			den = 1;
			vco_div = afeclkdiv_arr[altclkpln[row].afeclkdiv] *
				adcclkdiv_arr[altclkpln[row].adcclkdiv];
		}
	}
	/* bits_in_mu = 24 */
	/*
	fcw = (num * phy_utils_channel2freq(ch) * (((uint32)(1<<31))/
		(den * vco_div * 2 * M * bw)))>> 7;
	*/
	if (CHSPEC_IS8080(chanspec) &&
	    !(ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev))) {
		FOREACH_CORE(pi, core) {
			if (core == 0) {
				ch = wf_chspec_primary80_channel(chanspec);
				fc = wf_channel2mhz(ch, WF_CHAN_FACTOR_5_G);

				bcm_uint64_multiple_add(&tmp_high, &tmp_low, fc * num, 1 << 24, 0);
				bcm_uint64_divide(&fcw, tmp_high, tmp_low,
					(uint32) (den * vco_div * 2 * M * bw));

				PHY_INFORM(("%s: fcw 0x%0x ch %d freq %d vco_div %d bw %d\n",
					__FUNCTION__, fcw, ch, phy_utils_channel2freq(ch),
					vco_div, bw));

				MOD_PHYREG(pi, RxSdFeConfig20, fcw_value_lo, fcw & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig30, fcw_value_hi,
					(fcw >> 16) & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig30, fast_ADC_en,
					(pi->u.pi_acphy->chanmgri->data.fast_adc_en & 0x1));
			} else if (core == 1) {
				ch = wf_chspec_secondary80_channel(chanspec);
				fc = wf_channel2mhz(ch, WF_CHAN_FACTOR_5_G);

				bcm_uint64_multiple_add(&tmp_high, &tmp_low, fc * num, 1 << 24, 0);
				bcm_uint64_divide(&fcw, tmp_high, tmp_low,
					(uint32) (den * vco_div * 2 * M * bw));

				PHY_INFORM(("%s: fcw 0x%0x ch %d freq %d vco_div %d bw %d\n",
					__FUNCTION__, fcw, ch, phy_utils_channel2freq(ch),
					vco_div, bw));

				MOD_PHYREG(pi, RxSdFeConfig21, fcw_value_lo, fcw & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig31, fcw_value_hi,
					(fcw >> 16) & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig31, fast_ADC_en,
					(pi->u.pi_acphy->chanmgri->data.fast_adc_en & 0x1));
			}
		}
	} else {
		//ch = CHSPEC_CHANNEL(chanspec);
		//fc = wf_channel2mhz(ch, CHSPEC_IS2G(pi->radio_chanspec) ? WF_CHAN_FACTOR_2_4_G
		//: WF_CHAN_FACTOR_5_G);
		if (ACMAJORREV_33(pi->pubpi->phy_rev) &&
				(PHY_AS_80P80(pi, chanspec_sel))) {
			wf_chspec_get_80p80_channels(chanspec, chans);
			fc  = wf_channel2mhz(chans[0], WF_CHAN_FACTOR_5_G);
			fc1 = wf_channel2mhz(chans[1], WF_CHAN_FACTOR_5_G);

			bcm_uint64_multiple_add(&tmp_high, &tmp_low, fc * num, 1 << 24, 0);
			bcm_uint64_divide(&fcw, tmp_high, tmp_low,
					(uint32) (den * vco_div * 2 * M * bw));

			bcm_uint64_multiple_add(&tmp_high, &tmp_low, fc1 * num, 1 << 24, 0);
			bcm_uint64_divide(&fcw1, tmp_high, tmp_low,
					(uint32) (den * vco_div * 2 * M * bw));

			PHY_INFORM(("%s: fcw0 0x%0x ch0 %d fc %d freq0 %d vco_div %d bw %d\n",
				__FUNCTION__, fcw, chans[0], fc,
				phy_utils_channel2freq((uint)chans[0]), vco_div, bw));
			PHY_INFORM(("%s: fcw1 0x%0x ch1 %d fc %d freq1 %d vco_div %d bw %d\n",
				__FUNCTION__, fcw1, chans[1], fc1,
				phy_utils_channel2freq((uint)chans[1]), vco_div, bw));
		} else {
			ch = CHSPEC_CHANNEL(chanspec_sel);
			fc = wf_channel2mhz(ch, CHSPEC_IS2G(chanspec_sel) ? WF_CHAN_FACTOR_2_4_G
				: WF_CHAN_FACTOR_5_G);


			bcm_uint64_multiple_add(&tmp_high, &tmp_low, fc * num, 1 << 24, 0);
			bcm_uint64_divide(&fcw, tmp_high, tmp_low,
				(uint32) (den * vco_div * 2 * M * bw));
			fcw1 = fcw;
			PHY_INFORM(("%s: fcw 0x%0x ch %d freq %d vco_div %d bw %d\n",
				__FUNCTION__, fcw, ch, phy_utils_channel2freq(ch), vco_div, bw));
		}

		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		    ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			if (sc_mode == 1) {
				if (CHSPEC_BW(chanspec) != CHSPEC_BW(chanspec_sc)) {
					printf("NOT SUPPORT SC CORE BW != NORMAL CORE BW !!! \n");
					ASSERT(0);
				}
				MOD_PHYREG(pi, RxSdFeConfig2_path3, fcw_value_lo, fcw & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig3_path3, fcw_value_hi,
					(fcw >> 16) & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig3_path3, fast_ADC_en,
					(pi->u.pi_acphy->chanmgri->data.fast_adc_en & 0x1));
			} else {
				MOD_PHYREG(pi, RxSdFeConfig2_path0, fcw_value_lo, fcw & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig2_path1, fcw_value_lo, fcw & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig2_path2, fcw_value_lo, fcw1 & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig2_path3, fcw_value_lo, fcw1 & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig3_path0, fcw_value_hi,
					(fcw >> 16) & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig3_path1, fcw_value_hi,
					(fcw >> 16) & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig3_path2, fcw_value_hi,
					(fcw1 >> 16) & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig3_path3, fcw_value_hi,
					(fcw1 >> 16) & 0xffff);
				MOD_PHYREG(pi, RxSdFeConfig3_path0, fast_ADC_en,
					(pi->u.pi_acphy->chanmgri->data.fast_adc_en & 0x1));
				MOD_PHYREG(pi, RxSdFeConfig3_path1, fast_ADC_en,
					(pi->u.pi_acphy->chanmgri->data.fast_adc_en & 0x1));
				MOD_PHYREG(pi, RxSdFeConfig3_path2, fast_ADC_en,
					(pi->u.pi_acphy->chanmgri->data.fast_adc_en & 0x1));
				MOD_PHYREG(pi, RxSdFeConfig3_path3, fast_ADC_en,
					(pi->u.pi_acphy->chanmgri->data.fast_adc_en & 0x1));
			}
		} else {
			MOD_PHYREG(pi, RxSdFeConfig2, fcw_value_lo, fcw & 0xffff);
			MOD_PHYREG(pi, RxSdFeConfig3, fcw_value_hi, (fcw >> 16) & 0xffff);
			MOD_PHYREG(pi, RxSdFeConfig3, fast_ADC_en,
			           (pi->u.pi_acphy->chanmgri->data.fast_adc_en & 0x1));
		}
	}
}

void
wlc_phy_farrow_setup_acphy(phy_info_t *pi, chanspec_t chanspec)
{
#ifdef ACPHY_1X1_ONLY
	uint32 dac_resamp_fcw;
	uint16 MuDelta_l, MuDelta_u;
	uint16 MuDeltaInit_l, MuDeltaInit_u;
#endif
	uint16 channel = CHSPEC_CHANNEL(chanspec);
	const uint16 *resamp_set = NULL;
	chan_info_tx_farrow *tx_farrow = NULL;
	chan_info_rx_farrow *rx_farrow;
	int bw_idx = 0;
	int tbl_idx = 0;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	if (ISSIM_ENAB(pi->sh->sih)) {
		/* Use channel 7(2g)/151(5g) settings for Quickturn */
		if (CHSPEC_IS2G(chanspec)) {
			channel = 7;
		} else {
			channel = 155;
		}
	}

	/* China 40M Spur WAR */
	if (ACMAJORREV_0(pi->pubpi->phy_rev)) {
		uint8 core;
		/* Cleanup Overrides */
		MOD_PHYREG(pi, AfeClkDivOverrideCtrl, afediv_sel_div_ovr, 0);
		MOD_PHYREG(pi, AfeClkDivOverrideCtrl, afediv_sel_div, 0x0);
		pi->sdadc_config_override = FALSE;

		FOREACH_CORE(pi, core) {
			MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_iqadc_flashhspd, 0);
			MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqadc_flashhspd, 0);
			MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_ctrl_flash17lvl, 0);
			MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_ctrl_flash17lvl, 0);
			MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_iqadc_mode, 0);
			MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqadc_mode, 0);
		}
	}

#ifdef ACPHY_1X1_ONLY
	bw_idx = 0;
#else /* ACPHY_1X1_ONLY */
	bw_idx = CHSPEC_BW_LE20(chanspec)? 0 : (CHSPEC_IS40(chanspec)? 1 : 2);
#endif /* ACPHY_1X1_ONLY */
	if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		/* Compute rx farrow setup */
		wlc_phy_write_rx_farrow_acphy(pi->u.pi_acphy->chanmgri, chanspec);
	} else {
		/* Find the Rx Farrow settings in the table for the specific b/w and channel */
		for (tbl_idx = 0; tbl_idx < ACPHY_NUM_CHANS; tbl_idx++) {
			rx_farrow = &pi->u.pi_acphy->rx_farrow[bw_idx][tbl_idx];
			if (rx_farrow->chan == channel) {
				wlc_phy_write_rx_farrow_pre_tiny(pi, rx_farrow, chanspec);
				break;
			}
		}

		/*
		 * No need to iterate through the Tx Farrow table, since the channels have the same
		 * order as the Rx Farrow table.
		 */

		if (tbl_idx == ACPHY_NUM_CHANS) {
			PHY_ERROR(("wl%d: %s: Failed to find Farrow settings"
				   " for bw=%d, channel=%d\n",
				   pi->sh->unit, __FUNCTION__, CHSPEC_BW(chanspec), channel));
			return;
		}
	}

#ifdef ACPHY_1X1_ONLY
	ASSERT(((phy_info_acphy_t *)pi->u.pi_acphy)->dac_mode == 1);
	tx_farrow = &pi->u.pi_acphy->tx_farrow[0][tbl_idx];
	dac_resamp_fcw = tx_farrow->dac_resamp_fcw;

	if (CHSPEC_IS80(chanspec))
	{
		dac_resamp_fcw += (dac_resamp_fcw >> 1);
	}

	dac_resamp_fcw = (dac_resamp_fcw + 32) >> 6;

	MuDelta_l = (dac_resamp_fcw & 0xFFFF);
	MuDelta_u = (dac_resamp_fcw & 0xFF0000) >> 16;
	MuDeltaInit_l = (dac_resamp_fcw & 0xFFFF);
	MuDeltaInit_u = (dac_resamp_fcw & 0xFF0000) >> 16;

	wlc_phy_tx_farrow_mu_setup(pi, MuDelta_l, MuDelta_u, MuDeltaInit_l, MuDeltaInit_u);
#else /* ACPHY_1X1_ONLY */
	if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		/* Compute tx farrow setup */
		wlc_phy_write_tx_farrow_acphy(pi->u.pi_acphy->chanmgri, chanspec);
	} else {
		tx_farrow = &pi->u.pi_acphy->tx_farrow[bw_idx][tbl_idx];
		wlc_phy_tx_farrow_mu_setup(pi, tx_farrow->MuDelta_l, tx_farrow->MuDelta_u,
			tx_farrow->MuDeltaInit_l, tx_farrow->MuDeltaInit_u);
	}
#endif /* ACPHY_1X1_ONLY */

	/* China 40M Spur WAR */
	if (ACMAJORREV_0(pi->pubpi->phy_rev) &&
	    (pi->afe_override) && CHSPEC_IS40(pi->radio_chanspec)) {
		uint16 fc;
		if (CHSPEC_CHANNEL(pi->radio_chanspec) > 14)
			fc = CHAN5G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));
		else
			fc = CHAN2G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));

		/* AFE Settings */
		if (fc == 5310) {
			uint8 core;
			MOD_PHYREG(pi, AfeClkDivOverrideCtrl, afediv_sel_div_ovr, 0x1);
			MOD_PHYREG(pi, AfeClkDivOverrideCtrl, afediv_sel_div, 0x0);

			FOREACH_CORE(pi, core) {
				MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_iqadc_flashhspd, 1);
				MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core,
				             afe_iqadc_flashhspd, 1);
				MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_ctrl_flash17lvl, 0);
				MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core,
				             afe_ctrl_flash17lvl, 1);
				MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_iqadc_mode, 1);
				MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqadc_mode, 1);
			}

			ACPHY_REG_LIST_START
				MOD_RADIO_REG_ENTRY(pi, RF0, AFEDIV1, afediv_main_driver_size, 8)
				MOD_RADIO_REG_ENTRY(pi, RF0, AFEDIV2, afediv_repeater1_dsize, 8)
				MOD_RADIO_REG_ENTRY(pi, RF0, AFEDIV2, afediv_repeater2_dsize, 8)
			ACPHY_REG_LIST_EXECUTE(pi);

			/* Set Override variable to pick up correct settings during cals */
			pi->sdadc_config_override = TRUE;
		} else if (fc == 5270) {
			MOD_PHYREG(pi, AfeClkDivOverrideCtrl, afediv_sel_div_ovr, 0x1);
			MOD_PHYREG(pi, AfeClkDivOverrideCtrl, afediv_sel_div, 0x2);
		}

		/* Resampler Settings */
		if (fc == 5270)
			resamp_set = resamp_cnwar_5270;
		else if (fc == 5310)
			resamp_set = resamp_cnwar_5310;

		if (resamp_set != NULL) {
			WRITE_PHYREG(pi, rxFarrowDeltaPhase_lo, resamp_set[0]);
			WRITE_PHYREG(pi, rxFarrowDeltaPhase_hi, resamp_set[1]);
			WRITE_PHYREG(pi, rxFarrowDriftPeriod, resamp_set[2]);
			WRITE_PHYREG(pi, lbFarrowDeltaPhase_lo, resamp_set[3]);
			WRITE_PHYREG(pi, lbFarrowDeltaPhase_hi, resamp_set[4]);
			WRITE_PHYREG(pi, lbFarrowDriftPeriod, resamp_set[5]);
			ACPHYREG_BCAST(pi, TxResamplerMuDelta0l, resamp_set[6]);
			ACPHYREG_BCAST(pi, TxResamplerMuDelta0u, resamp_set[7]);
			ACPHYREG_BCAST(pi, TxResamplerMuDeltaInit0l, resamp_set[8]);
			ACPHYREG_BCAST(pi, TxResamplerMuDeltaInit0u, resamp_set[9]);
		}
	}

	if (ACMAJORREV_4(pi->pubpi->phy_rev))
		MOD_PHYREG(pi, AfeClkDivOverrideCtrl, afediv_sel_div_ovr, 0x1);
}

#define QT_2G_DEFAULT_28NM 2465
#define QT_5G_DEFAULT_28NM 5807
void
wlc_phy_tx_farrow_setup_28nm(phy_info_t *pi, uint16 dac_rate_mode)
{
	uint16 bits_in_mu = 23;
	uint16 fc;
	uint16 dac_div_idx, bw_idx, bw;
	uint16 dac_div_ratio;
	// lut entries [2G20 2G40 2G80 2G160 5G20 5G40 5G80 5G160]
	uint8 p_afediv_lut[] = {96, 48, 32, 192, 96, 64};

	uint32 dac_resamp_fcw, numerator_hi, numerator_lo;
	uint64 numerator;
	uint16 mu_delta_l, mu_delta_u;
	uint16 regval;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	bw_idx = CHSPEC_BW_LE20(pi->radio_chanspec)? 0 : (CHSPEC_IS40(pi->radio_chanspec)? 1 : 2);
	bw = 10*(1<<(bw_idx+1));

	if (ISSIM_ENAB(pi->sh->sih)) {
		/* 4347A0 QT AFE CLK MODEL */
		fc = (CHSPEC_IS2G(pi->radio_chanspec)) ? QT_2G_DEFAULT_28NM : QT_5G_DEFAULT_28NM;
	} else {
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			fc = CHAN2G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));
		} else {
			fc = CHAN5G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));
		}
	}
	if (dac_rate_mode == 2) {
		dac_div_ratio = CHSPEC_IS2G(pi->radio_chanspec) ? 128 : 256;
		if (CHSPEC_IS40(pi->radio_chanspec)) dac_div_ratio >>= 1;
	}
	else {
		dac_div_idx = CHSPEC_IS2G(pi->radio_chanspec) ? bw_idx : bw_idx + 3;
		dac_div_ratio = p_afediv_lut[dac_div_idx];
	}

	numerator =
		(((uint64)(bw << bits_in_mu) * dac_div_ratio));
	numerator_hi = (uint32)(numerator >> 32);
	numerator_lo = (uint32)(numerator & 0xFFFFFFFF);
	bcm_uint64_divide(&dac_resamp_fcw, numerator_hi, numerator_lo, (uint32)fc);

	mu_delta_l = (uint16)(dac_resamp_fcw & 0xFFFF);
	mu_delta_u = (uint16)((dac_resamp_fcw >> 16) & 0xFF);

	ACPHYREG_BCAST(pi, TxResamplerMuDelta0l, mu_delta_l);
	ACPHYREG_BCAST(pi, TxResamplerMuDelta0u, mu_delta_u);
	ACPHYREG_BCAST(pi, TxResamplerMuDeltaInit0l, mu_delta_l);
	ACPHYREG_BCAST(pi, TxResamplerMuDeltaInit0u, mu_delta_u);

	PHY_INFORM(("wl%d %s: band=%d fc=%d fcw=0x%x%x\n", PI_INSTANCE(pi), __FUNCTION__,
		(CHSPEC_IS2G(pi->radio_chanspec))?2:5, fc, mu_delta_u, mu_delta_l));

	/* Enable the Tx resampler on all cores */
	regval = READ_PHYREG(pi, TxResamplerEnable0);
	regval |= (1 < ACPHY_TxResamplerEnable0_enable_tx_SHIFT(pi->pubpi->phy_rev));
	ACPHYREG_BCAST(pi, TxResamplerEnable0,  regval);
}


void
wlc_phy_rx_farrow_setup_28nm(phy_info_t *pi)
{
	uint16 fc, afe_div_ratio, fcw_hi, fcw_lo;
	uint16 drift_period, bw_idx, bw;
	uint16 afe_div_idx;
	uint32 fcw;
	uint32 numerator_hi, numerator_lo;
	uint64 numerator;
	uint8 p_afediv_lut[] = {96, 48, 32, 192, 96, 64};

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	bw_idx = CHSPEC_BW_LE20(pi->radio_chanspec)? 0 : (CHSPEC_IS40(pi->radio_chanspec)? 1 : 2);
	bw = 10*(1<<(bw_idx+1));

	if (ISSIM_ENAB(pi->sh->sih)) {
		/* 4347A0 QT AFE CLK MODEL */
		fc = (CHSPEC_IS2G(pi->radio_chanspec)) ? QT_2G_DEFAULT_28NM : QT_5G_DEFAULT_28NM;
	} else {
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			fc = CHAN2G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));
		} else {
			fc = CHAN5G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));
		}
	}

	afe_div_idx = CHSPEC_IS2G(pi->radio_chanspec) ? bw_idx : bw_idx + 3;
	afe_div_ratio = p_afediv_lut[afe_div_idx];

	drift_period = afe_div_ratio * bw;
	numerator = (uint64)(fc) << 24;
	numerator_hi = (uint32)(numerator >> 32);
	numerator_lo = (uint32)(numerator & 0xFFFFFFFF);
	bcm_uint64_divide(&fcw, numerator_hi, numerator_lo, (uint32)drift_period);

	fcw -= (1 << 24);
	fcw_lo = (uint16)(fcw & 0xFFFF);
	fcw_hi = (uint16)((fcw >> 16) & 0xFFFF);

	ACPHYREG_BCAST(pi, rxFarrowDeltaPhase_lo, fcw_lo);
	ACPHYREG_BCAST(pi, rxFarrowDeltaPhase_hi, fcw_hi);
	WRITE_PHYREG(pi, rxFarrowDriftPeriod, drift_period);
	ACPHYREG_BCAST(pi, rxFarrowCtrl, 0xb3);

	PHY_INFORM(("wl%d %s: band=%d fc=%d fcw=0x%x%x\n", PI_INSTANCE(pi), __FUNCTION__,
		(CHSPEC_IS2G(pi->radio_chanspec))?2:5, fc, fcw_hi, fcw_lo));

	ACPHYREG_BCAST(pi, lbFarrowDeltaPhase_lo, fcw_lo);
	ACPHYREG_BCAST(pi, lbFarrowDeltaPhase_hi, fcw_hi);
	WRITE_PHYREG(pi, lbFarrowDriftPeriod, drift_period);
	ACPHYREG_BCAST(pi, lbFarrowCtrl, 0xb3);
}

void
wlc_phy_farrow_setup_28nm(phy_info_t *pi, uint16 dac_rate_mode)
{
	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	wlc_phy_rx_farrow_setup_28nm(pi);
	wlc_phy_tx_farrow_setup_28nm(pi, dac_rate_mode);

	/* Enabling the stall since stalls are disabled by default in 28nm Chips */
	ACPHY_ENABLE_STALL(pi, 0);

}

/* proc tx_farrow_setup_28nm_ulp */
void
wlc_phy_tx_farrow_setup_28nm_ulp(phy_info_t *pi, uint16 ulp_tx_mode)
{
	uint16 bits_in_mu = 23;
	uint16 fc, fi = 160;
	uint16 idx = (ulp_tx_mode - 1);
	uint16 dac_div_ratio;
	uint8* p_afediv_lut;
	uint8 afediv_lut_for_fc_ge_5745[] = {16, 12, 16, 12, 16};
	uint8 afediv_lut_for_fc_ge_5400[] = {16, 12, 16, 12, 14};
	uint8 afediv_lut_for_fc_ge_5180[] = {16, 12, 16, 12, 14};
	uint8 afediv_lut_for_fc_lt_5180[] = {14, 10, 14, 10, 13};
	uint32 dac_resamp_fcw, numerator_hi, numerator_lo;
	uint64 numerator;
	uint16 mu_delta_l, mu_delta_u;

	if (CHSPEC_CHANNEL(pi->radio_chanspec) > 14) {
		fc = CHAN5G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));
	} else {
		fc = CHAN2G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));
	}

	if (fc >= 5745) {
		p_afediv_lut = afediv_lut_for_fc_ge_5745;
	} else if (fc >= 5400) {
		p_afediv_lut = afediv_lut_for_fc_ge_5400;
	} else if (fc >= 5180) {
		p_afediv_lut = afediv_lut_for_fc_ge_5180;
	} else {
		p_afediv_lut = afediv_lut_for_fc_lt_5180;
	}

	dac_div_ratio = p_afediv_lut[idx];

	if (fc > 3000) {
		dac_div_ratio <<= 1;
	}

	numerator =
		(((uint64)(fi << bits_in_mu) * dac_div_ratio) + (fc >> 1));
	numerator_hi = (uint32)(numerator >> 32);
	numerator_lo = (uint32)(numerator & 0xFFFFFFFF);
	bcm_uint64_divide(&dac_resamp_fcw, numerator_hi, numerator_lo, (uint32)fc);

	mu_delta_l = (uint16)(dac_resamp_fcw & 0xFFFF);
	mu_delta_u = (uint16)((dac_resamp_fcw >> 16) & 0xFF);

	WRITE_PHYREG(pi, TxResamplerMuDelta0l, mu_delta_l);
	WRITE_PHYREG(pi, TxResamplerMuDelta0u, mu_delta_u);
	WRITE_PHYREG(pi, TxResamplerMuDeltaInit0l, mu_delta_l);
	WRITE_PHYREG(pi, TxResamplerMuDeltaInit0u, mu_delta_u);
	MOD_PHYREG(pi, TxResamplerEnable0, enable_tx, 1);
}

/* proc rx_farrow_setup_28nm_ulp */
void
wlc_phy_rx_farrow_setup_28nm_ulp(phy_info_t *pi, uint16 ulp_adc_mode)
{
	uint16 fc, afe_div_ratio, fcw_hi, fcw_lo;
	uint16 drift_period, bw;
	uint32 fcw;
	uint32 numerator_hi, numerator_lo;
	uint64 numerator;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	bw = CHSPEC_BW_LE20(pi->radio_chanspec) ? PHYBW_20: CHSPEC_IS40(pi->radio_chanspec)
			? PHYBW_40 : PHYBW_80;

	if (CHSPEC_CHANNEL(pi->radio_chanspec) > 14)
		fc = CHAN5G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));
	else
		fc = CHAN2G_FREQ(CHSPEC_CHANNEL(pi->radio_chanspec));

	if (fc < 3000) {
		if (ulp_adc_mode) {
			afe_div_ratio = 48;
		} else {
			afe_div_ratio = 56;
		}
	} else {
		if (ulp_adc_mode) {
			afe_div_ratio = 96;
			if (fc >= 5180) {
				afe_div_ratio = 104;
			}
		} else {
			afe_div_ratio = 112;
			if (fc >= 5180) {
				afe_div_ratio = 128;
			}
		}
	}

	drift_period = afe_div_ratio * bw * 2;
	numerator = (uint64)(fc) << 24;
	numerator_hi = (uint32)(numerator >> 32);
	numerator_lo = (uint32)(numerator & 0xFFFFFFFF);
	bcm_uint64_divide(&fcw, numerator_hi, numerator_lo, (uint32)drift_period);

	fcw -= (1 << 24);
	fcw_lo = (uint16)(fcw & 0xFFFF);
	fcw_hi = (uint16)((fcw >> 16) & 0xFFFF);

	WRITE_PHYREG(pi, rxFarrowDeltaPhase_lo, fcw_lo);
	WRITE_PHYREG(pi, rxFarrowDeltaPhase_hi, fcw_hi);
	WRITE_PHYREG(pi, rxFarrowDriftPeriod, drift_period);
	WRITE_PHYREG(pi, rxFarrowCtrl, 0xbb);

	WRITE_PHYREG(pi, lbFarrowDeltaPhase_lo, fcw_lo);
	WRITE_PHYREG(pi, lbFarrowDeltaPhase_hi, fcw_hi);
	WRITE_PHYREG(pi, lbFarrowDriftPeriod, drift_period);
	WRITE_PHYREG(pi, lbFarrowCtrl, 0xbb);
}

void
wlc_phy_farrow_setup_28nm_ulp(phy_info_t *pi, uint16 ulp_tx_mode,
	uint16 ulp_adc_mode)
{
	wlc_phy_rx_farrow_setup_28nm_ulp(pi, ulp_adc_mode);
	wlc_phy_tx_farrow_setup_28nm_ulp(pi, ulp_tx_mode);

	/* Enabling the stall since stalls are disabled by default in 28nm Chips */
	ACPHY_ENABLE_STALL(pi, 0);
}

void
wlc_phy_farrow_setup_20694(phy_info_t *pi, uint16 ulp_tx_mode,
	uint16 ulp_adc_mode)
{
	BCM_REFERENCE(pi);
	BCM_REFERENCE(ulp_tx_mode);
	BCM_REFERENCE(ulp_adc_mode);
	/* TODO: Add 20694 specific code to be added */
}

void
wlc_phy_enable_lna_dcc_comp_20691(phy_info_t *pi, bool on)
{
	uint16 sparereg = READ_PHYREG(pi, SpareReg);

	if (on)
		sparereg &= 0xfffe;
	else
		sparereg |= 0x0001;

	WRITE_PHYREG(pi, SpareReg, sparereg);
}

void
wlc_phy_set_lowpwr_phy_reg_rev3(phy_info_t *pi)
{
	ACPHY_REG_LIST_START
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_en_alc, 0x0)
		MOD_PHYREG_ENTRY(pi, radio_rxrf_lna5g, lna5g_lna1_bias_idac, 0x8)
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet4, vco_tempco_dcadj_1p2, 0x9)
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet2, vco_vctrl_buf_ical, 0x3)
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet4, vco_ib_bias_opamp, 0x6)
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet4, vco_ib_bias_opamp_fastsettle, 0xf)
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_bypass_vctrl_buf, 0x0)
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet3, vco_HDRM_CAL, 0x2)
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet2, vco_ICAL, 0x16)
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet3, vco_ICAL_1p2, 0xc)
		MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_USE_2p5V, 0x1)
	ACPHY_REG_LIST_EXECUTE(pi);
	if (ACMAJORREV_2(pi->pubpi->phy_rev) && ACMINORREV_1(pi)) {
		MOD_PHYREG(pi, radio_logen2gN5g, idac_mix, 0x4);
	}
}

void
wlc_phy_set_lowpwr_phy_reg(phy_info_t *pi)
{
	/* These guys not required for tiny based phys */
	if (!TINY_RADIO(pi)) {
		ACPHY_REG_LIST_START
			MOD_PHYREG_ENTRY(pi, radio_logen2g, idac_gm, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen2g, idac_gm_2nd, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen2g, idac_qb, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen2g, idac_qb_2nd, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen2g, idac_qtx, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_logen2gN5g, idac_itx, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_logen2gN5g, idac_qrx, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_logen2gN5g, idac_irx, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_logen2gN5g, idac_buf, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen2gN5g, idac_mix, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5g, idac_div, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5g, idac_vcob, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5gbufs, idac_bufb, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5g, idac_mixb, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5g, idac_load, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5gbufs, idac_buf2, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5gbufs, idac_bufb2, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5gbufs, idac_buf1, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5gbufs, idac_bufb1, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_logen5gQI, idac_qtx, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_logen5gQI, idac_itx, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_logen5gQI, idac_qrx, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_logen5gQI, idac_irx, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcocal, vcocal_rstn, 0x1)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcocal, vcocal_force_caps, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcocal, vcocal_force_caps_val, 0x40)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_ALC_ref_ctrl, 0xd)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_bias_mode, 0x1)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_cvar_extra, 0xb)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_cvar, 0xf)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_en_alc, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet2, vco_tempco_dcadj, 0xe)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet2, vco_tempco, 0xb)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet3, vco_cal_en, 0x1)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet3, vco_cal_en_empco, 0x1)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet3, vco_cap_mode, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet4, vco_ib_ctrl, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet3, vco_por, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_pll_lf_r1, lf_r1, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_pll_lf_r2r3, lf_r2, 0xc)
			MOD_PHYREG_ENTRY(pi, radio_pll_lf_r2r3, lf_r3, 0xc)
			MOD_PHYREG_ENTRY(pi, radio_pll_lf_cm, lf_rs_cm, 0xff)
			MOD_PHYREG_ENTRY(pi, radio_pll_lf_cm, lf_rf_cm, 0xc)
			MOD_PHYREG_ENTRY(pi, radio_pll_lf_cSet1, lf_c1, 0x99)
			MOD_PHYREG_ENTRY(pi, radio_pll_lf_cSet1, lf_c2, 0x8b)
			MOD_PHYREG_ENTRY(pi, radio_pll_lf_cSet2, lf_c3, 0x8b)
			MOD_PHYREG_ENTRY(pi, radio_pll_lf_cSet2, lf_c4, 0x8f)
			MOD_PHYREG_ENTRY(pi, radio_pll_cp, cp_kpd_scale, 0x34)
			MOD_PHYREG_ENTRY(pi, radio_pll_cp, cp_ioff, 0x60)
			MOD_PHYREG_ENTRY(pi, radio_ldo, ldo_1p2_xtalldo1p2_lowquiescenten, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_ldo, ldo_2p5_lowpwren_VCO, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_ldo, ldo_2p5_lowquiescenten_VCO_aux, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_ldo, ldo_2p5_lowpwren_VCO_aux, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_ldo, ldo_2p5_lowquiescenten_CP, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_ldo, ldo_2p5_lowquiescenten_VCO, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_lna2g, lna2g_lna1_bias_idac, 0x2)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_lna2g, lna2g_lna2_aux_bias_idac, 0x8)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_lna2g, lna2g_lna2_main_bias_idac, 0x8)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_lna5g, lna5g_lna1_bias_idac, 0x8)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_lna5g, lna5g_lna2_aux_bias_idac, 0x7)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_lna5g, lna5g_lna2_main_bias_idac, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_rxmix, rxmix2g_aux_bias_idac, 0x8)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_rxmix, rxmix2g_main_bias_idac, 0x8)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_rxmix, rxmix5g_gm_aux_bias_idac_i, 0x8)
			MOD_PHYREG_ENTRY(pi, radio_rxrf_rxmix, rxmix5g_gm_main_bias_idac_i, 0x8)
			MOD_PHYREG_ENTRY(pi, radio_rxbb_tia, tia_DC_Ib1, 0x6)
			MOD_PHYREG_ENTRY(pi, radio_rxbb_tia, tia_DC_Ib2, 0x6)
			MOD_PHYREG_ENTRY(pi, radio_rxbb_tia, tia_Ib_I, 0x6)
			MOD_PHYREG_ENTRY(pi, radio_rxbb_tia, tia_Ib_Q, 0x6)
			MOD_PHYREG_ENTRY(pi, radio_rxbb_bias12, lpf_bias_level1, 0x4)
			MOD_PHYREG_ENTRY(pi, radio_rxbb_bias12, lpf_bias_level2, 0x8)
			MOD_PHYREG_ENTRY(pi, radio_rxbb_bias34, lpf_bias_level3, 0x10)
			MOD_PHYREG_ENTRY(pi, radio_rxbb_bias34, lpf_bias_level4, 0x20)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet4, vco_tempco_dcadj_1p2, 0x9)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet2, vco_vctrl_buf_ical, 0x3)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet4, vco_ib_bias_opamp, 0x6)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet4, vco_ib_bias_opamp_fastsettle, 0xf)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_bypass_vctrl_buf, 0x0)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet3, vco_HDRM_CAL, 0x2)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet2, vco_ICAL, 0x16)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet3, vco_ICAL_1p2, 0xc)
			MOD_PHYREG_ENTRY(pi, radio_pll_vcoSet1, vco_USE_2p5V, 0x1)
		ACPHY_REG_LIST_EXECUTE(pi);
	}
}

/** Tx implicit beamforming. Ingress and outgress channels are assumed to have reprocity. */
void
wlc_phy_populate_recipcoeffs_acphy(phy_info_t *pi)
{
	uint16 start_words[][3] = {
		{0x005B, 0x0000, 0x0000},
		{0x8250, 0x0000, 0x0000},
		{0xC338, 0x0000, 0x0000},
		{0x4527, 0x0001, 0x0000},
		{0xA6A1, 0x0001, 0x0000},
		{0x081B, 0x0002, 0x0000},
		{0x8A18, 0x0002, 0x0000},
		{0x2C96, 0x0003, 0x0000},
		{0x8E17, 0x0003, 0x0000},
		{0x101B, 0x0004, 0x0000},
		{0x0020, 0x0000, 0x0000},
		{0x0020, 0x0000, 0x0000}};

	uint16 packed_word[6];
	uint16 zero_word[3] = {0, 0, 0};

	uint16 ang_tmp = 0, ang_tmp1 = 0;
	uint16 subband_idx, k;
	uint16 theta[2];
	uint32 packed;
	uint16 nwords_start = 12, nwords_pad = 4, nwords_recip;
	uint8  stall_val;

	bool is_caled = wlc_phy_is_txbfcal((wlc_phy_t *)pi);

	if (pi->sh->hw_phytxchain <= 1 || !(is_caled)) {
		return;
	}

	/* 1. obtain angles from SROM */
	subband_idx = phy_ac_chanmgr_get_chan_freq_range(pi, 0, PRIMARY_FREQ_SEGMENT);
	switch (subband_idx) {
	case WL_CHAN_FREQ_RANGE_2G:
#ifdef WLTXBF_2G_DISABLED
		ang_tmp = 0; ang_tmp1 = 0;
#else
		ang_tmp = pi->sromi->rpcal2g;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			ang_tmp1 = pi->sromi->rpcal2gcore3;
		}
#endif /* WLTXBF_2G_DISABLED */
		break;
	case WL_CHAN_FREQ_RANGE_5G_BAND0:
		ang_tmp = pi->sromi->rpcal5gb0;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			ang_tmp1 = pi->sromi->rpcal5gb0core3;
		}
		break;
	case WL_CHAN_FREQ_RANGE_5G_BAND1:
		ang_tmp = pi->sromi->rpcal5gb1;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			ang_tmp1 = pi->sromi->rpcal5gb1core3;
		}
		break;
	case WL_CHAN_FREQ_RANGE_5G_BAND2:
		ang_tmp = pi->sromi->rpcal5gb2;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			ang_tmp1 = pi->sromi->rpcal5gb2core3;
		}
		break;
	case WL_CHAN_FREQ_RANGE_5G_BAND3:
		ang_tmp = pi->sromi->rpcal5gb3;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			ang_tmp1 = pi->sromi->rpcal5gb3core3;
		}
		break;
	default:
		ang_tmp = pi->sromi->rpcal2g;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			ang_tmp1 = pi->sromi->rpcal2gcore3;
		}
		break;
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		ang_tmp1 = ((ang_tmp >> 8) & 0xff) + ((ang_tmp1 & 0x00ff) << 8);
		ang_tmp  = (ang_tmp & 0xff) << 8;
	}

	/* 2. generate packed word */
	if (ACMAJORREV_0(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {

		phy_utils_angle_to_phasor_lut(ang_tmp, packed_word);

		/* printf("reciprocity packed_word: %x%x%x\n",
		packed_word[2], packed_word[1], packed_word[0]);
		*/

	} else if ((ACMAJORREV_2(pi->pubpi->phy_rev)) || (ACMAJORREV_4(pi->pubpi->phy_rev))) {

		theta[0] = (uint8) (ang_tmp & 0xFF);
		theta[1] = (uint8) ((ang_tmp >> 8) & 0xFF);
		/* printf("---- theta1 = %d, theta2 = %d\n", theta[0], theta[1]); */

		/* every 4 tones are packed into 1 word */
		packed = (theta[0] | (theta[0] << 8) | (theta[0] << 16) | (theta[0] << 24));

		/* printf("reciprocity packedWideWord: %x\n", packed); */
	} else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	           ACMAJORREV_33(pi->pubpi->phy_rev) ||
	           ACMAJORREV_37(pi->pubpi->phy_rev)) {
		if (!PHY_AS_80P80(pi, pi->radio_chanspec)) {
			phy_utils_angle_to_phasor_lut(ang_tmp, &(packed_word[0]));
			phy_utils_angle_to_phasor_lut(ang_tmp1, &(packed_word[3]));
		}
	}

	/* Disable stalls and hold FIFOs in reset */
	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);

	/* 3. write to table */
	/* 4360 and 43602 */
	if (ACMAJORREV_0(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		nwords_recip = 64 + 128 + 256;

		for (k = 0; k < nwords_start; k++) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFECONFIG,
			1, k, 48, start_words[k]);
		}

		for (k = 0; k < nwords_recip; k++) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFECONFIG,
			1, nwords_start + k, 48, packed_word);
		}

		for (k = 0; k < nwords_pad; k++) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFECONFIG,
			1, nwords_start + nwords_recip + k, 48, zero_word);
		}
	} else if ((ACMAJORREV_2(pi->pubpi->phy_rev)) || (ACMAJORREV_4(pi->pubpi->phy_rev))) {

		/* 4 tones are packed into one word */
		nwords_recip = (256 >> 2);

		for (k = 0; k < nwords_recip; k++) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFECONFIG2X2TBL,
			1, k, 32, &packed);
		}
	} else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	           ACMAJORREV_33(pi->pubpi->phy_rev) ||
	           ACMAJORREV_37(pi->pubpi->phy_rev)) {

		/* total 512 words, and partitioned into 4 mem banks */
		nwords_recip = (512 >> 2);

		for (k = 0; k < nwords_recip; k++) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFECONFIG,
			1, k*2, 48, &(packed_word[0]));
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFECONFIG,
			1, k*2 + 1, 48, &(packed_word[3]));
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFECONFIG,
			1, k*2 + (nwords_recip >> 1), 48, &(packed_word[0]));
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFECONFIG,
			1, k*2 + (nwords_recip >> 1) + 1, 48, &(packed_word[3]));
		}
	}

	ACPHY_ENABLE_STALL(pi, stall_val);
}

/* get the two 80p80 complex freq. chanspec must be 80p80 or 160 */
void
phy_ac_chanmgr_get_chan_freq_range_80p80(phy_info_t *pi, chanspec_t chanspec, uint8 *freq)
{
	uint8 chans[NUM_CHANS_IN_CHAN_BONDING];

	if (CHSPEC_CHANNEL(chanspec) == 0) {
		chanspec = pi->radio_chanspec;
	}

	ASSERT(PHY_AS_80P80(pi, chanspec) || CHSPEC_IS160(chanspec));

	wf_chspec_get_80p80_channels(chanspec, chans);
	PHY_TRACE(("wl%d: %s: chan1=%d chan2=%d\n", pi->sh->unit,
			__FUNCTION__, chans[0], chans[1]));

	freq[0] = phy_ac_chanmgr_chan2freq_range(pi, chanspec, chans[0]);
	freq[1] = phy_ac_chanmgr_chan2freq_range(pi, chanspec, chans[1]);

}

/* get the complex freq. if chan==0, use default radio channel */
uint8
phy_ac_chanmgr_get_chan_freq_range(phy_info_t *pi, chanspec_t chanspec, uint8 core_segment_mapping)
{
	uint8 channel = CHSPEC_CHANNEL(chanspec);

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

		if (channel == 0) {
			channel = CHSPEC_CHANNEL(pi->radio_chanspec);
		}

		if (PHY_AS_80P80(pi, chanspec)) {
			PHY_INFORM(("wl%d: FIXME %s\n", pi->sh->unit, __FUNCTION__));
			channel -= 8;
		}
	} else {
		if (phy_get_phymode(pi) != PHYMODE_80P80) {
			if (channel == 0)
				channel = CHSPEC_CHANNEL(pi->radio_chanspec);
		} else {
			if (channel == 0)
				chanspec = pi->radio_chanspec;

			if (CHSPEC_BW(chanspec) == WL_CHANSPEC_BW_8080) {
				if (PRIMARY_FREQ_SEGMENT == core_segment_mapping)
					channel = wf_chspec_primary80_channel(chanspec);

				if (SECONDARY_FREQ_SEGMENT == core_segment_mapping)
					channel = wf_chspec_secondary80_channel(chanspec);
			} else {
				channel = CHSPEC_CHANNEL(chanspec);
			}
		}
	}

	return phy_ac_chanmgr_chan2freq_range(pi, chanspec, channel);

}


/* Internal function to get frequency for given channel
 * if channel==0, use default channel
 */
static int
phy_ac_chanmgr_chan2freq(phy_info_t *pi, uint8 channel)
{
	int freq;

	if (channel == 0) {
		channel = CHSPEC_CHANNEL(pi->radio_chanspec);
	}

	if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID)) {
		const void *chan_info;
		freq = wlc_phy_chan2freq_acphy(pi, channel, &chan_info);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) {
		const chan_info_radio20691_t *chan_info;
		freq = wlc_phy_chan2freq_20691(pi, channel, &chan_info);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
		const chan_info_radio20693_pll_t *chan_info_pll;
		const chan_info_radio20693_rffe_t *chan_info_rffe;
		const chan_info_radio20693_pll_wave2_t *chan_info_pll_wave2;
		freq = wlc_phy_chan2freq_20693(pi, channel, &chan_info_pll, &chan_info_rffe,
			&chan_info_pll_wave2);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20694_ID)) {
		const chan_info_radio20694_rffe_t *chan_info;
		freq = wlc_phy_chan2freq_20694(pi, channel, &chan_info);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20695_ID)) {
		const chan_info_radio20695_rffe_t *chan_info;
		freq = wlc_phy_chan2freq_20695(pi, channel, &chan_info);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20696_ID)) {
		const chan_info_radio20696_rffe_t *chan_info;
		freq = wlc_phy_chan2freq_20696(pi, channel, &chan_info);
	} else {
		ASSERT(0);
		freq = -1;
	}

	return freq;
}


/* Get the subband index (WL_CHAN_FREQ_RANGE_[25]G_*) for the given channel
 * according to the SROMREV<12 definitions, i.e. 4 subbands in 5G
 * if chan==0, use default radio channel
 */
uint8
phy_ac_chanmgr_chan2freq_range(phy_info_t *pi, chanspec_t chanspec, uint8 channel)
{
	int freq = phy_ac_chanmgr_chan2freq(pi, channel);

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	if (channel <= CH_MAX_2G_CHANNEL || freq < 0) {
		return WL_CHAN_FREQ_RANGE_2G;
	} else if ((pi->sromi->subband5Gver == PHY_MAXNUM_5GSUBBANDS) ||
	           (pi->sromi->subband5Gver == PHY_SUBBAND_4BAND)) {
		if ((freq >= PHY_SUBBAND_4BAND_BAND0) &&
		    (freq < PHY_SUBBAND_4BAND_BAND1))
			return WL_CHAN_FREQ_RANGE_5G_BAND0;
		else if ((freq >= PHY_SUBBAND_4BAND_BAND1) &&
			(freq < PHY_SUBBAND_4BAND_BAND2))
			return WL_CHAN_FREQ_RANGE_5G_BAND1;
		else if ((freq >= PHY_SUBBAND_4BAND_BAND2) &&
			(freq < PHY_SUBBAND_4BAND_BAND3))
			return WL_CHAN_FREQ_RANGE_5G_BAND2;
		else
			return WL_CHAN_FREQ_RANGE_5G_BAND3;
	} else if (pi->sromi->subband5Gver == PHY_SUBBAND_3BAND_EMBDDED) {
		if ((freq >= EMBEDDED_LOW_5G_CHAN) && (freq < EMBEDDED_MID_5G_CHAN)) {
			return WL_CHAN_FREQ_RANGE_5GL;
		} else if ((freq >= EMBEDDED_MID_5G_CHAN) &&
		           (freq < EMBEDDED_HIGH_5G_CHAN)) {
			return WL_CHAN_FREQ_RANGE_5GM;
		} else {
			return WL_CHAN_FREQ_RANGE_5GH;
		}
	} else if (pi->sromi->subband5Gver == PHY_SUBBAND_3BAND_HIGHPWR) {
		if ((freq >= HIGHPWR_LOW_5G_CHAN) && (freq < HIGHPWR_MID_5G_CHAN)) {
			return WL_CHAN_FREQ_RANGE_5GL;
		} else if ((freq >= HIGHPWR_MID_5G_CHAN) && (freq < HIGHPWR_HIGH_5G_CHAN)) {
			return WL_CHAN_FREQ_RANGE_5GM;
		} else {
			return WL_CHAN_FREQ_RANGE_5GH;
		}
	} else { /* Default PPR Subband subband5Gver = 7 */
		if ((freq >= JAPAN_LOW_5G_CHAN) && (freq < JAPAN_MID_5G_CHAN)) {
			return WL_CHAN_FREQ_RANGE_5GL;
		} else if ((freq >= JAPAN_MID_5G_CHAN) && (freq < JAPAN_HIGH_5G_CHAN)) {
			return WL_CHAN_FREQ_RANGE_5GM;
		} else {
			return WL_CHAN_FREQ_RANGE_5GH;
		}
	}
}

/* get the complex freq for 80p80 channels. if chan==0, use default radio channel */
void
phy_ac_chanmgr_get_chan_freq_range_80p80_srom12(phy_info_t *pi, chanspec_t chanspec, uint8 *freq)
{
	uint8 chans[NUM_CHANS_IN_CHAN_BONDING];

	ASSERT(SROMREV(pi->sh->sromrev) >= 12);

	if (CHSPEC_CHANNEL(chanspec) == 0) {
		chanspec = pi->radio_chanspec;
	}

	ASSERT(PHY_AS_80P80(pi, chanspec) || CHSPEC_IS160(chanspec));

	wf_chspec_get_80p80_channels(chanspec, chans);
	PHY_TRACE(("wl%d: %s: chan1=%d chan2=%d\n", pi->sh->unit,
			__FUNCTION__, chans[0], chans[1]));

	freq[0] = phy_ac_chanmgr_chan2freq_range_srom12(pi, chanspec, chans[0]);
	freq[1] = phy_ac_chanmgr_chan2freq_range_srom12(pi, chanspec, chans[1]);
}

/* get the complex freq. if chan==0, use default radio channel */
uint8
phy_ac_chanmgr_get_chan_freq_range_srom12(phy_info_t *pi, chanspec_t chanspec)
{
	uint8 channel = CHSPEC_CHANNEL(chanspec);
	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	ASSERT(SROMREV(pi->sh->sromrev) >= 12);

	if (channel == 0) {
		channel = CHSPEC_CHANNEL(pi->radio_chanspec);
	}

	return phy_ac_chanmgr_chan2freq_range_srom12(pi, chanspec, channel);
}

/* Get the subband index (WL_CHAN_FREQ_RANGE_[25]G_*) for the given channel
 * according to the SROMREV>=12 definitions, i.e. subband5gver=5 or 5 band in 5GHz
 * if chan==0, use default radio channel
 */
uint8
phy_ac_chanmgr_chan2freq_range_srom12(phy_info_t *pi, chanspec_t chanspec, uint8 channel)
{
	int freq = phy_ac_chanmgr_chan2freq(pi, channel);

	ASSERT(SROMREV(pi->sh->sromrev) >= 12);
	ASSERT(pi->sromi->subband5Gver == PHY_MAXNUM_5GSUBBANDS);

	if (channel <= CH_MAX_2G_CHANNEL || freq < PHY_MAXNUM_5GSUBBANDS_BAND0) {
		if (CHSPEC_IS40(chanspec))
			return WL_CHAN_FREQ_RANGE_2G_40;
		else
			return WL_CHAN_FREQ_RANGE_2G;
	} else if ((freq >= PHY_MAXNUM_5GSUBBANDS_BAND0) &&
	           (freq < PHY_MAXNUM_5GSUBBANDS_BAND1)) {
		if (CHSPEC_IS40(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND0_40;
		else if (CHSPEC_IS80(chanspec) || CHSPEC_IS160(chanspec) ||
		         CHSPEC_IS8080(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND0_80;
		else
			return WL_CHAN_FREQ_RANGE_5G_BAND0;
	} else if ((freq >= PHY_MAXNUM_5GSUBBANDS_BAND1) &&
	           (freq < PHY_MAXNUM_5GSUBBANDS_BAND2)) {
		if (CHSPEC_IS40(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND1_40;
		else if (CHSPEC_IS80(chanspec) || CHSPEC_IS160(chanspec) ||
		         CHSPEC_IS8080(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND1_80;
		else
			return WL_CHAN_FREQ_RANGE_5G_BAND1;
	} else if ((freq >= PHY_MAXNUM_5GSUBBANDS_BAND2) &&
	           (freq < PHY_MAXNUM_5GSUBBANDS_BAND3)) {
		if (CHSPEC_IS40(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND2_40;
		else if (CHSPEC_IS80(chanspec) || CHSPEC_IS160(chanspec) ||
		         CHSPEC_IS8080(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND2_80;
		else
			return WL_CHAN_FREQ_RANGE_5G_BAND2;
	} else if ((freq >= PHY_MAXNUM_5GSUBBANDS_BAND3) &&
	           (freq < PHY_MAXNUM_5GSUBBANDS_BAND4)) {
		if (CHSPEC_IS40(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND3_40;
		else if (CHSPEC_IS80(chanspec) || CHSPEC_IS160(chanspec) ||
		         CHSPEC_IS8080(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND3_80;
		else
			return WL_CHAN_FREQ_RANGE_5G_BAND3;
	} else {
		if (CHSPEC_IS40(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND4_40;
		else if (CHSPEC_IS80(chanspec))
			return WL_CHAN_FREQ_RANGE_5G_BAND4_80;
		else
			return WL_CHAN_FREQ_RANGE_5G_BAND4;
	}
}

static bool
phy_ac_chanmgr_is_txbfcal(phy_type_chanmgr_ctx_t *ctx)
{
	phy_ac_chanmgr_info_t *info = (phy_ac_chanmgr_info_t *)ctx;
	phy_info_t *pi = info->pi;
	uint8  subband_idx;
	uint8  chans[NUM_CHANS_IN_CHAN_BONDING];
	uint16 rpcal_val, rpcal_val1 = 0;
	bool   is_caled;

#ifdef MACOSX
	if (ACMAJORREV_0(pi->pubpi->phy_rev)|| ACMAJORREV_5(pi->pubpi->phy_rev))
		return FALSE;
#endif /* MACOSX */

	if (ACMAJORREV_33(pi->pubpi->phy_rev) && PHY_AS_80P80(pi, pi->radio_chanspec)) {
		phy_ac_chanmgr_get_chan_freq_range_80p80(pi, pi->radio_chanspec, chans);
		subband_idx = chans[0];
		PHY_INFORM(("wl%d: %s: FIXME for 80P80\n", pi->sh->unit, __FUNCTION__));
	} else {
		subband_idx = phy_ac_chanmgr_get_chan_freq_range(pi, 0, PRIMARY_FREQ_SEGMENT);
	}

	switch (subband_idx) {
	case WL_CHAN_FREQ_RANGE_2G:
		rpcal_val = pi->sromi->rpcal2g;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) ||
			ACMAJORREV_37(pi->pubpi->phy_rev)) {
			rpcal_val1 = pi->sromi->rpcal2gcore3;
		}
		break;
	case WL_CHAN_FREQ_RANGE_5G_BAND0:
		rpcal_val = pi->sromi->rpcal5gb0;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) ||
			ACMAJORREV_37(pi->pubpi->phy_rev)) {
			rpcal_val1 = pi->sromi->rpcal5gb0core3;
		}
		break;
	case WL_CHAN_FREQ_RANGE_5G_BAND1:
		rpcal_val = pi->sromi->rpcal5gb1;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) ||
			ACMAJORREV_37(pi->pubpi->phy_rev)) {
			rpcal_val1 = pi->sromi->rpcal5gb1core3;
		}
		break;
	case WL_CHAN_FREQ_RANGE_5G_BAND2:
		rpcal_val = pi->sromi->rpcal5gb2;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) ||
			ACMAJORREV_37(pi->pubpi->phy_rev)) {
			rpcal_val1 = pi->sromi->rpcal5gb2core3;
		}
		break;
	case WL_CHAN_FREQ_RANGE_5G_BAND3:
		rpcal_val = pi->sromi->rpcal5gb3;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) ||
			ACMAJORREV_37(pi->pubpi->phy_rev)) {
			rpcal_val1 = pi->sromi->rpcal5gb3core3;
		}
		break;
	default:
		PHY_ERROR(("wl%d: %s: Invalid chan_freq_range %d\n",
		           pi->sh->unit, __FUNCTION__, subband_idx));
		rpcal_val = pi->sromi->rpcal2g;
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) ||
			ACMAJORREV_37(pi->pubpi->phy_rev)) {
			rpcal_val1 = pi->sromi->rpcal2gcore3;
		}
		break;
	}

	is_caled = (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) ||
			ACMAJORREV_37(pi->pubpi->phy_rev)) ?
	        !(rpcal_val == 0 && rpcal_val1 == 0) : (rpcal_val != 0);

	return is_caled;
}

void
wlc_phy_smth(phy_info_t *pi, int8 enable_smth, int8 dump_mode)
{
#ifdef WL_PROXDETECT
	if (pi->u.pi_acphy->tof_smth_forced)
		return;
#endif

	if ((ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) ||
	    ACMAJORREV_5(pi->pubpi->phy_rev) || ACMAJORREV_4(pi->pubpi->phy_rev) ||
	    ACMAJORREV_3(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_36(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
		uint16 SmthReg0, SmthReg1;

		ACPHY_REG_LIST_START
			/* Set the SigB to the default values */
			MOD_PHYREG_ENTRY(pi, musigb2, mu_sigbmcs9, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb2, mu_sigbmcs8, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs7, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs6, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs5, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs4, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs3, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs2, 0x7)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs1, 0x3)
			MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs0, 0x2)
		ACPHY_REG_LIST_EXECUTE(pi);

		pi_ac->chanmgri->acphy_smth_dump_mode = SMTH_NODUMP;

		switch (enable_smth) {
		case SMTH_DISABLE:
			/* Disable Smoothing and Enable SigB */
			SmthReg0 = READ_PHYREG(pi, chnsmCtrl0) & 0xFFFE;
			SmthReg1 = READ_PHYREG(pi, chnsmCtrl1);
			break;
		case SMTH_ENABLE:
			/* Enable Smoothing With all modes ON */
			/* This is the default config in which Smth is enabled for */
			/* SISO pkts and HT TxBF case. Use SigB for VHT-TxBF */
			SmthReg0 = 0x33F;
			SmthReg1 = 0x2C0;
			if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
				SmthReg0 |=
					ACPHY_chnsmCtrl0_mte_pilot_enable_MASK(pi->pubpi->phy_rev);
			}
			if (ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			  SmthReg0 = 0x37f;
			  SmthReg1 = 0x5eC0;
			}
			pi_ac->chanmgri->acphy_smth_dump_mode = dump_mode;
			switch (dump_mode)
			{
			case SMTH_FREQDUMP:
			/* Enable Freq-domain dumping (Raw Channel Estimates) */
			SmthReg0 &= ~(
				ACPHY_chnsmCtrl0_nw_whiten_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_group_delay_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_mte_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_window_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_fft_enable_MASK(pi->pubpi->phy_rev));
			SmthReg1 &= ~(
				ACPHY_chnsmCtrl1_ifft_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl1_output_enable_MASK(pi->pubpi->phy_rev));
			break;

			case SMTH_FREQDUMP_AFTER_NW:
			/* Enable Freq-domain dumping (After NW Filtering) */
			SmthReg0 &= ~(
				ACPHY_chnsmCtrl0_group_delay_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_mte_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_window_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_fft_enable_MASK(pi->pubpi->phy_rev));
			SmthReg1 &= ~(
				ACPHY_chnsmCtrl1_ifft_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl1_output_enable_MASK(pi->pubpi->phy_rev));
			break;

			case SMTH_FREQDUMP_AFTER_GD:
			/* Enable Freq-domain dumping (After GD Compensation) */
			SmthReg0 &= ~(
				ACPHY_chnsmCtrl0_mte_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_window_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_fft_enable_MASK(pi->pubpi->phy_rev));
			SmthReg1 &= ~(
				ACPHY_chnsmCtrl1_ifft_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl1_output_enable_MASK(pi->pubpi->phy_rev));
			break;

			case SMTH_FREQDUMP_AFTER_MTE:
			/* Enable Freq-domain dumping (After MTE) */
			SmthReg0 &= ~(
				ACPHY_chnsmCtrl0_window_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_fft_enable_MASK(pi->pubpi->phy_rev));
			SmthReg1 &= ~(
				ACPHY_chnsmCtrl1_ifft_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl1_output_enable_MASK(pi->pubpi->phy_rev));
			break;

			case SMTH_TIMEDUMP_AFTER_IFFT:
			/* Enable Time-domain dumping (After IFFT) */
			SmthReg0 &= ~(
				ACPHY_chnsmCtrl0_window_enable_MASK(pi->pubpi->phy_rev) |
				ACPHY_chnsmCtrl0_fft_enable_MASK(pi->pubpi->phy_rev));
			SmthReg1 &= ~ACPHY_chnsmCtrl1_output_enable_MASK(pi->pubpi->phy_rev);
			break;

			case SMTH_TIMEDUMP_AFTER_WIN:
				/* Enable Time-domain dumping (After Windowing) */
			SmthReg0 &= ~ACPHY_chnsmCtrl0_fft_enable_MASK(pi->pubpi->phy_rev);
			SmthReg1 &= ~ACPHY_chnsmCtrl1_output_enable_MASK(pi->pubpi->phy_rev);
			break;

			case SMTH_FREQDUMP_AFTER_FFT:
			/* Enable Freq-domain dumping (After FFT) */
			SmthReg1 &= ~ACPHY_chnsmCtrl1_output_enable_MASK(pi->pubpi->phy_rev);
			break;
			}
			break;
		case SMTH_ENABLE_NO_NW:
			/* Enable Smoothing With all modes ON Except NW Filter */
			SmthReg0 = 0x337;
			SmthReg1 = 0x2C0;
			if (ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			  SmthReg0 = 0x377;
			  SmthReg1 = 0x5eC0;
			}
			break;
		case SMTH_ENABLE_NO_NW_GD:
			/* Enable Smoothing With all modes ON Except NW and GD  */
			SmthReg0 = 0x327;
			SmthReg1 = 0x2C0;
			if (ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			  SmthReg0 = 0x367;
			  SmthReg1 = 0x5eC0;
			}
			break;
		case SMTH_ENABLE_NO_NW_GD_MTE:
			/* Enable Smoothing With all modes ON Except NW, GD and  MTE */
			SmthReg0 = 0x307;
			SmthReg1 = 0x2C0;
			if (ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			  SmthReg0 = 0x307;
			  SmthReg1 = 0x5eC0;
			}
			break;
		case DISABLE_SIGB_AND_SMTH:
			/* Disable Smoothing and SigB */
			SmthReg0 = 0x33E;
			SmthReg1 = 0x0C0;
			if (ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			  SmthReg0 = 0x37E;
			  SmthReg1 = 0x0C0;
			}
			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, musigb2, mu_sigbmcs9, 0x0)
				MOD_PHYREG_ENTRY(pi, musigb2, mu_sigbmcs8, 0x0)
				MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs7, 0x0)
				MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs6, 0x0)
				MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs5, 0x0)
				MOD_PHYREG_ENTRY(pi, musigb1, mu_sigbmcs4, 0x0)
				MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs3, 0x0)
				MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs2, 0x0)
				MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs1, 0x0)
				MOD_PHYREG_ENTRY(pi, musigb0, mu_sigbmcs0, 0x0)
			ACPHY_REG_LIST_EXECUTE(pi);
			break;
		case SMTH_FOR_TXBF:
			/* Enable Smoothing for TxBF using Smth for HT and VHT */
			SmthReg0 = 0x33F;
			SmthReg1 = 0x6C0;
			if (ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			  SmthReg0 = 0x37F;
			  SmthReg1 = 0x5EC0;
			}
			break;
		default:
			PHY_ERROR(("wl%d: %s: Unrecognized smoothing mode: %d\n",
			          pi->sh->unit, __FUNCTION__, enable_smth));
			return;
		}
		WRITE_PHYREG(pi, chnsmCtrl0, SmthReg0);
		WRITE_PHYREG(pi, chnsmCtrl1, SmthReg1);
		pi_ac->chanmgri->acphy_enable_smth = enable_smth;

		if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			/* 4349 specific setting */
			if (enable_smth == SMTH_ENABLE) {
				/* output_enable_new = 0x0 no output
				 * output_enable_new = 0x1 only legacy channel is smoothed
				 * output_enable_new = 0x2 only HT/VHT channel is smoothed
				 * output_enable_new = 0x3 both legacy and HT/VHT are smoothed
				 */
				/* 0x2 since TXBF doesn't work if legacy smoothing is enabled */
				MOD_PHYREG(pi, chnsmCtrl1, output_enable_new, 0x2);
			} else {
			    MOD_PHYREG(pi, chnsmCtrl1, output_enable_new, 0x0);
			}
			if ((phy_get_phymode(pi) == PHYMODE_MIMO) && (pi->sh->phyrxchain == 0x3)) {
				MOD_PHYREG(pi, chnsmCtrl1, disable_2rx_nvar_calc, 0x0);
			} else {
				MOD_PHYREG(pi, chnsmCtrl1, disable_2rx_nvar_calc, 0x1);
			}
			MOD_PHYREG(pi, nvcfg3, unity_gain_for_2x2_coremask2, 0x1);
		} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			MOD_PHYREG(pi, chnsmCtrl0, mte_pilot_enable, 0x1);
			MOD_PHYREG(pi, chnsmCtrl1, ignore_VHT_txbf_bit, 0x1);
			if (enable_smth == SMTH_ENABLE) {
				/* output_enable_new = 0x0 no output
				 * output_enable_new = 0x1 only legacy channel is smoothed
				 * output_enable_new = 0x2 only HT/VHT channel is smoothed
				 * output_enable_new = 0x3 both legacy and HT/VHT are smoothed
				 */
				MOD_PHYREG(pi, chnsmCtrl1, output_enable_new, 0x3);
			} else {
				MOD_PHYREG(pi, chnsmCtrl1, output_enable_new, 0x0);
			}
		}

		/* set the Tiny specific filter slopes for channel smoothing */
		if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, chnsmCtrl5, filter_slope_20MHz, 0x2)
				MOD_PHYREG_ENTRY(pi, chnsmCtrl6, filter_slope_40MHz, 0x2)
				MOD_PHYREG_ENTRY(pi, chnsmCtrl6, filter_slope_80MHz, 0x1)
			ACPHY_REG_LIST_EXECUTE(pi);
		}

		if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
			MOD_PHYREG(pi, nvcfg3, unity_gain_for_2x2_coremask2, 0x1);
		}

	}
}

void
wlc_phy_ac_shared_ant_femctrl_master(phy_info_t *pi)
{
	bool band5g = CHSPEC_IS5G(pi->radio_chanspec);
	if (CHIPID(pi->sh->chip) == BCM4364_CHIP_ID) {
		  #define SLICE_3x3_MASTER_2G 0xf8
		  #define SLICE_3x3_MASTER_5G 0x307
		  if (ACMAJORREV_5(pi->pubpi->phy_rev)) {
		    /* make 3x3 the master */
		    if (band5g) {
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_09,
					(0x7 << 29), ((SLICE_3x3_MASTER_5G & 0x7) << 29));
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10,
					(0x7f << 0), (((SLICE_3x3_MASTER_5G >>3) & 0x7f) << 0));
		    } else {
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_09,
					(0x7 << 29), ((SLICE_3x3_MASTER_2G & 0x7) << 29));
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10,
					(0x7f << 0), (((SLICE_3x3_MASTER_2G >>3) & 0x7f) << 0));
		    }
		  } else if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
		    /* make 1x1 the master */
		    if (band5g) {
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_09,
					(0x7 << 29), ((SLICE_3x3_MASTER_2G & 0x7) << 29));
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10,
					(0x7f << 0), (((SLICE_3x3_MASTER_2G >>3) & 0x7f) << 0));
		    } else {
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_09,
					(0x7 << 29), ((SLICE_3x3_MASTER_5G & 0x7) << 29));
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10,
					(0x7f << 0), (((SLICE_3x3_MASTER_5G >>3) & 0x7f) << 0));
		    }
		  }
		if (phy_get_master(pi) == 0) {
			/* make 3x3 the master */
			si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_09,
				(0x7 << 29), (0x7 << 29));
			si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10,
				(0x7f << 0), (0x7f << 0));
		} else if (phy_get_master(pi) == 1) {
			si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_09,
				(0x7 << 29), (0 << 29));
			si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10,
				(0x7f << 0), (0 << 0));
		}
	} else {
		PHY_ERROR(("%s: Multi Slice femctrl not supported\n", __FUNCTION__));
	}
}

#if (defined(WL_SISOCHIP) || !defined(SWCTRL_TO_BT_IN_COEX))
static void
wlc_phy_ac_femctrl_mask_on_band_change(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	if (!ACMAJORREV_0(pi->pubpi->phy_rev)) {
		/* When WLAN is in 5G, WLAN table should control the FEM lines */
		/* and BT should not have any access permissions */
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			/* disable BT Fem control table accesses */
			MOD_PHYREG(pi, FemCtrl, enBtSignalsToFEMLut, 0x0);
			if (!ACPHY_FEMCTRL_ACTIVE(pi)) {
				if (ACMAJORREV_4(pi->pubpi->phy_rev) &&
					!BF3_RSDB_1x1_BOARD(pi_ac))  {
					if (phy_get_phymode(pi) == PHYMODE_MIMO) {
						/* writes to both cores */
						MOD_PHYREG(pi, FemCtrl, femCtrlMask, 0x3ff);
						/* now write to only core0 */
						wlapi_exclusive_reg_access_core0(
							pi->sh->physhim, 1);
						MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							pi_ac->sromi->femctrlmask_5g);
						wlapi_exclusive_reg_access_core0(
							pi->sh->physhim, 0);
					} else if (phy_get_phymode(pi) == PHYMODE_RSDB &&
						phy_get_current_core(pi) == PHY_RSBD_PI_IDX_CORE1) {
						MOD_PHYREG(pi, FemCtrl, femCtrlMask, 0x3ff);
					} else {
						MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							pi_ac->sromi->femctrlmask_5g);
					}
				} else {
					MOD_PHYREG(pi, FemCtrl, femCtrlMask,
						pi_ac->sromi->femctrlmask_5g);
				}
			} else {
				if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
					if (BFCTL(pi_ac) == 4) {
						if (BF3_FEMCTRL_SUB(pi_ac) == 1) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x23c);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 2) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x297);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 3) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x058);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 4) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x058);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 6) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0xe);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 7) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x2e);
						} else {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x3ff);
						}
					}
				} else if (ACMAJORREV_2(pi->pubpi->phy_rev) ||
				           ACMAJORREV_5(pi->pubpi->phy_rev)) {
					if (BFCTL(pi_ac) == 10) {
						if (BF3_FEMCTRL_SUB(pi_ac) == 0) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x317);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 1) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x347);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 2) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x303);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 3) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x307);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 4) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x309);
						} else if (BF3_FEMCTRL_SUB(pi_ac) == 5) {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x3c7);
						} else {
							MOD_PHYREG(pi, FemCtrl, femCtrlMask,
							           0x3ff);
						}
					} else if (pi->u.pi_acphy->sromi->femctrl == 2) {
					  if (pi->u.pi_acphy->sromi->femctrl_sub == 5)
						si_pmu_switch_off_PARLDO(pi->sh->sih, pi->sh->osh);
					  if (pi->u.pi_acphy->sromi->femctrl_sub == 6)
						si_pmu_switch_on_PARLDO(pi->sh->sih, pi->sh->osh);
					} else {
						MOD_PHYREG(pi, FemCtrl, femCtrlMask, 0x3ff);
					}
				} else if (TINY_RADIO(pi)) {
					MOD_PHYREG(pi, FemCtrl, femCtrlMask, 0x3ff);
				}
			}
			if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
				phy_utils_write_radioreg(pi, RF_2069_TXGM_LOFT_SCALE(0), 0x0);
				if (BF2_DAC_SPUR_IMPROVEMENT(pi_ac) == 1) {
					phy_utils_write_radioreg(pi, RFX_2069_ADC_CFG5, 0x83e3);
				}
			}
		} else { /* When WLAN is in 2G, BT controls should be allowed to go through */
			/* BT should also be able to control FEM Control Table */
			if ((!(CHIPID(pi->sh->chip) == BCM43602_CHIP_ID ||
				CHIPID(pi->sh->chip) == BCM43462_CHIP_ID)) &&
				BF_SROM11_BTCOEX(pi_ac)) {
				MOD_PHYREG(pi, FemCtrl, enBtSignalsToFEMLut, 0x1);
			}
			MOD_PHYREG(pi, FemCtrl, femCtrlMask, 0x3ff);

			if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
				uint8 DLNA_BTFLAG;
				DLNA_BTFLAG = (BOARDFLAGS(GENERIC_PHY_INFO(pi)->boardflags) &
					0x00400000) >> 22;
				MOD_PHYREG(pi, FemCtrl, femCtrlMask,
					pi_ac->sromi->femctrlmask_2g);
				if (DLNA_BTFLAG == 0) {
					MOD_PHYREG(pi, FemCtrl, enBtSignalsToFEMLut, 0x0);
				} else {
					if (BF3_RSDB_1x1_BOARD(pi_ac)) {
						MOD_PHYREG(pi, FemCtrl, enBtSignalsToFEMLut, 0x1);
					} else {
						if (phy_get_phymode(pi) == PHYMODE_MIMO) {
						/* writes to both cores */
						MOD_PHYREG(pi, FemCtrl, enBtSignalsToFEMLut, 0x0);
						wlapi_exclusive_reg_access_core0(
							pi->sh->physhim, 1);
						/* writes to only core0 */
						MOD_PHYREG(pi, FemCtrl, enBtSignalsToFEMLut, 0x1);
						wlapi_exclusive_reg_access_core0(
							pi->sh->physhim, 0);
						} else if (phy_get_phymode(pi) == PHYMODE_RSDB)  {
							if (phy_get_current_core(pi) == 0) {
								MOD_PHYREG(pi, FemCtrl,
									enBtSignalsToFEMLut, 0x1);
							} else {
								MOD_PHYREG(pi, FemCtrl,
									enBtSignalsToFEMLut, 0x0);
							}
						}
					}
				}
			}

			if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
				phy_utils_write_radioreg(pi, RF_2069_TXGM_LOFT_SCALE(0), 0xa);
				if (BF2_DAC_SPUR_IMPROVEMENT(pi_ac) == 1) {
					phy_utils_write_radioreg(pi, RFX_2069_ADC_CFG5, 0x83e0);
				}
			}
			if (ACMAJORREV_5(pi->pubpi->phy_rev) &&
				pi->u.pi_acphy->sromi->femctrl == 2) {
			    if (pi->u.pi_acphy->sromi->femctrl_sub == 5)
			        si_pmu_switch_on_PARLDO(pi->sh->sih, pi->sh->osh);
			    if (pi->u.pi_acphy->sromi->femctrl_sub == 6)
			        si_pmu_switch_off_PARLDO(pi->sh->sih, pi->sh->osh);
			}
		}
	}
}
#endif /* (defined(WL_SISOCHIP) || !defined(SWCTRL_TO_BT_IN_COEX)) */

void
phy_ac_chanmgr_disable_core2core_sync_setup(phy_info_t *pi)
{
	uint8 core;
	uint8 val =  0;
	uint8 stall_val;

	bool  suspend = !(R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC);

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);

	if (!suspend)
		wlapi_suspend_mac_and_wait(pi->sh->physhim);

	if (ACMAJORREV_32(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) {
		uint16 sparereg;
		sparereg = READ_PHYREG(pi, SpareReg);
		MOD_PHYREG(pi, SpareReg, spareReg, sparereg & 0xffbf);
	}

	FOREACH_CORE(pi, core) {
		MOD_PHYREGCE(pi, TxResamplerEnable, core, txfe_baseband_enfree, val);
		MOD_PHYREGCE(pi, TxResamplerEnable, core, txfetoptxfrreseten, val);
		MOD_PHYREGCE(pi, TxResamplerEnable, core, mufreeWren, val);
	}

	MOD_PHYREG(pi, dacClkCtrl, txcore2corefrclken, val);
	MOD_PHYREG(pi, dacClkCtrl, txcore2corefrdacclken, val);
	MOD_PHYREG(pi, dacClkCtrl, txfarrowresetfreeen, val);
	MOD_PHYREG(pi, dacClkCtrl, gateafeclocksoveren, val);
	if (ACMAJORREV_33(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, dacClkCtrl, dacpuoren, val);
		MOD_PHYREG(pi, dacClkCtrl, txframeoreden, val);
		MOD_PHYREG(pi, dacClkCtrl, endacratiochgvld, 0);
	}

	ACPHY_DISABLE_STALL(pi);
	wlc_phy_resetcca_acphy(pi);
	ACPHY_ENABLE_STALL(pi, stall_val);
	if (!suspend)
		wlapi_enable_mac(pi->sh->physhim);

}

void
phy_ac_chanmgr_enable_core2core_sync_setup(phy_info_t *pi)
{
	uint8 core, val = 1;
	uint16 sparereg;
	phy_ac_chanmgr_info_t *chanmgri = pi->u.pi_acphy->chanmgri;

	if (!((ACMAJORREV_32(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) ||
		ACMAJORREV_33(pi->pubpi->phy_rev))) {
		return;
	}

	FOREACH_CORE(pi, core) {
		MOD_PHYREGCE(pi, RfctrlCoreAfeCfg1, core, afe_iqdac_pwrup, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqdac_pwrup, 1);
		MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_iqadc_clamp_en, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqadc_clamp_en, 1);
		MOD_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, afe_iqadc_flashhspd, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqadc_flashhspd, 1);
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) {
		sparereg = READ_PHYREG(pi, SpareReg);
		MOD_PHYREG(pi, SpareReg, spareReg, sparereg | 0x0040);
	}

	FOREACH_CORE(pi, core) {
		MOD_PHYREGCE(pi, TxResamplerEnable, core, txfe_baseband_enfree, val);
		MOD_PHYREGCE(pi, TxResamplerEnable, core, txfetoptxfrreseten, val);
		MOD_PHYREGCE(pi, TxResamplerEnable, core, mufreeWren, val);
	}

	MOD_PHYREG(pi, dacClkCtrl, txcore2corefrclken, val);
	MOD_PHYREG(pi, dacClkCtrl, txcore2corefrdacclken, val);
	MOD_PHYREG(pi, dacClkCtrl, txfarrowresetfreeen, val);
	MOD_PHYREG(pi, dacClkCtrl, gateafeclocksoveren, val);
	if (ACMAJORREV_33(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, dacClkCtrl, dacpuoren, val);
		MOD_PHYREG(pi, dacClkCtrl, txframeoreden, val);
		MOD_PHYREG(pi, dacClkCtrl, endacratiochgvld, 0);
		phy_ac_chanmgr_mutx_core2core_sync_war(pi, chanmgri->mutx_war_on);
	}
}

void
phy_ac_chanmgr_mutx_core2core_sync_war(phy_info_t *pi, bool enable)
{
	uint8 core, val;
	val = enable ? 0:1;

	wlapi_suspend_mac_and_wait(pi->sh->physhim);
	FOREACH_CORE(pi, core) {
		/* val = 0 preserves core2core sync across resetcca */
		MOD_PHYREGCE(pi, TxResamplerEnable, core, tx_fifo_resetccaEn, val);
	}
	/* val = 1 preserves core2core sync across resetcca */
	MOD_PHYREG(pi, dacClkCtrl, txfarrowresetfreeen,  ~val);
	wlapi_enable_mac(pi->sh->physhim);
}

void
wlc_phy_preempt(phy_info_t *pi, bool enable_preempt, bool EnablePostRxFilter_Proc)
{
	uint8 core;
	uint8 stall_val;
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	if (CHSPEC_IS2G(pi->radio_chanspec) && IS_4350(pi) &&
	    (wlapi_bmac_btc_mode_get(pi->sh->physhim) == WL_BTC_HYBRID)) {
		/* override flag ... never enable */
		enable_preempt &= !(phy_btcx_is_btactive(pi->btcxi));
	}

	/* Update SW copy of preemption setting */
	pi_ac->current_preemption_status = enable_preempt;
	pi->u.pi_acphy->pktabortctl = 0;

	if ((ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) ||
	    ACMAJORREV_3(pi->pubpi->phy_rev)) {
		if (enable_preempt) {
			if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
				/* disable clip condition for norm power when preemption on */
				_PHY_REG_MOD(pi, ACPHY_MLDisableMcs(pi->pubpi->phy_rev),
				             0x0001, 0x0001);
				MOD_PHYREG(pi, Core0_BPHY_TargetVar_log2_pt8us,
				           bphy_targetVar_log2_pt8us, 469);
				WRITE_PHYREG(pi, PktAbortSupportedStates,
				             (ACREV_GE(pi->pubpi->phy_rev, 13)) ? 0x2bbf : 0x2bb7);
				/* Enable pre rxfilt power & disable sssgc post abort */
				wlapi_suspend_mac_and_wait(pi->sh->physhim);
				wlc_phy_deaf_acphy(pi, TRUE);
				WRITE_PHYREG(pi, PktAbortCounterClr, 0x08);
				wlc_phy_deaf_acphy(pi, FALSE);
				wlapi_enable_mac(pi->sh->physhim);
			} else {
				WRITE_PHYREG(pi, PktAbortSupportedStates, 0x2bb7);
				WRITE_PHYREG(pi, SpareReg, 0x3f);
			}
			ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0, 0x21)
				WRITE_PHYREG_ENTRY(pi, PktAbortCtrl, 0x1841)
				WRITE_PHYREG_ENTRY(pi, BphyAbortExitCtrl, 0x3840)
				MOD_PHYREG_ENTRY(pi, RxMacifMode, AbortStatusEn, 1)
				WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_nominal_clip_th0, 0xffff)
				WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_large_gain_mismatch_th0, 0x1f)
				WRITE_PHYREG_ENTRY(pi, PREMPT_cck_nominal_clip_th0, 0xffff)
				WRITE_PHYREG_ENTRY(pi, PREMPT_cck_large_gain_mismatch_th0, 0x1f)
			ACPHY_REG_LIST_EXECUTE(pi);
			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				if (CHSPEC_IS80(pi->radio_chanspec)) {
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0xb0);
				} else if (CHSPEC_IS40(pi->radio_chanspec)) {
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x50);
				} else {
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x28);
				}

				if ((ACMAJORREV_3(pi->pubpi->phy_rev)) &&
					(pi->u.pi_acphy->chanmgri->cfg.LTEJ_WAR_en == TRUE)) {
					WRITE_PHYREG(pi, PREMPT_ofdm_large_gain_mismatch_th0, 9);
				}
			} else {
				WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x28);
				WRITE_PHYREG(pi, PREMPT_cck_nominal_clip_cnt_th0, 0x38);
			}
		} else {
			ACPHY_REG_LIST_START
				/* disable Preempt */
				MOD_PHYREG_ENTRY(pi, RxMacifMode, AbortStatusEn, 0)
				MOD_PHYREG_ENTRY(pi, PktAbortCtrl, PktAbortEn, 0)
				WRITE_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0, 0x0)
			ACPHY_REG_LIST_EXECUTE(pi);
		}
	} else if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		/* Enable Preemption */
		if (enable_preempt) {
			ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, PktAbortCtrl, 0x1041)
				WRITE_PHYREG_ENTRY(pi, RxMacifMode, 0x0a00)
			ACPHY_REG_LIST_EXECUTE(pi);

			if ((ACMAJORREV_2(pi->pubpi->phy_rev) && ACMINORREV_4(pi)) ||
					ACMAJORREV_5(pi->pubpi->phy_rev)) {
			  ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, PktAbortSupportedStates, 0x2fff)
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_cck_large_gain_mismatch_th0, 0x9)
				MOD_PHYREG_ENTRY(pi, miscSigCtrl, force_1st_sigqual_bpsk, 1)
			  ACPHY_REG_LIST_EXECUTE(pi);
			} else {
			  ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, PktAbortSupportedStates,
				       ((AC4354REV(pi)) ? 0x2ab7 : 0x2ff7))
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_cck_large_gain_mismatch_th0, 0x1f)
			  ACPHY_REG_LIST_EXECUTE(pi);
			}
			if (ACMAJORREV_5(pi->pubpi->phy_rev)) {
				ACPHYREG_BCAST(pi, Core0cckcomputeGainInfo, 0x6);
				wlc_phy_preemption_abort_during_timing_search(pi,
					(phy_ac_btcx_get_btc_mode(pi_ac->btcxi) == 0) ||
					(phy_ac_btcx_get_btc_mode(pi_ac->btcxi) == 2));
			}
			ACPHY_REG_LIST_START
			  WRITE_PHYREG_ENTRY(pi, BphyAbortExitCtrl, 0x3840)
			  ACPHYREG_BCAST_ENTRY(pi, PREMPT_per_pkt_en0, 0x21)
			  ACPHYREG_BCAST_ENTRY(pi, PREMPT_ofdm_nominal_clip_th0, 0xffff)
			  ACPHYREG_BCAST_ENTRY(pi, PREMPT_cck_nominal_clip_th0, 0xffff)
			  ACPHYREG_BCAST_ENTRY(pi, PREMPT_ofdm_nominal_clip_th_xtra_bits0, 0x3)
			  ACPHYREG_BCAST_ENTRY(pi, PREMPT_ofdm_large_gain_mismatch_th0, 0x1f)
			  ACPHYREG_BCAST_ENTRY(pi, PREMPT_cck_nominal_clip_cnt_th0, 0x30)
			ACPHY_REG_LIST_EXECUTE(pi);


			if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
			  ACPHY_REG_LIST_START
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_cck_nominal_clip_cnt_th0, 0x32)
			  ACPHY_REG_LIST_EXECUTE(pi);
			}

			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				ACPHYREG_BCAST(pi, PREMPT_per_pkt_en0, 0x31);
				ACPHYREG_BCAST(pi, PREMPT_ofdm_large_gain_mismatch_th0,
					(IS_4364_3x3(pi) ? 0xf : 0x09));

				if (CHSPEC_IS20(pi->radio_chanspec)) {
					ACPHYREG_BCAST(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x24);
				} else if (CHSPEC_IS40(pi->radio_chanspec)) {
				        ACPHYREG_BCAST(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x48);
				} else {
					ACPHYREG_BCAST(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0xa0);
				}
			} else {
			        if (CHSPEC_IS20(pi->radio_chanspec)) {
				  ACPHY_REG_LIST_START
					ACPHYREG_BCAST_ENTRY(pi, PREMPT_per_pkt_en0, 0x31)
					ACPHYREG_BCAST_ENTRY(pi,
						PREMPT_ofdm_nominal_clip_cnt_th0, 0x24)
					ACPHYREG_BCAST_ENTRY(pi,
						PREMPT_ofdm_large_gain_mismatch_th0, 0x9)
				  ACPHY_REG_LIST_EXECUTE(pi);
				} else {
					ACPHYREG_BCAST(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x48);
				}
			}
			/* Enable Preemption */
		} else {
			ACPHY_REG_LIST_START
				/* disable Preempt */
				MOD_PHYREG_ENTRY(pi, RxMacifMode, AbortStatusEn, 0)
				MOD_PHYREG_ENTRY(pi, PktAbortCtrl, PktAbortEn, 0)
				WRITE_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0, 0x0)
				WRITE_PHYREG_ENTRY(pi, PREMPT_per_pkt_en1, 0x0)
				MOD_PHYREG_ENTRY(pi, miscSigCtrl, force_1st_sigqual_bpsk, 0)
			ACPHY_REG_LIST_EXECUTE(pi);

		    if (ACMINORREV_2(pi))
			    WRITE_PHYREG(pi, PREMPT_per_pkt_en2, 0);
		}
	} else if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		/* 4349 preemption settings */
		if (enable_preempt) {
			if (phy_get_phymode(pi) != PHYMODE_RSDB) {
				WRITE_PHYREG(pi, PktAbortCtrl, 0x1041);
			} else {
				WRITE_PHYREG(pi, PktAbortCtrl, 0x1841);
			}
			FOREACH_CORE(pi, core) {
				MOD_PHYREGC(pi, _BPHY_TargetVar_log2_pt8us,
					core, bphy_targetVar_log2_pt8us, 479);
			}
			ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, RxMacifMode, 0x0a00)
				WRITE_PHYREG_ENTRY(pi, PktAbortSupportedStates, 0x2bbf)
				WRITE_PHYREG_ENTRY(pi, BphyAbortExitCtrl, 0x3840)
				WRITE_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0, 0x21)
				WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_nominal_clip_th0, 0xffff)
				WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_large_gain_mismatch_th0, 0x1f)
				WRITE_PHYREG_ENTRY(pi, PREMPT_cck_nominal_clip_th0, 0xffff)
				WRITE_PHYREG_ENTRY(pi, PREMPT_cck_large_gain_mismatch_th0, 0x1f)
				WRITE_PHYREG_ENTRY(pi, PREMPT_cck_nominal_clip_cnt_th0, 0x38)
			ACPHY_REG_LIST_EXECUTE(pi);
			if (!ACMAJORREV_36(pi->pubpi->phy_rev)) {
				if (phy_get_phymode(pi) != PHYMODE_RSDB) {
					ACPHY_REG_LIST_START
						WRITE_PHYREG_ENTRY(pi, PREMPT_per_pkt_en1, 0x21)
						WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_nominal_clip_th1,
							0xffff)
						WRITE_PHYREG_ENTRY(pi,
							PREMPT_ofdm_large_gain_mismatch_th1, 0x1f)
						WRITE_PHYREG_ENTRY(pi, PREMPT_cck_nominal_clip_th1,
							0xffff)
						WRITE_PHYREG_ENTRY(pi,
							PREMPT_cck_large_gain_mismatch_th1, 0x1f)
						WRITE_PHYREG_ENTRY(pi,
							PREMPT_cck_nominal_clip_cnt_th1, 0x38)
					ACPHY_REG_LIST_EXECUTE(pi);
				}
			}

			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				if (CHSPEC_BW_LE20(pi->radio_chanspec)) {
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x28);
					if (phy_get_phymode(pi) != PHYMODE_RSDB)
						WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th1,
							0x28);
				} else if (CHSPEC_IS40(pi->radio_chanspec)) {
				    WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x50);
			        if (phy_get_phymode(pi) != PHYMODE_RSDB)
						WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th1,
							0x50);
				} else {
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0xb0);
			        if (phy_get_phymode(pi) != PHYMODE_RSDB)
						WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th1,
							0xb0);
				}
			} else {
				WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x28);
				if (phy_get_phymode(pi) != PHYMODE_RSDB)
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th1,
						0x28);
				if ((CHSPEC_IS40(pi->radio_chanspec)) &&
				    (phy_get_phymode(pi) != PHYMODE_RSDB)) {
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x50);
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th1, 0x50);
			    }
			}
			/* 4349B0: Disable SSAGC after pktabort & Power post RxFilter */
			MOD_PHYREG(pi, PktAbortCounterClr, ssagc_pktabrt_enable, 0);
			MOD_PHYREG(pi, PktAbortCounterClr,
				mux_post_rxfilt_power_for_abrt, 0);
#ifdef OCL
			if (PHY_OCL_ENAB(pi->sh->physhim)) {
				if (pi->sh->phyrxchain == 3) {
					/* Disable preemption on sleeping core */
					if (pi->u.pi_acphy->ocl_coremask == 1) {
						WRITE_PHYREG(pi, PREMPT_per_pkt_en1, 0);
					} else {
						WRITE_PHYREG(pi, PREMPT_per_pkt_en0, 0);
					}
				}
			}
#endif

		} else {
			ACPHY_REG_LIST_START
				/* Disable Preempt */
				MOD_PHYREG_ENTRY(pi, RxMacifMode, AbortStatusEn, 0)
				MOD_PHYREG_ENTRY(pi, PktAbortCtrl, PktAbortEn, 0)
				WRITE_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0, 0x0)
			ACPHY_REG_LIST_EXECUTE(pi);
			if (phy_get_phymode(pi) != PHYMODE_RSDB)
				WRITE_PHYREG(pi, PREMPT_per_pkt_en1, 0x0);
		}
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		/* 43012 preemption settings */
		if (enable_preempt) {
			if (EnablePostRxFilter_Proc) {
				ACPHY_REG_LIST_START
					/* If Post Rx Filter Processing is enabled */
					MOD_PHYREG_ENTRY(pi, PktAbortCounterClr,
							mux_post_rxfilt_power_for_abrt, 1)
					MOD_PHYREG_ENTRY(pi, PktAbortCounterClr,
							ssagc_pktabrt_enable, 0)
					MOD_PHYREG_ENTRY(pi, PktAbortCtrl, PktAbortEn, 1)
					WRITE_PHYREG_ENTRY(pi, PktAbortSupportedStates, 0x2bbf)
					/* Mode1: Nominal gain clip detect */
					/*	Cond1: Norm Pwr > Nominal clip threshold */
					/*	Cond2: Gain Needed < Large gain mismatch thres */
					/*	Cond3: ADC clip count threshold - > Not impltd */
					MOD_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0,
							clip_detect_enable, 1)
					MOD_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0,
							clip_detect_cond1_enable, 1)
					MOD_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0,
							clip_detect_cond2_enable, 1)
					MOD_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0,
							clip_detect_cond3_enable, 0)
					/*	Con1 threshold - I2 +Q2 value (max of 65535) -
						Moving average value over 0.8us
					*/
					WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_nominal_clip_th0, 43000)
					WRITE_PHYREG_ENTRY(pi, PREMPT_cck_nominal_clip_th0, 43000)
					/*	Cond2 threshold - Gain Mismatch */
					WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_large_gain_mismatch_th0,
							20)
					WRITE_PHYREG_ENTRY(pi, PREMPT_cck_large_gain_mismatch_th0,
							30)

					/* Mode2: Low Pwr Detect */
					/*	Gain needed > Low power mismatch threshold */
					MOD_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0, low_power_enable,
							0)
					WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_low_power_mismatch_th0,
							28)
					WRITE_PHYREG_ENTRY(pi, PREMPT_cck_low_power_mismatch_th0,
							28)

					/* Mode3: Pkt Rx Pwr Variations */
					/*	Gain Needed > max_gain_mismatch_pkt_rcv_th */
					/*	Gain needed < min_gain_mismatch_pkt_rcv_th */
					/* These variations observed over consecutive
					'n' intervals then abort
					*/
					MOD_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0,
							pwr_variation_enable, 1)
					WRITE_PHYREG_ENTRY(pi,
							PREMPT_ofdm_max_gain_mismatch_pkt_rcv_th0,
							28)
					WRITE_PHYREG_ENTRY(pi,
							PREMPT_ofdm_min_gain_mismatch_pkt_rcv_th0,
							8)
					WRITE_PHYREG_ENTRY(pi,
							PREMPT_cck_max_gain_mismatch_pkt_rcv_th0,
							28)
					WRITE_PHYREG_ENTRY(pi,
							PREMPT_cck_min_gain_mismatch_pkt_rcv_th0,
							8)
					/* Actually value to be set is 32, but since the
					polling clock fixed at 10MHz, using scaled value
					*/
					MOD_PHYREG_ENTRY(pi, PktAbortCtrl, PwrVariationThCCK, 4)
					MOD_PHYREG_ENTRY(pi, PktAbortCtrl, PwrVariationThOFDM, 4)

					MOD_PHYREG_ENTRY(pi, Core0_BPHY_TargetVar_log2_pt8us,
							bphy_targetVar_log2_pt8us, 479)
					/* Exit classifier state ona pre-emption of Bphy */
					MOD_PHYREG_ENTRY(pi, BphyAbortExitCtrl, CckStExitonAbrtEn,
							0)
					MOD_PHYREG_ENTRY(pi, BphyAbortExitCtrl, bphyAbrtTimeOutCtr,
							32)
					MOD_PHYREG_ENTRY(pi, BphyAbortExitCtrl, bphyIdleOnAbortEn,
							1)
					MOD_PHYREG_ENTRY(pi, BphyAbortExitCtrl,
							bphyIdleForPreDetDis, 1)
					/* Packet Abort enable */
					WRITE_PHYREG_ENTRY(pi, RxMacifMode, 0xa00)
					MOD_PHYREG_ENTRY(pi, RxMacifMode, AbortStatusEn, 1)
				ACPHY_REG_LIST_EXECUTE(pi);
			} else {
				/* If ADC samples are used for processing */
				ACPHY_REG_LIST_START
					MOD_PHYREG_ENTRY(pi, Core0_BPHY_TargetVar_log2_pt8us,
							bphy_targetVar_log2_pt8us, 479)
					MOD_PHYREG_ENTRY(pi, PktAbortCounterClr,
							mux_post_rxfilt_power_for_abrt, 0)
					WRITE_PHYREG_ENTRY(pi, PktAbortCtrl, 0x1841)
					WRITE_PHYREG_ENTRY(pi, RxMacifMode, 0x0a00)
					WRITE_PHYREG_ENTRY(pi, BphyAbortExitCtrl, 0x3840)
					WRITE_PHYREG_ENTRY(pi, PREMPT_per_pkt_en0, 0x21)
					WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_nominal_clip_th0, 0xffff)
					WRITE_PHYREG_ENTRY(pi, PREMPT_ofdm_large_gain_mismatch_th0,
							0x1f)
					WRITE_PHYREG_ENTRY(pi, PREMPT_cck_nominal_clip_th0, 0xffff)
					WRITE_PHYREG_ENTRY(pi, PREMPT_cck_large_gain_mismatch_th0,
							0x1f)
					WRITE_PHYREG_ENTRY(pi, PREMPT_cck_nominal_clip_cnt_th0,
							0x38)
				ACPHY_REG_LIST_EXECUTE(pi);
				if (CHSPEC_IS5G(pi->radio_chanspec)) {
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x28);
				} else {
					WRITE_PHYREG(pi, PREMPT_ofdm_nominal_clip_cnt_th0, 0x28);
				}
			}
		} else {
			/* Disable Preempt */
			MOD_PHYREG(pi, RxMacifMode, AbortStatusEn, 0);
			MOD_PHYREG(pi, PktAbortCtrl, PktAbortEn, 0);
			WRITE_PHYREG(pi, PREMPT_per_pkt_en0, 0x0);
		}
	} else if ((ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMINORREV_0(pi)) ||
	           ACMAJORREV_33(pi->pubpi->phy_rev)) {
	    /* 4365B0 PREEMPTion SETTINGs */
	    if (enable_preempt) {
	            WRITE_PHYREG(pi, PktAbortCtrl, 0x1841);
	            FOREACH_CORE(pi, core) {
	                    MOD_PHYREGC(pi, _BPHY_TargetVar_log2_pt8us,
	                            core, bphy_targetVar_log2_pt8us, 479);
	            }
	            WRITE_PHYREG(pi, RxMacifMode, 0x0a00);
	            WRITE_PHYREG(pi, PktAbortSupportedStates, 0x2bbf);
	            WRITE_PHYREG(pi, BphyAbortExitCtrl, 0x3840);
	            WRITE_PHYREG(pi, PktAbortCounterClr, 0x18);
	            /* Reduces -42 dbm humps in jammer */
	            if (CHSPEC_IS2G(pi->radio_chanspec)) {
	                    FOREACH_CORE(pi, core) {
	                            WRITE_PHYREGC(pi, Clip2Threshold, core, 0xb8b8);
	                    }
	            }
	            stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	            ACPHY_DISABLE_STALL(pi);
	            phy_ac_chanmgr_preempt_postfilter_reg_tbl(pi, TRUE);
	            ACPHY_ENABLE_STALL(pi, stall_val);

	    } else {
	            /* Disable Preempt */
	            MOD_PHYREG(pi, RxMacifMode, AbortStatusEn, 0);
	            MOD_PHYREG(pi, PktAbortCtrl, PktAbortEn, 0);
		        phy_ac_chanmgr_preempt_postfilter_reg_tbl(pi, FALSE);
	    }
	} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
	        /* 7271 PREEMPTion SETTINGs */
		if (enable_preempt) {
			FOREACH_CORE(pi, core) {
				MOD_PHYREGC(pi, _BPHY_TargetVar_log2_pt8us,
				core, bphy_targetVar_log2_pt8us, 479);
			}
			ACPHYREG_BCAST(pi, PREMPT_per_pkt_en0,
				((pi->sh->interference_mode & ACPHY_LPD_PREEMPTION) ? 0x1b:0x19));
			ACPHY_REG_LIST_START
				WRITE_PHYREG_ENTRY(pi, PktAbortCtrl, 0xf041)
				WRITE_PHYREG_ENTRY(pi, RxMacifMode, 0x0a00)
				WRITE_PHYREG_ENTRY(pi, BphyAbortExitCtrl, 0x3840)
				WRITE_PHYREG_ENTRY(pi, PktAbortCounterClr, 0x118)
				WRITE_PHYREG_ENTRY(pi, BphyAbortExitCtrl, 0x3840)
				WRITE_PHYREG_ENTRY(pi, PktAbortSupportedStates, 0x2bff)
				/* fill register value for 4 cores  */
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_ofdm_nominal_clip_th0, 0x2000)
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_cck_nominal_clip_th0, 0x2400)
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_ofdm_nominal_clip_th_xtra_bits0,
					0x2800)
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_ofdm_nominal_clip_th_hipwr0, 0xffff)
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_cck_nominal_clip_th_hipwr0, 0xffff)
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_ofdm_large_gain_mismatch_th0, 0x1f)
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_ofdm_low_power_mismatch_th0, 0x1e)
				ACPHYREG_BCAST_ENTRY(pi, PREMPT_cck_low_power_mismatch_th0, 0x1e)
			ACPHY_REG_LIST_EXECUTE(pi);
			} else {
/* Disable Preempt */
				MOD_PHYREG(pi, RxMacifMode, AbortStatusEn, 0);
				MOD_PHYREG(pi, PktAbortCtrl, PktAbortEn, 0);
				ACPHYREG_BCAST(pi, PREMPT_per_pkt_en0, 0);
			}
	}

	if (D11REV_GE(pi->sh->corerev, 47)) {
		if (READ_PHYREGFLD(pi, PktAbortCtrl, PktAbortEn)) {
			pi->u.pi_acphy->pktabortctl = READ_PHYREG(pi, PktAbortCtrl);
		}
		wlc_phy_btc_dyn_preempt(pi);
	}
}

static void
phy_ac_chanmgr_preempt(phy_type_chanmgr_ctx_t *ctx, bool enable_preempt,
    bool EnablePostRxFilter_Proc)
{
	phy_ac_chanmgr_info_t *info = (phy_ac_chanmgr_info_t *)ctx;
	phy_info_t *pi = info->pi;
	wlc_phy_preempt(pi, enable_preempt, EnablePostRxFilter_Proc);
}

void
wlc_phy_rxcore_setstate_acphy(wlc_phy_t *pih, uint8 rxcore_bitmask, uint8 phytxchain)
{
	phy_info_t *pi = (phy_info_t*)pih;
	uint16 rfseqCoreActv_DisRx_save;
	uint16 rfseqMode_save;
	uint8 stall_val = 0, core;
	uint8 orig_rxfectrl1 = 0;
	uint16 classifier_state = 0;
	uint16 edThreshold_save[PHY_MAX_CORES] = {0};

	ASSERT((rxcore_bitmask > 0) && (rxcore_bitmask <= 15));
	if ((pi->sh->phyrxchain == rxcore_bitmask) && !pi->u.pi_acphy->init)
		return;
	pi->sh->phyrxchain = rxcore_bitmask;
	pi->u.pi_acphy->both_txchain_rxchain_eq_1 =
		((rxcore_bitmask == 1) && (pi->sh->phytxchain == 1)) ? TRUE : FALSE;

	if (!pi->sh->clk)
		return;

	wlapi_suspend_mac_and_wait(pi->sh->physhim);
	pi->u.pi_acphy->chanmgri->data.phyrxchain_old = READ_PHYREGFLD(pi, CoreConfig, CoreMask);
	if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev) ||
		ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* Disable classifier */
		classifier_state = READ_PHYREG(pi, ClassifierCtrl);
		wlc_phy_classifier_acphy(pi, ACPHY_ClassifierCtrl_classifierSel_MASK, 4);

		/* Disable stalls and hold FIFOs in reset */
		stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
		orig_rxfectrl1 = READ_PHYREGFLD(pi, RxFeCtrl1, soft_sdfeFifoReset);
		ACPHY_DISABLE_STALL(pi);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);
	}

	/* Save Registers */
	rfseqCoreActv_DisRx_save = READ_PHYREGFLD(pi, RfseqCoreActv2059, DisRx);
	rfseqMode_save = READ_PHYREG(pi, RfseqMode);

	// Reset PHY. some bad state of inactive cores causes trouble in active cores.
	wlc_phy_resetcca_acphy(pi);

	/* JIRA: CRDOT11ACPHY-909; The fix is to max out the edThreshold to prevent
	* the unused core stuck at wait_energy_drop during coremask setting
	*/
	FOREACH_CORE(pi, core) {
		edThreshold_save[core] = READ_PHYREGC(pi, edThreshold, core);
		WRITE_PHYREGC(pi, edThreshold, core, 0xffff);
	}
	/* delay to allow the edThreshold setting take effect */
	OSL_DELAY(10);

	/* Indicate to PHY of the Inactive Core */
	MOD_PHYREG(pi, CoreConfig, CoreMask, rxcore_bitmask);
	/* Indicate to RFSeq of the Inactive Core */
	MOD_PHYREG(pi, RfseqCoreActv2059, EnRx, rxcore_bitmask);
	ACPHY_REG_LIST_START
		/* Make sure Rx Chain gets shut off in Rx2Tx Sequence */
		MOD_PHYREG_ENTRY(pi, RfseqCoreActv2059, DisRx, 7)
		/* Make sure Tx Chain doesn't get turned off during this function */
		MOD_PHYREG_ENTRY(pi, RfseqCoreActv2059, EnTx, 0)
		MOD_PHYREG_ENTRY(pi, RfseqMode, CoreActv_override, 1)
	ACPHY_REG_LIST_EXECUTE(pi);
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		acphy_set_lpmode(pi, ACPHY_LP_RADIO_LVL_OPT);
	}

	wlc_phy_force_rfseq_noLoleakage_acphy(pi);

	/* Make TxEn chains point to phytxchain */
	/* Needed for X51A for Assymetric TX /RX mode */
	MOD_PHYREG(pi, RfseqCoreActv2059, EnTx, phytxchain);

	/*  Restore Register */
	MOD_PHYREG(pi, RfseqCoreActv2059, DisRx, rfseqCoreActv_DisRx_save);
	WRITE_PHYREG(pi, RfseqMode, rfseqMode_save);

	if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev) ||
		ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* Restore FIFO reset and Stalls */
		ACPHY_ENABLE_STALL(pi, stall_val);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, orig_rxfectrl1);
		OSL_DELAY(1);

		/* Restore classifier */
		WRITE_PHYREG(pi, ClassifierCtrl, classifier_state);
		OSL_DELAY(1);

		/* Reset PHY */
		wlc_phy_resetcca_acphy(pi);
	}

	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		/* 4349 Channel Smoothing related changes */
		if ((phy_get_phymode(pi) == PHYMODE_MIMO) && (pi->sh->phyrxchain == 0x3)) {
			MOD_PHYREG(pi, chnsmCtrl1, disable_2rx_nvar_calc, 0x0);
		} else {
			MOD_PHYREG(pi, chnsmCtrl1, disable_2rx_nvar_calc, 0x1);
		}
#ifdef OCL
		if (PHY_OCL_ENAB(pi->sh->physhim)) {
			if ((pi->u.pi_acphy->chanmgri->data.phyrxchain_old == 3) &&
				(pi->sh->phyrxchain != 3)) {
				wlc_phy_ocl_disable_req_set(pih, OCL_DISABLED_SISO,
				                            TRUE, WLC_OCL_REQ_RXCHAIN);
			} else if ((pi->u.pi_acphy->chanmgri->data.phyrxchain_old != 3) &&
				(pi->sh->phyrxchain == 3)) {
				wlc_phy_ocl_disable_req_set(pih, OCL_DISABLED_SISO,
				                            FALSE, WLC_OCL_REQ_RXCHAIN);
			}
		}
#endif /* OCL */

	}
	/* Restore edThreshold */
	FOREACH_CORE(pi, core) {
		WRITE_PHYREGC(pi, edThreshold, core, edThreshold_save[core]);
	}

	wlc_phy_set_sdadc_pd_val_per_core_acphy(pi);
	wlapi_enable_mac(pi->sh->physhim);
}

void
wlc_phy_update_rxchains(wlc_phy_t *pih, uint8 *rxcore_bitmask, uint8 *txcore_bitmask,
        uint8 phyrxchain, uint8 phytxchain)
{
	phy_info_t *pi = (phy_info_t*)pih;

	/* Local copy of phyrxchains before overwrite */
	*rxcore_bitmask = 0;
	/* Local copy of EnTx bits from RfseqCoreActv.EnTx */
	*txcore_bitmask = 0;

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* Save and overwrite Rx chains */
		*rxcore_bitmask = pi->sh->phyrxchain;
		*txcore_bitmask = READ_PHYREGFLD(pi, RfseqCoreActv2059, EnTx);
		pi->sh->phyrxchain = pi->sh->hw_phyrxchain;
		wlc_phy_rxcore_setstate_acphy((wlc_phy_t *)pi, pi->sh->hw_phyrxchain,
			phytxchain);
	} else if (!PHY_COREMASK_SISO(pi->pubpi->phy_coremask) &&
		(phy_get_phymode(pi) != PHYMODE_RSDB)) {
		/* Save and overwrite Rx chains */
		wlapi_suspend_mac_and_wait(pi->sh->physhim);
		*rxcore_bitmask = pi->sh->phyrxchain;
		*txcore_bitmask = READ_PHYREGFLD(pi, RfseqCoreActv2059, EnTx);
		wlapi_enable_mac(pi->sh->physhim);
		wlc_phy_rxcore_setstate_acphy((wlc_phy_t *)pi, phyrxchain,
			phytxchain);
	}
}

void
wlc_phy_restore_rxchains(wlc_phy_t *pih, uint8 enRx, uint8 enTx)
{
	phy_info_t *pi = (phy_info_t*)pih;

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* Restore Rx chains */
		wlc_phy_rxcore_setstate_acphy((wlc_phy_t *)pi, enRx, enTx);
		MOD_PHYREG(pi, RfseqCoreActv2059, EnTx, enTx);
	} else if (!PHY_COREMASK_SISO(pi->pubpi->phy_coremask) &&
		(phy_get_phymode(pi) != PHYMODE_RSDB)) {
		/* Restore Rx chains */
		wlc_phy_rxcore_setstate_acphy((wlc_phy_t *)pi, enRx, enTx);
	}
}

uint8
wlc_phy_rxcore_getstate_acphy(wlc_phy_t *pih)
{
	uint16 rxen_bits;
	phy_info_t *pi = (phy_info_t*)pih;

	rxen_bits = READ_PHYREGFLD(pi, RfseqCoreActv2059, EnRx);

	ASSERT(pi->sh->phyrxchain == rxen_bits);

	return ((uint8) rxen_bits);
}

bool
wlc_phy_is_scan_chan_acphy(phy_info_t *pi)
{
	return (SCAN_RM_IN_PROGRESS(pi) &&
	        (pi->interf->curr_home_channel != CHSPEC_CHANNEL(pi->radio_chanspec)));
}

void
wlc_phy_resetcca_acphy(phy_info_t *pi)
{
	uint32 phy_ctl_reg_val = 0;
	uint16 clkgatests_reg_val = 0;
	uint8 stall_val = 0;
	/* SAVE PHY_CTL value */
	phy_ctl_reg_val = R_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param);
	/* MAC should be suspended before calling this function */
	ASSERT((R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC) == 0);
	/* If MACPhy clock not enabled (Bit1), wait for 1us */
	if ((phy_ctl_reg_val & 0x2) == 0) {
		OSL_DELAY(1);
	}

	if (PHY_MAC_REV_CHECK(pi, 36)) {
		/* Save ClkGateSts register */
		clkgatests_reg_val = R_REG(pi->sh->osh, &pi->regs->u.d11regs.ClkGateSts);

		/* Set ForceMacPhyClockRequest bit in ClkGateSts register : SWWLAN-101393 */
		W_REG(pi->sh->osh, &pi->regs->u.d11regs.ClkGateSts,
			(clkgatests_reg_val | (1 << 4)));
	}

	/* bilge count sequence fix */
	if ((ACMAJORREV_1(pi->pubpi->phy_rev) &&
	     (ACMINORREV_0(pi) || ACMINORREV_1(pi))) || ACMAJORREV_3(pi->pubpi->phy_rev) ||
			ACMAJORREV_40(pi->pubpi->phy_rev)) {
		wlapi_bmac_phyclk_fgc(pi->sh->physhim, ON);

		MOD_PHYREG(pi, BBConfig, resetCCA, 1);
		OSL_DELAY(1);
		if (!TINY_RADIO(pi) && !ACMAJORREV_40(pi->pubpi->phy_rev)) {
			MOD_PHYREG(pi, RxFeCtrl1, rxfe_bilge_cnt, 0);
			OSL_DELAY(1);
		}
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);
		OSL_DELAY(1);
		wlapi_bmac_phyclk_fgc(pi->sh->physhim, OFF);
		OSL_DELAY(1);
		MOD_PHYREG(pi, BBConfig, resetCCA, 0);
		OSL_DELAY(1);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 0);
	} else if (IS_4364_3x3(pi)) {
		/* Force gated clocks on */
		wlapi_bmac_phyclk_fgc(pi->sh->physhim, ON);
		W_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, 0x6); /* set reg(PHY_CTL) 0x6 */

		/* Disable Stalls */
		stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
		if (stall_val == 0)
			ACPHY_DISABLE_STALL(pi);

		/* Hold FIFO's in Reset */
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);
		OSL_DELAY(1);

		/* Do the Reset */
		MOD_PHYREG(pi, BBConfig, resetCCA, 1);
		OSL_DELAY(1);
		MOD_PHYREG(pi, BBConfig, resetCCA, 0);

		/* Wait for reset2rx finish, which is triggered by resetcca in hw */
		SPINWAIT(READ_PHYREGFLD(pi, RfseqStatus0, reset2rx), ACPHY_SPINWAIT_RESET2RX);
		if (READ_PHYREGFLD(pi, RfseqStatus0, reset2rx)) {
			PHY_FATAL_ERROR_MESG((" %s: SPINWAIT ERROR : Reset to Rx failed \n",
			__FUNCTION__));
			PHY_FATAL_ERROR(pi, PHY_RC_RESET2RX_FAILED);
		}

		/* Make sure pktproc came out of reset */
		SPINWAIT((READ_PHYREGFLD(pi, pktprocdebug, pktprocstate) == 0),
				ACPHY_SPINWAIT_PKTPROC_STATE);
		if (READ_PHYREGFLD(pi, pktprocdebug, pktprocstate) == 0) {
			PHY_FATAL_ERROR_MESG((" %s: SPINWAIT ERROR : PKTPROC was in PKTRESET \n",
			__FUNCTION__));
			PHY_FATAL_ERROR(pi, PHY_RC_PKTPROC_RESET_FAILED);
		}

		/* Undo Stalls and SDFEFIFO Reset */
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 0);
		ACPHY_ENABLE_STALL(pi, stall_val);
		OSL_DELAY(1);

		/* Force gated clocks off */
		W_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, 0x2); /* set reg(PHY_CTL) 0x2 */
		wlapi_bmac_phyclk_fgc(pi->sh->physhim, OFF);
	} else if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		ACMAJORREV_37(pi->pubpi->phy_rev)) {
		wlapi_bmac_phyclk_fgc(pi->sh->physhim, ON);

		/* # force gated clock on */
		W_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, 0x6); /* set reg(PHY_CTL) 0x6 */
		MOD_PHYREG(pi, BBConfig, resetCCA, 1);
		OSL_DELAY(1);
		MOD_PHYREG(pi, BBConfig, resetCCA, 0);
		W_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, 0x2); /* set reg(PHY_CTL) 0x2 */

		wlapi_bmac_phyclk_fgc(pi->sh->physhim, OFF);
	} else {
		wlapi_bmac_phyclk_fgc(pi->sh->physhim, ON);

		/* # force gated clock on */
		W_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, 0x6); /* set reg(PHY_CTL) 0x6 */
		W_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, 0xe); /* MacPhyResetCCA = 1 */
		OSL_DELAY(1);
		W_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, 0x6); /* MacPhyResetCCA = 0 */
		W_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, 0x2); /* set reg(PHY_CTL) 0x2 */

		wlapi_bmac_phyclk_fgc(pi->sh->physhim, OFF);
	}

	/* wait for reset2rx finish, which is triggered by resetcca in hw */
	OSL_DELAY(2);

	/* Restore PHY_CTL register */
	W_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, phy_ctl_reg_val);

	if (PHY_MAC_REV_CHECK(pi, 36)) {
		/* Restore ClkGateSts register */
		W_REG(pi->sh->osh, &pi->regs->u.d11regs.ClkGateSts, clkgatests_reg_val);
	}

	if (ACMAJORREV_4(pi->pubpi->phy_rev)|| ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, RfseqMode, Trigger_override, 0);
	}
}

/* 20693_dyn_papd_cfg */
static void
wlc_acphy_dyn_papd_cfg_20693(phy_info_t *pi)
{
	uint8 core;
	FOREACH_CORE(pi, core) {
		if (core == 0) {
			MOD_PHYREG(pi, dyn_radioa0, dyn_radio_ovr0, 0);
		} else {
			MOD_PHYREG(pi, dyn_radioa1, dyn_radio_ovr1, 0);
		}
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
				ovr_pa2g_idac_cas, 1);
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
				ovr_pa2g_idac_incap_compen_main, 1);
			MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
				ovr_pa2g_idac_main, 1);
		} else {
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR3, core,
				ovr_pa5g_idac_cas, 1);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core,
				ovr_pa5g_idac_incap_compen_main, 1);
			MOD_RADIO_REG_20693(pi, TX_TOP_5G_OVR2, core,
				ovr_pa5g_idac_main, 1);
		}
	}
}

static void
wlc_phy_set_bias_ipa_as_epa_acphy_20693(phy_info_t *pi, uint8 core)
{
	MOD_RADIO_REG_20693(pi, SPARE_CFG2, core,
		pa2g_bias_bw_main, 0);
	MOD_RADIO_REG_20693(pi, SPARE_CFG2, core,
		pa2g_bias_bw_cas, 0);
	MOD_RADIO_REG_20693(pi, SPARE_CFG2, core,
		pa2g_bias_bw_pmos, 0);
	MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
		ovr_pa2g_idac_main, 1);
	MOD_RADIO_REG_20693(pi, PA2G_IDAC1, core,
		pa2g_idac_main, 0x24);
	MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
		ovr_pa2g_idac_cas, 1);
	MOD_RADIO_REG_20693(pi, PA2G_IDAC1, core,
		pa2g_idac_cas, 0x22);
	MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
		ovr_pa2g_idac_incap_compen_main, 1);
	MOD_RADIO_REG_20693(pi, PA2G_INCAP, core,
		pa2g_idac_incap_compen_main, 0x2d);
	MOD_RADIO_REG_20693(pi, TX_TOP_2G_OVR1_EAST, core,
		ovr_mx2g_idac_bbdc, 1);
	MOD_RADIO_REG_20693(pi, TXMIX2G_CFG6, core,
		mx2g_idac_bbdc, 0x1c);
	MOD_RADIO_REG_20693(pi, TXMIX2G_CFG2, core,
		mx2g_idac_cascode, 0x13);
}

void wlc_phy_radio20693_sel_logen_mode(phy_info_t *pi)
{
	uint16 phymode = phy_get_phymode(pi);
	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		wlc_phy_radio20693_sel_logen_5g_mode(pi, 5);
		wlc_phy_radio20693_sel_logen_2g_mode(pi, 0);
	} else {
		wlc_phy_radio20693_sel_logen_2g_mode(pi, 2);
		if (phymode == PHYMODE_3x3_1x1) {
		  wlc_phy_radio20693_sel_logen_5g_mode(pi, 2);
		} else if (PHY_AS_80P80(pi, pi->radio_chanspec)) {
		  wlc_phy_radio20693_sel_logen_5g_mode(pi, 1);
		} else if (CHSPEC_IS160(pi->radio_chanspec)) {
		  wlc_phy_radio20693_sel_logen_5g_mode(pi, 0);
		  ASSERT(0);
		} else {
		  wlc_phy_radio20693_sel_logen_5g_mode(pi, 0);
		}
	}
}

void wlc_phy_radio20693_sel_logen_5g_mode(phy_info_t *pi, int mode)
{
	int ct;
	uint16 logen0_5g_inv_pu_val[] =	{1, 1, 1, 1, 1, 0};
	uint16 logen0_pu_val[]		  =	{1, 1, 1, 1, 1, 0};
	uint16 core0_gm_pu_val[]	  =	{1, 1, 1, 1, 1, 0};
	uint16 core1_gm_pu_val[]	  =	{1, 1, 1, 1, 1, 0};
	uint16 logen1_5g_inv_pu_val[] = {0, 1, 1, 1, 0, 0};
	uint16 logen1_pu_val[]		  =	{0, 1, 1, 1, 0, 0};
	uint16 logen1_gm_pu_val[]	  = {0, 1, 0, 1, 0, 0};
	uint16 core2_gm_pu_val[]	  =	{1, 0, 1, 0, 1, 0};
	uint16 core2_lc_pu_val[]	  =	{1, 1, 1, 0, 1, 0};
	uint16 core2_mux_sel_val[]	  =	{0, 1, 0, 0, 0, 0};
	uint16 core3_gm_pu_val[]	  =	{1, 1, 1, 1, 0, 0};
	//uint16 core3_lc_pu_val[]	  =	{1, 1, 1, 1, 0, 0};
	uint16 core3_mux_pu_val[]	  = {1, 1, 1, 1, 0, 0};
	uint16 core3_mux_sel_val[]	  =	{0, 1, 1, 1, 0, 0};
	uint16 bias0_pu_val[]         =	{1, 1, 1, 1, 1, 0};
	uint16 bias1_pu_val[]         =	{1, 1, 1, 1, 1, 0};
	uint16 bias2_pu_val[]		  =	{1, 1, 1, 1, 1, 0};
	uint16 bias3_pu_val[]		  = {1, 1, 1, 1, 1, 0};
	uint16 logen1_vco_inv_pu_val[]  = {0, 1, 1, 1, 0, 0};
	uint16 logen1_main_inv_pu_val[] = {0, 1, 1, 1, 0, 0};


	uint16 pll_regs_bit_vals[][3] = {
		RADIO_PLLREGC_FLD_20693(pi, LO2G_VCO_DRV_CFG1, 0, logen0_5g_inv_pu,
		logen0_5g_inv_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE0_CFG1, 0, logen0_pu,
		logen0_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO2G_VCO_DRV_CFG1, 0, logen1_5g_inv_pu,
		logen1_5g_inv_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE1_CFG1, 0, logen1_pu,
		logen1_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE1_CFG1, 0, logen1_gm_pu,
		logen1_gm_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE0_CFG1, 0, core0_gm_pu,
		core0_gm_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE1_CFG1, 0, core1_gm_pu,
		core1_gm_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE2_CFG1, 0, core2_gm_pu,
		core2_gm_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE3_CFG1, 0, core3_gm_pu,
		core3_gm_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE2_CFG1, 0, core2_lc_pu,
		core2_lc_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE2_CFG1, 0, core2_mux_sel,
		core2_mux_sel_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE3_CFG1, 0, core3_lc_pu,
		core3_mux_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE3_CFG1, 0, core3_mux_sel,
		core3_mux_sel_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE0_IDAC2, 0, bias0_pu,
		bias0_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE1_IDAC2, 0, bias1_pu,
		bias1_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE2_IDAC2, 0, bias2_pu,
		bias2_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO5G_CORE3_IDAC2, 0, bias3_pu,
		bias3_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO2G_VCO_DRV_CFG1, 0, logen1_vco_inv_pu,
		logen1_vco_inv_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO2G_VCO_DRV_CFG1, 0, logen1_main_inv_pu,
		logen1_main_inv_pu_val[mode])
	};

	for (ct = 0; ct < ARRAYSIZE(pll_regs_bit_vals); ct++) {
		phy_utils_mod_radioreg(pi, pll_regs_bit_vals[ct][0],
		                       pll_regs_bit_vals[ct][1], pll_regs_bit_vals[ct][2]);
	}
}

void wlc_phy_radio20693_sel_logen_2g_mode(phy_info_t *pi, int mode)
{
	int ct;
	uint16 logen0_pu_val[]              = {1, 1, 0};
	uint16 logen1_pu_val[]              = {1, 1, 0};
	uint16 logen1_sel_val[]	            = {0, 1, 0};
	uint16 logen1_vco_inv_pu_val[]      = {0, 1, 0};
	uint16 logen1_main_inv_pu_val[]     = {0, 1, 0};
	uint16 logen1_div3_en_val[]         = {0, 1, 0};
	uint16 logen1_div4_en_val[]         = {0, 0, 0};
	uint16 logen1_idac_cklc_bias_val[]  = {0, 3, 0};
	uint16 logen1_idac_cklc_qb_val[]    = {0, 4, 0};

	uint16 pll_regs_bit_vals[][3] = {
		RADIO_PLLREGC_FLD_20693(pi, LO2G_LOGEN0_CFG1, 0, lo2g_logen0_pu,
		logen0_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO2G_LOGEN1_CFG1, 0, lo2g_logen1_pu,
		logen1_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO2G_LOGEN1_CFG1, 0, logen1_sel,
		logen1_sel_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO2G_VCO_DRV_CFG1, 0, logen1_vco_inv_pu,
		logen1_vco_inv_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO2G_VCO_DRV_CFG1, 0, logen1_main_inv_pu,
		logen1_main_inv_pu_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO2G_SPARE0, 0, lo2g_1_div3_en,
		logen1_div3_en_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi, LO2G_SPARE0, 0, lo2g_1_div4_en,
		logen1_div4_en_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi,  LO2G_LOGEN1_IDAC1, 0, logen1_idac_cklc_bias,
		logen1_idac_cklc_bias_val[mode]),
		RADIO_PLLREGC_FLD_20693(pi,  LO2G_LOGEN1_IDAC1, 0, logen1_idac_cklc_qb,
		logen1_idac_cklc_qb_val[mode])
	};

	for (ct = 0; ct < ARRAYSIZE(pll_regs_bit_vals); ct++) {
			phy_utils_mod_radioreg(pi, pll_regs_bit_vals[ct][0],
			pll_regs_bit_vals[ct][1], pll_regs_bit_vals[ct][2]);
	}
}

void wlc_phy_radio20693_afe_clkdistribtion_mode(phy_info_t *pi, int mode)
{
	MOD_RADIO_PLLREG_20693(pi, AFECLK_DIV_CFG1, 0, afeclk_mode, mode);
}


void wlc_phy_radio20693_force_dacbuf_setting(phy_info_t *pi)
{
	uint8 core;

	FOREACH_CORE(pi, core) {
		MOD_RADIO_REG_20693(pi, TX_DAC_CFG5, core, DACbuf_fixed_cap, 0);
		MOD_RADIO_REG_20693(pi, TX_BB_OVR1, core, ovr_DACbuf_fixed_cap, 1);
		MOD_RADIO_REG_20693(pi, TX_DAC_CFG5, core, DACbuf_Cap, 0x6);
		MOD_RADIO_REG_20693(pi, TX_BB_OVR1, core, ovr_DACbuf_Cap, 1);
	}

}

/* Clean up chanspec */
void
chanspec_get_operating_channels(phy_info_t *pi, uint8 *ch)
{
	bool is_80p80 = FALSE;
	uint8 core;
	uint16 phymode = phy_get_phymode(pi);

	BCM_REFERENCE(is_80p80);
	for (core = 0; core < PHY_CORE_MAX; core++) {
		pi->u.pi_acphy->core_freq_mapping[core] = PRIMARY_FREQ_SEGMENT;
	}

	/* RSDB family has 80p80, need to handle carefully */
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		if (phymode == PHYMODE_80P80) {
			ch[0] = wf_chspec_primary80_channel(pi->radio_chanspec);
			ch[1] = wf_chspec_secondary80_channel(pi->radio_chanspec);
			pi->u.pi_acphy->core_freq_mapping[0] = PRIMARY_FREQ_SEGMENT;
			pi->u.pi_acphy->core_freq_mapping[1] = SECONDARY_FREQ_SEGMENT;
			is_80p80 = TRUE;
		} else {
			ch[0] = CHSPEC_CHANNEL(pi->radio_chanspec);
			ch[1] = ch[0];
		}
	} else {
		if (CHSPEC_IS160(pi->radio_chanspec) || CHSPEC_IS8080(pi->radio_chanspec)) {
			wf_chspec_get_80p80_channels(pi->radio_chanspec, ch);
		} else {
			ch[0] = CHSPEC_CHANNEL(pi->radio_chanspec);
			ch[1] = 0;
		}
	}

	PHY_INFORM(("wl%d: %s channels (%d, %d) | %s\n", PI_INSTANCE(pi), __FUNCTION__, ch[0],
		is_80p80 ? ch[1] : ch [0], is_80p80 ? "chan bonded" : "not 80p80, single chan"));
}

static void
chanspec_tune_phy_ACMAJORREV_40(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	if (!wlc_phy_is_scan_chan_acphy(pi)) {
		/* setup DCC parameters */
		if (CCT_INIT(pi_ac)) {
			phy_ac_dccal_init(pi);
			phy_ac_load_gmap_tbl(pi);
		}

		/* Load IDAC GMAP table */
		if (CCT_BAND_CHG(pi_ac)) {
			phy_ac_load_gmap_tbl(pi);
		}

		/* Cal DC cal in channel change */
		phy_ac_dccal(pi);
		/* TODO: 4347A0 */
	}

}

static void
chanspec_tune_phy_ACMAJORREV_37(phy_info_t *pi)
{
	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	//copied below from 4366 EAGLE flow - but think we should follow ACMAJORREV_40 flow
	//MOD_PHYREG(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl, 1);
	//wlc_tiny_setup_coarse_dcc(pi);
	PHY_INFORM(("FIXME: 7271 still need to implement and setup dc calibration here\n"));
}


static void
chanspec_tune_phy_ACMAJORREV_36(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	/* setup DCC parameters */
	if (CCT_INIT(pi_ac)) {
		phy_ac_dccal_init(pi);
		phy_ac_load_gmap_tbl(pi);
	}

	/* Load IDAC GMAP table */
	if (CCT_BAND_CHG(pi_ac)) {
		phy_ac_load_gmap_tbl(pi);
	}

	/* Cal DC cal in channel change */
	phy_ac_dccal(pi);

	/* Spur Canceller */
	phy_ac_spurcan(pi_ac->rxspuri, TRUE);

	/* Disable IQ swap in QT */
	if (ISSIM_ENAB(pi->sh->sih)) {
		ACPHY_REG_LIST_START
			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, swap_iq0, 0x0)
			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, swap_iq1, 0x0)
			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, swap_iq2, 0x0)
		ACPHY_REG_LIST_EXECUTE(pi);
	}

	if (CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac)) {
		wlc_phy_smth(pi, pi_ac->chanmgri->acphy_enable_smth,
			pi_ac->chanmgri->acphy_smth_dump_mode);
	}
}

static void
chanspec_tune_phy_ACMAJORREV_32(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	uint8 core;
	bool elna_present = (CHSPEC_IS2G(pi->radio_chanspec)) ? BF_ELNA_2G(pi_ac) :
		BF_ELNA_5G(pi_ac);

	/* setup DCC parameters */
	if (CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac)) {
		if (ACMAJORREV_32(pi->pubpi->phy_rev) && ACMINORREV_0(pi)) {
			/* Disable the old DCC */
			MOD_PHYREG(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl, 0);
			FOREACH_CORE(pi, core) {
				MOD_RADIO_REG_TINY(pi, RX_BB_2G_OVR_EAST, core,
					ovr_tia_offset_dac, 1);
			}
		} else {
			MOD_PHYREG(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl, 1);
			wlc_tiny_setup_coarse_dcc(pi);
		}
	}

	phy_ac_spurcan(pi_ac->rxspuri, !elna_present);

	if ((CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac)) &&
		ACMAJORREV_33(pi->pubpi->phy_rev)) {
		wlc_phy_smth(pi, pi_ac->chanmgri->acphy_enable_smth,
			pi_ac->chanmgri->acphy_smth_dump_mode);
	}

	if (ISSIM_ENAB(pi->sh->sih)) {
		ACPHY_DISABLE_STALL(pi);

		MOD_PHYREG(pi, RxFeCtrl1, swap_iq0, 0x0);
		MOD_PHYREG(pi, RxFeCtrl1, swap_iq1, 0x0);
		MOD_PHYREG(pi, RxFeCtrl1, swap_iq2, 0x0);
		MOD_PHYREG(pi, RxFeCtrl1, swap_iq3, 0x0);
		MOD_PHYREG(pi, Core1TxControl, iqSwapEnable, 0x0);
		MOD_PHYREG(pi, Core2TxControl, iqSwapEnable, 0x0);
		MOD_PHYREG(pi, Core3TxControl, iqSwapEnable, 0x0);
		MOD_PHYREG(pi, Core4TxControl, iqSwapEnable, 0x0);
	}

	wlc_dcc_fsm_reset(pi);

	if ((ACMAJORREV_32(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) ||
			ACMAJORREV_33(pi->pubpi->phy_rev)) {
		if (!PHY_AS_80P80(pi, pi->radio_chanspec)) {
			phy_ac_chanmgr_enable_core2core_sync_setup(pi);
		}
	}

	// Enable LESI for 4365
	phy_ac_rxgcrs_lesi(pi_ac->rxgcrsi, TRUE);

	if (ACMAJORREV_33(pi->pubpi->phy_rev)) {
		/* Enable MRC SIG QUAL */
		MOD_PHYREG(pi, MrcSigQualControl0, enableMrcSigQual, 0x1);
	}
}

static void
chanspec_tune_phy_ACMAJORREV_25(phy_info_t *pi)
{
	BCM_REFERENCE(pi);
	/* TODO: 4347TC2 */
}

static void
chanspec_tune_phy_ACMAJORREV_5(phy_info_t *pi)
{
	BCM_REFERENCE(pi);
}

static void
chanspec_tune_phy_ACMAJORREV_4(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	bool elna_present = (CHSPEC_IS2G(pi->radio_chanspec)) ? BF_ELNA_2G(pi_ac) :
		BF_ELNA_5G(pi_ac);

	/* setup DCC parameters */
	if (CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac))
		wlc_tiny_setup_coarse_dcc(pi);

	phy_ac_spurcan(pi_ac->rxspuri, !elna_present);

	if (CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac)) {
		wlc_phy_smth(pi, pi_ac->chanmgri->acphy_enable_smth,
			pi_ac->chanmgri->acphy_smth_dump_mode);
	}

	/* 4349A0: in quickturn, disable stalls and swap iq */
	if (ISSIM_ENAB(pi->sh->sih)) {
		ACPHY_REG_LIST_START
			ACPHY_DISABLE_STALL_ENTRY(pi)

			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, swap_iq0, 0x0)
			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, swap_iq1, 0x0)
			MOD_PHYREG_ENTRY(pi, Core1TxControl, iqSwapEnable, 0x0)
		ACPHY_REG_LIST_EXECUTE(pi);

		if (phy_get_phymode(pi) != PHYMODE_RSDB)
			MOD_PHYREG(pi, Core2TxControl, iqSwapEnable, 0x0);
	}

	if (ROUTER_4349(pi)) {
		/* JIRA: SWWLAN-90220
		 * DDR frequency of 392MHz is introducing spur at 2432MHz.
		 * JIRA: SWWLAN-76882 SWWLAN-90228 affects only RSDB mode
		 * Channel	fc   SpurFreq fspur-fc Tone#	AffectedTones (Location in Table)
		 *	9	2452 2457.778 5.778    18.4896	18,19
		 *	10	2457 2457.778 0.778    2.4896	2,3
		 *	11	2462 2457.778 -4.222   -13.5104	-14,-13	(50,51)
		 *	12	2467 2457.778 -9.222   -29.5104	-30,-29	(34,35)
		 *	5l,9u	2442 2457.778 15.778   50.4896	50,51
		 *	6l,10u	2447 2457.778 10.778   34.4896	34,35
		 *	7l,11u	2452 2457.778 5.778    18.4896	18,19
		 *	8l,12u	2457 2457.778 0.778    2.4896	2,3
		 *	9l,13u	2462 2457.778 -4.222   -13.5104	-14,-13	(114,115)
		 */
		acphy_router_4349_nvshptbl_t
			router_4349_nvshptbl[ACPHY_NUM_SPUR_CHANS_ROUTER4349] = {
				{ 2427, 0,  16, 1, 0, RSDB_SPUR | MIMO_SPUR},
				{ 2437, 0,  48, 1, 0, RSDB_SPUR | MIMO_SPUR},
				{ 2422, 1,  32, 1, 0, RSDB_SPUR | MIMO_SPUR},
				{ 2427, 1,  16, 1, 0, RSDB_SPUR | MIMO_SPUR},
				{ 2437, 1, 112, 1, 0, RSDB_SPUR | MIMO_SPUR},
				{ 2442, 1,  96, 1, 0, RSDB_SPUR | MIMO_SPUR},
				{ 2447, 1,  80, 1, 0, RSDB_SPUR | MIMO_SPUR},
				{ 2452, 0,  18, 2, 9, RSDB_SPUR},
				{ 2457, 0,   2, 2, 9, RSDB_SPUR},
				{ 2462, 0,  50, 2, 9, RSDB_SPUR},
				{ 2467, 0,  34, 2, 9, RSDB_SPUR},
				{ 2442, 1,  50, 2, 9, RSDB_SPUR},
				{ 2447, 1,  34, 2, 9, RSDB_SPUR},
				{ 2452, 1,  18, 2, 9, RSDB_SPUR},
				{ 2457, 1,   2, 2, 9, RSDB_SPUR},
				{ 2462, 1, 114, 2, 9, RSDB_SPUR}
		};
		uint32 NvShpTbl[ACPHY_NSHAPETBL_MAX_TONES_ROUTER4349] = {0};
		uint16 channel = CHSPEC_CHANNEL(pi->radio_chanspec);
		uint16 freq_mhz = (uint16)wf_channel2mhz(channel, WF_CHAN_FACTOR_2_4_G);
		uint8 offset = 0;
		uint8 bw = CHSPEC_IS40(pi->radio_chanspec);
		uint8 cnt, num_tones, i, spur_mode, core;

		/* Reset the entries */
		for (cnt = 0; cnt < ACPHY_NUM_SPUR_CHANS_ROUTER4349; ++cnt) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_NVNOISESHAPINGTBL,
				MIN(ACPHY_NSHAPETBL_MAX_TONES_ROUTER4349,
				router_4349_nvshptbl[cnt].num_tones),
				router_4349_nvshptbl[cnt].offset, 32, NvShpTbl);
		}

		for (cnt = 0; cnt < ACPHY_NUM_SPUR_CHANS_ROUTER4349; ++cnt) {
			/* If the chan is spur affected, update its NvShpTbl */
			if (freq_mhz == router_4349_nvshptbl[cnt].freq &&
				bw == router_4349_nvshptbl[cnt].bw) {
				offset = router_4349_nvshptbl[cnt].offset;
				spur_mode = router_4349_nvshptbl[cnt].spur_mode;
				num_tones = MIN(ACPHY_NSHAPETBL_MAX_TONES_ROUTER4349,
					router_4349_nvshptbl[cnt].num_tones);

				/* Update the noise variance in the NvShpTbl */
				for (i = 0; i < num_tones; ++i) {
					NvShpTbl[i] =
						router_4349_nvshptbl[cnt].nv_val;
					/* Writing the same Nv for all the cores */
					FOREACH_CORE(pi, core) {
						NvShpTbl[i] |= (NvShpTbl[i] << (8 * core));
					}
				}

				if (phy_get_phymode(pi) == PHYMODE_RSDB) {
					if (spur_mode & RSDB_SPUR) {
						wlc_phy_table_write_acphy(pi,
							ACPHY_TBL_ID_NVNOISESHAPINGTBL, num_tones,
							offset, 32, NvShpTbl);
					}
				} else if (phy_get_phymode(pi) == PHYMODE_MIMO) {
					if (spur_mode & MIMO_SPUR) {
						wlc_phy_table_write_acphy(pi,
							ACPHY_TBL_ID_NVNOISESHAPINGTBL, num_tones,
							offset, 32, NvShpTbl);
					}
				}
			}
		}

		/* JIRAs: SWWLAN-69184,SWWLAN-70731: Fix for BCC 2-stream
		 * 11n/11ac rate failures in 53573
		 */
		MOD_PHYREG(pi, DemodSoftreset, demod_reset_on_pktprocreset, 0x1);
		OSL_DELAY(10);
		MOD_PHYREG(pi, DemodSoftreset, demod_reset_on_pktprocreset, 0x0);

		/* JIRA: SWWLAN-87252: Fix for PER Humps at ~-35dBm in 5G 20 20in40 20in80
		 * Issue: High-Gain is having PER hump at ~-35dBm
		 * Fix: Moving the NbClip Thsh in such a way that Mid gain will be applied
		 */
		if (CHSPEC_IS5G(pi->radio_chanspec) && !BF_ELNA_5G(pi->u.pi_acphy)) {
			FOREACH_CORE(pi, core) {
				MOD_PHYREGC(pi, FastAgcClipCntTh, core, fastAgcNbClipCntTh,
					CHSPEC_BW_LE20(pi->radio_chanspec) ? 0x0a :
					CHSPEC_IS40(pi->radio_chanspec) ? 0x14 : 0x28);
			}
		}
	}

	wlc_dcc_fsm_reset(pi);
}

static void
chanspec_tune_phy_ACMAJORREV_3(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	/* setup DCC parameters */
	if (CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac))
		wlc_tiny_setup_coarse_dcc(pi);

	/* Spur war for 4345ilna */
	if (PHY_ILNA(pi))
		wlc_phy_spurwar_nvshp_acphy(pi, CCT_BW_CHG(pi_ac), TRUE, FALSE);

	if (CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac)) {
		wlc_phy_smth(pi, pi_ac->chanmgri->acphy_enable_smth,
			pi_ac->chanmgri->acphy_smth_dump_mode);
	}
}

static void
chanspec_tune_phy_ACMAJORREV_2(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	if ((!ACMINORREV_0(pi) && !ACMINORREV_2(pi)) &&
		CHSPEC_IS2G(pi->radio_chanspec) && (BF2_2G_SPUR_WAR(pi_ac) == 1)) {
		phy_ac_dssfB(pi_ac->rxspuri, TRUE);
	}

	/* Spur war for 4350 */
	if (BF2_2G_SPUR_WAR(pi_ac) == 1)
		wlc_phy_spurwar_nvshp_acphy(pi, CCT_BW_CHG(pi_ac), TRUE, FALSE);
}

static void
chanspec_tune_phy_ACMAJORREV_1(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	/* Spur war for 4335 Ax/Bx IPA */
	if (PHY_ILNA(pi) && (ACMINORREV_0(pi) || ACMINORREV_1(pi))) {
		if ((BF2_2G_SPUR_WAR(pi_ac) == 1) &&
			CHSPEC_IS2G(pi->radio_chanspec)) {
			wlc_phy_spurwar_nvshp_acphy(pi, CCT_BW_CHG(pi_ac), TRUE, FALSE);
			MOD_RADIO_REG(pi, RFP, PLL_XTAL5, xtal_bufstrg_BT, 3);
			PHY_TRACE(("BT buffer 3 for Spur WAR; %s \n", __FUNCTION__));
		}
		if ((BF3_5G_SPUR_WAR(pi_ac) == 1) &&
				CHSPEC_IS5G(pi->radio_chanspec)) {
			wlc_phy_spurwar_nvshp_acphy(pi, CCT_BW_CHG(pi_ac), TRUE, FALSE);
		}
	}

	/* Spur war for 4339iLNA */
	if (PHY_ILNA(pi) && ACMINORREV_2(pi))
		wlc_phy_spurwar_nvshp_acphy(pi, CCT_BW_CHG(pi_ac), TRUE, FALSE);

	/* Nvshp for 4335 C0 ELNA, 80 MHz since tight filter is being used */
	if (ACMINORREV_2(pi) && (!(PHY_ILNA(pi)))) {
		if (CHSPEC_IS80(pi->radio_chanspec)) {
			wlc_phy_spurwar_nvshp_acphy(pi, CCT_BW_CHG(pi_ac), FALSE, TRUE);
		} else {
		/* Restoring default for 20/40 mhz by reseting it */
			if (CCT_BW_CHG(pi_ac))
				wlc_phy_reset_noise_var_shaping_acphy(pi);
		}
	}

	MOD_PHYREG(pi, RfseqMode, CoreActv_override, 0);

	if ((CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac)) && ACMINORREV_2(pi)) {
		wlc_phy_smth(pi, pi_ac->chanmgri->acphy_enable_smth,
			pi_ac->chanmgri->acphy_smth_dump_mode);
	}
}

static void
chanspec_tune_phy_ACMAJORREV_0(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	/* Update ucode settings based on current band/bw */
	if (CCT_INIT(pi_ac) || CCT_BAND_CHG(pi_ac) || CCT_BW_CHG(pi_ac))
		wlc_phy_hirssi_elnabypass_set_ucode_params_acphy(pi);
}

int
wlc_phy_femctrl_clb_prio_2g_acphy(phy_info_t *pi, bool set, uint32 val)
{
	int ret = 0;

	if (set) {
		femctrl_clb_4347(pi, 1, val);
	} else {
		ret = phy_get_femctrl_clb_prio_2g_acphy(pi);
	}
	return ret;
}

int
wlc_phy_femctrl_clb_prio_5g_acphy(phy_info_t *pi, bool set, uint32 val)
{
	int ret = 0;

	if (set) {
		femctrl_clb_4347(pi, 0, val);
	} else {
		ret = phy_get_femctrl_clb_prio_5g_acphy(pi);
	}

	return ret;
}

static void
femctrl_clb_4347(phy_info_t *pi, int band_is_2g, int slice)
{

	uint8 core;
	int cur_val, mask;

	#define CLBMASK 0x3ff
	#define CLBCORE0_SHIFT 16
	#define CLBCORE1_SHIFT 20

	FOREACH_CORE(pi, core) {

		if (band_is_2g) {
			mask = pi->u.pi_acphy->sromi->nvram_femctrl_clb.map_2g[slice][core];
			phy_set_femctrl_clb_prio_2g_acphy(pi, slice);
		} else {
			mask = pi->u.pi_acphy->sromi->nvram_femctrl_clb.map_5g[slice][core];
			phy_set_femctrl_clb_prio_5g_acphy(pi, slice);
		}

		if (core == 0) {
			/* clb_swctrl_smask_coresel_ant0 */
			cur_val = si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_09, 0, 0);
			cur_val &= (CLBMASK<<CLBCORE0_SHIFT);

			if (slice) {
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_09,
					CLBMASK<<CLBCORE0_SHIFT, ~(mask<<CLBCORE0_SHIFT) & cur_val);
			} else {
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_09,
					CLBMASK<<CLBCORE0_SHIFT, (mask<<CLBCORE0_SHIFT) | cur_val);
			}
		}

		if (core == 1) {
			/* clb_swctrl_smask_coresel_ant1 */
			cur_val = si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10, 0, 0);
			cur_val &= (CLBMASK<<CLBCORE1_SHIFT);

			if (slice) {
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10,
					CLBMASK<<CLBCORE1_SHIFT, ~(mask<<CLBCORE1_SHIFT) & cur_val);
			} else {
				si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10,
					CLBMASK<<CLBCORE1_SHIFT, (mask<<CLBCORE1_SHIFT) | cur_val);
			}
		}
	}

}

static void
chanspec_setup_phy_ACMAJORREV_40(phy_info_t *pi)
{
	MOD_PHYREG(pi, BT_SwControl, inv_btcx_prisel, 0x1);

}

static void
chanspec_setup_phy_ACMAJORREV_37(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	MOD_PHYREG(pi, BT_SwControl, inv_btcx_prisel, 0x1);

	if (CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac)) {
		wlc_phy_smth(pi, pi_ac->chanmgri->acphy_enable_smth,
			pi_ac->chanmgri->acphy_smth_dump_mode);
	}
}

static void
chanspec_setup_phy_ACMAJORREV_36(phy_info_t *pi)
{
#ifdef PHYWAR_43012_HW43012_211_RF_SW_CTRL

	phy_ac_WAR_43012_rf_sw_ctrl_pinmux(pi);

#endif /* PHYWAR_43012_HW43012_211_RF_SW_CTRL */
}

static void
chanspec_setup_phy_ACMAJORREV_32(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	if (CCT_INIT(pi_ac))
		MOD_PHYREG(pi, BT_SwControl, inv_btcx_prisel, 0x1);

	/* Reset the TxPwrCtrl HW during the setup */
	MOD_PHYREG(pi, TxPwrCtrlCmd, txpwrctrlReset, 1);
	OSL_DELAY(10);
	MOD_PHYREG(pi, TxPwrCtrlCmd, txpwrctrlReset, 0);
}

static void
chanspec_setup_phy_ACMAJORREV_25(phy_info_t *pi)
{
	BCM_REFERENCE(pi);
	/* TODO: 4347TC2 */
}

static void
chanspec_setup_phy_ACMAJORREV_5(phy_info_t *pi)
{
	MOD_PHYREG(pi, BT_SwControl, inv_btcx_prisel, 0x1);
}

static void
chanspec_setup_phy_ACMAJORREV_4(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	MOD_PHYREG(pi, BT_SwControl, inv_btcx_prisel, 0x1);

	/* Reset the TxPwrCtrl HW during the setup */
	MOD_PHYREG(pi, TxPwrCtrlCmd, txpwrctrlReset, 1);
	OSL_DELAY(10);
	MOD_PHYREG(pi, TxPwrCtrlCmd, txpwrctrlReset, 0);

	/* 4349 specific chspec initializations */
	if (CCT_INIT(pi_ac) || CCT_BAND_CHG(pi_ac) || CCT_BW_CHG(pi_ac)) {
		wlc_acphy_load_4349_specific_tbls(pi);
		wlc_acphy_dyn_papd_cfg_20693(pi);
		wlc_phy_config_bias_settings_20693(pi);
		acphy_set_lpmode(pi, ACPHY_LP_PHY_LVL_OPT);
	}
}

static void
chanspec_setup_phy_ACMAJORREV_3(phy_info_t *pi)
{
	MOD_PHYREG(pi, BT_SwControl, inv_btcx_prisel, 0x1);
}

static void
chanspec_setup_phy_ACMAJORREV_2(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	MOD_PHYREG(pi, BT_SwControl, inv_btcx_prisel, 0x1);

	if (CCT_INIT(pi_ac) && (ACMINORREV_1(pi) || ACMINORREV_3(pi)) && PHY_ILNA(pi)) {
		si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_06, CC_GCI_XTAL_BUFSTRG_NFC,
			(0x1 << 12));
	}
}

static void
chanspec_setup_phy_ACMAJORREV_1(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	MOD_PHYREG(pi, BT_SwControl, inv_btcx_prisel, 0x1);

	if (CCT_INIT(pi_ac) && ACMINORREV_2(pi) && PHY_ILNA(pi)) {
		si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_06, CC_GCI_XTAL_BUFSTRG_NFC,
			(0x1 << 12));
	}
}

static void
chanspec_setup_phy_ACMAJORREV_0(phy_info_t *pi)
{
	/* store/clear the hirssi(shmem) info of previous channel */
	if (wlc_phy_hirssi_elnabypass_shmem_read_clear_acphy(pi)) {
		/* Check for previous channel */
		phy_ac_hirssi_set_timer(pi);
	}
}

static int
phy_ac_chanmgr_get_chanspec_bandrange(phy_type_chanmgr_ctx_t *ctx, chanspec_t chanspec)
{
	phy_ac_chanmgr_info_t *info = (phy_ac_chanmgr_info_t *)ctx;
	phy_info_t *pi = info->pi;
	if (SROMREV(pi->sh->sromrev) < 12) {
		return phy_ac_chanmgr_get_chan_freq_range(pi, chanspec, PRIMARY_FREQ_SEGMENT);
	} else {
		return phy_ac_chanmgr_get_chan_freq_range_srom12(pi, chanspec);
	}
}

static void
chanspec_setup_phy(phy_info_t *pi)
{
	if (ACMAJORREV_40(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_40(pi);
	else if (ACMAJORREV_37(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_37(pi);
	else if (ACMAJORREV_36(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_36(pi);
	else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_32(pi);
	else if (ACMAJORREV_25(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_25(pi);
	else if (ACMAJORREV_5(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_5(pi);
	else if (ACMAJORREV_4(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_4(pi);
	else if (ACMAJORREV_3(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_3(pi);
	else if (ACMAJORREV_2(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_2(pi);
	else if (ACMAJORREV_1(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_1(pi);
	else if (ACMAJORREV_0(pi->pubpi->phy_rev))
		chanspec_setup_phy_ACMAJORREV_0(pi);
	else {
		PHY_ERROR(("wl%d %s: Invalid ACMAJORREV %d!\n",
			PI_INSTANCE(pi), __FUNCTION__, pi->pubpi->phy_rev));
		ASSERT(0);
	}
}

static void
chanspec_setup_cmn(phy_info_t *pi)
{
	uint8 max_rxchain;
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	if (CCT_INIT(pi_ac)) {
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			if (pi_ac->sromi->nvram_femctrl.txswctrlmap_2g) {
				pi_ac->pa_mode = (pi_ac->sromi->nvram_femctrl.txswctrlmap_2g_mask >>
					(CHSPEC_CHANNEL(pi->radio_chanspec) - 1)) & 1;
			} else {
				pi_ac->pa_mode = 0;
			}
		} else {
			pi_ac->pa_mode = pi_ac->sromi->nvram_femctrl.txswctrlmap_5g;
		}
		wlc_phy_set_reg_on_reset_acphy(pi);
		wlc_phy_set_tbl_on_reset_acphy(pi);

		/* If any rx cores were disabled before phy_init,
		 * disable them again since phy_init enables all rx cores
		 * Also make RfseqCoreActv2059.EnTx = phytxchain except
		 * for cals where it is set to hw_phytxchain
		 */
		max_rxchain =  (1 << pi->pubpi->phy_corenum) - 1;
		if ((pi->sh->phyrxchain != max_rxchain) || (pi->sh->hw_phytxchain != max_rxchain)) {
			wlc_phy_rxcore_setstate_acphy((wlc_phy_t *)pi,
			    pi->sh->phyrxchain, pi->sh->phytxchain);
		}
	}

	/* Set up ED thresholds */
	wlc_phy_apply_default_edthresh_acphy(pi, pi->radio_chanspec);

	if (CCT_INIT(pi_ac) || CCT_BAND_CHG(pi_ac))
		wlc_phy_set_regtbl_on_band_change_acphy(pi);

	if (CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac))
		wlc_phy_set_regtbl_on_bw_change_acphy(pi);

	chanspec_setup_regtbl_on_chan_change(pi);
	chanspec_regtbl_fc_from_nvram(pi);
	chanspec_prefcbs_init(pi);
#ifdef OCL
	if (PHY_OCL_ENAB(pi->sh->physhim)) {
		wlc_phy_ocl_disable_req_set((wlc_phy_t *)pi, OCL_DISABLED_CHANSWITCH,
		                            TRUE, WLC_OCL_REQ_CHANSWITCH);
	}
#endif /* OCL */

}

static void
chanspec_cleanup(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	/* Restore FIFO reset and Stalls */
	MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, pi_ac->chanmgri->FifoReset);

	/* reset RX */
	wlc_phy_resetcca_acphy(pi);

	/* return from Deaf */
	wlc_phy_stay_in_carriersearch_acphy(pi, FALSE);

	/* clear Chspec Call Trace */
	CCT_CLR(pi_ac);

	/* Clear the chanest dump counter */
	pi->phy_chanest_dump_ctr = 0;
}

/* see chanspec_cleanup which restores some of the setup params */
static void
chanspec_setup(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		PHY_TRACE(("wl%d: %s chan = %d\n", pi->sh->unit, __FUNCTION__,
			CHSPEC_CHANNEL(pi->radio_chanspec)));
		PHY_CHANLOG(pi, __FUNCTION__, TS_ENTER, 0);
	}

#ifdef WL11ULB
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		if (PHY_ULB_ENAB(pi->sh->physhim)) {
			wlc_phy_ulb_mode(pi, PMU_ULB_BW_NONE);
		}
	}
#endif /* WL11ULB */

	/* JIRA(CRDOT11ACPHY-143) - Turn off receiver during channel change */
	pi_ac->deaf_count = 0;
	wlc_phy_stay_in_carriersearch_acphy(pi, TRUE);

	/* 7271 does not follow this, see HWJUSTY-263 */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
		phy_ac_chanmgr_disable_core2core_sync_setup(pi);
	}

	/* Hold FIFOs in reset before changing channels */
	pi_ac->chanmgri->FifoReset = READ_PHYREGFLD(pi, RxFeCtrl1, soft_sdfeFifoReset);
	MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);

	/* update corenum and coremask state variables */
	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev))
		phy_ac_update_phycorestate(pi);

	/* BAND CHANGED ? */
	if (CCT_INIT(pi_ac) ||
		(pi_ac->chanmgri->data.curr_band2g != CHSPEC_IS2G(pi->radio_chanspec))) {

		if (!ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMAJORREV_33(pi->pubpi->phy_rev))
			chanspec_setup_hirssi_ucode_cap(pi);

		pi_ac->chanmgri->data.curr_band2g = CHSPEC_IS2G(pi->radio_chanspec);

		/* indicate band change to control flow */
		mboolset(pi_ac->CCTrace, CALLED_ON_BAND_CHG);
	}

	/* BW CHANGED ? */
	if (CCT_INIT(pi_ac) || (pi_ac->curr_bw != CHSPEC_BW(pi->radio_chanspec))) {
		pi_ac->curr_bw = CHSPEC_BW(pi->radio_chanspec);

		/* If called from init, don't call this, as this is called before init */
		if (!CCT_INIT(pi_ac)) {

			/* Set the phy BW as dictated by the chspec (also calls phy_reset) */
			wlapi_bmac_bw_set(pi->sh->physhim, CHSPEC_BW(pi->radio_chanspec));

			/* bw change  do not need a phy_reset when BW_RESET == 1 */
			if (BW_RESET == 0) {
				/* indicate phy reset, follow init path to control flow */
				mboolset(pi_ac->CCTrace, CALLED_ON_INIT);
			} else {
				chanspec_sparereg_war(pi);
			}
		}

		OSL_DELAY(2);

		/* indicate bw change to control flow */
		mboolset(pi_ac->CCTrace, CALLED_ON_BW_CHG);
		if (PHY_AS_80P80(pi, pi_ac->curr_bw) ||
			PHY_AS_80P80(pi, pi->radio_chanspec)) {
			mboolset(pi_ac->CCTrace, CALLED_ON_BW_CHG_80P80);
		}
	}

	/* Change the band bit. Do this after phy_reset */
	if (CHSPEC_IS2G(pi->radio_chanspec))
		MOD_PHYREG(pi, ChannelControl, currentBand, 0);
	else
		MOD_PHYREG(pi, ChannelControl, currentBand, 1);
}

static void
chanspec_tune_phy(phy_info_t *pi)
{
	if (ACMAJORREV_5(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_5(pi);
	else if (ACMAJORREV_4(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_4(pi);
	else if (ACMAJORREV_3(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_3(pi);
	else if (ACMAJORREV_2(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_2(pi);
	else if (ACMAJORREV_1(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_1(pi);
	else if (ACMAJORREV_0(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_0(pi);
	else if (ACMAJORREV_40(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_40(pi);
	else if (ACMAJORREV_37(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_37(pi);
	else if (ACMAJORREV_36(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_36(pi);
	else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_32(pi);
	else if (ACMAJORREV_25(pi->pubpi->phy_rev))
		chanspec_tune_phy_ACMAJORREV_25(pi);
	else {
		PHY_ERROR(("wl%d %s: Invalid ACMAJORREV!\n", PI_INSTANCE(pi), __FUNCTION__));
		ASSERT(0);
	}
}

/* ******************** WARs ********************* */

static void
chanspec_setup_hirssi_ucode_cap(phy_info_t *pi)
{
	BCM_REFERENCE(pi);
}

static void
chanspec_sparereg_war(phy_info_t *pi)
{
	if (CHIPID(pi->sh->chip) == BCM4335_CHIP_ID &&
		CHSPEC_IS80(pi->radio_chanspec)) {

		WRITE_PHYREG(pi, SpareReg, 0xfe);
		wlc_phy_resetcca_acphy(pi);
		WRITE_PHYREG(pi, SpareReg, 0xff);

	}
}

static void
chanspec_regtbl_fc_from_nvram(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	if (!CCT_INIT(pi_ac) && CHSPEC_IS2G(pi->radio_chanspec) &&
		pi_ac->sromi->nvram_femctrl.txswctrlmap_2g &&
		(pi_ac->pa_mode ^ ((pi_ac->sromi->nvram_femctrl.txswctrlmap_2g_mask >>
		(CHSPEC_CHANNEL(pi->radio_chanspec) - 1)) & 1)) &&
		!ACPHY_FEMCTRL_ACTIVE(pi)) {

		pi_ac->pa_mode = (pi_ac->sromi->nvram_femctrl.txswctrlmap_2g_mask >>
			(CHSPEC_CHANNEL(pi->radio_chanspec) - 1)) & 1;

		wlc_phy_write_regtbl_fc_from_nvram(pi);
	}
}

static bool
chanspec_papr_enable(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	bool enable = FALSE;
	int freq;
	uint8 ch = CHSPEC_CHANNEL(pi->radio_chanspec);

	if (!pi_ac->chanmgri->cfg.srom_paprdis) {
		if (PHY_IPA(pi) && (ACMAJORREV_1(pi->pubpi->phy_rev) ||
			(ACMAJORREV_2(pi->pubpi->phy_rev) &&
			(ACMINORREV_1(pi) || ACMINORREV_3(pi))))) {
			enable = TRUE;
		} else if (!PHY_IPA(pi) && ACMAJORREV_2(pi->pubpi->phy_rev) && (ACMINORREV_1(pi))) {
			const void *chan_info = NULL;
			freq = wlc_phy_chan2freq_acphy(pi, ch, &chan_info);
			if (freq == 2472) {
				enable = FALSE;
			} else {
				enable = TRUE;
			}
		}
	}
	if ((ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) &&
		CHSPEC_IS5G(pi->radio_chanspec))
		enable = TRUE;

	return enable;
}

static void
chanspec_tune_rxpath(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
		!ACMAJORREV_33(pi->pubpi->phy_rev) &&
		!ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* DSSF for 4335C0 & 4345 */
		phy_ac_dssf(pi_ac->rxspuri, TRUE);
	}

	phy_ac_rssi_init_gain_err(pi_ac->rssii);

	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		/* 	To keep ED run throughout packet
			This has nothing to do with DSSF
		*/
		MOD_PHYREG(pi, DSSF_C_CTRL, disableCRSCorr, 0);
	}
}

static void
chanspec_tune_txpath(phy_info_t *pi)
{
	uint8 tx_pwr_ctrl_state = PHY_TPC_HW_OFF;
	int freq;
	uint8 ch = CHSPEC_CHANNEL(pi->radio_chanspec);
	uint8 core;
	uint32	fc = wf_channel2mhz(CHSPEC_CHANNEL(pi->radio_chanspec),
	                            CHSPEC_IS2G(pi->radio_chanspec) ? WF_CHAN_FACTOR_2_4_G
	                                                               : WF_CHAN_FACTOR_5_G);
	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		return;
	}

	if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID)) {
		const void *chan_info = NULL;
		freq = wlc_phy_chan2freq_acphy(pi, ch, &chan_info);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
		const chan_info_radio20693_pll_t *chan_info_pll;
		const chan_info_radio20693_rffe_t *chan_info_rffe;
		const chan_info_radio20693_pll_wave2_t *pll_tbl_wave2;

		freq = wlc_phy_chan2freq_20693(pi, ch, &chan_info_pll, &chan_info_rffe,
				&pll_tbl_wave2);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20694_ID)) {
		const chan_info_radio20694_rffe_t *chan_info;

		freq = wlc_phy_chan2freq_20694(pi, ch, &chan_info);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20696_ID)) {
		const chan_info_radio20696_rffe_t *chan_info;

		freq = wlc_phy_chan2freq_20696(pi, ch, &chan_info);
	} else {
		const chan_info_radio20691_t *chan_info_20691;

		freq = wlc_phy_chan2freq_20691(pi, ch, &chan_info_20691);
	}
#if defined(WLC_TXCAL) || (defined(WLOLPC) && !defined(WLOLPC_DISABLED))
	/* If olpc_thresh is present from nvram but olpc_thresh2g/5g is not,
	 * use olpc_thresh value;
	 * If olpc_thresh2g/5g is present from nvram, then use it
	 * the IOVAR olpc_thresh can still be used to override nvram value
	 */
	 if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
	     !ACMAJORREV_33(pi->pubpi->phy_rev) &&
	     !ACMAJORREV_37(pi->pubpi->phy_rev)) {
		ASSERT(pi->olpci != NULL);
		if (pi->olpci->olpc_thresh_iovar_ovr != 1) {
			if (CHSPEC_IS2G(pi->radio_chanspec)) {
				if (pi->olpci->olpc_thresh2g != 0) {
					pi->olpci->olpc_thresh = pi->olpci->olpc_thresh2g;
				}
			} else {
				if (pi->olpci->olpc_thresh5g != 0) {
					pi->olpci->olpc_thresh = pi->olpci->olpc_thresh5g;
				}
			}
		}
	 }
#endif	/* WLC_TXCAL || (WLOLPC && !WLOLPC_DISABLED) */

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev))
		MOD_PHYREG(pi, BT_SwControl, inv_btcx_prisel, 0x1);

	/* set txgain in case txpwrctrl is disabled */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			pi->u.pi_acphy->txpwrindex[core] = (fc < 5500)? 48: 64;
		}
	}
	wlc_phy_txpwr_fixpower_acphy(pi);

	/* Disable TxPwrCtrl */
	tx_pwr_ctrl_state = pi->txpwrctrl;
	wlc_phy_txpwrctrl_enable_acphy(pi, PHY_TPC_HW_OFF);

	/* Set the TSSI visibility limits for 4360 A0/B0 */

	/* Temporary fix for UTF failure for X51 SWWLAN-93602
	 * Always set TSSI visibility threshold
	 */
	wlc_phy_set_tssisens_lim_acphy(pi, TRUE);

	/* Enable TxPwrCtrl */
	if (!((ACMAJORREV_32(pi->pubpi->phy_rev) ||
	       ACMAJORREV_33(pi->pubpi->phy_rev) ||
	       ACMAJORREV_37(pi->pubpi->phy_rev)) &&
	      ((BOARDFLAGS2(GENERIC_PHY_INFO(pi)->boardflags2) & BFL2_TXPWRCTRL_EN) == 0)))
		wlc_phy_txpwrctrl_enable_acphy(pi, tx_pwr_ctrl_state);

#ifdef WLC_TXCAL
	if (pi->olpci->olpc_idx_in_use) {
		wlc_phy_set_olpc_anchor_acphy(pi);
	}

	/* If txcal based olpc is in use, compute the idx before */
	/* calling wlc_phy_txpwrctrl_set_baseindex */
	wlc_phy_txcal_olpc_idx_recal_acphy(pi, pi->olpci->olpc_idx_valid &&
		pi->olpci->olpc_idx_in_use);

	/* Load tx gain table: Typically needs to be done as part of band change.
	 * because of tx gain table capping using txcal and olpc infra, this
	 * needs to be done a chanspec
	 */
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		wlc_phy_ac_gains_load(pi);
	}
#endif /* WLC_TXCAL */
	chanspec_setup_papr(pi, 0, 0);
	if (ACMAJORREV_2(pi->pubpi->phy_rev) && (!PHY_IPA(pi)) &&
	    (pi->u.pi_acphy->chanmgri->cfg.srom_txnoBW80ClkSwitch == 0)) {
		wlc_phy_afeclkswitch_sifs_delay(pi);
		if (!(freq == 5210 || freq == 5290) && (CHSPEC_IS80(pi->radio_chanspec)))
			wlc_phy_modify_txafediv_acphy(pi, 9);
		else if (CHSPEC_IS80(pi->radio_chanspec))
			wlc_phy_modify_txafediv_acphy(pi, 6);
	}
}

static void
chanspec_prefcbs_init(phy_info_t *pi)
{
#ifdef ENABLE_FCBS
	int chanidx, chanidx_current;
	chanidx = 0;
	chanidx_current = 0;

	if (IS_FCBS(pi)) {

		chanidx_current = wlc_phy_channelindicator_obtain(pi);

		for (chanidx = 0; chanidx < MAX_FCBS_CHANS; chanidx++) {
			if ((chanidx != chanidx_current) &&
			(!(pi->phy_fcbs.initialized[chanidx]))) {

				wlc_phy_prefcbsinit_acphy(pi, chanidx);

				if (CCT_INIT(pi_ac)) {
					wlc_phy_set_reg_on_reset_acphy(pi);
					wlc_phy_set_tbl_on_reset_acphy(pi);
				}

				if (CCT_BAND_CHG(pi_ac))
					wlc_phy_set_regtbl_on_band_change_acphy(pi);

				if (CCT_BW_CHG(pi_ac))
					wlc_phy_set_regtbl_on_bw_change_acphy(pi);
			}
		}

		wlc_phy_prefcbsinit_acphy(pi, chanidx_current);
	}
#endif /* ENABLE_FCBS */
}

/* features and WARs enable */
static void
chanspec_fw_enab(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	/* min_res_mask = 0, max_res_mask = 0, clk_ctl_st = 0 */
	uint32 bbpll_parr_in[3] = {0, 0, 0};

	wlapi_bmac_write_shm(pi->sh->physhim, M_PAPDOFF_MCS(pi), pi_ac->chanmgri->cfg.srom_papdwar);

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_33(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* Toggle */
		chanspec_bbpll_parr(pi_ac->rxspuri, bbpll_parr_in, OFF);
		chanspec_bbpll_parr(pi_ac->rxspuri, bbpll_parr_in, ON);
	}

#if (defined(WLOLPC) && !defined(WLOLPC_DISABLED)) || defined(BCMDBG) || \
	defined(WLTEST)
	chanspec_clr_olpc_dbg_mode(pi_ac->tpci);
#endif /* ((WLOLPC) && !(WLOLPC_DISABLED)) || (BCMDBG) || (WLTEST) */

	/* Enable antenna diversity */
	if (wlc_phy_check_antdiv_enable_acphy(pi) &&
		(CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac)) &&
		pi->sh->rx_antdiv) {
		wlc_phy_antdiv_acphy(pi, pi->sh->rx_antdiv);
	}

#ifdef WLC_SW_DIVERSITY
	if (PHYSWDIV_ENAB(pi))
		wlc_phy_swdiv_antmap_init(pi->u.pi_acphy->antdivi);
#endif

#ifdef WL11ULB
	if (PHY_ULB_ENAB(pi)) {
		wlc_phy_ulb_mode(pi, CHSPEC_IS10(pi->radio_chanspec) ? PMU_ULB_BW_10MHZ :
				CHSPEC_IS5(pi->radio_chanspec) ? PMU_ULB_BW_5MHZ
				: CHSPEC_IS2P5(pi->radio_chanspec) ? PMU_ULB_BW_2P5MHZ
				: PMU_ULB_BW_NONE);
	}
#endif /* WL11ULB */

	/* 4347A0 QT BBMULT settings */
	if (ACMAJORREV_40(pi->pubpi->phy_rev) && ISSIM_ENAB(pi->sh->sih)) {
		uint8 ncore_idx;
		uint16 val = 50;

		FOREACH_CORE(pi, ncore_idx) {
			wlc_phy_set_tx_bbmult_acphy(pi, &val, ncore_idx);
		}
	}

#ifdef WL_DSI
	/* Update FCBS dynamic sequence
	   This is for DS0
	*/
	dsi_update_dyn_seq(pi);
#endif /* WL_DSI */
#ifdef OCL
	if (PHY_OCL_ENAB(pi->sh->physhim)) {
		if ((CCT_INIT(pi_ac) || CCT_BW_CHG(pi_ac) || CCT_BAND_CHG(pi_ac))) {
				wlc_phy_ocl_config_acphy(pi);
		}
		wlc_phy_ocl_disable_req_set((wlc_phy_t *)pi, OCL_DISABLED_CHANSWITCH,
				FALSE, WLC_OCL_REQ_CHANSWITCH);
	}
#endif /* OCL */

}

void
wlc_phy_apply_default_edthresh_acphy(phy_info_t *pi, chanspec_t chanspec)
{
	uint8 region_group = wlc_phy_get_locale(pi->rxgcrsi);
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	int32 edthresh_val = 0;

	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		PHY_INFORM(("FIXME: 4347A0 Edthresh is bypassed for the moment\n"));
		return;
	}

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_33(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* just in case for chanspec specific control */
		UNUSED_PARAMETER(chanspec);
	}

	if (region_group ==  REGION_EU) {
		edthresh_val = CHSPEC_IS2G(pi->radio_chanspec) ?
			pi->srom_eu_edthresh2g : pi->srom_eu_edthresh5g;
	} else {
		edthresh_val = CHSPEC_IS2G(pi->radio_chanspec) ?
			pi_ac->sromi->ed_thresh2g : pi_ac->sromi->ed_thresh5g;
	}

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_33(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_37(pi->pubpi->phy_rev)) {
		if (CCT_BAND_CHG(pi_ac)) {
			if (!edthresh_val || edthresh_val > MAX_VALID_EDTHRESH) {
				wlc_phy_adjust_ed_thres(pi, &pi_ac->sromi->ed_thresh_default, TRUE);
			} else {
				wlc_phy_adjust_ed_thres(pi, &edthresh_val, TRUE);
			}
		}
	} else {
		if (!edthresh_val || edthresh_val > MAX_VALID_EDTHRESH) {
			wlc_phy_adjust_ed_thres(pi, &pi_ac->sromi->ed_thresh_default, TRUE);
		} else {
			wlc_phy_adjust_ed_thres(pi, &edthresh_val, TRUE);
		}
	}
}


void
wlc_phy_preemption_abort_during_timing_search(phy_info_t *pi, bool enable)
{
		if (enable) {
				ACPHYREG_BCAST(pi, PktAbortSupportedStates, 0x2fff);
		} else {
				ACPHYREG_BCAST(pi, PktAbortSupportedStates, 0x2ffe);
		}
}

void
wlc_phy_chanspec_set_acphy(phy_info_t *pi, chanspec_t chanspec)
{
	chanspec_module_t *module = get_chanspec_module_list();

	PHY_CHANLOG(pi, __FUNCTION__, TS_ENTER, 0);

	/* sync pi->radio_chanspec with incoming chanspec */
	wlc_phy_chanspec_radio_set((wlc_phy_t *)pi, chanspec);

	/* CHANSPEC DISPATCH */
	do {
		(*module)(pi);
		++module;
	} while (*module != NULL);

	PHY_CHANLOG(pi, __FUNCTION__, TS_EXIT, 0);
	/* Note: !!! DO NOT ADD ANYTHING HERE !!!
	 * All changes to acphy chanspec should go to respective module list above
	 */
}

static void
phy_ac_chanmgr_chanspec_set(phy_type_chanmgr_ctx_t *ctx, chanspec_t chanspec)
{
	phy_ac_chanmgr_info_t *info = (phy_ac_chanmgr_info_t *)ctx;
	phy_info_t *pi = info->pi;
	wlc_phy_chanspec_set_acphy(pi, chanspec);
}

static void
phy_ac_chanmgr_upd_interf_mode(phy_type_chanmgr_ctx_t *ctx, chanspec_t chanspec)
{
	phy_ac_chanmgr_info_t *info = (phy_ac_chanmgr_info_t *)ctx;
	phy_info_t *pi = info->pi;
	if (pi->sh->interference_mode_override == TRUE) {
		pi->sh->interference_mode = CHSPEC_IS2G(chanspec) ?
		        pi->sh->interference_mode_2G_override :
		        pi->sh->interference_mode_5G_override;
	} else {
		pi->sh->interference_mode = CHSPEC_IS2G(chanspec) ?
		        pi->sh->interference_mode_2G :
		        pi->sh->interference_mode_5G;
	}
}

void
wlc_phy_ulb_feature_flag_set(wlc_phy_t *pih)
{
	phy_info_t *pi = (phy_info_t *)pih;
	phy_feature_flags_t *phyflag = pi->ff;
	phyflag ->_ulb = TRUE;
}

#ifdef PHYWAR_43012_HW43012_211_RF_SW_CTRL
static void
phy_ac_WAR_43012_rf_sw_ctrl_pinmux(phy_info_t *pi)
{

	/*
	TCL Proc : WAR_43012_rf_sw_ctrl_pinmux
	*/
	if (BOARDFLAGS(GENERIC_PHY_INFO(pi)->boardtype) ==
			BCM943012WLREF_SSID) {
		si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10, 0xFFFFFFFF,
				0xFFFFE000);
		si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_11, 0x7F,
				0x7F);
	} else {

		si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10, (0x1FFF << 13),
				(0x36 << 13));
		si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_10, (0x3F << 26),
				(0x36 << 26));
	}
	si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_02, (0xF << 24),
			(0x3 << 24));

	si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_02, (0xF << 28),
			(0x3 << 28));

	si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_03, 0xF,
			3);

	si_gci_chipcontrol(pi->sh->sih, CC_GCI_CHIPCTRL_03, (0xF << 4),
			(0x3 << 4));

	WRITE_PHYREG(pi, gpioLoOutEn, 0xFFFF);
	WRITE_PHYREG(pi, gpioHiOutEn, 0x0);
	WRITE_PHYREG(pi, gpioSel, 0x3E);

	/* si_gpiocontrol(si_t *sih, uint32 mask, uint32 val, uint8 priority) */
	si_gpiocontrol(pi->sh->sih, 0xFFFF, 0xFFFF, GPIO_DRV_PRIORITY);
}
#endif /* PHYWAR_43012_HW43012_211_RF_SW_CTRL */

void
wlc_phy_modify_txafediv_acphy(phy_info_t *pi, uint16 a)
{
	uint16 rfseqExtReg_bw40 = 0x5ea;
	uint16 rfseqExtReg_bw80 = 0x7f8;
	uint8	ch = CHSPEC_CHANNEL(pi->radio_chanspec), afe_clk_num = 3, afe_clk_den = 2;
	uint16	b = 640, lb_b = 320;
	uint32	fcw, lb_fcw, tmp_low = 0, tmp_high = 0;
	uint32  deltaphase;
	uint16  deltaphase_lo, deltaphase_hi;
	uint16  farrow_downsamp;
	uint32	fc = wf_channel2mhz(ch, CHSPEC_IS2G(pi->radio_chanspec) ? WF_CHAN_FACTOR_2_4_G
				    : WF_CHAN_FACTOR_5_G);
	uint32 tx_afediv_sel;
	uint32 write_val[2];
	uint8 stall_val, orig_rxfectrl1;

	if ((pi->sh->chippkg == 2) && (fc == 5290))
		return;

	wlapi_suspend_mac_and_wait(pi->sh->physhim);
	phy_utils_phyreg_enter(pi);

	bcm_uint64_multiple_add(&tmp_high, &tmp_low, a * afe_clk_num * b,
		1 << 23, (fc * afe_clk_den) >> 1);
	bcm_uint64_divide(&fcw, tmp_high, tmp_low, fc * afe_clk_den);
	wlc_phy_tx_farrow_mu_setup(pi, fcw & 0xffff, (fcw & 0xff0000) >> 16, fcw & 0xffff,
		(fcw & 0xff0000) >> 16);
	bcm_uint64_multiple_add(&tmp_high, &tmp_low, fc * afe_clk_den,
		1 << 25, 0);
	bcm_uint64_divide(&lb_fcw, tmp_high, tmp_low, a * afe_clk_num * lb_b);
	deltaphase = (lb_fcw - 33554431) >> 1;
	deltaphase_lo = deltaphase & 0xffff;
	deltaphase_hi = (deltaphase >> 16) & 0xff;
	farrow_downsamp = fc * afe_clk_den / (a * afe_clk_num * lb_b);
	WRITE_PHYREG(pi, lbFarrowDeltaPhase_lo, deltaphase_lo);
	WRITE_PHYREG(pi, lbFarrowDeltaPhase_hi, deltaphase_hi);
	if (a == 9) {
		WRITE_PHYREG(pi, lbFarrowDriftPeriod, 4320);
	} else {
		WRITE_PHYREG(pi, lbFarrowDriftPeriod, 2880);
	}
	MOD_PHYREG(pi, lbFarrowCtrl, lb_farrow_outShift, 0);
	MOD_PHYREG(pi, lbFarrowCtrl, lb_decimator_output_shift, 0);
	MOD_PHYREG(pi, lbFarrowCtrl, lb_farrow_outScale, 1);
	MOD_PHYREG(pi, lbFarrowCtrl, lb_farrow_downsampfactor, farrow_downsamp);
	if (RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2 &&
	    !(ISSIM_ENAB(pi->sh->sih))) {

		/* Disable stalls and hold FIFOs in reset */
		stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
		orig_rxfectrl1 = READ_PHYREGFLD(pi, RxFeCtrl1, soft_sdfeFifoReset);

		if (stall_val == 0)
			ACPHY_DISABLE_STALL(pi);

		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);
		if (a == 9) {
			tx_afediv_sel = (rfseqExtReg_bw40 & ~(0x7 << 14) & 0xfffff) |
				(0x2 << 14);
		} else {
			tx_afediv_sel = (rfseqExtReg_bw80 & ~(0x7 << 14) & 0xfffff) |
				(0x0 << 14);
		}

		write_val[0] = ((tx_afediv_sel & 0xfff) << 20) | tx_afediv_sel;
		write_val[1] = (tx_afediv_sel << 8) | (tx_afediv_sel >> 12);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQEXT, 1, 2, 60,
			write_val);
		wlc_phy_force_rfseq_noLoleakage_acphy(pi);

		/* Restore FIFO reset and Stalls */
		ACPHY_ENABLE_STALL(pi, stall_val);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, orig_rxfectrl1);
	}

	phy_utils_phyreg_exit(pi);
	wlapi_enable_mac(pi->sh->physhim);
}

void
wlc_phy_afeclkswitch_sifs_delay(phy_info_t *pi)
{
	uint16 sifs_rx_tx_tx = 0x3e3e, sifs_nav_tx = 0x23e;
	uint8 stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);

	if (stall_val == 0)
		ACPHY_DISABLE_STALL(pi);

	if (CHSPEC_IS80(pi->radio_chanspec)) {
		uint16 epaPwrupDly;

		W_REG(pi->sh->osh, &pi->regs->PHYREF_IFS_SIFS_RX_TX_TX,
		      sifs_rx_tx_tx - ((TXDELAY_BW80 *8)<<8 | (TXDELAY_BW80 *8)));
		W_REG(pi->sh->osh, &pi->regs->PHYREF_IFS_SIFS_NAV_TX,
		      sifs_nav_tx - (TXDELAY_BW80 *8));
		WRITE_PHYREG(pi, TxRealFrameDelay, 186 + TXDELAY_BW80 * 80);
		WRITE_PHYREG(pi, TxMacIfHoldOff, TXMAC_IFHOLDOFF_DEFAULT + TXDELAY_BW80 * 2);
		WRITE_PHYREG(pi, TxMacDelay, TXMAC_MACDELAY_DEFAULT + TXDELAY_BW80 * 80);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
		      16, rfseq_rx2tx_cmd_afeclkswitch);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
		      16, rfseq_rx2tx_cmd_afeclkswitch_dly);
		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x79, 16, &epaPwrupDly);
		epaPwrupDly = epaPwrupDly + TXDELAY_BW80 * 1000/25;
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x79,
		      16, &epaPwrupDly);
	} else {
		W_REG(pi->sh->osh, &pi->regs->PHYREF_IFS_SIFS_RX_TX_TX, sifs_rx_tx_tx);
		W_REG(pi->sh->osh, &pi->regs->PHYREF_IFS_SIFS_NAV_TX, sifs_nav_tx);
		WRITE_PHYREG(pi, TxRealFrameDelay, 186);
		WRITE_PHYREG(pi, TxMacIfHoldOff, TXMAC_IFHOLDOFF_DEFAULT);
		WRITE_PHYREG(pi, TxMacDelay, TXMAC_MACDELAY_DEFAULT);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x00,
		      16, rfseq_rx2tx_cmd_noafeclkswitch);
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 16, 0x70,
		      16, rfseq_rx2tx_cmd_noafeclkswitch_dly);
	}
	ACPHY_ENABLE_STALL(pi, stall_val);
}

static void
phy_ac_lp_enable(phy_info_t *pi)
{
	/* Enable memory standby based on lpflags */
	if ((pi->sromi->lpflags & LPFLAGS_PHY_GLOBAL_DISABLE) ||
		(pi->sromi->lpflags & LPFLAGS_PHY_LP_DISABLE)) {
		PHY_TRACE(("%s: phy lp disable\n", __FUNCTION__));
		goto exit;
	}

	if (!(pi->sromi->lpflags & LPFLAGS_PSM_PHY_CTL)) {
		AND_REG(pi->sh->osh, &pi->regs->psm_phy_hdr_param, 0x7fff);
	}

	PHY_TRACE(("%s: phy lp enable\n", __FUNCTION__));

	/* Disable xtal clocks */
	MOD_RADIO_REG_20695_MULTI_3(pi, RFP, XTAL6, 0, xtal_pu_btfmdig, 0x0,
			xtal_pu_caldrv, 0x0, xtal_pu_OFFCHIP, 0x0);
	MOD_RADIO_REG_20695_MULTI_2(pi, RFP, XTAL5, 0, xtal_pu_serdes, 0,
			xtal_pu_RCCAL, 0);
	MOD_RADIO_REG_20695_MULTI_2(pi, RFP, XTAL8, 0, xtal_pu_pfddrv_auxcore, 0,
			xtal_pu_caldrv_auxcore, 0);
	MOD_RADIO_REG_20695(pi, RFP, XTAL4, 0, xtal_pu_BT, 0);

exit:
	return;
}

uint8 *
BCMRAMFN(get_avvmid_set_36)(phy_info_t *pi, uint16 pdet_range_id, uint16 subband_id)
{
	BCM_REFERENCE(pi);
	return &avvmid_set_maj36[pdet_range_id][subband_id][0];
}

uint8 *
BCMRAMFN(get_avvmid_set_40)(phy_info_t *pi, uint16 pdet_range_id, uint16 subband_id)
{
	BCM_REFERENCE(pi);
	return &avvmid_set_maj40[pdet_range_id][subband_id][0];
}

void
wlc_phy_tx_farrow_mu_setup(phy_info_t *pi, uint16 MuDelta_l, uint16 MuDelta_u, uint16 MuDeltaInit_l,
	uint16 MuDeltaInit_u)
{
	ACPHYREG_BCAST(pi, TxResamplerMuDelta0l, MuDelta_l);
	ACPHYREG_BCAST(pi, TxResamplerMuDelta0u, MuDelta_u);
	ACPHYREG_BCAST(pi, TxResamplerMuDeltaInit0l, MuDeltaInit_l);
	ACPHYREG_BCAST(pi, TxResamplerMuDeltaInit0u, MuDeltaInit_u);
}

static void
phy_ac_chanmgr_write_tx_farrow_tiny(phy_info_t *pi, chanspec_t chanspec,
	chanspec_t chanspec_sc, int sc_mode)
{
	uint8	ch, afe_clk_num, afe_clk_den, core;
	uint16	a, b;
	uint32	fcw, tmp_low = 0, tmp_high = 0;
	uint32	fc;
	chanspec_t chanspec_sel;
	phy_ac_chanmgr_info_t *chanmgri = pi->u.pi_acphy->chanmgri;
	bool vco_12GHz_in5G = (chanmgri->vco_12GHz &&
		CHSPEC_IS5G(pi->radio_chanspec));
	bool  suspend = !(R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC);

	if (sc_mode == 1) {
		chanspec_sel = chanspec_sc;
	} else {
		chanspec_sel = chanspec;
	}

	fc = wf_channel2mhz(CHSPEC_CHANNEL(chanspec_sel), CHSPEC_IS2G(chanspec_sel) ?
	                    WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);

	if ((!suspend) && (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev)))
		wlapi_suspend_mac_and_wait(pi->sh->physhim);
	if (pi->u.pi_acphy->dac_mode == 1) {
		if (CHSPEC_IS20(chanspec_sel)) {
			if ((phy_ac_radio_get_data(pi->u.pi_acphy->radioi)->vcodivmode & 0x1) ||
				vco_12GHz_in5G) {
				a = 16;
			} else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev)) {
				if (fc <= 5320 && fc >= 5180) {
					/* dac_div_ratio = 12 */
					a = 12;
#ifdef PHY_CORE2CORESYC //WAR: core2core requires to fix dac_div_ratio
				} else if (fc <= 5825 && fc >= 5745) {
					/* dac_div_ratio = 15 */
					a = 15;
#endif
				} else {
					/* dac_div_ratio = 16 */
					a = 16;
				}
			} else
				a = 18;
			b = 160;
		} else if (CHSPEC_IS40(chanspec_sel)) {
			if ((phy_ac_radio_get_data(pi->u.pi_acphy->radioi)->vcodivmode & 0x2) ||
				vco_12GHz_in5G) {
				a = 8;
			} else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev)) {
#ifdef PHY_CORE2CORESYC
				if ((fc == 5190 || fc == 5230)) {
					/* dac_div_ratio = 9 */
					a = 9;
				} else
#endif
				{
					/* dac_div_ratio = 8 */
					a = 8;
				}
			} else
				a = 9;
			b = 320;
		} else if (CHSPEC_IS80(chanspec_sel) ||
				PHY_AS_80P80(pi, pi->radio_chanspec)) {
			a = 6;
			if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev)) {
#ifdef PHY_CORE2CORESYC
				if (fc == 5210) {
					/* dac_div_ratio = 7 */
					a = 7;
				} else
#endif
				{
					/* dac_div_ratio = 6 */
					a = 6;
				}
			}
			b = 640;
		} else if (CHSPEC_IS160(chanspec_sel)) {
			a = 6;
			b = 640;
			ASSERT(0);
		} else {
			a = 6;
			b = 640;
		}

		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev)) {
			if (sc_mode == 1) {
				MOD_RADIO_REG_20693(pi, CLK_DIV_OVR1, 3, ovr_afe_div_dac, 1);
				MOD_RADIO_REG_20693(pi, CLK_DIV_CFG1, 3, sel_dac_div, a);
			} else {
				MOD_RADIO_ALLREG_20693(pi, CLK_DIV_OVR1, ovr_afe_div_dac, 1);
				MOD_RADIO_ALLREG_20693(pi, CLK_DIV_CFG1, sel_dac_div, a);
			}
		}
	} else if (pi->u.pi_acphy->dac_mode == 2) {
		a = 6;
		b = 640;
	} else {
		if (CHSPEC_IS80(chanspec_sel) || (CHSPEC_IS8080(chanspec_sel) &&
			!ACMAJORREV_33(pi->pubpi->phy_rev))) {
			a = 6;
			b = 640;
		} else {
			a = 8;
			b = 320;
		}
	}
	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		afe_clk_num = 2;
		afe_clk_den = 3;
	} else {
		afe_clk_num = 3;
		afe_clk_den = 2;
#if !defined(MACOSX)
		if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) &&
				(CHSPEC_IS20(pi->radio_chanspec) ||
				CHSPEC_IS40(pi->radio_chanspec)) &&
				!PHY_IPA(pi) && !ROUTER_4349(pi)) {
				afe_clk_den = 3;
			}
		}
#endif /* MACOSX */
	}
	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
		const uint8 afeclkdiv_arr[] = {2, 16, 4, 8, 3, 24, 6, 12};
		const uint8 dacclkdiv_arr[] = {6, 8, 9, 16, 18, 32, 64, 10};
		const uint8 dacdiv_arr[] = {1, 2, 3, 4};
		const chan_info_radio20693_altclkplan_t *altclkpln = altclkpln_radio20693;
		int row;
		if (ROUTER_4349(pi)) {
			altclkpln = altclkpln_radio20693_router4349;
		}
		row = wlc_phy_radio20693_altclkpln_get_chan_row(pi);

		if ((row >= 0) && (pi->u.pi_acphy->chanmgri->data.fast_adc_en == 0)) {
			a = 1;
			afe_clk_num = afeclkdiv_arr[altclkpln[row].afeclkdiv] *
				dacclkdiv_arr[altclkpln[row].dacclkdiv] *
				dacdiv_arr[altclkpln[row].dacdiv];
			afe_clk_den = CHSPEC_IS2G(pi->radio_chanspec) ? 8 : 4;
		}
	}
	/* bits_in_mu = 23 */
	if (ACMAJORREV_33(pi->pubpi->phy_rev) && PHY_AS_80P80(pi, chanspec)) {
		uint8 chans[NUM_CHANS_IN_CHAN_BONDING];

		wf_chspec_get_80p80_channels(chanspec, chans);

		FOREACH_CORE(pi, core) {
			/* core 0/1: 80L, core 2/3 80U */
			ch = (core <= 1) ? chans[0] : chans[1];
			fc = wf_channel2mhz(ch, WF_CHAN_FACTOR_5_G);
			PHY_INFORM(("%s: core=%d, fc=%d\n", __FUNCTION__, core, fc));

			bcm_uint64_multiple_add(&tmp_high, &tmp_low, a * afe_clk_num * b,
				1 << 23, (fc * afe_clk_den) >> 1);
			bcm_uint64_divide(&fcw, tmp_high, tmp_low, fc * afe_clk_den);

			switch (core) {
			case 0:
				WRITE_PHYREG(pi, TxResamplerMuDelta0l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDelta0u,
						(fcw & 0xff0000) >> 16);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit0l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit0u,
						(fcw & 0xff0000) >> 16);
				break;
			case 1:
				WRITE_PHYREG(pi, TxResamplerMuDelta1l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDelta1u,
						(fcw & 0xff0000) >> 16);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit1l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit1u,
						(fcw & 0xff0000) >> 16);
				break;
			case 2:
				WRITE_PHYREG(pi, TxResamplerMuDelta2l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDelta2u,
						(fcw & 0xff0000) >> 16);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit2l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit2u,
						(fcw & 0xff0000) >> 16);
				break;
			case 3:
				WRITE_PHYREG(pi, TxResamplerMuDelta3l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDelta3u,
						(fcw & 0xff0000) >> 16);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit3l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit3u,
						(fcw & 0xff0000) >> 16);
				break;
			default:
				PHY_ERROR(("wl%d: %s: Max 4 cores only!\n",
						pi->sh->unit, __FUNCTION__));
				ASSERT(0);
			}
		}
	} else if (CHSPEC_IS8080(chanspec)) {
		FOREACH_CORE(pi, core) {
			if (core == 0) {
				ch = wf_chspec_primary80_channel(chanspec);
				fc = wf_channel2mhz(ch, WF_CHAN_FACTOR_5_G);

				bcm_uint64_multiple_add(&tmp_high, &tmp_low, a * afe_clk_num * b,
					1 << 23, (fc * afe_clk_den) >> 1);
				bcm_uint64_divide(&fcw, tmp_high, tmp_low, fc * afe_clk_den);

				WRITE_PHYREG(pi, TxResamplerMuDelta0l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDelta0u, (fcw & 0xff0000) >> 16);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit0l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit0u, (fcw & 0xff0000) >> 16);
			} else if (core == 1) {
				ch = wf_chspec_secondary80_channel(chanspec);
				fc = wf_channel2mhz(ch, WF_CHAN_FACTOR_5_G);

				bcm_uint64_multiple_add(&tmp_high, &tmp_low, a * afe_clk_num * b,
					1 << 23, (fc * afe_clk_den) >> 1);
				bcm_uint64_divide(&fcw, tmp_high, tmp_low, fc * afe_clk_den);

				WRITE_PHYREG(pi, TxResamplerMuDelta1l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDelta1u, (fcw & 0xff0000) >> 16);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit1l, fcw & 0xffff);
				WRITE_PHYREG(pi, TxResamplerMuDeltaInit1u, (fcw & 0xff0000) >> 16);
			}
		}
	} else {
		if (sc_mode == 1) {
			ch = CHSPEC_CHANNEL(chanspec_sel);
			fc = wf_channel2mhz(ch, CHSPEC_IS2G(chanmgri->radio_chanspec_sc) ?
				WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
		} else {
			ch = CHSPEC_CHANNEL(chanspec_sel);
			fc = wf_channel2mhz(ch, CHSPEC_IS2G(pi->radio_chanspec) ?
				WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
		}
		//ch = CHSPEC_CHANNEL(chanspec);
		//fc = wf_channel2mhz(ch, CHSPEC_IS2G(pi->radio_chanspec) ?
		//	WF_CHAN_FACTOR_2_4_G : WF_CHAN_FACTOR_5_G);
		bcm_uint64_multiple_add(&tmp_high, &tmp_low, a * afe_clk_num * b,
			1 << 23, (fc * afe_clk_den) >> 1);
		bcm_uint64_divide(&fcw, tmp_high, tmp_low, fc * afe_clk_den);
		if (sc_mode == 1) {
			WRITE_PHYREG(pi, TxResamplerMuDelta3l, fcw & 0xffff);
			WRITE_PHYREG(pi, TxResamplerMuDelta3u, (fcw & 0xff0000) >> 16);
			WRITE_PHYREG(pi, TxResamplerMuDeltaInit3l, fcw & 0xffff);
			WRITE_PHYREG(pi, TxResamplerMuDeltaInit3u, (fcw & 0xff0000) >> 16);
		} else {
			wlc_phy_tx_farrow_mu_setup(pi, fcw & 0xffff, (fcw & 0xff0000) >> 16,
				fcw & 0xffff, (fcw & 0xff0000) >> 16);
		}
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev)) {
		wlc_phy_force_rfseq_acphy(pi, ACPHY_RFSEQ_RX2TX);
		wlc_phy_force_rfseq_acphy(pi, ACPHY_RFSEQ_TX2RX);
		wlc_phy_resetcca_acphy(pi);
		if (!suspend)
			wlapi_enable_mac(pi->sh->physhim);
	}
}

bool
BCMATTACHFN(wlc_phy_attach_farrow)(phy_info_t *pi)
{
	int num_bw;
#ifndef ACPHY_1X1_ONLY
	phy_info_acphy_t *pi_ht = (phy_info_acphy_t *)pi->u.pi_acphy;
#endif
	const chan_info_tx_farrow(*tx_farrow) [ACPHY_NUM_CHANS];
	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	pi->u.pi_acphy->tx_farrow = NULL;
	pi->u.pi_acphy->rx_farrow = NULL;
#ifdef ACPHY_1X1_ONLY
	num_bw = 1;
#else
	num_bw = ACPHY_NUM_BW;
#endif

	if ((pi->u.pi_acphy->tx_farrow =
	     phy_malloc(pi,
	            num_bw * sizeof(chan_info_tx_farrow[ACPHY_NUM_CHANS]))) == NULL) {
		PHY_ERROR(("wl%d: %s: out of memory, malloced %d bytes\n", pi->sh->unit,
		           __FUNCTION__, MALLOCED(pi->sh->osh)));
		return FALSE;
	}

	if (!TINY_RADIO(pi) && (!(ACMAJORREV_36(pi->pubpi->phy_rev)))) {
		const chan_info_rx_farrow(*rx_farrow) [ACPHY_NUM_CHANS];
		/* TINY RADIO does not have an rx farrow table */
		if ((pi->u.pi_acphy->rx_farrow =
		     phy_malloc(pi,
		            num_bw * sizeof(chan_info_rx_farrow[ACPHY_NUM_CHANS]))) == NULL) {
			PHY_ERROR(("wl%d: %s: out of memory, malloced %d bytes\n", pi->sh->unit,
			           __FUNCTION__, MALLOCED(pi->sh->osh)));
			return FALSE;
		}

		rx_farrow = rx_farrow_tbl;
		memcpy(pi->u.pi_acphy->rx_farrow, rx_farrow,
		       ACPHY_NUM_CHANS * num_bw * sizeof(chan_info_rx_farrow));
	}


#ifdef ACPHY_1X1_ONLY
	ASSERT(((phy_info_acphy_t *)pi->u.pi_acphy)->dac_mode == 1);
	if (!(ACMAJORREV_36(pi->pubpi->phy_rev))) {
		tx_farrow = tx_farrow_dac1_tbl;
	}
#else /* ACPHY_1X1_ONLY */
	switch (pi_ht->dac_mode) {
	case 2:
		tx_farrow = tx_farrow_dac2_tbl;
		break;
	case 3:
		tx_farrow = tx_farrow_dac3_tbl;
		break;
	case 1:
	default:
		/* default to dac_mode 1 */
		tx_farrow = tx_farrow_dac1_tbl;
		break;
	}
#endif /* ACPHY_1X1_ONLY */
	if (!(ACMAJORREV_36(pi->pubpi->phy_rev))) {
		memcpy(pi->u.pi_acphy->tx_farrow, tx_farrow,
		       ACPHY_NUM_CHANS * num_bw * sizeof(chan_info_tx_farrow));
	}
	return TRUE;
}

#define TINY_GET_ADC_MODE(pi, chanspec)		\
	((CHSPEC_IS20(chanspec) || CHSPEC_IS40(chanspec)) ?	\
	(pi->u.pi_acphy->chanmgri->use_fast_adc_20_40) : 1)
void
wlc_phy_farrow_setup_tiny(phy_info_t *pi, chanspec_t chanspec)
{
	/* Setup adc mode based on BW */
	pi->u.pi_acphy->chanmgri->data.fast_adc_en = TINY_GET_ADC_MODE(pi, chanspec);

	phy_ac_chanmgr_write_tx_farrow_tiny(pi, chanspec, 0, 0);
	phy_ac_chanmgr_write_rx_farrow_tiny(pi, chanspec, 0, 0);

	/* Enable the Tx resampler on all cores */
	MOD_PHYREG(pi, TxResamplerEnable0, enable_tx, 1);
}

void
phy_ac_chanmgr_set_phymode(phy_info_t *pi, chanspec_t chanspec, chanspec_t chanspec_sc,
	uint16 phymode)
{
	uint8 ch[NUM_CHANS_IN_CHAN_BONDING];
	uint8 stall_val = 0;
	uint16 classifier_state = 0;
	uint8 orig_rxfectrl1 = 0;

	wlapi_suspend_mac_and_wait(pi->sh->physhim);
	/* Disable classifier */
	classifier_state = READ_PHYREG(pi, ClassifierCtrl);
	wlc_phy_classifier_acphy(pi, ACPHY_ClassifierCtrl_classifierSel_MASK, 4);

	/* Disable stalls and hold FIFOs in reset */
	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	orig_rxfectrl1 = READ_PHYREGFLD(pi, RxFeCtrl1, soft_sdfeFifoReset);
	ACPHY_DISABLE_STALL(pi);
	MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);
	switch (phymode) {
	case  PHYMODE_3x3_1x1:
		ch[0] = CHSPEC_CHANNEL(chanspec);
		ch[1] = CHSPEC_CHANNEL(chanspec_sc);
		wlc_phy_radio20693_afe_clkdistribtion_mode(pi, 2);
		phy_ac_radio_20693_pmu_pll_config_wave2(pi, 5);
		phy_ac_radio_20693_chanspec_setup(pi, ch[0], 0, 0, 0);
		phy_ac_radio_20693_chanspec_setup(pi, ch[1], 0, 1, 0);
		wlc_phy_radio2069x_vcocal_isdone(pi, FALSE, FALSE);
		wlc_phy_radio20693_sel_logen_5g_mode(pi, 2);
		/* Setup adc mode based on BW */
		pi->u.pi_acphy->chanmgri->data.fast_adc_en = TINY_GET_ADC_MODE(pi, chanspec);
		phy_ac_chanmgr_write_tx_farrow_tiny(pi, chanspec, chanspec_sc, 1);
		phy_ac_chanmgr_write_rx_farrow_tiny(pi, chanspec, chanspec_sc, 1);
		/* Enable the Tx resampler on all cores */
		MOD_PHYREG(pi, TxResamplerEnable3, enable_tx, 1);

		MOD_PHYREG(pi, RfseqMode, CoreActv_override_percore, 8);
		MOD_PHYREG(pi, RfseqCoreActv2059, EnRx, 0xf);
		MOD_PHYREG(pi, RfseqCoreActv2059, EnTx, 0x7);
		MOD_PHYREG(pi, CoreConfig, CoreMask, 7);
		MOD_PHYREG(pi, CoreConfig, pasvRxSampCapOn, 0);
		MOD_PHYREG(pi, CoreConfig, pasvRxCoreMask, 8);
		break;
	case 0:
		MOD_RADIO_PLLREG_20693(pi, PLL_CFG1, 1, rfpll_synth_pu, 0);
		MOD_RADIO_PLLREG_20693(pi, PLL_CP1, 1, rfpll_cp_pu, 0);
		MOD_RADIO_PLLREG_20693(pi, PLL_CFG1, 1, rfpll_vco_pu, 0);
		MOD_RADIO_PLLREG_20693(pi, PLL_CFG1, 1, rfpll_vco_buf_pu, 0);
		MOD_RADIO_PLLREG_20693(pi, PLL_CFG1, 1, rfpll_monitor_pu, 0);
		MOD_RADIO_PLLREG_20693(pi, PLL_LF6, 1, rfpll_lf_cm_pu, 0);
		MOD_RADIO_PLLREG_20693(pi, PLL_CFG1, 1, rfpll_pfd_en, 0);
		MOD_RADIO_PLLREG_20693(pi, RFPLL_OVR1, 1, ovr_rfpll_synth_pu, 1);
		MOD_RADIO_PLLREG_20693(pi, RFPLL_OVR1, 1, ovr_rfpll_cp_pu, 1);
		MOD_RADIO_PLLREG_20693(pi, RFPLL_OVR1, 1, ovr_rfpll_vco_pu, 1);
		MOD_RADIO_PLLREG_20693(pi, RFPLL_OVR1, 1, ovr_rfpll_vco_buf_pu, 1);
		MOD_RADIO_PLLREG_20693(pi, PLL_HVLDO1, 1, ldo_2p5_pu_ldo_CP, 0);
		MOD_RADIO_PLLREG_20693(pi, PLL_HVLDO1, 1, ldo_2p5_pu_ldo_VCO, 0);
		MOD_RADIO_PLLREG_20693(pi, RFPLL_OVR1, 1, ovr_ldo_2p5_pu_ldo_CP, 1);
		MOD_RADIO_PLLREG_20693(pi, RFPLL_OVR1, 1, ovr_ldo_2p5_pu_ldo_VCO, 1);
		wlc_phy_radio20693_afe_clkdistribtion_mode(pi, 0);
		wlc_phy_radio20693_sel_logen_5g_mode(pi, 0);

		/* Setup adc mode based on BW */
		pi->u.pi_acphy->chanmgri->data.fast_adc_en = TINY_GET_ADC_MODE(pi, chanspec);
		phy_ac_chanmgr_write_tx_farrow_tiny(pi, chanspec, chanspec_sc, 0);
		phy_ac_chanmgr_write_rx_farrow_tiny(pi, chanspec, chanspec_sc, 0);
		/* Enable the Tx resampler on all cores */
		MOD_PHYREG(pi, TxResamplerEnable0, enable_tx, 1);

		MOD_PHYREG(pi, RfseqMode, CoreActv_override_percore, 0);
		MOD_PHYREG(pi, RfseqCoreActv2059, EnRx, 0xf);
		MOD_PHYREG(pi, RfseqCoreActv2059, EnTx, 0xf);
		MOD_PHYREG(pi, CoreConfig, CoreMask, 0xf);
		MOD_PHYREG(pi, CoreConfig, pasvRxSampCapOn, 0);
		MOD_PHYREG(pi, CoreConfig, pasvRxCoreMask, 0);
		break;
	default:
		PHY_ERROR(("wl%d: %s: Unsupported radio revision %d\n",
			pi->sh->unit, __FUNCTION__, RADIOREV(pi->pubpi->radiorev)));
		ASSERT(0);
	}
	/* Restore FIFO reset and Stalls */
	ACPHY_ENABLE_STALL(pi, stall_val);
	MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, orig_rxfectrl1);
	OSL_DELAY(1);

	/* Restore classifier */
	WRITE_PHYREG(pi, ClassifierCtrl, classifier_state);
	OSL_DELAY(1);

	/* Reset PHY */
	wlc_phy_resetcca_acphy(pi);
	wlapi_enable_mac(pi->sh->physhim);
	switch (phymode) {
		case  PHYMODE_3x3_1x1:
			MOD_PHYREG(pi, AntDivConfig2059, Trigger_override_per_core, 8);
			MOD_PHYREG(pi, RadarSearchCtrl_SC, radarEnable, 1);
			MOD_PHYREG(pi, RadarDetectConfig3_SC, scan_mode, 1);
			MOD_PHYREG(pi, RadarDetectConfig3_SC, gain_override_en, 1);
			MOD_PHYREG(pi, RadarBlankCtrl2_SC, blank_mode, 1);
			break;
		case 0:
			MOD_PHYREG(pi, AntDivConfig2059, Trigger_override_per_core, 0);
			MOD_PHYREG(pi, RadarSearchCtrl_SC, radarEnable, 0);
			MOD_PHYREG(pi, RadarDetectConfig3_SC, scan_mode, 0);
			MOD_PHYREG(pi, RadarDetectConfig3_SC, gain_override_en, 0);
			MOD_PHYREG(pi, RadarBlankCtrl2_SC, blank_mode, 0);
			break;
		default:
			PHY_ERROR(("wl%d: %s: Unsupported radio revision %d\n",
			         pi->sh->unit, __FUNCTION__, RADIOREV(pi->pubpi->radiorev)));
			ASSERT(0);
	}

}

void
BCMATTACHFN(wlc_phy_nvram_avvmid_read)(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;
	uint8 i, j, ant;
	uint8 core;
	char phy_var_name[20];
	/*	phy_info_acphy_t *pi_ac = pi->u.pi_acphy; */
	FOREACH_CORE(pi, core) {
		ant = phy_get_rsdbbrd_corenum(pi, core);
		(void)snprintf(phy_var_name, sizeof(phy_var_name), rstr_AvVmid_cD, ant);
		if ((PHY_GETVAR_SLICE(pi, phy_var_name)) != NULL) {
			for (i = 0; i < ACPHY_NUM_BANDS; i++) {
				for (j = 0; j < ACPHY_AVVMID_NVRAM_PARAMS; j++) {
					pi_ac->sromi->avvmid_set_from_nvram[ant][i][j] =
						(uint8) PHY_GETINTVAR_ARRAY_SLICE(pi, phy_var_name,
						(ACPHY_AVVMID_NVRAM_PARAMS*i +j));
				}
			}
		}
	}
}

#if (defined(BCMINTERNAL) || defined(WLTEST))
void wlc_phy_get_avvmid_acphy(phy_info_t *pi, int32 *ret_int_ptr, wlc_avvmid_t avvmid_type,
		uint8 *core_sub_band)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	uint8 avvmid_idx = 0;
	uint8 band_idx = core_sub_band[1];
	uint8 core = core_sub_band[0];
	avvmid_idx = (avvmid_type == AV) ? 0 : 1;
	*ret_int_ptr = (int32)(pi_ac->sromi->avvmid_set_from_nvram[core][band_idx][avvmid_idx]);
	return;
}

void wlc_phy_set_avvmid_acphy(phy_info_t *pi, uint8 *avvmid, wlc_avvmid_t avvmid_type)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	uint8 core, sub_band_idx;
	uint8 avvmid_idx = 0;
	avvmid_idx = (avvmid_type == AV) ? 0 : 1;
	core = avvmid[0];
	sub_band_idx = avvmid[1];
	pi_ac->sromi->avvmid_set_from_nvram[core][sub_band_idx][avvmid_idx] = avvmid[2];
	/* Load Pdet related settings */
	wlc_phy_set_pdet_on_reset_acphy(pi);
}
#endif /* defined(BCMINTERNAL) || defined(WLTEST) */

void BCMATTACHFN(wlc_phy_nvram_vlin_params_read)(phy_info_t *pi)
{

	char phy_var_name2[20], phy_var_name3[20];
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
	uint8 core, ant;
	FOREACH_CORE(pi, core) {
		ant = phy_get_rsdbbrd_corenum(pi, core);
		if ((!TINY_RADIO(pi)) && BF3_VLIN_EN_FROM_NVRAM(pi_ac)) {
			(void)snprintf(phy_var_name2, sizeof(phy_var_name2),
				rstr_VlinPwr2g_cD, ant);
			if ((PHY_GETVAR_SLICE(pi, phy_var_name2)) != NULL) {
				pi_ac->chanmgri->cfg.vlinpwr2g_from_nvram =
					(uint8) PHY_GETINTVAR_SLICE(pi, phy_var_name2);
				}
			(void)snprintf(phy_var_name2, sizeof(phy_var_name2),
				rstr_VlinPwr5g_cD, ant);
			if ((PHY_GETVAR_SLICE(pi, phy_var_name2)) != NULL) {
				pi_ac->chanmgri->cfg.vlinpwr5g_from_nvram =
					(uint8) PHY_GETINTVAR_SLICE(pi, phy_var_name2);
				}
			(void)snprintf(phy_var_name3, sizeof(phy_var_name3),
				rstr_Vlinmask2g_cD, ant);
			if ((PHY_GETVAR_SLICE(pi, phy_var_name3)) != NULL) {
				pi_ac->chanmgri->data.vlinmask2g_from_nvram =
					(uint8) PHY_GETINTVAR_SLICE(pi, phy_var_name3);
				}
			(void)snprintf(phy_var_name3, sizeof(phy_var_name3),
				rstr_Vlinmask5g_cD, ant);
			if ((PHY_GETVAR_SLICE(pi, phy_var_name3)) != NULL) {
				pi_ac->chanmgri->data.vlinmask5g_from_nvram =
					(uint8) PHY_GETINTVAR_SLICE(pi, phy_var_name3);
				}
			}
		}
}

static void
wlc_tiny_setup_coarse_dcc(phy_info_t *pi)
{
	uint8 phybw;
	uint8 core;

	/*
	 * Settings required to use the RFSeq to trigger the coarse DCC
	 * 4345TC Not used. 20691_coarse_dcc used
	 * 4345A0 offset comparator has hysteresis and dc offset but is adequate for 5G
	 * 4365-analog DCC
	 */

	if ((!ACMAJORREV_4(pi->pubpi->phy_rev)) && (!ACMAJORREV_32(pi->pubpi->phy_rev)) &&
		(!ACMAJORREV_33(pi->pubpi->phy_rev))) {
		wlc_tiny_dc_static_WAR(pi);
	}

	/* DCC FSM Defaults */
	MOD_PHYREG(pi, BBConfig, dcc_wakeup_restart_en, 0);
	MOD_PHYREG(pi, BBConfig, dcc_wakeup_restart_delay, 10);

	/* Control via pktproc, instead of RFSEQ */
	MOD_PHYREG(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl,  1);

	FOREACH_CORE(pi, core) {

		/* Disable overrides that may have been set during 2G cal */
		MOD_RADIO_REG_TINY(pi, RX_BB_2G_OVR_EAST, core, ovr_tia_offset_dac_pwrup, 0);
		MOD_RADIO_REG_TINY(pi, RX_BB_2G_OVR_EAST, core, ovr_tia_offset_dac, 0);
		if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev)) {
			MOD_RADIO_REG_20693(pi, RX_BB_2G_OVR_EAST, core,
				ovr_tia_offset_comp_pwrup, 0);
		} else {
			MOD_RADIO_REG_TINY(pi, RX_BB_2G_OVR_NORTH, core,
				ovr_tia_offset_comp_pwrup, 0);
		}
		MOD_RADIO_REG_TINY(pi, RX_BB_2G_OVR_EAST, core, ovr_tia_offset_dac, 0);
		MOD_RADIO_REG_TINY(pi, TIA_CFG8, core, tia_offset_comp_drive_strength, 1);

		/* Set idac LSB to (50nA * 4) ~ 0.2uA for 2G, (50nA * 12) ~ 0.6 uA for 5G */
		/* changed biasadj to 1 as coupled d.c. in loop is very less. */
		if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			MOD_RADIO_REG_TINY(pi, TIA_CFG8, core, tia_offset_dac_biasadj,
			(CHSPEC_IS2G(pi->radio_chanspec)) ? 1 : 1);
		} else if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
			MOD_RADIO_REG_TINY(pi, TIA_CFG8, core, tia_offset_dac_biasadj,
			(CHSPEC_IS2G(pi->radio_chanspec)) ? 12 : 4);
		} else {
			MOD_RADIO_REG_TINY(pi, TIA_CFG8, core, tia_offset_dac_biasadj,
			(CHSPEC_IS2G(pi->radio_chanspec)) ? 4 : 12);
		}
	}
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, rx_tia_dc_loop_0, dac_sign, 1);
		MOD_PHYREG(pi, rx_tia_dc_loop_0, en_lock, 1);
		if (phy_get_phymode(pi) != PHYMODE_RSDB) {
			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, rx_tia_dc_loop_1, dac_sign, 1)
				MOD_PHYREG_ENTRY(pi, rx_tia_dc_loop_1, en_lock, 1)
				MOD_PHYREG_ENTRY(pi, rx_tia_dc_loop_1, restart_gear, 6)
			ACPHY_REG_LIST_EXECUTE(pi);
		}
	}
	/* Set minimum idle gain incase of restart */
	MOD_PHYREG(pi, rx_tia_dc_loop_0, restart_gear, 6);

	/* 4365-analog DCC: loop through the cores */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
		/* Loop through cores */
		FOREACH_CORE(pi, core) {
			MOD_PHYREGCE(pi, rx_tia_dc_loop_, core, dac_sign, 1);
			MOD_PHYREGCE(pi, rx_tia_dc_loop_, core, en_lock, 1);
			MOD_PHYREGCE(pi, rx_tia_dc_loop_, core, restart_gear, 6);
		}
	}

	if (IS20MHZ(pi))
		phybw = 0;
	else if (IS40MHZ(pi))
		phybw = 1;
	else
		phybw = 2;

	/*
	 * Because FSM clock is PHY_BW dependant scale gear gain and loop count.
	 *
	 * Settings below assume:
	 *	9 DCC FSM clock cycle latency and single pole  RC filter >=2MHz ala 4345B0.
	 * (Valid also for 4345A0).
	 */
	MOD_PHYREG(pi, rx_tia_dc_loop_gain_0, loop_gain_0, 15); /* disable */
	MOD_PHYREG(pi, rx_tia_dc_loop_gain_1, loop_gain_1, 2 + phybw);
	MOD_PHYREG(pi, rx_tia_dc_loop_gain_2, loop_gain_2, 4 + phybw);
	MOD_PHYREG(pi, rx_tia_dc_loop_gain_3, loop_gain_3, 5 + phybw);
	MOD_PHYREG(pi, rx_tia_dc_loop_gain_4, loop_gain_4, 6 + phybw);
	MOD_PHYREG(pi, rx_tia_dc_loop_gain_5, loop_gain_5, 8 + phybw);
	/* making Loop count of gear 1 because of CRDOT11ACPHY-1530 */
	MOD_PHYREG(pi, rx_tia_dc_loop_count_0, loop_count_0, 1); /* disable */
	MOD_PHYREG(pi, rx_tia_dc_loop_count_1, loop_count_1, (phybw > 1) ? 255 : (80 << phybw));
	MOD_PHYREG(pi, rx_tia_dc_loop_count_2, loop_count_2, (phybw > 1) ? 255 : (80 << phybw));
	MOD_PHYREG(pi, rx_tia_dc_loop_count_3, loop_count_3, (phybw > 1) ? 255 : (80 << phybw));
	MOD_PHYREG(pi, rx_tia_dc_loop_count_4, loop_count_4, (phybw > 1) ? 255 : (80 << phybw));
	MOD_PHYREG(pi, rx_tia_dc_loop_count_5, loop_count_5, (20 << phybw));

	if (ACMAJORREV_3(pi->pubpi->phy_rev))
		wlc_phy_enable_lna_dcc_comp_20691(pi, PHY_ILNA(pi));
}

void
wlc_phy_mlua_adjust_acphy(phy_info_t *pi, bool btactive)
{
	uint8 zfuA1, zfuA1_log2, zfuA2, zfuA2_log2;
	uint8 mluA1, mluA1_log2, mluA2, mluA2_log2;
	uint8 zfuA0 = 0, zfuA3 = 0;
	uint8 mluA0 = 0, mluA3 = 0;

	/* Disable this for now, there is some issue with BTcoex */
	if (btactive) {
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			mluA1 = 2; mluA1_log2 = 1; mluA2 = 2; mluA2_log2 = 0, mluA0 = 2, mluA3 = 2;
			zfuA1 = 2; zfuA1_log2 = 1; zfuA2 = 2; zfuA2_log2 = 1, zfuA0 = 2; zfuA3 = 2;
		} else {
			mluA1 = 2; mluA1_log2 = 1; mluA2 = 0; mluA2_log2 = 0;
			zfuA1 = 2; zfuA1_log2 = 1; zfuA2 = 2; zfuA2_log2 = 1;
		}
	} else {
		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			mluA1 = 4; mluA1_log2 = 2; mluA2 = 4; mluA2_log2 = 2; mluA0 = 4; mluA3 = 2;
			zfuA1 = 4; zfuA1_log2 = 2; zfuA2 = 4; zfuA2_log2 = 2; zfuA0 = 4; zfuA3 = 2;
		} else {
			mluA1 = 4; mluA1_log2 = 2; mluA2 = 4; mluA2_log2 = 2;
			zfuA1 = 4; zfuA1_log2 = 2; zfuA2 = 4; zfuA2_log2 = 2;
		}
	}

	/* Increase Channel Update ML mu */
	if (ACMAJORREV_0(pi->pubpi->phy_rev) && (ACMINORREV_0(pi) || ACMINORREV_1(pi))) {
		/* 4360 a0,b0 */
		MOD_PHYREG(pi, mluA, mluA1, mluA1);
		MOD_PHYREG(pi, mluA, mluA2, mluA2);
		/* zfuA register used to update channel for 256 QAM */
		MOD_PHYREG(pi, zfuA, zfuA1, zfuA1);
		MOD_PHYREG(pi, zfuA, zfuA2, zfuA2);
	} else if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		/* 4350 a0,b0 (log domain) */
		MOD_PHYREG(pi, mluA, mluA1, mluA1_log2);
		MOD_PHYREG(pi, mluA, mluA2, mluA2_log2);
		/* zfuA register used to update channel for 256 QAM */
		MOD_PHYREG(pi, zfuA, zfuA1, zfuA1_log2);
		MOD_PHYREG(pi, zfuA, zfuA2, zfuA2_log2);
	} else if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	           ACMAJORREV_37(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, mluA, mluA0, mluA0);
		MOD_PHYREG(pi, mluA, mluA1, mluA1);
		MOD_PHYREG(pi, mluA, mluA2, mluA2);
		MOD_PHYREG(pi, mluA, mluA3, mluA3);
		/* zfuA register used to update channel for 256 QAM */
		MOD_PHYREG(pi, zfuA, zfuA0, zfuA0);
		MOD_PHYREG(pi, zfuA, zfuA1, zfuA1);
		MOD_PHYREG(pi, zfuA, zfuA2, zfuA2);
		MOD_PHYREG(pi, zfuA, zfuA3, zfuA3);
	} else {
	}
}

void
phy_ac_chanmgr_cal_init(phy_info_t *pi, uint8 *enULB)
{
#ifdef WL11ULB
	/* Disable CRS min pwr cal in ULB mode
	 * For ULB mode, do crs cal at the end of the function
	 */
	if (PHY_ULB_ENAB(pi)) {
		if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			if (CHSPEC_IS10(pi->radio_chanspec) ||
					CHSPEC_IS5(pi->radio_chanspec) ||
					CHSPEC_IS2P5(pi->radio_chanspec)) {
				*enULB = 1;
				/* Disable ULB mode during cals */
				wlc_phy_ulb_mode(pi, PMU_ULB_BW_NONE);
			}
		}
	}
#endif /* WL11ULB */
	if (ACMAJORREV_2(pi->pubpi->phy_rev) && (!PHY_IPA(pi)) &&
	    (CHSPEC_IS80(pi->radio_chanspec)) &&
	    (pi->u.pi_acphy->chanmgri->cfg.srom_txnoBW80ClkSwitch == 0)) {
			wlc_phy_modify_txafediv_acphy(pi, 6);
	}
}

void
phy_ac_chanmgr_cal_reset(phy_info_t *pi)
{
	uint8 ch = CHSPEC_CHANNEL(pi->radio_chanspec);
	uint32 fc = wf_channel2mhz(ch, CHSPEC_IS5G(pi->radio_chanspec)
			    ? WF_CHAN_FACTOR_5_G : WF_CHAN_FACTOR_2_4_G);
	if (ACMAJORREV_2(pi->pubpi->phy_rev) && (!PHY_IPA(pi)) &&
		(pi->u.pi_acphy->chanmgri->cfg.srom_txnoBW80ClkSwitch == 0)) {
		wlc_phy_afeclkswitch_sifs_delay(pi);
		if (!(fc == 5210 || fc == 5290) && (CHSPEC_IS80(pi->radio_chanspec)))
			wlc_phy_modify_txafediv_acphy(pi, 9);
		else if (CHSPEC_IS80(pi->radio_chanspec))
			wlc_phy_modify_txafediv_acphy(pi, 6);
	}
}

#if defined(WLTEST)
static int
phy_ac_chanmgr_get_smth(phy_type_chanmgr_ctx_t *ctx, int32* ret_int_ptr)
{
	phy_ac_chanmgr_info_t *ci = (phy_ac_chanmgr_info_t *)ctx;
	if ((ACMAJORREV_1(ci->pi->pubpi->phy_rev) && ACMINORREV_2(ci->pi)) ||
	    ACMAJORREV_3(ci->pi->pubpi->phy_rev) || ACMAJORREV_4(ci->pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(ci->pi->pubpi->phy_rev) || ACMAJORREV_37(ci->pi->pubpi->phy_rev)) {
		*ret_int_ptr = ci->acphy_enable_smth;
		return BCME_OK;
	} else {
		PHY_ERROR(("Smth is not supported for this chip \n"));
		return BCME_UNSUPPORTED;
	}
}

static int
phy_ac_chanmgr_set_smth(phy_type_chanmgr_ctx_t *ctx, int8 int_val)
{
	phy_ac_chanmgr_info_t *ci = (phy_ac_chanmgr_info_t *)ctx;
	if ((ACMAJORREV_1(ci->pi->pubpi->phy_rev) && ACMINORREV_2(ci->pi)) ||
	    ACMAJORREV_3(ci->pi->pubpi->phy_rev) || ACMAJORREV_4(ci->pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(ci->pi->pubpi->phy_rev) || ACMAJORREV_37(ci->pi->pubpi->phy_rev)) {
		if ((int_val > SMTH_FOR_TXBF) || (int_val < SMTH_DISABLE)) {
			PHY_ERROR(("Smth %d is not supported \n", (uint16)int_val));
			return BCME_UNSUPPORTED;
		} else {
			wlc_phy_smth(ci->pi, int_val, SMTH_NODUMP);
			return BCME_OK;
		}
	} else {
		PHY_ERROR(("Smth is not supported for this chip \n"));
		return BCME_UNSUPPORTED;
	}
}
#endif /* defined(WLTEST) */

/* sets chanspec of scan core; returns error status */
int
phy_ac_chanmgr_set_val_sc_chspec(phy_ac_chanmgr_info_t *chanmgri, int32 set_val)
{
	chanmgri->radio_chanspec_sc = (chanspec_t)set_val;

	return BCME_OK;
}

/* gets scan core chanspec in pointer ret_val parameter passed; returns error status */
int
phy_ac_chanmgr_get_val_sc_chspec(phy_ac_chanmgr_info_t *chanmgri, int32 *ret_val)
{
	*ret_val = (int32)chanmgri->radio_chanspec_sc;
	return BCME_OK;
}

/* sets phymode; eg. to 3+1 or 4x4; returns error status */
int
phy_ac_chanmgr_set_val_phymode(phy_ac_chanmgr_info_t *chanmgri, int32 set_val)
{
	phy_info_t *pi = chanmgri->pi;

	phy_set_phymode(pi, (uint16) set_val);
	if (set_val == PHYMODE_3x3_1x1) {
		phy_ac_chanmgr_set_phymode(pi, pi->radio_chanspec,
				chanmgri->radio_chanspec_sc, (uint16) set_val);
	} else {
		phy_ac_chanmgr_set_phymode(pi, pi->radio_chanspec,
				0, (uint16) set_val);
	}
	return BCME_OK;
}

/* gets phymode in pointer ret_val parameter passed; returns error status */
int
phy_ac_chanmgr_get_val_phymode(phy_ac_chanmgr_info_t *chanmgri, int32 *ret_val)
{
	phy_info_t *pi = chanmgri->pi;

	*ret_val = (int32) phy_get_phymode(pi);
	return BCME_OK;
}

/*
 * Entry-point to enable (disable) driver-level PHY WARs when MU TX is enabled (disabled).
 * These will apply to all TX/RX frames once MU is active, including SU frames.
 */
void
phy_ac_chanmgr_mutx_war(wlc_phy_t *pih, bool enable)
{
	phy_info_t *pi = (phy_info_t*)pih;
	pi->u.pi_acphy->chanmgri->mutx_war_on = enable;
	if (ISACPHY(pi)) {
		if (ACMAJORREV_33(pi->pubpi->phy_rev) && ACMINORREV_0(pi)) {
			phy_ac_chanmgr_mutx_core2core_sync_war(pi, enable);
		}
	}
}

static void
phy_ac_chanmgr_set_spexp_matrix(phy_info_t *pi)
{
	uint32 svmp_start_addr = 0x1000;
	uint32 svmp_m4_idx = 0x4;
	uint32 spexp_offs[3] = {  0, 680, 456+680};
	//uint32 spexp_size[3] = {680, 456, 344};
	uint32 txv_config[3][3] = {{0x1000700, 0x2004008, 0x0000055},
		{0x1000680, 0x2004008, 0x0000039},
		{0x1000480, 0x2004008, 0x000002B}};
	uint32 zeros[5] = {0, 0, 0, 0, 0};
	int16 Q43[2*12] = {591, 0, 591, 0, 591, 0, 591, 0, 591, 0, 0, -591,
		-591, 0, 0, 591, 591, 0, -591, 0, 591, 0, -591, 0};
	//int16 Q43[2*12] = {836, 0, 0, 0, 836, 0, 0, 0, 0, 0, 836, 0,
	//	0, 0, 836, 0, 591, 0, -591, 0, -591, 0, 591, 0};
	int16 Q42[2*8]  = {724, 0, 724, 0, 724, 0, 724, 0, 724, 0, 0, 724, -724, 0, 0, -724};
	int16 Q32[2*6]  = {887, 0, 0, 0, 887, 0, 512, 0, 1024, 0, -512, 0};
	int16 csd_phasor[2*16] = {1024, 0, 946, 392, 724, 724, 392, 946, 0, 1024, -392, 946,
		-724, 724, -946, 392, -1024, 0, -946, -392, -724, -724, -392, -946, 0, -1024,
		392, -946, 724, -724, 946, -392};
	int16 k, m, n, j, ncol, nrow, ntones = 56;
	int16 Qr, Qi, *Q = NULL, csd_idx[3];
	uint32 Qcsd[12], svmp_addr[2] = {0x22552200, 0x0000228E};

	// steering matrix is of size S1.14
	//  Q_4x3 = 1/sqrt(3)*[1, 1, 1; 1, -j, -1; 1, -1, 1; 1, j, -1];
	//  Q_4x2 = 1/sqrt(2)*[1, 1; 1, j; 1, -1; 1, -j];
	//  Q_3x2 = [sqrt(3/4), sqrt(1/4); 0, 1; sqrt(3/4), -sqrt(1/4)];

	// These 3 special spacial expansion matrix are stored in M4
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_SVMPMEMS, 1,
			0x8000, 32, &svmp_m4_idx);

	for (k = 0; k < 3; k++) {
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_SVMPMEMS, 3,
			svmp_start_addr + spexp_offs[k], 32, &(txv_config[k][0]));
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_SVMPMEMS, 5,
			svmp_start_addr + spexp_offs[k] + 3, 32, &zeros);

		switch (k) {
		case 0:
			ncol = 3; nrow = 4; Q = Q43;
			break;
		case 1:
			ncol = 2; nrow = 4; Q = Q42;
			break;
		case 2:
			ncol = 2; nrow = 3; Q = Q32;
			break;
		}

		// initialize csd_idx to start from tone -28
		csd_idx[1] = -28;
		for (j = 0; j < ntones; j++) {
			// csd
			csd_idx[1] = (csd_idx[1]+64) & 0xf;
			csd_idx[0] = (csd_idx[1] << 1) & 0xf;
			csd_idx[2] = (csd_idx[0] + csd_idx[1]) & 0xf;
			for (m = 0; m < nrow; m++) {
				for (n = 0; n < ncol; n++) {
					if (m == 0) {
						Qr = Q[(n*nrow+m)*2];
						Qi = Q[(n*nrow+m)*2 + 1];
					} else {
						Qr = (((Q[(n*nrow+m)*2]*csd_phasor[csd_idx[m-1]*2] -
						Q[(n*nrow+m)*2 + 1]*csd_phasor[csd_idx[m-1]*2+1])
						>> 9) + 1) >> 1;
						Qi = (((Q[(n*nrow+m)*2]*csd_phasor[csd_idx[m-1]*2+1]
						+ Q[(n*nrow+m)*2+1]*csd_phasor[csd_idx[m-1]*2])
						>> 9) + 1) >> 1;
					}
					Qr = (Qr > 0)? Qr : (Qr + (1 << 12));
					Qi = (Qi > 0)? Qi : (Qi + (1 << 12));
					Qcsd[n*nrow + m] = ((Qr & 0xfff) << 4) |
						((Qi & 0xfff) << 20);
				}
			}
			csd_idx[1] += (j == ((ntones >> 1) - 1))? 2: 1;

			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_SVMPMEMS, ncol*nrow,
			svmp_start_addr + spexp_offs[k] + 8 + ncol*nrow*j, 32, &Qcsd);
		}
	}

	// writing txbf_user index 0x60, 0x61, 0x62 svmp address
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_BFMUSERINDEX, 2, 0x1030, 32, svmp_addr);
}

static int
phy_ac_chanmgr_switch_phymode_acphy(phy_info_t *pi, uint32 phymode)
{
	si_t *sih = pi->sh->sih;
	uint32 prev_phymode = (si_core_cflags(sih, 0, 0) && SICF_PHYMODE) >> SICF_PHYMODE_SHIFT;

	if (!ISACPHY(pi)) {
		return BCME_UNSUPPORTED;
	}

	if (phymode != prev_phymode)
		si_core_cflags(sih, SICF_PHYMODE, phymode << SICF_PHYMODE_SHIFT);

	return BCME_OK;
}
#endif /* (ACCONF != 0) || (ACCONF2 != 0) */