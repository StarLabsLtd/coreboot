/* SPDX-License-Identifier: GPL-2.0-only */

OperationRegion(ECF2, EmbeddedControl, 0, 0xFF)
Field (ECF2, ByteAcc, Lock, Preserve)
{
	Offset(0x00),
	ECMV, 8,	// Major Version Number
	ECSV, 8,	// Minor Version Number
	KBVS, 8,	// Keyboard Controller Version
	ECTV, 8,	// Test Version Number
	ECOS, 8,	// OS Flag
	FRMF, 8,	// Froce Mirror Flag
	SKUI, 8,	// SKU ID
	MDSB, 8,	// Modern Standby Flag

	Offset(0x09),
	KBLB, 8,	// Keyboard Backlight Brightness
	KBLE, 8,	// Keyboard Backlight State
	BDID, 8,	// Board ID
	TPEN, 8,	// Trackpad Enable
	ROTA, 8,	// Rotate Flag
	WIFI, 8,	// WiFi Enable
	FNST, 8,	// Function Lock Status

	Offset(0x13),
	AUDI, 8,	// Control Audio

	Offset(0x15),
	SURF, 8,	// Chassis Surface Temperature
	CHAR, 8,	// Charger Temperature
	FNCT, 8,	// Fn Ctrl Reverse

	Offset(0x1A),
	BFCP, 8,	// Battery Full Charge Percentage
	FANM, 8,	// Fan Mode
	FNLK, 8,	// Function Lock

	Offset(0x40),
	SHIP, 8,	// Shipping Mode Flag
	ECBT, 8,	// EC Build Time

	Offset(0x4B),
	ECBD, 8,	// EC Build Date

	Offset(0x62),
	SEN2, 8,	// Sensor 2 Temperature
	SENF, 8,	// Sensor F
	SENH, 8,	// Thermal Sensor High Trip Point
	SENL, 8,	// Thermal Sensor Low Trip Point
	THER, 8,	// Thermal Source

	Offset(0x68),
	BATT, 16,	// Battery Temperature

	Offset(0x70),
	BATC, 8,	// Battery Temperature Ces
	CPUT, 8,	// PECI CPU Temperature
	PMXT, 8,	// PLMX Temperature
	SEN1, 8,	// Sensor 1 Temperature
	SEN3, 8,	// Sensor 3 Temperature

	Offset(0x7F),
	LIDS, 1,	// Lid Status

	Offset(0x80),
	ECPS, 8,	// AC & Battery status
	XX10, 8,	// Battery Model Number Code
	XX11, 16,	// Battery Serial Number
	B1DC, 16,	// Battery Design Capacity
	B1FV, 16,	// Battery Design Voltage
	B1FC, 16,	// Battery Last Full Charge Capacity
	XX15, 16,	// Battery Trip Point
	B1ST, 8,	// Battery State
	B1CR, 16,	// Battery Present Rate
	B1RC, 16,	// Battery Remaining Capacity
	B1VT, 16,	// Battery Present Voltage
	BPCN, 8,	// Battery Remaining percentage

	Offset(0xc0),
	UCSV, 8,	// UCSI DS Version
	UCSR, 8,	// UCSI DS Reserved
	UCC0, 8,	// UCSI DS CCI 0
	UCC1, 8,	// UCSI DS CCI 1
	UCC2, 8,	// UCSI DS CCI 2
	UCC3, 8,	// UCSI DS CCI 3
	UCT0, 8,	// UCSI DS Control 0
	UCT1, 8,	// UCSI DS Control 0
	UCT2, 8,	// UCSI DS Control 0
	UCT3, 8,	// UCSI DS Control 0
	UCT4, 8,	// UCSI DS Control 0
	UCT5, 8,	// UCSI DS Control 0
	UCT6, 8,	// UCSI DS Control 0
	UCT7, 8,	// UCSI DS Control 0
	UCGI, 16,	// UCSI DS MGI
	UCGO, 16,	// UCSI DS MGO

	Offset(0xe6),
	ECWD, 16,	// EC Wakeup Delay
	ECWE, 8,	// EC Wakeup Enable

	Offset(0xf7),
	TBTC, 8,	// Thunderbolt Command
	TBTP, 8,	// Thunderbolt Data Port
	TBTD, 8,	// Thunderbolt Data
	TBTA, 8,	// Thunderbolt Acknowledge
	TBTG, 8,	// THunderbolt DBG Data
}
