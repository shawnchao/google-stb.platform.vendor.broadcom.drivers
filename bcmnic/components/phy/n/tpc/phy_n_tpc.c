/*
 * NPHY TxPowerCtrl module implementation
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
 * $Id: phy_n_tpc.c 642720 2016-06-09 18:56:12Z vyass $
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <phy_dbg.h>
#include <phy_mem.h>
#include "phy_type_tpc.h"
#include <phy_n.h>
#include <phy_n_tpc.h>
#include <phy_tpc_api.h>
#include <phy_tpc.h>
#include <wlc_phyreg_n.h>
#include <phy_utils_reg.h>

#ifndef ALL_NEW_PHY_MOD
/* < TODO: all these are going away... */
#include <wlc_phy_int.h>
/* TODO: all these are going away... > */
#endif

/* module private states */
struct phy_n_tpc_info {
	phy_info_t *pi;
	phy_n_info_t *ni;
	phy_tpc_info_t *ti;
};

/* local functions */
static void phy_n_tpc_recalc_tgt(phy_type_tpc_ctx_t *ctx);
static void wlc_phy_txpower_recalc_target_n_big(phy_type_tpc_ctx_t *ctx, ppr_t *tx_pwr_target,
    ppr_t *srom_max_txpwr, ppr_t *reg_txpwr_limit, ppr_t *txpwr_targets);
static void wlc_phy_txpower_sromlimit_get_nphy(phy_type_tpc_ctx_t *ctx, chanspec_t chanspec,
    ppr_t *max_pwr, uint8 core);
static void phy_n_tpc_reg_limit_calc(phy_type_tpc_ctx_t *ctx, ppr_t *txpwr,
    ppr_t *txpwr_limit, ppr_ht_mcs_rateset_t *mcs_limits);
static bool phy_n_tpc_hw_ctrl_get(phy_type_tpc_ctx_t *ctx);
static void phy_n_tpc_set(phy_type_tpc_ctx_t *ctx, ppr_t *reg_pwr);
static void phy_n_tpc_set_flags(phy_type_tpc_ctx_t *ctx, phy_tx_power_t *power);
static void phy_n_tpc_set_max(phy_type_tpc_ctx_t *ctx, phy_tx_power_t *power);
#if (defined(BCMINTERNAL) || defined(WLTEST))
static int phy_n_tpc_set_pavars(phy_type_tpc_ctx_t *ctx, void* a, void* p);
static int phy_n_tpc_get_pavars(phy_type_tpc_ctx_t *ctx, void* a, void* p);
#endif /* defined(BCMINTERNAL) || defined(WLTEST) */

