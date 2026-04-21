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
	#include <soc.asl>

	Name(LIDS, 0)

	Scope (\_SB.PCI0)
	{
		/* PS/2 Keyboard */
		#include <drivers/pc80/pc/ps2_keyboard.asl>
	}

	/* Star Labs EC */
	#include <ec/starlabs/merlin/acpi/ec.asl>

	Scope (\_SB)
	{
		/* HID Driver */
		#include <ec/starlabs/merlin/acpi/hid.asl>

		/* Suspend Methods */
		#include <ec/starlabs/merlin/acpi/suspend.asl>
	}

}
