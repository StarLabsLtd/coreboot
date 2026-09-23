/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_ROUTE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_ROUTE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE 0x00000001U
#define PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE 0x00000010U
#define PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH 0x00000020U
#define PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND 0x00000040U
#define PAYLOAD_MM_AUTHVAR_ROUTE_MAX_AUTHORITIES 2U
#define PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE 4096U

enum payload_mm_authvar_route_result {
	PAYLOAD_MM_AUTHVAR_ROUTE_OK = 0,
	PAYLOAD_MM_AUTHVAR_ROUTE_INVALID,
	PAYLOAD_MM_AUTHVAR_ROUTE_UNSUPPORTED,
	PAYLOAD_MM_AUTHVAR_ROUTE_WRITE_PROTECTED,
};

enum payload_mm_authvar_target {
	PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE = 0,
	PAYLOAD_MM_AUTHVAR_TARGET_PK,
	PAYLOAD_MM_AUTHVAR_TARGET_KEK,
	PAYLOAD_MM_AUTHVAR_TARGET_DB,
	PAYLOAD_MM_AUTHVAR_TARGET_DBX,
	PAYLOAD_MM_AUTHVAR_TARGET_DBT,
};

enum payload_mm_authvar_authority {
	PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE = 0,
	PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS,
	PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
	PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PAYLOAD_CERT,
	PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK,
	PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER,
	PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB,
};

struct payload_mm_authvar_route_request {
	uint8_t vendor_guid[16];
	const void *name;
	size_t name_size;
	uint32_t attributes;
	uint32_t existing_attributes;
	size_t payload_size;
	bool target_exists;
	bool setup_mode;
	bool custom_mode;
	bool trusted_physical_presence;
	bool require_self_signed_pk;
};

struct payload_mm_authvar_route_plan {
	enum payload_mm_authvar_target target;
	enum payload_mm_authvar_authority authorities[
		PAYLOAD_MM_AUTHVAR_ROUTE_MAX_AUTHORITIES];
	uint8_t authority_count;
	bool require_signature_list;
	bool enter_user_mode;
	bool enter_setup_mode;
	bool mark_vendor_keys_modified;
};

/*
 * Produce only a routing plan from an immutable protected snapshot. The name
 * is canonical UTF-16LE without its terminating NUL. trusted_physical_presence
 * must originate in a platform-owned presence source, never request bytes.
 * No trust, CMS, replay, payload-format or mutation decision is made here.
 */
enum payload_mm_authvar_route_result payload_mm_authvar_route_plan(
	const struct payload_mm_authvar_route_request *request,
	struct payload_mm_authvar_route_plan *plan);

#endif