/* Register/unregister NPHY specific implementation to common layer */
phy_n_tpc_info_t *
BCMATTACHFN(phy_n_tpc_register_impl)(phy_info_t *pi, phy_n_info_t *ni, phy_tpc_info_t *ti)
{
	phy_n_tpc_info_t *info;
	phy_type_tpc_fns_t fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* allocate all storage in once */
	if ((info = phy_malloc(pi, sizeof(phy_n_tpc_info_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc failed\n", __FUNCTION__));
		goto fail;
	}
	bzero(info, sizeof(phy_n_tpc_info_t));
	info->pi = pi;
	info->ni = ni;
	info->ti = ti;

	/* Register PHY type specific implementation */
	bzero(&fns, sizeof(fns));
	fns.recalc = phy_n_tpc_recalc_tgt;
	fns.recalc_target = wlc_phy_txpower_recalc_target_n_big;
	fns.get_sromlimit = wlc_phy_txpower_sromlimit_get_nphy;
	fns.reglimit_calc = phy_n_tpc_reg_limit_calc;
	fns.get_hwctrl = phy_n_tpc_hw_ctrl_get;
	fns.set = phy_n_tpc_set;
	fns.setflags = phy_n_tpc_set_flags;
	fns.setmax = phy_n_tpc_set_max;
#if (defined(BCMINTERNAL) || defined(WLTEST))
	fns.set_pavars = phy_n_tpc_set_pavars;
	fns.get_pavars = phy_n_tpc_get_pavars;
#endif /* defined(BCMINTERNAL) || defined(WLTEST) */
	fns.ctx = info;

	phy_tpc_register_impl(ti, &fns);

	return info;

fail:
	if (info != NULL)
		phy_mfree(pi, info, sizeof(phy_n_tpc_info_t));
	return NULL;
}

void
BCMATTACHFN(phy_n_tpc_unregister_impl)(phy_n_tpc_info_t *info)
{
	phy_info_t *pi = info->pi;
	phy_tpc_info_t *ti = info->ti;

	PHY_TRACE(("%s\n", __FUNCTION__));

	phy_tpc_unregister_impl(ti);

	phy_mfree(pi, info, sizeof(phy_n_tpc_info_t));
}

/* recalc target txpwr and apply to h/w */
static void
phy_n_tpc_recalc_tgt(phy_type_tpc_ctx_t *ctx)
{
	phy_n_tpc_info_t *info = (phy_n_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;

	PHY_TRACE(("%s\n", __FUNCTION__));

	wlc_phy_txpower_recalc_target_nphy(pi);
}

/* TODO: The code could be optimized by moving the common code to phy/cmn */
/* [PHY_RE_ARCH] There are two functions: Bigger function wlc_phy_txpower_recalc_target
 * and smaller function phy_tpc_recalc_tgt which in turn call their phy specific functions
 * which are named in a haphazard manner. This needs to be cleaned up.
 */
static void
wlc_phy_txpower_recalc_target_n_big(phy_type_tpc_ctx_t *ctx, ppr_t *tx_pwr_target,
    ppr_t *srom_max_txpwr, ppr_t *reg_txpwr_limit, ppr_t *txpwr_targets)
{
	phy_n_tpc_info_t *info = (phy_n_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	int8 tx_pwr_max = 0;
	int8 tx_pwr_min = 255;
	uint8 mintxpwr = 0;
	uint8 core;
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

			/* Adjust board limits based on environmental conditions */
			if (CHIPID_4324X_EPA_FAMILY(pi)) {
				wlc_nphy_apply_cond_chg(pi, srom_max_txpwr);
			}

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
			if (pi->tpci->data->txpwr_percent < 100) {
				ppr_multiply_percentage(tx_pwr_target,
					pi->tpci->data->txpwr_percent);
			}
			/* Common Code End */

			/* Enforce min power and save result as power target. */
			ppr_apply_min(tx_pwr_target, mintxpwr);
		}

		tx_pwr_max = ppr_get_max(tx_pwr_target);

		if (tx_pwr_max < (pi->min_txpower * WLC_TXPWR_DB_FACTOR)) {
			tx_pwr_max = pi->min_txpower * WLC_TXPWR_DB_FACTOR;
		}
		tx_pwr_min = ppr_get_min(tx_pwr_target, WL_RATE_DISABLED);

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

		if (CHIPID_4324X_EPA_FAMILY(pi)) {
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
}

#ifndef WLC_DISABLE_SROM8
static void
wlc_phy_ppr_set_dsss_srom8(ppr_t* tx_srom_max_pwr, uint8 bwtype,
          ppr_dsss_rateset_t* pwr_offsets)
{
	uint8 chain;
	for (chain = WL_TX_CHAINS_1; chain <= WL_TX_CHAINS_2; chain++)
		/* for 2g_dsss_20IN20: S1x1, S1x2 */
		ppr_set_dsss(tx_srom_max_pwr, bwtype, chain,
			(const ppr_dsss_rateset_t*)pwr_offsets);
}

static void
wlc_phy_ppr_set_ofdm_srom8(ppr_t* tx_srom_max_pwr, uint8 bwtype, wl_tx_mode_t mode,
          wl_tx_chains_t tx_chain, ppr_ofdm_rateset_t* pwr_offsets)
{
	ppr_set_ofdm(tx_srom_max_pwr, bwtype, mode, tx_chain,
		(const ppr_ofdm_rateset_t*)pwr_offsets);

}

static void
wlc_phy_ppr_set_mcs_srom8(ppr_t* tx_srom_max_pwr, uint8 bwtype, wl_tx_nss_t Nss,
	wl_tx_mode_t mode, wl_tx_chains_t tx_chains, ppr_ht_mcs_rateset_t* pwr_offsets)
{
	ppr_set_ht_mcs(tx_srom_max_pwr, bwtype, Nss, mode,
		tx_chains, (const ppr_ht_mcs_rateset_t*)pwr_offsets);

}

static void
wlc_phy_txpwr_apply_srom8(phy_info_t *pi, uint8 band,
	uint8 tmp_max_pwr, ppr_t *tx_srom_max_pwr)
{

	chanspec_t chanspec = pi->radio_chanspec;
	uint8 tmp_bw40po = 0, tmp_cddpo = 0, tmp_stbcpo = 0;
	uint32 tmp_mcs_word = 0;

	ppr_dsss_rateset_t cck20_offset;
	ppr_ofdm_rateset_t ofdm20_offset_siso, ofdm20_offset_cdd,
		ofdm40_offset_siso, ofdm40_offset_cdd;

	ppr_ht_mcs_rateset_t mcs20_offset_siso, mcs20_offset_cdd, mcs20_offset_stbc,
		mcs20_offset_sdm, mcs40_offset_siso, mcs40_offset_cdd, mcs40_offset_stbc,
		mcs40_offset_sdm;

	BCM_REFERENCE(chanspec);

	if (band == 0) {	/* 2G case */

		/* ----------------2G--------------------- */
		/* 2G - CCK */
		wlc_phy_txpwr_srom_convert_cck(pi->ppr->u.sr8.cck2gpo,
		        tmp_max_pwr,  &cck20_offset);

		if (CHSPEC_BW_LE20(chanspec)) {

			/* for 2g_dsss_20IN20: S1x1, S1x2 */
			wlc_phy_ppr_set_dsss_srom8(tx_srom_max_pwr, WL_TX_BW_20, &cck20_offset);
		}
		if (CHSPEC_IS40(chanspec)) {

			/* for 2g_dsss_20IN40: S1x1, S1x2 */
			wlc_phy_ppr_set_dsss_srom8(tx_srom_max_pwr, WL_TX_BW_20IN40, &cck20_offset);
		}

	}

	if (CHSPEC_BW_LE20(chanspec)) {

		/* 2G - OFDM_20 */
		wlc_phy_txpwr_srom_convert_ofdm(pi->ppr->u.sr8.ofdm[band], tmp_max_pwr,
			&ofdm20_offset_siso);

		/* for ofdm_20IN20: S1x1   */
		wlc_phy_ppr_set_ofdm_srom8(tx_srom_max_pwr, WL_TX_BW_20, WL_TX_MODE_NONE,
			WL_TX_CHAINS_1, &ofdm20_offset_siso);


		/* for 20MHz Mapping Legacy OFDM SISo to MCS0-7 SISO */
		wlc_phy_copy_ofdm_to_mcs_powers(&ofdm20_offset_siso, &mcs20_offset_siso);

		/* for mcs_20IN20 SISO: S1x1  */
		wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_20, WL_TX_NSS_1,
			WL_TX_MODE_NONE, 1, &mcs20_offset_siso);


		/* 2G - MCS_CDD  */
		/* Apply power-offset specified by the cddpo SROM field to rates sent
		 * in the CDD STF mode
		 */
		tmp_cddpo = pi->ppr->u.sr8.cdd[band];

		tmp_mcs_word = (pi->ppr->u.sr8.mcs[band][1] << 16)|(pi->ppr->u.sr8.mcs[band][0]);
		wlc_phy_txpwr_srom_convert_mcs_offset(tmp_mcs_word, tmp_cddpo, tmp_max_pwr,
			&mcs20_offset_cdd, 0);

		if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
			/* for mcs_20IN20 CDD: S1x1, S1x2  */
			wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_20, WL_TX_NSS_1,
				WL_TX_MODE_CDD, WL_TX_CHAINS_2, &mcs20_offset_cdd);
		}

		/* for 20MHz Mapping MCS0-7 CDD to Legacy OFDM CDD  */
		wlc_phy_copy_mcs_to_ofdm_powers(&mcs20_offset_cdd, &ofdm20_offset_cdd);

		if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
			/* for ofdm_20IN20 CDD:  S1x2  */
			wlc_phy_ppr_set_ofdm_srom8(tx_srom_max_pwr, WL_TX_BW_20, WL_TX_MODE_CDD,
				WL_TX_CHAINS_2, &ofdm20_offset_cdd);
		}

		/* STBC 20 MHz */
		/* Apply power-offset specified by the cddpo SROM field to rates sent
		 * in the CDD STF mode
		 */
		tmp_stbcpo = pi->ppr->u.sr8.stbc[band];

		tmp_mcs_word = (pi->ppr->u.sr8.mcs[band][1] << 16)|(pi->ppr->u.sr8.mcs[band][0]);
		wlc_phy_txpwr_srom_convert_mcs_offset(tmp_mcs_word, tmp_stbcpo, tmp_max_pwr,
			&mcs20_offset_stbc, 0);

		if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
			/* for mcs_20IN20 STBC: S2x2  */
			wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_20, WL_TX_NSS_2,
				WL_TX_MODE_STBC, WL_TX_CHAINS_2, &mcs20_offset_stbc);
		}

		/* SDM 20 MHz */
		tmp_mcs_word = (pi->ppr->u.sr8.mcs[band][3] << 16)|(pi->ppr->u.sr8.mcs[band][2]);
		wlc_phy_txpwr_srom_convert_mcs_offset(tmp_mcs_word, 0, tmp_max_pwr,
			&mcs20_offset_sdm, 0);

		if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
			/* for mcs_20IN20 SDM: S2x2  */
			wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_20, WL_TX_NSS_2,
				WL_TX_MODE_NONE, WL_TX_CHAINS_2, &mcs20_offset_sdm);
		}
	}

	/* 40 MHz PPRs */
	/* For nphy_rev>=5, re-interpret the mcs[2g,5g,5gl,5gh]po4-7 SROM fields to be the
	 * power-offsets for 40 MHz mcs0-15 w.r.t the max power. For nphy_rev<5, 40 MHz
	 * mcs0-15, use the same power offsets as for 20 MHz mcs0-15.
	 * The bw402gpo field is further used to implement an additional uniform power
	 * back-off for all 40 MHz OFDM rates.
	 */
	if (CHSPEC_IS40(chanspec)) {
		int8 mcs7_15_offset = 0;

		/* Hack for LCNXNPHY  rev 0 */
		/* 2 channels are looking 2 dB off with respect to evm and SM performance */
		/* Dropping the srom powers only for those 2 channels */
		if (NREV_IS(pi->pubpi->phy_rev, LCNXN_BASEREV)) {
			uint channel = CHSPEC_CHANNEL(pi->radio_chanspec);
			if (channel == 151)  {
				mcs7_15_offset = 4;
			}
		}

		/* MCS 40 SISO */
		tmp_bw40po = pi->ppr->u.sr8.bw40[band];

		tmp_mcs_word = (pi->ppr->u.sr8.mcs[band][5] << 16)|(pi->ppr->u.sr8.mcs[band][4]);
		wlc_phy_txpwr_srom_convert_mcs_offset(tmp_mcs_word, tmp_bw40po, tmp_max_pwr,
			&mcs40_offset_siso, mcs7_15_offset);

		/* for mcs_40 SISO: S1x1  */
		wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_40, WL_TX_NSS_1,
			WL_TX_MODE_NONE, WL_TX_CHAINS_1, &mcs40_offset_siso);

		/* for mcs_20in40 SISO: S1x1  */
		wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_20IN40, WL_TX_NSS_1,
			WL_TX_MODE_NONE, WL_TX_CHAINS_1, &mcs40_offset_siso);

		/* for 40MHz Mapping MCS0-7 SISO to Legacy OFDM SISO	*/
		wlc_phy_copy_mcs_to_ofdm_powers(&mcs40_offset_siso, &ofdm40_offset_siso);

		/* for ofdm_40 SISO: S1x1, */
		wlc_phy_ppr_set_ofdm_srom8(tx_srom_max_pwr, WL_TX_BW_40, WL_TX_MODE_NONE,
			WL_TX_CHAINS_1, &ofdm40_offset_siso);

		/* for ofdm_20IN40 SISO: S1x1, */
		wlc_phy_ppr_set_ofdm_srom8(tx_srom_max_pwr, WL_TX_BW_20IN40,
			WL_TX_MODE_NONE, WL_TX_CHAINS_1, &ofdm40_offset_siso);

		/* MCS 40 CDD */
		tmp_cddpo = pi->ppr->u.sr8.cdd[band];

		tmp_mcs_word = (pi->ppr->u.sr8.mcs[band][5] << 16)|(pi->ppr->u.sr8.mcs[band][4]);
		wlc_phy_txpwr_srom_convert_mcs_offset(tmp_mcs_word,
			(tmp_bw40po + tmp_cddpo), tmp_max_pwr, &mcs40_offset_cdd, mcs7_15_offset);

		if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
			/* for mcs_40 CDD: S1x2  */
			wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_40, WL_TX_NSS_1,
				WL_TX_MODE_CDD, WL_TX_CHAINS_2, &mcs40_offset_cdd);

			/* for mcs_20in40 CDD: S1x2  */
			wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_20IN40,
				WL_TX_NSS_1, WL_TX_MODE_CDD, WL_TX_CHAINS_2, &mcs40_offset_cdd);
		}
		/* for 40MHz Mapping MCS0-7 CDD to Legacy OFDM CDD	*/
		wlc_phy_copy_mcs_to_ofdm_powers(&mcs40_offset_cdd, &ofdm40_offset_cdd);

		if (PHYCORENUM(pi->pubpi->phy_corenum) > 1) {
			/* for ofdm_40 CDD: S1x2, */
			wlc_phy_ppr_set_ofdm_srom8(tx_srom_max_pwr, WL_TX_BW_40,
				WL_TX_MODE_CDD,	WL_TX_CHAINS_2, &ofdm40_offset_cdd);

			/* for ofdm_20IN40 CDD: S1x2, */
			wlc_phy_ppr_set_ofdm_srom8(tx_srom_max_pwr, WL_TX_BW_20IN40,
				WL_TX_MODE_CDD, WL_TX_CHAINS_2, &ofdm40_offset_cdd);

			/* MCS 40 STBC */
			tmp_stbcpo = pi->ppr->u.sr8.stbc[band];

			tmp_mcs_word = (pi->ppr->u.sr8.mcs[band][5] << 16) |
				(pi->ppr->u.sr8.mcs[band][4]);
			wlc_phy_txpwr_srom_convert_mcs_offset(tmp_mcs_word,
				(tmp_bw40po + tmp_stbcpo), tmp_max_pwr, &mcs40_offset_stbc,
				mcs7_15_offset);

			/* for mcs_40 STBC: S2x2  */
			wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_40, WL_TX_NSS_2,
				WL_TX_MODE_STBC, WL_TX_CHAINS_2, &mcs40_offset_stbc);

			/* for mcs_20in40 STBC: S2x2  */
			wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_20IN40,
				WL_TX_NSS_2, WL_TX_MODE_STBC, WL_TX_CHAINS_2, &mcs40_offset_stbc);

			/* MCS 40 SDM */
			tmp_mcs_word = (pi->ppr->u.sr8.mcs[band][7] << 16) |
				(pi->ppr->u.sr8.mcs[band][6]);
			wlc_phy_txpwr_srom_convert_mcs_offset(tmp_mcs_word, tmp_bw40po,
				tmp_max_pwr, &mcs40_offset_sdm, mcs7_15_offset);

			/* for mcs_40 SDM: S2x2  */
			wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_40, WL_TX_NSS_2,
				WL_TX_MODE_NONE, WL_TX_CHAINS_2, &mcs40_offset_sdm);

			/* for mcs_20in40 SDM: S2x2  */
			wlc_phy_ppr_set_mcs_srom8(tx_srom_max_pwr, WL_TX_BW_20IN40,
				WL_TX_NSS_2, WL_TX_MODE_NONE, WL_TX_CHAINS_2, &mcs40_offset_sdm);
		}
	}
}
#endif /* Compiling out sromrev 8 code for 4324 */

