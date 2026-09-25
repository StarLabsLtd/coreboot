/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear_x86.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static uint8_t page_tables[PAE_PGTL_SIZE] __aligned(PAE_PGTL_ALIGN);
static uint8_t not_excluded[PAE_PGTL_SIZE] __aligned(PAE_PGTL_ALIGN);
static uint8_t aperture[PAE_VMEM_SIZE] __aligned(PAE_VMEM_ALIGN);
static struct payload_mm_authvar_mor_clear_x86_backend backend;
static struct payload_mm_authvar_mor_clear_executor_ops executor_ops;

struct mock_arch {
	bool pg_active;
	bool pae_active;
	bool init_failure;
	bool disable_pg_failure;
	bool disable_pae_failure;
	bool flush_available;
	bool mutate_backend;
	bool mutate_arch;
	unsigned int initializes;
	unsigned int maps;
	unsigned int disables;
	unsigned int flushes;
	unsigned int fences;
	uint64_t mapped_physical;
	void *mapped_aperture;
	struct payload_mm_authvar_mor_clear_x86_arch_ops *source_arch;
};

static struct mock_arch mock;

static enum cb_err mock_init(void *tables)
{
	CHECK(tables == page_tables);
	mock.initializes++;
	if (mock.init_failure)
		return CB_ERR;
	mock.pg_active = true;
	mock.pae_active = true;
	return CB_SUCCESS;
}

static void mock_map(void *tables, uint64_t physical, void *window)
{
	CHECK(tables == page_tables);
	mock.maps++;
	mock.mapped_physical = physical;
	mock.mapped_aperture = window;
}

static void mock_disable(void)
{
	mock.disables++;
	mock.pg_active = mock.disable_pg_failure;
	mock.pae_active = mock.disable_pae_failure;
	if (mock.mutate_arch && mock.source_arch)
		mock.source_arch->map_2m = NULL;
}

static bool mock_active(void)
{
	return mock.pg_active || mock.pae_active;
}

static bool mock_flush_available(void)
{
	return mock.flush_available;
}

static void mock_flush(uintptr_t base, size_t size)
{
	CHECK(base >= (uintptr_t)aperture &&
		base + size <= (uintptr_t)aperture + sizeof(aperture));
	mock.flushes++;
	if (mock.mutate_backend)
		backend.arch.map_2m = NULL;
}

static void mock_fence(void)
{
	mock.fences++;
}

static struct payload_mm_authvar_mor_clear_x86_arch_ops valid_arch(void)
{
	return (struct payload_mm_authvar_mor_clear_x86_arch_ops) {
		.page_tables_init = mock_init,
		.map_2m = mock_map,
		.paging_disable = mock_disable,
		.paging_active = mock_active,
		.clflush_available = mock_flush_available,
		.clflush_range = mock_flush,
		.memory_fence = mock_fence,
	};
}

static struct payload_mm_authvar_mor_clear_plan valid_plan(void)
{
	struct payload_mm_authvar_mor_clear_inventory inventory = {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION,
		.size = sizeof(inventory),
		.generation = 7,
		.identity = { 1 },
		.span_count = 5,
		.spans = {
			{ 0x100000000ULL, 0x1000,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE },
			{ (uintptr_t)page_tables, sizeof(page_tables),
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE },
			{ (uintptr_t)aperture, sizeof(aperture),
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE },
			{ (uintptr_t)&backend, sizeof(backend),
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE },
			{ (uintptr_t)&executor_ops, sizeof(executor_ops),
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PROTECTED_FIRMWARE },
		},
	};
	struct payload_mm_authvar_mor_clear_plan plan;

	CHECK(payload_mm_authvar_mor_clear_plan_build(&inventory, &plan) ==
		CB_SUCCESS);
	return plan;
}

static void initialize(struct payload_mm_authvar_mor_clear_plan *plan,
	struct payload_mm_authvar_mor_clear_x86_arch_ops *arch)
{
	memset(&mock, 0, sizeof(mock));
	mock.flush_available = true;
	memset(&backend, 0xa5, sizeof(backend));
	memset(&executor_ops, 0xa5, sizeof(executor_ops));
	*plan = valid_plan();
	*arch = valid_arch();
	mock.source_arch = arch;
}

