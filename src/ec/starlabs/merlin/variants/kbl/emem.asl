/* SPDX-License-Identifier: GPL-2.0-only */

OperationRegion (ECF2, EmbeddedControl, 0x00, 0xFF)
Field (ECF2, ByteAcc, Lock, Preserve)
{
	Offset(0x00),
	ECMV, 8,	// Major Version Number
	ECSV, 8,	// Minor Version Number
	KBVS, 8,	// Keyboard Controller Version
	ECTV, 8,	// Test Version Number
	FRMF, 8,	// Froce Mirror Flag

	Offset(0x0c),
	ECBY, 8,	// Build Year
	ECBM, 8,	// Build Month
	ECBD, 8,	// Build Day
	ECBI, 8,	// Build Index

	Offset(0x10),
	CPWR, 8,	// Control Power
	CDEV, 8,	// Control Device
	OSFG, 8,	// OS Flag

	Offset(0x14)
	TPLA, 8,	// Trackpad State

	Offset(0x18)
	KLSE, 8,	// Keyboard Backlight State
	KLBE, 8,	// Keyboard Backlight Brightness
	KLTE, 8,	// Keyboard Backlight Timeout

	Offset(0x20),
	TCHC, 8,	// Thermal Charge CMD
	TCHF, 8,	// Thermal Charge Flag

	Offset(0x2c),
	FLKA, 8,	// Function Lock State

	Offset(0x30),
	STEF, 8,	// Sensor T Error F

	Offset(0x40)
	SHIP, 8,        // Shipping Mode Flag

	Offset(0x42)
	FANM, 8,	// Fan Mode
	KBFL, 8,	// Keyboard Flag

	Offset(0x50),
	CHRA, 16,	// Charge Rate
	CHIC, 16,	// Charge Input Current
	CHVL, 16,	// Charge Vlot
	CHOP, 16,	// Charge Option

	Offset(0x62),
	TSE2, 8,	// Sensor 2 Temperature
	SENF, 8,	// Sensor F
	TSHT, 8,	// Thermal Sensor High Trip Point
	TSLT, 8,	// Thermal Sensor Low Trip Point
	THER, 8,	// Thermal Source


	Offset(0x70),
	CPUT, 8,	// PECI CPU Temperature
	PMXT, 8,	// PLMX Temperature
	CHAR, 8,	// Charger Temperature

	Offset(0x7e),
	OCTF, 8,	// OEM Control Flag
	LSTE, 1,	// Lid Status
	    , 7,	// Reserved

	Offset(0x80),
	ECPS, 8,	// AC & Battery status
	B1MN, 8,	// Battery Model Number Code
	B1SN, 16,	// Battery Serial Number
	B1DC, 16,	// Battery Design Capacity
	B1DV, 16,	// Battery Design Voltage
	B1FC, 16,	// Battery Last Full Charge Capacity
	B1TP, 16,	// Battery Trip Point
	B1ST, 8,	// Battery State
	B1PR, 16,	// Battery Present Rate
	B1RC, 16,	// Battery Remaining Capacity
	B1PV, 16,	// Battery Present Voltage
	BPRP, 8,	// Battery Remaining percentage
	BT1A, 8,	// Bt1 ASOC

	// Unicorn - doesn't actually exist
	Offset(0x9d),
	OPWE, 8,	// OPM write to EC flag for UCSI
	// Unicorn - doesn't actually exist

	Offset(0xbf),
	EJ8A, 8,	// EJ898A Firmware Version

	Offset(0xc0),
	MGI0, 8,	// UCSI DS MGI 0
	MGI1, 8,	// UCSI DS MGI 1
	MGI2, 8,	// UCSI DS MGI 2
	MGI3, 8,	// UCSI DS MGI 3
	MGI4, 8,	// UCSI DS MGI 4
	MGI5, 8,	// UCSI DS MGI 5
	MGI6, 8,	// UCSI DS MGI 6
	MGI7, 8,	// UCSI DS MGI 7
	MGI8, 8,	// UCSI DS MGI 8
	MGI9, 8,	// UCSI DS MGI 9
	MGIA, 8,	// UCSI DS MGI A
	MGIB, 8,	// UCSI DS MGI B
	MGIC, 8,	// UCSI DS MGI C
	MGID, 8,	// UCSI DS MGI D
	MGIE, 8,	// UCSI DS MGI E
	MGIF, 8,	// UCSI DS MGI F

	Offset(0xd0),
	MGO0, 8,	// UCSI DS MGO 0
	MGO1, 8,	// UCSI DS MGO 1
	MGO2, 8,	// UCSI DS MGO 2
	MGO3, 8,	// UCSI DS MGO 3
	MGO4, 8,	// UCSI DS MGO 4
	MGO5, 8,	// UCSI DS MGO 5
	MGO6, 8,	// UCSI DS MGO 6
	MGO7, 8,	// UCSI DS MGO 7
	MGO8, 8,	// UCSI DS MGO 8
	MGO9, 8,	// UCSI DS MGO 9
	MGOA, 8,	// UCSI DS MGO A
	MGOB, 8,	// UCSI DS MGO B
	MGOC, 8,	// UCSI DS MGO C
	MGOD, 8,	// UCSI DS MGO D
	MGOE, 8,	// UCSI DS MGO E
	MGOF, 8,	// UCSI DS MGO F

	Offset(0xe0),
	UCSV, 16,	// UCSI DS Version
	UCSD, 16,	// UCSI DS Reserved
	CCI0, 8,	// UCSI DS CCI 0
	CCI1, 8,	// UCSI DS CCI 1
	CCI2, 8,	// UCSI DS CCI 2
	CCI3, 8,	// UCSI DS CCI 3
	CTL0, 8,	// UCSI DS Control 0
	CTL1, 8,	// UCSI DS Control 0
	CTL2, 8,	// UCSI DS Control 0
	CTL3, 8,	// UCSI DS Control 0
	CTL4, 8,	// UCSI DS Control 0
	CTL5, 8,	// UCSI DS Control 0
	CTL6, 8,	// UCSI DS Control 0
	CTL7, 8,	// UCSI DS Control 0

	Offset(0xf0),
	P0SD, 8,	// PD Port Status DD
	P0S4, 8,	// PD Port Status 4
	P0S5, 8,	// PD Port Status 5
	P0SE, 8,	// PD Port Status E
	P0SA, 8,	// PD Port Status 10
	P0SB, 8,	// PD Port Status 11

	Offset(0xfd),
	STCD, 8,	// Shutdown Code
	EJ8R, 8,	// EJ898A Need Reboot
	// TODO:
	// Causes build failure, but 0xff is in range	
	// EJ8E, 8,	// EJ898A Error
}
