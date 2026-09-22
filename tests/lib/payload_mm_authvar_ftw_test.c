/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <stdlib.h>
#include <string.h>

#define assert(c) do { if (!(c)) abort(); } while (0)
#define BLOCK_SIZE 4096U
#define BLOCK_COUNT 4U
#define REGION_SIZE (BLOCK_SIZE * BLOCK_COUNT)
#define FV_HEADER_SIZE 72U
#define STORE_SIZE (BLOCK_SIZE - FV_HEADER_SIZE)

static uint8_t region[REGION_SIZE];
static uint8_t variant[6U * BLOCK_SIZE];
static const uint8_t fv_guid[16] = {
	0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
	0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
};
static const uint8_t store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};
static const uint8_t work_guid[16] = {
	0x2b, 0x29, 0x58, 0x9e, 0x68, 0x7c, 0x7d, 0x49,
	0xa0, 0xce, 0x65, 0x00, 0xfd, 0x9f, 0x1b, 0x95,
};
static const uint8_t caller_guid[16] = {
	0x76, 0xea, 0x5c, 0xfe, 0x72, 0x4f, 0xe8, 0x49,
	0x98, 0x6f, 0x2c, 0xd8, 0x99, 0xdf, 0xfe, 0x5d,
};
static const uint8_t other_caller_guids[][16] = {
	{
		0x48, 0xb2, 0x0c, 0x47, 0xac, 0xe8, 0x3c, 0x47,
		0xbb, 0x4f, 0x81, 0x06, 0x9a, 0x1f, 0xe6, 0xfd,
	},
	{
		0xec, 0xe4, 0xad, 0x3a, 0xcc, 0x63, 0x48, 0x4a,
		0xa9, 0x28, 0x5a, 0x37, 0x4d, 0xd4, 0x63, 0xeb,
	},
};

