/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
DefinitionBlock(
	"dsdt.aml",
	"DSDT",
	0x02,		// DSDT revision: ACPI v2.0 and up
	OEM_ID,
	ACPI_TABLE_CREATOR,
	0x20110725	// OEM revision
)
{
        #include <acpi/dsdt_top.asl>
	#include <soc/intel/common/block/acpi/acpi/platform.asl>
//	#include <soc/intel/common/block/acpi/acpi/globalnvs.asl>
	#include <soc/intel/apollolake/acpi/globalnvs.asl>
	#include <cpu/intel/common/acpi/cpu.asl>

	Device (\_SB.PCI0)
	{
		#include <soc/intel/apollolake/acpi/northbridge.asl>
		#include <soc/intel/apollolake/acpi/southbridge.asl>
		#include <soc/intel/apollolake/acpi/pch_hda.asl>

		#include <drivers/intel/gma/acpi/default_brightness_levels.asl>
	}

	#include <southbridge/intel/common/acpi/sleepstates.asl>

	#include "acpi/mainboard.asl"
}
