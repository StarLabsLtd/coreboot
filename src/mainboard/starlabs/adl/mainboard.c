/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <boot/coreboot_tables.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <ec/starlabs/merlin/ec.h>
#include <variants.h>

bool payload_resource_firmware_owned(const struct device *dev)
{
	return dev->path.type == DEVICE_PATH_PCI && dev->upstream != NULL &&
		dev->upstream->secondary == 0 &&
		(dev->path.pci.devfn == PCI_DEVFN(22, 0) ||
		 dev->path.pci.devfn == PCI_DEVFN(31, 5));
}

static void starlabs_configure_mainboard(void *unused)
{
	const struct pad_config *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);
}

BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_ENTRY, starlabs_configure_mainboard, NULL);

void __weak starlabs_adl_mainboard_fill_ssdt(const struct device *dev)
{
	(void)dev;
}

static void starlabs_mainboard_fill_ssdt(const struct device *dev)
{
	merlin_fill_ssdt(dev);
	starlabs_adl_mainboard_fill_ssdt(dev);
}

static void enable_mainboard(struct device *dev)
{
	dev->ops->acpi_fill_ssdt = starlabs_mainboard_fill_ssdt;
}

struct chip_operations mainboard_ops = {
	.enable_dev = enable_mainboard,
};
