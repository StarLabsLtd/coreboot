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
	bool seeded;
	uint64_t generation;
	uint8_t identity[32];
	struct starbook_mtl_dma_guard_snapshot baseline;
	struct payload_mm_authvar_mor_clear_plan bound_plan;
} guard_authority;

enum cb_err starbook_mtl_dma_guard_seed(uint64_t generation,
	const uint8_t identity[32])
{
	const uintptr_t base = (uintptr_t)identity;
	uint8_t combined = 0;

	if (!identity || base > (uintptr_t)-1 - 31U || !generation ||
	    guard_authority.phase != GUARD_EMPTY)
		return CB_ERR_ARG;
	for (size_t index = 0; index < sizeof(guard_authority.identity); index++)
		combined |= identity[index];
	if (!combined)
		return CB_ERR;
	if (guard_authority.seeded)
		return guard_authority.generation == generation &&
			!memcmp(guard_authority.identity, identity,
				sizeof(guard_authority.identity)) ? CB_SUCCESS : CB_ERR;
	guard_authority.generation = generation;
	memcpy(guard_authority.identity, identity,
		sizeof(guard_authority.identity));
	guard_authority.seeded = true;
	return CB_SUCCESS;
}

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

static bool context_disjoint(const struct starbook_mtl_dma_guard_ops *ops,
	const void *const *objects, const size_t *sizes, size_t count)
{
	if (!ops->context && !ops->context_size)
		return true;
	if (!object_valid(ops->context, ops->context_size, 1))
		return false;
	for (size_t index = 0; index < count; index++)
		if (objects_overlap(ops->context, ops->context_size,
			objects[index], sizes[index]))
			return false;
	return true;
}

static void guard_poison(const struct starbook_mtl_dma_guard_ops *ops)
{
	ops->poison(ops->context);
	memset(&guard_authority.baseline, 0, sizeof(guard_authority.baseline));
	memset(&guard_authority.bound_plan, 0, sizeof(guard_authority.bound_plan));
	guard_authority.phase = GUARD_POISONED;
}

enum cb_err starbook_mtl_dma_guard_policy_validate_owned(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *snapshot,
	struct starbook_mtl_dma_guard_policy_workspace *workspace)
{
	const uintptr_t plan_base = (uintptr_t)plan;
	const uintptr_t snapshot_base = (uintptr_t)snapshot;

	if (!object_valid(workspace, sizeof(*workspace), _Alignof(*workspace)) ||
	    !plan || plan_base > (uintptr_t)-1 - (sizeof(*plan) - 1U) ||
	    plan_base % _Alignof(*plan) || !snapshot ||
	    snapshot_base > (uintptr_t)-1 - (sizeof(*snapshot) - 1U) ||
	    (uintptr_t)snapshot % _Alignof(*snapshot) ||
	    (plan_base <= snapshot_base + sizeof(*snapshot) - 1U &&
	     snapshot_base <= plan_base + sizeof(*plan) - 1U) ||
	    objects_overlap(workspace, sizeof(*workspace), plan, sizeof(*plan)) ||
	    objects_overlap(workspace, sizeof(*workspace), snapshot,
		sizeof(*snapshot)))
		return CB_ERR_ARG;
	memset(workspace, 0, sizeof(*workspace));
	memcpy(&workspace->plan, plan, sizeof(workspace->plan));
	memcpy(&workspace->snapshot, snapshot, sizeof(workspace->snapshot));
	if (payload_mm_authvar_mor_clear_plan_validate(&workspace->plan) !=
		CB_SUCCESS ||
	    workspace->snapshot.revision != STARBOOK_MTL_DMA_GUARD_REVISION ||
	    workspace->snapshot.size != sizeof(workspace->snapshot) ||
	    !workspace->snapshot.generation ||
	    bytes_zero(workspace->snapshot.identity,
		sizeof(workspace->snapshot.identity)) ||
	    workspace->snapshot.engine_count != STARBOOK_MTL_DMA_GUARD_ENGINES ||
	    workspace->snapshot.arena_count != STARBOOK_MTL_DMA_GUARD_ARENAS ||
	    !bytes_zero(workspace->snapshot.reserved,
		sizeof(workspace->snapshot.reserved)) ||
	    !snapshot_structure_valid(&workspace->snapshot) ||
	    workspace->plan.inventory_generation != workspace->snapshot.generation ||
	    memcmp(workspace->plan.inventory_identity, workspace->snapshot.identity,
		sizeof(workspace->plan.inventory_identity)) ||
	    !range_covered(&workspace->plan, &workspace->snapshot.handoff,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE) ||
	    !range_covered(&workspace->plan, &workspace->snapshot.table,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE) ||
	    !range_covered(&workspace->plan, &workspace->snapshot.table_mirror,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE))
		goto fail;
	for (size_t index = 0; index < STARBOOK_MTL_DMA_GUARD_ARENAS; index++)
		if (!range_covered(&workspace->plan,
			&workspace->snapshot.arenas[index],
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED))
			goto fail;
	if (memcmp(&workspace->plan, plan, sizeof(*plan)) ||
	    memcmp(&workspace->snapshot, snapshot, sizeof(*snapshot)))
		goto fail;
	memset(workspace, 0, sizeof(*workspace));
	return CB_SUCCESS;

fail:
	memset(workspace, 0, sizeof(*workspace));
	return CB_ERR;
}

