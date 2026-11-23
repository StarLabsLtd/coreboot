/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <security/vboot/vboot_common.c>

static tpm_result_t init_status, write_status;
static unsigned int writes;
static const char *failure_message;

tpm_result_t tlcl_lib_init(void)
{
	return init_status;
}

tpm_result_t antirollback_write_space_firmware(struct vb2_context *ctx)
{
	writes++;
	return write_status;
}

tpm_result_t antirollback_write_space_kernel(struct vb2_context *ctx)
{
	fail();
	return TPM_IOERROR;
}

void die(const char *message, ...)
{
	assert_string_equal(message, failure_message);
	mock_assert(false, message, __FILE__, __LINE__);
	__builtin_unreachable();
}

static int setup(void **state)
{
	init_status = TPM_SUCCESS;
	write_status = TPM_SUCCESS;
	writes = 0;
	return 0;
}

static void successful_write(void **state)
{
	struct vb2_context ctx = { .flags = VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED };

	save_secdata(&ctx);
	assert_int_equal(writes, 1);
	assert_false(ctx.flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
}

static void failed_write(void **state)
{
	struct vb2_context ctx = { .flags = VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED };

	write_status = TPM_IOERROR;
#if CONFIG(SECURITY_FAIL_CLOSED)
	failure_message = "Failed to save firmware rollback floor\n";
	expect_assert_failure(save_secdata(&ctx));
	assert_true(ctx.flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
#else
	save_secdata(&ctx);
	assert_false(ctx.flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
#endif
	assert_int_equal(writes, 1);
}

static void failed_init(void **state)
{
	struct vb2_context ctx = { .flags = VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED };

	init_status = TPM_IOERROR;
#if CONFIG(SECURITY_FAIL_CLOSED)
	failure_message = "TPM initialization failed before firmware floor write\n";
	expect_assert_failure(save_secdata(&ctx));
#else
	save_secdata(&ctx);
#endif
	assert_true(ctx.flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
	assert_int_equal(writes, 0);
}

static void unchanged_floor(void **state)
{
	struct vb2_context ctx = { 0 };

	init_status = TPM_IOERROR;
	save_secdata(&ctx);
	assert_int_equal(writes, 0);
	assert_int_equal(ctx.flags, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(successful_write, setup),
		cmocka_unit_test_setup(failed_write, setup),
		cmocka_unit_test_setup(failed_init, setup),
		cmocka_unit_test_setup(unchanged_floor, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
