/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <stdint.h>
#include <string.h>
#include <tests/test.h>

static struct lb_efi_fw_info record;

struct lb_record *lb_new_record(struct lb_header *header)
{
	(void)header;
	memset(&record, 0, sizeof(record));
	return (struct lb_record *)&record;
}

static void test_fw_info_without_capsule_policy(void **state)
{
	(void)state;
	struct lb_header header = { 0 };
	const uint8_t image_guid[] = {
		0xe6, 0xd0, 0x5c, 0x97, 0x40, 0xc5, 0x2b, 0x4e,
		0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d,
	};

	lb_efi_fw_info(&header);
	assert_int_equal(LB_TAG_EFI_FW_INFO, record.tag);
	assert_int_equal(sizeof(record), record.size);
	assert_memory_equal(image_guid, record.guid, sizeof(record.guid));
	assert_int_equal(0x001a0009, record.version);
	assert_int_equal(0x001a0008, record.lowest_supported_version);
	assert_int_equal(0x01000000, record.fw_size);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_fw_info_without_capsule_policy),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
