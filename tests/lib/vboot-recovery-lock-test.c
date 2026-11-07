/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <security/vboot/vboot_logic.c>

static struct vb2_context context;
static tpm_result_t lock_status;

void timestamp_add_now(enum timestamp_id id)
{
}

struct vb2_context *vboot_get_context(void)
{
	return &context;
}

void vbnv_init(void)
{
}

int platform_is_resuming(void)
{
	return 0;
}

int get_recovery_mode_switch(void)
{
	return 0;
}

int get_ec_is_trusted(void)
{
	return 1;
}

tpm_result_t vboot_setup_tpm(struct vb2_context *ctx)
{
	return TPM_SUCCESS;
}

tpm_result_t antirollback_read_space_firmware(struct vb2_context *ctx)
{
	return TPM_SUCCESS;
}

tpm_result_t antirollback_read_space_kernel(struct vb2_context *ctx)
{
	return TPM_SUCCESS;
}

void vb2api_fail(struct vb2_context *ctx, uint8_t reason, uint8_t subcode)
{
	fail();
}

vb2_error_t vb2api_fw_phase1(struct vb2_context *ctx)
{
	return VB2_ERROR_API_PHASE1_RECOVERY;
}

vb2_error_t vb2api_fw_phase2(struct vb2_context *ctx)
{
	fail();
	return VB2_ERROR_UNKNOWN;
}

vb2_error_t vb2api_fw_phase3(struct vb2_context *ctx)
{
	fail();
	return VB2_ERROR_UNKNOWN;
}

vb2_error_t vb2api_get_metadata_hash(struct vb2_context *ctx, struct vb2_hash **hash)
{
	fail();
	return VB2_ERROR_UNKNOWN;
}

void vboot_save_and_reboot(struct vb2_context *ctx, uint8_t subcode)
{
	fail();
	__builtin_unreachable();
}

void vboot_fail_and_reboot(struct vb2_context *ctx, uint8_t reason, uint8_t subcode)
{
	fail();
	__builtin_unreachable();
}

void vboot_save_data(struct vb2_context *ctx)
{
	function_called();
}

tpm_result_t antirollback_lock_space_firmware(void)
{
	function_called();
	return lock_status;
}

tpm_result_t vboot_extend_pcr(struct vb2_context *ctx, int pcr, enum vb2_pcr_digest which)
{
	function_called();
	return TPM_SUCCESS;
}

void die(const char *message, ...)
{
	assert_string_equal(message, "Unable to lock firmware rollback floor in recovery\n");
	mock_assert(false, message, __FILE__, __LINE__);
	__builtin_unreachable();
}

static void recovery_lock_order(void **state)
{
	lock_status = TPM_SUCCESS;
	expect_function_call(vboot_save_data);
	if (CONFIG(SECURITY_FAIL_CLOSED))
		expect_function_call(antirollback_lock_space_firmware);
	expect_function_calls(vboot_extend_pcr, 3);
	verstage_main();
}

#if CONFIG(SECURITY_FAIL_CLOSED)
static void recovery_lock_failure(void **state)
{
	lock_status = TPM_IOERROR;
	expect_function_call(vboot_save_data);
	expect_function_call(antirollback_lock_space_firmware);
	expect_assert_failure(verstage_main());
}
#endif

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(recovery_lock_order),
#if CONFIG(SECURITY_FAIL_CLOSED)
		cmocka_unit_test(recovery_lock_failure),
#endif
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
