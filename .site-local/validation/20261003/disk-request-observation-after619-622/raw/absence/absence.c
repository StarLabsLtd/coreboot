/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_store.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	/* Exact selected Q35 SMMSTORE region, read-only cbfstool extraction. */
	uint8_t region[65536];
	struct payload_mm_authvar_fv_geometry geometry;
	struct payload_mm_authvar_ftw_plan plan;
	struct payload_mm_authvar_store_entry entries[512], single;
	struct payload_mm_authvar_store_index index = {
		.entries = entries, .entry_capacity = 512,
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = sizeof(region), .maximum_name_size = 4096,
		.maximum_data_size = sizeof(region), .maximum_records = 512,
	};
	/* EFI_GLOBAL_VARIABLE GUID in actual EFI wire byte order. */
	const uint8_t global_guid[16] = {
		0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
		0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
	};
	const uint8_t name[] = {
		'O', 0, 's', 0, 'I', 0, 'n', 0, 'd', 0, 'i', 0, 'c', 0,
		'a', 0, 't', 0, 'i', 0, 'o', 0, 'n', 0, 's', 0, 0, 0,
	};
	bool found = true, recorded = true, erased = true;
	assert(argc == 3);
	assert(!strcmp(argv[1], "initial") || !strcmp(argv[1], "final"));
	FILE *input = fopen(argv[2], "rb");
	assert(input);
	assert(fread(region, 1, sizeof(region), input) == sizeof(region));
	assert(fgetc(input) == EOF && !ferror(input));
	assert(fclose(input) == 0);
	assert(payload_mm_authvar_fv_geometry(&geometry, sizeof(region), 4096));
	for (size_t i = 0; i < sizeof(region); i++)
		erased &= region[i] == 0xff;
	if (erased) {
		assert(!strcmp(argv[1], "initial"));
		puts("initial: physically erased/uninitialized; no encoded records; not a formatted-codec claim");
		return 0;
	}
	assert(payload_mm_authvar_ftw_plan(region, sizeof(region), 4096, &plan) == CB_SUCCESS);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	assert(plan.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING);
	/* CLEAN may retain decoder-validated completed history, not active work. */
	assert(plan.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY ||
		plan.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE);
	assert(plan.queue_entry_size == 0);
	assert(memcmp(&geometry, &plan.geometry, sizeof(geometry)) == 0);
	assert(plan.fv_header_size <= geometry.variable_size);
	assert(plan.variable_store_size <= geometry.variable_size - plan.fv_header_size);
	const uint8_t *store = region + geometry.variable_offset + plan.fv_header_size;
	assert(payload_mm_authvar_store_scan(&index, store, plan.variable_store_size,
		&limits) == CB_SUCCESS);
	assert(index.dirty_tail_offset == 0);
	assert(payload_mm_authvar_store_index_valid(&index));
	assert(payload_mm_authvar_store_find(&index, global_guid, name, sizeof(name)) == NULL);
	assert(payload_mm_authvar_store_find_one(&single, &found, store,
		plan.variable_store_size, &limits, global_guid, name, sizeof(name)) == CB_SUCCESS);
	assert(!found);
	assert(payload_mm_authvar_store_key_recorded(&index, global_guid, name,
		sizeof(name), &recorded) == CB_SUCCESS);
	printf("%s: formatted clean FTW; fully decoded records=%u current=%u; OsIndications absent; historical_key_recorded=%u\n",
		argv[1], index.record_count, index.entry_count, (unsigned int)recorded);
	return 0;
}
