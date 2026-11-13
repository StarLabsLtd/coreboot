/* SPDX-License-Identifier: GPL-2.0-only */

#define __SIMPLE_DEVICE__
#include <tests/test.h>
#include <device/pci.h>
#include <device/pci_ops.h>
#include <string.h>

static uint32_t hardware_bar;
static unsigned int enables;
static resource_t base_at_enable;

static uint32_t read_bar(pci_devfn_t dev, unsigned int reg)
{
	assert_int_equal(dev, PCI_DEV(0, 0x1f, 5));
	assert_int_equal(reg, PCI_BASE_ADDRESS_0);
	return hardware_bar;
}

static void enable_resources(struct device *dev)
{
	enables++;
	base_at_enable = dev->resource_list ? dev->resource_list->base : 0;
}

/* Mock hardware/configuration and the unchanged generic enable call only. */
#undef pci_read_config32
#define pci_read_config32 read_bar
#define pci_dev_enable_resources enable_resources
#define main coreboot_main
#include "../../src/soc/intel/common/block/fast_spi/fast_spi.c"
#undef main

static struct device device;
static struct resource resource;

/* ASan retains the driver's operations table. These callbacks are outside
 * the resource-enable tests and must not be called by the exercised path. */
struct pci_operations pci_dev_ops_pci;
struct resource *free_resources;

void pci_dev_read_resources(struct device *dev)
{
	fail();
}

void pci_dev_set_resources(struct device *dev)
{
	fail();
}

void scan_static_bus(struct device *dev)
{
	fail();
}

const char *acpi_device_scope(const struct device *dev)
{
	fail();
	return NULL;
}

const char *acpi_device_name(const struct device *dev)
{
	fail();
	return NULL;
}

void acpi_device_write_uid(const struct device *dev)
{
	fail();
}

int acpi_device_status(const struct device *dev)
{
	fail();
	return 0;
}

void acpigen_write_scope(const char *name)
{
	fail();
}

void acpigen_write_device(const char *name)
{
	fail();
}

void acpigen_write_name_string(const char *name, const char *string)
{
	fail();
}

void acpigen_write_STA(uint8_t status)
{
	fail();
}

void acpigen_write_name(const char *name)
{
	fail();
}

void acpigen_write_resourcetemplate_header(void)
{
	fail();
}

void acpigen_write_mem32fixed(int readwrite, uint32_t base, uint32_t size)
{
	fail();
}

void acpigen_write_resourcetemplate_footer(void)
{
	fail();
}

void acpigen_pop_len(void)
{
	fail();
}

static int setup(void **state)
{
	memset(&device, 0, sizeof(device));
	memset(&resource, 0, sizeof(resource));
	device.resource_list = &resource;
	resource.index = PCI_BASE_ADDRESS_0;
	resource.base = 0xbeddf000;
	resource.size = 0x1000;
	resource.limit = resource.base + resource.size - 1;
	resource.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED | IORESOURCE_STORED;
	hardware_bar = 0xfe010000;
	enables = 0;
	base_at_enable = 0;
	return 0;
}

static void test_restored_bar_updates_resource_before_enable(void **state)
{
	const unsigned long flags = resource.flags;
	fast_spi_enable_resources(&device);
	assert_int_equal(resource.base, hardware_bar);
	assert_int_equal(base_at_enable, hardware_bar);
	assert_int_equal(resource.size, 0x1000);
	assert_int_equal(resource.limit, hardware_bar + resource.size - 1);
	assert_int_equal(resource.flags, flags);
	assert_int_equal(enables, 1);
}

static void test_unchanged_bar_preserves_resource(void **state)
{
	hardware_bar = resource.base;
	struct resource before = resource;
	fast_spi_enable_resources(&device);
	assert_memory_equal(&resource, &before, sizeof(resource));
	assert_int_equal(enables, 1);
}

static void test_invalid_bar_does_not_replace_resource(void **state)
{
	const uint32_t invalid[] = { 0, UINT32_MAX, 0xfe010001, 0xfe010004, 0xfe010100 };
	for (size_t i = 0; i < ARRAY_SIZE(invalid); i++) {
		hardware_bar = invalid[i];
		fast_spi_enable_resources(&device);
		assert_int_equal(resource.base, 0xbeddf000);
	}
	assert_int_equal(enables, ARRAY_SIZE(invalid));
}

static void test_32bit_bar_extent_bounds(void **state)
{
	hardware_bar = 0xfffff000;
	fast_spi_enable_resources(&device);
	assert_int_equal(resource.base, hardware_bar);
	assert_int_equal(resource.limit, UINT32_MAX);
	resource.base = 0xbeddf000;
	resource.limit = resource.base + 0xfff;
	resource.size = 1ULL << 32;
	hardware_bar = 0xfe010000;
	fast_spi_enable_resources(&device);
	assert_int_equal(resource.base, 0xbeddf000);
	assert_int_equal(resource.limit, 0xbeddffff);
	assert_int_equal(enables, 2);
}

static void test_missing_or_unassigned_resource_is_not_created(void **state)
{
	resource.flags &= ~IORESOURCE_ASSIGNED;
	fast_spi_enable_resources(&device);
	assert_int_equal(resource.base, 0xbeddf000);
	resource.flags = IORESOURCE_IO | IORESOURCE_ASSIGNED;
	fast_spi_enable_resources(&device);
	assert_int_equal(resource.base, 0xbeddf000);
	resource.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED;
	resource.size = 0;
	fast_spi_enable_resources(&device);
	assert_int_equal(resource.base, 0xbeddf000);
	device.resource_list = NULL;
	fast_spi_enable_resources(&device);
	assert_null(device.resource_list);
	assert_int_equal(enables, 4);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_restored_bar_updates_resource_before_enable, setup),
		cmocka_unit_test_setup(test_unchanged_bar_preserves_resource, setup),
		cmocka_unit_test_setup(test_invalid_bar_does_not_replace_resource, setup),
		cmocka_unit_test_setup(test_32bit_bar_extent_bounds, setup),
		cmocka_unit_test_setup(test_missing_or_unassigned_resource_is_not_created, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
