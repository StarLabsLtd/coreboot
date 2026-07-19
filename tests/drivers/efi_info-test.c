/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <commonlib/bsd/fmap_serialized.h>
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <drivers/efi/info.h>
#include <fmap_config.h>
#include <stdint.h>
#include <string.h>
#include <tests/test.h>
#include <vb2_sha.h>

const char mainboard_vendor[] = "Star Labs";
const char mainboard_part_number[] = "Test Board";

_Static_assert(sizeof(struct lb_efi_capsule_region) == 28,
	       "capsule policy region ABI changed");
_Static_assert(sizeof(struct lb_efi_capsule_policy) == 96,
	       "capsule policy header ABI changed");

struct test_area {
	const char *name;
	size_t offset;
	size_t size;
	uint16_t flags;
};

static struct test_area areas[40];
static size_t area_count;
static uint8_t record_buffer[2048];
static size_t new_record_count;
static uint8_t live_fmap_sha256[VB2_SHA256_DIGEST_SIZE];

struct lb_record *lb_new_record(struct lb_header *header)
{
	(void)header;
	new_record_count++;
	return (struct lb_record *)record_buffer;
}

static void add_area(const char *name, size_t offset, size_t size, uint16_t flags)
{
	assert_true(area_count < ARRAY_SIZE(areas));
	areas[area_count++] = (struct test_area) {
		.name = name,
		.offset = offset,
		.size = size,
		.flags = flags,
	};
}

static int find_area(const char *name, struct region *region, uint16_t *flags)
{
	if (name == NULL || region == NULL)
		return -1;

	for (size_t i = 0; i < area_count; i++) {
		if (strcmp(name, areas[i].name))
			continue;
		region->offset = areas[i].offset;
		region->size = areas[i].size;
		if (flags != NULL)
			*flags = areas[i].flags;
		return 0;
	}

	return -1;
}

int fmap_locate_area(const char *name, struct region *region)
{
	return find_area(name, region, NULL);
}

int fmap_locate_area_with_flags(const char *name, struct region *region, uint16_t *flags)
{
	return find_area(name, region, flags);
}

int fmap_region_overlaps_area_with_flags(const struct region *region)
{
	if (region == NULL || region_sz(region) == 0)
		return -1;

	for (size_t i = 0; i < area_count; i++) {
		const struct region area = {
			.offset = areas[i].offset,
			.size = areas[i].size,
		};
		if (areas[i].flags != 0 && region_overlap(region, &area))
			return 1;
	}

	return 0;
}

uint64_t get_fmap_flash_offset(void)
{
	return FMAP_OFFSET;
}

ssize_t fmap_read_directory(void *buffer, size_t size)
{
	if (buffer == NULL || size < FMAP_SIZE)
		return -1;
	memset(buffer, 0xa5, FMAP_SIZE);
	return FMAP_SIZE;
}

vb2_error_t vb2_hash_calculate(bool allow_hwcrypto, const void *buffer,
			       uint32_t size, enum vb2_hash_algorithm algorithm,
			       struct vb2_hash *hash)
{
	(void)allow_hwcrypto;
	assert_non_null(buffer);
	assert_int_equal(FMAP_SIZE, size);
	assert_int_equal(VB2_HASH_SHA256, algorithm);
	hash->algo = algorithm;
	memcpy(hash->raw, live_fmap_sha256, sizeof(live_fmap_sha256));
	return VB2_SUCCESS;
}

static int setup(void **state)
{
	(void)state;
	area_count = 0;
	new_record_count = 0;
	memset(record_buffer, 0, sizeof(record_buffer));
	for (size_t i = 0; i < sizeof(live_fmap_sha256); i++)
		live_fmap_sha256[i] = i;
	add_area("COREBOOT", 0x00800000, 0x00700000, 0);
	add_area("EC", 0x00010000, 0x00020000, 0);
	add_area("FMAP", FMAP_OFFSET, FMAP_SIZE, FMAP_AREA_STATIC);
	add_area("SMMSTORE", 0x00700000, 0x00080000, FMAP_AREA_PRESERVE);
	return 0;
}

