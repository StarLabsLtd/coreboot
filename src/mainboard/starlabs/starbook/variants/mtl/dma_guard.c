/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_guard.h"

#include <limits.h>
#include <string.h>

#define VTD_ROOT_POINTER_SET (1U << 30)
#define VTD_TRANSLATION_ENABLE (1U << 31)
#define VTD_PROTECTED_MEMORY_ACTIVE (1U << 0)
#define VTD_PROTECTED_MEMORY_REQUEST (1U << 31)
#define VTD_BAR_ENABLED (1U << 0)
#define VTD_BAR_MASK 0xfffffffffffff000ULL

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static bool range_covered(const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_range *range, uint32_t reason)
{
	if (!range->size || range->base > UINT64_MAX - range->size)
		return false;
	for (size_t index = 0; index < plan->span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span = &plan->spans[index];

		if (span->span_class == PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED &&
		    span->exclusion_reason == reason && range->base >= span->base &&
		    range->size <= span->size &&
		    range->base - span->base <= span->size - range->size)
			return true;
	}
	return false;
}

static bool range_end(const struct starbook_mtl_dma_guard_range *range,
	uint64_t *end)
{
	if (!range->size || range->base > UINT64_MAX - range->size)
		return false;
	*end = range->base + range->size;
	return true;
}

