/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cpu.h>
#include <cpu/amd/msr.h>
#include <tests/test.h>

static uint32_t max_leaf, xsave_features, smm_features;
static uint64_t hwcr;
static unsigned int cpuid_reads, hwcr_reads;

static uint32_t mock_cpuid_eax(uint32_t leaf)
{
	cpuid_reads++;
	if (leaf == 0x80000000)
		return max_leaf;
	assert_int_equal(leaf, 0x80000021);
	assert_true(max_leaf >= leaf);
	return smm_features;
}

static uint32_t mock_cpuid_ebx(uint32_t leaf)
{
	cpuid_reads++;
	assert_int_equal(leaf, 0x80000008);
	assert_true(max_leaf >= leaf);
	return xsave_features;
}

static msr_t mock_rdmsr(unsigned int index)
{
	assert_int_equal(index, HWCR_MSR);
	hwcr_reads++;
	return (msr_t) { .raw = hwcr };
}

#define cpuid_eax mock_cpuid_eax
#define cpuid_ebx mock_cpuid_ebx
#define rdmsr mock_rdmsr
#include <drivers/efi_variable_service/entry_64.c>

static int setup(void **unused)
{
	max_leaf = 0x80000021;
	xsave_features = BIT(2);
	smm_features = BIT(3);
	hwcr = 0;
	cpuid_reads = hwcr_reads = 0;
	return 0;
}

#if CONFIG(SOC_AMD_COMMON)
static void missing_xsave_error_pointers(void **unused)
{
	max_leaf = 0x80000007;
	assert_false(efi_variable_service_entry_allowed());
	assert_int_equal(cpuid_reads, 1);
	assert_int_equal(hwcr_reads, 0);

	max_leaf = 0x80000021;
	xsave_features = 0;
	assert_false(efi_variable_service_entry_allowed());
	assert_int_equal(hwcr_reads, 0);
}

static void without_page_configuration_lock(void **unused)
{
	max_leaf = 0x80000008;
	assert_true(efi_variable_service_entry_allowed());
	assert_int_equal(hwcr_reads, 0);

	max_leaf = 0x80000021;
	smm_features = 0;
	assert_true(efi_variable_service_entry_allowed());
	assert_int_equal(hwcr_reads, 0);
}

static void recheck_page_configuration_lock(void **unused)
{
	/* Ordinary SMM configuration locks do not lock the FP guard's registers. */
	hwcr = SMM_LOCK | SMM_BASE_LOCK;
	assert_true(efi_variable_service_entry_allowed());
	assert_int_equal(hwcr_reads, 1);

	hwcr |= SMM_PGCFG_LOCK;
	assert_false(efi_variable_service_entry_allowed());
	assert_int_equal(hwcr_reads, 2);

	/* The per-SMI page lock clears on RSM; do not cache the earlier failure. */
	hwcr &= ~SMM_PGCFG_LOCK;
	assert_true(efi_variable_service_entry_allowed());
	assert_int_equal(hwcr_reads, 3);
}
#else
static void non_amd_uses_architectural_fxsave(void **unused)
{
	assert_true(efi_variable_service_entry_allowed());
	assert_int_equal(cpuid_reads, 0);
	assert_int_equal(hwcr_reads, 0);
}
#endif

int main(void)
{
	const struct CMUnitTest tests[] = {
#if CONFIG(SOC_AMD_COMMON)
		cmocka_unit_test_setup(missing_xsave_error_pointers, setup),
		cmocka_unit_test_setup(without_page_configuration_lock, setup),
		cmocka_unit_test_setup(recheck_page_configuration_lock, setup),
#else
		cmocka_unit_test_setup(non_amd_uses_architectural_fxsave, setup),
#endif
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
