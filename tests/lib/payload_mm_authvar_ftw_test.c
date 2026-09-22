/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern long write(int fd, const void *buffer, unsigned long size);

static void assertion_failed(const char *expression, const char *file, int line)
{
	char message[512];
	const int length = snprintf(message, sizeof(message),
		"%s:%d: assertion failed: %s\n", file, line, expression);

	if (length > 0)
		(void)write(2, message, (unsigned long)length);
	abort();
}

#define assert(c) do { if (!(c)) assertion_failed(#c, __FILE__, __LINE__); } while (0)
#define BLOCK_SIZE 4096U
#define BLOCK_COUNT 4U
#define REGION_SIZE (BLOCK_SIZE * BLOCK_COUNT)
#define FV_HEADER_SIZE 72U
#define STORE_SIZE (BLOCK_SIZE - FV_HEADER_SIZE)

static uint8_t region[REGION_SIZE];
static uint8_t variant[6U * BLOCK_SIZE];
#define EXACT_BLOCK_SIZE 4032U
static uint8_t exact_region[4U * EXACT_BLOCK_SIZE];
#define FIT_BLOCK_SIZE 112U
static uint8_t fit_region[4U * FIT_BLOCK_SIZE];
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
static const uint8_t coreboot_caller_guid[16] = {
	0xf3, 0x2e, 0xa5, 0xc4, 0x27, 0x4e, 0xe2, 0x47,
	0x95, 0x0b, 0xfd, 0xaa, 0xb5, 0x21, 0xb8, 0x95,
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

static bool all_bytes_are(const uint8_t *bytes, size_t size, uint8_t value)
{
	for (size_t i = 0; i < size; i++) {
		if (bytes[i] != value)
			return false;
	}
	return true;
}

static void make_fv_geometry(uint8_t *bytes, uint32_t blocks,
	uint32_t block_size)
{
	uint16_t sum = 0;
	const uint32_t variable_blocks = blocks - blocks / 2U - 1U;
	const uint32_t variable_size = variable_blocks * block_size;

	memset(bytes, 0xff, variable_size);
	memset(bytes, 0, FV_HEADER_SIZE);
	memcpy(bytes + 16, fv_guid, sizeof(fv_guid));
	put64(bytes + 32, (uint64_t)blocks * block_size);
	put32(bytes + 40, 0x4856465fU);
	put32(bytes + 44, 0x00000e36U);
	put16(bytes + 48, FV_HEADER_SIZE);
	bytes[55] = 2;
	put32(bytes + 56, blocks);
	put32(bytes + 60, block_size);
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

static void make_fv_blocks(uint8_t *bytes, uint32_t blocks)
{
	make_fv_geometry(bytes, blocks, BLOCK_SIZE);
}

static void make_fv(uint8_t *bytes)
{
	make_fv_blocks(bytes, BLOCK_COUNT);
}

static void make_workspace_size(uint8_t *bytes, size_t size, uint8_t state)
{
	uint8_t canonical[32];

	memset(bytes, 0xff, size);
	memset(canonical, 0xff, sizeof(canonical));
	memcpy(canonical, work_guid, sizeof(work_guid));
	put64(canonical + 24, size - 32U);
	put32(canonical + 16, crc32(canonical, sizeof(canonical)));
	memcpy(bytes, canonical, sizeof(canonical));
	bytes[20] = state;
}

static void make_workspace(uint8_t *bytes, uint8_t state)
{
	make_workspace_size(bytes, BLOCK_SIZE, state);
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

static void make_write_at(uint8_t *workspace, uint32_t spare_offset,
	uint32_t store_size, uint8_t record_state, uint8_t header_state)
{
	uint8_t *header = workspace + 32U;
	uint8_t *record = header + 40U;

	memset(header, 0xff, 80U);
	header[0] = header_state;
	memcpy(header + 4, caller_guid, sizeof(caller_guid));
	put64(header + 24, 1);
	put64(header + 32, 0);
	record[0] = record_state;
	put64(record + 8, 0);
	put64(record + 16, FV_HEADER_SIZE);
	put64(record + 24, store_size);
	put64(record + 32, (uint64_t)-(int64_t)spare_offset);
}

static void make_write(uint8_t record_state, uint8_t header_state)
{
	make_write_at(working(), 2U * BLOCK_SIZE, STORE_SIZE, record_state,
		header_state);
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

static struct payload_mm_authvar_ftw_plan plan_result(void)
{
	struct payload_mm_authvar_ftw_plan result;

	assert(payload_mm_authvar_ftw_plan(region, sizeof(region), BLOCK_SIZE,
		&result) == CB_SUCCESS);
	return result;
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
	struct payload_mm_authvar_ftw_plan result;

	make_media();
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE);
	make_workspace(working(), 0xfe);
	result = plan_result();
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	assert(result.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING);
	assert(result.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY);
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
	memcpy(working() + 32U + 4U, coreboot_caller_guid,
		sizeof(coreboot_caller_guid));
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD);
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
	result = plan_result();
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	assert(result.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE);
	assert(result.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY);
	make_write_at(spare(), 2U * BLOCK_SIZE, STORE_SIZE, 0xff, 0xfc);
	result = plan_result();
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	assert(result.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD);
	assert(result.queue_offset == 32U && result.queue_entry_size == 80U);
	make_workspace(spare(), 0xfe);
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
	make_media();
	make_workspace(working(), 0xfe);
	make_write(0xf9, 0xf8);
	{
		const struct payload_mm_authvar_ftw_plan result = plan_result();

		assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		assert(result.queue_offset == 112U);
		assert(result.queue_entry_size == 0U);
		assert(result.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE);
	}
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
		if (i == 20U) {
			const struct payload_mm_authvar_ftw_plan recovery = plan_result();

			assert(recovery.action ==
				PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED);
			assert(recovery.workspace ==
				PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING);
		} else {
			expect_failure();
		}
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
		else if (state == 0xffU)
			assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE);
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

static void make_completed_entries(uint8_t *workspace, size_t count,
	uint32_t spare_offset, uint32_t store_size)
{
	for (size_t i = 0; i < count; i++) {
		uint8_t *header = workspace + 32U + i * 80U;
		uint8_t *record = header + 40U;

		memset(header, 0xff, 80U);
		header[0] = 0xf8U;
		memcpy(header + 4U, caller_guid, sizeof(caller_guid));
		put64(header + 24U, 1U);
		put64(header + 32U, 0U);
		record[0] = 0xf9U;
		put64(record + 8U, 0U);
		put64(record + 16U, FV_HEADER_SIZE);
		put64(record + 24U, store_size);
		put64(record + 32U, (uint64_t)-(int64_t)spare_offset);
	}
}

static void queue_reclaim_boundaries(void)
{
	struct payload_mm_authvar_ftw_plan result;
	uint8_t dirty_entry[80];

	make_media();
	make_workspace(working(), 0xfe);
	make_write(0xff, 0xff);
	memcpy(dirty_entry, working() + 32U, sizeof(dirty_entry));
	for (size_t cut = 0; cut <= sizeof(dirty_entry); cut++) {
		memset(working() + 32U, 0xff, sizeof(dirty_entry));
		memcpy(working() + 32U, dirty_entry, cut);
		result = plan_result();
		if (all_bytes_are(working() + 32U, BLOCK_SIZE - 32U, 0xffU)) {
			assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		} else {
			assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE);
			assert(result.workspace ==
				PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING);
			assert(result.queue_offset == 32U);
		}
	}
	memset(working() + 32U, 0xff, BLOCK_SIZE - 32U);
	working()[32U + 7U] = 0x7fU;
	working()[32U + 31U] = 0xfeU;
	working()[32U + 79U] = 0xf7U;
	result = plan_result();
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE);
	assert(result.queue_offset == 32U);

	make_workspace(working(), 0xfe);
	make_completed_entries(working(), 50U, 2U * BLOCK_SIZE, STORE_SIZE);
	result = plan_result();
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE);
	assert(result.queue_offset == 4032U);

	memset(exact_region, 0xff, sizeof(exact_region));
	make_fv_geometry(exact_region, 4U, EXACT_BLOCK_SIZE);
	make_workspace_size(exact_region + EXACT_BLOCK_SIZE, EXACT_BLOCK_SIZE, 0xfeU);
	make_completed_entries(exact_region + EXACT_BLOCK_SIZE, 50U,
		2U * EXACT_BLOCK_SIZE, EXACT_BLOCK_SIZE - FV_HEADER_SIZE);
	assert(payload_mm_authvar_ftw_plan(exact_region, sizeof(exact_region),
		EXACT_BLOCK_SIZE, &result) == CB_SUCCESS);
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE);
	assert(result.queue_offset == EXACT_BLOCK_SIZE);

	memset(fit_region, 0xff, sizeof(fit_region));
	make_fv_geometry(fit_region, 4U, FIT_BLOCK_SIZE);
	make_workspace_size(fit_region + FIT_BLOCK_SIZE, FIT_BLOCK_SIZE, 0xfeU);
	assert(payload_mm_authvar_ftw_plan(fit_region, sizeof(fit_region),
		FIT_BLOCK_SIZE, &result) == CB_SUCCESS);
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	assert(result.queue_offset == 32U);
	assert(result.queue_entry_size == 0U);
}

