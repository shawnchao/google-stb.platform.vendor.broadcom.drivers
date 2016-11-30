/*
 * ACPHY VCO CAL module implementation
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
 * $Id: phy_ac_vcocal.c 650142 2016-07-20 17:06:54Z ernst $
 */

#include <typedefs.h>
#include <bcmdefs.h>
#include <phy_dbg.h>
#include <phy_mem.h>
#include <phy_type_vcocal.h>
#include <phy_ac.h>
#include <phy_ac_vcocal.h>
#include <phy_cache_api.h>

#include <phy_ac_info.h>
#include <phy_ac_radio.h>
#include <wlc_phy_radio.h>
#include <phy_utils_reg.h>
#include <wlc_radioreg_20691.h>
#include <wlc_radioreg_20693.h>
#include <wlc_radioreg_20694.h>
#include <wlc_radioreg_20695.h>
#include <wlc_radioreg_20696.h>

#include <phy_rstr.h>
#include <phy_utils_var.h>

/* module private states */
struct phy_ac_vcocal_info {
	phy_info_t			*pi;
	phy_ac_info_t		*aci;
	phy_vcocal_info_t	*cmn_info;
	uint8				pll_mode;
/* add other variable size variables here at the end */
	acphy_vcocal_radregs_t ac_vcocal_radioregs_orig;
	bool vcotune;
};

/* local functions */
static void wlc_phy_force_vcocal_acphy(phy_type_vcocal_ctx_t *ctx);
static void phy_ac_vcocal_nvram_attach(phy_ac_vcocal_info_t *vcocali);

/* register phy type specific implementation */
phy_ac_vcocal_info_t *
BCMATTACHFN(phy_ac_vcocal_register_impl)(phy_info_t *pi, phy_ac_info_t *aci,
	phy_vcocal_info_t *cmn_info)
{
	phy_ac_vcocal_info_t *ac_info;
	phy_type_vcocal_fns_t fns;

	PHY_CAL(("%s\n", __FUNCTION__));

	/* allocate all storage together */
	if ((ac_info = phy_malloc(pi, sizeof(phy_ac_vcocal_info_t))) == NULL) {
		PHY_ERROR(("%s: phy_malloc failed\n", __FUNCTION__));
		goto fail;
	}

	/* Initialize params */
	ac_info->pi = pi;
	ac_info->aci = aci;
	ac_info->cmn_info = cmn_info;

	ac_info->pll_mode = 0;

	/* register PHY type specific implementation */
	bzero(&fns, sizeof(fns));
#if defined(BCMINTERNAL) || defined(WLTEST)
	fns.force = wlc_phy_force_vcocal_acphy;
#endif /* defined(BCMINTERNAL) || defined(WLTEST) */
	fns.ctx = ac_info;

	/* Read srom params from nvram */
	phy_ac_vcocal_nvram_attach(ac_info);

	if (phy_vcocal_register_impl(cmn_info, &fns) != BCME_OK) {
		PHY_ERROR(("%s: phy_vcocal_register_impl failed\n", __FUNCTION__));
		goto fail;
	}

	return ac_info;

	/* error handling */
fail:
	if (ac_info != NULL)
		phy_mfree(pi, ac_info, sizeof(phy_ac_vcocal_info_t));
	return NULL;
}

void
BCMATTACHFN(phy_ac_vcocal_unregister_impl)(phy_ac_vcocal_info_t *ac_info)
{
	phy_vcocal_info_t *cmn_info;
	phy_info_t *pi;

	ASSERT(ac_info);
	pi = ac_info->pi;
	cmn_info = ac_info->cmn_info;

	PHY_CAL(("%s\n", __FUNCTION__));

	/* unregister from common */
	phy_vcocal_unregister_impl(cmn_info);

	phy_mfree(pi, ac_info, sizeof(phy_ac_vcocal_info_t));
}

/* ********************************************* */
/*				Internal Definitions					*/
/* ********************************************* */
static void
wlc_phy_force_vcocal_acphy(phy_type_vcocal_ctx_t *ctx)
{
	phy_ac_vcocal_info_t *info = (phy_ac_vcocal_info_t *)ctx;
	phy_info_t *pi = info->pi;
	/* IOVAR call */
	if (TINY_RADIO(pi)) {
		if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) {
			/* turn on VCO Calibration clock */
			MOD_RADIO_REG_20691(pi, PLL_XTAL2, 0, xtal_pu_caldrv, 0x1);
		}
		if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) &&
			RADIOMAJORREV(pi) == 3) {
			uint16 phymode = phy_get_phymode(pi);
			if (phymode == PHYMODE_3x3_1x1) {
				wlc_phy_radio_tiny_vcocal_wave2(pi, 0, 4, 1, FALSE);
			} else {
				wlc_phy_radio_tiny_vcocal_wave2(pi, 0, 1, 1, TRUE);
			}
		} else {
			wlc_phy_radio_tiny_vcocal(pi);
		}
	} else if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		wlc_phy_28nm_radio_vcocal(pi, VCO_CAL_MODE_20695,
				VCO_CAL_COUPLING_MODE_20695);
	} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
		wlc_phy_20696_radio_vcocal(pi, VCO_CAL_MODE_20696,
				VCO_CAL_COUPLING_MODE_20696);
	} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		wlc_phy_20694_radio_vcocal(pi, 0, 1);
	} else {
		wlc_phy_radio2069_vcocal(pi);
	}
}

