/* SPDX-License-Identifier: GPL-2.0-only */

#include <intelblocks/vtd.h>
#include <tests/test.h>

static uint32_t mock_vtd_read32(uintptr_t base, uint32_t reg);
static uint64_t mock_vtd_read64(uintptr_t base, uint32_t reg);
static void mock_vtd_write32(uintptr_t base, uint32_t reg, uint32_t value);
static void mock_vtd_write64(uintptr_t base, uint32_t reg, uint64_t value);
static const void *mock_find_hob(const uint8_t *guid, size_t *size);
static unsigned int mock_phys_address_size(void);

#define vtd_read32 mock_vtd_read32
#define vtd_read64 mock_vtd_read64
#define vtd_write32 mock_vtd_write32
#define vtd_write64 mock_vtd_write64
#define fsp_find_extension_hob_by_guid mock_find_hob
#define cpu_phys_address_size mock_phys_address_size
#include "../../src/soc/intel/common/block/vtd/vtd.c"
#undef cpu_phys_address_size
#undef fsp_find_extension_hob_by_guid
#undef vtd_write64
#undef vtd_write32
#undef vtd_read64
#undef vtd_read32

#define TEST_VTD_BASE 0x10000000
#define TEST_LOW_LIMIT (2 * MiB)
#define TEST_HIGH_LIMIT (5ULL * GiB)
#define TEST_ALIGNMENT (1 * MiB)

static struct vtd_pmr_info_hob test_hob;
static size_t test_hob_size;
static uint32_t regs32[0x80 / sizeof(uint32_t)];
static uint64_t regs64[0x80 / sizeof(uint64_t)];
static bool fail_enable;
static bool corrupt_low_readback;
static bool corrupt_high_readback;
static bool low_final_written;
static bool high_final_written;
static bool fail_disable;
static bool pmen_unreadable;
static bool transition_pending;
static unsigned int transition_delay;
static unsigned int pending_reads;
static unsigned int pmen_writes;
static uint64_t time_us;
static bool fail_high_disable;
static bool missing_hob;
static bool corrupt_after_enable;

void timer_monotonic_get(struct mono_time *mt)
{
	time_us += 1000;
	mono_time_set_usecs(mt, time_us);
}

static uint32_t mock_vtd_read32(uintptr_t base, uint32_t reg)
{
	assert_int_equal(base, TEST_VTD_BASE);
	if (reg == PMEN_REG) {
		if (pmen_unreadable)
			return UINT32_MAX;
		if (transition_pending && pending_reads)
			pending_reads--;
		else if (transition_pending) {
			bool enable = regs32[reg / 4] & PMEN_EPM;
			if (!(enable ? fail_enable : fail_disable)) {
				regs32[reg / 4] &= ~PMEN_PRS;
				if (enable)
					regs32[reg / 4] |= PMEN_PRS;
			}
			transition_pending = false;
		}
	}
	if ((corrupt_low_readback || (corrupt_after_enable &&
	     (regs32[PMEN_REG / 4] & PMEN_PRS))) && reg == PLMLIMIT_REG && low_final_written)
		return regs32[reg / 4] ^ TEST_ALIGNMENT;
	return regs32[reg / 4];
}

static uint64_t mock_vtd_read64(uintptr_t base, uint32_t reg)
{
	assert_int_equal(base, TEST_VTD_BASE);
	if (corrupt_high_readback && reg == PHMLIMIT_REG && high_final_written)
		return regs64[reg / 8] ^ TEST_ALIGNMENT;
	return regs64[reg / 8];
}

static void mock_vtd_write32(uintptr_t base, uint32_t reg, uint32_t value)
{
	assert_int_equal(base, TEST_VTD_BASE);
	if (reg == PLMLIMIT_REG) {
		low_final_written = value != UINT32_MAX;
		value &= ~(TEST_ALIGNMENT - 1);
	}
	if (reg == PMEN_REG) {
		assert_int_equal(!!(regs32[reg / 4] & PMEN_EPM),
				 !!(regs32[reg / 4] & PMEN_PRS));
		value = (value & ~PMEN_PRS) | (regs32[reg / 4] & PMEN_PRS);
		transition_pending = true;
		pending_reads = transition_delay;
		pmen_writes++;
	} else {
		assert_false(regs32[PMEN_REG / 4] & PMEN_PRS);
	}
	regs32[reg / 4] = value;
}

