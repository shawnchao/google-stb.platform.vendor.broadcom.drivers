/*
 * RadarDetect module implementation - iovar table/handlers & registration
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
 * $Id: phy_radar_iov.c 619527 2016-02-17 05:54:49Z renukad $
 */

#include <phy_cfg.h>

#include <typedefs.h>
#include <bcmutils.h>
#include <bcmendian.h>

#include <phy_api.h>
#include "phy_radar_st.h"
#include <phy_radar_iov.h>

#include <wlc_iocv_types.h>
#include <wlc_iocv_reg.h>

#ifndef ALL_NEW_PHY_MOD
#include <wlc_phy_int.h>
#endif

/* id's */
enum {
	IOV_RADAR_ARGS = 1,
	IOV_RADAR_ARGS_40MHZ = 2,
	IOV_RADAR_THRS = 3,
	IOV_PHY_DFS_LP_BUFFER = 4,
	IOV_RADAR_STATUS = 5,
	IOV_CLEAR_RADAR_STATUS = 6
};

/* iovar table */
static const bcm_iovar_t phy_radar_iovt[] = {
	{"radarargs", IOV_RADAR_ARGS, (0), 0, IOVT_BUFFER, sizeof(wl_radar_args_t)},
	{"radarargs40", IOV_RADAR_ARGS_40MHZ, (0), 0, IOVT_BUFFER, sizeof(wl_radar_args_t)},
	{"radarthrs", IOV_RADAR_THRS, (IOVF_SET_UP), 0, IOVT_BUFFER, sizeof(wl_radar_thr_t)},
	{"radar_status", IOV_RADAR_STATUS, (0), 0, IOVT_BUFFER, sizeof(wl_radar_status_t)},
	{"clear_radar_status", IOV_CLEAR_RADAR_STATUS, (IOVF_SET_UP), 0, IOVT_BUFFER,
	sizeof(wl_radar_status_t)},
#if defined(BCMDBG) || defined(BCMINTERNAL) || defined(WLTEST)
	{"phy_dfs_lp_buffer", IOV_PHY_DFS_LP_BUFFER, 0, 0, IOVT_UINT8, 0},
#endif /* if defined(BCMDBG) || defined(BCMINTERNAL) || defined(WLTEST) */
	{NULL, 0, 0, 0, 0, 0}
};

#include <wlc_patch.h>

/* iovar handler */
static int
phy_radar_doiovar(void *ctx, uint32 aid, void *p, uint plen, void *a, uint alen, uint vsz,
	struct wlc_if *wlcif)
{
	phy_info_t *pi = (phy_info_t *)ctx;
	phy_radar_info_t *ri = pi->radari;
	phy_radar_st_t *st = phy_radar_get_st(ri);
	int err = BCME_OK;
	int int_val = 0;
	bool bool_val;

	/* The PHY type implemenation isn't registered */
	if (st == NULL) {
		PHY_ERROR(("%s: not supported\n", __FUNCTION__));
		return BCME_UNSUPPORTED;
	}

	if (plen >= (uint)sizeof(int_val))
		bcopy(p, &int_val, sizeof(int_val));

	/* bool conversion to avoid duplication below */
	bool_val = int_val != 0;

