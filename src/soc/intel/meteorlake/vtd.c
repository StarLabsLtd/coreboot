/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/device.h>
#include <intelblocks/systemagent.h>
#include <intelblocks/vtd.h>
#include <option.h>
#include <soc/iomap.h>
#include <soc/pci_devs.h>
#include <soc/systemagent.h>

bool soc_is_igd_enabled(void)
{
	return get_uint_option("igd_enabled", !CONFIG(SOC_INTEL_DISABLE_IGD)) &&
		is_devfn_enabled(PCI_DEVFN_IGD);
}

bool soc_get_vtd_bases(const uintptr_t **bases, size_t *count)
{
	static uintptr_t active_bases[2];
	uint64_t gfx_bar = MCHBAR64(GFXVTBAR);
	bool gfx_enabled = gfx_bar & VTBAR_ENABLED;
	uintptr_t gfx_base = gfx_bar & VTBAR_MASK;
	size_t active_count = 0;

	if (soc_is_igd_enabled() && !gfx_enabled) {
		printk(BIOS_ERR, "GFXVTBAR is required but disabled\n");
		return false;
	}
	if (gfx_enabled) {
		if (gfx_base != GFXVT_BASE_ADDRESS) {
			printk(BIOS_ERR, "GFXVTBAR has unexpected base %lx\n",
			       (unsigned long)gfx_base);
			return false;
		}
		active_bases[active_count++] = gfx_base;
	}

	/* MTL's include-all engine is assigned directly through FSP-M. */
	active_bases[active_count++] = VTVC0_BASE_ADDRESS;

	*bases = active_bases;
	*count = active_count;
	return true;
}
