/* SPDX-License-Identifier: GPL-2.0-only */

#include <intelblocks/smm_gpio.h>
#include <soc/gpio_soc_defs.h>
#include <tests/test.h>

static void expect_pad(gpio_t pad, bool allowed)
{
	assert_int_equal(mainboard_smm_gpio_allowed(pad), allowed);
	assert_int_equal(mainboard_acpi_gpio_uses_smm(pad), allowed);
}

static void actual_board_allowlist(void **state)
{
	expect_pad(GPP_B18, true);
	expect_pad(GPP_B19, true);

#ifdef TEST_STARFIGHTER
	expect_pad(GPP_B21, true);
	expect_pad(GPP_D22, true);
#else
	expect_pad(GPP_B21, false);
	expect_pad(GPP_D22, false);
#endif

	expect_pad(GPP_B13, false);
	expect_pad(UINT32_MAX, false);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(actual_board_allowlist),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
