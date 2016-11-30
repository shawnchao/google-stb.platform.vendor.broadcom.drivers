/*
 * Per-BSS psq. Used to save suppressed packets in the BSS.
 * In multiple destination connection (AP, IBSS) the packets' order are preserved.
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
 * $Id: wlc_bsscfg_psq.c 642065 2016-06-07 07:39:03Z $
 */

#include <wlc_cfg.h>
#ifdef WL_BSSCFG_TX_SUPR
#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <d11.h>
#include <wlc_rate.h>
#include <wlc_pub.h>
#include <wlc_bsscfg.h>
#include <wlc.h>
#include <wlc_scb.h>
#include <wlc_tx.h>
#ifdef PROP_TXSTATUS
#include <wlc_wlfc.h>
#endif
#include <wlc_perf_utils.h>
#include <wlc_bsscfg_psq.h>

/* module states */
struct wlc_bsscfg_psq_info {
	wlc_info_t *wlc;
	int cfgh;
};

/* per bsscfg data in bsscfg cubby */
typedef struct bss_bsscfg_psq_info {
	bool tx_start_pending;	/* defer on/off */
	struct pktq psq;	/* defer queue */
} bss_bsscfg_psq_info_t;

/* bsscfg specific info access accessor */
#define BSS_BSSCFG_PSQ_INFO_LOC(psqi, cfg) (bss_bsscfg_psq_info_t **)BSSCFG_CUBBY(cfg, (psqi)->cfgh)
#define BSS_BSSCFG_PSQ_INFO(psqi, cfg) *BSS_BSSCFG_PSQ_INFO_LOC(psqi, cfg)

#define BSS_BSSCFG_PSQ_INFO_SUPP(cfg) (((cfg)->flags & WLC_BSSCFG_TX_SUPR_ENAB) != 0)

/* local declarations */
static void wlc_bsscfg_psq_bss_updown(void *ctx, bsscfg_up_down_event_data_t *notif);
static int wlc_bsscfg_psq_bss_init(void *ctx, wlc_bsscfg_t *cfg);
static void wlc_bsscfg_psq_bss_deinit(void *ctx, wlc_bsscfg_t *cfg);
#if defined(WL_DATAPATH_LOG_DUMP)
static void wlc_bsscfg_psq_datapath_log_dump(void *ctx, wlc_bsscfg_t *cfg, int tag);
#endif
static void wlc_bsscfg_psq_scb_deinit(void *ctx, struct scb *scb);

/* attach/detach */
wlc_bsscfg_psq_info_t *
BCMATTACHFN(wlc_bsscfg_psq_attach)(wlc_info_t *wlc)
{
	wlc_bsscfg_psq_info_t *psqi;
	bsscfg_cubby_params_t cubby_params;

	/* sanity check */
	ASSERT(wlc != NULL);

	/* module states */
	if ((psqi = MALLOCZ(wlc->osh, sizeof(wlc_bsscfg_psq_info_t))) == NULL) {
		WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes\n",
			wlc->pub->unit, __FUNCTION__, MALLOCED(wlc->osh)));
		goto exit;
	}

	psqi->wlc = wlc;

	/* reserve the bsscfg cubby for any bss specific private data */
	bzero(&cubby_params, sizeof(cubby_params));

	cubby_params.context = psqi;
	cubby_params.fn_init = wlc_bsscfg_psq_bss_init;
	cubby_params.fn_deinit = wlc_bsscfg_psq_bss_deinit;
#if defined(WL_DATAPATH_LOG_DUMP)
	cubby_params.fn_data_log_dump = wlc_bsscfg_psq_datapath_log_dump;
