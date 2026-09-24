/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear_executor.h>
#include <commonlib/helpers.h>
#include <limits.h>
#include <string.h>

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	const uintptr_t left_base = (uintptr_t)left;
	const uintptr_t right_base = (uintptr_t)right;

	if (!object_valid(left, left_size, 1) ||
	    !object_valid(right, right_size, 1))
		return true;
	if (left_base <= right_base)
		return right_base - left_base < left_size;
	return left_base - right_base < right_size;
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static size_t physical_window_chunk(uint64_t physical, uint64_t remaining,
	size_t window_bytes)
{
	size_t until_boundary;
	size_t bounded_remaining;

	if (!window_bytes)
		return 0;
	until_boundary = window_bytes - physical % window_bytes;
	bounded_remaining = remaining > window_bytes ? window_bytes :
		(size_t)remaining;

	return MIN(bounded_remaining, until_boundary);
}

static bool public_objects_valid(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant)
{
	const void *objects[] = { plan, entry, ops, transcript, grant };
	const size_t sizes[] = { sizeof(*plan), sizeof(*entry), sizeof(*ops),
		sizeof(*transcript), sizeof(*grant) };
	const size_t alignments[] = { _Alignof(*plan), _Alignof(*entry),
		_Alignof(*ops), _Alignof(*transcript), _Alignof(*grant) };

	for (size_t index = 0; index < ARRAY_SIZE(objects); index++) {
		if (!object_valid(objects[index], sizes[index], alignments[index]))
			return false;
		for (size_t other = 0; other < index; other++)
			if (ranges_overlap(objects[index], sizes[index],
				objects[other], sizes[other]))
				return false;
	}
	return true;
}

static bool mapping_safe(const void *mapping, size_t size,
	const void *const *protected, const size_t *protected_sizes,
	size_t protected_count, const void *state, size_t state_size)
{
	if (!object_valid(mapping, size, 1))
		return false;
	if (ranges_overlap(mapping, size, state, state_size))
		return false;
	for (size_t index = 0; index < protected_count; index++)
		if (ranges_overlap(mapping, size, protected[index],
			protected_sizes[index]))
			return false;
	return true;
}

static enum cb_err unmap_preserving_failure(
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	uint64_t physical, void *mapping, size_t size, enum cb_err first_error)
{
	const enum cb_err unmap_error = ops->unmap_window(ops->context,
		physical, mapping, size);

	if (first_error != CB_SUCCESS)
		return first_error;
	return unmap_error;
}

struct executor_iteration {
	size_t index;
	size_t byte;
	uint32_t record_index;
	uint64_t offset;
	uint64_t remaining;
	uint64_t physical;
	size_t chunk;
	void *mapping;
	enum cb_err chunk_error;
	enum cb_err fence_error;
	const struct payload_mm_authvar_mor_grant_span *span;
	struct payload_mm_authvar_mor_clear_record *record;
};

struct executor_state {
	const struct payload_mm_authvar_mor_clear_plan *plan_input;
	const struct payload_mm_authvar_mor_entry *entry_input;
	const struct payload_mm_authvar_mor_clear_executor_ops *ops_input;
	struct payload_mm_authvar_mor_clear_transcript *transcript_output;
	struct payload_mm_authvar_mor_grant *grant_output;
	uint64_t cold_boot_generation;
	struct executor_iteration iteration;
	struct payload_mm_authvar_mor_clear_plan plan_snapshot;
	struct payload_mm_authvar_mor_entry entry_snapshot;
	struct payload_mm_authvar_mor_clear_executor_ops ops_snapshot;
	struct payload_mm_authvar_mor_clear_facts facts;
	struct payload_mm_authvar_mor_clear_transcript candidate;
	struct payload_mm_authvar_mor_grant grant_candidate;
	const void *protected[5];
	size_t protected_sizes[5];
} __aligned(8);

