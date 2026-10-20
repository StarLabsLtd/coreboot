/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>

#include <cpu/x86/cache.h>
#include <cpu/x86/msr.h>

static msr_t test_rdmsr(unsigned int index);
static void test_wrmsr(unsigned int index, msr_t msr);
static void test_disable_cache(void);
static void test_enable_cache(void);

#define rdmsr test_rdmsr
#define wrmsr test_wrmsr
#define disable_cache test_disable_cache
#define enable_cache test_enable_cache
#include "cpu/x86/mtrr/mtrr.c"
#undef enable_cache
#undef disable_cache
#undef wrmsr
#undef rdmsr

#include <device/device.h>
#include <device/pci_ids.h>

static msr_t msrs[0x400];
static unsigned int write_count;

static uint64_t msr_value(msr_t msr)
{
	return msr.lo | ((uint64_t)msr.hi << 32);
}

static int mtrr_type_at(uint64_t address)
{
	const uint64_t phys_mask = (1ULL << 48) - 1;
	int type = msrs[MTRR_DEF_TYPE_MSR].lo & MTRR_DEF_TYPE_MASK;

	for (int i = 0; i < total_mtrrs; i++) {
		const uint64_t mask = msr_value(msrs[MTRR_PHYS_MASK(i)]);
		const uint64_t base = msr_value(msrs[MTRR_PHYS_BASE(i)]);
		const uint64_t address_mask = mask & phys_mask & ~((1ULL << RANGE_SHIFT) - 1);

		if ((mask & MTRR_PHYS_MASK_VALID) &&
		    (address & address_mask) == (base & address_mask))
			return base & MTRR_TAG_MASK;
	}

	return type;
}

struct candidate_counts {
	int full_wc;
	int full_no_wc;
	int low_wc;
	int low_no_wc;
};

static struct candidate_counts count_candidates(void)
{
	struct candidate_counts counts;
	struct memranges with_wc;
	struct memranges without_wc;

	memranges_init_empty(&with_wc, NULL, 0);
	memranges_init_empty(&without_wc, NULL, 0);
	build_temp_addr_space(&with_wc, true, NULL);
	build_temp_addr_space(&without_wc, false, NULL);
	calc_var_mtrrs(&with_wc, true, 48, &counts.full_wc, false);
	calc_var_mtrrs(&without_wc, true, 48, &counts.full_no_wc, false);
	calc_var_mtrrs(&with_wc, false, 48, &counts.low_wc, false);
	calc_var_mtrrs(&without_wc, false, 48, &counts.low_no_wc, false);
	memranges_teardown(&with_wc);
	memranges_teardown(&without_wc);

	return counts;
}

static void set_original_solution(void)
{
	const uint64_t mask = (~(1ULL * GiB - 1) & ((1ULL << 48) - 1)) |
		MTRR_PHYS_MASK_VALID;
	const msr_t original_default = {
		.lo = MTRR_DEF_TYPE_EN | MTRR_DEF_TYPE_FIX_EN | MTRR_TYPE_WRBACK,
	};

	memset(&mtrr_global_solution, 0, sizeof(mtrr_global_solution));
	mtrr_global_solution.mtrr_default_type = MTRR_TYPE_WRBACK;
	mtrr_global_solution.num_used = total_mtrrs;
	for (int i = 0; i < total_mtrrs; i++) {
		const uint64_t address = i * 1ULL * GiB;
		const msr_t base = {
			.lo = address | MTRR_TYPE_WRBACK,
			.hi = address >> 32,
		};
		const msr_t range_mask = {
			.lo = (uint32_t)mask,
			.hi = (uint32_t)(mask >> 32),
		};

		mtrr_global_solution.regs[i].base = base;
		mtrr_global_solution.regs[i].mask = range_mask;
		msrs[MTRR_PHYS_BASE(i)] = base;
		msrs[MTRR_PHYS_MASK(i)] = range_mask;
	}
	msrs[MTRR_DEF_TYPE_MSR] = original_default;
}

static msr_t test_rdmsr(unsigned int index)
{
	return msrs[index];
}

static void test_wrmsr(unsigned int index, msr_t msr)
{
	msrs[index] = msr;
	write_count++;
}

static void test_disable_cache(void)
{
}

static void test_enable_cache(void)
{
}

unsigned int cpu_phys_address_size(void)
{
	return 48;
}

