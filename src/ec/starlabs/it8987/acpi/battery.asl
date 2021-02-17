/* SPDX-License-Identifier: GPL-2.0-only */

//SCOPE EC0

Device (BAT0)
{
	Name (_HID, EISAID ("PNP0C0A"))
	Name (_UID, 1)
	Name (_PCL, Package () { \_SB })

	// Battery Slot Status
	Method (_STA, 0, Serialized)
	{
		If (And (ECWR, 0x02))
		{
			Return (0x1F)
		}
		Return(0x0F)
	}

	Method (_BIF, 0, Serialized)
	{
		// Default Static Battery Information
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
			"ASL_BATTERY_MODEL_NUMBER",
			"",
			"ASL_BATTERY_TYPE",
			"ASL_BATTERY_MANUFACTURE_NAME"
		})

		Store ("-----> BAT0: _BIF", Debug)
		Store (B1DC, Index (BPKG, 0x01))
		Store (B1FC, Index (BPKG, 0x02))
		Store (B1FV, Index (BPKG, 0x04))
		If (B1FC)
		{
			Store (Divide (B1FC, 10), Index (BPKG, 0x05))
			Store (Divide (B1FC, 25), Index (BPKG, 0x06))
			Store (Divide (B1DC, 100), Index (BPKG, 0x07))
		}

		Store ("<----- BAT0: _BIF", Debug)
		Return (BPKG)
	}

	Method (_BST, 0, Serialized)
	{
		Name (PKG1, Package (4)
		{
			0xFFFFFFFF,	// Battery State
			0xFFFFFFFF,	// Battery Present Rate
			0xFFFFFFFF,	// Battery Remaining Capacity
			0xFFFFFFFF,	// Battery Present Voltage
		})

		Store ("-----> BAT0: _BST", Debug)
		Store (And (B1ST, 0x07), Index (PKG1, 0x00))
		If (And (B1ST, 0x01))
		{
			Store (B1CR, Index (PKG1, 0x01))
		}
		Else
		{
			Store (B1CR, Index (PKG1, 0x01))
		}
		Store (B1RC, Index (PKG1, 0x02))
		Store (B1VT, Index (PKG1, 0x03))
		Store ("<----- BAT0: _BST", Debug)
		Return (PKG1)
	}
}

Device (BAT1)
{
	Name (_HID, EISAID ("PNP0C0A"))
	Name (_UID, 1)
	Method (_STA, 0, NotSerialized)
	{
		Return (0x00)
	}
}

Device (BAT2)
{
	Name (_HID, EISAID ("PNP0C0A"))
	Name (_UID, 2)
	Method (_STA, 0, NotSerialized)
	{
		Return (0x00)
	}
}
