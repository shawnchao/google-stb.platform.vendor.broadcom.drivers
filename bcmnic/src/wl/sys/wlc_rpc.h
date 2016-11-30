/*
 * WLC RPC common header file
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
 * $Id: wlc_rpc.h 613096 2016-01-16 03:43:49Z $
 */

#ifndef _WLC_RPC_H_
#define _WLC_RPC_H_
#define RPC_ID_LEN 64

#include <wlc_types.h>
#include <wlioctl.h>
#include <wl_dbg.h>

/* RPC IDs, reordering is OK. This needs to be in sync with RPC_ID_TABLE below */
typedef enum {
	WLRPC_NULL_ID = 0,
	WLRPC_WLC_REG_READ_ID,
	WLRPC_WLC_REG_WRITE_ID,
	WLRPC_WLC_MHF_SET_ID,
	WLRPC_WLC_MHF_GET_ID,
	WLRPC_WLC_BMAC_UP_PREP_ID,
	WLRPC_WLC_BMAC_UP_FINISH_ID,
	WLRPC_WLC_BMAC_SET_CTRL_EPA_ID,
	WLRPC_WLC_BMAC_SET_CTRL_SROM_ID,
	WLRPC_WLC_BMAC_DOWN_PREP_ID,
	WLRPC_WLC_BMAC_DOWN_FINISH_ID,
	WLRPC_WLC_BMAC_WRITE_HW_BCNTEMPLATES_ID,
	WLRPC_WLC_BMAC_RESET_ID,
	WLRPC_WLC_DNGL_REBOOT_ID,
	WLRPC_WLC_BMAC_RPC_TXQ_WM_SET_ID,
	WLRPC_WLC_BMAC_RPC_TXQ_WM_GET_ID,
	WLRPC_WLC_BMAC_RPC_AGG_SET_ID,
	WLRPC_WLC_BMAC_RPC_MSGLEVEL_SET_ID,
	WLRPC_WLC_BMAC_RPC_AGG_LIMIT_SET_ID,
	WLRPC_WLC_BMAC_RPC_AGG_LIMIT_GET_ID,
	WLRPC_WLC_BMAC_INIT_ID,
	WLRPC_WLC_BMAC_SET_CWMIN_ID,
	WLRPC_WLC_BMAC_MUTE_ID,
	WLRPC_WLC_PHY_HOLD_UPD_ID,
	WLRPC_WLC_PHY_MUTE_UPD_ID,
	WLRPC_WLC_PHY_CLEAR_TSSI_ID,
	WLRPC_WLC_PHY_ANTDIV_GET_RX_ID,
	WLRPC_WLC_PHY_ANTDIV_SET_RX_ID,
	WLRPC_WLC_PHY_PREAMBLE_SET_ID,
	WLRPC_WLC_PHY_NOISE_SAMPLE_REQUEST_ID,
	WLRPC_WLC_PHY_CAL_PERICAL_ID,
	WLRPC_WLC_PHY_TXPOWER_GET_ID,
	WLRPC_WLC_PHY_TXPOWER_SET_ID,
	WLRPC_WLC_PHY_TXPOWER_SROMLIMIT_ID,
	WLRPC_WLC_PHY_RADAR_DETECT_ENABLE_ID,
	WLRPC_WLC_PHY_RADAR_DETECT_RUN_ID,
	WLRPC_WLC_PHY_TEST_ISON_ID,
	WLRPC_WLC_BMAC_COPYFROM_OBJMEM_ID,
	WLRPC_WLC_BMAC_COPYTO_OBJMEM_ID,
	WLRPC_WLC_ENABLE_MAC_ID,
	WLRPC_WLC_MCTRL_ID,
	WLRPC_WLC_CORERESET_ID,
	WLRPC_WLC_BMAC_READ_SHM_ID,
	WLRPC_WLC_BMAC_READ_TSF_ID,
	WLRPC_WLC_BMAC_SET_RXE_ADDRMATCH_ID,
	WLRPC_WLC_BMAC_SET_CWMAX_ID,
	WLRPC_WLC_BMAC_GET_RCMTA_ID,
	WLRPC_WLC_BMAC_SET_RCMTA_ID,
	WLRPC_WLC_BMAC_SET_SHM_ID,
	WLRPC_WLC_SUSPEND_MAC_AND_WAIT_ID,
	WLRPC_WLC_BMAC_WRITE_SHM_ID,
	WLRPC_WLC_BMAC_UPDATE_SHM_ID,
	WLRPC_WLC_BMAC_WRITE_TEMPLATE_RAM_ID,
	WLRPC_WLC_TX_FIFO_SUSPEND_ID,
	WLRPC_WLC_TX_FIFO_RESUME_ID,
	WLRPC_WLC_TX_FIFO_SUSPENDED_ID,
	WLRPC_WLC_HW_ETHERADDR_ID,
	WLRPC_WLC_SET_HW_ETHERADDR_ID,
	WLRPC_WLC_BMAC_CHANSPEC_SET_ID,
	WLRPC_WLC_BMAC_TXANT_SET_ID,
	WLRPC_WLC_BMAC_ANTSEL_TYPE_SET_ID,
	WLRPC_WLC_BMAC_TXFIFO_ID,
	WLRPC_WLC_BMAC_PKTENG_ID,
	WLRPC_WLC_RADIO_READ_HWDISABLED_ID,
	WLRPC_WLC_RM_CCA_MEASURE_ID,
	WLRPC_WLC_SET_SHORTSLOT_ID,
	WLRPC_WLC_WAIT_FOR_WAKE_ID,
	WLRPC_WLC_PHY_TXPOWER_GET_CURRENT_ID,
	WLRPC_WLC_PHY_TXPOWER_HW_CTRL_GET_ID,
	WLRPC_WLC_PHY_TXPOWER_HW_CTRL_SET_ID,
	WLRPC_WLC_PHY_BSSINIT_ID,
	WLRPC_WLC_BAND_STF_SS_SET_ID,
	WLRPC_WLC_PHY_BAND_FIRST_CHANSPEC_ID,
	WLRPC_WLC_PHY_TXPOWER_LIMIT_SET_ID,
	WLRPC_WLC_PHY_BAND_CHANNELS_ID,
	WLRPC_WLC_BMAC_REVINFO_GET_ID,
	WLRPC_WLC_BMAC_STATE_GET_ID,
	WLRPC_WLC_BMAC_XMTFIFO_SZ_GET_ID,
	WLRPC_WLC_BMAC_XMTFIFO_SZ_SET_ID,
	WLRPC_WLC_BMAC_VALIDATE_CHIP_ACCESS_ID,
	WLRPC_WLC_RM_CCA_COMPLETE_ID,
	WLRPC_WLC_RECV_ID,
	WLRPC_WLC_DOTXSTATUS_ID,
	WLRPC_WLC_HIGH_DPC_ID,
	WLRPC_WLC_FATAL_ERROR_ID,
	WLRPC_WLC_PHY_SET_CHANNEL_14_WIDE_FILTER_ID,
	WLRPC_WLC_PHY_NOISE_AVG_ID,
	WLRPC_WLC_PHYCHAIN_INIT_ID,
	WLRPC_WLC_PHYCHAIN_SET_ID,
	WLRPC_WLC_PHYCHAIN_GET_ID,
	WLRPC_WLC_PHY_TKIP_RIFS_WAR_ID,
	WLRPC_WLC_BMAC_COPYFROM_VARS_ID,
	WLRPC_WLC_BMAC_RETRYLIMIT_UPD_ID,
	WLRPC_WLC_BMAC_BTC_MODE_SET_ID,
	WLRPC_WLC_BMAC_BTC_MODE_GET_ID,
	WLRPC_WLC_BMAC_BTC_WIRE_SET_ID,
	WLRPC_WLC_BMAC_BTC_WIRE_GET_ID,
	WLRPC_WLC_BMAC_SET_NORESET_ID,
	WLRPC_WLC_AMPDU_TXSTATUS_COMPLETE_ID,
	WLRPC_WLC_BMAC_FIFOERRORS_ID,
	WLRPC_WLC_PHY_TXPOWER_GET_TARGET_MIN_ID,
	WLRPC_WLC_PHY_TXPOWER_GET_TARGET_MAX_ID,
	WLRPC_WLC_NOISE_CB_ID,
	WLRPC_WLC_BMAC_LED_HW_DEINIT_ID,
	WLRPC_WLC_BMAC_LED_HW_MASK_INIT_ID,
	WLRPC_WLC_PLLREQ_ID,
	WLRPC_WLC_BMAC_TACLEAR_ID,
	WLRPC_WLC_BMAC_SET_CLK_ID,
	WLRPC_WLC_PHY_OFDM_RATESET_WAR_ID,
	WLRPC_WLC_PHY_BF_PREEMPT_ENABLE_ID,
	WLRPC_WLC_BMAC_DISPATCH_IOV_ID,
	WLRPC_WLC_BMAC_DISPATCH_IOC_ID,
	WLRPC_WLC_BMAC_DUMP_ID,
	WLRPC_WLC_CISWRITE_ID,
	WLRPC_WLC_CISDUMP_ID,
	WLRPC_WLC_UPDATE_PHY_MODE_ID,
	WLRPC_WLC_RESET_BMAC_DONE_ID,
	WLRPC_WLC_BMAC_LED_BLINK_EVENT_ID,
	WLRPC_WLC_BMAC_LED_SET_ID,
	WLRPC_WLC_BMAC_LED_BLINK_ID,
	WLRPC_WLC_BMAC_LED_ID,
	WLRPC_WLC_BMAC_RATE_SHM_OFFSET_ID,
	WLRPC_SI_ISCORE_UP_ID,
	WLRPC_WLC_BMAC_PS_SWITCH_ID,
	WLRPC_WLC_PHY_STF_SSMODE_GET_ID,
	WLRPC_WLC_BMAC_DEBUG_ID,
	WLRPC_WLC_EXTLOG_MSG_ID,
	WLRPC_WLC_EXTLOG_CFG_ID,
	WLRPC_BCM_ASSERT_LOG_ID,
	WLRPC_BCM_ASSERT_TYPE_ID,
	WLRPC_WLC_BMAC_SET_PHYCAL_CACHE_FLAG_ID,
	WLRPC_WLC_BMAC_GET_PHYCAL_CACHE_FLAG_ID,
	WLRPC_WLC_PHY_CAL_CACHE_INIT_ID,
	WLRPC_WLC_PHY_CAL_CACHE_DEINIT_ID,
	WLRPC_WLC_BMAC_HW_UP_ID,
	WLRPC_WLC_BMAC_SET_TXPWR_PERCENT_ID,
	WLRPC_WLC_PHYCHAIN_ACTIVE_GET_ID,
	WLRPC_WLC_BMAC_BLINK_SYNC_ID,
	WLRPC_WLC_BMAC_UCODE_DBGSEL_SET_ID,
	WLRPC_WLC_BMAC_UCODE_DBGSEL_GET_ID,
	WLRPC_WLC_PHY_RADAR_DETECT_MODE_SET_ID,
	WLRPC_WLC_PHY_ACIM_NOISEM_RESET_NPHY_ID,
	WLRPC_WLC_PHY_INTERFER_SET_NPHY_ID,
	WLRPC_WLC_BMAC_IFSCTL_EDCRS_SET_ID,
	WLRPC_WLC_BMAC_SET_DEAF,
	WLRPC_WLC_BMAC_CLEAR_DEAF,
	WLRPC_WLC_BMAC_BTC_FLAGS_IDX_SET_ID,
	WLRPC_WLC_BMAC_BTC_FLAGS_IDX_GET_ID,
	WLRPC_WLC_BMAC_BTC_FLAGS_GET_ID,
	WLRPC_WLC_BMAC_BTC_PARAMS_SET_ID,
	WLRPC_WLC_BMAC_BTC_PARAMS_GET_ID,
	WLRPC_WLC_BMAC_BTC_PERIOD_GET_ID,
	WLRPC_WLC_BMAC_BTC_RSSI_THRESHOLD_GET_ID,
	WLRPC_WLC_BMAC_BTC_STUCKWAR_ID,
	WLRPC_WLC_BMAC_CCA_STATS_READ_ID,
	WLRPC_WLC_BMAC_ANTSEL_SET_ID,
	WLRPC_WLC_BMAC_P2P_CAP_ID,
	WLRPC_WLC_BMAC_P2P_SET_ID,
	WLRPC_WLC_PHY_LDPC_SET_ID,
	WLRPC_WLC_BMAC_DNGL_WD_KEEP_ALIVE_ID,
	WLRPC_WLC_BMAC_TX_FIFO_SYNC_ID,
	WLRPC_WLC_BMAC_TX_FIFO_SYNC_COMPLETE_ID,
	WLRPC_WLC_BMAC_WRITE_IHR_ID,
	WLRPC_WLC_PHY_TXPOWER_IPA_ISON,
	WLRPC_WLC_BMAC_AMPDU_SET_ID,
	WLRPC_WLC_BMAC_TXPPR_ID,
	WLRPC_WLC_PHY_ISMUTED_ID,
	WLRPC_WLC_PHY_CAP_GET_ID,
	WLRPC_WLC_BMAC_WOWL_UCODE_ID,
	WLRPC_WLC_BMAC_WOWL_UCODESTART_ID,
	WLRPC_WLC_BMAC_WOWL_INITS_ID,
	WLRPC_WLC_BMAC_WOWL_DNLDDONE_ID,
	WLRPC_WLC_BMAC_SET_CTRL_BT_SHD0_ID,
	WLRPC_WLC_BMAC_4331_EPA_INIT_ID,
	WLRPC_WLC_BMAC_SET_FILT_WAR_ID,
	WLRPC_WLC_BMAC_CONFIG_4331_EPA_ID,
	WLRPC_WLC_BMAC_UCODEMBSS_HWCAP,
	WLRPC_WLC_PHY_DESTROY_CHANCTX_ID,
	WLRPC_WLC_PHY_CREATE_CHANCTX_ID,
	WLRPC_WLC_P2P_INT_PROC_ID,
	WLRPC_WLC_BMAC_SET_DEFMACINTMASK_ID,
	WLRPC_WL_WOWL_DNGLDOWN,
	WLRPC_WLC_BMAC_ENABLE_TBTT_ID,
	WLRPC_WLC_BMAC_SET_EXTLNA_PWRSAVE_SHMEM_ID,
	WLRPC_WLC_BMAC_READ_AMT_ID,
	WLRPC_WLC_BMAC_WRITE_AMT_ID,
	WLRPC_WLC_BMAC_RADIO_HW_ID,
	WLRPC_WLC_PHY_INTERF_RSSI_UPDATE,
	WLRPC_WLC_PHY_INTERF_CHAN_STATS_UPDATE,
	WLRPC_WLC_BMAC_CCA_READ_COUNTER_ID,
	WLRPC_WLC_PHY_GET_EST_POUT_ID,
	WLRPC_WLC_PHY_FORCE_VSDB_CHANS,
	WLRPC_WLC_BMAC_ACTIVATE_SRVSDB,
	WLRPC_WLC_BMAC_DEACTIVATE_SRVSDB,
	WLRPC_WLC_BMAC_SRVSDB_FORCE_SET,
	WLRPC_WLC_TSF_ADJUST,
	WLRPC_WLC_SRVSDB_SWITCH_PPR,
	WLRPC_WLC_PHY_SAR_LIM_ID,
	WLRPC_WLC_PHY_TSSIVIS_THRESH_GET_ID,
	WLRPC_WLC_BMAC_PHY_TXPWR_CACHE_INVALIDATE_ID,
	WLRPC_WLC_PHY_IS_TXBFCAL,
	WLRPC_WLC_PHY_CAL_CACHE_ACPHY_ID,
	WLRPC_WLC_PHY_CAL_CACHE_RESTORE_ID,
	WLRPC_WLC_PHY_PI_UPDATE_OLPC_CAL_ID,
	WLRPC_WLC_PHY_TSSISEN_MIN_ID,
	WLRPC_WLC_BMAC_STF_SET_RATE_SHM_OFFSET_ID,
	WLRPC_WLC_BMAC_GET_NORESET_ID,
	WLRPC_WLC_PHY_NOISE_AVG_PER_ANTENNA_ID,
	WLRPC_WLC_PHY_TXPWR_MIN_GET_ID,
	WLRPC_WLC_PHY_TXPWR_BACKOFF_GET_ID,
	WLRPC_WLC_PHY_MPHASE_CAL_SCHEDULE_ID,
	WLRPC_WLC_PHY_RXGCRS_SET_LOCALE,
	WLRPC_WLC_PHY_GET_BFE_NDP_RECVSTS,
	WLRPC_WLC_BMAC_SET_OBJMEM32_ID,
	WLRPC_WLC_BMAC_COPYTO_OBJMEM32_ID,
	WLRPC_WLC_BMAC_COPYFROM_OBJMEM32_ID,
	WLRPC_WLC_PHY_MU_GROUP_SET_ID,
	WLRPC_WLC_PHY_MIN_TXPWR_LIMIT_GET_ID,
	WLRPC_WLC_AMPDU_SIDECHANNEL_INIT,
	WLRPC_WLC_PHY_PREV_CHANSPEC_ID,
	WLRPC_WLC_PHY_CALC_PPR_PWR_CAP_ID,
	WLRPC_WLC_PHY_NOISE_PMSTATE_SET_ID,
	WLRPC_LAST
} wlc_rpc_id_t;



