/* SPDX-License-Identifier: GPL-2.0-only */

#include "capsule_update_internal.h"

#include <string.h>

static bool range_valid(u64 offset, u64 size, u64 limit)
{
	return size && offset <= limit && size <= limit - offset;
}

static bool ranges_overlap(u64 left_offset, u64 left_size,
			   u64 right_offset, u64 right_size)
{
	return left_offset < right_offset + right_size &&
		right_offset < left_offset + left_size;
}

static bool region_equal(const struct lb_capsule_update_region *left,
			 const struct lb_capsule_update_region *right)
{
	return left->image_offset == right->image_offset &&
		left->flash_offset == right->flash_offset &&
		left->size == right->size && left->flags == right->flags &&
		!left->reserved && !right->reserved;
}

static bool plan_allowed(const struct capsule_update_plan *plan,
			 const struct capsule_media_policy *policy)
{
	size_t bios_regions = 0;

	if (!plan || !plan->image || !plan->regions || !policy ||
	    !policy->regions || !policy->owner_layout || !plan->region_count ||
	    plan->region_count != policy->region_count || !policy->erase_size ||
	    !payload_mm_fmp_layout_valid(policy->owner_layout) ||
	    policy->owner_layout->media_size != policy->media_size ||
	    policy->owner_layout->erase_size != policy->erase_size ||
	    policy->owner_layout->smmstore.offset != policy->smmstore_offset ||
	    policy->owner_layout->smmstore.size != policy->smmstore_size ||
	    policy->owner_layout->route_count != policy->region_count ||
	    !range_valid(policy->smmstore_offset, policy->smmstore_size,
		policy->media_size))
		return false;
	for (size_t i = 0; i < plan->region_count; i++) {
		const struct lb_capsule_update_region *region = &plan->regions[i];

		if (!region_equal(region, &policy->regions[i]) ||
		    !region_equal(region, &policy->owner_layout->route[i]) ||
		    region->flags & ~LB_CAPSULE_REGION_VALID_FLAGS ||
		    !range_valid(region->image_offset, region->size,
			plan->image_bytes) ||
		    !range_valid(region->flash_offset, region->size,
			policy->media_size) ||
		    region->image_offset % policy->erase_size ||
		    region->flash_offset % policy->erase_size ||
		    region->size % policy->erase_size ||
		    ranges_overlap(region->flash_offset, region->size,
				   policy->smmstore_offset, policy->smmstore_size))
			return false;
		for (size_t previous = 0; previous < i; previous++) {
			const struct lb_capsule_update_region *other =
				&plan->regions[previous];

			if (ranges_overlap(region->image_offset, region->size,
					   other->image_offset, other->size) ||
			    ranges_overlap(region->flash_offset, region->size,
					   other->flash_offset, other->size))
				return false;
		}
		bios_regions += !!(region->flags & LB_CAPSULE_REGION_BIOS);
	}
	return bios_regions == 1;
}

enum cb_err capsule_apply_policy_verified(const struct capsule_update_plan *plan,
					  const struct capsule_media_policy *policy,
					  const struct capsule_media_backend *media,
					  void *scratch, size_t scratch_bytes)
{
	if (!plan_allowed(plan, policy) || !media || !scratch || !media->read ||
	    !media->erase || !media->write || media->size != policy->media_size ||
	    media->erase_size != policy->erase_size ||
	    scratch_bytes < policy->erase_size)
		return CB_ERR;

	for (size_t region_index = 0; region_index < plan->region_count;
	     region_index++) {
		const struct lb_capsule_update_region *region =
			&plan->regions[region_index];

		for (u64 done = 0; done < region->size;
		     done += policy->erase_size) {
			u64 flash_offset = region->flash_offset + done;
			u64 image_offset = region->image_offset + done;
			size_t size = policy->erase_size;
			const u8 *source = plan->image;

			if (media->erase(media->context, flash_offset, size) !=
			    CB_SUCCESS ||
			    media->write(media->context, flash_offset,
				&source[image_offset], size) != CB_SUCCESS ||
			    media->read(media->context, flash_offset, scratch, size) !=
				CB_SUCCESS ||
			    memcmp(scratch, &source[image_offset], size))
				return CB_ERR;
		}
	}
	return CB_SUCCESS;
}