static enum cb_err live_inventory_validate(const struct executor_state *state)
{
	return state->ops_snapshot.inventory_validate(
		state->ops_snapshot.inventory_context, state->plan_input);
}

static bool execution_boundary_unchanged(const struct executor_state *state)
{
	return !memcmp(&state->plan_snapshot, state->plan_input,
			sizeof(state->plan_snapshot)) &&
		!memcmp(&state->entry_snapshot, state->entry_input,
			sizeof(state->entry_snapshot)) &&
		!memcmp(&state->ops_snapshot, state->ops_input,
			sizeof(state->ops_snapshot)) &&
		bytes_zero(state->transcript_output,
			sizeof(*state->transcript_output)) &&
		bytes_zero(state->grant_output, sizeof(*state->grant_output));
}

enum cb_err payload_mm_authvar_mor_clear_execute(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	uint64_t cold_boot_generation,
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant)
{
	const bool transcript_valid = object_valid(transcript, sizeof(*transcript),
		_Alignof(*transcript));
	const bool grant_valid = object_valid(grant, sizeof(*grant), _Alignof(*grant));

	if (!transcript_valid || !grant_valid) {
		if (transcript_valid)
			memset(transcript, 0, sizeof(*transcript));
		if (grant_valid)
			memset(grant, 0, sizeof(*grant));
		return CB_ERR_ARG;
	}
	if (!public_objects_valid(plan, entry, ops, transcript, grant)) {
		memset(transcript, 0, sizeof(*transcript));
		memset(grant, 0, sizeof(*grant));
		return CB_ERR_ARG;
	}
	struct executor_state state = {
		.plan_input = plan,
		.entry_input = entry,
		.ops_input = ops,
		.transcript_output = transcript,
		.grant_output = grant,
		.cold_boot_generation = cold_boot_generation,
		.protected = { plan, entry, ops, transcript, grant },
		.protected_sizes = { sizeof(*plan), sizeof(*entry), sizeof(*ops),
			sizeof(*transcript), sizeof(*grant) },
	};
	memcpy(&state.plan_snapshot, state.plan_input, sizeof(state.plan_snapshot));
	memcpy(&state.entry_snapshot, state.entry_input, sizeof(state.entry_snapshot));
	memcpy(&state.ops_snapshot, state.ops_input, sizeof(state.ops_snapshot));
	memset(state.transcript_output, 0, sizeof(*state.transcript_output));
	memset(state.grant_output, 0, sizeof(*state.grant_output));
	if (payload_mm_authvar_mor_clear_plan_validate(&state.plan_snapshot) !=
		CB_SUCCESS ||
	    state.entry_snapshot.present != 1 ||
	    !(state.entry_snapshot.value & 1U) || state.entry_snapshot.reserved ||
	    !state.cold_boot_generation || !state.ops_snapshot.window_bytes ||
	    !state.ops_snapshot.inventory_validate ||
	    !state.ops_snapshot.dma_snapshot || !state.ops_snapshot.map_window ||
	    !state.ops_snapshot.cache_writeback_invalidate ||
	    !state.ops_snapshot.fence || !state.ops_snapshot.unmap_window ||
	    memcmp(&state.plan_snapshot, state.plan_input,
		sizeof(state.plan_snapshot)) ||
	    memcmp(&state.entry_snapshot, state.entry_input,
		sizeof(state.entry_snapshot)) ||
	    memcmp(&state.ops_snapshot, state.ops_input,
		sizeof(state.ops_snapshot)))
		goto fail;

	state.facts.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	state.facts.size = sizeof(state.facts);
	state.facts.cold_boot_generation = state.cold_boot_generation;
	state.facts.inventory_generation = state.plan_snapshot.inventory_generation;
	memcpy(state.facts.inventory_identity, state.plan_snapshot.inventory_identity,
		sizeof(state.facts.inventory_identity));
	if (live_inventory_validate(&state) != CB_SUCCESS ||
	    !execution_boundary_unchanged(&state) ||
	    state.ops_snapshot.dma_snapshot(state.ops_snapshot.context,
		&state.facts.dma_before) != CB_SUCCESS ||
	    payload_mm_authvar_mor_clear_dma_snapshot_validate(
		&state.facts.dma_before) != CB_SUCCESS)
		goto fail;

	state.candidate.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	state.candidate.size = sizeof(state.candidate);
	state.candidate.cold_boot_generation = state.cold_boot_generation;
	state.candidate.entry = state.entry_snapshot;
	state.candidate.dma_before = state.facts.dma_before;
	state.candidate.inventory_generation = state.plan_snapshot.inventory_generation;
	memcpy(state.candidate.inventory_identity,
		state.plan_snapshot.inventory_identity,
		sizeof(state.candidate.inventory_identity));

	for (state.iteration.index = 0;
	     state.iteration.index < state.plan_snapshot.span_count;
	     state.iteration.index++) {
		state.iteration.span =
			&state.plan_snapshot.spans[state.iteration.index];
		state.iteration.offset = 0;
		if (state.iteration.span->span_class !=
		    PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED)
			continue;
		state.iteration.record =
			&state.candidate.records[state.iteration.record_index++];
		state.iteration.record->base = state.iteration.span->base;
		state.iteration.record->size = state.iteration.span->size;
		while (state.iteration.offset < state.iteration.span->size) {
			state.iteration.remaining =
				state.iteration.span->size - state.iteration.offset;
			state.iteration.physical =
				state.iteration.span->base + state.iteration.offset;
			state.iteration.chunk = physical_window_chunk(
				state.iteration.physical, state.iteration.remaining,
				state.ops_snapshot.window_bytes);
			state.iteration.mapping = NULL;
			if (!state.iteration.chunk ||
			    state.ops_snapshot.map_window(state.ops_snapshot.context,
				state.iteration.physical, state.iteration.chunk,
				&state.iteration.mapping) != CB_SUCCESS)
				goto fail;
			if (!mapping_safe(state.iteration.mapping, state.iteration.chunk,
				state.protected, state.protected_sizes,
				ARRAY_SIZE(state.protected), &state, sizeof(state))) {
				(void)state.ops_snapshot.unmap_window(
					state.ops_snapshot.context, state.iteration.physical,
					state.iteration.mapping, state.iteration.chunk);
				goto fail;
			}
			for (state.iteration.byte = 0;
			     state.iteration.byte < state.iteration.chunk;
			     state.iteration.byte++)
				((volatile uint8_t *)state.iteration.mapping)
					[state.iteration.byte] = 0;
			state.iteration.chunk_error =
				state.ops_snapshot.cache_writeback_invalidate(
					state.ops_snapshot.context, state.iteration.physical,
					state.iteration.mapping, state.iteration.chunk);
			state.iteration.fence_error =
				state.ops_snapshot.fence(state.ops_snapshot.context);
			if (state.iteration.chunk_error == CB_SUCCESS)
				state.iteration.chunk_error = state.iteration.fence_error;
			state.iteration.chunk_error = unmap_preserving_failure(
				&state.ops_snapshot, state.iteration.physical,
				state.iteration.mapping, state.iteration.chunk,
				state.iteration.chunk_error);
			if (state.iteration.chunk_error != CB_SUCCESS)
				goto fail;
			state.iteration.record->written_bytes += state.iteration.chunk;
			state.iteration.record->cache_writeback_fenced_bytes +=
				state.iteration.chunk;
			state.iteration.offset += state.iteration.chunk;
		}
	}

	/* Readback is deliberately a separate pass after every write is durable. */
	state.iteration.record_index = 0;
	for (state.iteration.index = 0;
	     state.iteration.index < state.plan_snapshot.span_count;
	     state.iteration.index++) {
		state.iteration.span =
			&state.plan_snapshot.spans[state.iteration.index];
		state.iteration.offset = 0;
		if (state.iteration.span->span_class !=
		    PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED)
			continue;
		state.iteration.record =
			&state.candidate.records[state.iteration.record_index++];
		while (state.iteration.offset < state.iteration.span->size) {
			state.iteration.remaining =
				state.iteration.span->size - state.iteration.offset;
			state.iteration.physical =
				state.iteration.span->base + state.iteration.offset;
			state.iteration.chunk = physical_window_chunk(
				state.iteration.physical, state.iteration.remaining,
				state.ops_snapshot.window_bytes);
			state.iteration.mapping = NULL;
			if (!state.iteration.chunk ||
			    state.ops_snapshot.map_window(state.ops_snapshot.context,
				state.iteration.physical, state.iteration.chunk,
				&state.iteration.mapping) != CB_SUCCESS)
				goto fail;
			if (!mapping_safe(state.iteration.mapping, state.iteration.chunk,
				state.protected, state.protected_sizes,
				ARRAY_SIZE(state.protected), &state, sizeof(state))) {
				(void)state.ops_snapshot.unmap_window(
					state.ops_snapshot.context, state.iteration.physical,
					state.iteration.mapping, state.iteration.chunk);
				goto fail;
			}
			state.iteration.chunk_error =
				state.ops_snapshot.cache_writeback_invalidate(
					state.ops_snapshot.context, state.iteration.physical,
					state.iteration.mapping, state.iteration.chunk);
			state.iteration.fence_error =
				state.ops_snapshot.fence(state.ops_snapshot.context);
			if (state.iteration.chunk_error == CB_SUCCESS)
				state.iteration.chunk_error = state.iteration.fence_error;
			if (state.iteration.chunk_error == CB_SUCCESS)
				for (state.iteration.byte = 0;
				     state.iteration.byte < state.iteration.chunk;
				     state.iteration.byte++)
					if (((const volatile uint8_t *)state.iteration.mapping)
					    [state.iteration.byte])
						state.iteration.chunk_error = CB_ERR;
			state.iteration.chunk_error = unmap_preserving_failure(
				&state.ops_snapshot, state.iteration.physical,
				state.iteration.mapping, state.iteration.chunk,
				state.iteration.chunk_error);
			if (state.iteration.chunk_error != CB_SUCCESS)
				goto fail;
			state.iteration.record->zero_readback_bytes += state.iteration.chunk;
			state.iteration.offset += state.iteration.chunk;
		}
	}
	state.candidate.cleared_span_count = state.iteration.record_index;

	if (state.ops_snapshot.dma_snapshot(state.ops_snapshot.context,
		&state.facts.dma_after) != CB_SUCCESS ||
	    payload_mm_authvar_mor_clear_dma_snapshot_validate(
		&state.facts.dma_after) != CB_SUCCESS ||
	    memcmp(&state.facts.dma_before, &state.facts.dma_after,
		sizeof(state.facts.dma_before)))
		goto fail;
	state.candidate.dma_after = state.facts.dma_after;
	if (live_inventory_validate(&state) != CB_SUCCESS ||
	    !execution_boundary_unchanged(&state) ||
	    payload_mm_authvar_mor_clear_receipt_build(&state.plan_snapshot,
		&state.entry_snapshot, &state.facts, &state.candidate,
		&state.grant_candidate) != CB_SUCCESS ||
	    !execution_boundary_unchanged(&state))
		goto fail;
	memcpy(state.transcript_output, &state.candidate, sizeof(state.candidate));
	memcpy(state.grant_output, &state.grant_candidate,
		sizeof(state.grant_candidate));
	return CB_SUCCESS;

fail:
	memset(state.transcript_output, 0, sizeof(*state.transcript_output));
	memset(state.grant_output, 0, sizeof(*state.grant_output));
	return CB_ERR;
}
