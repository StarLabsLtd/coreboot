/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/intel/em64t100_save_state.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/intel/smm_invocation_adapter.h>
#include <cpu/x86/smm.h>
#include <string.h>

#if defined(__TEST__)
void intel_smm_invocation_adapter_test_hook(uint32_t point);
#define ADAPTER_TEST_HOOK(point) \
	intel_smm_invocation_adapter_test_hook(point)
#else
#define ADAPTER_TEST_HOOK(point) do { } while (0)
#endif

#define EM64T100_REVISION 0x30100U
#define EM64T101_REVISION 0x30101U
#define APMC_OUT_DX_BYTE_IO_MISC (((uint32_t)APM_CNT << 16) | 0x3U)

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
};

static bool range_valid(const void *base, size_t size)
{
	return base && size && (uintptr_t)base <= UINTPTR_MAX - size;
}

static bool ranges_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!range_valid(first, first_size) || !range_valid(second, second_size))
		return true;
	return first_base < second_base + second_size &&
		second_base < first_base + first_size;
}

static bool node_tuple(const struct intel_smm_invocation_node *node,
	struct invocation_tuple *tuple)
{
	if (!node || !tuple || !node->save_state)
		return false;

	if (node->revision == EM64T100_REVISION) {
		const em64t100_smm_state_save_area_t *state =
			(const void *)node->save_state;
		memcpy(&tuple->revision, &state->smm_revision,
			sizeof(tuple->revision));
		memcpy(&tuple->io_misc, &state->io_misc_info,
			sizeof(tuple->io_misc));
		memcpy(&tuple->rax, &state->rax, sizeof(tuple->rax));
	} else if (node->revision == EM64T101_REVISION) {
		const em64t101_smm_state_save_area_t *state =
			(const void *)node->save_state;
		memcpy(&tuple->revision, &state->smm_revision,
			sizeof(tuple->revision));
		memcpy(&tuple->io_misc, &state->io_misc_info,
			sizeof(tuple->io_misc));
		memcpy(&tuple->rax, &state->rax, sizeof(tuple->rax));
	} else {
		return false;
	}

	return tuple->revision == node->revision;
}

static bool sealed_tuple(const struct intel_smm_invocation_adapter *adapter,
	uint32_t cpu, const struct invocation_tuple *tuple)
{
	return __atomic_load_n(&adapter->seal_phase, __ATOMIC_ACQUIRE) ==
		ADAPTER_SEAL_READY && adapter->matched_cpu == cpu &&
		adapter->matched_revision == tuple->revision &&
		adapter->matched_io_misc == tuple->io_misc &&
		adapter->matched_rax == tuple->rax && adapter->matched_nonce &&
		adapter->matched_nonce == adapter->invocation_nonce;
}

static enum smm_invocation_match match_apmc_write(void *context,
	uint32_t cpu, uint8_t command)
{
	struct intel_smm_invocation_adapter *adapter = context;
	struct invocation_tuple first;
	struct invocation_tuple second;

	uint32_t empty = ADAPTER_SEAL_EMPTY;
	struct intel_smm_invocation_node node;

	if (!adapter || cpu >= adapter->active_cpus)
		return SMM_INVOCATION_MATCH_ERROR;
	memcpy(&node, &adapter->nodes[cpu], sizeof(node));
	if (!node_tuple(&node, &first))
		return SMM_INVOCATION_MATCH_ERROR;
	ADAPTER_TEST_HOOK(1);
	if (!node_tuple(&node, &second) ||
	    memcmp(&first, &second, sizeof(first)))
		return SMM_INVOCATION_MATCH_ERROR;
	if (!(first.io_misc & 1U))
		return SMM_INVOCATION_NOT_MATCHED;
	if (first.io_misc != APMC_OUT_DX_BYTE_IO_MISC ||
	    (uint8_t)first.rax != command)
		return SMM_INVOCATION_MATCH_ERROR;
	if (memcmp(&node, &adapter->nodes[cpu], sizeof(node)) ||
	    !__atomic_compare_exchange_n(&adapter->seal_phase, &empty,
		ADAPTER_SEAL_WRITING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return SMM_INVOCATION_MATCH_ERROR;
	adapter->matched_cpu = cpu;
	adapter->matched_revision = first.revision;
	adapter->matched_io_misc = first.io_misc;
	adapter->matched_command = command;
	adapter->matched_rax = first.rax;
	adapter->matched_nonce = adapter->invocation_nonce;
	__atomic_store_n(&adapter->seal_phase, ADAPTER_SEAL_READY,
		__ATOMIC_RELEASE);
	return SMM_INVOCATION_MATCHED;
}

static enum cb_err read_rax(void *context, uint32_t cpu, uint64_t *value)
{
	struct invocation_tuple first;
	struct invocation_tuple second;
	struct intel_smm_invocation_adapter *adapter = context;
	struct intel_smm_invocation_node node;

	if (!value || !adapter || cpu >= adapter->active_cpus)
		return CB_ERR;
	memcpy(&node, &adapter->nodes[cpu], sizeof(node));
	if (!node_tuple(&node, &first))
		return CB_ERR;
	ADAPTER_TEST_HOOK(2);
	if (!node_tuple(&node, &second) ||
	    memcmp(&first, &second, sizeof(first)) ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node)) ||
	    !sealed_tuple(adapter, cpu, &first))
		return CB_ERR;
	memcpy(value, &first.rax, sizeof(*value));
	return CB_SUCCESS;
}

