/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <security/vboot/secdata_tpm.c>

static tpm_result_t lock_status;

tpm_result_t tlcl2_lock_nv_write(uint32_t index)
{
	function_called();
	assert_int_equal(index, FIRMWARE_NV_INDEX);
	return lock_status;
}

tpm_result_t tlcl2_disable_platform_hierarchy(void)
{
	/* DAVE closes PlatformAuth itself after finishing its protected operations. */
	fail();
	return TPM_CB_FAIL;
}

static void firmware_index_locked(void **state)
{
	lock_status = TPM_SUCCESS;
	expect_function_call(tlcl2_lock_nv_write);
	assert_int_equal(antirollback_lock_space_firmware(), TPM_SUCCESS);
}

static void lock_failure_propagates(void **state)
{
	lock_status = TPM_CB_COMMUNICATION_ERROR;
	expect_function_call(tlcl2_lock_nv_write);
	assert_int_equal(antirollback_lock_space_firmware(), lock_status);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(firmware_index_locked),
		cmocka_unit_test(lock_failure_propagates),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
