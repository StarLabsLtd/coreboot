/* SPDX-License-Identifier: GPL-2.0-only */

#include <smbios.h>
#include <types.h>
#include <uuid.h>
#include <option.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <types.h>
#include <chip.h>
#include <baseboard/variants.h>

#include "variant/ec.h"

const char *smbios_mainboard_bios_version(void)
{
	if (CONFIG(BOARD_STARLABS_STARBOOK_TGL))
		return "0";
	else
		return "6";
}

/* Get the Embedded Controller firmware version */
void smbios_ec_revision(uint8_t *ec_major_revision, uint8_t *ec_minor_revision)
{
	u16 ec_version = it_get_version();

	*ec_major_revision = ec_version >> 8;
	*ec_minor_revision = ec_version & 0xff;
}

const char *smbios_system_manufacturer(void)
{
	return "Star Labs";
}

const char *smbios_system_sku(void)
{
	if (CONFIG(BOARD_STARLABS_STARBOOK_TGL))
		return "B5";
	else if (CONFIG(BOARD_STARLABS_LABTOP_CML))
		return "L4";
	else if (CONFIG(BOARD_STARLABS_LABTOP_KBL))
		return "L3-U";
	else
		return "Unknown";
}

u8 smbios_mainboard_feature_flags(void)
{
	return SMBIOS_FEATURE_FLAGS_HOSTING_BOARD | SMBIOS_FEATURE_FLAGS_REPLACEABLE;
}

const char *smbios_mainboard_location_in_chassis(void)
{
	return "Default";
}

const char *smbios_mainboard_asset_tag(void)
{
	return "Default";
}

smbios_enclosure_type smbios_mainboard_enclosure_type(void)
{
	return SMBIOS_ENCLOSURE_NOTEBOOK;
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
	struct device *nic_dev = NULL;
	config_t *cfg = config_of_soc();

	/* Perform any variant specific changes and return the nic_dev */
	nic_dev = variant_devtree_update();

	if (nic_dev != NULL) {
		if (get_uint_option("wireless", 0) == 0)
			nic_dev->enabled = 0;
	}

	if (get_uint_option("webcam", 0) == 0)
		cfg->usb2_ports[6].enable = 0;
}