static void
BCMATTACHFN(phy_ac_vcocal_nvram_attach)(phy_ac_vcocal_info_t *vcocali)
{
	phy_info_t *pi = vcocali->pi;

	/* Enabling VCO settings to lock at lower VCO swing to improve regulatory margins */
	vcocali->vcotune = (bool)PHY_GETINTVAR_DEFAULT_SLICE(pi, rstr_vcotune, 0);
}
/* ********************************************* */
/*				External Functions					*/
/* ********************************************* */
void
wlc_phy_radio_tiny_vcocal(phy_info_t *pi)
{

	uint8 core;
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;
	acphy_vcocal_radregs_t *porig = &(pi_ac->vcocali->ac_vcocal_radioregs_orig);

	ASSERT(TINY_RADIO(pi));

	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
		ASSERT(!porig->is_orig);
		porig->is_orig = TRUE;

		/* Save the radio regs for core 0 */
		porig->clk_div_ovr1 = _READ_RADIO_REG(pi, RADIO_REG_20693(pi, CLK_DIV_OVR1, 0));
		porig->clk_div_cfg1 = _READ_RADIO_REG(pi, RADIO_REG_20693(pi, CLK_DIV_CFG1, 0));

		/* JIRA: SW4349-937 */
		/* In core0, afeclk_6/12g_xxx_mimo_xxx needs to be off */
		ACPHY_REG_LIST_START
			MOD_RADIO_REG_20693_ENTRY(pi, CLK_DIV_OVR1, 0, ovr_afeclkdiv_6g_mimo_pu, 1)
			MOD_RADIO_REG_20693_ENTRY(pi, CLK_DIV_OVR1, 0,
				ovr_afeclkdiv_12g_mimo_div2_pu, 1)
			MOD_RADIO_REG_20693_ENTRY(pi, CLK_DIV_OVR1, 0, ovr_afeclkdiv_12g_mimo_pu, 1)
			MOD_RADIO_REG_20693_ENTRY(pi, CLK_DIV_CFG1, 0, afe_clk_div_6g_mimo_pu, 0)
			MOD_RADIO_REG_20693_ENTRY(pi, CLK_DIV_CFG1, 0, afe_clk_div_12g_mimo_div2_pu,
				0)
			MOD_RADIO_REG_20693_ENTRY(pi, CLK_DIV_CFG1, 0, afe_clk_div_12g_mimo_pu, 0)
		ACPHY_REG_LIST_EXECUTE(pi);
	}

	FOREACH_CORE(pi, core) {
		/* cal not required for non-zero cores in MIMO bu required for 80p80 */
		if ((phy_get_phymode(pi) == PHYMODE_MIMO) && (core != 0)) {
			break;
		}
		if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) {
			MOD_RADIO_REG_20691(pi, PLL_HVLDO3, core, ldo_2p5_ldo_VCO_vout_sel, 0xf);
			MOD_RADIO_REG_20691(pi, PLL_HVLDO3, core, ldo_2p5_ldo_CP_vout_sel, 0xf);
		}

		if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID)) {
			wlc_phy_radio20693_vco_opt(pi, (pi_ac->vcocali->vcotune &&
				CHSPEC_IS5G(pi->radio_chanspec)));
		}

		/* VCO-Cal startup seq */
		/* VCO cal mode selection */
		/* Use legacy mode */
		MOD_RADIO_REG_TINY(pi, PLL_VCOCAL10, core, rfpll_vcocal_ovr_mode, 0);

		/* # TODO: The below registers have direct PHY control in 20691 (unlike 2069?)
		 * so this reset should ideally be done by writing phy registers
		 */
		/* # Reset delta-sigma modulator */
		MOD_RADIO_REG_TINY(pi, PLL_CFG2, core, rfpll_rst_n, 0);
		/* # Reset VCO cal */
		MOD_RADIO_REG_TINY(pi, PLL_VCOCAL13, core, rfpll_vcocal_rst_n, 0);
		/* # Reset start of VCO Cal */
		MOD_RADIO_REG_TINY(pi, PLL_VCOCAL1, core, rfpll_vcocal_cal, 0);
		if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) {
			/* # Disable PHY direct control for vcocal reset */
			MOD_RADIO_REG_20691(pi, RFPLL_OVR1, core, ovr_rfpll_vcocal_rst_n, 1);
			/* # Disable PHY direct control for delta-sigma modulator reset signal */
			MOD_RADIO_REG_20691(pi, RFPLL_OVR1, core, ovr_rfpll_rst_n, 1);
			/* # Disable PHY direct control for vcocal start */
			MOD_RADIO_REG_20691(pi, RFPLL_OVR1, core, ovr_rfpll_vcocal_cal, 1);
		}
	}
	OSL_DELAY(11);
	FOREACH_CORE(pi, core) {
		/* cal not required for non-zero cores in MIMO but required for 80p80 */
		if ((phy_get_phymode(pi) == PHYMODE_MIMO) && (core != 0)) {
			break;
		}
		/* # Release reset */
		MOD_RADIO_REG_TINY(pi, PLL_CFG2, core, rfpll_rst_n, 1);
		/* # Release reset */
		MOD_RADIO_REG_TINY(pi, PLL_VCOCAL13, core, rfpll_vcocal_rst_n, 1);
	}
	OSL_DELAY(1);
	/* # Start VCO Cal */
	FOREACH_CORE(pi, core) {
		/* cal not required for non-zero cores in MIMO but required for 80p80 */
		if ((phy_get_phymode(pi) == PHYMODE_MIMO) && (core != 0)) {
			break;
		}
		MOD_RADIO_REG_TINY(pi, PLL_VCOCAL1, core, rfpll_vcocal_cal, 1);
	}
}

void
wlc_phy_radio_tiny_afe_resynch(phy_info_t *pi, uint8 mode)
{
	uint8 core;

	FOREACH_CORE(pi, core) {
		MOD_RADIO_REG_20693(pi, CLK_DIV_CFG1, core,
		                    sel_common_pu_rst, 0);
		switch (core) {
			case 0: {
				MOD_RADIO_PLLREG_20693(pi, AFECLK_DIV_CFG1, 0,
					adc_bypass_reset_core0, 1);
				MOD_RADIO_PLLREG_20693(pi, AFECLK_DIV_CFG1, 0,
					dac_bypass_reset_core0, 1);
				}
				break;
			case 1: {
				MOD_RADIO_PLLREG_20693(pi, AFECLK_DIV_CFG1, 0,
					adc_bypass_reset_core1, 1);
				MOD_RADIO_PLLREG_20693(pi, AFECLK_DIV_CFG1, 0,
					dac_bypass_reset_core1, 1);
				}
				break;
			case 2: {
				MOD_RADIO_PLLREG_20693(pi, AFECLK_DIV_CFG1, 0,
					adc_bypass_reset_core2, 1);
				MOD_RADIO_PLLREG_20693(pi, AFECLK_DIV_CFG1, 0,
					dac_bypass_reset_core2, 1);
				}
				break;
			case 3: {
				MOD_RADIO_PLLREG_20693(pi, AFECLK_DIV_CFG1, 0,
					adc_bypass_reset_core3, 1);
				MOD_RADIO_PLLREG_20693(pi, AFECLK_DIV_CFG1, 0,
					dac_bypass_reset_core3, 1);
				}
				break;
		}
	}
}

