/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_mor_identity.h>
#include <boot/payload_mm_authvar_record.h>
#include <boot/payload_mm_authvar_store.h>
#include <commonlib/payload_mm_authvar_fv.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURE_SIZE (64U * 1024U)
#define FIXTURE_BLOCK_SIZE 4096U
#define VARIABLE_STORE_HEADER_SIZE 28U
#define MOR_ASSERTED 0x11U
#define MOR_CLEARED 0x10U

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

static bool make_fixture(uint8_t media[FIXTURE_SIZE])
{
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	struct payload_mm_authvar_record_descriptor source = {
		.name = identity->name,
		.name_size = identity->name_size,
		.attributes = PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES,
	};
	const uint8_t value = MOR_ASSERTED;
	const struct payload_mm_authvar_record_span span = {
		.data = &value,
		.size = sizeof(value),
	};
	struct payload_mm_authvar_fv_geometry geometry;
	uint8_t record[256];
	uint8_t *workspace;
	uint32_t record_size;

	memcpy(source.vendor_guid, identity->vendor_guid,
		sizeof(source.vendor_guid));
	if (!payload_mm_authvar_fv_format(media, FIXTURE_SIZE,
		FIXTURE_BLOCK_SIZE) ||
	    !payload_mm_authvar_fv_geometry(&geometry, FIXTURE_SIZE,
		FIXTURE_BLOCK_SIZE) ||
	    !payload_mm_authvar_record_encode(&source, &span, 1U,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED, record,
		sizeof(record), &record_size))
		return false;
	record[2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	memcpy(media + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE +
		VARIABLE_STORE_HEADER_SIZE, record, record_size);

	workspace = media + geometry.working_offset;
	memcpy(workspace, payload_mm_authvar_ftw_working_block_guid,
		sizeof(payload_mm_authvar_ftw_working_block_guid));
	write_le64(workspace + 24U,
		geometry.working_size - PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE);
	write_le32(workspace + 16U,
		crc32(workspace, PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE));
	workspace[PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET] =
		PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID;
	return true;
}

static bool fixture_value(const uint8_t media[FIXTURE_SIZE], uint8_t *value)
{
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_SIZE,
		.maximum_name_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE,
		.maximum_data_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE,
		.maximum_records = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS,
	};
	struct payload_mm_authvar_store_entry entry;
	struct payload_mm_authvar_ftw_plan plan;
	const uint8_t *store;
	bool found;

	if (payload_mm_authvar_ftw_plan(media, FIXTURE_SIZE,
		FIXTURE_BLOCK_SIZE, &plan) != CB_SUCCESS ||
	    plan.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN)
		return false;
	store = media + plan.geometry.variable_offset + plan.fv_header_size;
	if (payload_mm_authvar_store_find_one(&entry, &found, store,
		plan.variable_store_size, &limits, identity->vendor_guid,
		identity->name, identity->name_size) != CB_SUCCESS || !found ||
	    entry.attributes != PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES ||
	    entry.data_size != 1U ||
	    entry.data_offset >= plan.variable_store_size)
		return false;
	*value = store[entry.data_offset];
	return true;
}

static bool read_fixture(const char *path, uint8_t media[FIXTURE_SIZE])
{
	FILE *file = fopen(path, "rb");
	bool ok;

	if (!file)
		return false;
	ok = fread(media, 1U, FIXTURE_SIZE, file) == FIXTURE_SIZE;
	if (fclose(file))
		ok = false;
	return ok;
}

static bool write_fixture(const char *path, const uint8_t media[FIXTURE_SIZE])
{
	FILE *file = fopen(path, "wb");
	bool ok;

	if (!file)
		return false;
	ok = fwrite(media, 1U, FIXTURE_SIZE, file) == FIXTURE_SIZE;
	if (fclose(file))
		ok = false;
	return ok;
}

int main(int argc, char **argv)
{
	uint8_t *media;
	uint8_t value;
	bool ok = false;

	if (argc != 3 || (strcmp(argv[1], "create") &&
	    strcmp(argv[1], "asserted") && strcmp(argv[1], "cleared"))) {
		fprintf(stderr, "usage: %s create|asserted|cleared file\n", argv[0]);
		return 2;
	}
	media = malloc(FIXTURE_SIZE);
	if (!media) {
		perror("malloc");
		return 1;
	}
	if (!strcmp(argv[1], "create")) {
		ok = make_fixture(media) && fixture_value(media, &value) &&
			value == MOR_ASSERTED && write_fixture(argv[2], media);
	} else {
		ok = read_fixture(argv[2], media) && fixture_value(media, &value) &&
			value == (!strcmp(argv[1], "asserted") ?
				MOR_ASSERTED : MOR_CLEARED);
	}
	free(media);
	if (!ok) {
		fprintf(stderr, "%s: invalid Q35 MOR fixture: %s\n", argv[0],
			strerror(errno));
		return 1;
	}
	return 0;
}
