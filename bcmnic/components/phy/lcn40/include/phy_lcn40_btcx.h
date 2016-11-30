/*
 * LCN40PHY BT Coex module interface (to other PHY modules).
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
 * $Id: phy_lcn40_btcx.h 617885 2016-02-09 00:18:54Z jkoster $
 */

#ifndef _phy_lcn40_btcx_h_
#define _phy_lcn40_btcx_h_

#include <phy_api.h>
#include <phy_lcn40.h>
#include <phy_btcx.h>

/* forward declaration */
typedef struct phy_lcn40_btcx_info phy_lcn40_btcx_info_t;

/* register/unregister LCN40PHY specific implementations to/from common */
phy_lcn40_btcx_info_t *phy_lcn40_btcx_register_impl(phy_info_t *pi,
	phy_lcn40_info_t *lcn40i, phy_btcx_info_t *ci);
void phy_lcn40_btcx_unregister_impl(phy_lcn40_btcx_info_t *info);

#endif /* _phy_lcn40_btcx_h_ */
