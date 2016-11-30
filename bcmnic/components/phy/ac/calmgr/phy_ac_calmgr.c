/*
 * ACPHY Calibration Manager module implementation
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
 * $Id: phy_ac_calmgr.c 649330 2016-07-15 16:17:13Z mvermeid $
 */

#include <phy_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <phy_dbg.h>
#include <phy_mem.h>
#include "phy_type_calmgr.h"
#include "phy_type_cache.h"
#include <phy_ac.h>
#include <phy_btcx.h>
#include <phy_papdcal.h>
#include <phy_ac_calmgr.h>
#include <phy_ac_chanmgr.h>
#include <phy_ac_misc.h>
#include <phy_ac_papdcal.h>
#include <phy_ac_radio.h>
#include <phy_ac_rxgcrs.h>
#include <phy_ac_rxiqcal.h>
#include <phy_ac_tssical.h>
#include <phy_ac_txiqlocal.h>
#include <phy_ac_vcocal.h>
#include <phy_cache_api.h>

/* ************************ */
/* Modules used by this module */
/* ************************ */
#include <wlc_radioreg_20691.h>
#include <wlc_radioreg_20695.h>
#include "wlc_phy_ac_gains.h"
#include <wlc_phy_radio.h>
#include <wlc_phyreg_ac.h>
#include <wlc_phy_int.h>

#include <phy_utils_math.h>
#include <phy_utils_reg.h>
#include <phy_ac_info.h>

/* module private states */
struct phy_ac_calmgr_info {
	phy_info_t *pi;
	phy_ac_info_t *aci;
	phy_calmgr_info_t *cmn_info;
	/* cache coeffs */
	txcal_coeffs_t txcal_cache[PHY_CORE_MAX];
	/* cals  - Local copy of phyrxchains & EnTx bits before overwrite */
	uint8 enRx;
	uint8 enTx;
	uint8 enULB;
	int16 idac_i;
	int16 idac_q;
	uint8 tx_pwr_ctrl_state;
	uint32 cal_phase_id;
	uint32 start_time;
	uint8 save_nap_en;
	bool enIndxCap;
};

/* local functions */
static int phy_ac_calmgr_prepare(phy_type_calmgr_ctx_t *ctx);
static void phy_ac_calmgr_cleanup(phy_type_calmgr_ctx_t *ctx);
static bool phy_ac_calmgr_wd(phy_wd_ctx_t *ctx);
static void phy_ac_calmgr_nvram_attach(phy_info_t *pi);