void
wlc_phy_radio_tiny_vcocal_wave2(phy_info_t *pi, uint8 vcocal_mode, uint8 pll_mode,
                                uint8 coupling_mode, bool cache_calcode)
{
	// # VCO cal supports 15 different modes as following: set by vcocal_mode:
	// ## Mode 0.  Cold Init-4
	// ## Mode 1.  Cold Init-3
	// ## Mode 2.  Cold Norm-4
	// ## Mode 3.  Cold Norm-3
	// ## Mode 4.  Cold Norm-2
	// ## Mode 5.  Cold Norm-1
	// ## Mode 6.  Warm Init-4
	// ## Mode 7.  Warm Init-3
	// ## Mode 8.  Warm Norm-4
	// ## Mode 9.  Warm Norm-3
	// ## Mode 10. Warm Norm-2
	// ## Mode 11. Warm Norm-1
	// ## Mode 12. Warm Fast-2
	// ## Mode 13. Warm Fast-1
	// ## Mode 14. Warm Fast-0

	// # PLL/VCO operating modes: set by pll_mode:
	// ## Mode 0. RFP0 non-coupled: e.g. 4x4 MIMO non-1024QAM
	// ## Mode 1. RFP0 coupled: e.g. 4x4 MIMO 1024QAM
	// ## Mode 2. RFP0 non-coupled + RFP1 non-coupled: 2x2 + 2x2 MIMO non-1024QAM
	// ## Mode 3. RFP0 non-coupled + RFP1 coupled: 3x3 + 1x1 scanning in 80MHz mode
	// ## Mode 4. RFP0 coupled + RFP1 non-coupled: 3x3 MIMO 1024QAM + 1x1 scanning in 20MHz mode
	// ## Mode 5. RFP0 coupled + RFP1 coupled: 3x3 MIMO 1024QAM + 1x1 scanning in 80MHz mode
	// ## Mode 6. RFP1 non-coupled
	// ## Mode 7. RFP1 coupled

	// # When coupled VCO mode is ON, there are two modes: set by coupling_mode
	// ## Mode 0. Use Aux cap to calibrate LC mismatch between VCO1 & VCO2
	// ## Mode 1. Use Aux cap to calibrate K error

	uint8 core, start_core = 0, end_core = 0;
	uint16 rfpll_vcocal_FastSwitch_val[] =
		{3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0};
	uint16 rfpll_vcocal_slopeInOVR_val[] =
		{1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0};
	uint16 rfpll_vcocal_slopeIn_val[] =
		{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	uint16 rfpll_vcocal_FourthMesEn_val[] =
		{1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0};
	uint16 rfpll_vcocal_FixMSB_val[] =
		{3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 0, 0, 0};

	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;
	pi_ac->vcocali->pll_mode = pll_mode;

	ASSERT(TINY_RADIO(pi) && RADIOID_IS(pi->pubpi->radioid, BCM20693_ID));

	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && RADIOMAJORREV(pi) == 3) {
		PHY_INFORM(("%s: vcocal_mode=%d, pll_mode=%d\n",
				__FUNCTION__, vcocal_mode, pll_mode));
	}

	switch (pll_mode) {
	case 0:
	        // intentional fall through
	case 1:
	        start_core = 0;
		end_core   = 0;
		break;
	case 2:
	        // intentional fall through
	case 3:
	        // intentional fall through
	case 4:
	        // intentional fall through
	case 5:
	        start_core = 0;
		end_core   = 1;
		break;
	case 6:
	        // intentional fall through
	case 7:
	        start_core = 1;
		end_core   = 1;
		break;
	default:
		PHY_ERROR(("wl%d: %s: Unsupported PLL/VCO operating mode %d\n",
			pi->sh->unit, __FUNCTION__, pll_mode));
		ASSERT(0);
		return;
	}

	/* Turn on the VCO-cal buffer */
	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && RADIOMAJORREV(pi) == 3) {
		MOD_RADIO_PLLREG_20693(pi, WL_XTAL_CFG1, 0, wl_xtal_out_pu, 0x3b);
	}

	for (core = start_core; core <= end_core; core++) {
		uint8 ct;
		// Put all regs/fields write/modification in a array
		uint16 pll_regs_bit_vals[][3] = {
			RADIO_PLLREGC_FLD_20693(pi, PLL_VCOCAL1, core, rfpll_vcocal_FastSwitch,
			rfpll_vcocal_FastSwitch_val[vcocal_mode]),
			RADIO_PLLREGC_FLD_20693(pi, PLL_VCOCAL_OVR1, core, ovr_rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeInOVR_val[vcocal_mode]),
			RADIO_PLLREGC_FLD_20693(pi, PLL_VCOCAL15, core, rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeIn_val[vcocal_mode]),
			RADIO_PLLREGC_FLD_20693(pi, PLL_VCOCAL1, core, rfpll_vcocal_FourthMesEn,
			rfpll_vcocal_FourthMesEn_val[vcocal_mode]),
			RADIO_PLLREGC_FLD_20693(pi, PLL_VCOCAL1, core, rfpll_vcocal_FixMSB,
			rfpll_vcocal_FixMSB_val[vcocal_mode]),
		};

		// now write/modification to radio regs
		for (ct = 0; ct < ARRAYSIZE(pll_regs_bit_vals); ct++) {
			phy_utils_mod_radioreg(pi, pll_regs_bit_vals[ct][0],
			pll_regs_bit_vals[ct][1], pll_regs_bit_vals[ct][2]);
		}

		if (READ_RADIO_PLLREGFLD_20693(pi, PLL_VCOCAL24, core,
		                               rfpll_vcocal_enableCoupling)) {
			MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL24, core,
			                       rfpll_vcocal_couplingMode, coupling_mode);
		}

		if (vcocal_mode <= 5) {
			// Cold Start
			MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL1, core, rfpll_vcocal_enableCal, 1);
			MOD_RADIO_PLLREG_20693(pi, PLL_CFG2,    core, rfpll_rst_n,            0);
			MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL1, core, rfpll_vcocal_rst_n,     0);
			OSL_DELAY(10);
			MOD_RADIO_PLLREG_20693(pi, PLL_CFG2,    core, rfpll_rst_n,            1);
			MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL1, core, rfpll_vcocal_rst_n,     1);
		} else {
			// WARM Start
			MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL1, core, rfpll_vcocal_rst_n,     1);
			MOD_RADIO_PLLREG_20693(pi, PLL_CFG2,    core, rfpll_rst_n,            0);
			MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL1, core, rfpll_vcocal_enableCal, 0);
			OSL_DELAY(10);
			MOD_RADIO_PLLREG_20693(pi, PLL_CFG2,    core, rfpll_rst_n,            1);
			MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL1, core, rfpll_vcocal_enableCal, 1);
		}
	}

	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && RADIOMAJORREV(pi) == 3) {
		wlc_phy_radio2069x_vcocal_isdone(pi, 0, cache_calcode);
	}
}

