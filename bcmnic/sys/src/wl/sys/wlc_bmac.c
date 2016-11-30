/*
 * Common (OS-independent) portion of
 * Broadcom 802.11bang Networking Device Driver
 *
 * BMAC portion of common driver.
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
 * $Id: wlc_bmac.c 613545 2016-01-19 08:03:39Z $
 */

/**
 * @file
 * @brief
 * In contrast to the traditional NIC driver architecture, dongle devices are limited by a slow(er)
 * host-client BUS. To cope with this bus latency(significantly slower R_REG, W_REG), some host
 * driver blocks have to be moved to run on dongle on-chip memory with simple CPU(like ARM7,
 * cortexM3). Dongle driver normally requires less load on host CPU due to the offloading.
 */


#include <wlc_cfg.h>

/* On a split driver, wlc_bmac_recv() runs in the low driver. When PKTC is defined,
 * wlc_bmac_recv() calls directly to wlc_rxframe_chainable() and wlc_sendup_chain(),
 * which run in the high driver.
 */
#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <proto/802.11.h>
#include <bcmwifi_channels.h>
#include <bcmutils.h>
#include <d11_cfg.h>
#include <siutils.h>
#include <bcmendian.h>
#include <wlioctl.h>
#include <sbconfig.h>
#include <sbchipc.h>
#include <pcicfg.h>
#include <sbhndpio.h>
#include <sbhnddma.h>
#include <hnddma.h>
#include <hndpmu.h>
#include <bcmdevs.h>
#include <d11.h>
#include <wlc_rate.h>
#include <wlc_pub.h>
#include <wlc_mbss.h>
#include <wlc_channel.h>
#include <wlc_pio.h>
#include <bcmsrom.h>
#include <wlc_rm.h>
#include <wlc_macdbg.h>
#include <sbgci.h>
#include <bcmnvram.h>
#ifdef WLMCNX
#include <wlc_mcnx.h>
#endif
#ifdef PROP_TXSTATUS
#include <wlfc_proto.h>
#include <wlc_wlfc.h>
#endif /* PROP_TAXSTATUS */
#include <wlc.h>
#include <wlc_txs.h>
#include <wlc_hw.h>
#include <wlc_hw_priv.h>
#include <wlc_bmac.h>
#include <wlc_led.h>
#include <wl_export.h>
#include "d11ucode.h"
#include <bcmotp.h>
#include <wlc_stf.h>
#include <wlc_rsdb.h>
#include <wlc_antsel.h>
#ifdef WLDIAG
#include <wlc_diag.h>
#endif
#include <pcie_core.h>
#ifdef ROUTER_COMA
#include <hndchipc.h>
#include <hndjtagdefs.h>
#endif
#ifdef AP
#include <wlc_apps.h>
#endif
#include <wlc_extlog.h>
#include <wlc_alloc.h>
#if defined(SR_ESSENTIALS)
#include "saverestore.h"
#endif /* SR_ESSENTIALS */
#include <wlc_iocv.h>
#include <wlc_bmac_iocv.h>
#include <wlc_dump_reg.h>
#include <wlc_macdbg.h>
#include <phyioctl.h>

/* ******************************************************** */
#include <phy_api.h>
#include <phy_ana_api.h>
#include <phy_chanmgr_api.h>
#include <phy_misc_api.h>
#include <phy_radio_api.h>
#include <phy_rssi_api.h>
#include <phy_wd_api.h>
#include <phy_dbg_api.h>
#include <phy_utils_api.h>
#include <phy_tpc_api.h>
/* ******************************************************** */

#include <wlc_vasip.h>

#ifdef WLDURATION
#include <wlc_duration.h>
#endif


#include <wlc_tx.h>

#include <wlc_btcx.h>

#ifdef BCMLTECOEX
#include <wlc_ltecx.h>
#endif /* BCMLTECOEX */

#include <wlc_rx.h>

#ifdef UCODE_IN_ROM_SUPPORT
#include <d11ucode_upatch.h>
/* for ucode init/download/patch routines */
#include <wlc_ucinit.h>
#include <hndd11.h>
#endif /* UCODE_IN_ROM_SUPPORT */

#ifdef BCMULP
#include <wlc_ulp.h>
#include <ulp.h>
#endif /* BCMULP */
#include <wlc_addrmatch.h>
#include <wlc_perf_utils.h>
#include <wlc_srvsdb.h>

#ifdef ENABLE_PANIC_CHECK_CLK
#define PANIC_CHECK_CLK(clk, format, ...)			\
	do {							\
		if (!clk) {					\
			osl_panic(format, __VA_ARGS__);		\
		}						\
	} while (0);
#else
#define PANIC_CHECK_CLK(clk, format, ...)
#endif

#define	TIMER_INTERVAL_WATCHDOG_BMAC	1000	/* watchdog timer, in unit of ms */
#define TIMER_INTERVAL_RPC_AGG_WATCHDOG_BMAC	5 /* rpc agg watchdog timer, in unit of ms */

/* QT PHY */
#define	SYNTHPU_DLY_PHY_US_QT		100	/* QT(no radio) synthpu_dly time in us */

/* real PHYs */
#define	SYNTHPU_DLY_BPHY_US		800	/* b/g phy synthpu_dly time in us, def */
#define SYNTHPU_DLY_LCNPHY_US		500	/* lcnphy synthpu_dly time in us */
#define SYNTHPU_DLY_LCN20PHY_US		300	/* lcn20phy synthpu_dly time in us */
#ifdef SRFAST
#define SYNTHPU_DLY_LCN40PHY_US		300	/* lcn40phy synthpu_dly time in us */
#else
#define SYNTHPU_DLY_LCN40PHY_US		500	/* lcn40phy synthpu_dly time in us */
#endif /* SRFAST */
#define	SYNTHPU_DLY_NPHY_US		1536	/* n phy REV3 synthpu_dly time in us, def */
#define	SYNTHPU_DLY_HTPHY_US		2288	/* HT phy REV0 synthpu_dly time in us, def */
#define	SYNTHPU_DLY_ACPHY_US		512
#define	SYNTHPU_DLY_ACPHY2_US		1200	/* AC phy synthpu_dly time in us, def */

/* chip specific */
#define SYNTHPU_DLY_LCNPHY_4336_US	400 	/* lcnphy 4336 synthpu_dly time in us */
#if defined(PMU_OPT_REV6)
#define SYNTHPU_DLY_ACPHY_4339_US	310 	/* acphy 4339 synthpu_dly time in us */
#else
#define SYNTHPU_DLY_ACPHY_4339_US	400 	/* acphy 4339 synthpu_dly time in us */
#endif
#define SYNTHPU_DLY_ACPHY_4335_US	400 	/* acphy 4335 synthpu_dly time in us */
#define SYNTHPU_DLY_ACPHY_4349_US	2000 	/* acphy 4349 synthpu_dly time in us */

#define SYNTHPU_DLY_ACPHY_4364_CORE0	2048	/* acphy 4364 3x3 synthpu_dly time in us */

#define SYNTHPU_DLY_ACPHY_4364_CORE1	1568	/* acphy 4364 1x1 synthpu_dly time in us */


typedef struct _btc_flags_ucode {
	uint8	idx;
	uint16	mask;
} btc_flags_ucode_t;

#define BTC_FLAGS_SIZE 9
#define BTC_FLAGS_MHF3_START 1
#define BTC_FLAGS_MHF3_END   6

const btc_flags_ucode_t btc_ucode_flags[BTC_FLAGS_SIZE] = {
	{MHF2, MHF2_BTCPREMPT},
	{MHF3, MHF3_BTCX_DEF_BT},
	{MHF3, MHF3_BTCX_ACTIVE_PROT},
	{MHF3, MHF3_BTCX_SIM_RSP},
	{MHF3, MHF3_BTCX_PS_PROTECT},
	{MHF3, MHF3_BTCX_SIM_TX_LP},
	{MHF3, MHF3_BTCX_ECI},
	{MHF5, MHF5_BTCX_LIGHT},
	{MHF5, MHF5_BTCX_PARALLEL}
};

#ifndef BMAC_DUP_TO_REMOVE
#define WLC_RM_WAIT_TX_SUSPEND		4 /* Wait Tx Suspend */
#define	ANTCNT			10		/* vanilla M_MAX_ANTCNT value */
#endif	/* BMAC_DUP_TO_REMOVE */

#define DMAREG(wlc_hw, direction, fifonum)	(((direction == DMA_TX) ? \
		(void*)(uintptr)&(wlc_hw->regs->f64regs[fifonum].dmaxmt) : \
		(void*)(uintptr)&(wlc_hw->regs->f64regs[fifonum].dmarcv)))

/*
 * The following table lists the buffer memory allocated to xmt fifos in HW.
 * the size is in units of 256bytes(one block), total size is HW dependent
 * ucode has default fifo partition, sw can overwrite if necessary
 *
 * This is documented in twiki under the topic UcodeTxFifo. Please ensure
 * the twiki is updated before making changes.
 */

#define XMTFIFOTBL_STARTREV	4	/* Starting corerev for the fifo size table */

static uint16 xmtfifo_sz[][NFIFO] = {
	{ 14, 14, 14, 14, 14, 2 }, 	/* corerev 4: 3584, 3584, 3584, 3584, 3584, 512 */
	{ 9, 13, 10, 8, 13, 1 }, 	/* corerev 5: 2304, 3328, 2560, 2048, 3328, 256 */
	{ 9, 13, 10, 8, 13, 1 }, 	/* corerev 6: 2304, 3328, 2560, 2048, 3328, 256 */
	{ 9, 13, 10, 8, 13, 1 }, 	/* corerev 7: 2304, 3328, 2560, 2048, 3328, 256 */
	{ 9, 13, 10, 8, 13, 1 }, 	/* corerev 8: 2304, 3328, 2560, 2048, 3328, 256 */
#if (defined(MBSS) && !defined(MBSS_DISABLED))
	/* Fifo sizes are different for ucode with this support */
	{ 9, 14, 10, 9, 14, 6 }, 	/* corerev 9: 2304, 3584, 2560, 2304, 3584, 1536 */
#else
	{ 10, 14, 11, 9, 14, 2 }, 	/* corerev 9: 2560, 3584, 2816, 2304, 3584, 512 */
#endif
	{ 10, 14, 11, 9, 14, 2 }, 	/* corerev 10: 2560, 3584, 2816, 2304, 3584, 512 */
	{ 9, 58, 22, 14, 14, 5 }, 	/* corerev 11: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 9, 58, 22, 14, 14, 5 }, 	/* corerev 12: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 10, 14, 11, 9, 14, 4 }, 	/* corerev 13: 2560, 3584, 2816, 2304, 3584, 1280 */
	{ 10, 14, 11, 9, 14, 2 }, 	/* corerev 14: 2560, 3584, 2816, 2304, 3584, 512 */
	{ 10, 14, 11, 9, 14, 2 }, 	/* corerev 15: 2560, 3584, 2816, 2304, 3584, 512 */
#ifdef WLLPRS
	{ 20, 176, 192, 21, 17, 5 },	/* corerev 16: 5120, 45056, 49152, 5376, 4352, 1280 */
#else /* WLLPRS */
	{ 20, 192, 192, 21, 17, 5 },	/* corerev 16: 5120, 49152, 49152, 5376, 4352, 1280 */
#endif /* WLLPRS */
#ifdef WLLPRS
	{ 20, 176, 192, 21, 17, 5 },	/* corerev 17: 5120, 45056, 49152, 5376, 4352, 1280 */
#else /* WLLPRS */
	{ 20, 192, 192, 21, 17, 5 },	/* corerev 17: 5120, 49152, 49152, 5376, 4352, 1280 */
#endif /* WLLPRS */
	{ 20, 192, 192, 21, 17, 5 },	/* corerev 18: 5120, 49152, 49152, 5376, 4352, 1280 */
	{ 20, 192, 192, 21, 17, 5 },	/* corerev 19: 5120, 49152, 49152, 5376, 4352, 1280 */
	{ 20, 192, 192, 21, 17, 5 },	/* corerev 20: 5120, 49152, 49152, 5376, 4352, 1280 */
	{ 9, 58, 22, 14, 14, 5 },	/* corerev 21: 2304, 14848, 5632, 3584, 3584, 1280 */
#ifdef WLLPRS
	{ 9, 42, 22, 14, 14, 5 }, 	/* corerev 22: 2304, 10752, 5632, 3584, 3584, 1280 */
#else /* WLLPRS */
	{ 9, 58, 22, 14, 14, 5 },	/* corerev 22: 2304, 14848, 5632, 3584, 3584, 1280 */
#endif /* WLLPRS */
	{ 20, 192, 192, 21, 17, 5 },    /* corerev 23: 5120, 49152, 49152, 5376, 4352, 1280 */
	{ 9, 58, 22, 14, 14, 5 },	/* corerev 24: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 9, 58, 22, 14, 14, 5 },	/* corerev 25: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 150, 223, 223, 21, 17, 5 },	/* corerev 26: 38400, 57088, 57088, 5376, 4352, 1280 */
	{ 20, 192, 192, 21, 17, 5 },	/* corerev 27: 5120, 49152, 49152, 5376, 4352, 1280 */
	{ 9, 58, 22, 14, 14, 5 },	/* corerev 28: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 9, 58, 22, 14, 14, 5 },	/* corerev 29: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 9, 98, 22, 14, 14, 5 },       /* corerev 30: 2304, 25088, 5632, 3584, 3584, 1280 */
	{ 9, 58, 22, 14, 14, 5 },	/* corerev 31: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 12, 183, 25, 17, 17, 8 },	/* corerev 32: 3072, 46848, 6400, 4352, 4352, 2048 */
	{ 9, 58, 22, 14, 14, 5 },	/* corerev 33: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 9, 183, 25, 17, 17, 8 },	/* corerev 34: 2304, 46848, 6400, 4352, 4352, 2048 */
	{ 9, 183, 25, 17, 17, 8 },	/* corerev 35: 2304, 46848, 6400, 4352, 4352, 2048 */
	{ 9, 183, 25, 17, 17, 8 },	/* corerev 36: 2304, 46848, 6400, 4352, 4352, 2048 */
	{ 9, 58, 22, 14, 14, 5 },	/* corerev 37: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 9, 183, 25, 17, 17, 8 },	/* corerev 38: 2304, 46848, 6400, 4352, 4352, 2048 */
	{ 9, 73, 14, 14, 9, 2 },	/* corerev 39: 2304, 14848, 5632, 3584, 3584, 1280 */
	{ 9, 183, 25, 17, 17, 8 },	/* corerev >=40: 2304, 46848, 6400, 4352, 4352, 2048 */
};

/* corerev 26 host agg fifo size: 38400, 57088, 57088, 5376, 4352, 1280 */
static uint16 xmtfifo_sz_hostagg[] = { 150, 223, 223, 21, 17, 5 };
/* corerev 26 hw agg fifo size: 25088, 65280, 62208, 5120, 4352, 1280 */
static uint16 xmtfifo_sz_hwagg[] = { 98, 255, 243, 21, 17, 5 };

static uint16 _xmtfifo_sz_dummy[] = { 98, 255, 243, 21, 17, 5 };

static void* BCMRAMFN(get_xmtfifo_sz)(uint *xmtsize)
{
	*xmtsize = ARRAYSIZE(xmtfifo_sz);
	return xmtfifo_sz;
}
static void* BCMRAMFN(get_xmtfifo_sz_dummy)(void)
{
	return _xmtfifo_sz_dummy;
}
/* WLP2P Support */
#ifdef WLP2P
#ifndef WLP2P_UCODE
#error "WLP2P_UCODE is not defined"
#endif
#endif /* WLP2P */

/* PIO Mode Support */
#ifdef WLPIO
#define PIO_ENAB_HW(wlc_hw) ((wlc_hw)->_piomode)
#else
#define PIO_ENAB_HW(wlc_hw) 0
#endif /* WLPIO */

/* P2P ucode Support */
#ifdef WLP2P_UCODE
	#if defined(WL_ENAB_RUNTIME_CHECK)
		#define DL_P2P_UC(wlc_hw)	((wlc_hw)->_p2p)
	#elif defined(WLP2P_UCODE_ONLY)
		#define DL_P2P_UC(wlc_hw)	1
	#elif defined(WLMCNX_DISABLED)
		#define DL_P2P_UC(wlc_hw)	0
	#else
		#define DL_P2P_UC(wlc_hw)	((wlc_hw)->_p2p)
	#endif /* WLP2P_UCODE_ONLY */
#else /* !WLP2P_UCODE */
	#define DL_P2P_UC(wlc_hw)	0
#endif /* !WLP2P_UCODE */

typedef struct bmac_pmq_entry {
	struct ether_addr ea;		/* station address */
	uint8 switches;
	uint8 ps_on;
	struct bmac_pmq_entry *next;
} bmac_pmq_entry_t;


#define BMAC_PMQ_SIZE 16
#define BMAC_PMQ_MIN_ROOM 5

struct bmac_pmq {
	bmac_pmq_entry_t *entry;
	int active_entries; /* number of entries still to receive akcs from high driver  */
	uint8 tx_draining; /* total number of entries */
	uint8 pmq_read_count; /* how many entries have been read since the last clear */
	uint8 pmq_size;
};

#define DMA_CTL_TX 0
#define DMA_CTL_RX 1

#define DMA_CTL_MR 0
#define DMA_CTL_PC 1
#define DMA_CTL_PT 2
#define DMA_CTL_BL 3

#define D11MAC_BMC_STARTADDR	0	/* Specified in units of 256B */
#define D11MAC_BMC_MAXBUFS		1024
#define D11MAC_BMC_BUFSIZE_512BLOCK	1	/* 1 = 512B */
#define D11MAC_BMC_BUFSIZE_256BLOCK	0	/* 0 = 256B */
#define D11MAC_BMC_MAXFIFOS		9
#define D11MAC_BMC_BUFS_512(sz)	((sz) / (1 << (8 + D11MAC_BMC_BUFSIZE_512BLOCK)))
#define D11MAC_BMC_BUFS_256(sz)	((sz) / (1 << (8 + D11MAC_BMC_BUFSIZE_256BLOCK)))

#define D11AC_MAX_RX_FIFO_NUM	2

#define D11MAC_BMC_TPL_IDX		7	/* Template FIFO#7 */
#define D11MAC_BMC_TPL_BYTES	(21 * 1024)	/* 21K bytes default */
#define D11MAC_BMC_TPL_NUMBUFS	D11MAC_BMC_BUFS_512(D11MAC_BMC_TPL_BYTES) /* Note: only for 512 */

/* D11MAC rev48, rev49 layout: BM uses 128KB sized banks
 *  bmc_startaddr = 80KB
 *  TPL FIFO#7
 *      TPL BUFs  = 24KB
 *      Deadspace = 24KB*  (deadspace to align SR ASM at start of bank1)
 *      SR ASM    = 4KB*   (allocated at start of bank1)
 * Total FIFO#7 sizing:
 *      SR disabled: 24KB
 *      SR enabled*: 52KB
 */
#define D11MAC_BMC_STARTADDR_SRASM	320 /* units of 256B => 80KB */
#define D11MAC_BMC_TPL_BUFS_BYTES	(24 * 1024)	/* Min TPL FIFO#7 size */
#define D11MAC_BMC_SRASM_OFFSET		(128 * 1024)	/* bank1 */
#define D11MAC_BMC_SRASM_BYTES		(28 * 1024)	/* deadspace + 4KB */
#define D11MAC_BMC_SRASM_NUMBUFS	D11MAC_BMC_BUFS_512(D11MAC_BMC_SRASM_BYTES)

#if (defined(MBSS) && !defined(MBSS_DISABLED))
#define D11MAC_BMC_TPL_BYTES_PERCORE   D11MAC_BMC_TPL_BYTES
#else
#define D11MAC_BMC_TPL_BYTES_PERCORE   4096        /* 4K Template bytes */
#endif /* MBSS && !MBSS_DISABLED */
#define D11MAC_BMC_STARTADDR            0     /* Specified in units of 256B */
#define D11MAC_BMC_MAXBUFS              1024
#define D11MAC_BMC_BUFSIZE_512BLOCK     1     /* 1 = 512B */
#define D11MAC_BMC_BUFSIZE_256BLOCK     0     /* 0 = 256B */
#define D11MAC_BMC_TPL_IDX              7       /* Template FIFO#7 */
#define D11AC_MAX_FIFO_NUM              9
#define D11MAC_BMC_BUFS_512(sz) ((sz) / (1 << (8 + D11MAC_BMC_BUFSIZE_512BLOCK)))
#define D11MAC_BMC_BUFS_256(sz) ((sz) / (1 << (8 + D11MAC_BMC_BUFSIZE_256BLOCK)))

#define D11AC_MAX_RX_FIFO_NUM   2

#define D11MAC_BMC_TPL_BYTES    (21 * 1024)     /* 21K bytes default */
#define D11MAC_BMC_TPL_NUMBUFS  D11MAC_BMC_BUFS_512(D11MAC_BMC_TPL_BYTES) /* Note: only for 512 */

/* For 4349 core revision 50 */
#define D11MAC_BMC_TPL_NUMBUFS_PERCORE	\
	D11MAC_BMC_BUFS_256(D11MAC_BMC_TPL_BYTES_PERCORE)

#define D11CORE_TEMPLATE_REGION_START D11MAC_BMC_TPL_BYTES_PERCORE

#ifdef SAVERESTORE
#define D11MAC_BMC_SR_BYTES				6144	/* 6K SR bytes */
#else
#define D11MAC_BMC_SR_BYTES				0
#endif /* SAVERESTORE */
#define D11MAC_BMC_SR_NUMBUFS			\
	D11MAC_BMC_BUFS_256(D11MAC_BMC_SR_BYTES)

/* For 4365 core revision 64 */
/* 4365 ca7 use first 1792KB space, 114688 units of 16B(128bits) */
#define D11MAC_SYSM_STARTADDR_H		0x1
#define D11MAC_SYSM_STARTADDR_L		0xC000

#ifndef WLC_BMAC_DUMP_NUM_REGS
#define WLC_BMAC_DUMP_NUM_REGS   0
#endif /* WLC_BMAC_DUMP_NUM_REGS */

const uint16 btc_fw_params_init_vals[BTC_FW_NUM_INDICES] = {
	BTC_FW_RX_REAGG_AFTER_SCO_INIT_VAL,
	BTC_FW_RSSI_THRESH_SCO_INIT_VAL,
	BTC_FW_ENABLE_DYN_LESCAN_PRI_INIT_VAL,
	BTC_FW_LESCAN_LO_TPUT_THRESH_INIT_VAL,
	BTC_FW_LESCAN_HI_TPUT_THRESH_INIT_VAL,
	BTC_FW_LESCAN_GRANT_INT_INIT_VAL,
	0,
	BTC_FW_RSSI_THRESH_BLE_INIT_VAL,
	0,
	0,
	BTC_FW_HOLDSCO_LIMIT_INIT_VAL,
	BTC_FW_HOLDSCO_LIMIT_HI_INIT_VAL,
	BTC_FW_SCO_GRANT_HOLD_RATIO_INIT_VAL,
	BTC_FW_SCO_GRANT_HOLD_RATIO_HI_INIT_VAL,
	BTC_FW_HOLDSCO_HI_THRESH_INIT_VAL,
};

static bool wlc_bmac_istxsuspend(wlc_hw_info_t *wlc_hw, uint tx_fifo);

static void wlc_clkctl_clk(wlc_hw_info_t *wlc, uint mode);
static void wlc_coreinit(wlc_hw_info_t *wlc_hw);
static void wlc_bmac_reset_amt(wlc_hw_info_t *wlc_hw);

/* used by wlc_bmac_wakeucode_init() */
static void wlc_write_inits(wlc_hw_info_t *wlc_hw, const d11init_t *inits);


#ifdef WLRSDB
static void wlc_bmac_rsdb_write_inits(wlc_hw_info_t *wlc_hw, const d11init_t *common_inits,
	const d11init_t *core1_inits);
#endif /* WLRSDB */

#if !defined(UCODE_IN_ROM_SUPPORT) || defined(ULP_DS1ROM_DS0RAM)
static void wlc_ucode_write(wlc_hw_info_t *wlc_hw, const uint32 ucode[], const uint nbytes);
static void wlc_ucode_write_byte(wlc_hw_info_t *wlc_hw, const uint8 ucode[], const uint nbytes);
#endif /* !defined(UCODE_IN_ROM_SUPPORT) || defined(ULP_DS1ROM_DS0RAM) */

#ifdef WAR_HW_RXFIFO_0
static void wlc_war_rxfifo_shm(wlc_hw_info_t *wlc_hw, uint fifo, uint fifosize);
#define BLOCK_SIZE      256
#endif

#ifndef BCMUCDOWNLOAD
static int wlc_ucode_download(wlc_hw_info_t *wlc_hw);
#else
#define wlc_ucode_download(wlc_hw) do {} while (0)
int wlc_process_ucodeparts(wlc_info_t *wlc, uint8 *buf_to_process);
int wlc_handle_ucodefw(wlc_info_t *wlc, wl_ucode_info_t *ucode_buf);
int wlc_handle_initvals(wlc_info_t *wlc, wl_ucode_info_t *ucode_buf);

int BCMINITDATA(cumulative_len) = 0;
#endif

static int wlc_reset_accum_pmdur(wlc_info_t *wlc);

d11init_t *BCMINITDATA(initvals_ptr) = NULL;
uint32 BCMINITDATA(initvals_len) = 0;

static void wlc_ucode_txant_set(wlc_hw_info_t *wlc_hw);

/**
 * The following variable used for dongle images which have ucode download feature. Since ucode is
 * downloaded in chunks & written to ucode memory it is necessary to identify the first chunk, hence
 * the variable which gets reclaimed in attach phase.
*/
uint32 ucode_chunk = 0;

/* used by wlc_dpc() */
static bool wlc_bmac_dotxstatus(wlc_hw_info_t *wlc, tx_status_t *txs, uint32 s2);

#ifdef WLLED
static void wlc_bmac_led_hw_init(wlc_hw_info_t *wlc_hw);
#endif

/* used by wlc_down() */
static void wlc_flushqueues(wlc_hw_info_t *wlc_hw);

static void wlc_write_mhf(wlc_hw_info_t *wlc_hw, uint16 *mhfs);

static void wlc_bmac_btc_btcflag2ucflag(wlc_hw_info_t *wlc_hw);
static bool wlc_bmac_btc_param_to_shmem(wlc_hw_info_t *wlc_hw, uint32 *pval);
static bool wlc_bmac_btc_flags_ucode(uint8 val, uint8 *idx, uint16 *mask);
static void wlc_bmac_btc_flags_upd(wlc_hw_info_t *wlc_hw, bool set_clear, uint16, uint8, uint16);
static void wlc_bmac_btc_gpio_enable(wlc_hw_info_t *wlc_hw);
static void wlc_bmac_btc_gpio_disable(wlc_hw_info_t *wlc_hw);
static void wlc_bmac_btc_gpio_configure(wlc_hw_info_t *wlc_hw);
static void wlc_bmac_gpio_configure(wlc_hw_info_t *wlc_hw, bool is_uppath);

typedef wlc_dump_reg_fn_t bmac_dump_fn_t;

#if defined(BCMDBG_PHYDUMP)
static int wlc_bmac_add_dump_fn(wlc_hw_info_t *wlc_hw, const char *name,
	bmac_dump_fn_t fn, const void *ctx);
#endif
#if defined(BCMDBG_PHYDUMP)
static int wlc_bmac_dump_phy(wlc_hw_info_t *wlc_hw, const char *name, struct bcmstrbuf *b);
static void wlc_bmac_suspend_dump(wlc_hw_info_t *wlc_hw, struct bcmstrbuf *b);
#endif 
static int wlc_bmac_register_dumps(wlc_hw_info_t *wlc_hw);


/* Low Level Prototypes */
#ifdef AP
static void wlc_bmac_pmq_delete(wlc_hw_info_t *wlc_hw);
static void wlc_bmac_pmq_init(wlc_hw_info_t *wlc);
static void wlc_bmac_clearpmq(wlc_hw_info_t *wlc);
#endif /* AP */
static uint16 wlc_bmac_read_objmem16(wlc_hw_info_t *wlc_hw, uint offset, uint32 sel);
static uint32 wlc_bmac_read_objmem32(wlc_hw_info_t *wlc_hw, uint offset, uint32 sel);
static void wlc_bmac_write_objmem16(wlc_hw_info_t *wlc_hw, uint offset, uint16 v, uint32 sel);
static void wlc_bmac_write_objmem32(wlc_hw_info_t *wlc_hw, uint offset, uint32 v, uint32 sel);
static bool wlc_bmac_attach_dmapio(wlc_hw_info_t *wlc_hw, bool wme);
static void wlc_bmac_detach_dmapio(wlc_hw_info_t *wlc_hw);
static int wlc_ucode_bsinit(wlc_hw_info_t *wlc_hw);
static bool wlc_validboardtype(wlc_hw_info_t *wlc);
static bool wlc_isgoodchip(wlc_hw_info_t* wlc_hw);
static char* wlc_get_macaddr(wlc_hw_info_t *wlc_hw, uint unit);
static void wlc_mhfdef(wlc_hw_info_t *wlc_hw, uint16 *mhfs, uint16 mhf2_init);
static void wlc_mctrl_write(wlc_hw_info_t *wlc_hw);
static void wlc_ucode_mute_override_set(wlc_hw_info_t *wlc_hw);
static void wlc_ucode_mute_override_clear(wlc_hw_info_t *wlc_hw);
#if defined(STA) && defined(WLRM)
static uint16 wlc_bmac_read_ihr(wlc_hw_info_t *wlc_hw, uint offset);
#endif
static uint32 wlc_wlintrsoff(wlc_hw_info_t *wlc_hw);
static void wlc_wlintrsrestore(wlc_hw_info_t *wlc_hw, uint32 macintmask);
static int wlc_bmac_btc_param_attach(wlc_info_t *wlc);
static void wlc_bmac_btc_param_init(wlc_hw_info_t *wlc_hw);
static void wlc_corerev_fifofixup(wlc_hw_info_t *wlc_hw);
static void wlc_gpio_init(wlc_hw_info_t *wlc_hw);
static int wlc_corerev_fifosz_validate(wlc_hw_info_t *wlc_hw, uint16 *buf);
static int wlc_bmac_bmc_init(wlc_hw_info_t *wlc_hw);
static bool wlc_bmac_txfifo_sz_chk(wlc_hw_info_t *wlc_hw);
static void wlc_bmac_set_myaddr(wlc_hw_info_t *wlc_hw, struct ether_addr *mac_addr);
static void wlc_write_hw_bcntemplate0(wlc_hw_info_t *wlc_hw, void *bcn, int len);
static void wlc_write_hw_bcntemplate1(wlc_hw_info_t *wlc_hw, void *bcn, int len);
static void wlc_bmac_bsinit(wlc_hw_info_t *wlc_hw, chanspec_t chanspec, bool chanswitch_path);
static uint32 wlc_setband_inact(wlc_hw_info_t *wlc_hw, uint bandunit);
static void wlc_bmac_setband(wlc_hw_info_t *wlc_hw, uint bandunit, chanspec_t chanspec);
static void wlc_bmac_update_slot_timing(wlc_hw_info_t *wlc_hw, bool shortslot);
#ifdef WL11N
static void wlc_upd_ofdm_pctl1_table(wlc_hw_info_t *wlc_hw);
static uint16 wlc_bmac_ofdm_ratetable_offset(wlc_hw_info_t *wlc_hw, uint8 rate);
#endif

#ifdef NOT_RIGGED_UP_YET
/* switch phymode supported on RSDB family of chips */
static int wlc_bmac_switch_phymode(wlc_hw_info_t *wlc_hw, uint16 requested_phymode);
#endif /* NOT_RIGGED_UP_YET */

#if defined(BCMPCIEDEV)
static int wlc_bmac_process_split_fifo_pkt(wlc_hw_info_t *wlc_hw, uint fifo, void* p);
#endif /* BCMPCIEDEV */
static uint8 wlc_bmac_rxfifo_enab(uint fifo);

void wlc_bmac_init_core_reset_disable_fn(wlc_hw_info_t *wlc_hw);
void wlc_bmac_core_reset(wlc_hw_info_t *wlc_hw, uint32 flags, uint32 resetflags);
void wlc_bmac_core_disable(wlc_hw_info_t *wlc_hw, uint32 bits);
bool wlc_bmac_islast_core(wlc_hw_info_t *wlc_hw);
static void wlc_bmac_4349_btcx_prisel_war(wlc_hw_info_t *wlc_hw);
static void wlc_bmac_bmc_template_allocstatus(wlc_hw_info_t *wlc_hw,
uint32 mac_core_unit, int tplbuf);

static void wlc_setxband(wlc_hw_info_t *wlc_hw, uint bandunit);

static const char BCMATTACHDATA(rstr_devid)[] = "devid";
static const char BCMATTACHDATA(rstr_wlD)[] = "wl%d:dma%d";
#if defined(__mips__) || defined(BCM47XX_CA9)
static const char BCMATTACHDATA(rstr_wl_tlclk)[] = "wl_tlclk";
#endif
static const char BCMATTACHDATA(rstr_vendid)[] = "vendid";
static const char BCMATTACHDATA(rstr_boardrev)[] = "boardrev";
static const char BCMATTACHDATA(rstr_sromrev)[] = "sromrev";
static const char BCMATTACHDATA(rstr_boardflags)[] = "boardflags";
static const char BCMATTACHDATA(rstr_boardflags2)[] = "boardflags2";
static const char BCMATTACHDATA(rstr_boardflags4)[] = "boardflags4";
static const char BCMATTACHDATA(rstr_antswctl2g)[] = "antswctl2g";
static const char BCMATTACHDATA(rstr_antswctl5g)[] = "antswctl5g";
#ifdef PKTC
static const char BCMATTACHDATA(rstr_pktc_disable)[] = "pktc_disable";
#endif
static const char BCMATTACHDATA(rstr_aa2g)[] = "aa2g";
static const char BCMATTACHDATA(rstr_macaddr)[] = "macaddr";
static const char BCMATTACHDATA(rstr_il0macaddr)[] = "il0macaddr";
static const char BCMATTACHDATA(rstr_et1macaddr)[] = "et1macaddr";
#ifdef WLLED
static const char BCMATTACHDATA(rstr_ledbhD)[] = "ledbh%d";
static const char BCMATTACHDATA(rstr_wl0gpioD)[] = "wl0gpio%d";
static const char BCMATTACHDATA(rstr_bmac_led_attach_out_of_mem_malloced_D_bytes)[] =
		"wlc_bmac_led_attach: out of memory, malloced %d bytes";
static const char BCMATTACHDATA(rstr_wlD_led_attach_wl_init_timer_for_led_blink_timer_failed)[] =
		"wl%d: wlc_led_attach: wl_init_timer for led_blink_timer failed\n";
#endif /* WLLED */
static const char BCMATTACHDATA(rstr_btc_paramsD)[] = "btc_params%d";
static const char BCMATTACHDATA(rstr_btc_flags)[] = "btc_flags";
static const char BCMATTACHDATA(rstr_btc_mode)[] = "btc_mode";
static const char BCMATTACHDATA(rstr_wowl_gpio)[] = "wowl_gpio";
static const char BCMATTACHDATA(rstr_wowl_gpiopol)[] = "wowl_gpiopol";
#ifdef BCMPCIEDEV
static const char BCMATTACHDATA(tx_burstlen_d11dma)[] = "tx_burstlen_d11dma";
#endif
static const char BCMATTACHDATA(rstr_rsdb_mode)[] = "rsdb_mode";

bool
wlc_bmac_pio_enab_check(wlc_hw_info_t *wlc_hw)
{
	BCM_REFERENCE(wlc_hw);
	return PIO_ENAB_HW(wlc_hw);
}

/** 11b/g has longer slot duration than 11g */
void
wlc_bmac_set_shortslot(wlc_hw_info_t *wlc_hw, bool shortslot)
{
	wlc_hw->shortslot = shortslot;

	if (BAND_2G(wlc_hw->band->bandtype) && wlc_hw->up) {
		wlc_bmac_suspend_mac_and_wait(wlc_hw);
		wlc_bmac_update_slot_timing(wlc_hw, shortslot);
		wlc_bmac_enable_mac(wlc_hw);
	}
}

/**
 * Update the slot timing for standard 11b/g (20us slots)
 * or shortslot 11g (9us slots)
 * The PSM needs to be suspended for this call.
 */
static void
wlc_bmac_update_slot_timing(wlc_hw_info_t *wlc_hw, bool shortslot)
{
	osl_t *osh = wlc_hw->osh;
	d11regs_t *regs = wlc_hw->regs;

	if (shortslot) {
		/* 11g short slot: 11a timing */
		W_REG(osh, &regs->u.d11regs.ifs_slot, 0x0207);	/* APHY_SLOT_TIME */
		wlc_bmac_write_shm(wlc_hw, M_DOT11_SLOT(wlc_hw), APHY_SLOT_TIME);
	} else {
		/* 11g long slot: 11b timing */
		W_REG(osh, &regs->u.d11regs.ifs_slot, 0x0212);	/* BPHY_SLOT_TIME */
		wlc_bmac_write_shm(wlc_hw, M_DOT11_SLOT(wlc_hw), BPHY_SLOT_TIME);
	}
}

/* Helper functions for full ROM chips */
#if !defined(UCODE_IN_ROM_SUPPORT) || defined(ULP_DS1ROM_DS0RAM)
#if !defined(BCMUCDOWNLOAD)
static CONST d11init_t*
WLBANDINITFN(wlc_get_n19initvals34_addr)(void)
{
	return (d11n19initvals34);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_n18initvals32_addr)(void)
{
	return (d11n18initvals32);
}
#endif /* !BCMUCDOWNLOAD */

static CONST d11init_t*
WLBANDINITFN(wlc_get_n19bsinitvals34_addr)(void)
{
	return (d11n19bsinitvals34);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_n18bsinitvals32_addr)(void)
{
	return (d11n18bsinitvals32);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_lcn0bsinitvals24_addr)(void)
{
	return (d11lcn0bsinitvals24);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_lcn0bsinitvals25_addr)(void)
{
	return (d11lcn0bsinitvals25);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_d11lcn400bsinitvals33_addr)(void)
{
	return (d11lcn400bsinitvals33);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_n22bsinitvals31_addr)(void)
{
	return (d11n22bsinitvals31);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_d11lcn406bsinitvals37_addr)(void)
{
	return (d11lcn406bsinitvals37);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_d11lcn407bsinitvals38_addr)(void)
{
	return (d11lcn407bsinitvals38);
}

#if !defined(BCMUCDOWNLOAD)
static CONST d11init_t*
WLBANDINITFN(wlc_get_lcn0initvals24_addr)(void)
{
	return (d11lcn0initvals24);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_lcn0initvals25_addr)(void)
{
	return (d11lcn0initvals25);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_d11lcn400initvals33_addr)(void)
{
	return (d11lcn400initvals33);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_n22initvals31_addr)(void)
{
	return (d11n22initvals31);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_d11lcn406initvals37_addr)(void)
{
	return (d11lcn406initvals37);
}

static CONST d11init_t*
WLBANDINITFN(wlc_get_d11lcn407initvals38_addr)(void)
{
	return (d11lcn407initvals38);
}

#endif /* BCMUCDOWNLOAD */

#endif /* !defined(UCODE_IN_ROM_SUPPORT) || defined(ULP_DS1ROM_DS0RAM) */

/**
 * Band can change as a result of a 'wl up', band request from a higher layer or VSDB related. In
 * such a case, the ucode has to be provided with new band initialization values.
 */
static int
WLBANDINITFN(wlc_ucode_bsinit)(wlc_hw_info_t *wlc_hw)
{
	int err = BCME_OK;
#if defined(MBSS)
	bool ucode9 = TRUE;
	(void)ucode9;
#endif

	/* init microcode host flags */
	wlc_write_mhf(wlc_hw, wlc_hw->band->mhfs);

	/* do band-specific ucode IHR, SHM, and SCR inits */
#ifdef WLRSDB
	if (wlc_bmac_rsdb_cap(wlc_hw))  {
		if (D11REV_IS(wlc_hw->corerev, 50)) {
			wlc_bmac_rsdb_write_inits(wlc_hw, d11ac12bsinitvals50,
				d11ac12bsinitvals50core1);
		} else if (D11REV_IS(wlc_hw->corerev, 55)) {
			wlc_bmac_rsdb_write_inits(wlc_hw, d11ac24bsinitvals55,
				d11ac24bsinitvals55core1);
		} else if (D11REV_IS(wlc_hw->corerev, 56)) {
			wlc_bmac_rsdb_write_inits(wlc_hw, d11ac24bsinitvals56,
				d11ac24bsinitvals56core1);
		} else if (D11REV_IS(wlc_hw->corerev, 59)) {
			wlc_bmac_rsdb_write_inits(wlc_hw, d11ac24bsinitvals59,
				d11ac24bsinitvals59core1);
		} else
			WL_ERROR(("wl%d: %s: corerev %d is invalid", wlc_hw->unit,
				__FUNCTION__, wlc_hw->corerev));
	} else
#endif /* WLRSDB */

#if defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM)
	if (wlc_uci_check_cap_ucode_rom_axislave(wlc_hw)) {
		wlc_uci_write_inits_with_rom_support(wlc_hw, UCODE_BSINITVALS);
	} else {
		WL_ERROR(("%s: wl%d: ROM enabled but no axi/ucode-rom cap! %d\n",
			__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		err = BCME_UNSUPPORTED;
		goto done;
	}
/* the "#else" below is intentional */
#else /* defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM) */

	if (D11REV_IS(wlc_hw->corerev, 58)) {
		if (WLCISACPHY(wlc_hw->band)) {
			if (wlc_hw->macunit == 0) {
				wlc_write_inits(wlc_hw, d11ac26bsinitvals58);
			} else  if (wlc_hw->macunit == 1) {
				wlc_write_inits(wlc_hw, d11ac27bsinitvals58);
			} else {
				ASSERT(0);
			}
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 60)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac36bsinitvals60);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 64)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac32bsinitvals64);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 49)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac9bsinitvals49);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 48)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac8bsinitvals48);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 45) || D11REV_IS(wlc_hw->corerev, 47) ||
	           D11REV_IS(wlc_hw->corerev, 51) || D11REV_IS(wlc_hw->corerev, 52)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac7bsinitvals47);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 54)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac20bsinitvals54);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 46)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac6bsinitvals46);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 43)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac3bsinitvals43);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 42)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac1bsinitvals42);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 41) || D11REV_IS(wlc_hw->corerev, 44)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac2bsinitvals41);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else  if (D11REV_IS(wlc_hw->corerev, 40)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac0bsinitvals40);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 39)) {
		if (WLCISLCN20PHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11lcn200initvals39);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 38)) {
		if (WLCISLCN40PHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_d11lcn407bsinitvals38_addr());
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 37)) {
		if (WLCISLCN40PHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_d11lcn406bsinitvals37_addr());
		} else if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_n22bsinitvals31_addr());
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 34)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_n19bsinitvals34_addr());
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 34\n",
				__FUNCTION__, wlc_hw->unit));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 33)) {
		if (WLCISLCN40PHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_d11lcn400bsinitvals33_addr());
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 32)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_n18bsinitvals32_addr());
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 32\n",
				__FUNCTION__, wlc_hw->unit));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 31)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ht0bsinitvals29);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 30)) {
		if (WLCISNPHY(wlc_hw->band))
			wlc_write_inits(wlc_hw, d11n16bsinitvals30);
	} else if (D11REV_IS(wlc_hw->corerev, 29)) {
		if (WLCISHTPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ht0bsinitvals29);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 26)) {
		if (WLCISHTPHY(wlc_hw->band)) {
			if (D11REV_IS(wlc_hw->corerev, 26))
				wlc_write_inits(wlc_hw, d11ht0bsinitvals26);
			else if (D11REV_IS(wlc_hw->corerev, 29))
				wlc_write_inits(wlc_hw, d11ht0bsinitvals29);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 25) || D11REV_IS(wlc_hw->corerev, 28)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11n0bsinitvals25);
		} else if (WLCISLCNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_lcn0bsinitvals25_addr());
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 24)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11n0bsinitvals24);
		} else if (WLCISLCNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_lcn0bsinitvals24_addr());
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_GE(wlc_hw->corerev, 22)) {
		if (WLCISNPHY(wlc_hw->band)) {
			/* ucode only supports rev23(43224b0) with rev16 ucode */
			if (D11REV_IS(wlc_hw->corerev, 23))
				wlc_write_inits(wlc_hw, d11n0bsinitvals16);
			else
				wlc_write_inits(wlc_hw, d11n0bsinitvals22);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else {
		WL_ERROR(("wl%d: %s: corerev %d is invalid", wlc_hw->unit,
			__FUNCTION__, wlc_hw->corerev));
		goto done;
	}
#endif /* defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM) */

done:
	return err;
} /* wlc_ucode_bsinit */

/** switch to new band but leave it inactive */
static uint32
WLBANDINITFN(wlc_setband_inact)(wlc_hw_info_t *wlc_hw, uint bandunit)
{
	wlc_info_t *wlc = wlc_hw->wlc;
	uint32 macintmask;

	WL_TRACE(("wl%d: wlc_setband_inact\n", wlc_hw->unit));

	ASSERT(bandunit != wlc_hw->band->bandunit);
	ASSERT(si_iscoreup(wlc_hw->sih));
	ASSERT((R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) & MCTL_EN_MAC) == 0);

	/* disable interrupts */
	macintmask = wl_intrsoff(wlc->wl);

	/* radio off -- NPHY radios don't require to be turned off and on on a band switch */
	phy_radio_xband((phy_info_t *)wlc_hw->band->pi);

#if defined(BCMNODOWN)
	/* This is not necessary in BCMNODOWN for either
	 * single- or dual-band, but just set it anyway
	 * to sync up with radio HW state.
	 */
	wlc->pub->radio_active = OFF;
#endif

	ASSERT(wlc_hw->clk);
	PANIC_CHECK_CLK(wlc_hw->clk, "wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__);

	if (!WLCISACPHY(wlc_hw->band))
		wlc_bmac_core_phy_clk(wlc_hw, OFF);

	wlc_setxband(wlc_hw, bandunit);

	return (macintmask);
} /* wlc_setband_inact */

#define	PREFSZ			160

#ifdef PKTC
#define WLPREFHDRS(h, sz)
#else
#define WLPREFHDRS(h, sz)	OSL_PREF_RANGE_ST((h), (sz))
#endif

/**
 * Called as a result of a hardware event: when the D11 core signals one or more received frames
 * on its RX FIFO(s). The received frames are then processed by firmware/driver.
 *
 * Return TRUE if more frames need to be processed. FALSE otherwise.
 * Param 'bound' indicates max. # frames to process before break out.
 */
bool BCMFASTPATH
wlc_bmac_recv(wlc_hw_info_t *wlc_hw, uint fifo, bool bound, wlc_dpc_info_t *dpc)
{
	void *p;
	void *head = NULL;
	void *tail = NULL;
	uint n = 0;
	uint32 tsf_l;
	wlc_d11rxhdr_t *wlc_rxhdr = NULL;
#if defined(PKTC) || defined(PKTC_DONGLE)
	uint16 index = 0;
	void *head0 = NULL;
	bool one_chain = PKTC_ENAB(wlc_hw->wlc->pub);
	uint bound_limit = bound ? wlc_hw->wlc->pub->tunables->pktcbnd : -1;
#else
	uint bound_limit = bound ? wlc_hw->wlc->pub->tunables->rxbnd : -1;
#endif
#if defined(IL_BIGENDIAN)
	uint rxh_len;
#endif
	uint16 rxchan;        /* receive channel in host byte order */
#ifdef WLC_RXFIFO_CNT_ENABLE
	d11rxhdr_t *rxh;
	struct dot11_header *h;
	uint16 fc, pad = 0;
	wl_rxfifo_cnt_t *rxcnt = wlc_hw->wlc->pub->_rxfifo_cnt;
#endif /* WLC_RXFIFO_CNT_ENABLE */

	/* split_fifo will always hold orginal fifo number.
	   Only in the case of mode4, it will hold FIFO-1
	*/
	uint split_fifo = fifo;

	ASSERT(bound_limit > 0);

	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));
	/* gather received frames */
	while (1) {
#ifdef	WL_RXEARLYRC
		if (wlc_hw->rc_pkt_head != NULL) {
			p = wlc_hw->rc_pkt_head;
			wlc_hw->rc_pkt_head = PKTLINK(p);
			PKTSETLINK(p, NULL);
		} else
#endif
		if ((p = (PIO_ENAB_HW(wlc_hw) ?
			wlc_pio_rx(wlc_hw->pio[fifo]) : dma_rx(wlc_hw->di[fifo]))) == NULL)
			break;
#if defined(BCMPCIEDEV)
		/* For fifo-split rx , fifo-0/1 has to be synced up */
		if (BCMPCIEDEV_ENAB() && RXFIFO_SPLIT() &&
		    PKTISRXFRAG(wlc_hw->osh, p)) {
			if (!wlc_bmac_process_split_fifo_pkt(wlc_hw, fifo, p))
				continue;
			/* In mode4 it's known that non-converted copy count data will
			 * arrive in fif0-1 so setting split_fifo to RX_FIFO1
			 */
			split_fifo = RX_FIFO1;
		}
#endif /*  defined(BCMPCIEDEV) */

#if defined(BCM47XX_CA9) || defined(STB)
		/* JIRA: SWWLAN-23796 */
		DMA_MAP(wlc_hw->osh, PKTDATA(wlc_hw->osh, p),
			PKTLEN(wlc_hw->osh, p), DMA_RX, p, NULL);
#endif /* defined(BCM47XX_CA9) */

		/* record the rxfifo in wlc_rxd11hdr */
		wlc_rxhdr = (wlc_d11rxhdr_t *)PKTDATA(wlc_hw->osh, p);
		wlc_rxhdr->rxhdr.fifo = (uint8)split_fifo;


		/* Convert the RxChan to a chanspec for pre-rev40 devices
		 * The chanspec will not have sideband info on this conversion.
		 */
		if (D11REV_LT(wlc_hw->corerev, 40)) {
			rxchan = wlc_rxhdr->rxhdr.RxChan;
			wlc_rxhdr->rxhdr.RxChan = (
				/* channel */
				((rxchan & RXS_CHAN_ID_MASK) >> RXS_CHAN_ID_SHIFT) |
				/* band */
				((rxchan & RXS_CHAN_5G) ? WL_CHANSPEC_BAND_5G :
				WL_CHANSPEC_BAND_2G) |
				/* bw */
				((rxchan & RXS_CHAN_40) ? WL_CHANSPEC_BW_40 :
				WL_CHANSPEC_BW_20) |
				/* bogus sideband */
				WL_CHANSPEC_CTL_SB_L);
		}

#if defined(IL_BIGENDIAN)
		/* If driver is not split, this code runs on the same processor as the rest
		 * of the driver. Convert receive status from little endian to host endian.
		 * On split driver, this code runs on the on chip processor and we don't know
		 * here what type of processor is on the host. On split driver, endian conversion
		 * is done in wlc_recv().
		 */
		if ((D11REV_GE(wlc_hw->wlc->pub->corerev, 64)) &&
		    (wlc_rxhdr->rxhdr.dma_flags & RXS_SHORT_MASK)) {
			/* short rx status received */
			rxh_len = SRXHDR_LEN;
		} else {
			/* long rx status received */
			rxh_len = RXHDR_LEN;
		}
		BCM_REFERENCE(rxh_len);  /* needed when next line turns out to be no-op */
		ltoh16_buf((void*)&wlc_rxhdr->rxhdr, rxh_len);

		/* Keep 8-bit fifo and dma_flags fields in original order */
		ltoh16_buf((void*) &wlc_rxhdr->rxhdr.dma_flags, 2);
#endif /* IL_BIGENDIAN */

#if defined(PKTC) || defined(PKTC_DONGLE)
		if (BCMSPLITRX_ENAB()) {
			/* if management pkt  or data in fifo-1 skip chainable checks */
			if (fifo == PKT_CLASSIFY_FIFO) {
				one_chain = FALSE;
			}
#ifdef WLC_RXFIFO_CNT_ENABLE
			/* Decode d11 header to extract frame type */
			rxh = (d11rxhdr_t *)PKTDATA(wlc_hw->wlc->osh, p);
			pad = ((rxh->RxStatus1 & htol16(RXS_PBPRES)) ? 2 : 0);
			h = (struct dot11_header *)(PKTDATA(wlc_hw->wlc->osh, p) +
				wlc_hw->wlc->hwrxoff + pad + D11_PHY_HDR_LEN);
			fc = ltoh16(h->fc);

			if ((FC_TYPE(fc) == FC_TYPE_DATA))
				WLCNTINCR(rxcnt->rxf_data[fifo]);
			else
				WLCNTINCR(rxcnt->rxf_mgmtctl[fifo]);
#endif /* WLC_RXFIFO_CNT_ENABLE */
		}

		ASSERT(PKTCLINK(p) == NULL);
		/* if current frame hits the hot bridge cache entry, and if it
		 * belongs to the burst received from same source and going to
		 * same destination then it is a candidate for chained sendup.
		 */

		if (one_chain && !wlc_rxframe_chainable(wlc_hw->wlc, &p, index)) {
			one_chain = FALSE;
			/* breaking chain from here, first half of burst can
			 * be sent up as one. frames in the other half are
			 * sent up individually.
			 */
			if (tail != NULL) {
				head0 = head;
				tail = NULL;
			}
		}

		if (p != NULL) {
			index++;
			PKTCENQTAIL(head, tail, p);
		}
#else /* PKTC || PKTC_DONGLE */
		if (!tail)
			head = tail = p;
		else {
			PKTSETLINK(tail, p);
			tail = p;
		}
#endif /* PKTC || PKTC_DONGLE */

#ifdef BCMDBG_POOL
		PKTPOOLSETSTATE(p, POOL_RXD11);
#endif

		/* !give others some time to run! */
		if (++n >= bound_limit)
			break;
	}

	/* post more rbufs */
	if (!PIO_ENAB_HW(wlc_hw))
		dma_rxfill(wlc_hw->di[fifo]);

#if defined(PKTC) || defined(PKTC_DONGLE)
	/* see if the chain is broken */
	if (head0 != NULL) {
		WL_TRACE(("%s: partial chain %p\n", __FUNCTION__, head0));
		wlc_sendup_chain(wlc_hw->wlc, head0);
	} else if (one_chain && (head != NULL)) {
		/* send up burst in one shot */
		WL_TRACE(("%s: full chain %p sz %d\n", __FUNCTION__, head, n));
		wlc_sendup_chain(wlc_hw->wlc, head);
		dpc->processed += n;
		return (n >= bound_limit);
	}
#endif

	/* prefetch the headers */
	if (head != NULL) {
		WLPREFHDRS(PKTDATA(wlc_hw->osh, head), PREFSZ);
	}

	/* get the TSF REG reading */
	wlc_bmac_read_tsf(wlc_hw, &tsf_l, NULL);

	/* process each frame */
	while ((p = head) != NULL) {
#if defined(PKTC) || defined(PKTC_DONGLE)
		head = PKTCLINK(head);
		PKTSETCLINK(p, NULL);
		WLCNTINCR(wlc_hw->wlc->pub->_cnt->unchained);
#else
		head = PKTLINK(head);
		PKTSETLINK(p, NULL);
#endif

		/* prefetch the headers */
		if (head != NULL) {
			WLPREFHDRS(PKTDATA(wlc_hw->osh, head), PREFSZ);
		}

		/* record the tsf_l in wlc_rxd11hdr */
		wlc_rxhdr = (wlc_d11rxhdr_t *)PKTDATA(wlc_hw->osh, p);
		/* On monolithic driver, write tsf in host byte order. rx status already
		 * in host byte order.
		 */
		wlc_rxhdr->tsf_l = tsf_l;
		/* compute the RSSI from d11rxhdr and record it in wlc_rxd11hr */
		phy_rssi_compute_rssi((phy_info_t *)wlc_hw->band->pi, wlc_rxhdr);

		wlc_recv(wlc_hw->wlc, p);
	}

	dpc->processed += n;

	return (n >= bound_limit);
} /* wlc_bmac_recv */

#ifdef WLP2P_UCODE
/** ucode generates p2p specific interrupts. Low level p2p interrupt processing */
void
wlc_p2p_bmac_int_proc(wlc_hw_info_t *wlc_hw)
{
	uint b, i;
	uint8 p2p_interrupts[M_P2P_BSS_MAX];
	uint32 tsf_l, tsf_h;

	ASSERT(DL_P2P_UC(wlc_hw));
	ASSERT(wlc_hw->p2p_shm_base != (uint)~0);

	memset(p2p_interrupts, 0, sizeof(uint8) * M_P2P_BSS_MAX);

	/* collect and clear p2p interrupts */
	for (b = 0; b < M_P2P_BSS_MAX; b ++) {

		for (i = 0; i < M_P2P_I_BLK_SZ; i ++) {
			uint loc = wlc_hw->p2p_shm_base + M_P2P_I(wlc_hw, b, i);

			/* any P2P event/interrupt? */
			if (wlc_bmac_read_shm(wlc_hw, loc) == 0)
				continue;

			/* ACK */
			wlc_bmac_write_shm(wlc_hw, loc, 0);

			/* store */
			p2p_interrupts[b] |= (1 << i);
		}
	}

	wlc_bmac_read_tsf(wlc_hw, &tsf_l, &tsf_h);
#ifdef WLMCNX
	wlc_p2p_int_proc(wlc_hw->wlc, p2p_interrupts, tsf_l, tsf_h);
#endif
} /* wlc_p2p_bmac_int_proc */
#endif /* WLP2P_UCODE */

#ifdef AP	/** PMQ (power management queue) stuff */
/**
 * AP specific. Called by high driver when it detects a switch which is normally already in the bmac
 * queue.
 * For full driver, this function will be called directly as a result of a call to
 * wlc_apps_process_ps_switch,
 *  with no delay, before the end of the wlc_bmac_processpmq loop. No state is necessary.
 *  In case of bmac-high split, it will be called after the high received the rpc
 *  call to wlc_apps_process_ps_switch, AFTER
 *  the end of  the wlc_bmac_processpmq loop.
 *  We need to keep state of what has been received by the high driver.
 */
void BCMFASTPATH
wlc_bmac_process_ps_switch(wlc_hw_info_t *wlc_hw, struct ether_addr *ea, int8 ps_flags)
{
	/*
	   ps_on's highest bits are used like this :
	   - PS_SWITCH_FIFO_FLUSHED : there is no more packets pending
	   - PS_SWITCH_MAC_INVALID  this is not really a switch, just a tx fifo
	   empty  indication. Mac address
	   is not present in the message.
	   - PS_SWITCH_STA_REMOVED : the scb for this mac has been removed by the high driver or
	   is not associated.
	*/
	BCM_REFERENCE(ea);

	/* no more packet pending and no more non-acked switches ... clear the PMQ */
	if (ps_flags & PS_SWITCH_FIFO_FLUSHED)
	{
		wlc_hw->bmac_pmq->tx_draining = 0;
		if (wlc_hw->bmac_pmq->active_entries == 0 &&
		    wlc_hw->bmac_pmq->pmq_read_count) {
			wlc_bmac_clearpmq(wlc_hw);
		}
	}
} /* wlc_bmac_process_ps_switch */

static void
BCMATTACHFN(wlc_bmac_pmq_init)(wlc_hw_info_t *wlc_hw)
{

	wlc_hw->bmac_pmq = (bmac_pmq_t *)MALLOC(wlc_hw->osh, sizeof(bmac_pmq_t));
	if (!wlc_hw->bmac_pmq) {
		WL_ERROR(("BPMQ error. Out of memory !!\n"));
		return;
	}
	memset(wlc_hw->bmac_pmq, 0, sizeof(bmac_pmq_t));
	wlc_hw->bmac_pmq->pmq_size = BMAC_PMQ_SIZE;
}

static void
BCMATTACHFN(wlc_bmac_pmq_delete)(wlc_hw_info_t *wlc_hw)
{
	if (!wlc_hw->bmac_pmq)
		return;
	MFREE(wlc_hw->osh, wlc_hw->bmac_pmq, sizeof(bmac_pmq_t));
	wlc_hw->bmac_pmq = NULL;
}

#ifdef TXQ_MUX
bool BCMFASTPATH
wlc_bmac_processpmq(wlc_hw_info_t *wlc_hw, bool bounded)
{
	volatile uint16 *pmqctrlstatus;

	BCM_REFERENCE(bounded);

	WL_ERROR(("-------------------------------->PS : clearing ucode PMQ\n"));

	pmqctrlstatus = (volatile uint16 *)&wlc_hw->regs->pmqreg.w.pmqctrlstatus;
	/* Clear the PMQ entry unless we are letting the data fifo drain
	 * when txstatus indicates unlocks the data fifo we clear
	 * the PMQ of any processed entries
	 */

	W_REG(wlc_hw->osh, pmqctrlstatus, PMQH_DEL_MULT);
	wlc_hw->bmac_pmq->pmq_read_count = 0;

	return FALSE;
}
#else
bool BCMFASTPATH
wlc_bmac_processpmq(wlc_hw_info_t *wlc_hw, bool bounded)
{
	volatile uint32 *pmqhostdata;
	uint32 pmqdata;
	d11regs_t *regs = wlc_hw->regs;
	uint32 pat_hi, pat_lo;
	struct ether_addr eaddr;
	bmac_pmq_t *pmq = wlc_hw->bmac_pmq;
	int8 ps_on, ps_pretend, read_count = 0;
	bool pmq_need_resched = FALSE;

#if defined(BCMDBG_ERR)
	char eabuf[ETHER_ADDR_STR_LEN];
#endif

	pmqhostdata = (volatile uint32 *)&regs->pmqreg.pmqhostdata;

	/* read entries until empty or pmq exeeding limit */
	while ((pmqdata = R_REG(wlc_hw->osh, pmqhostdata)) & PMQH_NOT_EMPTY) {

		pat_lo = R_REG(wlc_hw->osh, &regs->pmqpatl);
		pat_hi = R_REG(wlc_hw->osh, &regs->pmqpath);
		eaddr.octet[5] = (pat_hi >> 8)  & 0xff;
		eaddr.octet[4] =  pat_hi	& 0xff;
		eaddr.octet[3] = (pat_lo >> 24) & 0xff;
		eaddr.octet[2] = (pat_lo >> 16) & 0xff;
		eaddr.octet[1] = (pat_lo >> 8)  & 0xff;
		eaddr.octet[0] =  pat_lo	& 0xff;

		read_count++;
		pmq->pmq_read_count++;

		if (ETHER_ISMULTI(eaddr.octet)) {
			WL_ERROR(("wl%d: wlc_bmac_processpmq:"
				" skip entry with mc/bc address %s\n",
				wlc_hw->unit, bcm_ether_ntoa(&eaddr, eabuf)));
			continue;
		}

		ps_on = (pmqdata & PMQH_PMON) ? PS_SWITCH_PMQ_ENTRY : PS_SWITCH_OFF;
		ps_pretend = (pmqdata & PMQH_PMPS) ? PS_SWITCH_PMQ_PSPRETEND : PS_SWITCH_OFF;
		wlc_apps_process_ps_switch(wlc_hw->wlc, &eaddr, ps_on | ps_pretend);
		/* if we exceed the per invocation pmq entry processing limit,
		 * reschedule again (only if bounded) to process the remaining pmq entries at a
		 * later time.
		 */
		if (bounded &&
		    (read_count >= pmq->pmq_size - BMAC_PMQ_MIN_ROOM)) {
			pmq_need_resched = TRUE;
			break;
		}
	}

	return pmq_need_resched;
} /* wlc_bmac_processpmq */
#endif /* TXQ_MUX */

/**
 * AP specific. Read and drain all the PMQ entries while not EMPTY.
 * When PMQ handling is enabled (MCTL_DISCARD_PMQ in maccontrol is clear),
 * one PMQ entry per packet received from a STA is created with corresponding 'ea' as key.
 * AP reads the entry and handles the PowerSave mode transitions of a STA by
 * comparing the PMQ entry with current PS-state of the STA. If PMQ entry is same as the
 * driver state, it's ignored, else transition is handled.
 *
 * With MBSS code, ON PMQ entries are also added for BSS configs; they are
 * ignored by the SW.
 *
 * Note that PMQ entries remain in the queue for the ucode to search until
 * an explicit delete of the entries is done with PMQH_DEL_MULT (or DEL_ENTRY).
 */
static void
wlc_bmac_clearpmq(wlc_hw_info_t *wlc_hw)
{
	volatile uint16 *pmqctrlstatus;
	d11regs_t *regs = wlc_hw->regs;

	if (!wlc_hw->bmac_pmq->pmq_read_count)
		return;

	WL_PS(("PS : clearing ucode PMQ\n"));

	pmqctrlstatus = (volatile uint16 *)&regs->pmqreg.w.pmqctrlstatus;
	/* Clear the PMQ entry unless we are letting the data fifo drain
	 * when txstatus indicates unlocks the data fifo we clear
	 * the PMQ of any processed entries
	 */

	W_REG(wlc_hw->osh, pmqctrlstatus, PMQH_DEL_MULT);
	wlc_hw->bmac_pmq->pmq_read_count = 0;
}

#endif /* AP */

/**
 * Used for test functionality (packet engine / diagnostics), or for BMAC and offload firmware
 * builds.
 */
void
wlc_bmac_txfifo(wlc_hw_info_t *wlc_hw, uint fifo, void *p,
	bool commit, uint16 frameid, uint8 txpktpend)
{
	ASSERT(p);

	/* bump up pending count */
	if (commit) {
		TXPKTPENDINC(wlc_hw->wlc, fifo, txpktpend);
		wlc_low_txq_buffered_inc(wlc_hw->wlc->active_queue->low_txq,
			fifo, txpktpend);

		WL_TRACE(("wlc_bmac_txfifo, pktpend inc %d to %d\n", txpktpend,
			TXPKTPENDGET(wlc_hw->wlc, fifo)));
	}

	if (!PIO_ENAB_HW(wlc_hw)) {
		/* Commit BCMC sequence number in the SHM frame ID location */
		if (frameid != INVALIDFID) {
			wlc_bmac_write_shm(wlc_hw, M_BCMC_FID(wlc_hw), frameid);
		}

		if (dma_txfast(wlc_hw->di[fifo], p, commit) < 0) {
			PKTFREE(wlc_hw->osh, p, TRUE);
			WL_ERROR(("wlc_bmac_txfifo: fatal, toss frames !!!\n"));
			if (commit) {
				TXPKTPENDDEC(wlc_hw->wlc, fifo, txpktpend);
				wlc_low_txq_buffered_dec(wlc_hw->wlc->active_queue->low_txq,
					fifo, txpktpend);
			}
		}
	} else {
		wlc_pio_tx(wlc_hw->pio[fifo], p);
	}
} /* wlc_bmac_txfifo */

/** Periodic tasks are carried out by a watchdog timer that is called once every second */
void
wlc_bmac_watchdog(void *arg)
{
	wlc_info_t *wlc = (wlc_info_t*)arg;
	wlc_hw_info_t *wlc_hw = wlc->hw;
	uint i = 0;
	WL_TRACE(("wl%d: wlc_bmac_watchdog\n", wlc_hw->unit));

	if (!wlc_hw->up)
		return;

	ASSERT(!wlc_hw->sbclk || !si_taclear(wlc_hw->sih, TRUE));

	/* increment second count */
	wlc_hw->now++;

	/* Check for FIFO error interrupts */
	wlc_bmac_fifoerrors(wlc_hw);

	/* make sure RX dma has buffers */
	if (!PIO_ENAB_HW(wlc_hw)) {
		/* DMA RXFILL */
		for (i = 0; i < MAX_RX_FIFO; i++) {
			if ((wlc_hw->di[i] != NULL) && wlc_bmac_rxfifo_enab(i)) {
				dma_rxfill(wlc_hw->di[i]);
			}
		}
	}

#ifdef LPAS
	/* In LPAS mode phy_wdog is invoked from tbtt only. It can be upto
	 * 2 secs between two phy_wdog invokations.
	 */
	if (!wlc->lpas)
#endif /* LPAS */
	{
		phy_watchdog((phy_info_t *)wlc_hw->band->pi);
	}
	ASSERT(!wlc_hw->sbclk || !si_taclear(wlc_hw->sih, TRUE));
} /* wlc_bmac_watchdog */

/** bmac rpc agg watchdog code, called once every couple of milliseconds */
void
wlc_bmac_rpc_agg_watchdog(void *arg)
{
	wlc_info_t *wlc = (wlc_info_t*)arg;
	wlc_hw_info_t *wlc_hw = wlc->hw;

	WL_TRACE(("wl%d: wlc_bmac_rpc_agg_watchdog\n", wlc_hw->unit));

	if (!wlc_hw->up)
		return;
}

#ifdef SRHWVSDB
/** Higher MAC requests to activate SRVSDB operation for the currently selected band */
int wlc_bmac_activate_srvsdb(wlc_hw_info_t *wlc_hw, chanspec_t chan0, chanspec_t chan1)
{
	int err = BCME_ERROR;

	BCM_REFERENCE(chan0);
	BCM_REFERENCE(chan1);

	wlc_hw->sr_vsdb_active = FALSE;

	if (SRHWVSDB_ENAB(wlc_hw->wlc->pub) &&
	    wlc_phy_attach_srvsdb_module(wlc_hw->band->pi, chan0, chan1)) {
		wlc_hw->sr_vsdb_active = TRUE;
		err = BCME_OK;
	}

	return err;
}

/** Higher MAC requests to deactivate SRVSDB operation for the currently selected band */
void wlc_bmac_deactivate_srvsdb(wlc_hw_info_t *wlc_hw)
{
	wlc_hw->sr_vsdb_active = FALSE;

	if (SRHWVSDB_ENAB(wlc_hw->wlc->pub)) {
		wlc_phy_detach_srvsdb_module(wlc_hw->band->pi);
	}

}

/** All SW inits needed for band change */
static void
wlc_bmac_srvsdb_set_band(wlc_hw_info_t *wlc_hw, uint bandunit, chanspec_t chanspec)
{
	uint32 macintmask;
	uint32 mc;
	wlc_info_t *wlc = wlc_hw->wlc;

	BCM_REFERENCE(chanspec);

	ASSERT(NBANDS_HW(wlc_hw) > 1);
	ASSERT(bandunit != wlc_hw->band->bandunit);
	ASSERT(si_iscoreup(wlc_hw->sih));
	ASSERT((R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) & MCTL_EN_MAC) == 0);

	/* disable interrupts */
	macintmask = wl_intrsoff(wlc->wl);

	wlc_setxband(wlc_hw, bandunit);

	/* bsinit */
	wlc_ucode_bsinit(wlc_hw);

	mc = R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol);
	if ((mc & MCTL_EN_MAC) != 0) {
		if (mc == 0xffffffff)
			WL_ERROR(("wl%d: wlc_phy_init: chip is dead !!!\n", wlc_hw->unit));
		else
			WL_ERROR(("wl%d: wlc_phy_init:MAC running! mc=0x%x\n",
				wlc_hw->unit, mc));

		ASSERT((const char*)"wlc_phy_init: Called with the MAC running!" == NULL);
	}

	/* check D11 is running on Fast Clock */
	ASSERT(si_core_sflags(wlc_hw->sih, 0, 0) & SISF_FCLKA);

	/* cwmin is band-specific, update hardware with value for current band */
	wlc_bmac_set_cwmin(wlc_hw, wlc_hw->band->CWmin);
	wlc_bmac_set_cwmax(wlc_hw, wlc_hw->band->CWmax);

	wlc_bmac_update_slot_timing(wlc_hw,
	        BAND_5G(wlc_hw->band->bandtype) ? TRUE : wlc_hw->shortslot);

#ifdef WL11N
	/* initialize the txphyctl1 rate table since shmem is shared between bands */
	wlc_upd_ofdm_pctl1_table(wlc_hw);
#endif
	/* Configure BTC GPIOs as bands change */
	if (BAND_5G(wlc_hw->band->bandtype))
		wlc_bmac_mhf(wlc_hw, MHF5, MHF5_BTCX_DEFANT, MHF5_BTCX_DEFANT, WLC_BAND_ALL);
	else
		wlc_bmac_mhf(wlc_hw, MHF5, MHF5_BTCX_DEFANT, 0, WLC_BAND_ALL);

	/*
	 * If there are any pending software interrupt bits,
	 * then replace these with a harmless nonzero value
	 * so wlc_dpc() will re-enable interrupts when done.
	 */
	if (wlc_hw->macintstatus)
		wlc_hw->macintstatus = MI_DMAINT;

	/* restore macintmask */
	wl_intrsrestore(wlc->wl, macintmask);
} /* wlc_bmac_srvsdb_set_band */

/** optionally switches band */
static uint8
wlc_bmac_srvsdb_set_chanspec(wlc_hw_info_t *wlc_hw, chanspec_t chanspec, bool mute,
	bool fastclk, uint bandunit, bool band_change)
{
	uint8 switched;
	uint8 last_chan_saved = FALSE;

	/* if excursion is active (scan), switch to normal switch */
	/* mode, else allow sr switching when scan periodically */
	/* returns to home channel */
	if (wlc_hw->wlc->excursion_active)
		return FALSE;

	/* calls the PHY to switch */
	switched = wlc_set_chanspec_sr_vsdb(wlc_hw->band->pi, chanspec, &last_chan_saved);

	/*
	 * note: for bmac firmware this is an asynchronous call. Given caller supplied flags,
	 * optionally saves / restores PPR (power per rate, tx power control related) context.
	 */
	wlc_srvsdb_switch_ppr(wlc_hw->wlc, chanspec, last_chan_saved, switched);

	/* If phy context changed from SRVSDB, continue, otherwise return */
	if (!switched)
		return FALSE;

	/* SW init required after SRVSDB switch */
	if (band_change) {
		wlc_bmac_srvsdb_set_band(wlc_hw, bandunit, chanspec);

		if (CHSPEC_IS2G(chanspec)) {
			wlc_hw->band->bandtype = 2;
			wlc_hw->band->bandunit = 0;
		} else {
			wlc_hw->band->bandunit = 1;
			wlc_hw->band->bandtype = 1;
		}
	}

	/* come out of mute */
	wlc_bmac_mute(wlc_hw, mute, 0);

	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);

	/* Successfull SRVSDB switch */
	return TRUE;
} /* wlc_bmac_srvsdb_set_chanspec */
#endif /* SRHWVSDB */

/**
 * higher MAC requested a channel change, optionally to a different band
 * Parameters:
 *    mute : 'TRUE' for a 'quiet' channel, a channel on which no transmission is (yet) permitted. In
 *           that case, all txfifo's are suspended.
 */
void
wlc_bmac_set_chanspec(wlc_hw_info_t *wlc_hw, chanspec_t chanspec, bool mute, ppr_t *txpwr)
{
	bool fastclk;
	uint bandunit = 0;
#ifdef SRHWVSDB
	uint8 vsdb_switch = 0;
	uint8 vsdb_status = 0;

	if (SRHWVSDB_ENAB(wlc_hw->pub)) {
		vsdb_switch =  wlc_hw->sr_vsdb_force;
#if defined(WLMCHAN) && defined(SR_ESSENTIALS)
		vsdb_switch |= (wlc_hw->sr_vsdb_active &&
		                sr_engine_enable(wlc_hw->sih, IOV_GET, FALSE) > 0);
#endif /* WLMCHAN SR_ESSENTIALS */
	}
#endif /* SRHWVSDB */

	WL_TRACE(("wl%d: wlc_bmac_set_chanspec 0x%x\n", wlc_hw->unit, chanspec));

	/* request FAST clock if not on */
	if (!(fastclk = wlc_hw->forcefastclk))
		wlc_clkctl_clk(wlc_hw, CLK_FAST);

	wlc_hw->chanspec = chanspec;

	WL_TSLOG(wlc_hw->wlc, __FUNCTION__, TS_ENTER, 0);
	/* Switch bands if necessary */
	if (NBANDS_HW(wlc_hw) > 1) {
		bandunit = CHSPEC_WLCBANDUNIT(chanspec);
		if (wlc_hw->band->bandunit != bandunit) {
#ifdef SRHWVSDB
			/* wlc_bmac_setband disables other bandunit,
			 *  use light band switch if not up yet
			 */
			if (wlc_hw->up) {
				if (SRHWVSDB_ENAB(wlc_hw->pub) && vsdb_switch) {
					vsdb_status = wlc_bmac_srvsdb_set_chanspec(wlc_hw,
						chanspec, mute, fastclk, bandunit, TRUE);
					if (vsdb_status) {
						WL_INFORM(("SRVSDB Switch DONE successfully \n"));
						WL_TSLOG(wlc_hw->wlc, __FUNCTION__, TS_EXIT, 0);
						return;
					}
				}
			}
#endif /* SRHWVSDB */
			if (wlc_hw->up) {
				wlc_phy_chanspec_radio_set(wlc_hw->bandstate[bandunit]->pi,
					chanspec);
				wlc_bmac_setband(wlc_hw, bandunit, chanspec);
			} else {
				wlc_setxband(wlc_hw, bandunit);
			}
		}
	}

	wlc_phy_initcal_enable(wlc_hw->band->pi, !mute);

	if (!wlc_hw->up) {
		if (wlc_hw->clk)
			wlc_phy_txpower_limit_set(wlc_hw->band->pi, txpwr, chanspec);
		wlc_phy_chanspec_radio_set(wlc_hw->band->pi, chanspec);
	} else {
		/* Update muting of the channel.
		 * wlc_phy_chanspec_set may start a wlc_phy_cal_perical, to prevent emitting energy
		 * on a muted channel, muting of the channel is updated before hand.
		 */
		wlc_bmac_mute(wlc_hw, mute, 0);

		if ((wlc_hw->deviceid == BCM4360_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4335_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4345_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM43455_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM43602_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4350_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM43556_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM43558_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM43566_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM43568_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM43569_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4354_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4356_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4371_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4358_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4352_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM43012_CHIP_ID) ||
		    (wlc_hw->deviceid == BCM4349_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM53573_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM47189_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4355_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4359_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM43596_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4365_D11AC_ID) ||
		    (wlc_hw->deviceid == BCM4366_D11AC_ID) ||

		    0) {
			/* phymode switch requires phyinit */
			if (phy_init_pending((phy_info_t *)wlc_hw->band->pi))
				phy_init((phy_info_t *)wlc_hw->band->pi, chanspec);
			else
				wlc_phy_chanspec_set(wlc_hw->band->pi, chanspec);
		} else {
			/* Bandswitch above may end up changing the channel so avoid repetition */
			if (chanspec != phy_utils_get_chanspec((phy_info_t *)wlc_hw->band->pi)) {
#ifdef SRHWVSDB
				if (SRHWVSDB_ENAB(wlc_hw->pub) && vsdb_switch) {
					vsdb_status = wlc_bmac_srvsdb_set_chanspec(wlc_hw,
						chanspec, mute, fastclk, bandunit, FALSE);
					if (vsdb_status) {
						WL_INFORM(("SRVSDB Switch DONE successfully \n"));
						WL_TSLOG(wlc_hw->wlc, __FUNCTION__, TS_EXIT, 0);
						return;
					}
				}
#endif
				wlc_phy_chanspec_set(wlc_hw->band->pi, chanspec);
			}
		}
		wlc_phy_txpower_limit_set(wlc_hw->band->pi, txpwr, chanspec);
	}
	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);
	WL_TSLOG(wlc_hw->wlc, __FUNCTION__, TS_EXIT, 0);
} /* wlc_bmac_set_chanspec */

/**
 * Higher MAC (e.g. as contained in the wl host driver) has to query hardware+firmware
 * attributes to use the same settings as bmac, and has to query capabilities before enabling them.
 */
int
wlc_bmac_revinfo_get(wlc_hw_info_t *wlc_hw, wlc_bmac_revinfo_t *revinfo)
{
	si_t *sih = wlc_hw->sih;
	uint idx;

	revinfo->vendorid = wlc_hw->vendorid;
	revinfo->deviceid = wlc_hw->deviceid;

	revinfo->boardrev = wlc_hw->boardrev;
	revinfo->corerev = wlc_hw->corerev;
	revinfo->sromrev = wlc_hw->sromrev;
	/* srom9 introduced ppr, which requires corerev >= 24 */
	if (wlc_hw->sromrev >= 9) {
		WL_ERROR(("wlc_bmac_attach: srom9 ppr requires corerev >=24"));
		ASSERT(D11REV_GE(wlc_hw->corerev, 24));
	}
	revinfo->chiprev = sih->chiprev;
	revinfo->chip = SI_CHIPID(sih);
	revinfo->chippkg = sih->chippkg;
	revinfo->boardtype = sih->boardtype;
	revinfo->boardvendor = sih->boardvendor;
	revinfo->bustype = sih->bustype;
	revinfo->buscoretype = sih->buscoretype;
	revinfo->buscorerev = sih->buscorerev;
	revinfo->issim = sih->issim;
	revinfo->boardflags = wlc_hw->boardflags;
	revinfo->boardflags2 = wlc_hw->boardflags2;
	if (wlc_hw->sromrev >= 12)
		revinfo->boardflags4 = wlc_hw->boardflags4;

	revinfo->nbands = NBANDS_HW(wlc_hw);

	for (idx = 0; idx < NBANDS_HW(wlc_hw); idx++) {
		wlc_hwband_t *band;

		/* So if this is a single band 11a card, use band 1 */
		if (IS_SINGLEBAND_5G(wlc_hw->deviceid))
			idx = BAND_5G_INDEX;

		band = wlc_hw->bandstate[idx];
		revinfo->band[idx].bandunit = band->bandunit;
		revinfo->band[idx].bandtype = band->bandtype;
		revinfo->band[idx].phytype = band->phytype;
		revinfo->band[idx].phyrev = band->phyrev;
		revinfo->band[idx].phy_minor_rev = band->phy_minor_rev;
		revinfo->band[idx].radioid = band->radioid;
		revinfo->band[idx].radiorev = band->radiorev;
		revinfo->band[idx].anarev = 0;

	}

	revinfo->_wlsrvsdb = wlc_hw->wlc->pub->_wlsrvsdb;

	return BCME_OK;
} /* wlc_bmac_revinfo_get */

int
wlc_bmac_state_get(wlc_hw_info_t *wlc_hw, wlc_bmac_state_t *state)
{
	state->machwcap = wlc_hw->machwcap;
	state->preamble_ovr = (uint32)wlc_phy_preamble_override_get(wlc_hw->band->pi);

	return 0;
}

#ifdef DMATXRC
/**
 * improves # of free packets by recycling Tx packets faster: on D11 DMA IRQ instead of on D11 Tx
 * Complete IRQ.
 */
static void
wlc_phdr_handle(wlc_hw_info_t *wlc_hw, void *p)
{
	bool found, processed;
	void *pdata;
	osl_t *osh = wlc_hw->osh;
	wlc_info_t *wlc = wlc_hw->wlc;
	txrc_ctxt_t *rctxt;

	BCM_REFERENCE(wlc);

	processed = TRUE;
	found = (WLPKTTAG(p)->flags & (WLF_PHDR)) ? TRUE : FALSE;
	if (found) {
		pdata = PKTNEXT(osh, p);
		if (pdata != NULL) {
#ifndef PKTC_TX_DONGLE
			/* Only apply if not using pkt chaining */
			ASSERT(PKTNEXT(osh, pdata) == NULL);
#endif
			rctxt = TXRC_PKTTAIL(osh, p);
			ASSERT(TXRC_ISMARKER(rctxt));

#ifdef PROP_TXSTATUS
			/*
			send credit update only if this packet came from the host
			and this was not sent to a vFIFO (i.e. for a psq)
			*/
			if (PROP_TXSTATUS_ENAB(wlc->pub)) {
				uint32 whinfo = WLPKTTAG(p)->wl_hdr_information;
				void *p2;
#ifdef PKTC_TX_DONGLE
				int i = 0;
#endif

			   if ((WL_TXSTATUS_GET_FLAGS(whinfo) & WLFC_PKTFLAG_PKTFROMHOST) &&
			      !(WL_TXSTATUS_GET_FLAGS(whinfo) & WLFC_PKTFLAG_PKT_REQUESTED))
					processed = TRUE;
				else
					processed = FALSE;

				for (p2 = p; processed && p2; p2 = PKTNEXT(osh, p2)) {
					whinfo = WLPKTTAG(p2)->wl_hdr_information;

					/* whinfo is zero if prepended with phdr so only
					 * processed pkts with valid wl_hdr_information
					 */
					if (whinfo) {
						/* if this packet was intended for AC FIFO and ac
						 * credit has not been sent back,push a credit here
						 */
						if (WLFC_CONTROL_SIGNALS_TO_HOST_ENAB(wlc->pub) &&
							(!(WL_TXSTATUS_GET_FLAGS(whinfo) &
							WLFC_PKTFLAG_PKT_REQUESTED)) &&
							(!(WLPKTTAG(p2)->flags & WLF_CREDITED))) {
							wlfc_push_credit_data(wlc->wlfc, p2);
						}
#ifdef PKTC_TX_DONGLE
						/* Save starting with second pkt since phdr
						 * already has wl_hdr_information for first pkt
						 */
						if (PKTC_ENAB(wlc->pub) && (i > 0)) {
							/* Clear so PKTFREE() callback does not
							 * process wlhdr info
							 */
							WLPKTTAG(p2)->wl_hdr_information = 0;

							ASSERT(rctxt->n < ARRAYSIZE(rctxt->wlhdr));
							rctxt->wlhdr[rctxt->n] = whinfo;
							rctxt->seq[rctxt->n] = WLPKTTAG(p2)->seq;
							rctxt->n++;
						}
						i++; /* Next */
#endif /* PKTC_TX_DONGLE */
					}
				}

				if (processed) {
#ifdef PKTC_TX_DONGLE
					if (PKTC_ENAB(wlc->pub) && (rctxt->n))
						TXRC_SETWLHDR(rctxt);
#endif
				}
			}
#endif /* PROP_TXSTATUS */

			/* For proptxstatus, don't free if not processed so we can return
			 * info back to host
			 */
			if (processed) {
				PKTSETNEXT(osh, p, NULL);
				TXRC_SETRECLAIMED(rctxt);
#ifdef BCMDBG_POOL
				ASSERT(PKTPOOLSTATE(pdata) == POOL_TXD11);
#endif
				PKTFREE(osh, pdata, TRUE);
			}
		}
	}
} /* wlc_phdr_handle */

static void
wlc_dmatx_peekall(wlc_hw_info_t *wlc_hw, hnddma_t *di)
{
	void *phdr;
	int i, n;
	wlc_info_t *wlc = wlc_hw->wlc;

	ASSERT(wlc->phdr_list);

	n = wlc->phdr_len;
	if (dma_peekntxp(di, &n, wlc->phdr_list, HNDDMA_RANGE_TRANSFERED) != BCME_OK) {
		WL_ERROR(("wl%d: %s DMA range transfer failed with range param %d \n",
			wlc->pub->unit, __FUNCTION__, HNDDMA_RANGE_TRANSFERED));
		return;
	}

	for (i = 0; i < n; i++) {
		phdr = wlc->phdr_list[i];
		ASSERT(phdr);
		if (phdr != NULL) {
			wlc_phdr_handle(wlc_hw, phdr);
			wlc->phdr_list[i] = NULL;
		}
	}

}

/** early tx packet reclaim to improve memory usage */
void
wlc_dmatx_reclaim(wlc_hw_info_t *wlc_hw)
{
	int i;
	hnddma_t *di;
	wlc_info_t *wlc = wlc_hw->wlc;

	/*
	 * A couple of issues impacting tput with early reclaim:
	 * 1.  Doing pktpool avail callbacks has tput impact;
	 *     Temporarily disable callbacks while processing tx reclaim.
	 * 2.  Wasting cycles on fifos not enabled for early reclaim
	 */
	pktpool_emptycb_disable(wlc->pub->pktpool, TRUE);

	for (i = 0; i < NFIFO; i++) {
		if (!(wlc->txrc_fifo_mask & (1 << i)))
			continue;

		di = wlc_hw->di[i];
		if (di)
			wlc_dmatx_peekall(wlc_hw, di);
	}

	/* Re-enable pktpool avail callbacks */
	pktpool_emptycb_disable(wlc->pub->pktpool, FALSE);
}

#endif /* DMATXRC */

#ifdef BCMDBG_POOL
/**
 * Packet pool separates memory allocation for packets from other allocations, making the system
 * more robust to low memory conditions, and prevents DMA error conditions by reusing recently freed
 * packets in a faster manner.
 */
static void
wlc_pktpool_dbg_cb(pktpool_t *pool, void *arg)
{
	wlc_hw_info_t *wlc_hw = arg;
	pktpool_stats_t pstats;

	if (wlc_hw == NULL)
		return;

	if (wlc_hw->up == FALSE)
		return;

	WL_ERROR(("wl: post=%d rxactive=%d txactive=%d txpend=%d\n",
		NRXBUFPOST,
		dma_rxactive(wlc_hw->di[RX_FIFO]),
		dma_txactive(wlc_hw->di[1]),
		dma_txpending(wlc_hw->di[1])));

	pktpool_stats_dump(pool, &pstats);
	WL_ERROR(("pool len=%d\n", pktpool_len(pool)));
	WL_ERROR(("txdh:%d txd11:%d enq:%d rxdh:%d rxd11:%d rxfill:%d idle:%d\n",
		pstats.txdh, pstats.txd11, pstats.enq,
		pstats.rxdh, pstats.rxd11, pstats.rxfill, pstats.idle));
}
#endif /* BCMDBG_POOL */

static void
wlc_pktpool_empty_cb(pktpool_t *pool, void *arg)
{
	wlc_hw_info_t *wlc_hw = arg;
	BCM_REFERENCE(pool);

	if (wlc_hw == NULL)
		return;

	if (wlc_hw->up == FALSE)
		return;

#ifdef DMATXRC
	if (DMATXRC_ENAB(wlc_hw->wlc->pub) && !(PROP_TXSTATUS_ENAB(wlc_hw->wlc->pub)))
		wlc_dmatx_reclaim(wlc_hw);
#endif
}

static void
wlc_pktpool_avail_cb(pktpool_t *pool, void *arg)
{
	wlc_hw_info_t *wlc_hw = arg;

	BCM_REFERENCE(pool);
	BCM_REFERENCE(arg);

	if (wlc_hw == NULL)
		return;

	if (wlc_hw->up == FALSE || wlc_hw->reinit)
		return;

#ifdef	WL_RXEARLYRC
	if (WL_RXEARLYRC_ENAB(wlc_hw->wlc->pub)) {
		if ((wlc_hw->rc_pkt_head == NULL)) {
			if ((dma_activerxbuf(wlc_hw->di[RX_FIFO]) < 4)) {
				void *prev = NULL;
				void *p;
				while ((p = dma_rx(wlc_hw->di[RX_FIFO])) != NULL) {
					if (wlc_hw->rc_pkt_head == NULL) {
						wlc_hw->rc_pkt_head = p;
					} else {
						PKTSETLINK(prev, p);
					}
					prev = p;
				}
			}
		}
	}
#endif /* WL_RXEARLYRC */
	if (!PIO_ENAB_HW(wlc_hw) && wlc_hw->di[RX_FIFO])
		dma_rxfill(wlc_hw->di[RX_FIFO]);
}

#ifdef WLRXOV

/** Throttle tx on rx fifo overflow: flow control related */
void
wlc_rxov_timer(void *arg)
{
	wlc_info_t *wlc = (wlc_info_t*)arg;

	ASSERT(wlc->rxov_active == TRUE);
	if (wlc->rxov_delay > RXOV_TIMEOUT_MIN) {
		/* Gradually back off rxfifo overflow */
		wlc->rxov_delay -= RXOV_TIMEOUT_BACKOFF;

		wl_add_timer(wlc->wl, wlc->rxov_timer, wlc->rxov_delay, FALSE);
		wlc->rxov_active = TRUE;
	} else {
		/* Restore tx params */
		if (N_ENAB(wlc->pub) && AMPDU_MAC_ENAB(wlc->pub))
			wlc_set_txmaxpkts(wlc, MAXTXPKTS_AMPDUMAC);

		wlc->rxov_delay = RXOV_TIMEOUT_MIN;
		wlc->rxov_active = FALSE;

		if (POOL_ENAB(wlc->pub->pktpool))
			pktpool_avail_notify_normal(wlc->osh, SHARED_POOL);
	}
}

void
wlc_rxov_int(wlc_info_t *wlc)
{
	int ret;
	if (wlc->rxov_active == FALSE) {
		if (POOL_ENAB(wlc->pub->pktpool)) {
			ret = pktpool_avail_notify_exclusive(wlc->osh, SHARED_POOL,
				wlc_pktpool_avail_cb);
			if (ret != BCME_OK) {
				WL_ERROR(("wl%d: %s: notify excl fail: %d\n",
					WLCWLUNIT(wlc), __FUNCTION__, ret));
			}
		}
		/*
		 * Throttle tx when hitting rxfifo overflow
		 * Increase rx post??
		 */
		if (N_ENAB(wlc->pub) && AMPDU_MAC_ENAB(wlc->pub))
			wlc_set_txmaxpkts(wlc, wlc->rxov_txmaxpkts);

		wl_add_timer(wlc->wl, wlc->rxov_timer, wlc->rxov_delay, FALSE);
		wlc->rxov_active = TRUE;
	} else {
		/* Re-arm it */
		wlc->rxov_delay = MIN(wlc->rxov_delay*2, RXOV_TIMEOUT_MAX);
		wl_add_timer(wlc->wl, wlc->rxov_timer, wlc->rxov_delay, FALSE);
	}

	if (!PIO_ENAB_HW(wlc->hw) && wlc->hw->di[RX_FIFO])
		dma_rxfill(wlc->hw->di[RX_FIFO]);
}

#endif /* WLRXOV */

#ifdef BCMPCIEDEV
static void
BCMATTACHFN(wlc_bmac_set_dma_burstlen_pcie)(wlc_hw_info_t *wlc_hw, hnddma_t *di)
{
	uint32 devctl;
	uint32 mrrs;
	si_t *sih = wlc_hw->sih;

	si_corereg(sih, sih->buscoreidx, OFFSETOF(sbpcieregs_t, configaddr), ~0, 0xB4);
	devctl = si_corereg(sih, sih->buscoreidx, OFFSETOF(sbpcieregs_t, configdata), 0, 0);
	mrrs = (devctl & PCIE_CAP_DEVCTRL_MRRS_MASK) >> PCIE_CAP_DEVCTRL_MRRS_SHIFT;
	switch (mrrs)
	{
		case PCIE_CAP_DEVCTRL_MRRS_128B:
			dma_param_set(di, HNDDMA_PID_TX_BURSTLEN, DMA_BL_128);
			break;
		case PCIE_CAP_DEVCTRL_MRRS_256B:
			dma_param_set(di, HNDDMA_PID_TX_BURSTLEN, DMA_BL_256);
			break;
		case PCIE_CAP_DEVCTRL_MRRS_512B:
			dma_param_set(di, HNDDMA_PID_TX_BURSTLEN, DMA_BL_512);
			break;
		case PCIE_CAP_DEVCTRL_MRRS_1024B:
			dma_param_set(di, HNDDMA_PID_TX_BURSTLEN, DMA_BL_1024);
			break;
		default:
			ASSERT(0);
			break;
	}

	WL_INFORM(("MRRS Read from config reg %x \n", mrrs));

	if ((BUSCORETYPE(wlc_hw->sih->buscoretype) == PCIE2_CORE_ID) &&
		(wlc_hw->sih->buscorerev ==  5)) {
		dma_param_set(di, HNDDMA_PID_TX_BURSTLEN, DMA_BL_128);
	}

} /* wlc_bmac_set_dma_burstlen_pcie */
#endif /* BCMPCIEDEV */

static uint16
BCMATTACHFN(wlc_bmac_dma_max_outstread)(wlc_hw_info_t *wlc_hw)
{
	uint16 txmr = (TXMR == 2) ? DMA_MR_2 : DMA_MR_1;
	if ((CHIPID(wlc_hw->sih->chip) == BCM4345_CHIP_ID) &&
	    (CHIPREV(wlc_hw->sih->chiprev) >= 2)) {
		txmr = DMA_MR_2;
	} else if (BCM4349_CHIP(wlc_hw->sih->chip)) {
		txmr = DMA_MR_12;
	} else if (BCM53573_CHIP(wlc_hw->sih->chip)) {
		txmr = DMA_MR_12;
	}
	//#endif /* UNRELEASEDCHIP */
	else if (((CHIPID(wlc_hw->sih->chip) == BCM4350_CHIP_ID) &&
	(CHIPREV(wlc_hw->sih->chiprev) >= 3)) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4354_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4356_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4358_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43570_CHIP_ID))
		txmr = DMA_MR_12;
	else if (BCM43602_CHIP(wlc_hw->sih->chip) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4365_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4366_CHIP_ID))
		txmr = DMA_MR_12;
	return txmr;
}

static void
BCMATTACHFN(wlc_bmac_dma_param_set)(wlc_hw_info_t *wlc_hw, uint bustype, hnddma_t *di,
                                    uint16 dmactl[][4])
{
	if (bustype == PCI_BUS) {
		if (D11REV_GE(wlc_hw->corerev, 32)) {
			if ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
			    (CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
			    BCM4350_CHIP(wlc_hw->sih->chip) ||
			    (CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID) ||
			    BCM43602_CHIP(wlc_hw->sih->chip) ||
			    BCM4349_CHIP(wlc_hw->sih->chip) ||
			    BCM53573_CHIP(wlc_hw->sih->chip) ||
			    (CHIPID(wlc_hw->sih->chip) == BCM4335_CHIP_ID) ||
			    (CHIPID(wlc_hw->sih->chip) == BCM4365_CHIP_ID) ||
			    (CHIPID(wlc_hw->sih->chip) == BCM4366_CHIP_ID) ||
				0) {
				uint16 dma_mr = dmactl[DMA_CTL_TX][DMA_CTL_MR];
#ifdef DMA_MRENAB
				dma_mr = wlc_bmac_dma_max_outstread(wlc_hw);
#endif
				dma_param_set(di, HNDDMA_PID_TX_MULTI_OUTSTD_RD, dma_mr);
				dma_param_set(di, HNDDMA_PID_TX_PREFETCH_CTL,
				              dmactl[DMA_CTL_TX][DMA_CTL_PC]);
				dma_param_set(di, HNDDMA_PID_TX_PREFETCH_THRESH,
				              dmactl[DMA_CTL_TX][DMA_CTL_PT]);
				dma_param_set(di, HNDDMA_PID_TX_BURSTLEN,
				              dmactl[DMA_CTL_TX][DMA_CTL_BL]);
				dma_param_set(di, HNDDMA_PID_RX_PREFETCH_CTL,
				              dmactl[DMA_CTL_RX][DMA_CTL_PC]);
				dma_param_set(di, HNDDMA_PID_RX_PREFETCH_THRESH,
				              dmactl[DMA_CTL_RX][DMA_CTL_PT]);
				dma_param_set(di, HNDDMA_PID_RX_BURSTLEN,
				              dmactl[DMA_CTL_RX][DMA_CTL_BL]);
			} else {
				dma_burstlen_set(di, DMA_BL_128, DMA_BL_128);
			}
		}
	} else if (bustype == SI_BUS) {
			if (D11REV_GE(wlc_hw->corerev, 32)) {
				if ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM4345_CHIP_ID) ||
					BCM43602_CHIP(wlc_hw->sih->chip) ||
					BCM4350_CHIP(wlc_hw->sih->chip) ||
					(CHIPID(wlc_hw->sih->chip) == BCM43526_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM4345_CHIP_ID) ||
					BCM4349_CHIP(wlc_hw->sih->chip) ||
					BCM53573_CHIP(wlc_hw->sih->chip) ||
					(CHIPID(wlc_hw->sih->chip) == BCM4365_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM4366_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM4335_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM43012_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM43018_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM43430_CHIP_ID)) {
					dma_param_set(di, HNDDMA_PID_TX_MULTI_OUTSTD_RD,
						wlc_bmac_dma_max_outstread(wlc_hw));

					dma_param_set(di, HNDDMA_PID_TX_PREFETCH_CTL,
						DMA_PC_0);
					dma_param_set(di, HNDDMA_PID_TX_PREFETCH_THRESH,
						DMA_PT_1);
					dma_param_set(di, HNDDMA_PID_TX_BURSTLEN,
						DMA_BL_64);
					dma_param_set(di, HNDDMA_PID_RX_BURSTLEN,
						DMA_BL_64);
					dma_param_set(di, HNDDMA_PID_RX_PREFETCH_CTL,
						DMA_PC_0);
					dma_param_set(di, HNDDMA_PID_RX_PREFETCH_THRESH,
						DMA_PT_1);

#ifdef BCMPCIEDEV

					/* addresses going across the bus */
					if (BCMPCIEDEV_ENAB() &&
					    (BUSCORETYPE(wlc_hw->sih->buscoretype) ==
						PCIE2_CORE_ID)) {
						wlc_bmac_set_dma_burstlen_pcie(wlc_hw, di);
						dma_param_set(di, HNDDMA_PID_BURSTLEN_CAP, 1);
					}
#endif /* BCMPCIEDEV */
				}
			}
	}
} /* wlc_bmac_dma_param_set */

/**
 * D11 core contains several TX FIFO's and one or two RX FIFO's. These FIFO's are fed by either a
 * DMA engine or programmatically (PIO).
 */
static bool
BCMATTACHFN(wlc_bmac_attach_dmapio)(wlc_hw_info_t *wlc_hw, bool wme)
{
	uint i;
	char name[8];
	/* ucode host flag 2 needed for pio mode, independent of band and fifo */
	uint16 pio_mhf2 = 0;
	wlc_info_t *wlc = wlc_hw->wlc;
	uint unit = wlc_hw->unit;
	wlc_tunables_t *tune = wlc->pub->tunables;
	/* For split rx case, we dont want any extra head room */
	/* pkt coming from d11 dma will be used only in PKT RX path */
	/* For RX path, we dont need to grow the packet at head */
	/* Pkt loopback within a dongle case may require some changes with this logic */
	int extraheadroom = (BCMSPLITRX_ENAB()) ? 0 : WLRXEXTHDROOM;
	uint8 splittx_hdr = (BCMLFRAG_ENAB() ? 1 : 0);
	uint nrxd_fifo1 = 0;
	uint rxbufpost_fifo1 = 0;
	bool fifo1_rxen;
#ifdef WME
	bool fifo2_rxen;
#endif /* WME */

	/* name and offsets for dma_attach */
	snprintf(name, sizeof(name), rstr_wlD, unit, 0);

	/* init core's pio or dma channels */
	if (PIO_ENAB_HW(wlc_hw)) {
		if (wlc_hw->pio[0] == 0) {
			pio_t *pio;

			for (i = 0; i < NFIFO; i++) {
				pio = wlc_pio_attach(wlc->pub, wlc, i, &pio_mhf2);
				if (pio == NULL) {
					WL_ERROR(("wlc_attach: pio_attach failed\n"));
					return FALSE;
				}
				wlc_hw_set_pio(wlc_hw, i, pio);
			}
		}
	} else if (wlc_hw->di[RX_FIFO] == 0) {	/* Init FIFOs */

		uint addrwidth;
		osl_t *osh = wlc_hw->osh;
		hnddma_t *di;
		uint nrxd_multiplier = 1;
		static uint16 dmactl[2][4] = {
			/* TX */
			{ DMA_MR_2, DMA_PC_16, DMA_PT_8, DMA_BL_1024 },
			{ 0, DMA_PC_16, DMA_PT_8, DMA_BL_128 },
		};
		/* Use the *_large tunable values for cores that support the larger DMA ring size,
		 * 4k descriptors.
		 */
		uint ntxd = (D11REV_GE(wlc_hw->corerev, 42)) ? tune->ntxd_large : tune->ntxd;
		uint nrxd = (D11REV_GE(wlc_hw->corerev, 42)) ? tune->nrxd_large : tune->nrxd;

		/* Find out the DMA addressing capability and let OS know
		 * All the channels within one DMA core have 'common-minimum' same
		 * capability
		 */
		addrwidth = dma_addrwidth(wlc_hw->sih, DMAREG(wlc_hw, DMA_TX, 0));
		OSL_DMADDRWIDTH(osh, addrwidth);

		if (!wl_alloc_dma_resources(wlc->wl, addrwidth)) {
			WL_ERROR(("wl%d: wlc_attach: alloc_dma_resources failed\n", unit));
			return FALSE;
		}

		STATIC_ASSERT(BCMEXTRAHDROOM >= TXOFF);

		/*
		 * FIFO 0
		 * TX: TX_AC_BK_FIFO (TX AC Background data packets)
		 * RX: RX_FIFO (RX data packets)
		 */
		STATIC_ASSERT(TX_AC_BK_FIFO == 0);
		STATIC_ASSERT(RX_FIFO == 0);

		/* For dongle builds, the nrxd is updated in the chip specific rte Makefile */
		if (D11REV_IS(wlc_hw->corerev, 54) ||
				D11REV_IS(wlc_hw->corerev, 55) ||
				D11REV_IS(wlc_hw->corerev, 57)) {
			/* JIRA:CRWLDOT11M-1776 - For the above corerevs, we will post
			 * multiple descriptors for each packet. Hence increased the nrxd.
			 */
			nrxd_multiplier = 4;
		}
		snprintf(name, sizeof(name), rstr_wlD, unit, 0);
		di = dma_attach(osh, name, wlc_hw->sih,
			(wme ? DMAREG(wlc_hw, DMA_TX, 0) : NULL), DMAREG(wlc_hw, DMA_RX, 0),
			(wme ? ntxd : 0), nrxd*nrxd_multiplier, tune->rxbufsz, extraheadroom,
			tune->nrxbufpost, wlc->hwrxoff, &wl_msg_level);

		if (di == NULL)
			goto dma_attach_fail;

	/* Set separate rx hdr flag only for fifo-0 */
				if (SPLIT_RXMODE1() || SPLIT_RXMODE2())
					dma_param_set(di, HNDDMA_SEP_RX_HDR, 1);

				if (RXFIFO_SPLIT())
					dma_param_set(di, HNDDMA_SPLIT_FIFO, SPLIT_FIFO_0);

		if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) {
			dmactl[DMA_CTL_TX][DMA_CTL_MR] = (TXMR == 2 ? DMA_MR_2 : DMA_MR_1);
			dmactl[DMA_CTL_TX][DMA_CTL_PT] = (TXPREFTHRESH == 8 ? DMA_PT_8 :
			                                  TXPREFTHRESH == 4 ? DMA_PT_4 :
			                                  TXPREFTHRESH == 2 ? DMA_PT_2 : DMA_PT_1);
			dmactl[DMA_CTL_TX][DMA_CTL_PC] = (TXPREFCTL == 16 ? DMA_PC_16 :
			                                  TXPREFCTL == 8 ? DMA_PC_8 :
			                                  TXPREFCTL == 4 ? DMA_PC_4 : DMA_PC_0);
			dmactl[DMA_CTL_TX][DMA_CTL_BL] = (TXBURSTLEN == 1024 ? DMA_BL_1024 :
			                                  TXBURSTLEN == 512 ? DMA_BL_512 :
			                                  TXBURSTLEN == 256 ? DMA_BL_256 :
			                                  TXBURSTLEN == 128 ? DMA_BL_128 :
			                                  TXBURSTLEN == 64 ? DMA_BL_64 :
			                                  TXBURSTLEN == 32 ? DMA_BL_32 : DMA_BL_16);

			dmactl[DMA_CTL_RX][DMA_CTL_PT] =  (RXPREFTHRESH == 8 ? DMA_PT_8 :
			                                   RXPREFTHRESH == 4 ? DMA_PT_4 :
			                                   RXPREFTHRESH == 2 ? DMA_PT_2 : DMA_PT_1);
			dmactl[DMA_CTL_RX][DMA_CTL_PC] = (RXPREFCTL == 16 ? DMA_PC_16 :
			                                  RXPREFCTL == 8 ? DMA_PC_8 :
			                                  RXPREFCTL == 4 ? DMA_PC_4 : DMA_PC_0);
			dmactl[DMA_CTL_RX][DMA_CTL_BL] = (RXBURSTLEN == 1024 ? DMA_BL_1024 :
			                                  RXBURSTLEN == 512 ? DMA_BL_512 :
			                                  RXBURSTLEN == 256 ? DMA_BL_256 :
			                                  RXBURSTLEN == 128 ? DMA_BL_128 :
			                                  RXBURSTLEN == 64 ? DMA_BL_64 :
			                                  RXBURSTLEN == 32 ? DMA_BL_32 : DMA_BL_16);

			wlc_bmac_dma_param_set(wlc_hw, PCI_BUS, di, dmactl);

		} else if (BUSTYPE(wlc_hw->sih->bustype) == SI_BUS) {
			wlc_bmac_dma_param_set(wlc_hw, SI_BUS, di, dmactl);
		}
		wlc_hw_set_di(wlc_hw, 0, di);

		/*
		 * FIFO 1
		 * TX: TX_AC_BE_FIFO (TX AC Best-Effort data packets)
		 *   (legacy) TX_DATA_FIFO (TX data packets)
		 * RX: UNUSED
		 */
		STATIC_ASSERT(TX_AC_BE_FIFO == 1);
		STATIC_ASSERT(TX_DATA_FIFO == 1);
		ASSERT(wlc_hw->di[1] == 0);
		/* if fifo-1 is used for classification, use classiifcation specific tunables */
		fifo1_rxen = (PKT_CLASSIFY_EN(RX_FIFO1) || RXFIFO_SPLIT());
		nrxd_fifo1 = (PKT_CLASSIFY_EN(RX_FIFO1) ? tune->nrxd_classified_fifo : nrxd);
		rxbufpost_fifo1 = (PKT_CLASSIFY_EN(RX_FIFO1) ? tune->bufpost_classified_fifo :
			tune->nrxbufpost);
		extraheadroom = (PKT_CLASSIFY_EN(RX_FIFO1) ? WLRXEXTHDROOM : 0);
#ifdef	FORCE_RX_FIFO1
		/* JIRA CRWLDOT11M-1329 */
		/* in 4349a0, fifo-2 classification will work only if fifo-1 is enabled */
		/* Enable fifo-1, but dont do any posting */
		fifo1_rxen = TRUE;
		nrxd_fifo1 = 1;
		rxbufpost_fifo1 = 0;
#endif /* FORCE_RX_FIFO1 */
			/* Since we are splitting up TCM buffers, increase no of descriptors */
			/* if splitrx is enabled, fifo-1 needs to be inited for rx too */
		snprintf(name, sizeof(name), rstr_wlD, unit, 1);
			di = dma_attach(osh, name, wlc_hw->sih,
			(wme ? DMAREG(wlc_hw, DMA_TX, 1) : NULL),
#ifdef	FORCE_RX_FIFO1
			DMAREG(wlc_hw, DMA_RX, 1),
#else
			(fifo1_rxen ? DMAREG(wlc_hw, DMA_RX, 1) : NULL),
#endif
			(splittx_hdr ? tune->ntxd_lfrag : ntxd),
			nrxd_fifo1, tune->rxbufsz, extraheadroom,
			rxbufpost_fifo1, wlc->hwrxoff, &wl_msg_level);
#ifdef FORCE_RX_FIFO1
		BCM_REFERENCE(fifo1_rxen);
#endif

			if (di == NULL)
			goto dma_attach_fail;

		wlc_bmac_dma_param_set(wlc_hw, BUSTYPE(wlc_hw->sih->bustype), di, dmactl);
		wlc_hw_set_di(wlc_hw, 1, di);

		if (RXFIFO_SPLIT())
			dma_param_set(di, HNDDMA_SPLIT_FIFO, SPLIT_FIFO_1);

#ifdef WME
		/*
		 * FIFO 2
		 * TX: TX_AC_VI_FIFO (TX AC Video data packets)
		 * RX: UNUSED
		 */
		STATIC_ASSERT(TX_AC_VI_FIFO == 2);
		fifo2_rxen = (PKT_CLASSIFY_EN(RX_FIFO2));
		/* if splitrx mode 3 is enabled, fifo-2 needs to be inited for rx too */
		if (wme || fifo2_rxen) {
			snprintf(name, sizeof(name), rstr_wlD, unit, 2);
			di = dma_attach(osh, name, wlc_hw->sih,
				(wme ? DMAREG(wlc_hw, DMA_TX, 2):NULL),
				(fifo2_rxen ? DMAREG(wlc_hw, DMA_RX, 2) : NULL),
				ntxd, tune->nrxd_classified_fifo, tune->rxbufsz,
				WLRXEXTHDROOM, tune->bufpost_classified_fifo,
				wlc->hwrxoff, &wl_msg_level);

				if (di == NULL) {
				goto dma_attach_fail;
				}

			wlc_bmac_dma_param_set(wlc_hw, BUSTYPE(wlc_hw->sih->bustype), di, dmactl);

			wlc_hw_set_di(wlc_hw, 2, di);
		}
#endif /* WME */

		/*
		 * FIFO 3
		 * TX: TX_AC_VO_FIFO (TX AC Voice data packets)
		 *   (legacy) TX_CTL_FIFO (TX control & mgmt packets)
		 * RX: RX_TXSTATUS_FIFO (transmit-status packets)
		 *	for corerev < 5 only
		 */
		STATIC_ASSERT(TX_AC_VO_FIFO == 3);
		STATIC_ASSERT(TX_CTL_FIFO == 3);
		di = dma_attach(osh, name, wlc_hw->sih,
			DMAREG(wlc_hw, DMA_TX, 3), NULL, ntxd, 0, 0, -1, 0, 0,
			&wl_msg_level);

		if (di == NULL) {
			goto dma_attach_fail;
		}

		wlc_bmac_dma_param_set(wlc_hw, BUSTYPE(wlc_hw->sih->bustype), di, dmactl);

		wlc_hw_set_di(wlc_hw, 3, di);

#ifdef AP
		/*
		 * FIFO 4
		 * TX: TX_BCMC_FIFO (TX broadcast & multicast packets)
		 * RX: UNUSED
		 */

		STATIC_ASSERT(TX_BCMC_FIFO == 4);
		di = dma_attach(osh, name, wlc_hw->sih,
			DMAREG(wlc_hw, DMA_TX, 4), NULL, ntxd, 0, 0, -1, 0, 0, &wl_msg_level);
		if (di == NULL)
			goto dma_attach_fail;

		wlc_bmac_dma_param_set(wlc_hw, BUSTYPE(wlc_hw->sih->bustype), di, dmactl);

		wlc_hw_set_di(wlc_hw, 4, di);
#endif /* AP */

#if defined(MBSS) || defined(WLAIBSS) || defined(WLWNM_AP)
		/*
		 * FIFO 5: TX_ATIM_FIFO
		 * TX: MBSS: but used for beacon/probe resp pkts
		 * TX: WNM_AP: used for TIM Broadcast frames
		 * RX: UNUSED
		 */
		di = dma_attach(osh, name, wlc_hw->sih,
			DMAREG(wlc_hw, DMA_TX, 5), NULL, ntxd, 0, 0, -1, 0, 0,
			&wl_msg_level);
		if (di == NULL)
			goto dma_attach_fail;

		wlc_bmac_dma_param_set(wlc_hw,
			BUSTYPE(wlc_hw->sih->bustype), di, dmactl);

		wlc_hw_set_di(wlc_hw, 5, di);
#endif /* MBSS || WLAIBSS */

		/* get pointer to dma engine tx flow control variable */
		for (i = 0; i < NFIFO; i++)
			if (wlc_hw->di[i]) {
				wlc_hw->txavail[i] =
					(uint*)dma_getvar(wlc_hw->di[i], "&txavail");
				/* FIXIT: shouldn't be resetting to 64 bytes unconditionally */
#ifdef BCMPCIEDEV
				if (!BCMPCIEDEV_ENAB())
#endif /* BCMPCIEDEV */
				{
					if (CHIPID(wlc_hw->sih->chip) == BCM43143_CHIP_ID) {
					  dma_param_set(wlc_hw->di[i], HNDDMA_PID_TX_BURSTLEN,
					                DMA_BL_64);
					  dma_param_set(wlc_hw->di[i], HNDDMA_PID_RX_BURSTLEN,
					                DMA_BL_64);
					}
				}
			}
	}

	/* initial ucode host flags */
	wlc_mhfdef(wlc_hw, wlc_hw->band->mhfs, pio_mhf2);

	if (BCMSPLITRX_ENAB()) {
		/* enable host flags to do ucode frame classification */
		wlc_bmac_enable_rx_hostmem_access(wlc_hw, TRUE);
		wlc_mhf(wlc, MHF3, MHF3_SELECT_RXF1, MHF3_SELECT_RXF1, WLC_BAND_ALL);
	}

	if (PKT_CLASSIFY()) {
			/* enable host flags to do ucode frame classification */
		wlc_bmac_enable_rx_hostmem_access(wlc_hw, TRUE);
	}

	if (RXFIFO_SPLIT())
			dma_link_handle(wlc_hw->di[RX_FIFO1], wlc_hw->di[RX_FIFO]);


	return TRUE;

dma_attach_fail:
	WL_ERROR(("wl%d: wlc_attach: dma_attach failed\n", unit));
	return FALSE;
} /* wlc_bmac_dma_param_set */

static void
BCMATTACHFN(wlc_bmac_detach_dmapio)(wlc_hw_info_t *wlc_hw)
{
	uint j;

	for (j = 0; j < NFIFO; j++) {
		if (!PIO_ENAB_HW(wlc_hw)) {
			if (wlc_hw->di[j]) {
				dma_detach(wlc_hw->di[j]);
				wlc_hw_set_di(wlc_hw, j, NULL);
			}
		} else {
			if (wlc_hw->pio[j]) {
				wlc_pio_detach(wlc_hw->pio[j]);
				wlc_hw_set_pio(wlc_hw, j, NULL);
			}
		}
	}
}

#define GPIO_4_BTSWITCH          (1 << 4)
#define GPIO_4_GPIOOUT_DEFAULT    0
#define GPIO_4_GPIOOUTEN_DEFAULT  0

/** Bluetooth switch drives multiple outputs */
int
wlc_bmac_set_btswitch(wlc_hw_info_t *wlc_hw, int8 state)
{
	if (((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
	     (CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID)) &&
	    ((wlc_hw->sih->boardtype == BCM94331X28) ||
	     (wlc_hw->sih->boardtype == BCM94331X28B) ||
	     (wlc_hw->sih->boardtype == BCM94331CS_SSID) ||
	     (wlc_hw->sih->boardtype == BCM94331X29B) ||
	     (wlc_hw->sih->boardtype == BCM94331X29D))) {
		if (state == AUTO) {
			/* default */
			if (wlc_hw->up) {
				wlc_bmac_set_ctrl_bt_shd0(wlc_hw, TRUE);
			}
			si_gpioout(wlc_hw->sih, GPIO_4_BTSWITCH, GPIO_4_GPIOOUT_DEFAULT,
			           GPIO_DRV_PRIORITY);
			si_gpioouten(wlc_hw->sih, GPIO_4_BTSWITCH, GPIO_4_GPIOOUTEN_DEFAULT,
			             GPIO_DRV_PRIORITY);
		} else {
			uint32 val = 0;
			if (state == ON) {
				val = GPIO_4_BTSWITCH;
			}
			wlc_bmac_set_ctrl_bt_shd0(wlc_hw, FALSE);

			si_gpioout(wlc_hw->sih, GPIO_4_BTSWITCH, val, GPIO_DRV_PRIORITY);
			si_gpioouten(wlc_hw->sih, GPIO_4_BTSWITCH, GPIO_4_BTSWITCH,
			             GPIO_DRV_PRIORITY);
		}
		/* Save switch state */
		wlc_hw->btswitch_ovrd_state = state;
		return BCME_OK;
	} else if (WLCISACPHY(wlc_hw->band)) {
		if (wlc_hw->up) {
			wlc_phy_set_femctrl_bt_wlan_ovrd(wlc_hw->band->pi, state);
			/* Save switch state */
			wlc_hw->btswitch_ovrd_state = state;
			return BCME_OK;
		} else {
			return BCME_NOTUP;
		}
	} else {
		return BCME_UNSUPPORTED;
	}
} /* wlc_bmac_set_btswitch */

/** Switch between host and ucode AMPDU aggregation */
void
wlc_bmac_ampdu_set(wlc_hw_info_t *wlc_hw, uint8 mode)
{
	if ((D11REV_IS(wlc_hw->corerev, 26) || D11REV_IS(wlc_hw->corerev, 29))) {
		if (mode == AMPDU_AGG_HW)
			memcpy(wlc_hw->xmtfifo_sz, xmtfifo_sz_hwagg, sizeof(xmtfifo_sz_hwagg));
		else
			memcpy(wlc_hw->xmtfifo_sz, xmtfifo_sz_hostagg, sizeof(xmtfifo_sz_hostagg));
	}
}

#if defined(SAVERESTORE) /* conserves power by powering off parts of the chip when idle \
	*/

static CONST uint32 *
BCMPREATTACHFN(wlc_bmac_sr_params_get)(wlc_hw_info_t *wlc_hw, uint32 *offset, uint32 *srfwsz)
{
	CONST uint32 *srfw = sr_get_sr_params(wlc_hw->sih, srfwsz, offset);

	/* JIRA:SWWLAN-36186, 4350C0 */
	if (D11REV_IS(wlc_hw->corerev, 48) || D11REV_IS(wlc_hw->corerev, 49))
		*offset += D11MAC_BMC_SRASM_OFFSET - (D11MAC_BMC_STARTADDR_SRASM << 8);
	else if (D11REV_IS(wlc_hw->corerev, 50) || D11REV_IS(wlc_hw->corerev, 55) ||
		D11REV_IS(wlc_hw->corerev, 56) || D11REV_IS(wlc_hw->corerev, 59) ||
		D11REV_IS(wlc_hw->corerev, 58) ||
		D11REV_IS(wlc_hw->corerev, 60))
			*offset <<= 1;
	return srfw;
}

#ifdef BCMDBG_SR
/**
 * SR sanity check
 * - ASM code is expected to be constant so compare original with txfifo
 */
static int
wlc_bmac_sr_verify(wlc_hw_info_t *wlc_hw, struct bcmstrbuf *b)
{
	int i;
	uint32 offset = 0;
	uint32 srfwsz = 0;
	CONST uint32 *srfw;
	uint32 c1, c2;
	bool asm_pass = TRUE;

	bcm_bprintf(b, "SR ASM:\n");
	if (!wlc_hw->wlc->clk) {
		bcm_bprintf(b, "No clk\n");
		return BCME_NOCLK;
	}

	srfw = wlc_bmac_sr_params_get(wlc_hw, &offset, &srfwsz);

	/* The template region starts where the BMC_STARTADDR starts.
	 * This shouldn't use a #defined value but some parameter in a
	 * global struct.
	 */
	if (D11REV_IS(wlc_hw->corerev, 48) || D11REV_IS(wlc_hw->corerev, 49))
		offset += (D11MAC_BMC_STARTADDR_SRASM << 8);
	wlc_bmac_templateptr_wreg(wlc_hw, offset);
	bcm_bprintf(b, "len: %d offset: 0x%x ", srfwsz, wlc_bmac_templateptr_rreg(wlc_hw));

	for (i = 0; i < (srfwsz/4); i++) {
		c1 = *srfw++;
		c2 = wlc_bmac_templatedata_rreg(wlc_hw);

		if (c1 != c2) {
			bcm_bprintf(b, "\ncmp failed: %d - 0x%x exp: 0x%x got: 0x%x\n", i,
			wlc_bmac_templateptr_rreg(wlc_hw), c1, c2);
			asm_pass = FALSE;
			break;
		}
	}

	bcm_bprintf(b, "\ncmp: %s", asm_pass ? "PASS" : "FAIL");
	bcm_bprintf(b, "\n");
	return 0;
} /* wlc_bmac_sr_verify */
#endif /* BCMDBG_SR */

/** S/R binary code is written into the D11 TX FIFO */
static int
BCMPREATTACHFN(wlc_bmac_sr_asm_download)(wlc_hw_info_t *wlc_hw)
{
	uint32 offset = 0;
	uint32 srfwsz = 0;
	CONST uint32 *srfw = wlc_bmac_sr_params_get(wlc_hw, &offset, &srfwsz);

	wlc_bmac_write_template_ram(wlc_hw, offset, srfwsz, (void *)srfw);
	return BCME_OK;
}

static int
BCMPREATTACHFN(wlc_bmac_sr_enable)(wlc_hw_info_t *wlc_hw)
{
	sr_engine_enable_post_dnld(wlc_hw->sih, TRUE);

	/*
	 * After enabling SR engine, update PMU min res mask
	 * This is done before si_clkctl_fast_pwrup_delay().
	 */
	si_update_masks(wlc_hw->sih);

	return BCME_OK;
}

static int
BCMPREATTACHFN(wlc_bmac_sr_init)(wlc_hw_info_t *wlc_hw)
{
	if (sr_cap(wlc_hw->sih) == FALSE) {
		WL_ERROR(("%s: sr not supported\n", __FUNCTION__));
		return BCME_ERROR;
	}

	if (wlc_bmac_sr_asm_download(wlc_hw) == BCME_OK)
		wlc_bmac_sr_enable(wlc_hw);
	else
		WL_ERROR(("%s: sr download failed\n", __FUNCTION__));

	return BCME_OK;
}

#endif /* SAVERESTORE */

/** Only called for firmware builds. Saves RAM by freeing ucode and SR arrays in an early stadium */
int
BCMPREATTACHFN(wlc_bmac_process_ucode_sr)(uint16 device, osl_t *osh, volatile void *
				regsva, uint bustype, void *btparam)
{
	int err;
	wlc_hw_info_t *wlc_hw;
#if defined(BCMULP) && !defined(WLULP_DISABLED)
	p2_handle_t *p2_handle;

	/* allocate wlc_hw_info_t state structure */
	if ((p2_handle = (p2_handle_t*) MALLOC(osh, sizeof(p2_handle_t))) == NULL) {
		WL_ERROR(("%s: no mem for wlc_hw, malloced %d bytes\n", __FUNCTION__,
			MALLOCED(osh)));
		err = 30;
		return err;
	}
#endif

	/* allocate wlc_hw_info_t state structure */
	if ((wlc_hw = (wlc_hw_info_t*) MALLOC(osh, sizeof(wlc_hw_info_t))) == NULL) {
		WL_ERROR(("%s: no mem for wlc_hw, malloced %d bytes\n", __FUNCTION__,
			MALLOCED(osh)));
		err = 30;
		return err;
	}

	bzero((char *)wlc_hw, sizeof(wlc_hw_info_t));

	wlc_hw->sih = si_attach((uint)device, osh, regsva, bustype, btparam,
		&wlc_hw->vars, &wlc_hw->vars_size);
	if (wlc_hw->sih == NULL) {
		WL_ERROR(("%s: si_attach failed\n", __FUNCTION__));
		err = 11;
		/* return error below, after memory free */
	}
	else {
		uint32 flags = SICF_PRST;
		uint32 resetflags = 0;
		uint32 num_d11_cores = 1;

		/*
		 * corerev >= 18, mac no longer enables phyclk automatically when driver accesses
		 * phyreg throughput mac. This can be skipped since only mac reg is accessed below
		 */
		if (D11REV_GE(wlc_hw->corerev, 18))
			flags |= SICF_PCLKE;

#ifdef WLP2P_UCODE_ONLY
		/*
		 * DL_P2P_UC() evaluates to wlc_hw->_p2p in the ROM, so must set this field
		 * appropriately in order to decide which ucode to load.
		 */
		wlc_hw->_p2p = TRUE;	/* P2P ucode must be loaded in this case */
#endif
		wlc_hw->regs = (d11regs_t *)si_setcore(wlc_hw->sih, D11_CORE_ID, 0);
		ASSERT(wlc_hw->regs != NULL);

		/* Do the reset of both cores together at this point for RSDB
		 * device
		 */
		wlc_hw->macunit = 0;
		wlc_hw->num_mac_chains = si_numcoreunits(wlc_hw->sih, D11_CORE_ID);
		num_d11_cores = wlc_hw->num_mac_chains;

		wlc_bmac_init_core_reset_disable_fn(wlc_hw);
		wlc_bmac_core_reset(wlc_hw, flags, resetflags);

		/* Check teh mac capability */
		wlc_hw->num_mac_chains = 1 + (((R_REG(wlc_hw->osh, &wlc_hw->regs->machwcap1)
			& MCAP1_NUMMACCHAINS) >> MCAP1_NUMMACCHAINS_SHIFT) > 1);

#if defined(BCMULP) && !defined(WLULP_DISABLED)
		p2_handle->wlc_hw = wlc_hw;
		wlc_hw->corerev = si_corerev(wlc_hw->sih);
		wlc_bmac_autod11_shm_upd(wlc_hw, D11_IF_SHM_ULP);
		ulp_p2_retrieve(p2_handle);
#endif
		wlc_ucode_download(wlc_hw);

		if (num_d11_cores == 2 && wlc_hw->num_mac_chains == 1) {
			/* If it is dual mac instead of rsdb mac (like 4364), we have to download
			 * different ucode for core0 and core1
			 */
			wlc_hw->macunit = 1;
			wlc_hw->ucode_loaded = FALSE;
			wlc_hw->regs = (d11regs_t *)si_setcore(wlc_hw->sih, D11_CORE_ID, 1);
			ASSERT(wlc_hw->regs != NULL);
			wlc_bmac_core_reset(wlc_hw, flags, resetflags);
			wlc_ucode_download(wlc_hw);
#if defined(SAVERESTORE)
			if (SR_ENAB()) {
				wlc_hw->macunit = 0;
				wlc_hw->regs = (d11regs_t *)si_setcore(wlc_hw->sih, D11_CORE_ID, 0);
				wlc_bmac_sr_init(wlc_hw);
				wlc_hw->macunit = 1;
				wlc_hw->regs = (d11regs_t *)si_setcore(wlc_hw->sih, D11_CORE_ID, 1);
				wlc_bmac_sr_init(wlc_hw);
			}
#endif
		}
		else {
#if defined(SAVERESTORE)
			/* Download SR code and reclaim: ~3.5K for 4350, ~2.2K for 4335 */
			if (SR_ENAB()) {
				wlc_hw->macunit = 0;
				wlc_hw->regs = (d11regs_t *)si_setcore(wlc_hw->sih, D11_CORE_ID, 0);
				wlc_bmac_sr_init(wlc_hw);
			}
#endif
		}
		err = 0;

		si_detach(wlc_hw->sih);
	}
	/* Always free wlc_hw ptr here prior to return */
	MFREE(osh, wlc_hw, sizeof(wlc_hw_info_t));
#if defined(BCMULP) && !defined(WLULP_DISABLED)
	MFREE(osh, p2_handle, sizeof(p2_handle_t));
#endif
	return err;
}

int wlc_bmac_4360_pcie2_war(wlc_hw_info_t* wlc_hw, uint32 vcofreq);

/**
 * BMAC level function to allocate si handle.
 *     @param device   pci device id (used to determine chip#)
 *     @param osh      opaque OS handle
 *     @param regs     virtual address of initial core registers
 *     @param bustype  pci/pcmcia/sb/sdio/etc
 *     @param vars     pointer to a pointer area for "environment" variables
 *     @param varsz    pointer to int to return the size of the vars
 */
si_t *
BCMATTACHFN(wlc_bmac_si_attach)(uint device, osl_t *osh, volatile void *regsva, uint bustype,
	void *btparam, char **vars, uint *varsz)
{
	return si_attach(device, osh, regsva, bustype, btparam, vars, varsz);
}

/** may be called with core in reset */
void
BCMATTACHFN(wlc_bmac_si_detach)(osl_t *osh, si_t *sih)
{
	BCM_REFERENCE(osh);

	if (sih) {
		si_detach(sih);
	}
}

/** register iovar table/handlers to the system */
static int
BCMATTACHFN(wlc_bmac_register_iovt_all)(wlc_hw_info_t *hw, wlc_iocv_info_t *ii)
{
	phy_info_t *pi, *prev_pi = NULL;
	int err;
	uint i;

	if ((err = wlc_bmac_register_iovt(hw, ii)) != BCME_OK) {
		WL_ERROR(("%s: wlc_bmac_register_iovt failed\n", __FUNCTION__));
		goto fail;
	}

	for (i = 0; i < MAXBANDS; i++) {
		wlc_hwband_t *band = hw->bandstate[i];

		if (band == NULL)
			continue;

		pi = (phy_info_t *)band->pi;
		if (pi == NULL)
			continue;

		if (pi == prev_pi)
			continue;

		if ((err = phy_register_iovt_all(pi, ii)) != BCME_OK) {
			WL_ERROR(("%s: phy_register_iovt_all failed\n", __FUNCTION__));
			goto fail;
		}

		prev_pi = pi;
	}

	return BCME_OK;

fail:
	return err;
}

static int
BCMATTACHFN(wlc_bmac_register_ioct_all)(wlc_hw_info_t *hw, wlc_iocv_info_t *ii)
{
	phy_info_t *pi, *prev_pi = NULL;
	int err;
	uint i;

	for (i = 0; i < MAXBANDS; i++) {
		wlc_hwband_t *band = hw->bandstate[i];

		if (band == NULL)
			continue;

		pi = (phy_info_t *)band->pi;
		if (pi == NULL)
			continue;

		if (pi == prev_pi)
			continue;

		if ((err = phy_register_ioct_all(pi, ii)) != BCME_OK) {
			WL_ERROR(("%s: phy_register_ioct_all failed\n", __FUNCTION__));
			goto fail;
		}

		prev_pi = pi;
	}

	return BCME_OK;

fail:
	return err;
}

/**
 * low level attach
 *    run backplane attach, init nvram
 *    run phy attach
 *    initialize software state for each core and band
 *    put the whole chip in reset(driver down state), no clock
 */
int
BCMATTACHFN(wlc_bmac_attach)(wlc_info_t *wlc, uint16 vendor, uint16 device, uint unit,
	bool piomode, osl_t *osh, volatile void *regsva, uint bustype, void *btparam, uint macunit)
{
	wlc_hw_info_t *wlc_hw;
	d11regs_t *regs;
	char *macaddr = NULL;
	const char *rsdb_mode;
	char *vars;
	uint err = 0;
	uint j;
	bool wme = FALSE;
	shared_phy_params_t sha_params;
	uint xmtsize;
	uint16 *ptr_xmtfifo = NULL;
	uint16 *xmtfifo_sz_dummy = NULL;
	BCM_REFERENCE(regsva);
	BCM_REFERENCE(btparam);

	WL_TRACE(("wl%d: %s: vendor 0x%x device 0x%x\n", unit, __FUNCTION__, vendor, device));

	STATIC_ASSERT(sizeof(wlc_d11rxhdr_t) <= WL_HWRXOFF);

	if ((wlc_hw = wlc_hw_attach(wlc, osh, unit, &err, macunit)) == NULL)
		goto fail;

	wlc->hw = wlc_hw;

	/* initialize header conversion mode */
	wlc_hw->hdrconv_mode = HDR_CONV();

	wlc->cmn->num_d11_cores =
	si_numd11coreunits(wlc->pub->sih);
	wlc_hw->num_mac_chains = si_numcoreunits(wlc->pub->sih, D11_CORE_ID);
#ifdef WLRSDB
	/* Update the pub state saying we are an RSDB capable chip. */
#ifdef WL_DUALNIC_RSDB
		wlc->pub->cmn->_isrsdb = TRUE;
#else
	if (wlc->cmn->num_d11_cores > 1) {
		wlc->pub->cmn->_isrsdb = TRUE;
	}
#endif /* WL_DUALNIC_RSDB */
#endif /* WLRSDB */
#ifdef WME
	wme = TRUE;
#endif /* WME */

	wlc_hw->_piomode = piomode;

#if defined(SRHWVSDB) && !defined(SRHWVSDB_DISABLED)
	wlc->pub->_wlsrvsdb = TRUE; /* from this point on, macro SRHWVSDB_ENAB() may be used */
#endif /* SRHWVSDB SRHWVSDB_DISABLED */

	/* si_attach is done much more earlier in the attach path and we dont
	 * expect it to be null.
	 */
	wlc_hw->sih = wlc->pub->sih;
	wlc_hw->vars = wlc->pub->vars;
	wlc_hw->vars_size = wlc->pub->vars_size;
	ASSERT(wlc_hw->sih);
	vars = wlc_hw->vars;


	/* set bar0 window to point at D11 core */
	wlc_hw->regs = (d11regs_t *)si_setcore(wlc_hw->sih, D11_CORE_ID, wlc_hw->macunit);
	ASSERT(wlc_hw->regs != NULL);
	regs = wlc_hw->regs;

	wlc->regs = wlc_hw->regs;

	/* Save the corerev */
	wlc_hw->corerev = si_corerev(wlc_hw->sih);

	wlc_bmac_autod11_shm_upd(wlc_hw, D11_IF_SHM_STD);

	/*
	 * wlc_bmac_rsdb_cap() will return FALSE if dualmac_rsdb is set.
	 * use this because machwcap1 is not available at this point.
	 */

	/* populate wlc_hw_info_t with default values  */
	wlc_bmac_info_init(wlc_hw);

#if defined(__mips__) || defined(BCM47XX_CA9)
	if ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
	    (CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
	    (CHIPID(wlc_hw->sih->chip) == BCM43526_CHIP_ID) ||
	    (CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID)) {
		extern int do_4360_pcie2_war;
		char *var;
		uint8 tlclkwar = 0;
		/* changing the avb vcoFreq as 510M (from default: 500M) */
		/* Tl clk 127.5Mhz */
		if ((var = getvar(NULL, rstr_wl_tlclk)))
			tlclkwar = (uint8)bcm_strtoul(var, NULL, 16);

		if (tlclkwar == 1) {
			if (wlc_bmac_4360_pcie2_war(wlc_hw, 510) != BCME_OK) {
				err = 31;
				goto fail;
			}
		}
		else if (tlclkwar == 2)
			do_4360_pcie2_war = 1;
	}
#endif /* defined(__mips__) || defined(BCM47XX_CA9) */


	/*
	 * Get vendid/devid nvram overwrites, which could be different
	 * than those the BIOS recognizes for devices on PCMCIA_BUS,
	 * SDIO_BUS, and SROMless devices on PCI_BUS.
	 */
#ifdef BCMBUSTYPE
	bustype = BCMBUSTYPE;
#endif
	if (bustype != SI_BUS)
	{
	char *var;

	if ((var = getvar(vars, rstr_vendid))) {
		vendor = (uint16)bcm_strtoul(var, NULL, 0);
		WL_ERROR(("Overriding vendor id = 0x%x\n", vendor));
	}
	if ((var = getvar(vars, rstr_devid))) {
		uint16 devid = (uint16)bcm_strtoul(var, NULL, 0);
		if (devid != 0xffff) {
			device = devid;
			WL_ERROR(("Overriding device id = 0x%x\n", device));
		}
	}

	if (BCM43602_CHIP(wlc_hw->sih->chip) && device == BCM43602_CHIP_ID) {
		device = BCM43602_D11AC_ID;
	}

	/* verify again the device is supported */
	if (!wlc_chipmatch(vendor, device)) {
		WL_ERROR(("wl%d: %s: Unsupported vendor/device (0x%x/0x%x)\n",
		          unit, __FUNCTION__, vendor, device));
		err = 12;
		goto fail;
	}
	}

	wlc_hw->vendorid = vendor;
	wlc_hw->deviceid = device;

	if ((ISSIM_ENAB(wlc_hw->sih)) &&
	    ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
	     (CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
	     (CHIPID(wlc_hw->sih->chip) == BCM43526_CHIP_ID) ||
	     (CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID))) {
		wlc_hw->deviceid = (CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID) ?
			BCM4352_D11AC_ID : BCM4360_D11AC_ID;
	}

	wlc_hw->band = wlc_hw->bandstate[IS_SINGLEBAND_5G(wlc_hw->deviceid) ?
		BAND_5G_INDEX : BAND_2G_INDEX];
	/* Monolithic driver gets wlc->band and band members initialized in wlc_bmac_attach() */
	wlc->band = wlc->bandstate[IS_SINGLEBAND_5G(wlc_hw->deviceid) ?
		BAND_5G_INDEX : BAND_2G_INDEX];
#ifdef BCMPCIDEV
	wlc_hw->pcieregs = (sbpcieregs_t *)((volatile uchar *)regsva + PCI_16KB0_PCIREGS_OFFSET);
#endif

	/* validate chip, chiprev and corerev */
	if (!wlc_isgoodchip(wlc_hw)) {
		err = 13;
		goto fail;
	}

	/* In case of RSDB chip do the reset of both the cores
	 * together at the begining, before detectign whether it is a
	 * RSDB mac or dual MAC design. Read the capability register
	 * to check whether the MAC is rsdb capable or not and then
	 * onwards use core specific reset for non-rsdb mac device
	 */
	if (wlc_hw->macunit == 0) {
		wlc_bmac_init_core_reset_disable_fn(wlc_hw);
		wlc_bmac_core_reset(wlc_hw, 0, 0);

		/* Check the mac capability */
		if (D11REV_GE(wlc_hw->corerev, 14)) {
			wlc_hw->num_mac_chains = 1 + ((R_REG(wlc_hw->osh, &wlc_hw->regs->machwcap1)
				& MCAP1_NUMMACCHAINS) >> MCAP1_NUMMACCHAINS_SHIFT);
		}
		wlc_bmac_init_core_reset_disable_fn(wlc_hw);
	}
#if defined(WLRSDB) && !defined(WLRSDB_DISABLED)
	else {
		wlc_info_t *other_wlc = wlc_rsdb_get_other_wlc(wlc_hw->wlc);
		wlc_hw->num_mac_chains = other_wlc->hw->num_mac_chains;
	wlc_bmac_init_core_reset_disable_fn(wlc_hw);
	}
#endif /* WLRSDB && WLRSDB_DISABLED */
	if ((wlc->cmn->num_d11_cores > 1) &&
		(wlc_hw->num_mac_chains == 1))
		wlc->cmn->dualmac_rsdb = TRUE;

	/* initialize power control registers */
	si_clkctl_init(wlc_hw->sih);

	si_pcie_ltr_war(wlc_hw->sih);

	/* JIRA: SWWLAN-27486 optimize power consumption when wireless is down */
	/* 'ltr' advertizes to the PCIe host how long the device takes to power up or down */
	if ((BCM4350_CHIP(wlc_hw->sih->chip) &&
	     CST4350_IFC_MODE(wlc_hw->sih->chipst) == CST4350_IFC_MODE_PCIE) ||
	     BCM43602_CHIP(wlc_hw->sih->chip)) { /* 43602 is PCIe only */
		si_pcieltrenable(wlc_hw->sih, 1, 1);
	}

	/* JIRA: SWWLAN-28745, disable OBFF for pcie core rev 0,1,2,3,4 */
	if ((BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) &&
	    (BUSCORETYPE(wlc_hw->sih->buscoretype) == PCIE2_CORE_ID) &&
	    (wlc_hw->sih->buscorerev <= 4)) {
		si_pcieobffenable(wlc_hw->sih, 1, 0);
	}

	/* request fastclock and force fastclock for the rest of attach
	 * bring the d11 core out of reset.
	 *   For PMU chips, the first wlc_clkctl_clk is no-op since core-clk is still FALSE;
	 *   But it will be called again inside wlc_corereset, after d11 is out of reset.
	 */
	wlc_clkctl_clk(wlc_hw, CLK_FAST);

	/*   Programming d11 core oob  settings for 4364
	  *	WARs for HW4364-237 and HW4364-166
	  */
	if ((CHIPID(wlc_hw->sih->chip) == BCM4364_CHIP_ID) && (wlc_hw->macunit == 0)) {
		si_config_4364_d11_oob(wlc_hw->sih, D11_CORE_ID);
	}
	/* change the oob settings to route the d11 core1 interrupts via DDR */
	if (BCM53573_CHIP(wlc_hw->sih->chip) && (wlc_hw->macunit == 1)) {
		si_config_53573_d11_oob(wlc_hw->sih, D11_CORE_ID);
	}

	wlc_bmac_corereset(wlc_hw, WLC_USE_COREFLAGS);

	if (!wlc_bmac_validate_chip_access(wlc_hw)) {
		WL_ERROR(("wl%d: %s: validate_chip_access failed\n", unit, __FUNCTION__));
		err = 14;
		goto fail;
	}

	/* get the board rev, used just below */
	j = getintvar(vars, rstr_boardrev);
	/* promote srom boardrev of 0xFF to 1 */
	if (j == BOARDREV_PROMOTABLE)
		j = BOARDREV_PROMOTED;
	wlc_hw->boardrev = (uint16)j;
	if (!wlc_validboardtype(wlc_hw)) {
		WL_ERROR(("wl%d: %s: Unsupported Broadcom board type (0x%x)"
			" or revision level (0x%x)\n",
			unit, __FUNCTION__, wlc_hw->sih->boardtype, wlc_hw->boardrev));
		err = 15;
		goto fail;
	}

	/* set vars table accesser for multi-slice chips */
	if ((!wlc_bmac_rsdb_cap(wlc_hw)) &&
		(wlc->cmn->num_d11_cores > 1) &&
		(wlc_hw->macunit == 1)) {
		strncpy(wlc_hw->vars_table_accessor, "slice/1/",
			sizeof(wlc_hw->vars_table_accessor));
		wlc_hw->vars_table_accessor[sizeof(wlc_hw->vars_table_accessor)-1] = '\0';
	}

	wlc_hw->sromrev = (uint8)getintvar(vars, rstr_sromrev);
	wlc_hw->boardflags = (uint32)getintvar_slicespecific(wlc_hw, vars, rstr_boardflags);
	wlc_hw->boardflags2 = (uint32)getintvar_slicespecific(wlc_hw, vars, rstr_boardflags2);
	if (wlc_hw->sromrev >= 12)
		wlc_hw->boardflags4 =
			(uint32)getintvar_slicespecific(wlc_hw, vars, rstr_boardflags4);
	wlc_hw->antswctl2g = (uint8)getintvar(vars, rstr_antswctl2g);
	wlc_hw->antswctl5g = (uint8)getintvar(vars, rstr_antswctl5g);

	/* some branded-boards boardflags srom programmed incorrectly */
	if (wlc_hw->sih->boardvendor == VENDOR_APPLE) {
		if ((wlc_hw->sih->boardtype == 0x4e) && (wlc_hw->boardrev >= 0x41))
			wlc_hw->boardflags |= BFL_PACTRL;
		else if ((wlc_hw->sih->boardtype == BCM94331X28) &&
		         (wlc_hw->boardrev < 0x1501)) {
			wlc_hw->boardflags |= BFL_FEM_BT;
			wlc_hw->boardflags2 = 0;
		} else if ((wlc_hw->sih->boardtype == BCM94331X29B) &&
		           (wlc_hw->boardrev < 0x1202)) {
			wlc_hw->boardflags |= BFL_FEM_BT;
			wlc_hw->boardflags2 = 0;
		}
	}

	if (wlc_hw->boardflags & BFL_NOPLLDOWN)
		wlc_bmac_pllreq(wlc_hw, TRUE, WLC_PLLREQ_SHARED);

	if ((BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) && (si_pci_war16165(wlc_hw->sih)))
		wlc->war16165 = TRUE;

#if defined(DBAND)
	/* check device id(srom, nvram etc.) to set bands */
	if ((wlc_hw->deviceid == BCM43224_D11N_ID) ||
	    (wlc_hw->deviceid == BCM43224_D11N_ID_VEN1) ||
	    (wlc_hw->deviceid == BCM43421_D11N_ID) ||
	    (wlc_hw->deviceid == BCM43236_D11N_ID) ||
	    (wlc_hw->deviceid == BCM6362_D11N_ID) ||
	    (wlc_hw->deviceid == BCM4331_D11N_ID) ||
	    (wlc_hw->deviceid == BCM43228_D11N_ID) ||
	    (wlc_hw->deviceid == BCM4324_D11N_ID) ||
	    (wlc_hw->deviceid == BCM43242_D11N_ID) ||
	    (wlc_hw->deviceid == BCM4334_D11N_ID) ||
	    (wlc_hw->deviceid == BCM4360_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4352_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM43341_D11N_ID) ||
	    (wlc_hw->deviceid == BCM4335_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4345_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM43455_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM43602_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4352_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4350_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM43556_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM43558_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM43566_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM43568_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM43569_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4354_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4356_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4371_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4358_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4349_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4355_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4359_D11AC_ID) ||

	    (wlc_hw->deviceid == BCM43596_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4365_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM4366_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM43012_D11N_ID) ||
	    (wlc_hw->deviceid == BCM53573_D11AC_ID) ||
	    (wlc_hw->deviceid == BCM47189_D11AC_ID) ||
	    0) {
		/* Dualband boards */
		wlc_hw->_nbands = 2;
	} else
#endif /* DBAND */
		wlc_hw->_nbands = 1;

#if NCONF
	if (CHIPID(wlc_hw->sih->chip) == BCM43225_CHIP_ID ||
	    CHIPID(wlc_hw->sih->chip) == BCM43235_CHIP_ID ||
	    CHIPID(wlc_hw->sih->chip) == BCM43131_CHIP_ID ||
	    CHIPID(wlc_hw->sih->chip) == BCM43217_CHIP_ID ||
	    CHIPID(wlc_hw->sih->chip) == BCM43227_CHIP_ID) {
		wlc_hw->_nbands = 1;
	}
#endif /* NCONF */

	/* BMAC_NOTE: remove init of pub values when wlc_attach() unconditionally does the
	 * init of these values
	 */
	wlc->vendorid = wlc_hw->vendorid;
	wlc->deviceid = wlc_hw->deviceid;
	wlc->pub->sih = wlc_hw->sih;
	wlc->pub->corerev = wlc_hw->corerev;
	wlc->pub->sromrev = wlc_hw->sromrev;
	wlc->pub->boardrev = wlc_hw->boardrev;
	wlc->pub->boardflags = wlc_hw->boardflags;
	wlc->pub->boardflags2 = wlc_hw->boardflags2;
	if (wlc_hw->sromrev >= 12)
		wlc->pub->boardflags4 = wlc_hw->boardflags4;
	wlc->pub->_nbands = wlc_hw->_nbands;

	WL_ERROR(("wlc_bmac_attach, deviceid 0x%x nbands %d\n", wlc_hw->deviceid, wlc_hw->_nbands));

#ifdef PKTC
	wlc->pub->_pktc = (getintvar(vars, "pktc_disable") == 0) &&
		(getintvar(vars, "ctf_disable") == 0);
#endif
#if defined(PKTC_DONGLE)
	wlc->pub->_pktc = TRUE;
#endif

	wlc_hw->physhim = wlc_phy_shim_attach(wlc_hw, wlc->wl, wlc);

	if (wlc_hw->physhim == NULL) {
		WL_ERROR(("wl%d: %s: wlc_phy_shim_attach failed\n", unit, __FUNCTION__));
		err = 25;
		goto fail;
	}

	/* pass all the parameters to wlc_phy_shared_attach in one struct */
	sha_params.osh = osh;
	sha_params.sih = wlc_hw->sih;
	sha_params.physhim = wlc_hw->physhim;
	sha_params.unit = unit;
	sha_params.corerev = wlc_hw->corerev;
	sha_params.vars = vars;
	sha_params.vid = wlc_hw->vendorid;
	sha_params.did = wlc_hw->deviceid;
	sha_params.chip = wlc_hw->sih->chip;
	sha_params.chiprev = wlc_hw->sih->chiprev;
	sha_params.chippkg = wlc_hw->sih->chippkg;
	sha_params.sromrev = wlc_hw->sromrev;
	sha_params.boardtype = wlc_hw->sih->boardtype;
	sha_params.boardrev = wlc_hw->boardrev;
	sha_params.boardvendor = wlc_hw->sih->boardvendor;
	sha_params.boardflags = wlc_hw->boardflags;
	sha_params.boardflags2 = wlc_hw->boardflags2;
	sha_params.boardflags4 = wlc_hw->boardflags4;
	sha_params.bustype = wlc_hw->sih->bustype;
	sha_params.buscorerev = wlc_hw->sih->buscorerev;
	strncpy(sha_params.vars_table_accessor, wlc_hw->vars_table_accessor,
		sizeof(sha_params.vars_table_accessor));
	sha_params.vars_table_accessor[sizeof(sha_params.vars_table_accessor)-1] = '\0';
	/* alloc and save pointer to shared phy state area */
	wlc_hw->phy_sh = wlc_phy_shared_attach(&sha_params);
	if (!wlc_hw->phy_sh) {
		err = 16;
		goto fail;
	}

	/* use different hw rx offset for AC cores, must be done before dma_attach */
	wlc->hwrxoff = (D11REV_GE(wlc_hw->corerev, 40)) ? WL_HWRXOFF_AC : WL_HWRXOFF;
	wlc->hwrxoff_pktget = (wlc->hwrxoff % 4) ?  wlc->hwrxoff : (wlc->hwrxoff + 2);

	wlc_hw->vcoFreq_4360_pcie2_war = 510; /* Default Value */

	wlc_hw->machwcap1 = R_REG(wlc_hw->osh, &wlc_hw->regs->machwcap1);
	wlc_hw->num_mac_chains =
		1 + ((wlc_hw->machwcap1 & MCAP1_NUMMACCHAINS) >> MCAP1_NUMMACCHAINS_SHIFT);

	/* initialize software state for each core and band */
	for (j = 0; j < NBANDS_HW(wlc_hw); j++) {
		/*
		 * band0 is always 2.4Ghz
		 * band1, if present, is 5Ghz
		 */

		/* So if this is a single band 11a card, use band 1 */
		if (IS_SINGLEBAND_5G(wlc_hw->deviceid))
			j = BAND_5G_INDEX;

		wlc_setxband(wlc_hw, j);

		wlc_hw->band->bandunit = j;
		wlc_hw->band->bandtype = j ? WLC_BAND_5G : WLC_BAND_2G;
		/* Monolithic driver gets wlc->band and band members
		 * initialized in wlc_bmac_attach()
		 */
		wlc->band->bandunit = j;
		wlc->band->bandtype = j ? WLC_BAND_5G : WLC_BAND_2G;
		wlc->core->coreidx = si_coreidx(wlc_hw->sih);

		wlc_hw->machwcap = R_REG(osh, &regs->machwcap);
		if ((D11REV_IS(wlc_hw->corerev, 26) &&
			CHIPREV(wlc_hw->sih->chiprev) == 0) ||
		    (D11REV_IS(wlc_hw->corerev, 29)) || (D11REV_IS(wlc_hw->corerev, 33)) ||
		    (D11REV_IS(wlc_hw->corerev, 34)) || (D11REV_IS(wlc_hw->corerev, 35)) ||
		    (D11REV_IS(wlc_hw->corerev, 37)) || (D11REV_IS(wlc_hw->corerev, 30)) ||
		    (D11REV_IS(wlc_hw->corerev, 39)) ||
			(D11REV_IS(wlc_hw->corerev, 40)) || (D11REV_IS(wlc_hw->corerev, 41)) ||
		    (D11REV_IS(wlc_hw->corerev, 42)) ||
		    (D11REV_IS(wlc_hw->corerev, 43)) || (D11REV_IS(wlc_hw->corerev, 44)) ||
		    (D11REV_IS(wlc_hw->corerev, 48)) ||
		    (D11REV_IS(wlc_hw->corerev, 49)) || (D11REV_IS(wlc_hw->corerev, 39))) {
		     WL_ERROR(("%s: Disabling HW TKIP!\n", __FUNCTION__));
			 wlc_hw->machwcap &= ~MCAP_TKIPMIC;
		}

		ptr_xmtfifo = get_xmtfifo_sz(&xmtsize);
		xmtfifo_sz_dummy = get_xmtfifo_sz_dummy();
		/* init tx fifo size */

		if (D11REV_GE(wlc_hw->corerev, 48)) {
			/* this is just a way to reduce memory footprint */
			wlc_hw->xmtfifo_sz = xmtfifo_sz_dummy;
		} else if (D11REV_LT(wlc_hw->corerev, 40)) {
		/* init tx fifo size */
			ASSERT((wlc_hw->corerev - XMTFIFOTBL_STARTREV) < xmtsize);
			wlc_hw->xmtfifo_sz = (ptr_xmtfifo +
				((wlc_hw->corerev - XMTFIFOTBL_STARTREV)* NFIFO));
		} else {
			wlc_hw->xmtfifo_sz = (ptr_xmtfifo + ((40 - XMTFIFOTBL_STARTREV) * NFIFO));
		}

		wlc_bmac_ampdu_set(wlc_hw, AMPDU_AGGMODE_HOST);

		/* Get a phy for this band */
		WL_NONE(("wl%d: %s: bandunit %d bandtype %d coreidx %d\n", unit,
		         __FUNCTION__, wlc_hw->band->bandunit, wlc_hw->band->bandtype,
		         wlc->core->coreidx));
		if ((wlc_hw->band->pi = (wlc_phy_t *)
		     phy_module_attach(wlc_hw->phy_sh, (void *)(uintptr)regs,
		                wlc_hw->band->bandtype, vars)) == NULL) {
			WL_ERROR(("wl%d: %s: phy_module_attach failed\n", unit, __FUNCTION__));
			err = 17;
			goto fail;
		}
		/* it's called again after phy module attach as bmac module attach don't have pi */
		wlc_phy_set_shmdefs(wlc_hw->band->pi, wlc_hw->shmdefs);

		/* No need to call this for ACPHY chips */
		if (!WLCISACPHY(wlc_hw->band))
			wlc_bmac_set_btswitch(wlc_hw, AUTO);

		wlc_phy_machwcap_set(wlc_hw->band->pi, wlc_hw->machwcap);

		phy_utils_get_phyversion((phy_info_t *)wlc_hw->band->pi, &wlc_hw->band->phytype,
			&wlc_hw->band->phyrev, &wlc_hw->band->radioid, &wlc_hw->band->radiorev,
			&wlc_hw->band->phy_minor_rev);

		wlc_hw->band->core_flags = phy_utils_get_coreflags((phy_info_t *)wlc_hw->band->pi);

		/* verify good phy_type & supported phy revision */
		if (WLCISNPHY(wlc_hw->band)) {
			if (NCONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else if (WLCISLCNPHY(wlc_hw->band)) {
			if (LCNCONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else if (WLCISHTPHY(wlc_hw->band)) {
			if (HTCONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else if (WLCISLCN40PHY(wlc_hw->band)) {
			if (LCN40CONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else if (WLCISLCN20PHY(wlc_hw->band)) {
			if (LCN20CONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else if (WLCISACPHY(wlc_hw->band)) {
			if (ACCONF_HAS(wlc_hw->band->phyrev))
				goto good_phy;
			else
				goto bad_phy;
		} else {
bad_phy:
				WL_ERROR(("wl%d: %s: unsupported phy type/rev (%d/%d)\n",
					unit, __FUNCTION__, wlc_hw->band->phytype,
					wlc_hw->band->phyrev));
				err = 18;
				goto fail;
			}

good_phy:
			WL_ERROR(("wl%d: %s: chiprev %d corerev %d "
				"cccap 0x%x maccap 0x%x band %sG, phy_type %d phy_rev %d\n",
				unit, __FUNCTION__, CHIPREV(wlc_hw->sih->chiprev),
				wlc_hw->corerev, wlc_hw->sih->cccaps, wlc_hw->machwcap,
				BAND_2G(wlc_hw->band->bandtype) ? "2.4" : "5",
				wlc_hw->band->phytype, wlc_hw->band->phyrev));

		/* Monolithic driver gets wlc->band and band members
		 * initialized in wlc_bmac_attach()
		 */
		/* Initialize both wlc->pi and wlc->bandinst->pi */
		wlc->pi = wlc->bandinst[wlc->band->bandunit]->pi = wlc_hw->band->pi;
		wlc->band->phytype = wlc_hw->band->phytype;
		wlc->band->phyrev = wlc_hw->band->phyrev;
		wlc->band->radioid = wlc_hw->band->radioid;
		wlc->band->radiorev = wlc_hw->band->radiorev;
		wlc->band->phy_minor_rev = wlc_hw->band->phy_minor_rev;

		/* default contention windows size limits */
		wlc_hw->band->CWmin = APHY_CWMIN;
		wlc_hw->band->CWmax = PHY_CWMAX;

		if (!wlc_bmac_attach_dmapio(wlc_hw, wme)) {
		err = 19;
		goto fail;
		}
	}

	if (!PIO_ENAB_HW(wlc_hw) &&
	    (BCM4331_CHIP_ID == CHIPID(wlc_hw->sih->chip)) &&
	    (si_pcie_get_request_size(wlc_hw->sih) > 128)) {
		uint i;
		for (i = 0; i < NFIFO; i++) {
			if (wlc_hw->di[i])
				dma_ctrlflags(wlc_hw->di[i], DMA_CTRL_DMA_AVOIDANCE_WAR,
					DMA_CTRL_DMA_AVOIDANCE_WAR);
		}
		wlc->dma_avoidance_war = TRUE;
	}

	/* set default 2-wire or 3-wire setting */
	wlc_bmac_btc_wire_set(wlc_hw, WL_BTC_DEFWIRE);

	wlc_hw->btc->btcx_aa = (uint8)getintvar(vars, rstr_aa2g);
	wlc_hw->btc->mode = (uint8)getintvar(vars, rstr_btc_mode);
	/* set BT Coexistence default mode */
	if (getvar(vars, rstr_btc_mode))
		wlc_bmac_btc_mode_set(wlc_hw, (uint8)getintvar(vars, rstr_btc_mode));
	else
		wlc_bmac_btc_mode_set(wlc_hw, WL_BTC_DEFAULT);

	/* attach/register iovar/ioctl handlers */
	if ((wlc_hw->iocvi = wlc_iocv_attach(wlc_hw)) == NULL) {
		WL_ERROR(("wl%d: %s: wlc_iocv_attach failed\n", unit, __FUNCTION__));
		err = 11;
		goto fail;
	}
	if (wlc_bmac_register_iovt_all(wlc_hw, wlc_hw->iocvi) != BCME_OK) {
		err = 181;
		goto fail;
	}
	if (wlc_bmac_register_ioct_all(wlc_hw, wlc_hw->iocvi) != BCME_OK) {
		err = 182;
		goto fail;
	}

#ifdef PREATTACH_NORECLAIM
#endif /* PREATTACH_NORECLAIM */

#ifdef SAVERESTORE
	if (SR_ENAB() && sr_cap(wlc_hw->sih)) {
		/* Download SR code */
		wlc_bmac_sr_init(wlc_hw);
	}
#endif /* SAVERESTORE */

	/* disable core to match driver "down" state */
	wlc_coredisable(wlc_hw);


	/* JIRA: SWWLAN-23710. Power save. */
	if ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
		BCM43602_CHIP(wlc_hw->sih->chip) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43526_CHIP_ID))
		si_pmu_rfldo(wlc_hw->sih, 0);

	/* Match driver "down" state */
	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS)
		si_pci_down(wlc_hw->sih);

	/* register sb interrupt callback functions */
	si_register_intr_callback(wlc_hw->sih, (void *)wlc_wlintrsoff,
		(void *)wlc_wlintrsrestore, NULL, wlc_hw);

	ASSERT(!wlc_hw->sbclk || !si_taclear(wlc_hw->sih, TRUE));

	/* turn off pll and xtal to match driver "down" state */
	wlc_bmac_xtal(wlc_hw, OFF);

	/* *********************************************************************
	 * The hardware is in the DOWN state at this point. D11 core
	 * or cores are in reset with clocks off, and the board PLLs
	 * are off if possible.
	 *
	 * Beyond this point, wlc->sbclk == FALSE and chip registers
	 * should not be touched.
	 *********************************************************************
	 */

	/* init etheraddr state variables */
	if ((macaddr = wlc_get_macaddr(wlc_hw, wlc->pub->unit)) == NULL) {
		WL_ERROR(("wl%d: %s: macaddr not found\n", unit, __FUNCTION__));
		err = 21;
		goto fail;
	}
#ifdef WL_DUALNIC_RSDB
	if (wlc->pub->unit == 1) {
		bcopy(&wlc->cmn->wlc[0]->hw->etheraddr, &wlc_hw->etheraddr, ETHER_ADDR_LEN);
	} else
#endif
	{
		bcm_ether_atoe(macaddr, &wlc_hw->etheraddr);
	}

	if (ETHER_ISBCAST((char*)&wlc_hw->etheraddr) ||
		ETHER_ISNULLADDR((char*)&wlc_hw->etheraddr)) {
		WL_ERROR(("wl%d: %s: bad macaddr %s\n", unit, __FUNCTION__, macaddr));
		err = 22;
		goto fail;
	}

	WL_INFORM(("wl%d: %s: board 0x%x macaddr: %s\n", unit, __FUNCTION__,
		wlc_hw->sih->boardtype, macaddr));

#ifdef WLLED
	if ((wlc_hw->ledh = wlc_bmac_led_attach(wlc_hw)) == NULL) {
		WL_ERROR(("wl%d: %s: wlc_bmac_led_attach() failed.\n", unit, __FUNCTION__));
		err = 23;
		goto fail;
	}
#endif

#if defined(BCMDBG_PHYDUMP)
	wlc_hw->suspend_stats = (bmac_suspend_stats_t*) MALLOC(wlc_hw->osh,
	                                                       sizeof(*wlc_hw->suspend_stats));
	if (wlc_hw->suspend_stats == NULL) {
		WL_ERROR(("wl%d: wlc_bmac_attach: suspend_stats alloc failed.\n", unit));
		err = 26;
		goto fail;
	}
#endif 

#ifdef AP
	wlc_bmac_pmq_init(wlc_hw);
#endif

	/* Register to be notified when pktpool is available which can
	 * happen outside this scope from bus side.
	 */
	if (BCMSPLITRX_ENAB()) {
		if (POOL_ENAB(wlc->pub->pktpool_rxlfrag)) {
			/* if rx frag pool is enabled, use fragmented rx pool for registration */
#ifdef BCMDBG_POOL
			pktpool_dbg_register(wlc->pub->pktpool_rxlfrag, wlc_pktpool_dbg_cb, wlc_hw);
#endif

			/* Pass down pool info to dma layer */
			if (wlc_hw->di[RX_FIFO])
				dma_pktpool_set(wlc_hw->di[RX_FIFO], wlc->pub->pktpool_rxlfrag);

			/* if second fifo is set, set pktpool for that */
			if (wlc_hw->di[RX_FIFO1])
				dma_pktpool_set(wlc_hw->di[RX_FIFO1],
				(RXFIFO_SPLIT()?wlc->pub->pktpool_rxlfrag :wlc->pub->pktpool));

			/* FIFO- 2 */
			if (wlc_hw->di[RX_FIFO2])
				dma_pktpool_set(wlc_hw->di[RX_FIFO2], wlc->pub->pktpool);
			/* register a callback to be invoked when hostaddress is posted */
			if (wlc_hw->di[RX_FIFO])
				pkpool_haddr_avail_register_cb(wlc->pub->pktpool_rxlfrag,
					wlc_pktpool_avail_cb, wlc_hw);

		} else {
			WL_ERROR(("%s: RXLFRAG Pool not available split RX mode \n", __FUNCTION__));
		}
	} else if (POOL_ENAB(wlc->pub->pktpool)) {
		pktpool_avail_register(wlc->pub->pktpool,
			wlc_pktpool_avail_cb, wlc_hw);
		pktpool_empty_register(wlc->pub->pktpool,
			wlc_pktpool_empty_cb, wlc_hw);
#ifdef BCMDBG_POOL
		pktpool_dbg_register(wlc->pub->pktpool, wlc_pktpool_dbg_cb, wlc_hw);
#endif

#ifdef DMATXRC
		if (DMATXRC_ENAB(wlc->pub)) {
			err = wlc_phdr_attach(wlc);
			if (err != BCME_OK) {
				WL_ERROR(("%s: phdr attach FAILED (err %d)\n", __FUNCTION__, err));
				goto fail;
			}
		}
#endif

		/* set pool for rx dma */
		if (wlc_hw->di[RX_FIFO])
			dma_pktpool_set(wlc_hw->di[RX_FIFO], wlc->pub->pktpool);
	}

	/* Initialize btc param information from NVRAM */
	err = wlc_bmac_btc_param_attach(wlc);
	if (err != BCME_OK) {
		WL_ERROR(("%s: btc param attach FAILED (err %d)\n", __FUNCTION__, err));
		goto fail;
	}
#ifdef WOWL_GPIO
	wlc_hw->wowl_gpio = WOWL_GPIO;
#ifdef WOWL_GPIO_POLARITY
	wlc_hw->wowl_gpiopol = WOWL_GPIO_POLARITY;
#endif
	{
		/* override wowl gpio if defined in nvram */
		char *var;
		if ((var = getvar(wlc_hw->vars, rstr_wowl_gpio)) != NULL)
			wlc_hw->wowl_gpio =  (uint8)bcm_strtoul(var, NULL, 0);
		if ((var = getvar(wlc_hw->vars, rstr_wowl_gpiopol)) != NULL)
			wlc_hw->wowl_gpiopol =  (bool)bcm_strtoul(var, NULL, 0);
	}
#endif /* WOWL_GPIO */

#if WLC_BMAC_DUMP_NUM_REGS > 0
	wlc_hw->dump = wlc_dump_reg_create(wlc_hw->osh, WLC_BMAC_DUMP_NUM_REGS);
	if (!wlc_hw->dump) {
		WL_ERROR(("wl%d: %s: wlc_dump_reg_create() failed.\n", unit, __FUNCTION__));
		err = 32;
		goto fail;
	}
#endif
	wlc_bmac_register_dumps(wlc_hw);

	if (wlc_bmac_rsdb_cap(wlc_hw)) {
		wlc_hw->templatebase =
			D11MAC_BMC_TPL_BYTES_PERCORE * wlc_hw->macunit;
	} else {
		wlc_hw->templatebase = 0;
	}
	if ((rsdb_mode = getvar(NULL, rstr_rsdb_mode))) {
		wlc->cmn->ap_rsdb_mode = (int8)bcm_atoi(rsdb_mode);
	}

	return BCME_OK;

fail:
	WL_ERROR(("wl%d: %s: failed with err %d\n", unit, __FUNCTION__, err));
	return err;
} /* wlc_bmac_attach */

/**
 * Initialize wlc_info default values ... may get overrides later in this function.
 * BMAC_NOTES, move low out and resolve the dangling ones.
 */
void
BCMATTACHFN(wlc_bmac_info_init)(wlc_hw_info_t *wlc_hw)
{
	wlc_info_t *wlc = wlc_hw->wlc;

	(void)wlc;

	/* set default sw macintmask value */
	wlc_hw->defmacintmask = DEF_MACINTMASK;

	/* set default delayedintmask value */
	wlc_hw->delayedintmask = DELAYEDINTMASK;

	ASSERT(wlc_hw->corerev);

#ifdef WL_PRE_AC_DELAY_NOISE_INT
	/* delay noise interrupt for non ac chips */
	if (D11REV_LT(wlc_hw->corerev, 40)) {
		/* stop BG_NOISE from interrupting host */
		wlc_bmac_set_defmacintmask(wlc_hw, MI_BG_NOISE, ~MI_BG_NOISE);
		/* instead handle BG_NOISE when already interrupted */
		wlc_hw->delayedintmask |= MI_BG_NOISE;
	}
#endif /* WL_PRE_AC_DELAY_NOISE_INT */

	wlc_bmac_set_defmacintmask(wlc_hw, MI_HWACI_NOTIFY, MI_HWACI_NOTIFY);
#ifdef DMATXRC
	/* For D11 >= 40, use I_XI */
	if (DMATXRC_ENAB(wlc->pub) && D11REV_LT(wlc_hw->corerev, 40)) {
		wlc_hw->defmacintmask |= MI_DMATX;

		/* Default to process all fifos */
		wlc->txrc_fifo_mask = (1 << NFIFO) - 1;
	}
#endif

	/* inital value for receive interrupt lazy control */
	wlc_hw->intrcvlazy = WLC_INTRCVLAZY_DEFAULT;

	/* various 802.11g modes */
	wlc_hw->shortslot = FALSE;

	wlc_hw->SFBL = RETRY_SHORT_FB;
	wlc_hw->LFBL = RETRY_LONG_FB;

	/* default mac retry limits */
	wlc_hw->SRL = RETRY_SHORT_DEF;
	wlc_hw->LRL = RETRY_LONG_DEF;
	wlc_hw->chanspec = CH20MHZ_CHSPEC(1);

#ifdef WLRXOV
	wlc->rxov_delay = RXOV_TIMEOUT_MIN;
	wlc->rxov_txmaxpkts = MAXTXPKTS;

	if (WLRXOV_ENAB(wlc->pub))
		wlc_hw->defmacintmask |= MI_RXOV;
#endif

	wlc_hw->btswitch_ovrd_state = AUTO;

#ifdef WLP2P_UCODE
	/* default p2p to enabled */
#ifdef WLP2P_UCODE_ONLY
	wlc_hw->_p2p = TRUE;
	if (D11REV_IS(wlc_hw->corerev, 50) || D11REV_IS(wlc_hw->corerev, 55)) {
		wlc->cmn->ps_multista = TRUE;
	}
#endif
#endif /* WLP2P_UCODE */

#ifdef WL_BCNTRIM
	if (WLC_BCNTRIM_ENAB(wlc->pub))
		wlc_hw->defmacintmask |= MI_BCNTRIM_RX;
#endif
} /* wlc_bmac_info_init */

struct wlc_btc_param_vars_entry {
	uint16 index;
	uint16 value;
};

struct wlc_btc_param_vars_info {
	bool flags_present;
	uint16 flags;
	uint16 num_entries;
	struct wlc_btc_param_vars_entry param_list[0];
};

/** low level detach */
int
BCMATTACHFN(wlc_bmac_detach)(wlc_info_t *wlc)
{
	uint i;
	wlc_hwband_t *band;
	wlc_hw_info_t *wlc_hw = wlc->hw;
	int callbacks;

	callbacks = 0;

	if (wlc_hw == NULL) {
		return callbacks;
	}
#if WLC_BMAC_DUMP_NUM_REGS > 0
	if (wlc_hw->dump) {
		wlc_dump_reg_destroy(wlc_hw->dump);
	}
#endif
	if (wlc_hw->sih) {
		/* detach interrupt sync mechanism since interrupt is disabled and per-port
		 * interrupt object may has been freed. this must be done before sb core switch
		 */
		si_deregister_intr_callback(wlc_hw->sih);

		if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS)
			si_pci_sleep(wlc_hw->sih);
	}

	wlc_bmac_detach_dmapio(wlc_hw);

	band = wlc_hw->band;
	for (i = 0; i < NBANDS_HW(wlc_hw); i++) {
		/* So if this is a single band 11a card, use band 1 */
		if (IS_SINGLEBAND_5G(wlc_hw->deviceid))
			i = BAND_5G_INDEX;

		if (band->pi) {
			/* Detach this band's phy */
			phy_module_detach((phy_info_t *)band->pi);
			band->pi = NULL;
		}
		band = wlc_hw->bandstate[OTHERBANDUNIT(wlc_hw)];
	}

	/* Free shared phy state */
	wlc_phy_shared_detach(wlc_hw->phy_sh);

	wlc_phy_shim_detach(wlc_hw->physhim);

	/* free vars */
	/*
	 * we are done with vars now, let wlc_detach take care of freeing it.
	 */
	wlc_hw->vars = NULL;

	/*
	 * we are done with sih now, let wlc_detach take care of freeing it.
	 */
	wlc_hw->sih = NULL;

#ifdef WLLED
	if (wlc_hw->ledh) {
		callbacks += wlc_bmac_led_detach(wlc_hw);
		wlc_hw->ledh = NULL;
	}
#endif
#ifdef AP
	wlc_bmac_pmq_delete(wlc_hw);
#endif

#if defined(BCMDBG_PHYDUMP)
	if (wlc_hw->suspend_stats) {
		MFREE(wlc_hw->osh, wlc_hw->suspend_stats, sizeof(*wlc_hw->suspend_stats));
		wlc_hw->suspend_stats = NULL;
	}
#endif 

#ifdef DMATXRC
	if (DMATXRC_ENAB(wlc->pub))
		wlc_phdr_detach(wlc);
#endif

	if (wlc->btc_param_vars) {
		MFREE(wlc_hw->osh, wlc->btc_param_vars,
			sizeof(struct wlc_btc_param_vars_info) + wlc->btc_param_vars->num_entries
				* sizeof(struct wlc_btc_param_vars_entry));
		wlc->btc_param_vars = NULL;
	}


	if (wlc_hw->btc->wlc_btc_params_fw) {
		MFREE(wlc->osh, wlc_hw->btc->wlc_btc_params_fw,
			BTC_FW_MAX_INDICES*sizeof(uint16));
		wlc_hw->btc->wlc_btc_params_fw = NULL;
	}

	if (wlc_hw->iocvi != NULL)
		wlc_iocv_detach(wlc_hw->iocvi);

	wlc_hw_detach(wlc_hw);
	wlc->hw = NULL;

	return callbacks;
} /* wlc_bmac_detach */

/** d11 core needs to be reset during a 'wl up' or 'wl down' */
void
BCMINITFN(wlc_bmac_reset)(wlc_hw_info_t *wlc_hw)
{
	bool dev_gone;

	WLCNTINCR(wlc_hw->wlc->pub->_cnt->reset);

	dev_gone = DEVICEREMOVED(wlc_hw->wlc);
	/* reset the core */
	if (!dev_gone)
		wlc_bmac_corereset(wlc_hw, WLC_USE_COREFLAGS);

	/* purge the pio queues or dma rings */
	wlc_hw->reinit = TRUE;
	wlc_flushqueues(wlc_hw);

	/* save a copy of the btc params before going down */

	wlc_reset_bmac_done(wlc_hw->wlc);
}

/** d11 core needs to be initialized during a 'wl up' */
void
BCMINITFN(wlc_bmac_init)(wlc_hw_info_t *wlc_hw, chanspec_t chanspec, bool mute,
	uint32 defmacintmask)
{
	uint32 macintmask;
	bool fastclk;
	wlc_info_t *wlc = wlc_hw->wlc;

	WL_TRACE(("wl%d: wlc_bmac_init\n", wlc_hw->unit));

	UNUSED_PARAMETER(defmacintmask);
	/* request FAST clock if not on */
	if (!(fastclk = wlc_hw->forcefastclk))
		wlc_clkctl_clk(wlc_hw, CLK_FAST);

	/* disable interrupts */
	macintmask = wl_intrsoff(wlc->wl);

	/* JIRA: SWWLAN-23710 */
	if ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
		BCM43602_CHIP(wlc_hw->sih->chip) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43526_CHIP_ID))
		si_pmu_rfldo(wlc_hw->sih, 1);

	/* set up the specified band and chanspec */
	wlc_setxband(wlc_hw, CHSPEC_WLCBANDUNIT(chanspec));
	wlc_phy_chanspec_radio_set(wlc_hw->band->pi, chanspec);
	wlc_hw->chanspec = chanspec;

	/* do one-time phy inits and calibration */
	wlc_phy_cal_init(wlc_hw->band->pi);

	/* core-specific initialization. E.g. load and initialize ucode. */
	wlc_coreinit(wlc_hw);

	/*
	 * initialize mac_suspend_depth to 1 to match ucode initial suspended state
	 */
	wlc_hw->mac_suspend_depth = 1;
	/* suspend the tx fifos and mute the phy for preism cac time */
	if (mute)
		wlc_bmac_mute(wlc_hw, ON, PHY_MUTE_FOR_PREISM);

	phy_radio_init((phy_info_t *)wlc_hw->band->pi);

	if ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
	    (CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
	    (CHIPID(wlc_hw->sih->chip) == BCM43526_CHIP_ID) ||
	    (CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID) ||
	    (CHIPID(wlc_hw->sih->chip) == BCM43430_CHIP_ID) ||
	    (CHIPID(wlc_hw->sih->chip) == BCM43018_CHIP_ID) ||
	    (CHIPID(wlc_hw->sih->chip) == BCM4364_CHIP_ID) ||
	    BCM43602_CHIP(wlc_hw->sih->chip) ||
	    BCM4350_CHIP(wlc_hw->sih->chip)) {
		/**
		 * JIRA:SWWLAN-26291. Whenever driver changes BBPLL frequency it needs to adjust
		 * the TSF clock as well.
		 */
		wlc_bmac_switch_macfreq(wlc_hw, 0);
	}

	/* band-specific inits */
	wlc_bmac_bsinit(wlc_hw, chanspec, FALSE);

#if !defined(WL_PROXDETECT)
	/* Phantom devices use sdio & usb core dma to do message transfer */
	/* Low power modes will switch off cores other than host bus */
	/* TOF AVB timer CLK won't work when si_lowpwr_opt is called */
	si_lowpwr_opt(wlc_hw->sih);
#endif
	/* vasip intialization */
	/* TBD: Move VASIP initialization to PHY init */
	wlc_vasip_init(wlc_hw);

	/* restore macintmask */
	wl_intrsrestore(wlc->wl, macintmask);

	/* seed wake_override with WLC_WAKE_OVERRIDE_MACSUSPEND since the mac is suspended
	 * and wlc_bmac_enable_mac() will clear this override bit.
	 */
	mboolset(wlc_hw->wake_override, WLC_WAKE_OVERRIDE_MACSUSPEND);

	/* restore the clk */
	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);

	if (CHIPID(wlc_hw->sih->chip) == BCM4314_CHIP_ID ||
		CHIPID(wlc_hw->sih->chip) == BCM43142_CHIP_ID) {
		uint32 tmp;
		tmp = si_pcielcreg(wlc_hw->sih, 0, 0);
		tmp &= ~0x1;				/* disable L0s */
		si_pcielcreg(wlc_hw->sih, 3, tmp);
	}

	wlc_hw->reinit = FALSE;
} /* wlc_bmac_init */

int
BCMINITFN(wlc_bmac_4331_epa_init)(wlc_hw_info_t *wlc_hw)
{
#define GPIO_5	(1<<5)
	bool is_4331_12x9 = FALSE;

	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));

	if ((BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) &&
	    ((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
	     (CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID)))
		is_4331_12x9 = ((wlc_hw->sih->chippkg == 9 || wlc_hw->sih->chippkg == 0xb));

	if (!is_4331_12x9)
		return (-1);

	si_gpiopull(wlc_hw->sih, GPIO_PULLUP, GPIO_5, 0);
	si_gpiopull(wlc_hw->sih, GPIO_PULLDN, GPIO_5, GPIO_5);

	/* give the control to chip common */
	si_gpiocontrol(wlc_hw->sih, GPIO_2_PA_CTRL_5G_0, 0, GPIO_DRV_PRIORITY);
	/* drive the output to 0 */
	si_gpioout(wlc_hw->sih, GPIO_2_PA_CTRL_5G_0, 0, GPIO_DRV_PRIORITY);
	/* set output disable */
	si_gpioouten(wlc_hw->sih, GPIO_2_PA_CTRL_5G_0, 0, GPIO_DRV_PRIORITY);
	return 0;
}

static void
BCMINITFN(wlc_bmac_config_4331_5GePA)(wlc_hw_info_t *wlc_hw)
{
	bool is_4331_12x9 = FALSE;
	if ((BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) &&
	    ((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
	     (CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID)))
		is_4331_12x9 = ((wlc_hw->sih->chippkg == 9 || wlc_hw->sih->chippkg == 0xb));

	if (!is_4331_12x9)
		return;
	wlc_hw->band->mhfs[MHF1] &= ~MHF1_4331EPA_WAR;
	wlc_write_mhf(wlc_hw, wlc_hw->band->mhfs);
}

/** called during 'wl up' (after wlc_bmac_init), or on a 'big hammer' event */
int
BCMINITFN(wlc_bmac_up_prep)(wlc_hw_info_t *wlc_hw)
{
	uint coremask;

	WL_TRACE(("wl%d: %s:\n", wlc_hw->unit, __FUNCTION__));

	ASSERT(wlc_hw->wlc->pub->hw_up && wlc_hw->macintmask == 0);

	/* JIRA: SWWLAN-27486 optimize power consumption when wireless is down */
	if (BCM4350_CHIP(wlc_hw->sih->chip) &&
	    (CHIPREV(wlc_hw->sih->chiprev) == 0)) {
		si_pmu_chipcontrol(wlc_hw->sih, PMU_CHIPCTL2,
			PMU_CC2_FORCE_PHY_PWR_SWITCH_ON,
			PMU_CC2_FORCE_PHY_PWR_SWITCH_ON);
	}

	/*
	 * Enable pll and xtal, initialize the power control registers,
	 * and force fastclock for the remainder of wlc_up().
	 */
	wlc_bmac_xtal(wlc_hw, ON);
	si_clkctl_init(wlc_hw->sih);
	wlc_clkctl_clk(wlc_hw, CLK_FAST);

	/*
	 * Configure pci/pcmcia here instead of in wlc_attach()
	 * to allow mfg hotswap:  down, hotswap (chip power cycle), up.
	 */
	coremask = (1 << wlc_hw->wlc->core->coreidx);

	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS)
		si_pci_setup(wlc_hw->sih, coremask);
	else if (BUSTYPE(wlc_hw->sih->bustype) == PCMCIA_BUS) {
		wlc_hw->regs = (d11regs_t*)si_setcore(wlc_hw->sih, D11_CORE_ID, wlc_hw->macunit);
		ASSERT(wlc_hw->regs != NULL);
		wlc_hw->wlc->regs = wlc_hw->regs;
		si_pcmcia_init(wlc_hw->sih);
	}
	ASSERT(si_coreid(wlc_hw->sih) == D11_CORE_ID);

	/*
	 * Need to read the hwradio status here to cover the case where the system
	 * is loaded with the hw radio disabled. We do not want to bring the driver up in this case.
	 */
	if (wlc_bmac_radio_read_hwdisabled(wlc_hw)) {
		/* put SB PCI in down state again */
		if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS)
			si_pci_down(wlc_hw->sih);
		wlc_bmac_xtal(wlc_hw, OFF);
		return BCME_RADIOOFF;
	}

	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) {
		if (MRRS != AUTO) {
			si_pcie_set_request_size(wlc_hw->sih, MRRS);
			si_pcie_set_maxpayload_size(wlc_hw->sih, MRRS);
		}

		si_pci_up(wlc_hw->sih);
	}

	/* Jira: SWWLAN-47716: In the down path, the FEM control has been overridden.
	 * Restore FEM control back to its default.
	 */
	if (BCM43602_CHIP(wlc_hw->sih->chip)) {
		si_pmu_chipcontrol(wlc_hw->sih, CHIPCTRLREG1, PMU43602_CC1_GPIO12_OVRD, 0);
	}

	/* reset the d11 core */
	wlc_bmac_corereset(wlc_hw, WLC_USE_COREFLAGS);

	return 0;
} /* wlc_bmac_up_prep */

/** called during 'wl up', after the chanspec has been set */
int
BCMINITFN(wlc_bmac_up_finish)(wlc_hw_info_t *wlc_hw)
{
	WL_TRACE(("wl%d: %s:\n", wlc_hw->unit, __FUNCTION__));

#if defined(BCMDBG_PHYDUMP)
	bzero(wlc_hw->suspend_stats, sizeof(*wlc_hw->suspend_stats));
	wlc_hw->suspend_stats->suspend_start = (uint32)-1;
	wlc_hw->suspend_stats->suspend_end = (uint32)-1;
#endif
	wlc_hw->up = TRUE;

	wlc_phy_hw_state_upd(wlc_hw->band->pi, TRUE);

	/* FULLY enable dynamic power control and d11 core interrupt */
	wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);
	ASSERT(wlc_hw->macintmask == 0);
	ASSERT(!wlc_hw->sbclk || !si_taclear(wlc_hw->sih, TRUE));
	wl_intrson(wlc_hw->wlc->wl);

#if NCONF || HTCONF || ACCONF || ACCONF2
	wlc_bmac_ifsctl_edcrs_set(wlc_hw, WLCISHTPHY(wlc_hw->band));
#endif /* NCONF */
	return 0;
}

/** On some chips, pins are multiplexed and serve either an SROM or WLAN specific function */
int
BCMINITFN(wlc_bmac_set_ctrl_SROM)(wlc_hw_info_t *wlc_hw)
{
	WL_TRACE(("wl%d: %s:\n", wlc_hw->unit, __FUNCTION__));

	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) {
		if ((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID)) {
			WL_INFORM(("wl%d: %s: set mux pin to SROM\n", wlc_hw->unit, __FUNCTION__));
			/* force muxed pin to control SROM */
			si_chipcontrl_epa4331(wlc_hw->sih, FALSE);
		} else if (BCM43602_CHIP(wlc_hw->sih->chip) ||
			(((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID)) &&
			(CHIPREV(wlc_hw->sih->chiprev) <= 2))) {
			si_chipcontrl_srom4360(wlc_hw->sih, TRUE);
		}
	}

	return 0;
}

int
BCMINITFN(wlc_bmac_set_ctrl_ePA)(wlc_hw_info_t *wlc_hw)
{
	WL_TRACE(("wl%d: %s:\n", wlc_hw->unit, __FUNCTION__));

	if (!wlc_hw->clk) {
		WL_ERROR(("wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__));
		return -1;
	}
	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) {
		if ((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
		    (CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID)) {
			WL_INFORM(("wl%d: %s: set mux pin to ePA\n", wlc_hw->unit, __FUNCTION__));
			/* force muxed pin to control ePA */
			si_chipcontrl_epa4331(wlc_hw->sih, TRUE);
		}
	}

	return 0;
}

int
BCMINITFN(wlc_bmac_set_ctrl_bt_shd0)(wlc_hw_info_t *wlc_hw, bool enable)
{
	WL_TRACE(("wl%d: %s:\n", wlc_hw->unit, __FUNCTION__));

	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) {
		if (((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
		     (CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID)) &&
		    ((wlc_hw->sih->boardtype == BCM94331X28) ||
		     (wlc_hw->sih->boardtype == BCM94331X28B) ||
		     (wlc_hw->sih->boardtype == BCM94331CS_SSID) ||
		     (wlc_hw->sih->boardtype == BCM94331X29B) ||
		     (wlc_hw->sih->boardtype == BCM94331X29D))) {
			if (enable) {
				/* force muxed pin to bt_shd0 */
				WL_INFORM(("wl%d: %s: set mux pin to bt_shd0\n",
				           wlc_hw->unit, __FUNCTION__));
				si_chipcontrl_btshd0_4331(wlc_hw->sih, TRUE);
			} else {
				/* restore muxed pin to default state */
				WL_INFORM(("wl%d: %s: set mux pin to default (gpio4) \n",
				           wlc_hw->unit, __FUNCTION__));
				si_chipcontrl_btshd0_4331(wlc_hw->sih, FALSE);
			}
		}
	}

	return 0;
}

#ifndef BCMNODOWN
/** tear down d11 interrupts, cancel BMAC software timers, tear down PHY operation */
int
BCMUNINITFN(wlc_bmac_down_prep)(wlc_hw_info_t *wlc_hw)
{
	bool dev_gone;
	uint callbacks = 0;

	WL_TRACE(("wl%d: %s:\n", wlc_hw->unit, __FUNCTION__));

	if (!wlc_hw->up)
		return callbacks;

	dev_gone = DEVICEREMOVED(wlc_hw->wlc);

	/* disable interrupts */
	if (dev_gone)
		wlc_hw->macintmask = 0;
	else {
		/* now disable interrupts */
		wl_intrsoff(wlc_hw->wlc->wl);

		/* ensure we're running on the pll clock again */
		wlc_clkctl_clk(wlc_hw, CLK_FAST);

		/* Disable GPIOs related to BTC returning the control to chipcommon */
		if (!wlc_hw->noreset)
			wlc_bmac_btc_gpio_disable(wlc_hw);
	}

	/* apply gpio WAR in the down path */
	wlc_bmac_gpio_configure(wlc_hw, FALSE);

	if (CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) {
		wlc_bmac_write_shm(wlc_hw, M_EXTLNA_PWRSAVE(wlc_hw), 0x480);
	}

	/* save a copy of the btc params before going down */

	/* down phy at the last of this stage */
	callbacks += phy_down((phy_info_t *)wlc_hw->band->pi);

	return callbacks;
} /* wlc_bmac_down_prep */

uint32 wlc_bmac_current_pmu_time(wlc_info_t *wlc)
{
	ASSERT(FALSE);
	return 0;
}

void
BCMUNINITFN(wlc_bmac_hw_down)(wlc_hw_info_t *wlc_hw)
{
	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) {
		wlc_bmac_set_ctrl_SROM(wlc_hw);

		if ((wlc_hw->sih->boardvendor == VENDOR_APPLE) &&
		    ((wlc_hw->sih->boardtype == BCM94360X29C) ||
		     (wlc_hw->sih->boardtype == BCM94360X29CP2) ||
		     (wlc_hw->sih->boardtype == BCM94360X29CP3) ||
		     (wlc_hw->sih->boardtype == BCM94360X52C))) {
			/* Set GPIO7 as input */
			si_pmu_chipcontrol(wlc_hw->sih, CHIPCTRLREG1, PMU4360_CC1_GPIO7_OVRD, 0);
			/* Switch pin MUX from FEM to GPIO control */
			si_corereg(wlc_hw->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol),
				CCTRL4360_BTSWCTRL_MODE, 0);
		}
		wlc_bmac_set_ctrl_bt_shd0(wlc_hw, FALSE);
		si_pci_down(wlc_hw->sih);
#if defined(SAVERESTORE)
		/* for NIC mode, disable SR if we're down */
		if (SR_ENAB() && sr_cap(wlc_hw->sih)) {
			sr_engine_enable(wlc_hw->sih, IOV_SET, FALSE);
		}
#endif 
	}

	/* Jira: SWWLAN-47716: override the FEM control to GPIO (High-Z) so that in down state
	 * the pin is not driven low which causes excess current draw.
	 */
	if (BCM43602_CHIP(wlc_hw->sih->chip)) {
		si_pmu_chipcontrol(wlc_hw->sih, CHIPCTRLREG1,
			PMU43602_CC1_GPIO12_OVRD, PMU43602_CC1_GPIO12_OVRD);
	}

	/* JIRA: SWWLAN-23710 */
	if ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
		BCM43602_CHIP(wlc_hw->sih->chip) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43526_CHIP_ID))
		si_pmu_rfldo(wlc_hw->sih, 0);

	wlc_bmac_xtal(wlc_hw, OFF);

	/* JIRA: SWWLAN-27486 optimize power consumption when wireless is down */
	if (BCM4350_CHIP(wlc_hw->sih->chip) &&
	    (CHIPREV(wlc_hw->sih->chiprev) == 0)) {
		si_pmu_chipcontrol(wlc_hw->sih, PMU_CHIPCTL2, PMU_CC2_FORCE_PHY_PWR_SWITCH_ON, 0);
	}
}

int
BCMUNINITFN(wlc_bmac_down_finish)(wlc_hw_info_t *wlc_hw)
{
	uint callbacks = 0;
	bool dev_gone;

	WL_TRACE(("wl%d: %s:\n", wlc_hw->unit, __FUNCTION__));

	if (!wlc_hw->up)
		return callbacks;

	wlc_hw->up = FALSE;
	wlc_phy_hw_state_upd(wlc_hw->band->pi, FALSE);

	dev_gone = DEVICEREMOVED(wlc_hw->wlc);

	if (dev_gone) {
		wlc_hw->sbclk = FALSE;
		wlc_hw->clk = FALSE;
		wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, FALSE);

		/* reclaim any posted packets */
		wlc_flushqueues(wlc_hw);
	} else if (!wlc_hw->wlc->psm_watchdog_debug) {

		/* Reset and disable the core */
		if (si_iscoreup(wlc_hw->sih)) {
			if (R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) & MCTL_EN_MAC)
				wlc_bmac_suspend_mac_and_wait(wlc_hw);
			callbacks += wl_reset(wlc_hw->wlc->wl);
			wlc_coredisable(wlc_hw);
		}

		/* turn off primary xtal and pll */
		if (!wlc_hw->noreset)
			wlc_bmac_hw_down(wlc_hw);
	}

	ASSERT(!wlc_hw->sbclk || !si_taclear(wlc_hw->sih, TRUE));

	return callbacks;
}
#endif	/* BCMNODOWN */

/** 802.11 Power State (PS) related */
void
wlc_bmac_wait_for_wake(wlc_hw_info_t *wlc_hw)
{
	/* delay before first read of ucode state */
	if (!(wlc_hw->band)) {
		WL_ERROR(("wl%d: %s:Active per-band state not set. \n",
			wlc_hw->unit, __FUNCTION__));
		return;
	}
	OSL_DELAY(40);

	/* wait until ucode is no longer asleep */
	SPINWAIT((wlc_bmac_read_shm(wlc_hw, M_UCODE_DBGST(wlc_hw)) == DBGST_ASLEEP),
		wlc_hw->fastpwrup_dly);

#ifdef WAR4360_UCODE
	if (wlc_bmac_read_shm(wlc_hw, M_UCODE_DBGST(wlc_hw)) == DBGST_ASLEEP) {
		WL_ERROR(("wl%d:%s: Hammering due to M_UCODE_DBGST==DBGST_ASLEEP\n",
			wlc_hw->unit, __FUNCTION__));
		wlc_hw->need_reinit = 3;
		return;
	}
#endif /* WAR4360_UCODE */
	ASSERT(wlc_bmac_read_shm(wlc_hw, M_UCODE_DBGST(wlc_hw)) != DBGST_ASLEEP);
}

void
wlc_bmac_hw_etheraddr(wlc_hw_info_t *wlc_hw, struct ether_addr *ea)
{
	bcopy(&wlc_hw->etheraddr, ea, ETHER_ADDR_LEN);
}

void
wlc_bmac_set_hw_etheraddr(wlc_hw_info_t *wlc_hw, struct ether_addr *ea)
{
	bcopy(ea, &wlc_hw->etheraddr, ETHER_ADDR_LEN);
}

int
wlc_bmac_bandtype(wlc_hw_info_t *wlc_hw)
{
	return (wlc_hw->band->bandtype);
}

void *
wlc_cur_phy(wlc_info_t *wlc)
{
	wlc_hw_info_t *wlc_hw = wlc->hw;
	return ((void *)wlc_hw->band->pi);
}

/** control chip clock to save power, enable dynamic clock or force fast clock */
static void
wlc_clkctl_clk(wlc_hw_info_t *wlc_hw, uint mode)
{
	if (PMUCTL_ENAB(wlc_hw->sih)) {
		/* new chips with PMU, CCS_FORCEHT will distribute the HT clock on backplane,
		 *  but mac core will still run on ALP(not HT) when it enters powersave mode,
		 *      which means the FCA bit may not be set.
		 *      should wakeup mac if driver wants it to run on HT.
		 */

		if (wlc_hw->clk) {
			if (mode == CLK_FAST) {
				OR_REG(wlc_hw->osh, &wlc_hw->regs->clk_ctl_st, CCS_FORCEHT);

				OSL_DELAY(64);

				SPINWAIT(((R_REG(wlc_hw->osh, &wlc_hw->regs->clk_ctl_st) &
				           CCS_HTAVAIL) == 0), PMU_MAX_TRANSITION_DLY);
				ASSERT(R_REG(wlc_hw->osh, &wlc_hw->regs->clk_ctl_st) &
					CCS_HTAVAIL);
			} else {
				if ((PMUREV(wlc_hw->sih->pmurev) == 0) &&
				    (R_REG(wlc_hw->osh, &wlc_hw->regs->clk_ctl_st) &
				     (CCS_FORCEHT | CCS_HTAREQ)))
					SPINWAIT(((R_REG(wlc_hw->osh, &wlc_hw->regs->clk_ctl_st) &
					           CCS_HTAVAIL) == 0), PMU_MAX_TRANSITION_DLY);
				AND_REG(wlc_hw->osh, &wlc_hw->regs->clk_ctl_st, ~CCS_FORCEHT);
			}
		}
		wlc_hw->forcefastclk = (mode == CLK_FAST);
	} else {
		wlc_hw->forcefastclk = si_clkctl_cc(wlc_hw->sih, mode);

		/* check fast clock is available (if core is not in reset) */
		if (wlc_hw->forcefastclk && wlc_hw->clk)
			ASSERT(si_core_sflags(wlc_hw->sih, 0, 0) & SISF_FCLKA);

		/* keep the ucode wake bit on if forcefastclk is on
		 * since we do not want ucode to put us back to slow clock
		 * when it dozes for PM mode.
		 * Code below matches the wake override bit with current forcefastclk state
		 * Only setting bit in wake_override instead of waking ucode immediately
		 * since old code (wlc.c 1.4499) had this behavior. Older code set
		 * wlc->forcefastclk but only had the wake happen if the wakup_ucode work
		 * (protected by an up check) was executed just below.
		 */
		if (wlc_hw->forcefastclk)
			mboolset(wlc_hw->wake_override, WLC_WAKE_OVERRIDE_FORCEFAST);
		else
			mboolclr(wlc_hw->wake_override, WLC_WAKE_OVERRIDE_FORCEFAST);

	}
} /* wlc_clkctl_clk */

/** Forcing Core1's HW request Off bit in PM Mode for MIMO and 80P80 */
void
wlc_bmac_4349_core1_hwreqoff(wlc_hw_info_t *wlc_hw, bool mode)
{
	int sicoreunit = 0;

	ASSERT(wlc_hw != NULL);
	sicoreunit = si_coreunit(wlc_hw->sih);

	/* Apply the setting to D11 core unit one always */
		if ((wlc_rsdb_mode(wlc_hw->wlc) == PHYMODE_MIMO) ||
		(wlc_rsdb_mode(wlc_hw->wlc) == PHYMODE_80P80)) {
			d11regs_t *regs = si_d11_switch_addrbase(wlc_hw->sih, 1);
			if (mode == TRUE)
				OR_REG(wlc_hw->osh, &regs->clk_ctl_st, CCS_FORCEHWREQOFF);
			else
				AND_REG(wlc_hw->osh, &regs->clk_ctl_st, ~CCS_FORCEHWREQOFF);
		}

	si_d11_switch_addrbase(wlc_hw->sih, sicoreunit);
}

/**
 * Update the hardware for rcvlazy (interrupt mitigation) setting changes
 */
void
wlc_bmac_rcvlazy_update(wlc_hw_info_t *wlc_hw, uint32 intrcvlazy)
{
	W_REG(wlc_hw->osh, &wlc_hw->regs->intrcvlazy[0], intrcvlazy);

	/* interrupt for second fifo - 1 */
	if (PKT_CLASSIFY_EN(RX_FIFO1) || RXFIFO_SPLIT()) {
		W_REG(wlc_hw->osh, &wlc_hw->regs->intrcvlazy[RX_FIFO1], intrcvlazy);
	}
	/* interrupt for second fifo - 2 */
	if (PKT_CLASSIFY_EN(RX_FIFO2)) {
		W_REG(wlc_hw->osh, &wlc_hw->regs->intrcvlazy[RX_FIFO2], intrcvlazy);
	}
}

/** set initial host flags value. Ucode interprets these host flags. */
static void
BCMATTACHFN(wlc_mhfdef)(wlc_hw_info_t *wlc_hw, uint16 *mhfs, uint16 mhf2_init)
{
	bzero(mhfs, sizeof(uint16) * MHFMAX);

	mhfs[MHF2] |= mhf2_init;

	/* WAR for pin mux between ePA & SROM for 4331 12x9 package */
	{
	bool is_4331_12x9 = FALSE;
	is_4331_12x9 = (CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
	        (CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID);
	is_4331_12x9 &= ((wlc_hw->sih->chippkg == 9 || wlc_hw->sih->chippkg == 0xb));
	if (is_4331_12x9)
		mhfs[MHF1] |= MHF1_4331EPA_WAR;
	}

	/* prohibit use of slowclock on multifunction boards */
	if (wlc_hw->boardflags & BFL_NOPLLDOWN)
		mhfs[MHF1] |= MHF1_FORCEFASTCLK;

	if ((wlc_hw->band->radioid == BCM2050_ID) && (wlc_hw->band->radiorev < 6))
		mhfs[MHF2] |= MHF2_SYNTHPUWAR;

#ifdef WLFCTS
	if (WLFCTS_ENAB(wlc_hw->wlc->pub)) {
		ASSERT(D11REV_GE(wlc_hw->corerev, 26));
		mhfs[MHF2] |= MHF2_TX_TMSTMP;
	}
#endif /* WLFCTS */

	/* set host flag to enable ucode for srom9: tx power offset based on txpwrctrl word */
	if (WLCISNPHY(wlc_hw->band) && (wlc_hw->sromrev >= 9)) {
		mhfs[MHF2] |= MHF2_PPR_HWPWRCTL;
	}

	if (CHIPID(wlc_hw->sih->chip) == BCM43526_CHIP_ID) {
		/* hostflag to tell the ucode that the interface is USB.
		ucode doesn't pull the HT request from the backplane.
		*/
		mhfs[MHF3] |= MHF3_USB_OLD_NPHYMLADVWAR;
	}
}

/**
 * set or clear ucode host flag bits
 * it has an optimization for no-change write
 * it only writes through shared memory when the core has clock;
 * pre-CLK changes should use wlc_write_mhf to get around the optimization
 *
 * bands values are: WLC_BAND_AUTO <--- Current band only
 *                   WLC_BAND_5G   <--- 5G band only
 *                   WLC_BAND_2G   <--- 2G band only
 *                   WLC_BAND_ALL  <--- All bands
 */
void
wlc_bmac_mhf(wlc_hw_info_t *wlc_hw, uint8 idx, uint16 mask, uint16 val, int bands)
{
	uint16 save;
	const uint16 addr[] = {M_HOST_FLAGS(wlc_hw), M_HOST_FLAGS2(wlc_hw), M_HOST_FLAGS3(wlc_hw),
		M_HOST_FLAGS4(wlc_hw), M_HOST_FLAGS5(wlc_hw)};
	wlc_hwband_t *band;

	ASSERT((val & ~mask) == 0);
	ASSERT(idx < MHFMAX);
	ASSERT(ARRAYSIZE(addr) == MHFMAX);

	switch (bands) {
		/* Current band only or all bands,
		 * then set the band to current band
		 */
	case WLC_BAND_AUTO:
	case WLC_BAND_ALL:
		band = wlc_hw->band;
		break;
	case WLC_BAND_5G:
		band = wlc_hw->bandstate[BAND_5G_INDEX];
		break;
	case WLC_BAND_2G:
		band = wlc_hw->bandstate[BAND_2G_INDEX];
		break;
	default:
		ASSERT(0);
		band = NULL;
	}

	if (band) {
		save = band->mhfs[idx];
		band->mhfs[idx] = (band->mhfs[idx] & ~mask) | val;

		/* optimization: only write through if changed, and
		 * changed band is the current band
		 */
		if (wlc_hw->clk && (band->mhfs[idx] != save) && (band == wlc_hw->band))
			wlc_bmac_write_shm(wlc_hw, addr[idx], (uint16)band->mhfs[idx]);
	}

	if (bands == WLC_BAND_ALL) {
		wlc_hw->bandstate[0]->mhfs[idx] = (wlc_hw->bandstate[0]->mhfs[idx] & ~mask) | val;
		wlc_hw->bandstate[1]->mhfs[idx] = (wlc_hw->bandstate[1]->mhfs[idx] & ~mask) | val;
	}
} /* wlc_bmac_mhf */

uint16
wlc_bmac_mhf_get(wlc_hw_info_t *wlc_hw, uint8 idx, int bands)
{
	wlc_hwband_t *band;
	ASSERT(idx < MHFMAX);

	switch (bands) {
	case WLC_BAND_AUTO:
		band = wlc_hw->band;
		break;
	case WLC_BAND_5G:
		band = wlc_hw->bandstate[BAND_5G_INDEX];
		break;
	case WLC_BAND_2G:
		band = wlc_hw->bandstate[BAND_2G_INDEX];
		break;
	default:
		ASSERT(0);
		band = NULL;
	}

	if (!band)
		return 0;

	return band->mhfs[idx];
}

static void
wlc_write_mhf(wlc_hw_info_t *wlc_hw, uint16 *mhfs)
{
	uint8 idx;
	const uint16 addr[] = {M_HOST_FLAGS(wlc_hw), M_HOST_FLAGS2(wlc_hw), M_HOST_FLAGS3(wlc_hw),
		M_HOST_FLAGS4(wlc_hw), M_HOST_FLAGS5(wlc_hw)};

	ASSERT(ARRAYSIZE(addr) == MHFMAX);

	for (idx = 0; idx < MHFMAX; idx++) {
		wlc_bmac_write_shm(wlc_hw, addr[idx], mhfs[idx]);
	}
}

void
wlc_bmac_ifsctl1_regshm(wlc_hw_info_t *wlc_hw, uint32 mask, uint32 val)
{
	osl_t *osh;
	d11regs_t *regs;
	uint32 w;
	volatile uint16 *ifsctl_reg;

	if (D11REV_GE(wlc_hw->corerev, 40))
		return;

	osh = wlc_hw->osh;
	regs = wlc_hw->regs;

	ifsctl_reg = (volatile uint16 *) &regs->u.d11regs.ifs_ctl1;

	w = (R_REG(osh, ifsctl_reg) & ~mask) | val;
	W_REG(osh, ifsctl_reg, w);

	wlc_bmac_write_shm(wlc_hw, M_IFSCTL1(wlc_hw), (uint16)w);
}

#ifdef WL_PROXDETECT
/**
 * Proximity detection service - enables a way for mobile devices to pair based on relative
 * proximity.
 */
void wlc_enable_avb_timer(wlc_hw_info_t *wlc_hw, bool enable)
{
	osl_t *osh;
	d11regs_t *regs;

	osh = wlc_hw->osh;
	regs = wlc_hw->regs;

	if (enable) {
		OR_REG(osh, &regs->clk_ctl_st, CCS_AVBCLKREQ);
		OR_REG(osh, &regs->maccontrol1, MCTL1_AVB_ENABLE);
	} else {
		AND_REG(osh, &regs->clk_ctl_st, ~CCS_AVBCLKREQ);
		AND_REG(osh, &regs->maccontrol1, ~MCTL1_AVB_ENABLE);
	}

	/* enable/disable the avb timer */
	si_pmu_avb_clk_set(wlc_hw->sih, osh, enable);
}

void wlc_get_avb_timer_reg(wlc_hw_info_t *wlc_hw, uint32 *clkst, uint32 *maccontrol1)
{
	osl_t *osh;
	d11regs_t *regs;

	osh = wlc_hw->osh;
	regs = wlc_hw->regs;

	if (clkst)
		*clkst = R_REG(osh, &regs->clk_ctl_st);
	if (maccontrol1)
		*maccontrol1 = R_REG(osh, &regs->maccontrol1);
}

void wlc_get_avb_timestamp(wlc_hw_info_t *wlc_hw, uint32* ptx, uint32* prx)
{
	osl_t *osh;
	d11regs_t *regs;

	osh = wlc_hw->osh;
	regs = wlc_hw->regs;

	*ptx = R_REG(osh, &regs->avbtx_timestamp);
	*prx = R_REG(osh, &regs->avbrx_timestamp);
}
#endif /* WL_PROXDETECT */


/**
 * set the maccontrol register to desired reset state and
 * initialize the sw cache of the register
 */
void
wlc_mctrl_reset(wlc_hw_info_t *wlc_hw)
{
	/* IHR accesses are always enabled, PSM disabled, HPS off and WAKE on */
	wlc_hw->maccontrol = 0;
	wlc_hw->suspended_fifos = 0;
	wlc_hw->wake_override = 0;
	wlc_hw->mute_override = 0;
	wlc_bmac_mctrl(wlc_hw, ~0, MCTL_IHR_EN | MCTL_WAKE);
}

/** set or clear maccontrol bits */
void
wlc_bmac_mctrl(wlc_hw_info_t *wlc_hw, uint32 mask, uint32 val)
{
	uint32 maccontrol;
	uint32 new_maccontrol;

	ASSERT((val & ~mask) == 0);

	maccontrol = wlc_hw->maccontrol;
	new_maccontrol = (maccontrol & ~mask) | val;

	/* if the new maccontrol value is the same as the old, nothing to do */
	if (new_maccontrol == maccontrol)
		return;

	/* something changed, cache the new value */
	wlc_hw->maccontrol = new_maccontrol;

	/* write the new values with overrides applied */
	wlc_mctrl_write(wlc_hw);
}

/** write the software state of maccontrol and overrides to the maccontrol register */
static void
wlc_mctrl_write(wlc_hw_info_t *wlc_hw)
{
	uint32 maccontrol = wlc_hw->maccontrol;

	/* OR in the wake bit if overridden */
	if (wlc_hw->wake_override)
		maccontrol |= MCTL_WAKE;

	/* set AP and INFRA bits for mute if needed */
	if (wlc_hw->mute_override) {
		maccontrol &= ~(MCTL_AP);
		maccontrol |= MCTL_INFRA;
	}

	W_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol, maccontrol);
}

void
wlc_ucode_wake_override_set(wlc_hw_info_t *wlc_hw, uint32 override_bit)
{
	ASSERT((wlc_hw->wake_override & override_bit) == 0);

	if (wlc_hw->wake_override || (wlc_hw->maccontrol & MCTL_WAKE)) {
		mboolset(wlc_hw->wake_override, override_bit);
		return;
	}

	mboolset(wlc_hw->wake_override, override_bit);

	wlc_mctrl_write(wlc_hw);
	wlc_bmac_wait_for_wake(wlc_hw);

	return;
}

void
wlc_ucode_wake_override_clear(wlc_hw_info_t *wlc_hw, uint32 override_bit)
{
	ASSERT(wlc_hw->wake_override & override_bit);

	mboolclr(wlc_hw->wake_override, override_bit);

	if (wlc_hw->wake_override || (wlc_hw->maccontrol & MCTL_WAKE))
		return;

	wlc_mctrl_write(wlc_hw);

	return;
}

/**
 * Prevents ucode from transmitting beacons and probe responses.
 *
 * When driver needs ucode to stop beaconing, it has to make sure that
 * MCTL_AP is clear and MCTL_INFRA is set
 * Mode           MCTL_AP        MCTL_INFRA
 * AP                1              1
 * STA               0              1 <--- This will ensure no beacons
 * IBSS              0              0
 */
static void
wlc_ucode_mute_override_set(wlc_hw_info_t *wlc_hw)
{
	wlc_hw->mute_override = 1;

	/* if maccontrol already has AP == 0 and INFRA == 1 without this
	 * override, then there is no change to write
	 */
	if ((wlc_hw->maccontrol & (MCTL_AP | MCTL_INFRA)) == MCTL_INFRA)
		return;

	wlc_mctrl_write(wlc_hw);

	return;
}

/** Clear the override on AP and INFRA bits */
static void
wlc_ucode_mute_override_clear(wlc_hw_info_t *wlc_hw)
{
	if (wlc_hw->mute_override == 0)
		return;

	wlc_hw->mute_override = 0;

	/* if maccontrol already has AP == 0 and INFRA == 1 without this
	 * override, then there is no change to write
	 */
	if ((wlc_hw->maccontrol & (MCTL_AP | MCTL_INFRA)) == MCTL_INFRA)
		return;

	wlc_mctrl_write(wlc_hw);
}

/**
 * Updates suspended_fifos admin when suspending a txfifo
 * and may set the ucode wake override bit
 */
void
wlc_upd_suspended_fifos_set(wlc_hw_info_t *wlc_hw, uint txfifo)
{
	if (wlc_hw->suspended_fifos == 0) {
		wlc_ucode_wake_override_set(wlc_hw, WLC_WAKE_OVERRIDE_TXFIFO);
	}
	wlc_hw->suspended_fifos |= (1 << txfifo);
}

/**
 * Updates suspended_fifos admin when resuming a txfifo
 * and may clear the ucode wake override bit
 */
void
wlc_upd_suspended_fifos_clear(wlc_hw_info_t *wlc_hw, uint txfifo)
{
	if (!wlc_hw->suspended_fifos == 0) {
		wlc_hw->suspended_fifos &= ~(1 << txfifo);
		if (wlc_hw->suspended_fifos == 0) {
			wlc_ucode_wake_override_clear(wlc_hw, WLC_WAKE_OVERRIDE_TXFIFO);
		}
	}
}

/**
 * Add a MAC address to the rcmta memory in the D11 core. This is an associative memory used to
 * quickly compare a received address with a preloaded set of addresses.
 */
void
wlc_bmac_set_rcmta(wlc_hw_info_t *wlc_hw, int idx, const struct ether_addr *addr)
{
	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));

	ASSERT(idx >= 0);	/* This routine only for non primary interfaces */

	wlc_bmac_copyto_objmem(wlc_hw, (idx * 2) << 2, addr->octet,
		ETHER_ADDR_LEN, OBJADDR_RCMTA_SEL);
}

void
wlc_bmac_get_rcmta(wlc_hw_info_t *wlc_hw, int idx, struct ether_addr *addr)
{
	ASSERT(idx >= 0);	/* This routine only for non primary interfaces */

	wlc_bmac_copyfrom_objmem(wlc_hw, (idx * 2) << 2, addr->octet,
		ETHER_ADDR_LEN, OBJADDR_RCMTA_SEL);
}

/** for d11 rev >= 40, RCMTA was replaced with AMT (Address Match Table) */
void
wlc_bmac_read_amt(wlc_hw_info_t *wlc_hw, int idx, struct ether_addr *addr, uint16 *attr)
{
	uint32 word[2];

	WL_TRACE(("wl%d: %s: idx %d\n", wlc_hw->unit, __FUNCTION__, idx));
	ASSERT(wlc_hw->corerev >= 40);

	wlc_bmac_copyfrom_objmem(wlc_hw, (idx * 2) << 2, word,
		sizeof(word), OBJADDR_AMT_SEL);

	addr->octet[0] = (uint8)word[0];
	addr->octet[1] = (uint8)(word[0] >> 8);
	addr->octet[2] = (uint8)(word[0] >> 16);
	addr->octet[3] = (uint8)(word[0] >> 24);
	addr->octet[4] = (uint8)word[1];
	addr->octet[5] = (uint8)(word[1] >> 8);
	*attr = (word[1] >> 16);
}

void
wlc_bmac_amt_dump(wlc_hw_info_t *wlc_hw)
{
	struct ether_addr addr;
	uint16 attr = 0;

	wlc_bmac_read_amt(wlc_hw, AMT_IDX_MAC, &addr, &attr);
	WL_ERROR(("%s: mac etheraddr %02x:%02x:%02x:%02x:%02x:%02x  attr %x\n", __FUNCTION__,
		addr.octet[0], addr.octet[1],
		addr.octet[2], addr.octet[3],
		addr.octet[4], addr.octet[5], attr));

	attr = 0;
	wlc_bmac_read_amt(wlc_hw, AMT_IDX_BSSID, &addr, &attr);
	WL_ERROR(("%s: bssid etheraddr %02x:%02x:%02x:%02x:%02x:%02x  attr %x\n", __FUNCTION__,
		addr.octet[0], addr.octet[1], addr.octet[2], addr.octet[3],
		addr.octet[4], addr.octet[5], attr));
}

/** Write a MAC address to the AMT (Address Match Table) */
uint16
wlc_bmac_write_amt(wlc_hw_info_t *wlc_hw, int idx, const struct ether_addr *addr, uint16 attr)
{
	uint32 word[2];
	struct ether_addr prev_addr;
	uint16 prev_attr  = 0;

	WL_TRACE(("wl%d: %s: idx %d\n", wlc_hw->unit, __FUNCTION__, idx));
	ASSERT(wlc_hw->corerev >= 40);

	/* Read/Modify/Write unless entry is being disabled */
	wlc_bmac_read_amt(wlc_hw, idx, &prev_addr, &prev_attr);
	if (attr & AMT_ATTR_VALID) {
		attr |= prev_attr;
	} else {
		attr = 0;
	}

	word[0] = (addr->octet[3] << 24) |
	        (addr->octet[2] << 16) |
	        (addr->octet[1] << 8) |
	        addr->octet[0];
	word[1] = (attr << 16) |
	        (addr->octet[5] << 8) |
	        addr->octet[4];

	wlc_bmac_copyto_objmem(wlc_hw, (idx * 2) << 2, word, sizeof(word), OBJADDR_AMT_SEL);

	return prev_attr;
}

/** Write a MAC address to the given match reg offset in the RXE match engine. */
void
wlc_bmac_set_rxe_addrmatch(wlc_hw_info_t *wlc_hw, int match_reg_offset,
	const struct ether_addr *addr)
{
	d11regs_t *regs;
	uint16 mac_l;
	uint16 mac_m;
	uint16 mac_h;
	osl_t *osh;

	WL_TRACE(("wl%d: %s: offset %d\n", wlc_hw->unit, __FUNCTION__,
	          match_reg_offset));

	ASSERT(wlc_hw->corerev < 40);
	ASSERT((match_reg_offset < RCM_SIZE) || (wlc_hw->corerev == 4));

	/* RCM addrmatch is replaced by AMT in d11 rev40 */
	if (D11REV_GE(wlc_hw->corerev, 40)) {
		WL_ERROR(("wl%d: %s: RCM addrmatch not available on corerev >= 40\n",
		          wlc_hw->unit, __FUNCTION__));
		return;
	}

	regs = wlc_hw->regs;
	mac_l = addr->octet[0] | (addr->octet[1] << 8);
	mac_m = addr->octet[2] | (addr->octet[3] << 8);
	mac_h = addr->octet[4] | (addr->octet[5] << 8);


	osh = wlc_hw->osh;

	/* enter the MAC addr into the RXE match registers */
	W_REG(osh, &regs->u_rcv.d11regs.rcm_ctl, RCM_INC_DATA | match_reg_offset);
	W_REG(osh, &regs->u_rcv.d11regs.rcm_mat_data, mac_l);
	W_REG(osh, &regs->u_rcv.d11regs.rcm_mat_data, mac_m);
	W_REG(osh, &regs->u_rcv.d11regs.rcm_mat_data, mac_h);

} /* wlc_bmac_set_rxe_addrmatch */

static void
wlc_bmac_set_match_mac(wlc_hw_info_t *wlc_hw, const struct ether_addr *addr)
{
	if (D11REV_LT(wlc_hw->corerev, 40)) {
		wlc_bmac_set_rxe_addrmatch(wlc_hw, RCM_MAC_OFFSET, addr);
	} else {
		wlc_bmac_write_amt(wlc_hw, AMT_IDX_MAC, addr, (AMT_ATTR_VALID | AMT_ATTR_A1));
	}
}

static void
wlc_bmac_clear_match_mac(wlc_hw_info_t *wlc_hw)
{
	if (D11REV_LT(wlc_hw->corerev, 40)) {
		wlc_bmac_set_rxe_addrmatch(wlc_hw, RCM_MAC_OFFSET, &ether_null);
	} else {
		wlc_bmac_write_amt(wlc_hw, AMT_IDX_MAC, &ether_null, 0);
	}
}

static void
wlc_bmac_reset_amt(wlc_hw_info_t *wlc_hw)
{
	int i;
	for (i = 0; i < AMT_SIZE; i++)
		wlc_bmac_write_amt(wlc_hw, i, &ether_null, 0);
}

/**
 * Template memory is located in the d11 core, and is used by ucode to transmit frames based on a
 * preloaded template.
 */
void
wlc_bmac_templateptr_wreg(wlc_hw_info_t *wlc_hw, int offset)
{
	ASSERT(ISALIGNED(offset, sizeof(uint32)));

	/* Correct the template read pointer according to mac core using templatebase */
	offset = offset + wlc_hw->templatebase;
	W_REG(wlc_hw->osh, &wlc_hw->regs->tplatewrptr, offset);
}

uint32
wlc_bmac_templateptr_rreg(wlc_hw_info_t *wlc_hw)
{
	return R_REG(wlc_hw->osh, &wlc_hw->regs->tplatewrptr);
}

void
wlc_bmac_templatedata_wreg(wlc_hw_info_t *wlc_hw, uint32 word)
{
	W_REG(wlc_hw->osh, &wlc_hw->regs->tplatewrdata, word);
}

uint32
wlc_bmac_templatedata_rreg(wlc_hw_info_t *wlc_hw)
{
	return R_REG(wlc_hw->osh, &wlc_hw->regs->tplatewrdata);
}

void
wlc_bmac_write_template_ram(wlc_hw_info_t *wlc_hw, int offset, int len, void *buf)
{
	d11regs_t *regs = wlc_hw->regs;
	uint32 word;
	bool be_bit;
#ifdef IL_BIGENDIAN
	volatile uint16 *dptr = NULL;
#endif /* IL_BIGENDIAN */
	osl_t *osh = wlc_hw->osh;

	WL_TRACE(("wl%d: wlc_bmac_write_template_ram\n", wlc_hw->unit));

	ASSERT(ISALIGNED(offset, sizeof(uint32)));
	ASSERT(ISALIGNED(len, sizeof(uint32)));
	ASSERT((offset & ~0xffff) == 0);

	/* The template region starts where the BMC_STARTADDR starts.
	 * This shouldn't use a #defined value but some parameter in a
	 * global struct.
	 */
	if (D11REV_IS(wlc_hw->corerev, 48) || D11REV_IS(wlc_hw->corerev, 49))
		offset += (D11MAC_BMC_STARTADDR_SRASM << 8);
	wlc_bmac_templateptr_wreg(wlc_hw, offset);

#ifdef IL_BIGENDIAN
	if (BUSTYPE(wlc_hw->sih->bustype) == PCMCIA_BUS)
		dptr = (volatile uint16*)&regs->tplatewrdata;
#endif /* IL_BIGENDIAN */

	/* if MCTL_BIGEND bit set in mac control register,
	 * the chip swaps data in fifo, as well as data in
	 * template ram
	 */
	be_bit = (R_REG(osh, &regs->maccontrol) & MCTL_BIGEND) != 0;

	while (len > 0) {
		bcopy((uint8*)buf, &word, sizeof(uint32));

		if (be_bit)
			word = hton32(word);
		else
			word = htol32(word);

		wlc_bmac_templatedata_wreg(wlc_hw, word);

		buf = (uint8*)buf + sizeof(uint32);
		len -= sizeof(uint32);
	}
} /* wlc_bmac_write_template_ram */

void
wlc_bmac_write_template_const_ram(wlc_hw_info_t *wlc_hw, int offset, int len, const void *buf)
{
	uint32 word;
	int i = 0;
	const uint32 *buf1 = (const uint32*)buf;

	WL_TRACE(("wl%d: wlc_bmac_write_template_const_ram\n", wlc_hw->unit));

	ASSERT(ISALIGNED(offset, sizeof(uint32)));
	ASSERT(ISALIGNED(len, sizeof(uint32)));
	ASSERT((offset & ~0xffff) == 0);

	/* The template region starts where the BMC_STARTADDR starts.
	 * This shouldn't use a #defined value but some parameter in a
	 * global struct.
	 */
	if (D11REV_IS(wlc_hw->corerev, 48) || D11REV_IS(wlc_hw->corerev, 49))
		offset += (D11MAC_BMC_STARTADDR_SRASM << 8);
	else if (D11REV_IS(wlc_hw->corerev, 60))
		offset -= wlc_hw->templatebase;
	wlc_bmac_templateptr_wreg(wlc_hw, offset);

	for (i = 0; i < len; i++) {
		word = buf1[i];
		wlc_bmac_templatedata_wreg(wlc_hw, word);
	}
} /* wlc_bmac_write_template_ram */


/** contention window related */
void
wlc_bmac_set_cwmin(wlc_hw_info_t *wlc_hw, uint16 newmin)
{
	wlc_hw->band->CWmin = newmin;

	wlc_bmac_copyto_objmem(wlc_hw, S_DOT11_CWMIN << 2, &newmin,
		sizeof(newmin), OBJADDR_SCR_SEL);
}

void
wlc_bmac_set_cwmax(wlc_hw_info_t *wlc_hw, uint16 newmax)
{
	wlc_hw->band->CWmax = newmax;

	wlc_bmac_copyto_objmem(wlc_hw, S_DOT11_CWMAX << 2, &newmax,
		sizeof(newmax), OBJADDR_SCR_SEL);
}

void
wlc_bmac_bw_set(wlc_hw_info_t *wlc_hw, uint16 bw)
{
	phy_info_t *pi = (phy_info_t *)wlc_hw->band->pi;
	chanspec_t chspec = phy_utils_get_chanspec(pi);

	phy_utils_set_bwstate(pi, bw);

	ASSERT(wlc_hw->clk);
	PANIC_CHECK_CLK(wlc_hw->clk, "wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__);

	if (WLCISACPHY(wlc_hw->band) && (BW_RESET == 1))
	  wlc_bmac_bw_reset(wlc_hw);
	else
	  wlc_bmac_phy_reset(wlc_hw);

	/* No need to issue init for acphy on bw change */
	phy_bwinit(pi, chspec);

	/* restore the clk */
}

static void
wlc_write_hw_bcntemplate0(wlc_hw_info_t *wlc_hw, void *bcn, int len)
{
	d11regs_t *regs = wlc_hw->regs;
	uint shm_bcn_tpl0_base;

	if (D11REV_GE(wlc_hw->corerev, 40))
		shm_bcn_tpl0_base = D11AC_T_BCN0_TPL_BASE;
	else
		shm_bcn_tpl0_base = D11_T_BCN0_TPL_BASE;

	wlc_bmac_write_template_ram(wlc_hw, shm_bcn_tpl0_base, (len + 3) & ~3, bcn);
	/* write beacon length to SCR */
	ASSERT(len < 65536);
	wlc_bmac_write_shm(wlc_hw, M_BCN0_FRM_BYTESZ(wlc_hw), (uint16)len);
	/* mark beacon0 valid */
	OR_REG(wlc_hw->osh, &regs->maccommand, MCMD_BCN0VLD);
}

static void
wlc_write_hw_bcntemplate1(wlc_hw_info_t *wlc_hw, void *bcn, int len)
{
	d11regs_t *regs = wlc_hw->regs;
	uint shm_bcn_tpl1_base;

	if (D11REV_GE(wlc_hw->corerev, 40))
		shm_bcn_tpl1_base = D11AC_T_BCN1_TPL_BASE;
	else
		shm_bcn_tpl1_base = D11_T_BCN1_TPL_BASE;

	wlc_bmac_write_template_ram(wlc_hw, shm_bcn_tpl1_base, (len + 3) & ~3, bcn);
	/* write beacon length to SCR */
	ASSERT(len < 65536);
	wlc_bmac_write_shm(wlc_hw, M_BCN1_FRM_BYTESZ(wlc_hw), (uint16)len);
	/* mark beacon1 valid */
	OR_REG(wlc_hw->osh, &regs->maccommand, MCMD_BCN1VLD);
}

/** mac is assumed to be suspended at this point */
void
wlc_bmac_write_hw_bcntemplates(wlc_hw_info_t *wlc_hw, void *bcn, int len, bool both)
{
	d11regs_t *regs = wlc_hw->regs;

	if (both) {
		wlc_write_hw_bcntemplate0(wlc_hw, bcn, len);
		wlc_write_hw_bcntemplate1(wlc_hw, bcn, len);
	} else {
		/* bcn 0 */
		if (!(R_REG(wlc_hw->osh, &regs->maccommand) & MCMD_BCN0VLD))
			wlc_write_hw_bcntemplate0(wlc_hw, bcn, len);
		/* bcn 1 */
		else if (!(R_REG(wlc_hw->osh, &regs->maccommand) & MCMD_BCN1VLD))
			wlc_write_hw_bcntemplate1(wlc_hw, bcn, len);
		else	/* one template should always have been available */
			ASSERT(0);
	}
}

/** returns the time it takes to power up the synthesizer */
static uint16
WLBANDINITFN(wlc_bmac_synthpu_dly)(wlc_hw_info_t *wlc_hw)
{
	uint16 v;

	/* return SYNTHPU_DLY */


	if (ISSIM_ENAB(wlc_hw->sih)) {
		v = SYNTHPU_DLY_PHY_US_QT;
	} else {

		if (WLCISNPHY(wlc_hw->band)) {
			v = SYNTHPU_DLY_NPHY_US;
		} else if (WLCISHTPHY(wlc_hw->band)) {
			v = SYNTHPU_DLY_HTPHY_US;
		} else if (WLCISLCNPHY(wlc_hw->band)) {
			v = SYNTHPU_DLY_LCNPHY_US;
			if (CHIPID(wlc_hw->sih->chip) == BCM4336_CHIP_ID)
				v = SYNTHPU_DLY_LCNPHY_4336_US;
		} else if (WLCISLCN40PHY(wlc_hw->band)) {
			v = SYNTHPU_DLY_LCN40PHY_US;
		} else if (WLCISLCN20PHY(wlc_hw->band)) {
			v = SYNTHPU_DLY_LCN20PHY_US;
		} else if (WLCISACPHY(wlc_hw->band)) {
			if (BCM4350_CHIP(wlc_hw->sih->chip))
				v = SYNTHPU_DLY_ACPHY2_US;
			else if (CHIPID(wlc_hw->sih->chip) == BCM4335_CHIP_ID &&
				(CHIPREV(wlc_hw->sih->chiprev) < 2))
				v = SYNTHPU_DLY_ACPHY_4335_US;
			else if (CHIPID(wlc_hw->sih->chip) == BCM4335_CHIP_ID &&
				(CHIPREV(wlc_hw->sih->chiprev) >= 2))
				v = SYNTHPU_DLY_ACPHY_4339_US;

			else if (BCM4349_CHIP(wlc_hw->sih->chip) ||
				BCM53573_CHIP(wlc_hw->sih->chip))
				v = SYNTHPU_DLY_ACPHY_4349_US;

			else
				v = SYNTHPU_DLY_ACPHY_US;
		} else {
			v = SYNTHPU_DLY_BPHY_US;
		}
	}

	if ((wlc_hw->band->radioid == BCM2050_ID) && (wlc_hw->band->radiorev == 8)) {
		if (v < 2400)
			v = 2400;
	}

	return v;
} /* wlc_bmac_synthpu_dly */

static void
WLBANDINITFN(wlc_bmac_upd_synthpu)(wlc_hw_info_t *wlc_hw)
{
	uint16 v = wlc_bmac_synthpu_dly(wlc_hw);
	wlc_bmac_write_shm(wlc_hw, M_SYNTHPU_DELAY(wlc_hw), v);
}

void wlc_bmac_update_synthpu(wlc_hw_info_t *wlc_hw)
{
	wlc_bmac_upd_synthpu(wlc_hw);
}

void
wlc_bmac_set_extlna_pwrsave_shmem(wlc_hw_info_t *wlc_hw)
{
	if ((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) &&
	    ((wlc_hw->sih->boardtype == BCM94331X29B) ||
	     ((wlc_hw->boardflags2 & BFL2_EXTLNA_PWRSAVE) &&
	      (wlc_hw->antswctl2g >= 3 && wlc_hw->antswctl5g >= 3)))) {
		wlc_bmac_write_shm(wlc_hw, M_EXTLNA_PWRSAVE(wlc_hw), 0x4c0);
	}
}

/** band-specific init */
static void
WLBANDINITFN(wlc_bmac_bsinit)(wlc_hw_info_t *wlc_hw, chanspec_t chanspec, bool chanswitch_path)
{
	wlc_info_t *wlc = wlc_hw->wlc;

	(void)wlc;

	WL_TRACE(("wl%d: wlc_bmac_bsinit: bandunit %d\n", wlc_hw->unit, wlc_hw->band->bandunit));
	/* we need to do this before phy_init.  5G PA shares the same pin as SECI */
	if (((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
	     (CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID)) &&
	    (wlc_hw->sih->boardtype != BCM94331X19)) {
		si_seci_upd(wlc_hw->sih, CHSPEC_IS2G(chanspec));
	}
	/* sanity check */
	if (PHY_TYPE(R_REG(wlc_hw->osh, &wlc_hw->regs->phyversion)) != PHY_TYPE_LCNXN)
		ASSERT((uint)PHY_TYPE(R_REG(wlc_hw->osh, &wlc_hw->regs->phyversion)) ==
		       wlc_hw->band->phytype);

	wlc_ucode_bsinit(wlc_hw);

	/* phymode switch requires phyinit */
	phy_bsinit((phy_info_t *)wlc_hw->band->pi, chanspec,
		(!chanswitch_path) || phy_init_pending((phy_info_t *)wlc_hw->band->pi));

#if defined(BCMNODOWN)
	/* Radio is active after phy_init() */
	wlc->pub->radio_active = ON;
#endif

	wlc_ucode_txant_set(wlc_hw);

	/* cwmin is band-specific, update hardware with value for current band */
	wlc_bmac_set_cwmin(wlc_hw, wlc_hw->band->CWmin);
	wlc_bmac_set_cwmax(wlc_hw, wlc_hw->band->CWmax);

	wlc_bmac_update_slot_timing(wlc_hw,
		BAND_5G(wlc_hw->band->bandtype) ? TRUE : wlc_hw->shortslot);

	/* write phytype and phyvers */
	wlc_bmac_write_shm(wlc_hw, M_PHYTYPE(wlc_hw), (uint16)wlc_hw->band->phytype);
	wlc_bmac_write_shm(wlc_hw, M_PHYVER(wlc_hw), (uint16)wlc_hw->band->phyrev);

#ifdef WL11N
	/* initialize the txphyctl1 rate table since shmem is shared between bands */
	wlc_upd_ofdm_pctl1_table(wlc_hw);
#endif

	wlc_bmac_upd_synthpu(wlc_hw);

	/* Configure BTC GPIOs as bands change */
	if (BAND_5G(wlc_hw->band->bandtype))
		wlc_bmac_mhf(wlc_hw, MHF5, MHF5_BTCX_DEFANT, MHF5_BTCX_DEFANT, WLC_BAND_ALL);
	else
		wlc_bmac_mhf(wlc_hw, MHF5, MHF5_BTCX_DEFANT, 0, WLC_BAND_ALL);

	wlc_bmac_btc_gpio_enable(wlc_hw);


	if (BAND_5G(wlc_hw->band->bandtype))
		wlc_bmac_config_4331_5GePA(wlc_hw);

	wlc_bmac_set_extlna_pwrsave_shmem(wlc_hw);
} /* wlc_bmac_bsinit */

/**
 * Helper API to apply the phy reset on d11 core unit one for RSDB chip.
 * This is required while there is no core one init happens for cases like MIMO,
 * 80p80 mode.
 */
static void
wlc_bmac_4349_btcx_prisel_war(wlc_hw_info_t *wlc_hw)
{
	int sicoreunit = 0;

	ASSERT(wlc_hw != NULL);
	sicoreunit = si_coreunit(wlc_hw->sih);

	/* Apply the setting to D11 core unit one always */
	if ((wlc_rsdb_mode(wlc_hw->wlc) == PHYMODE_MIMO) ||
	(wlc_rsdb_mode(wlc_hw->wlc) == PHYMODE_80P80)) {
		WL_INFORM(("MIMO: PRISEL ISSUE WORKAROUND\n"));
		si_d11_switch_addrbase(wlc_hw->sih, 1);
		si_core_cflags(wlc_hw->sih,
			(SICF_MCLKE | SICF_FCLKON | SICF_PCLKE | SICF_PRST | SICF_MPCLKE),
			(SICF_MCLKE | SICF_FCLKON | SICF_PCLKE | SICF_PRST));
	}

	si_d11_switch_addrbase(wlc_hw->sih, sicoreunit);
}

void
wlc_bmac_core_phy_clk(wlc_hw_info_t *wlc_hw, bool clk)
{
	WL_TRACE(("wl%d: wlc_bmac_core_phy_clk: clk %d\n", wlc_hw->unit, clk));

	wlc_hw->phyclk = clk;

	if (OFF == clk) {
		/* CLEAR GMODE BIT, PUT PHY INTO RESET */

		si_core_cflags(wlc_hw->sih,
			(SICF_PRST | SICF_FGC | SICF_GMODE),
			(SICF_PRST | SICF_FGC));
		OSL_DELAY(1);

		si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_FGC), SICF_PRST);
		OSL_DELAY(1);
	} else {
		/* TAKE PHY OUT OF RESET */

		/* High Speed DAC Configuration */
		if (D11REV_GE(wlc_hw->corerev, 40)) {
			si_core_cflags(wlc_hw->sih, SICF_DAC, 0x100);
		}
		/* Special PHY RESET Sequence for ACPHY to ensure correct Clock Alignment */
		if (WLCISACPHY(wlc_hw->band)) {
			if ((BCM4349_CHIP(wlc_hw->sih->chip) || BCM53573_CHIP(wlc_hw->sih->chip)) &&
#ifdef WLRSDB
				WLC_RSDB_SINGLE_MAC_MODE(WLC_RSDB_CURR_MODE(wlc_hw->wlc)) &&
#endif
				wlc_hw->macunit == 0) {

				si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_FGC | SICF_PCLKE), 0);
				OSL_DELAY(1);

				si_pmu_chipcontrol(wlc_hw->sih, 5,
						CC5_4349_MAC_PHY_CLK_8_DIV,
						CC5_4349_MAC_PHY_CLK_8_DIV);

				OSL_DELAY(1);

				if (wlc_bmac_rsdb_cap(wlc_hw)) {
					if (!RSDB_ENAB(wlc_hw->wlc->pub) &&
					(wlc_hw->core1_mimo_reset == 0)) {
						wlc_bmac_4349_btcx_prisel_war(wlc_hw);
						wlc_hw->core1_mimo_reset = 1;
					}
				}
				si_core_cflags(wlc_hw->sih, SICF_PCLKE, SICF_PCLKE);
				OSL_DELAY(1);

				si_pmu_chipcontrol(wlc_hw->sih, 5,
						CC5_4349_MAC_PHY_CLK_8_DIV, 0);
				OSL_DELAY(1);

			} else {
				/* PHY Reset Sequence is changed for 43012 and
				* it should be 1 - 7 -5 in Reg 408
				*/
				if (D11REV_IS(wlc_hw->corerev, 60)) {
					/* This writes value 7 to register 408 */
					si_core_cflags(wlc_hw->sih,
					(SICF_PRST | SICF_PCLKE | SICF_FGC | SICF_CLOCK_EN),
					SICF_PCLKE | SICF_FGC | SICF_CLOCK_EN);
				}
				/* turn off phy clocks and bring out of reset */
				si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_FGC | SICF_PCLKE), 0);
				OSL_DELAY(1);

				/*
				 * CRWLDOT11M-1403: Enabling Core 1 PHY reset bit
				 * in Core 1 ioctrl for dot11macphy_prisel to PHY
				 */
				if (wlc_bmac_rsdb_cap(wlc_hw)) {
					if (!RSDB_ENAB(wlc_hw->wlc->pub) &&
					(wlc_hw->core1_mimo_reset == 0)) {
						wlc_bmac_4349_btcx_prisel_war(wlc_hw);
						wlc_hw->core1_mimo_reset = 1;
					}
				}

				/* reenable phy clocks to resync to mac mac clock */
				si_core_cflags(wlc_hw->sih, SICF_PCLKE, SICF_PCLKE);
				OSL_DELAY(1);
			}
		} else {
			/* turn off phy clocks */
			si_core_cflags(wlc_hw->sih,
				(SICF_PRST | SICF_FGC | SICF_PCLKE),
				SICF_FGC);
			OSL_DELAY(1);

			/* reenable phy clocks to resync to mac mac clock */
			si_core_cflags(wlc_hw->sih, (SICF_FGC | SICF_PCLKE), SICF_PCLKE);
			OSL_DELAY(1);
		}
	}
} /* wlc_bmac_core_phy_clk */

/** Perform a soft reset of the PHY PLL */
void
wlc_bmac_core_phypll_reset(wlc_hw_info_t *wlc_hw)
{
	WL_TRACE(("wl%d: wlc_bmac_core_phypll_reset\n", wlc_hw->unit));

	if (WLCISNPHY(wlc_hw->band) || WLCISHTPHY(wlc_hw->band)) {

		si_corereg(wlc_hw->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol_addr), ~0, 0);
		OSL_DELAY(1);
		si_corereg(wlc_hw->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol_data), 0x4, 0);
		OSL_DELAY(1);
		si_corereg(wlc_hw->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol_data), 0x4, 4);
		OSL_DELAY(1);
		si_corereg(wlc_hw->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol_data), 0x4, 0);
		OSL_DELAY(1);
	}
}

/**
 * light way to turn on phy clock without reset for NPHY and HTPHY only
 *  refer to wlc_bmac_core_phy_clk for full version
 */
void
wlc_bmac_phyclk_fgc(wlc_hw_info_t *wlc_hw, bool clk)
{
	/* support(necessary for NPHY and HTPHY) only */
	if (!WLCISNPHY(wlc_hw->band) && !WLCISHTPHY(wlc_hw->band) && !WLCISACPHY(wlc_hw->band))
		return;

	if (ON == clk)
		si_core_cflags(wlc_hw->sih, SICF_FGC, SICF_FGC);
	else
		si_core_cflags(wlc_hw->sih, SICF_FGC, 0);

}

void
wlc_bmac_macphyclk_set(wlc_hw_info_t *wlc_hw, bool clk)
{
	if (ON == clk)
		si_core_cflags(wlc_hw->sih, SICF_MPCLKE, SICF_MPCLKE);
	else
		si_core_cflags(wlc_hw->sih, SICF_MPCLKE, 0);
}

static uint32
wlc_bmac_clk_bwbits(wlc_hw_info_t *wlc_hw)
{
	uint32 phy_bw_clkbits = 0;

	/* select the phy speed according to selected channel b/w */
	switch (CHSPEC_BW(wlc_hw->chanspec)) {
#ifdef WL11ULB
	case WL_CHANSPEC_BW_2P5:
	case WL_CHANSPEC_BW_5:
	case WL_CHANSPEC_BW_10:
#else /* WL11ULB */
	case WL_CHANSPEC_BW_10:
		phy_bw_clkbits = SICF_BW10;
		break;
#endif /* WL11ULB */
	case WL_CHANSPEC_BW_20:
		phy_bw_clkbits = SICF_BW20;
		break;
	case WL_CHANSPEC_BW_40:
		phy_bw_clkbits = SICF_BW40;
		break;
	case WL_CHANSPEC_BW_80:
	case WL_CHANSPEC_BW_8080:
	case WL_CHANSPEC_BW_160:
		phy_bw_clkbits = SICF_BW80;
		break;
	default:
		ASSERT(0);	/* should never get here */
	}

	return phy_bw_clkbits;
}

void
wlc_bmac_phy_reset(wlc_hw_info_t *wlc_hw)
{
	uint32 phy_bw_clkbits;

	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));

	ASSERT(wlc_hw->band != NULL);

	phy_bw_clkbits = wlc_bmac_clk_bwbits(wlc_hw);

	if (WLCISNPHY(wlc_hw->band) && NREV_IS(wlc_hw->band->phyrev, 18)) {
		if (si_read_pmu_autopll(wlc_hw->sih))
		{
			if (phy_bw_clkbits != SICF_BW40) {
				/* Set the PHY bandwidth */
				si_core_cflags(wlc_hw->sih, SICF_BWMASK, SICF_BW40);
			}
			/* Turn on Auto reset for PLL phy clock */
			si_pmu_chipcontrol(wlc_hw->sih, PMU1_PLL0_CHIPCTL0, 2, 0);
		}
	}

	if (WLCISNPHY(wlc_hw->band)) {
		/* Set the PHY bandwidth */
		si_core_cflags(wlc_hw->sih, SICF_BWMASK, phy_bw_clkbits);

		OSL_DELAY(1);

		/* Perform a soft reset of the PHY PLL */
		wlc_bmac_core_phypll_reset(wlc_hw);

		/* reset the PHY */
		si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_PCLKE),
			(SICF_PRST | SICF_PCLKE));
	} else if (WLCISACPHY(wlc_hw->band)) {

		si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_PCLKE | SICF_BWMASK| SICF_FGC),
		               (SICF_PRST | SICF_PCLKE | phy_bw_clkbits| SICF_FGC));
	} else {

		si_core_cflags(wlc_hw->sih, (SICF_PRST | SICF_PCLKE | SICF_BWMASK),
			(SICF_PRST | SICF_PCLKE | phy_bw_clkbits));
	}

	OSL_DELAY(2);
	wlc_bmac_core_phy_clk(wlc_hw, ON);

	/* Must ensure that BW setting in pi->bw and SiCoreFlags are in Sync. There is a chance
	 * for such a sync loss on wlc_up() path. Currently in this function wlc_hw->chanspec is
	 * used to configure BW in sicoreflags and homechanspec gets reset to different value in
	 * wlc_init(). This can lead to a possible sync loss in BW, hence setting pi->bw here to
	 * avoid such issues.
	 */
	phy_utils_set_bwstate((phy_info_t *)wlc_hw->band->pi, CHSPEC_BW(wlc_hw->chanspec));

	if (wlc_hw->band->pi != NULL)
		phy_ana_reset((phy_info_t *)wlc_hw->band->pi);
} /* wlc_bmac_phy_reset */

void
wlc_bmac_bw_reset(wlc_hw_info_t *wlc_hw)
{
	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));

	si_core_cflags(wlc_hw->sih, SICF_BWMASK, wlc_bmac_clk_bwbits(wlc_hw));
}

/** switch to and initialize d11 + PHY for operation on caller supplied band */
static void
WLBANDINITFN(wlc_bmac_setband)(wlc_hw_info_t *wlc_hw, uint bandunit, chanspec_t chanspec)
{
	wlc_info_t *wlc = wlc_hw->wlc;
	uint32 macintmask;

	ASSERT(NBANDS_HW(wlc_hw) > 1);
	ASSERT(bandunit != wlc_hw->band->bandunit);

	WL_TSLOG(wlc_hw->wlc, __FUNCTION__, TS_ENTER, 0);

	/* Enable the d11 core before accessing it */
	if (!si_iscoreup(wlc_hw->sih)) {
		wlc_bmac_core_reset(wlc_hw, 0, 0);
		ASSERT(si_iscoreup(wlc_hw->sih));
		wlc_mctrl_reset(wlc_hw);
	}

	macintmask = wlc_setband_inact(wlc_hw, bandunit);

	if (!wlc_hw->up) {
		WL_TSLOG(wlc_hw->wlc, __FUNCTION__, TS_EXIT, 0);
		return;
	}

	if (!(WLCISACPHY(wlc_hw->band)))
		wlc_bmac_core_phy_clk(wlc_hw, ON);

	/* band-specific initializations */
	wlc_bmac_bsinit(wlc_hw, chanspec, TRUE);

	/*
	 * If there are any pending software interrupt bits,
	 * then replace these with a harmless nonzero value
	 * so wlc_dpc() will re-enable interrupts when done.
	 */
	if (wlc_hw->macintstatus)
		wlc_hw->macintstatus = MI_DMAINT;

	/* restore macintmask */
	wl_intrsrestore(wlc->wl, macintmask);

	/* ucode should still be suspended.. */
	ASSERT((R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) & MCTL_EN_MAC) == 0);
	WL_TSLOG(wlc_hw->wlc, __FUNCTION__, TS_EXIT, 0);
} /* wlc_bmac_setband */

/** low-level band switch utility routine */
static void
WLBANDINITFN(wlc_setxband)(wlc_hw_info_t *wlc_hw, uint bandunit)
{
	WL_TRACE(("wl%d: wlc_setxband: bandunit %d\n", wlc_hw->unit, bandunit));

	wlc_hw->band = wlc_hw->bandstate[bandunit];

	/* Update the wlc->band pointer for monolithic driver */
	wlc_pi_band_update(wlc_hw->wlc, bandunit);

	/* set gmode core flag */
	if (wlc_hw->sbclk && !wlc_hw->noreset) {
		si_core_cflags(wlc_hw->sih, SICF_GMODE, ((bandunit == 0) ? SICF_GMODE : 0));
	}
}

static bool
BCMATTACHFN(wlc_isgoodchip)(wlc_hw_info_t *wlc_hw)
{
	/* reject unsupported corerev */
	if (!VALID_COREREV((int)wlc_hw->corerev)) {
		WL_ERROR(("unsupported core rev %d\n", wlc_hw->corerev));
		return FALSE;
	}

	return TRUE;
}

static bool
BCMATTACHFN(wlc_validboardtype)(wlc_hw_info_t *wlc_hw)
{
	bool goodboard = TRUE;
	uint boardtype = wlc_hw->sih->boardtype;
	uint boardrev = wlc_hw->boardrev;

	if (boardrev == 0)
		goodboard = FALSE;
	else if (boardrev > 0xff) {
		uint brt = (boardrev & 0xf000) >> 12;
		uint b0 = (boardrev & 0xf00) >> 8;
		uint b1 = (boardrev & 0xf0) >> 4;
		uint b2 = boardrev & 0xf;

		if ((brt > 2) || (brt == 0) || (b0 > 9) || (b0 == 0) || (b1 > 9) || (b2 > 9))
			goodboard = FALSE;
	}

	if (wlc_hw->sih->boardvendor != VENDOR_BROADCOM)
		return goodboard;

	if ((boardtype == BCM94306MP_BOARD) || (boardtype == BCM94306CB_BOARD)) {
		if (boardrev < 0x40)
			goodboard = FALSE;
	} else if (boardtype == BCM94309MP_BOARD) {
		goodboard = FALSE;
	} else if (boardtype == BCM94309G_BOARD) {
		if (boardrev < 0x51)
			goodboard = FALSE;
	}
	return goodboard;
}

static char *
BCMATTACHFN(wlc_get_macaddr)(wlc_hw_info_t *wlc_hw, uint unit)
{
	const char *varname = rstr_macaddr;
	char ifvarname[16];
	char *macaddr;

	/* Check nvram for the presence of an interface specific macaddr.
	 * Interface specific mac addresses should be of the form wlx_macaddr, where x is the
	 * unit number of the interface
	 */
	snprintf(ifvarname, sizeof(ifvarname), "wl%d_%s", unit, rstr_macaddr);
	if ((macaddr = getvar(wlc_hw->vars, ifvarname)) != NULL)
		return macaddr;

	/* Fallback to get the default macaddr.
	 * If macaddr exists, use it (Sromrev4, CIS, ...).
	 */
	if ((macaddr = getvar(wlc_hw->vars, varname)) != NULL)
		return macaddr;

#ifndef BCMSMALL
	/*
	 * Take care of our legacy: MAC addresses can not change
	 * during sw upgrades!
	 * 4309B0 dualband:  il0macaddr
	 * other  dualband:  et1macaddr
	 * uniband-A cards:  et1macaddr
	 * else:             il0macaddr
	 */
	if (NBANDS_HW(wlc_hw) > 1)
		varname = rstr_et1macaddr;
	else
		varname = rstr_il0macaddr;

	if ((macaddr = getvar(wlc_hw->vars, varname)) == NULL) {
		WL_ERROR(("wl%d: %s: macaddr getvar(%s) not found\n",
			wlc_hw->unit, __FUNCTION__, varname));
	}
#endif /* !BCMSMALL */

	return macaddr;
}

/**
 * Return TRUE if radio is disabled, otherwise FALSE.
 * hw radio disable signal is an external pin, users activate it asynchronously
 * this function could be called when driver is down and w/o clock
 * it operates on different registers depending on corerev and boardflag.
 */
bool
wlc_bmac_radio_read_hwdisabled(wlc_hw_info_t* wlc_hw)
{
	bool v, clk, xtal;
	uint32 resetbits = 0, flags = 0;

	xtal = wlc_hw->sbclk;
	if (!xtal)
		wlc_bmac_xtal(wlc_hw, ON);

	/* may need to take core out of reset first */
	clk = wlc_hw->clk;
	if (!clk) {

		flags |= SICF_PCLKE;

		/* AI chip doesn't restore bar0win2 on hibernation/resume, need sw fixup */
		if ((CHIPID(wlc_hw->sih->chip) == BCM43224_CHIP_ID) ||
		    (CHIPID(wlc_hw->sih->chip) == BCM43225_CHIP_ID) ||
		    (CHIPID(wlc_hw->sih->chip) == BCM43421_CHIP_ID)) {
			wlc_hw->regs = (d11regs_t *)si_setcore(
					wlc_hw->sih, D11_CORE_ID, wlc_hw->macunit);
			ASSERT(wlc_hw->regs != NULL);
		}
		wlc_bmac_core_reset(wlc_hw, flags, resetbits);
		wlc_mctrl_reset(wlc_hw);
	}

	v = ((R_REG(wlc_hw->osh, &wlc_hw->regs->phydebug) & PDBG_RFD) != 0);

	/* put core back into reset */
	if (!clk && v) {
		wlc_bmac_core_disable(wlc_hw, 0);
	}

	if (!xtal)
		wlc_bmac_xtal(wlc_hw, OFF);

	return (v);
}

int
wlc_bmac_4360_pcie2_war(wlc_hw_info_t* wlc_hw, uint32 vcofreq)
{
	extern int do_4360_pcie2_war;
	uint32 xtalfreqi;
	uint32 p1div;
	uint32 xtalfreq1;
	uint32 ndiv_int;
	uint32 is_frac;
	uint32 ndiv_mode;
	uint32 val;
	uint32 data;
	int linkspeed;
	int ret = BCME_OK;

	if (((CHIPID(wlc_hw->sih->chip) != BCM4360_CHIP_ID) &&
	     (CHIPID(wlc_hw->sih->chip) != BCM43460_CHIP_ID) &&
	     (CHIPID(wlc_hw->sih->chip) != BCM4352_CHIP_ID)) ||
	    (CHIPREV(wlc_hw->sih->chiprev) > 2) ||
	    (BUSTYPE(wlc_hw->sih->bustype) != PCI_BUS))
		return ret;

#if !defined(__mips__) && !defined(BCM47XX_CA9)
	if (wl_osl_pcie_rc(wlc_hw->wlc->wl, 0, 0) == 1)	/* pcie gen 1 */
		return ret;
#endif /* !defined(__mips__) && !defined(BCM47XX_CA9) */

	if (do_4360_pcie2_war != 0)
		return ret;

	do_4360_pcie2_war = 1;

	si_corereg(wlc_hw->sih, 3, 0x120, ~0, 0xBC);
	data = si_corereg(wlc_hw->sih, 3, 0x124, 0, 0);
	linkspeed = (data >> 16) & 0xf;

	/* don't need the WAR if linkspeed is already gen2 */
	if (linkspeed == 2)
		return ret;

	/* Save PCI cfg space. (cfg offsets 0x0 - 0x3f) */
	ret = si_pcie_configspace_cache((si_t *)(uintptr)(wlc_hw->sih));
	if (ret != BCME_OK) {
		WL_ERROR(("wl%d: %s: Unable to save PCIe Configuration "
			"space\n", wlc_hw->unit, __FUNCTION__));
		return ret;
	}

	xtalfreqi = 40;
	p1div = 2;
	xtalfreq1 = xtalfreqi / p1div;
	ndiv_int = vcofreq / xtalfreq1;
	is_frac = (vcofreq % xtalfreq1) > 0 ? 1 : 0;
	ndiv_mode = is_frac ? 3 : 0;
	val = (ndiv_int << 7) | (ndiv_mode << 4) | (p1div << 0);

	si_pmu_pllcontrol(wlc_hw->sih, 10, ~0, val);

	if (is_frac) {
		uint32 frac = (vcofreq % xtalfreq1) * (1 << 24) / xtalfreq1;
		si_pmu_pllcontrol(wlc_hw->sih, 11, ~0, frac);
	}

	/* update pll */
	si_pmu_pllupd(wlc_hw->sih);

	/* Issuing Watchdog Reset */
	si_watchdog(wlc_hw->sih, 2);
	OSL_DELAY(2000);

	/* hot reset */
#if !defined(__mips__) && !defined(BCM47XX_CA9)
	wl_osl_pcie_rc(wlc_hw->wlc->wl, 1, 0);
#endif /* !defined(__mips__) && !defined(BCM47XX_CA9) */
	OSL_DELAY(50 * 1000);

	ret = si_pcie_configspace_restore((si_t *)(uintptr)(wlc_hw->sih));
	if (ret != BCME_OK) {
		WL_ERROR(("wl%d: %s: Unable to Restore PCIe Configuration "
			"space\n", wlc_hw->unit, __FUNCTION__));
		return ret;
	}

	/* set pcie gen2 capability */
	si_corereg(wlc_hw->sih, 3, 0x120, ~0, 0x4DC);
	data = si_corereg(wlc_hw->sih, 3, 0x124, 0, 0);

	si_corereg(wlc_hw->sih, 3, 0x120, ~0, 0x4DC);
	si_corereg(wlc_hw->sih, 3, 0x124, ~0, (data & 0xfffffff0) | 2);

	si_corereg(wlc_hw->sih, 3, 0x120, ~0, 0x1800);
	data = si_corereg(wlc_hw->sih, 3, 0x124, 0, 0);

	si_corereg(wlc_hw->sih, 3, 0x120, ~0, 0x1800);
	si_corereg(wlc_hw->sih, 3, 0x124, ~0, (data & 0xfffffff0) | 2);

	si_corereg(wlc_hw->sih, 3, 0x120, ~0, 0x1800);
	si_corereg(wlc_hw->sih, 3, 0x124, ~0, data & 0xfffffff0);

	OSL_DELAY(1000);

	si_corereg(wlc_hw->sih, 3, 0x120, ~0, 0xBC);
	data = si_corereg(wlc_hw->sih, 3, 0x124, 0, 0);
	linkspeed = (data >> 16) & 0xf;

	WL_INFORM(("wl%d: pcie gen2 link speed: %d\n", wlc_hw->unit, linkspeed));

	return ret;
} /* wlc_bmac_4360_pcie2_war */

/** Initialize just the hardware when coming out of POR or S3/S5 system states */
void
BCMINITFN(wlc_bmac_hw_up)(wlc_hw_info_t *wlc_hw)
{
	if (wlc_hw->wlc->pub->hw_up)
		return;

	WL_TRACE(("wl%d: %s:\n", wlc_hw->unit, __FUNCTION__));

	/* check if need to reinit pll */
	si_pll_sr_reinit(wlc_hw->sih);

	if (CHIPID(wlc_hw->sih->chip) == BCM43142_CHIP_ID)
		si_pmu_res_init(wlc_hw->sih, wlc_hw->osh);

	if ((BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) &&
	    (D11REV_GE(wlc_hw->corerev, 40))) {
		si_pmu_res_init(wlc_hw->sih, wlc_hw->osh);
	}

	/* config GPIO1 as HW radio on/off pin for 43162 */
	if ((BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) &&
		(CHIPID(wlc_hw->sih->chip) == BCM4335_CHIP_ID))
		si_gci_set_functionsel(wlc_hw->sih, CC4335_PIN_GPIO_01, CC4335_FNSEL_MISC0);

	/**
	 * JIRA: SWWLAN-27305 shut the bbpll off in sleep as well as improve the efficiency of
	 * some internal regulator.
	 */
	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS &&
	    (BCM4350_CHIP(wlc_hw->sih->chip) || BCM43602_CHIP(wlc_hw->sih->chip))) {
		si_pmu_chip_init(wlc_hw->sih, wlc_hw->osh);
		si_pmu_slow_clk_reinit(wlc_hw->sih, wlc_hw->osh);
	}

	/*
	 * Enable pll and xtal, initialize the power control registers,
	 * and force fastclock for the remainder of wlc_up().
	 */
	wlc_bmac_xtal(wlc_hw, ON);
	si_clkctl_init(wlc_hw->sih);
	wlc_clkctl_clk(wlc_hw, CLK_FAST);

	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) {
		si_pcie_hw_LTR_war(wlc_hw->sih);
		si_pciedev_crwlpciegen2(wlc_hw->sih);
		si_pciedev_reg_pm_clk_period(wlc_hw->sih);
	}
#ifndef WL_LTR
	si_pcie_ltr_war(wlc_hw->sih);
#endif

	/* Init BTC related GPIOs to clean state on power up as well. This must
	 * be done here as even if radio is disabled, driver needs to
	 * make sure that output GPIO is lowered
	 */
	wlc_bmac_btc_gpio_disable(wlc_hw);

	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) {
		/* HW up(initial load, post hibernation resume), core init/fixup */

		if ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
		    (CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
		    (CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID)) {
			/* changing the avb vcoFreq as 510M (from default: 500M) */
			/* Tl clk 127.5Mhz */
				WL_INFORM(("wl%d: %s: settng clock to %d\n",
				wlc_hw->unit, __FUNCTION__,	wlc_hw->vcoFreq_4360_pcie2_war));

				if (wlc_bmac_4360_pcie2_war(wlc_hw,
					wlc_hw->vcoFreq_4360_pcie2_war) != BCME_OK) {
					ASSERT(0);
				}
			}
		si_pci_fixcfg(wlc_hw->sih);

		/* AI chip doesn't restore bar0win2 on hibernation/resume, need sw fixup */
		if ((CHIPID(wlc_hw->sih->chip) == BCM43224_CHIP_ID) ||
		    (CHIPID(wlc_hw->sih->chip) == BCM43225_CHIP_ID) ||
		    (CHIPID(wlc_hw->sih->chip) == BCM43421_CHIP_ID)) {
			wlc_hw->regs = (d11regs_t *)si_setcore(
					wlc_hw->sih, D11_CORE_ID, wlc_hw->macunit);
			ASSERT(wlc_hw->regs != NULL);
		}

		if (CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID) {
			si_clk_pmu_htavail_set(wlc_hw->sih, FALSE);

			si_pmu_synth_pwrsw_4313_war(wlc_hw->sih);
		}
	}

#ifdef WLLED
	wlc_bmac_led_hw_init(wlc_hw);
#endif

#ifdef DMATXRC
	if (DMATXRC_ENAB(wlc_hw->wlc->pub) && PHDR_ENAB(wlc_hw->wlc))
		wlc_phdr_fill(wlc_hw->wlc);
#endif

	/* Inform phy that a POR reset has occurred so it does a complete phy init */
	wlc_phy_por_inform(wlc_hw->band->pi);

	wlc_hw->ucode_loaded = FALSE;
	wlc_hw->wlc->pub->hw_up = TRUE;
	/* 4313 EPA fix */
	if ((wlc_hw->boardflags & BFL_FEM) && (CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID)) {
		if (!(wlc_hw->boardrev >= 0x1250 && (wlc_hw->boardflags & BFL_FEM_BT)))
			si_epa_4313war(wlc_hw->sih);
		else
			si_btcombo_p250_4313_war(wlc_hw->sih);
	}
	if (((CHIPID(wlc_hw->sih->chip) == BCM43228_CHIP_ID)) &&
		(wlc_hw->boardflags & BFL_FEM_BT)) {
		si_btcombo_43228_war(wlc_hw->sih);
		si_pmu_chipcontrol(wlc_hw->sih, PMU1_PLL0_CHIPCTL1, 0x20, 0x20);
	}
} /* wlc_bmac_hw_up */

static bool
wlc_dma_rxreset(wlc_hw_info_t *wlc_hw, uint fifo)
{
	hnddma_t *di = wlc_hw->di[fifo];

	return (dma_rxreset(di));
}

/**
 * d11 core reset
 *   ensure fask clock during reset
 *   reset dma
 *   reset d11(out of reset)
 *   reset phy(out of reset)
 *   clear software macintstatus for fresh new start
 * one testing hack wlc_hw->noreset will bypass the d11/phy reset
 */
void
BCMINITFN(wlc_bmac_corereset)(wlc_hw_info_t *wlc_hw, uint32 flags)
{
	uint i;
	bool fastclk;
	uint32 resetbits = 0;

	if (flags == WLC_USE_COREFLAGS)
		flags = (wlc_hw->band->pi ? wlc_hw->band->core_flags : 0);

	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));

	/* request FAST clock if not on  */
	if (!(fastclk = wlc_hw->forcefastclk))
		wlc_clkctl_clk(wlc_hw, CLK_FAST);

	/* reset the dma engines except if core is in reset (first time thru or bigger hammer) */
	if (si_iscoreup(wlc_hw->sih)) {
		if (!PIO_ENAB_HW(wlc_hw)) {
			for (i = 0; i < NFIFO; i++) {
				if (wlc_hw->di[i]) {
					if (!wlc_bmac_txfifo_reset(wlc_hw, i)) {
						WL_ERROR(("wl%d: %s: bmac_dma_txreset[%d]:"
								" cannot stop dma\n",
								wlc_hw->unit, __FUNCTION__, i));
						WL_HEALTH_LOG(wlc_hw->wlc, DMATX_ERROR);
					}
					wlc_upd_suspended_fifos_clear(wlc_hw, i);
				}
			}
			for (i = 0; i < MAX_RX_FIFO; i++) {
				if ((wlc_hw->di[i] != NULL) && wlc_bmac_rxfifo_enab(i)) {
					if (!wlc_dma_rxreset(wlc_hw, i)) {
						WL_ERROR(("wl%d: %s: dma_rxreset[%d]:"
							" cannot stop dma\n",
							wlc_hw->unit, __FUNCTION__, i));
						WL_HEALTH_LOG(wlc_hw->wlc, DMARX_ERROR);
					}
				}
			}
		} else {
			for (i = 0; i < NFIFO; i++)
				if (wlc_hw->pio[i])
					wlc_pio_reset(wlc_hw->pio[i]);
		}
	}
	/* if noreset, just stop the psm and return */
	if (wlc_hw->noreset) {
		wlc_hw->macintstatus = 0;	/* skip wl_dpc after down */
		wlc_bmac_mctrl(wlc_hw, MCTL_PSM_RUN | MCTL_EN_MAC, 0);
		return;
	}

	/*
	 * corerev >= 18, mac no longer enables phyclk automatically when driver accesses phyreg
	 * throughput mac, AND phy_reset is skipped at early stage when band->pi is invalid
	 * need to enable PHY CLK
	 */
	flags |= SICF_PCLKE;
#if defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM)
	if (D11REV_GE(wlc_hw->corerev, 60)) {
		/* if sw patch control values are zero, save patch control values from hw
		 * before reset. They will be restored unconditionally after reset.
		 */
		if (wlc_hw->pc_mask == 0)
			wlc_hw->pc_flags = R_REG(wlc_hw->osh, &wlc_hw->regs->psm_patchcopy_ctrl);
			wlc_hw->pc_mask = wlc_hw->pc_flags;

		WL_TRACE(("saving patchCtrl: flags:%x, mask:%x\n", wlc_hw->pc_flags,
			wlc_hw->pc_mask));
	}
#endif /* defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM) */

	/* reset the core
	 * In chips with PMU, the fastclk request goes through d11 core reg 0x1e0, which
	 *  is cleared by the core_reset. have to re-request it.
	 *  This adds some delay and we can optimize it by also requesting fastclk through
	 *  chipcommon during this period if necessary. But that has to work coordinate
	 *  with other driver like mips/arm since they may touch chipcommon as well.
	 *  RSDB chips handle core reset programming of both cores from core 0
	 *  context only.
	 *  RSDB chip does D11 core reset only in Core 0 context.
	 *  In case of SISO 1 mode core reset sequence is required in CORE 1
	 *  context as well.
	 *
	 */
	wlc_hw->clk = FALSE;
	wlc_bmac_core_reset(wlc_hw, flags, resetbits);
	wlc_hw->clk = TRUE;

#if defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM)
	if (D11REV_GE(wlc_hw->corerev, 60)) {
		WL_TRACE(("enabling patchCtrl: after reset!\n"));
		hndd11_write_reg32(wlc_hw->osh, &wlc_hw->regs->psm_patchcopy_ctrl,
			wlc_hw->pc_mask,
			wlc_hw->pc_flags);
	}
#endif /* defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM) */

	if (D11REV_IS(wlc_hw->corerev, 33)) {
		/* CRLCNPHY-668: WAR for phy reg access hang in 4334/4314/43142 chips.
		 * A restore pulse to the phy unwedges the reg access
		 */
		wlc_bmac_write_ihr(wlc_hw, PHY_CTRL, PHY_CTRL_RESTORESTART | PHY_CTRL_MC);
		wlc_bmac_write_ihr(wlc_hw, PHY_CTRL, PHY_CTRL_MC);
	}

	/* PHY Mode has to be written only in Core 0 cflags.
	 * For Core 1 override, switch to core-0 and write it.
	 */
#ifdef WLRSDB
	if (wlc_bmac_rsdb_cap(wlc_hw)) {
		if (wlc_rsdb_is_other_chain_idle(wlc_hw->wlc) == TRUE)
			wlc_rsdb_set_phymode(wlc_hw->wlc, (wlc_rsdb_mode(wlc_hw->wlc)));
	}
#endif /* WLRSDB */
	/*
	 * If band->phytype & band->phyrev are not yet known, get them from the d11 registers.
	 * The phytype and phyrev are used in WLCISXXX() and XXXREV_XX() macros.
	 */
	ASSERT(wlc_hw->regs != NULL);
	if (wlc_hw->band && wlc_hw->band->phytype == 0 && wlc_hw->band->phyrev == 0) {
		uint16 phyversion = R_REG(wlc_hw->osh, &wlc_hw->regs->phyversion);

		wlc_hw->band->phytype = PHY_TYPE(phyversion);
		wlc_hw->band->phyrev = phyversion & PV_PV_MASK;
	}

	if (wlc_hw->band && wlc_hw->band->pi)
		wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, TRUE);

	if (wlc_hw->band && WLCISACPHY(wlc_hw->band)) {
		/* set up highspeed DAC mode to 1 by default
		 * (see default value 0 is undefined mode)
		 */
		si_core_cflags(wlc_hw->sih, SICF_DAC, 0x100);

		/* turn off phy clocks */
		si_core_cflags(wlc_hw->sih, (SICF_FGC | SICF_PCLKE), 0);

		/* re-enable phy clocks to resync to macphy clock */
		si_core_cflags(wlc_hw->sih, SICF_PCLKE, SICF_PCLKE);
	}

	wlc_mctrl_reset(wlc_hw);

	if (PMUCTL_ENAB(wlc_hw->sih))
		wlc_clkctl_clk(wlc_hw, CLK_FAST);

	if (wlc_hw->band && wlc_hw->band->pi) {
		wlc_bmac_phy_reset(wlc_hw);
	}

	/* turn on PHY_PLL */
	wlc_bmac_core_phypll_ctl(wlc_hw, TRUE);

	/* clear sw intstatus */
	wlc_hw->macintstatus = 0;

	/* restore the clk setting */
	if (!fastclk)
		wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);

#ifdef WLP2P_UCODE
	wlc_hw->p2p_shm_base = (uint)~0;
#endif
	wlc_hw->cca_shm_base = (uint)~0;
} /* wlc_bmac_corereset */

/* Search mem rw utilities */

#ifdef MBSS
bool
wlc_bmac_ucodembss_hwcap(wlc_hw_info_t *wlc_hw)
{
	/* add up template space here */
	int templ_ram_sz, fifo_mem_used, i, stat;
	uint blocks = 0;

	for (fifo_mem_used = 0, i = 0; i < NFIFO; i++) {
		stat = wlc_bmac_xmtfifo_sz_get(wlc_hw, i, &blocks);
		if (stat != 0)
			return FALSE;
		fifo_mem_used += blocks;
	}

	templ_ram_sz = ((wlc_hw->machwcap & MCAP_TXFSZ_MASK) >> MCAP_TXFSZ_SHIFT) * 2;

	if ((templ_ram_sz - fifo_mem_used) <
	    (int)MBSS_TPLBLKS(wlc_hw->corerev, WLC_MAX_AP_BSS(wlc_hw->corerev))) {
		WL_ERROR(("wl%d: %s: Insuff mem for MBSS: templ memblks %d fifo memblks %d\n",
			wlc_hw->unit, __FUNCTION__, templ_ram_sz, fifo_mem_used));
		return FALSE;
	}

	return TRUE;
}
#endif /* MBSS */

/**
 * If the ucode that supports corerev 5 is used for corerev 9 and above, txfifo sizes needs to be
 * modified (increased) since the newer cores have more memory.
 */
static void
BCMINITFN(wlc_corerev_fifofixup)(wlc_hw_info_t *wlc_hw)
{
	d11regs_t *regs = wlc_hw->regs;
	uint16 fifo_nu;
	uint16 txfifo_startblk = TXFIFO_START_BLK, txfifo_endblk;
	uint16 txfifo_def, txfifo_def1;
	uint16 txfifo_cmd;
	osl_t *osh;

	if (D11REV_LT(wlc_hw->corerev, 9))
		goto exit;

	/* Re-assign the space for tx fifos to allow BK aggregation */
	if (D11REV_IS(wlc_hw->corerev, 28)) {
		uint16 xmtsz[] = { 30, 47, 22, 14, 8, 1 };

		memcpy(xmtfifo_sz[(wlc_hw->corerev - XMTFIFOTBL_STARTREV)],
		       xmtsz, sizeof(xmtsz));
	}

	if ((CHIPID(wlc_hw->sih->chip) == BCM43242_CHIP_ID)) {
		uint16 xmtsz[] = { 18, 254, 25, 17, 17, 8 };
		memcpy(xmtfifo_sz[(wlc_hw->corerev - XMTFIFOTBL_STARTREV)],
		       xmtsz, sizeof(xmtsz));
	}

	/* tx fifos start at TXFIFO_START_BLK from the Base address */
#ifdef MBSS
	/* 4313 has total fifo space of 128 blocks. if we enable
	 * all 16 MBSSs we will not be left with enough fifo space to
	 * support max thru'put. so we only allow configuring/enabling
	 * max of 4 BSSs. Rest of the space is distributed acorss
	 * the tx fifos.
	 */
	if (D11REV_IS(wlc_hw->corerev, 24)) {
#ifdef WLLPRS
		uint16 xmtsz[] = { 9, 39, 22, 14, 14, 5 };
#else
		uint16 xmtsz[] = { 9, 47, 22, 14, 14, 5 };
#endif
		memcpy(xmtfifo_sz[(wlc_hw->corerev - XMTFIFOTBL_STARTREV)],
		       xmtsz, sizeof(xmtsz));
	}
	if (D11REV_IS(wlc_hw->corerev, 25)) {
		uint16 xmtsz[] = { 9, 47, 22, 14, 14, 5 };
		memcpy(xmtfifo_sz[(wlc_hw->corerev - XMTFIFOTBL_STARTREV)],
			xmtsz, sizeof(xmtsz));
	}

	if (TRUE &&
	    MBSS_ENAB(wlc_hw->wlc->pub) &&
	    wlc_bmac_ucodembss_hwcap(wlc_hw)) {
		ASSERT(WLC_MAX_AP_BSS(wlc_hw->corerev) > 0);
		txfifo_startblk =
			MBSS_TXFIFO_START_BLK(wlc_hw->corerev,
		                              WLC_MAX_AP_BSS(wlc_hw->corerev));
	}
#else
	txfifo_startblk = TXFIFO_START_BLK;
#endif /* MBSS */

	osh = wlc_hw->osh;

	/* sequence of operations:  reset fifo, set fifo size, reset fifo */
	for (fifo_nu = 0; fifo_nu < NFIFO; fifo_nu++) {

		txfifo_endblk = txfifo_startblk + wlc_hw->xmtfifo_sz[fifo_nu];
		txfifo_def = (txfifo_startblk & 0xff) |
			(((txfifo_endblk - 1) & 0xff) << TXFIFO_FIFOTOP_SHIFT);
		txfifo_def1 = ((txfifo_startblk >> 8) & 0x3) |
			((((txfifo_endblk - 1) >> 8) & 0x3) << TXFIFO_FIFOTOP_SHIFT);
		txfifo_cmd = TXFIFOCMD_RESET_MASK | (fifo_nu << TXFIFOCMD_FIFOSEL_SHIFT);

		W_REG(osh, &regs->u.d11regs.xmtfifocmd, txfifo_cmd);
		W_REG(osh, &regs->u.d11regs.xmtfifodef, txfifo_def);
		W_REG(osh, &regs->u.d11regs.xmtfifodef1, txfifo_def1);

		W_REG(osh, &regs->u.d11regs.xmtfifocmd, txfifo_cmd);

		txfifo_startblk += wlc_hw->xmtfifo_sz[fifo_nu];
	}
exit:
	/* need to propagate to shm location to be in sync since ucode/hw won't do this */
	wlc_bmac_write_shm(wlc_hw, M_FIFOSIZE0(wlc_hw), wlc_hw->xmtfifo_sz[TX_AC_BE_FIFO]);
	wlc_bmac_write_shm(wlc_hw, M_FIFOSIZE1(wlc_hw), wlc_hw->xmtfifo_sz[TX_AC_VI_FIFO]);
	wlc_bmac_write_shm(wlc_hw, M_FIFOSIZE2(wlc_hw), ((wlc_hw->xmtfifo_sz[TX_AC_VO_FIFO] << 8) |
		wlc_hw->xmtfifo_sz[TX_AC_BK_FIFO]));
	wlc_bmac_write_shm(wlc_hw, M_FIFOSIZE3(wlc_hw), ((wlc_hw->xmtfifo_sz[TX_ATIM_FIFO] << 8) |
		wlc_hw->xmtfifo_sz[TX_BCMC_FIFO]));
	/* Check if TXFIFO HW config is proper */
	wlc_bmac_txfifo_sz_chk(wlc_hw);
} /* wlc_corerev_fifofixup */

static void
BCMINITFN(wlc_bmac_btc_init)(wlc_hw_info_t *wlc_hw)
{
	/* make sure 2-wire or 3-wire decision has been made */
	ASSERT((wlc_hw->btc->wire >= WL_BTC_2WIRE) || (wlc_hw->btc->wire <= WL_BTC_4WIRE));

	/* Configure selected BTC mode */
	wlc_bmac_btc_mode_set(wlc_hw, wlc_hw->btc->mode);

	if (wlc_hw->boardflags2 & BFL2_BTCLEGACY) {
		if ((CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID) ||
		    (CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
		    (CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID))
			si_btc_enable_chipcontrol(wlc_hw->sih);
		/* Pin muxing changes for BT coex operation in LCNXNPHY */
		if ((CHIPID(wlc_hw->sih->chip) == BCM43131_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM43217_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM43227_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM43228_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM43428_CHIP_ID)) {
			si_btc_enable_chipcontrol(wlc_hw->sih);
			si_pmu_chipcontrol(wlc_hw->sih, PMU1_PLL0_CHIPCTL1, 0x10, 0x10);
		}

		if (CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID) {
			if (wlc_bmac_btc_mode_get(wlc_hw))
				wlc_phy_btclock_war(wlc_hw->band->pi, wlc_hw->btclock_tune_war);
		}
	}

	/* starting from ccrev 35, seci, 3/4 wire can be controlled by newly
	 * constructed SECI block.
	 * exception: X19 (4331) does not utilize this new feature
	 */
	if (wlc_hw->boardflags & BFL_BTCOEX) {
		if (wlc_hw->boardflags2 & BFL2_BTCLEGACY) {
			/* X19 has its special 4 wire which is not using new SECI block */
			if (CHIPID(wlc_hw->sih->chip) != BCM4331_CHIP_ID)
				si_seci_init(wlc_hw->sih, SECI_MODE_LEGACY_3WIRE_WLAN);
		} else if (BCMECICOEX_ENAB_BMAC(wlc_hw)) {
			si_eci_init(wlc_hw->sih);
		} else if (BCMSECICOEX_ENAB_BMAC(wlc_hw)) {
			si_seci_init(wlc_hw->sih, SECI_MODE_SECI);
		} else if (BCMGCICOEX_ENAB_BMAC(wlc_hw)) {
			si_gci_init(wlc_hw->sih);
		}
	}
} /* wlc_bmac_btc_init */

/* Update the splitrx mode
 * mode : requested mode
 * init : whether from coreinit or by other applications
 *
 * During init path, clocks are assured and can skip wl up checks
 * for other use cases wl up should be checked to prevent invalid reg access
 */
int
wlc_update_splitrx_mode(wlc_hw_info_t *wlc_hw, bool mode, bool init)
{
	uint16 rcv_fifo_ctl = 0;
	d11regs_t *regs = wlc_hw->regs;
	wlc_info_t *wlc = wlc_hw->wlc;

	WL_INFORM(("wl: %d %s :  pub up %d int %d mode %d \n",
		wlc_hw->unit, __FUNCTION__, wlc->pub->up, init, mode));

	ASSERT(RXFIFO_SPLIT());

	/* Update SW mode */
	wlc_hw->hdrconv_mode = mode;

	if (!init && !wlc->pub->up) {
		/* dont change HW mode, if not up */
		return BCME_OK;
	}

	/* Update HW mode */

	/* Suspend MAC if not in core init pathh */
	if (!init)
		wlc_bmac_suspend_mac_and_wait(wlc_hw);

	/* Enable bit */
	if (mode)
		OR_REG(wlc_hw->osh, &regs->u.d11acregs.RCVHdrConvStats, HDRCONV_USR_ENAB);
	else
		AND_REG(wlc_hw->osh, &regs->u.d11acregs.RCVHdrConvStats, ~HDRCONV_USR_ENAB);

	/* Read current fifo-sel for backup */
	rcv_fifo_ctl = R_REG(wlc_hw->osh, &regs->rcv_fifo_ctl);

	/* select fifo-0 */
	AND_REG(wlc_hw->osh, &regs->rcv_fifo_ctl,
		(uint16)(~(RXFIFO_CTL_FIFOSEL_MASK)));

	/* update fifo-0 status len to be 0x4 */
	W_REG(wlc_hw->osh, &regs->rcv_status_len, mode ?
		HDRCONV_FIFO0_STSLEN : DEFAULT_FIFO0_STSLEN);

	/* Put back fifo ctrl value */
	W_REG(wlc_hw->osh, &regs->rcv_fifo_ctl, rcv_fifo_ctl);

	/* Enable back MAC if not in core init path */
	if (!init)
		wlc_bmac_enable_mac(wlc_hw);

	return BCME_OK;
}
/**
 * d11 core init
 *   reset PSM
 *   download ucode/PCM
 *   let ucode run to suspended
 *   download ucode inits
 *   config other core registers
 *   init dma/pio
 *   init VASIP
 */
static void
BCMINITFN(wlc_coreinit)(wlc_hw_info_t *wlc_hw)
{
	wlc_info_t *wlc = wlc_hw->wlc;
	d11regs_t *regs = wlc_hw->regs;
	wlc_tunables_t *tune = wlc->pub->tunables;
	bool fifosz_fixup = FALSE;
	uint16 buf[NFIFO] = {0, 0, 0, 0, 0, 0};
#ifdef STA
	uint32 seqnum = 0;
#endif
	uint bcnint_us;
	uint i = 0;
	osl_t *osh = wlc_hw->osh;
#if defined(MBSS)
	bool ucode9 = TRUE;
	(void)ucode9;
#endif

	WL_TRACE(("wl%d: wlc_coreinit\n", wlc_hw->unit));

	wlc_bmac_btc_init(wlc_hw);

	/*
	 * for dual d11 core chips, ucode is downloaded only once
	 * and will be thru core-0
	 */
	if ((wlc_hw->macunit == 0) || !wlc_bmac_rsdb_cap(wlc_hw))
		wlc_ucode_download(wlc_hw);
#if defined(SAVERESTORE)
#ifdef SR_ESSENTIALS
	/* Only needs to be done once.
	 * Needs this before si_pmu_res_init() to use sr_isenab()
	 */
	if (SR_ESSENTIALS_ENAB())
		sr_save_restore_init(wlc_hw->sih);
#endif /* SR_ESSENTIALS */
	if (SR_ENAB() && sr_cap(wlc_hw->sih)) {
		/* Download SR code */
		wlc_bmac_sr_init(wlc_hw);
	}
#endif /* SAVERESTORE */

	/*
	 * FIFOSZ fixup
	 * 1) core5-9 use ucode 5 to save space since the PSM is the same
	 * 2) newer chips, driver wants to controls the fifo allocation
	 */
	fifosz_fixup = TRUE;

	(void) wlc_bmac_wowlucode_start(wlc_hw);

	wlc_gpio_init(wlc_hw);

	if (D11REV_GE(wlc_hw->corerev, 40)) {
		wlc_bmac_reset_amt(wlc_hw);
	}

#ifdef WL11N
	/* REV8+: mux out 2o3 control lines when 3 antennas are available */
	if (wlc_hw->antsel_avail) {
		if (CHIPID(wlc_hw->sih->chip) == BCM43234_CHIP_ID ||
		    CHIPID(wlc_hw->sih->chip) == BCM43235_CHIP_ID ||
		    CHIPID(wlc_hw->sih->chip) == BCM43236_CHIP_ID ||
		    CHIPID(wlc_hw->sih->chip) == BCM43238_CHIP_ID) {
			si_corereg(wlc_hw->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol),
				CCTRL43236_ANT_MUX_2o3, CCTRL43236_ANT_MUX_2o3);

		} else if (((CHIPID(wlc_hw->sih->chip)) == BCM5357_CHIP_ID) ||
		           ((CHIPID(wlc_hw->sih->chip)) == BCM4749_CHIP_ID) ||
		           ((CHIPID(wlc_hw->sih->chip)) == BCM53572_CHIP_ID)) {
			si_pmu_chipcontrol(wlc_hw->sih, 1, CCTRL5357_ANT_MUX_2o3,
				CCTRL5357_ANT_MUX_2o3);
		}
	}
#endif	/* WL11N */

#ifdef STA
	/* store the previous sequence number */
	wlc_bmac_copyfrom_objmem(wlc->hw, S_SEQ_NUM << 2, &seqnum, sizeof(seqnum), OBJADDR_SCR_SEL);


#endif /* STA */

#ifdef BCMUCDOWNLOAD
	if (initvals_ptr) {
		wlc_write_inits(wlc_hw, initvals_ptr);
#ifdef BCM_RECLAIM_INIT_FN_DATA
		MFREE(wlc->osh, initvals_ptr, initvals_len);
		initvals_ptr = NULL;
		initvals_len = 0;
#endif /* BCM_RECLAIM_INIT_FN_DATA */
	} else {
		printf("initvals_ptr is NULL, error in inivals download\n");
	}
#else
#ifdef WLRSDB
	/* init IHR, SHM, and SCR */
	if (wlc_bmac_rsdb_cap(wlc_hw))  {
		if (D11REV_IS(wlc_hw->corerev, 50)) {
			wlc_bmac_rsdb_write_inits(wlc_hw, d11ac12initvals50,
				d11ac12initvals50core1);
		} else if (D11REV_IS(wlc_hw->corerev, 55)) {
			wlc_bmac_rsdb_write_inits(wlc_hw, d11ac24initvals55,
				d11ac24initvals55core1);
		} else if (D11REV_IS(wlc_hw->corerev, 59)) {
			wlc_bmac_rsdb_write_inits(wlc_hw, d11ac24initvals59,
				d11ac24initvals59core1);
		} else
			WL_ERROR(("wl%d: %s: corerev %d is invalid", wlc_hw->unit,
				__FUNCTION__, wlc_hw->corerev));
	} else
#endif /* WLRSDB */

#if defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM)
	if (wlc_uci_check_cap_ucode_rom_axislave(wlc_hw)) {
		wlc_uci_write_inits_with_rom_support(wlc_hw, UCODE_INITVALS);
	} else {
		WL_ERROR(("%s: wl%d: ROM enabled but no axi/ucode-rom cap! %d\n",
			__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	}
/* the "#else" below is intentional */
#else /* defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM) */

	if (D11REV_IS(wlc_hw->corerev, 58)) {
		if (WLCISACPHY(wlc_hw->band)) {
			if (wlc_hw->macunit == 0) {
				wlc_write_inits(wlc_hw, d11ac26initvals58);
			} else if (wlc_hw->macunit == 1) {
				wlc_write_inits(wlc_hw, d11ac27initvals58);
			}
		}
	} else if (D11REV_IS(wlc_hw->corerev, 64)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac32initvals64);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 60)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac36initvals60);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 49)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac9initvals49);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 48)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac8initvals48);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 45) || D11REV_IS(wlc_hw->corerev, 47) ||
	           D11REV_IS(wlc_hw->corerev, 51) || D11REV_IS(wlc_hw->corerev, 52)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac7initvals47);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 54)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac20initvals54);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 54\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 46)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac6initvals46);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 43)) {
		if (WLCISACPHY(wlc_hw->band)) {
		    wlc_write_inits(wlc_hw, d11ac3initvals43);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 42)) {
		if (WLCISACPHY(wlc_hw->band)) {
		    wlc_write_inits(wlc_hw, d11ac1initvals42);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 41) || D11REV_IS(wlc_hw->corerev, 44) ||
		D11REV_IS(wlc_hw->corerev, 45)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac2initvals41);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 40)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ac0initvals40);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 39)) {
		if (WLCISLCN20PHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11lcn200initvals39);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 38)) {
		if (WLCISLCN40PHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_d11lcn407initvals38_addr());
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 37)) {
		if (WLCISLCN40PHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_d11lcn406initvals37_addr());
		} else if (WLCISNPHY(wlc_hw->band)) {
			fifosz_fixup = TRUE;
			wlc_write_inits(wlc_hw, wlc_get_n22initvals31_addr());
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 34)) {
		if (WLCISNPHY(wlc_hw->band)) {
			fifosz_fixup = TRUE;
			wlc_write_inits(wlc_hw, wlc_get_n19initvals34_addr());
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 34\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 33)) {
		if (WLCISLCN40PHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_d11lcn400initvals33_addr());

			wlc_bmac_mhf(wlc_hw, MHF5, MHF5_SPIN_AT_SLEEP,
				MHF5_SPIN_AT_SLEEP, WLC_BAND_2G);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 32)) {
		if (WLCISNPHY(wlc_hw->band)) {
			fifosz_fixup = TRUE;
			wlc_write_inits(wlc_hw, wlc_get_n18initvals32_addr());
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 32\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 31)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ht0initvals29);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 30)) {
		if (WLCISNPHY(wlc_hw->band))
			wlc_write_inits(wlc_hw, d11n16initvals30);
	} else if (D11REV_IS(wlc_hw->corerev, 29)) {
		if (WLCISHTPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11ht0initvals29);
		} else
			WL_ERROR(("wl%d: unsupported phy in corerev 26 \n", wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 26)) {
		if (WLCISHTPHY(wlc_hw->band)) {
			if (D11REV_IS(wlc_hw->corerev, 26))
				wlc_write_inits(wlc_hw, d11ht0initvals26);
			else if (D11REV_IS(wlc_hw->corerev, 29))
				wlc_write_inits(wlc_hw, d11ht0initvals29);
		} else
			WL_ERROR(("wl%d: unsupported phy in corerev 26 \n", wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 25) || D11REV_IS(wlc_hw->corerev, 28)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11n0initvals25);
		} else if (WLCISLCNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_lcn0initvals25_addr());
			if (MBSS_ENAB(wlc->pub)) {
				fifosz_fixup = TRUE;
			}
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 24)) {
		if (WLCISNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, d11n0initvals24);
		} else if (WLCISLCNPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, wlc_get_lcn0initvals24_addr());
		} else
			WL_ERROR(("wl%d: unsupported phy in corerev 24 \n", wlc_hw->unit));
	} else if (D11REV_GE(wlc_hw->corerev, 22)) {
		if (WLCISNPHY(wlc_hw->band)) {
			/* ucode only supports rev23(43224b0) with rev16 ucode */
			if (D11REV_IS(wlc_hw->corerev, 23))
				wlc_write_inits(wlc_hw, d11n0initvals16);
			else
				wlc_write_inits(wlc_hw, d11n0initvals22);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	}
#endif /* defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM) */
#endif /* BCMUCDOWNLOAD */
	/* For old ucode, txfifo sizes needs to be modified(increased) for Corerev >= 9 */
	if (D11REV_GE(wlc_hw->corerev, 40)) {
#ifdef WLRSDB
		if (!wlc_bmac_rsdb_cap(wlc_hw) ||
			wlc_rsdb_is_other_chain_idle(wlc_hw->wlc) == TRUE)
#endif /* WLRSDB */
			wlc_bmac_bmc_init(wlc_hw);
	} else if (D11REV_LT(wlc_hw->corerev, 40)) {
		if (fifosz_fixup == TRUE) {
			wlc_corerev_fifofixup(wlc_hw);
		}
		wlc_corerev_fifosz_validate(wlc_hw, buf);
	} else {
		printf("add support for fifo inits for corerev %d......\n", wlc_hw->corerev);
		ASSERT(0);
	}

	/* make sure we can still talk to the mac */
	ASSERT(R_REG(osh, &regs->maccontrol) != 0xffffffff);

	/* band-specific inits done by wlc_bsinit() */

#ifdef MBSS
	if (MBSS_ENAB(wlc->pub)) {
		/* Set search engine ssid lengths to zero */
		if (wlc_bmac_ucodembss_hwcap(wlc_hw) == TRUE) {
			uint32 start, swplen, idx;

			swplen = 0;
			for (idx = 0; idx < (uint) wlc->pub->tunables->maxucodebss; idx++) {

				start = SHM_MBSS_SSIDSE_BASE_ADDR(wlc) +
					(idx * SHM_MBSS_SSIDSE_BLKSZ(wlc));

				wlc_bmac_copyto_objmem(wlc_hw, start, &swplen,
					SHM_MBSS_SSIDLEN_BLKSZ, OBJADDR_SRCHM_SEL);
			}
		}
	}
#endif /* MBSS */

	/* Set up frame burst size and antenna swap threshold init values */
	wlc_bmac_write_shm(wlc_hw, M_MBURST_SIZE(wlc_hw), MAXTXFRAMEBURST);
	wlc_bmac_write_shm(wlc_hw, M_MAX_ANTCNT(wlc_hw), ANTCNT);

	/* set intrecvlazy to configured value */
	wlc_bmac_rcvlazy_update(wlc_hw, wlc_hw->intrcvlazy);

	/* set the station mode (BSS STA) */
	wlc_bmac_mctrl(wlc_hw,
	          (MCTL_INFRA | MCTL_DISCARD_PMQ | MCTL_AP),
	          (MCTL_INFRA | MCTL_DISCARD_PMQ));

	if (PIO_ENAB_HW(wlc_hw)) {
		/* set fifo mode for each VALID rx fifo */
		wlc_rxfifo_setpio(wlc_hw);

		for (i = 0; i < NFIFO; i++)
			if (wlc_hw->pio[i])
				wlc_pio_init(wlc_hw->pio[i]);

#ifdef IL_BIGENDIAN
		/* enable byte swapping */
		wlc_bmac_mctrl(wlc_hw, MCTL_BIGEND, MCTL_BIGEND);
#endif /* IL_BIGENDIAN */
	}

	/* set up Beacon interval */
	bcnint_us = 0x8000 << 10;
	W_REG(osh, &regs->tsf_cfprep, (bcnint_us << CFPREP_CBI_SHIFT));
	W_REG(osh, &regs->tsf_cfpstart, bcnint_us);
	SET_MACINTSTATUS(osh, regs, MI_GP1);

	/* write interrupt mask */
	W_REG(osh, &regs->intctrlregs[RX_FIFO].intmask, DEF_RXINTMASK);
	/* interrupt for second fifo */
	if (PKT_CLASSIFY_EN(RX_FIFO1) || RXFIFO_SPLIT())
		W_REG(osh, &regs->intctrlregs[RX_FIFO1].intmask, DEF_RXINTMASK);

	/* interrupt for third fifo in MODE 3 and MODE 4 */
	if (PKT_CLASSIFY_EN(RX_FIFO2)) {
		W_REG(osh, &regs->intctrlregs[RX_FIFO2].intmask, DEF_RXINTMASK);
	}

#ifdef DMATXRC
	if (DMATXRC_ENAB(wlc->pub) && D11REV_GE(wlc_hw->corerev, 40)) {
		W_REG(osh, &regs->intctrlregs[TX_DATA_FIFO].intmask, I_XI);
		wlc->txrc_fifo_mask |= (1 << TX_DATA_FIFO);
	}
#endif

	/* allow the MAC to control the PHY clock (dynamic on/off) */
	wlc_bmac_macphyclk_set(wlc_hw, ON);

	/* program dynamic clock control fast powerup delay register */
	wlc_hw->fastpwrup_dly = si_clkctl_fast_pwrup_delay(wlc_hw->sih);
	W_REG(osh, &regs->u.d11regs.scc_fastpwrup_dly, wlc_hw->fastpwrup_dly);
	if (D11REV_GT(wlc_hw->corerev, 40)) {
		/* For corerev >= 40, M_UCODE_DBGST is set after
			* the synthesizer is powered up in wake sequence.
			* So add the synthpu delay to wait for wake functionality.
			*/
		wlc_hw->fastpwrup_dly += wlc_bmac_synthpu_dly(wlc_hw);
	}

	/* tell the ucode the corerev */
	wlc_bmac_write_shm(wlc_hw, M_MACHW_VER(wlc_hw), (uint16)wlc_hw->corerev);
	wlc_bmac_write_shm(wlc_hw, M_UCODE_DBGST(wlc_hw), DBGST_SUSPENDED);

	wlc_bmac_write_shm(wlc_hw, M_MACHW_CAP_L(wlc_hw), (uint16)(wlc_hw->machwcap & 0xffff));
	wlc_bmac_write_shm(wlc_hw, M_MACHW_CAP_H(wlc_hw),
		(uint16)((wlc_hw->machwcap >> 16) & 0xffff));

	/* write retry limits to SCR, this done after PSM init */
	wlc_bmac_copyto_objmem(wlc_hw, S_DOT11_SRC_LMT << 2, &(wlc_hw->SRL),
		sizeof(wlc_hw->SRL), OBJADDR_SCR_SEL);

	wlc_bmac_copyto_objmem(wlc_hw, S_DOT11_LRC_LMT << 2, &(wlc_hw->LRL),
		sizeof(wlc_hw->LRL), OBJADDR_SCR_SEL);

#ifdef STA
	if (wlc->seq_reset) {
		wlc->seq_reset = FALSE;
	} else {
		/* write the previous sequence number, this done after PSM init */
		wlc_bmac_copyto_objmem(wlc->hw, S_SEQ_NUM << 2, &seqnum,
			sizeof(seqnum), OBJADDR_SCR_SEL);
	}
#endif /* STA */

	/* write rate fallback retry limits */
	wlc_bmac_write_shm(wlc_hw, M_SFRMTXCNTFBRTHSD(wlc_hw), wlc_hw->SFBL);
	wlc_bmac_write_shm(wlc_hw, M_LFRMTXCNTFBRTHSD(wlc_hw), wlc_hw->LFBL);

	if (D11REV_GE(wlc_hw->corerev, 16)) {
		AND_REG(osh, &regs->u.d11regs.ifs_ctl, 0x0FFF);
		W_REG(osh, &regs->u.d11regs.ifs_aifsn, EDCF_AIFSN_MIN);
	}

	/* dma or pio initializations */
	if (!PIO_ENAB_HW(wlc_hw)) {
		wlc->txpend16165war = 0;

		/* init the tx dma engines */
		for (i = 0; i < NFIFO; i++) {
			if (wlc_hw->di[i])
				dma_txinit(wlc_hw->di[i]);
		}

		/* tx DMA suspend and resume */
		if (D11REV_IS(wlc_hw->corerev, 43) ||
			D11REV_IS(wlc_hw->corerev, 55) ||
			D11REV_IS(wlc_hw->corerev, 59)) {
			for (i = 0; i < NFIFO; i++) {
				if (wlc_hw->di[i]) {

					dma_txsuspend(wlc_hw->di[i]);

			/* after dma tx suspend for a specifc channel  before going for txresume */
			/* corresponding chnstatus bit needs to checked for zero  */
			/* else it could have some bad  impact on dma engine */
					if (!wlc_bmac_istxsuspend(wlc_hw, i)) {

#ifdef WLUCODE_DBG
						wlc_bmac_ucode_dump_die_on_error(wlc_hw);
#endif /* WLUCODE_DBG */
					}

					dma_txresume(wlc_hw->di[i]);
				}
			}
		}

#ifdef WLRSDB
		if (wlc_bmac_rsdb_cap(wlc_hw)) {
			if (wlc_rsdb_mode(wlc_hw->wlc) == PHYMODE_RSDB)
				wlc_bmac_update_rxpost_rxbnd(wlc_hw, NRXBUFPOST_SMALL, RXBND_SMALL);
			else
				wlc_bmac_update_rxpost_rxbnd(wlc_hw, NRXBUFPOST, RXBND);
		}
#endif /* WLRSDB */

		for (i = 0; i < MAX_RX_FIFO; i++) {
			if ((wlc_hw->di[i] != NULL) && wlc_bmac_rxfifo_enab(i)) {
				/* JIRA:SWWLAN-61395 Enable D11 Rx WAR for required D11 Cores
				 * Refer JIRA:CRWLDOT11M-1776
				*/
				if (D11REV_IS(wlc_hw->corerev, 54) ||
					D11REV_IS(wlc_hw->corerev, 55) ||
					D11REV_IS(wlc_hw->corerev, 57)) {
					dma_param_set(wlc_hw->di[i], HNDDMA_PID_D11RX_WAR, 1);
				}

				dma_rxinit(wlc_hw->di[i]);
				dma_rxfill(wlc_hw->di[i]);
			}

		}

		if (RXFIFO_SPLIT()) {
			/* Enable Header conversion */
			wlc_update_splitrx_mode(wlc_hw, wlc_hw->hdrconv_mode, TRUE);

			/* copy count value */
			W_REG(wlc_hw->osh, &regs->u_rcv.d11acregs.rcv_copcnt_q1, tune->copycount);
		}
	} else {
		for (i = 0; i < NFIFO; i++) {
			uint tmp = 0;
			if (wlc_pio_txdepthget(wlc_hw->pio[i]) == 0) {
				wlc_pio_txdepthset(wlc_hw->pio[i], (buf[i] << 8));

				tmp = wlc_pio_txdepthget(wlc_hw->pio[i]);
				if (tmp)
					wlc_pio_txdepthset(wlc_hw->pio[i], tmp - 4);
			}
		}
	}

	if (
		(CHIPID(wlc_hw->sih->chip) == BCM4334_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4314_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43142_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43143_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43340_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43341_CHIP_ID) ||
		0) {
		/* The value to be written into these registers is (2^26)/(freq)MHz */
		/* Ex. MAC clock frequency for 4334 is 96MHz = 0xaaaab */
		uint32 val;

		val = (2 << 25)/(si_clock(wlc_hw->sih)/1000000);
		W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, val & 0xffff);
		W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, val >> 16);
	} else if ((CHIPID(wlc_hw->sih->chip) == BCM4335_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43430_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43018_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4364_CHIP_ID) ||
		BCM4349_CHIP(wlc_hw->sih->chip) ||
		0)
	{
		wlc_bmac_switch_macfreq(wlc_hw, 0);
	}

	/*
	Set the TA at the appropriate SHM location for CTS2SELF frames
	generated by ucode for AC only
	*/
	if (D11REV_GE(wlc_hw->corerev, 40))
		wlc_bmac_set_myaddr(wlc_hw, &(wlc_hw->etheraddr));

	/* initialize btc_params and btc_flags */
	wlc_bmac_btc_param_init(wlc_hw);
#ifdef BCMLTECOEX
	/* config ltecx interface */
	if (BCMLTECOEX_ENAB(wlc->pub))	{
		wlc_ltecx_init(wlc->ltecx);
	}
#endif /* BCMLTECOEX */

#ifdef WLP2P_UCODE
	if (DL_P2P_UC(wlc_hw)) {
		/* enable P2P mode */
		wlc_bmac_mhf(wlc_hw, MHF5, MHF5_P2P_MODE,
		             wlc_hw->_p2p ? MHF5_P2P_MODE : 0, WLC_BAND_ALL);
		/* cache p2p SHM location */
		wlc_hw->p2p_shm_base = wlc_bmac_read_shm(wlc_hw, M_P2P_BLK_PTR(wlc_hw)) << 1;
	}
#endif
	if (D11REV_LT(wlc_hw->corerev, 40)) {
		wlc_hw->cca_shm_base = M_CCA_STATS_BLK(wlc_hw);
	} else {
		wlc_hw->cca_shm_base = (wlc_bmac_read_shm(wlc_hw, M_CCASTATS_PTR(wlc_hw)) << 1);
		wlc_hw->macstat1_shm_base = (wlc_bmac_read_shm(wlc_hw,
			M_PSM2HOST_EXT_PTR(wlc_hw)) << 1);
	}

	/* Shmem pm_dur is reset by ucode as part of auto-init, hence call wlc_reset_accum_pmdur */
	wlc_reset_accum_pmdur(wlc);
	WL_ERROR(("wl%d: CORE INIT : mode %d pktclassify %d rxsplit %d  hdr conversion %d \n",
		wlc_hw->unit, BCMSPLITRX_MODE(), PKT_CLASSIFY(),
		RXFIFO_SPLIT(), wlc_hw->hdrconv_mode));

	/* Enable RXE HT request on receiving first valid byte */
	/* PSM_CORECTLSTS bit 7 RxeEarlyHTreqEn is increasing VBAT current by 4mA for 43012 chip
	* Modified the BIT setting same as ucode code flow
	*/
	if (D11REV_GE(wlc_hw->corerev, 24) &&
		(!(D11REV_GE(wlc_hw->corerev, 64) || D11REV_IS(wlc_hw->corerev, 60)))) {
		OR_REG(osh, &wlc_hw->regs->psm_corectlsts, PSM_CORE_CTL_REHE);
	}
}

/** Reset the PM duration accumulator maintained by SW */
static int
wlc_reset_accum_pmdur(wlc_info_t *wlc)
{
	wlc->pm_dur_clear_timeout = TIMEOUT_TO_READ_PM_DUR;
	wlc->wlc_pm_dur_last_sample =
		wlc_bmac_cca_read_counter(wlc->hw, M_MAC_SLPDUR_L_OFFSET(wlc),
			M_MAC_SLPDUR_H_OFFSET(wlc));
	return BCME_OK;
}

/**
 * On changing the MAC clock frequency, the tsf frac register must be adjusted accordingly.
 * If spur avoidance mode is off, the mac freq will be 80/120/160Mhz
 * If spur avoidance mode is on1, the mac freq will be 82/123/164Mhz
 * If spur avoidance mode is on2, the mac freq will be 84/126/168Mhz
 * Formula is 2^26/freq(MHz)
 */
void
wlc_bmac_switch_macfreq(wlc_hw_info_t *wlc_hw, uint8 spurmode)
{
	d11regs_t *regs;
	osl_t *osh;

	/* this function is called only by AC, N, LCN and HT PHYs */
	ASSERT(WLCISNPHY(wlc_hw->band) || WLCISLCNPHY(wlc_hw->band) ||
		WLCISHTPHY(wlc_hw->band) || WLCISACPHY(wlc_hw->band) ||
		WLCISLCN20PHY(wlc_hw->band));

	regs = wlc_hw->regs;
	osh = wlc_hw->osh;

	/* ??? better keying, corerev, phyrev ??? */
	if ((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID)) {
		if (spurmode == WL_SPURAVOID_ON2) { /* 168MHz */
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, 0x1862);
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, 0x6);
		} else if (spurmode == WL_SPURAVOID_ON1) { /* 164MHz */
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, 0x3E70);
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, 0x6);
		} else { /* 160MHz */
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, 0x6666);
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, 0x6);
		}
	} else if ((CHIPID(wlc_hw->sih->chip) == BCM43420_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43224_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43225_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43421_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43131_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43217_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43227_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43228_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43428_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43242_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43243_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43234_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43235_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43236_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43238_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43237_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM6362_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM5357_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4345_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4749_CHIP_ID)) {
		if (spurmode == WL_SPURAVOID_ON2) {	/* 126Mhz */
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, 0x2082);
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, 0x8);
		} else if (spurmode == WL_SPURAVOID_ON1) {	/* 123Mhz */
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, 0x5341);
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, 0x8);
		} else {	/* 120Mhz */
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, 0x8889);
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, 0x8);
		}
	} else if (CHIPID(wlc_hw->sih->chip) == BCM4335_CHIP_ID ||
		(CHIPID(wlc_hw->sih->chip) == BCM43430_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43018_CHIP_ID) ||
		BCM4349_CHIP(wlc_hw->sih->chip) ||
		BCM53573_CHIP(wlc_hw->sih->chip) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43012_CHIP_ID) ||
		0)
	{
		uint32 mac_clk;
		uint32 clk_frac;
		uint16 frac_l, frac_h;
		uint32 r_high, r_low;

		mac_clk = si_mac_clk(wlc_hw->sih, wlc_hw->osh);

		/* the mac_clk is scaled by 1000 */
		/* so, multiplier for numerator will be 1 / (mac_clk / 1000): 1000 */
		bcm_uint64_multiple_add(&r_high, &r_low, (1 << 26), 1000, (mac_clk >> 1));
		bcm_uint64_divide(&clk_frac, r_high, r_low, mac_clk);

		frac_l =  (uint16)(clk_frac & 0xffff);
		frac_h =  (uint16)((clk_frac >> 16) & 0xffff);

		W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, frac_l);
		W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, frac_h);

	} else if ((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM43526_CHIP_ID) ||
		(CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID) ||
		BCM43602_CHIP(wlc_hw->sih->chip) ||
		BCM4350_CHIP(wlc_hw->sih->chip) ||
		0) {
		uint32 bbpll_freq, clk_frac;
		uint32 xtalfreq = si_alp_clock(wlc_hw->sih);

		if (BCM4350_CHIP(wlc_hw->sih->chip) &&
			((BUSTYPE(wlc_hw->sih->bustype) == SI_BUS) || (xtalfreq == 37400000))) {
			WL_ERROR(("%s: 4350 need fix for 37.4Mhz\n", __FUNCTION__));
			return;
		}

		bbpll_freq = si_pmu_get_bb_vcofreq(wlc_hw->sih, osh, 40); /* in [100Hz] units */

		/* 6 * 8 * 10000 * 2^23 = 0x3A980000000 */
		bcm_uint64_divide(&clk_frac, 0x3A9, 0x80000000, bbpll_freq);

		W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, clk_frac & 0xffff);
		W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, (clk_frac >> 16) & 0xffff);

	} else if (WLCISLCNPHY(wlc_hw->band)) {
		if (spurmode == WL_SPURAVOID_ON1) {	/* 82Mhz */
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, 0x7CE0);
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, 0xC);
		} else {	/* 80Mhz */
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_l, 0xCCCD);
			W_REG(osh, &regs->u.d11regs.tsf_clk_frac_h, 0xC);
		}
	}
} /* wlc_bmac_switch_macfreq */

/** Initialize GPIOs that are controlled by D11 core */
static void
BCMINITFN(wlc_gpio_init)(wlc_hw_info_t *wlc_hw)
{
	d11regs_t *regs = wlc_hw->regs;
	uint32 gc, gm;
	osl_t *osh = wlc_hw->osh;
	/* use GPIO select 0 to get all gpio signals from the gpio out reg */
	wlc_bmac_mctrl(wlc_hw, MCTL_GPOUT_SEL_MASK, 0);


	/*
	 * Common GPIO setup:
	 *	G0 = LED 0 = WLAN Activity
	 *	G1 = LED 1 = WLAN 2.4 GHz Radio State
	 *	G2 = LED 2 = WLAN 5 GHz Radio State
	 *	G4 = radio disable input (HI enabled, LO disabled)
	 * Boards that support BT Coexistence:
	 *	G7 = BTC
	 *	G8 = BTC
	 * Boards with chips that have fewer gpios and support BT Coexistence:
	 *	G4 = BTC
	 *	G5 = BTC
	 */

	gc = gm = 0;

	/* Set/clear GPIOs for BTC */
	if (wlc_hw->btc->gpio_out != 0)
		wlc_bmac_btc_gpio_enable(wlc_hw);

#ifdef WL11N
	/* Allocate GPIOs for mimo antenna diversity feature */
	if (WLANTSEL_ENAB(wlc)) {
		if (wlc_hw->antsel_type == ANTSEL_2x3 || wlc_hw->antsel_type == ANTSEL_1x2_CORE1 ||
			wlc_hw->antsel_type == ANTSEL_1x2_CORE0) {
			/* Enable antenna diversity, use 2x3 mode */
			wlc_bmac_mhf(wlc_hw, MHF3, MHF3_ANTSEL_EN, MHF3_ANTSEL_EN, WLC_BAND_ALL);
			wlc_bmac_mhf(wlc_hw, MHF3, MHF3_ANTSEL_MODE, MHF3_ANTSEL_MODE,
				WLC_BAND_ALL);

			/* init superswitch control */
			wlc_phy_antsel_init(wlc_hw->band->pi, FALSE);

		} else if ((wlc_hw->antsel_type == ANTSEL_2x4) &&
		           ((wlc_hw->sih->boardvendor != VENDOR_APPLE) ||
		            ((wlc_hw->sih->boardtype != BCM94350X14) &&
		             (wlc_hw->sih->boardtype != BCM94350X14P2)))) {
			/* X14 module use GPIO13 as input (Power Throttle) */
			ASSERT((gm & BOARD_GPIO_12) == 0);
			gm |= gc |= (BOARD_GPIO_12 | BOARD_GPIO_13);
			/* The board itself is powered by these GPIOs (when not sending pattern)
			* So set them high
			*/
			OR_REG(osh, &regs->psm_gpio_oe, (BOARD_GPIO_12 | BOARD_GPIO_13));
			OR_REG(osh, &regs->psm_gpio_out, (BOARD_GPIO_12 | BOARD_GPIO_13));

			/* Enable antenna diversity, use 2x4 mode */
			wlc_bmac_mhf(wlc_hw, MHF3, MHF3_ANTSEL_EN, MHF3_ANTSEL_EN, WLC_BAND_ALL);
			wlc_bmac_mhf(wlc_hw, MHF3, MHF3_ANTSEL_MODE, 0, WLC_BAND_ALL);

			/* Configure the desired clock to be 4Mhz */
			wlc_bmac_write_shm(wlc_hw, M_ANTSEL_CLKDIV(wlc_hw), ANTSEL_CLKDIV_4MHZ);
		}
	}
#endif /* WL11N */
	/* gpio 9 controls the PA.  ucode is responsible for wiggling out and oe */
	if (wlc_hw->boardflags & BFL_PACTRL)
		gm |= gc |= BOARD_GPIO_PACTRL;

	/* gpio 14(Xtal_up) and gpio 15(PLL_powerdown) are controlled in PCI config space */

	/* config dual wlan radio coex function in bmac driver and monolithic driver */

	WL_INFORM(("wl%d: gpiocontrol mask 0x%x value 0x%x\n", wlc_hw->unit, gm, gc));

	/* apply to gpiocontrol register */
	si_gpiocontrol(wlc_hw->sih, gm, gc, GPIO_DRV_PRIORITY);
} /* wlc_gpio_init */

#ifndef BCMUCDOWNLOAD
static int
BCMATTACHFN(wlc_ucode_download)(wlc_hw_info_t *wlc_hw)
{
	int err = BCME_OK;
	if (wlc_hw->ucode_loaded)
		goto done;

#if defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM)
	// process and enable patches
	if ((err = wlc_uci_download_rom_patches(wlc_hw)) != BCME_OK) {
		goto done;
	}
/* the "#else" below is intentional */
#else /* defined (UCODE_IN_ROM_SUPPORT) && !defined (ULP_DS1ROM_DS0RAM) */

#if defined(WLP2P_UCODE)
	if (DL_P2P_UC(wlc_hw)) {
		const uint32 *ucode32 = NULL;
		const uint8 *ucode8 = NULL;
		uint nbytes = 0;

		if (WLCISACPHY(wlc_hw->band)) {
			if (D11REV_IS(wlc_hw->corerev, 58)) {
				if (wlc_hw->macunit == 0) {
					ucode32 = d11ucode_p2p58_D11b;
					nbytes = d11ucode_p2p58_D11bsz;
				} else if (wlc_hw->macunit == 1) {
					ucode32 = d11ucode_p2p58_D11a;
					nbytes = d11ucode_p2p58_D11asz;
				} else {
					/* Error condition */
					ASSERT(0);
				}
			} else if (D11REV_IS(wlc_hw->corerev, 50)) {
				ucode32 = d11ucode_p2p50;
				nbytes = d11ucode_p2p50sz;
			} else
			if (D11REV_IS(wlc_hw->corerev, 65)) {
				ucode32 = d11ucode_p2p65;
				nbytes = d11ucode_p2p65sz;
			} else if (D11REV_IS(wlc_hw->corerev, 64)) {
				ucode32 = d11ucode_p2p64;
				nbytes = d11ucode_p2p64sz;
			} else if (D11REV_IS(wlc_hw->corerev, 60)) {
				ucode32 = d11ucode_p2p60;
				nbytes = d11ucode_p2p60sz;
			} else if (D11REV_IS(wlc_hw->corerev, 55)) {
				ucode32 = d11ucode_p2p55;
				nbytes = d11ucode_p2p55sz;
			} else if (D11REV_IS(wlc_hw->corerev, 59)) {
				ucode32 = d11ucode_p2p59;
				nbytes = d11ucode_p2p59sz;
			} else if (D11REV_IS(wlc_hw->corerev, 49)) {
				ucode32 = d11ucode_p2p49;
				nbytes = d11ucode_p2p49sz;
			} else if (D11REV_IS(wlc_hw->corerev, 48)) {
				ucode32 = d11ucode_p2p48;
				nbytes = d11ucode_p2p48sz;
			} else if (D11REV_IS(wlc_hw->corerev, 45) ||
			           D11REV_IS(wlc_hw->corerev, 47) ||
			           D11REV_IS(wlc_hw->corerev, 51) ||
			           D11REV_IS(wlc_hw->corerev, 52)) {
				ucode32 = d11ucode_p2p47;
				nbytes = d11ucode_p2p47sz;
			} else if (D11REV_IS(wlc_hw->corerev, 54)) {
				ucode32 = d11ucode_p2p54;
				nbytes = d11ucode_p2p54sz;
			} else if (D11REV_IS(wlc_hw->corerev, 46)) {
				ucode32 = d11ucode_p2p46;
				nbytes = d11ucode_p2p46sz;
			} else if (D11REV_IS(wlc_hw->corerev, 43)) {
				ucode32 = d11ucode_p2p43;
				nbytes = d11ucode_p2p43sz;
			} else if (D11REV_IS(wlc_hw->corerev, 42)) {
				ucode32 = d11ucode_p2p42;
				nbytes = d11ucode_p2p42sz;
			} else if (D11REV_IS(wlc_hw->corerev, 41) ||
			           D11REV_IS(wlc_hw->corerev, 44)) {
				ucode32 = d11ucode_p2p41;
				nbytes = d11ucode_p2p41sz;
			} else if (D11REV_IS(wlc_hw->corerev, 40)) {
				ucode32 = d11ucode_p2p40;
				nbytes = d11ucode_p2p40sz;
			} else {
				/* not supported yet */
				WL_ERROR(("no p2p ucode for rev %d\n", wlc_hw->corerev));
				ASSERT(0);
				goto done;
			}
		} else if (WLCISHTPHY(wlc_hw->band)) {
			if (D11REV_IS(wlc_hw->corerev, 26)) {
				ucode32 = d11ucode_p2p26_mimo;
				nbytes = d11ucode_p2p26_mimosz;
			} else if (D11REV_IS(wlc_hw->corerev, 29)) {
				ucode32 = d11ucode_p2p29_mimo;
				nbytes = d11ucode_p2p29_mimosz;
			}
		} else if (WLCISNPHY(wlc_hw->band)) {
			if (D11REV_IS(wlc_hw->corerev, 37)) {
#if defined WLP2P_DISABLED
				/* Temporary hack to use non p2p ucode */
				ucode32 = d11ucode31_mimo;
				nbytes = d11ucode31_mimosz;
#else
				ucode32 = d11ucode_p2p31_mimo;
				nbytes = d11ucode_p2p31_mimosz;
#endif
			} else if (D11REV_IS(wlc_hw->corerev, 36)) {
#if defined WLP2P_DISABLED
				/* Temporary hack to use non p2p ucode */
				ucode32 = d11ucode36_mimo;
				nbytes = d11ucode36_mimosz;
#else
				ucode32 = d11ucode_p2p36_mimo;
				nbytes = d11ucode_p2p36_mimosz;
#endif
			} else if (D11REV_IS(wlc_hw->corerev, 34)) {
				ucode32 = d11ucode_p2p34_mimo;
				nbytes = d11ucode_p2p34_mimosz;
			} else if (D11REV_IS(wlc_hw->corerev, 32)) {
				ucode32 = d11ucode_p2p32_mimo;
				nbytes = d11ucode_p2p32_mimosz;
			} else if (D11REV_IS(wlc_hw->corerev, 31)) {
				ucode32 = d11ucode_p2p29_mimo;
				nbytes = d11ucode_p2p29_mimosz;
			} else if (D11REV_IS(wlc_hw->corerev, 30)) {
				ucode32 = d11ucode_p2p30_mimo;
				nbytes = d11ucode_p2p30_mimosz;
			} else if (D11REV_IS(wlc_hw->corerev, 25) ||
				D11REV_IS(wlc_hw->corerev, 28)) {
				ucode32 = d11ucode_p2p25_mimo;
				nbytes = d11ucode_p2p25_mimosz;
			} else if (D11REV_IS(wlc_hw->corerev, 24)) {
				ucode32 = d11ucode_p2p24_mimo;
				nbytes = d11ucode_p2p24_mimosz;
			} else if (D11REV_IS(wlc_hw->corerev, 23)) {
				/* ucode supports rev23(43224b0) with rev16 ucode */
				ucode32 = d11ucode_p2p16_mimo;
				nbytes = d11ucode_p2p16_mimosz;
			} else if (D11REV_IS(wlc_hw->corerev, 22)) {
				ucode32 = d11ucode_p2p22_mimo;
				nbytes = d11ucode_p2p22_mimosz;
			}
		} else if (WLCISLCNPHY(wlc_hw->band)) {
			if (D11REV_IS(wlc_hw->corerev, 25)) {
				ucode8 = d11ucode_p2p25_lcn;
				nbytes = d11ucode_p2p25_lcnsz;
			} else if (D11REV_IS(wlc_hw->corerev, 24)) {
				ucode8 = d11ucode_p2p24_lcn;
				nbytes = d11ucode_p2p24_lcnsz;
			}
		} else if (WLCISLCN20PHY(wlc_hw->band)) {
			if (D11REV_IS(wlc_hw->corerev, 39)) {
				ucode8 = d11ucode_p2p39_lcn20;
				nbytes = d11ucode_p2p39_lcn20sz;
			}
		} else if (WLCISLCN40PHY(wlc_hw->band)) {
			if (D11REV_IS(wlc_hw->corerev, 38)) {
				ucode8 = d11ucode_p2p38_lcn40;
				nbytes = d11ucode_p2p38_lcn40sz;
			} else if (D11REV_IS(wlc_hw->corerev, 37)) {
				ucode8 = d11ucode_p2p37_lcn40;
				nbytes = d11ucode_p2p37_lcn40sz;
			} else if (D11REV_IS(wlc_hw->corerev, 33)) {
				ucode8 = d11ucode_p2p33_lcn40;
				nbytes = d11ucode_p2p33_lcn40sz;
			}
		}

		if (ucode32 != NULL)
			wlc_ucode_write(wlc_hw, ucode32, nbytes);
		else if (ucode8 != NULL)
			wlc_ucode_write_byte(wlc_hw, ucode8, nbytes);
		else {
			WL_ERROR(("%s: wl%d: unsupported phy %d in corerev %d for P2P\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->band->phytype,
			          wlc_hw->corerev));
			goto done;
		}
	} else
#endif /* WLP2P_UCODE */
	if (D11REV_IS(wlc_hw->corerev, 58)) {
		if (WLCISACPHY(wlc_hw->band)) {
			if (wlc_hw->macunit == 0) {
				wlc_ucode_write(wlc_hw, d11ucode58_D11b, d11ucode58_D11bsz);
			} else if (wlc_hw->macunit == 1) {
				wlc_ucode_write(wlc_hw, d11ucode58_D11a, d11ucode58_D11asz);
			} else {
				/* Error condition */
				ASSERT(0);
			}
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 50\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 64)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_ucode_write(wlc_hw, d11ucode64, d11ucode64sz);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 64\n",
				__FUNCTION__, wlc_hw->unit));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 60)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_ucode_write(wlc_hw, d11ucode60, d11ucode60sz);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 60\n",
				__FUNCTION__, wlc_hw->unit));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 59)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_ucode_write(wlc_hw, d11ucode59, d11ucode59sz);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 59\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 50)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_ucode_write(wlc_hw, d11ucode50, d11ucode50sz);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 50\n",
				__FUNCTION__, wlc_hw->unit));
	} else
	if (D11REV_IS(wlc_hw->corerev, 56)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_ucode_write(wlc_hw, d11ucode56, d11ucode56sz);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 56\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 49)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_ucode_write(wlc_hw, d11ucode49, d11ucode49sz);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 49\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 48)) {
		if (WLCISACPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode48, d11ucode48sz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 48\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 45) ||
	           D11REV_IS(wlc_hw->corerev, 47) ||
	           D11REV_IS(wlc_hw->corerev, 51) ||
	           D11REV_IS(wlc_hw->corerev, 52)) {
		if (WLCISACPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode47, d11ucode47sz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 46\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 54)) {
		if (WLCISACPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode54, d11ucode54sz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 54\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 46)) {
		if (WLCISACPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode46, d11ucode46sz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 46\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 43)) {
		if (WLCISACPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode43, d11ucode43sz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 42\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 42)) {
		if (WLCISACPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode42, d11ucode42sz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 42\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 41) || D11REV_IS(wlc_hw->corerev, 44)) {
		if (WLCISACPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode41, d11ucode41sz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 40)) {
		if (WLCISACPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode40, d11ucode40sz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 40\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 39)) {
		if (WLCISLCN20PHY(wlc_hw->band)) {
			wlc_ucode_write_byte(wlc_hw, d11ucode39_lcn20, d11ucode39_lcn20sz);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 38)) {
		if (WLCISLCN40PHY(wlc_hw->band)) {
			wlc_ucode_write_byte(wlc_hw, d11ucode38_lcn40, d11ucode38_lcn40sz);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 37)) {
		if (WLCISLCN40PHY(wlc_hw->band)) {
			wlc_ucode_write_byte(wlc_hw, d11ucode37_lcn40, d11ucode37_lcn40sz);
		} else if (WLCISNPHY(wlc_hw->band)) {
			wlc_ucode_write(wlc_hw, d11ucode31_mimo,
				d11ucode31_mimosz);
		} else {
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
		}
	} else if (D11REV_IS(wlc_hw->corerev, 34)) {
		if (WLCISNPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode34_mimo, d11ucode34_mimosz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 34d\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 33)) {
		if (WLCISLCN40PHY(wlc_hw->band)) {
			wlc_ucode_write_byte(wlc_hw, d11ucode33_lcn40, d11ucode33_lcn40sz);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 32)) {
		if (WLCISNPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode32_mimo, d11ucode32_mimosz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev 32d\n",
				__FUNCTION__, wlc_hw->unit));
	} else if (D11REV_IS(wlc_hw->corerev, 31)) {
		if (WLCISNPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode29_mimo, d11ucode29_mimosz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 30)) {
		if (WLCISNPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode30_mimo, d11ucode30_mimosz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 29)) {
		if (WLCISHTPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode29_mimo, d11ucode29_mimosz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 26)) {
		if (WLCISHTPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode26_mimo, d11ucode26_mimosz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 25) || D11REV_IS(wlc_hw->corerev, 28)) {
		if (WLCISNPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode25_mimo, d11ucode25_mimosz);
		else if (WLCISLCNPHY(wlc_hw->band))
			wlc_ucode_write_byte(wlc_hw, d11ucode25_lcn, d11ucode25_lcnsz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 24)) {
		if (WLCISLCNPHY(wlc_hw->band))
			wlc_ucode_write_byte(wlc_hw, d11ucode24_lcn,
			                     d11ucode24_lcnsz);
		else if (WLCISNPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode24_mimo, d11ucode24_mimosz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 23)) {
		/* ucode only supports rev23(43224b0) with rev16 ucode */
		if (WLCISNPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode16_mimo, d11ucode16_mimosz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else if (D11REV_IS(wlc_hw->corerev, 22)) {
		if (WLCISNPHY(wlc_hw->band))
			wlc_ucode_write(wlc_hw, d11ucode22_mimo, d11ucode22_mimosz);
		else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
			          __FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	} else {
		WL_ERROR(("wl%d: %s: corerev %d is invalid\n", wlc_hw->unit,
			__FUNCTION__, wlc_hw->corerev));
	}
#endif /* defined(UCODE_IN_ROM_SUPPORT) && !defined(ULP_DS1ROM_DS0RAM) */
	wlc_hw->ucode_loaded = TRUE;
	/* it's done for NIC case for coming back from wowl to p2p */
	wlc_bmac_autod11_shm_upd(wlc_hw, D11_IF_SHM_STD);
done:
	return err;
} /* wlc_ucode_download */
#endif /* BCMUCDOWNLOAD */

#if !defined(UCODE_IN_ROM_SUPPORT) || defined(ULP_DS1ROM_DS0RAM)

static void
wlc_ucode_write(wlc_hw_info_t *wlc_hw, const uint32 ucode[], const uint nbytes)
{
	osl_t *osh = wlc_hw->osh;
	d11regs_t *regs = wlc_hw->regs;
	uint i;
	uint count;

	WL_TRACE(("wl%d: wlc_ucode_write\n", wlc_hw->unit));

	ASSERT(ISALIGNED(nbytes, sizeof(uint32)));

	count = (nbytes/sizeof(uint32));

	if (ucode_chunk == 0) {
		W_REG(osh, &regs->objaddr, (OBJADDR_AUTO_INC | OBJADDR_UCM_SEL));
		(void)R_REG(osh, &regs->objaddr);
	}
	for (i = 0; i < count; i++)
		W_REG(osh, &regs->objdata, ucode[i]);
#ifdef BCMUCDOWNLOAD
	ucode_chunk++;
#endif
}

static void
BCMINITFN(wlc_ucode_write_byte)(wlc_hw_info_t *wlc_hw, const uint8 ucode[], const uint nbytes)
{
	osl_t *osh = wlc_hw->osh;
	d11regs_t *regs = wlc_hw->regs;
	uint i;
	uint32 ucode_word;

	WL_TRACE(("wl%d: wlc_ucode_write\n", wlc_hw->unit));

	if (ucode_chunk == 0)
		W_REG(osh, &regs->objaddr, (OBJADDR_AUTO_INC | OBJADDR_UCM_SEL));
	for (i = 0; i < nbytes; i += 7) {
		ucode_word = ucode[i+3] << 24;
		ucode_word = ucode_word | (ucode[i+4] << 16);
		ucode_word = ucode_word | (ucode[i+5] << 8);
		ucode_word = ucode_word | (ucode[i+6] << 0);
		W_REG(osh, &regs->objdata, ucode_word);

		ucode_word = ucode[i+0] << 16;
		ucode_word = ucode_word | (ucode[i+1] << 8);
		ucode_word = ucode_word | (ucode[i+2] << 0);
		W_REG(osh, &regs->objdata, ucode_word);
	}
#ifdef BCMUCDOWNLOAD
	ucode_chunk++;
#endif
}

#endif /* !defined(UCODE_IN_ROM_SUPPORT) || defined(ULP_DS1ROM_DS0RAM) */
#ifdef WLRSDB
static void
wlc_bmac_rsdb_write_inits(wlc_hw_info_t *wlc_hw, const d11init_t *common_inits,
	const d11init_t *core1_inits)
{
	/* For RSDB chips, download common initvals d11ac12bsinitvals50
	 * for both cores. Later download the core-1 specific initvals
	 * d11ac12bsinitvals50core1 if macunit is 1 which will overwrite
	 * the initvals d11ac12bsinitvals50 in some places.
	 */

	if (wlc_bmac_rsdb_cap(wlc_hw)) {
		if (WLCISACPHY(wlc_hw->band)) {
			wlc_write_inits(wlc_hw, common_inits);

			/* If it is core-1, write core-1 inits */
			if (wlc_hw->macunit)
				wlc_write_inits(wlc_hw, core1_inits);
		} else
			WL_ERROR(("%s: wl%d: unsupported phy in corerev %d\n",
				__FUNCTION__, wlc_hw->unit, wlc_hw->corerev));
	}
}
#endif /* WLRSDB */

static void
wlc_write_inits(wlc_hw_info_t *wlc_hw, const d11init_t *inits)
{
	int i;
	osl_t *osh = wlc_hw->osh;
	volatile uint8 *base;

	WL_TRACE(("wl%d: wlc_write_inits\n", wlc_hw->unit));

	base = (volatile uint8*)wlc_hw->regs;

	for (i = 0; inits[i].addr != 0xffff; i++) {
		uint offset_val = 0;
		ASSERT((inits[i].size == 2) || (inits[i].size == 4));

		if (inits[i].addr == D11CORE_TEMPLATE_REG_OFFSET) {
			/* wlc_hw->templatebase is the template base address for core 1/0
			 * For core-0 it is zero and for core 1 it contains the core-1
			 * template offset.
			 */
			offset_val = wlc_hw->templatebase;
		}
		if (inits[i].size == 2)
			W_REG(osh, (uint16*)(uintptr)(base+inits[i].addr), inits[i].value +
			offset_val);
		else if (inits[i].size == 4)
			W_REG(osh, (uint32*)(uintptr)(base+inits[i].addr), inits[i].value +
			offset_val);
	}
}

int
wlc_bmac_wowlucode_start(wlc_hw_info_t *wlc_hw)
{
	d11regs_t *regs;
	uint32 mctrl = (MCTL_IHR_EN | MCTL_INFRA | MCTL_PSM_RUN | MCTL_WAKE);
	regs = wlc_hw->regs;

	/* let the PSM run to the suspended state, set mode to BSS STA */
	SET_MACINTSTATUS(wlc_hw->osh, regs, -1);
	wlc_bmac_mctrl(wlc_hw, ~0, (mctrl | MCTL_PSM_JMP_0));
	wlc_bmac_mctrl(wlc_hw, ~0, mctrl);

	if (ISSIM_ENAB(wlc_hw->sih)) {
		int count = 0;
		WL_TRACE(("spin waiting for MI_MACSSPNDD..\n"));
		/* wait for ucode to self-suspend after auto-init */
		SPINWAIT(((R_REG(wlc_hw->osh, &regs->macintstatus) & MI_MACSSPNDD) == 0), 1);
		while (1) {
			if ((R_REG(wlc_hw->osh, &regs->macintstatus) & MI_MACSSPNDD) == 0) {
				count++;
				WL_TRACE(("Further waiting...%d\n", count));
			} else {
				WL_TRACE(("Further waiting done in %d\n", count));
				break;
			}
			if (count > 1000) {
				break;
			}
			OSL_DELAY(10); // 10usec
		}
	} else {
		/* wait for ucode to self-suspend after auto-init */
		SPINWAIT(((R_REG(wlc_hw->osh, &regs->macintstatus) & MI_MACSSPNDD) == 0),
			1000 * 1000);
	}

	if ((R_REG(wlc_hw->osh, &regs->macintstatus) & MI_MACSSPNDD) == 0) {
		WL_ERROR(("wl%d: wlc_coreinit: ucode did not self-suspend!\n", wlc_hw->unit));
		WL_HEALTH_LOG(wlc_hw->wlc, MACSPEND_WOWL_TIMOUT);
		return BCME_ERROR;
	}

	return BCME_OK;
}
#ifdef WOWL
void
wlc_bmac_wowl_config_4331_5GePA(wlc_hw_info_t *wlc_hw, bool is_5G, bool is_4331_12x9)
{
	si_chipcontrl_epa4331(wlc_hw->sih, FALSE);

	if (!is_4331_12x9) {
		si_chipcontrl_epa4331(wlc_hw->sih, TRUE);
		return;
	}

	si_chipcontrl_epa4331_wowl(wlc_hw->sih, TRUE);

	if (is_5G) {
		wlc_hw->band->mhfs[MHF1] |= MHF1_4331EPA_WAR;
		wlc_write_mhf(wlc_hw, wlc_hw->band->mhfs);

		/* give the control to ucode */
		si_gpiocontrol(wlc_hw->sih, GPIO_2_PA_CTRL_5G_0, GPIO_2_PA_CTRL_5G_0,
			GPIO_DRV_PRIORITY);
		/* drive the output to 0 and ucode will drive to 1 */
		si_gpioout(wlc_hw->sih, GPIO_2_PA_CTRL_5G_0, 0, GPIO_DRV_PRIORITY);
		/* set default PA disable.  Ucode will toggle this at start of tx */
		si_gpioouten(wlc_hw->sih, GPIO_2_PA_CTRL_5G_0, GPIO_2_PA_CTRL_5G_0,
			GPIO_DRV_PRIORITY);
	}
}

/* External API to write the ucode to avoid exposing the details */

#define BOARD_GPIO_3_WOWL 0x8 /* bit mask of 3rd pin */

#ifdef WOWL_GPIO
static bool
wlc_bmac_wowl_config_hw(wlc_hw_info_t *wlc_hw)
{
	/* configure the gpio etc to inform host to wake up etc */

	WL_TRACE(("wl: %s: corerev = 0x%x boardtype = 0x%x\n",  __FUNCTION__,
		wlc_hw->corerev, wlc_hw->sih->boardtype));

	if (!wlc_hw->clk) {
		WL_ERROR(("wl: %s: No hw clk \n",  __FUNCTION__));
		return FALSE;
	}

	if (BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) {
		if ((CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM43431_CHIP_ID)) {
				WL_INFORM(("wl%d: %s: set mux pin to SROM\n",
				           wlc_hw->unit, __FUNCTION__));
				/* force muxed pin to control ePA */
				si_chipcontrl_epa4331(wlc_hw->sih, FALSE);
				/* Apply WAR to enable 2G ePA and force muxed pin to SROM */
				si_chipcontrl_epa4331_wowl(wlc_hw->sih, TRUE);
		} else if (BCM43602_CHIP(wlc_hw->sih->chip) ||
			(((CHIPID(wlc_hw->sih->chip) == BCM4360_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM43460_CHIP_ID) ||
			(CHIPID(wlc_hw->sih->chip) == BCM4352_CHIP_ID)) &&
			(CHIPREV(wlc_hw->sih->chiprev) <= 2))) {
			si_chipcontrl_srom4360(wlc_hw->sih, TRUE);
		}
	}

	si_gpiocontrol(wlc_hw->sih, 1 << wlc_hw->wowl_gpio, 0, GPIO_DRV_PRIORITY);
	si_gpioout(wlc_hw->sih, 1 << wlc_hw->wowl_gpio,
		wlc_hw->wowl_gpiopol << wlc_hw->wowl_gpio, GPIO_DRV_PRIORITY);
	si_gpioouten(wlc_hw->sih, 1 << wlc_hw->wowl_gpio,
		1 << wlc_hw->wowl_gpio, GPIO_DRV_PRIORITY);

	return TRUE;
}
#endif /* WOWL_GPIO */


int
wlc_bmac_wowlucode_init(wlc_hw_info_t *wlc_hw)
{
#ifdef WOWL_GPIO
	wlc_bmac_wowl_config_hw(wlc_hw);
#endif

	if (!wlc_hw->clk) {
		WL_ERROR(("wl: %s: No hw clk \n",  __FUNCTION__));
		return BCME_ERROR;
	}

	/* Reset ucode. PSM_RUN is needed because current PC is not going to be 0 */
	wlc_bmac_mctrl(wlc_hw, ~0, (MCTL_IHR_EN | MCTL_PSM_JMP_0 | MCTL_PSM_RUN));

	return BCME_OK;
}
int
wlc_bmac_write_inits(wlc_hw_info_t *wlc_hw, void *inits, int len)
{
	BCM_REFERENCE(len);

	wlc_write_inits(wlc_hw, inits);

	return BCME_OK;
}

int
wlc_bmac_wakeucode_dnlddone(wlc_hw_info_t *wlc_hw)
{
	d11regs_t *regs;
	regs = wlc_hw->regs;

#ifdef BCMULP
	/* Switch to point to ulp SHMs */
	wlc_bmac_autod11_shm_upd(wlc_hw, D11_IF_SHM_ULP);
#else /* BCMULP */
	/* Switch to point to wowl SHMs */
	wlc_bmac_autod11_shm_upd(wlc_hw, D11_IF_SHM_WOWL);
#endif /* BCMULP */

	/* tell the ucode the corerev */
	wlc_bmac_write_shm(wlc_hw, M_MACHW_VER(wlc_hw), (uint16)wlc_hw->corerev);

	/* overwrite default long slot timing */
	if (wlc_hw->shortslot)
		wlc_bmac_update_slot_timing(wlc_hw, wlc_hw->shortslot);

	/* write rate fallback retry limits */
	wlc_bmac_write_shm(wlc_hw, M_SFRMTXCNTFBRTHSD(wlc_hw), wlc_hw->SFBL);
	wlc_bmac_write_shm(wlc_hw, M_LFRMTXCNTFBRTHSD(wlc_hw), wlc_hw->LFBL);

	/* Restore the hostflags */
	wlc_write_mhf(wlc_hw, wlc_hw->band->mhfs);

	/* make sure we can still talk to the mac */
	ASSERT(R_REG(wlc_hw->osh, &regs->maccontrol) != 0xffffffff);
	UNUSED_PARAMETER(regs);

	wlc_bmac_mctrl(wlc_hw, MCTL_DISCARD_PMQ, MCTL_DISCARD_PMQ);

	wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);

	wlc_bmac_upd_synthpu(wlc_hw);

#ifdef WOWL_GPIO
	OR_REG(wlc_hw->osh, &wlc_hw->regs->psm_gpio_oe, 1 << wlc_hw->wowl_gpio);
	OR_REG(wlc_hw->osh, &wlc_hw->regs->psm_gpio_out, wlc_hw->wowl_gpiopol << wlc_hw->wowl_gpio);

	si_gpiocontrol(wlc_hw->sih, 1 << wlc_hw->wowl_gpio, 1 << wlc_hw->wowl_gpio,
			GPIO_DRV_PRIORITY);

#endif /* WOWL_GPIO */
	return BCME_OK;
}
#endif /* WOWL */

#ifdef SAMPLE_COLLECT
/**
 * Load sample collect ucode
 * Ucode inits the SHM and all MAC regs
 * can support all PHY types, implement NPHY for now.
 */
static void
wlc_ucode_sample_init_rev(wlc_hw_info_t *wlc_hw, const uint32 ucode[], const uint nbytes)
{
	if (WLCISNPHY(wlc_hw->band)) {
	  /* Restart the ucode (recover from wl out) */
		wlc_bmac_mctrl(wlc_hw, ~0, (MCTL_IHR_EN | MCTL_PSM_RUN | MCTL_EN_MAC));
		return;
	}

	/* Reset ucode. PSM_RUN is needed because current PC is not going to be 0 */
	wlc_bmac_mctrl(wlc_hw, ~0, (MCTL_IHR_EN | MCTL_PSM_JMP_0 | MCTL_PSM_RUN));

	/* Load new d11ucode */
#if !defined(UCODE_IN_ROM_SUPPORT) || defined(ULP_DS1ROM_DS0RAM)
	wlc_ucode_write(wlc_hw, ucode, nbytes);
#endif /* !defined(UCODE_IN_ROM_SUPPORT) || defined(ULP_DS1ROM_DS0RAM) */

	(void) wlc_bmac_wowlucode_start(wlc_hw);

	/* make sure we can still talk to the mac */
	ASSERT(R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) != 0xffffffff);
}

void
wlc_ucode_sample_init(wlc_hw_info_t *wlc_hw)
{
	wlc_ucode_sample_init_rev(wlc_hw, d11sampleucode16, d11sampleucode16sz);
}
#endif	/* SAMPLE_COLLECT */

static void
wlc_ucode_txant_set(wlc_hw_info_t *wlc_hw)
{
	uint16 phyctl;
	uint16 phytxant = wlc_hw->bmac_phytxant;
	uint16 mask = PHY_TXC_ANT_MASK;

	if (D11REV_GE(wlc_hw->corerev, 40)) {
		WL_INFORM(("wl%d: %s: need rev40 update\n", wlc_hw->unit, __FUNCTION__));
		return;
	}


	/* set the Probe Response frame phy control word */
	phyctl = wlc_bmac_read_shm(wlc_hw, M_CTXPRS_BLK(wlc_hw) + C_CTX_PCTLWD_POS);
	phyctl = (phyctl & ~mask) | phytxant;
	wlc_bmac_write_shm(wlc_hw, M_CTXPRS_BLK(wlc_hw) + C_CTX_PCTLWD_POS, phyctl);

	/* set the Response (ACK/CTS) frame phy control word */
	phyctl = wlc_bmac_read_shm(wlc_hw, M_RSP_PCTLWD(wlc_hw));
	phyctl = (phyctl & ~mask) | phytxant;
	wlc_bmac_write_shm(wlc_hw, M_RSP_PCTLWD(wlc_hw), phyctl);
}

void
wlc_bmac_txant_set(wlc_hw_info_t *wlc_hw, uint16 phytxant)
{
	/* update sw state */
	wlc_hw->bmac_phytxant = phytxant;

	/* push to ucode if up */
	if (!wlc_hw->up)
		return;
	wlc_ucode_txant_set(wlc_hw);

}

uint16
wlc_bmac_get_txant(wlc_hw_info_t *wlc_hw)
{
	return (uint16)wlc_hw->wlc->stf->txant;
}

void
wlc_bmac_antsel_type_set(wlc_hw_info_t *wlc_hw, uint8 antsel_type)
{
	wlc_hw->antsel_type = antsel_type;

	/* Update the antsel type for phy module to use */
	wlc_phy_antsel_type_set(wlc_hw->band->pi, antsel_type);
}

void
wlc_bmac_fifoerrors(wlc_hw_info_t *wlc_hw)
{
	bool fatal = FALSE;
	uint unit;
	uint intstatus, idx;
	d11regs_t *regs = wlc_hw->regs;

	unit = wlc_hw->unit;
	BCM_REFERENCE(unit);

	for (idx = 0; idx < NFIFO; idx++) {
		/* read intstatus register and ignore any non-error bits */
		intstatus = R_REG(wlc_hw->osh, &regs->intctrlregs[idx].intstatus) & I_ERRORS;
		if (!intstatus)
			continue;

		WL_TRACE(("wl%d: wlc_bmac_fifoerrors: intstatus%d 0x%x\n", unit, idx, intstatus));

		if (intstatus & I_RO) {
			WL_ERROR(("wl%d: fifo %d: receive fifo overflow\n", unit, idx));
			WLCNTINCR(wlc_hw->wlc->pub->_cnt->rxoflo);
			fatal = TRUE;
		}

		if (intstatus & I_PC) {
			WL_ERROR(("wl%d: fifo %d: descriptor error\n", unit, idx));
			WLCNTINCR(wlc_hw->wlc->pub->_cnt->dmade);
			fatal = TRUE;
		}

		if (intstatus & I_PD) {
			WL_ERROR(("wl%d: fifo %d: data error\n", unit, idx));
			WLCNTINCR(wlc_hw->wlc->pub->_cnt->dmada);
			fatal = TRUE;
		}

		if (intstatus & I_DE) {
			WL_ERROR(("wl%d: fifo %d: descriptor protocol error\n", unit, idx));
			WLCNTINCR(wlc_hw->wlc->pub->_cnt->dmape);
			fatal = TRUE;
		}

		if (intstatus & I_RU) {
			WL_ERROR(("wl%d: fifo %d: receive descriptor underflow\n", unit, idx));
			WLCNTINCR(wlc_hw->wlc->pub->_cnt->rxuflo[idx]);
		}

		if (intstatus & I_XU) {
			WL_ERROR(("wl%d: fifo %d: transmit fifo underflow\n", idx, unit));
			WLCNTINCR(wlc_hw->wlc->pub->_cnt->txuflo);
			fatal = TRUE;
		}


		if (fatal) {
			WLC_EXTLOG(wlc_hw->wlc, LOG_MODULE_COMMON, FMTSTR_FATAL_ERROR_ID,
				WL_LOG_LEVEL_ERR, 0, intstatus, NULL);
			WL_HEALTH_LOG(wlc_hw->wlc, DESCRIPTOR_ERROR);
			wlc_fatal_error(wlc_hw->wlc);	/* big hammer */
			break;
		} else {
			W_REG(wlc_hw->osh, &regs->intctrlregs[idx].intstatus, intstatus);
		}
	}
} /* wlc_bmac_fifoerrors */

/**
 * callback for siutils.c, which has only wlc handler, no wl
 * they both check up, not only because there is no need to off/restore d11 interrupt
 *  but also because per-port code may require sync with valid interrupt.
 */
static uint32
wlc_wlintrsoff(wlc_hw_info_t *wlc_hw)
{
	if (!wlc_hw->up)
		return 0;

	return wl_intrsoff(wlc_hw->wlc->wl);
}

static void
wlc_wlintrsrestore(wlc_hw_info_t *wlc_hw, uint32 macintmask)
{
	if (!wlc_hw->up)
		return;

	wl_intrsrestore(wlc_hw->wlc->wl, macintmask);
}


void
wlc_bmac_mute(wlc_hw_info_t *wlc_hw, bool on, mbool flags)
{
#define MUTE_DATA_FIFO	TX_DATA_FIFO
	if (on) {
		/* suspend tx fifos */
		wlc_bmac_tx_fifo_suspend(wlc_hw, MUTE_DATA_FIFO);
		wlc_bmac_tx_fifo_suspend(wlc_hw, TX_CTL_FIFO);
		wlc_bmac_tx_fifo_suspend(wlc_hw, TX_AC_BK_FIFO);
#ifdef WME
		wlc_bmac_tx_fifo_suspend(wlc_hw, TX_AC_VI_FIFO);
#endif /* WME */
#ifdef AP
		wlc_bmac_tx_fifo_suspend(wlc_hw, TX_BCMC_FIFO);
#endif /* AP */
#if defined(MBSS) || defined(WLAIBSS)
		wlc_bmac_tx_fifo_suspend(wlc_hw, TX_ATIM_FIFO);
#endif /* defined(MBSS) || defined(WLAIBSS) */

		/* clear the address match register so we do not send ACKs */
		wlc_bmac_clear_match_mac(wlc_hw);
	} else {
		/* resume tx fifos */
		if (!wlc_hw->wlc->tx_suspended) {
			wlc_bmac_tx_fifo_resume(wlc_hw, MUTE_DATA_FIFO);
		}
		wlc_bmac_tx_fifo_resume(wlc_hw, TX_CTL_FIFO);
		wlc_bmac_tx_fifo_resume(wlc_hw, TX_AC_BK_FIFO);
#ifdef WME
		wlc_bmac_tx_fifo_resume(wlc_hw, TX_AC_VI_FIFO);
#endif /* WME */
#ifdef AP
		wlc_bmac_tx_fifo_resume(wlc_hw, TX_BCMC_FIFO);
#endif /* AP */
#if defined(MBSS) || defined(WLAIBSS)
		wlc_bmac_tx_fifo_resume(wlc_hw, TX_ATIM_FIFO);
#endif /* defined(MBSS) || defined(WLAIBSS) */

		/* Restore address */
		wlc_bmac_set_match_mac(wlc_hw, &wlc_hw->etheraddr);
	}

	wlc_phy_mute_upd(wlc_hw->band->pi, on, flags);

	if (on)
		wlc_ucode_mute_override_set(wlc_hw);
	else
		wlc_ucode_mute_override_clear(wlc_hw);
} /* wlc_bmac_mute */

void
wlc_bmac_set_deaf(wlc_hw_info_t *wlc_hw, bool user_flag)
{
	wlc_phy_set_deaf(wlc_hw->band->pi, user_flag);
}


void
wlc_bmac_filter_war_upd(wlc_hw_info_t *wlc_hw, bool set)
{
	wlc_phy_set_filt_war(wlc_hw->band->pi, set);
}

int
wlc_bmac_xmtfifo_sz_get(wlc_hw_info_t *wlc_hw, uint fifo, uint *blocks)
{
	if (fifo >= NFIFO) {
		WL_ERROR(("wl%d: %s: Out of range fifo:%d\n", wlc_hw->unit, __FUNCTION__, fifo));
		return BCME_RANGE;
	}

	*blocks = wlc_hw->xmtfifo_sz[fifo];

	return 0;
}

int
wlc_bmac_xmtfifo_sz_set(wlc_hw_info_t *wlc_hw, uint fifo, uint16 blocks)
{
	if (fifo >= NFIFO || blocks > 299) {
		WL_ERROR(("wl%d: %s: fifo pair count %d or blocks %d not in range",
			wlc_hw->unit, __FUNCTION__, fifo, blocks));
		return BCME_RANGE;
	}

	wlc_hw->xmtfifo_sz[fifo] = blocks;

#ifdef WLAMPDU_HW
	if (fifo < AC_COUNT) {
		wlc_hw->xmtfifo_frmmax[fifo] =
			(wlc_hw->xmtfifo_sz[fifo] * 256 - 1300)	/ MAX_MPDU_SPACE;
		WL_INFORM(("%s: fifo sz blk %d entries %d\n",
			__FUNCTION__, wlc_hw->xmtfifo_sz[fifo], wlc_hw->xmtfifo_frmmax[fifo]));
	}
#endif

	return 0;
}

/**
 * Check the MAC's tx suspend status for a tx fifo.
 *
 * When the MAC acknowledges a tx suspend, it indicates that no more packets will be transmitted out
 * the radio. This is independent of DMA channel suspension---the DMA may have finished suspending,
 * or may still be pulling data into a tx fifo, by the time the MAC acks the suspend request.
 */
bool
wlc_bmac_tx_fifo_suspended(wlc_hw_info_t *wlc_hw, uint tx_fifo)
{
	/* check that a suspend has been requested and is no longer pending */
	if (!PIO_ENAB_HW(wlc_hw)) {
		/*
		 * for DMA mode, the suspend request is set in xmtcontrol of the DMA engine,
		 * and the tx fifo suspend at the lower end of the MAC is acknowledged in the
		 * chnstatus register.
		 * The tx fifo suspend completion is independent of the DMA suspend completion and
		 *   may be acked before or after the DMA is suspended.
		 */
		if (dma_txsuspended(wlc_hw->di[tx_fifo]) &&
		    (R_REG(wlc_hw->osh, &wlc_hw->regs->chnstatus) &
			(1<<tx_fifo)) == 0)
			return TRUE;
	} else {
		if (wlc_pio_txsuspended(wlc_hw->pio[tx_fifo]))
			return TRUE;
	}

	return FALSE;
}

void
wlc_bmac_tx_fifo_suspend(wlc_hw_info_t *wlc_hw, uint tx_fifo)
{
	uint8 fifo = 1 << tx_fifo;

	/* Two clients of this code, 11h Quiet period and scanning. */

	/* only suspend if not already suspended */
	if ((wlc_hw->suspended_fifos & fifo) == fifo)
		return;

	/* force the core awake only if not already */
	wlc_upd_suspended_fifos_set(wlc_hw, tx_fifo);

	if (!PIO_ENAB_HW(wlc_hw)) {
		if (wlc_hw->di[tx_fifo]) {
			bool suspend;

			/* Suspending AMPDU transmissions in the middle can cause underflow
			 * which may result in mismatch between ucode and driver
			 * so suspend the mac before suspending the FIFO
			 */
			suspend = !(R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) & MCTL_EN_MAC);

			if (WLC_PHY_11N_CAP(wlc_hw->band) && !suspend)
				wlc_bmac_suspend_mac_and_wait(wlc_hw);

			/* after dma tx suspend for a specifc channel , before going for txresume
			  *  corresponding chnstatus bit needs to checked for zero
			  *  else it could have some bad  impact on dma engine
			 */
			dma_txsuspend(wlc_hw->di[tx_fifo]);

			if (D11REV_GE(wlc_hw->corerev, 40)) {
				if (!wlc_bmac_istxsuspend(wlc_hw, tx_fifo)) {
#ifdef WLUCODE_DBG
					wlc_bmac_ucode_dump_die_on_error(wlc_hw);
#endif
				}
			}

			if (WLC_PHY_11N_CAP(wlc_hw->band) && !suspend)
				wlc_bmac_enable_mac(wlc_hw);
		}
	} else {
		wlc_pio_txsuspend(wlc_hw->pio[tx_fifo]);
	}
}

void
wlc_bmac_tx_fifo_resume(wlc_hw_info_t *wlc_hw, uint tx_fifo)
{
	/* BMAC_NOTE: WLC_TX_FIFO_ENAB is done in wlc_dpc() for DMA case but need to be done
	 * here for PIO otherwise the watchdog will catch the inconsistency and fire
	 */
	/* Two clients of this code, 11h Quiet period and scanning. */
	if (!PIO_ENAB_HW(wlc_hw)) {
		if (wlc_hw->di[tx_fifo])
			dma_txresume(wlc_hw->di[tx_fifo]);
	} else {
		wlc_pio_txresume(wlc_hw->pio[tx_fifo]);
	}

	/* allow core to sleep again */
	wlc_upd_suspended_fifos_clear(wlc_hw, tx_fifo);
}

static void wlc_bmac_service_txstatus(wlc_hw_info_t *wlc_hw);
static void wlc_bmac_flush_tx_fifos(wlc_hw_info_t *wlc_hw, uint fifo_bitmap);
static void wlc_bmac_uflush_tx_fifos(wlc_hw_info_t *wlc_hw, uint fifo_bitmap);
static void wlc_bmac_enable_tx_fifos(wlc_hw_info_t *wlc_hw, uint fifo_bitmap);

#if defined(WLAMPDU_MAC)
extern void wlc_sidechannel_init(wlc_info_t *wlc);
#endif

/** Enable new method of suspend and flush. Requires minimum ucode BOM 622.1. */
#define NEW_SUSPEND_FLUSH_UCODE 1

/**
 * Called during e.g. joining, excursion, channel switch. Suspends ucode, flushes all tx packets in
 * a caller provided set of hardware fifo's, waits for flush complete, processes all tx statuses,
 * removes remaining packets from the DMA ring and notifies upper layer that the flush completed.
 */
void
wlc_bmac_tx_fifo_sync(wlc_hw_info_t *wlc_hw, uint fifo_bitmap, uint8 flag)
{
#ifdef NEW_SUSPEND_FLUSH_UCODE
	/* halt any tx processing by ucode */
	wlc_bmac_suspend_mac_and_wait(wlc_hw);

	if (((CHIPID(wlc_hw->sih->chip)) == BCM5357_CHIP_ID) ||
	    ((CHIPID(wlc_hw->sih->chip)) == BCM53572_CHIP_ID))
		wlc_bmac_mctrl(wlc_hw, MCTL_PSM_RUN, MCTL_PSM_RUN);

	/* clear the hardware fifos */
	wlc_bmac_flush_tx_fifos(wlc_hw, fifo_bitmap);

	if (((CHIPID(wlc_hw->sih->chip)) == BCM5357_CHIP_ID) ||
	    ((CHIPID(wlc_hw->sih->chip)) == BCM53572_CHIP_ID))
	    wlc_bmac_mctrl(wlc_hw, MCTL_PSM_RUN, 0);

	/* process any frames that made it out before the suspend */
	wlc_bmac_service_txstatus(wlc_hw);

	/* allow ucode to run again */
	wlc_bmac_enable_mac(wlc_hw);

#if defined(WLAMPDU_MAC)
	if (AMPDU_UCODE_ENAB(wlc_hw->wlc->pub))
		wlc_sidechannel_init(wlc_hw->wlc);
#endif

#else
	bool suspend;

	/* enable MAC only if currently suspended */
	suspend = !(R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) & MCTL_EN_MAC);
	if (suspend)
		wlc_bmac_enable_mac(wlc_hw);

	/* clear the hardware fifos */
	wlc_bmac_flush_tx_fifos(wlc_hw, fifo_bitmap);

	/* put MAC back into suspended state if required */
	if (suspend)
		wlc_bmac_suspend_mac_and_wait(wlc_hw);

	/* process any frames that made it out before the suspend */
	wlc_bmac_service_txstatus(wlc_hw);

#endif /* NEW_SUSPEND_FLUSH_UCODE */

	/* signal to the upper layer that the fifos are flushed
	 * and any tx packet statuses have been returned
	 */
	wlc_tx_fifo_sync_complete(wlc_hw->wlc, fifo_bitmap, flag);

	/* reenable the fifos once the completion has been signaled */
	wlc_bmac_enable_tx_fifos(wlc_hw, fifo_bitmap);
} /* wlc_bmac_tx_fifo_sync */

static void
wlc_bmac_service_txstatus(wlc_hw_info_t *wlc_hw)
{
	bool fatal = FALSE;

	wlc_bmac_txstatus(wlc_hw, FALSE, &fatal);
}

#define BMC_IN_PROG_CHK_ENAB 0x8000

static void
wlc_bmac_uflush_tx_fifos(wlc_hw_info_t *wlc_hw, uint fifo_bitmap)
{
	uint i;
	uint chnstatus, status;
	uint count;
	osl_t *osh = wlc_hw->osh;
	uint fbmp;
	d11regs_t *regs;
	dma64regs_t *d64regs;
	bool fastclk;

	regs = wlc_hw->regs;

	/* request FAST clock if not on */
	if (!(fastclk = wlc_hw->forcefastclk)) {
		wlc_clkctl_clk(wlc_hw, CLK_FAST);
	}

	/* step 1. request dma suspend fifos */
	/* Do this one DMA at a time, suspending all in a loop causes
	   trouble for dongle drivers...
	*/
	for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
		if ((fbmp & 0x01) == 0)
			continue;

		//no need to update wlc_hw->suspended_fifos cause dma_txsuspend is called hereafter
		dma_txsuspend(wlc_hw->di[i]);
		/* ucode starts flushing now */
		wlc_bmac_write_shm(wlc_hw, M_TXFL_BMAP(wlc_hw), (1 << i));

		d64regs = &regs->f64regs[i].dmaxmt;
		count = 0;
		while (count < (80 * 1000)) {
			chnstatus = R_REG(osh, &regs->chnstatus);
			status = R_REG(osh, &d64regs->status0) & D64_XS0_XS_MASK; /* tX Status */
			if ((wlc_bmac_read_shm(wlc_hw, M_TXFL_BMAP(wlc_hw)) == 0) &&
			    chnstatus == 0 && status == D64_XS0_XS_IDLE)
				break;
			OSL_DELAY(10);
			count += 10;
		}
		if (chnstatus || (status != D64_XS0_XS_IDLE)) {
			WL_ERROR(("MQ ERROR %s: suspend dma %d not done after %d us: "
				 "chnstatus 0x%04x dma_status 0x%x txefs 0x%04x\n"
				 "BMCReadStatus: 0x%04x AQMFifoReady: 0x%04x\n",
				 __FUNCTION__, i, count, chnstatus, status,
				 R_REG(osh, &regs->u.d11acregs.XmtSuspFlush),
				 R_REG(osh, &regs->u.d11acregs.BMCReadStatus),
				 R_REG(osh, &regs->u.d11acregs.u0.lt64.AQMFifoReady)));
		}
	}

	/* step 4. re-wind dma last ptr to the first desc with EOF from current active index  */
	for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
		if ((fbmp & 0x01) == 0)
			continue;
		dma_txrewind(wlc_hw->di[i]);
	}

	/* step 5. un-suspend dma...and do another ucode flush for "partial" frames */
	/* Again, do this one DMA at a time and not all in a loop */
	for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
		if ((fbmp & 0x01) == 0)
			continue;
		dma_txresume(wlc_hw->di[i]);
		//no need to update wlc_hw->suspended_fifos cause dma_txsuspend is called hereafter
		/* (| BMC_IN_PROG_CHK_ENAB) enables BMC in_prog check in ucode */
		wlc_bmac_write_shm(wlc_hw, M_TXFL_BMAP(wlc_hw), ((1 << i) | BMC_IN_PROG_CHK_ENAB));

		count = 0;
		while (count < (80 * 1000)) {
			chnstatus = wlc_bmac_read_shm(wlc_hw, M_TXFL_BMAP(wlc_hw));
			chnstatus &= ~BMC_IN_PROG_CHK_ENAB;
			if (chnstatus == 0)
				break;
			OSL_DELAY(10);
			count += 10;
		}
		if (chnstatus) {
			WL_ERROR(("MQ ERROR %s: ucode flush 0x%02x not done after %d us: "
				  "M_TXFL_BMAP 0x%04x txefs 0x%04x\n"
				  "BMCReadStatus: 0x%04x AQMFifoReady: 0x%04x DMABusy %04x\n",
				  __FUNCTION__, i, count, chnstatus,
				  R_REG(osh, &regs->u.d11acregs.XmtSuspFlush),
				  R_REG(osh, &regs->u.d11acregs.BMCReadStatus),
				  R_REG(osh, &regs->u.d11acregs.u0.lt64.AQMFifoReady),
				  R_REG(osh, &regs->u.d11acregs.XmtDMABusy)));
		}
	}

	/* step 6: have to suspend dma again. otherwise, frame doesn't show up fifordy */
	for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
		if ((fbmp & 0x01) == 0)
			continue;

		wlc_upd_suspended_fifos_set(wlc_hw, i);
		dma_txsuspend(wlc_hw->di[i]);
		d64regs = &regs->f64regs[i].dmaxmt;
		count = 0;
		while (count < (80 * 1000)) {
			chnstatus = R_REG(osh, &regs->chnstatus);
			status = R_REG(osh, &d64regs->status0) & D64_XS0_XS_MASK;
			if (chnstatus == 0 && status == D64_XS0_XS_IDLE)
				break;
			OSL_DELAY(10);
			count += 10;
		}
		if (chnstatus || (status != D64_XS0_XS_IDLE)) {
			WL_PRINT(("MQ ERROR %s: final suspend dma %d not done after %d us: "
				 "chnstatus 0x%04x dma_status 0x%x txefs 0x%04x\n"
				 "BMCReadStatus: 0x%04x AQMFifoReady: 0x%04x\n",
				 __FUNCTION__, i, count, chnstatus, status,
				 R_REG(osh, &regs->u.d11acregs.XmtSuspFlush),
				 R_REG(osh, &regs->u.d11acregs.BMCReadStatus),
				 R_REG(osh, &regs->u.d11acregs.u0.lt64.AQMFifoReady)));
		}
	}

	if (!fastclk) {
		wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);
	}
} /* wlc_bmac_uflush_tx_fifos */

static void
wlc_bmac_flush_tx_fifos(wlc_hw_info_t *wlc_hw, uint fifo_bitmap)
{
	uint i;
	uint chnstatus;
	uint count;
	osl_t *osh = wlc_hw->osh;
	uint fbmp;
	d11regs_t *regs = wlc_hw->regs;

	/* filter out un-initialized txfifo */
	for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
		if ((fbmp & 0x01) == 0)
			continue;
		if ((!PIO_ENAB_HW(wlc_hw) && wlc_hw->di[i] == NULL))
			fifo_bitmap &= ~(1 << i);
	}

	/* JIRA CRWLDOT11M-1182/1197 WAR */
	if (D11REV_IS(wlc_hw->corerev, 48) || D11REV_IS(wlc_hw->corerev, 49) ||
		D11REV_IS(wlc_hw->corerev, 50) || D11REV_GE(wlc_hw->corerev, 54)) {
		wlc_bmac_uflush_tx_fifos(wlc_hw, fifo_bitmap);
		return;
	}

	if (D11REV_GE(wlc_hw->corerev, 40)) {
		uint status;

		/* set suspend to the requested fifos */
		for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
			if ((fbmp & 0x01) == 0)
				continue;

			wlc_upd_suspended_fifos_set(wlc_hw, i);
			dma_txsuspend(wlc_hw->di[i]);
			/* request ucode flush */
			wlc_bmac_write_shm(wlc_hw, M_TXFL_BMAP(wlc_hw), (uint16)(1 << i));

			/* check chnstatus and ucode flush status */
			count = 0;
			while (count < (80 * 1000)) {
				chnstatus = R_REG(osh, &regs->chnstatus);
				status = wlc_bmac_read_shm(wlc_hw, M_TXFL_BMAP(wlc_hw));
				if (chnstatus == 0 && status == 0)
					break;
				OSL_DELAY(10);
				count += 10;
			}
			if (chnstatus || status) {
				WL_ERROR(("MQ ERROR %s: suspend dma %d not done after %d us: "
					  "chnstatus 0x%04x txfl_bmap 0x%04x txefs 0x%04x\n",
					  __FUNCTION__, i, count, chnstatus, status,
					  R_REG(osh, &regs->u.d11acregs.XmtSuspFlush)));
			}
		}

		for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
			dma64regs_t *d64regs;
			uint status_local;

			/* skip uninterested and empty fifo */
			if ((fbmp & 0x01) == 0)
				continue;

			d64regs = &regs->f64regs[i].dmaxmt;

			/* need to make sure dma has become idle (finish any pending tx) */
			count = 0;
			while (count < (80 * 1000)) {
				status_local = R_REG(osh, &d64regs->status0) & D64_XS0_XS_MASK;
				if (status_local == D64_XS0_XS_IDLE)
					break;
				OSL_DELAY(10);
				count += 10;
			}
			if (status_local != D64_XS0_XS_IDLE) {
				WL_ERROR(("ERROR: dma %d status 0x%x %x doesn't return idle "
					  "after %d us. shm_bmap 0x%04x\n", i, status_local,
					  R_REG(osh, &d64regs->status1), count,
					  wlc_bmac_read_shm(wlc_hw, M_TXFL_BMAP(wlc_hw))));
			}
		}

	}
	/* end WAR 104924 */

	if (!PIO_ENAB_HW(wlc_hw)) {
		for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
			if ((fbmp & 0x01) == 0) /* not the right fifo to process */
				continue;

			wlc_upd_suspended_fifos_set(wlc_hw, i);
			dma_txflush(wlc_hw->di[i]);

			/* wait for flush complete */
			count = 0;
			while (((chnstatus = R_REG(osh, &regs->chnstatus)) & 0xFF00) &&
			       (count < (80 * 1000))) {
				OSL_DELAY(10);
				count += 10;
			}
			if (chnstatus & 0xFF00) {
				WL_ERROR(("MQ ERROR: %s: flush fifo %d timeout after %d us. "
					  "chnstatus 0x%x\n", __FUNCTION__, i, count, chnstatus));
			} else {
				WL_MQ(("MQ: %s: fifo %d waited %d us for success chanstatus 0x%x\n",
				       __FUNCTION__, i, count, chnstatus));
			}
		}

#ifdef WL_MULTIQUEUE_DBG
		/* DBG print */
		chnstatus = R_REG(osh, &regs->chnstatus);
		WL_MQ(("MQ: %s: post flush req chnstatus 0x%x\n", __FUNCTION__,
		       chnstatus));

		for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
			if ((fbmp & 0x01) == 0) /* not the right fifo to process */
				continue;

			dma64regs_t *d64regs = &regs->f64regs[i].dmaxmt;
			status = ((R_REG(osh, &d64regs->status0) & D64_XS0_XS_MASK) >>
			          D64_XS0_XS_SHIFT);
			WL_MQ(("MQ: %s: post flush req dma %d status %u\n", __FUNCTION__,
			       i, status));
		}
#endif /* WL_MULTIQUEUE_DBG */

		/* Clear the dma flush command */
		for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
			if ((fbmp & 0x01) == 0) /* not the right fifo to process */
				continue;

			dma_txflush_clear(wlc_hw->di[i]);
		}

#ifdef WL_MULTIQUEUE_DBG
		/* DBG print */
		for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
			uint status;

			if ((fbmp & 0x01) == 0) /* not the right fifo to process */
				continue;

			dma64regs_t *d64regs = &regs->f64regs[i].dmaxmt;
			status = ((R_REG(osh, &d64regs->status0) & D64_XS0_XS_MASK) >>
		          D64_XS0_XS_SHIFT);
			WL_MQ(("MQ: %s: post flush wait dma %d status %u\n", __FUNCTION__,
			       i, status));
		} /* for */
#endif /* WL_MULTIQUEUE_DBG */
	} else {
		for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
			if ((fbmp & 0x01) == 0) /* not the right fifo to process */
				continue;

			if (wlc_hw->pio[i])
				wlc_pio_reset(wlc_hw->pio[i]);
		} /* for */
	} /* else */
} /* wlc_bmac_flush_tx_fifos */

static void
wlc_bmac_enable_tx_fifos(wlc_hw_info_t *wlc_hw, uint fifo_bitmap)
{
	uint i;
	uint fbmp;

	if (!PIO_ENAB_HW(wlc_hw)) {
		for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
			if ((fbmp & 0x01) == 0) /* not the right fifo to process */
				continue;

			if (wlc_hw->di[i] == NULL)
				continue;
			wlc_bmac_txfifo_reset(wlc_hw, i);
			wlc_upd_suspended_fifos_clear(wlc_hw, i);
			dma_txinit(wlc_hw->di[i]);
		} /* for */
	} else {
		for (i = 0, fbmp = fifo_bitmap; fbmp; i++, fbmp = fbmp >> 1) {
			if ((fbmp & 0x01) == 0) /* not the right fifo to process */
				continue;

			if (wlc_hw->pio[i])
				wlc_pio_reset(wlc_hw->pio[i]);
		} /* for */
	} /* else */
}

static bool BCMFASTPATH
wlc_bmac_dotxstatus(wlc_hw_info_t *wlc_hw, tx_status_t *txs, uint32 s2)
{

	/* discard intermediate indications for ucode with one legitimate case:
	 *   e.g. if "useRTS" is set. ucode did a successful rts/cts exchange, but the subsequent
	 *   tx of DATA failed. so it will start rts/cts from the beginning (resetting the rts
	 *   transmission count)
	 */
	if (D11REV_LT(wlc_hw->corerev, 40) &&
		!(txs->status.raw_bits & TX_STATUS_AMPDU) &&
		(txs->status.raw_bits & TX_STATUS_INTERMEDIATE)) {
		WL_TRACE(("%s: discard status\n", __FUNCTION__));
		return FALSE;
	}

	return wlc_dotxstatus(wlc_hw->wlc, txs, s2);
}


/**
 * process tx completion events in BMAC
 * Return TRUE if more tx status need to be processed. FALSE otherwise.
 */
bool BCMFASTPATH
wlc_bmac_txstatus(wlc_hw_info_t *wlc_hw, bool bound, bool *fatal)
{
	bool morepending = FALSE;
	wlc_info_t *wlc = wlc_hw->wlc;

	WL_TRACE(("wl%d: wlc_bmac_txstatus\n", wlc_hw->unit));

	if (D11REV_LT(wlc_hw->corerev, 40)) {
		/* corerev >= 5 && < 40 */
		d11regs_t *regs = wlc_hw->regs;
		osl_t *osh = wlc_hw->osh;
		tx_status_t txs;
		uint32 s1, s2;
		uint16 status_bits;
		uint n = 0;
		/* Param 'max_tx_num' indicates max. # tx status to process before break out. */
		uint max_tx_num = bound ? wlc->pub->tunables->txsbnd : -1;
		uint32 tsf_time = 0;
#ifdef WLFCTS
		uint8 status_delay;
		if (WLFCTS_ENAB(wlc->pub)) {
			ASSERT(D11REV_GE(wlc_hw->corerev, 26));
			ASSERT(wlc_bmac_mhf_get(wlc_hw, MHF2, WLC_BAND_AUTO) & MHF2_TX_TMSTMP);
		}
#endif /* WLFCTS */

		WL_TRACE(("wl%d: %s: ltrev40\n", wlc_hw->unit, __FUNCTION__));

		/* To avoid overhead time is read only once for the whole while loop
		 * since time accuracy is not a concern for now.
		 */
#ifdef WLFCTS
		if (!WLFCTS_ENAB(wlc->pub))
#endif /* !WLFCTS */
		{
			tsf_time = R_REG(osh, &regs->tsf_timerlow);
			txs.dequeuetime = 0;
		}

		while (!(*fatal) && (s1 = R_REG(osh, &regs->frmtxstatus)) & TXS_V) {
			if (s1 == 0xffffffff) {
				WL_ERROR(("wl%d: %s: dead chip\n", wlc_hw->unit, __FUNCTION__));
				ASSERT(s1 != 0xffffffff);
				WL_HEALTH_LOG(wlc_hw->wlc, DEADCHIP_ERROR);
				return morepending;
			}

			s2 = R_REG(osh, &regs->frmtxstatus2);

#ifdef WLFCTS
			if (WLFCTS_ENAB(wlc->pub)) {
				/* For corerevs >= 26, the first txstatus package contains
				 * 32-bit timestamps for dequeue_time and last_tx_time
				 */
				txs.dequeuetime = s1;
				tsf_time = s2;

				/* wait till the next 8 bytes of txstatus is available */
				status_delay = 0;
				while (((s1 = R_REG(osh, &regs->frmtxstatus)) & TXS_V) == 0) {
					OSL_DELAY(1);
					status_delay++;
					if (status_delay > 10) {
						ASSERT(0);
						return 0;
					}
				}
				s2 = R_REG(osh, &regs->frmtxstatus2);
			}
#endif /* WLFCTS */

			WL_PRHDRS_MSG(("wl%d: %s: Raw txstatus s1 0x%0X s2 0x%0X\n",
				wlc_hw->unit, __FUNCTION__, s1, s2));

			status_bits = (s1 & TXS_STATUS_MASK);
			txs.status.raw_bits = status_bits;
			txs.status.was_acked = (status_bits & TX_STATUS_ACK_RCV) != 0;
			txs.status.is_intermediate = (status_bits & TX_STATUS_INTERMEDIATE) != 0;
			txs.status.pm_indicated = (status_bits & TX_STATUS_PMINDCTD) != 0;
			txs.status.suppr_ind =
			        (status_bits & TX_STATUS_SUPR_MASK) >> TX_STATUS_SUPR_SHIFT;
			txs.status.rts_tx_cnt =
			        ((s1 & TX_STATUS_RTS_RTX_MASK) >> TX_STATUS_RTS_RTX_SHIFT);
			txs.status.frag_tx_cnt =
			        ((s1 & TX_STATUS_FRM_RTX_MASK) >> TX_STATUS_FRM_RTX_SHIFT);
			txs.frameid = (s1 & TXS_FID_MASK) >> TXS_FID_SHIFT;
			txs.sequence = s2 & TXS_SEQ_MASK;
			txs.phyerr = (s2 & TXS_PTX_MASK) >> TXS_PTX_SHIFT;
			txs.lasttxtime = tsf_time;

			*fatal = wlc_bmac_dotxstatus(wlc_hw, &txs, s2);

			/* !give others some time to run! */
			if (++n >= max_tx_num)
				break;
		}

		if (*fatal)
			return 0;

		if (n >= max_tx_num)
			morepending = TRUE;
	} else {
		/* corerev >= 40 */
		d11regs_t *regs = wlc_hw->regs;
		osl_t *osh = wlc_hw->osh;
		tx_status_t txs;
		/* pkg 1 */
		uint32 v_s1, v_s2, v_s3, v_s4;
		/* pkg 2 */
		uint32 v_s5, v_s6, v_s7, v_s8;
		uint16 status_bits;
		uint n = 0;
		uint16 ncons;

		/* Param 'max_tx_num' indicates max. # tx status to process before break out. */
		uint max_tx_num = bound ? wlc->pub->tunables->txsbnd : -1;
#ifdef WLFCTS
		if (WLFCTS_ENAB(wlc->pub)) {
			ASSERT(D11REV_GE(wlc_hw->corerev, 26));
			ASSERT(wlc_bmac_mhf_get(wlc_hw, MHF2, WLC_BAND_AUTO) & MHF2_TX_TMSTMP);
		}
#endif /* WLFCTS */

		WL_TRACE(("wl%d: %s: rev40\n", wlc_hw->unit, __FUNCTION__));

		while (!(*fatal) && (v_s1 = R_REG(osh, &regs->frmtxstatus)) & TXS_V) {
			if (v_s1 == 0xffffffff) {
				WL_ERROR(("wl%d: %s: dead chip\n", wlc_hw->unit, __FUNCTION__));
				ASSERT(v_s1 != 0xffffffff);
				return morepending;
			}

			v_s2 = R_REG(osh, &regs->frmtxstatus2);
			v_s3 = R_REG(osh, &regs->frmtxstatus3);
			v_s4 = R_REG(osh, &regs->frmtxstatus4);
			WL_TRACE(("%s: s1=%0x ampdu=%d\n", __FUNCTION__, v_s1,
				((v_s1 & 0x4) != 0)));
			txs.frameid = (v_s1 & TXS_FID_MASK) >> TXS_FID_SHIFT;
			txs.sequence = v_s2 & TXS_SEQ_MASK;
			txs.phyerr = (v_s2 & TXS_PTX_MASK) >> TXS_PTX_SHIFT;
			txs.lasttxtime = R_REG(osh, &regs->tsf_timerlow);
			status_bits = v_s1 & TXS_STATUS_MASK;
			txs.status.raw_bits = status_bits;
			txs.status.is_intermediate = (status_bits & TX_STATUS40_INTERMEDIATE) != 0;
			txs.status.pm_indicated = (status_bits & TX_STATUS40_PMINDCTD) != 0;

			ncons = ((status_bits & TX_STATUS40_NCONS) >> TX_STATUS40_NCONS_SHIFT);
			txs.status.was_acked = ((ncons <= 1) ?
				((status_bits & TX_STATUS40_ACK_RCV) != 0) : TRUE);
			txs.status.suppr_ind =
			        (status_bits & TX_STATUS40_SUPR) >> TX_STATUS40_SUPR_SHIFT;
			txs.status.frag_tx_cnt = TX_STATUS40_TXCNT(v_s3, v_s4);

			/* pkg 2 comes always */
			v_s5 = R_REG(osh, &regs->frmtxstatus);
			v_s6 = R_REG(osh, &regs->frmtxstatus2);
			v_s7 = R_REG(osh, &regs->frmtxstatus3);
			v_s8 = R_REG(osh, &regs->frmtxstatus4);
			WL_TRACE(("wl%d: %s calls dotxstatus\n", wlc_hw->unit, __FUNCTION__));

			WL_PRHDRS_MSG(("wl%d: %s:: Raw txstatus %08X %08X %08X %08X "
				"%08X %08X %08X %08X\n",
				wlc_hw->unit, __FUNCTION__,
				v_s1, v_s2, v_s3, v_s4, v_s5, v_s6, v_s7, v_s8));

			/* store saved extras (check valid pkg ) */
			if ((v_s5 & TXS_V) == 0) {
				/* if not a valid package, assert and bail */
				WL_ERROR(("wl%d: %s: package read not valid\n",
				          wlc_hw->unit, __FUNCTION__));
				ASSERT(v_s5 != 0xffffffff);
				return morepending;
			}
			txs.status.s3 = v_s3;
			txs.status.s4 = v_s4;
			txs.status.s5 = v_s5;
			txs.status.ack_map1 = v_s6;
			txs.status.ack_map2 = v_s7;
			txs.status.s8 = v_s8;

			txs.status.rts_tx_cnt =
			        ((v_s5 & TX_STATUS40_RTS_RTX_MASK) >> TX_STATUS40_RTS_RTX_SHIFT);
			txs.status.cts_rx_cnt =
			        ((v_s5 & TX_STATUS40_CTS_RRX_MASK) >> TX_STATUS40_CTS_RRX_SHIFT);

#ifdef WLFCTS
			if (WLFCTS_ENAB(wlc->pub)) {
				uint32 lasttxtime_lo16 = (v_s8 >> 16) & 0x0000ffff;
				uint32 dequeuetime_lo16 = v_s8 & 0x0000ffff;
				txs.dequeuetime = ((txs.lasttxtime - dequeuetime_lo16) & 0xffff0000)
						| dequeuetime_lo16;
				txs.lasttxtime = ((txs.lasttxtime - lasttxtime_lo16) & 0xffff0000)
						| lasttxtime_lo16;
			}
#endif /* WLFCTS */

			*fatal = wlc_bmac_dotxstatus(wlc_hw, &txs, v_s2);

			/* !give others some time to run! */
#ifdef PROP_TXSTATUS
			/* We must drain out in case of suppress, to avoid Out of Orders */
			if (txs.status.suppr_ind == TX_STATUS_SUPR_NONE)
#endif
				if (++n >= max_tx_num)
					break;
		}

		if (*fatal) {
			WL_ERROR(("error %d caught in %s\n", *fatal, __FUNCTION__));
			return 0;
		}

		if (n >= max_tx_num)
			morepending = TRUE;
	}

	if (wlc->active_queue != NULL && WLC_TXQ_OCCUPIED(wlc)) {
		WLDURATION_ENTER(wlc, DUR_DPC_TXSTATUS_SENDQ);
		wlc_send_q(wlc, wlc->active_queue);
		WLDURATION_EXIT(wlc, DUR_DPC_TXSTATUS_SENDQ);
	}

	return morepending;
} /* wlc_bmac_txstatus */

#if defined(STA) && defined(WLRM)
static uint16
wlc_bmac_read_ihr(wlc_hw_info_t *wlc_hw, uint offset)
{
	uint16 v;
	wlc_bmac_copyfrom_objmem(wlc_hw, offset << 2, &v,
		sizeof(v), OBJADDR_IHR_SEL);
	return v;
}
#endif  /* STA && WLRM */

void
wlc_bmac_write_ihr(wlc_hw_info_t *wlc_hw, uint offset, uint16 v)
{
	wlc_bmac_copyto_objmem(wlc_hw, offset<<2, &v, sizeof(v), OBJADDR_IHR_SEL);
}

#ifdef WLRSDB
void
wlc_bmac_update_rxpost_rxbnd(wlc_hw_info_t *wlc_hw, uint8 nrxpost, uint8 rxbnd)
{
	wlc_info_t *wlc = wlc_hw->wlc;

	/* set new nrxpost value & update rxfill accordingly */
	dma_param_set(wlc_hw->di[RX_FIFO], HNDDMA_NRXPOST, nrxpost);
	if (wlc->pub->up)
		dma_update_rxfill(wlc_hw->di[RX_FIFO]);

#if defined(PKTC) || defined(PKTC_DONGLE)
	wlc->pub->tunables->pktcbnd = rxbnd;
#else
	wlc->pub->tunables->rxbnd = rxbnd;
#endif
}
#endif /* WLRSDB */

void
wlc_bmac_suspend_mac_and_wait(wlc_hw_info_t *wlc_hw)
{
	wlc_info_t *wlc = wlc_hw->wlc;
	d11regs_t *regs = wlc_hw->regs;
	osl_t *osh = wlc_hw->osh;
	uint32 mc, mi;

	WL_TRACE(("wl%d: wlc_bmac_suspend_mac_and_wait: bandunit %d\n", wlc_hw->unit,
		wlc_hw->band->bandunit));

	BCM_REFERENCE(wlc);
	/*
	 * Track overlapping suspend requests
	 */
	wlc_hw->mac_suspend_depth++;
	if (wlc_hw->mac_suspend_depth > 1) {
		mc = R_REG(osh, &regs->maccontrol);
		if (mc & MCTL_EN_MAC) {
			WL_PRINT(("%s ERROR: suspend_depth %d maccontrol 0x%x\n",
				__FUNCTION__, wlc_hw->mac_suspend_depth, mc));
			wlc_dump_ucode_fatal(wlc_hw->wlc, PSM_FATAL_SUSP);
			ASSERT(!(mc & MCTL_EN_MAC));
		}
		WL_TRACE(("wl%d: %s: bail: mac_suspend_depth=%d\n", wlc_hw->unit,
			__FUNCTION__, wlc_hw->mac_suspend_depth));
		return;
	}

#ifdef STA
	/* force the core awake */
	wlc_ucode_wake_override_set(wlc_hw, WLC_WAKE_OVERRIDE_MACSUSPEND);
#endif /* STA */
	mc = R_REG(osh, &regs->maccontrol);

	if (mc == 0xffffffff) {
		WL_ERROR(("wl%d: %s: dead chip\n", wlc_hw->unit, __FUNCTION__));
		WL_HEALTH_LOG(wlc, DEADCHIP_ERROR);
		wl_down(wlc->wl);
		return;
	}
	ASSERT(!(mc & MCTL_PSM_JMP_0));
	ASSERT(mc & MCTL_PSM_RUN);
	ASSERT(mc & MCTL_EN_MAC);

	mi = R_REG(osh, &regs->macintstatus);
	if (mi == 0xffffffff) {
		WL_ERROR(("wl%d: %s: dead chip\n", wlc_hw->unit, __FUNCTION__));
		WL_HEALTH_LOG(wlc, DEADCHIP_ERROR);
		wl_down(wlc->wl);
		return;
	}
#ifdef WAR4360_UCODE
	if (mi & MI_MACSSPNDD) {
		WL_ERROR(("wl%d:%s: Hammering due to (mc & MI_MACSPNDD)\n",
			wlc_hw->unit, __FUNCTION__));
		wlc_hw->need_reinit = 4;
		return;
	}
#endif /* WAR4360_UCODE */
	ASSERT(!(mi & MI_MACSSPNDD));

	wlc_bmac_mctrl(wlc_hw, MCTL_EN_MAC, 0);

	SPINWAIT(!(R_REG(osh, &regs->macintstatus) & MI_MACSSPNDD), WLC_MAX_MAC_SUSPEND);

	if (!(R_REG(osh, &regs->macintstatus) & MI_MACSSPNDD)) {
		WLC_EXTLOG(wlc, LOG_MODULE_COMMON, FMTSTR_SUSPEND_MAC_FAIL_ID,
			WL_LOG_LEVEL_ERR, 0, R_REG(osh, &regs->psmdebug), NULL);
		WLC_EXTLOG(wlc, LOG_MODULE_COMMON, FMTSTR_REG_PRINT_ID, WL_LOG_LEVEL_ERR,
			0, R_REG(osh, &regs->phydebug), "phydebug");
		WLC_EXTLOG(wlc, LOG_MODULE_COMMON, FMTSTR_REG_PRINT_ID, WL_LOG_LEVEL_ERR,
			0, R_REG(osh, &regs->psm_brc), "psm_brc");
		WL_PRINT(("wl%d: wlc_bmac_suspend_mac_and_wait: waited %d uS and "
			 "MI_MACSSPNDD is still not on.\n",
			 wlc_hw->unit, WLC_MAX_MAC_SUSPEND));
		wlc_dump_ucode_fatal(wlc, PSM_FATAL_SUSP);
		WL_HEALTH_LOG(wlc, MACSPEND_TIMOUT);
#ifdef WAR4360_UCODE
		WL_ERROR(("wl%d:%s: Hammering due to SPINWAIT timeout\n",
			wlc_hw->unit, __FUNCTION__));
		wlc_hw->need_reinit = 5;
		return;
#endif /* WAR4360_UCODE */
	}

	mc = R_REG(osh, &regs->maccontrol);
	if (mc == 0xffffffff) {
		WL_ERROR(("wl%d: %s: dead chip\n", wlc_hw->unit, __FUNCTION__));
		WL_HEALTH_LOG(wlc, DEADCHIP_ERROR);
		wl_down(wlc->wl);
		return;
	}
	ASSERT(!(mc & MCTL_PSM_JMP_0));
	ASSERT(mc & MCTL_PSM_RUN);
	ASSERT(!(mc & MCTL_EN_MAC));
	if (((CHIPID(wlc_hw->sih->chip)) == BCM5357_CHIP_ID) ||
	    ((CHIPID(wlc_hw->sih->chip)) == BCM53572_CHIP_ID)) {
	    wlc_bmac_mctrl(wlc_hw, MCTL_PSM_RUN, 0);
	}

#if defined(BCMDBG_PHYDUMP)
	{
	    bmac_suspend_stats_t* stats = wlc_hw->suspend_stats;

	    stats->suspend_start = R_REG(osh, &regs->tsf_timerlow);
	    stats->suspend_count++;

	    if (stats->suspend_start > stats->suspend_end) {
			uint32 unsuspend_time = (stats->suspend_start - stats->suspend_end)/100;
			stats->unsuspended += unsuspend_time;
			WL_TRACE(("wl%d: bmac now suspended; time spent active was %d ms\n",
			           wlc_hw->unit, (unsuspend_time + 5)/10));
	    }
	}
#endif 
} /* wlc_bmac_suspend_mac_and_wait */


void
wlc_bmac_enable_mac(wlc_hw_info_t *wlc_hw)
{
	d11regs_t *regs = wlc_hw->regs;
	uint32 mc, mi;
	osl_t *osh;

	WL_TRACE(("wl%d: wlc_bmac_enable_mac: bandunit %d\n",
		wlc_hw->unit, wlc_hw->band->bandunit));
#ifdef WAR4360_UCODE
	if (wlc_hw->need_reinit) {
		return;
	}
#endif
	/*
	 * Track overlapping suspend requests
	 */
	ASSERT(wlc_hw->mac_suspend_depth > 0);
	wlc_hw->mac_suspend_depth--;
	if (wlc_hw->mac_suspend_depth > 0)
		return;

	osh = wlc_hw->osh;

	mc = R_REG(osh, &regs->maccontrol);
	ASSERT(!(mc & MCTL_PSM_JMP_0));
	ASSERT(!(mc & MCTL_EN_MAC));
	if (((CHIPID(wlc_hw->sih->chip)) != BCM5357_CHIP_ID) &&
	    ((CHIPID(wlc_hw->sih->chip)) != BCM53572_CHIP_ID)) {
		ASSERT(mc & MCTL_PSM_RUN);
	}


	if (((CHIPID(wlc_hw->sih->chip)) == BCM5357_CHIP_ID) ||
	    ((CHIPID(wlc_hw->sih->chip)) == BCM53572_CHIP_ID))
		wlc_bmac_mctrl(wlc_hw, (MCTL_EN_MAC | MCTL_PSM_RUN), (MCTL_EN_MAC | MCTL_PSM_RUN));
	else
		wlc_bmac_mctrl(wlc_hw, MCTL_EN_MAC, MCTL_EN_MAC);

	W_REG(osh, &regs->macintstatus, MI_MACSSPNDD);

	mc = R_REG(osh, &regs->maccontrol);
	ASSERT(!(mc & MCTL_PSM_JMP_0));
	ASSERT(mc & MCTL_EN_MAC);
	ASSERT(mc & MCTL_PSM_RUN);
	BCM_REFERENCE(mc);

	mi = R_REG(osh, &regs->macintstatus);
	ASSERT(!(mi & MI_MACSSPNDD));
	BCM_REFERENCE(mi);

#ifdef STA
	wlc_ucode_wake_override_clear(wlc_hw, WLC_WAKE_OVERRIDE_MACSUSPEND);
#endif /* STA */

#ifdef MBSS
	if (MBSS_SUPPORT(wlc_hw->wlc->pub)) {
		wlc_mbss_reset_prq(wlc_hw->wlc);
	}
#endif

#if defined(BCMDBG_PHYDUMP)
	{
	bmac_suspend_stats_t* stats = wlc_hw->suspend_stats;

	stats->suspend_end = R_REG(osh, &regs->tsf_timerlow);

	if (stats->suspend_end > stats->suspend_start) {
		uint32 suspend_time = (stats->suspend_end - stats->suspend_start)/100;

		if (suspend_time > stats->suspend_max) {
			stats->suspend_max = suspend_time;
		}
		stats->suspended += suspend_time;
		WL_TRACE(("wl%d: bmac now active; time spent suspended was %d ms\n",
		          wlc_hw->unit, (suspend_time + 5)/10));
	}
	}
#endif 
} /* wlc_bmac_enable_mac */

void
wlc_bmac_sync_macstate(wlc_hw_info_t *wlc_hw)
{
	bool wake_override = ((wlc_hw->wake_override & WLC_WAKE_OVERRIDE_MACSUSPEND) != 0);
	if (wake_override && wlc_hw->mac_suspend_depth == 1)
		wlc_bmac_enable_mac(wlc_hw);
}

void
wlc_bmac_ifsctl_vht_set(wlc_hw_info_t *wlc_hw, int ed_sel)
{
	uint32 mask, val;
	uint32 val_mask1, val_mask2;
	bool sb_ctrl, enable;
	volatile uint16 *ifsctl_reg;
	osl_t *osh;
	d11regs_t *regs;
	uint16 chanspec;
	bool suspend = FALSE;

	ASSERT(D11REV_GE(wlc_hw->corerev, 40));
	if (!WLCISACPHY(wlc_hw->band))
		return;

	osh = wlc_hw->osh;
	regs = wlc_hw->regs;
	mask = IFS_CTL_CRS_SEL_MASK|IFS_CTL_ED_SEL_MASK;

	if (ed_sel == AUTO) {
		val = (uint16)wlc_bmac_read_shm(wlc_hw, M_IFS_PRICRS(wlc_hw));
		enable = (val & IFS_CTL_ED_SEL_MASK) ? TRUE:FALSE;
	} else {
		enable = (ed_sel == ON) ? TRUE : FALSE;
	}

	if (enable)
		val_mask1 = 0x0f0f;
	else
		val_mask1 = 0x000f; /* deselect ED */

	/* http://jira.broadcom.com/browse/SWWFA-4:  always disable secondary ED */
	val_mask2 = 0xf;

	chanspec = wlc_hw->chanspec;
	switch (CHSPEC_BW(chanspec)) {
		case WL_CHANSPEC_BW_20:
#ifdef WL11ULB
		case WL_CHANSPEC_BW_2P5:
		case WL_CHANSPEC_BW_5:
		case WL_CHANSPEC_BW_10:
#endif /* WL11ULB */
		ifsctl_reg = (volatile uint16 *)&regs->u.d11regs.ifs_ctl_sel_pricrs;
		val = mask & val_mask1;
		W_REG(osh, ifsctl_reg, val);

		if (D11REV_GE(wlc_hw->corerev, 40))
			wlc_bmac_write_shm(wlc_hw, M_IFS_PRICRS(wlc_hw), (uint16)val);
		else
			wlc_bmac_write_shm(wlc_hw, M_IFSCTL1(wlc_hw), (uint16)val);
		break;

	case WL_CHANSPEC_BW_40:
		/* Secondary first */
		sb_ctrl = (chanspec & WL_CHANSPEC_CTL_SB_MASK) ==  WL_CHANSPEC_CTL_SB_L;
		val = (uint32)(sb_ctrl ? 0x0202 : 0x0101) & val_mask2;

		ifsctl_reg = (volatile uint16 *)&regs->u.d11regs.ifs_ctl_sel_seccrs;
		W_REG(osh, ifsctl_reg, val);

		/* Primary */
		val = (uint32)((wlc_hw->band->mhfs[MHF1] & MHF1_D11AC_DYNBW) ?
			(val ^ 0x303) : 0x303) & val_mask1;

		if (D11REV_GE(wlc_hw->corerev, 40))
			wlc_bmac_write_shm(wlc_hw, M_IFS_PRICRS(wlc_hw), (uint16)val);
		else
			wlc_bmac_write_shm(wlc_hw, M_IFSCTL1(wlc_hw), (uint16)val);
		ifsctl_reg = (volatile uint16 *)&regs->u.d11regs.ifs_ctl_sel_pricrs;
		W_REG(osh, ifsctl_reg, val);
		break;

	case WL_CHANSPEC_BW_80:
	case WL_CHANSPEC_BW_8080:
		/* Secondary first */
		sb_ctrl =
			(chanspec & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_LL ||
			(chanspec & WL_CHANSPEC_CTL_SB_MASK) == WL_CHANSPEC_CTL_SB_LU;
		val = (uint32)(sb_ctrl ? 0x0c0c : 0x0303) & val_mask2;
		ifsctl_reg = (volatile uint16 *)&regs->u.d11regs.ifs_ctl_sel_seccrs;
		W_REG(osh, ifsctl_reg, val);

		/* Primary */
		val = (uint32)((wlc_hw->band->mhfs[MHF1] & MHF1_D11AC_DYNBW) ?
			(val ^ 0xf0f) : 0xf0f) & val_mask1;

		if (D11REV_GE(wlc_hw->corerev, 40))
			wlc_bmac_write_shm(wlc_hw, M_IFS_PRICRS(wlc_hw), (uint16)val);
		else
			wlc_bmac_write_shm(wlc_hw, M_IFSCTL1(wlc_hw), (uint16)val);

		ifsctl_reg = (volatile uint16 *)&regs->u.d11regs.ifs_ctl_sel_pricrs;
		W_REG(osh, ifsctl_reg, val);

		break;
	default:
		WL_ERROR(("Unsupported bandwidth - chanspec: %04x\n",
			wlc_hw->chanspec));
		ASSERT(!"Invalid bandwidth in chanspec");
	}

	wlc_phy_conditional_suspend((phy_info_t *)wlc_hw->band->pi, &suspend);

	/* update phyreg NsyncscramInit1:scramb_dyn_bw_en */
	wlc_acphy_set_scramb_dyn_bw_en(wlc_hw->band->pi, enable);

	wlc_phy_conditional_resume((phy_info_t *)wlc_hw->band->pi, &suspend);

} /* wlc_bmac_ifsctl_vht_set */

void
wlc_bmac_ifsctl_edcrs_set(wlc_hw_info_t *wlc_hw, bool isht)
{
	if (!(WLCISNPHY(wlc_hw->band)) &&
	    !WLCISHTPHY(wlc_hw->band) && !WLCISACPHY(wlc_hw->band))
		return;

	if (!isht) {
		/* enable EDCRS for non-11n association */
		wlc_bmac_ifsctl1_regshm(wlc_hw, IFS_CTL1_EDCRS, IFS_CTL1_EDCRS);
	}
	if (WLCISHTPHY(wlc_hw->band) || WLCISNPHY(wlc_hw->band)) {
		if (CHSPEC_IS20(wlc_hw->chanspec)) {
			/* 20 mhz, use 20U ED only */
			wlc_bmac_ifsctl1_regshm(wlc_hw,
				(IFS_CTL1_EDCRS | IFS_CTL1_EDCRS_20L | IFS_CTL1_EDCRS_40),
				IFS_CTL1_EDCRS);
		} else {
			/* 40 mhz, use 20U 20L and 40 ED */
			wlc_bmac_ifsctl1_regshm(wlc_hw,
				(IFS_CTL1_EDCRS | IFS_CTL1_EDCRS_20L | IFS_CTL1_EDCRS_40),
				(IFS_CTL1_EDCRS | IFS_CTL1_EDCRS_20L | IFS_CTL1_EDCRS_40));
		}
	} else if (WLCISACPHY(wlc_hw->band)) {
		wlc_bmac_ifsctl_vht_set(wlc_hw, ON);
	}
} /* wlc_bmac_ifsctl_edcrs_set */

#ifdef WL11N
static void
wlc_upd_ofdm_pctl1_table(wlc_hw_info_t *wlc_hw)
{
	uint8 rate;
	const uint8 rates[8] = {
		WLC_RATE_6M, WLC_RATE_9M, WLC_RATE_12M, WLC_RATE_18M,
		WLC_RATE_24M, WLC_RATE_36M, WLC_RATE_48M, WLC_RATE_54M
	};

	uint16 rate_phyctl1[8] = {0x0002, 0x0202, 0x0802, 0x0a02, 0x1002, 0x1202, 0x1902, 0x1a02};

	uint16 entry_ptr;
	uint16 pctl1, phyctl;
	uint i;

	if (!WLC_PHY_11N_CAP(wlc_hw->band))
		return;

	/* walk the phy rate table and update the entries */
	for (i = 0; i < ARRAYSIZE(rates); i++) {
		rate = rates[i];

		entry_ptr = wlc_bmac_ofdm_ratetable_offset(wlc_hw, rate);

		/* read the SHM Rate Table entry OFDM PCTL1 values */
		pctl1 = wlc_bmac_read_shm(wlc_hw, entry_ptr + M_RT_OFDM_PCTL1_POS(wlc_hw));

		/* modify the MODE & code_rate value */
		if (D11REV_IS(wlc_hw->corerev, 31) && WLCISNPHY(wlc_hw->band)) {
			/* corerev31 uses corerev29 ucode, where PHY_CTL_1 inits is for HTPHY
			 * fix it to OFDM rate
			 */
			pctl1 &= (PHY_TXC1_MODE_MASK | PHY_TXC1_BW_MASK);
			pctl1 |= (rate_phyctl1[i] & 0xFFC0);
		}

		if (D11REV_IS(wlc_hw->corerev, 29) &&
			WLCISHTPHY(wlc_hw->band) &&
			AMPDU_HW_ENAB(wlc_hw->wlc->pub)) {
			pctl1 &= ~PHY_TXC1_BW_MASK;
			if (CHSPEC_WLC_BW(wlc_hw->chanspec) == WLC_40_MHZ)
				pctl1 |= PHY_TXC1_BW_40MHZ_DUP;
			else
				pctl1 |= PHY_TXC1_BW_20MHZ;
		}

		/* modify the STF value */
		if ((WLCISNPHY(wlc_hw->band)) || (WLCISLCNPHY(wlc_hw->band))) {
			pctl1 &= ~PHY_TXC1_MODE_MASK;
			if (wlc_bmac_btc_mode_get(wlc_hw))
				pctl1 |= (PHY_TXC1_MODE_SISO << PHY_TXC1_MODE_SHIFT);
			else
				pctl1 |= (wlc_hw->hw_stf_ss_opmode << PHY_TXC1_MODE_SHIFT);
		}

		/* Update the SHM Rate Table entry OFDM PCTL1 values */
		wlc_bmac_write_shm(wlc_hw, entry_ptr + M_RT_OFDM_PCTL1_POS(wlc_hw), pctl1);
	}

	/* only works for nphy */
	if (wlc_bmac_btc_mode_get(wlc_hw))
	{
		uint16 ant_ctl = ((wlc_hw->boardflags2 & BFL2_BT_SHARE_ANT0) == BFL2_BT_SHARE_ANT0)
			? PHY_TXC_ANT_1 : PHY_TXC_ANT_0;
		/* set the Response (ACK/CTS) frame phy control word */
		phyctl = wlc_bmac_read_shm(wlc_hw, M_RSP_PCTLWD(wlc_hw));
		phyctl = (phyctl & ~PHY_TXC_ANT_MASK) | ant_ctl;
		wlc_bmac_write_shm(wlc_hw, M_RSP_PCTLWD(wlc_hw), phyctl);
	}
} /* wlc_upd_ofdm_pctl1_table */

static uint16
wlc_bmac_ofdm_ratetable_offset(wlc_hw_info_t *wlc_hw, uint8 rate)
{
	uint i;
	uint8 plcp_rate = 0;
	struct plcp_signal_rate_lookup {
		uint8 rate;
		uint8 signal_rate;
	};
	/* OFDM RATE sub-field of PLCP SIGNAL field, per 802.11 sec 17.3.4.1 */
	const struct plcp_signal_rate_lookup rate_lookup[] = {
		{WLC_RATE_6M,  0xB},
		{WLC_RATE_9M,  0xF},
		{WLC_RATE_12M, 0xA},
		{WLC_RATE_18M, 0xE},
		{WLC_RATE_24M, 0x9},
		{WLC_RATE_36M, 0xD},
		{WLC_RATE_48M, 0x8},
		{WLC_RATE_54M, 0xC}
	};

	for (i = 0; i < ARRAYSIZE(rate_lookup); i++) {
		if (rate == rate_lookup[i].rate) {
			plcp_rate = rate_lookup[i].signal_rate;
			break;
		}
	}

	/* Find the SHM pointer to the rate table entry by looking in the
	 * Direct-map Table
	 */
	return (2*wlc_bmac_read_shm(wlc_hw, M_RT_DIRMAP_A(wlc_hw) + (plcp_rate * 2)));
}

void
wlc_bmac_band_stf_ss_set(wlc_hw_info_t *wlc_hw, uint8 stf_mode)
{
	wlc_hw->hw_stf_ss_opmode = stf_mode;

	if (wlc_hw->clk)
		wlc_upd_ofdm_pctl1_table(wlc_hw);
}

void
wlc_bmac_txbw_update(wlc_hw_info_t *wlc_hw)
{
	if (wlc_hw->clk)
		wlc_upd_ofdm_pctl1_table(wlc_hw);

}
#endif /* WL11N */

void BCMFASTPATH
wlc_bmac_read_tsf(wlc_hw_info_t* wlc_hw, uint32* tsf_l_ptr, uint32* tsf_h_ptr)
{
	d11regs_t *regs = wlc_hw->regs;
	uint32 tsf_l;

	/* read the tsf timer low, then high to get an atomic read */
	tsf_l = R_REG(wlc_hw->osh, &regs->tsf_timerlow);

	if (tsf_l_ptr)
		*tsf_l_ptr = tsf_l;

	if (tsf_h_ptr)
		*tsf_h_ptr = R_REG(wlc_hw->osh, &regs->tsf_timerhigh);

	return;
}

uint32
wlc_bmac_read_usec_timer(wlc_hw_info_t* wlc_hw)
{
	d11regs_t *regs = wlc_hw->regs;

	/* use usec timer for revisions 26, 29 and revision 31 onwards */
	if (D11REV_GE(wlc_hw->corerev, 31) ||
		D11REV_IS(wlc_hw->corerev, 26) ||
		D11REV_IS(wlc_hw->corerev, 29)) {
		return R_REG(wlc_hw->osh, &regs->usectimer);
	}

	return R_REG(wlc_hw->osh, &regs->tsf_timerlow);
}

bool
#ifdef WLDIAG
wlc_bmac_validate_chip_access(wlc_hw_info_t *wlc_hw)
#else
BCMATTACHFN(wlc_bmac_validate_chip_access)(wlc_hw_info_t *wlc_hw)
#endif
{
	d11regs_t *regs = wlc_hw->regs;
	uint32 w, valw, valr;
	osl_t *osh = wlc_hw->osh;

	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));

	/* Validate dchip register access */
	wlc_bmac_copyfrom_shm(wlc_hw, 0, &w, sizeof(w));

	/* Can we write and read back a 32bit register? */
	valw = 0xaa5555aa;
	wlc_bmac_copyto_shm(wlc_hw, 0, &valw, sizeof(valw));

	wlc_bmac_copyfrom_shm(wlc_hw, 0, &valr, sizeof(valr));
	if (valr != valw) {
		WL_ERROR(("wl%d: %s: SHM = 0x%x, expected 0x%x\n",
			wlc_hw->unit, __FUNCTION__, valr, valw));
		return (FALSE);
	}

	valw = 0x55aaaa55;
	wlc_bmac_copyto_shm(wlc_hw, 0, &valw, sizeof(valw));

	wlc_bmac_copyfrom_shm(wlc_hw, 0, &valr, sizeof(valr));
	if (valr != valw) {
		WL_ERROR(("wl%d: %s: SHM = 0x%x, expected 0x%x\n",
			wlc_hw->unit, __FUNCTION__, valr, valw));
		return (FALSE);
	}

	wlc_bmac_copyto_shm(wlc_hw, 0, &w, sizeof(w));

	/* clear CFPStart */
	W_REG(osh, &regs->tsf_cfpstart, 0);

	w = R_REG(osh, &regs->maccontrol);
	if ((w != (MCTL_IHR_EN | MCTL_WAKE)) &&
	    (w != (MCTL_IHR_EN | MCTL_GMODE | MCTL_WAKE))) {
		WL_ERROR(("wl%d: %s: maccontrol = 0x%x, expected 0x%x or 0x%x\n",
		          wlc_hw->unit, __FUNCTION__, w, (MCTL_IHR_EN | MCTL_WAKE),
		          (MCTL_IHR_EN | MCTL_GMODE | MCTL_WAKE)));
		return (FALSE);
	}
	return (TRUE);
} /* wlc_bmac_validate_chip_access */

#define PHYPLL_WAIT_US	100000

void
wlc_bmac_core_phypll_ctl(wlc_hw_info_t* wlc_hw, bool on)
{
	d11regs_t *regs;
	osl_t *osh;
	uint32 req_bits, avail_bits, tmp;

	WL_TRACE(("wl%d: wlc_bmac_core_phypll_ctl\n", wlc_hw->unit));

	if (D11REV_IS(wlc_hw->corerev, 27))
		return;

	regs = wlc_hw->regs;
	osh = wlc_hw->osh;

	/* Do not access registers if core is not up */
	if (wlc_bmac_si_iscoreup(wlc_hw) == FALSE)
		return;

	if (on) {
		if (D11REV_GE(wlc_hw->corerev, 24) &&
			!(D11REV_IS(wlc_hw->corerev, 29) || D11REV_GE(wlc_hw->corerev, 40))) {
			req_bits = PSM_CORE_CTL_PPAR;
			avail_bits = PSM_CORE_CTL_PPAS;

			if (CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID) {
				req_bits = PSM_CORE_CTL_PPAR | PSM_CORE_CTL_HAR;
				avail_bits = PSM_CORE_CTL_HAS;
			}

			OR_REG(osh, &regs->psm_corectlsts, req_bits);
			SPINWAIT((R_REG(osh, &regs->psm_corectlsts) & avail_bits) != avail_bits,
				PHYPLL_WAIT_US);

			tmp = R_REG(osh, &regs->psm_corectlsts);
		} else {
			req_bits = CCS_ERSRC_REQ_D11PLL | CCS_ERSRC_REQ_PHYPLL;
			avail_bits = CCS_ERSRC_AVAIL_D11PLL | CCS_ERSRC_AVAIL_PHYPLL;

			if (CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID) {
				req_bits = CCS_ERSRC_REQ_D11PLL | CCS_ERSRC_REQ_PHYPLL |
					CCS_ERSRC_REQ_HT;
				avail_bits = CCS_ERSRC_AVAIL_HT;
			}
			/* MIMO mode FORCEHWREQOFF is done on core-1. Hence it
			 * requires to be cleared when switching happens from
			 * MIMO to RSDB
			 */
			if (wlc_bmac_rsdb_cap(wlc_hw)) {
			        AND_REG(wlc_hw->osh, &regs->clk_ctl_st, ~CCS_FORCEHWREQOFF);
			}

			tmp = R_REG(osh, &regs->clk_ctl_st);
			/* if the req_bits alread set, then bail out */
			if ((tmp & req_bits) != req_bits) {
				W_REG(osh, &regs->clk_ctl_st, req_bits);
				/* avail_bit can be set prior to the write of req_bits */
				if ((tmp & avail_bits) == avail_bits) {
					/* break down 64usec delay to 4*16 delay so that
					 * OSL_DELAY will not yield to other thread
					 */
					int i;
					for (i = 0; i < 4; i++) {
						OSL_DELAY(16);
					}
				}
				SPINWAIT((R_REG(osh, &regs->clk_ctl_st) & avail_bits) != avail_bits,
					PHYPLL_WAIT_US);
				tmp = R_REG(osh, &regs->clk_ctl_st);
			}
		}

		if ((tmp & avail_bits) != avail_bits) {
			WL_ERROR(("%s: turn on PHY PLL failed\n", __FUNCTION__));
			WL_HEALTH_LOG(wlc_hw->wlc, PHY_PLL_ERROR);
			ASSERT(0);
		}
	} else {
		/* Since the PLL may be shared, other cores can still be requesting it;
		 * so we'll deassert the request but not wait for status to comply.
		 */
		if (D11REV_GE(wlc_hw->corerev, 24) &&
		!(D11REV_IS(wlc_hw->corerev, 29) || D11REV_GE(wlc_hw->corerev, 40))) {
			req_bits = PSM_CORE_CTL_PPAR;

			if (CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID)
				req_bits = PSM_CORE_CTL_PPAR | PSM_CORE_CTL_HAR;

			AND_REG(osh, &regs->psm_corectlsts, ~req_bits);
			tmp = R_REG(osh, &regs->psm_corectlsts);
		} else {
			req_bits = CCS_ERSRC_REQ_D11PLL | CCS_ERSRC_REQ_PHYPLL;

			if (CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID)
				req_bits = CCS_ERSRC_REQ_D11PLL | CCS_ERSRC_REQ_PHYPLL |
					CCS_ERSRC_REQ_HT;

			AND_REG(osh, &regs->clk_ctl_st, ~req_bits);
			tmp = R_REG(osh, &regs->clk_ctl_st);
		}
	}

	if (wlc_bmac_rsdb_cap(wlc_hw))
		wlc_bmac_4349_core1_hwreqoff(wlc_hw, (on == 0)? TRUE:FALSE);

	WL_TRACE(("%s: clk_ctl_st after phypll(%d) request 0x%x\n",
		__FUNCTION__, on, tmp));
} /* wlc_bmac_core_phypll_ctl */

void
wlc_coredisable(wlc_hw_info_t* wlc_hw)
{
	bool dev_gone;

	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));

	ASSERT(!wlc_hw->up);

	dev_gone = DEVICEREMOVED(wlc_hw->wlc);

	if (dev_gone)
		return;

	if (wlc_hw->noreset)
		return;

	/* radio off */
	phy_radio_switch((phy_info_t *)wlc_hw->band->pi, OFF);

	/* turn off analog core */
	phy_ana_switch((phy_info_t *)wlc_hw->band->pi, OFF);

	/* turn off PHYPLL to save power */
	wlc_bmac_core_phypll_ctl(wlc_hw, FALSE);

	/* No need to set wlc->pub->radio_active = OFF
	 * because this function needs down capability and
	 * radio_active is designed for BCMNODOWN.
	 */

	/* remove gpio controls */
	if (wlc_hw->ucode_dbgsel)
		si_gpiocontrol(wlc_hw->sih, ~0, 0, GPIO_DRV_PRIORITY);

	wlc_hw->clk = FALSE;
	wlc_bmac_core_disable(wlc_hw, 0);
	wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, FALSE);
} /* wlc_coredisable */

/** power both the pll and external oscillator on/off */
void
wlc_bmac_xtal(wlc_hw_info_t* wlc_hw, bool want)
{
	WL_TRACE(("wl%d: wlc_bmac_xtal: want %d\n", wlc_hw->unit, want));

	/* dont power down if plldown is false or we must poll hw radio disable */
	if (!want && wlc_hw->pllreq)
		return;

	if (wlc_hw->sih)
		si_clkctl_xtal(wlc_hw->sih, XTAL|PLL, want);

	wlc_hw->sbclk = want;
	if (!wlc_hw->sbclk) {
		wlc_hw->clk = FALSE;
		if (wlc_hw->band && wlc_hw->band->pi)
			wlc_phy_hw_clk_state_upd(wlc_hw->band->pi, FALSE);
	}
}

static void
wlc_flushqueues(wlc_hw_info_t *wlc_hw)
{
	wlc_info_t *wlc = wlc_hw->wlc;
	uint i;

	if (!PIO_ENAB_HW(wlc_hw)) {
		wlc->txpend16165war = 0;

		/* free any posted tx packets */
		for (i = 0; i < NFIFO; i++)
			if (wlc_hw->di[i]) {
				dma_txreclaim(wlc_hw->di[i], HNDDMA_RANGE_ALL);
				TXPKTPENDCLR(wlc, i);
				WL_TRACE(("wlc_flushqueues: pktpend fifo %d cleared\n", i));
#if defined(DMA_TX_FREE)
				WL_TRACE(("wlc_flushqueues: ampdu_flags cleared, head %d tail %d\n",
				          wlc_hw->txstatus_ampdu_flags[i].head,
				          wlc_hw->txstatus_ampdu_flags[i].tail));
				wlc_hw->txstatus_ampdu_flags[i].head = 0;
				wlc_hw->txstatus_ampdu_flags[i].tail = 0;
#endif
			}

		/* Free the packets which is early reclaimed */
#ifdef	WL_RXEARLYRC
		while (wlc_hw->rc_pkt_head) {
			void *p = wlc_hw->rc_pkt_head;
			wlc_hw->rc_pkt_head = PKTLINK(p);
			PKTSETLINK(p, NULL);
			PKTFREE(wlc_hw->osh, p, FALSE);
		}
#endif
		/* free any posted rx packets */
		for (i = 0; i < MAX_RX_FIFO; i++) {
			if ((wlc_hw->di[i] != NULL) && wlc_bmac_rxfifo_enab(i)) {
				dma_rxreclaim(wlc_hw->di[i]);
			}
		}
	} else {
		for (i = 0; i < NFIFO; i++) {
			if (wlc_hw->pio[i]) {
				/* include reset the counter */
				wlc_pio_txreclaim(wlc_hw->pio[i]);
			}
		}
		/* For PIO, no rx sw queue to reclaim */
	}
} /* wlc_flushqueues */

#ifdef STA
#if defined(WLRM)
/** start a CCA measurement for the given number of microseconds */
void
wlc_bmac_rm_cca_measure(wlc_hw_info_t *wlc_hw, uint32 us)
{
	uint32 gpt_ticks;

	/* convert dur in TUs to 1/8 us units for GPT */
	gpt_ticks = us << 3;

	/* config GPT 2 to decrement by TSF ticks */
	wlc_bmac_write_ihr(wlc_hw, TSF_GPT_2_STAT, TSF_GPT_USETSF);
	/* set GPT 2 to the measurement duration */
	wlc_bmac_write_ihr(wlc_hw, TSF_GPT_2_CTR_L, (gpt_ticks & 0xffff));
	wlc_bmac_write_ihr(wlc_hw, TSF_GPT_2_CTR_H, (gpt_ticks >> 16));
	/* tell ucode to start the CCA measurement */
	OR_REG(wlc_hw->osh, &wlc_hw->regs->maccommand, MCMD_CCA);

	return;
}

void
wlc_bmac_rm_cca_int(wlc_hw_info_t *wlc_hw)
{
	uint32 cca_idle;
	uint32 cca_idle_us;
	uint32 gpt2_h, gpt2_l;

	gpt2_l = wlc_bmac_read_ihr(wlc_hw, TSF_GPT_2_VAL_L);
	gpt2_h = wlc_bmac_read_ihr(wlc_hw, TSF_GPT_2_VAL_H);
	cca_idle = (gpt2_h << 16) | gpt2_l;

	/* convert GTP 1/8 us units to us */
	cca_idle_us = (cca_idle >> 3);

	wlc_rm_cca_complete(wlc_hw->wlc, cca_idle_us);
}
#endif /* WLRM */
#endif /* STA */

/** set the PIO mode bit in the control register for the rxfifo */
void
wlc_rxfifo_setpio(wlc_hw_info_t *wlc_hw)
{
	fifo64_t *fiforegs;

	fiforegs = &wlc_hw->regs->f64regs[RX_FIFO];
	W_REG(wlc_hw->osh, &fiforegs->dmarcv.control, D64_RC_FM);
}

#ifdef WL_HWKTAB
/**
 * Set the range of objmem memory that is organized as 32bit words to a value.
 * 'offset' needs to be multiple of 4 bytes and
 * Buffer length 'len' must be an multiple of 4 bytes
 * 'sel' selects the type of memory
 */
void
wlc_bmac_set_objmem32(wlc_hw_info_t *wlc_hw, uint offset, uint32 val, int len, uint32 sel)
{
	d11regs_t *regs = wlc_hw->regs;
	int i;

	ASSERT(wlc_hw->clk);
	PANIC_CHECK_CLK(wlc_hw->clk, "wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__);

	ASSERT((offset & 3) == 0);
	ASSERT((len & 3) == 0);

	ASSERT(regs != NULL);

	W_REG(wlc_hw->osh, &regs->objaddr, sel | OBJADDR_AUTO_INC | (offset >> 2));
	(void)R_REG(wlc_hw->osh, &regs->objaddr);

	for (i = 0; i < len; i += 4) {
		W_REG(wlc_hw->osh, &regs->objdata, val);
	}
}

/**
 * Copy a buffer to an objmem memory that is organized as 32bit words.
 * 'offset' needs to be multiple of 4 bytes and
 * Buffer length 'len' must be an multiple of 4 bytes
 * 'sel' selects the type of memory
 */
void
wlc_bmac_copyto_objmem32(wlc_hw_info_t *wlc_hw, uint offset, const uint8 *buf, int len, uint32 sel)
{
	d11regs_t *regs = wlc_hw->regs;
	const uint8* p = buf;
	int i;
	uint32 val;

	ASSERT(wlc_hw->clk);
	PANIC_CHECK_CLK(wlc_hw->clk, "wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__);

	ASSERT((offset & 3) == 0);
	ASSERT((len & 3) == 0);

	ASSERT(regs != NULL);

	W_REG(wlc_hw->osh, &regs->objaddr, sel | OBJADDR_AUTO_INC | (offset >> 2));
	(void)R_REG(wlc_hw->osh, &regs->objaddr);

	for (i = 0; i < len; i += 4) {
		val = p[i] | (p[i+1] << 8) | (p[i+2] << 16) | (p[i+3] << 24);
		val = htol32(val);
		W_REG(wlc_hw->osh, &regs->objdata, val);
	}

}

/**
 * Copy objmem memory that is organized as 32bit words to a buffer.
 * 'offset' needs to be multiple of 4 bytes and
 * Buffer length 'len' must be an multiple of 4 bytes
 * 'sel' selects the type of memory
 */
void
wlc_bmac_copyfrom_objmem32(wlc_hw_info_t *wlc_hw, uint offset, uint8 *buf, int len, uint32 sel)
{
	d11regs_t *regs = wlc_hw->regs;
	uint8* p = buf;
	int i, len32 = (len/4)*4;
	uint32 val;

	ASSERT(wlc_hw->clk);
	PANIC_CHECK_CLK(wlc_hw->clk, "wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__);

	ASSERT((offset & 3) == 0);
	ASSERT((len & 3) == 0);

	W_REG(wlc_hw->osh, &regs->objaddr, sel | OBJADDR_AUTO_INC | (offset >> 2));
	(void)R_REG(wlc_hw->osh, &regs->objaddr);
	for (i = 0; i < len32; i += 4) {
		val = R_REG(wlc_hw->osh, &regs->objdata);
		val = ltoh32(val);
		p[i] = val & 0xFF;
		p[i+1] = (val >> 8) & 0xFF;
		p[i+2] = (val >> 16) & 0xFF;
		p[i+3] = (val >> 24) & 0xFF;
	}
}
#endif /* WL_HWKTAB */

uint16
wlc_bmac_read_shm(wlc_hw_info_t *wlc_hw, uint offset)
{
	return  wlc_bmac_read_objmem16(wlc_hw, offset, OBJADDR_SHM_SEL);
}

void
wlc_bmac_write_shm(wlc_hw_info_t *wlc_hw, uint offset, uint16 v)
{
	wlc_bmac_write_objmem16(wlc_hw, offset, v, OBJADDR_SHM_SEL);
}

void
wlc_bmac_update_shm(wlc_hw_info_t *wlc_hw, uint offset, uint16 v, uint16 mask)
{
	uint16 shmval;

	ASSERT((v & ~mask) == 0);

	shmval = wlc_bmac_read_shm(wlc_hw, offset);
	shmval = (shmval & ~mask) | v;
	wlc_bmac_write_shm(wlc_hw, offset, shmval);
}

/**
 * Set a range of shared memory to a value.
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 */
void
wlc_bmac_set_shm(wlc_hw_info_t *wlc_hw, uint offset, uint16 v, int len)
{
	int i;

	/* offset and len need to be even */
	ASSERT((offset & 1) == 0);
	ASSERT((len & 1) == 0);

	if (len <= 0)
		return;

	for (i = 0; i < len; i += 2) {
		wlc_bmac_write_objmem16(wlc_hw, offset + i, v, OBJADDR_SHM_SEL);
	}
}

static uint16
wlc_bmac_read_objmem16(wlc_hw_info_t *wlc_hw, uint offset, uint32 sel)
{
	d11regs_t *regs = wlc_hw->regs;
	volatile uint16* objdata_lo = (volatile uint16*)(uintptr)&regs->objdata;
	volatile uint16* objdata_hi = objdata_lo + 1;
	uint16 v;

	ASSERT(wlc_hw->clk);
	PANIC_CHECK_CLK(wlc_hw->clk, "wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__);

	ASSERT((offset & 1) == 0);
	W_REG(wlc_hw->osh, &regs->objaddr, sel | (offset >> 2));
	(void)R_REG(wlc_hw->osh, &regs->objaddr);
	if (offset & 2) {
		v = R_REG(wlc_hw->osh, objdata_hi);
	} else {
		v = R_REG(wlc_hw->osh, objdata_lo);
	}

	return v;
}

static uint32
wlc_bmac_read_objmem32(wlc_hw_info_t *wlc_hw, uint offset, uint32 sel)
{
	d11regs_t *regs = wlc_hw->regs;
	uint32 v;

	ASSERT(wlc_hw->clk);
	PANIC_CHECK_CLK(wlc_hw->clk, "wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__);

	ASSERT((offset & 3) == 0);

	W_REG(wlc_hw->osh, &regs->objaddr, sel | (offset >> 2));
	(void)R_REG(wlc_hw->osh, &regs->objaddr);
	v = R_REG(wlc_hw->osh, &regs->objdata);
	return v;
}

static void
wlc_bmac_write_objmem16(wlc_hw_info_t *wlc_hw, uint offset, uint16 v, uint32 sel)
{
	d11regs_t *regs = wlc_hw->regs;
	volatile uint16* objdata_lo = (volatile uint16*)(uintptr)&regs->objdata;
	volatile uint16* objdata_hi = objdata_lo + 1;

	ASSERT(wlc_hw->clk);
	PANIC_CHECK_CLK(wlc_hw->clk, "wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__);

	ASSERT(regs != NULL);

	ASSERT((offset & 1) == 0);

	W_REG(wlc_hw->osh, &regs->objaddr, sel | (offset >> 2));
	(void)R_REG(wlc_hw->osh, &regs->objaddr);
	if (offset & 2) {
		W_REG(wlc_hw->osh, objdata_hi, v);
	} else {
		W_REG(wlc_hw->osh, objdata_lo, v);
	}
}

static void
wlc_bmac_write_objmem32(wlc_hw_info_t *wlc_hw, uint offset, uint32 v, uint32 sel)
{
	d11regs_t *regs = wlc_hw->regs;

	ASSERT(wlc_hw->clk);
	PANIC_CHECK_CLK(wlc_hw->clk, "wl%d: %s: NO CLK\n", wlc_hw->unit, __FUNCTION__);

	ASSERT(regs != NULL);
	ASSERT((offset & 3) == 0);

	W_REG(wlc_hw->osh, &regs->objaddr, sel | (offset >> 2));
	(void)R_REG(wlc_hw->osh, &regs->objaddr);
	W_REG(wlc_hw->osh, &regs->objdata, v);
}

/**
 * Copy a buffer to shared memory of specified type .
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 * 'sel' selects the type of memory
 */
void
wlc_bmac_copyto_objmem(wlc_hw_info_t *wlc_hw, uint offset, const void* buf, int len, uint32 sel)
{
	const uint8* p = (const uint8*)buf;
	int i;
	uint16 v16;
	uint32 v32;

	/* offset and len need to be even */
	ASSERT((offset & 1) == 0);
	ASSERT((len & 1) == 0);

	if (len <= 0)
		return;

	/* Some of the OBJADDR memories can be accessed as 4 byte
	 * and some as 2 byte
	 */
	if (sel & (OBJADDR_RCMTA_SEL | OBJADDR_AMT_SEL)) {
		int len16 = (len%4);
		int len32 = (len/4)*4;

		/* offset needs to be multiple of 4 here */
		ASSERT((offset & 3) == 0);

		/* Write all the 32bit words */
		for (i = 0; i < len32; i += 4) {
			v32 = htol32(p[i] | (p[i+1] << 8) | (p[i+2] << 16) | (p[i+3] << 24));
			wlc_bmac_write_objmem32(wlc_hw, offset + i, v32, sel);
		}

		/* Write the last 16bit if any */
		if (len16) {
			v16 = htol16(p[i] | (p[i+1] << 8));
			wlc_bmac_write_objmem16(wlc_hw, offset + i, v16, sel);
		}

	} else {

		for (i = 0; i < len; i += 2) {
			v16 = htol16(p[i] | (p[i+1] << 8));
			wlc_bmac_write_objmem16(wlc_hw, offset + i, v16, sel);
		}
	}
}

/**
 * Copy a piece of shared memory of specified type to a buffer .
 * SHM 'offset' needs to be an even address and
 * Buffer length 'len' must be an even number of bytes
 * 'sel' selects the type of memory
 */
void
wlc_bmac_copyfrom_objmem(wlc_hw_info_t *wlc_hw, uint offset, void* buf, int len, uint32 sel)
{
	uint8* p = (uint8*)buf;
	int i;
	uint16 v16;
	uint32 v32;

	/* offset and len need to be even */
	ASSERT((offset & 1) == 0);
	ASSERT((len & 1) == 0);

	if (len <= 0)
		return;

	/* Some of the OBJADDR memories can be accessed as 4 byte
	 * and some as 2 byte
	 */
	if (sel & (OBJADDR_RCMTA_SEL | OBJADDR_AMT_SEL)) {
		int len16 = (len%4);
		int len32 = (len/4)*4;

		/* offset needs to be multiple of 4 here */
		ASSERT((offset & 3) == 0);

		/* Read all the 32bit words */
		for (i = 0; i < len32; i += 4) {
			v32 = ltoh32(wlc_bmac_read_objmem32(wlc_hw, offset + i, sel));
			p[i] = v32 & 0xFF;
			p[i+1] = (v32 >> 8) & 0xFF;
			p[i+2] = (v32 >> 16) & 0xFF;
			p[i+3] = (v32 >> 24) & 0xFF;
		}

		/* Read the last 16bit if any */
		if (len16) {
			v16 = ltoh16(wlc_bmac_read_objmem16(wlc_hw, offset + i, sel));
			p[i] = v16 & 0xFF;
			p[i+1] = (v16 >> 8) & 0xFF;
		}

	} else {

		for (i = 0; i < len; i += 2) {
			v16 = ltoh16(wlc_bmac_read_objmem16(wlc_hw, offset + i, sel));
			p[i] = v16 & 0xFF;
			p[i+1] = (v16 >> 8) & 0xFF;
		}
	}
} /* wlc_bmac_copyfrom_objmem */

void
wlc_bmac_copyfrom_vars(wlc_hw_info_t *wlc_hw, char ** buf, uint *len)
{
	WL_TRACE(("wlc_bmac_copyfrom_vars, nvram vars totlen=%d\n", wlc_hw->vars_size));

	if (wlc_hw->vars) {
		*buf = wlc_hw->vars;
		*len = wlc_hw->vars_size;
	}
}

void
wlc_bmac_retrylimit_upd(wlc_hw_info_t *wlc_hw, uint16 SRL, uint16 LRL)
{
	wlc_hw->SRL = SRL;
	wlc_hw->LRL = LRL;

	/* write retry limit to SCR, shouldn't need to suspend */
	if (wlc_hw->up) {
		wlc_bmac_copyto_objmem(wlc_hw, S_DOT11_SRC_LMT << 2, &(wlc_hw->SRL),
			sizeof(wlc_hw->SRL), OBJADDR_SCR_SEL);

		wlc_bmac_copyto_objmem(wlc_hw, S_DOT11_LRC_LMT << 2, &(wlc_hw->LRL),
			sizeof(wlc_hw->LRL), OBJADDR_SCR_SEL);
	}
}

void
wlc_bmac_set_noreset(wlc_hw_info_t *wlc_hw, bool noreset_flag)
{
	wlc_hw->noreset = noreset_flag;
}

bool
wlc_bmac_get_noreset(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->noreset;
}

bool
wlc_bmac_p2p_cap(wlc_hw_info_t *wlc_hw)
{
#ifdef WLP2P_UCODE
	return wlc_hw->corerev >= 15;
#else
	return FALSE;
#endif
}

int
wlc_bmac_p2p_set(wlc_hw_info_t *wlc_hw, bool enable)
{
	if (wlc_hw->_p2p == enable)
		return BCME_OK;
	if (enable &&
	    !wlc_bmac_p2p_cap(wlc_hw))
		return BCME_ERROR;
#ifdef WLP2P_UCODE
#ifdef WLP2P_UCODE_ONLY
	if (!enable)
		return BCME_ERROR;
#endif
	wlc_hw->ucode_loaded = FALSE;
	wlc_hw->_p2p = enable;
#endif /* WLP2P_UCODE */
	return BCME_OK;
}

void
wlc_bmac_pllreq(wlc_hw_info_t *wlc_hw, bool set, mbool req_bit)
{
	ASSERT(req_bit);

	if (set) {
		if (mboolisset(wlc_hw->pllreq, req_bit))
			return;

		mboolset(wlc_hw->pllreq, req_bit);

		if (mboolisset(wlc_hw->pllreq, WLC_PLLREQ_FLIP)) {
			if (!wlc_hw->sbclk) {
				wlc_bmac_xtal(wlc_hw, ON);
			}
		}
	} else {
		if (!mboolisset(wlc_hw->pllreq, req_bit))
			return;

		mboolclr(wlc_hw->pllreq, req_bit);

		if (mboolisset(wlc_hw->pllreq, WLC_PLLREQ_FLIP)) {
			if (wlc_hw->sbclk) {
				wlc_bmac_xtal(wlc_hw, OFF);
			}
		}
	}

	return;
}

void
wlc_bmac_set_clk(wlc_hw_info_t *wlc_hw, bool on)
{
	if (on) {
		/* power up pll and oscillator */
		wlc_bmac_xtal(wlc_hw, ON);

		/* enable core(s), ignore bandlocked
		 * Leave with the same band selected as we entered
		 */
		wlc_bmac_corereset(wlc_hw, WLC_USE_COREFLAGS);
	} else {
		/* if already down, must skip the core disable */
		if (wlc_hw->clk) {
			/* disable core(s), ignore bandlocked */
			wlc_coredisable(wlc_hw);
		}
			/* power down pll and oscillator */
		wlc_bmac_xtal(wlc_hw, OFF);
	}
}


#ifdef WLLED
/** may touch sb register inside */
void
wlc_bmac_led_hw_deinit(wlc_hw_info_t *wlc_hw, uint32 gpiomask_cache)
{
	/* BMAC_NOTE: split mac should not worry about pci cfg access to disable GPIOs. */
	bool xtal_set = FALSE;

	if (!wlc_hw->sbclk) {
		wlc_bmac_xtal(wlc_hw, ON);
		xtal_set = TRUE;
	}

	/* opposite sequence of wlc_led_init */
	if (wlc_hw->sih) {
		si_gpioout(wlc_hw->sih, gpiomask_cache, 0, GPIO_DRV_PRIORITY);
		si_gpioouten(wlc_hw->sih, gpiomask_cache, 0, GPIO_DRV_PRIORITY);
		si_gpioled(wlc_hw->sih, gpiomask_cache, 0);
	}

	if (xtal_set)
		wlc_bmac_xtal(wlc_hw, OFF);
}

void
wlc_bmac_led_hw_mask_init(wlc_hw_info_t *wlc_hw, uint32 mask)
{
	wlc_hw->led_gpio_mask = mask;
}

static void
wlc_bmac_led_hw_init(wlc_hw_info_t *wlc_hw)
{
	uint32 mask = wlc_hw->led_gpio_mask, val = 0;
	struct bmac_led *led;
	bmac_led_info_t *li = wlc_hw->ledh;


	if (!wlc_hw->sbclk)
		return;

	/* designate gpios driving LEDs . Make sure that we have the control */
	si_gpiocontrol(wlc_hw->sih, mask, 0, GPIO_DRV_PRIORITY);
	si_gpioled(wlc_hw->sih, mask, mask);

	/* Begin with LEDs off */
	for (led = &li->led[0]; led < &li->led[WL_LED_NUMGPIO]; led++) {
		if (!led->activehi)
			val |= (1 << led->pin);
	}
	val = val & mask;

	if (!(wlc_hw->boardflags2 & BFL2_TRISTATE_LED)) {
		li->gpioout_cache = si_gpioout(wlc_hw->sih, mask, val, GPIO_DRV_PRIORITY);
		si_gpioouten(wlc_hw->sih, mask, mask, GPIO_DRV_PRIORITY);
	} else {
		si_gpioout(wlc_hw->sih, mask, ~val & mask, GPIO_DRV_PRIORITY);
		li->gpioout_cache = si_gpioouten(wlc_hw->sih, mask, 0, GPIO_DRV_PRIORITY);
		/* for tristate leds, clear gpiopullup/gpiopulldown registers to
		 * allow the tristated gpio to float
		 */
		if (CCREV(wlc_hw->sih->ccrev) >= 20) {
			si_gpiopull(wlc_hw->sih, GPIO_PULLDN, mask, 0);
			si_gpiopull(wlc_hw->sih, GPIO_PULLUP, mask, 0);
		}
	}

	li->gpiomask_cache = mask;

	/* set override bit for the GPIO line controlling the LED */
	val = 0;
	for (led = &li->led[0]; led < &li->led[WL_LED_NUMGPIO]; led++) {
		if (led->pin_ledbh) {
			if (val == 0) {
				val = si_pmu_chipcontrol(wlc_hw->sih, PMU_CHIPCTL1, 0, 0);
			}

			val |= (1 << (PMU_CCA1_OVERRIDE_BIT_GPIO0 + led->pin));
		}
	}

	if (val) {
		si_pmu_chipcontrol(wlc_hw->sih, PMU_CHIPCTL1, 0xFFFFFFFF, val);
	}
} /* wlc_bmac_led_hw_init */

/** called by the led_blink_timer at every li->led_blink_time interval */
static void
wlc_bmac_led_blink_timer(bmac_led_info_t *li)
{
	struct bmac_led *led;
#if OSL_SYSUPTIME_SUPPORT
	uint32 now = OSL_SYSUPTIME();
	/* Timer event can come early, and the LED on/off state change will be missed until the
	 * next li->led_blink_time cycle. Thus, the LED on/off state could be extended. To adjust
	 * for this situation, LED time may need to restart at the end of the current
	 * li->led_blink_time cycle
	 */
	wlc_hw_info_t *wlc_hw = (wlc_hw_info_t *)li->wlc_hw;
	uint time_togo;
	uint restart_time = 0;
	uint time_passed;

	/* blink each pin at its respective blinkrate */
	for (led = &li->led[0]; led < &li->led[WL_LED_NUMGPIO]; led++) {
		if (led->msec_on || led->msec_off) {
			bool change_state = FALSE;
			uint factor;

			time_passed = now - led->timestamp;

			/* Currently off */
			if ((led->next_state) || (led->restart)) {
				if (time_passed > led->msec_off)
					change_state = TRUE;
				else {
					time_togo = led->msec_off - time_passed;
					factor = (led->msec_off > 1000) ? 20 : 10;
					if (time_togo < li->led_blink_time) {
						if (time_togo < led->msec_off/factor ||
							time_togo < LED_BLINK_TIME) {
							if (li->led_blink_time - time_togo >
								li->led_blink_time/10)
								change_state = TRUE;
						} else {
							if (!restart_time)
								restart_time = time_togo;
							else if (time_togo < restart_time)
								restart_time = time_togo;
						}
					}
				}

				/* Blink on */
				if (led->restart || change_state) {
					wlc_bmac_led((wlc_hw_info_t*)li->wlc_hw,
					             (1<<led->pin), (1<<led->pin), led->activehi);
					led->next_state = OFF;
					led->timestamp = now;
					led->restart = FALSE;
				}
			}
			/* Currently on */
			else {
				if (time_passed > led->msec_on)
					change_state = TRUE;
				else {
							time_togo = led->msec_on - time_passed;
					if (time_togo < li->led_blink_time) {
						factor = (led->msec_on > 1000) ? 20 : 10;
						if (time_togo < led->msec_on/factor ||
							time_togo < LED_BLINK_TIME) {
							if (li->led_blink_time - time_togo >
								li->led_blink_time/10)
								change_state = TRUE;
						} else {
							if (!restart_time)
								restart_time = time_togo;
							else if (time_togo < restart_time)
								restart_time = time_togo;
						}
					}
				}

				/* Blink off  */
				if (change_state) {
					wlc_bmac_led((wlc_hw_info_t*)li->wlc_hw,
					             (1<<led->pin), 0, led->activehi);
					led->next_state = ON;
					led->timestamp = now;
				}
			}
		}
	}

	if (restart_time) {
		wl_del_timer(wlc_hw->wlc->wl, li->led_blink_timer);
		wl_add_timer(wlc_hw->wlc->wl, li->led_blink_timer, restart_time, 0);
		li->blink_start = TRUE;
		li->blink_adjust = TRUE;
	} else if (li->blink_adjust) {
		wlc_bmac_led_blink_event(wlc_hw, TRUE);
		li->blink_start = TRUE;
		li->blink_adjust = FALSE;
	}
#else
	for (led = &li->led[0]; led < &li->led[WL_LED_NUMGPIO]; led++) {
		if (led->blinkmsec) {
			if (led->blinkmsec > (int32) led->msec_on) {
				wlc_bmac_led((wlc_hw_info_t*)li->wlc_hw,
				             (1<<led->pin), 0, led->activehi);
			} else {
				wlc_bmac_led((wlc_hw_info_t*)li->wlc_hw,
				             (1<<led->pin), (1<<led->pin), led->activehi);
			}
			led->blinkmsec -= LED_BLINK_TIME;
			if (led->blinkmsec <= 0)
				led->blinkmsec = led->msec_on + led->msec_off;
		}
	}
#endif /* (OSL_SYSUPTIME_SUPPORT) */
} /* wlc_bmac_led_blink_timer */

static void
wlc_bmac_timer_led_blink(void *arg)
{
	wlc_info_t *wlc = (wlc_info_t*)arg;
	wlc_hw_info_t *wlc_hw = wlc->hw;

	if (DEVICEREMOVED(wlc)) {
		WL_ERROR(("wl%d: %s: dead chip\n", wlc_hw->unit, __FUNCTION__));
		wl_down(wlc->wl);
		return;
	}

	wlc_bmac_led_blink_timer(wlc_hw->ledh);
}


bmac_led_info_t *
BCMATTACHFN(wlc_bmac_led_attach)(wlc_hw_info_t *wlc_hw)
{
	bmac_led_info_t *bmac_li;
	bmac_led_t *led;
	int i;
	char name[32];
	char *var;
	uint val;

	if ((bmac_li = (bmac_led_info_t *)MALLOC
			(wlc_hw->osh, sizeof(bmac_led_info_t))) == NULL) {
		printf(rstr_bmac_led_attach_out_of_mem_malloced_D_bytes,
			MALLOCED(wlc_hw->osh));
		goto fail;
	}
	bzero((char *)bmac_li, sizeof(bmac_led_info_t));

	led = &bmac_li->led[0];
	for (i = 0; i < WL_LED_NUMGPIO; i ++) {
		led->pin = i;
		led->activehi = TRUE;
#if OSL_SYSUPTIME_SUPPORT
		/* current time, in ms, for computing LED blink duration */
		led->timestamp = OSL_SYSUPTIME();
		led->next_state = ON; /* default to turning on */
#endif
		led ++;
	}

	/* look for led gpio/behavior nvram overrides */
	for (i = 0; i < WL_LED_NUMGPIO; i++) {
		led = &bmac_li->led[i];

		snprintf(name, sizeof(name), rstr_ledbhD, i);

		if ((var = getvar(wlc_hw->vars, name)) == NULL) {
			snprintf(name, sizeof(name), rstr_wl0gpioD, i);
			if ((var = getvar(wlc_hw->vars, name)) == NULL) {
				continue;
			}
		}

		val = bcm_strtoul(var, NULL, 0);

		/* silently ignore old card srom garbage */
		if ((val & WL_LED_BEH_MASK) >= WL_LED_NUMBEHAVIOR)
			continue;

		led->pin = i;	/* gpio pin# == led index# */
		if (val & WL_LED_PMU_OVERRIDE) {
			led->pin_ledbh = TRUE;
		}
		led->activehi = (val & WL_LED_AL_MASK)? FALSE : TRUE;
	}

	bmac_li->wlc_hw = wlc_hw;
	if (!(bmac_li->led_blink_timer = wl_init_timer
			(wlc_hw->wlc->wl, wlc_bmac_timer_led_blink, wlc_hw->wlc,
	                                          "led_blink"))) {
		printf(rstr_wlD_led_attach_wl_init_timer_for_led_blink_timer_failed,
			wlc_hw->unit);
		goto fail;
	}

#if !OSL_SYSUPTIME_SUPPORT
	bmac_li->led_blink_time = LED_BLINK_TIME;
#endif

	return bmac_li;

fail:
	if (bmac_li) {
		MFREE(wlc_hw->osh, bmac_li, sizeof(bmac_led_info_t));
	}
	return NULL;
} /* wlc_bmac_led_attach */

int
BCMATTACHFN(wlc_bmac_led_detach)(wlc_hw_info_t *wlc_hw)
{
	bmac_led_info_t *li = wlc_hw->ledh;
	int callbacks = 0;

	if (li) {
		if (li->led_blink_timer) {
			if (!wl_del_timer(wlc_hw->wlc->wl, li->led_blink_timer))
				callbacks++;
			wl_free_timer(wlc_hw->wlc->wl, li->led_blink_timer);
			li->led_blink_timer = NULL;
		}

		MFREE(wlc_hw->osh, li, sizeof(bmac_led_info_t));
	}

	return callbacks;
}

static void
wlc_bmac_led_blink_off(bmac_led_info_t *li)
{
	struct bmac_led *led;

	/* blink each pin at its respective blinkrate */
	for (led = &li->led[0]; led < &li->led[WL_LED_NUMGPIO]; led++) {
		if (led->msec_on || led->msec_off) {
			wlc_bmac_led((wlc_hw_info_t*)li->wlc_hw,
				(1<<led->pin), 0, led->activehi);
#if OSL_SYSUPTIME_SUPPORT
			led->restart = TRUE;
#endif
		}
	}
}

int
wlc_bmac_led_blink_event(wlc_hw_info_t *wlc_hw, bool blink)
{
	bmac_led_info_t *li = (bmac_led_info_t *)(wlc_hw->ledh);

	if (blink) {
		wl_del_timer(wlc_hw->wlc->wl, li->led_blink_timer);
		wl_add_timer(wlc_hw->wlc->wl, li->led_blink_timer, li->led_blink_time, 1);
		li->blink_start = TRUE;
	} else {
		if (!wl_del_timer(wlc_hw->wlc->wl, li->led_blink_timer))
			return 1;
		li->blink_start = FALSE;
		wlc_bmac_led_blink_off(li);
	}
	return 0;
}

void
wlc_bmac_led_set(wlc_hw_info_t *wlc_hw, int indx, uint8 activehi)
{
	bmac_led_t *led = &wlc_hw->ledh->led[indx];

	led->activehi = activehi;

	return;
}

void
wlc_bmac_led_blink(wlc_hw_info_t *wlc_hw, int indx, uint16 msec_on, uint16 msec_off)
{
	bmac_led_t *led = &wlc_hw->ledh->led[indx];
#if OSL_SYSUPTIME_SUPPORT
	bmac_led_info_t *li = (bmac_led_info_t *)(wlc_hw->ledh);
	uint num_leds_set = 0;
	uint led_blink_rates[WL_LED_NUMGPIO];
	uint tmp, a, b, i;
	led_blink_rates[0] = 1000; /* 1 sec, default timer */
#endif

	led->msec_on = msec_on;
	led->msec_off = msec_off;

#if !OSL_SYSUPTIME_SUPPORT
	led->blinkmsec = msec_on + msec_off;
#else
	if ((led->msec_on != msec_on) || (led->msec_off != msec_off)) {
		led->restart = TRUE;
	}

	/* recompute to an optimized blink rate timer interval */
	for (led = &li->led[0]; led < &li->led[WL_LED_NUMGPIO]; led++) {
		if (!(led->msec_on || led->msec_off)) {
			led->restart = TRUE;
			continue;
		}

		/* compute the GCF of this particular LED's on+off rates */
		b = led->msec_off;
		a = led->msec_on;
		while (b != 0) {
			tmp = b;
			b = a % b;
			a = tmp;
		}

		led_blink_rates[num_leds_set++] = a;
	}

	/* compute the GCF across all LEDs, if more than one */
	a = led_blink_rates[0];

	for (i = 1; i < num_leds_set; i++) {
		b = led_blink_rates[i];
		while (b != 0) {
			tmp = b;
			b = a % b;
			a = tmp; /* A is the running GCF */
		}
	}

	li->led_blink_time = MAX(a, LED_BLINK_TIME);

	if (num_leds_set) {
		if ((li->blink_start) && !li->blink_adjust) {
			wlc_bmac_led_blink_event(wlc_hw, FALSE);
			wlc_bmac_led_blink_event(wlc_hw, TRUE);
		}
	}

#endif /* !(OSL_SYSUPTIME_SUPPORT) */
	return;
} /* wlc_bmac_led_blink */

void
wlc_bmac_blink_sync(wlc_hw_info_t *wlc_hw, uint32 led_pins)
{
#if OSL_SYSUPTIME_SUPPORT
	bmac_led_info_t *li = wlc_hw->ledh;
	int i;

	for (i = 0; i < WL_LED_NUMGPIO; i++) {
		if (led_pins & (0x1 << i)) {
			li->led[i].restart = TRUE;
		}
	}
#endif

	return;
}

/** turn gpio bits on or off */
void
wlc_bmac_led(wlc_hw_info_t *wlc_hw, uint32 mask, uint32 val, bool activehi)
{
	bmac_led_info_t *li = wlc_hw->ledh;
	bool off = (val != mask);

	ASSERT((val & ~mask) == 0);

	if (!wlc_hw->sbclk)
		return;

	if (!activehi)
		val = ((~val) & mask);

	/* Tri-state the GPIO if the board flag is set */
	if (wlc_hw->boardflags2 & BFL2_TRISTATE_LED) {
		if ((!activehi && ((val & mask) == (li->gpioout_cache & mask))) ||
		    (activehi && ((val & mask) != (li->gpioout_cache & mask))))
			li->gpioout_cache = si_gpioouten(wlc_hw->sih, mask, off ? 0 : mask,
			                                 GPIO_DRV_PRIORITY);
	} else {
		/* prevent the unnecessary writes to the gpio */
		if ((val & mask) != (li->gpioout_cache & mask))
			/* Traditional GPIO behavior */
			li->gpioout_cache = si_gpioout(wlc_hw->sih, mask, val,
			                               GPIO_DRV_PRIORITY);
	}
}
#endif /* WLLED */

int
wlc_bmac_dispatch_iov(wlc_hw_info_t *wlc_hw, uint16 tid, uint32 aid, uint16 type,
	void *p, uint plen, void *a, int alen, int vsize)
{
	BCM_REFERENCE(type);
	return wlc_iocv_dispatch_iov(wlc_hw->iocvi, tid, aid, p, plen, a, alen, vsize);
}

int
wlc_bmac_dispatch_ioc(wlc_hw_info_t *wlc_hw, uint16 tid, uint16 cid, uint16 type,
	void *a, uint alen, bool *ta)
{
	BCM_REFERENCE(type);
	return wlc_iocv_dispatch_ioc(wlc_hw->iocvi, tid, cid, a, alen, ta);
}

#if defined(BCMDBG_PHYDUMP)
#endif 

#if defined(BCMDBG_PHYDUMP)
/** register a dump name/callback in bmac */
static int
wlc_bmac_add_dump_fn(wlc_hw_info_t *wlc_hw, const char *name,
	bmac_dump_fn_t fn, const void *ctx)
{
/* unfortunately no way of disposing of const cast - the 'ctx'
 * is a parameter passed back to the callback as it can't force
 * the callback to take a const void *ctx in case the callback
 * does need to modify the object so let's leave that protection
 * to the callback itself.
 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	return wlc_dump_reg_add_fn(wlc_hw->dump, name, (wlc_dump_reg_fn_t)fn, (void *)ctx);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
}
#endif 

#if defined(BCMDBG_PHYDUMP)
/** lookup a dump name in phy and execute it if found */
static int
wlc_bmac_dump_phy(wlc_hw_info_t *wlc_hw, const char *name, struct bcmstrbuf *b)
{
	int ret = BCME_UNSUPPORTED;
	bool ta_ok = FALSE;

#if defined(DBG_PHY_IOV)
	if (!strcmp(name, "radioreg") || !strcmp(name, "phyreg") || !strcmp(name, "phytbl")) {
		ret = phy_dbg_dump((phy_info_t *)wlc_hw->band->pi, name, b);
		ta_ok = TRUE;
	} else
#endif 
	{
	bool single_phy, a_only;
	single_phy = (wlc_hw->bandstate[0]->pi == wlc_hw->bandstate[1]->pi) ||
	        (wlc_hw->bandstate[1]->pi == NULL);

	a_only = (wlc_hw->bandstate[0]->pi == NULL);

	if (wlc_hw->bandstate[0]->pi)
		ret = phy_dbg_dump((phy_info_t *)wlc_hw->bandstate[0]->pi, name, b);
	if (!single_phy || a_only)
		ret = phy_dbg_dump((phy_info_t *)wlc_hw->bandstate[1]->pi, name, b);
	}

	ASSERT(wlc_bmac_taclear(wlc_hw, ta_ok) || !ta_ok);
	BCM_REFERENCE(ta_ok);
	return ret;
}
#endif 

/** register bmac/si dump names */
static int
wlc_bmac_register_dumps(wlc_hw_info_t *wlc_hw)
{

#if defined(BCMDBG_PHYDUMP)
	wlc_bmac_add_dump_fn(wlc_hw, "macsuspend", (bmac_dump_fn_t)wlc_bmac_suspend_dump, wlc_hw);
#endif 


	return BCME_OK;
}

int
wlc_bmac_dump(wlc_hw_info_t *wlc_hw, const char *name, struct bcmstrbuf *b)
{
	int ret = BCME_NOTFOUND;

#if defined(BCMDBG_PHYDUMP)
	/* dump if 'name' is a bmac/si dump */
	ret = wlc_dump_reg_invoke_dump_fn(wlc_hw->dump, name, b);
	if (ret == BCME_NOTFOUND)
		/* dump if 'name' is a phy dump */
		ret = wlc_bmac_dump_phy(wlc_hw, name, b);
#endif 

	return ret;
}

int
wlc_bmac_dump_clr(wlc_hw_info_t *wlc_hw, const char *name)
{
	int ret = BCME_NOTFOUND;

#if defined(BCMDBG_PHYDUMP)
	/* dump if 'name' is a bmac/si dump */
	ret = wlc_dump_reg_invoke_clr_fn(wlc_hw->dump, name);
#endif 

	return ret;
}


#if defined(WLPKTENG)
int
wlc_bmac_pkteng(wlc_hw_info_t *wlc_hw, wl_pkteng_t *pkteng, void* p)
{
	wlc_phy_t *pi = wlc_hw->band->pi;
	uint32 cmd;
	bool is_sync;
	uint16 pkteng_mode;
	uint err = BCME_OK;

#if defined(WLP2P_UCODE)
	if (DL_P2P_UC(wlc_hw) && (CHIPID(wlc_hw->sih->chip) != BCM4360_CHIP_ID) &&
		(CHIPID(wlc_hw->sih->chip) != BCM4335_CHIP_ID) &&
		(CHIPID(wlc_hw->sih->chip) != BCM4345_CHIP_ID) &&
		!BCM43602_CHIP(wlc_hw->sih->chip) &&
		!BCM4349_CHIP(wlc_hw->sih->chip) &&
		!BCM53573_CHIP(wlc_hw->sih->chip) &&
		!BCM4350_CHIP(wlc_hw->sih->chip) &&
		(CHIPID(wlc_hw->sih->chip) != BCM43012_CHIP_ID) &&
		1) {
		WL_ERROR(("p2p-ucode does not support pkteng\n"));
		if (p)
			PKTFREE(wlc_hw->osh, p, TRUE);
		return BCME_UNSUPPORTED;
	}
#endif /* WLP2P_UCODE */

	cmd = pkteng->flags & WL_PKTENG_PER_MASK;
	is_sync = (pkteng->flags & WL_PKTENG_SYNCHRONOUS) ? TRUE : FALSE;

	switch (cmd) {
	case WL_PKTENG_PER_RX_START:
	case WL_PKTENG_PER_RX_WITH_ACK_START:
	{
#if defined(WLCNT)
		uint32 pktengrxducast_start = 0;
		wlc_pub_t	*pub = wlc_hw->wlc->pub;
#endif /* WLCNT */
		/* Reset the counters */
		wlc_bmac_write_shm(wlc_hw, M_MFGTEST_FRMCNT_LO(wlc_hw), 0);
		wlc_bmac_write_shm(wlc_hw, M_MFGTEST_FRMCNT_HI(wlc_hw), 0);
		wlc_phy_hold_upd(pi, PHY_HOLD_FOR_PKT_ENG, TRUE);
		if (!is_sync)
			wlc_bmac_mhf(wlc_hw, MHF3, MHF3_PKTENG_PROMISC,
				MHF3_PKTENG_PROMISC, WLC_BAND_ALL);

		if (is_sync) {
#if defined(WLCNT)
			/* get counter value before start of pkt engine */
			wlc_ctrupd(wlc_hw->wlc, MCSTOFF_RXGOODUCAST);
			pktengrxducast_start = WLCNTVAL(MCSTVAR(pub, pktengrxducast));
#else
			/* BMAC_NOTE: need to split wlc_ctrupd before supporting this in bmac */
			ASSERT(0);
#endif /* WLCNT */
		}

		pkteng_mode = (cmd == WL_PKTENG_PER_RX_START) ?
			M_PKTENG_MODE_RX(wlc_hw): M_PKTENG_MODE_RX_WITH_ACK(wlc_hw);

		wlc_bmac_write_shm(wlc_hw, M_MFGTEST_NUM(wlc_hw), pkteng_mode);

		/* This is to enable averaging of RSSI value in the ucode
		  * and initialize the results to zero
		  */
		if (D11REV_IS(wlc_hw->corerev, 48) || D11REV_IS(wlc_hw->corerev, 47) ||
			D11REV_IS(wlc_hw->corerev, 51) || D11REV_IS(wlc_hw->corerev, 49)) {
			uint16 p2pblk_strt = wlc_bmac_read_shm(wlc_hw, M_P2P_BLK_PTR(wlc_hw)) << 1;
			wlc_bmac_write_shm(wlc_hw, (p2pblk_strt +
				M_PKTENG_RXAVGPWR_ANT(wlc_hw, 0)), 0);
			wlc_bmac_write_shm(wlc_hw, (p2pblk_strt +
				M_PKTENG_RXAVGPWR_ANT(wlc_hw, 1)), 0);
			if (D11REV_IS(wlc_hw->corerev, 49))
				wlc_bmac_write_shm(wlc_hw, (p2pblk_strt
				+ M_PKTENG_RXAVGPWR_ANT(wlc_hw, 2)), 0);
		}
		/* set RA match reg with dest addr */
		wlc_bmac_set_match_mac(wlc_hw, &pkteng->dest);

#if defined(WLCNT)
		/* wait for counter for synchronous receive with a maximum total delay */
		if (is_sync) {
			/* loop delay in msec */
			uint32 delay_msec = 1;
			/* avoid calculation in loop */
			uint32 delay_usec = delay_msec * 1000;
			uint32 total_delay = 0;
			uint32 delta;
			do {
				OSL_DELAY(delay_usec);
				total_delay += delay_msec;
				wlc_ctrupd(wlc_hw->wlc, MCSTOFF_RXGOODUCAST);
				if (WLCNTVAL(MCSTVAR(pub, pktengrxducast))
					> pktengrxducast_start) {
					delta = WLCNTVAL(MCSTVAR(pub, pktengrxducast)) -
						pktengrxducast_start;
				} else {
					/* counter overflow */
					delta = (~pktengrxducast_start + 1) +
						WLCNTVAL(MCSTVAR(pub, pktengrxducast));
				}
			} while (delta < pkteng->nframes && total_delay < pkteng->delay);

			wlc_phy_hold_upd(pi, PHY_HOLD_FOR_PKT_ENG, FALSE);
			/* implicit rx stop after synchronous receive */
			wlc_bmac_write_shm(wlc_hw, M_MFGTEST_NUM(wlc_hw), 0);
			wlc_bmac_set_match_mac(wlc_hw, &wlc_hw->etheraddr);
		}
#endif /* WLCNT */

		break;
	}

	case WL_PKTENG_PER_RX_STOP:
		WL_INFORM(("Pkteng RX Stop Called\n"));
		wlc_bmac_write_shm(wlc_hw, M_MFGTEST_NUM(wlc_hw), 0);
		wlc_bmac_mhf(wlc_hw, MHF3, MHF3_PKTENG_PROMISC,
			0, WLC_BAND_ALL);
		wlc_phy_hold_upd(pi, PHY_HOLD_FOR_PKT_ENG, FALSE);
		/* Restore match address register */
		wlc_bmac_set_match_mac(wlc_hw, &wlc_hw->etheraddr);

		break;

	case WL_PKTENG_PER_TX_START:
	case WL_PKTENG_PER_TX_WITH_ACK_START:
	{
		uint16 val = M_PKTENG_MODE_TX(wlc_hw);

		WL_INFORM(("Pkteng TX Start Called\n"));

		ASSERT(p != NULL);
		if ((pkteng->delay < 15) || (pkteng->delay > 1000)) {
			WL_ERROR(("delay out of range, freeing the packet\n"));
			PKTFREE(wlc_hw->osh, p, TRUE);
			err = BCME_RANGE;
			break;
		}

		wlc_phy_hold_upd(pi, PHY_HOLD_FOR_PKT_ENG, TRUE);
		wlc_bmac_suspend_mac_and_wait(wlc_hw);

		if (WLCISLCNPHY(wlc_hw->band)) {
			wlc_phy_set_deaf(pi, TRUE);
		} else {
			/*
			 * mute the rx side for the regular TX.
			 * tx_with_ack mode makes the ucode update rxdfrmucastmbss count
			 */
			if (cmd == WL_PKTENG_PER_TX_START) {
				wlc_phy_set_deaf(pi, TRUE);
			} else {
				wlc_phy_clear_deaf(pi, TRUE);
			}
		}

		/* set nframes */
		if (pkteng->nframes) {
			wlc_bmac_write_shm(wlc_hw, M_MFGTEST_FRMCNT_LO(wlc_hw),
				(pkteng->nframes & 0xffff));
			wlc_bmac_write_shm(wlc_hw, M_MFGTEST_FRMCNT_HI(wlc_hw),
				((pkteng->nframes>>16) & 0xffff));
			val |= M_PKTENG_FRMCNT_VLD(wlc_hw);
		}

		wlc_bmac_write_shm(wlc_hw, M_MFGTEST_NUM(wlc_hw), val);

		/* we write to M_MFGTEST_IFS the IFS required in 1/8us factor */
		/* 10 : for factoring difference b/w Tx.crs and energy in air */
		/* 44 : amount of time spent after TX_RRSP to frame start */
		/* IFS */
		wlc_bmac_write_shm(wlc_hw, M_MFGTEST_IO1(wlc_hw), (pkteng->delay - 10)*8 - 44);
		if (is_sync)
			wlc_bmac_mctrl(wlc_hw, MCTL_DISCARD_TXSTATUS, 1 << 29);
		wlc_bmac_enable_mac(wlc_hw);

		/* Do the low part of wlc_txfifo() */
		wlc_bmac_txfifo(wlc_hw, TX_DATA_FIFO, p, TRUE, INVALIDFID, 1);

		/* wait for counter for synchronous transmit */
		if (is_sync) {
			int i;
			do {
				OSL_DELAY(1000);
				i = wlc_bmac_read_shm(wlc_hw, M_MFGTEST_NUM(wlc_hw));
			} while (i & M_PKTENG_MODE_TX(wlc_hw));

			wlc_bmac_suspend_mac_and_wait(wlc_hw);
			wlc_bmac_mctrl(wlc_hw, MCTL_DISCARD_TXSTATUS, 0);
			wlc_bmac_enable_mac(wlc_hw);
			/* implicit tx stop after synchronous transmit */
			wlc_phy_clear_deaf(pi, (bool)1);
			wlc_phy_hold_upd(pi, PHY_HOLD_FOR_PKT_ENG, FALSE);
			wlc_bmac_service_txstatus(wlc_hw);
			p = dma_getnexttxp(wlc_hw->di[TX_DATA_FIFO], HNDDMA_RANGE_TRANSMITTED);
			ASSERT(p != NULL);
			PKTFREE(wlc_hw->osh, p, TRUE);
			/* Decrementing txpktpend with '1' since wlc_bmac_txfifo
			 * is also called with txpktpend = 1
			 */
			TXPKTPENDDEC(wlc_hw->wlc, TX_DATA_FIFO, 1);
		}

		break;
	}

	case WL_PKTENG_PER_TX_STOP:
	{
		int status;

		ASSERT(p == NULL);

		WL_INFORM(("Pkteng TX Stop Called\n"));

		/* Check pkteng state */
		status = wlc_bmac_read_shm(wlc_hw, M_MFGTEST_NUM(wlc_hw));
		if (status & M_PKTENG_MODE_TX(wlc_hw)) {
			uint16 val = M_PKTENG_MODE_TX(wlc_hw);

			/* Still running
			 * Stop cleanly by setting frame count
			 */
			wlc_bmac_suspend_mac_and_wait(wlc_hw);
			wlc_bmac_write_shm(wlc_hw, M_MFGTEST_FRMCNT_LO(wlc_hw), 1);
			wlc_bmac_write_shm(wlc_hw, M_MFGTEST_FRMCNT_HI(wlc_hw), 0);
			val |= M_PKTENG_FRMCNT_VLD(wlc_hw);
			wlc_bmac_write_shm(wlc_hw, M_MFGTEST_NUM(wlc_hw), val);
			wlc_bmac_enable_mac(wlc_hw);

			/* Wait for the pkteng to stop */
			do {
				OSL_DELAY(1000);
				status = wlc_bmac_read_shm(wlc_hw, M_MFGTEST_NUM(wlc_hw));
			} while (status & M_PKTENG_MODE_TX(wlc_hw));
		}


		/* Clean up */
		wlc_phy_clear_deaf(pi, (bool)1);
		wlc_phy_hold_upd(pi, PHY_HOLD_FOR_PKT_ENG, FALSE);
		break;
	}

	default:
		err = BCME_UNSUPPORTED;
	}

	return err;
} /* wlc_bmac_pkteng */
#endif 

/**
 * Lower down relevant GPIOs like LED/BTC when going down w/o
 * doing PCI config cycles or touching interrupts
 */
void
wlc_gpio_fast_deinit(wlc_hw_info_t *wlc_hw)
{
	if ((wlc_hw == NULL) || (wlc_hw->sih == NULL))
		return;

	/* Only chips with internal bus or PCIE cores or certain PCI cores
	 * are able to switch cores w/o disabling interrupts
	 */
	if (!((BUSTYPE(wlc_hw->sih->bustype) == SI_BUS) ||
	      ((BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) &&
	       ((BUSCORETYPE(wlc_hw->sih->buscoretype) == PCIE_CORE_ID) ||
	        (BUSCORETYPE(wlc_hw->sih->buscoretype) == PCIE2_CORE_ID) ||
	        (wlc_hw->sih->buscorerev >= 13))))) {
		return;
	}

	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));

#ifdef WLLED
	if (wlc_hw->wlc->ledh)
		wlc_led_deinit(wlc_hw->wlc->ledh);
#endif

	wlc_bmac_btc_gpio_disable(wlc_hw);

	return;
}

bool
wlc_bmac_radio_hw(wlc_hw_info_t *wlc_hw, bool enable, bool skip_anacore)
{
	/* Do not access Phy registers if core is not up */
	if (si_iscoreup(wlc_hw->sih) == FALSE)
		return FALSE;

	if (enable) {
		if (PMUCTL_ENAB(wlc_hw->sih)) {
			AND_REG(wlc_hw->osh, &wlc_hw->regs->clk_ctl_st, ~CCS_FORCEHWREQOFF);
			si_pmu_radio_enable(wlc_hw->sih, TRUE);
		}

		/* need to skip for 5356 in case of radio_pwrsave feature. */
		if (!skip_anacore)
			phy_ana_switch((phy_info_t *)wlc_hw->band->pi, ON);
		phy_radio_switch((phy_info_t *)wlc_hw->band->pi, ON);

		/* resume d11 core */
		wlc_bmac_enable_mac(wlc_hw);
	} else {
		/* suspend d11 core */
		wlc_bmac_suspend_mac_and_wait(wlc_hw);

		phy_radio_switch((phy_info_t *)wlc_hw->band->pi, OFF);
		/* need to skip for 5356 in case of radio_pwrsave feature. */
		if (!skip_anacore)
			phy_ana_switch((phy_info_t *)wlc_hw->band->pi, OFF);

		if (PMUCTL_ENAB(wlc_hw->sih)) {
			si_pmu_radio_enable(wlc_hw->sih, FALSE);
			OR_REG(wlc_hw->osh, &wlc_hw->regs->clk_ctl_st, CCS_FORCEHWREQOFF);
		}
	}

	return TRUE;
}

void
wlc_bmac_minimal_radio_hw(wlc_hw_info_t *wlc_hw, bool enable)
{
	wlc_info_t *wlc = wlc_hw->wlc;
	if (PMUCTL_ENAB(wlc_hw->sih)) {

		if (enable == TRUE) {
			AND_REG(wlc->osh, &wlc->regs->clk_ctl_st, ~CCS_FORCEHWREQOFF);
			si_pmu_radio_enable(wlc_hw->sih, TRUE);
		} else {
			si_pmu_radio_enable(wlc_hw->sih, FALSE);
			OR_REG(wlc->osh, &wlc->regs->clk_ctl_st, CCS_FORCEHWREQOFF);
		}
	}
}

bool
wlc_bmac_si_iscoreup(wlc_hw_info_t *wlc_hw)
{
	return si_iscoreup(wlc_hw->sih);
}

uint16
wlc_bmac_rate_shm_offset(wlc_hw_info_t *wlc_hw, uint8 rate)
{
	uint16 table_ptr;
	uint8 phy_rate, indx;

	/* get the phy specific rate encoding for the PLCP SIGNAL field */
	/* XXX4321 fixup needed ? */
	if (RATE_ISOFDM(rate))
		table_ptr = M_RT_DIRMAP_A(wlc_hw);
	else
		table_ptr = M_RT_DIRMAP_B(wlc_hw);

	/* for a given rate, the LS-nibble of the PLCP SIGNAL field is
	 * the index into the rate table.
	 */
	phy_rate = rate_info[rate] & RATE_MASK;
	indx = phy_rate & 0xf;

	/* Find the SHM pointer to the rate table entry by looking in the
	 * Direct-map Table
	 */
	return (2*wlc_bmac_read_shm(wlc_hw, table_ptr + (indx * 2)));
}

void
wlc_bmac_stf_set_rateset_shm_offset(wlc_hw_info_t *wlc_hw, uint count, uint16 pos, uint16 mask,
wlc_stf_rs_shm_offset_t *stf_rs)
{
	uint16 idx;
	uint16 entry_ptr;
	uint16 val;
	uint8 rate;

	for (idx = 0; idx < count; idx++) {
		rate = stf_rs->rate[idx] & RATE_MASK;
		entry_ptr = wlc_bmac_rate_shm_offset(wlc_hw, rate);
		val = stf_rs->val[idx];
		if (D11REV_GE(wlc_hw->corerev, 40)) {
			val |= (wlc_bmac_read_shm(wlc_hw, (entry_ptr + pos)) & ~mask);
		}
		wlc_bmac_write_shm(wlc_hw, (entry_ptr + pos), val);
	}
}

#ifdef PHYCAL_CACHING
void
wlc_bmac_set_phycal_cache_flag(wlc_hw_info_t *wlc_hw, bool state)
{
	wlc_phy_cal_cache_set(wlc_hw->band->pi, state);
}

bool
wlc_bmac_get_phycal_cache_flag(wlc_hw_info_t *wlc_hw)
{
	return wlc_phy_cal_cache_get(wlc_hw->band->pi);
}
#endif /* PHYCAL_CACHING */

void
wlc_bmac_set_txpwr_percent(wlc_hw_info_t *wlc_hw, uint8 val)
{
	wlc_phy_txpwr_percent_set(wlc_hw->band->pi, val);
}


/** update auto d11 shmdef specific to ucode downlaod type */
void
wlc_bmac_autod11_shm_upd(wlc_hw_info_t *wlc_hw, uint8 ucodeType)
{
	int corerev = 0;

	ASSERT(wlc_hw != NULL);
	corerev = (wlc_hw->corerev == 28) ? 25 : wlc_hw->corerev;

	switch (ucodeType) {
		case D11_IF_SHM_STD:
			d11shm_select_ucode_std(&wlc_hw->shmdefs, corerev);
			break;
#ifdef BCMULP
		case D11_IF_SHM_ULP:
			d11shm_select_ucode_ulp(&wlc_hw->shmdefs, corerev);
			break;
#endif /* BCMULP */
#ifdef WOWL
		case D11_IF_SHM_WOWL:
			d11shm_select_ucode_wowl(&wlc_hw->shmdefs, corerev);
			break;
#endif /* WOWL */
		default:
			d11shm_select_ucode_std(&wlc_hw->shmdefs, corerev);
			break;
	}
	if (wlc_hw->wlc)
		wlc_hw->wlc->shmdefs = wlc_hw->shmdefs;

	if (wlc_hw->band && wlc_hw->band->pi)
		wlc_phy_set_shmdefs(wlc_hw->band->pi, wlc_hw->shmdefs);
}

uint32
wlc_bmac_read_counter(wlc_hw_info_t* wlc_hw, uint baseaddr, int lo_off, int hi_off)
{
	uint16 high, tmp_high, low;

	ASSERT(baseaddr != (uint)~0);

	tmp_high = wlc_bmac_read_shm(wlc_hw, baseaddr + hi_off);
	low = wlc_bmac_read_shm(wlc_hw, baseaddr + lo_off);
	high = wlc_bmac_read_shm(wlc_hw, baseaddr + hi_off);
	if (high != tmp_high) {
		low = 0;	/* assume it zero */
	}
	return (high << 16) | low;
}

uint32
wlc_bmac_cca_read_counter(wlc_hw_info_t* wlc_hw, int lo_off, int hi_off)
{
	return wlc_bmac_read_counter(wlc_hw, wlc_hw->cca_shm_base, lo_off, hi_off);
}

int
wlc_bmac_cca_stats_read(wlc_hw_info_t *wlc_hw, cca_ucode_counts_t *cca_counts)
{
	uint32 tsf_h;

	/* Read shmem */
	cca_counts->txdur =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_TXDUR_L_OFFSET(wlc_hw),
			M_CCA_TXDUR_H_OFFSET(wlc_hw));
	cca_counts->ibss =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_INBSS_L_OFFSET(wlc_hw),
			M_CCA_INBSS_H_OFFSET(wlc_hw));
	cca_counts->obss =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_OBSS_L_OFFSET(wlc_hw),
			M_CCA_OBSS_H_OFFSET(wlc_hw));
	cca_counts->noctg =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_NOCTG_L_OFFSET(wlc_hw),
			M_CCA_NOCTG_H_OFFSET(wlc_hw));
	cca_counts->nopkt =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_NOPKT_L_OFFSET(wlc_hw),
			M_CCA_NOPKT_H_OFFSET(wlc_hw));
	cca_counts->PM =
		wlc_bmac_cca_read_counter(wlc_hw, M_MAC_SLPDUR_L_OFFSET(wlc_hw),
			M_MAC_SLPDUR_H_OFFSET(wlc_hw));
#ifdef ISID_STATS
	cca_counts->crsglitch = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_RXCRSGLITCH));
	cca_counts->badplcp = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_RXBADPLCP));
	cca_counts->bphy_crsglitch = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_BPHYGLITCH));
	cca_counts->bphy_badplcp = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_BPHY_BADPLCP));
#endif /* ISID_STATS */
	wlc_bmac_read_tsf(wlc_hw, &cca_counts->usecs, &tsf_h);
	return 0;
}

int
wlc_bmac_obss_stats_read(wlc_hw_info_t *wlc_hw, wlc_bmac_obss_counts_t *obss_counts)
{
	uint32 tsf_h;

#if defined(BCMDBG_PHYDUMP)
	bmac_suspend_stats_t* susp_stats = wlc_hw->suspend_stats;
	uint32 suspend_time = susp_stats->suspended;
	uint32 timenow = R_REG(wlc_hw->osh, &wlc_hw->regs->tsf_timerlow);

	if (susp_stats->suspend_start > susp_stats->suspend_end &&
	    timenow > susp_stats->suspend_start) {
		suspend_time += (timenow - susp_stats->suspend_start) / 100;
	}

	obss_counts->suspend = suspend_time;
	obss_counts->suspend_cnt = susp_stats->suspend_count;
#endif

	/* Read shmem */
	obss_counts->txdur =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_TXDUR_L_OFFSET(wlc_hw),
			M_CCA_TXDUR_H_OFFSET(wlc_hw));
	obss_counts->ibss =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_INBSS_L_OFFSET(wlc_hw),
			M_CCA_INBSS_H_OFFSET(wlc_hw));
	obss_counts->obss =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_OBSS_L_OFFSET(wlc_hw),
			M_CCA_OBSS_H_OFFSET(wlc_hw));
	obss_counts->noctg =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_NOCTG_L_OFFSET(wlc_hw),
			M_CCA_NOCTG_H_OFFSET(wlc_hw));
	obss_counts->nopkt =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_NOPKT_L_OFFSET(wlc_hw),
			M_CCA_NOPKT_H_OFFSET(wlc_hw));
	obss_counts->PM =
		wlc_bmac_cca_read_counter(wlc_hw, M_MAC_SLPDUR_L_OFFSET(wlc_hw),
			M_MAC_SLPDUR_H_OFFSET(wlc_hw));
	obss_counts->txopp =
		wlc_bmac_cca_read_counter(wlc_hw, M_CCA_TXOP_L_OFFSET(wlc_hw),
			M_CCA_TXOP_H_OFFSET(wlc_hw));

	obss_counts->slot_time_txop = R_REG(wlc_hw->osh, &wlc_hw->regs->u.d11regs.ifs_slot);
#ifdef ISID_STATS
	obss_counts->crsglitch = wlc_bmac_read_shm(wlc_hw,
	MACSTAT_ADDR(wlc_hw, MCSTOFF_RXCRSGLITCH));
	obss_counts->badplcp = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_RXBADPLCP));
	obss_counts->bphy_crsglitch = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_BPHYGLITCH));
	obss_counts->bphy_badplcp = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_BPHY_BADPLCP));
#endif /* ISID_STATS */
#if (defined(WL_PROT_OBSS) || defined(WL_OBSS_DYNBW))
	if (WLC_PROT_OBSS_ENAB(wlc_hw->wlc->pub) || WLC_OBSS_DYNBW_ENAB(wlc_hw->wlc->pub)) {
		if (D11REV_GE(wlc_hw->corerev, 40)) {
			obss_counts->rxcrs_sec20 =
				wlc_bmac_read_counter(wlc_hw, wlc_hw->macstat1_shm_base,
				OFFSETOF(macstat1_t, rxsec20_durl),
				OFFSETOF(macstat1_t, rxsec20_durh));
			obss_counts->rxcrs_sec40 =
				wlc_bmac_read_counter(wlc_hw, wlc_hw->macstat1_shm_base,
				OFFSETOF(macstat1_t, rxsec40_durl),
				OFFSETOF(macstat1_t, rxsec40_durh));
			obss_counts->rxcrs_pri =
				wlc_bmac_read_counter(wlc_hw, wlc_hw->macstat1_shm_base,
				OFFSETOF(macstat1_t, rxpri_durl), OFFSETOF(macstat1_t, rxpri_durh));

			obss_counts->sec_rssi_hist_hi = wlc_bmac_read_shm(wlc_hw,
				wlc_hw->macstat1_shm_base + OFFSETOF(macstat1_t, rxsecrssi0));
			obss_counts->sec_rssi_hist_med = wlc_bmac_read_shm(wlc_hw,
				wlc_hw->macstat1_shm_base + OFFSETOF(macstat1_t, rxsecrssi1));
			obss_counts->sec_rssi_hist_low = wlc_bmac_read_shm(wlc_hw,
				wlc_hw->macstat1_shm_base + OFFSETOF(macstat1_t, rxsecrssi2));

			obss_counts->rxdrop20s = wlc_bmac_read_shm(wlc_hw,
				MACSTAT_ADDR(wlc_hw, MCSTOFF_RXDROP20S));
			obss_counts->rx20s = wlc_bmac_read_shm(wlc_hw,
				wlc_hw->macstat1_shm_base + OFFSETOF(macstat1_t, rx20s));
			}
	}
#endif /* (defined(WL_PROT_OBSS) || defined(WL_OBSS_DYNBW)) */
	wlc_bmac_read_tsf(wlc_hw, &obss_counts->usecs, &tsf_h);

	obss_counts->txfrm = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_TXFRAME));
	obss_counts->rxstrt = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_RXSTRT));
	obss_counts->rxglitch = wlc_bmac_read_shm(wlc_hw,
		MACSTAT_ADDR(wlc_hw, MCSTOFF_RXCRSGLITCH));

	return 0;
} /* wlc_bmac_obss_stats_read */

void
wlc_bmac_antsel_set(wlc_hw_info_t *wlc_hw, uint32 antsel_avail)
{
	wlc_hw->antsel_avail = antsel_avail;
}


/* BTC stuff BEGIN */
/**
 * Create space in wlc_hw->btc struct to save a local copy of btc_params when the PSM goes down
 * Copy btc_paramXX and btc_flag information from NVRAM to wlc_info_t structure, so that it can be
 * used during INIT, when NVRAM has already been released (reclaimed).
 */
static int
BCMATTACHFN(wlc_bmac_btc_param_attach)(wlc_info_t *wlc)
{
	wlc_hw_info_t *wlc_hw = wlc->hw;
	uint16 indx;
	char   buf[15];
	uint16 num_params = 0;

	/* Allocate space for shadow btc params in driver.
	 * first check whether the chip includes the underlying shmem
	 * Not needed for MFG images, since radio is always on (i.e. mpc 0)
	 */

	/* Allocate space for Host-based COEX variables */
	if ((wlc_hw->btc->wlc_btc_params_fw = (uint16 *)
		MALLOCZ(wlc->osh, (BTC_FW_MAX_INDICES*sizeof(uint16)))) == NULL) {
		WL_ERROR(
		("wlc_bmac_attach: no mem for wlc_btc_params_fw, malloced %d bytes\n",
			MALLOCED(wlc->osh)));
		return BCME_NOMEM;
	}

	/* first count nr of present btc_params in NVRAM */
	for (indx = 0; indx <= M_BTCX_MAX_INDEX; indx++) {
		snprintf(buf, sizeof(buf), rstr_btc_paramsD, indx);
		if (getvar(wlc_hw->vars, buf) != NULL) {
			num_params++;
		}
	}
	if ((num_params > 0) || (getvar(wlc_hw->vars, rstr_btc_flags) != NULL)) {
		wlc->btc_param_vars = (struct wlc_btc_param_vars_info*) MALLOC(wlc->osh,
			sizeof(struct wlc_btc_param_vars_info) +
			num_params * sizeof(struct wlc_btc_param_vars_entry));
		if (wlc->btc_param_vars == NULL) {
			WL_ERROR(("wlc_btc_param_attach: no mem for btc_param_vars, malloc: %db\n",
				MALLOCED(wlc->osh)));
			return BCME_NOMEM;
		}
		wlc->btc_param_vars->num_entries = 0;
		/* go through all btc_params, and if exist in nvram copy to wlc->btc_param_vars */
		for (indx = 0; indx <= M_BTCX_MAX_INDEX; indx++) {
			snprintf(buf, sizeof(buf), rstr_btc_paramsD, indx);
			if (getvar(wlc_hw->vars, buf) != NULL) {
				wlc->btc_param_vars->param_list[
					wlc->btc_param_vars->num_entries].value =
					(uint16)getintvar(wlc_hw->vars, buf);
				wlc->btc_param_vars->param_list[
					wlc->btc_param_vars->num_entries++].index = indx;
			}
		}
		ASSERT(wlc->btc_param_vars->num_entries == num_params);
		/* check if btc_flags exist in nvram and if so copy to wlc->btc_param_vars */
		if (getvar(wlc_hw->vars, rstr_btc_flags) != NULL) {
			wlc->btc_param_vars->flags = (uint16)getintvar(wlc_hw->vars,
				rstr_btc_flags);
			wlc->btc_param_vars->flags_present = TRUE;
		} else {
			wlc->btc_param_vars->flags_present = FALSE;
		}
	}
	/* Initializing the host-based Bt-coex parameters */
	if (wlc_hw->btc->wlc_btc_params_fw) {

		for (indx = 0; indx < BTC_FW_NUM_INDICES; indx++) {
			wlc_hw->btc->wlc_btc_params_fw[indx] = btc_fw_params_init_vals[indx];
		}
	}
	return BCME_OK;
} /* wlc_bmac_btc_param_attach */

static void
BCMINITFN(wlc_bmac_btc_param_init)(wlc_hw_info_t *wlc_hw)
{
	wlc_info_t *wlc = wlc_hw->wlc;
	uint16 indx;

	/* cache the pointer to the BTCX shm block, which won't change after coreinit */
	wlc_hw->btc->bt_shm_addr = 2 * wlc_bmac_read_shm(wlc_hw, M_BTCX_BLK_PTR(wlc));

	if (wlc_hw->btc->bt_shm_addr == 0)
		return;

		if (wlc->btc_param_vars == NULL) {
		/* wlc_btc_param_init: wlc->btc_param_vars unavailable */
			return;
		}
		/* go through all btc_params, if they existed in nvram, overwrite shared memory */
		for (indx = 0; indx < wlc->btc_param_vars->num_entries; indx++)
			wlc_bmac_write_shm(wlc_hw, wlc_hw->btc->bt_shm_addr +
				wlc->btc_param_vars->param_list[indx].index * 2,
				wlc->btc_param_vars->param_list[indx].value);
		/* go through btc_flags list as copied from nvram and initialize them */
		if (wlc->btc_param_vars->flags_present) {
			wlc_hw->btc->flags = wlc->btc_param_vars->flags;
		}
	wlc_bmac_btc_btcflag2ucflag(wlc_hw);
}

static void
wlc_bmac_btc_btcflag2ucflag(wlc_hw_info_t *wlc_hw)
{
	int indx;
	int btc_flags = wlc_hw->btc->flags;
	uint16 btc_mhf = (btc_flags & WL_BTC_FLAG_PREMPT) ? MHF2_BTCPREMPT : 0;

	wlc_bmac_mhf(wlc_hw, MHF2, MHF2_BTCPREMPT, btc_mhf, WLC_BAND_2G);
	btc_mhf = 0;
	for (indx = BTC_FLAGS_MHF3_START; indx <= BTC_FLAGS_MHF3_END; indx++)
		if (btc_flags & (1 << indx))
			btc_mhf |= btc_ucode_flags[indx].mask;

	btc_mhf &= ~(MHF3_BTCX_ACTIVE_PROT | MHF3_BTCX_PS_PROTECT);
	wlc_bmac_mhf(wlc_hw, MHF3, MHF3_BTCX_DEF_BT | MHF3_BTCX_SIM_RSP |
		MHF3_BTCX_ECI | MHF3_BTCX_SIM_TX_LP, btc_mhf, WLC_BAND_2G);

	/* Ucode needs ECI indication in all bands */
	if ((btc_mhf & ~MHF3_BTCX_ECI) == 0)
		wlc_bmac_mhf(wlc_hw, MHF3, MHF3_BTCX_ECI, btc_mhf & MHF3_BTCX_ECI, WLC_BAND_AUTO);
	btc_mhf = 0;
	for (indx = BTC_FLAGS_MHF3_END + 1; indx < BTC_FLAGS_SIZE; indx++)
		if (btc_flags & (1 << indx))
			btc_mhf |= btc_ucode_flags[indx].mask;

	wlc_bmac_mhf(wlc_hw, MHF5, MHF5_BTCX_LIGHT | MHF5_BTCX_PARALLEL,
		btc_mhf, WLC_BAND_2G);

	/* Need to specify when platform has low shared antenna isolation */
	if ((wlc_hw->sih->boardvendor == VENDOR_APPLE) &&
	    ((wlc_hw->sih->boardtype == BCM94331X29B) ||
	     (wlc_hw->sih->boardtype == BCM94331X29D) ||
	     (wlc_hw->sih->boardtype == BCM94331X33) ||
	     (wlc_hw->sih->boardtype == BCM94331X28B))) {
		wlc_bmac_mhf(wlc_hw, MHF5, MHF5_4331_BTCX_LOWISOLATION,
			MHF5_4331_BTCX_LOWISOLATION, WLC_BAND_2G);
	}
}

#ifdef STA
void
wlc_bmac_btc_update_predictor(wlc_hw_info_t *wlc_hw)
{
	uint32 tsf;
	uint16 bt_period, bt_last_l, bt_last_h, bt_shm_addr;
	uint32 bt_last, bt_next;
	d11regs_t *regs = wlc_hw->regs;

	bt_shm_addr = wlc_hw->btc->bt_shm_addr;
	if (bt_shm_addr == 0)
		return;

	/* Make sure period is known */
	bt_period = wlc_bmac_read_shm(wlc_hw, wlc_hw->btc->bt_shm_addr +
		M_BTCX_PRED_PER_OFFSET(wlc_hw));

	if (bt_period == 0)
		return;

	tsf = R_REG(wlc_hw->osh, &regs->tsf_timerlow);

	/* Avoid partial read */
	do {
		bt_last_l = wlc_bmac_read_shm(wlc_hw, bt_shm_addr +
			M_BTCX_LAST_SCO_OFFSET(wlc_hw));
		bt_last_h = wlc_bmac_read_shm(wlc_hw, bt_shm_addr +
			M_BTCX_LAST_SCO_H_OFFSET(wlc_hw));
	} while (bt_last_l != wlc_bmac_read_shm(wlc_hw, bt_shm_addr +
		M_BTCX_LAST_SCO_OFFSET(wlc_hw)));
	bt_last = ((uint32)bt_last_h << 16) | bt_last_l;

	/* Calculate next expected BT slot time */
	bt_next = bt_last + ((((tsf - bt_last) / bt_period) + 1) * bt_period);
	wlc_bmac_write_shm(wlc_hw, bt_shm_addr + M_BTCX_NEXT_SCO_OFFSET(wlc_hw),
		(uint16)(bt_next & 0xffff));
}
#endif /* STA */

/**
 * Bluetooth/WLAN coexistence parameters are exposed for some customers.
 * Rather than exposing all of shared memory, an index that is range-checked
 * is translated to an address.
 */
static bool
wlc_bmac_btc_param_to_shmem(wlc_hw_info_t *wlc_hw, uint32 *pval)
{
	if ((*pval <= M_BTCX_MAX_INDEX) && (wlc_hw->btc->bt_shm_addr)) {
		*pval = wlc_hw->btc->bt_shm_addr + (2 * (*pval));
		return TRUE;
	}
	return FALSE;
}

static bool
wlc_bmac_btc_flags_ucode(uint8 val, uint8 *idx, uint16 *mask)
{
	/* Check that the index is valid */
	if (val >= ARRAYSIZE(btc_ucode_flags))
		return FALSE;

	*idx = btc_ucode_flags[val].idx;
	*mask = btc_ucode_flags[val].mask;

	return TRUE;
}

int
wlc_bmac_btc_period_get(wlc_hw_info_t *wlc_hw, uint16 *btperiod, bool *btactive,
	uint16 *agg_off_bm, uint16 *acl_last_ts, uint16 *a2dp_last_ts)
{
	uint16 bt_period = 0, bt_shm_addr, bt_per_count = 0;
	uint32 tmp;
	d11regs_t *regs = wlc_hw->regs;

	bt_shm_addr = wlc_hw->btc->bt_shm_addr;

#define BTCX_PER_THRESHOLD 4
#define BTCX_BT_ACTIVE_THRESHOLD 5
#define BTCX_PER_OUT_OF_SYNC_CNT 5

	if (bt_shm_addr == 0)
		tmp = 0;

	else if ((bt_period = wlc_bmac_read_shm(wlc_hw, bt_shm_addr +
			M_BTCX_PRED_PER_OFFSET(wlc_hw))) == 0)
		tmp = 0;

	else {
		/*
		Read PRED_PER_COUNT only for non-ECI chips. For ECI, PRED_PER gets
		cleared as soon as periodic activity ends so there is no need to
		monitor PRED_PER_COUNT.
		*/
		if (!BCMCOEX_ENAB_BMAC(wlc_hw)) {
			if ((bt_per_count = wlc_bmac_read_shm(wlc_hw,
				bt_shm_addr + M_BTCX_PRED_COUNT_OFFSET(wlc_hw))) <=
					BTCX_PER_THRESHOLD)
				tmp = 0;
			else
				tmp = bt_period;
		}
		else {
			tmp = bt_period;
		}
	}
	/*
	This code has been added to filter out any spurious reads of PRED_PER
	being '0' (this may happen if the value is read right after a mac
	suspend/resume because ucode clears out this value after resumption).
	*/
	if (!tmp) {
		wlc_hw->btc->bt_period_out_of_sync_cnt++;
		if (wlc_hw->btc->bt_period_out_of_sync_cnt <= BTCX_PER_OUT_OF_SYNC_CNT) {
			tmp = wlc_hw->btc->bt_period;
		} else {
			wlc_hw->btc->bt_period_out_of_sync_cnt = BTCX_PER_OUT_OF_SYNC_CNT;
		}
	}
	else {
		wlc_hw->btc->bt_period_out_of_sync_cnt = 0;
	}

	*btperiod = wlc_hw->btc->bt_period = (uint16)tmp;

	*agg_off_bm = wlc_bmac_read_shm(wlc_hw, bt_shm_addr + M_BTCX_AGG_OFF_BM);
	*a2dp_last_ts = wlc_bmac_read_shm(wlc_hw, bt_shm_addr + M_BTCX_LAST_A2DP_OFFSET(wlc_hw));
	*acl_last_ts = wlc_bmac_read_shm(wlc_hw, bt_shm_addr + M_BTCX_LAST_DATA_OFFSET(wlc_hw));

	if (R_REG(wlc_hw->osh, &regs->maccontrol) & MCTL_PSM_RUN) {
		tmp = R_REG(wlc_hw->osh, &regs->u.d11regs.btcx_cur_rfact_timer);
		/* code below can be optimized for speed; however, we choose not
		 * to do that to achieve better readability
		 */
		if (wlc_hw->btc->bt_active) {
			/* active state : switch to inactive when reading 0xffff */
			if (tmp == 0xffff) {
				wlc_hw->btc->bt_active = FALSE;
				wlc_hw->btc->bt_active_asserted_cnt = 0;
			}
		} else {
			/* inactive state : switch to active when bt_active asserted for
			 * more than a certain times
			 */
			if (tmp == 0xffff)
				wlc_hw->btc->bt_active_asserted_cnt = 0;
			/* concecutive asserts, now declare bt is active */
			else if (++wlc_hw->btc->bt_active_asserted_cnt >= BTCX_BT_ACTIVE_THRESHOLD)
				wlc_hw->btc->bt_active = TRUE;
		}
	}

	*btactive = wlc_hw->btc->bt_active;

	return BCME_OK;
} /* wlc_bmac_btc_period_get */

int
wlc_bmac_btc_mode_set(wlc_hw_info_t *wlc_hw, int btc_mode)
{
	uint16 btc_mhfs[MHFMAX];
	bool ucode_up = FALSE;

	if (btc_mode > WL_BTC_DEFAULT) {
		WL_ERROR(("wl%d: %s: Bad argument btc_mode:%d\n", wlc_hw->unit, __FUNCTION__,
			btc_mode));
		return BCME_BADARG;
	}

	/* Make sure 2-wire or 3-wire decision has been made */
	ASSERT((wlc_hw->btc->wire >= WL_BTC_2WIRE) || (wlc_hw->btc->wire <= WL_BTC_4WIRE));

	 /* Determine the default mode for the device */
	if (btc_mode == WL_BTC_DEFAULT) {
		if (BCMCOEX_ENAB_BMAC(wlc_hw) || (wlc_hw->boardflags2 & BFL2_BTCLEGACY)) {
			btc_mode = WL_BTC_FULLTDM;
			/* default to hybrid mode for combo boards with 2 or more antennas */
			if (wlc_hw->btc->btcx_aa > 2) {
				if (CHIPID(wlc_hw->sih->chip) == BCM43242_CHIP_ID ||
					(CHIPID(wlc_hw->sih->chip) == BCM4354_CHIP_ID) ||
					(CHIPID(wlc_hw->sih->chip) == BCM4356_CHIP_ID))
					btc_mode = WL_BTC_FULLTDM;
				else if (CHIPID(wlc_hw->sih->chip) == BCM43142_CHIP_ID)
					btc_mode = WL_BTC_LITE;
				else
					btc_mode = WL_BTC_HYBRID;
			}
		} else {
			btc_mode = WL_BTC_DISABLE;
		}
	}

	/* Do not allow an enable without hw support */
	if (btc_mode != WL_BTC_DISABLE) {
		if ((wlc_hw->btc->wire >= WL_BTC_3WIRE) &&
			!(wlc_hw->machwcap & MCAP_BTCX)) {
			WL_ERROR(("wl%d: %s: Bad option wire:%d machwcap:%d\n", wlc_hw->unit,
				__FUNCTION__, wlc_hw->btc->wire, wlc_hw->machwcap));
			return BCME_BADOPTION;
		}
	}

	/* Initialize ucode flags */
	bzero(btc_mhfs, sizeof(btc_mhfs));
	wlc_hw->btc->flags = 0;

	if (wlc_hw->up)
		ucode_up = (R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) & MCTL_EN_MAC);

	if (btc_mode != WL_BTC_DISABLE) {
		btc_mhfs[MHF1] |= MHF1_BTCOEXIST;
		if (wlc_hw->btc->wire == WL_BTC_2WIRE) {
			/* BMAC_NOTES: sync the state with HIGH driver ??? */
			/* Make sure 3-wire coex is off */
			if (wlc_hw->boardflags & BFL_BTC2WIRE_ALTGPIO) {
				btc_mhfs[MHF2] |= MHF2_BTC2WIRE_ALTGPIO;
				wlc_hw->btc->gpio_mask =
					BOARD_GPIO_BTCMOD_OUT | BOARD_GPIO_BTCMOD_IN;
				wlc_hw->btc->gpio_out = BOARD_GPIO_BTCMOD_OUT;
			} else {
				btc_mhfs[MHF2] &= ~MHF2_BTC2WIRE_ALTGPIO;
			}
		} else {
			/* by default we use PS protection unless overriden. */
			if (btc_mode == WL_BTC_HYBRID)
				wlc_hw->btc->flags |= WL_BTC_FLAG_SIM_RSP;
			else if (btc_mode == WL_BTC_LITE) {
				/* for X28, parallel mode used given 30+ isolation */
				if (CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID &&
					(wlc_hw->boardflags & BFL_FEM_BT))
					wlc_hw->btc->flags |= WL_BTC_FLAG_PARALLEL;
				else
					wlc_hw->btc->flags |= WL_BTC_FLAG_LIGHT;
				wlc_hw->btc->flags |= WL_BTC_FLAG_SIM_RSP;
			} else if (btc_mode == WL_BTC_PARALLEL) {
				wlc_hw->btc->flags |= WL_BTC_FLAG_PARALLEL;
				wlc_hw->btc->flags |= WL_BTC_FLAG_SIM_RSP;
			} else {
				wlc_hw->btc->flags |=
					(WL_BTC_FLAG_PS_PROTECT | WL_BTC_FLAG_ACTIVE_PROT);
			}

			if (BCMCOEX_ENAB_BMAC(wlc_hw)) {
				wlc_hw->btc->flags |= WL_BTC_FLAG_ECI;
			} else {
				if (wlc_hw->btc->wire == WL_BTC_4WIRE)
					btc_mhfs[MHF3] |= MHF3_BTCX_EXTRA_PRI;
				else
					wlc_hw->btc->flags |= WL_BTC_FLAG_PREMPT;
			}
		}

	} else {
		btc_mhfs[MHF1] &= ~MHF1_BTCOEXIST;
	}

	if (D11REV_GE(wlc_hw->corerev, 48)) {
		/* no auto mode for rev < 48 to preserve existing sisoack setting */
		if (btc_mode == WL_BTC_HYBRID || btc_mode == WL_BTC_FULLTDM) {
			wlc_btc_siso_ack_set(wlc_hw->wlc, AUTO, FALSE);
		} else {
			wlc_btc_siso_ack_set(wlc_hw->wlc, 0, FALSE);
		}
	}

	wlc_hw->btc->mode = btc_mode;

	/* Set the MHFs only in 2G band
	 * If we are on the other band, update the sw cache for the
	 * 2G band.
	 */
	if (wlc_hw->up && ucode_up)
		wlc_bmac_suspend_mac_and_wait(wlc_hw);

	wlc_bmac_mhf(wlc_hw, MHF1, MHF1_BTCOEXIST, btc_mhfs[MHF1], WLC_BAND_2G);
	wlc_bmac_mhf(wlc_hw, MHF2, MHF2_BTC2WIRE_ALTGPIO, btc_mhfs[MHF2],
		WLC_BAND_2G);
	wlc_bmac_mhf(wlc_hw, MHF3, MHF3_BTCX_EXTRA_PRI, btc_mhfs[MHF3], WLC_BAND_2G);
	wlc_bmac_btc_btcflag2ucflag(wlc_hw);

	if (wlc_hw->up && ucode_up) {
		wlc_bmac_enable_mac(wlc_hw);
	}


	return BCME_OK;
} /* wlc_bmac_btc_mode_set */

int
wlc_bmac_btc_mode_get(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->btc->mode;
}

int
wlc_bmac_btc_wire_get(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->btc->wire;
}

int
wlc_bmac_btc_wire_set(wlc_hw_info_t *wlc_hw, int btc_wire)
{
	/* Has to be down. Enforced through iovar flag */
	ASSERT(!wlc_hw->up);

	if (btc_wire > WL_BTC_4WIRE) {
		WL_ERROR(("wl%d: %s: Unsupported wire setting btc_wire: %d\n", wlc_hw->unit,
			__FUNCTION__, btc_wire));
		return BCME_BADARG;
	}

	/* default to 4-wire ucode if 3-wire boardflag is set or
	 * - M93 or ECI is enabled
	 * else default to 2-wire
	 */
	if (btc_wire == WL_BTC_DEFWIRE) {
		/* Use the boardflags to finally fix the setting for
		 * boards with correct flags
		 */
		if (BCMCOEX_ENAB_BMAC(wlc_hw))
			wlc_hw->btc->wire = WL_BTC_3WIRE;
		else if (wlc_hw->boardflags2 & BFL2_BTCLEGACY) {
			if (wlc_hw->boardflags2 & BFL2_BTC3WIREONLY)
				wlc_hw->btc->wire = WL_BTC_3WIRE;
			else
				wlc_hw->btc->wire = WL_BTC_4WIRE;
		} else
			wlc_hw->btc->wire = WL_BTC_2WIRE;

	}
	else
		wlc_hw->btc->wire = btc_wire;
	/* flush ucode_loaded so the ucode download will happen again to pickup the right ucode */
	wlc_hw->ucode_loaded = FALSE;

	wlc_bmac_btc_gpio_configure(wlc_hw);

	return BCME_OK;
} /* wlc_bmac_btc_wire_set */

int
wlc_bmac_btc_flags_get(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->btc->flags;
}


static void
wlc_bmac_btc_flags_upd(wlc_hw_info_t *wlc_hw, bool set_clear, uint16 val, uint8 idx, uint16 mask)
{
	if (set_clear) {
		wlc_hw->btc->flags |= val;
		wlc_bmac_mhf(wlc_hw, idx, mask, mask, WLC_BAND_2G);
	} else {
		wlc_hw->btc->flags &= ~val;
		wlc_bmac_mhf(wlc_hw, idx, mask, 0, WLC_BAND_2G);
	}
}

int
wlc_bmac_btc_flags_idx_get(wlc_hw_info_t *wlc_hw, int int_val)
{
	uint8 idx = 0;
	uint16 mask = 0;

	if (!wlc_bmac_btc_flags_ucode((uint8)int_val, &idx, &mask))
		return 0xbad;

	return (wlc_bmac_mhf_get(wlc_hw, idx, WLC_BAND_2G) & mask) ? 1 : 0;
}

int
wlc_bmac_btc_flags_idx_set(wlc_hw_info_t *wlc_hw, int int_val, int int_val2)
{
	uint8 idx = 0;
	uint16 mask = 0;

	if (!wlc_bmac_btc_flags_ucode((uint8)int_val, &idx, &mask))
		return BCME_BADARG;

	if (int_val2)
		wlc_bmac_btc_flags_upd(wlc_hw, TRUE, (uint16)(int_val2 << int_val), idx, mask);
	else
		wlc_bmac_btc_flags_upd(wlc_hw, FALSE, (uint16)(1 << int_val), idx, mask);

	return BCME_OK;
}

void
wlc_bmac_btc_stuck_war50943(wlc_hw_info_t *wlc_hw, bool enable)
{
	if (enable) {
		wlc_hw->btc->stuck_detected = FALSE;
		wlc_hw->btc->stuck_war50943 = TRUE;
		wlc_bmac_mhf(wlc_hw, MHF3, MHF3_BTCX_DELL_WAR, MHF3_BTCX_DELL_WAR, WLC_BAND_ALL);
	} else {
		wlc_hw->btc->stuck_war50943 = FALSE;
		wlc_bmac_mhf(wlc_hw, MHF3, MHF3_BTCX_DELL_WAR, 0, WLC_BAND_ALL);
	}
}

int
wlc_bmac_btc_params_set(wlc_hw_info_t *wlc_hw, int int_val, int int_val2)
{
	/* btc_params with indices > 1000 are stored in FW.
	 * First check to see whether this is a FW btc_param.
	 */
	if (int_val >= BTC_PARAMS_FW_START_IDX) {
		if (!(wlc_hw->btc->wlc_btc_params_fw)) return BCME_ERROR;
		int_val -= BTC_PARAMS_FW_START_IDX; /* Normalize to a 0-based index */
		if (int_val < BTC_FW_MAX_INDICES) {
			wlc_hw->btc->wlc_btc_params_fw[int_val] = (uint16)int_val2;
			return BCME_OK;
		} else {
			return BCME_BADADDR;
		}
	} else {
		/* If shmem is powered down & wlc_btc_params cached values exist,
		 * then update the relevant cached value based on the int_val index
		 */
		if (!(wlc_hw->up)) {
			if (!(wlc_hw->btc->wlc_btc_params))
				return BCME_ERROR;
			if (int_val < M_BTCX_BACKUP_SIZE) {
				wlc_hw->btc->wlc_btc_params[int_val] = (uint16)int_val2;
				return BCME_OK;
			} else {
				return BCME_BADADDR;
			}
		} else {
			if (!wlc_bmac_btc_param_to_shmem(wlc_hw, (uint32*)&int_val)) {
				return BCME_BADARG;
			}
		wlc_bmac_write_shm(wlc_hw, (uint16)int_val, (uint16)int_val2);
		return BCME_OK;
		}
	}
}

int
wlc_bmac_btc_params_get(wlc_hw_info_t *wlc_hw, int int_val)
{
	/* btc_params with indices > 1000 are stored in FW.
	 * First check to see whether this is a FW btc_param.
	 */
	if (int_val >= BTC_PARAMS_FW_START_IDX) {
		if (!(wlc_hw->btc->wlc_btc_params_fw))
			return BCME_ERROR;
		int_val -= BTC_PARAMS_FW_START_IDX; /* Normalize to a 0-based index */
		if (int_val < BTC_FW_MAX_INDICES) {
			return wlc_hw->btc->wlc_btc_params_fw[int_val];
		} else {
			return 0xbad;
		}
	} else {
		/* If shmem is powered down & wlc_btc_params cached values exist,
		 * then read from the relevant cached value based on the int_val index
		 */
		if (!(wlc_hw->up)) {
			if (!(wlc_hw->btc->wlc_btc_params))
				return 0xbad;
			if (int_val < M_BTCX_BACKUP_SIZE) {
				return wlc_hw->btc->wlc_btc_params[int_val];
			} else {
				return 0xbad;
			}
		} else {
	if (!wlc_bmac_btc_param_to_shmem(wlc_hw, (uint32*)&int_val)) {
		return 0xbad;
	}
			return wlc_bmac_read_shm(wlc_hw, (uint16)int_val);
		}
	}
}


void
wlc_bmac_btc_rssi_threshold_get(wlc_hw_info_t *wlc_hw,
	uint8 *prot, uint8 *high_thresh, uint8 *low_thresh)
{
	uint16 bt_shm_addr = wlc_hw->btc->bt_shm_addr;

	if (bt_shm_addr == 0)
		return;

	*prot =	(uint8)wlc_bmac_read_shm(wlc_hw, bt_shm_addr +
		M_BTCX_PROT_RSSI_THRESH_OFFSET(wlc_hw));
	*high_thresh = (uint8)wlc_bmac_read_shm(wlc_hw, bt_shm_addr +
		M_BTCX_HIGH_THRESH_OFFSET(wlc_hw));
	*low_thresh = (uint8)wlc_bmac_read_shm(wlc_hw, bt_shm_addr +
		M_BTCX_LOW_THRESH_OFFSET(wlc_hw));
}

static void
wlc_bmac_gpio_configure(wlc_hw_info_t *wlc_hw, bool is_uppath)
{
#ifndef BCMPCIDEV
	/* for X87 module; need to change power throttle pin to output
	 * tri-state so that leakage current is minimized.
	 */
	if ((wlc_hw->sih->boardvendor == VENDOR_APPLE) &&
	    (wlc_hw->sih->chip == BCM43602_CHIP_ID)) {
		if (!is_uppath) {
			/* Set to Input Mode */
			si_gpioouten(wlc_hw->sih, BOARD_GPIO_2_WLAN_PWR, 0, GPIO_HI_PRIORITY);
			/* Force the output High to reduce internal leakage via output buffer */
			si_gpioout(wlc_hw->sih, BOARD_GPIO_2_WLAN_PWR,
				BOARD_GPIO_2_WLAN_PWR, GPIO_HI_PRIORITY);
		}
	}
#endif /* BCMPCIDEV */
}

/** configure 3/4 wire coex gpio for newer chips */
static void
wlc_bmac_btc_gpio_configure(wlc_hw_info_t *wlc_hw)
{

	if (wlc_hw->btc->wire >= WL_BTC_3WIRE) {
		uint32 gm = 0;
		switch ((CHIPID(wlc_hw->sih->chip))) {
		case BCM43224_CHIP_ID:
		case BCM43421_CHIP_ID:
			if (wlc_hw->boardflags & BFL_FEM_BT)
				gm = GPIO_BTC4W_OUT_43224_SHARED;
			else
				gm = GPIO_BTC4W_OUT_43224;
			break;
		case BCM43225_CHIP_ID:
			gm = GPIO_BTC4W_OUT_43225;
			break;
		case BCM4313_CHIP_ID:
			gm = GPIO_BTC4W_OUT_4313;
			break;
		};

		wlc_hw->btc->gpio_mask = wlc_hw->btc->gpio_out = gm;
	}
}

/** Lower BTC GPIO through ChipCommon when BTC is OFF or D11 MAC is in reset or on powerup */
static void
wlc_bmac_btc_gpio_disable(wlc_hw_info_t *wlc_hw)
{
	uint32 gm, go;
	si_t *sih;
	bool xtal_set = FALSE;

	if (!wlc_hw->sbclk) {
		wlc_bmac_xtal(wlc_hw, ON);
		xtal_set = TRUE;
	}

	/* Proceed only if BTC GPIOs had been configured */
	if (wlc_hw->btc->gpio_mask == 0)
		return;

	sih = wlc_hw->sih;

	gm = wlc_hw->btc->gpio_mask;
	go = wlc_hw->btc->gpio_out;

	/* Set the control of GPIO back and lower only GPIO OUT pins and not the ones that
	 * are supposed to be IN
	 */
	si_gpiocontrol(sih, gm, 0, GPIO_DRV_PRIORITY);
	/* configure gpio to input to float pad */
	si_gpioouten(sih, gm, 0, GPIO_DRV_PRIORITY);
	/* a HACK to enable internal pulldown for 4313 */
	if (CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID)
		si_gpiopull(wlc_hw->sih, GPIO_PULLDN, gm, 0x40);

	si_gpioout(sih, go, 0, GPIO_DRV_PRIORITY);

	if (wlc_hw->clk)
		AND_REG(wlc_hw->osh, &wlc_hw->regs->psm_gpio_oe, ~wlc_hw->btc->gpio_out);

	/* BMAC_NOTE: PCI_BUS check here is actually not relevant; there is nothing PCI
	 * bus specific here it was only meant to be compile time optimization. Now it's
	 * true that it may not anyway be applicable to 4323, but need to see if there are
	 * any more places like this
	 */
	/* On someboards, which give GPIOs to UART via strapping,
	 * GPIO_BTC_OUT is not directly controlled by gpioout on CC
	 */
	if ((BUSTYPE(sih->bustype) == PCI_BUS) && (gm & BOARD_GPIO_BTC_OUT))
		si_btcgpiowar(sih);

	if (xtal_set)
		wlc_bmac_xtal(wlc_hw, OFF);
} /* wlc_bmac_btc_gpio_disable */

/** Set BTC GPIO through ChipCommon when BTC is ON */
static void
wlc_bmac_btc_gpio_enable(wlc_hw_info_t *wlc_hw)
{
	uint32 gm, gi;
	si_t *sih;

	ASSERT(wlc_hw->clk);

	/* Proceed only if GPIO-based BTC is configured */
	if (wlc_hw->btc->gpio_mask == 0)
		return;


	sih = wlc_hw->sih;

	gm = wlc_hw->btc->gpio_mask;
	gi = (~wlc_hw->btc->gpio_out) & wlc_hw->btc->gpio_mask;

	OR_REG(wlc_hw->osh, &wlc_hw->regs->psm_gpio_oe, wlc_hw->btc->gpio_out);
	/* Clear OUT enable from GPIOs that the driver expects to be IN */
	si_gpioouten(sih, gi, 0, GPIO_DRV_PRIORITY);

	if (CHIPID(wlc_hw->sih->chip) == BCM4313_CHIP_ID)
		si_gpiopull(wlc_hw->sih, GPIO_PULLDN, gm, 0);
	si_gpiocontrol(sih, gm, gm, GPIO_DRV_PRIORITY);
}


#if defined(BCMDBG_PHYDUMP)
static void
wlc_bmac_suspend_dump(wlc_hw_info_t *wlc_hw, struct bcmstrbuf *b)
{
	bmac_suspend_stats_t* stats = wlc_hw->suspend_stats;
	uint32 suspend_time = stats->suspended;
	uint32 unsuspend_time = stats->unsuspended;
	uint32 ratio = 0;
	uint32 timenow = R_REG(wlc_hw->osh, &wlc_hw->regs->tsf_timerlow);
	bool   suspend_active = stats->suspend_start > stats->suspend_end;

	bcm_bprintf(b, "bmac suspend stats---\n");
	bcm_bprintf(b, "Suspend count: %d%s\n", stats->suspend_count,
	            suspend_active ? " ACTIVE" : "");

	if (suspend_active) {
		if (timenow > stats->suspend_start) {
			suspend_time += (timenow - stats->suspend_start) / 100;
			stats->suspend_start = timenow;
		}
	} else {
		if (timenow > stats->suspend_end) {
			unsuspend_time += (timenow - stats->suspend_end) / 100;
			stats->suspend_end = timenow;
		}
	}

	bcm_bprintf(b, "    Suspended: %9d millisecs\n", (suspend_time + 5)/10);
	bcm_bprintf(b, "  Unsuspended: %9d millisecs\n", (unsuspend_time + 5)/10);
	bcm_bprintf(b, "  Max suspend: %9d millisecs\n", (stats->suspend_max + 5)/10);
	bcm_bprintf(b, " Mean suspend: %9d millisecs\n",
	           (suspend_time / (stats->suspend_count ? stats->suspend_count : 1) + 5)/10);

	/* avoid problems with arithmetric overflow */
	while ((suspend_time > (1 << 26)) || (unsuspend_time > (1 << 26))) {
		suspend_time >>= 1;
		unsuspend_time >>= 1;
	}

	if (suspend_time && unsuspend_time) {
		ratio = (suspend_time + unsuspend_time) * 10;
		ratio /= suspend_time;

		if (ratio > 0) {
			ratio = 100000 / ratio;
		}
		ratio = (ratio + 5)/10;
	}

	bcm_bprintf(b, "Suspend ratio: %3d / 1000\n", ratio);

	stats->suspend_count = 0;
	stats->unsuspended = 0;
	stats->suspended = 0;
	stats->suspend_max = 0;
}
#endif	

/* BTC stuff END */

#ifdef STA
/** Change PCIE War override for some platforms */
void
wlc_bmac_pcie_war_ovr_update(wlc_hw_info_t *wlc_hw, uint8 aspm)
{
	if ((BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) &&
		(BUSCORETYPE(wlc_hw->sih->buscoretype) == PCIE_CORE_ID)) {
		si_pcie_war_ovr_update(wlc_hw->sih, aspm);
	}
}

void
wlc_bmac_pcie_power_save_enable(wlc_hw_info_t *wlc_hw, bool enable)
{
	if ((BUSTYPE(wlc_hw->sih->bustype) == PCI_BUS) &&
		(BUSCORETYPE(wlc_hw->sih->buscoretype) == PCIE_CORE_ID)) {
		si_pcie_power_save_enable(wlc_hw->sih, enable);
	}
}
#endif /* STA */

#ifdef BCMUCDOWNLOAD
/** function to write ucode to ucode memory */
int
wlc_handle_ucodefw(wlc_info_t *wlc, wl_ucode_info_t *ucode_buf)
{
	/* for first chunk turn on the clock & do core reset */
	if (ucode_buf->chunk_num == 1) {
		wlc_bmac_xtal(wlc->hw, ON);
		wlc_bmac_corereset(wlc->hw, WLC_USE_COREFLAGS);
	}
	/* write ucode chunk to ucode memory */
	if (WLCISLCNPHY(wlc->hw->band))
		wlc_ucode_write_byte(wlc->hw, &ucode_buf->data_chunk[0], ucode_buf->chunk_len);
	else
		wlc_ucode_write(wlc->hw,  (uint32 *)(&ucode_buf->data_chunk[0]),
			ucode_buf->chunk_len);
	return 0;
}

/**
 * function to handle initvals & bsinitvals. Initvals chunks are accumulated
 * in the driver & kept allocated till 'wl up'. During 'wl up' initvals
 * are written to the memory & then buffer is freed. Even though bsinitvals
 * implementation is also present it is not being downloaded from the host
 * since the size is small & will not be reclaimed if it is dual band image
 */
int
wlc_handle_initvals(wlc_info_t *wlc, wl_ucode_info_t *ucode_buf)
{
	if (ucode_buf->chunk_num == 1) {
		initvals_len = ucode_buf->num_chunks * ucode_buf->chunk_len * sizeof(uint8);
		initvals_ptr = (d11init_t *)MALLOC(wlc->osh, initvals_len);
	}

	bcopy(ucode_buf->data_chunk, (uint8*)initvals_ptr + cumulative_len, ucode_buf->chunk_len);
	cumulative_len += ucode_buf->chunk_len;

	/* when last chunk is received call the write function  */
	if (ucode_buf->chunk_num == ucode_buf->num_chunks)
		wlc->is_initvalsdloaded = TRUE;
	return 0;
}

/**
 * Generic function to handle different downloadable parts like ucode fw
 * & initvals & bsinitvals
 */
int
wlc_process_ucodeparts(wlc_info_t *wlc, uint8 *buf_to_process)
{
	wl_ucode_info_t *ucode_buf = (wl_ucode_info_t *)buf_to_process;
	if (ucode_buf->ucode_type == INIT_VALS)
		wlc_handle_initvals(wlc, ucode_buf);
	else
		wlc_handle_ucodefw(wlc, ucode_buf);
	return 0;
}
#endif /* BCMUCDOWNLOAD */

/**
 * The function is supposed to enable/disable MI_TBTT or M_P2P_I_PRE_TBTT.
 * But since there is no control over M_P2P_I_PRE_TBTT interrupt ,
 * this is achieved by enabling/disabling MI_P2P interrupt as a whole, though
 * that is not the actual intention. The assumption here is if
 * M_P2P_I_PRE_TBTT is no required, no other P2P interrupt will be required.
 * Do not use this function to enable/disable MI_P2P in other conditions.
 * Smply use wlc_bmac_set_defmacintmask() if required.
 */
void
wlc_bmac_enable_tbtt(wlc_hw_info_t *wlc_hw, uint32 mask, uint32 val)
{
	wlc_hw->tbttenablemask = (wlc_hw->tbttenablemask & ~mask) | (val & mask);

	if (wlc_hw->tbttenablemask)
		wlc_bmac_set_defmacintmask(wlc_hw, MI_P2P|MI_TBTT, MI_P2P|MI_TBTT);
	else
		wlc_bmac_set_defmacintmask(wlc_hw, MI_P2P|MI_TBTT, ~(MI_P2P|MI_TBTT));
}

void
wlc_bmac_set_defmacintmask(wlc_hw_info_t *wlc_hw, uint32 mask, uint32 val)
{
	wlc_hw->defmacintmask = (wlc_hw->defmacintmask & ~mask) | (val & mask);
}

#ifdef BPRESET
#include <wlc_scb.h>
void
wlc_full_reset(wlc_hw_info_t *wlc_hw, uint32 val)
{
	osl_t *osh;
	uint32 bar0win;
	uint32 bar0win_after;
	int i;
	uint tmp_bcn_li_dtim;
	uint32 mac_intmask;
	wlc_info_t *wlc = wlc_hw->wlc;
	int ac;

	if (!BPRESET_ENAB(wlc->pub)) {
		WL_ERROR(("wl%d: BPRESET not enabled, do nothing!\n", wlc->pub->unit));
		return;
	}

	/*
	 * 0:	Just show we are alive
	 * 1:	Basic big hammer
	 * 2:	Bigger hammer, big hammer plus backplane reset
	 * 4:	Extra debugging after wl_init
	 * 8:	Issue wl_down() & wl_up() after wl_init
	 */
	WL_ERROR(("wl%d: %s(0x%x): starting backplane reset\n",
	           wlc_hw->unit, __FUNCTION__, val));

	osh = wlc_hw->osh;

	if (val == 0)
		return;

	/* stop DMA */
	if (!PIO_ENAB(wlc_hw->wlc->pub)) {
		for (i = 0; i < NFIFO; i++) {
			if (wlc_hw->di[i]) {
				if (!wlc_bmac_txfifo_reset(wlc_hw, i)) {
					WL_ERROR(("wl%d: %s: bmac_dma_txreset[%d]: "
					"cannot stop dma\n", wlc_hw->unit, __FUNCTION__, i));
					WL_HEALTH_LOG(wlc_hw->wlc, DMATX_ERROR);
				}
				wlc_upd_suspended_fifos_clear(wlc_hw, i);
			}
		}
		if ((wlc_hw->di[RX_FIFO]) && (!wlc_dma_rxreset(wlc_hw, RX_FIFO))) {
			WL_ERROR(("wl%d: %s: dma_rxreset[%d]: cannot stop dma\n",
			          wlc_hw->unit, __FUNCTION__, RX_FIFO));
			WL_HEALTH_LOG(wlc_hw->wlc, DMARX_ERROR);
		}
	} else {
		for (i = 0; i < NFIFO; i++)
			if (wlc_hw->pio[i])
				wlc_pio_reset(wlc_hw->pio[i]);
	}

	WL_NONE(("wl%d: %s: up %d, hw->up %d, sbclk %d, clk %d, hw->clk %d, fastclk %d\n",
	         wlc_hw->unit, __FUNCTION__, wlc_hw->wlc->pub->up, wlc_hw->up,
	         wlc_hw->sbclk, wlc_hw->wlc->clk, wlc_hw->clk, wlc_hw->forcefastclk));

	if (val & 2) {
		/* cause chipc watchdog */
		WL_INFORM(("wl%d: %s: starting chipc watchdog\n",
		           wlc_hw->unit, __FUNCTION__));

		bar0win = OSL_PCI_READ_CONFIG(osh, PCI_BAR0_WIN, sizeof(uint32));

		/* Stop interrupt handling */
		wlc_hw->macintmask = 0;

		wlc_bmac_set_ctrl_SROM(wlc_hw);
		if (R_REG(wlc_hw->osh, &wlc_hw->regs->maccontrol) & MCTL_EN_MAC) {
			wlc_bmac_suspend_mac_and_wait(wlc_hw);
		}

		if (CHIPID(wlc_hw->sih->chip) == BCM4331_CHIP_ID &&
		    ((D11REV_IS(wlc_hw->corerev, 26) && CHIPREV(wlc_hw->sih->chiprev) == 0) ||
		     D11REV_IS(wlc_hw->corerev, 29))) {
			si_corereg(wlc_hw->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, chipcontrol),
				(CCTRL4331_EXTPA_EN | CCTRL4331_EXTPA_EN2 |
				CCTRL4331_EXTPA_ON_GPIO2_5), 0);
		}

		/* Write the watchdog */
		si_corereg(wlc_hw->sih, SI_CC_IDX, OFFSETOF(chipcregs_t, watchdog), ~0, 100);

		/* Srom read takes ~12mS */
		OSL_DELAY(20000);

		bar0win_after = OSL_PCI_READ_CONFIG(osh, PCI_BAR0_WIN, sizeof(uint32));

		if (bar0win_after != bar0win) {
			WL_ERROR(("wl%d: %s: bar0win before %08x, bar0win after %08x\n",
			          wlc_hw->unit, __FUNCTION__, bar0win, bar0win_after));
			OSL_PCI_WRITE_CONFIG(osh, PCI_BAR0_WIN, sizeof(uint32), bar0win);
		}

		/* If the core is up, the watchdog did not take effect */
		if (si_iscoreup(wlc_hw->sih))
			WL_ERROR(("wl%d: %s: Core still up after WD\n",
			          wlc_hw->unit, __FUNCTION__));

		/* Fixup the state to say the chip (or at least d11) is down */
		wlc_hw->clk = FALSE;

		/* restore hardware related stuff */
		wlc_bmac_up_prep(wlc_hw);
	}

	WL_INFORM(("wl%d: %s: about to wl_init()\n", wlc_hw->unit, __FUNCTION__));

	tmp_bcn_li_dtim = wlc_hw->wlc->bcn_li_dtim;
	wlc_hw->wlc->bcn_li_dtim = 0;
	wlc_fatal_error(wlc_hw->wlc);	/* big hammer */

	/* Propagate rfaware_lifetime setting to ucode */
	wlc_rfaware_lifetime_set(wlc, wlc->rfaware_lifetime);

	/* for full backplane reset, need to reenable interrupt */
	if (val & 2) {
		/* FULLY enable dynamic power control and d11 core interrupt */
		wlc_clkctl_clk(wlc_hw, CLK_DYNAMIC);
		ASSERT(wlc_hw->macintmask == 0);
		ASSERT(!wlc_hw->sbclk || !si_taclear(wlc_hw->sih, TRUE));
		wl_intrson(wlc_hw->wlc->wl);
	}

	mac_intmask = wlc_intrsoff(wlc_hw->wlc);
	wlc_bmac_set_ctrl_ePA(wlc_hw);
	if (!WLCISACPHY(wlc_hw->band))
		wlc_bmac_set_btswitch(wlc_hw, wlc_hw->btswitch_ovrd_state);
	wlc_intrsrestore(wlc_hw->wlc, mac_intmask);

	/* Write WME tunable parameters for retransmit/max rate from wlc struct to ucode */
	for (ac = 0; ac < AC_COUNT; ac++) {
		wlc_bmac_write_shm(wlc_hw, M_AC_TXLMT_ADDR(wlc_hw, ac),
			wlc_hw->wlc->wme_retries[ac]);
	}
	/* sanitize any existing scb rates */
	wlc_scblist_validaterates(wlc);
	/* ensure antenna config is up to date */
	wlc_stf_phy_txant_upd(wlc);

	wlc_hw->wlc->bcn_li_dtim = tmp_bcn_li_dtim;

	WL_INFORM(("wl%d: %s: back from wl_init()\n", wlc_hw->unit, __FUNCTION__));
	WL_NONE(("wl%d: %s: up %d, hw->up %d, sbclk %d, clk %d, hw->clk %d, fastclk %d\n",
	         wlc_hw->unit, __FUNCTION__, wlc_hw->wlc->pub->up, wlc_hw->up,
	         wlc_hw->sbclk, wlc_hw->wlc->clk, wlc_hw->clk, wlc_hw->forcefastclk));

	if (val & 8) {
		WL_INFORM(("wl%d: %s: calling wl_down()\n", wlc_hw->unit, __FUNCTION__));
		wl_down(wlc_hw->wlc->wl);

		WL_INFORM(("wl%d: %s: calling wl_up()\n", wlc_hw->unit, __FUNCTION__));
		wl_up(wlc_hw->wlc->wl);
	}
	WL_INFORM(("wl%d: %s(0x%x): done in %dmS\n", wlc_hw->unit, __FUNCTION__, val,
	           OSL_SYSUPTIME() - start));
} /* wlc_full_reset */
#endif	/* BPRESET */

/** Returns 1 if any error is detected in TXFIFO configuration */
static bool
BCMINITFN(wlc_bmac_txfifo_sz_chk)(wlc_hw_info_t *wlc_hw)
{
	d11regs_t *regs = wlc_hw->regs;
	osl_t *osh;
	uint16 fifo_nu = 0;
	uint16 txfifo_cmd_org = 0;
	uint16 txfifo_cmd = 0;

	uint16 txfifo_def = 0;
	uint16 txfifo_def1 = 0;

	/* Index of "256 byte" block where this FIFO starts */
	uint16 txfifo_start = 0;
	/* Index of "256 byte" block where this FIFO ends */
	uint16 txfifo_end = 0;
	/* Number of "256 byte" blocks used so far */
	uint16 txfifo_used = 0;
	/* Total number of "256 byte" blocks available in chip */
	uint16 txfifo_total;
	bool err = 0;

	osh = wlc_hw->osh;

	/* Adjust size as MACHWCAP gives size in "512 blocks" */
	txfifo_total = ((wlc_hw->machwcap & MCAP_TXFSZ_MASK) >> MCAP_TXFSZ_SHIFT) * 2;

	/* Store current value of xmtfifocmd for restoring later */
	txfifo_cmd_org = R_REG(osh, &regs->u.d11regs.xmtfifocmd);

	/* Read all configured FIFO size entries and check if they are valid */
	for (fifo_nu = 0; fifo_nu < NFIFO; fifo_nu++) {
		/* Select the FIFO */
		txfifo_cmd = ((txfifo_cmd_org & ~TXFIFOCMD_FIFOSEL_SET(-1)) |
			TXFIFOCMD_FIFOSEL_SET(fifo_nu));
		W_REG(osh, &regs->u.d11regs.xmtfifocmd, txfifo_cmd);

		/* Read the current configured size */
		txfifo_def = R_REG(osh, &regs->u.d11regs.xmtfifodef);
		txfifo_def1 = R_REG(osh, &regs->u.d11regs.xmtfifodef1);

		/* Validate the size of the template fifo too */
		if (fifo_nu == 0) {
			if (TXFIFO_FIFO_START(txfifo_def, txfifo_def1) == 0) {
				WL_ERROR(("wl%d: %s: Template FIFO size is zero\n",
				          wlc_hw->unit, __FUNCTION__));
				ASSERT(0);
				err = 1;
				break;
			}

			/* End of template FIFO is just before start of fifo0 */
			txfifo_end = (TXFIFO_FIFO_START(txfifo_def, txfifo_def1) - 1);
			txfifo_used += ((txfifo_end - txfifo_start) + 1);
		}

		txfifo_start = TXFIFO_FIFO_START(txfifo_def, txfifo_def1);
		/* Check FIFO overlap with previous FIFO */
		if (txfifo_start < txfifo_end) {
			WL_ERROR(("wl%d: %s: FIFO %d overlaps with FIFO %d\n",
				wlc_hw->unit, __FUNCTION__, fifo_nu,
				((fifo_nu == 0) ? -1 : (fifo_nu-1))));
			ASSERT(0);
			err = 1;
			break;

		/* If consecutive blocks are not contiguous, this function cannot check overlap */
		} else if (txfifo_start != (txfifo_end + 1)) {
			WL_ERROR(("wl%d: %s: FIFO %d not contiguous with previous FIFO."
			"Cannot check overlap. (start=%d prev_end=%d)\n",
				wlc_hw->unit, __FUNCTION__, fifo_nu,
				txfifo_start, txfifo_end));
			ASSERT(0);
			err = 1;
			break;
		}
		txfifo_end = TXFIFO_FIFO_END(txfifo_def, txfifo_def1);
		/* Fifo should be configured to atleast 1 block */
		if (txfifo_end < txfifo_start) {
			WL_ERROR(("wl%d: %s: FIFO %d config invalid. start=%d and end=%d\n",
				wlc_hw->unit, __FUNCTION__, fifo_nu,
				txfifo_start, txfifo_end));
			ASSERT(0);
			err = 1;
			break;
		}
		txfifo_used += ((txfifo_end - txfifo_start) + 1);
		/* At any point, FIFO size used should not exceed capacity */
		if (txfifo_used > txfifo_total) {
			WL_ERROR(("wl%d: %s: FIFO %d config causes memblk usage %d"
			"to exceed chip capacity %d\n",
				wlc_hw->unit, __FUNCTION__, fifo_nu,
				txfifo_used, txfifo_total));
			ASSERT(0);
			err = 1;
			break;
		}
		WL_INFORM(("wl%d: %s: FIFO %d block config, "
		"start=%d end=%d sz=%d used=%d avail=%d\n",
			wlc_hw->unit, __FUNCTION__, fifo_nu,
			txfifo_start, txfifo_end,
			((txfifo_end - txfifo_start) + 1),
			txfifo_used, (txfifo_total - txfifo_used)));
	}
	/* Restore xmtfifocmd configuration */
	W_REG(osh, &regs->u.d11regs.xmtfifocmd, txfifo_cmd_org);

	return err;
} /* wlc_bmac_txfifo_sz_chk */


static int
BCMINITFN(wlc_corerev_fifosz_validate)(wlc_hw_info_t *wlc_hw, uint16 *buf)
{
	int i = 0, err = 0;

	/* check txfifo allocations match between ucode and driver */
	buf[TX_AC_BE_FIFO] = wlc_bmac_read_shm(wlc_hw, M_FIFOSIZE0(wlc_hw));
	if (buf[TX_AC_BE_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_BE_FIFO]) {
		i = TX_AC_BE_FIFO;
		err = -1;
	}
	buf[TX_AC_VI_FIFO] = wlc_bmac_read_shm(wlc_hw, M_FIFOSIZE1(wlc_hw));
	if (buf[TX_AC_VI_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_VI_FIFO]) {
		i = TX_AC_VI_FIFO;
	        err = -1;
	}
	buf[TX_AC_BK_FIFO] = wlc_bmac_read_shm(wlc_hw, M_FIFOSIZE2(wlc_hw));
	buf[TX_AC_VO_FIFO] = (buf[TX_AC_BK_FIFO] >> 8) & 0xff;
	buf[TX_AC_BK_FIFO] &= 0xff;
	if (buf[TX_AC_BK_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_BK_FIFO]) {
		i = TX_AC_BK_FIFO;
	        err = -1;
	}
	if (buf[TX_AC_VO_FIFO] != wlc_hw->xmtfifo_sz[TX_AC_VO_FIFO]) {
		i = TX_AC_VO_FIFO;
		err = -1;
	}
	buf[TX_BCMC_FIFO] = wlc_bmac_read_shm(wlc_hw, M_FIFOSIZE3(wlc_hw));
	buf[TX_ATIM_FIFO] = (buf[TX_BCMC_FIFO] >> 8) & 0xff;
	buf[TX_BCMC_FIFO] &= 0xff;
	if (buf[TX_BCMC_FIFO] != wlc_hw->xmtfifo_sz[TX_BCMC_FIFO]) {
		i = TX_BCMC_FIFO;
		err = -1;
	}
	if (buf[TX_ATIM_FIFO] != wlc_hw->xmtfifo_sz[TX_ATIM_FIFO]) {
		i = TX_ATIM_FIFO;
		err = -1;
	}
	if (err != 0) {
		WL_ERROR(("wlc_coreinit: txfifo mismatch: ucode size %d driver size %d index %d\n",
			buf[i], wlc_hw->xmtfifo_sz[i], i));
		/* DO NOT ASSERT corerev < 4 even there is a mismatch
		 * shmem, since driver don't overwrite those chip and
		 * ucode initialize data will be used.
		 */
		if (D11REV_GE(wlc_hw->corerev, 4))
			ASSERT(0);
	}

#ifdef WLAMPDU_HW
	for (i = 0; i < AC_COUNT; i++) {
		wlc_hw->xmtfifo_frmmax[i] =
		        (wlc_hw->xmtfifo_sz[i] * 256 - 1300) / MAX_MPDU_SPACE;
		WL_INFORM(("%s: fifo sz blk %d entries %d\n",
			__FUNCTION__, wlc_hw->xmtfifo_sz[i], wlc_hw->xmtfifo_frmmax[i]));
	}
#else
	BCM_REFERENCE(i);
#endif	/* WLAMPDU_HW */
	return err;
} /* wlc_corerev_fifosz_validate */

typedef struct _bmc_params {
	uint8	rxq_in_bm;	    /* 1: rx queues are allocated in BMC, 0: not */
	uint16	rxq0_buf;	    /* number of buffers (in 512 bytes) for rx queue 0
				     * if rxbmmap_is_en == 1, this number indicates
				     * the fifo boundary
				     */
	uint16	rxq1_buf;	    /* number of buffers for rx queue 1 */
	uint8	rxbmmap_is_en;	    /* 1: rx queues are managed as fifo, 0: not */
	uint8	tx_flowctrl_scheme; /* 1: new tx flow control scheme,
				     *	  don't preallocate as many buffers,
				     * 0: old scheme, preallocate
				     */
	uint16	full_thresh;	    /* used when tx_flowctrl_scheme == 0 */
	uint16	minbufs[];
} bmc_params_t;

static const bmc_params_t bmc_params_40 = {0, 0, 0, 0, 0, 11, {32, 32, 32, 32, 32, 8}};
static const bmc_params_t bmc_params_41 = {0, 0, 0, 0, 0, 6, {32, 32, 32, 32, 32, 0}};
static const bmc_params_t bmc_params_42 = {0, 0, 0, 0, 0, 11, {32, 32, 32, 32, 32, 32}};
static const bmc_params_t bmc_params_43 = {1, 40, 40, 0, 0, 11, {32, 32, 32, 32, 32, 32}};
static const bmc_params_t bmc_params_44 = {0, 0, 0, 0, 0, 6, {32, 32, 32, 32, 32, 0}};
static const bmc_params_t bmc_params_45 = {1, 20, 20, 0, 0, 6, {32, 32, 32, 32, 32, 0}};
static const bmc_params_t bmc_params_46 = {0, 0, 0, 0, 0, 6, {32, 32, 32, 32, 32, 0}};
/* corerev 47 uses bmc_params_45 */
static const bmc_params_t bmc_params_48 =
	{1, 128, 128, 1, 1, 0, {32, 32, 32, 32, 32, 0, 20, 0, 10}};
static const bmc_params_t bmc_params_49 =
	{1, 192, 192, 1, 1, 0, {32, 32, 32, 32, 10, 0, 92, 0, 10}};
static const bmc_params_t bmc_params_50 =
	{1, 128, 128, 1, 1, 0, {16, 16, 16, 16, 16, 0, 96, 0, 10}};
static const bmc_params_t bmc_params_55 =
	{1, 128, 128, 1, 1, 0, {16, 16, 16, 16, 16, 0, 96, 0, 10}};
static const bmc_params_t bmc_params_56 =
	{1, 128, 128, 1, 1, 0, {16, 16, 16, 16, 16, 0, 32, 0, 10}};

/* corerev 51 uses bmc_params_45 */
static const bmc_params_t bmc_params_60 =
	{1, 20, 2, 1, 1, 0, {8, 64, 8, 8, 8, 8, 20, 0, 2}};

static const bmc_params_t bmc_params_54 =
	{1, 128, 128, 1, 1, 0, {16, 32, 32, 16, 8, 0, 96, 0, 10}};

/* The values need to be finalized after d11 core freeze */
static const bmc_params_t bmc_params_58_core0 =
	{1, 128, 128, 1, 1, 0, {32, 32, 32, 32, 32, 0, 96, 0, 10}};

static const bmc_params_t bmc_params_58_core1 =
	{1, 64, 64, 1, 1, 0, {16, 16, 16, 16, 16, 0, 40, 0, 10}};


static const bmc_params_t *bmc_params = NULL;

static uint16 bmc_maxbufs;
static uint16 bmc_nbufs = D11MAC_BMC_MAXBUFS;

static void
wlc_bmac_bmc_template_allocstatus(wlc_hw_info_t *wlc_hw, uint32 mac_core_unit, int tplbuf)
{
	volatile uint16 *alloc_status;

	ASSERT(wlc_hw != NULL);
	ASSERT(wlc_hw->regs != NULL);

	if (mac_core_unit == MAC_CORE_UNIT_0) {
		alloc_status = (volatile uint16 *)
			&wlc_hw->regs->u.d11acregs.Core0BMCAllocStatusTID7;
	} else {
		alloc_status = (volatile uint16 *)
			&wlc_hw->regs->u.d11acregs.Core1BMCAllocStatusTID7;
	}

	SPINWAIT((R_REG(wlc_hw->osh, alloc_status) != (uint)tplbuf), 10);

	if (R_REG(wlc_hw->osh, alloc_status) != (uint)tplbuf) {
		WL_ERROR(("Error BMC buffer allocation: TID 7 of Core unit %d reg 0x%p val 0x%x",
		mac_core_unit, alloc_status, R_REG(wlc_hw->osh, alloc_status)));
	}
}

/** buffer manager initialisation */
static int
BCMINITFN(wlc_bmac_bmc_init)(wlc_hw_info_t *wlc_hw)
{
	osl_t *osh;
	d11regs_t *regs;
	uint32 bmc_ctl;
	uint16 maxbufs, minbufs, alloc_cnt, alloc_thresh, full_thresh, buf_desclen;
	int bmc_fifo_list[D11MAC_BMC_MAXFIFOS] = {7, 0, 1, 2, 3, 4, 5, 6, 8};
	int num_of_fifo, rxmapfifosz;

	/* used for BMCCTL */
	uint8 bufsize = D11MAC_BMC_BUFSIZE_512BLOCK;
	uint8 loopback = 0;
	uint8 reset_stats = 0;
	uint8 init = 1;

	int i, fifo;
	uint32 fifo_sz;
	int tplbuf = D11MAC_BMC_TPL_NUMBUFS;
	uint32 bmc_startaddr = D11MAC_BMC_STARTADDR;
	uint8 doublebufsize = 0;

	int rxq0buf, rxq1buf;
	int rxq0_more_bufs = 0;
	uint sicoreunit = 0;
	uint8 clkgateen = 1;

	osh = wlc_hw->osh;
	regs = wlc_hw->regs;

	if (D11REV_IS(wlc_hw->corerev, 58)) {
		if (wlc_hw->macunit == 0) {
			bmc_params = &bmc_params_58_core0;
		} else if (wlc_hw->macunit == 1) {
			bmc_params = &bmc_params_58_core1;
		} else {
			ASSERT(0);
		}
	} else if (D11REV_IS(wlc_hw->corerev, 52) ||
	    D11REV_IS(wlc_hw->corerev, 51) ||
	    D11REV_IS(wlc_hw->corerev, 47) ||
	    D11REV_IS(wlc_hw->corerev, 45))
		bmc_params = &bmc_params_45;
	else if (D11REV_IS(wlc_hw->corerev, 54))
		bmc_params = &bmc_params_54;
	else if (D11REV_IS(wlc_hw->corerev, 60))
		bmc_params = &bmc_params_60;
	else if (
		 D11REV_IS(wlc_hw->corerev, 64) ||
		 D11REV_IS(wlc_hw->corerev, 50))
		bmc_params = &bmc_params_50;
	else if (D11REV_IS(wlc_hw->corerev, 49))
		bmc_params = &bmc_params_49;
	else if (D11REV_IS(wlc_hw->corerev, 55))
		bmc_params = &bmc_params_55;
	else if (D11REV_IS(wlc_hw->corerev, 56))
		bmc_params = &bmc_params_56;
	else if (D11REV_IS(wlc_hw->corerev, 59))
		bmc_params = &bmc_params_55;
	else if (D11REV_IS(wlc_hw->corerev, 48))
		bmc_params = &bmc_params_48;
	else if (D11REV_IS(wlc_hw->corerev, 46))
		bmc_params = &bmc_params_46;
	else if (D11REV_IS(wlc_hw->corerev, 44))
		bmc_params = &bmc_params_44;
	else if (D11REV_IS(wlc_hw->corerev, 43))
		bmc_params = &bmc_params_43;
	else if (D11REV_IS(wlc_hw->corerev, 42))
		bmc_params = &bmc_params_42;
	else if (D11REV_IS(wlc_hw->corerev, 41))
		bmc_params = &bmc_params_41;
	else if (D11REV_IS(wlc_hw->corerev, 40))
		bmc_params = &bmc_params_40;
	else {
		WL_ERROR(("corerev %d not supported\n", wlc_hw->corerev));
		ASSERT(0);
		return BCME_ERROR;
	}

	/* CRWLDOT11M-1160, impacts both revs 48, 49 */
	if (D11REV_IS(wlc_hw->corerev, 48) || D11REV_IS(wlc_hw->corerev, 49)) {

		/* Minimum region of TPL FIFO#7 */
		int tpl_fifo_sz = D11MAC_BMC_TPL_BUFS_BYTES;

		/* Need it to be 512B/buffer */
		bufsize = D11MAC_BMC_BUFSIZE_512BLOCK;

		/* start at 80KB, there are fewer buffers available for BMC use */
		bmc_startaddr = D11MAC_BMC_STARTADDR_SRASM;

#if defined(SAVERESTORE) && defined(SR_ESSENTIALS)
		/* When SR is disabled, allot the unused SR ASM space to RXQ0 FIFO */
		if (SR_ESSENTIALS_ENAB()) {
			/* Allot space in TPL FIFO#7 for 4KB aligned SR ASM */
			tpl_fifo_sz += D11MAC_BMC_SRASM_BYTES;
		} else
#endif /* (SAVERESTORE && SR_ESSENTIALS) */
		{
			/* Increase RXQ0 FIFO#6 by SR ASM unused space */
			rxq0_more_bufs = D11MAC_BMC_SRASM_NUMBUFS;
		}

		/* Number of 512 Bytes buffers for TPL FIFO#7 */
		tplbuf = D11MAC_BMC_BUFS_512(tpl_fifo_sz);
	} else if (D11REV_IS(wlc_hw->corerev, 50) || D11REV_IS(wlc_hw->corerev, 54) ||
		D11REV_IS(wlc_hw->corerev, 55) || D11REV_IS(wlc_hw->corerev, 56) ||
		D11REV_IS(wlc_hw->corerev, 59) || D11REV_IS(wlc_hw->corerev, 60) ||
		D11REV_IS(wlc_hw->corerev, 58) ||
		D11REV_IS(wlc_hw->corerev, 61) ||
		wlc_bmac_rsdb_cap(wlc_hw)) {
		bufsize = D11MAC_BMC_BUFSIZE_256BLOCK;
	}
	/* Steps followed:
	* 1. BMC configuration registers are accessed from core-0.
	* 2. This follows template MSDU initialization which is core specific program.
	*/
	if (wlc_bmac_rsdb_cap(wlc_hw)) {
		sicoreunit = si_coreunit(wlc_hw->sih);
		regs = (d11regs_t *)si_d11_switch_addrbase(wlc_hw->sih, 0);
	}
	/* MCTL_IHR_EN is also used in DEVICEREMOVED macro to identify the device state
	* hence it is not suppose to happen at init.
	* forcing this bit for register access.
	*/
	OR_REG(osh, &regs->maccontrol, MCTL_IHR_EN);

	if (bufsize == D11MAC_BMC_BUFSIZE_256BLOCK) {
		doublebufsize = 1;
		if (wlc_bmac_rsdb_cap(wlc_hw) || D11REV_IS(wlc_hw->corerev, 60)) {
			/* Consider max/min to both core templates and sr array area */
			tplbuf = (si_numcoreunits(wlc_hw->sih, D11_CORE_ID) *
				D11MAC_BMC_TPL_NUMBUFS_PERCORE) + D11MAC_BMC_SR_NUMBUFS;
		} else {
			tplbuf = D11MAC_BMC_BUFS_256(D11MAC_BMC_TPL_BYTES);
		}
	}

	/* Derive from machwcap registers */
	fifo_sz = ((R_REG(osh, &regs->machwcap) & MCAP_TXFSZ_MASK) >> MCAP_TXFSZ_SHIFT) * 2048;

	/* Account for bmc_startaddr which is specified in units of 256B */
	bmc_maxbufs = (fifo_sz - (bmc_startaddr << 8)) >> (8 + bufsize);

	if (bmc_params->rxq_in_bm) {
		rxq0buf = bmc_params->rxq0_buf;
		rxq1buf = bmc_params->rxq1_buf;

		if (bmc_params->rxbmmap_is_en) {  /* RXBMMAP is enabled */
			/* Convert to word addresses, num of buffer * 512 / 4 */
			W_REG(osh, &regs->u_rcv.d11acregs.rcv_bm_sp_q0, 0);
			W_REG(osh, &regs->u_rcv.d11acregs.rcv_bm_ep_q0, (rxq0buf << 7) - 1);

			W_REG(osh, &regs->u_rcv.d11acregs.rcv_bm_sp_q1, rxq0buf << 7);
			W_REG(osh, &regs->u_rcv.d11acregs.rcv_bm_ep_q1,
				((rxq0buf + rxq1buf) << 7) - 1);
		} else {
			/* This corresponds to the case where rxbmmap
			 * is not present/disabled/passthru
			 * Convert to word addresses, num of buffer * 512 / 4
			 */
			W_REG(osh, &regs->u_rcv.d11acregs.rcv_bm_sp_q0, tplbuf << 7);
			W_REG(osh, &regs->u_rcv.d11acregs.rcv_bm_ep_q0,
				((tplbuf + rxq0buf) << 7) - 1);

			W_REG(osh, &regs->u_rcv.d11acregs.rcv_bm_sp_q1, (tplbuf + rxq0buf) << 7);
			W_REG(osh, &regs->u_rcv.d11acregs.rcv_bm_ep_q1,
				((tplbuf + rxq0buf + rxq1buf) << 7) - 1);

			tplbuf += rxq0buf + rxq1buf;
		}

		/* Reset the RXQs to have the pointers take effect;resets are self-clearing */
		W_REG(osh, &regs->rcv_fifo_ctl, 0x101);	/* sel and reset q1 */
		W_REG(osh, &regs->rcv_fifo_ctl, 0x001);	/* sel and reset q0 */
	}

	/* init the total number for now */
	bmc_nbufs = bmc_maxbufs;
	W_REG(osh, &regs->u.d11acregs.BMCConfig, bmc_nbufs);
	bmc_ctl = (loopback << BMCCTL_LOOPBACK_SHIFT) 			|
	        (bufsize << BMCCTL_TXBUFSIZE_SHIFT) 	|
	        (reset_stats << BMCCTL_RESETSTATS_SHIFT)		|
	        (init << BMCCTL_INITREQ_SHIFT);

	/*
	 * Enable hardware clock gating for BM memories, only for 4350A0/B0/B1.
	 * The memory wrapper in BM don't have the chip select clock gate feature, increasing
	 * Tx/Rx currents -- setting it to 1 to enable hardware clock gating. Code is conditioned
	 * to MAC rev 43 only, as 4350C0 will handle this in different manner.
	 */
	if (D11REV_IS(wlc_hw->corerev, 43))
		bmc_ctl |= (clkgateen << BMCCTL_CLKGATEEN_SHIFT);

	W_REG(osh, &regs->u.d11acregs.BMCCTL, bmc_ctl);
	SPINWAIT((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCTL) & BMC_CTL_DONE), 200);
	if (R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCTL) & BMC_CTL_DONE) {
		WL_ERROR(("wl%d: bmc init not done yet :-(\n", wlc_hw->unit));
	}

	buf_desclen = ((D11AC_TXH_LEN - DOT11_FCS_LEN - AMPDU_DELIMITER_LEN)
		       << BMCDescrLen_LongLen_SHIFT)
		| (D11AC_TXH_SHORT_LEN - DOT11_FCS_LEN - AMPDU_DELIMITER_LEN);

	if (bmc_params->rxbmmap_is_en)
		num_of_fifo = 9;
	else
		num_of_fifo = 7;

	for (i = 0; i < num_of_fifo; i++) {
		fifo = bmc_fifo_list[i];
		/* configure per-fifo parameters and enable them one fifo by fifo
		 * always init template first to guarantee template start from first buffer
		 */
		if (fifo == D11MAC_BMC_TPL_IDX) {
			maxbufs = (uint16)tplbuf;
			minbufs = maxbufs;
			full_thresh = maxbufs;
			alloc_cnt = minbufs;
			alloc_thresh = alloc_cnt - 4;
		} else {
			if (fifo == 6 || fifo == 8) {	/* rx fifo */
				int rxq0_bufs = bmc_params->minbufs[fifo] << doublebufsize;

				if (fifo == 6)
					rxq0_bufs += rxq0_more_bufs; /* any unused SR ASM space */

				maxbufs = rxq0_bufs + 3;
				minbufs = rxq0_bufs + 3;

				rxmapfifosz = rxq0_bufs;
				W_REG(osh, &regs->u.d11acregs.RXMapFifoSize, rxmapfifosz);
#ifdef WAR_HW_RXFIFO_0
				wlc_war_rxfifo_shm(wlc_hw, fifo, rxmapfifosz);
#endif
			} else {
				maxbufs = bmc_nbufs - tplbuf;
				minbufs = bmc_params->minbufs[fifo] << doublebufsize;
			}

			if (bmc_params->tx_flowctrl_scheme == 0) {
				full_thresh = bmc_params->full_thresh;
				alloc_cnt = 2 * full_thresh;
				alloc_thresh = alloc_cnt - 4;
			} else {
				full_thresh = 1;
				alloc_cnt = 2;
				alloc_thresh = 2;
			}
		}

		W_REG(osh, &regs->u.d11acregs.BMCMaxBuffers, maxbufs);
		W_REG(osh, &regs->u.d11acregs.BMCMinBuffers, minbufs);
		W_REG(osh, &regs->u.d11acregs.XmtFIFOFullThreshold, full_thresh);
		if (D11REV_IS(wlc_hw->corerev, 50) || D11REV_GT(wlc_hw->corerev, 52)) {
			W_REG(osh, &regs->u.d11acregs.BMCAllocCtl,
			(alloc_thresh << BMCAllocCtl_AllocThreshold_SHIFT_Rev50) | alloc_cnt);

			/* If the MSDUINDEXFIFO for a given TID,
			 * has fewer entries then the buffer arbiter doesn't grant requests
			 */
			W_REG(osh, &regs->u.d11acregs.MsduThreshold, 0x8);
		} else {
			W_REG(osh, &regs->u.d11acregs.BMCAllocCtl,
			(alloc_thresh << BMCAllocCtl_AllocThreshold_SHIFT) | alloc_cnt);
		}
		W_REG(osh, &regs->u.d11acregs.BMCDescrLen, buf_desclen);

		/* Enable this fifo */
		W_REG(osh, &regs->u.d11acregs.BMCCmd, fifo | (1 << BMCCmd_Enable_SHIFT));

		if (D11REV_IS(wlc_hw->corerev, 50) || D11REV_GE(wlc_hw->corerev, 54)) {
			if (fifo == D11MAC_BMC_TPL_IDX) {
				wlc_bmac_bmc_template_allocstatus(wlc_hw, MAC_CORE_UNIT_0, tplbuf);
			}
		}

		if (RSDB_ENAB(wlc_hw->wlc->pub)) {
			/* 4349 . Set maccore_sel to 1 for Core 1 */
			if (wlc_bmac_rsdb_cap(wlc_hw) &&
				(si_coreunit(wlc_hw->sih) == 0) &&
				(wlc_rsdb_mode(wlc_hw->wlc) == PHYMODE_RSDB)) {
				W_REG(osh, &regs->u.d11acregs.BMCCmd,
					fifo | (1 << 10) | (1 <<  BMCCmd_Enable_SHIFT));
				if (fifo == D11MAC_BMC_TPL_IDX) {
					wlc_bmac_bmc_template_allocstatus(wlc_hw,
					MAC_CORE_UNIT_1, tplbuf);
				}
			}
		}
	}

	if (wlc_bmac_rsdb_cap(wlc_hw)) {
		tplbuf =  D11MAC_BMC_TPL_NUMBUFS_PERCORE;
	}

	/* init template */
	for (i = 0; i < tplbuf; i ++) {
		int end_idx = i + 2 + doublebufsize;

		if (end_idx >= tplbuf)
			end_idx = tplbuf - 1;
		W_REG(osh, &regs->u.d11acregs.MSDUEntryStartIdx, i);
		W_REG(osh, &regs->u.d11acregs.MSDUEntryEndIdx, end_idx);
		W_REG(osh, &regs->u.d11acregs.MSDUEntryBufCnt, end_idx - i + 1);
		W_REG(osh, &regs->u.d11acregs.PsmMSDUAccess,
		      ((1 << PsmMSDUAccess_WriteBusy_SHIFT) |
		       (i << PsmMSDUAccess_MSDUIdx_SHIFT) |
		       (D11MAC_BMC_TPL_IDX << PsmMSDUAccess_TIDSel_SHIFT)));

		SPINWAIT((R_REG(wlc_hw->osh, &regs->u.d11acregs.PsmMSDUAccess) &
			(1 << PsmMSDUAccess_WriteBusy_SHIFT)), 200);
		if (R_REG(wlc_hw->osh, &regs->u.d11acregs.PsmMSDUAccess) &
		    (1 << PsmMSDUAccess_WriteBusy_SHIFT))
			{
				WL_ERROR(("wl%d: PSM MSDU init not done yet :-(\n", wlc_hw->unit));
			}
	}

#ifdef WLRSDB
	if (wlc_bmac_rsdb_cap(wlc_hw)) {
		wlc_rsdb_bmc_smac_template(wlc_hw->wlc, tplbuf, doublebufsize);
	}
#endif
	if (wlc_bmac_rsdb_cap(wlc_hw)) {
		si_d11_switch_addrbase(wlc_hw->sih, sicoreunit);
	}
	WL_INFORM(("wl%d: bmc_init done\n", wlc_hw->unit));
	return 0;
} /* wlc_bmac_bmc_init */



void
wlc_bmac_tsf_adjust(wlc_hw_info_t *wlc_hw, int delta)
{
	uint32 tsf_l, tsf_h;
	uint32 delta_h;
	osl_t *osh = wlc_hw->osh;
	d11regs_t *regs = wlc_hw->regs;

	/* adjust the tsf time by offset */
	wlc_bmac_read_tsf(wlc_hw, &tsf_l, &tsf_h);
	/* check for wrap:
	 * if we are close to an overflow (2 ms) from tsf_l to high,
	 * make sure we did not read tsf_h after the overflow
	 */
	if (tsf_l > (uint32)(-2000)) {
		uint32 tsf_l_new;
		tsf_l_new = R_REG(osh, &regs->tsf_timerlow);
		/* update the tsf_h if tsf_l rolled over since we do not know if we read tsf_h
		 * before or after the roll over
		 */
		if (tsf_l_new < tsf_l)
			tsf_h = R_REG(osh, &regs->tsf_timerhigh);
		tsf_l = tsf_l_new;
	}

	/* sign extend delta to delta_h */
	if (delta < 0)
		delta_h = -1;
	else
		delta_h = 0;

	wlc_uint64_add(&tsf_h, &tsf_l, delta_h, (uint32)delta);

	W_REG(osh, &regs->tsf_timerlow, tsf_l);
	W_REG(osh, &regs->tsf_timerhigh, tsf_h);
}

void
wlc_bmac_update_bt_chanspec(wlc_hw_info_t *wlc_hw,
	chanspec_t chanspec, bool scan_in_progress, bool roam_in_progress)
{
}

int wlc_bmac_is_singleband_5g(unsigned int device)
{
	return (_IS_SINGLEBAND_5G(device));
}

int wlc_bmac_srvsdb_force_set(wlc_hw_info_t *wlc_hw, uint8 force)
{
	wlc_hw->sr_vsdb_force = force;
	return BCME_OK;
}

/**
 * Function to set input mac address in SHM for ucode generated CTS2SELF. The Mac addresses are
 * written out 2 bytes at a time at the specific SHM location. For non-AC chips this mac address was
 * retrieved from the RCMTA by ucode directly. For AC chips there is a bug that prevents access to
 * the search engine by ucode. For CTS packets (normal and CTS2SELF), the mac address is bit-
 * substituted before transmission. So we use the address set in this SHM location for CTS2SELF
 * packets. GE40 only.
 */
static void
wlc_bmac_set_myaddr(wlc_hw_info_t *wlc_hw, struct ether_addr *mac_addr)
{
	unsigned short mac;

	mac = ((mac_addr->octet[1]) << 8) | mac_addr->octet[0];
	wlc_bmac_write_shm(wlc_hw, M_MYMAC_ADDR_L(wlc_hw), mac);
	mac = ((mac_addr->octet[3]) << 8) | mac_addr->octet[2];
	wlc_bmac_write_shm(wlc_hw, M_MYMAC_ADDR_M(wlc_hw), mac);
	mac = ((mac_addr->octet[5]) << 8) | mac_addr->octet[4];
	wlc_bmac_write_shm(wlc_hw, M_MYMAC_ADDR_H(wlc_hw), mac);
}

/**
 * This function attempts to drain A2DP buffers in BT before granting the antenna to Wl for
 * various calibrations, etc. This can only be done for ECI supported chips (including GCI) since
 * task and buffer count information is needed. It is also assumed that the mac is suspended when
 * this function is called. This function does the following:
 *   - Grant the antenna to BT (ANTSEL and TXCONF set to 0)
 *   - If the BT task type is A2DP and the buffer count is non-zero wait for up to 50 ms
 *     until the buffer count becomes zero.
 *   - If the task type is not A2DP or the buffer count is zero, exit the wait loop
 *   - If BT RF Active is asserted, wait for up to 5 ms for it to de-assert after setting
 *     TXCONF to 1 (don't grant to BT).
 * This functionality has been moved out of common PHY code since it is mac-related.
*/
#define BTCX_FLUSH_WAIT_MAX_MS  50
void
wlc_bmac_coex_flush_a2dp_buffers(wlc_hw_info_t *wlc_hw)
{
} /* wlc_bmac_coex_flush_a2dp_buffers */

#ifdef NOT_RIGGED_UP_YET
/* Macmode / Phymode / Opmode are used interchangebly some-
 * times even though they are one and the same. Going ahead
 * with HW signal name - using phymode here on (even though
 * its a misnomer).
 */
static int
wlc_bmac_switch_phymode(wlc_hw_info_t *wlc_hw, uint16 requested_phymode)
{
	uint32 phymode = 0x0;

	/* No need to do anything if requested and current phymode are same */
	if (requested_phymode == phy_get_phymode((phy_info_t *)wlc_hw->band->pi))
		return BCME_OK;
	else if ((requested_phymode != PHYMODE_RSDB) ||
		(requested_phymode != PHYMODE_RSDB_SISO_0) ||
		(requested_phymode != PHYMODE_RSDB_SISO_1) ||
		(requested_phymode != PHYMODE_MIMO) ||
		(requested_phymode != PHYMODE_80P80))
		return BCME_UNSUPPORTED;

	phymode = (uint32)requested_phymode << SICF_PHYMODE_SHIFT;

	/* put phy in reset */
	wlc_bmac_core_phy_clk(wlc_hw, OFF);

	/* doing phymode switch */
	si_core_cflags(wlc_hw->sih, SICF_PHYMODE, phymode);

	/* bring phy out of reset */
	wlc_bmac_core_phy_clk(wlc_hw, ON);

	phy_set_phymode((phy_info_t *)wlc_hw->band->pi, requested_phymode);

	return BCME_OK;
}
#endif /* WLRSDB */

void
wlc_bmac_exclusive_reg_access_core0(wlc_hw_info_t *wlc_hw, bool set)
{
	uint32 phymode = (si_core_cflags(wlc_hw->sih, 0, 0) & SICF_PHYMODE) >> SICF_PHYMODE_SHIFT;

	ASSERT(phy_get_phymode((phy_info_t *)wlc_hw->band->pi) == PHYMODE_MIMO);
	if (set)
		phymode |= SUPPORT_EXCLUSIVE_REG_ACCESS_CORE0;
	else
		phymode &= ~SUPPORT_EXCLUSIVE_REG_ACCESS_CORE0;

	si_core_cflags(wlc_hw->sih, SICF_PHYMODE, phymode << SICF_PHYMODE_SHIFT);
}
void
wlc_bmac_exclusive_reg_access_core1(wlc_hw_info_t *wlc_hw, bool set)
{
	uint32 phymode = (si_core_cflags(wlc_hw->sih, 0, 0) & SICF_PHYMODE) >> SICF_PHYMODE_SHIFT;

	ASSERT(phy_get_phymode((phy_info_t *)wlc_hw->band->pi) == PHYMODE_MIMO);
	if (set)
		phymode |= SUPPORT_EXCLUSIVE_REG_ACCESS_CORE1;
	else
		phymode &= ~SUPPORT_EXCLUSIVE_REG_ACCESS_CORE1;

	si_core_cflags(wlc_hw->sih, SICF_PHYMODE, phymode << SICF_PHYMODE_SHIFT);
}

#define  STARTBUSY_BIT_POLL_MAX_TIME 50
#define  INCREMENT_ADDRESS 4

#ifdef BCMPCIEDEV
void
wlc_bmac_enable_tx_hostmem_access(wlc_hw_info_t *wlc_hw, bool enabled)
{
	if (BCMPCIEDEV_ENAB()) {
		uint fifo_bitmap = BITMAP_SYNC_ALL_TX_FIFOS;
		wlc_info_t *wlc = wlc_hw->wlc;

		if (!wlc_hw->up) {
			return;
		}
		WL_INFORM(("wlc_bmac_enable_tx_hostmem_access \n"));
		if (!enabled)  {
			/* device power state changed in D3 device can't  */
			/* access host memory any more */
			/* 1. Stop sending data queued in the TCM to dma. */
			/* 2. Flush the FIFOs.  */
			/* 3. Drop all pending dma packets. */
			/* 4. Enable the DMA. */
			wlc_sync_txfifo(wlc, wlc->active_queue, fifo_bitmap, FLUSHFIFO);
		}
	}
}
#endif /* BCMPCIEDEV */

void
wlc_bmac_enable_rx_hostmem_access(wlc_hw_info_t *wlc_hw, bool enabled)
{
	wlc_info_t *wlc = wlc_hw->wlc;


	/* device power state changed in D3 device can;t access host memory any more */
	/* switch off the classification in the ucode and let the packet come to fifo1 only */
	if (BCMSPLITRX_ENAB()) {
		/* Handle software state change of FIFO0 first.
		* This will happen even when wl is down.
		*/
		dma_rxfill_suspend(wlc_hw->di[RX_FIFO], !enabled);

		/* For the rest, make sure hw is up */
		if (!wlc_hw->up) {
			return;
		}
		WL_ERROR(("enable %d: q0 frmcnt %d, wrdcnt %d, q1 frmcnt %d, wrdcnt %d\n",
		enabled, R_REG(wlc->osh, &wlc->regs->u_rcv.d11acregs.rcv_frm_cnt_q0),
		R_REG(wlc->osh, &wlc->regs->u_rcv.d11acregs.rcv_wrd_cnt_q0),
		R_REG(wlc->osh, &wlc->regs->u_rcv.d11acregs.rcv_frm_cnt_q1),
		R_REG(wlc->osh, &wlc->regs->u_rcv.d11acregs.rcv_wrd_cnt_q1)));

		if (enabled)  {
			wlc_mhf(wlc, MHF1, MHF1_RXFIFO1, 0, WLC_BAND_ALL);
			wlc_mhf(wlc, MHF3, MHF3_SELECT_RXF1, MHF3_SELECT_RXF1, WLC_BAND_ALL);
			/* Previous suspend might have made sure that there are
			* no pending buffers posted to Rx. So fill it up.
			*/
			dma_rxfill(wlc_hw->di[RX_FIFO]);
		} else {
			wlc_bmac_suspend_mac_and_wait(wlc_hw);
			if (R_REG(wlc->osh, &wlc->regs->u_rcv.d11acregs.rcv_frm_cnt_q0)) {
				uint32 pend_cnt;
				SPINWAIT((R_REG(wlc_hw->osh,
					&wlc_hw->regs->u_rcv.d11acregs.rcv_frm_cnt_q0) != 0),
					10000);
				pend_cnt = R_REG(wlc->osh,
					&wlc->regs->u_rcv.d11acregs.rcv_frm_cnt_q0);
				if (pend_cnt) {
					WL_ERROR(("TO pkts to be drained fifo0 %d, dma pend %d\n",
						pend_cnt, dma_rxactive(wlc_hw->di[RX_FIFO])));
				}
			}
			wlc_mhf(wlc, MHF1, MHF1_RXFIFO1, MHF1_RXFIFO1, WLC_BAND_ALL);
			wlc_bmac_enable_mac(wlc->hw);
		}
	}
} /* wlc_bmac_enable_rx_hostmem_access */

#if defined WLTXPWR_CACHE
void wlc_bmac_clear_band_pwr_offset(ppr_t *txpwr_offsets, wlc_hw_info_t *wlc_hw)
{
	if (NBANDS_HW(wlc_hw) > 1) {
		wlc_phy_clear_match_tx_offset(wlc_hw->bandstate[BAND_2G_INDEX]->pi, txpwr_offsets);
		wlc_phy_clear_match_tx_offset(wlc_hw->bandstate[BAND_5G_INDEX]->pi, txpwr_offsets);
	} else {
		wlc_phy_clear_match_tx_offset(wlc_hw->band->pi, txpwr_offsets);
	}
}
#endif

#ifndef WL_DUALMAC_RSDB
bool
wlc_bmac_rsdb_cap(wlc_hw_info_t *wlc_hw)
{
	if (WLC_DUALMAC_RSDB(wlc_hw->wlc->cmn)) {
		return FALSE;
	} else {
		bool hwcap = FALSE;

		ASSERT(wlc_hw != NULL);
		hwcap = (wlc_hw->num_mac_chains > 1) ? TRUE : FALSE;
		return hwcap;
	}
}
#endif /* WL_DUALMAC_RSDB */

void
wlc_bmac_init_core_reset_disable_fn(wlc_hw_info_t *wlc_hw)
{
	ASSERT(wlc_hw != NULL);

#ifdef WLRSDB
	if (wlc_bmac_rsdb_cap(wlc_hw)) {
		wlc_hw->mac_core_reset_fn = si_d11rsdb_core_reset;
		wlc_hw->mac_core_disable_fn = si_d11rsdb_core_disable;

	} else

#endif
	{
		wlc_hw->mac_core_reset_fn = si_core_reset;
		wlc_hw->mac_core_disable_fn = si_core_disable;
	}
}

void
wlc_bmac_core_reset(wlc_hw_info_t *wlc_hw, uint32 flags, uint32 resetflags)
{
	if (!wlc_hw || !wlc_hw->mac_core_reset_fn)
		return;

	if (D11REV_GE(wlc_hw->corerev, 64)) {
		int idx;

		/* New MAC uses sysmem as the buffers;
		 * need to make sure the sysmem core is up before use
		 */
		idx = si_coreidx(wlc_hw->sih);
		if (si_setcore(wlc_hw->sih, SYSMEM_CORE_ID, 0)) {
			if (!si_iscoreup(wlc_hw->sih))
				si_core_reset(wlc_hw->sih, 0, 0);
			si_setcoreidx(wlc_hw->sih, idx);
		}
	}

#ifdef WLRSDB
	if (!wlc_bmac_rsdb_cap(wlc_hw) || (wlc_rsdb_is_other_chain_idle(wlc_hw->wlc) == TRUE))
#endif
		(wlc_hw->mac_core_reset_fn)(wlc_hw->sih, flags, resetflags);

	/* Program the location in Sysmem RAM where the MAC Buffer Memory region begins. */
	if (D11REV_GE(wlc_hw->corerev, 64) && (BUSTYPE(wlc_hw->sih->bustype) == SI_BUS)) {
		/* Enable IHR for programming below regs. */
		wlc_bmac_mctrl(wlc_hw, ~0, MCTL_IHR_EN);
		W_REG(wlc_hw->osh, &wlc_hw->regs->u.d11acregs.SysMStartAddrHi,
			D11MAC_SYSM_STARTADDR_H);
		W_REG(wlc_hw->osh, &wlc_hw->regs->u.d11acregs.SysMStartAddrLo,
			D11MAC_SYSM_STARTADDR_L);
	}
} /* wlc_bmac_core_reset */

void
wlc_bmac_core_disable(wlc_hw_info_t *wlc_hw, uint32 bits)
{
	if (!wlc_hw || !wlc_hw->mac_core_disable_fn)
		return;
#ifdef WLRSDB
	if (!wlc_bmac_rsdb_cap(wlc_hw) || (wlc_rsdb_is_other_chain_idle(wlc_hw->wlc) == TRUE))
#endif
		(wlc_hw->mac_core_disable_fn)(wlc_hw->sih, bits);

}

/** This returns the context of last D11 core units using wlc_hw->macunit identity. */
bool
wlc_bmac_islast_core(wlc_hw_info_t *wlc_hw)
{
	ASSERT(wlc_hw != NULL);
	ASSERT(wlc_hw->sih != NULL);
	return (wlc_hw->macunit ==
		(si_numd11coreunits(wlc_hw->sih) - 1));
}

/**
 * FIFO- interrupt state machine is explained below
 * FIFO0-INT	FIFO1-INT	DECODE AS
 * 0		0		Idle state; no interrupt recieved on this pkt
 * 0		1		fifo-1 interrupt recieved; waiting for fifo-0 int
 * 1		0		fifo-0 interrupt recieved; waiting for fifo-1 int
 */
#if defined(BCMPCIEDEV)
static int
wlc_bmac_process_split_fifo_pkt(wlc_hw_info_t *wlc_hw, uint fifo, void* p)
{
/*
	uint16 fifo1len = 0;
*/
	uint16 convstatus = 0;
	wlc_info_t *wlc = wlc_hw->wlc;
	wlc_tunables_t *tune = wlc->pub->tunables;
	bool hdr_converted = FALSE;

	WL_TRACE(("wl%d: %s BMAC rev pkt on fifo %d \n\n", wlc_hw->unit, __FUNCTION__, fifo));

	if (fifo == RX_FIFO) {
		if (PKTISFIFO0INT(wlc_hw->osh, p)) {
			/* FIFO-0 cant be set while processing fifo-0 int */
			WL_ERROR(("Error:FIFO-0 allready set for pkt %p  \n", p));
		} else {
			/* fifo-0 is the first int */
			PKTSETFIFO0INT(wlc_hw->osh, p);
		}
	}
	if (fifo == RX_FIFO1) {
		if (PKTISFIFO1INT(wlc_hw->osh, p)) {
			/* fifo-1 int cant be set during F-1 processing */
			WL_ERROR(("Error: fifo-1 allready set for %p \n", p));
		} else {
			/* Set F1 int */
			PKTSETFIFO1INT(wlc_hw->osh, p);
		}

	}

	if (!(PKTISFIFO0INT(wlc_hw->osh, p) && PKTISFIFO1INT(wlc_hw->osh, p))) {
		/* both fifos not set, return */
		return 0;
	} else {
		/* Recieved both interrupts, Proceed with rx processing */
		/* retrieve fifo-0 len */
		uint16 fifo0len = 0;
		wlc_d11rxhdr_t * wlc_rxhdr = NULL;
		wlc_rxhdr = (wlc_d11rxhdr_t *)PKTDATA(wlc_hw->osh, p);

		/* reset interrupt bits */
		PKTRESETFIFO0INT(wlc_hw->osh, p);
		PKTRESETFIFO1INT(wlc_hw->osh, p);

		/* length & conv status */
		fifo0len = ltoh16(wlc_rxhdr->rxhdr.RxFameSize_0);

		/*
		fifo1len = ltoh16(wlc_rxhdr->rxhdr.RxFrameSize);
		*/
		convstatus = ltoh16(wlc_rxhdr->rxhdr.HdrConvSt);
		hdr_converted = ((convstatus & HDRCONV_ENAB) ? TRUE : FALSE);

		if (!hdr_converted) {
			if (fifo0len <= ((uint16)(tune->copycount * 4))) {
				PKTSETFRAGUSEDLEN(wlc_hw->osh, p, 0);
			} else {
				PKTSETFRAGUSEDLEN(wlc_hw->osh, p,
					(fifo0len - (tune->copycount * 4)));
			}
		} else {
			PKTSETFRAGUSEDLEN(wlc_hw->osh, p, fifo0len);
			PKTSETHDRCONVTD(wlc_hw->osh, p);
		}
		return 1;
	}
} /* wlc_bmac_process_split_fifo_pkt */
#endif /* BCMPCIEDEV */

/** Check if given rx fifo is valid */
static uint8
wlc_bmac_rxfifo_enab(uint fifo)
{
	switch (fifo) {
		case RX_FIFO :
			return 1;
			break;
		case RX_FIFO1:
#ifdef FORCE_RX_FIFO1
			/* JIRA CRWLDOT11M-1329 */
			/* in 4349a0, fifo-2 classification will work only if fifo-1 is enabled */
			return 1;
#endif /* FORCE_RX_FIFO1 */
			return ((uint8)(PKT_CLASSIFY_EN(RX_FIFO1) || RXFIFO_SPLIT()));
			break;
		case RX_FIFO2 :
			return ((uint8)(PKT_CLASSIFY_EN(RX_FIFO2)));
			break;
		default :
			WL_ERROR(("wl%s: Unsupported FIFO %d\n", __FUNCTION__, fifo));
			return 0;
	}
}

#ifdef BCMDBG_TXSTUCK
void wlc_bmac_print_muted(wlc_info_t *wlc, struct bcmstrbuf *b)
{
	if (!wlc->clk) {
		bcm_bprintf(b, "Need clk to dump bmac tx stuck information\n");
		return;
	}

	bcm_bprintf(b, "tx suspended: %d %d, muted %d %d, mac_enab %d\n",
		wlc_tx_suspended(wlc),
		wlc->tx_suspended,
		wlc_phy_ismuted(wlc->hw->band->pi),
		wlc->hw->mute_override,
		(R_REG(wlc->hw->osh, &wlc->hw->regs->maccontrol) & MCTL_EN_MAC));

	bcm_bprintf(b, "post=%d rxactive=%d txactive=%d txpend=%d\n",
		NRXBUFPOST,
		dma_rxactive(wlc->hw->di[RX_FIFO]),
		dma_txactive(wlc->hw->di[1]),
		dma_txpending(wlc->hw->di[1]));

	bcm_bprintf(b, "pktpool callback disabled: %d\n\n",
		pktpool_emptycb_disabled(wlc->hw->wlc->pub->pktpool));
}
#endif /* BCMDBG_TXSTUCK */

/* Check if programmed SICF_BW bits match with the current CHANSPEC BW */
int
wlc_bmac_bw_check(wlc_hw_info_t *wlc_hw)
{
	if ((si_core_cflags(wlc_hw->sih, 0, 0) & SICF_BWMASK) == wlc_bmac_clk_bwbits(wlc_hw))
		return BCME_OK;

	return BCME_BADCHAN;
}

/* get slice specific OTP/SROM parameters */
int
getintvar_slicespecific(wlc_hw_info_t *wlc_hw, char *vars, const char *name)
{
	int ret = 0;
	char *name_with_prefix = NULL;
	/* if accessor is initalized with slice/<slice_index>string */
	if (wlc_hw->vars_table_accessor[0] !=  0) {
		uint16 sz = (strlen(name) + strlen(wlc_hw->vars_table_accessor)+1);
		name_with_prefix = (char *) MALLOC(wlc_hw->osh, sz);
		/* Prepare fab name */
		if (name_with_prefix == NULL) {
			WL_ERROR(("wl: %s: MALLOC failure\n", __FUNCTION__));
			return 0;
		}
		/* prefix accessor to the vars-name */
		name_with_prefix[0] = 0;
		bcmstrcat(name_with_prefix, wlc_hw->vars_table_accessor);
		bcmstrcat(name_with_prefix, name);
		ret = getintvar(vars, name_with_prefix);
		MFREE(wlc_hw->osh, name_with_prefix, sz);
	}
	else {
		ret = getintvar(vars, name);
	}
	return ret;
}

int
wlc_bmac_reset_txrx(wlc_hw_info_t *wlc_hw)
{
	int err = BCME_OK;
	wlc_info_t *wlc;

	ASSERT(wlc_hw != NULL);

	wlc = wlc_hw->wlc;
	wlc_sync_txfifo(wlc, wlc->active_queue, BITMAP_SYNC_ALL_TX_FIFOS, SYNCFIFO);
	return err;
}

int
wlc_bmac_bmc_dyn_reinit(wlc_hw_info_t *wlc_hw, uint8 bufsize_in_256_blocks)
{
	osl_t *osh;
	d11regs_t *regs;
	uint32 bufsize = D11MAC_BMC_BUFSIZE_512BLOCK;
	uint32 fifo_sz, txfifo_sz, bufsize_tx, bufsize_pertxtid, bufsizeptxid_percore, rxfifo_sz;
	uint32 bmc_startaddr = D11MAC_BMC_STARTADDR;
	uint32 doublebufsize = 0;
	wlc_info_t *wlc = wlc_hw->wlc;
	int num_of_fifo, rxmapfifosz;
	int bmc_tx_fifo_list[5] = {0, 1, 2, 3, 4};
	int bmc_rx_fifo_list[D11AC_MAX_RX_FIFO_NUM] = {6, 8};
	int i, fifo;
	uint16 minbufs;
	uint16 maxbufs;


	osh = wlc_hw->osh;
	regs = wlc_hw->regs;

	WL_TRACE(("wl%d: %s\n", wlc_hw->unit, __FUNCTION__));

	/* Check the input parameters */
	ASSERT(wlc_hw->corerev >= 50);

	ASSERT(bmc_params != NULL);

	/* bufsize is used everywhere - calculate number of buffers, programming bmcctl etc. */
	bufsize = bufsize_in_256_blocks;

	/* Derive from machwcap registers */
	fifo_sz = ((R_REG(osh, &regs->machwcap) & MCAP_TXFSZ_MASK) >> MCAP_TXFSZ_SHIFT) * 2048;

	/* Account for bmc_startaddr which is specified in units of 256B */
	bmc_maxbufs = (fifo_sz - (bmc_startaddr << 8)) >> (8 + bufsize);

	/* Calc:   448k(total) - (2* tplpercore + SR) - (rxq0 + rxq1) - 10(extra)= X
	 * each rx fifo requires 3 additional fifos which makes to 6 with additional
	 * margin as 10 buffers.
	 * X/512=Y buffers
	 * per txtid ie Y/6
	 */
	rxfifo_sz = (bmc_params->minbufs[6] << (8 + bufsize)) +
	(bmc_params->minbufs[8] << (8 + bufsize));
	txfifo_sz = fifo_sz - D11MAC_BMC_TPL_BYTES_PERCORE - D11MAC_BMC_SR_BYTES - rxfifo_sz - 10;
	bufsize_tx = txfifo_sz / (8 + bufsize);
	bufsize_pertxtid = bufsize_tx / (D11AC_MAX_FIFO_NUM - D11AC_MAX_RX_FIFO_NUM - 1);

	bufsizeptxid_percore = bufsize_pertxtid / (wlc->cmn->num_d11_cores);
	/* JIRA:SW4349-217:JIRA:CRWLDOT11M-1290:Fixing TxAllMaxBuffers values to 0x800 */
	bufsizeptxid_percore = 0x800;

	if (bufsize ==  D11MAC_BMC_BUFSIZE_256BLOCK)
	{
		doublebufsize = 1;
	}

	/*
	Things to be changed for MIMO
		1.	MinBuffers for the 6 chain 0 TIDs double in size (for 2x2 throughput)
		2.	MinBuffers for the 6 chain 1 TIDs reduce to 0
		3.	TXAllMaxBuffers for chain 0 increased
			to match BM buffer size to eliminate this limit
		4.	RXFIFO size for the two chain 0
			RX channels need to be doubled (for 2x2 throughput)
		5.	Reduce RXFIFO size for the two chain 1 RX channels to minimum allowed (TBD)

		Change BMC to MIMO mode
	*/
	if ((wlc_bmac_rsdb_cap(wlc_hw)) &&
		(wlc_rsdb_mode(wlc_hw->wlc) ==  PHYMODE_MIMO)) {
		WL_TRACE(("wl%d: In dynamic re init:TO MIMO %s\n", wlc_hw->unit, __FUNCTION__));
		/* Decrease core1 rxfifo sz */
		num_of_fifo = D11AC_MAX_RX_FIFO_NUM;


		for (i = 0; i < num_of_fifo; i++) {
			fifo =  bmc_rx_fifo_list[i];
			W_REG(osh, &regs->u.d11acregs.RXMapFifoSize, 0x1);

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxffszlden_SHIFT) |
			(fifo << 1)) | (BMCCmd_Core1_Sel_MASK));
			if (fifo == 6) {
				/* Delay may require to be 1sec */
				SPINWAIT(!(R_REG(wlc_hw->osh,
				&regs->u.d11acregs.RXMapStatus) & 0x1), 1000);
				if (R_REG(wlc_hw->osh,
					&regs->u.d11acregs.RXMapStatus) & 0x1) {
						WL_INFORM(("wl%d: Success:"
						"Core 1 rx fifo zero decreased\n",
						wlc_hw->unit));
				} else {
					WL_ERROR(("wl%d:Error:"
					"Waiting for core 1 rx fifo zero decrease\n",
					wlc_hw->unit));
				}
			}

			if (fifo == 8) {
				/* Delay may require to be 1sec */
				SPINWAIT(!(R_REG(wlc_hw->osh,
				&regs->u.d11acregs.RXMapStatus) & 0x2), 1000);
				if (R_REG(wlc_hw->osh, &regs->u.d11acregs.RXMapStatus) & 0x2) {
					WL_INFORM(("wl%d: Success:"
						"Core 1 rx fifo one decreased\n", wlc_hw->unit));
				} else {
					WL_ERROR(("wl%d: Error:"
						"Waiting for core 1 rx fifo one decrease\n",
						wlc_hw->unit));
				}
			}

			/* 3 buffers more than rxmapfifo size */
			W_REG(osh, &regs->u.d11acregs.BMCMaxBuffers, 0x800);
			W_REG(osh, &regs->u.d11acregs.BMCMinBuffers, 0x4);
			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) |
			(1 << BMCCmd1_Minmaxlden_SHIFT) | (fifo << 1)) | (BMCCmd_Core1_Sel_MASK));


			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			(R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) |
			(1 << BMCCmd1_Minmaxappall_SHIFT));

			/* Read MaxMinMet to confirm the decreased buffer space.
			 * Delay may require to be 1sec
			 */
			SPINWAIT(!(R_REG(wlc_hw->osh,
			&regs->u.d11acregs.BMCDynAllocStatus1) & 0x1ff), 1000);
			if (R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCDynAllocStatus1) & 0x1ff) {
				WL_INFORM(("wl%d:Success:"
					"Core 1 tx fifo decreased\n", wlc_hw->unit));
			} else {
				WL_ERROR(("wl%d:Error:"
					"Polling for core 1 tx fifo decrease\n", wlc_hw->unit));
			}
		}

		WL_TRACE(("wl%d:To:MIMO:RxCore1 reduced.\n", wlc_hw->unit));
		/* Increase core 0 max buf to max value */
		for (i = 0; i < num_of_fifo; i++) {
			fifo =  bmc_rx_fifo_list[i];
			maxbufs = (bmc_params->minbufs[fifo] << doublebufsize)  + 3;
			minbufs = (bmc_params->minbufs[fifo] << doublebufsize) + 3;
			/* overide to sync with TCL changes */
			maxbufs = 0x800;

			rxmapfifosz = (bmc_params->minbufs[fifo] << doublebufsize);


			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			(R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) | (fifo << 1));
			W_REG(osh, &regs->u.d11acregs.BMCMaxBuffers, maxbufs);

			W_REG(osh, &regs->u.d11acregs.BMCMinBuffers, minbufs);


			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxlden_SHIFT) | (fifo << 1)) & (~BMCCmd_Core1_Sel_MASK));


			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			(R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) |
			(1 << BMCCmd1_Minmaxappall_SHIFT));

			/* Read MaxMinMet to confirm the decreased buffer space.
			 * Delay may require to be 1sec
			 */
			SPINWAIT((!(R_REG(wlc_hw->osh,
			&regs->u.d11acregs.BMCDynAllocStatus) & 0x1ff)), 1000);
			if (R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCDynAllocStatus1) & 0x1ff) {
				WL_INFORM(("wl%d:Success:"
				"Polling for core 0 rx fifo increase\n", wlc_hw->unit));
			} else {
				WL_ERROR(("wl%d:Error:"
				"Polling for core 0 rx fifo increase\n", wlc_hw->unit));
			}

			W_REG(osh, &regs->u.d11acregs.RXMapFifoSize, rxmapfifosz);

#ifdef WAR_HW_RXFIFO_0
			wlc_war_rxfifo_shm(wlc_hw, fifo, rxmapfifosz);
#endif

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) |
			(1 << BMCCmd1_Minmaxffszlden_SHIFT) |
			(fifo << 1)) & (~BMCCmd_Core1_Sel_MASK));
		}

		WL_TRACE(("wl%d:To:MIMO:RxCore0 increased.\n", wlc_hw->unit));

		/* TX buffers:decrease core 1 min/max buf to 0:Change values: */
		num_of_fifo = 5;

		for (i = 0; i < num_of_fifo; i++) {
			fifo =  bmc_tx_fifo_list[i];

			W_REG(osh, &regs->u.d11acregs.BMCMinBuffers, 0x0);
			W_REG(osh, &regs->u.d11acregs.BMCMaxBuffers, 0x800);


			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxlden_SHIFT) | (fifo << 1)) | (BMCCmd_Core1_Sel_MASK));


			W_REG(osh, &regs->u.d11acregs.BMCCmd, (R_REG(wlc_hw->osh,
			&regs->u.d11acregs.BMCCmd)) | fifo << 0);
		}

		/* increase core 0 min/max buf to max val */
		for (i = 0; i < num_of_fifo; i++) {
			fifo =  bmc_tx_fifo_list[i];
			minbufs = (bmc_params->minbufs[fifo] << doublebufsize);

			W_REG(osh, &regs->u.d11acregs.BMCMinBuffers, minbufs);

			W_REG(osh, &regs->u.d11acregs.BMCMaxBuffers, 0x800);

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxlden_SHIFT) | (fifo << 1)) & (~BMCCmd_Core1_Sel_MASK));

			W_REG(osh, &regs->u.d11acregs.BMCCmd, (R_REG(wlc_hw->osh,
			&regs->u.d11acregs.BMCCmd) | fifo << 0));
		}

		/* 3 buffers per tid *6 buffers for core 1 */
		/* calc buffers per tid *6 buffers for core 0 */

		W_REG(osh, &regs->u.d11acregs.BMCCore1TXAllMaxBuffers, 0x800);

		W_REG(osh, &regs->u.d11acregs.BMCCore0TXAllMaxBuffers, 0x800);

		W_REG(osh, &regs->u.d11acregs.BMCCmd1,
		(R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) |
		(1 << BMCCmd1_Minmaxappall_SHIFT));

		/* Read MaxMinMet to confirm the decreased buffer space. */
		SPINWAIT((!((R_REG(wlc_hw->osh,
		&regs->u.d11acregs.BMCDynAllocStatus) & 0xdff) == 0xdff)) ||
		(!((R_REG(wlc_hw->osh,
		&regs->u.d11acregs.BMCDynAllocStatus1) & 0x1ff) == 0x1ff)), 1000);
		if (((R_REG(wlc_hw->osh,
			&regs->u.d11acregs.BMCDynAllocStatus) & 0xdff) == 0xdff) ||
			((R_REG(wlc_hw->osh,
			&regs->u.d11acregs.BMCDynAllocStatus1) & 0x1ff) == 0x1ff)) {

			WL_INFORM(("wl%d:Sucess:polling Dyn all sts:"
			"$reg(TXE_BMCDynAllocStatus) $reg(TXE_BMCDynAllocStatus1)\n",
			wlc_hw->unit));
		} else {
			WL_ERROR(("wl%d:Error:polling Dyn all sts:"
				"$reg(TXE_BMCDynAllocStatus) $reg(TXE_BMCDynAllocStatus1)\n",
				wlc_hw->unit));
		}

		WL_TRACE(("wl%d:ToMIMO: Tx core 0 increased\n", wlc_hw->unit));
		WL_TRACE(("wl%d:To MIMO:Tx Core1 reduced\n", wlc_hw->unit));
	}

	/* MIMO to RSDB mode: */
	if ((wlc_bmac_rsdb_cap(wlc_hw)) &&
		(!si_coreunit(wlc_hw->sih) && (wlc_rsdb_mode(wlc_hw->wlc) == PHYMODE_RSDB))) {
		WL_TRACE(("wl%d:In dynamic reinit:TO RSDB\n", wlc_hw->unit));

		/* MinBuffers for the 6 chain 0 tids to reduce to half of that in MIMO.
		 * Decrease core 0 rx fifo size
		 */
		num_of_fifo = D11AC_MAX_RX_FIFO_NUM;
		for (i = 0; i < num_of_fifo; i++) {
			fifo =  bmc_rx_fifo_list[i];
			rxmapfifosz = (bmc_params->minbufs[fifo] << doublebufsize);
			if (fifo == 6) {
				/* reduce to half */
				rxmapfifosz = rxmapfifosz >> 1;
			}

			W_REG(osh, &regs->u.d11acregs.RXMapFifoSize, rxmapfifosz);
#ifdef WAR_HW_RXFIFO_0
			wlc_war_rxfifo_shm(wlc_hw, fifo, rxmapfifosz);
#endif

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxffszlden_SHIFT) |
			(fifo << 1)) & (~BMCCmd_Core1_Sel_MASK));
			if (fifo == 6) {
				/* Delay may require to be 1sec */
				SPINWAIT(!(R_REG(wlc_hw->osh,
				&regs->u.d11acregs.RXMapStatus) & 0x1), 1000);
				if (R_REG(wlc_hw->osh, &regs->u.d11acregs.RXMapStatus) & 0x1) {
						WL_INFORM(("wl%d: Success:"
						"Waiting for core 1 rx fifo zero decrease\n",
						wlc_hw->unit));
				} else {
						WL_ERROR(("wl%d: Error:"
						"Waiting for core 1 rx fifo zero decrease\n",
						wlc_hw->unit));
				}
			}

			if (fifo == 8) {
				/* Delay may require to be 1sec */
				SPINWAIT(!(R_REG(wlc_hw->osh,
				&regs->u.d11acregs.RXMapStatus) & 0x2), 1000);
				if (R_REG(wlc_hw->osh, &regs->u.d11acregs.RXMapStatus) & 0x2) {
						WL_INFORM(("wl%d: Success:"
						"Waiting for core 1 rx fifo one decrease\n",
						wlc_hw->unit));
				} else {
						WL_ERROR(("wl%d: Error:"
						"Waiting for core 1 rx fifo one decrease\n",
						wlc_hw->unit));
				}
			}

			maxbufs = (bmc_params->minbufs[fifo] << doublebufsize);

			if (fifo == 6) {
				/* reduce to half */
				maxbufs = maxbufs >> 1;
			}

			maxbufs = maxbufs + 3;
			minbufs = maxbufs;

			/* overide to sync with TCL changes */
			maxbufs = 0x800;
			W_REG(osh, &regs->u.d11acregs.BMCMaxBuffers, maxbufs);
			W_REG(osh, &regs->u.d11acregs.BMCMinBuffers, minbufs);

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxlden_SHIFT) | (fifo << 1)) & (~BMCCmd_Core1_Sel_MASK));

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			(R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) |
			(1 << BMCCmd1_Minmaxappall_SHIFT));

			SPINWAIT((!(R_REG(wlc_hw->osh,
			&regs->u.d11acregs.BMCDynAllocStatus) & 0x1ff)), 1000);
			if (R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCDynAllocStatus1) & 0x1ff) {
				WL_INFORM(("wl%d:Success:"
				"Polling for core 0 rx fifo increase\n", wlc_hw->unit));
			} else {
				WL_ERROR(("wl%d:Error:"
				"Polling for core 0 rx fifo increase\n", wlc_hw->unit));
			}
		}
		WL_TRACE(("wl%d:To RSDB:Core0 Rx fifo size reduced.\n", wlc_hw->unit));
		/* Increase core 1 rxfifo size */
		for (i = 0; i < num_of_fifo; i++) {
			fifo =  bmc_rx_fifo_list[i];

			maxbufs = (bmc_params->minbufs[fifo] << doublebufsize);

			if (fifo == 6) {
				/* reduce to half */
				maxbufs = maxbufs >> 1;
			}

			maxbufs = maxbufs + 3;
			minbufs = maxbufs;

			/* overide to sync with TCL changes */
			maxbufs = 0x800;
			rxmapfifosz = minbufs - 3;


			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			(R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) | (fifo << 1));

			W_REG(osh, &regs->u.d11acregs.BMCMaxBuffers, maxbufs);

			W_REG(osh, &regs->u.d11acregs.BMCMinBuffers, minbufs);


			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxlden_SHIFT) | (fifo << 1)) | (BMCCmd_Core1_Sel_MASK));

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			(R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) |
			(1 << BMCCmd1_Minmaxappall_SHIFT));

			/* Read MaxMinMet to confirm the applied buffer space.
			 * Delay may require to be 1sec
			 */
			SPINWAIT((!(R_REG(wlc_hw->osh,
			&regs->u.d11acregs.BMCDynAllocStatus) & 0x1ff)), 1000);
			if (R_REG(wlc_hw->osh,
			&regs->u.d11acregs.BMCDynAllocStatus1) & 0x1ff) {
				WL_INFORM(("wl%d:Success:"
				"Polling for core 0 rx fifo increase\n", wlc_hw->unit));
			} else {
				WL_ERROR(("wl%d:Error:"
				"Polling for core 0 rx fifo increase\n", wlc_hw->unit));
			}


			W_REG(osh, &regs->u.d11acregs.RXMapFifoSize, rxmapfifosz);

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxffszlden_SHIFT) | (fifo << 1)) |
			(BMCCmd_Core1_Sel_MASK));
		}

		WL_TRACE(("wl%d:To RSDB:Core1 Rx size increased.\n", wlc_hw->unit));
		/* Core 1 done */

		/* To RSDB:Tx buffers
		 * Tx buffers: Decrease core 0 number of buffers
		 * and increase core 1 number of buffers
		 * Decrease core 0 buffers to half its original value ,ie 0x73/2 = 0x39
		 */
		num_of_fifo = 5;

		for (i = 0; i < num_of_fifo; i++) {
			fifo =  bmc_tx_fifo_list[i];
			minbufs = (bmc_params->minbufs[fifo] << doublebufsize);

			W_REG(osh, &regs->u.d11acregs.BMCMinBuffers, minbufs);

			W_REG(osh, &regs->u.d11acregs.BMCMaxBuffers, bufsizeptxid_percore);

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxlden_SHIFT) | (fifo << 1)) & (~BMCCmd_Core1_Sel_MASK));
		}
		/* Core 1 buffers */
		for (i = 0; i < num_of_fifo; i++) {
			fifo =  bmc_tx_fifo_list[i];
			/* increase core 1 min/max buf to max val */
			minbufs = (bmc_params->minbufs[fifo] << doublebufsize);

			W_REG(osh, &regs->u.d11acregs.BMCMinBuffers, minbufs);

			W_REG(osh, &regs->u.d11acregs.BMCMaxBuffers, bufsizeptxid_percore);

			W_REG(osh, &regs->u.d11acregs.BMCCmd1,
			((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1) & 0x0) |
			(1 << BMCCmd1_Minmaxlden_SHIFT) | (fifo << 1)) | (BMCCmd_Core1_Sel_MASK));
		}


		W_REG(osh, &regs->u.d11acregs.BMCCore1TXAllMaxBuffers, 0x800);

		W_REG(osh, &regs->u.d11acregs.BMCCore0TXAllMaxBuffers, 0x800);

		W_REG(osh, &regs->u.d11acregs.BMCCmd1,
		(R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCCmd1)) |
		(1 << BMCCmd1_Minmaxappall_SHIFT));

		/* Read MaxMinMet to confirm the decreased buffer space. */
		SPINWAIT((!((R_REG(wlc_hw->osh,
		&regs->u.d11acregs.BMCDynAllocStatus) & 0xdff) == 0xdff)) ||
		(!((R_REG(wlc_hw->osh,
		&regs->u.d11acregs.BMCDynAllocStatus1) & 0x1ff) == 0x1ff)), 1000);
		if (((R_REG(wlc_hw->osh,
		&regs->u.d11acregs.BMCDynAllocStatus) & 0xdff) == 0xdff) ||
		((R_REG(wlc_hw->osh, &regs->u.d11acregs.BMCDynAllocStatus1) & 0x1ff) == 0x1ff)) {
			WL_INFORM(("wl%d:Success:polling Dyn all sts:"
			"$reg(TXE_BMCDynAllocStatus)"
			"$reg(TXE_BMCDynAllocStatus1) \n", wlc_hw->unit));
		} else {
			WL_ERROR(("wl%d:Error:polling Dyn all sts:"
			"$reg(TXE_BMCDynAllocStatus)"
			"$reg(TXE_BMCDynAllocStatus1) \n", wlc_hw->unit));
		}
		WL_TRACE(("wl%d:To RSDB: Tx core 0 increased\n", wlc_hw->unit));
		WL_TRACE(("wl%d:To RSDB:Tx Core1 reduced\n", wlc_hw->unit));
	}

	return 0;
}

/* update ucode to operate core/mode specific PM operation */
void
wlc_bmac_rsdb_mode_param_to_shmem(wlc_hw_info_t *wlc_hw)
{
	uint16 phymode;

	if (wlc_hw->macunit == 0) {

		if ((wlc_rsdb_mode(wlc_hw->wlc) ==  PHYMODE_MIMO)) {
			phymode = CORE0_MODE_MIMO;
		} else if ((wlc_rsdb_mode(wlc_hw->wlc) ==  PHYMODE_80P80)) {
			phymode = CORE0_MODE_80P80;
		} else {
			phymode = CORE0_MODE_RSDB;
		}

	} else {
		phymode = CORE1_MODE_RSDB;
	}

	wlc_bmac_write_shm(wlc_hw, M_MODE_CORE(wlc_hw), phymode);
	wlc_hw->shmphymode = phymode;
}

uint16
wlc_bmac_read_eci_data_reg(wlc_hw_info_t *wlc_hw, uint8 reg_num)
{
	W_REG(wlc_hw->osh, &wlc_hw->regs->u.d11regs.btcx_eci_addr, reg_num);
	return (R_REG(wlc_hw->osh, &wlc_hw->regs->u.d11regs.btcx_eci_data));
}

/* Disables D11 DMA of tx_fifo. Don't call dma_txreset for D11 DMA.
 * Call this API wlc_bmac_txfifo_reset instead.
 */
bool
wlc_bmac_txfifo_reset(wlc_hw_info_t *wlc_hw, uint tx_fifo)
{
	bool ret = TRUE;

	/* Suspend the DMA first */
	wlc_upd_suspended_fifos_set(wlc_hw, tx_fifo);
	dma_txsuspend(wlc_hw->di[tx_fifo]);

	/* Poll for chanstatus to confirm that D11 DMA is suspended. */
	if ((ret = wlc_bmac_istxsuspend(wlc_hw, tx_fifo)) == FALSE) {
		wlc_fatal_error(wlc_hw->wlc);
		return ret;
	}
	/* Disable/reset the DMA */
	return dma_txdisable(wlc_hw->di[tx_fifo]);
}

/* check for chanstatus for dma suspension complete */
static bool
wlc_bmac_istxsuspend(wlc_hw_info_t *wlc_hw, uint tx_fifo)
{
	osl_t *osh = wlc_hw->osh;
	d11regs_t *regs = wlc_hw->regs;
	uint count;
	uint chnstatus;

	count = 0;
	BCM_REFERENCE(tx_fifo);
	while (count < (80 * 1000)) {
		chnstatus = R_REG(osh, &regs->chnstatus);
		if (chnstatus == 0)
			break;
		OSL_DELAY(4);
		count += 4;
	}
	/* When a xmtCtrl.suspend is done when the BM is full,
	 * the TxDMA  engine will move into SuspendPending State (xmtStatus0[31:28] = 0x4)
	 * and will not return to Idle, until the BM is freed of some space.
	 * The space in BM will be freed only after MAC is resumed.
	 * However the ucode finishes the suspend sequence
	 * and the CHNSTATUS.SuspPend bit will be cleared. So checking for dmastatus
	 * is not required. Check for dma status is required only if we doing
	 * BM flush.
	 */
	if ((chnstatus)) {
		WL_ERROR(("MQ ERROR %s:"
		          "not done after %d us: chnstatus 0x%04x "
		          " txefs 0x%04x\n"
		          "BMCReadStatus: 0x%04x AQMFifoReady: 0x%04x\n Fifo:%x\n",
		          __FUNCTION__, count, chnstatus,
		          R_REG(osh, &regs->u.d11acregs.XmtSuspFlush),
		          R_REG(osh, &regs->u.d11acregs.BMCReadStatus),
		          R_REG(osh, &regs->u.d11acregs.u0.lt64.AQMFifoReady), tx_fifo));

		return FALSE;
	}
	return TRUE;
}

/* dump bmac and PHY registered names */
int
wlc_bmac_dump_list(wlc_hw_info_t *wlc_hw, struct bcmstrbuf *b)
{
	int ret;
	ret = wlc_dump_reg_list(wlc_hw->dump, b);
	if (ret != BCME_OK)
		return ret;
	ret = phy_dbg_dump_list((phy_info_t *)wlc_hw->band->pi, b);
	return ret;
}
#ifdef WAR_HW_RXFIFO_0
static void wlc_war_rxfifo_shm(wlc_hw_info_t *wlc_hw, uint fifo, uint fifo_size)
{
	uint fifo_0_shm_offset = 0;
	uint16 shm_val = 0;
	if (fifo == 6) {
		fifo_0_shm_offset = RXFIFO_0_OFFSET;
		shm_val = (fifo_size * BLOCK_SIZE)>>2;
		wlc_bmac_write_shm(wlc_hw, fifo_0_shm_offset, shm_val);
	} else if (fifo == 8) {
		fifo_0_shm_offset = RXFIFO_1_OFFSET;
		shm_val = ((fifo_size * BLOCK_SIZE)>>2) - 0x14;
		wlc_bmac_write_shm(wlc_hw, fifo_0_shm_offset, shm_val);
	}
}
#endif /* WAR_HW_RXFIFO_0 */

unsigned int
wlc_bmac_shmphymode_dump(wlc_hw_info_t *wlc_hw)
{
	return wlc_hw->shmphymode;

}

uint
wlc_bmac_coreunit(wlc_info_t *wlc)
{
#if defined(WL_AUXCORE_TMP_NIC_SUPPORT)
	extern int auxcore;
	return (auxcore == 0) ? DUALMAC_MAIN : DUALMAC_AUX;
#else
	return (wlc->hw->macunit == 0 ? DUALMAC_MAIN : DUALMAC_AUX);
#endif
}