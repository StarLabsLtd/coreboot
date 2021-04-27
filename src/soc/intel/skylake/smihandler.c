/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>
#include <intelblocks/cse.h>
#include <intelblocks/smihandler.h>
#include <soc/soc_chip.h>
#include <soc/pci_devs.h>
#include <soc/pm.h>
#include <option.h>
#include <types.h>


void smihandler_soc_at_finalize(void)
{
	if (!CONFIG(HECI_DISABLE_USING_SMM))
		return;

	const struct device *dev = pcidev_path_on_root(PCH_DEVFN_CSE);

	if (!is_dev_enabled(dev) && get_int_option("me_state", 0))
		heci_disable();

}


const smi_handler_t southbridge_smi[SMI_STS_BITS] = {
	[SMI_ON_SLP_EN_STS_BIT] = smihandler_southbridge_sleep,
	[APM_STS_BIT] = smihandler_southbridge_apmc,
	[PM1_STS_BIT] = smihandler_southbridge_pm1,
	[GPE0_STS_BIT] = smihandler_southbridge_gpe0,
	[GPIO_STS_BIT] = smihandler_southbridge_gpi,
	[ESPI_SMI_STS_BIT] = smihandler_southbridge_espi,
	[MCSMI_STS_BIT] = smihandler_southbridge_mc,
#if CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_TCO_ENABLE)
	[TCO_STS_BIT] = smihandler_southbridge_tco,
#endif
	[PERIODIC_STS_BIT] = smihandler_southbridge_periodic,
	[MONITOR_STS_BIT] = smihandler_southbridge_monitor,
};
