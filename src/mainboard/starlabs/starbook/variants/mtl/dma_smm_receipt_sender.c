/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_receipt_provision.h"

#include <arch/io.h>
#include <cpu/x86/smm.h>
#include <string.h>
#include <symbols.h>

#if !ENV_RAMSTAGE && !ENV_TEST
#error "MTL DMA receipt sender is ramstage-only"
#endif

static struct {
	uint32_t ready;
	uint32_t reserved;
	struct starbook_mtl_dma_receipt_frame frame;
	struct starbook_mtl_dma_smm_receipt snapshot;
} sender __aligned(8);

#define frame sender.frame

#if ENV_TEST
uintptr_t starbook_mtl_dma_receipt_frame_address_test(uintptr_t address);
bool starbook_mtl_dma_receipt_frame_in_program_test(uintptr_t address,
	size_t size);
#define RECEIPT_FRAME_ADDRESS \
	starbook_mtl_dma_receipt_frame_address_test((uintptr_t)&frame)
#define RECEIPT_FRAME_IN_PROGRAM(address, size) \
	starbook_mtl_dma_receipt_frame_in_program_test(address, size)
#else
#define RECEIPT_FRAME_ADDRESS ((uintptr_t)&frame)
#define RECEIPT_FRAME_IN_PROGRAM(address, size) \
	((uintptr_t)_program <= (uintptr_t)_eprogram && \
	 (address) >= (uintptr_t)_program && (address) <= (uintptr_t)_eprogram && \
	 (size) <= (uintptr_t)_eprogram - (address))
#endif

bool platform_smm_dma_receipt_frame(uintptr_t *base, size_t *size)
{
	const uintptr_t frame_address = RECEIPT_FRAME_ADDRESS;

	if (!base || !size || frame_address != (uintptr_t)&frame ||
	    frame_address > UINT32_MAX - (sizeof(frame) - 1U) ||
	    !RECEIPT_FRAME_IN_PROGRAM(frame_address, sizeof(frame)))
		return false;
	*base = frame_address;
	*size = sizeof(frame);
	return true;
}

bool starbook_mtl_dma_receipt_transport_frame(uintptr_t *base, size_t *size)
{
	uintptr_t frame_base;
	size_t frame_size;

	if (!base || !size ||
	    __atomic_load_n(&sender.ready, __ATOMIC_ACQUIRE) != 1U ||
	    !platform_smm_dma_receipt_frame(&frame_base, &frame_size) ||
	    frame_base != (uintptr_t)&frame || frame_size != sizeof(frame))
		return false;
	*base = frame_base;
	*size = frame_size;
	return __atomic_load_n(&sender.ready, __ATOMIC_ACQUIRE) == 1U;
}

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

#if ENV_TEST
uint64_t starbook_mtl_dma_receipt_trigger_test(uint32_t request,
	uint32_t frame_address);
#endif

enum cb_err starbook_mtl_dma_receipt_provision_send(void)
{
	uint64_t wire;
	const uintptr_t frame_address = RECEIPT_FRAME_ADDRESS;
	enum cb_err status = CB_ERR;

	if (frame_address > UINT32_MAX - (sizeof(frame) - 1U))
		return CB_ERR;
	frame = (struct starbook_mtl_dma_receipt_frame) {
		.revision = STARBOOK_MTL_DMA_RECEIPT_FRAME_REVISION,
		.size = sizeof(frame),
		.state = STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST,
	};
	if (starbook_mtl_dma_receipt_candidate_build(&frame.candidate) !=
		CB_SUCCESS)
		goto out;
	sender.snapshot = frame.candidate;
#if ENV_TEST
	wire = starbook_mtl_dma_receipt_trigger_test(
		STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST,
		(uint32_t)frame_address);
#else
	{
		uint32_t eax = STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST;
		uint32_t ecx = (uint32_t)frame_address;
		const uint16_t dx = pm_acpi_smi_cmd_port();

		__asm__ __volatile__("outb %%al, %%dx"
			: "+a" (eax), "+c" (ecx)
			: "d" (dx) : "memory");
		wire = (uint64_t)eax | ((uint64_t)ecx << 32);
	}
#endif
	if (wire == STARBOOK_MTL_DMA_RECEIPT_WIRE_SUCCESS &&
	    frame.revision == STARBOOK_MTL_DMA_RECEIPT_FRAME_REVISION &&
	    frame.size == sizeof(frame) &&
	    frame.state == STARBOOK_MTL_DMA_RECEIPT_FRAME_ACCEPTED &&
	    !frame.reserved &&
	    !memcmp(&frame.candidate, &sender.snapshot,
		sizeof(sender.snapshot)))
		status = CB_SUCCESS;
out:
	scrub(&sender.snapshot, sizeof(sender.snapshot));
	scrub(&frame, sizeof(frame));
	if (status == CB_SUCCESS)
		__atomic_store_n(&sender.ready, 1U, __ATOMIC_RELEASE);
	return status;
}
