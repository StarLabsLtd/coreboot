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
#if CONFIG(STARLABS_STARBOOK_MTL_LIFECYCLE_INSTALL_CARRIER)
	uint32_t state;
	uint32_t reserved;
#endif
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

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

#if CONFIG(STARLABS_STARBOOK_MTL_LIFECYCLE_INSTALL_CARRIER)
static bool overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t a = (uintptr_t)first;
	const uintptr_t b = (uintptr_t)second;

	return a <= b ? b - a < first_size : a - b < second_size;
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return !value;
}

enum carrier_state {
	CARRIER_EMPTY,
	CARRIER_DMA_BUSY,
	CARRIER_DMA_DONE,
	CARRIER_LIFECYCLE_BUSY,
	CARRIER_LIFECYCLE_RETIRING,
	CARRIER_RETIRED,
	CARRIER_FAILED,
};

static void carrier_fail(void)
{
	scrub(&frame, sizeof(frame));
	__atomic_store_n(&sender.state, CARRIER_FAILED, __ATOMIC_RELEASE);
}

enum cb_err starbook_mtl_dma_receipt_carrier_lifecycle_acquire(
	struct payload_mm_authvar_presence_lifecycle_close_install_frame **output)
{
	uintptr_t base;
	size_t bytes;
	uint32_t expected = CARRIER_DMA_DONE;

	if (!output || (uintptr_t)output % _Alignof(*output) ||
	    (uintptr_t)output > UINTPTR_MAX - (sizeof(*output) - 1U) ||
	    overlap(output, sizeof(*output), &sender, sizeof(sender))) {
		if (__atomic_compare_exchange_n(&sender.state, &expected,
			CARRIER_FAILED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			scrub(&frame, sizeof(frame));
		else
			__atomic_store_n(&sender.state, CARRIER_FAILED,
				__ATOMIC_RELEASE);
		return CB_ERR;
	}
	if (!__atomic_compare_exchange_n(&sender.state, &expected,
		CARRIER_LIFECYCLE_BUSY, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE)) {
		__atomic_store_n(&sender.state, CARRIER_FAILED, __ATOMIC_RELEASE);
		return CB_ERR;
	}
	if (!platform_smm_dma_receipt_frame(&base, &bytes) ||
	    base != (uintptr_t)&frame || bytes != sizeof(frame) ||
	    base % _Alignof(struct starbook_mtl_dma_receipt_frame) ||
	    base > UINT32_MAX - (bytes - 1U)) {
		carrier_fail();
		return CB_ERR;
	}
	scrub(&frame, sizeof(frame));
	if (!zero(&frame, sizeof(frame)) ||
	    !zero(&sender.snapshot, sizeof(sender.snapshot)) ||
	    __atomic_load_n(&sender.state, __ATOMIC_ACQUIRE) !=
		CARRIER_LIFECYCLE_BUSY) {
		__atomic_store_n(&sender.state, CARRIER_FAILED, __ATOMIC_RELEASE);
		return CB_ERR;
	}
	*output = (void *)&frame;
	return CB_SUCCESS;
}

static enum cb_err lifecycle_retire(uint32_t state, bool require_zero_tail)
{
	uint32_t expected = CARRIER_LIFECYCLE_BUSY;
	const size_t prefix = sizeof(struct
		payload_mm_authvar_presence_lifecycle_close_install_frame);

	if (!__atomic_compare_exchange_n(&sender.state, &expected,
		CARRIER_LIFECYCLE_RETIRING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE)) {
		__atomic_store_n(&sender.state, CARRIER_FAILED, __ATOMIC_RELEASE);
		return CB_ERR;
	}
	if (require_zero_tail && !zero((uint8_t *)&frame + prefix,
		sizeof(frame) - prefix))
		state = CARRIER_FAILED;
	scrub(&frame, sizeof(frame));
	expected = CARRIER_LIFECYCLE_RETIRING;
	if (!zero(&frame, sizeof(frame)) ||
	    !__atomic_compare_exchange_n(&sender.state, &expected, state, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&sender.state, CARRIER_FAILED, __ATOMIC_RELEASE);
		return CB_ERR;
	}
	return state == CARRIER_RETIRED ? CB_SUCCESS : CB_ERR;
}

enum cb_err starbook_mtl_dma_receipt_carrier_lifecycle_complete(void)
{
	return lifecycle_retire(CARRIER_RETIRED, true);
}

void starbook_mtl_dma_receipt_carrier_lifecycle_abort(void)
{
	(void)lifecycle_retire(CARRIER_FAILED, false);
}

_Static_assert((offsetof(typeof(sender), snapshot) - sizeof(frame)) %
	_Alignof(struct starbook_mtl_dma_receipt_frame) == 0,
	"MTL DMA receipt carrier alignment changed");
_Static_assert(sizeof(struct
	payload_mm_authvar_presence_lifecycle_close_install_frame) <= sizeof(frame),
	"lifecycle-close install frame exceeds DMA receipt carrier");
#endif

#if ENV_TEST
uint64_t starbook_mtl_dma_receipt_trigger_test(uint32_t request,
	uint32_t frame_address);
#endif

enum cb_err starbook_mtl_dma_receipt_provision_send(void)
{
	uint64_t wire;
	const uintptr_t frame_address = RECEIPT_FRAME_ADDRESS;
	enum cb_err status = CB_ERR;
#if CONFIG(STARLABS_STARBOOK_MTL_LIFECYCLE_INSTALL_CARRIER)
	uint32_t expected = CARRIER_EMPTY;

	if (!__atomic_compare_exchange_n(&sender.state, &expected,
		CARRIER_DMA_BUSY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&sender.state, CARRIER_FAILED, __ATOMIC_RELEASE);
		return CB_ERR;
	}
#endif

	if (frame_address > UINT32_MAX - (sizeof(frame) - 1U)) {
#if CONFIG(STARLABS_STARBOOK_MTL_LIFECYCLE_INSTALL_CARRIER)
		carrier_fail();
#endif
		return CB_ERR;
	}
#if CONFIG(STARLABS_STARBOOK_MTL_LIFECYCLE_INSTALL_CARRIER)
	scrub(&frame, sizeof(frame));
#endif
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
#if CONFIG(STARLABS_STARBOOK_MTL_LIFECYCLE_INSTALL_CARRIER)
	expected = CARRIER_DMA_BUSY;
	if (!__atomic_compare_exchange_n(&sender.state, &expected,
		status == CB_SUCCESS ? CARRIER_DMA_DONE : CARRIER_FAILED, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&sender.state, CARRIER_FAILED, __ATOMIC_RELEASE);
		status = CB_ERR;
	}
#endif
	return status;
}
