/**
 * @file
 * @brief
 * Performance measurement/analysis facilities
 *
 * Copyright (C) 2016, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: wlc_perf_utils.c 612761 2016-01-14 23:06:00Z $
 */

#include <wlc_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <wlioctl.h>
#include <d11.h>
#include <wlc_rate.h>
#include <wlc_pub.h>
#include <wlc_bsscfg.h>
#include <wlc.h>
#include <wlc_apps.h>
#include <wlc_scb.h>
#ifdef WLNAR
#include <wlc_nar.h>
#endif
#ifdef WLAMPDU
#include <wlc_ampdu.h>
#endif
#include <wl_export.h>
#include <wlc_lq.h>
#include <wlc_qoscfg.h>
#include <wlc_event_utils.h>
#include <wlc_perf_utils.h>
#include <wlc_dump.h>

/* IOVar table */
enum {
	IOV_PKTQ_STATS = 0,
	IOV_TXDELAY_PARAMS = 1,
	IOV_DLYSTATS = 2,
	IOV_DLYSTATS_CLEAR = 3,
	IOV_LAST
};

static const bcm_iovar_t perf_utils_iovars[] = {
#ifdef PKTQ_LOG
	{"pktq_stats", IOV_PKTQ_STATS, (IOVF_GET_UP), IOVT_BUFFER,
	MAX(sizeof(wl_iov_mac_params_t), sizeof(wl_iov_pktq_log_t))},
#endif
#ifdef WLPKTDLYSTAT
#ifdef WLPKTDLYSTAT_IND
	{"txdelay_params", IOV_TXDELAY_PARAMS, 0, IOVT_BUFFER, sizeof(txdelay_params_t)},
#endif /* WLPKTDLYSTAT_IND */
	{"dlystats", IOV_DLYSTATS, 0, IOVT_BUFFER, WL_TXDELAY_STATS_FIXED_SIZE},
	{"dlystats_clear", IOV_DLYSTATS_CLEAR, 0, IOVT_VOID, 0},
#endif /* WLPKTDLYSTAT */
	{NULL, 0, 0, 0, 0}
};

/* local prototypes */
#ifdef WLPKTDLYSTAT
static int wlc_dlystats_scb_init(void *ctx, struct scb *scb);
static void wlc_dlystats_scb_deinit(void *ctx, struct scb *scb);
static int wlc_dlystats(wlc_info_t *wlc, txdelay_stats_t *dlystats,
	struct ether_addr *scb_addr);
#ifdef WLPKTDLYSTAT_IND
static void wlc_delay_stats_timer(void *arg);
#endif
#endif /* WLPKTDLYSTAT */
#ifdef PKTQ_LOG
static int wlc_pktq_stats(wl_iov_mac_full_params_t* full_params, uint8 params_version,
	struct bcmstrbuf* b, wl_iov_pktq_log_t* iov_pktq, wlc_info_t* wlc, wlc_bsscfg_t *cfg);
#endif /* PKTQ_LOG */

static int wlc_perf_utils_doiovar(void *hdl, const bcm_iovar_t *vi, uint32 actionid,
	const char *name,
	void *params, uint p_len, void *arg, int len, int val_size, struct wlc_if *wlcif);
static int wlc_perf_utils_wlc_up(void *ctx);
static int wlc_perf_utils_wlc_down(void *ctx);

typedef struct wlc_perf_stats wlc_perf_stats_t;

struct wlc_perf_utils {
	wlc_info_t *wlc;
#if defined(WLPKTDLYSTAT)
#if defined(WLPKTDLYSTAT_IND)
	txdelay_params_t *txdelay_params;
	struct wl_timer *pktdlystat_timer;	/* timer to generate event based on delay */
#endif
#endif /* WLPKTDLYSTAT */
};

#ifdef WLPKTDLYSTAT
/* scb cubby */
typedef struct {
	scb_delay_stats_t	delay_stats[AC_COUNT];	/**< per-AC delay stats */
#ifdef WLPKTDLYSTAT_IND
	txdelay_params_t	txdelay_params;
#endif /* WLPKTDLYSTAT_IND */
} scb_cubby_t;

#ifdef WLPKTDLYSTAT_IND
#define TXDELAY_SAMPLE_PERIOD	1	/* Txdelay sample period, in sec */
#define TXDELAY_SAMPLE_CNT	4	/* Txdelay sample count number */
#define TXDELAY_RATIO		30	/* Avg Txdelay ratio */
#define TXDELAY_TIMER_PERIOD	1000	/* Txdelay timer period, in msec */
#define TXDELAY_MIN_SAMPLE_DATA	8	/* minimum number of time period units before
					consequent packet delay events can be generated
					*/
