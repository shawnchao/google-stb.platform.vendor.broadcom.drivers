/*
 * STA monitor interface
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
 * $Id: wlc_stamon.h 607839 2015-12-22 09:58:17Z $
 */

/** This is an AP/router specific feature. Twiki: [STASniffingModeOnAP] */

#ifndef _WLC_STAMON_H_
#define _WLC_STAMON_H_

#define STA_MONITORING(_wlc_, _ea_) \
	((wlc_stamon_sta_num((_wlc_)->stamon_info) > 0) && \
	(wlc_stamon_sta_find((_wlc_)->stamon_info, (_ea_)) >= 0))

/*
 * Initialize sta monitor private context.
 * Returns a pointer to the sta monitor private context, NULL on failure.
 */
extern wlc_stamon_info_t *wlc_stamon_attach(wlc_info_t *wlc);
/* Cleanup sta monitor private context */
extern void wlc_stamon_detach(wlc_stamon_info_t *stamon_ctxt);

extern int wlc_stamon_sta_config(wlc_stamon_info_t *stamon_ctxt,
	wlc_stamon_sta_config_t* cfg);
extern int8 wlc_stamon_sta_find(wlc_stamon_info_t *stamon_ctxt, const struct ether_addr *ea);
extern int wlc_stamon_sta_sniff_enab(wlc_stamon_info_t *stamon_ctxt,
	struct ether_addr *ea, bool enab);
extern uint16 wlc_stamon_sta_num(wlc_stamon_info_t *stamon_ctxt);
extern void wlc_stamon_rxstamonucast_update(wlc_info_t *wlc, bool reset);
#ifdef ACKSUPR_MAC_FILTER
extern bool wlc_stamon_acksupr_is_duplicate(wlc_info_t *wlc, struct ether_addr *ea);
extern bool wlc_stamon_is_slot_reserved(wlc_info_t *wlc, int idx);
#endif /* ACKSUPR_MAC_FILTER */
#endif /* _WLC_STAMON_H_ */