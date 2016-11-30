/*
 * Inits for Broadcom PHY core tables,
 * Networking Adapter Device Driver.
 *
 * THIS IS A GENERATED FILE - DO NOT EDIT
 * Generated on Thu Nov 12 11:37:24 PST 2009
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
 * All Rights Reserved.
 *
 * $Id: wlc_phytbl_ht.c 606042 2015-12-14 06:21:23Z jqliu $
 */
/* FILE-CSTYLED */

#include <wlc_cfg.h>
#include <typedefs.h>

#include <wlc_phy_int.h>
#include <wlc_phytbl_ht.h>

CONST uint16 ht_mcs_tbl_rev0[] = {
	0x0000,
	0x0008,
	0x000a,
	0x0010,
	0x0012,
	0x0019,
	0x001a,
	0x001c,
	0x0080,
	0x0088,
	0x008a,
	0x0090,
	0x0092,
	0x0099,
	0x009a,
	0x009c,
	0x0100,
	0x0108,
	0x010a,
	0x0110,
	0x0112,
	0x0119,
	0x011a,
	0x011c,
	0x0180,
	0x0188,
	0x018a,
	0x0190,
	0x0192,
	0x0199,
	0x019a,
	0x019c,
	0x0000,
	0x0098,
	0x00a0,
	0x00a8,
	0x009a,
	0x00a2,
	0x00aa,
	0x0120,
	0x0128,
	0x0128,
	0x0130,
	0x0138,
	0x0138,
	0x0140,
	0x0122,
	0x012a,
	0x012a,
	0x0132,
	0x013a,
	0x013a,
	0x0142,
	0x01a8,
	0x01b0,
	0x01b8,
	0x01b0,
	0x01b8,
	0x01c0,
	0x01c8,
	0x01c0,
	0x01c8,
	0x01d0,
	0x01d0,
	0x01d8,
	0x01aa,
	0x01b2,
	0x01ba,
	0x01b2,
	0x01ba,
	0x01c2,
	0x01ca,
	0x01c2,
	0x01ca,
	0x01d2,
	0x01d2,
	0x01da,
	0x0001,
	0x0002,
	0x0004,
	0x0009,
	0x000c,
	0x0011,
	0x0014,
	0x0018,
	0x0020,
	0x0021,
	0x0022,
	0x0024,
	0x0081,
	0x0082,
	0x0084,
	0x0089,
	0x008c,
	0x0091,
	0x0094,
	0x0098,
	0x00a0,
	0x00a1,
	0x00a2,
	0x00a4,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
	0x0007,
};


CONST uint8 ht_tx_evm_tbl_rev0[] = {
	0x09,
	0x0e,
	0x11,
	0x14,
	0x17,
	0x1a,
	0x1d,
	0x20,
	0x09,
	0x0e,
	0x11,
	0x14,
	0x17,
	0x1a,
	0x1d,
	0x20,
	0x09,
	0x0e,
	0x11,
	0x14,
	0x17,
	0x1a,
	0x1d,
	0x20,
	0x09,
	0x0e,
	0x11,
	0x14,
	0x17,
	0x1a,
	0x1d,
	0x20,
};


CONST uint8 ht_rx_evm_shaping_tbl_rev0[] = {
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
	0x00,
};


CONST uint32 ht_noise_shaping_tbl_rev0[] = {
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};


CONST uint32 ht_phasetrack_tbl_rev0[] = {

        0x00035700,
	0x0002cc9a,
	0x00026666,
	0x00032064,
	0x00032064,
	0x00032064,
	0x00032064,
	0x00032064,
	0x00032064,
	0x0002583c,
	0x0002583c,
	0x00035700,
	0x0002cc9a,
	0x00026666,
	0x00032064,
	0x00032064,
	0x00032064,
	0x00032064,
	0x00032064,
	0x00032064,
	0x0002583c,
	0x0002583c,
	
};


