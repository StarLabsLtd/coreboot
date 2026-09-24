/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_H

#include <boot/payload_mm_authvar_service.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_MOR_KEY_SIZE 8U
#define PAYLOAD_MM_AUTHVAR_MOR_MAX_MUTATIONS 2U

/*
 * State, snapshots, retained SET input, and plans are protected SMM objects.
 * Platform support, the boot-entry Control value, and the dirty fact come only
 * from trusted coreboot lifecycle code, never an endpoint field, store
 * existence, or build option. READY facts are read under the commit's lease.
 *
 * Planners do not access media. Retain the exact disjoint input and plan until
 * mutations commit atomically, read back and rescan, and the lease ends. Only
 * then pass durable_commit=true. Ambiguous mutation or lease failure poisons
 * the service until reset. Scrub retained inputs, plans, and keys afterwards.
 */

enum payload_mm_authvar_mor_variable {
	PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE,
	PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL,
	PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK,
};

enum payload_mm_authvar_mor_lock_state {
	PAYLOAD_MM_AUTHVAR_MOR_UNLOCKED,
	PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITHOUT_KEY,
	PAYLOAD_MM_AUTHVAR_MOR_LOCKED_WITH_KEY,
};

enum payload_mm_authvar_mor_init_phase {
	PAYLOAD_MM_AUTHVAR_MOR_INIT_END_OF_DXE,
	PAYLOAD_MM_AUTHVAR_MOR_INIT_READY_TO_BOOT_FALLBACK,
};

enum payload_mm_authvar_mor_mutation_kind {
	PAYLOAD_MM_AUTHVAR_MOR_MUTATION_NONE,
	PAYLOAD_MM_AUTHVAR_MOR_MUTATION_WRITE,
	PAYLOAD_MM_AUTHVAR_MOR_MUTATION_DELETE,
};

struct payload_mm_authvar_mor_state {
	uint64_t generation;
	bool initialized;
	bool supported;
	bool entry_clear_pending;
	bool control_dirty;
	bool ready_complete;
	enum payload_mm_authvar_mor_lock_state lock_state;
	uint8_t key[PAYLOAD_MM_AUTHVAR_MOR_KEY_SIZE];
};

struct payload_mm_authvar_mor_boot_snapshot {
	enum payload_mm_authvar_mor_init_phase phase;
	bool trusted_platform_support;
	bool entry_control_present;
	bool control_present;
	bool lock_present;
	bool control_dirty_since_entry;
	uint32_t control_attributes;
	uint32_t lock_attributes;
	size_t control_size;
	size_t lock_size;
	uint8_t control_value;
	uint8_t entry_control_value;
	uint8_t lock_value;
};

enum payload_mm_authvar_mor_transition {
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_NONE,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_INIT_SUPPORTED,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_INIT_UNSUPPORTED,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_CONTROL_WRITE,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_LOCK_WRITE,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_LOCK_KEY,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_UNLOCK_KEY,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_KEY_MISMATCH,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_NOOP,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_BLOCKED,
	PAYLOAD_MM_AUTHVAR_MOR_TRANSITION_READY_CLEAR,
};

struct payload_mm_authvar_mor_request {
	uint8_t vendor_guid[16];
	const void *name;
	size_t name_size;
	uint32_t attributes;
	const void *data;
	size_t data_size;
};

struct payload_mm_authvar_mor_mutation {
	enum payload_mm_authvar_mor_mutation_kind kind;
	enum payload_mm_authvar_mor_variable variable;
	uint32_t attributes;
	uint8_t value;
};

struct payload_mm_authvar_mor_plan {
	bool matched;
	bool pass_to_store;
	bool requires_durable_commit;
	bool observed_control_valid;
	uint8_t observed_control_value;
	enum payload_mm_authvar_mor_transition transition;
	uint64_t status;
	struct payload_mm_authvar_mor_mutation
		mutations[PAYLOAD_MM_AUTHVAR_MOR_MAX_MUTATIONS];
	size_t mutation_count;
	uint8_t transition_key[PAYLOAD_MM_AUTHVAR_MOR_KEY_SIZE];
	struct payload_mm_authvar_mor_state source_state;
	struct payload_mm_authvar_mor_state projected_state;
};

enum payload_mm_authvar_mor_input_kind {
	PAYLOAD_MM_AUTHVAR_MOR_INPUT_INIT,
	PAYLOAD_MM_AUTHVAR_MOR_INPUT_SET,
	PAYLOAD_MM_AUTHVAR_MOR_INPUT_READY_TO_BOOT,
};

struct payload_mm_authvar_mor_ready_snapshot {
	bool control_present;
	uint32_t control_attributes;
	size_t control_size;
	uint8_t control_value;
};

struct payload_mm_authvar_mor_finalize_input {
	enum payload_mm_authvar_mor_input_kind kind;
	union {
		struct payload_mm_authvar_mor_boot_snapshot init;
		const struct payload_mm_authvar_mor_request *set;
		struct payload_mm_authvar_mor_ready_snapshot ready;
	};
};

enum payload_mm_authvar_mor_finalize_result {
	/* Rejected/stale plan, or ordinary mutation that did not become durable. */
	PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_REJECTED,
	/* The regenerated projection was published. */
	PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED,
	/* Public Lock=1 failed, but the mismatched volatile key was destroyed. */
	PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED_KEY_DESTROYED,
	/* RTB clear failed after this boot was terminalized; durable bit 0 remains. */
	PAYLOAD_MM_AUTHVAR_MOR_FINALIZE_PUBLISHED_READY_CLEAR_FAILED,
};

enum payload_mm_authvar_mor_variable payload_mm_authvar_mor_classify(
	const uint8_t vendor_guid[16], const void *name, size_t name_size);

uint64_t payload_mm_authvar_mor_init_plan(
	const struct payload_mm_authvar_mor_state *state,
	const struct payload_mm_authvar_mor_boot_snapshot *snapshot,
	struct payload_mm_authvar_mor_plan *plan);

uint64_t payload_mm_authvar_mor_set_plan(
	const struct payload_mm_authvar_mor_state *state,
	const struct payload_mm_authvar_mor_request *request,
	struct payload_mm_authvar_mor_plan *plan);

uint64_t payload_mm_authvar_mor_ready_to_boot_plan(
	const struct payload_mm_authvar_mor_state *state, bool control_present,
	uint32_t control_attributes, size_t control_size, uint8_t control_value,
	struct payload_mm_authvar_mor_plan *plan);

enum payload_mm_authvar_mor_finalize_result payload_mm_authvar_mor_finalize(
	struct payload_mm_authvar_mor_state *state,
	const struct payload_mm_authvar_mor_finalize_input *input,
	const struct payload_mm_authvar_mor_plan *plan, bool durable_commit);

#endif
