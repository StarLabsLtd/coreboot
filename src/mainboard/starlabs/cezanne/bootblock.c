/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootblock_common.h>
#include <amdblocks/lpc.h>
#include <soc/espi.h>
#include <soc/gpio.h>
#include <timer.h>
#include <variants.h>

void bootblock_mainboard_early_init(void)
{
	const struct soc_amd_gpio *pads;
	size_t num;

	struct stopwatch pcie_init_timeout_sw;

	pads = variant_early_gpio_table(&num);
	gpio_configure_pads(pads, num);

	/* Delay 10ms for SSD initialisation */
	stopwatch_init_usecs_expire(&pcie_init_timeout_sw, (10 * USECS_PER_MSEC));

	lpc_enable_sio_decode(LPC_SELECT_SIO_4E4F);
	lpc_enable_decode(DECODE_ENABLE_KBC_PORT | DECODE_ENABLE_ACPIUC_PORT);
}

void bootblock_mainboard_init(void)
{
	const struct soc_amd_gpio *pads;
	size_t num;

	pads = variant_bootblock_gpio_table(&num);
	gpio_configure_pads(pads, num);
}
