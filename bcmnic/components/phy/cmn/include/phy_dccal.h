/*
 * dccal module internal interface (to other PHY modules).
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
 * $Id: phy_dccal.h 606042 2015-12-14 06:21:23Z jqliu $
 */

#ifndef _phy_dccal_h_
#define _phy_dccal_h_

#include <typedefs.h>
#include <phy_api.h>

/* forward declaration */
typedef struct phy_dccal_info phy_dccal_info_t;

/* attach/detach */
phy_dccal_info_t *phy_dccal_attach(phy_info_t *pi);
void phy_dccal_detach(phy_dccal_info_t *dccali);

#endif /* _phy_dccal_h_ */