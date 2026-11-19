/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <delay.h>
#include <gpio.h>

#if CONFIG(SOC_INTEL_METEORLAKE)
#include "../../../src/mainboard/starlabs/starfighter/variants/mtl/board_id.c"
static const gpio_t strap_pads[] = { GPP_B05, GPP_B06, GPP_B07, GPP_B08 };
#else
#include "../../../src/mainboard/starlabs/starfighter/variants/rpl/romstage.c"
static const gpio_t strap_pads[] = { GPP_F15, GPP_F14, GPP_F13, GPP_F12 };
#endif

int gpio_get(gpio_t gpio)
{
	check_expected(gpio);
	return mock_type(int);
}

void gpio_input(gpio_t gpio)
{
	fail_msg("Reading configured straps must not change the pad configuration");
}

void gpio_configure_pads(const struct pad_config *pads, size_t num_pads)
{
	fail_msg("Reading configured straps must not change the pad configuration");
}

void udelay(unsigned int usecs)
{
}

static void test_all_strap_values(void **state)
{
	for (unsigned int value = 0; value < 16; value++) {
		for (size_t i = 0; i < ARRAY_SIZE(strap_pads); i++) {
			expect_value(gpio_get, gpio, strap_pads[i]);
			will_return(gpio_get, (value >> i) & 1);
		}
		assert_int_equal(get_memory_config_straps(), value);
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_all_strap_values),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
