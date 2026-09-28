/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_SMM_INVOCATION_ADAPTER_INTERNAL_H
#define SOC_INTEL_COMMON_BLOCK_SMM_INVOCATION_ADAPTER_INTERNAL_H

#include <cpu/intel/smm_invocation_adapter.h>

enum cb_err intel_smm_invocation_adapter_bind(
	struct intel_smm_invocation_adapter *adapter,
	struct smm_invocation_save_state_ops *ops);
enum cb_err intel_smm_invocation_adapter_begin(
	struct intel_smm_invocation_adapter *adapter);
enum cb_err intel_smm_invocation_adapter_end(
	struct intel_smm_invocation_adapter *adapter);
bool intel_smm_invocation_adapter_revision_supported(uint32_t revision);
bool intel_smm_invocation_adapter_range_disjoint(
	const struct intel_smm_invocation_adapter *adapter,
	const void *range, size_t range_size);

#endif
