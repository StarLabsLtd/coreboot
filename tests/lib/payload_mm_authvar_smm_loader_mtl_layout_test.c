/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_smm_loader.h>
#include <commonlib/helpers.h>
#include <cpu/intel/em64t101_save_state.h>

#define MTL_TSEG_SIZE 0x800000U
#define MTL_IED_SIZE 0x400000U
#define MTL_CACHE_SIZE 0x200000U
#define MTL_OPAL_SIZE 0x1000U
#define MTL_CPU_COUNT 22U
#define MTL_STACK_SIZE 0x800U
/* LOAD MemSiz/Align from the selected MTL smm.elf and smmstub.elf. */
#define MTL_HANDLER_SIZE 0x47d8U
#define MTL_HANDLER_ALIGNMENT 0x20U
#define MTL_STUB_SIZE 0x1c0U
#define SMM_SEGMENT_SIZE 0x10000U
/* Resolved MTL board.fmd SMMSTORE@0x30000 size and its exact arena result. */
#define MTL_SMMSTORE_SIZE 0x80000U
#define MTL_REQUIRED_ARENA_SIZE 3935168U
#define MTL_AVAILABLE_ARENA_SIZE 0x1e2420U
#define MTL_ARENA_DEFICIT 0x1de7a0U

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static const uint64_t smram_base = 0x7e000000ULL;

static void add_range(struct payload_mm_authvar_range *ranges, size_t *count,
	uint64_t base, uint64_t size)
{
	ranges[*count] = (struct payload_mm_authvar_range) {
		.base = base,
		.size = size,
	};
	(*count)++;
}

int main(void)
{
	struct payload_mm_authvar_range occupied[2U * MTL_CPU_COUNT + 2U];
	struct payload_mm_authvar_smm_arena_receipt receipt;
	struct payload_mm_authvar_smm_arena_seed seed = {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION,
		.size = sizeof(seed),
		.cold_boot_generation = 1,
		.owner = { 1 },
	};
	const uint64_t handler_smram_size = MTL_TSEG_SIZE - MTL_IED_SIZE -
		MTL_CACHE_SIZE - MTL_OPAL_SIZE;
	const uint64_t smram_top = smram_base + handler_smram_size;
	const uint64_t handler_base = ALIGN_DOWN(smram_top - MTL_HANDLER_SIZE,
		MTL_HANDLER_ALIGNMENT);
	const uint64_t stub_segment_base = handler_base - SMM_SEGMENT_SIZE;
	const size_t save_state_size = sizeof(em64t101_smm_state_save_area_t);
	const size_t slot_size = MAX(save_state_size, (size_t)MTL_STUB_SIZE);
	const size_t cpus_per_segment =
		(SMM_SEGMENT_SIZE - SMM_ENTRY_OFFSET - MTL_STUB_SIZE) / slot_size;
	size_t count = 0;

	_Static_assert(MTL_TSEG_SIZE == 0x800000U, "MTL TSEG assumption changed");
	_Static_assert(MTL_SMMSTORE_SIZE == 512U * 1024U,
		"MTL SMMSTORE assumption changed");
	CHECK(save_state_size == 0x400U);
	CHECK(cpus_per_segment >= MTL_CPU_COUNT);
	add_range(occupied, &count, handler_base, MTL_HANDLER_SIZE);
	for (size_t cpu = 0; cpu < MTL_CPU_COUNT; cpu++) {
		const uint64_t smbase = stub_segment_base -
			SMM_SEGMENT_SIZE * (cpu / cpus_per_segment) -
			slot_size * (cpu % cpus_per_segment);

		add_range(occupied, &count, smbase + SMM_ENTRY_OFFSET,
			MTL_STUB_SIZE);
		add_range(occupied, &count,
			smbase + SMM_SEGMENT_SIZE - save_state_size,
			save_state_size);
	}
	add_range(occupied, &count, smram_base, MTL_CPU_COUNT * MTL_STACK_SIZE);
	CHECK(count == ARRAY_SIZE(occupied));
	CHECK(payload_mm_authvar_smm_arena_reserve(&receipt, smram_base,
		handler_smram_size, occupied, count, &seed) == CB_SUCCESS);
	CHECK(receipt.arena.size == MTL_AVAILABLE_ARENA_SIZE);
	CHECK(MTL_REQUIRED_ARENA_SIZE - receipt.arena.size == MTL_ARENA_DEFICIT);
	return 0;
}
