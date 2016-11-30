/*
 * WLC Object Registry API Implementation
 * Broadcom 802.11abg Networking Device Driver
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
 * $Id: wlc_objregistry.c 629911 2016-04-06 22:14:43Z $
 */

/**
 * @file
 * @brief
 * Chip/Feature specific enable/disable need to be done for Object registry
 * Moved object registry related functions from wl/sys to utils folder
 * A wrapper is provided in this layer/file to enab/disab obj registry for each key
 */


#ifdef WL_OBJ_REGISTRY

#include <wlc_cfg.h>
#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <wlioctl.h>
#include <wl_dbg.h>
#include <wlc_types.h>
#include <bcm_objregistry.h>
#include <wlc_objregistry.h>

#define WLC_OBJR_ERROR(args)

/* WLC OBj reg to include Chip/feature specific enable/disable support for data sharing */
struct wlc_obj_registry {
	obj_registry_t *objr;
	uint8 key_enab[OBJR_MAX_KEYS/NBBY + 1];
};

wlc_obj_registry_t*
BCMATTACHFN(obj_registry_alloc)(osl_t *osh, int count)
{
	wlc_obj_registry_t *wlc_objr = NULL;
	if ((wlc_objr = MALLOCZ(osh, sizeof(wlc_obj_registry_t))) == NULL) {
			WLC_OBJR_ERROR(("bcm obj registry %s: out of memory, malloced %d bytes\n",
				__FUNCTION__, MALLOCED(osh)));
	} else {
		wlc_objr->objr = bcm_obj_registry_alloc(osh, count);
		if (wlc_objr->objr) {
			memset(wlc_objr->key_enab, 0xff, OBJR_MAX_KEYS / NBBY + 1);
		} else {
			MFREE(osh, wlc_objr, sizeof(wlc_obj_registry_t));
			wlc_objr = NULL;
		}
	}
	return wlc_objr;
}

void
BCMATTACHFN(obj_registry_free)(wlc_obj_registry_t *wlc_objr, osl_t *osh)
{
	if (wlc_objr) {
		if (wlc_objr->objr)
			bcm_obj_registry_free(wlc_objr->objr, osh);
		MFREE(osh, wlc_objr, sizeof(wlc_obj_registry_t));
	}
}

int
BCMATTACHFN(obj_registry_set)(wlc_obj_registry_t *wlc_objr, obj_registry_key_t key, void *value)
{
	int ret = BCME_OK;
	ASSERT(key < OBJR_MAX_KEYS);
	if (isset(wlc_objr->key_enab, key)) {
		ret = bcm_obj_registry_set(wlc_objr->objr, key, value);
	}
	return ret;
}

void*
BCMATTACHFN(obj_registry_get)(wlc_obj_registry_t *wlc_objr, obj_registry_key_t key)
{
	void *ret = NULL;
	ASSERT(key < OBJR_MAX_KEYS);
	if (isset(wlc_objr->key_enab, key)) {
		ret = bcm_obj_registry_get(wlc_objr->objr, key);
	}
	return ret;
}

int
BCMATTACHFN(obj_registry_ref)(wlc_obj_registry_t *wlc_objr, obj_registry_key_t key)
{
	int ret = 1;
	ASSERT(key < OBJR_MAX_KEYS);
	if (isset(wlc_objr->key_enab, key)) {
		ret = bcm_obj_registry_ref(wlc_objr->objr, key);
	}
	return ret;
}

int
BCMATTACHFN(obj_registry_unref)(wlc_obj_registry_t *wlc_objr, obj_registry_key_t key)
{
	int ret = 0;
	ASSERT(key < OBJR_MAX_KEYS);
	if (isset(wlc_objr->key_enab, key)) {
		return bcm_obj_registry_unref(wlc_objr->objr, key);
	}
	return ret;
}

/* A special helper function to identify if we are cleaning up for the finale WLC */
int
obj_registry_islast(wlc_obj_registry_t *wlc_objr)
{
	return bcm_obj_registry_islast(wlc_objr->objr);
}

void
BCMATTACHFN(obj_registry_disable)(wlc_obj_registry_t *wlc_objr, obj_registry_key_t key)
{
	ASSERT(key < OBJR_MAX_KEYS);
	clrbit(wlc_objr->key_enab, key);

	/* Un Ref the registry and then reset the value of the stored pointer to NULL */
	if (obj_registry_unref(wlc_objr, key) == 0) {
		obj_registry_set(wlc_objr, key, NULL);
	}
}


#endif /* WL_OBJ_REGISTRY */