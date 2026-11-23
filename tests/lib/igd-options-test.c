/* SPDX-License-Identifier: GPL-2.0-only */

#include <intelblocks/igd_options.h>
#include <tests/test.h>

unsigned int get_uint_option(const char *name, unsigned int fallback)
{
	assert_string_equal(name, "igd_dvmt_prealloc");
	return mock_type(unsigned int);
}

static void test_dvmt_options(void **state)
{
	const unsigned int values[] = { 0, 5, 6, 0xef, 0xf0, 0xf7, 0xfe, 0xff, UINT_MAX };
	const unsigned int expected[] = { 0, 5, 2, 2, 0xf0, 0xf7, 0xfe, 2, 2 };

	for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
		will_return(get_uint_option, values[i]);
		assert_int_equal(get_igd_dvmt_option(2, 5), expected[i]);
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_dvmt_options),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
