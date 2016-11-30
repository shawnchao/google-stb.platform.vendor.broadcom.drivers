/*
 * Private SCAN info of
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
 * $Id: wlc_scan_priv.h 638671 2016-05-18 15:33:37Z $
 */

#ifndef _WLC_SCAN_PRIV_H_
#define _WLC_SCAN_PRIV_H_

#include <wlc_scan.h>

/* forward declarations */
typedef struct scan_info scan_info_t;
typedef struct scan_info_cmn scan_cmn_info_t;

typedef enum {
	WLC_SCAN_STATE_START = 0,
	WLC_SCAN_STATE_SEND_PROBE = 1,
	WLC_SCAN_STATE_LISTEN = 2,
	WLC_SCAN_STATE_CHN_SCAN_DONE = 3,
	WLC_SCAN_STATE_PARTIAL = 4,
	WLC_SCAN_STATE_COMPLETE = 5,
	WLC_SCAN_STATE_ABORT = 6
}	scan_state_t;

/* Flag is used for going with active scanning in Radar channel with AP's presence when
 * parallel passive scanning is completed in both the cores
 */
#define SCAN_CMN_FLAG_NO_RADAR_CLEAR	(1 << 0)
#define SCAN_CMN_FLAG_5G_5G_PASSIVE		(1 << 1)


struct scan_info {
	struct wlc_scan_info	*scan_pub;
	uint		unit;
	wlc_info_t	*wlc;
	osl_t		*osh;
	wlc_scandb_t	*sdb;
	scan_cmn_info_t *scan_cmn;

	int		channel_idx;	/* index in chanspec_list of current channel we are
					 * scanning
					 */
	/* scan times are in milliseconds */
	chanspec_t	chanspec_list[MAXCHANNEL];	/* list of channels to scan */
	int		channel_num;	/* length of chanspec_list */
	int8		pass;		/* current scan pass or scan state */
	int8		nprobes;	/* number of probes per channel */
	int8		npasses;	/* number of passes per channel */
	uint16		active_time;	/* dwell time per channel for active scanning */
	uint16		passive_time;	/* dwell time per channel for passive scanning */
	uint32		start_tsf;	/* TSF read from chip at start of channel scan */
	struct wl_timer *timer;		/* timer for BSS scan operations */
	scancb_fn_t	cb;				/* function to call when scan is done */
	void		*cb_arg;		/* arg to cb fn */
	bool		extdscan;

	/* EXT_STA */
	bool		suppress_ssid;	/* Suppress ssid after hibernation */

	int 		nssid;		/* number off ssids in the ssid list */
	wlc_ssid_t	*ssid_list;	/* ssids to look for in scan (could be dynamic) */
	wlc_ssid_t	*ssid_prealloc;	/* pointer to preallocated (non-dynamic) store */
	int		nssid_prealloc;	/* number of preallocated entries */

	uint8   ssid_wildcard_enabled;
	wlc_bsscfg_t	*bsscfg;
	actcb_fn_t	act_cb;		/* function to call when scan is done */
	void		*act_cb_arg;	/* arg to cb fn */
	uint		wd_count;
	int		status;		/* deferred status */
	uint8		pspend_guard_time;	/* ms */
	uint8		wsuspend_guard_time;	/* ms */
	wl_pwr_scan_stats_t *scan_stats;	/* power stats for scan */

	uint8		scan_rx_pwrsave;	/* reduce rxchain to save power in scan rx window */
	uint8		scan_tx_pwrsave;	/* reduce txchain to save power in scan tx */
	uint8		scan_ps_txchain;	/* track txchain and restore after scan complete */
	uint8		scan_ps_rxchain;	/* track rxchain and restore after scan complete */
	uint8		scan_pwrsave_enable;	/* turn on/off single core scanning */

	wlc_bsscfg_t *scanmac_bsscfg;		/* scanmac bsscfg */
	wl_scanmac_config_t scanmac_config;	/* scanmac config */
	bool is_scanmac_config_updated;		/* config updated flag */

