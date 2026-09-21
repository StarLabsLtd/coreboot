/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/coreboot_tables.h>
#include <string.h>

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!value)
		__builtin_trap();
}

int parse_uuid(uint8_t *uuid, const char *text)
{
	if (!strcmp(text, "bad"))
		return -1;
	memset(uuid, 0x5a, 16);
	return 0;
}

int main(void)
{
	struct lb_efi_fw_info info;
	enum cb_err status;

	memset(&info, 0xa5, sizeof(info));
	status = efi_fw_info_get(&info);
#if TEST_INVALID
	assert(status == CB_ERR);
	for (size_t i = 0; i < sizeof(info); i++)
		assert(((uint8_t *)&info)[i] == 0xa5);
#else
	assert(status == CB_SUCCESS);
	assert(info.tag == LB_TAG_EFI_FW_INFO);
	assert(info.size == sizeof(info));
	assert(info.version == TEST_VERSION);
	assert(info.lowest_supported_version == TEST_LSV);
	assert(info.fw_size == CONFIG_ROM_SIZE);
	assert(info.guid[0] == 0x5a);
#endif
	return 0;
}
