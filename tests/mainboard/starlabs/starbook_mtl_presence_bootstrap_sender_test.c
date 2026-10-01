/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_topology.h>
#include "authvar_presence_bootstrap_install.h"
#include "dma_smm_receipt_provision.h"
#include <boot/coreboot_tables.h>
#include <boot/payload_mm_authvar_presence_publication.h>
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <boot/payload_mm_authvar_service.h>
#include <cpu/x86/smm_command.h>
#include <console/console.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Coreboot's freestanding stdlib header omits these host process helpers. */
extern void exit(int status) __noreturn;
extern unsigned long strtoul(const char *text, char **end, int base);

static struct starbook_mtl_presence_bootstrap_frame frame;
static unsigned int scenario, frame_queries, emissions, triggers;
static struct lb_authvar_service_endpoint table_endpoint;
static unsigned int table_records;

uint16_t pm_acpi_smi_cmd_port(void)
{
	return 0xb2U;
}
struct lb_record *lb_new_record(struct lb_header *header)
{
	if (!header || frame.state != STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED)
		exit(90);
	table_records++;
	if (scenario == 28) {
		/* The frame may change after validation; publication must use the copy. */
		frame.service_endpoint.communication_base += 0x1000U;
		frame.service_endpoint.generation++;
	}
	return (void *)&table_endpoint;
}

static void require(bool condition)
{
	if (!condition)
		exit(90);
}

enum cb_err payload_mm_authvar_presence_publication_loader_required(bool *required)
{
	*required = scenario != 1;
	return scenario == 2 ? CB_ERR : CB_SUCCESS;
}

bool starbook_mtl_dma_receipt_transport_frame(uintptr_t *base, size_t *size)
{
	frame_queries++;
	*base = (uintptr_t)&frame;
	*size = sizeof(frame);
	if (scenario == 3)
		return false;
	if (scenario == 4)
		(*size)--;
	if (scenario == 5)
		(*base)++;
	if (scenario == 6)
		*base = UINT32_MAX - sizeof(frame) + 2U;
	return true;
}

enum cb_err payload_mm_authvar_presence_tuple_sender_receipts_take(
	struct payload_mm_authvar_presence_bootstrap_receipts *receipts,
	struct payload_mm_authvar_presence_tuple_sender *sender,
	struct bootmem_reservation_receipt *boot_private)
{
	emissions++;
	memset(receipts, 0x36, sizeof(*receipts));
	/* Explicit fixture receipts, not a substitute for production authentication. */
	receipts->service.base = 0x700000U;
	receipts->service.bytes = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE;
	receipts->service.generation = 7;
	memset(sender, 0, sizeof(*sender));
	memset(boot_private, 0, sizeof(*boot_private));
	return scenario == 7 ? CB_ERR : CB_SUCCESS;
}

uint64_t starbook_mtl_presence_bootstrap_trigger_test(uint32_t request, uint32_t address)
{
	triggers++;
	require(request == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST);
	require(address == (uintptr_t)&frame);
	require(frame.revision == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION);
	require(frame.size == sizeof(frame));
	require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
	require(!frame.reserved && !frame.initiator_cpu && !frame.maximum_cpus);
	const struct lb_authvar_service_endpoint no_endpoint = { 0 };
	require(!memcmp(&frame.service_endpoint, &no_endpoint, sizeof(no_endpoint)));
	frame.state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED;
	frame.maximum_cpus = CONFIG_MAX_CPUS;
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	frame.service_endpoint = (struct lb_authvar_service_endpoint) {
		.tag = LB_TAG_AUTHVAR_SERVICE_ENDPOINT, .size = sizeof(frame.service_endpoint),
		.revision = LB_AUTHVAR_SERVICE_ENDPOINT_REVISION,
		.header_size = sizeof(frame.service_endpoint), .flags = LB_AUTHVAR_ENDPOINT_REQUIRED_FLAGS,
		.generation = frame.receipts.service.generation,
		.communication_base = frame.receipts.service.base,
		.communication_size = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE,
		.message_size = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE,
		.transport = LB_AUTHVAR_ENDPOINT_TRANSPORT_APM_IO8, .trigger_width = 1,
		.trigger_address = 0xb2U, .trigger_value = SMM_APMC_AUTHVAR_SERVICE,
		.maximum_name_size = 256,
		.maximum_data_size = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE -
			PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE - 256U,
	};
#endif
	switch (scenario) {
	case 8: return 0;
	case 9: frame.revision++; break;
	case 10: frame.size--; break;
	case 11: frame.state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST; break;
	case 12: frame.reserved = 1; break;
	case 13: frame.initiator_cpu = 1; break;
	case 14: frame.maximum_cpus = 0; break;
	case 15: frame.maximum_cpus = CONFIG_MAX_CPUS + 1; break;
	case 16: ((unsigned char *)&frame.receipts)[0] ^= 1; break;
	case 17: frame.service_endpoint.tag++; break;
	case 18: frame.service_endpoint.communication_base += 8; break;
	case 19: frame.service_endpoint.generation++; break;
	case 20: frame.service_endpoint.trigger_address++; break;
	case 21: frame.service_endpoint.trigger_value++; break;
	case 22: frame.service_endpoint.communication_size -= 8; break;
	case 23: frame.service_endpoint.flags ^= LB_AUTHVAR_ENDPOINT_PROTECTED_AUTHORITY; break;
	case 24: frame.service_endpoint.revision--; break;
	case 25: frame.service_endpoint.reserved = 1; break;
	case 26: frame.revision = 1; break;
	case 27: frame.size -= (uint32_t)sizeof(frame.service_endpoint); break;
	case 29:
		frame.boot_private.mac[0] = 1;
		break;
	case 30:
		frame.revision = 2;
		break;
	}
	return STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS;
}

void die(const char *format, ...)
{
	(void)format;
	require(scenario >= 2 && scenario <= 30 && !table_records);
	require(frame_queries == (scenario == 2 ? 0U : 1U));
	require(emissions == (scenario >= 7 ? 1U : 0U));
	require(triggers == (scenario >= 8 ? 1U : 0U));
	exit(77);
}

int main(int argc, char **argv)
{
	struct starbook_mtl_presence_bootstrap_frame original;
	struct lb_header header = { 0 };
	require(argc == 2);
	scenario = (unsigned int)strtoul(argv[1], NULL, 10);
	memset(&frame, 0x5a, sizeof(frame));
	original = frame;
	lb_board(&header);
	require(scenario < 2 || scenario == 28);
	if (scenario == 1) {
		require(!frame_queries && !emissions && !triggers);
		require(!memcmp(&frame, &original, sizeof(frame)));
		require(!table_records);
	} else {
		memset(&original, 0, sizeof(original));
		require(frame_queries == 1 && emissions == 1 && triggers == 1);
		require(!memcmp(&frame, &original, sizeof(frame)));
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
		require(table_records == 1 &&
			payload_mm_authvar_service_endpoint_validate(&table_endpoint) == CB_SUCCESS &&
			table_endpoint.generation == 7 && table_endpoint.communication_base == 0x700000U);
#else
		require(!table_records);
#endif
	}
	return 0;
}
