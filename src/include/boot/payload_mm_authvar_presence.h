/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_H

#include <commonlib/coreboot_tables.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE 80U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING UINT32_MAX
#define PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE 0U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING UINT64_MAX

/* Fixed-width EFI_STATUS values carried by the wire ABI. */
#define PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ERROR_BIT (1ULL << 63)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS 0ULL
#define PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_UNSUPPORTED \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ERROR_BIT | 3ULL)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ERROR_BIT | 7ULL)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_WRITE_PROTECTED \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ERROR_BIT | 8ULL)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ACCESS_DENIED \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ERROR_BIT | 15ULL)
#define PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SECURITY_VIOLATION \
	(PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ERROR_BIT | 26ULL)

/* Fixed request and completion frame for ENTER_SETUP_MODE only. */
struct payload_mm_authvar_presence_message {
	uint32_t revision;
	uint32_t size;
	uint32_t action;
	uint32_t flags;
	uint64_t generation;
	uint64_t request_id;
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];
	uint64_t status;
	uint32_t reserved;
	uint32_t completion;
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_presence_message) ==
	PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
	"authenticated-variable presence message ABI");
_Static_assert(_Alignof(struct payload_mm_authvar_presence_message) == 8,
	"authenticated-variable presence message alignment");
_Static_assert(offsetof(struct payload_mm_authvar_presence_message, generation) == 16 &&
	offsetof(struct payload_mm_authvar_presence_message, request_id) == 24 &&
	offsetof(struct payload_mm_authvar_presence_message, capability) == 32 &&
	offsetof(struct payload_mm_authvar_presence_message, status) == 64 &&
	offsetof(struct payload_mm_authvar_presence_message, reserved) == 72 &&
	offsetof(struct payload_mm_authvar_presence_message, completion) == 76,
	"authenticated-variable presence message layout");

enum cb_err payload_mm_authvar_presence_endpoint_validate(
	const struct lb_authvar_presence_endpoint *endpoint);
enum cb_err payload_mm_authvar_presence_request_validate(
	const struct lb_authvar_presence_endpoint *endpoint, const void *message,
	size_t message_size);
enum cb_err payload_mm_authvar_presence_response_validate(
	const struct lb_authvar_presence_endpoint *endpoint, const void *request,
	const void *response, size_t message_size);

#endif
