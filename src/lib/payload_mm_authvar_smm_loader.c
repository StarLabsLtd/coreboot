/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_smm_loader.h>
#include <commonlib/helpers.h>
#include <string.h>

#if !ENV_RAMSTAGE && !ENV_TEST
#error "Payload-MM authenticated-variable arena reservation is ramstage-only"
#endif

#if ENV_TEST
void payload_mm_authvar_smm_loader_scrub_test_hook(const void *buffer,
	size_t size);
#endif

__weak bool platform_payload_mm_authvar_smm_arena_seed(
	struct payload_mm_authvar_smm_arena_seed *seed)
{
	if (seed)
		memset(seed, 0, sizeof(*seed));
	return false;
}

__weak bool platform_payload_mm_authvar_smm_arena_required(void)
{
	return true;
}

__weak void platform_payload_mm_authvar_smm_arena_abort(void)
{
}

static bool nonzero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return combined;
}

static bool range_end(uint64_t base, uint64_t size, uint64_t *end)
{
	if (!size || base > UINT64_MAX - size)
		return false;
	*end = base + size;
	return true;
}

static bool ranges_overlap(const struct payload_mm_authvar_range *left,
	const struct payload_mm_authvar_range *right)
{
	uint64_t left_end;
	uint64_t right_end;

	return !range_end(left->base, left->size, &left_end) ||
		!range_end(right->base, right->size, &right_end) ||
		(left->base < right_end && right->base < left_end);
}

static bool seed_valid(const struct payload_mm_authvar_smm_arena_seed *seed)
{
	return seed && seed->revision == PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION &&
		seed->size == sizeof(*seed) && seed->cold_boot_generation &&
		nonzero(seed->owner, sizeof(seed->owner)) &&
		!seed->reserved[0] && !seed->reserved[1];
}

static __noinline void scrub_seed(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
#if ENV_TEST
	payload_mm_authvar_smm_loader_scrub_test_hook(buffer,
		sizeof(struct payload_mm_authvar_smm_arena_seed));
#endif
}

enum cb_err payload_mm_authvar_smm_arena_reserve(
	struct payload_mm_authvar_smm_arena_receipt *receipt,
	uint64_t smram_base, uint64_t smram_size,
	const struct payload_mm_authvar_range *occupied, size_t occupied_count,
	const struct payload_mm_authvar_smm_arena_seed *seed)
{
	struct payload_mm_authvar_smm_arena_seed seed_copy = { 0 };
	uint64_t smram_end;
	uint64_t cursor;
	uint64_t best_base = 0;
	uint64_t best_size = 0;
	enum cb_err status = CB_ERR;

	if (!receipt)
		goto out;
	memset(receipt, 0, sizeof(*receipt));
	if (!occupied || !occupied_count || !seed ||
	    !range_end(smram_base, smram_size, &smram_end))
		goto out;
	memcpy(&seed_copy, seed, sizeof(seed_copy));
	if (!seed_valid(&seed_copy) || memcmp(&seed_copy, seed, sizeof(seed_copy)))
		goto out;
	for (size_t index = 0; index < occupied_count; index++) {
		uint64_t end;

		if (!range_end(occupied[index].base, occupied[index].size, &end) ||
		    occupied[index].base < smram_base || end > smram_end)
			goto out;
		for (size_t other = 0; other < index; other++)
			if (ranges_overlap(&occupied[index], &occupied[other]))
				goto out;
	}
	cursor = smram_base;
	while (cursor < smram_end) {
		uint64_t next_base = smram_end;
		uint64_t next_end = smram_end;
		uint64_t aligned;

		if (cursor > UINT64_MAX - (__BIGGEST_ALIGNMENT__ - 1U))
			goto out;
		aligned = ALIGN_UP(cursor, (uint64_t)__BIGGEST_ALIGNMENT__);

		for (size_t index = 0; index < occupied_count; index++) {
			uint64_t end = occupied[index].base + occupied[index].size;

			if (occupied[index].base >= cursor &&
			    occupied[index].base < next_base) {
				next_base = occupied[index].base;
				next_end = end;
			}
		}
		if (aligned < next_base && next_base - aligned > best_size) {
			best_base = aligned;
			best_size = next_base - aligned;
		}
		if (next_base == smram_end)
			break;
		cursor = next_end;
	}
	if (!best_size || memcmp(&seed_copy, seed, sizeof(seed_copy)))
		goto out;
	*receipt = (struct payload_mm_authvar_smm_arena_receipt) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION,
		.size = sizeof(*receipt),
		.cold_boot_generation = seed_copy.cold_boot_generation,
		.smram = { .base = smram_base, .size = smram_size },
		.arena = { .base = best_base, .size = best_size },
	};
	memcpy(receipt->owner, seed_copy.owner, sizeof(receipt->owner));
	status = CB_SUCCESS;
out:
	scrub_seed(&seed_copy, sizeof(seed_copy));
	return status;
}
