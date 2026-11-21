/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_SERVICE_NATIVE_VARIABLES_H
#define EFI_VARIABLE_SERVICE_NATIVE_VARIABLES_H

#include <types.h>

struct authvar_owner;
struct authvar_fixed_policy;
struct authvar_policy;
struct ass_capsule_platform;
struct ass_capsule_source;
struct ass_capsule_transaction;
struct payload_mm_crypto_owner;

struct ass_mm_capsule {
	struct ass_capsule_transaction *transaction;
	struct ass_capsule_source *source;
	struct payload_mm_crypto_owner *crypto;
	const struct ass_capsule_platform *platform;
	uint8_t *image;
};

enum ass_mm_phase {
	ASS_MM_WAIT_END_OF_DXE,
	ASS_MM_WAIT_READY_TO_LOCK,
	ASS_MM_WAIT_READY_TO_BOOT,
	ASS_MM_WAIT_EXIT_BOOT_SERVICES,
	ASS_MM_RUNTIME,
};

/* The caller owns an aligned private copy of the IA32 variable message. */
bool ass_native_variable_dispatch(struct authvar_owner *owner,
	struct authvar_policy *policy, const struct authvar_fixed_policy *fixed,
	void *message, size_t size);
uint32_t ass_native_mm_dispatch(struct authvar_owner *owner,
	struct authvar_policy *policy, const struct authvar_fixed_policy *fixed,
	enum ass_mm_phase *phase, const struct ass_mm_capsule *capsule, void *message,
	size_t size);

/* SMM must validate the mailbox region and own the disjoint scratch buffer. */
uint32_t ass_native_mailbox_dispatch(struct authvar_owner *owner,
	struct authvar_policy *policy, const struct authvar_fixed_policy *fixed,
	enum ass_mm_phase *phase, const struct ass_mm_capsule *capsule, void *mailbox,
	size_t mailbox_size,
	void *scratch, size_t scratch_size);

#endif
