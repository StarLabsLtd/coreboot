/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_topology.h>
#include "authvar_presence_bootstrap_install.h"
#include "dma_smm_receipt_provision.h"

#include <arch/io.h>
#include <boot/coreboot_tables.h>
#include <boot/payload_mm_authvar_presence_publication.h>
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <boot/payload_mm_authvar_service.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_command.h>
#include <console/console.h>
#include <string.h>

#if !ENV_RAMSTAGE && !ENV_TEST
#error "StarBook MTL presence bootstrap sender is ramstage-only"
#endif

/* Future composition reuses this exact transport, not a rediscovered page. */
static struct payload_mm_authvar_presence_tuple_sender transport;

#if ENV_TEST
uint64_t starbook_mtl_presence_bootstrap_trigger_test(uint32_t request, uint32_t frame);
#endif

void lb_board(struct lb_header *header)
{
	struct starbook_mtl_presence_bootstrap_frame *frame;
	struct starbook_mtl_presence_bootstrap_frame response;
	struct payload_mm_authvar_presence_bootstrap_receipts receipts;
	struct bootmem_reservation_receipt boot_private;
	uintptr_t frame_base;
	size_t frame_capacity;
	uint64_t wire;
	bool required;
#if !CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	const struct lb_authvar_service_endpoint no_endpoint = { 0 };
#endif

	if (payload_mm_authvar_presence_publication_loader_required(&required) != CB_SUCCESS)
		die("StarBook MTL presence: invalid early publication decision\n");
	if (!required)
		return;
	if (!starbook_mtl_dma_receipt_transport_frame(&frame_base, &frame_capacity) ||
	    frame_capacity < sizeof(*frame) || frame_base % _Alignof(*frame) ||
	    frame_base > UINT32_MAX - (sizeof(*frame) - 1U) ||
	    payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts, &transport,
		&boot_private) !=
		CB_SUCCESS)
		die("StarBook MTL presence: receipt emission failed\n");
	frame = (void *)frame_base;
	*frame = (struct starbook_mtl_presence_bootstrap_frame) {
		.revision = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION,
		.size = sizeof(*frame), .state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST,
		.receipts = receipts,
		.boot_private = boot_private,
	};
#if ENV_TEST
	wire = starbook_mtl_presence_bootstrap_trigger_test(
		STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST, (uint32_t)frame_base);
#else
	{
		uint32_t eax = STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST;
		uint32_t ecx = (uint32_t)frame_base;
		const uint16_t dx = pm_acpi_smi_cmd_port();

		__asm__ __volatile__("outb %%al, %%dx"
			: "+a" (eax), "+c" (ecx) : "d" (dx) : "memory");
		wire = (uint64_t)eax | ((uint64_t)ecx << 32);
	}
#endif
	/* Snapshot the public reply once; validation and publication use this copy. */
	response = *frame;
	if (wire != STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS ||
	    response.revision != STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION ||
	    response.size != sizeof(response) ||
	    response.state != STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED ||
	    response.reserved || response.initiator_cpu || !response.maximum_cpus ||
	    response.maximum_cpus > CONFIG_MAX_CPUS ||
	    memcmp(&response.receipts, &receipts, sizeof(receipts)) ||
	    memcmp(&response.boot_private, &boot_private, sizeof(boot_private)))
		die("StarBook MTL presence: protected route installation failed\n");
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	if (!header || payload_mm_authvar_service_endpoint_validate(&response.service_endpoint) !=
		CB_SUCCESS ||
	    response.service_endpoint.communication_base != receipts.service.base ||
	    response.service_endpoint.communication_size != receipts.service.bytes ||
	    response.service_endpoint.communication_size !=
		PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE ||
	    response.service_endpoint.generation != receipts.service.generation ||
	    response.service_endpoint.trigger_address != pm_acpi_smi_cmd_port() ||
	    response.service_endpoint.trigger_value != SMM_APMC_AUTHVAR_SERVICE)
		die("StarBook MTL presence: invalid admitted service endpoint\n");
	memcpy(lb_new_record(header), &response.service_endpoint, sizeof(response.service_endpoint));
#else
	(void)header;
	if (memcmp(&response.service_endpoint, &no_endpoint, sizeof(no_endpoint)))
		die("StarBook MTL presence: unexpected service endpoint\n");
#endif
	memset(frame, 0, sizeof(*frame));
	memset(&response, 0, sizeof(response));
	memset(&receipts, 0, sizeof(receipts));
	memset(&boot_private, 0, sizeof(boot_private));
}
