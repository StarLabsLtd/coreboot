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

enum cb_err payload_mm_authvar_mor_clear_plan_build_owned(
	const struct payload_mm_authvar_mor_clear_inventory *inventory,
	struct payload_mm_authvar_mor_clear_plan *plan,
	struct payload_mm_authvar_mor_clear_plan_workspace *workspace)
{
	uint64_t previous_end = 0;
	enum cb_err result = CB_ERR;

	/* Validate every address before reading or writing any caller object. */
	if (!object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
	    !object_valid(workspace, sizeof(*workspace), _Alignof(*workspace)))
		return CB_ERR_ARG;
	if (ranges_overlap(plan, sizeof(*plan), workspace, sizeof(*workspace)) ||
	    !object_valid(inventory, sizeof(*inventory), _Alignof(*inventory))) {
		if (!ranges_overlap(plan, sizeof(*plan), workspace, sizeof(*workspace)))
			memset(workspace, 0, sizeof(*workspace));
		return CB_ERR_ARG;
	}
	if (ranges_overlap(inventory, sizeof(*inventory), plan, sizeof(*plan)) ||
	    ranges_overlap(inventory, sizeof(*inventory), workspace,
		sizeof(*workspace)))
		return CB_ERR_ARG;

	memset(workspace, 0, sizeof(*workspace));
	memcpy(&workspace->output_snapshot, plan, sizeof(*plan));
	memcpy(&workspace->inventory_snapshot, inventory, sizeof(*inventory));
	memcpy(&workspace->inventory_working, &workspace->inventory_snapshot,
		sizeof(workspace->inventory_working));
	if (workspace->inventory_snapshot.revision !=
		PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION ||
	    workspace->inventory_snapshot.size !=
		sizeof(workspace->inventory_snapshot) ||
	    !workspace->inventory_snapshot.generation ||
	    bytes_zero(workspace->inventory_snapshot.identity,
		sizeof(workspace->inventory_snapshot.identity)) ||
	    !workspace->inventory_snapshot.span_count ||
	    workspace->inventory_snapshot.span_count >
		PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RAW_MAX_SPANS ||
	    workspace->inventory_snapshot.reserved)
		goto restore;
	for (size_t index = workspace->inventory_snapshot.span_count;
	     index < PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RAW_MAX_SPANS; index++)
		if (!bytes_zero(&workspace->inventory_snapshot.spans[index],
			sizeof(workspace->inventory_snapshot.spans[index])))
			goto restore;

	for (size_t index = 1;
	     index < workspace->inventory_working.span_count; index++) {
		struct payload_mm_authvar_mor_grant_span selected =
			workspace->inventory_working.spans[index];
		size_t position = index;

		while (position && workspace->inventory_working.spans[position - 1].base >
		       selected.base) {
			workspace->inventory_working.spans[position] =
				workspace->inventory_working.spans[position - 1];
			position--;
		}
		workspace->inventory_working.spans[position] = selected;
	}

	workspace->candidate.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	workspace->candidate.size = sizeof(workspace->candidate);
	workspace->candidate.inventory_generation =
		workspace->inventory_snapshot.generation;
	memcpy(workspace->candidate.inventory_identity,
		workspace->inventory_snapshot.identity,
		sizeof(workspace->candidate.inventory_identity));
	for (size_t index = 0;
	     index < workspace->inventory_working.span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span =
			&workspace->inventory_working.spans[index];
		struct payload_mm_authvar_mor_grant_span *previous =
			workspace->candidate.span_count ?
			&workspace->candidate.spans[
				workspace->candidate.span_count - 1] : NULL;

		if (!span_valid(span) || (index && span->base < previous_end))
			goto restore;
		if (previous && span->base == previous_end &&
		    previous->span_class == span->span_class &&
		    previous->exclusion_reason == span->exclusion_reason) {
			if (previous->size > UINT64_MAX - span->size)
				goto restore;
			previous->size += span->size;
		} else {
			if (workspace->candidate.span_count >=
			    PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS)
				goto restore;
			workspace->candidate.spans[
				workspace->candidate.span_count++] = *span;
		}
		previous_end = span->base + span->size;
	}
	if (!plan_snapshot_valid(&workspace->candidate) ||
	    memcmp(&workspace->inventory_snapshot, inventory,
		sizeof(*inventory)) ||
	    memcmp(&workspace->output_snapshot, plan, sizeof(*plan)))
		goto restore;
	memcpy(plan, &workspace->candidate, sizeof(*plan));
	result = CB_SUCCESS;
	goto scrub;

restore:
	memcpy(plan, &workspace->output_snapshot, sizeof(*plan));
scrub:
	memset(workspace, 0, sizeof(*workspace));
	return result;
}

#if ENV_TEST
enum cb_err payload_mm_authvar_mor_clear_plan_build(
	const struct payload_mm_authvar_mor_clear_inventory *inventory,
	struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct payload_mm_authvar_mor_clear_plan_workspace workspace;
	enum cb_err result;

	if (!object_valid(plan, sizeof(*plan), _Alignof(*plan)))
		return CB_ERR_ARG;
	result = payload_mm_authvar_mor_clear_plan_build_owned(inventory, plan,
		&workspace);
	if (result != CB_SUCCESS)
		memset(plan, 0, sizeof(*plan));
	return result;
}
#endif
