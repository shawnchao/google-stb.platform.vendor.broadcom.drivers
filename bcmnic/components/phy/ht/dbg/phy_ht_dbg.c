/*
 * HTPHY Debug modules implementation
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
 * $Id: phy_ht_dbg.c 606042 2015-12-14 06:21:23Z jqliu $
 */

#include <phy_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <phy_dbg.h>
#include <phy_mem.h>
#include "phy_type_dbg.h"
#include <phy_ht.h>
#include <phy_ht_dbg.h>

/* *************************** */
/* Modules used by this module */
/* *************************** */
#include <wlc_phyreg_ht.h>

/* module private states */
struct phy_ht_dbg_info {
	phy_info_t *pi;
	phy_ht_info_t *hti;
	phy_dbg_info_t *info;
};

/* local functions */
#if defined(BCMDBG) || defined(BCMDBG_DUMP)
static void wlc_htphy_txerr_dump(phy_type_dbg_ctx_t *ctx, uint16 err);
#else
#define wlc_htphy_txerr_dump NULL
#endif
#ifdef WL_MACDBG
static void phy_ht_dbg_gpio_out_enab(phy_type_dbg_ctx_t *ctx, bool enab);
#else
#define phy_ht_dbg_gpio_out_enab NULL
#endif

/* register phy type specific implementation */
phy_ht_dbg_info_t *
BCMATTACHFN(phy_ht_dbg_register_impl)(phy_info_t *pi, phy_ht_info_t *hti,
	phy_dbg_info_t *info)
{
	phy_ht_dbg_info_t *di;
	phy_type_dbg_fns_t fns;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* allocate all storage together */
	if ((di = phy_malloc(pi, sizeof(phy_ht_dbg_info_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc failed\n", __FUNCTION__));
		goto fail;
	}
	di->pi = pi;
	di->hti = hti;
	di->info = info;

	/* register PHY type specific implementation */
	bzero(&fns, sizeof(fns));
	fns.ctx = di;
	fns.txerr_dump = wlc_htphy_txerr_dump;
	fns.gpio_out_enab = phy_ht_dbg_gpio_out_enab;

	if (phy_dbg_register_impl(info, &fns) != BCME_OK) {
		PHY_ERROR(("%s: phy_dbg_register_impl failed\n", __FUNCTION__));
		goto fail;
	}

	return di;

	/* error handling */
fail:
	if (di != NULL)
		phy_mfree(pi, di, sizeof(phy_ht_dbg_info_t));
	return NULL;
}

void
BCMATTACHFN(phy_ht_dbg_unregister_impl)(phy_ht_dbg_info_t *di)
{
	phy_info_t *pi;
	phy_dbg_info_t *info;

	ASSERT(di);

	pi = di->pi;
	info = di->info;

	PHY_TRACE(("%s\n", __FUNCTION__));

	/* unregister from common */
	phy_dbg_unregister_impl(info);

	phy_mfree(pi, di, sizeof(phy_ht_dbg_info_t));
}

#if defined(BCMDBG) || defined(BCMDBG_DUMP)
static const bcm_bit_desc_t attr_flags_phy_err[] = {
	{HTPHY_TxError_NDPError_MASK, "NDPError"},
	{HTPHY_TxError_illegal_frame_type_MASK, "Illegal frame type"},
	{HTPHY_TxError_COMBUnsupport_MASK, "COMBUnsupport"},
	{HTPHY_TxError_BWUnsupport_MASK, "BWUnsupport"},
	{HTPHY_TxError_txInCal_MASK, "txInCal_MASK"},
	{HTPHY_TxError_send_frame_low_MASK, "send_frame_low"},
	{HTPHY_TxError_lengthmismatch_short_MASK, "lengthmismatch_short"},
	{HTPHY_TxError_lengthmismatch_long_MASK, "lengthmismatch_long"},
	{HTPHY_TxError_invalidRate_MASK, "invalidRate_MASK"},
	{0, NULL}
};

static void
wlc_htphy_txerr_dump(phy_type_dbg_ctx_t *ctx, uint16 err)
{
	if (err != 0) {
		char flagstr[128];
		phy_ht_dbg_info_t *di = (phy_ht_dbg_info_t *)ctx;
		phy_info_t *pi = di->pi;
		printf("!!!corerev %d, dump as HT phy\n", pi->sh->corerev);
		bcm_format_flags(attr_flags_phy_err, err, flagstr, 128);
		printf("Tx PhyErr 0x%04x (%s)\n", err, flagstr);
	}
}
#endif /* BCMDBG || BCMDBG_DUMP */

#ifdef WL_MACDBG
static void
phy_ht_dbg_gpio_out_enab(phy_type_dbg_ctx_t *ctx, bool enab)
{
	phy_ht_dbg_info_t *di = (phy_ht_dbg_info_t *)ctx;
	phy_info_t *pi = di->pi;
	BCM_REFERENCE(enab);

	if (D11REV_LT(pi->sh->corerev, 50)) {
		W_REG(pi->sh->osh, &pi->regs->phyregaddr, HTPHY_gpioLoOutEn);
		W_REG(pi->sh->osh, &pi->regs->phyregdata, 0);
		W_REG(pi->sh->osh, &pi->regs->phyregaddr, HTPHY_gpioHiOutEn);
		W_REG(pi->sh->osh, &pi->regs->phyregdata, 0);
	}
}
#endif /* WL_MACDBG */