/* register phy type specific implementation */
phy_ac_calmgr_info_t *
BCMATTACHFN(phy_ac_calmgr_register_impl)(phy_info_t *pi, phy_ac_info_t *aci,
	phy_calmgr_info_t *cmn_info)
{
	phy_ac_calmgr_info_t *ac_info;
	phy_type_calmgr_fns_t fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* allocate all storage together */
	if ((ac_info = phy_malloc(pi, sizeof(phy_ac_calmgr_info_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc failed\n", __FUNCTION__));
		goto fail;
	}
	ac_info->pi = pi;
	ac_info->aci = aci;
	ac_info->cmn_info = cmn_info;

	/* register watchdog fn */
	if (phy_wd_add_fn(pi->wdi, phy_ac_calmgr_wd, ac_info,
		PHY_WD_PRD_1TICK, PHY_WD_GLACIAL_CAL, PHY_WD_FLAG_DEF_DEFER) != BCME_OK) {
		PHY_ERROR(("%s: phy_wd_add_fn failed\n", __FUNCTION__));
		goto fail;
	}

	/* register PHY type specific implementation */
	bzero(&fns, sizeof(fns));
	fns.prepare = phy_ac_calmgr_prepare;
	fns.cleanup = phy_ac_calmgr_cleanup;
	fns.cals = wlc_phy_cals_acphy;
	fns.ctx = ac_info;

	/* Read srom params from nvram */
	phy_ac_calmgr_nvram_attach(pi);

	if (phy_calmgr_register_impl(cmn_info, &fns) != BCME_OK) {
		PHY_ERROR(("%s: phy_calmgr_register_impl failed\n", __FUNCTION__));
		goto fail;
	}

	return ac_info;

	/* error handling */
fail:
	if (ac_info != NULL)
		phy_mfree(pi, ac_info, sizeof(phy_ac_calmgr_info_t));
	return NULL;
}

void
BCMATTACHFN(phy_ac_calmgr_unregister_impl)(phy_ac_calmgr_info_t *ac_info)
{
	phy_info_t *pi;
	phy_calmgr_info_t *cmn_info;

	ASSERT(ac_info);
	pi = ac_info->pi;
	cmn_info = ac_info->cmn_info;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* unregister from common */
	phy_calmgr_unregister_impl(cmn_info);

	phy_mfree(pi, ac_info, sizeof(phy_ac_calmgr_info_t));
}

static int
phy_ac_calmgr_prepare(phy_type_calmgr_ctx_t *ctx)
{
	BCM_REFERENCE(ctx);
	PHY_TRACE(("%s\n", __FUNCTION__));
	return BCME_OK;
}

static void
phy_ac_calmgr_cleanup(phy_type_calmgr_ctx_t *ctx)
{
	BCM_REFERENCE(ctx);
	PHY_TRACE(("%s\n", __FUNCTION__));
}

/* Check to see if a cal needs to be run */
static bool
phy_ac_wd_perical_schedule(phy_info_t *pi)
{
	if (pi->disable_percal) {
		return FALSE;
	}

	if (phy_papdcal_is_wfd_phy_ll_enable(pi->papdcali) && DCS_INPROG_PHY(pi)) {
		return FALSE;
	}

	if ((pi->phy_cal_mode == PHY_PERICAL_DISABLE) ||
		(pi->phy_cal_mode == PHY_PERICAL_MANUAL) ||
		(pi->cal_info->cal_suppress_count != 0)) {
		return FALSE;
	}

	if (GLACIAL_TIMEOUT(pi)) {

		/* RSDB designs need special handling */
		if (RSDB_FAMILY(pi)) {

			uint8 curr_core = phy_get_current_core(pi);
			uint16 phymode = phy_get_phymode(pi);

			if ((phymode == PHYMODE_RSDB) &&
				(curr_core == PHY_RSBD_PI_IDX_CORE1) &&
				!PUB_NOT_ASSOC(phy_get_other_pi(pi))) {

				uint referred_time, available_cal_slot;

				referred_time = PHYTIMER_NOW(phy_get_other_pi(pi));
				available_cal_slot = LAST_CAL_TIME(phy_get_other_pi(pi)) +
					(GLACIAL_TIMER(pi) >> 1);

				if (referred_time <= available_cal_slot)
					pi->sh->scheduled_cal_time = available_cal_slot;
				else
					pi->sh->scheduled_cal_time = available_cal_slot +
						GLACIAL_TIMER(pi);

				PHY_CAL(("wl%d: %s : Rt %d Acs %d | Sct %d\n",
					PI_INSTANCE(pi), __FUNCTION__,
					referred_time, available_cal_slot,
					pi->sh->scheduled_cal_time));

				if (referred_time >= pi->sh->scheduled_cal_time)
					return TRUE;
				else
					return FALSE;
			} /* instance : pi(1) */
			else
				return TRUE;
		} /* chip : rsdb family */
		else {
			pi->cal_info->last_cal_time = pi->sh->now;
			return TRUE;
		}
	} /* event : galcial timeout */
	else
		return FALSE;
}

/* watchdog callback */
static bool
phy_ac_calmgr_wd(phy_wd_ctx_t *ctx)
{
	phy_ac_calmgr_info_t *info = (phy_ac_calmgr_info_t *)ctx;
	phy_info_t *pi = info->pi;
	/* Check to see if a cal needs to be run */
	if (phy_ac_wd_perical_schedule(pi)) {
		PHY_CAL(("wl%d: %s: wd triggered cal\n"
			"(cal mode=%d, now=%d, prev_time=%d, gt=%d)\n",
			PI_INSTANCE(pi), __FUNCTION__, pi->phy_cal_mode, PHYTIMER_NOW(pi),
			LAST_CAL_TIME(pi), GLACIAL_TIMER(pi)));
		wlc_phy_cal_perical((wlc_phy_t *)pi, PHY_PERICAL_WATCHDOG);
	}
	return TRUE;
}

static void
BCMATTACHFN(phy_ac_calmgr_nvram_attach)(phy_info_t *pi)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		pi->phy_cal_mode = PHY_PERICAL_SPHASE;
	}
	pi_ac->enIndxCap = TRUE;
}

static void
wlc_idac_read_20691(phy_info_t *pi, int16 *i, int16 *q)
{
	int16 i1, q1;

	ASSERT(ACREV_GE(pi->pubpi->phy_rev, 11));

	MOD_PHYREG(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl, 0x0);

	wlc_phy_enable_lna_dcc_comp_20691(pi, 0);
	OSL_DELAY(2);

	i1 = READ_PHYREG(pi, TIA_offset_DAC_I);

	if (i1 > 255)
		i1 = 256 - i1;

	q1 = READ_PHYREG(pi, TIA_offset_DAC_Q);

	if (q1 > 255)
		q1 = 256 - q1;

	*i = i1;
	*q = q1;

	MOD_PHYREG(pi, RfseqTrigger, en_pkt_proc_dcc_ctrl, 0x1);

	wlc_phy_enable_lna_dcc_comp_20691(pi, PHY_ILNA(pi));
}

void
phy_ac_calmgr_init(phy_info_t *pi, uint8 *searchmode, acphy_cal_result_t *accal, uint8 phase_id)
{
	phy_ac_calmgr_info_t *ci = pi->u.pi_acphy->calmgri;

	/* Initializations */
	ci->enRx = 0;
	ci->enTx = 0;
	ci->enULB = 0;
	ci->idac_i = 0;
	ci->idac_q = 0;
#ifdef WL_NAP
	ci->save_nap_en = 0;
#endif /* WL_NAP */
	ci->enIndxCap = pi->u.pi_acphy->enIndxCap;

	if (ACMAJORREV_4(pi->pubpi->phy_rev))
		pi->u.pi_acphy->enIndxCap = FALSE;

	phy_ac_chanmgr_cal_init(pi, &(ci->enULB));

#ifdef WL_NAP
	/* Disable napping during cals */
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		ci->save_nap_en = READ_PHYREGFLD(pi, NapCtrl, nap_en);
		phy_ac_nap_enable(pi, FALSE, FALSE);
	}
#endif /* WL_NAP */

	if (ACMAJORREV_3(pi->pubpi->phy_rev)) {
		wlc_phy_enable_lna_dcc_comp_20691(pi, 0);

		if (PHY_ILNA(pi)) {
			wlc_idac_read_20691(pi, &(ci->idac_i), &(ci->idac_q));
		}
	}

	phy_ac_radio_cal_init(pi);

	/* Save and overwrite Rx chains */
	wlc_phy_update_rxchains((wlc_phy_t *)pi, &(ci->enRx), &(ci->enTx),
		pi->sh->hw_phyrxchain, pi->sh->hw_phytxchain);

#ifdef OCL
	if (PHY_OCL_ENAB(pi->sh->physhim)) {
		wlc_phy_ocl_disable_req_set((wlc_phy_t *)pi, OCL_DISABLED_CAL,
			TRUE, WLC_OCL_REQ_CAL);
	}
#endif

	phy_ac_rxgcrs_cal(pi->u.pi_acphy->rxgcrsi, ci->enULB);
	/*
	 * Search-Mode Sanity Check for Tx-iqlo-Cal
	 *
	 * Notes: - "RESTART" means: start with 0-coeffs and use large search radius
	 *        - "REFINE"  means: start with latest coeffs and only search
	 *                    around that (faster)
	 *        - here, if channel has changed or no previous valid coefficients
	 *          are available, enforce RESTART search mode (this shouldn't happen
	 *          unless cal driver code is work-in-progress, so this is merely a safety net)
	 */
	if ((pi->radio_chanspec != accal->chanspec) ||
	    (accal->txiqlocal_coeffsvalid == 0)) {
		*searchmode = PHY_CAL_SEARCHMODE_RESTART;
	}

	/*
	 * If previous phase of multiphase cal was on different channel,
	 * then restart multiphase cal on current channel (again, safety net)
	 */
	if ((phase_id > MPHASE_CAL_STATE_INIT)) {
		if (accal->chanspec != pi->radio_chanspec) {
			wlc_phy_cal_perical_mphase_restart(pi);
		}
	}

#ifdef WFD_PHY_LL_DEBUG
	ci->cal_phase_id = pi->cal_info->cal_phase_id;
	ci->start_time = hnd_time_us();
#endif

	/* Make the ucode send a CTS-to-self packet with duration set to 10ms. This
	 *  prevents packets from other STAs/AP from interfering with Rx IQcal
	 */
	ci->tx_pwr_ctrl_state = pi->txpwrctrl;

	phy_ac_papdcal_cal_init(pi);

	if ((ACMAJORREV_32(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev)) {
		phy_ac_chanmgr_disable_core2core_sync_setup(pi);
	}
	pi->u.pi_acphy->radar_cal_active = TRUE;
}

void
phy_ac_calmgr_clean(phy_info_t *pi, bool *suspend)
{
	phy_ac_calmgr_info_t *ci = pi->u.pi_acphy->calmgri;

	uint8 core;
	if ((PHY_IPA(pi)) && (ci->tx_pwr_ctrl_state == PHY_TPC_HW_ON) && (!TINY_RADIO(pi)) &&
			(!ACMAJORREV_36(pi->pubpi->phy_rev))) {
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			MOD_PHYREGCEE(pi, EpsilonTableAdjust, core, epsilonOffset, 0);
		}
	}

	wlc_phy_txpwrctrl_enable_acphy(pi, ci->tx_pwr_ctrl_state);

	/* Restore Rx chains */
	wlc_phy_restore_rxchains((wlc_phy_t *)pi, ci->enRx, ci->enTx);

	phy_ac_radio_cal_reset(pi, ci->idac_i, ci->idac_q);

	phy_ac_chanmgr_cal_reset(pi);

	phy_utils_phyreg_exit(pi);
	wlapi_enable_mac(pi->sh->physhim);

#ifdef ATE_BUILD
		PHY_INFORM(("===> Resuming MAC, after cal\n"));
#endif /* ATE_BUILD */

#ifdef WL11ULB
	phy_ac_rxgcrs_ulb_cal(pi->u.pi_acphy->rxgcrsi, ci->enULB);
#endif /* WL11ULB */

#ifdef OCL
	if (PHY_OCL_ENAB(pi->sh->physhim)) {
		wlc_phy_ocl_disable_req_set((wlc_phy_t *)pi, OCL_DISABLED_CAL,
		                            FALSE, WLC_OCL_REQ_CAL);
	}
#endif

#ifdef WL_NAP
	/* Restore nap_en state */
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		phy_ac_nap_enable(pi, ci->save_nap_en, FALSE);
	}
