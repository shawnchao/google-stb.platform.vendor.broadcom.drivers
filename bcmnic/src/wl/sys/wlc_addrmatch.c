/*
 * Common (OS-independent) portion of
 * Broadcom 802.11bang Networking Device Driver
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
 * $Id: wlc_addrmatch.c 631569 2016-04-15 02:17:57Z $
 */

/**
 * @file
 * @brief
 * This file implements the address matching interface to be used
 * by the high driver or monolithic driver. d11 corerev >= 40 supports
 * AMT with attributes for matching in addition to the address. Prior
 * versions ignore the attributes provided in the interface
 */

#include <wlc_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <bcmutils.h>
#include <proto/ethernet.h>
#include <proto/bcmeth.h>
#include <proto/bcmevent.h>
#include <bcmwifi_channels.h>
#include <siutils.h>
#include <d11.h>
#include <wlc_rate.h>
#include <wlioctl.h>
#include <wlc_pub.h>
#include <wlc.h>
#include <wlc_hw.h>
#include <wlc_bmac.h>
#include <wlc_txbf.h>
#include <wlc_dump.h>

#include <wlc_addrmatch.h>

#define HAS_AMT(wlc) D11REV_GE(wlc->pub->corerev, 40)
#define IS_PRE_AMT(wlc) D11REV_LT(wlc->pub->corerev, 40)

#ifdef ACKSUPR_MAC_FILTER
#define ADDRMATCH_INFO_STATUS_GET(wlc, idx) wlc->addrmatch_info[idx].status
#define ADDRMATCH_INFO_STATUS_SET(wlc, idx, val) wlc->addrmatch_info[idx].status = val

/* amt entry status */
enum {
	ADDRMATCH_INIT = 0,
	ADDRMATCH_EMPTY,
	ADDRMATCH_USED,
	ADDRMATCH_NEED_DELETE
};

/* for acksupr amt info */
struct wlc_addrmatch_info {
	int8 status;
};

int
wlc_addrmatch_info_alloc(wlc_info_t *wlc, int max_entry_num)
{
	if (wlc->addrmatch_info == NULL) {
		int i;
		struct ether_addr ea;
		uint16 attr;

		wlc->addrmatch_info = MALLOC(wlc->osh,
			sizeof(wlc_addrmatch_info_t) * max_entry_num);
		if (wlc->addrmatch_info == NULL) {
			WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes\n",
			         wlc->pub->unit, __FUNCTION__, MALLOCED(wlc->osh)));
			return BCME_NOMEM;
		}
		memset(wlc->addrmatch_info, ADDRMATCH_INIT,
			sizeof(wlc_addrmatch_info_t) * max_entry_num);

		for (i = 0; i < max_entry_num; i++) {
			/* generic used entry sync */
			wlc_get_addrmatch(wlc, i, &ea, &attr);
			if (attr)
				ADDRMATCH_INFO_STATUS_SET(wlc, i, ADDRMATCH_USED);
			else
				ADDRMATCH_INFO_STATUS_SET(wlc, i, ADDRMATCH_EMPTY);
		}
	}
	return BCME_OK;
}

void
wlc_addrmatch_info_free(wlc_info_t *wlc, int max_entry_num)
{
	if (wlc->addrmatch_info != NULL) {
		MFREE(wlc->osh, wlc->addrmatch_info,
			sizeof(wlc_addrmatch_info_t) * max_entry_num);
		wlc->addrmatch_info = NULL;
	}
	return;
}
#endif /* ACKSUPR_MAC_FILTER */

#if defined(BCMULP) && defined(BCMFCBS)
int wlc_get_valid_amt_count(wlc_info_t *wlc)
{
	int i, amt_count = 0;
	struct ether_addr ea;
	uint16 attr;
	for (i = 0; i < (uint8)wlc->pub->max_addrma_idx; i++) {
		wlc_get_addrmatch(wlc, i, &ea, &attr);
		if (ETHER_ISNULLADDR(&ea) && !(attr & AMT_ATTR_VALID))
			continue;
		else
			amt_count++;
	}
	return amt_count;
}
#endif /* defined(BCMULP) && defined(BCMFCBS) */

uint16
wlc_set_addrmatch(wlc_info_t *wlc, int idx, const struct ether_addr *addr,
	uint16 attr)

