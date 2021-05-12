/* SPDX-License-Identifier: GPL-2.0-only */
Device (LID0)
{
	Name (_HID, EisaId ("PNP0C0D"))

	Method (_STA, 0, NotSerialized)
	{
		DEBUG = "---> IT5570 LID: _STA"
		Return (0x0F)
	}


	Method (_LID, 0, NotSerialized)
	{
		DEBUG = "---> IT5570 LID: _LID"
		If (\_SB.PCI0.LPCB.H_EC.ECRD (RefOf (\_SB.PCI0.LPCB.H_EC.LSTE)) ==  0x01)
		{
			\_SB.PC00.GFX0.CLID = 3
			Local0 = 1
		}
		else
		{
			\_SB.PC00.GFX0.CLID = 0
			Local0 = 0
		}
		Return (Local0)
	}
}

Method (_Q0C, 0, NotSerialized)		// Lid close event
{
	DEBUG = "---> IT5570 LID: Q0C (close event)"
	LIDS = 0
	\LIDS = LIDS
	Notify (LID0, 0x80)
}

Method (_Q0D, 0, NotSerialized)		// Lid open event
{
	DEBUG = "---> IT5570 LID: Q0D (open event)"
	LIDS = 1
	\LIDS = LIDS
	Notify (LID0, 0x80)
}