	struct ether_addr sa_override;		/* override source MAC */
	wlc_msch_req_handle_t	*msch_req_hdl;	/* hdl to msch request */
	chanspec_t	cur_scan_chanspec;	/* current scan channel spec */
	scan_state_t state;			/* Channel scheduler state */
	wl_scan_summary_t *scan_sum_chan_info;
	/* keep all these debugging related fields at the end */
		uint32	timeslot_id;
		uint8	scan_mimo_override;		/* force MIMO scan on this scan */
		void	*stf_scan_req;			/* STF Arbitrator request */
};
struct scan_info_cmn {
	/* Scan private shared elements.. */
	uint		memsize;	/* allocated size of this structure (for freeing) */
	/* scan defaults */
	struct {
		uint16	home_time;	/* dwell time for the home channel between channel
					 * scans
					 */
		uint16	unassoc_time;	/* dwell time per channel when unassociated */
		uint16	assoc_time;	/* dwell time per channel when associated */
		uint16	passive_time;	/* dwell time per channel for passive scanning */
		int8	nprobes;	/* number of probes per channel */
		int8	passive;	/* scan type: 1 -> passive, 0 -> active */
	} defaults;

	bool	force_active;	/* Force passive to active conversion on radar/restricted channel */
	uint16 home_away_time; /* dwell time in away channels */

	uint32 flag;			/* flag bits under scan_cmn */
#ifdef WLRSDB
	scancb_fn_t	cb;		/* function to call when scan is done */
	void		*cb_arg;	/* arg to cb fn */
	uint8 num_of_cbs;
	uint8 first_scanresult_status; /* First chain scanresults status in parallel scanning */
	bool rsdb_parallel_scan;    /* If set, parallel scan is on in an RSDB chip */
	chanspec_t *chanspeclist;
	uint chanspec_list_size;

#endif /* WLRSDB */
	uint8 chan_offset;    /* channel idx during scan to store per chan specific statistics */
	wl_scan_summary_t *scn_summary;	/* parallel scan statistics */
	uint16 scan_sync_id;	/* sync id counter for scan_summary */

};
typedef struct scan_iter_params {
	wlc_bss_list_t *bss_list;	/* list on which cached items will be added */
	int merge;			/* if TRUE, merge cached entries with different timestamp
					 * to existing entries on bss_list
					 */
	uint current_ts;		/* timestamp of most recent cache additions */
} scan_iter_params_t;

#define SCAN_WLC(scan)			((scan)->wlc)
#define SCAN_UNIT(scan)			((scan)->unit)
#define SCAN_OSH(scan)			((scan)->osh)

#ifdef WLRSDB
#define RSDB_PARALLEL_SCAN_ON(scan) wlc_scan_is_parallel_enab(scan)
#ifdef WLRSDB_POLICY_MGR
#define WLC_SCAN_SIB_ISSET(wlc, bandindex)	\
	wlc_rsdb_policymgr_is_sib_set(wlc->rsdb_policy_info, bandindex)
#else
#define WLC_SCAN_SIB_ISSET(wlc, bandindex)	FALSE
#endif /* WLRSDB_POLICY_MGR */
#else
#define RSDB_PARALLEL_SCAN_ON(scan) FALSE
#endif /* WLRSDB */

