/*
 * NPHY Core module implementation
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
 * $Id: phy_n_iovt.c 583048 2015-08-31 16:43:34Z jqliu $
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <phy_dbg.h>
#include <wlc_iocv_types.h>
#include <phy_n_tbl_iov.h>
#include "phy_type_n.h"
#include "phy_type_n_iovt.h"

/* local functions */

/* register iovar tables/handlers to IOC module */
int
BCMATTACHFN(phy_n_register_iovt)(phy_info_t *pi, phy_type_info_t *ti, wlc_iocv_info_t *ii)
{
	int err;

	if ((err = phy_n_tbl_register_iovt(pi, ii)) != BCME_OK) {
		PHY_ERROR(("%s: phy_n_tbl_register_iovt failed\n", __FUNCTION__));
		goto fail;
	}

	return BCME_OK;

fail:
	return err;
}
