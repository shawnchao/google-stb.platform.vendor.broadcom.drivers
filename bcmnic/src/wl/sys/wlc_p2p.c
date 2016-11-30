/**
 * @file
 * @brief
 * WiFi P2P source file
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
 * GO Power Management is somewhat global because there is only one set of registers
 * controlling all BSSs (TSF, beacon interval, DTIM interval) even though each GO has
 * its own CTWindow, HPS, and NoA schedule.
 *
 * Client Power Management is per BSS given that each STA has to keep track of its
 * own BSS (TSF, beacon interval, DTIM interval, CTWindow, HPS, and NoA schedule).
 *
 * CTWindow takes precedence over HPS of NoA absence and HPS of NoA absence
 * takes precedence over non-CTWindow.
 *
 * $Id: wlc_p2p.c 659389 2016-09-14 02:41:12Z $
 */



#include <wlc_cfg.h>

#ifdef WLP2P
#ifndef WLMCNX
#error "WLMCNX needs to be defined for WLP2P"
#endif

#ifndef WL_BSSCFG_TX_SUPR
#error "WL_BSSCFG_TX_SUPR needs to be defined for WLP2P"
#endif


#include <typedefs.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmendian.h>
#include <bcmutils.h>
#include <siutils.h>
#include <wlioctl.h>
#include <bcmwpa.h>
#include <d11.h>
#include <wlc_rate.h>
#include <wlc_pub.h>
#include <wlc_bsscfg.h>
#include <wlc.h>
#include <wlc_scb.h>
#include <wlc_scan_utils.h>
#include <wlc_apps.h>
#include <wlc_bmac.h>
#ifdef WLMCHAN
#include <wlc_mchan.h>
#endif
#include <wl_export.h>
#include <wlc_utils.h>
#include <wlc_mcnx.h>
#include <wlc_p2p.h>
#ifdef PROP_TXSTATUS
#include <wlc_ampdu.h>
#include <wlfc_proto.h>
#include <wlc_wlfc.h>
#endif
#include <wlc_probresp.h>
#include <wlc_pcb.h>
#include <wlc_tbtt.h>
#include <wlc_ie_mgmt.h>
#include <wlc_ie_mgmt_ft.h>
#include <wlc_ie_mgmt_vs.h>
#include <wlc_tx.h>
#ifdef WLRSDB
#include <wlc_rsdb.h>
#endif
#include <wlc_objregistry.h>
#include <wlc_event.h>
#include <wlc_bsscfg_psq.h>
#include <wlc_bsscfg_viel.h>
#if defined(WL_NAN_PD_P2P)
#include <wlc_nan.h>
#include <wlc_tsmap.h>
#endif /* WL_NAN_PD_P2P */
#include <wlc_assoc.h>
#include <wlc_pm.h>
#include <wlc_hw.h>
#include <wlc_event_utils.h>
#include <wlc_dump.h>
#include <wlc_iocv.h>

#ifndef USE_DEF_P2P_IE
#define USE_DEF_P2P_IE 0
#endif /* USE_DEF_P2P_IE */
#ifndef WFA_VER_1_02_TEST
#define WFA_VER_1_02_TEST 0
#endif

#ifndef DISABLE_UCODE_PRBRESP
#define DISABLE_UCODE_PRBRESP 0
#endif

/* iovar table */
enum {
	IOV_P2P,		/**< enable/disable p2p feature */
	IOV_P2P_SSID,		/**< set/get p2p wildcard SSID */
	IOV_P2P_DISC,		/**< enable/disable p2p discovery */
	IOV_P2P_DEV,		/**< query "p2p device" bsscfg index */
	IOV_P2P_SCAN,		/**< perform "p2p scan" in FIND phase */
	IOV_P2P_STATE,		/**< set/reset discovery state */
	IOV_P2P_IFADD,		/**< add "p2p interface" */
	IOV_P2P_IFDEL,		/**< del "p2p interface" */
	IOV_P2P_IF,		/**< query "p2p interface" bsscfg index */
	IOV_P2P_OPS,		/**< set/clear OppPS & CTWindow */
	IOV_P2P_NOA,		/**< set/clear NoA schedule */
	IOV_P2P_IFUPD,		/**< set "p2p interface" property */
	IOV_P2P_DEFIE,		/**< Insert default p2p ies */
	IOV_P2P_FEATURES,	/**< set get some feature flags */
	IOV_P2P_DA_OVERRIDE,	/**< get/set P2P device address */
	IOV_P2P_APP_ACTIVE,	/**< get/set P2P appication active */
	IOV_P2P_ADD_WFDS_HASH, /**< Insert a WFDS Advertiser hash information */
	IOV_P2P_DEL_WFDS_HASH, /**< Delete a WFDS Advertiser hash information */
	IOV_P2P_NAN,            /* super command for NAN+P2P operation */
};

static const bcm_iovar_t p2p_iovars[] = {
	{"p2p", IOV_P2P, (IOVF_SET_DOWN|IOVF_RSDB_SET), 0, IOVT_BOOL, 0},
	{"p2p_ssid", IOV_P2P_SSID, 0, 0, IOVT_BUFFER, sizeof(wlc_ssid_t)},
	{"p2p_disc", IOV_P2P_DISC, 0, 0, IOVT_BOOL, 0},
	{"p2p_dev", IOV_P2P_DEV, 0, 0, IOVT_UINT8, 0},
	{"p2p_scan", IOV_P2P_SCAN, 0, 0, IOVT_BUFFER, 0},
	{"p2p_state", IOV_P2P_STATE, 0, 0, IOVT_BUFFER, OFFSETOF(wl_p2p_disc_st_t, chspec)},
	{"p2p_ifadd", IOV_P2P_IFADD, 0, 0, IOVT_BUFFER, OFFSETOF(wl_p2p_if_t, chspec)},
	{"p2p_ifdel", IOV_P2P_IFDEL, 0, 0, IOVT_BUFFER, ETHER_ADDR_LEN},
	{"p2p_if", IOV_P2P_IF, 0, 0, IOVT_BUFFER, ETHER_ADDR_LEN},
	{"p2p_ops", IOV_P2P_OPS, 0, 0, IOVT_BUFFER, sizeof(wl_p2p_ops_t)},
	{"p2p_noa", IOV_P2P_NOA, 0, 0, IOVT_BUFFER, WL_P2P_SCHED_FIXED_LEN},
	{"p2p_ifupd", IOV_P2P_IFUPD, 0, 0, IOVT_BUFFER, ETHER_ADDR_LEN + 1},
	{"p2p_defie", IOV_P2P_DEFIE, 0, 0, IOVT_BOOL, 0},
	{"p2p_features", IOV_P2P_FEATURES, 0, 0, IOVT_UINT32, 0},
	{"p2p_da_override", IOV_P2P_DA_OVERRIDE, 0, 0, IOVT_BUFFER, ETHER_ADDR_LEN},
	{"p2p_app_active", IOV_P2P_APP_ACTIVE, 0, 0, IOVT_BOOL, 0},
#ifdef WLWFDS
	{"p2p_add_wfds_hash", IOV_P2P_ADD_WFDS_HASH, 0, 0, IOVT_BUFFER, sizeof(wl_p2p_wfds_hash_t)},
	{"p2p_del_wfds_hash", IOV_P2P_DEL_WFDS_HASH, 0, 0, IOVT_BUFFER, sizeof(wl_p2p_wfds_hash_t)},
#endif /* WLWFDS */
	{"p2p_nan", IOV_P2P_NAN, 0, 0, IOVT_BUFFER, 0},
	{NULL, 0, 0, 0, 0, 0}
};

typedef struct wl_p2p_wfds_hash_list {
	wl_p2p_wfds_hash_t		adv_data;
	struct wl_p2p_wfds_hash_list	*next;
} wl_p2p_wfds_hash_list_t;

typedef struct wlc_p2p_adv_svc_info_attr {
	uint32	advt_id;
	uint16	nw_cfg_method;
	uint8	name_len;
	uint8	service_name[MAX_WFDS_SVC_NAME_LEN];
	struct wlc_p2p_adv_svc_info_attr *next;
} wlc_p2p_adv_svc_info_attr;

typedef struct wl_p2p_nan_inst_id_list {
	uint8 inst_type;
	//wl_nan_instance_id_t    inst_id;
	uint8   inst_id;
	void    *nan_build_cb_handle; /* nan attribute build cb handle */
	void    *nan_parse_cb_handle; /* nan attribute parse cb handle */
	void    *p2pinfo;
	struct wl_p2p_nan_inst_id_list *next;
} wl_p2p_nan_inst_id_list_t;

/** P2P module shared state */
typedef struct wlc_p2p_data {
	wlc_bsscfg_t 	*devcfg;	/**< device bsscfg (discovery) */
	struct ether_addr devaddr;	/**< device address (discovery) */
	uint8		flags;		/**< see flags */
	uint8		state;		/**< device state (discovery), see state */
	wlc_ssid_t	ssid;		/**< P2P wildcard SSID (discovery) */
	uint8		dialog;		/**< next dialog token */
	uint32		features;	/**< feature 'overrides' */
	uint		scb_timeout;	/**< p2p specific scb_timeout for GOs */
	uint16		prblen;		/**< SHM probe request buffer len */
	uint8		num_wfds_adv;
	/** Store the wfds_hash data's for every advertised service */
	wl_p2p_wfds_hash_list_t		*reg_adv_svc_list;
	/** Store the current advertisement service list to be sent  */
	wlc_p2p_adv_svc_info_attr	*to_send_wfds_adv_list;
	wl_p2p_nan_config_t *p2p_nan_cfg;
	wl_p2p_nan_inst_id_list_t *ids_head;
	uint8 dw_counter;
	void *favail_sched_handle;
/* ==== please keep following stuff at the bottom ==== */
#if USE_DEF_P2P_IE
	bool		insert_def_p2pie;
#endif
} wlc_p2p_data_t;

/** P2P module instance state */
struct wlc_p2p_info {
	wlc_info_t	*wlc;		/**< wlc structure */
	int		cfgh;		/**< handle to private data in bsscfg (Client/GO) */
	int		cfgh_cmn;	/**< handle to private data in bsscfg (Client/GO/Device) */
	int		scbh;		/**< private data in scb */
	wlc_p2p_data_t	*p2p_data;	/**< p2p info shared structure */
	bcm_notif_h cxnoa_upd_hdl;	/* notifier handles */
	bcm_notif_h cxnoa_desc_hdl;	/* notifier handles */
};

/* wlc_p2p_info flags */
#define P2P_FLAG_SCAN_ALL	0x02	/**< scan both P2P and legacy devices in SCAN phase */
#define WLC_P2P_INFO_FLAG_WFDS_HASH 0x4 /**< 'hash' attr exists in probe req */

#define DISCCNTINC(pm, ctr)

/* As of now, on GO there is only one peridic schedule max at any given time;
 * on Client there is one periodic schedule plus one one-tiem requested schedule max.
 */
#define WLC_P2P_MAX_SCHED	2	/**< two concurrent Absence schedules */

/* On GO the start time in schedule descriptors is in local TSF time,
 * on Client the start time in schedule descriptors is in remote TSF time.
 */
typedef struct {
	uint8 idx;			/**< current descriptor index in 'desc' */
	uint8 cnt;			/**< the number of descriptors in 'desc' */
	uint8 flags;			/**< see WLC_P2P_SCHED_XXX */
	uint8 action;			/**< see WL_P2P_SCHED_ACTION_XXXX */
	uint8 option;			/**< see WL_P2P_SCHED_OPTION_XXXX */
	uint8 start;			/**< absence start time in percent of beacon interval */
	uint8 dur;			/**< absense period in percent of beacon interval */
	wl_p2p_sched_desc_t *desc;	/**< len = cnt * sizeof(wl_p2p_sched_desc_t) */
} wlc_p2p_sched_t;

/* noa schedule 'flags' */
#define WLC_P2P_SCHED_NORM	0x1	/**< the schedule is normalized */
#define WLC_P2P_SCHED_RUN	0x2	/**< the schedule is running */


/** bsscfg specific data - Client/GO */
typedef struct {
	bool enable;			/**< enable/disable p2p */
	bool ps;			/**< GO is in PS state */
	uint16 flags;			/**< see 'flags' below */
	/** schedules - see 'sched' index below */
	wlc_p2p_sched_t sched[WLC_P2P_MAX_SCHED];
	/** current OppPS and CTWindow */
	bool ops;
	uint8 ctw;
	/* current absence schedule */
	uint8 id;			/**< NoA index in beacons/prbrsps */
	uint8 action;			/**< see WL_P2P_SCHED_ACTION_XXXX */
	wl_p2p_sched_desc_t cur;	/**< active schedule in h/w */
	uint32 start;			/**< schedule commence time */
	uint16 count;			/**< schedule count cache */
	uint8 sidx;			/**< schedule index into 'sched' array */
	/** back pointer to bsscfg */
	wlc_bsscfg_t *bsscfg;
	/** send NoA callback */
	wlc_p2p_noa_cb_t send_noa_cb;
	void *send_noa_cb_arg;
/* ==== please keep these debug stuff at the bottom ==== */
} bss_p2p_info_t;

/* bss_p2p_info_t 'flags' */
#define WLC_P2P_INFO_CUR	0x01	/**< 'cur' is valid */
#define WLC_P2P_INFO_OPS	0x02	/**< 'ops' is valid */
#define WLC_P2P_INFO_ID		0x04	/**< 'id' is valid */
#define WLC_P2P_INFO_NET	0x08	/**< network parms valid (Client assoc'd or GO is up) */
#define WLC_P2P_INFO_STRT	0x10	/**< 'start' is valid */
#ifdef WLMCHAN
#define WLC_P2P_INFO_MCHAN_NOA	0x20	/**< mchan noa schedule in use */
#endif
#define WLC_P2P_INFO_DLY	0x40	/**< delay absence start init in SHM BSS block */
#define WLC_P2P_INFO_IGN_SMPS		0x80	/**< don't process SM PS in beacons */
#define WLC_P2P_INFO_APSD_RETRIG	0x200	/**< APSD retrigger is expected */
#define WLC_P2P_INFO_PSPOLL_RESEND	0x400	/**< Another PSPOLL is expected */


/* p2p info 'sched' index */
#define WLC_P2P_NOA_ABS		0	/**< scheduled Absence */
#define WLC_P2P_NOA_REQ_ABS	1	/**< requested Absence */

#define INTRCNTINC(p2p, ctr)

/* p2p info accessor */
#define P2P_BSSCFG_CUBBY_LOC(pm, cfg) ((bss_p2p_info_t **)BSSCFG_CUBBY((cfg), (pm)->cfgh))
#define P2P_BSSCFG_CUBBY(pm, cfg) (*(P2P_BSSCFG_CUBBY_LOC(pm, cfg)))
#define BSS_P2P_INFO(pm, cfg) P2P_BSSCFG_CUBBY(pm, cfg)

/** bsscfg specific data - Client/GO/Device */
typedef struct {
	uint8 flags;
} bss_p2p_cmn_info_t;

/* cmn p2p info 'flags' */
#define BSS_P2P_CMN_INFO_P2P_IE	0x01	/**< include P2P IEs in frame */

/* cmn p2p info accessor */
#define BSS_P2P_CMN_INFO(pm, cfg) ((bss_p2p_cmn_info_t *)BSSCFG_CUBBY((cfg), (pm)->cfgh_cmn))

/* debug */
#define WL_P2P_TSF(pm, x)
#define WL_P2P_INTR(pm, x)
#define WL_P2P_PRB(pm, x)
#define WL_P2P_AS(pm, x)
#define WL_P2P_TSF_ON(pm)	FALSE
#define WL_P2P_INTR_ON(pm)	FALSE
#define WL_P2P_PRB_ON(pm)	FALSE
#define WL_P2P_AS_ON(pm)	FALSE
#define WL_P2P_TS(wlc)		(wlc->clk ? R_REG(wlc->osh, &wlc->regs->tsf_timerlow) : 0xDEADDAED)

/* d11 SHM BSS block schedule limitations */
#define P2P_NOA_MAX_INT		(((1U << 15) -1) << P2P_UCODE_TIME_SHIFT) /* max NoA interval */
#define P2P_NOA_MAX_NXT		(((1U << 15) -1) << P2P_UCODE_TIME_SHIFT) /* max NoA next */
#define P2P_NOA_MAX_CNT		((1U << 16) - 1)		/* max NoA count */

#define P2P_NOA_MAX_PRD		(1U << 31)

/* internal limitations */
#define P2P_NOA_MIN_OFS		1024	/**< min TSF offset to make sure there is
					 * enough time to plumb the schedule
					 */

/** scb specific info */
typedef struct p2p_scb_cubby {
#if USE_DEF_P2P_IE
	/** Association Request IEs */
	uint16	as_ies_len;
	uint8	*as_ies;
#endif
	void *dummy;
} p2p_scb_cubby_t;

/* scb specific info access accessor */
#define P2P_SCB_CUBBY_LOC(pm, scb) ((p2p_scb_cubby_t **)SCB_CUBBY((scb), (pm)->scbh))
#define P2P_SCB_CUBBY(pm, scb) (*(P2P_SCB_CUBBY_LOC(pm, scb)))

#define WPS_ATID_REQ_DEV_TYPE	0x106a	/* Requested Device Type */

/* feature flags */
#define P2P_GO_NOLEGACY(pm) \
	(((pm)->p2p_data->features & WL_P2P_FEAT_GO_NOLEGACY) == WL_P2P_FEAT_GO_NOLEGACY)
#define P2P_RESTRICT_DEV_RESP(pm) \
	(((pm)->p2p_data->features & WL_P2P_FEAT_RESTRICT_DEV_RESP) == \
	WL_P2P_FEAT_RESTRICT_DEV_RESP)

/* definition to support long listen dwell time under VSDB. unit is ms. */
#define LONG_LISTEN_DWELL_TIME_THRESHOLD			512
#define LONG_LISTEN_BG_SCAN_HOME_TIME				110
#define LONG_LISTEN_BG_SCAN_PASSIVE_TIME			80
#define MARGIN_FROM_ONESHOT_TO_BG_PASSIVE			30

#if defined(WL_NAN_PD_P2P)
#define CMD_FLAG_GET    0x01
#define CMD_FLAG_SET    0x02

typedef int
(p2p_nan_ioc_handler_t)(wlc_p2p_info_t *p2pinfo, void *params,
        uint16 paramlen, void *result, uint16 buflen, bool set);

/* list of "wl p2p_nan" <cmd> handlers  */
typedef struct p2p_nan_ioc_cmd {
	uint16 cmd;
	uint16 flags;    /* set, get, for validation and other */
	uint16 min_len;  /* for ioctl param validaton */
	p2p_nan_ioc_handler_t *handler;
} p2p_nan_ioc_cmd_t;
#endif /* WL_NAN_PD_P2P */

/* local prototypes */

/* module */
static int wlc_p2p_doiovar(void *hdl, uint32 actionid,
	void *p, uint plen, void *a, uint alen, uint vsize, struct wlc_if *wlcif);
static int wlc_p2p_up(void *context);

/* scb cubby */
static int wlc_p2p_scb_init(void *context, struct scb *scb);
static void wlc_p2p_scb_deinit(void *context, struct scb *scb);
#define wlc_p2p_scb_dump NULL

static int wlc_p2p_enab(wlc_p2p_info_t *pm, bool enable);
static void wlc_p2p_da_set(wlc_p2p_info_t *pm);

/* discovery */
#define wlc_p2p_disc_state(pm)	(pm->p2p_data->devcfg != NULL)
static int wlc_p2p_disc_enab(wlc_p2p_info_t *pm, bool enable, int idx);
static int wlc_p2p_state_set(wlc_p2p_info_t *pm, wl_p2p_disc_st_t *state);
static bool wlc_p2p_ensure_disc_state(wlc_p2p_info_t *pm);

/* schedules */
static int wlc_p2p_sched_wl2se(wl_p2p_sched_desc_t *desc, wifi_p2p_noa_desc_t *sedesc, int cnt);
static bool wlc_p2p_sched_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);
static void wlc_p2p_sched_adj(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);
static bool wlc_p2p_sched_renew(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);
static bool wlc_p2p_sched_norm(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);
static int wlc_p2p_sched_noa(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, int sched,
	wifi_p2p_noa_se_t *se, int slen, bool force, bool *update);
static uint32 wlc_p2p_noa_start_dist(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint32 tsf);
static void wlc_p2p_noa_resched(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);

/* iovars */
static int wlc_p2p_ops_get(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wl_p2p_ops_t *buf);
static int wlc_p2p_noa_get(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wl_p2p_sched_t *buf, int blen);
#ifdef WLWFDS
static void wlc_p2p_wfds_free(wlc_p2p_info_t *pm);
static int wlc_p2p_del_wfds_hash(wlc_p2p_info_t *pm, wl_p2p_wfds_hash_t *hash);
static int wlc_p2p_add_wfds_hash(wlc_p2p_info_t *pm, wl_p2p_wfds_hash_t *hash);
#endif /* WLWFDS */

/* h/w */
static void _wlc_p2p_noa_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);
static void wlc_p2p_bcn_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);
static void wlc_p2p_noa_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);

/* PS */
static bool wlc_p2p_ps_allowed(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);

/* callbacks */
static void wlc_p2p_assoc_upd_cb(void *ctx, wlc_mcnx_assoc_upd_data_t *notif_data);
static void wlc_p2p_bss_upd_cb(void *ctx, wlc_mcnx_bss_upd_data_t *notif_data);
static void wlc_p2p_tsf_upd_cb(void *ctx, wlc_mcnx_tsf_upd_data_t *notif_data);
static void wlc_p2p_intr_cb(void *ctx, wlc_mcnx_intr_data_t *notif_data);
static void wlc_p2p_pretbtt_query_cb(void *ctx, bss_pretbtt_query_data_t *notif_data);

static void wlc_p2p_pre_tbtt_cb(void *ctx, wlc_tbtt_ent_data_t *notif_data);
static void wlc_p2p_tbtt_cb(void *ctx, wlc_tbtt_ent_data_t *notif_data);

/* bsscfg cubby */
static int wlc_p2p_info_init(void *ctx, wlc_bsscfg_t *cfg);
static void wlc_p2p_info_deinit(void *ctx, wlc_bsscfg_t *cfg);
#define wlc_p2p_info_dump NULL
static bss_p2p_info_t *wlc_p2p_info_alloc(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg);
static void wlc_p2p_info_free(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p);
#define wlc_p2p_cmn_info_dump NULL

/* bsscfg up/down */
static void wlc_p2p_bss_updn(void *ctx, bsscfg_up_down_event_data_t *evt);

/* tx suppression */
static void wlc_p2p_abs_q_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint8 state);

/* others */
static bool wlc_p2p_other_active(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg);


static int wlc_p2p_vndr_ie_getlen(wlc_info_t *wlc, wlc_bsscfg_t *cfg, uint16 ft, uint32 pktflag);
#ifdef WLPROBRESP_SW
static bool wlc_p2p_recv_process_prbreq_ap(void *handle, wlc_bsscfg_t *cfg,
	wlc_d11rxhdr_t *wrxh, uint8 *plcp, struct dot11_management_header *hdr,
	uint8 *body, int body_len, bool *psendProbeResp);
#endif /* WLPROBRESP_SW */

/* IE mgmt */
static uint wlc_p2p_bcn_calc_ie_len(void *ctx, wlc_iem_calc_data_t *data);
static int wlc_p2p_bcn_write_ie(void *ctx, wlc_iem_build_data_t *data);
static uint wlc_p2p_as_calc_ie_len(void *ctx, wlc_iem_calc_data_t *data);
static int wlc_p2p_as_write_ie(void *ctx, wlc_iem_build_data_t *data);
static uint wlc_p2p_other_calc_ie_len(void *ctx, wlc_iem_calc_data_t *data);
static int wlc_p2p_other_write_ie(void *ctx, wlc_iem_build_data_t *data);

static int wlc_p2p_go_update_chanspec(wlc_info_t *wlc, chanspec_t chanspec, struct wlc_if *wlcif);
static bool wlc_p2p_go_chanspec_is_valid(wlc_info_t *wlc, chanspec_t chanspec);

#if WFA_VER_1_02_TEST
static int wlc_p2p_ver_1_02_attr_test(wlc_p2p_info_t *p2p, wlc_bsscfg_t *cfg);
#endif
static int wlc_p2p_calc_advt_ie_len(wlc_p2p_info_t *pm, int len);
static int wlc_p2p_add_advt_ie(wlc_p2p_info_t *pm, wifi_p2p_ie_t *ie, uint8 *buf, int len);
#if defined(WL_NAN_PD_P2P)
static const p2p_nan_ioc_cmd_t *wlc_p2p_nan_find_cmd_handler(
	const p2p_nan_ioc_cmd_t *cmd_tab, uint16 cmd);
static int
wlc_p2p_nan_config(wlc_p2p_info_t *pm, void *params,
        uint16 paramlen, void *result, uint16 buflen, bool set);
static int
wlc_p2p_nan_del_config(wlc_p2p_info_t *pm, void *params,
        uint16 paramlen, void *result, uint16 buflen, bool set);
static int
wlc_p2p_nan_get_svc_inst_list(wlc_p2p_info_t *pm, void *params, uint16 paramlen,
        void *result, uint16 buflen, bool set);
static void wlc_p2p_window_sched_upd(void *ctx, wlc_nan_sched_upd_data_t *notif_data);
static uint16 getentries_p2p_nan_ioctls(void);
#endif /* WL_NAN_PD_P2P */

static void wlc_p2p_noa_upd_notif(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg);
static bool wlc_p2p_noa_desc_upd_notif(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
		wl_p2p_sched_desc_t *desc);

/* local variables */

#if USE_DEF_P2P_IE
#define WFA_P2P_OUI_INIT	0x50, 0x6f, 0x9a, WFA_OUI_TYPE_P2P	/* WFA OUI + type */

#define WPS_ATID_PRI_DEV_TYPE	0x1054	/**< Primary Device Type */
#define WPS_ATID_SEC_DEV_TYPE	0x1055	/**< Secondary Device Type */

static uint8 def_p2p_ie_in_bcn[] = {
	'a', 'd', 'd', 0,
	0x1, 0x0, 0x0, 0x0,	/* ie count */
	/* vendor ie 0 */
	VNDR_IE_BEACON_FLAG, 0x0, 0x0, 0x0,	/* flags */
	DOT11_MNG_VS_ID,	/* P2P IE */
	13,
	WFA_P2P_OUI_INIT,
	P2P_SEID_DEV_ID,	/* DeviceID SE */
	6, 0,
	0, 0, 0, 0, 0, 0
};

static uint8 def_p2p_ie_in_prbreq[] = {
	'a', 'd', 'd', 0,
	0x1, 0x0, 0x0, 0x0,	/* ie count */
	/* vendor ie 0 */
	VNDR_IE_PRBREQ_FLAG, 0x0, 0x0, 0x0,	/* flags */
	DOT11_MNG_VS_ID,	/* P2P IE */
	14,
	WFA_P2P_OUI_INIT,
	P2P_SEID_P2P_INFO,	/* Capability SE */
	2, 0,
	0, 0,
	P2P_SEID_CHANNEL,	/* Channel SE */
	2, 0,
	0, 0
};

static uint8 def_p2p_ie_in_prbresp_dev[] = {
	'a', 'd', 'd', 0,
	0x1, 0x0, 0x0, 0x0,	/* ie count */
	/* vendor ie 0 */
	VNDR_IE_PRBRSP_FLAG, 0x0, 0x0, 0x0,	/* flags */
	DOT11_MNG_VS_ID,	/* P2P IE */
	9,
	WFA_P2P_OUI_INIT,
	P2P_SEID_P2P_INFO,	/* Capability SE */
	2, 0,
	0, 0
};

static uint8 def_p2p_ie_in_prbresp_go[] = {
	'a', 'd', 'd', 0,
	0x1, 0x0, 0x0, 0x0,	/* ie count */
	/* vendor ie 0 */
	VNDR_IE_PRBRSP_FLAG, 0x0, 0x0, 0x0,	/* flags */
	DOT11_MNG_VS_ID,	/* P2P IE */
	18,
	WFA_P2P_OUI_INIT,
	P2P_SEID_P2P_INFO,	/* Capability SE */
	2, 0,
	0, 0,
	P2P_SEID_DEV_INFO,	/* DeviceInfo SE */
	6, 0,
	0, 0, 0, 0, 0, 0
};

static uint8 def_p2p_ie_in_assocreq[] = {
	'a', 'd', 'd', 0,
	0x1, 0x0, 0x0, 0x0,	/* ie count */
	/* vendor ie 0 */
	VNDR_IE_ASSOCREQ_FLAG, 0x0, 0x0, 0x0,	/* flags */
	DOT11_MNG_VS_ID,	/* P2P IE */
	18,
	WFA_P2P_OUI_INIT,
	P2P_SEID_P2P_INFO,	/* Capability SE */
	2, 0,
	0, 0,
	P2P_SEID_DEV_INFO,	/* DeviceInfo SE */
	6, 0,
	0, 0, 0, 0, 0, 0
};

static uint8 def_wps_ie_in_prbreq[] = {
	'a', 'd', 'd', 0,
	0x1, 0x0, 0x0, 0x0,	/* ie count */
	/* vendor ie 0 */
	VNDR_IE_PRBREQ_FLAG, 0x0, 0x0, 0x0,	/* flags */
	DOT11_MNG_VS_ID,	/* WPS IE */
	40,
	0x00, 0x50, 0xf2, WPS_OUI_TYPE,
	(uint8)(WPS_ATID_REQ_DEV_TYPE>>8), (uint8)WPS_ATID_REQ_DEV_TYPE,	/* Attribute ID */
	0, 8,			/* Length */
	0, 1,			/* Category */
	0, 0, 0, 0,		/* OUI */
	0, 1,			/* Subcategory */
	(uint8)(WPS_ATID_REQ_DEV_TYPE>>8), (uint8)WPS_ATID_REQ_DEV_TYPE,	/* Attribute ID */
	0, 8,			/* Length */
	0, 2,			/* Category */
	0, 0, 0, 0,		/* OUI */
	0, 2,			/* Subcategory */
	(uint8)(WPS_ATID_REQ_DEV_TYPE>>8), (uint8)WPS_ATID_REQ_DEV_TYPE,	/* Attribute ID */
	0, 8,			/* Length */
	0, 3,			/* Category */
	0, 0, 0, 0,		/* OUI */
	0, 3			/* Subcategory */
};

static uint8 def_wps_ie_in_prbresp[] = {
	'a', 'd', 'd', 0,
	0x1, 0x0, 0x0, 0x0,	/* ie count */
	/* vendor ie 0 */
	VNDR_IE_PRBRSP_FLAG, 0x0, 0x0, 0x0,	/* flags */
	DOT11_MNG_VS_ID,	/* WPS IE */
	28,
	0x00, 0x50, 0xf2, WPS_OUI_TYPE,
	(uint8)(WPS_ATID_PRI_DEV_TYPE>>8), (uint8)WPS_ATID_PRI_DEV_TYPE,	/* Attribute ID */
	0, 8,			/* Length */
	0, 22,			/* Category */
	0, 0, 0, 0,		/* OUI */
	0, 22,			/* Subcategory */
	(uint8)(WPS_ATID_SEC_DEV_TYPE>>8), (uint8)WPS_ATID_SEC_DEV_TYPE,	/* Attribute ID */
	0, 8,			/* Length */
	0, 2,			/* Category */
	0, 0, 0, 0,		/* OUI */
	0, 2			/* Subcategory */
};
#endif /* USE_DEF_P2P_IE */

#ifdef WLWFDS
/* Generic WFDS service name hash */
static uint8	def_gen_wfds_hash[] = { 0x6D, 0xB8, 0x71, 0x03, 0x11, 0xF8 };
#define	GEN_WFDS_ADVT_ID	0
#define	GEN_WFDS_NW_CONFIG	0

/** To prevent ROMming shdat issue because of ROMmed functions accessing RAM */
static uint8* BCMRAMFN(get_def_gen_wfds_hash)(void)
{
	return (uint8 *)def_gen_wfds_hash;
}
#endif /* WLWFDS */

#if defined(WL_NAN_PD_P2P)
/*  length of the ioc_cmd_t array  */
#define TLEN(array)  (sizeof(array) / sizeof(*array))

/*  all nan cmd ioctls for all nan submodules  */
static const p2p_nan_ioc_cmd_t p2p_nan_ioctls[] = {
	/* wl p2p_nan config */
	{WL_P2P_NAN_CMD_CONFIG, (CMD_FLAG_GET | CMD_FLAG_SET),
	OFFSETOF(wl_p2p_nan_config_t, ie), wlc_p2p_nan_config
	},
	{WL_P2P_NAN_CMD_DEL_CONFIG, (CMD_FLAG_SET),
	0, wlc_p2p_nan_del_config
	},
	{WL_P2P_NAN_CMD_GET_INSTS, (CMD_FLAG_GET),
	OFFSETOF(wl_nan_svc_inst_list_t, svc) + sizeof(wl_nan_svc_inst_t),
	wlc_p2p_nan_get_svc_inst_list
	}
};
#endif  /* WL_NAN_PD_P2P */

/* This includes the auto generated ROM IOCTL/IOVAR patch handler C source file (if auto patching is
 * enabled). It must be included after the prototypes and declarations above (since the generated
 * source file may reference private constants, types, variables, and functions).
 */
#include <wlc_patch.h>

/** sets the p2p device address according to spec */
static void
wlc_p2p_da_set(wlc_p2p_info_t *pm)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_data_t *pd = pm->p2p_data;

#ifdef WL_NDWDI
	/* For WDI, randomized mac address is locally administered. Use permanent address */
	if (ETHER_IS_LOCALADDR(&wlc->pub->cur_etheraddr)) {
		bcopy(&wlc->perm_etheraddr, &pd->devaddr, ETHER_ADDR_LEN);
	} else
#endif
	{
		/* copy the current ethernet address and set locally admin bit */
		bcopy(&wlc->pub->cur_etheraddr, &pd->devaddr, ETHER_ADDR_LEN);
		ETHER_SET_LOCALADDR(&pd->devaddr);
	}
}

/** Vendor added Vendor Specific IEs */
typedef struct {
	wlc_iem_calc_fn_t calc;
	wlc_iem_build_fn_t build;
} iem_fns_t;

static const iem_fns_t BCMATTACHDATA(fst2iemfns)[] = {
	/* FC_ASSOC_REQ 0 */ {wlc_p2p_other_calc_ie_len, wlc_p2p_other_write_ie},
	/* FC_ASSOC_RESP 1 */ {wlc_p2p_as_calc_ie_len, wlc_p2p_as_write_ie},
	/* FC_REASSOC_REQ 2 */ {wlc_p2p_other_calc_ie_len, wlc_p2p_other_write_ie},
	/* FC_REASSOC_RESP 3 */ {wlc_p2p_as_calc_ie_len, wlc_p2p_as_write_ie},
	/* FC_PROBE_REQ 4 */ {wlc_p2p_other_calc_ie_len, wlc_p2p_other_write_ie},
	/* FC_PROBE_RESP 5 */ {wlc_p2p_bcn_calc_ie_len, wlc_p2p_bcn_write_ie},
	{NULL, NULL},
	{NULL, NULL},
	/* FC_BEACON 8 */ {wlc_p2p_bcn_calc_ie_len, wlc_p2p_bcn_write_ie},
	{NULL, NULL},
	/* FC_DISASSOC 10 */ {wlc_p2p_other_calc_ie_len, wlc_p2p_other_write_ie},
	/* FC_AUTH 11 */ {wlc_p2p_other_calc_ie_len, wlc_p2p_other_write_ie},
	/* FC_DEAUTH 12 */ {wlc_p2p_other_calc_ie_len, wlc_p2p_other_write_ie},
};

#define FST2IEMFNS(fst) (const iem_fns_t *)((fst) < ARRAYSIZE(fst2iemfns) ? &fst2iemfns[fst] : NULL)

/** module attach/detach */
wlc_p2p_info_t *
BCMATTACHFN(wlc_p2p_attach)(wlc_info_t *wlc)
{
	wlc_p2p_info_t *pm;
	wlc_p2p_data_t *pd;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;
	uint16 fst;
	bcm_notif_module_t	*notif;

	/* sanity check */
	ASSERT(wlc != NULL);

	/* module instance */
	if ((pm = (wlc_p2p_info_t *)MALLOCZ(wlc->osh, sizeof(wlc_p2p_info_t))) == NULL) {
		WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes\n",
			wlc->pub->unit, __FUNCTION__, MALLOCED(wlc->osh)));
		goto fail;
	}

	/* module shared states */
	pd = (wlc_p2p_data_t*) obj_registry_get(wlc->objr, OBJR_P2P_DATA_PTR);
	if (pd == NULL) {
		if ((pd = (wlc_p2p_data_t *)MALLOCZ(wlc->osh, sizeof(wlc_p2p_data_t))) == NULL) {
			WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes\n",
				wlc->pub->unit, __FUNCTION__, MALLOCED(wlc->osh)));
			goto fail;
		}
		obj_registry_set(wlc->objr, OBJR_P2P_DATA_PTR, pd);
	}
	(void)obj_registry_ref(wlc->objr, OBJR_P2P_DATA_PTR);
	pm->p2p_data = pd;

	/* initialize module info */
	pm->wlc = wlc;
	/* default P2P wildcard SSID */
	pd->ssid.SSID_len = snprintf((char *)pd->ssid.SSID, sizeof(pd->ssid.SSID), "DIRECT-");
	pd->dialog = 1;
	pd->prblen = (uint16)~0;

	/* register callbacks with p2p uc module */
	ASSERT(mcnx != NULL);

	notif = wlc->notif;
	/* create notification list for multi desc upd. */
	if (bcm_notif_create_list(notif, &pm->cxnoa_upd_hdl) != BCME_OK) {
		WL_ERROR(("wl%d: %s: multi desc upd bcm_notif_create_list() failed\n",
			wlc->pub->unit, __FUNCTION__));
		goto fail;
	}
	if (bcm_notif_create_list(notif, &pm->cxnoa_desc_hdl) != BCME_OK) {
		WL_ERROR(("wl%d: %s: multi desc upd bcm_notif_create_list() failed\n",
			wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

	if (wlc_mcnx_assoc_upd_register(mcnx, wlc_p2p_assoc_upd_cb, pm) != BCME_OK) {
		WL_ERROR(("wl%d: %s: wlc_p2p_assoc_upd_register() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}
	if (wlc_mcnx_bss_upd_register(mcnx, wlc_p2p_bss_upd_cb, pm) != BCME_OK) {
		WL_ERROR(("wl%d: %s: wlc_p2p_bss_upd_register() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}
	if (wlc_mcnx_tsf_upd_register(mcnx, wlc_p2p_tsf_upd_cb, pm) != BCME_OK) {
		WL_ERROR(("wl%d: %s: wlc_p2p_tsf_upd_register() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}
	if (wlc_mcnx_intr_register(mcnx, wlc_p2p_intr_cb, pm) != BCME_OK) {
		WL_ERROR(("wl%d: %s: wlc_p2p_intr_register() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

	/* register pretbtt query callback */
	if (wlc_bss_pretbtt_query_register(wlc, wlc_p2p_pretbtt_query_cb, pm) != BCME_OK) {
		WL_ERROR(("wl%d: %s: wlc_bss_pretbtt_query_register() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

	/* reserve cubby in the bsscfg container for per-bsscfg private data (for Client/GO) */
	if ((pm->cfgh = wlc_bsscfg_cubby_reserve(wlc, sizeof(bss_p2p_info_t *),
		wlc_p2p_info_init, wlc_p2p_info_deinit, wlc_p2p_info_dump,
		pm)) < 0) {
		WL_ERROR(("wl%d: %s: wlc_bsscfg_cubby_reserve() failed\n",
			wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

	/* reserve cubby in the bsscfg container for per-bsscfg private data (for Client/GO/Dev) */
	if ((pm->cfgh_cmn = wlc_bsscfg_cubby_reserve(wlc, sizeof(bss_p2p_cmn_info_t),
	                NULL, NULL, wlc_p2p_cmn_info_dump,
	                pm)) < 0) {
		WL_ERROR(("wl%d: %s: wlc_bsscfg_cubby_reserve() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

	/* reserve cubby in the scb container for per-scb private data */
	if ((pm->scbh = wlc_scb_cubby_reserve(wlc, sizeof(p2p_scb_cubby_t *),
	                wlc_p2p_scb_init, wlc_p2p_scb_deinit, wlc_p2p_scb_dump,
	                (void *)pm)) < 0) {
		WL_ERROR(("wl%d: %s: wlc_scb_cubby_reserve() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

	/* bsscfg up/down callback */
	if (wlc_bsscfg_updown_register(wlc, wlc_p2p_bss_updn, pm) != BCME_OK) {
		WL_ERROR(("wl%d: %s: wlc_bsscfg_updown_register() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

	/* register IE mgmt callback */
	for (fst = 0; fst < ARRAYSIZE(fst2iemfns); fst ++) {
		const iem_fns_t *fns;

		fns = FST2IEMFNS(fst);
		ASSERT(fns != NULL);

		if (fns->calc == NULL || fns->build == NULL)
			continue;

		if (wlc_iem_vs_add_build_fn(wlc->iemi, FST2FT(fst), WLC_IEM_VS_IE_PRIO_P2P,
		                            fns->calc, fns->build, pm) != BCME_OK) {
			WL_ERROR(("wl%d: %s wlc_iem_vs_add_build_fn failed, ft 0x%04x\n",
			          wlc->pub->unit, __FUNCTION__, FST2FT(fst)));
			goto fail;
		}
	}

	/* register module up/down, watchdog, and iovar callbacks */
	if (wlc_module_register(wlc->pub, p2p_iovars, "p2p", pm, wlc_p2p_doiovar,
	                        NULL, wlc_p2p_up, NULL)) {
		WL_ERROR(("wl%d: %s: wlc_module_register() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}


	/* ENABLE P2P by default */
	if (wlc_p2p_cap(pm) &&
	    wlc_p2p_enab(pm, TRUE) != BCME_OK) {
		WL_ERROR(("wl%d: %s: wlc_p2p_enab() failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

	/* Restrict device response on the channel which STA is associated with AP */
	pm->p2p_data->features |= WL_P2P_FEAT_RESTRICT_DEV_RESP;

	wlc_probresp_register(wlc->mprobresp, pm, wlc_p2p_recv_process_prbreq_ap, TRUE);

#if defined(WLWFDS) && !defined(WLWFDS_DISABLED)
	wlc->pub->_wfds = TRUE;
#endif

	return pm;

fail:
	/* error handling */
	MODULE_DETACH(pm, wlc_p2p_detach);
	return NULL;
}

void
BCMATTACHFN(wlc_p2p_detach)(wlc_p2p_info_t *pm)
{
	wlc_info_t *wlc;
	wlc_mcnx_info_t *mcnx;
	wlc_p2p_data_t *pd;

	if (pm == NULL)
		return;

	wlc = pm->wlc;
	ASSERT(wlc != NULL);

	mcnx = wlc->mcnx;
	ASSERT(mcnx != NULL);

	wlc_probresp_unregister(wlc->mprobresp, pm);
	wlc_module_unregister(wlc->pub, "p2p", pm);

	if (pm->cxnoa_upd_hdl != NULL)
		bcm_notif_delete_list(&pm->cxnoa_upd_hdl);
	if (pm->cxnoa_desc_hdl != NULL)
		bcm_notif_delete_list(&pm->cxnoa_desc_hdl);

#ifdef WLWFDS
	wlc_p2p_wfds_free(pm);
#endif /* WLWFDS */

	wlc_bsscfg_updown_unregister(wlc, wlc_p2p_bss_updn, pm);

	wlc_bss_pretbtt_query_unregister(wlc, wlc_p2p_pretbtt_query_cb, pm);
	wlc_mcnx_assoc_upd_unregister(mcnx, wlc_p2p_assoc_upd_cb, pm);
	wlc_mcnx_bss_upd_unregister(mcnx, wlc_p2p_bss_upd_cb, pm);
	wlc_mcnx_tsf_upd_unregister(mcnx, wlc_p2p_tsf_upd_cb, pm);
	wlc_mcnx_intr_unregister(mcnx, wlc_p2p_intr_cb, pm);

	pd = pm->p2p_data;

	if (pd && (obj_registry_unref(wlc->objr, OBJR_P2P_DATA_PTR) == 0)) {
		obj_registry_set(wlc->objr, OBJR_P2P_DATA_PTR, NULL);
		MFREE(wlc->osh, pd, sizeof(wlc_p2p_data_t));
	}
	pm->p2p_data = NULL;
	MFREE(wlc->osh, pm, sizeof(wlc_p2p_info_t));
}

/* feature enable/disable */
static int
wlc_p2p_enab(wlc_p2p_info_t *pm, bool enable)
{
	wlc_info_t *wlc = pm->wlc;

	if (enable) {
		if (!MCNX_ENAB(wlc->pub)) {
			WL_ERROR(("wl%d: mcnx not enabled, unable to enable p2p\n",
			          wlc->pub->unit));
			return BCME_ERROR;
		}
	}

	wlc->pub->_p2p = enable;


	return BCME_OK;
}

/** h/w capable of p2p? */
bool
wlc_p2p_cap(wlc_p2p_info_t *pm)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;
	wlc_hw_info_t *hw = wlc->hw;

	if (mcnx == NULL || hw == NULL)
		return FALSE;

	return wlc_mcnx_cap(mcnx) && wlc_bmac_p2p_cap(hw);
}

/** scb cubby */
static int
wlc_p2p_scb_init(void *context, struct scb *scb)
{
	BCM_REFERENCE(context);
	BCM_REFERENCE(scb);

	return BCME_OK;
}

static int
_wlc_p2p_scb_init(wlc_p2p_info_t *pm, struct scb *scb)
{
	wlc_info_t *wlc = pm->wlc;
	p2p_scb_cubby_t **pcubby = P2P_SCB_CUBBY_LOC(pm, scb);
	p2p_scb_cubby_t *cubby;

	(void)wlc;

	if ((cubby = MALLOCZ(wlc->osh, sizeof(p2p_scb_cubby_t))) == NULL) {
		WL_ERROR((WLC_MALLOC_ERR, WLCWLUNIT(wlc), __FUNCTION__,
			(int)sizeof(p2p_scb_cubby_t), MALLOCED(wlc->osh)));
		return BCME_NOMEM;
	}
	*pcubby = cubby;

	return BCME_OK;
}

static void
wlc_p2p_scb_deinit(void *context, struct scb *scb)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)context;
	wlc_info_t *wlc = pm->wlc;
	p2p_scb_cubby_t **pcubby = P2P_SCB_CUBBY_LOC(pm, scb);
	p2p_scb_cubby_t *cubby;

	(void)wlc;

	cubby = *pcubby;
	if (cubby != NULL) {
#if USE_DEF_P2P_IE
		if (cubby->as_ies != NULL)
			MFREE(wlc->osh, cubby->as_ies, cubby->as_ies_len);
#endif
		MFREE(wlc->osh, cubby, sizeof(p2p_scb_cubby_t));
	}
	*pcubby = NULL;
}

/** bss_p2p_info_t alloc/free */
static bss_p2p_info_t *
wlc_p2p_info_alloc(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;

	(void)wlc;

	ASSERT(cfg != NULL);

	if ((p2p = MALLOCZ(wlc->osh, sizeof(bss_p2p_info_t))) == NULL) {
		WL_ERROR((WLC_BSS_MALLOC_ERR, WLCWLUNIT(wlc), WLC_BSSCFG_IDX(cfg),
			__FUNCTION__, (int)sizeof(bss_p2p_info_t), MALLOCED(wlc->osh)));
		return NULL;
	}
	p2p->bsscfg = cfg;

	return p2p;
}

static void
wlc_p2p_info_free(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	uint i;

	(void)wlc;

	ASSERT(p2p != NULL);

	for (i = 0; i < WLC_P2P_MAX_SCHED; i ++) {
		if (p2p->sched[i].desc == NULL)
			continue;
		MFREE(wlc->osh, p2p->sched[i].desc,
		      p2p->sched[i].cnt * sizeof(wl_p2p_sched_desc_t));
	}

	MFREE(wlc->osh, p2p, sizeof(bss_p2p_info_t));
}

/** bsscfg cubby */
static int
wlc_p2p_info_init(void *ctx, wlc_bsscfg_t *cfg)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t **pp2p = P2P_BSSCFG_CUBBY_LOC(pm, cfg);
	bss_p2p_info_t *p2p;
	int err = BCME_OK;
	uint8 gmode = GMODE_AUTO;

	if (!BSS_P2P_ENAB(wlc, cfg)) {
		return BCME_OK;
	}

	if (!BSS_P2P_DISC_ENAB(wlc, cfg)) {
		if (wlc_tbtt_ent_fn_add(wlc->tbtt, cfg,
				wlc_p2p_pre_tbtt_cb, wlc_p2p_tbtt_cb, pm) != BCME_OK) {
			WL_ERROR(("wl%d: %s: wlc_tbtt_ent_fn_add() failed\n",
			          wlc->pub->unit, __FUNCTION__));
			return BCME_NORESOURCE;
		}

		if ((p2p = wlc_p2p_info_alloc(pm, cfg)) == NULL) {
			WL_ERROR(("wl%d: %s: wlc_p2p_info_alloc() failed\n",
			          wlc->pub->unit, __FUNCTION__));
			err = BCME_NOMEM;
			goto fail;
		}
		*pp2p = p2p;
	}

	/* make sure gmode is not GMODE_LEGACY_B */
	if (!IS_SINGLEBAND_5G(wlc->deviceid)) {
		gmode = wlc->bandstate[BAND_2G_INDEX]->gmode;
	}
	if (gmode == GMODE_LEGACY_B) {
		WL_ERROR(("wl%d: %s: gmode cannot be GMODE_LEGACY_B\n",
		          wlc->pub->unit, __FUNCTION__));
		err = BCME_BADRATESET;
		goto fail;
	}

	if ((err = wlc_bsscfg_rateset_init(wlc, cfg, WLC_RATES_OFDM,
	                                   WL_BW_CAP_40MHZ(wlc->band->bw_cap) ?
	                                   CHSPEC_WLC_BW(wlc->home_chanspec) : 0,
	                                   wlc_get_mcsallow(wlc, cfg))) != BCME_OK) {
		WL_ERROR(("wl%d: %s: failed rateset init\n", wlc->pub->unit, __FUNCTION__));
		goto fail;
	}

	if (BSS_P2P_DISC_ENAB(wlc, cfg)) {
		bcopy(&cfg->cur_etheraddr, &cfg->BSSID, ETHER_ADDR_LEN);
		wlc_bsscfg_set_current_bss_chan(cfg, wlc->home_chanspec);
		cfg->current_bss->bss_type = DOT11_BSSTYPE_INDEPENDENT;
	}

	return BCME_OK;

fail:
	wlc_p2p_info_deinit(pm, cfg);
	return err;
}

static void
wlc_p2p_info_deinit(void *ctx, wlc_bsscfg_t *cfg)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t **pp2p = P2P_BSSCFG_CUBBY_LOC(pm, cfg);
	bss_p2p_info_t *p2p = *pp2p;

	if (!P2P_IF(wlc, cfg))
		return;

	wlc_tbtt_ent_fn_del(wlc->tbtt, cfg, wlc_p2p_pre_tbtt_cb, wlc_p2p_tbtt_cb, pm);

	if (p2p == NULL)
		return;

	wlc_p2p_info_free(pm, p2p);
	*pp2p = NULL;
}

/** bsscfg up/down */
static void
wlc_p2p_bss_updn(void *ctx, bsscfg_up_down_event_data_t *evt)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;

	ASSERT(evt != NULL);

	cfg = evt->bsscfg;
	ASSERT(cfg != NULL);

	if (!P2P_IF(wlc, cfg))
		return;

	if (evt->up) {
		/* The intention here is to enable:
		 * M_P2P_I_PRE_TBTT
		 * M_P2P_I_CTW_END
		 * M_P2P_I_ABS
		 * M_P2P_I_PRS
		 */
		wlc_bmac_enable_tbtt(wlc->hw, TBTT_P2P_MASK, TBTT_P2P_MASK);
	}
	/* Disable tbtt, if this was the last P2P bsscfg */
	else if (!wlc_p2p_other_active(pm, cfg)) {
		wlc_bmac_enable_tbtt(wlc->hw, TBTT_P2P_MASK, 0);
	}
}

/** p2p related s/w or h/w initializations */
static int
wlc_p2p_up(void *context)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)context;
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_info_t *wlc = pm->wlc;

	if (!P2P_ENAB(wlc->pub))
		return BCME_OK;

	/* device address */
	wlc_p2p_da_set(pm);

#if USE_DEF_P2P_IE
	/* init device address in default P2P IEs */
	bcopy(&pd->devaddr, &def_p2p_ie_in_bcn[21], ETHER_ADDR_LEN);
	bcopy(&pd->devaddr, &def_p2p_ie_in_prbresp_go[26], ETHER_ADDR_LEN);
	bcopy(&pd->devaddr, &def_p2p_ie_in_assocreq[26], ETHER_ADDR_LEN);
#endif

	/* probe request buffer length in SHM,
	 * ucode inspects the probe request frame for p2p IE up to this length.
	 */
	pd->prblen = wlc_read_shm(wlc, M_SHM_BYT_CNT(wlc));

	return BCME_OK;
}

/** enable/disable discovery */
static int
wlc_p2p_disc_enab(wlc_p2p_info_t *pm, bool start, int alloc_idx)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_bsscfg_t *bsscfg = NULL;
	bool state = wlc_p2p_disc_state(pm);
	int err = BCME_OK;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;

	if (start) {
		int idx;
		uint32 flags = WLC_BSSCFG_P2P_DISC | WLC_BSSCFG_NOBCMC;
		wlc_bsscfg_type_t type = {BSSCFG_TYPE_P2P, BSSCFG_P2P_DISC};

#ifndef IO80211P2P
		if (!WLEXTSTA_ENAB(wlc->pub)) {
			flags |= WLC_BSSCFG_NOIF;
		}
#endif

		/* already in discovery state */
		if (state) {
			WL_P2P(("wl%d: it is already in DISC state\n", wlc->pub->unit));
			return BCME_OK;
		}

		/* bsscfg with the Device Address exists */
		if (wlc_bsscfg_find_by_unique_hwaddr(wlc, &pd->devaddr) != NULL) {
			WL_ERROR(("wl%d: MAC address is in use\n", wlc->pub->unit));
			return BCME_BUSY;
		}

		/* allocate bsscfg */
		if ((alloc_idx == -1) && (idx = wlc_bsscfg_get_free_idx(wlc)) == -1) {
			WL_ERROR(("wl%d: no free bsscfg\n", wlc->pub->unit));
			return BCME_NORESOURCE;
		}
		else if ((bsscfg = wlc_bsscfg_alloc(wlc,
				(alloc_idx == -1)?idx:alloc_idx, &type, flags,
		                &pd->devaddr)) == NULL) {
			WL_ERROR(("wl%d: cannot create bsscfg\n", wlc->pub->unit));
			return BCME_NOMEM;
		}
		else if (wlc_bsscfg_init(wlc, bsscfg) != BCME_OK) {
			WL_ERROR(("wl%d: cannot init bsscfg\n", wlc->pub->unit));
			err = BCME_ERROR;
			goto free;
		}

		pd->devcfg = bsscfg;

		/* use P2P wildcard SSID */
		wlc_bsscfg_SSID_set(bsscfg, pd->ssid.SSID, pd->ssid.SSID_len);

#if USE_DEF_P2P_IE
		if (pd->insert_def_p2pie) {
			/* default WPS IE??? */
			wlc_iovar_op(wlc, "vndr_ie", NULL, 0, def_wps_ie_in_prbreq,
			             sizeof(def_wps_ie_in_prbreq), IOV_SET, bsscfg->wlcif);
			wlc_iovar_op(wlc, "vndr_ie", NULL, 0, def_wps_ie_in_prbresp,
			             sizeof(def_wps_ie_in_prbresp), IOV_SET, bsscfg->wlcif);
			/* default P2P IE??? */
			wlc_iovar_op(wlc, "vndr_ie", NULL, 0, def_p2p_ie_in_prbreq,
			             sizeof(def_p2p_ie_in_prbreq), IOV_SET, bsscfg->wlcif);
			wlc_iovar_op(wlc, "vndr_ie", NULL, 0, def_p2p_ie_in_prbresp_dev,
			             sizeof(def_p2p_ie_in_prbresp_dev), IOV_SET, bsscfg->wlcif);
		}
#endif

		wlc_mcnx_ra_set(mcnx, bsscfg);
	}
	else {
		/* not in discovery state */
		if (!state) {
			WL_P2P(("wl%d: it is not in DISC state\n", wlc->pub->unit));
			return BCME_OK;
		}

		bsscfg = pd->devcfg;
		ASSERT(bsscfg != NULL);
#ifdef WLRSDB
		/* RSDB: Update wlc from bsscfg */
		if (bsscfg && RSDB_ENAB(wlc->pub)) {
			wlc = bsscfg->wlc;
			mcnx = wlc->mcnx;
		}
#endif

		wlc_scan_abort_ex(wlc->scan, bsscfg, WLC_E_STATUS_ABORT);

		wlc_mcnx_ra_unset(mcnx, bsscfg);

	free:
		/* free bsscfg + error handling */
		if (bsscfg != NULL)
			wlc_bsscfg_free(wlc, bsscfg);
		pd->devcfg = NULL;
	}

	pd->state = WL_P2P_DISC_ST_SCAN;

	return err;
}

static void
wlc_p2p_listen_complete(void * arg, int status, wlc_bsscfg_t *bsscfg)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)arg;
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_info_t *wlc = pm->wlc;

	ASSERT(bsscfg != NULL);
	ASSERT(bsscfg == pd->devcfg);
	/* ASSERT(pm->devcfg == cfg); */

	/* for win8 keep listening until stop req from iovar */
	{
		/* receive probe request not required now */
		wlc_enable_probe_req(wlc, PROBE_REQ_PROBRESP_P2P_MASK, 0);

		pd->state = WL_P2P_DISC_ST_SCAN;
	}

	/* Send the notification to the host */
	wlc_bss_mac_event(wlc, bsscfg, WLC_E_P2P_DISC_LISTEN_COMPLETE, NULL, status, 0, 0, NULL, 0);
}

static bool
wlc_p2p_go_chanspec_is_valid(wlc_info_t *wlc, chanspec_t chanspec)
{
	/* use default_bss->chanspec which should have been validated */
	if (chanspec == 0)
		return TRUE;

	/* validate chanspec */
	if (wf_chspec_malformed(chanspec) ||
	    !wlc_valid_chanspec_db(wlc->cmi, chanspec)) {
		return FALSE;
	}
	/* If mchan not enabled, don't allow GO to start on different channel */
	if (wlc->pub->associated &&
#ifdef WLMCHAN
	    !MCHAN_ENAB(wlc->pub) &&
#endif
	    chanspec != wlc->home_chanspec) {
		return FALSE;
	}

	return TRUE;
}

static int
wlc_p2p_state_set(wlc_p2p_info_t *pm, wl_p2p_disc_st_t *state)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_data_t *pd = pm->p2p_data;

	/* it must be in discovery mode */
	if (!wlc_p2p_ensure_disc_state(pm)) {
		WL_ERROR(("wl%d: it is not in DISC state\n", wlc->pub->unit));
		return BCME_ERROR;
	}

	wlc_scan_abort_ex(wlc->scan, pd->devcfg, WLC_E_STATUS_ABORT);

	/* use the scan engine to handle any off home channel activities */
	if ((pd->state = state->state) == WL_P2P_DISC_ST_LISTEN) {
		struct ether_addr bssid;
		wlc_ssid_t ssid;
		wlc_bsscfg_t *cfg;
		uint16 dwell;
		int ret, passive_time;
		int home_time = 0;
		int active_time = -1;
		int chanspec_num = 1;
		chanspec_t chanspec;
		chanspec_t *chanspec_list = &chanspec;

		chanspec = load16_ua((uint8 *)&state->chspec);
		if (chanspec == 0)
			chanspec = wlc->home_chanspec;
		if (wf_chspec_malformed(chanspec) ||
		    !wlc_valid_chanspec_db(wlc->cmi, chanspec))
			return BCME_BADCHAN;

		cfg = pd->devcfg;
		ASSERT(cfg != NULL);
		ASSERT(cfg->current_bss != NULL);

		wlc_bsscfg_set_current_bss_chan(cfg, chanspec);

		bzero(&bssid, sizeof(bssid));
		bzero(&ssid, sizeof(ssid));

		dwell = load16_ua((uint8 *)&state->dwell);
		passive_time = dwell;

		/* receive probe request required */
		wlc_enable_probe_req(wlc, PROBE_REQ_PROBRESP_P2P_MASK, PROBE_REQ_PROBRESP_P2P_MASK);

		/* update scan parameters to support long listen dwell time under VSDB
		 * if STA/AP is active and the requested listen dwell time is bigger than threshold,
		 * replace one shot long passive scan to bg scan with passive scan on same channel.
		 */
		if (STA_ACTIVE(wlc) && (dwell > LONG_LISTEN_DWELL_TIME_THRESHOLD)) {
			chanspec_num = (dwell) / (LONG_LISTEN_BG_SCAN_PASSIVE_TIME +
			                          LONG_LISTEN_BG_SCAN_HOME_TIME +
			                          (MARGIN_FROM_ONESHOT_TO_BG_PASSIVE));

			if (chanspec_num > MAXCHANNEL)
				chanspec_num = MAXCHANNEL;

			chanspec_list = (chanspec_t *)MALLOC(wlc->osh,
			                          (sizeof(chanspec_t) * chanspec_num));
			if (chanspec_list) {
				int i, scan_home_away_time;

				for (i = 0; i < chanspec_num; i++) {
					chanspec_list[i] = chanspec;
				}

				passive_time = LONG_LISTEN_BG_SCAN_PASSIVE_TIME;
				home_time = LONG_LISTEN_BG_SCAN_HOME_TIME;

				scan_home_away_time = 0;
				wlc_iovar_op(wlc, "scan_home_away_time", NULL, 0,
					&scan_home_away_time, sizeof(scan_home_away_time),
					IOV_GET, cfg->wlcif);
				if (!scan_home_away_time)
					scan_home_away_time = WLC_SCAN_AWAY_LIMIT;
				active_time = scan_home_away_time;

				WL_P2P(("%s: chanspec_num %d, home_time %d, passive_time %d\n",
					__FUNCTION__, chanspec_num, home_time, passive_time));
			}
		}

		ret = wlc_scan_request_ex(wlc, DOT11_BSSTYPE_ANY, &bssid, 1, &ssid,
			DOT11_SCANTYPE_PASSIVE, -1,
			active_time, passive_time, home_time,
			chanspec_list, chanspec_num, 0,
			FALSE,
			wlc_p2p_listen_complete, pm,
			WLC_ACTION_SCAN, FALSE, cfg, NULL, NULL);

		if (chanspec_list && (chanspec_list != &chanspec))
			MFREE(wlc->osh, chanspec_list, (sizeof(chanspec_t) * chanspec_num));

		return ret;
	}

	return BCME_OK;
}

static int
wlc_p2p_go_update_chanspec(wlc_info_t *wlc, chanspec_t chanspec, struct wlc_if *wlcif)
{
	uint v = chanspec;

	/* use default_bss->chanspec */
	if (chanspec == 0)
		return BCME_OK;

	/* configure the GO channel via default_bss->chanspec */
	return wlc_iovar_op(wlc, "chanspec", NULL, 0, &v, sizeof(v), IOV_SET, wlcif);
}

/** a callback fn invoked by the transmit path after sending a NoA schedule */
static void
wlc_p2p_send_noa_complete(wlc_info_t *wlc, uint txs, void *cfgid)
{
	wlc_bsscfg_t *cfg = wlc_bsscfg_find_by_ID(wlc, (uint16)(uintptr)cfgid);
	bss_p2p_info_t *p2p;

	if (cfg == NULL) {
		WL_ERROR(("wl%d: wlc_p2p_send_noa_complete: unable to find bsscfg by ID %p\n",
		          wlc->pub->unit, OSL_OBFUSCATE_BUF(cfgid)));
		return;
	}

	p2p = BSS_P2P_INFO(wlc->p2p, cfg);
	ASSERT(p2p != NULL);

	if (p2p->send_noa_cb == NULL)
		return;
	(p2p->send_noa_cb)(wlc, txs, p2p->send_noa_cb_arg);

	wlc_update_pmstate(cfg, txs);
}

/** handle P2P related iovars */
static int
wlc_p2p_doiovar(void *context, uint32 actionid,
	void *p, uint plen, void *a, uint alen, uint vsize, struct wlc_if *wlcif)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)context;
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *bsscfg;
	int32 int_val = 0;
	int err = BCME_OK;
#if defined(WL_NAN_PD_P2P)
	wl_p2p_nan_ioc_t *p2pnanioc; /* p2p nan ioc header */
#endif /* WL_NAN_PD_P2P */

	BCM_REFERENCE(vsize);

	bsscfg = wlc_bsscfg_find_by_wlcif(wlc, wlcif);
	ASSERT(bsscfg != NULL);

	if (plen >= (int)sizeof(int_val))
		bcopy(p, &int_val, sizeof(int_val));

	/* all iovars require p2p being enabled */
	switch (actionid) {
	case IOV_GVAL(IOV_P2P):
	case IOV_SVAL(IOV_P2P):
		break;
	default:
		if (P2P_ENAB(wlc->pub))
			break;
		return BCME_ERROR;
	}

	switch (actionid) {
	case IOV_GVAL(IOV_P2P):
		*((uint32*)a) = wlc->pub->_p2p;
		break;
	case IOV_SVAL(IOV_P2P):
		err = wlc_p2p_enab(pm, int_val != 0);
		break;
	case IOV_GVAL(IOV_P2P_SSID):
		if ((size_t) alen < sizeof(wlc_ssid_t)) {
			err = BCME_BUFTOOSHORT;
			break;
		}
		bcopy(&pm->p2p_data->ssid, a, sizeof(wlc_ssid_t));
		break;

	case IOV_SVAL(IOV_P2P_SSID):
		if (load32_ua((uint8 *)&((wlc_ssid_t *)p)->SSID_len) > DOT11_MAX_SSID_LEN) {
			err = BCME_BADARG;
			break;
		}
		bcopy(p, &pd->ssid, sizeof(wlc_ssid_t));
		if (wlc_p2p_disc_state(pm))
			wlc_bsscfg_SSID_set(pd->devcfg, pd->ssid.SSID, pd->ssid.SSID_len);
		break;

	case IOV_SVAL(IOV_P2P_DISC):
		err = wlc_p2p_disc_enab(pm, int_val != 0, -1);
		break;

	case IOV_SVAL(IOV_P2P_STATE):
		err = wlc_p2p_state_set(pm, (wl_p2p_disc_st_t *)p);
		break;

	case IOV_SVAL(IOV_P2P_SCAN): {
		wl_p2p_scan_t *t = (wl_p2p_scan_t *)p;

		if (!wlc_p2p_ensure_disc_state(pm)) {
			WL_ERROR(("wl%d: it is not in DISC state\n", wlc->pub->unit));
			err = BCME_ERROR;
			break;
		}

		/* Clear flag and set it in wlc_p2p_fixup_SSID */
		pd->flags &= ~P2P_FLAG_SCAN_ALL;

		switch (t->type) {
		case 'S':
			err = wlc_ioctl(wlc, WLC_SCAN, (void *)(t + 1), plen - sizeof(*t),
			                pd->devcfg->wlcif);
			break;
		case 'E':
			err = wlc_iovar_op(wlc, "escan", NULL, 0,
			                   (void *)(t + 1), plen - sizeof(*t),
			                   IOV_SET, pd->devcfg->wlcif);
			break;
		default:
			err = BCME_BADARG;
			break;
		}
		break;
	}

	case IOV_GVAL(IOV_P2P_DEV):
		if (!wlc_p2p_disc_state(pm)) {
			WL_ERROR(("wl%d: it is not in DISC state\n",
			          wlc->pub->unit));
			err = BCME_ERROR;
			break;
		}
		*((uint32*)a) = WLC_BSSCFG_IDX(pd->devcfg);
		break;

	case IOV_SVAL(IOV_P2P_IFADD): {
		struct ether_addr *ea = &((wl_p2p_if_t *)p)->addr;
		uint8 type_parm = ((wl_p2p_if_t *)p)->type;
		bool go = type_parm != 0;
		int idx;
		uint32 flags = WLC_BSSCFG_TX_SUPR_ENAB;
		chanspec_t chanspec = load16_ua((uint8 *)&((wl_p2p_if_t *)p)->chspec);
		wlc_bsscfg_type_t type = {BSSCFG_TYPE_P2P, BSSCFG_SUBTYPE_NONE};

		/* validate chanspec */
		if (go && !wlc_p2p_go_chanspec_is_valid(wlc, chanspec)) {
			err = BCME_BADCHAN;
			WL_ERROR(("%s(): GO IOV_P2P_IFADD with bad chanspec = 0x%x\n",
			          __FUNCTION__, chanspec));
			break;
		}

		/* bsscfg with the given MAC address exists */
		if (wlc_bsscfg_find_by_unique_hwaddr(wlc, ea) != NULL) {
			err = BCME_BADARG;
			break;
		}

		if (type_parm == WL_P2P_IF_DYNBCN_GO)
			flags |= WLC_BSSCFG_DYNBCN;

		if (go) {

			/* for p2p operation, may start GO on radar channel so
			 * should have AP_NORADAR_CHAN rules in place.
			 */
			flags |= WLC_BSSCFG_AP_NORADAR_CHAN;
			type.subtype = BSSCFG_P2P_GO;
		}
		else {
			type.subtype = BSSCFG_P2P_GC;
		}

		/* allocate bsscfg */
		if ((idx = wlc_bsscfg_get_free_idx(wlc)) == -1) {
			WL_ERROR(("wl%d: no free bsscfg\n",
			          wlc->pub->unit));
			err = BCME_ERROR;
			break;
		}
		else if ((bsscfg = wlc_bsscfg_alloc(wlc, idx, &type, flags, ea)) == NULL) {
			WL_ERROR(("wl%d: cannot create bsscfg\n",
			          wlc->pub->unit));
			err = BCME_ERROR;
			break;
		}
		else if (wlc_bsscfg_init(wlc, bsscfg) != BCME_OK) {
			WL_ERROR(("wl%d: cannot init bsscfg (GO=%d)\n",
			          wlc->pub->unit, go));
			wlc_bsscfg_free(wlc, bsscfg);
			err = BCME_ERROR;
			break;
		}

		if (go) {

			/* Send bsscfg->wlcif so that the correct bsscfg is updated with
			 * chanspec information. Since the alloc and init of the bsscfg has
			 * happened above, bsscfg->wlcif should return a valid value.
			 * In IOVAR case, chanpec of bsscfg will updated in any case
			 * In supplicant case, IOV chanspec will be called later
			 */
			err = wlc_p2p_go_update_chanspec(wlc, chanspec, bsscfg->wlcif);

			if (err != BCME_OK)
				break;
			}

#if USE_DEF_P2P_IE
		/* default P2P IE??? */
		if (pd->insert_def_p2pie) {
			if (go) {
				wlc_iovar_op(wlc, "vndr_ie", NULL, 0, def_p2p_ie_in_bcn,
				             sizeof(def_p2p_ie_in_bcn), IOV_SET, bsscfg->wlcif);
				wlc_iovar_op(wlc, "vndr_ie", NULL, 0, def_p2p_ie_in_prbresp_go,
				             sizeof(def_p2p_ie_in_prbresp_go), IOV_SET,
				             bsscfg->wlcif);
			} else {
				wlc_iovar_op(wlc, "vndr_ie", NULL, 0, def_p2p_ie_in_prbreq,
				             sizeof(def_p2p_ie_in_prbreq), IOV_SET, bsscfg->wlcif);
				wlc_iovar_op(wlc, "vndr_ie", NULL, 0, def_p2p_ie_in_assocreq,
				             sizeof(def_p2p_ie_in_assocreq), IOV_SET,
				             bsscfg->wlcif);
			}
		}
#endif /* USE_DEF_P2P_IE */

#ifdef WLMCHAN
		/* save go chanspec into bsscfg and set the bsscfg chanspec */
		if (MCHAN_ENAB(wlc->pub) && go)
			wlc_mchan_config_go_chanspec(wlc->mchan, bsscfg, chanspec);
#endif
		break;
	}

	case IOV_SVAL(IOV_P2P_IFDEL): {
		wlc_bsscfg_t *bc = wlc_bsscfg_find_by_unique_hwaddr(wlc, p);

		/* bsscfg with the given MAC address doesn't exist */
		if (bc == NULL) {
			err = BCME_NOTFOUND;
			break;
		}

		/* can't delete the bsscfg associated with the interface */
		if (bc == bsscfg) {
			err = BCME_BADARG;
			break;
		}

		/* can't use ifdel to delete a discovery device.
		 * Only 'p2p_disc 0' should be used
		 */
		if (pd->devcfg == bc) {
			err = BCME_BADARG;
			break;
		}

		if (bc->enable)
			wlc_bsscfg_disable(bc->wlc, bc);
		wlc_bsscfg_free(bc->wlc, bc);
		break;
	}

	case IOV_GVAL(IOV_P2P_IF): {
		wlc_bsscfg_t *bc = wlc_bsscfg_find_by_unique_hwaddr(wlc, p);

		/* bsscfg with the given MAC address doesn't exist */
		if (bc == NULL) {
			err = BCME_NOTFOUND;
			break;
		}

		((wl_p2p_ifq_t *)a)->bsscfgidx = WLC_BSSCFG_IDX(bc);
		strncpy(((wl_p2p_ifq_t *)a)->ifname, wl_ifname(bc->wlc->wl, bc->wlcif->wlif),
		        BCM_MSG_IFNAME_MAX-1);
		break;
	}

	case IOV_SVAL(IOV_P2P_OPS):
		err = wlc_p2p_ops_set(pm, bsscfg, (wl_p2p_ops_t *)p);
		break;

	case IOV_GVAL(IOV_P2P_OPS):
		err = wlc_p2p_ops_get(pm, bsscfg, (wl_p2p_ops_t *)a);
		break;

	case IOV_SVAL(IOV_P2P_NOA):
		err = wlc_p2p_noa_set(pm, bsscfg, (wl_p2p_sched_t *)p, plen);
		break;

	case IOV_GVAL(IOV_P2P_NOA):
#if WFA_VER_1_02_TEST
		wlc_p2p_ver_1_02_attr_test(pm, NULL);
#endif /* WFA_VER_1_02_TEST */
		err = wlc_p2p_noa_get(pm, bsscfg, (wl_p2p_sched_t *)a, alen);
		break;


#if USE_DEF_P2P_IE
	case IOV_SVAL(IOV_P2P_DEFIE):
		pd->insert_def_p2pie = (int_val != 0);
		break;
	case IOV_GVAL(IOV_P2P_DEFIE):
	        *((uint32*)a) = pd->insert_def_p2pie;
		break;
#endif

	case IOV_SVAL(IOV_P2P_IFUPD): {
		struct ether_addr *ea = &((wl_p2p_if_t *)p)->addr;
		uint8 type_parm = ((wl_p2p_if_t *)p)->type;
		chanspec_t chanspec = ltoh16_ua((uint8 *)&((wl_p2p_if_t *)p)->chspec);
		bool go = type_parm != 0;
		uint flags = 0;
		wlc_bsscfg_type_t type = {BSSCFG_TYPE_P2P, BSSCFG_SUBTYPE_NONE};
		WL_INFORM(("%s(): IOV_P2P_IFUPD with idx = %d.\n",
			__FUNCTION__, bsscfg->_idx));

		if (WLC_BSSCFG_IDX(bsscfg) == 0) {
			WL_ERROR(("%s(): IOV_P2P_IFUPD with idx = 0.\n",
				__FUNCTION__));
			err = BCME_BADARG;
			break;
		}

		/* validate chanspec */
		if (go) {
			if (!wlc_p2p_go_chanspec_is_valid(wlc, chanspec)) {
				WL_ERROR(("%s(): GO IOV_P2P_IFUPD with bad chanspec = 0x%x\n",
				          __FUNCTION__, chanspec));
				err = BCME_BADCHAN;
				break;
			}
		}

		wlc_bsscfg_disable(wlc, bsscfg);

		if (type_parm == WL_P2P_IF_DEV) {
			WL_INFORM(("%s(): IOV_P2P_IFUPD with type WL_P2P_IF_DEV, reset bsscfg...\n",
				__FUNCTION__));
			type.subtype = BSSCFG_P2P_DISC;
		}
		else {
			type.subtype = go ? BSSCFG_P2P_GO : BSSCFG_P2P_GC;
		}

		if (go) {
			/* validate chanspec */
			/* IOV_P2P_IFUPD: here it is safe to assum bsscfg is created already
			* we can pass the bsscfg->wlcif pointer here
			*/
			err = wlc_p2p_go_update_chanspec(wlc, chanspec, bsscfg->wlcif);
			if (err != BCME_OK) {
				WL_ERROR(("%s(): GO IOV_P2P_IFUPD with bad chanspec = 0x%x\n",
					__FUNCTION__, chanspec));
				break;
			}
			/* for p2p operation, may start GO on radar channel, so
			 * should have AP_NORADAR_CHAN rules in place.
			 */
			flags |= WLC_BSSCFG_AP_NORADAR_CHAN;
			flags = bsscfg->flags;
		}

		/* reinit the bsscfg to the specified role */
		if (wlc_bsscfg_reset(wlc, bsscfg, &type, flags, ea) != BCME_OK) {
			WL_ERROR(("wl%d: cannot init bsscfg (GO=%d)\n", wlc->pub->unit, go));
			err = BCME_ERROR;
			break;
		}

#ifdef WLMCHAN
		/* save go chanspec into bsscfg and set the bsscfg chanspec */
		if (MCHAN_ENAB(wlc->pub) && go)
			wlc_mchan_config_go_chanspec(wlc->mchan, bsscfg, chanspec);
#endif
		break;
	}

	case IOV_SVAL(IOV_P2P_FEATURES):
		pd->features = (uint32)int_val;
		wlc_disable_probe_resp(wlc, PROBE_RESP_P2P_MASK,
			P2P_GO_NOLEGACY(pm) ? PROBE_RESP_P2P_MASK : 0);
		break;

	case IOV_GVAL(IOV_P2P_FEATURES):
	        *((uint32*)a) = pd->features;
		break;

	case IOV_GVAL(IOV_P2P_DA_OVERRIDE):
	        bcopy(&pd->devaddr, a, ETHER_ADDR_LEN);
	        break;

	case IOV_SVAL(IOV_P2P_DA_OVERRIDE):
	        /* Only allow setting of device address if not in disc state */
	        if (wlc_p2p_disc_state(pm)) {
			err = BCME_BUSY;
			WL_ERROR(("wl%d: Already in disc state, can't set DA at this time!\n",
			          wlc->pub->unit));
		}
		/* look for 0 address, which means restore default da */
		else if (ETHER_ISNULLADDR(p)) {
			wlc_p2p_da_set(pm);
		}
		else {
			bcopy(p, &pd->devaddr, ETHER_ADDR_LEN);
		}
	        break;

	case IOV_SVAL(IOV_P2P_APP_ACTIVE):
		err = wlc_mpc_off_req_set(wlc, MPC_OFF_REQ_P2P_APP_ACTIVE,
			(int_val != 0) ? MPC_OFF_REQ_P2P_APP_ACTIVE : 0);
		break;

#ifdef WLWFDS
	case IOV_SVAL(IOV_P2P_ADD_WFDS_HASH):
		if (WFDS_ENAB(wlc->pub))
			err = wlc_p2p_add_wfds_hash(pm, (wl_p2p_wfds_hash_t *)p);
		else
			err = BCME_UNSUPPORTED;
		break;


	case IOV_SVAL(IOV_P2P_DEL_WFDS_HASH):
		if (WFDS_ENAB(wlc->pub))
			err = wlc_p2p_del_wfds_hash(pm, (wl_p2p_wfds_hash_t *)p);
		else
			err = BCME_UNSUPPORTED;
		break;
#endif /* WLWFDS */

#if defined(WL_NAN_PD_P2P)
	case IOV_GVAL(IOV_P2P_NAN):
	case IOV_SVAL(IOV_P2P_NAN):
		{
			const p2p_nan_ioc_cmd_t *p2pnancmd_ptr = NULL;
			bool is_set;
			if (p && (plen >= (uint)OFFSETOF(wl_p2p_nan_ioc_t, data))) {
				p2pnanioc = p;

				WL_ERROR(("got p2pnanioc : ver:%d, cmd_id:%d, len:%d, data:%d\n",
					p2pnanioc->version, p2pnanioc->id, p2pnanioc->len,
					p2pnanioc->data[0]));
			} else {
				WL_ERROR(("wl%d: %s: params:NULL | plen < ioc sz\n",
					wlc->pub->unit, __FUNCTION__));
				err = BCME_BADLEN;
				break;
			}
			is_set = IOV_ISSET(actionid);

			/* look up p2p nan cmd handler in each subcommand  */

			if ((p2pnancmd_ptr = wlc_p2p_nan_find_cmd_handler(p2p_nan_ioctls,
				p2pnanioc->id))) {
				/* prep the result buf with ioc header */
				wl_p2p_nan_ioc_t *ioc_result = a;
				bcopy(p2pnanioc, ioc_result, sizeof(wl_p2p_nan_ioc_t));

				if (!p2pnancmd_ptr->handler)  {
					WL_ERROR(("wl%d: %s: handler is not implemented\n",
						wlc->pub->unit, __FUNCTION__));
					err = BCME_UNSUPPORTED;
					break;
				}

				return p2pnancmd_ptr->handler(pm, p2pnanioc->data,
					p2pnanioc->len, ioc_result->data, ioc_result->len, is_set);
			} else {
				WL_ERROR(("wl%d: %s: sub command %u not supported\n",
					wlc->pub->unit, __FUNCTION__, p2pnanioc->id));
				err = BCME_UNSUPPORTED;
			}
		}
		break;
#endif /* WL_NAN_PD_P2P */

	default:
		err = BCME_UNSUPPORTED;
		break;
	}

	return err;
}

/** update NoA attribute index */
static void
wlc_p2p_id_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;

	(void)wlc;
	p2p->id ++;

	WL_P2P(("wl%d: update NoA index to %u at tick 0x%x\n",
	        wlc->pub->unit, p2p->id, WL_P2P_TS(wlc)));

	p2p->flags |= WLC_P2P_INFO_ID;
}

static void
wlc_p2p_dialog_upd(wlc_p2p_info_t *pm)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	pd->dialog ++;
	if (pd->dialog == 0)
		pd->dialog = 1;
}


/* come back later when it's too far to schedule the NoA absence now... */
/* 'tsf' must be in local TSF time */
static bool
wlc_p2p_noa_dly(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint32 tsf)
{
	ASSERT(p2p != NULL);

	if (p2p->flags & WLC_P2P_INFO_CUR) {
		if (wlc_p2p_noa_start_dist(pm, p2p, tsf) > P2P_NOA_MAX_NXT) {
			return TRUE;
		}
	}

	return FALSE;
}

/** refresh SHM when the the programmed schedule has run out */
static void
wlc_p2p_noa_resched(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	bool newid = FALSE;
	wlc_bsscfg_t *cfg;

	(void)wlc;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	wlc_p2p_sched_adj(pm, p2p);
	if (P2P_GO(wlc, cfg)) {
		newid = wlc_p2p_sched_renew(pm, p2p);
		if (p2p->sidx == WLC_P2P_NOA_ABS &&
		    newid) {
			p2p->flags &= ~WLC_P2P_INFO_STRT;
		}
	}
	wlc_p2p_sched_norm(pm, p2p);
	if (!wlc_p2p_sched_upd(pm, p2p)) {
		return;
	}
	if (P2P_GO(wlc, cfg)) {
		if (newid) {
			wlc_p2p_id_upd(pm, p2p);
		}
		wlc_p2p_bcn_upd(pm, p2p);
	}
	wlc_p2p_noa_upd(pm, p2p);
}

/**
 * decide if the start is later than the 'tsf' in the context of P2P spec. section 3.3.3.2
 * 'start' and 'tsf' must be in the same TSF domain
 */
static bool
wlc_p2p_noa_start_future(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint32 start, uint32 tsf)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;

	(void)wlc;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	if (P2P_GO(wlc, cfg)) {
		if (!(p2p->flags & WLC_P2P_SCHED_RUN) || tsf <= start)
			return TRUE;
	}
	else /* if (P2P_CLIENT(wlc, cfg)) */ {
		if (U32_DUR(tsf, start) < P2P_NOA_MAX_PRD)
			return TRUE;
	}

	return FALSE;
}

/**
 * bring the start time to the next interval after the 'tsf'. 'tsf' and 'start' must be in local TSF
 * time
 */
static uint32
wlc_p2p_noa_start_next(uint32 tsf, uint32 start, uint32 interval)
{
	return tsf + (interval - ((tsf - start) % interval));
}

/**
 * Calculate the distance between 'tsf' and the start time. GO's start time is always in the future.
 * 'tsf' must be in local TSF time
 */
static uint32
wlc_p2p_noa_start_dist(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint32 tsf)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;
	uint32 dist = 0;
	uint32 start;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	/* p2p->cur.start is remote TSF */
	/* local TSF and remate(BSS) TSF are the same for GO */
	start = p2p->cur.start;

	if (P2P_GO(wlc, cfg)) {
		if (!wlc_p2p_noa_start_future(pm, p2p, start, tsf)) {
			/* bring the start to after the tsf */
			start = wlc_p2p_noa_start_next(tsf, start, p2p->cur.interval);
		}
		dist = U32_DUR(tsf, start);
	}
	else /* if (P2P_CLIENT(wlc, cfg)) */ {
		start = wlc_mcnx_r2l_tsf32(wlc->mcnx, cfg, start);
		if (!wlc_p2p_noa_start_future(pm, p2p, start, tsf)) {
			/* the start is in the past */
			dist = U32_DUR(start, tsf);
		}
		else {
			/* the start is in the future */
			dist = U32_DUR(tsf, start);
		}
	}

	return dist;
}

static void
wlc_p2p_tbtt_cb(void *ctx, wlc_tbtt_ent_data_t *notif_data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	wlc_bsscfg_t *cfg;
	int bss;
	wlc_mcnx_info_t *mcnx;
	bool proc = FALSE;

	cfg = notif_data->cfg;
	ASSERT(cfg != NULL);

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

#ifdef PROP_TXSTATUS
	if (PROP_TXSTATUS_ENAB(wlc->pub)) {
		if ((p2p->flags & WLC_P2P_INFO_OPS) && p2p->ops) {
			wlc_wlfc_interface_state_update(wlc, cfg, WLFC_CTL_TYPE_INTERFACE_OPEN);
		}
	}
#endif

	/* early bailout... */
	if (P2P_GO(wlc, cfg)) {
		if (p2p->ps)
			proc = TRUE;
	}
	else /* if (P2P_CLIENT(wlc, cfg)) */ {
		if (cfg->associated &&
			(p2p->flags & WLC_P2P_INFO_OPS) && p2p->ops)
			proc = TRUE;
	}
	if (!proc)
		return;

	INTRCNTINC(p2p, tbtt);

	mcnx = wlc->mcnx;

	bss = wlc_mcnx_BSS_idx(mcnx, cfg);
	ASSERT(bss < M_P2P_BSS_MAX);

	WL_P2P_INTR(pm, ("wl%d: %s: state 0x%04x at tick 0x%x\n",
	                 wlc->pub->unit, __FUNCTION__,
	                 wlc_mcnx_read_shm(mcnx, M_P2P_BSS_ST(wlc, bss)),
	                 WL_P2P_TS(wlc)));

	if (P2P_GO(wlc, cfg)) {
		wlc_mcnx_hps_upd(mcnx, cfg, M_P2P_HPS_CTW(bss), FALSE);
		p2p->ps = FALSE;
	}
	else /* if (P2P_CLIENT(wlc, cfg)) */ {
		wlc_p2p_abs_q_upd(pm, p2p, ON);
	}
}

/** dispatch PSM event */
static void
wlc_p2p_pre_tbtt_proc(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint32 ltsf_l, uint32 ltsf_h)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	if (WL_P2P_INTR_ON(pm)) {
		uint32 rtsf_l, rtsf_h;
		uint32 tbtt_offset;
		int bss = wlc_mcnx_BSS_idx(mcnx, cfg);

		ASSERT(bss < M_P2P_BSS_MAX);
		BCM_REFERENCE(bss);

		/* remote TSF */
		wlc_mcnx_l2r_tsf64(mcnx, cfg, ltsf_h, ltsf_l, &rtsf_h, &rtsf_l);
		tbtt_offset = wlc_calc_tbtt_offset(cfg->current_bss->beacon_period, rtsf_h, rtsf_l);
		BCM_REFERENCE(tbtt_offset);

		WL_P2P_INTR(pm, ("wl%d: %s: BSS %d at tick 0x%x%08x (local) 0x%x%08x (remote) "
		                 "tbtt offset %d\n",
		                 wlc->pub->unit, __FUNCTION__, bss,
		                 ltsf_h, ltsf_l, rtsf_h, rtsf_l,
		                 tbtt_offset));
	}

	INTRCNTINC(p2p, pretbtt);

	/* set the delayed NoA absence schedule */
	if (p2p->flags & WLC_P2P_INFO_DLY) {
		if (!wlc_p2p_noa_dly(pm, p2p, ltsf_l)) {
			wlc_p2p_noa_upd(pm, p2p);
		}
	}
}

static void
wlc_p2p_pre_tbtt_cb(void *ctx, wlc_tbtt_ent_data_t *notif_data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;
	uint32 tsf_h, tsf_l;
	bss_p2p_info_t *p2p;

	(void)wlc;

	ASSERT(notif_data != NULL);

	cfg = notif_data->cfg;
	ASSERT(cfg != NULL);

	tsf_h = notif_data->tsf_h;
	tsf_l = notif_data->tsf_l;

	p2p = BSS_P2P_INFO(pm, cfg);
	if (p2p == NULL)
		return;

	wlc_p2p_pre_tbtt_proc(pm, p2p, tsf_l, tsf_h);
}

/** return the required pretbtt value */
static void
wlc_p2p_pretbtt_query_cb(void *ctx, bss_pretbtt_query_data_t *notif_data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;

	(void)wlc;

	ASSERT(notif_data != NULL);

	cfg = notif_data->cfg;
	ASSERT(cfg != NULL);

	if (!BSS_P2P_ENAB(wlc, cfg))
		return;

#ifdef BCM7271
	notif_data->pretbtt = 8000;
#else
	notif_data->pretbtt = 3000;
#endif /* BCM7271 */
}

static void
wlc_p2p_ctw_end_proc(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint32 ltsf_l, uint32 ltsf_h)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;
	int bss;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);


	bss = wlc_mcnx_BSS_idx(mcnx, cfg);
	ASSERT(bss < M_P2P_BSS_MAX);

	if (WL_P2P_INTR_ON(pm)) {
		uint32 rtsf_l, rtsf_h;
		uint32 tbtt_offset;

		/* remote TSF */
		wlc_mcnx_l2r_tsf64(mcnx, cfg, ltsf_h, ltsf_l, &rtsf_h, &rtsf_l);
		tbtt_offset = wlc_calc_tbtt_offset(cfg->current_bss->beacon_period, rtsf_h, rtsf_l);
		BCM_REFERENCE(tbtt_offset);

		WL_P2P_INTR(pm, ("wl%d: %s: BSS %d at tick 0x%x%08x (local) 0x%x%08x (remote) "
		                 "tbtt offset %d\n",
		                 wlc->pub->unit, __FUNCTION__, bss,
		                 ltsf_h, ltsf_l, rtsf_h, rtsf_l,
		                 tbtt_offset));
	}

	INTRCNTINC(p2p, ctwend);

	if (P2P_GO(wlc, cfg)) {
		if (!wlc_p2p_ps_allowed(pm, p2p))
			return;
#ifdef PROP_TXSTATUS
		if (PROP_TXSTATUS_ENAB(wlc->pub)) {
			wlc_wlfc_interface_state_update(wlc, cfg, WLFC_CTL_TYPE_INTERFACE_CLOSE);
		}
#endif
		wlc_mcnx_hps_upd(mcnx, cfg, M_P2P_HPS_CTW(bss), TRUE);
		p2p->ps = TRUE;
	}
	else /* if (P2P_CLIENT(wlc, cfg)) */ {
		if ((!cfg->associated || cfg->pm->PMenabled)) {
#ifdef PROP_TXSTATUS
			if (PROP_TXSTATUS_ENAB(wlc->pub)) {
				wlc_wlfc_interface_state_update(wlc, cfg,
					WLFC_CTL_TYPE_INTERFACE_CLOSE);
			}
#endif
			wlc_p2p_abs_q_upd(pm, p2p, OFF);
		}
	}
}

static void
wlc_p2p_tx_block(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, bool block)
{
	wlc_info_t *wlc = pm->wlc;
	struct scb_iter scbiter;
	struct scb *scb;

	FOREACH_BSS_SCB(wlc->scbstate, &scbiter, cfg, scb) {
		if (!SCB_ASSOCIATED(scb) || !SCB_P2P(scb))
			continue;
		wlc_apps_scb_tx_block(wlc, scb, 1, block);
	}
}

static void
wlc_p2p_abs_proc(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint32 ltsf_l, uint32 ltsf_h)
{
	wlc_info_t *wlc = pm->wlc;
	int bss;
	wlc_bsscfg_t *cfg;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	bss = wlc_mcnx_BSS_idx(mcnx, cfg);
	ASSERT(bss < M_P2P_BSS_MAX);

	if (WL_P2P_INTR_ON(pm)) {
		uint32 rtsf_l, rtsf_h;
		uint32 tbtt_offset;

		/* remote TSF */
		wlc_mcnx_l2r_tsf64(mcnx, cfg, ltsf_h, ltsf_l, &rtsf_h, &rtsf_l);
		tbtt_offset = wlc_calc_tbtt_offset(cfg->current_bss->beacon_period, rtsf_h, rtsf_l);
		BCM_REFERENCE(tbtt_offset);

		WL_P2P_INTR(pm, ("wl%d: %s: BSS %d at tick 0x%x%08x (local) 0x%x%08x (remote) "
		                 "tbtt offset %d\n",
		                 wlc->pub->unit, __FUNCTION__, bss,
		                 ltsf_h, ltsf_l, rtsf_h, rtsf_l,
		                 tbtt_offset));
	}

	INTRCNTINC(p2p, abs);

	wlc_p2p_abs_q_upd(pm, p2p, OFF);

	if (P2P_GO(wlc, cfg)) {
		switch (p2p->action) {
		case WL_P2P_SCHED_ACTION_DOZE:
			wlc_mcnx_hps_upd(mcnx, cfg, M_P2P_HPS_NOA(bss), TRUE);
			break;
		case WL_P2P_SCHED_ACTION_GOOFF:
			WL_P2P_INTR(pm, ("wl%d: %s: stop AP\n", wlc->pub->unit, __FUNCTION__));
			wlc_ap_mute(wlc, TRUE, cfg, -1);
			break;
		}
		/* block APSD delivery and PS poll response frames */
		wlc_p2p_tx_block(pm, cfg, TRUE);
	}
	else /* if (P2P_CLIENT(wlc, cfg)) */ {
		if (cfg->pm->PMpending)
			wlc_update_pmstate(cfg, TX_STATUS_NO_ACK);
		/* allow chip to sleep if possible */
#ifdef WME
		if (WME_ENAB(wlc->pub) &&
		    cfg->pm->apsd_sta_usp)
			wlc_set_apsd_stausp(cfg, FALSE);
#endif
		if (cfg->pm->PSpoll)
			wlc_set_pspoll(cfg, FALSE);
	}

#ifdef PROP_TXSTATUS
	if (PROP_TXSTATUS_ENAB(wlc->pub) && !MCHAN_ACTIVE(wlc->pub))
	{
		wlc_wlfc_interface_state_update(wlc, cfg, WLFC_CTL_TYPE_INTERFACE_CLOSE);
		wlc_wlfc_flush_pkts_to_host(wlc, cfg);
	}
#endif /* PROP_TXSTATUS */
}

static void
wlc_p2p_psc_proc(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint32 ltsf_l, uint32 ltsf_h)
{
	wlc_info_t *wlc = pm->wlc;
	int bss;
	wlc_bsscfg_t *cfg;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	bss = wlc_mcnx_BSS_idx(mcnx, cfg);
	ASSERT(bss < M_P2P_BSS_MAX);

	if (WL_P2P_INTR_ON(pm)) {
		uint32 rtsf_l, rtsf_h;
		uint32 tbtt_offset;

		/* remote TSF */
		wlc_mcnx_l2r_tsf64(mcnx, cfg, ltsf_h, ltsf_l, &rtsf_h, &rtsf_l);
		tbtt_offset = wlc_calc_tbtt_offset(cfg->current_bss->beacon_period, rtsf_h, rtsf_l);
		BCM_REFERENCE(tbtt_offset);

		WL_P2P_INTR(pm, ("wl%d: %s: BSS %d at tick 0x%x%08x (local) 0x%x%08x (remote) "
		                 "tbtt offset %d\n",
		                 wlc->pub->unit, __FUNCTION__, bss,
		                 ltsf_h, ltsf_l, rtsf_h, rtsf_l,
		                 tbtt_offset));
	}

	INTRCNTINC(p2p, prs);

	/* process things related to the current schedule here
	 * before it is changed in wlc_p2p_noa_resched()...
	 */
	if (P2P_GO(wlc, cfg)) {
		switch (p2p->action) {
		case WL_P2P_SCHED_ACTION_DOZE:
			wlc_mcnx_hps_upd(mcnx, cfg, M_P2P_HPS_NOA(bss), FALSE);
			break;
		case WL_P2P_SCHED_ACTION_GOOFF:
			WL_P2P_INTR(pm, ("wl%d: %s: resume AP\n", wlc->pub->unit, __FUNCTION__));
			wlc_ap_mute(wlc, FALSE, cfg, -1);
			break;
		}
		/* allow APSD delivery and PS poll response frames */
		wlc_p2p_tx_block(pm, cfg, FALSE);
	}
	else /* if (P2P_CLIENT(wlc, cfg)) */ {
#ifdef WME
		/* send APSD re-trigger if APSD USP hasn't ended */
		if (WME_ENAB(wlc->pub) &&
		    (p2p->flags & WLC_P2P_INFO_APSD_RETRIG)) {
			wlc_sendapsdtrigger(wlc, cfg);
		}
#endif
		/* send PS poll if not responded yet */
		if (p2p->flags & WLC_P2P_INFO_PSPOLL_RESEND) {
			if (wlc_sendpspoll(wlc, cfg) == FALSE) {
				WL_ERROR(("wl%d: %s: wlc_sendpspoll() failed\n",
				          wlc->pub->unit, __FUNCTION__));
			}
		}
	}

	wlc_p2p_abs_q_upd(pm, p2p, ON);

	p2p->count --;
	if (wlc_mcnx_read_shm(mcnx, M_P2P_BSS_NOA_CNT(wlc, bss)) == 0) {
		WL_P2P(("wl%d: %s: BSS %d NoA count down to 0\n",
		        wlc->pub->unit, __FUNCTION__, bss));
		wlc_p2p_noa_resched(pm, p2p);
	}

#ifdef PROP_TXSTATUS
	if (PROP_TXSTATUS_ENAB(wlc->pub)) {
#ifdef WLMCHAN
		if (!MCHAN_ACTIVE(wlc->pub) ||
			(cfg == wlc_mchan_get_cfg_frm_q(wlc, wlc->primary_queue))) {
			wlc_wlfc_interface_state_update(wlc, cfg, WLFC_CTL_TYPE_INTERFACE_OPEN);
		}
#endif /* WLMCHAN */
#ifdef WLAMPDU
		if (AMPDU_ENAB(wlc->pub)) {
			/* packet supression during close will reset ampdu-seq */
			struct scb_iter scbiter;
			struct scb *scb = NULL;
			FOREACHSCB(wlc->scbstate, &scbiter, scb) {
				if (scb->bsscfg != cfg)
					continue;
				wlc_ampdu_send_bar_cfg(wlc->ampdu_tx, scb);
			}
		}
#endif /* WLAMPDU */
	}
#endif /* PROP_TXSTATUS */
}


/* These functions register/unregister a callback that wlc_mcnx_tsf_upd may invoke. */
int
BCMATTACHFN(wlc_p2p_noa_upd_register)(wlc_p2p_info_t *pm, wlc_p2p_noa_upd_fn_t cb, void *arg)
{
	bcm_notif_h hdl = pm->cxnoa_upd_hdl;
	return bcm_notif_add_interest(hdl, (bcm_notif_client_callback)cb, arg);
}

int
BCMATTACHFN(wlc_p2p_noa_upd_unregister)(wlc_p2p_info_t *pm, wlc_p2p_noa_upd_fn_t cb, void *arg)
{
	bcm_notif_h hdl = pm->cxnoa_upd_hdl;
	return bcm_notif_remove_interest(hdl, (bcm_notif_client_callback)cb, arg);
}

int
BCMATTACHFN(wlc_p2p_noa_desc_upd_register)(wlc_p2p_info_t *pm, wlc_p2p_noa_desc_upd_fn_t cb,
		void *arg)
{
	bcm_notif_h hdl = pm->cxnoa_desc_hdl;
	return bcm_notif_add_interest(hdl, (bcm_notif_client_callback)cb, arg);
}

int
BCMATTACHFN(wlc_p2p_noa_desc_upd_unregister)(wlc_p2p_info_t *pm, wlc_p2p_noa_desc_upd_fn_t cb,
		void *arg)
{
	bcm_notif_h hdl = pm->cxnoa_desc_hdl;
	return bcm_notif_remove_interest(hdl, (bcm_notif_client_callback)cb, arg);
}

static void
wlc_p2p_noa_upd_notif(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	bss_p2p_info_t *p2p;
	wlc_p2p_noa_upd_data_t notif_data;
	bcm_notif_h hdl = pm->cxnoa_upd_hdl;

	p2p = BSS_P2P_INFO(pm, cfg);
	if (p2p == NULL)
		return;
	notif_data.cfg = cfg;
	notif_data.desc = p2p->sched[0].desc;
	notif_data.cnt = p2p->sched[0].cnt;
	bcm_notif_signal(hdl, &notif_data);
	memcpy(&p2p->cur,  &notif_data.cur, sizeof(notif_data.cur));
	p2p->flags |= notif_data.flags;
}

static bool
wlc_p2p_noa_desc_upd_notif(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wl_p2p_sched_desc_t *desc)
{
	wlc_p2p_noa_desc_upd_data_t notif_data;
	bcm_notif_h hdl = pm->cxnoa_desc_hdl;

	memcpy(&notif_data.desc, desc, sizeof(*desc));
	notif_data.cfg = cfg;
	bcm_notif_signal(hdl, &notif_data);

	return notif_data.is_go_rfact_avail;
}
#ifdef BCMCOEXNOA

void
wlc_p2p_cxnoa_upd(wlc_info_t *wlc, wlc_bsscfg_t *cfg)
{
	bss_p2p_info_t * p2p;

	p2p = BSS_P2P_INFO(wlc->p2p, cfg);
	ASSERT(p2p != NULL);

	wlc_p2p_noa_upd(wlc->p2p, p2p);
}

/*
 * Wrapper function returns the status of bss_p2p_info structure
 * if BSS_P2P_INFO returns NULL, then coex doesn't make sense.
 */
bool
wlc_p2p_cxnoa_bss_status(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	bss_p2p_info_t *p2p;
	bool status;

	p2p = BSS_P2P_INFO(pm, cfg);
	if (p2p == NULL)
		status = FALSE;
	else {
		/* ignore when GO is not UP or Client is not associated,
		 * ignore when neither Absence nor CTW is scheduled.
		 */
		if (p2p->flags & WLC_P2P_INFO_NET)
			status = TRUE;
		else
			status = FALSE;
	}

	return status;
}

#endif /* BCMCOEXNOA */

static void
wlc_p2p_intr_cb(void *ctx, wlc_mcnx_intr_data_t *notif_data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;
	bss_p2p_info_t *p2p;
	uint32 tsf_h;
	uint32 tsf_l;
	uint intr;

	(void)wlc;

	cfg = notif_data->cfg;
	ASSERT(cfg != NULL);

	p2p = BSS_P2P_INFO(pm, cfg);
	if (p2p == NULL)
		return;

	/* ignore when GO is not UP or Client is not associated,
	 * ignore when neither Absence nor CTW is scheduled.
	 */

	if (!(p2p->flags & WLC_P2P_INFO_NET))
		return;

	tsf_h = notif_data->tsf_h;
	tsf_l = notif_data->tsf_l;
	intr = notif_data->intr;

	/* process each P2P interrupt (generated by ucode) */
	switch (intr) {
	case M_P2P_I_PRE_TBTT:
		break;
	case M_P2P_I_CTW_END:
		if (!(p2p->flags & WLC_P2P_INFO_OPS) || !p2p->ops)
			break;
		wlc_p2p_ctw_end_proc(pm, p2p, tsf_l, tsf_h);
		break;
	case M_P2P_I_ABS: /* trigger to start switching channels */
		if (!(p2p->flags & WLC_P2P_INFO_CUR))
			break;
#ifdef WLMCHAN
		if (MCHAN_ENAB(wlc->pub))
			wlc_mchan_abs_proc(wlc, cfg, tsf_l);
#endif
		wlc_p2p_abs_proc(pm, p2p, tsf_l, tsf_h);
		break;
	case M_P2P_I_PRS:
		if (!(p2p->flags & WLC_P2P_INFO_CUR))
			break;
#ifdef WLMCHAN
		if (MCHAN_ENAB(wlc->pub))
			wlc_mchan_psc_proc(wlc, cfg, tsf_l);
#endif
		wlc_p2p_psc_proc(pm, p2p, tsf_l, tsf_h);
		break;
	default:
		ASSERT(0);
		break;
	}
}

static void
wlc_free_p2p_attr_buf(wlc_info_t *wlc, uint8 **se)
{
	if (*se) {
		MFREE(wlc->osh, *se, ltoh16_ua(&(*se)[P2P_ATTR_LEN_OFF]) + P2P_ATTR_HDR_LEN);
		*se = NULL;
	}

	return;
}

static bool
wlc_is_p2p_ie(uint8 *nextie, uint8 type)
{
	wifi_p2p_ie_t *ie = (wifi_p2p_ie_t *) nextie;

	BCM_REFERENCE(type);

	if (ie->id != DOT11_MNG_VS_ID || ie->len <= (WPA_OUI_LEN + 1) ||
		bcmp(&nextie[TLV_BODY_OFF], WFA_OUI, WFA_OUI_LEN) ||
		ie->oui_type != WFA_OUI_TYPE_P2P) {
		return FALSE;
	}

	return TRUE;
}


/**
 * Locate an attribute (subelement) in a P2P IE.
 *   ie = Current P2P IE to search in.
 *   len = Current P2P IE length.
 *   id = P2P attribute ID to search for.
 *   buf_len = Remaining length of the packet buffer that 'ie' is in.
 *             There may be more P2P IEs in this buffer after the current P2P IE.
 *   combined_se = If the found attribute is split across multiple P2P IEs,
 *                 the reassembled attribute will be copied into a malloc'd
 *                 buffer, this output pointer will be set to point to that
 *                 buffer, and the caller is responsible for freeing this ptr.
 *                 Else this pointer is set to NULL.
 * Returns a pointer to the found attribute in the P2P IE.
 */
static uint8 *
wlc_p2p_find_se(wlc_info_t *wlc, wifi_p2p_ie_t *ie, int len, uint8 id,
	int buf_len, uint8 **combined_se)
{
	uint8 *se;
	uint8 *nextie, *remain_datap;
	uint8 *buf_limit;
	int remainlen, bodylen;
	int slen = 0;
	bool match_found;

	*combined_se = NULL;
	se = ie->subelts;
	len -= P2P_IE_FIXED_LEN;
	buf_limit = (uint8*) ie + buf_len;

	WL_NONE(("%s: ie=%p id=%u len=%d buflen=%d\n",
		__FUNCTION__, OSL_OBFUSCATE_BUF(ie), id, len, buf_len));
	while (len >= P2P_ATTR_HDR_LEN) {

		{
			slen = ltoh16_ua(&se[P2P_ATTR_LEN_OFF]) + P2P_ATTR_HDR_LEN;
		}

		/*
		 * Spec 1.02: handle 'Attributes' split across multiple P2P-IEs
		 */
		match_found = (se[P2P_ATTR_ID_OFF] == id) ? TRUE : FALSE;
		WL_NONE(("%s: ie=%p se=%p slen=%d seid=%u %s\n", __FUNCTION__,
			OSL_OBFUSCATE_BUF(ie), OSL_OBFUSCATE_BUF(se), slen, se[P2P_ATTR_ID_OFF],
				match_found ? "match" : ""));
		if (slen > len) {
			nextie = se + len;
			remainlen = slen - len;
			ie = (wifi_p2p_ie_t *) nextie;
			WL_NONE(("%s: split attrib, nextie=%p remlen=%u\n",
				__FUNCTION__, OSL_OBFUSCATE_BUF(ie), remainlen));
			/*
			 * This attribute data spans across to next IE.
			 */
			if (match_found) {
				if ((*combined_se = MALLOCZ(wlc->osh, slen)) == NULL) {
					WL_ERROR((WLC_MALLOC_ERR, WLCWLUNIT(wlc), __FUNCTION__,
						slen, MALLOCED(wlc->osh)));
					return NULL;
				}
				WL_NONE(("%s:   1 bcopy from %p, len=%d\n",
					__FUNCTION__, OSL_OBFUSCATE_BUF(se), len));
				bcopy(se, *combined_se, len);
			}
			while (remainlen) {
				remain_datap = nextie + P2P_IE_FIXED_LEN;
				if (remain_datap > buf_limit) {
					WL_NONE(("%s 0: nextie %p + 0x%x > limit %p\n",
						__FUNCTION__, OSL_OBFUSCATE_BUF(nextie),
						P2P_IE_FIXED_LEN,
						OSL_OBFUSCATE_BUF(buf_limit)));
					goto incomplete_attr;
				}
				if (wlc_is_p2p_ie(nextie, WFA_OUI_TYPE_P2P) == FALSE) {
					/* Can't find next IE contains remaining p2p attr. */
					WL_NONE(("%s 1: nextie not P2P IE\n", __FUNCTION__));
					goto incomplete_attr;
				}
				bodylen = ie->len - (WPA_OUI_LEN + 1);
				if (remainlen > bodylen) {
					if (remain_datap + bodylen > buf_limit) {
						WL_NONE(("%s 2: remainp %p + 0x%x > limit %p\n",
							__FUNCTION__,
							OSL_OBFUSCATE_BUF(remain_datap),
							bodylen, OSL_OBFUSCATE_BUF(buf_limit)));
						goto incomplete_attr;
					}
					remainlen -= bodylen;
					if (match_found) {
						WL_NONE(("%s:   2 bcopy from %p, len=%d\n",
							__FUNCTION__,
						OSL_OBFUSCATE_BUF(remain_datap), bodylen));
						bcopy(remain_datap, *combined_se + len, bodylen);
						len += bodylen;
					}
					nextie = remain_datap + bodylen;
					ie = (wifi_p2p_ie_t *) nextie;
				} else {
					if (remain_datap + remainlen > buf_limit) {
						WL_NONE(("%s 3: remainp %p + 0x%x > limit %p\n",
							__FUNCTION__,
							OSL_OBFUSCATE_BUF(remain_datap),
							remainlen, OSL_OBFUSCATE_BUF(buf_limit)));
						goto incomplete_attr;
					}
					if (match_found) {
						WL_NONE(("%s:   3 bcopy from %p, len=%d\n",
							__FUNCTION__,
							OSL_OBFUSCATE_BUF(remain_datap),
							remainlen));
						bcopy(remain_datap, *combined_se + len, remainlen);
						/*
						 * Successfully copied all segmented p2p attribute
						 * data to the allocated buffer return the p2p
						 * attribute to the calling function.
						 */
						return *combined_se;
					} else {
						/*
						 * Found entire attribute, find next p2p attribute.
						 */
						se = nextie + P2P_IE_FIXED_LEN + remainlen;
						len = bodylen - remainlen;
						if (len == 0) {
							/*
							 * the p2p attribute is not in this IE,
							 * check next IE.
							 */
							nextie = se;
							if (nextie + P2P_IE_FIXED_LEN
								> buf_limit) {
								WL_NONE(("%s 4: %p + 0x%x"
								" > limit %p\n",
								__FUNCTION__,
								OSL_OBFUSCATE_BUF(nextie),
								P2P_IE_FIXED_LEN,
								OSL_OBFUSCATE_BUF(buf_limit)));
								goto incomplete_attr;
							}
							if (wlc_is_p2p_ie(nextie,
								WFA_OUI_TYPE_P2P) == FALSE) {
								WL_NONE(("%s 5: nextie not P2P\n",
									__FUNCTION__));
								goto incomplete_attr;
							} else {
								ie = (wifi_p2p_ie_t *) nextie;
								se = nextie + P2P_IE_FIXED_LEN;
								len = ie->len - (WPA_OUI_LEN + 1);
							}
						}
						break;
					}
				}
			}
		} else if (match_found) {
			return se;
		} else {
			se += slen;
			len -= slen;
		}
	}

	WL_NONE(("%s: Not found. cse=%p slen=%d len=%d\n",
		__FUNCTION__, OSL_OBFUSCATE_BUF(*combined_se), slen, len));
	if (*combined_se) {
		MFREE(wlc->osh, *combined_se, slen);
		*combined_se = NULL;
	}
	return NULL;

incomplete_attr:
	/* Reaching here means the attribute data exceeds the length of all IEs
	 * or we can't find the next P2P IE for a p2p attr spanning multiple IEs.
	 */
	WL_NONE(("%s: discarded incomplete spanning SE! match=%d\n",
		__FUNCTION__, match_found));
	if (*combined_se) {
		MFREE(wlc->osh, *combined_se, slen);
		*combined_se = NULL;
	}
	return NULL;
}

/**
 * Search all IEs in a packet buffer for a specific P2P IE attribute.
 *   id          : IN: P2P attribute ID to search for.
 *   ies         : IN: Pointer to the first IE in a buffer of IEs in TLV format.
 *                 OUT: If the attribute was found, this points to next IE
 *                      after the IE where the attribute was found.
 *                      If the attribute was not found, this value is left
 *                      unchanged (points to the first IE).
 *   ies_len     : IN: Length of all IEs in the buffer.
 *                 OUT: Length of remaining buffer.
 *   combined_se : OUT: If the found attribute is split across multiple P2P IEs,
 *                 the reassembled attribute will be copied into a malloc'd
 *                 buffer, this output pointer will be set to point to that
 *                 buffer, and the caller is responsible for freeing this ptr.
 *                 Else this pointer is set to NULL.
 *   se_len      : OUT - If not NULL and the attribute is found, this is set
 *                 to the attribute's length
 *   found_p2p_ie: OUT: If not NULL, this is set to TRUE if any P2P IE was
 *                 found in the buffer or FALSE if none were found.
 * Return:
 *     If the attribute was found, returns a pointer to the found attribute
 *     in the P2P IE.  If not found, returns NULL.
 * Notes:
 *     The output values of tlvs and tlvs_len are set such that the caller can
 *     call this function in a loop to search all IEs in a buffer for multiple
 *     occurrences of a given attribute ID.
 * this function
 */
static uint8*
wlc_p2p_find_all_se_core(wlc_info_t *wlc, uint8 id, uint8 **ies, int *ies_len,
	uint8 **combined_se, int *se_len, bool *found_p2p_ie)
{
	uint8 *tlvs;
	int tlvs_len;
	wifi_p2p_ie_t *ie;
	bool found_p2p = FALSE;
	int ilen;
	uint8 find_id = id;
	uint8 *se = NULL;

	ASSERT(ies_len != NULL);
	ASSERT(ies != NULL);
	ASSERT(*ies != NULL);
	tlvs = *ies;
	tlvs_len = *ies_len;
	WL_NONE(("%s  IN: id=%u tlvs=%p,l=%d\n",
		__FUNCTION__, id, OSL_OBFUSCATE_BUF(tlvs), tlvs_len));

	while (se == NULL && (ie = bcm_find_p2pie(tlvs, tlvs_len)) != NULL) {

		found_p2p = TRUE;
		ilen = ie->len + TLV_HDR_LEN;
		se = wlc_p2p_find_se(wlc, ie, ilen, find_id, tlvs_len, combined_se);

		tlvs_len -= (int)((uint8 *)ie - tlvs) + ilen;
		tlvs = (uint8 *)ie + ilen;
	}

	if (se_len != NULL) {
		if (se != NULL) {
			wifi_p2p_noa_se_t *attr = (wifi_p2p_noa_se_t*) se;
			*se_len = ltoh16_ua(attr->len) + P2P_ATTR_HDR_LEN;
		} else {
			*se_len = 0;
		}
	}

	if (found_p2p_ie != NULL) {
		*found_p2p_ie = found_p2p;
	}

	WL_NONE(("%s OUT: tlvs=%p,l=%d se=%p,l=%u fndp2p=%u\n", __FUNCTION__,
		OSL_OBFUSCATE_BUF(tlvs), tlvs_len,
		OSL_OBFUSCATE_BUF(se), se_len ? *se_len : 0, found_p2p));
	*ies = tlvs;
	*ies_len = tlvs_len;
	return se;
}

/**
 * Call wlc_p2p_find_all_se_core() without modifying our caller's ies and ies_len parameters.
 * wlc_p2p_find_all_se_core() modifies its caller's ies and ies_len parameters.
 */
static uint8*
wlc_p2p_find_all_se(wlc_info_t *wlc, uint8 id, uint8 *ies, int ies_len,
	uint8 **combined_se, int *se_len, bool *found_p2p_ie)
{
	return wlc_p2p_find_all_se_core(wlc, id, &ies, &ies_len, combined_se,
		se_len, found_p2p_ie);
}

/**
 * fixup P2P SSID to P2P in each state
 * Search State: fixup wildcard SSID to P2P wildcard SSID
 * Scan State: set P2P_FLAG_SCAN_ALL flag if SSID is wildcard
 */
void
wlc_p2p_fixup_SSID(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wlc_ssid_t *ssid)
{
	wlc_p2p_data_t *pd = pm->p2p_data;

	ASSERT(ssid->SSID_len == 0);

	if (!wlc_p2p_disc_state(pm))
		return;

	if (cfg != pd->devcfg)
		return;

	if (pd->state == WL_P2P_DISC_ST_SEARCH) {
		bcopy(&pd->ssid, ssid, sizeof(wlc_ssid_t));
	} else if (pd->state == WL_P2P_DISC_ST_SCAN) {
		WL_INFORM(("wl%d.%d: SSID length is 0, set P2P_FLAG_SCAN_ALL flag\n",
		        pm->wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
		pd->flags |= P2P_FLAG_SCAN_ALL;
	}
}

/** Calculate the length of the Advertised Service Info P2P IE attribute to be added */
static int
wlc_p2p_calc_advt_ie_len(wlc_p2p_info_t *pm, int len)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_p2p_adv_svc_info_attr	*list = pd->to_send_wfds_adv_list;

	if (!len) /** Add the P2P_IE_FIXED_LEN if IE len is 0 */
		len += P2P_IE_FIXED_LEN;

	len += P2P_ADVT_SERV_SE_FIXED_LEN;

	while (list) {
		/* Assume only one ADVT info */
		len += P2P_ADVT_SERV_INFO_FIXED_LEN;
		len += list->name_len;
		list = list->next;
	}
	WL_TRACE(("wlc_p2p_write_ie_len: WLC_P2P_INFO_FLAG_WFDS_HASH "
		"is set total len is %d svc len %d \n", len,
		pd->to_send_wfds_adv_list ?
		pd->to_send_wfds_adv_list->name_len : 0));

	return len;
}

/** Append a Advertised Service Info P2P IE attribute to the given P2P IE buffer */
static int
wlc_p2p_add_advt_ie(wlc_p2p_info_t *pm, wifi_p2p_ie_t *ie, uint8 *buf, int len)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	wifi_p2p_advt_serv_se_t	*adv_se = NULL;
	wifi_p2p_adv_serv_info_t *adv_info;
	uint16 se_length = 0;
	wlc_p2p_adv_svc_info_attr *list = pd->to_send_wfds_adv_list;

	if (!ie) { /** IE is not populated */
		ie = (wifi_p2p_ie_t *)(buf + len);
		adv_se = (wifi_p2p_advt_serv_se_t *)ie->subelts;
		ie->id = DOT11_MNG_VS_ID;
		ie->len = P2P_IE_FIXED_LEN - TLV_HDR_LEN;
		bcopy(WFA_OUI, ie->OUI, WFA_OUI_LEN);
		ie->oui_type = WFA_OUI_TYPE_P2P;
		len += P2P_IE_FIXED_LEN;
	} else /* Piggy back with existing IE */
		adv_se = (wifi_p2p_advt_serv_se_t *)(ie->subelts + ie->len);

	adv_info = adv_se->p_advt_serv_info;
	adv_se->eltId = P2P_SEID_ADVERTISE_SERVICE;
	se_length = 0;

	while (list) {
		se_length += P2P_ADVT_SERV_INFO_FIXED_LEN;
		se_length += list->name_len;

		memcpy(adv_info->advt_id, &(list->advt_id), sizeof(list->advt_id));
		adv_info->nw_cfg_method = list->nw_cfg_method;
		adv_info->serv_name_len = list->name_len;
		memcpy(adv_info->serv_name, list->service_name, list->name_len);
		WL_NONE(("%s: add ADVT_SERV id=%u cm=%x nlen=%u\n",
			__FUNCTION__, *(uint32*)(adv_info->advt_id),
			adv_info->nw_cfg_method, adv_info->serv_name_len));

		if (list->next)
			adv_info = (wifi_p2p_adv_serv_info_t*)
				((uint8 *)adv_info +
				P2P_ADVT_SERV_INFO_FIXED_LEN +
				list->name_len);

		list = list->next;
	}

	htol16_ua_store(se_length, adv_se->len);

	ie->len += P2P_ADVT_SERV_SE_FIXED_LEN + se_length;
	len += P2P_ADVT_SERV_SE_FIXED_LEN + se_length;
	WL_TRACE(("wlc_p2p_add_advt_ie: len=%d se_len=%d\n", len, se_length));

	return len;
}

/** returns the length of the P2P IE. */
static int
wlc_p2p_write_ie_len(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, uint type)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	bss_p2p_cmn_info_t *cmn;
	wlc_p2p_sched_t *sched;
	int len = 0;
	int cnt;
	uint8 abs[] = {WLC_P2P_NOA_ABS, WLC_P2P_NOA_REQ_ABS};
	uint i;
	bool noa = FALSE;

	(void)wlc;

	ASSERT(cfg != NULL);

	if (!P2P_GO(wlc, cfg) &&
#ifdef WLWFDS /** Reuse the same API for writing the WFDS IE's */
	    (!WFDS_ENAB(pm->wlc->pub) || !(pd->flags & WLC_P2P_INFO_FLAG_WFDS_HASH)) &&
#endif
	    TRUE)
		return 0;

	cmn = BSS_P2P_CMN_INFO(pm, cfg);
	ASSERT(cmn != NULL);

	if (type == FC_PROBE_RESP && !(cmn->flags & BSS_P2P_CMN_INFO_P2P_IE))
		return 0;

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL || !P2P_GO(wlc, cfg));

#ifdef WLWFDS
	if (P2P_GO(wlc, cfg)) {
#endif
		for (i = 0; i < ARRAYSIZE(abs); i ++) {
			sched = &p2p->sched[abs[i]];
			if (sched->cnt > sched->idx) {
				noa = TRUE;
				break;
			}
		}
		if (((p2p->flags & WLC_P2P_INFO_OPS) && p2p->ops) || noa) {
			len += P2P_IE_FIXED_LEN;
			len += P2P_NOA_SE_FIXED_LEN;
		}
		if (noa) {
			for (i = 0; i < ARRAYSIZE(abs); i ++) {
				sched = &p2p->sched[abs[i]];
				if ((cnt = sched->cnt - sched->idx) > 0)
					len += cnt * sizeof(wifi_p2p_noa_desc_t);
			}
		}

#ifdef WLWFDS
	}
#endif

	if (WFDS_ENAB(pm->wlc->pub) && (pd->flags & WLC_P2P_INFO_FLAG_WFDS_HASH))
		len = wlc_p2p_calc_advt_ie_len(pm, len);

	return len;
}

/**
 * write the P2P IE out in the given buffer and return the length in bytes written in the buffer
 * wlc_p2p_write_ie() is shared by both beacon and prbreq/prbresp updates
 */
static int
wlc_p2p_write_ie(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, uint type, uint8 *buf)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	bss_p2p_cmn_info_t *cmn;
	wlc_p2p_sched_t *sched;
	int len = 0;
	int cnt;
	uint8 abs[] = {WLC_P2P_NOA_ABS, WLC_P2P_NOA_REQ_ABS};
	uint i;
	bool noa = FALSE;
	wifi_p2p_ie_t *ie = NULL;
	wifi_p2p_noa_se_t *se = NULL;
	uint16 se_length = 0;
	wifi_p2p_noa_desc_t *desc;

	(void)wlc;

	ASSERT(cfg != NULL);
	ASSERT(buf != NULL);

	if (!P2P_GO(wlc, cfg) &&
#ifdef WLWFDS /** Reuse the same API for writing the WFDS IE's */
	    (!WFDS_ENAB(wlc->pub) || !(pd->flags & WLC_P2P_INFO_FLAG_WFDS_HASH)) &&
#endif
	    TRUE)
		return 0;

	cmn = BSS_P2P_CMN_INFO(pm, cfg);
	ASSERT(cmn != NULL);

	if (type == FC_PROBE_RESP && !(cmn->flags & BSS_P2P_CMN_INFO_P2P_IE))
		return 0;

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL || !P2P_GO(wlc, cfg));

#ifdef WLWFDS
	if (P2P_GO(wlc, cfg)) { /** Prepend to the New IE to be added */
#endif /* WLWFDS */

	for (i = 0; i < ARRAYSIZE(abs); i ++) {
		sched = &p2p->sched[abs[i]];
		if (sched->cnt > sched->idx) {
			noa = TRUE;
			break;
		}
	}
	if (((p2p->flags & WLC_P2P_INFO_OPS) && p2p->ops) || noa) {
		ie = (wifi_p2p_ie_t *)(buf + len);
		se = (wifi_p2p_noa_se_t *)ie->subelts;

		ie->id = DOT11_MNG_VS_ID;
		ie->len = P2P_IE_FIXED_LEN - TLV_HDR_LEN;
		bcopy(WFA_OUI, ie->OUI, WFA_OUI_LEN);
		ie->oui_type = WFA_OUI_TYPE_P2P;
		len += P2P_IE_FIXED_LEN;

		{
			se->eltId = P2P_SEID_ABSENCE;
			se_length = P2P_NOA_SE_FIXED_LEN - P2P_ATTR_HDR_LEN;
			htol16_ua_store(se_length, se->len);
		}
		se->index = p2p->id;
		se->ops_ctw_parms = (p2p->ops << P2P_NOA_OPS_SHIFT) | p2p->ctw;
		ie->len += P2P_NOA_SE_FIXED_LEN;
		len += P2P_NOA_SE_FIXED_LEN;
	}
	if (noa) {
		desc = se->desc;

		for (i = 0; i < ARRAYSIZE(abs); i ++) {
			sched = &p2p->sched[abs[i]];
			if ((cnt = sched->cnt - sched->idx) > 0) {
				int dlen;
				ASSERT(sched->desc != NULL);
				dlen = wlc_p2p_sched_wl2se(&sched->desc[sched->idx], desc, cnt);
				if (abs[i] == WLC_P2P_NOA_ABS &&
				    (p2p->flags & WLC_P2P_INFO_STRT)) {
					htol32_ua_store(p2p->start, (uint8 *)&desc->start);
					WL_P2P(("wl%d: overwrite NoA schedule start time 0x%x\n",
						wlc->pub->unit, p2p->start));
				}
				desc += cnt;
				se_length += (uint8)dlen;
				{
					htol16_ua_store(se_length, se->len);
				}
				ie->len += (uint8)dlen;
				len += dlen;
			}
		}
	}

#ifdef WLWFDS
	}
#endif
	if (WFDS_ENAB(pm->wlc->pub) && (pd->flags & WLC_P2P_INFO_FLAG_WFDS_HASH))
		len = wlc_p2p_add_advt_ie(pm, ie, buf, len);
	else
		WL_NONE(("%s: else no WFDS_HASH flag\n", __FUNCTION__));

	return len;
}

/* return the length of the P2P IE. */
int
wlc_p2p_write_ie_quiet_len(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, uint type)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	bss_p2p_cmn_info_t *cmn;
	wlc_p2p_sched_t *sched;
	int len = 0;
	int cnt;

	(void)wlc;

	ASSERT(cfg != NULL);

	if (!P2P_GO(wlc, cfg))
		return 0;

	cmn = BSS_P2P_CMN_INFO(pm, cfg);
	ASSERT(cmn != NULL);

	if (type == FC_PROBE_RESP && !(cmn->flags & BSS_P2P_CMN_INFO_P2P_IE))
		return 0;

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	sched = &p2p->sched[WLC_P2P_NOA_ABS];

	cnt = sched->cnt - sched->idx;

	if (cnt > 0 &&
	    sched->option == WL_P2P_SCHED_OPTION_BCNPCT) {
		len += sizeof(dot11_quiet_t);

		WL_TMP(("wl%d: %s: length %u BSS %s\n",
		        wlc->pub->unit, __FUNCTION__, len,
		        bcm_ether_ntoa(&cfg->BSSID, eabuf)));
	}

	return len;
}

/**
 * write the Quiet IE out in the given buffer and return the length in bytes written in the buffer
 */
int
wlc_p2p_write_ie_quiet(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, uint type, uint8 *buf)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	bss_p2p_cmn_info_t *cmn;
	wlc_p2p_sched_t *sched;
	int len = 0;
	int cnt;

	(void)wlc;

	ASSERT(cfg != NULL);
	ASSERT(buf != NULL);

	if (!P2P_GO(wlc, cfg))
		return 0;

	cmn = BSS_P2P_CMN_INFO(pm, cfg);
	ASSERT(cmn != NULL);

	if (type == FC_PROBE_RESP && !(cmn->flags & BSS_P2P_CMN_INFO_P2P_IE))
		return 0;

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	sched = &p2p->sched[WLC_P2P_NOA_ABS];

	cnt = sched->cnt - sched->idx;

	if (cnt > 0 &&
	    sched->option == WL_P2P_SCHED_OPTION_BCNPCT) {
		dot11_quiet_t *quiet = (dot11_quiet_t *)buf;
		uint16 bcnint;
		uint16 off;
		uint16 dur;

		bcnint = cfg->current_bss->beacon_period;

		quiet->id = DOT11_MNG_QUIET_ID;
		quiet->len = sizeof(dot11_quiet_t) - TLV_HDR_LEN;
		quiet->count = 1;
		quiet->period = 1;
		off = bcnint * sched->start / 100;
		dur = bcnint * sched->dur / 100;
		htol16_ua_store(off, (uint8 *)&quiet->offset);
		htol16_ua_store(dur, (uint8 *)&quiet->duration);
		len += sizeof(dot11_quiet_t);

		WL_TMP(("wl%d: %s: length %u BSS %s\n",
		        wlc->pub->unit, __FUNCTION__, len,
		        bcm_ether_ntoa(&cfg->BSSID, eabuf)));
	}

	return len;
}

/** WiFi P2P IE's Device Info subelement fixed portion */
typedef struct wifi_p2p_di_fixed_s {
	uint8	id;				/**< ID: P2P_SEID_DEVINFO */
	uint8	len[2];				/**< length not including id, len fields */
	uint8	devaddr[ETHER_ADDR_LEN];	/**< P2P Device MAC address */
	uint8	cfg_meths[2];			/**< Config Methods: reg_prototlv.h WPS_CONFMET_* */
	uint8	pridt[P2P_DEV_TYPE_LEN];	/**< Primary Device Type */
	uint8	secdts;				/**< Number of Secondary Device Types */
} wifi_p2p_di_fixed_t;

#define P2P_DI_FIXED_LEN	19

/** WiFi P2P IE's Group Info subelement fixed portion */
typedef struct wifi_p2p_gi_fixed_s {
	uint8	id;				/**< ID: P2P_SEID_GROUP_INFO */
	uint8	len[2];				/**< length not including id, len fields */
} wifi_p2p_gi_fixed_t;

#define P2P_GI_FIXED_LEN	4

/** return the length of the P2P IE extra. */
static int
wlc_p2p_write_ie_extra_len(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, uint type)
{
	wlc_info_t *wlc = pm->wlc;
#if USE_DEF_P2P_IE
	struct scb_iter scbiter;
	struct scb *scb;
	int cid_len;
#endif /* USE_DEF_P2P_IE */
	int len = 0;
	bss_p2p_cmn_info_t *cmn;

	(void)wlc;

	ASSERT(cfg != NULL);

	if (!P2P_GO(wlc, cfg))
		return 0;

	cmn = BSS_P2P_CMN_INFO(pm, cfg);
	ASSERT(cmn != NULL);

	if (type != FC_PROBE_RESP || !(cmn->flags & BSS_P2P_CMN_INFO_P2P_IE))
		return 0;

#if USE_DEF_P2P_IE
	/* collect group info from all associated P2P clients? */
	if (!pm->insert_def_p2pie)
		return 0;
	cid_len = 0;
	FOREACH_BSS_SCB(wlc->scbstate, &scbiter, cfg, scb) {
		if (SCB_ASSOCIATED(scb) && SCB_P2P(scb)) {
			p2p_scb_cubby_t *cubby;
			wifi_p2p_info_se_t *cap = NULL;
			uint8 *cap_combined = NULL;
			wifi_p2p_di_fixed_t *di = NULL;
			uint8 *di_combined = NULL;

			cubby = P2P_SCB_CUBBY(pm, scb);
			ASSERT(cubby != NULL);

			/* find the Capability and DeviceInfo attributes from P2P IEs if any */
			cap = (wifi_p2p_info_se_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_P2P_INFO,
				cubby->as_ies, cubby->as_ies_len, &cap_combined, NULL, NULL);
			di = (wifi_p2p_di_fixed_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_DEV_INFO,
				cubby->as_ies, cubby->as_ies_len, &di_combined, NULL, NULL);
			ASSERT(cap != NULL);
			ASSERT(di != NULL);
			(void) cap;

			/* build Client Info Descriptor */
			cid_len += 1;
			cid_len += ETHER_ADDR_LEN;
			cid_len += ETHER_ADDR_LEN;
			cid_len += 1;
			if (ltoh16_ua(di->len) > ETHER_ADDR_LEN)
				cid_len += ltoh16_ua(di->len) - ETHER_ADDR_LEN;
			wlc_free_p2p_attr_buf(wlc, &cap_combined);
			cap = NULL;
			wlc_free_p2p_attr_buf(wlc, &di_combined);
			di = NULL;
		}
	}
	if (cid_len > 0) {
		len += P2P_IE_FIXED_LEN;
		len += TLV_HDR_LEN;
		len += cid_len;
	}
#endif /* USE_DEF_P2P_IE */

	return len;
}

/** add any prbresp specific IEs */
static int
wlc_p2p_write_ie_extra(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, uint type, uint8 *buf)
{
	wlc_info_t *wlc = pm->wlc;
#if USE_DEF_P2P_IE
	wifi_p2p_ie_t *pi;
	struct scb_iter scbiter;
	struct scb *scb;
	uint8 *gi;
	uint8 *cid;
	int cid_len;
#endif /* USE_DEF_P2P_IE */
	int len = 0;
	bss_p2p_cmn_info_t *cmn;

	(void)wlc;

	ASSERT(cfg != NULL);
	ASSERT(buf != NULL);

	if (!P2P_GO(wlc, cfg))
		return 0;

	cmn = BSS_P2P_CMN_INFO(pm, cfg);
	ASSERT(cmn != NULL);

	if (type != FC_PROBE_RESP || !(cmn->flags & BSS_P2P_CMN_INFO_P2P_IE))
		return 0;

#if USE_DEF_P2P_IE
	/* collect group info from all associated P2P clients? */
	if (!pm->insert_def_p2pie)
		return 0;
	pi = (wifi_p2p_ie_t *)(buf + len);
	gi = pi->subelts;
	cid = &gi[TLV_BODY_OFF];
	cid_len = 0;
	FOREACH_BSS_SCB(wlc->scbstate, &scbiter, cfg, scb) {
		if (SCB_ASSOCIATED(scb) && SCB_P2P(scb)) {
			p2p_scb_cubby_t *cubby;
			wifi_p2p_info_se_t *cap = NULL;
			uint8 *cap_combined = NULL;
			wifi_p2p_di_fixed_t *di = NULL;
			uint8 *di_combined = NULL;
			uint16 length;

			cubby = P2P_SCB_CUBBY(pm, scb);
			ASSERT(cubby != NULL);

			/* find the Capability and DeviceInfo attributes from P2P IEs if any */
			cap = (wifi_p2p_info_se_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_P2P_INFO,
				cubby->as_ies, cubby->as_ies_len, &cap_combined, NULL, NULL);
			di = (wifi_p2p_di_fixed_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_DEV_INFO,
				cubby->as_ies, cubby->as_ies_len, &di_combined, NULL, NULL);
			ASSERT(cap != NULL);
			ASSERT(di != NULL);

			/* build Client Info Descriptor */
			length = ltoh16_ua(di->len) + ETHER_ADDR_LEN + 1;
			htol16_ua_store(length, &cid[cid_len]);
			cid_len += 1;
			bcopy(&di->devaddr, &cid[cid_len], ETHER_ADDR_LEN);
			cid_len += ETHER_ADDR_LEN;
			bcopy(&scb->ea, &cid[cid_len], ETHER_ADDR_LEN);
			cid_len += ETHER_ADDR_LEN;
			cid[cid_len] = cap->dev;
			cid_len += 1;
			if (ltoh16_ua(di->len) > ETHER_ADDR_LEN) {
				bcopy((uint8 *)di + P2P_ATTR_HDR_LEN + ETHER_ADDR_LEN,
				      &cid[cid_len], ltoh16_ua(di->len) - ETHER_ADDR_LEN);
				cid_len += ltoh16_ua(di->len) - ETHER_ADDR_LEN;
			}
			wlc_free_p2p_attr_buf(wlc, &cap_combined);
			wlc_free_p2p_attr_buf(wlc, &di_combined);
		}
	}

	if (cid_len > 0) {
		len = P2P_IE_FIXED_LEN;
		len += TLV_HDR_LEN;
		len += cid_len;
		pi->id = DOT11_MNG_VS_ID;
		pi->len = len - TLV_HDR_LEN;
		bcopy(WFA_OUI, pi->OUI, WFA_OUI_LEN);
		pi->oui_type = WFA_OUI_TYPE_P2P;
		gi[P2P_ATTR_ID_OFF] = P2P_SEID_GROUP_INFO;
		htol16_ua_store((uint16)cid_len, &gi[P2P_ATTR_LEN_OFF]);
	}
#endif /* USE_DEF_P2P_IE */

	return len;
}

/* return the length of the P2P IE. */
static int
wlc_p2p_write_ie_assoc_len(wlc_p2p_info_t *pm, struct scb *scb, uint16 status)
{
	bool write;
	int len = 0;

	BCM_REFERENCE(pm);
	BCM_REFERENCE(status);

	ASSERT(scb != NULL);

	write = wlc_p2p_vndr_ie_getlen(pm->wlc, SCB_BSSCFG(scb),
	                               FC_ASSOC_RESP, VNDR_IE_ASSOCRSP_FLAG) == 0;

	/* write all attributes in the same P2P IE (assuming the total length
	 * won't exceed 251 bytes.
	 */
	if (write) {
		len += P2P_IE_FIXED_LEN;
#ifdef NOT_YET
		if (status != DOT11_SC_SUCCESS) {
			len += sizeof(wifi_p2p_status_se_t);
		}
#endif
	}

	return len;
}

static uint8 *
wlc_p2p_write_ie_assoc(wlc_p2p_info_t *pm, struct scb *scb, uint16 status, uint8 *buf)
{
	bool write;
	int len = 0;

	BCM_REFERENCE(pm);
	BCM_REFERENCE(status);

	ASSERT(scb != NULL);

	write = wlc_p2p_vndr_ie_getlen(pm->wlc, SCB_BSSCFG(scb),
	                               FC_ASSOC_RESP, VNDR_IE_ASSOCRSP_FLAG) == 0;

	/* write all attributes in the same P2P IE (assuming the total length
	 * won't exceed 251 bytes.
	 */
	if (write) {
		wifi_p2p_ie_t *pi = (wifi_p2p_ie_t *)buf;
		pi->id = DOT11_MNG_VS_ID;
		bcopy(WFA_OUI, pi->OUI, WFA_OUI_LEN);
		pi->oui_type = WFA_OUI_TYPE_P2P;
		len += P2P_IE_FIXED_LEN;
#ifdef NOT_YET
		if (status != DOT11_SC_SUCCESS) {
			wifi_p2p_status_se_t *st = (wifi_p2p_status_se_t *)(buf + len);
			st->eltId = P2P_SEID_STATUS;
			htol16_ua_store(sizeof(wifi_p2p_status_se_t) - P2P_ATTR_HDR_LEN, st->len);
			st->status = P2P_STATSE_FAIL_PROTO_ERROR;
			len += sizeof(wifi_p2p_status_se_t);
		}
#endif
		pi->len = len - TLV_HDR_LEN;
	}

	return buf + len;
}

/** applicable p2p management frames include only ofdm rates in the Supported Rates IE */
static bool
wlc_p2p_no_cck_rates(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	return !wlc_p2p_disc_state(pm) ||
	        cfg != pd->devcfg ||
	        pd->state != WL_P2P_DISC_ST_SCAN ||
	        !(pd->flags & P2P_FLAG_SCAN_ALL);
}

void
wlc_p2p_rateset_filter(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wlc_rateset_t *rs)
{
	/* always include OFDM rates only unless it is for P2P SCAN in discovery */
	if (wlc_p2p_no_cck_rates(pm, cfg))
		wlc_rateset_filter(rs, rs, FALSE, WLC_RATES_OFDM, RATE_MASK_FULL,
		                   wlc_get_mcsallow(pm->wlc, cfg));
}

/**
 * Figure out the TBTTs in remote TSF and local TSF times from the beacon/prbresp
 * timestamp and beacon interval fields and the beacon/prbresp rx timestamp.
 * 'tsf' carries out the local TSF time of the TBTT and 'bcn' carries out
 * the remote TSF time of the TBTT.
 */
static void
wlc_p2p_tbtt_calc_bss(wlc_p2p_info_t *pm, wlc_bss_info_t *bi,
	uint32 *tsf_h, uint32 *tsf_l, uint32 *bcn_h, uint32 *bcn_l)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;

	/* recover the local tsf value from the saved low 32 bits of the local tsf */
	wlc_read_tsf(wlc, tsf_l, tsf_h);
	if (*tsf_l < bi->rx_tsf_l)
		*tsf_h -= 1;
	*tsf_l = bi->rx_tsf_l;

	wlc_mcnx_tbtt_calc(mcnx, NULL, NULL, NULL, bi->bcn_prb, tsf_h, tsf_l, bcn_h, bcn_l);

	WL_P2P(("wl%d: %s: tbtt 0x%x%08x (local) 0x%x%08x (remote) offset 0x%x at tick 0x%x\n",
	        wlc->pub->unit, __FUNCTION__, *tsf_h, *tsf_l, *bcn_h, *bcn_l, *tsf_l - *bcn_l,
	        WL_P2P_TS(wlc)));
}

/** pick up the schedule info from beacon/prbresp and store it in p2p->sched */
static int
wlc_p2p_bcn_prb_noa(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p,
	struct dot11_bcn_prb *bcn, int bcn_len, bool *upd)
{
	wlc_info_t *wlc = pm->wlc;
	wifi_p2p_noa_se_t *se = NULL;
	uint8 *se_combined = NULL;
	int slen = 0;
	int err;
	wifi_p2p_noa_se_t one;
	bool u;

	ASSERT(p2p != NULL);

	/* Need to have a beacon/proberesp that has IEs */
	if (bcn_len < DOT11_BCN_PRB_LEN) {
		WL_ERROR(("wl%d: %s: Invalid beacon probe length %d\n",
		        WLCWLUNIT(wlc), __FUNCTION__, bcn_len));
		return BCME_BADARG;
	}

	/* find the NoA attribute from P2P IEs if any */
	se = (wifi_p2p_noa_se_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_ABSENCE,
		(uint8*)bcn + DOT11_BCN_PRB_LEN, bcn_len - DOT11_BCN_PRB_LEN,
		&se_combined, &slen, NULL);

	/* find the requested absence schedule (one-time schedule) and
	 * treat it as WLC_P2P_NOA_REQ_ABS type...
	 */
	/* we can only handle one requested absence schedule... */
	one.desc[0].cnt_type = 0;
	if (se != NULL && slen > P2P_NOA_SE_FIXED_LEN) {
		int cnt, i;

		cnt = (slen - P2P_NOA_SE_FIXED_LEN) / sizeof(wifi_p2p_noa_desc_t);
		for (i = 0; i < cnt; i ++) {
			if (se->desc[i].cnt_type != 1)
				continue;
			/* found and copy it over */
			bcopy((uint8 *)se, (uint8 *)&one, P2P_NOA_SE_FIXED_LEN);
			htol16_ua_store(P2P_NOA_SE_FIXED_LEN - P2P_ATTR_HDR_LEN, one.len);
			one.desc[0] = se->desc[i];
			/* remove it */
			if (i + 1 < cnt) {
				bcopy((uint8 *)&se->desc[i + 1], (uint8 *)&se->desc[i],
				      (cnt - i - 1) * sizeof(wifi_p2p_noa_desc_t));
			}
			slen -= sizeof(wifi_p2p_noa_desc_t);
			break;
		}
	}

	/* update schedule when:
	 * - set a new schedule, update or cancel the current absence schedule and the OppPS
	 */
	err = wlc_p2p_sched_noa(pm, p2p, WLC_P2P_NOA_ABS, se, slen, FALSE, upd);

	wlc_free_p2p_attr_buf(wlc, &se_combined);

	if (err != BCME_OK)
		return err;

	/* '*upd' being FALSE means the NoA index hasn't changed... */
	if (*upd == FALSE)
		return BCME_OK;

	/* no requested absence schedule */
	if (one.desc[0].cnt_type == 0)
		return BCME_OK;

	/* set the requested absence schedule separately... */
	err = wlc_p2p_sched_noa(pm, p2p, WLC_P2P_NOA_REQ_ABS, &one, (int)sizeof(one), TRUE, &u);

	return err;
}

/** parse P2P IE in beacons and update NoA schedule if needed */
int
wlc_p2p_recv_process_beacon(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	struct dot11_bcn_prb *bcn, int bcn_len)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	bool update = FALSE;
	int err;

	(void)wlc;

	ASSERT(cfg != NULL);
	ASSERT(P2P_CLIENT(wlc, cfg));

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	/* don't bother to do anything if not UP */
	if (!(p2p->flags & WLC_P2P_INFO_NET))
		return BCME_OK;

	/* pick up and update schedule */
	err = wlc_p2p_bcn_prb_noa(pm, p2p, bcn, bcn_len, &update);
	if (err == BCME_OK && update) {
#ifdef WLMCHAN
		/* if there is an update, notify mchan too if enabled */
		if (MCHAN_ENAB(wlc->pub))
			wlc_mchan_reset_params(wlc->mchan);
#endif /* WLMCHAN */
		wlc_p2p_sched_norm(pm, p2p);
		wlc_p2p_sched_upd(pm, p2p);
		if (p2p->sched[0].cnt > 1) {
			wlc_p2p_noa_upd_notif(pm, cfg);
		}
		wlc_p2p_noa_upd(pm, p2p);
	}

	return err;
}

/** convert wl_p2p_sched_desc_t to wifi_p2p_noa_desc_t */
static int
wlc_p2p_sched_wl2se(wl_p2p_sched_desc_t *desc, wifi_p2p_noa_desc_t *sedesc, int cnt)
{
	int len = 0;
	int i;

	for (i = 0; i < cnt; i ++) {
		htol32_ua_store(desc[i].start, (uint8 *)&sedesc[i].start);
		htol32_ua_store(desc[i].interval, (uint8 *)&sedesc[i].interval);
		htol32_ua_store(desc[i].duration, (uint8 *)&sedesc[i].duration);
		sedesc[i].cnt_type = (uint8)desc[i].count;
		len += sizeof(*sedesc);
	}

	return len;
}

/**
 * Move the start time in the descriptor to right before or after the current TSF, update the count
 * in the descriptor accordingly.
 */
static bool
_wlc_p2p_sched_adv(wl_p2p_sched_desc_t *desc, uint32 tsf, uint32 start, uint32 cnt)
{
	uint32 count = desc->count;
	bool ret = FALSE;

	/* if we are past the absence duration of this interval,
	 * start with the next interval
	 */
	if (tsf - start > desc->duration) {
		cnt ++;
		start += desc->interval;
	}

	/* update the count */
	if (count != WL_P2P_SCHED_REPEAT) {
		count -= MIN(cnt, count);
		if (count == 0) {
			ret = TRUE;
			goto exit;
		}
	}

exit:
	desc->start = start;
	desc->count = count;

	return ret;
}

static bool
wlc_p2p_sched_adv(wl_p2p_sched_desc_t *desc, uint32 tsf)
{
	uint32 start = desc->start;
	uint32 interval = desc->interval;
	uint32 cnt;

	/* calculate how many intervals from the start have passed */
	cnt = (tsf - start) / interval;
	start += cnt * interval;

	return _wlc_p2p_sched_adv(desc, tsf, start, cnt);
}

/**
 * adjust the current descriptor start time back by one NoA interval if the tsf
 * is in absence period; move to the next descriptor if the current descriptor expired;
 * calculate the current schedule based on the following factors and the orders:
 * - requested absence
 * - scheduled absence
 * return TRUE to indicate the schedule has changed.
 */
static bool
wlc_p2p_sched_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	uint32 tsf_l;
	wlc_p2p_sched_t *sched;
	wlc_p2p_sched_t *sched_req;
	wl_p2p_sched_desc_t *desc;
	wl_p2p_sched_desc_t *desc_req;
	uint8 idx;
	uint8 idx_req;
	uint32 cnt;
	bool update = FALSE;
	bool had;
	int s;
	wlc_bsscfg_t *cfg;
	uint8 abs[] = {WLC_P2P_NOA_ABS, WLC_P2P_NOA_REQ_ABS};
	uint i;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	if (!wlc->pub->up)
		return FALSE;

	/* remote TSF */
	tsf_l = R_REG(wlc->osh, &wlc->regs->tsf_timerlow);
	tsf_l = wlc_mcnx_l2r_tsf32(mcnx, cfg, tsf_l);

	/* 1. skip schedules that are already past the current TSF and
	 * adjust schedules that are remaining to after the current TSF
	 */

	for (i = 0; i < ARRAYSIZE(abs); i ++) {
		sched = &p2p->sched[abs[i]];
		if ((desc = sched->desc) != NULL) {

			idx = sched->idx;

			while (idx < sched->cnt) {

				update = TRUE;

				/* this one is done, move on */
				if (desc[idx].count == WL_P2P_SCHED_RSVD) {
					WL_P2P(("wl%d: sched %d desc %d expired, "
					        "move to the next one\n",
					        wlc->pub->unit, abs[i], idx));
					idx ++;
					continue;
				}

				/* advance the schedule to the current TSF */
				if (!wlc_p2p_noa_start_future(pm, p2p, desc[idx].start, tsf_l) &&
				    wlc_p2p_sched_adv(&desc[idx], tsf_l)) {
					WL_P2P(("wl%d: sched %d desc %d expired, "
					        "start 0x%08X tsf 0x%08X\n",
					        wlc->pub->unit, abs[i], idx,
					        desc[idx].start, tsf_l));
					idx ++;
					continue;
				}

				break;
			}

			sched->idx = idx;

		}
	}

	/* 2. calculate the current schedule based on all available schedules and
	 * their priorities.
	 */

	/* 1). reset the current schedule */
	had = (p2p->flags & WLC_P2P_INFO_CUR) ? TRUE : FALSE;
	bzero(&p2p->cur, sizeof(p2p->cur));
	p2p->flags &= ~WLC_P2P_INFO_CUR;

	/* 2). check if the scheduled absence and the requested absence both exist,
	 * if they do merge the two and split the combined schedule into smaller and SHM
	 * acceptable pieces.
	 *
	 * scheduled     |     +-----+     +-----+   +-----+     +-----+   +-----+
	 * absence       | ... | ABS | ... | ABS |   | ABS | ... | ABS |   | ABS | ...
	 * --------------+-----+-----+-----------+---+--+--+-----+--+--+---+-----+-----
	 * requested     |                       |   |   | ABS       | |   |
	 * absence       |                       |   |   +-----------+ |   |
	 *               | <======== s1 =========+   +======= s2 ======+   +=== s3 ===>
	 *               TSF                     T1  T2                    T3
	 */
	sched = &p2p->sched[WLC_P2P_NOA_ABS];
	sched_req = &p2p->sched[WLC_P2P_NOA_REQ_ABS];
	desc = sched->desc;
	desc_req = sched_req->desc;
	idx = sched->idx;
	idx_req = sched_req->idx;
	if (desc != NULL && idx < sched->cnt &&
	    desc_req != NULL && idx_req < sched_req->cnt) {

		/* the number of scheduled absence intervals from the start
		 * of the scheduled absence schedule to the start of the requested
		 * absence schedule.
		 */
		if (desc[idx].count == WL_P2P_SCHED_REPEAT ||
		    desc[idx].start + desc[idx].interval * desc[idx].count >
		    desc_req[idx_req].start)
			cnt = ABS((int32)(desc_req[idx_req].start - desc[idx].start)) /
			        desc[idx].interval;
		else
			cnt = desc[idx].count;

		/* s1 - scheduled absence schedule stops at T1 */
		if (cnt > 0) {
			/* use the scheduled absence schedule as the current schedule with
			 * the number of absence intervals modified to have the schedule
			 * stop right before the interval whose absence period overlaps with
			 * the requested absence schedule.
			 */
			bcopy(&desc[idx], &p2p->cur, sizeof(p2p->cur));
			p2p->cur.count = cnt;

			p2p->sidx = WLC_P2P_NOA_ABS;

			WL_P2P(("wl%d: CUR: split sched %d at tick 0x%x: "
			        "start 0x%x interval %u duration %u count %u\n",
			        wlc->pub->unit, p2p->sidx, WL_P2P_TS(wlc),
			        p2p->cur.start, p2p->cur.interval,
			        p2p->cur.duration, p2p->cur.count));
		}
		/* s2 - requested absence schedule starts at T2 */
		else {
			uint32 start = MIN(desc[idx].start, desc_req[idx_req].start);
			uint32 stop = desc_req[idx_req].start + desc_req[idx_req].duration;
			/* use the requested absence schedule as the current schedule with
			 * both start and stop times extended if they overlap with
			 * the schedule absence intervals.
			 */
			/* the number of scheduled absence intervals from the start
			 * of the scheduled absence schedule or the requested absence schedule,
			 * which ever comes earlier, to the end of the requested absence schedule.
			 */
			cnt = ABS((int32)(stop - start)) / desc[idx].interval;
			/* extend the requested absence to the end of the absence duration
			 * of the scheduled absence schedule that possibly overlaps with
			 * the requested absence schedule.
			 */
			if (stop > desc[idx].start + desc[idx].interval * cnt)
				stop = desc[idx].start + desc[idx].interval * cnt +
				        desc[idx].duration;
			p2p->cur.start = start;
			p2p->cur.duration = ABS((int32)(stop - start));
			p2p->cur.interval = p2p->cur.duration;
			p2p->cur.count = 1;

			p2p->sidx = WLC_P2P_NOA_REQ_ABS;

			WL_P2P(("wl%d: CUR: use sched %d at tick 0x%x: "
			        "start 0x%x interval %u duration %u count %u\n",
			        wlc->pub->unit, p2p->sidx, WL_P2P_TS(wlc),
			        p2p->cur.start, p2p->cur.interval,
			        p2p->cur.duration, p2p->cur.count));
		}
		p2p->flags |= WLC_P2P_INFO_CUR;

		/* s3 - scheduled absence schedule resumes at T3,
		 * which will be taken care in the next else block...
		 */
	}
	else
	if (((s = WLC_P2P_NOA_REQ_ABS, sched = &p2p->sched[s], (desc = sched->desc) != NULL) &&
	     (idx = sched->idx, idx < sched->cnt)) ||
	    ((s = WLC_P2P_NOA_ABS, sched = &p2p->sched[s], (desc = sched->desc) != NULL) &&
	     (idx = sched->idx, idx < sched->cnt))) {

		bcopy(&sched->desc[idx], &p2p->cur, sizeof(p2p->cur));
		p2p->flags |= WLC_P2P_INFO_CUR;
		p2p->sidx = (uint8)s;

		WL_P2P(("wl%d: CUR: single sched %d at tick 0x%x: "
		        "start 0x%x interval %u duration %u count %u\n",
		        wlc->pub->unit, s, WL_P2P_TS(wlc),
		        p2p->cur.start, p2p->cur.interval,
		        p2p->cur.duration, p2p->cur.count));
	}
	if (p2p->flags & WLC_P2P_INFO_CUR) {

		/* init all side band info w.r.t the current schedule... */
		ASSERT(p2p->sidx < WLC_P2P_MAX_SCHED);
		sched = &p2p->sched[p2p->sidx];
		p2p->action = sched->action;
		sched->flags |= WLC_P2P_SCHED_RUN;

		/* adjust the NoA count as needed */

		/* find the NoA count the SHM BSS block can take */
		if (p2p->cur.count == WL_P2P_SCHED_REPEAT ||
		    p2p->cur.count > P2P_NOA_MAX_CNT)
			cnt = P2P_NOA_MAX_CNT;
		else
			cnt = p2p->cur.count;
		/* limit the NoA count to what's needed to update NoA index every 2^31 us */
		if (p2p->sidx == WLC_P2P_NOA_ABS &&
		    P2P_GO(wlc, cfg)) {
			uint32 r, c;

			/* save the start time */
			if (!(p2p->flags & WLC_P2P_INFO_STRT)) {

				ASSERT(sched->desc != NULL);
				ASSERT(sched->idx < sched->cnt);
				p2p->start = sched->desc[sched->idx].start;
				p2p->flags |= WLC_P2P_INFO_STRT;

				WL_P2P(("wl%d: remember NoA schedule commence time 0x%x\n",
				        wlc->pub->unit, p2p->start));
			}

			/* adjust the count */
			r = P2P_NOA_MAX_PRD - (p2p->cur.start - p2p->start);
			c = r / p2p->cur.interval + 1;
			if (cnt > c)
				cnt = c;
		}
		/* it is the NoA count we'll program the BSS block */
		p2p->cur.count = cnt;

		WL_P2P(("wl%d: CUR: use NoA count %u\n", wlc->pub->unit, cnt));
	}

	/* remove a schedule... */
	if (!(p2p->flags & WLC_P2P_INFO_CUR) &&
	    had) {
		WL_P2P(("wl%d: %s: reset schedule at tick 0x%x\n",
		        wlc->pub->unit, __FUNCTION__, WL_P2P_TS(wlc)));
		update = TRUE;
#ifdef WLMCHAN
		/* clean up mchan related abs/psc states */
		if (MCHAN_ENAB(wlc->pub)) {
			wlc_mchan_client_noa_clear(wlc->mchan, cfg);
		}
#endif /* WLMCHAN */
	}

	return update;
}

/** adjust the NoA to the next interval after the current TSF */
static void
wlc_p2p_sched_adj(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_sched_t *sched;
	wl_p2p_sched_desc_t *desc;
	wlc_bsscfg_t *cfg;
	uint32 start;
	uint32 tsf_l;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;

	ASSERT(p2p != NULL);
	ASSERT(p2p->flags & WLC_P2P_INFO_CUR);

	if (p2p->flags & WLC_P2P_INFO_DLY)
		return;

	sched = &p2p->sched[p2p->sidx];
	if (sched->idx >= sched->cnt)
		return;

	ASSERT(sched->desc != NULL);

	desc = &sched->desc[sched->idx];

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	tsf_l = R_REG(wlc->osh, &wlc->regs->tsf_timerlow);

	/* local TSF */
	start = wlc_mcnx_r2l_tsf32(mcnx, cfg, desc->start);
	start = wlc_p2p_noa_start_next(tsf_l, start, desc->interval);
	/* remote TSF */
	desc->start = wlc_mcnx_l2r_tsf32(mcnx, cfg, start);

	if (desc->count != WL_P2P_SCHED_REPEAT)
		desc->count -= MIN(desc->count, p2p->cur.count);

	WL_P2P(("wl%d: adjust sched %d desc %d at tick 0x%x: start 0x%x count %u\n",
	        wlc->pub->unit, p2p->sidx, sched->idx, WL_P2P_TS(wlc),
	        desc->start, desc->count));
}

/**
 * Renew the NoA schedule after:
 * - it has been running for 2^31 us
 */
static bool
wlc_p2p_sched_renew(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_sched_t *sched;
	bool new_sched = FALSE;

	ASSERT(p2p != NULL);
	ASSERT(p2p->flags & WLC_P2P_INFO_CUR);

	if (p2p->flags & WLC_P2P_INFO_DLY)
		return FALSE;

	ASSERT(p2p->bsscfg != NULL);
	ASSERT(P2P_GO(wlc, p2p->bsscfg));

	sched = &p2p->sched[p2p->sidx];
	if (sched->idx >= sched->cnt)
		return FALSE;

	/* publish a new schedule if it has been running for longer than 2^31 us */
	if (p2p->sidx == WLC_P2P_NOA_ABS &&
	    (p2p->flags & WLC_P2P_INFO_STRT)) {
		uint32 tsf_l = R_REG(wlc->osh, &wlc->regs->tsf_timerlow);
		uint32 start = p2p->cur.start;

		/* p2p->cur.start, and p2p->start in GO are all in local TSF time */
		start = wlc_p2p_noa_start_next(tsf_l, start, p2p->cur.interval);
		if (U32_DUR(p2p->start, start) > P2P_NOA_MAX_PRD)
			new_sched = TRUE;
	}
	/* publish a new schedule since we are done... */
	else if (p2p->sidx == WLC_P2P_NOA_REQ_ABS) {
		new_sched = TRUE;
	}

	return  new_sched;
}

/** alloc wl_p2p_sched_desc_t for bss_p2p_info_t.sched[sched] */
static int
wlc_p2p_sched_alloc(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, wlc_p2p_sched_t *s, int count)
{
	wlc_info_t *wlc = pm->wlc;

	(void)wlc;

	BCM_REFERENCE(p2p);

	if (s->desc != NULL) {
		MFREE(wlc->osh, s->desc, s->cnt * sizeof(wl_p2p_sched_desc_t));
		bzero(s, sizeof(*s));
	}

	if ((s->desc = MALLOCZ(wlc->osh, count * sizeof(wl_p2p_sched_desc_t))) == NULL) {
		WL_ERROR((WLC_MALLOC_ERR, WLCWLUNIT(wlc), __FUNCTION__,
			(int)(count * sizeof(wl_p2p_sched_desc_t)), MALLOCED(wlc->osh)));
		return BCME_NOMEM;
	}

	s->idx = 0;
	s->cnt = (uint8)count;

	return BCME_OK;
}

/** compare the NoA with what we have had to see if they are the 'same' */
static int
wlc_p2p_sched_cmp(bss_p2p_info_t *p2p, wifi_p2p_noa_se_t *se)
{
	ASSERT(p2p != NULL);
	ASSERT(se != NULL);

	if ((p2p->flags & WLC_P2P_INFO_ID) &&
	    p2p->id == se->index)
		return 0;

	return 1;
}

/** validate wl_p2p_sched_t */
static int
wlc_p2p_sched_valid(wl_p2p_sched_t *s, int slen)
{
	int cnt;
	int i;

	/* type check */
	switch (s->type) {
	case WL_P2P_SCHED_TYPE_ABS:
	case WL_P2P_SCHED_TYPE_REQ_ABS:
		break;
	default:
		WL_ERROR(("%s: type %u is not supported\n", __FUNCTION__, s->type));
		return BCME_BADARG;
	}

	/* action check */
	switch (s->action) {
	case WL_P2P_SCHED_ACTION_NONE:
	case WL_P2P_SCHED_ACTION_DOZE:
	case WL_P2P_SCHED_ACTION_GOOFF:
	case WL_P2P_SCHED_ACTION_RESET:
		break;
	default:
		WL_ERROR(("%s: action %u is not supported\n", __FUNCTION__, s->action));
		return BCME_BADARG;
	}

	/* length check */
	if (slen >= (int)OFFSETOF(wl_p2p_sched_t, desc) &&
	    (slen - (int)OFFSETOF(wl_p2p_sched_t, desc)) % sizeof(wl_p2p_sched_desc_t) != 0) {
		WL_ERROR(("%s: wrong parm buffer length %d\n", __FUNCTION__, slen));
		return BCME_BADARG;
	}

	/* type, action, option, # descriptors, and schedules inter-dependent checks... */
	cnt = (slen - WL_P2P_SCHED_FIXED_LEN) / sizeof(wl_p2p_sched_desc_t);
	if (s->type == WL_P2P_SCHED_TYPE_REQ_ABS) {
		if (s->option == WL_P2P_SCHED_OPTION_BCNPCT) {
			WL_ERROR(("%s: invalid option %u for type %u\n",
			          __FUNCTION__, s->option, s->type));
			return BCME_BADARG;
		}
		if (s->action != WL_P2P_SCHED_ACTION_RESET && cnt != 1) {
			WL_ERROR(("%s: invalid # descriptors %u for type %u\n",
			          __FUNCTION__, cnt, s->type));
			return BCME_BADARG;
		}
	}
	for (i = 0; i < cnt; i ++) {
		switch (s->option) {
		case WL_P2P_SCHED_OPTION_BCNPCT:
			if (s->desc[i].start >= 100) {
				WL_ERROR(("%s: wrong start 0x%x in desc %d\n",
				          __FUNCTION__, s->desc[i].start, i));
				return BCME_BADARG;
			}
			if (s->desc[i].duration == 0 || s->desc[i].duration >= 100) {
				WL_ERROR(("%s: wrong duration %d in desc %d\n",
				          __FUNCTION__, s->desc[i].duration, i));
				return BCME_BADARG;
			}
			if (s->desc[i].start + s->desc[i].duration > 100) {
				WL_ERROR(("%s: wrong start 0x%x + duration %d in desc %d\n",
				          __FUNCTION__, s->desc[i].start, s->desc[i].duration, i));
				return BCME_BADARG;
			}
			break;
		case WL_P2P_SCHED_OPTION_TSFOFS:
			if (s->desc[i].start < P2P_NOA_MIN_OFS) {
				WL_ERROR(("%s: minimum offset %d in desc %d is required\n",
				          __FUNCTION__, P2P_NOA_MIN_OFS, i));
				return BCME_BADARG;
			}
			/* FALL THROUGH */
		default:
			if (s->desc[i].interval == 0 || s->desc[i].interval > P2P_NOA_MAX_INT) {
				WL_ERROR(("%s: invalid interval %u in desc %d\n",
				          __FUNCTION__, s->desc[i].interval, i));
				return BCME_BADARG;
			}
			if (s->desc[i].duration == 0 ||
			    s->desc[i].duration > s->desc[i].interval) {
				WL_ERROR(("%s: invalid duration %u in desc %d\n",
				          __FUNCTION__, s->desc[i].duration, i));
				return BCME_BADARG;
			}
			break;
		}
	}

	return BCME_OK;
}

/**
 * Normalize a beacon percentage based schedule and set the start time
 * of such schedule to the next interval past the tsf, adjust the start
 * time back by one interval if the tsf is in absence period, called when
 * an AP BSS is brought up or when a new NoA schedule is set;
 * normalize a regular start/interval/duration/count schedule and
 * adjust the start time of all descriptors to the current interval where
 * the tsf is in, move the start time to the next interval if the tsf
 * is in the presence period, called when receiving the first beacon
 * after association or when the GO publishes a new schedule.
 */
static bool
wlc_p2p_sched_norm(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_sched_t *sched;
	wlc_bsscfg_t *cfg;
	uint32 tsf_l, tsf_h;
	uint32 bcnint;
	uint8 abs[] = {WLC_P2P_NOA_ABS, WLC_P2P_NOA_REQ_ABS};
	uint i;
	bool valid = FALSE;
	int idx;
	uint32 rtsf_l;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	ASSERT(p2p->flags & WLC_P2P_INFO_NET);

	wlc_read_tsf(wlc, &tsf_l, &tsf_h);

	for (i = 0; i < ARRAYSIZE(abs); i ++) {
		sched = &p2p->sched[abs[i]];

		if (sched->idx >= sched->cnt)
			continue;

		ASSERT(sched->desc != NULL);

		switch (sched->option) {
		case WL_P2P_SCHED_OPTION_BCNPCT:
			/* convert beacon interval percentage based schedule into
			 * the normal schedule format
			 */
			if (P2P_GO(wlc, cfg)) {
				uint32 start, dur;
				uint32 tsfo_h, tsfo_l;
				uint32 tbtt_h, tbtt_l;

				ASSERT(cfg->associated);

				bcnint = cfg->current_bss->beacon_period << 10;

				ASSERT(sched->start + sched->dur <= 100);
				start = bcnint * sched->start / 100;
				dur = bcnint * sched->dur / 100;

				/* offset to the last tbtt */
				tsfo_l = wlc_calc_tbtt_offset(bcnint >> 10, tsf_h, tsf_l);
				/* the last tbtt */
				tbtt_h = tsf_h;
				tbtt_l = tsf_l;
				wlc_uint64_sub(&tbtt_h, &tbtt_l, 0, tsfo_l);
				/* the next tbtt if the tsf is past the absence period and
				 * in the presence period
				 */
				tsfo_h = tsf_h;
				tsfo_l = tsf_l;
				wlc_uint64_sub(&tsfo_h, &tsfo_l, tbtt_h, tbtt_l);
				if (tsfo_l >= start + dur)
					wlc_uint64_add(&tbtt_h, &tbtt_l, 0, bcnint);

				sched->desc[0].start = tbtt_l + start;
				sched->desc[0].interval = bcnint;
				sched->desc[0].duration = dur;
				sched->desc[0].count = WL_P2P_SCHED_REPEAT;

				WL_P2P(("wl%d: normalize sched %d desc 0 at tick 0x%x: "
				        "start 0x%x duration %u "
				        "(opt %u start %u%% dur %u%% of bi %u tbtt 0x%x)\n",
				        wlc->pub->unit, abs[i], WL_P2P_TS(wlc),
				        sched->desc[0].start, sched->desc[0].duration,
				        sched->option, sched->start, sched->dur, bcnint, tbtt_l));
			}
			break;

		case WL_P2P_SCHED_OPTION_NORMAL:
			/* remote TSF */
			/* GO has the same local TSF and remote TSF */
			rtsf_l = wlc_mcnx_l2r_tsf32(wlc->mcnx, cfg, tsf_l);

			/* bring the schedule into the current interval covering the TSF
			 * if the start is in the past
			 */
			for (idx = sched->idx; idx < sched->cnt; idx ++) {
				uint32 start = sched->desc[idx].start;

				/* do nothing if the start is in the future */
				if (wlc_p2p_noa_start_future(pm, p2p, start, rtsf_l))
					continue;

				/* TODO: fixup bogus schedule... */
				if (sched->desc[idx].interval == 0) {
					WL_INFORM(("wl%d: bogus NoA interval, "
					           "setting to maximum...\n",
					           wlc->pub->unit));
					sched->desc[idx].interval = P2P_NOA_MAX_INT;
				}

				WL_P2P(("wl%d: normalize sched %d desc %d "
				        "start 0x%x at tick 0x%x ",
				        wlc->pub->unit, abs[i], idx, start, tsf_l));
				wlc_p2p_sched_adv(&sched->desc[idx], rtsf_l);
				WL_P2P(("new start 0x%x\n", sched->desc[idx].start));
			}
			break;

		case WL_P2P_SCHED_OPTION_TSFOFS:
			/* bring the schedule to the offset */
			if (P2P_GO(wlc, cfg) &&
			    !(sched->flags & WLC_P2P_SCHED_NORM)) {

				WL_P2P(("wl%d: normalize sched %d desc 0 "
				        "start 0x%x at tick 0x%x ",
				        wlc->pub->unit, abs[i], sched->desc[0].start, tsf_l));
				sched->desc[0].start += tsf_l;
				WL_P2P(("new start 0x%x\n", sched->desc[0].start));
			}
			break;
		}

		/* mark the schedule has been normalized in case someone doesn't
		 * want to do it again i.e. WL_P2P_SCHED_OPTION_TSFOFS option...
		 */
		if (P2P_GO(wlc, cfg))
			sched->flags |= WLC_P2P_SCHED_NORM;

		valid = TRUE;
	}

	return valid;
}

/** turn suppression queue on/off based on tx stop/start state. */
static void
wlc_p2p_abs_q_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, uint8 state)
{
	wlc_bsscfg_t *cfg;
	void (*txqupd)(wlc_bsscfg_psq_info_t *psqi, wlc_bsscfg_t *cfg);

	BCM_REFERENCE(pm);

	ASSERT(p2p != NULL);

	txqupd = state == ON ? wlc_bsscfg_tx_start : wlc_bsscfg_tx_stop;

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	txqupd(pm->wlc->psqi, cfg);
}

#ifdef ROBUST_DISASSOC_TX
bool wlc_p2p_get_noa_status(wlc_info_t *wlc, wlc_bsscfg_t *cfg)
{
	bss_p2p_info_t *p2p;
	wlc_p2p_info_t *pm = wlc->p2p;

	p2p = BSS_P2P_INFO(pm, cfg);
	if (p2p == NULL) {
		return FALSE;
	}
	if (P2P_CLIENT(wlc, cfg) && (p2p->flags & WLC_P2P_INFO_OPS)) {
		return TRUE;
	}

	return FALSE;
}
#endif /* ROBUST_DISASSOC_TX */

/** cancel/set/update the NoA schedules in bss_p2p_info_t structure */
static int
wlc_p2p_sched_noa(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p, int sched,
	wifi_p2p_noa_se_t *se, int slen, bool force, bool *update)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_sched_t *s;
	int cnt;
	int i;
	wlc_bsscfg_t *cfg;

	(void)wlc;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	s = &p2p->sched[sched];

	/* cancel NoA and OppPS */
	if (slen == 0) {
		if (p2p->flags & WLC_P2P_INFO_OPS) {
			if (p2p->ops) {
				p2p->ops = FALSE;
				p2p->ctw = 0;
				WL_P2P(("wl%d: %s: reset OppPS at tick 0x%x\n",
				        wlc->pub->unit, __FUNCTION__, WL_P2P_TS(wlc)));
			}
			*update = TRUE;
			p2p->flags &= ~WLC_P2P_INFO_OPS;
		}
		if (s->desc != NULL) {
			MFREE(wlc->osh, s->desc, s->cnt * sizeof(wl_p2p_sched_desc_t));
			bzero(s, sizeof(*s));
			*update = TRUE;
			WL_P2P(("wl%d: %s: reset schedule %d at tick 0x%x\n",
			        wlc->pub->unit, __FUNCTION__, sched, WL_P2P_TS(wlc)));
		}
		p2p->flags &= ~WLC_P2P_INFO_ID;

		/* If the bss was stopped, restart it */
		wlc_p2p_abs_q_upd(pm, p2p, ON);

		if (P2P_GO(wlc, cfg)) {
			/* unblock APSD delivery and PS poll response frame */
			wlc_p2p_tx_block(pm, cfg, FALSE);
		}
		else /* if (P2P_CLIENT(wlc, cfg)) */ {
#ifdef WME
			/* disable APSD re-trigger */
			if (WME_ENAB(wlc->pub))
				wlc_p2p_apsd_retrigger_upd(pm, cfg, FALSE);
#endif
			/* disable PS poll resend */
			wlc_p2p_pspoll_resend_upd(pm, cfg, FALSE);
		}

		return BCME_OK;
	}

	/* sanity check */
	if (slen < P2P_NOA_SE_FIXED_LEN ||
	    (slen - P2P_NOA_SE_FIXED_LEN) % sizeof(wifi_p2p_noa_desc_t) != 0) {
		WL_ERROR(("wl%d: NoA attribute length %d is wrong\n",
		          wlc->pub->unit, slen));
		return BCME_BADARG;
	}

	/* is it a new schedule? */
	if (!force && wlc_p2p_sched_cmp(p2p, se) == 0)
		return BCME_OK;

	WL_P2P(("wl%d: update sched %d at tick 0x%x\n",
	        wlc->pub->unit, sched, WL_P2P_TS(wlc)));

	/* Check some obvious mis-configurations and don't commit it if there is any error(s). */
	/* GO's schedule validation is done in wlc_p2p_sched_valid() so here it is redundant. */
	if (P2P_CLIENT(wlc, cfg) &&
	    slen > P2P_NOA_SE_FIXED_LEN) {
		cnt = (slen - P2P_NOA_SE_FIXED_LEN) / sizeof(wifi_p2p_noa_desc_t);
		for (i = 0; i < cnt; i ++) {
			uint32 interval = ltoh32_ua((uint8 *)&se->desc[i].interval);
			uint32 duration = ltoh32_ua((uint8 *)&se->desc[i].duration);
			if (interval == 0 || duration == 0 ||
			    duration > interval) {
				WL_ERROR(("wl%d: invalid NoA, interval %u duration %u\n",
				          wlc->pub->unit, interval, duration));
				return BCME_ERROR;
			}
		}
	}

	/* set a new schedule or update the current schedule */
	if (slen > P2P_NOA_SE_FIXED_LEN) {
		cnt = (slen - P2P_NOA_SE_FIXED_LEN) / sizeof(wifi_p2p_noa_desc_t);
		if (wlc_p2p_sched_alloc(pm, p2p, s, cnt) != BCME_OK) {
			return BCME_ERROR;
		}
		for (i = 0; i < cnt; i ++) {
			s->desc[i].start = ltoh32_ua((uint8 *)&se->desc[i].start);
			s->desc[i].interval = ltoh32_ua((uint8 *)&se->desc[i].interval);
			s->desc[i].duration = ltoh32_ua((uint8 *)&se->desc[i].duration);
			s->desc[i].count = se->desc[i].cnt_type;
			WL_P2P(("wl%d: start 0x%x interval %u duration %u count %u\n",
			        wlc->pub->unit,
			        s->desc[i].start, s->desc[i].interval,
			        s->desc[i].duration, s->desc[i].count));
		}
		s->action = WL_P2P_SCHED_ACTION_NONE;
		s->option = WL_P2P_SCHED_OPTION_NORMAL;
	}

	/* set OppPS and CTWindow */
	if (sched == WLC_P2P_NOA_ABS) {
		p2p->ops = (se->ops_ctw_parms & P2P_NOA_OPS_MASK) ? TRUE : FALSE;
		p2p->ctw = se->ops_ctw_parms & P2P_NOA_CTW_MASK;

		p2p->flags |= WLC_P2P_INFO_OPS;

		WL_P2P(("wl%d: update OppPS %u CTWindow %u at tick 0x%x\n",
		        wlc->pub->unit, p2p->ops, p2p->ctw, WL_P2P_TS(wlc)));
	}

	/* record new NoA attribute index */
	if (!force) {
		p2p->id = se->index;

		p2p->flags |= WLC_P2P_INFO_ID;

		WL_P2P(("wl%d: take NoA index %d at tick 0x%x\n",
		        wlc->pub->unit, p2p->id, WL_P2P_TS(wlc)));
	}

	*update = TRUE;

	return BCME_OK;
}

/** set OppPS and CTWindow */
int
wlc_p2p_ops_set(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wl_p2p_ops_t *ops)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;

	(void)wlc;

	ASSERT(cfg != NULL);

	/* sanity checks */
	if (!P2P_GO(wlc, cfg)) {
		WL_ERROR(("wl%d: bsscfg %d is not a P2P GO\n",
		          wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
		return BCME_ERROR;
	}

	if (ops->ops && ops->ctw < P2P_CTW_MIN) {
		WL_ERROR(("wl%d: CTWindow should be at least %d\n",
		          wlc->pub->unit, P2P_CTW_MIN));
		return BCME_BADARG;
	}

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	if (p2p->ops == ops->ops) {
		if (p2p->ops) {
			if (p2p->ctw == ops->ctw) {
				WL_P2P(("wl%d: no changes to OppPS and CTWindow\n",
				        wlc->pub->unit));
				return BCME_OK;
			}
		}
		else {
			WL_P2P(("wl%d: no changes to OppPS\n", wlc->pub->unit));
			return BCME_OK;
		}
	}

	p2p->ops = ops->ops;
	p2p->ctw = ops->ctw;

	p2p->flags |= WLC_P2P_INFO_OPS;

	WL_P2P(("wl%d: %s: set OppPS %d CTWindow %d at tick 0x%x\n",
	        wlc->pub->unit, __FUNCTION__, p2p->ops, p2p->ctw, WL_P2P_TS(wlc)));

	if (p2p->flags & WLC_P2P_INFO_NET) {
		wlc_p2p_id_upd(pm, p2p);
		wlc_p2p_bcn_upd(pm, p2p);
		wlc_p2p_noa_upd(pm, p2p);
	}

	return BCME_OK;
}

static int
wlc_p2p_ops_get(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wl_p2p_ops_t *ops)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;

	(void)wlc;

	ASSERT(cfg != NULL);

	/* sanity checks */
	if (!P2P_GO(wlc, cfg)) {
		WL_ERROR(("wl%d: bsscfg %d is not a P2P GO\n",
		          wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
		return BCME_ERROR;
	}

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	if (!(p2p->flags & WLC_P2P_INFO_NET)) {
		WL_ERROR(("wl%d: bsscfg %d is not initialized\n",
		          wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
		return BCME_NOTUP;
	}
	if (!(p2p->flags & WLC_P2P_INFO_OPS)) {
		WL_ERROR(("wl%d: bsscfg %d no CTWindow/OppPS\n",
		          wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
		return BCME_ERROR;
	}

	ops->ops = p2p->ops;
	ops->ctw = p2p->ctw;

	return BCME_OK;
}

/** set a new scheduled absence. */
int
wlc_p2p_noa_set(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wl_p2p_sched_t *s, int slen)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	bool reset = FALSE;
	bool update = FALSE;
	wifi_p2p_noa_se_t *se = NULL;
	int sched = 0;
	int cnt = 0;
	int err = BCME_OK;

	(void)wlc;

	ASSERT(cfg != NULL);

	/* parms validation */
	if (wlc_p2p_sched_valid(s, slen) != BCME_OK) {
		WL_ERROR(("wl%d: parm length or value is wrong\n", wlc->pub->unit));
		return BCME_BADARG;
	}

	/* type to index */
	switch (s->type) {
	case WL_P2P_SCHED_TYPE_ABS:
		sched = WLC_P2P_NOA_ABS;
		break;
	case WL_P2P_SCHED_TYPE_REQ_ABS:
		sched = WLC_P2P_NOA_REQ_ABS;
		break;
	default:
		WL_ERROR(("wl%d: schedule type %u is not supported\n",
		          wlc->pub->unit, s->type));
		return BCME_BADARG;
	}

	cnt = (slen - WL_P2P_SCHED_FIXED_LEN) / sizeof(wl_p2p_sched_desc_t);

	/* sanity checks */
	switch (sched) {
	case WLC_P2P_NOA_ABS:
	case WLC_P2P_NOA_REQ_ABS:
		if (!P2P_GO(wlc, cfg)) {
			WL_ERROR(("wl%d: bsscfg %d is not a P2P GO\n",
			          wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
			return BCME_ERROR;
		}
		break;
	}

	/* GO only from now on */
	ASSERT(P2P_GO(wlc, cfg));

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

#ifdef WLMCHAN
	/* if multichannel and noa in use, disallow user noa */
	if (MCHAN_ENAB(wlc->pub) &&
	    (p2p->flags & WLC_P2P_INFO_MCHAN_NOA))
		return BCME_BUSY;
#endif

	/* set, update, or cancel the current schedule */
	/* HACK: convert wl_p2p_sched_t to wifi_p2p_noa_se_t so that we can share
	 * wlc_p2p_sched_noa().
	 */
	if (cnt > 0) {
		uint16 length;

		WL_P2P(("wl%d: %s: sched %u action %u option %u desc %u\n",
		        wlc->pub->unit, __FUNCTION__, sched, s->action, s->option, cnt));

		if ((se = MALLOCZ(wlc->osh, P2P_NOA_SE_FIXED_LEN +
		                 cnt * sizeof(wifi_p2p_noa_desc_t))) == NULL) {
			WL_ERROR((WLC_BSS_MALLOC_ERR, WLCWLUNIT(wlc), WLC_BSSCFG_IDX(cfg),
				__FUNCTION__,
				(int)(P2P_NOA_SE_FIXED_LEN + cnt * sizeof(wifi_p2p_noa_desc_t)),
				MALLOCED(wlc->osh)));
			return BCME_NOMEM;
		}
		se->eltId = P2P_SEID_ABSENCE;
		length = P2P_NOA_SE_FIXED_LEN - P2P_ATTR_HDR_LEN +
		        cnt * sizeof(wifi_p2p_noa_desc_t);
		htol16_ua_store(length, se->len);
		se->index = p2p->id;
		se->ops_ctw_parms = (p2p->ops << P2P_NOA_OPS_SHIFT) | p2p->ctw;
		slen = wlc_p2p_sched_wl2se(s->desc, se->desc, cnt);
		slen += P2P_NOA_SE_FIXED_LEN;
	}
	else if (s->action == WL_P2P_SCHED_ACTION_RESET) {
		WL_P2P(("wl%d: %s: reset sched %u at tick 0x%x\n",
		        wlc->pub->unit, __FUNCTION__, sched, WL_P2P_TS(wlc)));

		reset = TRUE;
		slen = 0;
	}
	else {
		/* no reason to come here! */
		err = BCME_BADARG;
	}
	if (err == BCME_OK)
		err = wlc_p2p_sched_noa(pm, p2p, sched, se, slen, TRUE, &update);
	if (cnt > 0)
		MFREE(wlc->osh, se, P2P_NOA_SE_FIXED_LEN + cnt * sizeof(wifi_p2p_noa_desc_t));
	if (err != BCME_OK) {
		WL_ERROR(("wl%d: %s: noa update failed\n", wlc->pub->unit, __FUNCTION__));
		return err;
	}
	if (reset)
		p2p->sched[sched].action = WL_P2P_SCHED_ACTION_NONE;
	else
		p2p->sched[sched].action = s->action;
	p2p->sched[sched].option = s->option;
	if (s->option == WL_P2P_SCHED_OPTION_BCNPCT) {
		p2p->sched[sched].start = (uint8)s->desc[0].start;
		p2p->sched[sched].dur = (uint8)s->desc[0].duration;
	}

	/* update schedule in SHM and NoA in beacons */
	if ((reset || update) &&
	    (p2p->flags & WLC_P2P_INFO_NET)) {
		if (sched == WLC_P2P_NOA_ABS) {
			p2p->flags &= ~WLC_P2P_INFO_DLY;
			p2p->flags &= ~WLC_P2P_INFO_STRT;
		}
		wlc_p2p_sched_norm(pm, p2p);
		if (!wlc_p2p_sched_upd(pm, p2p)) {
			return BCME_OK;
		}

		if (p2p->sched[0].cnt > 1) {
			wlc_p2p_noa_upd_notif(pm, cfg);
		}
		wlc_p2p_id_upd(pm, p2p);
		wlc_p2p_bcn_upd(pm, p2p);
		wlc_p2p_noa_upd(pm, p2p);
	}

	return BCME_OK;
}

static int
wlc_p2p_noa_get(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wl_p2p_sched_t *buf, int blen)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	wlc_p2p_sched_t *sched;
	int vlen, dlen;

	(void)wlc;

	ASSERT(cfg != NULL);

	/* sanity checks */
	if (!P2P_GO(wlc, cfg)) {
		WL_ERROR(("wl%d: bsscfg %d is not a P2P GO\n",
		          wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
		return BCME_NOTAP;
	}

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	if (!(p2p->flags & WLC_P2P_INFO_NET)) {
		WL_ERROR(("wl%d: bsscfg %d is not initialized\n",
		          wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
		return BCME_NOTUP;
	}

	sched = &p2p->sched[WLC_P2P_NOA_ABS];

	dlen = 0;
	vlen = sizeof(wl_p2p_sched_t);
	if (sched->desc != NULL &&
	    sched->idx < sched->cnt) {
		dlen = (sched->cnt - sched->idx) * sizeof(wl_p2p_sched_desc_t);
		vlen += dlen;
	}
	if (blen < vlen)
		return BCME_BUFTOOSHORT;

	bzero((uint8 *)buf, vlen);
	buf->type = WL_P2P_SCHED_TYPE_ABS;
	buf->option = sched->option;
	buf->action = sched->action;
	if (dlen > 0) {
		bcopy((uint8 *)&sched->desc[sched->idx], buf->desc, dlen);
		if (p2p->flags & WLC_P2P_INFO_STRT)
			buf->desc[0].start = p2p->start;
	}

	return BCME_OK;
}

bool
wlc_p2p_noa_valid(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;

	ASSERT(wlc != NULL);
	ASSERT(cfg != NULL);

	if (!BSS_P2P_ENAB(wlc, cfg))
		return FALSE;
	else {
		p2p = BSS_P2P_INFO(pm, cfg);
		ASSERT(p2p != NULL);
		return (p2p->flags & WLC_P2P_INFO_CUR) ? TRUE : FALSE;
	}
}

bool
wlc_p2p_ops_valid(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;

	ASSERT(wlc != NULL);
	ASSERT(cfg != NULL);

	if (!BSS_P2P_ENAB(wlc, cfg))
		return FALSE;
	else {
		p2p = BSS_P2P_INFO(pm, cfg);
		ASSERT(p2p != NULL);
		return ((p2p->flags & WLC_P2P_INFO_OPS) && p2p->ops) ? TRUE : FALSE;
	}
}


/** update NoA in beacons. */
static void
wlc_p2p_bcn_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	ASSERT(P2P_GO(wlc, cfg));

	WL_P2P(("wl%d.%d: update bcn template at tick 0x%x\n",
	        wlc->pub->unit, WLC_BSSCFG_IDX(cfg), WL_P2P_TS(wlc)));

	wlc_bss_update_beacon(wlc, cfg);
}

/** update NoA in SHM */
static void
_wlc_p2p_noa_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;
	wlc_bsscfg_t *cfg;
	uint32 ltsf_h, ltsf_l;
	uint32 rtsf_h, rtsf_l;
	bool dly = FALSE;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	wlc_mcnx_read_tsf64(mcnx, cfg, &rtsf_h, &rtsf_l);
	wlc_mcnx_r2l_tsf64(mcnx, cfg, rtsf_h, rtsf_l, &ltsf_h, &ltsf_l);

	if (p2p->flags & WLC_P2P_INFO_CUR) {
		/* delay NoA absence schedule if needed */
		dly = wlc_p2p_noa_dly(pm, p2p, ltsf_l);
		if (!(p2p->flags & WLC_P2P_INFO_DLY) && dly) {
			WL_P2P(("wl%d: delay absence schedule at tick 0x%x (local) "
			        "0x%x (remote)\n", wlc->pub->unit, ltsf_l, rtsf_l));
			p2p->flags |= WLC_P2P_INFO_DLY;
		}
		else if ((p2p->flags & WLC_P2P_INFO_DLY) && !dly) {
			WL_P2P(("wl%d: start absence schedule at tick 0x%x (local) "
			        "0x%x (remote)\n", wlc->pub->unit, ltsf_l, rtsf_l));
			p2p->flags &= ~WLC_P2P_INFO_DLY;
		}

		/* update/create NoA schedule */
		if (!dly) {
			wlc_mcnx_abs_t abs;

			ASSERT(wlc_p2p_noa_start_dist(pm, p2p, ltsf_l) <= P2P_NOA_MAX_NXT);

			/* absence start time in local TSF */
			abs.start = wlc_mcnx_r2l_tsf32(mcnx, cfg, p2p->cur.start);
			abs.duration = p2p->cur.duration;
			abs.interval = p2p->cur.interval;
			abs.count = p2p->cur.count;
			wlc_mcnx_abs_upd(mcnx, cfg, TRUE, &abs);

			/* save the schedule count and decrement it on every
			 * presence interrupt until it reaches zero and then
			 * reprogram the SHM
			 */
			p2p->count = (uint16)abs.count;

			return;
		}
	}

	wlc_mcnx_abs_upd(mcnx, cfg, FALSE, NULL);
#ifdef PROP_TXSTATUS
		if (PROP_TXSTATUS_ENAB(wlc->pub)) {
			wlc_wlfc_interface_state_update(wlc, cfg, WLFC_CTL_TYPE_INTERFACE_OPEN);
		}
#endif
}

/**
 * update SHM BSS schedule block
 * M_P2P_BSS_CTW/M_P2P_BSS_N_CTW_END/M_P2P_BSS_N_NOA/M_P2P_BSS_NOA_TD/M_P2P_BSS_NOA_CNT words.
 *
 * all time related fields in d11 SHM BSS block are in units of 32us
 */
static void
wlc_p2p_noa_upd(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;
	wlc_bsscfg_t *cfg;

	ASSERT(p2p != NULL);

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	/* enable BSS control block */
	if (p2p->flags & WLC_P2P_INFO_NET) {

		/* update CTWindow schedule */
		if ((p2p->flags & WLC_P2P_INFO_OPS) && p2p->ops)
			wlc_mcnx_ctw_upd(mcnx, cfg, TRUE, p2p->ctw << 10);
		else
			wlc_mcnx_ctw_upd(mcnx, cfg, FALSE, 0);

		/* update NoA schedule */
		_wlc_p2p_noa_upd(pm, p2p);
	}
}

/** update NoA in SHM BSS block */
static void
wlc_p2p_tsf_upd_cb(void *ctx, wlc_mcnx_tsf_upd_data_t *notif_data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	bss_p2p_info_t *p2p;
	wlc_bsscfg_t *cfg;

	ASSERT(notif_data != NULL);

	cfg = notif_data->cfg;
	ASSERT(cfg != NULL);

	p2p = BSS_P2P_INFO(pm, cfg);
	if (p2p == NULL) {
		return;
	}

	if (p2p->flags & WLC_P2P_INFO_NET) {
		wlc_p2p_sched_norm(pm, p2p);
		wlc_p2p_sched_upd(pm, p2p);
		wlc_p2p_noa_upd(pm, p2p);
	}
}

/** probe request/response */
void
wlc_p2p_sendprobe(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, void *p)
{
	wlc_info_t *wlc = pm->wlc;
	ratespec_t rate_override = 0;


	/* always use 6Mbps unless it is for P2P SCAN in discovery */
	if (wlc_p2p_no_cck_rates(pm, cfg))
		rate_override = WLC_RATE_6M;


	if (!wlc_queue_80211_frag(wlc, p, wlc->active_queue, NULL, cfg, FALSE, NULL,
		rate_override)) {
		WL_ERROR(("wl%d: %s: wlc_queue_80211_frag failed\n", wlc->pub->unit, __FUNCTION__));
		return;
	}
}

static void
wlc_p2p_send_prbresp(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	struct ether_addr *da, struct ether_addr *bssid, int band)
{
	wlc_info_t *wlc = pm->wlc;
#ifdef WLMCHAN
	wlc_p2p_data_t *pd = pm->p2p_data;
#endif /* WLMCHAN */

	void *p;
	uint8 *pbody;
	int len = ETHER_MAX_LEN;
#if defined(WLMSG_INFORM)
	char eabuf[ETHER_ADDR_STR_LEN];
#endif
	wlc_txq_info_t *qi;

	ASSERT(cfg != NULL);

	ASSERT(wlc->pub->up);

	/* build response and send */
	if ((p = wlc_frame_get_mgmt(wlc, FC_PROBE_RESP, da, &cfg->cur_etheraddr,
		bssid, len, &pbody)) == NULL) {
		WL_ERROR(("wl%d.%d: %s: wlc_frame_get_mgmt failed\n",
			wlc->pub->unit, WLC_BSSCFG_IDX(cfg), __FUNCTION__));
		return;
	}

	wlc_bcn_prb_body(wlc, FC_PROBE_RESP, cfg, pbody, &len, FALSE);
	PKTSETLEN(wlc->osh, p, len + DOT11_MGMT_HDR_LEN);

#ifdef WLMCHAN
	/* For a BSS in Disc state, its probe responses need to go immediately
	 * on the active queue!
	 */
	if (MCHAN_ENAB(wlc->pub) &&
	    cfg == pd->devcfg &&
	    pd->state == WL_P2P_DISC_ST_LISTEN) {
		qi = wlc->active_queue;
	}
	else
#endif
	qi = cfg->wlcif->qi;

	/* Set time of expiry over LONG_LISTEN_DWELL_TIME_THRESHOLD(ms) */
	wlc_lifetime_set(wlc, p, (LONG_LISTEN_DWELL_TIME_THRESHOLD*2)*1000);

	/* use proper bcmc_scb to pass the band check in wlc_prep_pdu() */
	WLPKTTAG(p)->flags |= WLF_PSDONTQ;
	if (!wlc_queue_80211_frag(wlc, p, qi, cfg->bcmc_scb[band], cfg,
		FALSE, NULL, LEGACY_RSPEC(WLC_RATE_6M))) {
		WL_ERROR(("wl%d.%d: %s: wlc_queue_80211_frag failed\n",
			wlc->pub->unit, WLC_BSSCFG_IDX(cfg), __FUNCTION__));
		return;
	}

	WL_INFORM(("wl%d.%d: sending prbresp to %s at tick 0x%x\n",
	           wlc->pub->unit, WLC_BSSCFG_IDX(cfg), bcm_ether_ntoa(da, eabuf),
	           WL_P2P_TS(wlc)));
}

#ifdef WLWFDS
static bool
wlc_p2p_wfds_advhash_match(uint8 *rx_hash_array, uint rx_hash_len, uint8 *adv_wfds_hash)
{
	uint8 *elt;
	uint totlen;

	elt = rx_hash_array;
	totlen = rx_hash_len;

	/* find match hash */
	while (totlen >= 6) {
		if (!memcmp(elt, adv_wfds_hash, P2P_WFDS_HASH_LEN)) {
			WL_NONE(("WFDS hash matched: rx=%02x%02x%02x%02x%02x%02x"
				" adv=%02x%02x%02x%02x%02x%02x\n",
				elt[0], elt[1], elt[2], elt[3], elt[4], elt[5],
				adv_wfds_hash[0], adv_wfds_hash[1], adv_wfds_hash[2],
				adv_wfds_hash[3], adv_wfds_hash[4], adv_wfds_hash[5]));
			return TRUE;
		}

		elt = elt + P2P_WFDS_HASH_LEN;
		totlen -= P2P_WFDS_HASH_LEN;
	}

	return FALSE;
}

static bool
wlc_p2p_wfds_add_send_hash(wlc_p2p_info_t *pm, uint32 advt_id, uint16 nw_cfg_method,
	uint8 name_len, uint8 *service_name, bool overwrite_head)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_p2p_adv_svc_info_attr	*list = NULL, *temp;
	wlc_info_t *wlc = pm->wlc;

	if (!pd->to_send_wfds_adv_list) {
		if ((pd->to_send_wfds_adv_list =
		     MALLOCZ(wlc->osh, sizeof(wlc_p2p_adv_svc_info_attr))) == NULL) {
			WL_ERROR((WLC_MALLOC_ERR, WLCWLUNIT(wlc), __FUNCTION__,
				(int)(sizeof(wlc_p2p_adv_svc_info_attr)), MALLOCED(wlc->osh)));
			return FALSE;
		}
		pd->to_send_wfds_adv_list->next = NULL;
	}

	if (overwrite_head) {
		list = pd->to_send_wfds_adv_list->next;
		while (list) { /** Free up the list as we reply only to gen_wfds_hash */
			temp = list->next;
			MFREE(wlc->osh, list, sizeof(wlc_p2p_adv_svc_info_attr));
			list = temp;
		}

		pd->to_send_wfds_adv_list->next  = NULL;
	} else { /** Add to top of the queue with fresh Node */
		if ((list = MALLOCZ(wlc->osh, sizeof(wlc_p2p_adv_svc_info_attr))) == NULL) {
			WL_ERROR((WLC_MALLOC_ERR, WLCWLUNIT(wlc), __FUNCTION__,
				(int)sizeof(wlc_p2p_adv_svc_info_attr), MALLOCED(wlc->osh)));
			return FALSE;
		}
		list->next = pd->to_send_wfds_adv_list;
		pd->to_send_wfds_adv_list = list;
	}

	/** @TODO: to skip re-write of same data again and again which is most common case */

	/** Fresh Data to be written at head */
	pd->to_send_wfds_adv_list->advt_id =  advt_id;
	pd->to_send_wfds_adv_list->nw_cfg_method = nw_cfg_method;
	pd->to_send_wfds_adv_list->name_len = name_len;
	memset(pd->to_send_wfds_adv_list->service_name, 0, MAX_WFDS_SVC_NAME_LEN);
	memcpy(pd->to_send_wfds_adv_list->service_name, service_name, name_len);

	return TRUE;

}
#endif /* WLWFDS */

static int
wlc_p2p_process_prbreq_ext(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	struct dot11_management_header *hdr, uint8 *body, int body_len,
	bool p2pie, wl_event_rx_frame_data_t *rxframe_data, bool send,
	int band)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_info_t *wlc = pm->wlc;
	uint8 *vndr_ies = NULL;
	int vndr_ies_len = 0;
	uint8 *req_wps;
	uint8 requested;
	uint8 notfound;
	uint8 *parse;
	int parse_len;
	uint8 *parse2;
	int parse2_len;
	wifi_p2p_devid_se_t *did = NULL;
	uint8 *did_combined = NULL;
	uint8 *di_combined = NULL;
	uint8 *gi_combined = NULL;
	wifi_p2p_di_fixed_t *di;
	int di_len;
	wifi_p2p_gi_fixed_t *gi;
	int gi_len;
	wifi_p2p_cid_fixed_t *cid;
	int cid_len;
#ifdef WLWFDS
	wifi_p2p_serv_hash_se_t *wfds_p2p_serv_hash = NULL;
	uint8 *wfds_hash = NULL;
	bool wfds_attr = FALSE;
	wifi_p2p_ie_t *ie;
#endif /* WLWFDS */

	(void)pd;
	ASSERT(cfg != NULL);

	if (!p2pie)
		goto resp;


	/* retrieve the P2P IEs if any */
	if ((vndr_ies_len = wlc_vndr_ie_getlen(wlc->vieli, cfg, VNDR_IE_PRBRSP_FLAG, NULL)) > 0) {
		if ((vndr_ies = MALLOCZ(wlc->osh, vndr_ies_len)) == NULL) {
			WL_ERROR((WLC_BSS_MALLOC_ERR, WLCWLUNIT(wlc), WLC_BSSCFG_IDX(cfg),
				__FUNCTION__, vndr_ies_len, MALLOCED(wlc->osh)));
			return BCME_NOMEM;
		}
		wlc_vndr_ie_write(wlc->vieli, cfg, vndr_ies, vndr_ies_len, VNDR_IE_PRBRSP_FLAG);
	}

#ifdef WLWFDS
	if (WFDS_ENAB(wlc->pub)) {

		/* Process the HASH attribute. Check if this is a P2P
		 * probe request if it includes the seek serv hash
		 * attributes
		 */
		parse = body;
		parse_len = body_len;
		while ((ie = bcm_find_p2pie(parse, parse_len)) != NULL) {
			int ilen = ie->len + TLV_HDR_LEN;

			parse_len -= (int)((uint8 *)ie - parse) + ilen;
			parse = (uint8 *)ie + ilen;

			/* GGC: Is this the correct call?  Is parse_len
			 * the correct value to pass here?
			 */
			wfds_p2p_serv_hash = (wifi_p2p_serv_hash_se_t *)
				wlc_p2p_find_se(wlc, ie,
				ilen, P2P_SEID_SERVICE_HASH, parse_len, &wfds_hash);

			if (wfds_p2p_serv_hash == NULL) {
				WL_NONE(("%s: no hash\n", __FUNCTION__));
				continue;
			} else {
				/* Expecting only one set of service hash attribute in
				 * the IE always
				 */
				WL_NONE(("%s: found hash %02x%02x%02x%02x%02x%02x\n",
					__FUNCTION__,
					wfds_p2p_serv_hash->hash[0], wfds_p2p_serv_hash->hash[1],
					wfds_p2p_serv_hash->hash[2], wfds_p2p_serv_hash->hash[3],
					wfds_p2p_serv_hash->hash[4], wfds_p2p_serv_hash->hash[5]));
				break;
			}
		}

		if (wfds_p2p_serv_hash && pd->reg_adv_svc_list) {
			uint i = 0, rx_hash_len;
			wl_p2p_wfds_hash_list_t	*hash_data = pd->reg_adv_svc_list;
			uint8	*rx_hash;
			bool	add_done;

			rx_hash = wfds_p2p_serv_hash->hash;
			rx_hash_len = ltoh16_ua(wfds_p2p_serv_hash->len);

			/** Length should be minimum or multiple of 6 */
			if (rx_hash_len % P2P_WFDS_HASH_LEN) {
				WL_NONE(("%s: bad hashlen %u\n", __FUNCTION__, rx_hash_len));
				goto hashfail;
			}

			/** Expect only one genric wfds hash */
			if (wlc_p2p_wfds_advhash_match(rx_hash, rx_hash_len,
				get_def_gen_wfds_hash())) {
				WL_NONE(("%s: generic hash match, len=%u\n", __FUNCTION__,
					ltoh16_ua(wfds_p2p_serv_hash->len)));

				/* Overwrite head for generic always */
				add_done = wlc_p2p_wfds_add_send_hash(pm, GEN_WFDS_ADVT_ID,
					GEN_WFDS_NW_CONFIG, P2P_GEN_WFDS_SVC_NAME_LEN,
					(uint8 *)P2P_GEN_WFDS_SVC_NAME, TRUE);
				if (add_done == FALSE) {
					WL_ERROR(("wl%d.%d: add_done failed for alloc\n",
						wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
					WL_NONE(("%s: add_done failed\n", __FUNCTION__));
					goto hashfail;
				}

				wfds_attr = TRUE;

			} else {
				do { /* search in the adv hash list */
					if (wlc_p2p_wfds_advhash_match(rx_hash, rx_hash_len,
						hash_data->adv_data.wfds_hash)) {

						/* Overwrite head for first time */
						add_done = wlc_p2p_wfds_add_send_hash(pm,
							hash_data->adv_data.advt_id,
							hash_data->adv_data.nw_cfg_method,
							hash_data->adv_data.name_len,
							hash_data->adv_data.service_name,
							(wfds_attr == TRUE) ? FALSE : TRUE);

						if (add_done == FALSE) {
							WL_ERROR(("wl%d.%d: add_done failed for"
								" alloc\n", wlc->pub->unit,
								WLC_BSSCFG_IDX(cfg)));
							goto hashfail;
						}

						wfds_attr = TRUE;

					}

					i++;
					hash_data = hash_data->next;
				} while ((i < pd->num_wfds_adv) && (hash_data));
			}

			wlc_free_p2p_attr_buf(wlc, &wfds_hash);
			WL_NONE(("%s: rxhash %02x%02x%02x%02x%02x%02x %s\n", __FUNCTION__,
				rx_hash[0], rx_hash[1], rx_hash[2], rx_hash[3], rx_hash[4],
				rx_hash[5], wfds_attr ? "matched" : "mismatch"));
		}

hashfail:

		/* P2P Device shouldn't reply to the P2P PRB REQ if the SERV_HASH doesn't
		 * match current advertised info
		 */
		if (wfds_p2p_serv_hash && (wfds_attr == FALSE)) {

			if (vndr_ies != NULL)
			   MFREE(wlc->osh, vndr_ies, vndr_ies_len);

			if (wfds_hash != NULL)
				wlc_free_p2p_attr_buf(wlc, &wfds_hash);

			WL_NONE(("%s: no SERV_HASH match, no prbrsp\n", __FUNCTION__));
			return BCME_OK;
		}
	}
#endif /* WLWFDS */

	/* check if this P2P probe request includes the Device ID attribute */
	requested = 0;
	notfound = 0;
	parse = body;
	parse_len = body_len;
	WL_NONE(("%s: Find SEID_DEV_ID in p=%p,l=%d\n",
		__FUNCTION__, OSL_OBFUSCATE_BUF(parse), parse_len));
	while ((did = (wifi_p2p_devid_se_t *)
		wlc_p2p_find_all_se_core(wlc, P2P_SEID_DEV_ID, &parse,
		&parse_len, &did_combined, NULL, NULL)) != NULL) {

		WL_NONE(("%s: Found P2P_SEID_DEV_ID at %p\n",
			__FUNCTION__, OSL_OBFUSCATE_BUF(did)));
		requested ++;

		/* no P2P IEs have been plumbed */
		if (vndr_ies == NULL || vndr_ies_len == 0) {
			WL_ERROR(("wl%d.%d: no P2P IEs plumbed\n",
			        wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
			wlc_free_p2p_attr_buf(wlc, &did_combined);
			goto edid;
		}

		/* does it match the device' Device Address? */
		parse2 = vndr_ies;
		parse2_len = vndr_ies_len;
		WL_NONE(("%s: Find SEID_DEV_INFO in p=%p,l=%d\n", __FUNCTION__,
			OSL_OBFUSCATE_BUF(parse2), parse2_len));
		di_combined = NULL;
		while ((di = (wifi_p2p_di_fixed_t *)
			wlc_p2p_find_all_se_core(wlc, P2P_SEID_DEV_INFO, &parse2,
			&parse2_len, &di_combined, &di_len, NULL)) != NULL) {

			WL_NONE(("%s: Found P2P_SEID_DEV_INFO at %p,%d\n",
				__FUNCTION__, OSL_OBFUSCATE_BUF(di), di_len));
			WL_NONE(("%s:   devaddr=%02x:%02x:%02x:%02x:%02x:%02x\n", __FUNCTION__,
				di->devaddr[0], di->devaddr[1], di->devaddr[2],
				di->devaddr[3], di->devaddr[4], di->devaddr[5]));
			WL_NONE(("%s: did addr=%02x:%02x:%02x:%02x:%02x:%02x\n", __FUNCTION__,
				did->addr.octet[0], did->addr.octet[1], did->addr.octet[2],
				did->addr.octet[3], did->addr.octet[4], did->addr.octet[5]));
			if (di_len < P2P_DI_FIXED_LEN) {
				wlc_free_p2p_attr_buf(wlc, &di_combined);
				continue;
			}

			if (bcmp(&did->addr, di->devaddr, ETHER_ADDR_LEN) == 0) {
				wlc_free_p2p_attr_buf(wlc, &di_combined);
				wlc_free_p2p_attr_buf(wlc, &did_combined);
				WL_NONE(("%s: did/di addr match\n", __FUNCTION__));
				goto rdt;
			}
			wlc_free_p2p_attr_buf(wlc, &di_combined);
			di_combined = NULL;
		}

		/* does it match one of the associated Client's Device Address? */
		if (P2P_GO(wlc, cfg)) {
			parse2 = vndr_ies;
			parse2_len = vndr_ies_len;
			WL_NONE(("%s: Find SEID_GROUP_INFO p=%p,l=%d\n",
				__FUNCTION__, OSL_OBFUSCATE_BUF(parse2), parse2_len));
			gi_combined = NULL;
			while ((gi = (wifi_p2p_gi_fixed_t *)
				wlc_p2p_find_all_se_core(wlc, P2P_SEID_GROUP_INFO,
				&parse2, &parse2_len, &gi_combined, NULL, NULL))
				!= NULL) {

				cid = (wifi_p2p_cid_fixed_t *)&gi[1];
				gi_len = ltoh16_ua(gi->len);
				WL_NONE(("%s: Found P2P_SEID_GROUP_INFO at %p,%d\n",
					__FUNCTION__, OSL_OBFUSCATE_BUF(gi), gi_len));
				while (gi_len >= (int)sizeof(wifi_p2p_cid_fixed_t)) {
					cid_len = cid->len + 1;

					if (bcmp(&did->addr, cid->devaddr, ETHER_ADDR_LEN) == 0) {
						wlc_free_p2p_attr_buf(wlc, &gi_combined);
						wlc_free_p2p_attr_buf(wlc, &did_combined);
						goto rdt;
					}

					cid = (wifi_p2p_cid_fixed_t *)((uint8 *)cid + cid_len);
					gi_len -= cid_len;
				}
				wlc_free_p2p_attr_buf(wlc, &gi_combined);
				gi_combined = NULL;
			}
		}

		WL_P2P_PRB(pm, ("wl%d: ignore prbreq from %s, Device ID %s not match\n",
		        wlc->pub->unit,
		        bcm_ether_ntoa(&hdr->sa, eabuf1), bcm_ether_ntoa(&did->addr, eabuf2)));

		wlc_free_p2p_attr_buf(wlc, &did_combined);

		notfound ++;
	}

edid:
	if (requested > 0 && notfound == requested) {
		WL_P2P_PRB(pm, ("wl%d: ignore prbreq, none of %d DeviceID attributes found\n",
		        wlc->pub->unit, requested));
		if (vndr_ies != NULL)
			MFREE(wlc->osh, vndr_ies, vndr_ies_len);
		return BCME_NOTFOUND;
	}

rdt:
	/* validate RequestedDeviceType attribute if present */
	requested = 0;
	notfound = 0;
	parse = body;
	parse_len = body_len;
	WL_NONE(("%s: Find wpsie\n", __FUNCTION__));
	while ((req_wps = (uint8 *)bcm_find_wpsie(parse, parse_len)) != NULL) {
		int req_wps_len = req_wps[TLV_LEN_OFF] + TLV_HDR_LEN;
		uint8 *req_ats = req_wps + WPS_IE_FIXED_LEN;
		int req_ats_len = req_wps_len - WPS_IE_FIXED_LEN;
		wps_at_fixed_t *req_at;

		WL_NONE(("%s: Found wpsie\n", __FUNCTION__));
		parse_len -= (int)(req_wps - parse) + req_wps_len;
		parse = req_wps + req_wps_len;

		while ((req_at =
		        bcm_wps_find_at((wps_at_fixed_t *)req_ats, req_ats_len,
		                        WPS_ATID_REQ_DEV_TYPE)) != NULL) {
			int req_at_len = ntoh16_ua(req_at->len) + WPS_AT_FIXED_LEN;
			uint8 *secdt;
			int secdts = 0;
			uint8 *wps_subcat;
			int wps_subcat_len;
			int i, cmp_len;

			WL_NONE(("%s: Found req_at\n", __FUNCTION__));
			req_ats_len -= (int)((uint8 *)req_at - req_ats) + req_at_len;
			req_ats = (uint8 *)req_at + req_at_len;

			requested ++;

			/* no P2P IEs have been plumbed */
			if (vndr_ies == NULL || vndr_ies_len == 0) {
				WL_ERROR(("wl%d.%d: no P2P IEs plumbed\n",
				        wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
				goto erdt;
			}

			/* check if there is vendor-specific sub-category inside
			 * WPS RequestedDeviceType attribute
			 */
			wps_subcat = (uint8*)req_at->data + 2;	/* skip category id */
			wps_subcat_len = P2P_DEV_TYPE_LEN - 2;
			for (i = 0; i < wps_subcat_len; i++) {
				if (wps_subcat[i] != 0)
					break;
			}
			/* compare vendor-specific sub-category only if it presents */
			cmp_len = (i == wps_subcat_len) ? 2 : P2P_DEV_TYPE_LEN;

			/* does it match the device's {Pri,Sec}DeviceType? */
			parse2 = vndr_ies;
			parse2_len = vndr_ies_len;
			WL_NONE(("%s: Find rdt DEVINFO p=%p,l=%d\n",
				__FUNCTION__, OSL_OBFUSCATE_BUF(parse2), parse2_len));
			di_combined = NULL;
			while ((di = (wifi_p2p_di_fixed_t *)
				wlc_p2p_find_all_se_core(wlc, P2P_SEID_DEV_INFO, &parse2,
				&parse2_len, &di_combined, &di_len, NULL)) != NULL) {

				WL_NONE(("%s: Found rdt P2P_SEID_DEV_INFO at %p\n",
					__FUNCTION__, OSL_OBFUSCATE_BUF(di)));
				if (di_len < P2P_DI_FIXED_LEN) {
					wlc_free_p2p_attr_buf(wlc, &di_combined);
					continue;
				}
				if (bcmp(req_at->data, di->pridt, cmp_len) == 0) {
					wlc_free_p2p_attr_buf(wlc, &di_combined);
					goto prep;
				}
				secdts = di->secdts;
				if (secdts == 0) {
					wlc_free_p2p_attr_buf(wlc, &di_combined);
					continue;
				}
				secdt = (uint8 *)&di[1];
				for (i = 0; i < secdts; i ++) {
					if (bcmp(req_at->data, secdt, cmp_len) == 0) {
						wlc_free_p2p_attr_buf(wlc, &di_combined);
						goto prep;
					}
					secdt += P2P_DEV_TYPE_LEN;
				}
				wlc_free_p2p_attr_buf(wlc, &di_combined);
				di_combined = NULL;
			}

			/* does it match one of the associated Client's {Pri,Sec}DeviceType? */
			if (P2P_GO(wlc, cfg)) {
				parse2 = vndr_ies;
				parse2_len = vndr_ies_len;
				WL_NONE(("%s: Find rdt SEID_GROUP_INFO at %p,%d\n",
					__FUNCTION__, OSL_OBFUSCATE_BUF(parse2), parse2_len));
				gi_combined = NULL;
				while ((gi = (wifi_p2p_gi_fixed_t *)
					wlc_p2p_find_all_se_core(wlc, P2P_SEID_GROUP_INFO,
					&parse2, &parse2_len, &gi_combined, NULL, NULL))
					!= NULL) {

					WL_NONE(("%s: Found rdt SEID_GROUP_INFO at %p\n",
						__FUNCTION__, OSL_OBFUSCATE_BUF(gi)));
					cid = (wifi_p2p_cid_fixed_t *)&gi[1];
					gi_len = ltoh16_ua(gi->len);
					while (gi_len >= (int)sizeof(wifi_p2p_cid_fixed_t)) {
						cid_len = cid->len + 1;

						if (bcmp(req_at->data, cid->pridt, cmp_len) == 0) {
							wlc_free_p2p_attr_buf(wlc, &gi_combined);
							goto prep;
						}
						secdts = cid->secdts;
						if (secdts == 0)
							break;
						secdt = (uint8 *)&cid[1];
						for (i = 0; i < secdts; i ++) {
							if (bcmp(req_at->data, secdt,
								cmp_len) == 0) {
								wlc_free_p2p_attr_buf(wlc,
									&gi_combined);
								goto prep;
							}
							secdt += P2P_DEV_TYPE_LEN;
						}

						cid = (wifi_p2p_cid_fixed_t *)
						        ((uint8 *)cid + cid_len);
						gi_len -= cid_len;
					}
					wlc_free_p2p_attr_buf(wlc, &gi_combined);
					gi_combined = NULL;
				}
			}

			WL_P2P_PRB(pm, ("wl%d: RequestedDeviceType "
			        "%02X%02X%02X%02X%02X%02X%02X%02X not found\n", wlc->pub->unit,
			        req_at->data[0], req_at->data[1], req_at->data[2], req_at->data[3],
			        req_at->data[4], req_at->data[5], req_at->data[6],
			        req_at->data[7]));

			notfound ++;
		}
	}

erdt:
	if (requested > 0 && notfound == requested) {
		WL_P2P_PRB(pm, ("wl%d: ignore prbreq, none of %d RequestedDeviceType attributes "
		        "match\n", wlc->pub->unit, requested));
		if (vndr_ies != NULL)
			MFREE(wlc->osh, vndr_ies, vndr_ies_len);
		return BCME_ERROR;
	}

prep:

	if (vndr_ies != NULL)
		MFREE(wlc->osh, vndr_ies, vndr_ies_len);

	/* Send this event to every virtual interface to benefit an application that may be
	 * interested only in particular virtual interface
	 */
	if (wlc_eventq_test_ind(wlc->eventq, WLC_E_P2P_PROBREQ_MSG))
		wlc_bss_mac_rxframe_event(wlc, cfg, WLC_E_P2P_PROBREQ_MSG, &hdr->sa, 0, 0, 0,
		                          (char *)hdr, body_len + DOT11_MGMT_HDR_LEN,
		                          rxframe_data);

resp:
	if (send) {
		bss_p2p_cmn_info_t *cmn = NULL;
		if (p2pie) {
			cmn = BSS_P2P_CMN_INFO(pm, cfg);
			ASSERT(cmn != NULL);
		}
		/* send Probe Response */
#ifdef WLWFDS
		if (WFDS_ENAB(wlc->pub) && wfds_attr) {
			/* Set the flag for adding the P2P IE Advertised Service Info
			 * attribute to the probe response
			 */
			pd->flags |= WLC_P2P_INFO_FLAG_WFDS_HASH;
			WL_NONE(("%s: set WFDS_HASH pd=%p flags=%x",
				__FUNCTION__, OSL_OBFUSCATE_BUF(pd), pd->flags));
			WL_TRACE(("WFDS hash found  Send Probe Response if false"
				" WLC_P2P_INFO_FLAG_WFDS_HASH set pm %p \n",
				OSL_OBFUSCATE_BUF(pm)));
		}
#endif /* WLWFDS */

		if (p2pie)
			cmn->flags |= BSS_P2P_CMN_INFO_P2P_IE;
		WL_NONE(("prbreqx: tx %sprbrsp to %02x:%02x:%02x:%02x:%02x:%02x\n",
			(pd->flags & WLC_P2P_INFO_FLAG_WFDS_HASH) ? "WFDS " : "",
			hdr->sa.octet[0], hdr->sa.octet[1], hdr->sa.octet[2],
			hdr->sa.octet[3], hdr->sa.octet[4], hdr->sa.octet[5]));
		wlc_p2p_send_prbresp(pm, cfg, &hdr->sa, &cfg->BSSID, band);
		if (p2pie)
			cmn->flags &= ~BSS_P2P_CMN_INFO_P2P_IE;
#ifdef WLWFDS
		/** Reset the Flag to make sure only for the PROBEREQ matching is sent with */
		if (WFDS_ENAB(wlc->pub) && wfds_attr) {
			pd->flags &= ~WLC_P2P_INFO_FLAG_WFDS_HASH;
			WL_NONE(("%s: clear WFDS_HASH pd=%p flags=%x",
				__FUNCTION__, OSL_OBFUSCATE_BUF(pd), pd->flags));
			WL_TRACE(("WFDS hash found  Send Probe Response if false"
				" WLC_P2P_INFO_FLAG_WFDS_HASH unset \n"));
		}
#endif /* WLWFDS */

	}
	DISCCNTINC(pm, prbresp);
	WL_NONE(("%s: exit\n", __FUNCTION__));
	return BCME_OK;
}

#if !DISABLE_UCODE_PRBRESP
/** check if ucode has responded to the probe request */
static bool
wlc_p2p_ucode_prbrsp(wlc_p2p_info_t *pm, wlc_d11rxhdr_t *wrxh)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	wlc_info_t *wlc = pm->wlc;
	uint len = FRAMELEN(wlc->pub->corerev, &wrxh->rxhdr) - DOT11_FCS_LEN;

	ASSERT(pd->prblen != (uint16)~0);


	return len <= pd->prblen;
}
#endif /* !DISABLE_UCODE_PRBRESP */

/**
 * ucode will not respond to any probe requests with WiFi P2P IE
 * (if the ucode can find any P2P IE in the first N bytes of the probe request frame).
 * return whether it is a bad probe request.
 */
bool
wlc_p2p_recv_process_prbreq(wlc_p2p_info_t *pm, struct dot11_management_header *hdr,
	uint8 *body, int body_len, wlc_d11rxhdr_t *wrxh, uint8 *plcp, bool sta_only)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_data_t *pd = pm->p2p_data;
#if defined(WLMSG_INFORM)
	char eabuf[ETHER_ADDR_STR_LEN];
#endif
	wifi_p2p_info_se_t *cap = NULL;
	uint8 *cap_combined = NULL;
	bcm_tlv_t *ssid, *rs;
	wlc_bsscfg_t *cfg = NULL;
	bool p2pie = FALSE;
#if defined(WLMSG_INFORM)
	char ssidbuf[SSID_FMT_BUF_LEN];
#endif
	wl_event_rx_frame_data_t rxframe_data;
	bool p2pbcssid = FALSE;
	bool bcssid = FALSE;
	wlc_bsscfg_t *bc;
	int idx, band;
	bool listen = FALSE;
#ifdef WLWFDS
	bool p2pwfds = FALSE;
	wifi_p2p_serv_hash_se_t *wfds_p2p_serv_hash = NULL;
	uint8 *wfds_hash = NULL;
#endif


	DISCCNTINC(pm, prbreq);

	/* find the SSID tag */
	if ((ssid = bcm_parse_tlvs(body, body_len, DOT11_MNG_SSID_ID)) == NULL) {
		WL_ERROR(("wl%d: ignore prbreq from %s no SSID IE\n",
		          wlc->pub->unit, bcm_ether_ntoa(&hdr->sa, eabuf)));
		return TRUE;
	}

#if defined(WLMSG_INFORM)
	wlc_format_ssid(ssidbuf, ssid->data, ssid->len);
#endif

	/* check wildcard SSID */
	if (ssid->len == 0)
		bcssid = TRUE;
	else if (ssid->len == pd->ssid.SSID_len &&
	         bcmp(ssid->data, pd->ssid.SSID, ssid->len) == 0)
		p2pbcssid = TRUE;

	/* listen state matters only if it is P2P wildcard SSID */
	if (p2pbcssid &&
	    wlc_p2p_disc_state(pm) &&
	    pd->state == WL_P2P_DISC_ST_LISTEN)
		listen = TRUE;

	/* check if this is a P2P probe request and if it includes the attributes */
	cap = (wifi_p2p_info_se_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_P2P_INFO,
		body, body_len, &cap_combined, NULL, &p2pie);

	wlc_free_p2p_attr_buf(wlc, &cap_combined);
#ifdef WLWFDS
	if (WFDS_ENAB(wlc->pub)) {
		wfds_p2p_serv_hash = (wifi_p2p_serv_hash_se_t *)
			wlc_p2p_find_all_se(wlc, P2P_SEID_SERVICE_HASH,
			body, body_len, &wfds_hash, NULL, &p2pie);

		wlc_free_p2p_attr_buf(wlc, &wfds_hash);

		if (wfds_p2p_serv_hash)
			p2pwfds = TRUE;
	}
#endif /* WLWFDS */


	if (!p2pie) {
#if !DISABLE_UCODE_PRBRESP
		/* nothing to do if ucode has responded to the probe request */
		if (wlc_p2p_ucode_prbrsp(pm, wrxh)) {
			WL_P2P_PRB(pm, ("wl%d: ignore prbreq '%s' from %s no P2P IE\n",
			        wlc->pub->unit, ssidbuf, bcm_ether_ntoa(&hdr->sa, eabuf)));
			return FALSE;
		}
#endif
	}
	else {
		DISCCNTINC(pm, p2pprbreq);

#if !defined(P2P_IE_OVRD)
		/* validate mandatory attribute */
		if (cap == NULL) {
			WL_P2P_PRB(pm, ("wl%d: ignore prbreq '%s' from %s cap 0x%p\n",
			        wlc->pub->unit, ssidbuf, bcm_ether_ntoa(&hdr->sa, eabuf),
				OSL_OBFUSCATE_BUF(cap)));
			return TRUE;
		}
#endif 


		/* validate ratesets */
		if (((rs = bcm_parse_tlvs(body, body_len, DOT11_MNG_RATES_ID)) == NULL ||
		     !wlc_rateset_isofdm(rs->len, rs->data)) &&
		    ((rs = bcm_parse_tlvs(body, body_len, DOT11_MNG_EXT_RATES_ID)) == NULL ||
		     !wlc_rateset_isofdm(rs->len, rs->data))) {
			WL_P2P_PRB(pm, ("wl%d: ignore prbreq '%s' from %s no OFDM rate\n",
			        wlc->pub->unit, ssidbuf, bcm_ether_ntoa(&hdr->sa, eabuf)));
			return TRUE;
		}
	}

	/* respond */
	WL_INFORM(("wl%d: recv %sprbreq '%s' from %s at tick 0x%x\n",
		wlc->pub->unit, p2pie ? "P2P " : "", ssidbuf, bcm_ether_ntoa(&hdr->sa, eabuf),
	        WL_P2P_TS(wlc)));

	/* This event requires more information about received frame. Prepare it once */
	if (wlc_eventq_test_ind(wlc->eventq, WLC_E_P2P_PROBREQ_MSG))
		wlc_recv_prep_event_rx_frame_data(wlc, wrxh, plcp, &rxframe_data);

	/* Send the notification to the host on base interface */
	if (p2pie &&
	    wlc_eventq_test_ind(wlc->eventq, WLC_E_P2P_PROBREQ_MSG))
		wlc_bss_mac_rxframe_event(wlc, NULL, WLC_E_P2P_PROBREQ_MSG, &hdr->sa, 0, 0, 0,
		                          (char *)hdr, body_len + DOT11_MGMT_HDR_LEN,
		                          &rxframe_data);

	band = CHSPEC_BANDUNIT(wrxh->rxhdr.lt80.RxChan);

	/* wildcard SSID */
	if (bcssid || p2pbcssid) {
		/* respond as Device */
		if (p2pbcssid && p2pie && listen) {
			/* in LISTEN state the DA must be bcast or Device Address and
			 * the BSSID must be wildcard
			 */
			cfg = pd->devcfg;
			ASSERT(cfg != NULL);
			if (ETHER_ISBCAST(&hdr->bssid) &&
			    (ETHER_ISBCAST(&hdr->da) ||
			     bcmp(&hdr->da, &cfg->cur_etheraddr, ETHER_ADDR_LEN) == 0) &&
				wf_chspec_ctlchan(cfg->current_bss->chanspec) ==
				wf_chspec_ctlchan(WLC_BAND_PI_RADIO_CHANSPEC) &&
				TRUE)
				wlc_p2p_process_prbreq_ext(pm, cfg, hdr, body, body_len, TRUE,
				                           &rxframe_data, TRUE, band);
		} else if ((wlc_p2p_disc_state(pm) ||
#ifdef WLWFDS  /* Ignore discover state if wfds attribute is present */
			(WFDS_ENAB(wlc->pub) && p2pwfds) ||
#endif
			FALSE) &&
			!P2P_RESTRICT_DEV_RESP(pm)) {
			/* respond as Device when associated with a WLAN and
			 * not a member of any P2P Group
			 */
			/* associated with a WLAN? */
			FOREACH_AS_STA(wlc, idx, bc) {
				if (!P2P_CLIENT(wlc, bc) && BSS_P2P_ENAB(wlc, bc))
					break;
			}
			if (bc == NULL)
				goto bcend;
			/* non-member of any P2P Group? */
			FOREACH_BSS(wlc, idx, bc) {
				if ((P2P_GO(wlc, bc) || P2P_CLIENT(wlc, bc)) &&
				    bc->associated)
					break;
			}
			if (bc != NULL)
				goto bcend;
			cfg = pd->devcfg;
			ASSERT(cfg != NULL);
			if (p2pie)
				wlc_p2p_process_prbreq_ext(pm, cfg, hdr, body, body_len, TRUE,
				                           &rxframe_data, TRUE, band);
		}
	bcend:
		if (!sta_only) {
			/* respond as GO */
			FOREACH_UP_AP(wlc, idx, bc) {
				if (!P2P_GO(wlc, bc))
					continue;
				if (bcssid ||
				    (p2pbcssid && p2pie &&
				     bcmp(bc->SSID, ssid->data, ssid->len) == 0)) {
					if (!p2pie && P2P_GO_NOLEGACY(pm)) {
						WL_INFORM(("wl%d: not responding to non-p2p"
						           " wildcard probe\n", wlc->pub->unit));
						continue;
					}
					wlc_p2p_process_prbreq_ext(pm, bc, hdr, body,
						body_len, p2pie, &rxframe_data, TRUE, band);
#if WFA_VER_1_02_TEST
					WL_NONE(("Test: call wlc_p2p_process_prbreq_ext as GO\n"));
					wlc_p2p_ver_1_02_attr_test(pm, bc);
#endif /* WFA_VER_1_02_TEST */
				}
			}
		}
		return FALSE;
	}
	/* GO SSID */
	else if ((!sta_only) &&
	            (cfg = wlc_bsscfg_find_by_ssid(wlc, ssid->data, ssid->len)) != NULL &&
	            P2P_GO(wlc, cfg)) {
		if (!p2pie && P2P_GO_NOLEGACY(pm)) {
			WL_INFORM(("wl%d: not responding to non-p2p wildcard probe\n",
			           wlc->pub->unit));
			return FALSE;
		}

		wlc_p2p_process_prbreq_ext(pm, cfg, hdr, body, body_len, p2pie,
			&rxframe_data, TRUE, band);
		return FALSE;
	}
	WL_INFORM(("wl%d: ignore prbreq '%s' from %s\n",
	        wlc->pub->unit, ssidbuf, bcm_ether_ntoa(&hdr->sa, eabuf)));
	return FALSE;
}

/** scan result filter */
int
wlc_p2p_recv_parse_bcn_prb(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, bool beacon,
	wlc_rateset_t *rs, uint8 *body, int body_len)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_data_t *pd = pm->p2p_data;
	wifi_p2p_info_se_t *cap = NULL;
	uint8 *cap_combined = NULL;
	bool p2pie = FALSE;

	BCM_REFERENCE(beacon);

	(void)wlc;

	ASSERT(cfg != NULL);

	/* For non-p2p bsscfg or assoc scan, there is no validation of P2P IE */
	if (!BSS_P2P_ENAB(wlc, cfg) ||
	    cfg->assoc->state != AS_IDLE)
		return BCME_OK;

	/* In the scan phase, there is no validation of P2P IE */
	if (BSS_P2P_DISC_ENAB(wlc, cfg) &&
	    pd->state == WL_P2P_DISC_ST_SCAN)
		return BCME_OK;

	/* check if this is a P2P probe request and if it includes the IE and attributes */
	cap = (wifi_p2p_info_se_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_P2P_INFO,
		body + DOT11_BCN_PRB_LEN, body_len - DOT11_BCN_PRB_LEN,
		&cap_combined, NULL, &p2pie);

	wlc_free_p2p_attr_buf(wlc, &cap_combined);

	if (!p2pie) {
		WL_P2P_PRB(pm, ("wl%d.%d: ignore %s no P2P IE\n",
		        wlc->pub->unit, WLC_BSSCFG_IDX(cfg), beacon ? "beacon" : "prbresp"));
		return BCME_ERROR;
	}

#if !defined(P2P_IE_OVRD)
	/* validate mandatory attributes */
	if (cap == NULL) {
		WL_P2P_PRB(pm, ("wl%d.%d: ignore %s no CAP attribute\n",
			wlc->pub->unit, WLC_BSSCFG_IDX(cfg), beacon ? "beacon" : "prbresp"));
		return BCME_ERROR;
	}
#endif 

	/* validate rates */
	if (!wlc_rateset_isofdm(rs->count, rs->rates)) {
		WL_P2P_PRB(pm, ("wl%d.%d: ignore %s no OFDM rates\n",
			wlc->pub->unit, WLC_BSSCFG_IDX(cfg), beacon ? "beacon" : "prbresp"));
		return BCME_ERROR;
	}

	return BCME_OK;
}

bool
wlc_p2p_ssid_match(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	uint8 *ref_SSID, uint ref_SSID_len, uint8 *SSID, uint SSID_len)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	ASSERT(cfg != NULL);

	/* If in the middle of p2p scan looking for "DIRECT-" as wildcard ssid,
	 * then match it
	 */
	if (ref_SSID_len == pd->ssid.SSID_len &&
	    bcmp(ref_SSID, pd->ssid.SSID, ref_SSID_len) == 0 &&
	    SSID_len >= ref_SSID_len &&
	    bcmp(SSID, ref_SSID, ref_SSID_len) == 0)
		return TRUE;

	return FALSE;
}

#if WFA_VER_1_02_TEST

/* #define WFA_VER_1_02_TEST_0 1 */
/* #define WFA_VER_1_02_TEST_1 1 */
/* #define WFA_VER_1_02_TEST_2 1 */
/* #define WFA_VER_1_02_TEST_3 1 */
/* #define WFA_VER_1_02_TEST_4 1 */
/* #define WFA_VER_1_02_TEST_5 1 */
/* #define WFA_VER_1_02_TEST_6 1 */
static int
wlc_p2p_ver_1_02_attr_test(wlc_p2p_info_t *p2p, wlc_bsscfg_t *bsscfg)
{
	wlc_info_t *wlc = p2p->wlc;
	uint8 *ie, *nextie;
	wifi_p2p_ie_t *p2p_ie;
	wifi_p2p_ie_t *p2p_ie_save;
	int p2p_ie_save_len;
	wifi_p2p_status_se_t *p2p_attr_status;
	wifi_p2p_noa_se_t *p2p_attr_noa;
	int se_len;
	bool foundp2p;
	int ie_len_remain, attr_body_len;
	uint8 data, *nextdata;
	int more_ies_len = 0;
	wifi_p2p_grpinfo_se_t *p2p_attr_grpinfo;
	wifi_p2p_devid_se_t *p2p_attr_devid;
	wifi_p2p_devinfo_se_t *p2p_attr_devinfo;
	uint8 *status_combined = NULL, *noa_combined = NULL, *grpinfo_combined = NULL;
	uint8 *ies;
	int ies_len;
	uint8 *p;
	struct dot11_management_header hdr;
	vndr_ie_buf_t *ie_buf;

	printf("wlc_p2p_ver_1_02_attr_test: bsscfg=%p\n", OSL_OBFUSCATE_BUF(bsscfg));
#define P2P_TEST_SPLIT_ATTR_SIZE 400

#ifdef WFA_VER_1_02_TEST_4
#undef P2P_TEST_ATTR_SIZE
#define P2P_TEST_ATTR_SIZE (520-27)
	printf("WFA_VER_1_02_TEST_4\n");
#endif

#ifdef WFA_VER_1_02_TEST_5
#undef P2P_TEST_ATTR_SIZE
#define P2P_TEST_ATTR_SIZE (520-27)
	printf("WFA_VER_1_02_TEST_5\n");
#endif

#ifdef WFA_VER_1_02_TEST_6
#undef P2P_TEST_ATTR_SIZE
#define P2P_TEST_ATTR_SIZE 247
	printf("WFA_VER_1_02_TEST_6\n");
#endif

	if ((ie =  MALLOCZ(wlc->osh, 800)) == NULL) {
		WL_ERROR((WLC_BSS_MALLOC_ERR, WLCWLUNIT(wlc), WLC_BSSCFG_IDX(bsscfg),
			__FUNCTION__, 800, MALLOCED(wlc->osh)));
		return BCME_NOMEM;
	}

	/*
	 * First p2p ie: 3 attributes:
	 * P2P_SEID_STATUS, P2P_SEID_GROUP_INFO, 1st half of P2P_SEID_ABSENCE
	 */
	nextie = ie;
	p2p_ie = (wifi_p2p_ie_t *)ie;
	p2p_ie->id = DOT11_MNG_VS_ID;
	bcopy(WFA_OUI, p2p_ie->OUI, WFA_OUI_LEN);
	p2p_ie->oui_type = WFA_OUI_TYPE_P2P;

	/* 1st p2p attribute: P2P_SEID_STATUS */
	p2p_attr_status = (wifi_p2p_status_se_t *)p2p_ie->subelts;
	p2p_attr_status->eltId = P2P_SEID_STATUS;
	htol16_ua_store(1, p2p_attr_status->len);
	p2p_attr_status->status = 1;
	p2p_ie->len = WFA_OUI_LEN + 1 + P2P_ATTR_HDR_LEN + 1;

	/* 2nd p2p attribute: P2P_SEID_GROUP_INFO */
	p2p_attr_grpinfo = (wifi_p2p_grpinfo_se_t *) (nextie + p2p_ie->len + 2);
	p2p_attr_grpinfo->eltId = P2P_SEID_GROUP_INFO;
	htol16_ua_store(2, p2p_attr_grpinfo->len);
	p2p_ie->len += P2P_ATTR_HDR_LEN + 2;
	nextdata = nextie + p2p_ie->len;
	*nextdata++ = 1;
	*nextdata++ = 2;

	/* 3rd p2p attribute: 1st half of P2P_SEID_ABSENCE split attribute */
	p2p_attr_noa = (wifi_p2p_noa_se_t *)(nextie + p2p_ie->len + 2);
	p2p_attr_noa->eltId = P2P_SEID_ABSENCE;
	p2p_attr_noa->index = 1;
	p2p_attr_noa->ops_ctw_parms = 2;
	ie_len_remain = 255 - (p2p_ie->len + P2P_NOA_SE_FIXED_LEN);
	attr_body_len = P2P_TEST_SPLIT_ATTR_SIZE;
	htol16_ua_store(2 + attr_body_len, p2p_attr_noa->len);
	p2p_ie->len += P2P_NOA_SE_FIXED_LEN + ie_len_remain;
	data = 0;
	nextdata = (uint8 *) &p2p_attr_noa->desc[0];
	while (ie_len_remain-- && attr_body_len > 0) {
		*nextdata++ = data++;
		attr_body_len--;
	}
	more_ies_len += p2p_ie->len + OFFSETOF(wifi_p2p_ie_t, len) + 1;
	printf("test: 1st p2p_ie=%p len=%d buflen=%u, attrlen=1,2,%d\n",
		OSL_OBFUSCATE_BUF(nextie), p2p_ie->len, more_ies_len, 2 + P2P_TEST_SPLIT_ATTR_SIZE);

	/*
	 * Second p2p ie: 2nd half of P2P_SEID_ABSENCE split attribute
	 */
	nextie = ie + p2p_ie->len + 2;
	p2p_ie = (wifi_p2p_ie_t *)nextie;
	p2p_ie->id = DOT11_MNG_VS_ID;
	bcopy(WFA_OUI, p2p_ie->OUI, WFA_OUI_LEN);
	p2p_ie->oui_type = WFA_OUI_TYPE_P2P;
	p2p_ie->len = WFA_OUI_LEN + 1 + attr_body_len;
	ie_len_remain = 251;
	nextdata = nextie + P2P_IE_FIXED_LEN;
	while (ie_len_remain-- && attr_body_len > 0) {
		*nextdata++ = data++;
		attr_body_len--;
	}
	more_ies_len += p2p_ie->len + OFFSETOF(wifi_p2p_ie_t, len) + 1;
	printf("test: 2nd p2p_ie=%p len=%d, buflen=%d, attrlen=%d\n",
		OSL_OBFUSCATE_BUF(nextie), p2p_ie->len, more_ies_len,
		p2p_ie->len - WFA_OUI_LEN - 1);

#ifdef WFA_VER_1_02_TEST_1
	/* test an incorrect OUI in the P2P IE hdr */
	p2p_ie->OUI[0] = 0;
	printf("WFA_VER_1_02_TEST_1\n");
#endif

	/*
	 * Third p2p ie: P2P_SEID_GROUP_INFO, P2P_SEID_DEV_ID, P2P_SEID_DEV_INFO
	 */
	nextie += p2p_ie->len + 2;
	p2p_ie = (wifi_p2p_ie_t *)nextie;
	p2p_ie->id = DOT11_MNG_VS_ID;
	bcopy(WFA_OUI, p2p_ie->OUI, WFA_OUI_LEN);
	p2p_ie->oui_type = WFA_OUI_TYPE_P2P;
	p2p_ie->len = WFA_OUI_LEN + 1;

	/* 1st p2p attribute: P2P_SEID_GROUP_INFO */
	p2p_attr_grpinfo = (wifi_p2p_grpinfo_se_t *) (nextie + p2p_ie->len + 2);
	p2p_attr_grpinfo->eltId = P2P_SEID_GROUP_INFO;
	htol16_ua_store(3, p2p_attr_grpinfo->len);
	p2p_ie->len += P2P_ATTR_HDR_LEN + 3;
	nextdata = nextie + p2p_ie->len;
	*nextdata++ = 0x0a;
	*nextdata++ = 0x0b;
	*nextdata++ = 0x0c;

	/* 2nd p2p attribute: P2P_SEID_DEV_ID */
	p2p_attr_devid = (wifi_p2p_devid_se_t *) (nextie + p2p_ie->len + 2);
	p2p_attr_devid->eltId = P2P_SEID_DEV_ID;
	htol16_ua_store(sizeof(wifi_p2p_devid_se_t) - P2P_ATTR_HDR_LEN,
		p2p_attr_devid->len);
	p2p_ie->len += sizeof(wifi_p2p_devid_se_t);
	p2p_attr_devid->addr.octet[0] = 0xd0;
	p2p_attr_devid->addr.octet[1] = 0xd1;
	p2p_attr_devid->addr.octet[2] = 0xd2;
	p2p_attr_devid->addr.octet[3] = 0xd3;
	p2p_attr_devid->addr.octet[4] = 0xd4;
	p2p_attr_devid->addr.octet[5] = 0xd5;

	/* 3rd p2p attribute: P2P_SEID_DEV_INFO */
	p2p_attr_devinfo = (wifi_p2p_devinfo_se_t *) (nextie + p2p_ie->len + 2);
	p2p_attr_devinfo->eltId = P2P_SEID_DEV_INFO;
	htol16_ua_store(sizeof(wifi_p2p_devinfo_se_t) - P2P_ATTR_HDR_LEN,
		p2p_attr_devinfo->len);
	p2p_ie->len += sizeof(wifi_p2p_devinfo_se_t);
	p2p_attr_devinfo->mac[0] = 0xd0;
	p2p_attr_devinfo->mac[1] = 0xd1;
	p2p_attr_devinfo->mac[2] = 0xd2;
	p2p_attr_devinfo->mac[3] = 0xd3;
	p2p_attr_devinfo->mac[4] = 0xd4;
	p2p_attr_devinfo->mac[5] = 0xd5;
	p2p_attr_devinfo->wps_cfg_meths = 0xab;
	memset(p2p_attr_devinfo->pri_devtype, 0xee, 8);

	more_ies_len += p2p_ie->len + OFFSETOF(wifi_p2p_ie_t, len) + 1;
	p2p_ie_save = p2p_ie;
	p2p_ie_save_len = more_ies_len;
	printf("test: 3rd p2p_ie=%p len=%d, buflen=%d devinfo=%p\n",
		OSL_OBFUSCATE_BUF(nextie), p2p_ie->len, more_ies_len,
		OSL_OBFUSCATE_BUF(p2p_attr_devinfo));
	p = (uint8*) p2p_ie;
	printf("    : p2pie: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
	p = (uint8*) p2p_attr_devid;
	printf("    : devid_se: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);
	p = (uint8*) p2p_attr_devinfo;
	printf("    : devinfo_se: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
		p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]);

#ifdef WFA_VER_1_02_TEST_0
	/* test moredata too short for 2nd P2P IE hdr */
	more_ies_len = P2P_IE_FIXED_LEN - 1;
	printf("WFA_VER_1_02_TEST_0\n");
#endif

#ifdef WFA_VER_1_02_TEST_2
	/* test moredata too short for 2nd P2P IE attr body */
	more_ies_len = P2P_IE_FIXED_LEN + 251 - 1;
	printf("WFA_VER_1_02_TEST_2\n");
#endif

#ifdef WFA_VER_1_02_TEST_3
	/* test moredata too short for continued attr data in 2nd P2P IE */
	more_ies_len -= (P2P_ATTR_HDR_LEN + 2);
	more_ies_len--;
	printf("WFA_VER_1_02_TEST_3\n");
#endif

#ifdef WFA_VER_1_02_TEST_4
	/* test moredata too short for next P2P IE */
	more_ies_len -= 9;
	printf("WFA_VER_1_02_TEST_4\n");
#endif

#ifdef WFA_VER_1_02_TEST_5
	p2p_ie->OUI[0] = 0;
	printf("WFA_VER_1_02_TEST_5\n");
#endif

	/*
	 * Test calling wlc_p2p_process_prbreq_ext with the test IEs
	 */
	printf("===Test wlc_p2p_process_prbreq_ext(%p, %d)\n", OSL_OBFUSCATE_BUF(ie), more_ies_len);

	/* Fill in wlc->cfg->vndr_ie_listp with a copy of the 3rd P2P IE */
	if ((ie_buf = MALLOCZ(wlc->osh, sizeof(ie_buf) + p2p_ie_save_len)) == NULL) {
		WL_ERROR((WLC_BSS_MALLOC_ERR, WLCWLUNIT(wlc), WLC_BSSCFG_IDX(bsscfg), __FUNCTION__,
			(int)(sizeof(ie_buf) + p2p_ie_save_len), MALLOCED(wlc->osh)));
		return BCME_NOMEM;
	}
	ie_buf->iecount = 1;
	ie_buf->vndr_ie_list[0].pktflag = VNDR_IE_PRBRSP_FLAG;
	ie_buf->vndr_ie_list[0].vndr_ie_data.id = p2p_ie_save->id;
	ie_buf->vndr_ie_list[0].vndr_ie_data.len = p2p_ie_save->len;
	bcopy(p2p_ie_save->OUI, ie_buf->vndr_ie_list[0].vndr_ie_data.oui, WFA_OUI_LEN);
	bcopy((uint8*)(&p2p_ie_save->oui_type), ie_buf->vndr_ie_list[0].vndr_ie_data.data,
		ie_buf->vndr_ie_list[0].vndr_ie_data.len);
	(void) wlc_vndr_ie_add(wlc->vieli, wlc->cfg,
	                       ie_buf, ie_buf->vndr_ie_list[0].vndr_ie_data.len);

	/* Call wlc_p2p_process_prbreq_ext with a matching test P2P IE */
	bzero(&hdr, sizeof(hdr));
	if (bsscfg == NULL)
		bsscfg = wlc->cfg;
	wlc_p2p_process_prbreq_ext(p2p, bsscfg, &hdr, ie, more_ies_len,
		TRUE, NULL, FALSE, 0);

	MFREE(wlc->osh, ie_buf, sizeof(ie_buf) + p2p_ie_save_len);
	printf("===End of wlc_p2p_process_prbreq_ext(%p, %d) test\n",
		OSL_OBFUSCATE_BUF(ie), more_ies_len);

	/*
	 * Test using wlc_p2p_find_se() to find some P2P IE attributes.
	 */
	printf("= Begin wlc_p2p_find_se() tests\n");

	printf("=Test wlc_p2p_find_se(P2P_SEID_STATUS, %d)\n", more_ies_len);
	p2p_attr_status = (wifi_p2p_status_se_t *)
		wlc_p2p_find_se(wlc, (wifi_p2p_ie_t *)ie, ie[TLV_LEN_OFF] + TLV_HDR_LEN,
			P2P_SEID_STATUS, more_ies_len, &status_combined);
	if (p2p_attr_status != NULL)
		printf("=Found P2P_SEID_STATUS: id=%d len=%d status=%d\n",
			p2p_attr_status->eltId, ltoh16_ua(p2p_attr_status->len),
			p2p_attr_status->status);

	printf("=Test wlc_p2p_find_se(P2P_SEID_ABSENCE, %d)\n", more_ies_len);
	p2p_attr_noa = (wifi_p2p_noa_se_t *)
	        wlc_p2p_find_se(wlc, (wifi_p2p_ie_t *)ie, ie[TLV_LEN_OFF] + TLV_HDR_LEN,
	                        P2P_SEID_ABSENCE, more_ies_len, &noa_combined);
	if (p2p_attr_noa != NULL) {
		attr_body_len = ltoh16_ua(p2p_attr_noa->len);
		printf("=Found P2P_SEID_ABSENCE: id=%d len=%d index=%d ctw=%d\n",
			p2p_attr_noa->eltId, attr_body_len,
			p2p_attr_noa->index, p2p_attr_noa->ops_ctw_parms);
		p = (uint8*) noa_combined;
		printf("=     %02x%02x%02x%02x %02x%02x%02x%02x...%02x%02x%02x%02x\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
			p[attr_body_len-4], p[attr_body_len-3],
			p[attr_body_len-2], p[attr_body_len-1]);
	}

	printf("=Test wlc_p2p_find_se(P2P_SEID_GROUP_INFO, %d)\n", more_ies_len);
	p2p_attr_grpinfo = (wifi_p2p_grpinfo_se_t *)
	        wlc_p2p_find_se(wlc, (wifi_p2p_ie_t *)ie, ie[TLV_LEN_OFF] + TLV_HDR_LEN,
	                        P2P_SEID_GROUP_INFO, more_ies_len, &grpinfo_combined);
	if (p2p_attr_grpinfo != NULL)
		printf("=Found P2P_SEID_GROUP_INFO: id=%d len=%d\n",
			p2p_attr_grpinfo->eltId, ltoh16_ua(p2p_attr_grpinfo->len));

	wlc_free_p2p_attr_buf(wlc, &status_combined);
	wlc_free_p2p_attr_buf(wlc, &noa_combined);
	wlc_free_p2p_attr_buf(wlc, &grpinfo_combined);
	printf("= End wlc_p2p_find_se() tests\n");

	/*
	 * Test wlc_p2p_find_all_se()
	 */
	printf("== Begin wlc_p2p_find_all_se() tests\n");
	ies = ie;
	ies_len = more_ies_len;

	printf("==Test wlc_p2p_find_all_se(P2P_SEID_STATUS, %p, %d)\n",
		OSL_OBFUSCATE_BUF(ies), ies_len);
	p2p_attr_status = (wifi_p2p_status_se_t *)
		wlc_p2p_find_all_se(wlc, P2P_SEID_STATUS, ies, ies_len,
			&status_combined, &se_len, &foundp2p);
	if (p2p_attr_status != NULL)
		printf("==Found status se: f=%d ID=%d len=%d, status=%d \n",
			foundp2p, p2p_attr_status->eltId, se_len, p2p_attr_status->status);

	printf("==Test wlc_p2p_find_all_se(P2P_SEID_ABSENCE, %p, %d)\n",
		OSL_OBFUSCATE_BUF(ies), ies_len);
	p2p_attr_noa = (wifi_p2p_noa_se_t *)
		wlc_p2p_find_all_se(wlc, P2P_SEID_ABSENCE, ies, ies_len,
			&noa_combined, &se_len, &foundp2p);
	if (p2p_attr_noa != NULL) {
		printf("==Found noa se: f=%d ID=%d len=%d, index=%d ctw=%d\n",
			foundp2p, p2p_attr_noa->eltId, se_len,
			p2p_attr_noa->index, p2p_attr_noa->ops_ctw_parms);
		p = (uint8*) noa_combined;
		printf("==   %02x%02x%02x%02x %02x%02x%02x%02x...%02x%02x%02x%02x\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
			p[se_len-4], p[se_len-3], p[se_len-2], p[se_len-1]);
	}

	printf("==Test wlc_p2p_find_all_se(P2P_SEID_GROUP_INFO, %p, %d)\n",
		OSL_OBFUSCATE_BUF(ies), ies_len);
	p2p_attr_grpinfo = (wifi_p2p_grpinfo_se_t *)
		wlc_p2p_find_all_se(wlc, P2P_SEID_GROUP_INFO, ies, ies_len,
			&grpinfo_combined, &se_len, &foundp2p);
	if (p2p_attr_grpinfo != NULL)
		printf("==Found grpinfo se: f=%d ID=%d len=%d\n",
			foundp2p, p2p_attr_grpinfo->eltId, se_len);

	wlc_free_p2p_attr_buf(wlc, &status_combined);
	wlc_free_p2p_attr_buf(wlc, &noa_combined);
	wlc_free_p2p_attr_buf(wlc, &grpinfo_combined);
	printf("== End wlc_p2p_find_all_se() tests=\n");

	/*
	 * Test repeated calls to wlc_p2p_find_all_se_core() to parse 3
	 * different attribute in all P2P IEs.
	 */
	printf("=== Begin wlc_p2p_find_all_se_core() tests\n");

	ies = ie;
	ies_len = more_ies_len;
	printf("===Test find_all_se_core(P2P_SEID_STATUS, %p, %d)\n",
		OSL_OBFUSCATE_BUF(ies), ies_len);
	while ((p2p_attr_status = (wifi_p2p_status_se_t *)
		wlc_p2p_find_all_se_core(wlc, P2P_SEID_STATUS, &ies, &ies_len,
		&status_combined, &se_len, &foundp2p)) != NULL) {
		printf("===Found status se: f=%d ID=%d len=%d, status=%d \n",
			foundp2p, p2p_attr_status->eltId, se_len, p2p_attr_status->status);
		wlc_free_p2p_attr_buf(wlc, &status_combined);
	}

	ies = ie;
	ies_len = more_ies_len;
	printf("===Test find_all_se_core(P2P_SEID_ABSENCE, %p, %d)\n",
		OSL_OBFUSCATE_BUF(ies), ies_len);
	while ((p2p_attr_noa = (wifi_p2p_noa_se_t *)
		wlc_p2p_find_all_se_core(wlc, P2P_SEID_ABSENCE, &ies, &ies_len,
		&noa_combined, &se_len, &foundp2p)) != NULL) {
		printf("===Found noa se: f=%d ID=%d len=%d, index=%d ctw=%d\n",
			foundp2p, p2p_attr_noa->eltId, se_len,
			p2p_attr_noa->index, p2p_attr_noa->ops_ctw_parms);
		p = (uint8*) noa_combined;
		printf("===   %02x%02x%02x%02x %02x%02x%02x%02x...%02x%02x%02x%02x\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
			p[se_len-4], p[se_len-3], p[se_len-2], p[se_len-1]);
		wlc_free_p2p_attr_buf(wlc, &noa_combined);
	}

	ies = ie;
	ies_len = more_ies_len;
	printf("===Test find_all_se_core(P2P_SEID_GROUP_INFO, %p, %d)\n",
		OSL_OBFUSCATE_BUF(ies), ies_len);
	while ((p2p_attr_grpinfo = (wifi_p2p_grpinfo_se_t *)
		wlc_p2p_find_all_se_core(wlc, P2P_SEID_GROUP_INFO, &ies, &ies_len,
		&grpinfo_combined, &se_len, &foundp2p)) != NULL) {
		printf("===Found grpinfo se: f=%d ID=%d len=%d\n",
			foundp2p, p2p_attr_grpinfo->eltId, se_len);
		wlc_free_p2p_attr_buf(wlc, &grpinfo_combined);
	}

	printf("=== End wlc_p2p_find_all_se_core() tests=\n");

	MFREE(wlc->osh, ie, 800);
	return BCME_OK;
}
#endif /* WFA_VER_1_02_TEST */

void
wlc_p2p_recv_process_prbresp(wlc_p2p_info_t *pm, uint8 *body, int body_len)
{
	BCM_REFERENCE(pm);
	BCM_REFERENCE(body);
	BCM_REFERENCE(body_len);
}

/** association */
int
wlc_p2p_process_assocreq(wlc_p2p_info_t *pm, struct scb *scb,
	uint8 *tlvs, int tlvs_len)
{
	wlc_info_t *wlc = pm->wlc;
	wifi_p2p_info_se_t *cap = NULL;
	wifi_p2p_di_fixed_t *di = NULL;
	uint8 *cap_combined = NULL;
	uint8 *di_combined = NULL;
	bool p2pie = FALSE;
	p2p_scb_cubby_t *cubby;

	(void)wlc;

	/* check if this is a P2P assoc request and if it includes the IE and attributes */
	cap = (wifi_p2p_info_se_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_P2P_INFO,
		tlvs, tlvs_len, &cap_combined, NULL, &p2pie);
	di = (wifi_p2p_di_fixed_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_DEV_INFO,
		tlvs, tlvs_len, &di_combined, NULL, &p2pie);

	wlc_free_p2p_attr_buf(wlc, &cap_combined);
	wlc_free_p2p_attr_buf(wlc, &di_combined);

	if (!p2pie) {
		WL_P2P_AS(pm, ("wl%d: assocreq from %s has no P2P IE\n",
		        wlc->pub->unit, bcm_ether_ntoa(&scb->ea, eabuf)));
		return BCME_OK;
	}

#if !defined(P2P_IE_OVRD)
	/* validate mandatory attributes */
	if (cap == NULL || di == NULL) {
		/* There are concurrent mode devices (P2P+STA) which sends the P2P IE
		 * along with STA assoc req. But these STA P2P IE won't have the device info
		 * Attribute (to differentiate from P2P GC). So allow the connection to
		 * proceed as a legacy STA.
		 */
		WL_P2P_AS(pm, ("wl%d: assocreq with P2PIE from %s w/o mandatory "
				"CAP attribute 0x%p or DeviceInfo attribute 0x%p. Proceeding"
				"as legacy STA\n",
				wlc->pub->unit, bcm_ether_ntoa(&scb->ea, eabuf),
				OSL_OBFUSCATE_BUF(cap), OSL_OBFUSCATE_BUF(di)));
		return BCME_OK;
	}
#endif 

	scb->flags2 |= SCB2_P2P;

	/* allocate private data storage */
	cubby = P2P_SCB_CUBBY(pm, scb);
	if (cubby == NULL) {
		if (_wlc_p2p_scb_init(pm, scb) != BCME_OK) {
			return BCME_NOMEM;
		}
	}

	cubby = P2P_SCB_CUBBY(pm, scb);
	ASSERT(cubby != NULL);

#if USE_DEF_P2P_IE
	/* save IEs in scb cubby */
	if (cubby->as_ies != NULL)
		MFREE(wlc->osh, cubby->as_ies, cubby->as_ies_len);
	cubby->as_ies_len = 0;
	if ((cubby->as_ies = MALLOCZ(wlc->osh, tlvs_len)) == NULL) {
		WL_ERROR((WLC_MALLOC_ERR, WLCWLUNIT(wlc), __FUNCTION__, tlvs_len,
			MALLOCED(wlc->osh)));
		return BCME_NOMEM;
	}
	bcopy(tlvs, cubby->as_ies, tlvs_len);
	cubby->as_ies_len = (uint16)tlvs_len;
#endif /* USE_DEF_P2P_IE */

	return BCME_OK;
}

int
wlc_p2p_process_assocresp(wlc_p2p_info_t *pm, struct scb *scb,
	uint8 *tlvs, int tlvs_len)
{
	wlc_info_t *wlc = pm->wlc;

	(void)wlc;

	if (bcm_find_p2pie(tlvs, tlvs_len) == NULL) {
		WL_P2P_AS(pm, ("wl%d: assocresp from %s has no P2P IE\n",
		        wlc->pub->unit, bcm_ether_ntoa(&scb->ea, eabuf)));
		return BCME_OK;
	}

	scb->flags2 |= SCB2_P2P;

	return BCME_OK;
}

/** update STA/Client specific stuff when a STA (re)associates to a BSS */
static void
wlc_p2p_assoc_upd_cb(void *ctx, wlc_mcnx_assoc_upd_data_t *notif_data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	wlc_bsscfg_t *cfg;
	bool assoc;

	(void)wlc;

	ASSERT(notif_data != NULL);

	cfg = notif_data->cfg;
	ASSERT(cfg != NULL);
	ASSERT(BSSCFG_STA(cfg));

	p2p = BSS_P2P_INFO(pm, cfg);
	if (p2p == NULL)
		return;

	assoc = notif_data->assoc;

	ASSERT(P2P_CLIENT(wlc, cfg));

	if (assoc) {
		/* clear all other flags */
		p2p->flags = WLC_P2P_INFO_NET;

		/* SHM will be updated when receiving the first beacon... */
	}
	else {
		p2p->flags = 0;
#ifdef WLMCHAN
		/* clean up mchan related abs/psc states */
		if (MCHAN_ENAB(wlc->pub)) {
			wlc_mchan_client_noa_clear(wlc->mchan, cfg);
		}
#endif /* WLMCHAN */
	}

	p2p->enable = assoc;

	return;
}

/** update AP/GO specific stuff when an BSS is brought up/down */
static void
wlc_p2p_bss_upd_cb(void *ctx, wlc_mcnx_bss_upd_data_t *notif_data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	wlc_bsscfg_t *cfg;
	bool is_up;

	ASSERT(notif_data != NULL);

	cfg = notif_data->cfg;
	ASSERT(cfg != NULL);

	p2p = BSS_P2P_INFO(pm, cfg);
	if (p2p == NULL)
		return;

	ASSERT(P2P_GO(wlc, cfg));

	is_up = notif_data->up;

	if (is_up) {
		uint16 flags;

		/* clear all other flags */
		flags = p2p->flags & WLC_P2P_INFO_OPS;
		p2p->flags = flags | WLC_P2P_INFO_NET;

		wlc_p2p_enab_upd(pm, cfg);

		wlc_p2p_sched_norm(pm, p2p);
		wlc_p2p_sched_upd(pm, p2p);

		wlc_p2p_id_upd(pm, p2p);
		wlc_p2p_bcn_upd(pm, p2p);
		wlc_p2p_noa_upd(pm, p2p);

#if !DISABLE_UCODE_PRBRESP
		if (P2P_GO_NOLEGACY(pm))
#endif
		/* disable ucode ProbeResp */
		wlc_disable_probe_resp(wlc, PROBE_RESP_P2P_MASK,
			PROBE_RESP_P2P_MASK);
	}
	else {
		/* enable ucode ProbeResp */
		/* When DISABLE_UCODE_PRBRESP is defined to 0, may still need to reenable
		 * because P2P_GO_NOLEGACY feature might have disabled ucode ProbeResp.
		 */
		wlc_disable_probe_resp(wlc, PROBE_RESP_P2P_MASK, 0);

		p2p->flags = 0;
	}
}

/**
 * enable/disable P2P when GO grants a STA permission to associate or GO
 * is notified of a STA disassociation. checking if all associated STAs are P2P Client.
 */
void
wlc_p2p_enab_upd(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	wlc_info_t *wlc = pm->wlc;
	bss_p2p_info_t *p2p;
	struct scb_iter scbiter;
	struct scb *scb;

	ASSERT(cfg != NULL);
	ASSERT(P2P_GO(wlc, cfg));

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	ASSERT(p2p->flags & WLC_P2P_INFO_NET);

	p2p->enable = TRUE;

	/* are all associated STAs P2P client? */
	FOREACH_BSS_SCB(wlc->scbstate, &scbiter, cfg, scb) {
		if (SCB_ASSOCIATED(scb) && !SCB_P2P(scb)) {
			WL_P2P(("wl%d: disable NoA for bsscfg %d, non P2P Client STA %s\n",
			        wlc->pub->unit, WLC_BSSCFG_IDX(cfg),
			        bcm_ether_ntoa(&scb->ea, eabuf)));
			p2p->enable = FALSE;
			break;
		}
	}

}

static void *
wlc_p2p_af_pktget(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, const struct ether_addr *da,
	uint8 type, uint8 dialog, int at_len, uint8 **at)
{
	wlc_info_t *wlc = pm->wlc;
	int len;
	void *p;
	uint8 *pbody;
	wifi_p2p_action_frame_t *af;
	wifi_p2p_ie_t *ie;

	len = P2P_AF_FIXED_LEN + P2P_IE_FIXED_LEN + at_len;

	if ((p = wlc_frame_get_action(wlc, da, &cfg->cur_etheraddr,
	                            &cfg->BSSID, len, &pbody, P2P_AF_CATEGORY)) == NULL) {
		WL_ERROR(("wl%d: %s: wlc_frame_get_mgmt failed\n",
		          wlc->pub->unit, __FUNCTION__));
		return NULL;
	}

	af = (wifi_p2p_action_frame_t *)pbody;
	af->category = P2P_AF_CATEGORY;
	bcopy(WFA_OUI, af->OUI, WFA_OUI_LEN);
	af->type = WFA_OUI_TYPE_P2P;
	af->subtype = type;
	af->dialog_token = dialog;
	ie = (wifi_p2p_ie_t *)af->elts;
	ie->id = DOT11_MNG_VS_ID;
	ie->len = P2P_IE_FIXED_LEN - TLV_HDR_LEN + at_len;
	bcopy(WFA_OUI, ie->OUI, WFA_OUI_LEN);
	ie->oui_type = WFA_OUI_TYPE_P2P;

	*at = ie->subelts;
	return p;
}

/** process NoA action frame */
static void
wlc_p2p_process_noa(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	struct dot11_management_header *hdr, wifi_p2p_action_frame_t *af, int len)
{
	wlc_info_t *wlc = pm->wlc;
	bool update = FALSE;
	bss_p2p_info_t *p2p;
	int err;
	wifi_p2p_noa_se_t *se = NULL;
	uint8 *se_combined = NULL;
	int se_len = 0;

	BCM_REFERENCE(hdr);

	(void)wlc;
	ASSERT(cfg != NULL);
	ASSERT(P2P_CLIENT(wlc, cfg));

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	se = (wifi_p2p_noa_se_t*) wlc_p2p_find_all_se(wlc, P2P_SEID_ABSENCE,
		af->elts, len - P2P_AF_FIXED_LEN, &se_combined, &se_len, NULL);

	if (se == NULL) {
		WL_P2P(("wl%d: %s: no NoA attribute in Absence Request from %s\n",
		          wlc->pub->unit, __FUNCTION__, bcm_ether_ntoa(&hdr->sa, eabuf)));
		return;
	}

	err = wlc_p2p_sched_noa(pm, p2p, WLC_P2P_NOA_REQ_ABS, se, se_len, TRUE, &update);

	wlc_free_p2p_attr_buf(wlc, &se_combined);

	if (err == BCME_OK && update &&
	    (p2p->flags & WLC_P2P_INFO_NET)) {
		wlc_p2p_sched_norm(pm, p2p);
		wlc_p2p_sched_upd(pm, p2p);
		wlc_p2p_noa_upd(pm, p2p);
	}
}

#define P2P_NOA_CNT		65535
/* process NoA action frame */
static uint32
wlc_p2p_process_presence_req(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	struct dot11_management_header *hdr, wifi_p2p_action_frame_t *af, int len)
{
	wlc_info_t		*wlc = pm->wlc;
	wifi_p2p_noa_se_t	*se = NULL;
	uint8			*se_combined = NULL;
	wl_p2p_sched_t		s;
	wlc_mcnx_info_t		*mcnx = wlc->mcnx;
	bss_p2p_info_t		*p2p;
	bool		is_go_rfa_avail = FALSE;

	(void)wlc;
	ASSERT(cfg != NULL);
	ASSERT(P2P_CLIENT(wlc, cfg));
	p2p = BSS_P2P_INFO(pm, cfg);
	if (p2p == NULL)
		return BCME_NOTUP;

	se = (wifi_p2p_noa_se_t *)
		    wlc_p2p_find_se(wlc, (wifi_p2p_ie_t *)af->elts, len - P2P_AF_FIXED_LEN,
		                    P2P_SEID_VNDR, 0, &se_combined);
	if (se == NULL) {
		WL_P2P(("wl%d: %s: no NoA attribute in Absence Request from %s\n",
		          wlc->pub->unit, __FUNCTION__, bcm_ether_ntoa(&hdr->sa, eabuf)));
		return BCME_NO_P2P_SE;
	}
	s.action = WL_P2P_SCHED_ACTION_NONE;
	s.type = WL_P2P_SCHED_TYPE_ABS;
	s.option = WL_P2P_SCHED_OPTION_NORMAL;

	s.desc[0].start = wlc_mcnx_r2l_tsf32(mcnx, cfg, ltoh32_ua((uint8 *)&se->desc[0].start));
	s.desc[0].duration = ltoh32_ua((uint8 *)&se->desc[0].duration);
	s.desc[0].interval = ltoh32_ua((uint8 *)&se->desc[0].interval);
	s.desc[0].count = P2P_NOA_CNT;

	is_go_rfa_avail = wlc_p2p_noa_desc_upd_notif(pm, cfg, s.desc);

	if (!is_go_rfa_avail) {
		/* GO's sched is not set yet, means GO's eSCO is off */
		wlc_p2p_noa_set(pm, cfg, &s, sizeof(s));
		WL_P2P(("Setting noa-sched from GC-PrsAF\n"));
	}

	return BCME_OK;
}

/* Presence Request is initiated by GC */
int
wlc_p2p_send_presence_req(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	wl_p2p_sched_desc_t *noa, const struct ether_addr *da,
	wlc_p2p_noa_cb_t fn, void *arg)
{
	wlc_info_t		*wlc = pm->wlc;
	void			*p = NULL;
	wifi_p2p_noa_se_t	*se;
	int			se_len;
	wlc_txq_info_t		*qi;
	bss_p2p_info_t		*p2p;
	int			err = BCME_OK;

	BCM_REFERENCE(arg);

	ASSERT(cfg != NULL);
	ASSERT(cfg->associated);
	ASSERT(!P2P_GO(wlc, cfg));
	ASSERT(da != NULL);
	ASSERT(fn != NULL);

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	se_len = P2P_NOA_SE_FIXED_LEN + sizeof(se->desc);

	if (p2p->send_noa_cb != NULL) {
		WL_ERROR(("wl%d: %s: NoA pending\n", wlc->pub->unit, __FUNCTION__));
		err = BCME_NOA_PND;
	} else if ((p = wlc_p2p_af_pktget(pm, cfg, da, P2P_AF_PRESENCE_REQ,
			pm->p2p_data->dialog, se_len, (uint8 **)(uintptr)&se)) == NULL) {
		WL_ERROR(("wl%d: %s: wlc_p2p_af_pktget failed\n",
		          wlc->pub->unit, __FUNCTION__));
		err = BCME_GET_AF_FAILED;
	} else {
		wlc_p2p_dialog_upd(pm);
		se->eltId = P2P_SEID_VNDR;
		htol16_ua_store(se_len - P2P_ATTR_HDR_LEN, se->len);
		se->index = 0;
		se->ops_ctw_parms = 0;
		wlc_p2p_sched_wl2se(noa, se->desc, 1);
		qi = cfg->wlcif->qi;
		if (!wlc_queue_80211_frag(wlc, p, qi, NULL, cfg, FALSE,
			NULL, WLC_RATE_6M)) {
			WL_ERROR(("wl%d: %s: wlc_queue_80211_frag failed\n",
					wlc->pub->unit, __FUNCTION__));
			err = BCME_FRAG_Q_FAILED;
		}
		PKTFREE(wlc->osh, p, TRUE);
	}
	return err;
}


/* process p2p action frame */
void
wlc_p2p_process_action(wlc_p2p_info_t *pm,
	struct dot11_management_header *hdr, uint8 *body, int body_len)
{
	wlc_info_t *wlc = pm->wlc;
	wifi_p2p_action_frame_t *af = (wifi_p2p_action_frame_t *)body;
	wlc_bsscfg_t *cfg;

	ASSERT(body_len >= P2P_AF_FIXED_LEN);
	ASSERT(af->category == P2P_AF_CATEGORY);
	ASSERT(bcmp(af->OUI, WFA_OUI, WFA_OUI_LEN) == 0);
	ASSERT(af->type == WFA_OUI_TYPE_P2P);

	if ((cfg = wlc_bsscfg_find_by_bssid(wlc, &hdr->bssid)) == NULL) {
		WL_ERROR(("wl%d: %s: failed to find bsscfg %s\n",
		          wlc->pub->unit, __FUNCTION__, bcm_ether_ntoa(&hdr->bssid, eabuf)));
		return;
	}

	switch (af->subtype) {
	case P2P_AF_NOTICE_OF_ABSENCE:
		if (!P2P_CLIENT(wlc, cfg)) {
			WL_P2P(("wl%d: bsscfg %d is not a P2P Client\n",
			        wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
			break;
		}
		wlc_p2p_process_noa(pm, cfg, hdr, af, body_len);
		break;
	case P2P_AF_PRESENCE_REQ: {
		uint32 err = 0;
		if (P2P_GO(wlc, cfg)) {
			err = wlc_p2p_process_presence_req(pm, cfg, hdr, af, body_len);
			if (err != BCME_OK) {
				WL_ERROR(("wl%d: %s: failed to process presence req\n",
					wlc->pub->unit, __FUNCTION__));
			}
		} else {
			WL_P2P(("wl%d: bsscfg %d is not a P2P Client\n",
				wlc->pub->unit, WLC_BSSCFG_IDX(cfg)));
		}
	}
		break;

	default:
		WL_P2P(("wl%d: %s: subtype %d is not valid\n",
		        wlc->pub->unit, __FUNCTION__, af->subtype));
		break;
	}
}

/** process p2p public action frame */
void
wlc_p2p_process_public_action(wlc_p2p_info_t *pm,
	struct dot11_management_header *hdr, uint8 *body, int body_len)
{
	BCM_REFERENCE(pm);
	BCM_REFERENCE(hdr);
	BCM_REFERENCE(body);
	BCM_REFERENCE(body_len);
}

/** check if device can enter PS mode (as GO(s)) */
static bool
wlc_p2p_ps_allowed(wlc_p2p_info_t *pm, bss_p2p_info_t *p2p)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg;
#ifdef AP
	struct scb_iter scbiter;
	struct scb *scb;
#endif

	ASSERT(p2p != NULL);

	if (!p2p->ops)
		return FALSE;

	cfg = p2p->bsscfg;
	ASSERT(cfg != NULL);

	if (!P2P_GO(wlc, cfg))
		return TRUE;
#ifdef AP
	FOREACH_BSS_SCB(wlc->scbstate, &scbiter, cfg, scb) {
		if (SCB_ASSOCIATED(scb) && !scb->PS)
			return FALSE;
	}
#endif /* AP */

	return TRUE;
}

/**
 * broadcast a NoA schedule with a single descriptor to all associated clients
 * only support one outstanding send request
 */
bool
wlc_p2p_send_noa(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg,
	wl_p2p_sched_desc_t *noa, const struct ether_addr *da,
	wlc_p2p_noa_cb_t fn, void *arg)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_data_t *pd = pm->p2p_data;
	void *p = NULL;
	wifi_p2p_noa_se_t *se;
	int se_len;
	wlc_txq_info_t *qi;
	bss_p2p_info_t *p2p;

	ASSERT(cfg != NULL);
	ASSERT(cfg->associated);
	ASSERT(P2P_GO(wlc, cfg));
	ASSERT(da != NULL);
	ASSERT(fn != NULL);

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	if (p2p->send_noa_cb != NULL) {
		WL_ERROR(("wl%d: %s: NoA pending\n", wlc->pub->unit, __FUNCTION__));
		goto err;
	}

	se_len = P2P_NOA_SE_FIXED_LEN + sizeof(wifi_p2p_noa_desc_t);

	if ((p = wlc_p2p_af_pktget(pm, cfg, da, P2P_AF_NOTICE_OF_ABSENCE, pd->dialog,
	                           se_len, (uint8 **)(uintptr)&se)) == NULL) {
		WL_ERROR(("wl%d: %s: wlc_p2p_af_pktget failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto err;
	}
	wlc_p2p_dialog_upd(pm);

	{
	se->eltId = P2P_SEID_ABSENCE;
	htol16_ua_store(se_len - P2P_ATTR_HDR_LEN, se->len);
	}
	se->index = 0;
	se->ops_ctw_parms = 0;
	wlc_p2p_sched_wl2se(noa, se->desc, 1);

	qi = cfg->wlcif->qi;
	if (!wlc_queue_80211_frag(wlc, p, qi, NULL, cfg, FALSE, NULL, LEGACY_RSPEC(WLC_RATE_6M))) {
		WL_ERROR(("wl%d: %s: wlc_queue_80211_frag failed\n",
		          wlc->pub->unit, __FUNCTION__));
		goto err;
	}

	p2p->send_noa_cb = NULL;
	p2p->send_noa_cb_arg = NULL;
	if (wlc_pcb_fn_register(wlc->pcb, wlc_p2p_send_noa_complete,
	                        (void *)(uintptr)cfg->ID, p) != 0) {
		WL_ERROR(("wl%d: %s: wlc_pcb_fn_register failed\n",
			wlc->pub->unit, __FUNCTION__));
		goto err;
	}
	p2p->send_noa_cb = fn;
	p2p->send_noa_cb_arg = arg;

	return TRUE;

err:
	if (p != NULL)
		PKTFREE(wlc->osh, p, TRUE);
	return FALSE;
}

/** build a NoA schedule with a single descriptor */
void
wlc_p2p_build_noa(wlc_p2p_info_t *pm, wl_p2p_sched_desc_t *noa,
	uint32 start, uint32 duration, uint32 interval, uint32 count)
{
	BCM_REFERENCE(pm);

	ASSERT(count > 0);
	ASSERT(duration > 0);
	ASSERT(interval > 0);
	ASSERT((count == 1 && duration == interval) || (count > 1 && interval > duration));

	noa->start = start;
	noa->interval = interval;
	noa->duration = duration;
	noa->count = count;
}

bool
wlc_p2p_active(wlc_p2p_info_t *pm)
{
	wlc_info_t *wlc;
	int idx;
	wlc_bsscfg_t *cfg;

	if (pm == NULL)
		return FALSE;

	wlc = pm->wlc;

	FOREACH_BSS(wlc, idx, cfg) {
		if (P2P_IF(wlc, cfg))
			return TRUE;
	}

	return FALSE;
}

static bool
wlc_p2p_other_active(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	wlc_info_t *wlc = pm->wlc;
	int idx;
	wlc_bsscfg_t *ocfg;

	FOREACH_BSS(wlc, idx, ocfg) {
		if (P2P_IF(wlc, ocfg) && ocfg != cfg)
			return TRUE;
	}

	return FALSE;
}

/**
 * adopt remote TSF/TBTT based on beacon/prbresp timestamp,
 * init BSS block with NoA and/or OPS if any to allow ucode to follow
 * the necessary chains of states in the transmit direction
 * prior to association.
 */
void
wlc_p2p_adopt_bss(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wlc_bss_info_t *bi)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;
	bss_p2p_info_t *p2p;
	uint32 bcn_l, bcn_h;
	uint32 tsf_l, tsf_h;
	bool update = FALSE;

	ASSERT(cfg != NULL);
	ASSERT(bi != NULL);

	WL_P2P(("wl%d.%d: %s: start adopting bss\n",
		wlc->pub->unit, WLC_BSSCFG_IDX(cfg), __FUNCTION__));

	/* Avoid crash in external driver */
	if (bi->bcn_prb == NULL)
		return;

	/* update the bsscfg BSSID now for use in init routines */
	cfg->BSSID = bi->BSSID;

	/* write the BSSID */
	wlc_set_bssid(cfg);

	/* handle NoA/OPS */
	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	/* clear all flags and mark that we are configured */
	p2p->flags = WLC_P2P_INFO_NET;

	/* grab NoA/Ops info from IEs in beacon or probe resp */
	wlc_p2p_bcn_prb_noa(pm, p2p, bi->bcn_prb, bi->bcn_prb_len, &update);

	/* force PS mode on in ucode so that the tx suppression can be enforced... */
	if ((p2p->flags & WLC_P2P_INFO_OPS) && p2p->ops) {
		WL_P2P(("wl%d.%d: %s: enable PS for CTWindow suppression\n",
		        wlc->pub->unit, WLC_BSSCFG_IDX(cfg), __FUNCTION__));
		wlc_mcnx_hps_force(mcnx, cfg);
	}

	wlc_mcnx_st_upd(mcnx, cfg, TRUE);

	/* extraplate remote TSF/TBTT from beacon or probe resp */
	wlc_p2p_tbtt_calc_bss(pm, bi, &tsf_h, &tsf_l, &bcn_h, &bcn_l);

	/* plumb remote tsf/tbtt into h/w */
	wlc_mcnx_tbtt_set(mcnx, cfg, tsf_h, tsf_l, bcn_h, bcn_l);
}

/** prepare for the association process. */
/* TODO: clear/reset stale BSS block */
void
wlc_p2p_prep_bss(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	wlc_info_t *wlc = pm->wlc;

	BCM_REFERENCE(cfg);

	/* The intention here is to enable:
	 * M_P2P_I_PRE_TBTT
	 * M_P2P_I_CTW_END
	 * M_P2P_I_ABS
	 * M_P2P_I_PRS
	 */
	wlc_bmac_enable_tbtt(wlc->hw, TBTT_P2P_MASK, TBTT_P2P_MASK);
}

/** reset p2p assoc states and s/w and h/w BSSID, done during association */
void
wlc_p2p_reset_bss(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_mcnx_info_t *mcnx = wlc->mcnx;

	ASSERT(cfg != NULL);

	wlc_clear_bssid(cfg);

	bzero(&cfg->BSSID, ETHER_ADDR_LEN);

	wlc_mcnx_assoc_upd(mcnx, cfg, FALSE);

	/* Disable tbtt, if this was the last P2P bsscfg */
	if (!wlc_p2p_other_active(pm, cfg))
		wlc_bmac_enable_tbtt(wlc->hw, TBTT_P2P_MASK, 0);
}

void
wlc_p2p_apsd_retrigger_upd(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, bool retrigger)
{
	bss_p2p_info_t *p2p;

	ASSERT(cfg != NULL);

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	if (!retrigger) {
		p2p->flags &= ~WLC_P2P_INFO_APSD_RETRIG;
		return;
	}

	p2p->flags |= WLC_P2P_INFO_APSD_RETRIG;
}

void
wlc_p2p_pspoll_resend_upd(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, bool resend)
{
	bss_p2p_info_t *p2p;

	ASSERT(cfg != NULL);

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	if (!resend) {
		p2p->flags &= ~WLC_P2P_INFO_PSPOLL_RESEND;
		return;
	}

	p2p->flags |= WLC_P2P_INFO_PSPOLL_RESEND;
}

static bool
wlc_p2p_vndr_ie_filter(void *arg, const vndr_ie_t *ie)
{
	uint8 *parse;
	uint parse_len;
	BCM_REFERENCE(arg);

	ASSERT(ie != NULL);

	if (ie->id != DOT11_MNG_VS_ID)
		return FALSE;
/* unfortunately no way of disposing of const cast  - the
 * function is a callback so must have particular signature
 * and making bcm_is_p2p_ie causes cascade bringing need for
 * const cast in other places, on the other hand
 * no problem here with discarding const attribute from ie
 * as all side effects are just moving local pointer *parse
 */
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wcast-qual\"")
#endif
	parse = (uint8 *)ie;
	parse_len = TLV_HDR_LEN + ie->len;

	return bcm_is_p2p_ie((uint8 *)ie, &parse, &parse_len);
#if defined(STRICT_GCC_WARNINGS) && defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == \
	4 && __GNUC_MINOR__ >= 6))
_Pragma("GCC diagnostic pop")
#endif
}

static int
wlc_p2p_vndr_ie_getlen(wlc_info_t *wlc, wlc_bsscfg_t *cfg,
	uint16 ft, uint32 pktflag)
{
	if (ft == FC_PROBE_RESP) {
		wlc_p2p_info_t *pm;
		bss_p2p_cmn_info_t *cmn;

		ASSERT(cfg != NULL);

		pm = wlc->p2p;

		cmn = BSS_P2P_CMN_INFO(pm, cfg);
		ASSERT(cmn != NULL);

		if (!(cmn->flags & BSS_P2P_CMN_INFO_P2P_IE))
			return 0;
	}

	return wlc_vndr_ie_getlen_ext(wlc->vieli, cfg, wlc_p2p_vndr_ie_filter, pktflag, NULL);
}

static bool
wlc_p2p_vndr_ie_write_filter(void *arg, uint type, const vndr_ie_t *ie)
{
	BCM_REFERENCE(type);
	return wlc_p2p_vndr_ie_filter(arg, ie);
}

static uint8 *
wlc_p2p_vndr_ie_write(wlc_info_t *wlc, wlc_bsscfg_t *cfg,
	uint type, uint8 *cp, int buflen, uint32 pktflag)
{
	if (type == FC_PROBE_RESP) {
		wlc_p2p_info_t *pm;
		bss_p2p_cmn_info_t *cmn;

		ASSERT(cfg != NULL);

		pm = wlc->p2p;

		cmn = BSS_P2P_CMN_INFO(pm, cfg);
		ASSERT(cmn != NULL);

		if (!(cmn->flags & BSS_P2P_CMN_INFO_P2P_IE))
			return FALSE;
	}

	return wlc_vndr_ie_write_ext(wlc->vieli, cfg, wlc_p2p_vndr_ie_write_filter, type,
	                             cp, buflen, pktflag);
}


#ifdef WLMCHAN
int
wlc_p2p_mchan_noa_set(wlc_p2p_info_t *pm, wlc_bsscfg_t *cfg, wl_p2p_sched_t *s, int slen)
{
	bss_p2p_info_t *p2p;
	int status;

	ASSERT(cfg != NULL);

	p2p = BSS_P2P_INFO(pm, cfg);
	ASSERT(p2p != NULL);

	if (s->action == WL_P2P_SCHED_ACTION_RESET)
		p2p->flags &= ~WLC_P2P_INFO_MCHAN_NOA;
	status = wlc_p2p_noa_set(pm, cfg, s, slen);
	if (s->action != WL_P2P_SCHED_ACTION_RESET &&
	    status == BCME_OK)
		p2p->flags |= WLC_P2P_INFO_MCHAN_NOA;

	return status;
}
#endif /* WLMCHAN */

bool
wlc_p2p_go_scb_timeout(wlc_p2p_info_t *pm)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_data_t *pd = pm->p2p_data;

	if (pd->scb_timeout && (((wlc->pub->now - wlc->pub->pending_now)
		 % pd->scb_timeout) == 0)) {
		return TRUE;
	}
	return FALSE;
}

void
wlc_p2p_go_scb_timeout_set(wlc_p2p_info_t *pm, uint timeout)
{
	pm->p2p_data->scb_timeout = timeout;
}

#ifdef WLPROBRESP_SW
static bool
wlc_p2p_recv_process_prbreq_ap(void *handle, wlc_bsscfg_t *cfg,
	wlc_d11rxhdr_t *wrxh, uint8 *plcp, struct dot11_management_header *hdr,
	uint8 *body, int body_len, bool *psendProbeResp)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)handle;
	wlc_p2p_data_t *pd;
	wlc_info_t *wlc;
	wl_event_rx_frame_data_t rxframe_data;
	bcm_tlv_t *ssid;
	bool p2pie;
	bool p2pbcssid = FALSE;
	int band;

	wlc = pm->wlc;
	ssid = bcm_parse_tlvs(body, body_len, DOT11_MNG_SSID_ID);
	if (ssid == NULL)
		return FALSE;
	pd = pm->p2p_data;
	ASSERT(pd != NULL); /* ASSERT or return? */
	if ((ssid->len != 0) && (ssid->len == pd->ssid.SSID_len) &&
	    (bcmp(ssid->data, pd->ssid.SSID, ssid->len) == 0))
		p2pbcssid = TRUE;

	/* check if this is a P2P probe request and if it includes the attributes */
	p2pie = ((bcm_find_p2pie(body, body_len)) != NULL);

	if (P2P_GO(wlc, cfg)) {
		/* respond as GO, wildcard SSID or GO SSID */

		if (!p2pbcssid || p2pie) {
			if (!p2pie && P2P_GO_NOLEGACY(pm)) {
				WL_INFORM(("wl%d: not responding to non-p2p wildcard probe\n",
				           wlc->pub->unit));
				return FALSE;
			}

			/* This event requires more information about received frame. */
			if (wlc_eventq_test_ind(wlc->eventq, WLC_E_P2P_PROBREQ_MSG))
				wlc_recv_prep_event_rx_frame_data(wlc, wrxh, plcp,
					&rxframe_data);

			band = CHSPEC_BANDUNIT(wrxh->rxhdr.lt80.RxChan);

			wlc_p2p_process_prbreq_ext(pm, cfg, hdr, body, body_len, p2pie,
				&rxframe_data, *psendProbeResp, band);
		}
		/* if p2pbcssid && !p2pie, discard it */
		return FALSE;
	}

	return *psendProbeResp;
}
#endif /* WLPROBRESP_SW */

/** P2P IEs in bcn/prbrsp */
static uint
wlc_p2p_bcn_calc_ie_len(void *ctx, wlc_iem_calc_data_t *data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg = data->cfg;

	if (BSS_P2P_ENAB(wlc, cfg)) {
		uint16 type = data->ft;
		uint32 flag = wlc_ft2vieflag(type);

		return (uint)wlc_p2p_vndr_ie_getlen(wlc, cfg, type, flag) +
		        (uint)wlc_p2p_write_ie_len(pm, cfg, type) +
		        (uint)wlc_p2p_write_ie_extra_len(pm, cfg, type);
	}

	return 0;
}

static int
wlc_p2p_bcn_write_ie(void *ctx, wlc_iem_build_data_t *data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg = data->cfg;

	if (BSS_P2P_ENAB(wlc, cfg)) {
		uint16 type = data->ft;
		uint32 flag = wlc_ft2vieflag(type);
		uint8 *buf = data->buf;
		uint buf_len = data->buf_len;

		buf = wlc_p2p_vndr_ie_write(wlc, cfg, type, buf, buf_len, flag);
		buf += (uint)wlc_p2p_write_ie(pm, cfg, type, buf);
		wlc_p2p_write_ie_extra(pm, cfg, type, buf);
	}

	return BCME_OK;
}

/** P2P IEs in assocresp/reassocresp */
static uint
wlc_p2p_as_calc_ie_len(void *ctx, wlc_iem_calc_data_t *data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	uint16 type = data->ft;

	if (type == FC_ASSOC_RESP || type == FC_REASSOC_RESP) {
		wlc_iem_ft_cbparm_t *cbparm = data->cbparm->ft;
		struct scb *scb = cbparm->assocresp.scb;

		if (SCB_P2P(scb)) {
			uint status = cbparm->assocresp.status;
			uint32 flag = wlc_ft2vieflag(type);

			return (uint)wlc_p2p_vndr_ie_getlen(wlc, data->cfg, type, flag) +
			        (uint)wlc_p2p_write_ie_assoc_len(pm, scb, (uint16)status);
		}
	}

	return 0;
}

static int
wlc_p2p_as_write_ie(void *ctx, wlc_iem_build_data_t *data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	uint16 type = data->ft;

	if (type == FC_ASSOC_RESP || type == FC_REASSOC_RESP) {
		wlc_iem_ft_cbparm_t *cbparm = data->cbparm->ft;
		struct scb *scb = cbparm->assocresp.scb;

		if (SCB_P2P(scb)) {
			uint status = cbparm->assocresp.status;
			uint32 flag = wlc_ft2vieflag(type);
			uint8 *buf = data->buf;
			uint buf_len = data->buf_len;

			buf = wlc_p2p_vndr_ie_write(wlc, data->cfg, type, buf, buf_len, flag);
			wlc_p2p_write_ie_assoc(pm, scb, (uint16)status, buf);
		}
	}

	return BCME_OK;
}

/** P2P IEs in all other frame types */
static uint
wlc_p2p_other_calc_ie_len(void *ctx, wlc_iem_calc_data_t *data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg = data->cfg;

	if (BSS_P2P_ENAB(wlc, cfg)) {
		uint16 type = data->ft;
		uint32 flag;

		if (type == FC_AUTH) {
			wlc_iem_ft_cbparm_t *ftcbparm;
			ASSERT(data->cbparm != NULL);
			ftcbparm = data->cbparm->ft;
			ASSERT(ftcbparm != NULL);
			flag = wlc_auth2vieflag(ftcbparm->auth.seq);
		}
		else
			flag = wlc_ft2vieflag(type);

		return (uint)wlc_p2p_vndr_ie_getlen(wlc, cfg, type, flag);
	}

	return 0;
}

static int
wlc_p2p_other_write_ie(void *ctx, wlc_iem_build_data_t *data)
{
	wlc_p2p_info_t *pm = (wlc_p2p_info_t *)ctx;
	wlc_info_t *wlc = pm->wlc;
	wlc_bsscfg_t *cfg = data->cfg;

	if (BSS_P2P_ENAB(wlc, cfg)) {
		uint16 type = data->ft;
		uint32 flag;

		if (type == FC_AUTH) {
			wlc_iem_ft_cbparm_t *ftcbparm;
			ASSERT(data->cbparm != NULL);
			ftcbparm = data->cbparm->ft;
			ASSERT(ftcbparm != NULL);
			flag = wlc_auth2vieflag(ftcbparm->auth.seq);
		}
		else
			flag = wlc_ft2vieflag(type);

		(void) wlc_p2p_vndr_ie_write(wlc, cfg, type, data->buf, data->buf_len, flag);
	}
	return BCME_OK;
}

#ifdef WLWFDS
static void
wlc_p2p_wfds_free(wlc_p2p_info_t *pm)
{
	wlc_info_t *wlc = pm->wlc;
	wlc_p2p_data_t *pd = pm->p2p_data;

	while (pd->to_send_wfds_adv_list) {
		wlc_p2p_adv_svc_info_attr *temp
			= (wlc_p2p_adv_svc_info_attr *)pd->to_send_wfds_adv_list->next;
		MFREE(wlc->osh, pd->to_send_wfds_adv_list, sizeof(wlc_p2p_adv_svc_info_attr));
		pd->to_send_wfds_adv_list = temp;
	}
	while  (pd->reg_adv_svc_list) {
		wl_p2p_wfds_hash_list_t *temp
			= (wl_p2p_wfds_hash_list_t *)pd->reg_adv_svc_list->next;
		MFREE(wlc->osh, pd->reg_adv_svc_list, sizeof(wl_p2p_wfds_hash_list_t));
		pd->reg_adv_svc_list = temp;
	}
}

static int
wlc_p2p_add_wfds_hash(wlc_p2p_info_t *pm, wl_p2p_wfds_hash_t *hash)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	int err = BCME_ERROR;
	wl_p2p_wfds_hash_list_t *list, **tail = NULL;
	bool found = FALSE;

	WL_TRACE(("wl%d: wlc_p2p_add_wfds_hash(hash=%x %x %x %x %x %x, "
		  "name=%c%c%c%c%c%c, len=%d, nw config method=%d)\n",
		  pm->wlc->pub->unit,  hash->wfds_hash[0], hash->wfds_hash[1],
		  hash->wfds_hash[2], hash->wfds_hash[3], hash->wfds_hash[4],
		  hash->wfds_hash[5], hash->service_name[0], hash->service_name[1],
		  hash->service_name[2], hash->service_name[3], hash->service_name[4],
		  hash->service_name[5], hash->name_len, hash->nw_cfg_method));

	WL_TRACE(("wl%d: current number of adv items are %d", pm->wlc->pub->unit,
	          pd->num_wfds_adv));

	WL_NONE(("%s: name_len=%u\n", __FUNCTION__, hash->name_len));
	if (hash->name_len > MAX_WFDS_SVC_NAME_LEN) {
		WL_ERROR(("wl%d: Name len is %d should be less than %d",
		          pm->wlc->pub->unit, hash->name_len, MAX_WFDS_SVC_NAME_LEN));
		err = BCME_BADARG;
		goto exit;
	}

	/* check for duplicate before add */
	if (pd->num_wfds_adv) {
		tail = &(pd->reg_adv_svc_list);

		while (*tail) {
			if (!bcmp((*tail)->adv_data.wfds_hash, hash->wfds_hash,
				P2P_WFDS_HASH_LEN)) {
				/* Entry exists skip new add */
				found = TRUE;
				err = BCME_OK;
				break;
			}
			tail = (wl_p2p_wfds_hash_list_t **) &((*tail)->next);
		}
	}

	if (!found) {
		list = MALLOC(pm->wlc->osh, sizeof(wl_p2p_wfds_hash_list_t));

		if (!list) {
			WL_TRACE(("wl%d: current number of adv items are %d \n",
				pm->wlc->pub->unit, pd->num_wfds_adv));
			err = BCME_NOMEM;
			goto exit;
		}

		bcopy(hash, &(list->adv_data), sizeof(wl_p2p_wfds_hash_t));
		list->next = pd->reg_adv_svc_list; /* Push to head new add's */

		pd->reg_adv_svc_list = list;

		pd->num_wfds_adv++;
		err = BCME_OK;
	}
exit:
	return err;
}

static int
wlc_p2p_del_wfds_hash(wlc_p2p_info_t *pm, wl_p2p_wfds_hash_t *hash)
{
	wlc_p2p_data_t *pd = pm->p2p_data;
	int err = BCME_NOTFOUND;
	wl_p2p_wfds_hash_list_t *list, *prev = NULL;

	WL_TRACE(("wl%d: wlc_p2p_del_wfds_hash(%x %x %x %x %x %x)\n",
	          pm->wlc->pub->unit, hash->wfds_hash[0], hash->wfds_hash[1], hash->wfds_hash[2],
	          hash->wfds_hash[3], hash->wfds_hash[4], hash->wfds_hash[5]));

	WL_TRACE(("wl%d: current number of adv items are %d \n",
		pm->wlc->pub->unit, pd->num_wfds_adv));

	list = pd->reg_adv_svc_list;
	while (list) {
		if (!bcmp(list->adv_data.wfds_hash, hash->wfds_hash, P2P_WFDS_HASH_LEN)) {

			pd->num_wfds_adv--;

			if (list == pd->reg_adv_svc_list)
				pd->reg_adv_svc_list = list->next;
			else
				prev->next = list->next;

			MFREE(pm->wlc->osh, list, sizeof(wl_p2p_wfds_hash_list_t));
			err = BCME_OK;
			break;
		}
		prev = list;
		list = list->next;
	}
	return err;
}

#endif /* WLWFDS */

#ifdef WLRSDB
wlc_bsscfg_t *
wlc_p2p_rsdb_disc_config(wlc_p2p_info_t *pm)
{
	if (!wlc_p2p_ensure_disc_state(pm)) {
		return NULL;
	}
	return pm->p2p_data->devcfg;
}
#endif /* WLRSDB */

static bool
wlc_p2p_ensure_disc_state(wlc_p2p_info_t *pm)
{
	/* First, we must be in Disc state */
	if (!wlc_p2p_disc_state(pm))
		return FALSE;
#ifdef WLRSDB
	/* In RSDB, we ensure that discovery cfg is moved to WLC where a GO/GC cfg is present.
	 * If no GO/GC interface are present then it is left in 2G-WLC.
	 * In MIMO Mode, discovery cfg is always on primary WLC
	 */
	if (RSDB_ENAB(pm->wlc->pub)) {
		wlc_p2p_data_t *pd = pm->p2p_data;
		wlc_info_t *from_wlc = pd->devcfg->wlc;
		wlc_bsscfg_t *to_cfg = NULL;
		wlc_info_t *wlc_2g, *wlc_5g, *to_wlc = NULL;
		int is_rsdb_mode = WLC_RSDB_DUAL_MAC_MODE(WLC_RSDB_CURR_MODE(from_wlc));
		int err = BCME_OK;

		/* If not RSDB mode, use primary wlc. In case of RSDB Mode find if there is any
		 * GO/GC bsscfg is present. If present then move the discovery cfg to the WLC
		 * where GO/GC is present
		 */
		if (is_rsdb_mode) {
			wlc_info_t *wlc = from_wlc;
			int idx, idx2;
			wlc_info_t *wlc_iter;
			wlc_bsscfg_t *cfg_iter;

			FOREACH_WLC(wlc->cmn, idx, wlc_iter) {
				FOREACH_P2P_BSS(wlc_iter, idx2, cfg_iter) {
					if (!BSS_P2P_DISC_ENAB(wlc_iter, cfg_iter) &&
						cfg_iter->up) {
						to_wlc = wlc_iter;
						break;
					}
				}

				if (to_wlc != NULL)
					break;
			}

			/* to_wlc is still NULL, indicates no GO/GC interface present. Hence move
			 * it to 2G-WLC
			 */
			wlc_rsdb_get_wlcs(from_wlc, &wlc_2g, &wlc_5g);
			if (to_wlc == NULL) {
				to_wlc = wlc_2g;
			}
		} else {
			to_wlc = WLC_RSDB_GET_PRIMARY_WLC(from_wlc);
		}

		/* Check if discovery is already present on required WLC */
		if (from_wlc == to_wlc) {
			if (is_rsdb_mode && to_wlc != wlc_2g) {
				if (wlc_2g->clk)
					wlc_mcnx_ra_set(wlc_2g->mcnx, pd->devcfg);
			}
			return TRUE;
		}
		/* Clone cfg..
		 * Stop discovery in from_wlc
		 * Start discovery in to_wlc (current wlc)
		*/

		/* Clone will free this cfg, ensure we stop all work on this current CFG */
		wlc_mcnx_ra_unset(from_wlc->mcnx, pd->devcfg);
		wlc_enable_probe_req(from_wlc, PROBE_REQ_PROBRESP_P2P_MASK, 0);

		to_cfg = wlc_rsdb_bsscfg_clone(from_wlc, to_wlc, pd->devcfg, &err);

		ASSERT(to_cfg != NULL);

		if ((err == BCME_OK) && to_cfg) {
			pd->devcfg = to_cfg;
			wlc_mcnx_ra_set(to_wlc->mcnx, pd->devcfg);

			if (is_rsdb_mode && to_wlc != wlc_2g && wlc_2g->clk) {
				wlc_mcnx_ra_set(wlc_2g->mcnx, pd->devcfg);
			}
			return TRUE;
		}
		/* Not supposed to come here, bsscfg_clone failed */
		WL_ERROR(("wl%d %s RSDB: bsscfg clone failure (error=%d/newcfg=%p)\n",
			to_wlc->pub->unit, __FUNCTION__, err, OSL_OBFUSCATE_BUF(to_cfg)));
		ASSERT(0);
	}
#endif /* WLRSDB */
	return TRUE;
}

/* debug... */

#if defined(WL_NAN_PD_P2P)
static void
nan_p2p_op_attr_parse_fn_cb(void *ctx, wlc_nan_ie_attr_parse_cb_data_t *data)
{
	wl_p2p_nan_inst_id_list_t *id = (wl_p2p_nan_inst_id_list_t *)ctx;
	wlc_p2p_info_t *pm = NULL;
	wlc_info_t *wlc = NULL;
	wifi_nan_p2p_op_attr_t *p2pattr = NULL;
	uint8 *cp = NULL;
	chanspec_t chanspec = 0;
	uint8 resolution = 0, repeat = 0, map_id = 0;
	uint8 bmap_size = 0;
	uint32 avail_bmp = 0;

	ASSERT(id != NULL);
	ASSERT(ctx != NULL);
	ASSERT(data != NULL);
	ASSERT(data->attr_id == NAN_ATTR_P2P);
	ASSERT(data->frm_types_bitmap & NAN_FRM_SVC_DISC);
	ASSERT(data->hdr != NULL);

	pm = (wlc_p2p_info_t *) id->p2pinfo;
	ASSERT(pm != NULL);

	cp = data->buf;

	wlc = ((wlc_p2p_info_t *)pm)->wlc;

	if (pm->p2p_data->p2p_nan_cfg) {
		p2pattr = (wifi_nan_p2p_op_attr_t *) cp;

		map_id = p2pattr->map_ctrl & NAN_MAPCTRL_IDMASK;
		repeat = (p2pattr->map_ctrl & NAN_MAPCTRL_REPEAT) >> NAN_MAPCTRL_REPEATSHIFT;

		resolution = (p2pattr->map_ctrl & NAN_MAPCTRL_DURMASK) >> NAN_MAPCTRL_DURSHIFT;
		switch (resolution)
		{
			case NAN_AVAIL_RES_32_TU:
				bmap_size = 2;
			break;
			case NAN_AVAIL_RES_64_TU:
				bmap_size = 1;
			break;
			default:
				bmap_size = 4;
		}
		memcpy(&avail_bmp, &p2pattr->avail_bmp, bmap_size);
		/* retrieve chanspec from FAM */
		chanspec = wlc_tsmap_retrieve_chanspec(wlc, avail_bmp, resolution, map_id);

		/* Generate p2p Attr event */
		if (nan_event_p2p_availability(wlc->nan, p2pattr->dev_role, &data->hdr->sa,
			&p2pattr->p2p_dev_addr, resolution,	repeat,
			avail_bmp, chanspec) != BCME_OK) {
			WL_ERROR(("wl%d: %s:failed to send p2p attr event of peer "
				"%02x%02x%02x%02x%02x%02x\n", wlc->pub->unit, __FUNCTION__,
				ETHERP_TO_MACF(&p2pattr->p2p_dev_addr)));
			return;
		}
	}
	return;
}

static uint16 nan_p2p_op_attr_len_cb(void *ctx, wlc_nan_ie_attr_build_cb_data_t *data)
{
	wl_p2p_nan_inst_id_list_t *id = (wl_p2p_nan_inst_id_list_t *)ctx;
	wlc_p2p_info_t *pm = NULL;
	uint total_len = 0;
	pm = (wlc_p2p_info_t *) id->p2pinfo;
	uint8 bmp_size = 0;
	ASSERT(pm != NULL);

	if (data->attr_id == NAN_ATTR_P2P && pm && pm->p2p_data->p2p_nan_cfg) {
		total_len = OFFSETOF(wifi_nan_p2p_op_attr_t, avail_bmp);
		switch (pm->p2p_data->p2p_nan_cfg->resolution)
		{
			case NAN_AVAIL_RES_32_TU:
				bmp_size = 2; /* two octets */
				break;
			case NAN_AVAIL_RES_64_TU:
				bmp_size = 1; /* one octet */
				break;
			default:
				bmp_size = 4; /* four octet */
		}
		total_len += bmp_size;
	}

	return total_len;
}

/* TODO: Add map id */
static uint16 nan_p2p_op_attr_build_fn_cb(void *ctx, wlc_nan_ie_attr_build_cb_data_t *data)
{
	wl_p2p_nan_inst_id_list_t *id = (wl_p2p_nan_inst_id_list_t *)ctx;
	wlc_p2p_info_t *pm = NULL;
	wifi_nan_p2p_op_attr_t *p2pattr;
	uint8 *cp = NULL;
	uint16 total_len = 0;
	uint8 bmap_size = 0;

	ASSERT(id != NULL);
	pm = (wlc_p2p_info_t *) id->p2pinfo;
	ASSERT(pm != NULL);
	ASSERT(data != NULL);
	cp = data->buf;
	ASSERT(cp != NULL);

	if (pm->p2p_data->p2p_nan_cfg) {
		p2pattr = (wifi_nan_p2p_op_attr_t *) cp;
		p2pattr->id = NAN_ATTR_P2P;

		total_len = nan_p2p_op_attr_len_cb(ctx, data);
		htol16_ua_store((total_len-NAN_ATTR_HDR_LEN), &p2pattr->len);

		p2pattr->dev_role = pm->p2p_data->p2p_nan_cfg->dev_role;

		bcopy(&pm->p2p_data->p2p_nan_cfg->dev_mac, &p2pattr->p2p_dev_addr, ETHER_ADDR_LEN);
	    /* TODO: Fix map id */
		p2pattr->map_ctrl |= NAN_MAP_ID_2G;

		p2pattr->map_ctrl |=
			(pm->p2p_data->p2p_nan_cfg->resolution << NAN_MAPCTRL_DURSHIFT);
		if (pm->p2p_data->p2p_nan_cfg->repeat)
			p2pattr->map_ctrl |= NAN_MAPCTRL_REPEAT;

		switch (pm->p2p_data->p2p_nan_cfg->resolution)
		{
			case NAN_AVAIL_RES_32_TU:
				bmap_size = 2;
			break;
			case NAN_AVAIL_RES_64_TU:
				bmap_size = 1;
			break;
			default:
				bmap_size = 4;
		}
		memcpy(&p2pattr->avail_bmp, &pm->p2p_data->p2p_nan_cfg->avail_bmap, bmap_size);
		return total_len;
	} else {
		return 0;
	}
}

/*
 *  look up p2p_nan_ioc cmd handler
 */

static uint16 BCMRAMFN(getentries_p2p_nan_ioctls)(void)
{
	return TLEN(p2p_nan_ioctls);
}

static const p2p_nan_ioc_cmd_t *wlc_p2p_nan_find_cmd_handler(const p2p_nan_ioc_cmd_t *cmd_tab,
        uint16 cmd)
{
	const p2p_nan_ioc_cmd_t *ioc_cmd = NULL;
	uint16 cmdtab_sz = getentries_p2p_nan_ioctls();

	ASSERT(cmd_tab);

	WL_TRACE((":p2p nan ioc_tab sz:%d\n", cmdtab_sz));

	/* TODO: add validation for set/get and min size */

	while (cmdtab_sz-- != 0) { /* valid cmd id can't be zero */
		if (cmd_tab->cmd == cmd) {
			WL_TRACE(("found cmd:%d, minlen:%d, cbfn:%p,\n",
				cmd_tab->cmd, cmd_tab->min_len,
				OSL_OBFUSCATE_BUF(cmd_tab->handler)));
			/* found cmd descriptor  */
			ioc_cmd = cmd_tab;
			break;
		}
		cmd_tab++;
	}
	return ioc_cmd;
}

static void
wlc_p2p_nan_fill_svc_list(wlc_p2p_info_t *pm, wl_nan_svc_inst_list_t *iocres, uint32 buflen)
{
	wl_p2p_nan_inst_id_list_t *id = pm->p2p_data->ids_head;
	uint32 cnt = 0, size = 0;

	while (id && size < buflen) {
		iocres->svc[cnt].inst_id = id->inst_id;
		iocres->svc[cnt].inst_type = id->inst_type;
		cnt++;
		size += sizeof(wl_nan_svc_inst_t);
		id = id->next;
	}
	iocres->count = cnt;
}

static void
wlc_p2p_nan_free_instance_id(wlc_p2p_info_t *pm, wl_p2p_nan_inst_id_list_t *id)
{
	wlc_nan_ie_attr_build_fn_unregister
		(pm->wlc, id->nan_build_cb_handle);
	wlc_nan_ie_attr_parse_fn_unregister
		(pm->wlc, id->nan_parse_cb_handle);
	MFREE(pm->wlc->osh, id, sizeof(wl_p2p_nan_inst_id_list_t));
}

static void
wlc_p2p_nan_del_instance_id(wlc_p2p_info_t *pm, wl_nan_instance_id_t inst_id, uint8 inst_type)
{
	wl_p2p_nan_inst_id_list_t *id = pm->p2p_data->ids_head;
	wl_p2p_nan_inst_id_list_t *prev_id = pm->p2p_data->ids_head;

	while (id) {
		if (id->inst_id == inst_id && id->inst_type == inst_type) {
			if (id == pm->p2p_data->ids_head) {
				pm->p2p_data->ids_head = id->next;
				prev_id = id->next;
				wlc_p2p_nan_free_instance_id(pm, id);
				id = prev_id;
			} else {
				prev_id->next = id->next;
				wlc_p2p_nan_free_instance_id(pm, id);
				id = prev_id->next;
			}
		} else {
			prev_id = id;
			id = id->next;
		}
	}
}

static void
wlc_p2p_nan_free_instance_id_list(wlc_p2p_info_t *pm)
{
	wl_p2p_nan_inst_id_list_t *cur = pm->p2p_data->ids_head;
	wl_p2p_nan_inst_id_list_t *next = NULL;

	while (cur) {
		next = cur->next;
		wlc_p2p_nan_free_instance_id(pm, cur);
		cur = next;
	}
	pm->p2p_data->ids_head = NULL;
}

static int
wlc_p2p_nan_add_instance_id(wlc_p2p_info_t *pm, wl_nan_instance_id_t inst_id, uint8 inst_type)
{
	wl_p2p_nan_inst_id_list_t *id = NULL;
	wl_p2p_nan_inst_id_list_t *current = pm->p2p_data->ids_head;
	wl_p2p_nan_inst_id_list_t *prev = NULL;
	wlc_info_t *wlc = pm->wlc;
	int res = BCME_OK;

	while (current != NULL) {
		if (inst_id == current->inst_id &&
			inst_type == current->inst_type) {
			WL_ERROR(("wl%d: %s: inst id %u and type %u exist\n",
				wlc->pub->unit, __FUNCTION__, inst_id, inst_type));
			res = BCME_OK;
			goto fail;
		}
		/* Store 'current' before updating it */
		prev = current;
		current = current->next;
	}

	if ((id = MALLOCZ(wlc->osh, sizeof(wl_p2p_nan_inst_id_list_t))) == NULL) {
		WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes\n",
			wlc->pub->unit, __FUNCTION__, MALLOCED(wlc->osh)));
		res = BCME_NOMEM;
		goto fail;
	}
	id->p2pinfo = pm;
	id->nan_build_cb_handle = wlc_nan_ie_attr_build_fn_register(wlc,
		NAN_ATTR_P2P, (NAN_FRM_SVC_DISC),
		NAN_ATTR_P2P, nan_p2p_op_attr_len_cb,
		nan_p2p_op_attr_build_fn_cb, id, inst_id);
	if (!id->nan_build_cb_handle) {
		WL_ERROR(("wl%d: %s: wlc_nan_ie_attr_build_fn_register "
			"for p2p failed \n", wlc->pub->unit, __FUNCTION__));
		res = BCME_ERROR;
		goto fail;
	}
	id->nan_parse_cb_handle = wlc_nan_ie_attr_parse_fn_register(wlc, wlc->nan,
		NAN_ATTR_P2P, (NAN_FRM_SVC_DISC),
		nan_p2p_op_attr_parse_fn_cb, id);
	if (!id->nan_parse_cb_handle) {
		WL_ERROR(("wl%d: %s: wlc_nan_ie_attr_parse_fn_register "
			"for p2p failed \n", wlc->pub->unit, __FUNCTION__));
		res = BCME_ERROR;
		goto fail;
	}
	id->inst_id = inst_id;
	id->inst_type = inst_type;
	if (prev == NULL)
		pm->p2p_data->ids_head = id;
	else
		prev->next = id;

	return res;
fail:
	if (id)
		MFREE(wlc->osh, id, sizeof(wl_p2p_nan_inst_id_list_t));
	return res;
}

static int
wlc_p2p_nan_handle_new_config(wlc_p2p_info_t *pm, wl_p2p_nan_config_t *config,
	uint16 configlen)
{
	wlc_info_t *wlc = pm->wlc;
	wl_nan_sched_svc_timeslot_t p2p_ts;
	int  ret = BCME_OK;

	if (pm->p2p_data->p2p_nan_cfg) {
		WL_ERROR(("wl%d: %s: Config exists, delete first or "
			"use ADD/DEL config type\n",
			wlc->pub->unit, __FUNCTION__));
		return BCME_BADARG;
	}
	if ((pm->p2p_data->p2p_nan_cfg = (wl_p2p_nan_config_t*)
		MALLOCZ(wlc->osh, configlen)) == NULL) {
		WL_ERROR(("wl%d: %s: out of mem, malloced %d bytes\n",
			wlc->pub->unit, __FUNCTION__, MALLOCED(wlc->osh)));
		return BCME_NOMEM;
	}
	/* Convert bitmap and channel to wl_nan_sched_svc_timeslot_t format */
	memset(&p2p_ts, 0, sizeof(wl_nan_sched_svc_timeslot_t));
	/* TODO: Change uint16 chanspec_t to uint32 allover */
	if (wlc_convert_timeslot_format(0, 0, (chanspec_t)config->chanspec,
		&p2p_ts, config->avail_bmap) != BCME_OK) {
		WL_ERROR(("wl%d: %s: Timeslot convertion failed\n",
			wlc->pub->unit, __FUNCTION__));
		return BCME_BADARG;
	}
	if ((ret = postdisc_timeslot_reserve(wlc, &p2p_ts, NAN_ATTR_P2P))
		!= BCME_OK) {
		WL_ERROR(("wl%d: %s: Timeslot reservation failed\n",
			wlc->pub->unit, __FUNCTION__));
		return BCME_BADARG;
	}

	pm->p2p_data->p2p_nan_cfg->version = config->version;
	pm->p2p_data->p2p_nan_cfg->len = config->len;
	pm->p2p_data->p2p_nan_cfg->dev_role = config->dev_role;
	pm->p2p_data->p2p_nan_cfg->chanspec = config->chanspec;
	pm->p2p_data->p2p_nan_cfg->resolution = config->resolution;
	pm->p2p_data->p2p_nan_cfg->repeat = config->repeat;
	memcpy(&pm->p2p_data->p2p_nan_cfg->dev_mac, &config->dev_mac,
		sizeof(struct ether_addr));
	pm->p2p_data->p2p_nan_cfg->avail_bmap = config->avail_bmap;
	if (config->ie_len) {
		memcpy(pm->p2p_data->p2p_nan_cfg->ie, config->ie, config->ie_len);
	}
	pm->p2p_data->p2p_nan_cfg->ie_len = config->ie_len;
	if ((ret = wlc_p2p_nan_add_instance_id
		(pm, config->inst_id, config->inst_type))
		!= BCME_OK) {
		WL_ERROR(("wl%d: %s: Adding instance id failed\n",
			wlc->pub->unit, __FUNCTION__));
		if (pm->p2p_data->p2p_nan_cfg) {
			MFREE(wlc->osh, pm->p2p_data->p2p_nan_cfg, configlen);
			pm->p2p_data->p2p_nan_cfg = NULL;
		}
		return ret;
	}
	/* indicates time slot avaialble and build/parse
	 * function registering also successful. Schedule the availability window
	 */
	else {
		pm->p2p_data->favail_sched_handle = (void *)wlc_postdisc_sched_timeslot(
				pm->wlc, &p2p_ts, -1, 0,
				wlc_p2p_window_sched_upd, pm);

	}
	return BCME_OK;
}

static int
wlc_p2p_nan_handle_add_config(wlc_p2p_info_t *pm, wl_p2p_nan_config_t *config,
	uint16 configlen)
{
	wlc_info_t *wlc = pm->wlc;
	BCM_REFERENCE(wlc);

	if (!pm->p2p_data->p2p_nan_cfg) {
		WL_ERROR(("wl%d: %s: Config does not exists use NEW "
			"config type\n", wlc->pub->unit, __FUNCTION__));
		return BCME_BADARG;
	}
	if (wlc_p2p_nan_add_instance_id
		(pm, config->inst_id, config->inst_type)
		!= BCME_OK) {
		WL_ERROR(("wl%d: %s: Adding instance id failed\n",
			wlc->pub->unit, __FUNCTION__));
		return BCME_ERROR;
	}
	return BCME_OK;
}

static int
wlc_p2p_nan_handle_del_config(wlc_p2p_info_t *pm, wl_p2p_nan_config_t *config,
	uint16 configlen)
{
	wlc_info_t *wlc = pm->wlc;
	wl_nan_sched_svc_timeslot_t p2p_ts;
	int alloc_size;

	if (!pm->p2p_data->p2p_nan_cfg) {
		WL_ERROR(("wl%d: %s: Config does not exists use NEW "
			"config type\n", wlc->pub->unit,
			__FUNCTION__));
		return BCME_BADARG;
	}
	/* check if single instance id */
	if (pm->p2p_data->ids_head &&
		pm->p2p_data->ids_head->next == NULL &&
		pm->p2p_data->ids_head->inst_id == config->inst_id &&
		pm->p2p_data->ids_head->inst_type == config->inst_type) {
		WL_ERROR(("wl%d: %s: Single instance id, delete full config\n",
			wlc->pub->unit, __FUNCTION__));
		/* Convert bitmap and channel to wl_nan_sched_svc_timeslot_t format */
		memset(&p2p_ts, 0, sizeof(wl_nan_sched_svc_timeslot_t));
		/* TODO: Change uint16 chanspec_t to uint32 allover */
		if (wlc_convert_timeslot_format(0, 0,
			(chanspec_t)pm->p2p_data->p2p_nan_cfg->chanspec, &p2p_ts,
			pm->p2p_data->p2p_nan_cfg->avail_bmap) != BCME_OK) {
			WL_ERROR(("wl%d: %s: Timeslot convertion failed\n",
				wlc->pub->unit, __FUNCTION__));
			return BCME_ERROR;
		}
		/* Release reserved timeslot */
		if (postdisc_timeslot_release(wlc, &p2p_ts, NAN_ATTR_P2P)
				!= BCME_OK) {
			WL_ERROR(("wl%d: %s: Timeslot release failed\n",
			wlc->pub->unit, __FUNCTION__));
			return BCME_ERROR;
		}
		if (wlc_cancel_postdisc_timeslot(wlc, pm->p2p_data->favail_sched_handle)
				!= BCME_OK) {
			WL_ERROR(("wl%d: %s: postdisc timeslot cancel failed\n",
			wlc->pub->unit, __FUNCTION__));
			return BCME_ERROR;
		}
		wlc_p2p_nan_del_instance_id
			(pm, config->inst_id, config->inst_type);
		alloc_size = OFFSETOF(wl_p2p_nan_config_t, ie) +
			pm->p2p_data->p2p_nan_cfg->ie_len;
		MFREE(wlc->osh, pm->p2p_data->p2p_nan_cfg, alloc_size);
		pm->p2p_data->p2p_nan_cfg = NULL;
	} else {
		wlc_p2p_nan_del_instance_id
			(pm, config->inst_id, config->inst_type);
	}
	return BCME_OK;
}

static int
wlc_p2p_nan_config(wlc_p2p_info_t *pm, void *params, uint16 paramlen,
        void *result, uint16 buflen, bool set)
{
	wl_p2p_nan_config_t *config = params;
	wl_p2p_nan_config_t *iocres = result;
	wlc_info_t *wlc = NULL;
	int len = 0;
	int  ret = BCME_OK;

	ASSERT(pm != NULL);
	wlc = pm->wlc;
	BCM_REFERENCE(wlc);

	if (set) {
		ASSERT(config != NULL);
		/* TODO: If version mismatch is there handle it here */
		if (config->version != WL_P2P_NAN_CONFIG_VERSION) {
			WL_ERROR(("wl%d: %s: config version mismatch\n",
				wlc->pub->unit, __FUNCTION__));
			return BCME_BADARG;
		}
		WL_TRACE(("wl%d: %s:config len %u paramlen %u ie_len %u flags %u"
			"role %0x chanspec 0x%x resol %u repeat %u bmap %0x\n",
			wlc->pub->unit, __FUNCTION__, config->len, paramlen, config->ie_len,
			config->flags, config->dev_role, config->chanspec, config->resolution,
			config->repeat, config->avail_bmap));
		/* length check */
		if ((config->len != paramlen) ||
			(config->ie_len != (paramlen - OFFSETOF(wl_p2p_nan_config_t, ie)))) {
			WL_ERROR(("wl%d: %s: i/p len mismatch\n", wlc->pub->unit, __FUNCTION__));
			return BCME_BADARG;
		}

		if (config->flags & WL_P2P_NAN_CONFIG_NEW) {
			if ((ret = wlc_p2p_nan_handle_new_config(pm, config, paramlen))
				!= BCME_OK) {
				WL_ERROR(("wl%d: %s: new config handle failed\n", wlc->pub->unit,
					__FUNCTION__));
				return ret;
			}
		} else if (config->flags & WL_P2P_NAN_CONFIG_ADD) {
			if ((ret = wlc_p2p_nan_handle_add_config(pm, config, paramlen))
				!= BCME_OK) {
				WL_ERROR(("wl%d: %s: add config handle failed\n", wlc->pub->unit,
					__FUNCTION__));
				return ret;
			}
		} else if (config->flags & WL_P2P_NAN_CONFIG_DEL) {
			if ((ret = wlc_p2p_nan_handle_del_config(pm, config, paramlen))
				!= BCME_OK) {
				WL_ERROR(("wl%d: %s: del config handle failed\n", wlc->pub->unit,
					__FUNCTION__));
				return ret;
			}
		} else {
			WL_ERROR(("wl%d: %s: Wrong config type %0x\n",
				wlc->pub->unit, __FUNCTION__, config->flags));
			return BCME_UNSUPPORTED;
		}
	} else { /* Get */
		ASSERT(iocres != NULL);
		bzero((char *)iocres, buflen);

		if (pm->p2p_data->p2p_nan_cfg) {
			len = OFFSETOF(wl_p2p_nan_config_t, ie)+ pm->p2p_data->p2p_nan_cfg->ie_len;
			if (buflen < len)
				return BCME_BUFTOOSHORT;
			iocres->version = pm->p2p_data->p2p_nan_cfg->version;
			iocres->len = pm->p2p_data->p2p_nan_cfg->len;
			iocres->dev_role = pm->p2p_data->p2p_nan_cfg->dev_role;
			iocres->chanspec = pm->p2p_data->p2p_nan_cfg->chanspec;
			iocres->resolution = pm->p2p_data->p2p_nan_cfg->resolution;
			iocres->repeat = pm->p2p_data->p2p_nan_cfg->repeat;
			iocres->avail_bmap = pm->p2p_data->p2p_nan_cfg->avail_bmap;
			memcpy(&iocres->dev_mac, &pm->p2p_data->p2p_nan_cfg->dev_mac,
				sizeof(struct ether_addr));
			iocres->ie_len = pm->p2p_data->p2p_nan_cfg->ie_len;
			if (pm->p2p_data->p2p_nan_cfg->ie_len) {
				memcpy(iocres->ie, pm->p2p_data->p2p_nan_cfg->ie,
					pm->p2p_data->p2p_nan_cfg->ie_len);
			}
		}
	}

	return BCME_OK;
}

static int
wlc_p2p_nan_del_config(wlc_p2p_info_t *pm, void *params,
        uint16 paramlen, void *result, uint16 buflen, bool set)
{
	int alloc_size;
	wlc_info_t *wlc = NULL;
	wl_nan_sched_svc_timeslot_t p2p_ts;

	ASSERT(pm != NULL);
	wlc = pm->wlc;

	if (set) {
		if (pm->p2p_data->p2p_nan_cfg) {
			/* Convert bitmap and channel to wl_nan_sched_svc_timeslot_t format */
			memset(&p2p_ts, 0, sizeof(wl_nan_sched_svc_timeslot_t));
			/* TODO: Change uint16 chanspec_t to uint32 allover */
			if (wlc_convert_timeslot_format(0, 0,
				(chanspec_t)pm->p2p_data->p2p_nan_cfg->chanspec, &p2p_ts,
			        pm->p2p_data->p2p_nan_cfg->avail_bmap) != BCME_OK) {
			        WL_ERROR(("wl%d: %s: Timeslot convertion failed\n",
			                wlc->pub->unit, __FUNCTION__));
			        return BCME_ERROR;
			}
			printf("bitmap %x res %u map id %u\n", p2p_ts.abitmap,
				p2p_ts.res, p2p_ts.mapid);
			/* Release reserved timeslot */
			if (postdisc_timeslot_release(wlc, &p2p_ts, NAN_ATTR_P2P) != BCME_OK) {
				WL_ERROR(("wl%d: %s: Timeslot release failed\n",
					wlc->pub->unit, __FUNCTION__));
				return BCME_ERROR;
			}
			if (wlc_cancel_postdisc_timeslot(wlc, pm->p2p_data->favail_sched_handle)
				!= BCME_OK) {
				WL_ERROR(("wl%d: %s: postdisc timeslot cancel failed\n",
					wlc->pub->unit, __FUNCTION__));
				return BCME_ERROR;
			}
			wlc_p2p_nan_free_instance_id_list(pm);
			alloc_size = OFFSETOF(wl_p2p_nan_config_t, ie) +
				pm->p2p_data->p2p_nan_cfg->ie_len;
			MFREE(wlc->osh, pm->p2p_data->p2p_nan_cfg, alloc_size);
			pm->p2p_data->p2p_nan_cfg = NULL;
		}
	} else {
		return BCME_UNSUPPORTED;
	}

	return BCME_OK;
}

static int
wlc_p2p_nan_get_svc_inst_list(wlc_p2p_info_t *pm, void *params, uint16 paramlen,
        void *result, uint16 buflen, bool set)
{
	wl_nan_svc_inst_list_t *iocres = result;

	ASSERT(pm != NULL);

	if (set) {
		return BCME_UNSUPPORTED;
	} else { /* Get */
		ASSERT(iocres != NULL);
		bzero((char *)iocres, buflen);
		if (pm->p2p_data->ids_head) {
			if (buflen < (OFFSETOF(wl_nan_svc_inst_list_t, svc) +
				sizeof(wl_nan_svc_inst_t)))
				return BCME_BUFTOOSHORT;
			wlc_p2p_nan_fill_svc_list(pm, iocres,
				(buflen - OFFSETOF(wl_nan_svc_inst_list_t, svc)));
		}
	}

	return BCME_OK;
}

static void wlc_p2p_window_sched_upd(void *ctx, wlc_nan_sched_upd_data_t *notif_data)
{
	wlc_p2p_info_t *pm = ctx;
	uint8 *window_data = NULL; /* Todo: does the host need any data at beginning of window? */
	uint16 window_len = 0;
	wlc_nan_info_t *naninfo = pm->wlc->nan;
	int res = BCME_OK;

	switch (notif_data->state)
	{
		case WLC_NAN_STATE_FURTHER_AVAILABILITY_START:
			pm->p2p_data->dw_counter++;
			/* send event signifying beginning of availability window for post disc
			operations
			*/
			res = nan_event_postdisc_window_begin(naninfo, window_data, window_len,
				NAN_ATTR_P2P);
			if (res != BCME_OK) {
				/* Error in sending event */
			}
			/* to do at beginning of window */
			break;
		case WLC_NAN_STATE_FURTHER_AVAILABILITY_END:
			/* Todo: At the end of the availability window */
			break;
		default:
			break;
	}

}

#endif	/* WL_NAN_PD_P2P */

#endif	/* WLP2P */