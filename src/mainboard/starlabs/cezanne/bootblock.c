/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootblock_common.h>
#include <amdblocks/lpc.h>
#include <soc/gpio.h>
#include <variants.h>

void bootblock_mainboard_early_init(void)
{
	uint32_t decodes = DECODE_ENABLE_ACPIUC_PORT | DECODE_ENABLE_KBC_PORT;
	lpc_enable_sio_decode(LPC_SELECT_SIO_4E4F);
	lpc_enable_decode(decodes);
}

void bootblock_mainboard_init(void)
{
	const struct soc_amd_gpio *pads;
	size_t num;

	pads = variant_early_gpio_table(&num);
	gpio_configure_pads(pads, num);
}
