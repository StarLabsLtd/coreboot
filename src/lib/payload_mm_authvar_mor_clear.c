/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear.h>
#include <string.h>

#include "payload_mm_authvar_mor_clear_internal.h"

static bool dma_snapshot_valid(
	const struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	return snapshot->generation &&
		!bytes_zero(snapshot->identity, sizeof(snapshot->identity)) &&
		bytes_zero(snapshot->reserved, sizeof(snapshot->reserved));
}

enum cb_err payload_mm_authvar_mor_clear_dma_snapshot_validate(
	const struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	struct payload_mm_authvar_mor_clear_dma_snapshot copy;

	if (!object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)))
		return CB_ERR_ARG;
	memcpy(&copy, snapshot, sizeof(copy));
	if (!dma_snapshot_valid(&copy) || memcmp(&copy, snapshot, sizeof(copy)))
		return CB_ERR;
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
	if (payload_mm_authvar_mor_clear_plan_validate(&plan_snapshot) != CB_SUCCESS ||
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
