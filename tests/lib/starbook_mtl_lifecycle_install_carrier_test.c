/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <cpu/x86/smm.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_provision.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static uintptr_t carrier_address;
static bool concurrent_acquire;
static bool mutate_dma;
static bool mutate_tail;

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return !value;
}

bool starbook_mtl_dma_receipt_frame_in_program_test(uintptr_t address,
	size_t size)
{
	return address && size == sizeof(struct starbook_mtl_dma_receipt_frame);
}

uintptr_t starbook_mtl_dma_receipt_frame_address_test(uintptr_t address)
{
	carrier_address = address;
	return address;
}

enum cb_err starbook_mtl_dma_receipt_candidate_build(
	struct starbook_mtl_dma_smm_receipt *candidate)
{
	candidate->revision = STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION;
	candidate->size = sizeof(*candidate);
	return CB_SUCCESS;
}

uint64_t starbook_mtl_dma_receipt_trigger_test(uint32_t request,
	uint32_t frame_address)
{
	struct starbook_mtl_dma_receipt_frame *frame =
		(void *)(uintptr_t)frame_address;

	assert(request == STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST);
	assert((uintptr_t)frame == carrier_address);
	assert(frame->state == STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST);
	if (concurrent_acquire) {
		struct payload_mm_authvar_presence_lifecycle_close_install_frame *output;

		assert(starbook_mtl_dma_receipt_carrier_lifecycle_acquire(&output) ==
			CB_ERR);
		assert(frame->state == STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST);
	}
	frame->state = STARBOOK_MTL_DMA_RECEIPT_FRAME_ACCEPTED;
	if (mutate_dma)
		frame->candidate.revision++;
	return STARBOOK_MTL_DMA_RECEIPT_WIRE_SUCCESS;
}

uint64_t starbook_mtl_authvar_presence_lifecycle_close_install_trigger_test(
	uint32_t request, uint32_t frame_address)
{
	struct payload_mm_authvar_presence_lifecycle_close_install_frame *frame =
		(void *)(uintptr_t)frame_address;
	uint8_t *carrier = (void *)carrier_address;

	assert(request ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST);
	assert((uintptr_t)frame == carrier_address);
	assert(zero(carrier + sizeof(*frame),
		sizeof(struct starbook_mtl_dma_receipt_frame) - sizeof(*frame)));
	frame->receipt =
		(struct payload_mm_authvar_presence_lifecycle_close_install_receipt) {
			.descriptor = frame->request,
			.protected_route_identity = 1,
			.route_nonce = 2,
			.installed = 1,
		};
	frame->state =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_RECEIPT;
	memset(&frame->backing_receipt, 0, sizeof(frame->backing_receipt));
	if (mutate_tail)
		carrier[sizeof(*frame)] = 1;
	return PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_SUCCESS;
}

void __noreturn
platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop(void)
{
	abort();
}

static enum cb_err lifecycle_install(void)
{
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		descriptor = {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_REVISION,
			.size = sizeof(descriptor),
			.generation = 1,
		};
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt receipt;
	struct bootmem_reservation_receipt backing_receipt = { .revision = 1 };

	return platform_payload_mm_authvar_presence_lifecycle_close_route_install(
		&descriptor, &backing_receipt, &receipt);
}

int main(int argc, char **argv)
{
	uintptr_t base;
	size_t size;

	assert(argc == 2);
	if (!strcmp(argv[1], "before-dma")) {
		assert(lifecycle_install() == CB_ERR);
		assert(starbook_mtl_dma_receipt_provision_send() == CB_ERR);
		return 0;
	}
	if (!strcmp(argv[1], "concurrent"))
		concurrent_acquire = true;
	else if (!strcmp(argv[1], "dma-failure"))
		mutate_dma = true;
	else if (!strcmp(argv[1], "tail-mutation"))
		mutate_tail = true;
	else if (strcmp(argv[1], "valid") && strcmp(argv[1], "stale-tail") &&
		 strcmp(argv[1], "alias-output"))
		return 2;
	assert(starbook_mtl_dma_receipt_provision_send() ==
		((concurrent_acquire || mutate_dma) ? CB_ERR : CB_SUCCESS));
	assert(platform_smm_dma_receipt_frame(&base, &size));
	assert(base == carrier_address &&
		size == sizeof(struct starbook_mtl_dma_receipt_frame));
	assert(zero((void *)base, size));
	if (concurrent_acquire || mutate_dma) {
		assert(lifecycle_install() == CB_ERR);
		return 0;
	}
	if (!strcmp(argv[1], "alias-output")) {
		assert(starbook_mtl_dma_receipt_carrier_lifecycle_acquire(
			(void *)base) == CB_ERR);
		assert(zero((void *)base, size));
		return 0;
	}
	if (!strcmp(argv[1], "stale-tail"))
		((uint8_t *)base)[size - 1U] = 1;
	assert(lifecycle_install() == CB_SUCCESS);
	assert(zero((void *)base, size));
	assert(lifecycle_install() == CB_ERR);
	assert(starbook_mtl_dma_receipt_provision_send() == CB_ERR);
	return 0;
}
