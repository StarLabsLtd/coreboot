/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbfs.h>
#include <tests/test.h>
#define main coreboot_main
#include <security/tpm/preboot_auth.c>
#undef main

static struct vb2_context context;
#if CONFIG(VBOOT)
static struct cbfs_boot_device rw_cbfs;
#endif
static bool cbfs_available, context_available;
static int resume_status;
static tpm_result_t extend_status;
static const char *fatal_message;

#if CONFIG(VBOOT)
const struct cbfs_boot_device *vboot_get_cbfs_boot_device(void)
{
	return cbfs_available ? &rw_cbfs : NULL;
}
#endif

struct vb2_context *vboot_get_context(void)
{
	return context_available ? &context : NULL;
}

int platform_is_resuming(void)
{
	return resume_status;
}

tpm_result_t tpm_extend_pcr_compact_hash(int pcr, enum vb2_hash_algorithm algo,
				      const uint8_t *digest, size_t digest_len,
				      const char *digest_name)
{
	function_called();
	assert_int_equal(pcr, 6);
	assert_int_equal(algo, VB2_HASH_SHA256);
	assert_int_equal(digest_len, sizeof(preboot_auth_open));
	assert_memory_equal(digest, preboot_auth_open, sizeof(preboot_auth_open));
	assert_string_equal(digest_name, "Star Labs pre-OS authentication phase open v1");
	return extend_status;
}

void die(const char *message, ...)
{
	assert_non_null(fatal_message);
	assert_string_equal(message, fatal_message);
	mock_assert(false, message, __FILE__, __LINE__);
	__builtin_unreachable();
}

static int setup(void **state)
{
	memset(&context, 0, sizeof(context));
	cbfs_available = true;
	context_available = true;
	resume_status = 0;
	extend_status = TPM_SUCCESS;
	fatal_message = NULL;
	return 0;
}

static void expect_open(void)
{
	expect_function_call(tpm_extend_pcr_compact_hash);
	open_preboot_auth_phase(NULL);
}

static void expect_denied(void)
{
	/* An unexpected PCR extension has no matching function-call expectation. */
	fatal_message = "DAVE requires a verified normal boot\n";
	expect_assert_failure(open_preboot_auth_phase(NULL));
}

static void boot_without_verification(void **state)
{
	cbfs_available = false;
	if (CONFIG(DAVE_MODE))
		expect_denied();
	else
		expect_open();
}

#if CONFIG(DAVE_MODE) && (!CONFIG(VBOOT) || CONFIG(VBOOT_MOCK_SECDATA))
static void unavailable_verification(void **state)
{
	/* A simulated RW selection cannot override the build's trust configuration. */
	expect_denied();
}
#endif

#if !CONFIG(DAVE_MODE) || (CONFIG(VBOOT) && !CONFIG(VBOOT_MOCK_SECDATA))
static void normal_boot(void **state)
{
	expect_open();
}

static void absent_context(void **state)
{
	context_available = false;
	if (CONFIG(DAVE_MODE))
		expect_denied();
	else
		expect_open();
}

static void forbidden_vboot_modes(void **state)
{
	const uint64_t forbidden[] = {
		VB2_CONTEXT_DEVELOPER_MODE, VB2_CONTEXT_RECOVERY_MODE,
		VB2_CONTEXT_NO_BOOT, VB2_CONTEXT_S3_RESUME,
	};

	for (size_t i = 0; i < ARRAY_SIZE(forbidden); i++) {
		context.flags = forbidden[i];
		if (CONFIG(DAVE_MODE))
			expect_denied();
		else
			expect_open();
	}
}

static void platform_resume(void **state)
{
	/* Reject both an S3 indication and failure to determine resume state. */
	for (resume_status = -1; resume_status <= 1; resume_status += 2) {
		if (CONFIG(DAVE_MODE))
			expect_denied();
		else
			expect_open();
	}
}

static void tpm_failure(void **state)
{
	extend_status = TPM_CB_COMMUNICATION_ERROR;
	fatal_message = "Unable to open TPM pre-OS authentication phase\n";
	expect_function_call(tpm_extend_pcr_compact_hash);
	expect_assert_failure(open_preboot_auth_phase(NULL));
}
#endif

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(boot_without_verification, setup),
#if CONFIG(DAVE_MODE) && (!CONFIG(VBOOT) || CONFIG(VBOOT_MOCK_SECDATA))
		cmocka_unit_test_setup(unavailable_verification, setup),
#endif
#if !CONFIG(DAVE_MODE) || (CONFIG(VBOOT) && !CONFIG(VBOOT_MOCK_SECDATA))
		cmocka_unit_test_setup(normal_boot, setup),
		cmocka_unit_test_setup(absent_context, setup),
		cmocka_unit_test_setup(forbidden_vboot_modes, setup),
		cmocka_unit_test_setup(platform_resume, setup),
		cmocka_unit_test_setup(tpm_failure, setup),
#endif
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
