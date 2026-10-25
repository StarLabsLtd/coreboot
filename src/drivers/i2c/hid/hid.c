/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpigen_dsm.h>
#include <acpi/acpi_device.h>
#include <assert.h>
#include <device/device.h>
#include <stdio.h>

#include "chip.h"
#include <gpio.h>
#include <console/console.h>

#if CONFIG(DRIVERS_I2C_DESIGNWARE) && CONFIG(SOC_INTEL_COMMON_BLOCK_GPIO)
#include <boot/coreboot_tables.h>
#include <device/pci_def.h>
#include <device/resource.h>
#include <drivers/i2c/designware/dw_i2c.h>
#include <intelblocks/gpio.h>
#include <string.h>
#endif

#if CONFIG(HAVE_ACPI_TABLES)
static void i2c_hid_fill_dsm(const struct device *dev)
{
	struct drivers_i2c_hid_config *config = dev->chip_info;
	struct dsm_i2c_hid_config dsm_config = {
		.hid_desc_reg_offset = config->hid_desc_reg_offset,
	};

	acpigen_write_dsm_i2c_hid(&dsm_config);
}

static void i2c_hid_fill_ssdt_generator(const struct device *dev)
{
	struct drivers_i2c_hid_config *config = dev->chip_info;

	if (!config->generic.cid)
		config->generic.cid = I2C_HID_CID;

	i2c_generic_fill_ssdt(dev, &i2c_hid_fill_dsm, &config->generic);
}

static const char *i2c_hid_acpi_name(const struct device *dev)
{
	static char name[5];
	struct drivers_i2c_hid_config *config = dev->chip_info;
	if (config->generic.name)
		return config->generic.name;

	snprintf(name, sizeof(name), "H%03X", dev->path.i2c.device);
	name[4] = '\0';
	return name;
}
#endif

static struct device_operations i2c_hid_ops = {
	.read_resources		= noop_read_resources,
	.set_resources		= noop_set_resources,
#if CONFIG(HAVE_ACPI_TABLES)
	.acpi_name		= i2c_hid_acpi_name,
	.acpi_fill_ssdt		= i2c_hid_fill_ssdt_generator,
#endif
};

static void i2c_hid_enable(struct device *dev)
{
	struct drivers_i2c_hid_config *config = dev->chip_info;

	if (!config)
		return;

	/* Check if device is present by reading GPIO */
	if (config->generic.device_present_gpio) {
		int present = gpio_get(config->generic.device_present_gpio);
		present ^= config->generic.device_present_gpio_invert;

		printk(BIOS_INFO, "%s is %spresent\n",
		       dev->chip_ops->name, present ? "" : "not ");

		if (!present) {
			dev->enabled = 0;
			return;
		}
	}

	/*
	 * Ensure that I2C HID devices use level triggered interrupts as per ACPI
	 * I2C HID requirement. Check interrupt and GPIO interrupt.
	 */
	if ((!config->generic.irq_gpio.pin_count &&
	      config->generic.irq.mode != ACPI_IRQ_LEVEL_TRIGGERED) ||
	    (config->generic.irq_gpio.pin_count &&
	     config->generic.irq_gpio.irq.mode != ACPI_IRQ_LEVEL_TRIGGERED)) {
		printk(BIOS_ERR, "%s IRQ is not level triggered.\n", config->generic.hid);
		BUG();
	}

	dev->ops = &i2c_hid_ops;

	if (config && config->generic.desc) {
		dev->name = config->generic.desc;
	}
}

struct chip_operations drivers_i2c_hid_ops = {
	.name = "I2C HID Device",
	.enable_dev = i2c_hid_enable
};

#if CONFIG(DRIVERS_I2C_DESIGNWARE) && CONFIG(SOC_INTEL_COMMON_BLOCK_GPIO)
size_t fill_lb_i2c_hid(struct lb_prh_i2c_hid *devices, size_t capacity)
{
	size_t count = 0;

	for (const struct device *dev = all_devices; dev; dev = dev->next) {
		const struct drivers_i2c_hid_config *config;
		const struct device *controller;
		const struct resource *resource;
		const struct acpi_irq *irq;
		uintptr_t ready_address;
		struct lb_prh_i2c_hid *output;

		if (!dev->enabled || dev->chip_ops != &drivers_i2c_hid_ops ||
		    dev->path.type != DEVICE_PATH_I2C || !dev->chip_info ||
		    !dev->upstream || !dev->upstream->dev)
			continue;
		config = dev->chip_info;
		controller = dev->upstream->dev;
		if (!controller->enabled || controller->hidden || !controller->ops ||
		    controller->path.type != DEVICE_PATH_PCI ||
		    controller->ops->ops_i2c_bus != &dw_i2c_bus_ops ||
		    dev->path.i2c.mode_10bit || dev->path.i2c.device == 0 ||
		    dev->path.i2c.device > 0x7f)
			continue;
		irq = config->generic.irq_gpio.pin_count ?
			&config->generic.irq_gpio.irq : &config->generic.irq;
		if (irq->mode != ACPI_IRQ_LEVEL_TRIGGERED ||
		    irq->polarity == ACPI_IRQ_ACTIVE_BOTH)
			continue;
		resource = probe_resource(controller, PCI_BASE_ADDRESS_0);
		if (!resource || !resource->base || resource->size < 0x100 ||
		    resource->base > UINTPTR_MAX - (resource->size - 1) ||
		    resource->size > UINT32_MAX ||
		    (resource->flags & IORESOURCE_TYPE_MASK) != IORESOURCE_MEM ||
		    !(resource->flags & IORESOURCE_ASSIGNED) ||
		    !(resource->flags & (IORESOURCE_STORED | IORESOURCE_FIXED)) ||
		    (resource->flags & (IORESOURCE_BRIDGE | IORESOURCE_CACHEABLE |
				IORESOURCE_RESERVE | IORESOURCE_SUBTRACTIVE)))
			continue;
		if (config->generic.irq_gpio.pin_count) {
			if (config->generic.irq_gpio.pin_count != 1 ||
			    config->generic.irq_gpio.type != ACPI_GPIO_TYPE_INTERRUPT ||
			    config->generic.irq_gpio.resource ||
			    !gpio_input_address(config->generic.irq_gpio.pins[0], &ready_address))
				continue;
		} else if (!gpio_ioapic_input_address(irq->pin, &ready_address)) {
			printk(BIOS_INFO, "%s: no unique GPIO input for payload\n", dev_path(dev));
			continue;
		}
		if (count == capacity) {
			printk(BIOS_WARNING, "I2C HID payload inventory is full\n");
			break;
		}
		output = &devices[count++];
		memset(output, 0, sizeof(*output));
		output->revision = LB_PRH_I2C_HID_REVISION;
		output->family = LB_PRH_I2C_HID_DESIGNWARE;
		output->controller_base = resource->base;
		output->controller_size = resource->size;
		output->ready_address = ready_address;
		output->ready_mask = PAD_CFG0_RX_STATE;
		output->ready_value = irq->polarity == ACPI_IRQ_ACTIVE_LOW ?
			0 : PAD_CFG0_RX_STATE;
		output->descriptor_register = config->hid_desc_reg_offset;
		output->address = dev->path.i2c.device;
		printk(BIOS_INFO, "I2C HID payload device %s at %llx, address %02x\n",
		       dev_path(dev), resource->base, output->address);
	}
	return count;
}
#endif
