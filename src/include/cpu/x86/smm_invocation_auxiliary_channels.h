/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_AUXILIARY_CHANNELS_H
#define CPU_X86_SMM_INVOCATION_AUXILIARY_CHANNELS_H

#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_invocation_loader_composition.h>

#define SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT 2U
#define SMM_INVOCATION_AUXILIARY_CHANNELS_REVISION 1U

enum smm_invocation_auxiliary_channels_state {
	SMM_INVOCATION_AUXILIARY_CHANNELS_EMPTY,
	SMM_INVOCATION_AUXILIARY_CHANNELS_PROVISIONING,
	SMM_INVOCATION_AUXILIARY_CHANNELS_READY,
	SMM_INVOCATION_AUXILIARY_CHANNELS_FAILED,
};

struct smm_invocation_auxiliary_channels {
	uint32_t state;
	uint32_t revision;
	uint32_t size;
	uint32_t count;
	uint32_t reserved[2];
	uint64_t composition_identity;
	uint64_t topology_identity;
	uint64_t instance_identity;
	uint64_t primary_evidence_identity;
	struct smm_invocation_loader_seed seed;
	struct {
		uint32_t index;
		uint32_t reserved;
		uint64_t evidence_identity;
	} binding[SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT];
	struct smm_invocation_evidence
		evidence[SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT] __aligned(8);
} __aligned(8);

enum cb_err smm_invocation_auxiliary_channels_compose(
	struct smm_invocation_auxiliary_channels *channels,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *primary_evidence);
void smm_invocation_auxiliary_channels_loader_abort(
	struct smm_invocation_auxiliary_channels *channels);
#endif
