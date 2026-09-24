/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_SET_PREFLIGHT_H
#define PAYLOAD_MM_AUTHVAR_SET_PREFLIGHT_H

#include <boot/payload_mm_authvar_policy.h>

#define PAYLOAD_MM_AUTHVAR_SET_REQUEST_ATTRIBUTES \
	(PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED | \
	 PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE)
/* Monotonic count + UEFI GUID certificate header + RSA2048/SHA256 cert block. */
#define PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE 560U

enum payload_mm_authvar_set_kind {
	PAYLOAD_MM_AUTHVAR_SET_ORDINARY_WRITE = 1,
	PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE,
	PAYLOAD_MM_AUTHVAR_SET_NOOP,
	PAYLOAD_MM_AUTHVAR_SET_AUTH2,
};

struct payload_mm_authvar_set_snapshot {
	const struct payload_mm_authvar_policy_request *request;
	const struct payload_mm_authvar_store_index *index;
	bool at_runtime;
	bool trusted_physical_presence;
};

struct payload_mm_authvar_set_plan {
	enum payload_mm_authvar_set_kind kind;
	uint64_t post_auth_status;
};

uint64_t payload_mm_authvar_set_preflight(
	const struct payload_mm_authvar_set_snapshot *snapshot,
	struct payload_mm_authvar_set_plan *plan);

#endif