void
wlc_phy_radio2069_vcocal(phy_info_t *pi)
{
	/* Use legacy mode */
	uint8 legacy_n = 0;

	ASSERT(RADIOID_IS(pi->pubpi->radioid, BCM2069_ID));

	/* VCO cal mode selection */
	MOD_RADIO_REG(pi, RFP, PLL_VCOCAL10, rfpll_vcocal_ovr_mode, legacy_n);

	/* VCO-Cal startup seq */
	ACPHY_REG_LIST_START
		MOD_RADIO_REG_ENTRY(pi, RFP, PLL_CFG2, rfpll_rst_n, 0)
		MOD_RADIO_REG_ENTRY(pi, RFP, PLL_VCOCAL13, rfpll_vcocal_rst_n, 0)
		MOD_RADIO_REG_ENTRY(pi, RFP, PLL_VCOCAL1, rfpll_vcocal_cal, 0)
	ACPHY_REG_LIST_EXECUTE(pi);
	OSL_DELAY(10);
	MOD_RADIO_REG(pi, RFP, PLL_CFG2, rfpll_rst_n, 1);
	MOD_RADIO_REG(pi, RFP, PLL_VCOCAL13, rfpll_vcocal_rst_n, 1);
	OSL_DELAY(1);
	MOD_RADIO_REG(pi, RFP, PLL_VCOCAL1, rfpll_vcocal_cal, 1);
}

#define MAX_2069x_VCOCAL_WAITLOOPS 100

/* vcocal should take < 120 us */
void
wlc_phy_radio2069x_vcocal_isdone(phy_info_t *pi, bool set_delay, bool cache_calcode)
{
	/* Use legacy mode */
	uint8 done, itr, core, start_core = 0, end_core = 0;
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;
	acphy_vcocal_radregs_t *porig = &(pi_ac->vcocali->ac_vcocal_radioregs_orig);
	uint8 pll_mode = pi_ac->vcocali->pll_mode;
	uint16 maincap = 0, secondcap = 0, auxcap = 0, base_addr;

	if (ISSIM_ENAB(pi->sh->sih))
		return;

	switch (pll_mode) {
	case 0:
			// intentional fall through
	case 1:
			start_core = 0;
		end_core   = 0;
		break;
	case 2:
			// intentional fall through
	case 3:
			// intentional fall through
	case 4:
			// intentional fall through
	case 5:
			start_core = 0;
		end_core   = 1;
		break;
	case 6:
			// intentional fall through
	case 7:
			start_core = 1;
		end_core   = 1;
		break;
	default:
		PHY_ERROR(("wl%d: %s: Unsupported PLL/VCO operating mode %d\n",
			pi->sh->unit, __FUNCTION__, pll_mode));
		ASSERT(0);
		return;
	}

	/* Wait for vco_cal to be done, max = 100us * 10 = 1ms	*/
	done = 0;
	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && RADIOMAJORREV(pi) == 3) {
		for (itr = 0; itr < MAX_2069x_VCOCAL_WAITLOOPS; itr++) {
			OSL_DELAY(10);
			if (TINY_RADIO(pi)) {
				done = READ_RADIO_PLLREGFLD_20693(pi, PLL_STATUS1, start_core,
					rfpll_vcocal_done_cal);
				if (end_core != start_core) {
					done &= READ_RADIO_PLLREGFLD_20693(pi, PLL_STATUS1,
						end_core, rfpll_vcocal_done_cal);
				}
			} else if (RADIOID(pi->pubpi->radioid) == BCM20696_ID) {
				done = READ_RADIO_PLLREGFLD_20696(pi, PLL_STATUS1,
					rfpll_vcocal_done_cal);
			} else
				done = READ_RADIO_REGFLD(pi, RFP, PLL_VCOCAL14,
					rfpll_vcocal_done_cal);

			if (ISSIM_ENAB(pi->sh->sih))
				done = 1;

			if (done == 1)
				break;
		}
	} else {
		wlapi_suspend_mac_and_wait(pi->sh->physhim);

		if (TINY_RADIO(pi)) {
			SPINWAIT((READ_RADIO_REGFLD_TINY(pi, PLL_VCOCAL14, 0,
				rfpll_vcocal_done_cal) == 0),
					ACPHY_SPINWAIT_VCO_CAL_STATUS);
			done = READ_RADIO_REGFLD_TINY(pi, PLL_VCOCAL14, 0, rfpll_vcocal_done_cal);
			if (done == 0) {
			   PHY_FATAL_ERROR_MESG((" %s: SPINWAIT ERROR :", __FUNCTION__));
			   PHY_FATAL_ERROR_MESG(("VCO CAL failed"));
			   PHY_FATAL_ERROR(pi, PHY_RC_VCOCAL_FAILED);
			}
			/* In 80P80 mode, vcocal should be complete on core 0 and core 1 */
			if (phy_get_phymode(pi) == PHYMODE_80P80) {
				SPINWAIT((READ_RADIO_REGFLD_TINY(pi,
					PLL_VCOCAL14, 1, rfpll_vcocal_done_cal) == 0),
						ACPHY_SPINWAIT_VCO_CAL_STATUS);
				done &= READ_RADIO_REGFLD_TINY(pi, PLL_VCOCAL14, 1,
					rfpll_vcocal_done_cal);
				if (done == 0) {
					PHY_FATAL_ERROR_MESG((" %s: SPINWAIT ERROR :",
						__FUNCTION__));
					PHY_FATAL_ERROR_MESG((
						"VCO CAL failed for core1 in 80P80 mode"));
					PHY_FATAL_ERROR(pi, PHY_RC_VCOCAL_FAILED);
				}
			}
		} else {
			SPINWAIT(READ_RADIO_REGFLD(pi, RFP, PLL_VCOCAL14,
				rfpll_vcocal_done_cal) == 0, ACPHY_SPINWAIT_VCO_CAL_STATUS);
			done = READ_RADIO_REGFLD(pi, RFP, PLL_VCOCAL14, rfpll_vcocal_done_cal);
			if (done == 0) {
			   PHY_FATAL_ERROR_MESG((" %s: SPINWAIT ERROR :", __FUNCTION__));
			   PHY_FATAL_ERROR_MESG(("VCO CAL failed"));
			   PHY_FATAL_ERROR(pi, PHY_RC_VCOCAL_FAILED);
			}
		}

		wlapi_enable_mac(pi->sh->physhim);
	}

	/* Need to wait extra time after vcocal done bit is high for it to settle */
	if (set_delay == TRUE)
		OSL_DELAY(120);

	ASSERT(done & 0x1);

	/* Restore the radio regs for core 0 */
	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && RADIOMAJORREV(pi) != 3) {
		ASSERT(porig->is_orig);
		porig->is_orig = FALSE;

		phy_utils_write_radioreg(pi, RADIO_REG_20693(pi, CLK_DIV_OVR1, 0),
			porig->clk_div_ovr1);
		phy_utils_write_radioreg(pi, RADIO_REG_20693(pi, CLK_DIV_CFG1, 0),
			porig->clk_div_cfg1);
	}

	if (RADIOID_IS(pi->pubpi->radioid, BCM20693_ID) && RADIOMAJORREV(pi) == 3) {
		/* Clear Slope In Override */
		MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL_OVR1, start_core,
			ovr_rfpll_vcocal_slopeIn, 0x0);
		if (end_core != start_core) {
			MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL_OVR1, end_core,
				ovr_rfpll_vcocal_slopeIn, 0x0);
		}

		/* store the vco calcode into shmem */
		if (cache_calcode) {
			for (core = start_core; core <= end_core; core++) {
				MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL7, core,
						rfpll_vcocal_CalCapRBMode, 0x2);
				maincap = READ_RADIO_PLLREGFLD_20693(pi, PLL_STATUS2, 0,
					rfpll_vcocal_calCapRB) | (1 << 12);
				MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL7, core,
						rfpll_vcocal_CalCapRBMode, 0x3);
				secondcap = READ_RADIO_PLLREGFLD_20693(pi, PLL_STATUS2, 0,
					rfpll_vcocal_calCapRB) | (1 << 12);
				MOD_RADIO_PLLREG_20693(pi, PLL_VCOCAL7, core,
						rfpll_vcocal_CalCapRBMode, 0x4);
				auxcap = READ_RADIO_PLLREGFLD_20693(pi, PLL_STATUS2, 0,
					rfpll_vcocal_calCapRB) | (1 << 12);
			}

			base_addr = wlapi_bmac_read_shm(pi->sh->physhim, M_USEQ_PWRUP_PTR(pi));
			wlapi_bmac_write_shm(pi->sh->physhim,
				2*(base_addr + VCOCAL_MAINCAP_OFFSET_20693), maincap);
			wlapi_bmac_write_shm(pi->sh->physhim,
				2*(base_addr + VCOCAL_SECONDCAP_OFFSET_20693), secondcap);
			wlapi_bmac_write_shm(pi->sh->physhim,
				2*(base_addr + VCOCAL_AUXCAP0_OFFSET_20693), auxcap);
			wlapi_bmac_write_shm(pi->sh->physhim,
				2*(base_addr + VCOCAL_AUXCAP1_OFFSET_20693), auxcap);
		}

		wlc_phy_radio_tiny_afe_resynch(pi, 2);

		/* Restore back */
		MOD_RADIO_PLLREG_20693(pi, WL_XTAL_CFG1, 0, wl_xtal_out_pu, 0x3);
	}

	PHY_INFORM(("wl%d: %s vcocal done\n", pi->sh->unit, __FUNCTION__));
}

