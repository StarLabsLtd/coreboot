/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/save_state.h>
#include <cpu/x86/smm.h>
#include <intelblocks/gpio.h>
#include <intelblocks/smihandler.h>
#include <string.h>
#include <tests/test.h>

static unsigned int board_calls;
static unsigned int gpio_calls;
static unsigned int gpio_finalize_calls;
static unsigned int soc_calls;
static uint32_t caller_result;

int mainboard_smi_finalize(void)
{
	board_calls++;
	return CONFIG_TEST_BOARD_FAILURE ? -1 : 0;
}

int smihandler_soc_at_finalize(void)
{
	soc_calls++;
	return CONFIG_TEST_SOC_FAILURE ? -1 : 0;
}

const struct gpio_lock_config *soc_gpio_lock_config(size_t *num)
{
	static const struct gpio_lock_config pad = {
		.pad = 1,
		.lock_action = GPIO_LOCK_CONFIG,
	};

	*num = 1;
	return &pad;
}

int gpio_lock_pads(const struct gpio_lock_config *pad_list, size_t count)
{
	gpio_calls++;
	return CONFIG_TEST_GPIO_FAILURE ? -1 : 0;
}

int soc_gpio_lock_finalize(void)
{
	gpio_finalize_calls++;
	return 0;
}

bool enable_smm_bios_protection(void)
{
	return false;
}

bool opal_s3_smi_apmc(uint8_t command)
{
	return true;
}

uint8_t apm_get_apmc(void)
{
	return APM_CNT_FINALIZE;
}

void pmc_disable_pm1_control(uint32_t mask)
{
}

void pmc_enable_pm1_control(uint32_t mask)
{
}

int mainboard_smi_apmc(uint8_t command)
{
	return 0;
}

static int apmc_node(uint8_t command)
{
	return 0;
}

static int set_reg(enum cpu_reg reg, int node, void *value, uint8_t length)
{
	assert_int_equal(reg, RAX);
	assert_int_equal(node, 0);
	assert_int_equal(length, sizeof(caller_result));
	memcpy(&caller_result, value, sizeof(caller_result));
	return 0;
}

static const struct smm_save_state_ops save_state_ops = {
	.apmc_node = apmc_node,
	.set_reg = set_reg,
};

static void failure_reaches_caller_and_is_cached(void **state)
{
	smihandler_southbridge_apmc(&save_state_ops);
	assert_int_equal(caller_result, UINT32_MAX);
	assert_int_equal(board_calls, 1);
	assert_int_equal(soc_calls, 1);

	if (CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS)) {
		assert_int_equal(gpio_calls, 1);
		assert_int_equal(gpio_finalize_calls, 1);
	} else {
		assert_int_equal(gpio_calls, 0);
		assert_int_equal(gpio_finalize_calls, 0);
	}

	caller_result = 0;
	smihandler_southbridge_apmc(&save_state_ops);
	assert_int_equal(caller_result, UINT32_MAX);
	assert_int_equal(board_calls, 1);
	assert_int_equal(soc_calls, 1);
	assert_int_equal(gpio_calls, CONFIG(SOC_INTEL_COMMON_BLOCK_SMM_LOCK_GPIO_PADS) ? 1 : 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(failure_reaches_caller_and_is_cached),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
