/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_clear.h>
#include <string.h>

#include "payload_mm_authvar_mor_clear_internal.h"

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

enum cb_err payload_mm_authvar_mor_clear_plan_validate(
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct payload_mm_authvar_mor_clear_plan snapshot;

	if (!object_valid(plan, sizeof(*plan), _Alignof(*plan)))
		return CB_ERR_ARG;
	memcpy(&snapshot, plan, sizeof(snapshot));
	if (!plan_snapshot_valid(&snapshot) ||
	    memcmp(&snapshot, plan, sizeof(snapshot)))
		return CB_ERR;
	return CB_SUCCESS;
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
