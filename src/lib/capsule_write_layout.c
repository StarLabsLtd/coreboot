/* SPDX-License-Identifier: GPL-2.0-only */

#include "capsule_write_layout_internal.h"

static bool range_valid(const struct capsule_write_range *range,
	const struct capsule_write_layout *layout)
{
	return range->size && range->offset <= layout->media_size &&
		range->size <= layout->media_size - range->offset &&
		!(range->offset % layout->erase_size) &&
		!(range->size % layout->erase_size);
}

static bool overlap(const struct capsule_write_range *left,
	const struct capsule_write_range *right)
{
	/* Callers validate both ranges before calculating their ends. */
	return left->offset < right->offset + right->size &&
		right->offset < left->offset + left->size;
}

bool capsule_write_layout_valid(const struct capsule_write_layout *layout)
{
	size_t bios_routes = 0;

	if (!layout || layout->revision != CAPSULE_WRITE_LAYOUT_REVISION ||
	    layout->size != sizeof(*layout) || !layout->media_size ||
	    !layout->erase_size || (layout->erase_size & (layout->erase_size - 1U)) ||
	    layout->reserved || !layout->route_count ||
	    layout->route_count > CAPSULE_UPDATE_MAX_REGIONS ||
	    layout->metadata_count > CAPSULE_WRITE_LAYOUT_MAX_METADATA ||
	    !range_valid(&layout->smmstore, layout))
		return false;

	for (size_t i = 0; i < layout->metadata_count; i++) {
		const struct capsule_write_range *metadata = &layout->metadata[i];

		if (!range_valid(metadata, layout) || overlap(metadata, &layout->smmstore))
			return false;
		for (size_t previous = 0; previous < i; previous++)
			if (overlap(metadata, &layout->metadata[previous]))
				return false;
	}
	for (size_t i = layout->metadata_count; i < CAPSULE_WRITE_LAYOUT_MAX_METADATA; i++)
		if (layout->metadata[i].offset || layout->metadata[i].size)
			return false;

	for (size_t i = 0; i < layout->route_count; i++) {
		const struct lb_capsule_update_region *route = &layout->route[i];
		struct capsule_write_range span = { route->flash_offset, route->size };

		if (route->reserved || route->flags & ~LB_CAPSULE_REGION_VALID_FLAGS ||
		    !range_valid(&span, layout) ||
		    route->image_offset > UINT64_MAX - route->size ||
		    route->image_offset % layout->erase_size || overlap(&span, &layout->smmstore))
			return false;
		for (size_t metadata = 0; metadata < layout->metadata_count; metadata++)
			if (overlap(&span, &layout->metadata[metadata]))
				return false;
		for (size_t previous = 0; previous < i; previous++) {
			const struct lb_capsule_update_region *other = &layout->route[previous];
			struct capsule_write_range prior_flash = { other->flash_offset, other->size };
			struct capsule_write_range prior_image = { other->image_offset, other->size };
			struct capsule_write_range image = { route->image_offset, route->size };

			if (overlap(&span, &prior_flash) || overlap(&image, &prior_image))
				return false;
		}
		bios_routes += !!(route->flags & LB_CAPSULE_REGION_BIOS);
	}
	for (size_t i = layout->route_count; i < CAPSULE_UPDATE_MAX_REGIONS; i++) {
		const struct lb_capsule_update_region *route = &layout->route[i];

		if (route->image_offset || route->flash_offset || route->size ||
		    route->flags || route->reserved)
			return false;
	}
	return bios_routes == 1;
}
