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
		Name(_HID, EISAID("PNP0C09"))  
		Name(_UID,1)
		Name(ECAV, Zero)	// OS Bug Checks if EC OpRegion accessed before Embedded Controller Driver loaded
		Name(ECTK, One)		// ECDT (Embedded Controller Boot Resources Table) Check to correct ECAV flag in the beginning
		Name(LIDS, 0)

		Mutex(ECMT, 0)		// EC Mutex

		Name(BFFR, ResourceTemplate()
		{
			IO(Decode16, 0x62, 0x62, 0, 1)  // DIN/DOUT
			IO(Decode16, 0x66, 0x66, 0, 1)  // CMD/STS
		})

		Method(_CRS, 0, Serialized)
		{
			Return(BFFR)
		}

		Method (_STA, 0, NotSerialized)
		{
			// Store (0x03, \_SB.PCI0.GFX0.CLID)
			Return (0x0F)
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
			ECMV, 8,	//0x00		EC Firmware main version
			ECSV, 8,	//0x01		EC Firmware sub version
			KBVS, 8,	//0x02		Keyboard revision
			ECTV, 8,	//0x03		EC Firmware test version
			OSFG, 8,	//0X04		Flag for enter OS //ICL_010+
			Offset(0x2C),
			FNST, 8,
			Offset(0x7F),	//Support LID feature
			LSTE, 1,	//BIT0LID GPI
			, 7,		//Reserved
			Offset(0x80),	//Offset(0x80)
			ECPS, 8,	//0x80		AC & Battery status
			B1MN, 8,	//0x81		Battery#1 Model Number Code
			B1SN, 16,	//0x82~83	Battery#1 Serial Number
			B1DC, 16,	//0x84~85	Battery#1 Design Capacity
			B1DV, 16,	//0x86~87	Battery#1 Design Voltage
			B1FC, 16,	//0x88~89	Battery#1 Last Full Charge Capacity
			B1TP, 16,	//0x8A~8B	Battery#1 Trip Point
			B1ST, 8,	//0x8C		Battery#1 State
			B1PR, 16,	//0x8D~8E	Battery#1 Present Rate
			B1RC, 16,	//0x8F~90	Battery#1 Remaining Capacity
			B1PV, 16,	//0x91~92	Battery#1 Present Voltage
			B1RP, 8,	//0x93		Battery#1 Remaining percentage 
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

		Method(ECMD,1,Serialized)
		{
		}
  
		Method (ECNT,1,Serialized)
		{
		}
  
		// EREG method will be used in _REG (evaluated by OS without ECDT support) or _INI (for OS with ECDT support)
/*		Method(EREG)
		{
			// Update ECAV Object. ASL should check for this value to be One before accessing EC OpRegion.
			ECAV = 1
			
			If (LEqual(\_SB.PC00.LPCB.H_EC.ECRD(RefOf(\_SB.PC00.LPCB.H_EC.LSTE)), 0))
			{
				\_SB.PC00.GFX0.CLID = 0
			}
			If (LEqual(\_SB.PC00.LPCB.H_EC.ECRD(RefOf(\_SB.PC00.LPCB.H_EC.LSTE)), 1))
			{
				\_SB.PC00.GFX0.CLID = 3
			}
    
			Store(\_SB.PC00.LPCB.H_EC.ECRD(RefOf(\_SB.PC00.LPCB.H_EC.LSTE)), LIDS)

			//Report OSFG to notify EC
			\_SB.PC00.LPCB.H_EC.ECWT(One, RefOf(\_SB.PC00.LPCB.H_EC.OSFG))
    
			//Save the current Power State for later.
			PWRS = Local0
    
			//Update power state
			Store(And(\_SB.PC00.LPCB.H_EC.ECRD(RefOf(\_SB.PC00.LPCB.H_EC.ECPS)), 0x01), PWRS)
    
			\_SB.CPPC = 0x00 // Note: \_SB.CPPC must be an Integer not a Method
    
			//Perform needed ACPI Notifications.
			PNOT()
		}

		Method(_REG,2)
		{
			If (LAnd(LEqual(Arg0,3),LEqual(Arg1,1)))
			{
				EREG()
			}
		}
*/		
		Method(_GPE)
		{
			Local0 = 0x6E	// GPI6E for eSPI
			Return (Local0)
		}
		
		Method (PTS, 1, Serialized)
		{
			Debug = Concatenate("EC: PTS: ", ToHexString(Arg0))
			\_SB.PCI0.LPCB.H_EC.OSFG = 0
		}

		Method (WAK, 1, Serialized)
		{
			Debug = Concatenate("EC: WAK: ", ToHexString(Arg0))
			\_SB.PCI0.LPCB.H_EC.OSFG = 1
		}

		// Include the other parts of the Embedded Controller ASL.
		#include "keyboard.asl"
		#include "battery.asl"
		#include "ac.asl"
		#include "lid.asl"
	}
}
