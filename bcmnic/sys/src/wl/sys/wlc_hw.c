/*
 * Public H/W info of
 * Broadcom 802.11bang Networking Device Driver
 *
 * Copyright (C) 2016, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 *
 * $Id: wlc_hw.c 612761 2016-01-14 23:06:00Z $
 */

#include <wlc_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <wlioctl.h>
#include <sbhnddma.h>
#include <hnddma.h>
#include <d11.h>
#include <wlc_rate.h>
#include <wlc_pub.h>
#include <wlc_bsscfg.h>
#include <wlc.h>
#include <wlc_hw.h>
#include <wlc_hw_priv.h>
#include <wlc_bmac.h>
#include <wlc_dump.h>

/* local functions */

wlc_hw_info_t *
BCMATTACHFN(wlc_hw_attach)(wlc_info_t *wlc, osl_t *osh, uint unit, uint *err, uint macunit)
{
	wlc_hw_info_t *wlc_hw;
	int i;

	if ((wlc_hw = (wlc_hw_info_t *)
	     MALLOCZ(osh, sizeof(wlc_hw_info_t))) == NULL) {
		*err = 1010;
		WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes for wlc_hw\n",
				unit, __FUNCTION__, MALLOCED(osh)));
		goto fail;
	}
	wlc_hw->wlc = wlc;
	wlc_hw->osh = osh;
	wlc_hw->vars_table_accessor[0] = 0;
	wlc_hw->unit = unit;

	wlc_hw->macunit = macunit;

	wlc_hw->regs = (d11regs_t *)si_setcore(wlc->pub->sih, D11_CORE_ID, unit);
	wlc_hw->macunit = si_coreunit(wlc->pub->sih);
	if ((wlc_hw->btc = (wlc_hw_btc_info_t*)
	     MALLOCZ(osh, sizeof(wlc_hw_btc_info_t))) == NULL) {
		*err = 1011;
		WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes for btc\n",
				unit, __FUNCTION__, MALLOCED(osh)));
		goto fail;
	}

	if ((wlc_hw->bandstate[0] = (wlc_hwband_t*)
	     MALLOCZ(osh, sizeof(wlc_hwband_t) * MAXBANDS)) == NULL) {
		*err = 1012;
		WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes for bandstate\n",
				unit, __FUNCTION__, MALLOCED(osh)));
		goto fail;
	}

	for (i = 1; i < MAXBANDS; i++) {
		wlc_hw->bandstate[i] = (wlc_hwband_t *)
		        ((uintptr)wlc_hw->bandstate[0] + sizeof(wlc_hwband_t) * i);
	}

	if ((wlc_hw->pub = MALLOCZ(osh, sizeof(wlc_hw_t))) == NULL) {
		*err = 1013;
		WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes for pub\n",
				unit, __FUNCTION__, MALLOCED(osh)));
		goto fail;
	}


	*err = 0;

	wlc->hw_pub = wlc_hw->pub;
	return wlc_hw;

fail:
	wlc_hw_detach(wlc_hw);
	return NULL;
}

void
BCMATTACHFN(wlc_hw_detach)(wlc_hw_info_t *wlc_hw)
{
	osl_t *osh;
	wlc_info_t *wlc;

	if (wlc_hw == NULL)
		return;

	osh = wlc_hw->osh;
	wlc = wlc_hw->wlc;

	wlc->hw_pub = NULL;

	if (wlc_hw->btc != NULL)
		MFREE(osh, wlc_hw->btc, sizeof(wlc_hw_btc_info_t));

	if (wlc_hw->bandstate[0] != NULL)
		MFREE(osh, wlc_hw->bandstate[0], sizeof(wlc_hwband_t) * MAXBANDS);

	/* free hw struct */
	if (wlc_hw->pub != NULL)
		MFREE(osh, wlc_hw->pub, sizeof(wlc_hw_t));

	/* free hw struct */
	MFREE(osh, wlc_hw, sizeof(wlc_hw_info_t));
}

void
wlc_hw_set_piomode(wlc_hw_info_t *wlc_hw, bool piomode)
{
	wlc_hw->_piomode = piomode;
}

bool
wlc_hw_get_piomode(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->_piomode;
}

void
wlc_hw_set_di(wlc_hw_info_t *wlc_hw, uint fifo, hnddma_t *di)
{
	wlc_hw->di[fifo] = di;
	wlc_hw->pub->di[fifo] = di;
}

void
wlc_hw_set_pio(wlc_hw_info_t *wlc_hw, uint fifo, pio_t *pio)
{
	wlc_hw->pio[fifo] = pio;
	wlc_hw->pub->pio[fifo] = pio;
}

