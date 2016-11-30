/*
 * Monitor moude interface
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
 * $Id: wlc_monitor.h 599296 2015-11-13 06:36:13Z $
 */


#ifndef _WLC_MONITOR_H_
#define _WLC_MONITOR_H_

#define MONITOR_PROMISC_ENAB(_ctxt_) \
	wlc_monitor_get_mctl_promisc_bits((_ctxt_))

extern wlc_monitor_info_t *wlc_monitor_attach(wlc_info_t *wlc);
extern void wlc_monitor_detach(wlc_monitor_info_t *ctxt);
extern void wlc_monitor_promisc_enable(wlc_monitor_info_t *ctxt, bool enab);
extern uint32 wlc_monitor_get_mctl_promisc_bits(wlc_monitor_info_t *ctxt);
extern void wlc_monitor_phy_cal(wlc_monitor_info_t *ctx, bool enable);
extern void wlc_monitor_phy_cal_timer_start(wlc_monitor_info_t *ctxt, uint32 tm);
#ifdef WLTXMONITOR
extern void wlc_tx_monitor(wlc_info_t *wlc, d11txh_t *txh, tx_status_t *txs, void *p,
                    struct wlc_if *wlcif);
#endif

#endif /* _WLC_MONITOR_H_ */