/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_transport.h>
#include <cpu/x86/smm.h>
#include <string.h>
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY)
#include "dma_smm_receipt_provision.h"
#endif

#if !ENV_RAMSTAGE && !ENV_TEST
#error "StarBook MTL lifecycle-close install sender is ramstage-only"
#endif

#if !CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY)
static struct payload_mm_authvar_presence_lifecycle_close_install_frame
	standalone_frame __aligned(8);
#endif

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

#if ENV_TEST
uint64_t starbook_mtl_authvar_presence_lifecycle_close_install_trigger_test(
	uint32_t request, uint32_t frame);
#endif

enum cb_err platform_payload_mm_authvar_presence_lifecycle_close_route_install(
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor,
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt *receipt)
{
	struct payload_mm_authvar_presence_lifecycle_close_install_frame *frame;
	uintptr_t frame_base;
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY)
	size_t frame_capacity;
#endif
	uint64_t wire;
	enum cb_err status = CB_ERR;

	if (!receipt)
		return CB_ERR;
	scrub(receipt, sizeof(*receipt));
	if (!descriptor)
		return CB_ERR;
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY)
	if (!starbook_mtl_dma_receipt_transport_frame(&frame_base,
		&frame_capacity) || frame_capacity < sizeof(*frame))
		return CB_ERR;
	frame = (void *)frame_base;
#else
	frame = &standalone_frame;
	frame_base = (uintptr_t)frame;
#endif
	if (frame_base > UINT32_MAX - (sizeof(*frame) - 1U))
		return CB_ERR;
	*frame = (struct payload_mm_authvar_presence_lifecycle_close_install_frame) {
		.revision =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REVISION,
		.size = sizeof(*frame),
		.state =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REQUEST,
		.request = *descriptor,
	};
#if ENV_TEST
	wire = starbook_mtl_authvar_presence_lifecycle_close_install_trigger_test(
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST,
		(uint32_t)frame_base);
#else
	{
		uint32_t eax =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST;
		uint32_t ecx = (uint32_t)frame_base;
		const uint16_t dx = pm_acpi_smi_cmd_port();

		__asm__ __volatile__("outb %%al, %%dx"
			: "+a" (eax), "+c" (ecx)
			: "d" (dx) : "memory");
		wire = (uint64_t)eax | ((uint64_t)ecx << 32);
	}
#endif
	if (wire ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_SUCCESS &&
	    frame->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REVISION &&
	    frame->size == sizeof(*frame) &&
	    frame->state ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_RECEIPT &&
	    !frame->reserved &&
	    !memcmp(&frame->request, descriptor, sizeof(*descriptor))) {
		*receipt = frame->receipt;
		status = CB_SUCCESS;
	}
	scrub(frame, sizeof(*frame));
	return status;
}
