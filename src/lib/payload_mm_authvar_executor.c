/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_executor.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_RECOVERY)
#include <boot/payload_mm_authvar_default_store.h>
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
#include <boot/payload_mm_authvar_candidate.h>
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT) || \
	CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
#include <payload_mm_cms.h>
#endif
#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_media.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
#include <boot/payload_mm_authvar_mor_grant.h>
#include <boot/payload_mm_authvar_mor_identity.h>
#endif
#include <boot/payload_mm_authvar_policy.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
#include <boot/payload_mm_authvar_view.h>
#endif
#include <boot/payload_mm_authvar_writer.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_authvar_recovery.h"
#include "payload_mm_authvar_set_preflight.h"
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
#include "payload_mm_authvar_coordinator.h"
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT) || \
	CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
#include "payload_mm_crypto/crypto.h"
#endif

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable executor must only be built in SMM"
#endif

#define EXECUTOR_ALIGNMENT ((size_t)__BIGGEST_ALIGNMENT__)
#define EXECUTOR_TRANSFER_SIZE 4096U
#define EXECUTOR_RECOVERY_LIMIT 16U

#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
struct executor_recovery_plan {
	uint64_t generation;
	uint64_t token;
	uint32_t count;
	uint32_t reserved;
	struct payload_mm_authvar_ftw_plan steps[EXECUTOR_RECOVERY_LIMIT];
};
#endif

struct executor_session {
	struct payload_mm_authvar_contract contract;
	struct payload_mm_authvar_ftw_plan ftw;
	struct payload_mm_authvar_ftw_plan previous_ftw;
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan write;
	struct payload_mm_authvar_record_source source;
	struct payload_mm_authvar_write_policy policy;
	struct payload_mm_authvar_policy_request request;
#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	struct payload_mm_authvar_policy_view view;
#endif
	struct payload_mm_authvar_read_result read_result;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	struct payload_mm_authvar_candidate_result candidate_result;
	struct payload_mm_authvar_candidate_binding candidate_binding;
	uint8_t candidate_image_digest[PAYLOAD_MM_SHA256_SIZE];
	uint32_t candidate_size;
	uint32_t candidate_phase;
	uint8_t staged_volatile_modes;
#endif
	uint64_t generation;
	uint64_t token;
	uint32_t read_name_capacity;
	uint32_t read_data_capacity;
	uint32_t recovery_count;
	bool have_previous_ftw;
	bool invariant_failure;
	bool at_runtime;
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
	struct executor_recovery_plan recovery;
	struct executor_recovery_plan recovery_sealed;
	bool recovery_plan_active;
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	struct payload_mm_authvar_mor_grant mor_grant;
	struct payload_mm_authvar_write_plan mor_write_sealed;
	struct payload_mm_authvar_reclaim_plan mor_reclaim_sealed;
	bool mor_plan_sealed;
	bool mor_clear_active;
	bool mor_write_active;
#endif
};

struct executor_policy {
	void *arena;
	size_t arena_size;
	struct payload_mm_authvar_executor_limits limits;
	size_t snapshot_offset;
	size_t candidate_offset;
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
	size_t recovery_image_seal_offset;
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	size_t mor_record_seal_offset;
	size_t mor_copies_seal_offset;
#endif
	size_t entries_offset;
	size_t candidate_entries_offset;
	size_t copies_offset;
	size_t record_offset;
	size_t name_offset;
	size_t data_offset;
	size_t transfer_offset;
	size_t session_offset;
#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	size_t provider_seal_offset;
	size_t mutation_offset;
#endif
	size_t mutation_data_offset;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	size_t coordinator_context_offset;
#endif
	size_t required_size;
#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	struct payload_mm_authvar_policy_provider provider;
#endif
};

static struct {
	struct executor_policy policy;
	struct executor_policy sealed;
	uint32_t install_attempted;
#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	uint32_t provider_install_attempted;
#endif
	uint32_t busy;
	uint32_t provider_active;
	uint32_t provider_violation;
	uint64_t owner_generation;
	uint64_t owner_token;
	uint64_t sealed_owner_generation;
	uint64_t sealed_owner_token;
	bool installed;
	bool ready_to_boot;
	bool at_runtime;
	bool sealed_ready_to_boot;
	bool sealed_at_runtime;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	uint8_t volatile_modes;
	uint8_t sealed_volatile_modes;
	bool volatile_modes_valid;
	bool sealed_volatile_modes_valid;
	bool modes_need_reconcile;
	bool sealed_modes_need_reconcile;
#endif
} executor;

static bool owner_equal(const struct executor_session *state)
{
	return executor.owner_generation && executor.owner_token &&
		executor.owner_generation == executor.sealed_owner_generation &&
		executor.owner_token == executor.sealed_owner_token &&
		state->generation == executor.sealed_owner_generation &&
		state->token == executor.sealed_owner_token;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
static bool owner_released(void)
{
	return !executor.owner_generation && !executor.owner_token &&
		!executor.sealed_owner_generation && !executor.sealed_owner_token;
}
#endif

static bool provider_reentry(void)
{
	if (!__atomic_load_n(&executor.provider_active, __ATOMIC_ACQUIRE))
		return false;
	__atomic_store_n(&executor.provider_violation, 1, __ATOMIC_RELEASE);
	return true;
}

static bool add_size(size_t left, size_t right, size_t *result)
{
	if (right > SIZE_MAX - left)
		return false;
	*result = left + right;
	return true;
}

static bool multiply_size(size_t left, size_t right, size_t *result)
{
	if (left && right > SIZE_MAX / left)
		return false;
	*result = left * right;
	return true;
}

static bool align_size(size_t value, size_t *result)
{
	if (value > SIZE_MAX - (EXECUTOR_ALIGNMENT - 1U))
		return false;
	*result = (value + EXECUTOR_ALIGNMENT - 1U) &
		~(size_t)(EXECUTOR_ALIGNMENT - 1U);
	return true;
}

static bool add_area(size_t *cursor, size_t size, size_t *offset)
{
	size_t aligned;
	size_t end;

	if (!align_size(*cursor, &aligned) || !add_size(aligned, size, &end))
		return false;
	*offset = aligned;
	*cursor = end;
	return true;
}

static bool policy_equal(void)
{
	return !memcmp(&executor.policy, &executor.sealed,
		sizeof(executor.policy)) &&
		executor.ready_to_boot == executor.sealed_ready_to_boot &&
		executor.at_runtime == executor.sealed_at_runtime
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		&& executor.volatile_modes == executor.sealed_volatile_modes &&
		executor.volatile_modes_valid == executor.sealed_volatile_modes_valid &&
		executor.modes_need_reconcile ==
			executor.sealed_modes_need_reconcile
#endif
		;
}

static bool limits_valid(
	const struct payload_mm_authvar_executor_limits *limits)
{
	return limits->maximum_store_size >= PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE &&
		limits->maximum_name_size >= 4U &&
		!(limits->maximum_name_size & 1U) && limits->maximum_data_size &&
		limits->maximum_record_size >= PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE &&
		limits->maximum_record_size <= limits->maximum_store_size &&
		limits->maximum_name_size <= limits->maximum_record_size &&
		limits->maximum_data_size <= limits->maximum_record_size &&
		limits->maximum_records;
}

static bool layout_build(struct executor_policy *policy)
{
	size_t cursor = 0;
	size_t entries_size;
	size_t copies_size;

	if (!multiply_size(policy->limits.maximum_records,
		sizeof(struct payload_mm_authvar_store_entry), &entries_size) ||
	    !multiply_size(policy->limits.maximum_records,
		sizeof(struct payload_mm_authvar_reclaim_copy), &copies_size) ||
	    !add_area(&cursor, policy->limits.maximum_store_size,
		&policy->snapshot_offset) ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT) || \
	CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER) || \
	CONFIG(PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_RECOVERY)
	    !add_area(&cursor, policy->limits.maximum_store_size,
		&policy->candidate_offset) ||
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
	    !add_area(&cursor, policy->limits.maximum_store_size,
		&policy->recovery_image_seal_offset) ||
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	    !add_area(&cursor, policy->limits.maximum_record_size,
		&policy->mor_record_seal_offset) ||
	    !add_area(&cursor, copies_size, &policy->mor_copies_seal_offset) ||
#endif
	    !add_area(&cursor, entries_size, &policy->entries_offset) ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	    !add_area(&cursor, entries_size, &policy->candidate_entries_offset) ||
#endif
	    !add_area(&cursor, copies_size, &policy->copies_offset) ||
	    !add_area(&cursor, policy->limits.maximum_record_size,
		&policy->record_offset) ||
	    !add_area(&cursor, policy->limits.maximum_name_size,
		&policy->name_offset) ||
	    !add_area(&cursor, policy->limits.maximum_data_size,
		&policy->data_offset) ||
	    !add_area(&cursor, EXECUTOR_TRANSFER_SIZE, &policy->transfer_offset) ||
	    !add_area(&cursor, sizeof(struct executor_session),
		&policy->session_offset) || !align_size(cursor, &cursor))
		return false;
#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	/* Byte-exact seal of all provider inputs and executor control, not a CRC. */
	policy->provider_seal_offset = cursor;
	if (!add_size(cursor, cursor, &cursor) ||
	    !add_area(&cursor, sizeof(struct payload_mm_authvar_policy_mutation),
		&policy->mutation_offset))
		return false;
#endif
	if (!add_area(&cursor, policy->limits.maximum_data_size,
		&policy->mutation_data_offset) ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	    !add_area(&cursor, PAYLOAD_MM_AUTHVAR_COORDINATOR_CONTEXT_MAX,
		&policy->coordinator_context_offset) ||
#endif
	    !align_size(cursor, &policy->required_size))
		return false;
	return policy->required_size <= policy->arena_size;
}

static void *arena_at(size_t offset)
{
	return (uint8_t *)executor.sealed.arena + offset;
}

static bool external_protected_span(const void *buffer, size_t size)
{
	return buffer && size && payload_mm_authvar_smram_buffer(buffer, size) &&
		payload_mm_authvar_media_buffer_disjoint(buffer, size) &&
		!payload_mm_authvar_buffers_overlap(buffer, size, &executor,
			sizeof(executor)) &&
		!payload_mm_authvar_buffers_overlap(buffer, size,
			executor.sealed.arena, executor.sealed.arena_size);
}