	switch (aid) {
	case IOV_GVAL(IOV_RADAR_ARGS):
		bcopy(&st->rparams.radar_args, a, sizeof(wl_radar_args_t));
		break;

	case IOV_SVAL(IOV_RADAR_THRS): {
		wl_radar_thr_t radar_thr;

		/* len is check done before gets here */
		bzero(&radar_thr, sizeof(wl_radar_thr_t));
		bcopy(p, &radar_thr, sizeof(wl_radar_thr_t));
		if (radar_thr.version != WL_RADAR_THR_VERSION) {
			err = BCME_VERSION;
			break;
		}
		st->rparams.radar_thrs.thresh0_20_lo = radar_thr.thresh0_20_lo;
		st->rparams.radar_thrs.thresh1_20_lo = radar_thr.thresh1_20_lo;
		st->rparams.radar_thrs.thresh0_20_hi = radar_thr.thresh0_20_hi;
		st->rparams.radar_thrs.thresh1_20_hi = radar_thr.thresh1_20_hi;
		if (ISNPHY(pi) || ISHTPHY(pi) || ISACPHY(pi)) {
			st->rparams.radar_thrs.thresh0_40_lo = radar_thr.thresh0_40_lo;
			st->rparams.radar_thrs.thresh1_40_lo = radar_thr.thresh1_40_lo;
			st->rparams.radar_thrs.thresh0_40_hi = radar_thr.thresh0_40_hi;
			st->rparams.radar_thrs.thresh1_40_hi = radar_thr.thresh1_40_hi;
		}
		if (ISACPHY(pi)) {
			st->rparams.radar_thrs.thresh0_80_lo = radar_thr.thresh0_80_lo;
			st->rparams.radar_thrs.thresh1_80_lo = radar_thr.thresh1_80_lo;
			st->rparams.radar_thrs.thresh0_80_hi = radar_thr.thresh0_80_hi;
			st->rparams.radar_thrs.thresh1_80_hi = radar_thr.thresh1_80_hi;
		}
		phy_radar_detect_enable(pi, pi->sh->radar);
		break;
	}
	case IOV_SVAL(IOV_RADAR_ARGS): {
		wl_radar_args_t radarargs;

		if (!pi->sh->up) {
			err = BCME_NOTUP;
			break;
		}

		/* len is check done before gets here */
		bcopy(p, &radarargs, sizeof(wl_radar_args_t));
		if (radarargs.version != WL_RADAR_ARGS_VERSION) {
			err = BCME_VERSION;
			break;
		}
		bcopy(&radarargs, &st->rparams.radar_args, sizeof(wl_radar_args_t));
		/* apply radar inits to hardware if we are on the A/LP/NPHY */
		phy_radar_detect_enable(pi, pi->sh->radar);
		break;
	}
	case IOV_SVAL(IOV_PHY_DFS_LP_BUFFER):
		if (ISNPHY(pi) || ISHTPHY(pi) || ISACPHY(pi)) {
			pi->dfs_lp_buffer_nphy = bool_val;
		} else
			err = BCME_UNSUPPORTED;
		break;

	case IOV_GVAL(IOV_RADAR_STATUS):
		if (ISNPHY(pi) || ISHTPHY(pi) || ISACPHY(pi)) {
			bcopy(&st->radar_status, a, sizeof(wl_radar_status_t));
		} else
			err = BCME_UNSUPPORTED;
		break;

	case IOV_SVAL(IOV_CLEAR_RADAR_STATUS):
		if (ISNPHY(pi) || ISHTPHY(pi) || ISACPHY(pi)) {
			st->radar_status.detected = FALSE;
			st->radar_status.count = 0;
		} else
			err = BCME_UNSUPPORTED;
		break;

	default:
		err = BCME_UNSUPPORTED;
		break;
	}

	return err;
}

/* register iovar table/handlers to the system */
int
BCMATTACHFN(phy_radar_register_iovt)(phy_info_t *pi, wlc_iocv_info_t *ii)
{
	wlc_iovt_desc_t iovd;
#if defined(WLC_PATCH_IOCTL)
	wlc_iov_disp_fn_t disp_fn = IOV_PATCH_FN;
	const bcm_iovar_t *patch_table = IOV_PATCH_TBL;
#else
	wlc_iov_disp_fn_t disp_fn = NULL;
	const bcm_iovar_t* patch_table = NULL;
#endif /* WLC_PATCH_IOCTL */

	ASSERT(ii != NULL);

	wlc_iocv_init_iovd(phy_radar_iovt,
	                   phy_radar_pack_iov, phy_radar_unpack_iov,
	                   phy_radar_doiovar, disp_fn, patch_table, pi,
	                   &iovd);

	return wlc_iocv_register_iovt(ii, &iovd);
}
