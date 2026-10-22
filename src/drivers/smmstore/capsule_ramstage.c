/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#include "capsule.h"
#include <boot/coreboot_tables.h>
#include <boot_device.h>
#include <cbmem.h>
#include <console/console.h>
#include <drivers/efi/firmware_info.h>
#include <fmap_config.h>
#include <string.h>

void lb_smm_capsule(struct lb_header *header)
{
	uint8_t policy[sizeof(struct lb_capsule_handoff) + SMM_CAPSULE_MAX_REGIONS *
		sizeof(struct lb_capsule_update_region)] __aligned(8) = { 0 };
	struct lb_efi_fw_info firmware;
	struct lb_capsule_broker_endpoint endpoint;
	const struct cbmem_entry *staging = cbmem_entry_find(CBMEM_ID_CAPSULE_STAGING);
	const struct cbmem_entry *communication = cbmem_entry_find(CBMEM_ID_SMM_COMBUFFER);
	struct lb_capsule_handoff *handoff = (void *)policy;
	uint32_t count;
	uint8_t fmap[FMAP_SIZE];

	/* lb_authvar_service has already initialized the real SMM owners synchronously. */
	if (!staging || !communication ||
	    !efi_firmware_info(&firmware) ||
	    rdev_readat(boot_device_ro(), fmap, FMAP_OFFSET, sizeof(fmap)) != sizeof(fmap) ||
	    !smm_capsule_regions(fmap, sizeof(fmap), handoff->regions, &count))
		die("Capsules: protected update policy unavailable\n");
	handoff->region_count = count;
	handoff->tag = LB_TAG_CAPSULE_HANDOFF;
	handoff->size = sizeof(*handoff) + handoff->region_count * sizeof(handoff->regions[0]);
	handoff->revision = LB_CAPSULE_HANDOFF_REVISION;
	handoff->header_size = sizeof(*handoff);
	handoff->flags = LB_CAPSULE_HANDOFF_REQUIRED_FLAGS;
	handoff->broker_type = LB_CAPSULE_BROKER_COREBOOT_UPDATE;
	handoff->capsule_format = LB_CAPSULE_FORMAT_FMP_V3;
	handoff->authentication_format = LB_CAPSULE_AUTH_EFI_PKCS7;
	handoff->board_binding_format = LB_CAPSULE_BOARD_BINDING_CBFS_BUILD_INFO_V1;
	handoff->payload_format = LB_CAPSULE_PAYLOAD_MSS1_V1;
	handoff->broker_capabilities = LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES;
	memcpy(handoff->image_type_guid, firmware.guid, 16);
	handoff->version = firmware.version;
	handoff->lowest_supported_version = firmware.lowest_supported_version;
	handoff->capsule_flags = LB_CAPSULE_FLAGS_PERSIST_RESET;
	handoff->block_size = handoff->erase_size = SMM_CAPSULE_ERASE_SIZE;
	handoff->image_size = handoff->boot_media_size = CONFIG_ROM_SIZE;
	handoff->smmstore_offset = FMAP_SECTION_SMMSTORE_START;
	handoff->smmstore_size = FMAP_SECTION_SMMSTORE_SIZE;
	memcpy(lb_new_record(header), &policy, handoff->size);
	smm_capsule_endpoint(&endpoint, (uintptr_t)cbmem_entry_start(communication),
		(uintptr_t)cbmem_entry_start(staging));
	memcpy(lb_new_record(header), &endpoint, sizeof(endpoint));
}
