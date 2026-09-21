/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <boot/capsule_broker_buffers.h>
#include <bootmem.h>
#include <cbmem.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <stdint.h>
#include <string.h>

#if ENV_RAMSTAGE || defined(__TEST__)
static struct {
	struct capsule_broker_buffer_reservation reservation;
	bool attempted;
	bool valid;
} buffer_owner;
#endif

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

bool capsule_broker_buffers_overlap(
	const struct capsule_broker_buffer_reservation *reservation,
	uint64_t base, uint64_t size)
{
	uint64_t end;
	uint64_t communication_end;
	uint64_t staging_end;

	if (!reservation || !size || base > UINT64_MAX - size ||
	    reservation->communication_base >
		UINT64_MAX - reservation->communication_reserved_size ||
	    reservation->staging_base > UINT64_MAX - reservation->staging_size)
		return true;
	end = base + size;
	communication_end = reservation->communication_base +
		reservation->communication_reserved_size;
	staging_end = reservation->staging_base + reservation->staging_size;
	return (base < communication_end &&
		reservation->communication_base < end) ||
		(base < staging_end && reservation->staging_base < end);
}

#if CONFIG(CAPSULE_BROKER_CBMEM_BUFFERS)
static bool cbmem_reservation(
	struct capsule_broker_buffer_reservation *reservation)
{
	const struct cbmem_entry *communication_entry;
	const struct cbmem_entry *staging_entry;
	const size_t communication_size =
		CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE;
	const size_t staging_size = platform_capsule_broker_staging_size();
	uintptr_t communication;
	uintptr_t staging;

	if (!reservation || communication_size < CAPSULE_BROKER_TRANSPORT_SIZE ||
	    !IS_ALIGNED(communication_size, CAPSULE_BROKER_BUFFER_ALIGNMENT) ||
	    staging_size < CAPSULE_BROKER_TRANSPORT_SIZE ||
	    !IS_ALIGNED(staging_size, CAPSULE_BROKER_BUFFER_ALIGNMENT))
		return false;
	communication_entry = cbmem_entry_find(CBMEM_ID_CAPSULE_COMMUNICATION);
	staging_entry = cbmem_entry_find(CBMEM_ID_CAPSULE_STAGING);
	if (!communication_entry || !staging_entry ||
	    cbmem_entry_size(communication_entry) != communication_size ||
	    cbmem_entry_size(staging_entry) != staging_size)
		return false;
	communication = (uintptr_t)cbmem_entry_start(communication_entry);
	staging = (uintptr_t)cbmem_entry_start(staging_entry);
	if (!IS_ALIGNED(communication, CAPSULE_BROKER_BUFFER_ALIGNMENT) ||
	    !IS_ALIGNED(staging, CAPSULE_BROKER_BUFFER_ALIGNMENT) ||
	    ranges_overlap(communication, communication_size, staging, staging_size))
		return false;
	*reservation = (struct capsule_broker_buffer_reservation) {
		.communication_base = communication,
		.communication_reserved_size = communication_size,
		.communication_size = CAPSULE_BROKER_TRANSPORT_SIZE,
		.staging_base = staging,
		.staging_size = staging_size,
	};
	return true;
}

static void allocate_capsule_broker_buffers(int is_recovery)
{
	struct capsule_broker_buffer_reservation reservation;
	const size_t staging_size = platform_capsule_broker_staging_size();

	(void)is_recovery;
	if (CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE <
		CAPSULE_BROKER_TRANSPORT_SIZE ||
	    !IS_ALIGNED(CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE,
		CAPSULE_BROKER_BUFFER_ALIGNMENT) ||
	    staging_size < CAPSULE_BROKER_TRANSPORT_SIZE ||
	    !IS_ALIGNED(staging_size, CAPSULE_BROKER_BUFFER_ALIGNMENT))
		die("Invalid capsule broker CBMEM reservation size\n");
	if (!cbmem_entry_add(CBMEM_ID_CAPSULE_COMMUNICATION,
		CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE) ||
	    !cbmem_entry_add(CBMEM_ID_CAPSULE_STAGING, staging_size) ||
	    !cbmem_reservation(&reservation))
		die("Invalid capsule broker CBMEM reservation\n");
	memset((void *)(uintptr_t)reservation.communication_base, 0,
		reservation.communication_reserved_size);
	memset((void *)(uintptr_t)reservation.staging_base, 0,
		reservation.staging_size);
}
CBMEM_READY_HOOK_EARLY(allocate_capsule_broker_buffers);

bool capsule_broker_buffers_find(
	struct capsule_broker_buffer_reservation *reservation)
{
	return cbmem_reservation(reservation);
}
#else
bool capsule_broker_buffers_find(
	struct capsule_broker_buffer_reservation *reservation)
{
	(void)reservation;
	return false;
}
#endif

#if ENV_RAMSTAGE || defined(__TEST__)
bool capsule_broker_buffers_reserve(void)
{
#if !CONFIG(CAPSULE_BROKER_CBMEM_BUFFERS)
	const size_t communication_reserved_size =
		CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE;
	const size_t staging_size = platform_capsule_broker_staging_size();
	uintptr_t communication;
	uintptr_t staging;
	uintptr_t smram_base;
	size_t smram_size;
#endif

	if (buffer_owner.attempted)
		return false;
	buffer_owner.attempted = true;
#if CONFIG(CAPSULE_BROKER_CBMEM_BUFFERS)
	if (!capsule_broker_buffers_find(&buffer_owner.reservation) ||
	    !bootmem_region_targets_type(
		buffer_owner.reservation.communication_base,
		buffer_owner.reservation.communication_reserved_size,
		BM_MEM_TABLE) ||
	    !bootmem_region_targets_type(buffer_owner.reservation.staging_base,
		buffer_owner.reservation.staging_size, BM_MEM_TABLE))
		return false;
	buffer_owner.valid = true;
	return true;
#else
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
#endif
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
#endif
