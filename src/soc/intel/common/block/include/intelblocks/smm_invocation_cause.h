/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_SMM_INVOCATION_CAUSE_H
#define SOC_INTEL_COMMON_BLOCK_SMM_INVOCATION_CAUSE_H

#include <cpu/x86/smm_invocation_entry.h>
#include <cpu/x86/smm_invocation_loader_composition.h>

enum intel_smm_invocation_cause_result {
	INTEL_SMM_INVOCATION_CAUSE_NOT_PRIVATE,
	INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID,
	INTEL_SMM_INVOCATION_CAUSE_PRIVATE_INVALID,
};

/*
 * Classify one raw Intel APMC cause without clearing SMI_STS or writing APMC.
 * This interface and its implementation are linked only into SMM.
 * PRIVATE_INVALID owns the allocated command and must be routed directly to
 * the selected platform fail-stop; it must never fall back to legacy dispatch.
 */
enum intel_smm_invocation_cause_result intel_smm_invocation_private_cause(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const uint32_t *runtime_cpus,
	struct smm_invocation_entry_cause *cause);

#endif
