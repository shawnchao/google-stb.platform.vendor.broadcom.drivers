/*
 * HT PHY RXIQ CAL module interface (to other PHY modules).
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
 * $Id: phy_ht_rxiqcal.h 639978 2016-05-25 16:03:11Z vyass $
 */

#ifndef _phy_ht_rxiqcal_h_
#define _phy_ht_rxiqcal_h_

#include <phy_api.h>
#include <phy_ht.h>
#include <phy_rxiqcal.h>

/* forward declaration */
typedef struct phy_ht_rxiqcal_info phy_ht_rxiqcal_info_t;

/* register/unregister ACPHY specific implementations to/from common */
phy_ht_rxiqcal_info_t *phy_ht_rxiqcal_register_impl(phy_info_t *pi,
	phy_ht_info_t *aci, phy_rxiqcal_info_t *mi);
void phy_ht_rxiqcal_unregister_impl(phy_ht_rxiqcal_info_t *info);

void wlc_phy_dig_lpf_override_htphy(phy_info_t *pi, uint8 dig_lpf_ht);
#endif /* _phy_ht_rxiqcal_h_ */
