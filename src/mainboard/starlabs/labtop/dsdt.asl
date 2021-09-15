/* SPDX-License-Identifier: GPL-2.0-only */

#if CONFIG(BOARD_STARLABS_STARBOOK_TGL)
#define EC_GPE_SCI 0x6e /* GPP_??? */
#else
#define EC_GPE_SCI 0x50 /* GPP_E16 */
#endif

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

	/* global NVS and variables */
#if CONFIG(BOARD_STARLABS_LABTOP_KBL)
	#include <soc/intel/skylake/acpi/globalnvs.asl>
#else
	#include <soc/intel/common/block/acpi/acpi/globalnvs.asl>
#endif
	/* CPU */
	#include <cpu/intel/common/acpi/cpu.asl>

	Device (\_SB.PCI0)
	{
#if CONFIG(BOARD_STARLABS_STARBOOK_TGL)
		#include <soc/intel/common/block/acpi/acpi/northbridge.asl>
		#include <soc/intel/tigerlake/acpi/southbridge.asl>
		#include <soc/intel/tigerlake/acpi/tcss.asl>
#elif CONFIG(BOARD_STARLABS_LABTOP_CML)
		#include <soc/intel/common/block/acpi/acpi/northbridge.asl>
		#include <soc/intel/cannonlake/acpi/southbridge.asl>
#else
		#include <soc/intel/skylake/acpi/systemagent.asl>
		#include <soc/intel/skylake/acpi/pch.asl>
#endif
	}

	#include <southbridge/intel/common/acpi/sleepstates.asl>

	/* Star Labs EC */
	#include <ec/starlabs/merlin/acpi/ec.asl>

	/* HID Driver */
	Scope (\_SB)
	{
		#include <ec/starlabs/merlin/acpi/hid.asl>
	}

	/* PS/2 Keyboard */
	Scope (\_SB.PCI0)
	{
		// Add the entries for the PS/2 keyboard and mouse.
		#include <drivers/pc80/pc/ps2_controller.asl>
	}

	#include "acpi/mainboard.asl"
}
