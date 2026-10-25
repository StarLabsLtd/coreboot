/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef COMMONLIB_AUTHVAR_SERVICE_H
#define COMMONLIB_AUTHVAR_SERVICE_H

#include <commonlib/bsd/compiler.h>
#include <stddef.h>
#include <stdint.h>

#define LB_AUTHVAR_SERVICE_ENDPOINT_REVISION 5U

#define LB_AUTHVAR_ENDPOINT_COREBOOT_SMM_OWNER  (1U << 0)
#define LB_AUTHVAR_ENDPOINT_FIXED_COMMUNICATION (1U << 1)
/* Hardware protects authoritative SMM state/code and its backend, not the
 * shared mailbox against DMA. Each request must be snapshotted into SMRAM. */
#define LB_AUTHVAR_ENDPOINT_PROTECTED_AUTHORITY (1U << 2)
#define LB_AUTHVAR_ENDPOINT_CPU_RENDEZVOUS      (1U << 3)
#define LB_AUTHVAR_ENDPOINT_POLICY_IN_SMM       (1U << 4)
#define LB_AUTHVAR_ENDPOINT_NO_RAW_SMMSTORE     (1U << 5)
#define LB_AUTHVAR_ENDPOINT_SMM_BWP_REQUIRED    (1U << 6)
#define LB_AUTHVAR_ENDPOINT_LIFECYCLE_SEALED    (1U << 7)
/* Repeatable boot-services op9, not private BOOT delivery or origin proof. */
#define LB_AUTHVAR_ENDPOINT_IMAGE_POLICY_GENERAL (1U << 8)
/* Installed MM-owned predicate keys retain their class across deletion. */
#define LB_AUTHVAR_ENDPOINT_STATE_PREDICATE_PINNED (1U << 9)
/* Finite confirmed Setup actions; ordinary SetVariable gains no privilege. */
#define LB_AUTHVAR_ENDPOINT_CONFIRMED_SETUP (1U << 10)
#define LB_AUTHVAR_ENDPOINT_REQUIRED_FLAGS \
	(LB_AUTHVAR_ENDPOINT_COREBOOT_SMM_OWNER | \
	 LB_AUTHVAR_ENDPOINT_FIXED_COMMUNICATION | \
	 LB_AUTHVAR_ENDPOINT_PROTECTED_AUTHORITY | \
	 LB_AUTHVAR_ENDPOINT_CPU_RENDEZVOUS | \
	 LB_AUTHVAR_ENDPOINT_POLICY_IN_SMM | \
	 LB_AUTHVAR_ENDPOINT_NO_RAW_SMMSTORE | \
	 LB_AUTHVAR_ENDPOINT_SMM_BWP_REQUIRED | \
	 LB_AUTHVAR_ENDPOINT_LIFECYCLE_SEALED)

#define LB_AUTHVAR_ENDPOINT_TRANSPORT_APM_IO8 1U

struct lb_authvar_service_endpoint {
	uint32_t tag;
	uint32_t size;
	uint16_t revision;
	uint16_t header_size;
	uint32_t flags;
	uint64_t generation;
	uint64_t communication_base;
	uint32_t communication_size;
	uint32_t message_size;
	uint16_t transport;
	uint16_t trigger_width;
	uint32_t trigger_address;
	uint32_t trigger_value;
	uint32_t maximum_name_size;
	/* Logical data limit; unused bytes through message_size are zero padding. */
	uint32_t maximum_data_size;
	uint32_t reserved;
} __packed __aligned(4);

_Static_assert(sizeof(struct lb_authvar_service_endpoint) == 64U,
	"authenticated-variable endpoint ABI");
_Static_assert(offsetof(struct lb_authvar_service_endpoint, generation) == 16U &&
	offsetof(struct lb_authvar_service_endpoint, communication_base) == 24U &&
	offsetof(struct lb_authvar_service_endpoint, transport) == 40U &&
	offsetof(struct lb_authvar_service_endpoint, maximum_name_size) == 52U &&
	offsetof(struct lb_authvar_service_endpoint, reserved) == 60U,
	"authenticated-variable endpoint layout");

