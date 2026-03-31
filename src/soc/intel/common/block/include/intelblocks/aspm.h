/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_ASPM_H
#define SOC_INTEL_COMMON_BLOCK_ASPM_H

#include <fsp/api.h>
#include <intelblocks/pcie_rp.h>

struct pcie_pm_option_names {
	const char *clk_pm;
	const char *aspm;
	const char *l1ss;
};

void mainboard_get_pcie_pm_options(const struct pcie_rp_config *rp_cfg,
				   unsigned int index,
				   bool is_cpu_rp,
				   struct pcie_pm_option_names *names);

void configure_pch_rp_power_management(FSP_S_CONFIG *s_cfg,
				       const struct pcie_rp_config *rp_cfg,
				       unsigned int index);

#if CONFIG(HAS_INTEL_CPU_ROOT_PORTS)
void configure_cpu_rp_power_management(FSP_S_CONFIG *s_cfg,
				       const struct pcie_rp_config *rp_cfg,
				       unsigned int index);
#endif

#endif	/* SOC_INTEL_COMMON_BLOCK_ASPM_H */
