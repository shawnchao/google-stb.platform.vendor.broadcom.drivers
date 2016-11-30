/*
 * TOF module implementation
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
 * $Id: phy_tof.c 633216 2016-04-21 20:17:37Z vyass $
 */

#ifdef WL_PROXDETECT
#include <phy_dbg.h>
#include <phy_mem.h>
#include <phy_tof_api.h>
#include "phy_type_tof.h"
#include <phy_tof.h>
#include <phy_misc_api.h>

/* module private states */
struct phy_tof_info {
	phy_info_t *pi;
	phy_type_tof_fns_t *fns;
};

/* module private states memory layout */
typedef struct {
	phy_tof_info_t info;
	phy_type_tof_fns_t fns;

} phy_tof_mem_t;

/* local function declaration */
static int phy_tof_init(phy_init_ctx_t *ctx);

/* attach/detach */
phy_tof_info_t *
BCMATTACHFN(phy_tof_attach)(phy_info_t *pi)
{
	phy_tof_info_t *info;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* allocate attach info storage */
	if ((info = phy_malloc(pi, sizeof(phy_tof_mem_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc failed\n", __FUNCTION__));
		goto fail;
	}
	info->pi = pi;
	info->fns = &((phy_tof_mem_t *)info)->fns;

	/* register init fn */
	if (phy_init_add_init_fn(pi->initi, phy_tof_init, info, PHY_INIT_TOF) != BCME_OK) {
		PHY_ERROR(("%s: phy_init_add_init_fn failed\n", __FUNCTION__));
		goto fail;
	}

	return info;

	/* error */
fail:
	phy_tof_detach(info);
	return NULL;
}

void
BCMATTACHFN(phy_tof_detach)(phy_tof_info_t *info)
{
	phy_info_t *pi;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (info == NULL) {
		PHY_INFORM(("%s: null tof module\n", __FUNCTION__));
		return;
	}

	pi = info->pi;

	phy_mfree(pi, info, sizeof(phy_tof_mem_t));
}

/* register phy type specific implementations */
int
BCMATTACHFN(phy_tof_register_impl)(phy_tof_info_t *ti, phy_type_tof_fns_t *fns)
{
	PHY_TRACE(("%s\n", __FUNCTION__));


	*ti->fns = *fns;
	return BCME_OK;
}

void
BCMATTACHFN(phy_tof_unregister_impl)(phy_tof_info_t *ti)
{
	PHY_TRACE(("%s\n", __FUNCTION__));
}

/* TOF init */
static int
WLBANDINITFN(phy_tof_init)(phy_init_ctx_t *ctx)
{
	phy_tof_info_t *tofi = (phy_tof_info_t *)ctx;
	phy_type_tof_fns_t *fns = tofi->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->init_tof != NULL) {
		return (fns->init_tof)(fns->ctx);
	}
	return BCME_OK;
}

#ifdef WL_PROXD_SEQ
/* Configure phy appropiately for RTT measurements */
int
wlc_phy_tof(wlc_phy_t *ppi, bool enter, bool tx, bool hw_adj, bool seq_en, int core)
{
	phy_info_t *pi = (phy_info_t *)ppi;
	int err;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->tof != NULL) {
		if (enter)
			wlc_phy_hold_upd(ppi, PHY_HOLD_FOR_TOF, enter);
		err = (fns->tof)(fns->ctx,  enter, tx, hw_adj, seq_en, core);
		if (!enter)
			wlc_phy_hold_upd(ppi, PHY_HOLD_FOR_TOF, enter);
		return err;
	}
	else {
		return BCME_UNSUPPORTED;
	}

}

/* Setup delays for trigger in RTT Sequence measurements */
int
wlc_phy_tof_seq_params(wlc_phy_t *ppi)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->seq_params != NULL) {
		return (fns->seq_params)(fns->ctx);
	}
	else {
		return BCME_UNSUPPORTED;
	}

}

int
wlc_tof_seq_upd_dly(wlc_phy_t *ppi, bool tx, uint8 core, bool mac_suspend)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->seq_upd_dly != NULL) {
		(fns->seq_upd_dly)(fns->ctx,  tx, core, mac_suspend);
		return BCME_OK;
	}
	else {
		return BCME_UNSUPPORTED;
	}

}

#else
/* Configure phy appropiately for RTT measurements */
int
wlc_phy_tof(wlc_phy_t *ppi, bool enter, bool hw_adj)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	int err;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	if (fns->tof != NULL) {
		if (enter)
			wlc_phy_hold_upd(ppi, PHY_HOLD_FOR_TOF, enter);
		err = (fns->tof)(fns->ctx,  enter, FALSE, hw_adj, FALSE, 0);
		if (!enter)
			wlc_phy_hold_upd(ppi, PHY_HOLD_FOR_TOF, enter);
		return err;
	}
	else {
		return BCME_UNSUPPORTED;
	}

}


