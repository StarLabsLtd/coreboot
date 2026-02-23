/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <console/console.h>
#include <device/device.h>
#include <security/tcg/opal_s3_resume.h>
#include <smm_call.h>

#if CONFIG(SOC_INTEL_COMMON_BLOCK_PCIE_RTD3)
#include <soc/intel/common/block/pcie/rtd3/chip.h>

extern struct chip_operations soc_intel_common_block_pcie_rtd3_ops;

static bool opal_s3_unlock_from_rtd3_on(void)
{
	for (DEVTREE_CONST struct device *dev = all_devices; dev; dev = dev->next) {
		if (dev->chip_ops != &soc_intel_common_block_pcie_rtd3_ops)
			continue;

		const struct soc_intel_common_block_pcie_rtd3_config *config = config_of(dev);
		if (config->is_storage)
			return true;
	}

	return false;
}
#else
static bool opal_s3_unlock_from_rtd3_on(void)
{
	return false;
}
#endif

void opal_s3_resume_unlock(void)
{
	/*
	 * If the OPAL NVMe sits behind an RTD3 storage root port, the device may
	 * still be powered off during early resume. In that case, let the ACPI
	 * _ON method trigger the unlock SMI once the device is powered.
	 */
	if (opal_s3_unlock_from_rtd3_on()) {
		if (CONFIG(DEBUG_SMI))
			printk(BIOS_DEBUG, "OPAL-S3: skipping early resume unlock (RTD3 storage)\n");
		return;
	}

	/* Best-effort: trigger OPAL unlock early on S3 resume. */
	u32 rc = call_smm(APM_CNT_OPAL_S3_UNLOCK, 0, NULL);
	if (CONFIG(DEBUG_SMI)) {
		/* Keep logs quiet unless explicitly debugging SMIs. */
		printk(BIOS_DEBUG, "OPAL-S3: resume unlock rc=0x%x\n", rc);
	}
}