#endif /* WLPKTDLYSTAT_IND */
#endif /* WLPKTDLYSTAT */


/* attach/detach */
wlc_perf_utils_t *
BCMATTACHFN(wlc_perf_utils_attach)(wlc_info_t *wlc)
{
	wlc_perf_utils_t *pui;

	if ((pui = MALLOCZ(wlc->osh, sizeof(*pui))) == NULL) {
		WL_ERROR(("wl%d: %s: mem alloc failed. allocated %d bytes.\n",
		          wlc->pub->unit, __FUNCTION__, MALLOCED(wlc->osh)));
		goto fail;
	}
	pui->wlc = wlc;


	if (wlc_module_register(wlc->pub, perf_utils_iovars, "perf_utils", pui,
			wlc_perf_utils_doiovar,
			NULL, wlc_perf_utils_wlc_up, wlc_perf_utils_wlc_down) != BCME_OK) {
		WL_ERROR(("wl%d: %s wlc_module_register() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}
#if defined(WLPKTDLYSTAT)
	/* utilize scb cubby facility for stats storage allocation/free */
	if (wlc_scb_cubby_reserve(wlc, 0,
			wlc_dlystats_scb_init, wlc_dlystats_scb_deinit, NULL, wlc) < 0) {
		WL_ERROR(("wl%d: %s: wlc_scb_cubby_reserve failed.\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

#if defined(WLPKTDLYSTAT_IND)
	if ((pui->txdelay_params =
	     MALLOCZ(wlc->osh, sizeof(*(pui->txdelay_params)))) == NULL) {
		WL_ERROR(("wl%d: %s: mem alloc failed. allocated %d bytes.\n",
		          wlc->pub->unit, __FUNCTION__, MALLOCED(wlc->osh)));
		goto fail;
	}

	pui->txdelay_params->period = TXDELAY_SAMPLE_PERIOD;
	pui->txdelay_params->cnt = TXDELAY_SAMPLE_CNT;
	pui->txdelay_params->ratio = TXDELAY_RATIO;

	if (pui->txdelay_params->period > 0) {
		/* Init the timer */
		pui->pktdlystat_timer =
		        wl_init_timer(wlc->wl, wlc_delay_stats_timer, pui,
		                      "pkt_delay_stats_timer");
		if (pui->pktdlystat_timer == NULL) {
			WL_ERROR(("wl%d: %s: wl_init_timer failed.\n",
			          wlc->pub->unit, __FUNCTION__));
			goto fail;
		}
	}
#endif /* defined(WLPKTDLYSTAT_IND) */
#endif /* defined(WLPKTDLYSTAT) */

	return pui;

fail:
	wlc_perf_utils_detach(pui);
	return NULL;
}

void
BCMATTACHFN(wlc_perf_utils_detach)(wlc_perf_utils_t *pui)
{
	wlc_info_t *wlc;

	if (pui == NULL)
		return;

	wlc = pui->wlc;

#if defined(WLPKTDLYSTAT)
#if defined(WLPKTDLYSTAT_IND)
	/* Cancel the delay stats timer */
	if (pui->pktdlystat_timer != NULL) {
		wl_free_timer(wlc->wl, pui->pktdlystat_timer);
	}
	if (pui->txdelay_params != NULL) {
		MFREE(wlc->osh, pui->txdelay_params, sizeof(*(pui->txdelay_params)));
	}
#endif /* WLPKTDLYSTAT_IND */
#endif /* WLPKTDLYSTAT */
	(void)wlc_module_unregister(wlc->pub, "perf_utils", pui);

	MFREE(wlc->osh, pui, sizeof(*pui));
}


#ifdef PKTQ_LOG
/** de-allocate pktq stats log data */
void
wlc_pktq_stats_free(wlc_info_t* wlc, struct pktq* q)
{
	if (q->pktqlog) {
		uint32 prec_mask = q->pktqlog->_prec_log;
		uint32 index;

		for (index = 0; index < PKTQ_MAX_PREC; index++) {

			pktq_counters_t* counter = q->pktqlog->_prec_cnt[index];

			if (prec_mask & (1 << index)) {
				ASSERT(counter != 0);
				MFREE(wlc->osh, counter, sizeof(*counter));
			} else {
				ASSERT(counter == 0);
			}
		}
		MFREE(wlc->osh, q->pktqlog, sizeof(*q->pktqlog));
		q->pktqlog = 0;
	}
}

/** enable PKTQ_LOG for a precedence */
pktq_counters_t*
wlc_txq_prec_log_enable(wlc_info_t* wlc, struct pktq* q, uint32 i)
{
	pktq_counters_t* new = MALLOC(wlc->osh, sizeof(*new));
	/* check not already allocated */
	ASSERT(q->pktqlog);
	ASSERT(q->pktqlog->_prec_cnt[i] == NULL);
	ASSERT(i < q->num_prec);

	if (new) {
		bzero(new, sizeof(*new));

		new->max_avail = pktq_pavail(q, i);
		new->max_used = pktq_plen(q, i);

		q->pktqlog->_prec_cnt[i] = new;
		q->pktqlog->_prec_log |= (1 << i);
	}

	/* if this is HI-PREC, ensure standard prec is enabled */
	if (i & 1) {
		i &= ~1;
		if (q->pktqlog->_prec_cnt[i] == NULL) {
			wlc_txq_prec_log_enable(wlc, q, i);
		}
	}
	return new;
}

/** format data according to v05 of the interface */
static void
wlc_txq_prec_dump(wlc_info_t* wlc, struct pktq* q, wl_iov_pktq_log_t* pktq_log, uint8 index,
                  bool clear, uint32 time, uint32 prec_mask, uint32 pps_time)
{
	uint32 i;
	uint32 result_mask = 0;

	ASSERT(pktq_log->version == 0x05);

	pktq_log->pktq_log.v05.num_prec[index] = 0;

	if (prec_mask & PKTQ_LOG_AUTO) {
		result_mask |= PKTQ_LOG_AUTO;
	}

	if (!q) {
		/* Handle non-existent queues */
		pktq_log->pktq_log.v05.counter_info[index] = result_mask;
	    return;
	}

	if (!q->pktqlog) {
		/* Dynamically allocate memory for pktq stats logging */
		q->pktqlog = MALLOC(wlc->osh, sizeof(*q->pktqlog));

		if (!q->pktqlog) {
			return;
		}
		bzero(q->pktqlog, sizeof(*q->pktqlog));
	}

	pktq_log->pktq_log.v05.num_prec[index] = (uint8)q->num_prec;

	/* this is the 'auto' prec setting */
	if (prec_mask & PKTQ_LOG_AUTO) {
		q->pktqlog->_prec_log |= PKTQ_LOG_AUTO;

		if (prec_mask & PKTQ_LOG_DEF_PREC) {
			/* if was default (all prec) and AUTO mode is set,
			 * we can remove the "all prec"
			 */
			prec_mask &= ~0xFFFF;
		} else if (prec_mask & 0xFFFF) {
			/* this was not default, there is actual specified
			 * prec_mask, so use it rather than automatic mode
			 */
			prec_mask &= ~PKTQ_LOG_AUTO;
		}
	}

	for (i = 0; i < q->num_prec; i++) {

		/* check pktq stats data was requested and also available */
		if ((((prec_mask & (1 << i)) || (prec_mask & PKTQ_LOG_AUTO)) &&
		                             (q->pktqlog->_prec_log & (1 << i)))) {
			pktq_log_counters_v05_t* counters =
			                    &pktq_log->pktq_log.v05.counters[index][i];
			pktq_counters_t*         pktq_count = q->pktqlog->_prec_cnt[i];

			ASSERT(pktq_count);

			result_mask |= (1 << i);

			counters->throughput     = pktq_count->throughput;

			counters->requested      = pktq_count->requested;
			counters->stored         = pktq_count->stored;
			counters->saved          = pktq_count->saved;
			counters->selfsaved      = pktq_count->selfsaved;
			counters->full_dropped   = pktq_count->full_dropped;
			counters->dropped        = pktq_count->dropped;
			counters->sacrificed     = pktq_count->sacrificed;
			counters->busy           = pktq_count->busy;
			counters->retry          = pktq_count->retry;
			counters->ps_retry       = pktq_count->ps_retry;
			counters->suppress       = pktq_count->suppress;
			counters->retry_drop     = pktq_count->retry_drop;
			counters->max_avail      = pktq_count->max_avail;
			counters->max_used       = pktq_count->max_used;
			counters->rtsfail        = pktq_count->rtsfail;
			counters->acked          = pktq_count->acked;
			counters->queue_capacity = pktq_pmax(q, i);
			counters->txrate_succ    = pktq_count->txrate_succ;
			counters->txrate_main    = pktq_count->txrate_main;
			counters->airtime        = pktq_count->airtime;

			counters->time_delta     = time - pktq_count->_logtime;

			if (clear) {
				bzero(pktq_count, sizeof(*pktq_count));
				pktq_count->max_avail = pktq_pavail(q, i);
				pktq_count->max_used = pktq_plen(q, i);
			}

			pktq_count->_logtime    = time;
		}
		/* else check pktq stats data was requested but not yet allocated */
		else if ((prec_mask & (1 << i)) && !(q->pktqlog->_prec_log & (1 << i))) {
			wlc_txq_prec_log_enable(wlc, q, i);
		}
	}
	pktq_log->pktq_log.v05.counter_info[index] = result_mask;
	{
		pktq_log->pktq_log.v05.pspretend_time_delta[index] = (uint32)-1;
	}
} /* wlc_txq_prec_dump */

/** wlc_pktq_stats - return data according to revision v05 of the API */
static int
wlc_pktq_stats(wl_iov_mac_full_params_t* full_params, uint8 params_version, struct bcmstrbuf* b,
               wl_iov_pktq_log_t* iov_pktq, wlc_info_t* wlc, wlc_bsscfg_t *cfg)
{
	wl_iov_mac_params_t* params	= &full_params->params;
	wl_iov_mac_extra_params_t* extra_params = &full_params->extra_params;

	uint32 addrs_todo = (params->num_addrs <= WL_IOV_MAC_PARAM_LEN ?
	                     params->num_addrs : WL_IOV_MAC_PARAM_LEN);
	struct ether_addr* ea = params->ea;

	wlc_txq_info_t* qi = wlc->active_queue;

	char* time = "";
	uint32 tsf_time;


	tsf_time = (R_REG(wlc->osh, &wlc->regs->tsf_timerlow));

	while (addrs_todo) {
		typedef struct pktq * pktq_get_fn_t(wlc_info_t*, struct scb*);
		pktq_get_fn_t* pktq_func = NULL;
		char* queue_type = "";
		uint8 index = (uint8)(ea - params->ea);
		char marker = '0' + index;
		uint32 prec_mask;

		if (params_version == 4) {
			prec_mask = extra_params->addr_info[index];
		} else {
			prec_mask = 0xFFFF;
		}

		/* bit 7 (0x80) is used as a flag, so mask it off */
		switch (params->addr_type[index] & 0x7F) {
			case 'C':
			{
				bcm_bprintf(b, "[%c]%sCommon queue:\n[%c]", marker, time, marker);
				wlc_txq_prec_dump(wlc, WLC_GET_TXQ(qi),
					iov_pktq, index, TRUE, tsf_time, prec_mask, (uint32)-1);
				break;
			}
			default:
			{
				bcm_bprintf(b, "[%c]%sUnknown or missing addr prefix\n[%c]",
				                marker, time, marker);
				iov_pktq->pktq_log.v05.num_prec[index] = 0;
				break;
			}
#ifdef AP
			case 'P':
			{
				pktq_func = wlc_apps_prec_pktq;
				queue_type = "Power save queue";
				break;
			}
#endif

#ifdef WLAMPDU
			case 'A':
			{
				pktq_func = scb_ampdu_prec_pktq;
				queue_type = "AMPDU queue";
				break;
			}
#endif
#ifdef WLNAR
			case 'N':
			{
				pktq_func = wlc_nar_prec_pktq;
				queue_type = "NAR queue";
				break;
			}
#endif /* WLNAR */
		}

		if (pktq_func) {
			struct scb *scb = NULL;

			bool addr_OK = (!ETHER_ISMULTI(ea) && (scb = wlc_scbfind(wlc, cfg, ea)));

			if (addr_OK) {
				uint32  pps_time = (uint32)-1;
				bcm_bprintf(b, "[%c]%s%s "MACF"\n[%c]",
				            marker, time, queue_type,
				            ETHERP_TO_MACF(ea), marker);

				wlc_txq_prec_dump(wlc, pktq_func(wlc, scb), iov_pktq, index,
				                  TRUE, tsf_time, prec_mask, pps_time);
			} else {
				iov_pktq->pktq_log.v05.num_prec[index] = 0;
			}

		}

		addrs_todo--;
		ea++;
	}
	return BCME_OK;
} /* wlc_pktq_stats */
#endif /* PKTQ_LOG */

/* wlc up/down */
static int
wlc_perf_utils_wlc_up(void *ctx)
{
#if defined(WLPKTDLYSTAT) && defined(WLPKTDLYSTAT_IND)
	wlc_perf_utils_t *pui = ctx;
	wlc_info_t *wlc = pui->wlc;

	/* Start the Packet Delay stats' periodic sampling timer */
	if (pui->pktdlystat_timer != NULL) {
		wl_add_timer(wlc->wl, pui->pktdlystat_timer, TXDELAY_TIMER_PERIOD, TRUE);
	}
#endif /* defined(WLPKTDLYSTAT) && defined(WLPKTDLYSTAT_IND) */
	return BCME_OK;
}

static int
wlc_perf_utils_wlc_down(void *ctx)
{
#if defined(WLPKTDLYSTAT) && defined(WLPKTDLYSTAT_IND)
	wlc_perf_utils_t *pui = ctx;
	wlc_info_t *wlc = pui->wlc;

	/* Start the Packet Delay stats' periodic sampling timer */
	if (pui->pktdlystat_timer != NULL) {
		wl_del_timer(wlc->wl, pui->pktdlystat_timer);
	}
#endif /* defined(WLPKTDLYSTAT) && defined(WLPKTDLYSTAT_IND) */
	return BCME_OK;
}

/* iovar dispatcher */
static int
wlc_perf_utils_doiovar(void *hdl, const bcm_iovar_t *vi, uint32 actionid, const char *name,
	void *params, uint p_len, void *arg, int len, int val_size, struct wlc_if *wlcif)
{
	wlc_perf_utils_t *pui = hdl;
	wlc_info_t *wlc = pui->wlc;
	wlc_bsscfg_t *bsscfg;
	int err = BCME_OK;

	/* update bsscfg w/provided interface context */
	bsscfg = wlc_bsscfg_find_by_wlcif(wlc, wlcif);
	ASSERT(bsscfg != NULL);

	BCM_REFERENCE(bsscfg);

	/* Do the actual parameter implementation */
	switch (actionid) {

#ifdef PKTQ_LOG
	case IOV_GVAL(IOV_PKTQ_STATS):	{
		struct bcmstrbuf b_local;
		int32  avail_len;
		uint8 params_version;
		wl_iov_pktq_log_t*  iov_pktq_log_p  =  (wl_iov_pktq_log_t*) arg;
		wl_iov_mac_full_params_t iov_full_params;

		/* keep a local copy of the input parameter block */
		iov_full_params.params = *(wl_iov_mac_params_t*) params;

		params_version = (iov_full_params.params.num_addrs >> 8) & 0xFF;
		iov_full_params.params.num_addrs &= ~(0xFF << 8);

		if (params_version == 4) {
			/* this means the "extra_params" is there */
			iov_full_params.extra_params =
			          ((wl_iov_mac_full_params_t*) params)->extra_params;
		}

		/* clear return parameter block */
		bzero(iov_pktq_log_p, sizeof(*iov_pktq_log_p));

		iov_pktq_log_p->version = 0x05;

		/* Copy back the input parameter block */
		iov_pktq_log_p->params = iov_full_params.params;

		/* Setup character string return block */
		avail_len = len - OFFSETOF(wl_iov_pktq_log_t, pktq_log.v05.headings);
		avail_len = (avail_len > 0 ? avail_len : 0);
		bcm_binit(&b_local, (char*)(& iov_pktq_log_p->pktq_log.v05.headings[0]), avail_len);

		err = wlc_pktq_stats(&iov_full_params, params_version, &b_local, iov_pktq_log_p,
		                     wlc, bsscfg);
		break;
	}
#endif /* PKTQ_LOG */
#ifdef WLPKTDLYSTAT
#ifdef WLPKTDLYSTAT_IND
	case IOV_GVAL(IOV_TXDELAY_PARAMS): {
		txdelay_params_t *txdelay_params = (txdelay_params_t *)arg;
		txdelay_params->ratio = pui->txdelay_params->ratio;
		txdelay_params->period = pui->txdelay_params->period;
		txdelay_params->cnt = pui->txdelay_params->cnt;
		txdelay_params->tune = pui->txdelay_params->tune;
		break;
	}
	case IOV_SVAL(IOV_TXDELAY_PARAMS): {
		txdelay_params_t *txdelay_params = (txdelay_params_t *)arg;
		pui->txdelay_params->ratio = txdelay_params->ratio;
		pui->txdelay_params->period = txdelay_params->period;
		pui->txdelay_params->cnt = txdelay_params->cnt;
		pui->txdelay_params->tune = txdelay_params->tune;
		break;
	}
#endif /* WLPKTDLYSTAT_IND */
	case IOV_GVAL(IOV_DLYSTATS): {
		txdelay_stats_t *p = (txdelay_stats_t *)params;
		struct ether_addr ea;

		if (p->version != TXDELAY_STATS_VERSION) {
			err = BCME_VERSION;
			break;
		}
		memcpy(&ea, &p->scb_delay_stats[0].ea, sizeof(ea));
		err = wlc_dlystats(wlc, arg, &ea);
		break;
	}
	case IOV_SVAL(IOV_DLYSTATS_CLEAR):
		wlc_dlystats_clear(wlc);
		break;
#endif /* WLPKTDLYSTAT */

	default:
		err = BCME_UNSUPPORTED;
		break;
	}

	return err;
} /* wlc_perf_utils_doiovar */


#ifdef WLPKTDLYSTAT
static int
wlc_dlystats_scb_init(void *ctx, struct scb *scb)
{
	wlc_info_t *wlc = ctx;
#ifdef NOT_YET
	if (SCB_INTERNAL(scb))
		return BCME_OK;
#endif
	scb->delay_stats = MALLOCZ(wlc->osh, sizeof(scb_cubby_t));
	if (scb->delay_stats == NULL) {
		return BCME_NOMEM;
	}
#ifdef WLPKTDLYSTAT_IND
	scb->txdelay_params = (txdelay_params_t *)&scb->delay_stats[AC_COUNT];
#endif
	return BCME_OK;
}

static void
wlc_dlystats_scb_deinit(void *ctx, struct scb *scb)
{
	wlc_info_t *wlc = ctx;

	if (scb->delay_stats != NULL) {
		MFREE(wlc->osh, scb->delay_stats, sizeof(scb_cubby_t));
	}
}

void
wlc_scb_dlystat_dump(struct scb *scb, struct bcmstrbuf *b)
{
	int i, last;
	uint ac;
	uint32 sum;
	uint32 max_val, total = 0;
	scb_delay_stats_t *delay_stats;

	/* Report Latency and packet loss statistics (for each AC) */
	for (ac = 0; ac < AC_COUNT; ac++) {

		delay_stats = scb->delay_stats + ac;
		for (i = 0, total = 0, sum = 0; i < RETRY_SHORT_DEF; i++) {
			total += delay_stats->txmpdu_cnt[i];
			sum += delay_stats->delay_sum[i];
		}
		if (total) {
			bcm_bprintf(b, "%s:  PLoss %d/%d (%d%%)\n",
					aci_names[ac], delay_stats->txmpdu_lost, total,
					(delay_stats->txmpdu_lost * 100) / total);
			bcm_bprintf(b, "\tDelay stats in usec (avg/min/max): %d %d %d\n",
					CEIL(sum, total), delay_stats->delay_min,
					delay_stats->delay_max);
			bcm_bprintf(b, "\tTxcnt Hist    :");
			for (i = 0; i < RETRY_SHORT_DEF; i++) {
				bcm_bprintf(b, "  %d", delay_stats->txmpdu_cnt[i]);
			}
			bcm_bprintf(b, "\n\tDelay vs Txcnt:");
			for (i = 0; i < RETRY_SHORT_DEF; i++) {
				if (delay_stats->txmpdu_cnt[i])
					bcm_bprintf(b, "  %d", delay_stats->delay_sum[i] /
							delay_stats->txmpdu_cnt[i]);
				else
					bcm_bprintf(b, "  -1");
			}
			bcm_bprintf(b, "\n");

			for (i = 0, max_val = 0, last = 0; i < WLPKTDLY_HIST_NBINS; i++) {
				max_val += delay_stats->delay_hist[i];
				if (delay_stats->delay_hist[i]) last = i;
			}
			last = 8 * (last/8 + 1) - 1;
			if (max_val) {
				bcm_bprintf(b, "\tDelay Hist \n\t\t:");
				for (i = 0; i <= last; i++) {
					bcm_bprintf(b, " %d(%d%%)", delay_stats->
							delay_hist[i],
							(delay_stats->delay_hist[i] * 100)
							/ max_val);
					if ((i % 8) == 7 && i != last)
						bcm_bprintf(b, "\n\t\t:");
				}
			}
			bcm_bprintf(b, "\n");
		}
	}
	bcm_bprintf(b, "\n");
}

static int
wlc_dlystats(wlc_info_t *wlc, txdelay_stats_t *dlystats, struct ether_addr *scb_addr)
{
	uint32 ret = 0, scb_idx = 0;

	struct scb *scb;
	struct scb_iter scbiter;

	scb_total_delay_stats_t *scb_stats;
	uint32 specified_macaddr;
	struct ether_addr zmac;

	memset(&zmac, 0, sizeof(zmac));
	specified_macaddr = memcmp(scb_addr, &zmac, sizeof(zmac));
	memset(dlystats, 0, sizeof(*dlystats));

	dlystats->full_result = TXDELAY_STATS_FULL_RESULT;
	FOREACHSCB(wlc->scbstate, &scbiter, scb) {
		scb_stats = dlystats->scb_delay_stats+scb_idx;
		if (specified_macaddr) {
			if (!memcmp(scb_addr, &scb->ea, sizeof(*scb_addr))) {
				dlystats->scb_cnt = 1;
				memcpy(scb_stats->dlystats, scb->delay_stats,
				       sizeof(*(scb->delay_stats)) * AC_COUNT);
				memcpy(&scb_stats->ea, &scb->ea, sizeof(scb_stats->ea));
				break;
			}
		} else {
			scb_idx = dlystats->scb_cnt++;
			if (MAX_TXDELAY_STATS_SCBS < dlystats->scb_cnt) {
				dlystats->scb_cnt = MAX_TXDELAY_STATS_SCBS;
				dlystats->full_result = TXDELAY_STATS_PARTIAL_RESULT;
				break;
			}

			memcpy(scb_stats->dlystats, scb->delay_stats,
			       sizeof(*(scb->delay_stats)) * AC_COUNT);
			memcpy(&scb_stats->ea, &scb->ea, sizeof(scb_stats->ea));
		}
	}

	return ret;
}

void
wlc_dlystats_clear(wlc_info_t *wlc)
{
	struct scb *scb;
	struct scb_iter scbiter;

	FOREACHSCB(wlc->scbstate, &scbiter, scb) {
		memset(scb->delay_stats, 0, sizeof(*(scb->delay_stats)) * AC_COUNT);
	}
}

void
wlc_delay_stats_upd(scb_delay_stats_t *delay_stats, uint32 delay, uint tr, bool ack_recd)
{
	WLCNTSCBINCR(delay_stats->txmpdu_cnt[tr]);
	WLCNTSCBADD(delay_stats->delay_sum[tr], delay);
	if (delay_stats->delay_min == 0) {
		delay_stats->delay_min = delay;
		delay_stats->delay_max = delay;
	} else {
		if (delay < delay_stats->delay_min)
			delay_stats->delay_min = delay;
		else if (delay > delay_stats->delay_max)
			delay_stats->delay_max = delay;
	}
	WLCNTSCBINCR(delay_stats->delay_hist[MIN(delay/500, WLPKTDLY_HIST_NBINS-1)]);
	if (!ack_recd)
		WLCNTSCBINCR(delay_stats->txmpdu_lost);
}

#ifdef WLPKTDLYSTAT_IND
/** This function calculate avg txdly and trigger */
static void
wlc_delay_stats_timer(void *arg)
{
	struct scb *scb;
	struct scb_iter scbiter;
	wlc_perf_utils_t *pui = arg;
	wlc_info_t *wlc = pui->wlc;
	txdelay_event_t txdelay;
	int rssi;
	uint8 ac, i;
	uint32 sum_1s, total_1s, avg_1s;
	uint32 avg, delta;
	scb_delay_stats_t *delay_stats;
	txdelay_params_t *txdelay_params;
	char eabuf[ETHER_ADDR_STR_LEN];

	FOREACHSCB(wlc->scbstate, &scbiter, scb) {
		if (SCB_ASSOCIATED(scb)) {
			txdelay_params = scb->txdelay_params;

			txdelay_params->period += 1;
			txdelay_params->period %=
				pui->txdelay_params->period;

			if (txdelay_params->period)
				continue;

			ac = AC_VI;
			delay_stats = scb->delay_stats + ac;

			/* Calculate txdelay avg */
			total_1s = 0, sum_1s = 0;
			for (i = 0; i < RETRY_SHORT_DEF; i++) {
				total_1s += delay_stats->txmpdu_cnt[i];
				sum_1s += delay_stats->delay_sum[i];
			}
			/* total_1s or sum_1s variable overflow check */
			if (total_1s >= delay_stats->prev_txmpdu_cnt)
				total_1s -= delay_stats->prev_txmpdu_cnt;
			else
				total_1s += UINT32_MAX - delay_stats->prev_txmpdu_cnt;

			if (sum_1s >= delay_stats->prev_delay_sum)
				sum_1s -= delay_stats->prev_delay_sum;
			else
				sum_1s += UINT32_MAX - delay_stats->prev_delay_sum;

			/* Store current txmpdu count and delay_sum for next second */
			delay_stats->prev_txmpdu_cnt += total_1s;
			delay_stats->prev_delay_sum += sum_1s;

			if (total_1s != 0) {
				avg_1s = sum_1s / total_1s;
				/* for the first calculation */
				if (delay_stats->delay_avg == 0) {
					delay_stats->delay_count = TXDELAY_MIN_SAMPLE_DATA - 1;
					delay_stats->delay_avg = avg_1s;
					continue;
				}

				avg = delay_stats->delay_avg;
				delta = (avg * pui->txdelay_params->ratio)/100;

				/* Collect SCB rssi */
				wlc_lq_sample_req_enab(scb, RX_LQ_SAMP_REQ_WLC, TRUE);
				rssi = wlc_lq_rssi_get(wlc, SCB_BSSCFG(scb), scb);

				if (pui->txdelay_params->tune) {
					bcm_ether_ntoa(&scb->ea, eabuf);
					WL_ERROR(("TXDLY:[%s] rssi=%d avg_1s(%5d)-avg(%d)=%d "
						"(avg*ratio)/100=%d\n", eabuf, rssi,
						avg_1s, avg, avg_1s - avg, delta));
				}
				/* Collect stats for atleast delay_count times before next event */
				if (delay_stats->delay_count) {
					delay_stats->delay_count--;
					delay_stats->delay_avg =
					(delay_stats->delay_avg *
					(TXDELAY_MIN_SAMPLE_DATA - 1))/ TXDELAY_MIN_SAMPLE_DATA +
					avg_1s / TXDELAY_MIN_SAMPLE_DATA;
					continue;
				}
				avg = delay_stats->delay_avg;
				delta = (avg * pui->txdelay_params->ratio)/100;

				/* Check if need to trigger event */
				if ((avg_1s > avg) && ((avg_1s - avg) > delta)) {
					txdelay_params->cnt++;

					if (txdelay_params->cnt >=
						pui->txdelay_params->cnt) {
						if (pui->txdelay_params->tune)
							WL_ERROR(("TXDLY:Event Triggered\n"));

						txdelay_params->cnt = 0;

						/* Issue PKT TX Delay Stats Event */
						memset(&txdelay, 0, sizeof(txdelay_event_t));
						memcpy(&(txdelay.delay_stats), scb->delay_stats,
						       sizeof(*(scb->delay_stats)) * AC_COUNT);

						(void)wlc_lq_chanim_stats_get(wlc->chanim_info,
							wlc->home_chanspec, &txdelay.chanim_stats);
						txdelay.rssi = rssi;

						wlc_bss_mac_event(wlc, scb->bsscfg,
							WLC_E_PKTDELAY_IND, &scb->ea, 0, 0, 0,
							&txdelay, sizeof(txdelay_event_t));
						WL_INFORM(("WLC_E_PKTDELAY_IND:Event Triggered\n"));

						delay_stats->delay_avg = 0;
						delay_stats->delay_count =
							TXDELAY_MIN_SAMPLE_DATA - 1;
					}
				}
				else
					txdelay_params->cnt = 0;
				delay_stats->delay_avg =
					(delay_stats->delay_avg * (TXDELAY_MIN_SAMPLE_DATA - 1))/
					TXDELAY_MIN_SAMPLE_DATA + avg_1s / TXDELAY_MIN_SAMPLE_DATA;
			}
		}
	}
} /* wlc_delay_stats_timer */
#endif /* WLPKTDLYSTAT_IND */
#endif /* WLPKTDLYSTAT */