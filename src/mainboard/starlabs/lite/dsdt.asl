/* SPDX-License-Identifier: GPL-2.0-only */

#define EC_GPE_SCI 0x50

#include <acpi/acpi.h>
DefinitionBlock(
	"dsdt.aml",
	"DSDT",
	ACPI_DSDT_REV_2,
	OEM_ID,
	ACPI_TABLE_CREATOR,
	0x20110725	// OEM revision
)
{
	#include <acpi/dsdt_top.asl>
	#include <soc/intel/common/block/acpi/acpi/platform.asl>
	#include <soc/intel/apollolake/acpi/globalnvs.asl>
	#include <cpu/intel/common/acpi/cpu.asl>

	Device (\_SB.PCI0)
	{
		#include <soc/intel/apollolake/acpi/northbridge.asl>
		#include <soc/intel/apollolake/acpi/southbridge.asl>
		#include <soc/intel/apollolake/acpi/pch_hda.asl>
	}

	#include <southbridge/intel/common/acpi/sleepstates.asl>

	/* Star Labs EC */
	#include <ec/starlabs/merlin/acpi/ec.asl>

	Scope (\_SB)
	{
		/* HID Driver */
		#include <ec/starlabs/merlin/acpi/hid.asl>

		/* Suspend Methods */
		#include <ec/starlabs/merlin/acpi/suspend.asl>
	}

	/* PS/2 Keyboard */
	Scope (\_SB.PCI0)
	{
		// Add the entries for the PS/2 keyboard and mouse.
		#include <drivers/pc80/pc/ps2_controller.asl>
	}

	#include "acpi/mainboard.asl"
}