static void mock_vtd_write64(uintptr_t base, uint32_t reg, uint64_t value)
{
	assert_int_equal(base, TEST_VTD_BASE);
	assert_false(regs32[PMEN_REG / 4] & PMEN_PRS);
	if (fail_high_disable && reg == PHMBASE_REG && value == UINT64_MAX)
		return;
	if (reg == PHMLIMIT_REG) {
		high_final_written = value != UINT64_MAX;
		value &= ~(TEST_ALIGNMENT - 1);
	}
	regs64[reg / 8] = value;
}

static const void *mock_find_hob(const uint8_t *guid, size_t *size)
{
	(void)guid;
	*size = test_hob_size;
	return missing_hob ? NULL : &test_hob;
}

static unsigned int mock_phys_address_size(void)
{
	return 48;
}

static int setup(void **state)
{
	(void)state;
	memset(regs32, 0, sizeof(regs32));
	memset(regs64, 0, sizeof(regs64));
	regs32[VER_REG / 4] = 0x10;
	regs32[CAP_REG / 4] = CAP_PMR_LO | CAP_PMR_HI;
	test_hob = (struct vtd_pmr_info_hob) {
		.protected_low_limit = TEST_LOW_LIMIT,
		.protected_high_base = 4ULL * GiB,
		.protected_high_limit = TEST_HIGH_LIMIT,
	};
	test_hob_size = sizeof(test_hob);
	pmr_hob = NULL;
	fail_enable = false;
	corrupt_low_readback = false;
	corrupt_high_readback = false;
	low_final_written = false;
	high_final_written = false;
	fail_disable = false;
	pmen_unreadable = false;
	transition_pending = false;
	transition_delay = 0;
	pending_reads = 0;
	pmen_writes = 0;
	time_us = 0;
	fail_high_disable = false;
	missing_hob = false;
	corrupt_after_enable = false;
	return 0;
}

