/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_SERVICE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_SERVICE_H

#include <commonlib/coreboot_tables.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PAYLOAD_MM_AUTHVAR_SERVICE_REVISION 1U
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
#define PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 14ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 15ULL)
#define PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION \
	(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 26ULL)

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
};

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

enum cb_err payload_mm_authvar_service_endpoint_validate(
	const struct lb_authvar_service_endpoint *endpoint);
enum cb_err payload_mm_authvar_service_request_validate(
	const struct lb_authvar_service_endpoint *endpoint, const void *message,
	size_t message_size);
enum cb_err payload_mm_authvar_service_response_validate(
	const struct lb_authvar_service_endpoint *endpoint, const void *request,
	const void *response, size_t message_size);

#endif
