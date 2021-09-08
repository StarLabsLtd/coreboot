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
		// Name(PLMX, 0)
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
			// Store (0x03, \_SB.PCI0.GFX0.CLID)
			Return (0x0F)
		}

		Method (_REG, 2, NotSerialized)
		{
			If ((Arg0 == 0x03) && (Arg1 == 0x01))
			{
				// Flag that the DPTF supports ACPI.
				Store(1, ECAV)

				// Flag that the OS supports ACPI.
				Store(1, ECOS)

				/* Initialize LID switch state */
				Store(LIDS, \LIDS)

				// Initialise the AC Power State
				Store(And(ECRD(RefOf(ECPS)), 0x01), PWRS)

				// Inform platform code
				PNOT()
			}
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

		OperationRegion (SMIP,SystemIO, 0xB2, 0x1)
		Field (SMIP, ByteAcc, Lock, Preserve) {
			SMB2, 8
		}

		// EC RAM fields
		OperationRegion(ECF2, EmbeddedControl, 0, 0xFF)
		Field (ECF2, ByteAcc, Lock, Preserve)
		{
			Offset(0x00),
			ECMV, 8,	// Major Version Number
			ECSV, 8,	// Minor Version Number
			KBVS, 8,	// Keyboard Controller Version
			ECTV, 8,	// Test Version Number
			ECOS, 8,	// OS Flag
			FRMF, 8,	// Froce Mirror Flag
			SKUI, 8,	// SKU ID
			MDSB, 8,	// Modern Standby Flag

			Offset(0x09),
			KBLB, 8,	// Keyboard Backlight Brightness
			KBLE, 8,	// Keyboard Backlight State
			BDID, 8,	// Board ID
			TPEN, 8,	// Trackpad Enable
			ROTA, 8,	// Rotate Flag
			WIFI, 8,	// WiFi Enable
			FNST, 8,	// Function Lock Status

			Offset(0x13),
			AUDI, 8,	// Control Audio
			SURF, 8,	// Chassis Surface Temperature
			CHAR, 8,	// Charger Temperature
			FNCT, 8,	// Fn Ctrl Reverse

			Offset(0x1A),
			BFCP, 8,	// Battery Full Charge Percentage
			FANM, 8,	// Fan Mode
			FNLK, 8,	// Function Lock

			Offset(0x40),
			SHIP, 8,	// Shipping Mode Flag
			ECBT, 8,	// EC Build Time

			Offset(0x4B),
			ECBD, 8,	// EC Build Date

			Offset(0x62),
			SEN2, 8,	// Sensor 2 Temperature
			SENF, 8,	// Sensor F
			SENH, 8,	// Thermal Sensor High Trip Point
			SENL, 8,	// Thermal Sensor Low Trip Point
			THER, 8,	// Thermal Source

			Offset(0x68),
			BATT, 8,	// Battery Temperature
					// TODO:
					// Defined as XWORD, could be a 16?

			Offset(0x70),
			BATC, 8,	// Battery Temperature Ces
			CPUT, 8,	// PECI CPU Temperature
			PLMX, 8,	// PLMX Temperature
			SEN1, 8,	// Sensor 1 Temperature
			SEN3, 8,	// Sensor 3 Temperature // 73

			// Settings below are unconfirmed

			Offset(0x7F),
			LIDS, 1,	// BIT0 LID GPI
			    , 7,	// Reserved

			Offset(0x80),
			ECPS, 8,	// AC & Battery status
			XX10, 8,	// Battery#1 Model Number Code
			XX11, 16,	// Battery#1 Serial Number
			B1DC, 16,	// Battery#1 Design Capacity
			B1FV, 16,	// Battery#1 Design Voltage
			B1FC, 16,	// Battery#1 Last Full Charge Capacity
			XX15, 16,	// Battery#1 Trip Point
			B1ST, 8,	// Battery#1 State
			B1CR, 16,	// Battery#1 Present Rate
			B1RC, 16,	// Battery#1 Remaining Capacity
			B1VT, 16,	// Battery#1 Present Voltage
			BPCN, 8,	// Battery#1 Remaining percentage

			// USB Type C Mailbox Interface// PPM->OPM Message In
			Offset(0xc0),
			MGI0, 8,
			MGI1, 8,
			MGI2, 8,
			MGI3, 8,
			MGI4, 8,
			MGI5, 8,
			MGI6, 8,
			MGI7, 8,
			MGI8, 8,
			MGI9, 8,
			MGIA, 8,
			MGIB, 8,
			MGIC, 8,
			MGID, 8,
			MGIE, 8,
			MGIF, 8,

			// USB Type C Mailbox Interface// OPM->PPM Message Out
			MGO0, 8,
			MGO1, 8,
			MGO2, 8,
			MGO3, 8,
			MGO4, 8,
			MGO5, 8,
			MGO6, 8,
			MGO7, 8,
			MGO8, 8,
			MGO9, 8,
			MGOA, 8,
			MGOB, 8,
			MGOC, 8,
			MGOD, 8,
			MGOE, 8,
			MGOF, 8,

			// USB Type C UCSI DATA Structure.
			VER1, 8,
			VER2, 8,
			RSV1, 8,
			RSV2, 8,

			// PPM->OPM CCI indicator
			CCI0, 8,
			CCI1, 8,
			CCI2, 8,
			CCI3, 8,

			// OPM->PPM Control message
			CTL0, 8,
			CTL1, 8,
			CTL2, 8,
			CTL3, 8,
			CTL4, 8,
			CTL5, 8,
			CTL6, 8,
			CTL7, 8,

			Offset(0xF0),
			    , 3,	// BIT0 .. BIT2 Reserved
			TPCC, 1,	// BIT3 TypeC connection bit
			    , 2,	// BIT4 .. BIT5 Reserved
			DRMD, 1,	// Bit6 Dual Role Mode. 0->DFP: Host mode; 1->UFP: Device Mode.
			    , 1,	// BIT7 Reserved
		}

		Method (ECRD, 1, Serialized, 0, IntObj, FieldUnitObj)
		{
			// Check for ECDT support, set ECAV to One if ECDT is supported by OS
			// Only check once at beginning since ECAV might be clear later in certain conditions
			If (ECTK)
			{
				If (_REV >= 2)
				{
					ECAV = 0x01
				}
				ECTK = 0x00   // Clear flag for checking once only
			}

			Local0 = Acquire(ECMT, 1000) // save Acquire result so we can check for Mutex acquired
			If (Local0 == 0x00)
			{
				If (ECAV)
				{
					Store(DerefOf (Arg0), Local1) // Execute Read from EC
					Release(ECMT)
					Return(Local1)
				}
				Else
				{
					Release(ECMT)
				}
			}
			Return(0)
		}

		Method(ECWT,2,Serialized,,, {IntObj, FieldUnitObj})
		{
			If (ECTK)
			{
				If (_REV >= 2)
				{
					ECAV = 1
				}
				ECTK = 0x00
			}

			Local0 = Acquire(ECMT, 1000) // save Acquire result so we can check for Mutex acquired
			If (Local0 == 0x00)
			{
				If (ECAV)
				{
					Arg1 = Arg0 // Execute Write to EC
				}
				Release(ECMT)
			}
		}

		// Method(_GPE)
		// {
		//	Local0 = 0x6E	// GPI6E for eSPI
		//	Return (Local0)
		// }

		// Include the other parts of the Embedded Controller ASL.
		#include "keyboard.asl"
		#include "battery.asl"
		#include "ac.asl"
		#include "lid.asl"
	}
}
