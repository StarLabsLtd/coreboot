/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <amdblocks/fsp.h>
#include <console/console.h>
#include <fsp/api.h>

void amd_fsp_early_init(void)
{
	const bool s3wake = acpi_is_wakeup_s3();

	printk(BIOS_DEBUG, "AMD FSP early init: entering, s3wake=%d\n", s3wake);
	fsp_memory_init(s3wake);
	printk(BIOS_DEBUG, "AMD FSP early init: returned from FspMemoryInit path\n");
}
