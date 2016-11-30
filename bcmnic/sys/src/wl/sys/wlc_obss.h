/*
 * Out of Band
 *
 * Broadcom 802.11 Networking Device Driver
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
 * $Id: wlc_obss.h 596126 2015-10-29 19:53:48Z $
 */


#ifndef _wlc_obss_h_
#define _wlc_obss_h_

/* for coex defs */
#include <wlioctl.h>

/* global OBSS Coex info */
#define OBSS_CHANVEC_SIZE	CEIL(CH_MAX_2G_CHANNEL + 1, NBBY)
/* OBSS Coexistence support */
#ifdef WLCOEX
#define COEX_ENAB(wlc) ((wlc)->pub->_coex != OFF)
#define COEX_ACTIVE(obss, cfg) (wlc_obss_cfg_get_coex_enab((obss), (cfg)))
#else
#define COEX_ENAB(pub) 0
#define COEX_ACTIVE(obss, cfg) ((void)(cfg), 0)
#endif /* WLCOEX */
#define IS_SCAN_COEX_ENAB(wlc)	COEX_ENAB(wlc)
#define IS_SCAN_COEX_ACTIVE(obss, cfg) COEX_ACTIVE((obss), (cfg))
#define WLC_INTOL40_DET(wlc, cfg) ((wlc_obss_cfg_get_coex_det((wlc)->obss, (cfg)) & \
	WL_COEX_40MHZ_INTOLERANT) != 0)
#define WLC_COEX_STATE_BITS(bit) (bit & (WL_COEX_40MHZ_INTOLERANT | WL_COEX_WIDTH20))

extern wlc_obss_info_t *wlc_obss_attach(wlc_info_t *wlc);
extern void wlc_obss_detach(wlc_obss_info_t *obss);

/* Coex and obss */
extern void wlc_ht_update_coex_support(wlc_info_t *wlc, int8 setting);

extern void wlc_recv_public_coex_action(wlc_info_t *wlc, wlc_bsscfg_t *bsscfg,
	struct dot11_management_header *hdr,
	uint8 *body, int body_len, wlc_d11rxhdr_t *wrxh);

extern void wlc_obss_update_scbstate(wlc_obss_info_t *obss, wlc_bsscfg_t *bsscfg,
	obss_params_t *obss_params);

#ifdef WL11N
extern bool wlc_obss_cfg_get_coex_enab(wlc_obss_info_t *obss, wlc_bsscfg_t *cfg);
extern uint8 wlc_obss_cfg_get_coex_det(wlc_obss_info_t *obss, wlc_bsscfg_t *cfg);
void wlc_obss_coex_checkadd_40intol(wlc_obss_info_t *obss, wlc_bsscfg_t *cfg,
	bool is2G, uint16* cap);
extern bool wlc_obss_scan_fields_valid(wlc_obss_info_t *obss, wlc_bsscfg_t *cfg);
extern bool wlc_obss_is_scan_complete(wlc_obss_info_t *obss, wlc_bsscfg_t *cfg, bool status,
	int act_time, int pass_time);
extern void wlc_obss_scan_update_countdown(wlc_obss_info_t *obss, wlc_bsscfg_t *cfg,
	uint8* vec, int chansz);
#else
#define wlc_obss_cfg_get_coex_enab(a, b) (BCM_REFERENCE(b), FALSE)
#define wlc_obss_cfg_get_coex_det(a, b) 0
#define wlc_obss_coex_checkadd_40intol(a, b, c, d)
#define wlc_obss_scan_fields_valid(a, b) TRUE
#define wlc_obss_is_scan_complete(a, b, c, d, e) TRUE
#define wlc_obss_scan_update_countdown(a, b, c, d)
#endif /* WL11N */

#ifdef AP
extern void wlc_ht_coex_update_permit(wlc_bsscfg_t *cfg, bool permit);
extern void wlc_ht_coex_update_fid_time(wlc_bsscfg_t *cfg);
#else
#define wlc_ht_coex_update_fid_time(a) do {} while (0)
#define wlc_ht_coex_update_permit(a, b) do {} while (0)
#endif /* AP */

#ifdef WL11N
extern void wlc_ht_obss_scan_reset(wlc_bsscfg_t *cfg);
#else
#define wlc_ht_obss_scan_reset(a)	do {} while (0)
#endif

#endif /* _wlc_obss_h_ */