#endif

	psqi->cfgh = wlc_bsscfg_cubby_reserve_ext(wlc, sizeof(bss_bsscfg_psq_info_t *),
	                                          &cubby_params);

	/* reserve the bsscfg cubby for any bss specific private data */
	if (psqi->cfgh < 0) {
		WL_ERROR(("wl%d: %s: wlc_bsscfg_cubby_reserve_ext failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto exit;
	}

	if (wlc_bsscfg_updown_register(wlc, wlc_bsscfg_psq_bss_updown, psqi) != BCME_OK) {
		WL_ERROR(("wl%d: %s: wlc_bsscfg_updown_register failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto exit;
	}

	/* utilize the scb cubby mechanism to register a deinit callback */
	if (wlc_scb_cubby_reserve(wlc, 0, NULL, wlc_bsscfg_psq_scb_deinit, NULL, psqi) < 0) {
		WL_ERROR(("wl%d: %s: wlc_scb_cubby_reserve failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto exit;
	}

	return psqi;

exit:
	MODULE_DETACH(psqi, wlc_bsscfg_psq_detach);
	return NULL;
}

void
BCMATTACHFN(wlc_bsscfg_psq_detach)(wlc_bsscfg_psq_info_t *psqi)
{
	wlc_info_t *wlc;

	if (psqi == NULL)
		return;

	wlc = psqi->wlc;

	(void)wlc_bsscfg_updown_unregister(wlc, wlc_bsscfg_psq_bss_updown, psqi);

	MFREE(wlc->osh, psqi, sizeof(wlc_bsscfg_psq_info_t));
}

/* bsscfg hooks */
static void
wlc_bsscfg_psq_bss_updown(void *ctx, bsscfg_up_down_event_data_t *notif)
{
	wlc_bsscfg_psq_info_t *psqi = (wlc_bsscfg_psq_info_t *)ctx;
	bss_bsscfg_psq_info_t *bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, notif->bsscfg);

	if (!notif->up) {
		wlc_info_t *wlc = psqi->wlc;

		if (bpsqi != NULL) {
			wlc_txq_pktq_flush(wlc, &bpsqi->psq);
		}
	}
}

/* bsscfg cubby hooks */
static int
wlc_bsscfg_psq_bss_init(void *ctx, wlc_bsscfg_t *cfg)
{
	wlc_bsscfg_psq_info_t *psqi = (wlc_bsscfg_psq_info_t *)ctx;
	bss_bsscfg_psq_info_t **pbpsqi = BSS_BSSCFG_PSQ_INFO_LOC(psqi, cfg);
	bss_bsscfg_psq_info_t *bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, cfg);

	/* sanity check */
	ASSERT(bpsqi == NULL);

	/* allocate suppression queue */
	if (BSS_BSSCFG_PSQ_INFO_SUPP(cfg)) {
		wlc_info_t *wlc = psqi->wlc;

		BCM_REFERENCE(wlc);

		if ((bpsqi = MALLOCZ(wlc->osh, sizeof(*bpsqi))) == NULL) {
			WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes\n",
			          wlc->pub->unit, __FUNCTION__, MALLOCED(wlc->osh)));
			return BCME_NOMEM;
		}
		*pbpsqi = bpsqi;

		pktq_init(&bpsqi->psq, WLC_PREC_COUNT, PKTQ_LEN_MAX);
	}

	return BCME_OK;
}

static void
wlc_bsscfg_psq_bss_deinit(void *ctx, wlc_bsscfg_t *cfg)
{
	wlc_bsscfg_psq_info_t *psqi = (wlc_bsscfg_psq_info_t *)ctx;
	bss_bsscfg_psq_info_t **pbpsqi = BSS_BSSCFG_PSQ_INFO_LOC(psqi, cfg);
	bss_bsscfg_psq_info_t *bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, cfg);

	if (bpsqi != NULL) {
		wlc_info_t *wlc = psqi->wlc;

		BCM_REFERENCE(wlc);
#ifdef PKTQ_LOG
		wlc_pktq_stats_free(wlc, &bpsqi->psq);
#endif
		MFREE(wlc->osh, bpsqi, sizeof(*bpsqi));

		*pbpsqi = NULL;
	}
}


#if defined(WL_DATAPATH_LOG_DUMP)
static void
wlc_bsscfg_psq_datapath_log_dump(void *ctx, wlc_bsscfg_t *cfg, int tag)
{
	wlc_bsscfg_psq_info_t *psqi = (wlc_bsscfg_psq_info_t *)ctx;
	bss_bsscfg_psq_info_t *bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, cfg);

	if (bpsqi != NULL && pktq_n_pkts_tot(&bpsqi->psq) > 0) {
		struct pktq *q = &bpsqi->psq;
		uint prec;

		EVENT_LOG(tag, "BSSCFG PSQ Dump: nprec %d", q->num_prec);

		for (prec = 0; prec < q->num_prec; prec++) {
			uint len = pktqprec_n_pkts(q, prec);
			if (len > 0) {
				EVENT_LOG(tag, "[%u]: %u", prec, len);
			}
		}
	}
}
#endif /* WL_DATAPATH_LOG_DUMP */

