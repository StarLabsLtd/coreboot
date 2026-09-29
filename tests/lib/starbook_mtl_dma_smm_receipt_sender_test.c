/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <cpu/x86/smm.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_provision.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static bool high_address;
static bool mutate_candidate;
static unsigned int builds;
static unsigned int triggers;

bool starbook_mtl_dma_receipt_frame_in_program_test(uintptr_t address,
	size_t size)
{
	return address && size == sizeof(struct starbook_mtl_dma_receipt_frame);
}

uintptr_t starbook_mtl_dma_receipt_frame_address_test(uintptr_t address)
{
	return high_address ? (uintptr_t)UINT32_MAX + 1U : address;
}

enum cb_err starbook_mtl_dma_receipt_candidate_build(
	struct starbook_mtl_dma_smm_receipt *candidate)
{
	builds++;
	candidate->revision = STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION;
	candidate->size = sizeof(*candidate);
	return CB_SUCCESS;
}

uint64_t starbook_mtl_dma_receipt_trigger_test(uint32_t request,
	uint32_t frame_address)
{
	struct starbook_mtl_dma_receipt_frame *transport =
		(void *)(uintptr_t)frame_address;

	triggers++;
	assert(request == STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST);
	assert(transport->state == STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST);
	transport->state = STARBOOK_MTL_DMA_RECEIPT_FRAME_ACCEPTED;
	if (mutate_candidate)
		transport->candidate.revision++;
	return STARBOOK_MTL_DMA_RECEIPT_WIRE_SUCCESS;
}

int main(int argc, char **argv)
{
	uintptr_t base;
	size_t size;

	assert(argc == 2);
	if (!strcmp(argv[1], "high-address"))
		high_address = true;
	else if (!strcmp(argv[1], "mutated-candidate"))
		mutate_candidate = true;
	else if (strcmp(argv[1], "valid"))
		return 2;

	if (!strcmp(argv[1], "valid")) {
		assert(platform_smm_dma_receipt_frame(&base, &size));
		assert(base && size == sizeof(struct starbook_mtl_dma_receipt_frame));
		assert(starbook_mtl_dma_receipt_provision_send() == CB_SUCCESS);
		assert(builds == 1 && triggers == 1);
	} else {
		if (high_address)
			assert(!platform_smm_dma_receipt_frame(&base, &size));
		assert(starbook_mtl_dma_receipt_provision_send() != CB_SUCCESS);
		if (high_address)
			assert(!builds && !triggers);
	}
	return 0;
}
