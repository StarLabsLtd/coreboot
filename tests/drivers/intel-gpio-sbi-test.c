/* SPDX-License-Identifier: GPL-2.0-only */

#include <intelblocks/gpio.h>
#include <intelblocks/gpio_defs.h>
#include <intelblocks/p2sb.h>
#include <intelblocks/pcr.h>
#include <soc/pci_devs.h>
#include <tests/test.h>

#if CONFIG(SECURITY_FAIL_CLOSED)
#include <setjmp.h>
#endif

#define TEST_PAD 5
#define PAD_CFG_OFFSET 0x700

static const struct pad_group group = {
	.first_pad = 0,
	.size = 24,
	.acpi_pad_base = PAD_BASE_NONE,
};

static const struct pad_community community = {
	.first_pad = 0,
	.last_pad = 23,
	.pad_cfg_base = PAD_CFG_OFFSET,
	.port = 1,
	.groups = &group,
	.num_groups = 1,
};

static bool initially_hidden;
static bool unhide_success;
static bool hide_success;
static unsigned int unhide_calls;
static unsigned int hide_calls;
static unsigned int sbi_calls;
static unsigned int fail_sbi_call;
static uint32_t initial_data;
static uint32_t written_data;
static bool verification_mismatch;

#if CONFIG(SECURITY_FAIL_CLOSED)
static jmp_buf die_target;

void die(const char *msg, ...)
{
	longjmp(die_target, 1);
}
#endif

const struct pad_community *soc_gpio_get_community(size_t *count)
{
	*count = 1;
	return &community;
}

bool p2sb_dev_is_hidden(pci_devfn_t dev)
{
	assert_int_equal(dev, PCH_DEV_P2SB);
	return initially_hidden;
}

bool p2sb_dev_unhide(pci_devfn_t dev)
{
	assert_int_equal(dev, PCH_DEV_P2SB);
	unhide_calls++;
	return unhide_success;
}

bool p2sb_dev_hide(pci_devfn_t dev)
{
	assert_int_equal(dev, PCH_DEV_P2SB);
	hide_calls++;
	return hide_success;
}

int pcr_execute_sideband_msg(pci_devfn_t dev, struct pcr_sbi_msg *msg,
			     uint32_t *data, uint8_t *response)
{
	assert_int_equal(dev, PCH_DEV_P2SB);
	assert_int_equal(msg->offset, PAD_CFG_OFFSET + TEST_PAD * GPIO_NUM_PAD_CFG_REGS * 4);
	sbi_calls++;
	if (sbi_calls == fail_sbi_call)
		return -1;

	if (msg->opcode == PCR_WRITE) {
		written_data = *data;
	} else if (sbi_calls == 1) {
		*data = initial_data;
	} else {
		*data = verification_mismatch ? written_data ^ PAD_CFG0_TX_STATE : written_data;
	}
	*response = 0;
	return 0;
}

static int setup(void **state)
{
	initially_hidden = true;
	unhide_success = true;
	hide_success = true;
	unhide_calls = 0;
	hide_calls = 0;
	sbi_calls = 0;
	fail_sbi_call = 0;
	initial_data = 0xa5a50000;
	written_data = 0;
	verification_mismatch = false;
	return 0;
}

static void get_zero_and_one(void **state)
{
	int value = -1;

	assert_int_equal(gpio_sbi_get_tx(TEST_PAD, &value), 0);
	assert_int_equal(value, 0);
	assert_int_equal(sbi_calls, 1);
	assert_int_equal(unhide_calls, 1);
	assert_int_equal(hide_calls, 1);

	setup(NULL);
	initial_data |= PAD_CFG0_TX_STATE;
	assert_int_equal(gpio_sbi_get_tx(TEST_PAD, &value), 0);
	assert_int_equal(value, 1);
}

static void get_failure_still_rehides(void **state)
{
	int value = 7;

	fail_sbi_call = 1;
	assert_int_equal(gpio_sbi_get_tx(TEST_PAD, &value), -1);
	assert_int_equal(value, 7);
	assert_int_equal(hide_calls, 1);
}

static void set_changes_only_tx_and_verifies(void **state)
{
	assert_int_equal(gpio_sbi_set_tx(TEST_PAD, 1), 0);
	assert_int_equal(written_data, initial_data | PAD_CFG0_TX_STATE);
	assert_int_equal(sbi_calls, 3);

	setup(NULL);
	initial_data |= PAD_CFG0_TX_STATE;
	assert_int_equal(gpio_sbi_set_tx(TEST_PAD, 0), 0);
	assert_int_equal(written_data, initial_data & ~PAD_CFG0_TX_STATE);
}

static void each_set_stage_failure_is_reported(void **state)
{
	for (unsigned int stage = 1; stage <= 3; stage++) {
		setup(NULL);
		fail_sbi_call = stage;
		assert_int_equal(gpio_sbi_set_tx(TEST_PAD, 1), -1);
		assert_int_equal(sbi_calls, stage);
		assert_int_equal(hide_calls, 1);
	}

	setup(NULL);
	verification_mismatch = true;
	assert_int_equal(gpio_sbi_set_tx(TEST_PAD, 1), -1);
	assert_int_equal(hide_calls, 1);
}

static void visibility_failures_are_reported(void **state)
{
	unhide_success = false;
	assert_int_equal(gpio_sbi_set_tx(TEST_PAD, 1), -1);
	assert_int_equal(sbi_calls, 0);
	assert_int_equal(hide_calls, 0);

#if !CONFIG(SECURITY_FAIL_CLOSED)
	setup(NULL);
	hide_success = false;
	assert_int_equal(gpio_sbi_set_tx(TEST_PAD, 1), -1);
	assert_int_equal(sbi_calls, 3);
	assert_int_equal(hide_calls, 1);
#endif
}

#if CONFIG(SECURITY_FAIL_CLOSED)
static void rehide_failure_enforces_policy(void **state)
{
	hide_success = false;
	if (!setjmp(die_target)) {
		gpio_sbi_set_tx(TEST_PAD, 1);
		fail_msg("fail-closed GPIO access returned");
	}
	assert_int_equal(sbi_calls, 3);
	assert_int_equal(hide_calls, 1);
}
#endif

static void already_visible_is_not_toggled(void **state)
{
	initially_hidden = false;
	assert_int_equal(gpio_sbi_set_tx(TEST_PAD, 1), 0);
	assert_int_equal(unhide_calls, 0);
	assert_int_equal(hide_calls, 0);
}

static void invalid_arguments_do_not_touch_hardware(void **state)
{
	int value;

	assert_int_equal(gpio_sbi_get_tx(UINT32_MAX, &value), -1);
	assert_int_equal(gpio_sbi_get_tx(TEST_PAD, NULL), -1);
	assert_int_equal(gpio_sbi_set_tx(UINT32_MAX, 1), -1);
	assert_int_equal(sbi_calls, 0);
	assert_int_equal(unhide_calls, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(get_zero_and_one, setup),
		cmocka_unit_test_setup(get_failure_still_rehides, setup),
		cmocka_unit_test_setup(set_changes_only_tx_and_verifies, setup),
		cmocka_unit_test_setup(each_set_stage_failure_is_reported, setup),
		cmocka_unit_test_setup(visibility_failures_are_reported, setup),
		cmocka_unit_test_setup(already_visible_is_not_toggled, setup),
		cmocka_unit_test_setup(invalid_arguments_do_not_touch_hardware, setup),
#if CONFIG(SECURITY_FAIL_CLOSED)
		cmocka_unit_test_setup(rehide_failure_enforces_policy, setup),
#endif
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
