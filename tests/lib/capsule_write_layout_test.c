/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <string.h>

#include "lib/capsule_write_layout_internal.h"
#include "lib/payload_mm_fmp_owner_layout_internal.h"

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static struct capsule_write_layout valid_layout(void)
{
	return (struct capsule_write_layout) {
		.revision = CAPSULE_WRITE_LAYOUT_REVISION,
		.size = sizeof(struct capsule_write_layout),
		.media_size = 0x800000,
		.erase_size = 4096,
		.route_count = 1,
		.smmstore = { 0, 0x10000 },
		.route = {
			{ .image_offset = 0x12000, .flash_offset = 0x12000,
			  .size = 0x7ee000, .flags = LB_CAPSULE_REGION_BIOS },
		},
	};
}

int main(void)
{
	struct capsule_write_layout layout = valid_layout();
	struct fmp_owner_layout raw = {
		.revision = PAYLOAD_MM_FMP_OWNER_LAYOUT_REVISION,
		.size = sizeof(raw),
		.media_size = layout.media_size,
		.erase_size = layout.erase_size,
		.route_count = layout.route_count,
		.smmstore = { 0, 0x10000 },
	};

	assert(capsule_write_layout_valid(&layout));
	raw.route[0] = layout.route[0];
	assert(!payload_mm_fmp_layout_valid(&raw));
	assert(!capsule_write_layout_valid(NULL));

#define REJECT(member, value) do { \
	layout = valid_layout(); \
	layout.member = (value); \
	assert(!capsule_write_layout_valid(&layout)); \
} while (0)
	REJECT(revision, 0);
	REJECT(size, sizeof(layout) - 1);
	REJECT(media_size, 0);
	REJECT(erase_size, 0);
	REJECT(erase_size, 4095);
	REJECT(reserved, 1);
	REJECT(route_count, 0);
	REJECT(route_count, CAPSULE_UPDATE_MAX_REGIONS + 1);
	REJECT(metadata_count, CAPSULE_WRITE_LAYOUT_MAX_METADATA + 1);
	REJECT(smmstore.size, 0);
	REJECT(smmstore.offset, UINT64_MAX);
	REJECT(smmstore.offset, 1);
	REJECT(smmstore.size, 4095);
	REJECT(metadata[0].offset, 0x10000);
	REJECT(metadata[0].size, 4096);
	REJECT(route[0].reserved, 1);
	REJECT(route[0].flags, UINT32_MAX);
	REJECT(route[0].flags, 0);
	REJECT(route[0].image_offset, UINT64_MAX);
	REJECT(route[0].image_offset, 1);
	REJECT(route[0].flash_offset, 0);
	REJECT(route[0].flash_offset, 1);
	REJECT(route[0].size, 0);
	REJECT(route[0].size, 0x7ef000);
	REJECT(route[1].image_offset, 1);
	REJECT(route[1].flash_offset, 1);
	REJECT(route[1].size, 1);
	REJECT(route[1].flags, 1);
	REJECT(route[1].reserved, 1);
#undef REJECT

	layout = valid_layout();
	layout.metadata_count = 2;
	layout.metadata[0] = (struct capsule_write_range) { 0x10000, 4096 };
	layout.metadata[1] = (struct capsule_write_range) { 0x11000, 4096 };
	assert(capsule_write_layout_valid(&layout));
	layout.metadata[1] = layout.metadata[0];
	assert(!capsule_write_layout_valid(&layout));
	layout.metadata[1] = (struct capsule_write_range) { 0x12000, 4096 };
	assert(!capsule_write_layout_valid(&layout));
	layout.metadata[1] = (struct capsule_write_range) { 0, 4096 };
	assert(!capsule_write_layout_valid(&layout));
	layout.metadata[1] = (struct capsule_write_range) { UINT64_MAX, 4096 };
	assert(!capsule_write_layout_valid(&layout));
	layout.metadata[1] = (struct capsule_write_range) { 0x11001, 4096 };
	assert(!capsule_write_layout_valid(&layout));

	layout = valid_layout();
	layout.route_count = 2;
	layout.route[0].size -= 4096;
	layout.route[1] = (struct lb_capsule_update_region) {
		.image_offset = 0x7ff000, .flash_offset = 0x7ff000, .size = 4096,
	};
	assert(capsule_write_layout_valid(&layout));
	layout.route[1].flags = LB_CAPSULE_REGION_BIOS;
	assert(!capsule_write_layout_valid(&layout));
	layout.route[1].flags = 0;
	layout.route[1].image_offset = layout.route[0].image_offset;
	assert(!capsule_write_layout_valid(&layout));
	layout.route[1].image_offset = 0x7ff000;
	layout.route[1].flash_offset = layout.route[0].flash_offset;
	assert(!capsule_write_layout_valid(&layout));
	return 0;
}
