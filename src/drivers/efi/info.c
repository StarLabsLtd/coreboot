/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#if CONFIG(DRIVERS_EFI_CAPSULE_POLICY)
#include <capsule_policy_fmap_hash.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/helpers.h>
#include <drivers/efi/info.h>
#include <fmap.h>
#include <fmap_config.h>
#include <identity.h>
#include <limits.h>
#include <vb2_sha.h>
#endif
#include <console/console.h>
#include <stdint.h>
#include <string.h>
#include <uuid.h>

#if CONFIG(DRIVERS_EFI_CAPSULE_POLICY)
#define CAPSULE_MAX_ENVELOPE_SIZE MiB
#define CAPSULE_SMMSTORE_PROTOCOL_REVISION 2

static const uint8_t capsule_policy_fmap_sha256[] = CAPSULE_POLICY_FMAP_SHA256;
_Static_assert(sizeof(capsule_policy_fmap_sha256) == 32,
	       "capsule policy FMAP SHA-256 must be 32 bytes");

static const char *const protected_regions[] = {
	"CONSOLE",
	"FMAP",
	"FPF_STATUS",
	"GBB",
	"RO_VPD",
	"RW_MRC_CACHE",
	"RW_VAR_MRC_CACHE",
	"RW_VPD",
	"SI_DESC",
	"SI_ME",
	"SMMSTORE",
	"UNIFIED_MRC_CACHE",
};

static const char *const forbidden_regions[] = {
	"BIOS",
	"OBB",
	"OBBP",
	"SI_ALL",
	"SI_BIOS",
};

static enum cb_err validate_live_fmap_digest(void)
{
	uint8_t serialized_fmap[FMAP_SIZE];
	struct vb2_hash hash;

	if (fmap_read_directory(serialized_fmap, sizeof(serialized_fmap)) !=
	    sizeof(serialized_fmap))
		return CB_ERR;
	if (vb2_hash_calculate(false, serialized_fmap, sizeof(serialized_fmap),
			       VB2_HASH_SHA256, &hash) != VB2_SUCCESS)
		return CB_ERR;
	if (memcmp(hash.raw, capsule_policy_fmap_sha256,
		   sizeof(capsule_policy_fmap_sha256)))
		return CB_ERR;

	return CB_SUCCESS;
}

static bool region_separator(char character)
{
	return character == ' ' || character == '\t' || character == '\n' || character == '\r';
}

static bool valid_region_character(char character)
{
	return (character >= 'A' && character <= 'Z') ||
	       (character >= '0' && character <= '9') || character == '_';
}

static bool named_region(const char *name, const char *const *regions, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (!strcmp(name, regions[i]))
			return true;
	}

	return false;
}

static bool regions_overlap(const struct lb_efi_capsule_region *first,
			    const struct region *second)
{
	const struct region first_region = {
		.offset = first->offset,
		.size = first->size,
	};

	return region_overlap(&first_region, second);
}

static enum cb_err validate_protected_overlap(const struct lb_efi_capsule_region *selected)
{
	struct region protected;

	for (size_t i = 0; i < ARRAY_SIZE(protected_regions); i++) {
		if (fmap_locate_area(protected_regions[i], &protected))
			continue;
		if (region_sz(&protected) == 0 || region_offset(&protected) > CONFIG_ROM_SIZE ||
		    region_sz(&protected) > CONFIG_ROM_SIZE - region_offset(&protected))
			return CB_ERR;
		if (regions_overlap(selected, &protected))
			return CB_ERR;
	}

	return CB_SUCCESS;
}

