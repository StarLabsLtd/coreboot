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

static bool addr_space_has_type(const struct memranges *addr_space, int type)
{
	const struct range_entry *r;

	memranges_each_entry(r, addr_space) {
		if (range_entry_tag(r) == type)
			return true;
	}

	return false;
}

static bool mtrrs_have_type(int type)
{
	for (int i = 0; i < total_mtrrs; i++) {
		if ((msrs[MTRR_PHYS_BASE(i)].lo & MTRR_TAG_MASK) == type &&
		    (msrs[MTRR_PHYS_MASK(i)].lo & MTRR_PHYS_MASK_VALID))
			return true;
	}

	return false;
}

static bool mtrrs_have_base(uint64_t base, int type)
{
	for (int i = 0; i < total_mtrrs; i++) {
		const msr_t mtrr_base = msrs[MTRR_PHYS_BASE(i)];
		const uint64_t address = (mtrr_base.lo & ~MTRR_TAG_MASK) |
			((uint64_t)mtrr_base.hi << 32);

		if ((mtrr_base.lo & MTRR_TAG_MASK) == type && address == base &&
		    (msrs[MTRR_PHYS_MASK(i)].lo & MTRR_PHYS_MASK_VALID))
			return true;
	}

	return false;
}

static void set_original_solution(void)
{
	memset(&mtrr_global_solution, 0, sizeof(mtrr_global_solution));
	mtrr_global_solution.mtrr_default_type = MTRR_TYPE_WRBACK;
	mtrr_global_solution.num_used = total_mtrrs;
	for (int i = 0; i < total_mtrrs; i++) {
		const msr_t base = { .lo = 0x100000 * (i + 1) | MTRR_TYPE_WRBACK };
		const msr_t mask = { .lo = MTRR_PHYS_MASK_VALID };

		mtrr_global_solution.regs[i].base = base;
		mtrr_global_solution.regs[i].mask = mask;
		msrs[MTRR_PHYS_BASE(i)] = base;
		msrs[MTRR_PHYS_MASK(i)] = mask;
	}
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

static void test_temporary_mtrrs_retain_wc(void **state)
{
	struct memranges with_wc;
	struct memranges without_wc;
	bool wrcomb_seen = false;
	int retained_mtrrs;
	int removed_mtrrs;
	msr_t original_base[NUM_MTRR_STATIC_STORAGE];
	msr_t original_mask[NUM_MTRR_STATIC_STORAGE];

	(void)state;
	resources[0].next = &resources[1];
	resources[1].next = &resources[2];
	resources[2].next = NULL;
	total_mtrrs = 8;
	memset(msrs, 0, sizeof(msrs));
	memset(temp_ranges, 0, sizeof(temp_ranges));
	put_back_original_solution = false;
	temp_ranges[0] = (struct temp_range) {
		.begin = 3ULL * GiB,
		.size = 256 * MiB,
		.type = MTRR_TYPE_WRCOMB,
	};
	temp_ranges[1] = (struct temp_range) {
		.begin = 0xfe100000,
		.size = 5 * MiB,
		.type = MTRR_TYPE_WRPROT,
	};
	memranges_init_empty(&with_wc, NULL, 0);
	memranges_init_empty(&without_wc, NULL, 0);
	build_temp_addr_space(&with_wc, true, &wrcomb_seen);
	assert_true(wrcomb_seen);
	assert_true(addr_space_has_type(&with_wc, MTRR_TYPE_WRCOMB));
	calc_var_mtrrs(&with_wc, true, 48, &retained_mtrrs, false);
	assert_int_equal(retained_mtrrs, 7);

	build_temp_addr_space(&without_wc, false, NULL);
	assert_false(addr_space_has_type(&without_wc, MTRR_TYPE_WRCOMB));
	calc_var_mtrrs(&without_wc, true, 48, &removed_mtrrs, false);
	assert_true(removed_mtrrs <= total_mtrrs - get_os_reserved_mtrrs());

	memranges_teardown(&with_wc);
	memranges_teardown(&without_wc);
	temp_ranges[1] = (struct temp_range) { 0 };

	set_original_solution();
	for (int i = 0; i < total_mtrrs; i++) {
		original_base[i] = msrs[MTRR_PHYS_BASE(i)];
		original_mask[i] = msrs[MTRR_PHYS_MASK(i)];
	}

	write_count = 0;
	mtrr_use_temp_range(0xfe100000, 5 * MiB, MTRR_TYPE_WRPROT);
	assert_true(write_count != 0);
	assert_true(mtrrs_have_type(MTRR_TYPE_WRCOMB));
	assert_true(mtrrs_have_type(MTRR_TYPE_WRPROT));
	assert_true(mtrrs_have_base(4ULL * GiB, MTRR_TYPE_WRBACK));

	mtrr_remove_temp_ranges();
	for (int i = 0; i < total_mtrrs; i++) {
		assert_memory_equal(&msrs[MTRR_PHYS_BASE(i)], &original_base[i],
				    sizeof(original_base[i]));
		assert_memory_equal(&msrs[MTRR_PHYS_MASK(i)], &original_mask[i],
				    sizeof(original_mask[i]));
	}

	write_count = 0;
	mtrr_use_temp_range(0, 0, MTRR_TYPE_WRPROT);
	assert_int_equal(write_count, 0);

	total_mtrrs = 0;
	mtrr_use_temp_range(0xfd000000, 1 * MiB, MTRR_TYPE_WRPROT);
	assert_int_equal(write_count, 0);
	for (int i = 0; i < 8; i++) {
		assert_memory_equal(&msrs[MTRR_PHYS_BASE(i)], &original_base[i],
				    sizeof(original_base[i]));
		assert_memory_equal(&msrs[MTRR_PHYS_MASK(i)], &original_mask[i],
				    sizeof(original_mask[i]));
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_temporary_mtrrs_retain_wc),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
