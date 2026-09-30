/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/em64t100_save_state.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/intel/smm_invocation_adapter.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_save_state.h>
#include <string.h>

#if CONFIG(SMM_INVOCATION_INTEL_ADAPTER_PROVIDER)
#include "invocation_adapter_internal.h"
#endif

#if defined(__TEST__)
void intel_smm_invocation_adapter_test_hook(uint32_t point);
size_t intel_smm_invocation_adapter_test_revision_size(
	uint32_t layout_revision, size_t native_size);
#define ADAPTER_TEST_HOOK(point) \
	intel_smm_invocation_adapter_test_hook(point)
#define ADAPTER_REVISION_SIZE(revision, size) \
	intel_smm_invocation_adapter_test_revision_size(revision, size)
#else
#define ADAPTER_TEST_HOOK(point) do { } while (0)
#define ADAPTER_REVISION_SIZE(revision, size) (size)
#endif

#define EM64T100_REVISION 0x30100U
#define EM64T101_REVISION 0x30101U
#define APMC_OUT_DX_BYTE_IO_MISC (((uint32_t)APM_CNT << 16) | 0x3U)

_Static_assert(sizeof(struct intel_smm_invocation_adapter) <=
	SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX,
	"Intel SMM invocation adapter exceeds generic context bound");

enum adapter_seal_phase {
	ADAPTER_SEAL_EMPTY,
	ADAPTER_SEAL_WRITING,
	ADAPTER_SEAL_READY,
	ADAPTER_SEAL_POISONED,
};

struct invocation_tuple {
	uint32_t revision;
	uint32_t io_misc;
	uint64_t rax;
	uint64_t rcx;
};

static bool range_valid(const void *base, size_t size)
{
	return base && size && (uintptr_t)base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!range_valid(first, first_size) || !range_valid(second, second_size))
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static size_t revision_size(uint32_t revision)
{
	if (revision == EM64T100_REVISION)
		return ADAPTER_REVISION_SIZE(EM64T100_REVISION,
			sizeof(em64t100_smm_state_save_area_t));
	if (revision == EM64T101_REVISION)
		return ADAPTER_REVISION_SIZE(EM64T101_REVISION,
			sizeof(em64t101_smm_state_save_area_t));
	return 0;
}

#if CONFIG(SMM_INVOCATION_INTEL_ADAPTER_PROVIDER)
bool intel_smm_invocation_adapter_revision_supported(uint32_t revision)
{
	return revision_size(revision) != 0;
}

bool intel_smm_invocation_adapter_range_disjoint(
	const struct intel_smm_invocation_adapter *adapter,
	const void *range, size_t range_size)
{
	const size_t save_state_size = adapter ?
		revision_size(adapter->expected_revision) : 0;

	if (!adapter || !range_valid(range, range_size) ||
	    adapter->revision != INTEL_SMM_INVOCATION_ADAPTER_REVISION ||
	    adapter->size != sizeof(*adapter) || !adapter->active_cpus ||
	    adapter->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    adapter->reserved || !save_state_size)
		return false;
	for (uint32_t cpu = 0; cpu < adapter->active_cpus; cpu++)
		if (ranges_overlap(range, range_size,
			(const void *)adapter->nodes[cpu].save_state,
			save_state_size))
			return false;
	return true;
}
#endif

static bool node_tuple(const struct intel_smm_invocation_node *node,
	uint32_t revision, struct invocation_tuple *tuple)
{
	if (!node || !tuple || !node->save_state)
		return false;

	if (revision == EM64T100_REVISION) {
		const em64t100_smm_state_save_area_t *state =
			(const void *)node->save_state;
		memcpy(&tuple->revision, &state->smm_revision,
			sizeof(tuple->revision));
		memcpy(&tuple->io_misc, &state->io_misc_info,
			sizeof(tuple->io_misc));
		memcpy(&tuple->rax, &state->rax, sizeof(tuple->rax));
		memcpy(&tuple->rcx, &state->rcx, sizeof(tuple->rcx));
	} else if (revision == EM64T101_REVISION) {
		const em64t101_smm_state_save_area_t *state =
			(const void *)node->save_state;
		memcpy(&tuple->revision, &state->smm_revision,
			sizeof(tuple->revision));
		memcpy(&tuple->io_misc, &state->io_misc_info,
			sizeof(tuple->io_misc));
		memcpy(&tuple->rax, &state->rax, sizeof(tuple->rax));
		memcpy(&tuple->rcx, &state->rcx, sizeof(tuple->rcx));
	} else {
		return false;
	}

	return tuple->revision == revision;
}

