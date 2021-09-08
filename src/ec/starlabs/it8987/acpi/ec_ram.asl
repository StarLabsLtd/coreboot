OperationRegion(ECF2, EmbeddedControl, 0, 0xFF)
Field (ECF2, ByteAcc, Lock, Preserve)
{
	Offset(0x00),
	XXX0, 8,	// EC Firmware main - version number.
	XXX1, 8,	// EC Firmware sub - version number.
	XXX2, 8,	// EC Firmware test - version number.

	Offset(0x06),
	SKID, 8,	// SKU ID

	Offset(0x11),
	KBCD, 8,	// Key / Touch Pad disable/enable bit
	ECOS, 8,	// Enter OS flag
	HDAO, 8,
	ECHK, 8,	// Hot keys flag

	Offset(0x18),
	KLBS, 8,	// Keyboard backlight begin.
	KLBE, 8,	// Keyboard backlight status.

	Offset(0x1A),
	KBLT, 8,	// Keyboard Backlight Timeout
	PWPF, 8,	// Power Profile

	Offset(0x1E),
	BTHP,8,		// Health Battery Percentage

	Offset(0x20),
	RCMD, 8,	// Same function as IO 66 port to send EC command
	RCST, 8,	// Report status for the result of command execution

	Offset(0x2C),
	FNST, 8,	// FN LOCK key status.

	Offset(0x3F),
	SFAN, 8,	// Set Fan Speed.
	BTMP, 16,	// Battery Temperature.
	BCNT, 16,	// Battery Cycle Count.
	FRMP, 16,	// Fan Current Speed.

	Offset(0x60),
	TSR1, 8,	// Thermal Sensor Register 1 [CPU VR (IMVP) Temp on RVP]
	TSR2, 8,	// Thermal Sensor Register 2 [Heat exchanger fan temp on RVP]
	TER4, 8,	// Thermal Sensor Register 3 (skin temperature)

	Offset(0x63),
	TSI,  4,	// [0..3]  0 = SEN1 - CPU VR temperature sensor
			// 1 = SEN2 - Heat Exchanger temperature sensor
			// 2 = SEN3 - Skin temperature sensor
			// 3 = SEN4 - Ambient temperature sensor
			// 4 = SEN5 - DIMM temperature sensor [IR sensor 1 on WSB]
			// 5 = SEN6 - not used on RVP
	HYST, 4,	// [4..7] - Hysteresis in degC.
	TSHT, 8,	// Thermal Sensor (N) high trip point(set default value =70)
	TSLT, 8,	// Thermal Sensor (N) low trip point (set default value =70)
	TSSR, 8,	// TSSR- thermal sensor status register (set bit2 =1)
			// BIT0: SEN1 - CPU VR Temp Sensor Trip Flag
			// BIT1: SEN2 - Fan Temp Sensor Trip Flag
			// BIT2: SEN3 - Skin Temp Sensor Trip Flag
			// BIT3: SEN4 - Ambient Temp Sensor Trip Flag
			// BIT4: Reserved
			// BIT5: Reserved
			// BIT6: Reserved
			// BIT7: Reserved
	CHGR, 16,	// Charge Rate

	Offset(0x70),
	CPTM, 8,	// CPU Temperature

	Offset(0x72),
	TER2, 8,	// Charger Temperature, Charger thermistor support

	Offset(0x7F),
	LIDS, 1,	// BIT0 LID GPI
	    , 7,	// Reserved

	Offset(0x80),
	ECPS, 8,	// AC & Battery status
	XX10, 8,	// Battery#1 Model Number Code
	XX11, 16,	// Battery#1 Serial Number
	B1DC, 16,	// Battery#1 Design Capacity
	B1FV, 16,	// Battery#1 Design Voltage
	B1FC, 16,	// Battery#1 Last Full Charge Capacity
	XX15, 16,	// Battery#1 Trip Point
	B1ST, 8,	// Battery#1 State
	B1CR, 16,	// Battery#1 Present Rate
	B1RC, 16,	// Battery#1 Remaining Capacity
	B1VT, 16,	// Battery#1 Present Voltage
	BPCN, 8,	// Battery#1 Remaining percentage

	// USB Type C Mailbox Interface// PPM->OPM Message In
	Offset(0xc0),
	MGI0, 8,
	MGI1, 8,
	MGI2, 8,
	MGI3, 8,
	MGI4, 8,
	MGI5, 8,
	MGI6, 8,
	MGI7, 8,
	MGI8, 8,
	MGI9, 8,
	MGIA, 8,
	MGIB, 8,
	MGIC, 8,
	MGID, 8,
	MGIE, 8,
	MGIF, 8,

	// USB Type C Mailbox Interface// OPM->PPM Message Out
	MGO0, 8,
	MGO1, 8,
	MGO2, 8,
	MGO3, 8,
	MGO4, 8,
	MGO5, 8,
	MGO6, 8,
	MGO7, 8,
	MGO8, 8,
	MGO9, 8,
	MGOA, 8,
	MGOB, 8,
	MGOC, 8,
	MGOD, 8,
	MGOE, 8,
	MGOF, 8,

	// USB Type C UCSI DATA Structure.
	VER1, 8,
	VER2, 8,
	RSV1, 8,
	RSV2, 8,

	// PPM->OPM CCI indicator
	CCI0, 8,
	CCI1, 8,
	CCI2, 8,
	CCI3, 8,

	// OPM->PPM Control message
	CTL0, 8,
	CTL1, 8,
	CTL2, 8,
	CTL3, 8,
	CTL4, 8,
	CTL5, 8,
	CTL6, 8,
	CTL7, 8,

	Offset(0xF0),
	    , 3,	// BIT0 .. BIT2 Reserved
	TPCC, 1,	// BIT3 TypeC connection bit
	    , 2,	// BIT4 .. BIT5 Reserved
	DRMD, 1,	// Bit6 Dual Role Mode. 0->DFP: Host mode; 1->UFP: Device Mode.
	    , 1,	// BIT7 Reserved
		}
