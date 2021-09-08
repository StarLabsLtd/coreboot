/* SPDX-License-Identifier: GPL-2.0-only */

Scope (\_SB)
{
	#include "hid.asl"
}

Scope (\_SB.PCI0)
{
	// Add the entries for the PS/2 keyboard and mouse.
	#include <drivers/pc80/pc/ps2_controller.asl>
}

Scope (\_SB.PCI0.LPCB)
{
	// Include the definitions for accessing CMOS.
	#include "cmos.asl"

	// Our embedded controller device.
	Device (H_EC)
	{
		Name (_HID, EisaId ("PNP0C09"))		// ACPI Embedded Controller
		Name (_UID, 1)
		Name (_GPE, EC_GPE_SCI)

		// ECDT (Embedded Controller Boot Resources Table) Check to correct
		// ECAV flag in the beginning
		Name(ECTK, 1)
		Name(ECFG, 0)
		Name(WIBT, 0)
		Name(APST, 0)

		Name(ECON, 1)				// AC debug
		Name(BNUM, 0)				// Number Of Batteries Present
		Name(PVOL, 0xe8)
		Name(B1CC, 0)
		Name(B2CC, 0)

		Name(B2ST, 0)
		Name(CFAN, 0)
		Name(CMDR, 0)
		Name(DOCK, 0)
		Name(EJET, 0)
		Name(MCAP, 0)
		Name(PLMX, 0)
		Name(PECH, 0)
		Name(PECL, 0)
		Name(PENV, 0)
		Name(PINV, 0)
		Name(PPSH, 0)
		Name(PPSL, 0)
		Name(PSTP, 0)
		Name(RPWR, 0)
		Name(SLPC, 0)
		Name(VPWR, 0)
		Name(WTMS, 0)
		Name(AWT2, 0)
		Name(AWT1, 0)
		Name(AWT0, 0)
		Name(DLED, 0)
		Name(IBT1, 0)
		Name(ECAV, 1)		// Support DPTF feature
		Name(SPT2, 0)
		Name(PB10, 0)
		Name(IWCW, 0)
		Name(IWCR, 0)
		Name(BTEN, 0)

		Mutex(ECMT, 0)

		Name (BFFR, ResourceTemplate()
		{
			IO (Decode16, 0x62, 0x62, 0x00, 0x01)
			IO (Decode16, 0x66, 0x66, 0x00, 0x01)
		})

		Method (_CRS, 0, Serialized)
		{
			Return (BFFR)
		}

		Method (_STA, 0, NotSerialized)
		{
			If ((ECON == 1))
			{
				Return (0x0F)
			}

			Return (0x00)
		}

		Method (_REG, 2, NotSerialized)
		{
			// Initialise the AC Power State
			Store ((ECRD (RefOf (ECPS)) & 0x01), \PWRS)

			// Inform platform code
			\PNOT ()

			/* Initialize LID switch state */
			Store (LIDS, \LIDS)

			// Flag that the OS supports ACPI.
			\_SB.PCI0.LPCB.H_EC.ECOS = 1
		}

		Method (PTS, 1, Serialized)
		{
			Debug = Concatenate("EC: PTS: ", ToHexString(Arg0))
			\_SB.PCI0.LPCB.H_EC.ECOS = 0
		}

		Method (WAK, 1, Serialized)
		{
			Debug = Concatenate("EC: WAK: ", ToHexString(Arg0))
			\_SB.PCI0.LPCB.H_EC.ECOS = 1
		}

		OperationRegion (SIPR, SystemIO, 0xB2, 0x1)
		Field (SIPR, ByteAcc, Lock, Preserve)
		{
			SMB2, 8
		}

		// EC RAM fields
		#include "ec_ram.asl"

		Method (ECWT, 2, Serialized,,, {IntObj, FieldUnitObj})
		{
			Local0 = Acquire (ECMT, 1000)
			If (Local0 == 0x00)
			{
				If (ECAV)
				{
					// Execute write to Embedded Controller
					Arg1 = Arg0
				}
				Release (ECMT)
			}
		}

		Method (ECRD, 1, Serialized, 0, IntObj, FieldUnitObj)
		{
			Local0 = Acquire (ECMT, 1000)
			If (Local0 == 0)
			{
				If (ECAV)
				{
					// Execute read from Embedded Controller
					Local1 = DerefOf (Arg0)
					Release (ECMT)
					Return (Local1)
				}
				Else
				{
					Release (ECMT)
				}
			}
			Return (Local1)
		}

		// Include the other parts of the Embedded Controller ASL.
		#include "keyboard.asl"
		#include "battery.asl"
		#include "ac.asl"
		#include "lid.asl"

#if CONFIG(BOARD_STARLABS_LABTOP_CML)
		Method(_Q45) // SMM Mode
		{
			SMB2 = 0xC1
		}
#endif
	}
}