typedef struct wlc_rpc_ctx {
	rpc_info_t *rpc;
	wlc_info_t *wlc;
	wlc_hw_info_t *wlc_hw;
} wlc_rpc_ctx_t;

static INLINE rpc_buf_t *
wlc_rpc_buf_alloc(rpc_info_t *rpc, bcm_xdr_buf_t *b, uint len, wlc_rpc_id_t rpc_id)
{
	int err;
	rpc_buf_t *rpc_buf;

	rpc_buf = bcm_rpc_buf_alloc(rpc, len + sizeof(uint32));

	if (!rpc_buf)
		return NULL;

	bcm_xdr_buf_init(b, bcm_rpc_buf_data(bcm_rpc_tp_get(rpc), rpc_buf),
	                 len + sizeof(uint32));

	err = bcm_xdr_pack_uint32(b, rpc_id);
	ASSERT(!err);
	BCM_REFERENCE(err);

	return rpc_buf;
}


static __inline int
_wlc_rpc_call(struct rpc_info *rpc, rpc_buf_t *send)
{
		int _err = 0;
		_err = bcm_rpc_call(rpc, send);
		if (_err) {
			_err = 0;
		}
		return _err;
}

#define wlc_rpc_call(rpc, send) (_wlc_rpc_call(rpc, send))

