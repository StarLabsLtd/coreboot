/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <acpi/acpi.h>

DefinitionBlock (
	"dsdt.aml",
	"DSDT",
	ACPI_DSDT_REV_2,
	OEM_ID,
	ACPI_TABLE_CREATOR,
	0x00010001	/* OEM Revision */
	)
{
	#include <acpi/dsdt_top.asl>
	/* Set policy before sleepstates.asl executes; a later SSDT is too late. */
	Name (OSFG, 0x08)
	If (CONFIG(DISABLE_ACPI_HIBERNATE)) {
		OSFG = Zero
	}
	#include <soc.asl>

	Name(LIDS, 0)
	Name(KBFG, 1)

	Scope (\_SB.PCI0)
	{
		#include "acpi/ps2_keyboard.asl"

		#include "acpi/pcie_power.asl"
		#include "acpi/usb4.asl"
	}

	#include <mainboard/starlabs/common/acpi/mainboard.asl>

	/* General Purpose Events */
	#include "acpi/gpe.asl"
}
