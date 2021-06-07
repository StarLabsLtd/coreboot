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
#include <device/azalia_device.h>

#if CONFIG(BOARD_STARLABS_STARBOOK_TGL)
#include <ec/starlabs/it5570/ec.h>
#else
#include <ec/starlabs/it8987/ec.h>
#endif

#define CODEC_ALC256 0x10ec0256
#define CODEC_ALC269 0x10ec0269

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

/* Override dev tree settings based on CMOS settings */
void devtree_update(void)
{
	config_t *cfg = config_of_soc();

	if (get_uint_option("camera", 0) == 0)
		cfg->usb2_ports[6].enable = 0;

/* TODO: Remove preprocessor... */
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
	/* TODO: TGL needs power_limits_config[POWER_LIMITS_U_2_CORE] and power_limits_config[POWER_LIMITS_U_4_CORE] */
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

/* TODO: Fix */
void mainboard_azalia_program_runtime_verbs(u8 *base, u32 viddid)
{
	uint8_t microphone = get_uint_option("microphone", 0xff);
	printk(BIOS_DEBUG, "CMOS: microphone = %d\n", microphone);
	/* if ((viddid == CODEC_ALC256) || (viddid == CODEC_ALC269)) { */
		/* if (get_uint_option("microphone", 0)) */
        printk(BIOS_DEBUG, "CMOS: Disabling microphone\n");
	disable_microphone(base);
        /* } */
}

const u32 override_verb[] = {
	AZALIA_PIN_CFG(0, 0x12, 0x411111f0),
};

void disable_microphone(u8 *base)
{
	/* azalia_program_verb_table(base, cim_verb_data, ARRAY_SIZE(cim_verb_data)); */
	azalia_program_verb_table(base, override_verb, ARRAY_SIZE(override_verb));
	printk(BIOS_DEBUG, "CMOS: Disable attempted\n");
}

