
/*
 * ACPHY TxPowerCtrl module implementation
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
 * $Id: phy_ac_tpc.c 660700 2016-09-21 16:15:26Z luka $
 */
#include <phy_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <phy_dbg.h>
#include <phy_mem.h>
#include <phy_wd.h>
#include "phy_type_tpc.h"
#include <phy_ac.h>
#include <phy_ac_tpc.h>
#include <phy_ac_tbl.h>
#include <phy_ac_temp.h>
#include <phy_ac_txiqlocal.h>
#include <phy_utils_reg.h>
#include <phy_ac_info.h>
#include <wlc_phyreg_ac.h>
#include <wlc_phytbl_ac.h>
#include <phy_papdcal.h>
#include <phy_tpc_api.h>
#include <phy_tpc.h>
#ifdef WL_SAR_SIMPLE_CONTROL
#endif /* WL_SAR_SIMPLE_CONTROL */
#include <phy_utils_var.h>
#include <phy_rstr.h>
#ifdef WLC_SW_DIVERSITY
#include "phy_ac_antdiv.h"
#endif
#ifdef WLC_TXPWRCAP
#include <phy_ac_txpwrcap.h>
#endif

#ifndef ALL_NEW_PHY_MOD
/* < TODO: all these are going away... */
#include <wlc_phy_hal.h>
/* TODO: all these are going away... > */
#endif

#include <bcmdevs.h>

#define PDOFFSET(pi, nvramstrng, core) ((uint16)(PHY_GETINTVAR((pi), \
	(nvramstrng)) >> (5 * (core))) & 0x1f)

/* #ifdef PREASSOC_PWRCTRL */
typedef struct phy_pwr_ctrl_save_acphy {
	bool status_idx_carry_2g[PHY_CORE_MAX];
	bool status_idx_carry_5g[PHY_CORE_MAX];
	uint8 status_idx_2g[PHY_CORE_MAX];
	uint8 status_idx_5g[PHY_CORE_MAX];
	uint16 last_chan_stored_2g;
	uint16 last_chan_stored_5g;
	int8   pwr_qdbm_2g[PHY_CORE_MAX];
	int8   pwr_qdbm_5g[PHY_CORE_MAX];
	bool   stored_not_restored_2g[PHY_CORE_MAX];
	bool   stored_not_restored_5g[PHY_CORE_MAX];
} phy_pwr_ctrl_s;
/* #endif */  /* PREASSOC_PWRCTRL */

/* module private states */
struct phy_ac_tpc_info {
	phy_info_t *pi;
	phy_ac_info_t *aci;
	phy_tpc_info_t *ti;
	/* #ifdef PREASSOC_PWRCTRL */
	phy_pwr_ctrl_s *pwr_ctrl_save;
	/* #endif */  /* PREASSOC_PWRCTRL */
	/* std params */
	uint16	offset_targetpwr; /* target offset power (in qDb) */
	int8	txpwr_offset[PHY_CORE_MAX];			/* qdBm signed offset for tx pwr */
	uint8	txpwrindex_hw_save[PHY_CORE_MAX];	/* txpwr start index for hwpwrctrl */
	uint8	txpwrindex_cck_hw_save[PHY_CORE_MAX]; /* txpwr start index for hwpwrctrl */
	uint8	txpwrindexlimit[NUM_TXPWRINDEX_LIM];
	uint8	txpwrindex_hw_save_chan; /* current chan for which baseindex is saved */
	uint8	txpwr_damping_factor_set;
	/* #if defined(WLOLPC) || defined (BCMDBG) || defined(WLTEST) */
	bool	olpc_dbg_mode;
	bool	olpc_dbg_mode_caldone;
	/* #endif */
/* add other variable size variables here at the end */
};

/* local functions */
static void phy_ac_tpc_std_params_attach(phy_ac_tpc_info_t *info);
static void wlc_phy_txpwrctrl_pwr_setup_acphy(phy_info_t *pi);
static void wlc_phy_txpwrctrl_pwr_setup_srom12_acphy(phy_info_t *pi);
int8 wlc_phy_fittype_srom12_acphy(phy_info_t *pi);
static void wlc_phy_get_srom12_pdoffset_acphy(phy_info_t *pi, int8 *pdoffs);
static void phy_ac_tpc_get_paparams_percore_srom12(phy_info_t *pi,
		uint8 chan_freq_range, int16 *a, int16 *b, int16 *c, int16 *d, uint8 core);
static void phy_ac_tpc_get_paparams_srom12(phy_info_t *pi,
		uint8 chan_freq_range, int16 *a, int16 *b, int16 *c, int16 *d);
static void phy_ac_tpc_get_paparams_80p80_srom12(phy_info_t *pi,
		uint8 *chan_freqs, int16 *a, int16 *b, int16 *c, int16 *d);
static int32 phy_ac_tpc_get_estpwrlut_srom12(int16 *a, int16 *b, int16 *c, int16 *d,
		uint8 pa_fittype, uint8 core, int32 idx);
static void wlc_phy_get_tssi_floor_acphy(phy_info_t *pi, int16 *floor);
static uint32 wlc_phy_pdoffset_cal_acphy(uint32 pdoffs, uint16 pdoffset, uint8 band, uint8 core);
static bool wlc_phy_txpwrctrl_ison_acphy(phy_info_t *pi);
static uint8 wlc_phy_set_txpwr_clamp_acphy(phy_info_t *pi, uint8 core);
static void wlc_phy_txpower_recalc_target_acphy(phy_info_t *pi);
#ifdef WL_MU_TX
static void phy_ac_tpc_offload_ppr_to_svmp(phy_info_t *pi, ppr_t* tx_power_offset);
#endif
static void phy_ac_tpc_recalc_tgt(phy_type_tpc_ctx_t *ctx);
static void wlc_phy_txpower_recalc_target_ac_big(phy_type_tpc_ctx_t *ctx, ppr_t *tx_pwr_target,
    ppr_t *srom_max_txpwr, ppr_t *reg_txpwr_limit, ppr_t *txpwr_targets);
static void phy_ac_tpc_sromlimit_get(phy_type_tpc_ctx_t *ctx, chanspec_t chanspec, ppr_t *max_pwr,
    uint8 core);
static void wlc_phy_txpwrctrl_setminpwr(phy_info_t *pi);
static bool phy_ac_tpc_hw_ctrl_get(phy_type_tpc_ctx_t *ctx);
static void phy_ac_tpc_set(phy_type_tpc_ctx_t *ctx, ppr_t *reg_pwr);
static void phy_ac_tpc_set_flags(phy_type_tpc_ctx_t *ctx, phy_tx_power_t *power);
static void phy_ac_tpc_set_max(phy_type_tpc_ctx_t *ctx, phy_tx_power_t *power);
static bool phy_ac_tpc_wd(phy_wd_ctx_t *ctx);
static int phy_ac_tpc_init(phy_type_tpc_ctx_t *ctx);
static void phy_ac_fill_estpwrshft_table(phy_info_t *pi, srom12_pwrdet_t *pwrdet, uint8 chan_freq,
		uint32 *shfttblval);

#if defined(PHYCAL_CACHING)
static int8 wlc_phy_get_thresh_acphy(phy_info_t *pi);
#if (defined(WLOLPC) && !defined(WLOLPC_DISABLED))
static int8 wlc_phy_olpcthresh(void);
static void phy_ac_tpc_update_olpc_cal(phy_type_tpc_ctx_t *tpc_ctx, bool set, bool dbg);
#endif /* WLOLPC && !defined(WLOLPC_DISABLED) */
#endif /* PHYCAL_CACHING */
#ifdef FCC_PWR_LIMIT_2G
static void wlc_phy_fcc_pwr_limit_set_acphy(phy_type_tpc_ctx_t *ctx, bool enable);
#endif /* FCC_PWR_LIMIT_2G */
#if defined(WL_SARLIMIT) || defined(WL_SAR_SIMPLE_CONTROL)
static void wlc_phy_set_sarlimit_acphy(phy_type_tpc_ctx_t *ctx);
#endif /* defined(WL_SARLIMIT) || defined(WL_SAR_SIMPLE_CONTROL) */
#ifdef WL_SAR_SIMPLE_CONTROL
static void wlc_phy_nvram_dynamicsarctrl_read(phy_info_t *pi);
static bool wlc_phy_isenabled_dynamic_sarctrl(phy_info_t *pi);
#endif /* WL_SAR_SIMPLE_CONTROL */
static void wlc_acphy_avvmid_txcal(phy_type_tpc_ctx_t *ctx,
	wlc_phy_avvmid_txcal_t *avvmidinfo, bool set);

#ifdef PREASSOC_PWRCTRL
static void wlc_phy_store_tx_pwrctrl_setting_acphy(phy_type_tpc_ctx_t *ctx,
    chanspec_t previous_channel);
static void phy_ac_tpc_shortwindow_upd(phy_type_tpc_ctx_t *ctx, bool new_channel);
static void wlc_phy_pwrctrl_shortwindow_upd_acphy(phy_info_t *pi, bool shortterm);
#endif /* PREASSOC_PWRCTRL */

static uint8 wlc_phy_txpwrctrl_get_target_acphy(phy_info_t *pi, uint8 core);

#ifdef POWPERCHANNL
static void wlc_phy_tx_target_pwr_per_channel_set_acphy(phy_info_t *pi);
#endif /* POWPERCHANNL */
#ifdef BAND5G
static void wlc_phy_txpwr_srom11_read_5Gsubbands(phy_info_t *pi, srom11_pwrdet_t * pwrdet,
	uint8 subband, bool update_rsdb_core1_params, uint8 ant);
#endif /* BAND5G */
static void wlc_phy_txpwr_srom11_read_pwrdet(phy_info_t *pi, srom11_pwrdet_t * pwrdet,
	uint8 param, uint8 band, uint8 offset,  const char * name);
static bool BCMATTACHFN(wlc_phy_txpwr_srom11_read)(phy_type_tpc_ctx_t *ctx);
static bool BCMATTACHFN(wlc_phy_txpwr_srom12_read)(phy_type_tpc_ctx_t *ctx);

static int phy_ac_tpc_txpower_core_offset_set(phy_type_tpc_ctx_t *ctx,
	struct phy_txcore_pwr_offsets *offsets);
static int phy_ac_tpc_txpower_core_offset_get(phy_type_tpc_ctx_t *ctx,
	struct phy_txcore_pwr_offsets *offsets);

static void phy_ac_tpc_nvram_attach(phy_ac_tpc_info_t *tpci);

/* External functions */
#ifdef POWPERCHANNL
void wlc_phy_tx_target_pwr_per_channel_decide_run_acphy(phy_info_t *pi);
void BCMATTACHFN(wlc_phy_tx_target_pwr_per_channel_limit_acphy)(phy_info_t *pi);
#endif /* POWPERCHANNL */
uint8 wlc_phy_ac_set_tssi_params_maj36(phy_info_t *pi);
uint8 wlc_phy_ac_set_tssi_params_majrev40(phy_info_t *pi);
#if (defined(BCMINTERNAL) || defined(WLTEST))
static int phy_ac_tpc_set_pavars(phy_type_tpc_ctx_t *ctx, void* a, void* p);
static int phy_ac_tpc_get_pavars(phy_type_tpc_ctx_t *ctx, void* a, void* p);
#endif /* defined(BCMINTERNAL) || defined(WLTEST) */

/* Register/unregister ACPHY specific implementation to common layer. */
phy_ac_tpc_info_t *
BCMATTACHFN(phy_ac_tpc_register_impl)(phy_info_t *pi, phy_ac_info_t *aci, phy_tpc_info_t *ti)
{
	phy_ac_tpc_info_t *info;
	phy_type_tpc_fns_t fns;

	PHY_TRACE(("%s\n", __FUNCTION__));
	BCM_REFERENCE(rstr_offtgpwr);

	/* allocate all storage in once */
	if ((info = phy_malloc(pi, sizeof(phy_ac_tpc_info_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc failed\n", __FUNCTION__));
		goto fail;
	}
#ifdef PREASSOC_PWRCTRL
	if ((info->pwr_ctrl_save = phy_malloc(pi, sizeof(phy_pwr_ctrl_s))) == NULL) {
		PHY_ERROR(("%s: pwr_ctrl_save malloc failed\n", __FUNCTION__));
		return FALSE;
	}
#endif

	info->pi = pi;
	info->aci = aci;
	info->ti = ti;

	/* register watchdog fn */
	if (phy_wd_add_fn(pi->wdi, phy_ac_tpc_wd, info,
	                  PHY_WD_PRD_1TICK, PHY_WD_1TICK_TPC, PHY_WD_FLAG_DEF_SKIP) != BCME_OK) {
		PHY_ERROR(("%s: phy_wd_add_fn failed\n", __FUNCTION__));
		goto fail;
	}

	/* Register PHY type specific implementation */
	bzero(&fns, sizeof(fns));
	fns.init = phy_ac_tpc_init;
	fns.recalc = phy_ac_tpc_recalc_tgt;
	fns.recalc_target = wlc_phy_txpower_recalc_target_ac_big;
#ifdef PREASSOC_PWRCTRL
	fns.store_setting = wlc_phy_store_tx_pwrctrl_setting_acphy;
	fns.shortwindow_upd = phy_ac_tpc_shortwindow_upd;
#endif /* PREASSOC_PWRCTRL */
	fns.get_sromlimit = phy_ac_tpc_sromlimit_get;
#ifdef PHYCAL_CACHING
#ifdef WLOLPC
	fns.update_cal = phy_ac_tpc_update_olpc_cal;
#endif /* WLOLPC */
#endif /* PHYCAL_CACHING */
#ifdef FCC_PWR_LIMIT_2G
	fns.set_pwr_limit = wlc_phy_fcc_pwr_limit_set_acphy;
#endif /* FCC_PWR_LIMIT_2G */
#if defined(WL_SARLIMIT) || defined(WL_SAR_SIMPLE_CONTROL)
	fns.set_sarlimit = wlc_phy_set_sarlimit_acphy;
#endif /* defined(WL_SARLIMIT) || defined(WL_SAR_SIMPLE_CONTROL) */
	fns.set_avvmid = wlc_acphy_avvmid_txcal;
	fns.get_hwctrl = phy_ac_tpc_hw_ctrl_get;
	fns.set = phy_ac_tpc_set;
	fns.setflags = phy_ac_tpc_set_flags;
	fns.setmax = phy_ac_tpc_set_max;
	fns.txcorepwroffsetset = phy_ac_tpc_txpower_core_offset_set;
	fns.txcorepwroffsetget = phy_ac_tpc_txpower_core_offset_get;
#if (defined(BCMINTERNAL) || defined(WLTEST))
	fns.set_pavars = phy_ac_tpc_set_pavars;
	fns.get_pavars = phy_ac_tpc_get_pavars;
#endif /* defined(BCMINTERNAL) || defined(WLTEST) */
	fns.ctx = info;
	/* set up srom cfg */
	phy_ac_tpc_nvram_attach(info);

#ifdef WL_SAR_SIMPLE_CONTROL
	wlc_phy_nvram_dynamicsarctrl_read(pi);
#endif

	phy_ac_tpc_std_params_attach(info);

	wlc_phy_txpwrctrl_config_acphy(pi);

	/* read srom txpwr limits */
	if (SROMREV(pi->sh->sromrev) >= 12) {
		if (wlc_phy_txpwr_srom12_read(info) != TRUE) {
			PHY_ERROR(("%s: wlc_phy_txpwr_srom12_read failed\n", __FUNCTION__));
			goto fail;
		}
	} else {
		if (wlc_phy_txpwr_srom11_read(info) != TRUE) {
			PHY_ERROR(("%s: wlc_phy_txpwr_srom11_read failed\n", __FUNCTION__));
			goto fail;
		}
	}

	phy_tpc_register_impl(ti, &fns);

	return info;
fail:
	phy_ac_tpc_unregister_impl(info);
	return NULL;
}

void
BCMATTACHFN(phy_ac_tpc_unregister_impl)(phy_ac_tpc_info_t *tpci)
{
	PHY_TRACE(("%s\n", __FUNCTION__));

	if (tpci == NULL) {
		return;
	}

	phy_tpc_unregister_impl(tpci->ti);

#ifdef PREASSOC_PWRCTRL
	if (tpci->pwr_ctrl_save != NULL) {
		phy_mfree(tpci->pi, tpci->pwr_ctrl_save, sizeof(phy_pwr_ctrl_s));
	}
#endif /* PREASSOC_PWRCTRL */
	phy_mfree(tpci->pi, tpci, sizeof(phy_ac_tpc_info_t));
}

static void
BCMATTACHFN(phy_ac_tpc_std_params_attach)(phy_ac_tpc_info_t *tpci)
{
	uint8 core;
	/* Read the offset target power var */
	tpci->offset_targetpwr = (uint16)PHY_GETINTVAR_DEFAULT_SLICE(tpci->pi, rstr_offtgpwr, 0);
#ifdef WL_SAR_SIMPLE_CONTROL
	/* user specified sarlimit by nvram. off as a default */
	tpci->ti->data->cfg.dynamic_sarctrl_2g = 0;
	tpci->ti->data->cfg.dynamic_sarctrl_5g = 0;
	tpci->ti->data->cfg.dynamic_sarctrl_2g_2 = 0;
	tpci->ti->data->cfg.dynamic_sarctrl_5g_2 = 0;
#endif /* WL_SAR_CONTROL_LIMIT */
#ifdef FCC_PWR_LIMIT_2G
	tpci->ti->data->fcc_pwr_limit_2g = FALSE;
#endif /* FCC_PWR_LIMIT_2G */
FOREACH_CORE(tpci->pi, core)
	tpci->txpwrindex_hw_save[core] = 128;
#if (defined(BCMINTERNAL) || defined(WLTEST))
	/* Initialize baseindex override to FALSE */
	tpci->ti->data->ovrinitbaseidx = FALSE;
#endif /* defined (BCMINTERNAL) || defined(WLTEST) */
}

static int
WLBANDINITFN(phy_ac_tpc_init)(phy_type_tpc_ctx_t *ctx)
{
	phy_ac_tpc_info_t *tpci = (phy_ac_tpc_info_t *)ctx;
	uint8 core;

	FOREACH_CORE(tpci->pi, core) {
		tpci->ti->data->base_index_init[core] = 20;
		tpci->ti->data->base_index_cck_init[core] = 20;
#ifdef PREASSOC_PWRCTRL
		tpci->pwr_ctrl_save->status_idx_carry_2g[core] = FALSE;
		tpci->pwr_ctrl_save->status_idx_carry_5g[core] = FALSE;
#endif
	}

	return BCME_OK;
}

void
chanspec_setup_tpc(phy_info_t *pi)
{
	uint8 core;

#if (defined(BCMINTERNAL) ||defined(WLTEST))
	/* Override init base index */
	if ((pi->tpci->data->ovrinitbaseidx) && !ACMAJORREV_32(pi->pubpi->phy_rev) &&
		!ACMAJORREV_33(pi->pubpi->phy_rev) && !ACMAJORREV_37(pi->pubpi->phy_rev)) {
		wlc_phy_txpwr_ovrinitbaseidx(pi);
	}
#endif /* defined(BCMINTERNAL) ||defined(WLTEST) */

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_33(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_37(pi->pubpi->phy_rev)) {
		pi->tpci->data->base_index_init_invalid_frame_cnt = PHY_TOTAL_TX_FRAMES(pi);
		pi->u.pi_acphy->tpci->txpwr_damping_factor_set = 0;
		/* This is the damping value to be written to "TxPwrCtrlDamping.DeltaPwrDamp"
		 * This value is in 1.6 format. So, 0x20 ie 32 corresponds to factor 0.5.
		 * This damping factor is multiplied by the (est pwr - target power) to arrive
		 * at the new index to be used.
		 */
		pi->tpci->data->deltapwrdamp = 0x20;

		/* Set default damping scale factor - 0x40 corresponds to damping factor 1
		 * So, this means default behaviour, no damping.
		 */
		if (pi->tpci->data->tx_pwr_ctrl_damping_en) {
				MOD_PHYREG(pi, TxPwrCtrlDamping, DeltaPwrDamp, 0x40);
		}
	}

	if (!ACMAJORREV_32(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_33(pi->pubpi->phy_rev) &&
	    !ACMAJORREV_37(pi->pubpi->phy_rev)) {
		FOREACH_CORE(pi, core) {
			pi->tpci->data->adjusted_pwr_cap[core] = 127;
		}
	}
}

/* recalc target txpwr and apply to h/w */
static void
phy_ac_tpc_recalc_tgt(phy_type_tpc_ctx_t *ctx)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;

	PHY_TRACE(("%s\n", __FUNCTION__));

	wlc_phy_txpower_recalc_target_acphy(pi);
}

/* TODO: The code could be optimized by moving the common code to phy/cmn */
/* [PHY_RE_ARCH] There are two functions: Bigger function wlc_phy_txpower_recalc_target
 * and smaller function phy_tpc_recalc_tgt which in turn call their phy specific functions
 * which are named in a haphazard manner. This needs to be cleaned up.
 */
static void
wlc_phy_txpower_recalc_target_ac_big(phy_type_tpc_ctx_t *ctx, ppr_t *tx_pwr_target,
    ppr_t *srom_max_txpwr, ppr_t *reg_txpwr_limit, ppr_t *txpwr_targets)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	int8 tx_pwr_max = 0;
	int8 tx_pwr_min = 255;
	uint8 mintxpwr = 0;
	int8 maxpwr = 127;
	uint8 core;
#if (defined(WLTEST) || defined(BCMINTERNAL) || defined(WLPKTENG))
	int16 openloop_pwrctrl_delta;
	bool mac_enabled = FALSE;
	int8 olpc_anchor = 0;
#endif

	chanspec_t chspec = pi->radio_chanspec;
	/* Combine user target, regulatory limit, SROM/HW/board limit and power
	 * percentage to get a tx power target for each rate.
	 */
	FOREACH_CORE(pi, core) {
		/* The user target is the starting point for determining the transmit
		 * power.  If pi->txoverride is true, then use the user target as the
		 * tx power target.
		 */
		ppr_set_cmn_val(tx_pwr_target, info->ti->data->tx_user_target);

#if defined(BCMINTERNAL) || defined(WLTEST) || defined(WL_EXPORT_TXPOWER)
		/* Only allow tx power override for internal or test builds. */
		if (!info->ti->data->txpwroverride)
#endif
		{
			/* Get board/hw limit */
			wlc_phy_txpower_sromlimit((wlc_phy_t *)pi, chspec,
			    &mintxpwr, srom_max_txpwr, core);

			/* Save the Board Limit for Radio Debugability Radar */
			pi->tpci->data->txpwr_max_boardlim_percore[core] =
				ppr_get_max(srom_max_txpwr);

			/* Common Code Start */
			/* Choose minimum of provided regulatory and board/hw limits */
			ppr_compare_min(srom_max_txpwr, reg_txpwr_limit);

			/* Subtract 4 (1.0db) for 4313(IPA) as we are doing PA trimming
			 * otherwise subtract 6 (1.5 db)
			 * to ensure we don't go over
			 * the limit given a noisy power detector.  The result
			 * is now a target, not a limit.
			 */
			ppr_minus_cmn_val(srom_max_txpwr, pi->tx_pwr_backoff);

			/* Choose least of user and now combined regulatory/hw targets */
			ppr_compare_min(tx_pwr_target, srom_max_txpwr);

			/* Board specific fix reduction */

			/* Apply power output percentage */
			if (pi->tpci->data->txpwr_percent < 100)
				ppr_multiply_percentage(tx_pwr_target,
					pi->tpci->data->txpwr_percent);
			/* Common Code End */

			/* Enforce min power and save result as power target.
			 * LCNPHY references off the minimum so this is not appropriate for it.
			 */

			maxpwr = MIN(maxpwr,
				wlc_phy_calc_ppr_pwr_cap_acphy(pi,
				core, ppr_get_max(tx_pwr_target)));
			mintxpwr = wlc_phy_txpwrctrl_update_minpwr_acphy(pi);
			PHY_INFORM(("%s, core %d, maxpwr %d, mintxpwr %d\n",
				__FUNCTION__, core, maxpwr, (int8)mintxpwr));
			if (maxpwr >= (int8)mintxpwr) {
#ifdef WLC_TXCAL
				if (!(pi->olpci->olpc_idx_valid &&
					pi->olpci->olpc_idx_in_use))
#endif
				{
					/* maxpwr is the max power among all rates
					 * (min accross cores)
					 * mintxpwr is the visibility threshold
					 * Rates will be disabled ONLY if some rates
					 * are above visibility thresh and some below.
					 * If all rates are above visi thresh, no OLPC.
					 * If all rates are below visi thresh,
					 *     disable OLPC, dont disable rates.
					 * If LUT txcal based OLPC is used, use OLPC
					 *     but don't disable rates.
					 */
					ppr_force_disabled(tx_pwr_target, mintxpwr);
				}
			}
		}

		tx_pwr_max = ppr_get_max(tx_pwr_target);

		if (tx_pwr_max < (pi->min_txpower * WLC_TXPWR_DB_FACTOR)) {
			tx_pwr_max = pi->min_txpower * WLC_TXPWR_DB_FACTOR;
		}
		tx_pwr_min = ppr_get_min(tx_pwr_target, mintxpwr);

		/* Common Code Start */
		/* Now calculate the tx_power_offset and update the hardware... */
		pi->tx_power_max_per_core[core] = tx_pwr_max;
		pi->tx_power_min_per_core[core] = tx_pwr_min;

#ifdef WLTXPWR_CACHE
		if (wlc_phy_txpwr_cache_is_cached(pi->txpwr_cache, pi->radio_chanspec) == TRUE) {
			wlc_phy_set_cached_pwr_min(pi->txpwr_cache, pi->radio_chanspec, core,
				tx_pwr_min);
			wlc_phy_set_cached_pwr_max(pi->txpwr_cache, pi->radio_chanspec, core,
				tx_pwr_max);
			wlc_phy_set_cached_boardlim(pi->txpwr_cache, pi->radio_chanspec, core,
				pi->tpci->data->txpwr_max_boardlim_percore[core]);
		}
#endif
		pi->openlp_tx_power_min = tx_pwr_min;
		info->ti->data->txpwrnegative = 0;

		PHY_NONE(("wl%d: %s: min %d max %d\n", pi->sh->unit, __FUNCTION__,
		    tx_pwr_min, tx_pwr_max));

		/* determinate the txpower offset by either of the following 2 methods:
		 * txpower_offset = txpower_max - txpower_target OR
		 * txpower_offset = txpower_target - txpower_min
		 * return curpower for last core loop since we are now checking
		 * MIN_cores(MAX_rates(power)) for rate disabling
		 * Only the last core loop info is valid
		 */
		if (core == PHYCORENUM((pi)->pubpi->phy_corenum) - 1) {
			info->ti->data->curpower_display_core =
				PHYCORENUM((pi)->pubpi->phy_corenum) - 1;
			if (txpwr_targets != NULL)
				ppr_copy_struct(tx_pwr_target, txpwr_targets);
		}
		/* Common Code End */

		ppr_cmn_val_minus(tx_pwr_target, pi->tx_power_max_per_core[core]);
		ppr_compare_max(pi->tx_power_offset, tx_pwr_target);

		if (TINY_RADIO(pi)) {
			phy_tpc_cck_corr(pi);
		}
	}	/* CORE loop */

	/* Common Code Start */
#ifdef WLTXPWR_CACHE
	if (wlc_phy_txpwr_cache_is_cached(pi->txpwr_cache, pi->radio_chanspec) == TRUE) {
		wlc_phy_set_cached_pwr(pi->sh->osh, pi->txpwr_cache, pi->radio_chanspec,
			TXPWR_CACHE_POWER_OFFSETS, pi->tx_power_offset);
	}
#endif
	/*
	 * PHY_ERROR(("#####The final power offset limit########\n"));
	 * ppr_mcs_printf(pi->tx_power_offset);
	 */
	ppr_delete(pi->sh->osh, reg_txpwr_limit);
	ppr_delete(pi->sh->osh, tx_pwr_target);
	ppr_delete(pi->sh->osh, srom_max_txpwr);
	/* Common Code End */

#if (defined(WLTEST) || defined(BCMINTERNAL) || defined(WLPKTENG))
	/* for 4360A/B0, when targetPwr is out of the tssi visibility range,
	 * force the power offset to be the delta between the lower bound of visibility
	 * range and the targetPwr
	 */
	if (info->ti->data->txpwroverrideset) {
		if (ACMAJORREV_0(pi->pubpi->phy_rev) ||
		    ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev) ||
		    ACMAJORREV_36(pi->pubpi->phy_rev)) {

			openloop_pwrctrl_delta = wlc_phy_tssivisible_thresh_acphy(pi) -
				info->ti->data->tx_user_target;

			if (openloop_pwrctrl_delta > 0) {
				if (R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC)
					mac_enabled = TRUE;
				ppr_set_cmn_val(pi->tx_power_offset, 0);

				if (CHSPEC_IS2G(pi->radio_chanspec))
					olpc_anchor = pi->olpci->olpc_anchor2g;
				else
					olpc_anchor = pi->olpci->olpc_anchor5g;

				phy_tpc_recalc_tgt(pi->tpci);

				/* Stop PKTENG if already running.. */
				if (!mac_enabled)
					wlapi_enable_mac(pi->sh->physhim);
				wlapi_bmac_pkteng(pi->sh->physhim, 0, 0);
				OSL_DELAY(100);

				/* Turn ON Power Control */
				wlapi_suspend_mac_and_wait(pi->sh->physhim);
				wlc_phy_txpwrctrl_enable_acphy(pi, 1);

				FOREACH_CORE(pi, core) {
					if ((olpc_anchor == 0) || (olpc_anchor
						< pi->olpci->olpc_thresh)) {
						pi->tx_power_max_per_core[core] =
							wlc_phy_tssivisible_thresh_acphy(pi) & 0xff;
					} else {
						pi->tx_power_max_per_core[core]
							= olpc_anchor;
						openloop_pwrctrl_delta = olpc_anchor -
							info->ti->data->tx_user_target;
					}

					/* Set TX Power here for PKTENG */
					wlc_phy_txpwrctrl_set_target_acphy
						(pi, pi->tx_power_max_per_core[core], core);
				}
#ifdef WLC_TXCAL
				/* If table based txcal anchor txidx is used for OLPC, No need to
				 * send out training packets in txpwr ovr mode
				 * Init idx will be set based on cal info from
				 * pi->olpci->olpc_anchor_idx
				 */
				if (!(pi->olpci->olpc_idx_valid && pi->olpci->olpc_idx_in_use))
#endif /* WLC_TXCAL */
				{
					wlapi_enable_mac(pi->sh->physhim);

					/* Start PKTENG to settle TX power Control */
					wlapi_bmac_pkteng(pi->sh->physhim, 1, 100);
					OSL_DELAY(1000);
					if (!mac_enabled)
						wlapi_suspend_mac_and_wait(pi->sh->physhim);
					OSL_DELAY(100);
				}

				/* Toggle Power Control to save off base index */
				wlc_phy_txpwrctrl_enable_acphy(pi, 0);
				if (openloop_pwrctrl_delta > 127) {
					openloop_pwrctrl_delta = 127;
				} else if (openloop_pwrctrl_delta < -128) {
					openloop_pwrctrl_delta = -128;
				}
				ppr_set_cmn_val(pi->tx_power_offset,
					(int8) openloop_pwrctrl_delta);
#ifdef WLTXPWR_CACHE
				wlc_phy_txpwr_cache_invalidate(pi->txpwr_cache);
#endif  /* WLTXPWR_CACHE */
				PHY_NONE(("###offset: %d targetPwr %d###\n",
					openloop_pwrctrl_delta,
					pi->tx_power_max_per_core[0]));
			}
		}
		info->ti->data->txpwroverrideset = FALSE;
	}
#endif /* (WLTEST || BCMINTERNAL || PKTENG) */
}

#ifdef WL_SAR_SIMPLE_CONTROL
static void
BCMATTACHFN(wlc_phy_nvram_dynamicsarctrl_read)(phy_info_t *pi)
{
/* Nvram parameter to get sarlimits customized by user
 * Value interpetation:
 *  dynamicsarctrl_2g = 0x[core3][core2][core1][core0]
 * each core# has the bitmask followings:
 * 8th bit : 0 - sarlimit enable / 1 - sarlimit disable
 * 0 ~ 7 bits : qdbm power val (0x7f as a maxumum)
 */
	char phy_var_name[20];
	phy_tpc_data_t *data = pi->tpci->data;
	(void)snprintf(phy_var_name, sizeof(phy_var_name), "dynamicsarctrl_2g");
	if ((PHY_GETVAR_SLICE(pi, phy_var_name)) != NULL) {
		data->cfg.dynamic_sarctrl_2g = (uint32)PHY_GETINTVAR_SLICE(pi, phy_var_name);
	}

	(void)snprintf(phy_var_name, sizeof(phy_var_name), "dynamicsarctrl_5g");
	if ((PHY_GETVAR_SLICE(pi, phy_var_name)) != NULL) {
		data->cfg.dynamic_sarctrl_5g = (uint32)PHY_GETINTVAR_SLICE(pi, phy_var_name);
	}

	(void)snprintf(phy_var_name, sizeof(phy_var_name), "dynamicsarctrl_2g_2");
	if ((PHY_GETVAR_SLICE(pi, phy_var_name)) != NULL) {
		data->cfg.dynamic_sarctrl_2g_2 = (uint32)PHY_GETINTVAR_SLICE(pi, phy_var_name);
	} else {
		data->cfg.dynamic_sarctrl_2g_2 = data->cfg.dynamic_sarctrl_2g;
	}

	(void)snprintf(phy_var_name, sizeof(phy_var_name), "dynamicsarctrl_5g_2");
	if ((PHY_GETVAR_SLICE(pi, phy_var_name)) != NULL) {
		data->cfg.dynamic_sarctrl_5g_2 = (uint32)PHY_GETINTVAR_SLICE(pi, phy_var_name);
	} else {
		data->cfg.dynamic_sarctrl_5g_2 = data->cfg.dynamic_sarctrl_5g;
	}
}

static bool
wlc_phy_isenabled_dynamic_sarctrl(phy_info_t *pi)
{
	uint core;

	/* WLC_TXPWR_MAX value means SAR disabled */
	for (core = 0; core < PHY_CORE_MAX; core++) {
		if (pi->tpci->data->sarlimit[core] != WLC_TXPWR_MAX) {
			return TRUE;
		}
	}
	return FALSE;
}
#endif /* WL_SAR_SIMPLE_CONTROL */

static bool
phy_ac_tpc_wd(phy_wd_ctx_t *ctx)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	bool suspend = FALSE;
#if defined(WLC_TXCAL)
	int16 txcal_delta_temp;
	int16 txcal_currtemp = 0;
#endif  /* WLC_TXCAL */

#if defined(PREASSOC_PWRCTRL)
	/* Suspend MAC if haven't done so */
	wlc_phy_conditional_suspend(pi, &suspend);
	phy_ac_tpc_shortwindow_upd(ctx, FALSE);
	/* Resume MAC */
	wlc_phy_conditional_resume(pi, &suspend);
#endif /* PREASSOC_PWRCTRL */

	if (pi->tpci->data->tx_pwr_ctrl_damping_en && !info->txpwr_damping_factor_set &&
		(PHY_TOTAL_TX_FRAMES(pi) -
			pi->tpci->data->base_index_init_invalid_frame_cnt >= 150)) {
		suspend = !(R_REG(pi->sh->osh, &pi->regs->maccontrol) & MCTL_EN_MAC);
		if (!suspend)
			wlapi_suspend_mac_and_wait(pi->sh->physhim);
		MOD_PHYREG(pi, TxPwrCtrlDamping, DeltaPwrDamp, pi->tpci->data->deltapwrdamp);
		if (!suspend)
			wlapi_enable_mac(pi->sh->physhim);
		info->txpwr_damping_factor_set = 1;
	}

#if defined(WLC_TXCAL)
	if (pi->olpci->olpc_idx_in_use && pi->olpci->olpc_idx_valid) {
		wlapi_suspend_mac_and_wait(pi->sh->physhim);
		/* Recalculate baseindex if temperature changed for txcal based OLPC */
		txcal_currtemp = wlc_phy_tempsense_acphy(pi);
		txcal_delta_temp =
			(txcal_currtemp > pi->olpci->txcal_olpc_last_calc_temp)?
			txcal_currtemp - pi->olpci->txcal_olpc_last_calc_temp:
			pi->olpci->txcal_olpc_last_calc_temp - txcal_currtemp;
		if (txcal_delta_temp >= TXCAL_OLPC_RECALC_TEMP) {
			wlc_phy_txcal_olpc_idx_recal_acphy(pi, TRUE);
			PHY_TXPWR(("%s TXCAL_OLPC: temp change exceed thresh\n",
				__FUNCTION__));
			pi->olpci->txcal_olpc_last_calc_temp = txcal_currtemp;
		}
		wlapi_enable_mac(pi->sh->physhim);
	}
#endif  /* WLC_TXCAL */

	return TRUE;
}

int16
wlc_phy_calc_adjusted_cap_rgstr_acphy(phy_info_t *pi, uint8 core)
{
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
#ifdef WL_SARLIMIT
	return MIN(pi->tpci->data->sarlimit[core],
		pi->tx_power_max_per_core[core] + pi_ac->tpci->txpwr_offset[core]);
#else
	return pi->tx_power_max_per_core[core] + pi_ac->tpci->txpwr_offset[core];
#endif /* WL_SARLIMIT */
}


/* ********************************************* */
/*				Internal Definitions					*/
/* ********************************************* */
#define PWRCTRL_SHORTW_AVG 1
#define PWRCTRL_LONGW_AVG 4
#define PWRCTRL_MIN_INIT_IDX 5
#define PWRCTRL_MAX_INIT_IDX 127
#define SAMPLE_TSSI_AFTER_100_SAMPLES 100
#define SAMPLE_TSSI_AFTER_110_SAMPLES 110
#define SAMPLE_TSSI_AFTER_111_SAMPLES 111
#define SAMPLE_TSSI_AFTER_115_SAMPLES 115
#define SAMPLE_TSSI_AFTER_150_SAMPLES 150
#define SAMPLE_TSSI_AFTER_160_SAMPLES 160
#define SAMPLE_TSSI_AFTER_170_SAMPLES 170
#define SAMPLE_TSSI_AFTER_185_SAMPLES 185
#define SAMPLE_TSSI_AFTER_200_SAMPLES 200
#define SAMPLE_TSSI_AFTER_220_SAMPLES 220
#define SAMPLE_TSSI_AFTER_190_SAMPLES 190


#define ACPHY_TBL_ID_ESTPWRLUTS(core)	\
	(((core == 0) ? ACPHY_TBL_ID_ESTPWRLUTS0 : \
	((core == 1) ? ACPHY_TBL_ID_ESTPWRLUTS1 : \
	((core == 2) ? ACPHY_TBL_ID_ESTPWRLUTS2 : ACPHY_TBL_ID_ESTPWRLUTS3))))

static void
wlc_phy_txpwrctrl_pwr_setup_acphy(phy_info_t *pi)
{
#define ESTPWRLUTS_TBL_LEN		128
#define ESTPWRLUTS_TBL_OFFSET		0
#define ESTPWRSHFTLUTS_TBL_LEN		24
#define ESTPWRSHFTLUTS_TBL_OFFSET	0

	uint8 stall_val;
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;
	phy_ac_tpc_info_t *ti = pi_ac->tpci;
	srom11_pwrdet_t *pwrdet = pi->pwrdet_ac;
	int8   target_min_limit;
	int16  a1[PAPARAM_SET_NUM], b0[PAPARAM_SET_NUM], b1[PAPARAM_SET_NUM];
	uint8  chan_freq_range, iidx, core_freq_segment_map;
	uint8  core, core2range, idx_set[2], k;
	int32  pwr_est;
	uint32 idx;
	uint16 regval[ESTPWRLUTS_TBL_LEN];
	uint32 shfttblval[ESTPWRSHFTLUTS_TBL_LEN];
	uint8  tssi_delay;
	uint32 pdoffs = 0;
	uint8  mult_mode = 1;
	bool flag2rangeon;
	int8 targetidx, tx_idx;
#ifdef PREASSOC_PWRCTRL
	bool init_idx_carry_from_lastchan;
	uint8 step_size, prev_target_qdbm;
#endif
	struct _tp_qtrdbm {
		uint8 core;
		int8 target_pwr_qtrdbm;
	} tp_qtrdbm_each_core[PHY_CORE_MAX]; /* TP for each core */
	uint core_count = 0;

#if defined(PHYCAL_CACHING)
	ch_calcache_t *ctx = NULL;
	ctx = wlc_phy_get_chanctx(pi, pi->radio_chanspec);
	BCM_REFERENCE(ctx);
#endif /* PHYCAL_CACHING */
	iidx = 0;

#ifdef WLC_TXCAL
	pi->txcali->txcal_status = 0;
#endif	/* WLC_TXCAL */

	flag2rangeon =
		((CHSPEC_IS2G(pi->radio_chanspec) && pi->tpci->data->cfg.srom_tworangetssi2g) ||
		(CHSPEC_IS5G(pi->radio_chanspec) && pi->tpci->data->cfg.srom_tworangetssi5g)) &&
		PHY_IPA(pi);
	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));
	PHY_CHANLOG(pi, __FUNCTION__, TS_ENTER, 0);

	if (BF3_TSSI_DIV_WAR(pi->u.pi_acphy) && ACMAJORREV_4(pi->pubpi->phy_rev)) {
		/* DIV_WAR is priority between DIV WAR & two range */
		flag2rangeon = 0;
	}
	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_REG_LIST_START
		ACPHY_DISABLE_STALL_ENTRY(pi)
		/* enable TSSI */
		MOD_PHYREG_ENTRY(pi, TSSIMode, tssiEn, 1)
		MOD_PHYREG_ENTRY(pi, TxPwrCtrlCmd, txPwrCtrl_en, 0)
	ACPHY_REG_LIST_EXECUTE(pi);

	/* Initialize ALL PA Param arrays a1, b0, b1 to be all zero */
	for (idx = 0; idx < PAPARAM_SET_NUM; idx++) {
		a1[idx] = 0;
		b0[idx] = 0;
		b1[idx] = 0;
	}

	if (RADIOREV(pi->pubpi->radiorev) == 4 || RADIOREV(pi->pubpi->radiorev) == 8 ||
		ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev) ||
		ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_40(pi->pubpi->phy_rev) ||
		IS_4364_1x1(pi)) {
		/* 4360B0/B1/4350 using 0.5dB-step gaintbl, bbmult interpolation enabled */
		MOD_PHYREG(pi, TxPwrCtrlCmd, bbMultInt_en, 1);
#ifdef PREASSOC_PWRCTRL
		step_size = 2;
#endif
	} else if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		/* correct check should be use bbMultInt_en=1 if 0.5 dB gain table
		 * bbMultInt_en=0 if 0.25 dB gain table
		 */
		if (ROUTER_4349(pi)) {
			MOD_PHYREG(pi, TxPwrCtrlCmd, bbMultInt_en, !(pi_ac->is_p25TxGainTbl));
		} else {
			if (PHY_IPA(pi)) {
			        MOD_PHYREG(pi, TxPwrCtrlCmd, bbMultInt_en, 0);
			} else {
			        MOD_PHYREG(pi, TxPwrCtrlCmd, bbMultInt_en, 1);
			}
		}
#ifdef PREASSOC_PWRCTRL
		step_size = 2;
#endif
	} else {
	/* disable bbmult interpolation
	   to work with a 0.25dB step txGainTbl
	*/
		MOD_PHYREG(pi, TxPwrCtrlCmd, bbMultInt_en, 0);
#ifdef PREASSOC_PWRCTRL
		step_size = 1;
#endif
	}

	FOREACH_CORE(pi, core) {
		core_freq_segment_map = pi->u.pi_acphy->core_freq_mapping[core];

		/* Get pwrdet params from SROM for current subband */
		chan_freq_range = phy_ac_chanmgr_get_chan_freq_range(pi, 0, core_freq_segment_map);
		/* Check if 2 range should be activated for band */
		/* First load PA Param sets for corresponding band/frequemcy range */
		/* for all cores 0 to PHYNUMCORE()-1 */
		switch (chan_freq_range) {
		case WL_CHAN_FREQ_RANGE_2G:
		case WL_CHAN_FREQ_RANGE_5G_BAND0:
		case WL_CHAN_FREQ_RANGE_5G_BAND1:
		case WL_CHAN_FREQ_RANGE_5G_BAND2:
		case WL_CHAN_FREQ_RANGE_5G_BAND3:
			a1[core] =  pwrdet->pwrdet_a1[core][chan_freq_range];
			b0[core] =  pwrdet->pwrdet_b0[core][chan_freq_range];
			b1[core] =  pwrdet->pwrdet_b1[core][chan_freq_range];
			PHY_TXPWR(("wl%d: %s: pwrdet core%d: a1=%d b0=%d b1=%d\n",
				pi->sh->unit, __FUNCTION__, core,
				a1[core], b0[core], b1[core]));
			break;
		}

		/* Set cck pwr offset from nvram */
		if (CHSPEC_IS2G(pi->radio_chanspec) && (TINY_RADIO(pi) ||
				ACMAJORREV_36(pi->pubpi->phy_rev))) {
			if (CHSPEC_BW_LE20(pi->radio_chanspec) &&
				!CHSPEC_IS20(pi->radio_chanspec)) {
				int16 cckulbPwrOffset = pi->tpci->data->cfg.cckulbpwroffset[core];

				if (READ_PHYREGFLD(pi, perPktIdleTssiCtrlcck,
					base_index_cck_en) != 1) {
					cckulbPwrOffset -= pi->sromi->cckPwrIdxCorr;
				}

				MOD_PHYREGCEE(pi, TxPwrCtrlTargetPwr_path, core,
				              cckPwrOffset, cckulbPwrOffset);
			} else {
				int16 cckPwrOffset = pi->tpci->data->cfg.cckpwroffset[core];

				if (READ_PHYREGFLD(pi, perPktIdleTssiCtrlcck,
					base_index_cck_en) != 1) {
					cckPwrOffset -= pi->sromi->cckPwrIdxCorr;
				}

				MOD_PHYREGCEE(pi, TxPwrCtrlTargetPwr_path, core,
				              cckPwrOffset, cckPwrOffset);

			}
		}
		/* Next if special consitions are met, load additional PA Param sets */
		/* for corresponding band/frequemcy range */
		if (flag2rangeon || (BF3_TSSI_DIV_WAR(pi_ac) &&
		    (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_4(pi->pubpi->phy_rev)))) {
			/* If two-range TSSI scheme is enabled via flag2rangeon, */
			/* or if TSSI divergence WAR is enable for 4350, load PHYCORENUM() */
			/* additional PA Param sets for corresponding band/frequemcy range. */
			/* For 4350, extra PA Params are used for CCK in 2G band or */
			/* for 40/80 MHz bands in 5G band */
			if ((phy_get_phymode(pi) == PHYMODE_RSDB) &&
				ACMAJORREV_4(pi->pubpi->phy_rev)) {
				core2range = TSSI_DIVWAR_INDX;
			} else {
				core2range = PHYCORENUM(pi->pubpi->phy_corenum) + core;
			}
			ASSERT(core2range < PAPARAM_SET_NUM);
			a1[core2range] = pwrdet->pwrdet_a1[core2range][chan_freq_range];
			b0[core2range] = pwrdet->pwrdet_b0[core2range][chan_freq_range];
			b1[core2range] = pwrdet->pwrdet_b1[core2range][chan_freq_range];

			PHY_TXPWR(("wl%d: %s: pwrdet %s core%d: a1=%d b0=%d b1=%d\n",
			           pi->sh->unit, __FUNCTION__,
			           (flag2rangeon) ? "2nd-TSSI" : "CCK/40/80MHz",
			           core, a1[core2range], b0[core2range], b1[core2range]));
		} else if (BF3_TSSI_DIV_WAR(pi_ac) && ACMAJORREV_1(pi->pubpi->phy_rev)) {
			/* If TSSI divergence WAR is enable for 4335, */
			/* use core1 and core2 paparams for 40Mhz and 40/80 paparams. */
			a1[1] =	pwrdet->pwrdet_a1[1][chan_freq_range];
			b0[1] =	pwrdet->pwrdet_b0[1][chan_freq_range];
			b1[1] =	pwrdet->pwrdet_b1[1][chan_freq_range];
			PHY_TXPWR(("wl%d: %s: pwrdet 40mhz case: %d: a1=%d b0=%d b1=%d\n",
				pi->sh->unit, __FUNCTION__, 1,
				a1[1], b0[1], b1[1]));

			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				a1[2] =	pwrdet->pwrdet_a1[2][chan_freq_range];
				b0[2] =	pwrdet->pwrdet_b0[2][chan_freq_range];
				b1[2] =	pwrdet->pwrdet_b1[2][chan_freq_range];
				PHY_TXPWR(("wl%d: %s: pwrdet 80mhz case: %d: a1=%d b0=%d b1=%d\n",
					pi->sh->unit, __FUNCTION__, 2,
					a1[2], b0[2], b1[2]));
			}
		}
	}

	/* target power */
	wlc_phy_txpwrctrl_update_minpwr_acphy(pi);
	target_min_limit = pi->min_txpower * WLC_TXPWR_DB_FACTOR;

	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		int8 target_pwr_qtrdbm;
		target_pwr_qtrdbm = (int8)pi->tx_power_max_per_core[core];
		/* never target below the min threashold */
		if (target_pwr_qtrdbm < target_min_limit)
			target_pwr_qtrdbm = target_min_limit;

		if (ACMAJORREV_1(pi->pubpi->phy_rev) || ACMAJORREV_2(pi->pubpi->phy_rev)) {
			int16  tssifloor;

			chan_freq_range = phy_ac_chanmgr_get_chan_freq_range(pi,
				0, PRIMARY_FREQ_SEGMENT);
			tssifloor = (int16)pwrdet->tssifloor[core][chan_freq_range];
			if ((tssifloor != 0x3ff) && (tssifloor != 0)) {
				uint8 maxpwr = wlc_phy_set_txpwr_clamp_acphy(pi, core);
				if (maxpwr < target_pwr_qtrdbm) {
					target_pwr_qtrdbm = maxpwr;
				}
			}
		}

		tp_qtrdbm_each_core[core_count].core = core;
		tp_qtrdbm_each_core[core_count].target_pwr_qtrdbm = target_pwr_qtrdbm;
		++core_count;
	        /* PHY_ERROR(("####targetPwr: %d#######\n",
	         * tp_qtrdbm_each_core[core_count].target_pwr_qtrdbm));
		 */
	}

	/* determine pos/neg TSSI slope */
	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		MOD_PHYREG(pi, TSSIMode, tssiPosSlope, pi->fem2g->tssipos);
	} else {
		MOD_PHYREG(pi, TSSIMode, tssiPosSlope, pi->fem5g->tssipos);
	}
	MOD_PHYREG(pi, TSSIMode, tssiPosSlope, 1);

	/* disable txpwrctrl during idleTssi measurement, etc */
	MOD_PHYREG(pi, TxPwrCtrlCmd, txPwrCtrl_en, 0);
	if (flag2rangeon) {
		MOD_PHYREG(pi, TxPwrCtrlPwrRange2, maxPwrRange2, 127);
		MOD_PHYREG(pi, TxPwrCtrlPwrRange2, minPwrRange2, 44);
	} else {
		MOD_PHYREG(pi, TxPwrCtrlPwrRange2, maxPwrRange2, 0);
		MOD_PHYREG(pi, TxPwrCtrlPwrRange2, minPwrRange2, 1);
	}

#ifdef PREASSOC_PWRCTRL
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		init_idx_carry_from_lastchan = (CHSPEC_IS5G(pi->radio_chanspec)) ?
		        ti->pwr_ctrl_save->status_idx_carry_5g[core]
		        : ti->pwr_ctrl_save->status_idx_carry_2g[core];
		if (!init_idx_carry_from_lastchan) {
			/* 4360B0 using 0.5dB-step gaintbl so start with a lower starting idx */
			if (RADIOID(pi->pubpi->radioid) == BCM2069_ID &&
			    ((RADIOREV(pi->pubpi->radiorev) == 4) ||
				(RADIOREV(pi->pubpi->radiorev) == 7) ||
			    (RADIOREV(pi->pubpi->radiorev) == 8) ||
				(RADIOREV(pi->pubpi->radiorev) == 11) ||
				(RADIOREV(pi->pubpi->radiorev) == 13) ||
			    ACMAJORREV_5(pi->pubpi->phy_rev))) {
				iidx = 20;
			} else {
				iidx = 50;
			}
#if defined(WLC_TXCAL)
			if (pi->olpci->olpc_idx_in_use && pi->olpci->olpc_idx_valid) {
				wlc_phy_olpc_idx_tempsense_comp_acphy(pi, &iidx, core);
			}
#endif /* WLC_TXCAL */
			MOD_PHYREGCEE(pi, TxPwrCtrlInit_path, core, pwrIndex_init_path, iidx);
			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				ti->pwr_ctrl_save->status_idx_carry_5g[core] = TRUE;
			} else {
				ti->pwr_ctrl_save->status_idx_carry_2g[core] = TRUE;
			}
			ti->txpwrindex_hw_save[core] = 128;
		} else {
		/* set power index initial condition */
			int32 new_iidx;

			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				iidx = ti->pwr_ctrl_save->status_idx_5g[core];
				prev_target_qdbm = ti->pwr_ctrl_save->pwr_qdbm_5g[core];
				if ((ti->pwr_ctrl_save->stored_not_restored_5g[core])) {
					ti->pwr_ctrl_save->stored_not_restored_5g[core] = FALSE;
					ti->txpwrindex_hw_save[core] = 128;
				}
			} else {
				iidx = ti->pwr_ctrl_save->status_idx_2g[core];
				prev_target_qdbm = ti->pwr_ctrl_save->pwr_qdbm_2g[core];
				if ((ti->pwr_ctrl_save->stored_not_restored_2g[core])) {
					ti->pwr_ctrl_save->stored_not_restored_2g[core] = FALSE;
					ti->txpwrindex_hw_save[core] = 128;

				}
			}
			new_iidx = (int32)iidx + ((int32)tp_qtrdbm_each_core[core].target_pwr_qtrdbm
			          - prev_target_qdbm) / step_size;

			if (new_iidx < PWRCTRL_MIN_INIT_IDX) {
				iidx = PWRCTRL_MIN_INIT_IDX;
			} else if (new_iidx > PWRCTRL_MAX_INIT_IDX) {
				iidx = PWRCTRL_MAX_INIT_IDX;
			} else {
				iidx = (uint8)new_iidx;
			}
			if (!pi->tpci->data->ovrinitbaseidx) {
				MOD_PHYREGCEE(pi, TxPwrCtrlInit_path, core, pwrIndex_init_path,
					iidx);
				if (!PHY_IPA(pi) && ACMAJORREV_4(pi->pubpi->phy_rev)) {
				/* JIRA: SW4349-1379
				 * FOR ePA chips, to improve CCK spectral mask margins, bphy scale
				 * is increased to 0x5F. this improves SM margins by >2dB, with an
				 * acceptable degradation in the EVM. This change also mandates the
				 * use of separate loop for cck TPC (target power control)
				 */
					MOD_PHYREGCEE(pi, TxPwrCtrlInit_path, core,
						pwr_index_init_cck_path, iidx);
				}
			}
		}
	}
#else
	if ((RADIOREV(pi->pubpi->radiorev) == 4) ||
	    (RADIOREV(pi->pubpi->radiorev) == 8) ||
		(RADIOREV(pi->pubpi->radiorev) == 13) ||
	    (ACMAJORREV_5(pi->pubpi->phy_rev))) {
		iidx = 20;
	} else {
		iidx = 50;
	}
#if defined(PHYCAL_CACHING)
	if (!ctx || !ctx->valid)
#endif /* PHYCAL_CACHING */
	{
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
#if defined(WLC_TXCAL)
			if (pi->olpci->olpc_idx_valid && pi->olpci->olpc_idx_in_use) {
				wlc_phy_olpc_idx_tempsense_comp_acphy(pi,
					&pi->tpci->data->base_index_init[core], core);
				if (TINY_RADIO(pi)) {
					wlc_phy_olpc_idx_tempsense_comp_acphy(pi,
						&pi->tpci->data->base_index_cck_init[core], core);
				}
			}
#endif /* WLC_TXCAL */
			if (!pi->tpci->data->ovrinitbaseidx) {
				MOD_PHYREGCEE(pi, TxPwrCtrlInit_path, core,
					pwrIndex_init_path, iidx);
				if (!PHY_IPA(pi) && ACMAJORREV_4(pi->pubpi->phy_rev)) {
				/* JIRA: SW4349-1379
				 * FOR ePA chips, to improve CCK spectral mask margins, bphy scale
				 * is increased to 0x5F. this improves SM margins by >2dB, with an
				 * acceptable degradation in the EVM. This change also mandates the
				 * use of separate loop for cck TPC (target power control)
				 */
					MOD_PHYREGCEE(pi, TxPwrCtrlInit_path, core,
						pwr_index_init_cck_path, iidx);
				}
			}
		}
	}

#endif /* PREASSOC_PWRCTRL */
	/* MOD_PHYREG(pi, TxPwrCtrlIdleTssi, rawTssiOffsetBinFormat, 1); */

	/* sample TSSI at 7.5us */
	if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		if (BOARDFLAGS(GENERIC_PHY_INFO(pi)->boardflags) & BFL_TSSIAVG) {
			tssi_delay = SAMPLE_TSSI_AFTER_150_SAMPLES;
			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, Ntssi_intg_log2, 4)
				MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, tssi_accum_en, 1)
				MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, tssi_filter_pos, 1)
			ACPHY_REG_LIST_EXECUTE(pi);
			MOD_PHYREG(pi, TssiAccumCtrl, Ntssi_accum_delay, tssi_delay);
		} else {
			if (PHY_IPA(pi)) {
				tssi_delay = SAMPLE_TSSI_AFTER_170_SAMPLES;
			} else {
				if (CHSPEC_IS2G(pi->radio_chanspec)) {
					if (pi->tpci->data->cfg.srom_2g_pdrange_id == 21) {
						tssi_delay = SAMPLE_TSSI_AFTER_200_SAMPLES;
					} else {
						tssi_delay = SAMPLE_TSSI_AFTER_150_SAMPLES;
					}
				} else {
					if (pi->tpci->data->cfg.srom_5g_pdrange_id == 23) {
						tssi_delay = SAMPLE_TSSI_AFTER_200_SAMPLES;
					} else {
						tssi_delay = SAMPLE_TSSI_AFTER_150_SAMPLES;
					}
				}
			}
		}
	} else if ((ACMAJORREV_1(pi->pubpi->phy_rev) && ACMINORREV_2(pi)) || TINY_RADIO(pi)) {
		uint8  tssi_delay_cck;

		tssi_delay = SAMPLE_TSSI_AFTER_150_SAMPLES;
		if (TINY_RADIO(pi)) {
			if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
				tssi_delay = SAMPLE_TSSI_AFTER_185_SAMPLES;
				MOD_PHYREG(pi, perPktIdleTssiCtrl, perPktIdleTssiUpdate_en, 0);
				/* Enable the TwoPowerRange flag so that multi_mode
				TPC works correctly
				*/
				if (BF3_TSSI_DIV_WAR(pi_ac) || flag2rangeon)
					MOD_PHYREG(pi, TSSIMode, TwoPwrRange, 1);
				else
					MOD_PHYREG(pi, TSSIMode, TwoPwrRange, 0);
				if (!flag2rangeon) {
					MOD_PHYREG(pi, TxPwrCtrlPwrRange2, maxPwrRange2, 127);
					MOD_PHYREG(pi, TxPwrCtrlPwrRange2, minPwrRange2, -128);
				}
				if (CHSPEC_IS5G(pi->radio_chanspec) &&
					(BF3_TSSI_DIV_WAR(pi_ac))) {
					mult_mode = 4;
				} else {
					mult_mode = 1;
				}
				FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
					if (core == 0) {
						MOD_PHYREG(pi, TxPwrCtrl_Multi_Mode0,
						multi_mode, mult_mode);
					} else if (core == 1) {
						MOD_PHYREG(pi, TxPwrCtrl_Multi_Mode1,
						multi_mode, mult_mode);
					}
				}
			} else {
				tssi_delay = SAMPLE_TSSI_AFTER_100_SAMPLES;
			}
			if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			    ACMAJORREV_33(pi->pubpi->phy_rev) ||
			    ACMAJORREV_37(pi->pubpi->phy_rev)) {
				tssi_delay_cck =  SAMPLE_TSSI_AFTER_115_SAMPLES;
			} else {
				tssi_delay_cck =  SAMPLE_TSSI_AFTER_220_SAMPLES;
			}
		} else {
			tssi_delay = 150;
			tssi_delay_cck = 0;
		}

		if (PHY_IPA(pi) && !(TINY_RADIO(pi))) {		/* this is for 4335C0 iPA */
			tssi_delay = SAMPLE_TSSI_AFTER_170_SAMPLES;
		} else {
			/* Enable tssi accum for C0. also change the tssi digi filter position. */
			/* this helps to reduce the tssi noise. */
			MOD_PHYREG(pi, TssiAccumCtrl, Ntssi_accum_delay, tssi_delay);
			ACPHY_REG_LIST_START
				MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, Ntssi_intg_log2, 4)
				MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, tssi_accum_en, 1)
				MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, tssi_filter_pos, 1)
			ACPHY_REG_LIST_EXECUTE(pi);
			if (TINY_RADIO(pi)) {
				MOD_PHYREG(pi, TssiAccumCtrlcck, Ntssi_accum_delay_cck,
					tssi_delay_cck);
				MOD_PHYREG(pi, TssiAccumCtrlcck, Ntssi_intg_log2_cck, 1);
			}
		}
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		tssi_delay = wlc_phy_ac_set_tssi_params_maj36(pi);
	} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		tssi_delay = wlc_phy_ac_set_tssi_params_majrev40(pi);
	} else {
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			if (pi->tpci->data->cfg.srom_2g_pdrange_id >= 5) {
				tssi_delay = SAMPLE_TSSI_AFTER_200_SAMPLES;
			} else if (pi->tpci->data->cfg.srom_2g_pdrange_id >= 4) {
				tssi_delay = SAMPLE_TSSI_AFTER_220_SAMPLES;
			} else {
				tssi_delay = SAMPLE_TSSI_AFTER_150_SAMPLES;
			}
		} else {
			if (pi->tpci->data->cfg.srom_5g_pdrange_id >= 5) {
				tssi_delay = SAMPLE_TSSI_AFTER_200_SAMPLES;
			} else if (pi->tpci->data->cfg.srom_5g_pdrange_id >= 4) {
				tssi_delay = SAMPLE_TSSI_AFTER_220_SAMPLES;
			} else {
				tssi_delay = SAMPLE_TSSI_AFTER_150_SAMPLES;
			}
		}
	}
	MOD_PHYREG(pi, TxPwrCtrlNnum, Ntssi_delay, tssi_delay);
	if (pi->tpci->data->cfg.bphy_scale != 0) {
		MOD_PHYREG(pi, BphyControl3, bphyScale20MHz, pi->tpci->data->cfg.bphy_scale);
	}

#if defined(PREASSOC_PWRCTRL)
	/* average over 2 or 16 packets */
	wlc_phy_pwrctrl_shortwindow_upd_acphy(pi, pi->tpci->data->channel_short_window);
#else
	MOD_PHYREG(pi, TxPwrCtrlNnum, Npt_intg_log2, PWRCTRL_LONGW_AVG);
#endif /* PREASSOC_PWRCTRL */

	/* decouple IQ comp and LOFT comp from Power Control */
	MOD_PHYREG(pi, TxPwrCtrlCmd, use_txPwrCtrlCoefsIQ, 0);
	if (ACREV_IS(pi->pubpi->phy_rev, 1) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
		if ((CHSPEC_IS5G(pi->radio_chanspec) &&
		    (pi->sromi->epa_on_during_txiqlocal) &&
		    !(pi->sromi->precal_tx_idx)) ||
		    (pi->sh->boardtype == BCM94360MCM5)) {
			/* Intended to address: JIRA CRDOT11ACPHY-658 */
			MOD_PHYREG(pi, TxPwrCtrlCmd, use_txPwrCtrlCoefsLO, 0);
		} else {
			MOD_PHYREG(pi, TxPwrCtrlCmd, use_txPwrCtrlCoefsLO, 1);
		}

	} else {
		MOD_PHYREG(pi, TxPwrCtrlCmd, use_txPwrCtrlCoefsLO, 0);
	}

	/* adding maxCap for each Tx chain */
	if (0) {
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			if (core == 0) {
				MOD_PHYREG(pi, TxPwrCapping_path0, maxTxPwrCap_path0, 80);
			} else if (core == 1) {
				MOD_PHYREG(pi, TxPwrCapping_path1, maxTxPwrCap_path1, 32);
			} else if (core == 2) {
				MOD_PHYREG(pi, TxPwrCapping_path2, maxTxPwrCap_path2, 32);
			}
		}
	}
#ifdef WL_SARLIMIT
	wlc_phy_set_sarlimit_acphy(ti);
#endif
	while (core_count > 0) {
		--core_count;
		if (ti->offset_targetpwr) {
			uint8 tgt_pwr_qdbm = tp_qtrdbm_each_core[core_count].target_pwr_qtrdbm;
			tgt_pwr_qdbm -= (ti->offset_targetpwr << 2);
			wlc_phy_txpwrctrl_set_target_acphy(pi,
				tgt_pwr_qdbm,
				0);
		} else {
			/* set target powers */
			wlc_phy_txpwrctrl_set_target_acphy(pi,
				tp_qtrdbm_each_core[core_count].target_pwr_qtrdbm,
				tp_qtrdbm_each_core[core_count].core);
		}
		PHY_TXPWR(("wl%d: %s: txpwrctl[%d]: %d\n",
			pi->sh->unit, __FUNCTION__, tp_qtrdbm_each_core[core_count].core,
		              tp_qtrdbm_each_core[core_count].target_pwr_qtrdbm));
	}
#ifdef ENABLE_FCBS
	if (IS_FCBS(pi) && pi->phy_fcbs->FCBS_INPROG)
		pi->phy_fcbs->FCBS_INPROG = 0;
	else {
#endif
	/* load estimated power tables (maps TSSI to power in dBm)
	 *    entries in tx power table 0000xxxxxx
	 */

	if (BF3_TSSI_DIV_WAR(pi_ac) && (ACMAJORREV_1(pi->pubpi->phy_rev) ||
	     ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	     ACMAJORREV_37(pi->pubpi->phy_rev))) {

		if ((CHSPEC_IS80(pi->radio_chanspec) || PHY_AS_80P80(pi, pi->radio_chanspec)) &&
			CHSPEC_IS5G(pi->radio_chanspec)) {
			/* core 2 conatins 40-80mhz paparam
			 * core 0 conatins 20mhz paparam
			 */
			idx_set[0] = 2; idx_set[1] = 0;
		} else if (CHSPEC_IS160(pi->radio_chanspec)) {
			idx_set[0] = 2; idx_set[1] = 0;
			ASSERT(0);
		} else if (CHSPEC_IS40(pi->radio_chanspec)) {
			/* core 1 conatins 40mhz paparam
			 * core 0 contains 20mhz paparam
			 */
			if (PHY_IPA(pi) && CHSPEC_IS2G(pi->radio_chanspec)) {
			    idx_set[0] = 0; idx_set[1] = 1;
			} else {
			    idx_set[0] = 1; idx_set[1] = 0;
			}
		} else {
			/* core 0 conatins 20mhz OFDM paparam
			 * core 1 contains 20mhz CCK paparam
			 */
			idx_set[0] = 0;
			idx_set[1] = (TINY_RADIO(pi)) ? 0 : 1;
		}

		for (idx = 0; idx < ESTPWRLUTS_TBL_LEN; idx++) {
			for (k = 0, regval[idx] = 0; k < 2; k++) {
				core = idx_set[k];
				if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				    ACMAJORREV_33(pi->pubpi->phy_rev) ||
				    ACMAJORREV_37(pi->pubpi->phy_rev)) {
					int32 num, den;
					num = 8 * (16 * b0[core] + b1[core] * idx);
					den = 32768 + a1[core] * idx;
					pwr_est = MAX(((4 * num + den/2)/den), -8);
					pwr_est = MIN(pwr_est, 0x7F);
				} else {
					pwr_est = wlc_phy_tssi2dbm_acphy(pi, idx,
						a1[core], b0[core], b1[core]);
				}
				regval[idx] |= (uint16)(pwr_est&0xff) << (8*k);
			}
		}
		/* Est Pwr Table is 128x16 Table for 4335 */
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRLUTS0, ESTPWRLUTS_TBL_LEN,
		                          ESTPWRLUTS_TBL_OFFSET, 16, regval);
	} else if (BF3_TSSI_DIV_WAR(pi_ac) && (ACMAJORREV_2(pi->pubpi->phy_rev))) {
		if ((CHSPEC_IS80(pi->radio_chanspec) || CHSPEC_IS40(pi->radio_chanspec)) &&
		    CHSPEC_IS5G(pi->radio_chanspec) && !PHY_IPA(pi)) {
			/* core 2/3 contains 40-80mhz paparam
			 * core 0/1 contains 20mhz paparam
			 */
			idx_set[0] = 2; idx_set[1] = 0;
		} else if (CHSPEC_IS20(pi->radio_chanspec) &&
		    CHSPEC_IS2G(pi->radio_chanspec) && PHY_IPA(pi)) {
			/* core 0,1 conatins 20mhz OFDM paparam
			 * core 2,3 contains 20mhz CCK paparam
			 */
			idx_set[0] = 0;  idx_set[1] = 2;
		} else {
			/* For 80P80 case,bits<7:0> should have 20MHz,bits<15:8> should have
			PAPARMS correspond to 40/80MHz, bits<23:16> are same as bits <15:8>
			currently.
			*/
			if (CHSPEC_IS8080(pi->radio_chanspec)) {
				idx_set[0] = 0; idx_set[1] = 2;
			} else {
				idx_set[0] = 0; idx_set[1] = 0;
			}
		}

		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			uint8 core_off;

			for (idx = 0; idx < ESTPWRLUTS_TBL_LEN; idx++) {
				for (k = 0, regval[idx] = 0; k < 2; k++) {
					core_off = core + ((core < 2)  ?
					                   idx_set[k] : 0);

					pwr_est = wlc_phy_tssi2dbm_acphy(pi, idx,
					                                 a1[core_off],
					                                 b0[core_off],
					                                 b1[core_off]);

					regval[idx] |= (uint16)(pwr_est&0xff) << (8*k);
				}
			}
			/* Est Pwr Table is 128x16 Table. Limit Write to 16 bits */
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRLUTS(core),
				ESTPWRLUTS_TBL_LEN, ESTPWRLUTS_TBL_OFFSET, 16, regval);

		}
	} else if (ACMAJORREV_4(pi->pubpi->phy_rev) && (flag2rangeon || BF3_TSSI_DIV_WAR(pi_ac))) {
		uint32 *estPwrLutReg24bit;
		if ((flag2rangeon)) {
				idx_set[0] = 2; idx_set[1] = 0;
		} else if (BF3_TSSI_DIV_WAR(pi_ac)) {
			if ((CHSPEC_IS80(pi->radio_chanspec) ||
				CHSPEC_IS40(pi->radio_chanspec)) &&
				CHSPEC_IS5G(pi->radio_chanspec) &&
				(ACMAJORREV_4(pi->pubpi->phy_rev))) {
				/* core 2/3 contains 40-80mhz paparam
				 * core 0/1 contains 20mhz paparam
				 */
				idx_set[0] = 0; idx_set[1] = 2;
			} else if (CHSPEC_BW_LE20(pi->radio_chanspec) &&
			    CHSPEC_IS2G(pi->radio_chanspec)) {
				/* core 0,1 conatins 20mhz OFDM paparam
				 * core 2,3 contains 20mhz CCK paparam
				 */
				idx_set[0] = 0;  idx_set[1] = 2;
			} else {
				/* For 80P80 case,bits<7:0> should have 20MHz,bits<15:8> should have
				PAPARMS correspond to 40/80MHz, bits<23:16> are same as bits <15:8>
				currently.
				*/
				if (CHSPEC_IS8080(pi->radio_chanspec)) {
					idx_set[0] = 0; idx_set[1] = 2;
				} else {
					idx_set[0] = 0; idx_set[1] = 0;
				}
			}
		}
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			uint8 core_off;

			for (idx = 0; idx < ESTPWRLUTS_TBL_LEN; idx++) {
				for (k = 0, regval[idx] = 0; k < 2; k++) {
					core_off = core + ((core < 2)  ?
					                   idx_set[k] : 0);
					pwr_est = wlc_phy_tssi2dbm_acphy(pi, idx,
					                                 a1[core_off],
					                                 b0[core_off],
					                                 b1[core_off]);
					regval[idx] |= (uint16)(pwr_est&0xff) << (8*k);
				}
			}
			estPwrLutReg24bit = phy_malloc_fatal(pi, ESTPWRLUTS_TBL_LEN*4);
			/* Est Power LUT is of 24 bits.
			bit<7:0> corresponds to 20MHz BW case
			bits<15:0> corresponds to 40/80MHz case
			bits<23:16> corresponds to 80+80 MHz case.
			Tx Power control multi mode is set to
			4 for 80P80 case
			*/
			for (idx = 0; idx < ESTPWRLUTS_TBL_LEN; idx++) {
				estPwrLutReg24bit[idx] =
					((((uint32)regval[idx]&0xFF00)
					<< 8) | (uint32)regval[idx]);
			}
			/* Est Pwr Tbl is 128x24 Table.
			Limit Write to 32 bits
			*/
			wlc_phy_table_write_acphy(pi,
				ACPHY_TBL_ID_ESTPWRLUTS(core), ESTPWRLUTS_TBL_LEN,
				ESTPWRLUTS_TBL_OFFSET, 32, estPwrLutReg24bit);
			/* Free up the memory allocated */
			phy_mfree(pi, estPwrLutReg24bit, ESTPWRLUTS_TBL_LEN*4);
		}
	} else {
#ifdef WLC_TXCAL
		if (pi->txcali->txcal_pwr_tssi_tbl_in_use == 1) {
			wlc_phy_apply_pwr_tssi_tble_chan_acphy(pi);
		} else
#endif	/* WLC_TXCAL */
		{
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			for (idx = 0; idx < ESTPWRLUTS_TBL_LEN; idx++) {
				pwr_est = wlc_phy_tssi2dbm_acphy(pi, idx,
				                                 a1[core], b0[core], b1[core]);
				if (flag2rangeon) {
					int32  pwr_est2range;

					/* iPa - ToDo 2 range TSSI */
					core2range = PHYCORENUM(pi->pubpi->phy_corenum) + core;
					ASSERT(core2range < PAPARAM_SET_NUM);

					pwr_est2range = wlc_phy_tssi2dbm_acphy(pi, idx,
					                                       a1[core2range],
					                                       b0[core2range],
					                                       b1[core2range]);

					regval[idx] =
						(uint16)((pwr_est2range&0xff) +
						((pwr_est&0xff)<<8));
				} else {
					if (ACMAJORREV_36(pi->pubpi->phy_rev) ||
						ACMAJORREV_40(pi->pubpi->phy_rev)) {
						regval[idx] = (uint16)(((pwr_est&0xff)<< 8)
							| (pwr_est&0xff));
					} else {
						regval[idx] = (uint16)(pwr_est&0xff);
					}
				}
			}
			/* Est Pwr Table is 128x8 Table. Limit Write to 8 bits */
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRLUTS(core),
				ESTPWRLUTS_TBL_LEN, ESTPWRLUTS_TBL_OFFSET, 16, regval);
		}
		}
	}

	if (IBOARD(pi)) {
		tx_idx = 0;
		phy_ac_rfseq_mode_set(pi, 1);
		targetidx = wlc_phy_tone_pwrctrl(pi, tx_idx, 0);
		phy_ac_rfseq_mode_set(pi, 0);
		wlc_phy_txpwr_by_index_acphy(pi, 1, targetidx);
		if (targetidx < 0) {
			wlc_phy_txpwr_by_index_acphy(pi, 1, 0);
		}
	}

	/* start to populate estPwrShftTbl */
	for (idx = 0; idx < ESTPWRSHFTLUTS_TBL_LEN; idx++) {
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			if ((idx == 0)||(idx == 2)||(idx == 4)||((idx > 6)&&(idx < 10))) {
				/* 20in40 and 20in80 subband cases */
				pdoffs = 0;
				FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
					/* core_freq_segment_map is only required for 80P80 mode.
					For other modes, it is ignored
					*/
					core_freq_segment_map =
						pi->u.pi_acphy->core_freq_mapping[core];
					chan_freq_range = phy_ac_chanmgr_get_chan_freq_range(pi, 0,
						PRIMARY_FREQ_SEGMENT);
					pdoffs = wlc_phy_pdoffset_cal_acphy(pdoffs,
						pwrdet->pdoffset5gsubband[core],
						chan_freq_range, core);
					}
				shfttblval[idx] = pdoffs & 0xffffff;
			} else if ((idx == 1) || ((idx > 4) && (idx < 7)) || (idx == 14)) {
				pdoffs = 0;
				FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
					/* core_freq_segment_map is only required for 80P80 mode.
					For other modes, it is ignored
					*/
					core_freq_segment_map =
						pi->u.pi_acphy->core_freq_mapping[core];
					chan_freq_range = phy_ac_chanmgr_get_chan_freq_range(pi, 0,
						PRIMARY_FREQ_SEGMENT);
					pdoffs = wlc_phy_pdoffset_cal_acphy(pdoffs,
						pwrdet->pdoffset40[core], chan_freq_range, core);
				}
				shfttblval[idx] = pdoffs & 0xffffff;
				if ((idx == 5) &&
				    (BF3_TSSI_DIV_WAR(pi_ac) && ACMAJORREV_1(pi->pubpi->phy_rev))) {
					shfttblval[idx] = 0;
				}
			} else if (idx == 10) {
				pdoffs = 0;
				FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
					/* core_freq_segment_map is only required for 80P80 mode.
					For other modes, it is ignored
					*/
					core_freq_segment_map =
						pi->u.pi_acphy->core_freq_mapping[core];
					chan_freq_range = phy_ac_chanmgr_get_chan_freq_range(pi, 0,
						PRIMARY_FREQ_SEGMENT);
					pdoffs = wlc_phy_pdoffset_cal_acphy(pdoffs,
						pwrdet->pdoffset80[core], chan_freq_range, core);
				}
				shfttblval[idx] = pdoffs & 0xffffff;
			} else {
				shfttblval[idx] = 0;
			}
		} else {
			/* hardcoding for 4335 wlbga for now, will add nvram var later if needed */
			if (CHIPID(pi->sh->chip) == BCM4345_CHIP_ID ||
				(CHIPID(pi->sh->chip) == BCM4335_CHIP_ID &&
				CHSPEC_IS2G(pi->radio_chanspec) &&
				CHSPEC_IS20(pi->radio_chanspec))) {
				if (CHIPID(pi->sh->chip) != BCM4345_CHIP_ID &&
				    BF3_TSSI_DIV_WAR(pi_ac)) {
					/* Note: SROM entry rpcal2g and rpcal5gb0 is redefined for
					 * 4335 to represent the 2G channel-dependent TSSI offset
					 */
					if (idx == 3 || idx == 17) {
						switch (CHSPEC_CHANNEL(pi->radio_chanspec)) {
						case 1:
							pdoffs =
							((pi->sromi->rpcal2g >> 0)  & 0xF);
							break;
						case 2:
							pdoffs =
							((pi->sromi->rpcal2g >> 4)  & 0xF);
							break;
						case 3:
							pdoffs =
							((pi->sromi->rpcal2g >> 8)  & 0xF);
							break;
						case 12:
							pdoffs =
							((pi->sromi->rpcal2g >> 12) & 0xF);
							break;
						case 13:
							pdoffs =
							((pi->sromi->rpcal5gb0 >> 0) & 0xF);
							break;
						case 14:
							pdoffs =
							((pi->sromi->rpcal5gb0 >> 4) & 0xF);
							break;
						case 4:
							pdoffs =
							((pi->sromi->rpcal5gb0 >> 8) & 0xF);
							break;
						case 5:
							pdoffs =
							((pi->sromi->rpcal5gb0 >> 12) & 0xF);
							break;
						case 6:
							pdoffs =
							((pi->sromi->rpcal5gb1 >> 0) & 0xF);
							break;
						case 7:
							pdoffs =
							((pi->sromi->rpcal5gb1 >> 4) & 0xF);
							break;
						case 8:
							pdoffs =
							((pi->sromi->rpcal5gb1 >> 8) & 0xF);
							break;
						case 9:
							pdoffs =
							((pi->sromi->rpcal5gb1 >> 12) & 0xF);
							break;
						case 10:
							pdoffs =
							((pi->sromi->rpcal5gb2 >> 0) & 0xF);
							break;
						case 11:
							pdoffs =
							((pi->sromi->rpcal5gb2 >> 4) & 0xF);
							break;

						default:
							pdoffs = 0;
							break;
						}
						pdoffs = (pdoffs > 7) ? (0xf0 | pdoffs) : pdoffs;
						shfttblval[idx] = pdoffs & 0xff;
					} else {
						shfttblval[idx] = 0;
					}
				} else { /* when tssi_div WAR is off, only cck offset is used */
					if (idx == 17) {
						pdoffs = pwrdet->pdoffsetcck[0];
						pdoffs = (pdoffs > 7) ? (0xf0 | pdoffs) : pdoffs;
						shfttblval[idx] = pdoffs & 0xff;
					} else {
						shfttblval[idx] = 0;
					}
				}
			} else {
#ifdef POWPERCHANNL
				if (PWRPERCHAN_ENAB(pi)) {
				if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
					if (idx == 3) {
						pdoffs = 0;
						/* for now temp based offst support is
						 * not added
						 */
						FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain,
							core) {
							core_freq_segment_map =
							pi->u.pi_acphy->core_freq_mapping[core];
							chan_freq_range =
							phy_ac_chanmgr_get_chan_freq_range(pi,
									0, PRIMARY_FREQ_SEGMENT);
							pdoffs = wlc_phy_pdoffset_cal_acphy(pdoffs,
							pwrdet->PwrOffsets2GNormTemp[core][
							CHSPEC_CHANNEL(pi->radio_chanspec)-1],
							chan_freq_range, core);
						}
						shfttblval[idx] = pdoffs & 0xffff;
					} else if (idx == 17) {
						pdoffs = 0;
						/* for now temp based offset support is not added */
						FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain,
							core) {
							core_freq_segment_map =
							pi->u.pi_acphy->core_freq_mapping[core];
							chan_freq_range =
							phy_ac_chanmgr_get_chan_freq_range(pi,
								0, PRIMARY_FREQ_SEGMENT);
							pdoffs = wlc_phy_pdoffset_cal_acphy(pdoffs,
							pwrdet->PwrOffsets2GNormTemp[core][
							CHSPEC_CHANNEL(pi->radio_chanspec)-1]
							+ pwrdet->pdoffsetcck[core],
							chan_freq_range, core);
						}
						shfttblval[idx] = pdoffs & 0xffff;
					} else {
						shfttblval[idx] = 0;
					}
				}
			}
			else
#endif /* POWPERCHANNL */
			{
				if (idx == 3) {
					pdoffs = 0;
					FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
						core_freq_segment_map =
							pi->u.pi_acphy->core_freq_mapping[core];
						chan_freq_range =
							phy_ac_chanmgr_get_chan_freq_range(pi,
							0, PRIMARY_FREQ_SEGMENT);
						pdoffs = wlc_phy_pdoffset_cal_acphy(pdoffs,
							pwrdet->pdoffset2g20in20[core],
							chan_freq_range, core);
					}
					shfttblval[idx] = pdoffs & 0xffffff;
				} else if (idx == 17) {
					pdoffs = 0;
					FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
						core_freq_segment_map =
							pi->u.pi_acphy->core_freq_mapping[core];
						chan_freq_range =
							phy_ac_chanmgr_get_chan_freq_range(pi,
							0, PRIMARY_FREQ_SEGMENT);
						pdoffs = wlc_phy_pdoffset_cal_acphy(pdoffs,
							pwrdet->pdoffsetcck[core],
							chan_freq_range, core);
					}
					shfttblval[idx] = pdoffs & 0xffffff;
				} else {
					if (pwrdet->pdoffset2g40_flag == 1) {
						shfttblval[idx] = 0;
					} else {
						shfttblval[idx] = 0;
						if (idx == 5) {
							pdoffs = 0;
							FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain,
								core) {
								core_freq_segment_map =
								pi->u.pi_acphy->core_freq_mapping[
								core];

								chan_freq_range =
								phy_ac_chanmgr_get_chan_freq_range(
								pi, 0, PRIMARY_FREQ_SEGMENT);

								pdoffs =
								wlc_phy_pdoffset_cal_acphy(pdoffs,
								        pwrdet->pdoffset2g40[core],
								       chan_freq_range, core);
							}
							shfttblval[idx] = pdoffs & 0xffffff;
						}
					}
				}
			}
		}
	}
	if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
			shfttblval[idx] = 0;
		}
	}

	/* JIRA: SW4349-239 */
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		/*
		1.	In RSDB mode, just write to EstPWrShiftluts0
		2.	In 2x2 mode, since this table used to be a common table,
			write same entries to EstPWrShiftluts0 and EstPWrShiftluts1.
		3.	In 80p80 mode, TBD by SubraK
		*/
		FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
			for (idx = 0; idx < 24; idx++) {
				shfttblval[idx] = (shfttblval[idx]>>(8*core));
			}
			/*
			Only Least significant 8 bits
			<7:0> of shfttblval[idx] are written into phy tbl
			*/
			wlc_phy_table_write_acphy(pi, wlc_phy_get_tbl_id_estpwrshftluts(pi, core),
				ESTPWRSHFTLUTS_TBL_LEN, ESTPWRSHFTLUTS_TBL_OFFSET, 32, shfttblval);
		}
	} else {
		if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRSHFTLUTS0,
				ESTPWRSHFTLUTS_TBL_LEN, ESTPWRSHFTLUTS_TBL_OFFSET, 32, shfttblval);
		} else if (ACMAJORREV_25(pi->pubpi->phy_rev)) {
			switch (core) {
				case 0:
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRSHFTLUTS0,
						ESTPWRSHFTLUTS_TBL_LEN, ESTPWRSHFTLUTS_TBL_OFFSET,
						32, shfttblval);
					break;
				case 1:
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRSHFTLUTS1,
						ESTPWRSHFTLUTS_TBL_LEN, ESTPWRSHFTLUTS_TBL_OFFSET,
						32, shfttblval);
					break;
			}
		} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, AC2PHY_TBL_ID_ESTPWRSHFTLUTS0,
				ESTPWRSHFTLUTS_TBL_LEN, ESTPWRSHFTLUTS_TBL_OFFSET,
				32, shfttblval);
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRSHFTLUTS,
				ESTPWRSHFTLUTS_TBL_LEN, ESTPWRSHFTLUTS_TBL_OFFSET, 32, shfttblval);
		}
	}
#ifdef ENABLE_FCBS
	}
#endif
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		uint8 txpwrindexlimit;
		/* Setting Tx gain table index limit value */
		if (CHSPEC_IS2G(pi->radio_chanspec)) {
			/* currently, assume that both cck and OFDM have same index
			 * cap value. Once, ucode support is available, fix this
			 */
			txpwrindexlimit = ti->txpwrindexlimit[ACPHY_TXPWRINDEX_LIM_2G_CCK];
		} else {
			uint8 indx = ACPHY_TXPWRINDEX_LIM_5G_LL;
			uint8 subband = phy_ac_chanmgr_get_chan_freq_range(pi,
				0, PRIMARY_FREQ_SEGMENT);
			switch (subband) {
				case WL_CHAN_FREQ_RANGE_5G_BAND0:
					indx = ACPHY_TXPWRINDEX_LIM_5G_LL;
					break;
				case WL_CHAN_FREQ_RANGE_5G_BAND1:
					indx = ACPHY_TXPWRINDEX_LIM_5G_LH;
					break;
				case WL_CHAN_FREQ_RANGE_5G_BAND2:
					indx = ACPHY_TXPWRINDEX_LIM_5G_HL;
					break;
				case WL_CHAN_FREQ_RANGE_5G_BAND3:
					indx = ACPHY_TXPWRINDEX_LIM_5G_HH;
					break;
				default:
					PHY_ERROR(("wl%d: %s: Unrecognized subband: %d\n",
					pi->sh->unit, __FUNCTION__, subband));
					break;
			}
			txpwrindexlimit = ti->txpwrindexlimit[indx];
		}

		/* HW internally does right shift by 1 before capping */
		txpwrindexlimit <<= 1;
		FOREACH_CORE(pi, core) {
			MOD_PHYREGCE(pi, TxPwrCtrl_Multi_Mode, core,
				txPwrIndexLimit, txpwrindexlimit);
		}
	}

	ACPHY_ENABLE_STALL(pi, stall_val);
	PHY_CHANLOG(pi, __FUNCTION__, TS_EXIT, 0);
}

int8
wlc_phy_tone_pwrctrl(phy_info_t *pi, int8 tx_idx, uint8 core)
{
	int8 pwr;
	int8 targetpwr = -99, tgt_pwr_qdbm;
	int16  idle_tssi[PHY_CORE_MAX], tone_tssi[PHY_CORE_MAX];
	uint16 adjusted_tssi[PHY_CORE_MAX];
	int16 a1[PHY_CORE_MAX];
	int16 b1[PHY_CORE_MAX];
	int16 b0[PHY_CORE_MAX];
	int8 postive_slope = 1;
	int8 targetidx;
	int8 deltapwr;
	int16 tmpidx;
	txgain_setting_t txgain_settings;
	int8 orig_rxfarrow_shift = 0;

	wlc_phy_get_papd_cal_pwr_acphy(pi, &targetpwr, &tx_idx, core);

	if (IBOARD(pi)) {
		tgt_pwr_qdbm = READ_PHYREGFLD(pi, TxPwrCtrlTargetPwr_path0, targetPwr0);
		targetpwr = tgt_pwr_qdbm / 4;
	} else {
		tgt_pwr_qdbm = targetpwr * 4;
	}

	if (targetpwr == -99) {
		targetidx = -1;
	} else {
		wlc_phy_get_paparams_for_band_acphy(pi, a1, b0, b1);
		/* meas the idle tssi */
		wlc_phy_txpwrctrl_idle_tssi_meas_acphy(pi);
		idle_tssi[core] = READ_PHYREGCE(pi, TxPwrCtrlIdleTssi_path, core) & 0x3ff;
		idle_tssi[core] = idle_tssi[core] - 1023;

		/* prevent crs trigger */
		wlc_phy_stay_in_carriersearch_acphy(pi, TRUE);
		if (!(ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev))) {
			orig_rxfarrow_shift = READ_PHYREGFLD(pi, RxSdFeConfig6, rx_farrow_rshift_0);
			MOD_PHYREG(pi, RxSdFeConfig6, rx_farrow_rshift_0, 2);
		}
		if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID))
			wlc_phy_tssi_radio_setup_acphy_tiny(pi, pi->sh->hw_phyrxchain, 0);
		else if (RADIOID_IS(pi->pubpi->radioid, BCM20694_ID)) {
			wlc_phy_tssi_radio_setup_acphy_20694(pi, 0);
		}
		else if (RADIOID_IS(pi->pubpi->radioid, BCM20695_ID))
			wlc_phy_tssi_radio_setup_acphy_28nm(pi, 0);
		else if (RADIOID_IS(pi->pubpi->radioid, BCM20696_ID))
			wlc_phy_tssi_radio_setup_acphy_20696(pi, 0);
		else
			wlc_phy_tssi_radio_setup_acphy(pi, pi->sh->hw_phyrxchain, 0);

		wlc_phy_txpwr_by_index_acphy(pi, 1, tx_idx);
		wlc_phy_get_txgain_settings_by_index_acphy(
			pi, &txgain_settings, tx_idx);
		wlc_phy_poll_samps_WAR_acphy(pi, tone_tssi,
			TRUE, FALSE, &txgain_settings, FALSE, TRUE, 0, 0);

		adjusted_tssi[core] = 1023 - postive_slope * (tone_tssi[core] - idle_tssi[core]);
		adjusted_tssi[core] = adjusted_tssi[core] >> 3;
		/* prevent crs trigger */
		wlc_phy_stay_in_carriersearch_acphy(pi, FALSE);
		if (!(ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev))) {
			MOD_PHYREG(pi, RxSdFeConfig6, rx_farrow_rshift_0, orig_rxfarrow_shift);
		}
		pwr = wlc_phy_tssi2dbm_acphy(pi, adjusted_tssi[core], a1[core], b0[core], b1[core]);

		/* delta pwr in qdb */
		deltapwr = tgt_pwr_qdbm - pwr;
		if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
			/* for 4350 with 0.5dB step size gaintable */
			tmpidx = (int16)tx_idx - (int16)(deltapwr >> 1);
		} else {
			tmpidx = (int16)tx_idx - (int16)deltapwr;
		}
		if (tmpidx < 0)
			targetidx = 0;
		else if (tmpidx > MAX_TX_IDX)
			targetidx = MAX_TX_IDX;
		else
			targetidx = (int8)tmpidx;
	}

	return targetidx;
}

static int32
phy_ac_tpc_get_estpwrlut_srom12(int16 *a, int16 *b, int16 *c, int16 *d,
		uint8 pa_fittype, uint8 core, int32 idx)
{
	int32 firstTerm = 0, secondTerm = 0, thirdTerm = 0, fourthTerm = 0;
	int32 ctrSqr = idx * idx;
	int32 pwr_est = 0;


	if (pa_fittype == 0) {
		/* logdetect */
		firstTerm  = (int32)a[core] * 128;
		secondTerm = ((int32)b[core] * idx) / 2;
		thirdTerm  = ((int32)c[core] * ctrSqr) / 128;
		fourthTerm = ((int32)d[core] * idx) / (((int32)idx - 128));
		pwr_est = (firstTerm + secondTerm +
				thirdTerm + fourthTerm) / 8192;
	} else if (pa_fittype == 1) {
		/* diode type */
		firstTerm = (int32)a[core] * 16;
		secondTerm = (b[core] * ctrSqr) / 4096;
		if (idx == 0)
			thirdTerm = 0;
		else
			thirdTerm = c[core] * ctrSqr /
				(ctrSqr - ((int32)d[core] * 2));
		pwr_est = (firstTerm + secondTerm + thirdTerm) / 1024;
	} else {
		/* original */
		firstTerm  = 8 * (16 * (int32)b[core]
				+ (int32)c[core] * idx);
		secondTerm = 32768 + (int32)a[core] * idx;
		pwr_est = MAX(((4 * firstTerm + secondTerm/2)
					/secondTerm), -8);
		pwr_est = MIN(pwr_est, 0x7F);
	}
	return pwr_est;

}

static void
phy_ac_tpc_get_paparams_percore_srom12(phy_info_t *pi, uint8 chan_freq_range,
		int16 *a, int16 *b, int16 *c, int16 *d, uint8 core)
{
	srom12_pwrdet_t *pwrdet = pi->pwrdet_ac;

	switch (chan_freq_range) {
		case WL_CHAN_FREQ_RANGE_2G:
		case WL_CHAN_FREQ_RANGE_5G_BAND0:
		case WL_CHAN_FREQ_RANGE_5G_BAND1:
		case WL_CHAN_FREQ_RANGE_5G_BAND2:
		case WL_CHAN_FREQ_RANGE_5G_BAND3:
		case WL_CHAN_FREQ_RANGE_5G_BAND4:
		   a[core] =  pwrdet->pwrdet_a[core][chan_freq_range];
		   b[core] =  pwrdet->pwrdet_b[core][chan_freq_range];
		   c[core] =  pwrdet->pwrdet_c[core][chan_freq_range];
		   d[core] =  pwrdet->pwrdet_d[core][chan_freq_range];
		   break;
		case WL_CHAN_FREQ_RANGE_2G_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND0_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND1_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND2_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND3_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND4_40:
		   /* Adjust index for 40M */
		   a[core] =  pwrdet->pwrdet_a_40[core][chan_freq_range - 6];
		   b[core] =  pwrdet->pwrdet_b_40[core][chan_freq_range - 6];
		   c[core] =  pwrdet->pwrdet_c_40[core][chan_freq_range - 6];
		   d[core] =  pwrdet->pwrdet_d_40[core][chan_freq_range - 6];
		   break;
		case WL_CHAN_FREQ_RANGE_5G_BAND0_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND1_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND2_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND3_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND4_80:
		   /* Adjust index for 80M */
		   a[core] =  pwrdet->pwrdet_a_80[core][chan_freq_range - 12];
		   b[core] =  pwrdet->pwrdet_b_80[core][chan_freq_range - 12];
		   c[core] =  pwrdet->pwrdet_c_80[core][chan_freq_range - 12];
		   d[core] =  pwrdet->pwrdet_d_80[core][chan_freq_range - 12];
		   break;
		default:
			PHY_ERROR(("wl%d: %s: pwrdet core%d: a=%d b=%d c=%d d=%d\n",
					pi->sh->unit, __FUNCTION__, core,
					a[core], b[core], c[core], d[core]));
			break;
	}
}

static void
phy_ac_tpc_get_paparams_srom12(phy_info_t *pi, uint8 chan_freq_range,
		int16 *a, int16 *b, int16 *c, int16 *d)
{
	uint8  core;

	FOREACH_CORE(pi, core) {
		phy_ac_tpc_get_paparams_percore_srom12(pi,
				chan_freq_range, a, b, c, d, core);
	}
}

static void
phy_ac_tpc_get_paparams_80p80_srom12(phy_info_t *pi, uint8 *chan_freqs,
		int16 *a, int16 *b, int16 *c, int16 *d)
{
	uint8 core, chan_freq_range;

	FOREACH_CORE(pi, core) {
		if (ACMAJORREV_33(pi->pubpi->phy_rev)) {
			/* core 0/1: 80L, core 2/3: 80U */
			chan_freq_range = (core <= 1) ? chan_freqs[0] : chan_freqs[1];
		} else {
			chan_freq_range = chan_freqs[0];
		}

		phy_ac_tpc_get_paparams_percore_srom12(pi,
				chan_freq_range, a, b, c, d, core);
	}
}

int8
wlc_phy_fittype_srom12_acphy(phy_info_t *pi)
{
	int8 pdet_range;
	if (CHSPEC_IS5G(pi->radio_chanspec))
		pdet_range = pi->tpci->data->cfg.srom_5g_pdrange_id;
	else
		pdet_range = pi->tpci->data->cfg.srom_2g_pdrange_id;
	switch (pdet_range) {
	case 24:
		return 0; /* Log detector for both 2G and 5G */
	case 25:
	case 26:
		return 1; /* Diode detector for both 2G and 5G */
	default:
		if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) ||
			ACMAJORREV_37(pi->pubpi->phy_rev)) {
			if (pi->sromi->sr13_dettype_en) {
				if (CHSPEC_IS5G(pi->radio_chanspec))
					return pi->sromi->dettype_5g;
				else
					return pi->sromi->dettype_2g;
			} else {
				if (pdet_range == 0) {
					/* logdetect - 4366 MC card */
					return 0;
				} else if ((CHSPEC_IS5G(pi->radio_chanspec) &&
						(pdet_range == 2))) {
					/* original: 4366 MCH5L - will be obsolete soon */
					return 2;
				} else {
					/* diode type: 4366 MCH2L,MCM2/5L, etc */
					return 1;
				}
			}
		} else {
			if (CHSPEC_IS5G(pi->radio_chanspec))
				return pi->sromi->dettype_5g;
			else
				return pi->sromi->dettype_2g;
		}
	}
}

static void
wlc_phy_get_srom12_pdoffset_acphy(phy_info_t *pi, int8 *pdoffs)
{
	srom12_pwrdet_t *pwrdet = pi->pwrdet_ac;
	int8  poffs1, poffs2;
	uint8 core, band = 0;
	uint8 bands[NUM_CHANS_IN_CHAN_BONDING];

	ASSERT(SROMREV(pi->sh->sromrev) >= 12);

	memset(pdoffs, 0, 2*PHY_CORE_MAX * sizeof(int8));

	/* to figure out which subband is in 5G */
	/* in the range of 0, 1, 2, 3, 4, 5 */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		if (PHY_AS_80P80(pi, pi->radio_chanspec)) {
			phy_ac_chanmgr_get_chan_freq_range_80p80_srom12(pi,
				pi->radio_chanspec, bands);
			bands[0] = bands[0] - WL_CHAN_FREQ_RANGE_5G_BAND0_80 + 1;
			bands[1] = bands[1] - WL_CHAN_FREQ_RANGE_5G_BAND0_80 + 1;
		} else {
			band = phy_ac_chanmgr_get_chan_freq_range_srom12(pi,
				pi->radio_chanspec);
			if (band >= WL_CHAN_FREQ_RANGE_5G_BAND0_80) {
				ASSERT((CHSPEC_IS80(pi->radio_chanspec)));
				band = band - WL_CHAN_FREQ_RANGE_5G_BAND0_80 + 1;
			} else if (band >= WL_CHAN_FREQ_RANGE_2G_40) {
				ASSERT(CHSPEC_IS40(pi->radio_chanspec));
				band = band - WL_CHAN_FREQ_RANGE_2G_40;
			} else {
				ASSERT(CHSPEC_IS20(pi->radio_chanspec));
			}
		}
	} else {
		band = phy_ac_chanmgr_get_chan_freq_range_srom12(pi, pi->radio_chanspec);
		if (band >= WL_CHAN_FREQ_RANGE_2G_40) {
			if (CHSPEC_IS80(pi->radio_chanspec)) {
				band = band - WL_CHAN_FREQ_RANGE_5G_BAND0_80 + 1;
			} else {
				ASSERT(CHSPEC_IS40(pi->radio_chanspec));
				band = band - WL_CHAN_FREQ_RANGE_2G_40;
			}
		}
	}

	FOREACH_CORE(pi, core) {
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			if (PHY_AS_80P80(pi, pi->radio_chanspec)) {
				if (ACMAJORREV_33(pi->pubpi->phy_rev) ||
				    ACMAJORREV_37(pi->pubpi->phy_rev)) {
					/* core 0/1: 80L, core 2/3: 80U */
					band = (core <= 1) ? bands[0] : bands[1];
				} else {
					band = bands[0];
				}
				poffs1 = (uint8)(pwrdet->pdoffset20in80[core][band]);
				poffs2 = (uint8)(pwrdet->pdoffset40in80[core][band]);
				poffs1 -= (poffs1 >= 16)? 32 : 0;
				poffs2 -= (poffs2 >= 16)? 32 : 0;
				pdoffs[core] = poffs1;
				pdoffs[PHY_CORE_MAX+core] = poffs2;
			} else if (CHSPEC_IS160(pi->radio_chanspec)) {
				pdoffs[PHY_CORE_MAX+core] = pdoffs[core] = 0;
				ASSERT(0);
			} else if (CHSPEC_IS80(pi->radio_chanspec)) {
				poffs1 = (uint8)(pwrdet->pdoffset20in80[core][band]);
				poffs2 = (uint8)(pwrdet->pdoffset40in80[core][band]);
				poffs1 -= (poffs1 >= 16)? 32 : 0;
				poffs2 -= (poffs2 >= 16)? 32 : 0;
				pdoffs[core] = poffs1;
				pdoffs[PHY_CORE_MAX+core] = poffs2;
			} else if (CHSPEC_IS40(pi->radio_chanspec)) {
				poffs1 = (uint8)(pwrdet->pdoffset20in40[core][band]);
				poffs1 -= (poffs1 >= 16)? 32 : 0;
				pdoffs[core] = poffs1;
				pdoffs[PHY_CORE_MAX+core] = 0;
			} else {
				pdoffs[core] = 0;
				pdoffs[PHY_CORE_MAX+core] = 0;
			}
		} else {
			if (CHSPEC_IS40(pi->radio_chanspec)) {
				poffs1 = (uint8)(pwrdet->pdoffset20in40[core][band]);
				poffs1 -= (poffs1 >= 16)? 32 : 0;
				pdoffs[core] = poffs1;
				/* pdoffset for cck 20in40MHz */
				poffs2 = (uint8)(pwrdet->pdoffsetcck20m[core]);
				poffs2 -= (poffs2 >= 16)? 32 : 0;
				pdoffs[PHY_CORE_MAX+core] = poffs2;
			} else {
				pdoffs[core] = 0;
				/* pdoffset for cck in 20MHz */
				poffs2 = (uint8)(pwrdet->pdoffsetcck[core]);
				poffs2 -= (poffs2 >= 16)? 32 : 0;
				pdoffs[PHY_CORE_MAX+core] = poffs2;
			}
		}
	}
}

static void
wlc_phy_txpwrctrl_pwr_setup_srom12_acphy(phy_info_t *pi)
{
	uint8 stall_val;
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;
	srom12_pwrdet_t *pwrdet = pi->pwrdet_ac;
	int16  a[PHY_CORE_MAX], b[PHY_CORE_MAX];
	int16  c[PHY_CORE_MAX], d[PHY_CORE_MAX];
	int16  ak[PHY_CORE_MAX], bk[PHY_CORE_MAX];
	int16  ck[PHY_CORE_MAX], dk[PHY_CORE_MAX];
	int32  idx;
	uint32 shfttblval[ESTPWRSHFTLUTS_TBL_LEN];
	int32  pwr_est, pwr_est0, pwr_est1, pwr_est_cck, tbl_len;
	uint32 regval[128];
	int8   pdoffsets[PHY_CORE_MAX*2];
	int32  firstTerm = 0, secondTerm = 0, thirdTerm = 0, fourthTerm = 0;
	int32  ctrSqr[128];
	int8   target_min_limit;
	uint8  chan_freq_range, iidx, chan_freq = 0;
	uint8  chan_freqs[NUM_CHANS_IN_CHAN_BONDING];
	uint8  core;
	uint8  tssi_delay = 0, tssi_delay_cck = 0;
	uint8  mult_mode = 0;
	uint8  pa_fittype = 0;
	bool   using_estpwr_lut_cck = FALSE;

#ifdef PREASSOC_PWRCTRL
	bool init_idx_carry_from_lastchan;
	uint8 step_size, prev_target_qdbm;
#endif
	struct _tp_qtrdbm {
		uint8 core;
		int8 target_pwr_qtrdbm;
	} tp_qtrdbm_each_core[PHY_CORE_MAX]; /* TP for each core */
	uint core_count = 0;

#if defined(PHYCAL_CACHING)
	ch_calcache_t *ctx = NULL;
	ctx = wlc_phy_get_chanctx(pi, pi->radio_chanspec);
	BCM_REFERENCE(ctx);
#endif /* PHYCAL_CACHING */
	iidx = 0;
	tbl_len = 128;
	pwr_est_cck = 0;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	ASSERT(SROMREV(pi->sh->sromrev) >= 12);

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_REG_LIST_START
		ACPHY_DISABLE_STALL_ENTRY(pi)
		/* enable TSSI */
		MOD_PHYREG_ENTRY(pi, TSSIMode, tssiEn, 1)
		MOD_PHYREG_ENTRY(pi, TxPwrCtrlCmd, txPwrCtrl_en, 0)
	ACPHY_REG_LIST_EXECUTE(pi);

	/* initialize a, b, c,d to be all zero */
	for (idx = 0; idx < PHY_CORE_MAX; idx++) {
		a[idx] = 0; ak[idx] = 0;
		b[idx] = 0; bk[idx] = 0;
		c[idx] = 0; ck[idx] = 0;
		d[idx] = 0; dk[idx] = 0;
	}

	if (RADIOREV(pi->pubpi->radiorev) == 4 ||
	    RADIOREV(pi->pubpi->radiorev) == 8 ||
	    ACMAJORREV_2(pi->pubpi->phy_rev) ||
	    ACMAJORREV_5(pi->pubpi->phy_rev) ||
	    ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		/* 4360B0/B1/4350 using 0.5dB-step gaintbl, bbmult interpolation enabled */
		MOD_PHYREG(pi, TxPwrCtrlCmd, bbMultInt_en, 1);
#ifdef PREASSOC_PWRCTRL
		step_size = 2;
#endif
	} else if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		if (PHY_IPA(pi)) {
			MOD_PHYREG(pi, TxPwrCtrlCmd, bbMultInt_en, 0);
		} else {
			MOD_PHYREG(pi, TxPwrCtrlCmd, bbMultInt_en, 1);
		}
#ifdef PREASSOC_PWRCTRL
		step_size = 2;
#endif
	} else {
		/* disable bbmult interpolation
		   to work with a 0.25dB step txGainTbl
		*/
		MOD_PHYREG(pi, TxPwrCtrlCmd, bbMultInt_en, 0);
#ifdef PREASSOC_PWRCTRL
		step_size = 1;
#endif
	}

	/* Get the band (chan_freq_range) to get the pwrdet params from SROM */
	if (IS_4364_1x1(pi)|| IS_4364_3x3(pi)||
	    (ACMAJORREV_5(pi->pubpi->phy_rev) && ACMINORREV_1(pi))) {
		chan_freq_range = phy_ac_chanmgr_get_chan_freq_range_srom12(pi,
				pi->radio_chanspec);
	} else if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	           ACMAJORREV_33(pi->pubpi->phy_rev) ||
	           ACMAJORREV_37(pi->pubpi->phy_rev)) {
		if (PHY_AS_80P80(pi, pi->radio_chanspec)) {
			phy_ac_chanmgr_get_chan_freq_range_80p80_srom12(pi,
					pi->radio_chanspec, chan_freqs);
			chan_freq_range = chan_freqs[0];
		} else {
			chan_freq_range = phy_ac_chanmgr_get_chan_freq_range_srom12(pi,
					pi->radio_chanspec);
		}
	} else {
		chan_freq_range = phy_ac_chanmgr_get_chan_freq_range_srom12(pi, 0);
	}

	/* Get pwrdet params from SROM for current subband */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		if (PHY_AS_80P80(pi, pi->radio_chanspec)) {
			phy_ac_tpc_get_paparams_80p80_srom12(pi, chan_freqs, a, b, c, d);
		} else {
			phy_ac_tpc_get_paparams_srom12(pi, chan_freq_range, a, b, c, d);
		}
		if (CHSPEC_IS2G(pi->radio_chanspec) &&
		    (pi->tpci->data->cfg.srom_2g_pdrange_id == 2)) {
			/* Using separate estPwrLuts for 2G CCK for
			 * the 2G-only board to improve the performance
			 */
			using_estpwr_lut_cck = TRUE;
			if (CHSPEC_IS20(pi->radio_chanspec)) {
				phy_ac_tpc_get_paparams_srom12(pi,
					WL_CHAN_FREQ_RANGE_5G_BAND0,
					ak, bk, ck, dk);
			} else {
				phy_ac_tpc_get_paparams_srom12(pi,
					WL_CHAN_FREQ_RANGE_5G_BAND0_40,
					ak, bk, ck, dk);
			}
		}
	} else {
		FOREACH_CORE(pi, core) {
			switch (chan_freq_range) {
			case WL_CHAN_FREQ_RANGE_2G:
			case WL_CHAN_FREQ_RANGE_5G_BAND0:
			case WL_CHAN_FREQ_RANGE_5G_BAND1:
			case WL_CHAN_FREQ_RANGE_5G_BAND2:
			case WL_CHAN_FREQ_RANGE_5G_BAND3:
			case WL_CHAN_FREQ_RANGE_5G_BAND4:
				a[core] =  pwrdet->pwrdet_a[core][chan_freq_range];
				b[core] =  pwrdet->pwrdet_b[core][chan_freq_range];
				c[core] =  pwrdet->pwrdet_c[core][chan_freq_range];
				d[core] =  pwrdet->pwrdet_d[core][chan_freq_range];
				break;
			case WL_CHAN_FREQ_RANGE_2G_40:
			case WL_CHAN_FREQ_RANGE_5G_BAND0_40:
			case WL_CHAN_FREQ_RANGE_5G_BAND1_40:
			case WL_CHAN_FREQ_RANGE_5G_BAND2_40:
			case WL_CHAN_FREQ_RANGE_5G_BAND3_40:
			case WL_CHAN_FREQ_RANGE_5G_BAND4_40:
				/* Adjust index for 40M */
				a[core] =  pwrdet->pwrdet_a_40[core][chan_freq_range - 6];
				b[core] =  pwrdet->pwrdet_b_40[core][chan_freq_range - 6];
				c[core] =  pwrdet->pwrdet_c_40[core][chan_freq_range - 6];
				d[core] =  pwrdet->pwrdet_d_40[core][chan_freq_range - 6];
				break;
			case WL_CHAN_FREQ_RANGE_5G_BAND0_80:
			case WL_CHAN_FREQ_RANGE_5G_BAND1_80:
			case WL_CHAN_FREQ_RANGE_5G_BAND2_80:
			case WL_CHAN_FREQ_RANGE_5G_BAND3_80:
			case WL_CHAN_FREQ_RANGE_5G_BAND4_80:
				/* Adjust index for 80M */
				a[core] =  pwrdet->pwrdet_a_80[core][chan_freq_range - 12];
				b[core] =  pwrdet->pwrdet_b_80[core][chan_freq_range - 12];
				c[core] =  pwrdet->pwrdet_c_80[core][chan_freq_range - 12];
				d[core] =  pwrdet->pwrdet_d_80[core][chan_freq_range - 12];
				break;
			default:
				PHY_ERROR(("wl%d: %s: pwrdet core%d: a=%d b=%d c=%d d=%d\n",
				   pi->sh->unit, __FUNCTION__, core,
				   a[core], b[core], c[core], d[core]));
				break;
			}
		}
	}

	/* target power */
	wlc_phy_txpwrctrl_update_minpwr_acphy(pi);
	target_min_limit = pi->min_txpower * WLC_TXPWR_DB_FACTOR;

	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		int8 target_pwr_qtrdbm;
		target_pwr_qtrdbm = (int8)pi->tx_power_max_per_core[core];
		/* never target below the min threashold */
		if (target_pwr_qtrdbm < target_min_limit)
		    target_pwr_qtrdbm = target_min_limit;

		tp_qtrdbm_each_core[core_count].core = core;
		tp_qtrdbm_each_core[core_count].target_pwr_qtrdbm = target_pwr_qtrdbm;
		++core_count;
	}

	/* determine pos/neg TSSI slope */
	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		MOD_PHYREG(pi, TSSIMode, tssiPosSlope, pi->fem2g->tssipos);
	} else {
		MOD_PHYREG(pi, TSSIMode, tssiPosSlope, pi->fem5g->tssipos);
	}
	MOD_PHYREG(pi, TSSIMode, tssiPosSlope, 1);

	/* disable txpwrctrl during idleTssi measurement, etc */
	MOD_PHYREG(pi, TxPwrCtrlCmd, txPwrCtrl_en, 0);

#ifdef PREASSOC_PWRCTRL
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		init_idx_carry_from_lastchan = (CHSPEC_IS5G(pi->radio_chanspec)) ?
		    pi_ac->tpci->pwr_ctrl_save->status_idx_carry_5g[core]
		    : pi_ac->tpci->pwr_ctrl_save->status_idx_carry_2g[core];
		if (!init_idx_carry_from_lastchan) {
		    /* 4360B0 using 0.5dB-step gaintbl so start with a lower starting idx */
		    if ((RADIOREV(pi->pubpi->radiorev) == 4) ||
		       (RADIOREV(pi->pubpi->radiorev) == 8) ||
		       (ACMAJORREV_5(pi->pubpi->phy_rev))) {
			iidx = 20;
		    } else {
			iidx = 50;
		    }
			if (!pi->tpci->data->ovrinitbaseidx) {
				MOD_PHYREGCEE(pi, TxPwrCtrlInit_path, core,
						pwrIndex_init_path, iidx);
			}
		    if (CHSPEC_IS5G(pi->radio_chanspec)) {
			pi_ac->tpci->pwr_ctrl_save->status_idx_carry_5g[core] = TRUE;
		    } else {
			pi_ac->tpci->pwr_ctrl_save->status_idx_carry_2g[core] = TRUE;
		    }
		    pi_ac->tpci->txpwrindex_hw_save[core] = 128;
		} else {
		    /* set power index initial condition */
		    int32 new_iidx;

		    if (CHSPEC_IS5G(pi->radio_chanspec)) {
			iidx = pi_ac->tpci->pwr_ctrl_save->status_idx_5g[core];
			prev_target_qdbm = pi_ac->tpci->pwr_ctrl_save->pwr_qdbm_5g[core];
			if ((pi_ac->tpci->pwr_ctrl_save->stored_not_restored_5g[core])) {
			    pi_ac->tpci->pwr_ctrl_save->stored_not_restored_5g[core]
				= FALSE;
			    pi_ac->tpci->txpwrindex_hw_save[core] = 128;

			}
		    } else {
			iidx = pi_ac->tpci->pwr_ctrl_save->status_idx_2g[core];
			prev_target_qdbm = pi_ac->tpci->pwr_ctrl_save->pwr_qdbm_2g[core];
			if ((pi_ac->tpci->pwr_ctrl_save->stored_not_restored_2g[core])) {
			    pi_ac->tpci->pwr_ctrl_save->stored_not_restored_2g[core] = FALSE;
			    pi_ac->tpci->txpwrindex_hw_save[core] = 128;

			}
		    }
		    new_iidx = (int32)iidx - ((int32)tp_qtrdbm_each_core[core].target_pwr_qtrdbm
					      - prev_target_qdbm) / step_size;

		    if (new_iidx < PWRCTRL_MIN_INIT_IDX) {
			iidx = PWRCTRL_MIN_INIT_IDX;
		    } else if (new_iidx > PWRCTRL_MAX_INIT_IDX) {
			iidx = PWRCTRL_MAX_INIT_IDX;
		    } else {
			iidx = (uint8)new_iidx;
		    }
			if (!pi->tpci->data->ovrinitbaseidx) {
				MOD_PHYREGCEE(pi, TxPwrCtrlInit_path,
						core, pwrIndex_init_path, iidx);
			}
		}
	}
#else
	if ((RADIOREV(pi->pubpi->radiorev) == 4) || (RADIOREV(pi->pubpi->radiorev) == 8) ||
	    (RADIOREV(pi->pubpi->radiorev) == 10) ||
	    (RADIOREV(pi->pubpi->radiorev) == 11) ||
	    (RADIOREV(pi->pubpi->radiorev) == 13) ||
	    (ACMAJORREV_5(pi->pubpi->phy_rev))) {
		iidx = 20;
	} else {
		iidx = 50;
	}
#if defined(PHYCAL_CACHING)
	if (!ctx || !ctx->valid)
#endif /* PHYCAL_CACHING */
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			if (!pi->tpci->data->ovrinitbaseidx) {
				MOD_PHYREGCEE(pi, TxPwrCtrlInit_path,
						core, pwrIndex_init_path, iidx);
			}
		}
#endif /* PREASSOC_PWRCTRL */
	/* MOD_PHYREG(pi, TxPwrCtrlIdleTssi, rawTssiOffsetBinFormat, 1); */

	/* When to sample TSSI */
	if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		tssi_delay = 200;
		MOD_PHYREG(pi, perPktIdleTssiCtrl, perPktIdleTssiUpdate_en, 0);
		MOD_PHYREG(pi, TSSIMode, TwoPwrRange, 0);

		MOD_PHYREG(pi, TssiAccumCtrl, tssi_accum_en, 1);
		MOD_PHYREG(pi, TssiAccumCtrl, tssi_filter_pos, 1);
		MOD_PHYREG(pi, TssiAccumCtrl, Ntssi_accum_delay, 200);
		MOD_PHYREG(pi, TssiAccumCtrl, Ntssi_intg_log2, 4);

		MOD_PHYREG(pi, TssiAccumCtrlcck, Ntssi_accum_delay_cck, 200);
		MOD_PHYREG(pi, TssiAccumCtrlcck, Ntssi_intg_log2_cck, 4);

		mult_mode = 2;
		MOD_PHYREG(pi, TxPwrCtrl_Multi_Mode0, multi_mode, mult_mode);
		MOD_PHYREG(pi, TxPwrCtrl_Multi_Mode1, multi_mode, mult_mode);
		MOD_PHYREG(pi, TxPwrCtrl_Multi_Mode2, multi_mode, mult_mode);
		MOD_PHYREG(pi, TxPwrCtrl_Multi_Mode3, multi_mode, mult_mode);
	} else if (IS_4364(pi)) {
		tssi_delay = SAMPLE_TSSI_AFTER_160_SAMPLES;
		MOD_PHYREG(pi, TssiAccumCtrl, tssi_accum_en, 1);
		MOD_PHYREG(pi, TssiAccumCtrl, Ntssi_intg_log2, 4);
		MOD_PHYREG(pi, TssiAccumCtrl, Ntssi_accum_delay, tssi_delay);
		MOD_PHYREG(pi, TssiAccumCtrl, tssi_filter_pos, 1);
	} else if (CHSPEC_IS2G(pi->radio_chanspec)) {
		if (pi->tpci->data->cfg.srom_2g_pdrange_id >= 5) {
			tssi_delay = 150;
		} else if (pi->tpci->data->cfg.srom_2g_pdrange_id >= 4) {
			tssi_delay = 220;
		} else {
			tssi_delay = 150;
		}
	} else {
		if (pi->tpci->data->cfg.srom_5g_pdrange_id >= 5) {
			tssi_delay = 150;
		} else if (pi->tpci->data->cfg.srom_5g_pdrange_id >= 4) {
			tssi_delay = 220;
		} else {
			tssi_delay = 150;
		}
	}
	MOD_PHYREG(pi, TxPwrCtrlNnum, Ntssi_delay, tssi_delay);

	/* When to sample TSSI for CCK */
	if (IS_4364_1x1(pi)) {
		tssi_delay_cck = SAMPLE_TSSI_AFTER_160_SAMPLES;
		MOD_PHYREG(pi, perPktIdleTssiCtrlcck, base_index_cck_en, 1);
		MOD_PHYREG(pi, TssiAccumCtrlcck, Ntssi_accum_delay_cck, tssi_delay_cck);
		MOD_PHYREG(pi, TssiAccumCtrlcck, Ntssi_intg_log2_cck, 2);
	}

	if ((pi->tpci->data->cfg.bphy_scale != 0) &&
		!ACMAJORREV_32(pi->pubpi->phy_rev) &&
		!ACMAJORREV_33(pi->pubpi->phy_rev)) {
		MOD_PHYREG(pi, BphyControl3, bphyScale20MHz,
			pi->tpci->data->cfg.bphy_scale);
	}

#if defined(PREASSOC_PWRCTRL)
	/* average over 2 or 16 packets */
	wlc_phy_pwrctrl_shortwindow_upd_acphy(pi, pi->tpci->data->channel_short_window);
#else
	MOD_PHYREG(pi, TxPwrCtrlNnum, Npt_intg_log2, PWRCTRL_LONGW_AVG);
#endif /* PREASSOC_PWRCTRL */

	/* decouple IQ comp and LOFT comp from Power Control */
	MOD_PHYREG(pi, TxPwrCtrlCmd, use_txPwrCtrlCoefsIQ, 0);
	MOD_PHYREG(pi, TxPwrCtrlCmd, use_txPwrCtrlCoefsLO,
	   (ACREV_IS(pi->pubpi->phy_rev, 1) || ACREV_IS(pi->pubpi->phy_rev, 9)) ? 1 : 0);

#ifdef WL_SARLIMIT
	wlc_phy_set_sarlimit_acphy(pi_ac->tpci);
#endif
	while (core_count > 0) {
		--core_count;
		if (pi_ac->tpci->offset_targetpwr) {
		    uint8 tgt_pwr_qdbm = tp_qtrdbm_each_core[core_count].target_pwr_qtrdbm;
		    tgt_pwr_qdbm -= (pi_ac->tpci->offset_targetpwr << 2);
		    wlc_phy_txpwrctrl_set_target_acphy(pi, tgt_pwr_qdbm, 0);
		} else {
		    /* set target powers */
		    wlc_phy_txpwrctrl_set_target_acphy(pi, tp_qtrdbm_each_core[core_count].
						       target_pwr_qtrdbm, tp_qtrdbm_each_core
						       [core_count].core);
		}
		PHY_TXPWR(("wl%d: %s: txpwrctl[%d]: %d\n",
		           pi->sh->unit, __FUNCTION__, tp_qtrdbm_each_core[core_count].core,
		           tp_qtrdbm_each_core[core_count].target_pwr_qtrdbm));
	}
#ifdef ENABLE_FCBS
	if (IS_FCBS(pi) && pi->phy_fcbs->FCBS_INPROG)
		pi->phy_fcbs->FCBS_INPROG = 0;
	else {
#endif
		/* load estimated power tables (maps TSSI to power in dBm)
		 *    entries in tx power table 0000xxxxxx
		 */
		tbl_len = 128;

		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			wlc_phy_get_srom12_pdoffset_acphy(pi, pdoffsets);

			// the following is for debugging only
			PHY_TXPWR(("wl%d: %s: pdoffsets: ", pi->sh->unit, __FUNCTION__));
			for (idx = 0; idx < ARRAYSIZE(pdoffsets); idx++) {
				PHY_TXPWR(("%d ", pdoffsets[idx]));
			}
			PHY_TXPWR(("\n"));

			/* get PA fittype */
			pa_fittype = wlc_phy_fittype_srom12_acphy(pi);
			PHY_TXPWR(("wl%d: %s:pa_fittype = %d\n",
				pi->sh->unit, __FUNCTION__, pa_fittype));
		}
		if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
			/* 7271 meas the idle tssi added the call here because TCL does so */
			wlc_phy_txpwrctrl_idle_tssi_meas_acphy(pi);
		}
#ifdef WLC_TXCAL
		if (pi->txcali->txcal_pwr_tssi_tbl_in_use == 1) {
			wlc_phy_apply_pwr_tssi_tble_chan_acphy(pi);
		} else {
#endif		/* WLC_TXCAL */
			FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core)
			{
				PHY_TXPWR(("wl%d: %s: paparam of core%d = [%4x, %4x, %4x, %4x]\n",
					pi->sh->unit, __FUNCTION__, core,
					a[core], b[core], c[core], d[core]));

				for (idx = 0; idx < tbl_len; idx++) {
				ctrSqr[idx] = idx * idx;
				if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
					ACMAJORREV_33(pi->pubpi->phy_rev) ||
					ACMAJORREV_37(pi->pubpi->phy_rev)) {
					pwr_est = phy_ac_tpc_get_estpwrlut_srom12(
							a, b, c, d,
							pa_fittype, core, idx);

					if (using_estpwr_lut_cck) {
						pwr_est_cck = phy_ac_tpc_get_estpwrlut_srom12(
								ak, bk, ck, dk,
								pa_fittype, core, idx);
					}
				} else {
					if (wlc_phy_fittype_srom12_acphy(pi) == 0) {
						firstTerm  = (int32)a[core] * 128;
						secondTerm = ((int32)b[core] * idx) / 2;
						thirdTerm  = ((int32)c[core] * ctrSqr[idx]) / 128;
						fourthTerm = ((int32)d[core] * idx) /
							(((int32)idx - 128));
						pwr_est = MAX((firstTerm + secondTerm + thirdTerm +
						   fourthTerm) / 8192, 0);
						pwr_est = MIN(pwr_est, 0x7e);
					} else {
						firstTerm = (int32)a[core] * 16;
						secondTerm = (b[core] * ctrSqr[idx]) / 4096;
						if (idx == 0)
							thirdTerm = 0;
						else
							thirdTerm = c[core] * ctrSqr[idx] /
								(ctrSqr[idx] -
								((int32)d[core] * 2));
						pwr_est = MAX((firstTerm + secondTerm +
							thirdTerm) / 1024, 0);
						pwr_est = MIN(pwr_est, 0x7e);
					}
				}

				if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				    ACMAJORREV_33(pi->pubpi->phy_rev)) {
					/* The EstPwrShift table has 3 columns
					 * in 5G 20/40/80 and in 2G 20/40/CCK
					 */
					if (CHSPEC_IS5G(pi->radio_chanspec)) {
						pwr_est0 = pwr_est + pdoffsets[core];
						pwr_est1 = pwr_est + pdoffsets[core+PHY_CORE_MAX];
					} else {
						if (CHSPEC_IS20(pi->radio_chanspec)) {
							pwr_est0 = pwr_est;
							pwr_est1 = 0;
							if (using_estpwr_lut_cck) {
								pwr_est = pwr_est_cck;
							} else {
								pwr_est = pwr_est0
								+ pdoffsets[core+PHY_CORE_MAX];
							}
						} else {
							pwr_est1 = pwr_est;
							pwr_est0 = pwr_est1 + pdoffsets[core];
							/* CCK pdoffset in 40MHz only */
							if (using_estpwr_lut_cck) {
								pwr_est = pwr_est_cck;
							} else {
								pwr_est = pwr_est1
								+ pdoffsets[core+PHY_CORE_MAX];
							}
						}
					}

					pwr_est  = MIN(0x7f, (MAX(pwr_est,  0)));
					pwr_est0 = MIN(0x7f, (MAX(pwr_est0, 0)));
					pwr_est1 = MIN(0x7f, (MAX(pwr_est1, 0)));

					regval[idx] = ((uint32)(pwr_est0 & 0xff)) |
					        (((uint32)(pwr_est1 & 0xff)) << 8) |
					        (((uint32)(pwr_est & 0xff)) << 16);

				} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
					/* The EstPwrShift table has 3 columns but all are set
					 * the same, offsets are compensated in the estPwrShftTbl
					 */
					pwr_est  = MIN(0x7f, (MAX(pwr_est,  0)));
					regval[idx] = ((uint32)(pwr_est & 0xff)) |
					        (((uint32)(pwr_est & 0xff)) << 8) |
					        (((uint32)(pwr_est & 0xff)) << 16);
				} else {
					regval[idx] = (uint16)(pwr_est&0xff);
				}
				PHY_TXPWR(("idx = %d, pwrest = %u (0x%x) \n",
					idx, regval[idx], regval[idx]));
				}

				if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				    ACMAJORREV_33(pi->pubpi->phy_rev) ||
				    ACMAJORREV_37(pi->pubpi->phy_rev)) {
					/* Est Pwr Table is 128x24 Table. Limit Write to 8 bits */
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRLUTS(core),
						tbl_len, 0, 32, regval);
				} else {
					/* Est Pwr Table is 128x8 Table. Limit Write to 8 bits */
					wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRLUTS(core),
						tbl_len, 0, 16, regval);
				}
			}
#ifdef WLC_TXCAL
		}
#endif		/* WLC_TXCAL */
		/* start to populate estPwrShftTbl */

		if (CHSPEC_IS5G(pi->radio_chanspec)) {
		    if (CHSPEC_IS80(pi->radio_chanspec) || PHY_AS_80P80(pi, pi->radio_chanspec)) {
			chan_freq = chan_freq_range - 12;
		    } else if (CHSPEC_IS160(pi->radio_chanspec)) {
			chan_freq = chan_freq_range - 12;
			ASSERT(0);
		    } else if (CHSPEC_IS40(pi->radio_chanspec)) {
			chan_freq = chan_freq_range - 7;
		    } else {
			chan_freq = chan_freq_range - 1;
		    }
		}

		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev)) {
			/* Do not use ESTPWRSHFTLUTS table but offset ESTLUTPWR table instead */
			memset(shfttblval, 0, sizeof(shfttblval));
		} else {
			phy_ac_fill_estpwrshft_table(pi, pwrdet, chan_freq, shfttblval);
		}

		if (ACMAJORREV_32(pi->pubpi->phy_rev) || ACMAJORREV_33(pi->pubpi->phy_rev) ||
		    ACMAJORREV_37(pi->pubpi->phy_rev)) {
			wlc_phy_table_write_acphy(pi, AC2PHY_TBL_ID_ESTPWRSHFTLUTS0,
				ESTPWRSHFTLUTS_TBL_LEN, 0, 32, shfttblval);
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRSHFTLUTS,
				ESTPWRSHFTLUTS_TBL_LEN, 0, 32, shfttblval);
		}

#ifdef ENABLE_FCBS
	}
#endif

	ACPHY_ENABLE_STALL(pi, stall_val);
}

/* Get the content for the ESTPWRSHFTLUTS_TBL table */
static void
phy_ac_fill_estpwrshft_table(phy_info_t *pi, srom12_pwrdet_t *pwrdet, uint8 chan_freq,
		uint32 *shfttblval)
{
	uint8  core, idx1;
	uint8  poffs;
	uint32 pdoffs;

	PHY_TXPWR(("wl%d: %s: ESTPWRSHFTLUT table for band %d\n",
	           pi->sh->unit, __FUNCTION__, chan_freq));

	for (idx1 = 0; idx1 < ESTPWRSHFTLUTS_TBL_LEN; idx1++) {
		pdoffs = 0;
		FOREACH_CORE(pi, core) {
			if (CHSPEC_IS5G(pi->radio_chanspec)) {
				if ((idx1 == 0)||((idx1 > 6)&&(idx1 < 10))) {
					poffs = (uint8)(pwrdet->pdoffset20in80[core][chan_freq+1]);
				} else if ((idx1 == 1)||(idx1 == 6)) {
					poffs = (uint8)(pwrdet->pdoffset40in80[core][chan_freq+1]);
				} else if ((idx1 == 2) || (idx1 == 4)) {
					poffs = (uint8)(pwrdet->pdoffset20in40[core][chan_freq+1]);
				} else {
					poffs = 0;
				}
			} else {
				if (idx1 == 17) {
					/* CCK 20in20 */
					poffs = (uint8)(pwrdet->pdoffsetcck[core]);
				} else if (idx1 == 16 || idx1 == 18) {
					/* CCK 20in40 (bphy20L and bphy20U) */
					poffs = (uint8)(pwrdet->pdoffsetcck20m[core]);
				} else {
					poffs = 0;
				}
			}
			poffs = (poffs > 15) ? (0xe0 | poffs) : poffs;
			pdoffs = pdoffs | (poffs << core*8);
		}
		PHY_TXPWR(("idx = %d, pdoffs = 0x%08x\n", idx1, pdoffs));
		shfttblval[idx1] = pdoffs;
	}
}

#ifdef PREASSOC_PWRCTRL
static void
wlc_phy_pwrctrl_shortwindow_upd_acphy(phy_info_t *pi, bool shortterm)
{
	if (shortterm) {
		/* 2 packet avergaing */
		MOD_PHYREG(pi, TxPwrCtrlNnum, Npt_intg_log2, PWRCTRL_SHORTW_AVG);
		if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
			MOD_PHYREG(pi, TxPwrCtrlDamping, DeltaPwrDamp, 64);
		}
		if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
			MOD_PHYREG(pi, TxPwrCtrlNnum, Npt_intg_log2, PWRCTRL_LONGW_AVG);
		}
	} else {
		if (ACMAJORREV_4(pi->pubpi->phy_rev) || ACMAJORREV_32(pi->pubpi->phy_rev) ||
			ACMAJORREV_33(pi->pubpi->phy_rev) || ACMAJORREV_37(pi->pubpi->phy_rev)) {
			/* 4 packet avergaing with Damp of 0.25 */
			MOD_PHYREG(pi, TxPwrCtrlNnum, Npt_intg_log2, 2);
			MOD_PHYREG(pi, TxPwrCtrlDamping, DeltaPwrDamp, 16);
		} else {
			/* 16 packet avergaing */
			MOD_PHYREG(pi, TxPwrCtrlNnum, Npt_intg_log2, PWRCTRL_LONGW_AVG);

		}
	}
}

#endif /* PREASSOC_PWRCTRL */

static uint8
wlc_phy_txpwrctrl_get_target_acphy(phy_info_t *pi, uint8 core)
{
	/* set target powers in 6.2 format (in dBs) */
	switch (core) {
	case 0:
		return READ_PHYREGFLD(pi, TxPwrCtrlTargetPwr_path0, targetPwr0);
		break;
	case 1:
		return READ_PHYREGFLD(pi, TxPwrCtrlTargetPwr_path1, targetPwr1);
		break;
	case 2:
		return READ_PHYREGFLD(pi, TxPwrCtrlTargetPwr_path2, targetPwr2);
		break;
	case 3:
		return READ_PHYREGFLD(pi, TxPwrCtrlTargetPwr_path3, targetPwr3);
		break;
	}
	return 0;
}

static
void wlc_phy_get_tssi_floor_acphy(phy_info_t *pi, int16 *floor)
{
	srom11_pwrdet_t *pwrdet = pi->pwrdet_ac;
	uint8 chan_freq_range, core, core_freq_segment_map;


	FOREACH_CORE(pi, core) {
		/* core_freq_segment_map is only required for 80P80 mode.
		For other modes, it is ignored
		*/
		core_freq_segment_map = pi->u.pi_acphy->core_freq_mapping[core];

		chan_freq_range = phy_ac_chanmgr_get_chan_freq_range(pi, 0, core_freq_segment_map);

		switch (chan_freq_range) {
		case WL_CHAN_FREQ_RANGE_2G:
		case WL_CHAN_FREQ_RANGE_5G_BAND0:
		case WL_CHAN_FREQ_RANGE_5G_BAND1:
		case WL_CHAN_FREQ_RANGE_5G_BAND2:
		case WL_CHAN_FREQ_RANGE_5G_BAND3:
			floor[core] = pwrdet->tssifloor[core][chan_freq_range];
		break;
		}
	}

}

#if defined(BCMINTERNAL) || defined(WLTEST) || defined(ATE_BUILD)
void
wlc_phy_tone_pwrctrl_loop(phy_info_t *pi, int8 targetpwr_dBm)
{
	uint8 core = 0; /* presently this functionality only required in RSDB mode */
	int8 pwr;
	int8 targetpwr, tgt_pwr_qdbm;
	int16  idle_tssi[PHY_CORE_MAX], tone_tssi[PHY_CORE_MAX];
	uint16 adjusted_tssi[PHY_CORE_MAX];
	int16 a1[PHY_CORE_MAX];
	int16 b1[PHY_CORE_MAX];
	int16 b0[PHY_CORE_MAX];
	int8 postive_slope = 1;
	int8 targetidx;
	int8 tx_idx;
	int8 deltapwr;
	txgain_setting_t txgain_settings;
	int8 orig_rxfarrow_shift = 0;
	uint16 orig_RxSdFeConfig6 = 0;
	bool bbmult_interpolation;

	targetpwr = targetpwr_dBm;
	tgt_pwr_qdbm = targetpwr * 4;

	if (targetpwr == -99) {
		targetidx = -1;
	} else {
		wlc_phy_get_paparams_for_band_acphy(pi, a1, b0, b1);

		/* meas the idle tssi */
		wlc_phy_txpwrctrl_idle_tssi_meas_acphy(pi);
	    idle_tssi[core] = READ_PHYREGCE(pi, TxPwrCtrlIdleTssi_path, core) & 0x3ff;
		idle_tssi[core] = idle_tssi[core] - 1023;

		/* prevent crs trigger */
		wlc_phy_stay_in_carriersearch_acphy(pi, TRUE);

		if (!(ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)||
			ACMAJORREV_4(pi->pubpi->phy_rev))) {
			orig_rxfarrow_shift = READ_PHYREGFLD(pi, RxSdFeConfig6, rx_farrow_rshift_0);
			MOD_PHYREG(pi, RxSdFeConfig6, rx_farrow_rshift_0, 2);
		}

		if (RADIOID_IS(pi->pubpi->radioid, BCM20694_ID)) {
			/* TBD for 20694 radio */
			targetidx = -1;
			return;
		}
		if (RADIOID_IS(pi->pubpi->radioid, BCM20696_ID)) {
			/* TBD for 20696 radio */
			targetidx = -1;
			return;
		}
		if ((RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) ||
				(RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) ||
				(RADIOID_IS(pi->pubpi->radioid, BCM20695_ID))) {
			phy_ac_tssi_loopback_path_setup(pi, 0);
			if (TINY_RADIO(pi)) {
				MOD_PHYREG(pi, RxSdFeConfig1, farrow_rshift_force, 1);
				orig_RxSdFeConfig6 = READ_PHYREG(pi, RxSdFeConfig6);
				MOD_PHYREG(pi, RxSdFeConfig6, rx_farrow_rshift_0,
					READ_PHYREGFLD(pi, RxSdFeConfig1, farrow_rshift_tx));
			}
		} else
			wlc_phy_tssi_radio_setup_acphy(pi, pi->sh->hw_phyrxchain, 0);

		tx_idx = 30;
		wlapi_suspend_mac_and_wait(pi->sh->physhim);
		phy_utils_phyreg_enter(pi);
		wlc_phy_txpwrctrl_enable_acphy(pi, PHY_TPC_HW_OFF);
		wlc_phy_txpwr_by_index_acphy(pi, (1 << core), tx_idx);
		wlc_phy_get_txgain_settings_by_index_acphy(
			pi, &txgain_settings, tx_idx);
		wlc_phy_poll_samps_WAR_acphy(pi, tone_tssi,
			TRUE, FALSE, &txgain_settings, FALSE, TRUE, 0, 0);
		adjusted_tssi[core] = 1023 - postive_slope * (tone_tssi[core] - idle_tssi[core]);
		adjusted_tssi[core] = adjusted_tssi[core] >> 3;

		if (TINY_RADIO(pi)) {
			 MOD_PHYREG(pi, RxSdFeConfig1, farrow_rshift_force, 0);
			 WRITE_PHYREG(pi, RxSdFeConfig6, orig_RxSdFeConfig6);
		}
		/* prevent crs trigger */
		wlc_phy_stay_in_carriersearch_acphy(pi, TRUE);
		if (!(ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)||
			ACMAJORREV_4(pi->pubpi->phy_rev))) {
			 MOD_PHYREG(pi, RxSdFeConfig6, rx_farrow_rshift_0, orig_rxfarrow_shift);
		}
		pwr = wlc_phy_tssi2dbm_acphy(pi, adjusted_tssi[core], a1[core], b0[core], b1[core]);

		/* delta pwr in qdb */
		deltapwr = tgt_pwr_qdbm - pwr;
		if (ACMAJORREV_2(pi->pubpi->phy_rev) || ACMAJORREV_5(pi->pubpi->phy_rev)) {
			/* for 4350 with 0.5dB step size gaintable */
			targetidx = tx_idx - (deltapwr >> 1);
		} else {
		    bbmult_interpolation = READ_PHYREGFLD(pi, TxPwrCtrlCmd, bbMultInt_en);
			if (ACMAJORREV_4(pi->pubpi->phy_rev) &&
				CHSPEC_IS5G(pi->radio_chanspec)	&& bbmult_interpolation) {
				targetidx = tx_idx - (deltapwr >> 1);
			} else {
				targetidx = tx_idx - deltapwr;
			}
		}

		wlc_phy_txpwr_by_index_acphy(pi, (1 << core), targetidx);
		wlc_phy_stay_in_carriersearch_acphy(pi, TRUE);
		wlc_phy_tx_tone_acphy(pi, 2000, 181, 0, 0, FALSE);
		phy_utils_phyreg_exit(pi);
		wlapi_enable_mac(pi->sh->physhim);
		wlc_phy_stay_in_carriersearch_acphy(pi, FALSE);
	}
}
#endif /* defined(BCMINTERNAL) || defined(WLTEST) || defined(ATE_BUILD) */

static uint32
wlc_phy_pdoffset_cal_acphy(uint32 pdoffs, uint16 pdoffset, uint8 band, uint8 core)
{
	uint8 pdoffs_t;
	switch (band) {
	case WL_CHAN_FREQ_RANGE_2G:
		pdoffs_t = pdoffset & 0xf; break;
	case WL_CHAN_FREQ_RANGE_5G_BAND0:
		pdoffs_t = pdoffset & 0xf; break;
	case WL_CHAN_FREQ_RANGE_5G_BAND1:
		pdoffs_t = (pdoffset >> 4) & 0xf; break;
	case WL_CHAN_FREQ_RANGE_5G_BAND2:
		pdoffs_t = (pdoffset >> 8) & 0xf; break;
	case WL_CHAN_FREQ_RANGE_5G_BAND3:
		pdoffs_t = (pdoffset >> 12) & 0xf; break;
	default:
		pdoffs_t = pdoffset & 0xf; break;
	}

	pdoffs_t = (pdoffs_t > 7) ? (0xf0|pdoffs_t) : pdoffs_t;
	pdoffs   = pdoffs | (pdoffs_t << core*8);
	return pdoffs;

}

/* Apply min or max gain cap to tx gain table by copying/repeating last
 * entry over specified range
 */
void
wlc_phy_gaintbl_blanking(phy_info_t *pi, uint16 *tx_pwrctrl_tbl, uint8 txidxcap, bool is_max_cap)
{
	uint16 k, m, K, start, end;
	/* ACPHY has 48bit gaincode = 3 16-bit word */
	uint16 nword_per_gaincode = 3;

	K = txidxcap & 0xFF;

	if (is_max_cap) {
		start = 0;
		end = K - 1;
	} else {
		start = K + 1;
		end = 127;
	}

	for (k = start; k <= end; k++) {
		for (m = 0; m < nword_per_gaincode; m++) {
			tx_pwrctrl_tbl[k*nword_per_gaincode + m] =
			        tx_pwrctrl_tbl[K*nword_per_gaincode + m];
		}
	}
}

static bool
wlc_phy_txpwrctrl_ison_acphy(phy_info_t *pi)
{
	bool suspend = FALSE;
	bool retval;
	uint16 mask = (ACPHY_TxPwrCtrlCmd_txPwrCtrl_en_MASK(pi->pubpi->phy_rev) |
	               ACPHY_TxPwrCtrlCmd_hwtxPwrCtrl_en_MASK(pi->pubpi->phy_rev) |
	               ACPHY_TxPwrCtrlCmd_use_txPwrCtrlCoefs_MASK(pi->pubpi->phy_rev));

	/* Suspend MAC if haven't done so */
	wlc_phy_conditional_suspend(pi, &suspend);

	retval = ((READ_PHYREG(pi, TxPwrCtrlCmd) & mask) == mask);

	/* Resume MAC */
	wlc_phy_conditional_resume(pi, &suspend);

	return retval;
}

static uint8
wlc_phy_set_txpwr_clamp_acphy(phy_info_t *pi, uint8 core)
{
	uint16 idle_tssi_shift, adj_tssi_min;
	int16 tssi_floor[PHY_CORE_MAX] = {0};
	int16 idleTssi_2C = 0;
	int16 a1[PHY_CORE_MAX];
	int16 b0[PHY_CORE_MAX];
	int16 b1[PHY_CORE_MAX];
	uint8 pwr = 0;

	wlc_phy_get_tssi_floor_acphy(pi, tssi_floor);
	wlc_phy_get_paparams_for_band_acphy(pi, a1, b0, b1);

	idleTssi_2C = READ_PHYREGCE(pi, TxPwrCtrlIdleTssi_path, core) & 0x3ff;
	if (idleTssi_2C >= 512) {
		idle_tssi_shift = idleTssi_2C - 1023 - (-512);
	} else {
		idle_tssi_shift = 1023 + idleTssi_2C - 511;
	}
	idle_tssi_shift = idle_tssi_shift + 4;
	adj_tssi_min = MAX(tssi_floor[core], idle_tssi_shift);
	/* convert to 7 bits */
	adj_tssi_min = adj_tssi_min >> 3;

	pwr = wlc_phy_tssi2dbm_acphy(pi, adj_tssi_min, a1[core], b0[core], b1[core]);

	return pwr;
}

static int
wlc_phy_txpower_check_target_in_limits_acphy(phy_info_t *pi)
{
	uint8 core;
	int8 target_pwr_reg;
#ifdef WLC_TXCAL
	int8 txpwr_min;
#endif

	if (!pi->tpci->data->txpwroverride) {
		FOREACH_ACTV_CORE(pi, pi->sh->phytxchain, core) {
			/* Check if target power register value is greater than board limits */
			target_pwr_reg = wlc_phy_txpwrctrl_get_target_acphy(pi, core);
			if (target_pwr_reg > pi->tpci->data->txpwr_max_boardlim_percore[core]) {
				PHY_FATAL_ERROR_MESG(("core%d, target_pwr_reg = %d, "
					"board_lim = %d\n",
					core, target_pwr_reg,
					pi->tpci->data->txpwr_max_boardlim_percore[core]));
				PHY_FATAL_ERROR(pi, PHY_RC_TXPOWER_LIMITS);
				/* Change Target Register Value to be board_limit - backoff */
				wlc_phy_txpwrctrl_set_target_acphy(pi,
					(pi->tpci->data->txpwr_max_boardlim_percore[core] -
					PHY_TXPWRBCKOF_DEF),
					core);
			}

#ifdef WLC_TXCAL
			/* Check if min power per rate is lower than the limit with OLPC on */
			if (!pi->olpci->disable_olpc) {
				/* TXCAL Data based OLPC */
				if (pi->olpci->olpc_idx_valid && pi->olpci->olpc_idx_in_use) {
					txpwr_min = pi->tx_power_min_per_core[core];
					if ((txpwr_min <
						ACPHY_MIN_POWER_SUPPORTED_WITH_OLPC_QDBM) &&
						(txpwr_min != WL_RATE_DISABLED)) {
							PHY_FATAL_ERROR_MESG(("core%d, "
								"txpwr_min = %d\n",
								core, txpwr_min));
							PHY_FATAL_ERROR(pi, PHY_RC_TXPOWER_LIMITS);
					}
				}
			}
#endif /* WLC_TXCAL */
		}
	}
	return BCME_OK;
}

static void
wlc_phy_txpower_recalc_target_acphy(phy_info_t *pi)
{
	srom11_pwrdet_t *pwrdet = pi->pwrdet_ac;
	uint8 chan_freq_range, core;
	int16 tssifloor;

	PHY_CHANLOG(pi, __FUNCTION__, TS_ENTER, 0);
	if (ACREV_IS(pi->pubpi->phy_rev, 2)) {
		chan_freq_range = phy_ac_chanmgr_get_chan_freq_range(pi, 0, PRIMARY_FREQ_SEGMENT);

		FOREACH_CORE(pi, core) {
			tssifloor = (int16)pwrdet->tssifloor[core][chan_freq_range];
			if (tssifloor != 0) {
				wlc_phy_set_txpwr_clamp_acphy(pi, core);
			}
		}
	}
#ifdef POWPERCHANNL
	if (PWRPERCHAN_ENAB(pi)) {
		if (!(ACMAJORREV_4(pi->pubpi->phy_rev)))
			/* update the board - limits per channel if in 2G Band */
			wlc_phy_tx_target_pwr_per_channel_set_acphy(pi);
	}
#endif /* POWPERCHANNL */
	wlapi_high_update_txppr_offset(pi->sh->physhim, pi->tx_power_offset);
	/* recalc targets -- turns hwpwrctrl off */

	if (IS_4364_1x1(pi) || IS_4364_3x3(pi)) {
	   /* Disabling TPC for 4364 */
	   MOD_PHYREG(pi, TxPwrCtrlCmd, txPwrCtrl_en, 0);
	   FOREACH_CORE(pi, core) {
		  wlc_phy_txpwrctrl_enable_acphy(pi, PHY_TPC_HW_OFF);
		  wlc_phy_txpwr_by_index_acphy(pi, (1 << core), 70);
		}
	} else {
	   if (SROMREV(pi->sh->sromrev) < 12) {
		  wlc_phy_txpwrctrl_pwr_setup_acphy(pi);
	   } else {
		  wlc_phy_txpwrctrl_pwr_setup_srom12_acphy(pi);
	   }
	}
	wlc_phy_txpower_check_target_in_limits_acphy(pi);

	/* restore power control */
	wlc_phy_txpwrctrl_enable_acphy(pi, pi->txpwrctrl);

#ifdef WL_MU_TX
	if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	    ACMAJORREV_33(pi->pubpi->phy_rev) ||
	    ACMAJORREV_37(pi->pubpi->phy_rev)) {
		phy_ac_tpc_offload_ppr_to_svmp(pi, pi->tx_power_offset);
	}
#endif

	PHY_CHANLOG(pi, __FUNCTION__, TS_EXIT, 0);
}

#ifdef WL_MU_TX
static void
phy_ac_tpc_offload_ppr_to_svmp(phy_info_t *pi, ppr_t* tx_power_offset)
{

	uint8  k, m, n, bwtype, stall_val, pwr0, pwr1, mem_id;
	uint32 txbf_ppr_buff[8], svmp_addr = 0xe90;
	ppr_vht_mcs_rateset_t pwr_backoff;
	wl_tx_mode_t tx_mode[5] = {WL_TX_MODE_TXBF, WL_TX_MODE_TXBF, WL_TX_MODE_TXBF,
		WL_TX_MODE_TXBF, WL_TX_MODE_NONE};
	wl_tx_nss_t nss[5] = {WL_TX_NSS_1, WL_TX_NSS_2, WL_TX_NSS_3, WL_TX_NSS_4, WL_TX_NSS_4};
#if defined(WL_PSMX)
	uint16 shmx_ndp_pwroffs_addr = 0x80, ndp_pwroffs = 0, bfi_blk_addr = 0;
	uint16 mx_mubf_rate = 0, mx_mubf_rate_addr = 0;
#endif /* WL_PSMX */

	if (!ISACPHY(pi)) {
		return;
	}

	/* disable stall */
	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);
	BCM_REFERENCE(txbf_ppr_buff);

	mem_id = 4;
	wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_SVMPMEMS, 1, 0x8000, 32, &mem_id);
	for (k = 0; k < 3; k++) {
		// bwtype: "20IN20", "40IN40", "80IN80", "20IN40", "20IN80", "40IN80"
		if (CHSPEC_IS20(pi->radio_chanspec)) {
			bwtype = (k == 0)? 0: 0xff;
		} else if (CHSPEC_IS40(pi->radio_chanspec)) {
			bwtype = (k == 0)? 3: ((k == 1)? 1: 0xff);
		} else if (CHSPEC_IS80(pi->radio_chanspec)) {
			bwtype = (k == 0)? 4: ((k == 1)? 5: ((k == 2)? 2: 0xff));
		} else {
			bwtype = 0xff;
		}

		for (n = 0; n < 5; n++) {
			ppr_get_vht_mcs(tx_power_offset, bwtype, nss[n], tx_mode[n],
			                WL_TX_CHAINS_4, &pwr_backoff);
			for (m = 0; m < 8; m++) {

				if (2*m < WL_RATESET_SZ_VHT_MCS_P) {
					pwr0 = (pwr_backoff.pwr[2*m] >= 0)?pwr_backoff.pwr[2*m]:
						(pwr_backoff.pwr[2*m] + 256);
					pwr1 = (pwr_backoff.pwr[2*m+1] >= 0)?pwr_backoff.pwr[2*m+1]:
						(pwr_backoff.pwr[2*m+1] + 256);
					txbf_ppr_buff[m] = pwr0 | (pwr1 << 16);
				} else {
					txbf_ppr_buff[m] = 0;
				}
				if (bwtype == 0xff)
					txbf_ppr_buff[m] = 0;
			}

			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_SVMPMEMS, 8,
					svmp_addr, 32, &txbf_ppr_buff[0]);
			svmp_addr += 8;

#if defined(WL_PSMX)
			/* MU NDP pwroffs follows c5s4 */
			if (bwtype <= 2 && n == 4) {
				ndp_pwroffs = (((pwr_backoff.pwr[5] >= 0)? pwr_backoff.pwr[5]:
					(pwr_backoff.pwr[5] + 256)) >> 1) & 0x7f;
			}
#endif /* WL_PSMX */
		}
	}

#if defined(WL_PSMX)
	if (D11REV_GE(pi->sh->corerev, 64)) {
		/* write proper pwroffset for the MU NDP frames into shmx */
		bfi_blk_addr = wlapi_bmac_read_shmx(pi->sh->physhim, MX_BFI_BLK_PTR(pi));
		mx_mubf_rate_addr = shm_addr(bfi_blk_addr, shmx_ndp_pwroffs_addr);
		mx_mubf_rate = wlapi_bmac_read_shmx(pi->sh->physhim, mx_mubf_rate_addr);
		mx_mubf_rate = (mx_mubf_rate & 0xff) | (ndp_pwroffs << 8);
		wlapi_bmac_write_shmx(pi->sh->physhim, mx_mubf_rate_addr, mx_mubf_rate);
	}
#endif /* WL_PSMX */

	/* restore stall value */
	ACPHY_ENABLE_STALL(pi, stall_val);
}
#endif /* WL_MU_TX */

static void
wlc_phy_txpwrctrl_setminpwr(phy_info_t *pi)
{
	if (ISACPHY(pi)) {
		if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
			pi->min_txpower = PHY_TXPWR_MIN_ACPHY2X2;
		} else if (ACMAJORREV_1(pi->pubpi->phy_rev)) {
			if (PHY_IPA(pi)) {
				pi->min_txpower = PHY_TXPWR_MIN_ACPHY1X1IPA;
			} else {
				pi->min_txpower = PHY_TXPWR_MIN_ACPHY1X1EPA;
			}
		} else if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			pi->min_txpower = pi->olpci->olpc_thresh2g;
			pi->min_txpower_5g = pi->olpci->olpc_thresh5g;
		} else {
			pi->min_txpower = PHY_TXPWR_MIN_ACPHY;
		}
	}
}

int8
wlc_phy_txpwrctrl_update_minpwr_acphy(phy_info_t *pi)
{
	int8 mintxpwr;

	wlc_phy_txpwrctrl_setminpwr(pi);

#if defined(PHYCAL_CACHING)
	/* Update the min_txpower
	 * - equals OLPC threshold when OLPC is enabled
	 * - equals tssivisible threshold when OLPC is disabled
	 * - equals board minimum when tssivisible is not defined
	 */
	mintxpwr = wlc_phy_get_thresh_acphy(pi);
	if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
		return mintxpwr;
	}

	if (mintxpwr == WL_RATE_DISABLED) {
		/* If tssivisible is not defined
		 * use min_txpower instead.
		 */
		mintxpwr = pi->min_txpower * WLC_TXPWR_DB_FACTOR;
	}
	else {
		/* Update min_txpower to OLPC thresh/tssivisible */
		if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
			if (CHSPEC_IS2G(pi->radio_chanspec))
				pi->min_txpower = mintxpwr / WLC_TXPWR_DB_FACTOR;
			else
				pi->min_txpower_5g = mintxpwr / WLC_TXPWR_DB_FACTOR;
		} else
			pi->min_txpower = mintxpwr / WLC_TXPWR_DB_FACTOR;
	}
#else
	if (ACMAJORREV_2(pi->pubpi->phy_rev)) {
		mintxpwr = wlc_phy_tssivisible_thresh_acphy(pi);
	} else {
		mintxpwr = pi->min_txpower * WLC_TXPWR_DB_FACTOR;
	}
#endif /* PHYCAL_CACHING */
	return mintxpwr;
}

#ifdef POWPERCHANNL
void
BCMATTACHFN(wlc_phy_tx_target_pwr_per_channel_limit_acphy)(phy_info_t *pi)
{ /* Limit the max and min offset values */
	srom11_pwrdet_t *pwrdet = pi->pwrdet_ac;
	uint8 core, ch_ind;
	FOREACH_CORE(pi, core) {
		for (ch_ind = 0; ch_ind < CH20MHz_NUM_2G; ch_ind++) {
			if (pwrdet->PwrOffsets2GNormTemp[core][ch_ind] >
				PWR_PER_CH_POS_OFFSET_LIMIT_QDBM) {
				pwrdet->PwrOffsets2GNormTemp[core][ch_ind] =
					PWR_PER_CH_POS_OFFSET_LIMIT_QDBM;
			} else if (pwrdet->PwrOffsets2GNormTemp[core][ch_ind] <
					-PWR_PER_CH_NEG_OFFSET_LIMIT_QDBM) {
				pwrdet->PwrOffsets2GNormTemp[core][ch_ind] =
					-PWR_PER_CH_NEG_OFFSET_LIMIT_QDBM;
			}
			if (pwrdet->PwrOffsets2GLowTemp[core][ch_ind] >
				PWR_PER_CH_POS_OFFSET_LIMIT_QDBM) {
				pwrdet->PwrOffsets2GLowTemp[core][ch_ind] =
					PWR_PER_CH_POS_OFFSET_LIMIT_QDBM;
			} else if (pwrdet->PwrOffsets2GLowTemp[core][ch_ind] <
					-PWR_PER_CH_NEG_OFFSET_LIMIT_QDBM) {
				pwrdet->PwrOffsets2GLowTemp[core][ch_ind] =
					-PWR_PER_CH_NEG_OFFSET_LIMIT_QDBM;
			}
			if (pwrdet->PwrOffsets2GHighTemp[core][ch_ind] >
					PWR_PER_CH_POS_OFFSET_LIMIT_QDBM) {
					pwrdet->PwrOffsets2GHighTemp[core][ch_ind] =
						PWR_PER_CH_POS_OFFSET_LIMIT_QDBM;
			} else if (pwrdet->PwrOffsets2GHighTemp[core][ch_ind] <
						-PWR_PER_CH_NEG_OFFSET_LIMIT_QDBM) {
					pwrdet->PwrOffsets2GHighTemp[core][ch_ind] =
						-PWR_PER_CH_NEG_OFFSET_LIMIT_QDBM;
			}
		}
	}
}
void
wlc_phy_tx_target_pwr_per_channel_decide_run_acphy(phy_info_t *pi)
{ /* Decide if should recaculate power per channel due to temp diff */
	srom11_pwrdet_t *pwrdet = pi->pwrdet_ac;
	uint8 ch = CHSPEC_CHANNEL(pi->radio_chanspec);
	int16 temp = phy_ac_temp_get(pi->u.pi_acphy->tempi); /* previous temprature */

	if (ch > CH20MHz_NUM_2G) /* 2 GHz channels only */
		return;
	if ((temp == 255) ){ /* This value is invalid - do not decide based on temp */
		return;
	}

	/* Check if temprature measurment is in a diffrenent temprature zone, */
	/*	with margin, than the Target power settings */
	if (((temp < pwrdet->Low2NormTemp - PWR_PER_CH_TEMP_MIN_STEP) &&
		(pwrdet->CurrentTempZone != PWR_PER_CH_LOW_TEMP)) ||
		((temp > pwrdet->High2NormTemp + PWR_PER_CH_TEMP_MIN_STEP) &&
		(pwrdet->CurrentTempZone != PWR_PER_CH_HIGH_TEMP)) ||
		((temp > pwrdet->Low2NormTemp + PWR_PER_CH_TEMP_MIN_STEP) &&
		(temp < pwrdet->High2NormTemp - PWR_PER_CH_TEMP_MIN_STEP) &&
		(pwrdet->CurrentTempZone != PWR_PER_CH_NORM_TEMP))) {
			wlc_phy_txpower_recalc_target_acphy(pi);
		}
}

static void
wlc_phy_tx_target_pwr_per_channel_set_acphy(phy_info_t *pi)
{
	srom11_pwrdet_t *pwrdet = pi->pwrdet_ac;
	uint8 core, ch_ind = CHSPEC_CHANNEL(pi->radio_chanspec)-1;
	int16 temp = phy_ac_temp_get(pi->u.pi_acphy->tempi); /* Copy temprture without measuring */

	ASSERT(pi->sh->sromrev >= 11);

	if (ch_ind >= CH20MHz_NUM_2G)
		return;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	FOREACH_CORE(pi, core) {
		if ((pwrdet->Low2NormTemp != 0xff) && (temp < pwrdet->Low2NormTemp) &&
			(temp != 255)) {
			pwrdet->max_pwr[core][0] = pwrdet->max_pwr_SROM2G[core] +
				pwrdet->PwrOffsets2GLowTemp[core][ch_ind];
			pwrdet->CurrentTempZone = PWR_PER_CH_LOW_TEMP;
		} else if ((pwrdet->High2NormTemp != 0xff) && (temp > pwrdet->High2NormTemp) &&
			(temp != 255)) {
			pwrdet->max_pwr[core][0] = pwrdet->max_pwr_SROM2G[core] +
				pwrdet->PwrOffsets2GHighTemp[core][ch_ind];
			pwrdet->CurrentTempZone = PWR_PER_CH_HIGH_TEMP;
		} else {
			pwrdet->max_pwr[core][0] = pwrdet->max_pwr_SROM2G[core] +
				pwrdet->PwrOffsets2GNormTemp[core][ch_ind];
			pwrdet->CurrentTempZone = PWR_PER_CH_NORM_TEMP;
		}
		PHY_TXPWR(("wl%d: %s: core = %d  ChannelInd=%d Temprature=%d, ",
			pi->sh->unit, __FUNCTION__,
			core, ch_ind, temp));
		PHY_TXPWR(("Ch max pwr=%d, 2G max pwr =%d, Offset = %d \n",
			pwrdet->max_pwr[core][0],
			pwrdet->max_pwr_SROM2G[core],
			pwrdet->PwrOffsets2GNormTemp[core][ch_ind]));
	}
}
#endif /* POWPERCHANNL */

static void
BCMATTACHFN(phy_ac_tpc_nvram_attach)(phy_ac_tpc_info_t *tpci)
{
	uint8 i;
	phy_info_t *pi = tpci->pi;
#ifndef BOARD_FLAGS3
	uint32 bfl3; /* boardflags3 */
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
#endif

	for (i = 0; i < NUM_TXPWRINDEX_LIM; i++) {
		tpci->txpwrindexlimit[i] = (uint8) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE
			(pi, rstr_txpwrindexlimit, i, 0));
	}
	pi->sromi->precal_tx_idx = ((pi->sh->boardflags & BFL_SROM11_PRECAL_TX_IDX) != 0);
	pi->sromi->txidxcap2g = (uint8)PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_txidxcap2g, 0);
	pi->sromi->txidxcap5g = (uint8)PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_txidxcap5g, 0);

#if defined(WLC_TXCAL) || (defined(WLOLPC) && !defined(WLOLPC_DISABLED))
	pi->olpci->disable_olpc = (int8) (PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_disable_olpc, 0));
	for (i = 0; i < PHY_CORE_MAX; i++) {
		/* Tempslope is in S0.10 format */
		pi->olpci->olpc_tempslope2g[i] = (int16) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE
			(pi, rstr_olpc_tempslope2g, i, 0));
		pi->olpci->olpc_tempslope5g[i] = (int16) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE
			(pi, rstr_olpc_tempslope5g, i, 0));
	}
	/* Both olpc_thresh and olpc_anchor are in qdb format */
	pi->olpci->olpc_thresh = (int8) (PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_olpc_thresh, 0));
	/* If olpc_thresh2g/5g not present in nvram, just load them with olpc_thresh value */
	pi->olpci->olpc_thresh2g = (int8) (PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_olpc_thresh2g,
		pi->olpci->olpc_thresh));
	pi->olpci->olpc_thresh5g = (int8) (PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_olpc_thresh5g,
		pi->olpci->olpc_thresh));
	pi->olpci->olpc_anchor2g = (int8) (PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_olpc_anchor2g, 0));
	pi->olpci->olpc_anchor5g = (int8) (PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_olpc_anchor5g, 0));
	/* olpc_idx_in_use is the top level control for whether  */
	/* table based txcal anchor point txidx will be used for OLPC */
	pi->olpci->olpc_idx_in_use = (uint8) (PHY_GETINTVAR_DEFAULT_SLICE(pi,
		rstr_olpc_idx_in_use, 0));

	for (i = 0; i < 5; i++) {
		/* 2G + 5G 4 subbands */
		pi->olpci->olpc_offset[i] = (int8) (PHY_GETINTVAR_ARRAY_DEFAULT_SLICE
			(pi, rstr_olpc_offset, i, 0));
	}
#endif	/* WLC_TXCAL || (WLOLPC && !WLOLPC_DISABLED) */

#ifndef BOARD_FLAGS3
	if ((PHY_GETVAR_SLICE(pi, rstr_boardflags3)) != NULL) {
		bfl3 = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_boardflags3);
		BF3_TXGAINTBLID(pi_ac) = (bfl3 & BFL3_TXGAINTBLID) >> BFL3_TXGAINTBLID_SHIFT;
		BF3_PPR_BIT_EXT(pi_ac) = (bfl3 & BFL3_PPR_BIT_EXT) >> BFL3_PPR_BIT_EXT_SHIFT;
		BF3_2GTXGAINTBL_BLANK(pi_ac) = (bfl3 & BFL3_2GTXGAINTBL_BLANK) >>
			BFL3_2GTXGAINTBL_BLANK_SHIFT;
		BF3_5GTXGAINTBL_BLANK(pi_ac) = (bfl3 & BFL3_5GTXGAINTBL_BLANK) >>
			BFL3_5GTXGAINTBL_BLANK_SHIFT;
	} else {
		BF3_TXGAINTBLID(pi_ac) = 0;
		BF3_PPR_BIT_EXT(pi_ac) = 0;
		BF3_2GTXGAINTBL_BLANK(pi_ac) = 0;
		BF3_5GTXGAINTBL_BLANK(pi_ac) = 0;
	}
#endif /* BOARD_FLAGS3 */

	pi->sromi->dettype_2g = (pi->sh->boardflags4 & BFL4_SROM12_2G_DETTYPE) >> 1;
	pi->sromi->dettype_5g = (pi->sh->boardflags4 & BFL4_SROM12_5G_DETTYPE) >> 2;
	pi->sromi->sr13_dettype_en = (pi->sh->boardflags4 & BFL4_SROM13_DETTYPE_EN) >> 3;
	pi->sromi->sr13_cck_spur_en = (pi->sh->boardflags4 & BFL4_SROM13_CCK_SPUR_EN) >> 4;
	pi->sromi->sr13_1p5v_cbuck = ((pi->sh->boardflags4 & BFL4_SROM13_1P5V_CBUCK) != 0);
	pi->sromi->epa_on_during_txiqlocal = ((pi->sh->boardflags2 &
	    BFL2_SROM11_EPA_ON_DURING_TXIQLOCAL) != 0);
	pi->tpci->data->tx_pwr_ctrl_damping_en	= (int8) (PHY_GETINTVAR_DEFAULT_SLICE(pi,
		rstr_pwrdampingen, 0));
}

/* ********************************************* */
/*				External Definitions					*/
/* ********************************************* */

uint8
wlc_phy_tssi2dbm_acphy(phy_info_t *pi, int32 tssi, int32 a1, int32 b0, int32 b1)
{
		int32 num, den;
		int8 pwrest = 0;
		uint8 core;

		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			num = 8*(16*b0+b1*tssi);
			den = 32768+a1*tssi;
			if (IBOARD(pi)) {
				pwrest = MAX(((4*num+den/2)/den), -60);
				pwrest = MIN(pwrest, 0x28);
			} else {
				pwrest = MAX(((4*num+den/2)/den), -8);
				if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
				    ACMAJORREV_33(pi->pubpi->phy_rev) ||
				    ACMAJORREV_37(pi->pubpi->phy_rev)) {
					pwrest = MIN(pwrest, 0x7f);
				} else {
					pwrest = MIN(pwrest, 0x7e);
				}
			}
		}
		return pwrest;
}

void
wlc_phy_read_txgain_acphy(phy_info_t *pi)
{
	uint8 core;
	uint8 stall_val;
	txgain_setting_t txcal_txgain[4];
	uint16 lpfgain;

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);

	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		/* store off orig tx radio gain */
		if (core == 3) {
			wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x501, 16,
			                         &(txcal_txgain[core].rad_gain));
			wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x504, 16,
			                         &(txcal_txgain[core].rad_gain_mi));
			wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x507, 16,
			                         &(txcal_txgain[core].rad_gain_hi));
			if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
				// Tx BQ2 gain is in a 3-bit field at different location
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, 0x51e, 16,
				                         &lpfgain);
				txcal_txgain[core].rad_gain &= 0xFF0F;
				txcal_txgain[core].rad_gain |= (lpfgain & 0x0007) << 4;
			}
		} else {
			wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, (0x100 + core), 16,
			                         &(txcal_txgain[core].rad_gain));
			wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, (0x103 + core), 16,
			                         &(txcal_txgain[core].rad_gain_mi));
			wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1, (0x106 + core), 16,
			                         &(txcal_txgain[core].rad_gain_hi));
			if (ACMAJORREV_37(pi->pubpi->phy_rev) ||
			    ACMAJORREV_40(pi->pubpi->phy_rev)) {
				// Tx BQ2 gain is in a 3-bit field at different location
				wlc_phy_table_read_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
						(0x17e + 16*core), 16, &lpfgain);
				txcal_txgain[core].rad_gain &= 0xFF0F;
				txcal_txgain[core].rad_gain |= (lpfgain & 0x0007) << 4;
			}
		}
		wlc_phy_get_tx_bbmult_acphy(pi, &(txcal_txgain[core].bbmult),  core);
		PHY_NONE(("\n radio gain = 0x%x%x%x, bbm=%d  \n",
			txcal_txgain[core].rad_gain_hi,
			txcal_txgain[core].rad_gain_mi,
			txcal_txgain[core].rad_gain,
			txcal_txgain[core].bbmult));
	}
	ACPHY_ENABLE_STALL(pi, stall_val);
}

void
wlc_phy_txpwr_by_index_acphy(phy_info_t *pi, uint8 core_mask, int8 txpwrindex)
{
	uint8 core;
	uint8 stall_val;
	bool suspend = FALSE;

	/* Suspend MAC if haven't done so */
	wlc_phy_conditional_suspend(pi, &suspend);

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	(void) wlc_phy_set_txpwr_by_index_acphy(pi, core_mask, txpwrindex);

	stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	ACPHY_DISABLE_STALL(pi);

	/* Set tx power based on an input "index"
	 * (Emulate what HW power control would use for a given table index)
	 */
	FOREACH_ACTV_CORE(pi, core_mask, core) {

		/* Check txprindex >= 0 */
		if (txpwrindex < 0)
			ASSERT(0); /* negative index not supported */

		if (PHY_PAPDEN(pi)) {
			if ((pi->acphy_txpwrctrl == PHY_TPC_HW_OFF) ||
				(TINY_RADIO(pi))|| ACMAJORREV_36(pi->pubpi->phy_rev))  {
				int16 rfPwrLutVal;
				uint8 rfpwrlut_table_ids[] = { ACPHY_TBL_ID_RFPWRLUTS0,
					ACPHY_TBL_ID_RFPWRLUTS1, ACPHY_TBL_ID_RFPWRLUTS2};

				if ((!TINY_RADIO(pi)) && (!ACMAJORREV_36(pi->pubpi->phy_rev))) {
					MOD_PHYREGCEE(pi, EpsilonTableAdjust, core, epsilonOffset,
					0);
				}
				MOD_PHYREGCEE(pi, PapdEnable, core, gain_dac_rf_override,
					1);
				wlc_phy_table_read_acphy(pi, rfpwrlut_table_ids[core],
					1, txpwrindex, 16, &rfPwrLutVal);
				MOD_PHYREGCEE(pi, PapdEnable, core, gain_dac_rf_reg,
					rfPwrLutVal);
#ifdef WL_ETMODE
				if (ET_ENAB(pi)) {
					int etflag;
					etflag = (int8) pi->ff->_et;
					/* For Envelope Tracking supported cases only...
					 * There is a bug in 43012, which necessitates a negation...
					 * rf_pwr_ovrd. This needs to be in for TPC OFF when ...
					 * Envelope Tracking is enabled/ operating in ET mode
					 */
					if (etflag > 0) {
						int16 rfPwrLutVal;
						uint8 rfpwrlut_table_ids[] =
							{ ACPHY_TBL_ID_RFPWRLUTS0,
							ACPHY_TBL_ID_RFPWRLUTS1,
							ACPHY_TBL_ID_RFPWRLUTS2};
						wlc_phy_table_read_acphy(pi,
							rfpwrlut_table_ids[core],
							1, txpwrindex, 16, &rfPwrLutVal);
						MOD_PHYREGCE(pi, rf_pwr_ovrd, core, rf_pwr_ovrd, 1);
						MOD_PHYREGCE(pi, rf_pwr_ovrd, core,
							rf_pwr_ovrd_value, -1*rfPwrLutVal);
					}
				}
#endif /* WL_ETMODE */
			}
		}

		/* Update the per-core state of power index */
		pi->u.pi_acphy->txpwrindex[core] = txpwrindex;
	}
	ACPHY_ENABLE_STALL(pi, stall_val);
	/* Resume MAC */
	wlc_phy_conditional_resume(pi, &suspend);
}

void
wlc_phy_get_txgain_settings_by_index_acphy(phy_info_t *pi, txgain_setting_t *txgain_settings,
                                     int8 txpwrindex)
{
	uint16 txgain[3];
	wlc_phy_table_read_acphy(pi, wlc_phy_get_tbl_id_gainctrlbbmultluts(pi, 0), 1,
		txpwrindex, 48, &txgain);

	txgain_settings->rad_gain    = ((txgain[0] >> 8) & 0xff) + ((txgain[1] & 0xff) << 8);
	txgain_settings->rad_gain_mi = ((txgain[1] >> 8) & 0xff) + ((txgain[2] & 0xff) << 8);
	txgain_settings->rad_gain_hi = ((txgain[2] >> 8) & 0xff);
	txgain_settings->bbmult      = (txgain[0] & 0xff);
}

void
wlc_phy_get_tx_bbmult_acphy(phy_info_t *pi, uint16 *bb_mult, uint16 core)
{
	uint16 tbl_ofdm_offset[] = { 99, 103, 107, 111};
	uint8 iqlocal_tbl_id = wlc_phy_get_tbl_id_iqlocal(pi, core);

	/* JIRA: SW4349-130 */
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		core = 0;
	}

	wlc_phy_table_read_acphy(pi, iqlocal_tbl_id, 1,
	                         tbl_ofdm_offset[core], 16,
	                         bb_mult);
}

void
wlc_phy_set_tx_bbmult_acphy(phy_info_t *pi, uint16 *bb_mult, uint16 core)
{
	uint16 tbl_ofdm_offset[] = { 99, 103, 107, 111};
	uint16 tbl_bphy_offset[] = {115, 119, 123, 127};
	uint8 stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);
	uint8 iqlocal_tbl_id = wlc_phy_get_tbl_id_iqlocal(pi, core);

	ACPHY_DISABLE_STALL(pi);

	/* JIRA: SW4349-130 */
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		core = 0;
	}

	wlc_phy_table_write_acphy(pi, iqlocal_tbl_id, 1,
	                          tbl_ofdm_offset[core], 16,
	                          bb_mult);
	wlc_phy_table_write_acphy(pi, iqlocal_tbl_id, 1,
	                          tbl_bphy_offset[core], 16,
	                          bb_mult);
	ACPHY_ENABLE_STALL(pi, stall_val);
}

uint32
wlc_phy_txpwr_idx_get_acphy(phy_info_t *pi)
{
	uint8 core;
	uint32 pwr_idx[] = {0, 0, 0, 0};
	uint32 tmp = 0;
	bool suspend = FALSE;

	/* Suspend MAC if haven't done so */
	wlc_phy_conditional_suspend(pi, &suspend);

	if (wlc_phy_txpwrctrl_ison_acphy(pi)) {
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			pwr_idx[core] = READ_PHYREGFLDCE(pi, TxPwrCtrlStatus_path, core, baseIndex);
		}
	} else {
		FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
			pwr_idx[core] = (pi->u.pi_acphy->txpwrindex[core] & 0xff);
		}
	}
	tmp = (pwr_idx[3] << 24) | (pwr_idx[2] << 16) | (pwr_idx[1] << 8) | pwr_idx[0];

	/* Resume MAC */
	wlc_phy_conditional_resume(pi, &suspend);
	return tmp;
}

void
wlc_phy_txpwrctrl_enable_acphy(phy_info_t *pi, uint8 ctrl_type)
{
	phy_ac_tpc_info_t *tpci = (phy_ac_tpc_info_t *)pi->u.pi_acphy->tpci;
	uint16 mask;
	uint8 core;
	bool suspend = FALSE;

	/* Suspend MAC if haven't done so */
	wlc_phy_conditional_suspend(pi, &suspend);

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	/* check for recognized commands */
	switch (ctrl_type) {
	case PHY_TPC_HW_OFF:
	case PHY_TPC_HW_ON:
		pi->txpwrctrl = ctrl_type;
		break;
	default:
		PHY_ERROR(("wl%d: %s: Unrecognized ctrl_type: %d\n",
			pi->sh->unit, __FUNCTION__, ctrl_type));
		break;
	}

	if (ctrl_type == PHY_TPC_HW_OFF) {
		/* save previous txpwr index if txpwrctl was enabled */
		if (wlc_phy_txpwrctrl_ison_acphy(pi)) {
			FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
				tpci->txpwrindex_hw_save[core] =
					READ_PHYREGFLDCE(pi, TxPwrCtrlStatus_path, core, baseIndex);
				PHY_TXPWR(("wl%d: %s PWRCTRL: %d Cache Baseindex: %d Core: %d\n",
					pi->sh->unit, __FUNCTION__, ctrl_type,
					tpci->txpwrindex_hw_save[core], core));
			}
		}
		tpci->txpwrindex_hw_save_chan = CHSPEC_CHANNEL(pi->radio_chanspec);
		/* Disable hw txpwrctrl */
		mask = ACPHY_TxPwrCtrlCmd_txPwrCtrl_en_MASK(pi->pubpi->phy_rev) |
		       ACPHY_TxPwrCtrlCmd_hwtxPwrCtrl_en_MASK(pi->pubpi->phy_rev) |
		       ACPHY_TxPwrCtrlCmd_use_txPwrCtrlCoefs_MASK(pi->pubpi->phy_rev);
		_PHY_REG_MOD(pi, ACPHY_TxPwrCtrlCmd(pi->pubpi->phy_rev), mask, 0);

	} else {
		/* Setup the loopback path here */
		phy_ac_tssi_loopback_path_setup(pi, LOOPBACK_FOR_TSSICAL);
		if (!IBOARD(pi)) {
			/* Enable hw txpwrctrl */
			mask = ACPHY_TxPwrCtrlCmd_txPwrCtrl_en_MASK(pi->pubpi->phy_rev) |
				ACPHY_TxPwrCtrlCmd_hwtxPwrCtrl_en_MASK(pi->pubpi->phy_rev) |
				ACPHY_TxPwrCtrlCmd_use_txPwrCtrlCoefs_MASK(pi->pubpi->phy_rev);
			_PHY_REG_MOD(pi, ACPHY_TxPwrCtrlCmd(pi->pubpi->phy_rev), mask, mask);
			FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
#if defined(WLC_TXCAL)
				ASSERT(pi->olpci != NULL);
				if (tpci->ti->data->txpwroverride && pi->olpci->olpc_idx_valid &&
					pi->olpci->olpc_idx_in_use) {
					/* If in override mode and table based txcal */
					/* olpc anchor power tx idx is valid, */
					/* set init idx based on anchor power tx idx */
					/* and target power */
					uint8 iidx = 0;
					wlc_phy_olpc_idx_tempsense_comp_acphy(pi, &iidx, core);
					MOD_PHYREGCEE(pi, TxPwrCtrlInit_path, core,
						pwrIndex_init_path, iidx);
				} else {
					if ((tpci->txpwrindex_hw_save_chan !=
						CHSPEC_CHANNEL(pi->radio_chanspec)) &&
						pi->olpci->olpc_idx_valid &&
						pi->olpci->olpc_idx_in_use) {
						/* If the saved txpwrindex is for another channel
						   Do not reapply it to prevent the baseindex
						   for txcal based olpc gets overwritten
						*/
						tpci->txpwrindex_hw_save[core] = 128;
					}
#endif /* WLC_TXCAL */
					if (tpci->txpwrindex_hw_save[core] != 128) {
						wlc_phy_txpwrctrl_set_baseindex(pi, core,
							tpci->txpwrindex_hw_save[core], 1);
						PHY_TXPWR(("wl%d: %s PWRCTRL: %d Restore Baseindex:"
							"%d Core: %d\n",
							pi->sh->unit, __FUNCTION__,
							ctrl_type,
							tpci->txpwrindex_hw_save[core],
							core));
					}
					if (TINY_RADIO(pi) &&
						(tpci->txpwrindex_cck_hw_save[core] != 128)) {
						wlc_phy_txpwrctrl_set_baseindex(pi, core,
							tpci->txpwrindex_cck_hw_save[core], 0);
						PHY_TXPWR(("wl%d: %s PWRCTRL: %d Restore"
							" Baseindex cck:"
							"%d Core: %d\n",
							pi->sh->unit, __FUNCTION__,
								ctrl_type,
							tpci->txpwrindex_cck_hw_save[core],
								core));
					}
#if defined(WLC_TXCAL)
				}
#endif /* WLC_TXCAL */
			}
		}

		if (!TINY_RADIO(pi)) {
			FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
				MOD_PHYREGCEE(pi, PapdEnable, core, gain_dac_rf_override, 0);
#ifdef WL_ETMODE
				if (ET_ENAB(pi)) {
					MOD_PHYREGCE(pi, rf_pwr_ovrd, core, rf_pwr_ovrd, 0);
				}
#endif /* WL_ETMODE */
			}
		}
	}

	if (TINY_RADIO(pi)) {
		uint16 txPwrCtrlCmd = READ_PHYREGFLD(pi, TxPwrCtrlCmd, hwtxPwrCtrl_en);
		/* set phyreg(PapdEnable$core.gain_dac_rf_override$core)
		 * [expr !$phyreg(TxPwrCtrlCmd.hwtxPwrCtrl_en)]
		 */
		FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
			MOD_PHYREGCEE(pi, PapdEnable, core, gain_dac_rf_override,
				(txPwrCtrlCmd ? 0 : 1));
		}
	}
	/* Resume MAC */
	wlc_phy_conditional_resume(pi, &suspend);
}

void phy_ac_tpc_ipa_upd(phy_info_t *pi)
{
	pi->ipa2g_on = ((pi->epagain2g == 2) || (pi->extpagain2g == 2));
	pi->ipa5g_on = ((pi->epagain5g == 2) || (pi->extpagain5g == 2));
}

static bool
phy_ac_tpc_hw_ctrl_get(phy_type_tpc_ctx_t *ctx)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	return pi->txpwrctrl;
}

static void
phy_ac_tpc_set(phy_type_tpc_ctx_t *ctx, ppr_t *reg_pwr)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	uint8 tx_pwr_ctrl_state = pi->txpwrctrl;
	wlc_phy_txpwrctrl_enable_acphy(pi, PHY_TPC_HW_OFF);
	wlc_phy_txpower_recalc_target(pi, reg_pwr, NULL);
	wlc_phy_txpwrctrl_enable_acphy(pi, tx_pwr_ctrl_state);
	wlc_phy_set_tssisens_lim_acphy(pi, TRUE);
}

static void
phy_ac_tpc_set_flags(phy_type_tpc_ctx_t *ctx, phy_tx_power_t *power)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	power->rf_cores = PHYCORENUM(pi->pubpi->phy_corenum);
	power->flags |= (WL_TX_POWER_F_MIMO);
	if (pi->txpwrctrl == PHY_TPC_HW_ON)
		power->flags |= (WL_TX_POWER_F_ENABLED | WL_TX_POWER_F_HW);
}

static void
phy_ac_tpc_set_max(phy_type_tpc_ctx_t *ctx, phy_tx_power_t *power)
{
	uint8 core;
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	/* Store the maximum target power among all rates */
	FOREACH_CORE(pi, core) {
		power->tx_power_max[core] = pi->tx_power_max_per_core[core];
#ifdef WL_SARLIMIT
		power->SARLIMIT[core] = pi->tpci->data->sarlimit[core];
#endif
	}
}

/* set txgain in case txpwrctrl is disabled (fixed power) */
void
wlc_phy_txpwr_fixpower_acphy(phy_info_t *pi)
{
	uint8 core;

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	PHY_CHANLOG(pi, __FUNCTION__, TS_ENTER, 0);
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		wlc_phy_txpwr_by_index_acphy(pi, (1 << core), pi->u.pi_acphy->txpwrindex[core]);
	}
	PHY_CHANLOG(pi, __FUNCTION__, TS_EXIT, 0);
}

#ifdef PPR_API
/* add 1MSB to represent 5bit-width ppr value, for mcs10 and mcs11 only */
void
wlc_phy_txpwr_ppr_bit_ext_srom13_mcs8to11(phy_info_t *pi, ppr_vht_mcs_rateset_t* vht, uint8 bshift)
{
	const struct srom13_ppr *sr13 = &pi->ppr->u.sr13;
	uint8 i;
	uint8 ppr_bit_ext, msb = 0;

	ppr_bit_ext = 1;

	if (ppr_bit_ext) {
		/* msb: bit 3 for mcs11, bit 2 for mcs10, bit 1 for mcs9, bit 0 for mcs8 */
		for (i = 0; i < MCS_PPREXP_GROUP; i++) {
			msb |= (((sr13->ppmcsexp[i] >> bshift) & 0x1) << i);
		}

		/* this added 1MSB is the 4th bit, so left shift 4 bits
		* then left shift 1 more bit since boardlimit is 0.5dB format
		* bit 0-3 for mcs8-11
		*/
		vht->pwr[PPREXP_MCS8]  -= ((msb & 0x1) << 4) << 1;
		vht->pwr[PPREXP_MCS9]  -= ((msb & 0x2) << 3) << 1;
		if (sizeof(*vht) > WL_RATESET_SZ_VHT_MCS) {
			vht->pwr[PPREXP_MCS_P_10] -= ((msb & 0x4) << 2) << 1;
			vht->pwr[PPREXP_MCS_P_11] -= ((msb & 0x8) << 1) << 1;
		}
	}
}

/* for MCS20&40_2G case, 10 rates only */
static void
wlc_phy_txpwr_srom11_convert_mcs_2g(uint32 po, uint8 nibble,
         uint8 tmp_max_pwr, ppr_vht_mcs_rateset_t* vht) {
	uint8 i;
	int8 offset;
	offset = (nibble + 8)%16 - 8;

	for (i = 0; i < WL_RATESET_SZ_VHT_MCS; i++) {
		if ((i == 1)||(i == 2)) {
			vht->pwr[i] = vht->pwr[0];
		} else {
			vht->pwr[i] = tmp_max_pwr - ((po & 0xf)<<1);
			po = po >> 4;
		}
	}
	vht->pwr[1] -= (offset << 1);
	vht->pwr[2] = vht->pwr[1];
}

#ifdef WL11AC

/* for MCS10/11 cases, 2 rates only */
static void
wlc_phy_txpwr_srom13_ext_1024qam_convert_mcs_2g(uint16 po, chanspec_t chanspec,
         uint8 tmp_max_pwr, ppr_vht_mcs_rateset_t* vht) {

	if (!(sizeof(*vht) > WL_RATESET_SZ_VHT_MCS)) {
		PHY_ERROR(("%s: should not call me this file without VHT MCS10/11 supported!\n",
			__FUNCTION__));
		ASSERT(0);
		return;
	}

	if (CHSPEC_IS20(chanspec)) {
		vht->pwr[PPREXP_MCS_P_10] = tmp_max_pwr - ((po & 0xf) << 1);
		vht->pwr[PPREXP_MCS_P_11] = tmp_max_pwr - (((po >> 4) & 0xf) << 1);
	} else if (CHSPEC_IS40(chanspec)) {
		vht->pwr[PPREXP_MCS_P_10] = tmp_max_pwr - (((po >> 8) & 0xf) << 1);
		vht->pwr[PPREXP_MCS_P_11] = tmp_max_pwr - (((po >> 12) & 0xf) << 1);
	}
}
#endif /* WL11AC */

/* for 2G Legacy 40Dup mode, providing the base pwr */
static void
wlc_phy_txpwr_srom11_convert_ofdm_2g_dup40(uint32 po, uint8 nibble,
         uint8 tmp_max_pwr, ppr_ofdm_rateset_t* ofdm) {
	uint8 i;
	int8 offset;
	offset = (nibble + 8)%16 - 8;
	for (i = 0; i < WL_RATESET_SZ_OFDM; i++) {
		if ((i == 1)||(i == 2)||(i == 3)) {
			ofdm->pwr[i] = ofdm->pwr[0];
		} else {
			ofdm->pwr[i] = tmp_max_pwr - ((po & 0xf) <<1);
			po = po >> 4;
		}
	}
	ofdm->pwr[2] -= (offset << 1);
	ofdm->pwr[3] = ofdm->pwr[2];
}

/* for ofdm20in40_2G case, 8 rates only */
static void
wlc_phy_txpwr_srom11_convert_ofdm_offset(ppr_ofdm_rateset_t* po,
                                         uint8 nibble2, ppr_ofdm_rateset_t* ofdm)
{
	uint8 i;
	int8 offsetL, offsetH;
	offsetL = ((nibble2 & 0xf) + 8)%16 - 8;
	offsetH = (((nibble2>>4) & 0xf) + 8)%16 - 8;
	for (i = 0; i < WL_RATESET_SZ_OFDM; i++) {
		if (i < 6)
			ofdm->pwr[i] = po->pwr[i] + (offsetL << 1);
		else
			ofdm->pwr[i] = po->pwr[i] + (offsetH << 1);
	}
}

/* for mcs20in40_2G case, 10 rates only */
static void
wlc_phy_txpwr_srom11_convert_mcs_offset(ppr_vht_mcs_rateset_t* po,
                                        uint8 nibble2, ppr_vht_mcs_rateset_t* vht)
{
	uint8 i;
	int8 offsetL, offsetH;
	offsetL = ((nibble2 & 0xf) + 8)%16 - 8;
	offsetH = (((nibble2>>4) & 0xf) + 8)%16 - 8;
	for (i = 0; i < sizeof(*vht); i++) {
		if (i < 5)
			vht->pwr[i] = po->pwr[i] + (offsetL << 1);
		else
			vht->pwr[i] = po->pwr[i] + (offsetH << 1);
	}
}

#ifdef BAND5G
/* for ofdm20_5G case, 8 rates only */
static void
wlc_phy_txpwr_srom11_convert_ofdm_5g(uint32 po, uint8 nibble,
         uint8 tmp_max_pwr, ppr_ofdm_rateset_t* ofdm) {
	uint8 i;
	int8 offset;
	offset = (nibble + 8)%16 - 8;
	for (i = 0; i < WL_RATESET_SZ_OFDM; i++) {
		if ((i == 1)||(i == 2)||(i == 3)) {
			ofdm->pwr[i] = ofdm->pwr[0];
		} else {
			ofdm->pwr[i] = tmp_max_pwr - ((po & 0xf) <<1);
			po = po >> 4;
		}
	}
	ofdm->pwr[2] -= (offset << 1);
	ofdm->pwr[3] = ofdm->pwr[2];
}

/* for MCS20&40_5G case, 10 rates only */
static void
wlc_phy_txpwr_srom11_convert_mcs_5g(uint32 po, uint8 nibble,
         uint8 tmp_max_pwr, ppr_vht_mcs_rateset_t* vht) {
	uint8 i;
	int8 offset;
	uint32 shift_check = 0;
	offset = (nibble + 8)%16 - 8;
	for (i = 0; i < sizeof(*vht); i++) {
		if ((i == 1)||(i == 2)) {
			vht->pwr[i] = vht->pwr[0];
		} else {
			if (++shift_check > sizeof(po)*2)
				break;
			vht->pwr[i] = tmp_max_pwr - ((po & 0xf)<<1);
			po = po >> 4;
		}
	}
	vht->pwr[1] -= (offset << 1);
	vht->pwr[2] = vht->pwr[1];
}

#ifdef WL11AC
/* for MCS10/11 cases, 2 rates only */
static void
wlc_phy_txpwr_srom13_ext_1024qam_convert_mcs_5g(uint32 po, chanspec_t chanspec,
	uint8 tmp_max_pwr, ppr_vht_mcs_rateset_t* vht) {
	if (!(sizeof(*vht) > WL_RATESET_SZ_VHT_MCS)) {
		PHY_ERROR(("%s: should not call me this file without VHT MCS10/11 supported!\n",
			__FUNCTION__));
		ASSERT(0);
		return;
	}
	if (CHSPEC_IS20(chanspec)) {
		vht->pwr[PPREXP_MCS_P_10] = tmp_max_pwr - ((po & 0xf) << 1);
		vht->pwr[PPREXP_MCS_P_11] = tmp_max_pwr - (((po >> 4) & 0xf) << 1);
	} else if (CHSPEC_IS40(chanspec)) {
		vht->pwr[PPREXP_MCS_P_10] = tmp_max_pwr - (((po >> 8) & 0xf) << 1);
		vht->pwr[PPREXP_MCS_P_11] = tmp_max_pwr - (((po >> 12) & 0xf) << 1);
	} else if (CHSPEC_IS80(chanspec)) {
		vht->pwr[PPREXP_MCS_P_10] = tmp_max_pwr - (((po >> 16) & 0xf) << 1);
		vht->pwr[PPREXP_MCS_P_11] = tmp_max_pwr - (((po >> 20) & 0xf) << 1);
	} else { /* when we are ready to BU 80p80 chanspec, settings have to be updated */
		vht->pwr[PPREXP_MCS_P_10] = tmp_max_pwr - (((po >> 24) & 0xf) << 1);
		vht->pwr[PPREXP_MCS_P_11] = tmp_max_pwr - (((po >> 28) & 0xf) << 1);
	}
}
#endif /* WL11AC */
#endif /* BAND5G */

static void
wlc_phy_ppr_set_mcs(ppr_t* tx_srom_max_pwr, uint8 bwtype,
          ppr_vht_mcs_rateset_t* pwr_offsets, phy_info_t *pi) {
		int8 tmp_mcs8, tmp_mcs9;

	ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_1, WL_TX_MODE_NONE,
		WL_TX_CHAINS_1, (const ppr_vht_mcs_rateset_t*)pwr_offsets);

	if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
		/* for vht_S1x2_CDD */
		ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_1, WL_TX_MODE_CDD,
			WL_TX_CHAINS_2, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
		/* for vht_S2x2_STBC */
		ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_2, WL_TX_MODE_STBC,
			WL_TX_CHAINS_2, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
		/* for vht_S2x2_SDM */
		ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_2, WL_TX_MODE_NONE,
			WL_TX_CHAINS_2, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
#if defined(WL_BEAMFORMING) && !defined(WLTXBF_DISABLED)
		/* for vht_S1x2_TXBF */
		ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_1, WL_TX_MODE_TXBF,
			WL_TX_CHAINS_2, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
		tmp_mcs8 = pwr_offsets->pwr[8];
		tmp_mcs9 = pwr_offsets->pwr[9];
		pwr_offsets->pwr[8] = WL_RATE_DISABLED;
		pwr_offsets->pwr[9] = WL_RATE_DISABLED;
		/* for vht_S2x2_TXBF */
		/* VHT8SS2_TXBF0 and VHT9SS2_TXBF0 are invalid */
		ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_2, WL_TX_MODE_TXBF,
			WL_TX_CHAINS_2, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
		pwr_offsets->pwr[8] = tmp_mcs8;
		pwr_offsets->pwr[9] = tmp_mcs9;
#endif /* WL_BEAMFORMING && !WLTXBF_DISABLED */
		if (PHYCORENUM(pi->pubpi->phy_corenum) > 2) {
			/* for vht_S1x3_CDD */
			ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_1, WL_TX_MODE_CDD,
				WL_TX_CHAINS_3, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
			/* for vht_S2x3_STBC */
			ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_2, WL_TX_MODE_STBC,
				WL_TX_CHAINS_3, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
			/* for vht_S2x3_SDM */
			ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_2, WL_TX_MODE_NONE,
				WL_TX_CHAINS_3, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
			/* for vht_S3x3_SDM */
			ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_3, WL_TX_MODE_NONE,
				WL_TX_CHAINS_3, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
#if defined(WL_BEAMFORMING) && !defined(WLTXBF_DISABLED)
			/* for vht_S1x3_TXBF */
			ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_1, WL_TX_MODE_TXBF,
				WL_TX_CHAINS_3, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
			/* for vht_S2x3_TXBF */
			ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_2, WL_TX_MODE_TXBF,
				WL_TX_CHAINS_3, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
			tmp_mcs8 = pwr_offsets->pwr[8];
			tmp_mcs9 = pwr_offsets->pwr[9];
			pwr_offsets->pwr[8] = WL_RATE_DISABLED;
			pwr_offsets->pwr[9] = WL_RATE_DISABLED;
			/* for vht_S3x3_TXBF */
			/* VHT8SS3_TXBF0 and VHT9SS3_TXBF0 are invalid */
			ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_3, WL_TX_MODE_TXBF,
				WL_TX_CHAINS_3, (const ppr_vht_mcs_rateset_t*)pwr_offsets);
			pwr_offsets->pwr[8] = tmp_mcs8;
			pwr_offsets->pwr[9] = tmp_mcs9;
#endif /* WL_BEAMFORMING && !WLTXBF_DISABLED */
			if (PHYCORENUM(pi->pubpi->phy_corenum) > 3) {
				/* for vht_S1x4_CDD */
				ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_1,
					WL_TX_MODE_CDD, WL_TX_CHAINS_4,
					(const ppr_vht_mcs_rateset_t*)pwr_offsets);
				/* for vht_S2x4_STBC */
				ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_2,
					WL_TX_MODE_STBC, WL_TX_CHAINS_4,
					(const ppr_vht_mcs_rateset_t*)pwr_offsets);
				/* for vht_S2x4_SDM */
				ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_2,
					WL_TX_MODE_NONE, WL_TX_CHAINS_4,
					(const ppr_vht_mcs_rateset_t*)pwr_offsets);
				/* for vht_S3x4_SDM */
				ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_3,
					WL_TX_MODE_NONE, WL_TX_CHAINS_4,
					(const ppr_vht_mcs_rateset_t*)pwr_offsets);
				/* for vht_S4x4_SDM */
				ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_4,
					WL_TX_MODE_NONE, WL_TX_CHAINS_4,
					(const ppr_vht_mcs_rateset_t*)pwr_offsets);
#if defined(WL_BEAMFORMING) && !defined(WLTXBF_DISABLED)
				/* for vht_S1x4_TXBF */
				ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_1,
					WL_TX_MODE_TXBF, WL_TX_CHAINS_4,
					(const ppr_vht_mcs_rateset_t*)pwr_offsets);
				/* for vht_S2x4_TXBF */
				ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_2,
					WL_TX_MODE_TXBF, WL_TX_CHAINS_4,
					(const ppr_vht_mcs_rateset_t*)pwr_offsets);
				/* for vht_S3x4_TXBF */
				ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_3,
					WL_TX_MODE_TXBF, WL_TX_CHAINS_4,
					(const ppr_vht_mcs_rateset_t*)pwr_offsets);
				/* for vht_S4x4_TXBF */
				ppr_set_vht_mcs(tx_srom_max_pwr, bwtype, WL_TX_NSS_4,
					WL_TX_MODE_TXBF, WL_TX_CHAINS_4,
					(const ppr_vht_mcs_rateset_t*)pwr_offsets);
#endif /* WL_BEAMFORMING && !WLTXBF_DISABLED */
			}
		}
	}
	BCM_REFERENCE(tmp_mcs8);
	BCM_REFERENCE(tmp_mcs9);
}

static uint8
wlc_phy_make_byte(uint16 nibbleH, uint16 nibbleL)
{
	return (uint8) (((nibbleH & 0xf) << 4) | (nibbleL & 0xf));
}

static void
wlc_phy_txpwr_apply_srom11(phy_info_t *pi, uint8 band, chanspec_t chanspec,
                           uint8 tmp_max_pwr, ppr_t *tx_srom_max_pwr)
{
	uint8 nibbles;
	uint8 ppr_bit_ext, msb;
	const struct srom11_ppr *sr11 = &pi->ppr->u.sr11;
	ppr_bit_ext = BF3_PPR_BIT_EXT(pi->u.pi_acphy);

	if (CHSPEC_IS2G(chanspec))
	{
		ppr_ofdm_rateset_t	ofdm20_offset_2g;
		ppr_vht_mcs_rateset_t	mcs20_offset_2g;
		memset(&mcs20_offset_2g, WL_RATE_DISABLED, sizeof(mcs20_offset_2g));

		/* 2G - OFDM_20 */
		wlc_phy_txpwr_srom_convert_ofdm(sr11->ofdm_2g.bw20, tmp_max_pwr, &ofdm20_offset_2g);

		/* 2G - MCS_20 */
		nibbles = (sr11->offset_2g >> 8) & 0xf;   /* 2LSB is needed */
		wlc_phy_txpwr_srom11_convert_mcs_2g(sr11->mcs_2g.bw20, nibbles,
		        tmp_max_pwr, &mcs20_offset_2g);

		if (ppr_bit_ext) {
			/* msb: bit 1 for mcs9, bit 0 for mcs8
			 * sb40and80hr5glpo, nib3 is 2G
			 * bit13 and bit12 are 2g-20MHz: mcs9,mcs8
			 */
			msb = (sr11->offset_40in80_h[0] >> 12) & 0x3;
			wlc_phy_txpwr_ppr_bit_ext_mcs8and9(&mcs20_offset_2g, msb);
		}

		if (CHSPEC_BW_LE20(chanspec)) {
			ppr_dsss_rateset_t	cck20_offset;

			/* 2G - CCK */
			wlc_phy_txpwr_srom_convert_cck(sr11->cck.bw20, tmp_max_pwr, &cck20_offset);

			wlc_phy_ppr_set_dsss(tx_srom_max_pwr, WL_TX_BW_20, &cck20_offset, pi);
			wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20, &ofdm20_offset_2g, pi);
			wlc_phy_ppr_set_mcs(tx_srom_max_pwr,  WL_TX_BW_20, &mcs20_offset_2g, pi);
		} else if (CHSPEC_IS40(chanspec)) {
			ppr_dsss_rateset_t	cck20in40_offset = {{0, }};
			ppr_ofdm_rateset_t	ofdm20in40_offset_2g = {{0, }};
			ppr_ofdm_rateset_t	ofdmdup40_offset_2g = {{0, }};
			ppr_ofdm_rateset_t	ofdm40_offset_2g = {{0, }};
			ppr_vht_mcs_rateset_t	mcs40_offset_2g;
			ppr_vht_mcs_rateset_t	mcs20in40_offset_2g;
			memset(&mcs40_offset_2g, WL_RATE_DISABLED, sizeof(mcs40_offset_2g));
			memset(&mcs20in40_offset_2g, WL_RATE_DISABLED, sizeof(mcs20in40_offset_2g));

			/* 2G - CCK */
			wlc_phy_txpwr_srom_convert_cck(sr11->cck.bw20in40,
			        tmp_max_pwr, &cck20in40_offset);

			/* 2G - MCS_40 */
			nibbles = (sr11->offset_2g >> 12) & 0xf;   /* 3LSB is needed */
			wlc_phy_txpwr_srom11_convert_mcs_2g(sr11->mcs_2g.bw40, nibbles,
			        tmp_max_pwr, &mcs40_offset_2g);

			if (ppr_bit_ext) {
				/* msb: bit 1 for mcs9, bit 0 for mcs8
				 * sb40and80hr5glpo, nib3 is 2G
				 * bit15 and bit14 are 2g-40MHz: mcs9,mcs8
				 */
				msb = (sr11->offset_40in80_h[0] >> 14) & 0x3;
				wlc_phy_txpwr_ppr_bit_ext_mcs8and9(&mcs40_offset_2g, msb);
			}
			/* this is used for 2g_ofdm_dup40 mode,
			 * remapping mcs40_offset_2g to ofdm40_offset_2g as the basis for dup
			 */
			wlc_phy_txpwr_srom11_convert_ofdm_2g_dup40(sr11->mcs_2g.bw40,
			        nibbles, tmp_max_pwr, &ofdm40_offset_2g);

			/* 2G - OFDM_20IN40 */
			nibbles = wlc_phy_make_byte(sr11->offset_20in40_h, sr11->offset_20in40_l);
			wlc_phy_txpwr_srom11_convert_ofdm_offset(&ofdm20_offset_2g, nibbles,
				&ofdm20in40_offset_2g);

			/* 2G - MCS_20IN40 */
			wlc_phy_txpwr_srom11_convert_mcs_offset(&mcs20_offset_2g, nibbles,
			        &mcs20in40_offset_2g);

			/* 2G OFDM_DUP40 */
			nibbles = wlc_phy_make_byte(sr11->offset_dup_h, sr11->offset_dup_l);
			wlc_phy_txpwr_srom11_convert_ofdm_offset(&ofdm40_offset_2g, nibbles,
			        &ofdmdup40_offset_2g);

			wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_40, &ofdmdup40_offset_2g,
				pi);
			wlc_phy_ppr_set_mcs(tx_srom_max_pwr,  WL_TX_BW_40, &mcs40_offset_2g, pi);

			wlc_phy_ppr_set_dsss(tx_srom_max_pwr, WL_TX_BW_20IN40,
			                     &cck20in40_offset, pi);
			wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20IN40,
			                     &ofdm20in40_offset_2g, pi);
			wlc_phy_ppr_set_mcs(tx_srom_max_pwr,  WL_TX_BW_20IN40,
			                     &mcs20in40_offset_2g, pi);
		}
	}

#ifdef BAND5G
	else if (CHSPEC_IS5G(chanspec)) {
		uint8 band5g = band - 1;
		int bitN = (band == 1) ? 4 : ((band == 2) ? 8 : 12);
		ppr_ofdm_rateset_t	ofdm20_offset_5g;
		ppr_vht_mcs_rateset_t	mcs20_offset_5g;
		memset(&mcs20_offset_5g, WL_RATE_DISABLED, sizeof(mcs20_offset_5g));

		/* 5G 11agnac_20IN20 */
		nibbles = sr11->offset_5g[band5g] & 0xf;		/* 0LSB */
		wlc_phy_txpwr_srom11_convert_ofdm_5g(sr11->ofdm_5g.bw20[band5g],
		        nibbles, tmp_max_pwr, &ofdm20_offset_5g);
		wlc_phy_txpwr_srom11_convert_mcs_5g(sr11->ofdm_5g.bw20[band5g],
		        nibbles, tmp_max_pwr, &mcs20_offset_5g);
		if (ppr_bit_ext) {
			/* msb: bit 1 for mcs9, bit 0 for mcs8
			 * sb40and80hr5glpo, nib2 and nib1 is 5G-low
			 * sb40and80hr5gmpo, nib2 and nib1 is 5G-mid
			 * sb40and80hr5ghpo, nib2 and nib1 is 5G-high
			 * bit5 and bit4 are 5g-20MHz: mcs9,mcs8
			 */
			msb = (sr11->offset_40in80_h[band5g] >> 4) & 0x3;
			wlc_phy_txpwr_ppr_bit_ext_mcs8and9(&mcs20_offset_5g, msb);
		}

		if (CHSPEC_BW_LE20(chanspec)) {
			wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20, &ofdm20_offset_5g, pi);
			wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_20, &mcs20_offset_5g, pi);
		} else {
			ppr_ofdm_rateset_t	ofdm40_offset_5g = {{0, }};
			ppr_vht_mcs_rateset_t	mcs40_offset_5g = {{0, }};
			memset(&mcs40_offset_5g, WL_RATE_DISABLED, sizeof(mcs40_offset_5g));

			/* 5G 11nac 40IN40 */
			nibbles = (sr11->offset_5g[band5g] >> 4) & 0xf; /* 1LSB */
			wlc_phy_txpwr_srom11_convert_mcs_5g(sr11->mcs_5g.bw40[band5g],
			        nibbles, tmp_max_pwr, &mcs40_offset_5g);
			if (ppr_bit_ext) {
				/* msb: bit 1 for mcs9, bit 0 for mcs8
				 * sb40and80hr5glpo, nib2 and nib1 is 5G-low
				 * sb40and80hr5gmpo, nib2 and nib1 is 5G-mid
				 * sb40and80hr5ghpo, nib2 and nib1 is 5G-high
				 * bit7andbit6 are 5g-40MHz: mcs9, mcs8
				 */
				msb = (sr11->offset_40in80_h[band5g] >> 6) & 0x3;
				wlc_phy_txpwr_ppr_bit_ext_mcs8and9(&mcs40_offset_5g, msb);
			}

			/* same for ofdm 5g dup40 in 40MHz and dup80 in 80MHz */
			wlc_phy_txpwr_srom11_convert_ofdm_5g(sr11->mcs_5g.bw40[band5g],
			        nibbles, tmp_max_pwr, &ofdm40_offset_5g);

			if (CHSPEC_IS40(chanspec)) {
				ppr_ofdm_rateset_t	ofdm20in40_offset_5g;
				ppr_ofdm_rateset_t	ofdmdup40_offset_5g;
				ppr_vht_mcs_rateset_t	mcs20in40_offset_5g;
				memset(&mcs20in40_offset_5g, WL_RATE_DISABLED,
					sizeof(mcs20in40_offset_5g));

				/* 5G 11agnac_20IN40 */
				nibbles = wlc_phy_make_byte(sr11->offset_20in40_h >> bitN,
				                            sr11->offset_20in40_l >> bitN);
				wlc_phy_txpwr_srom11_convert_ofdm_offset(&ofdm20_offset_5g,
				        nibbles, &ofdm20in40_offset_5g);
				wlc_phy_txpwr_srom11_convert_mcs_offset(&mcs20_offset_5g,
				        nibbles, &mcs20in40_offset_5g);

				/* 5G ofdm_DUP40 */
				nibbles = wlc_phy_make_byte(sr11->offset_dup_h >> bitN,
				                            sr11->offset_dup_l >> bitN);
				wlc_phy_txpwr_srom11_convert_ofdm_offset((ppr_ofdm_rateset_t*)
				      &ofdm40_offset_5g, nibbles, &ofdmdup40_offset_5g);

				wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_40,
				                     &ofdmdup40_offset_5g, pi);
				wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_40,
				                     &mcs40_offset_5g, pi);

				wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20IN40,
				                     &ofdm20in40_offset_5g, pi);
				wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_20IN40,
				                     &mcs20in40_offset_5g, pi);

#ifdef WL11AC
			} else if (CHSPEC_IS80(chanspec)) {
				ppr_ofdm_rateset_t	ofdm20in80_offset_5g = {{0, }};
				ppr_ofdm_rateset_t	ofdm80_offset_5g = {{0, }};
				ppr_ofdm_rateset_t	ofdmdup80_offset_5g = {{0, }};
				ppr_ofdm_rateset_t	ofdmquad80_offset_5g = {{0, }};
				ppr_vht_mcs_rateset_t	mcs80_offset_5g;
				ppr_vht_mcs_rateset_t	mcs20in80_offset_5g;
				ppr_vht_mcs_rateset_t	mcs40in80_offset_5g;
				memset(&mcs80_offset_5g, WL_RATE_DISABLED, sizeof(mcs80_offset_5g));
				memset(&mcs20in80_offset_5g, WL_RATE_DISABLED,
					sizeof(mcs20in80_offset_5g));
				memset(&mcs40in80_offset_5g, WL_RATE_DISABLED,
					sizeof(mcs40in80_offset_5g));

				/* 5G 11nac 80IN80 */
				nibbles = (sr11->offset_5g[band5g] >> 8) & 0xf; /* 2LSB */
				wlc_phy_txpwr_srom11_convert_mcs_5g(sr11->mcs_5g.bw80[band5g],
				        nibbles, tmp_max_pwr, &mcs80_offset_5g);
				wlc_phy_txpwr_srom11_convert_ofdm_5g(sr11->mcs_5g.bw80[band5g],
				        nibbles, tmp_max_pwr, &ofdm80_offset_5g);

				if (ppr_bit_ext) {
					/* msb: bit 1 for mcs9, bit 0 for mcs8
					 * sb40and80hr5glpo, nib2 and nib1 is 5G-low
					 * sb40and80hr5gmpo, nib2 and nib1 is 5G-mid
					 * sb40and80hr5ghpo, nib2 and nib1 is 5G-high
					 * bit9andbit8 are 5g-80MHz: mcs9,mcs8
					 */
					msb = (sr11->offset_40in80_h[band5g] >> 8) & 0x3;
					wlc_phy_txpwr_ppr_bit_ext_mcs8and9(&mcs80_offset_5g, msb);
				}

				/* 5G ofdm_QUAD80, 80in80 */
				nibbles = wlc_phy_make_byte(sr11->offset_dup_h >> bitN,
				                            sr11->offset_dup_l >> bitN);
				wlc_phy_txpwr_srom11_convert_ofdm_offset((ppr_ofdm_rateset_t*)
				        &ofdm80_offset_5g, nibbles, &ofdmquad80_offset_5g);

				/* 5G ofdm_DUP40in80 */
				wlc_phy_txpwr_srom11_convert_ofdm_offset((ppr_ofdm_rateset_t*)
			            &ofdm40_offset_5g, nibbles, &ofdmdup80_offset_5g);

				/* 5G 11agnac_20Ul/20LU/20UU/20LL */
				/* 8 for 20LU/20UL subband  */
				nibbles = wlc_phy_make_byte(sr11->offset_20in80_h[band5g],
				                            sr11->offset_20in80_l[band5g]);
				wlc_phy_txpwr_srom11_convert_ofdm_offset(
				        &ofdm20_offset_5g, nibbles, &ofdm20in80_offset_5g);
				wlc_phy_txpwr_srom11_convert_mcs_offset(
				        &mcs20_offset_5g, nibbles, &mcs20in80_offset_5g);

				if ((CHSPEC_CTL_SB(chanspec) == WL_CHANSPEC_CTL_SB_UU) ||
					(CHSPEC_CTL_SB(chanspec) == WL_CHANSPEC_CTL_SB_LL)) {
					/* for 20UU/20LL subband = offset + 20UL/20LU */
					/* 8 for 20LL/20UU subband  */
					nibbles = wlc_phy_make_byte(
							sr11->offset_20in80_h[band5g] >> 2,
							sr11->offset_20in80_l[band5g] >> 2);
					wlc_phy_txpwr_srom11_convert_ofdm_offset(
					    &ofdm20in80_offset_5g, nibbles, &ofdm20in80_offset_5g);
					wlc_phy_txpwr_srom11_convert_mcs_offset(
					    &mcs20in80_offset_5g, nibbles, &mcs20in80_offset_5g);
				}

				/* 5G 11nac_40IN80 */
				nibbles = wlc_phy_make_byte(sr11->offset_40in80_h[band5g],
				                            sr11->offset_40in80_l[band5g]);
				wlc_phy_txpwr_srom11_convert_mcs_offset(&mcs40_offset_5g,
					nibbles, &mcs40in80_offset_5g);

				/* for 80IN80MHz OFDM or OFDMQUAD80 */
				wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_80,
				                     &ofdmquad80_offset_5g, pi);
				/* for 80IN80MHz HT */
				wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_80,
				                     &mcs80_offset_5g, pi);
				/* for ofdm_20IN80: S1x1, S1x2, S1x3 */
				wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20IN80,
				                     &ofdm20in80_offset_5g, pi);
				/* for 20IN80MHz HT */
				wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_20IN80,
				                     &mcs20in80_offset_5g, pi);
				/* for 40IN80MHz HT */
				wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_40IN80,
				                     &mcs40in80_offset_5g, pi);

				/* for ofdm_40IN80: S1x1, S1x2, S1x3 */
				wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_40IN80,
				                     &ofdmdup80_offset_5g, pi);
#endif /* WL11AC */

			}
		}
	}
#endif /* BAND5G */
}

#ifdef WL11AC
static void
BCMATTACHFN(wlc_phy_txpwr_srom13_read_ppr)(phy_info_t *pi)
{
	if (!(SROMREV(pi->sh->sromrev) < 13)) {
		/* Read and interpret the power-offset parameters from the SROM for each
		 *  band/subband
		 */
		ASSERT(pi->sh->sromrev >= 13);

		PHY_INFORM(("Get SROM 13 Power Offset per rate\n"));
		/* --------------2G------------------- */
		/* 2G CCK */
		pi->ppr->u.sr13.cck.bw20 = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_cckbw202gpo);
		pi->ppr->u.sr13.cck.bw20in40 = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_cckbw20ul2gpo);

		pi->ppr->u.sr13.offset_2g = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_ofdmlrbw202gpo);
		/* 2G OFDM_20 */
		pi->ppr->u.sr13.ofdm_2g.bw20 =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_dot11agofdmhrbw202gpo);
		/* 2G MCS_20 */
		pi->ppr->u.sr13.mcs_2g.bw20 = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw202gpo);
		/* 2G MCS_40 */
		pi->ppr->u.sr13.mcs_2g.bw40 = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw402gpo);

		pi->ppr->u.sr13.offset_20in40_l =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in40lrpo);
		pi->ppr->u.sr13.offset_20in40_h =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in40hrpo);

		pi->ppr->u.sr13.offset_dup_h =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_dot11agduphrpo);
		pi->ppr->u.sr13.offset_dup_l =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_dot11agduplrpo);

		pi->ppr->u.sr13.pp1024qam2g = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_mcs1024qam2gpo);

#ifdef BAND5G
		/* ---------------5G--------------- */
		/* 5G 11agnac_20IN20 */
		pi->ppr->u.sr13.ofdm_5g.bw20[0] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw205glpo);
		pi->ppr->u.sr13.ofdm_5g.bw20[1] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw205gmpo);
		pi->ppr->u.sr13.ofdm_5g.bw20[2] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw205ghpo);
		pi->ppr->u.sr13.ofdm_5g.bw20[3] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw205gx1po);
		pi->ppr->u.sr13.ofdm_5g.bw20[4] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw205gx2po);

		pi->ppr->u.sr13.offset_5g[0]	= (uint16)PHY_GETINTVAR_SLICE(pi, rstr_mcslr5glpo);
		pi->ppr->u.sr13.offset_5g[1] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_mcslr5gmpo);
		pi->ppr->u.sr13.offset_5g[2] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_mcslr5ghpo);
		pi->ppr->u.sr13.offset_5g[3] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_mcslr5gx1po);
		pi->ppr->u.sr13.offset_5g[4] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_mcslr5gx2po);

		/* 5G 11nac 40IN40 */
		pi->ppr->u.sr13.mcs_5g.bw40[0] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw405glpo);
		pi->ppr->u.sr13.mcs_5g.bw40[1] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw405gmpo);
		pi->ppr->u.sr13.mcs_5g.bw40[2] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw405ghpo);
		pi->ppr->u.sr13.mcs_5g.bw40[3] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw405gx1po);
		pi->ppr->u.sr13.mcs_5g.bw40[4] =
				(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw405gx2po);

		/* 5G 11nac 80IN80 */
		pi->ppr->u.sr13.mcs_5g.bw80[0] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw805glpo);
		pi->ppr->u.sr13.mcs_5g.bw80[1] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw805gmpo);
		pi->ppr->u.sr13.mcs_5g.bw80[2] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw805ghpo);
		pi->ppr->u.sr13.mcs_5g.bw80[3] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw805gx1po);
		pi->ppr->u.sr13.mcs_5g.bw80[4] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw805gx2po);

		pi->ppr->u.sr13.offset_20in80_l[0] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160lr5glpo);
		pi->ppr->u.sr13.offset_20in80_h[0] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160hr5glpo);
		pi->ppr->u.sr13.offset_20in80_l[1] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160lr5gmpo);
		pi->ppr->u.sr13.offset_20in80_h[1] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160hr5gmpo);
		pi->ppr->u.sr13.offset_20in80_l[2] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160lr5ghpo);
		pi->ppr->u.sr13.offset_20in80_h[2] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160hr5ghpo);
		pi->ppr->u.sr13.offset_20in80_l[3] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160lr5gx1po);
		pi->ppr->u.sr13.offset_20in80_h[3] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160hr5gx1po);
		pi->ppr->u.sr13.offset_20in80_l[4] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160lr5gx2po);
		pi->ppr->u.sr13.offset_20in80_h[4] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb20in80and160hr5gx2po);

		pi->ppr->u.sr13.offset_40in80_l[0] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80lr5glpo);
		pi->ppr->u.sr13.offset_40in80_h[0] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80hr5glpo);
		pi->ppr->u.sr13.offset_40in80_l[1] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80lr5gmpo);
		pi->ppr->u.sr13.offset_40in80_h[1] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80hr5gmpo);
		pi->ppr->u.sr13.offset_40in80_l[2] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80lr5ghpo);
		pi->ppr->u.sr13.offset_40in80_h[2] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80hr5ghpo);
		pi->ppr->u.sr13.offset_40in80_l[3] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80lr5gx1po);
		pi->ppr->u.sr13.offset_40in80_h[3] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80hr5gx1po);
		pi->ppr->u.sr13.offset_40in80_l[4] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80lr5gx2po);
		pi->ppr->u.sr13.offset_40in80_h[4] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_sb40and80hr5gx2po);

		/* 5G 11nac 160IN160 */
		pi->ppr->u.sr13.mcs_5g.bw160[0] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw1605glpo);
		pi->ppr->u.sr13.mcs_5g.bw160[1] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw1605gmpo);
		pi->ppr->u.sr13.mcs_5g.bw160[2] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw1605ghpo);
		pi->ppr->u.sr13.mcs_5g.bw160[3] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw1605gx1po);
		pi->ppr->u.sr13.mcs_5g.bw160[4] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcsbw1605gx2po);

		/* extension fields in SROM 13 */
		pi->ppr->u.sr13.pp1024qam5g[0] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcs1024qam5glpo);
		pi->ppr->u.sr13.pp1024qam5g[1] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcs1024qam5gmpo);
		pi->ppr->u.sr13.pp1024qam5g[2] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcs1024qam5ghpo);
		pi->ppr->u.sr13.pp1024qam5g[3] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcs1024qam5gx1po);
		pi->ppr->u.sr13.pp1024qam5g[4] =
			(uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcs1024qam5gx2po);

		pi->ppr->u.sr13.ppmcsexp[0] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcs8poexp);
		pi->ppr->u.sr13.ppmcsexp[1] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcs9poexp);
		pi->ppr->u.sr13.ppmcsexp[2] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcs10poexp);
		pi->ppr->u.sr13.ppmcsexp[3] = (uint32)PHY_GETINTVAR_SLICE(pi, rstr_mcs11poexp);

#endif /* BAND5G */
	}
}

void
wlc_phy_txpwr_apply_srom13_2g_bw2040(phy_info_t *pi, chanspec_t chanspec,
                           uint8 tmp_max_pwr, ppr_t *tx_srom_max_pwr)
{
	uint8 nibbles;
	uint32 ofdm_po;
	const struct srom13_ppr *sr13 = &pi->ppr->u.sr13;

	if (CHSPEC_IS2G(chanspec)) {
		ppr_ofdm_rateset_t	ofdm20_offset_2g;
		ppr_vht_mcs_rateset_t	mcs20_offset_2g;

		/* 2G - OFDM_20 */
		ofdm_po = (uint32)(sr13->ofdm_2g.bw20) << 16;
		ofdm_po |= (uint32)(sr13->offset_2g);
		wlc_phy_txpwr_srom_convert_ofdm(ofdm_po, tmp_max_pwr, &ofdm20_offset_2g);

		/* 2G - MCS_20 */
		nibbles = (sr13->offset_2g >> 8) & 0xf;   /* 2LSB is needed */
		wlc_phy_txpwr_srom11_convert_mcs_2g(sr13->mcs_2g.bw20, nibbles,
		        tmp_max_pwr, &mcs20_offset_2g);

		wlc_phy_txpwr_srom13_ext_1024qam_convert_mcs_2g(sr13->pp1024qam2g,
		        chanspec, tmp_max_pwr, &mcs20_offset_2g);
		/* No shift needed for 2g bw20 ppr mcsexp */
		wlc_phy_txpwr_ppr_bit_ext_srom13_mcs8to11(pi, &mcs20_offset_2g, 0);

#ifdef WL11ULB
		if (CHSPEC_IS20(chanspec) || CHSPEC_ISLE20(chanspec)) {
#else
		if (CHSPEC_IS20(chanspec)) {
#endif
			ppr_dsss_rateset_t	cck20_offset;
			/* 2G - CCK */
			wlc_phy_txpwr_srom_convert_cck(sr13->cck.bw20, tmp_max_pwr, &cck20_offset);
			wlc_phy_ppr_set_dsss(tx_srom_max_pwr, WL_TX_BW_20, &cck20_offset, pi);
			wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20, &ofdm20_offset_2g, pi);
			wlc_phy_ppr_set_mcs(tx_srom_max_pwr,  WL_TX_BW_20, &mcs20_offset_2g, pi);
		} else if (CHSPEC_IS40(chanspec)) {
			ppr_dsss_rateset_t	cck20in40_offset;
			ppr_ofdm_rateset_t	ofdm20in40_offset_2g;
			ppr_ofdm_rateset_t	ofdmdup40_offset_2g;
			ppr_ofdm_rateset_t	ofdm40_offset_2g;
			ppr_vht_mcs_rateset_t	mcs40_offset_2g;
			ppr_vht_mcs_rateset_t	mcs20in40_offset_2g;

			/* 2G - CCK */
			wlc_phy_txpwr_srom_convert_cck(sr13->cck.bw20in40, tmp_max_pwr,
			        &cck20in40_offset);

			/* 2G - MCS_40 */
			nibbles = (sr13->offset_2g >> 12) & 0xf;   /* 3LSB is needed */
			wlc_phy_txpwr_srom11_convert_mcs_2g(sr13->mcs_2g.bw40, nibbles,
			        tmp_max_pwr, &mcs40_offset_2g);

			wlc_phy_txpwr_srom13_ext_1024qam_convert_mcs_2g(sr13->pp1024qam2g,
				chanspec, tmp_max_pwr, &mcs40_offset_2g);
			/* shift 8 bits for 2g bw40 ppr mcsexp */
			wlc_phy_txpwr_ppr_bit_ext_srom13_mcs8to11(pi, &mcs40_offset_2g, 8);

			/* this is used for 2g_ofdm_dup40 mode,
			 * remapping mcs40_offset_2g to ofdm40_offset_2g as the basis for dup
			 */
			wlc_phy_txpwr_srom11_convert_ofdm_2g_dup40(sr13->mcs_2g.bw40,
			        nibbles, tmp_max_pwr, &ofdm40_offset_2g);
			/* 2G - OFDM_20IN40 */
			nibbles = wlc_phy_make_byte(sr13->offset_20in40_h, sr13->offset_20in40_l);
			wlc_phy_txpwr_srom11_convert_ofdm_offset(&ofdm20_offset_2g, nibbles,
				&ofdm20in40_offset_2g);
			/* 2G - MCS_20IN40 */
			wlc_phy_txpwr_srom11_convert_mcs_offset(&mcs20_offset_2g, nibbles,
			        &mcs20in40_offset_2g);
			/* 2G OFDM_DUP40 */
			nibbles = wlc_phy_make_byte(sr13->offset_dup_h, sr13->offset_dup_l);
			wlc_phy_txpwr_srom11_convert_ofdm_offset(&ofdm40_offset_2g, nibbles,
			        &ofdmdup40_offset_2g);
			wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_40, &ofdmdup40_offset_2g,
				pi);
			wlc_phy_ppr_set_mcs(tx_srom_max_pwr,  WL_TX_BW_40, &mcs40_offset_2g, pi);
			wlc_phy_ppr_set_dsss(tx_srom_max_pwr, WL_TX_BW_20IN40,
			                     &cck20in40_offset, pi);
			wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20IN40,
			                     &ofdm20in40_offset_2g, pi);
			wlc_phy_ppr_set_mcs(tx_srom_max_pwr,  WL_TX_BW_20IN40,
			                     &mcs20in40_offset_2g, pi);
		}
	}
}

#ifdef BAND5G
void
wlc_phy_txpwr_apply_srom13_5g_bw40(phy_info_t *pi, uint8 band, ppr_t *tx_srom_max_pwr,
		sr13_ppr_5g_rateset_t *rate5g)
{
	uint8 nibbles;
	int bitN = (band == 1) ? 4 : ((band == 2) ? 8 : 12);
	ppr_ofdm_rateset_t      ofdm20in40_offset_5g;
	ppr_ofdm_rateset_t      ofdmdup40_offset_5g;
	ppr_vht_mcs_rateset_t   mcs20in40_offset_5g;

	const struct srom13_ppr *sr13 = &pi->ppr->u.sr13;

	/* 5G 11agnac_20IN40 */
	nibbles = wlc_phy_make_byte(sr13->offset_20in40_h >> bitN, sr13->offset_20in40_l >> bitN);
	wlc_phy_txpwr_srom11_convert_ofdm_offset(&rate5g->ofdm20_offset_5g, nibbles,
		&ofdm20in40_offset_5g);
	wlc_phy_txpwr_srom11_convert_mcs_offset(&rate5g->mcs20_offset_5g, nibbles,
		&mcs20in40_offset_5g);

	/* 5G ofdm_DUP40 */
	nibbles = wlc_phy_make_byte(sr13->offset_dup_h >> bitN, sr13->offset_dup_l >> bitN);
	wlc_phy_txpwr_srom11_convert_ofdm_offset((ppr_ofdm_rateset_t*)&rate5g->ofdm40_offset_5g,
	      nibbles, &ofdmdup40_offset_5g);

	wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_40, &ofdmdup40_offset_5g, pi);
	wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_40, &rate5g->mcs40_offset_5g, pi);

	wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20IN40, &ofdm20in40_offset_5g, pi);
	wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_20IN40, &mcs20in40_offset_5g, pi);
}

void
wlc_phy_txpwr_apply_srom13_5g_bw80(phy_info_t *pi, uint8 band, chanspec_t chanspec,
		ppr_t *tx_srom_max_pwr, sr13_ppr_5g_rateset_t *rate5g)
{
	uint8 nibbles;
	uint8 band5g = band - 1;
	int bitN = (band == 1) ? 4 : ((band == 2) ? 8 : 12);
	ppr_ofdm_rateset_t      ofdm20in80_offset_5g;
	ppr_ofdm_rateset_t      ofdmdup80_offset_5g;
	ppr_ofdm_rateset_t      ofdmquad80_offset_5g;
	ppr_vht_mcs_rateset_t   mcs20in80_offset_5g;
	ppr_vht_mcs_rateset_t   mcs40in80_offset_5g;

	const struct srom13_ppr *sr13 = &pi->ppr->u.sr13;

	/* 5G ofdm_QUAD80, 80in80 */
	nibbles = wlc_phy_make_byte(sr13->offset_dup_h >> bitN, sr13->offset_dup_l >> bitN);
	wlc_phy_txpwr_srom11_convert_ofdm_offset((ppr_ofdm_rateset_t*)&rate5g->ofdm80_offset_5g,
		nibbles, &ofdmquad80_offset_5g);

	/* 5G ofdm_DUP40in80 */
	wlc_phy_txpwr_srom11_convert_ofdm_offset((ppr_ofdm_rateset_t*)&rate5g->ofdm40_offset_5g,
		nibbles, &ofdmdup80_offset_5g);

	/* 5G 11agnac_20Ul/20LU/20UU/20LL, 8 for 20LU/20UL subband */
	nibbles = wlc_phy_make_byte(sr13->offset_20in80_h[band5g], sr13->offset_20in80_l[band5g]);
	wlc_phy_txpwr_srom11_convert_ofdm_offset(&rate5g->ofdm20_offset_5g, nibbles,
		&ofdm20in80_offset_5g);
	wlc_phy_txpwr_srom11_convert_mcs_offset(&rate5g->mcs20_offset_5g, nibbles,
		&mcs20in80_offset_5g);

	if ((CHSPEC_CTL_SB(chanspec) == WL_CHANSPEC_CTL_SB_UU) ||
		(CHSPEC_CTL_SB(chanspec) == WL_CHANSPEC_CTL_SB_LL))
	{
		/* for 20UU/20LL subband = offset + 20UL/20LU, 8 for 20LL/20UU subband */
		nibbles = wlc_phy_make_byte(sr13->offset_20in80_h[band5g] >> 2,
			sr13->offset_20in80_l[band5g] >> 2);
		wlc_phy_txpwr_srom11_convert_ofdm_offset(&ofdm20in80_offset_5g, nibbles,
			&ofdm20in80_offset_5g);
		wlc_phy_txpwr_srom11_convert_mcs_offset(&mcs20in80_offset_5g, nibbles,
			&mcs20in80_offset_5g);
	}

	/* 5G 11nac_40IN80 */
	nibbles = wlc_phy_make_byte(sr13->offset_40in80_h[band5g], sr13->offset_40in80_l[band5g]);
	wlc_phy_txpwr_srom11_convert_mcs_offset(&rate5g->mcs40_offset_5g, nibbles,
		&mcs40in80_offset_5g);

	/* for 80IN80MHz OFDM or OFDMQUAD80 */
	wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_80, &ofdmquad80_offset_5g, pi);
	/* for 80IN80MHz HT */
	wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_80,  &rate5g->mcs80_offset_5g, pi);
	/* for ofdm_20IN80: S1x1, S1x2, S1x3 */
	wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20IN80, &ofdm20in80_offset_5g, pi);
	/* for 20IN80MHz HT */
	wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_20IN80, &mcs20in80_offset_5g, pi);
	/* for 40IN80MHz HT */
	wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_40IN80, &mcs40in80_offset_5g, pi);
	/* for ofdm_40IN80: S1x1, S1x2, S1x3 */
	wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_40IN80, &ofdmdup80_offset_5g, pi);
}

void
wlc_phy_txpwr_apply_srom13_5g_bw160(phy_info_t *pi, uint8 band, chanspec_t chanspec,
		uint8 tmp_max_pwr, ppr_t *tx_srom_max_pwr, sr13_ppr_5g_rateset_t *rate5g)
{
	uint8 nibbles;
	uint8 band5g = band - 1;
	int bitN = (band == 1) ? 4 : ((band == 2) ? 8 : 12);
	ppr_ofdm_rateset_t      ofdm160_offset_5g;
	ppr_ofdm_rateset_t      ofdm20in160_offset_5g;
	ppr_ofdm_rateset_t      ofdmdup160_offset_5g;
	ppr_ofdm_rateset_t      ofdmquad160_offset_5g;
	ppr_ofdm_rateset_t      ofdmoct160_offset_5g;
	ppr_vht_mcs_rateset_t   mcs160_offset_5g;
	ppr_vht_mcs_rateset_t   mcs20in160_offset_5g;
	ppr_vht_mcs_rateset_t   mcs40in160_offset_5g;
	ppr_vht_mcs_rateset_t   mcs80in160_offset_5g;

	const struct srom13_ppr *sr13 = &pi->ppr->u.sr13;

	/* 5G 11nac 160IN160, shift 12 for  bandwidth 160 */
	nibbles = (sr13->offset_5g[band5g] >> 12) & 0xf;
	wlc_phy_txpwr_srom11_convert_ofdm_5g(sr13->mcs_5g.bw160[band5g], nibbles,
		tmp_max_pwr, &ofdm160_offset_5g);
	wlc_phy_txpwr_srom11_convert_mcs_5g(sr13->mcs_5g.bw160[band5g], nibbles,
		tmp_max_pwr, &mcs160_offset_5g);

	wlc_phy_txpwr_srom13_ext_1024qam_convert_mcs_5g(sr13->pp1024qam5g[band5g], chanspec,
		tmp_max_pwr, &mcs160_offset_5g);
	/* shift band5g +  25 bits for 5g bw160 ppr mcsexp */
	wlc_phy_txpwr_ppr_bit_ext_srom13_mcs8to11(pi, &mcs160_offset_5g, band5g + 25);

	/* 5G ofdm_OCT160, 160in160 */
	nibbles = wlc_phy_make_byte(sr13->offset_dup_h >> bitN, sr13->offset_dup_l >> bitN);
	wlc_phy_txpwr_srom11_convert_ofdm_offset((ppr_ofdm_rateset_t*)&ofdm160_offset_5g,
		nibbles, &ofdmoct160_offset_5g);

	/* 5G ofdm_QUAD80in160 */
	wlc_phy_txpwr_srom11_convert_ofdm_offset((ppr_ofdm_rateset_t*)&rate5g->ofdm80_offset_5g,
		nibbles, &ofdmquad160_offset_5g);

	/* 5G ofdm_DUP40in160 */
	wlc_phy_txpwr_srom11_convert_ofdm_offset((ppr_ofdm_rateset_t*)&rate5g->ofdm40_offset_5g,
		nibbles, &ofdmdup160_offset_5g);

	/* 5G 11agnac_20IN160 */
	nibbles = wlc_phy_make_byte(sr13->offset_20in80_h[band5g] >> 4,
		sr13->offset_20in80_l[band5g] >> 4);
	wlc_phy_txpwr_srom11_convert_ofdm_offset(&rate5g->ofdm20_offset_5g, nibbles,
		&ofdm20in160_offset_5g);
	wlc_phy_txpwr_srom11_convert_mcs_offset(&rate5g->mcs20_offset_5g, nibbles,
		&mcs20in160_offset_5g);
	if ((CHSPEC_CTL_SB(chanspec) == WL_CHANSPEC_CTL_SB_UUU) ||
		(CHSPEC_CTL_SB(chanspec) == WL_CHANSPEC_CTL_SB_LLL)) {
		/* 12 for 20LLL/20UUU subband w.r.t 20in160 sb */
		nibbles = wlc_phy_make_byte(sr13->offset_20in80_h[band5g] >> 12,
			sr13->offset_20in80_l[band5g] >> 12);
		wlc_phy_txpwr_srom11_convert_ofdm_offset(&ofdm20in160_offset_5g, nibbles,
			&ofdm20in160_offset_5g);
		wlc_phy_txpwr_srom11_convert_mcs_offset(&mcs20in160_offset_5g, nibbles,
			&mcs20in160_offset_5g);
	}
	/* 5G 11nac_40IN160 */
	nibbles = wlc_phy_make_byte(sr13->offset_40in80_h[band5g] >> 4,
			sr13->offset_40in80_l[band5g] >> 4);
	wlc_phy_txpwr_srom11_convert_mcs_offset(&rate5g->mcs40_offset_5g, nibbles,
		&mcs40in160_offset_5g);
	if ((CHSPEC_CTL_SB(chanspec) == WL_CHANSPEC_CTL_SB_UU) ||
		(CHSPEC_CTL_SB(chanspec) == WL_CHANSPEC_CTL_SB_LL)) {
		/* 12 for 40LL/40UU subband w.r.t to 40LU/UL */
		nibbles = wlc_phy_make_byte(sr13->offset_40in80_h[band5g] >> 12,
			sr13->offset_40in80_l[band5g] >> 12);
		wlc_phy_txpwr_srom11_convert_mcs_offset(&mcs40in160_offset_5g, nibbles,
			&mcs40in160_offset_5g);
	}

	/* 5G 11nac_80IN160 */
	nibbles = wlc_phy_make_byte(sr13->offset_40in80_h[band5g] >> 8,
		sr13->offset_40in80_l[band5g] >> 8);
	wlc_phy_txpwr_srom11_convert_mcs_offset(&rate5g->mcs80_offset_5g, nibbles,
		&mcs80in160_offset_5g);

	/* for 160IN160MHz OFDM or OFDMQUAD160 */
	wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_160, &ofdmoct160_offset_5g, pi);
	/* for ofdm_20IN160: S1x1, S1x2, S1x3, S1x4 */
	wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20IN160, &ofdm20in160_offset_5g, pi);
	/* for ofdm_40IN160: S1x1, S1x2, S1x3, S1x4 */
	wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_40IN160, &ofdmdup160_offset_5g, pi);
	/* for ofdm_80IN160: S1x1, S1x2, S1x3, S1x4 */
	wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_80IN160, &ofdmquad160_offset_5g, pi);

	/* for 160IN160MHz HT */
	wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_160, &mcs160_offset_5g, pi);
	/* for 20IN160MHz HT */
	wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_20IN160, &mcs20in160_offset_5g, pi);
	/* for 40IN160MHz HT */
	wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_40IN160, &mcs40in160_offset_5g, pi);
	/* for 80IN160MHz HT */
	wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_80IN160, &mcs80in160_offset_5g, pi);
}
#endif /* BAND5G */

void
wlc_phy_txpwr_apply_srom13(phy_info_t *pi, uint8 band, chanspec_t chanspec,
                           uint8 tmp_max_pwr, ppr_t *tx_srom_max_pwr)
{
	if (!ISACPHY(pi)) {
		ASSERT(0);
		return;
	}

	if (CHSPEC_IS2G(chanspec)) {
		wlc_phy_txpwr_apply_srom13_2g_bw2040(pi, chanspec, tmp_max_pwr, tx_srom_max_pwr);
	}
#ifdef BAND5G
	else if (CHSPEC_IS5G(chanspec)) {
		uint8 nibbles;
		uint8 band5g = band - 1;
		sr13_ppr_5g_rateset_t rate5g;
		const struct srom13_ppr *sr13 = &pi->ppr->u.sr13;

		/* 5G 11agnac_20IN20 */
		nibbles = sr13->offset_5g[band5g] & 0xf;                /* 0LSB */
		wlc_phy_txpwr_srom11_convert_ofdm_5g(sr13->ofdm_5g.bw20[band5g],
			nibbles, tmp_max_pwr, &rate5g.ofdm20_offset_5g);
		wlc_phy_txpwr_srom11_convert_mcs_5g(sr13->ofdm_5g.bw20[band5g],
			nibbles, tmp_max_pwr, &rate5g.mcs20_offset_5g);

		wlc_phy_txpwr_srom13_ext_1024qam_convert_mcs_5g(sr13->pp1024qam5g[band5g],
			chanspec, tmp_max_pwr, &rate5g.mcs20_offset_5g);
		/* shift band5g + 1 for 5g bw20 ppr mcsexp */
		wlc_phy_txpwr_ppr_bit_ext_srom13_mcs8to11(pi, &rate5g.mcs20_offset_5g, band5g + 1);

#ifdef WL11ULB
		if (CHSPEC_IS20(chanspec) || CHSPEC_ISLE20(chanspec)) {
#else
		if (CHSPEC_IS20(chanspec)) {
#endif
			wlc_phy_ppr_set_ofdm(tx_srom_max_pwr, WL_TX_BW_20,
				&rate5g.ofdm20_offset_5g, pi);
			wlc_phy_ppr_set_mcs(tx_srom_max_pwr, WL_TX_BW_20,
				&rate5g.mcs20_offset_5g, pi);
		} else {
			/* 5G 11nac 40IN40 */
			nibbles = (sr13->offset_5g[band5g] >> 4) & 0xf; /* 1LSB */
			wlc_phy_txpwr_srom11_convert_mcs_5g(sr13->mcs_5g.bw40[band5g],
				nibbles, tmp_max_pwr, &rate5g.mcs40_offset_5g);

			wlc_phy_txpwr_srom13_ext_1024qam_convert_mcs_5g(sr13->pp1024qam5g[band5g],
				chanspec, tmp_max_pwr, &rate5g.mcs40_offset_5g);
			/* shift band5g + 9 for 5g bw40 ppr mcsexp */
			wlc_phy_txpwr_ppr_bit_ext_srom13_mcs8to11(pi, &rate5g.mcs40_offset_5g,
				band5g + 9);

			/* same for ofdm 5g dup40 in 40MHz and dup80 in 80MHz */
			wlc_phy_txpwr_srom11_convert_ofdm_5g(sr13->mcs_5g.bw40[band5g], nibbles,
				tmp_max_pwr, &rate5g.ofdm40_offset_5g);

			if (CHSPEC_IS40(chanspec)) {
				wlc_phy_txpwr_apply_srom13_5g_bw40(pi, band, tx_srom_max_pwr,
					&rate5g);
			} else {
				/* 5G 11nac 80IN80 */
				nibbles = (sr13->offset_5g[band5g] >> 8) & 0xf; /* 2LSB */
				wlc_phy_txpwr_srom11_convert_ofdm_5g(sr13->mcs_5g.bw80[band5g],
					nibbles, tmp_max_pwr, &rate5g.ofdm80_offset_5g);
				wlc_phy_txpwr_srom11_convert_mcs_5g(sr13->mcs_5g.bw80[band5g],
					nibbles, tmp_max_pwr, &rate5g.mcs80_offset_5g);
				wlc_phy_txpwr_srom13_ext_1024qam_convert_mcs_5g(
					sr13->pp1024qam5g[band5g], chanspec, tmp_max_pwr,
					&rate5g.mcs80_offset_5g);
				/* shift band5g + 17 for 5g bw80 ppr mcsexp */
				wlc_phy_txpwr_ppr_bit_ext_srom13_mcs8to11(pi,
					&rate5g.mcs80_offset_5g, band5g + 17);

				if (CHSPEC_IS80(chanspec)) {
					wlc_phy_txpwr_apply_srom13_5g_bw80(pi, band, chanspec,
						tx_srom_max_pwr, &rate5g);
				} else if (CHSPEC_IS160(chanspec)) {
					wlc_phy_txpwr_apply_srom13_5g_bw160(pi, band, chanspec,
						tmp_max_pwr, tx_srom_max_pwr, &rate5g);
				}
			}
		}
	}
#endif /* BAND5G */
}

#endif /* WL11AC */
#endif /* PPR_API */

static void
wlc_phy_txpower_sromlimit_get_acphy(phy_info_t *pi, chanspec_t chanspec,
                                        ppr_t *max_pwr, uint8 core)
{
	uint8 band = 0, band_srom = 0;
	uint8 tmp_max_pwr = 0, core_freq_segment_map;
	int8 deltaPwr = 0;
	srom11_pwrdet_t *pwrdet11 = pi->pwrdet_ac;

	ASSERT(core < PHY_CORE_MAX);
	ASSERT((pi->sromi->subband5Gver == PHY_SUBBAND_4BAND) ||
	       (pi->sromi->subband5Gver == PHY_MAXNUM_5GSUBBANDS));

	/* core_freq_segment_map is only required for 80P80 mode.
	For other modes, it is ignored
	*/
	core_freq_segment_map = pi->u.pi_acphy->core_freq_mapping[core];

	/* to figure out which subband is in 5G */
	/* in the range of 0, 1, 2, 3 */
	band = phy_ac_chanmgr_get_chan_freq_range(pi, chanspec, core_freq_segment_map);

	tmp_max_pwr = pwrdet11->max_pwr[0][band];
	if (PHYCORENUM(pi->pubpi->phy_corenum) > 1)
	    tmp_max_pwr = MIN(tmp_max_pwr, pwrdet11->max_pwr[1][band]);
	if (PHYCORENUM(pi->pubpi->phy_corenum) > 2)
	    tmp_max_pwr = MIN(tmp_max_pwr, pwrdet11->max_pwr[2][band]);

	/*	--------  in 5g_ext case  -----------
	 *	if 5170 <= freq < 5250, then band = 1;
	 *	if 5250 <= freq < 5500, then band = 2;
	 *	if 5500 <= freq < 5745, then band = 3;
	 *	if 5745 <= freq,		then band = 4;

	 *	--------  in 5g case  ---------------
	 *	if 5170 <= freq < 5500, then band = 1;
	 *	if 5500 <= freq < 5745, then band = 2;
	 *	if 5745 <= freq,		then band = 3;
	 */
	/*  -------- 4 subband to 3 subband mapping --------
	 *	subband#0 -> low
	 *	subband#1 -> mid
	 *	subband#2 -> high
	 *	subband#3 -> high
	 */

	if (band <= WL_CHAN_FREQ_RANGE_5G_BAND2)
	    band_srom = band;
	else
	    band_srom = band - 1;
	wlc_phy_txpwr_apply_srom11(pi, band_srom, chanspec, tmp_max_pwr, max_pwr);
	deltaPwr = pwrdet11->max_pwr[core][band] - tmp_max_pwr;

	if (deltaPwr > 0)
	    ppr_plus_cmn_val(max_pwr, deltaPwr);

	ppr_apply_max(max_pwr, pwrdet11->max_pwr[core][band]);
}

static void
wlc_phy_txpower_sromlimit_get_srom12_acphy(phy_info_t *pi, chanspec_t chanspec,
                                        ppr_t *max_pwr, uint8 core)
{
	if (!(SROMREV(pi->sh->sromrev) < 12)) {
	  uint8 band = 0;
	  uint8 tmp_max_pwr = 0;
	  uint8 chans[NUM_CHANS_IN_CHAN_BONDING];
	  int8 deltaPwr = 0;
	  srom12_pwrdet_t *pwrdet = pi->pwrdet_ac;

	  ASSERT(core < PHY_CORE_MAX);
	  if (ACMAJORREV_32(pi->pubpi->phy_rev) ||
	      ACMAJORREV_33(pi->pubpi->phy_rev) ||
	      ACMAJORREV_37(pi->pubpi->phy_rev)) {
	    if (CHSPEC_IS5G(pi->radio_chanspec))
	      ASSERT(pi->sromi->subband5Gver == PHY_MAXNUM_5GSUBBANDS);
	  } else {
	    ASSERT(pi->sromi->subband5Gver == PHY_MAXNUM_5GSUBBANDS);
	  }

	  /* to figure out which subband is in 5G */
	  /* in the range of 0, 1, 2, 3, 4, 5 */
	  if (ACMAJORREV_33(pi->pubpi->phy_rev) && PHY_AS_80P80(pi, chanspec)) {
		  phy_ac_chanmgr_get_chan_freq_range_80p80_srom12(pi, chanspec, chans);
		  band = (core <= 1) ? chans[0] : chans[1];
	  } else {
		  band = phy_ac_chanmgr_get_chan_freq_range_srom12(pi, chanspec);
	  }

	  if (band >= WL_CHAN_FREQ_RANGE_2G_40) {
	    if (CHSPEC_IS80(chanspec) || CHSPEC_IS8080(chanspec) ||
	      CHSPEC_IS160(chanspec)) {
	      band = band - WL_CHAN_FREQ_RANGE_5G_BAND0_80 + 1;
	    } else {
	      ASSERT(CHSPEC_IS40(chanspec));
	      band = band - WL_CHAN_FREQ_RANGE_2G_40;
	    }
	  }

	  tmp_max_pwr = pwrdet->max_pwr[0][band];
	  if (PHYCORENUM(pi->pubpi->phy_corenum) > 1)
	    tmp_max_pwr = MIN(tmp_max_pwr, pwrdet->max_pwr[1][band]);
	  if (PHYCORENUM(pi->pubpi->phy_corenum) > 2)
	    tmp_max_pwr = MIN(tmp_max_pwr, pwrdet->max_pwr[2][band]);
		if (PHYCORENUM(pi->pubpi->phy_corenum) > 3)
			tmp_max_pwr = MIN(tmp_max_pwr, pwrdet->max_pwr[3][band]);

		if (SROMREV(pi->sh->sromrev) < 13) {
			wlc_phy_txpwr_apply_srom11(pi, band, chanspec, tmp_max_pwr, max_pwr);
		}
#ifdef WL11AC
		else {
			wlc_phy_txpwr_apply_srom13(pi, band, chanspec, tmp_max_pwr, max_pwr);
		}
#endif
	  deltaPwr = pwrdet->max_pwr[core][band] - tmp_max_pwr;

	  if (deltaPwr > 0)
	    ppr_plus_cmn_val(max_pwr, deltaPwr);

	  ppr_apply_max(max_pwr, pwrdet->max_pwr[core][band]);
	}
}

static void
phy_ac_tpc_sromlimit_get(phy_type_tpc_ctx_t *ctx, chanspec_t chanspec, ppr_t *max_pwr, uint8 core)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	if (SROMREV(pi->sh->sromrev) < 12) {
	  wlc_phy_txpower_sromlimit_get_acphy(pi, chanspec, max_pwr, core);
	} else {
	  wlc_phy_txpower_sromlimit_get_srom12_acphy(pi, chanspec, max_pwr, core);
	}
}

/* report estimated power and adjusted estimated power in quarter dBms */
void
wlc_phy_txpwr_est_pwr_acphy(phy_info_t *pi, uint8 *Pout, uint8 *Pout_adj)
{
	uint8 core;
	int8 val;
	/* JIRA: SW4345-571: Ignore valid bit when reading estPwr for wl curpower for 4345
	 *Please refer to RB33098 for more details
	 */
	/* Do not check for valid bit for 4345(major rev3) since Reset CCA in
	 * ucode clears that bit
	 */
	if (ACMAJORREV_3(pi->pubpi->phy_rev))
		val = 1;

	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		if (!ACMAJORREV_3(pi->pubpi->phy_rev)) {
		val = READ_PHYREGFLDCE(pi, EstPower_path, core, estPowerValid);
		}

		/* Read the Actual Estimated Powers without adjustment */
		if (val) {
			Pout[core] = READ_PHYREGFLDCE(pi, EstPower_path, core, estPower);
		} else {
			Pout[core] = 0;
		}
		if (!ACMAJORREV_3(pi->pubpi->phy_rev)) {
		val = READ_PHYREGFLDCE(pi, TxPwrCtrlStatus_path, core, estPwrAdjValid);
		}
		if (val) {
			Pout_adj[core] = READ_PHYREGFLDCE(pi, TxPwrCtrlStatus_path, core,
			                                  estPwr_adj);
		} else {
			Pout_adj[core] = 0;
		}
	}
}

const uint16 *
wlc_phy_get_tx_pwrctrl_tbl_2069(phy_info_t *pi)
{
	const uint16 *tx_pwrctrl_tbl = NULL;

	if (CHSPEC_IS2G(pi->radio_chanspec)) {

		tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev0;

		if (PHY_IPA(pi)) {

			switch (RADIO2069REV(pi->pubpi->radiorev)) {
			case 16:
				tx_pwrctrl_tbl = acphy_txgain_ipa_2g_2069rev16;
				break;
			case 17:
				tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev17;
				break;
			case 23: /* iPa  case TXGain tables */
				tx_pwrctrl_tbl = acphy_txgain_ipa_2g_2069rev17;
				break;
			case 25: /* iPa  case TXGain tables */
				tx_pwrctrl_tbl = acphy_txgain_ipa_2g_2069rev25;
				break;
			case 18:
			case 24:
			case 26:
				tx_pwrctrl_tbl = acphy_txgain_ipa_2g_2069rev18;
				break;
			case 32:
			case 33:
			case 34:
			case 35:
			case 37:
			case 38:
				tx_pwrctrl_tbl = acphy_txgain_ipa_2g_2069rev33_37;
				break;
			case 39:
			case 40:
				tx_pwrctrl_tbl = acphy_txgain_ipa_2g_2069rev39;
				break;
			default:
				tx_pwrctrl_tbl = acphy_txgain_ipa_2g_2069rev0;
				break;

			}
		} else {

			switch (RADIO2069REV(pi->pubpi->radiorev)) {
			case 17:
			case 23:
			case 25:
				if (BF3_2GTXGAINTBL_BLANK(pi->u.pi_acphy)) {
					wlc_phy_gaintbl_blanking(pi, acphy_txgain_epa_2g_2069rev17,
					                         pi->sromi->txidxcap2g, TRUE);
				}
				tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev17;
				break;
			case 18:
			case 24:
			case 26:
				tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev18;
				break;
			case 4:
			case 8:
			case 7: /* 43602a0 uses radio rev4 tx pwr ctrl tables */
				switch (BF3_TXGAINTBLID(pi->u.pi_acphy)) {
				case 0:
					tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev4;
					break;
				case 1:
					tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev4_id1;
					break;
				default:
					tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev4;
					break;
				}
				break;
			case 16:
				tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev16;
				break;
			case 32:
			case 33:
				tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev33_35_36_37;
				break;
			case 34:
			case 36:
				tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev34;
				break;
			case 35:
			case 37:
			case 38:
			case 39:
				tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev33_35_36_37;
				break;
			case 64:
			case 66:
				tx_pwrctrl_tbl = pi->u.pi_acphy->gaintbl_2g;
				break;
			default:
				tx_pwrctrl_tbl = acphy_txgain_epa_2g_2069rev0;
				break;
			}
		}
	} else {
		if (PHY_IPA(pi)) {
			switch (RADIO2069REV(pi->pubpi->radiorev)) {
			case 17:
			case 23:
				tx_pwrctrl_tbl = acphy_txgain_ipa_5g_2069rev17;
				break;
			case 25:
				tx_pwrctrl_tbl = acphy_txgain_ipa_5g_2069rev25;
				break;
			case 18:
			case 24:
			case 26:
				tx_pwrctrl_tbl = acphy_txgain_ipa_5g_2069rev18;
				break;
			case 16:
				tx_pwrctrl_tbl = acphy_txgain_ipa_5g_2069rev16;
				break;
			case 32:
			case 33:
			case 34:
			case 35:
			case 37:
			case 38:
				tx_pwrctrl_tbl = acphy_txgain_ipa_5g_2069rev33_37;
				break;
			case 39:
			case 40:
				tx_pwrctrl_tbl = acphy_txgain_ipa_5g_2069rev39;
				break;
			default:
				tx_pwrctrl_tbl = acphy_txgain_ipa_5g_2069rev0;
				break;
			}
		} else {

			switch (RADIO2069REV(pi->pubpi->radiorev)) {
			case 17:
			case 23:
			case 25:
				if (BF3_5GTXGAINTBL_BLANK(pi->u.pi_acphy)) {
					wlc_phy_gaintbl_blanking(pi, acphy_txgain_epa_5g_2069rev17,
					                         pi->sromi->txidxcap5g, TRUE);
				}
				tx_pwrctrl_tbl = acphy_txgain_epa_5g_2069rev17;
				break;
			case 18:
			case 24:
			case 26:
				tx_pwrctrl_tbl = acphy_txgain_epa_5g_2069rev18;
				break;
			case 4:
			case 8:
			case 7: /* 43602a0 uses radio rev4 tx pwr ctrl tables */
				tx_pwrctrl_tbl = acphy_txgain_epa_5g_2069rev4;
				break;
			case 64:
			case 66:
				tx_pwrctrl_tbl = pi->u.pi_acphy->gaintbl_5g;
				break;
			case 16:
				tx_pwrctrl_tbl = acphy_txgain_epa_5g_2069rev16;
				break;
			case 32:
			case 33:
				tx_pwrctrl_tbl = acphy_txgain_epa_5g_2069rev33_35_36;
				break;
			case 34:
			case 36:
				tx_pwrctrl_tbl = acphy_txgain_epa_5g_2069rev34;
				break;
			case 35:
				tx_pwrctrl_tbl = acphy_txgain_epa_5g_2069rev33_35_36;
				break;
			case 37:
			case 38:
			case 39:
				tx_pwrctrl_tbl = acphy_txgain_epa_5g_2069rev37_38;
				break;
			default:
				tx_pwrctrl_tbl = acphy_txgain_epa_5g_2069rev0;
				break;

			}
		}
	}

	return tx_pwrctrl_tbl;
}

#ifdef PREASSOC_PWRCTRL
static void
wlc_phy_store_tx_pwrctrl_setting_acphy(phy_type_tpc_ctx_t *ctx, chanspec_t previous_channel)
{
	phy_ac_tpc_info_t *tpci = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = tpci->pi;
	uint8 core, iidx;

	if (!pi->sh->up)
		return;

	if (CHSPEC_IS5G(previous_channel)) {
		tpci->pwr_ctrl_save->last_chan_stored_5g = previous_channel;

	} else {
		tpci->pwr_ctrl_save->last_chan_stored_2g = previous_channel;

	}
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		iidx = READ_PHYREGFLDCE(pi, TxPwrCtrlStatus_path, core, baseIndex);
		if (CHSPEC_IS5G(previous_channel)) {
			tpci->pwr_ctrl_save->status_idx_5g[core] = iidx;
			tpci->pwr_ctrl_save->pwr_qdbm_5g[core] =
			        wlc_phy_txpwrctrl_get_target_acphy(pi, core);
			tpci->pwr_ctrl_save->stored_not_restored_5g[core] = TRUE;

		} else {
			tpci->pwr_ctrl_save->status_idx_2g[core] = iidx;
			tpci->pwr_ctrl_save->pwr_qdbm_2g[core] =
			        wlc_phy_txpwrctrl_get_target_acphy(pi, core);
			tpci->pwr_ctrl_save->stored_not_restored_5g[core] = TRUE;
		}

	}
}
#endif /* PREASSOC_PWRCTRL */

void
wlc_phy_txpwrctrl_set_target_acphy(phy_info_t *pi, uint8 pwr_qtrdbm, uint8 core)
{
	/* set target powers in 6.2 format (in dBs) */
	switch (core) {
	case 0:
		MOD_PHYREG(pi, TxPwrCtrlTargetPwr_path0, targetPwr0, pwr_qtrdbm);
		break;
	case 1:
		MOD_PHYREG(pi, TxPwrCtrlTargetPwr_path1, targetPwr1, pwr_qtrdbm);
		break;
	case 2:
		MOD_PHYREG(pi, TxPwrCtrlTargetPwr_path2, targetPwr2, pwr_qtrdbm);
		break;
	case 3:
		MOD_PHYREG(pi, TxPwrCtrlTargetPwr_path3, targetPwr3, pwr_qtrdbm);
		break;
	}
}

/* Set init index and cycle clocks to propagate init index to base index */
void
wlc_phy_txpwrctrl_set_baseindex(phy_info_t *pi, uint8 core, uint8 baseindex, bool frame_type)
{
	uint16 txpwrctrlcmd_SAVE;
#if defined(WLC_TXCAL)
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;
#endif

	txpwrctrlcmd_SAVE = READ_PHYREG(pi, TxPwrCtrlCmd);
	if (frame_type) {
		MOD_PHYREGCEE(pi, TxPwrCtrlInit_path, core,
			pwrIndex_init_path, baseindex);
	} else {
		MOD_PHYREGCEE(pi, TxPwrCtrlInit_path, core,
			pwr_index_init_cck_path, baseindex);
	}

#if defined(WLC_TXCAL)
	/* Update saved baseindex */
	if (pi->olpci->olpc_idx_in_use && pi->olpci->olpc_idx_valid) {
		if (frame_type) {
			pi_ac->tpci->txpwrindex_hw_save[core] = baseindex;
		} else {
			pi_ac->tpci->txpwrindex_cck_hw_save[core] = baseindex;
		}
	}
#endif	/* WLC_TXCAL */

	if (!ACMAJORREV_40(pi->pubpi->phy_rev)) {
		wlapi_bmac_phyclk_fgc(pi->sh->physhim, ON);
		MOD_PHYREG(pi, TxPwrCtrlCmd, txPwrCtrl_en, 0);
		MOD_PHYREG(pi, TxPwrCtrlCmd, hwtxPwrCtrl_en, 1);
		MOD_PHYREG(pi, TxPwrCtrlCmd, txpwrctrlReset, 1);
		OSL_DELAY(2);
		MOD_PHYREG(pi, TxPwrCtrlCmd, txpwrctrlReset, 0);
		WRITE_PHYREG(pi, TxPwrCtrlCmd, txpwrctrlcmd_SAVE);
		wlapi_bmac_phyclk_fgc(pi->sh->physhim, OFF);
	}
	PHY_TXPWR(("%s baseindex set to %d for core %d\n",
		__FUNCTION__, baseindex, core));
}

void
BCMATTACHFN(wlc_phy_txpwrctrl_config_acphy)(phy_info_t *pi)
{
	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	pi->hwpwrctrl_capable = TRUE;
	pi->txpwrctrl = PHY_TPC_HW_ON;
	pi->phy_5g_pwrgain = TRUE;
}

int
phy_ac_tpc_txpower_core_offset_set(phy_type_tpc_ctx_t *ctx, struct phy_txcore_pwr_offsets *offsets)
{
	int8 core_offset;
	int core, offset_changed = FALSE;
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;

	FOREACH_CORE(pi, core) {
		core_offset = offsets->offset[core];
		if (core_offset != 0 && core > pi->pubpi->phy_corenum) {
			return BCME_BADARG;
		}

		if (info->txpwr_offset[core] != core_offset) {
			offset_changed = TRUE;
			info->txpwr_offset[core] = core_offset;
		}
	}

	/* Apply the new per-core targets to the hw */
	if (pi->sh->clk && offset_changed) {
		wlapi_suspend_mac_and_wait(pi->sh->physhim);
		wlc_phy_txpower_recalc_target_acphy(pi);
		wlapi_enable_mac(pi->sh->physhim);
	}
	return BCME_OK;
}

int
phy_ac_tpc_txpower_core_offset_get(phy_type_tpc_ctx_t *ctx, struct phy_txcore_pwr_offsets *offsets)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	int core;

	memset(offsets, 0, sizeof(struct phy_txcore_pwr_offsets));

	FOREACH_CORE(info->pi, core) {
		offsets->offset[core] = info->txpwr_offset[core];
	}
	return BCME_OK;
}

#if defined(WL_SARLIMIT) || defined(WL_SAR_SIMPLE_CONTROL)
static void
wlc_phy_set_sarlimit_acphy(phy_type_tpc_ctx_t *ctx)
{
	phy_ac_tpc_info_t *tpci = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = tpci->pi;
	uint core;
	int16 txpwr_sarcap[3] = { 0, 0, 0 };

#ifdef FCC_PWR_LIMIT_2G
	uint cur_chan = CHSPEC_CHANNEL(pi->radio_chanspec);
	phy_tpc_data_t *data = tpci->ti->data;
#endif /* FCC_PWR_LIMIT_2G */

	ASSERT(pi->sh->clk);
	wlapi_suspend_mac_and_wait(pi->sh->physhim);
	phy_utils_phyreg_enter(pi);
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		txpwr_sarcap[core] = pi->tpci->data->sarlimit[core];
#ifdef FCC_PWR_LIMIT_2G
		if (data->cfg.srom_tworangetssi2g &&
			(data->fcc_pwr_limit_2g || data->cfg.fccpwroverride)) {
			if (cur_chan == 12 && data->cfg.fccpwrch12 > 0) {
				txpwr_sarcap[core] = MIN(data->cfg.fccpwrch12, txpwr_sarcap[core]);
			} else if (cur_chan == 13 && data->cfg.fccpwrch13 > 0) {
				txpwr_sarcap[core] = MIN(data->cfg.fccpwrch13, txpwr_sarcap[core]);
			}
		}
#endif /* FCC_PWR_LIMIT_2G */
#ifdef WL_SARLIMIT
		if ((tpci->txpwr_offset[core] != 0) &&
		    !CHSPEC_IS5G(pi->radio_chanspec))
			txpwr_sarcap[core] = wlc_phy_calc_adjusted_cap_rgstr_acphy(pi, core);
#endif
	}

	IF_ACTV_CORE(pi, pi->sh->phyrxchain, 0)
	        MOD_PHYREG(pi, TxPwrCapping_path0,
	                   maxTxPwrCap_path0, txpwr_sarcap[0]);
	IF_ACTV_CORE(pi, pi->sh->phyrxchain, 1)
	        MOD_PHYREG(pi, TxPwrCapping_path1,
	                   maxTxPwrCap_path1, txpwr_sarcap[1]);
	IF_ACTV_CORE(pi, pi->sh->phyrxchain, 2)
	        MOD_PHYREG(pi, TxPwrCapping_path2,
	                   maxTxPwrCap_path2, txpwr_sarcap[2]);
	phy_utils_phyreg_exit(pi);
	wlapi_enable_mac(pi->sh->physhim);
}
#endif /* WL_SARLIMIT || WL_SAR_SIMPLE_CONTROL */

static void
wlc_acphy_avvmid_txcal(phy_type_tpc_ctx_t *ctx, wlc_phy_avvmid_txcal_t *avvmidinfo, bool set)
{
	uint8 i, j;
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	if (set) {
		FOREACH_CORE(info->pi, i) {
			for (j = 0; j < NUM_SUBBANDS_FOR_AVVMID; j++) {
				info->aci->sromi->avvmid_set_from_nvram[i][j][0] =
					avvmidinfo->avvmid[i][j].Av;
				info->aci->sromi->avvmid_set_from_nvram[i][j][1] =
					avvmidinfo->avvmid[i][j].Vmid;
			}
		}
	} else {
		FOREACH_CORE(info->pi, i) {
			for (j = 0; j < NUM_SUBBANDS_FOR_AVVMID; j++) {
				avvmidinfo->avvmid[i][j].Av =
					info->aci->sromi->avvmid_set_from_nvram[i][j][0];
				avvmidinfo->avvmid[i][j].Vmid =
					info->aci->sromi->avvmid_set_from_nvram[i][j][1];
			}
		}
	}
}

static void
BCMATTACHFN(wlc_phy_txpwr_srom11_read_pwrdet)(phy_info_t *pi, srom11_pwrdet_t * pwrdet,
	uint8 param, uint8 band, uint8 offset,  const char * name)
{
	pwrdet->pwrdet_a1[param][band] =
		(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, name, offset);
	offset++;
	pwrdet->pwrdet_b0[param][band] =
		(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, name, offset);
	offset++;
	pwrdet->pwrdet_b1[param][band] =
		(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, name, offset);
}

#ifdef BAND5G
static void
BCMATTACHFN(wlc_phy_txpwr_srom11_read_5Gsubbands)(phy_info_t *pi, srom11_pwrdet_t * pwrdet,
	uint8 subband, bool update_rsdb_core1_params, uint8 ant)
{
	uint8 b, b_triple_offset;

	b = subband + CH_2G_GROUP;
	b_triple_offset = 3 * subband;

	/* CORE 0 */
	pwrdet->max_pwr[0][b] = (int8)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_maxp5ga0, subband);
	wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 0, b, b_triple_offset, rstr_pa5ga0);
	pwrdet->tssifloor[0][b] = (int16)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
		rstr_tssifloor5g, subband, 0);

	/* CORE 1 no reuse */
	if ((PHYCORENUM(pi->pubpi->phy_corenum) > 1) ||
		(update_rsdb_core1_params == TRUE)) {
		pwrdet->max_pwr[ant][b] =
			(int8)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_maxp5ga1, subband);
		pwrdet->tssifloor[ant][b] = (int16)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
			rstr_tssifloor5g, subband, 0);
	}
	/* CORE 1 or reuse */
	if ((PHYCORENUM(pi->pubpi->phy_corenum) > 1) ||
		(pi->tpci->data->cfg.srom_tworangetssi5g &&
		ACMAJORREV_1(pi->pubpi->phy_rev)) ||
		(update_rsdb_core1_params == TRUE))  {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, ant, b, b_triple_offset, rstr_pa5ga1);
	}
	/* CORE 2 no reuse */
	if (PHYCORENUM(pi->pubpi->phy_corenum) > 2) {
		pwrdet->max_pwr[2][b] = (int8)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_maxp5ga2, subband);
		pwrdet->tssifloor[2][b] = (int16)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
			rstr_tssifloor5g, subband, 0);
	}
	/* CORE 2 or reuse */
	if (PHYCORENUM(pi->pubpi->phy_corenum) > 2 ||
		(pi->tpci->data->cfg.srom_tworangetssi5g &&
			ACMAJORREV_2(pi->pubpi->phy_rev))) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 2, b, b_triple_offset, rstr_pa5ga2);
	}
	/* CORE 3 reuse only */
	if (pi->tpci->data->cfg.srom_tworangetssi5g &&
		ACMAJORREV_2(pi->pubpi->phy_rev)) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 3, b, b_triple_offset, rstr_pa5ga3);
	}
	if (pi->tpci->data->cfg.srom_tworangetssi5g &&
		ACMAJORREV_4(pi->pubpi->phy_rev)) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 3, b, b_triple_offset, rstr_pa5ga2);
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 3, b, b_triple_offset, rstr_pa5ga3);
	}
	/* Partial BW */
	if ((BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) &&
		ACMAJORREV_1(pi->pubpi->phy_rev)) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 1, b,
			b_triple_offset, rstr_pa5gbw40a0);
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 2, b,
			b_triple_offset, rstr_pa5gbw80a0);
	}
	if ((ACMAJORREV_2(pi->pubpi->phy_rev) ||
		ACMAJORREV_4(pi->pubpi->phy_rev)) &&
		BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 2, b,
			b_triple_offset, rstr_pa5gbw4080a0);
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 2 + ant, b,
			b_triple_offset, rstr_pa5gbw4080a1);
	}
}
#endif /* BAND5G */

static bool BCMATTACHFN(wlc_phy_txpwr_srom11_read)(phy_type_tpc_ctx_t *ctx)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	srom11_pwrdet_t *pwrdet = pi->pwrdet_ac;
	uint8 ant = 1;
	bool update_rsdb_core1_params = FALSE;
#ifdef POWPERCHANNL
	uint8 ch;
#endif /* POWPERCHANNL */
	ASSERT(pi->sh->sromrev >= 11);

	if (ACMAJORREV_4(pi->pubpi->phy_rev) && (phy_get_phymode(pi) == PHYMODE_RSDB))
	{
		/* update pi[0] to hold pwrdet params for all cores */
		/* This is required for mimo operation */
		if (phy_get_current_core(pi) == PHY_RSBD_PI_IDX_CORE0)
		{
			pi->pubpi->phy_corenum <<= 1;
		}
		else if (phy_get_current_core(pi) == PHY_RSBD_PI_IDX_CORE1)
		{
			/* Incase of RSDB Core1 PA PD MAXPwr Params needs to be
			 * updated from Core1 nvram params
			 */
			ant = 0;
			update_rsdb_core1_params = TRUE;
		}
	}
	/* read pwrdet params for each band/subband */

	/* 2G band */
	pwrdet->max_pwr[0][0] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2ga0);
	wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 0, 0, 0, rstr_pa2ga0);
	pwrdet->tssifloor[0][0] = (int16)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
		rstr_tssifloor2g, 0, 0);
#ifdef POWPERCHANNL
	if (PWRPERCHAN_ENAB(pi)) {
		/* power per channel and Temp */
		pwrdet->max_pwr_SROM2G[0] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2ga0);

		PHY_TXPWR(("wl%d: %s: Loading max = %d \n",
			pi->sh->unit, __FUNCTION__,
			pwrdet->max_pwr_SROM2G[0]));
		for (ch = 0; ch < CH20MHz_NUM_2G; ch++) {
			pwrdet->PwrOffsets2GNormTemp[0][ch] =
				(int8)PHY_GETINTVAR_ARRAY_DEFAULT(pi, rstr_PowOffs2GTNA0, ch, 0);
			pwrdet->PwrOffsets2GLowTemp[0][ch] =
				(int8)PHY_GETINTVAR_ARRAY_DEFAULT(pi, rstr_PowOffs2GTLA0, ch, 0);
			pwrdet->PwrOffsets2GHighTemp[0][ch] =
				(int8)PHY_GETINTVAR_ARRAY_DEFAULT(pi, rstr_PowOffs2GTHA0, ch, 0);

			PHY_TXPWR(("Core=0 Ch=%d Offset: Norm=%d Low=%d High=%d\n",
				ch, pwrdet->PwrOffsets2GNormTemp[0][ch],
				pwrdet->PwrOffsets2GLowTemp[0][ch],
				pwrdet->PwrOffsets2GHighTemp[0][ch]));
		}
		pwrdet->Low2NormTemp =
			(int16)PHY_GETINTVAR_ARRAY_DEFAULT(pi, rstr_PowOffsTempRange, 0, 0xff);
		pwrdet->High2NormTemp =
			(int16)PHY_GETINTVAR_ARRAY_DEFAULT(pi, rstr_PowOffsTempRange, 1, 0xff);

		PHY_TXPWR((" Low Temp Limit=%d	High Temp Limit=%d \n",
			pwrdet->Low2NormTemp, pwrdet->High2NormTemp));
	}
#endif  /* POWPERCHANNL */
	if ((PHYCORENUM(pi->pubpi->phy_corenum) > 1) ||
		(update_rsdb_core1_params == TRUE)) {
		pwrdet->max_pwr[ant][0] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2ga1);
		pwrdet->tssifloor[ant][0] = (int16)PHY_GETINTVAR_ARRAY_DEFAULT(
			pi, rstr_tssifloor2g, 1, 0);
#ifdef POWPERCHANNL
		if (PWRPERCHAN_ENAB(pi)) {
			pwrdet->max_pwr_SROM2G[ant] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2ga1);
			for (ch = 0; ch < CH20MHz_NUM_2G; ch++) {
				pwrdet->PwrOffsets2GNormTemp[ant][ch] =
						(int8)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
						rstr_PowOffs2GTNA1, ch, 0);
				pwrdet->PwrOffsets2GLowTemp[ant][ch] =
						(int8)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
						rstr_PowOffs2GTLA1, ch, 0);
				pwrdet->PwrOffsets2GHighTemp[ant][ch] =
						(int8)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
						rstr_PowOffs2GTHA1, ch, 0);

				PHY_TXPWR(("Core=1 Ch=%d Offset:", ch));
				PHY_TXPWR(("Norm=%d Low=%d High=%d\n",
					pwrdet->PwrOffsets2GNormTemp[ant][ch],
					pwrdet->PwrOffsets2GLowTemp[ant][ch],
					pwrdet->PwrOffsets2GHighTemp[ant][ch]));
			}
		}
#endif  /* POWPERCHANNL */
	}
	if ((PHYCORENUM(pi->pubpi->phy_corenum) > 1) ||
		(pi->tpci->data->cfg.srom_tworangetssi2g &&
		ACMAJORREV_1(pi->pubpi->phy_rev)) ||
		(update_rsdb_core1_params == TRUE)) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, ant, 0, 0, rstr_pa2ga1);
	}
	if (PHYCORENUM(pi->pubpi->phy_corenum) > 2 ||
		(pi->tpci->data->cfg.srom_tworangetssi2g &&
		ACMAJORREV_2(pi->pubpi->phy_rev))) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 2, 0, 0, rstr_pa2ga2);
	}
	if ((pi->tpci->data->cfg.srom_tworangetssi2g &&
		ACMAJORREV_4(pi->pubpi->phy_rev))) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 2, 0, 0, rstr_pa2ga2);
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 2, 0, 0, rstr_pa2ga3);
	}
	if (PHYCORENUM(pi->pubpi->phy_corenum) > 2) {
		pwrdet->max_pwr[2][0] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2ga2);
		pwrdet->tssifloor[2][0] = (int16)PHY_GETINTVAR_ARRAY_DEFAULT(
			pi, rstr_tssifloor2g, 2, 0);
#ifdef POWPERCHANNL
		if (PWRPERCHAN_ENAB(pi)) {
			pwrdet->max_pwr_SROM2G[2] =
				(int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2ga2);
			for (ch = 0; ch < CH20MHz_NUM_2G; ch++) {
				pwrdet->PwrOffsets2GNormTemp[2][ch] =
						(int8)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
						rstr_PowOffs2GTNA2, ch, 0);
				pwrdet->PwrOffsets2GLowTemp[2][ch] =
						(int8)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
						rstr_PowOffs2GTLA2, ch, 0);
				pwrdet->PwrOffsets2GHighTemp[2][ch] =
						(int8)PHY_GETINTVAR_ARRAY_DEFAULT(pi,
						rstr_PowOffs2GTHA2, ch, 0);
			}
			/* input range limit for power per channel */
			wlc_phy_tx_target_pwr_per_channel_limit_acphy(pi);
		}
#endif  /* POWPERCHANNL */
	}
	if (pi->tpci->data->cfg.srom_tworangetssi2g &&
		ACMAJORREV_2(pi->pubpi->phy_rev)) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 3, 0, 0, rstr_pa2ga3);
	}
#ifdef POWPERCHANNL
	if (PWRPERCHAN_ENAB(pi)) {
		/* input range limit for power per channel */
		wlc_phy_tx_target_pwr_per_channel_limit_acphy(pi);
	}
#endif  /* POWPERCHANNL */
	if ((BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) &&
		ACMAJORREV_1(pi->pubpi->phy_rev)) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 1, 0, 0, rstr_pa2gccka0);
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 2, 0, 0, rstr_pa2gbw40a0);
	}
	if ((BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) &&
		(((ACMAJORREV_2(pi->pubpi->phy_rev)) && (PHY_IPA(pi))) ||
		(ACMAJORREV_4(pi->pubpi->phy_rev)))) {
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 1, 0, 0, rstr_pa2gccka0);
		wlc_phy_txpwr_srom11_read_pwrdet(pi, pwrdet, 2, 0, 0, rstr_pa2gccka1);
	}

	/* 5G band */
#ifdef BAND5G
	wlc_phy_txpwr_srom11_read_5Gsubbands(pi, pwrdet, 0, update_rsdb_core1_params, ant);
	wlc_phy_txpwr_srom11_read_5Gsubbands(pi, pwrdet, 1, update_rsdb_core1_params, ant);
	wlc_phy_txpwr_srom11_read_5Gsubbands(pi, pwrdet, 2, update_rsdb_core1_params, ant);
	wlc_phy_txpwr_srom11_read_5Gsubbands(pi, pwrdet, 3, update_rsdb_core1_params, ant);
#endif  /* BAND5G */

	wlc_phy_txpwr_srom11_read_ppr(pi);
	/* read out power detect offset values */
	pwrdet->pdoffset2g40_flag = (uint8)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset2g40mvalid);
	pwrdet->pdoffset40[0] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset40ma0);
	pwrdet->pdoffset80[0] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset80ma0);
	pwrdet->pdoffset5gsubband[0] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset5gsubbanda0);
	pwrdet->pdoffset2g40[0] = (uint8)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset2g40ma0);
	pwrdet->pdoffsetcck[0] = (uint8)PHY_GETINTVAR_SLICE(pi, rstr_pdoffsetcckma0);
	pi->tpci->data->cfg.cckpwroffset[0] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_cckpwroffset0);
	pi->tpci->data->cfg.cckulbpwroffset[0] = (int8)PHY_GETINTVAR_SLICE(pi,
		rstr_cckulbpwroffset0);
	pi->sh->cckPwrIdxCorr = (int8) PHY_GETINTVAR_SLICE(pi, rstr_cckPwrIdxCorr);
	pi->sromi->txpwr2gAdcScale = (uint8) PHY_GETINTVAR_DEFAULT(pi, rstr_txpwr2gAdcScale, -1);
	pi->sromi->txpwr5gAdcScale = (uint8) PHY_GETINTVAR_DEFAULT(pi, rstr_txpwr5gAdcScale, -1);
	if ((PHYCORENUM(pi->pubpi->phy_corenum) > 1) ||
		(update_rsdb_core1_params == TRUE)) {
		pwrdet->pdoffset40[ant] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset40ma1);
		pwrdet->pdoffset80[ant] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset80ma1);
		pwrdet->pdoffset5gsubband[ant] =
			(uint16)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset5gsubbanda1);
		pwrdet->pdoffset2g40[ant] = (uint8)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset2g40ma1);
		pwrdet->pdoffsetcck[ant] = (uint8)PHY_GETINTVAR_SLICE(pi, rstr_pdoffsetcckma1);
		pi->tpci->data->cfg.cckpwroffset[ant] = (int8)PHY_GETINTVAR_SLICE(pi,
			rstr_cckpwroffset1);
		pi->tpci->data->cfg.cckulbpwroffset[ant] = (int8)PHY_GETINTVAR_SLICE(pi,
			rstr_cckulbpwroffset1);
	}
	if (PHYCORENUM(pi->pubpi->phy_corenum) > 2) {
		pwrdet->pdoffset40[2] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset40ma2);
		pwrdet->pdoffset80[2] = (uint16)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset80ma2);
		pwrdet->pdoffset2g40[2] = (uint8)PHY_GETINTVAR_SLICE(pi, rstr_pdoffset2g40ma2);
		pwrdet->pdoffsetcck[2] = (uint8)PHY_GETINTVAR_SLICE(pi, rstr_pdoffsetcckma2);
		pi->tpci->data->cfg.cckpwroffset[2] = (int8)PHY_GETINTVAR_SLICE(pi,
			rstr_cckpwroffset2);
		pi->tpci->data->cfg.cckulbpwroffset[2] = (int8)PHY_GETINTVAR_SLICE(pi,
			rstr_cckulbpwroffset2);
	}
	if (ACMAJORREV_4(pi->pubpi->phy_rev)) {
		if ((phy_get_phymode(pi) == PHYMODE_RSDB) &&
			(phy_get_current_core(pi) == PHY_RSBD_PI_IDX_CORE0)) {
			pi->pubpi->phy_corenum >>= 1;
		}
	}
	pi->phy_tempsense_offset = (int8)PHY_GETINTVAR_SLICE(pi, rstr_tempoffset);
	if (pi->phy_tempsense_offset == -1) {
		pi->phy_tempsense_offset = 0;
	} else if (pi->phy_tempsense_offset != 0) {
		if (pi->phy_tempsense_offset >
			(ACPHY_SROM_TEMPSHIFT + ACPHY_SROM_MAXTEMPOFFSET)) {
			pi->phy_tempsense_offset = ACPHY_SROM_MAXTEMPOFFSET;
		} else if (pi->phy_tempsense_offset < (ACPHY_SROM_TEMPSHIFT +
			ACPHY_SROM_MINTEMPOFFSET)) {
			pi->phy_tempsense_offset = ACPHY_SROM_MINTEMPOFFSET;
		} else {
			pi->phy_tempsense_offset -= ACPHY_SROM_TEMPSHIFT;
		}
	}

	/* For ACPHY, if the SROM contains a bogus value, then tempdelta
	 * will default to ACPHY_DEFAULT_CAL_TEMPDELTA. If the SROM contains
	 * a valid value, then the default will be overwritten with this value
	 */
	wlc_phy_read_tempdelta_settings(pi, ACPHY_CAL_MAXTEMPDELTA);

	return TRUE;

}

static bool BCMATTACHFN(wlc_phy_txpwr_srom12_read)(phy_type_tpc_ctx_t *ctx)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;

	srom12_pwrdet_t *pwrdet = pi->pwrdet_ac;
	uint8 b = 0, core;
	if (!(SROMREV(pi->sh->sromrev) < 12)) {
#ifdef BAND5G
	    uint8 i = 0, j = 0, maxval = 0;
	    maxval = CH_5G_5BAND * 4; /* PAparams per subband for particular bandwidth = 4 */
#endif

	    ASSERT(pi->sh->sromrev >= 12);

	    /* read pwrdet params for each band/subband/bandwidth */
	    /* 2G_20MHz */
	    pwrdet->pwrdet_a[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga0, 0);
	    pwrdet->pwrdet_b[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga0, 1);
	    pwrdet->pwrdet_c[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga0, 2);
	    pwrdet->pwrdet_d[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga0, 3);
	    /* 2G_40MHz */
	    pwrdet->pwrdet_a_40[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a0, 0);
	    pwrdet->pwrdet_b_40[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a0, 1);
	    pwrdet->pwrdet_c_40[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a0, 2);
	    pwrdet->pwrdet_d_40[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a0, 3);

	    if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
		/* 2G_20MHz */
		pwrdet->pwrdet_a[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga1, 0);
		pwrdet->pwrdet_b[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga1, 1);
		pwrdet->pwrdet_c[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga1, 2);
		pwrdet->pwrdet_d[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga1, 3);
		/* 2G_40MHz */
		pwrdet->pwrdet_a_40[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a1, 0);
		pwrdet->pwrdet_b_40[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a1, 1);
		pwrdet->pwrdet_c_40[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a1, 2);
		pwrdet->pwrdet_d_40[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a1, 3);
	    }
	    if (PHYCORENUM(pi->pubpi->phy_corenum) > 2) {
		/* 2G_20MHz */
		pwrdet->pwrdet_a[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga2, 0);
		pwrdet->pwrdet_b[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga2, 1);
		pwrdet->pwrdet_c[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga2, 2);
		pwrdet->pwrdet_d[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga2, 3);
		/* 2G_40MHz */
		pwrdet->pwrdet_a_40[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a2, 0);
		pwrdet->pwrdet_b_40[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a2, 1);
		pwrdet->pwrdet_c_40[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a2, 2);
		pwrdet->pwrdet_d_40[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a2, 3);
	    }
	    if (PHYCORENUM(pi->pubpi->phy_corenum) > 3) {
		/* 2G_20MHz */
		pwrdet->pwrdet_a[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga3, 0);
		pwrdet->pwrdet_b[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga3, 1);
		pwrdet->pwrdet_c[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga3, 2);
		pwrdet->pwrdet_d[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2ga3, 3);
		/* 2G_40MHz */
		pwrdet->pwrdet_a_40[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a3, 0);
		pwrdet->pwrdet_b_40[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a3, 1);
		pwrdet->pwrdet_c_40[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a3, 2);
		pwrdet->pwrdet_d_40[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa2g40a3, 3);
	    }

	    ++b;

#ifdef BAND5G
	    for (i = 0; i < maxval; i = i + 4) {
		j = b - 1; /* 5G 80 MHz index starts from 0 */
		/* 5G_BANDS_20MHz */
		pwrdet->pwrdet_a[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga0, i);
		pwrdet->pwrdet_b[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga0, i+1);
		pwrdet->pwrdet_c[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga0, i+2);
		pwrdet->pwrdet_d[0][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga0, i+3);

		/* 5G_BANDS_40MHz */
		pwrdet->pwrdet_a_40[0][b] =
			(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a0, i);
		pwrdet->pwrdet_b_40[0][b] =
			(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a0, i+1);
		pwrdet->pwrdet_c_40[0][b] =
			(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a0, i+2);
		pwrdet->pwrdet_d_40[0][b] =
			(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a0, i+3);

		/* 5G_BANDS_80MHz */
		pwrdet->pwrdet_a_80[0][j] =
			(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a0, i);
		pwrdet->pwrdet_b_80[0][j] =
			(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a0, i+1);
		pwrdet->pwrdet_c_80[0][j] =
			(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a0, i+2);
		pwrdet->pwrdet_d_80[0][j] =
			(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a0, i+3);

		if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
		    /* 5G_BANDS_20MHz */
		    pwrdet->pwrdet_a[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga1, i);
		    pwrdet->pwrdet_b[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga1, i+1);
		    pwrdet->pwrdet_c[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga1, i+2);
		    pwrdet->pwrdet_d[1][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga1, i+3);
		    /* 5G_BANDS_40MHz */
			pwrdet->pwrdet_a_40[1][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a1, i);
			pwrdet->pwrdet_b_40[1][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a1, i+1);
			pwrdet->pwrdet_c_40[1][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a1, i+2);
			pwrdet->pwrdet_d_40[1][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a1, i+3);
		    /* 5G_BANDS_80MHz */
			pwrdet->pwrdet_a_80[1][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a1, i);
			pwrdet->pwrdet_b_80[1][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a1, i+1);
			pwrdet->pwrdet_c_80[1][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a1, i+2);
			pwrdet->pwrdet_d_80[1][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a1, i+3);
		}

		if (PHYCORENUM(pi->pubpi->phy_corenum) > 2) {
		    /* 5G_BANDS_20MHz */
		    pwrdet->pwrdet_a[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga2, i);
		    pwrdet->pwrdet_b[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga2, i+1);
		    pwrdet->pwrdet_c[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga2, i+2);
		    pwrdet->pwrdet_d[2][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga2, i+3);
		    /* 5G_BANDS_40MHz */
			pwrdet->pwrdet_a_40[2][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a2, i);
			pwrdet->pwrdet_b_40[2][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a2, i+1);
			pwrdet->pwrdet_c_40[2][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a2, i+2);
			pwrdet->pwrdet_d_40[2][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a2, i+3);
		    /* 5G_BANDS_80MHz */
			pwrdet->pwrdet_a_80[2][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a2, i);
			pwrdet->pwrdet_b_80[2][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a2, i+1);
			pwrdet->pwrdet_c_80[2][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a2, i+2);
			pwrdet->pwrdet_d_80[2][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a2, i+3);
		}
		if (PHYCORENUM(pi->pubpi->phy_corenum) > 3) {
		    /* 5G_BANDS_20MHz */
		    pwrdet->pwrdet_a[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga3, i);
		    pwrdet->pwrdet_b[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga3, i+1);
		    pwrdet->pwrdet_c[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga3, i+2);
		    pwrdet->pwrdet_d[3][b] = (int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5ga3, i+3);
		    /* 5G_BANDS_40MHz */
			pwrdet->pwrdet_a_40[3][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a3, i);
			pwrdet->pwrdet_b_40[3][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a3, i+1);
			pwrdet->pwrdet_c_40[3][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a3, i+2);
			pwrdet->pwrdet_d_40[3][b] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g40a3, i+3);

		    /* 5G_BANDS_80MHz */
			pwrdet->pwrdet_a_80[3][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a3, i);
			pwrdet->pwrdet_b_80[3][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a3, i+1);
			pwrdet->pwrdet_c_80[3][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a3, i+2);
			pwrdet->pwrdet_d_80[3][j] =
				(int16)PHY_GETINTVAR_ARRAY_SLICE(pi, rstr_pa5g80a3, i+3);
		}
		++b;
	    }

	    i = 0;
	    pwrdet->max_pwr[0][i++] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2gb0a0);
	    pwrdet->max_pwr[0][i++] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb0a0);
	    pwrdet->max_pwr[0][i++] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb1a0);
	    pwrdet->max_pwr[0][i++] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb2a0);
	    pwrdet->max_pwr[0][i++] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb3a0);
	    pwrdet->max_pwr[0][i++] = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb4a0);

	    if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
		i = 0;
		pwrdet->max_pwr[1][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2gb0a1);
		pwrdet->max_pwr[1][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb0a1);
		pwrdet->max_pwr[1][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb1a1);
		pwrdet->max_pwr[1][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb2a1);
		pwrdet->max_pwr[1][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb3a1);
		pwrdet->max_pwr[1][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb4a1);
	    }

	    if (PHYCORENUM(pi->pubpi->phy_corenum) > 2) {
		i = 0;
		pwrdet->max_pwr[2][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2gb0a2);
		pwrdet->max_pwr[2][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb0a2);
		pwrdet->max_pwr[2][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb1a2);
		pwrdet->max_pwr[2][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb2a2);
		pwrdet->max_pwr[2][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb3a2);
		pwrdet->max_pwr[2][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb4a2);
	    }
	    if (PHYCORENUM(pi->pubpi->phy_corenum) > 3) {
		i = 0;
		pwrdet->max_pwr[3][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp2gb0a3);
		pwrdet->max_pwr[3][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb0a3);
		pwrdet->max_pwr[3][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb1a3);
		pwrdet->max_pwr[3][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb2a3);
		pwrdet->max_pwr[3][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb3a3);
		pwrdet->max_pwr[3][i++]  = (int8)PHY_GETINTVAR_SLICE(pi, rstr_maxp5gb4a3);
	    }
#endif /* BAND5G */
	}

	if (SROMREV(pi->sh->sromrev) < 13) {
		wlc_phy_txpwr_srom12_read_ppr(pi);
	}
#ifdef WL11AC
	else {
		wlc_phy_txpwr_srom13_read_ppr(pi);
	}
#endif /* WL11AC */

	if (!(SROMREV(pi->sh->sromrev) < 12)) {
	    /* read out power detect offset values */
	    FOREACH_CORE(pi, core) {
			if (core < 3) {
				pwrdet->pdoffsetcck[core] =
					PDOFFSET(pi, rstr_pdoffset2gcck, core);
				pwrdet->pdoffsetcck20m[core] =
					PDOFFSET(pi, rstr_pdoffset2gcck20m, core);
				pwrdet->pdoffset20in40[core][0] =
					PDOFFSET(pi, rstr_pdoffset20in40m2g, core);
				pwrdet->pdoffset20in40[core][1] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gb0, core);
				pwrdet->pdoffset20in40[core][2] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gb1, core);
				pwrdet->pdoffset20in40[core][3] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gb2, core);
				pwrdet->pdoffset20in40[core][4] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gb3, core);
				pwrdet->pdoffset20in40[core][5] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gb4, core);
				pwrdet->pdoffset20in80[core][1] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gb0, core);
				pwrdet->pdoffset20in80[core][2] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gb1, core);
				pwrdet->pdoffset20in80[core][3] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gb2, core);
				pwrdet->pdoffset20in80[core][4] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gb3, core);
				pwrdet->pdoffset20in80[core][5] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gb4, core);
				pwrdet->pdoffset40in80[core][1] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gb0, core);
				pwrdet->pdoffset40in80[core][2] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gb1, core);
				pwrdet->pdoffset40in80[core][3] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gb2, core);
				pwrdet->pdoffset40in80[core][4] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gb3, core);
				pwrdet->pdoffset40in80[core][5] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gb4, core);
			} else {
				pwrdet->pdoffsetcck[core] =
					PDOFFSET(pi, rstr_pdoffset20in40m2gcore3, 1);
				pwrdet->pdoffsetcck20m[core] =
					PDOFFSET(pi, rstr_pdoffset20in40m2gcore3, 2);
				pwrdet->pdoffset20in40[core][0] =
					PDOFFSET(pi, rstr_pdoffset20in40m2gcore3, 0);
				pwrdet->pdoffset20in40[core][1] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gcore3, 0);
				pwrdet->pdoffset20in40[core][2] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gcore3, 1);
				pwrdet->pdoffset20in40[core][3] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gcore3, 2);
				pwrdet->pdoffset20in40[core][4] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gcore3_1, 0);
				pwrdet->pdoffset20in40[core][5] =
					PDOFFSET(pi, rstr_pdoffset20in40m5gcore3_1, 1);
				pwrdet->pdoffset20in80[core][1] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gcore3, 0);
				pwrdet->pdoffset20in80[core][2] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gcore3, 1);
				pwrdet->pdoffset20in80[core][3] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gcore3, 2);
				pwrdet->pdoffset20in80[core][4] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gcore3_1, 0);
				pwrdet->pdoffset20in80[core][5] =
					PDOFFSET(pi, rstr_pdoffset20in80m5gcore3_1, 1);
				pwrdet->pdoffset40in80[core][1] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gcore3, 0);
				pwrdet->pdoffset40in80[core][2] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gcore3, 1);
				pwrdet->pdoffset40in80[core][3] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gcore3, 2);
				pwrdet->pdoffset40in80[core][4] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gcore3_1, 0);
				pwrdet->pdoffset40in80[core][5] =
					PDOFFSET(pi, rstr_pdoffset40in80m5gcore3_1, 1);
			}
	    }

	    pi->phy_tempsense_offset = (int8)PHY_GETINTVAR_SLICE(pi, rstr_tempoffset);
	    if (pi->phy_tempsense_offset < 0) {
		pi->phy_tempsense_offset = 0;
	    } else if (pi->phy_tempsense_offset != 0) {
		if (pi->phy_tempsense_offset >
		    (ACPHY_SROM_TEMPSHIFT + ACPHY_SROM_MAXTEMPOFFSET)) {
		    pi->phy_tempsense_offset = ACPHY_SROM_MAXTEMPOFFSET;
		} else if (pi->phy_tempsense_offset < (ACPHY_SROM_TEMPSHIFT +
		                                       ACPHY_SROM_MINTEMPOFFSET)) {
		    pi->phy_tempsense_offset = ACPHY_SROM_MINTEMPOFFSET;
		} else {
		    pi->phy_tempsense_offset -= ACPHY_SROM_TEMPSHIFT;
		}
	    }

	    /* For ACPHY, if the SROM contains a bogus value, then tempdelta
	     * will default to ACPHY_DEFAULT_CAL_TEMPDELTA. If the SROM contains
	     * a valid value, then the default will be overwritten with this value
	     */
	    wlc_phy_read_tempdelta_settings(pi, ACPHY_CAL_MAXTEMPDELTA);
	}
	return TRUE;
}

#if defined(BCMINTERNAL) || defined(WLTEST)
void
wlc_phy_iovar_patrim_acphy(phy_info_t *pi, int32 *ret_int_ptr)
{
	if ((ACMAJORREV_2(pi->pubpi->phy_rev) || (ACMAJORREV_4(pi->pubpi->phy_rev))) &&
		BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) {
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			if (CHSPEC_IS20(pi->radio_chanspec)) {
				*ret_int_ptr = 0x0;
			}
			else {
				*ret_int_ptr = 0x3;
			}
		}
		else {
			if (ACMAJORREV_4(pi->pubpi->phy_rev))
				*ret_int_ptr = 0x14;
			else
				*ret_int_ptr = 0x0;
		}
	} else if (ACMAJORREV_1(pi->pubpi->phy_rev) && BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) {
		if (CHSPEC_IS5G(pi->radio_chanspec)) {
			*ret_int_ptr = 0x21;
		}
		else
			*ret_int_ptr = 0x14;
	}
	else
		*ret_int_ptr = 0x0;

}

void
wlc_phy_txpwr_ovrinitbaseidx(phy_info_t *pi)
{
	uint8 core, ovrval;
	phy_tpc_data_t *data = pi->tpci->data;
	ovrval = CHSPEC_IS2G(pi->radio_chanspec) ?
		data->cfg.initbaseidx2govrval : data->cfg.initbaseidx5govrval;
	if (ovrval != 255) {
		FOREACH_CORE(pi, core) {
			data->base_index_init[core] = ovrval;
			wlc_phy_txpwrctrl_set_baseindex(pi, core,
			    data->base_index_init[core], 1);
		}
	}
}
#endif /* defined (BCMINTERNAL) || defined(WLTEST) */

void
wlc_phy_get_paparams_for_band_acphy(phy_info_t *pi, int16 *a1, int16 *b0, int16 *b1)
{

	srom11_pwrdet_t *pwrdet = pi->pwrdet_ac;
	uint8 chan_freq_range, core, core_freq_segment_map;


	FOREACH_CORE(pi, core) {
		/* core_freq_segment_map is only required for 80P80 mode.
		For other modes, it is ignored
		*/
		core_freq_segment_map = pi->u.pi_acphy->core_freq_mapping[core];

		/* Get pwrdet params from SROM for current subband */
		chan_freq_range = phy_ac_chanmgr_get_chan_freq_range(pi, 0, core_freq_segment_map);

		switch (chan_freq_range) {
		case WL_CHAN_FREQ_RANGE_2G:
		case WL_CHAN_FREQ_RANGE_5G_BAND0:
		case WL_CHAN_FREQ_RANGE_5G_BAND1:
		case WL_CHAN_FREQ_RANGE_5G_BAND2:
		case WL_CHAN_FREQ_RANGE_5G_BAND3:
			a1[core] =  (int16)pwrdet->pwrdet_a1[core][chan_freq_range];
			b0[core] =  (int16)pwrdet->pwrdet_b0[core][chan_freq_range];
			b1[core] =  (int16)pwrdet->pwrdet_b1[core][chan_freq_range];
			PHY_TXPWR(("wl%d: %s: pwrdet core%d: a1=%d b0=%d b1=%d\n",
				pi->sh->unit, __FUNCTION__, core,
				a1[core], b0[core], b1[core]));
			break;
		}
	}
}

#if defined(PREASSOC_PWRCTRL)
static void
phy_ac_tpc_shortwindow_upd(phy_type_tpc_ctx_t *ctx, bool new_channel)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	uint shmaddr;
	uint32 txallfrm_cnt, txallfrm_diff;

	if ((!pi->sh->up))
		return;

	shmaddr = MACSTAT_ADDR(pi, MCSTOFF_TXFRAME);
	/* default is long term */
	pi->tpci->data->channel_short_window = FALSE;

	phy_utils_phyreg_enter(pi);
	if (new_channel) {
		wlc_phy_pwrctrl_shortwindow_upd_acphy(pi, TRUE);
		pi->tpci->data->channel_short_window = TRUE;
		pi->tpci->data->txallfrm_cnt_ref = wlapi_bmac_read_shm(pi->sh->physhim, shmaddr);
	} else {
		txallfrm_cnt = wlapi_bmac_read_shm(pi->sh->physhim, shmaddr);
		if (pi->tpci->data->txallfrm_cnt_ref > txallfrm_cnt) {
			pi->tpci->data->txallfrm_cnt_ref = 0;
		} else {
			txallfrm_diff = txallfrm_cnt - pi->tpci->data->txallfrm_cnt_ref;
			if (txallfrm_diff > NUM_FRAME_BEFORE_PWRCTRL_CHANGE) {
				wlc_phy_pwrctrl_shortwindow_upd_acphy(pi, FALSE);
				pi->tpci->data->channel_short_window = FALSE;

			} else {
				wlc_phy_pwrctrl_shortwindow_upd_acphy(pi, TRUE);
				pi->tpci->data->channel_short_window = TRUE;
			}
		}
	}
	phy_utils_phyreg_exit(pi);
}
#endif /* PREASSOC_PWRCTRL */

#ifdef WLC_TXCAL
uint8
wlc_phy_set_olpc_anchor_acphy(phy_info_t *pi)
{
	/* Search over the linked txcal table list */
	/* to find out the anchor power tx idx */
	txcal_pwr_tssi_lut_t *LUT_pt;
	txcal_pwr_tssi_lut_t *LUT_root;
	uint8 chan_num = CHSPEC_CHANNEL(pi->radio_chanspec);
	uint8 flag_chan_found = 0;
	uint8 core;
	txcal_root_pwr_tssi_t *pi_txcal_root_pwr_tssi_tbl = pi->txcali->txcal_root_pwr_tssi_tbl;
	int8 *olpc_anchor;

	if (CHSPEC_IS2G(pi->radio_chanspec)) {
		LUT_root = pi_txcal_root_pwr_tssi_tbl->root_pwr_tssi_lut_2G;
		olpc_anchor = &pi->olpci->olpc_anchor2g;
	} else {
		olpc_anchor = &pi->olpci->olpc_anchor5g;
		if (CHSPEC_IS80(pi->radio_chanspec))
			LUT_root = pi_txcal_root_pwr_tssi_tbl->root_pwr_tssi_lut_5G80;
		else if (CHSPEC_IS40(pi->radio_chanspec))
			LUT_root = pi_txcal_root_pwr_tssi_tbl->root_pwr_tssi_lut_5G40;
		else
			LUT_root = pi_txcal_root_pwr_tssi_tbl->root_pwr_tssi_lut_5G20;
	}
	if (LUT_root->txcal_pwr_tssi->channel == 0) {
		if (CHSPEC_BW_LE20(pi->radio_chanspec)) {
			/* No Txcal table is present, return */
			/* olpc_anchor_idx is a check in phy level */
			/* to verify whether the tx idx at the anchor */
			/* power is valid for current channel */
			pi->olpci->olpc_idx_valid = FALSE;
			return BCME_OK;
		} else {
			/* For 40 and 80 if no Txcal table is present */
			/* Use 20mhz txcal table */
			LUT_root = pi_txcal_root_pwr_tssi_tbl->root_pwr_tssi_lut_5G20;
			if (LUT_root->txcal_pwr_tssi->channel == 0) {
				pi->olpci->olpc_idx_valid = FALSE;
				return BCME_OK;
			}
		}
	}
	LUT_pt = LUT_root;
	while (LUT_pt->next_chan != 0) {
		/* Go over all the entries in the list */
		if (LUT_pt->txcal_pwr_tssi->channel == chan_num) {
			flag_chan_found = 1;
			break;
		}
		if ((LUT_pt->txcal_pwr_tssi->channel < chan_num) &&
		    (LUT_pt->next_chan->txcal_pwr_tssi->channel > chan_num)) {
			flag_chan_found = 2;
			break;
		}
		LUT_pt = LUT_pt->next_chan;
	}
	if (LUT_pt->txcal_pwr_tssi->channel == chan_num) {
		/* In case only one entry is in the list */
		flag_chan_found = 1;
	}
	switch (flag_chan_found) {
	case 0:
		/* Channel not found in linked list or not between two channels */
		/* Then pick the closest one */
		if (chan_num < LUT_root->txcal_pwr_tssi->channel)
			LUT_pt = LUT_root;
		break;
	case 2:
		/* Channel is in between two channels, pick closest one as the anchor idx */
		if (ABS(chan_num - LUT_pt->txcal_pwr_tssi->channel) >=
		        ABS(LUT_pt->next_chan->txcal_pwr_tssi->channel - chan_num))
			LUT_pt = LUT_pt->next_chan;
		break;
	}
	FOREACH_CORE(pi, core) {
		pi->olpci->olpc_anchor_idx[core] = LUT_pt->txcal_pwr_tssi->pwr_start_idx[core];
		/* if anchor idx is 0, then decide it is not valid */
		if (pi->olpci->olpc_anchor_idx[core] == 0) {
			pi->olpci->olpc_idx_valid = FALSE;
			return BCME_OK;
		}
		/* temperature recorded for tx idx at the anchor power */
		pi->olpci->olpc_tempsense[core] = LUT_pt->txcal_pwr_tssi->tempsense[core];
	}
	pi->olpci->olpc_idx_valid = TRUE;
	/* If olpc idx is valid from LUT assign pwr_start (Ptssi) to olpc_thresh and anchor */
	pi->olpci->olpc_thresh = LUT_pt->txcal_pwr_tssi->pwr_start[0] >> 1;
	*olpc_anchor = pi->olpci->olpc_thresh;

	return BCME_OK;
}

uint8
wlc_phy_estpwrlut_intpol_acphy(phy_info_t *pi, uint8 channel,
       wl_txcal_power_tssi_t *pwr_tssi_lut_ch1, wl_txcal_power_tssi_t *pwr_tssi_lut_ch2)
{
	uint16 estpwr1[128];
	uint16 estpwr2[128];
	uint16 estpwr[128];
	int16 est_pwr_calc, est_pwr_calc1, est_pwr_calc2, est_pwr_intpol1, est_pwr_intpol2;
	uint32 tbl_len = 128;
	uint32 tbl_offset = 0;
	uint8 core, i;
	uint8 tx_pwr_ctrl_state;
	tx_pwr_ctrl_state =  pi->txpwrctrl;
	wlc_phy_txpwrctrl_enable_acphy(pi, PHY_TPC_HW_OFF);
	/* Interpolate between estpwrlut */
	FOREACH_ACTV_CORE(pi, pi->sh->phyrxchain, core) {
		wlc_phy_txcal_generate_estpwr_lut(pwr_tssi_lut_ch1, estpwr1, core);
		wlc_phy_txcal_generate_estpwr_lut(pwr_tssi_lut_ch2, estpwr2, core);
		for (i = 0; i < 128; i++) {
			est_pwr_calc1 = estpwr1[i] > 0x7F ?
				(int16) (estpwr1[i] - 0x100) : estpwr1[i];
			est_pwr_calc2 = estpwr2[i] > 0x7F ?
				(int16) (estpwr2[i] - 0x100) : estpwr2[i];
			/* round to the nearest integer */
			est_pwr_intpol1 = 2*(channel - pwr_tssi_lut_ch1->channel)*(est_pwr_calc2 -
				est_pwr_calc1)/(pwr_tssi_lut_ch2->channel -
				pwr_tssi_lut_ch1->channel);
			est_pwr_intpol2 = (channel - pwr_tssi_lut_ch1->channel)*(est_pwr_calc2 -
			        est_pwr_calc1)/(pwr_tssi_lut_ch2->channel -
				pwr_tssi_lut_ch1->channel);
			est_pwr_calc = est_pwr_calc1 + est_pwr_intpol1 - est_pwr_intpol2;
			/* Program the upper 8-bits for CCK for 43012 */
			if (ACMAJORREV_36(pi->pubpi->phy_rev))
				estpwr[i] = (uint16) (((est_pwr_calc & 0x00FF) << 8) |
						(est_pwr_calc & 0x00FF));
			else
				estpwr[i] = (uint16)(est_pwr_calc & 0xFF);
		}
		wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_ESTPWRLUTS(core), tbl_len,
			tbl_offset, 16, estpwr);
	}
	wlc_phy_txpwrctrl_enable_acphy(pi, tx_pwr_ctrl_state);
	return BCME_OK;
}
uint8
wlc_phy_olpc_idx_tempsense_comp_acphy(phy_info_t *pi, uint8 *iidx, uint8 core)
{
	/* This function calculates the init idx based on max tgt pwr, */
	/* table based txcal anchor power tx idx, and temperature */
	uint8 olpc_anchor = 0;
	int16 currtemp = 0;
	int16 olpc_tempslope = 0;
	int16 idx = 0;
	int8 chan_freq_range, offset_idx = 0;
	if (CHSPEC_IS5G(pi->radio_chanspec)) {
		olpc_anchor = pi->olpci->olpc_anchor5g;
		olpc_tempslope = pi->olpci->olpc_tempslope5g[core];
	} else {
		olpc_anchor = pi->olpci->olpc_anchor2g;
		olpc_tempslope = pi->olpci->olpc_tempslope2g[core];
	}
	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		/* Gain table in steps of 0.25dBm */
		idx = pi->olpci->olpc_anchor_idx[core] - (pi->tx_power_max_per_core[core]
			- olpc_anchor + 1);
	} else {
		idx = pi->olpci->olpc_anchor_idx[core] - ((pi->tx_power_max_per_core[core]
		- olpc_anchor + 1) >> 1);
	}
	if (olpc_tempslope) {
		currtemp = wlc_phy_tempsense_acphy(pi);
		idx = idx - (((currtemp - pi->olpci->olpc_tempsense[core]) * olpc_tempslope +
			512) >> 10);
	}

	chan_freq_range = phy_ac_chanmgr_get_chan_freq_range(pi, pi->radio_chanspec, core);

	switch (chan_freq_range) {
		case WL_CHAN_FREQ_RANGE_2G:
			offset_idx = pi->olpci->olpc_offset[0];
			break;

		case WL_CHAN_FREQ_RANGE_5G_BAND0:
			offset_idx = pi->olpci->olpc_offset[1];
			break;

		case WL_CHAN_FREQ_RANGE_5G_BAND1:
			offset_idx = pi->olpci->olpc_offset[2];
			break;

		case WL_CHAN_FREQ_RANGE_5G_BAND2:
			offset_idx = pi->olpci->olpc_offset[3];
			break;

		case WL_CHAN_FREQ_RANGE_5G_BAND3:
			offset_idx = pi->olpci->olpc_offset[4];
			break;

		default:
			offset_idx = 0;
	}
	idx = idx + offset_idx;
	if (idx < 0) {
		idx = 0;
	} else if (idx > 127) {
		idx = 127;
	}

	*iidx = (uint8) idx;

	return BCME_OK;
}

uint8
wlc_phy_txcal_olpc_idx_recal_acphy(phy_info_t *pi, bool compute_idx)
{
	uint8 core;

	FOREACH_ACTV_CORE(pi, pi->sh->hw_phyrxchain, core) {
		if (compute_idx) {
			wlc_phy_olpc_idx_tempsense_comp_acphy(pi,
				&pi->tpci->data->base_index_init[core], core);
			if (TINY_RADIO(pi)) {
				wlc_phy_olpc_idx_tempsense_comp_acphy(pi,
					&pi->tpci->data->base_index_cck_init[core], core);
			}
		}
		wlc_phy_txpwrctrl_set_baseindex(pi, core,
			pi->tpci->data->base_index_init[core], 1);
		if (TINY_RADIO(pi)) {
			wlc_phy_txpwrctrl_set_baseindex(pi, core,
				pi->tpci->data->base_index_cck_init[core], 0);
		}
	}

	return BCME_OK;
}
#endif /* WLC_TXCAL */

#if (defined(WLOLPC) && !defined(WLOLPC_DISABLED)) || defined(BCMDBG) || \
	defined(WLTEST)
void
chanspec_clr_olpc_dbg_mode(phy_ac_tpc_info_t *info)
{
	info->olpc_dbg_mode = FALSE;
}
#endif /* ((WLOLPC) && !(WLOLPC_DISABLED)) || (BCMDBG) || (WLTEST) */

#if defined(PHYCAL_CACHING)
static int8 wlc_phy_get_thresh_acphy(phy_info_t *pi)
{
#if (defined(WLOLPC) && !defined(WLOLPC_DISABLED))
	acphy_calcache_t *cache;
	ch_calcache_t *ctx = wlc_phy_get_chanctx(pi, pi->radio_chanspec);
#if defined(BCMDBG) || defined(WLTEST)
	phy_ac_tpc_info_t *tpci = pi->u.pi_acphy->tpci;
	if (tpci->olpc_dbg_mode && tpci->olpc_dbg_mode_caldone) {
		/* If dbg mode, ignore caching and channel context */
		return wlc_phy_olpcthresh();
	} else
#endif /* BCMDBG || WLTEST */
	{
		if (ctx) {
			if (ctx->valid) {
				cache = &ctx->u.acphy_cache;
				if (cache->olpc_caldone)
					return wlc_phy_olpcthresh();
			}
		}
	}
#endif /* WLOLPC && !WLOLPC_DISABLED */
	return wlc_phy_tssivisible_thresh_acphy(pi);
}

#if (defined(WLOLPC) && !defined(WLOLPC_DISABLED))
static int8
wlc_phy_olpcthresh()
{
	/* Threshold = -128 */
	/* When OLPC in use, allow rates with negative power */
	int8 olpc_thresh = WL_RATE_DISABLED;
	return olpc_thresh;
}
#endif /* WLOLPC && !WLOLPC_DISABLED */
#if defined(WLOLPC)
static void
phy_ac_tpc_update_olpc_cal(phy_type_tpc_ctx_t *tpc_ctx, bool set, bool dbg)
{
	phy_ac_tpc_info_t *tpci = (phy_ac_tpc_info_t *)tpc_ctx;
	phy_info_t *pi = tpci->pi;
	ch_calcache_t *ctx = wlc_phy_get_chanctx(pi, pi->radio_chanspec);
	acphy_calcache_t *cache;

	PHY_TRACE(("%s\n", __FUNCTION__));

#if defined(BCMDBG) || defined(WLTEST)
	if (dbg) {
		/* If dbg mode, ignore caching and channel context */
		tpci->olpc_dbg_mode = dbg;
		if (set) {
			tpci->olpc_dbg_mode_caldone = set;
			wlapi_suspend_mac_and_wait(pi->sh->physhim);
			/* Toggle Power Control to save off base index */
			wlc_phy_txpwrctrl_enable_acphy(pi, 0);
			wlc_phy_txpwrctrl_enable_acphy(pi, 1);
			wlc_phy_set_tssisens_lim_acphy(pi, TRUE);
			wlapi_enable_mac(pi->sh->physhim);
		}
	} else
#endif /* BCMDBG || WLTEST */
	{
		if (ctx && set) {
			/* do the following only if
			 * caching exist and also cal is done
			 */
			cache = &ctx->u.acphy_cache;
			cache->olpc_caldone = set;
			wlc_phy_set_tssisens_lim_acphy(pi, tpci->ti->data->txpwroverride);
			wlapi_suspend_mac_and_wait(pi->sh->physhim);
			/* Toggle Power Control to save off base index */
			wlc_phy_txpwrctrl_enable_acphy(pi, 0);
			wlc_phy_txpwrctrl_enable_acphy(pi, 1);
			wlapi_enable_mac(pi->sh->physhim);
		}
	}
}
#endif /* WLOLPC */
#endif /* PHYCAL_CACHING */

#ifdef FCC_PWR_LIMIT_2G
void
wlc_phy_fcc_pwr_limit_set_acphy(phy_type_tpc_ctx_t *tpc_ctx, bool enable)
{
	phy_ac_tpc_info_t *info = (phy_ac_tpc_info_t *)tpc_ctx;
	phy_info_t *pi = info->pi;
	uint target_ch = CHSPEC_CHANNEL(pi->radio_chanspec);
	int8 reflimit[3];
	int8 limit[3];
	uint core;
#ifdef WL_SAR_SIMPLE_CONTROL
	bool restore_sarlimit = TRUE;
#endif /* WL_SAR_SIMPLE_CONTROL */

	if (!pi->tpci->data->cfg.srom_tworangetssi2g) {
		return;
	}

	ASSERT(pi->sh->clk);

	limit[0] = limit[1] = limit[2] = WLC_TXPWR_MAX;

	for (core = 0; core < 3; core++) {
#ifdef WL_SAR_SIMPLE_CONTROL
		reflimit[core] = pi->tpci->data->sarlimit[core];
#else
		reflimit[core] = WLC_TXPWR_MAX;
#endif /* WL_SAR_SIMPLE_CONTROL */
	}

	if (enable) {
		if (target_ch == 12 && pi->tpci->data->cfg.fccpwrch12 > 0) {
			for (core = 0; core < PHY_CORE_MAX; core++) {
				limit[core] = MIN(reflimit[core], pi->tpci->data->cfg.fccpwrch12);
			}
#ifdef WL_SAR_SIMPLE_CONTROL
			restore_sarlimit = FALSE;
#endif /* WL_SAR_SIMPLE_CONTROL */
		} else if (target_ch == 13 && pi->tpci->data->cfg.fccpwrch13 > 0) {
			for (core = 0; core < PHY_CORE_MAX; core++) {
				limit[core] = MIN(reflimit[core], pi->tpci->data->cfg.fccpwrch13);
			}
#ifdef WL_SAR_SIMPLE_CONTROL
			restore_sarlimit = FALSE;
#endif /* WL_SAR_SIMPLE_CONTROL */
		}
	}

#ifdef WL_SAR_SIMPLE_CONTROL
	if (restore_sarlimit) {
		wlc_phy_dynamic_sarctrl_set((wlc_phy_t *)pi,
			wlc_phy_isenabled_dynamic_sarctrl(pi));
		return;
	}
#endif /* WL_SAR_SIMPLE_CONTROL */

	wlapi_suspend_mac_and_wait(pi->sh->physhim);
	phy_utils_phyreg_enter(pi);

	IF_ACTV_CORE(pi, pi->sh->phyrxchain, 0) {
		MOD_PHYREG(pi, TxPwrCapping_path0,
			maxTxPwrCap_path0, limit[0]);
	}
	IF_ACTV_CORE(pi, pi->sh->phyrxchain, 1) {
		MOD_PHYREG(pi, TxPwrCapping_path1,
			maxTxPwrCap_path1, limit[1]);
	}
	IF_ACTV_CORE(pi, pi->sh->phyrxchain, 2) {
		MOD_PHYREG(pi, TxPwrCapping_path2,
			maxTxPwrCap_path2, limit[2]);
	}

	phy_utils_phyreg_exit(pi);
	wlapi_enable_mac(pi->sh->physhim);
}
#endif /* FCC_PWR_LIMIT_2G */

uint8
wlc_phy_ac_set_tssi_params_maj36(phy_info_t *pi)
{

	ACPHY_REG_LIST_START

		MOD_PHYREG_ENTRY(pi, TxPwrCtrlDamping, DeltaPwrDamp, 16)
		MOD_PHYREG_ENTRY(pi, TxPwrCtrlNnum, Ntssi_delay,
			75)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, Ntssi_accum_delay, 150)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrlcck, Ntssi_accum_delay_cck,
			75)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, tssi_accum_en, 1)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, tssi_filter_pos, 1)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, Ntssi_intg_log2, 4)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrlcck, Ntssi_intg_log2_cck, 3)
		MOD_PHYREG_ENTRY(pi, perPktIdleTssiCtrl,
			perPktIdleTssiUpdate_en, 0)
		MOD_PHYREG_ENTRY(pi, TSSIMode, TwoPwrRange, 0)
		MOD_PHYREG_ENTRY(pi, TxPwrCtrl_Multi_Mode0, multi_mode, 0x3)
		MOD_PHYREG_ENTRY(pi, TxPwrCtrlCmd, bbMultInt_en, 0x0)

	ACPHY_REG_LIST_EXECUTE(pi)

	return SAMPLE_TSSI_AFTER_185_SAMPLES;
}

uint8
wlc_phy_ac_set_tssi_params_majrev40(phy_info_t *pi)
{

	ACPHY_REG_LIST_START

		MOD_PHYREG_ENTRY(pi, TxPwrCtrlNnum, Ntssi_delay, 200)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, tssi_accum_en, 0)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, tssi_filter_pos, 0)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrl, Ntssi_intg_log2, 2)
		MOD_PHYREG_ENTRY(pi, TssiAccumCtrlcck, Ntssi_intg_log2_cck, 3)
		MOD_PHYREG_ENTRY(pi, perPktIdleTssiCtrl,
			perPktIdleTssiUpdate_en, 0)
		MOD_PHYREG_ENTRY(pi, TSSIMode, TwoPwrRange, 0)
	/*	MOD_PHYREG_ENTRY(pi, TxPwrCtrl_Multi_Mode0, multi_mode, 0x3) */
		MOD_PHYREG_ENTRY(pi, TxPwrCtrlCmd, bbMultInt_en, 0x1)

	ACPHY_REG_LIST_EXECUTE(pi)

	return SAMPLE_TSSI_AFTER_200_SAMPLES;
}

int8
wlc_phy_calc_ppr_pwr_cap_acphy(phy_info_t *pi, uint8 core, int8 maxpwr)
{
	/* Store MIN(MAX_rates(tgtpwr), sarlimit, txpwrcap) in pi structure */
	phy_info_acphy_t *pi_ac = pi->u.pi_acphy;

	if (core >= PHY_CORE_MAX) {
		PHY_ERROR(("%s: Invalid PHY core[0x%02x], setting cap to %d,"
			" hw_phytxchain[0x%02x] hw_phyrxchain[0x%02x]\n",
			__FUNCTION__, core, 127, pi->sh->hw_phytxchain, pi->sh->hw_phyrxchain));
		return 127;
	}

#ifdef WL_SARLIMIT
	pi->tpci->data->adjusted_pwr_cap[core] = MIN(pi->tpci->data->sarlimit[core],
		maxpwr + pi_ac->tpci->txpwr_offset[core]);
#else
	pi->tpci->data->adjusted_pwr_cap[core] = maxpwr + pi_ac->tpci->txpwr_offset[core];
#endif

#ifdef WLC_TXPWRCAP
	if (PHYTXPWRCAP_ENAB(pi)) {
		pi->tpci->data->adjusted_pwr_cap[core] =
			MIN(wlc_phy_txpwrcap_tbl_get_max_percore_acphy(pi, core),
			pi->tpci->data->adjusted_pwr_cap[core]);
	}
#endif /* WLC_TXPWRCAP */
	return pi->tpci->data->adjusted_pwr_cap[core];
}

uint16
wlc_phy_set_txpwr_by_index_acphy(phy_info_t *pi, uint8 core_mask, int8 txpwrindex)
{
	uint8 core;
	uint16 lpfgain;
	txgain_setting_t txgain_settings;
	uint8 stall_val = READ_PHYREGFLD(pi, RxFeCtrl1, disable_stalls);

	PHY_TRACE(("wl%d: %s\n", pi->sh->unit, __FUNCTION__));

	ASSERT(core_mask);
	ASSERT(txpwrindex >= 0);	/* negative index not supported */

	ACPHY_DISABLE_STALL(pi);

	/* Set tx power based on an input "index"
	 * (Emulate what HW power control would use for a given table index)
	 */
	FOREACH_ACTV_CORE(pi, core_mask, core) {
		/* Read tx gain table */
		wlc_phy_get_txgain_settings_by_index_acphy(pi, &txgain_settings, txpwrindex);
		PHY_TXPWR(("wl%d: %s: Setting Tx radio_gain to %02x%04x%04x and bbmult to %d "
		           "for core %d\n", pi->sh->unit, __FUNCTION__,
		           txgain_settings.rad_gain_hi, txgain_settings.rad_gain_mi,
		           txgain_settings.rad_gain, txgain_settings.bbmult, core));

		lpfgain = ((txgain_settings.rad_gain & 0x70) >> 4);
		lpfgain = lpfgain | (lpfgain <<3) | (lpfgain <<6);
		/* Override gains: DAC, Radio and BBmult */
		if (core == 3) {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
				0x501, 16, &(txgain_settings.rad_gain));
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
				0x504, 16, &(txgain_settings.rad_gain_mi));
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
				0x507, 16, &(txgain_settings.rad_gain_hi));
			if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
				// Tx BQ2 gain is in a 3-bit field at different location
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
					0x51e, 16, &lpfgain);
			}
		} else {
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
			    (0x100 + core), 16, &(txgain_settings.rad_gain));
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
			    (0x103 + core), 16, &(txgain_settings.rad_gain_mi));
			wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
			    (0x106 + core), 16, &(txgain_settings.rad_gain_hi));
			if (ACMAJORREV_37(pi->pubpi->phy_rev) ||
			    ACMAJORREV_40(pi->pubpi->phy_rev)) {
				// Tx BQ2 gain is in a 3-bit field at different location
				wlc_phy_table_write_acphy(pi, ACPHY_TBL_ID_RFSEQ, 1,
				    (0x17e + 16*core), 16, &lpfgain);
			}
		}
		wlc_phy_set_tx_bbmult_acphy(pi, &txgain_settings.bbmult, core);

		PHY_TXPWR(("wl%d: %s: Fixed txpwrindex for core%d is %d\n",
		          pi->sh->unit, __FUNCTION__, core, txpwrindex));
	}
	ACPHY_ENABLE_STALL(pi, stall_val);

	return txgain_settings.bbmult;
}

#if (defined(BCMINTERNAL) || defined(WLTEST))
static int
phy_ac_tpc_set_pavars(phy_type_tpc_ctx_t *ctx, void *a, void *p)
{
	phy_ac_tpc_info_t *tpci = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = tpci->pi;
	uint16 inpa[WL_PHY_PAVARS_LEN];
	uint j = 3; /* PA parameters start from offset 3 */
	int chain, freq_range, num_paparams = PHY_CORE_MAX;
#ifdef WL_CHAN_FREQ_RANGE_5G_4BAND
	int n;
#endif

	bcopy(p, inpa, sizeof(inpa));

	chain = inpa[2];
	freq_range = inpa[1];

	if ((BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) &&
		(ACMAJORREV_1(pi->pubpi->phy_rev))) {
		num_paparams = 3;
	} else if ((ACMAJORREV_2(pi->pubpi->phy_rev) ||
		ACMAJORREV_4(pi->pubpi->phy_rev)) &&
		BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) {
		num_paparams = 4;
	} else if (tpci->ti->data->cfg.srom_tworangetssi2g &&
	 (inpa[1] == WL_CHAN_FREQ_RANGE_2G) && pi->ipa2g_on &&
		(ACMAJORREV_1(pi->pubpi->phy_rev))) {
		num_paparams = 2;
	} else if (tpci->ti->data->cfg.srom_tworangetssi5g &&
	 (inpa[1] != WL_CHAN_FREQ_RANGE_2G) && pi->ipa5g_on &&
		(ACMAJORREV_1(pi->pubpi->phy_rev))) {
		num_paparams = 2;
	}

	if (inpa[0] != PHY_TYPE_AC) {
		PHY_ERROR(("Wrong phy type %d\n", inpa[0]));
		return BCME_BADARG;
	}

	if (chain > (num_paparams - 1)) {
		PHY_ERROR(("Wrong chain number %d\n", chain));
		return BCME_BADARG;
	}

	if (SROMREV(pi->sh->sromrev) >= 12) {
		srom12_pwrdet_t *pwrdet = pi->pwrdet_ac;
		switch (freq_range) {
		case WL_CHAN_FREQ_RANGE_2G:
		pwrdet->pwrdet_a[chain][freq_range] = inpa[j++];
		pwrdet->pwrdet_b[chain][freq_range] = inpa[j++];
		pwrdet->pwrdet_c[chain][freq_range] = inpa[j++];
		pwrdet->pwrdet_d[chain][freq_range] = inpa[j++];
		break;
		case WL_CHAN_FREQ_RANGE_2G_40:
		pwrdet->pwrdet_a_40[chain][freq_range-6] = inpa[j++];
		pwrdet->pwrdet_b_40[chain][freq_range-6] = inpa[j++];
		pwrdet->pwrdet_c_40[chain][freq_range-6] = inpa[j++];
		pwrdet->pwrdet_d_40[chain][freq_range-6] = inpa[j++];
		break;
		/* allow compile in branches without 4BAND definition */
#ifdef WL_CHAN_FREQ_RANGE_5G_4BAND
		case WL_CHAN_FREQ_RANGE_5G_BAND4:
		pwrdet->pwrdet_a[chain][freq_range] = inpa[j++];
		pwrdet->pwrdet_b[chain][freq_range] = inpa[j++];
		pwrdet->pwrdet_c[chain][freq_range] = inpa[j++];
		pwrdet->pwrdet_d[chain][freq_range] = inpa[j++];
		break;

		case WL_CHAN_FREQ_RANGE_5G_BAND0:
		case WL_CHAN_FREQ_RANGE_5G_BAND1:
		case WL_CHAN_FREQ_RANGE_5G_BAND2:
		case WL_CHAN_FREQ_RANGE_5G_BAND3:
		if (ACMAJORREV_2(pi->pubpi->phy_rev) ||
			ACMAJORREV_5(pi->pubpi->phy_rev)) {
			pwrdet->pwrdet_a[chain][freq_range] = inpa[j++];
			pwrdet->pwrdet_b[chain][freq_range] = inpa[j++];
			pwrdet->pwrdet_c[chain][freq_range] = inpa[j++];
			pwrdet->pwrdet_d[chain][freq_range] = inpa[j++];
		} else {
			PHY_ERROR(("bandrange %d is out of scope\n", inpa[1]));
			return BCME_OUTOFRANGECHAN;
		}
		break;
		case WL_CHAN_FREQ_RANGE_5G_BAND0_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND1_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND2_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND3_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND4_40:
		pwrdet->pwrdet_a_40[chain][freq_range-6] = inpa[j++];
		pwrdet->pwrdet_b_40[chain][freq_range-6] = inpa[j++];
		pwrdet->pwrdet_c_40[chain][freq_range-6] = inpa[j++];
		pwrdet->pwrdet_d_40[chain][freq_range-6] = inpa[j++];
		break;
		case WL_CHAN_FREQ_RANGE_5G_BAND0_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND1_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND2_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND3_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND4_80:
		pwrdet->pwrdet_a_80[chain][freq_range-12] = inpa[j++];
		pwrdet->pwrdet_b_80[chain][freq_range-12] = inpa[j++];
		pwrdet->pwrdet_c_80[chain][freq_range-12] = inpa[j++];
		pwrdet->pwrdet_d_80[chain][freq_range-12] = inpa[j++];
		break;
		case WL_CHAN_FREQ_RANGE_5G_5BAND:
		for (n = 1; n <= 5; n++) {
			pwrdet->pwrdet_a[chain][n] = inpa[j++];
			pwrdet->pwrdet_b[chain][n] = inpa[j++];
			pwrdet->pwrdet_c[chain][n] = inpa[j++];
			pwrdet->pwrdet_d[chain][n] = inpa[j++];
		}
		break;
		case WL_CHAN_FREQ_RANGE_5G_5BAND_40:
		for (n = 1; n <= 5; n++) {
			pwrdet->pwrdet_a_40[chain][n] = inpa[j++];
			pwrdet->pwrdet_b_40[chain][n] = inpa[j++];
			pwrdet->pwrdet_c_40[chain][n] = inpa[j++];
			pwrdet->pwrdet_d_40[chain][n] = inpa[j++];
		}
		break;
		case WL_CHAN_FREQ_RANGE_5G_5BAND_80:
		for (n = 0; n <= 4; n++) {
			pwrdet->pwrdet_a_80[chain][n] = inpa[j++];
			pwrdet->pwrdet_b_80[chain][n] = inpa[j++];
			pwrdet->pwrdet_c_80[chain][n] = inpa[j++];
			pwrdet->pwrdet_d_80[chain][n] = inpa[j++];
		}
		break;
#endif /* WL_CHAN_FREQ_RANGE_5G_4BAND */
		default:
		PHY_ERROR(("bandrange %d is out of scope\n", inpa[1]));
		return BCME_OUTOFRANGECHAN;
		}
	} else {
		srom11_pwrdet_t *pwrdet11 = pi->pwrdet_ac;
		switch (freq_range) {
		case WL_CHAN_FREQ_RANGE_2G:
		pwrdet11->pwrdet_a1[chain][freq_range] = inpa[j++];
		pwrdet11->pwrdet_b0[chain][freq_range] = inpa[j++];
		pwrdet11->pwrdet_b1[chain][freq_range] = inpa[j++];
		break;
		/* allow compile in branches without 4BAND definition */
#ifdef WL_CHAN_FREQ_RANGE_5G_4BAND
		case WL_CHAN_FREQ_RANGE_5G_4BAND:
		for (n = 1; n <= 4; n ++) {
			pwrdet11->pwrdet_a1[chain][n] = inpa[j++];
			pwrdet11->pwrdet_b0[chain][n] = inpa[j++];
			pwrdet11->pwrdet_b1[chain][n] = inpa[j++];
		}
		break;

		case WL_CHAN_FREQ_RANGE_5G_BAND0:
		case WL_CHAN_FREQ_RANGE_5G_BAND1:
		case WL_CHAN_FREQ_RANGE_5G_BAND2:
		case WL_CHAN_FREQ_RANGE_5G_BAND3:
		if (ACMAJORREV_2(pi->pubpi->phy_rev) ||
			ACMAJORREV_5(pi->pubpi->phy_rev)) {
			pwrdet11->pwrdet_a1[chain][freq_range] = inpa[j++];
			pwrdet11->pwrdet_b0[chain][freq_range] = inpa[j++];
			pwrdet11->pwrdet_b1[chain][freq_range] = inpa[j++];
		} else {
			PHY_ERROR(("bandrange %d is out of scope\n", inpa[1]));
			return BCME_OUTOFRANGECHAN;
		}
		break;
#endif /* WL_CHAN_FREQ_RANGE_5G_4BAND */
		default:
		PHY_ERROR(("bandrange %d is out of scope\n", inpa[1]));
		return BCME_OUTOFRANGECHAN;
		}
	}

	return BCME_OK;
}

static int
phy_ac_tpc_get_pavars(phy_type_tpc_ctx_t *ctx, void *a, void *p)
{
	phy_ac_tpc_info_t *tpci = (phy_ac_tpc_info_t *)ctx;
	phy_info_t *pi = tpci->pi;
	uint16 *outpa = a;
	uint16 inpa[WL_PHY_PAVARS_LEN];
	uint j = 3; /* PA parameters start from offset 3 */
	int chain, freq_range, num_paparams = PHY_CORE_MAX;
#ifdef WL_CHAN_FREQ_RANGE_5G_4BAND
	int n;
#endif

	bcopy(p, inpa, sizeof(inpa));

	outpa[0] = inpa[0]; /* Phy type */
	outpa[1] = inpa[1]; /* Band range */
	outpa[2] = inpa[2]; /* Chain */

	chain = inpa[2];
	freq_range = inpa[1];

	if ((BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) &&
		(ACMAJORREV_1(pi->pubpi->phy_rev))) {
		num_paparams = 3;
	} else if ((ACMAJORREV_2(pi->pubpi->phy_rev) ||
		(ACMAJORREV_4(pi->pubpi->phy_rev))) &&
		BF3_TSSI_DIV_WAR(pi->u.pi_acphy)) {
		num_paparams = 4;
	} else if (tpci->ti->data->cfg.srom_tworangetssi2g &&
	 (inpa[1] == WL_CHAN_FREQ_RANGE_2G) && pi->ipa2g_on &&
		(ACMAJORREV_1(pi->pubpi->phy_rev))) {
		num_paparams = 2;
	} else if (tpci->ti->data->cfg.srom_tworangetssi5g &&
	 (inpa[1] != WL_CHAN_FREQ_RANGE_2G) && pi->ipa5g_on &&
		(ACMAJORREV_1(pi->pubpi->phy_rev))) {
		num_paparams = 2;
	}
	if (inpa[0] != PHY_TYPE_AC) {
		PHY_ERROR(("Wrong phy type %d\n", inpa[0]));
		outpa[0] = PHY_TYPE_NULL;
		return BCME_BADARG;
	}
	if (chain > (num_paparams - 1)) {
		PHY_ERROR(("Wrong chain number %d\n", chain));
		outpa[0] = PHY_TYPE_NULL;
		return BCME_BADARG;
	}

	if (SROMREV(pi->sh->sromrev) >= 12) {
		srom12_pwrdet_t *pwrdet = pi->pwrdet_ac;
		switch (freq_range) {
		case WL_CHAN_FREQ_RANGE_2G:
		outpa[j++] = pwrdet->pwrdet_a[chain][freq_range];
		outpa[j++] = pwrdet->pwrdet_b[chain][freq_range];
		outpa[j++] = pwrdet->pwrdet_c[chain][freq_range];
		outpa[j++] = pwrdet->pwrdet_d[chain][freq_range];
		break;
		case WL_CHAN_FREQ_RANGE_2G_40:
		outpa[j++] = pwrdet->pwrdet_a_40[chain][freq_range-6];
		outpa[j++] = pwrdet->pwrdet_b_40[chain][freq_range-6];
		outpa[j++] = pwrdet->pwrdet_c_40[chain][freq_range-6];
		outpa[j++] = pwrdet->pwrdet_d_40[chain][freq_range-6];
		break;
		/* allow compile in branches without 4BAND definition */
#ifdef WL_CHAN_FREQ_RANGE_5G_4BAND
		case WL_CHAN_FREQ_RANGE_5G_BAND4:
		outpa[j++] = pwrdet->pwrdet_a[chain][freq_range];
		outpa[j++] = pwrdet->pwrdet_b[chain][freq_range];
		outpa[j++] = pwrdet->pwrdet_c[chain][freq_range];
		outpa[j++] = pwrdet->pwrdet_d[chain][freq_range];
		break;
		case WL_CHAN_FREQ_RANGE_5G_BAND0:
		case WL_CHAN_FREQ_RANGE_5G_BAND1:
		case WL_CHAN_FREQ_RANGE_5G_BAND2:
		case WL_CHAN_FREQ_RANGE_5G_BAND3:
		if (ACMAJORREV_2(pi->pubpi->phy_rev) ||
			ACMAJORREV_5(pi->pubpi->phy_rev)) {
			outpa[j++] = pwrdet->pwrdet_a[chain][freq_range];
			outpa[j++] = pwrdet->pwrdet_b[chain][freq_range];
			outpa[j++] = pwrdet->pwrdet_c[chain][freq_range];
			outpa[j++] = pwrdet->pwrdet_d[chain][freq_range];
		} else {
			PHY_ERROR(("bandrange %d is out of scope\n", inpa[1]));
			return BCME_OUTOFRANGECHAN;
		}
		break;
		case WL_CHAN_FREQ_RANGE_5G_BAND0_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND1_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND2_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND3_40:
		case WL_CHAN_FREQ_RANGE_5G_BAND4_40:
		outpa[j++] = pwrdet->pwrdet_a_40[chain][freq_range-6];
		outpa[j++] = pwrdet->pwrdet_b_40[chain][freq_range-6];
		outpa[j++] = pwrdet->pwrdet_c_40[chain][freq_range-6];
		outpa[j++] = pwrdet->pwrdet_d_40[chain][freq_range-6];
		break;
		case WL_CHAN_FREQ_RANGE_5G_BAND0_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND1_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND2_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND3_80:
		case WL_CHAN_FREQ_RANGE_5G_BAND4_80:
		outpa[j++] = pwrdet->pwrdet_a_80[chain][freq_range-12];
		outpa[j++] = pwrdet->pwrdet_b_80[chain][freq_range-12];
		outpa[j++] = pwrdet->pwrdet_c_80[chain][freq_range-12];
		outpa[j++] = pwrdet->pwrdet_d_80[chain][freq_range-12];
		break;
		case WL_CHAN_FREQ_RANGE_5G_5BAND:
		for (n = 1; n <= 5; n++) {
			outpa[j++] = pwrdet->pwrdet_a[chain][n];
			outpa[j++] = pwrdet->pwrdet_b[chain][n];
			outpa[j++] = pwrdet->pwrdet_c[chain][n];
			outpa[j++] = pwrdet->pwrdet_d[chain][n];
		}
		break;
		case WL_CHAN_FREQ_RANGE_5G_5BAND_40:
		for (n = 1; n <= 5; n++) {
			outpa[j++] = pwrdet->pwrdet_a_40[chain][n];
			outpa[j++] = pwrdet->pwrdet_b_40[chain][n];
			outpa[j++] = pwrdet->pwrdet_c_40[chain][n];
			outpa[j++] = pwrdet->pwrdet_d_40[chain][n];
		}
		break;
		case WL_CHAN_FREQ_RANGE_5G_5BAND_80:
		for (n = 0; n <= 4; n++) {
			outpa[j++] = pwrdet->pwrdet_a_80[chain][n];
			outpa[j++] = pwrdet->pwrdet_b_80[chain][n];
			outpa[j++] = pwrdet->pwrdet_c_80[chain][n];
			outpa[j++] = pwrdet->pwrdet_d_80[chain][n];
		}
		break;
#endif /* WL_CHAN_FREQ_RANGE_5G_4BAND */
		default:
		PHY_ERROR(("bandrange %d is out of scope\n", inpa[1]));
		return BCME_OUTOFRANGECHAN;
		}
	} else {
		srom11_pwrdet_t *pwrdet11 = pi->pwrdet_ac;
		switch (freq_range) {
		case WL_CHAN_FREQ_RANGE_2G:
		outpa[j++] = pwrdet11->pwrdet_a1[chain][freq_range];
		outpa[j++] = pwrdet11->pwrdet_b0[chain][freq_range];
		outpa[j++] = pwrdet11->pwrdet_b1[chain][freq_range];
		break;
		/* allow compile in branches without 4BAND definition */
#ifdef WL_CHAN_FREQ_RANGE_5G_4BAND
		case WL_CHAN_FREQ_RANGE_5G_4BAND:
		for (n = 1; n <= 4; n ++) {
			outpa[j++] = pwrdet11->pwrdet_a1[chain][n];
			outpa[j++] = pwrdet11->pwrdet_b0[chain][n];
			outpa[j++] = pwrdet11->pwrdet_b1[chain][n];
		}
		break;
		case WL_CHAN_FREQ_RANGE_5G_BAND0:
		case WL_CHAN_FREQ_RANGE_5G_BAND1:
		case WL_CHAN_FREQ_RANGE_5G_BAND2:
		case WL_CHAN_FREQ_RANGE_5G_BAND3:
		if (ACMAJORREV_2(pi->pubpi->phy_rev) ||
			ACMAJORREV_5(pi->pubpi->phy_rev)) {
			outpa[j++] = pwrdet11->pwrdet_a1[chain][freq_range];
			outpa[j++] = pwrdet11->pwrdet_b0[chain][freq_range];
			outpa[j++] = pwrdet11->pwrdet_b1[chain][freq_range];
		} else {
			PHY_ERROR(("bandrange %d is out of scope\n", inpa[1]));
			return BCME_OUTOFRANGECHAN;
		}
		break;
		default:
		PHY_ERROR(("bandrange %d is out of scope\n", inpa[1]));
		return BCME_OUTOFRANGECHAN;
		break;
		}
#endif /* WL_CHAN_FREQ_RANGE_5G_4BAND */
	}

	return BCME_OK;
}
#endif /* defined(BCMINTERNAL) || defined(WLTEST) */