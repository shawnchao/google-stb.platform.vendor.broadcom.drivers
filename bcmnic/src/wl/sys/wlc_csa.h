/*
 * 802.11h CSA module header file
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
 * $Id: wlc_csa.h 633968 2016-04-26 09:01:47Z $
*/

/**
 * Channel Switch Announcement and Extended Channel Switch Announcement
 * Related to radar avoidance
 */


#ifndef _wlc_csa_h_
#define _wlc_csa_h_

/* APIs */
#ifdef WLCSA

/* module */
extern wlc_csa_info_t *wlc_csa_attach(wlc_info_t *wlc);
extern void wlc_csa_detach(wlc_csa_info_t *csam);

/* recv/send */
extern void wlc_recv_public_csa_action(wlc_csa_info_t *csam,
	struct dot11_management_header *hdr, uint8 *body, int body_len);
extern void wlc_recv_csa_action(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg,
	struct dot11_management_header *hdr, uint8 *body, int body_len);
extern void wlc_recv_ext_csa_action(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg,
	struct dot11_management_header *hdr, uint8 *body, int body_len);

extern int wlc_send_action_switch_channel(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg);

/* actions */
extern void wlc_csa_do_switch(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg,
	chanspec_t chspec);
extern void wlc_csa_count_down(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg);
extern void wlc_csa_reset_all(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg);
extern void wlc_csa_do_csa(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg,
	wl_chan_switch_t *cs, bool docs);

#ifdef AP
extern int wlc_csa_do_channel_switch(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg,
	chanspec_t chanspec, uint8 mode, uint8 count, uint8 reg_class, uint8 frame_type);
#endif /* AP */

/* IE build/parse */
#ifdef WL11AC
extern uint8 *wlc_csa_write_chan_switch_wrapper_ie(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg,
	uint8 *cp, int buflen);
#endif /* WL11AC */

extern bool wlc_csa_quiet_mode(wlc_csa_info_t *csam, uint8 *tag, uint tag_len);

/* accessors */
extern uint8 wlc_csa_get_csa_count(wlc_csa_info_t *csam, wlc_bsscfg_t *cfg);

#else /* !WLCSA */

#define wlc_csa_attach(wlc) NULL
#define wlc_csa_detach(csam) do {} while (0)

#define wlc_recv_public_csa_action(csam, hdr, body, body_len) do {} while (0)
#define wlc_recv_csa_action(csam, cfg, hdr, body, body_len) do {} while (0)
#define wlc_recv_ext_csa_action(csam, cfg, hdr, body, body_len) do {} while (0)

#define wlc_send_action_switch_channel(csam, cfg) do {} while (0)

#define wlc_csa_do_switch(csam, cfg, chspec) do {} while (0)
#define wlc_csa_count_down(csam, cfg) do {} while (0)
#define wlc_csa_reset_all(csam, cfg) do {} while (0)
#define wlc_csa_do_csa(csam, cfg, cs, docs) do {} while (0)

#define wlc_csa_write_chan_switch_wrapper_ie(csam, cfg, cp, buflen) (cp)

#define wlc_csa_quiet_mode(csam, tag, tag_len) FALSE

#define wlc_csa_get_csa_count(csam, cfg) 0

#endif /* !WLCSA */

#endif /* _wlc_csa_h_ */
