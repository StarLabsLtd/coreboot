/* SPDX-License-Identifier: GPL-2.0-only */

Device (LID0)
{
	Name (_HID, EisaId ("PNP0C0D"))

	Method (_STA, 0, NotSerialized)
	{
		DEBUG = "---> ITE LID: _STA"
		Return (0x0F)
	}


	Method (_LID, 0, NotSerialized)
	{
		DEBUG = "---> ITE LID: _LID"
		If (\_SB.PCI0.LPCB.H_EC.ECRD (RefOf (\_SB.PCI0.LPCB.H_EC.LSTE)) ==  0x01)
		{
			Local0 = 1
		}
		else
		{
			Local0 = 0
		}
		Return (Local0)
	}
}

Method (_Q0C, 0, NotSerialized)		// Lid close event
{
	DEBUG = "---> ITE LID: Q0C (close event)"
	// \_SB.PCI0.GFX0.GLID = 0
	\LIDS = 0x00
	Notify (LID0, 0x80)
}

Method (_Q0D, 0, NotSerialized)		// Lid open event
{
	DEBUG = "---> ITE LID: Q0D (open event)"
	// \_SB.PCI0.GFX0.GLID = 1
	\LIDS = 0x01
	Notify (LID0, 0x80)
}
