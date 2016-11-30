/*
 * ACPHY RXIQ CAL module implementation
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
 * $Id: phy_ac_rxiqcal.c 664725 2016-10-13 15:23:03Z mvermeid $
 */

#include <phy_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <phy_dbg.h>
#include <phy_mem.h>
#include <phy_btcx.h>
#include <phy_type_rxiqcal.h>
#include <phy_ac.h>
#include <phy_ac_rxiqcal.h>
#include <phy_ac_calmgr.h>
#include <phy_ac_misc.h>
#include <phy_ac_txiqlocal.h>

#include <phy_ac_info.h>
#include <wlc_radioreg_20691.h>
#include <wlc_radioreg_20693.h>
#include <wlc_radioreg_20694.h>
#include <wlc_radioreg_20695.h>
#include <wlc_radioreg_20696.h>
#include <wlc_phy_radio.h>
#include <wlc_phy_shim.h>
#include <wlc_phyreg_ac.h>
#include <phy_utils_reg.h>

typedef struct acphy_rx_fdiqi_ctl_struct {
	bool forced;
	uint16 forced_val;
	bool enabled;
	int32 slope[PHY_CORE_MAX];
	uint8 leakage_comp_mode;
} acphy_rx_fdiqi_ctl_t;

/* module private states */
struct phy_ac_rxiqcal_info {
	phy_info_t			*pi;
	phy_ac_info_t		*aci;
	phy_rxiqcal_info_t	*cmn_info;
	acphy_2069_rxcal_radioregs_t *ac_2069_rxcal_radioregs_orig;
	acphy_tiny_rxcal_radioregs_t *ac_tiny_rxcal_radioregs_orig;
	acphy_rx_fdiqi_ctl_t	*fdiqi;
	uint8 txpwridx_for_rxiqcal[PHY_CORE_MAX];
	/* cache coeffs */
	rxcal_coeffs_t rxcal_cache[PHY_CORE_MAX];
	uint16 rxcal_cache_cookie;
	/* std params */
	bool rxiqcal_percore_2g, rxiqcal_percore_5g;
#if defined(BCMDBG_RXCAL)
	phy_iq_est_t rxcal_noise[PHY_CORE_MAX];
	phy_iq_est_t rxcal_signal[PHY_CORE_MAX];
#endif /* BCMDBG_RXCAL */
/* add other variable size variables here at the end */
};

/* local functions */
static void phy_ac_rxiqlocal_std_params(phy_ac_rxiqcal_info_t *ac_info);

/* register phy type specific implementation */
phy_ac_rxiqcal_info_t *
BCMATTACHFN(phy_ac_rxiqcal_register_impl)(phy_info_t *pi, phy_ac_info_t *aci,
	phy_rxiqcal_info_t *cmn_info)
{
	phy_ac_rxiqcal_info_t *ac_info;
	phy_type_rxiqcal_fns_t fns;

	PHY_CAL(("%s\n", __FUNCTION__));

	/* allocate all storage together */
	if ((ac_info = phy_malloc(pi, sizeof(phy_ac_rxiqcal_info_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc failed\n", __FUNCTION__));
		goto fail;
	}

	/* Initialize params */
	ac_info->pi = pi;
	ac_info->aci = aci;
	ac_info->cmn_info = cmn_info;

	if ((ac_info->ac_2069_rxcal_radioregs_orig =
		phy_malloc(pi, sizeof(acphy_2069_rxcal_radioregs_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc ac_2069_rxcal_radioregs_orig failed\n", __FUNCTION__));
		goto fail;
	}

	if ((ac_info->ac_tiny_rxcal_radioregs_orig =
		phy_malloc(pi, sizeof(acphy_tiny_rxcal_radioregs_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc ac_tiny_rxcal_radioregs_orig failed\n", __FUNCTION__));
		goto fail;
	}

	if ((pi->u.pi_acphy->ac_rxcal_phyregs_orig = phy_malloc(pi,
		sizeof(acphy_rxcal_phyregs_t))) == NULL) {
		PHY_ERROR(("%s: ac_rxcal_phyregs_orig malloc failed\n", __FUNCTION__));
		goto fail;
	}

	if ((ac_info->fdiqi = phy_malloc(pi, sizeof(acphy_rx_fdiqi_ctl_t))) == NULL) {
		PHY_ERROR(("%s: fdiqi malloc failed\n", __FUNCTION__));
		goto fail;
	}

	/* register PHY type specific implementation */
	bzero(&fns, sizeof(fns));
	fns.ctx = ac_info;
#if !defined(PHYCAL_CACHING)
	fns.scanroam_cache = wlc_phy_scanroam_cache_rxcal_acphy;
#endif

	phy_ac_rxiqlocal_std_params(ac_info);

	if (phy_rxiqcal_register_impl(cmn_info, &fns) != BCME_OK) {
		PHY_ERROR(("%s: phy_rxiqcal_register_impl failed\n", __FUNCTION__));
		goto fail;
	}

	return ac_info;

	/* error handling */

fail:
	phy_ac_rxiqcal_unregister_impl(ac_info);
	return NULL;
}

void
BCMATTACHFN(phy_ac_rxiqcal_unregister_impl)(phy_ac_rxiqcal_info_t *ac_info)
{
	phy_rxiqcal_info_t *cmn_info;
	phy_info_t *pi;

	if (ac_info == NULL)
		return;

	cmn_info = ac_info->cmn_info;
	pi = ac_info->pi;

	PHY_CAL(("%s\n", __FUNCTION__));

	/* unregister from common */
	phy_rxiqcal_unregister_impl(cmn_info);

	if (ac_info->fdiqi != NULL) {
		phy_mfree(pi, ac_info->fdiqi, sizeof(acphy_rx_fdiqi_ctl_t));
	}
	if (pi->u.pi_acphy->ac_rxcal_phyregs_orig != NULL) {
		phy_mfree(pi, pi->u.pi_acphy->ac_rxcal_phyregs_orig, sizeof(acphy_rxcal_phyregs_t));
	}
	if (ac_info->ac_tiny_rxcal_radioregs_orig != NULL) {
		phy_mfree(pi, ac_info->ac_tiny_rxcal_radioregs_orig,
			sizeof(acphy_tiny_rxcal_radioregs_t));
	}
	if (ac_info->ac_2069_rxcal_radioregs_orig != NULL) {
		phy_mfree(pi, ac_info->ac_2069_rxcal_radioregs_orig,
			sizeof(acphy_2069_rxcal_radioregs_t));
	}

	phy_mfree(pi, ac_info, sizeof(phy_ac_rxiqcal_info_t));
}

int32
phy_ac_rxiqcal_get_fdiqi_slope(phy_ac_rxiqcal_info_t *rxiqcali, uint8 core)
{
	return rxiqcali->fdiqi->slope[core];
}

void
phy_ac_rxiqcal_set_fdiqi_slope(phy_ac_rxiqcal_info_t *rxiqcali, uint8 core, int32 slope)
{
	rxiqcali->fdiqi->slope[core] = slope;
}

bool
phy_ac_rxiqcal_is_fdiqi_enabled(phy_ac_rxiqcal_info_t *rxiqcali)
{
	return rxiqcali->fdiqi->enabled;
}

void
phy_ac_rxiqcal_set_fdiqi_enable(phy_ac_rxiqcal_info_t *rxiqcali, bool enable)
{
	rxiqcali->fdiqi->enabled = enable;
}

/* ********************************************* */
/*				Internal Definitions					*/
/* ********************************************* */
#define ACPHY_RXCAL_MAX_NUM_FREQ 6

enum {
	ACPHY_RXCAL_NORMAL = 0,
	ACPHY_RXCAL_LEAKAGE_COMP
};

#define ACPHY_RXCAL_NUMGAINS 11
#define ACPHY_RXCAL_NUMRXGAINS 16

typedef struct _acphy_rxcal_rxgain {
	int8 lna;
	uint8 tia;
	uint8 far;
	uint8 dvga;
	uint8 bq1;
} acphy_rxcal_rxgain_t;

typedef struct _acphy_rxcal_txrxgain {
	uint16 lpf_biq1;
	uint16 lpf_biq0;
	int8 txpwrindex;
} acphy_rxcal_txrxgain_t;

enum {
	ACPHY_RXCAL_GAIN_INIT = 0,
	ACPHY_RXCAL_GAIN_UP,
	ACPHY_RXCAL_GAIN_DOWN
};

typedef struct acphy_iq_mismatch_struct {
	int32 angle;
	int32 mag;
	int32 sin_angle;
} acphy_iq_mismatch_t;

static void wlc_phy_rxcal_radio_setup_acphy_tiny(phy_ac_rxiqcal_info_t *rxiqcalii);
static void wlc_phy_rxcal_radio_setup_acphy_28nm(phy_ac_rxiqcal_info_t *ti);
static void wlc_phy_rxcal_radio_setup_acphy_20694(phy_ac_rxiqcal_info_t *ti);
static void wlc_phy_rxcal_radio_setup_acphy_20696(phy_ac_rxiqcal_info_t *ti);
static void wlc_phy_rxcal_radio_cleanup_acphy_tiny(phy_ac_rxiqcal_info_t *rxiqcali);
static void wlc_phy_rxcal_phy_setup_acphy(phy_ac_rxiqcal_info_t *rxiqcali);
static void wlc_phy_rxcal_phy_cleanup_acphy(phy_ac_rxiqcal_info_t *rxiqcali);
static void wlc_phy_rxcal_radio_setup_acphy(phy_ac_rxiqcal_info_t *rxiqcali);
static void wlc_phy_rxcal_radio_cleanup_acphy(phy_ac_rxiqcal_info_t *rxiqcali);
static void wlc_phy_rxcal_radio_cleanup_acphy_28nm(phy_ac_rxiqcal_info_t *ti);
static void wlc_phy_cal_txgain_control_dBm(phy_ac_rxiqcal_info_t *rxiqcali, int8 targetpwr_dBm);
static void wlc_phy_rxcal_loopback_gainctrl_acphy(phy_ac_rxiqcal_info_t *rxiqcali);
static int phy_28nm_wn(phy_ac_rxiqcal_info_t *rxiqcali);
static void wlc_phy_rxcal_txrx_gainctrl_acphy_tiny(phy_ac_rxiqcal_info_t *rxiqcali);
static void wlc_phy_rxcal_txrx_gainctrl_acphy_28nm(phy_ac_rxiqcal_info_t *rxiqcali);
static void wlc_phy_rxcal_phy_setup_acphy_save_rfctrl(phy_ac_rxiqcal_info_t *rxiqcali, uint8 core,
	acphy_rxcal_phyregs_t *porig);
static void wlc_phy_rxcal_phy_setup_acphy_core(phy_ac_rxiqcal_info_t *rxiqcali, uint8 core,
	uint8 bw_idx, uint16 sdadc_config);
static void wlc_phy_rx_fdiqi_freq_config(phy_ac_rxiqcal_info_t *rxiqcali, int8 *fdiqi_cal_freqs,
	uint16 *num_data);
static void wlc_phy_rxcal_leakage_comp_acphy(phy_ac_rxiqcal_info_t *rxiqcali,
	phy_iq_est_t loopback_rx_iq, phy_iq_est_t leakage_rx_iq, int32 *angle, int32 *mag);
static void wlc_phy_rxcal_phy_setup_acphy_core_lpf(phy_ac_rxiqcal_info_t *rxiqcali, uint8 core,
	uint8 bw_idx);
static void wlc_phy_calc_iq_mismatch_acphy(phy_iq_est_t *est, acphy_iq_mismatch_t *mismatch);
#if defined(BCMDBG_RXCAL)
static void wlc_phy_rxcal_snr_acphy(phy_ac_rxiqcal_info_t *ri, uint16 num_samps, uint8 core_mask);
#endif /* BCMDBG_RXCAL */

static void
wlc_phy_rxcal_phy_setup_acphy(phy_ac_rxiqcal_info_t *rxiqcali)
{

	phy_info_t *pi = rxiqcali->pi;
	acphy_rxcal_phyregs_t *porig = rxiqcali->aci->ac_rxcal_phyregs_orig;
	uint8 core;
	uint16 sdadc_config;
	uint8 bw_idx;
	uint8 stall_val;
	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		phy_ac_rfseq_mode_set(pi, 1);
	}

	if (CHSPEC_IS80(pi->radio_chanspec) || PHY_AS_80P80(pi, pi->radio_chanspec)) {
		bw_idx = 2;
		sdadc_config = sdadc_cfg80;
	} else if (CHSPEC_IS160(pi->radio_chanspec)) {
		bw_idx = 3;
		sdadc_config = sdadc_cfg80;
		ASSERT(0);
	} else if (CHSPEC_IS40(pi->radio_chanspec)) {
		bw_idx = 1;
		if (pi->sdadc_config_override)
			sdadc_config = sdadc_cfg40hs;
		else
			sdadc_config = sdadc_cfg40;
	} else {
		bw_idx = 0;
		sdadc_config = sdadc_cfg20;
	}

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	ASSERT(!porig->is_orig);
	porig->is_orig = TRUE;
	porig->AfePuCtrl = READ_PHYREG(pi, AfePuCtrl);

	/* turn off tssi sleep feature during cal */
	MOD_PHYREG(pi, AfePuCtrl, tssiSleepEn, 0);

	if (ACMAJORREV_1(pi->pubpi->phy_rev) && (ACMINORREV_0(pi) || ACMINORREV_1(pi))) {
		ACPHY_REG_LIST_START
			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, rxfe_bilge_cnt, 4)
			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, soft_sdfeFifoReset, 1)
			MOD_PHYREG_ENTRY(pi, RxFeCtrl1, soft_sdfeFifoReset, 0)
		ACPHY_REG_LIST_EXECUTE(pi);
	}

	FOREACH_CORE(pi, core) {
		wlc_phy_rxcal_phy_setup_acphy_save_rfctrl(rxiqcali, core, porig);
	}

	porig->RfseqCoreActv2059 = READ_PHYREG(pi, RfseqCoreActv2059);

	if (TINY_RADIO(pi)) {
		porig->RxSdFeConfig1 = READ_PHYREG(pi, RxSdFeConfig1);
		porig->RxSdFeConfig6 = READ_PHYREG(pi, RxSdFeConfig6);

		MOD_PHYREG(pi, RxSdFeConfig1, farrow_rshift_force, 1);
	}

	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_36(pi->pubpi->phy_rev) ||
		ACMAJORREV_37(pi->pubpi->phy_rev)) {
		FOREACH_CORE(pi, core) {
			porig->forceFront[core] = READ_PHYREGCE(pi, forceFront, core);
		}
	}


	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, RfseqCoreActv2059, EnTx, 0xf);
	} else {
		MOD_PHYREG(pi, RfseqCoreActv2059, EnTx, pi->sh->phyrxchain);
	}
	MOD_PHYREG(pi, RfseqCoreActv2059, DisRx, 0);

	FOREACH_CORE(pi, core) {
		wlc_phy_rxcal_phy_setup_acphy_core(rxiqcali, core, bw_idx, sdadc_config);

		porig->PapdEnable[core] = READ_PHYREGCE(pi, PapdEnable, core);
		MOD_PHYREGCEE(pi, PapdEnable, core, papd_compEnb, 0);

		if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_36(pi->pubpi->phy_rev) ||
			ACMAJORREV_37(pi->pubpi->phy_rev)) {
			MOD_PHYREGCE(pi, forceFront, core, freqEst, 1);
			MOD_PHYREGCE(pi, forceFront, core, freqCor, 1);
		}
	}

	ACPHY_ENABLE_STALL(pi, stall_val);
}


static void
wlc_phy_rxcal_phy_cleanup_acphy(phy_ac_rxiqcal_info_t *rxiqcali)
{
	phy_info_t *pi = rxiqcali->pi;
	acphy_rxcal_phyregs_t *porig = rxiqcali->aci->ac_rxcal_phyregs_orig;
	uint8 core, i;
	uint8 stall_val;
	uint16 rfseq[] = { 0x100, 0x101, 0x102, 0x501 };

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	ASSERT(porig->is_orig);
	porig->is_orig = FALSE;
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		phy_ac_rfseq_mode_set(pi, 0);
	}

	WRITE_PHYREG(pi, RfseqCoreActv2059, porig->RfseqCoreActv2059);

	if (TINY_RADIO(pi)) {
	WRITE_PHYREG(pi, RxSdFeConfig1, porig->RxSdFeConfig1);
	WRITE_PHYREG(pi, RxSdFeConfig6, porig->RxSdFeConfig6);
	}

	FOREACH_CORE(pi, core) {
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
			for (i = 0; i < 3; i++)
			  wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, rfseq[core] + (i * 3),
			  16, &porig->rfseq_txgain[core + (i * 3)]);
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, (0x100 + core), 16,
			&porig->rfseq_txgain[core + 0]);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, (0x103 + core), 16,
			&porig->rfseq_txgain[core + 3]);
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, (0x106 + core), 16,
			&porig->rfseq_txgain[core + 6]);
		}


		wlc_phy_txpwr_by_index_acphy(pi, (1 << core), porig->txpwridx[core]);
		wlc_phy_set_tx_bbmult_acphy(pi, &(porig->bbmult[core]), core);

		WRITE_PHYREGCE(pi, RfctrlIntc, core, porig->RfctrlIntc[core]);
		WRITE_PHYREGCE(pi, PapdEnable, core, porig->PapdEnable[core]);

		WRITE_PHYREGCE(pi, RfctrlOverrideTxPus, core, porig->RfctrlOverrideTxPus[core]);
		WRITE_PHYREGCE(pi, RfctrlOverrideRxPus, core, porig->RfctrlOverrideRxPus[core]);
		WRITE_PHYREGCE(pi, RfctrlOverrideGains, core, porig->RfctrlOverrideGains[core]);
		WRITE_PHYREGCE(pi, RfctrlOverrideLpfCT, core, porig->RfctrlOverrideLpfCT[core]);
		WRITE_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core,
			porig->RfctrlOverrideLpfSwtch[core]);
		WRITE_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, porig->RfctrlOverrideAfeCfg[core]);
		WRITE_PHYREGCE(pi, RfctrlOverrideAuxTssi, core, porig->RfctrlOverrideAuxTssi[core]);
		WRITE_PHYREGCE(pi, RfctrlOverrideLowPwrCfg, core,
			porig->RfctrlOverrideLowPwrCfg[core]);

		WRITE_PHYREGCE(pi, RfctrlCoreTxPus, core, porig->RfctrlCoreTxPus[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreRxPus, core, porig->RfctrlCoreRxPus[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreTXGAIN1, core, porig->RfctrlCoreTXGAIN1[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreTXGAIN2, core, porig->RfctrlCoreTXGAIN2[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreRXGAIN1, core, porig->RfctrlCoreRXGAIN1[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreRXGAIN2, core, porig->RfctrlCoreRXGAIN2[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreLpfGain, core, porig->RfctrlCoreLpfGain[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreLpfCT, core, porig->RfctrlCoreLpfCT[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreLpfGmult, core, porig->RfctrlCoreLpfGmult[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreRCDACBuf, core, porig->RfctrlCoreRCDACBuf[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, porig->RfctrlCoreLpfSwtch[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreAfeCfg1, core, porig->RfctrlCoreAfeCfg1[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreAfeCfg2, core, porig->RfctrlCoreAfeCfg2[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreLowPwr, core, porig->RfctrlCoreLowPwr[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreAuxTssi1, core, porig->RfctrlCoreAuxTssi1[core]);
		WRITE_PHYREGCE(pi, RfctrlCoreAuxTssi2, core, porig->RfctrlCoreAuxTssi2[core]);
		WRITE_PHYREGCE(pi, Dac_gain, core, porig->Dac_gain[core]);
	}
	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_36(pi->pubpi->phy_rev) ||
		ACMAJORREV_37(pi->pubpi->phy_rev)) {
		FOREACH_CORE(pi, core) {
			WRITE_PHYREGCE(pi, forceFront, core, porig->forceFront[core]);
		}
	}
	WRITE_PHYREG(pi, AfePuCtrl, porig->AfePuCtrl);
	wlc_phy_force_rfseq_acphy(pi, ACPHY_RFSEQ_RESET2RX);

	ACPHY_ENABLE_STALL(pi, stall_val);
}

static void
wlc_phy_rxcal_radio_setup_acphy(phy_ac_rxiqcal_info_t *rxiqcali)
{
	phy_info_t *pi = rxiqcali->pi;
	acphy_2069_rxcal_radioregs_t *porig = rxiqcali->ac_2069_rxcal_radioregs_orig;
	uint16 tx_atten, rx_atten;
	uint8 core;

	ASSERT(RADIOID_IS(pi->pubpi->radioid, BCM2069_ID));

	tx_atten = 0;
	rx_atten = 0;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	ASSERT(!porig->is_orig);
	porig->is_orig = TRUE;

	FOREACH_CORE(pi, core) {
		porig->rf_2069_txrx2g_cal_tx[core] = READ_RADIO_REGC(pi, RF, TXRX2G_CAL_TX, core);
		porig->rf_2069_txrx5g_cal_tx[core] = READ_RADIO_REGC(pi, RF, TXRX5G_CAL_TX, core);
		porig->rf_2069_txrx2g_cal_rx[core] = READ_RADIO_REGC(pi, RF, TXRX2G_CAL_RX, core);
		porig->rf_2069_txrx5g_cal_rx[core] = READ_RADIO_REGC(pi, RF, TXRX5G_CAL_RX, core);
		porig->rf_2069_rxrf2g_cfg2[core] = READ_RADIO_REGC(pi, RF, RXRF2G_CFG2, core);
		porig->rf_2069_rxrf5g_cfg2[core] = READ_RADIO_REGC(pi, RF, RXRF5G_CFG2, core);

		/* Disable all loopback options first */
		phy_utils_write_radioreg(pi, RF_2069_TXRX2G_CAL_TX(core), 0);
		phy_utils_write_radioreg(pi, RF_2069_TXRX5G_CAL_TX(core), 0);
		phy_utils_write_radioreg(pi, RF_2069_TXRX2G_CAL_RX(core), 0);
		phy_utils_write_radioreg(pi, RF_2069_TXRX5G_CAL_RX(core), 0);
		phy_utils_write_radioreg(pi, RF_2069_RXRF2G_CFG2(core), 0);
		phy_utils_write_radioreg(pi, RF_2069_RXRF5G_CFG2(core), 0);

		/* Disable PAPD paths
		 *	- Powerdown the papd loopback path on Rx side
		 *	- Disable the epapd
		 */
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_RADIO_REGC(pi, TXRX2G_CAL_RX, core, loopback2g_papdcal_pu, 0);
			MOD_RADIO_REGC(pi, RXRF2G_CFG2, core, lna2g_epapd_en, 0);
		} else {
			MOD_RADIO_REGC(pi, TXRX5G_CAL_RX, core, loopback5g_papdcal_pu, 0);
			MOD_RADIO_REGC(pi, RXRF5G_CFG2, core, lna5g_epapd_en, 0);
		}

		/* Disable RCCR Phase Shifter */
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_RADIO_REGC(pi, TXRX2G_CAL_RX, core, loopback2g_rxiqcal_cr_pu, 0);
			MOD_RADIO_REGC(pi, TXRX2G_CAL_RX, core, loopback2g_rxiqcal_rc_pu, 0);
		} else {
			MOD_RADIO_REGC(pi, TXRX5G_CAL_RX, core, loopback5g_rxiqcal_cr_pu, 0);
			MOD_RADIO_REGC(pi, TXRX5G_CAL_RX, core, loopback5g_rxiqcal_rc_pu, 0);
		}


		/* Enable Tx Path */
		if (!ACRADIO_2069_EPA_IS(pi->pubpi->radiorev)) {
			if (CHSPEC_IS2G(pi->radio_chanspec)) {
				MOD_RADIO_REGC(pi, TXRX2G_CAL_TX, core, i_calPath_pa2g_pu, 1);
				MOD_RADIO_REGC(pi, TXRX2G_CAL_TX, core, i_calPath_pad2g_pu, 0);
			} else {
				MOD_RADIO_REGC(pi, TXRX5G_CAL_TX, core, i_calPath_pa_pu_5g, 1);
				MOD_RADIO_REGC(pi, TXRX5G_CAL_TX, core, i_calPath_pad_pu_5g, 0);
			}
		} else {
			if (CHSPEC_IS2G(pi->radio_chanspec)) {
				MOD_RADIO_REGC(pi, TXRX2G_CAL_TX, core, i_calPath_pa2g_pu, 0);
				MOD_RADIO_REGC(pi, TXRX2G_CAL_TX, core, i_calPath_pad2g_pu, 1);
			} else {
				MOD_RADIO_REGC(pi, TXRX5G_CAL_TX, core, i_calPath_pa_pu_5g, 0);
				MOD_RADIO_REGC(pi, TXRX5G_CAL_TX, core, i_calPath_pad_pu_5g, 1);

			}
		}

		/* Enable Rx Path
		 *	- Powerup the master cal PU signal on Rx side (common to papd & rxiqcal).
		 *	  Not needed for rx/cr rxiqcal PU.
		 *	- Powerup the rxiqcal loopback path on Rx side.
		 */
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_RADIO_REGC(pi, TXRX2G_CAL_RX, core, loopback2g_cal_pu, 1);
			MOD_RADIO_REGC(pi, TXRX2G_CAL_RX, core, loopback2g_rxiqcal_pu, 1);
		} else {
			if (ACREV_IS(pi->pubpi->phy_rev, 0)) {
				MOD_RADIO_REGC(pi, TXRX5G_CAL_RX, core, loopback5g_cal_pu, 1);
				MOD_RADIO_REGC(pi, TXRX5G_CAL_RX, core, loopback5g_rxiqcal_pu, 1);
				} else {
				MOD_RADIO_REGC(pi, TXRX5G_CAL_RX, core, loopback5g_lna12_mux, 1);
				MOD_RADIO_REGC(pi, TXRX5G_CAL_RX, core, loopback5g_cal_pu, 1);
				}
		}

		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_RADIO_REGC(pi, TXRX2G_CAL_TX, core, i_cal_pad_atten_2g, tx_atten);
			MOD_RADIO_REGC(pi, TXRX2G_CAL_RX, core,
				loopback2g_rxiqcal_rx_attn, rx_atten);
		} else {
			MOD_RADIO_REGC(pi, TXRX5G_CAL_TX, core, i_cal_pad_atten_5g, tx_atten);
			MOD_RADIO_REGC(pi, TXRX5G_CAL_RX, core,
				loopback5g_rxiqcal_rx_attn, rx_atten);
		}
		/* additional settings for 4350 epa 5G */
		if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID) &&
			(RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
			(ACRADIO_2069_EPA_IS(pi->pubpi->radiorev)) &&
			(CHSPEC_IS5G(pi->radio_chanspec))) {
			/* Turn off the loopback path */
			MOD_RADIO_REGC(pi, TXRX5G_CAL_RX, core, loopback5g_cal_pu, 0);
			/* turn off kill switch */
			MOD_RADIO_REGC(pi, LNA5G_CFG1, core, tr_rx_en, 0x1);
			MOD_RADIO_REGC(pi, OVR7, core, ovr_lna5g_tr_rx_en, 0x1);
			/* Turn off ext LNA */
			MOD_PHYREGCE(pi, RfctrlIntc, core, ext_lna_5g_pu, 0);
			MOD_PHYREGCE(pi, RfctrlIntc, core, override_ext_lna, 1);
			/* Turn off PAD coupler */
			MOD_RADIO_REGC(pi, TXRX5G_CAL_TX, core, i_calPath_pad_pu_5g, 0);
		}
	}

}

static void
wlc_phy_rxcal_radio_setup_acphy_28nm(phy_ac_rxiqcal_info_t *ti)
{
	uint16 core;
	phy_info_t *pi = ti->pi;
	core = 0;
	phy_ac_reg_cache_save(ti->aci, RADIOREGS_RXIQCAL);
	if (RADIOMAJORREV(pi) >= 2)
	{
		//43012b0 making ntssi 0 during TX for cal
		// To have rx adc in normal mode
		MOD_RADIO_REG_28NM(pi, RF, AFEDIV2G_CFG1_OVR, 0, ovr_afediv_rst_en, 0x1);
		MOD_RADIO_REG_28NM(pi, RF, AFEDIV2G_CFG1_OVR, 0, ovr_afediv_reset, 0x1);

		MOD_RADIO_REG_28NM(pi, RF, AFEDIV2G_REG5, 0, afediv_rst_en, 0x1);

		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_RADIO_REG_28NM(pi, RF, SPARE_CFG1, 0, ovr_afediv2g_adc_Ntssi, 1);
			MOD_RADIO_REG_28NM(pi, RF, AFEDIV2G_REG1, 0, afediv2g_adc_Ntssi, 0);
		} else {
			MOD_RADIO_REG_28NM(pi, RF, SPARE_CFG1, 0, ovr_afediv5g_adc_Ntssi, 1);
			MOD_RADIO_REG_28NM(pi, RF, AFEDIV5G_REG1, 0, afediv5g_adc_Ntssi,  0);
		}
		MOD_RADIO_REG_28NM(pi, RF, AFEDIV2G_REG1, 0, afediv_reset, 0x1);
		MOD_RADIO_REG_28NM(pi, RF, AFEDIV2G_REG1, 0, afediv_reset, 0x0);

		MOD_RADIO_REG_28NM(pi, RF, AFEDIV2G_REG5, 0, afediv_rst_en, 0x0);
	}

	FOREACH_CORE(pi, core) {
		/* muxing aux path to adc path disabling all other paths */
		ACPHY_REG_LIST_START
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG2_OVR, 0, ovr_tia_sw_rx_bq1, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG2_OVR, 0, ovr_tia_sw_bq2_adc, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG2_OVR, 0, ovr_tia_sw_bq2_wbcal, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG2_OVR, 0, ovr_tia_sw_wbcal_adc, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG2_OVR, 0, ovr_tia_sw_aux_adc, 0x1)

			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_REG18, 0, tia_sw_rx_bq1, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_REG16, 0, tia_sw_bq2_adc, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_REG16, 0, tia_sw_bq2_wbcal, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_REG18, 0, tia_sw_wbcal_adc, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_REG16, 0, tia_sw_aux_adc, 0x0)

			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG1_OVR, 0, ovr_tia_bq1_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG1_OVR, 0, ovr_tia_bq2_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG1_OVR, 0, ovr_tia_dcdac_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG1_OVR, 0, ovr_tia_bias_pu, 0x1)

			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_REG7, 0, tia_bq1_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_REG7, 0, tia_bq2_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_DCDAC_REG2, 0, tia_dcdac_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_REG7, 0, tia_bias_pu, 0x1)

			//Lna1 is not there in loopback for 43012
			//putting dc estimation for lna1 to 0
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_CFG1_OVR, 0, ovr_tia_dcdac, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_DCDAC_REG1, 0, tia_dcdac_i, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TIA_DCDAC_REG2, 0, tia_dcdac_q, 0x0)
		ACPHY_REG_LIST_EXECUTE(pi);

			if (CHSPEC_IS2G(pi->radio_chanspec)) {
		ACPHY_REG_LIST_START
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_CFG1_OVR, 0,
			ovr_rx2g_gm_en, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_CFG2_OVR, 0,
			ovr_rx2g_gm_ds_en, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_CFG2_OVR, 0,
			ovr_lna2g_tr_rx_en, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_CFG2_OVR, 0,
			ovr_lna2g_lna1_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_CFG2_OVR, 0,
			ovr_lna2g_lna1_out_short_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_CFG2_OVR, 0,
			ovr_lna2g_lna1_bypass, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TX2G_CFG1_OVR, 0,
			ovr_pa2g_pu, 0x1)

			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_REG3, 0,
			rx2g_gm_en, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_REG3, 0,
			rx2g_gm_ds_en, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TX2G_MISC_CFG1, 0,
			pa2g_cal_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_REG1, 0,
			rx2g_iloopback_en, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, PA2G_CFG1, 0,
			pa2g_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, PA2G_CFG1, 0,
			pa2g_cal_atten, 1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_REG1, 0,
			rx2g_iloopback_attn, 0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, LNA2G_REG2, 0,
			lna2g_auxpath_en, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, LNA2G_REG2, 0,
			lna2g_epapd_en, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_REG3, 0,
			rx2g_gm_loopback_mainpath, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_REG3, 0,
			rx2g_gm_loopback_en, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX2G_REG3, 0,
			rx2g_gm_bias_en, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, LNA2G_REG1, 0,
			lna2g_tr_rx_en, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, LNA2G_REG1, 0,
			lna2g_lna1_pu, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, LNA2G_REG1, 0,
			lna2g_lna1_out_short_pu, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, LNA2G_REG1, 0,
			lna2g_lna1_bypass, 0x0)
		ACPHY_REG_LIST_EXECUTE(pi);
		}

			if (CHSPEC_IS5G(pi->radio_chanspec)) {
		ACPHY_REG_LIST_START
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_CFG1_OVR, 0,
			ovr_rx5g_gm_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_CFG2_OVR, 0,
			ovr_rx5g_lna_tr_rx_en, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_CFG2_OVR, 0,
			ovr_rx5g_lna_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_CFG2_OVR, 0,
			ovr_rx5g_lna_out_short, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_CFG2_OVR, 0,
			ovr_rx5g_lna_bypass, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TX5G_CFG2_OVR, 0,
			ovr_pa5g_cal, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, TX5G_CFG1_OVR, 0,
			ovr_pa5g_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_CFG1_OVR, 0,
			ovr_rx5g_gm_pu_bias, 0x1)

			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG4, 0,
			rx5g_gm_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG4, 0,
			rx5g_coup_loopback_en, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, PA5G_CFG11, 0,
			pa5g_cal_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, PA5G_CFG11, 0,
			pa5g_cal_atten, 1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG4, 0,
			rx5g_coup_loopback_attn, 0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG2, 0,
			lna5g_auxpath_en, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG2, 0,
			rx5g_lna_epapd_en, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG4, 0,
			rx5g_gm_loopback_en_mainpath, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG4, 0,
			rx5g_gm_loopback_en_auxpath, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG1, 0,
			rx5g_lna_tr_rx_en, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG1, 0,
			rx5g_lna_pu, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG1, 0,
			rx5g_lna_out_short, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG1, 0,
			rx5g_lna_bypass, 0x0)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, PA5G_CFG4, 0,
			pa5g_pu, 0x1)
			MOD_RADIO_REG_28NM_ENTRY(pi, RF, RX5G_REG4, 0,
			rx5g_gm_pu_bias, 0x1)
		ACPHY_REG_LIST_EXECUTE(pi);
		}
	}
}

