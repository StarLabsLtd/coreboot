/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/bsd/helpers.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci.h>
#include <intelblocks/systemagent.h>
#include <intelblocks/vtd.h>
#include <option.h>
#include <soc/pci_devs.h>
#include <soc/systemagent.h>

bool soc_is_igd_enabled(void)
{
	bool enabled = get_uint_option("igd_enabled", !CONFIG(SOC_INTEL_DISABLE_IGD)) &&
		is_devfn_enabled(SA_DEVFN_IGD);

	/* Prevent FSP-M from touching an IGD that is not physically present. */
	if (enabled && pci_read_config16(SA_DEV_IGD, PCI_VENDOR_ID) == 0xffff) {
		printk(BIOS_ERR, "igd_enabled is set, but IGD is not present. Disabling IGD.\n");
		return false;
	}

	return enabled;
}

bool soc_get_vtd_bases(const uintptr_t **bases, size_t *count)
{
	static uintptr_t active_bases[ARRAY_SIZE(soc_vtd_resources)];
	size_t active_count = 0;

	for (size_t i = 0; i < ARRAY_SIZE(soc_vtd_resources); i++) {
		const struct sa_mmio_descriptor *engine = &soc_vtd_resources[i];
		uint64_t bar = MCHBAR64(engine->index);
		bool enabled = bar & VTBAR_ENABLED;
		bool required;
		uintptr_t base = bar & VTBAR_MASK;

		switch (engine->index) {
		case GFXVTBAR:
			required = soc_is_igd_enabled();
			break;
		case IPUVTBAR:
			required = is_devfn_enabled(SA_DEVFN_IPU);
			break;
		case TBTxBAR(0):
			required = is_devfn_enabled(SA_DEVFN_TBT0);
			break;
		case TBTxBAR(1):
			required = is_devfn_enabled(SA_DEVFN_TBT1);
			break;
		case TBTxBAR(2):
			required = is_devfn_enabled(SA_DEVFN_TBT2);
			break;
		case TBTxBAR(3):
			required = is_devfn_enabled(SA_DEVFN_TBT3);
			break;
		case VTVC0BAR:
			required = true;
			break;
		default:
			printk(BIOS_ERR, "Unknown VT-d BAR %x\n", engine->index);
			return false;
		}

		if (required && !enabled) {
			printk(BIOS_ERR, "%s is required but disabled\n", engine->description);
			return false;
		}
		if (!enabled)
			continue;
		if (base != engine->base) {
			printk(BIOS_ERR, "%s has unexpected base %lx\n",
			       engine->description, (unsigned long)base);
			return false;
		}

		active_bases[active_count++] = base;
	}

	*bases = active_bases;
	*count = active_count;
	return active_count != 0;
}
