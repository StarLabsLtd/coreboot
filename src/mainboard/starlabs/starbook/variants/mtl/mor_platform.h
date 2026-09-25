/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_MOR_PLATFORM_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_MOR_PLATFORM_H

#include <boot/payload_mm_authvar_mor_grant.h>
#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>

#define STARBOOK_MTL_MOR_PRIVATE_BOUNDARY_REVISION 2U
#define STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE 32U
#define STARBOOK_MTL_MOR_PRIVATE_CALLBACK_STACK_MAX 1024U

/*
 * Narrow typed boundary for the separately reviewed private SMI channel.
 * The implementation owns its transport reservation and protected receipt
 * authorities.  resolve() may succeed only after the loader receipt, its
 * transport-page receipt, the SMM bootstrap and the chipset SPI-write proof
 * have all been verified for this generation and owner.
 */
struct starbook_mtl_mor_private_boundary_ops {
	uint32_t revision;
	uint32_t size;
	void *context;
	size_t context_size;
	/* Provider-verified worst-case stack below every callback entry point. */
	uint32_t callback_stack_bytes;
	uint32_t reserved;
	enum cb_err (*arena_seed)(void *context, uint64_t generation,
		const uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE]);
	enum cb_err (*reservations_register)(void *context, uint64_t generation);
	enum cb_err (*resolve)(void *context, uint64_t generation,
		const uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE]);
	enum cb_err (*complete)(void *context,
		const struct payload_mm_authvar_mor_grant *grant);
	enum cb_err (*close)(void *context);
};

bool starbook_mtl_mor_private_boundary(
	struct starbook_mtl_mor_private_boundary_ops *ops);

#if ENV_TEST
void starbook_mtl_mor_platform_reset_test(void);
bool starbook_mtl_mor_platform_poisoned_test(void);
bool starbook_mtl_mor_platform_owner_zero_test(void);
bool starbook_mtl_mor_platform_scratch_zero_test(void);
bool starbook_mtl_mor_platform_scratch_idle_test(void);
bool starbook_mtl_mor_platform_scratch_exact_gap_test(void);
bool starbook_mtl_mor_platform_scratch_contend_test(void);
void *starbook_mtl_mor_platform_storage_test(bool scratch_storage);
void starbook_mtl_mor_platform_claim_conflict_test_hook(void);
void starbook_mtl_mor_platform_leave_test_hook(void);
void starbook_mtl_mor_platform_constructor_scratch_test_hook(
	const void *candidate, size_t candidate_size,
	const void *original, size_t original_size);
#endif

#endif
