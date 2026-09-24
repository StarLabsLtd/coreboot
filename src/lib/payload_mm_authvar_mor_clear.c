/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear.h>
#include <limits.h>
#include <string.h>

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

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

static bool span_valid(const struct payload_mm_authvar_mor_grant_span *span)
{
	if (!span->size || span->base > UINT64_MAX - span->size)
		return false;
	if (span->span_class == PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED)
		return span->exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE;
	if (span->span_class != PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED)
		return false;
	return span->exclusion_reason >=
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		span->exclusion_reason <=
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED;
}

static bool dma_snapshot_valid(
	const struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	return snapshot->generation &&
		!bytes_zero(snapshot->identity, sizeof(snapshot->identity)) &&
		bytes_zero(snapshot->reserved, sizeof(snapshot->reserved));
}

static bool plan_snapshot_valid(
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	uint64_t previous_end = 0;
	bool cleared = false;

	if (plan->revision != PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION ||
	    plan->size != sizeof(*plan) || !plan->inventory_generation ||
	    bytes_zero(plan->inventory_identity, sizeof(plan->inventory_identity)) ||
	    !plan->span_count ||
	    plan->span_count > PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS ||
	    plan->reserved)
		return false;
	for (size_t index = 0; index < plan->span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span = &plan->spans[index];

		if (!span_valid(span) ||
		    (index && (span->base < previous_end ||
		     (span->base == previous_end &&
		      span->span_class == plan->spans[index - 1].span_class &&
		      span->exclusion_reason ==
			plan->spans[index - 1].exclusion_reason))))
			return false;
		previous_end = span->base + span->size;
		cleared |= span->span_class ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED;
	}
	for (size_t index = plan->span_count;
	     index < PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS; index++)
		if (!bytes_zero(&plan->spans[index], sizeof(plan->spans[index])))
			return false;
	return cleared;
}

