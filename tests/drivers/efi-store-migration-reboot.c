/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <cpu/x86/smi_trigger.c>

static u32 result;
static bool reset_called, die_called;

u32 apm_control_call_smm(u8 command)
{
	return result;
}

void board_reset(void)
{
	reset_called = true;
	mock_assert(false, "reset", __FILE__, __LINE__);
	__builtin_unreachable();
}

void die(const char *message, ...)
{
	die_called = true;
	mock_assert(false, "die", __FILE__, __LINE__);
	__builtin_unreachable();
}

static void reset_only_after_completed_finalization(void **unused)
{
	result = APM_FINALIZE_REBOOT_REQUIRED;
	reset_called = die_called = false;
	expect_assert_failure(apm_control(APM_CNT_FINALIZE));
	assert_true(reset_called);
	assert_false(die_called);
}

static void errors_do_not_become_a_reboot(void **unused)
{
	const u32 errors[] = { APM_FINALIZE_EFI_ERROR, APM_FINALIZE_OTHER_ERROR };

	for (size_t i = 0; i < ARRAY_SIZE(errors); i++) {
		result = APM_FINALIZE_REBOOT_REQUIRED | errors[i];
		reset_called = die_called = false;
		expect_assert_failure(apm_control(APM_CNT_FINALIZE));
		assert_false(reset_called);
		assert_true(die_called);
	}
}

static void normal_commands_do_not_reset(void **unused)
{
	result = 0;
	reset_called = die_called = false;
	assert_int_equal(apm_control(APM_CNT_FINALIZE), CB_SUCCESS);
	result = APM_FINALIZE_REBOOT_REQUIRED;
	assert_int_equal(apm_control(APM_CNT_NOOP_SMI), CB_SUCCESS);
	assert_false(reset_called);
	assert_false(die_called);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(reset_only_after_completed_finalization),
		cmocka_unit_test(errors_do_not_become_a_reboot),
		cmocka_unit_test(normal_commands_do_not_reset),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