#define PAYLOAD_MM_AUTHVAR_SERVICE_REVISION 3U
#define PAYLOAD_MM_AUTHVAR_CONFIRMED_REVISION 4U
#define PAYLOAD_MM_AUTHVAR_CONFIRMED_HEADER_SIZE 184U
#define PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_REVISION 5U
#define PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_HEADER_SIZE 192U
#define PAYLOAD_MM_AUTHVAR_POLICY_MIN_SIZE 44U
#define PAYLOAD_MM_AUTHVAR_POLICY_MAX_SIZE 512U
#define PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE 144U
#define PAYLOAD_MM_AUTHVAR_SERVICE_MIN_MESSAGE_SIZE 512U
#define PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE (64U * 1024U)
#define PAYLOAD_MM_AUTHVAR_SERVICE_PENDING UINT32_MAX
#define PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE 0U
#define PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING UINT64_MAX

/* Fixed-width EFI_STATUS values carried by the wire ABI. */
#define PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT \
	(1ULL << 63)
#define PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS 0ULL
#define PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 2ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 3ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 5ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 7ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 8ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 9ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 10ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 14ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 15ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_ALREADY_STARTED \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 20ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_ABORTED \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 21ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 26ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 33ULL)

#define PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE        (1U << 0)
#define PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS  (1U << 1)
#define PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS      (1U << 2)
#define PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR      (1U << 3)
#define PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE (1U << 4)
#define PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED  (1U << 5)
#define PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE        (1U << 6)
#define PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED \
	(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE | \
	 PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS | \
	 PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS | \
	 PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR | \
	 PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED | \
	 PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE)

enum payload_mm_authvar_service_operation {
	PAYLOAD_MM_AUTHVAR_SERVICE_GET = 1,
	PAYLOAD_MM_AUTHVAR_SERVICE_NEXT = 2,
	PAYLOAD_MM_AUTHVAR_SERVICE_SET = 3,
	PAYLOAD_MM_AUTHVAR_SERVICE_QUERY = 4,
	PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT = 5,
	PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME = 6,
	PAYLOAD_MM_AUTHVAR_SERVICE_REGISTER_POLICY = 7,
	PAYLOAD_MM_AUTHVAR_SERVICE_LOCK_POLICY = 8,
	PAYLOAD_MM_AUTHVAR_SERVICE_IMAGE_POLICY_SNAPSHOT = 9,
	PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY = 10,
	PAYLOAD_MM_AUTHVAR_SERVICE_CONFIRMED_SETUP = 11,
};

enum payload_mm_authvar_confirmed_action {
	PAYLOAD_MM_AUTHVAR_CONFIRMED_ENABLE = 1,
	PAYLOAD_MM_AUTHVAR_CONFIRMED_CUSTOM_MODE = 2,
	PAYLOAD_MM_AUTHVAR_CONFIRMED_DELETE_PK = 3,
	PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_MUTATION = 4,
};

enum payload_mm_authvar_confirmed_key {
	PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_PK = 1,
	PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_KEK = 2,
	PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_DB = 3,
	PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_DBX = 4,
	PAYLOAD_MM_AUTHVAR_CONFIRMED_KEY_DBT = 5,
};

enum payload_mm_authvar_confirmed_mutation {
	PAYLOAD_MM_AUTHVAR_CONFIRMED_REPLACE = 1,
	PAYLOAD_MM_AUTHVAR_CONFIRMED_APPEND = 2,
	PAYLOAD_MM_AUTHVAR_CONFIRMED_DELETE = 3,
};

#define PAYLOAD_MM_AUTHVAR_CONFIRMED_RESET_REQUIRED 1U