void
wlc_phy_28nm_radio_vcocal(phy_info_t *pi, uint8 cal_mode, uint8 coupling_mode)
{
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;
	uint8 rfpll_vcocal_FastSwitch[] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0};
	uint8 rfpll_vcocal_slopeInOVR[] = {1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0};
	uint8 rfpll_vcocal_slopeIn[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	uint8 rfpll_vcocal_FourthMesEn[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0};
	uint8 rfpll_vcocal_FixMSB[] = {3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 0, 0, 0};
	radio_pll_sel_t pll_sel;

	/* PU cal clock */
	MOD_RADIO_REG_28NM(pi, RFP, XTAL6, 0, xtal_pu_caldrv, 0x1);
	MOD_RADIO_REG_28NM(pi, RFP, XTAL6, 0, xtal_outbufCalstrg, 0xF);

	/* PLL configured */
	pll_sel = pi_ac->pll_sel;

	if (pll_sel == PLL_2G) {
		/* 2G PLL */
		/* Fast Switch mode */
		MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_FastSwitch,
				rfpll_vcocal_FastSwitch[cal_mode]);
		/* Slope In OVR */
		MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL_OVR1, 0, ovr_rfpll_vcocal_slopeIn,
				rfpll_vcocal_slopeInOVR[cal_mode]);
		/* Slope In */
		MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL15, 0, rfpll_vcocal_slopeIn,
				rfpll_vcocal_slopeIn[cal_mode]);
		/* Additional Measurement for VCO CAL */
		MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_FourthMesEn,
				rfpll_vcocal_FourthMesEn[cal_mode]);
		/* Fixed MSB */
		MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_FixMSB,
				rfpll_vcocal_FixMSB[cal_mode]);

		if (READ_RADIO_REGFLD_28NM(pi, RFP, PLL_VCOCAL24, 0,
				rfpll_vcocal_enableCoupling) == 1) {
			/* 2 coupled VCO cal modes. Change only if coupled VCO is enbled */
			MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL24, 0, rfpll_vcocal_couplingMode,
					coupling_mode);
		}

		MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL_OVR1, 0, ovr_rfpll_vcocal_rst_n, 0x1);

		if (cal_mode <= 5) {
			/* Cold Start */
			MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_enableCal, 0x1);
			MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_rst_n, 0x0);
			MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_rst_n, 0x1);
		} else {
			/* WARM Start */
			MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_rst_n, 0x1);
			MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_enableCal, 0x1);
		}
	} else {
		/* 5G PLL */
		/* Fast Switch mode */
		MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL1, 0, rfpll_5g_vcocal_FastSwitch,
				rfpll_vcocal_FastSwitch[cal_mode]);
		/* Slope In OVR */
		MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL_OVR1, 0, ovr_rfpll_5g_vcocal_slopeIn,
				rfpll_vcocal_slopeInOVR[cal_mode]);
		/* Slope In */
		MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL15, 0, rfpll_5g_vcocal_slopeIn,
				rfpll_vcocal_slopeIn[cal_mode]);
		/* Additional Measurement for VCO CAL */
		MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL1, 0, rfpll_5g_vcocal_FourthMesEn,
				rfpll_vcocal_FourthMesEn[cal_mode]);
		/* Fixed MSB */
		MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL1, 0, rfpll_5g_vcocal_FixMSB,
				rfpll_vcocal_FixMSB[cal_mode]);

		if (READ_RADIO_REGFLD_28NM(pi, RFP, PLL5G_VCOCAL24, 0,
				rfpll_5g_vcocal_enableCoupling) == 1) {
			/* 2 coupled VCO cal modes. Change only if coupled VCO is enbled */
			MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL24, 0, rfpll_5g_vcocal_couplingMode,
					coupling_mode);
		}

		MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL_OVR1, 0, ovr_rfpll_5g_vcocal_rst_n, 0x1);

		if (cal_mode <= 5) {
			/* Cold Start */
			MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL1, 0,
					rfpll_5g_vcocal_enableCal, 0x1);
			MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL1, 0, rfpll_5g_vcocal_rst_n, 0x0);
			MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL1, 0, rfpll_5g_vcocal_rst_n, 0x1);
		} else {
			/* WARM Start */
			MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL1, 0, rfpll_5g_vcocal_rst_n, 0x1);
			MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL1, 0,
					rfpll_5g_vcocal_enableCal, 0x1);
		}
	}
}

