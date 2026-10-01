/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/intel/em64t100_save_state.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_save_state.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define TEST_CPUS 4U
#define TEST_ALLOCATION \
	(sizeof(em64t101_smm_state_save_area_t) + TEST_RUNTIME_VIEW_RESERVED_SIZE)

_Static_assert(sizeof(em64t100_smm_state_save_area_t) ==
	sizeof(em64t101_smm_state_save_area_t),
	"test requires the Intel native save-state layouts to share one allocation");

static _Alignas(4096) struct {
	struct smm_runtime runtime;
	uint8_t save_state[TEST_CPUS][TEST_ALLOCATION];
} test_memory;

static uint32_t hook_mode;

void smm_invocation_runtime_view_test_hook(uint32_t point)
{
	if (hook_mode == 1U && point == 1U) {
		test_memory.runtime.num_cpus--;
		test_memory.runtime.invocation_topology.active_cpus--;
		test_memory.runtime.invocation_topology.initial_apic_ids[TEST_CPUS - 1U] = 0;
		test_memory.runtime.save_state_top[TEST_CPUS - 1U] = 0;
	}
	if (hook_mode == 2U && point == 10U)
		test_memory.runtime.save_state_top[0]--;
	if (hook_mode == 3U && point == 2U) {
		test_memory.runtime.num_cpus--;
		test_memory.runtime.invocation_topology.active_cpus--;
		test_memory.runtime.invocation_topology.initial_apic_ids[TEST_CPUS - 1U] = 0;
		test_memory.runtime.save_state_top[TEST_CPUS - 1U] = 0;
	}
	if (hook_mode == 4U && point == 100U)
		test_memory.runtime.save_state_top[0]--;
	if (hook_mode == 5U && point == 100U)
		test_memory.runtime.save_state_size--;
	if (hook_mode == 6U && point == 100U) {
		test_memory.runtime.smbase--;
		test_memory.runtime.smm_size++;
	}
	if (hook_mode == 7U && point == 3U)
		test_memory.runtime.invocation_composition.evidence_identity++;
	if (hook_mode == 8U && point == 4U)
		test_memory.runtime.smm_size--;
	if (hook_mode == 9U && point == 5U)
		test_memory.runtime.smm_size--;
}

#define smm_runtime test_memory.runtime
#define SMM_RUNTIME_ALIGNMENT 8U
#define RUNTIME_VIEW_TEST_HOOK(point) smm_invocation_runtime_view_test_hook(point)
#define RUNTIME_VIEW_RESERVED_SIZE TEST_RUNTIME_VIEW_RESERVED_SIZE
#include "runtime-view-fragment.h"
#undef SMM_RUNTIME_ALIGNMENT
#undef smm_runtime

static void reset_runtime(void)
{
	struct smm_runtime *runtime = &test_memory.runtime;

	memset(&test_memory, 0, sizeof(test_memory));
	runtime->smbase = (uint32_t)(uintptr_t)&test_memory;
	runtime->smm_size = sizeof(test_memory);
	runtime->save_state_size = TEST_ALLOCATION;
	runtime->num_cpus = TEST_CPUS;
	for (uint32_t cpu = 0; cpu < TEST_CPUS; cpu++)
		runtime->save_state_top[cpu] =
			(uintptr_t)&test_memory.save_state[cpu][TEST_ALLOCATION];
	runtime->invocation_topology = (struct smm_invocation_topology) {
		.state = SMM_INVOCATION_TOPOLOGY_READY,
		.revision = SMM_INVOCATION_TOPOLOGY_REVISION,
		.size = sizeof(runtime->invocation_topology),
		.active_cpus = TEST_CPUS,
		.bsp_cpu = 0,
	};
	for (uint32_t cpu = 0; cpu < TEST_CPUS; cpu++)
		runtime->invocation_topology.initial_apic_ids[cpu] = cpu * 2U;
	runtime->invocation_composition =
		(struct smm_invocation_loader_composition) {
			.state = SMM_INVOCATION_LOADER_COMPOSITION_READY,
			.owner_attempt = 1U,
			.evidence_identity =
				(uint64_t)(uintptr_t)&runtime->invocation_evidence,
		};
	hook_mode = 0;
}

