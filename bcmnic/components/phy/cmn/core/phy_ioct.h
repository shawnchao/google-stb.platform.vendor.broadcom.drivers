/*
 * PHY Core internal interface (to other modules) - IOCtlTable registration.
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
 * $Id: phy_ioct.h 583048 2015-08-31 16:43:34Z jqliu $
 */

#ifndef _phy_ioct_h_
#define _phy_ioct_h_

#include <phy_api.h>

#include <wlc_iocv_types.h>

/* register all modules' ioctl tables/handlers */
int phy_register_ioct(phy_info_t *pi, wlc_iocv_info_t *ii);

#endif /* _phy_ioct_h_ */
