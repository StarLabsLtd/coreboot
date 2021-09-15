/* SPDX-License-Identifier: GPL-2.0-only */

Device (ADP1)
{
	Name (_HID, "ACPI0003")
	Method (_STA)
	{
		Return (0x0F)
	}
	Method (_PSR, 0)
	{
		If (ECPS && 0x01)
		{
			PWRS = 0x01
		} Else {
			PWRS = 0x00
		}
		Return(PWRS)
	}
	Method (_PCL, 0)
	{
		Return (
			Package() { _SB }
		)
	}
}