static void test_rejects_short_hob(void **state)
{
	test_hob_size--;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_requires_high_pmr(void **state)
{
	regs32[CAP_REG / 4] = CAP_PMR_LO;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_rejects_malformed_high_range(void **state)
{
	test_hob.protected_high_base = 0;
	test_hob.protected_high_limit = 4ULL * GiB;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_rejects_unaligned_limit(void **state)
{
	test_hob.protected_low_limit++;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_rejects_low_readback_mismatch(void **state)
{
	corrupt_low_readback = true;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_rejects_high_readback_mismatch(void **state)
{
	corrupt_high_readback = true;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_rejects_enable_failure(void **state)
{
	fail_enable = true;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_accepts_exact_low_and_high_coverage(void **state)
{
	assert_true(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
	assert_int_equal(regs32[PLMLIMIT_REG / 4], TEST_LOW_LIMIT - TEST_ALIGNMENT);
	assert_int_equal(regs64[PHMLIMIT_REG / 8], TEST_HIGH_LIMIT - TEST_ALIGNMENT);
}

static void test_accepts_low_only_coverage(void **state)
{
	test_hob.protected_high_base = 0;
	test_hob.protected_high_limit = 0;
	regs32[CAP_REG / 4] = CAP_PMR_LO;
	assert_true(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_accepts_delayed_enable(void **state)
{
	transition_delay = 5;
	assert_true(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_accepts_delayed_disable(void **state)
{
	regs32[PMEN_REG / 4] = PMEN_EPM | PMEN_PRS;
	transition_delay = 5;
	assert_true(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_waits_for_pending_enable(void **state)
{
	regs32[PMEN_REG / 4] = PMEN_EPM;
	transition_pending = true;
	pending_reads = 5;
	assert_true(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_waits_for_pending_disable(void **state)
{
	regs32[PMEN_REG / 4] = PMEN_PRS;
	transition_pending = true;
	pending_reads = 5;
	assert_true(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_rejects_stuck_pending_enable(void **state)
{
	regs32[PMEN_REG / 4] = PMEN_EPM;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
	assert_int_equal(pmen_writes, 0);
	assert_false(low_final_written);
}

static void test_rejects_disable_timeout(void **state)
{
	regs32[PMEN_REG / 4] = PMEN_EPM | PMEN_PRS;
	fail_disable = true;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
	assert_false(low_final_written);
	assert_true(regs32[PMEN_REG / 4] & PMEN_PRS);
}

static void test_rejects_unreadable_pmen(void **state)
{
	pmen_unreadable = true;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
	assert_int_equal(pmen_writes, 0);
}

static void test_disables_unused_high_range(void **state)
{
	test_hob.protected_high_base = 0;
	test_hob.protected_high_limit = 0;
	regs64[PHMBASE_REG / 8] = 4ULL * GiB;
	regs64[PHMLIMIT_REG / 8] = TEST_HIGH_LIMIT;
	assert_true(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
	assert_true(regs64[PHMBASE_REG / 8] > regs64[PHMLIMIT_REG / 8]);
}

static void test_accepts_empty_high_range_at_4g(void **state)
{
	test_hob.protected_high_base = 4ULL * GiB;
	test_hob.protected_high_limit = 4ULL * GiB;
	assert_true(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
	assert_true(regs64[PHMBASE_REG / 8] > regs64[PHMLIMIT_REG / 8]);
}

static void test_accepts_empty_high_range_without_high_pmr(void **state)
{
	test_hob.protected_high_base = 4ULL * GiB;
	test_hob.protected_high_limit = 4ULL * GiB;
	regs32[CAP_REG / 4] = CAP_PMR_LO;
	assert_true(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_rejects_high_disable_failure(void **state)
{
	test_hob.protected_high_base = 0;
	test_hob.protected_high_limit = 0;
	fail_high_disable = true;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

static void test_rejects_missing_hob(void **state)
{
	missing_hob = true;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
	assert_int_equal(pmen_writes, 0);
}

static void test_rejects_missing_engine(void **state)
{
	regs32[VER_REG / 4] = UINT32_MAX;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
	assert_int_equal(pmen_writes, 0);
}

static void test_rejects_missing_low_capability(void **state)
{
	regs32[CAP_REG / 4] = CAP_PMR_HI;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
	assert_int_equal(pmen_writes, 0);
}

static void test_rejects_post_enable_corruption(void **state)
{
	corrupt_after_enable = true;
	assert_false(vtd_engine_enable_dma_protection(TEST_VTD_BASE));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_rejects_short_hob, setup),
		cmocka_unit_test_setup(test_requires_high_pmr, setup),
		cmocka_unit_test_setup(test_rejects_malformed_high_range, setup),
		cmocka_unit_test_setup(test_rejects_unaligned_limit, setup),
		cmocka_unit_test_setup(test_rejects_low_readback_mismatch, setup),
		cmocka_unit_test_setup(test_rejects_high_readback_mismatch, setup),
		cmocka_unit_test_setup(test_rejects_enable_failure, setup),
		cmocka_unit_test_setup(test_accepts_exact_low_and_high_coverage, setup),
		cmocka_unit_test_setup(test_accepts_low_only_coverage, setup),
		cmocka_unit_test_setup(test_accepts_delayed_enable, setup),
		cmocka_unit_test_setup(test_accepts_delayed_disable, setup),
		cmocka_unit_test_setup(test_waits_for_pending_enable, setup),
		cmocka_unit_test_setup(test_waits_for_pending_disable, setup),
		cmocka_unit_test_setup(test_rejects_stuck_pending_enable, setup),
		cmocka_unit_test_setup(test_rejects_disable_timeout, setup),
		cmocka_unit_test_setup(test_rejects_unreadable_pmen, setup),
		cmocka_unit_test_setup(test_disables_unused_high_range, setup),
		cmocka_unit_test_setup(test_accepts_empty_high_range_at_4g, setup),
		cmocka_unit_test_setup(test_accepts_empty_high_range_without_high_pmr, setup),
		cmocka_unit_test_setup(test_rejects_high_disable_failure, setup),
		cmocka_unit_test_setup(test_rejects_missing_hob, setup),
		cmocka_unit_test_setup(test_rejects_missing_engine, setup),
		cmocka_unit_test_setup(test_rejects_missing_low_capability, setup),
		cmocka_unit_test_setup(test_rejects_post_enable_corruption, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