static const struct smm_invocation_runtime_view *valid_view(void)
{
	const struct smm_invocation_runtime_view *view = NULL;

	assert(smm_invocation_runtime_view_get(&view) == CB_SUCCESS);
	assert(view != NULL);
	return view;
}

static void exact_bounded_view(void)
{
	const struct smm_invocation_runtime_view *view;
	struct smm_save_state_span span;
	struct smm_save_state_span unchanged;
	uint32_t active_cpus;

	reset_runtime();
	view = valid_view();
	assert(smm_invocation_runtime_cpu_count(view, &active_cpus) == CB_SUCCESS);
	assert(active_cpus == TEST_CPUS);
	assert(smm_invocation_runtime_save_state_span(view, 0, &span) == CB_SUCCESS);
	assert(span.base == (uintptr_t)&test_memory.save_state[0]
		+ RUNTIME_VIEW_RESERVED_SIZE);
	assert(span.size == sizeof(em64t100_smm_state_save_area_t));
	assert(smm_invocation_runtime_save_state_span(view, TEST_CPUS - 1U,
		&span) == CB_SUCCESS);
	assert(span.base == (uintptr_t)&test_memory.save_state[TEST_CPUS - 1U]
		+ RUNTIME_VIEW_RESERVED_SIZE);
	unchanged = span;
	assert(smm_invocation_runtime_save_state_span(view, TEST_CPUS, &span) ==
		CB_ERR);
	assert(span.base == unchanged.base && span.size == unchanged.size);
}

static void exact_top_address_arithmetic(void)
{
	const uintptr_t base = UINTPTR_MAX - 31U;

	assert(runtime_range_valid(base, 32U, 1U));
	assert(runtime_range_contains(base, 32U, base, 32U));
	assert(runtime_range_contains(base, 32U, UINTPTR_MAX - 7U, 8U));
	assert(!runtime_range_contains(base, 31U, UINTPTR_MAX - 7U, 8U));
	assert(runtime_ranges_overlap(base, 16U, base + 15U, 17U));
	assert(!runtime_ranges_overlap(base, 16U, base + 16U, 16U));
	assert(!runtime_range_valid(base, 33U, 1U));
}

static void exact_protected_range(void)
{
	const struct smm_invocation_runtime_view *view;
	uint8_t outside = 0;

	reset_runtime();
	view = valid_view();
	assert(smm_invocation_runtime_range_is_protected(view, &test_memory,
		sizeof(test_memory)) == CB_SUCCESS);
	assert(smm_invocation_runtime_range_is_protected(view,
		&test_memory.save_state[TEST_CPUS - 1U][TEST_ALLOCATION - 1U],
		1U) == CB_SUCCESS);
	assert(smm_invocation_runtime_range_is_protected(view, &outside,
		sizeof(outside)) == CB_ERR);
	assert(smm_invocation_runtime_range_is_protected(NULL, &test_memory,
		1U) == CB_ERR_ARG);
	assert(smm_invocation_runtime_range_is_protected((const void *)1,
		&test_memory, 1U) == CB_ERR_ARG);
	assert(smm_invocation_runtime_range_is_protected(view, NULL, 1U) ==
		CB_ERR_ARG);
	assert(smm_invocation_runtime_range_is_protected(view,
		(const void *)UINTPTR_MAX, 2U) == CB_ERR_ARG);
	hook_mode = 8U;
	assert(smm_invocation_runtime_range_is_protected(view, &test_memory,
		1U) == CB_ERR);
}

