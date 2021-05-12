/* SPDX-License-Identifier: GPL-2.0-only */

Device (ADP1)
{
	Name (_HID, "ACPI0003")

	Method (_STA, 0, NotSerialized)		// _STA: Status
	{
		Return (0x0F)
	}

	Method (_PSR, 0, NotSerialized)		// _PSR: Power Source
	{
		If(And(\_SB.PC00.LPCB.H_EC.ECRD(RefOf(\_SB.PC00.LPCB.H_EC.ECPS)), 0x01))
		{
			PWRS = 1
		}
		Else
		{
			PWRS = 0
		}
		Return (PWRS)
	}
	Method(_PCL,0)
	{
		Return (
			Package() { _SB }
		)
	}
}

Method(_Q0A, 0, NotSerialized)
{
	Notify(BAT0, 0x81)
    	Notify(ADP1, 0x80)
}
  
Method(_Q0B, 0, NotSerialized)
{
	Notify(BAT0, 0x81)
	Notify(BAT0, 0x80)
}