void
wlc_phy_28nm_radio_vcocal_isdone(phy_info_t *pi, bool set_delay)
{
	uint8 done, itr;
	phy_info_acphy_t *pi_ac = (phy_info_acphy_t *)pi->u.pi_acphy;

	/* Wait for vco_cal to be done, max = 10us * 10 = 100us  */
	done = 0;
	for (itr = 0; itr < 10; itr++) {
		OSL_DELAY(10);

		done = (pi_ac->pll_sel == PLL_2G) ? READ_RADIO_REGFLD_28NM(pi, RFP,
				PLL_STATUS1, 0, rfpll_vcocal_done_cal) :
				READ_RADIO_REGFLD_28NM(pi, RFP, PLL5G_STATUS1, 0,
				rfpll_5g_vcocal_done_cal);
		if (done == 1) {
			break;
		}
	}

	/* Clear Slope In OVR */
	(pi_ac->pll_sel == PLL_2G) ? MOD_RADIO_REG_28NM(pi, RFP, PLL_VCOCAL_OVR1, 0,
			ovr_rfpll_vcocal_slopeIn, 0x0) :
			MOD_RADIO_REG_28NM(pi, RFP, PLL5G_VCOCAL_OVR1, 0,
			ovr_rfpll_5g_vcocal_slopeIn, 0x0);

	/* PD cal clock */
	MOD_RADIO_REG_28NM(pi, RFP, XTAL6, 0, xtal_pu_caldrv, 0x0);

	/* Need to wait extra time after vcocal done bit is high for it to settle */
	if (set_delay == TRUE) {
	  OSL_DELAY(120);
	}

	if (pi_ac->pll_sel == PLL_2G) {
		if (done == 1) {
			PHY_INFORM(("wl%d: %s 2G vcocal success, status = %d\n",
					pi->sh->unit, __FUNCTION__, READ_RADIO_REGFLD_28NM(pi,
					RFP, PLL_STATUS2, 0, rfpll_vcocal_calCapRB)));
		} else {
			PHY_INFORM(("wl%d: %s 2G vcocal failure\n",
					pi->sh->unit, __FUNCTION__));
		}
	} else {
		if (done == 1) {
			PHY_INFORM(("wl%d: %s 5G vcocal success, status = %d\n",
					pi->sh->unit, __FUNCTION__, READ_RADIO_REGFLD_28NM(pi,
					RFP, PLL5G_STATUS2, 0, rfpll_5g_vcocal_calCapRB)));
		} else {
			PHY_INFORM(("wl%d: %s 5G vcocal failure\n",
					pi->sh->unit, __FUNCTION__));
		}
	}
}