#endif /* WL_PROXD_SEQ */
/* Get channel frequency response for deriving 11v rx timestamp */
int
wlc_phy_chan_freq_response(wlc_phy_t *ppi, int len, int nbits,
	int32* Hr, int32* Hi, uint32* Hraw)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));
	if (fns->chan_freq_response != NULL)
		return (fns->chan_freq_response)(fns->ctx,  len, nbits, (cint32 *)Hr, Hraw);

	return BCME_UNSUPPORTED;
}

/* Get mag sqrd channel impulse response(from channel smoothing hw) to derive 11v rx timestamp */
int
wlc_phy_chan_mag_sqr_impulse_response(wlc_phy_t *ppi, int frame_type,
	int len, int offset, int nbits, int32* h, int* pgd, uint32* hraw, uint16 tof_shm_ptr)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));
	if (fns->chan_mag_sqr_impulse_response != NULL)
		return (fns->chan_mag_sqr_impulse_response)(fns->ctx,
			frame_type, len, offset, nbits, h, pgd, hraw, tof_shm_ptr);

	return BCME_UNSUPPORTED;
}
#ifdef WL_PROXD_SEQ
/* Extract information from status bytes of last rxd frame */
int wlc_phy_tof_info(wlc_phy_t *ppi, int* p_frame_type, int* p_frame_bw, int* p_cfo, int8* p_rssi)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	if (fns->info != NULL) {
		return (fns->info)(fns->ctx,  p_frame_type, p_frame_bw, p_cfo, p_rssi);
	}
	else {
		return BCME_UNSUPPORTED;
	}

}

#else
/* Extract information from status bytes of last rxd frame */
int wlc_phy_tof_info(wlc_phy_t *ppi, int* p_frame_type, int* p_frame_bw, int8* p_rssi)
{
	phy_info_t *pi = (phy_info_t *)ppi;
	int cfo;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	if (fns->info != NULL) {
		return (fns->info)(fns->ctx,  p_frame_type, p_frame_bw, &cfo, p_rssi);
	}
	else {
		return BCME_UNSUPPORTED;
	}

}
#endif /* WL_PROXD_SEQ */

/* Get timestamps from ranging sequence */
int
wlc_phy_seq_ts(wlc_phy_t *ppi, int n, void* p_buffer, int tx, int cfo, int adj, void* pparams,
	int32* p_ts, int32* p_seq_len, uint32* p_raw)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	PHY_TRACE(("%s\n", __FUNCTION__));
	if (fns->seq_ts != NULL) {
		return (fns->seq_ts)(fns->ctx, n, (cint32*)p_buffer, tx, cfo, adj, pparams, p_ts,
			p_seq_len, p_raw);
	}
	return BCME_UNSUPPORTED;
}

/* Do any phy specific setup needed for each command */
void wlc_phy_tof_cmd(wlc_phy_t *ppi, bool seq)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	if (fns->cmd != NULL) {
		return (fns->cmd)(fns->ctx,  seq);
	}
}


#ifdef WL_PROXD_SEQ
/* Get TOF K value for initiator and target */
int
wlc_phy_tof_kvalue(wlc_phy_t *ppi, chanspec_t chanspec, uint32 *kip, uint32 *ktp, uint8 seq_en)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	if (fns->kvalue != NULL) {
		return (fns->kvalue)(fns->ctx,  chanspec, 0, kip, ktp, seq_en);
	}

	return -1;
}


/* Get TOF K value for initiator and target */
int
wlc_phy_kvalue(wlc_phy_t *ppi, chanspec_t chanspec, uint32 rspecidx, uint32 *kip,
	uint32 *ktp, uint8 seq_en)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	if (fns->kvalue != NULL) {
		return (fns->kvalue)(fns->ctx,  chanspec, rspecidx, kip, ktp, seq_en);
	}

	return -1;
}

#else
/* Get TOF K value for initiator and target */
int
wlc_phy_tof_kvalue(wlc_phy_t *ppi, chanspec_t chanspec, uint32 *kip, uint32 *ktp)
{
	phy_info_t *pi = (phy_info_t *)ppi;
	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	if (fns->kvalue != NULL) {
		return (fns->kvalue)(fns->ctx,  chanspec, kip, ktp, FALSE);
	}

	return -1;
}
#endif /* WL_PROXD_SEQ */

/* DEBUG */
#ifdef TOF_DBG
int wlc_phy_tof_dbg(wlc_phy_t *ppi, int arg)
{
	phy_info_t *pi = (phy_info_t *)ppi;

	ASSERT(pi != NULL);
	phy_tof_info_t *info = pi->tofi;
	phy_type_tof_fns_t *fns = info->fns;

	if (fns->dbg != NULL) {
		return (fns->dbg)(fns->ctx,  arg);
	}
	else {
		return BCME_UNSUPPORTED;
	}
}
#endif /* TOF_DBG */

#endif /* WL_PROXDETECT */