CONST uint8 ht_est_pwr_lut_core1_rev0[] = {
	0x55,
	0x54,
	0x54,
	0x53,
	0x52,
	0x52,
	0x51,
	0x51,
	0x50,
	0x4f,
	0x4f,
	0x4e,
	0x4e,
	0x4d,
	0x4c,
	0x4c,
	0x4b,
	0x4a,
	0x49,
	0x49,
	0x48,
	0x47,
	0x46,
	0x46,
	0x45,
	0x44,
	0x43,
	0x42,
	0x41,
	0x40,
	0x40,
	0x3f,
	0x3e,
	0x3d,
	0x3c,
	0x3a,
	0x39,
	0x38,
	0x37,
	0x36,
	0x35,
	0x33,
	0x32,
	0x31,
	0x2f,
	0x2e,
	0x2c,
	0x2b,
	0x29,
	0x27,
	0x25,
	0x23,
	0x21,
	0x1f,
	0x1d,
	0x1a,
	0x18,
	0x15,
	0x12,
	0x0e,
	0x0b,
	0x07,
	0x02,
	0xfd,
};


CONST uint8 ht_est_pwr_lut_core2_rev0[] = {
	0x55,
	0x54,
	0x54,
	0x53,
	0x52,
	0x52,
	0x51,
	0x51,
	0x50,
	0x4f,
	0x4f,
	0x4e,
	0x4e,
	0x4d,
	0x4c,
	0x4c,
	0x4b,
	0x4a,
	0x49,
	0x49,
	0x48,
	0x47,
	0x46,
	0x46,
	0x45,
	0x44,
	0x43,
	0x42,
	0x41,
	0x40,
	0x40,
	0x3f,
	0x3e,
	0x3d,
	0x3c,
	0x3a,
	0x39,
	0x38,
	0x37,
	0x36,
	0x35,
	0x33,
	0x32,
	0x31,
	0x2f,
	0x2e,
	0x2c,
	0x2b,
	0x29,
	0x27,
	0x25,
	0x23,
	0x21,
	0x1f,
	0x1d,
	0x1a,
	0x18,
	0x15,
	0x12,
	0x0e,
	0x0b,
	0x07,
	0x02,
	0xfd,
};


CONST uint8 ht_est_pwr_lut_core3_rev0[] = {
	0x55,
	0x54,
	0x54,
	0x53,
	0x52,
	0x52,
	0x51,
	0x51,
	0x50,
	0x4f,
	0x4f,
	0x4e,
	0x4e,
	0x4d,
	0x4c,
	0x4c,
	0x4b,
	0x4a,
	0x49,
	0x49,
	0x48,
	0x47,
	0x46,
	0x46,
	0x45,
	0x44,
	0x43,
	0x42,
	0x41,
	0x40,
	0x40,
	0x3f,
	0x3e,
	0x3d,
	0x3c,
	0x3a,
	0x39,
	0x38,
	0x37,
	0x36,
	0x35,
	0x33,
	0x32,
	0x31,
	0x2f,
	0x2e,
	0x2c,
	0x2b,
	0x29,
	0x27,
	0x25,
	0x23,
	0x21,
	0x1f,
	0x1d,
	0x1a,
	0x18,
	0x15,
	0x12,
	0x0e,
	0x0b,
	0x07,
	0x02,
	0xfd,
};


CONST uint32 ht_gainctrl_lut_rev0[] = {
	0x5bf70044,
	0x5bf70042,
	0x5bf70040,
	0x5bf7003e,
	0x5bf7003c,
	0x5bf7003b,
	0x5bf70039,
	0x5bf70037,
	0x5bf70036,
	0x5bf70034,
	0x5bf70033,
	0x5bf70031,
	0x5bf70030,
	0x5ba70044,
	0x5ba70042,
	0x5ba70040,
	0x5ba7003e,
	0x5ba7003c,
	0x5ba7003b,
	0x5ba70039,
	0x5ba70037,
	0x5ba70036,
	0x5ba70034,
	0x5ba70033,
	0x5b770044,
	0x5b770042,
	0x5b770040,
	0x5b77003e,
	0x5b77003c,
	0x5b77003b,
	0x5b770039,
	0x5b770037,
	0x5b770036,
	0x5b770034,
	0x5b770033,
	0x5b770031,
	0x5b770030,
	0x5b77002f,
	0x5b77002d,
	0x5b77002c,
	0x5b470044,
	0x5b470042,
	0x5b470040,
	0x5b47003e,
	0x5b47003c,
	0x5b47003b,
	0x5b470039,
	0x5b470037,
	0x5b470036,
	0x5b470034,
	0x5b470033,
	0x5b470031,
	0x5b470030,
	0x5b47002f,
	0x5b47002d,
	0x5b47002c,
	0x5b47002b,
	0x5b47002a,
	0x5b270044,
	0x5b270042,
	0x5b270040,
	0x5b27003e,
	0x5b27003c,
	0x5b27003b,
	0x5b270039,
	0x5b270037,
	0x5b270036,
	0x5b270034,
	0x5b270033,
	0x5b270031,
	0x5b270030,
	0x5b27002f,
	0x5b170044,
	0x5b170042,
	0x5b170040,
	0x5b17003e,
	0x5b17003c,
	0x5b17003b,
	0x5b170039,
	0x5b170037,
	0x5b170036,
	0x5b170034,
	0x5b170033,
	0x5b170031,
	0x5b170030,
	0x5b17002f,
	0x5b17002d,
	0x5b17002c,
	0x5b17002b,
	0x5b17002a,
	0x5b170028,
	0x5b170027,
	0x5b170026,
	0x5b170025,
	0x5b170024,
	0x5b170023,
	0x5b070044,
	0x5b070042,
	0x5b070040,
	0x5b07003e,
	0x5b07003c,
	0x5b07003b,
	0x5b070039,
	0x5b070037,
	0x5b070036,
	0x5b070034,
	0x5b070033,
	0x5b070031,
	0x5b070030,
	0x5b07002f,
	0x5b07002d,
	0x5b07002c,
	0x5b07002b,
	0x5b07002a,
	0x5b070028,
	0x5b070027,
	0x5b070026,
	0x5b070025,
	0x5b070024,
	0x5b070023,
	0x5b070022,
	0x5b070021,
	0x5b070020,
	0x5b07001f,
	0x5b07001e,
	0x5b07001d,
	0x5b07001d,
	0x5b07001c,
};