#endif /* WL_NAP */

	/* Resume MAC */
	wlc_phy_conditional_resume(pi, suspend);

#ifdef WFD_PHY_LL_DEBUG
	PHY_INFORM(("phase_id:%2d usec:%d\n", ci->cal_phase_id, hnd_time_us() - ci->start_time));
#endif
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		pi->u.pi_acphy->enIndxCap = ci->enIndxCap;
		wlc_phy_ac_gains_load(pi);
	}
}

void
phy_ac_calmgr_singleshot(phy_info_t *pi, uint8 searchmode, acphy_cal_result_t *accal)
{
	/*
	 * SINGLE-SHOT Calibrations
	 *
	 *    Call all Cals one after another
	 *
	 *    Notes:
	 *    - if this proc is called with the phase state in IDLE,
	 *      we know that this proc was called directly rather
	 *      than via the mphase scheduler (the latter puts us into
	 *      INIT state); under those circumstances, perform immediate
	 *      execution over all cal tasks
	 *    - for better code structure, we would use the below mphase code for
	 *      sphase case, too, by utilizing an appropriate outer for-loop
	 */

	/* TO-DO: Ensure that all inits and cleanups happen here */
	/* carry out all phases "en bloc", for comments see the various phases below */
	const uint16 tbl_cookie = TXCAL_CACHE_VALID;
#if defined(PHYCAL_CACHING)
	ch_calcache_t *ctx = wlc_phy_get_chanctx(pi, pi->radio_chanspec);
#endif
	pi->cal_info->last_cal_time = pi->sh->now;
	accal->chanspec = pi->radio_chanspec;

	if (TINY_RADIO(pi) && (READ_RADIO_REGFLD_20691(pi, PLL_DSPR27, 0,
		rfpll_monitor_need_refresh) == 1)) {
		if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) {
			/* turn on VCO Calibration clock */
			MOD_RADIO_REG_20691(pi, PLL_XTAL2, 0, xtal_pu_caldrv, 0x1);
		}
		wlc_phy_radio_tiny_vcocal(pi);
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		bool need_refresh = (pi->u.pi_acphy->pll_sel == PLL_2G) ?
				READ_RADIO_REGFLD_28NM(pi, RFP, PLL_CFGR1, 0,
				rfpll_monitor_need_refresh) :
				READ_RADIO_REGFLD_28NM(pi, RFP, PLL5G_CFGR1, 0,
				rfpll_5g_monitor_need_refresh);
		if (need_refresh) {
			wlc_phy_28nm_radio_vcocal(pi, VCO_CAL_MODE_20695,
					VCO_CAL_COUPLING_MODE_20695);
		}
	}
	/* turn off VCO Calibration clock */
	if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) {
		wlc_phy_radio2069x_vcocal_isdone(pi, TRUE, FALSE);
		MOD_RADIO_REG_20691(pi, PLL_XTAL2, 0, xtal_pu_caldrv, 0x0);
	}

	if (!ACMAJORREV_40(pi->pubpi->phy_rev)) {
		wlc_phy_precal_txgain_acphy(pi, accal->txcal_txgain);
		wlc_phy_cal_txiqlo_acphy(pi, searchmode, FALSE, 0);
		/* request "Sphase" */
	}

	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		phy_ac_dccal(pi);
	}


	if (TINY_RADIO(pi)) {
		if ((RADIO20691_MAJORREV(pi->pubpi->radiorev) == 1) &&
			(RADIO20691_MINORREV(pi->pubpi->radiorev) == 16)) {
			/* There is a return at the beginning of below cal */
			/* to enable the war go to below function and remove the return */
			acphy_analog_dc_cal_war(pi); /* DC-cal software WAR for 4364A0 */
		} else {
			wlc_phy_tiny_static_dc_offset_cal(pi);
		}
	} else if ((!ACMAJORREV_36(pi->pubpi->phy_rev)) &&
			(!ACMAJORREV_25(pi->pubpi->phy_rev)) &&
			(!ACMAJORREV_40(pi->pubpi->phy_rev))) {
		wlc_phy_precal_txgain_acphy(pi, accal->txcal_txgain);
		wlc_phy_cal_txiqlo_acphy(pi, searchmode, FALSE, 1); /* request "Sphase" */
	}

	wlc_phy_txpwrctrl_idle_tssi_meas_acphy(pi);

	wlc_phy_cals_mac_susp_en_other_cr(pi, TRUE);
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		wlc_btcx_override_enable(pi);
		wlc_phy_cal_rx_fdiqi_acphy(pi);
		wlc_phy_btcx_override_disable(pi);
	} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		phy_ac_dccal_init(pi);
		phy_ac_load_gmap_tbl(pi);

		wlc_btcx_override_enable(pi);
		phy_ac_dccal(pi);

		wlc_phy_precal_txgain_acphy(pi, accal->txcal_txgain);
		wlc_phy_cal_txiqlo_acphy(pi, searchmode, FALSE, 1);
		/* request "Sphase" */

		wlc_phy_cal_rx_fdiqi_acphy(pi);

		wlc_phy_precal_txgain_acphy(pi, accal->txcal_txgain);
		wlc_phy_cal_txiqlo_acphy(pi, searchmode, FALSE, 0);
		wlc_phy_btcx_override_disable(pi);
		/* request "Sphase" */
	} else  {
		wlc_phy_cal_rx_fdiqi_acphy(pi);
	}

	wlc_phy_cals_mac_susp_en_other_cr(pi, FALSE);

	if (!ACMAJORREV_40(pi->pubpi->phy_rev) && !ACMAJORREV_37(pi->pubpi->phy_rev)) {
		if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
			!ACMAJORREV_33(pi->pubpi->phy_rev)) {
			phy_ac_dssf(pi->u.pi_acphy->rxspuri, TRUE);
		}
		wlc_phy_table_write_acphy(pi, wlc_phy_get_tbl_id_iqlocal(pi, 0), 1,
				IQTBL_CACHE_COOKIE_OFFSET, 16, &tbl_cookie);
		wlc_phy_do_papd_cal_acphy(pi);
	}

	pi->first_cal_after_assoc = FALSE;

