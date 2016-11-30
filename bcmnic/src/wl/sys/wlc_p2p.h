/*
 * P2P related header file
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
 * $Id: wlc_p2p.h 628025 2016-03-29 12:47:19Z $
*/


#ifndef _wlc_p2p_h_
#define _wlc_p2p_h_

/* TODO: include header files containing referenced data types here... */

/*
 * Macros to check if P2P is active.
 * P2P Active means we have bsscfg with p2p related flags set.
 */
#define P2P_ACTIVE(wlc)	wlc_p2p_active((wlc)->p2p)

/* Check if the suppress is due to Absence */
#ifdef WLP2P
#define P2P_ABS_SUPR(wlc, supr)	(P2P_ENAB((wlc)->pub) && (supr) == TX_STATUS_SUPR_NACK_ABS)
#else
#define P2P_ABS_SUPR(wlc, supr)	(0)
#endif

/* Generic WFDS service name */
#define	P2P_GEN_WFDS_SVC_NAME	"org.wi-fi.wfds"
#define	P2P_GEN_WFDS_SVC_NAME_LEN	14


/* APIs */
#ifdef WLP2P

/* tsf change notification */
typedef struct {
	wlc_bsscfg_t *cfg;
	wl_p2p_sched_desc_t	*desc;
	wl_p2p_sched_desc_t	cur;
	uint16			flags;
	uint8		cnt;
} wlc_p2p_noa_upd_data_t;
typedef struct {
	wlc_bsscfg_t *cfg;
	wl_p2p_sched_desc_t	desc;
	bool		is_go_rfact_avail;
} wlc_p2p_noa_desc_upd_data_t;

typedef void (*wlc_p2p_noa_upd_fn_t)(void *ctx, wlc_p2p_noa_upd_data_t *notif_data);
typedef void (*wlc_p2p_noa_desc_upd_fn_t)(void *ctx, wlc_p2p_noa_desc_upd_data_t *notif_data);
extern int wlc_p2p_noa_upd_register(wlc_p2p_info_t *pm,
	wlc_p2p_noa_upd_fn_t cb, void *arg);
extern int wlc_p2p_noa_upd_unregister(wlc_p2p_info_t *pm,
	wlc_p2p_noa_upd_fn_t cb, void *arg);
extern int wlc_p2p_noa_desc_upd_register(wlc_p2p_info_t *pm,
	wlc_p2p_noa_desc_upd_fn_t cb, void *arg);
extern int wlc_p2p_noa_desc_upd_unregister(wlc_p2p_info_t *pm,
	wlc_p2p_noa_desc_upd_fn_t cb, void *arg);


#ifdef BCMCOEXNOA
/* These are wrapper calls for wlc_cxnoa.c/h module to call into p2p */
extern bool wlc_p2p_cxnoa_bss_status(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg);
extern void wlc_p2p_cxnoa_upd(wlc_info_t *wlc, wlc_bsscfg_t *cfg);

#endif /* BCMCOEXNOA */
extern wlc_p2p_info_t *wlc_p2p_attach(wlc_info_t *wlc);
extern void wlc_p2p_detach(wlc_p2p_info_t *pm);
extern bool wlc_p2p_active(wlc_p2p_info_t *pm);
extern bool wlc_p2p_cap(wlc_p2p_info_t *pm);
extern void wlc_p2p_fixup_SSID(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wlc_ssid_t *ssid);
extern int wlc_p2p_write_ie_quiet_len(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, uint type);
extern int wlc_p2p_write_ie_quiet(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, uint type, uint8 *buf);
extern void wlc_p2p_rateset_filter(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wlc_rateset_t *rs);
extern int wlc_p2p_recv_process_beacon(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	struct dot11_bcn_prb *bcn, int bcn_len);
extern int wlc_p2p_recv_parse_bcn_prb(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	bool beacon, wlc_rateset_t *rs, uint8 *body, int body_len);
extern bool wlc_p2p_recv_process_prbreq(wlc_p2p_info_t *pm, struct dot11_management_header *hdr,
	uint8 *body, int body_len, wlc_d11rxhdr_t *wrxh, uint8 *plcp, bool sta_only);
extern void wlc_p2p_recv_process_prbresp(wlc_p2p_info_t *pm, uint8 *body, int body_len);
extern void wlc_p2p_sendprobe(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, void *p);
extern bool wlc_p2p_ssid_match(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	uint8 *ref_SSID, uint ref_SSID_len, uint8 *SSID, uint SSID_len);
extern int wlc_p2p_process_assocreq(wlc_p2p_info_t *pm, struct scb *scb,
	uint8 *tlvs, int tlvs_len);
extern int wlc_p2p_process_assocresp(wlc_p2p_info_t *pm, struct scb *scb,
	uint8 *tlvs, int tlvs_len);
extern void wlc_p2p_process_action(wlc_p2p_info_t *pm,
	struct dot11_management_header *hdr, uint8 *body, int body_len);
extern void wlc_p2p_process_public_action(wlc_p2p_info_t *pm,
	struct dot11_management_header *hdr, uint8 *body, int body_len);
extern int wlc_p2p_ops_set(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wl_p2p_ops_t *ops);
extern bool wlc_p2p_noa_valid(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg);
extern bool wlc_p2p_ops_valid(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg);
extern int wlc_p2p_noa_set(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	wl_p2p_sched_t *s, int slen);
extern void wlc_p2p_enab_upd(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg);
extern void wlc_p2p_build_noa(wlc_p2p_info_t *pm, wl_p2p_sched_desc_t *noa,
	uint32 start, uint32 duration, uint32 interval, uint32 count);
typedef void (*wlc_p2p_noa_cb_t)(wlc_info_t *wlc, uint txs, void *arg);
extern int wlc_p2p_send_presence_req(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	wl_p2p_sched_desc_t *noa, const struct ether_addr *da,
	wlc_p2p_noa_cb_t fn, void *arg);
extern bool wlc_p2p_send_noa(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	wl_p2p_sched_desc_t *noa, const struct ether_addr *da,
	wlc_p2p_noa_cb_t fn, void *arg);
extern void wlc_p2p_adopt_bss(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wlc_bss_info_t *bi);
extern void wlc_p2p_reset_bss(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg);
extern void wlc_p2p_prep_bss(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg);
extern void wlc_p2p_apsd_retrigger_upd(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, bool retrigger);
extern void wlc_p2p_pspoll_resend_upd(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, bool resend);
extern bool wlc_p2p_go_scb_timeout(wlc_p2p_info_t *pm);
extern void wlc_p2p_go_scb_timeout_set(wlc_p2p_info_t *pm, uint timeout);
#ifdef WLRSDB
extern wlc_bsscfg_t *wlc_p2p_rsdb_disc_config(wlc_p2p_info_t *pm);
#endif /* WLRSDB */
#ifdef WLMCHAN
extern int wlc_p2p_mchan_noa_set(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	wl_p2p_sched_t *s, int slen);
#endif /* WLMCHAN */

#ifdef ROBUST_DISASSOC_TX
extern bool wlc_p2p_get_noa_status(wlc_info_t *wlc, wlc_bsscfg_t *cfg);
#endif /* ROBUST_DISASSOC_TX */
#else	/* !WLP2P */

#define wlc_p2p_attach(a) NULL
#define	wlc_p2p_detach(a) do {} while (0)
#define wlc_p2p_active(wlc) FALSE

#endif /* !WLP2P */

#endif /* _wlc_p2p_h_ */