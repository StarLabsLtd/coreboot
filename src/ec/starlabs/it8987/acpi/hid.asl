/* SPDX-License-Identifier: GPL-2.0-only */

Device (HIDD)					// HID APCI Device
{
	Name (_HID, "INT33D5")			// Intel Ultrabook HID Platform Event Driver.
	Name (HBSY, 0)				// HID Busy
	Name (HIDX, 0)				// HID Index
	Name (HMDE, 0)				// HID Mode
	Name (HRDY, 0)				// HID Ready
	Name (BTLD, 0)				// Button Driver Loaded
	Name (BTS1, 0)				// Button Status

	Method (_STA, 0, Serialized)		// Status Method
	{
		// Usually, ACPI will check if the OS is 0x07DD (2013 - Windows 8.1ish)
		// before showing the HID event filter. Seeing as we use Linux we show
		// it regardless.
		Return (0x0F)
	}

	Method (HDDM, 0, Serialized)
	{
		Store ("-----> HDDM", Debug)
		Name (DPKG, Package (0x04)
		{
			0x11111111,
			0x22222222,
			0x33333333,
			0x44444444
		})
		Return (DPKG)
	}

	Method (HDEM, 0, Serialized)
	{
		Store ("-----> HDEM", Debug)
		HBSY = 0
		If ((HMDE == 0))
		{
			Return (HIDX)
		}
		Return (HMDE)
	}

	Method (HDMM, 0, Serialized)
	{
		Store ("-----> HDMM", Debug)
		Return (HMDE)
	}

	Method (HDSM, 1, Serialized)
	{
		Store ("-----> HDSM", Debug)
		HRDY = Arg0
	}

	Method (HPEM, 1, Serialized)
	{
		Store ("-----> HPEM", Debug)
		HBSY = 1
		HIDX = Arg0

		Notify (^^HIDD, 0xC0)
		Local0 = 0
		While ((Local0 < 0xFA) && HBSY)
		{
			Sleep (0x04)
			Local0++
		}

		If (HBSY == 1)
		{
			HBSY = 0
			HIDX = 0
			Return (1)
		}
		Else
		{
			Return (0)
		}
	}

	Method (BTNL, 0, Serialized)
	{
		BTS1 = 0
	}

	Method (BTNE, 1, Serialized)
	{
		Store ("-----> BTNE", Debug)
		Return(BTS1)
	}

	Method (BTNS, 0, Serialized)
	{
		Store ("-----> BTNS", Debug)
		Return (BTS1)
	}

	Method (BTNC, 0, Serialized)
	{
		Store ("-----> BTNC", Debug)
		Return (0x1F)
	}

	Method (HEBC,0,Serialized) {
		Store ("-----> HEBC", Debug)
		Return (0)
	}

	Method (H2BC, 0, Serialized)
	{
		Store ("-----> H2BC", Debug)
		Return (0)
	}

	Method (HEEC, 0, Serialized)
	{
		Store ("-----> HEEC", Debug)
		Return (0)
	}
}

Method (PWPR, 0, Serialized)
{
	 Notify (HIDD, 0xCE)
}

Method (PWRR, 0, Serialized)
{
	Notify (HIDD, 0xCF)
}