#if !defined(PHYCAL_CACHING)
	/* cache cals for restore on return to home channel */
	wlc_phy_scanroam_cache_txcal_acphy(pi->u.pi_acphy->txiqlocali, 1);
	wlc_phy_scanroam_cache_rxcal_acphy(pi->u.pi_acphy->rxiqcali, 1);
#endif /* !defined(PHYCAL_CACHING) */
#if defined(PHYCAL_CACHING)
	if (ctx)
		wlc_phy_cal_cache((wlc_phy_t *)pi);
#endif
}

static void
phy_ac_calmgr_init_phase(phy_info_t *pi)
{
	/*
	 *   Housekeeping & Pre-Txcal Tx Gain Adjustment
	 */

#ifdef WFD_PHY_LL
	/* Single-core on 20MHz channel */
	wlc_phy_susp2tx_cts2self(pi, 2500);
#else
	wlc_phy_susp2tx_cts2self(pi, 4000);
#endif

	/* remember time and channel of this cal event */
	pi->cal_info->last_cal_time     = pi->sh->now;
	pi->cal_info->u.accal.chanspec = pi->radio_chanspec;

	wlc_phy_precal_txgain_acphy(pi, pi->cal_info->u.accal.txcal_txgain);

	/* move on */
	pi->cal_info->cal_phase_id++;
}

