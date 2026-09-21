/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_broker.h>
#include <boot/capsule_broker_buffers.h>
#include <bootmem.h>
#include <cbmem.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct cbmem_entry {
	uintptr_t start;
	u64 size;
};

static struct cbmem_entry communication;
static struct cbmem_entry staging;
static const char *test_case;
static unsigned int add_count;
static uint8_t communication_memory[CAPSULE_BROKER_BUFFER_ALIGNMENT]
	__aligned(CAPSULE_BROKER_BUFFER_ALIGNMENT);
static uint8_t staging_memory[2 * CAPSULE_BROKER_BUFFER_ALIGNMENT]
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

size_t platform_capsule_broker_staging_size(void)
{
	return 2 * CAPSULE_BROKER_BUFFER_ALIGNMENT;
}

const struct cbmem_entry *cbmem_entry_find(u32 id)
{
	if (id == CBMEM_ID_CAPSULE_COMMUNICATION)
		return &communication;
	if (id == CBMEM_ID_CAPSULE_STAGING)
		return &staging;
	return NULL;
}

const struct cbmem_entry *cbmem_entry_add(u32 id, u64 size)
{
	const struct cbmem_entry *entry = cbmem_entry_find(id);

	(void)size;
	add_count++;
	return entry;
}

u64 cbmem_entry_size(const struct cbmem_entry *entry)
{
	return entry->size;
}

void *cbmem_entry_start(const struct cbmem_entry *entry)
{
	return (void *)entry->start;
}

int bootmem_region_targets_type(uint64_t start, uint64_t size,
	enum bootmem_type type)
{
	(void)start;
	(void)size;
	assert(type == BM_MEM_TABLE);
	return strcmp(test_case, "publication");
}

void die(const char *fmt, ...)
{
	(void)fmt;
	abort();
}

#include "../../src/lib/capsule_broker_buffers.c"

static void set_valid_noncontiguous_entries(void)
{
	communication = (struct cbmem_entry) {
		.start = 0x100000,
		.size = CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE,
	};
	staging = (struct cbmem_entry) {
		.start = 0x300000,
		.size = 2 * CAPSULE_BROKER_BUFFER_ALIGNMENT,
	};
}

int main(int argc, char **argv)
{
	struct capsule_broker_buffer_reservation reservation;

	assert(argc == 2);
	test_case = argv[1];
	set_valid_noncontiguous_entries();
	if (!strcmp(test_case, "recovered-size"))
		communication.size--;
	else if (!strcmp(test_case, "overlap"))
		staging.start = communication.start +
			CAPSULE_BROKER_BUFFER_ALIGNMENT / 2;
	else if (!strcmp(test_case, "overflow"))
		communication.start = UINTPTR_MAX -
			(CAPSULE_BROKER_BUFFER_ALIGNMENT - 1);
	else if (!strcmp(test_case, "misaligned"))
		staging.start++;
	else if (!strcmp(test_case, "hook-reentry")) {
		communication.start = (uintptr_t)communication_memory;
		staging.start = (uintptr_t)staging_memory;
		memset(communication_memory, 0xa5, sizeof(communication_memory));
		memset(staging_memory, 0xa5, sizeof(staging_memory));
		allocate_capsule_broker_buffers(0);
		assert(add_count == 2);
		for (size_t i = 0; i < sizeof(communication_memory); i++)
			assert(communication_memory[i] == 0);
		for (size_t i = 0; i < sizeof(staging_memory); i++)
			assert(staging_memory[i] == 0);
		memset(communication_memory, 0xa5, sizeof(communication_memory));
		memset(staging_memory, 0xa5, sizeof(staging_memory));
		allocate_capsule_broker_buffers(1);
		assert(add_count == 4);
		for (size_t i = 0; i < sizeof(communication_memory); i++)
			assert(communication_memory[i] == 0);
		for (size_t i = 0; i < sizeof(staging_memory); i++)
			assert(staging_memory[i] == 0);
		return 0;
	}

	if (!strcmp(test_case, "noncontiguous")) {
		assert(capsule_broker_buffers_find(&reservation));
		/* A noncontiguous TOLUM-to-TSEG gap is not part of the contract. */
		assert(!capsule_broker_buffers_overlap(&reservation,
			0x500000, CAPSULE_BROKER_BUFFER_ALIGNMENT));
		assert(capsule_broker_buffers_overlap(&reservation,
			staging.start, CAPSULE_BROKER_BUFFER_ALIGNMENT));
		assert(capsule_broker_buffers_overlap(&reservation,
			UINT64_MAX - 1, CAPSULE_BROKER_BUFFER_ALIGNMENT));
		assert(reservation.communication_base == communication.start);
		assert(reservation.staging_base == staging.start);
		assert(capsule_broker_buffers_reserve());
		assert(!capsule_broker_buffers_reserve());
		assert(capsule_broker_buffers_get(&reservation));
	} else if (!strcmp(test_case, "publication")) {
		assert(capsule_broker_buffers_find(&reservation));
		assert(!capsule_broker_buffers_reserve());
		assert(!capsule_broker_buffers_get(&reservation));
	} else {
		assert(!capsule_broker_buffers_find(&reservation));
		assert(!capsule_broker_buffers_reserve());
	}
	return 0;
}
