/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear_executor.h>
#include <commonlib/helpers.h>
#include <limits.h>
#include <string.h>

#include "payload_mm_authvar_mor_clear_internal.h"

static bool excluded_range(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	uintptr_t base, size_t size)
{
	if (!size || base > UINTPTR_MAX - (size - 1U))
		return false;
	for (size_t index = 0; index < plan->span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span =
			&plan->spans[index];

		if (span->span_class == PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED &&
		    base >= span->base && size <= span->size &&
		    base - span->base <= span->size - size)
			return true;
	}
	return false;
}

static bool range_contains(const void *owner, size_t owner_size,
	uintptr_t member, size_t member_size)
{
	const uintptr_t base = (uintptr_t)owner;

	return object_valid(owner, owner_size, 1) && member_size &&
		member <= UINTPTR_MAX - (member_size - 1U) && member >= base &&
		member_size <= owner_size && member - base <= owner_size - member_size;
}

static bool live_storage_excluded(const struct payload_mm_authvar_mor_clear_plan *plan,
	const void *const *objects, const size_t *sizes, size_t count)
{
	for (size_t index = 0; index < count; index++)
		if (!excluded_range(plan, (uintptr_t)objects[index], sizes[index]))
			return false;
	return true;
}

static bool callbacks_owned(
	const struct payload_mm_authvar_mor_clear_executor_ops *ops)
{
	const uintptr_t callbacks[] = {
		(uintptr_t)ops->dma_snapshot,
		(uintptr_t)ops->map_window,
		(uintptr_t)ops->cache_writeback_invalidate,
		(uintptr_t)ops->fence,
		(uintptr_t)ops->unmap_window,
		(uintptr_t)ops->inventory_validate,
	};

	for (size_t index = 0; index < ARRAY_SIZE(callbacks); index++)
		if (!range_contains(ops->executable_owner,
			ops->executable_owner_size, callbacks[index], 1))
			return false;
	return true;
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
	const struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant)
{
	const void *objects[] = { workspace, plan, entry, ops, transcript, grant };
	const size_t sizes[] = { sizeof(*workspace), sizeof(*plan), sizeof(*entry),
		sizeof(*ops), sizeof(*transcript), sizeof(*grant) };
	const size_t alignments[] = { _Alignof(*workspace), _Alignof(*plan),
		_Alignof(*entry), _Alignof(*ops), _Alignof(*transcript),
		_Alignof(*grant) };

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

static bool disjoint_from_public(const void *object, size_t size,
	const struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant)
{
	return object_valid(object, size, 1) &&
		!ranges_overlap(object, size, workspace, sizeof(*workspace)) &&
		!ranges_overlap(object, size, plan, sizeof(*plan)) &&
		!ranges_overlap(object, size, entry, sizeof(*entry)) &&
		!ranges_overlap(object, size, ops, sizeof(*ops)) &&
		!ranges_overlap(object, size, transcript, sizeof(*transcript)) &&
		!ranges_overlap(object, size, grant, sizeof(*grant));
}

static bool ops_preflight_valid(
	const struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant)
{
	return disjoint_from_public(ops->context, ops->context_size, workspace, plan,
			entry, ops, transcript, grant) &&
		disjoint_from_public(ops->inventory_context,
			ops->inventory_context_size, workspace, plan, entry, ops,
			transcript, grant) &&
		disjoint_from_public(ops->executable_owner,
			ops->executable_owner_size, workspace, plan, entry, ops,
			transcript, grant) &&
		object_valid(ops->stack_owner, ops->stack_owner_size, 1);
}

enum workspace_lifecycle {
	WORKSPACE_EMPTY,
	WORKSPACE_ACTIVE,
	WORKSPACE_TERMINAL,
};

static bool workspace_claim(
	struct payload_mm_authvar_mor_clear_workspace *workspace)
{
	uint8_t previous = __atomic_load_n(&workspace->lifecycle,
		__ATOMIC_ACQUIRE);
	uint8_t next;

	do {
		next = previous == WORKSPACE_EMPTY ? WORKSPACE_ACTIVE :
			WORKSPACE_TERMINAL;
	} while (!__atomic_compare_exchange_n(&workspace->lifecycle, &previous,
		next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
	return previous == WORKSPACE_EMPTY;
}

static __noinline void scrub_workspace(void *buffer,
	size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
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
	uint64_t cold_boot_generation;
	struct executor_iteration iteration;
	struct payload_mm_authvar_mor_clear_plan plan_snapshot;
	struct payload_mm_authvar_mor_entry entry_snapshot;
	struct payload_mm_authvar_mor_clear_executor_ops ops_snapshot;
	struct payload_mm_authvar_mor_clear_facts facts;
	struct payload_mm_authvar_mor_clear_transcript candidate;
	struct payload_mm_authvar_mor_grant grant_candidate;
	const void *protected[10];
	size_t protected_sizes[10];
} __aligned(8);

_Static_assert(sizeof(struct executor_state) <=
	PAYLOAD_MM_AUTHVAR_MOR_CLEAR_WORKSPACE_BYTES,
	"MOR clear workspace is too small");

static bool execution_boundary_unchanged(const struct executor_state *state,
	const struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant)
{
	return __atomic_load_n(&workspace->lifecycle, __ATOMIC_ACQUIRE) ==
		WORKSPACE_ACTIVE &&
		workspace->revision == PAYLOAD_MM_AUTHVAR_MOR_CLEAR_WORKSPACE_REVISION &&
		workspace->size == sizeof(*workspace) &&
		bytes_zero(workspace->reserved, sizeof(workspace->reserved)) &&
		!memcmp(&state->plan_snapshot, state->plan_input,
			sizeof(state->plan_snapshot)) &&
		!memcmp(&state->entry_snapshot, state->entry_input,
			sizeof(state->entry_snapshot)) &&
		!memcmp(&state->ops_snapshot, state->ops_input,
			sizeof(state->ops_snapshot)) &&
		bytes_zero(transcript, sizeof(*transcript)) &&
		bytes_zero(grant, sizeof(*grant));
}

static uint64_t workspace_digest(const struct executor_state *state)
{
	const volatile uint8_t *bytes = (const volatile uint8_t *)state;
	uint64_t digest = 1469598103934665603ULL;

	for (size_t index = 0; index < sizeof(*state); index++) {
		digest ^= bytes[index];
		digest *= 1099511628211ULL;
	}
	return digest;
}

static bool callback_unchanged(const struct executor_state *state,
	uint64_t digest,
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant)
{
	const bool unchanged = digest == workspace_digest(state) &&
		execution_boundary_unchanged(state, workspace, transcript, grant);

	if (!unchanged)
		__atomic_store_n(&workspace->lifecycle, WORKSPACE_TERMINAL,
			__ATOMIC_RELEASE);
	return unchanged;
}

static enum cb_err call_inventory(struct executor_state *state,
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant)
{
	enum cb_err (*callback)(void *, const struct payload_mm_authvar_mor_clear_plan *) =
		state->ops_snapshot.inventory_validate;
	void *context = state->ops_snapshot.inventory_context;
	const uint64_t digest = workspace_digest(state);
	const enum cb_err result = callback(context, state->plan_input);

	if (!callback_unchanged(state, digest, workspace, transcript, grant))
		return CB_ERR;
	return result;
}

static enum cb_err call_dma(struct executor_state *state,
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	enum cb_err (*callback)(void *, struct payload_mm_authvar_mor_clear_dma_snapshot *) =
		state->ops_snapshot.dma_snapshot;
	void *context = state->ops_snapshot.context;
	const uint64_t digest = workspace_digest(state);
	const enum cb_err result = callback(context, snapshot);

	if (!callback_unchanged(state, digest, workspace, transcript, grant))
		return CB_ERR;
	return result;
}

static enum cb_err call_map(struct executor_state *state,
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant,
	uint64_t physical, size_t size, void **mapping, bool *established)
{
	enum cb_err (*callback)(void *, uint64_t, size_t, void **) =
		state->ops_snapshot.map_window;
	void *context = state->ops_snapshot.context;
	const uint64_t digest = workspace_digest(state);
	const enum cb_err result = callback(context, physical, size, mapping);

	*established = result == CB_SUCCESS;
	if (!callback_unchanged(state, digest, workspace, transcript, grant))
		return CB_ERR;
	return result;
}

static enum cb_err call_cache(struct executor_state *state,
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant,
	uint64_t physical, const volatile void *mapping, size_t size)
{
	enum cb_err (*callback)(void *, uint64_t, const volatile void *, size_t) =
		state->ops_snapshot.cache_writeback_invalidate;
	void *context = state->ops_snapshot.context;
	const uint64_t digest = workspace_digest(state);
	const enum cb_err result = callback(context, physical, mapping, size);

	if (!callback_unchanged(state, digest, workspace, transcript, grant))
		return CB_ERR;
	return result;
}

static enum cb_err call_fence(struct executor_state *state,
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant)
{
	enum cb_err (*callback)(void *) = state->ops_snapshot.fence;
	void *context = state->ops_snapshot.context;
	const uint64_t digest = workspace_digest(state);
	const enum cb_err result = callback(context);

	if (!callback_unchanged(state, digest, workspace, transcript, grant))
		return CB_ERR;
	return result;
}

static enum cb_err call_unmap(struct executor_state *state,
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	const struct payload_mm_authvar_mor_grant *grant,
	uint64_t physical, void *mapping, size_t size, bool *unmapped)
{
	enum cb_err (*callback)(void *, uint64_t, void *, size_t) =
		state->ops_snapshot.unmap_window;
	void *context = state->ops_snapshot.context;
	const uint64_t digest = workspace_digest(state);
	const enum cb_err result = callback(context, physical, mapping, size);

	*unmapped = result == CB_SUCCESS;
	if (!callback_unchanged(state, digest, workspace, transcript, grant))
		return CB_ERR;
	return result;
}

static bool callback_context_disjoint(const struct executor_state *state,
	const void *context, size_t size)
{
	if (!object_valid(context, size, 1) ||
	    ranges_overlap(context, size, state, sizeof(*state)))
		return false;
	for (size_t index = 0; index < 6; index++)
		if (ranges_overlap(context, size, state->protected[index],
			state->protected_sizes[index]))
			return false;
	return true;
}

#if ENV_TEST
void payload_mm_authvar_mor_clear_executor_mapping_tamper_test(
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	uint64_t physical, void *mapping, size_t size)
{
	struct executor_state *state = (void *)workspace->storage;

	state->iteration.physical = physical;
	state->iteration.mapping = mapping;
	state->iteration.chunk = size;
}
#endif

enum cb_err payload_mm_authvar_mor_clear_execute(
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	uint64_t cold_boot_generation,
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant)
{
	struct executor_state *state;
	uint8_t expected;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma_candidate;
	void *mapping_candidate;
	enum cb_err callback_status;
	enum cb_err (*trusted_unmap)(void *context, uint64_t physical,
		void *mapping, size_t size);
	void *trusted_context;
	uint64_t trusted_physical = 0;
	size_t trusted_size = 0;
	void *trusted_mapping = NULL;
	bool map_established = false;
	bool map_active = false;
	bool unmap_completed;
	enum cb_err result = CB_ERR;

	/* Address-only validation must precede the first write or atomic claim. */
	if (!public_objects_valid(workspace, plan, entry, ops, transcript, grant) ||
	    !ops_preflight_valid(workspace, plan, entry, ops, transcript, grant))
		return CB_ERR_ARG;
	if (!workspace_claim(workspace))
		return CB_ERR;
	if (workspace->revision || workspace->size ||
	    !bytes_zero(workspace->reserved, sizeof(workspace->reserved)))
		goto fail_without_state;
	workspace->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_WORKSPACE_REVISION;
	workspace->size = sizeof(*workspace);
	memset(workspace->storage, 0, sizeof(workspace->storage));
	state = (void *)workspace->storage;
	*state = (struct executor_state) {
		.plan_input = plan,
		.entry_input = entry,
		.ops_input = ops,
		.cold_boot_generation = cold_boot_generation,
		.protected = { workspace, plan, entry, ops, transcript, grant },
		.protected_sizes = { sizeof(*workspace), sizeof(*plan), sizeof(*entry),
			sizeof(*ops), sizeof(*transcript), sizeof(*grant) },
	};
	memcpy(&state->plan_snapshot, state->plan_input, sizeof(state->plan_snapshot));
	memcpy(&state->entry_snapshot, state->entry_input, sizeof(state->entry_snapshot));
	memcpy(&state->ops_snapshot, state->ops_input, sizeof(state->ops_snapshot));
	state->protected[6] = state->ops_snapshot.context;
	state->protected_sizes[6] = state->ops_snapshot.context_size;
	state->protected[7] = state->ops_snapshot.inventory_context;
	state->protected_sizes[7] = state->ops_snapshot.inventory_context_size;
	state->protected[8] = state->ops_snapshot.executable_owner;
	state->protected_sizes[8] = state->ops_snapshot.executable_owner_size;
	state->protected[9] = state->ops_snapshot.stack_owner;
	state->protected_sizes[9] = state->ops_snapshot.stack_owner_size;
	memset(transcript, 0, sizeof(*transcript));
	memset(grant, 0, sizeof(*grant));
	if (payload_mm_authvar_mor_clear_plan_validate(&state->plan_snapshot) !=
		CB_SUCCESS ||
	    state->entry_snapshot.present != 1 ||
	    !(state->entry_snapshot.value & 1U) || state->entry_snapshot.reserved ||
	    !state->cold_boot_generation || !state->ops_snapshot.window_bytes ||
	    !state->ops_snapshot.inventory_validate ||
	    !callback_context_disjoint(state, state->ops_snapshot.context,
		state->ops_snapshot.context_size) ||
	    !callback_context_disjoint(state, state->ops_snapshot.inventory_context,
		state->ops_snapshot.inventory_context_size) ||
	    !live_storage_excluded(&state->plan_snapshot, state->protected,
		state->protected_sizes, ARRAY_SIZE(state->protected)) ||
	    !excluded_range(&state->plan_snapshot,
		(uintptr_t)state->ops_snapshot.context,
		state->ops_snapshot.context_size) ||
	    !excluded_range(&state->plan_snapshot,
		(uintptr_t)state->ops_snapshot.inventory_context,
		state->ops_snapshot.inventory_context_size) ||
	    !object_valid(state->ops_snapshot.executable_owner,
		state->ops_snapshot.executable_owner_size, 1) ||
	    !object_valid(state->ops_snapshot.stack_owner,
		state->ops_snapshot.stack_owner_size, 1) ||
	    !excluded_range(&state->plan_snapshot,
		(uintptr_t)state->ops_snapshot.executable_owner,
		state->ops_snapshot.executable_owner_size) ||
	    !excluded_range(&state->plan_snapshot,
		(uintptr_t)state->ops_snapshot.stack_owner,
		state->ops_snapshot.stack_owner_size) ||
	    !callbacks_owned(&state->ops_snapshot) ||
	    !state->ops_snapshot.dma_snapshot || !state->ops_snapshot.map_window ||
	    !state->ops_snapshot.cache_writeback_invalidate ||
	    !state->ops_snapshot.fence || !state->ops_snapshot.unmap_window ||
	    memcmp(&state->plan_snapshot, state->plan_input,
		sizeof(state->plan_snapshot)) ||
	    memcmp(&state->entry_snapshot, state->entry_input,
		sizeof(state->entry_snapshot)) ||
	    memcmp(&state->ops_snapshot, state->ops_input,
		sizeof(state->ops_snapshot)))
		goto fail;
	trusted_unmap = state->ops_snapshot.unmap_window;
	trusted_context = state->ops_snapshot.context;

	state->facts.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	state->facts.size = sizeof(state->facts);
	state->facts.cold_boot_generation = state->cold_boot_generation;
	state->facts.inventory_generation = state->plan_snapshot.inventory_generation;
	memcpy(state->facts.inventory_identity, state->plan_snapshot.inventory_identity,
		sizeof(state->facts.inventory_identity));
	if (call_inventory(state, workspace, transcript, grant) != CB_SUCCESS)
		goto fail;
	memset(&dma_candidate, 0, sizeof(dma_candidate));
	if (call_dma(state, workspace, transcript, grant, &dma_candidate) !=
		CB_SUCCESS ||
	    payload_mm_authvar_mor_clear_dma_snapshot_validate(&dma_candidate) !=
		CB_SUCCESS)
		goto fail;
	state->facts.dma_before = dma_candidate;

	state->candidate.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	state->candidate.size = sizeof(state->candidate);
	state->candidate.cold_boot_generation = state->cold_boot_generation;
	state->candidate.entry = state->entry_snapshot;
	state->candidate.dma_before = state->facts.dma_before;
	state->candidate.inventory_generation = state->plan_snapshot.inventory_generation;
	memcpy(state->candidate.inventory_identity,
		state->plan_snapshot.inventory_identity,
		sizeof(state->candidate.inventory_identity));

	for (state->iteration.index = 0;
	     state->iteration.index < state->plan_snapshot.span_count;
	     state->iteration.index++) {
		state->iteration.span =
			&state->plan_snapshot.spans[state->iteration.index];
		state->iteration.offset = 0;
		if (state->iteration.span->span_class !=
		    PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED)
			continue;
		state->iteration.record =
			&state->candidate.records[state->iteration.record_index++];
		state->iteration.record->base = state->iteration.span->base;
		state->iteration.record->size = state->iteration.span->size;
		while (state->iteration.offset < state->iteration.span->size) {
			state->iteration.remaining =
				state->iteration.span->size - state->iteration.offset;
			state->iteration.physical =
				state->iteration.span->base + state->iteration.offset;
			state->iteration.chunk = physical_window_chunk(
				state->iteration.physical, state->iteration.remaining,
				state->ops_snapshot.window_bytes);
			mapping_candidate = NULL;
			map_established = false;
			trusted_physical = state->iteration.physical;
			trusted_size = state->iteration.chunk;
			callback_status = state->iteration.chunk ?
				call_map(state, workspace, transcript, grant,
				trusted_physical, trusted_size,
				&mapping_candidate, &map_established) : CB_ERR;
			if (map_established) {
				trusted_mapping = mapping_candidate;
				map_active = true;
			}
			if (callback_status != CB_SUCCESS)
				goto fail;
			state->iteration.mapping = mapping_candidate;
			if (!mapping_safe(state->iteration.mapping, state->iteration.chunk,
				state->protected, state->protected_sizes,
				ARRAY_SIZE(state->protected), state, sizeof(*state))) {
				goto fail;
			}
			for (state->iteration.byte = 0;
			     state->iteration.byte < state->iteration.chunk;
			     state->iteration.byte++)
				((volatile uint8_t *)state->iteration.mapping)
					[state->iteration.byte] = 0;
			state->iteration.chunk_error =
				call_cache(state, workspace, transcript, grant,
					state->iteration.physical,
					state->iteration.mapping, state->iteration.chunk);
			if (__atomic_load_n(&workspace->lifecycle, __ATOMIC_ACQUIRE) !=
			    WORKSPACE_ACTIVE)
				goto fail;
			state->iteration.fence_error =
				call_fence(state, workspace, transcript, grant);
			if (__atomic_load_n(&workspace->lifecycle, __ATOMIC_ACQUIRE) !=
			    WORKSPACE_ACTIVE)
				goto fail;
			if (state->iteration.chunk_error == CB_SUCCESS)
				state->iteration.chunk_error = state->iteration.fence_error;
			unmap_completed = false;
			callback_status = call_unmap(state, workspace, transcript, grant,
				state->iteration.physical, state->iteration.mapping,
				state->iteration.chunk, &unmap_completed);
			if (unmap_completed)
				map_active = false;
			if (__atomic_load_n(&workspace->lifecycle, __ATOMIC_ACQUIRE) !=
			    WORKSPACE_ACTIVE || callback_status != CB_SUCCESS)
				goto fail;
			if (state->iteration.chunk_error == CB_SUCCESS)
				state->iteration.chunk_error = callback_status;
			if (state->iteration.chunk_error != CB_SUCCESS)
				goto fail;
			state->iteration.record->written_bytes += state->iteration.chunk;
			state->iteration.record->cache_writeback_fenced_bytes +=
				state->iteration.chunk;
			state->iteration.offset += state->iteration.chunk;
		}
	}

	/* Readback is deliberately a separate pass after every write is durable. */
	state->iteration.record_index = 0;
	for (state->iteration.index = 0;
	     state->iteration.index < state->plan_snapshot.span_count;
	     state->iteration.index++) {
		state->iteration.span =
			&state->plan_snapshot.spans[state->iteration.index];
		state->iteration.offset = 0;
		if (state->iteration.span->span_class !=
		    PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED)
			continue;
		state->iteration.record =
			&state->candidate.records[state->iteration.record_index++];
		while (state->iteration.offset < state->iteration.span->size) {
			state->iteration.remaining =
				state->iteration.span->size - state->iteration.offset;
			state->iteration.physical =
				state->iteration.span->base + state->iteration.offset;
			state->iteration.chunk = physical_window_chunk(
				state->iteration.physical, state->iteration.remaining,
				state->ops_snapshot.window_bytes);
			mapping_candidate = NULL;
			map_established = false;
			trusted_physical = state->iteration.physical;
			trusted_size = state->iteration.chunk;
			callback_status = state->iteration.chunk ?
				call_map(state, workspace, transcript, grant,
				trusted_physical, trusted_size,
				&mapping_candidate, &map_established) : CB_ERR;
			if (map_established) {
				trusted_mapping = mapping_candidate;
				map_active = true;
			}
			if (callback_status != CB_SUCCESS)
				goto fail;
			state->iteration.mapping = mapping_candidate;
			if (!mapping_safe(state->iteration.mapping, state->iteration.chunk,
				state->protected, state->protected_sizes,
				ARRAY_SIZE(state->protected), state, sizeof(*state))) {
				goto fail;
			}
			state->iteration.chunk_error =
				call_cache(state, workspace, transcript, grant,
					state->iteration.physical,
					state->iteration.mapping, state->iteration.chunk);
			if (__atomic_load_n(&workspace->lifecycle, __ATOMIC_ACQUIRE) !=
			    WORKSPACE_ACTIVE)
				goto fail;
			state->iteration.fence_error =
				call_fence(state, workspace, transcript, grant);
			if (__atomic_load_n(&workspace->lifecycle, __ATOMIC_ACQUIRE) !=
			    WORKSPACE_ACTIVE)
				goto fail;
			if (state->iteration.chunk_error == CB_SUCCESS)
				state->iteration.chunk_error = state->iteration.fence_error;
			if (state->iteration.chunk_error == CB_SUCCESS)
				for (state->iteration.byte = 0;
				     state->iteration.byte < state->iteration.chunk;
				     state->iteration.byte++)
					if (((const volatile uint8_t *)state->iteration.mapping)
					    [state->iteration.byte])
						state->iteration.chunk_error = CB_ERR;
			unmap_completed = false;
			callback_status = call_unmap(state, workspace, transcript, grant,
				state->iteration.physical, state->iteration.mapping,
				state->iteration.chunk, &unmap_completed);
			if (unmap_completed)
				map_active = false;
			if (__atomic_load_n(&workspace->lifecycle, __ATOMIC_ACQUIRE) !=
			    WORKSPACE_ACTIVE || callback_status != CB_SUCCESS)
				goto fail;
			if (state->iteration.chunk_error == CB_SUCCESS)
				state->iteration.chunk_error = callback_status;
			if (state->iteration.chunk_error != CB_SUCCESS)
				goto fail;
			state->iteration.record->zero_readback_bytes += state->iteration.chunk;
			state->iteration.offset += state->iteration.chunk;
		}
	}
	state->candidate.cleared_span_count = state->iteration.record_index;

	memset(&dma_candidate, 0, sizeof(dma_candidate));
	if (call_dma(state, workspace, transcript, grant, &dma_candidate) !=
		CB_SUCCESS ||
	    payload_mm_authvar_mor_clear_dma_snapshot_validate(&dma_candidate) !=
		CB_SUCCESS ||
	    memcmp(&state->facts.dma_before, &dma_candidate,
		sizeof(state->facts.dma_before)))
		goto fail;
	state->facts.dma_after = dma_candidate;
	state->candidate.dma_after = dma_candidate;
	if (call_inventory(state, workspace, transcript, grant) != CB_SUCCESS ||
	    !execution_boundary_unchanged(state, workspace, transcript, grant) ||
	    payload_mm_authvar_mor_clear_receipt_build_owned(&state->plan_snapshot,
		&state->entry_snapshot, &state->facts, &state->candidate,
		&state->grant_candidate) != CB_SUCCESS ||
	    !execution_boundary_unchanged(state, workspace, transcript, grant))
		goto fail;
	expected = WORKSPACE_ACTIVE;
	if (!__atomic_compare_exchange_n(&workspace->lifecycle, &expected,
		WORKSPACE_TERMINAL, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto fail;
	memcpy(transcript, &state->candidate, sizeof(state->candidate));
	memcpy(grant, &state->grant_candidate,
		sizeof(state->grant_candidate));
	result = CB_SUCCESS;
	goto out;

fail:
	if (map_active)
		(void)trusted_unmap(trusted_context, trusted_physical,
			trusted_mapping, trusted_size);
	__atomic_store_n(&workspace->lifecycle, WORKSPACE_TERMINAL,
		__ATOMIC_RELEASE);
	memset(transcript, 0, sizeof(*transcript));
	memset(grant, 0, sizeof(*grant));
out:
	scrub_workspace(workspace->storage, sizeof(workspace->storage));
	return result;
fail_without_state:
	scrub_workspace(workspace->storage, sizeof(workspace->storage));
	__atomic_store_n(&workspace->lifecycle, WORKSPACE_TERMINAL,
		__ATOMIC_RELEASE);
	memset(transcript, 0, sizeof(*transcript));
	memset(grant, 0, sizeof(*grant));
	return CB_ERR;
}
