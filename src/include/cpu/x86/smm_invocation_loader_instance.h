/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_LOADER_INSTANCE_H
#define CPU_X86_SMM_INVOCATION_LOADER_INSTANCE_H

#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_invocation_loader_identity.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define SMM_INVOCATION_LOADER_INSTANCE_REVISION 1U

enum smm_invocation_loader_instance_state {
	SMM_INVOCATION_LOADER_INSTANCE_EMPTY,
	SMM_INVOCATION_LOADER_INSTANCE_PROVISIONING,
	SMM_INVOCATION_LOADER_INSTANCE_READY,
	SMM_INVOCATION_LOADER_INSTANCE_SCRUBBING,
};

struct smm_invocation_loader_instance_seed {
	uint32_t revision;
	uint32_t size;
	uint32_t lifecycle;
	uint32_t reserved;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
} __aligned(8);

/* Loader-written protected POD. It contains no pointer or capability. */
struct smm_invocation_loader_instance {
	uint32_t state;
	uint32_t revision;
	uint32_t size;
	uint32_t lifecycle;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint64_t reserved;
} __aligned(8);

_Static_assert(sizeof(struct smm_invocation_loader_instance) == 40,
	"SMM invocation loader instance ABI changed");
_Static_assert(sizeof(struct smm_invocation_loader_instance_seed) == 32,
	"SMM invocation loader instance seed ABI changed");
_Static_assert(offsetof(struct smm_invocation_loader_instance_seed,
	loader_instance_nonce) == 16,
	"SMM invocation loader instance seed layout changed");
_Static_assert(_Alignof(struct smm_invocation_loader_instance_seed) == 8,
	"SMM invocation loader instance seed alignment changed");
_Static_assert(offsetof(struct smm_invocation_loader_instance,
	loader_instance_nonce) == 16,
	"SMM invocation loader instance layout changed");
_Static_assert(_Alignof(struct smm_invocation_loader_instance) == 8,
	"SMM invocation loader instance alignment changed");

enum cb_err smm_invocation_loader_instance_publish(
	struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_loader_instance_seed *seed);
enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_loader_instance *snapshot);
void smm_invocation_loader_instance_scrub(
	struct smm_invocation_loader_instance *instance);
int smm_invocation_loader_instance_loader_result(
	struct smm_invocation_loader_instance *instance, int result);

#endif
