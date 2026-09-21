/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <boot/capsule_broker.h>
#include <boot/capsule_broker_buffers.h>
#include <cpu/x86/smm.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Capsule broker scratch must only be built in SMM"
#endif

static uint8_t broker_write_scratch[CAPSULE_BROKER_SCRATCH_SIZE]
	__aligned(CAPSULE_BROKER_BUFFER_ALIGNMENT);
static uint8_t broker_read_scratch[CAPSULE_BROKER_SCRATCH_SIZE]
	__aligned(CAPSULE_BROKER_BUFFER_ALIGNMENT);
static bool scratch_acquired;

bool capsule_broker_buffers_get(
	struct capsule_broker_buffer_reservation *reservation)
{
	if (!reservation)
		return false;
	smm_get_capsule_broker_buffers(reservation);
	return reservation->communication_base &&
		reservation->communication_reserved_size >=
			CAPSULE_BROKER_TRANSPORT_SIZE &&
		reservation->communication_size == CAPSULE_BROKER_TRANSPORT_SIZE &&
		reservation->staging_base && reservation->staging_size;
}

void capsule_broker_buffers_scrub(void)
{
	struct capsule_broker_buffer_reservation reservation;

	if (!capsule_broker_buffers_get(&reservation))
		return;
	memset((void *)(uintptr_t)reservation.communication_base, 0,
		reservation.communication_reserved_size);
	memset((void *)(uintptr_t)reservation.staging_base, 0,
		reservation.staging_size);
}

enum cb_err capsule_broker_scratch_acquire(uint32_t erase_size,
	struct capsule_broker_scratch_reservation *reservation)
{
	if (reservation)
		memset(reservation, 0, sizeof(*reservation));
	if (!reservation || scratch_acquired ||
	    erase_size != sizeof(broker_write_scratch) ||
	    erase_size != sizeof(broker_read_scratch))
		return CB_ERR;
	memset(broker_write_scratch, 0, sizeof(broker_write_scratch));
	memset(broker_read_scratch, 0, sizeof(broker_read_scratch));
	scratch_acquired = true;
	*reservation = (struct capsule_broker_scratch_reservation) {
		.write_base = (uintptr_t)broker_write_scratch,
		.read_base = (uintptr_t)broker_read_scratch,
		.erase_size = erase_size,
	};
	return CB_SUCCESS;
}

void capsule_broker_scratch_scrub(void)
{
	memset(broker_write_scratch, 0, sizeof(broker_write_scratch));
	memset(broker_read_scratch, 0, sizeof(broker_read_scratch));
}