static bool snapshot_structure_valid(
	const struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	uint64_t end;

	if (snapshot->engine_count != STARBOOK_MTL_DMA_GUARD_ENGINES ||
	    snapshot->arena_count != STARBOOK_MTL_DMA_GUARD_ARENAS ||
	    snapshot->engines[0].mode !=
		STARBOOK_MTL_DMA_GUARD_DEFAULT_DENY_TRANSLATION ||
	    snapshot->engines[1].mode != STARBOOK_MTL_DMA_GUARD_BME_QUIESCED ||
	    !snapshot->engines[0].base || !snapshot->engines[0].root ||
	    !snapshot->engines[1].base ||
	    snapshot->engines[0].base == snapshot->engines[1].base ||
	    (snapshot->engines[0].base & 0xfffU) ||
	    (snapshot->engines[1].base & 0xfffU) ||
	    (snapshot->engines[0].root & 0xfffU) ||
	    snapshot->engines[0].root != snapshot->table.base ||
	    (snapshot->engines[0].status &
	     (VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) !=
		(VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE) ||
	    (snapshot->engines[0].protected_memory_enable &
	     (VTD_PROTECTED_MEMORY_ACTIVE | VTD_PROTECTED_MEMORY_REQUEST)) ||
	    (snapshot->handoff.base & 0xfffU) ||
	    snapshot->handoff.size != 0x1000U ||
	    (snapshot->table.base & 0xfffU) || (snapshot->table.size & 0xfffU) ||
	    (snapshot->table_mirror.base & 0xfffU) ||
	    (snapshot->table_mirror.size & 0xfffU) ||
	    !range_end(&snapshot->handoff, &end) || end != snapshot->table.base ||
	    !range_end(&snapshot->table, &end) || end != snapshot->arenas[0].base ||
	    !range_end(&snapshot->table_mirror, &end) ||
	    end > snapshot->handoff.base)
		return false;
	for (size_t index = 0; index < STARBOOK_MTL_DMA_GUARD_ARENAS; index++) {
		if ((snapshot->arenas[index].base & 0xfffU) ||
		    (snapshot->arenas[index].size & 0xfffU) ||
		    !range_end(&snapshot->arenas[index], &end) ||
		    (index && snapshot->arenas[index].base !=
			snapshot->arenas[index - 1U].base +
			snapshot->arenas[index - 1U].size))
			return false;
	}
	return true;
}

static bool prepared_snapshot_valid(
	const struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	return snapshot->revision == STARBOOK_MTL_DMA_GUARD_REVISION &&
		snapshot->size == sizeof(*snapshot) &&
		!snapshot->generation &&
		bytes_zero(snapshot->identity, sizeof(snapshot->identity)) &&
		bytes_zero(snapshot->reserved, sizeof(snapshot->reserved)) &&
		snapshot_structure_valid(snapshot);
}

enum cb_err starbook_mtl_dma_guard_snapshot_build(
	const struct starbook_mtl_dma_guard_facts *facts,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	struct starbook_mtl_dma_guard_facts observed;
	struct starbook_mtl_dma_guard_snapshot candidate = { 0 };
	uint64_t live_end;
	uint64_t end;

	if (!snapshot || (uintptr_t)snapshot % _Alignof(*snapshot) ||
	    (uintptr_t)snapshot > (uintptr_t)-1 - (sizeof(*snapshot) - 1U))
		return CB_ERR_ARG;
	memset(snapshot, 0, sizeof(*snapshot));
	if (!facts || (uintptr_t)facts % _Alignof(*facts) ||
	    (uintptr_t)facts > (uintptr_t)-1 - (sizeof(*facts) - 1U) ||
	    ((uintptr_t)facts <= (uintptr_t)snapshot + sizeof(*snapshot) - 1U &&
	     (uintptr_t)snapshot <= (uintptr_t)facts + sizeof(*facts) - 1U))
		return CB_ERR_ARG;
	memcpy(&observed, facts, sizeof(observed));
	if (!observed.tables_match || !observed.integrated_requesters_verified ||
	    memcmp(&observed.live_buffer, &observed.current_fsp_buffer,
		sizeof(observed.live_buffer)) ||
	    memcmp(&observed.table_mirror, &observed.current_cbmem_mirror,
		 sizeof(observed.table_mirror)) ||
	    !range_end(&observed.live_buffer, &live_end) ||
	    observed.handoff.base != observed.live_buffer.base ||
	    !range_end(&observed.arenas[STARBOOK_MTL_DMA_GUARD_ARENAS - 1U],
		&end) || end != live_end ||
	    !(observed.gfxvtbar & VTD_BAR_ENABLED) ||
	    (observed.gfxvtbar & VTD_BAR_MASK) != observed.engines[1].base)
		return CB_ERR;
	if (memcmp(&observed, facts, sizeof(observed)))
		return CB_ERR;
	candidate.revision = STARBOOK_MTL_DMA_GUARD_REVISION;
	candidate.size = sizeof(candidate);
	candidate.engine_count = STARBOOK_MTL_DMA_GUARD_ENGINES;
	candidate.arena_count = STARBOOK_MTL_DMA_GUARD_ARENAS;
	memcpy(candidate.engines, observed.engines, sizeof(candidate.engines));
	candidate.handoff = observed.handoff;
	candidate.table = observed.table;
	candidate.table_mirror = observed.table_mirror;
	memcpy(candidate.arenas, observed.arenas, sizeof(candidate.arenas));
	if (!snapshot_structure_valid(&candidate))
		return CB_ERR;
	memcpy(snapshot, &candidate, sizeof(candidate));
	return CB_SUCCESS;
}

enum guard_phase {
	GUARD_EMPTY,
	GUARD_PREPARED,
	GUARD_BOUND,
	GUARD_POISONED,
};

static struct {
	enum guard_phase phase;
	struct starbook_mtl_dma_guard_snapshot baseline;
	struct payload_mm_authvar_mor_clear_plan bound_plan;
} guard_authority;

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static void guard_poison(const struct starbook_mtl_dma_guard_ops *ops)
{
	ops->poison(ops->context);
	memset(&guard_authority.baseline, 0, sizeof(guard_authority.baseline));
	memset(&guard_authority.bound_plan, 0, sizeof(guard_authority.bound_plan));
	guard_authority.phase = GUARD_POISONED;
}

enum cb_err starbook_mtl_dma_guard_policy_validate(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	struct payload_mm_authvar_mor_clear_plan plan_copy;
	struct starbook_mtl_dma_guard_snapshot snapshot_copy;
	const uintptr_t plan_base = (uintptr_t)plan;
	const uintptr_t snapshot_base = (uintptr_t)snapshot;

	if (!plan || plan_base > (uintptr_t)-1 - (sizeof(*plan) - 1U) ||
	    plan_base % _Alignof(*plan) || !snapshot ||
	    snapshot_base > (uintptr_t)-1 - (sizeof(*snapshot) - 1U) ||
	    (uintptr_t)snapshot % _Alignof(*snapshot) ||
	    (plan_base <= snapshot_base + sizeof(*snapshot) - 1U &&
	     snapshot_base <= plan_base + sizeof(*plan) - 1U))
		return CB_ERR_ARG;
	memcpy(&plan_copy, plan, sizeof(plan_copy));
	memcpy(&snapshot_copy, snapshot, sizeof(snapshot_copy));
	if (payload_mm_authvar_mor_clear_plan_validate(&plan_copy) != CB_SUCCESS ||
	    snapshot_copy.revision != STARBOOK_MTL_DMA_GUARD_REVISION ||
	    snapshot_copy.size != sizeof(snapshot_copy) ||
	    !snapshot_copy.generation ||
	    bytes_zero(snapshot_copy.identity, sizeof(snapshot_copy.identity)) ||
	    snapshot_copy.engine_count != STARBOOK_MTL_DMA_GUARD_ENGINES ||
	    snapshot_copy.arena_count != STARBOOK_MTL_DMA_GUARD_ARENAS ||
	    !bytes_zero(snapshot_copy.reserved, sizeof(snapshot_copy.reserved)) ||
	    !snapshot_structure_valid(&snapshot_copy) ||
	    plan_copy.inventory_generation != snapshot_copy.generation ||
	    memcmp(plan_copy.inventory_identity, snapshot_copy.identity,
		sizeof(plan_copy.inventory_identity)) ||
	    !range_covered(&plan_copy, &snapshot_copy.handoff,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE) ||
	    !range_covered(&plan_copy, &snapshot_copy.table,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE) ||
	    !range_covered(&plan_copy, &snapshot_copy.table_mirror,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE))
		return CB_ERR;
	for (size_t index = 0; index < STARBOOK_MTL_DMA_GUARD_ARENAS; index++)
		if (!range_covered(&plan_copy, &snapshot_copy.arenas[index],
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED))
			return CB_ERR;
	if (memcmp(&plan_copy, plan, sizeof(plan_copy)) ||
	    memcmp(&snapshot_copy, snapshot, sizeof(snapshot_copy)))
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_dma_guard_prepare_with_ops(
	struct starbook_mtl_dma_guard_snapshot *snapshot,
	const struct starbook_mtl_dma_guard_ops *ops)
{
	struct starbook_mtl_dma_guard_snapshot candidate;
	struct starbook_mtl_dma_guard_snapshot recheck;
	struct starbook_mtl_dma_guard_ops ops_copy;
	uint64_t random_words[5];
	bool identity_nonzero = false;
	const enum guard_phase original_phase = guard_authority.phase;

	if (!object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)))
		return CB_ERR_ARG;
	memset(snapshot, 0, sizeof(*snapshot));
	if (!object_valid(ops, sizeof(*ops), _Alignof(*ops)) ||
	    objects_overlap(snapshot, sizeof(*snapshot), ops, sizeof(*ops)))
		return CB_ERR_ARG;
	memcpy(&ops_copy, ops, sizeof(ops_copy));
	if (!ops_copy.ensure || !ops_copy.observe || !ops_copy.random64 ||
	    !ops_copy.poison ||
	    guard_authority.phase == GUARD_POISONED)
		return CB_ERR;
	guard_authority.phase = GUARD_POISONED;
	memset(&candidate, 0, sizeof(candidate));
	if (ops_copy.ensure(ops_copy.context) != CB_SUCCESS ||
	    ops_copy.observe(ops_copy.context, &candidate) != CB_SUCCESS ||
	    !prepared_snapshot_valid(&candidate))
		goto fail;
	if (original_phase == GUARD_PREPARED || original_phase == GUARD_BOUND) {
		candidate.generation = guard_authority.baseline.generation;
		memcpy(candidate.identity, guard_authority.baseline.identity,
			sizeof(candidate.identity));
	} else {
		for (size_t index = 0; index < 5U; index++)
			if (ops_copy.random64(ops_copy.context, &random_words[index]) !=
				CB_SUCCESS)
				goto fail;
		if (!random_words[0])
			goto fail;
		for (size_t index = 1; index < 5U; index++)
			identity_nonzero |= random_words[index] != 0;
		if (!identity_nonzero)
			goto fail;
		candidate.generation = random_words[0];
		memcpy(candidate.identity, &random_words[1],
			sizeof(candidate.identity));
	}
	if (original_phase != GUARD_EMPTY &&
	    memcmp(&candidate, &guard_authority.baseline, sizeof(candidate)))
		goto fail;
	memset(&recheck, 0, sizeof(recheck));
	if (ops_copy.observe(ops_copy.context, &recheck) != CB_SUCCESS ||
	    !prepared_snapshot_valid(&recheck))
		goto fail;
	recheck.generation = candidate.generation;
	memcpy(recheck.identity, candidate.identity, sizeof(recheck.identity));
	if (memcmp(&candidate, &recheck, sizeof(candidate)) ||
	    memcmp(&ops_copy, ops, sizeof(ops_copy)) ||
	    memcmp(snapshot, &(const struct starbook_mtl_dma_guard_snapshot) { 0 },
		sizeof(*snapshot)))
		goto fail;
	if (original_phase == GUARD_EMPTY)
		guard_authority.baseline = candidate;
	guard_authority.phase = original_phase == GUARD_BOUND ?
		GUARD_BOUND : GUARD_PREPARED;
	memcpy(snapshot, &candidate, sizeof(candidate));
	return CB_SUCCESS;

fail:
	guard_poison(&ops_copy);
	memset(snapshot, 0, sizeof(*snapshot));
	return CB_ERR;
}

