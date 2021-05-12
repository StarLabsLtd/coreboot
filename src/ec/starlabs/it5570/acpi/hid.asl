Device(HIDD)	//HID ACPI Device.
{
	Name(_HID,"INTC1051")	// Intel Ultrabook HID Platform Event Driver.
	Name (HBSY, 0)   // HID Busy
	Name (HIDX, 0)   // HID Index
	Name (HMDE, 0)   // HID Mode
	Name (HRDY, 0)   // HID Ready
	Name (BTLD, 0)   // Button Driver Loaded
	Name (BTS1, 0)   // Button Status
	
	Method(_STA,0,Serialized)
	{
		If (OSYS > 2013) {
			Return(0x0F)
		} Else {
			Return(0)
		}
	}
	
	Method(HDDM,0,Serialized)
	{
		// Placeholder.
		Name(DPKG, Package(4) {0x11111111, 0x22222222, 0x33333333, 0x44444444})
		Return(DPKG)
	}
	
	Method(HDEM,0,Serialized)
	{
		HBSY = 0x00	// Clear HID Busy.
		If (LEqual(HMDE,0))
		{
			Return(HIDX)
		}
		Return(HMDE)
	}
	
	Method(HDMM,0,Serialized)
	{
		Return(HMDE)	// Return Mode of operation.
	}

	Method(HDSM,1,Serialized)
	{
		HRDY = Arg0	// Store HID Ready Status.
	}
	
	Method(HPEM,1,Serialized)	// HID Platform Event Method.
	{
		HBSY = 1
		If (LEqual(HMDE,0))
		{
			HIDX = Arg0
		} Else {
			HIDX = Arg0
		}
		Notify(\_SB.HIDD,0xC0)
		Local0 = 0
		While((Local0 < 250) && (HBSY))
		{
			Sleep(4)
			Increment(Local0)
		}
		If (HBSY <= 1)
		{
			HBSY = 0
			HIDX = 0
			Return(1)
		} Else {
			Return(0)
		}
	}
	
	Method(BTNL,0,Serialized)
	{
		BTS1 = 0
	}

	Method(BTNE,1,Serialized)
	{
		Return(BTS1)
	}

	Method(BTNS,0,Serialized)
	{
		Return(BTS1)
	}

	Method(BTNC,0,Serialized) // HID Button Capabilities Method
	{
		Return(0x1F)
	}

	Method (HEBC,0,Serialized) {
		Return (0x00063002)
	}
	
	Method (H2BC,0,Serialized) {
		Return (0x00063002)
	}
	
	Method (HEEC,0,Serialized) {
		Return(0)
	}
	
	Method (_DSM, 4, Serialized, 0, UnknownObj, {BuffObj, IntObj, IntObj, PkgObj})
	{
		If (LEqual(Arg0, ToUUID ("EEEC56B3-4442-408F-A792-4EDD4D758054")))
		{
			If (LEqual(1,ToInteger(Arg1)))
			{
				Switch (ToInteger(Arg2))
				{
					Case (0)
					{
						Return (Buffer() {0xFF, 0x03})
					}

					Case (1)
					{
						BTNL()
					}

					Case (2)
					{
						Return(HDMM())
					}

					Case (3)
					{
						HDSM(DeRefOf(Index(Arg3, 0)))
					}

					Case (4)
					{
						  Return(HDEM())
					}

					Case (5)
					{
						Return(BTNS())
					}

					Case (6)
					{
						BTNE(DeRefOf(Index(Arg3, 0)))
					}

					Case (7)
					{
						Return(HEBC())
					}

					Case (8)
					{
						Return(0x40)
					}

					Case (9)
					{
						Return(H2BC())
					}
				}
			}
		}
		Return (Buffer() {0x00})
	}
}

Method(PWPR, 0, Serialized) // Power Button Press Helper Method
{
	Notify(\_SB.HIDD,0xCE) // Notify HID driver that Power button is pressed.
}

Method(PWRR, 0, Serialized) // Power Button Release Helper Method
{
	Notify(\_SB.HIDD,0xCF) // Notify HID driver that Power button is released.
}