CONST uint32 ht_iq_lut_core1_rev0[] = {
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};


CONST uint32 ht_iq_lut_core2_rev0[] = {
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};


CONST uint32 ht_iq_lut_core3_rev0[] = {
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};


CONST uint16 ht_loft_lut_core1_rev0[] = {
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
};


CONST uint16 ht_loft_lut_core2_rev0[] = {
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
};


CONST uint16 ht_loft_lut_core3_rev0[] = {
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
	0x0000,
};


CONST uint16 ht_papd_comp_rfpwr_tbl_core0_rev0[] = {
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
};


CONST uint16 ht_papd_comp_rfpwr_tbl_core1_rev0[] = {
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
};


CONST uint16 ht_papd_comp_rfpwr_tbl_core2_rev0[] = {
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x0036,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x002a,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x001e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x000e,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01fc,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01ee,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
	0x01d6,
};


CONST uint32 ht_papd_comp_epsilon_tbl_core0_rev0[] = {
	0x00000000,
	0x00000000,
	0x00016023,
	0x00006028,
	0x00034036,
	0x0003402e,
	0x0007203c,
	0x0006e037,
	0x00070030,
	0x0009401f,
	0x0009a00f,
	0x000b600d,
	0x000c8007,
	0x000ce007,
	0x00101fff,
	0x00121ff9,
	0x0012e004,
	0x0014dffc,
	0x0016dff6,
	0x0018dfe9,
	0x001b3fe5,
	0x001c5fd0,
	0x001ddfc2,
	0x001f1fb6,
	0x00207fa4,
	0x00219f8f,
	0x0022ff7d,
	0x00247f6c,
	0x0024df5b,
	0x00267f4b,
	0x0027df3b,
	0x0029bf3b,
	0x002b5f2f,
	0x002d3f2e,
	0x002f5f2a,
	0x002fff15,
	0x00315f0b,
	0x0032defa,
	0x0033beeb,
	0x0034fed9,
	0x00353ec5,
	0x00361eb0,
	0x00363e9b,
	0x0036be87,
	0x0036be70,
	0x0038fe67,
	0x0044beb2,
	0x00513ef3,
	0x00595f11,
	0x00669f3d,
	0x0078dfdf,
	0x00a143aa,
	0x01642fff,
	0x0162afff,
	0x01620fff,
	0x0160cfff,
	0x015f0fff,
	0x015dafff,
	0x015bcfff,
	0x015bcfff,
	0x015b4fff,
	0x015acfff,
	0x01590fff,
	0x0156cfff,
};


