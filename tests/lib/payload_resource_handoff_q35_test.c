/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <device/device.h>

int main(void)
{
	struct device device = { 0 };
	uint16_t priority = 0;

	if (!payload_resource_revision4_ready())
		return 1;
	device.class = 0x010802;
	if (!payload_resource_boot_controller(&device, &priority) || priority != 10)
		return 1;
	device.class = 0x0c0330;
	if (!payload_resource_boot_controller(&device, &priority) || priority != 20)
		return 1;
	device.class = 0x010601;
	if (!payload_resource_boot_controller(&device, &priority) || priority != 30)
		return 1;
	if (payload_resource_boot_controller(&device, NULL))
		return 1;
	device.class = 0x030000;
	if (payload_resource_boot_controller(&device, &priority))
		return 1;
	return 0;
}