static enum cb_err append_policy_region(struct lb_efi_capsule_region *regions,
					size_t count, const char *name, size_t name_length)
{
	struct lb_efi_capsule_region *region = &regions[count];
	struct region live;
	uint16_t flags;

	if (name_length == 0 || name_length > sizeof(region->name))
		return CB_ERR_ARG;

	for (size_t i = 0; i < name_length; i++) {
		if (!valid_region_character(name[i]))
			return CB_ERR_ARG;
	}

	memset(region, 0, sizeof(*region));
	memcpy(region->name, name, name_length);
	region->name_length = name_length;

	char terminated_name[LB_EFI_CAPSULE_REGION_NAME_LEN + 1] = { 0 };
	memcpy(terminated_name, name, name_length);
	if (named_region(terminated_name, protected_regions, ARRAY_SIZE(protected_regions)) ||
	    named_region(terminated_name, forbidden_regions, ARRAY_SIZE(forbidden_regions)))
		return CB_ERR_ARG;

	for (size_t i = 0; i < count; i++) {
		if (regions[i].name_length == name_length &&
		    !memcmp(regions[i].name, name, name_length))
			return CB_ERR_ARG;
	}

	if (fmap_locate_area_with_flags(terminated_name, &live, &flags))
		return CB_ERR;
	if (flags != 0 || region_sz(&live) == 0 ||
	    region_offset(&live) > UINT32_MAX || region_sz(&live) > UINT32_MAX ||
	    region_offset(&live) > CONFIG_ROM_SIZE ||
	    region_sz(&live) > CONFIG_ROM_SIZE - region_offset(&live) ||
	    fmap_region_overlaps_area_with_flags(&live) != 0)
		return CB_ERR;

	region->offset = region_offset(&live);
	region->size = region_sz(&live);
	region->flags = flags;

	for (size_t i = 0; i < count; i++) {
		struct region previous = {
			.offset = regions[i].offset,
			.size = regions[i].size,
		};
		if (regions_overlap(region, &previous))
			return CB_ERR;
	}

	return validate_protected_overlap(region);
}

enum cb_err efi_capsule_policy_build(struct lb_efi_capsule_policy *policy,
				     size_t capacity, const char *region_list)
{
	struct lb_efi_capsule_region regions[LB_EFI_CAPSULE_POLICY_MAX_REGIONS];
	size_t region_count = 0;
	const char *cursor = region_list;
	size_t vendor_size;
	size_t part_number_size;
	size_t vendor_offset;
	size_t part_number_offset;
	size_t record_size;
	uint64_t max_capsule_size = (uint64_t)CONFIG_ROM_SIZE + CAPSULE_MAX_ENVELOPE_SIZE;
	uint8_t image_guid[16];

	if (policy == NULL || region_list == NULL ||
	    CONFIG(DRIVERS_EFI_CAPSULE_ACCEPT_EMBEDDED_DRIVERS) ||
	    CONFIG(DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE) ||
	    CONFIG_DRIVERS_EFI_CAPSULE_TRUSTED_PUBLIC_CERT[0] == '\0')
		return CB_ERR_ARG;
	if (validate_live_fmap_digest() != CB_SUCCESS)
		return CB_ERR;
	if (parse_uuid(image_guid, CONFIG_DRIVERS_EFI_MAIN_FW_GUID))
		return CB_ERR_ARG;

	while (*cursor != '\0') {
		const char *start;
		size_t length;

		while (region_separator(*cursor))
			cursor++;
		if (*cursor == '\0')
			break;

		start = cursor;
		while (*cursor != '\0' && !region_separator(*cursor))
			cursor++;
		length = cursor - start;

		if (region_count == ARRAY_SIZE(regions) ||
		    append_policy_region(regions, region_count, start, length) != CB_SUCCESS)
			return CB_ERR_ARG;
		region_count++;
	}

	if (region_count == 0)
		return CB_ERR_ARG;

	vendor_size = strlen(mainboard_vendor) + 1;
	part_number_size = strlen(mainboard_part_number) + 1;
	vendor_offset = sizeof(*policy) + region_count * sizeof(regions[0]);
	part_number_offset = vendor_offset + vendor_size;
	record_size = ALIGN_UP(part_number_offset + part_number_size, LB_ENTRY_ALIGN);
	const uint64_t fmap_offset = get_fmap_flash_offset();
	if (record_size > capacity || record_size > UINT32_MAX ||
	    vendor_offset > UINT16_MAX || part_number_offset > UINT16_MAX ||
	    max_capsule_size > UINT32_MAX || fmap_offset > UINT32_MAX ||
	    fmap_offset > CONFIG_ROM_SIZE || FMAP_SIZE > CONFIG_ROM_SIZE - fmap_offset)
		return CB_ERR_ARG;

	memset(policy, 0, record_size);
	policy->tag = LB_TAG_EFI_CAPSULE_POLICY;
	policy->size = record_size;
	policy->version = LB_EFI_CAPSULE_POLICY_VERSION;
	policy->header_size = sizeof(*policy);
	policy->capabilities = LB_EFI_CAPSULE_POLICY_REQUIRE_AUTHENTICATION |
				 LB_EFI_CAPSULE_POLICY_REQUIRE_DRIVER_FREE |
				 LB_EFI_CAPSULE_POLICY_REQUIRE_RMAP |
				 LB_EFI_CAPSULE_POLICY_REQUIRE_EXACT_REGIONS |
				 LB_EFI_CAPSULE_POLICY_REQUIRE_FMAP_DIGEST;
	policy->max_capsule_size = max_capsule_size;
	policy->firmware_size = CONFIG_ROM_SIZE;
	memcpy(policy->image_guid, image_guid, sizeof(policy->image_guid));
	policy->hardware_instance = 0;
	policy->fmap_offset = fmap_offset;
	policy->fmap_size = FMAP_SIZE;
	memcpy(policy->fmap_sha256, capsule_policy_fmap_sha256,
	       sizeof(policy->fmap_sha256));
	policy->smmstore_protocol_revision = CAPSULE_SMMSTORE_PROTOCOL_REVISION;
	policy->region_count = region_count;
	policy->vendor_offset = vendor_offset;
	policy->part_number_offset = part_number_offset;
	memcpy(policy->regions, regions, region_count * sizeof(regions[0]));
	memcpy((uint8_t *)policy + vendor_offset, mainboard_vendor, vendor_size);
	memcpy((uint8_t *)policy + part_number_offset, mainboard_part_number, part_number_size);

	return CB_SUCCESS;
}
#endif