static void
wlc_phy_rxcal_radio_setup_acphy_20694(phy_ac_rxiqcal_info_t *ti)
{
	uint16 core;
	phy_info_t *pi = ti->pi;
	uint sicoreunit;

	sicoreunit = wlapi_si_coreunit(pi->sh->physhim);
	phy_ac_reg_cache_save(ti->aci, RADIOREGS_RXIQCAL);
	FOREACH_CORE(pi, core) {
		if (core == 0) {
			/* muxing aux path to adc path disabling all other paths */
			ACPHY_REG_LIST_START
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR1, 0,
				ovr_lpf_sw_bq2_adc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR1, 0,
				ovr_lpf_sw_bq2_rc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR1, 0,
				ovr_lpf_sw_dac_bq2, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR1, 0,
				ovr_lpf_sw_dac_rc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR2, 0,
				ovr_lpf_sw_bq1_adc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR2, 0,
				ovr_lpf_sw_bq1_bq2, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR2, 0,
				ovr_lpf_sw_aux_adc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 0,
				lpf_sw_bq1_bq2, 0x1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 0,
				lpf_sw_bq2_adc, 0x1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 0,
				lpf_sw_bq1_adc, 0x0)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 0,
				lpf_sw_dac_bq2, 0x0)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 0,
				lpf_sw_bq2_rc, 0x0)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 0,
				lpf_sw_dac_rc, 0x1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 0,
				lpf_sw_aux_adc, 0x0)
				ACPHY_REG_LIST_EXECUTE(pi);
			if (CHSPEC_IS2G(pi->radio_chanspec)) {
				if (sicoreunit == DUALMAC_MAIN) {
					MOD_RADIO_REG_20694(pi, RF, LOGEN_CFG1, 0,
					ovr_logen2g_rx_div2_pu, 1);
					MOD_RADIO_REG_20694(pi, RF, LOGEN2G_REG1, 0,
					logen2g_rx_div2_pu, 1);
				}
				ACPHY_REG_LIST_START
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG2_OVR, 0,
					ovr_lna2g_lna1_out_short_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG2_OVR, 0,
					ovr_lna2g_lna1_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG2_OVR, 0,
					ovr_lna2g_lna1_bypass, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG2_OVR, 0,
					ovr_lna2g_tr_rx_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG1_OVR, 0,
					ovr_rx2g_gm_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG1_OVR, 0,
					ovr_rx2g_lo_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG1_OVR, 0,
					ovr_rx2g_gm_bypass, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG1, 0,
					rx2g_lo_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG3, 0,
					rx2g_gm_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG3, 0,
					rx2g_gm_ds_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG4, 0,
					rx2g_gm_bypass, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX2G_MISC_CFG1, 0,
					cal2g_pa_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX2G_MISC_CFG1, 0,
					cal2g_pa_atten, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG1, 0,
					rx2g_iloopback_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG1, 0,
					rx2g_iloopback_attn, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG2, 0,
					lna2g_auxpath, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG2, 0,
					lna2g_epapd_en, 0x0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG3, 0,
					rx2g_gm_loopback_mainpath, 0x0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG3, 0,
					rx2g_gm_loopback_en, 0x0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG1, 0,
					lna2g_tr_rx_en, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG1, 0,
					lna2g_lna1_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG1, 0,
					lna2g_lna1_out_short_pu, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG1, 0,
					lna2g_lna1_bypass, 0)
					ACPHY_REG_LIST_EXECUTE(pi);
			}
			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				/* Using leakage path for 5G */
				ACPHY_REG_LIST_START
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 0,
					ovr_rx5g_lna_tr_rx_en, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 0,
					ovr_rx5g_lna_pu, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 0,
					ovr_rx5g_lna_out_short, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 0,
					ovr_rx5g_lna_bypass, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG1_OVR, 0,
					ovr_rx5g_gm_pu, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 0,
					ovr_rx5g_mix_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX5G_CFG2_OVR, 0,
					ovr_pa5g_cal, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG1_OVR, 0,
					ovr_rx5g_lna_pu_bias, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG1_OVR, 0,
					ovr_rx5g_gm_pu_bias, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 0,
					rx5g_lna_tr_rx_en, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 0,
					rx5g_lna_out_short, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 0,
					rx5g_lna_bypass, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 0,
					rx5g_lna_pu_pulse, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 0,
					rx5g_gm_pu_bias, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 0,
					rx5g_lna_pu, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 0,
					rx5g_gm_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG5, 0,
					rx5g_mix_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG2, 0,
					rx5g_lna_pu_bias, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG2, 0,
					rx5g_lna_epapd_en, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG2, 0,
					rx5g_lna_auxpath_en, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 0,
					rx5g_gm_loopback_en_mainpath, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 0,
					rx5g_gm_loopback_en_auxpath, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 0,
					rx5g_coup_loopback_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 0,
					rx5g_coup_loopback_attn, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX5G_MISC_CFG1, 0,
					cal5g_pa_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX5G_MISC_CFG1, 0,
					cal5g_pa_atten, 0)
					ACPHY_REG_LIST_EXECUTE(pi);
			}
		} else {
			/* muxing aux path to adc path disabling all other paths */
			ACPHY_REG_LIST_START
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR1, 1,
				ovr_lpf_sw_bq2_adc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR1, 1,
				ovr_lpf_sw_bq2_rc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR1, 1,
				ovr_lpf_sw_dac_bq2, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR1, 1,
				ovr_lpf_sw_dac_rc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR2, 1,
				ovr_lpf_sw_bq1_adc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR2, 1,
				ovr_lpf_sw_bq1_bq2, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_OVR2, 1,
				ovr_lpf_sw_aux_adc, 1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 1,
				lpf_sw_bq1_bq2, 0x1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 1,
				lpf_sw_bq2_adc, 0x1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 1,
				lpf_sw_bq1_adc, 0x0)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 1,
				lpf_sw_dac_bq2, 0x0)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 1,
				lpf_sw_bq2_rc, 0x0)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 1,
				lpf_sw_dac_rc, 0x1)
				MOD_RADIO_REG_20694_ENTRY(pi, RF, LPF_REG7, 1,
				lpf_sw_aux_adc, 0x0)
				ACPHY_REG_LIST_EXECUTE(pi);
			if (CHSPEC_IS2G(pi->radio_chanspec)) {
				if (sicoreunit == DUALMAC_MAIN) {
					MOD_RADIO_REG_20694(pi, RF, LOGEN_CFG1, 1,
					ovr_logen2g_rx_div2_pu, 1);
					MOD_RADIO_REG_20694(pi, RF, LOGEN2G_REG1, 1,
					logen2g_rx_div2_pu, 1);
				}
				ACPHY_REG_LIST_START
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG2_OVR, 1,
					ovr_lna2g_lna1_out_short_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG2_OVR, 1,
					ovr_lna2g_lna1_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG2_OVR, 1,
					ovr_lna2g_lna1_bypass, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG2_OVR, 1,
					ovr_lna2g_tr_rx_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG1_OVR, 1,
					ovr_rx2g_gm_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG1_OVR, 1,
					ovr_rx2g_lo_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_CFG1_OVR, 1,
					ovr_rx2g_gm_bypass, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG1, 1,
					rx2g_lo_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG3, 1,
					rx2g_gm_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG3, 1,
					rx2g_gm_ds_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG4, 1,
					rx2g_gm_bypass, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX2G_MISC_CFG1, 1,
					cal2g_pa_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX2G_MISC_CFG1, 1,
					cal2g_pa_atten, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG1, 1,
					rx2g_iloopback_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG1, 1,
					rx2g_iloopback_attn, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG2, 1,
					lna2g_auxpath, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG2, 1,
					lna2g_epapd_en, 0x0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG3, 1,
					rx2g_gm_loopback_mainpath, 0x0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX2G_REG3, 1,
					rx2g_gm_loopback_en, 0x0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG1, 1,
					lna2g_tr_rx_en, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG1, 1,
					lna2g_lna1_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG1, 1,
					lna2g_lna1_out_short_pu, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, LNA2G_REG1, 1,
					lna2g_lna1_bypass, 0)
					ACPHY_REG_LIST_EXECUTE(pi);
			}
			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				ACPHY_REG_LIST_START
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 1,
					ovr_rx5g_lna_tr_rx_en, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 1,
					ovr_rx5g_lna_pu, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 1,
					ovr_rx5g_lna_out_short, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 1,
					ovr_rx5g_lna_bypass, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG1_OVR, 1,
					ovr_rx5g_gm_pu, 0x1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG2_OVR, 1,
					ovr_rx5g_mix_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX5G_CFG2_OVR, 1,
					ovr_pa5g_cal, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG1_OVR, 1,
					ovr_rx5g_lna_pu_bias, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_CFG1_OVR, 1,
					ovr_rx5g_gm_pu_bias, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 1,
					rx5g_lna_tr_rx_en, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 1,
					rx5g_lna_out_short, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 1,
					rx5g_lna_bypass, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 1,
					rx5g_lna_pu_pulse, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 1,
					rx5g_gm_pu_bias, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG1, 1,
					rx5g_lna_pu, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 1,
					rx5g_gm_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG5, 1,
					rx5g_mix_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG2, 1,
					rx5g_lna_pu_bias, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG2, 1,
					rx5g_lna_epapd_en, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG2, 1,
					rx5g_lna_auxpath_en, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 1,
					rx5g_gm_loopback_en_mainpath, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 1,
					rx5g_gm_loopback_en_auxpath, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 1,
					rx5g_coup_loopback_en, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, RX5G_REG4, 1,
					rx5g_coup_loopback_attn, 0)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX5G_MISC_CFG1, 1,
					cal5g_pa_pu, 1)
					MOD_RADIO_REG_20694_ENTRY(pi, RF, TX5G_MISC_CFG1, 1,
					cal5g_pa_atten, 0)
					ACPHY_REG_LIST_EXECUTE(pi);
			}
		}
	}
}

