/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear.h>
#include <commonlib/helpers.h>
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

enum cb_err payload_mm_authvar_mor_clear_receipt_build_owned(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	const struct payload_mm_authvar_mor_clear_facts *facts,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant)
{
	uint32_t cleared_index = 0;

	if (!object_valid(grant, sizeof(*grant), _Alignof(*grant)) ||
	    !object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
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
		return CB_ERR_ARG;
	}
	memset(grant, 0, sizeof(*grant));
	if (payload_mm_authvar_mor_clear_plan_validate(plan) != CB_SUCCESS ||
	    entry->present != 1 || !(entry->value & 1U) || entry->reserved ||
	    facts->revision != PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION ||
	    facts->size != sizeof(*facts) || !facts->cold_boot_generation ||
	    !dma_snapshot_valid(&facts->dma_before) ||
	    !dma_snapshot_valid(&facts->dma_after) ||
	    memcmp(&facts->dma_before, &facts->dma_after, sizeof(facts->dma_before)) ||
	    facts->inventory_generation != plan->inventory_generation ||
	    memcmp(facts->inventory_identity, plan->inventory_identity,
		sizeof(facts->inventory_identity)) ||
	    !bytes_zero(facts->reserved, sizeof(facts->reserved)) ||
	    transcript->revision != PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION ||
	    transcript->size != sizeof(*transcript) ||
	    transcript->cold_boot_generation != facts->cold_boot_generation ||
	    memcmp(&transcript->entry, entry, sizeof(*entry)) ||
	    transcript->reserved0 ||
	    memcmp(&transcript->dma_before, &facts->dma_before,
		sizeof(facts->dma_before)) ||
	    memcmp(&transcript->dma_after, &facts->dma_after,
		sizeof(facts->dma_after)) ||
	    transcript->inventory_generation != plan->inventory_generation ||
	    memcmp(transcript->inventory_identity, plan->inventory_identity,
		sizeof(plan->inventory_identity)) || transcript->reserved1)
		return CB_ERR;

	for (size_t index = 0; index < plan->span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span =
			&plan->spans[index];
		const struct payload_mm_authvar_mor_clear_record *record;

		if (span->span_class != PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED)
			continue;
		if (cleared_index >= transcript->cleared_span_count ||
		    cleared_index >= PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS)
			return CB_ERR;
		record = &transcript->records[cleared_index++];
		if (record->base != span->base || record->size != span->size ||
		    record->written_bytes != span->size ||
		    record->cache_writeback_fenced_bytes != span->size ||
		    record->zero_readback_bytes != span->size ||
		    !bytes_zero(record->reserved, sizeof(record->reserved)))
			return CB_ERR;
	}
	if (cleared_index != transcript->cleared_span_count)
		return CB_ERR;
	for (size_t index = cleared_index;
	     index < PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS; index++)
		if (!bytes_zero(&transcript->records[index],
			sizeof(transcript->records[index])))
			return CB_ERR;

	grant->revision = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION;
	grant->size = sizeof(*grant);
	grant->cold_boot_generation = facts->cold_boot_generation;
	grant->entry = *entry;
	grant->flags = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS;
	grant->dma_policy_generation = facts->dma_before.generation;
	memcpy(grant->dma_policy_identity, facts->dma_before.identity,
		sizeof(grant->dma_policy_identity));
	grant->inventory_generation = plan->inventory_generation;
	memcpy(grant->inventory_identity, plan->inventory_identity,
		sizeof(grant->inventory_identity));
	grant->total_spans = plan->span_count;
	for (size_t index = 0; index < plan->span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span =
			&plan->spans[index];

		grant->spans[index] = *span;
		if (grant->total_bytes > UINT64_MAX - span->size)
			return CB_ERR;
		grant->total_bytes += span->size;
		if (span->span_class == PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED) {
			if (grant->cleared_bytes > UINT64_MAX - span->size)
				return CB_ERR;
			grant->cleared_bytes += span->size;
			grant->cleared_spans++;
		} else {
			if (grant->excluded_bytes > UINT64_MAX - span->size)
				return CB_ERR;
			grant->excluded_bytes += span->size;
			grant->excluded_spans++;
		}
	}
	if (payload_mm_authvar_mor_grant_validate(grant) != CB_SUCCESS)
		return CB_ERR;
	return CB_SUCCESS;
}

#if ENV_TEST
static __noinline void scrub_receipt_scratch(void *buffer,
	size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

enum cb_err payload_mm_authvar_mor_clear_receipt_build(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	const struct payload_mm_authvar_mor_clear_facts *facts,
	const struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant)
{
	struct {
		struct payload_mm_authvar_mor_clear_plan plan;
		struct payload_mm_authvar_mor_entry entry;
		struct payload_mm_authvar_mor_clear_facts facts;
		struct payload_mm_authvar_mor_clear_transcript transcript;
		struct payload_mm_authvar_mor_grant grant;
	} scratch_storage __aligned(8);
	typeof(scratch_storage) *scratch = &scratch_storage;
	enum cb_err result = CB_ERR_ARG;
	const void *inputs[] = { plan, entry, facts, transcript };
	const size_t sizes[] = { sizeof(*plan), sizeof(*entry), sizeof(*facts),
		sizeof(*transcript) };
	const size_t alignments[] = { _Alignof(*plan), _Alignof(*entry),
		_Alignof(*facts), _Alignof(*transcript) };

	if (!object_valid(grant, sizeof(*grant), _Alignof(*grant)))
		return CB_ERR_ARG;
	for (size_t index = 0; index < ARRAY_SIZE(inputs); index++) {
		if (!object_valid(inputs[index], sizes[index], alignments[index]) ||
		    ranges_overlap(grant, sizeof(*grant), inputs[index], sizes[index]))
			goto invalid;
		for (size_t other = 0; other < index; other++)
			if (ranges_overlap(inputs[index], sizes[index], inputs[other],
				sizes[other]))
				goto invalid;
	}
	memset(scratch, 0, sizeof(*scratch));
	memcpy(&scratch->plan, plan, sizeof(scratch->plan));
	memcpy(&scratch->entry, entry, sizeof(scratch->entry));
	memcpy(&scratch->facts, facts, sizeof(scratch->facts));
	memcpy(&scratch->transcript, transcript, sizeof(scratch->transcript));
	memset(grant, 0, sizeof(*grant));
	result = payload_mm_authvar_mor_clear_receipt_build_owned(&scratch->plan,
		&scratch->entry, &scratch->facts, &scratch->transcript,
		&scratch->grant);
	if (result == CB_SUCCESS &&
	    (!memcmp(&scratch->plan, plan, sizeof(scratch->plan)) &&
	     !memcmp(&scratch->entry, entry, sizeof(scratch->entry)) &&
	     !memcmp(&scratch->facts, facts, sizeof(scratch->facts)) &&
	     !memcmp(&scratch->transcript, transcript,
		sizeof(scratch->transcript))))
		memcpy(grant, &scratch->grant, sizeof(*grant));
	else
		result = CB_ERR;
	scrub_receipt_scratch(scratch, sizeof(*scratch));
	return result;

invalid:
	memset(grant, 0, sizeof(*grant));
	return CB_ERR_ARG;
}
#endif
