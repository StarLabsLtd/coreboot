/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <string.h>
#include <uuid.h>

static uint32_t version_from_localversion(void)
{
	const char *p = CONFIG_LOCALVERSION;
	uint32_t major = 0;
	uint32_t minor = 0;

	while (*p && (*p < '0' || *p > '9'))
		p++;
	while (*p >= '0' && *p <= '9') {
		major = major * 10 + (uint32_t)(*p - '0');
		p++;
	}
	if (*p++ != '.')
		return 0;
	while (*p >= '0' && *p <= '9') {
		minor = minor * 10 + (uint32_t)(*p - '0');
		p++;
	}
	return major <= UINT16_MAX && minor <= UINT16_MAX ?
		major << 16 | minor : 0;
}

enum cb_err efi_fw_info_get(struct lb_efi_fw_info *info)
{
	struct lb_efi_fw_info result = {
		.tag = LB_TAG_EFI_FW_INFO,
		.size = sizeof(result),
		.version = CONFIG_DRIVERS_EFI_MAIN_FW_VERSION,
		.lowest_supported_version = CONFIG_DRIVERS_EFI_MAIN_FW_LSV,
		.fw_size = CONFIG_ROM_SIZE,
	};

	if (!info || parse_uuid(result.guid, CONFIG_DRIVERS_EFI_MAIN_FW_GUID))
		return CB_ERR;
	if (!result.version)
		result.version = version_from_localversion();
	if (!result.lowest_supported_version)
		result.lowest_supported_version = result.version;
	if (result.lowest_supported_version > result.version || !result.fw_size)
		return CB_ERR;
	*info = result;
	return CB_SUCCESS;
}
