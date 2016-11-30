/*
 * VCO CAL module internal interface - iovar table registration
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
 * $Id: phy_vcocal_iov.h 603319 2015-12-01 21:41:40Z vyass $
 */

#ifndef _phy_vcocal_iov_t_
#define _phy_vcocal_iov_t_

#include <phy_api.h>

/* register iovar table/handlers */
int phy_vcocal_register_iovt(phy_info_t *pi, wlc_iocv_info_t *ii);

#endif /* _phy_vcocal_iov_t_ */