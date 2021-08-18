/* SPDX-License-Identifier: GPL-2.0-only */

#include <option.h>
#include <console/console.h>
#include <device/azalia_device.h>
#include <types.h>

#define AZALIA_CODEC_ALC256 0x10ec0256 /* 283902550 */
#define AZALIA_CODEC_ALC269 0x10ec0269

const u32 override_verb[] = {
	AZALIA_PIN_CFG(0, 0x12, 0x411111f0),
};

static void disable_microphone(u8 *base, u32 viddid)
{
	azalia_program_verb_table(base, override_verb, ARRAY_SIZE(override_verb));
}

void mainboard_azalia_program_runtime_verbs(u8 *base, u32 viddid)
{
	if ((viddid == AZALIA_CODEC_ALC256) || (viddid == AZALIA_CODEC_ALC269)) {
		printk(BIOS_DEBUG, "CMOS: viddid = %d\n", viddid);
		if (get_uint_option("microphone", 1) == 0) {
			printk(BIOS_DEBUG, "Disabling mic\n");
			disable_microphone(base, viddid);
		}
	}
}
