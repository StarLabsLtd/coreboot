/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <console/console.h>
#include <stdint.h>
#include <string.h>
#include <uuid.h>
#include "firmware_info.h"

static uint32_t efi_fw_version_from_localversion(void)
{
	const char *localversion = CONFIG_LOCALVERSION;
	uint32_t major = 0;
	uint32_t minor = 0;
	const char *p = localversion;

	while (*p != '\0' && (*p < '0' || *p > '9'))
		p++;

	while (*p >= '0' && *p <= '9') {
		if (major > (0xffffU - (*p - '0')) / 10U)
			return 0;
		major = major * 10 + (*p - '0');
		p++;
	}

	if (*p != '.')
		return 0;
	p++;

	while (*p >= '0' && *p <= '9') {
		if (minor > (0xffffU - (*p - '0')) / 10U)
			return 0;
		minor = minor * 10 + (*p - '0');
		p++;
	}

	if (major > 0xffff || minor > 0xffff)
		return 0;

	return (major << 16) | minor;
}

bool efi_firmware_info(struct lb_efi_fw_info *fw_info)
{
	uint8_t guid[16];
	uint32_t fw_version;
	uint32_t lsv;

	if (parse_uuid(guid, CONFIG_DRIVERS_EFI_MAIN_FW_GUID)) {
		printk(BIOS_ERR, "%s(): failed to parse firmware's GUID: '%s'\n", __func__,
		       CONFIG_DRIVERS_EFI_MAIN_FW_GUID);
		return false;
	}

	memset(fw_info, 0, sizeof(*fw_info));
	fw_info->tag = LB_TAG_EFI_FW_INFO;
	fw_info->size = sizeof(*fw_info);

	memcpy(fw_info->guid, guid, sizeof(guid));

	fw_version = CONFIG_DRIVERS_EFI_MAIN_FW_VERSION;
	if (fw_version == 0) {
		fw_version = efi_fw_version_from_localversion();
		if (fw_version != 0)
			printk(BIOS_DEBUG,
			       "EFI FW version derived from CONFIG_LOCALVERSION '%s': 0x%08x\n",
			       CONFIG_LOCALVERSION, fw_version);
	}

	lsv = CONFIG_DRIVERS_EFI_MAIN_FW_LSV;
	if (lsv == 0)
		lsv = fw_version;

	fw_info->version = fw_version;
	fw_info->lowest_supported_version = lsv;
	fw_info->fw_size = CONFIG_ROM_SIZE;
	return true;
}

#if ENV_RAMSTAGE
void lb_efi_fw_info(struct lb_header *header)
{
	struct lb_efi_fw_info info;

	if (efi_firmware_info(&info))
		memcpy(lb_new_record(header), &info, sizeof(info));
}
#endif