static void put16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
	for (size_t i = 0; i < 4; i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static void put64(uint8_t *p, uint64_t value)
{
	for (size_t i = 0; i < 8; i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static uint32_t crc32(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_MAX;

	for (size_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320U &
				(uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

static void make_fv_blocks(uint8_t *bytes, uint32_t blocks)
{
	uint16_t sum = 0;
	const uint32_t variable_blocks = blocks - blocks / 2U - 1U;
	const uint32_t variable_size = variable_blocks * BLOCK_SIZE;

	memset(bytes, 0xff, variable_size);
	memset(bytes, 0, FV_HEADER_SIZE);
	memcpy(bytes + 16, fv_guid, sizeof(fv_guid));
	put64(bytes + 32, (uint64_t)blocks * BLOCK_SIZE);
	put32(bytes + 40, 0x4856465fU);
	put32(bytes + 44, 0x00000e36U);
	put16(bytes + 48, FV_HEADER_SIZE);
	bytes[55] = 2;
	put32(bytes + 56, blocks);
	put32(bytes + 60, BLOCK_SIZE);
	for (size_t i = 0; i < FV_HEADER_SIZE; i += 2U)
		sum = (uint16_t)(sum + (uint16_t)bytes[i] +
			((uint16_t)bytes[i + 1U] << 8));
	put16(bytes + 50, (uint16_t)-sum);
	memset(bytes + FV_HEADER_SIZE, 0, 28U);
	memcpy(bytes + FV_HEADER_SIZE, store_guid, sizeof(store_guid));
	put32(bytes + FV_HEADER_SIZE + 16U, variable_size - FV_HEADER_SIZE);
	bytes[FV_HEADER_SIZE + 20U] = 0x5a;
	bytes[FV_HEADER_SIZE + 21U] = 0xfe;
}

static void make_fv(uint8_t *bytes)
{
	make_fv_blocks(bytes, BLOCK_COUNT);
}

static void make_workspace(uint8_t *bytes, uint8_t state)
{
	uint8_t canonical[32];

	memset(bytes, 0xff, BLOCK_SIZE);
	memset(canonical, 0xff, sizeof(canonical));
	memcpy(canonical, work_guid, sizeof(work_guid));
	put64(canonical + 24, BLOCK_SIZE - 32U);
	put32(canonical + 16, crc32(canonical, sizeof(canonical)));
	memcpy(bytes, canonical, sizeof(canonical));
	bytes[20] = state;
}

static void make_media(void)
{
	memset(region, 0xff, sizeof(region));
	make_fv(region);
}

static uint8_t *working(void)
{
	return region + BLOCK_SIZE;
}

static uint8_t *spare(void)
{
	return region + 2U * BLOCK_SIZE;
}

static void make_write(uint8_t record_state, uint8_t header_state)
{
	uint8_t *header = working() + 32U;
	uint8_t *record = header + 40U;

	memset(header, 0xff, 80U);
	header[0] = header_state;
	memcpy(header + 4, caller_guid, sizeof(caller_guid));
	put64(header + 24, 1);
	put64(header + 32, 0);
	record[0] = record_state;
	put64(record + 8, 0);
	put64(record + 16, FV_HEADER_SIZE);
	put64(record + 24, STORE_SIZE);
	put64(record + 32, (uint64_t)-(int64_t)(2U * BLOCK_SIZE));
}

static enum payload_mm_authvar_ftw_action plan(void)
{
	struct payload_mm_authvar_ftw_plan result;

	if (payload_mm_authvar_ftw_plan(region, sizeof(region), BLOCK_SIZE,
		&result) != CB_SUCCESS)
		return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
	assert(result.geometry.variable_size == BLOCK_SIZE);
	assert(result.geometry.working_offset == BLOCK_SIZE);
	assert(result.geometry.spare_offset == 2U * BLOCK_SIZE);
	assert(result.fv_header_size == FV_HEADER_SIZE);
	assert(result.variable_store_size == STORE_SIZE);
	return result.action;
}

static void expect_failure(void)
{
	struct payload_mm_authvar_ftw_plan result;
	const uint8_t zero[sizeof(result)] = { 0 };

	memset(&result, 0xa5, sizeof(result));
	assert(payload_mm_authvar_ftw_plan(region, sizeof(region), BLOCK_SIZE,
		&result) == CB_ERR);
	assert(!memcmp(&result, zero, sizeof(result)));
}

static void valid_states(void)
{
	make_media();
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE);
	make_workspace(working(), 0xfe);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	make_write(0xff, 0xfe);
	memset(working() + 32U + 40U, 0xff, 40U);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD);
	make_workspace(working(), 0xfe);
	make_write(0xff, 0xfc);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD);
	for (size_t i = 0; i < sizeof(other_caller_guids) /
	     sizeof(other_caller_guids[0]); i++) {
		memcpy(working() + 32U + 4U, other_caller_guids[i], 16U);
		assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD);
	}
	memcpy(working() + 32U + 4U, caller_guid, sizeof(caller_guid));
	make_write(0xfd, 0xfc);
	memcpy(spare(), region, BLOCK_SIZE);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE);
	memset(region, 0xff, BLOCK_SIZE);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE);
	memcpy(region, spare(), BLOCK_SIZE);
	region[0] = 1;
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE);
	memcpy(region, spare(), BLOCK_SIZE);
	make_write(0xf9, 0xfc);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW);
	make_media();
	memcpy(spare(), region, BLOCK_SIZE);
	make_workspace(spare(), 0xfe);
	memset(working(), 0xff, BLOCK_SIZE);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	for (size_t cut = 0; cut < BLOCK_SIZE; cut++) {
		memset(working(), 0xff, BLOCK_SIZE);
		memset(working(), 0, cut);
		assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	}
}

static void completed_history(void)
{
	uint8_t *header;
	uint8_t *record;

	make_media();
	make_workspace(working(), 0xfe);
	header = working() + 32U;
	record = header + 40U;
	memset(header, 0xff, 80U);
	header[0] = 0xf8;
	memcpy(header + 4, caller_guid, sizeof(caller_guid));
	put64(header + 24, 1);
	put64(header + 32, 0);
	record[0] = 0xf9;
	make_write(0xff, 0xfc);
	memmove(working() + 112U, working() + 32U, 80U);
	header[0] = 0xf8;
	put64(header + 24, 1);
	record[0] = 0xf9;
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD);
	make_media();
	make_workspace(working(), 0xfe);
	make_write(0xff, 0xfa);
	memset(working() + 72U, 0xff, 40U);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	make_media();
	make_workspace(working(), 0xfe);
	make_write(0xff, 0xf8);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	make_media();
	make_workspace(working(), 0xfe);
	make_write(0xf9, 0xf8);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
}

