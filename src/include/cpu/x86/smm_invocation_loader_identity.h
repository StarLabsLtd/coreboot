/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_LOADER_IDENTITY_H
#define CPU_X86_SMM_INVOCATION_LOADER_IDENTITY_H

#include <stdint.h>
#include <types.h>

struct smm_invocation_loader_instance_nonce {
	uint64_t low;
	uint64_t high;
} __aligned(8);

enum smm_invocation_loader_lifecycle {
	SMM_INVOCATION_LOADER_NON_S3_LOAD = 1,
	SMM_INVOCATION_LOADER_S3_RELOAD,
};

_Static_assert(sizeof(struct smm_invocation_loader_instance_nonce) == 16,
	"SMM invocation loader-instance nonce ABI changed");
_Static_assert(_Alignof(struct smm_invocation_loader_instance_nonce) == 8,
	"SMM invocation loader-instance nonce alignment changed");

static inline bool smm_invocation_loader_instance_nonce_is_zero(
	struct smm_invocation_loader_instance_nonce nonce)
{
	return !nonce.low && !nonce.high;
}

static inline bool smm_invocation_loader_instance_nonce_equal(
	struct smm_invocation_loader_instance_nonce first,
	struct smm_invocation_loader_instance_nonce second)
{
	return first.low == second.low && first.high == second.high;
}

#endif