static bool sealed_tuple(const struct intel_smm_invocation_adapter *adapter,
	uint32_t cpu, const struct invocation_tuple *tuple)
{
	return __atomic_load_n(&adapter->seal_phase, __ATOMIC_ACQUIRE) ==
		ADAPTER_SEAL_READY && adapter->matched_cpu == cpu &&
		adapter->expected_revision == tuple->revision &&
		adapter->matched_revision == tuple->revision &&
		adapter->matched_io_misc == tuple->io_misc &&
		adapter->matched_rax == tuple->rax &&
		adapter->matched_rcx == tuple->rcx &&
		adapter->matched_nonce &&
		adapter->matched_nonce == adapter->invocation_nonce;
}

static enum smm_invocation_match match_apmc_write(void *context,
	uint32_t cpu, uint8_t command)
{
	struct intel_smm_invocation_adapter *adapter = context;
	struct invocation_tuple first;
	struct invocation_tuple second;

	uint32_t empty = ADAPTER_SEAL_EMPTY;
	uint32_t revision;
	struct intel_smm_invocation_node node;

	if (!adapter || cpu >= adapter->active_cpus)
		return SMM_INVOCATION_MATCH_ERROR;
	revision = adapter->expected_revision;
	memcpy(&node, &adapter->nodes[cpu], sizeof(node));
	if (!node_tuple(&node, revision, &first))
		return SMM_INVOCATION_MATCH_ERROR;
	ADAPTER_TEST_HOOK(1);
	if (!node_tuple(&node, revision, &second) ||
	    memcmp(&first, &second, sizeof(first)))
		return SMM_INVOCATION_MATCH_ERROR;
	if (!(first.io_misc & 1U))
		return SMM_INVOCATION_NOT_MATCHED;
	if (first.io_misc != APMC_OUT_DX_BYTE_IO_MISC ||
	    (uint8_t)first.rax != command)
		return SMM_INVOCATION_MATCH_ERROR;
	/* The fixed service carries no pointer or size in either saved register. */
	if (command == SMM_APMC_AUTHVAR_SERVICE &&
	    (first.rax != SMM_APMC_AUTHVAR_SERVICE || first.rcx))
		return SMM_INVOCATION_MATCH_ERROR;
	/* Revalidate the same immutable capture; never capture a second truth. */
	if (__atomic_load_n(&adapter->seal_phase, __ATOMIC_ACQUIRE) == ADAPTER_SEAL_READY)
		return revision == adapter->expected_revision &&
			!memcmp(&node, &adapter->nodes[cpu], sizeof(node)) &&
			adapter->matched_command == command && sealed_tuple(adapter, cpu, &first) ?
			SMM_INVOCATION_MATCHED : SMM_INVOCATION_MATCH_ERROR;
	if (revision != adapter->expected_revision ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node)) ||
	    !__atomic_compare_exchange_n(&adapter->seal_phase, &empty,
		ADAPTER_SEAL_WRITING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return SMM_INVOCATION_MATCH_ERROR;
	adapter->matched_cpu = cpu;
	adapter->matched_revision = first.revision;
	adapter->matched_io_misc = first.io_misc;
	adapter->matched_command = command;
	adapter->matched_rax = first.rax;
	adapter->matched_rcx = first.rcx;
	adapter->matched_nonce = adapter->invocation_nonce;
	__atomic_store_n(&adapter->seal_phase, ADAPTER_SEAL_READY,
		__ATOMIC_RELEASE);
	return SMM_INVOCATION_MATCHED;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	struct invocation_tuple first;
	struct invocation_tuple second;
	struct intel_smm_invocation_adapter *adapter = context;
	struct intel_smm_invocation_node node;
	uint32_t revision;

	if (!value || !adapter || cpu >= adapter->active_cpus)
		return CB_ERR;
	revision = adapter->expected_revision;
	memcpy(&node, &adapter->nodes[cpu], sizeof(node));
	if (!node_tuple(&node, revision, &first))
		return CB_ERR;
	ADAPTER_TEST_HOOK(2);
	if (!node_tuple(&node, revision, &second) ||
	    memcmp(&first, &second, sizeof(first)) ||
	    revision != adapter->expected_revision ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node)) ||
	    !sealed_tuple(adapter, cpu, &first))
		return CB_ERR;
	*value = (uint32_t)first.rax | ((uint64_t)(uint32_t)first.rcx << 32);
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	struct intel_smm_invocation_adapter *adapter = context;
	struct invocation_tuple before;
	struct invocation_tuple after;
	struct intel_smm_invocation_node node;
	uint32_t revision;
	void *rax;
	void *rcx;
	uint64_t target_rax;
	uint64_t target_rcx;

	if (!adapter || cpu >= adapter->active_cpus)
		return CB_ERR;
	revision = adapter->expected_revision;
	memcpy(&node, &adapter->nodes[cpu], sizeof(node));
	if (!node_tuple(&node, revision, &before) ||
	    before.io_misc != APMC_OUT_DX_BYTE_IO_MISC ||
	    !sealed_tuple(adapter, cpu, &before) ||
	    revision != adapter->expected_revision ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node)))
		return CB_ERR;
	if (revision == EM64T100_REVISION)
		rax = &((em64t100_smm_state_save_area_t *)
			node.save_state)->rax;
	else if (revision == EM64T101_REVISION)
		rax = &((em64t101_smm_state_save_area_t *)
			node.save_state)->rax;
	else
		return CB_ERR;
	if (revision == EM64T100_REVISION)
		rcx = &((em64t100_smm_state_save_area_t *)
			node.save_state)->rcx;
	else
		rcx = &((em64t101_smm_state_save_area_t *)
			node.save_state)->rcx;
	target_rax = (before.rax & ~((uint64_t)UINT32_MAX)) |
		(uint32_t)value;
	target_rcx = (before.rcx & ~((uint64_t)UINT32_MAX)) |
		(uint32_t)(value >> 32);
	ADAPTER_TEST_HOOK(3);
	if (!node_tuple(&node, revision, &after) ||
	    memcmp(&before, &after, sizeof(before)) ||
	    revision != adapter->expected_revision ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node)))
		return CB_ERR;
	memcpy(rcx, &target_rcx, sizeof(target_rcx));
	ADAPTER_TEST_HOOK(5);
	if (!node_tuple(&node, revision, &after) ||
	    after.revision != before.revision ||
	    after.io_misc != before.io_misc || after.rax != before.rax ||
	    after.rcx != target_rcx || revision != adapter->expected_revision ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node))) {
		__atomic_store_n(&adapter->seal_phase, ADAPTER_SEAL_POISONED,
			__ATOMIC_RELEASE);
		smm_invocation_platform_fail_stop();
	}
	memcpy(rax, &target_rax, sizeof(target_rax));
	ADAPTER_TEST_HOOK(4);
	if (!node_tuple(&node, revision, &after) ||
	    after.revision != before.revision ||
	    after.io_misc != before.io_misc || after.rax != target_rax ||
	    after.rcx != target_rcx ||
	    revision != adapter->expected_revision ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node))) {
		__atomic_store_n(&adapter->seal_phase, ADAPTER_SEAL_POISONED,
			__ATOMIC_RELEASE);
		smm_invocation_platform_fail_stop();
	}
	adapter->matched_rax = target_rax;
	adapter->matched_rcx = target_rcx;
	return CB_SUCCESS;
}

