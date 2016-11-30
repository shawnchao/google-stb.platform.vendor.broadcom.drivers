/*
 * RSSICompute module implementation
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
 * $Id: phy_rssi.c 646150 2016-06-28 17:04:40Z jqliu $
 */

#include <phy_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <phy_dbg.h>
#include <phy_mem.h>
#include <phy_rssi_api.h>
#include "phy_type_rssi.h"
#include "phy_rssi_cfg.h"
#include <phy_rssi.h>
#include <bcmdevs.h>

/* rssi moving average window */
typedef struct {
	uint16  win_sz;
	int8  *rssi0_buffer;
	int8  *rssi1_buffer;
	int8  rssi0_avg;
	int8  rssi1_avg;
	int8  rssi0_index;
	int8  rssi1_index;
/* leave these arrays here at the end */
	int8 rssi0[RSSI_MA_WIN_SZ];
	int8 rssi1[RSSI_MA_WIN_SZ];
} phy_rssi_ma_t;

/* module private states */
struct phy_rssi_info {
	phy_info_t *pi;
	phy_type_rssi_fns_t *fns;
	phy_rssi_ma_t *ma;
	bool do_ma;
};

/* module private states memory layout */
typedef struct {
	phy_rssi_info_t info;
	phy_type_rssi_fns_t fns;
/* could postpone the ma allocation until it is enabled... */
	phy_rssi_ma_t ma;
/* add other variable size variables here at the end */
} phy_rssi_mem_t;

/* local function declaration */
static void phy_rssi_do_ma(phy_rssi_info_t *info, wlc_d11rxhdr_t *wrxh);
#if defined(BCMDBG) || defined(BCMDBG_DUMP)
static int phy_rssi_dump(void *ctx, struct bcmstrbuf *b);
#endif

