/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#define main coreboot_main
#include <bootstate.h>
#undef main
#include <ec/starlabs/merlin/ec.h>
#include <gpio.h>
#include <variants.h>

extern struct boot_state_init_entry *test_bootstate_begin[];
extern struct boot_state_init_entry *test_bootstate_end[];

static const struct pad_config early_pads[] = {
	PAD_NC_LOCK(GPP_H10, NONE, LOCK_CONFIG),
	PAD_CFG_GPO(GPP_H0, 0, PLTRST),
	PAD_CFG_GPI_LOCK(GPP_F12, UP_20K, LOCK_CONFIG),
	PAD_CFG_GPO(GPP_D16, 1, PLTRST),
};

static const struct pad_config unlocked_early_pads[] = {
	PAD_CFG_NF(GPP_H10, NONE, DEEP, NF2),
	PAD_CFG_GPO(GPP_H0, 0, PLTRST),
};

static const struct pad_config full_pads[] = {
	PAD_CFG_GPO(GPP_H0, 1, PLTRST),
	PAD_CFG_GPO(GPP_D16, 1, PLTRST),
	PAD_CFG_GPI_APIC_LOW(GPP_E12, NONE, PLTRST),
};

void merlin_fill_ssdt(const struct device *dev)
{
	fail_msg("GPIO initialization must not generate ACPI tables");
}

const struct pad_config *variant_early_gpio_table(size_t *num)
{
	function_called();
	*num = mock_type(size_t);
	return mock_ptr_type(const struct pad_config *);
}

const struct pad_config *variant_gpio_table(size_t *num)
{
	function_called();
	*num = ARRAY_SIZE(full_pads);
	return full_pads;
}

void gpio_configure_pads(const struct pad_config *pads, size_t num_pads)
{
	function_called();
	check_expected(num_pads);
	check_expected(pads);
}

static void expect_configure(const struct pad_config *pads, size_t count)
{
	expect_function_call(gpio_configure_pads);
	expect_value(gpio_configure_pads, num_pads, count);
	expect_memory(gpio_configure_pads, pads, pads, count * sizeof(*pads));
}

static void run_gpio_init(void)
{
	assert_int_equal(test_bootstate_end - test_bootstate_begin, 1);
	struct boot_state_init_entry *entry = test_bootstate_begin[0];

	/* FSP-S runs later, while initializing the SoC chip. */
	assert_int_equal(entry->state, BS_PRE_DEVICE);
	assert_int_equal(entry->when, BS_ON_ENTRY);
	assert_non_null(entry->bscb.callback);
	entry->bscb.callback(entry->bscb.arg);
}

static void test_lock_marked_pads_before_full_table(void **state)
{
	if (CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS)) {
		expect_function_call(variant_early_gpio_table);
		will_return(variant_early_gpio_table, ARRAY_SIZE(early_pads));
		will_return(variant_early_gpio_table, early_pads);
		expect_configure(&early_pads[0], 1);
		expect_configure(&early_pads[2], 1);
	}
	expect_function_call(variant_gpio_table);
	expect_configure(full_pads, ARRAY_SIZE(full_pads));
	run_gpio_init();
}

static void test_unlocked_early_pads_are_not_replayed(void **state)
{
	if (CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS)) {
		expect_function_call(variant_early_gpio_table);
		will_return(variant_early_gpio_table, ARRAY_SIZE(unlocked_early_pads));
		will_return(variant_early_gpio_table, unlocked_early_pads);
	}
	expect_function_call(variant_gpio_table);
	expect_configure(full_pads, ARRAY_SIZE(full_pads));
	run_gpio_init();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_lock_marked_pads_before_full_table),
		cmocka_unit_test(test_unlocked_early_pads_are_not_replayed),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
