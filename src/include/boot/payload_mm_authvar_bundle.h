/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_BUNDLE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_BUNDLE_H

#include <boot/payload_mm_authvar_authority.h>
#include <boot/payload_mm_authvar_certdb.h>

#define PAYLOAD_MM_AUTHVAR_BUNDLE_MAX_MUTATIONS 3U

#define PAYLOAD_MM_AUTHVAR_MODE_SETUP		(1U << 0)
#define PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT	(1U << 1)
#define PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS	(1U << 2)

enum payload_mm_authvar_bundle_role {
	PAYLOAD_MM_AUTHVAR_BUNDLE_TARGET = 0,
	PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB,
	PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE,
	PAYLOAD_MM_AUTHVAR_BUNDLE_VENDOR_KEYS_NV,
};

struct payload_mm_authvar_bundle_facts {
	bool ready_to_boot;
	bool at_runtime;
	bool setup_mode;
	bool secure_boot;
	bool vendor_keys;
};

struct payload_mm_authvar_bundle_snapshot {
	const struct payload_mm_authvar_policy_request *request;
	const struct payload_mm_authvar_authority_decision *decision;
	const struct payload_mm_authvar_store_index *index;
	void *certdb_workspace;
	size_t certdb_workspace_size;
	struct payload_mm_authvar_bundle_facts facts;
};

struct payload_mm_authvar_bundle_mutation {
	enum payload_mm_authvar_bundle_role role;
	struct payload_mm_authvar_policy_mutation mutation;
	uint8_t vendor_guid[16];
	const void *name;
	size_t name_size;
	const void *data;
	size_t data_size;
};

struct payload_mm_authvar_bundle_plan {
	enum payload_mm_authvar_authority_outcome outcome;
	uint8_t mutation_count;
	uint8_t volatile_modes;
	struct payload_mm_authvar_bundle_mutation mutations[
		PAYLOAD_MM_AUTHVAR_BUNDLE_MAX_MUTATIONS];
	enum payload_mm_authvar_certdb_operation certdb_operation;
	size_t private_binding_size;
	uint8_t private_binding[PAYLOAD_MM_MAX_DIGEST_SIZE];
};

/*
 * Expand one final authority outcome into a fixed-role atomic commit plan.
 * The output is a description only: it writes no media, installs no provider
 * and publishes no endpoint. All mutations and the volatile projection must
 * be committed together before success becomes observable. Target name/data
 * pointers borrow the authority snapshot lifetime. Fixed keys have static
 * lifetime. A CERTDB mutation borrows certdb_workspace through candidate
 * construction; that protected workspace must cover index.maximum_data_size.
 * The codec leaves it untouched on failure and fills the complete replacement
 * on success, even if a later invariant rejects the plan.
 */
enum payload_mm_verify_status payload_mm_authvar_bundle_plan(
	const struct payload_mm_authvar_bundle_snapshot *snapshot,
	struct payload_mm_authvar_bundle_plan *plan);
bool payload_mm_authvar_bundle_key_reserved(const uint8_t vendor_guid[16],
	const void *name, size_t name_size);

#endif
