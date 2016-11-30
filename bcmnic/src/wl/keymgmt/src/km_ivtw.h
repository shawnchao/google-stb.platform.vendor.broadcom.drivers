/*
 * Internal interface to BRCMAPIVTW support
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
 * $Id: km_ivtw.h 617309 2016-02-04 23:50:36Z $
 */

#ifndef _km_ivtw_h_
#define _km_ivtw_h_

#include "km.h"

typedef struct km_ivtw km_ivtw_t;

km_ivtw_t* km_ivtw_attach(wlc_info_t *wlc, wlc_keymgmt_t *km);
void km_ivtw_detach(km_ivtw_t **ivtw);

int km_ivtw_get_mode(km_ivtw_t *ivtw);
int km_ivtw_set_mode(km_ivtw_t *ivtw, int val);

int km_ivtw_enable(km_ivtw_t *ivtw, wlc_key_index_t key_idx, bool enable);

bool km_ivtw_is_replay(km_ivtw_t *ivtw, wlc_key_info_t *key_info, int ins,
	uint8 *key_seq, uint8 *rx_seq, size_t seq_len);

int km_ivtw_reset(km_ivtw_t *ivtw, wlc_key_index_t key_idx);

void km_ivtw_update(km_ivtw_t *ivtw, wlc_key_info_t *key_info, int ins,
    uint8 *rx_seq, size_t seq_len, bool chained);


void km_ivtw_clone(km_ivtw_t *from_ivtw, km_ivtw_t *to_ivtw,
	wlc_key_index_t from_key_idx, wlc_key_index_t to_key_idx);

#endif /* _km_ivtw_h_ */
