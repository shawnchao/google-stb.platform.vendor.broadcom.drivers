/*
 * PHY Core module internal interface - used by high driver.
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
 * $Id: phy_type_disp_high.h 583048 2015-08-31 16:43:34Z jqliu $
 */

#ifndef _phy_type_disp_high_h_
#define _phy_type_disp_high_h_

#include <typedefs.h>

#include <wlc_iocv_types.h>

/*
 * Register PHY type specific iovar tables/handlers to IOC.
 *
 * Return BCME_OK when all registrations are successfully done; BCME_XXXX otherwise.
 */
int phy_type_high_register_iovt(uint phytype, wlc_iocv_info_t *ii);

/*
 * Register PHY type specific ioctl tables/handlers to IOC.
 *
 * Return BCME_OK when all registrations are successfully done; BCME_XXXX otherwise.
 */
int phy_type_high_register_ioct(uint phytype, wlc_iocv_info_t *ii);

#endif /* _phy_type_disp_high_h_ */
