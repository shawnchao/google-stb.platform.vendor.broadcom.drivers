/*
 * ACPHY TSSI Cal module interface (to other PHY modules).
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
 * $Id: phy_ac_tssical.h 647004 2016-07-01 15:56:22Z kriedte $
 */

#ifndef _phy_ac_tssical_h_
#define _phy_ac_tssical_h_

#include <phy_api.h>
#include <phy_ac.h>
#include <phy_tssical.h>

#define LOOPBACK_FOR_TSSICAL 0
#define LOOPBACK_FOR_IQCAL 1

/* forward declaration */
typedef struct phy_ac_tssical_info phy_ac_tssical_info_t;

/* register/unregister ACPHY specific implementations to/from common */
phy_ac_tssical_info_t *phy_ac_tssical_register_impl(phy_info_t *pi,
	phy_ac_info_t *aci, phy_tssical_info_t *cmn_info);
void phy_ac_tssical_unregister_impl(phy_ac_tssical_info_t *ac_info);
extern void wlc_phy_tssi_radio_setup_acphy_tiny(phy_info_t *pi, uint8 core_mask, uint8 for_iqcal);
extern void wlc_phy_tssi_radio_setup_acphy_28nm(phy_info_t *pi, uint8 for_iqcal);
extern void wlc_phy_tssi_radio_setup_acphy_20694(phy_info_t *pi, uint8 for_iqcal);
extern void wlc_phy_tssi_radio_setup_acphy_20696(phy_info_t *pi, uint8 for_iqcal);
extern int8 wlc_phy_tssivisible_thresh_acphy(phy_info_t *pi);
extern void wlc_phy_txpwrctrl_idle_tssi_meas_acphy(phy_info_t *pi);
extern void wlc_phy_tssi_phy_setup_acphy(phy_info_t *pi, uint8 for_iqcal);
extern void wlc_phy_tssi_radio_setup_acphy(phy_info_t *pi, uint8 core_mask, uint8 for_iqcal);
extern void wlc_phy_txpwrctrl_set_idle_tssi_acphy(phy_info_t *pi, int16 idle_tssi, uint8 core);
extern void phy_ac_tssi_loopback_path_setup(phy_info_t *pi, uint8 for_iqcal);
extern uint8 wlc_phy_ac_set_tssi_params_maj36(phy_info_t *pi);


#if defined(BCMINTERNAL) || defined(WLTEST)
extern int16 wlc_phy_test_tssi_acphy(phy_info_t *pi, int8 ctrl_type, int8 pwr_offs);
extern int16 wlc_phy_test_idletssi_acphy(phy_info_t *pi, int8 ctrl_type);
#endif /* BCMINTERNAL || WLTEST */

#ifdef WLC_TXCAL
extern uint16 wlc_phy_adjusted_tssi_acphy(phy_info_t *pi, uint8 core_num);
extern uint8 wlc_phy_apply_pwr_tssi_tble_chan_acphy(phy_info_t *pi);
#endif	/* WLC_TXCAL */

extern void
wlc_phy_set_tssisens_lim_acphy(phy_info_t *pi, uint8 override);

void phy_ac_tssical_idle(phy_info_t *pi);

#endif /* _phy_ac_tssical_h_ */