void
phy_ac_calmgr_multiphase(phy_info_t *pi, uint8 phase_id, uint8 searchmode)
{
/*
 * MULTI-PHASE CAL
 *
 *	 Carry out next step in multi-phase execution of cal tasks
 *
 */
	switch (phase_id) {
	case ACPHY_CAL_PHASE_INIT:
		phy_ac_calmgr_init_phase(pi);
		break;

	case ACPHY_CAL_PHASE_TX0:
	case ACPHY_CAL_PHASE_TX1:
	case ACPHY_CAL_PHASE_TX2:
	case ACPHY_CAL_PHASE_TX3:
	case ACPHY_CAL_PHASE_TX4:
	case ACPHY_CAL_PHASE_TX5:
	case ACPHY_CAL_PHASE_TX6:
	case ACPHY_CAL_PHASE_TX7:
	case ACPHY_CAL_PHASE_TX8:
	case ACPHY_CAL_PHASE_TX9:
	case ACPHY_CAL_PHASE_TX_LAST:
		phy_ac_txiqlocal(pi, phase_id, searchmode);
		break;

	case ACPHY_CAL_PHASE_PAPDCAL:
		phy_ac_papdcal(pi);
		break;

	case ACPHY_CAL_PHASE_TXPRERXCAL0:
	case ACPHY_CAL_PHASE_TXPRERXCAL1:
	case ACPHY_CAL_PHASE_TXPRERXCAL2:
		phy_ac_txiqlocal_prerx(pi, searchmode);
		break;
	case ACPHY_CAL_PHASE_RXCAL:
		phy_ac_rxiqcal(pi);
		break;

	case ACPHY_CAL_PHASE_RSSICAL:
		phy_ac_vcocal(pi);
		break;

	case ACPHY_CAL_PHASE_IDLETSSI:
		phy_ac_tssical_idle(pi);
		break;

	default:
		PHY_ERROR(("%s: Invalid calibration phase %d\n", __FUNCTION__, phase_id));
		ASSERT(0);
		wlc_phy_cal_perical_mphase_reset(pi);
		break;
	}
}

