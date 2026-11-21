/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <efi_variable_service.h>
#include <intelblocks/fast_spi.h>

const struct region_device *efi_variable_service_write_device(void)
{
	if (!fast_spi_wpd_status())
		return NULL;
	boot_device_init();
	return boot_device_rw();
}