void
wlc_phy_20694_radio_vcocal(phy_info_t *pi, uint8 cal_mode, uint8 coupling_mode)
{
	uint8 rfpll_vcocal_FastSwitch[] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0};
	uint8 rfpll_vcocal_slopeInOVR[] = {1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0};
	uint8 rfpll_vcocal_slopeIn[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	uint8 rfpll_vcocal_FourthMesEn[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0};
	uint8 rfpll_vcocal_FixMSB[] = {3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 0, 0, 0};
	uint sicoreunit;
	sicoreunit = wlapi_si_coreunit(pi->sh->physhim);

	/* PU cal clock */
	MOD_RADIO_REG_20694(pi, RFP, XTAL6, 0, xtal_pu_caldrv, 0x1);
	MOD_RADIO_REG_20694(pi, RFP, XTAL6, 0, xtal_outbufCalstrg, 0x1);

	MOD_RADIO_REG_20694(pi, RFP, PLL_CFG6, 0, rfpll_vcocal_clk_pu, 0x1);
	MOD_RADIO_REG_20694(pi, RFP, PLL_OVR1, 0, ovr_rfpll_vcocal_clk_pu, 1);
	MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL2, 0, rfpll_vcocal_force_caps_ovr, 0x0);

	if (sicoreunit == DUALMAC_MAIN) {
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL21, 0, rfpll_vcocal_force_caps2_ovr, 0x0);
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL22, 0, rfpll_vcocal_force_aux1_ovr, 0x0);
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL23, 0, rfpll_vcocal_force_aux2_ovr, 0x0);
	}

	if (cal_mode <= 0) {
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_FastSwitch,
			rfpll_vcocal_FastSwitch[cal_mode]);
		/* Slope In OVR */
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL_OVR1, 0, ovr_rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeInOVR[cal_mode]);
		/* Slope In */
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL15, 0, rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeIn[cal_mode]);
		/* Additional Measurement for VCO CAL */
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_FourthMesEn,
			rfpll_vcocal_FourthMesEn[cal_mode]);
		/* Fixed MSB */
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_FixMSB,
			rfpll_vcocal_FixMSB[cal_mode]);

	} else  {
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL_OVR1, 0, ovr_rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeInOVR[1]);
		/* Slope In */
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL15, 0, rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeIn[0]);
	  }

	if (sicoreunit == DUALMAC_MAIN) {
		if (READ_RADIO_REGFLD_20694(pi, RFP, PLL_VCOCAL24, 0,
			rfpll_vcocal_enableCoupling) == 1) {
			/* 2 coupled VCO cal modes. Change only if coupled VCO is enbled */
			MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL24, 0, rfpll_vcocal_couplingMode,
					coupling_mode);
		}
	} else {
		// NO VCO2 or coupling option in AUX
	}

	if (cal_mode <= 5) {
		/* Cold Start */
		// Cold Start When rst_n is asserted to VCO Cal
		// Initialize slope & offset: if slope == 0
		//Normal operation: update slope & offset if error > thres
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_enableCal, 0x1);
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_rst_n, 0x0);
		OSL_DELAY(10);
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_rst_n, 0x1);

		MOD_RADIO_REG_20694(pi, RFP, PLL_CFG2, 0, rfpll_rst_n, 0x0);
		OSL_DELAY(10);
		MOD_RADIO_REG_20694(pi, RFP, PLL_CFG2, 0, rfpll_rst_n, 0x1);

		} else {
		/* WARM Start */
		// WARM Start When pll_val is changed without rst_n toggling
		// Initialize slope & offset: If slope == 0
		// Fast switch option!
		// Normal operation: update slope & offset if error > thres
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_enableCal, 0x0);
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_enableCal, 0x1);
		MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL1, 0, rfpll_vcocal_rst_n, 0x1);
		MOD_RADIO_REG_20694(pi, RFP, PLL_CFG2, 0, rfpll_rst_n, 0x0);
		MOD_RADIO_REG_20694(pi, RFP, PLL_CFG2, 0, rfpll_rst_n, 0x1);
	}
}
/* ************************************************************************** */


void
wlc_phy_20696_radio_vcocal(phy_info_t *pi, uint8 cal_mode, uint8 coupling_mode)
{
	uint8 rfpll_vcocal_FastSwitch[] = {3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 1, 0};
	uint8 rfpll_vcocal_slopeInOVR[] = {1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0};
	uint8 rfpll_vcocal_slopeIn[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	uint8 rfpll_vcocal_FourthMesEn[] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0};
	uint8 rfpll_vcocal_FixMSB[] = {3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 3, 0, 0, 0, 0};
	/* PU cal clock */
	if (cal_mode <= 0) {
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL1, rfpll_vcocal_FastSwitch,
			rfpll_vcocal_FastSwitch[cal_mode]);
		/* Slope In OVR */
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL_OVR1, ovr_rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeInOVR[cal_mode]);
		/* Slope In */
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL15, rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeIn[cal_mode]);
		/* Additional Measurement for VCO CAL */
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL1, rfpll_vcocal_FourthMesEn,
			rfpll_vcocal_FourthMesEn[cal_mode]);
		/* Fixed MSB */
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL1, rfpll_vcocal_FixMSB,
			rfpll_vcocal_FixMSB[cal_mode]);

	} else  {
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL_OVR1, ovr_rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeInOVR[1]);
		/* Slope In */
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL15, rfpll_vcocal_slopeIn,
			rfpll_vcocal_slopeIn[0]);
	  }

	if (cal_mode <= 5) {
		/* Cold Start */
		// Cold Start When rst_n is asserted to VCO Cal
		// Initialize slope & offset: if slope == 0
		//Normal operation: update slope & offset if error > thres
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL1, rfpll_vcocal_enableCal, 0x1);
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL1, rfpll_vcocal_rst_n, 0x0);
		OSL_DELAY(10);
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL1, rfpll_vcocal_rst_n, 0x1);

		MOD_RADIO_PLLREG_20696(pi, PLL_CFG2, rfpll_rst_n, 0x0);
		OSL_DELAY(10);
		MOD_RADIO_PLLREG_20696(pi, PLL_CFG2, rfpll_rst_n, 0x1);

		} else {
		/* WARM Start */
		// WARM Start When pll_val is changed without rst_n toggling
		// Initialize slope & offset: If slope == 0
		// Fast switch option!
		// Normal operation: update slope & offset if error > thres
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL1, rfpll_vcocal_enableCal, 0x0);
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL1, rfpll_vcocal_enableCal, 0x1);
		MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL1, rfpll_vcocal_rst_n, 0x1);
		MOD_RADIO_PLLREG_20696(pi, PLL_CFG2, rfpll_rst_n, 0x0);
		MOD_RADIO_PLLREG_20696(pi, PLL_CFG2, rfpll_rst_n, 0x1);
	}
}
/* ************************************************************************** */

