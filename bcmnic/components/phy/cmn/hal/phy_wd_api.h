/*
 * WatchDog module public interface (to MAC driver).
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
 * $Id: phy_wd_api.h 583048 2015-08-31 16:43:34Z jqliu $
 */

#ifndef _phy_wd_api_h_
#define _phy_wd_api_h_

#include <typedefs.h>
#include <phy_api.h>

int phy_watchdog(phy_info_t *pi);

#endif /* _phy_wd_api_h_ */