CONST uint32 ht_papd_comp_epsilon_tbl_core1_rev0[] = {
	0x00000000,
	0x00000000,
	0x00016023,
	0x00006028,
	0x00034036,
	0x0003402e,
	0x0007203c,
	0x0006e037,
	0x00070030,
	0x0009401f,
	0x0009a00f,
	0x000b600d,
	0x000c8007,
	0x000ce007,
	0x00101fff,
	0x00121ff9,
	0x0012e004,
	0x0014dffc,
	0x0016dff6,
	0x0018dfe9,
	0x001b3fe5,
	0x001c5fd0,
	0x001ddfc2,
	0x001f1fb6,
	0x00207fa4,
	0x00219f8f,
	0x0022ff7d,
	0x00247f6c,
	0x0024df5b,
	0x00267f4b,
	0x0027df3b,
	0x0029bf3b,
	0x002b5f2f,
	0x002d3f2e,
	0x002f5f2a,
	0x002fff15,
	0x00315f0b,
	0x0032defa,
	0x0033beeb,
	0x0034fed9,
	0x00353ec5,
	0x00361eb0,
	0x00363e9b,
	0x0036be87,
	0x0036be70,
	0x0038fe67,
	0x0044beb2,
	0x00513ef3,
	0x00595f11,
	0x00669f3d,
	0x0078dfdf,
	0x00a143aa,
	0x01642fff,
	0x0162afff,
	0x01620fff,
	0x0160cfff,
	0x015f0fff,
	0x015dafff,
	0x015bcfff,
	0x015bcfff,
	0x015b4fff,
	0x015acfff,
	0x01590fff,
	0x0156cfff,
};


CONST uint32 ht_papd_comp_epsilon_tbl_core2_rev0[] = {
	0x00000000,
	0x00000000,
	0x00016023,
	0x00006028,
	0x00034036,
	0x0003402e,
	0x0007203c,
	0x0006e037,
	0x00070030,
	0x0009401f,
	0x0009a00f,
	0x000b600d,
	0x000c8007,
	0x000ce007,
	0x00101fff,
	0x00121ff9,
	0x0012e004,
	0x0014dffc,
	0x0016dff6,
	0x0018dfe9,
	0x001b3fe5,
	0x001c5fd0,
	0x001ddfc2,
	0x001f1fb6,
	0x00207fa4,
	0x00219f8f,
	0x0022ff7d,
	0x00247f6c,
	0x0024df5b,
	0x00267f4b,
	0x0027df3b,
	0x0029bf3b,
	0x002b5f2f,
	0x002d3f2e,
	0x002f5f2a,
	0x002fff15,
	0x00315f0b,
	0x0032defa,
	0x0033beeb,
	0x0034fed9,
	0x00353ec5,
	0x00361eb0,
	0x00363e9b,
	0x0036be87,
	0x0036be70,
	0x0038fe67,
	0x0044beb2,
	0x00513ef3,
	0x00595f11,
	0x00669f3d,
	0x0078dfdf,
	0x00a143aa,
	0x01642fff,
	0x0162afff,
	0x01620fff,
	0x0160cfff,
	0x015f0fff,
	0x015dafff,
	0x015bcfff,
	0x015bcfff,
	0x015b4fff,
	0x015acfff,
	0x01590fff,
	0x0156cfff,
};


CONST uint32 ht_papd_cal_scalars_tbl_core0_rev0[] = {
	0x0b5e002d,
	0x0ae2002f,
	0x0a3b0032,
	0x09a70035,
	0x09220038,
	0x08ab003b,
	0x081f003f,
	0x07a20043,
	0x07340047,
	0x06d2004b,
	0x067a004f,
	0x06170054,
	0x05bf0059,
	0x0571005e,
	0x051e0064,
	0x04d3006a,
	0x04910070,
	0x044c0077,
	0x040f007e,
	0x03d90085,
	0x03a1008d,
	0x036f0095,
	0x033d009e,
	0x030b00a8,
	0x02e000b2,
	0x02b900bc,
	0x029200c7,
	0x026d00d3,
	0x024900e0,
	0x022900ed,
	0x020a00fb,
	0x01ec010a,
	0x01d20119,
	0x01b7012a,
	0x019e013c,
	0x0188014e,
	0x01720162,
	0x015d0177,
	0x0149018e,
	0x013701a5,
	0x012601be,
	0x011501d8,
	0x010601f4,
	0x00f70212,
	0x00e90231,
	0x00dc0253,
	0x00d00276,
	0x00c4029b,
	0x00b902c3,
	0x00af02ed,
	0x00a50319,
	0x009c0348,
	0x0093037a,
	0x008b03af,
	0x008303e6,
	0x007c0422,
	0x00750460,
	0x006e04a3,
	0x006804e9,
	0x00620533,
	0x005d0582,
	0x005805d6,
	0x0053062e,
	0x004e068c,
};