static void
wlc_phy_rxcal_radio_setup_acphy_20696(phy_ac_rxiqcal_info_t *ti)
{
	uint16 core;
	phy_info_t *pi = ti->pi;

	ASSERT(RADIOID(pi->pubpi->radioid) == BCM20696_ID);

	phy_ac_reg_cache_save(ti->aci, RADIOREGS_RXIQCAL);

	FOREACH_CORE(pi, core) {
		/* Mux AUX path to ADC, disable other paths to ADC */
		MOD_RADIO_REG_20696(pi, LPF_OVR1, core, ovr_lpf_sw_bq2_adc, 0x1);
		MOD_RADIO_REG_20696(pi, LPF_OVR1, core, ovr_lpf_sw_bq2_rc, 0x1);
		MOD_RADIO_REG_20696(pi, LPF_OVR1, core, ovr_lpf_sw_dac_bq2, 0x1);
		MOD_RADIO_REG_20696(pi, LPF_OVR1, core, ovr_lpf_sw_dac_rc, 0x1);
		MOD_RADIO_REG_20696(pi, LPF_OVR2, core, ovr_lpf_sw_bq1_adc, 0x1);
		MOD_RADIO_REG_20696(pi, LPF_OVR2, core, ovr_lpf_sw_bq1_bq2, 0x1);
		MOD_RADIO_REG_20696(pi, LPF_OVR2, core, ovr_lpf_sw_aux_adc, 0x1);
		MOD_RADIO_REG_20696(pi, LPF_REG7, core, lpf_sw_bq1_bq2,  0x1);
		MOD_RADIO_REG_20696(pi, LPF_REG7, core, lpf_sw_bq2_adc,  0x1);
		MOD_RADIO_REG_20696(pi, LPF_REG7, core, lpf_sw_bq1_adc,  0x0);
		MOD_RADIO_REG_20696(pi, LPF_REG7, core, lpf_sw_dac_bq2,  0x0);
		MOD_RADIO_REG_20696(pi, LPF_REG7, core, lpf_sw_bq2_rc,  0x0);
		MOD_RADIO_REG_20696(pi, LPF_REG7, core, lpf_sw_dac_rc,  0x1);
		MOD_RADIO_REG_20696(pi, LPF_REG7, core, lpf_sw_aux_adc,  0x0);
		/* TIA power up sequence */
		MOD_RADIO_REG_20696(pi, TIA_CFG1_OVR, core, ovr_tia_pu, 1);
		MOD_RADIO_REG_20696(pi, TIA_CFG1_OVR, core, ovr_tia_bias_pu, 1);
		MOD_RADIO_REG_20696(pi, TIA_REG7, core, tia_pu, 1);
		MOD_RADIO_REG_20696(pi, TIA_REG7, core, tia_bias_pu, 1);

		MOD_RADIO_REG_20696(pi, TXDAC_REG0, core, iqdac_buf_cmsel, 0);
		MOD_RADIO_REG_20696(pi, TXDAC_REG1, core, iqdac_lowcm_en, 1);
		MOD_RADIO_REG_20696(pi, TXDAC_REG0, core, iqdac_attn, 3);

		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_RADIO_REG_20696(pi, RX2G_CFG1_OVR, core, ovr_lna2g_lna1_out_short_pu,
				0x1);
			MOD_RADIO_REG_20696(pi, RX2G_CFG1_OVR, core, ovr_lna2g_lna1_pu, 0x1);
			MOD_RADIO_REG_20696(pi, RX2G_CFG1_OVR, core, ovr_lna2g_lna1_bypass, 0x1);
			MOD_RADIO_REG_20696(pi, RX2G_CFG1_OVR, core, ovr_lna2g_tr_rx_en, 0x1);
			MOD_RADIO_REG_20696(pi, RX2G_CFG1_OVR, core, ovr_rx2g_gm_en, 0x1);
			MOD_RADIO_REG_20696(pi, RX2G_CFG1_OVR, core, ovr_rx2g_lo_en, 0x1);
			MOD_RADIO_REG_20696(pi, RX2G_REG1, core, rx2g_lo_en, 0x1);
			MOD_RADIO_REG_20696(pi, RX2G_REG3, core, rx2g_gm_en, 0x1);
			MOD_RADIO_REG_20696(pi, RX2G_REG3, core, rx2g_gm_ds_en, 0x1);
			MOD_RADIO_REG_20696(pi, TXDAC_REG0, core, iqdac_buf_cmsel, 0x1);
			/* Logen Rx 2G power up sequence
			 * powering up/down LogenRx core, bias, div2 buffers and rccr
			 */
			MOD_RADIO_PLLREG_20696(pi, LOGEN_REG0, logen_pu, 0x1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_div2_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_bias_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_gm_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_lc_pu, 1);

			/* Mux powerups for core 2 and core 3 */
			if ((core == 2) || (core == 3))
				MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG5, core, logen_mux_pu, 1);

			/* Rx rccr and rx div2 buf power up/down */
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_OVR0, core, ovr_logen_div2_rxbuf_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_div2_rxbuf_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_OVR0, core, ovr_logen_rx_rccr_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_rx_rccr_pu, 0);

			/* RX IQCAL 2G in mode 0: using iPAPD path and main GM */
			MOD_RADIO_REG_20696(pi, TX2G_MIX_REG0, core, cal2g_pa_pu, 0x1);
			MOD_RADIO_REG_20696(pi, TX2G_MISC_CFG1, core, cal2g_pa_atten, 0);
			MOD_RADIO_REG_20696(pi, RX2G_REG1, core, rx2g_iloopback_en, 0x1);
			MOD_RADIO_REG_20696(pi, RX2G_REG1, core, rx2g_iloopback_attn, 0);
			MOD_RADIO_REG_20696(pi, LNA2G_REG2, core, lna2g_auxpath, 0x0);
			MOD_RADIO_REG_20696(pi, LNA2G_REG2, core, lna2g_epapd_en, 0x0);
			MOD_RADIO_REG_20696(pi, RX2G_REG3, core, rx2g_gm_loopback_mainpath, 0x1);
			/* Using the main gm path */
			MOD_RADIO_REG_20696(pi, RX2G_REG3, core, rx2g_gm_loopback_en, 0x0);
			MOD_RADIO_REG_20696(pi, LNA2G_REG1, core, lna2g_tr_rx_en, 0x0);
			/* Not going through lna1 */
			MOD_RADIO_REG_20696(pi, LNA2G_REG1, core, lna2g_lna1_pu, 0x0);
			MOD_RADIO_REG_20696(pi, RX2G_CFG1_OVR, core, ovr_rx2g_gm_gain, 0x1);
			MOD_RADIO_REG_20696(pi, RX2G_REG3, core, rx2g_gm_gain, 0x3);
			MOD_RADIO_REG_20696(pi, LNA2G_REG1, core, lna2g_lna1_out_short_pu, 0x0);
			MOD_RADIO_REG_20696(pi, LNA2G_REG1, core, lna2g_lna1_bypass, 0x0);

			MOD_RADIO_REG_20696(pi, TXDAC_REG3, core, i_config_IQDACbuf_2g_cmref_en, 1);
		} else {
			MOD_RADIO_REG_20696(pi, RX5G_CFG1_OVR, core, ovr_rx5g_lna_tr_rx_en, 0x1);
			MOD_RADIO_REG_20696(pi, RX5G_CFG1_OVR, core, ovr_rx5g_lna_pu, 0x1);
			MOD_RADIO_REG_20696(pi, RX5G_CFG1_OVR, core, ovr_rx5g_lna_out_short, 0x1);
			MOD_RADIO_REG_20696(pi, RX5G_CFG1_OVR, core, ovr_rx5g_lna_bypass, 0x1);
			MOD_RADIO_REG_20696(pi, RX5G_CFG1_OVR, core, ovr_rx5g_gm_pu, 0x1);
			MOD_RADIO_REG_20696(pi, RX5G_CFG1_OVR, core, ovr_rx5g_mix_pu, 0x1);
			MOD_RADIO_REG_20696(pi, RX5G_REG4, core, rx5g_gm_pu, 0x1);
			MOD_RADIO_REG_20696(pi, RX5G_REG5, core, rx5g_mix_pu, 0x1);

			/* Logen Rx 5G power up sequence
			 * powering up/down LogenRx core, bias, div2 buffers and rccr
			 */
			MOD_RADIO_PLLREG_20696(pi, LOGEN_REG0, logen_pu, 0x1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_div2_pu, 0);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_bias_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_gm_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_lc_pu, 1);

			/* Mux powerups for core 2 and core 3 */
			if ((core == 2) || (core == 3))
				MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG5, core, logen_mux_pu, 1);

			/* Rx rccr and rx div2 buf power up/down */
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_OVR0, core, ovr_logen_div2_rxbuf_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_div2_rxbuf_pu, 0);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_OVR0, core, ovr_logen_rx_rccr_pu, 1);
			MOD_RADIO_REG_20696(pi, LOGEN_CORE_REG0, core, logen_rx_rccr_pu, 1);

			/* RX IQCAL 5G in mode 0: using iPAPD path and main GM */
			MOD_RADIO_REG_20696(pi, TX5G_MIX_REG0, core, cal5g_pa_pu, 0x1);
			MOD_RADIO_REG_20696(pi, TX5G_MISC_CFG1, core, cal5g_pa_atten, 0);
			MOD_RADIO_REG_20696(pi, RX5G_REG4, core, rx5g_coup_loopback_en, 0x1);
			MOD_RADIO_REG_20696(pi, RX5G_REG4, core, rx5g_coup_loopback_attn, 0);
			MOD_RADIO_REG_20696(pi, RX5G_REG2, core, rx5g_lna_auxpath_en, 0x0);
			MOD_RADIO_REG_20696(pi, RX5G_REG2, core, rx5g_lna_epapd_en, 0x0);
			MOD_RADIO_REG_20696(pi, RX5G_REG4, core, rx5g_gm_loopback_en_mainpath,
				0x1);
			MOD_RADIO_REG_20696(pi, RX5G_REG4, core, rx5g_gm_loopback_en_auxpath, 0x0);
			MOD_RADIO_REG_20696(pi, RX5G_REG1, core, rx5g_lna_tr_rx_en, 0x0);
			MOD_RADIO_REG_20696(pi, RX5G_REG1, core, rx5g_lna_pu, 0x0);
			MOD_RADIO_REG_20696(pi, RX5G_REG1, core, rx5g_lna_out_short, 0x0);
			MOD_RADIO_REG_20696(pi, RX5G_REG1, core, rx5g_lna_bypass, 0x0);

			MOD_RADIO_REG_20696(pi, TXDAC_REG3, core, i_config_IQDACbuf_2g_cmref_en, 0);
		}
	}
}


static void
wlc_phy_rxcal_radio_cleanup_acphy_28nm(phy_ac_rxiqcal_info_t *ti)
{
	phy_ac_reg_cache_restore(ti->aci, RADIOREGS_RXIQCAL);
}

static void
wlc_phy_rxcal_radio_cleanup_acphy(phy_ac_rxiqcal_info_t *rxiqcali)
{
	phy_info_t *pi = rxiqcali->pi;
	acphy_2069_rxcal_radioregs_t *porig = rxiqcali->ac_2069_rxcal_radioregs_orig;
	uint8 core;

	ASSERT(RADIOID_IS(pi->pubpi->radioid, BCM2069_ID));

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	ASSERT(porig->is_orig);
	porig->is_orig = FALSE;

	FOREACH_CORE(pi, core) {
		phy_utils_write_radioreg(pi, RF_2069_TXRX2G_CAL_TX(core),
			porig->rf_2069_txrx2g_cal_tx[core]);
		phy_utils_write_radioreg(pi, RF_2069_TXRX5G_CAL_TX(core),
			porig->rf_2069_txrx5g_cal_tx[core]);
		phy_utils_write_radioreg(pi, RF_2069_TXRX2G_CAL_RX(core),
			porig->rf_2069_txrx2g_cal_rx[core]);
		phy_utils_write_radioreg(pi, RF_2069_TXRX5G_CAL_RX(core),
			porig->rf_2069_txrx5g_cal_rx[core]);
		phy_utils_write_radioreg(pi, RF_2069_RXRF2G_CFG2(core),
			porig->rf_2069_rxrf2g_cfg2[core]);
		phy_utils_write_radioreg(pi, RF_2069_RXRF5G_CFG2(core),
			porig->rf_2069_rxrf5g_cfg2[core]);
	}

	if (ACMAJORREV_1(pi->pubpi->phy_rev) && (ACMINORREV_0(pi) || ACMINORREV_1(pi))) {
		MOD_PHYREG(pi, RxFeCtrl1, rxfe_bilge_cnt, 0);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 1);
		MOD_PHYREG(pi, RxFeCtrl1, soft_sdfeFifoReset, 0);
	}
	if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID) &&
		(RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
		(ACRADIO_2069_EPA_IS(pi->pubpi->radiorev)) &&
		(CHSPEC_IS5G(pi->radio_chanspec))) {
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		  MOD_RADIO_REGC(pi, OVR7, core, ovr_lna5g_tr_rx_en, 0x0);
		  MOD_PHYREGCE(pi, RfctrlIntc, core, override_ext_lna, 0);
		}
	}
}


/* see also: proc acphy_rx_iq_cal_txrxgain_control { core } */
static void
wlc_phy_rxcal_loopback_gainctrl_acphy(phy_ac_rxiqcal_info_t *rxiqcali)
{
	/*
	 * joint tx-rx gain control for Rx IQ calibration
	 */

	/* gain candidates tables,
	 * columns are: B1 B0 L2 Tx-Pwr-Idx
	 * rows are monotonically increasing gain
	 */
	acphy_rxcal_txrxgain_t gaintbl_5G[ACPHY_RXCAL_NUMGAINS] =
		{
		{0, 0, 0},
		{0, 1, 0},
		{0, 2, 0},
		{0, 3, 0},
		{0, 4, 0},
		{1, 4, 0},
		{2, 4, 0},
		{3, 4, 0},
		{4, 4, 0},
		{5, 4, 0},
		{5, 5, 0}
		};
	acphy_rxcal_txrxgain_t gaintbl_2G[ACPHY_RXCAL_NUMGAINS] =
	 {
		{0, 0, 10},
		{0, 1, 10},
		{0, 2, 10},
		{0, 3, 10},
		{0, 4, 10},
		{0, 5, 10},
		{1, 5, 10},
		{2, 5, 10},
		{3, 5, 10},
		{4, 5, 10},
		{5, 5, 10}
	 };
	uint16 num_samps = 1024;
	uint32 thresh_pwr_hi = 5789 /* thresh_pwr (=4100)* 1.412 */;
	uint32 thresh_pwr_lo = 2903 /* thresh_pwr (=4100)/ 1.412 */;
	phy_iq_est_t est[PHY_CORE_MAX];
	/* threshold for "too high power"(313 mVpk, where clip = 400mVpk in 4322) */
	uint32 i_pwr, q_pwr, curr_pwr, optim_pwr = 0;
	uint32 curr_pwr_tmp;

	uint8 gainctrl_dirn[PHY_CORE_MAX];
	bool gainctrl_done[PHY_CORE_MAX];
	bool gainctrl_not_done;
	uint16 mix_tia_gain[PHY_CORE_MAX];
	int8 curr_gaintbl_index[PHY_CORE_MAX];

	acphy_rxcal_txrxgain_t *gaintbl;
	uint16 lpf_biq1_gain, lpf_biq0_gain;

	int8 txpwrindex;

	uint8 core, lna2_gain = 0, lna1_gain = 0;
	phy_info_t *pi = rxiqcali->pi;

	ASSERT(RADIOID_IS(pi->pubpi->radioid, BCM2069_ID));
	if (CHSPEC_IS5G(pi->radio_chanspec)) {
		if (ACREV_GT(pi->pubpi->phy_rev, 0)) {
			lna2_gain = 6;
		}
	}
	/* 4335a0/b0 epa : turn on lna2 */
	if ((RADIO2069_MAJORREV(pi->pubpi->radiorev) == 1) &&
		ACRADIO_2069_EPA_IS(pi->pubpi->radiorev)) {
		lna2_gain = 6;
	}
	BCM_REFERENCE(optim_pwr);
	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

#if defined(BCMDBG_RXCAL)
	printf("Rx IQCAL : Loopback Gain Control\n");
#endif /* BCMDBG_RXCAL */

	/* gain candidates */
	gaintbl = gaintbl_2G;
	if (CHSPEC_IS5G(pi->radio_chanspec)) {
		gaintbl = gaintbl_5G;
	}

	FOREACH_CORE(pi, core) {
		gainctrl_dirn[core] = ACPHY_RXCAL_GAIN_INIT;
		gainctrl_done[core] = FALSE;

		/* retrieve Rx Mixer/TIA gain from InitGain and via GainBits table */
		mix_tia_gain[core] = READ_PHYREGFLDC(pi, InitGainCodeA, core, initmixergainIndex);

		curr_gaintbl_index[core] = 0;
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			curr_gaintbl_index[core] = 4;
		}
	}

	do {

		FOREACH_CORE(pi, core) {
			if (!gainctrl_done[core]) {

				lpf_biq1_gain = gaintbl[curr_gaintbl_index[core]].lpf_biq1;
				lpf_biq0_gain = gaintbl[curr_gaintbl_index[core]].lpf_biq0;
				txpwrindex = gaintbl[curr_gaintbl_index[core]].txpwrindex;

				if ((ACMAJORREV_1(pi->pubpi->phy_rev) ||
				     ACMAJORREV_2(pi->pubpi->phy_rev) ||
				     ACMAJORREV_5(pi->pubpi->phy_rev)) && PHY_ILNA(pi)) {
					if (CHSPEC_IS5G(pi->radio_chanspec)) {
						txpwrindex = 80;
					} else {
						txpwrindex = 40;
					}
				}
				/* supply max gain from LNA1,LNA2 */
				if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID) &&
					(RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
					(ACRADIO_2069_EPA_IS(pi->pubpi->radiorev)) &&
					(CHSPEC_IS5G(pi->radio_chanspec))) {
				  lna1_gain = 5;
				  lna2_gain = 1;
				  txpwrindex = rxiqcali->txpwridx_for_rxiqcal[core];
				}

				if (IS_4364_3x3(pi)) {
					txpwrindex = ((CHSPEC_IS5G(pi->radio_chanspec)) ? 20 : 40);
				}

				/* rx */
				/* LNA1 bypass mode */
				WRITE_PHYREGCE(pi, RfctrlCoreRXGAIN1, core,
					mix_tia_gain[core] << 6 | lna2_gain << 3 | lna1_gain);

				if (CHSPEC_IS2G(pi->radio_chanspec)) {
					WRITE_PHYREGCE(pi, RfctrlCoreRXGAIN2, core, 0);
				} else if (CHSPEC_IS5G(pi->radio_chanspec)) {
					WRITE_PHYREGCE(pi, RfctrlCoreRXGAIN2, core, 4);
				}

				WRITE_PHYREGCE(pi, RfctrlCoreLpfGain, core,
					(lpf_biq1_gain << 3) | lpf_biq0_gain);

				MOD_PHYREGCE(pi, RfctrlOverrideGains, core, rxgain, 1);
				MOD_PHYREGCE(pi, RfctrlOverrideGains, core, lpf_bq1_gain, 1);
				MOD_PHYREGCE(pi, RfctrlOverrideGains, core, lpf_bq2_gain, 1);

				/* tx */
				wlc_phy_txpwr_by_index_acphy(pi, (1 << core), txpwrindex);
			}
		}

		/* turn on testtone (this will override bbmult, but that's ok) */
		wlc_phy_tx_tone_acphy(pi, (((CHSPEC_IS80(pi->radio_chanspec) ||
			PHY_AS_80P80(pi, pi->radio_chanspec)) ?
			ACPHY_IQCAL_TONEFREQ_80MHz : (CHSPEC_IS160(pi->radio_chanspec)) ?
			0 : (CHSPEC_IS40(pi->radio_chanspec)) ?
			ACPHY_IQCAL_TONEFREQ_40MHz : ACPHY_IQCAL_TONEFREQ_20MHz) >> 1),
			ACPHY_RXCAL_TONEAMP, 0, 0, FALSE);

		if (CHSPEC_IS160(pi->radio_chanspec) &&
				!PHY_AS_80P80(pi, pi->radio_chanspec)) {
			ASSERT(0);
		}

		/* estimate digital power using rx_iq_est
		*/
		wlc_phy_rx_iq_est_acphy(pi, est, num_samps, 32, 0, FALSE);

		/* Turn off the tone */
		wlc_phy_stopplayback_acphy(pi);

		gainctrl_not_done = FALSE;

		FOREACH_CORE(pi, core) {
			if (!gainctrl_done[core]) {

				i_pwr = (est[core].i_pwr + num_samps / 2) / num_samps;
				q_pwr = (est[core].q_pwr + num_samps / 2) / num_samps;
				curr_pwr = i_pwr + q_pwr;
			PHY_NONE(("core %u (gain idx %d): i_pwr = %u, q_pwr = %u, curr_pwr = %d\n",
				core, curr_gaintbl_index[core], i_pwr, q_pwr, curr_pwr));

#if defined(BCMDBG_RXCAL)
			printf("Core-%d : g_id=%d MX:%d LNA2: %d BQ0:%d BQ1:%d tx_id = %d Pwr=%d\n",
				core, curr_gaintbl_index[core], mix_tia_gain[core],
				lna2_gain,
				gaintbl[curr_gaintbl_index[core]].lpf_biq0,
				gaintbl[curr_gaintbl_index[core]].lpf_biq1,
				gaintbl[curr_gaintbl_index[core]].txpwrindex, curr_pwr);
#endif /* BCMDBG_RXCAL */

				switch (gainctrl_dirn[core]) {
				case ACPHY_RXCAL_GAIN_INIT:
					if (curr_pwr > thresh_pwr_hi) {
						gainctrl_dirn[core] = ACPHY_RXCAL_GAIN_DOWN;
						curr_pwr_tmp = curr_pwr;
						while ((curr_pwr_tmp > thresh_pwr_hi) &&
						(curr_gaintbl_index[core] > 1)) {
							curr_gaintbl_index[core]--;
							curr_pwr_tmp /= 2;
						}
					} else if  (curr_pwr < thresh_pwr_lo) {
						gainctrl_dirn[core] = ACPHY_RXCAL_GAIN_UP;
						curr_pwr_tmp = curr_pwr;
						if (curr_pwr_tmp != 0) {
							while ((curr_pwr_tmp < thresh_pwr_lo) &&
								(curr_gaintbl_index[core] <
								ACPHY_RXCAL_NUMGAINS - 3)) {
								curr_gaintbl_index[core]++;
								curr_pwr_tmp *= 2;
							}
						}
					} else {
						gainctrl_done[core] = TRUE;
						optim_pwr = curr_pwr;
					}
					break;

				case ACPHY_RXCAL_GAIN_UP:
					if (curr_pwr > thresh_pwr_lo) {
						gainctrl_done[core] = TRUE;
						optim_pwr = curr_pwr;
					} else {
						curr_gaintbl_index[core]++;
					}
					break;

				case ACPHY_RXCAL_GAIN_DOWN:
					if (curr_pwr > thresh_pwr_hi) {
						curr_gaintbl_index[core]--;
					} else {
						gainctrl_done[core] = TRUE;
						optim_pwr = curr_pwr;
					}
					break;

				default:
					PHY_ERROR(("Invalid gaintable direction id %d\n",
						gainctrl_dirn[core]));
					ASSERT(0);
				}

				if ((curr_gaintbl_index[core] < 0) ||
				(curr_gaintbl_index[core] >= ACPHY_RXCAL_NUMGAINS)) {
					gainctrl_done[core] = TRUE;
					optim_pwr = curr_pwr;
				}

				gainctrl_not_done = gainctrl_not_done || (!gainctrl_done[core]);

#if defined(BCMDBG_RXCAL)
				/* Store the signal powers for SNR calculations later */
				rxiqcali->rxcal_signal[core] = est[core];
#endif /* BCMDBG_RXCAL */
			}
		}

	} while (gainctrl_not_done);
}

