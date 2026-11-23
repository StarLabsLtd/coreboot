/* SPDX-License-Identifier: GPL-2.0-only */

#include <fsp/api.h>
#include <intelblocks/aspm.h>
#include <intelblocks/pcie_rp.h>
#include <option.h>
#include <soc/soc_chip.h>
#include <soc/pcie.h>

/*
 * The PCIe RP ASPM and PCIe L1 Substate UPDs follow the PCI Express Base
 * Specification 1.1. The UPDs and their default values are consistent
 * from Skylake through Meteor Lake. However, the default for CPU ports
 * differs from PCH ports. Use auto and maximum unless overwritten
 * to make the behaviour consistent.
 *
 * +-------------------+--------------------------+-----------+-----------+
 * | Setting           | Option                   | PCH Ports | CPU Ports |
 * |-------------------|--------------------------|-----------|-----------|
 * | PcieRpEnableCpm   | Disabled                 | [Default] | [Default] |
 * |                   | Enabled                  |           |           |
 * |-------------------|--------------------------|-----------|-----------|
 * | PcieRpAspm        | PchPcieAspmDisabled      |           |           |
 * |                   | PchPcieAspmL0s           |           |           |
 * |                   | PchPcieAspmL1            |           |           |
 * |                   | PchPcieAspmL0sL1         |           | [Default] |
 * |                   | PchPcieAspmAutoConfig    | [Default] |           |
 * |                   | PchPcieAspmMax           |           |           |
 * |-------------------|--------------------------|-----------|-----------|
 * | PcieRpL1Substates | Disabled                 |           |           |
 * |                   | PchPcieL1SubstatesL1_1   |           |           |
 * |                   | PchPcieL1SubstatesL1_1_2 |           | [Default] |
 * |                   | PchPcieL1SubstatesMax    | [Default] |           |
 * |-------------------|--------------------------|-----------|-----------|
 * | PchPcieRpPcieSpeed| PchPcieRpPcieSpeedAuto   | [Default] |           |
 * |                   | PchPcieRpPcieSpeedGen1   |           |           |
 * |                   | PchPcieRpPcieSpeedGen2   |           |           |
 * |                   | PchPcieRpPcieSpeedGen3   |           |           |
 * |                   | PchPcieRpPcieSpeedGen4   |           |           |
 * +-------------------+--------------------------+-----------+-----------+
 */

static unsigned int aspm_control_to_upd(enum ASPM_control aspm_control, bool is_cpu_rp)
{
	/* Disable without Kconfig selected */
	if (!CONFIG(PCIEXP_ASPM))
		return UPD_INDEX(ASPM_DISABLE);

	/* Use default unless overwritten */
	if (!aspm_control)
		return is_cpu_rp ? UPD_INDEX(ASPM_L0S_L1) : UPD_INDEX(ASPM_AUTO);

	return UPD_INDEX(aspm_control);
}

static unsigned int l1ss_control_to_upd(enum L1_substates_control l1_substates_control)
{
	/* Disable without Kconfig selected */
	if (!CONFIG(PCIEXP_ASPM))
		return UPD_INDEX(L1_SS_DISABLED);

	/* Don't enable UPD if Kconfig not set */
	if (!CONFIG(PCIEXP_L1_SUB_STATE))
		return UPD_INDEX(L1_SS_DISABLED);

	/* L1 Substate should be disabled in compliance mode */
	if (CONFIG(SOC_INTEL_COMPLIANCE_TEST_MODE))
		return UPD_INDEX(L1_SS_DISABLED);

	/* Use maximum unless overwritten */
	if (!l1_substates_control)
		return UPD_INDEX(L1_SS_L1_2);

	return UPD_INDEX(l1_substates_control);
}

void __weak mainboard_get_pcie_pm_options(const struct pcie_rp_config *rp_cfg,
					  unsigned int index,
					  bool is_cpu_rp,
					  struct pcie_pm_option_names *names)
{
	(void)rp_cfg;
	(void)index;

	names->clk_pm = "pciexp_clk_pm";
	names->l1ss = "pciexp_l1ss";
	names->aspm = is_cpu_rp ? "pciexp_aspm_cpu" : "pciexp_aspm";
	names->speed = "pciexp_speed";
}

static unsigned int get_pcie_pm_option(const char *opt_name, unsigned int fallback,
				       struct option_constraints rule)
{
	return opt_name ? get_uint_option_checked(opt_name, fallback, rule) : fallback;
}

void configure_pch_rp_power_management(FSP_S_CONFIG *s_cfg,
				       const struct pcie_rp_config *rp_cfg,
				       unsigned int index)
{
	struct pcie_pm_option_names options = {0};

	mainboard_get_pcie_pm_options(rp_cfg, index, false, &options);

	s_cfg->PcieRpEnableCpm[index] =
		get_pcie_pm_option(options.clk_pm, CONFIG(PCIEXP_CLK_PM), OPTION_BOOL);
	s_cfg->PcieRpAspm[index] =
		aspm_control_to_upd(get_pcie_pm_option(options.aspm, rp_cfg->pcie_rp_aspm,
			OPTION_RANGE(ASPM_DISABLE, ASPM_AUTO)),
			false);
	s_cfg->PcieRpL1Substates[index] =
		l1ss_control_to_upd(get_pcie_pm_option(options.l1ss, rp_cfg->PcieRpL1Substates,
			OPTION_RANGE(L1_SS_DISABLED, L1_SS_L1_2)));
	s_cfg->PcieRpPcieSpeed[index] =
		pcie_speed_control_to_upd(
			get_pcie_pm_option(options.speed, rp_cfg->pcie_rp_pcie_speed,
				OPTION_RANGE(SPEED_AUTO, SPEED_GEN4)));
}

#if CONFIG(HAS_INTEL_CPU_ROOT_PORTS)
/*
 * Starting with Alder Lake, UPDs for Clock Power Management were
 * introduced for the CPU root ports.
 *
 * CpuPcieClockGating:
 *	Disabled
 *	Enabled		[Default]
 *
 * CpuPciePowerGating
 *	Disabled
 *	Enabled		[Default]
 *
 */
void configure_cpu_rp_power_management(FSP_S_CONFIG *s_cfg,
				       const struct pcie_rp_config *rp_cfg,
				       unsigned int index)
{
	struct pcie_pm_option_names options = {0};

	mainboard_get_pcie_pm_options(rp_cfg, index, true, &options);
	bool pciexp_clk_pm = get_pcie_pm_option(options.clk_pm, CONFIG(PCIEXP_CLK_PM),
		OPTION_BOOL);
	s_cfg->CpuPcieRpEnableCpm[index] = pciexp_clk_pm;
	s_cfg->CpuPcieClockGating[index] = pciexp_clk_pm;
	s_cfg->CpuPciePowerGating[index] = pciexp_clk_pm;
	s_cfg->CpuPcieRpAspm[index] =
		aspm_control_to_upd(get_pcie_pm_option(options.aspm, rp_cfg->pcie_rp_aspm,
			OPTION_RANGE(ASPM_DISABLE, ASPM_L0S_L1)), true);
	s_cfg->CpuPcieRpL1Substates[index] =
		l1ss_control_to_upd(get_pcie_pm_option(options.l1ss, rp_cfg->PcieRpL1Substates,
			OPTION_RANGE(L1_SS_DISABLED, L1_SS_L1_2)));
}

#endif	// CONFIG(HAS_INTEL_CPU_ROOT_PORTS)