/* scb cubby hooks */
static void
wlc_bsscfg_psq_scb_deinit(void *ctx, struct scb *scb)
{
	wlc_bsscfg_t *cfg = SCB_BSSCFG(scb);

	if (cfg != NULL) { /* this condition may never be FALSE */
		wlc_bsscfg_psq_info_t *psqi = (wlc_bsscfg_psq_info_t *)ctx;
		bss_bsscfg_psq_info_t *bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, cfg);

		if (bpsqi != NULL) {
			wlc_info_t *wlc = psqi->wlc;

			wlc_pktq_scb_free(wlc, &bpsqi->psq, scb);
		}
	}
}

/* public interfaces */

/* block data fifo during drain */
void
wlc_bsscfg_tx_stop(wlc_bsscfg_psq_info_t *psqi, wlc_bsscfg_t *cfg)
{
	wlc_info_t *wlc = psqi->wlc;

	/* Nothing to do */
	if (BSS_TX_SUPR(cfg))
		return;

	cfg->flags |= WLC_BSSCFG_TX_SUPR;

	/* PropTx: wlc_txfifo_suppress() and wlc_wlfc_flush_pkts_to_host()
	 * takes care of flushing out packets
	 */
	if (PROP_TXSTATUS_ENAB(wlc->pub))
		return;

	/* If there is anything in the data fifo then allow it to drain */
	if (TXPKTPENDTOT(wlc) > 0)
		wlc_block_datafifo(wlc, DATA_BLOCK_TX_SUPR, DATA_BLOCK_TX_SUPR);

	WL_INFORM(("wl%d.%d: %s: pending %d packets %d\n",
	        wlc->pub->unit, WLC_BSSCFG_IDX(cfg), __FUNCTION__,
	        TXPKTPENDTOT(wlc), pktq_n_pkts_tot(WLC_GET_TXQ(cfg->wlcif->qi))));

}

/** Call after the FIFO has drained */
void
wlc_bsscfg_tx_check(wlc_bsscfg_psq_info_t *psqi)
{
	wlc_info_t *wlc = psqi->wlc;

	ASSERT(TXPKTPENDTOT(wlc) == 0);

	WL_INFORM(("wl%d: %s: TX SUPR %d\n",
	        wlc->pub->unit, __FUNCTION__,
	        (wlc->block_datafifo & DATA_BLOCK_TX_SUPR) != 0));

	if (wlc->block_datafifo & DATA_BLOCK_TX_SUPR) {
		int i;
		wlc_bsscfg_t *cfg;

		wlc_block_datafifo(wlc, DATA_BLOCK_TX_SUPR, 0);

		/* Now complete all the pending transitions */
		FOREACH_BSS(wlc, i, cfg) {
			/*
			* bpsqi could be NULL e.g. if this is the STA cfg (no psq)
			* and there's an AP cfg elsewhere; check cfg flags to avoid
			* NULL deref
			*/
			if (BSS_BSSCFG_PSQ_INFO_SUPP(cfg)) {
				bss_bsscfg_psq_info_t *bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, cfg);

				if (bpsqi->tx_start_pending) {
					bpsqi->tx_start_pending = FALSE;
					wlc_bsscfg_tx_start(psqi, cfg);
				}
			}
		}
	}
}