/* see also: proc acphy_rx_iq_cal_txrxgain_control_tiny { } */
static void
wlc_phy_rxcal_txrx_gainctrl_acphy_tiny(phy_ac_rxiqcal_info_t *rxiqcali)
{
	/* table for leakage path 5G : lna,tia,far,dvga */
	acphy_rxcal_rxgain_t gaintbl_5G[ACPHY_RXCAL_NUMRXGAINS] = {
		{ -4, 0, 2, 0, 0 },
		{ -4, 0, 1, 0, 0 },
		{ -4, 0, 0, 0, 0 },
		{ -3, 0, 0, 0, 0 },
		{ -2, 0, 0, 0, 0 },
		{ -1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0 },
		{ 0, 1, 0, 0, 0 },
		{ 0, 2, 0, 0, 0 },
		{ 0, 3, 0, 0, 0 },
		{ 0, 4, 0, 0, 0 },
		{ 0, 5, 0, 0, 0 },
		{ 0, 6, 0, 0, 0 },
		{ 0, 7, 0, 0, 0 },
		{ 0, 8, 0, 0, 0 },
		{ 0, 9, 0, 0, 0 }
	};

	/* table for papd loopback path 2G : lna,tia,far,dvga */
	acphy_rxcal_rxgain_t gaintbl_2G[ACPHY_RXCAL_NUMRXGAINS] = {
		{ 0, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 0 },
		{ 0, 1, 0, 0, 0 },
		{ 0, 2, 0, 0, 0 },
		{ 0, 3, 0, 0, 0 },
		{ 0, 4, 0, 0, 0 },
		{ 0, 5, 0, 0, 0 },
		{ 0, 6, 0, 0, 0 },
		{ 0, 7, 0, 0, 0 },
		{ 0, 8, 0, 0, 0 },
		{ 0, 9, 0, 0, 0 },
		{ 0, 10, 0, 0, 0 },
		{ 0, 10, 0, 1, 0 },
		{ 0, 10, 0, 2, 0 },
		{ 0, 10, 0, 3, 0 },
		{ 0, 10, 0, 4, 0 }
	};

	acphy_rxcal_rxgain_t *gaintbl;
	uint8 core;
	uint8 g_index, done, found_ideal, wn, txindex;
	bool txdone;
	uint8 do_max = 10;	/* >= ACPHY_RXCAL_NUMRXGAINS / 2 */
	uint8 tia, far, lna_idx;
	int8 lna;
	uint16 num_samps = 1024;
	phy_info_t *pi = rxiqcali->pi;
	uint32 meansq_max = (PHY_ILNA(pi)) ? 4000 : 7000; /* As dictated by iqest / dc offsets */

	/*
	 * Set min power more than max gain step below max power to prevent AGC hunting
	 * set 8 dB below max setting
	 */
	uint32 meansq_min = (PHY_ILNA(pi)) ?  535 : 1111; /* -8dB on pwr_max / 6.3  */
	uint32 i_meansq = 0;
	uint32 q_meansq = 0;
	uint8 lna2 = 0;
	uint8 lna2_rout = 0;
	uint8 dvga = 0;
	const uint8 txindx_start = 104;
	const uint8 txindx_stop  = (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev)) ? 20 : 56;
	const uint8 txindx_step  = 12;
	int p;
	uint8 clipDet;
	uint8 lna_gain_code, lna_rout_code;

	phy_iq_est_t est[PHY_CORE_MAX];

	(void)memset(est, 0, sizeof(est));
	/* JIRA:CRDOT11ACPHY-1542: rx_iq_est is increased to 10 bits */
	if (ACMAJORREV_33(pi->pubpi->phy_rev)) {
	  meansq_max = meansq_max * 16;
	  meansq_min = meansq_min * 16;
	}

	if (CHSPEC_IS5G(pi->radio_chanspec)) {
		gaintbl = gaintbl_5G;
	} else {
		gaintbl = gaintbl_2G;
	}

	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev)) {
		/* reusing PAPD path for 5G as well in 4349A0,
		 * so using the same gain sets as for 2G PAPD
		 * loopback path
		 */
		gaintbl = gaintbl_2G;
		/* use maximum LNA2 gain index */
		lna2 = 3;
	}

	if (IS_4364_1x1(pi)) {
		uint8 txindx_start_1x1 = 45;
		FOREACH_CORE(pi, core) {
				wlc_phy_set_txpwr_by_index_acphy(pi, (1 << core), txindx_start_1x1);
		}
	}

	FOREACH_CORE(pi, core) {
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev)) {
			g_index = 1;
		} else {
			g_index = 8;
		}
		done = 0;
		found_ideal = 0;
		if (ACMAJORREV_4(pi->pubpi->phy_rev) || IS_4364_1x1(pi)) {
			txdone = 1;
		} else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev)) {
			txdone = 0;
		} else {
			txdone = CHSPEC_IS2G(pi->radio_chanspec);	/* only adapt tx in 5G */
		}
		wn = 0;
		txindex = txindx_start;

		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev))
			lna_idx = 5;   /* always use highest for best SNR */
		else
			lna_idx = READ_PHYREGFLDC(pi, InitGainCodeA, core, initLnaIndex);

		while ((done != do_max) && (g_index != 0) && (g_index != ACPHY_RXCAL_NUMRXGAINS)) {
			if (CHSPEC_IS2G(pi->radio_chanspec) ||
				(CHSPEC_IS5G(pi->radio_chanspec) &&
				(ACMAJORREV_4(pi->pubpi->phy_rev) ||
				ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev)))) {
				/* papd loopback path */
				 lna = lna_idx;
				 tia = gaintbl[g_index].tia;
				 far = gaintbl[g_index].far;
				 dvga = gaintbl[g_index].dvga;
			} else { /* leakage path for 5G */
				lna = lna_idx + gaintbl[g_index].lna;
				tia = gaintbl[g_index].tia;
				far = gaintbl[g_index].far;
			}

			MOD_PHYREG(pi, RxSdFeConfig6, rx_farrow_rshift_0, far);
			MOD_PHYREGCE(pi, RfctrlOverrideGains, core, rxgain, 1);

			lna_gain_code = wlc_phy_get_lna_gain_rout(pi, lna, GET_LNA_GAINCODE);
			lna_rout_code = wlc_phy_get_lna_gain_rout(pi, lna, GET_LNA_ROUT);

			WRITE_PHYREGCE(pi, RfctrlCoreRXGAIN1, core,
			               (dvga << 10) | (tia << 6) | (lna2 << 3) |
			               lna_gain_code);
			WRITE_PHYREGCE(pi, RfctrlCoreRXGAIN2, core,
			               ((lna2_rout << 4) | (lna_rout_code & 0xf)));

			MOD_PHYREG(pi, RfseqCoreActv2059, EnTx,
				((ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev)) ? 0xf : 0x7));

			if (!txdone)
				wlc_phy_set_txpwr_by_index_acphy(pi, (1 << core), txindex);

			/* turn on testtone */
			wlc_phy_tx_tone_acphy(pi, (((CHSPEC_IS80(pi->radio_chanspec) ||
			                              PHY_AS_80P80(pi, pi->radio_chanspec))
			                              ? ACPHY_IQCAL_TONEFREQ_80MHz
			                              : (CHSPEC_IS160(pi->radio_chanspec))
			                              ? 0
			                              : (CHSPEC_IS40(pi->radio_chanspec))
			                              ? ACPHY_IQCAL_TONEFREQ_40MHz
			                              : ACPHY_IQCAL_TONEFREQ_20MHz) >> 1),
			                              ACPHY_RXCAL_TONEAMP, 0, 0, FALSE);

			if (!PHY_AS_80P80(pi, pi->radio_chanspec) &&
					CHSPEC_IS160(pi->radio_chanspec)) {
				ASSERT(0);
			}

			/*
			 * Check for RF saturation by (1) power detect or (2) bb power.
			 * See txdone condition.
			 */
			wn = 0;
			for (p = 0; p < 8; p++) {
				wn +=  READ_RADIO_REGFLD_TINY(pi, LNA5G_RSSI2, core,
				                               lna5g_dig_wrssi1_out_low);
				wn +=  READ_RADIO_REGFLD_TINY(pi, TIA_CFG14, core, nbrssi_Ich_low);
				wn +=  READ_RADIO_REGFLD_TINY(pi, TIA_CFG14, core, nbrssi_Qch_low);
			}

			/* estimate digital power using rx_iq_est */
			wlc_phy_rx_iq_est_acphy(pi, est, num_samps, 32, 0, FALSE);

			/* Turn off the tone */
			wlc_phy_stopplayback_acphy(pi);

			i_meansq = (est[core].i_pwr + num_samps / 2) / num_samps;
			q_meansq = (est[core].q_pwr + num_samps / 2) / num_samps;
			if ((core == 3) && (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				ACMAJORREV_33(pi->pubpi->phy_rev))) {
			  clipDet = READ_PHYREGFLDCXE(pi, IqestCmd, clipDet, core-1);
			} else {
			  clipDet = READ_PHYREGFLDCXE(pi, IqestCmd, clipDet, core);
			}
#if defined(BCMDBG_RXCAL)
			printf("RxIQCAL[%d]: txindx=%d g_index=%d lna=%d tia=%d far=%d dvga=%d\n",
			       core, txindex, g_index, lna, tia, far, dvga);
			printf("RxIQCAL[%d]: "
				"i_meansq=%d q_meansq=%d meansq_max=%d meansq_min=%d clipDet=%d\n",
				core, i_meansq, q_meansq, meansq_max, meansq_min, clipDet);
#endif
			txdone = txdone ||
				(txindex < txindx_stop) || (wn > 0) || (i_meansq > meansq_min) ||
				(q_meansq > meansq_min);

			if (!txdone) {
				txindex -= txindx_step;
				continue;
			}

			if ((i_meansq > meansq_max) || (q_meansq > meansq_max) || (clipDet == 1)) {
				g_index--;
				done++;
			} else if ((i_meansq < meansq_max) && (q_meansq < meansq_min)) {
				g_index++;
				done++;
			} else {
				done = do_max;
				found_ideal = 1;
			}
		}
		if (found_ideal == 0) {
			PHY_ERROR(("%s: Too much or too little power? "
				"[core: %d, pwr: (%d, %d), gain_index=%d]\n",
				__FUNCTION__, core, i_meansq, q_meansq, g_index));
		}
	}
}

static int
phy_28nm_wn(phy_ac_rxiqcal_info_t *rxiqcali)
{
	phy_info_t *pi = rxiqcali->pi;
	int nb_clip1_cnt = READ_PHYREGFLD(pi, NbClipCnt1, NbClipCntAccum1_i)
			+ READ_PHYREGFLD(pi, NbClipCnt1, NbClipCntAccum1_q);
	int w1_clip1_cnt = READ_PHYREGFLD(pi, W2W1ClipCnt1, W1ClipCntAccum1);
	int w3_clip1_cnt = READ_PHYREGFLD(pi, W3ClipCnt1, W3ClipCntAccum1_i)
			+ READ_PHYREGFLD(pi, W3ClipCnt1, W3ClipCntAccum1_q);
	return (nb_clip1_cnt + w1_clip1_cnt + w3_clip1_cnt);
}

static void
wlc_phy_rxcal_txrx_gainctrl_acphy_28nm(phy_ac_rxiqcal_info_t *rxiqcali)
{
	/* table for leakage path 5G : lna,tia,far,dvga */
	acphy_rxcal_rxgain_t gaintbl_5G[ACPHY_RXCAL_NUMRXGAINS] = {
		{ -4, 0, 2, 0, 0 },
		{ -4, 0, 1, 0, 0 },
		{ -4, 0, 0, 0, 0 },
		{ -3, 0, 0, 0, 0 },
		{ -2, 0, 0, 0, 0 },
		{ -1, 0, 0, 0, 0 },
		{ 0, 0, 0, 0, 0 },
		{ 0, 1, 0, 0, 0 },
		{ 0, 2, 0, 0, 0 },
		{ 0, 3, 0, 0, 0 },
		{ 0, 4, 0, 0, 0 },
		{ 0, 5, 0, 0, 0 },
		{ 0, 6, 0, 0, 0 },
		{ 0, 7, 0, 0, 0 },
		{ 0, 8, 0, 0, 0 },
		{ 0, 9, 0, 0, 0 }
	};

	/* table for papd loopback path 2G : lna,tia,far,dvga, bq */
	acphy_rxcal_rxgain_t gaintbl_2G[ACPHY_RXCAL_NUMRXGAINS] = {
		{ 0, 0, 0, 0, 0 },
		{ 0, 1, 0, 0, 0 },
		{ 0, 2, 0, 0, 0 },
		{ 0, 3, 0, 0, 0 },
		{ 0, 4, 0, 0, 0 },
		{ 0, 5, 0, 0, 0 },
		{ 0, 5, 0, 0, 2 },
		{ 0, 5, 0, 0, 3 },
		{ 0, 5, 0, 0, 4 },
		{ 0, 5, 0, 0, 4 },
		{ 0, 5, 0, 0, 4 },
		{ 0, 5, 0, 0, 4 },
		{ 0, 5, 0, 0, 4 },
		{ 0, 5, 0, 0, 4 },
		{ 0, 5, 0, 0, 4 },
		{ 0, 5, 0, 0, 4 }
	};


	/* table for papd loopback path 2G and 5G : lna,tia,far,dvga, bq */
	acphy_rxcal_rxgain_t gaintbl_2G_5G_majrev37[ACPHY_RXCAL_NUMRXGAINS] = {
		{0, 0, 0, 0, 0},
		{0, 0, 0, 0, 1},
		{0, 0, 0, 0, 2},
		{0, 1, 0, 0, 0},
		{0, 1, 0, 0, 1},
		{0, 1, 0, 0, 2},
		{0, 2, 0, 0, 0},
		{0, 2, 0, 0, 1},
		{0, 2, 0, 0, 2},
		{0, 3, 0, 0, 0},
		{0, 3, 0, 0, 1},
		{0, 3, 0, 0, 2},
		{0, 4, 0, 0, 0},
		{0, 4, 0, 0, 1},
		{0, 4, 0, 0, 2},
		{0, 5, 0, 0, 0}
	};

	acphy_rxcal_rxgain_t *gaintbl;
	uint8 core;
	uint8 g_index, done, found_ideal, wn, txindex;
	bool txdone;
	uint8 do_max = 10;	/* >= ACPHY_RXCAL_NUMRXGAINS / 2 */
	uint8 tia, far, lna_idx;
	int8 lna;
	uint16 num_samps = 1024;
	phy_info_t *pi = rxiqcali->pi;
	uint32 meansq_max = (PHY_ILNA(pi)) ? (ACMAJORREV_40(pi->pubpi->phy_rev)) ? 16000 : 4000 :
		(ACMAJORREV_37(pi->pubpi->phy_rev) || ACMAJORREV_40(pi->pubpi->phy_rev)) ? 28000 :
			7000;
	/* As dictated by iqest / dc offsets */

	/*
	 * Set min power more than max gain step below max power to prevent AGC hunting
	 * set 8 dB below max setting
	 */
	uint32 meansq_min = (PHY_ILNA(pi)) ?  (ACMAJORREV_40(pi->pubpi->phy_rev)) ? 2540 : 535 :
		(ACMAJORREV_37(pi->pubpi->phy_rev) || ACMAJORREV_40(pi->pubpi->phy_rev)) ? 4444 :
			1111; /* -8dB on pwr_max / 6.3  */
	uint32 i_meansq = 0;
	uint32 q_meansq = 0;
	uint8 lna2 = 0;
	uint8 lna2_rout = 0;
	uint8 dvga = 0;
	uint8 bq1 = 0;
	const uint8 txindx_start = (ACMAJORREV_37(pi->pubpi->phy_rev) ||
		ACMAJORREV_40(pi->pubpi->phy_rev)) ? 60 : 104;
	const uint8 txindx_stop  = (ACMAJORREV_37(pi->pubpi->phy_rev) ||
		ACMAJORREV_40(pi->pubpi->phy_rev)) ? 1 : 56;
	const uint8 txindx_step  = (ACMAJORREV_37(pi->pubpi->phy_rev) ||
		ACMAJORREV_40(pi->pubpi->phy_rev)) ? 7 : 12;
	uint8 clipDet, txindx_final[PHY_CORE_MAX];
	uint8 lna_gain_code, lna_rout_code;
	phy_iq_est_t est[PHY_CORE_MAX];

	(void)memset(est, 0, sizeof(est));

	if (CHSPEC_IS5G(pi->radio_chanspec)) {
		gaintbl = gaintbl_5G;
	} else {
		gaintbl = gaintbl_2G;
	}

	if (ACMAJORREV_36(pi->pubpi->phy_rev) || ACMAJORREV_40(pi->pubpi->phy_rev)) {
		gaintbl = gaintbl_2G;
		lna2 = 3;
	} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
		gaintbl = gaintbl_2G_5G_majrev37;
		lna2 = 3;
	}

	FOREACH_CORE(pi, core) {
		if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
			g_index = 12;
		} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
			g_index = 3;
		} else {
			g_index = 8;
		}
		done = 0;
		found_ideal = 0;
		txdone = (ACMAJORREV_37(pi->pubpi->phy_rev) || ACMAJORREV_40(pi->pubpi->phy_rev)) ?
			0 : CHSPEC_IS2G(pi->radio_chanspec);	/* only adapt tx in 5G */
		wn = 0;
		txindex = txindx_start;
		txindx_final[core] = txindx_start;
		lna_idx = READ_PHYREGFLDC(pi, InitGainCodeA, core, initLnaIndex);
		while ((done != do_max) && (g_index != 0) && (g_index != ACPHY_RXCAL_NUMRXGAINS)) {
			if (CHSPEC_IS2G(pi->radio_chanspec) ||
				(CHSPEC_IS5G(pi->radio_chanspec) &&
				ACMAJORREV_36(pi->pubpi->phy_rev)) ||
				ACMAJORREV_37(pi->pubpi->phy_rev) ||
				ACMAJORREV_40(pi->pubpi->phy_rev)) {
				/* papd loopback path */
				 lna = lna_idx;
				 tia = gaintbl[g_index].tia;
				 far = gaintbl[g_index].far;
				 dvga = gaintbl[g_index].dvga;
				 bq1 = gaintbl[g_index].bq1;
			} else { /* leakage path for 5G */
				lna = lna_idx + gaintbl[g_index].lna;
				tia = gaintbl[g_index].tia;
				far = gaintbl[g_index].far;
			}

			MOD_PHYREG(pi, RxSdFeConfig6, rx_farrow_rshift_0, far);
			MOD_PHYREGCE(pi, RfctrlOverrideGains, core, rxgain, 1);

			lna_gain_code = wlc_phy_get_lna_gain_rout(pi, lna, GET_LNA_GAINCODE);
			lna_rout_code = wlc_phy_get_lna_gain_rout(pi, lna, GET_LNA_ROUT);
			WRITE_PHYREGCE(pi, RfctrlCoreRXGAIN1, core,
			               (dvga << 10) | (tia << 6) | (lna2 << 3) |
			               lna_gain_code);
			WRITE_PHYREGCE(pi, RfctrlCoreLpfGain, core, (bq1));
			WRITE_PHYREGCE(pi, RfctrlCoreRXGAIN2, core,
			               ((lna2_rout << 4) | (lna_rout_code & 0xf)));

			MOD_PHYREG(pi, RfseqCoreActv2059, EnTx, 0xf);

			if (!txdone)
				wlc_phy_set_txpwr_by_index_acphy(pi, (1 << core), txindex);

			/* turn on testtone */
			wlc_phy_tx_tone_acphy(pi, (((CHSPEC_IS80(pi->radio_chanspec))
			                          ? ACPHY_IQCAL_TONEFREQ_80MHz
			                          : (CHSPEC_IS40(pi->radio_chanspec))
			                              ? ACPHY_IQCAL_TONEFREQ_40MHz
			                              : ACPHY_IQCAL_TONEFREQ_20MHz) >> 1),
			                              ACPHY_RXCAL_TONEAMP, 0, 0, FALSE);

			/*
			 * Check for RF saturation by (1) power detect or (2) bb power.
			 * See txdone condition.
			 */
			wn = 0;
			if (ACMAJORREV_36(pi->pubpi->phy_rev) ||
			ACMAJORREV_40(pi->pubpi->phy_rev)) {
				wn = (uint8)phy_28nm_wn(rxiqcali);
			}
			/* estimate digital power using rx_iq_est */
			wlc_phy_rx_iq_est_acphy(pi, est, num_samps, 32, 0, FALSE);

			/* Turn off the tone */
			wlc_phy_stopplayback_acphy(pi);

			i_meansq = (est[core].i_pwr + num_samps / 2) / num_samps;
			q_meansq = (est[core].q_pwr + num_samps / 2) / num_samps;

			if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
				/* Ignore clip detect for 7271 */
				clipDet = 0;
			} else {
				clipDet = READ_PHYREGFLDCXE(pi, IqestCmd, clipDet, core);
			}

#if defined(BCMDBG_RXCAL)
			printf("RxIQCAL[%d]: txindx=%d g_index=%d lna=%d tia=%d far=%d dvga=%d\n",
			       core, txindex, g_index, lna, tia, far, dvga);
			printf("RxIQCAL[%d]: "
				"i_meansq=%d q_meansq=%d meansq_max=%d meansq_min=%d clipDet=%d\n",
				core, i_meansq, q_meansq, meansq_max, meansq_min, clipDet);
#endif

			txdone = txdone ||
				(txindex < txindx_stop) || (wn > 0) || (i_meansq > meansq_min) ||
				(q_meansq > meansq_min);

			if (!txdone && ((txindex - txindx_step) > 0)) {
				txindex -= txindx_step;
				continue;
			}

			if ((i_meansq > meansq_max) || (q_meansq > meansq_max) || (clipDet == 1)) {
				g_index--;
				done++;
			} else if ((i_meansq < meansq_min) && (q_meansq < meansq_min)) {
				g_index++;
				done++;
			} else {
				done = do_max;
				found_ideal = 1;
				txindx_final[core] = txindex;
				wlc_phy_set_txpwr_by_index_acphy(pi, (1 << core), txindx_start);
			}
		}
#if defined(BCMDBG_RXCAL)
		/* Store the signal powers for SNR calculations later */
		rxiqcali->rxcal_signal[core] = est[core];
