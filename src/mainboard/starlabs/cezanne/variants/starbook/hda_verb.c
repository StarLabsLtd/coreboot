/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/azalia_device.h>

const u32 cim_verb_data[] = {
	/* coreboot specific header */
	0x14f15098, /* Codec Vendor / Device ID: Conexant CX20632 */
	0x14f10216, /* Subsystem ID */
	14,	    /* Number of jacks (NID entries) */

	/* Reset Codec First */
	AZALIA_RESET(0x1),

	/* HDA Codec Subsystem ID: 0x14f10216 */
	AZALIA_SUBVENDOR(0, 0x14f10216),

	/* Pin Widget Verb-table */
	AZALIA_PIN_CFG(0, 0x01, 0x00000000),
	AZALIA_PIN_CFG(0, 0x19, 0x042b1010),
	AZALIA_PIN_CFG(0, 0x1a, 0x04ab1020),
	AZALIA_PIN_CFG(0, 0x1b, 0x411111f0),
	AZALIA_PIN_CFG(0, 0x1c, 0x411111f0),
	AZALIA_PIN_CFG(0, 0x1d, 0x411111f0),
	AZALIA_PIN_CFG(0, 0x1e, 0x90a61120),
	AZALIA_PIN_CFG(0, 0x1f, 0x411111f0),
	AZALIA_PIN_CFG(0, 0x20, 0x411111f0),
	AZALIA_PIN_CFG(0, 0x21, 0x90171110),
	AZALIA_PIN_CFG(0, 0x26, 0x411111f0),

	/* Enable EAPD */
	0x01870c02,
	0xffffffff,
	0xffffffff,
	0xffffffff,
};

const u32 pc_beep_verbs[] = {};

AZALIA_ARRAY_SIZES;
