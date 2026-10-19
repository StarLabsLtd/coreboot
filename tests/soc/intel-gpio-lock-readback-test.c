/* SPDX-License-Identifier: GPL-2.0-only */

#include <intelblocks/gpio.h>
#include <intelblocks/gpio_defs.h>
#include <intelblocks/p2sb.h>
#include <intelblocks/pcr.h>
#include <string.h>
#include <tests/test.h>

#define TEST_PAD        5
#define PAD_CFG_OFFSET  0x700
#define PAD_LOCK_OFFSET 0x80
#define PAD_BIT         BIT(TEST_PAD)

static const struct pad_group group = {
	.first_pad = 0,
	.size = 24,
	.acpi_pad_base = PAD_BASE_NONE,
};

static const struct pad_community community = {
	.name = "test",
	.first_pad = 0,
	.last_pad = 23,
	.pad_cfg_base = PAD_CFG_OFFSET,
	.pad_cfg_lock_offset = PAD_LOCK_OFFSET,
	.port = 1,
	.groups = &group,
	.num_groups = 1,
};

static uint32_t pad_cfg;
static uint32_t config_lock;
static uint32_t tx_lock;
static bool ignore_config_lock;
static bool ignore_tx_lock;

const struct pad_community *soc_gpio_get_community(size_t *count)
{
	*count = 1;
	return &community;
}

uint32_t pcr_read32(uint8_t pid, uint16_t offset)
{
	if (offset == PAD_CFG_OFFSET + TEST_PAD * GPIO_NUM_PAD_CFG_REGS * sizeof(uint32_t))
		return pad_cfg;
	if (offset == PAD_LOCK_OFFSET)
		return config_lock;
	if (offset == PAD_LOCK_OFFSET + sizeof(uint32_t))
		return tx_lock;
	return 0;
}

void pcr_or32(uint8_t pid, uint16_t offset, uint32_t value)
{
	if (offset == PAD_LOCK_OFFSET && !ignore_config_lock)
		config_lock |= value;
	if (offset == PAD_LOCK_OFFSET + sizeof(uint32_t) && !ignore_tx_lock)
		tx_lock |= value;
}

bool p2sb_unhide(void)
{
	return true;
}

bool p2sb_hide(void)
{
	return true;
}

static int setup(void **state)
{
	pad_cfg = PAD_CFG0_MODE_NF1;
	config_lock = 0;
	tx_lock = 0;
	ignore_config_lock = false;
	ignore_tx_lock = false;
	return 0;
}

static int lock_pad(uint32_t expected_mode)
{
	const struct gpio_lock_config pad = {
		.pad = TEST_PAD,
		.lock_action = GPIO_LOCK_FULL,
		.expected_mode = expected_mode,
	};

	return gpio_lock_pads(&pad, 1);
}

static void valid_readback_succeeds(void **state)
{
	assert_int_equal(lock_pad(PAD_CFG0_MODE_NF1), 0);
	assert_true(config_lock & PAD_BIT);
	assert_true(tx_lock & PAD_BIT);
}

static void wrong_native_mode_fails(void **state)
{
	pad_cfg = 0;
	assert_int_equal(lock_pad(PAD_CFG0_MODE_NF1), -1);
}

static void unavailable_uniform_readback_fails_mode_check(void **state)
{
	pad_cfg = UINT32_MAX;
	config_lock = UINT32_MAX;
	tx_lock = UINT32_MAX;
	assert_int_equal(lock_pad(PAD_CFG0_MODE_NF1), -1);
}

static void missing_configuration_lock_fails(void **state)
{
	ignore_config_lock = true;
	assert_int_equal(lock_pad(PAD_CFG0_MODE_NF1), -1);
}

static void missing_tx_lock_fails(void **state)
{
	ignore_tx_lock = true;
	assert_int_equal(lock_pad(PAD_CFG0_MODE_NF1), -1);
}

static void all_ones_lock_registers_can_be_valid(void **state)
{
	config_lock = UINT32_MAX;
	tx_lock = UINT32_MAX;
	assert_int_equal(lock_pad(PAD_CFG0_MODE_NF1), 0);
}

static void no_expected_mode_skips_native_mode_check(void **state)
{
	pad_cfg = UINT32_MAX;
	config_lock = UINT32_MAX;
	tx_lock = UINT32_MAX;
	assert_int_equal(lock_pad(0), 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(valid_readback_succeeds, setup),
		cmocka_unit_test_setup(wrong_native_mode_fails, setup),
		cmocka_unit_test_setup(unavailable_uniform_readback_fails_mode_check, setup),
		cmocka_unit_test_setup(missing_configuration_lock_fails, setup),
		cmocka_unit_test_setup(missing_tx_lock_fails, setup),
		cmocka_unit_test_setup(all_ones_lock_registers_can_be_valid, setup),
		cmocka_unit_test_setup(no_expected_mode_skips_native_mode_check, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
