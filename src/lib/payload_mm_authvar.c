/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

bool payload_mm_authvar_range_end(uint64_t base, uint64_t size, uint64_t *end)
{
	if (!size || base > UINT64_MAX - size)
		return false;
	*end = base + size;
	return true;
}

bool payload_mm_authvar_range_within(uint64_t base, uint64_t size,
	uint64_t outer_base, uint64_t outer_size)
{
	uint64_t end;
	uint64_t outer_end;

	return payload_mm_authvar_range_end(base, size, &end) &&
		payload_mm_authvar_range_end(outer_base, outer_size, &outer_end) &&
		base >= outer_base && end <= outer_end;
}

static bool ranges_overlap(const struct payload_mm_authvar_range *left,
	const struct payload_mm_authvar_range *right)
{
	uint64_t left_end;
	uint64_t right_end;

	if (!payload_mm_authvar_range_end(left->base, left->size, &left_end) ||
	    !payload_mm_authvar_range_end(right->base, right->size, &right_end))
		return true;
	return left->base < right_end && right->base < left_end;
}

static bool power_of_two(uint32_t value)
{
	return value && !(value & (value - 1U));
}

static bool range_addressable(const struct payload_mm_authvar_range *range,
	uint32_t address_bits)
{
	uint64_t end;

	if (!payload_mm_authvar_range_end(range->base, range->size, &end))
		return false;
	if (address_bits == 64U)
		return true;
	return address_bits == 32U && end <= (1ULL << 32);
}

bool payload_mm_authvar_contract_valid(
	const struct payload_mm_authvar_contract *contract)
{
	if (!contract || contract->revision != PAYLOAD_MM_AUTHVAR_REVISION ||
	    contract->size != sizeof(*contract) ||
	    contract->flags != PAYLOAD_MM_AUTHVAR_REQUIRED_FLAGS ||
	    !contract->generation || contract->reserved[0] || contract->reserved[1] ||
	    !range_addressable(&contract->smram, contract->smm_address_bits) ||
	    !range_addressable(&contract->communication, contract->smm_address_bits) ||
	    ranges_overlap(&contract->smram, &contract->communication) ||
	    contract->communication.base % sizeof(uint64_t) ||
	    contract->communication.size < PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES ||
	    contract->communication.size % sizeof(uint64_t) ||
	    !contract->boot_media_size || !contract->store_size ||
	    contract->store_size >= contract->boot_media_size ||
	    !payload_mm_authvar_range_within(contract->store_offset,
		contract->store_size,
		0, contract->boot_media_size) ||
	    !power_of_two(contract->block_size) ||
	    !power_of_two(contract->erase_size) ||
	    contract->erase_size % contract->block_size ||
	    contract->store_offset % contract->erase_size ||
	    contract->store_size % contract->erase_size ||
	    contract->store_size / contract->erase_size <
		PAYLOAD_MM_AUTHVAR_MIN_STORE_BLOCKS)
		return false;
	return true;
}

enum cb_err payload_mm_authvar_contract_build(
	struct payload_mm_authvar_contract *contract,
	const struct payload_mm_authvar_platform *platform)
{
	struct payload_mm_authvar_contract candidate;

	if (!contract)
		return CB_ERR;
	memset(contract, 0, sizeof(*contract));
	if (!platform || !platform->smm_entry_owned ||
	    !platform->spi_writes_restricted_to_smm ||
	    !platform->raw_flash_transport_absent ||
	    !platform->communication_region_reserved ||
	    !platform->store_region_owned_by_smm)
		return CB_ERR;
	candidate = (struct payload_mm_authvar_contract) {
		.revision = PAYLOAD_MM_AUTHVAR_REVISION,
		.size = sizeof(candidate),
		.flags = PAYLOAD_MM_AUTHVAR_REQUIRED_FLAGS,
		.smm_address_bits = platform->smm_address_bits,
		.generation = platform->generation,
		.smram = platform->smram,
		.communication = platform->communication,
		.boot_media_size = platform->boot_media_size,
		.store_offset = platform->store_offset,
		.store_size = platform->store_size,
		.block_size = platform->block_size,
		.erase_size = platform->erase_size,
	};
	if (!payload_mm_authvar_contract_valid(&candidate) ||
	    !platform->smm_entry_owned(platform->context) ||
	    !platform->spi_writes_restricted_to_smm(platform->context) ||
	    !platform->raw_flash_transport_absent(platform->context) ||
	    !platform->communication_region_reserved(platform->context,
		candidate.communication.base, candidate.communication.size) ||
	    !platform->store_region_owned_by_smm(platform->context,
		candidate.store_offset, candidate.store_size))
		return CB_ERR;
	*contract = candidate;
	return CB_SUCCESS;
}
