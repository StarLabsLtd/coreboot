/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>
#include <common/automatic_start.h>
#include <ec/starlabs/merlin/ec.h>
#include <variants.h>

static void init_mainboard(void *chip_info)
{
	const struct pad_config *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);
}

static void starlabs_mainboard_fill_ssdt(const struct device *dev)
{
	if (CONFIG(PAYLOAD_MM_INTERFACE) && CONFIG(STARLABS_ACPI_EFI_OPTION_SMI) &&
	    CONFIG(STARLABS_AUTOMATIC_START))
		starlabs_automatic_start_ssdt();
	merlin_fill_ssdt(dev);
}

static void enable_mainboard(struct device *dev)
{
	dev->ops->acpi_fill_ssdt = starlabs_mainboard_fill_ssdt;
}

struct chip_operations mainboard_ops = {
	.enable_dev = enable_mainboard,
	.init = init_mainboard,
};
