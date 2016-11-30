/*
 * 802.11v definitions for
 * Broadcom 802.11abgn Networking Device Driver
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
 * $Id: wlc_wnm.h 645240 2016-06-23 06:03:18Z $
 */


#ifndef _wlc_wnm_h_
#define _wlc_wnm_h_

#define WNM_BSSTRANS_ENABLED(mask)	((mask & WL_WNM_BSSTRANS)? TRUE: FALSE)
#define WNM_PROXYARP_ENABLED(mask)	((mask & WL_WNM_PROXYARP)? TRUE: FALSE)
#define WNM_TIMBC_ENABLED(mask)		((mask & WL_WNM_TIMBC)? TRUE: FALSE)
#define WNM_MAXIDLE_ENABLED(mask)	((mask & WL_WNM_MAXIDLE)? TRUE: FALSE)
#define WNM_TFS_ENABLED(mask)		((mask & WL_WNM_TFS)? TRUE: FALSE)
#define WNM_SLEEP_ENABLED(mask)		((mask & WL_WNM_SLEEP)? TRUE: FALSE)
#define WNM_DMS_ENABLED(mask)		((mask & WL_WNM_DMS)? TRUE: FALSE)
#define WNM_FMS_ENABLED(mask)		((mask & WL_WNM_FMS)? TRUE: FALSE)
#define WNM_NOTIF_ENABLED(mask)		((mask & WL_WNM_NOTIF)? TRUE: FALSE)

#define SCB_PROXYARP(cap)		((cap & WL_WNM_PROXYARP)? TRUE: FALSE)
#define SCB_TFS(cap)			((cap & WL_WNM_TFS)? TRUE: FALSE)
#define SCB_WNM_SLEEP(cap)		((cap & WL_WNM_SLEEP)? TRUE: FALSE)
#define SCB_TIMBC(cap)			((cap & WL_WNM_TIMBC)? TRUE: FALSE)
#define SCB_BSSTRANS(cap)		((cap & WL_WNM_BSSTRANS)? TRUE: FALSE)
#define SCB_DMS(cap)			((cap & WL_WNM_DMS)? TRUE: FALSE)
#define SCB_FMS(cap)			((cap & WL_WNM_FMS)? TRUE: FALSE)
#define WBTEXT_SCOREDELTA_PROF 0  /* profile where score delta percentage available */
#define WBTEXT_ALLOWED_PROFS 2  /* allowed profiles in WBTEXT */

extern wlc_wnm_info_t *wlc_wnm_attach(wlc_info_t *wlc);
extern void wlc_wnm_detach(wlc_wnm_info_t *wnm);
extern void wlc_frameaction_wnm(wlc_wnm_info_t *wnm, uint action_id,
	struct dot11_management_header *hdr, uint8 *body, int body_len,
	int8 rssi, ratespec_t rspec);
extern int wlc_wnm_recv_process_wnm(wlc_wnm_info_t *wnm, wlc_bsscfg_t *bsscfg,
	uint action_id, struct scb *scb, struct dot11_management_header *hdr,
	uint8 *body, int body_len);
extern void wlc_wnm_recv_process_uwnm(wlc_wnm_info_t *wnm, wlc_bsscfg_t *bsscfg,
	uint action_id, struct scb *scb, struct dot11_management_header *hdr,
	uint8 *body, int body_len);

extern int wlc_wnm_get_trans_candidate_list_pref(wlc_info_t *wlc, wlc_bsscfg_t *cfg,
	struct ether_addr *bssid);
extern void wlc_wnm_scb_cleanup(wlc_info_t *wlc, struct scb *scb);
extern void wlc_wnm_scb_assoc(wlc_info_t *wlc, struct scb *scb);
extern uint32 wlc_wnm_get_cap(wlc_info_t *wlc, wlc_bsscfg_t *bsscfg);
extern void wlc_wnm_set_cap(wlc_info_t *wlc, wlc_bsscfg_t *bsscfg, uint32 cap);
extern uint32 wlc_wnm_get_scbcap(wlc_info_t *wlc, struct scb *scb);
extern uint32 wlc_wnm_set_scbcap(wlc_info_t *wlc, struct scb *scb, uint32 cap);
extern uint16 wlc_wnm_maxidle(wlc_info_t *wlc, wlc_bsscfg_t *cfg);
extern int wlc_wnm_packets_handle(wlc_bsscfg_t *bsscfg, void *p, bool istx);
extern bool wlc_wnm_pkt_chainable(wlc_wnm_info_t *wnm, wlc_bsscfg_t *bsscfg);

#ifdef WLWNM_AP
/* WNM packet handler */
extern bool wlc_wnm_bss_idle_opt(wlc_info_t *wlc, wlc_bsscfg_t *cfg);
extern void wlc_wnm_rx_tstamp_update(wlc_info_t *wlc, struct scb *scb);
extern bool wlc_wnm_timbc_req_ie_process(wlc_info_t *wlc, uint8 *tlvs, int len, struct scb *scb);
extern int wlc_wnm_scb_timbc_status(wlc_info_t *wlc, struct scb *scb);
extern void wlc_wnm_tbtt(wlc_info_t *wlc, wlc_bsscfg_t *bsscfg);
extern void wlc_wnm_tttt(wlc_info_t *wlc);
extern int wlc_wnm_scb_sm_interval(wlc_info_t *wlc, struct scb *scb);
extern bool wlc_wnm_dms_amsdu_on(wlc_info_t *wlc, wlc_bsscfg_t *cfg);
extern void wlc_wnm_dms_spp_conflict(wlc_info_t *wlc, struct scb *scb);
#ifdef MFP
extern void wlc_wnm_sleep_key_update(wlc_info_t *wlc, wlc_bsscfg_t *cfg);
#endif
#endif /* WLWNM_AP */

