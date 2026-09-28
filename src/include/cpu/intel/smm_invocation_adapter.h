/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_SMM_INVOCATION_ADAPTER_H
#define CPU_INTEL_SMM_INVOCATION_ADAPTER_H

#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_save_state.h>

#define INTEL_SMM_INVOCATION_ADAPTER_REVISION 3U

struct intel_smm_invocation_node {
	uintptr_t save_state;
};

struct intel_smm_invocation_adapter {
	uint32_t revision;
	uint32_t size;
	uint32_t active_cpus;
	uint32_t reserved;
	uint32_t seal_phase;
	uint32_t matched_cpu;
	uint32_t matched_revision;
	uint32_t matched_io_misc;
	uint32_t matched_command;
	uint32_t expected_revision;
	uint64_t matched_rax;
	uint64_t matched_rcx;
	uint64_t invocation_nonce;
	uint64_t matched_nonce;
	struct intel_smm_invocation_node
		nodes[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
};

enum cb_err intel_smm_invocation_adapter_init(
	struct intel_smm_invocation_adapter *adapter, uint32_t active_cpus,
	const uintptr_t *save_state_top, uint32_t save_state_size,
	uint32_t expected_revision);
enum cb_err intel_smm_invocation_adapter_init_layout(
	struct intel_smm_invocation_adapter *adapter, uint32_t active_cpus,
	const uintptr_t *save_state_top, uint32_t allocation_size,
	uint32_t reserved_size, uint32_t expected_revision);
typedef enum cb_err (*intel_smm_invocation_native_span_fn)(
	const void *context, uint32_t cpu, struct smm_save_state_span *span);

/*
 * Initialize a zeroed adapter through a bounded native-span borrower. The
 * callback must be non-reentrant and its protected geometry source quiescent
 * for the call. Returned geometry remains immutable for the adapter lifetime;
 * violating either condition is a caller contract violation. Neither callback
 * nor context is retained. On failure the adapter is scrubbed back to zero.
 */
enum cb_err intel_smm_invocation_adapter_init_spans(
	struct intel_smm_invocation_adapter *adapter, uint32_t active_cpus,
	intel_smm_invocation_native_span_fn span_for_cpu, const void *context,
	size_t context_size, uint32_t expected_revision);
enum cb_err intel_smm_invocation_adapter_ops(
	struct intel_smm_invocation_adapter *adapter,
	struct smm_invocation_save_state_ops *ops);

#endif