static void assert_buffer_value(const uint8_t *buffer, size_t size, uint8_t value)
{
	for (size_t i = 0; i < size; i++)
		assert_int_equal(buffer[i], value);
}

static void test_build_policy(void **state)
{
	(void)state;
	uint8_t buffer[2048];
	struct lb_efi_capsule_policy *policy = (void *)buffer;
	const uint8_t image_guid[] = {
		0xe6, 0xd0, 0x5c, 0x97, 0x40, 0xc5, 0x2b, 0x4e,
		0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d,
	};
	const uint8_t fmap_sha256[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	};

	assert_int_equal(CB_SUCCESS,
			 efi_capsule_policy_build(policy, sizeof(buffer), "  COREBOOT\tEC\n"));
	assert_int_equal(LB_TAG_EFI_CAPSULE_POLICY, policy->tag);
	assert_int_equal(LB_EFI_CAPSULE_POLICY_VERSION, policy->version);
	assert_int_equal(sizeof(*policy), policy->header_size);
	assert_int_equal(0x01100000, policy->max_capsule_size);
	assert_int_equal(0x01000000, policy->firmware_size);
	assert_memory_equal(image_guid, policy->image_guid, sizeof(policy->image_guid));
	assert_int_equal(0, policy->hardware_instance);
	assert_int_equal(FMAP_OFFSET, policy->fmap_offset);
	assert_int_equal(FMAP_SIZE, policy->fmap_size);
	assert_memory_equal(fmap_sha256, policy->fmap_sha256, sizeof(policy->fmap_sha256));
	assert_int_equal(2, policy->smmstore_protocol_revision);
	assert_int_equal(2, policy->region_count);
	assert_true(IS_ALIGNED(policy->size, LB_ENTRY_ALIGN));
	assert_int_equal(0x00800000, policy->regions[0].offset);
	assert_int_equal(0x00700000, policy->regions[0].size);
	assert_int_equal(strlen("COREBOOT"), policy->regions[0].name_length);
	assert_memory_equal("COREBOOT", policy->regions[0].name,
			    policy->regions[0].name_length);
	assert_int_equal(0x00010000, policy->regions[1].offset);
	assert_int_equal(0x00020000, policy->regions[1].size);
	assert_memory_equal("EC", policy->regions[1].name, policy->regions[1].name_length);
	assert_string_equal("Star Labs", (char *)buffer + policy->vendor_offset);
	assert_string_equal("Test Board", (char *)buffer + policy->part_number_offset);
	assert_int_equal(
		LB_EFI_CAPSULE_POLICY_REQUIRE_AUTHENTICATION |
		LB_EFI_CAPSULE_POLICY_REQUIRE_DRIVER_FREE |
		LB_EFI_CAPSULE_POLICY_REQUIRE_RMAP |
		LB_EFI_CAPSULE_POLICY_REQUIRE_EXACT_REGIONS |
		LB_EFI_CAPSULE_POLICY_REQUIRE_FMAP_DIGEST,
		policy->capabilities);
}

static void test_accepts_sixteen_byte_region_name(void **state)
{
	(void)state;
	uint8_t buffer[2048];
	struct lb_efi_capsule_policy *policy = (void *)buffer;

	add_area("FIRMWARE_REGION1", 0x00040000, 0x00010000, 0);
	assert_int_equal(CB_SUCCESS,
			 efi_capsule_policy_build(policy, sizeof(buffer), "FIRMWARE_REGION1"));
	assert_int_equal(LB_EFI_CAPSULE_REGION_NAME_LEN, policy->regions[0].name_length);
	assert_memory_equal("FIRMWARE_REGION1", policy->regions[0].name,
			    LB_EFI_CAPSULE_REGION_NAME_LEN);
}