/* debugging... */
#ifdef EVENT_LOG_COMPILE
#define WL_WNM_BSSTRANS_LOG(str, p1, p2, p3, p4) \
	EVENT_LOG(EVENT_LOG_TAG_WNM_BSSTRANS_INFO, str, p1, p2, p3, p4)
#else /* EVENT_LOG_COMPILE */
#define WL_WNM_BSSTRANS_LOG(str, p1, p2, p3, p4)
#endif /* EVENT_LOG_COMPILE */

/* debugging... */
#ifdef WBTEXTDBG
#define WBTEXT_INFO(args) printf args
#else /* WBTEXTDBG */
#define WBTEXT_INFO(args) WL_ERROR(args)
#endif /* WBTEXTDBG */

#ifdef STA

extern int wlc_wnm_timbc_resp_ie_process(wlc_info_t *, dot11_timbc_resp_ie_t*, int, struct scb*);
extern uint8 *wlc_wnm_timbc_assoc(wlc_info_t *wlc, wlc_bsscfg_t *bsscfg, uint8 *pbody);
extern void wlc_wnm_check_dms_req(wlc_info_t *wlc, struct ether_addr *ea);
extern bool wlc_wnm_bsstrans_roamscan_complete(wlc_info_t *wlc, wlc_bsscfg_t *bsscfg, uint8 status,
struct ether_addr *trgt_bssid);
extern int wlc_wnm_enter_sleep_mode(wlc_bsscfg_t *cfg);
extern int wlc_wnm_update_sleep_mode(wlc_bsscfg_t *cfg, bool intim);
#endif /* STA */

#define WNM_DROP	0
#define WNM_NOP		1
#define WNM_TAKEN	2

extern int
wlc_wnm_bss_pref_score_rssi(wlc_bsscfg_t *cfg, wlc_bss_info_t *bi, int8 rssi, uint32 *score);
extern void
wlc_wnm_process_join_trgts_bsstrans(wlc_wnm_info_t *wnm, wlc_bsscfg_t *cfg, wlc_bss_info_t **bip,
	int trgt_count);
extern bool wlc_wnm_bsstrans_zero_assoc_bss_score(wlc_wnm_info_t *wnm, wlc_bsscfg_t *cfg);
extern void wlc_wnm_bsstrans_reset_pending_join(wlc_info_t *wlc, wlc_bsscfg_t *cfg);
extern uint32 wlc_wnm_bsstrans_get_scoredelta(wlc_wnm_info_t *wnm, wlc_bsscfg_t *cfg);
extern void wlc_wnm_bsstrans_update_scoredelta(wlc_wnm_info_t *wnm,
		wlc_bsscfg_t *cfg, uint32 scoredelta);
extern int8 wlc_wnm_btm_get_rssi_thresh(wlc_wnm_info_t *wnm, wlc_bsscfg_t *cfg);
extern bool wlc_wnm_bsstrans_is_product_policy(wlc_wnm_info_t *wnm);

extern void wlc_wnm_bssload_calc_reset(wlc_bsscfg_t *cfg);
extern void wlc_wnm_set_cu_trigger_percent(wlc_info_t *wlc, wlc_bsscfg_t *cfg, uint8 cu_trigger);
extern int wlc_wnm_get_cu_trigger_percent(wlc_info_t *wlc, wlc_bsscfg_t *cfg);
extern void wlc_wnm_set_cu_avg_calc_dur(wlc_info_t *wlc, wlc_bsscfg_t *cfg, uint8 cu_calc_dur);

extern bool wlc_wnm_bsstrans_roam_required(wlc_info_t *wlc, wlc_bsscfg_t *cfg, int *reason);
extern bool wlc_wnm_bsstrans_check_for_roamthrash(wlc_info_t *wlc, wlc_bsscfg_t *cfg);
extern void wlc_wnm_bsstrans_print_score(wlc_wnm_info_t *wnm, wlc_bsscfg_t *cfg,
	wlc_bss_info_t *bi, uint32 weight);
extern void wlc_wnm_update_nonnbr_bestscore(wlc_wnm_info_t *wnm, wlc_bsscfg_t *cfg,
	wlc_bss_info_t *bsi, uint32 score);
extern bool wlc_wnm_is_blacklisted_bss(wlc_wnm_info_t *wnm, wlc_bsscfg_t *cfg, wlc_bss_info_t *bip);
extern bool wlc_wnm_update_join_pref_score(wlc_wnm_info_t *wnm, wlc_bsscfg_t *cfg,
	wlc_bss_list_t *join_targets, void *join_pref_list, uint32 current_score);
int wlc_wnm_notif_req_send(wlc_wnm_info_t *wnm, wlc_bsscfg_t *bsscfg, struct scb *scb,
	uint8 type, uint8 *sub_elem_buf, uint16 se_buf_len);
#endif	/* _wlc_wnm_h_ */