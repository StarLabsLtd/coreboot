/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <arch/io.h>
#include <intelblocks/pmclib.h>
#include <intelblocks/wadt_wake.h>
#include <soc/iomap.h>
#include <soc/pm.h>

bool wadt_wake_should_preserve(uint8_t slp_typ)
{
	if (slp_typ == ACPI_S3 || slp_typ == ACPI_S4)
		return true;

	return CONFIG(SOC_INTEL_COMMON_ACPI_TIME_ALARM_S5) && slp_typ == ACPI_S5;
}

bool wadt_wake_is_enabled(void)
{
	return !!(inl(ACPI_BASE_ADDRESS + GPE0_EN(GPE_STD)) & WADT_EN);
}

uint32_t wadt_wake_status_preserve_mask(bool enabled)
{
	return enabled ? WADT_STS : 0;
}

void wadt_wake_restore(uint8_t slp_typ, bool enabled)
{
	/*
	 * Restore an OS-armed WADT at the final pre-SLP_EN point, after board
	 * callbacks have finished changing wake sources. If the OS had not
	 * armed WADT, leave any board-managed S3/S4 wake-source updates intact.
	 * For S5, restore the full sampled state so RTC-only S5 wake remains
	 * WADT-disabled after board policy restores Time and Alarm wake.
	 */
	if (enabled)
		pmc_enable_std_gpe(WADT_EN);
	else if (slp_typ == ACPI_S5)
		pmc_disable_std_gpe(WADT_EN);
}
