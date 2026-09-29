/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_transport.h>
#include <cpu/x86/smm.h>
#include <string.h>

#if !ENV_RAMSTAGE && !ENV_TEST
#error "StarBook MTL lifecycle-close install sender is ramstage-only"
#endif

static struct payload_mm_authvar_presence_lifecycle_close_install_frame frame
	__aligned(8);

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return !value;
}

#if ENV_TEST
uint64_t starbook_mtl_authvar_presence_lifecycle_close_install_trigger_test(
	uint32_t request, uint32_t frame);
#endif

enum cb_err platform_payload_mm_authvar_presence_lifecycle_close_route_install(
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor,
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	struct bootmem_reservation_receipt *backing_receipt,
#endif
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt *receipt)
{
	uint64_t wire;
	struct payload_mm_authvar_presence_lifecycle_close_install_frame frozen_frame;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	struct bootmem_reservation_receipt backing_frozen;
#endif
	enum cb_err status = CB_ERR;

	if (!receipt
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	    || !backing_receipt
#endif
	    )
		return CB_ERR;
	scrub(receipt, sizeof(*receipt));
	if (!descriptor || (uintptr_t)&frame >
		UINT32_MAX - (sizeof(frame) - 1U))
		return CB_ERR;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	backing_frozen = *backing_receipt;
#endif
	frame = (struct payload_mm_authvar_presence_lifecycle_close_install_frame) {
		.revision =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REVISION,
		.size = sizeof(frame),
		.state =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REQUEST,
		.request = *descriptor,
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
		.backing_receipt = *backing_receipt,
#endif
	};
	frozen_frame = frame;
#if ENV_TEST
	wire = starbook_mtl_authvar_presence_lifecycle_close_install_trigger_test(
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST,
		(uint32_t)(uintptr_t)&frame);
#else
	{
		uint32_t eax =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST;
		uint32_t ecx = (uint32_t)(uintptr_t)&frame;
		const uint16_t dx = pm_acpi_smi_cmd_port();

		__asm__ __volatile__("outb %%al, %%dx"
			: "+a" (eax), "+c" (ecx)
			: "d" (dx) : "memory");
		wire = (uint64_t)eax | ((uint64_t)ecx << 32);
	}
#endif
	if (
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	    memcmp(backing_receipt, &backing_frozen, sizeof(backing_frozen)) ||
#endif
	    false)
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
	if (wire ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_SUCCESS &&
	    frame.revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REVISION &&
	    frame.size == sizeof(frame) &&
	    frame.state ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_RECEIPT &&
	    !frame.reserved &&
	    !memcmp(&frame.request, descriptor, sizeof(*descriptor))
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	    && zero(&frame.backing_receipt, sizeof(frame.backing_receipt))
#endif
	    ) {
		*receipt = frame.receipt;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
		memset(backing_receipt, 0, sizeof(*backing_receipt));
#endif
		status = CB_SUCCESS;
	} else if (wire ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_SUCCESS)
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
	else if (memcmp(&frame, &frozen_frame, sizeof(frame)) &&
		 !(frame.revision == frozen_frame.revision &&
		   frame.size == frozen_frame.size &&
		   frame.state ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REJECTED &&
		   !frame.reserved &&
		   !memcmp(&frame.request, descriptor, sizeof(*descriptor)) &&
		   zero(&frame.receipt, sizeof(frame.receipt))
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
		   && (!memcmp(&frame.backing_receipt,
			&frozen_frame.backing_receipt,
			sizeof(frame.backing_receipt)) ||
		       zero(&frame.backing_receipt,
				sizeof(frame.backing_receipt)))
#endif
		   ))
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	scrub(&backing_frozen, sizeof(backing_frozen));
#endif
	scrub(&frame, sizeof(frame));
	scrub(&frozen_frame, sizeof(frozen_frame));
	return status;
}
