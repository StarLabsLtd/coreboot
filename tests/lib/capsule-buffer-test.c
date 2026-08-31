/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/efi/capsule_buffer.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return 1; } while (0)

static void clear_ram(uint8_t *arena, struct memranges *ranges)
{
	const struct range_entry *entry;

	memranges_each_entry(entry, ranges) {
		if (range_entry_tag(entry) == BM_MEM_RAM)
			memset(arena + range_entry_base(entry), 0,
			       range_entry_size(entry));
	}
}

static int test_capsule_buffer_survives_clear(void)
{
	struct range_entry storage[16];
	struct memranges ranges;
	uint8_t arena[0x10000];
	uint64_t base;
	uint64_t size;

	memset(arena, 0xa5, sizeof(arena));
	memranges_init_empty(&ranges, storage, ARRAY_SIZE(storage));
	memranges_insert(&ranges, 0x1000, 0xf000, BM_MEM_RAM);
	memranges_insert(&ranges, 0xf000, 0x1000, BM_MEM_RESERVED);
	CHECK(efi_capsule_pick_buffer(&ranges, 481, &base, &size));
	CHECK(base == 0xe000);
	CHECK(size == 0x1000);
	memranges_insert(&ranges, base, size, BM_MEM_RESERVED);
	clear_ram(arena, &ranges);
	CHECK(arena[base] == 0xa5);
	CHECK(arena[base + 480] == 0xa5);
	CHECK(arena[base - 1] == 0);
	CHECK(arena[0xf000] == 0xa5);
	memranges_teardown(&ranges);
	return 0;
}

static int test_capsule_buffer_contract(void)
{
	struct range_entry storage[16];
	struct memranges ranges;
	uint64_t base = 0x55;
	uint64_t size = 0x66;
	uint64_t again_base;
	uint64_t again_size;

	memranges_init_empty(&ranges, storage, ARRAY_SIZE(storage));
	memranges_insert(&ranges, 0x1000, 0x8000, BM_MEM_RAM);
	CHECK(!efi_capsule_pick_buffer(&ranges, 0, &base, &size));
	CHECK(base == 0x55);
	CHECK(size == 0x66);
	CHECK(!efi_capsule_pick_buffer(&ranges, UINT64_MAX, &base, &size));
	memranges_insert(&ranges, 0x8000, 0x1000, BM_MEM_RESERVED);
	CHECK(efi_capsule_pick_buffer(&ranges, 1, &base, &size));
	CHECK(base == 0x7000);
	CHECK(size == 0x1000);
	CHECK(efi_capsule_pick_buffer(&ranges, 1, &again_base, &again_size));
	CHECK(again_base == base);
	CHECK(again_size == size);
	memranges_insert(&ranges, 0x1000, 0x7000, BM_MEM_RESERVED);
	CHECK(!efi_capsule_pick_buffer(&ranges, 1, &base, &size));
	memranges_teardown(&ranges);
	return 0;
}

int main(void)
{
	if (test_capsule_buffer_survives_clear() || test_capsule_buffer_contract())
		return 1;
	return 0;
}