void
wlc_phy_radio20694_vcocal_isdone(phy_info_t *pi, bool set_delay, bool cache_calcode)
{
	uint8 done, itr;
	uint16 maincap = 0, secondcap = 0, auxcap = 0;
	uint16 shmoff = (wlapi_bmac_read_shm(pi->sh->physhim, M_USEQ_PWRUP_PTR(pi)) << 1);

	/* Wait for vco_cal to be done, max = 10us * 10 = 100us  */
	/* timeout is 1ms in TCL */
	done = 0;
	for (itr = 0; itr < 10; itr++) {
		OSL_DELAY(10);
		done = READ_RADIO_REGFLD_20694(pi, RFP, PLL_STATUS1, 0, rfpll_vcocal_done_cal);
		if (done == 1) {
			break;
		}
	}
	/* Clear Slope In OVR */
	MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL_OVR1, 0, ovr_rfpll_vcocal_slopeIn, 0x0);
	/* PD cal clock */
	MOD_RADIO_REG_20694(pi, RFP, XTAL6, 0, xtal_pu_caldrv, 0x0);
	/* power off vcocal_clk */
	MOD_RADIO_REG_20694(pi, RFP, PLL_CFG6, 0, rfpll_vcocal_clk_pu, 0x0);
	MOD_RADIO_REG_20694(pi, RFP, PLL_OVR1, 0, ovr_rfpll_vcocal_clk_pu, 0x0);

	/* Need to wait extra time after vcocal done bit is high for it to settle */
	if (set_delay == TRUE) {
	  OSL_DELAY(120);
	}
	if (done == 1) {
		if (cache_calcode) {
			MOD_RADIO_REG_20694(pi, RFP, XTAL6, 0, xtal_pu_caldrv, 0x1);
			/* power off vcocal_clk */
			MOD_RADIO_REG_20694(pi, RFP, PLL_CFG6, 0, rfpll_vcocal_clk_pu, 0x1);
			MOD_RADIO_REG_20694(pi, RFP, PLL_OVR1, 0, ovr_rfpll_vcocal_clk_pu, 0x1);
			MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL7, 0,
			rfpll_vcocal_CalCapRBMode, 0x2);

			maincap = READ_RADIO_REGFLD_20694(pi, RFP, PLL_STATUS2, 0,
			rfpll_vcocal_calCapRB);
			MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL7, 0,
			rfpll_vcocal_CalCapRBMode, 0x3);
			secondcap = READ_RADIO_REGFLD_20694(pi, RFP, PLL_STATUS2, 0,
			rfpll_vcocal_calCapRB);
			MOD_RADIO_REG_20694(pi, RFP, PLL_VCOCAL7, 0,
			rfpll_vcocal_CalCapRBMode, 0x4);
			auxcap = READ_RADIO_REGFLD_20694(pi, RFP, PLL_STATUS2, 0,
			rfpll_vcocal_calCapRB);

			/* PD cal clock */
			MOD_RADIO_REG_20694(pi, RFP, XTAL6, 0, xtal_pu_caldrv, 0x0);
			/* power off vcocal_clk */
			MOD_RADIO_REG_20694(pi, RFP, PLL_CFG6, 0, rfpll_vcocal_clk_pu, 0x0);
			MOD_RADIO_REG_20694(pi, RFP, PLL_OVR1, 0, ovr_rfpll_vcocal_clk_pu, 0x0);

			wlapi_bmac_write_shm(pi->sh->physhim, (shmoff + 0x44), (0x1000 | maincap));
			wlapi_bmac_write_shm(pi->sh->physhim, (shmoff + 0x4c),
			(0x1000 | secondcap));
			wlapi_bmac_write_shm(pi->sh->physhim, (shmoff + 0x54), (0x1000 | auxcap));
			wlapi_bmac_write_shm(pi->sh->physhim, (shmoff + 0x5c), (0x1000 | auxcap));
		}
	} else {
		PHY_INFORM(("wl%d: %s  vcocal failure\n",
			pi->sh->unit, __FUNCTION__));
	}
}

void
wlc_phy_radio20696_vcocal_isdone(phy_info_t *pi, bool set_delay, bool cache_calcode)
{
	uint8 done, itr;

	/* Wait for vco_cal to be done, max = 10us * 10 = 100us */
	/* timeout is 1ms in TCL */
	done = 0;
	for (itr = 0; itr < 10; itr++) {
		OSL_DELAY(10);
		done = READ_RADIO_PLLREGFLD_20696(pi, PLL_STATUS1,
				rfpll_vcocal_done_cal);
		if (done == 1) {
			break;
		}
	}
	/* Clear Slope In OVR */
	MOD_RADIO_PLLREG_20696(pi, PLL_VCOCAL_OVR1, ovr_rfpll_vcocal_slopeIn, 0x0);

	/* Need to wait extra time after vcocal done bit is high for it to settle */
	if (set_delay == TRUE) {
		OSL_DELAY(120);
	}
	if (!cache_calcode) {
		PHY_ERROR(("wl%d: %s  vcocal failure (not done, timeout)\n",
				pi->sh->unit, __FUNCTION__));
	}
}


void
phy_ac_vcocal(phy_info_t *pi)
{
	const uint16 tbl_cookie = TXCAL_CACHE_VALID;
#if defined(PHYCAL_CACHING)
	ch_calcache_t *ctx = NULL;
	ctx = wlc_phy_get_chanctx(pi, pi->radio_chanspec);
#endif

#ifdef WFD_PHY_LL
	/* Single-core on 20MHz channel */
	wlc_phy_susp2tx_cts2self(pi, 600);
#else
	wlc_phy_susp2tx_cts2self(pi, 300);
#endif

	if ((pi->radar_percal_mask & 0x4) != 0) {
	    pi->u.pi_acphy->radar_cal_active = TRUE;
	}

	/* RSSI & VCO cal (prevents VCO/PLL from losing lock with temp delta) */
	wlc_phy_force_vcocal_acphy(pi->u.pi_acphy->vcocali);

	if (ACMAJORREV_36(pi->pubpi->phy_rev)) {
		wlc_phy_28nm_radio_vcocal_isdone(pi, TRUE);
	} else if (ACMAJORREV_37(pi->pubpi->phy_rev)) {
		wlc_phy_radio20696_vcocal_isdone(pi, TRUE, FALSE);
	} else if (ACMAJORREV_40(pi->pubpi->phy_rev)) {
		wlc_phy_radio20694_vcocal_isdone(pi, FALSE, TRUE);
	} else {
		wlc_phy_radio2069x_vcocal_isdone(pi, TRUE, FALSE);
	}
	/* turn off VCO Calibration clock */
	if (RADIOID_IS(pi->pubpi->radioid, BCM20691_ID)) {
		wlc_phy_radio2069x_vcocal_isdone(pi, TRUE, FALSE);
		MOD_RADIO_REG_20691(pi, PLL_XTAL2, 0, xtal_pu_caldrv, 0x0);
	}
	pi->cal_info->last_cal_time = pi->sh->now;
	pi->cal_info->u.accal.chanspec = pi->radio_chanspec;

	/* If this is the first calibration after association then we
	 * still have to do calibrate the idle-tssi, otherrwise done
	 */
	if (pi->first_cal_after_assoc) {
		pi->cal_info->cal_phase_id++;
	} else {
		wlc_phy_table_write_acphy(pi, wlc_phy_get_tbl_id_iqlocal(pi, 0), 1,
		                          IQTBL_CACHE_COOKIE_OFFSET, 16, &tbl_cookie);
#if defined(PHYCAL_CACHING)
		if (ctx)
			wlc_phy_cal_cache((wlc_phy_t *)pi);
#endif
		wlc_phy_cal_perical_mphase_reset(pi);
	}
}