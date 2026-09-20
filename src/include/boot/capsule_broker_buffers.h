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

/* Platform-owned immutable capacity; zero means unavailable. */
size_t platform_capsule_broker_staging_size(void);

bool capsule_broker_buffers_reserve(void);
bool capsule_broker_buffers_get(
	struct capsule_broker_buffer_reservation *reservation);
void capsule_broker_buffers_scrub(void);

/* SMM-module-owned scratch; acquiring it is deliberately one-shot. */
enum cb_err capsule_broker_scratch_acquire(void **scratch, size_t *size);
void capsule_broker_scratch_scrub(void);

#endif