static void exact_hardware_containment(void)
{
	const struct smm_invocation_runtime_view *view;
	const uintptr_t base = (uintptr_t)&test_memory;
	const size_t size = sizeof(test_memory);

	reset_runtime();
	view = valid_view();
	assert(smm_invocation_runtime_geometry_is_contained(view, base, size) == CB_SUCCESS);
	assert(smm_invocation_runtime_geometry_is_contained(view, base - 1U,
		size + 2U) == CB_SUCCESS);
	assert(smm_invocation_runtime_geometry_is_contained(view, base + 1U,
		size) == CB_ERR);
	assert(smm_invocation_runtime_geometry_is_contained(view, base,
		size - 1U) == CB_ERR);
	assert(smm_invocation_runtime_geometry_is_contained(view, base - 1U,
		size) == CB_ERR);
	assert(smm_invocation_runtime_geometry_is_contained(NULL, base, size) == CB_ERR_ARG);
	assert(smm_invocation_runtime_geometry_is_contained((const void *)1,
		base, size) == CB_ERR_ARG);
	assert(smm_invocation_runtime_geometry_is_contained(view, 0, size) == CB_ERR_ARG);
	assert(smm_invocation_runtime_geometry_is_contained(view, base, 0) == CB_ERR_ARG);
	assert(smm_invocation_runtime_geometry_is_contained(view,
		UINTPTR_MAX, 2U) == CB_ERR_ARG);
	hook_mode = 9U;
	assert(smm_invocation_runtime_geometry_is_contained(view, base, size) == CB_ERR);
}

static void invalid_counts_and_binding(void)
{
	const struct smm_invocation_runtime_view *view = (const void *)0xa5a5U;
	const struct smm_invocation_runtime_view *unchanged = view;

	reset_runtime();
	test_memory.runtime.num_cpus = 0;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	assert(view == unchanged);
	reset_runtime();
	test_memory.runtime.num_cpus = CONFIG_MAX_CPUS + 1U;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.invocation_topology.active_cpus--;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.invocation_topology.initial_apic_ids[1] = 0;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.invocation_composition.evidence_identity++;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
}

static void invalid_geometry(void)
{
	const struct smm_invocation_runtime_view *view = NULL;
	const uintptr_t original_top =
		(uintptr_t)&test_memory.save_state[0][TEST_ALLOCATION];

	reset_runtime();
	test_memory.runtime.save_state_size = RUNTIME_VIEW_RESERVED_SIZE;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.save_state_top[0] = TEST_ALLOCATION - 1U;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.save_state_top[0] =
		(uintptr_t)&test_memory + TEST_ALLOCATION - 1U;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.save_state_top[0] =
		test_memory.runtime.save_state_top[1];
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.save_state_top[1] = original_top + TEST_ALLOCATION / 2U;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.save_state_top[0] =
		(uintptr_t)&test_memory + sizeof(test_memory) + TEST_ALLOCATION;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.smbase = UINT32_MAX - 1U;
	test_memory.runtime.smm_size = 4U;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	test_memory.runtime.save_state_top[TEST_CPUS] =
		test_memory.runtime.save_state_top[0];
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
}

static void packed_native_alignment(void)
{
	const struct smm_invocation_runtime_view *view;
	struct smm_save_state_span span;

	_Static_assert(_Alignof(em64t100_smm_state_save_area_t) == 1,
		"EM64T100 unexpectedly gained an alignment requirement");
	_Static_assert(_Alignof(em64t101_smm_state_save_area_t) == 1,
		"EM64T101 unexpectedly gained an alignment requirement");
	reset_runtime();
	test_memory.runtime.num_cpus = 1U;
	test_memory.runtime.invocation_topology.active_cpus = 1U;
	for (uint32_t cpu = 1; cpu < TEST_CPUS; cpu++) {
		test_memory.runtime.invocation_topology.initial_apic_ids[cpu] = 0;
		test_memory.runtime.save_state_top[cpu] = 0;
	}
	test_memory.runtime.save_state_top[0]++;
	view = valid_view();
	assert(smm_invocation_runtime_save_state_span(view, 0, &span) == CB_SUCCESS);
	assert(span.base == (uintptr_t)&test_memory.save_state[0]
		+ RUNTIME_VIEW_RESERVED_SIZE + 1U);
}

