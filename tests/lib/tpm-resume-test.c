/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <security/tpm/tspi/tspi.c>
#include <security/tpm/tspi/crtm.c>

static struct vb2_context context;
static unsigned int accesses, entries, copies;
static tpm_result_t init_result, extend_result;
static uint8_t retained[16];
TEST_REGION(tpm_log, 256);

struct vb2_context *vboot_get_context(void)
{
	return &context;
}

tpm_result_t tlcl_lib_init(void)
{
	accesses++;
	return init_result;
}

tpm_result_t tlcl2_extend(int pcr, const uint8_t *digest, enum vb2_hash_algorithm algo)
{
	accesses++;
	return extend_result;
}

void *tpm2_log_cbmem_init(void)
{
	accesses++;
	return retained;
}

uint16_t tpm2_log_get_size(const void *log)
{
	return 1;
}

void tpm2_log_copy_entries(const void *from, void *to)
{
	copies++;
	memset(to, 0, sizeof(retained));
}

void tpm2_log_add_table_entry(const char *name, const uint32_t pcr,
			    enum vb2_hash_algorithm algo, const uint8_t *digest,
			    const size_t size)
{
	entries++;
}

static void resume_preserves_measurements(void **state)
{
	const uint8_t digest[32] = { 1 };
	uint8_t before[sizeof(retained)];

	context.flags = VB2_CONTEXT_S3_RESUME;
	accesses = 0;
	entries = 0;
	copies = 0;
	memset(retained, 0xa5, sizeof(retained));
	memcpy(before, retained, sizeof(before));
	assert_int_equal(tpm_extend_pcr(2, VB2_HASH_SHA256, digest, sizeof(digest),
				      "resume file"), TPM_SUCCESS);
	recover_tpm_log(1);
	assert_int_equal(accesses, 0);
	assert_int_equal(entries, 0);
	assert_int_equal(copies, 0);
	assert_memory_equal(retained, before, sizeof(before));
	assert_int_equal(tpm_extend_pcr(2, VB2_HASH_SHA256, NULL, 32, "invalid"),
			 TPM_IOERROR);
}

static void resume_keeps_retained_log(void **state)
{
	uint8_t before[sizeof(retained)];

	context.flags = VB2_CONTEXT_S3_RESUME;
	accesses = 0;
	copies = 0;
	memset(retained, 0xa5, sizeof(retained));
	memcpy(before, retained, sizeof(before));
	recover_tpm_log(1);
	assert_int_equal(accesses, 0);
	assert_int_equal(copies, 0);
	assert_memory_equal(retained, before, sizeof(before));
}

static void cold_boot_still_measures(void **state)
{
	const uint8_t digest[32] = { 1 };

	context.flags = 0;
	accesses = 0;
	entries = 0;
	copies = 0;
	init_result = TPM_SUCCESS;
	extend_result = TPM_SUCCESS;
	assert_int_equal(tpm_extend_pcr(2, VB2_HASH_SHA256, digest, sizeof(digest),
				      "cold file"), TPM_SUCCESS);
	assert_int_equal(accesses, 2);
	assert_int_equal(entries, 1);
	recover_tpm_log(1);
	assert_int_equal(copies, 1);
	init_result = TPM_IOERROR;
	assert_int_equal(tpm_extend_pcr(2, VB2_HASH_SHA256, digest, sizeof(digest),
				      "init failure"), TPM_IOERROR);
	init_result = TPM_SUCCESS;
	extend_result = TPM_IOERROR;
	assert_int_equal(tpm_extend_pcr(2, VB2_HASH_SHA256, digest, sizeof(digest),
				      "extend failure"), TPM_IOERROR);
	assert_int_equal(entries, 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(resume_preserves_measurements),
		cmocka_unit_test(resume_keeps_retained_log),
		cmocka_unit_test(cold_boot_still_measures),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
