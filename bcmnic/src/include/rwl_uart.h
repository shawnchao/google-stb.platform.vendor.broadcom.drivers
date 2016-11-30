/*
 * uart driver definitions
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
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied or
 * duplicated in any form, in whole or in part, without the prior written
 * permission of Broadcom Corporation.
 *
 * $Id: rwl_uart.h 543582 2015-03-24 19:48:01Z $
 */

#ifndef _rwl_uart_h_
#define _rwl_uart_h_

extern rwl_dongle_packet_t g_rwl_dongle_data;

extern void remote_uart_tx(uchar*buf);

#endif	/* _rwl_uart_h_ */