#include <sbhnddma.h>
#include <sbhndpio.h>
#include <d11.h>

#define WLC_BMAC_F_AMPDU_MPDU 1
/* Align the WLRPCTXPARAMS offset in WLRPC_WLC_BMAC_TXFIFO_ID packet on word boundary in
wlc_rpctx_tx() by padding 2 bytes before pushing the WLRPCTXPARAMS.
*/
#define WLC_BMAC_F_PAD2 2
/* WLRPC_WLC_BMAC_TXFIFO_ID packet is padded with 2 bytes in wlc_rpctx_tx() if WLC_BMAC_F_PAD2
is set
*/
#define WLC_RPC_TXFIFO_UNALIGN_PAD_2BYTE 2
#ifdef WLC_LOW
extern void wlc_rpc_bmac_dispatch(wlc_rpc_ctx_t *rpc_ctx, struct rpc_buf* buf);
extern void wlc_rpc_bmac_dump_txfifohist(wlc_hw_info_t *wlc_hw, bool dump_clear);
#else
extern void wlc_rpc_high_dispatch(wlc_rpc_ctx_t *ctx, struct rpc_buf* buf);
#endif

/* Packed structure for ease of transport across RPC bus along uint32 boundary */
typedef struct wlc_rpc_txstatus {
	uint32 frameid_framelen;
	uint32 sequence_status;
	uint32 lasttxtime;
	uint32 ackphyrxsh_phyerr;

	uint32 s3;
	uint32 s4;
	uint32 s5;
	uint32 s6;
	uint32 s7;
} wlc_rpc_txstatus_t;

