/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_executor.h>
#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_policy.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <boot/payload_mm_authvar_writer.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable executor must only be built in SMM"
#endif

#define EXECUTOR_ALIGNMENT ((size_t)__BIGGEST_ALIGNMENT__)
#define EXECUTOR_TRANSFER_SIZE 4096U
#define EXECUTOR_RECOVERY_LIMIT 16U
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
	struct payload_mm_authvar_policy_view view;
	struct payload_mm_authvar_read_result read_result;
	uint64_t generation;
	uint64_t token;
	uint32_t read_name_capacity;
	uint32_t read_data_capacity;
	uint32_t recovery_count;
	bool have_previous_ftw;
	bool invariant_failure;
	bool at_runtime;
};

struct executor_policy {
	void *arena;
	size_t arena_size;
	struct payload_mm_authvar_executor_limits limits;
	size_t snapshot_offset;
	size_t entries_offset;
	size_t copies_offset;
	size_t record_offset;
	size_t name_offset;
	size_t data_offset;
	size_t transfer_offset;
	size_t session_offset;
	size_t provider_seal_offset;
	size_t mutation_offset;
	size_t mutation_data_offset;
	size_t required_size;
	struct payload_mm_authvar_policy_provider provider;
};

static struct {
	struct executor_policy policy;
	struct executor_policy sealed;
	uint32_t install_attempted;
	uint32_t provider_install_attempted;
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
} executor;

static bool owner_equal(const struct executor_session *state)
{
	return executor.owner_generation && executor.owner_token &&
		executor.owner_generation == executor.sealed_owner_generation &&
		executor.owner_token == executor.sealed_owner_token &&
		state->generation == executor.sealed_owner_generation &&
		state->token == executor.sealed_owner_token;
}

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
		executor.at_runtime == executor.sealed_at_runtime;
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
	    !add_area(&cursor, entries_size, &policy->entries_offset) ||
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
	/* Byte-exact seal of all provider inputs and executor control, not a CRC. */
	policy->provider_seal_offset = cursor;
	if (!add_size(cursor, cursor, &cursor) ||
	    !add_area(&cursor, sizeof(struct payload_mm_authvar_policy_mutation),
		&policy->mutation_offset) ||
	    !add_area(&cursor, policy->limits.maximum_data_size,
		&policy->mutation_data_offset) ||
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
	struct payload_mm_authvar_policy_view view;
	struct payload_mm_authvar_read_result read_result;
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
};

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
	seal->view = state->view;
	seal->read_result = state->read_result;
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
		state->index.entry_capacity <= executor.sealed.limits.maximum_records;
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
	struct payload_mm_authvar_ftw_geometry geometry;

	return payload_mm_authvar_contract_valid(contract) &&
		contract->store_size <= limits->maximum_store_size &&
		contract->store_size <= UINT32_MAX && contract->block_size &&
		contract->erase_size && contract->erase_size <= EXECUTOR_TRANSFER_SIZE &&
		contract->block_size % contract->erase_size == 0 &&
		payload_mm_authvar_ftw_geometry(&geometry, contract->store_size,
			contract->block_size) == CB_SUCCESS;
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
	const struct payload_mm_authvar_ftw_geometry *geometry =
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
	const struct payload_mm_authvar_ftw_geometry *geometry =
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
	const struct payload_mm_authvar_ftw_geometry *geometry =
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
	const struct payload_mm_authvar_ftw_geometry *geometry =
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

static bool same_recovery(const struct payload_mm_authvar_ftw_plan *left,
	const struct payload_mm_authvar_ftw_plan *right)
{
	return left->action == right->action && left->workspace == right->workspace &&
		left->queue_disposition == right->queue_disposition &&
		left->queue_offset == right->queue_offset &&
		left->queue_entry_size == right->queue_entry_size;
}

static uint64_t poison_session(void)
{
	executor.installed = false;
	(void)payload_mm_authvar_media_fail_closed(
		executor.sealed_owner_generation, executor.sealed_owner_token);
	return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
}

