/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_GRANT_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_GRANT_H

#include <boot/payload_mm_authvar_mor_probe.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS 15U
#define PAYLOAD_MM_AUTHVAR_MOR_GRANT_TAKE_CONTEXT_MAX 32U

#define PAYLOAD_MM_AUTHVAR_MOR_GRANT_COLD_BOOT (1U << 0)
#define PAYLOAD_MM_AUTHVAR_MOR_GRANT_DMA_HELD_BEFORE_CLEAR (1U << 1)
#define PAYLOAD_MM_AUTHVAR_MOR_GRANT_DMA_REVALIDATED_AFTER (1U << 2)
#define PAYLOAD_MM_AUTHVAR_MOR_GRANT_CACHE_WRITEBACK_FENCE_COMPLETE (1U << 3)
#define PAYLOAD_MM_AUTHVAR_MOR_GRANT_ZERO_READBACK_COMPLETE (1U << 4)
#define PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS \
	(PAYLOAD_MM_AUTHVAR_MOR_GRANT_COLD_BOOT | \
	 PAYLOAD_MM_AUTHVAR_MOR_GRANT_DMA_HELD_BEFORE_CLEAR | \
	 PAYLOAD_MM_AUTHVAR_MOR_GRANT_DMA_REVALIDATED_AFTER | \
	 PAYLOAD_MM_AUTHVAR_MOR_GRANT_CACHE_WRITEBACK_FENCE_COMPLETE | \
	 PAYLOAD_MM_AUTHVAR_MOR_GRANT_ZERO_READBACK_COMPLETE)

enum payload_mm_authvar_mor_grant_span_class {
	PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED = 1,
	PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED = 2,
};

enum payload_mm_authvar_mor_grant_exclusion_reason {
	PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE,
	PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PROTECTED_FIRMWARE,
	PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
};

struct payload_mm_authvar_mor_grant_span {
	uint64_t base;
	uint64_t size;
	uint32_t span_class;
	uint32_t exclusion_reason;
} __aligned(8);

/* Fixed, pointer-free completion receipt. */
struct payload_mm_authvar_mor_grant {
	uint32_t revision;
	uint32_t size;
	uint64_t cold_boot_generation;
	struct payload_mm_authvar_mor_entry entry;
	uint32_t flags;
	uint64_t dma_policy_generation;
	uint8_t dma_policy_identity[32];
	uint64_t inventory_generation;
	uint8_t inventory_identity[32];
	uint64_t total_bytes;
	uint64_t cleared_bytes;
	uint64_t excluded_bytes;
	uint32_t total_spans;
	uint32_t cleared_spans;
	uint32_t excluded_spans;
	uint32_t reserved;
	struct payload_mm_authvar_mor_grant_span spans[
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS];
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_mor_grant_span) == 24,
	"MOR grant span ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_grant) == 504,
	"MOR grant ABI changed");
_Static_assert(_Alignof(struct payload_mm_authvar_mor_grant) == 8,
	"MOR grant alignment changed");
_Static_assert(offsetof(struct payload_mm_authvar_mor_grant, entry) == 16 &&
	offsetof(struct payload_mm_authvar_mor_grant, spans) == 144,
	"MOR grant fields moved");

typedef bool (*payload_mm_authvar_mor_grant_protected_storage)(void *context,
	const void *storage, size_t size);

#if !ENV_SMM || ENV_TEST
enum cb_err payload_mm_authvar_mor_grant_validate(
	const struct payload_mm_authvar_mor_grant *grant);
#endif

#if ENV_SMM || ENV_TEST
/* SMM copies one trusted receipt exactly once into protected private storage. */
enum cb_err payload_mm_authvar_mor_grant_install(
	const struct payload_mm_authvar_mor_grant *trusted_grant,
	payload_mm_authvar_mor_grant_protected_storage storage_is_protected,
	void *context);

/* Permanently consume the still-unused install opportunity without a grant. */
enum cb_err payload_mm_authvar_mor_grant_close(void);

/* The first consume attempt is terminal, including a mismatched attempt. */
enum cb_err payload_mm_authvar_mor_grant_consume(
	const struct payload_mm_authvar_mor_grant *expected_grant);

/*
 * Atomically consume the ready grant into initially-zero protected storage.
 * The protection callback is called for the output, its own code address, and
 * the context when one is supplied. It is pure: it must not retain pointers
 * or mutate its context, output, or the grant authority. A non-NULL context must have a
 * nonzero size no larger than PAYLOAD_MM_AUTHVAR_MOR_GRANT_TAKE_CONTEXT_MAX.
 * Every take attempt made while a grant is ready is terminal.
 */
enum cb_err payload_mm_authvar_mor_grant_take(
	struct payload_mm_authvar_mor_grant *output,
	payload_mm_authvar_mor_grant_protected_storage storage_is_protected,
	void *context, size_t context_size);

/*
 * Terminally discard a ready grant, or close a still-unused install slot.
 * The protection callback is pure and follows the take callback contract.
 * A completed discard is idempotent; a malformed or concurrent attempt
 * poisons the authority and returns an error.
 */
enum cb_err payload_mm_authvar_mor_grant_discard(
	payload_mm_authvar_mor_grant_protected_storage storage_is_protected,
	void *context, size_t context_size);

bool payload_mm_authvar_mor_grant_ready(void);
#endif

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_MOR_GRANT_H */
