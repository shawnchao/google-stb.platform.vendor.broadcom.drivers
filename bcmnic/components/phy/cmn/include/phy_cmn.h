/*
 * PHYComMoN module internal interface (to other PHY modules).
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
 * $Id: phy_cmn.h 619656 2016-02-17 21:16:33Z gkatzir $
 */

#ifndef _phy_cmn_h_
#define _phy_cmn_h_

#include <phy_api.h>

typedef struct phy_cmn_info phy_cmn_info_t;

/* attach/detach */
phy_cmn_info_t *phy_cmn_attach(phy_info_t *pi);
void phy_cmn_detach(phy_cmn_info_t *ci);

/* query object */
typedef enum phy_obj_type {
	PHY_OBJ_RADAR_DETECT = 0
} phy_obj_type_t;

typedef struct phy_obj_ptr phy_obj_ptr_t;

int phy_cmn_register_obj(phy_cmn_info_t *ci, phy_obj_ptr_t *obj, phy_obj_type_t type);
phy_obj_ptr_t *phy_cmn_find_obj(phy_cmn_info_t *ci, phy_obj_type_t type);
phy_info_t *phy_get_other_pi(phy_info_t *pi);
uint8 phy_get_current_core(phy_info_t *pi);
int8 phy_get_master(const phy_info_t *pi);
int8 phy_set_master(const phy_info_t *pi, int8 master);

void phy_set_femctrl_clb_prio_5g_acphy(phy_info_t *pi, uint32 slice);
uint32 phy_get_femctrl_clb_prio_5g_acphy(phy_info_t *pi);
void phy_set_femctrl_clb_prio_2g_acphy(phy_info_t *pi, uint32 slice);
uint32 phy_get_femctrl_clb_prio_2g_acphy(phy_info_t *pi);


#endif /* _phy_cmn_h_ */
