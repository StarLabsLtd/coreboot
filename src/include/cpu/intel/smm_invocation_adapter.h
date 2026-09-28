/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_INTEL_SMM_INVOCATION_ADAPTER_H
#define CPU_INTEL_SMM_INVOCATION_ADAPTER_H

#include <cpu/x86/smm_invocation_evidence.h>

#define INTEL_SMM_INVOCATION_ADAPTER_REVISION 2U

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
enum cb_err intel_smm_invocation_adapter_ops(
	struct intel_smm_invocation_adapter *adapter,
	struct smm_invocation_save_state_ops *ops);

#endif