enum cb_err intel_smm_invocation_adapter_init(
	struct intel_smm_invocation_adapter *adapter, uint32_t active_cpus,
	const uintptr_t *save_state_top, uint32_t save_state_size,
	uint32_t expected_revision)
{
	return intel_smm_invocation_adapter_init_layout(adapter, active_cpus,
		save_state_top, save_state_size, 0, expected_revision);
}

enum cb_err intel_smm_invocation_adapter_init_layout(
	struct intel_smm_invocation_adapter *adapter, uint32_t active_cpus,
	const uintptr_t *save_state_top, uint32_t allocation_size,
	uint32_t reserved_size, uint32_t expected_revision)
{
	uintptr_t tops[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
	const size_t expected_size = revision_size(expected_revision);

	if (!adapter || !save_state_top || !active_cpus ||
	    active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    !expected_size)
		return CB_ERR;
	if (allocation_size <= reserved_size ||
	    !range_valid(save_state_top, active_cpus * sizeof(*save_state_top)) ||
	    ranges_overlap(adapter, sizeof(*adapter), save_state_top,
		active_cpus * sizeof(*save_state_top)))
		return CB_ERR;
	memcpy(tops, save_state_top, active_cpus * sizeof(*tops));
	for (uint32_t cpu = 0; cpu < active_cpus; cpu++) {
		struct smm_save_state_span span;

		if (smm_save_state_native_span(tops[cpu], allocation_size,
			reserved_size, &span) != CB_SUCCESS ||
		    span.size != expected_size ||
		    !range_valid((const void *)span.base, span.size) ||
		    ranges_overlap(adapter, sizeof(*adapter),
			(const void *)span.base, span.size))
			return CB_ERR;
		for (uint32_t prior = 0; prior < cpu; prior++)
			if (ranges_overlap(
				(const void *)span.base, span.size,
				(const void *)tops[prior], expected_size))
				return CB_ERR;
		tops[cpu] = span.base;
	}
	memset(adapter, 0, sizeof(*adapter));
	adapter->revision = INTEL_SMM_INVOCATION_ADAPTER_REVISION;
	adapter->size = sizeof(*adapter);
	adapter->active_cpus = active_cpus;
	adapter->expected_revision = expected_revision;
	adapter->invocation_nonce = 1;
	for (uint32_t cpu = 0; cpu < active_cpus; cpu++) {
		adapter->nodes[cpu].save_state = tops[cpu];
	}
	return CB_SUCCESS;
}

enum cb_err intel_smm_invocation_adapter_init_spans(
	struct intel_smm_invocation_adapter *adapter, uint32_t active_cpus,
	intel_smm_invocation_native_span_fn span_for_cpu, const void *context,
	size_t context_size, uint32_t expected_revision)
{
	const size_t expected_size = revision_size(expected_revision);
	const uint8_t *adapter_bytes;

	if (!adapter || !span_for_cpu || !context || !context_size || !active_cpus ||
	    active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS || !expected_size ||
	    !range_valid(adapter, sizeof(*adapter)) ||
	    !range_valid(context, context_size) ||
	    ranges_overlap(adapter, sizeof(*adapter), context, context_size))
		return CB_ERR;
	adapter_bytes = (const void *)adapter;
	for (size_t index = 0; index < sizeof(*adapter); index++)
		if (adapter_bytes[index])
			return CB_ERR;
	adapter->reserved = UINT32_MAX;

	/*
	 * The unpublished adapter is the protected snapshot scratch. The reserved
	 * marker rejects reentrant initialization. Any failure after ownership is
	 * taken scrubs the entire destination before returning.
	 */
	for (uint32_t cpu = 0; cpu < active_cpus; cpu++) {
		struct smm_save_state_span span = { 0 };

		ADAPTER_TEST_HOOK(100U + cpu);
		if (span_for_cpu(context, cpu, &span) != CB_SUCCESS ||
		    span.size != expected_size ||
		    !range_valid((const void *)span.base, span.size) ||
		    ranges_overlap(&span, sizeof(span),
			(const void *)span.base, span.size) ||
		    ranges_overlap(adapter, sizeof(*adapter),
			(const void *)span.base, span.size) ||
		    ranges_overlap(context, context_size,
			(const void *)span.base, span.size))
			goto fail;
		for (uint32_t prior = 0; prior < cpu; prior++)
			if (ranges_overlap((const void *)span.base, span.size,
				(const void *)adapter->nodes[prior].save_state,
				expected_size))
				goto fail;
		adapter->nodes[cpu].save_state = span.base;
	}

	adapter->revision = INTEL_SMM_INVOCATION_ADAPTER_REVISION;
	adapter->size = sizeof(*adapter);
	adapter->active_cpus = active_cpus;
	adapter->reserved = 0;
	adapter->expected_revision = expected_revision;
	adapter->invocation_nonce = 1;
	return CB_SUCCESS;

fail:
	memset(adapter, 0, sizeof(*adapter));
	return CB_ERR;
}

enum cb_err intel_smm_invocation_adapter_ops(
	struct intel_smm_invocation_adapter *adapter,
	struct smm_invocation_save_state_ops *ops)
{
	struct intel_smm_invocation_adapter snapshot;
	size_t save_state_size;

	if (!adapter || !ops || ranges_overlap(adapter, sizeof(*adapter), ops,
		sizeof(*ops)))
		return CB_ERR;
	memcpy(&snapshot, adapter, sizeof(snapshot));
	if (snapshot.revision != INTEL_SMM_INVOCATION_ADAPTER_REVISION ||
	    snapshot.size != sizeof(snapshot) || !snapshot.active_cpus ||
	    snapshot.active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    snapshot.reserved || !revision_size(snapshot.expected_revision))
		return CB_ERR;
	ADAPTER_TEST_HOOK(6);
	save_state_size = revision_size(snapshot.expected_revision);
	for (uint32_t cpu = 0; cpu < snapshot.active_cpus; cpu++) {
		if (!range_valid((const void *)snapshot.nodes[cpu].save_state,
			save_state_size) ||
		    ranges_overlap(ops, sizeof(*ops),
			(const void *)snapshot.nodes[cpu].save_state,
			save_state_size))
			return CB_ERR;
	}
	if (memcmp(&snapshot, adapter, sizeof(snapshot)))
		return CB_ERR;
	if (snapshot.invocation_nonce == UINT64_MAX) {
		adapter->reserved = 1U;
		return CB_ERR;
	}
	adapter->invocation_nonce = snapshot.invocation_nonce + 1U;
	adapter->seal_phase = ADAPTER_SEAL_EMPTY;
	adapter->matched_cpu = UINT32_MAX;
	adapter->matched_revision = 0;
	adapter->matched_io_misc = 0;
	adapter->matched_command = 0;
	adapter->matched_rax = 0;
	adapter->matched_rcx = 0;
	adapter->matched_nonce = 0;
	*ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match_apmc_write,
		.read_value = read_value,
		.write_value = write_value,
		.context = adapter,
		.context_size = sizeof(*adapter),
	};
	return CB_SUCCESS;
}