#define WLC_BAND_T			wlcband_t
#define IS_PM_PENDING(scan)		(SCAN_WLC(scan)->PMpending)
#define IS_ASSOCIATED(scan)		(SCAN_WLC(scan)->pub->associated)
#define IS_BSS_ASSOCIATED(cfg)		((cfg)->associated)
#define IS_IBSS_ALLOWED(scan)		(SCAN_WLC(scan)->ibss_allowed)
#define IS_AP_ACTIVE(scan)		AP_ACTIVE(SCAN_WLC(scan))
#define IS_EXPTIME_CNT_ZERO(scan)	(SCAN_WLC(scan)->exptime_cnt == 0)
#define IS_N_ENAB(scan)			N_ENAB(SCAN_WLC(scan)->pub)
#define IS_11H_ENAB(scan)		WL11H_ENAB(SCAN_WLC(scan))
#define IS_11D_ENAB(scan)		WL11D_ENAB(SCAN_WLC(scan))
#define IS_SIM_ENAB(scan)		ISSIM_ENAB(SCAN_WLC(scan)->pub->sih)
#define IS_P2P_ENAB(scan)		P2P_ENAB(SCAN_WLC(scan)->pub)
#define IS_MCHAN_ENAB(scan)		MCHAN_ENAB(SCAN_WLC(scan)->pub)
#define IS_CCX_ENAB(scan)		CCX_ENAB(SCAN_WLC(scan)->pub)
#define IS_AUTOCOUNTRY_ENAB(scan)	WLC_AUTOCOUNTRY_ENAB(SCAN_WLC(scan))
#define IS_EXTSTA_ENAB(scan)		WLEXTSTA_ENAB(SCAN_WLC(scan)->pub)
#define IS_AS_IN_PROGRESS(scan)		AS_IN_PROGRESS(SCAN_WLC(scan))
#define IS_COREREV(scan)		(SCAN_WLC(scan)->pub->corerev)
#define IS_SCAN_BLOCK_DATAFIFO(scan, bit)	(SCAN_WLC(scan)->block_datafifo & (bit))
#define SCAN_BCN_PROMISC(scan)		(SCAN_WLC(scan)->bcnmisc_scan)
#define SCAN_RESULT_PTR(scan)		(SCAN_WLC(scan)->scan_results)
#define SCAN_RESULT_MEB(scan, m)	(SCAN_WLC(scan)->scan_results->m)
#define SCAN_FOREACH_STA(scan, idx, cfg)	FOREACH_STA(SCAN_WLC(scan), (idx), (cfg))
#define SCAN_FOREACH_AS_STA(scan, idx, cfg)	FOREACH_AS_STA(SCAN_WLC(scan), (idx), (cfg))
#define SCAN_USER(scan, cfg)		((cfg) != NULL ? (cfg) : SCAN_WLC(scan)->cfg)
#define SCAN_HOME_CHANNEL(scan)		(SCAN_WLC(scan)->home_chanspec)
#define SCAN_ISUP(scan)			(SCAN_WLC(scan)->pub->up)
#define SCAN_STAY_AWAKE(scan)		STAY_AWAKE(SCAN_WLC(scan))
#define SCAN_NBANDS(scan)		NBANDS(SCAN_WLC(scan))
#define SCAN_BLOCK_DATAFIFO_SET(scan, bit)	wlc_block_datafifo(SCAN_WLC(scan), bit, bit)
#define SCAN_BLOCK_DATAFIFO_CLR(scan, bit)	wlc_block_datafifo(SCAN_WLC(scan), bit, 0)
#define SCAN_READ_TSF(scan, a, b)	wlc_read_tsf(SCAN_WLC(scan), a, b)
#define SCAN_IS_MBAND_UNLOCKED(scan)	IS_MBAND_UNLOCKED(SCAN_WLC(scan))
#define SCAN_VALID_CHANNEL20_IN_BAND(scan, bu, val) \
	VALID_CHANNEL20_IN_BAND(SCAN_WLC(scan), (bu), (val))
#define SCAN_WL_DOWN(scan)		wl_down(SCAN_WLC(scan)->wl)
#define SCAN_GET_PI_PTR(scan)		(WLC_PI(SCAN_WLC(scan)))
#define SCAN_BAND_PI_RADIO_CHANSPEC(scan)	(SCAN_WLC(scan)->chanspec)
#define SCAN_DEVICEREMOVED(scan)	DEVICEREMOVED(SCAN_WLC(scan))
#define SCAN_TO_MUTE(scan, b, c)	wlc_mute(SCAN_WLC(scan), (b), (c))
#define SCAN_GET_CUR_BAND(scan)		(SCAN_WLC(scan)->band)
#define SCAN_GET_PI_BANDUNIT(scan, bu)	(WLC_PI_BANDUNIT(SCAN_WLC(scan), bu))
#define SCAN_OTHERBANDUNIT(scan)	OTHERBANDUNIT(SCAN_WLC(scan))
#define SCAN_GET_BANDSTATE(scan, bu)	(SCAN_WLC(scan)->bandstate[(bu)])
#define SCAN_CMIPTR(scan)		(SCAN_WLC(scan)->cmi)
#define SCAN_IS_MATCH_SSID(scan, ssid1, ssid2, len1, len2) \
	WLC_IS_MATCH_SSID(SCAN_WLC(scan), (ssid1), (ssid2), (len1), (len2))
#define SCAN_GET_TSF_TIMERLOW(scan) \
	(R_REG(SCAN_WLC(scan)->osh, &SCAN_WLC(scan)->regs->tsf_timerlow))
#define SCAN_MAXBSS(scan)		(SCAN_WLC(scan)->pub->tunables->maxbss)
#define SCAN_ISCAN_CHANSPEC_LAST(scan)	(SCAN_WLC(scan)->iscan_chanspec_last)
#define SCAN_SET_WATCHDOG_FN(fnpt)
#define SCAN_RESTORE_BSSCFG(scan, cfg)
#define SCAN_ANQPO_ENAB(scan)		ANQPO_ENAB(SCAN_WLC(scan)->pub)
#define SCAN_CLK(scan)			(scan)->wlc->clk