static void expect_ops_zero(void)
{
	const struct payload_mm_authvar_mor_clear_executor_ops zero = { 0 };

	CHECK(!memcmp(&executor_ops, &zero, sizeof(zero)));
}

static enum cb_err inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	return context == &backend &&
		payload_mm_authvar_mor_clear_plan_validate(plan) == CB_SUCCESS ?
		CB_SUCCESS : CB_ERR;
}

static enum cb_err dma_snapshot(void *unused,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	(void)unused;
	*snapshot = (struct payload_mm_authvar_mor_clear_dma_snapshot) {
		.generation = 9,
		.identity = { 1 },
	};
	return CB_SUCCESS;
}

static void test_execute_high(void)
{
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_x86_arch_ops arch;
	struct payload_mm_authvar_mor_entry entry = { 1, 1, 0 };
	struct payload_mm_authvar_mor_clear_transcript transcript;
	struct payload_mm_authvar_mor_grant grant;
	struct payload_mm_authvar_mor_clear_workspace workspace = { 0 };

	initialize(&plan, &arch);
	memset(aperture, 0xa5, 0x1000);
	CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
		page_tables, aperture, &backend, &executor_ops, &arch) == CB_SUCCESS);
	executor_ops.dma_snapshot = dma_snapshot;
	executor_ops.inventory_context = &backend;
	executor_ops.inventory_context_size = sizeof(backend);
	executor_ops.inventory_validate = inventory_validate;
	const uintptr_t callbacks[] = { (uintptr_t)executor_ops.dma_snapshot,
		(uintptr_t)executor_ops.map_window,
		(uintptr_t)executor_ops.cache_writeback_invalidate,
		(uintptr_t)executor_ops.fence, (uintptr_t)executor_ops.unmap_window,
		(uintptr_t)executor_ops.inventory_validate };
	const void *stack_objects[] = { &workspace, &plan, &entry, &arch,
		&transcript, &grant };
	const size_t stack_sizes[] = { sizeof(workspace), sizeof(plan), sizeof(entry),
		sizeof(arch), sizeof(transcript), sizeof(grant) };
	uintptr_t code_base = callbacks[0];
	uintptr_t code_end = callbacks[0] + 1U;
	uintptr_t stack_base = (uintptr_t)stack_objects[0];
	uintptr_t stack_end = stack_base + stack_sizes[0];
	for (size_t index = 1; index < ARRAY_SIZE(callbacks); index++) {
		if (callbacks[index] < code_base)
			code_base = callbacks[index];
		if (callbacks[index] + 1U > code_end)
			code_end = callbacks[index] + 1U;
	}
	for (size_t index = 1; index < ARRAY_SIZE(stack_objects);
	     index++) {
		const uintptr_t base = (uintptr_t)stack_objects[index];
		if (base < stack_base)
			stack_base = base;
		if (base + stack_sizes[index] > stack_end)
			stack_end = base + stack_sizes[index];
	}
	plan.spans[plan.span_count++] = (struct payload_mm_authvar_mor_grant_span) {
		.base = code_base, .size = code_end - code_base,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
		.exclusion_reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	plan.spans[plan.span_count++] = (struct payload_mm_authvar_mor_grant_span) {
		.base = stack_base, .size = stack_end - stack_base,
		.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
		.exclusion_reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	for (size_t index = 1; index < plan.span_count; index++) {
		struct payload_mm_authvar_mor_grant_span span = plan.spans[index];
		size_t insert = index;
		while (insert && plan.spans[insert - 1U].base > span.base) {
			plan.spans[insert] = plan.spans[insert - 1U];
			insert--;
		}
		plan.spans[insert] = span;
	}
	executor_ops.executable_owner = (void *)code_base;
	executor_ops.executable_owner_size = code_end - code_base;
	executor_ops.stack_owner = (void *)stack_base;
	executor_ops.stack_owner_size = stack_end - stack_base;
	CHECK(payload_mm_authvar_mor_clear_execute(&workspace, &plan, &entry, 11,
		&executor_ops, &transcript, &grant) == CB_SUCCESS);
	CHECK(mock.mapped_physical == 0x100000000ULL);
	CHECK(mock.mapped_aperture == aperture);
	CHECK(mock.initializes == 2 && mock.maps == 2 && mock.flushes == 2 &&
		mock.fences == 2 && mock.disables == 3);
	CHECK(!mock.pg_active && !mock.pae_active && !backend.mapped &&
		!backend.poisoned);
	for (size_t index = 0; index < 0x1000; index++)
		CHECK(!aperture[index]);
}

static void test_mapping(const char *name)
{
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_x86_arch_ops arch;
	void *mapping = (void *)1;
	uint64_t physical = !strcmp(name, "physical-zero") ? 0 :
		0x100000000ULL + 0x1234;

	initialize(&plan, &arch);
	CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
		page_tables, aperture, &backend, &executor_ops, &arch) == CB_SUCCESS);
	if (!strcmp(name, "init-failure"))
		mock.init_failure = true;
	if (!strcmp(name, "cross-boundary"))
		physical = 0x1fffff;
	CHECK(executor_ops.map_window(&backend, physical,
		!strcmp(name, "cross-boundary") ? 2 : 0x100, &mapping) ==
		(!strcmp(name, "init-failure") || !strcmp(name, "cross-boundary") ?
		 CB_ERR : CB_SUCCESS));
	if (!strcmp(name, "init-failure") || !strcmp(name, "cross-boundary")) {
		CHECK(!mapping && !mock.pg_active && !mock.pae_active &&
			backend.poisoned);
		return;
	}
	CHECK(mapping == aperture + (physical & (PAE_VMEM_SIZE - 1U)));
	CHECK(mapping && mock.mapped_physical ==
		(physical & ~((uint64_t)PAE_VMEM_SIZE - 1U)));
	if (!strcmp(name, "unmap-mismatch"))
		physical++;
	CHECK(executor_ops.unmap_window(&backend, physical, mapping, 0x100) ==
		(!strcmp(name, "unmap-mismatch") ? CB_ERR : CB_SUCCESS));
	CHECK(!mock.pg_active && !mock.pae_active && !backend.mapped);
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_x86_arch_ops arch;

	CHECK(argc == 2);
	if (!strcmp(argv[1], "execute-high")) {
		test_execute_high();
		return 0;
	}
	if (!strcmp(argv[1], "physical-zero") || !strcmp(argv[1], "high-map") ||
	    !strcmp(argv[1], "init-failure") ||
	    !strcmp(argv[1], "cross-boundary") ||
	    !strcmp(argv[1], "unmap-mismatch")) {
		test_mapping(argv[1]);
		return 0;
	}
	initialize(&plan, &arch);
	if (!strcmp(argv[1], "prepare")) {
		CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
			page_tables, aperture, &backend, &executor_ops, &arch) == CB_SUCCESS);
		CHECK(executor_ops.context == &backend &&
			executor_ops.window_bytes == PAE_VMEM_SIZE &&
			!mock.pg_active && !mock.pae_active && mock.disables == 1);
		return 0;
	}
	if (!strcmp(argv[1], "paging-bits")) {
		CHECK(!payload_mm_authvar_mor_clear_x86_paging_active_test(0, 0));
		CHECK(payload_mm_authvar_mor_clear_x86_paging_active_test(
			(uintptr_t)1U << 31, 0));
		CHECK(payload_mm_authvar_mor_clear_x86_paging_active_test(0,
			(uintptr_t)1U << 5));
		CHECK(payload_mm_authvar_mor_clear_x86_paging_active_test(
			(uintptr_t)1U << 31, (uintptr_t)1U << 5));
		return 0;
	} else if (!strcmp(argv[1], "prepare-arch-mutation"))
		mock.mutate_arch = true;
	else if (!strcmp(argv[1], "prepare-residual-pg"))
		mock.disable_pg_failure = true;
	else if (!strcmp(argv[1], "prepare-residual-pae"))
		mock.disable_pae_failure = true;
	else if (!strcmp(argv[1], "prepare-no-clflush"))
		mock.flush_available = false;
	else if (!strcmp(argv[1], "prepare-misaligned")) {
		CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
			page_tables + 1, aperture, &backend, &executor_ops, &arch) ==
			CB_ERR_ARG);
		expect_ops_zero();
		return 0;
	} else if (!strcmp(argv[1], "prepare-not-excluded")) {
		CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
			not_excluded, aperture, &backend, &executor_ops, &arch) !=
			CB_SUCCESS);
		expect_ops_zero();
		return 0;
	} else if (!strcmp(argv[1], "prepare-null-plan")) {
		CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(NULL,
			page_tables, aperture, &backend, &executor_ops, &arch) ==
			CB_ERR_ARG);
		expect_ops_zero();
		CHECK(!memcmp(&backend,
			&(struct payload_mm_authvar_mor_clear_x86_backend) { 0 },
			sizeof(backend)));
		return 0;
	} else if (!strcmp(argv[1], "prepare-backing-alias")) {
		CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
			aperture, aperture, &backend, &executor_ops, &arch) ==
			CB_ERR_ARG);
		expect_ops_zero();
		return 0;
	} else if (!strcmp(argv[1], "prepare-state-alias")) {
		CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
			page_tables, aperture,
			(struct payload_mm_authvar_mor_clear_x86_backend *)aperture,
			&executor_ops, &arch) == CB_ERR_ARG);
		expect_ops_zero();
		return 0;
	} else if (!strcmp(argv[1], "prepare-partial") ||
		   !strcmp(argv[1], "prepare-wrong-reason")) {
		for (size_t index = 0; index < plan.span_count; index++) {
			if (plan.spans[index].base == (uintptr_t)page_tables) {
				if (!strcmp(argv[1], "prepare-partial"))
					plan.spans[index].size--;
				else {
					plan.spans[index].span_class =
						PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED;
					plan.spans[index].exclusion_reason =
						PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE;
				}
				break;
			}
		}
		CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
			page_tables, aperture, &backend, &executor_ops, &arch) !=
			CB_SUCCESS);
		expect_ops_zero();
		return 0;
	} else if (!strcmp(argv[1], "cache-mutation") ||
		   !strcmp(argv[1], "map-output-alias")) {
		void *mapping;

		CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
			page_tables, aperture, &backend, &executor_ops, &arch) == CB_SUCCESS);
		if (!strcmp(argv[1], "map-output-alias")) {
			CHECK(executor_ops.map_window(&backend, 0x100000123ULL,
				0x100, (void **)&backend.mapping) != CB_SUCCESS);
			CHECK(backend.poisoned && !mock.pg_active &&
				!mock.pae_active);
			return 0;
		}
		CHECK(executor_ops.map_window(&backend, 0x100000123ULL, 0x100,
			&mapping) == CB_SUCCESS);
		mock.mutate_backend = true;
		CHECK(executor_ops.cache_writeback_invalidate(&backend,
			0x100000123ULL, mapping, 0x100) != CB_SUCCESS);
		CHECK(backend.poisoned && !mock.pg_active && !mock.pae_active);
		return 0;
	} else {
		CHECK(false);
	}
	CHECK(payload_mm_authvar_mor_clear_x86_prepare_with_ops(&plan,
		page_tables, aperture, &backend, &executor_ops, &arch) != CB_SUCCESS);
	expect_ops_zero();
	if (!strcmp(argv[1], "prepare-residual-pg"))
		CHECK(mock.pg_active && !mock.pae_active && backend.poisoned);
	else if (!strcmp(argv[1], "prepare-residual-pae"))
		CHECK(!mock.pg_active && mock.pae_active && backend.poisoned);
	else
		CHECK(!mock.pg_active && !mock.pae_active);
	return 0;
}
