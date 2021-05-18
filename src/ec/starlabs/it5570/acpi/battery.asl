/* SPDX-License-Identifier: GPL-2.0-only */

Device(BAT0)
{
	Name(_HID, EISAID("PNP0C0A"))
	Name(_UID, 1)
	Name (_PCL, Package () { \_SB })

	Method(_STA, 0)
	{
		If(And(ECRD(RefOf(ECPS)), 0x02))
		{
			Return(0x1F)
		}
		Return(0x0F)
	}

	Name(BPKG, Package() {
		1,		//  0: Power Unit
		0xFFFFFFFF,	//  1: Design Capacity
		0xFFFFFFFF,	//  2: Last Full Charge Capacity
		1,		//  3: Battery Technology(Rechargeable)
		0xFFFFFFFF,	//  4: Design Voltage 10.8V
		0,		//  5: Design capacity of warning
		0,		//  6: Design capacity of low
		0x00000100,	//  7: Battery capacity granularity 1
		0x00000040,	//  8: Battery capacity granularity 2
		"CN6613-2S3P",	//  9: Model Number
		"6UA3",		// 10: Serial Number
		"Real",		// 11: Battery Type
		"GDPT"		// 12: OEM Information
	})

	Name (B1CN, "Real")

	Method(_BIF, 0, Serialized)
	{
		BPKG[1] = ECRD (RefOf(B1DC))
		BPKG[2] = ECRD (RefOf(B1FC))
		BPKG[4] = ECRD (RefOf(B1DV))
      
		If(ECRD(RefOf(B1FC)))
		{
			BPKG[5] = ECRD (RefOf(B1FC)) / 10
			BPKG[6] = ECRD (RefOf(B1FC)) / 25
			BPKG[7] = ECRD (RefOf(B1DC)) / 100
		}
		Return(BPKG)
	}
    
	Name(PKG1, Package() {
		0xFFFFFFFF, // Battery State.
		0xFFFFFFFF, // Battery Present Rate. (in mWh)
		0xFFFFFFFF, // Battery Remaining Capacity. (in mWh)
		0xFFFFFFFF  // Battery Present Voltage. (in mV)
	})

	Method(_BST, 0, Serialized)
	{
		PKG1[0] = ECRD (RefOf(B1ST)) & 0x07
		PKG1[1] = ECRD (RefOf(B1PR))
		PKG1[2] = ECRD (RefOf(B1RC))
		PKG1[3] = ECRD (RefOf(B1PV))
		Return(PKG1)
	}
}
  