/* attach/detach */
phy_rssi_info_t *
BCMATTACHFN(phy_rssi_attach)(phy_info_t *pi)
{
	phy_rssi_info_t *info;
	phy_rssi_ma_t *ma;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* allocate attach info storage */
	if ((info = phy_malloc(pi, sizeof(phy_rssi_mem_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc failed\n", __FUNCTION__));
		goto fail;
	}
	info->pi = pi;
	info->fns = &((phy_rssi_mem_t *)info)->fns;

	/* init moving average window */
	ma = &((phy_rssi_mem_t *)info)->ma;
	ma->win_sz = RSSI_MA_WIN_SZ;
	ma->rssi0_buffer = ma->rssi0;
	ma->rssi1_buffer = ma->rssi1;
	info->ma = ma;

#if defined(BCMDBG) || defined(BCMDBG_DUMP)
	/* register dump callback */
	phy_dbg_add_dump_fn(pi, "phyrssi", phy_rssi_dump, info);
#endif

	return info;

	/* error */
fail:
	phy_rssi_detach(info);
	return NULL;
}

void
BCMATTACHFN(phy_rssi_detach)(phy_rssi_info_t *info)
{
	phy_info_t *pi;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (info == NULL) {
		PHY_INFORM(("%s: null rssi module\n", __FUNCTION__));
		return;
	}

	pi = info->pi;

	phy_mfree(pi, info, sizeof(phy_rssi_mem_t));
}

/* register phy type specific implementations */
int
BCMATTACHFN(phy_rssi_register_impl)(phy_rssi_info_t *ri, phy_type_rssi_fns_t *fns)
{
	PHY_TRACE(("%s\n", __FUNCTION__));


	*ri->fns = *fns;
	return BCME_OK;
}

void
BCMATTACHFN(phy_rssi_unregister_impl)(phy_rssi_info_t *ri)
{
	PHY_TRACE(("%s\n", __FUNCTION__));
}

/* enable moving average */
int
BCMATTACHFN(phy_rssi_enable_ma)(phy_rssi_info_t *ri, bool enab)
{
	PHY_TRACE(("%s: enab %d\n", __FUNCTION__, enab));

	ri->do_ma = enab;
	return BCME_OK;
}

/* compute RSSI based on rxh and other info, save result in wrxh */
int8 BCMFASTPATH
phy_rssi_compute_rssi(phy_info_t *pi, wlc_d11rxhdr_t *wrxh)
{
	phy_rssi_info_t *info = pi->rssii;
	phy_type_rssi_fns_t *fns = info->fns;
	d11rxhdr_t *rxh = &wrxh->rxhdr;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (ISSIM_ENAB(pi->sh->sih)) {
		goto end;
	}

	if (D11REV_GE(pi->sh->corerev, 64) && (rxh->lt80.dma_flags & RXS_SHORT_MASK)) {
		/* non-last MPDU in AMSDU. No valid phy status. */
		goto end;
	}

	/* intermediate mpdus in a AMPDU do not have a valid phy status */
	if (!(rxh->lt80.RxStatus2 & RXS_PHYRXST_VALID))
		goto end;

	/* redirect the request to PHY type specific implementation */
	ASSERT(fns->compute != NULL);
	(fns->compute)(fns->ctx, wrxh);

	/* getting moving average of rssi values */
	if (info->do_ma)
		phy_rssi_do_ma(info, wrxh);

	return wrxh->rssi;

	/* abnormal exit path */
end:
	wrxh->rssi = WLC_RSSI_INVALID;
	wrxh->rssi_qdb = 0;
	/* skip calc rssi MA */
	wrxh->do_rssi_ma = 1;

	return WLC_RSSI_INVALID;
}

static void
phy_rssi_do_ma(phy_rssi_info_t *info, wlc_d11rxhdr_t *wrxh)
{
	phy_rssi_ma_t *ma = info->ma;
	int16 sz = (int16)ma->win_sz;
	int16 rssi0_avg, rssi1_avg;
	int16 ctr;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* Compute RSSI for 16 samples per core */
	ma->rssi0_buffer[ma->rssi0_index] = (int8)wrxh->rxpwr[0];
	ma->rssi1_buffer[ma->rssi1_index] = (int8)wrxh->rxpwr[1];
	ma->rssi0_index++;
	ma->rssi1_index++;
	/* average over 16 values/packets */
	ma->rssi1_index %= sz;
	ma->rssi0_index %= sz;
	rssi0_avg = 0;
	rssi1_avg = 0;
	for (ctr = 0; ctr < sz; ctr++) {
		rssi0_avg += ma->rssi0_buffer[ctr];
		rssi1_avg += ma->rssi1_buffer[ctr];
	}
	rssi0_avg /= sz;
	rssi1_avg /= sz;
	ma->rssi0_avg = (int8) rssi0_avg;
	ma->rssi1_avg = (int8) rssi1_avg;
}

/* compare rssi at antenna */
uint8
phy_rssi_compare_ant(phy_rssi_info_t *ri)
{
	phy_rssi_ma_t *ma = ri->ma;
	int8 rssi1, rssi0;
	uint8 chainmap;

	PHY_TRACE(("%s\n", __FUNCTION__));

	rssi1 = ma->rssi1_avg;
	rssi0 = ma->rssi0_avg;

	if (rssi1 >= rssi0) {
		chainmap = 2;
	} else {
		chainmap = 1;
	}

	return chainmap;
}

/* init gain err table */
void
phy_rssi_init_gain_err(phy_rssi_info_t *ri)
{
	phy_type_rssi_fns_t *fns = ri->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->init_gain_err == NULL)
		return;

	(fns->init_gain_err)(fns->ctx);
}

/* This function returns shared_ant info.
 * boardflags2 denote which core is shared [core0/1]
 */
int
wlc_phy_sharedant_acphy(phy_info_t *pi)
{
	if (PHYCORENUM(pi->pubpi->phy_corenum) > 1 &&
		(BOARDFLAGS(GENERIC_PHY_INFO(pi)->boardflags2) & BFL2_BT_SHARE_ANT0) == 0) {
			return PHY_CORE_1;
	}
	return PHY_CORE_0;
}

#if defined(BCMDBG) || defined(BCMDBG_DUMP)
static int
phy_rssi_dump(void *ctx, struct bcmstrbuf *b)
{
	phy_rssi_info_t *info = (phy_rssi_info_t *)ctx;
	phy_type_rssi_fns_t *fns = info->fns;

	if (info->ma != NULL) {
		phy_rssi_ma_t *ma = info->ma;
		uint i;

		bcm_bprintf(b, "average: %u\n", info->do_ma);
		bcm_bprintf(b, "win_sz: %u\n", ma->win_sz);
		bcm_bprintf(b, "rssi0_avg: %d rssi1_avg: %d\n", ma->rssi0_avg, ma->rssi1_avg);
		bcm_bprintf(b, "rssi0_index: %d rssi1_index: %d\n",
		            ma->rssi0_index, ma->rssi1_index);
		bcm_bprintf(b, "rssi0:");
		for (i = 0; i < ARRAYSIZE(ma->rssi0); i ++)
			bcm_bprintf(b, " %d", ma->rssi0[i]);
		bcm_bprintf(b, "\n");
		bcm_bprintf(b, "rssi1:");
		for (i = 0; i < ARRAYSIZE(ma->rssi1); i ++)
			bcm_bprintf(b, " %d", ma->rssi1[i]);
		bcm_bprintf(b, "\n");
	}

	if (fns->dump == NULL)
		return BCME_UNSUPPORTED;

	return (fns->dump)(fns->ctx, b);
}
#endif /* BCMDBG || BCMDBG_DUMP */

#if defined(BCMINTERNAL) || defined(WLTEST)
void
wlc_phy_pkteng_rxstats_update(wlc_phy_t *ppi, uint8 statidx)
{
	phy_info_t *pi = (phy_info_t*)ppi;
	phy_type_rssi_fns_t *fns = pi->rssii->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->update_pkteng_rxstats != NULL) {
		(fns->update_pkteng_rxstats)(fns->ctx, statidx);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
	}
}

int
wlc_phy_pkteng_stats_get(phy_rssi_info_t *rssii, void *a, int alen)
{
	phy_type_rssi_fns_t *fns = rssii->fns;
	phy_info_t *pi = rssii->pi;
	wl_pkteng_stats_t stats;
	uint16 hi, lo;

	if (!pi->sh->up) {
		return BCME_NOTUP;
	}

	PHY_INFORM(("Pkteng Stats Called\n"));

	bzero(&stats, sizeof(stats));

	/* Read with guard against carry */
	do {
		hi = wlapi_bmac_read_shm(pi->sh->physhim, M_MFGTEST_FRMCNT_HI(pi));
		lo = wlapi_bmac_read_shm(pi->sh->physhim, M_MFGTEST_FRMCNT_LO(pi));
	} while (hi != wlapi_bmac_read_shm(pi->sh->physhim, M_MFGTEST_FRMCNT_HI(pi)));

	stats.lostfrmcnt = (hi << 16) | lo;

	if (fns->get_pkteng_stats != NULL) {
		return (fns->get_pkteng_stats)(fns->ctx, a, alen, stats);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
}
#endif /* defined(BCMINTERNAL) || defined(WLTEST) */

int
phy_rssi_set_gain_delta_2g(phy_rssi_info_t *rssii, uint32 aid, int8 *deltaValues)
{
	phy_type_rssi_fns_t *fns = rssii->fns;
	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->set_gain_delta_2g != NULL) {
		return (fns->set_gain_delta_2g)(fns->ctx, aid, deltaValues);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
}

int
phy_rssi_get_gain_delta_2g(phy_rssi_info_t *rssii, uint32 aid, int8 *deltaValues)
{
	phy_type_rssi_fns_t *fns = rssii->fns;
	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->get_gain_delta_2g != NULL) {
		return (fns->get_gain_delta_2g)(fns->ctx, aid, deltaValues);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
}

int
phy_rssi_set_gain_delta_5g(phy_rssi_info_t *rssii, uint32 aid, int8 *deltaValues)
{
	phy_type_rssi_fns_t *fns = rssii->fns;
	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->set_gain_delta_5g != NULL) {
		return (fns->set_gain_delta_5g)(fns->ctx, aid, deltaValues);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
}

int
phy_rssi_get_gain_delta_5g(phy_rssi_info_t *rssii, uint32 aid, int8 *deltaValues)
{
	phy_type_rssi_fns_t *fns = rssii->fns;
	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->get_gain_delta_5g != NULL) {
		return (fns->get_gain_delta_5g)(fns->ctx, aid, deltaValues);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
}

#ifdef WLTEST
int
phy_rssi_set_gain_delta_2gb(phy_rssi_info_t *rssii, uint32 aid, int8 *deltaValues)
{
	phy_type_rssi_fns_t *fns = rssii->fns;
	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->set_gain_delta_2gb != NULL) {
		return (fns->set_gain_delta_2gb)(fns->ctx, aid, deltaValues);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
}

int
phy_rssi_get_gain_delta_2gb(phy_rssi_info_t *rssii, uint32 aid, int8 *deltaValues)
{
	phy_type_rssi_fns_t *fns = rssii->fns;
	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->get_gain_delta_2gb != NULL) {
		return (fns->get_gain_delta_2gb)(fns->ctx, aid, deltaValues);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
}

int
phy_rssi_set_cal_freq_2g(phy_rssi_info_t *rssii, int8 *nvramValues)
{
	phy_type_rssi_fns_t *fns = rssii->fns;
	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->set_cal_freq_2g != NULL) {
		return (fns->set_cal_freq_2g)(fns->ctx, nvramValues);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
}

int
phy_rssi_get_cal_freq_2g(phy_rssi_info_t *rssii, int8 *nvramValues)
{
	phy_type_rssi_fns_t *fns = rssii->fns;
	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->get_cal_freq_2g != NULL) {
		return (fns->get_cal_freq_2g)(fns->ctx, nvramValues);
	} else {
		PHY_INFORM(("%s: No phy specific function\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}
}
#endif /* WLTEST */