static void test_rejects_malformed_policy_without_output(void **state)
{
	(void)state;
	uint8_t buffer[2048];
	const char *const cases[] = {
		"",
		"COREBOOT COREBOOT",
		"coreboot",
		"MISSING",
		"SMMSTORE",
		"FIRMWARE_REGION12",
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		memset(buffer, 0xa5, sizeof(buffer));
		assert_int_not_equal(CB_SUCCESS,
				     efi_capsule_policy_build((void *)buffer, sizeof(buffer),
							      cases[i]));
		assert_buffer_value(buffer, sizeof(buffer), 0xa5);
	}
}

static void test_rejects_flags_overlap_and_bad_geometry(void **state)
{
	(void)state;
	uint8_t buffer[2048];

	areas[0].flags = FMAP_AREA_RO;
	assert_int_not_equal(CB_SUCCESS,
			     efi_capsule_policy_build((void *)buffer, sizeof(buffer), "COREBOOT"));

	areas[0].flags = 0;
	add_area("FLAGGED_CHILD", 0x00810000, 0x00001000, FMAP_AREA_PRESERVE);
	assert_int_not_equal(CB_SUCCESS,
			     efi_capsule_policy_build((void *)buffer, sizeof(buffer), "COREBOOT"));
	area_count--;

	areas[0].offset = 0x00680000;
	assert_int_not_equal(CB_SUCCESS,
			     efi_capsule_policy_build((void *)buffer, sizeof(buffer), "COREBOOT"));

	areas[0].offset = 0x00800000;
	areas[1].offset = 0x00810000;
	assert_int_not_equal(CB_SUCCESS,
			     efi_capsule_policy_build((void *)buffer, sizeof(buffer),
						      "COREBOOT EC"));

	areas[1].offset = CONFIG_ROM_SIZE;
	areas[1].size = 1;
	assert_int_not_equal(CB_SUCCESS,
			     efi_capsule_policy_build((void *)buffer, sizeof(buffer), "EC"));
}

static void test_rejects_short_output_buffer_without_output(void **state)
{
	(void)state;
	uint8_t buffer[sizeof(struct lb_efi_capsule_policy)];

	memset(buffer, 0x5a, sizeof(buffer));
	assert_int_not_equal(CB_SUCCESS,
			     efi_capsule_policy_build((void *)buffer, sizeof(buffer), "COREBOOT"));
	assert_buffer_value(buffer, sizeof(buffer), 0x5a);
}

static void test_table_emission_requires_valid_live_policy(void **state)
{
	(void)state;
	struct lb_header header = { 0 };

	lb_efi_capsule_policy(&header);
	assert_int_equal(1, new_record_count);
	assert_int_equal(LB_TAG_EFI_CAPSULE_POLICY,
			 ((struct lb_efi_capsule_policy *)record_buffer)->tag);

	new_record_count = 0;
	area_count = 0;
	memset(record_buffer, 0xa5, sizeof(record_buffer));
	lb_efi_capsule_policy(&header);
	assert_int_equal(0, new_record_count);
	assert_buffer_value(record_buffer, sizeof(record_buffer), 0xa5);
}

static void test_rejects_live_fmap_digest_mismatch(void **state)
{
	(void)state;
	uint8_t buffer[2048];

	live_fmap_sha256[0] ^= 1;
	memset(buffer, 0x5a, sizeof(buffer));
	assert_int_not_equal(CB_SUCCESS,
			     efi_capsule_policy_build((void *)buffer, sizeof(buffer), "COREBOOT"));
	assert_buffer_value(buffer, sizeof(buffer), 0x5a);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_build_policy, setup),
		cmocka_unit_test_setup(test_accepts_sixteen_byte_region_name, setup),
		cmocka_unit_test_setup(test_rejects_malformed_policy_without_output, setup),
		cmocka_unit_test_setup(test_rejects_flags_overlap_and_bad_geometry, setup),
		cmocka_unit_test_setup(test_rejects_short_output_buffer_without_output, setup),
		cmocka_unit_test_setup(test_table_emission_requires_valid_live_policy, setup),
		cmocka_unit_test_setup(test_rejects_live_fmap_digest_mismatch, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
