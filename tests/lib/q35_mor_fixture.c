/* SPDX-License-Identifier: GPL-2.0-only */

#include "q35_mor_fixture.h"

#include <string.h>

#define EDK2_FV_HEADER_SIZE 72U
#define EDK2_STORE_HEADER_SIZE 28U
#define EDK2_RECORD_HEADER_SIZE 60U
#define EDK2_VARIABLE_SIZE Q35_MOR_FIXTURE_BLOCK_SIZE
#define EDK2_WORKING_OFFSET Q35_MOR_FIXTURE_BLOCK_SIZE
#define EDK2_STORE_SIZE (EDK2_VARIABLE_SIZE - EDK2_FV_HEADER_SIZE)
#define EDK2_CONTROL_RECORD_SIZE 124U
#define EDK2_CONTROL_RECORD_OFFSET EDK2_STORE_HEADER_SIZE
#define EDK2_REPLACEMENT_RECORD_OFFSET \
	(EDK2_CONTROL_RECORD_OFFSET + EDK2_CONTROL_RECORD_SIZE)

static const uint8_t edk2_fv_guid[16] = {
	0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
	0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
};
static const uint8_t edk2_store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};
static const uint8_t edk2_work_guid[16] = {
	0x2b, 0x29, 0x58, 0x9e, 0x68, 0x7c, 0x7d, 0x49,
	0xa0, 0xce, 0x65, 0x00, 0xfd, 0x9f, 0x1b, 0x95,
};
static const uint8_t mor_guid[16] = {
	0xbe, 0x39, 0x09, 0xe2, 0xd4, 0x32, 0xbe, 0x41,
	0xa1, 0x50, 0x89, 0x7f, 0x85, 0xd4, 0x98, 0x29,
};
static const uint8_t mor_name_utf16le[] = {
	0x4d, 0x00, 0x65, 0x00, 0x6d, 0x00, 0x6f, 0x00,
	0x72, 0x00, 0x79, 0x00, 0x4f, 0x00, 0x76, 0x00,
	0x65, 0x00, 0x72, 0x00, 0x77, 0x00, 0x72, 0x00,
	0x69, 0x00, 0x74, 0x00, 0x65, 0x00, 0x52, 0x00,
	0x65, 0x00, 0x71, 0x00, 0x75, 0x00, 0x65, 0x00,
	0x73, 0x00, 0x74, 0x00, 0x43, 0x00, 0x6f, 0x00,
	0x6e, 0x00, 0x74, 0x00, 0x72, 0x00, 0x6f, 0x00,
	0x6c, 0x00, 0x00, 0x00,
};

static void write_le16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
	for (size_t index = 0; index < sizeof(value); index++)
		bytes[index] = (uint8_t)(value >> (8U * index));
}

static void write_le64(uint8_t *bytes, uint64_t value)
{
	for (size_t index = 0; index < sizeof(value); index++)
		bytes[index] = (uint8_t)(value >> (8U * index));
}

