/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/azalia_device.h>

const u32 cim_verb_data[] = {
	/* coreboot specific header */
	0x10ec0269, // Codec Vendor / Device ID: Realtek ALC269
	0xffffffff, // Subsystem ID
	16,	    // Number of jacks (NID entries)

	/* Reset Codec First */
	AZALIA_RESET(0x1),

	/* HDA Codec Subsystem ID Verb-table */
	AZALIA_SUBVENDOR(0, 0x10EC111E),

	/* Pin Widget Verb-table */
	AZALIA_PIN_CFG(0, 0x01, 0x00000000),
	AZALIA_PIN_CFG(0, 0x12, 0x90a61120),
	AZALIA_PIN_CFG(0, 0x14, 0x90171110),
	AZALIA_PIN_CFG(0, 0x15, 0x042B1010),
	AZALIA_PIN_CFG(0, 0x17, 0x411111F0),
	AZALIA_PIN_CFG(0, 0x18, 0x04AB1020),
	AZALIA_PIN_CFG(0, 0x19, 0x411111F0),
	AZALIA_PIN_CFG(0, 0x1A, 0x411111F0),
	AZALIA_PIN_CFG(0, 0x1B, 0x411111F0),
	AZALIA_PIN_CFG(0, 0x1D, 0x411111F0),
	AZALIA_PIN_CFG(0, 0x1E, 0x411111F0),

	/* Widget node 0x20 */
	0x02050018,
	0x02040184, /* Stock: 0x02043984 */
	0x0205001C,
	0x02040800,

	/* Widget node 0x20 - 1 */
	0x02050024,
	0x02040000,
	0x02050004,
	0x02040080,

	/* Widget node 0x20 - 2 */
	0x02050008,
	0x02040300,
	0x0205000C,
	0x02043F00,

	/* Widget node 0x20 - 3 */
	0x02050015,
	0x02048002,
	0x02050015,
	0x02048002,

	/* Widget node 0x0C */
	0x00C37080,
	0x00270610,
	0x00D37080,
	0x00370610,

	/*
	 * Equalizer:
	 *
	 * AGC
	 * Threshold:	- 6.00 dB
	 * Front Boost:	+ 6.00 dB
	 * Post Boost:	+ 6.00 dB
	 *
	 * Low Pass Filter
	 * Boost Gain:	Enabled
	 * BW:		200Hz
	 * Gain:	+ 4.00 dB
	 *
	 * Band Pass Filter 1
	 * Fc:		240Hz
	 * BW:		400Hz
	 * Gain:	+ 6.00 dB
	 *
	 * Band Pass Filter 2
	 * Fc:		16000Hz
	 * BW:		1000Hz
	 * Gain:	- 12.00 dB
	 *
	 * High Pass Filter
	 * Boost Gain:	Enabled
	 * BW:		200Hz
	 * Gain:	- 4.00 dB
	 *
	 * Class D Amp
	 * Power:	2.5W
	 * Resistance:	4ohms
	 *
	 * EQ Output
	 * Left:	+ 3.00 dB
	 * Right:	+ 3.00 dB
	 *
	 * VARQ
	 * Q:		0.707
	 */

	0x05350000,
	0x0534065A,
	0x0535001d,
	0x05340800,

	0x0535001e,
	0x05340B4C,
	0x05350003,
	0x05341F7A,

	0x05350004,
	0x0534FA18,
	0x0535000F,
	0x05341961,

	0x05350010,
	0x053412C2,
	0x05350011,
	0x0534F405,

	0x05350018,
	0x0534C1AA,
	0x05350019,
	0x05341E5E,

	0x0535001A,
	0x05340FED,
	0x0535001B,
	0x05341F2C,

	0x0535001C,
	0x0534095C,
	0x05450000,
	0x05440000,

	0x0545001d,
	0x05440800,
	0x0545001e,
	0x05440B4C,

	0x05450003,
	0x05441F7A,
	0x05450004,
	0x0544FA18,

	0x0545000F,
	0x05441961,
	0x05450010,
	0x054412C2,

	0x05450011,
	0x0544F405,
	0x05450018,
	0x0544C1AA,

	0x05450019,
	0x05441E5E,
	0x0545001A,
	0x05440FED,

	0x0545001B,
	0x05441F2C,
	0x0545001C,
	0x0544095C,

	0x05350000,
	0x0534465A,
	0x02050038,
	0x02044901,

	0x02050013,
	0x02044193,
	0x02050016,
	0x02044E50,

	0x02050012,
	0x0204EBC4,
	0x02050020,
	0x020451FF,

	0x8086280b, /* Codec Vendor / Device ID: Intel */
	0x80860101, /* Subsystem ID */
	4,	    /* Number of 4 dword sets */

	AZALIA_SUBVENDOR(2, 0x80860101),

	AZALIA_PIN_CFG(2, 0x05, 0x18560010),
	AZALIA_PIN_CFG(2, 0x06, 0x18560010),
	AZALIA_PIN_CFG(2, 0x07, 0x18560010),
};

const u32 pc_beep_verbs[] = {};

AZALIA_ARRAY_SIZES;
