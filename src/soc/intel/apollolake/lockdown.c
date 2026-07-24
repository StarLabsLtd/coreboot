/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <device/mmio.h>
#include <intelblocks/cfg.h>
#include <intelblocks/fast_spi.h>
#include <intelblocks/pmclib.h>
#include <intelpch/lockdown.h>
#include <security/lockdown/lockdown.h>
#include <soc/pm.h>

static void pmc_lock_smi(void)
{
	uint8_t *pmcbase;

	pmcbase = pmc_mmio_regs();

	setbits32(pmcbase + GEN_PMCON2, SMI_LOCK);
}

void soc_lockdown_config(int chipset_lockdown)
{
	/* Only Gemini Lake exposes SMI_LOCK through the PMC MMIO path. */
	if (chipset_lockdown == CHIPSET_LOCKDOWN_COREBOOT &&
	    CONFIG(SOC_INTEL_GEMINILAKE))
		pmc_lock_smi();
}

static void fsp_bios_lock_config(void *unused)
{
	if (get_lockdown_config() != CHIPSET_LOCKDOWN_FSP)
		return;

	if (enable_smm_bios_protection())
		fast_spi_set_eiss();

	fast_spi_set_lock_enable();
}
BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_LOAD, BS_ON_EXIT, fsp_bios_lock_config, NULL);
