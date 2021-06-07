/* SPDX-License-Identifier: GPL-2.0-only */

#include <option.h>
#include <console/console.h>
#include <device/azalia_device.h>

#include "variant/hda_verb.h"

#define AZALIA_CODEC_ALC256 0x10ec0256
#define AZALIA_CODEC_ALC269 0x10ec0269

const u32 override_verb[] = {
	AZALIA_PIN_CFG(0, 0x12, 0x411111f0),
};

static void disable_microphone(u8 *base)
{
	/* azalia_program_verb_table(base, cim_verb_data, ARRAY_SIZE(cim_verb_data)); */
	azalia_program_verb_table(base, override_verb, ARRAY_SIZE(override_verb));
	printk(BIOS_DEBUG, "CMOS: Disable attempted\n");
}

void mainboard_azalia_program_runtime_verbs(u8 *base, u32 viddid)
{
	uint8_t microphone = get_uint_option("microphone", 0xff);
	printk(BIOS_DEBUG, "CMOS: microphone = %d\n", microphone);

	if ((viddid == AZALIA_CODEC_ALC256) || (viddid == AZALIA_CODEC_ALC269)) {
		if (get_uint_option("microphone", 0)) {
			printk(BIOS_DEBUG, "CMOS: Disabling microphone\n");
			disable_microphone(base);
		}
	}
}