void
wlc_phy_cals_acphy(phy_type_calmgr_ctx_t *ctx, uint8 legacy_caltype, uint8 searchmode)
{
	phy_ac_calmgr_info_t *info = (phy_ac_calmgr_info_t *)ctx;
	phy_info_t *pi = info->pi;
	uint8 phase_id = pi->cal_info->cal_phase_id;
	acphy_cal_result_t *accal = &pi->cal_info->u.accal;
	bool suspend = FALSE;
	uint32 cal_start_time;
	BCM_REFERENCE(legacy_caltype);

	PHY_CAL(("wl%d: Running ACPHY periodic calibration: Searchmode: %d. phymode: 0x%x \n",
	         pi->sh->unit, searchmode, phy_get_phymode(pi)));

	if (PHY_CAL_SEARCHMODE_UNDEF == searchmode) {
		PHY_ERROR(("%s: Invalid search mode %d\n", __FUNCTION__, searchmode));
		ASSERT(0);
		return;
	}

	/* Suspend MAC if haven't done so */
	wlc_phy_conditional_suspend(pi, &suspend);

	if (NORADIO_ENAB(pi->pubpi)) {
		/* Resume MAC */
		wlc_phy_conditional_resume(pi, &suspend);
		return;
	}

#ifdef ATE_BUILD
	PHY_INFORM(("===> wl%d:"
		"Running ACPHY periodic calibration: Searchmode: %d. phymode: 0x%x \n",
		pi->sh->unit, searchmode, phy_get_phymode(pi)));
#endif /* ATE_BUILD */

	/* -----------------
	 *  Initializations
	 * -----------------
	 */

	/* Exit immediately if we are running on Quickturn */
	if (ISSIM_ENAB(pi->sh->sih)) {
		wlc_phy_cal_perical_mphase_reset(pi);
		/* Resume MAC */
		wlc_phy_conditional_resume(pi, &suspend);
		return;
	}

	/* skip cal if phy is muted */
	if (PHY_MUTED(pi) && ((!TINY_RADIO(pi)) ||
		(ACMAJORREV_3(pi->pubpi->phy_rev) && ACMINORREV_6(pi)))) {
		/* Resume MAC */
		wlc_phy_conditional_resume(pi, &suspend);
		return;
	}

	cal_start_time = OSL_SYSUPTIME();
	phy_ac_calmgr_init(pi, &searchmode, accal, phase_id);

	/* -------------------
	 *  Calibration Calls
	 * -------------------
	 */

	PHY_CAL(("wlc_phy_cals_acphy: Time=%d, LastTi=%d, SrchMd=%d, PhIdx=%d,"
		" Chan=%d, LastCh=%d, First=%d, vld=%d\n",
		pi->sh->now, pi->cal_info->last_cal_time, searchmode, phase_id,
		pi->radio_chanspec, accal->chanspec,
		pi->first_cal_after_assoc, accal->txiqlocal_coeffsvalid));

	if (phase_id == MPHASE_CAL_STATE_IDLE) {
		phy_ac_calmgr_singleshot(pi, searchmode, accal);
	} else {
		phy_ac_calmgr_multiphase(pi, phase_id, searchmode);
	}

	/* ----------
	 *  Cleanups
	 * ----------
	 */
	phy_ac_calmgr_clean(pi, &suspend);

	pi->cal_dur += OSL_SYSUPTIME() - cal_start_time;
}