#if ENV_TEST
enum cb_err starbook_mtl_dma_guard_policy_validate(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	struct starbook_mtl_dma_guard_policy_workspace workspace;

	return starbook_mtl_dma_guard_policy_validate_owned(plan, snapshot,
		&workspace);
}
#endif

enum cb_err starbook_mtl_dma_guard_prepare_with_ops(
	struct starbook_mtl_dma_guard_snapshot *snapshot,
	const struct starbook_mtl_dma_guard_ops *ops)
{
	const void *objects[] = { snapshot, ops };
	const size_t sizes[] = { sizeof(*snapshot), sizeof(*ops) };
	struct starbook_mtl_dma_guard_snapshot candidate;
	struct starbook_mtl_dma_guard_snapshot recheck;
	struct starbook_mtl_dma_guard_ops ops_copy;
	uint64_t random_words[5];
	bool identity_nonzero = false;
	const enum guard_phase original_phase = guard_authority.phase;

	if (!object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)))
		return CB_ERR_ARG;
	if (!object_valid(ops, sizeof(*ops), _Alignof(*ops)) ||
	    objects_overlap(snapshot, sizeof(*snapshot), ops, sizeof(*ops)) ||
	    !context_disjoint(ops, objects, sizes, ARRAY_SIZE(objects)))
		return CB_ERR_ARG;
#if ENV_TEST
	starbook_mtl_dma_guard_pre_copy_test_hook(ops);
