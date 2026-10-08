/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <device/azalia_device.h>
#include <device/azalia_codec/realtek.h>

static const u32 realtek_alc256_verbs[] = {
	AZALIA_SUBVENDOR(0, 0x1e507030),

	/* Reset audio function group node 0x01. */
	0x0017ff00,
	0x0017ff00,
	0x0017ff00,
	0x0017ff00,

	/* Pin widget verb-table */
	AZALIA_PIN_CFG(0, ALC256_DMIC12, 0x90a60130),
	AZALIA_PIN_CFG(0, ALC256_DMIC34, 0x40000040),
	AZALIA_PIN_CFG(0, ALC256_SPEAKERS, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, 0x18, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, ALC256_MIC2, 0x04a19040),
	AZALIA_PIN_CFG(0, ALC256_LINE1, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, ALC256_LINE2, 0x90170110),
	AZALIA_PIN_CFG(0, ALC256_PC_BEEP, 0x40689a6d),
	AZALIA_PIN_CFG(0, ALC256_SPDIF_OUT, AZALIA_PIN_CFG_NC(0)),
	AZALIA_PIN_CFG(0, ALC256_HP_OUT, 0x04214020),

	/* Hidden SW reset & LDO3 output set to 1.2V */
	0x0205001a,
	0x0204c003,
	0x02050019,
	0x02040f52,

	/* Widget node 0x20 */
	0x0205001b,
	0x0204064b,
	0x02050045,
	0x0204b089,

	/* Widget node 0x20 - 1 */
	0x02050046,
	0x02040004,
	0x02050040,
	0x02048800,
};

const u32 pc_beep_verbs[] = {};

static struct azalia_codec mainboard_azalia_codecs[] = {
	{
		.name = "Realtek ALC256",
		.vendor_id = 0x10ec0256,
		.subsystem_id = 0x1e507030,
		.address = 0,
		.verbs = realtek_alc256_verbs,
		.verb_count = ARRAY_SIZE(realtek_alc256_verbs),
	},
};

AZALIA_ARRAY_SIZES;
