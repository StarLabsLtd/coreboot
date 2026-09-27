/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_LOADER_COMPOSITION_H
#define CPU_X86_SMM_INVOCATION_LOADER_COMPOSITION_H

#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <stdint.h>
#include <types.h>

enum smm_invocation_loader_composition_state {
	SMM_INVOCATION_LOADER_COMPOSITION_EMPTY,
	SMM_INVOCATION_LOADER_COMPOSITION_COMPOSING,
	SMM_INVOCATION_LOADER_COMPOSITION_UNWINDING,
	SMM_INVOCATION_LOADER_COMPOSITION_READY,
	SMM_INVOCATION_LOADER_COMPOSITION_FAILED,
};

/* Loader-written protected POD. It contains no executable capability. */
struct smm_invocation_loader_composition {
	uint32_t state;
	uint32_t owner_attempt;
	uint32_t reserved[2];
	uint64_t evidence_identity;
} __aligned(8);

_Static_assert(sizeof(struct smm_invocation_loader_composition) == 24,
	"SMM invocation loader composition ABI changed");
_Static_assert(_Alignof(struct smm_invocation_loader_composition) == 8,
	"SMM invocation loader composition alignment changed");
_Static_assert(offsetof(struct smm_invocation_loader_composition,
	owner_attempt) == 4,
	"SMM invocation loader composition layout changed");
_Static_assert(offsetof(struct smm_invocation_loader_composition,
	evidence_identity) == 16,
	"SMM invocation loader composition identity layout changed");

enum cb_err smm_invocation_platform_loader_instance_take(
	struct smm_invocation_loader_instance_seed *seed);
enum cb_err smm_invocation_loader_compose(
	struct smm_invocation_loader_composition *composition,
	struct smm_invocation_topology *topology,
	struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence);
const struct smm_invocation_evidence *
smm_invocation_loader_composition_evidence(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_evidence *evidence);

#endif
