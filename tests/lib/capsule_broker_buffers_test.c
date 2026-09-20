/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_broker.h>
#include <boot/capsule_broker_buffers.h>
#include <bootmem.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint8_t shared[3 * CAPSULE_BROKER_BUFFER_ALIGNMENT]
	__aligned(CAPSULE_BROKER_BUFFER_ALIGNMENT);
static const char *test_case;

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!value)
		__builtin_trap();
}

size_t platform_capsule_broker_staging_size(void)
{
	if (!strcmp(test_case, "size"))
		return 1;
	return 2 * CAPSULE_BROKER_BUFFER_ALIGNMENT;
}

uintptr_t cbmem_top(void)
{
	return (uintptr_t)shared + (!strcmp(test_case, "cbmem") ? 4096 : 0);
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)shared + sizeof(shared);
	*size = 8 * 1024 * 1024;
}

int bootmem_region_targets_type(uint64_t start, uint64_t size,
	enum bootmem_type type)
{
	(void)start;
	(void)size;
	assert(type == BM_MEM_RESERVED);
	return strcmp(test_case, "proof") != 0;
}

static void test_reservation(void)
{
	struct capsule_broker_buffer_reservation reservation;

	memset(shared, 0xa5, sizeof(shared));
	if (strcmp(test_case, "happy")) {
		assert(!capsule_broker_buffers_reserve());
		assert(!capsule_broker_buffers_get(&reservation));
		return;
	}
	assert(capsule_broker_buffers_reserve());
	assert(!capsule_broker_buffers_reserve());
	assert(!capsule_broker_buffers_get(NULL));
	assert(capsule_broker_buffers_get(&reservation));
	assert(reservation.communication_base == (uintptr_t)shared);
	assert(reservation.communication_reserved_size ==
		CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE);
	assert(reservation.communication_size == CAPSULE_BROKER_TRANSPORT_SIZE);
	assert(reservation.staging_base == (uintptr_t)shared +
		CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE);
	assert(reservation.staging_size == 2 * CAPSULE_BROKER_BUFFER_ALIGNMENT);
	for (size_t i = 0; i < sizeof(shared); i++)
		assert(shared[i] == 0);
	memset(shared, 0xa5, sizeof(shared));
	capsule_broker_buffers_scrub();
	for (size_t i = 0; i < sizeof(shared); i++)
		assert(shared[i] == 0);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	test_case = argv[1];
	test_reservation();
	return 0;
}
