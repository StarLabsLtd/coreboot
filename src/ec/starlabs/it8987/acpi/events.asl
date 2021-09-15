/* SPDX-License-Identifier: GPL-2.0-only */

Method (_Q0A, 0, NotSerialized)			// Event: AC Power Connected
{
	Notify (BAT0, 0x81)
	Notify (ADP1, 0x80)
}

Method (_Q0B, 0, NotSerialized)			// Event: AC Power Disconnected
{
	Notify (BAT0, 0x81)
	Notify (BAT0, 0x80)
}

Method (_Q0C)					// Event: Lid Close
{
	\LIDS = LSTE
	Notify (LID0, 0x80)
}

Method (_Q0D)					// Event: Lid Open
{
	\LIDS = LSTE
	Notify (LID0, 0x80)
}

Method (_Q99)					// Event: Airplane Mode
{
	^^^^HIDD.HPEM (8)
}

Method (_Q54)					// Event: Power Button
{
	P8XH(0, 0x54)
	ADBG("PB Sleep 0x80")
	// If (CondRefOf(\_SB.PWRB)){
	//	Notify(\_SB.PWRB, 0x80)
	// }
}

Method (_QD5)					// Event: 10 Second Power Button Press
{
	P8XH(0, 0xD5)
	// \_SB.PWPR()
}

Method (_QD6)					// Event: 10 Second Power Button Depress
{
	P8XH(0,0xD6)
	// \_SB.PWRR()
}

Method (_Q05)					// Event: Brightness Down
{
	^^^^HIDD.HPEM (20)
}

Method (_Q06)					// Event: Brightness Up
{
	^^^^HIDD.HPEM (19)
}

Method (_Q87)					// Event: Function Lock
{
	FLKS = FLKA
}

Method (_Q04)					// Event: Trackpad Lock
{
	TPLS = TPLA
}

Method (_Q89)					// Event: Keyboard Backlight Brightness
{
	KLBC = KLBE
}

Method (_Q90)					// Event: Keyboard Backlight State
{
	KLSC = KLSE
}

#if CONFIG(BOARD_STARLABS_LABTOP_CML)
Method (_Q45)
{
	SMB2 = 0xC1
}
#endif