#define PAYLOAD_MM_AUTHVAR_KEY_CLASSIFICATION_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_KEY_CLASSIFICATION_SIZE 16U
#define PAYLOAD_MM_AUTHVAR_KEY_PERSISTENT 1U
#define PAYLOAD_MM_AUTHVAR_KEY_SYNTHETIC 2U
/* Protected-resident volatile state; deletion retention requires predicate pinning. */
#define PAYLOAD_MM_AUTHVAR_KEY_VOLATILE_PINNED 3U
#define PAYLOAD_MM_AUTHVAR_KEY_VISIBLE 1U

/*
 * Fixed header at the start of the endpoint's complete message buffer. Names
 * occupy maximum_name_size bytes immediately after this header; data follows
 * at the next eight-byte boundary. There are no caller-provided offsets or
 * pointers. The status field uses the fixed 64-bit
 * PAYLOAD_MM_AUTHVAR_STATUS_* wire values, independent of native word size.
 * READY_TO_BOOT and ENTER_RUNTIME can only restrict service state.
 */
struct payload_mm_authvar_service_frame {
	uint32_t revision;
	uint32_t header_size;
	uint32_t operation;
	uint32_t flags;
	uint64_t generation;
	uint64_t request_id;
	uint8_t vendor_guid[16];
	uint32_t attributes;
	uint32_t name_size;
	uint32_t data_size;
	uint32_t name_capacity;
	uint32_t data_capacity;
	uint32_t reserved0;
	uint64_t maximum_storage;
	uint64_t remaining_storage;
	uint64_t maximum_variable;
	uint64_t status;
	uint32_t result_name_size;
	uint32_t result_data_size;
	uint32_t result_attributes;
	uint8_t result_vendor_guid[16];
	uint32_t reserved[2];
	uint32_t completion;
} __aligned(8);

/* Only this finite v4 operation carries boot-presence authorization. The
 * ordinary v3 header, offsets and SetVariable contract remain unchanged. */
struct payload_mm_authvar_confirmed_frame {
	struct payload_mm_authvar_service_frame service;
	uint8_t capability[32];
	uint32_t value;
	uint32_t result_flags;
} __aligned(8);

/* Complete AUTH2 bytes follow this header; service.data_size is their sole length. */
struct payload_mm_authvar_confirmed_key_frame {
	struct payload_mm_authvar_confirmed_frame confirmed;
	uint32_t key_id;
	uint32_t mutation;
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_confirmed_key_frame) == 192U &&
	offsetof(struct payload_mm_authvar_confirmed_key_frame, key_id) == 184U &&
	offsetof(struct payload_mm_authvar_confirmed_key_frame, mutation) == 188U,
	"confirmed key action wire layout");

_Static_assert(sizeof(struct payload_mm_authvar_confirmed_frame) == 184U &&
	offsetof(struct payload_mm_authvar_confirmed_frame, capability) == 144U &&
	offsetof(struct payload_mm_authvar_confirmed_frame, value) == 176U &&
	offsetof(struct payload_mm_authvar_confirmed_frame, result_flags) == 180U,
	"confirmed Setup action wire layout");

_Static_assert(sizeof(struct payload_mm_authvar_service_frame) ==
	PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE,
	"authenticated-variable service frame ABI");
_Static_assert(_Alignof(struct payload_mm_authvar_service_frame) == 8,
	"authenticated-variable service frame alignment");
_Static_assert(offsetof(struct payload_mm_authvar_service_frame, generation) == 16 &&
	offsetof(struct payload_mm_authvar_service_frame, vendor_guid) == 32 &&
	offsetof(struct payload_mm_authvar_service_frame, maximum_storage) == 72 &&
	offsetof(struct payload_mm_authvar_service_frame, status) == 96 &&
	offsetof(struct payload_mm_authvar_service_frame, result_vendor_guid) == 116 &&
	offsetof(struct payload_mm_authvar_service_frame, completion) == 140,
	"authenticated-variable service frame layout");

#endif
