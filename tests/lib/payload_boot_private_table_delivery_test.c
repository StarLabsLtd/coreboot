/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Actual allocator, receipt, combined sender and table publication owners.
 * The trigger models admitted BOOT/provider finalization, not SMM placement,
 * origin authority or protection of a resumed CPU's subsequent copy interval.
 */
#include <boot/coreboot_tables.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/bsd/ipchksum.h>
#include <cpu/x86/smm_command.h>
#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_install.h"

#define die bootmem_fixture_die
#define BOOT_PRIVATE_TABLE_COMPONENT
#include "payload_boot_private_loader_delivery_test.c"
#undef die

struct lb_header *boot_private_table_initialize(uintptr_t base);
void boot_private_table_finish(struct lb_header *header);
void boot_private_table_memory(struct lb_header *header);
extern void exit(int status) __noreturn;
extern long write(int descriptor, const void *bytes, unsigned long size);

static struct starbook_mtl_presence_bootstrap_frame shared_frame;
static struct payload_mm_authvar_presence_bootstrap protected_slot;
static uint8_t table_bytes[4096] __aligned(16);
static const char *scenario;

uint16_t pm_acpi_smi_cmd_port(void)
{
	return 0xb2;
}

enum cb_err payload_mm_authvar_presence_publication_loader_required(bool *required)
{
	*required = true;
	return CB_SUCCESS;
}

bool starbook_mtl_dma_receipt_transport_frame(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)&shared_frame;
	*size = sizeof(shared_frame);
	return true;
}

uint64_t starbook_mtl_presence_bootstrap_trigger_test(uint32_t request, uint32_t frame)
{
	struct bootmem_reservation_receipt private_receipt = shared_frame.boot_private;
	struct lb_payload_boot_private_buffer consumed;

	CHECK(request == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST &&
		frame == (uintptr_t)&shared_frame);
	CHECK(payload_boot_private_buffer_consume(&protected_slot.boot_private_verifier,
		&private_receipt, &consumed) == CB_SUCCESS);
	CHECK(consumed.physical_base == shared_frame.boot_private.base);
	if (!strcmp(scenario, "owner-drift")) {
		/* Recommit the actual owner, keeping the original local receipt unchanged. */
		resources[0].size -= 4096;
		initialize();
	}
	shared_frame.state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED;
	shared_frame.maximum_cpus = CONFIG_MAX_CPUS;
	shared_frame.service_endpoint = (struct lb_authvar_service_endpoint) {
		.tag = LB_TAG_AUTHVAR_SERVICE_ENDPOINT,
		.size = sizeof(shared_frame.service_endpoint),
		.revision = LB_AUTHVAR_SERVICE_ENDPOINT_REVISION,
		.header_size = sizeof(shared_frame.service_endpoint),
		.flags = LB_AUTHVAR_ENDPOINT_REQUIRED_FLAGS,
		.generation = shared_frame.receipts.service.generation,
		.communication_base = shared_frame.receipts.service.base,
		.communication_size = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE,
		.message_size = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE,
		.transport = LB_AUTHVAR_ENDPOINT_TRANSPORT_APM_IO8,
		.trigger_width = 1, .trigger_address = 0xb2,
		.trigger_value = SMM_APMC_AUTHVAR_SERVICE,
		.maximum_name_size = 256,
		.maximum_data_size = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE -
			PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE - 256U,
	};
	if (!strcmp(scenario, "failed-ack"))
		return 0;
	if (!strcmp(scenario, "mixed-revision"))
		shared_frame.revision = 2;
	if (!strcmp(scenario, "wrong-span"))
		shared_frame.boot_private.bytes += 4096;
	if (!strcmp(scenario, "wrong-handle"))
		shared_frame.boot_private.handle.opaque[0]++;
	return STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS;
}

void die(const char *format, ...)
{
	const struct lb_header *header = (void *)table_bytes;

	(void)format;
	CHECK(strcmp(scenario, "valid") && header->table_entries == 1 && !header->table_bytes);
	exit(77);
}

int main(int argc, char **argv)
{
	const struct smm_invocation_loader_instance instance = {
		.state = SMM_INVOCATION_LOADER_INSTANCE_READY,
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION,
		.size = sizeof(instance), .lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.loader_instance_nonce = { .low = 0x123, .high = 0x456 },
	};
	const struct smm_invocation_topology topology = {
		.state = SMM_INVOCATION_TOPOLOGY_READY,
		.revision = SMM_INVOCATION_TOPOLOGY_REVISION, .size = sizeof(topology),
		.active_cpus = CONFIG_MAX_CPUS,
	};
	struct lb_header *header;
	const struct lb_record *record;

	CHECK(argc == 2);
	scenario = argv[1];
	CHECK(payload_mm_authvar_presence_tuple_sender_reserve() == CB_SUCCESS);
	CHECK(payload_mm_authvar_presence_tuple_sender_loader_provision(&protected_slot,
		&instance, &topology, true) == CB_SUCCESS);
	header = boot_private_table_initialize((uintptr_t)table_bytes);
	boot_private_table_memory(header);
	lb_board(header);
	CHECK((!strcmp(scenario, "valid") || !strcmp(scenario, "owner-drift")) &&
		header->table_entries == 3);
	boot_private_table_finish(header);
	CHECK(ipchksum(header, sizeof(*header)) == 0);
	CHECK(ipchksum(header + 1, header->table_bytes) == header->table_checksum);
	record = (const void *)(header + 1);
	CHECK(record->tag == LB_TAG_MEMORY);
	record = (const void *)((const uint8_t *)record + record->size);
	CHECK(record->tag == LB_TAG_AUTHVAR_SERVICE_ENDPOINT);
	record = (const void *)((const uint8_t *)record + record->size);
	CHECK(record->tag == LB_TAG_PAYLOAD_BOOT_PRIVATE_BUFFER && record->size == 40);
	CHECK(write(1, table_bytes, sizeof(*header) + header->table_bytes) ==
		(long)(sizeof(*header) + header->table_bytes));
	return 0;
}