void
wlc_bsscfg_tx_start(wlc_bsscfg_psq_info_t *psqi, wlc_bsscfg_t *cfg)
{
	wlc_info_t *wlc = psqi->wlc;
	bss_bsscfg_psq_info_t *bpsqi;
	struct pktq *txq;
	void *pkt;
	int prec;

	/* Nothing to do */
	if (!BSS_TX_SUPR(cfg))
		return;

	bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, cfg);

	if (wlc->block_datafifo & DATA_BLOCK_TX_SUPR) {
		/* Finish the transition first to avoid reordering frames */
		if (TXPKTPENDTOT(wlc) > 0) {
			bpsqi->tx_start_pending = TRUE;
			return;
		}
		wlc_block_datafifo(wlc, DATA_BLOCK_TX_SUPR, 0);
	}

	WL_INFORM(("wl%d.%d: %s: TX SUPR %d pending %d packets %d\n",
	        wlc->pub->unit, WLC_BSSCFG_IDX(cfg), __FUNCTION__,
	        (wlc->block_datafifo & DATA_BLOCK_TX_SUPR) != 0,
	        TXPKTPENDTOT(wlc), pktq_n_pkts_tot(&bpsqi->psq)));

	cfg->flags &= ~WLC_BSSCFG_TX_SUPR;

	/* Dump all the packets from bpsqi->psq to txq but to the front */
	/* This is done to preserve the ordering w/o changing the precedence level
	 * since AMPDU module keeps track of sequence numbers according to their
	 * precedence!
	 */
	txq = WLC_GET_TXQ(cfg->wlcif->qi);

	while ((pkt = pktq_deq_tail(&bpsqi->psq, &prec))) {
		if (!wlc_prec_enq_head(wlc, txq, pkt, prec, TRUE)) {
			WL_ERROR(("wl%d: wlc_bsscfg_tx_start: txq full, frame discarded\n",
			          wlc->pub->unit));
			PKTFREE(wlc->osh, pkt, TRUE);
		}
	}

	/* Must check both the primary and low TXQ in new datapath */
	if (!pktq_empty(txq) ||
	    !wlc_low_txq_empty(cfg->wlcif->qi->low_txq) ||
	    FALSE) {
		WL_INFORM(("wl%d.%d: %s: resend packets %d\n",
		        wlc->pub->unit, WLC_BSSCFG_IDX(cfg),
		        __FUNCTION__, pktq_n_pkts_tot(txq)));
		wlc_send_q(wlc, cfg->wlcif->qi);
	}
}

/* Enqueue absence suppressed frame into psq at high prec */
/* Caller should free the packet if it cannot be accommodated */
bool
wlc_bsscfg_tx_supr_enq(wlc_bsscfg_psq_info_t *psqi, wlc_bsscfg_t *cfg, void *pkt)
{
	ASSERT(pkt != NULL);

	/* Mark as retrieved from HW FIFO */
	WLPKTTAG(pkt)->flags |= WLF_FIFOPKT;

	return wlc_bsscfg_tx_psq_enq(psqi, cfg, pkt, WLC_PRIO_TO_HI_PREC(PKTPRIO(pkt)));
}

/* Enqueue frame into psq at the given prec */
/* Caller should free the packet if it cannot be accommodated */
bool
wlc_bsscfg_tx_psq_enq(wlc_bsscfg_psq_info_t *psqi, wlc_bsscfg_t *cfg, void *sdu, uint prec)
{
	wlc_info_t *wlc = psqi->wlc;
	bss_bsscfg_psq_info_t *bpsqi;

	ASSERT(cfg != NULL);

	bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, cfg);

	/* Caller should free the packet if it cannot be accomodated */
	if (!wlc_prec_enq(wlc, &bpsqi->psq, sdu, prec)) {
		WL_INFORM(("wl%d: %s: txq full, frame discarded\n",
		        wlc->pub->unit, __FUNCTION__));
		return TRUE;
	}

	return FALSE;
}

/* Count the number of packets held in the psq */
uint
wlc_bsscfg_tx_pktcnt(wlc_bsscfg_psq_info_t *psqi, wlc_bsscfg_t *cfg)
{
	bss_bsscfg_psq_info_t *bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, cfg);

	if (bpsqi != NULL) {
		return (uint)pktq_n_pkts_tot(&bpsqi->psq);
	}
	return 0;
}

/* Remove all packets from the psq for PROP_TXSTATUS */
void
wlc_bsscfg_tx_flush(wlc_bsscfg_psq_info_t *psqi, wlc_bsscfg_t *cfg)
{
#ifdef PROP_TXSTATUS
	bss_bsscfg_psq_info_t *bpsqi = BSS_BSSCFG_PSQ_INFO(psqi, cfg);

	if (PROP_TXSTATUS_ENAB(psqi->wlc->pub) && bpsqi != NULL) {
		wlc_wlfc_flush_queue(psqi->wlc, &bpsqi->psq);
	}
#else
	BCM_REFERENCE(psqi);
	BCM_REFERENCE(cfg);
#endif
}

struct pktq *
wlc_bsscfg_get_psq(wlc_bsscfg_psq_info_t *psqi, wlc_bsscfg_t *cfg)
{
	bss_bsscfg_psq_info_t *bpsqi;

	bpsqi = psqi ? BSS_BSSCFG_PSQ_INFO(psqi, cfg) : NULL;
	if (bpsqi != NULL) {
		return &bpsqi->psq;
	}

	return NULL;
}

#endif /* WL_BSSCFG_TX_SUPR */