static enum cb_err write_rax(void *context, uint32_t cpu, uint64_t value)
{
	struct intel_smm_invocation_adapter *adapter = context;
	struct invocation_tuple before;
	struct invocation_tuple after;
	struct intel_smm_invocation_node node;
	void *rax;

	if (!adapter || cpu >= adapter->active_cpus)
		return CB_ERR;
	memcpy(&node, &adapter->nodes[cpu], sizeof(node));
	if (!node_tuple(&node, &before) ||
	    before.io_misc != APMC_OUT_DX_BYTE_IO_MISC ||
	    !sealed_tuple(adapter, cpu, &before) ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node)))
		return CB_ERR;
	if (node.revision == EM64T100_REVISION)
		rax = &((em64t100_smm_state_save_area_t *)
			node.save_state)->rax;
	else if (node.revision == EM64T101_REVISION)
		rax = &((em64t101_smm_state_save_area_t *)
			node.save_state)->rax;
	else
		return CB_ERR;
	ADAPTER_TEST_HOOK(3);
	if (!node_tuple(&node, &after) ||
	    memcmp(&before, &after, sizeof(before)) ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node)))
		return CB_ERR;
	memcpy(rax, &value, sizeof(value));
	ADAPTER_TEST_HOOK(4);
	if (!node_tuple(&node, &after) ||
	    after.revision != before.revision ||
	    after.io_misc != before.io_misc || after.rax != value ||
	    memcmp(&node, &adapter->nodes[cpu], sizeof(node))) {
		__atomic_store_n(&adapter->seal_phase, ADAPTER_SEAL_POISONED,
			__ATOMIC_RELEASE);
		return CB_ERR;
	}
	adapter->matched_rax = value;
	return CB_SUCCESS;
}

enum cb_err intel_smm_invocation_adapter_init(
	struct intel_smm_invocation_adapter *adapter, uint32_t active_cpus,
	const uintptr_t *save_state_top, uint32_t save_state_size,
	uint32_t expected_revision)
{
	uintptr_t tops[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
	size_t expected_size;

	if (!adapter || !save_state_top || !active_cpus ||
	    active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    (expected_revision != EM64T100_REVISION &&
	     expected_revision != EM64T101_REVISION))
		return CB_ERR;
	expected_size = expected_revision == EM64T100_REVISION ?
		sizeof(em64t100_smm_state_save_area_t) :
		sizeof(em64t101_smm_state_save_area_t);
	if (save_state_size != expected_size ||
	    !range_valid(save_state_top, active_cpus * sizeof(*save_state_top)) ||
	    ranges_overlap(adapter, sizeof(*adapter), save_state_top,
		active_cpus * sizeof(*save_state_top)))
		return CB_ERR;
	memcpy(tops, save_state_top, active_cpus * sizeof(*tops));
	for (uint32_t cpu = 0; cpu < active_cpus; cpu++) {
		if (tops[cpu] < save_state_size ||
		    !range_valid((const void *)(tops[cpu] - save_state_size),
			save_state_size) ||
		    ranges_overlap(adapter, sizeof(*adapter),
			(const void *)(tops[cpu] - save_state_size),
			save_state_size))
			return CB_ERR;
		for (uint32_t prior = 0; prior < cpu; prior++)
			if (ranges_overlap(
				(const void *)(tops[cpu] - save_state_size),
				save_state_size,
				(const void *)(tops[prior] - save_state_size),
				save_state_size))
				return CB_ERR;
	}
	memset(adapter, 0, sizeof(*adapter));
	adapter->revision = INTEL_SMM_INVOCATION_ADAPTER_REVISION;
	adapter->size = sizeof(*adapter);
	adapter->active_cpus = active_cpus;
	adapter->invocation_nonce = 1;
	for (uint32_t cpu = 0; cpu < active_cpus; cpu++) {
		uintptr_t base;

		base = tops[cpu] - save_state_size;
		adapter->nodes[cpu].save_state = base;
		adapter->nodes[cpu].revision = expected_revision;
	}
	return CB_SUCCESS;
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
	    snapshot.reserved)
		return CB_ERR;
	ADAPTER_TEST_HOOK(5);
	for (uint32_t cpu = 0; cpu < snapshot.active_cpus; cpu++) {
		if (snapshot.nodes[cpu].revision == EM64T100_REVISION)
			save_state_size = sizeof(em64t100_smm_state_save_area_t);
		else if (snapshot.nodes[cpu].revision == EM64T101_REVISION)
			save_state_size = sizeof(em64t101_smm_state_save_area_t);
		else
			return CB_ERR;
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
	adapter->matched_nonce = 0;
	*ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match_apmc_write,
		.read_rax = read_rax,
		.write_rax = write_rax,
		.context = adapter,
		.context_size = sizeof(*adapter),
	};
	return CB_SUCCESS;
}
