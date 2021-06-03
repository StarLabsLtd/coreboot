/* SPDX-License-Identifier: GPL-2.0-only */

#include <smbios.h>
#include <types.h>
#include <uuid.h>
#include <option.h>
#include <console/console.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <types.h>

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

static void device_by_cmos(void)
{
	u8 wireless_state = get_uint_option("wireless", 0xff);
        printk(BIOS_DEBUG, "CMOS: wireless = %d\n", wireless_state);
        if (wireless_state == 0) {
                struct device *wireless = pcidev_on_root(0x14, 3);
                if (wireless) {
                        printk(BIOS_DEBUG, "Disabling wireless!\n");
                        wireless->enabled = 0;
                }
        }
}

static void mainboard_enable(struct device *dev)
{
        device_by_cmos();
}

struct chip_operations mainboard_ops = {
	.enable_dev = mainboard_enable,
};
