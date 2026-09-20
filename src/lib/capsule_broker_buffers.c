/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <boot/capsule_broker_buffers.h>
#include <bootmem.h>
#include <cbmem.h>
#include <commonlib/helpers.h>
#include <cpu/x86/smm.h>
#include <stdint.h>
#include <string.h>

static struct {
	struct capsule_broker_buffer_reservation reservation;
	bool attempted;
	bool valid;
} buffer_owner;

static bool range_end(uintptr_t base, size_t size, uintptr_t *end)
{
	if (!size || base > UINTPTR_MAX - size)
		return false;
	*end = base + size;
	return true;
}

static bool ranges_overlap(uintptr_t left, size_t left_size,
	uintptr_t right, size_t right_size)
{
	uintptr_t left_end;
	uintptr_t right_end;

	if (!range_end(left, left_size, &left_end) ||
	    !range_end(right, right_size, &right_end))
		return true;
	return left < right_end && right < left_end;
}

bool capsule_broker_buffers_reserve(void)
{
	const size_t communication_reserved_size =
		CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE;
	const size_t staging_size = platform_capsule_broker_staging_size();
	uintptr_t communication;
	uintptr_t staging;
	uintptr_t smram_base;
	size_t smram_size;

	if (buffer_owner.attempted)
		return false;
	buffer_owner.attempted = true;
	if (communication_reserved_size < CAPSULE_BROKER_TRANSPORT_SIZE ||
	    !IS_ALIGNED(communication_reserved_size,
		CAPSULE_BROKER_BUFFER_ALIGNMENT) ||
	    staging_size < CAPSULE_BROKER_TRANSPORT_SIZE ||
	    !IS_ALIGNED(staging_size, CAPSULE_BROKER_BUFFER_ALIGNMENT))
		return false;

	smm_region(&smram_base, &smram_size);
	if (!smram_size || staging_size > smram_base ||
	    communication_reserved_size > smram_base - staging_size)
		return false;
	communication = smram_base - staging_size - communication_reserved_size;
	staging = communication + communication_reserved_size;
	if (communication != cbmem_top() ||
	    !IS_ALIGNED(communication,
		CAPSULE_BROKER_BUFFER_ALIGNMENT) ||
	    !IS_ALIGNED(staging, CAPSULE_BROKER_BUFFER_ALIGNMENT) ||
	    !bootmem_region_targets_type(communication,
		communication_reserved_size, BM_MEM_RESERVED) ||
	    !bootmem_region_targets_type(staging, staging_size,
		BM_MEM_RESERVED) ||
	    ranges_overlap(communication, communication_reserved_size,
		staging, staging_size))
		return false;

	memset((void *)communication, 0, communication_reserved_size);
	memset((void *)staging, 0, staging_size);
	buffer_owner.reservation = (struct capsule_broker_buffer_reservation) {
		.communication_base = communication,
		.communication_reserved_size = communication_reserved_size,
		.communication_size = CAPSULE_BROKER_TRANSPORT_SIZE,
		.staging_base = staging,
		.staging_size = staging_size,
	};
	buffer_owner.valid = true;
	return true;
}

bool capsule_broker_buffers_get(
	struct capsule_broker_buffer_reservation *reservation)
{
	if (!reservation || !buffer_owner.valid)
		return false;
	*reservation = buffer_owner.reservation;
	return true;
}

void capsule_broker_buffers_scrub(void)
{
	if (!buffer_owner.valid)
		return;
	memset((void *)(uintptr_t)buffer_owner.reservation.communication_base, 0,
		buffer_owner.reservation.communication_reserved_size);
	memset((void *)(uintptr_t)buffer_owner.reservation.staging_base, 0,
		buffer_owner.reservation.staging_size);
}
