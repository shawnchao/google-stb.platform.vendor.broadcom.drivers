/*
 * Proxy STA interface
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
 * $Id: wlc_psta.h 619444 2016-02-16 22:31:00Z $
 */


#ifndef _WLC_PSTA_H_
#define _WLC_PSTA_H_

/* Max PSTA assoc limits exclude the default primary assoc. One entry in
 * bottom half of the skl and key tables is accounted for the primary's
 * pairwise keys and one in top half is used for storing the broadcast key.
 */
#ifndef MAXVSLAVEDEVS
#define MAXVSLAVEDEVS	0
#endif

#if MAXVSLAVEDEVS > 0 && MAXVSLAVEDEVS < (RCMTA_SIZE / 2)
#define	PSTA_PROXY_MAX_ASSOC	(MAXVSLAVEDEVS)
#define	PSTA_RPT_MAX_ASSOC	(MAXVSLAVEDEVS - 1) /* reserve for AP(wlx.1) mac */
#else
#define	PSTA_PROXY_MAX_ASSOC	((RCMTA_SIZE / 2) - 1)
#define	PSTA_RPT_MAX_ASSOC	((RCMTA_SIZE / 2) - 1)
#endif /* MAXVSLAVEDEVS > 0 */

#define PSTA_MAX_ASSOC(wlc)	(PSTA_IS_REPEATER(wlc) ? PSTA_RPT_MAX_ASSOC : \
				                         PSTA_PROXY_MAX_ASSOC)

/* RCMTA engine can have a maximum of 50 entries. Reserve slot 25 for the
 * primary. Slots 26 to 49 are available for use by PSTA RAs. Slots 0 to 24
 * are available for use for the downstream STAs TAs.
 */
#define PSTA_TA_STRT_INDX	0
#define PSTA_RA_PRIM_INDX	(RCMTA_SIZE / 2)
#define PSTA_RA_STRT_INDX	((RCMTA_SIZE / 2) + 1)

typedef enum {
	/* Store current PSTA mode and switch
	 * to DWDS in case if the
	 * corresponding conditions are
	 * satisfied.
	 */
	PSTA_MODE_UPDATE_ACTION_SAVE = 0,
	/* If there is previously saved mode
	 * then restore it
	 */
	PSTA_MODE_UPDATE_ACTION_RESTORE = 1
} psta_mode_update_action_t;

/* Initialize psta private context.It returns a pointer to the
 * psta private context if succeeded. Otherwise it returns NULL.
 */
extern wlc_psta_info_t *wlc_psta_attach(wlc_info_t *wlc);

/* Cleanup psta private context */
extern void wlc_psta_detach(wlc_psta_info_t *psta);

/* Process frames in transmit direction */
extern int32 wlc_psta_send_proc(wlc_psta_info_t *psta, void **p, wlc_bsscfg_t **cfg);

/* Process frames in receive direction */
extern void wlc_psta_recv_proc(wlc_psta_info_t *psta, void *p, struct ether_header *eh,
	wlc_bsscfg_t **cfg);

extern wlc_bsscfg_t *wlc_psta_find(wlc_psta_info_t *psta, uint8 *mac);

extern uint8 wlc_psta_rcmta_idx(wlc_psta_info_t *psta, wlc_bsscfg_t *cfg);

extern void wlc_psta_mode_update(wlc_psta_info_t *psta, wlc_bsscfg_t *cfg, struct ether_addr *addr,
	psta_mode_update_action_t action);

#ifdef DPSTA
extern bool wlc_psta_is_ds_sta(void *psta, struct ether_addr *mac);
extern bool wlc_psta_authorized(wlc_bsscfg_t *cfg);
extern wlc_bsscfg_t *wlc_psta_find_dpsta(void *psta, uint8 *mac);
extern void *wlc_psta_get_psta(wlc_info_t *wlc);
#endif /* DPSTA */
#endif	/* _WLC_PSTA_H_ */