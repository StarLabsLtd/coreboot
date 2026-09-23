/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_POLICY_H
#define BOOT_PAYLOAD_MM_AUTHVAR_POLICY_H

#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>

#define PAYLOAD_MM_AUTHVAR_POLICY_REVISION 1U

/* Internal protected request, not a wire ABI. No caller timestamp or plan. */
struct payload_mm_authvar_policy_request {
	uint32_t operation;
	uint32_t attributes;
	uint8_t vendor_guid[16];
	const void *name;
	size_t name_size;
	const void *data;
	size_t data_size;
};

struct payload_mm_authvar_policy_view {
	const struct payload_mm_authvar_store_index *index;
	bool ready_to_boot;
	bool at_runtime;
};

enum payload_mm_authvar_mutation_kind {
	PAYLOAD_MM_AUTHVAR_MUTATION_WRITE = 1,
	PAYLOAD_MM_AUTHVAR_MUTATION_DELETE = 2,
};

struct payload_mm_authvar_policy_mutation {
	uint32_t kind;
	uint32_t attributes;
	uint32_t data_size;
	uint8_t timestamp[16];
};

/*
 * Called only after recovery and scanning, under the same exclusive media
 * session as execution. All input is immutable protected storage and expires
 * on return. The provider must not retain pointers or call any media/service
 * API. It may write only mutation and data[0..data_capacity). It cannot change
 * the request key, select a destination, supply a plan, or acquire ownership.
 * Forbidden nested transaction calls return DEVICE_ERROR without completing
 * their result; they also make the outer transaction fail closed.
 * SUCCESS authorizes exactly this canonical mutation. Every other status
 * discards the output. This contract does not itself implement authentication,
 * trust anchors, Auth2 parsing, or multi-variable atomicity.
 */
typedef uint64_t payload_mm_authvar_policy_authorize_fn(
	const struct payload_mm_authvar_policy_request *request,
	const struct payload_mm_authvar_policy_view *view,
	struct payload_mm_authvar_policy_mutation *mutation,
	void *data, size_t data_capacity);

struct payload_mm_authvar_policy_provider {
	uint32_t revision;
	uint32_t size;
	payload_mm_authvar_policy_authorize_fn *authorize;
};

struct payload_mm_authvar_policy_result {
	uint64_t status;
	uint32_t completion;
	uint32_t reserved;
};

/* One attempt, after executor installation; descriptor and code must be SMRAM. */
enum cb_err payload_mm_authvar_policy_install(
	const struct payload_mm_authvar_policy_provider *provider);

/*
 * SET and monotone READY_TO_BOOT/ENTER_RUNTIME only. Other operations return
 * UNSUPPORTED. No endpoint, dispatch route, or production provider is supplied.
 * Request, result, name, and data must be mutually disjoint protected spans,
 * outside executor/media private storage. An alias/invalid-span rejection or
 * unsafe or aliasing output and forbidden provider reentry are untouched.
 * A pre-validation executor-seal failure completes a separately valid result
 * with DEVICE_ERROR. Otherwise
 * the result is scrubbed and completed on every exit.
 */
uint64_t payload_mm_authvar_policy_transaction(
	const struct payload_mm_authvar_policy_request *request,
	struct payload_mm_authvar_policy_result *result);

#endif
