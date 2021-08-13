/* SPDX-License-Identifier: GPL-2.0-only */

Scope (\_SB.PCI0)
{
	// Add the entries for the PS/2 keyboard and mouse.
	#include <drivers/pc80/pc/ps2_controller.asl>
}


Device (\_SB.PCI0.LPCB.H_EC)
{
	// Include the definitions for accessing CMOS.
	#include "cmos.asl"

		Name (_HID, EISAID("PNP0C09"))
		Name (ECTK, 0x01)			// ECDT (Embedded Controller Boot Resources Table) Check to correct ECAV flag in the beginning
		Name (ECFG, 0x00)
		Name (WIBT, 0x00)
		Name (_UID, 0x01)
		Name (APST, 0x00)
		
		Name (ECON, 0x01)			// AC debug
		Name (BNUM, 0)				// Number Of Batteries Present
		Name (PVOL, 0xE8)
		Name (B1CC, 0)
		Name (B2CC, 0)

		Name (B2ST, 0)
		Name (CFAN, 0)
		Name (CMDR, 0)
		Name (DOCK, 0)
		Name (EJET, 0)
		Name (MCAP, 0)
		Name (PLMX, 0)
		Name (PECH, 0)
		Name (PECL, 0)
		Name (PENV, 0)
		Name (PINV, 0)
		Name (PPSH, 0)
		Name (PPSL, 0)
		Name (PSTP, 0)
		Name (RPWR, 0)
		Name (LIDS, 0)
		Name (SLPC, 0)
		Name (VPWR, 0)
		Name (WTMS, 0)
		Name (AWT2, 0)
		Name (AWT1, 0)
		Name (AWT0, 0)
		Name (DLED, 0)
		Name (IBT1, 0)
		Name (ECAV, 1)				//Support DPTF feature
		Name (SPT2, 0)
		Name (PB10, 0)
		Name (IWCW, 0)
		Name (IWCR, 0)
		Name (BTEN, 0) 
		Mutex (ECMT, 0)
  
		Method(_CRS, 0, NotSerialized)
		{
			Name(BFFR, ResourceTemplate()
			{
				IO(Decode16, 0x0062, 0x0062, 0x00, 0x01)
				IO(Decode16, 0x0066, 0x0066, 0x00, 0x01)
			})
			Return(BFFR)
		}

		Method(_STA, 0, NotSerialized)
		{
			// \_SB.PCI0.GFX0.CLID = 0x03
			If(ECON == 0x01)
			{
				Return(0x0F)
			}
			Return(0x00)
		}

		OperationRegion (SIPR,SystemIO, 0xB2, 0x1)
		Field (SIPR, ByteAcc, Lock, Preserve) {
			SMB2, 8
		}
	   
		OperationRegion(ECF2, EmbeddedControl, 0x00, 0xFF)
		Field(ECF2, ByteAcc, Lock, Preserve)
		{			// Offset		Description
			XXX0, 8,	// 0x00			EC Firmware main version number.
			XXX1, 8,	// 0x01			EC Firmware sub version number.
			XXX2, 8,	// 0x02			EC Firmware test version number.
			Offset(0x06),	// Offset(0x06),	//SKU ID 
			SKID, 8,		   
			Offset(0x11),	// Offset(0x11),	//Control to enable/disable Key / Touch Pad
			KBCD, 8,	//0x11			KCB(Key / Touch Pad) disable/enable bit,  3 for disable and 0 for Enable
			ECOS, 8,	//0X12  Enter OS flag  
			HDAO, 8,	//0x13 
			ECHK, 8,	//0X14  hot keys flag	 
			Offset(0x18),	//Offset(0x11), //Control to enable/disable Key / Touch Pad
			KLBS, 8,	//0x18	 For record Keyboard backlight begin. //Eddie_debug
			KLBE, 8,	//0x19	 For record Keyboard back light status. //Eddie_debug

			Offset(0x1A),	//Offset(0x1A), 
			KBLT, 8,	//0x1A	 "Keyboard Backlight Timeout"
			PWPF, 8,	//0x1B	 "Power Profile"

			Offset(0x1E),				  
			BTHP,  8,	//0x1E	  Health Battery Percentage
			Offset(0x20),
			RCMD, 8,	//0x20	  Same function as IO 66 port to send EC command
			RCST, 8,	//0x21	  Report status for the result of command execution
			Offset(0x2C),				  
			FNST, 8,	//0x2C	  FN LOCK key status.	
			Offset(0x3F),				  
			SFAN, 8,	//0x3F	  Set Fan Speed.   
			BTMP, 16,	//0x40-0x41 Battery Temperature. 
			BCNT, 16,	//0x42-0x43 Battery Cycle Count. 
			FRMP, 16,	//0x44-0x45 Fan Current Speed.
			Offset(0x60),
			TSR1, 8,	//0x60	  Thermal Sensor Register 1 [CPU VR (IMVP) Temp on RVP]
			TSR2, 8,	//0x61	  Thermal Sensor Register 2 [Heat exchanger fan temp on RVP]
			TER4, 8,	//0x62	  Thermal Sensor Register 3 (skin temperature) //APL_MRD change name TSR3 to TER4 follow RC code
			Offset(0x63),	//0x63-66	DPTF fields
			TSI,  4,	// 0x63		0~3  (set value =2)
					//			0 = SEN1 - CPU VR temperature sensor
					//			1 = SEN2 - Heat Exchanger temperature sensor
					//			2 = SEN3 - Skin temperature sensor
					//			3 = SEN4 - Ambient temperature sensor
					//			4 = SEN5 - DIMM temperature sensor [IR sensor 1 on WSB]
					//			5 = SEN6 - not used on RVP
			HYST, 4,	// 0x63		4-7 Hysteresis in degC, hysteresis selection is global and meant for all sensors. (Set default value =2)
			TSHT, 8,	// 0x64		Thermal Sensor (N) high trip point(set default value =70)
			TSLT, 8,	// 0x65		Thermal Sensor (N) low trip point (set default value =70)
			TSSR, 8,	// 0x66		TSSR- thermal sensor status register (set bit2 =1)
					//		TSSR bits defined:
					//			BIT0:	SEN1 - CPU VR Temp Sensor Trip Flag
					//			BIT1:	SEN2 - Fan Temp Sensor Trip Flag
					//			BIT2:	SEN3 - Skin Temp Sensor Trip Flag
					//			BIT3:	SEN4 - Ambient Temp Sensor Trip Flag
					//			BIT4:	Reserved
					//			BIT5:	Reserved
					//			BIT6:	Reserved
					//			BIT7:	Reserved
			CHGR, 16,	// 0x67~68	Charge Rate // Support DPTF feature
			Offset(0x70),	// 0x70
			CPTM, 8,	// 0x70=CPU	Temperature 
			Offset(0x72),	// 0x72  
			TER2, 8,	// Charger Temperature, Charger thermistor support 
			Offset(0x7F),   // Offset(0x7F),Support LID feature
			LSTE, 1,	// BIT0		LID GPI
			    , 7,	// Reserved
			Offset(0x80),   // Offset(0x80),Support DPTF feature
			ECWR, 8,	// 0x80		AC & Battery status
			XX10, 8,	// 0x81		Battery#1 Model Number Code
			XX11, 16,	// 0x82~83	Battery#1 Serial Number
			B1DC, 16,	// 0x84~85	Battery#1 Design Capacity
			B1FV, 16,	// 0x86~87	Battery#1 Design Voltage
			B1FC, 16,	// 0x88~89	Battery#1 Last Full Charge Capacity
			XX15, 16,	// 0x8A~8B	Battery#1 Trip Point
			B1ST, 8,	// 0x8C		Battery#1 State
			B1CR, 16,	// 0x8D~8E	Battery#1 Present Rate  
			B1RC, 16,	// 0x8F~90	Battery#1 Remaining Capacity
			B1VT, 16,	// 0x91~92	Battery#1 Present Voltage
			BPCN, 8,	// 0x93		Battery#1 Remaining percentage

			// USB Type C Mailbox Interface // PPM->OPM Message In
			Offset(0xc0),
			MGI0, 8,	// 0xc0
			MGI1, 8,	// 0xc1
			MGI2, 8,	// 0xc2
			MGI3, 8,	// 0xc3
			MGI4, 8,	// 0xc4
			MGI5, 8,	// 0xc5
			MGI6, 8,	// 0xc6
			MGI7, 8,	// 0xc7
			MGI8, 8,	// 0xc8
			MGI9, 8,	// 0xc9
			MGIA, 8,	// 0xcA
			MGIB, 8,	// 0xcB
			MGIC, 8,	// 0xcC
			MGID, 8,	// 0xcD
			MGIE, 8,	// 0xcE
			MGIF, 8,	// 0xcF

			// USB Type C Mailbox Interface // OPM->PPM Message Out
			MGO0, 8,	// 0xD0	   
			MGO1, 8,	// 0xD1
			MGO2, 8,	// 0xD2
			MGO3, 8,	// 0xD3
			MGO4, 8,	// 0xD4
			MGO5, 8,	// 0xD5
			MGO6, 8,	// 0xD6
			MGO7, 8,	// 0xD7
			MGO8, 8,	// 0xD8
			MGO9, 8,	// 0xD9
			MGOA, 8,	// 0xDA
			MGOB, 8,	// 0xDB
			MGOC, 8,	// 0xDC
			MGOD, 8,	// 0xDD
			MGOE, 8,	// 0xDE
			MGOF, 8,	// 0xDF		   

			// USB Type C UCSI DATA Structure.
			VER1, 8,	// 0xE0
			VER2, 8,	// 0xE1
			RSV1, 8,	// 0xE2
			RSV2, 8,	// 0xE3

			// PPM->OPM CCI indicator
			CCI0, 8,	// 0xE4
			CCI1, 8,	// 0xE5
			CCI2, 8,	// 0xE6
			CCI3, 8,	// 0xE7

			// OPM->PPM Control message
			CTL0, 8,	// 0xE8
			CTL1, 8,	// 0xE9
			CTL2, 8,	// 0xEA
			CTL3, 8,	// 0xEB
			CTL4, 8,	// 0xEC
			CTL5, 8,	// 0xED
			CTL6, 8,	// 0xEE
			CTL7, 8,	// 0xEF
   
			Offset(0xF0),   // PD status reg 0xDD 
			    , 3,	// BIT0~BIT2		Reserved
			TPCC, 1,	// BIT3			TypeC connection bit
			    , 2,	// BIT4~BIT5		Reserved
			DRMD, 1,	// Bit6			Dual Roal Mode. 0->DFP: Host mode; 1->UFP: Device Mode.
			    , 1,	// BIT7			Reserved
		}
		//Support DPTF feature	 
		Method(ECMD,1,Serialized)
		{
			//SECC(Arg0)	
		}
		
		Method(ECWT,2,Serialized,,,{IntObj, FieldUnitObj})
		{
			Local0 = Acquire(ECMT, 1000)			// save Acquire result so we can check for Mutex acquired
			If (Local0 == 0x00)				// check for Mutex acquired
			{
				If (ECAV) {
					Arg1 = Arg0			// Execute Write to EC
				}
				Release(ECMT)
			}
		}
	
		Method(ECRD,1,Serialized, 0, IntObj, FieldUnitObj)
		{
			Local0 = Acquire(ECMT, 1000)			// save Acquire result so we can check for Mutex acquired
			If (Local0 == 0x00)				// check for Mutex acquired
			{
				If (ECAV) {
					Local1 = DerefOf (Arg0)		// Execute Read from EC
					Release(ECMT)
					Return(Local1)
				} Else {
					Release(ECMT)
				}
			}
		}

		Method(_GPE)
		{
			// TODO: Fix this
			// Local0 = GGPE(GPIO_CNL_LP_GPP_E16		// SMC_RUNTIME_SCI = Group E, Pad 1
			// Return (Local0)
		}

		Method(_Q80)						// Volume Up
		{
			// Notify(\_SB.HIDD, 0xC4)
			// Notify(\_SB.HIDD, 0xC5)
		}
	
		Method(_Q81)						// Volume Down
		{
			// Notify(\_SB.HIDD, 0xC6)
			// Notify(\_SB.HIDD, 0xC7)
		} 
	
		Method(_Q99)						// Wireless Mode
		{
			// \_SB.HIDD.HPEM (8)
		}

		Method(_Q06)						// Brightness Down
		{
			// \_SB.HIDD.HPEM (20)
		} 
	
		Method(_Q07)						// Brightness Up
		{
			// \_SB.HIDD.HPEM (19)
		} 
	
		Method(_Q08)						// FN Lock Toggle
		{
			\_SB.PCI0.LPCB.FNLC = \_SB.PCI0.LPCB.H_EC.FNST
		}

		Method(_Q40)	
		{
			SMB2 = 0xC6
		}
	
		Method(_Q41)	
		{
			SMB2 = 0xC7
		}
	
		Method(_Q42)	
		{
			SMB2 = 0xC8
		}
	
		Method(_Q43)	
		{
			SMB2 = 0xC9
		}

		Method(_Q44)	
		{
			SMB2 = 0xCA
		}
 
		Method(_Q45)	
		{
			SMB2 = 0xC1
		}

		Method(_Q54)	// Power Button Event for Control method Power Button(10sec PB Override without V-GPIO driver)
		{
			// P8XH(0,0x54)
			ADBG("PB Sleep 0x80")
			If (CondRefOf(\_SB.PWRB)){
				Notify(\_SB.PWRB, 0x80)
			}
		}
	
		Method(_QD5)			// 10 second power button press.
		{
			P8XH(0,0xD5)
			// \_SB.PWPR()
		}
	
		Method(_QD6)			// 10 second power button de-press.
		{
			P8XH(0,0xD6)
			// \_SB.PWRR()
		}
	
		Method(_QF0)			// Thermal Event.
		{
			// If (DBGS == 0x00)
			// {
			//	// Only handle the numerous Thermal Events if we are NOT doing ACPI Debugging.
			//	If (CondRefOf(\_TZ.TZ01)) {
			//		Notify(\_TZ.TZ01,0x80)
			//	}
			// }
		}

		// Section Start: Adapter	
		Device(ADP1)
		{
			Name(_HID,"ACPI0003")
			Method(_STA)
			{	   
				// 0x80, BIT0
				// Adapter status
				// 0 = Off
				// 1 = On
				If(ECON == 0x01) {
					Return(0x0F)
				}
				Return(0x00)				}
	
			// Return the value that determines if running
			// from AC or not.
	
			Method(_PSR,0)
			{
				If(ECWR & 0x01)
				{
					PWRS = 0x01
				} Else {
					PWRS = 0x00
				}
				Return(PWRS)
			}

			// Return that everything runs off of AC.
			Method(_PCL,0)
			{
				Return (
					Package() { _SB }
				)
			}
		}
		// Section End: Adapter

		// Section Start: Battery
		Device(BAT0)
		{
			Name(_HID, EISAID("PNP0C0A"))
			Name(_UID,1)
			Method(_STA, 0, NotSerialized)
			{
				If(ECWR & 0x02)
				{
					Return(0x1F)
				}
			Return(0x0F) 
			}
	
			Method(_BIF, 0, NotSerialized)
			{
	
				Name (BPKG, Package (13)
				{
						1,		//  0: Power Unit
						0xFFFFFFFF,	//  1: Design Capacity
						0xFFFFFFFF,	//  2: Last Full Charge Capacity
						1,		//  3: Battery Technology(Rechargeable)
						0xFFFFFFFF,	//  4: Design Voltage 10.8V
						0,		//  5: Design capacity of warning
						0,		//  6: Design capacity of low
						0x64,		//  7: Battery capacity granularity 1
						0,		//  8: Battery capacity granularity 2
						"CN6613-2S3P",	//  9: Model Number
						"6UA3",		// 10: Serial Number
						"Real",		// 11: Battery Type
						"GDPT"		// 12: OEM Information
					})
	
					BPKG[0x01] = B1DC
					BPKG[0x02] = B1FC
					BPKG[0x03] = B1FV
					If(B1FC)
					{
						BPKG[0x05] = B1FC / 0x0A
						BPKG[0x06] = B1FC / 0x19
						BPKG[0x07] = B1DC / 0x64
					}
					Return(BPKG)
				}
				Name (B1CN, "Real")
				Method(_BST, 0, NotSerialized)
				{
					Name(PKG1, Package(4)
					{
						0xFFFFFFFF,	// Battery State
						0xFFFFFFFF,	// Battery Present Rate
						0xFFFFFFFF,	// Battery Remaining Capacity
						0xFFFFFFFF,	// Battery Present Voltage
					})
					PKG1[0x00] = (B1ST & 0x07)
					If (B1ST & 0x01)
					{
						PKG1[0x01] = B1CR
					} Else {
						PKG1[0x01] = B1CR
					}
					PKG1[0x02] = B1RC
					PKG1[0x03] = B1VT
					Return(PKG1)
				}
				Method(_PCL, 0, NotSerialized)
				{
					Return(_SB)
				}
			}
			// Section End: Battery
	  
			Method(_REG, 2, NotSerialized)
			{
				If (LAnd(LEqual(Arg0,3),LEqual(Arg1,1)))
				{
					ECAV = 0x01
					
					// Unconditionally fix up the Battery and Power State.
		
					// Initialize the Number of Present Batteries.
					//  1 = Real Battery 1 is present
					//  2 = Real Battery 2 is present
					//  3 = Real Battery 1 and 2 are present
					BNUM = 0x00
					Or(BNUM,ShiftRight(And(ECRD(RefOf(ECWR)),0x02),1),BNUM)
					// Save the current Power State for later.
					Local0 = PWRS
		
					// Initialize the Power State.
					//  BNUM = 0 = Virtual Power State
					//  BNUM > 0 = Real Power State
					If (BNUM == 0x00))
					{
						PWRS = ECRD(RefOf(VPWR))
					} Else {
						PWRS = (ECRD(RefOf(ECWR) & 0x01))
					}
					PNOT()
				}
				\_SB.PCI0.LPCB.H_EC.ECOS = 0x01
			}
			   
			Method(_QA0, 0, NotSerialized)				// AC Power Connected
			{
				If(ECWR & 0x01)
				{
					PWRS = 0x01
				} Else {
					PWRS = 0x00
				}
				Notify(\_SB.PCI0.LPCB.BAT0, 0x81)
				Notify(ADP1, 0x80)
			}

			Method(_Q0B, 0, NotSerialized)				// Battery Connected.
			{
				Notify(\_SB.PCI0.LPCB.BAT0, 0x81)
				Notify(\_SB.PCI0.LPCB.BAT0, 0x80)
			}

			Method(_Q0C)						// Lid Close Event.
			{
			//	P8XH(0,0x0C)
				LIDS = 0x00
			//	\_SB.PCI0.GFX0.GLID(LIDS)
			//	Notify(\_SB.PCI0.LPCB.H_EC.LID0,0x80)
			}
		
			Method(_Q0D)						// Lid Open Event.
			{
			//	P8XH(0,0x0D)
				LIDS = 0x01
			//	\_SB.PCI0.GFX0.GLID(LIDS)
			//	Notify(\_SB.PCI0.LPCB.H_EC.LID0,0x80)
			}  
			// Section Start: LID  
			Device(LID0)
			{
				Name(_HID,EISAID("PNP0C0D"))
				Method(_STA)
				{
					Return(0x0F)
				}
				Method(_PRW, 0)
				{
					Return(GPRW(0x0E,3))
				}
				Method(_LID,0)
				{
					// 0 = Closed
					// 1 = Open
					// Return(\_SB.GGIV(GPIO_SKL_LP_GPP_C8))
					If (LEqual(\_SB.PCI0.LPCB.H_EC.ECRD(RefOf(\_SB.PCI0.LPCB.H_EC.LSTE)),1))
					{
						// Store (3, \_SB.PCI0.GFX0.CLID)
						// \_SB.PCI0.GFX0.CLID = 3
						Return(1)
					} Else {
						// Store(0, \_SB.PCI0.GFX0.CLID)
						// \SB.PCI.GFX0.CLID = 3
						Return(0)
					}
				}
			}
			// Section End: LID

			// ECNT (Embedded Controller Notify)
			//
			// Handle all commands sent to EC by BIOS
			//
			// Arguments: (1)
			//   1 = CS Entry Notify
			//   0 = CS Exit Notify
			//
			// Return Value:
			//   0x00 = Success
			//   0xFF = Failure
			//
			Method(ECNT,1,Serialized)
			{
				Return(0x00)
			}
		}
	}
