/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/cpu/x86/smm/smm_module_loader.c"

#include <stdlib.h>
#include <tests/test.h>

#define PAGE_TABLE_BUFFER_SIZE (32 * KiB)

struct page_table_memory {
	uint8_t *allocation;
	uintptr_t base;
	uintptr_t handler;
};

static struct page_table_memory allocate_page_table_memory(void)
{
	struct page_table_memory memory = {
		.allocation = calloc(1, PAGE_TABLE_BUFFER_SIZE + 4096),
	};

	assert_non_null(memory.allocation);
	memory.base = ALIGN_UP((uintptr_t)memory.allocation, 4096);
	memory.handler = memory.base + PAGE_TABLE_BUFFER_SIZE;
	return memory;
}

static void free_page_table_memory(struct page_table_memory *memory)
{
	free(memory->allocation);
}

static void assert_no_page_table_writes(const struct page_table_memory *memory)
{
	for (size_t i = 0; i < PAGE_TABLE_BUFFER_SIZE; i++)
		assert_int_equal(((uint8_t *)memory->base)[i], 0);
}

static void assert_page_table_writes_are_in_region(const struct page_table_memory *memory,
					   const struct region *region)
{
	for (size_t i = 0; i < PAGE_TABLE_BUFFER_SIZE; i++) {
		if (((uint8_t *)memory->base)[i])
			assert_true(memory->base + i >= region_offset(region) &&
				    memory->base + i <= region_last(region));
	}
}

static void test_rejects_page_tables_before_writing(void **state)
{
	const bool one_g_pages = (uintptr_t)*state;
	struct page_table_memory memory = allocate_page_table_memory();
	uintptr_t pt_base;
	struct region regions[SMM_REGIONS_ARRAY_SIZE] = {};
	struct smm_loader_params params = { .cr3 = 0 };

	assert_int_equal(page_table_base(memory.handler, one_g_pages, &pt_base), 0);
	const struct region smram = region_create(pt_base + 1, memory.handler - pt_base - 1);
	assert_int_equal(install_page_tables(smram, memory.handler, one_g_pages, regions,
					&params, &pt_base), -1);
	assert_int_equal(params.cr3, 0);
	assert_no_page_table_writes(&memory);
	free_page_table_memory(&memory);
}

static void test_rejects_unaligned_page_table_space_before_writing(void **state)
{
	const bool one_g_pages = (uintptr_t)*state;
	struct page_table_memory memory = allocate_page_table_memory();
	uintptr_t pt_base;
	struct region regions[SMM_REGIONS_ARRAY_SIZE] = {};
	struct smm_loader_params params = { .cr3 = 0 };

	memory.handler++;
	assert_int_equal(page_table_base(memory.handler, one_g_pages, &pt_base), 0);
	const struct region smram = region_create(pt_base + 8, memory.handler - pt_base - 8);
	assert_int_equal(install_page_tables(smram, memory.handler, one_g_pages, regions,
					&params, &pt_base), -1);
	assert_int_equal(params.cr3, 0);
	assert_no_page_table_writes(&memory);
	free_page_table_memory(&memory);
}

static void test_installs_page_tables_in_exact_fit_region(void **state)
{
	const bool one_g_pages = (uintptr_t)*state;
	struct page_table_memory memory = allocate_page_table_memory();
	uintptr_t pt_base;
	struct region regions[SMM_REGIONS_ARRAY_SIZE] = {};
	struct smm_loader_params params = { .cr3 = 0 };

	assert_int_equal(page_table_base(memory.handler, one_g_pages, &pt_base), 0);
	const struct region smram = region_create(pt_base, memory.handler - pt_base);
	assert_int_equal(install_page_tables(smram, memory.handler, one_g_pages, regions,
					&params, &pt_base), 0);
	assert_int_equal(params.cr3, pt_base);

	const uintptr_t pages_base = ALIGN_UP(pt_base + 8, 4096);
	assert_int_equal(*(uint64_t *)pages_base, _GEN_PAGE(0));
	if (one_g_pages) {
		assert_int_equal(*(uint64_t *)pt_base, _GEN_DIR(pages_base));
	} else {
		const uintptr_t pdpt = pages_base + 4096 * CONFIG_CPU_PT_ROM_MAP_GB;
		assert_int_equal(*(uint64_t *)pdpt, _GEN_DIR(pages_base));
		assert_int_equal(*(uint64_t *)pt_base, _GEN_DIR(pdpt));
	}
	assert_page_table_writes_are_in_region(&memory, &smram);

	free_page_table_memory(&memory);
}

static void test_installs_page_tables_with_space(void **state)
{
	const bool one_g_pages = (uintptr_t)*state;
	struct page_table_memory memory = allocate_page_table_memory();
	uintptr_t pt_base;
	struct region regions[SMM_REGIONS_ARRAY_SIZE] = {};
	struct smm_loader_params params = { .cr3 = 0 };

	assert_int_equal(page_table_base(memory.handler, one_g_pages, &pt_base), 0);
	const struct region smram = region_create(pt_base - 4096, memory.handler - pt_base + 4096);
	assert_int_equal(install_page_tables(smram, memory.handler, one_g_pages, regions,
					&params, &pt_base), 0);
	assert_int_equal(params.cr3, pt_base);
	assert_page_table_writes_are_in_region(&memory, &smram);

	free_page_table_memory(&memory);
}

static void test_rejects_page_table_underflow(void **state)
{
	const bool one_g_pages = (uintptr_t)*state;
	uintptr_t pt_base;

	assert_int_equal(page_table_base(0, one_g_pages, &pt_base), -1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_prestate(test_rejects_page_tables_before_writing, (void *)true),
		cmocka_unit_test_prestate(test_rejects_page_tables_before_writing, (void *)false),
		cmocka_unit_test_prestate(test_rejects_unaligned_page_table_space_before_writing,
					  (void *)true),
		cmocka_unit_test_prestate(test_rejects_unaligned_page_table_space_before_writing,
					  (void *)false),
		cmocka_unit_test_prestate(test_installs_page_tables_in_exact_fit_region,
					  (void *)true),
		cmocka_unit_test_prestate(test_installs_page_tables_in_exact_fit_region,
					  (void *)false),
		cmocka_unit_test_prestate(test_installs_page_tables_with_space, (void *)true),
		cmocka_unit_test_prestate(test_installs_page_tables_with_space, (void *)false),
		cmocka_unit_test_prestate(test_rejects_page_table_underflow, (void *)true),
		cmocka_unit_test_prestate(test_rejects_page_table_underflow, (void *)false),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
