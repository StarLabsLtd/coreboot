/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_SMM_LOADER_H
#define BOOT_PAYLOAD_MM_AUTHVAR_SMM_LOADER_H

#include <boot/payload_mm_authvar.h>
#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_SMM_ARENA_OWNER_SIZE 32U

#define PAYLOAD_MM_AUTHVAR_SMM_ARENA_EMPTY 0U
#define PAYLOAD_MM_AUTHVAR_SMM_ARENA_READY 0xa5U
#define PAYLOAD_MM_AUTHVAR_SMM_ARENA_TAKING 0x5aU
#define PAYLOAD_MM_AUTHVAR_SMM_ARENA_CONSUMED 0xc3U

/* Private platform-to-loader input; owner is the seal-channel capability. */
struct payload_mm_authvar_smm_arena_seed {
	uint32_t revision;
	uint32_t size;
	uint64_t cold_boot_generation;
	uint8_t owner[PAYLOAD_MM_AUTHVAR_SMM_ARENA_OWNER_SIZE];
	uint32_t reserved[2];
};

/* Loader-minted receipt copied into the SMM module's protected parameters. */
struct payload_mm_authvar_smm_arena_receipt {
	uint32_t revision;
	uint32_t size;
	uint64_t cold_boot_generation;
	struct payload_mm_authvar_range smram;
	struct payload_mm_authvar_range arena;
	uint8_t owner[PAYLOAD_MM_AUTHVAR_SMM_ARENA_OWNER_SIZE];
	uint32_t reserved[2];
};

struct payload_mm_authvar_smm_arena_slot {
	struct payload_mm_authvar_smm_arena_receipt receipt;
	uint8_t state;
};

_Static_assert(sizeof(struct payload_mm_authvar_smm_arena_seed) == 56,
	"SMM arena seed ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_smm_arena_receipt) == 88,
	"SMM arena receipt ABI changed");

#if ENV_RAMSTAGE || ENV_TEST
bool platform_payload_mm_authvar_smm_arena_required(void);
void platform_payload_mm_authvar_smm_arena_abort(void);
bool platform_payload_mm_authvar_smm_arena_seed(
	struct payload_mm_authvar_smm_arena_seed *seed);
enum cb_err payload_mm_authvar_smm_arena_reserve(
	struct payload_mm_authvar_smm_arena_receipt *receipt,
	uint64_t smram_base, uint64_t smram_size,
	const struct payload_mm_authvar_range *occupied, size_t occupied_count,
	const struct payload_mm_authvar_smm_arena_seed *seed);
#endif

#if ENV_SMM || ENV_TEST
bool smm_take_payload_mm_authvar_arena_receipt(
	struct payload_mm_authvar_smm_arena_receipt *receipt);
bool smm_payload_mm_authvar_arena_receipt_consumed(void);

static inline bool payload_mm_authvar_smm_arena_slot_consumed(
	const volatile struct payload_mm_authvar_smm_arena_slot *slot)
{
	const volatile uint8_t *bytes;
	uint8_t combined = 0;

	if (!slot || __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_SMM_ARENA_CONSUMED)
		return false;
	bytes = (const volatile uint8_t *)&slot->receipt;
	for (size_t index = 0; index < sizeof(slot->receipt); index++)
		combined |= bytes[index];
	return !combined;
}

static inline bool payload_mm_authvar_smm_arena_slot_take(
	volatile struct payload_mm_authvar_smm_arena_slot *slot,
	struct payload_mm_authvar_smm_arena_receipt *receipt)
{
	const volatile uint8_t *source;
	volatile uint8_t *bytes;
	uint8_t expected = PAYLOAD_MM_AUTHVAR_SMM_ARENA_READY;

	if (!receipt)
		return false;
	memset(receipt, 0, sizeof(*receipt));
	if (!slot || !__atomic_compare_exchange_n(&slot->state,
		&expected, PAYLOAD_MM_AUTHVAR_SMM_ARENA_TAKING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
	source = (const volatile uint8_t *)&slot->receipt;
	bytes = (volatile uint8_t *)receipt;
	for (size_t index = 0; index < sizeof(*receipt); index++)
		bytes[index] = source[index];
	bytes = (volatile uint8_t *)&slot->receipt;
	for (size_t index = 0; index < sizeof(slot->receipt); index++)
		bytes[index] = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
	__atomic_store_n(&slot->state, PAYLOAD_MM_AUTHVAR_SMM_ARENA_CONSUMED,
		__ATOMIC_RELEASE);
	return true;
}
#endif

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_SMM_LOADER_H */