#endif /* BCMDBG_RXCAL */

		if (found_ideal == 0) {
			PHY_ERROR(("%s: Too much or too little power? "
				"[core: %d, pwr: (%d, %d), gain_index=%d]\n",
				__FUNCTION__, core, i_meansq, q_meansq, g_index));
		}
	}
	FOREACH_CORE(pi, core) {
		wlc_phy_set_txpwr_by_index_acphy(pi, (1 << core), txindx_final[core]);
	}
}

static void
wlc_phy_rxcal_phy_setup_acphy_save_rfctrl(phy_ac_rxiqcal_info_t *rxiqcali, uint8 core,
	acphy_rxcal_phyregs_t *porig)
{
	phy_info_t *pi = rxiqcali->pi;
	uint16 rfseq[] = { 0x100, 0x101, 0x102, 0x501 };
	int i;
	porig->txpwridx[core] = pi->u.pi_acphy->txpwrindex[core];

	porig->RfctrlOverrideTxPus[core] = READ_PHYREGCE(pi, RfctrlOverrideTxPus, core);
	porig->RfctrlOverrideRxPus[core] = READ_PHYREGCE(pi, RfctrlOverrideRxPus, core);
	porig->RfctrlOverrideGains[core] = READ_PHYREGCE(pi, RfctrlOverrideGains, core);
	porig->RfctrlOverrideLpfCT[core] = READ_PHYREGCE(pi, RfctrlOverrideLpfCT, core);
	porig->RfctrlOverrideLpfSwtch[core] = READ_PHYREGCE(pi, RfctrlOverrideLpfSwtch,
		core);
	porig->RfctrlOverrideAfeCfg[core] = READ_PHYREGCE(pi, RfctrlOverrideAfeCfg, core);
	porig->RfctrlOverrideLowPwrCfg[core] = READ_PHYREGCE(pi, RfctrlOverrideLowPwrCfg,
		core);
	porig->RfctrlOverrideAuxTssi[core] = READ_PHYREGCE(pi, RfctrlOverrideAuxTssi, core);

	porig->RfctrlCoreTxPus[core] = READ_PHYREGCE(pi, RfctrlCoreTxPus, core);
	porig->RfctrlCoreRxPus[core] = READ_PHYREGCE(pi, RfctrlCoreRxPus, core);
	porig->RfctrlCoreTXGAIN1[core] = READ_PHYREGCE(pi, RfctrlCoreTXGAIN1, core);
	porig->RfctrlCoreTXGAIN2[core] = READ_PHYREGCE(pi, RfctrlCoreTXGAIN2, core);
	porig->RfctrlCoreRXGAIN1[core] = READ_PHYREGCE(pi, RfctrlCoreRXGAIN1, core);
	porig->RfctrlCoreRXGAIN2[core] = READ_PHYREGCE(pi, RfctrlCoreRXGAIN2, core);
	porig->RfctrlCoreLpfGain[core] = READ_PHYREGCE(pi, RfctrlCoreLpfGain, core);
	porig->RfctrlCoreLpfCT[core] = READ_PHYREGCE(pi, RfctrlCoreLpfCT, core);
	porig->RfctrlCoreLpfGmult[core] = READ_PHYREGCE(pi, RfctrlCoreLpfGmult, core);
	porig->RfctrlCoreRCDACBuf[core] = READ_PHYREGCE(pi, RfctrlCoreRCDACBuf, core);
	porig->RfctrlCoreLpfSwtch[core] = READ_PHYREGCE(pi, RfctrlCoreLpfSwtch, core);
	porig->RfctrlCoreAfeCfg1[core] = READ_PHYREGCE(pi, RfctrlCoreAfeCfg1, core);
	porig->RfctrlCoreAfeCfg2[core] = READ_PHYREGCE(pi, RfctrlCoreAfeCfg2, core);
	porig->RfctrlCoreLowPwr[core] = READ_PHYREGCE(pi, RfctrlCoreLowPwr, core);
	porig->RfctrlCoreAuxTssi1[core] = READ_PHYREGCE(pi, RfctrlCoreAuxTssi1, core);
	porig->RfctrlCoreAuxTssi2[core] = READ_PHYREGCE(pi, RfctrlCoreAuxTssi2, core);
	porig->Dac_gain[core] = READ_PHYREGCE(pi, Dac_gain, core);

	wlc_phy_get_tx_bbmult_acphy(pi, &(porig->bbmult[core]), core);
	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
		ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
		for (i = 0; i < 3; i++) {
		  wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, rfseq[core] + (i * 3),
		  16, &porig->rfseq_txgain[core + (i * 3)]);
		}
	} else {
		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, (0x100 + core), 16,
		&porig->rfseq_txgain[core+0]);
		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, (0x103 + core), 16,
		&porig->rfseq_txgain[core+3]);
		wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, (0x106 + core), 16,
		&porig->rfseq_txgain[core+6]);
	}

	porig->RfctrlIntc[core] = READ_PHYREGCE(pi, RfctrlIntc, core);
}

static void
wlc_phy_rxcal_phy_setup_acphy_core(phy_ac_rxiqcal_info_t *rxiqcali, uint8 core, uint8 bw_idx,
	uint16 sdadc_config)
{
	phy_info_t *pi = rxiqcali->pi;
	MOD_PHYREGCE(pi, RfctrlIntc, core, ext_2g_papu, 0);
	if (RADIOID(pi->pubpi->radioid) == BCM2069_ID &&
	    (RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
	    (ACRADIO_2069_EPA_IS(pi->pubpi->radiorev)) &&
	    (CHSPEC_IS5G(pi->radio_chanspec))) {
		MOD_PHYREGCE(pi, RfctrlIntc, core, ext_5g_papu, 1);
	} else {
		MOD_PHYREGCE(pi, RfctrlIntc, core, ext_5g_papu, 0);
	}
	MOD_PHYREGCE(pi, RfctrlIntc, core, override_ext_pa, 1);
	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		MOD_PHYREGCE(pi, RfctrlIntc, core, override_ext_lna, 1);
	}

	MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, fast_nap_bias_pu, 1);
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		ACMAJORREV_37(pi->pubpi->phy_rev))
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, fast_nap_bias_pu, 0);

	if (TINY_RADIO(pi) || ACMAJORREV_36(pi->pubpi->phy_rev) ||
		ACMAJORREV_37(pi->pubpi->phy_rev) || ACMAJORREV_40(pi->pubpi->phy_rev)) {
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, rxrf_pwrup, 1);
			MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_pwrup, 1);
		} else {
			MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, rxrf_5G_pwrup, 1);
			MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_5G_pwrup, 1);
		}

		MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, logen_rx_pwrup, 1);
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, logen_rx_pwrup, 1);
		if (!ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMAJORREV_33(pi->pubpi->phy_rev) &&
			!ACMAJORREV_37(pi->pubpi->phy_rev))
			MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, fast_nap_bias_pu, 0);
	} else {
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, fast_nap_bias_pu, 1);
	}


	/* Setting the SD-ADC related stuff */
	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		MOD_PHYREGCE(pi, RfctrlOverrideAfeCfg, core, afe_iqdac_pwrup, 1);
		MOD_PHYREGCE(pi, RfctrlCoreAfeCfg1, core, afe_iqdac_pwrup, 1);
	} else {
		if (!ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMAJORREV_33(pi->pubpi->phy_rev) &&
			!ACMAJORREV_37(pi->pubpi->phy_rev)) {
			wlc_phy_txcal_phy_setup_acphy_core_sd_adc(pi, core, sdadc_config);
		}
	}
	/* Turning off all the RF component that are not needed */
	MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna2_pwrup, 0);
	MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, rxrf_lna2_pwrup, 1);
	MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna1_pwrup, 0);
	MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, rxrf_lna1_pwrup, 1);
	MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna2_wrssi2_pwrup, 0);
	MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, rxrf_lna2_wrssi2_pwrup, 1);
	MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, lpf_nrssi_pwrup, 0);
	MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, lpf_nrssi_pwrup, 1);
	MOD_PHYREGCE(pi, RfctrlCoreTxPus, core, lpf_wrssi3_pwrup, 0);
	MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, lpf_wrssi3_pwrup, 1);

	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rssi_wb1g_pu, 0);
		MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, rssi_wb1g_pu, 1);
	} else {
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rssi_wb1a_pu, 0);
		MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, rssi_wb1a_pu, 1);
	}

	/* Turn on PA for iPA chip, turn off for ePA chip */
	if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID) &&
		ACRADIO_2069_EPA_IS(pi->pubpi->radiorev)) {
		MOD_PHYREGCE(pi, RfctrlCoreTxPus, core, pa_pwrup, 0);
	} else {
		MOD_PHYREGCE(pi, RfctrlCoreTxPus, core, pa_pwrup, 1);
	}
	MOD_PHYREGCE(pi, RfctrlOverrideTxPus, core, pa_pwrup, 1);

	if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID) && CHSPEC_IS5G(pi->radio_chanspec) &&
		(ISACPHY(pi) && ACREV_GT(pi->pubpi->phy_rev, 0))) {
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna2_pwrup, 1);
	}
	/* 4335a0/b0 epa : turn on lna2 */
	else if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID) &&
		(RADIO2069_MAJORREV(pi->pubpi->radiorev) == 1) &&
		ACRADIO_2069_EPA_IS(pi->pubpi->radiorev)) {
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna2_pwrup, 1);
	}
	else if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* Turn on lna1 and lna1 for 4365 */
	  //	MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna1_pwrup, 1);
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna2_pwrup, 1);
	}
	else if (TINY_RADIO(pi) && CHSPEC_IS5G(pi->radio_chanspec)) {
		/* Turn on 5G lna's see 'use_lna12' in acphyprocs.tcl and 20691_procs.tcl */
		if (!ACMAJORREV_4(pi->pubpi->phy_rev)) {
			MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna1_pwrup, 1);
		}
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna2_pwrup, 1);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && CHSPEC_IS2G(pi->radio_chanspec)) {
		/* For 4349A0, AUX LNA2 op connects to LNA2 ip.
		   Hence LNA2 needs to be turned on
		 */
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna2_pwrup, 1);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20694_ID)) {
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna1_pwrup, 1);
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna2_pwrup, 1);
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20695_ID)) {
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, rxrf_lna2_pwrup, 1);
	}
	if (!ACMAJORREV_36(pi->pubpi->phy_rev)) {
	MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, tia_DC_loop_PU, 1);
	MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, tia_DC_loop_PU, 1);
	}
	/* bypass LPF config for 4349A0 and 4365 */
	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		MOD_PHYREGCE(pi, RfctrlCoreLpfCT, core, lpf_bq1_bw, bw_idx);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfCT, core, lpf_bq1_bw, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfCT, core, lpf_bq2_bw, bw_idx);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfCT, core, lpf_bq2_bw, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfCT, core, lpf_q_biq2, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfCT, core, lpf_q_biq2, 1);
		MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, lpf_pu_dc, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, lpf_pu_dc, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfCT, core, lpf_dc_bypass, 0);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfCT, core, lpf_dc_bypass, 1);
	} else if (!ACMAJORREV_4(pi->pubpi->phy_rev) && !ACMAJORREV_32(pi->pubpi->phy_rev) &&
		!ACMAJORREV_33(pi->pubpi->phy_rev) && !ACMAJORREV_36(pi->pubpi->phy_rev) &&
		!ACMAJORREV_37(pi->pubpi->phy_rev)) {
		wlc_phy_rxcal_phy_setup_acphy_core_lpf(rxiqcali, core, bw_idx);
	}
}


static void
wlc_phy_rx_fdiqi_freq_config(phy_ac_rxiqcal_info_t *rxiqcali, int8 *fdiqi_cal_freqs,
	uint16 *num_data)
{
	phy_info_t *pi = rxiqcali->pi;
	uint8 fdiqi_en;
	fdiqi_cal_freqs[0] = (int8)(((CHSPEC_IS80(pi->radio_chanspec) ||
		PHY_AS_80P80(pi, pi->radio_chanspec)) ? ACPHY_IQCAL_TONEFREQ_80MHz :
		CHSPEC_IS160(pi->radio_chanspec) ? 0 :
		CHSPEC_IS40(pi->radio_chanspec) ? ACPHY_IQCAL_TONEFREQ_40MHz :
		ACPHY_IQCAL_TONEFREQ_20MHz)/1000);
	fdiqi_cal_freqs[1] = - fdiqi_cal_freqs[0];
	*num_data = 2;

	if (CHSPEC_IS160(pi->radio_chanspec) &&
			!PHY_AS_80P80(pi, pi->radio_chanspec)) {
		ASSERT(0);
	}

	/* rx_fdiqi is enabled in 80MHz channel by default unless it's forced OFF */
	fdiqi_en = (CHSPEC_IS80(pi->radio_chanspec) ||
		CHSPEC_IS160(pi->radio_chanspec) ||
		CHSPEC_IS8080(pi->radio_chanspec) ||
		(CHSPEC_IS40(pi->radio_chanspec) && (ACMAJORREV_2(pi->pubpi->phy_rev))));

	if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID)) {
		if (RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) {
			fdiqi_en = 1;
		}
	}
	if (RADIOID_IS(pi->pubpi->radioid, BCM20695_ID)) {
		fdiqi_en = 1;
	}

	if (IS_4364_1x1(pi) || IS_4364_3x3(pi)) {
		fdiqi_en = 1;
	}

	if (pi->fdiqi_disable) {
		fdiqi_en = FALSE;
	}

	rxiqcali->fdiqi->enabled = fdiqi_en;

#if defined(BCMDBG)
	if (rxiqcali->fdiqi->forced) {
		switch (rxiqcali->fdiqi->forced_val) {
		case 0:
			rxiqcali->fdiqi->enabled = FALSE;
			break;
		case 1:
			rxiqcali->fdiqi->enabled = fdiqi_en;
			break;
		case 2:
			rxiqcali->fdiqi->enabled = TRUE;
			break;
		}
	} else {
		rxiqcali->fdiqi->enabled = fdiqi_en;
	}
#endif /* BCMDBG */

	if (rxiqcali->fdiqi->enabled) {
		if (CHSPEC_IS80(pi->radio_chanspec) ||
			PHY_AS_80P80(pi, pi->radio_chanspec)) {
			fdiqi_cal_freqs[2] = 24;
			fdiqi_cal_freqs[3] = - fdiqi_cal_freqs[2];
			fdiqi_cal_freqs[4] = 32;
			fdiqi_cal_freqs[5] = - fdiqi_cal_freqs[4];
			*num_data = 6;
		} else if (CHSPEC_IS160(pi->radio_chanspec)) {
			fdiqi_cal_freqs[2] = 0;
			*num_data = 6;
			ASSERT(0);
		} else if (CHSPEC_IS40(pi->radio_chanspec)) {
			fdiqi_cal_freqs[2] = 15;
			fdiqi_cal_freqs[3] = - fdiqi_cal_freqs[2];
			*num_data = 4;
		} else {
			fdiqi_cal_freqs[2] = 7;
			fdiqi_cal_freqs[3] = - fdiqi_cal_freqs[2];
			*num_data = 4;
		}
	}

}


static void
wlc_phy_rxcal_leakage_comp_acphy(phy_ac_rxiqcal_info_t *rxiqcali, phy_iq_est_t loopback_rx_iq,
	phy_iq_est_t leakage_rx_iq, int32 *angle, int32 *mag)
{

	acphy_iq_mismatch_t loopback_mismatch, leakage_mismatch;
	int32 loopback_sin_angle;
	int32 leakage_sin_angle;

	int32 den, num, tmp;
	int16 nbits;
	int32 weight = 0;
	math_cint32 val;

	wlc_phy_calc_iq_mismatch_acphy(&loopback_rx_iq, &loopback_mismatch);
	*mag = loopback_mismatch.mag;

	if (rxiqcali->fdiqi->leakage_comp_mode == ACPHY_RXCAL_LEAKAGE_COMP) {
		wlc_phy_calc_iq_mismatch_acphy(&leakage_rx_iq, &leakage_mismatch);

		loopback_sin_angle = loopback_mismatch.sin_angle;
		leakage_sin_angle  = leakage_mismatch.sin_angle;

		den = loopback_rx_iq.i_pwr + loopback_rx_iq.q_pwr;
		num = leakage_rx_iq.i_pwr  + leakage_rx_iq.q_pwr;


		nbits = phy_utils_nbits(num);
		if (nbits % 2 == 1) nbits++;

		num = num << (30 - nbits);
		if (nbits > 10)
			den = den >> (nbits - 10);
		else
			den = den << (10 - nbits);
		num += (den >> 1);

		if (den != 0) {
			weight = (int32) phy_utils_sqrt_int((uint32)(num / den));
		}

		if (weight > 41) { /* 40.96 = 0.04 * 2^10 */
			tmp = (loopback_sin_angle-leakage_sin_angle) * weight;
			tmp = tmp >> 10;

			val.q = loopback_sin_angle + tmp;

			tmp = (val.q >> 1);
			tmp *= tmp;
			tmp = (1 << 30) - tmp;
			val.i = (int32) phy_utils_sqrt_int((uint32) tmp);
			val.i = ( val.i << 1) ;

			phy_utils_invcordic(val, angle);
		} else {
			*angle = loopback_mismatch.angle;
		}
#if defined(BCMDBG_RXCAL)
	printf("   Ang :: %d loopback %d leakage %d weight %d Mag :: %d\n",
		*angle, loopback_mismatch.angle, leakage_mismatch.angle,
		weight, *mag);
#endif /* BCMDBG_RXCAL */

	} else {
		*angle = loopback_mismatch.angle;
#if defined(BCMDBG_RXCAL)
		printf("   Ang :: %d Mag :: %d\n", *angle, *mag);
#endif /* BCMDBG_RXCAL */
	}


}

void
wlc_phy_fdiqi_lin_reg_acphy(phy_info_t *pi, acphy_fdiqi_t *freq_ang_mag,
    uint16 num_data, int fdiq_data_valid, bool TX)
{

	int32 Sf2 = 0;
	int32 Sfa[PHY_CORE_MAX], Sa[PHY_CORE_MAX], Sm[PHY_CORE_MAX];
	int32 intcp[PHY_CORE_MAX], mag[PHY_CORE_MAX];
	int32 refBW;

	int8 idx;
	uint8 core;

	phy_iq_comp_t coeffs[PHY_CORE_MAX];
	int32 sin_angle, cos_angle;
	math_cint32 cordic_out;
	int32  a, b, sign_sa;

	/* initialize array for all cores to prevent compile warning (UNINIT) */
	FOREACH_CORE(pi, core) {
		Sfa[core] = 0; Sa[core] = 0; Sm[core] = 0;
	}

	for (idx = 0; idx < num_data; idx++) {
		Sf2 += freq_ang_mag[idx].freq * freq_ang_mag[idx].freq;
		FOREACH_CORE(pi, core) {
			Sfa[core] += freq_ang_mag[idx].freq * freq_ang_mag[idx].angle[core];
			Sa[core] += freq_ang_mag[idx].angle[core];
			Sm[core] += freq_ang_mag[idx].mag[core];
		}
	}

	/* num_data is initialized to 2 and can be set to 4 or 6 depending on BW
	* in wlc_phy_rx_fdiqi_freq_config function
	*/
	ASSERT(num_data > 0);

	FOREACH_CORE(pi, core) {
		sign_sa = Sa[core] >= 0 ? 1 : -1;
		intcp[core] = (Sa[core] + sign_sa * (num_data >> 1)) / num_data;
		mag[core] = (Sm[core] + (num_data >> 1)) / num_data;

		phy_utils_cordic(intcp[core], &cordic_out);
		sin_angle = cordic_out.q;
		cos_angle = cordic_out.i;

		b = mag[core] * cos_angle;
		a = mag[core] * sin_angle;

		b = ((b >> 15) + 1) >> 1;
		b -= (1 << 10);  /* 10 bit */
		a = ((a >> 15) + 1) >> 1;

		coeffs[core].a = a & 0x3ff;
		coeffs[core].b = b & 0x3ff;

		if (pi->u.pi_acphy->rxiqcali->fdiqi->enabled) {
			/* sanity check: Sf2 = sum of freq^2 > 0 */
			ASSERT(Sf2 > 0);

			refBW = (CHSPEC_IS80(pi->radio_chanspec) ||
				PHY_AS_80P80(pi, pi->radio_chanspec)) ? 30 :
				(CHSPEC_IS160(pi->radio_chanspec)) ? 60 :
				(CHSPEC_IS40(pi->radio_chanspec)) ? 15 : 8;
			pi->u.pi_acphy->rxiqcali->fdiqi->slope[core] =
				(((-Sfa[core] * refBW / Sf2) >> 14) + 1) >> 1;
		}
#if defined(BCMDBG_RXCAL)
		printf("   a=%d b=%d :: ", a, b);
		if (pi->u.pi_acphy->rxiqcali->fdiqi->enabled) {
			printf("   Slope = %d\n", pi->u.pi_acphy->rxiqcali->fdiqi->slope[core]);
		} else {
			printf("   Slope = OFF\n");
		}
#endif /* BCMDBG_RXCAL */
	}

	FOREACH_CORE(pi, core) {
		wlc_phy_rx_iq_comp_acphy(pi, 1, &(coeffs[core]), core);
	}

	if (pi->u.pi_acphy->rxiqcali->fdiqi->enabled) {
		wlc_phy_rx_fdiqi_comp_acphy(pi, TRUE);
	}

}

static void
wlc_phy_rxcal_phy_setup_acphy_core_lpf(phy_ac_rxiqcal_info_t *rxiqcali, uint8 core, uint8 bw_idx)
{
	uint16 addr_lo, val16;
	phy_info_t *pi = rxiqcali->pi;

	if (TINY_RADIO(pi)) {
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_bq2_adc, 0);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_bq2_adc, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_bq1_adc, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_bq1_adc, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_bq2_rc, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_bq2_rc, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_dac_rc, 0);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_dac_rc, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_dac_bq2, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_dac_bq2, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_bq1_bq2, 0);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_bq1_bq2, 1);
	} else {
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_bq2_rc, 0);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_bq2_rc, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_bq1_adc, 0);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_bq1_adc, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_dac_bq2, 0);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_dac_bq2, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_bq2_adc, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_bq2_adc, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_dac_rc, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_dac_rc, 1);
		MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_bq1_bq2, 1);
		MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_bq1_bq2, 1);
	}

	MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_dac_adc, 0);
	MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_dac_adc, 1);
	MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_aux_bq1, 0);
	MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_aux_bq1, 1);
	MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_iqcal_bq1, 0);
	MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_iqcal_bq1, 1);
	MOD_PHYREGCE(pi, RfctrlCoreLpfSwtch, core, lpf_sw_tia_bq1, 1);
	MOD_PHYREGCE(pi, RfctrlOverrideLpfSwtch, core, lpf_sw_tia_bq1, 1);

	addr_lo = (bw_idx < 2) ? 0x140 + 0x10 * core + bw_idx : 0x441 + 0x2 * core;
	wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, addr_lo, 16, &val16);

	MOD_PHYREGCE(pi, RfctrlCoreLpfCT, core, lpf_bq1_bw, val16 & 7);
	MOD_PHYREGCE(pi, RfctrlOverrideLpfCT, core, lpf_bq1_bw, 1);
	MOD_PHYREGCE(pi, RfctrlCoreLpfCT, core, lpf_bq2_bw, (val16 >> 3) & 7);
	MOD_PHYREGCE(pi, RfctrlOverrideLpfCT, core, lpf_bq2_bw, 1);
	MOD_PHYREGCE(pi, RfctrlCoreLpfGmult, core, lpf_g_mult, (val16 >> 6) & 0xff);
	MOD_PHYREGCE(pi, RfctrlOverrideLpfCT, core, lpf_g_mult, 1);

	MOD_PHYREGCE(pi, RfctrlCoreLpfCT, core, lpf_dc_bypass, 0);
	MOD_PHYREGCE(pi, RfctrlOverrideLpfCT, core, lpf_dc_bypass, 1);
	MOD_PHYREGCE(pi, RfctrlCoreLpfCT, core, lpf_q_biq2, 1);
	MOD_PHYREGCE(pi, RfctrlOverrideLpfCT, core, lpf_q_biq2, 1);
	MOD_PHYREGCE(pi, RfctrlCoreRxPus, core, lpf_pu_dc, 1);
	MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core, lpf_pu_dc, 1);
}


