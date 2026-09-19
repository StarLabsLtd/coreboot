/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_CAPSULE_BROKER_H
#define BOOT_CAPSULE_BROKER_H

#include <boot/payload_mm_authvar.h>
#include <commonlib/coreboot_tables.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define CAPSULE_BROKER_TRANSPORT_REVISION 1U
#define CAPSULE_BROKER_DIGEST_SHA256 1U
#define CAPSULE_BROKER_DIGEST_SIZE 32U
#define CAPSULE_BROKER_RESULT_PENDING UINT32_MAX
#define CAPSULE_BROKER_STATUS_PENDING UINT32_MAX

/* Broker-owned description of the authenticated raw ROM in an envelope. */
struct capsule_broker_raw_image {
	uint64_t offset;
	uint64_t size;
};

enum capsule_broker_result {
	CAPSULE_BROKER_RESULT_SUCCESS = 0,
	CAPSULE_BROKER_RESULT_EXECUTION = 1,
};

#define CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS 0U
#define CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL 1U

enum capsule_broker_transport_operation {
	CAPSULE_BROKER_TRANSPORT_EXECUTE = 1,
};

/*
 * Fixed transport header. The intent and result immediately follow this
 * header; callers cannot supply an address or select another message buffer.
 */
struct capsule_broker_transport_request {
	uint32_t revision;
	uint32_t size;
	uint32_t operation;
	uint32_t flags;
	uint64_t generation;
	uint64_t transaction;
	uint32_t intent_size;
	uint32_t result_size;
} __aligned(8);

struct capsule_broker_transport_result {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint64_t transaction;
	uint32_t attempted_version;
	uint32_t reserved;
	uint32_t last_attempt_status;
	/* Completion marker: written only after every other response byte. */
	uint32_t result;
} __aligned(8);

#define CAPSULE_BROKER_TRANSPORT_REQUEST_OFFSET 0U
#define CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET 40U
#define CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET 128U
#define CAPSULE_BROKER_TRANSPORT_SIZE 168U

_Static_assert(sizeof(struct capsule_broker_transport_request) == 40,
	"capsule broker request ABI");
_Static_assert(_Alignof(struct capsule_broker_transport_request) == 8,
	"capsule broker request alignment");
_Static_assert(offsetof(struct capsule_broker_transport_request, generation) == 16 &&
	offsetof(struct capsule_broker_transport_request, transaction) == 24 &&
	offsetof(struct capsule_broker_transport_request, intent_size) == 32,
	"capsule broker request layout");
_Static_assert(sizeof(struct capsule_broker_transport_result) == 40,
	"capsule broker result ABI");
_Static_assert(_Alignof(struct capsule_broker_transport_result) == 8,
	"capsule broker result alignment");
_Static_assert(offsetof(struct capsule_broker_transport_result, generation) == 8 &&
	offsetof(struct capsule_broker_transport_result, transaction) == 16 &&
	offsetof(struct capsule_broker_transport_result, last_attempt_status) == 32 &&
	offsetof(struct capsule_broker_transport_result, result) == 36,
	"capsule broker result layout");
_Static_assert(sizeof(struct capsule_broker_transport_request) ==
	CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET &&
	CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET +
		sizeof(struct payload_mm_fmp_capsule_intent) ==
		CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET &&
	CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET +
		sizeof(struct capsule_broker_transport_result) ==
		CAPSULE_BROKER_TRANSPORT_SIZE,
	"capsule broker transport layout");

/* Dormant typed entry point; no SMI route or table producer calls this. */
enum cb_err capsule_broker_transport_dispatch(void);

enum cb_err capsule_broker_endpoint_validate(
	const struct lb_capsule_broker_endpoint *endpoint,
	const struct lb_capsule_handoff *handoff);

#endif
