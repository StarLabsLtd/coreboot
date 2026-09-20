/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_broker.h>
#include <boot/capsule_broker_buffers.h>
#include <stdint.h>
#include <string.h>

static uint8_t communication[CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE]
	__aligned(CAPSULE_BROKER_BUFFER_ALIGNMENT);
static uint8_t staging[2 * CAPSULE_BROKER_BUFFER_ALIGNMENT]
	__aligned(CAPSULE_BROKER_BUFFER_ALIGNMENT);

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!value)
		__builtin_trap();
}

void smm_get_capsule_broker_buffers(
	struct capsule_broker_buffer_reservation *reservation)
{
	*reservation = (struct capsule_broker_buffer_reservation) {
		.communication_base = (uintptr_t)communication,
		.communication_reserved_size = sizeof(communication),
		.communication_size = CAPSULE_BROKER_TRANSPORT_SIZE,
		.staging_base = (uintptr_t)staging,
		.staging_size = sizeof(staging),
	};
}

int main(void)
{
	struct capsule_broker_buffer_reservation reservation;
	void *scratch = (void *)(uintptr_t)1;
	size_t size = 1;

	assert(!capsule_broker_buffers_get(NULL));
	assert(capsule_broker_buffers_get(&reservation));
	assert(reservation.communication_base == (uintptr_t)communication);
	memset(communication, 0xa5, sizeof(communication));
	memset(staging, 0x5a, sizeof(staging));
	capsule_broker_buffers_scrub();
	for (size_t i = 0; i < sizeof(communication); i++)
		assert(communication[i] == 0);
	for (size_t i = 0; i < sizeof(staging); i++)
		assert(staging[i] == 0);

	assert(capsule_broker_scratch_acquire(&scratch, &size) == CB_SUCCESS);
	assert(scratch);
	assert(size == CAPSULE_BROKER_SCRATCH_SIZE);
	assert(((uintptr_t)scratch % CAPSULE_BROKER_BUFFER_ALIGNMENT) == 0);
	for (size_t i = 0; i < size; i++)
		assert(((uint8_t *)scratch)[i] == 0);
	memset(scratch, 0xa5, size);
	capsule_broker_scratch_scrub();
	for (size_t i = 0; i < size; i++)
		assert(((uint8_t *)scratch)[i] == 0);
	assert(capsule_broker_scratch_acquire(&scratch, &size) == CB_ERR);
	assert(!scratch);
	assert(size == 0);
	return 0;
}