/* For a given target power, find txidx */
static void
wlc_phy_cal_txgain_control_dBm(phy_ac_rxiqcal_info_t *rxiqcali, int8 targetpwr_dBm)
{
	uint8 core = 0;
	int16  idle_tssi[PHY_CORE_MAX], tone_tssi[PHY_CORE_MAX];
	uint16 adjusted_tssi[PHY_CORE_MAX];
	int16 a1[PHY_CORE_MAX];
	int16 b0[PHY_CORE_MAX];
	int16 b1[PHY_CORE_MAX];
	int8 curr_txidx;
	int8 currpwr_qdBm, deltapwr_qdBm;
	int8 num_iters = 3, iter;
	txgain_setting_t txgain_settings;
	phy_info_t *pi = rxiqcali->pi;

	/* Initialize txidx */
	curr_txidx = 45;

	/* tssi loopback setup */
	wlc_phy_tssi_phy_setup_acphy(pi, 1);
	if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID))
		wlc_phy_tssi_radio_setup_acphy_tiny(pi, pi->sh->hw_phyrxchain, 1);
	else if (RADIOID_IS(pi->pubpi->radioid, BCM20694_ID)) {
		/* TBD for 20694 radio */
	} else if (RADIOID_IS(pi->pubpi->radioid, BCM20695_ID)) {
		wlc_phy_tssi_radio_setup_acphy_28nm(pi, 1);
	} else
		wlc_phy_tssi_radio_setup_acphy(pi,  pi->sh->hw_phyrxchain, 1);

	/* Get paparams */
	wlc_phy_get_paparams_for_band_acphy(pi, a1, b0, b1);

	FOREACH_CORE(pi, core) {
		/* Initialization */
		curr_txidx = 45;
		for (iter = 0; iter < num_iters; iter++) {
			PHY_CAL(("wlc_phy_cal_txgain_control_dBm: core = %d, iter = %d, idx = %d\n",
			         core, iter, curr_txidx));

			/* Set txidx and get corresponding gain settings */
			wlc_phy_txpwr_by_index_acphy(pi, 1 << core, curr_txidx);
			wlc_phy_get_txgain_settings_by_index_acphy(
				pi, &txgain_settings, curr_txidx);

			/* Meas the idle and tone tssi
			*/
			wlc_phy_txpwrctrl_idle_tssi_meas_acphy(pi);
			idle_tssi[core] =  READ_PHYREGCE(pi, TxPwrCtrlIdleTssi_path, core);
			idle_tssi[core] = idle_tssi[core] - 1023;
			wlc_phy_poll_samps_WAR_acphy(pi, tone_tssi, TRUE, FALSE, &txgain_settings,
			                             FALSE, TRUE, core, 0);
			adjusted_tssi[core] = 1024+idle_tssi[core] - tone_tssi[core];
			adjusted_tssi[core] = adjusted_tssi[core] >> 3;

			/* Get output power, diff with target (in qdBm) */
			currpwr_qdBm = wlc_phy_tssi2dbm_acphy(pi, adjusted_tssi[core],
			  a1[core], b0[core], b1[core]);
			PHY_CAL(("wlc_phy_cal_txgain_control_dBm: currpwr (qdBm) %d\n",
			         currpwr_qdBm));
			deltapwr_qdBm = (currpwr_qdBm - (targetpwr_dBm << 2));

			/* Update txidx */
			curr_txidx = (deltapwr_qdBm >> 1) + curr_txidx;
			if (curr_txidx < 0)
				curr_txidx = 0;

			PHY_CAL(("wlc_phy_cal_txgain_control_dBm: deltapwr (qdBm) = %d - %d = %d\n",
			         currpwr_qdBm, targetpwr_dBm << 2, deltapwr_qdBm));
			PHY_CAL(("wlc_phy_cal_txgain_control_dBm: new_idx = %d\n", curr_txidx));
		}
		rxiqcali->txpwridx_for_rxiqcal[core] = curr_txidx;
	}
}

static void
wlc_phy_calc_iq_mismatch_acphy(phy_iq_est_t *est, acphy_iq_mismatch_t *mismatch)
{

	/* angle = asin (-iq / sqrt( ii*qq ))
	* mag	= sqrt ( qq/ii )
	*/

	int32  iq = est->iq_prod;
	uint32 ii = est->i_pwr;
	uint32 qq = est->q_pwr;

	int16  iq_nbits, qq_nbits, ii_nbits;
	int32  tmp;
	int32  den, num;
	int32  angle;
	math_cint32 val;

	iq_nbits = phy_utils_nbits(iq);
	qq_nbits = phy_utils_nbits(qq);
	ii_nbits = phy_utils_nbits(ii);
	if (ii_nbits > qq_nbits)
		qq_nbits = ii_nbits;

	if (30 >=  qq_nbits) {
		tmp = ii;
		tmp = tmp << (30 - qq_nbits);
		den = (int32) phy_utils_sqrt_int((uint32) tmp);
		tmp = qq;
		tmp = tmp << (30 - qq_nbits);
		den *= (int32) phy_utils_sqrt_int((uint32) tmp);
	} else {
		tmp = ii;
		tmp = tmp >> (qq_nbits - 30);
		den = (int32) phy_utils_sqrt_int((uint32) tmp);
		tmp = qq;
		tmp = tmp >> (qq_nbits - 30);
		den *= (int32) phy_utils_sqrt_int((uint32) tmp);
	}
	if (qq_nbits <= iq_nbits + 16) {
		den = den >> (16 + iq_nbits - qq_nbits);
	} else {
		den = den << (qq_nbits - (16 + iq_nbits));
	}

	tmp = -iq;
	num = (tmp << (30 - iq_nbits));
	if (num > 0)
		num += (den >> 1);
	else
		num -= (den >> 1);

	if (den == 0) {
		tmp = 0;
	} else {
		tmp = num / den; /* in X,16 */
	}

	mismatch->sin_angle = tmp;

	tmp = (tmp >> 1);
	tmp *= tmp;
	tmp = (1 << 30) - tmp;
	val.i = (int32) phy_utils_sqrt_int((uint32) tmp);
	val.i = ( val.i << 1) ;

	val.q = mismatch->sin_angle;
	phy_utils_invcordic(val, &angle);
	mismatch->angle = angle; /* in X,16 */


	iq_nbits = phy_utils_nbits(qq - ii);
	if (iq_nbits % 2 == 1)
		iq_nbits++;

	den = ii;

	num = qq - ii;
	num = num << (30 - iq_nbits);
	if (iq_nbits > 10)
		den = den >> (iq_nbits - 10);
	else
		den = den << (10 - iq_nbits);
	if (num > 0)
		num += (den >> 1);
	else
		num -= (den >> 1);

	if (den == 0) {
		mismatch->mag = (1 << 10); /* in X,10 */
	} else {
		tmp = num / den + (1 << 20);
		mismatch->mag = (int32) phy_utils_sqrt_int((uint32) tmp); /* in X,10 */
	}

#if defined(BCMDBG_RXCAL)
	printf("	  Mag=%d/2^10, Angle=%d/2^16, cos(angle)=%d/2^16, sin(angle)=%d/2^16\n",
	(int)mismatch->mag, (int)mismatch->angle, (int)val.i, (int)val.q);
#endif /* BCMDBG_RXCAL */

}

#if defined(BCMDBG_RXCAL)
static void
wlc_phy_rxcal_snr_acphy(phy_ac_rxiqcal_info_t *rxiqcali, uint16 num_samps, uint8 core_mask)
{
	phy_info_t *pi = rxiqcali->pi;
	uint16 bbmult_orig[PHY_CORE_MAX], bbmult_zero = 0;
	phy_iq_est_t  noise_vals[PHY_CORE_MAX];
	uint8 core;

	/* take noise measurement (for SNR calc, for information purposes only) */
	FOREACH_ACTV_CORE(pi, core_mask, core) {
		wlc_phy_get_tx_bbmult_acphy(pi, &(bbmult_orig[core]), core);
		wlc_phy_set_tx_bbmult_acphy(pi, &bbmult_zero, core);
	}

	wlc_phy_rx_iq_est_acphy(pi, noise_vals, num_samps, 32, 0, FALSE);

	FOREACH_ACTV_CORE(pi, core_mask, core) {
		/* Store the noise powers for SNR calculations later */
		rxiqcali->rxcal_noise[core] = noise_vals[core];
	}

	FOREACH_ACTV_CORE(pi, core_mask, core) {
		wlc_phy_set_tx_bbmult_acphy(pi, &(bbmult_orig[core]), core);
	}
}
#endif /* BCMDBG_RXCAL */

static void
BCMATTACHFN(phy_ac_rxiqlocal_std_params)(phy_ac_rxiqcal_info_t *ac_info)
{
#if defined(BCMDBG)
	ac_info->fdiqi->forced = FALSE;
	ac_info->fdiqi->forced_val = 0;
#endif
	/* RX-IQ-CAL per core */
	ac_info->rxiqcal_percore_2g = FALSE;
	ac_info->rxiqcal_percore_5g = TRUE;
	ac_info->rxcal_cache_cookie = 0;
}
/* ********************************************* */
/*				External Definitions					*/
/* ********************************************* */
void
wlc_phy_rx_iq_comp_acphy(phy_info_t *pi, uint8 write, phy_iq_comp_t *pcomp, uint8 rx_core)
{
	/* write: 0 - fetch values from phyregs into *pcomp
	 *		  1 - deposit values from *pcomp into phyregs
	 *		  2 - set all coeff phyregs to 0
	 *
	 * rx_core: specify which core to fetch/deposit
	 */

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	ASSERT(write <= 2);

	/* write values */
	if (write == 0) {
		pcomp->a = READ_PHYREGCE(pi, Core1RxIQCompA, rx_core);
		pcomp->b = READ_PHYREGCE(pi, Core1RxIQCompB, rx_core);
	} else if (write == 1) {
		WRITE_PHYREGCE(pi, Core1RxIQCompA, rx_core, pcomp->a);
		WRITE_PHYREGCE(pi, Core1RxIQCompB, rx_core, pcomp->b);
	} else {
		WRITE_PHYREGCE(pi, Core1RxIQCompA, rx_core, 0);
		WRITE_PHYREGCE(pi, Core1RxIQCompB, rx_core, 0);
	}
}

void
wlc_phy_rx_fdiqi_comp_acphy(phy_info_t *pi, bool enable)
{
	uint8 core;
	int16 sign_slope;
	int8 idx;
	int32 slope;
#if defined(BCMDBG_RXCAL)
	int16 regval;
#endif /* BCMDBG_RXCAL */

	int16 filtercoeff[11][11] = {{0, 0, 0, 0, 0, 1024, 0, 0, 0, 0, 0},
		{-12, 15, -20, 30, -60, 1024, 61, -30, 20, -15, 12},
		{-24, 30, -40, 60, -120, 1024, 122, -61, 41, -30, 24},
		{-36, 45, -60, 91, -180, 1024, 184, -92, 61, -46, 37},
		{-42, 52, -69, 103, -206, 1024, 211, -105, 70, -52, 42},
		{-52, 65, -86, 129, -256, 1024, 264, -131, 87, -65, 52},
		{-62, 78, -103, 155, -307, 1023, 319, -158, 105, -78, 63},
		{-73, 91, -121, 180, -357, 1023, 373, -184, 122, -92, 73},
		{-83, 104, -138, 206, -407, 1023, 428, -211, 140, -105, 84},
		{-93, 117, -155, 231, -456, 1023, 483, -238, 158, -118, 94},
		{-104, 129, -172, 257, -506, 1022, 539, -265, 176, -132, 105}};

	/* enable: 0 - disable FDIQI comp
	 *         1 - program FDIQI comp filter and enable
	 */

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

#ifdef WL_PROXDETECT
	if (pi->u.pi_acphy->tof_active) {
		return;
	}
#endif

	/* write values */
	if (enable == FALSE) {
		MOD_PHYREG(pi, rxfdiqImbCompCtrl, rxfdiqImbCompEnable, 0);
		FOREACH_CORE(pi, core) {
			pi->u.pi_acphy->rxiqcali->fdiqi->slope[core] = 0;
		}
#if defined(BCMDBG_RXCAL)
		printf("   FDIQI Disabled\n");
#endif /* BCMDBG_RXCAL */
		return;
	} else {

#define ACPHY_RXFDIQCOMP_STR(pi, core, tap)	((ACPHY_rxfdiqcomp_str0_c0(pi->pubpi->phy_rev) + \
	(0x200 * (core)) + (tap)))

		FOREACH_CORE(pi, core) {
			slope = pi->u.pi_acphy->rxiqcali->fdiqi->slope[core];
			sign_slope = slope >= 0 ? 1 : -1;
			slope *= sign_slope;
			if (slope > 10) slope = 10;

			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, rxfdiqImbN_offcenter_scale, N_offcenter_scale,
					4)
				MOD_PHYREG_ENTRY(pi, rxfdiqImbCompCtrl, rxfdiq_iorq, 0)
				MOD_PHYREG_ENTRY(pi, rxfdiqImbCompCtrl, calibration_notoperation, 0)
				MOD_PHYREG_ENTRY(pi, fdiqi_rx_comp_Nshift_out, Nshift_out, 10)
			ACPHY_REG_LIST_EXECUTE(pi);

			for (idx = 0; idx < 11; idx++) {
				if (sign_slope == -1) {
					phy_utils_write_phyreg(pi,
					                       ACPHY_RXFDIQCOMP_STR(pi, core, idx),
						filtercoeff[slope][10-idx]);
				} else {
					phy_utils_write_phyreg(pi,
					                       ACPHY_RXFDIQCOMP_STR(pi, core, idx),
						filtercoeff[slope][idx]);
				}
			}

#if defined(BCMDBG_RXCAL)
			printf("   Core=%d, Slope= %d :: ", core, sign_slope*slope);
			for (idx = 0; idx < 11; idx++) {
				regval = _PHY_REG_READ(pi, ACPHY_RXFDIQCOMP_STR(pi, core, idx));
				if (regval > 1024) regval -= 2048;
				printf(" %d", regval);
			}
			printf("\n");
#endif /* BCMDBG_RXCAL */

		}
		MOD_PHYREG(pi, rxfdiqImbCompCtrl, rxfdiqImbCompEnable, 1);
	}

}

int
wlc_phy_cal_rx_fdiqi_acphy(phy_info_t *pi)
{
	acphy_fdiqi_t freq_ang_mag[ACPHY_RXCAL_MAX_NUM_FREQ];
	int8 fdiqi_cal_freqs[ACPHY_RXCAL_MAX_NUM_FREQ];
	uint16 num_data, bbmult = 32;
	uint8 core;
	phy_iq_est_t loopback_rx_iq[PHY_CORE_MAX];
	phy_iq_est_t leakage_rx_iq[PHY_CORE_MAX];
	int32 angle;
	int32 mag;
	uint8 freq_idx;
	int16 tone_freq;
	uint16 *coeff_ptr;
	uint16 coeff_vals_temp[8] = {0}; /* accounting for max 4 core case */
	uint16 start_coeffs_RESTART[] = {0, 0, 0, 0, 0, 0, 0, 0};
	int8 k;
	acphy_cal_result_t *accal = &pi->cal_info->u.accal;
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;
	phy_ac_rxiqcal_info_t *ti = pi_ac->rxiqcali;
	bool suspend = TRUE;

	if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
		/* Let WLAN have FEMCTRL to ensure cal is done properly */
		suspend = !(R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC);
		if (!suspend) {
			wlapi_suspend_mac_and_wait(pi->sh->physhim);
		}
		wlc_btcx_override_enable(pi);
	}

#ifdef ATE_BUILD
	printf("===> Running rx fdiqi\n");
#endif /* ATE_BUILD */

	/* Enable leakage compensation by default */
	/* Disable leakage compensation for selected revisions only */
	/* LNA1 bypass mode */
	ti->fdiqi->leakage_comp_mode = ACPHY_RXCAL_LEAKAGE_COMP;

	if (ACMAJORREV_1(pi->pubpi->phy_rev) || ACMAJORREV_2(pi->pubpi->phy_rev) ||
	    ACMAJORREV_3(pi->pubpi->phy_rev) || ACMAJORREV_4(pi->pubpi->phy_rev) ||
	    ACMAJORREV_5(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_36(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev) || ACMAJORREV_40(pi->pubpi->phy_rev)) {
		ti->fdiqi->leakage_comp_mode = ACPHY_RXCAL_NORMAL;
	}
	if (ACREV_IS(pi->pubpi->phy_rev, 1)) {
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			ti->fdiqi->leakage_comp_mode = ACPHY_RXCAL_NORMAL;
		}
	}
	if (!ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMAJORREV_33(pi->pubpi->phy_rev) &&
		!ACMAJORREV_37(pi->pubpi->phy_rev)) {
		phy_ac_dssf(pi_ac->rxspuri, FALSE);
	}

	if ((!ACMINORREV_0(pi) && !ACMINORREV_2(pi)) &&
		CHSPEC_IS2G(pi->radio_chanspec) && (BF2_2G_SPUR_WAR(pi_ac) == 1)) {
		phy_ac_dssfB(pi_ac->rxspuri, FALSE);
	}

	if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_36(pi->pubpi->phy_rev)) {
		phy_ac_spurcan(pi_ac->rxspuri, FALSE);
	}

	wlc_phy_stay_in_carriersearch_acphy(pi, TRUE);
	wlc_phy_force_rfseq_acphy(pi, ACPHY_RFSEQ_RESET2RX);

	/* Zero Out coefficients */
	FOREACH_CORE(pi, core) {
		wlc_phy_rx_iq_comp_acphy(pi, 2, NULL, core);
	}
	wlc_phy_rx_fdiqi_comp_acphy(pi, FALSE);
#ifdef WLC_TXFDIQ
	if (phy_ac_txiqlocal_fdiqi_is_enabled(pi_ac->txiqlocali)) {
		wlc_phy_tx_fdiqi_comp_acphy(pi, FALSE, 0xFF);
	}
