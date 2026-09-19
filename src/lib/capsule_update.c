/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_update.h>
#include <string.h>

static bool guid_present(const uint8_t guid[16])
{
	uint8_t bits = 0;

	for (size_t i = 0; i < 16; i++)
		bits |= guid[i];
	return bits != 0;
}

static bool range_valid(uint64_t offset, uint64_t size, uint64_t limit)
{
	return size && offset <= limit && size <= limit - offset;
}

static bool ranges_overlap(uint64_t left_offset, uint64_t left_size,
	uint64_t right_offset, uint64_t right_size)
{
	return left_offset < right_offset + right_size &&
		right_offset < left_offset + left_size;
}

static enum cb_err validate_regions(const struct lb_capsule_handoff *handoff)
{
	size_t bios_regions = 0;

	for (size_t i = 0; i < handoff->region_count; i++) {
		const struct lb_capsule_update_region *region = &handoff->regions[i];

		if (region->reserved ||
		    region->flags & ~LB_CAPSULE_REGION_VALID_FLAGS ||
		    !range_valid(region->image_offset, region->size,
			handoff->image_size) ||
		    !range_valid(region->flash_offset, region->size,
			handoff->boot_media_size) ||
		    region->image_offset % handoff->block_size ||
		    region->flash_offset % handoff->erase_size ||
		    region->size % handoff->erase_size ||
		    ranges_overlap(region->flash_offset, region->size,
			handoff->smmstore_offset, handoff->smmstore_size))
			return CB_ERR;
		for (size_t previous = 0; previous < i; previous++) {
			const struct lb_capsule_update_region *other =
				&handoff->regions[previous];

			if (ranges_overlap(region->image_offset, region->size,
				    other->image_offset, other->size) ||
			    ranges_overlap(region->flash_offset, region->size,
				    other->flash_offset, other->size))
				return CB_ERR;
		}
		bios_regions += !!(region->flags & LB_CAPSULE_REGION_BIOS);
	}
	return bios_regions == 1 ? CB_SUCCESS : CB_ERR;
}

enum cb_err capsule_handoff_validate(const struct lb_capsule_handoff *handoff,
	size_t bytes, const struct lb_efi_fw_info *firmware)
{
	size_t expected;

	if (!handoff || !firmware || bytes < sizeof(*handoff) ||
	    firmware->tag != LB_TAG_EFI_FW_INFO ||
	    firmware->size != sizeof(*firmware) ||
	    handoff->tag != LB_TAG_CAPSULE_HANDOFF ||
	    handoff->revision != LB_CAPSULE_HANDOFF_REVISION ||
	    handoff->header_size != sizeof(*handoff) ||
	    handoff->flags != LB_CAPSULE_HANDOFF_REQUIRED_FLAGS ||
	    handoff->broker_type != LB_CAPSULE_BROKER_COREBOOT_UPDATE ||
	    handoff->broker_capabilities !=
		LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES ||
	    handoff->capsule_format != LB_CAPSULE_FORMAT_FMP_V3 ||
	    handoff->authentication_format != LB_CAPSULE_AUTH_EFI_PKCS7 ||
	    handoff->board_binding_format !=
		LB_CAPSULE_BOARD_BINDING_CBFS_BUILD_INFO_V1 ||
	    handoff->payload_format != LB_CAPSULE_PAYLOAD_MSS1_V1 ||
	    handoff->capsule_flags != LB_CAPSULE_FLAGS_PERSIST_RESET ||
	    handoff->reserved16 || handoff->reserved32[0] ||
	    handoff->reserved32[1] || !handoff->region_count ||
	    handoff->region_count > CAPSULE_UPDATE_MAX_REGIONS)
		return CB_ERR;
	expected = sizeof(*handoff) + handoff->region_count *
		sizeof(handoff->regions[0]);
	if (handoff->size != expected || bytes != expected ||
	    !guid_present(firmware->guid) ||
	    !guid_present(handoff->image_type_guid) ||
	    memcmp(firmware->guid, handoff->image_type_guid,
		sizeof(firmware->guid)) || firmware->version != handoff->version ||
	    firmware->lowest_supported_version !=
		handoff->lowest_supported_version ||
	    firmware->fw_size != handoff->image_size ||
	    handoff->lowest_supported_version > handoff->version ||
	    !handoff->image_size || !handoff->boot_media_size ||
	    handoff->image_size > handoff->boot_media_size ||
	    !handoff->block_size || !handoff->erase_size ||
	    handoff->erase_size % handoff->block_size ||
	    !range_valid(handoff->smmstore_offset, handoff->smmstore_size,
		handoff->boot_media_size))
		return CB_ERR;
	return validate_regions(handoff);
}
