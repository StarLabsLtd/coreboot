/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <device/device.h>
#include <ec/starlabs/merlin/ec.h>
#if CONFIG(SOC_INTEL_COMMON_BLOCK_I2C_SMM)
#include <cpu/x86/smm.h>
#include <drivers/i2c/designware/dw_i2c.h>
#include <common/touchpad.h>
#endif
#include <variants.h>

static void starlabs_configure_gpios(void *unused)
{
	const struct pad_config *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);
}

BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_ENTRY, starlabs_configure_gpios, NULL);

static void enable_mainboard(struct device *dev)
{
	dev->ops->acpi_fill_ssdt = merlin_fill_ssdt;
}

#if CONFIG(SOC_INTEL_COMMON_BLOCK_I2C_SMM)
void smm_mainboard_pci_resource_store_init(struct smm_pci_resource_info *slots, size_t size)
{
	const int devfn = dw_i2c_soc_bus_to_devfn(STARLABS_TOUCHPAD_I2C_BUS);
	const struct device *device;

	if (devfn < 0)
		return;

	device = pcidev_path_on_root(devfn);
	if (!device)
		return;

	smm_pci_resource_store_fill_resources(slots, size, &device, 1);
}
#endif

struct chip_operations mainboard_ops = {
	.enable_dev = enable_mainboard,
};
