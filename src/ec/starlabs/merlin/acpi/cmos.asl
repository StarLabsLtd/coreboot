/* SPDX-License-Identifier: GPL-2.0-only */

OperationRegion (CMOS, SystemIO, 0x70, 0x2)
Field (CMOS, ByteAcc, NoLock, Preserve)
{
	IND1, 8,
	DAT1, 8,
}

IndexField (IND1, DAT1, ByteAcc, NoLock, Preserve)
{
	Offset (0x4b),
	KLTC, 3,		// Keyboard Backlight Timeout
	    , 5,		// Reserved
	FCLS, 1,		// Ctrl Fn Reverse (make keyboard Apple-like)
	    , 7,		// Reserved
	MXCH, 2,		// Max Charge Level
	    , 6,		// Reserved
	FNMD, 2,		// Fan Mode
	    , 6,		// Reserved
}

OperationRegion (CMS2, SystemIO, 0x72, 0x2)
Field (CMS2, ByteAcc, NoLock, Preserve)
{
	IND2, 8,
	DAT2, 8,
}

IndexField (IND2, DAT2, ByteAcc, NoLock, Preserve)
{
	Offset (0x80),
	FLKS, 1,		// Function Lock State
	    , 7,		// Reserved
	TPLS, 8,		// Trackpad State
	KLBC, 2,		// Keyboard Backlight Brightness
	    , 6,		// Reserved
	KLSC, 1,		// Keyboard Backlight State
	    , 7,		// Reserved
}

