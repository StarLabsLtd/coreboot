/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_fmp_owner_layout_internal.h"

static bool range_end(u64 offset, u64 size, u64 *end)
{
	if (!size || offset > UINT64_MAX - size)
		return false;
	*end = offset + size;
	return true;
}

static bool range_inside(const struct fmp_owner_range *range, u64 limit)
{
	u64 end;

	return range_end(range->offset, range->size, &end) && end <= limit;
}

static bool overlap(const struct fmp_owner_range *left,
		    const struct fmp_owner_range *right)
{
	u64 left_end;
	u64 right_end;

	if (!range_end(left->offset, left->size, &left_end) ||
	    !range_end(right->offset, right->size, &right_end))
		return true;
	return left->offset < right_end && right->offset < left_end;
}

static bool route_span(const struct lb_capsule_update_region *route,
		       struct fmp_owner_range *range)
{
	u64 image_end;

	if (route->flags & ~LB_CAPSULE_REGION_VALID_FLAGS || route->reserved ||
	    !range_end(route->image_offset, route->size, &image_end))
		return false;
	*range = (struct fmp_owner_range) {
		.offset = route->flash_offset,
		.size = route->size,
	};
	return true;
}

bool payload_mm_fmp_layout_valid(const struct fmp_owner_layout *layout)
{
	u64 slots;
	size_t bios_routes = 0;

	if (!layout || layout->revision !=
		PAYLOAD_MM_FMP_OWNER_LAYOUT_REVISION ||
	    layout->size != sizeof(*layout) || !layout->media_size ||
	    !layout->erase_size || !layout->slot_size || layout->reserved ||
	    layout->route_count == 0 ||
	    layout->route_count > PAYLOAD_MM_FMP_OWNER_LAYOUT_MAX_ROUTES ||
	    (layout->erase_size & (layout->erase_size - 1U)) ||
	    layout->slot_size % layout->erase_size)
		return false;

	if (!range_inside(&layout->smmstore, layout->media_size) ||
	    layout->smmstore.offset % layout->erase_size ||
	    layout->smmstore.size % layout->erase_size)
		return false;

	for (size_t domain = 0;
	     domain < PAYLOAD_MM_FMP_OWNER_LAYOUT_DOMAINS; domain++) {
		const struct fmp_owner_range *state =
			&layout->state[domain];

		if (!range_inside(state, layout->media_size) ||
		    state->offset % layout->erase_size ||
		    state->size % layout->erase_size ||
		    state->size % layout->slot_size)
			return false;
		slots = state->size / layout->slot_size;
		if (slots < 2 || slots > 32 ||
		    overlap(state, &layout->smmstore))
			return false;
	}
	if (overlap(&layout->state[0], &layout->state[1]))
		return false;

	for (size_t index = 0; index < layout->route_count; index++) {
		struct fmp_owner_range route;

		if (!route_span(&layout->route[index], &route) ||
		    !range_inside(&route, layout->media_size) ||
		    route.offset % layout->erase_size ||
		    route.size % layout->erase_size ||
		    overlap(&route, &layout->smmstore) ||
		    overlap(&route, &layout->state[0]) ||
		    overlap(&route, &layout->state[1]))
			return false;
		bios_routes += !!(layout->route[index].flags &
			LB_CAPSULE_REGION_BIOS);
		for (size_t previous = 0; previous < index; previous++) {
			struct fmp_owner_range other;

			if (!route_span(&layout->route[previous], &other) ||
			    overlap(&route, &other))
				return false;
		}
	}
	for (size_t index = layout->route_count;
	     index < PAYLOAD_MM_FMP_OWNER_LAYOUT_MAX_ROUTES; index++) {
		const struct lb_capsule_update_region *route = &layout->route[index];

		if (route->image_offset || route->flash_offset || route->size ||
		    route->flags || route->reserved)
			return false;
	}

	return bios_routes == 1;
}