static uint32_t efi_fw_version_from_localversion(void)
{
	const char *localversion = CONFIG_LOCALVERSION;
	uint32_t major = 0;
	uint32_t minor = 0;
	const char *p = localversion;

	while (*p != '\0' && (*p < '0' || *p > '9'))
		p++;

	while (*p >= '0' && *p <= '9') {
		major = major * 10 + (*p - '0');
		p++;
	}

	if (*p != '.')
		return 0;
	p++;

	while (*p >= '0' && *p <= '9') {
		minor = minor * 10 + (*p - '0');
		p++;
	}

	if (major > 0xffff || minor > 0xffff)
		return 0;

	return (major << 16) | minor;
}

void lb_efi_fw_info(struct lb_header *header)
{
	uint8_t guid[16];
	struct lb_efi_fw_info *fw_info;
	uint32_t fw_version;
	uint32_t lsv;

	if (parse_uuid(guid, CONFIG_DRIVERS_EFI_MAIN_FW_GUID)) {
		printk(BIOS_ERR, "%s(): failed to parse firmware's GUID: '%s'\n", __func__,
		       CONFIG_DRIVERS_EFI_MAIN_FW_GUID);
		return;
	}

	fw_info = (struct lb_efi_fw_info *)lb_new_record(header);
	fw_info->tag = LB_TAG_EFI_FW_INFO;
	fw_info->size = sizeof(*fw_info);

	memcpy(fw_info->guid, guid, sizeof(guid));

	fw_version = CONFIG_DRIVERS_EFI_MAIN_FW_VERSION;
	if (fw_version == 0) {
		fw_version = efi_fw_version_from_localversion();
		if (fw_version != 0)
			printk(BIOS_DEBUG,
			       "EFI FW version derived from CONFIG_LOCALVERSION '%s': 0x%08x\n",
			       CONFIG_LOCALVERSION, fw_version);
	}

	lsv = CONFIG_DRIVERS_EFI_MAIN_FW_LSV;
	if (lsv == 0)
		lsv = fw_version;

	fw_info->version = fw_version;
	fw_info->lowest_supported_version = lsv;
	fw_info->fw_size = CONFIG_ROM_SIZE;
}

#if CONFIG(DRIVERS_EFI_CAPSULE_POLICY)
void lb_efi_capsule_policy(struct lb_header *header)
{
	union {
		struct lb_efi_capsule_policy policy;
		uint8_t bytes[sizeof(struct lb_efi_capsule_policy) +
			      LB_EFI_CAPSULE_POLICY_MAX_REGIONS *
				      sizeof(struct lb_efi_capsule_region) +
			      sizeof(CONFIG_MAINBOARD_VENDOR) +
			      sizeof(CONFIG_MAINBOARD_PART_NUMBER) + LB_ENTRY_ALIGN];
	} record;
	struct lb_record *destination;

	if (efi_capsule_policy_build(&record.policy, sizeof(record),
				     CONFIG_DRIVERS_EFI_CAPSULE_REGIONS) != CB_SUCCESS) {
		printk(BIOS_ERR, "EFI capsule policy does not match the live FMAP\n");
		return;
	}

	destination = lb_new_record(header);
	memcpy(destination, &record.policy, record.policy.size);
}
#endif