static uint32_t crc32(const uint8_t *bytes, size_t size)
{
	uint32_t crc = UINT32_MAX;

	for (size_t index = 0; index < size; index++) {
		crc ^= bytes[index];
		for (unsigned int bit = 0; bit < 8U; bit++)
			crc = (crc >> 1) ^
				(0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

static void format_fv(uint8_t *media)
{
	uint16_t checksum = 0;

	memset(media, 0xff, Q35_MOR_FIXTURE_SIZE);
	memset(media, 0, EDK2_FV_HEADER_SIZE);
	memcpy(media + 16U, edk2_fv_guid, sizeof(edk2_fv_guid));
	write_le64(media + 32U, Q35_MOR_FIXTURE_SIZE);
	write_le32(media + 40U, 0x4856465fU);
	write_le32(media + 44U, 0x00000e36U);
	write_le16(media + 48U, EDK2_FV_HEADER_SIZE);
	media[55U] = 2U;
	write_le32(media + 56U, Q35_MOR_FIXTURE_BLOCKS);
	write_le32(media + 60U, Q35_MOR_FIXTURE_BLOCK_SIZE);
	for (size_t index = 0; index < EDK2_FV_HEADER_SIZE; index += 2U)
		checksum = (uint16_t)(checksum + (uint16_t)media[index] +
			((uint16_t)media[index + 1U] << 8));
	write_le16(media + 50U, (uint16_t)-checksum);
	memset(media + EDK2_FV_HEADER_SIZE, 0, EDK2_STORE_HEADER_SIZE);
	memcpy(media + EDK2_FV_HEADER_SIZE, edk2_store_guid,
		sizeof(edk2_store_guid));
	write_le32(media + EDK2_FV_HEADER_SIZE + 16U, EDK2_STORE_SIZE);
	media[EDK2_FV_HEADER_SIZE + 20U] = 0x5aU;
	media[EDK2_FV_HEADER_SIZE + 21U] = 0xfeU;
}

static void format_workspace(uint8_t *media)
{
	uint8_t *workspace = media + EDK2_WORKING_OFFSET;

	memcpy(workspace, edk2_work_guid, sizeof(edk2_work_guid));
	write_le64(workspace + 24U,
		Q35_MOR_FIXTURE_BLOCK_SIZE - PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE);
	write_le32(workspace + 16U,
		crc32(workspace, PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE));
	workspace[PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET] =
		PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID;
}

static void add_control(uint8_t *media, uint32_t record_offset,
	uint8_t state, uint8_t value, uint32_t attributes)
{
	uint8_t *record = media + EDK2_FV_HEADER_SIZE + record_offset;

	memset(record, 0xff, EDK2_CONTROL_RECORD_SIZE);
	write_le16(record, 0x55aaU);
	record[2U] = state;
	record[3U] = 0U;
	write_le32(record + 4U, attributes);
	memset(record + 8U, 0, 28U);
	write_le32(record + 36U, sizeof(mor_name_utf16le));
	write_le32(record + 40U, 1U);
	memcpy(record + 44U, mor_guid, sizeof(mor_guid));
	memcpy(record + EDK2_RECORD_HEADER_SIZE, mor_name_utf16le,
		sizeof(mor_name_utf16le));
	record[120U] = value;
}

bool q35_mor_fixture_generate(enum q35_mor_fixture_kind kind,
	uint8_t media[Q35_MOR_FIXTURE_SIZE],
	struct q35_mor_fixture_expectation *expectation)
{
	uint8_t value = 0x11U;
	bool clean = true;
	bool control = true;
	uint32_t attributes = 0x00000007U;

	if (!media || !expectation || (int)kind < 0 ||
	    (int)kind >= (int)Q35_MOR_FIXTURE_COUNT)
		return false;
	format_fv(media);
	*expectation = (struct q35_mor_fixture_expectation) {
		.probe_status = CB_SUCCESS,
		.ftw_status = CB_SUCCESS,
		.entry = { .present = 1U, .value = value },
		.ftw_action = PAYLOAD_MM_AUTHVAR_FTW_CLEAN,
		.clear_allowed = true,
	};
	switch (kind) {
	case Q35_MOR_FIXTURE_ASSERTED_UPPER_BITS:
	case Q35_MOR_FIXTURE_IDEMPOTENT_BEFORE:
		break;
	case Q35_MOR_FIXTURE_NO_REQUEST:
		value = 0x10U;
		expectation->entry.value = value;
		expectation->clear_allowed = false;
		break;
	case Q35_MOR_FIXTURE_ABSENT:
		control = false;
		expectation->entry = (struct payload_mm_authvar_mor_entry) { 0 };
		expectation->clear_allowed = false;
		break;
	case Q35_MOR_FIXTURE_MALFORMED:
		attributes = 0x00000003U;
		expectation->probe_status = CB_ERR;
		expectation->entry = (struct payload_mm_authvar_mor_entry) { 0 };
		expectation->clear_allowed = false;
		break;
	case Q35_MOR_FIXTURE_RECOVERY:
		clean = false;
		expectation->probe_status = CB_ERR;
		expectation->entry = (struct payload_mm_authvar_mor_entry) { 0 };
		expectation->ftw_action =
			PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE;
		expectation->clear_allowed = false;
		break;
	case Q35_MOR_FIXTURE_FAULT:
		expectation->probe_status = CB_ERR;
		expectation->ftw_status = CB_ERR;
		expectation->entry = (struct payload_mm_authvar_mor_entry) { 0 };
		expectation->ftw_action = PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		expectation->clear_allowed = false;
		break;
	case Q35_MOR_FIXTURE_IDEMPOTENT_AFTER:
		add_control(media, EDK2_CONTROL_RECORD_OFFSET, 0x3cU, 0x11U,
			attributes);
		add_control(media, EDK2_REPLACEMENT_RECORD_OFFSET, 0x3fU, 0x10U,
			attributes);
		value = 0x10U;
		expectation->entry.value = value;
		expectation->clear_allowed = false;
		control = false;
		break;
	case Q35_MOR_FIXTURE_S3:
		expectation->resume_from_s3 = true;
		expectation->clear_allowed = false;
		break;
	case Q35_MOR_FIXTURE_COUNT:
		return false;
	}
	if (control)
		add_control(media, EDK2_CONTROL_RECORD_OFFSET, 0x3fU, value,
			attributes);
	if (clean)
		format_workspace(media);
	if (kind == Q35_MOR_FIXTURE_FAULT)
		media[50U] ^= 1U;
	return true;
}
