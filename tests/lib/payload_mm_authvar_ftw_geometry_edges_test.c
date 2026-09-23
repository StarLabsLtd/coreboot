/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REGION_SIZE (8U * 8192U)
#define FV_HEADER_SIZE 72U

static const uint8_t fv_guid[16] = {
	0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
	0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
};
static const uint8_t store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};

static void put16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		bytes[i] = (uint8_t)(value >> (8U * i));
}

static void put64(uint8_t *bytes, uint64_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		bytes[i] = (uint8_t)(value >> (8U * i));
}

static uint16_t get16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

static uint32_t crc32(const uint8_t *bytes, size_t size)
{
	uint32_t crc = UINT32_MAX;

	for (size_t i = 0; i < size; i++) {
		crc ^= bytes[i];
		for (unsigned int bit = 0; bit < 8U; bit++)
			crc = (crc >> 1) ^ (0xedb88320U &
				(uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

static void build_image(uint8_t *media, size_t block_size, size_t blocks,
	size_t completed_entries, size_t fv_header_size, bool multiple_maps)
{
	size_t region_size = block_size * blocks;
	size_t spare_blocks = blocks / 2U;
	size_t variable_blocks = blocks - spare_blocks - 1U;
	size_t variable_size = variable_blocks * block_size;
	uint8_t *workspace = media + variable_size;
	uint8_t header[32];
	uint16_t checksum = 0;

	if (blocks > UINT32_MAX || block_size > UINT32_MAX ||
	    fv_header_size > UINT16_MAX || fv_header_size + 28U > variable_size ||
	    variable_size - fv_header_size > UINT32_MAX)
		abort();
	memset(media, 0xff, region_size);
	memset(media, 0, fv_header_size);
	memcpy(media + 16U, fv_guid, sizeof(fv_guid));
	put64(media + 32U, region_size);
	put32(media + 40U, 0x4856465fU);
	put32(media + 44U, 0x00000e36U);
	put16(media + 48U, (uint16_t)fv_header_size);
	media[55] = 2U;
	put32(media + 56U, multiple_maps ? 1U : (uint32_t)blocks);
	put32(media + 60U, (uint32_t)block_size);
	if (multiple_maps) {
		put32(media + 64U, (uint32_t)blocks - 1U);
		put32(media + 68U, (uint32_t)block_size);
	}
	for (size_t i = 0; i < fv_header_size; i += 2U)
		checksum = (uint16_t)(checksum + get16(media + i));
	put16(media + 50U, (uint16_t)-checksum);
	memset(media + fv_header_size, 0, 28U);
	memcpy(media + fv_header_size, store_guid, sizeof(store_guid));
	put32(media + fv_header_size + 16U,
		(uint32_t)(variable_size - fv_header_size));
	media[fv_header_size + 20U] = 0x5aU;
	media[fv_header_size + 21U] = 0xfeU;
	memset(header, 0xff, sizeof(header));
	memcpy(header, payload_mm_authvar_ftw_working_block_guid, 16U);
	put64(header + 24U, block_size - sizeof(header));
	put32(header + 16U, crc32(header, sizeof(header)));
	memcpy(workspace, header, sizeof(header));
	workspace[PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET] =
		PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID;
	for (size_t i = 0; i < completed_entries; i++) {
		uint8_t *entry = workspace + PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE +
			i * (PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
			PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE);

		entry[0] = PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE;
		memcpy(entry + 4U, payload_mm_authvar_ftw_coreboot_caller_guid, 16U);
		put64(entry + 24U, 1U);
		put64(entry + 32U, 0U);
	}
}

static void expect_action(const uint8_t *media, size_t block_size,
	enum payload_mm_authvar_ftw_action action)
{
	struct payload_mm_authvar_ftw_plan plan;

	if (payload_mm_authvar_ftw_plan(media, 3U * block_size, block_size,
		&plan) != CB_SUCCESS || plan.action != action)
		abort();
}

int main(void)
{
	static uint8_t media[MAX_REGION_SIZE];
	const size_t remainders[] = { 0U, 79U, 80U, 81U };

	for (size_t i = 0; i < ARRAY_SIZE(remainders); i++) {
		size_t block_size = 32U + 80U + remainders[i];
		enum payload_mm_authvar_ftw_action action = remainders[i] < 80U ?
			PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE :
			PAYLOAD_MM_AUTHVAR_FTW_CLEAN;

		build_image(media, block_size, 3U, 1U, FV_HEADER_SIZE, false);
		expect_action(media, block_size, action);
	}
	for (size_t prefix = 1U; prefix < 80U; prefix++) {
		size_t block_size = 32U + 160U;
		uint8_t *tail;

		build_image(media, block_size, 3U, 1U, FV_HEADER_SIZE, false);
		tail = media + block_size + 32U + 80U;
		memset(tail + 1U, 0, prefix);
		expect_action(media, block_size,
			PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE);
	}
	for (size_t block_size = 512U; block_size <= 8192U; block_size *= 2U) {
		build_image(media, block_size, 3U, 0, FV_HEADER_SIZE, false);
		expect_action(media, block_size, PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	}
	build_image(media, 512U, 3U, 0, 80U, true);
	expect_action(media, 512U, PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	build_image(media, 512U, 3U, 0, 484U, false);
	expect_action(media, 512U, PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	return 0;
}
