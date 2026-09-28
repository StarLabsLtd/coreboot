/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_H

#include <commonlib/coreboot_tables.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_SIZE 64U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE 4096U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT 4096U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_PENDING UINT64_MAX
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_ERROR_BIT (1ULL << 63)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS 0ULL
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_INVALID_PARAMETER \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_ERROR_BIT | 2ULL)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_UNSUPPORTED \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_ERROR_BIT | 3ULL)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_DEVICE_ERROR \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_ERROR_BIT | 7ULL)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_ACCESS_DENIED \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_ERROR_BIT | 15ULL)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SECURITY_VIOLATION \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_ERROR_BIT | 26ULL)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE 0U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING UINT32_MAX

struct payload_mm_authvar_presence_lifecycle_close_message {
	uint32_t revision;
	uint32_t size;
	uint32_t source;
	uint32_t flags;
	uint64_t generation;
	uint64_t request_id;
	uint64_t status;
	uint32_t reserved[5];
	uint32_t completion;
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_presence_lifecycle_close_message) == 64,
	"presence lifecycle-close message ABI");
_Static_assert(_Alignof(struct payload_mm_authvar_presence_lifecycle_close_message) == 8,
	"presence lifecycle-close message alignment");
_Static_assert(offsetof(struct payload_mm_authvar_presence_lifecycle_close_message,
	completion) == 60, "presence lifecycle-close completion must be last");

enum cb_err payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint);
enum cb_err payload_mm_authvar_presence_lifecycle_close_request_validate(
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	const void *message, size_t message_size);
enum cb_err payload_mm_authvar_presence_lifecycle_close_response_validate(
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	const void *request, const void *response, size_t message_size);

#endif
