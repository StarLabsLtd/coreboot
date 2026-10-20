/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <stdarg.h>
#include <setjmp.h>
#include <tests/test.h>

static uint32_t smm_result;
static jmp_buf die_target;
static bool die_expected;

uint32_t apm_control_call_smm(uint8_t cmd)
{
	return smm_result;
}

void die(const char *fmt, ...)
{
	assert_true(die_expected);
	longjmp(die_target, 1);
}

static void successful_finalize_succeeds(void **state)
{
	smm_result = 0;
	die_expected = false;
	assert_int_equal(apm_control(APM_CNT_FINALIZE), CB_SUCCESS);
}

static void failed_finalize_obeys_handler_contract(void **state)
{
	smm_result = 1;
	die_expected = CONFIG(SECURITY_FAIL_CLOSED) && CONFIG(SOC_INTEL_COMMON_BLOCK_SMM);

	if (die_expected) {
		if (!setjmp(die_target)) {
			apm_control(APM_CNT_FINALIZE);
			fail_msg("fail-closed finalization returned");
		}
		return;
	}

	assert_int_equal(apm_control(APM_CNT_FINALIZE),
			 CONFIG(SOC_INTEL_COMMON_BLOCK_SMM) ? CB_ERR : CB_SUCCESS);
}

static void other_commands_ignore_return_value(void **state)
{
	smm_result = 1;
	die_expected = false;
	assert_int_equal(apm_control(APM_CNT_ACPI_ENABLE), CB_SUCCESS);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(successful_finalize_succeeds),
		cmocka_unit_test(failed_finalize_obeys_handler_contract),
		cmocka_unit_test(other_commands_ignore_return_value),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
