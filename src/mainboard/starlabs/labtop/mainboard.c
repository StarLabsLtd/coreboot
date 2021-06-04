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

#if CONFIG(BOARD_STARLABS_STARBOOK_TGL)
#include <ec/starlabs/it5570/ec.h>
#else
#include <ec/starlabs/it8987/ec.h>
#endif

/* Override the BIOS version using smbios_mainboard_bios_version() */
const char *smbios_mainboard_bios_version(void)
{
#if CONFIG(BOARD_STARLABS_STARBOOK_TGL)
	return "0";
#else
	return "6";
#endif
}

/* Get the Embedded Controller firmware version */
void smbios_ec_revision(uint8_t *ec_major_revision, uint8_t *ec_minor_revision)
{
	u16 ec_version = it_get_version();

	*ec_major_revision = ec_version >> 8;
	*ec_minor_revision = ec_version & 0xff;
}

/* Override smbios_system_manufacturer */
const char *smbios_system_manufacturer(void)
{
	return "Star Labs";
}

/* Override smbios_system_sku */
const char *smbios_system_sku(void)
{
#if CONFIG(BOARD_STARLABS_STARBOOK_TGL)
	return "B5";
#elif CONFIG(BOARD_STARLABS_LABTOP_CML)
	return "L4";
#else
	return "L3-U";
#endif
}

/* Override smbios_mainboard_features_flags */
u8 smbios_mainboard_feature_flags(void)
{
	return SMBIOS_FEATURE_FLAG_HOSTING_BOARD | SMBIOS_FEATURE_FLAG_REPLACEABLE;
}

/* Override smbios_mainboard_location_in_chassis */
const char *smbios_mainboard_location_in_chassis(void)
{
	return "Default";
}

/* Override smbios_mainboard_board_type */
smbios_board_type smbios_mainboard_board_type(void)
{
	return SMBIOS_BOARD_TYPE_MOTHERBOARD;
}

/* Override smbios_mainboard_asset_tag */
const char *smbios_mainboard_asset_tag(void)
{
	return "Default";
}

smbios_enclosure_type smbios_mainboard_enclosure_type(void)
{
	return SMBIOS_ENCLOSURE_NOTEBOOK;
}

/* Override smbios_chassis_version */
const char *smbios_chassis_version(void)
{
	return smbios_mainboard_version();
}

/* Override smbios_chassis_serial_number */
const char *smbios_chassis_serial_number(void)
{
	return smbios_mainboard_serial_number();
}

/* Override smbios_chassis_asset_tag */
const char *smbios_chassis_asset_tag(void)
{
	return CONFIG_MAINBOARD_SERIAL_NUMBER;
}

/* Override dev tree settings based on CMOS settings */
void devtree_update(void)
{
	/*
	 * usb2_ports[2] = Bluetooth
	 * usb2_ports[6] = Camera
	 * usb2_ports[9] = CNVi Bluetooth
	 */
	config_t *cfg = config_of_soc();

	if (get_uint_option("camera", 0) == 0)
		cfg->usb2_ports[6].enable = 0;

	if (get_uint_option("microphone", 0) == 0)
		/* Need to switch verb table */
		return;

#if CONFIG(BOARD_STARLABS_STARBOOK_TGL)
		struct device *nic = pcidev_on_root(0x1d, 5);
#elif CONFIG(BOARD_STARLABS_LABTOP_CML)
		struct device *nic = pcidev_on_root(0x14, 3);
#elif CONFIG(BOARD_STARLABS_LABTOP_KBL)
		struct device *nic = pcidev_on_root(0x1c, 5);
#endif

	if (get_uint_option("wireless", 0) == 0) {
                nic->enabled = 0;
	}

	struct soc_power_limits_config *soc_conf;
	soc_conf = &cfg->power_limits_config;

	/* Update PL2 based on CMOS settings */
	switch (get_uint_option("tdp", 0)) {
		case 0:
			soc_conf->tdp_pl2_override = 15;
			break;
		case 1:
			soc_conf->tdp_pl2_override = 20;
			break;
		case 2:
			soc_conf->tdp_pl2_override = 25;
			break;
		default:
			return;
	}
}
