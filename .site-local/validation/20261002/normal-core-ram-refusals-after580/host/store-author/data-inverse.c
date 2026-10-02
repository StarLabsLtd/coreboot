/* SPDX-License-Identifier: GPL-2.0-only */

/* Read-only HOST oracle; actual producer decoders, never guest admission. */
#include <boot/payload_mm_authvar.h>
#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_store.h>
#include <commonlib/helpers.h>
#include <uuid.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define REGION_SIZE (64U * 1024U)
#define ENTRY_CAPACITY 128U

static const uint16_t state_name[] = { 'F', 'm', 'p', 'S', 't', 'a', 't', 'e', 0 };
static uint8_t media[REGION_SIZE];
static uint8_t changed[REGION_SIZE];

static bool matches(const uint8_t *region, const uint8_t *guid,
	uint32_t *record_offset, uint32_t *data_offset)
{
	const uint8_t zero[PAYLOAD_MM_FMP_STATE_WIRE_SIZE] = { 0 };
	struct payload_mm_authvar_fv_geometry geometry;
	struct payload_mm_authvar_ftw_plan plan;
	struct payload_mm_authvar_store_entry entries[ENTRY_CAPACITY];
	struct payload_mm_authvar_store_index index = {
		.entries = entries, .entry_capacity = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = REGION_SIZE, .maximum_name_size = 4096U,
		.maximum_data_size = REGION_SIZE, .maximum_records = ENTRY_CAPACITY,
	};
	const struct payload_mm_authvar_store_entry *entry;
	const void *data;

	if (!payload_mm_authvar_fv_geometry(&geometry, REGION_SIZE, 4096U))
		return false;
	if (payload_mm_authvar_ftw_plan(region, REGION_SIZE, 4096U, &plan) != CB_SUCCESS ||
	    plan.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    plan.workspace != PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING ||
	    plan.queue_disposition != PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY ||
	    plan.queue_offset != PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE ||
	    plan.queue_entry_size != 0U)
		return false;
	if (payload_mm_authvar_store_scan(&index,
	    region + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE,
	    geometry.variable_size - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE, &limits) != CB_SUCCESS ||
	    index.dirty_tail_offset != 0U)
		return false;
	entry = payload_mm_authvar_store_find(&index, guid, state_name, sizeof(state_name));
	if (entry == NULL || entry->attributes != PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES ||
	    entry->data_size != sizeof(zero))
		return false;
	data = payload_mm_authvar_store_data(&index, entry);
	if (data == NULL || memcmp(data, zero, sizeof(zero)) != 0)
		return false;
	if (record_offset != NULL)
		*record_offset = PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + entry->record_offset;
	if (data_offset != NULL)
		*data_offset = PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + entry->data_offset;
	return true;
}

static void self_test(const uint8_t *guid, uint32_t record_offset, uint32_t data_offset)
{
	uint8_t other_guid[UUID_LEN];
	struct payload_mm_authvar_fv_geometry geometry;
	bool attributes_refused;

	assert(payload_mm_authvar_fv_geometry(&geometry, REGION_SIZE, 4096U));
	for (size_t byte = 0U; byte < PAYLOAD_MM_FMP_STATE_WIRE_SIZE; byte++) {
		bool data_refused;

		memcpy(changed, media, sizeof(changed));
		changed[data_offset + byte] ^= 1U;
		data_refused = !matches(changed, guid, NULL, NULL);
		assert(data_refused);
	}
	/*
	 * Attributes are at byte four in the actual authenticated-variable header.
	 * Adding RT keeps a valid record but must not alter the owner's attrs3.
	 */
	memcpy(changed, media, sizeof(changed));
	changed[record_offset + 4U] |= 4U;
	attributes_refused = !matches(changed, guid, NULL, NULL);
	assert(attributes_refused);
	for (size_t byte = 0U; byte < sizeof(other_guid); byte++) {
		bool guid_refused;

		memcpy(other_guid, guid, sizeof(other_guid));
		other_guid[byte] ^= 1U;
		guid_refused = !matches(media, other_guid, NULL, NULL);
		assert(guid_refused);
	}
	for (size_t byte = 0U; byte < PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE; byte++) {
		bool workspace_refused;

		memcpy(changed, media, sizeof(changed));
		changed[geometry.working_offset + byte] ^= 1U;
		workspace_refused = !matches(changed, guid, NULL, NULL);
		assert(workspace_refused);
	}
}

int main(int argc, char **argv)
{
	uint8_t guid[UUID_LEN];
	uint32_t record_offset, data_offset;
	FILE *input;
	bool complete;

	if ((argc != 3 && argc != 4) ||
	    (argc == 4 && strcmp(argv[3], "--self-test") != 0) ||
	    parse_uuid(guid, argv[2]) != 0)
		return 2;
	input = fopen(argv[1], "rb");
	if (input == NULL)
		return 2;
	complete = fread(media, 1U, sizeof(media), input) == sizeof(media) &&
		fgetc(input) == EOF && !ferror(input);
	if (fclose(input) != 0 || !complete)
		return 2;
	if (!matches(media, guid, &record_offset, &data_offset))
		return 1;
	if (argc == 4)
		self_test(guid, record_offset, data_offset);
	puts("Actual producer store: exact namespace/attrs3/20B zero FmpState and clean FTW: PASS");
	return 0;
}
