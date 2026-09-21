/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_CAPSULE_BROKER_BUFFERS_H
#define BOOT_CAPSULE_BROKER_BUFFERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define CAPSULE_BROKER_BUFFER_ALIGNMENT 4096U
#define CAPSULE_BROKER_COMMUNICATION_RESERVATION_SIZE 4096U
#define CAPSULE_BROKER_SCRATCH_SIZE 4096U

struct capsule_broker_buffer_reservation {
	uint64_t communication_base;
	uint64_t communication_reserved_size;
	uint64_t communication_size;
	uint64_t staging_base;
	uint64_t staging_size;
};

struct capsule_broker_scratch_reservation {
	uint64_t write_base;
	uint64_t read_base;
	uint32_t erase_size;
	uint32_t reserved;
};

/* Platform-owned immutable capacity; zero means unavailable. */
size_t platform_capsule_broker_staging_size(void);

bool capsule_broker_buffers_reserve(void);
bool capsule_broker_buffers_find(
	struct capsule_broker_buffer_reservation *reservation);
bool capsule_broker_buffers_overlap(
	const struct capsule_broker_buffer_reservation *reservation,
	uint64_t base, uint64_t size);
bool capsule_broker_buffers_get(
	struct capsule_broker_buffer_reservation *reservation);
void capsule_broker_buffers_scrub(void);

/* Two distinct SMM-owned erase-block buffers; acquisition is one-shot. */
enum cb_err capsule_broker_scratch_acquire(uint32_t erase_size,
	struct capsule_broker_scratch_reservation *reservation);
void capsule_broker_scratch_scrub(void);

#endif