static void aliases_and_drift(void)
{
	const struct smm_invocation_runtime_view *view;
	const struct smm_invocation_runtime_view **runtime_alias =
		(void *)&test_memory.runtime.invocation_composition;
	struct smm_save_state_span *state_alias =
		(void *)&test_memory.save_state[0][0];
	struct smm_save_state_span span = { .base = 0xa5a5U, .size = 0x5a5aU };
	const struct smm_save_state_span unchanged = span;
	_Alignas(uintptr_t) uint8_t misaligned[sizeof(span) + sizeof(uintptr_t)];
	uint8_t bytes[sizeof(span)];
	uint32_t active_cpus = 0xa5a5a5a5U;

	reset_runtime();
	assert(smm_invocation_runtime_view_get(NULL) == CB_ERR_ARG);
	assert(smm_invocation_runtime_view_get((void *)(misaligned + 1)) ==
		CB_ERR_ARG);
	memcpy(bytes, runtime_alias, sizeof(*runtime_alias));
	assert(smm_invocation_runtime_view_get(runtime_alias) == CB_ERR_ARG);
	assert(!memcmp(bytes, runtime_alias, sizeof(*runtime_alias)));
	reset_runtime();
	view = valid_view();
	assert(smm_invocation_runtime_cpu_count(NULL, &active_cpus) == CB_ERR_ARG);
	assert(smm_invocation_runtime_cpu_count(view, NULL) == CB_ERR_ARG);
	assert(smm_invocation_runtime_cpu_count(view,
		(void *)(misaligned + 1)) == CB_ERR_ARG);
	assert(smm_invocation_runtime_save_state_span(view, 0, NULL) == CB_ERR_ARG);
	assert(smm_invocation_runtime_save_state_span(view, 0,
		(void *)(misaligned + 1)) == CB_ERR_ARG);
	memcpy(bytes, state_alias, sizeof(bytes));
	assert(smm_invocation_runtime_save_state_span(view, 0, state_alias) ==
		CB_ERR);
	assert(!memcmp(bytes, state_alias, sizeof(bytes)));
	assert(smm_invocation_runtime_cpu_count(view,
		(void *)&test_memory.runtime.num_cpus) == CB_ERR_ARG);
	assert(smm_invocation_runtime_cpu_count((const void *)1, &active_cpus) ==
		CB_ERR_ARG);
	assert(active_cpus == 0xa5a5a5a5U);
	for (uint32_t mode = 3U; mode <= 6U; mode++) {
		reset_runtime();
		view = valid_view();
		hook_mode = mode;
		if (mode == 3U) {
			assert(smm_invocation_runtime_cpu_count(view, &active_cpus) ==
				CB_ERR);
			continue;
		}
		assert(smm_invocation_runtime_save_state_span(view, 0, &span) ==
			CB_ERR);
		assert(span.base == unchanged.base && span.size == unchanged.size);
	}
	reset_runtime();
	hook_mode = 1U;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
	reset_runtime();
	hook_mode = 2U;
	assert(smm_invocation_runtime_view_get(&view) == CB_ERR);
}

static void exact_runtime_binding(void)
{
	struct smm_invocation_runtime_binding binding;
	struct smm_invocation_runtime_binding unchanged;

	reset_runtime();
	assert(smm_invocation_runtime_binding_get(&binding) == CB_SUCCESS);
	assert(binding.composition == &test_memory.runtime.invocation_composition);
	assert(binding.instance == &test_memory.runtime.invocation_loader_instance);
	assert(binding.evidence == &test_memory.runtime.invocation_evidence);
	assert(binding.topology == &test_memory.runtime.invocation_topology);
	unchanged = binding;
	hook_mode = 7U;
	assert(smm_invocation_runtime_binding_get(&binding) == CB_ERR);
	assert(!memcmp(&binding, &unchanged, sizeof(binding)));
	assert(smm_invocation_runtime_binding_get(NULL) == CB_ERR_ARG);
}

int main(void)
{
	exact_bounded_view();
	exact_top_address_arithmetic();
	exact_protected_range();
	exact_hardware_containment();
	invalid_counts_and_binding();
	invalid_geometry();
	packed_native_alignment();
	aliases_and_drift();
	exact_runtime_binding();
	return 0;
}
