/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include "../../../src/mainboard/prodrive/atlas/option_values.h"

unsigned int get_uint_option(const char *name, unsigned int fallback)
{
	check_expected(name);
	return mock_type(unsigned int);
}

static void check_option(unsigned int (*read)(void), const char *name, unsigned int stored,
			 unsigned int expected)
{
	expect_string(get_uint_option, name, name);
	will_return(get_uint_option, stored);
	assert_int_equal(read(), expected);
}

static void test_c_state_limit(void **state)
{
	check_option(atlas_pkg_c_state_limit, "pkg_c_state_limit", 8, 8);
	check_option(atlas_pkg_c_state_limit, "pkg_c_state_limit", 9, 255);
	check_option(atlas_pkg_c_state_limit, "pkg_c_state_limit", 253, 255);
	check_option(atlas_pkg_c_state_limit, "pkg_c_state_limit", 254, 254);
	check_option(atlas_pkg_c_state_limit, "pkg_c_state_limit", 255, 255);
	check_option(atlas_pkg_c_state_limit, "pkg_c_state_limit", UINT_MAX, 255);
}

static void test_pcie_ssc(void **state)
{
	check_option(atlas_pcie_ssc, "pch_pcie_pll_ssc", 0, 0);
	check_option(atlas_pcie_ssc, "pch_pcie_pll_ssc", 20, 20);
	check_option(atlas_pcie_ssc, "pch_pcie_pll_ssc", 21, 255);
	check_option(atlas_pcie_ssc, "pch_pcie_pll_ssc", 255, 255);
}

static void test_primary_display(void **state)
{
	check_option(atlas_primary_display, "primary_display", 0, 0);
	check_option(atlas_primary_display, "primary_display", 3, 3);
	check_option(atlas_primary_display, "primary_display", 4, 4);
	check_option(atlas_primary_display, "primary_display", 5, 3);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_c_state_limit),
		cmocka_unit_test(test_pcie_ssc),
		cmocka_unit_test(test_primary_display),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