void clear_var_mtrr(int index)
{
	const msr_t zero = { 0 };

	test_wrmsr(MTRR_PHYS_BASE(index), zero);
	test_wrmsr(MTRR_PHYS_MASK(index), zero);
}

struct resource resources[] = {
	{
		.base = 1ULL * MiB,
		.size = 3ULL * GiB - 1ULL * MiB,
		.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED | IORESOURCE_CACHEABLE,
	},
	{
		.base = 3ULL * GiB,
		.size = 256ULL * MiB,
		.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED | IORESOURCE_CACHEABLE,
	},
	{
		.base = 4ULL * GiB,
		.size = 4ULL * GiB,
		.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED | IORESOURCE_CACHEABLE,
	},
};

static struct device device = {
	.enabled = 1,
	.path.type = DEVICE_PATH_PCI,
	.class = PCI_CLASS_DISPLAY_VGA << 8,
	.resource_list = &resources[0],
};
struct device *all_devices = &device;

static void assert_counts(struct candidate_counts counts, int full_wc, int full_no_wc,
			  int low_wc, int low_no_wc)
{
	assert_int_equal(counts.full_wc, full_wc);
	assert_int_equal(counts.full_no_wc, full_no_wc);
	assert_int_equal(counts.low_wc, low_wc);
	assert_int_equal(counts.low_no_wc, low_no_wc);
}

static void assert_next_counts(uintptr_t begin, size_t size, int type,
			       int full_wc, int full_no_wc, int low_wc, int low_no_wc)
{
	for (size_t i = 0; i < ARRAY_SIZE(temp_ranges); i++) {
		if (temp_ranges[i].size != 0)
			continue;

		temp_ranges[i] = (struct temp_range) { begin, size, type };
		assert_counts(count_candidates(), full_wc, full_no_wc, low_wc, low_no_wc);
		temp_ranges[i] = (struct temp_range) { 0 };
		return;
	}

	fail_msg("no temporary MTRR slots remain");
}

static void save_solution(msr_t *base, msr_t *mask, msr_t *default_type)
{
	for (int i = 0; i < total_mtrrs; i++) {
		base[i] = msrs[MTRR_PHYS_BASE(i)];
		mask[i] = msrs[MTRR_PHYS_MASK(i)];
	}
	*default_type = msrs[MTRR_DEF_TYPE_MSR];
}

static void assert_solution_restored(const msr_t *base, const msr_t *mask,
				     const msr_t *default_type)
{
	for (int i = 0; i < total_mtrrs; i++) {
		assert_memory_equal(&msrs[MTRR_PHYS_BASE(i)], &base[i], sizeof(base[i]));
		assert_memory_equal(&msrs[MTRR_PHYS_MASK(i)], &mask[i], sizeof(mask[i]));
	}
	assert_memory_equal(&msrs[MTRR_DEF_TYPE_MSR], default_type, sizeof(*default_type));
}

static void restore_original_solution(const msr_t *base, const msr_t *mask,
				      const msr_t *default_type)
{
	mtrr_remove_temp_ranges();
	assert_solution_restored(base, mask, default_type);
}

