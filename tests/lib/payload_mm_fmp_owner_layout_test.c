/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "payload_mm_fmp_owner_layout_internal.h"

#define MEDIA_SIZE 0x1000000ULL
#define ERASE_SIZE 0x1000U
#define STATE_A_OFFSET 0xd00000ULL
#define STATE_B_OFFSET 0xd10000ULL
#define STATE_SIZE 0x10000ULL
#define SMMSTORE_OFFSET 0xd20000ULL
#define SMMSTORE_SIZE 0x40000ULL

void mock_assert(int result, const char *expression, const char *file, int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static struct fmp_owner_layout valid_layout(void)
{
	return (struct fmp_owner_layout) {
		.revision = PAYLOAD_MM_FMP_OWNER_LAYOUT_REVISION,
		.size = sizeof(struct fmp_owner_layout),
		.media_size = MEDIA_SIZE,
		.erase_size = ERASE_SIZE,
		.slot_size = ERASE_SIZE,
		.route_count = 2,
		.state = {
			{ .offset = STATE_A_OFFSET, .size = STATE_SIZE },
			{ .offset = STATE_B_OFFSET, .size = STATE_SIZE },
		},
		.smmstore = {
			.offset = SMMSTORE_OFFSET,
			.size = SMMSTORE_SIZE,
		},
		.route = {
			{
				.image_offset = 0,
				.flash_offset = 0,
				.size = STATE_A_OFFSET,
				.flags = LB_CAPSULE_REGION_BIOS,
			},
			{
				.image_offset = SMMSTORE_OFFSET + SMMSTORE_SIZE,
				.flash_offset = SMMSTORE_OFFSET + SMMSTORE_SIZE,
				.size = MEDIA_SIZE -
					(SMMSTORE_OFFSET + SMMSTORE_SIZE),
			},
		},
	};
}

#define REJECT(member, value) do { \
	struct fmp_owner_layout layout = valid_layout(); \
	layout.member = (value); \
	assert(!payload_mm_fmp_layout_valid(&layout)); \
} while (0)

int main(void)
{
	struct fmp_owner_layout layout = valid_layout();

	assert(sizeof(layout) == 592);
	assert(payload_mm_fmp_layout_valid(&layout));
	assert(!payload_mm_fmp_layout_valid(NULL));
	REJECT(revision, 0);
	REJECT(size, 0);
	REJECT(media_size, 0);
	REJECT(erase_size, 0x1800);
	REJECT(slot_size, 0x1800);
	REJECT(route_count, 0);
	REJECT(route_count, PAYLOAD_MM_FMP_OWNER_LAYOUT_MAX_ROUTES + 1U);
	REJECT(reserved, 1);
	REJECT(state[0].size, 0);
	REJECT(state[0].offset, STATE_A_OFFSET + 1U);
	REJECT(state[0].size, ERASE_SIZE);
	REJECT(state[0].size, 33U * ERASE_SIZE);
	REJECT(state[0].offset, STATE_B_OFFSET);
	REJECT(state[1].offset, STATE_A_OFFSET + ERASE_SIZE);
	REJECT(state[1].offset, MEDIA_SIZE);
	REJECT(state[1].size, UINT64_MAX);
	REJECT(smmstore.offset, STATE_A_OFFSET);
	REJECT(smmstore.size, 0);
	REJECT(smmstore.offset, MEDIA_SIZE);
	REJECT(smmstore.size, UINT64_MAX);
	REJECT(route[0].flash_offset, STATE_A_OFFSET);
	REJECT(route[0].flash_offset, STATE_B_OFFSET);
	REJECT(route[0].flash_offset, SMMSTORE_OFFSET);
	REJECT(route[0].flash_offset, MEDIA_SIZE);
	REJECT(route[0].size, 0);
	REJECT(route[0].size, UINT64_MAX);
	REJECT(route[0].image_offset, UINT64_MAX);
	REJECT(route[0].flags, UINT32_MAX);
	REJECT(route[0].reserved, 1);
	REJECT(route[1].flash_offset, 0);
	layout = valid_layout();
	layout.route[layout.route_count].size = ERASE_SIZE;
	assert(!payload_mm_fmp_layout_valid(&layout));
	return 0;
}