static uint64_t recover_session(struct executor_session *state)
{
	for (state->recovery_count = 0;
	     state->recovery_count < EXECUTOR_RECOVERY_LIMIT;
	     state->recovery_count++) {
		enum payload_mm_authvar_media_result result;

		result = snapshot_read(state);
		if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
			return state->invariant_failure ? poison_session() :
				payload_mm_authvar_media_result_status(result);
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

static enum payload_mm_authvar_media_result execute_reclaim(
	struct executor_session *state,
	const struct payload_mm_authvar_store_entry *replaced)
{
	const struct payload_mm_authvar_ftw_geometry *geometry =
		&state->ftw.geometry;
	uint32_t image_store_base;
	uint32_t media_store_base;
	uint32_t queue = geometry->working_offset + state->ftw.queue_offset;
	uint8_t *header = snapshot() + queue;
	uint8_t *record_header = header + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE;
	uint8_t *image = snapshot() + geometry->spare_offset;
	uint8_t *record = arena_at(executor.sealed.record_offset);
	enum payload_mm_authvar_media_result result;
	uint32_t destination = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	if (!ftw_store_base(state, &media_store_base))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	image_store_base = media_store_base;
	if (state->ftw.queue_offset > geometry->working_size ||
	    PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE >
		geometry->working_size - state->ftw.queue_offset ||
	    state->reclaim.action != PAYLOAD_MM_AUTHVAR_SPACE_RECLAIM ||
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
		PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	result = checked_program(state, queue + 1U, header + 1U,
		PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE - 1U);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, queue + 1U, header + 1U,
			PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE - 1U);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state, queue,
			PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state, queue,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = checked_program(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + 1U,
			record_header + 1U, PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE - 1U);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + 1U,
			record_header + 1U, PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE - 1U);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = erase_span(state, geometry->spare_offset,
			geometry->spare_size);
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
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE,
			PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
			PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = erase_span(state, geometry->variable_offset,
			geometry->variable_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = checked_program(state, geometry->variable_offset, image,
			geometry->variable_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = verify_media(state, geometry->variable_offset, image,
			geometry->variable_size);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE,
			PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
			PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = advance_marker(state, queue,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE);
	if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		result = erase_span(state, geometry->spare_offset,
			geometry->spare_size);
	if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)
		return result;
	memmove(snapshot(), image, geometry->variable_size);
	memset(snapshot() + geometry->spare_offset, 0xff, geometry->spare_size);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;

contradiction:
	state->invariant_failure = true;
	return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

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
	    request->attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED)
		return false;
	for (size_t i = 0; i + 2U < request->name_size; i += 2U)
		if (!name[i] && !name[i + 1U])
			return false;
	return true;
}

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

uint64_t payload_mm_authvar_read_transaction(
	const struct payload_mm_authvar_read_request *request,
	struct payload_mm_authvar_read_result *completion)
{
	struct payload_mm_authvar_read_request copied;
	struct payload_mm_authvar_read_result published;
	struct payload_mm_authvar_get_result get;
	struct payload_mm_authvar_next_result next;
	struct payload_mm_authvar_query_result query;
	struct payload_mm_authvar_store_policy query_policy;
	struct executor_session *state;
	enum payload_mm_authvar_media_result media_result;
	enum payload_mm_authvar_media_result end_result;
	const void *bytes;
	uint64_t status;
	uint32_t expected = 0;
	bool began = false;

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
	switch (state->request.operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
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
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
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
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		query_policy.maximum_storage = state->index.store_size -
			PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
		query_policy.maximum_record_size =
			executor.sealed.limits.maximum_record_size;
		if (query_policy.maximum_record_size > state->index.store_size)
			query_policy.maximum_record_size = state->index.store_size;
		status = payload_mm_authvar_store_query(&state->index, &query_policy,
			state->request.attributes, state->at_runtime, &query);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			state->read_result.maximum_storage = query.maximum_storage;
			state->read_result.remaining_storage = query.remaining_storage;
			state->read_result.maximum_variable = query.maximum_variable;
		}
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
	enum payload_mm_authvar_media_result result;
	enum payload_mm_authvar_media_result end_result;
	uint64_t status;
	uint32_t expected = 0;
	uint32_t old_data_size = 0;
	bool append;
	bool deletion;
	bool expected_present;
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
	if (!executor.installed || !executor.sealed.provider.authorize) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto complete;
	}
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
	status = authorize_mutation(state);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		goto end;
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
	deletion = !state->source.data_size && !append;
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
out:
	memset(executor.sealed.arena, 0, executor.sealed.required_size);
	__atomic_store_n(&executor.busy, 0, __ATOMIC_RELEASE);
complete:
	memset(completion, 0, sizeof(*completion));
	completion->status = status;
	__atomic_store_n(&completion->completion, PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE,
		__ATOMIC_RELEASE);
	return status;
}
