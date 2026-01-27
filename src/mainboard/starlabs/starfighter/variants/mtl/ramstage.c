/* SPDX-License-Identifier: GPL-2.0-only */

#include <option.h>
#include <bootstate.h>
#include <commonlib/helpers.h>
#include <drivers/intel/gma/opregion.h>
#include <soc/ramstage.h>
#include <variants.h>
#include <common/pin_mux.h>

/* SSD (PCH) power sequence: 2. power on; clkreq on; PERST still asserted */
static const struct pad_config pch_ssd_pwr_seq2_pads[] = {
	PAD_CFG_GPO(GPP_D22, 1, DEEP),					/* Enable */
	PAD_CFG_GPO(GPP_H00, 0, PLTRST),				/* Reset */
	PAD_CFG_NF_IOSTANDBY_IGNORE(GPP_C10, NONE, PLTRST, NF1),	/* Clock Request 1 */
};

/* SSD (PCH) power sequence: 3. PERST de-asserted */
static const struct pad_config pch_ssd_pwr_seq3_pads[] = {
	PAD_CFG_GPO(GPP_H00, 1, PLTRST),				/* Reset */
};

static void pch_ssd_power_sequence_step2(void *unused)
{
	gpio_configure_pads(pch_ssd_pwr_seq2_pads, ARRAY_SIZE(pch_ssd_pwr_seq2_pads));
}

static void pch_ssd_power_sequence_step3(void *unused)
{
	gpio_configure_pads(pch_ssd_pwr_seq3_pads, ARRAY_SIZE(pch_ssd_pwr_seq3_pads));
}

BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_EXIT, pch_ssd_power_sequence_step2, NULL);
BOOT_STATE_INIT_ENTRY(BS_DEV_INIT_CHIPS, BS_ON_ENTRY, pch_ssd_power_sequence_step3, NULL);

void mainboard_silicon_init_params(FSP_S_CONFIG *supd)
{
	configure_pin_mux(supd);
	supd->TcNotifyIgd = 2; // Auto
}

const char *mainboard_vbt_filename(void)
{
	if (get_uint_option("display_native_res", 0) == 1)
		return "vbt_native_res.bin";


	if (get_memory_config_straps() == 13)
		return "vbt_qhd.bin";
	return "vbt.bin";
}
