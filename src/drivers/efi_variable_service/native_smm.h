/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_SERVICE_NATIVE_SMM_H
#define EFI_VARIABLE_SERVICE_NATIVE_SMM_H

#include <types.h>

struct ass_mm_capsule;
struct authvar_fixed_policy;
struct region_device;
struct efi_variable_service_state;
struct ass_capsule_platform;
struct ass_capsule_transaction;
struct efi_capsule_range;
struct payload_mm_crypto_owner;
const struct authvar_fixed_policy *ass_native_fixed_policy(void);

/* Prepare capsule policy from producer-owned state, flash layout and trust. */
const struct ass_capsule_platform *ass_native_smm_capsule_platform(
	const struct efi_variable_service_state *state,
	uint32_t (*program)(uint32_t, const struct efi_capsule_range *, uint32_t,
		uint8_t *, uint32_t), uint32_t boot_slot, uint32_t (*commit)(uint32_t));

/* The flash region and policy must remain valid in SMM for the owner lifetime. */
uint64_t ass_native_smm_open(void *arena, size_t arena_size,
	const struct region_device *region, uint32_t block_size, size_t maximum_data_size,
	bool (*write_enabled)(void), bool secure_boot, const struct authvar_fixed_policy *fixed,
	bool *restart_required);
bool ass_native_smm_resume(void *arena, size_t arena_size,
	const struct region_device *region, uint32_t block_size,
	bool (*write_enabled)(void), const struct authvar_fixed_policy *fixed);
uint32_t ass_native_smm_communicate(void *arena, void *mailbox, size_t mailbox_size,
	const struct ass_mm_capsule *capsule);
/* Firmware-private access; never grant this through a mailbox command. */
bool ass_native_smm_boot_access(void *arena, bool *allowed);
bool ass_native_smm_variable(void *arena, void *message, size_t size);
struct payload_mm_crypto_owner *ass_native_smm_crypto(void *arena);
struct ass_capsule_transaction *ass_native_smm_capsule_transaction(void *arena,
	const struct ass_capsule_platform *platform, bool resume);

#endif
