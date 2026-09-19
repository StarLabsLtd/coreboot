/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_DISPATCH_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_DISPATCH_INTERNAL_H

#include <boot/payload_mm_authvar.h>
#include "payload_mm_fmp_state_internal.h"

struct payload_mm_fmp_dispatch_workspace {
	struct payload_mm_authvar_request request;
	size_t message_size;
	uint8_t message[sizeof(struct payload_mm_fmp_state_message)] __aligned(8);
	struct payload_mm_fmp_state_command command;
} __aligned(8);

_Static_assert(offsetof(struct payload_mm_fmp_dispatch_workspace, request) == 0 &&
	offsetof(struct payload_mm_fmp_dispatch_workspace, message_size) == 40 &&
	offsetof(struct payload_mm_fmp_dispatch_workspace, message) == 48 &&
	offsetof(struct payload_mm_fmp_dispatch_workspace, command) == 112 &&
	sizeof(struct payload_mm_fmp_dispatch_workspace) == 280,
	"Payload-MM FMP dispatch workspace layout");

enum cb_err payload_mm_fmp_dispatch_workspace_install(
	struct payload_mm_fmp_dispatch_workspace *trusted_workspace,
	payload_mm_authvar_protected_storage storage_is_protected, void *context);
bool payload_mm_fmp_dispatch_ready(void);
bool payload_mm_fmp_dispatch_buffer_available(const void *buffer, size_t size);
enum cb_err payload_mm_fmp_dispatch_prepare(uint64_t request_address,
	const void *current_state, size_t current_state_size);
const struct payload_mm_fmp_state_command *payload_mm_fmp_dispatch_command(void);
enum cb_err payload_mm_fmp_dispatch_complete(uint64_t transaction);

#endif
