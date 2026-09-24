/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <bootmem.h>
#include <commonlib/helpers.h>
#include <string.h>

struct compose_context {
	const struct payload_mm_authvar_mor_live_inventory_request *request;
	struct payload_mm_authvar_mor_clear_inventory *inventory;
	uint64_t overlay_covered[PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_MAX_OVERLAYS];
	uint64_t previous_end;
	bool have_previous;
	bool failed;
};

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

	return object && !(base % alignment) && size &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool objects_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	const uintptr_t left_base = (uintptr_t)left;
	const uintptr_t right_base = (uintptr_t)right;

	if (left_base <= right_base)
		return right_base - left_base < left_size;
	return left_base - right_base < right_size;
}

static bool request_valid(
	const struct payload_mm_authvar_mor_live_inventory_request *request)
{
	if (request->revision != PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_REVISION ||
	    request->size != sizeof(*request) || !request->generation ||
	    bytes_zero(request->identity, sizeof(request->identity)) ||
	    !request->overlay_count ||
	    request->overlay_count >
		PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_MAX_OVERLAYS ||
	    request->reserved)
		return false;
	for (size_t index = 0; index < request->overlay_count; index++) {
		const struct payload_mm_authvar_mor_live_inventory_overlay *overlay =
			&request->overlays[index];

		if (!overlay->size || overlay->base > UINT64_MAX - overlay->size ||
		    (overlay->exclusion_reason !=
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		     overlay->exclusion_reason !=
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED) ||
		    overlay->reserved)
			return false;
		for (size_t other = 0; other < index; other++) {
			const struct payload_mm_authvar_mor_live_inventory_overlay *prior =
				&request->overlays[other];

			if (overlay->base < prior->base + prior->size &&
			    prior->base < overlay->base + overlay->size)
				return false;
		}
	}
	for (size_t index = request->overlay_count;
	     index < PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_MAX_OVERLAYS; index++)
		if (!bytes_zero(&request->overlays[index],
			sizeof(request->overlays[index])))
			return false;
	return true;
}

static bool add_span(struct compose_context *context, uint64_t base,
	uint64_t size, uint32_t span_class, uint32_t reason)
{
	struct payload_mm_authvar_mor_grant_span *previous;

	if (!size || base > UINT64_MAX - size)
		return false;
	previous = context->inventory->span_count ?
		&context->inventory->spans[context->inventory->span_count - 1U] : NULL;
	if (previous && previous->base + previous->size == base &&
	    previous->span_class == span_class &&
	    previous->exclusion_reason == reason) {
		if (previous->size > UINT64_MAX - size)
			return false;
		previous->size += size;
		return true;
	}
	if (context->inventory->span_count >=
	    PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RAW_MAX_SPANS)
		return false;
	context->inventory->spans[context->inventory->span_count++] =
		(struct payload_mm_authvar_mor_grant_span) {
			.base = base,
			.size = size,
			.span_class = span_class,
			.exclusion_reason = reason,
		};
	return true;
}

static bool classify_tag(unsigned long tag, uint32_t *span_class,
	uint32_t *reason)
{
	if (tag <= BM_MEM_FIRST || tag >= BM_MEM_LAST)
		return false;
	if (tag == BM_MEM_RAM) {
		*span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED;
		*reason = PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE;
	} else {
		*span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED;
		*reason = tag == BM_MEM_RAMSTAGE || tag == BM_MEM_TABLE ||
			tag == BM_MEM_PAYLOAD ?
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE :
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED;
	}
	return true;
}

static bool collect_range(const struct range_entry *range, void *argument)
{
	struct compose_context *context = argument;
	const uint64_t base = range_entry_base(range);
	const uint64_t size = range_entry_size(range);
	uint64_t position = base;
	uint64_t end;
	uint32_t source_class;
	uint32_t source_reason;

	if (!size || base > UINT64_MAX - size ||
	    (context->have_previous && base < context->previous_end) ||
	    !classify_tag(range_entry_tag(range), &source_class, &source_reason))
		goto fail;
	end = base + size;
	while (position < end) {
		uint64_t next = end;
		uint32_t span_class = source_class;
		uint32_t reason = source_reason;
		size_t active = context->request->overlay_count;

		for (size_t index = 0; index < context->request->overlay_count; index++) {
			const struct payload_mm_authvar_mor_live_inventory_overlay *overlay =
				&context->request->overlays[index];
			const uint64_t overlay_end = overlay->base + overlay->size;

			if (position >= overlay->base && position < overlay_end) {
				if (active != context->request->overlay_count)
					goto fail;
				active = index;
				next = MIN(next, overlay_end);
			} else if (overlay->base > position) {
				next = MIN(next, overlay->base);
			}
		}
		if (active != context->request->overlay_count) {
			span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED;
			reason = context->request->overlays[active].exclusion_reason;
			if (context->overlay_covered[active] > UINT64_MAX - (next - position))
				goto fail;
			context->overlay_covered[active] += next - position;
		}
		if (next <= position ||
		    !add_span(context, position, next - position, span_class, reason))
			goto fail;
		position = next;
	}
	context->previous_end = end;
	context->have_previous = true;
	return true;

fail:
	context->failed = true;
	return false;
}

enum cb_err payload_mm_authvar_mor_live_inventory_compose(
	const struct payload_mm_authvar_mor_live_inventory_request *request,
	struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct payload_mm_authvar_mor_live_inventory_request snapshot;
	struct payload_mm_authvar_mor_clear_inventory inventory = { 0 };
	struct payload_mm_authvar_mor_clear_plan candidate;
	struct compose_context context = {
		.request = &snapshot,
		.inventory = &inventory,
	};

	if (!object_valid(plan, sizeof(*plan), _Alignof(*plan)))
		return CB_ERR_ARG;
	memset(plan, 0, sizeof(*plan));
	if (!object_valid(request, sizeof(*request), _Alignof(*request)) ||
	    objects_overlap(request, sizeof(*request), plan, sizeof(*plan)))
		return CB_ERR_ARG;
	memcpy(&snapshot, request, sizeof(snapshot));
	if (!request_valid(&snapshot))
		goto fail;
	inventory.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	inventory.size = sizeof(inventory);
	inventory.generation = snapshot.generation;
	memcpy(inventory.identity, snapshot.identity, sizeof(inventory.identity));
	if (bootmem_walk_dram(collect_range, &context) || context.failed ||
	    !inventory.span_count)
		goto fail;
	for (size_t index = 0; index < snapshot.overlay_count; index++)
		if (context.overlay_covered[index] != snapshot.overlays[index].size)
			goto fail;
	if (payload_mm_authvar_mor_clear_plan_build(&inventory, &candidate) !=
		CB_SUCCESS || memcmp(&snapshot, request, sizeof(snapshot)) ||
		!bytes_zero(plan, sizeof(*plan)))
		goto fail;
	*plan = candidate;
	return CB_SUCCESS;

fail:
	memset(plan, 0, sizeof(*plan));
	return CB_ERR;
}