static void restore_queue_validation(void)
{
	struct payload_mm_authvar_ftw_plan result;

	make_media();
	memset(working(), 0xff, BLOCK_SIZE);
	make_workspace(spare(), 0xfe);
	make_write_at(spare(), 2U * BLOCK_SIZE, STORE_SIZE, 0xf9, 0xf8);
	expect_failure();

	make_workspace(spare(), 0xfe);
	make_write_at(spare(), 2U * BLOCK_SIZE, STORE_SIZE, 0xfd, 0xfc);
	result = plan_result();
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	assert(result.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD);

	make_workspace(spare(), 0xfe);
	make_write_at(spare(), 2U * BLOCK_SIZE, STORE_SIZE, 0xf9, 0xfc);
	result = plan_result();
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	assert(result.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD);

	make_workspace(spare(), 0xfe);
	make_write_at(spare(), 2U * BLOCK_SIZE, STORE_SIZE, 0xff, 0xfe);
	memset(spare() + 72U, 0xff, 40U);
	result = plan_result();
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	assert(result.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD);
}

static void workspace_reclaim_reset_matrix(void)
{
	struct payload_mm_authvar_ftw_plan first;
	struct payload_mm_authvar_ftw_plan second;
	uint8_t staged[BLOCK_SIZE];

	make_media();
	make_workspace(working(), 0xfe);
	make_workspace(spare(), 0xfe);
	make_write_at(spare(), 2U * BLOCK_SIZE, STORE_SIZE, 0xff, 0xfc);
	memcpy(staged, spare(), sizeof(staged));
	first = plan_result();
	second = plan_result();
	assert(!memcmp(&first, &second, sizeof(first)));
	assert(first.action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	assert(first.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD);

	working()[20] = 0xfcU;
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	memset(working(), 0xff, BLOCK_SIZE);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	for (size_t cut = 1; cut < BLOCK_SIZE; cut++) {
		memset(working(), 0xff, BLOCK_SIZE);
		memcpy(working(), staged, cut);
		working()[20] = 0xffU;
		assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	}
	memcpy(working(), staged, BLOCK_SIZE);
	first = plan_result();
	assert(first.action == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);
	assert(first.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD);
	working()[32U] = 0xf8U;
	memset(spare(), 0xff, BLOCK_SIZE);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);

	make_workspace(working(), 0xfe);
	memcpy(spare(), working(), BLOCK_SIZE);
	first = plan_result();
	assert(first.action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED);
	assert(first.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE);
	for (size_t cut = 1; cut < BLOCK_SIZE; cut++) {
		memcpy(spare(), working(), BLOCK_SIZE);
		memset(spare(), 0xff, cut);
		first = plan_result();
		if (all_bytes_are(spare(), BLOCK_SIZE, 0xffU)) {
			assert(first.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		} else {
			assert(first.action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED);
			assert(first.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE);
		}
	}
	memcpy(spare(), working(), BLOCK_SIZE);
	spare()[0] = 0xffU;
	spare()[17] = 0xffU;
	spare()[20] = 0xffU;
	spare()[31] = 0xffU;
	first = plan_result();
	assert(first.action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED);
	memcpy(spare(), working(), BLOCK_SIZE);
	spare()[17] ^= 1U;
	expect_failure();
}

static void completed_spare_cleanup_matrix(void)
{
	struct payload_mm_authvar_ftw_plan result;

	make_media();
	make_workspace(working(), 0xfe);
	memcpy(spare(), region, BLOCK_SIZE);
	for (size_t cut = 1; cut < BLOCK_SIZE; cut++) {
		memcpy(spare(), region, BLOCK_SIZE);
		memset(spare(), 0xff, cut);
		result = plan_result();
		if (all_bytes_are(spare(), BLOCK_SIZE, 0xffU)) {
			assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		} else {
			assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED);
			assert(result.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE);
		}
	}
	memcpy(spare(), region, BLOCK_SIZE);
	spare()[0] = 0xffU;
	spare()[20] = 0xffU;
	spare()[511] = 0xffU;
	spare()[2047] = 0xffU;
	result = plan_result();
	assert(result.action == PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED);
	memcpy(spare(), region, BLOCK_SIZE);
	spare()[40] ^= 1U;
	expect_failure();
	memset(spare(), 0xff, BLOCK_SIZE);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
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
				&result) == CB_SUCCESS && result.action ==
				PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED &&
				result.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE);
		}
	}
	make_workspace(spare(), 0xfe);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE);

	memset(spare(), 0xff, BLOCK_SIZE);
	for (size_t cut = 1; cut < BLOCK_SIZE; cut++) {
		memset(working(), 0xff, BLOCK_SIZE);
		memcpy(working(), incomplete_workspace, cut);
		assert(payload_mm_authvar_ftw_plan(region, sizeof(region), BLOCK_SIZE,
			&result) == CB_SUCCESS && result.action ==
			PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED &&
			result.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING);
	}
	make_workspace(spare(), 0xfe);

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
	queue_reclaim_boundaries();
	restore_queue_validation();
	workspace_reclaim_reset_matrix();
	completed_spare_cleanup_matrix();
	prefix_program_cuts();
	return 0;
}