static void test_temporary_mtrr_fallbacks(void **state)
{
	msr_t original_base[NUM_MTRR_STATIC_STORAGE];
	msr_t original_mask[NUM_MTRR_STATIC_STORAGE];
	msr_t original_default;

	(void)state;
	resources[0].next = &resources[1];
	resources[1].next = &resources[2];
	resources[2].next = NULL;
	total_mtrrs = 8;
	memset(msrs, 0, sizeof(msrs));
	memset(temp_ranges, 0, sizeof(temp_ranges));
	put_back_original_solution = false;
	set_original_solution();
	save_solution(original_base, original_mask, &original_default);

	/* A caller-supplied WC overlay replaces the cacheable resource beneath it. */
	write_count = 0;
	mtrr_use_temp_range(3ULL * GiB, 256 * MiB, MTRR_TYPE_WRCOMB);
	assert_int_equal(write_count, 2 * total_mtrrs + 1);
	assert_int_equal(mtrr_type_at(3ULL * GiB), MTRR_TYPE_WRCOMB);
	restore_original_solution(original_base, original_mask, &original_default);

	/* The WC overlay and protected flash range fit in the seventh slot. */
	assert_next_counts(0xfe100000, 5 * MiB, MTRR_TYPE_WRPROT, 7, 6, 6, 5);
	write_count = 0;
	mtrr_use_temp_range(0xfe100000, 5 * MiB, MTRR_TYPE_WRPROT);
	assert_int_equal(write_count, 2 * total_mtrrs + 1);
	assert_int_equal(mtrr_type_at(3ULL * GiB), MTRR_TYPE_WRCOMB);
	assert_int_equal(mtrr_type_at(0xfe100000), MTRR_TYPE_WRPROT);
	assert_int_equal(mtrr_type_at(4ULL * GiB), MTRR_TYPE_WRBACK);
	assert_int_not_equal(msrs[MTRR_DEF_TYPE_MSR].lo & MTRR_DEF_TYPE_MASK,
			     original_default.lo & MTRR_DEF_TYPE_MASK);
	restore_original_solution(original_base, original_mask, &original_default);

	/* Full coverage without WC wins over a lower-address WC solution. */
	assert_next_counts(0xfc300000, 3 * MiB, MTRR_TYPE_WRPROT, 9, 8, 8, 7);
	write_count = 0;
	mtrr_use_temp_range(0xfc300000, 3 * MiB, MTRR_TYPE_WRPROT);
	assert_int_equal(write_count, 2 * total_mtrrs + 1);
	assert_int_equal(mtrr_type_at(3ULL * GiB), MTRR_TYPE_UNCACHEABLE);
	assert_int_equal(mtrr_type_at(4ULL * GiB), MTRR_TYPE_WRBACK);
	restore_original_solution(original_base, original_mask, &original_default);

	/* Only the below-4-GiB WC candidate fits after an above-4-GiB request. */
	assert_next_counts(8ULL * GiB + MiB, MiB, MTRR_TYPE_WRPROT, 10, 9, 8, 7);
	write_count = 0;
	mtrr_use_temp_range(8ULL * GiB + MiB, MiB, MTRR_TYPE_WRPROT);
	assert_int_equal(write_count, 2 * total_mtrrs + 1);
	assert_int_equal(mtrr_type_at(3ULL * GiB), MTRR_TYPE_WRCOMB);
	assert_int_equal(mtrr_type_at(4ULL * GiB), MTRR_TYPE_UNCACHEABLE);
	restore_original_solution(original_base, original_mask, &original_default);

	/* The final fallback drops both above-4-GiB coverage and WC. */
	assert_next_counts(0xf8100000, MiB, MTRR_TYPE_WRPROT, 11, 10, 9, 8);
	write_count = 0;
	mtrr_use_temp_range(0xf8100000, MiB, MTRR_TYPE_WRPROT);
	assert_int_equal(write_count, 2 * total_mtrrs + 1);
	assert_int_equal(mtrr_type_at(3ULL * GiB), MTRR_TYPE_UNCACHEABLE);
	assert_int_equal(mtrr_type_at(4ULL * GiB), MTRR_TYPE_UNCACHEABLE);
	restore_original_solution(original_base, original_mask, &original_default);

	write_count = 0;
	assert_int_equal(temp_ranges[5].size, 0);
	mtrr_use_temp_range(0, 0, MTRR_TYPE_WRPROT);
	assert_int_equal(temp_ranges[5].size, 0);
	assert_int_equal(write_count, 0);

	/* A nonzero budget still rejects a request when all candidates exceed it. */
	mtrr_use_temp_range(0xf0000000, MiB, MTRR_TYPE_WRPROT);
	assert_int_equal(write_count, 0);
	assert_solution_restored(original_base, original_mask, &original_default);

	for (size_t i = 6; i < ARRAY_SIZE(temp_ranges); i++)
		mtrr_use_temp_range(0xe0000000 - i * MiB, MiB, MTRR_TYPE_WRPROT);
	assert_int_not_equal(temp_ranges[ARRAY_SIZE(temp_ranges) - 1].size, 0);
	const struct temp_range last = temp_ranges[ARRAY_SIZE(temp_ranges) - 1];
	write_count = 0;
	mtrr_use_temp_range(0xd0000000, MiB, MTRR_TYPE_WRPROT);
	assert_int_equal(write_count, 0);
	assert_memory_equal(&temp_ranges[ARRAY_SIZE(temp_ranges) - 1], &last, sizeof(last));
	assert_solution_restored(original_base, original_mask, &original_default);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_temporary_mtrr_fallbacks),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