#endif
	memcpy(&ops_copy, ops, sizeof(ops_copy));
	if (!context_disjoint(&ops_copy, objects, sizes, ARRAY_SIZE(objects)) ||
	    memcmp(&ops_copy, ops, sizeof(ops_copy)))
		return CB_ERR_ARG;
	memset(snapshot, 0, sizeof(*snapshot));
	if (!ops_copy.ensure || !ops_copy.observe || !ops_copy.random64 ||
	    !ops_copy.poison ||
	    (CONFIG(STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD) &&
	     !guard_authority.seeded) ||
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
	} else if (guard_authority.seeded) {
		candidate.generation = guard_authority.generation;
		memcpy(candidate.identity, guard_authority.identity,
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

enum cb_err starbook_mtl_dma_guard_bind_with_ops_owned(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct starbook_mtl_dma_guard_snapshot *bound,
	struct payload_mm_authvar_mor_clear_dma_snapshot *dma,
	const struct starbook_mtl_dma_guard_ops *ops,
	struct starbook_mtl_dma_guard_bind_workspace *workspace)
{
	const bool bound_valid = object_valid(bound, sizeof(*bound), _Alignof(*bound));
	const bool dma_valid = object_valid(dma, sizeof(*dma), _Alignof(*dma));
	const void *objects[] = { plan, prepared, bound, dma, ops, workspace };
	const size_t sizes[] = { sizeof(*plan), sizeof(*prepared), sizeof(*bound),
		sizeof(*dma), sizeof(*ops), sizeof(*workspace) };
	const size_t alignments[] = { _Alignof(*plan), _Alignof(*prepared),
		_Alignof(*bound), _Alignof(*dma), _Alignof(*ops),
		_Alignof(*workspace) };
	struct starbook_mtl_dma_guard_ops ops_copy;

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
	if (!context_disjoint(ops, objects, sizes, ARRAY_SIZE(objects)))
		return CB_ERR_ARG;
#if ENV_TEST
	starbook_mtl_dma_guard_pre_copy_test_hook(ops);
#endif
	memcpy(&ops_copy, ops, sizeof(ops_copy));
	if (!context_disjoint(&ops_copy, objects, sizes, ARRAY_SIZE(objects)) ||
	    memcmp(&ops_copy, ops, sizeof(ops_copy)))
		return CB_ERR_ARG;
	memset(bound, 0, sizeof(*bound));
	memset(dma, 0, sizeof(*dma));
	memset(workspace, 0, sizeof(*workspace));
	memcpy(&workspace->plan, plan, sizeof(workspace->plan));
	memcpy(&workspace->prepared, prepared, sizeof(workspace->prepared));
	workspace->ops = ops_copy;
	if (!workspace->ops.ensure || !workspace->ops.observe ||
	    !workspace->ops.poison ||
	    guard_authority.phase == GUARD_EMPTY ||
	    guard_authority.phase == GUARD_POISONED)
		goto fail_without_poison;
	guard_authority.phase = GUARD_POISONED;
	if (memcmp(&workspace->prepared, &guard_authority.baseline,
		sizeof(workspace->prepared)) ||
	    starbook_mtl_dma_guard_policy_validate_owned(&workspace->plan,
		&workspace->prepared, &workspace->policy) != CB_SUCCESS ||
	    (guard_authority.bound_plan.revision &&
	     memcmp(&workspace->plan, &guard_authority.bound_plan,
		sizeof(workspace->plan))) ||
	    workspace->ops.ensure(workspace->ops.context) != CB_SUCCESS)
		goto fail;
	memset(&workspace->observed, 0, sizeof(workspace->observed));
	if (workspace->ops.observe(workspace->ops.context,
		&workspace->observed) != CB_SUCCESS ||
	    !prepared_snapshot_valid(&workspace->observed))
		goto fail;
	workspace->observed.generation = guard_authority.baseline.generation;
	memcpy(workspace->observed.identity, guard_authority.baseline.identity,
		sizeof(workspace->observed.identity));
	if (memcmp(&workspace->observed, &guard_authority.baseline,
		sizeof(workspace->observed)) ||
	    memcmp(&workspace->plan, plan, sizeof(*plan)) ||
	    memcmp(&workspace->prepared, prepared, sizeof(*prepared)) ||
	    memcmp(&workspace->ops, ops, sizeof(*ops)) ||
	    !bytes_zero(bound, sizeof(*bound)) || !bytes_zero(dma, sizeof(*dma)))
		goto fail;
	if (!guard_authority.bound_plan.revision)
		guard_authority.bound_plan = workspace->plan;
	guard_authority.phase = GUARD_BOUND;
	memcpy(bound, &guard_authority.baseline, sizeof(*bound));
	dma->generation = guard_authority.baseline.generation;
	memcpy(dma->identity, guard_authority.baseline.identity,
		sizeof(dma->identity));
	memset(workspace, 0, sizeof(*workspace));
	return CB_SUCCESS;

fail:
	guard_poison(&workspace->ops);
	memset(bound, 0, sizeof(*bound));
	memset(dma, 0, sizeof(*dma));
	memset(workspace, 0, sizeof(*workspace));
	return CB_ERR;

fail_without_poison:
	memset(workspace, 0, sizeof(*workspace));
	return CB_ERR;
}

#if ENV_TEST
enum cb_err starbook_mtl_dma_guard_bind_with_ops(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct starbook_mtl_dma_guard_snapshot *bound,
	struct payload_mm_authvar_mor_clear_dma_snapshot *dma,
	const struct starbook_mtl_dma_guard_ops *ops)
{
	struct starbook_mtl_dma_guard_bind_workspace workspace;

	return starbook_mtl_dma_guard_bind_with_ops_owned(plan, prepared, bound, dma,
		ops, &workspace);
}
#endif
