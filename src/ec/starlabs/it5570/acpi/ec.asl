/* SPDX-License-Identifier: GPL-2.0-only */

#define ASL_PVOL_DEFOF_NUM 0xe8

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
			// Store (0x03, \_SB.PCI0.GFX0.CLID)
			Return (0x0F)
		}

		Method (_REG, 2)
		{
			If ((Arg0 == 0x03) && (Arg1 == 0x01))
			{
				EREG()
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
			ECMV, 8,	// EC Firmware main - version number.
			ECSV, 8,	// EC Firmware sub - version number.
			KBVS, 8,	// EC Firmware test - version number.
			ECTV, 8,	//0x03		EC Firmware test version
			ECOS, 8,	//0X04		Flag for enter OS //ICL_010+


			Offset(0x18),
			KLBS, 8,	// Keyboard backlight begin.
			KLBE, 8,	// Keyboard backlight status.
			//
			// KLKT, 8,	// Keyboard backlight timeout (0x1A)
			//
			Offset(0x1A),
			KBLT, 8,	// Keyboard Backlight Timeout
			PWPF, 8,	// Power Profile

			Offset(0x1E),
			BTHP,8,		// Health Battery Percentage

			Offset(0x20),
			RCMD, 8,	// Same function as IO 66 port to send EC command
			RCST, 8,	// Report status for the result of command execution

			Offset(0x2C),
			FNST, 8,	// FN LOCK key status.

			Offset(0x3F),
			SFAN, 8,	// Set Fan Speed.
			BTMP, 16,	// Battery Temperature.
			BCNT, 16,	// Battery Cycle Count.
			FRMP, 16,	// Fan Current Speed.

			Offset(0x60),
			TSR1, 8,	// Thermal Sensor Register 1 [CPU VR (IMVP) Temp on RVP]
			TSR2, 8,	// Thermal Sensor Register 2 [Heat exchanger fan temp on RVP]
			TER4, 8,	// Thermal Sensor Register 3 (skin temperature)

			Offset(0x63),
			TSI,  4,	// [0..3]  0 = SEN1 - CPU VR temperature sensor
					// 1 = SEN2 - Heat Exchanger temperature sensor
					// 2 = SEN3 - Skin temperature sensor
					// 3 = SEN4 - Ambient temperature sensor
					// 4 = SEN5 - DIMM temperature sensor [IR sensor 1 on WSB]
					// 5 = SEN6 - not used on RVP
			HYST, 4,	// [4..7] - Hysteresis in degC.
			TSHT, 8,	// Thermal Sensor (N) high trip point(set default value =70)
			TSLT, 8,	// Thermal Sensor (N) low trip point (set default value =70)
			TSSR, 8,	// TSSR- thermal sensor status register (set bit2 =1)
					// BIT0: SEN1 - CPU VR Temp Sensor Trip Flag
					// BIT1: SEN2 - Fan Temp Sensor Trip Flag
					// BIT2: SEN3 - Skin Temp Sensor Trip Flag
					// BIT3: SEN4 - Ambient Temp Sensor Trip Flag
					// BIT4: Reserved
					// BIT5: Reserved
					// BIT6: Reserved
					// BIT7: Reserved
			CHGR, 16,	// Charge Rate

			Offset(0x70),
			CPTM, 8,	// CPU Temperature

			Offset(0x72),
			TER2, 8,	// Charger Temperature, Charger thermistor support

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

		// EREG method will be used in _REG (evaluated by OS without
		// ECDT support) or _INI (for OS with ECDT support)
		Method (EREG)
		{
			// Update ECAV Object. ASL should check for this value
			// to be 1 before accessing EC OpRegion.
			ECAV = 1

			// LIDS = ECRD (RefOf (LSTE))
			/* Initialize LID switch state */
			Store (LIDS, \LIDS)

			// Report OSFG to notify EC
			ECWT(0x01, RefOf (ECOS))

			// Save the current Power State for later.
			// PWRS = Local0

			// Update power state
			PWRS = (ECRD (RefOf (ECPS)) & 0x01)

			//\_SB.CPPC = 0x00 // Note: \_SB.CPPC must be an Integer not a Method

			//Perform needed ACPI Notifications.
			PNOT()
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