static void hostile_inputs(void)
{
	struct payload_mm_authvar_ftw_plan result;
	struct payload_mm_authvar_ftw_geometry geometry;
	const uint8_t zero[sizeof(result)] = { 0 };

	assert(payload_mm_authvar_ftw_geometry(&geometry, 3U * BLOCK_SIZE,
		BLOCK_SIZE) == CB_SUCCESS && geometry.variable_size == BLOCK_SIZE &&
		geometry.spare_size == BLOCK_SIZE);
	assert(payload_mm_authvar_ftw_geometry(&geometry, REGION_SIZE - 1U,
		BLOCK_SIZE) == CB_ERR);
	assert(payload_mm_authvar_ftw_geometry(&geometry, 5U * BLOCK_SIZE,
		BLOCK_SIZE) == CB_SUCCESS && geometry.variable_size == 2U * BLOCK_SIZE &&
		geometry.spare_size == 2U * BLOCK_SIZE);
	assert(payload_mm_authvar_ftw_geometry(&geometry, 6U * BLOCK_SIZE,
		BLOCK_SIZE) == CB_SUCCESS && geometry.variable_size == 2U * BLOCK_SIZE &&
		geometry.spare_size == 3U * BLOCK_SIZE);
	memset(&result, 0xa5, sizeof(result));
	assert(payload_mm_authvar_ftw_plan(NULL, REGION_SIZE, BLOCK_SIZE,
		&result) == CB_ERR);
	assert(!memcmp(&result, zero, sizeof(result)));
	for (size_t i = 0; i < REGION_SIZE; i++) {
		memset(&result, 0xa5, sizeof(result));
		assert(payload_mm_authvar_ftw_plan(region, i, BLOCK_SIZE,
			&result) == CB_ERR);
		assert(!memcmp(&result, zero, sizeof(result)));
	}
	for (size_t i = 0; i < FV_HEADER_SIZE + 28U; i++) {
		make_media();
		make_workspace(working(), 0xfe);
		region[i] ^= 1U;
		expect_failure();
	}
	for (size_t i = 0; i < 32U; i++) {
		make_media();
		make_workspace(working(), 0xfe);
		working()[i] ^= 1U;
		expect_failure();
	}
	for (size_t i = 0; i < 40U; i++) {
		make_media();
		make_workspace(working(), 0xfe);
		make_write(0xff, 0xfc);
		working()[32U + i] ^= 1U;
		expect_failure();
	}
	for (size_t i = 0; i < 40U; i++) {
		make_media();
		make_workspace(working(), 0xfe);
		make_write(0xff, 0xfc);
		working()[72U + i] ^= 1U;
		if (i < 8U)
			expect_failure();
		else
			assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD);
	}
	for (size_t i = 0; i < 40U; i++) {
		make_media();
		make_workspace(working(), 0xfe);
		make_write(0xf9, 0xfc);
		working()[72U + i] ^= 1U;
		expect_failure();
	}
	make_media();
	make_workspace(working(), 0xfe);
	make_write(0xfd, 0xfc);
	expect_failure();
	memcpy(spare(), region, BLOCK_SIZE);
	spare()[40] ^= 1U;
	expect_failure();
	make_media();
	make_workspace(working(), 0xfe);
	make_write(0xff, 0xfc);
	working()[32U + 24U] = 2;
	expect_failure();
}

static void state_matrix(void)
{
	for (unsigned int state = 0; state <= UINT8_MAX; state++) {
		make_media();
		make_workspace(working(), 0xfe);
		make_write((uint8_t)state, 0xfc);
		memcpy(spare(), region, BLOCK_SIZE);
		if (state == 0xffU)
			assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD);
		else if (state == 0xfdU)
			assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE);
		else if (state == 0xf9U)
			assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW);
		else
			expect_failure();
	}
	for (unsigned int state = 0; state <= UINT8_MAX; state++) {
		make_media();
		make_workspace(working(), 0xfe);
		make_write(0xff, (uint8_t)state);
		if (state == 0xfcU)
			assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD);
		else if (state == 0xf8U)
			assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		else
			expect_failure();
	}
}

