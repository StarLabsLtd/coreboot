/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#include "capsule.h"
#include <commonlib/bsd/fmap_serialized.h>
#include <commonlib/helpers.h>
#include <cpu/x86/smm.h>
#include <endian.h>
#include <fmap_config.h>
#include <string.h>

bool smm_capsule_regions(const void *data, size_t size,
	struct lb_capsule_update_region *regions, uint32_t *count)
{
	const struct fmap *fmap = data;
	const struct fmap_area *areas = (const void *)(fmap + 1);
	const char *names = CONFIG_DRIVERS_EFI_CAPSULE_REGIONS;
	uint32_t bios = 0;
	size_t area_count;

	*count = 0;
	if (size < sizeof(*fmap) || memcmp(fmap->signature, FMAP_SIGNATURE, 8) ||
	    fmap->ver_major != 1 || le64toh(fmap->base) ||
	    le32toh(fmap->size) != CONFIG_ROM_SIZE)
		return false;
	area_count = le16toh(fmap->nareas);
	if (!area_count || area_count > (size - sizeof(*fmap)) / sizeof(*areas))
		return false;
	while (*names) {
		char name[FMAP_STRLEN] = { 0 };
		const struct fmap_area *found = NULL;
		size_t length = 0;
		uint64_t offset, length_bytes;

		while (*names == ' ' || *names == '\t')
			names++;
		if (!*names)
			break;
		while (*names && *names != ' ' && *names != '\t') {
			if (length == sizeof(name) - 1 || *names <= ' ' || *names > '~')
				return false;
			name[length++] = *names++;
		}
		if (*count == SMM_CAPSULE_MAX_REGIONS)
			return false;
		for (size_t i = 0; i < area_count; i++) {
			if (memcmp(areas[i].name, name, sizeof(name)))
				continue;
			if (found)
				return false;
			found = &areas[i];
		}
		if (!found)
			return false;
		offset = le32toh(found->offset);
		length_bytes = le32toh(found->size);
		if (!length_bytes || offset > CONFIG_ROM_SIZE ||
		    length_bytes > CONFIG_ROM_SIZE - offset ||
		    offset % SMM_CAPSULE_ERASE_SIZE || length_bytes % SMM_CAPSULE_ERASE_SIZE ||
		    (offset < (uint64_t)FMAP_SECTION_SMMSTORE_START + FMAP_SECTION_SMMSTORE_SIZE &&
		     FMAP_SECTION_SMMSTORE_START < offset + length_bytes) ||
		    (offset < (uint64_t)FMAP_SECTION_FMAP_START + FMAP_SECTION_FMAP_SIZE &&
		     FMAP_SECTION_FMAP_START < offset + length_bytes))
			return false;
#if defined(FMAP_SECTION_SRK_PIN_V1_START)
		if (offset < ALIGN_UP((uint64_t)FMAP_SECTION_SRK_PIN_V1_START +
			FMAP_SECTION_SRK_PIN_V1_SIZE, CONFIG_SMMSTORE_BLOCK_SIZE) &&
		    ALIGN_DOWN(FMAP_SECTION_SRK_PIN_V1_START, CONFIG_SMMSTORE_BLOCK_SIZE) <
			offset + length_bytes)
			return false;
#endif
		for (size_t i = 0; i < *count; i++)
			if (offset < regions[i].flash_offset + regions[i].size &&
			    regions[i].flash_offset < offset + length_bytes)
				return false;
		regions[*count] = (struct lb_capsule_update_region) {
			.image_offset = offset, .flash_offset = offset, .size = length_bytes,
			.flags = !strcmp(name, "COREBOOT") ? LB_CAPSULE_REGION_BIOS : 0,
		};
		bios += regions[*count].flags != 0;
		(*count)++;
	}
	return *count && bios == 1;
}

void smm_capsule_endpoint(struct lb_capsule_broker_endpoint *endpoint,
	uintptr_t communication, uintptr_t staging)
{
	*endpoint = (struct lb_capsule_broker_endpoint) {
		.tag = LB_TAG_CAPSULE_BROKER_ENDPOINT, .size = sizeof(*endpoint),
		.revision = LB_CAPSULE_BROKER_ENDPOINT_RAM_REVISION,
		.header_size = sizeof(*endpoint), .flags = LB_CAPSULE_ENDPOINT_SNAPSHOT_FLAGS,
		.generation = 1, .communication_base = communication,
		.communication_size = 168, .message_size = 168,
		.staging_base = staging, .staging_size = CONFIG_ROM_SIZE + 1 * MiB,
		.transport = LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8,
		.trigger_width = 1, .trigger_address = pm_acpi_smi_cmd_port(),
		.trigger_value = APM_CNT_CAPSULE,
	};
}
