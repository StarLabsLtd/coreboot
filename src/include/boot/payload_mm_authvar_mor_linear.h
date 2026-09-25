/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_LINEAR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_LINEAR_H

#include <boot/payload_mm_authvar_mor_clear_executor.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION 1U

enum payload_mm_authvar_mor_linear_boot_kind {
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COLD_BOOT = 1,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_S3_RESUME = 2,
};

enum payload_mm_authvar_mor_linear_phase {
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_EMPTY,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_RESERVED,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COMPLETE,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CLOSED,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILED,
};

enum payload_mm_authvar_mor_linear_failure {
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_NONE,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PROVIDER,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLASSIFICATION,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PROBE,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLOSE,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_RESERVATION,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_PHASE,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_BINDING,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_CLEAR,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_FAILURE_COMMIT,
};

enum payload_mm_authvar_mor_linear_result {
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_CONTINUE,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_WAIT_FOR_BOOTMEM,
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_HALT,
};

struct payload_mm_authvar_mor_linear_boot {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint32_t kind;
	uint32_t reserved;
} __aligned(8);

struct payload_mm_authvar_mor_linear_ops {
	void *context;
	size_t context_size;
	enum cb_err (*classify_guard)(void *context,
		struct payload_mm_authvar_mor_linear_boot *boot);
	enum cb_err (*reservations_register)(void *context);
	enum cb_err (*resolve_binding)(void *context, uint64_t generation,
		struct payload_mm_authvar_mor_clear_plan *plan,
		struct payload_mm_authvar_mor_clear_executor_ops *executor);
	enum cb_err (*private_complete)(void *context,
		const struct payload_mm_authvar_mor_grant *grant);
	enum cb_err (*private_close)(void *context);
};

struct payload_mm_authvar_mor_linear_state {
	uint32_t revision;
	uint32_t size;
	uint32_t phase;
	uint32_t failure;
	uint64_t generation;
	struct payload_mm_authvar_mor_entry entry;
	uint32_t reserved;
	struct payload_mm_authvar_mor_linear_ops ops;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_executor_ops executor;
	struct payload_mm_authvar_mor_clear_transcript transcript;
	struct payload_mm_authvar_mor_grant grant;
} __aligned(8);

enum payload_mm_authvar_mor_linear_result
payload_mm_authvar_mor_linear_before_bootmem(
	struct payload_mm_authvar_mor_linear_state *state,
	const struct payload_mm_authvar_mor_linear_ops *ops,
	struct payload_mm_authvar_mor_linear_state *callback_snapshot);

enum payload_mm_authvar_mor_linear_result
payload_mm_authvar_mor_linear_after_bootmem(
	struct payload_mm_authvar_mor_linear_state *state,
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	struct payload_mm_authvar_mor_linear_state *callback_snapshot);

const char *payload_mm_authvar_mor_linear_failure_name(
	enum payload_mm_authvar_mor_linear_failure failure);

bool platform_payload_mm_authvar_mor_linear_ops(
	struct payload_mm_authvar_mor_linear_ops *ops);

#if ENV_TEST
void payload_mm_authvar_mor_linear_reset_test(void);
#endif

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_MOR_LINEAR_H */