static void
wlc_phy_txpower_sromlimit_get_nphy(phy_type_tpc_ctx_t *ctx, chanspec_t chanspec, ppr_t *max_pwr,
    uint8 core)
{
	phy_n_tpc_info_t *info = (phy_n_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	srom_pwrdet_t	*pwrdet  = pi->pwrdet;
	uint8 band;
	uint8 tmp_max_pwr = 0;
	int8 deltaPwr;

	uint8 chan = CHSPEC_CHANNEL(chanspec);
	/* to figure out which subband is in 5G */
	/* in the range of 0, 1, 2, 3, 4 */
	band = wlc_phy_get_chan_freq_range_nphy(pi, chan);

	tmp_max_pwr = pwrdet->max_pwr[0][band];
	if (PHYCORENUM(pi->pubpi->phy_corenum) > 1)
		tmp_max_pwr = MIN(tmp_max_pwr, pwrdet->max_pwr[1][band]);

	if (pi->sh->sromrev >= 9) {
		wlc_phy_txpwr_apply_srom9(pi, band, pi->radio_chanspec, tmp_max_pwr, max_pwr);
	} else {
#ifndef WLC_DISABLE_SROM8
		wlc_phy_txpwr_apply_srom8(pi, band, tmp_max_pwr, max_pwr);
#endif /* Compiling out sromrev 8 code for 4324 */
	}

	deltaPwr = pwrdet->max_pwr[core][band] > tmp_max_pwr;
	if (deltaPwr > 0)
		ppr_plus_cmn_val(max_pwr, deltaPwr);

	switch (band) {
	case WL_CHAN_FREQ_RANGE_2G:
	case WL_CHAN_FREQ_RANGE_5G_BAND0:
	case WL_CHAN_FREQ_RANGE_5G_BAND1:
	case WL_CHAN_FREQ_RANGE_5G_BAND2:
		ppr_apply_max(max_pwr, pwrdet->max_pwr[core][band]);
		break;
	default:
		break;
	}

}

static void phy_n_tpc_reg_limit_calc(phy_type_tpc_ctx_t *ctx, ppr_t *txpwr,
    ppr_t *txpwr_limit, ppr_ht_mcs_rateset_t *mcs_limits)
{
	phy_n_tpc_info_t *info = (phy_n_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	uint k, i, j;
	ppr_ofdm_rateset_t ofdm_limits;
	wl_tx_bw_t bw = WL_TX_BW_20;
	wl_tx_mode_t mode = WL_TX_MODE_NONE;
	wl_tx_chains_t chains = WL_TX_CHAINS_1;
	BCM_REFERENCE(pi);
	/* Use min of OFDM and MCS_20_SISO values as the regulatory
	 * limit for SISO Legacy OFDM and MCS0-7 rates. Similarly, for 40 MHz SIS0 Legacy
	 * OFDM  and MCS0-7 rates as well as for 20 MHz and 40 MHz CDD Legacy OFDM and
	 * MCS0-7 rates. This is because the current hardware implementation uses common
	 * powers for the 8 Legacy ofdm and 8 mcs0-7 rates, i.e. they share the same power
	 * table. The power table is populated based on the constellation, coding rate, and
	 * transmission mode (SISO/CDD/STBC/SDM). Therefore, care must be taken to match the
	 * constellation and coding rates of the Legacy OFDM and MCS0-7 rates since the 8
	 * Legacy OFDM rates and the 8 MCS0-7 rates do not have a 1-1 correspondence in
	 * these parameters.
	 */

	/* Regulatory limits for Legacy OFDM rates 20 and 40 MHz, SISO and CDD. The
	 * regulatory limits for the corresponding MCS0-7 20 and 40 MHz, SISO and
	 * CDD rates should also be mapped into Legacy OFDM limits and the minimum
	 * of the two limits should be taken for each rate.
	 */
	/* Regulatory limits for MCS0-7 rates 20 and 40 MHz, SISO and CDD. The
	 * regulatory limits for the corresponding Legacy OFDM 20 and 40 MHz, SISO and
	 * CDD rates should also be mapped into MCS0-7 limits and the minimum
	 * of the two limits should be taken for each rate.
	 */
	if (CHSPEC_IS20(pi->radio_chanspec)) {
		/* 20 MHz */
		j = 2;
		k = 0;
	} else {
		/* 40 MHz and 20IN40 */
		j = 6;
		k = 2;
	}

	for (; k < j; k++) {

		ppr_ofdm_rateset_t ofdm_from_mcs_limits;
		ppr_ht_mcs_rateset_t mcs_from_ofdm_limits;

		switch (k) {
		case 0:
			/* 20 MHz Legacy OFDM SISO */
			bw = WL_TX_BW_20;
			mode = WL_TX_MODE_NONE;
			chains = WL_TX_CHAINS_1;
			break;
		case 1:
			/* 20 MHz Legacy OFDM CDD */
			bw = WL_TX_BW_20;
			mode = WL_TX_MODE_CDD;
			chains = WL_TX_CHAINS_2;
			break;
		case 2:
			/* 40 MHz Legacy OFDM SISO */
			bw = WL_TX_BW_40;
			mode = WL_TX_MODE_NONE;
			chains = WL_TX_CHAINS_1;
			break;
		case 3:
			/* 40 MHz Legacy OFDM CDD */
			bw = WL_TX_BW_40;
			mode = WL_TX_MODE_CDD;
			chains = WL_TX_CHAINS_2;
			break;
		case 4:
			/* case 4: 20in40 MHz Legacy OFDM SISO */
			bw = WL_TX_BW_20IN40;
			mode = WL_TX_MODE_NONE;
			chains = WL_TX_CHAINS_1;
			break;
		case 5:
			/* 20in40 legacy ofdm cdd */
			bw = WL_TX_BW_20IN40;
			mode = WL_TX_MODE_CDD;
			chains = WL_TX_CHAINS_2;
			break;
		}

		ppr_get_ht_mcs(txpwr, bw, WL_TX_NSS_1, mode, chains, mcs_limits);
		ppr_get_ofdm(txpwr, bw, mode, chains, &ofdm_limits);
		wlc_phy_copy_mcs_to_ofdm_powers(mcs_limits, &ofdm_from_mcs_limits);
		wlc_phy_copy_ofdm_to_mcs_powers(&ofdm_limits, &mcs_from_ofdm_limits);

		for (i = 0; i < WL_RATESET_SZ_OFDM; i++) {
			if (ofdm_from_mcs_limits.pwr[i] == WL_RATE_DISABLED)
				continue;
			ofdm_limits.pwr[i] = MIN(ofdm_limits.pwr[i],
				ofdm_from_mcs_limits.pwr[i]);
		}

		for (i = 0; i < WL_RATESET_SZ_HT_MCS; i++) {
			if (mcs_from_ofdm_limits.pwr[i] == WL_RATE_DISABLED)
				continue;
			mcs_limits->pwr[i] = MIN(mcs_limits->pwr[i],
				mcs_from_ofdm_limits.pwr[i]);
		}

		ppr_set_ofdm(txpwr_limit, bw, mode, chains, &ofdm_limits);
		ppr_set_ht_mcs(txpwr_limit, bw, WL_TX_NSS_1, mode, chains, mcs_limits);

	}
}


static bool
phy_n_tpc_hw_ctrl_get(phy_type_tpc_ctx_t *ctx)
{
	phy_n_tpc_info_t *info = (phy_n_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	return pi->nphy_txpwrctrl;
}

static void
phy_n_tpc_set(phy_type_tpc_ctx_t *ctx, ppr_t *reg_pwr)
{
	phy_n_tpc_info_t *info = (phy_n_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	uint8 tx_pwr_ctrl_state = pi->nphy_txpwrctrl;
	wlc_phy_txpwrctrl_enable_nphy(pi, PHY_TPC_HW_OFF);
	wlc_phy_txpower_recalc_target(pi, reg_pwr, NULL);
	wlc_phy_txpwrctrl_enable_nphy(pi, tx_pwr_ctrl_state);
}

static void
phy_n_tpc_set_flags(phy_type_tpc_ctx_t *ctx, phy_tx_power_t *power)
{
	phy_n_tpc_info_t *info = (phy_n_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	power->rf_cores = 2;
	power->flags |= (WL_TX_POWER_F_MIMO);
	if (pi->nphy_txpwrctrl == PHY_TPC_HW_ON)
		power->flags |= (WL_TX_POWER_F_ENABLED | WL_TX_POWER_F_HW);
}

static void
phy_n_tpc_set_max(phy_type_tpc_ctx_t *ctx, phy_tx_power_t *power)
{
	uint8 core;
	phy_n_tpc_info_t *info = (phy_n_tpc_info_t *)ctx;
	phy_info_t *pi = info->pi;
	/* Store the maximum target power among all rates */
	FOREACH_CORE(pi, core) {
		power->tx_power_max[core] = pi->tx_power_max_per_core[core];
#ifdef WL_SARLIMIT
		power->SARLIMIT[core] = WLC_TXPWR_MAX;
#endif
	}
}

#if (defined(BCMINTERNAL) || defined(WLTEST))
static int
phy_n_tpc_set_pavars(phy_type_tpc_ctx_t *ctx, void *a, void *p)
{
	phy_n_tpc_info_t *tpci = (phy_n_tpc_info_t *)ctx;
	uint16 inpa[WL_PHY_PAVARS_LEN];
	uint j = 3; /* PA parameters start from offset 3 */
	srom_pwrdet_t *pwrdet = tpci->pi->pwrdet;

	bcopy(p, inpa, sizeof(inpa));

	if (inpa[0] != PHY_TYPE_N) {
		return BCME_BADARG;
	}

	if (inpa[2] > 1) {
		return BCME_BADARG;
	}

	pwrdet->pwrdet_a1[inpa[2]][inpa[1]] = inpa[j++];
	pwrdet->pwrdet_b0[inpa[2]][inpa[1]] = inpa[j++];
	pwrdet->pwrdet_b1[inpa[2]][inpa[1]] = inpa[j++];

	return BCME_OK;
}

static int
phy_n_tpc_get_pavars(phy_type_tpc_ctx_t *ctx, void *a, void *p)
{
	phy_n_tpc_info_t *tpci = (phy_n_tpc_info_t *)ctx;
	uint16 *outpa = a;
	uint16 inpa[WL_PHY_PAVARS_LEN];
	uint j = 3; /* PA parameters start from offset 3 */
	srom_pwrdet_t *pwrdet = tpci->pi->pwrdet;

	bcopy(p, inpa, sizeof(inpa));

	outpa[0] = inpa[0]; /* Phy type */
	outpa[1] = inpa[1]; /* Band range */
	outpa[2] = inpa[2]; /* Chain */

	if (inpa[0] != PHY_TYPE_N) {
		outpa[0] = PHY_TYPE_NULL;
		return BCME_BADARG;
	}
	outpa[j++] = pwrdet->pwrdet_a1[inpa[2]][inpa[1]];	/* a1 */
	outpa[j++] = pwrdet->pwrdet_b0[inpa[2]][inpa[1]];	/* b0 */
	outpa[j++] = pwrdet->pwrdet_b1[inpa[2]][inpa[1]];	/* b1 */

	return BCME_OK;
}
#endif /* defined(BCMINTERNAL) || defined(WLTEST) */