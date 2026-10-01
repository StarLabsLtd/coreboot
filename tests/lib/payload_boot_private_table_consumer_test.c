/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Parse the actual producer's file bytes without rewriting its accepted table.
 * File transfer is a host fixture, not authoritative entry or DMA delivery.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "coreboot_hobs.h"

static uint8_t table[4096] __aligned(16);
static uint8_t hobs[4096] __aligned(4096);

int main(int argc, char **argv)
{
	struct cdk2_coreboot_handoff coreboot;
	struct cb_payload_boot_private_buffer buffer;
	EFI_HOB_HANDOFF_INFO_TABLE *handoff;
	const EFI_HOB_MEMORY_ALLOCATION *allocation;
	FILE *input;
	size_t bytes;
	uint64_t allocation_address;
	struct cb_header *header = (void *)table;
	struct cb_record *record;
	struct cb_payload_boot_private_buffer *private_record;
	bool hostile;

	assert(argc == 3);
	input = fopen(argv[1], "rb");
	assert(input);
	bytes = fread(table, 1, sizeof(table), input);
	assert(bytes && bytes < sizeof(table) && !ferror(input) && !fclose(input));
	assert(cdk2_coreboot_parse_table(table, bytes, &coreboot) == EFI_SUCCESS);
	record = (void *)(table + header->header_bytes);
	assert(record->tag == CB_TAG_MEMORY);
	record = (void *)((uint8_t *)record + record->size);
	assert(record->tag == CB_TAG_AUTHVAR_SERVICE_ENDPOINT);
	private_record = (void *)((uint8_t *)record + record->size);
	assert(private_record->tag == 0x58 && private_record->size == 40);
	if (!strcmp(argv[2], "checksum")) {
		table[bytes - 1] ^= 1;
		assert(cdk2_coreboot_parse_table(table, bytes, &coreboot) == EFI_COMPROMISED_DATA);
		return 0;
	}
	if (!strcmp(argv[2], "truncated")) {
		assert(cdk2_coreboot_parse_table(table, bytes - 1, &coreboot) == EFI_COMPROMISED_DATA);
		return 0;
	}
	hostile = strcmp(argv[2], "valid") != 0;
	if (!strcmp(argv[2], "nonreserved")) {
		struct cb_memory *memory = (void *)(table + header->header_bytes);
		size_t count = (memory->size - sizeof(*memory)) / sizeof(memory->map[0]);

		for (size_t index = 0; index < count; index++)
			if (memory->map[index].type == CB_MEM_RESERVED)
				memory->map[index].type = CB_MEM_RAM;
	} else if (!strcmp(argv[2], "wrong-geometry")) {
		private_record->slot_size--;
	} else if (!strcmp(argv[2], "duplicate")) {
		assert(bytes + sizeof(*private_record) <= sizeof(table));
		memcpy(table + bytes, private_record, sizeof(*private_record));
		bytes += sizeof(*private_record);
		header->table_bytes += sizeof(*private_record);
		header->table_entries++;
	} else {
		assert(!hostile);
	}
	header->table_checksum = cdk2_coreboot_checksum16(table + header->header_bytes,
		header->table_bytes);
	header->header_checksum = 0;
	header->header_checksum = cdk2_coreboot_checksum16(header, header->header_bytes);
	assert(cdk2_coreboot_parse_table(table, bytes, &coreboot) == EFI_SUCCESS);
	if (hostile) {
		struct cb_payload_boot_private_buffer sentinel;
		uint8_t before[sizeof(hobs)];

		memset(&buffer, 0xa5, sizeof(buffer));
		sentinel = buffer;
		assert(cdk2_coreboot_import_boot_private_buffer(&coreboot, &buffer) == EFI_COMPROMISED_DATA);
		assert(!memcmp(&buffer, &sentinel, sizeof(buffer)));
		assert(cdk2_coreboot_build_hobs(&coreboot, hobs, hobs + sizeof(hobs),
			hobs, hobs + sizeof(hobs), FALSE, (void **)&handoff) == EFI_SUCCESS);
		memcpy(before, hobs, sizeof(hobs));
		assert(cdk2_coreboot_reserve_boot_private_buffer(&coreboot, handoff, &buffer) ==
			EFI_COMPROMISED_DATA);
		assert(!memcmp(&buffer, &sentinel, sizeof(buffer)) && !memcmp(before, hobs, sizeof(hobs)));
		return 0;
	}
	assert(cdk2_coreboot_import_boot_private_buffer(&coreboot, &buffer) == EFI_SUCCESS);
	assert(buffer.tag == 0x58 && buffer.size == 40 && buffer.revision == 1 &&
		buffer.header_size == 40 && buffer.slot_size == 65536 &&
		buffer.bytes == 196608 && buffer.slot_count == 3 &&
		!buffer.reserved[0] && !buffer.reserved[1]);
	assert(cdk2_coreboot_build_hobs(&coreboot, hobs, hobs + sizeof(hobs),
		hobs, hobs + sizeof(hobs), FALSE, (void **)&handoff) == EFI_SUCCESS);
	allocation_address = handoff->efi_end_of_hob_list;
	assert(cdk2_coreboot_reserve_boot_private_buffer(&coreboot, handoff, &buffer) == EFI_SUCCESS);
	allocation = (void *)(uintptr_t)allocation_address;
	assert(allocation->header.hob_type == EFI_HOB_TYPE_MEMORY_ALLOCATION &&
		allocation->alloc_descriptor.memory_base_address ==
			((uint64_t)buffer.physical_base.hi << 32 | buffer.physical_base.lo) &&
		allocation->alloc_descriptor.memory_length == 196608 &&
		allocation->alloc_descriptor.memory_type == efi_reserved_memory_type);
	return 0;
}