static uint8_t *snapshot(void)
{
	return arena_at(executor.sealed.snapshot_offset);
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
static bool recovery_image_unchanged(const struct executor_session *state);

static bool recovery_read_valid(struct executor_session *state)
{
	if (state->recovery_plan_active && !recovery_image_unchanged(state)) {
		state->invariant_failure = true;
		return false;
	}
	return true;
}
#endif

static uint8_t *transfer(void)
{
	return arena_at(executor.sealed.transfer_offset);
}

static bool ftw_store_base(struct executor_session *state, uint32_t *base)
{
	if (state->ftw.geometry.variable_offset ||
	    state->ftw.fv_header_size > state->contract.store_size ||
	    state->ftw.variable_store_size >
		state->contract.store_size - state->ftw.fv_header_size) {
		state->invariant_failure = true;
		return false;
	}
	*base = state->ftw.fv_header_size;
	return true;
}

static struct executor_session *session(void)
{
	return arena_at(executor.sealed.session_offset);
}

struct executor_control_seal {
	struct payload_mm_authvar_contract contract;
	struct payload_mm_authvar_ftw_plan ftw;
	struct payload_mm_authvar_ftw_plan previous_ftw;
	struct payload_mm_authvar_write_plan write;
	struct payload_mm_authvar_write_policy policy;
	struct payload_mm_authvar_policy_request request;
#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	struct payload_mm_authvar_policy_view view;
#endif
	struct payload_mm_authvar_read_result read_result;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	struct payload_mm_authvar_candidate_result candidate_result;
	struct payload_mm_authvar_candidate_binding candidate_binding;
	uint8_t candidate_image_digest[PAYLOAD_MM_SHA256_SIZE];
	uint32_t candidate_size;
	uint32_t candidate_phase;
	uint8_t staged_volatile_modes;
	uint8_t candidate_padding[3];
#endif
	uint8_t source_guid[16];
	uint8_t source_timestamp[16];
	uint64_t generation;
	uint64_t token;
	uint32_t source_name_size;
	uint32_t source_data_size;
	uint32_t source_attributes;
	uint32_t reclaim_action;
	uint32_t reclaim_record_size;
	uint32_t reclaim_destination_offset;
	uint32_t reclaim_compacted_used_size;
	uint32_t reclaim_reclaimable_size;
	uint32_t reclaim_copy_count;
	uint32_t reclaim_copy_capacity;
	uint32_t recovery_count;
	uint32_t read_name_capacity;
	uint32_t read_data_capacity;
	uint32_t index_store_size;
	uint32_t index_used_size;
	uint32_t index_dirty_tail_offset;
	uint32_t index_record_count;
	uint32_t index_entry_count;
	uint32_t index_entry_capacity;
	uint32_t index_maximum_name_size;
	uint32_t index_maximum_data_size;
	uint32_t index_maximum_records;
	uint32_t index_store_offset;
	uint8_t have_previous_ftw;
	uint8_t invariant_failure;
	uint8_t at_runtime;
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
	uint8_t recovery_plan_active;
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	struct payload_mm_authvar_mor_grant mor_grant;
	struct payload_mm_authvar_write_plan mor_write_sealed;
	struct payload_mm_authvar_reclaim_plan mor_reclaim_sealed;
	uint8_t mor_record_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t mor_record_seal_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t mor_copies_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t mor_copies_seal_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t mor_canonical_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t mor_canonical_seal_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t mor_plan_sealed;
	uint8_t mor_clear_active;
	uint8_t mor_write_active;
#endif
};

#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
static bool mor_reclaim_image_valid(const struct executor_session *state)
{
	const struct payload_mm_authvar_fv_geometry *geometry =
		&state->ftw.geometry;
	const uint8_t *canonical = arena_at(executor.sealed.candidate_offset);
	const uint8_t *image = snapshot() + geometry->spare_offset;
	const uint8_t *record = arena_at(executor.sealed.mor_record_seal_offset);
	uint32_t store_base;
	uint32_t destination = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	size_t prefix;

	store_base = state->ftw.fv_header_size;
	if (state->ftw.geometry.variable_offset ||
	    state->ftw.fv_header_size > state->contract.store_size ||
	    state->ftw.variable_store_size >
		state->contract.store_size - state->ftw.fv_header_size ||
	    geometry->variable_size > geometry->spare_size ||
	    state->write.action != PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM ||
	    state->reclaim.action != PAYLOAD_MM_AUTHVAR_SPACE_RECLAIM ||
	    state->reclaim.copy_count > state->reclaim.copy_capacity ||
	    state->reclaim.copy_count > executor.sealed.limits.maximum_records ||
	    store_base > geometry->variable_size ||
	    PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE >
		geometry->variable_size - store_base)
		return false;
	prefix = (size_t)store_base + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	if (memcmp(image, canonical, prefix))
		return false;
	for (uint32_t i = 0; i < state->reclaim.copy_count; i++) {
		const struct payload_mm_authvar_reclaim_copy *copy =
			&state->reclaim.copies[i];
		uint64_t source_end = (uint64_t)copy->source_offset + copy->size;
		uint64_t destination_end =
			(uint64_t)copy->destination_offset + copy->size;
		const uint8_t *source;
		const uint8_t *built;

		if (copy->destination_offset != destination || copy->size < 3U ||
		    source_end > state->ftw.variable_store_size ||
		    destination_end > state->ftw.variable_store_size)
			return false;
		source = canonical + store_base + copy->source_offset;
		built = image + store_base + copy->destination_offset;
		if (memcmp(source, built, 2U) ||
		    built[2] != (copy->promote_transition ?
			PAYLOAD_MM_AUTHVAR_STATE_ADDED : source[2]) ||
		    memcmp(source + 3U, built + 3U, copy->size - 3U))
			return false;
		destination += copy->size;
	}
	if (state->write.record_offset != destination ||
	    state->write.record_size < 3U ||
	    (uint64_t)destination + state->write.record_size >
		state->ftw.variable_store_size)
		return false;
	{
		const uint8_t *built = image + store_base + destination;

		if (memcmp(record, built, 2U) ||
		    built[2] != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
		    memcmp(record + 3U, built + 3U,
			state->write.record_size - 3U))
			return false;
	}
	destination += state->write.record_size;
	for (size_t offset = (size_t)store_base + destination;
	     offset < geometry->variable_size; offset++)
		if (image[offset] != 0xffU)
			return false;
	return true;
}

static bool mor_clear_state_valid(const struct executor_session *state)
{
	size_t copies_size;

	if (!state->mor_plan_sealed)
		return !state->mor_clear_active && !state->mor_write_active;
	if (state->write.record_size >
		executor.sealed.limits.maximum_record_size ||
	    state->reclaim.copies != arena_at(executor.sealed.copies_offset) ||
	    state->reclaim.copy_capacity !=
		executor.sealed.limits.maximum_records ||
	    state->reclaim.copy_count > state->reclaim.copy_capacity ||
	    memcmp(&state->write, &state->mor_write_sealed,
		sizeof(state->write)) ||
	    memcmp(&state->reclaim, &state->mor_reclaim_sealed,
		sizeof(state->reclaim)) ||
	    state->source.name != arena_at(executor.sealed.name_offset) ||
	    state->source.data != arena_at(executor.sealed.data_offset) ||
	    state->source.name_size !=
		payload_mm_authvar_mor_control_identity.name_size ||
	    state->source.data_size != 1U ||
	    state->source.attributes != PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES ||
	    memcmp(state->source.vendor_guid,
		payload_mm_authvar_mor_control_identity.vendor_guid,
		sizeof(state->source.vendor_guid)) ||
	    memcmp(state->source.name,
		payload_mm_authvar_mor_control_identity.name,
		state->source.name_size) ||
	    (*(const uint8_t *)state->source.data & 1U) ||
	    memcmp(arena_at(executor.sealed.record_offset),
		arena_at(executor.sealed.mor_record_seal_offset),
		state->write.record_size) ||
	    !multiply_size(state->reclaim.copy_count,
		sizeof(state->reclaim.copies[0]), &copies_size) ||
	    memcmp(state->reclaim.copies,
		arena_at(executor.sealed.mor_copies_seal_offset), copies_size) ||
	    !recovery_image_unchanged(state))
		return false;
	(void)copies_size;
	return !state->mor_clear_active || !state->mor_write_active ||
		state->write.action != PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM ||
		mor_reclaim_image_valid(state);
}

static bool mor_digest(const void *data, size_t size,
	uint8_t digest[PAYLOAD_MM_SHA256_SIZE])
{
	return payload_mm_sha256(data, size, digest) == PAYLOAD_MM_VERIFY_OK;
}

static bool mor_record_snapshot_digests(const struct executor_session *state,
	struct executor_control_seal *seal)
{
	return mor_digest(arena_at(executor.sealed.record_offset),
			state->write.record_size, seal->mor_record_digest) &&
		mor_digest(arena_at(executor.sealed.mor_record_seal_offset),
			state->write.record_size, seal->mor_record_seal_digest);
}

static bool mor_copies_snapshot_digests(const struct executor_session *state,
	struct executor_control_seal *seal, size_t copies_size)
{
	return mor_digest(state->reclaim.copies, copies_size,
			seal->mor_copies_digest) &&
		mor_digest(arena_at(executor.sealed.mor_copies_seal_offset),
			copies_size, seal->mor_copies_seal_digest);
}

static bool mor_canonical_snapshot_digests(
	const struct executor_session *state, struct executor_control_seal *seal)
{
	return mor_digest(arena_at(executor.sealed.candidate_offset),
			state->contract.store_size, seal->mor_canonical_digest) &&
		mor_digest(arena_at(executor.sealed.recovery_image_seal_offset),
			state->contract.store_size, seal->mor_canonical_seal_digest);
}

static bool mor_snapshot_digests(const struct executor_session *state,
	struct executor_control_seal *seal)
{
	size_t copies_size;

	if (!state->mor_plan_sealed)
		return true;
	if (!mor_clear_state_valid(state) ||
	    !multiply_size(state->reclaim.copy_count,
		sizeof(state->reclaim.copies[0]), &copies_size))
		return false;
	return mor_record_snapshot_digests(state, seal) &&
		mor_copies_snapshot_digests(state, seal, copies_size) &&
		mor_canonical_snapshot_digests(state, seal);
}
#endif

static bool control_snapshot(const struct executor_session *state,
	struct executor_control_seal *seal, bool omit_session_id)
{
	uintptr_t arena_base = (uintptr_t)executor.sealed.arena;
	uintptr_t store_address = (uintptr_t)state->index.store;

	memset(seal, 0, sizeof(*seal));
	seal->contract = state->contract;
	seal->ftw = state->ftw;
	seal->previous_ftw = state->previous_ftw;
	seal->write = state->write;
	seal->policy = state->policy;
	seal->request = state->request;
#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	seal->view = state->view;
#endif
	seal->read_result = state->read_result;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	seal->candidate_result = state->candidate_result;
	seal->candidate_binding = state->candidate_binding;
	memcpy(seal->candidate_image_digest, state->candidate_image_digest,
		sizeof(seal->candidate_image_digest));
	seal->candidate_size = state->candidate_size;
	seal->candidate_phase = state->candidate_phase;
	seal->staged_volatile_modes = state->staged_volatile_modes;
#endif
	memcpy(seal->source_guid, state->source.vendor_guid,
		sizeof(seal->source_guid));
	memcpy(seal->source_timestamp, state->source.timestamp,
		sizeof(seal->source_timestamp));
	if (!omit_session_id) {
		seal->generation = state->generation;
		seal->token = state->token;
	}
	if (state->source.name_size > UINT32_MAX ||
	    state->source.data_size > UINT32_MAX)
		return false;
	seal->source_name_size = (uint32_t)state->source.name_size;
	seal->source_data_size = (uint32_t)state->source.data_size;
	seal->source_attributes = state->source.attributes;
	seal->reclaim_action = state->reclaim.action;
	seal->reclaim_record_size = state->reclaim.record_size;
	seal->reclaim_destination_offset = state->reclaim.destination_offset;
	seal->reclaim_compacted_used_size = state->reclaim.compacted_used_size;
	seal->reclaim_reclaimable_size = state->reclaim.reclaimable_size;
	seal->reclaim_copy_count = state->reclaim.copy_count;
	seal->reclaim_copy_capacity = state->reclaim.copy_capacity;
	seal->recovery_count = state->recovery_count;
	seal->read_name_capacity = state->read_name_capacity;
	seal->read_data_capacity = state->read_data_capacity;
	seal->index_store_size = state->index.store_size;
	seal->index_used_size = state->index.used_size;
	seal->index_dirty_tail_offset = state->index.dirty_tail_offset;
	seal->index_record_count = state->index.record_count;
	seal->index_entry_count = state->index.entry_count;
	seal->index_entry_capacity = state->index.entry_capacity;
	seal->index_maximum_name_size = state->index.maximum_name_size;
	seal->index_maximum_data_size = state->index.maximum_data_size;
	seal->index_maximum_records = state->index.maximum_records;
	if (state->index.store) {
		if (store_address < arena_base ||
		    store_address - arena_base < executor.sealed.snapshot_offset ||
		    store_address - arena_base - executor.sealed.snapshot_offset >
			executor.sealed.limits.maximum_store_size ||
		    state->index.store_size >
			executor.sealed.limits.maximum_store_size -
			(store_address - arena_base -
			 executor.sealed.snapshot_offset) ||
		    store_address - arena_base > UINT32_MAX)
			return false;
		seal->index_store_offset = (uint32_t)(store_address - arena_base);
	} else {
		seal->index_store_offset = UINT32_MAX;
	}
	seal->have_previous_ftw = state->have_previous_ftw;
	seal->invariant_failure = state->invariant_failure;
	seal->at_runtime = state->at_runtime;
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
	seal->recovery_plan_active = state->recovery_plan_active;
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	seal->mor_grant = state->mor_grant;
	seal->mor_write_sealed = state->mor_write_sealed;
	seal->mor_reclaim_sealed = state->mor_reclaim_sealed;
	seal->mor_plan_sealed = state->mor_plan_sealed;
	seal->mor_clear_active = state->mor_clear_active;
	seal->mor_write_active = state->mor_write_active;
	if (!mor_snapshot_digests(state, seal))
		return false;
#endif
	return (!state->source.name_size ||
		state->source.name == arena_at(executor.sealed.name_offset)) &&
		state->source.name_size <= executor.sealed.limits.maximum_name_size &&
		(!state->source.data_size ||
		 state->source.data == arena_at(executor.sealed.data_offset)) &&
		state->source.data_size <= executor.sealed.limits.maximum_data_size &&
		(!state->reclaim.copies || state->reclaim.copies ==
		 arena_at(executor.sealed.copies_offset)) &&
		state->reclaim.copy_capacity <= executor.sealed.limits.maximum_records &&
		(!state->index.entries || state->index.entries ==
		 arena_at(executor.sealed.entries_offset)) &&
		state->index.entry_capacity <= executor.sealed.limits.maximum_records
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
		&& !memcmp(&state->recovery, &state->recovery_sealed,
			sizeof(state->recovery))
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
		&& mor_clear_state_valid(state)
#endif
		;
}

static bool control_unchanged(const struct executor_session *state,
	const struct executor_control_seal *before, bool omit_session_id)
{
	struct executor_control_seal after;

	return policy_equal() && control_snapshot(state, &after, omit_session_id) &&
		!memcmp(before, &after, sizeof(after));
}

static bool contract_allowed(const struct payload_mm_authvar_contract *contract,
	const struct payload_mm_authvar_executor_limits *limits)
{
	struct payload_mm_authvar_fv_geometry geometry;

	return payload_mm_authvar_contract_valid(contract) &&
		contract->store_size <= limits->maximum_store_size &&
		contract->store_size <= UINT32_MAX && contract->block_size &&
		contract->erase_size && contract->erase_size <= EXECUTOR_TRANSFER_SIZE &&
		contract->block_size % contract->erase_size == 0 &&
		payload_mm_authvar_fv_geometry(&geometry, contract->store_size,
			contract->block_size);
}

static bool maximum_record_fits(
	const struct payload_mm_authvar_executor_limits *limits)
{
	size_t size = PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;

	if (!add_size(size, limits->maximum_name_size, &size) ||
	    size > SIZE_MAX - 3U)
		return false;
	size = (size + 3U) & ~(size_t)3U;
	if (!add_size(size, limits->maximum_data_size, &size) ||
	    size > SIZE_MAX - 3U)
		return false;
	size = (size + 3U) & ~(size_t)3U;
	return size <= limits->maximum_record_size &&
		size <= limits->maximum_store_size;
}

enum cb_err payload_mm_authvar_executor_install(
	void *trusted_smram_arena, size_t arena_size,
	const struct payload_mm_authvar_executor_limits *limits)
{
	struct payload_mm_authvar_executor_limits copied_limits;
	struct payload_mm_authvar_contract contract;
	struct executor_policy policy = { 0 };
	uint32_t expected = 0;

	if (provider_reentry())
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&executor.install_attempted, &expected, 1,
		false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		return CB_ERR;
	if (!trusted_smram_arena || !arena_size || !limits ||
	    (uintptr_t)trusted_smram_arena % EXECUTOR_ALIGNMENT ||
	    !payload_mm_authvar_smram_buffer(&executor, sizeof(executor)) ||
	    !payload_mm_authvar_smram_buffer(trusted_smram_arena, arena_size) ||
	    !payload_mm_authvar_smram_buffer(limits, sizeof(*limits)) ||
	    !payload_mm_authvar_media_buffer_disjoint(trusted_smram_arena,
		arena_size) ||
	    !payload_mm_authvar_media_buffer_disjoint(limits, sizeof(*limits)) ||
	    payload_mm_authvar_buffers_overlap(trusted_smram_arena, arena_size,
		&executor, sizeof(executor)) ||
	    payload_mm_authvar_buffers_overlap(trusted_smram_arena, arena_size,
		limits, sizeof(*limits)) ||
	    payload_mm_authvar_buffers_overlap(limits, sizeof(*limits), &executor,
		sizeof(executor)))
		return CB_ERR;
	memcpy(&copied_limits, limits, sizeof(copied_limits));
	if (!limits_valid(&copied_limits) || !maximum_record_fits(&copied_limits) ||
	    memcmp(limits, &copied_limits, sizeof(copied_limits)) ||
	    !payload_mm_authvar_authority_snapshot(&contract) ||
	    !contract_allowed(&contract, &copied_limits))
		return CB_ERR;
	policy.arena = trusted_smram_arena;
	policy.arena_size = arena_size;
	policy.limits = copied_limits;
	if (!layout_build(&policy))
		return CB_ERR;
	memset(trusted_smram_arena, 0, policy.required_size);
	executor.policy = policy;
	executor.sealed = policy;
	if (!policy_equal())
		return CB_ERR;
	executor.installed = true;
	return CB_SUCCESS;
}

#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
enum cb_err payload_mm_authvar_policy_install(
	const struct payload_mm_authvar_policy_provider *provider)
{
	struct payload_mm_authvar_policy_provider copied;
	uint32_t expected = 0;
	enum cb_err result = CB_ERR;

	if (provider_reentry())
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&executor.provider_install_attempted,
		&expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		return CB_ERR;
	expected = 0;
	if (!__atomic_compare_exchange_n(&executor.busy, &expected, 1, false,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return CB_ERR;
	if (!executor.installed || !policy_equal() ||
	    !external_protected_span(provider, sizeof(*provider)))
		goto out;
	memcpy(&copied, provider, sizeof(copied));
	if (copied.revision != PAYLOAD_MM_AUTHVAR_POLICY_REVISION ||
	    copied.size != sizeof(copied) || !copied.authorize ||
	    !external_protected_span((const void *)(uintptr_t)copied.authorize, 1) ||
	    memcmp(&copied, provider, sizeof(copied)))
		goto out;
	executor.policy.provider = copied;
	executor.sealed.provider = copied;
	result = CB_SUCCESS;
out:
	__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);
	return result;
}
#endif

static enum payload_mm_authvar_media_result media_read(
	struct executor_session *state, uint32_t offset, void *buffer, size_t size)
{
	struct executor_control_seal before;
	bool sealed = control_snapshot(state, &before, false);
	enum payload_mm_authvar_media_result result;

	if (!sealed || !owner_equal(state)) {
		state->generation = executor.sealed_owner_generation;
		state->token = executor.sealed_owner_token;
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	result =
		payload_mm_authvar_media_read(executor.sealed_owner_generation,
			executor.sealed_owner_token, offset,
			buffer, size);

	if (!owner_equal(state) || !control_unchanged(state, &before, false)) {
		state->generation = before.generation;
		state->token = before.token;
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
	if (!recovery_read_valid(state))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
#endif
	return result;
}

static enum payload_mm_authvar_media_result media_program(
	struct executor_session *state, uint32_t offset, const void *buffer,
	size_t size)
{
	struct executor_control_seal before;
	bool sealed = control_snapshot(state, &before, false);
	enum payload_mm_authvar_media_result result;
	if (!sealed || !owner_equal(state)) {
		state->generation = executor.sealed_owner_generation;
		state->token = executor.sealed_owner_token;
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	result =
		payload_mm_authvar_media_program(executor.sealed_owner_generation,
			executor.sealed_owner_token,
			offset, buffer, size);

	if (!owner_equal(state) || !control_unchanged(state, &before, false)) {
		state->generation = before.generation;
		state->token = before.token;
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	return result;
}

static enum payload_mm_authvar_media_result media_erase(
	struct executor_session *state, uint32_t offset, size_t size)
{
	struct executor_control_seal before;
	bool sealed = control_snapshot(state, &before, false);
	enum payload_mm_authvar_media_result result;
	if (!sealed || !owner_equal(state)) {
		state->generation = executor.sealed_owner_generation;
		state->token = executor.sealed_owner_token;
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	result =
		payload_mm_authvar_media_erase(executor.sealed_owner_generation,
			executor.sealed_owner_token, offset,
			size);

	if (!owner_equal(state) || !control_unchanged(state, &before, false)) {
		state->generation = before.generation;
		state->token = before.token;
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	return result;
}

static enum payload_mm_authvar_media_result media_begin(
	struct executor_session *state)
{
	struct executor_control_seal before;
	bool sealed = control_snapshot(state, &before, true);
	enum payload_mm_authvar_media_result result;

	if (!sealed || !policy_equal()) {
		(void)payload_mm_authvar_media_fail_closed(0, 0);
		executor.installed = false;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	result = payload_mm_authvar_media_begin(&state->generation, &state->token);

	if (!sealed || !control_unchanged(state, &before, true)) {
		state->invariant_failure = true;
		if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
			executor.owner_generation = state->generation;
			executor.owner_token = state->token;
			executor.sealed_owner_generation = state->generation;
			executor.sealed_owner_token = state->token;
			(void)payload_mm_authvar_media_fail_closed(state->generation,
				state->token);
			(void)payload_mm_authvar_media_end(state->generation, state->token);
			executor.owner_generation = 0;
			executor.owner_token = 0;
			executor.sealed_owner_generation = 0;
			executor.sealed_owner_token = 0;
		} else {
			(void)payload_mm_authvar_media_fail_closed(0, 0);
		}
		executor.installed = false;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		executor.owner_generation = state->generation;
		executor.owner_token = state->token;
		executor.sealed_owner_generation = state->generation;
		executor.sealed_owner_token = state->token;
	}
	return result;
}

static enum payload_mm_authvar_media_result media_end(
	struct executor_session *state)
{
	struct executor_control_seal before;
	bool sealed = control_snapshot(state, &before, false);
	enum payload_mm_authvar_media_result result;
	uint64_t generation = executor.sealed_owner_generation;
	uint64_t token = executor.sealed_owner_token;

	if (!sealed || !owner_equal(state)) {
		(void)payload_mm_authvar_media_fail_closed(generation, token);
		result = payload_mm_authvar_media_end(generation, token);
		executor.installed = false;
		goto clear_owner;
	}
	result = payload_mm_authvar_media_end(generation, token);

	if (!owner_equal(state) || !control_unchanged(state, &before, false)) {
		state->generation = before.generation;
		state->token = before.token;
		executor.installed = false;
		(void)payload_mm_authvar_media_fail_closed(0, 0);
		result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}

clear_owner:
	executor.owner_generation = 0;
	executor.owner_token = 0;
	executor.sealed_owner_generation = 0;
	executor.sealed_owner_token = 0;
	return result;
}

static enum payload_mm_authvar_media_result snapshot_read(
	struct executor_session *state)
{
	uint32_t offset = 0;
	uint8_t *bytes = snapshot();

	while (offset < state->contract.store_size) {
		size_t size = state->contract.store_size - offset;
		enum payload_mm_authvar_media_result result;

		if (size > EXECUTOR_TRANSFER_SIZE)
			size = EXECUTOR_TRANSFER_SIZE;
		result = media_read(state, offset, bytes + offset, size);
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return result;
		offset += (uint32_t)size;
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result checked_program(
	struct executor_session *state, uint32_t offset, const uint8_t *bytes,
	size_t size)
{
	while (size) {
		size_t chunk = size > EXECUTOR_TRANSFER_SIZE ?
			EXECUTOR_TRANSFER_SIZE : size;
		enum payload_mm_authvar_media_result result =
			media_program(state, offset, bytes, chunk);

		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return result;
		offset += (uint32_t)chunk;
		bytes += chunk;
		size -= chunk;
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result verify_media(
	struct executor_session *state, uint32_t offset, const uint8_t *expected,
	size_t size)
{
	while (size) {
		size_t chunk = size > EXECUTOR_TRANSFER_SIZE ?
			EXECUTOR_TRANSFER_SIZE : size;
		enum payload_mm_authvar_media_result result =
			media_read(state, offset, transfer(), chunk);

		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return result;
		if (memcmp(transfer(), expected, chunk))
			state->invariant_failure = true;
		if (state->invariant_failure)
			return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		offset += (uint32_t)chunk;
		expected += chunk;
		size -= chunk;
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result program_body(
	struct executor_session *state, uint32_t offset, const uint8_t *bytes,
	size_t size, size_t marker_offset)
{
	enum payload_mm_authvar_media_result result;

	if (marker_offset >= size) {
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	result = checked_program(state, offset, bytes, marker_offset);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = checked_program(state, offset + (uint32_t)marker_offset + 1U,
			bytes + marker_offset + 1U, size - marker_offset - 1U);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, offset, bytes, marker_offset);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, offset + (uint32_t)marker_offset + 1U,
			bytes + marker_offset + 1U, size - marker_offset - 1U);
	return result;
}

static enum payload_mm_authvar_media_result verify_erased(
	struct executor_session *state, uint32_t offset, size_t size)
{
	while (size) {
		size_t chunk = size > EXECUTOR_TRANSFER_SIZE ?
			EXECUTOR_TRANSFER_SIZE : size;
		enum payload_mm_authvar_media_result result =
			media_read(state, offset, transfer(), chunk);

		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return result;
		for (size_t i = 0; i < chunk; i++)
			if (transfer()[i] != 0xffU) {
				state->invariant_failure = true;
				return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
			}
		offset += (uint32_t)chunk;
		size -= chunk;
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result erase_span(
	struct executor_session *state, uint32_t offset, uint32_t size)
{
	if (!size || offset % state->contract.erase_size ||
	    size % state->contract.erase_size) {
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	while (size) {
		enum payload_mm_authvar_media_result result =
			media_erase(state, offset, state->contract.erase_size);

		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return result;
		offset += state->contract.erase_size;
		size -= state->contract.erase_size;
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static uint32_t crc32(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_MAX;

	for (size_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320U &
				(uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

static void write_le32(uint8_t *p, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static void write_le64(uint8_t *p, uint64_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static void empty_workspace(uint8_t *workspace, uint32_t size)
{
	memset(workspace, 0xff, size);
	memcpy(workspace, payload_mm_authvar_ftw_working_block_guid,
		sizeof(payload_mm_authvar_ftw_working_block_guid));
	write_le64(workspace + 24U, size - PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE);
	write_le32(workspace + 16U, crc32(workspace, PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE));
}

static enum payload_mm_authvar_media_result marker(
	struct executor_session *state, uint32_t offset, uint8_t expected,
	uint8_t wanted)
{
	enum payload_mm_authvar_media_result result;
	uint8_t *byte = transfer();

	result = media_read(state, offset, byte, 1);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	if (*byte != expected || wanted != (expected & wanted)) {
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	*byte = wanted;
	return media_program(state, offset, byte, 1);
}

static enum payload_mm_authvar_media_result workspace_rebuild(
	struct executor_session *state, bool invalidate_working)
{
	const struct payload_mm_authvar_fv_geometry *geometry =
		&state->ftw.geometry;
	uint8_t *image = snapshot() + geometry->spare_offset;
	enum payload_mm_authvar_media_result result;

	empty_workspace(image, geometry->working_size);
	result = erase_span(state, geometry->spare_offset, geometry->spare_size);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = program_body(state, geometry->spare_offset, image,
		geometry->working_size, PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS &&
	    geometry->spare_size > geometry->working_size)
		result = verify_erased(state,
			geometry->spare_offset + geometry->working_size,
			geometry->spare_size - geometry->working_size);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = marker(state,
		geometry->spare_offset + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET,
		PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
		PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	if (invalidate_working) {
		result = marker(state,
			geometry->working_offset + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET,
			PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID,
			PAYLOAD_MM_AUTHVAR_FTW_WORK_INVALID);
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return result;
	}
	result = erase_span(state, geometry->working_offset, geometry->working_size);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = program_body(state, geometry->working_offset, image,
		geometry->working_size, PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = marker(state,
		geometry->working_offset + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET,
		PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
		PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	return erase_span(state, geometry->spare_offset, geometry->spare_size);
}

static enum payload_mm_authvar_media_result recover_abort(
	struct executor_session *state)
{
	uint32_t offset = state->ftw.geometry.working_offset +
		state->ftw.queue_offset;
	const uint8_t *header = snapshot() + offset;
	const uint8_t *record = header + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE;

	if (state->ftw.queue_entry_size != PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
	    PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE) {
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (header[0] == PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED &&
	    record[0] == PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED)
		return marker(state, offset, PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_ABORTED);
	if (header[0] == PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED &&
	    record[0] == PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED) {
		enum payload_mm_authvar_media_result result = erase_span(state,
			state->ftw.geometry.spare_offset,
			state->ftw.geometry.spare_size);

		if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			result = verify_erased(state, state->ftw.geometry.spare_offset,
				state->ftw.geometry.spare_size);
		return result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ?
			marker(state, offset,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE) : result;
	}
	state->invariant_failure = true;
	return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

static enum payload_mm_authvar_media_result replay_spare(
	struct executor_session *state)
{
	const struct payload_mm_authvar_fv_geometry *geometry =
		&state->ftw.geometry;
	uint32_t queue = geometry->working_offset + state->ftw.queue_offset;
	uint8_t *image = snapshot() + geometry->spare_offset;
	enum payload_mm_authvar_media_result result;

	result = verify_media(state, geometry->spare_offset, image,
		geometry->variable_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS &&
	    geometry->spare_size > geometry->variable_size)
		result = verify_erased(state,
			geometry->spare_offset + geometry->variable_size,
			geometry->spare_size - geometry->variable_size);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = erase_span(state, geometry->variable_offset, geometry->variable_size);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = checked_program(state, geometry->variable_offset, image,
		geometry->variable_size);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = verify_media(state, geometry->variable_offset, image,
		geometry->variable_size);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = marker(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
		PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = marker(state, queue,
		PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
		PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	return erase_span(state, geometry->spare_offset, geometry->spare_size);
}

static enum payload_mm_authvar_media_result restore_workspace(
	struct executor_session *state)
{
	const struct payload_mm_authvar_fv_geometry *geometry =
		&state->ftw.geometry;
	uint8_t *image = snapshot() + geometry->spare_offset;
	uint32_t queue = geometry->working_offset + state->ftw.queue_offset;
	enum payload_mm_authvar_media_result result;
	uint8_t committed_state =
		image[PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET];

	if (committed_state != PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID) {
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	image[PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET] =
		PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED;
	result = erase_span(state, geometry->working_offset, geometry->working_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = program_body(state, geometry->working_offset, image,
			geometry->working_size, PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = marker(state,
			geometry->working_offset + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET,
			PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
			PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);
	image[PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET] = committed_state;
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	if (state->ftw.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD) {
		uint8_t header_state = image[state->ftw.queue_offset];

		if (header_state == PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED)
			result = marker(state, queue,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_ABORTED);
		else if (header_state ==
			 PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED)
			result = marker(state, queue,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE);
		else {
			state->invariant_failure = true;
			result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		}
	} else if (state->ftw.queue_disposition !=
		   PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY) {
		state->invariant_failure = true;
		result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	return erase_span(state, geometry->spare_offset, geometry->spare_size);
}

static enum payload_mm_authvar_media_result recover_once(
	struct executor_session *state)
{
	const struct payload_mm_authvar_fv_geometry *geometry =
		&state->ftw.geometry;

	payload_mm_authvar_media_cache_invalidate();
	switch (state->ftw.action) {
	case PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE:
		return workspace_rebuild(state, false);
	case PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED:
		if (state->ftw.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING)
			return erase_span(state, geometry->working_offset,
				geometry->working_size);
		if (state->ftw.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE)
			return erase_span(state, geometry->spare_offset,
				geometry->spare_size);
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	case PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE: {
		enum payload_mm_authvar_media_result result;

		if (state->ftw.workspace != PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE) {
			state->invariant_failure = true;
			return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		}
		result = erase_span(state, geometry->spare_offset,
			geometry->spare_size);
		return result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ?
			verify_erased(state, geometry->spare_offset,
				geometry->spare_size) : result;
	}
	case PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE:
		if (state->ftw.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING)
			return workspace_rebuild(state, true);
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	case PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD:
		return recover_abort(state);
	case PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE:
		return replay_spare(state);
	case PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW: {
		uint32_t queue = geometry->working_offset + state->ftw.queue_offset;
		enum payload_mm_authvar_media_result result = marker(state, queue,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE);

		return result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ?
			erase_span(state, geometry->spare_offset, geometry->spare_size) :
			result;
	}
	case PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE:
		if (state->ftw.workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE)
			return restore_workspace(state);
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	case PAYLOAD_MM_AUTHVAR_FTW_CLEAN:
	case PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED:
	default:
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
}

#if !CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
static bool same_recovery(const struct payload_mm_authvar_ftw_plan *left,
	const struct payload_mm_authvar_ftw_plan *right)
{
	return left->action == right->action && left->workspace == right->workspace &&
		left->queue_disposition == right->queue_disposition &&
		left->queue_offset == right->queue_offset &&
		left->queue_entry_size == right->queue_entry_size;
}
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
static uint64_t poison_session(void);

static bool recovery_image_marker(uint8_t *image, size_t size, uint32_t offset,
	uint8_t expected, uint8_t wanted)
{
	if (offset >= size || image[offset] != expected ||
	    wanted != (expected & wanted))
		return false;
	image[offset] = wanted;
	return true;
}

static bool recovery_image_apply(uint8_t *image, size_t size,
	const struct payload_mm_authvar_ftw_plan *plan)
{
	const struct payload_mm_authvar_fv_geometry *geometry = &plan->geometry;
	uint32_t queue;
	uint8_t *working;
	uint8_t *spare;

	if (geometry->variable_offset > size ||
	    geometry->variable_size > size - geometry->variable_offset ||
	    geometry->working_offset > size ||
	    geometry->working_size > size - geometry->working_offset ||
	    geometry->spare_offset > size ||
	    geometry->spare_size > size - geometry->spare_offset ||
	    plan->queue_offset > geometry->working_size)
		return false;
	working = image + geometry->working_offset;
	spare = image + geometry->spare_offset;
	queue = geometry->working_offset + plan->queue_offset;
	switch (plan->action) {
	case PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE:
	case PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE:
		empty_workspace(working, geometry->working_size);
		working[PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET] =
			PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID;
		memset(spare, 0xff, geometry->spare_size);
		return true;
	case PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED:
		if (plan->workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING)
			memset(working, 0xff, geometry->working_size);
		else if (plan->workspace == PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE)
			memset(spare, 0xff, geometry->spare_size);
		else
			return false;
		return true;
	case PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE:
		memset(spare, 0xff, geometry->spare_size);
		return true;
	case PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD:
		if (plan->queue_entry_size != PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
		    PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE || queue >= size)
			return false;
		if (image[queue] == PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED)
			return recovery_image_marker(image, size, queue,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_ABORTED);
		if (image[queue] != PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED ||
		    queue > size - PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE ||
		    image[queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE] !=
			PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED)
			return false;
		memset(spare, 0xff, geometry->spare_size);
		return recovery_image_marker(image, size, queue,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE);
	case PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE:
		if (geometry->variable_size > geometry->spare_size ||
		    queue > size - PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE)
			return false;
		memcpy(image + geometry->variable_offset, spare,
			geometry->variable_size);
		if (!recovery_image_marker(image, size,
			queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE,
			PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
			PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE) ||
		    !recovery_image_marker(image, size, queue,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE))
			return false;
		memset(spare, 0xff, geometry->spare_size);
		return true;
	case PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW:
		if (!recovery_image_marker(image, size, queue,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE))
			return false;
		memset(spare, 0xff, geometry->spare_size);
		return true;
	case PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE:
		if (geometry->working_size > geometry->spare_size)
			return false;
		memcpy(working, spare, geometry->working_size);
		if (plan->queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD) {
			if (image[queue] == PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED) {
				if (!recovery_image_marker(image, size, queue,
					PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED,
					PAYLOAD_MM_AUTHVAR_FTW_HEADER_ABORTED))
					return false;
			} else if (!recovery_image_marker(image, size, queue,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE)) {
				return false;
			}
		} else if (plan->queue_disposition !=
			   PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY) {
			return false;
		}
		memset(spare, 0xff, geometry->spare_size);
		return true;
	case PAYLOAD_MM_AUTHVAR_FTW_CLEAN:
	case PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED:
	default:
		return false;
	}
}

static bool recovery_plan_build(struct executor_session *state)
{
	uint8_t *canonical = arena_at(executor.sealed.candidate_offset);
	uint8_t *canonical_sealed =
		arena_at(executor.sealed.recovery_image_seal_offset);

	memset(&state->recovery, 0, sizeof(state->recovery));
	state->recovery.generation = state->generation;
	state->recovery.token = state->token;
	memcpy(canonical, snapshot(), state->contract.store_size);
	for (state->recovery.count = 0;
	     state->recovery.count < EXECUTOR_RECOVERY_LIMIT;
	     state->recovery.count++) {
		struct payload_mm_authvar_ftw_plan *step =
			&state->recovery.steps[state->recovery.count];

		if (payload_mm_authvar_ftw_plan(canonical, state->contract.store_size,
			state->contract.block_size, step) != CB_SUCCESS)
			return false;
		if (step->action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN) {
			memset(step, 0, sizeof(*step));
			memcpy(canonical_sealed, canonical,
				state->contract.store_size);
			state->recovery_sealed = state->recovery;
			return true;
		}
		if (!recovery_image_apply(canonical, state->contract.store_size, step))
			return false;
	}
	return false;
}

static bool recovery_image_unchanged(const struct executor_session *state)
{
	return !memcmp(arena_at(executor.sealed.candidate_offset),
		arena_at(executor.sealed.recovery_image_seal_offset),
		state->contract.store_size);
}

static uint64_t recovery_plan_execute(struct executor_session *state)
{
	uint8_t *canonical = arena_at(executor.sealed.candidate_offset);

	if (memcmp(&state->recovery, &state->recovery_sealed,
		sizeof(state->recovery)) || !owner_equal(state) ||
	    state->recovery.generation != state->generation ||
	    state->recovery.token != state->token ||
	    !recovery_image_unchanged(state))
		return poison_session();
	state->recovery_plan_active = true;
	if (verify_media(state, 0, snapshot(), state->contract.store_size) !=
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS || !recovery_image_unchanged(state))
		return poison_session();
	for (state->recovery_count = 0;
	     state->recovery_count < state->recovery.count;
	     state->recovery_count++) {
		struct payload_mm_authvar_ftw_plan observed;
		enum payload_mm_authvar_media_result result;

		if (payload_mm_authvar_ftw_plan(snapshot(), state->contract.store_size,
			state->contract.block_size, &observed) != CB_SUCCESS ||
		    memcmp(&observed,
			&state->recovery.steps[state->recovery_count], sizeof(observed)))
			return poison_session();
		state->ftw = observed;
		if (!recovery_image_unchanged(state))
			return poison_session();
		result = recover_once(state);

		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return state->invariant_failure ? poison_session() :
				payload_mm_authvar_media_result_status(result);
		if (!recovery_image_unchanged(state))
			return poison_session();
		result = snapshot_read(state);
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return state->invariant_failure ? poison_session() :
				payload_mm_authvar_media_result_status(result);
		if (!recovery_image_unchanged(state))
			return poison_session();
	}
	if (payload_mm_authvar_ftw_plan(snapshot(), state->contract.store_size,
		state->contract.block_size, &state->ftw) != CB_SUCCESS ||
	    state->ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    memcmp(snapshot(), canonical, state->contract.store_size))
		return poison_session();
	state->recovery_plan_active = false;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
#endif

static uint64_t poison_session(void)
{
	executor.installed = false;
	(void)payload_mm_authvar_media_fail_closed(
		executor.sealed_owner_generation, executor.sealed_owner_token);
	return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_RECOVERY)
static bool bytes_erased(const uint8_t *bytes, size_t size)
{
	while (size--)
		if (*bytes++ != 0xffU)
			return false;
	return true;
}
#endif

static uint64_t recover_session(struct executor_session *state)
{
	for (state->recovery_count = 0;
	     state->recovery_count < EXECUTOR_RECOVERY_LIMIT;
	     state->recovery_count++) {
		enum payload_mm_authvar_media_result result;
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
		uint64_t recovery_status;
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_RECOVERY)
		struct payload_mm_authvar_fv_geometry geometry;
		enum payload_mm_authvar_default_store_source source;
		uint8_t *target = arena_at(executor.sealed.candidate_offset);
		uint8_t *current = snapshot();
		size_t offset;
		size_t size;
#endif

		result = snapshot_read(state);
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return state->invariant_failure ? poison_session() :
				payload_mm_authvar_media_result_status(result);
#if CONFIG(PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_RECOVERY)
		if (!payload_mm_authvar_fv_geometry(&geometry,
			state->contract.store_size, state->contract.block_size) ||
		    geometry.variable_offset ||
		    geometry.variable_size < PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE)
			return poison_session();
		if (!bytes_erased(current + geometry.working_offset,
			geometry.working_size) ||
		    !bytes_erased(current + geometry.spare_offset,
			geometry.spare_size))
			goto plan_ftw;
		source = payload_mm_authvar_default_store_compose(current, target,
			state->contract.store_size, state->contract.block_size);
		if (source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID)
			return poison_session();
		if (source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_FOREIGN)
			goto plan_ftw;
		if (executor.ready_to_boot || executor.at_runtime)
			return poison_session();
		if (source == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_COMPLETE)
			goto plan_ftw;
		if (source != PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED &&
		    source != PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET)
			return poison_session();
		for (offset = PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
		     offset < geometry.variable_size &&
		     current[offset] == target[offset]; offset++)
			;
		if (offset < geometry.variable_size) {
			size = geometry.variable_size - offset;
			if (size > EXECUTOR_TRANSFER_SIZE)
				size = EXECUTOR_TRANSFER_SIZE;
		} else if (memcmp(current, target,
			PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE)) {
			offset = 0;
			size = PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
		} else {
			return poison_session();
		}
		result = checked_program(state, (uint32_t)offset,
			target + offset, size);
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return state->invariant_failure ? poison_session() :
				payload_mm_authvar_media_result_status(result);
		continue;
plan_ftw:
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
		if (!recovery_plan_build(state))
			return poison_session();
		recovery_status = recovery_plan_execute(state);

		if (recovery_status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
			return recovery_status;
		{
			uint32_t store_base;
			struct payload_mm_authvar_store_limits limits = {
				.maximum_store_size =
					executor.sealed.limits.maximum_store_size,
				.maximum_name_size = executor.sealed.limits.maximum_name_size,
				.maximum_data_size = executor.sealed.limits.maximum_data_size,
				.maximum_records = executor.sealed.limits.maximum_records,
			};

			memset(&state->index, 0, sizeof(state->index));
			state->index.entries = arena_at(executor.sealed.entries_offset);
			state->index.entry_capacity =
				executor.sealed.limits.maximum_records;
			if (!ftw_store_base(state, &store_base) ||
			    payload_mm_authvar_store_scan(&state->index,
				snapshot() + store_base,
				state->ftw.variable_store_size, &limits) != CB_SUCCESS)
				return poison_session();
			return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		}
#else
		if (payload_mm_authvar_ftw_plan(snapshot(), state->contract.store_size,
			state->contract.block_size, &state->ftw) != CB_SUCCESS)
			return poison_session();
		if (state->ftw.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN) {
			uint32_t store_base;
			struct payload_mm_authvar_store_limits limits = {
				.maximum_store_size = executor.sealed.limits.maximum_store_size,
				.maximum_name_size = executor.sealed.limits.maximum_name_size,
				.maximum_data_size = executor.sealed.limits.maximum_data_size,
				.maximum_records = executor.sealed.limits.maximum_records,
			};

			memset(&state->index, 0, sizeof(state->index));
			state->index.entries = arena_at(executor.sealed.entries_offset);
			state->index.entry_capacity = executor.sealed.limits.maximum_records;
			if (!ftw_store_base(state, &store_base) ||
			    payload_mm_authvar_store_scan(&state->index,
				snapshot() + store_base,
				state->ftw.variable_store_size, &limits) != CB_SUCCESS)
				return poison_session();
			return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		}
		if (state->have_previous_ftw &&
		    same_recovery(&state->previous_ftw, &state->ftw))
			return poison_session();
		state->previous_ftw = state->ftw;
		state->have_previous_ftw = true;
		result = recover_once(state);
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS && state->invariant_failure)
			return poison_session();
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return payload_mm_authvar_media_result_status(result);
#endif
	}
	return poison_session();
}

static enum payload_mm_authvar_media_result execute_direct(
	struct executor_session *state)
{
	uint32_t store_base;
	uint8_t *record = arena_at(executor.sealed.record_offset);

	if (!ftw_store_base(state, &store_base))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;

	for (uint32_t i = 0; i < state->write.step_count; i++) {
		const struct payload_mm_authvar_write_step *step =
			&state->write.steps[i];
		uint64_t end = (uint64_t)step->offset + step->size;
		uint32_t media_offset;
		enum payload_mm_authvar_media_result result;

		if (end > state->ftw.variable_store_size ||
		    step->offset > UINT32_MAX - store_base) {
			state->invariant_failure = true;
			return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		}
		media_offset = store_base + step->offset;
		if (step->kind == PAYLOAD_MM_AUTHVAR_WRITE_RECORD) {
			if (step->offset != state->write.record_offset ||
			    step->size != state->write.record_size || step->size < 3U) {
				state->invariant_failure = true;
				return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
			}
			result = verify_erased(state, media_offset, step->size);
			if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
				return result;
			if (record[2] != PAYLOAD_MM_AUTHVAR_STATE_ERASED) {
				state->invariant_failure = true;
				return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
			}
			result = program_body(state, media_offset, record, step->size, 2U);
			if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
				return result;
			memcpy(snapshot() + media_offset, record, step->size);
		} else if (step->kind == PAYLOAD_MM_AUTHVAR_WRITE_STATE &&
			   step->size == 1U) {
			result = marker(state, media_offset, step->expected_state,
				step->new_state);
			if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
				return result;
			snapshot()[media_offset] = step->new_state;
		} else {
			state->invariant_failure = true;
			return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		}
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result advance_marker(
	struct executor_session *state, uint32_t offset, uint8_t expected,
	uint8_t wanted)
{
	enum payload_mm_authvar_media_result result = marker(state, offset,
		expected, wanted);

	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		snapshot()[offset] = wanted;
	return result;
}

enum candidate_commit_phase {
	CANDIDATE_PHASE_ADMITTED = 1,
	CANDIDATE_PHASE_JOURNAL_HEADER,
	CANDIDATE_PHASE_JOURNAL_ALLOCATED,
	CANDIDATE_PHASE_JOURNAL_RECORD,
	CANDIDATE_PHASE_SPARE_ERASE,
	CANDIDATE_PHASE_SPARE_IMAGE,
	CANDIDATE_PHASE_SPARE_COMPLETE,
	CANDIDATE_PHASE_PRIMARY_ERASE,
	CANDIDATE_PHASE_PRIMARY_IMAGE,
	CANDIDATE_PHASE_DESTINATION_COMPLETE,
	CANDIDATE_PHASE_JOURNAL_COMPLETE,
	CANDIDATE_PHASE_SPARE_CLEAN,
	CANDIDATE_PHASE_DURABLE,
};

#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
static bool digest_matches(const void *data, size_t size,
	const uint8_t expected[PAYLOAD_MM_SHA256_SIZE])
{
	uint8_t digest[PAYLOAD_MM_SHA256_SIZE];
	enum payload_mm_verify_status status = payload_mm_sha256(data, size, digest);
	bool matches = status == PAYLOAD_MM_VERIFY_OK &&
		!memcmp(digest, expected, sizeof(digest));

	memset(digest, 0, sizeof(digest));
	return matches;
}

static enum payload_mm_authvar_media_result candidate_checkpoint(
	struct executor_session *state, const uint8_t *image,
	enum candidate_commit_phase phase, bool source_must_match)
{
	struct executor_control_seal before;
	uint32_t store_base;

	state->candidate_phase = phase;
	if (!ftw_store_base(state, &store_base) || !owner_equal(state) ||
	    !control_snapshot(state, &before, false) ||
	    state->candidate_size != state->ftw.variable_store_size ||
	    !digest_matches(image, state->ftw.geometry.variable_size,
		state->candidate_image_digest) ||
	    !digest_matches(image + store_base, state->candidate_size,
		state->candidate_result.candidate_digest) ||
	    !control_unchanged(state, &before, false))
		goto contradiction;
	if (!source_must_match)
		return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
	{
		enum payload_mm_authvar_media_result result = verify_media(state,
			state->ftw.geometry.variable_offset, snapshot(),
			state->ftw.geometry.variable_size);

		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return result;
	}
	if (!digest_matches(snapshot() + store_base, state->candidate_size,
		state->candidate_result.source_digest) ||
	    !control_unchanged(state, &before, false))
		goto contradiction;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;

contradiction:
	state->invariant_failure = true;
	return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

static enum payload_mm_authvar_media_result candidate_source_fresh(
	struct executor_session *state)
{
	const struct payload_mm_authvar_fv_geometry *geometry =
		&state->ftw.geometry;
	enum payload_mm_authvar_media_result result;

	/* The three verified spans exactly cover the sealed SMMSTORE geometry. */
	result = verify_media(state, geometry->variable_offset, snapshot(),
		geometry->variable_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, geometry->working_offset,
			snapshot() + geometry->working_offset,
			geometry->working_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_erased(state, geometry->spare_offset,
			geometry->spare_size);
	return result;
}
#endif

static enum payload_mm_authvar_media_result candidate_phase_checkpoint(
	struct executor_session *state, const uint8_t *image,
	bool candidate_commit, enum candidate_commit_phase phase,
	bool source_must_match,
	enum payload_mm_authvar_media_result current)
{
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	if (candidate_commit && current == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return candidate_checkpoint(state, image, phase, source_must_match);
#else
	(void)state;
	(void)image;
	(void)candidate_commit;
	(void)phase;
	(void)source_must_match;
#endif
	return current;
}

static enum payload_mm_authvar_media_result execute_ftw_image(
	struct executor_session *state, const uint8_t *image,
	bool candidate_commit)
{
	const struct payload_mm_authvar_fv_geometry *geometry =
		&state->ftw.geometry;
	uint32_t queue = geometry->working_offset + state->ftw.queue_offset;
	uint8_t *header = snapshot() + queue;
	uint8_t *record_header = header + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE;
	enum payload_mm_authvar_media_result result;

	if (!image || state->ftw.queue_offset > geometry->working_size ||
	    PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
		PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE >
		geometry->working_size - state->ftw.queue_offset) {
		state->invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	}
	memset(header, 0xff, PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
		PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE);
	memcpy(header + 4U, payload_mm_authvar_ftw_coreboot_caller_guid,
		sizeof(payload_mm_authvar_ftw_coreboot_caller_guid));
	write_le64(header + 24U, 1U);
	write_le64(header + 32U, 0U);
	write_le64(record_header + 8U, 0U);
	write_le64(record_header + 16U, state->ftw.fv_header_size);
	write_le64(record_header + 24U, state->ftw.variable_store_size);
	write_le64(record_header + 32U,
		(uint64_t)-(int64_t)geometry->spare_offset);
	result = verify_erased(state, queue,
		PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
		PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_ADMITTED, true, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = checked_program(state, queue + 1U, header + 1U,
			PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE - 1U);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, queue + 1U, header + 1U,
			PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE - 1U);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_JOURNAL_HEADER, true, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state, queue,
			PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state, queue,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_JOURNAL_ALLOCATED, true, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = checked_program(state,
			queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + 1U,
			record_header + 1U,
			PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE - 1U);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + 1U,
			record_header + 1U,
			PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE - 1U);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_JOURNAL_RECORD, true, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = erase_span(state, geometry->spare_offset,
			geometry->spare_size);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_SPARE_ERASE, true, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = checked_program(state, geometry->spare_offset, image,
			geometry->variable_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, geometry->spare_offset, image,
			geometry->variable_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS &&
	    geometry->spare_size > geometry->variable_size)
		result = verify_erased(state,
			geometry->spare_offset + geometry->variable_size,
			geometry->spare_size - geometry->variable_size);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_SPARE_IMAGE, true, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state,
			queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE,
			PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
			PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_SPARE_COMPLETE, true, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = erase_span(state, geometry->variable_offset,
			geometry->variable_size);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_PRIMARY_ERASE, false, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = checked_program(state, geometry->variable_offset, image,
			geometry->variable_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, geometry->variable_offset, image,
			geometry->variable_size);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_PRIMARY_IMAGE, false, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state,
			queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE,
			PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
			PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_DESTINATION_COMPLETE, false, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state, queue,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_JOURNAL_COMPLETE, false, result);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = erase_span(state, geometry->spare_offset,
			geometry->spare_size);
	result = candidate_phase_checkpoint(state, image, candidate_commit,
		CANDIDATE_PHASE_SPARE_CLEAN, false, result);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	memmove(snapshot(), image, geometry->variable_size);
	memset(snapshot() + geometry->spare_offset, 0xff, geometry->spare_size);
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	if (candidate_commit)
		state->candidate_phase = CANDIDATE_PHASE_DURABLE;
#endif
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
static bool zero_bytes(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	uint8_t combined = 0;

	while (size--)
		combined |= *bytes++;
	return combined == 0;
}

static bool exact_index_equal(
	const struct payload_mm_authvar_store_index *left,
	const struct payload_mm_authvar_store_index *right)
{
	return left->store == right->store &&
		left->store_size == right->store_size &&
		left->used_size == right->used_size &&
		left->dirty_tail_offset == right->dirty_tail_offset &&
		left->record_count == right->record_count &&
		left->entry_count == right->entry_count &&
		left->entry_capacity == right->entry_capacity &&
		left->maximum_name_size == right->maximum_name_size &&
		left->maximum_data_size == right->maximum_data_size &&
		left->maximum_records == right->maximum_records &&
		!memcmp(left->entries, right->entries,
			(size_t)left->entry_count * sizeof(left->entries[0]));
}

static enum payload_mm_authvar_media_result __maybe_unused
commit_candidate_image(
	struct executor_session *state, uint8_t *candidate_store,
	const struct payload_mm_authvar_candidate_result *result)
{
	const struct payload_mm_authvar_store_index source_index = state->index;
	struct payload_mm_authvar_ftw_plan fresh_ftw;
	struct payload_mm_authvar_store_index candidate_index = {
		.entries = arena_at(executor.sealed.candidate_entries_offset),
		.entry_capacity = executor.sealed.limits.maximum_records,
	};
	struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = executor.sealed.limits.maximum_store_size,
		.maximum_name_size = executor.sealed.limits.maximum_name_size,
		.maximum_data_size = executor.sealed.limits.maximum_data_size,
		.maximum_records = executor.sealed.limits.maximum_records,
	};
	const uint8_t mode_mask = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	uint8_t *image = snapshot() + state->ftw.geometry.spare_offset;
	enum payload_mm_authvar_media_result media_result;
	uint32_t store_base;

	if (!ftw_store_base(state, &store_base) ||
	    candidate_store != arena_at(executor.sealed.candidate_offset) ||
	    state->ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    state->ftw.geometry.variable_size != store_base +
		state->ftw.variable_store_size ||
	    state->index.store != snapshot() + store_base ||
	    state->index.store_size != state->ftw.variable_store_size ||
	    !payload_mm_authvar_store_index_valid(&state->index) ||
	    result != &state->candidate_result ||
	    memcmp(&result->binding, &state->candidate_binding,
		sizeof(result->binding)) ||
	    result->binding.generation != state->generation ||
	    result->binding.token != state->token ||
	    result->binding.at_runtime > 1U ||
	    result->binding.at_runtime != state->at_runtime ||
	    result->binding.source_volatile_modes & ~mode_mask ||
	    result->volatile_modes & ~mode_mask ||
	    !zero_bytes(result->binding.reserved,
		sizeof(result->binding.reserved)) ||
	    !zero_bytes(result->reserved, sizeof(result->reserved)) ||
	    memcmp(&result->policy, &state->policy, sizeof(state->policy)) ||
	    result->source_used_size != state->index.used_size ||
	    !result->candidate_record_count ||
	    result->candidate_record_count > state->policy.maximum_records ||
	    result->candidate_used_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    result->candidate_used_size > state->ftw.variable_store_size ||
	    !digest_matches(candidate_store, state->ftw.variable_store_size,
		result->candidate_digest))
		goto contradiction;
	/* Candidate staging is disjoint, so this is a literal last-media view. */
	media_result = snapshot_read(state);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return media_result;
	if (payload_mm_authvar_ftw_plan(snapshot(), state->contract.store_size,
		state->contract.block_size, &fresh_ftw) != CB_SUCCESS ||
	    fresh_ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    memcmp(&fresh_ftw, &state->ftw, sizeof(fresh_ftw)))
		goto contradiction;
	memset(candidate_index.entries, 0,
		(size_t)candidate_index.entry_capacity *
		sizeof(candidate_index.entries[0]));
	if (payload_mm_authvar_store_scan(&candidate_index,
		snapshot() + store_base, state->ftw.variable_store_size,
		&limits) != CB_SUCCESS ||
	    !exact_index_equal(&source_index, &candidate_index) ||
	    !digest_matches(candidate_index.store, candidate_index.store_size,
		result->source_digest))
		goto contradiction;
	memset(candidate_index.entries, 0,
		(size_t)candidate_index.entry_capacity *
		sizeof(candidate_index.entries[0]));
	if (payload_mm_authvar_store_scan(&candidate_index, candidate_store,
		state->ftw.variable_store_size, &limits) != CB_SUCCESS ||
	    !payload_mm_authvar_store_index_valid(&candidate_index) ||
	    candidate_index.store_size != state->ftw.variable_store_size ||
	    candidate_index.used_size != result->candidate_used_size ||
	    candidate_index.record_count != result->candidate_record_count ||
	    candidate_index.entry_count != result->candidate_record_count ||
	    candidate_index.dirty_tail_offset ||
	    !payload_mm_authvar_candidate_projection_valid(&source_index,
		&candidate_index, &state->candidate_binding,
		result->volatile_modes))
		goto contradiction;
	memcpy(image, snapshot(), store_base);
	memcpy(image + store_base, candidate_store,
		state->ftw.variable_store_size);
	state->candidate_size = state->ftw.variable_store_size;
	state->staged_volatile_modes = result->volatile_modes;
	if (payload_mm_sha256(image, state->ftw.geometry.variable_size,
		state->candidate_image_digest) != PAYLOAD_MM_VERIFY_OK)
		goto contradiction;
	media_result = candidate_source_fresh(state);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return media_result;
	media_result = candidate_checkpoint(state, image,
		CANDIDATE_PHASE_ADMITTED, true);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return media_result;
	return execute_ftw_image(state, image, true);

contradiction:
	state->invariant_failure = true;
	return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}
#endif

static enum payload_mm_authvar_media_result execute_reclaim(
	struct executor_session *state,
	const struct payload_mm_authvar_store_entry *replaced)
{
	const struct payload_mm_authvar_fv_geometry *geometry =
		&state->ftw.geometry;
	uint32_t image_store_base;
	uint32_t media_store_base;
	uint8_t *image = snapshot() + geometry->spare_offset;
	uint8_t *record = arena_at(executor.sealed.record_offset);
	uint32_t destination = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	if (!ftw_store_base(state, &media_store_base))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	image_store_base = media_store_base;
	if (state->reclaim.action != PAYLOAD_MM_AUTHVAR_SPACE_RECLAIM ||
	    state->reclaim.copy_count > state->reclaim.copy_capacity ||
	    state->reclaim.copy_count > executor.sealed.limits.maximum_records ||
	    state->reclaim.copy_count != state->index.entry_count -
		(replaced ? 1U : 0U))
		goto contradiction;
	memset(image, 0xff, geometry->variable_size);
	memcpy(image, snapshot() + geometry->variable_offset,
		state->ftw.fv_header_size +
		PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE);
	for (uint32_t i = 0; i < state->reclaim.copy_count; i++) {
		const struct payload_mm_authvar_reclaim_copy *copy =
			&state->reclaim.copies[i];
		uint64_t source_end = (uint64_t)copy->source_offset + copy->size;
		uint64_t destination_end = (uint64_t)copy->destination_offset +
			copy->size;
		const struct payload_mm_authvar_store_entry *entry = NULL;
		uint32_t exact_size;

		for (uint32_t candidate = 0; candidate < state->index.entry_count;
		     candidate++) {
			const struct payload_mm_authvar_store_entry *possible =
				&state->index.entries[candidate];

			if (possible != replaced &&
			    possible->record_offset == copy->source_offset) {
				entry = possible;
				break;
			}
		}
		if (!entry || entry->data_offset > UINT32_MAX - entry->data_size ||
		    entry->data_offset + entry->data_size > UINT32_MAX - 3U)
			goto contradiction;
		exact_size = (entry->data_offset + entry->data_size + 3U) & ~3U;
		if (exact_size < entry->record_offset)
			goto contradiction;
		exact_size -= entry->record_offset;
		for (uint32_t prior = 0; prior < i; prior++)
			if (state->reclaim.copies[prior].source_offset ==
			    copy->source_offset)
				goto contradiction;

		if (copy->destination_offset != destination || copy->size < 3U ||
		    !copy->source_offset || source_end >
			state->ftw.variable_store_size ||
		    destination_end > state->ftw.variable_store_size ||
		    destination > UINT32_MAX - copy->size ||
		    copy->size != exact_size || copy->promote_transition !=
			(snapshot()[media_store_base + copy->source_offset + 2U] ==
			 PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION))
			goto contradiction;
		memcpy(image + image_store_base + copy->destination_offset,
			snapshot() + media_store_base + copy->source_offset, copy->size);
		if (copy->promote_transition)
			image[image_store_base + copy->destination_offset + 2U] =
				PAYLOAD_MM_AUTHVAR_STATE_ADDED;
		destination += copy->size;
	}
	if ((uint64_t)state->write.record_offset + state->write.record_size >
	    state->ftw.variable_store_size || !state->write.record_size ||
	    state->reclaim.compacted_used_size != destination ||
	    state->reclaim.destination_offset != destination ||
	    state->write.record_offset != destination ||
	    state->reclaim.record_size != state->write.record_size)
		goto contradiction;
	memcpy(image + image_store_base + state->write.record_offset, record,
		state->write.record_size);
	image[image_store_base + state->write.record_offset + 2U] =
		PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	{
		struct payload_mm_authvar_store_limits limits = {
			.maximum_store_size = executor.sealed.limits.maximum_store_size,
			.maximum_name_size = executor.sealed.limits.maximum_name_size,
			.maximum_data_size = executor.sealed.limits.maximum_data_size,
			.maximum_records = executor.sealed.limits.maximum_records,
		};

		memset(&state->index, 0, sizeof(state->index));
		state->index.entries = arena_at(executor.sealed.entries_offset);
		state->index.entry_capacity = executor.sealed.limits.maximum_records;
		if (payload_mm_authvar_store_scan(&state->index,
			image + image_store_base, state->ftw.variable_store_size,
			&limits) != CB_SUCCESS ||
		    state->index.used_size != state->write.record_offset +
			state->write.record_size ||
		    state->index.entry_count != state->reclaim.copy_count + 1U) {
			goto contradiction;
		} else {
			const struct payload_mm_authvar_store_entry *winner =
				payload_mm_authvar_store_find(&state->index,
					state->source.vendor_guid, state->source.name,
					state->source.name_size);
			const uint8_t *built = image + image_store_base +
				state->write.record_offset;

			if (!winner || winner->record_offset != state->write.record_offset ||
			    record[2] != PAYLOAD_MM_AUTHVAR_STATE_ERASED ||
			    built[2] != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
			    memcmp(built, record, 2U) ||
			    memcmp(built + 3U, record + 3U,
				state->write.record_size - 3U))
				goto contradiction;
		}
		memset(&state->index, 0, sizeof(state->index));
	}
	return execute_ftw_image(state, image, false);

contradiction:
	state->invariant_failure = true;
	return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
static bool mor_grant_output_protected(void *unused, const void *storage,
	size_t size)
{
	(void)unused;
	return payload_mm_authvar_smram_buffer(storage, size);
}

static bool mor_control_plan(struct executor_session *state,
	const uint8_t *image, uint8_t *control_value)
{
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	const struct payload_mm_authvar_store_entry *entry;
	const uint8_t *data;
	struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = executor.sealed.limits.maximum_store_size,
		.maximum_name_size = executor.sealed.limits.maximum_name_size,
		.maximum_data_size = executor.sealed.limits.maximum_data_size,
		.maximum_records = executor.sealed.limits.maximum_records,
	};
	uint32_t store_base;

	if (payload_mm_authvar_ftw_plan(image, state->contract.store_size,
		state->contract.block_size, &state->ftw) != CB_SUCCESS ||
	    state->ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    !ftw_store_base(state, &store_base))
		return false;
	memset(&state->index, 0, sizeof(state->index));
	state->index.entries = arena_at(executor.sealed.entries_offset);
	state->index.entry_capacity = executor.sealed.limits.maximum_records;
	if (payload_mm_authvar_store_scan(&state->index, image + store_base,
		state->ftw.variable_store_size, &limits) != CB_SUCCESS)
		return false;
	entry = payload_mm_authvar_store_find(&state->index, identity->vendor_guid,
		identity->name, identity->name_size);
	if (!entry || entry->attributes != PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES ||
	    entry->data_size != 1U)
		return false;
	data = payload_mm_authvar_store_data(&state->index, entry);
	if (!data || !(*data & 1U))
		return false;
	*control_value = *data;
	memcpy(arena_at(executor.sealed.name_offset), identity->name,
		identity->name_size);
	*(uint8_t *)arena_at(executor.sealed.data_offset) =
		(uint8_t)(*data & (uint8_t)~1U);
	memset(&state->source, 0, sizeof(state->source));
	memcpy(state->source.vendor_guid, identity->vendor_guid,
		sizeof(state->source.vendor_guid));
	state->source.name = arena_at(executor.sealed.name_offset);
	state->source.name_size = identity->name_size;
	state->source.data = arena_at(executor.sealed.data_offset);
	state->source.data_size = 1U;
	state->source.attributes = PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES;
	state->policy = (struct payload_mm_authvar_write_policy) {
		.maximum_name_size = executor.sealed.limits.maximum_name_size,
		.maximum_record_size = executor.sealed.limits.maximum_record_size,
		.maximum_data_size = executor.sealed.limits.maximum_data_size,
		.maximum_records = executor.sealed.limits.maximum_records,
	};
	if (state->policy.maximum_record_size > state->index.store_size)
		state->policy.maximum_record_size = state->index.store_size;
	if (state->policy.maximum_data_size > state->policy.maximum_record_size)
		state->policy.maximum_data_size = state->policy.maximum_record_size;
	state->reclaim.copies = arena_at(executor.sealed.copies_offset);
	state->reclaim.copy_capacity = executor.sealed.limits.maximum_records;
	if (payload_mm_authvar_write_plan_build(&state->index, entry,
		&state->source, &state->policy, false,
		arena_at(executor.sealed.record_offset),
		executor.sealed.limits.maximum_record_size, &state->reclaim,
		&state->write) != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
	    (state->write.action != PAYLOAD_MM_AUTHVAR_WRITE_TAIL_APPEND &&
	     state->write.action != PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM))
		return false;
	return true;
}

static bool mor_control_seal_plan(struct executor_session *state)
{
	size_t copies_size;

	if (!multiply_size(state->reclaim.copy_count,
		sizeof(state->reclaim.copies[0]), &copies_size))
		return false;
	state->mor_write_sealed = state->write;
	state->mor_reclaim_sealed = state->reclaim;
	memcpy(arena_at(executor.sealed.mor_record_seal_offset),
		arena_at(executor.sealed.record_offset), state->write.record_size);
	memcpy(arena_at(executor.sealed.mor_copies_seal_offset),
		state->reclaim.copies, copies_size);
	return true;
}

#if ENV_TEST
bool payload_mm_authvar_executor_test_mutate_mor_seal(
	enum payload_mm_authvar_mor_seal_test_mutation mutation)
{
	struct executor_session *state = session();
	size_t copies_size;

	if (!state->mor_plan_sealed || !mor_clear_state_valid(state) ||
	    !multiply_size(state->reclaim.copy_count,
		sizeof(state->reclaim.copies[0]), &copies_size))
		return false;
	switch (mutation) {
	case PAYLOAD_MM_AUTHVAR_MOR_SEAL_TEST_RECORD_PAIR:
		if (!state->write.record_size)
			return false;
		*(uint8_t *)arena_at(executor.sealed.record_offset) ^= 1U;
		*(uint8_t *)arena_at(executor.sealed.mor_record_seal_offset) ^= 1U;
		return true;
	case PAYLOAD_MM_AUTHVAR_MOR_SEAL_TEST_COPIES_PAIR:
		if (!copies_size)
			return false;
		*(uint8_t *)state->reclaim.copies ^= 1U;
		*(uint8_t *)arena_at(executor.sealed.mor_copies_seal_offset) ^= 1U;
		return true;
	case PAYLOAD_MM_AUTHVAR_MOR_SEAL_TEST_CANONICAL_PAIR:
		if (!state->contract.store_size)
			return false;
		((uint8_t *)arena_at(executor.sealed.candidate_offset))
			[state->contract.store_size - 1U] ^= 1U;
		((uint8_t *)arena_at(executor.sealed.recovery_image_seal_offset))
			[state->contract.store_size - 1U] ^= 1U;
		return true;
	default:
		return false;
	}
}
#endif

static bool mor_control_plan_matches(struct executor_session *state,
	uint8_t expected_value)
{
	size_t copies_size;
	struct payload_mm_authvar_write_plan write = state->mor_write_sealed;
	struct payload_mm_authvar_reclaim_plan reclaim = state->mor_reclaim_sealed;

	if (!mor_control_plan(state, snapshot(), &expected_value) ||
	    expected_value != state->mor_grant.entry.value ||
	    memcmp(&state->write, &write, sizeof(write)) ||
	    memcmp(&state->reclaim, &reclaim, sizeof(reclaim)) ||
	    memcmp(arena_at(executor.sealed.record_offset),
		arena_at(executor.sealed.mor_record_seal_offset),
		state->write.record_size) ||
	    !multiply_size(state->reclaim.copy_count,
		sizeof(state->reclaim.copies[0]), &copies_size) ||
	    memcmp(state->reclaim.copies,
		arena_at(executor.sealed.mor_copies_seal_offset), copies_size))
		return false;
	return true;
}

static bool mor_control_final(struct executor_session *state,
	uint8_t expected_value)
{
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	const struct payload_mm_authvar_store_entry *entry;
	const uint8_t *data;
	struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = executor.sealed.limits.maximum_store_size,
		.maximum_name_size = executor.sealed.limits.maximum_name_size,
		.maximum_data_size = executor.sealed.limits.maximum_data_size,
		.maximum_records = executor.sealed.limits.maximum_records,
	};
	uint32_t store_base;

	if (payload_mm_authvar_ftw_plan(snapshot(), state->contract.store_size,
		state->contract.block_size, &state->ftw) != CB_SUCCESS ||
	    state->ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    !ftw_store_base(state, &store_base))
		return false;
	memset(&state->index, 0, sizeof(state->index));
	state->index.entries = arena_at(executor.sealed.entries_offset);
	state->index.entry_capacity = executor.sealed.limits.maximum_records;
	if (payload_mm_authvar_store_scan(&state->index, snapshot() + store_base,
		state->ftw.variable_store_size, &limits) != CB_SUCCESS)
		return false;
	entry = payload_mm_authvar_store_find(&state->index, identity->vendor_guid,
		identity->name, identity->name_size);
	if (!entry || entry->attributes != PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES ||
	    entry->data_size != 1U)
		return false;
	data = payload_mm_authvar_store_data(&state->index, entry);
	return data && *data == expected_value;
}

uint64_t payload_mm_authvar_mor_control_clear_transaction(void)
{
	struct executor_session *state;
	struct executor_control_seal grant_before;
	struct payload_mm_authvar_mor_grant taken_grant;
	enum payload_mm_authvar_media_result media_result;
	enum payload_mm_authvar_media_result end_result;
	enum cb_err take_result;
	uint64_t status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	uint32_t expected = 0;
	uint8_t control_value;
	bool grant_taken = false;

	if (provider_reentry() || !executor.installed || !policy_equal() ||
	    !__atomic_compare_exchange_n(&executor.busy, &expected, 1, false,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	state = session();
	memset(state, 0, sizeof(*state));
	media_result = media_begin(state);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = payload_mm_authvar_media_result_status(media_result);
		goto out;
	}
	payload_mm_authvar_media_cache_invalidate();
	if (!policy_equal() ||
	    !payload_mm_authvar_authority_snapshot(&state->contract) ||
	    !contract_allowed(&state->contract, &executor.sealed.limits) ||
	    snapshot_read(state) != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ||
	    !recovery_plan_build(state) ||
	    !mor_control_plan(state,
		arena_at(executor.sealed.candidate_offset), &control_value) ||
	    !mor_control_seal_plan(state)) {
		status = poison_session();
		goto end;
	}
	memset(&state->index, 0, sizeof(state->index));
	state->mor_plan_sealed = true;
	if (!control_snapshot(state, &grant_before, false)) {
		status = poison_session();
		goto end;
	}
	take_result = payload_mm_authvar_mor_grant_take(&state->mor_grant,
		mor_grant_output_protected, NULL, 0);
	memcpy(&taken_grant, &state->mor_grant, sizeof(taken_grant));
	memset(&state->mor_grant, 0, sizeof(state->mor_grant));
	if (!control_unchanged(state, &grant_before, false)) {
		memset(&taken_grant, 0, sizeof(taken_grant));
		status = poison_session();
		goto end;
	}
	if (take_result != CB_SUCCESS) {
		memset(&taken_grant, 0, sizeof(taken_grant));
		goto end;
	}
	memcpy(&state->mor_grant, &taken_grant, sizeof(state->mor_grant));
	memset(&taken_grant, 0, sizeof(taken_grant));
	grant_taken = true;
	if (!state->mor_grant.entry.present ||
	    state->mor_grant.entry.reserved ||
	    state->mor_grant.entry.value != control_value ||
	    !(state->mor_grant.entry.value & 1U)) {
		status = poison_session();
		goto end;
	}
	state->mor_clear_active = true;
	status = recovery_plan_execute(state);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
	    !mor_control_plan_matches(state, control_value)) {
		status = poison_session();
		goto end;
	}
	state->mor_write_active = true;
	if (state->write.action == PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM)
		media_result = execute_reclaim(state,
			payload_mm_authvar_store_find(&state->index,
				state->source.vendor_guid, state->source.name,
				state->source.name_size));
	else
		media_result = execute_direct(state);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = poison_session();
		goto end;
	}
	state->mor_write_active = false;
	media_result = snapshot_read(state);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ||
	    !mor_control_final(state,
		(uint8_t)(control_value & (uint8_t)~1U))) {
		status = poison_session();
		goto end;
	}
	payload_mm_authvar_media_cache_bind(state->generation, state->token);
	status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;

end:
	end_result = media_end(state);
	if (end_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		executor.installed = false;
		payload_mm_authvar_media_cache_invalidate();
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	} else if (grant_taken && status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
out:
	memset(executor.sealed.arena, 0, executor.sealed.required_size);
	__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);
	return status;
}
#endif

static bool copy_request(struct executor_session *state,
	const struct payload_mm_authvar_policy_request *request)
{
	struct payload_mm_authvar_policy_request copied = *request;

	if (copied.name_size)
		memcpy(arena_at(executor.sealed.name_offset), copied.name, copied.name_size);
	if (copied.data_size)
		memcpy(arena_at(executor.sealed.data_offset), copied.data, copied.data_size);
	if (memcmp(request, &copied, sizeof(copied)) ||
	    (copied.name_size && memcmp(copied.name,
		arena_at(executor.sealed.name_offset), copied.name_size)) ||
	    (copied.data_size && memcmp(copied.data,
		arena_at(executor.sealed.data_offset), copied.data_size)))
		return false;
	state->request = copied;
	state->request.name = copied.name_size ?
		arena_at(executor.sealed.name_offset) : NULL;
	state->request.data = copied.data_size ?
		arena_at(executor.sealed.data_offset) : NULL;
	state->policy = (struct payload_mm_authvar_write_policy) {
		.maximum_name_size = executor.sealed.limits.maximum_name_size,
		.maximum_data_size = executor.sealed.limits.maximum_data_size,
		.maximum_record_size = executor.sealed.limits.maximum_record_size,
		.maximum_records = executor.sealed.limits.maximum_records,
	};
	return true;
}

static bool request_disjoint(
	const struct payload_mm_authvar_policy_request *request,
	const struct payload_mm_authvar_policy_result *result)
{
	struct payload_mm_authvar_policy_request copied;
	const void *parts[4];
	size_t sizes[4];

	if (!external_protected_span(request, sizeof(*request)) ||
	    !external_protected_span(result, sizeof(*result)) ||
	    (uintptr_t)request % _Alignof(struct payload_mm_authvar_policy_request) ||
	    (uintptr_t)result % _Alignof(struct payload_mm_authvar_policy_result))
		return false;
	memcpy(&copied, request, sizeof(copied));
	if (copied.name_size > executor.sealed.limits.maximum_name_size ||
	    copied.data_size > executor.sealed.limits.maximum_data_size)
		return false;
	parts[0] = request;
	sizes[0] = sizeof(*request);
	parts[1] = result;
	sizes[1] = sizeof(*result);
	parts[2] = copied.name;
	sizes[2] = copied.name_size;
	parts[3] = copied.data;
	sizes[3] = copied.data_size;
	for (size_t i = 0; i < 4; i++) {
		if (!sizes[i])
			continue;
		if (!external_protected_span(parts[i], sizes[i]))
			return false;
		for (size_t j = i + 1; j < 4; j++)
			if (sizes[j] && payload_mm_authvar_buffers_overlap(parts[i],
				sizes[i], parts[j], sizes[j]))
				return false;
	}
	return !memcmp(request, &copied, sizeof(copied));
}

static bool read_request_disjoint(
	const struct payload_mm_authvar_read_request *request,
	const struct payload_mm_authvar_read_result *result,
	struct payload_mm_authvar_read_request *snapshot_request)
{
	struct payload_mm_authvar_read_request copied;
	const void *parts[5];
	size_t sizes[5];

	if (!external_protected_span(request, sizeof(*request)) ||
	    !external_protected_span(result, sizeof(*result)) ||
	    (uintptr_t)request % _Alignof(struct payload_mm_authvar_read_request) ||
	    (uintptr_t)result % _Alignof(struct payload_mm_authvar_read_result))
		return false;
	memcpy(&copied, request, sizeof(copied));
	if (copied.name_size > executor.sealed.limits.maximum_name_size ||
	    copied.name_capacity > executor.sealed.limits.maximum_name_size ||
	    copied.data_capacity > executor.sealed.limits.maximum_data_size)
		return false;
	parts[0] = request;
	sizes[0] = sizeof(*request);
	parts[1] = result;
	sizes[1] = sizeof(*result);
	parts[2] = copied.name;
	sizes[2] = copied.name_size;
	parts[3] = copied.result_name;
	sizes[3] = copied.name_capacity;
	parts[4] = copied.result_data;
	sizes[4] = copied.data_capacity;
	for (size_t i = 0; i < 5; i++) {
		if (!sizes[i])
			continue;
		if (!external_protected_span(parts[i], sizes[i]))
			return false;
		for (size_t j = i + 1; j < 5; j++)
			if (sizes[j] && payload_mm_authvar_buffers_overlap(parts[i],
				sizes[i], parts[j], sizes[j]))
				return false;
	}
	if (memcmp(request, &copied, sizeof(copied)))
		return false;
	*snapshot_request = copied;
	return true;
}

static bool read_request_copy(struct executor_session *state,
	const struct payload_mm_authvar_read_request *request,
	struct payload_mm_authvar_read_request *copied)
{
	/* Keep the descriptor admitted by read_request_disjoint(), not a reread. */
	if (memcmp(request, copied, sizeof(*copied)))
		return false;
	if (copied->name_size)
		memcpy(arena_at(executor.sealed.name_offset), copied->name,
			copied->name_size);
	if (memcmp(request, copied, sizeof(*copied)) ||
	    (copied->name_size && memcmp(copied->name,
		arena_at(executor.sealed.name_offset), copied->name_size)))
		return false;
	copied->name = copied->name_size ?
		arena_at(executor.sealed.name_offset) : NULL;
	state->request.operation = copied->operation;
	state->request.attributes = copied->attributes;
	memcpy(state->request.vendor_guid, copied->vendor_guid,
		sizeof(state->request.vendor_guid));
	state->request.name = copied->name;
	state->request.name_size = copied->name_size;
	state->read_name_capacity = (uint32_t)copied->name_capacity;
	state->read_data_capacity = (uint32_t)copied->data_capacity;
	state->at_runtime = executor.at_runtime;
	return true;
}

static bool bytes_all_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static bool read_operation_valid(
	const struct payload_mm_authvar_read_request *request)
{
	switch (request->operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
		return !request->attributes && request->name_size &&
			!request->name_capacity && !request->result_name &&
			(request->result_data != NULL) == (request->data_capacity != 0);
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
		return !request->attributes && request->name_capacity >= 2U &&
			request->name_size <= request->name_capacity &&
			request->result_name && !request->data_capacity &&
			!request->result_data;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		return !request->name_size && !request->name &&
			!request->name_capacity && !request->result_name &&
			!request->data_capacity && !request->result_data &&
			bytes_all_zero(request->vendor_guid,
				sizeof(request->vendor_guid));
	default:
		return false;
	}
}

static bool set_request_valid(const struct payload_mm_authvar_policy_request *request)
{
	const uint8_t *name = request->name;

	if (!name || request->name_size < 4U || (request->name_size & 1U) ||
	    name[request->name_size - 1U] || name[request->name_size - 2U] ||
	    request->attributes & ~(CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR) ?
		PAYLOAD_MM_AUTHVAR_SET_REQUEST_ATTRIBUTES :
		PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED))
		return false;
	for (size_t i = 0; i + 2U < request->name_size; i += 2U)
		if (!name[i] && !name[i + 1U])
			return false;
	return true;
}

#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
static bool provider_status_valid(uint64_t status)
{
	return status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER ||
		status == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED ||
		status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR ||
		status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED ||
		status == PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES ||
		status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND ||
		status == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
}

static uint64_t authorize_mutation(struct executor_session *state)
{
	struct payload_mm_authvar_policy_mutation *mutation =
		arena_at(executor.sealed.mutation_offset);
	void *data = arena_at(executor.sealed.mutation_data_offset);
	void *before = arena_at(executor.sealed.provider_seal_offset);
	void *protected_arena = executor.sealed.arena;
	size_t sealed_size = executor.sealed.provider_seal_offset;
	size_t capacity = executor.sealed.limits.maximum_data_size;
	uint64_t status;
	struct payload_mm_authvar_media_provider_scope provider_scope;
	static const uint8_t zero_timestamp[16];

	if (!policy_equal() || !owner_equal(state) || !executor.sealed.provider.authorize)
		return poison_session();
	state->view.index = &state->index;
	state->view.ready_to_boot = executor.ready_to_boot;
	state->view.at_runtime = executor.at_runtime;
	memset(mutation, 0, sizeof(*mutation));
	memset(data, 0, capacity);
	memcpy(before, protected_arena, sealed_size);
	if (!payload_mm_authvar_media_provider_enter(&provider_scope))
		return poison_session();
	__atomic_store_n(&executor.provider_active, 1, __ATOMIC_RELEASE);
	status = executor.sealed.provider.authorize(&state->request, &state->view,
		mutation, data, capacity);
	if (!payload_mm_authvar_media_provider_leave(&provider_scope))
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	__atomic_store_n(&executor.provider_active, 0, __ATOMIC_RELEASE);
	if (!policy_equal() || !owner_equal(state) ||
	    __atomic_load_n(&executor.provider_violation, __ATOMIC_ACQUIRE) ||
	    payload_mm_authvar_media_provider_violated() ||
	    memcmp(before, protected_arena, sealed_size) || !provider_status_valid(status)) {
		/* Restore controls before the single mandatory end, never execute output. */
		memcpy(protected_arena, before, sealed_size);
		return poison_session();
	}
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return status;
	if (mutation->data_size > capacity ||
	    (mutation->kind != PAYLOAD_MM_AUTHVAR_MUTATION_WRITE &&
	     mutation->kind != PAYLOAD_MM_AUTHVAR_MUTATION_DELETE) ||
	    (mutation->kind == PAYLOAD_MM_AUTHVAR_MUTATION_DELETE &&
	     (mutation->data_size || mutation->attributes ||
	      memcmp(mutation->timestamp, zero_timestamp, sizeof(zero_timestamp)))) ||
	    (mutation->kind == PAYLOAD_MM_AUTHVAR_MUTATION_WRITE &&
	     !mutation->data_size &&
	     !(mutation->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE)))
		return poison_session();
	memcpy(state->source.vendor_guid, state->request.vendor_guid, 16);
	state->source.name = state->request.name;
	state->source.name_size = state->request.name_size;
	state->source.attributes = mutation->attributes;
	memcpy(state->source.timestamp, mutation->timestamp, 16);
	state->source.data_size = mutation->data_size;
	state->source.data = mutation->data_size ?
		arena_at(executor.sealed.data_offset) : NULL;
	if (mutation->data_size)
		memcpy(arena_at(executor.sealed.data_offset), data, mutation->data_size);
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
#endif

uint64_t payload_mm_authvar_executor_recover(void)
{
	struct executor_session *state;
	enum payload_mm_authvar_media_result begin_result;
	enum payload_mm_authvar_media_result end_result;
	uint64_t status;
	uint32_t expected = 0;

	if (provider_reentry())
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!executor.installed)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!policy_equal()) {
		executor.installed = false;
		(void)payload_mm_authvar_media_fail_closed(0, 0);
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	if (!__atomic_compare_exchange_n(&executor.busy, &expected, 1, false,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	state = session();
	memset(state, 0, sizeof(*state));
	begin_result = media_begin(state);
	if (begin_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = payload_mm_authvar_media_result_status(begin_result);
		goto out;
	}
	payload_mm_authvar_media_cache_invalidate();
	if (!policy_equal()) {
		status = poison_session();
		goto end;
	}
	if (!payload_mm_authvar_authority_snapshot(&state->contract) ||
	    !contract_allowed(&state->contract, &executor.sealed.limits)) {
		status = poison_session();
		goto end;
	}
	status = recover_session(state);
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		payload_mm_authvar_media_cache_bind(state->generation, state->token);
end:
	end_result = media_end(state);
	if (end_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		payload_mm_authvar_media_cache_invalidate();
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
			status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
out:
	memset(executor.sealed.arena, 0, executor.sealed.required_size);
	__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);
	return status;
}

#if ENV_TEST && CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
uint64_t payload_mm_authvar_executor_test_recovery_plan(
	enum payload_mm_authvar_recovery_test_mutation mutation, bool execute)
{
	struct executor_session *state;
	enum payload_mm_authvar_media_result result;
	enum payload_mm_authvar_media_result end_result;
	uint64_t status;
	uint32_t expected = 0;

	if (!__atomic_compare_exchange_n(&executor.busy, &expected, 1, false,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!executor.installed || !policy_equal()) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto out;
	}
	state = session();
	memset(state, 0, sizeof(*state));
	result = media_begin(state);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = payload_mm_authvar_media_result_status(result);
		goto out;
	}
	if (!payload_mm_authvar_authority_snapshot(&state->contract) ||
	    !contract_allowed(&state->contract, &executor.sealed.limits) ||
	    snapshot_read(state) != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ||
	    !recovery_plan_build(state)) {
		status = poison_session();
		goto end;
	}
	switch (mutation) {
	case PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_NONE:
		break;
	case PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_SNAPSHOT:
		snapshot()[state->contract.store_size - 1U] ^= 1U;
		break;
	case PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_GEOMETRY:
		if (!state->recovery.count) {
			status = poison_session();
			goto end;
		}
		state->recovery.steps[0].geometry.block_size ^= 1U;
		break;
	case PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_STEP:
		if (!state->recovery.count) {
			status = poison_session();
			goto end;
		}
		state->recovery.steps[0].queue_offset ^= 1U;
		break;
	case PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_TOKEN:
		state->recovery.token ^= 1U;
		break;
	case PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_CALLBACK_IMAGE:
		payload_mm_authvar_recovery_test_arm_image_mutation(
			arena_at(executor.sealed.candidate_offset),
			state->contract.store_size, 0, EXECUTOR_TRANSFER_SIZE, 0);
		break;
	case PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_ABORT_READ_IMAGE:
		payload_mm_authvar_recovery_test_arm_image_mutation(
			arena_at(executor.sealed.candidate_offset),
			state->contract.store_size,
			state->contract.block_size +
				PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE,
			1, 0);
		break;
	case PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_REPLAY_READ_IMAGE:
		payload_mm_authvar_recovery_test_arm_image_mutation(
			arena_at(executor.sealed.candidate_offset),
			state->contract.store_size,
			state->recovery.steps[0].geometry.spare_offset,
			state->recovery.steps[0].geometry.variable_size, 1);
		break;
	default:
		status = poison_session();
		goto end;
	}
	status = execute ? recovery_plan_execute(state) :
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
end:
	end_result = media_end(state);
	if (end_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
out:
	memset(executor.sealed.arena, 0, executor.sealed.required_size);
	__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);
	return status;
}
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
static uint64_t verify_committed_candidate(struct executor_session *state)
{
	struct payload_mm_authvar_store_limits scan_limits;
	enum payload_mm_authvar_media_result media_result;
	uint32_t store_base;

	media_result = verify_media(state, 0, snapshot(), state->contract.store_size);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return state->invariant_failure ? poison_session() :
			payload_mm_authvar_media_result_status(media_result);
	media_result = snapshot_read(state);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return state->invariant_failure ? poison_session() :
			payload_mm_authvar_media_result_status(media_result);
	if (payload_mm_authvar_ftw_plan(snapshot(), state->contract.store_size,
		state->contract.block_size, &state->ftw) != CB_SUCCESS ||
	    state->ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    !ftw_store_base(state, &store_base) ||
	    !digest_matches(snapshot() + store_base,
		state->ftw.variable_store_size,
		state->candidate_result.candidate_digest))
		return poison_session();
	scan_limits = (struct payload_mm_authvar_store_limits) {
		.maximum_store_size = executor.sealed.limits.maximum_store_size,
		.maximum_name_size = executor.sealed.limits.maximum_name_size,
		.maximum_data_size = executor.sealed.limits.maximum_data_size,
		.maximum_records = executor.sealed.limits.maximum_records,
	};
	memset(&state->index, 0, sizeof(state->index));
	state->index.entries = arena_at(executor.sealed.entries_offset);
	state->index.entry_capacity = executor.sealed.limits.maximum_records;
	if (payload_mm_authvar_store_scan(&state->index, snapshot() + store_base,
		state->ftw.variable_store_size, &scan_limits) != CB_SUCCESS ||
	    state->index.used_size != state->candidate_result.candidate_used_size ||
	    state->index.entry_count !=
		state->candidate_result.candidate_record_count)
		return poison_session();
	payload_mm_authvar_media_cache_bind(state->generation, state->token);
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
#endif

#if ENV_TEST && CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
void payload_mm_authvar_executor_test_corrupt_policy(bool corrupt)
{
	if (corrupt) {
		executor.sealed.arena = (void *)(uintptr_t)1U;
		executor.sealed.required_size = SIZE_MAX;
	} else {
		executor.sealed = executor.policy;
	}
}

uint64_t payload_mm_authvar_executor_test_commit_candidate(
	payload_mm_authvar_candidate_prepare_test_fn prepare, void *context,
	u8 source_volatile_modes, u8 *published_volatile_modes)
{
	struct payload_mm_authvar_candidate_binding binding;
	struct payload_mm_authvar_candidate_result prepared;
	struct executor_control_seal before_prepare;
	struct executor_session *state;
	enum payload_mm_authvar_media_result media_result;
	enum payload_mm_authvar_media_result end_result =
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	uint8_t *candidate_store;
	uint8_t source_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t entries_digest[PAYLOAD_MM_SHA256_SIZE];
	uint8_t check_digest[PAYLOAD_MM_SHA256_SIZE];
	uint64_t status;
	uint32_t expected = 0;
	uint32_t store_base;
	size_t entries_size;

	if (!prepare || !published_volatile_modes || source_volatile_modes &
	    ~(PAYLOAD_MM_AUTHVAR_MODE_SETUP | PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
	      PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) ||
	    !external_protected_span(published_volatile_modes,
		sizeof(*published_volatile_modes)))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	*published_volatile_modes = 0;
	if (provider_reentry())
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!__atomic_compare_exchange_n(&executor.busy, &expected, 1, false,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!executor.installed || !policy_equal()) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto release_busy;
	}
	state = session();
	memset(state, 0, sizeof(*state));
	state->at_runtime = executor.at_runtime;
	media_result = media_begin(state);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = payload_mm_authvar_media_result_status(media_result);
		goto out;
	}
	payload_mm_authvar_media_cache_invalidate();
	if (!policy_equal() ||
	    !payload_mm_authvar_authority_snapshot(&state->contract) ||
	    !contract_allowed(&state->contract, &executor.sealed.limits)) {
		status = poison_session();
		goto end;
	}
	status = recover_session(state);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		goto end;
	if (!ftw_store_base(state, &store_base)) {
		status = poison_session();
		goto end;
	}
	state->policy = (struct payload_mm_authvar_write_policy) {
		.maximum_name_size = executor.sealed.limits.maximum_name_size,
		.maximum_data_size = executor.sealed.limits.maximum_data_size,
		.maximum_record_size = executor.sealed.limits.maximum_record_size,
		.maximum_records = executor.sealed.limits.maximum_records,
	};
	if (state->policy.maximum_record_size > state->index.store_size)
		state->policy.maximum_record_size = state->index.store_size;
	if (state->policy.maximum_data_size > state->policy.maximum_record_size)
		state->policy.maximum_data_size = state->policy.maximum_record_size;
	binding = (struct payload_mm_authvar_candidate_binding) {
		.generation = state->generation,
		.token = state->token,
		.source_volatile_modes = source_volatile_modes,
		.at_runtime = state->at_runtime,
	};
	state->candidate_binding = binding;
	candidate_store = arena_at(executor.sealed.candidate_offset);
	memset(&state->candidate_result, 0, sizeof(state->candidate_result));
	memset(&prepared, 0, sizeof(prepared));
	entries_size = (size_t)state->index.entry_count *
		sizeof(state->index.entries[0]);
	if (!control_snapshot(state, &before_prepare, false) ||
	    payload_mm_sha256(state->index.store, state->index.store_size,
		source_digest) != PAYLOAD_MM_VERIFY_OK ||
	    payload_mm_sha256(state->index.entries, entries_size,
		entries_digest) != PAYLOAD_MM_VERIFY_OK) {
		status = poison_session();
		goto end;
	}
	status = prepare(&state->index, &binding, candidate_store,
		state->ftw.variable_store_size,
		arena_at(executor.sealed.candidate_entries_offset),
		executor.sealed.limits.maximum_records,
		&prepared, context);
	/* A hostile test callback may know the caller output through its context. */
	*published_volatile_modes = 0;
	if (!owner_equal(state) || !control_unchanged(state, &before_prepare, false) ||
	    payload_mm_sha256(state->index.store, state->index.store_size,
		check_digest) != PAYLOAD_MM_VERIFY_OK ||
	    memcmp(check_digest, source_digest, sizeof(check_digest)) ||
	    payload_mm_sha256(state->index.entries, entries_size,
		check_digest) != PAYLOAD_MM_VERIFY_OK ||
	    memcmp(check_digest, entries_digest, sizeof(check_digest))) {
		status = poison_session();
		goto end;
	}
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		goto end;
	state->candidate_result = prepared;
	media_result = commit_candidate_image(state, candidate_store,
		&state->candidate_result);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = state->invariant_failure ? poison_session() :
			payload_mm_authvar_media_result_status(media_result);
		goto end;
	}
	status = verify_committed_candidate(state);
end:
	end_result = media_end(state);
	if (end_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		payload_mm_authvar_media_cache_invalidate();
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
	    end_result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		*published_volatile_modes = state->staged_volatile_modes;
out:
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		*published_volatile_modes = 0;
	memset(executor.sealed.arena, 0, executor.sealed.required_size);
release_busy:
	__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);
	return status;
}
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
struct coordinator_invocation {
	const struct payload_mm_authvar_policy_request *original_request;
	struct payload_mm_authvar_policy_request admitted_request;
	struct payload_mm_crypto_owner *owner;
	payload_mm_authvar_authority_verify_fn *verify;
	void *verify_context;
	size_t verify_context_size;
	bool trusted_physical_presence;
	const void *external_descriptor;
	const void *sealed_descriptor;
	size_t descriptor_size;
	void *external_result;
	const void *sealed_result;
	size_t result_size;
};

static bool immutable_digest(const void *data, size_t size,
	u8 digest[PAYLOAD_MM_SHA256_SIZE])
{
	if (!size) {
		memset(digest, 0, PAYLOAD_MM_SHA256_SIZE);
		return data == NULL;
	}
	return data && payload_mm_sha256(data, size, digest) == PAYLOAD_MM_VERIFY_OK;
}

static bool coordinator_crypto_clean(struct executor_session *state,
	struct payload_mm_crypto_owner *owner)
{
	bool clean = true;

	if (!payload_mm_crypto_idle()) {
		if (!payload_mm_crypto_abort_active())
			state->invariant_failure = true;
		clean = false;
	}
	if (!payload_mm_crypto_owner_is_clean(owner)) {
		if (!payload_mm_crypto_abort(owner))
			state->invariant_failure = true;
		clean = false;
	}
	if (!clean)
		state->invariant_failure = true;
	return clean;
}

static bool coordinator_invocation_unchanged(
	const struct coordinator_invocation *invocation,
	const u8 context_digest[PAYLOAD_MM_SHA256_SIZE])
{
	u8 check_digest[PAYLOAD_MM_SHA256_SIZE];

	return immutable_digest(invocation->verify_context,
			invocation->verify_context_size, check_digest) &&
		!memcmp(context_digest, check_digest, sizeof(check_digest)) &&
		immutable_digest(invocation->verify_context_size ?
			arena_at(executor.sealed.coordinator_context_offset) : NULL,
			invocation->verify_context_size, check_digest) &&
		!memcmp(context_digest, check_digest, sizeof(check_digest)) &&
		!memcmp(invocation->external_descriptor,
			invocation->sealed_descriptor, invocation->descriptor_size) &&
		!memcmp(invocation->original_request,
			&invocation->admitted_request,
			sizeof(invocation->admitted_request)) &&
		!memcmp(invocation->external_result, invocation->sealed_result,
			invocation->result_size);
}

static uint64_t __maybe_unused coordinate_transaction(
	const struct coordinator_invocation *invocation,
	enum payload_mm_authvar_authority_outcome *published_outcome,
	u8 *published_modes)
{
	struct payload_mm_authvar_coordinator_result prepared;
	struct payload_mm_authvar_coordinator_policy coordinator;
	struct payload_mm_authvar_candidate_binding binding;
	struct payload_mm_authvar_media_provider_scope provider_scope;
	struct executor_control_seal before_prepare;
	struct executor_session *state;
	enum payload_mm_authvar_media_result media_result;
	enum payload_mm_authvar_media_result end_result =
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	u8 context_digest[PAYLOAD_MM_SHA256_SIZE];
	u8 check_digest[PAYLOAD_MM_SHA256_SIZE];
	u8 snapshot_digest[PAYLOAD_MM_SHA256_SIZE];
	u8 source_modes;
	uint64_t status;
	uint32_t expected = 0;

	*published_outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NONE;
	*published_modes = 0U;
	if (provider_reentry())
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!__atomic_compare_exchange_n(&executor.busy, &expected, 1, false,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!executor.installed || !policy_equal() ||
	    !payload_mm_crypto_owner_is_clean(invocation->owner)) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto release_busy;
	}
	state = session();
	memset(state, 0, sizeof(*state));
	if (memcmp(invocation->original_request, &invocation->admitted_request,
		sizeof(invocation->admitted_request)) ||
	    !copy_request(state, &invocation->admitted_request)) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		goto out;
	}
	if (state->request.operation != PAYLOAD_MM_AUTHVAR_SERVICE_SET ||
	    !set_request_valid(&state->request)) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		goto out;
	}
	if (!immutable_digest(invocation->verify_context,
		invocation->verify_context_size, context_digest)) {
		status = poison_session();
		goto out;
	}
	if (invocation->verify_context_size) {
		memcpy(arena_at(executor.sealed.coordinator_context_offset),
			invocation->verify_context, invocation->verify_context_size);
		if (!coordinator_invocation_unchanged(invocation,
			context_digest)) {
			status = poison_session();
			goto out;
		}
	}
	state->at_runtime = executor.at_runtime;
	media_result = media_begin(state);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = payload_mm_authvar_media_result_status(media_result);
		goto no_lease;
	}
	payload_mm_authvar_media_cache_invalidate();
	if (!policy_equal() ||
	    !payload_mm_authvar_authority_snapshot(&state->contract) ||
	    !contract_allowed(&state->contract, &executor.sealed.limits)) {
		status = poison_session();
		goto end;
	}
	status = recover_session(state);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		goto end;
	if (!coordinator_crypto_clean(state, invocation->owner) ||
	    !coordinator_invocation_unchanged(invocation, context_digest)) {
		memcpy(invocation->external_result, invocation->sealed_result,
			invocation->result_size);
		status = poison_session();
		goto end;
	}
	if (!(executor.sealed_modes_need_reconcile ?
		payload_mm_authvar_coordinator_reconcile_modes(&state->index,
			state->at_runtime, executor.sealed_volatile_modes,
			&source_modes) :
		payload_mm_authvar_coordinator_source_modes(&state->index,
			state->at_runtime, executor.sealed_volatile_modes_valid,
			executor.sealed_volatile_modes, &source_modes))) {
		status = poison_session();
		goto end;
	}
	state->policy = (struct payload_mm_authvar_write_policy) {
		.maximum_name_size = executor.sealed.limits.maximum_name_size,
		.maximum_data_size = executor.sealed.limits.maximum_data_size,
		.maximum_record_size = executor.sealed.limits.maximum_record_size,
		.maximum_records = executor.sealed.limits.maximum_records,
	};
	if (state->policy.maximum_record_size > state->index.store_size)
		state->policy.maximum_record_size = state->index.store_size;
	if (state->policy.maximum_data_size > state->policy.maximum_record_size)
		state->policy.maximum_data_size = state->policy.maximum_record_size;
	binding = (struct payload_mm_authvar_candidate_binding) {
		.generation = state->generation,
		.token = state->token,
		.source_volatile_modes = source_modes,
		.at_runtime = state->at_runtime,
	};
	state->candidate_binding = binding;
	memset(&state->candidate_result, 0, sizeof(state->candidate_result));
	memset(&prepared, 0, sizeof(prepared));
	coordinator = (struct payload_mm_authvar_coordinator_policy) {
		.request = &state->request,
		.owner = invocation->owner,
		.verify = invocation->verify,
		.verify_context = invocation->verify_context_size ?
			arena_at(executor.sealed.coordinator_context_offset) : NULL,
		.verify_context_size = invocation->verify_context_size,
		.trusted_physical_presence =
			invocation->trusted_physical_presence,
	};
	if (!control_snapshot(state, &before_prepare, false) ||
	    payload_mm_sha256(snapshot(), state->contract.store_size,
		snapshot_digest) != PAYLOAD_MM_VERIFY_OK ||
	    !payload_mm_authvar_media_provider_enter(&provider_scope)) {
		status = poison_session();
		goto end;
	}
	__atomic_store_n(&executor.provider_active, 1, __ATOMIC_RELEASE);
	status = payload_mm_authvar_coordinator_prepare(&coordinator, &state->index,
		&state->policy, &binding, executor.ready_to_boot,
		arena_at(executor.sealed.mutation_data_offset),
		executor.sealed.limits.maximum_data_size,
		arena_at(executor.sealed.candidate_offset),
		state->ftw.variable_store_size,
		arena_at(executor.sealed.candidate_entries_offset),
		executor.sealed.limits.maximum_records, &prepared,
		&state->invariant_failure);
	if (!payload_mm_authvar_media_provider_leave(&provider_scope))
		state->invariant_failure = true;
	__atomic_store_n(&executor.provider_active, 0, __ATOMIC_RELEASE);
	(void)coordinator_crypto_clean(state, invocation->owner);
	if (!policy_equal() || !owner_equal(state) ||
	    __atomic_load_n(&executor.provider_violation, __ATOMIC_ACQUIRE) ||
	    payload_mm_authvar_media_provider_violated() ||
	    !coordinator_invocation_unchanged(invocation, context_digest) ||
	    !control_unchanged(state, &before_prepare, false) ||
	    !immutable_digest(invocation->verify_context,
		invocation->verify_context_size, check_digest) ||
	    memcmp(context_digest, check_digest, sizeof(check_digest)) ||
	    payload_mm_sha256(snapshot(), state->contract.store_size,
		check_digest) != PAYLOAD_MM_VERIFY_OK ||
	    memcmp(snapshot_digest, check_digest, sizeof(check_digest)) ||
	    memcmp(invocation->external_descriptor,
		invocation->sealed_descriptor, invocation->descriptor_size) ||
	    memcmp(invocation->original_request, &invocation->admitted_request,
		sizeof(invocation->admitted_request)) ||
	    memcmp(invocation->external_result, invocation->sealed_result,
		invocation->result_size)) {
		memcpy(invocation->external_result, invocation->sealed_result,
			invocation->result_size);
		state->invariant_failure = true;
	}
	if (state->invariant_failure) {
		status = poison_session();
		goto end;
	}
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
	    status != PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND)
		goto end;
	switch (prepared.outcome) {
	case PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION:
	case PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP:
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			status = poison_session();
			goto end;
		}
		break;
	case PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND:
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) {
			status = poison_session();
			goto end;
		}
		break;
	case PAYLOAD_MM_AUTHVAR_OUTCOME_NONE:
	default:
		status = poison_session();
		goto end;
	}
	if (prepared.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION) {
		executor.modes_need_reconcile = true;
		executor.sealed_modes_need_reconcile = true;
		state->candidate_result = prepared.candidate;
		media_result = commit_candidate_image(state,
			arena_at(executor.sealed.candidate_offset),
			&state->candidate_result);
		if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
			status = state->invariant_failure ? poison_session() :
				payload_mm_authvar_media_result_status(media_result);
			goto end;
		}
		status = verify_committed_candidate(state);
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
			goto end;
	}
	if (!coordinator_crypto_clean(state, invocation->owner) ||
	    !policy_equal() || !owner_equal(state) ||
	    !coordinator_invocation_unchanged(invocation, context_digest)) {
		memcpy(invocation->external_result, invocation->sealed_result,
			invocation->result_size);
		status = poison_session();
		goto end;
	}
	/*
	 * The verified durable image is authoritative even if releasing the media
	 * lease later fails. Keep the private lifecycle projection coherent now;
	 * scalar publication still waits for a successful media_end().
	 */
	executor.volatile_modes = prepared.volatile_modes;
	executor.sealed_volatile_modes = prepared.volatile_modes;
	executor.volatile_modes_valid = true;
	executor.sealed_volatile_modes_valid = true;
end:
	end_result = media_end(state);
	if (!coordinator_crypto_clean(state, invocation->owner) ||
	    !policy_equal() || !owner_released() ||
	    !coordinator_invocation_unchanged(invocation, context_digest)) {
		memcpy(invocation->external_result, invocation->sealed_result,
			invocation->result_size);
		status = poison_session();
	}
	if (end_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		payload_mm_authvar_media_cache_invalidate();
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	if ((status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
	     status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) &&
	    end_result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		executor.modes_need_reconcile = false;
		executor.sealed_modes_need_reconcile = false;
		*published_outcome = prepared.outcome;
		*published_modes = prepared.volatile_modes;
	}
	goto out;
no_lease:
	if (!coordinator_crypto_clean(state, invocation->owner) ||
	    !policy_equal() || !owner_released() ||
	    !coordinator_invocation_unchanged(invocation, context_digest)) {
		memcpy(invocation->external_result, invocation->sealed_result,
			invocation->result_size);
		status = poison_session();
	}
out:
	memset(executor.sealed.arena, 0, executor.sealed.required_size);
release_busy:
	__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);
	return status;
}
#endif

#if ENV_TEST && CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
static bool corrupt_coordinate_policy;

void payload_mm_authvar_executor_test_corrupt_coordinate_policy(void)
{
	corrupt_coordinate_policy = true;
}

static bool coordinator_spans_valid(const void *const parts[7],
	const size_t sizes[7])
{
	for (size_t left = 0U; left < 7U; left++) {
		if (sizes[left] && !external_protected_span(parts[left], sizes[left]))
			return false;
		for (size_t right = left + 1U; right < 7U; right++)
			if (sizes[right] && payload_mm_authvar_buffers_overlap(parts[left],
				sizes[left], parts[right], sizes[right]))
				return false;
	}
	return true;
}

bool payload_mm_authvar_executor_test_coordinator_spans(
	const void *const parts[7], const size_t sizes[7])
{
	return parts && sizes && coordinator_spans_valid(parts, sizes);
}

static bool coordinator_lengths_valid(size_t name_size, size_t data_size,
	size_t context_size)
{
	return name_size && data_size &&
		name_size <= executor.sealed.limits.maximum_name_size &&
		data_size <= executor.sealed.limits.maximum_data_size &&
		context_size <= PAYLOAD_MM_AUTHVAR_COORDINATOR_CONTEXT_MAX;
}

bool payload_mm_authvar_executor_test_coordinator_lengths(size_t name_size,
	size_t data_size, size_t context_size)
{
	return coordinator_lengths_valid(name_size, data_size, context_size);
}

static bool coordinator_test_inputs(
	const struct payload_mm_authvar_coordinator_test_request *request,
	struct payload_mm_authvar_coordinator_test_result *result,
	struct payload_mm_authvar_coordinator_test_request *copied,
	struct payload_mm_authvar_policy_request *copied_request)
{
	const void *parts[7];
	size_t sizes[7];

	if (!external_protected_span(request, sizeof(*request)) ||
	    !external_protected_span(result, sizeof(*result)) ||
	    (uintptr_t)request % _Alignof(*request) ||
	    (uintptr_t)result % _Alignof(*result))
		return false;
	memcpy(copied, request, sizeof(*copied));
	if (copied->revision != PAYLOAD_MM_AUTHVAR_COORDINATOR_TEST_REVISION ||
	    copied->size != sizeof(*copied) || !copied->request || !copied->owner ||
	    !copied->verify || copied->trusted_physical_presence > 1U ||
	    (uintptr_t)copied->owner % _Alignof(*copied->owner) ||
	    !bytes_all_zero(copied->reserved, sizeof(copied->reserved)) ||
	    (copied->verify_context == NULL) != (copied->verify_context_size == 0U) ||
	    !external_protected_span((const void *)(uintptr_t)copied->verify, 1U) ||
	    (uintptr_t)copied->request % _Alignof(*copied->request) ||
	    !external_protected_span(copied->request, sizeof(*copied->request)))
		return false;
	memcpy(copied_request, copied->request, sizeof(*copied_request));
	if (!copied_request->name || !copied_request->data ||
	    !coordinator_lengths_valid(copied_request->name_size,
		copied_request->data_size, copied->verify_context_size))
		return false;
	parts[0] = request; sizes[0] = sizeof(*request);
	parts[1] = result; sizes[1] = sizeof(*result);
	parts[2] = copied->request; sizes[2] = sizeof(*copied_request);
	parts[3] = copied_request->name; sizes[3] = copied_request->name_size;
	parts[4] = copied_request->data; sizes[4] = copied_request->data_size;
	parts[5] = copied->owner; sizes[5] = sizeof(*copied->owner);
	parts[6] = copied->verify_context; sizes[6] = copied->verify_context_size;
	if (!coordinator_spans_valid(parts, sizes))
		return false;
	return !memcmp(request, copied, sizeof(*copied)) &&
		!memcmp(copied->request, copied_request, sizeof(*copied_request));
}

uint64_t payload_mm_authvar_executor_test_coordinate(
	const struct payload_mm_authvar_coordinator_test_request *request,
	struct payload_mm_authvar_coordinator_test_result *result)
{
	struct payload_mm_authvar_coordinator_test_request copied;
	struct payload_mm_authvar_coordinator_test_result published = { 0 };
	struct payload_mm_authvar_policy_request copied_request;
	struct payload_mm_authvar_coordinator_test_result pending;
	struct coordinator_invocation invocation;
	enum payload_mm_authvar_authority_outcome outcome;
	u8 modes;
	uint64_t status;

	if (!executor.installed || !policy_equal() ||
	    !coordinator_test_inputs(request, result, &copied, &copied_request))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(result, 0, sizeof(*result));
	result->status = PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING;
	result->completion = PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
	memcpy(&pending, result, sizeof(pending));
	invocation = (struct coordinator_invocation) {
		.original_request = copied.request,
		.admitted_request = copied_request,
		.owner = copied.owner,
		.verify = copied.verify,
		.verify_context = copied.verify_context,
		.verify_context_size = copied.verify_context_size,
		.trusted_physical_presence = copied.trusted_physical_presence,
		.external_descriptor = request,
		.sealed_descriptor = &copied,
		.descriptor_size = sizeof(copied),
		.external_result = result,
		.sealed_result = &pending,
		.result_size = sizeof(pending),
	};
	if (corrupt_coordinate_policy) {
		corrupt_coordinate_policy = false;
		payload_mm_authvar_executor_test_corrupt_policy(true);
	}
	status = coordinate_transaction(&invocation, &outcome, &modes);
	if (!policy_equal())
		payload_mm_authvar_executor_test_corrupt_policy(false);
	published.status = status;
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
	    status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) {
		published.outcome = outcome;
		published.volatile_modes = modes;
	}
	memcpy(result, &published, offsetof(
		struct payload_mm_authvar_coordinator_test_result, completion));
	__atomic_store_n(&result->completion,
		PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE, __ATOMIC_RELEASE);
	return status;
}
#endif

uint64_t payload_mm_authvar_read_transaction(
	const struct payload_mm_authvar_read_request *request,
	struct payload_mm_authvar_read_result *completion)
{
	struct payload_mm_authvar_read_request copied;
	struct payload_mm_authvar_read_result published;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	struct payload_mm_authvar_view read_view;
	struct payload_mm_authvar_view_value value;
#else
	struct payload_mm_authvar_get_result get;
	struct payload_mm_authvar_next_result next;
#endif
	struct payload_mm_authvar_query_result query;
	struct payload_mm_authvar_store_policy query_policy;
	struct executor_session *state;
	enum payload_mm_authvar_media_result media_result;
	enum payload_mm_authvar_media_result end_result;
	const void *bytes;
	uint64_t status;
	uint32_t expected = 0;
	bool began = false;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	u8 source_modes;
	bool reconcile_modes = false;
	bool modes_validated = false;
#endif

	if (provider_reentry())
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!policy_equal()) {
		executor.installed = false;
		(void)payload_mm_authvar_media_fail_closed(0, 0);
		if (!read_request_disjoint(request, completion, &copied))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto complete_without_arena;
	}
	if (!read_request_disjoint(request, completion, &copied))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (copied.operation != PAYLOAD_MM_AUTHVAR_SERVICE_GET &&
	    copied.operation != PAYLOAD_MM_AUTHVAR_SERVICE_NEXT &&
	    copied.operation != PAYLOAD_MM_AUTHVAR_SERVICE_QUERY) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		goto complete_without_arena;
	}
	if (!read_operation_valid(&copied)) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		goto complete_without_arena;
	}
	if (!executor.installed) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto complete_without_arena;
	}
	if (!__atomic_compare_exchange_n(&executor.busy, &expected, 1, false,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	memset(completion, 0, sizeof(*completion));
	completion->status = PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING;
	completion->completion = PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
	state = session();
	memset(state, 0, sizeof(*state));
	if (!read_request_copy(state, request, &copied)) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		goto out;
	}
	media_result = media_begin(state);
	if (media_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = payload_mm_authvar_media_result_status(media_result);
		goto out;
	}
	began = true;
	payload_mm_authvar_media_cache_invalidate();
	if (!policy_equal() ||
	    !payload_mm_authvar_authority_snapshot(&state->contract) ||
	    !contract_allowed(&state->contract, &executor.sealed.limits)) {
		status = poison_session();
		goto end;
	}
	status = recover_session(state);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		goto end;
	memset(&state->read_result, 0, sizeof(state->read_result));
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	reconcile_modes = executor.sealed_modes_need_reconcile;
	if (!(reconcile_modes ?
		payload_mm_authvar_coordinator_reconcile_modes(&state->index,
			state->at_runtime, executor.sealed_volatile_modes,
			&source_modes) :
		payload_mm_authvar_coordinator_source_modes(&state->index,
			state->at_runtime, executor.sealed_volatile_modes_valid,
			executor.sealed_volatile_modes, &source_modes)) ||
	    payload_mm_authvar_view_init(&read_view, &state->index, source_modes,
		state->at_runtime) != CB_SUCCESS) {
		status = poison_session();
		goto end;
	}
	executor.volatile_modes = source_modes;
	executor.sealed_volatile_modes = source_modes;
	executor.volatile_modes_valid = true;
	executor.sealed_volatile_modes_valid = true;
	modes_validated = true;
#endif
	switch (state->request.operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		memset(&value, 0, sizeof(value));
		status = payload_mm_authvar_view_get(&read_view,
			state->request.vendor_guid, state->request.name,
			state->request.name_size, state->read_data_capacity, &value);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		    status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL) {
			state->read_result.required_data_size = value.data_size;
			state->read_result.attributes = value.attributes;
		}
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			bytes = value.data;
			if (!bytes || value.data_size > state->read_data_capacity) {
				status = poison_session();
				break;
			}
			memcpy(arena_at(executor.sealed.data_offset), bytes,
				value.data_size);
		} else if (status != PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL &&
			   status != PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND &&
			   status != PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
			   status != PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR) {
			status = poison_session();
		}
#else
		status = payload_mm_authvar_store_get(&state->index,
			state->request.vendor_guid, state->request.name,
			state->request.name_size, state->read_data_capacity,
			state->at_runtime, &get);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		    status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL) {
			state->read_result.required_data_size = get.required_data_size;
			state->read_result.attributes = get.attributes;
		}
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			bytes = payload_mm_authvar_store_data(&state->index, get.entry);
			if (!bytes || get.required_data_size > state->read_data_capacity) {
				status = poison_session();
				break;
			}
			memcpy(arena_at(executor.sealed.data_offset), bytes,
				get.required_data_size);
		}
#endif
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		memset(&value, 0, sizeof(value));
		status = payload_mm_authvar_view_get_next(&read_view,
			state->request.vendor_guid, state->request.name,
			state->request.name_size, state->read_name_capacity, &value);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		    status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)
			state->read_result.required_name_size = value.name_size;
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			bytes = value.name;
			if (!bytes || !value.vendor_guid ||
			    value.name_size > state->read_name_capacity) {
				status = poison_session();
				break;
			}
			memcpy(arena_at(executor.sealed.name_offset), bytes,
				value.name_size);
			memcpy(state->read_result.vendor_guid, value.vendor_guid,
				sizeof(state->read_result.vendor_guid));
		} else if (status != PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL &&
			   status != PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND &&
			   status != PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER) {
			status = poison_session();
		}
#else
		status = payload_mm_authvar_store_get_next(&state->index,
			state->request.vendor_guid, state->request.name,
			state->request.name_size, state->read_name_capacity,
			state->at_runtime, &next);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		    status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)
			state->read_result.required_name_size = next.required_name_size;
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			bytes = payload_mm_authvar_store_name(&state->index, next.entry);
			if (!bytes || next.required_name_size > state->read_name_capacity) {
				status = poison_session();
				break;
			}
			memcpy(arena_at(executor.sealed.name_offset), bytes,
				next.required_name_size);
			memcpy(state->read_result.vendor_guid, next.entry->vendor_guid,
				sizeof(state->read_result.vendor_guid));
		}
#endif
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		query_policy.maximum_storage = state->index.store_size -
			PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
		query_policy.maximum_record_size =
			executor.sealed.limits.maximum_record_size;
		if (query_policy.maximum_record_size > state->index.store_size)
			query_policy.maximum_record_size = state->index.store_size;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		status = payload_mm_authvar_view_query(&read_view, &query_policy,
			state->request.attributes, &query);
#else
		status = payload_mm_authvar_store_query(&state->index, &query_policy,
			state->request.attributes, state->at_runtime, &query);
#endif
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			state->read_result.maximum_storage = query.maximum_storage;
			state->read_result.remaining_storage = query.remaining_storage;
			state->read_result.maximum_variable = query.maximum_variable;
		}
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		    status != PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
		    status != PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED &&
		    status != PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR)
			status = poison_session();
#endif
		break;
	default:
		status = poison_session();
		break;
	}
	if (executor.installed && policy_equal() && owner_equal(state))
		payload_mm_authvar_media_cache_bind(state->generation, state->token);
end:
	end_result = media_end(state);
	if (end_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		payload_mm_authvar_media_cache_invalidate();
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		memset(&state->read_result, 0, sizeof(state->read_result));
	}
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	if (reconcile_modes && modes_validated && executor.installed &&
	    end_result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		executor.modes_need_reconcile = false;
		executor.sealed_modes_need_reconcile = false;
	}
#endif
	state->read_result.status = status;
	published = state->read_result;
	/* Publication is allowed only after the media session ended successfully. */
	if (end_result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		if (copied.name_capacity)
			memset(copied.result_name, 0, copied.name_capacity);
		if (copied.data_capacity)
			memset(copied.result_data, 0, copied.data_capacity);
	}
	if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		if (state->request.operation == PAYLOAD_MM_AUTHVAR_SERVICE_GET)
			memcpy(copied.result_data,
				arena_at(executor.sealed.data_offset),
				published.required_data_size);
		else if (state->request.operation == PAYLOAD_MM_AUTHVAR_SERVICE_NEXT)
			memcpy(copied.result_name,
				arena_at(executor.sealed.name_offset),
				published.required_name_size);
	}
out:
	memset(executor.sealed.arena, 0, executor.sealed.required_size);
	if (!began)
		memset(&published, 0, sizeof(published));
	published.status = status;
	memset(completion, 0, sizeof(*completion));
	memcpy(completion, &published, offsetof(struct payload_mm_authvar_read_result,
		completion));
	__atomic_store_n(&completion->completion,
		PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE, __ATOMIC_RELEASE);
	__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);
	return status;

complete_without_arena:
	memset(&published, 0, sizeof(published));
	published.status = status;
	memset(completion, 0, sizeof(*completion));
	memcpy(completion, &published, offsetof(struct payload_mm_authvar_read_result,
		completion));
	__atomic_store_n(&completion->completion,
		PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE, __ATOMIC_RELEASE);
	return status;
}

uint64_t payload_mm_authvar_policy_transaction(
	const struct payload_mm_authvar_policy_request *request,
	struct payload_mm_authvar_policy_result *completion)
{
	struct executor_session *state;
	const struct payload_mm_authvar_store_entry *replaced;
	const struct payload_mm_authvar_store_entry *final;
	const struct payload_mm_authvar_record_source *writer_source;
	struct payload_mm_authvar_store_limits scan_limits;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	struct payload_mm_authvar_set_snapshot set_snapshot;
	struct payload_mm_authvar_set_plan set_plan;
	struct payload_mm_authvar_view current_view;
#endif
	enum payload_mm_authvar_media_result result;
	enum payload_mm_authvar_media_result end_result;
	uint64_t status;
	uint32_t expected = 0;
	uint32_t old_data_size = 0;
	bool append;
	bool deletion;
	bool expected_present;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	bool reconcile_modes = false;
	bool modes_validated = false;
	u8 source_modes;
#endif
	uint32_t store_base;

	if (provider_reentry())
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (!policy_equal()) {
		executor.installed = false;
		(void)payload_mm_authvar_media_fail_closed(0, 0);
		if (!request_disjoint(request, completion))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto complete;
	}
	if (!request_disjoint(request, completion))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(completion, 0, sizeof(*completion));
	completion->completion = PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
	completion->status = PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING;
	if (!executor.installed) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto complete;
	}
#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	if (!executor.sealed.provider.authorize) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto complete;
	}
#endif
	if (!__atomic_compare_exchange_n(&executor.busy, &expected, 1, false,
		__ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto complete;
	}
	state = session();
	memset(state, 0, sizeof(*state));
	if (!copy_request(state, request)) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		goto out;
	}
	if (state->request.operation != PAYLOAD_MM_AUTHVAR_SERVICE_SET &&
	    state->request.operation != PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT &&
	    state->request.operation != PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		goto out;
	}
	if (state->request.operation == PAYLOAD_MM_AUTHVAR_SERVICE_SET) {
		if (!set_request_valid(&state->request)) {
			status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
			goto out;
		}
	} else {
		static const uint8_t zero_guid[16];

		if (state->request.name_size || state->request.data_size ||
		    state->request.attributes ||
		    memcmp(state->request.vendor_guid, zero_guid, sizeof(zero_guid))) {
			status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
			goto out;
		}
	}
	state->at_runtime = executor.at_runtime;
	result = media_begin(state);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = payload_mm_authvar_media_result_status(result);
		goto out;
	}
	payload_mm_authvar_media_cache_invalidate();
	if (!policy_equal() ||
	    !payload_mm_authvar_authority_snapshot(&state->contract) ||
	    !contract_allowed(&state->contract, &executor.sealed.limits)) {
		status = poison_session();
		goto end;
	}
	status = recover_session(state);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		goto end;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	reconcile_modes = executor.sealed_modes_need_reconcile;
	if (!(reconcile_modes ?
		payload_mm_authvar_coordinator_reconcile_modes(&state->index,
			state->at_runtime, executor.sealed_volatile_modes,
			&source_modes) :
		payload_mm_authvar_coordinator_source_modes(&state->index,
			state->at_runtime, executor.sealed_volatile_modes_valid,
			executor.sealed_volatile_modes, &source_modes)) ||
	    payload_mm_authvar_view_init(&current_view, &state->index, source_modes,
		state->at_runtime) != CB_SUCCESS) {
		status = poison_session();
		goto end;
	}
	executor.volatile_modes = source_modes;
	executor.sealed_volatile_modes = source_modes;
	executor.volatile_modes_valid = true;
	executor.sealed_volatile_modes_valid = true;
	modes_validated = true;
#endif
	if (state->request.operation != PAYLOAD_MM_AUTHVAR_SERVICE_SET) {
		executor.ready_to_boot = true;
		executor.sealed_ready_to_boot = true;
		if (state->request.operation == PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME) {
			executor.at_runtime = true;
			executor.sealed_at_runtime = true;
		}
		payload_mm_authvar_media_cache_bind(state->generation, state->token);
		goto end;
	}
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	set_snapshot = (struct payload_mm_authvar_set_snapshot) {
		.request = &state->request,
		.index = &state->index,
		.at_runtime = state->at_runtime,
		/* No production physical-presence source is published yet. */
		.trusted_physical_presence = false,
	};
	status = payload_mm_authvar_set_preflight(&set_snapshot, &set_plan);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		goto end;
	if (set_plan.kind == PAYLOAD_MM_AUTHVAR_SET_AUTH2) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		goto end;
	}
	if (set_plan.kind == PAYLOAD_MM_AUTHVAR_SET_NOOP) {
		payload_mm_authvar_media_cache_bind(state->generation, state->token);
		goto end;
	}
	memcpy(state->source.vendor_guid, state->request.vendor_guid,
		sizeof(state->source.vendor_guid));
	state->source.name = state->request.name;
	state->source.name_size = state->request.name_size;
	state->source.data = state->request.data;
	state->source.data_size = state->request.data_size;
	state->source.attributes = state->request.attributes;
#else
	status = authorize_mutation(state);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		goto end;
#endif
	if (state->policy.maximum_record_size > state->index.store_size)
		state->policy.maximum_record_size = state->index.store_size;
	if (state->policy.maximum_data_size > state->policy.maximum_record_size)
		state->policy.maximum_data_size = state->policy.maximum_record_size;
	replaced = payload_mm_authvar_store_find(&state->index,
		state->source.vendor_guid, state->source.name,
		state->source.name_size);
	if (replaced) {
		old_data_size = replaced->data_size;
	}
	append = !!(state->source.attributes &
		PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE);
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	deletion = set_plan.kind == PAYLOAD_MM_AUTHVAR_SET_ORDINARY_DELETE;
#else
	deletion = !state->source.data_size && !append;
#endif
	writer_source = deletion ? NULL : &state->source;
	state->reclaim.copies = arena_at(executor.sealed.copies_offset);
	state->reclaim.copy_capacity = executor.sealed.limits.maximum_records;
	status = payload_mm_authvar_write_plan_build(&state->index, replaced,
		writer_source, &state->policy, state->at_runtime,
		arena_at(executor.sealed.record_offset),
		executor.sealed.limits.maximum_record_size, &state->reclaim,
		&state->write);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		goto end;
	if (state->write.action == PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM) {
		result = execute_reclaim(state, replaced);
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
			status = state->invariant_failure ? poison_session() :
				payload_mm_authvar_media_result_status(result);
			goto end;
		}
	} else if (state->write.action != PAYLOAD_MM_AUTHVAR_WRITE_NOOP) {
		result = execute_direct(state);
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
			status = state->invariant_failure ? poison_session() :
				payload_mm_authvar_media_result_status(result);
			goto end;
		}
	}
	result = verify_media(state, 0, snapshot(), state->contract.store_size);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = state->invariant_failure ? poison_session() :
			payload_mm_authvar_media_result_status(result);
		goto end;
	}
	result = snapshot_read(state);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		status = state->invariant_failure ? poison_session() :
			payload_mm_authvar_media_result_status(result);
		goto end;
	}
	if (payload_mm_authvar_ftw_plan(snapshot(), state->contract.store_size,
		state->contract.block_size, &state->ftw) != CB_SUCCESS ||
	    state->ftw.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    !ftw_store_base(state, &store_base)) {
		status = poison_session();
		goto end;
	}
	scan_limits = (struct payload_mm_authvar_store_limits) {
		.maximum_store_size = executor.sealed.limits.maximum_store_size,
		.maximum_name_size = executor.sealed.limits.maximum_name_size,
		.maximum_data_size = executor.sealed.limits.maximum_data_size,
		.maximum_records = executor.sealed.limits.maximum_records,
	};
	memset(&state->index, 0, sizeof(state->index));
	state->index.entries = arena_at(executor.sealed.entries_offset);
	state->index.entry_capacity = executor.sealed.limits.maximum_records;
	if (payload_mm_authvar_store_scan(&state->index,
		snapshot() + store_base,
		state->ftw.variable_store_size, &scan_limits) != CB_SUCCESS) {
		status = poison_session();
		goto end;
	}
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	if (payload_mm_authvar_view_init(&current_view, &state->index, source_modes,
		state->at_runtime) != CB_SUCCESS) {
		status = poison_session();
		goto end;
	}
#endif
	final = payload_mm_authvar_store_find(&state->index,
		state->source.vendor_guid, state->source.name,
		state->source.name_size);
	expected_present = !deletion &&
		(state->write.action != PAYLOAD_MM_AUTHVAR_WRITE_NOOP || replaced);
	if ((!expected_present && final) || (expected_present && !final)) {
		status = poison_session();
		goto end;
	}
	if (final) {
		const uint8_t *final_data = payload_mm_authvar_store_data(&state->index,
			final);
		const uint8_t *final_record = snapshot() + store_base +
			final->record_offset;
		const uint8_t *canonical_record =
			arena_at(executor.sealed.record_offset);
		uint64_t expected_size = state->source.data_size;
		uint64_t physical_end = (uint64_t)final->data_offset +
			final->data_size;
		uint64_t physical_size;

		if (append && replaced)
			expected_size += old_data_size;
		physical_end = (physical_end + 3U) & ~3ULL;
		physical_size = physical_end >= final->record_offset ?
			physical_end - final->record_offset : UINT64_MAX;
		if (!final_data || expected_size != final->data_size ||
		    final->attributes != (state->source.attributes &
			~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) ||
		    (state->write.action != PAYLOAD_MM_AUTHVAR_WRITE_NOOP &&
		     (final->record_offset != state->write.record_offset ||
		      physical_size != state->write.record_size ||
		      state->write.record_size < 3U ||
		      canonical_record[2] != PAYLOAD_MM_AUTHVAR_STATE_ERASED ||
		      final_record[2] != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
		      memcmp(final_record, canonical_record, 2U) ||
		      memcmp(final_record + 3U, canonical_record + 3U,
			state->write.record_size - 3U))) ||
		    (state->source.data_size &&
		     memcmp(final_data + final->data_size - state->source.data_size,
			state->source.data, state->source.data_size))) {
			status = poison_session();
			goto end;
		}
	}
	payload_mm_authvar_media_cache_bind(state->generation, state->token);
	status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
end:
	end_result = media_end(state);
	if (end_result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		payload_mm_authvar_media_cache_invalidate();
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
			status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	if (reconcile_modes && modes_validated && executor.installed &&
	    policy_equal() && end_result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
		executor.modes_need_reconcile = false;
		executor.sealed_modes_need_reconcile = false;
	}
#endif
out:
	memset(executor.sealed.arena, 0, executor.sealed.required_size);
	memset(completion, 0, sizeof(*completion));
	completion->status = status;
	__atomic_store_n(&completion->completion, PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE,
		__ATOMIC_RELEASE);
	__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);
	return status;
complete:
	memset(completion, 0, sizeof(*completion));
	completion->status = status;
	__atomic_store_n(&completion->completion, PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE,
		__ATOMIC_RELEASE);
	return status;
}
