/* SPDX-License-Identifier: GPL-2.0-only */

#include <cdk2/system_fmp_handoff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coreboot.h"

static struct lb_efi_fw_info firmware;
static UINT8 record_bytes[sizeof(struct cb_capsule_handoff) +
	CDK2_SYSTEM_FMP_MAX_REGIONS * sizeof(struct cb_capsule_update_region)];

EFI_STATUS cdk2_coreboot_find_unique_record(const struct cdk2_coreboot_handoff *coreboot,
					    UINT32 tag,
	UINT32 minimum_size, const void **record)
{
	(void)coreboot;
	if (!record)
		return EFI_INVALID_PARAMETER;
	if (tag == CB_TAG_FW_INFO) {
		if (firmware.size < minimum_size)
			return EFI_COMPROMISED_DATA;
		*record = &firmware;
		return EFI_SUCCESS;
	}
	if (tag != CB_TAG_CAPSULE_HANDOFF ||
	    ((struct cb_capsule_handoff *)record_bytes)->size < minimum_size)
		return EFI_COMPROMISED_DATA;
	*record = record_bytes;
	return EFI_SUCCESS;
}

int main(int argc, char **argv)
{
	static const UINT8 image_type[16] = {
		0xe6, 0xd0, 0x5c, 0x97, 0x40, 0xc5, 0x2b, 0x4e,
		0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d,
	};
	struct cdk2_system_fmp_handoff handoff;
	FILE *fixture;
	size_t bytes;

	if (argc != 2 || !(fixture = fopen(argv[1], "rb")))
		return EXIT_FAILURE;
	bytes = fread(record_bytes, 1, sizeof(record_bytes), fixture);
	if (fclose(fixture) || bytes != 144U)
		return EXIT_FAILURE;
	firmware = (struct lb_efi_fw_info) {
		.tag = CB_TAG_FW_INFO,
		.size = sizeof(firmware),
		.version = 10U,
		.lowest_supported_version = 8U,
		.fw_size = 0x01000000U,
	};
	memcpy(firmware.guid, image_type, sizeof(image_type));
	if (cdk2_system_fmp_handoff_from_coreboot(&(struct cdk2_coreboot_handoff){0},
						  &handoff) != EFI_SUCCESS)
		return EXIT_FAILURE;
	return handoff.region_count == 1U &&
		handoff.regions[0].flash_offset == 0x00800000U &&
		handoff.regions[0].size == 0x00700000U ? EXIT_SUCCESS : EXIT_FAILURE;
}