#if CONFIG(SMM_INVOCATION_INTEL_ADAPTER_PROVIDER)
/* The provider calls these only while it exclusively owns the adapter. */
enum cb_err intel_smm_invocation_adapter_bind(
	struct intel_smm_invocation_adapter *adapter,
	struct smm_invocation_save_state_ops *ops)
{
	if (!adapter || !ops || ranges_overlap(adapter, sizeof(*adapter), ops,
		sizeof(*ops)) || !intel_smm_invocation_adapter_range_disjoint(adapter,
		ops, sizeof(*ops)) ||
	    adapter->revision != INTEL_SMM_INVOCATION_ADAPTER_REVISION ||
	    adapter->size != sizeof(*adapter) || !adapter->active_cpus ||
	    adapter->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    adapter->reserved || !revision_size(adapter->expected_revision))
		return CB_ERR;
	*ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match_apmc_write,
		.read_value = read_value,
		.write_value = write_value,
		.context = adapter,
		.context_size = sizeof(*adapter),
	};
	return CB_SUCCESS;
}

enum cb_err intel_smm_invocation_adapter_begin(
	struct intel_smm_invocation_adapter *adapter)
{
	const size_t save_state_size = adapter ?
		revision_size(adapter->expected_revision) : 0;

	if (!adapter || adapter->revision != INTEL_SMM_INVOCATION_ADAPTER_REVISION ||
	    adapter->size != sizeof(*adapter) || !adapter->active_cpus ||
	    adapter->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    adapter->reserved || !save_state_size)
		return CB_ERR;
	for (uint32_t cpu = 0; cpu < adapter->active_cpus; cpu++)
		if (!range_valid((const void *)adapter->nodes[cpu].save_state,
			save_state_size))
			return CB_ERR;
	if (adapter->invocation_nonce == UINT64_MAX) {
		adapter->reserved = 1U;
		return CB_ERR;
	}
	adapter->invocation_nonce++;
	adapter->seal_phase = ADAPTER_SEAL_EMPTY;
	adapter->matched_cpu = UINT32_MAX;
	adapter->matched_revision = 0;
	adapter->matched_io_misc = 0;
	adapter->matched_command = 0;
	adapter->matched_rax = 0;
	adapter->matched_rcx = 0;
	adapter->matched_nonce = 0;
	return CB_SUCCESS;
}

enum cb_err intel_smm_invocation_adapter_end(
	struct intel_smm_invocation_adapter *adapter)
{
	if (!adapter || adapter->revision != INTEL_SMM_INVOCATION_ADAPTER_REVISION ||
	    adapter->size != sizeof(*adapter) || !adapter->active_cpus ||
	    adapter->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    adapter->reserved || !revision_size(adapter->expected_revision))
		return CB_ERR;
	__atomic_store_n(&adapter->seal_phase, ADAPTER_SEAL_POISONED,
		__ATOMIC_RELEASE);
	adapter->matched_cpu = UINT32_MAX;
	adapter->matched_revision = 0;
	adapter->matched_io_misc = 0;
	adapter->matched_command = 0;
	adapter->matched_rax = 0;
	adapter->matched_rcx = 0;
	adapter->matched_nonce = 0;
	return CB_SUCCESS;
}
#endif
