/* SPDX-License-Identifier: GPL-2.0-only */

#include <common/hda.h>
#include <boot/coreboot_tables.h>
#include <device/azalia_codec/realtek.h>
#include <device/azalia_device.h>
#include <option.h>
#include <soc/pci_devs.h>

#define STARFIGHTER_GPIO2	0x04
#define STARFIGHTER_EAPD	0x02

static const uint32_t speaker_amp_enable_verb[] = {
	AZALIA_VERB_12B(0, 0x01, 0x716, STARFIGHTER_GPIO2),
	AZALIA_VERB_12B(0, 0x01, 0x717, STARFIGHTER_GPIO2),
	AZALIA_VERB_12B(0, 0x01, 0x715, STARFIGHTER_GPIO2),
	AZALIA_VERB_12B(0, ALC269_LINE2, 0x70c, STARFIGHTER_EAPD),
};

static const uint32_t microphone_disable_verb[] = {
	AZALIA_PIN_CFG(0, ALC269_DMIC12, AZALIA_PIN_CFG_NC(0)),
};

void mainboard_azalia_program_runtime_verbs(uint8_t *base, uint32_t viddid)
{
	starlabs_hda_program_legacy_subsystem_id_verbs(base, viddid);

	if (get_uint_option_checked("microphone", 1, OPTION_BOOL) == 0)
		azalia_program_verb_table(base, microphone_disable_verb,
					  ARRAY_SIZE(microphone_disable_verb));

	if (!CONFIG(PAYLOAD_EDIIE) && starlabs_hda_use_legacy_subsystem_id() &&
	    get_uint_option_checked("firmware_enable_amp", 1, OPTION_BOOL))
		azalia_program_verb_table(base, speaker_amp_enable_verb,
					  ARRAY_SIZE(speaker_amp_enable_verb));
}

void lb_board(struct lb_header *header)
{
	if (!CONFIG(PAYLOAD_EDIIE) ||
	    !get_uint_option_checked("firmware_enable_amp", 1, OPTION_BOOL))
		return;

	struct lb_hda_speaker_amp *amp = (void *)lb_new_record(header);

	*amp = (struct lb_hda_speaker_amp) {
		.tag = LB_TAG_HDA_SPEAKER_AMP,
		.size = sizeof(*amp),
		.codec_id = 0x10ec0235,
		.bdf = PCI_DEVFN_HDA,
		.gpio = 2,
		.eapd_node = ALC269_LINE2,
		.active_high = 1,
	};
}