#endif
	/* Save original TX comp coeffs
	 *  Load the special TX IQ COMP coefficients
	 */
	coeff_ptr = start_coeffs_RESTART;
	if (!TINY_RADIO(pi) && (!ACMAJORREV_36(pi->pubpi->phy_rev)) &&
		(!ACMAJORREV_40(pi->pubpi->phy_rev))) {
		FOREACH_CORE(pi, core) {
			wlc_phy_cal_txiqlo_coeffs_acphy(pi, CAL_COEFF_READ,
				coeff_ptr + 2*core, TB_OFDM_COEFFS_AB, core);
			for (k = 0; k < 2; k++) {
				coeff_vals_temp[k] = accal->txiqlocal_biq2byp_coeffs[2*core + k];
			}
			wlc_phy_cal_txiqlo_coeffs_acphy(pi, CAL_COEFF_WRITE,
				coeff_vals_temp, TB_OFDM_COEFFS_AB, core);
		}
	}

	if (RADIOID_IS(pi->pubpi->radioid, BCM2069_ID) &&
	    (RADIO2069_MAJORREV(pi->pubpi->radiorev) == 2) &&
	    (ACRADIO_2069_EPA_IS(pi->pubpi->radiorev)) &&
	    (CHSPEC_IS5G(pi->radio_chanspec))) {
		/* Find tx_idx for target pwr 10dBm. This txidx will be used for rx iq cal. */
		wlc_phy_cal_txgain_control_dBm(ti, 10);
	}
	wlc_phy_rxcal_phy_setup_acphy(ti);

	if (TINY_RADIO(pi)) {
		wlc_phy_rxcal_radio_setup_acphy_tiny(ti);

		FOREACH_CORE(pi, core) {
			wlc_phy_set_tx_bbmult_acphy(pi, &bbmult, core);
		}


		/* turn on testtone */
		wlc_phy_tx_tone_acphy(pi, (((CHSPEC_IS80(pi->radio_chanspec) ||
		                              PHY_AS_80P80(pi, pi->radio_chanspec))
			                          ? ACPHY_IQCAL_TONEFREQ_80MHz
		                              : (CHSPEC_IS160(pi->radio_chanspec))
		                              ? 0
			                          : (CHSPEC_IS40(pi->radio_chanspec))
			                          ? ACPHY_IQCAL_TONEFREQ_40MHz
			                          : ACPHY_IQCAL_TONEFREQ_20MHz) >> 1),
			                          ACPHY_RXCAL_TONEAMP, 0, 0, FALSE);

		if (ACREV_GE(pi->pubpi->phy_rev, 11)) {
			wlc_dcc_fsm_reset(pi);
		} else {
			MOD_PHYREG(pi, dcc_ctrl_restart_length_grp,
				dcc_ctrl_restart_length, 0x1);
			MOD_PHYREG(pi, rx_tia_dc_loop_0, restart_gear, 0x1);
			OSL_DELAY(10);
			MOD_PHYREG(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl,  0x0);
		}
		wlc_phy_stopplayback_acphy(pi);
		wlc_phy_rxcal_txrx_gainctrl_acphy_tiny(ti);
	} else {
		if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			wlc_phy_rxcal_radio_setup_acphy_28nm(ti);
			wlc_phy_rxcal_txrx_gainctrl_acphy_28nm(ti);
		} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
			wlc_phy_rxcal_radio_setup_acphy_20696(ti);
			wlc_phy_rxcal_txrx_gainctrl_acphy_28nm(ti);
		} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
			wlc_phy_rxcal_radio_setup_acphy_20694(ti);
			wlc_phy_rxcal_txrx_gainctrl_acphy_28nm(ti);
		} else {
			wlc_phy_rxcal_radio_setup_acphy(ti);
			wlc_phy_rxcal_loopback_gainctrl_acphy(ti);
		}
	}
	wlc_phy_rx_fdiqi_freq_config(ti, fdiqi_cal_freqs, &num_data);

	for (freq_idx = 0; freq_idx < num_data; freq_idx++) {

		tone_freq = (int16)fdiqi_cal_freqs[freq_idx] * 1000;
		if (!(ACMAJORREV_36(pi->pubpi->phy_rev) || IS_4364_1x1(pi) || IS_4364_3x3(pi))) {
			tone_freq = tone_freq >> 1; }
		freq_ang_mag[freq_idx].freq = (int32)fdiqi_cal_freqs[freq_idx];
		wlc_phy_tx_tone_acphy(pi, (int32)tone_freq, ACPHY_RXCAL_TONEAMP, 0, 0, FALSE);

		/* get iq, ii, qq measurements from iq_est */
		if (ti->fdiqi->enabled) {
			wlc_phy_rx_iq_est_acphy(pi, loopback_rx_iq, 0x3000, 32, 0, TRUE);
		} else {
			wlc_phy_rx_iq_est_acphy(pi, loopback_rx_iq, 0x4000, 32, 0, TRUE);
		}

		if (ti->fdiqi->leakage_comp_mode == ACPHY_RXCAL_LEAKAGE_COMP) {
			if (TINY_RADIO(pi)) {
				wlc_phy_rxcal_radio_cleanup_acphy_tiny(ti);
				wlc_phy_rx_iq_est_acphy(pi, leakage_rx_iq, 0x4000, 32, 0, TRUE);
				wlc_phy_rxcal_radio_setup_acphy_tiny(ti);
			} else {
				wlc_phy_rxcal_radio_cleanup_acphy(ti);
				wlc_phy_rx_iq_est_acphy(pi, leakage_rx_iq, 0x4000, 32, 0, TRUE);
				wlc_phy_rxcal_radio_setup_acphy(ti);
			}
		}
		wlc_phy_stopplayback_acphy(pi);

#if defined(BCMDBG_RXCAL)
		printf("  tone freq  %d\n", freq_ang_mag[freq_idx].freq);
		FOREACH_CORE(pi, core) {
			printf("[core = %d] iq:%d \ti2:%d \tq2: %d\n",
				core, loopback_rx_iq[core].iq_prod,
				loopback_rx_iq[core].i_pwr, loopback_rx_iq[core].q_pwr);
		}
		if (ti->fdiqi->leakage_comp_mode == ACPHY_RXCAL_LEAKAGE_COMP) {
			FOREACH_CORE(pi, core) {
				printf(" %d %d %d ", leakage_rx_iq[core].iq_prod,
					leakage_rx_iq[core].i_pwr, leakage_rx_iq[core].q_pwr);
			}
		}
		printf("\n");
#endif /* BCMDBG_RXCAL */

		FOREACH_CORE(pi, core) {
			wlc_phy_rxcal_leakage_comp_acphy(ti, loopback_rx_iq[core],
				leakage_rx_iq[core], &angle, &mag);
			freq_ang_mag[ freq_idx ].angle[core] = angle;
			freq_ang_mag[ freq_idx ].mag[core] = mag;
		}
	}

	wlc_phy_fdiqi_lin_reg_acphy(pi, freq_ang_mag, num_data, 0xFF, 0);

	if (TINY_RADIO(pi)) {
		wlc_phy_rxcal_radio_cleanup_acphy_tiny(ti);
	} else if (IS_28NM_RADIO(pi)) {
		wlc_phy_rxcal_radio_cleanup_acphy_28nm(ti);
	} else {
		wlc_phy_rxcal_radio_cleanup_acphy(ti);
	}
	wlc_phy_rxcal_phy_cleanup_acphy(ti);
	/* restore the coeffs after RX-cal */
	if (!TINY_RADIO(pi) && (!ACMAJORREV_36(pi->pubpi->phy_rev)) &&
		(!ACMAJORREV_40(pi->pubpi->phy_rev))) {
		FOREACH_CORE(pi, core) {
			wlc_phy_cal_txiqlo_coeffs_acphy(pi, CAL_COEFF_WRITE,
				coeff_ptr + 2*core, TB_OFDM_COEFFS_AB, core);
		}
	}

#if defined(BCMDBG_RXCAL)
	FOREACH_CORE(pi, core) {
		/* Measure the SNR in the Rx IQ cal feedback path */
		wlc_phy_rxcal_snr_acphy(ti, 0x4000, (1 << core));

		printf("wlc_phy_cal_rx_iq_acphy: core%d => "
			"(S =%9d,  N =%9d,  K =%d)\n",
			core,
			ti->rxcal_signal[core].i_pwr + ti->rxcal_signal[core].q_pwr,
			ti->rxcal_noise[core].i_pwr + ti->rxcal_noise[core].q_pwr,
			0x4000);
	}
#endif /* BCMDBG_RXCAL */

	if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
		wlc_phy_btcx_override_disable(pi);
		if (!suspend) {
			wlapi_enable_mac(pi->sh->physhim);
		}
	}

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMAJORREV_33(pi->pubpi->phy_rev) &&
		!ACMAJORREV_37(pi->pubpi->phy_rev))
		wlc_phy_stay_in_carriersearch_acphy(pi, FALSE);

	 /* For 4345 need to calibrate dc offset after cleanup */
	if (TINY_RADIO(pi)) {
		if (ACREV_GE(pi->pubpi->phy_rev, 11)) {
			wlc_dcc_fsm_reset(pi);
		} else {
			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, dcc_ctrl_restart_length_grp,
					dcc_ctrl_restart_length, 0xffff)
				MOD_PHYREG_ENTRY(pi, rx_tia_dc_loop_0, restart_gear, 0x6)
				MOD_PHYREG_ENTRY(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl,  0x1)
			ACPHY_REG_LIST_EXECUTE(pi);
			OSL_DELAY(10);
		}
	}

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) && !ACMAJORREV_33(pi->pubpi->phy_rev) &&
		!ACMAJORREV_37(pi->pubpi->phy_rev)) {
		phy_ac_dssf(pi_ac->rxspuri, TRUE);
	}

	if ((!ACMINORREV_0(pi) && !ACMINORREV_2(pi)) &&
		CHSPEC_IS2G(pi->radio_chanspec) && (BF2_2G_SPUR_WAR(pi_ac) == 1)) {
		phy_ac_dssfB(pi_ac->rxspuri, TRUE);
	}

	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		bool elna_present = (CHSPEC_IS2G(pi->radio_chanspec)) ? BF_ELNA_2G(pi_ac)
	                                                      : BF_ELNA_5G(pi_ac);

		phy_ac_spurcan(pi_ac->rxspuri, !elna_present);
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		phy_ac_spurcan(pi_ac->rxspuri, TRUE);
	}

	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		ACMAJORREV_37(pi->pubpi->phy_rev)) {
		wlc_phy_resetcca_acphy(pi);
		wlc_phy_stay_in_carriersearch_acphy(pi, FALSE);
	}

#ifdef ATE_BUILD
	printf("===> Finished rx fdiqi\n");
#endif /* ATE_BUILD */

	return BCME_OK;
}

static void
wlc_phy_rx_iq_est_acphy_percore(phy_info_t *pi, phy_iq_est_t *est)
{
	/* structure for saving txpu_ovrd, txpu_val, rxpu_ovrd and rxpu_val
	 * 4 register values for each core
	 */
	struct _save_regs {
		uint16 reg_val;
		uint16 reg_addr;
	} savereg[PHY_CORE_MAX*4];

	uint8 core;
	uint core_count = 0;
	uint8 stall_val;

	FOREACH_CORE(pi, core) {
		uint8 core_off;

		if (PHYCORENUM((pi)->pubpi->phy_corenum) > 1) {
			/* SAVE and turn off all cores except current */
			FOREACH_CORE(pi, core_off) {
				if (core != core_off) {
					savereg[core_count].reg_val =
						READ_PHYREGCE(pi, RfctrlOverrideTxPus, core_off);
					savereg[core_count].reg_addr =
						ACPHYREGCE(pi, RfctrlOverrideTxPus, core_off);
					++core_count;

					savereg[core_count].reg_val =
						READ_PHYREGCE(pi, RfctrlCoreTxPus, core_off);
					savereg[core_count].reg_addr =
						ACPHYREGCE(pi, RfctrlCoreTxPus, core_off);
					++core_count;

					savereg[core_count].reg_val =
						READ_PHYREGCE(pi, RfctrlOverrideRxPus, core_off);
					savereg[core_count].reg_addr =
						ACPHYREGCE(pi, RfctrlOverrideRxPus, core_off);
					++core_count;

					savereg[core_count].reg_val =
						READ_PHYREGCE(pi, RfctrlCoreRxPus, core_off);
					savereg[core_count].reg_addr =
						ACPHYREGCE(pi, RfctrlCoreRxPus, core_off);
					++core_count;

					MOD_PHYREGCE(pi, RfctrlOverrideTxPus, core_off,
					             txrf_pwrup, 1);
					MOD_PHYREGCE(pi, RfctrlCoreTxPus, core_off, txrf_pwrup, 0);
					MOD_PHYREGCE(pi, RfctrlOverrideRxPus, core_off,
					             logen_rx_pwrup, 1);
					MOD_PHYREGCE(pi, RfctrlCoreRxPus, core_off,
					             logen_rx_pwrup, 0);
				}
			}

			OSL_DELAY(1);
		}

		stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
		ACPHY_DISABLE_STALL(pi);
		MOD_PHYREG(pi, IqestCmd, iqstart, 1);
		ACPHY_ENABLE_STALL(pi, stall_val);

		/* wait for estimate */
		SPINWAIT((READ_PHYREGFLD(pi, IqestCmd, iqstart) != 0), ACPHY_SPINWAIT_IQEST);

		if ((READ_PHYREGFLD(pi, IqestCmd, iqstart) != 0) &&
			(!ACMAJORREV_32(pi->pubpi->phy_rev)) &&
			(!ACMAJORREV_33(pi->pubpi->phy_rev)) &&
			(!ACMAJORREV_37(pi->pubpi->phy_rev))) {
			PHY_FATAL_ERROR_MESG((" %s: SPINWAIT ERROR : IQ measurement timed out \n",
				__FUNCTION__));
			PHY_FATAL_ERROR(pi, PHY_RC_IQEST_FAILED);
		}

		/* FIX later: bypass the assert for 4366C0 */
		if (!(ACMAJORREV_2(pi->pubpi->phy_rev)) && !(ACMAJORREV_32(pi->pubpi->phy_rev)) &&
			!(ACMAJORREV_33(pi->pubpi->phy_rev)) &&
			!(ACMAJORREV_37(pi->pubpi->phy_rev)))
			ASSERT(READ_PHYREGFLD(pi, IqestCmd, iqstart) == 0);

		/* Restore */
		if (PHYCORENUM((pi)->pubpi->phy_corenum) > 1) {
			while (core_count > 0) {
				--core_count;
				phy_utils_write_phyreg(pi, savereg[core_count].reg_addr,
					savereg[core_count].reg_val);
			}
		}

		if (READ_PHYREGFLD(pi, IqestCmd, iqstart) == 0) {
			ASSERT(PHYCORENUM(pi->pubpi->phy_corenum) <= PHY_CORE_MAX);
			est[core].i_pwr = (READ_PHYREGCE(pi, IqestipwrAccHi, core) << 16) |
			                  READ_PHYREGCE(pi, IqestipwrAccLo, core);
			est[core].q_pwr = (READ_PHYREGCE(pi, IqestqpwrAccHi, core) << 16) |
			                  READ_PHYREGCE(pi, IqestqpwrAccLo, core);
			est[core].iq_prod = (READ_PHYREGCE(pi, IqestIqAccHi, core) << 16) |
			                    READ_PHYREGCE(pi, IqestIqAccLo, core);
			PHY_NONE(("wlc_phy_rx_iq_est_acphy: core%d "
			          "i_pwr = %u, q_pwr = %u, iq_prod = %d\n",
			          core, est[core].i_pwr, est[core].q_pwr,
			          est[core].iq_prod));
		} else {
			PHY_ERROR(("wl%d: %s: IQ measurement timed out\n",
			          pi->sh->unit, __FUNCTION__));
		}
	}
}

/* see also: proc acphy_rx_iq_est { {num_samps 2000} {wait_time ""} } */
void
wlc_phy_rx_iq_est_acphy(phy_info_t *pi, phy_iq_est_t *est, uint16 num_samps,
                        uint8 wait_time, uint8 wait_for_crs, bool rxiq_cal)
{
	uint8 core;
	bool percore = rxiq_cal & (CHSPEC_IS2G(pi->radio_chanspec) ?
		pi->u.pi_acphy->rxiqcali->rxiqcal_percore_2g :
		pi->u.pi_acphy->rxiqcali->rxiqcal_percore_5g);
	uint8 stall_val;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	/* Skip this function for QT but provide dummy initialisation */
	if (ISSIM_ENAB(pi->sh->sih)) {
		est[0].i_pwr = 0;
		est[0].q_pwr = 0;
		return;
	}

	wlc_phy_pulse_adc_reset_acphy(pi);

	/* Get Rx IQ Imbalance Estimate from modem */
	WRITE_PHYREG(pi, IqestSampleCount, num_samps);
	MOD_PHYREG(pi, IqestWaitTime, waitTime, wait_time);
	MOD_PHYREG(pi, IqestCmd, iqMode, wait_for_crs);

	if (!percore) {
		stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
		ACPHY_DISABLE_STALL(pi);
		MOD_PHYREG(pi, IqestCmd, iqstart, 1);
		ACPHY_ENABLE_STALL(pi, stall_val);

		/* wait for estimate */
		SPINWAIT((READ_PHYREGFLD(pi, IqestCmd, iqstart) != 0),
			ACPHY_SPINWAIT_IQEST);
		if ((READ_PHYREGFLD(pi, IqestCmd, iqstart) != 0) &&
			(!ACMAJORREV_32(pi->pubpi->phy_rev)) &&
			(!ACMAJORREV_33(pi->pubpi->phy_rev)) &&
			(!ACMAJORREV_37(pi->pubpi->phy_rev))) {
			PHY_FATAL_ERROR_MESG((" %s: SPINWAIT ERROR : IQ measurement timed out \n",
				__FUNCTION__));
			PHY_FATAL_ERROR(pi, PHY_RC_IQEST_FAILED);
		}

		if (READ_PHYREGFLD(pi, IqestCmd, iqstart) == 0) {
			ASSERT(PHYCORENUM(pi->pubpi->phy_corenum) <= PHY_CORE_MAX);
			FOREACH_CORE(pi, core) {
				est[core].i_pwr = (READ_PHYREGCE(pi, IqestipwrAccHi, core) << 16) |
				                  READ_PHYREGCE(pi, IqestipwrAccLo, core);
				est[core].q_pwr = (READ_PHYREGCE(pi, IqestqpwrAccHi, core) << 16) |
				                  READ_PHYREGCE(pi, IqestqpwrAccLo, core);
				est[core].iq_prod = (READ_PHYREGCE(pi, IqestIqAccHi, core) << 16) |
				                    READ_PHYREGCE(pi, IqestIqAccLo, core);
				PHY_NONE(("wlc_phy_rx_iq_est_acphy: core%d "
				          "i_pwr = %u, q_pwr = %u, iq_prod = %d\n",
				          core, est[core].i_pwr, est[core].q_pwr,
				          est[core].iq_prod));
			}
		} else {
			PHY_ERROR(("wl%d: %s: IQ measurement timed out\n",
			          pi->sh->unit, __FUNCTION__));
			/* FIX later: bypass the assert for 4366C0 */
			if (!(ACMAJORREV_2(pi->pubpi->phy_rev) && ACMINORREV_1(pi)) &&
			(!ACMAJORREV_32(pi->pubpi->phy_rev)) &&
			(!ACMAJORREV_33(pi->pubpi->phy_rev)) &&
			(!ACMAJORREV_37(pi->pubpi->phy_rev)))
				ASSERT(READ_PHYREGFLD(pi, IqestCmd, iqstart) == 0);
		}
	} else {
		wlc_phy_rx_iq_est_acphy_percore(pi, est);
	}
}

#if defined(BCMDBG)
void
wlc_phy_force_fdiqi_acphy(phy_info_t *pi, uint16 int_val)
{

	pi->u.pi_acphy->rxiqcali->fdiqi->forced = TRUE;
	pi->u.pi_acphy->rxiqcali->fdiqi->forced_val = int_val;
	wlc_phy_cals_acphy(pi->u.pi_acphy->calmgri, PHY_PERICAL_UNDEF,
			PHY_CAL_SEARCHMODE_RESTART);
}
#endif

/* Override/Restore routine for Rx Digital LPF:
 * 1) Override: Save digital LPF config and set new LPF configuration
 * 2) Restore: Restore digital LPF config
 */
void
wlc_phy_dig_lpf_override_acphy(phy_info_t *pi, uint8 dig_lpf_ht)
{
	phy_rxiqcal_data_t *data = pi->rxiqcali->data;
	if ((dig_lpf_ht > 0) && !data->phy_rx_diglpf_default_coeffs_valid) {
		data->phy_rx_diglpf_default_coeffs[0] = READ_PHYREG(pi, RxStrnFilt40Num00);
		data->phy_rx_diglpf_default_coeffs[1] = READ_PHYREG(pi, RxStrnFilt40Num01);
		data->phy_rx_diglpf_default_coeffs[2] = READ_PHYREG(pi, RxStrnFilt40Num02);
		data->phy_rx_diglpf_default_coeffs[3] = READ_PHYREG(pi, RxStrnFilt40Den00);
		data->phy_rx_diglpf_default_coeffs[4] = READ_PHYREG(pi, RxStrnFilt40Den01);
		data->phy_rx_diglpf_default_coeffs[5] = READ_PHYREG(pi, RxStrnFilt40Num10);
		data->phy_rx_diglpf_default_coeffs[6] = READ_PHYREG(pi, RxStrnFilt40Num11);
		data->phy_rx_diglpf_default_coeffs[7] = READ_PHYREG(pi, RxStrnFilt40Num12);
		data->phy_rx_diglpf_default_coeffs[8] = READ_PHYREG(pi, RxStrnFilt40Den10);
		data->phy_rx_diglpf_default_coeffs[9] = READ_PHYREG(pi, RxStrnFilt40Den11);
		data->phy_rx_diglpf_default_coeffs_valid = TRUE;

	}

	switch (dig_lpf_ht) {
	case 0:  /* restore rx dig lpf */

		/* ASSERT(pi->phy_rx_diglpf_default_coeffs_valid); */
		if (!data->phy_rx_diglpf_default_coeffs_valid) {
			break;
		}
		WRITE_PHYREG(pi, RxStrnFilt40Num00, data->phy_rx_diglpf_default_coeffs[0]);
		WRITE_PHYREG(pi, RxStrnFilt40Num01, data->phy_rx_diglpf_default_coeffs[1]);
		WRITE_PHYREG(pi, RxStrnFilt40Num02, data->phy_rx_diglpf_default_coeffs[2]);
		WRITE_PHYREG(pi, RxStrnFilt40Den00, data->phy_rx_diglpf_default_coeffs[3]);
		WRITE_PHYREG(pi, RxStrnFilt40Den01, data->phy_rx_diglpf_default_coeffs[4]);
		WRITE_PHYREG(pi, RxStrnFilt40Num10, data->phy_rx_diglpf_default_coeffs[5]);
		WRITE_PHYREG(pi, RxStrnFilt40Num11, data->phy_rx_diglpf_default_coeffs[6]);
		WRITE_PHYREG(pi, RxStrnFilt40Num12, data->phy_rx_diglpf_default_coeffs[7]);
		WRITE_PHYREG(pi, RxStrnFilt40Den10, data->phy_rx_diglpf_default_coeffs[8]);
		WRITE_PHYREG(pi, RxStrnFilt40Den11, data->phy_rx_diglpf_default_coeffs[9]);

		data->phy_rx_diglpf_default_coeffs_valid = FALSE;
		break;
	case 1:  /* set rx dig lpf to ltrn-lpf mode */

		WRITE_PHYREG(pi, RxStrnFilt40Num00, READ_PHYREG(pi, RxFilt40Num00));
		WRITE_PHYREG(pi, RxStrnFilt40Num01, READ_PHYREG(pi, RxFilt40Num01));
		WRITE_PHYREG(pi, RxStrnFilt40Num02, READ_PHYREG(pi, RxFilt40Num02));
		WRITE_PHYREG(pi, RxStrnFilt40Num10, READ_PHYREG(pi, RxFilt40Num10));
		WRITE_PHYREG(pi, RxStrnFilt40Num11, READ_PHYREG(pi, RxFilt40Num11));
		WRITE_PHYREG(pi, RxStrnFilt40Num12, READ_PHYREG(pi, RxFilt40Num12));
		WRITE_PHYREG(pi, RxStrnFilt40Den00, READ_PHYREG(pi, RxFilt40Den00));
		WRITE_PHYREG(pi, RxStrnFilt40Den01, READ_PHYREG(pi, RxFilt40Den01));
		WRITE_PHYREG(pi, RxStrnFilt40Den10, READ_PHYREG(pi, RxFilt40Den10));
		WRITE_PHYREG(pi, RxStrnFilt40Den11, READ_PHYREG(pi, RxFilt40Den11));

		break;
	case 2:  /* bypass rx dig lpf */
		/* 0x2d4 = sqrt(2) * 512 */
		ACPHY_REG_LIST_START
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Num00, 0x2d4)
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Num01, 0)
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Num02, 0)
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Den00, 0)
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Den01, 0)
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Num10, 0x2d4)
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Num11, 0)
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Num12, 0)
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Den10, 0)
			WRITE_PHYREG_ENTRY(pi, RxStrnFilt40Den11, 0)
		ACPHY_REG_LIST_EXECUTE(pi);
		break;

	default:
		ASSERT((dig_lpf_ht == 2) || (dig_lpf_ht == 1) || (dig_lpf_ht == 0));
		break;
	}
}

