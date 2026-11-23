/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/rapl_options.h>
#include <tests/test.h>

unsigned int get_uint_option(const char *name, unsigned int fallback)
{
	check_expected(name);
	return mock_type(unsigned int);
}

static void expect_option(const char *name, unsigned int value)
{
	expect_string(get_uint_option, name, name);
	will_return(get_uint_option, value);
}

static void pl1(unsigned int stored, unsigned int expected)
{
	expect_option("tdp_pl1_override", stored);
	assert_int_equal(get_rapl_pl1_override(0), expected);
}

static void pl2(unsigned int stored, unsigned int expected)
{
	expect_option("tdp_pl2_override", stored);
	assert_int_equal(get_rapl_pl2_override(0), expected);
}

static void test_board_limits(void **state)
{
	pl1(0, 0);
	pl2(0, 0);

	if (CONFIG(BOARD_DELL_PRECISION_M4800)) {
		pl1(36, 0);
		pl1(37, 37);
		pl1(47, 47);
		pl1(57, 57);
		pl1(58, 0);
		pl2(36, 0);
		pl2(37, 37);
		pl2(59, 59);
		pl2(4095, 4095);
		pl2(4096, 4096);
		pl2(4097, 0);
		assert_true(rapl_pl2_disabled(4096));
	} else if (CONFIG(BOARD_GOOGLE_PANTHER)) {
		pl1(14, 0);
		pl1(15, 15);
		pl1(25, 25);
		pl1(26, 0);
		pl2(14, 0);
		pl2(15, 15);
		pl2(30, 30);
		pl2(31, 0);
		assert_false(rapl_pl2_disabled(4096));
	} else {
		pl1(130, 130);
		pl2(425, 425);
		assert_false(rapl_pl2_disabled(4096));
	}

	expect_option("tdp_pl2_override", 0);
	assert_int_equal(get_rapl_pl2_override(25), 0);
	assert_false(rapl_pl2_disabled(0));
}

static void test_power_encoding(void **state)
{
	assert_int_equal(rapl_encode_watts(0, 8, 0x7fff, 400), 400);
	assert_int_equal(rapl_encode_watts(47, 8, 0x7fff, 400), 376);
	assert_int_equal(rapl_encode_watts(4095, 8, 0x7fff, 400), 32760);
	assert_int_equal(rapl_encode_watts(4096, 8, 0x7fff, 400), 400);
	assert_int_equal(rapl_encode_watts(UINT_MAX, 8, 0x7fff, 400), 400);
	assert_int_equal(rapl_encode_watts(1, 1, 0x7fff, 400), 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_board_limits),
		cmocka_unit_test(test_power_encoding),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
