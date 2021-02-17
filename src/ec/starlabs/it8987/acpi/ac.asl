/* SPDX-License-Identifier: GPL-2.0-only */

//SCOPE EC0

Device (ADP1)
{
	Name (_HID, "ACPI0003")
	Name (_PCL, Package () { \_SB })

	Method (_STA, 0, NotSerialized)  // _STA: Status
	{
		Store ("-----> AC: _STA", Debug)
		If (LEqual (ECON, 1))
		{
			Store(0x0F, Local0)
		}
		Else
		{
			Store(0x00, Local0)
		}
		Store ("<----- AC: _STA", Debug)
		Return (Local0)
	}

	Method (_PSR, 0, NotSerialized)  // _PSR: Power Source
	{
		Store ("-----> AC: _PSR", Debug)
		If (And (ECWR, 0x01))
		{
			Store (0x01, \PWRS)
		}
		Else
		{
			Store (0x00, \PWRS)
		}
		Store ("<----- AC: _PSR", Debug)
		Return (\PWRS)
	}
}

Method (_QA0, 0, NotSerialized)		// AC Power Connected
{
	Store ("-----> _QA0", Debug)
	If (And (ECWR, 0x01))
	{
		Store (0x01, \PWRS)
	}
	Else
	{
		Store (0x00, \PWRS)
	}

	// Fix issue "AC/DC switch slowly after S3 or Reboot"
	// Add 500ms delay before notify battery in QEvent
	Sleep (500)
	Notify (BAT0, 0x81)
	Sleep (500)
	Notify (ADP1, 0x80)
	Store ("<----- _QA0", Debug)
}

Method(_Q0B, 0, NotSerialized)
{
	Store ("-----> _Q0B", Debug)
	// Add 500ms delay before notify battery in QEvent.
	Sleep(500)
	Notify(BAT0, 0x81)
	Sleep(500)
	Notify(BAT0, 0x80)
	Store ("<----- _Q0B", Debug)
}
