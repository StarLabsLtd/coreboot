/* SPDX-License-Identifier: GPL-2.0-only */

#include "coreboot.h"

#include <cdk2/dma_handoff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GENERATION 0x1122334455667788ULL
#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static struct cb_dma_handoff reference;
static struct {
	struct cb_payload_resource_handoff header;
	struct cb_payload_resource_section section[2];
	struct cb_prh_pci_topology_entry topology[2];
	struct cb_prh_boot_intent_entry boot[2];
} __packed resource;

EFI_STATUS cdk2_coreboot_find_unique_record(
	const struct cdk2_coreboot_handoff *handoff, UINT32 tag,
	UINT32 minimum_size, const void **record)
{
	(void)handoff;
	CHECK(tag == CB_TAG_DMA_HANDOFF);
	CHECK(minimum_size == sizeof(reference));
	*record = &reference;
	return EFI_SUCCESS;
}

EFI_STATUS cdk2_coreboot_find_payload_resource_section(
	const struct cdk2_coreboot_handoff *handoff, UINT16 type,
	const struct cb_payload_resource_section **section)
{
	CHECK(handoff != NULL && section != NULL);
	if (type == CB_PRH_SECTION_PCI_TOPOLOGY)
		*section = &resource.section[0];
	else if (type == CB_PRH_SECTION_BOOT_INTENT)
		*section = &resource.section[1];
	else
		return EFI_NOT_FOUND;
	return EFI_SUCCESS;
}

int main(int argc, char **argv)
{
	UINT8 table[sizeof(struct cb_header) + sizeof(struct cb_cbmem_entry)] = {0};
	struct cb_header *header = (void *)table;
	struct cb_cbmem_entry *entry = (void *)(header + 1);
	struct cdk2_dma_handoff_state state;
	struct cdk2_coreboot_handoff coreboot = {
		.header = header,
		.table_size = sizeof(table),
		.record_count = 1,
		.payload_resource_handoff_status = EFI_SUCCESS,
		.payload_resource_handoff = &resource.header,
	};
	UINT8 blob[512];
	UINT64 address = (UINTN)blob;
	FILE *fixture;
	size_t bytes;

	CHECK(argc == 2);
	fixture = fopen(argv[1], "rb");
	CHECK(fixture != NULL);
	bytes = fread(blob, 1, sizeof(blob), fixture);
	CHECK(!ferror(fixture) && feof(fixture) && fclose(fixture) == 0);
	CHECK(bytes == 104);
	header->header_bytes = sizeof(*header);
	*entry = (struct cb_cbmem_entry){
		.tag = CB_TAG_CBMEM_ENTRY,
		.size = sizeof(*entry),
		.address = { (UINT32)address, (UINT32)(address >> 32) },
		.entry_size = bytes,
		.id = CBMEM_ID_DMA_HANDOFF,
	};
	resource.header.revision = CB_PAYLOAD_RESOURCE_HANDOFF_REVISION_4;
	resource.header.producer_generation.lo = (UINT32)GENERATION;
	resource.header.producer_generation.hi = (UINT32)(GENERATION >> 32);
	resource.section[0] = (struct cb_payload_resource_section) {
		.type = CB_PRH_SECTION_PCI_TOPOLOGY,
		.entry_size = sizeof(resource.topology[0]),
		.entry_count = 2,
		.offset = OFFSET_OF(__typeof__(resource), topology),
		.length = sizeof(resource.topology),
	};
	resource.section[1] = (struct cb_payload_resource_section) {
		.type = CB_PRH_SECTION_BOOT_INTENT,
		.entry_size = sizeof(resource.boot[0]),
		.entry_count = 2,
		.offset = OFFSET_OF(__typeof__(resource), boot),
		.length = sizeof(resource.boot),
	};
	resource.topology[0].device = 3;
	resource.topology[1].device = 4;
	resource.boot[0].topology_index = 0;
	resource.boot[1].topology_index = 1;
	reference = (struct cb_dma_handoff){
		.tag = CB_TAG_DMA_HANDOFF,
		.size = sizeof(reference),
		.address = (UINTN)blob,
		.bytes = bytes,
		.revision = DMA_HANDOFF_REVISION,
	};
	CHECK(cdk2_dma_handoff_import(&coreboot, &state) == EFI_SUCCESS);
	CHECK(state.valid && state.generation == GENERATION &&
		state.requester_count == 2 &&
		state.requesters[0].bdf == 0x18 &&
		state.requesters[0].protection_domain == 1 &&
		state.requesters[0].arena_cpu_base == 0x200000 &&
		state.requesters[0].arena_device_base == 0x80000000 &&
		state.requesters[0].arena_pages == 32 &&
		state.requesters[0].arena_flags == DMA_HANDOFF_ARENA_FLAGS &&
		state.requesters[1].bdf == 0x20 &&
		state.requesters[1].protection_domain == 2 &&
		state.requesters[1].arena_cpu_base == 0x220000 &&
		state.requesters[1].arena_device_base == 0x90000000 &&
		state.requesters[1].arena_pages == 128 &&
		state.requesters[1].arena_flags == DMA_HANDOFF_ARENA_FLAGS);
	entry->entry_size = 4096;
	CHECK(cdk2_dma_handoff_import(&coreboot, &state) == EFI_COMPROMISED_DATA);
	CHECK(!state.valid);
	puts("coreboot Q35 producer -> CDK2 consumer: PASS");
	return 0;
}