CONST uint32 ht_papd_cal_scalars_tbl_core1_rev0[] = {
	0x0b5e002d,
	0x0ae2002f,
	0x0a3b0032,
	0x09a70035,
	0x09220038,
	0x08ab003b,
	0x081f003f,
	0x07a20043,
	0x07340047,
	0x06d2004b,
	0x067a004f,
	0x06170054,
	0x05bf0059,
	0x0571005e,
	0x051e0064,
	0x04d3006a,
	0x04910070,
	0x044c0077,
	0x040f007e,
	0x03d90085,
	0x03a1008d,
	0x036f0095,
	0x033d009e,
	0x030b00a8,
	0x02e000b2,
	0x02b900bc,
	0x029200c7,
	0x026d00d3,
	0x024900e0,
	0x022900ed,
	0x020a00fb,
	0x01ec010a,
	0x01d20119,
	0x01b7012a,
	0x019e013c,
	0x0188014e,
	0x01720162,
	0x015d0177,
	0x0149018e,
	0x013701a5,
	0x012601be,
	0x011501d8,
	0x010601f4,
	0x00f70212,
	0x00e90231,
	0x00dc0253,
	0x00d00276,
	0x00c4029b,
	0x00b902c3,
	0x00af02ed,
	0x00a50319,
	0x009c0348,
	0x0093037a,
	0x008b03af,
	0x008303e6,
	0x007c0422,
	0x00750460,
	0x006e04a3,
	0x006804e9,
	0x00620533,
	0x005d0582,
	0x005805d6,
	0x0053062e,
	0x004e068c,
};


CONST uint32 ht_papd_cal_scalars_tbl_core2_rev0[] = {
	0x0b5e002d,
	0x0ae2002f,
	0x0a3b0032,
	0x09a70035,
	0x09220038,
	0x08ab003b,
	0x081f003f,
	0x07a20043,
	0x07340047,
	0x06d2004b,
	0x067a004f,
	0x06170054,
	0x05bf0059,
	0x0571005e,
	0x051e0064,
	0x04d3006a,
	0x04910070,
	0x044c0077,
	0x040f007e,
	0x03d90085,
	0x03a1008d,
	0x036f0095,
	0x033d009e,
	0x030b00a8,
	0x02e000b2,
	0x02b900bc,
	0x029200c7,
	0x026d00d3,
	0x024900e0,
	0x022900ed,
	0x020a00fb,
	0x01ec010a,
	0x01d20119,
	0x01b7012a,
	0x019e013c,
	0x0188014e,
	0x01720162,
	0x015d0177,
	0x0149018e,
	0x013701a5,
	0x012601be,
	0x011501d8,
	0x010601f4,
	0x00f70212,
	0x00e90231,
	0x00dc0253,
	0x00d00276,
	0x00c4029b,
	0x00b902c3,
	0x00af02ed,
	0x00a50319,
	0x009c0348,
	0x0093037a,
	0x008b03af,
	0x008303e6,
	0x007c0422,
	0x00750460,
	0x006e04a3,
	0x006804e9,
	0x00620533,
	0x005d0582,
	0x005805d6,
	0x0053062e,
	0x004e068c,
};