bool
wlc_hw_deviceremoved(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->clk ?
	        (R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) &
	         (MCTL_PSM_JMP_0 | MCTL_IHR_EN)) != MCTL_IHR_EN :
	        si_deviceremoved(wlc_hw->sih);
}

uint32
wlc_hw_get_wake_override(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->wake_override;
}

uint
wlc_hw_get_bandunit(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->band->bandunit;
}

void
BCMATTACHFN(wlc_hw_get_txavail)(wlc_hw_info_t *wlc_hw, uint *txavail[])
{
	int i;

	for (i = 0; i < NFIFO; i++)
		txavail[i] = wlc_hw->txavail[i];
}

uint32
wlc_hw_get_macintmask(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->macintmask;
}

uint32
wlc_hw_get_macintstatus(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->macintstatus;
}

/* MHF2_SKIP_ADJTSF muxing, clear the flag when no one requests to skip the ucode
 * TSF adjustment.
 */
void
wlc_skip_adjtsf(wlc_info_t *wlc, bool skip, wlc_bsscfg_t *cfg, uint32 user, int bands)
{
	wlc_hw_info_t *wlc_hw = wlc->hw;
	uint global_start = NBITS(wlc_hw->skip_adjtsf) - WLC_SKIP_ADJTSF_USER_MAX;
	int b;

	ASSERT(cfg != NULL || user < WLC_SKIP_ADJTSF_USER_MAX);
	ASSERT(cfg == NULL || (uint32)WLC_BSSCFG_IDX(cfg) < global_start);

	b = (cfg == NULL) ? user + global_start : (uint32)WLC_BSSCFG_IDX(cfg);
	if (skip)
		setbit(&wlc_hw->skip_adjtsf, b);
	else
		clrbit(&wlc_hw->skip_adjtsf, b);


	wlc_bmac_mhf(wlc_hw, MHF2, MHF2_SKIP_ADJTSF,
	        wlc_hw->skip_adjtsf ? MHF2_SKIP_ADJTSF : 0, bands);
}

/* MCTL_AP muxing, set the bit when no one requests to stop the AP functions (beacon, prbrsp) */
void
wlc_ap_ctrl(wlc_info_t *wlc, bool on, wlc_bsscfg_t *cfg, uint32 user)
{
	wlc_hw_info_t *wlc_hw = wlc->hw;

	BCM_REFERENCE(cfg);
	BCM_REFERENCE(user);

	if (on && wlc_hw->mute_ap != 0) {
		WL_INFORM(("wl%d: ignore %s %d request %d mute_ap 0x%08x\n",
		           wlc->pub->unit, cfg != NULL ? "bsscfg" : "user",
		           cfg != NULL ? WLC_BSSCFG_IDX(cfg) : user, on, wlc_hw->mute_ap));
		return;
	}

	wlc_hw->mute_ap = 0;

	WL_INFORM(("wl%d: %s %d MCTL_AP %d\n",
	           wlc->pub->unit, cfg != NULL ? "bsscfg" : "user",
	           cfg != NULL ? WLC_BSSCFG_IDX(cfg) : user, on));

	wlc_bmac_mctrl(wlc_hw, MCTL_AP, on ? MCTL_AP : 0);
}

#ifdef AP
void
wlc_ap_mute(wlc_info_t *wlc, bool mute, wlc_bsscfg_t *cfg, uint32 user)
{
	wlc_hw_info_t *wlc_hw = wlc->hw;
	uint global_start = NBITS(wlc_hw->mute_ap) - WLC_AP_MUTE_USER_MAX;
	int b;
	bool ap;

	ASSERT(cfg != NULL || user < WLC_AP_MUTE_USER_MAX);
	ASSERT(cfg == NULL || (uint32)WLC_BSSCFG_IDX(cfg) < global_start);

	b = (cfg == NULL) ? user + global_start : (uint32)WLC_BSSCFG_IDX(cfg);
	if (mute)
		setbit(&wlc_hw->mute_ap, b);
	else
		clrbit(&wlc_hw->mute_ap, b);

	/* aibss doesn't set ap iovar, but still needs to TOGGLE mctl_ap bit */
	ap = AP_ACTIVE(wlc) && wlc_hw->mute_ap == 0;

	WL_INFORM(("wl%d: %s %d mute %d mute_ap 0x%x AP_ACTIVE() %d MCTL_AP %d\n",
	           wlc->pub->unit, cfg != NULL ? "bsscfg" : "user",
	           cfg != NULL ? WLC_BSSCFG_IDX(cfg) : user,
	           mute, wlc_hw->mute_ap, AP_ACTIVE(wlc), ap));

	wlc_bmac_mctrl(wlc_hw, MCTL_AP, ap ? MCTL_AP : 0);
}
#endif /* AP */