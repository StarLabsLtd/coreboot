/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_topology.h>
#include "authvar_presence_bootstrap_install.h"
#include "dma_smm_receipt_provision.h"

#include <arch/io.h>
#include <boot/coreboot_tables.h>
#include <boot/payload_mm_authvar_presence_publication.h>
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <cpu/x86/smm.h>
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
	struct payload_mm_authvar_presence_bootstrap_receipts receipts;
	uintptr_t frame_base;
	size_t frame_capacity;
	uint64_t wire;
	bool required;

	(void)header;
	if (payload_mm_authvar_presence_publication_loader_required(&required) != CB_SUCCESS)
		die("StarBook MTL presence: invalid early publication decision\n");
	if (!required)
		return;
	if (!starbook_mtl_dma_receipt_transport_frame(&frame_base, &frame_capacity) ||
	    frame_capacity < sizeof(*frame) || frame_base % _Alignof(*frame) ||
	    frame_base > UINT32_MAX - (sizeof(*frame) - 1U) ||
	    payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts, &transport) !=
		CB_SUCCESS)
		die("StarBook MTL presence: receipt emission failed\n");
	frame = (void *)frame_base;
	*frame = (struct starbook_mtl_presence_bootstrap_frame) {
		.revision = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION,
		.size = sizeof(*frame), .state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST,
		.receipts = receipts,
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
	if (wire != STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS ||
	    frame->revision != STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION ||
	    frame->size != sizeof(*frame) ||
	    frame->state != STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED ||
	    frame->reserved || frame->initiator_cpu || !frame->maximum_cpus ||
	    frame->maximum_cpus > CONFIG_MAX_CPUS ||
	    memcmp(&frame->receipts, &receipts, sizeof(receipts)))
		die("StarBook MTL presence: protected route installation failed\n");
	memset(frame, 0, sizeof(*frame));
	memset(&receipts, 0, sizeof(receipts));
}
