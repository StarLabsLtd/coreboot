/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/acpimmio.h>
#include <bootmem.h>
#include <cbmem.h>
#include <cpu/x86/smm.h>
#include <efi_variable_service.h>
#include <soc/iomap.h>
#include <soc/southbridge.h>
#include <vendorcode/amd/opensil/opensil.h>

#include "hob.h"

_Static_assert(CONFIG_EARLY_RESERVED_DRAM_BASE > 1 * MiB,
	       "ASS source RAM must precede the early firmware reservation");
static const struct region source_ram = {
	.offset = 1 * MiB, .size = CONFIG_EARLY_RESERVED_DRAM_BASE - 1 * MiB,
};

int efi_variable_service_get_platform_info(struct efi_variable_service_platform *info)
{
	uintptr_t smram_base;
	size_t smram_size;
	const uintptr_t end = CONFIG_EARLY_RESERVED_DRAM_BASE;
	const uint8_t decode = PM_ACPI_DECODE_STD | PM_ACPI_TIMER_EN_EN;

	smm_region(&smram_base, &smram_size);
	if (!smram_size || end > (uintptr_t)cbmem_top() || end > smram_base ||
	    pm_read16(PM_TMR_BLK) != ACPI_PM_TMR_BLK ||
	    (pm_read8(PM_ACPI_CONF) & decode) != decode)
		return -1;
	if (CONFIG(SOC_AMD_PHOENIX_OPENSIL) &&
	    !opensil_memory_region_is_ram(region_offset(&source_ram), region_sz(&source_ram)))
		return -1;
	*info = (struct efi_variable_service_platform) {
		.runtime_ram = source_ram, .pm_timer = ACPI_PM_TMR_BLK,
	};
	return 0;
}

bool efi_variable_service_ram_ready(void)
{
	/* Final resource enumeration can reveal additional platform reservations. */
	return bootmem_region_targets_type(region_offset(&source_ram), region_sz(&source_ram),
					   BM_MEM_RAM);
}
