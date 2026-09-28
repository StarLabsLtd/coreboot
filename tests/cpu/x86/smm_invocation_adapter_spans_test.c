/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <commonlib/helpers.h>
#include <cpu/intel/em64t100_save_state.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/intel/smm_invocation_adapter.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define REV100 0x30100U
#define REV101 0x30101U

struct span_source {
	struct smm_save_state_span spans[SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1U];
	uint32_t active_cpus;
};

static struct {
	uint32_t calls;
	uint32_t failure_call;
	bool order_error;
	bool reenter;
	enum cb_err reenter_result;
	struct intel_smm_invocation_adapter *adapter;
	struct span_source *source;
	uint32_t revision;
} control;

static uint32_t revised_layout;
static size_t revision_extra;

void intel_smm_invocation_adapter_test_hook(uint32_t point)
{
	(void)point;
}

size_t intel_smm_invocation_adapter_test_revision_size(
	uint32_t layout_revision, size_t native_size)
{
	return native_size + (layout_revision == revised_layout ? revision_extra : 0U);
}

void __noreturn smm_invocation_platform_fail_stop(void)
{
	abort();
}

static void reset_control(struct span_source *source,
	struct intel_smm_invocation_adapter *adapter, uint32_t revision)
{
	memset(&control, 0, sizeof(control));
	control.source = source;
	control.adapter = adapter;
	control.revision = revision;
}

static enum cb_err span_for_cpu(const void *context, uint32_t cpu,
	struct smm_save_state_span *span)
{
	struct span_source *source = (struct span_source *)context;

