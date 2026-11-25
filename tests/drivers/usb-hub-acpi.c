/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi_device.h>
#include <acpi/acpigen.h>
#include <tests/test.h>

#include "drivers/usb/hub/chip.h"

extern struct chip_operations drivers_usb_hub_ops;

void scan_static_bus(struct device *dev)
{
}

static const char *parent_acpi_name(const struct device *dev)
{
	return "\\_SB";
}

static void shared_hub_config(void **state)
{
	struct drivers_usb_hub_config config = { .port_count = 2 };
	struct device_operations parent_ops = { .acpi_name = parent_acpi_name };
	struct device parent = { .ops = &parent_ops };
	struct bus parent_bus = { .dev = &parent };
	struct device hub;
	struct device ports[] = {
		{ .path = { .type = DEVICE_PATH_USB, .usb = { 2, 0 } }, .enabled = 1 },
		{ .path = { .type = DEVICE_PATH_USB, .usb = { 2, 99 } }, .enabled = 1 },
		{ .path = { .type = DEVICE_PATH_USB, .usb = { 2, 1 } }, .enabled = 1 },
	};
	struct bus downstream = { .dev = &hub, .children = ports };
	char aml[256], expected[128];

	hub = (struct device) {
		.path.type = DEVICE_PATH_USB, .upstream = &parent_bus,
		.downstream = &downstream, .chip_info = &config,
		.chip_ops = &drivers_usb_hub_ops,
	};
	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		ports[i].upstream = &downstream;
		ports[i].chip_info = hub.chip_info;
		ports[i].chip_ops = hub.chip_ops;
		if (i + 1 < ARRAY_SIZE(ports))
			ports[i].sibling = &ports[i + 1];
		ports[i].chip_ops->enable_dev(&ports[i]);
	}
	hub.chip_ops->enable_dev(&hub);

	const char *first = acpi_device_name(&ports[0]);
	assert_string_equal(acpi_device_name(&ports[2]), "HS02");
	assert_string_equal(first, "HS01");

	acpigen_set_current(aml);
	hub.ops->acpi_fill_ssdt(&hub);
	for (size_t i = 0; i < ARRAY_SIZE(ports); i++)
		ports[i].ops->acpi_fill_ssdt(&ports[i]);
	size_t length = acpigen_get_current() - aml;

	acpigen_set_current(expected);
	acpigen_write_scope("\\_SB");
	acpigen_write_device("EHUB");
	acpigen_write_ADR(0);
	acpigen_write_device("HS01");
	acpigen_write_name_byte("_ADR", 1);
	acpigen_write_device_end();
	acpigen_write_device("HS02");
	acpigen_write_name_byte("_ADR", 2);
	acpigen_write_device_end();
	acpigen_write_device_end();
	acpigen_write_scope_end();

	assert_int_equal(length, acpigen_get_current() - expected);
	assert_memory_equal(aml, expected, length);
}

int main(void)
{
	const struct CMUnitTest tests[] = { cmocka_unit_test(shared_hub_config) };

	return cb_run_group_tests(tests, NULL, NULL);
}
