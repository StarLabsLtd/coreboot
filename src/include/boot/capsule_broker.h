/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_CAPSULE_BROKER_H
#define BOOT_CAPSULE_BROKER_H

#include <commonlib/coreboot_tables.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define CAPSULE_BROKER_MESSAGE_REVISION 1U
#define CAPSULE_BROKER_DIGEST_SHA256 1U
#define CAPSULE_BROKER_DIGEST_SIZE 32U
#define CAPSULE_BROKER_RESULT_PENDING UINT32_MAX
#define CAPSULE_BROKER_STATUS_PENDING UINT32_MAX

enum capsule_broker_operation {
	CAPSULE_BROKER_APPLY = 1,
	CAPSULE_BROKER_CLOSE = 2,
};

enum capsule_broker_result {
	CAPSULE_BROKER_RESULT_SUCCESS = 0,
	CAPSULE_BROKER_RESULT_INVALID = 1,
	CAPSULE_BROKER_RESULT_CLOSED = 2,
	CAPSULE_BROKER_RESULT_REPLAY = 3,
	CAPSULE_BROKER_RESULT_CHECKPOINT = 4,
	CAPSULE_BROKER_RESULT_DIGEST = 5,
	CAPSULE_BROKER_RESULT_MEDIA = 6,
};

#define CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS 0U
#define CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL 1U

struct capsule_broker_message {
	uint32_t revision;
	uint32_t size;
	uint32_t operation;
	uint32_t flags;
	uint64_t generation;
	uint64_t transaction;
	uint64_t image_size;
	uint32_t digest_algorithm;
	uint32_t digest_size;
	uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE];
	uint32_t result;
	uint32_t last_attempt_status;
	uint32_t attempted_version;
	uint32_t reserved;
} __aligned(8);

_Static_assert(sizeof(struct capsule_broker_message) == 96,
	"capsule broker message ABI");
_Static_assert(_Alignof(struct capsule_broker_message) == 8,
	"capsule broker message alignment");
_Static_assert(offsetof(struct capsule_broker_message, generation) == 16 &&
	offsetof(struct capsule_broker_message, transaction) == 24 &&
	offsetof(struct capsule_broker_message, image_size) == 32 &&
	offsetof(struct capsule_broker_message, digest_algorithm) == 40 &&
	offsetof(struct capsule_broker_message, digest) == 48 &&
	offsetof(struct capsule_broker_message, result) == 80 &&
	offsetof(struct capsule_broker_message, attempted_version) == 88,
	"capsule broker message layout");

enum cb_err capsule_broker_endpoint_validate(
	const struct lb_capsule_broker_endpoint *endpoint,
	const struct lb_capsule_handoff *handoff);

#endif
