/* SPDX-License-Identifier: GPL-2.0-only */

Method (_Q02, 0, NotSerialized)			// Event: APP
{
	Store ("EC: APP", Debug)
}

Method (_Q04, 0, NotSerialized)			// Event: Trackpad Lock
{
	TPLS = TPLA
}

Method (_Q06, 0, NotSerialized)			// Event: Backlight Brightness Down
{
	^^^^HIDD.HPEM (20)
}

Method (_Q07, 0, NotSerialized)			// Event: Backlight Brightness Up
{
	^^^^HIDD.HPEM (19)
}
Method (_Q08, 0, NotSerialized)			// Event: Function Lock
{
	FLKS = FLKA
}

Method (_Q0B, 0, NotSerialized)			// Event: AC Power Disconnected
{
	Notify (BAT0, 0x81)
	Notify (BAT0, 0x80)
}

Method (_Q0C, 0, NotSerialized)			// Event: Lid Closed
{
	\LIDS = LSTE
	Notify (LID0, 0x80)
}
Method (_Q0D, 0, NotSerialized)			// Event: Lid Opened
{
	\LIDS = LSTE
	Notify (LID0, 0x80)
}
Method (_Q22, 0, NotSerialized)			// Event: CHARGER_T
{
	Store ("EC: CHARGER_T", Debug)
}

Method (_Q40, 0, NotSerialized)			// Event: AC_DC

	Store ("EC: AC_DC", Debug)
}

Method (_Q41, 0, NotSerialized)			// Event: DC_20_0

	Store ("EC: DC_20_0", Debug)
}

Method (_Q42, 0, NotSerialized)			// Event: DC_60_20

	Store ("EC: DC_60_20", Debug)
}

Method (_Q43, 0, NotSerialized)			// Event: DC_100_60

	Store ("EC: DC_100_60", Debug)
}

Method (_Q44, 0, NotSerialized)			// Event: AC_ONLY

	Store ("EC: AC_ONLY", Debug)
}

Method (_Q54, 0, NotSerialized)			// Event: PWRBTN

	Store ("EC: PWRBTN", Debug)
}

Method (_Q80, 0, NotSerialized)			// Event: VOLUME_UP

	Store ("EC: VOLUME_UP", Debug)
}

Method (_Q81, 0, NotSerialized)			// Event: VOLUME_DOWN

	Store ("EC: VOLUME_DOWN", Debug)
}

Method (_Q82, 0, NotSerialized)			// Event: MIC

	Store ("EC: MIC", Debug)
}

Method (_Q83, 0, NotSerialized)			// Event: MUTE

	Store ("EC: MUTE", Debug)
}

Method (_Q99, 0, NotSerialized)			// Event: Airplane Mode
{
	^^^^HIDD.HPEM (8)
}

Method (_QA0), 0, NotSerialized)		// Event: AC Power Connected
{
	Notify (BAT0, 0x81)
	Notify (ADP1, 0x80)
}

Method (_QD5), 0, NotSerialized)		// Event: 10 Second Power Button Pressed
{
	Store ("EC: 10 Second Power Button Pressed", Debug)
}

Method (_QD6), 0, NotSerialized)		// Event: 10 Second Power Button Release
{
	Store ("EC: 10 Second Power Button Release", Debug)
}

Method (_QF0), 0, NotSerialized)		// Event: TEMP_REPORT
{

	Store ("EC: TEMP_REPORT", Debug)
}

Method (_QF1), 0, NotSerialized)		// Event: TrigPoint
{
	Store ("EC: TrigPoint", Debug)
}

// 
// TODO:
// Below Q Events need to be added
//
// Method (_Q)					// Event: Keyboard Backlight Brightness
// {
//	KLBC = KLBE
// }
//
// Method (_Q)					// Event: Keyboard Backlight State
// {
//	KLSC = KLSE
// }