static INLINE
void txstatus2rpc_txstatus(tx_status_t *txstatus, wlc_rpc_txstatus_t *rpc_txstatus)
{
	ASSERT(txstatus);
	ASSERT(rpc_txstatus);

	rpc_txstatus->frameid_framelen = (txstatus->frameid << 16) | txstatus->framelen;
	rpc_txstatus->sequence_status = (txstatus->sequence << 16) | txstatus->status.raw_bits;
	rpc_txstatus->lasttxtime = txstatus->lasttxtime;
	rpc_txstatus->ackphyrxsh_phyerr = (txstatus->ackphyrxsh << 16) | txstatus->phyerr;

	rpc_txstatus->s3 = txstatus->status.s3;
	rpc_txstatus->s4 = txstatus->status.s4;
	rpc_txstatus->s5 = txstatus->status.s5;
	rpc_txstatus->s6 = txstatus->status.ack_map1;
	rpc_txstatus->s7 = txstatus->status.ack_map2;
}

static INLINE
void rpc_txstatus2txstatus(wlc_rpc_txstatus_t *rpc_txstatus, tx_status_t *txstatus)
{
	ASSERT(txstatus);
	ASSERT(rpc_txstatus);
	txstatus->framelen = rpc_txstatus->frameid_framelen & 0xffff;
	txstatus->status.raw_bits = rpc_txstatus->sequence_status & 0xffff;
	txstatus->frameid = (rpc_txstatus->frameid_framelen >> 16) & 0xffff;
	txstatus->sequence  = (rpc_txstatus->sequence_status >> 16) & 0xffff;
	txstatus->lasttxtime = rpc_txstatus->lasttxtime & 0xffffffff;
	txstatus->ackphyrxsh  = (rpc_txstatus->ackphyrxsh_phyerr >> 16) & 0xffff;
	txstatus->phyerr = rpc_txstatus->ackphyrxsh_phyerr & 0xffff;

	txstatus->status.s3 = rpc_txstatus->s3;
	txstatus->status.s4 = rpc_txstatus->s4;
	txstatus->status.s5 = rpc_txstatus->s5;
	txstatus->status.ack_map1 = rpc_txstatus->s6;
	txstatus->status.ack_map2 = rpc_txstatus->s7;
}

#endif /* WLC_RPC_H */