enum cb_err starbook_mtl_dma_guard_bind_with_ops(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct starbook_mtl_dma_guard_snapshot *bound,
	struct payload_mm_authvar_mor_clear_dma_snapshot *dma,
	const struct starbook_mtl_dma_guard_ops *ops)
{
	struct payload_mm_authvar_mor_clear_plan plan_copy;
	struct starbook_mtl_dma_guard_snapshot prepared_copy;
	struct starbook_mtl_dma_guard_snapshot observed;
	struct starbook_mtl_dma_guard_ops ops_copy;
	const bool bound_valid = object_valid(bound, sizeof(*bound), _Alignof(*bound));
	const bool dma_valid = object_valid(dma, sizeof(*dma), _Alignof(*dma));
	const void *objects[] = { plan, prepared, bound, dma, ops };
	const size_t sizes[] = { sizeof(*plan), sizeof(*prepared), sizeof(*bound),
		sizeof(*dma), sizeof(*ops) };
	const size_t alignments[] = { _Alignof(*plan), _Alignof(*prepared),
		_Alignof(*bound), _Alignof(*dma), _Alignof(*ops) };

	if (bound_valid)
		memset(bound, 0, sizeof(*bound));
	if (dma_valid)
		memset(dma, 0, sizeof(*dma));
	if (!bound_valid || !dma_valid)
		return CB_ERR_ARG;
	for (size_t index = 0; index < ARRAY_SIZE(objects); index++) {
		if (!object_valid(objects[index], sizes[index], alignments[index]))
			return CB_ERR_ARG;
		for (size_t other = 0; other < index; other++)
			if (objects_overlap(objects[index], sizes[index], objects[other],
				sizes[other]))
				return CB_ERR_ARG;
	}
	memcpy(&plan_copy, plan, sizeof(plan_copy));
	memcpy(&prepared_copy, prepared, sizeof(prepared_copy));
	memcpy(&ops_copy, ops, sizeof(ops_copy));
	if (!ops_copy.ensure || !ops_copy.observe || !ops_copy.poison ||
	    guard_authority.phase == GUARD_EMPTY ||
	    guard_authority.phase == GUARD_POISONED)
		return CB_ERR;
	guard_authority.phase = GUARD_POISONED;
	if (memcmp(&prepared_copy, &guard_authority.baseline,
		sizeof(prepared_copy)) ||
	    starbook_mtl_dma_guard_policy_validate(&plan_copy, &prepared_copy) !=
		CB_SUCCESS ||
	    (guard_authority.bound_plan.revision &&
	     memcmp(&plan_copy, &guard_authority.bound_plan, sizeof(plan_copy))) ||
	    ops_copy.ensure(ops_copy.context) != CB_SUCCESS)
		goto fail;
	memset(&observed, 0, sizeof(observed));
	if (ops_copy.observe(ops_copy.context, &observed) != CB_SUCCESS ||
	    !prepared_snapshot_valid(&observed))
		goto fail;
	observed.generation = guard_authority.baseline.generation;
	memcpy(observed.identity, guard_authority.baseline.identity,
		sizeof(observed.identity));
	if (memcmp(&observed, &guard_authority.baseline, sizeof(observed)) ||
	    memcmp(&plan_copy, plan, sizeof(plan_copy)) ||
	    memcmp(&prepared_copy, prepared, sizeof(prepared_copy)) ||
	    memcmp(&ops_copy, ops, sizeof(ops_copy)) ||
	    !bytes_zero(bound, sizeof(*bound)) || !bytes_zero(dma, sizeof(*dma)))
		goto fail;
	if (!guard_authority.bound_plan.revision)
		guard_authority.bound_plan = plan_copy;
	guard_authority.phase = GUARD_BOUND;
	memcpy(bound, &guard_authority.baseline, sizeof(*bound));
	dma->generation = guard_authority.baseline.generation;
	memcpy(dma->identity, guard_authority.baseline.identity,
		sizeof(dma->identity));
	return CB_SUCCESS;

fail:
	guard_poison(&ops_copy);
	memset(bound, 0, sizeof(*bound));
	memset(dma, 0, sizeof(*dma));
	return CB_ERR;
}
