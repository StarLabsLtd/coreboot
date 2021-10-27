/* SPDX-License-Identifier: GPL-2.0-only */

#include <baseboard/variants.h>
#include <chip.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <ec/starlabs/merlin/ec.h>
#include <limits.h>
#include <option.h>
#include <smbios.h>
#include <types.h>
#include <uuid.h>

const char *smbios_mainboard_bios_version(void)
{
	return "8";
}

/* Get the Embedded Controller firmware version */
void smbios_ec_revision(uint8_t *ec_major_revision, uint8_t *ec_minor_revision)
{
	u16 ec_version = it_get_version();

	*ec_major_revision = ec_version >> 8;
	*ec_minor_revision = ec_version & 0xff;
}

const char *smbios_system_sku(void)
{
	if (CONFIG(BOARD_STARLABS_LABTOP_KBL))
		return "L3-U";
	else if (CONFIG(BOARD_STARLABS_LABTOP_CML))
		return "L4";
	else if (CONFIG(BOARD_STARLABS_STARBOOK_TGL))
		return "B5";
}

u8 smbios_mainboard_feature_flags(void)
{
	return SMBIOS_FEATURE_FLAGS_HOSTING_BOARD | SMBIOS_FEATURE_FLAGS_REPLACEABLE;
}

const char *smbios_chassis_version(void)
{
	return smbios_mainboard_version();
}

const char *smbios_chassis_serial_number(void)
{
	return smbios_mainboard_serial_number();
}

__weak struct device *variant_devtree_update(void)
{
	return NULL;
}

/* Override dev tree settings based on CMOS settings */
void devtree_update(void)
{
	/* Perform any variant specific changes */
	variant_devtree_update();
}
