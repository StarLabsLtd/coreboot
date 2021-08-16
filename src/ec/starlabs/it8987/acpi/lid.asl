/* SPDX-License-Identifier: GPL-2.0-only */

Device (LID0)
{
	Name (_HID, EisaId ("PNP0C0D"))
	Method (_LID, 0)
	{
		Return (^^LIDS)
	}
}

Method (_Q0C, 0, NotSerialized)		// Lid close event
{
	Store ("EC: LID CLOSE", Debug)
	Store (LIDS, \LIDS)
	Notify (LID0, 0x80)
}

Method (_Q0D, 0, NotSerialized)		// Lid open event
{
	Store ("EC: LID OPEN", Debug)
	Store (LIDS, \LIDS)
	Notify (LID0, 0x80)
}
