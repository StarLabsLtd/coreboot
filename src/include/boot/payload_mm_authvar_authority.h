/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_AUTHORITY_H
#define BOOT_PAYLOAD_MM_AUTHVAR_AUTHORITY_H

#include <boot/payload_mm_authvar_policy.h>
#include <boot/payload_mm_authvar_route.h>
#include <payload_mm_cms.h>

#define PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS 5U

struct payload_mm_authvar_authority_verification {
	enum payload_mm_authvar_authority accepted_authority;
	size_t new_binding_size;
	uint8_t new_binding[PAYLOAD_MM_MAX_DIGEST_SIZE];
};

enum payload_mm_authvar_authority_intent {
	PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE = 1U << 0,
	PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE = 1U << 1,
	PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS = 1U << 2,
	PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING = 1U << 3,
	PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING = 1U << 4,
};

struct payload_mm_authvar_authority_facts {
	bool setup_mode;
	bool custom_mode;
	bool trusted_physical_presence;
	bool require_self_signed_pk;
};

struct payload_mm_authvar_authority_verify_request {
	struct payload_mm_crypto_owner *owner;
	const struct payload_mm_crypto_span *pkcs7;
	const struct payload_mm_crypto_span *content;
	size_t content_count;
	const struct payload_mm_authvar_store_index *index;
	const struct payload_mm_authvar_route_plan *route;
	const struct payload_mm_crypto_span *new_payload;
};

/* The verifier runs synchronously and must retain no pointer or state. */
typedef enum payload_mm_verify_status payload_mm_authvar_authority_verify_fn(
	void *context,
	const struct payload_mm_authvar_authority_verify_request *request,
	struct payload_mm_authvar_authority_verification *verification);

struct payload_mm_authvar_authority_snapshot {
	const struct payload_mm_authvar_policy_request *request;
	const struct payload_mm_authvar_store_index *index;
	struct payload_mm_crypto_owner *owner;
	struct payload_mm_authvar_authority_facts facts;
	payload_mm_authvar_authority_verify_fn *verify;
	void *verify_context;
	void *append_workspace;
	size_t append_workspace_size;
};

enum payload_mm_authvar_authority_outcome {
	PAYLOAD_MM_AUTHVAR_OUTCOME_NONE = 0,
	PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION,
	PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP,
	PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND,
};

struct payload_mm_authvar_authority_decision {
	enum payload_mm_authvar_authority_outcome outcome;
	struct payload_mm_authvar_policy_mutation mutation;
	const void *data;
	size_t data_size;
	uint32_t intents;
	enum payload_mm_authvar_target target;
	enum payload_mm_authvar_authority accepted_authority;
	size_t new_binding_size;
	uint8_t new_binding[PAYLOAD_MM_MAX_DIGEST_SIZE];
};

/*
 * Authorize one immutable protected Auth2 SET snapshot. The function emits one
 * canonical target outcome plus typed post-success intents. NOT_FOUND maps to
 * the UEFI SetVariable status only after successful authentication. It
 * performs no
 * store or media write and does not install a provider. The caller must commit
 * the target and every intent atomically before exposing success.
 */
enum payload_mm_verify_status payload_mm_authvar_authority_decide(
	const struct payload_mm_authvar_authority_snapshot *snapshot,
	struct payload_mm_authvar_authority_decision *decision);

#endif
