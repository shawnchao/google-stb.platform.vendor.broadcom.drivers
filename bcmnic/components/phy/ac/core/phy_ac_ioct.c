/*
 * ACPHY Core module implementation
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
 * $Id: phy_ac_ioct.c 598405 2015-11-09 13:44:18Z sahud $
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <phy_dbg.h>
#include <wlc_iocv_types.h>
#include "phy_type_ac.h"
#include "phy_type_ac_ioct.h"

/* local functions */

/* register ioctl tables/handlers to IOC module */
int
BCMATTACHFN(phy_ac_register_ioct)(phy_info_t *pi, phy_type_info_t *ti, wlc_iocv_info_t *ii)
{
	BCM_REFERENCE(pi);
	BCM_REFERENCE(ti);
	BCM_REFERENCE(ii);
	return BCME_OK;
}