static void
wlc_phy_rxcal_radio_setup_acphy_tiny(phy_ac_rxiqcal_info_t *rxiqcali)
{
	phy_info_t *pi = rxiqcali->pi;
	acphy_tiny_rxcal_radioregs_t *porig = rxiqcali->ac_tiny_rxcal_radioregs_orig;
	uint8 core, txattn, rxattn;

	ASSERT(TINY_RADIO(pi));

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	/* sanity check */
	ASSERT(!porig->is_orig);
	porig->is_orig = TRUE;

	FOREACH_CORE(pi, core) {
		/* force off the dac 2 adc switches */
		MOD_RADIO_REG_TINY(pi, ADC_OVR1, core, ovr_adc_in_test, 0x1);
		MOD_RADIO_REG_TINY(pi, ADC_CFG10, core, adc_in_test, 0x0);
		/* papd loopback path settings for 2G */
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			porig->rf_tiny_rfrx_top_2g_ovr_east[core] = READ_RADIO_REG_TINY(pi,
				RX_TOP_2G_OVR_EAST, core);

			 /* Enable ipapd */
			MOD_RADIO_REG_TINY(pi, TX2G_MISC_CFG1, core, cal2g_pa_pu, 0x1);
			MOD_RADIO_REG_TINY(pi, RXRF2G_CFG2, core, loopback2g_papdcal_pu, 0x1);
			MOD_RADIO_REG_TINY(pi, RXRF2G_CFG2, core, lna2g_epapd_en, 0x0);

			if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && RADIOMAJORREV(pi) == 3) {
				MOD_RADIO_REG_20693(pi,
					RXIQMIX_CFG1, core, rxiq2g_coupler_pu, 0x1);
			}
			/* Set txattn and rxattn */
			if (RADIOREV(pi->pubpi->radiorev) == 8 ||
				RADIOREV(pi->pubpi->radiorev) == 10 ||
				RADIOREV(pi->pubpi->radiorev) == 13 ||
				RADIOREV(pi->pubpi->radiorev) == 14 ||
				RADIOREV(pi->pubpi->radiorev) == 18 ||
				RADIOMAJORREV(pi) == 3) {
					MOD_RADIO_REG_TINY(pi, TX2G_MISC_CFG1, core,
						cal2g_pa_atten, 0x0);
					MOD_RADIO_REG_TINY(pi, RXRF2G_CFG2, core,
						rf2g_papdcal_rx_attn, 0x0);
			} else {
					MOD_RADIO_REG_TINY(pi, TX2G_MISC_CFG1, core,
						cal2g_pa_atten, 0x3);
					MOD_RADIO_REG_TINY(pi, RXRF2G_CFG2, core,
						rf2g_papdcal_rx_attn, 0x3);
			}
			if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && RADIOMAJORREV(pi) == 3) {
			      /* lna2g_cfg1.lna2g_lna1_out_short_pu = 0 */
			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
			      ovr_lna2g_lna1_out_short_pu, 0x1);
			  MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_lna1_out_short_pu, 0x0);

			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
			      ovr_lna2g_tr_rx_en, 0x1);
			  MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_tr_rx_en, 0x0);

			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
			      ovr_lna2g_lna1_bypass, 0x1);
			  MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_lna1_bypass, 0x0);
			  MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_lna1_bypass_hiip3, 0x0);

			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
			      ovr_lna2g_lna1_pu, 0x1);
			  MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_lna1_pu, 0x0);

			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core,
			      ovr_gm2g_auxgm_pwrup, 0x1);
			  MOD_RADIO_REG_20693(pi, LNA2G_CFG2, core, gm2g_auxgm_pwrup, 0x0);
			} else {
				/* powerdown rxgm2g, powerup auxgm2g */
				MOD_RADIO_REG_TINY(pi, LNA2G_CFG2, core, gm2g_auxgm_pwrup, 0x1);
				MOD_RADIO_REG_TINY(pi, RX_TOP_2G_OVR_EAST, core,
					ovr_gm2g_auxgm_pwrup, 0x1);

				MOD_RADIO_REG_TINY(pi, RX_TOP_2G_OVR_EAST, core,
					ovr_gm2g_pwrup, 0x1);

				/* For 4349A0, AUX LNA2 op connects to LNA2 ip.
				   Hence LNA2 needs to be turned on. For 4349A2 however,
				   LNA2 needs to be turned off.
				 */
				if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
					MOD_RADIO_REG_20693(pi, LNA2G_CFG2, core, gm2g_pwrup,
						(IS_4349A2_RADIO(pi)) ? 0x0 : 0x1);
					MOD_RADIO_REG_20693(pi,
						RX_TOP_2G_OVR_EAST, core, ovr_lna2g_lna2_gain, 0x1);
					MOD_RADIO_REG_20693(pi, LNA2G_CFG2, core,
						lna2g_lna2_gain, 0x3);
				} else {
					MOD_RADIO_REG_TINY(pi, LNA2G_CFG2, core, gm2g_pwrup, 0x0);
				}
			}
		}

		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) &&
				(RADIOMAJORREV(pi) != 3)) {
				/* PAPD path settings */
				MOD_RADIO_REG_TINY(pi,
					RX_TOP_5G_OVR, core, ovr_lna5g_pu_auxlna2, 1);
				MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core, ovr_lna5g_tr_rx_en, 1);
				MOD_RADIO_REG_TINY(pi, LNA5G_CFG1, core, lna5g_tr_rx_en, 0);
				MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core, ovr_lna5g_lna1_pu, 1);
				MOD_RADIO_REG_TINY(pi, LNA5G_CFG1, core, lna5g_lna1_pu, 0);
				MOD_RADIO_REG_TINY(pi, LNA5G_CFG2, core, lna5g_pu_auxlna2, 1);
				MOD_RADIO_REG_TINY(pi, TX5G_MISC_CFG1, core, cal5g_pa_pu, 1);
				MOD_RADIO_REG_TINY(pi, TXRX5G_CAL_RX, core, loopback5g_cal_pu, 1);
				MOD_RADIO_REG_TINY(pi, PA5G_CFG1, core, rf5g_epapd_en, 0);
				MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core, ovr_gm5g_pwrup, 1);

				if (IS_4349A2_RADIO(pi)) {
					txattn = 0; rxattn = 0;
				} else {
					if (ROUTER_4349(pi) && !(PHY_IPA(pi))) {
						txattn = 1; rxattn = 1;
					} else {
						txattn = 3; rxattn = 3;
					}
				}

				MOD_RADIO_REG_TINY(pi,
					TX5G_MISC_CFG1, core, cal5g_pa_atten, txattn);
				MOD_RADIO_REG_TINY(pi,
					RXRF5G_CFG2, core, loopback5g_papdcel_rx_attn, rxattn);

				MOD_RADIO_REG_TINY(pi, PA5G_CFG4, core, pa5g_pu, 1);
				MOD_RADIO_REG_TINY(pi, TX_TOP_5G_OVR1, core, ovr_pa5g_pu, 1);
				MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core, ovr_lna5g_lna1_pu, 1);
				MOD_RADIO_REG_TINY(pi, LNA5G_CFG1, core, lna5g_lna1_pu, 0);

			} else if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) &&
			   RADIOMAJORREV(pi) == 3) {
			  /* loopback_mode 3 is loopback path for 4365 */
			  txattn = 0;
			  rxattn = 0;
			  /* Enable RX_IQ_CAL */
				MOD_RADIO_REG_20693(pi, TX5G_MISC_CFG1, core,
				cal5g_pa_pu, 1);
				MOD_RADIO_REG_20693(pi, LOGEN5G_EPAPD, core,
				rxiq5g_coupler_pu, 0x1);
				MOD_RADIO_REG_20693(pi, LOGEN5G_EPAPD, core,
				loopback5g_cal_pu, 0x1);

				MOD_RADIO_REG_20693(pi, TX5G_MISC_CFG1, core,
				cal5g_pa_atten, txattn);
				MOD_RADIO_REG_20693(pi, LOGEN5G_EPAPD, core,
				loopback5g_papdcel_rx_attn, rxattn);

				MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
				ovr_lna5g_lna1_out_short_pu, 0x1);
				MOD_RADIO_REG_20693(pi, LNA5G_CFG1, core,
				lna5g_lna1_out_short_pu, 0x0);
				/* lna5g_cfg1.lna5g_lna1_bypass = 0 */
				MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
				ovr_lna5g_lna1_bypass, 0x1);
				MOD_RADIO_REG_20693(pi, LNA5G_CFG1, core,
				lna5g_lna1_bypass, 0x0);
				/* lna5g_cfg1.lna5g_lna1_pu = 0 */
				MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
				ovr_lna5g_lna1_pu, 0x1);
				MOD_RADIO_REG_20693(pi, LNA5G_CFG1, core,
				lna5g_lna1_pu, 0x0);
				/* lna5g_cfg1.lna5g_tr_rx_en = 0 */
				MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
				ovr_lna5g_tr_rx_en, 0x1);
				MOD_RADIO_REG_20693(pi, LNA5G_CFG1, core,
				lna5g_tr_rx_en, 0x0);
				MOD_RADIO_REG_20693(pi, LOGEN5G_EPAPD, core, epapd_en, 0x0);
				MOD_RADIO_REG_20693(pi, LNA5G_CFG2, core, auxgm_pwrup, 0x0);
			} else {
				/* use low idx:forcing tx idx to 80 to account
				   for lna blow for YA1 chip variant
				 */
				wlc_phy_set_txpwr_by_index_acphy(pi, (1 << core), 116);

				/* leakage path settings */
				MOD_RADIO_REG_TINY(pi,
					PA5G_CFG4, core, pa5g_pu,
					(PHY_IPA(pi) && !PHY_ILNA(pi)) ? 0 : 1);
				MOD_RADIO_REG_TINY(pi, TX_TOP_5G_OVR1, core, ovr_pa5g_pu, 0x1);
				/* force lna1 kill switch to on */
				MOD_RADIO_REG_TINY(pi,
					RX_TOP_5G_OVR, core, ovr_lna5g_tr_rx_en, 0x1);
				MOD_RADIO_REG_TINY(pi, LNA5G_CFG1, core, lna5g_tr_rx_en, 0x1);
				MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core, ovr_lna5g_nap, 0x1);
				MOD_RADIO_REG_TINY(pi, LNA5G_CFG2, core, lna5g_nap, 0x0);

				MOD_RADIO_REG_TINY(pi, LOGEN_OVR1, core, ovr_logen_en_nap, 0x1);
				MOD_RADIO_REG_TINY(pi, LOGEN_CFG3, core, logen_en_nap, 0x0);

				MOD_RADIO_REG_TINY(pi, TXMIX5G_CFG4, core, mx5g_bbpdI_en, 0x0);
				MOD_RADIO_REG_TINY(pi, TXMIX5G_CFG4, core, mx5g_bbpdQ_en, 0x0);
				MOD_RADIO_REG_TINY(pi, PA5G_CFG1, core, rf5g_epapd_en,  0x0);
				MOD_RADIO_REG_TINY(pi,
					TXRX5G_CAL_RX, core, loopback5g_cal_pu,  0x0);
				MOD_RADIO_REG_TINY(pi, TIA_CFG9, core, tia_tx_lpbck_i, 0x0);
				MOD_RADIO_REG_TINY(pi, TIA_CFG9, core, tia_tx_lpbck_q, 0x0);

				/* power up detectors for saturation detection during loopback */
				MOD_RADIO_REG_TINY(pi, LNA5G_RSSI1, core, lna5g_dig_wrssi1_pu, 1);
				MOD_RADIO_REG_TINY(pi,
					RX_TOP_5G_OVR, core, ovr_lna5g_dig_wrssi1_pu, 1);
				MOD_RADIO_REG_TINY(pi, TIA_CFG12, core, rssi_pwrup, 1);
				MOD_RADIO_REG_TINY(pi, RX_BB_2G_OVR_EAST, core,
					ovr_tia_offset_rssi_pwrup, 1);
			}
		}
		if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
			if (RADIO20693REV(pi->pubpi->radiorev) == 8 ||
			RADIO20693REV(pi->pubpi->radiorev) == 10) {
				wlc_phy_set_txpwr_by_index_acphy(pi, (1 << core), 50);
			} else {
				wlc_phy_set_txpwr_by_index_acphy(pi, (1 << core), 100);
			}
		}
	}
}

void
wlc_phy_rxcal_radio_cleanup_acphy_tiny(phy_ac_rxiqcal_info_t *rxiqcali)
{
	phy_info_t *pi = rxiqcali->pi;
	acphy_tiny_rxcal_radioregs_t *porig = rxiqcali->ac_tiny_rxcal_radioregs_orig;

	uint8 core;
	ASSERT(TINY_RADIO(pi));

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	/* sanity check */
	ASSERT(porig->is_orig);
	porig->is_orig = FALSE;

	FOREACH_CORE(pi, core) {
		/* restore the dac 2 adc switches */
		MOD_RADIO_REG_TINY(pi, ADC_OVR1, core, ovr_adc_in_test, 0x0);
		MOD_RADIO_REG_TINY(pi, ADC_CFG10, core, adc_in_test, 0x0);

		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
				if (RADIOMAJORREV(pi) == 3) {
					/* Disable RX_IQ_CAL */
					MOD_RADIO_REG_20693(pi, TX5G_MISC_CFG1, core,
						cal5g_pa_pu, 0);
					MOD_RADIO_REG_20693(pi, LOGEN5G_EPAPD, core,
						rxiq5g_coupler_pu, 0);
					MOD_RADIO_REG_20693(pi, LOGEN5G_EPAPD, core,
						loopback5g_cal_pu, 0);
					/* lna5g_cfg1.lna5g_lna1_pu = 1 */
					MOD_RADIO_REG_20693(pi, LNA5G_CFG1, core,
						lna5g_lna1_pu, 0x1);
					/* lna5g_cfg1.lna5g_tr_rx_en = 1 */
					MOD_RADIO_REG_20693(pi, LNA5G_CFG1, core,
						lna5g_tr_rx_en, 0x1);
					/* disable ovr */
					MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
						ovr_lna5g_lna1_out_short_pu, 0x0);
					MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
						ovr_lna5g_lna1_bypass, 0x0);
					MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
						ovr_lna5g_lna1_pu, 0x0);
					MOD_RADIO_REG_20693(pi, RX_TOP_5G_OVR, core,
						ovr_lna5g_tr_rx_en, 0x0);
				} else {
					/* 5G PAPD path cleanup */
					MOD_RADIO_REG_TINY(pi, TX5G_MISC_CFG1, core,
						cal5g_pa_pu, 0x0);
					MOD_RADIO_REG_TINY(pi,
						TXRX5G_CAL_RX, core, loopback5g_cal_pu, 0x0);
					MOD_RADIO_REG_TINY(pi,
						RX_TOP_5G_OVR, core, ovr_lna5g_pu_auxlna2, 0x1);
					MOD_RADIO_REG_TINY(pi, LNA5G_CFG2, core,
						lna5g_pu_auxlna2, 0x0);
					MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core,
						ovr_gm5g_pwrup, 0x0);
					MOD_RADIO_REG_TINY(pi,
						RX_TOP_5G_OVR, core, ovr_lna5g_lna1_bypass, 0x0);
					MOD_RADIO_REG_TINY(pi, PA5G_CFG1, core,
						rf5g_epapd_en, 0x0);
					MOD_RADIO_REG_TINY(pi, TX_TOP_5G_OVR1, core,
						ovr_pa5g_pu, 0x0);
					MOD_RADIO_REG_TINY(pi, PA5G_CFG4, core, pa5g_pu, 0x0);
					MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core,
						ovr_lna5g_lna1_pu, 0);
					MOD_RADIO_REG_TINY(pi, LNA5G_CFG1, core, lna5g_lna1_pu, 0);
					MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core,
						ovr_lna5g_tr_rx_en, 0);
				}
			} else {
				/* 5G  leakage path cleanup */
				MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core, ovr_lna5g_nap, 0x0);
				MOD_RADIO_REG_TINY(pi, LOGEN_OVR1, core, ovr_logen_en_nap, 0x0);
				MOD_RADIO_REG_TINY(pi, LOGEN_OVR1, core, ovr_logencore_reset, 0x1);
				MOD_RADIO_REG_TINY(pi, LOGEN_CFG2, core, logencore_reset, 0x0);
				MOD_RADIO_REG_TINY(pi, LOGEN_CFG2, core, logencore_reset, 0x1);
				OSL_DELAY(1000);
				MOD_RADIO_REG_TINY(pi, LOGEN_CFG2, core, logencore_reset, 0x0);
				MOD_RADIO_REG_TINY(pi, LOGEN_OVR1, core, ovr_logencore_reset, 0x0);

				MOD_RADIO_REG_TINY(pi, TX_TOP_5G_OVR1, core, ovr_pa5g_pu, 0x0);
				MOD_RADIO_REG_TINY(pi, RX_TOP_5G_OVR, core, ovr_gm5g_pwrup, 0x0);
				/* disable override for lna kill switch */
				MOD_RADIO_REG_TINY(pi,
					RX_TOP_5G_OVR, core, ovr_lna5g_tr_rx_en, 0x0);
				MOD_RADIO_REG_TINY(pi, LNA5G_CFG1, core, lna5g_tr_rx_en, 0x1);
				MOD_RADIO_REG_TINY(pi, LNA5G_RSSI1, core, lna5g_dig_wrssi1_pu, 0);
				MOD_RADIO_REG_TINY(pi,
					RX_TOP_5G_OVR, core, ovr_lna5g_dig_wrssi1_pu, 0);
				MOD_RADIO_REG_TINY(pi, TIA_CFG12, core, rssi_pwrup, 0);
				MOD_RADIO_REG_TINY(pi, RX_BB_2G_OVR_EAST, core,
					ovr_tia_offset_rssi_pwrup, 0);
			}
		} else {
			/* 2g papd path cleanup */
			MOD_RADIO_REG_TINY(pi, TX2G_MISC_CFG1, core, cal2g_pa_pu, 0x0);
			MOD_RADIO_REG_TINY(pi, RXRF2G_CFG2, core, loopback2g_papdcal_pu, 0x0);
			MOD_RADIO_REG_TINY(pi, LNA2G_CFG2, core, gm2g_auxgm_pwrup, 0x0);
			if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && RADIOMAJORREV(pi) == 3) {
				MOD_RADIO_REG_20693(pi,
					RXIQMIX_CFG1, core, rxiq2g_coupler_pu, 0x0);
			}
			if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) {
				phy_utils_write_radioreg(pi, RADIO_REG_20691(pi,
					RX_TOP_2G_OVR_EAST, core),
					porig->rf_tiny_rfrx_top_2g_ovr_east[core]);
			} else {
				phy_utils_write_radioreg(pi, RADIO_REG_20693(pi,
					RX_TOP_2G_OVR_EAST, core),
					porig->rf_tiny_rfrx_top_2g_ovr_east[core]);
			}
			if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) &&
			    RADIOMAJORREV(pi) == 3) {
			  MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_tr_rx_en, 0x1);
			  MOD_RADIO_REG_20693(pi, LNA2G_CFG1, core, lna2g_lna1_pu, 0x1);

			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
			     ovr_lna2g_tr_rx_en, 0x0);
			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
			      ovr_lna2g_lna1_out_short_pu, 0x0);
			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
			      ovr_lna2g_lna1_bypass, 0x0);
			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST2, core,
			      ovr_lna2g_lna1_pu, 0x0);
			  MOD_RADIO_REG_20693(pi, RX_TOP_2G_OVR_EAST, core,
			      ovr_gm2g_auxgm_pwrup, 0x0);
			}
			MOD_RADIO_REG_TINY(pi, LNA2G_CFG2, core, gm2g_pwrup, 0x1);
			MOD_RADIO_REG_TINY(pi, RX_TOP_2G_OVR_EAST, core, ovr_gm2g_pwrup, 0x0);
		}
	}
}

void
phy_ac_rxiqcal(phy_info_t *pi)
{
	/*
	 *   Rx IQ Cal
	 */

#ifdef WFD_PHY_LL
	/* Single-core on 20MHz channel */
	wlc_phy_susp2tx_cts2self(pi, 3000);
#else
	wlc_phy_susp2tx_cts2self(pi, 9500);
#endif

	if ((pi->radar_percal_mask & 0x1) != 0) {
		pi->u.pi_acphy->radar_cal_active = TRUE;
	}

	wlc_phy_cals_mac_susp_en_other_cr(pi, TRUE);

	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		phy_ac_dccal(pi);
	}

	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		wlc_btcx_override_enable(pi);
		wlc_phy_cal_rx_fdiqi_acphy(pi);
		wlc_phy_btcx_override_disable(pi);
	}
	else {
		wlc_phy_cal_rx_fdiqi_acphy(pi);
	}

	wlc_phy_cals_mac_susp_en_other_cr(pi, FALSE);

#if !defined(PHYCAL_CACHING)
	/* cache cals for restore on return to home channel */
	wlc_phy_scanroam_cache_txcal_acphy(pi->u.pi_acphy->txiqlocali, 1);
	wlc_phy_scanroam_cache_rxcal_acphy(pi->u.pi_acphy->rxiqcali, 1);
#endif /* !defined(PHYCAL_CACHING) */

	/* move on */
	pi->cal_info->cal_phase_id++;
}

void
wlc_phy_rxcal_coeffs_upd(phy_info_t *pi, rxcal_coeffs_t *rxcal_cache)
{
	uint8 core;
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		WRITE_PHYREGCE(pi, Core1RxIQCompA, core, rxcal_cache[core].rxa);
		WRITE_PHYREGCE(pi, Core1RxIQCompB, core, rxcal_cache[core].rxb);
	}
}

#if !defined(PHYCAL_CACHING)
void
wlc_phy_scanroam_cache_rxcal_acphy(phy_type_rxiqcal_ctx_t *ctx, bool set)
{
	phy_ac_rxiqcal_info_t *info = (phy_ac_rxiqcal_info_t *)ctx;
	phy_info_t *pi = info->pi;
	uint8 core;

	/* Prepare Mac and Phregs */
	wlapi_suspend_mac_and_wait(pi->sh->physhim);
	phy_utils_phyreg_enter(pi);

	PHY_TRACE(("wl%d: %s: in scan/roam set %d\n", pi->sh->unit, __FUNCTION__, set));

	if (set) {
		PHY_CAL(("wl%d: %s: save the rxcal for scan/roam\n",
			pi->sh->unit, __FUNCTION__));
		/* save the rxcal to cache */
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			info->rxcal_cache[core].rxa =
				READ_PHYREGCE(pi, Core1RxIQCompA, core);
			info->rxcal_cache[core].rxb =
				READ_PHYREGCE(pi, Core1RxIQCompB, core);
		}

		/* mark the cache as valid */
		info->rxcal_cache_cookie = RXCAL_CACHE_VALID;
	} else {
		if (info->rxcal_cache_cookie == RXCAL_CACHE_VALID) {
			PHY_CAL(("wl%d: %s: restore the txcal after scan/roam\n",
				pi->sh->unit, __FUNCTION__));
			/* restore the rxcal from cache */
			wlc_phy_rxcal_coeffs_upd(pi, info->rxcal_cache);
		}
	}

	phy_utils_phyreg_exit(pi);
	wlapi_enable_mac(pi->sh->physhim);
}
#endif /* !defined(PHYCAL_CACHING) */