	control.calls++;
	if (!source || !span || !source->active_cpus ||
	    cpu != (control.calls - 1U) % source->active_cpus)
		control.order_error = true;
	if (control.reenter && control.calls == 1U) {
		control.reenter = false;
		control.reenter_result = intel_smm_invocation_adapter_init_spans(
			control.adapter, source->active_cpus, span_for_cpu, source,
			sizeof(*source), control.revision);
	}
	*span = source->spans[cpu];
	if (control.calls == control.failure_call)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err span_from_context(const void *context, uint32_t cpu,
	struct smm_save_state_span *span)
{
	control.calls++;
	if (cpu)
		control.order_error = true;
	memcpy(span, context, sizeof(*span));
	return CB_SUCCESS;
}

static enum cb_err fixed_span(const void *context, uint32_t cpu,
	struct smm_save_state_span *span)
{
	(void)context;
	control.calls++;
	if (cpu)
		control.order_error = true;
	*span = control.source->spans[0];
	return CB_SUCCESS;
}

static enum cb_err output_alias_span(const void *context, uint32_t cpu,
	struct smm_save_state_span *span)
{
	(void)context;
	control.calls++;
	if (cpu)
		control.order_error = true;
	*span = (struct smm_save_state_span) {
		.base = (uintptr_t)span,
		.size = sizeof(em64t101_smm_state_save_area_t),
	};
	return CB_SUCCESS;
}

static bool all_zero(const void *object, size_t size)
{
	const uint8_t *bytes = object;

	for (size_t index = 0; index < size; index++)
		if (bytes[index])
			return false;
	return true;
}

static void fill_source(struct span_source *source, void *states,
	size_t stride, uint32_t active_cpus, size_t state_size)
{
	memset(source, 0, sizeof(*source));
	source->active_cpus = active_cpus;
	for (uint32_t cpu = 0; cpu < active_cpus; cpu++)
		source->spans[cpu] = (struct smm_save_state_span) {
			.base = (uintptr_t)states + cpu * stride,
			.size = state_size,
		};
}

static void expect_failure_zero(struct intel_smm_invocation_adapter *adapter,
	uint32_t count, intel_smm_invocation_native_span_fn getter,
	const void *context, size_t context_size, uint32_t revision)
{
	memset(adapter, 0, sizeof(*adapter));
	control.calls = 0;
	assert(intel_smm_invocation_adapter_init_spans(adapter, count, getter,
		context, context_size, revision) == CB_ERR);
	assert(all_zero(adapter, sizeof(*adapter)));
}

static void success_revisions_order_and_nonretention(void)
{
	_Alignas(16) uint8_t states[2][sizeof(em64t101_smm_state_save_area_t)];
	struct span_source source;
	struct intel_smm_invocation_adapter adapter = { 0 };
	struct smm_invocation_save_state_ops ops;

	assert(sizeof(em64t100_smm_state_save_area_t) == sizeof(states[0]));
	fill_source(&source, states, sizeof(states[0]), 2U, sizeof(states[0]));
	reset_control(&source, &adapter, REV101);
	assert(intel_smm_invocation_adapter_init_spans(&adapter, 2,
		span_for_cpu, &source, sizeof(source), REV101) == CB_SUCCESS);
	assert(control.calls == 2U && !control.order_error);
	assert(adapter.revision == INTEL_SMM_INVOCATION_ADAPTER_REVISION);
	assert(adapter.size == sizeof(adapter));
	assert(adapter.active_cpus == 2U && adapter.expected_revision == REV101);
	assert(adapter.invocation_nonce == 1U);
	assert(adapter.nodes[0].save_state == source.spans[0].base);
	assert(adapter.nodes[1].save_state == source.spans[1].base);
	control.failure_call = control.calls + 1U;
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	assert(control.calls == 2U);

	memset(&adapter, 0, sizeof(adapter));
	reset_control(&source, &adapter, REV100);
	assert(intel_smm_invocation_adapter_init_spans(&adapter, 2,
		span_for_cpu, &source, sizeof(source), REV100) == CB_SUCCESS);
	assert(control.calls == 2U && adapter.expected_revision == REV100);
}

static void maximum_cpu_count(void)
{
	static _Alignas(16) uint8_t
		states[SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1U]
		      [sizeof(em64t101_smm_state_save_area_t)];
	static struct span_source source;
	struct intel_smm_invocation_adapter adapter = { 0 };

	fill_source(&source, states, sizeof(states[0]),
		SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1U, sizeof(states[0]));
	reset_control(&source, &adapter, REV101);
	assert(intel_smm_invocation_adapter_init_spans(&adapter,
		SMM_INVOCATION_EVIDENCE_MAX_CPUS, span_for_cpu, &source,
		sizeof(source), REV101) == CB_SUCCESS);
	assert(control.calls == SMM_INVOCATION_EVIDENCE_MAX_CPUS);
	assert(adapter.nodes[SMM_INVOCATION_EVIDENCE_MAX_CPUS - 1U].save_state ==
		source.spans[SMM_INVOCATION_EVIDENCE_MAX_CPUS - 1U].base);
	memset(&adapter, 0, sizeof(adapter));
	reset_control(&source, &adapter, REV101);
	assert(intel_smm_invocation_adapter_init_spans(&adapter,
		SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1U, span_for_cpu, &source,
		sizeof(source), REV101) == CB_ERR);
	assert(control.calls == 0U && all_zero(&adapter, sizeof(adapter)));
}

static void invalid_inputs_ranges_and_aliases(void)
{
	_Alignas(16) uint8_t states[2][sizeof(em64t101_smm_state_save_area_t) + 1U];
	struct span_source source;
	struct intel_smm_invocation_adapter adapter;
	struct intel_smm_invocation_adapter before;

	fill_source(&source, states, sizeof(states[0]), 2U,
		sizeof(em64t101_smm_state_save_area_t));
	reset_control(&source, &adapter, REV101);
	expect_failure_zero(&adapter, 0, span_for_cpu, &source, sizeof(source), REV101);
	expect_failure_zero(&adapter, SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1U,
		span_for_cpu, &source, sizeof(source), REV101);
	expect_failure_zero(&adapter, 1, NULL, &source, sizeof(source), REV101);
	expect_failure_zero(&adapter, 1, span_for_cpu, NULL, sizeof(source), REV101);
	expect_failure_zero(&adapter, 1, span_for_cpu, &source, 0, REV101);
	expect_failure_zero(&adapter, 1, span_for_cpu,
		(const void *)(UINTPTR_MAX - sizeof(source) + 2U), sizeof(source), REV101);
	expect_failure_zero(&adapter, 1, span_for_cpu, &source, sizeof(source), 0);

	source.spans[0].base = 0;
	expect_failure_zero(&adapter, 1, span_for_cpu, &source, sizeof(source), REV101);
	source.spans[0].base = (uintptr_t)states[0];
	source.spans[0].size = 0;
	expect_failure_zero(&adapter, 1, span_for_cpu, &source, sizeof(source), REV101);
	source.spans[0].size = sizeof(em64t101_smm_state_save_area_t) - 1U;
	expect_failure_zero(&adapter, 1, span_for_cpu, &source, sizeof(source), REV101);
	source.spans[0].size = sizeof(em64t101_smm_state_save_area_t);
	source.spans[1] = source.spans[0];
	expect_failure_zero(&adapter, 2, span_for_cpu, &source, sizeof(source), REV101);
	source.spans[1].base = source.spans[0].base + source.spans[0].size - 1U;
	expect_failure_zero(&adapter, 2, span_for_cpu, &source, sizeof(source), REV101);
	source.spans[1].base = source.spans[0].base + source.spans[0].size;
	reset_control(&source, &adapter, REV101);
	assert(intel_smm_invocation_adapter_init_spans(&adapter, 2, span_for_cpu,
		&source, sizeof(source), REV101) == CB_SUCCESS);

	memset(&adapter, 0x5a, sizeof(adapter));
	before = adapter;
	reset_control(&source, &adapter, REV101);
	assert(intel_smm_invocation_adapter_init_spans(&adapter, 1, span_for_cpu,
		&source, sizeof(source), REV101) == CB_ERR);
	assert(!memcmp(&adapter, &before, sizeof(adapter)) && control.calls == 0U);
	assert(intel_smm_invocation_adapter_init_spans(NULL, 1, span_for_cpu,
		&source, sizeof(source), REV101) == CB_ERR);

	memset(&adapter, 0, sizeof(adapter));
	reset_control(&source, &adapter, REV101);
	assert(intel_smm_invocation_adapter_init_spans(&adapter, 1, fixed_span,
		(uint8_t *)&adapter + 1U, 1U, REV101) == CB_ERR);
	assert(control.calls == 0U && all_zero(&adapter, sizeof(adapter)));

	memset(&adapter, 0, sizeof(adapter));
	source.spans[0] = (struct smm_save_state_span) {
		.base = (uintptr_t)&adapter,
		.size = sizeof(em64t101_smm_state_save_area_t),
	};
	reset_control(&source, &adapter, REV101);
	expect_failure_zero(&adapter, 1, span_for_cpu, &source, sizeof(source), REV101);
}

static void context_state_alias_and_top_boundary(void)
{
	union {
		uint64_t alignment;
		uint8_t bytes[sizeof(em64t101_smm_state_save_area_t)];
	} state;
	static struct intel_smm_invocation_adapter adapter;
	static struct span_source source;
	struct smm_save_state_span *embedded =
		(void *)(state.bytes + sizeof(state.bytes) / 2U);

	memset(&source, 0, sizeof(source));
	source.active_cpus = 1U;

	*embedded = (struct smm_save_state_span) {
		.base = (uintptr_t)state.bytes,
		.size = sizeof(state.bytes),
	};
	reset_control(&source, &adapter, REV101);
	expect_failure_zero(&adapter, 1, span_from_context, embedded,
		sizeof(*embedded), REV101);
	reset_control(&source, &adapter, REV101);
	expect_failure_zero(&adapter, 1, output_alias_span, &source,
		sizeof(source), REV101);

	source.spans[0] = (struct smm_save_state_span) {
		.base = UINTPTR_MAX - sizeof(state.bytes) + 1U,
		.size = sizeof(state.bytes),
	};
	reset_control(&source, &adapter, REV101);
	assert(intel_smm_invocation_adapter_init_spans(&adapter, 1, span_for_cpu,
		&source, sizeof(source), REV101) == CB_SUCCESS);
	assert(adapter.nodes[0].save_state == source.spans[0].base);
}

static void callback_failures_and_reentrancy(void)
{
	_Alignas(16) uint8_t states[2][sizeof(em64t101_smm_state_save_area_t)];
	struct span_source source;
	struct intel_smm_invocation_adapter adapter;
	const uint32_t failure_calls[] = { 1U, 2U };

	fill_source(&source, states, sizeof(states[0]), 2U, sizeof(states[0]));
	for (size_t index = 0; index < ARRAY_SIZE(failure_calls); index++) {
		memset(&adapter, 0, sizeof(adapter));
		reset_control(&source, &adapter, REV101);
		control.failure_call = failure_calls[index];
		assert(intel_smm_invocation_adapter_init_spans(&adapter, 2,
			span_for_cpu, &source, sizeof(source), REV101) == CB_ERR);
		assert(all_zero(&adapter, sizeof(adapter)));
	}

	memset(&adapter, 0, sizeof(adapter));
	reset_control(&source, &adapter, REV101);
	control.reenter = true;
	assert(intel_smm_invocation_adapter_init_spans(&adapter, 2, span_for_cpu,
		&source, sizeof(source), REV101) == CB_SUCCESS);
	assert(control.reenter_result == CB_ERR && control.calls == 2U);
}

static void wrapper_equivalence(void)
{
	_Alignas(16) em64t100_smm_state_save_area_t states100[2];
	_Alignas(16) em64t101_smm_state_save_area_t states101[2];
	struct {
		uint8_t psd[256];
		em64t101_smm_state_save_area_t state;
	} stm[2];
	struct span_source source;
	struct intel_smm_invocation_adapter direct;
	struct intel_smm_invocation_adapter legacy;
	uintptr_t tops[2];

	fill_source(&source, states100, sizeof(states100[0]), 2U,
		sizeof(states100[0]));
	for (uint32_t cpu = 0; cpu < 2U; cpu++)
		tops[cpu] = (uintptr_t)&states100[cpu] + sizeof(states100[cpu]);
	memset(&direct, 0, sizeof(direct));
	reset_control(&source, &direct, REV100);
	assert(intel_smm_invocation_adapter_init_spans(&direct, 2, span_for_cpu,
		&source, sizeof(source), REV100) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_init(&legacy, 2, tops,
		sizeof(states100[0]), REV100) == CB_SUCCESS);
	assert(!memcmp(&direct, &legacy, sizeof(direct)));

	fill_source(&source, states101, sizeof(states101[0]), 2U,
		sizeof(states101[0]));
	for (uint32_t cpu = 0; cpu < 2U; cpu++)
		tops[cpu] = (uintptr_t)&states101[cpu] + sizeof(states101[cpu]);
	memset(&direct, 0, sizeof(direct));
	reset_control(&source, &direct, REV101);
	assert(intel_smm_invocation_adapter_init_spans(&direct, 2, span_for_cpu,
		&source, sizeof(source), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_init_layout(&legacy, 2, tops,
		sizeof(states101[0]), 0, REV101) == CB_SUCCESS);
	assert(!memcmp(&direct, &legacy, sizeof(direct)));

	memset(&source, 0, sizeof(source));
	source.active_cpus = 2U;
	for (uint32_t cpu = 0; cpu < 2U; cpu++) {
		tops[cpu] = (uintptr_t)(&stm[cpu] + 1);
		assert(smm_save_state_native_span(tops[cpu], sizeof(stm[cpu]),
			sizeof(stm[cpu].psd), &source.spans[cpu]) == CB_SUCCESS);
	}
	memset(&direct, 0, sizeof(direct));
	reset_control(&source, &direct, REV101);
	assert(intel_smm_invocation_adapter_init_spans(&direct, 2, span_for_cpu,
		&source, sizeof(source), REV101) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_init_layout(&legacy, 2, tops,
		sizeof(stm[0]), sizeof(stm[0].psd), REV101) == CB_SUCCESS);
	assert(!memcmp(&direct, &legacy, sizeof(direct)));
}

static void revision_size_discrimination(void)
{
	_Alignas(16) uint8_t state[sizeof(em64t100_smm_state_save_area_t) + 16U];
	struct span_source source;
	struct intel_smm_invocation_adapter adapter = { 0 };

	fill_source(&source, state, sizeof(state), 1U, sizeof(state));
	revised_layout = REV100;
	revision_extra = 16U;
	reset_control(&source, &adapter, REV100);
	assert(intel_smm_invocation_adapter_init_spans(&adapter, 1, span_for_cpu,
		&source, sizeof(source), REV100) == CB_SUCCESS);
	revised_layout = 0;
	revision_extra = 0;
}

int main(void)
{
	success_revisions_order_and_nonretention();
	maximum_cpu_count();
	invalid_inputs_ranges_and_aliases();
	context_state_alias_and_top_boundary();
	callback_failures_and_reentrancy();
	wrapper_equivalence();
	revision_size_discrimination();
	return 0;
}
