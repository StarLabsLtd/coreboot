/* SPDX-License-Identifier: GPL-2.0-only */

//SCOPE EC0

Device (LID0)
{
	Name (_HID, EisaId ("PNP0C0D"))

	Method (_STA, 0, NotSerialized)
	{
		DEBUG = "---> IT8987 LID: _STA"
		Return (0x0F)
	}

//	Method (_PRW, 0, NotSerialized)
//	{
//		DEBUG = "---> IT8987 LID: _PRW"
//		Return (GPRW (0x0E, 0x03))
//	}

	Method (_PSW, 1, NotSerialized)
	{
		DEBUG = Concatenate ("---> IT8987 LID: _PSW", ToHexString(Arg0))
	}

	Method (_LID, 0, NotSerialized)
	{
		DEBUG = "---> IT8987 LID: _LID"
		If (LEqual (\_SB.PCI0.LPCB.H_EC.ECRD (RefOf (\_SB.PCI0.LPCB.H_EC.LSTE)), 0x01))
		{
//			Store (0x03, \_SB.PCI0.GFX0.CLID)
			Store (0x01, Local0)
		}
		else
		{
//			Store (0x00, \_SB.PCI0.GFX0.CLID)
			Store (0x00, Local0)
		}
		Return (Local0)
	}
}

Method (_Q0C, 0, NotSerialized)		// Lid close event
{
	DEBUG = "---> IT8987 LID: Q0C (close event)"  
	Store (0x00, LIDS)
//	Store (LIDS, \LIDS)
	Notify (LID0, 0x80)
}

Method (_Q0D, 0, NotSerialized)		// Lid open event
{
	DEBUG = "---> IT8987 LID: Q0D (open event)"
	Store (0x01, LIDS)
//	Store (LIDS, \LIDS)
 	Notify (LID0, 0x80)
}