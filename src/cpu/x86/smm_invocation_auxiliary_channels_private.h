/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_AUXILIARY_CHANNELS_PRIVATE_H
#define CPU_X86_SMM_INVOCATION_AUXILIARY_CHANNELS_PRIVATE_H

#include <cpu/x86/smm_invocation_auxiliary_channels.h>

struct smm_invocation_evidence *smm_invocation_auxiliary_channel_evidence(
	const volatile struct smm_invocation_auxiliary_channels *channels,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *primary, uint32_t index);

#endif