#define wlc_scan_quiet_chanspec(scan, chanspec) \
	wlc_quiet_chanspec(SCAN_WLC(scan)->cmi, chanspec)
#define wlc_scan_valid_chanspec_db(scan, chanspec) \
	wlc_valid_chanspec_db(SCAN_WLC(scan)->cmi, chanspec)
#define wlc_scan_bss_list_free(scan) \
	wlc_bss_list_free(SCAN_WLC(scan), SCAN_WLC(scan)->scan_results)
#define wlc_scan_pm2_sleep_ret_timer_start(cfg) \
	wlc_pm2_sleep_ret_timer_start(cfg, 0)
#define wlc_scan_set_pmstate(cfg, state) \
	wlc_set_pmstate(cfg, state)
#define wlc_scan_skip_adjtsf(scan, skip, cfg, user, bands) \
	wlc_skip_adjtsf(SCAN_WLC(scan), skip, cfg, user, bands)
#define wlc_scan_suspend_mac_and_wait(scan) \
	wlc_suspend_mac_and_wait(SCAN_WLC(scan))
#define wlc_scan_enable_mac(scan) \
	wlc_enable_mac(SCAN_WLC(scan));
#define wlc_scan_mhf(scan, idx, mask, val, bands) \
	wlc_mhf(SCAN_WLC(scan), idx, mask, val, bands)
#define _wlc_scan_sendprobe(scan, cfg, ssid, len, sa, da, bssid, \
	rateoverride, extra_ie, extra_ie_len) \
	wlc_sendprobe(SCAN_WLC(scan), cfg, ssid, len, sa, da, bssid, \
	rateoverride, extra_ie, extra_ie_len)
#define wlc_scan_set_wake_ctrl(scan) \
	wlc_set_wake_ctrl(SCAN_WLC(scan))
#define wlc_scan_ibss_enable(cfg) \
	wlc_ibss_enable(cfg)
#define wlc_scan_ibss_disable_all(scan) \
	wlc_ibss_disable_all(SCAN_WLC(scan))
#define wlc_scan_bss_mac_event(scan, cfg, msg, addr, result, status, auth_type, data, datalen) \
	wlc_bss_mac_event(SCAN_WLC(scan), cfg, msg, addr, result, status, auth_type, data, datalen)
#define wlc_scan_validate_bcn_phytxctl(scan, cfg) \
	wlc_validate_bcn_phytxctl(SCAN_WLC(scan), cfg)
#define wlc_scan_mac_bcn_promisc(scan) \
	wlc_mac_bcn_promisc(SCAN_WLC(scan))
#define wlc_scan_ap_mute(scan, mute, cfg, user) \
	wlc_ap_mute(SCAN_WLC(scan), mute, cfg, user)
#define wlc_scan_tx_resume(scan) \
	wlc_tx_resume(SCAN_WLC(scan))
#define wlc_scan_send_q(scan) \
	wlc_send_active_q(SCAN_WLC(scan))
#define wlc_scan_11d_scan_start(scan) \
	wlc_11d_scan_start(SCAN_WLC(scan)->m11d)
#define wlc_scan_11d_scan_complete(scan, status) \
	wlc_11d_scan_complete(SCAN_WLC(scan)->m11d, status)
#define wlc_scan_11d_get_autocountry_default(scan) \
	wlc_11d_get_autocountry_default(SCAN_WLC(scan)->m11d)
#define wlc_scan_radio_mpc_upd(scan) \
	wlc_radio_mpc_upd(SCAN_WLC(scan))
#define wlc_scan_radio_upd(scan) \
	wlc_radio_upd(SCAN_WLC(scan))
#define wlc_scan_get_chanvec(scan, acdef, bandtype, chanvec) \
	wlc_channel_get_chanvec(SCAN_WLC(scan), (acdef), (bandtype), (chanvec))
#define wl_scan_anqpo_scan_start(scan) \
	wl_anqpo_scan_start(SCAN_WLC(scan)->anqpo);
#define wl_scan_anqpo_scan_stop(scan) \
	wl_anqpo_scan_stop(SCAN_WLC(scan)->anqpo);
#define wl_scan_hnd_time() 0
#endif /* _WLC_SCAN_PRIV_H_ */
