/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/endian.h>
#include <boot/payload_mm_authvar_format.h>
#include <tests/test.h>

static int count_lists(const void *data, size_t size)
{
	struct payload_mm_authvar_signature_list_cursor cursor;
	struct payload_mm_authvar_signature_list_view list;
	enum payload_mm_authvar_format_result result;
	int count = 0;

	assert_true(payload_mm_authvar_signature_list_begin(&cursor, data, size));
	while ((result = payload_mm_authvar_signature_list_next(&cursor, &list)) ==
	       PAYLOAD_MM_AUTHVAR_FORMAT_OK)
		count++;
	return result == PAYLOAD_MM_AUTHVAR_FORMAT_DONE ? count : -1;
}

static void signature_list_framing(void **state)
{
	uint8_t data[2 * 45 + 1] = {
		0, 0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
		0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72,
		45, 0, 0, 0, 0, 0, 0, 0, 17,
	};
	uint8_t *list = data + 1;

	assert_int_equal(count_lists(list, 45), 1);
	assert_int_equal(count_lists(list, 27), -1);
	assert_int_equal(count_lists(list, 44), -1);
	assert_int_equal(count_lists(list, 46), -1);
	write_le32(list + 20, UINT32_MAX);
	assert_int_equal(count_lists(list, 45), -1);
	write_le32(list + 20, 0);
	write_le32(list + 24, 0);
	assert_int_equal(count_lists(list, 45), -1);
	write_le32(list + 24, 16);
	assert_int_equal(count_lists(list, 45), -1);
	write_le32(list + 16, 28);
	assert_int_equal(count_lists(list, 28), 1);
	write_le32(list + 16, 45);
	write_le32(list + 24, 17);
	memcpy(list + 45, list, 45);
	assert_int_equal(count_lists(list, 90), 2);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(signature_list_framing),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
