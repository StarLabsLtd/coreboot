/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_smm_loader.h>
#include <commonlib/helpers.h>
#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static bool overlap(const struct payload_mm_authvar_range *left,
	const struct payload_mm_authvar_range *right)
{
	return left->base < right->base + right->size &&
		right->base < left->base + left->size;
}

static struct payload_mm_authvar_smm_arena_seed seed(void)
{
	return (struct payload_mm_authvar_smm_arena_seed) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION,
		.size = sizeof(struct payload_mm_authvar_smm_arena_seed),
		.cold_boot_generation = 19,
		.owner = { 1 },
	};
}

int main(void)
{
	const uint64_t base = 0x100000U;
	const uint64_t size = 8U * 1024U * 1024U;
	struct payload_mm_authvar_range occupied[] = {
		/* Active SMM stacks, including the current handler stack. */
		{ .base = 0x100000U, .size = 0x20000U },
		/* CPU stubs and save states. */
		{ .base = 0x180000U, .size = 0x40000U },
		/* Page tables. */
		{ .base = 0x600000U, .size = 0x20000U },
		/* Complete handler module: text, rodata, data and statics. */
		{ .base = 0x700000U, .size = 0x100000U },
		/* STM/MSEG and BIOS resource list. */
		{ .base = 0x880000U, .size = 0x80000U },
	};
	struct payload_mm_authvar_smm_arena_seed valid = seed();
	struct payload_mm_authvar_smm_arena_receipt receipt;

	CHECK(payload_mm_authvar_smm_arena_reserve(&receipt, base, size,
		occupied, ARRAY_SIZE(occupied), &valid) == CB_SUCCESS);
	CHECK(receipt.revision == PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION &&
		receipt.size == sizeof(receipt) &&
		receipt.cold_boot_generation == valid.cold_boot_generation &&
		receipt.smram.base == base && receipt.smram.size == size &&
		!memcmp(receipt.owner, valid.owner, sizeof(receipt.owner)));
	CHECK(!(receipt.arena.base % __BIGGEST_ALIGNMENT__));
	for (size_t index = 0; index < ARRAY_SIZE(occupied); index++)
		CHECK(!overlap(&receipt.arena, &occupied[index]));

	{
		struct payload_mm_authvar_range bad[] = {
			{ .base = 0x100000U, .size = 0x40000U },
			{ .base = 0x120000U, .size = 0x40000U },
		};

		CHECK(payload_mm_authvar_smm_arena_reserve(&receipt, base, size,
			bad, ARRAY_SIZE(bad), &valid) == CB_ERR);
	}
	occupied[0].base = UINT64_MAX - 8U;
	occupied[0].size = 16U;
	CHECK(payload_mm_authvar_smm_arena_reserve(&receipt, base, size,
		occupied, ARRAY_SIZE(occupied), &valid) == CB_ERR);
	CHECK(payload_mm_authvar_smm_arena_reserve(&receipt, UINT64_MAX - 7U,
		16U, occupied, ARRAY_SIZE(occupied), &valid) == CB_ERR);
	valid.owner[0] = 0;
	CHECK(payload_mm_authvar_smm_arena_reserve(&receipt, base, size,
		occupied + 1, ARRAY_SIZE(occupied) - 1U, &valid) == CB_ERR);
	{
		union {
			struct payload_mm_authvar_smm_arena_seed seed;
			struct payload_mm_authvar_smm_arena_receipt receipt;
		} alias = { .seed = seed() };

		CHECK(payload_mm_authvar_smm_arena_reserve(&alias.receipt, base, size,
			occupied + 1, ARRAY_SIZE(occupied) - 1U,
			&alias.seed) == CB_ERR);
	}
	return 0;
}
