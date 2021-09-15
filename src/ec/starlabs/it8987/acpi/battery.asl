/* SPDX-License-Identifier: GPL-2.0-only */

Device (BAT0)
{
	Name (_HID, EisaId("PNP0C0A"))
	Name (_UID, 0)
	Method (_STA, 0, NotSerialized)
	{
		// Battery Status
		// 0x80 BIT1 0x01 = Present
		// 0x80 BIT1 0x00 = Not Present
		If(ECPS && 0x02)
		{
			Return(0x1F)
		}
		Return(0x0F)
	}
	Name (BPKG, Package(13)
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
		"597077-3S",	//  9: Model Number
		"3ICP6/70/77",	// 10: Serial Number
		"Real",		// 11: Battery Type
		"DGFGE"		// 12: OEM Information
	})
	Method (_BIF, 0, Serialized)
	{
		BPKG[0x01] = B1DC
		BPKG[0x02] = B1FC
		BPKG[0x04] = B1DV
		If(B1FC)
		{
			BPKG[0x05] = B1FC / 0x0a
			BPKG[0x06] = B1FC / 0x64
			BPKG[0x07] = B1DC / 0x64
		}
		Return(BPKG)
	}
	Name (PKG1, Package (4)
	{
		0xFFFFFFFF,     // Battery State
		0xFFFFFFFF,     // Battery Present Rate
		0xFFFFFFFF,     // Battery Remaining Capacity
		0xFFFFFFFF,     // Battery Present Voltage
	})
	Method (_BST, 0, NotSerialized)
	{
		PKG1[0x00] = (B1ST & 0x07)
		PKG1[0x01] = B1PR
		PKG1[0x02] = B1RC
		PKG1[0x03] = B1PV
		Return(PKG1)
	}
	Method (_PCL, 0, NotSerialized)
	{
		Return (
			Package() { _SB }
		)
	}
}