{
	uint16 prev_attr = 0;
#ifdef ACKSUPR_MAC_FILTER
	int slot = idx;
#endif /* ACKSUPR_MAC_FILTER */


	ASSERT(wlc->pub->corerev > 4);
	if (HAS_AMT(wlc)) {
		switch (idx) {
		case WLC_ADDRMATCH_IDX_MAC:
			prev_attr = wlc_bmac_write_amt(wlc->hw, AMT_IDX_MAC, addr, attr);
#ifdef ACKSUPR_MAC_FILTER
			slot = AMT_IDX_MAC;
#endif /* ACKSUPR_MAC_FILTER */
			break;
		case WLC_ADDRMATCH_IDX_BSSID:
			prev_attr = wlc_bmac_write_amt(wlc->hw, AMT_IDX_BSSID, addr, attr);
#ifdef ACKSUPR_MAC_FILTER
			slot = AMT_IDX_BSSID;
#endif /* ACKSUPR_MAC_FILTER */
			break;
		default:
			ASSERT(idx >= 0);
			if (idx < (int)wlc->pub->max_addrma_idx) {
				prev_attr = wlc_bmac_write_amt(wlc->hw, idx, addr, attr);
#ifdef WL_BEAMFORMING
				if (TXBF_ENAB(wlc->pub))
					wlc_txfbf_update_amt_idx(wlc->txbf, idx, addr);
#endif
			}
			break;
		}
		goto done;
	}

	switch (idx) {
	case WLC_ADDRMATCH_IDX_MAC:
		wlc_bmac_set_rxe_addrmatch(wlc->hw, RCM_MAC_OFFSET, addr);
#ifdef ACKSUPR_MAC_FILTER
		slot = RCM_MAC_OFFSET;
#endif /* ACKSUPR_MAC_FILTER */
		break;
	case WLC_ADDRMATCH_IDX_BSSID:
		wlc_bmac_set_rxe_addrmatch(wlc->hw, RCM_BSSID_OFFSET, addr);
#ifdef ACKSUPR_MAC_FILTER
		slot = RCM_BSSID_OFFSET;
#endif /* ACKSUPR_MAC_FILTER */
		break;
	default:
		ASSERT(idx >= 0);
		if (idx < RCMTA_SIZE)
			wlc_bmac_set_rcmta(wlc->hw, idx, addr);
		break;
	}

done:
#ifdef ACKSUPR_MAC_FILTER
	if (WLC_ACKSUPR(wlc)) {
		if (attr)
			ADDRMATCH_INFO_STATUS_SET(wlc, slot, ADDRMATCH_USED);
		else
			ADDRMATCH_INFO_STATUS_SET(wlc, slot, ADDRMATCH_EMPTY);
	}
#endif /* ACKSUPR_MAC_FILTER */
	return prev_attr;
}

uint16
wlc_clear_addrmatch(wlc_info_t *wlc, int idx)
{
	return wlc_set_addrmatch(wlc, idx, &ether_null, 0);
}

#if defined(WL_BEAMFORMING) || defined(ACKSUPR_MAC_FILTER) || (defined(BCMULP) && \
	defined(BCMFCBS))
void
wlc_get_addrmatch(wlc_info_t *wlc, int idx, struct ether_addr *addr,
	uint16 *attr)
{
	ASSERT(wlc->pub->corerev > 4);
#ifdef ACKSUPR_MAC_FILTER
	if (WLC_ACKSUPR(wlc) &&
		(ADDRMATCH_INFO_STATUS_GET(wlc, idx) == ADDRMATCH_EMPTY)) {
		*attr = 0;
		memset(addr, 0, sizeof(*addr));
		return;
	}
#endif /* ACKSUPR_MAC_FILTER */
	if (HAS_AMT(wlc)) {
		switch (idx) {
		case WLC_ADDRMATCH_IDX_MAC: idx = AMT_IDX_MAC; break;
		case WLC_ADDRMATCH_IDX_BSSID: idx = AMT_IDX_BSSID; break;
		default: break;
		}
		wlc_bmac_read_amt(wlc->hw, idx, addr, attr);
		return;
	}

	/* no support for reading the rxe address match registers for now.
	 * can be added if necessary by supporting it in the bmac layer
	 * and the corresponding RPCs for the split driver.
	 */
	if (idx >= 0) {
		wlc_bmac_get_rcmta(wlc->hw, idx, addr);
		*attr =  !ETHER_ISNULLADDR(addr) ? AMT_ATTR_VALID : 0;
#ifdef ACKSUPR_MAC_FILTER
		if (WLC_ACKSUPR(wlc) && (*attr == 0) &&
			(ADDRMATCH_INFO_STATUS_GET(wlc, idx) == ADDRMATCH_USED))
			*attr = AMT_ATTR_VALID;
#endif /* ACKSUPR_MAC_FILTER */
	} else {
		memset(addr, 0, sizeof(*addr));
		*attr = 0;
	}
}
#endif 



int
BCMATTACHFN(wlc_addrmatch_attach)(wlc_info_t *wlc)
{
	return BCME_OK;
}

void
BCMATTACHFN(wlc_addrmatch_detach)(wlc_info_t *wlc)
{
}