enum cb_err payload_mm_authvar_mor_clear_plan_build(
	const struct payload_mm_authvar_mor_clear_inventory *inventory,
	struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct payload_mm_authvar_mor_clear_inventory snapshot;
	struct payload_mm_authvar_mor_clear_inventory working;
	struct payload_mm_authvar_mor_clear_plan candidate = { 0 };
	uint64_t previous_end = 0;

	if (!object_valid(plan, sizeof(*plan), _Alignof(*plan)))
		return CB_ERR_ARG;
	if (!object_valid(inventory, sizeof(*inventory), _Alignof(*inventory)) ||
	    ranges_overlap(inventory, sizeof(*inventory), plan, sizeof(*plan))) {
		memset(plan, 0, sizeof(*plan));
		return CB_ERR_ARG;
	}
	memcpy(&snapshot, inventory, sizeof(snapshot));
	memcpy(&working, &snapshot, sizeof(working));
	memset(plan, 0, sizeof(*plan));
	if (snapshot.revision != PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION ||
	    snapshot.size != sizeof(snapshot) || !snapshot.generation ||
	    bytes_zero(snapshot.identity, sizeof(snapshot.identity)) ||
	    !snapshot.span_count ||
	    snapshot.span_count > PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RAW_MAX_SPANS ||
	    snapshot.reserved)
		return CB_ERR;
	for (size_t index = snapshot.span_count;
	     index < PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RAW_MAX_SPANS; index++)
		if (!bytes_zero(&snapshot.spans[index], sizeof(snapshot.spans[index])))
			return CB_ERR;

	for (size_t index = 1; index < working.span_count; index++) {
		struct payload_mm_authvar_mor_grant_span selected = working.spans[index];
		size_t position = index;

		while (position && working.spans[position - 1].base > selected.base) {
			working.spans[position] = working.spans[position - 1];
			position--;
		}
		working.spans[position] = selected;
	}

	candidate.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	candidate.size = sizeof(candidate);
	candidate.inventory_generation = snapshot.generation;
	memcpy(candidate.inventory_identity, snapshot.identity,
		sizeof(candidate.inventory_identity));
	for (size_t index = 0; index < working.span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span = &working.spans[index];
		struct payload_mm_authvar_mor_grant_span *previous = candidate.span_count ?
			&candidate.spans[candidate.span_count - 1] : NULL;

		if (!span_valid(span) || (index && span->base < previous_end))
			return CB_ERR;
		if (previous && span->base == previous_end &&
		    previous->span_class == span->span_class &&
		    previous->exclusion_reason == span->exclusion_reason) {
			if (previous->size > UINT64_MAX - span->size)
				return CB_ERR;
			previous->size += span->size;
		} else {
			if (candidate.span_count >=
			    PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS)
				return CB_ERR;
			candidate.spans[candidate.span_count++] = *span;
		}
		previous_end = span->base + span->size;
	}
	if (!plan_snapshot_valid(&candidate) ||
	    memcmp(&snapshot, inventory, sizeof(snapshot)))
		return CB_ERR;
	*plan = candidate;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_clear_receipt_build(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	const struct payload_mm_authvar_mor_clear_facts *facts,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant)
{
	struct payload_mm_authvar_mor_clear_plan plan_snapshot;
	struct payload_mm_authvar_mor_entry entry_snapshot;
	struct payload_mm_authvar_mor_clear_facts facts_snapshot;
	struct payload_mm_authvar_mor_clear_transcript transcript_snapshot;
	struct payload_mm_authvar_mor_grant candidate = { 0 };
	uint32_t cleared_index = 0;

	if (!object_valid(grant, sizeof(*grant), _Alignof(*grant)))
		return CB_ERR_ARG;
	if (!object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
	    !object_valid(entry, sizeof(*entry), _Alignof(*entry)) ||
	    !object_valid(facts, sizeof(*facts), _Alignof(*facts)) ||
	    !object_valid(transcript, sizeof(*transcript), _Alignof(*transcript)) ||
	    ranges_overlap(grant, sizeof(*grant), plan, sizeof(*plan)) ||
	    ranges_overlap(grant, sizeof(*grant), entry, sizeof(*entry)) ||
	    ranges_overlap(grant, sizeof(*grant), facts, sizeof(*facts)) ||
	    ranges_overlap(grant, sizeof(*grant), transcript, sizeof(*transcript)) ||
	    ranges_overlap(plan, sizeof(*plan), entry, sizeof(*entry)) ||
	    ranges_overlap(plan, sizeof(*plan), facts, sizeof(*facts)) ||
	    ranges_overlap(plan, sizeof(*plan), transcript, sizeof(*transcript)) ||
	    ranges_overlap(entry, sizeof(*entry), facts, sizeof(*facts)) ||
	    ranges_overlap(entry, sizeof(*entry), transcript, sizeof(*transcript)) ||
	    ranges_overlap(facts, sizeof(*facts), transcript, sizeof(*transcript))) {
		memset(grant, 0, sizeof(*grant));
		return CB_ERR_ARG;
	}
	memcpy(&plan_snapshot, plan, sizeof(plan_snapshot));
	memcpy(&entry_snapshot, entry, sizeof(entry_snapshot));
	memcpy(&facts_snapshot, facts, sizeof(facts_snapshot));
	memcpy(&transcript_snapshot, transcript, sizeof(transcript_snapshot));
	memset(grant, 0, sizeof(*grant));
	if (!plan_snapshot_valid(&plan_snapshot) ||
	    entry_snapshot.present != 1 || !(entry_snapshot.value & 1U) ||
	    entry_snapshot.reserved ||
	    facts_snapshot.revision != PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION ||
	    facts_snapshot.size != sizeof(facts_snapshot) ||
	    !facts_snapshot.cold_boot_generation ||
	    !dma_snapshot_valid(&facts_snapshot.dma_before) ||
	    !dma_snapshot_valid(&facts_snapshot.dma_after) ||
	    memcmp(&facts_snapshot.dma_before, &facts_snapshot.dma_after,
		sizeof(facts_snapshot.dma_before)) ||
	    facts_snapshot.inventory_generation != plan_snapshot.inventory_generation ||
	    memcmp(facts_snapshot.inventory_identity,
		plan_snapshot.inventory_identity,
		sizeof(facts_snapshot.inventory_identity)) ||
	    !bytes_zero(facts_snapshot.reserved, sizeof(facts_snapshot.reserved)) ||
	    transcript_snapshot.revision != PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION ||
	    transcript_snapshot.size != sizeof(transcript_snapshot) ||
	    transcript_snapshot.cold_boot_generation !=
		facts_snapshot.cold_boot_generation ||
	    memcmp(&transcript_snapshot.entry, &entry_snapshot,
		sizeof(entry_snapshot)) || transcript_snapshot.reserved0 ||
	    memcmp(&transcript_snapshot.dma_before, &facts_snapshot.dma_before,
		sizeof(facts_snapshot.dma_before)) ||
	    memcmp(&transcript_snapshot.dma_after, &facts_snapshot.dma_after,
		sizeof(facts_snapshot.dma_after)) ||
	    transcript_snapshot.inventory_generation !=
		plan_snapshot.inventory_generation ||
	    memcmp(transcript_snapshot.inventory_identity,
		plan_snapshot.inventory_identity,
		sizeof(plan_snapshot.inventory_identity)) ||
	    transcript_snapshot.reserved1)
		return CB_ERR;

	for (size_t index = 0; index < plan_snapshot.span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span =
			&plan_snapshot.spans[index];
		const struct payload_mm_authvar_mor_clear_record *record;

		if (span->span_class != PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED)
			continue;
		if (cleared_index >= transcript_snapshot.cleared_span_count ||
		    cleared_index >= PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS)
			return CB_ERR;
		record = &transcript_snapshot.records[cleared_index++];
		if (record->base != span->base || record->size != span->size ||
		    record->written_bytes != span->size ||
		    record->cache_writeback_fenced_bytes != span->size ||
		    record->zero_readback_bytes != span->size ||
		    !bytes_zero(record->reserved, sizeof(record->reserved)))
			return CB_ERR;
	}
	if (cleared_index != transcript_snapshot.cleared_span_count)
		return CB_ERR;
	for (size_t index = cleared_index;
	     index < PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS; index++)
		if (!bytes_zero(&transcript_snapshot.records[index],
			sizeof(transcript_snapshot.records[index])))
			return CB_ERR;

	candidate.revision = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION;
	candidate.size = sizeof(candidate);
	candidate.cold_boot_generation = facts_snapshot.cold_boot_generation;
	candidate.entry = entry_snapshot;
	candidate.flags = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS;
	candidate.dma_policy_generation = facts_snapshot.dma_before.generation;
	memcpy(candidate.dma_policy_identity, facts_snapshot.dma_before.identity,
		sizeof(candidate.dma_policy_identity));
	candidate.inventory_generation = plan_snapshot.inventory_generation;
	memcpy(candidate.inventory_identity, plan_snapshot.inventory_identity,
		sizeof(candidate.inventory_identity));
	candidate.total_spans = plan_snapshot.span_count;
	for (size_t index = 0; index < plan_snapshot.span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span =
			&plan_snapshot.spans[index];

		candidate.spans[index] = *span;
		if (candidate.total_bytes > UINT64_MAX - span->size)
			return CB_ERR;
		candidate.total_bytes += span->size;
		if (span->span_class == PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED) {
			if (candidate.cleared_bytes > UINT64_MAX - span->size)
				return CB_ERR;
			candidate.cleared_bytes += span->size;
			candidate.cleared_spans++;
		} else {
			if (candidate.excluded_bytes > UINT64_MAX - span->size)
				return CB_ERR;
			candidate.excluded_bytes += span->size;
			candidate.excluded_spans++;
		}
	}
	if (payload_mm_authvar_mor_grant_validate(&candidate) != CB_SUCCESS ||
	    memcmp(&plan_snapshot, plan, sizeof(plan_snapshot)) ||
	    memcmp(&entry_snapshot, entry, sizeof(entry_snapshot)) ||
	    memcmp(&facts_snapshot, facts, sizeof(facts_snapshot)) ||
	    memcmp(&transcript_snapshot, transcript, sizeof(transcript_snapshot)))
		return CB_ERR;
	*grant = candidate;
	return CB_SUCCESS;
}
