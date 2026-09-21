/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_CAPSULE_BROKER_H
#define BOOT_CAPSULE_BROKER_H

#include <boot/payload_mm_authvar.h>
#include <commonlib/coreboot_tables.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define CAPSULE_BROKER_TRANSPORT_REVISION_1 1U
#define CAPSULE_BROKER_TRANSPORT_REVISION 2U
#define CAPSULE_BROKER_DIGEST_SHA256 1U
#define CAPSULE_BROKER_DIGEST_SIZE 32U
#define CAPSULE_BROKER_RESULT_PENDING UINT32_MAX
#define CAPSULE_BROKER_STATUS_PENDING UINT32_MAX
#define CAPSULE_BROKER_APM_PORT 0xb2U
#define CAPSULE_BROKER_APM_COMMAND 0xe8U

/* Broker-owned description of the authenticated raw ROM in an envelope. */
struct capsule_broker_raw_image {
	uint64_t offset;
	uint64_t size;
	uint32_t lowest_supported_version;
	uint32_t reserved;
};

/* Protected completion metadata released only after verified media success. */
struct capsule_broker_success {
	uint32_t version;
	uint32_t lowest_supported_version;
};

_Static_assert(sizeof(struct capsule_broker_raw_image) == 24,
	"capsule broker authenticated image layout");
_Static_assert(sizeof(struct capsule_broker_success) == 8,
	"capsule broker success layout");

enum capsule_broker_result {
	CAPSULE_BROKER_RESULT_SUCCESS = 0,
	CAPSULE_BROKER_RESULT_EXECUTION = 1,
};

#define CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS 0U
#define CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL 1U

enum capsule_broker_transport_operation {
	CAPSULE_BROKER_TRANSPORT_EXECUTE = 1,
	CAPSULE_BROKER_TRANSPORT_READ_INFO = 2,
};

#define CAPSULE_BROKER_INFO_POLICY_REVISION 1U
#define CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_STATUS_VALID BIT(0)
#define CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_VERSION_VALID BIT(1)
#define CAPSULE_BROKER_INFO_STATE_VALID_FLAGS \
	(CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_STATUS_VALID | \
	 CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_VERSION_VALID)

/* Authenticated current-image facts sealed by trusted platform init. */
struct capsule_broker_info_policy {
	uint32_t revision;
	uint32_t size;
	guid_t image_type;
	uint64_t hardware_instance;
	uint32_t current_version;
	uint32_t lowest_supported_version;
	uint32_t image_size;
	uint32_t capabilities;
	uint32_t reserved[2];
} __aligned(8);

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

/* Revision-2 READ_INFO response occupying transport bytes 40..167. */
struct capsule_broker_transport_info {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint64_t transaction;
	guid_t image_type;
	uint64_t hardware_instance;
	uint32_t current_version;
	uint32_t lowest_supported_version;
	uint32_t image_size;
	uint32_t capabilities;
	uint32_t state_flags;
	uint32_t last_attempt_version;
	uint32_t last_attempt_status;
	uint32_t reserved[12];
	/* Completion marker: written only after every other response byte. */
	uint32_t result;
} __aligned(8);

#define CAPSULE_BROKER_TRANSPORT_REQUEST_OFFSET 0U
#define CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET 40U
#define CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET 128U
#define CAPSULE_BROKER_TRANSPORT_INFO_OFFSET 40U
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
_Static_assert(sizeof(struct capsule_broker_info_policy) == 56,
	"capsule broker info policy ABI");
_Static_assert(_Alignof(struct capsule_broker_info_policy) == 8 &&
	offsetof(struct capsule_broker_info_policy, image_type) == 8 &&
	offsetof(struct capsule_broker_info_policy, hardware_instance) == 24 &&
	offsetof(struct capsule_broker_info_policy, current_version) == 32 &&
	offsetof(struct capsule_broker_info_policy, reserved) == 48,
	"capsule broker info policy layout");
_Static_assert(sizeof(struct capsule_broker_transport_info) == 128,
	"capsule broker info response ABI");
_Static_assert(_Alignof(struct capsule_broker_transport_info) == 8 &&
	offsetof(struct capsule_broker_transport_info, generation) == 8 &&
	offsetof(struct capsule_broker_transport_info, image_type) == 24 &&
	offsetof(struct capsule_broker_transport_info, hardware_instance) == 40 &&
	offsetof(struct capsule_broker_transport_info, current_version) == 48 &&
	offsetof(struct capsule_broker_transport_info, state_flags) == 64 &&
	offsetof(struct capsule_broker_transport_info, reserved) == 76 &&
	offsetof(struct capsule_broker_transport_info, result) == 124,
	"capsule broker info response layout");
_Static_assert(sizeof(struct capsule_broker_transport_request) ==
	CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET &&
	CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET +
		sizeof(struct payload_mm_fmp_capsule_intent) ==
		CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET &&
	CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET +
		sizeof(struct capsule_broker_transport_result) ==
		CAPSULE_BROKER_TRANSPORT_SIZE,
	"capsule broker transport layout");
_Static_assert(CAPSULE_BROKER_TRANSPORT_INFO_OFFSET +
		sizeof(struct capsule_broker_transport_info) ==
		CAPSULE_BROKER_TRANSPORT_SIZE,
	"capsule broker info transport layout");

/* Dormant sealed producer contract; no platform in this tree installs it. */
enum cb_err capsule_broker_info_policy_install(
	const struct capsule_broker_info_policy *trusted_policy,
	payload_mm_authvar_protected_storage storage_is_protected, void *context);

/* Dormant typed entry point, reachable only through the exact route below. */
enum cb_err capsule_broker_transport_dispatch(void);

/* Platform composition proves the exact sealed endpoint before publication. */
typedef enum cb_err (*capsule_broker_endpoint_ready_fn)(
	const struct lb_capsule_broker_endpoint *endpoint,
	const struct lb_capsule_handoff *handoff, size_t handoff_size,
	const struct lb_efi_fw_info *firmware);

enum cb_err capsule_broker_endpoint_publication_install(
	const struct lb_capsule_broker_endpoint *endpoint,
	const struct lb_capsule_handoff *handoff, size_t handoff_size,
	const struct lb_efi_fw_info *firmware,
	capsule_broker_endpoint_ready_fn ready);
void capsule_broker_endpoint_publication_close(void);

/* Called only by the platform's exclusive APMC handler with observed I/O. */
enum cb_err capsule_broker_smi_dispatch(uint16_t port, uint8_t value);

enum cb_err capsule_broker_endpoint_validate(
	const struct lb_capsule_broker_endpoint *endpoint,
	const struct lb_capsule_handoff *handoff);

#endif