CONST htphytbl_info_t htphytbl_info_rev0[] = {
	{             &ht_mcs_tbl_rev0,                    sizeof(ht_mcs_tbl_rev0)/sizeof(ht_mcs_tbl_rev0[0]),  18,    0,  16 },
	{          &ht_tx_evm_tbl_rev0,              sizeof(ht_tx_evm_tbl_rev0)/sizeof(ht_tx_evm_tbl_rev0[0]),  39,    0,   8 },
	{  &ht_rx_evm_shaping_tbl_rev0, sizeof(ht_rx_evm_shaping_tbl_rev0)/sizeof(ht_rx_evm_shaping_tbl_rev0[0]),  38,    0,   8 },
	{   &ht_noise_shaping_tbl_rev0, sizeof(ht_noise_shaping_tbl_rev0)/sizeof(ht_noise_shaping_tbl_rev0[0]),  37,    0,  32 },
	{      &ht_phasetrack_tbl_rev0,      sizeof(ht_phasetrack_tbl_rev0)/sizeof(ht_phasetrack_tbl_rev0[0]),  47,    0,  32 },
	{   &ht_est_pwr_lut_core1_rev0, sizeof(ht_est_pwr_lut_core1_rev0)/sizeof(ht_est_pwr_lut_core1_rev0[0]),  26,    0,   8 },
	{   &ht_est_pwr_lut_core2_rev0, sizeof(ht_est_pwr_lut_core2_rev0)/sizeof(ht_est_pwr_lut_core2_rev0[0]),  27,    0,   8 },
	{   &ht_est_pwr_lut_core3_rev0, sizeof(ht_est_pwr_lut_core3_rev0)/sizeof(ht_est_pwr_lut_core3_rev0[0]),  28,    0,   8 },
	{        &ht_gainctrl_lut_rev0,          sizeof(ht_gainctrl_lut_rev0)/sizeof(ht_gainctrl_lut_rev0[0]),  26,  192,  32 },
	{        &ht_iq_lut_core1_rev0,          sizeof(ht_iq_lut_core1_rev0)/sizeof(ht_iq_lut_core1_rev0[0]),  26,  320,  32 },
	{        &ht_iq_lut_core2_rev0,          sizeof(ht_iq_lut_core2_rev0)/sizeof(ht_iq_lut_core2_rev0[0]),  27,  320,  32 },
	{        &ht_iq_lut_core3_rev0,          sizeof(ht_iq_lut_core3_rev0)/sizeof(ht_iq_lut_core3_rev0[0]),  28,  320,  32 },
	{      &ht_loft_lut_core1_rev0,      sizeof(ht_loft_lut_core1_rev0)/sizeof(ht_loft_lut_core1_rev0[0]),  26,  448,  16 },
	{      &ht_loft_lut_core2_rev0,      sizeof(ht_loft_lut_core2_rev0)/sizeof(ht_loft_lut_core2_rev0[0]),  27,  448,  16 },
	{      &ht_loft_lut_core3_rev0,      sizeof(ht_loft_lut_core3_rev0)/sizeof(ht_loft_lut_core3_rev0[0]),  28,  448,  16 },
	{&ht_papd_comp_rfpwr_tbl_core0_rev0, sizeof(ht_papd_comp_rfpwr_tbl_core0_rev0)/sizeof(ht_papd_comp_rfpwr_tbl_core0_rev0[0]),  26,  576,  16 },
	{&ht_papd_comp_rfpwr_tbl_core1_rev0, sizeof(ht_papd_comp_rfpwr_tbl_core1_rev0)/sizeof(ht_papd_comp_rfpwr_tbl_core1_rev0[0]),  27,  576,  16 },
	{&ht_papd_comp_rfpwr_tbl_core2_rev0, sizeof(ht_papd_comp_rfpwr_tbl_core2_rev0)/sizeof(ht_papd_comp_rfpwr_tbl_core2_rev0[0]),  28,  576,  16 },
	{&ht_papd_comp_epsilon_tbl_core0_rev0, sizeof(ht_papd_comp_epsilon_tbl_core0_rev0)/sizeof(ht_papd_comp_epsilon_tbl_core0_rev0[0]),  31,    0,  32 },
	{&ht_papd_comp_epsilon_tbl_core1_rev0, sizeof(ht_papd_comp_epsilon_tbl_core1_rev0)/sizeof(ht_papd_comp_epsilon_tbl_core1_rev0[0]),  33,    0,  32 },
	{&ht_papd_comp_epsilon_tbl_core2_rev0, sizeof(ht_papd_comp_epsilon_tbl_core2_rev0)/sizeof(ht_papd_comp_epsilon_tbl_core2_rev0[0]),  35,    0,  32 },
	{&ht_papd_cal_scalars_tbl_core0_rev0, sizeof(ht_papd_cal_scalars_tbl_core0_rev0)/sizeof(ht_papd_cal_scalars_tbl_core0_rev0[0]),  32,    0,  32 },
	{&ht_papd_cal_scalars_tbl_core1_rev0, sizeof(ht_papd_cal_scalars_tbl_core1_rev0)/sizeof(ht_papd_cal_scalars_tbl_core1_rev0[0]),  34,    0,  32 },
	{&ht_papd_cal_scalars_tbl_core2_rev0, sizeof(ht_papd_cal_scalars_tbl_core2_rev0)/sizeof(ht_papd_cal_scalars_tbl_core2_rev0[0]),  36,    0,  32 },
};


CONST uint32 htphytbl_info_sz_rev0 = sizeof(htphytbl_info_rev0)/sizeof(htphytbl_info_rev0[0]);