static void every_supported_geometry(void)
{
	for (uint32_t blocks = 3; blocks <= 6; blocks++) {
		const size_t region_size = (size_t)blocks * BLOCK_SIZE;
		const uint32_t variable_blocks = blocks - blocks / 2U - 1U;
		const uint32_t variable_size = variable_blocks * BLOCK_SIZE;
		const uint32_t working_offset = variable_size;
		const uint32_t spare_offset = working_offset + BLOCK_SIZE;
		const uint32_t store_size = variable_size - FV_HEADER_SIZE;
		struct payload_mm_authvar_ftw_plan result;
		uint8_t *header;
		uint8_t *record;

		memset(variant, 0xff, sizeof(variant));
		make_fv_blocks(variant, blocks);
		assert(payload_mm_authvar_ftw_plan(variant, region_size, BLOCK_SIZE,
			&result) == CB_SUCCESS &&
			result.action == PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE);
		make_workspace(variant + working_offset, 0xfe);
		assert(payload_mm_authvar_ftw_plan(variant, region_size, BLOCK_SIZE,
			&result) == CB_SUCCESS && result.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		header = variant + working_offset + 32U;
		record = header + 40U;
		memset(header, 0xff, 80U);
		header[0] = 0xfc;
		memcpy(header + 4, caller_guid, sizeof(caller_guid));
		put64(header + 24, 1);
		put64(header + 32, 0);
		record[0] = 0xfd;
		put64(record + 8, 0);
		put64(record + 16, FV_HEADER_SIZE);
		put64(record + 24, store_size);
		put64(record + 32, (uint64_t)-(int64_t)spare_offset);
		memcpy(variant + spare_offset, variant, variable_size);
		memset(variant, 0xff, variable_size);
		assert(payload_mm_authvar_ftw_plan(variant, region_size, BLOCK_SIZE,
			&result) == CB_SUCCESS &&
			result.action == PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE);
		make_fv_blocks(variant, blocks);
		make_workspace(variant + spare_offset, 0xfe);
		memset(variant + working_offset, 0xff, BLOCK_SIZE);
		assert(payload_mm_authvar_ftw_plan(variant, region_size, BLOCK_SIZE,
			&result) == CB_SUCCESS &&
			result.action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	}
}

static void prefix_program_cuts(void)
{
	uint8_t complete_record[40];
	uint8_t incomplete_workspace[BLOCK_SIZE];
	struct payload_mm_authvar_ftw_plan result;

	make_media();
	make_workspace(working(), 0xfe);
	make_write(0xff, 0xfc);
	memcpy(complete_record, working() + 72U, sizeof(complete_record));
	for (size_t cut = 0; cut <= sizeof(complete_record); cut++) {
		memset(working() + 72U, 0xff, sizeof(complete_record));
		memcpy(working() + 72U, complete_record, cut);
		assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD);
	}
	make_media();
	make_workspace(incomplete_workspace, 0xff);
	memset(working(), 0xff, BLOCK_SIZE);
	for (size_t cut = 0; cut < BLOCK_SIZE; cut++) {
		memset(spare(), 0xff, BLOCK_SIZE);
		memcpy(spare(), incomplete_workspace, cut);
		if (!cut) {
			assert(payload_mm_authvar_ftw_plan(region, sizeof(region), BLOCK_SIZE,
				&result) == CB_SUCCESS && result.action ==
				PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE);
		} else {
			assert(payload_mm_authvar_ftw_plan(region, sizeof(region), BLOCK_SIZE,
				&result) == CB_ERR);
		}
	}
	make_workspace(spare(), 0xfe);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);

	/* The primary workspace is copied with both state bits still erased. */
	for (size_t cut = 0; cut < BLOCK_SIZE; cut++) {
		memset(working(), 0xff, BLOCK_SIZE);
		memcpy(working(), incomplete_workspace, cut);
		assert(working()[20] == 0xff);
		assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	}

	/* A committed header makes a malformed queue corruption, not a cut. */
	make_workspace(working(), 0xfe);
	working()[32] = 0xfc;
	working()[33] = 0;
	expect_failure();
}

int main(void)
{
	valid_states();
	completed_history();
	hostile_inputs();
	state_matrix();
	every_supported_geometry();
	prefix_program_cuts();
	return 0;
}
