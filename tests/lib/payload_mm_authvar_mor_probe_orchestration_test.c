/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_mor_identity.h>
#include <boot/payload_mm_authvar_mor_probe.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <commonlib/region.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(c) do { if (!(c)) abort(); } while (0)
#define BLOCK_SIZE 4096U
#define REGION_SIZE (4U * BLOCK_SIZE)
#define FV_HEADER_SIZE 72U
#define STORE_SIZE (BLOCK_SIZE - FV_HEADER_SIZE)

static uint8_t media[REGION_SIZE];
static int lookup_status;
static bool map_fails;
static int unmap_status;
static enum cb_err plan_status;
static enum payload_mm_authvar_ftw_action plan_action;
static enum cb_err find_status;
static bool find_found;
static uint32_t find_attributes;
static uint32_t find_size;
static uint32_t find_offset;
static bool exact_identity_seen;
static uint32_t variable_offset;
static uint32_t variable_size;
static uint32_t fv_header_size;
static uint32_t variable_store_size;
static uint32_t geometry_block_size;
static uint32_t geometry_block_count;
static uint32_t working_offset;
static uint32_t working_size;
static uint32_t spare_offset;
static uint32_t spare_size;

static void *test_mmap(const struct region_device *rdev, size_t offset, size_t size)
{
	(void)rdev;
	if (map_fails || offset > sizeof(media) || size > sizeof(media) - offset)
		return NULL;
	return media + offset;
}

static int test_munmap(const struct region_device *rdev, void *mapping)
{
	(void)rdev;
	assert(mapping == media);
	return unmap_status;
}

static const struct region_device_ops test_ops = {
	.mmap = test_mmap,
	.munmap = test_munmap,
};

void *rdev_mmap(const struct region_device *rdev, size_t offset, size_t size)
{
	return rdev->ops->mmap(rdev, offset, size);
}

int rdev_munmap(const struct region_device *rdev, void *mapping)
{
	return rdev->ops->munmap(rdev, mapping);
}

int smmstore_lookup_read_region(struct region_device *rstore)
{
	if (lookup_status < 0)
		return lookup_status;
	*rstore = (struct region_device)REGION_DEV_INIT(&test_ops, 0, REGION_SIZE);
	return 0;
}

enum cb_err payload_mm_authvar_ftw_plan(const void *region, size_t region_size,
	size_t block_size, struct payload_mm_authvar_ftw_plan *plan)
{
	assert(region == media && region_size == sizeof(media));
	assert(block_size == BLOCK_SIZE);
	memset(plan, 0, sizeof(*plan));
	plan->action = plan_action;
	plan->geometry.variable_offset = variable_offset;
	plan->geometry.variable_size = variable_size;
	plan->geometry.block_size = geometry_block_size;
	plan->geometry.block_count = geometry_block_count;
	plan->geometry.working_offset = working_offset;
	plan->geometry.working_size = working_size;
	plan->geometry.spare_offset = spare_offset;
	plan->geometry.spare_size = spare_size;
	plan->fv_header_size = fv_header_size;
	plan->variable_store_size = variable_store_size;
	return plan_status;
}

bool payload_mm_authvar_fv_geometry(struct payload_mm_authvar_fv_geometry *geometry,
	size_t region_size, size_t requested_block_size)
{
	assert(region_size == REGION_SIZE && requested_block_size == BLOCK_SIZE);
	*geometry = (struct payload_mm_authvar_fv_geometry) {
		.block_size = BLOCK_SIZE,
		.block_count = 4U,
		.variable_offset = 0U,
		.variable_size = BLOCK_SIZE,
		.working_offset = BLOCK_SIZE,
		.working_size = BLOCK_SIZE,
		.spare_offset = 2U * BLOCK_SIZE,
		.spare_size = 2U * BLOCK_SIZE,
	};
	return true;
}

enum cb_err payload_mm_authvar_store_find_one(
	struct payload_mm_authvar_store_entry *entry, bool *found,
	const void *store, size_t buffer_size,
	const struct payload_mm_authvar_store_limits *limits,
	const uint8_t vendor_guid[16], const void *name, size_t name_size)
{
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;

	assert(store == media + FV_HEADER_SIZE && buffer_size == STORE_SIZE);
	assert(limits->maximum_store_size ==
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_SIZE);
	exact_identity_seen = name_size == identity->name_size &&
		!memcmp(vendor_guid, identity->vendor_guid, sizeof(identity->vendor_guid)) &&
		!memcmp(name, identity->name, name_size);
	memset(entry, 0, sizeof(*entry));
	*found = find_found;
	if (find_found) {
		entry->attributes = find_attributes;
		entry->data_size = find_size;
		entry->data_offset = find_offset;
	}
	return find_status;
}

static void reset_case(void)
{
	memset(media, 0, sizeof(media));
	lookup_status = 0;
	map_fails = false;
	unmap_status = 0;
	plan_status = CB_SUCCESS;
	plan_action = PAYLOAD_MM_AUTHVAR_FTW_CLEAN;
	find_status = CB_SUCCESS;
	find_found = true;
	find_attributes = PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES;
	find_size = 1U;
	find_offset = 128U;
	exact_identity_seen = false;
	variable_offset = 0U;
	variable_size = BLOCK_SIZE;
	fv_header_size = FV_HEADER_SIZE;
	variable_store_size = STORE_SIZE;
	geometry_block_size = BLOCK_SIZE;
	geometry_block_count = 4U;
	working_offset = BLOCK_SIZE;
	working_size = BLOCK_SIZE;
	spare_offset = 2U * BLOCK_SIZE;
	spare_size = 2U * BLOCK_SIZE;
}

static void expect_error(void)
{
	struct payload_mm_authvar_mor_entry output = { 0xa5, 0xa5, 0xa5a5 };
	const struct payload_mm_authvar_mor_entry zero = { 0 };

	assert(payload_mm_authvar_mor_probe_entry(&output) == CB_ERR);
	assert(!memcmp(&output, &zero, sizeof(output)));
}

static void infrastructure_failures(void)
{
	reset_case();
	lookup_status = -1;
	expect_error();
	reset_case();
	map_fails = true;
	expect_error();
	reset_case();
	plan_status = CB_ERR;
	expect_error();
	reset_case();
	unmap_status = -1;
	expect_error();
	reset_case();
	find_status = CB_ERR;
	expect_error();
}

static void non_clean_actions(void)
{
	for (enum payload_mm_authvar_ftw_action action =
		PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
	     action <= PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE; action++) {
		if (action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN)
			continue;
		reset_case();
		plan_action = action;
		expect_error();
	}
}

static void geometry_failures(void)
{
	uint32_t output_words[2];

	reset_case();
	/* Exercise the null-output path without allowing any lookup. */
	lookup_status = -1;
	assert(payload_mm_authvar_mor_probe_entry(NULL) == CB_ERR);
	memset(output_words, 0xa5, sizeof(output_words));
	assert(payload_mm_authvar_mor_probe_entry(
		(struct payload_mm_authvar_mor_entry *)((uint8_t *)output_words + 1U)) ==
		CB_ERR);
	for (size_t i = 0; i < sizeof(output_words); i++)
		assert(((const uint8_t *)output_words)[i] == 0xa5U);
	assert(payload_mm_authvar_mor_probe_entry(
		(struct payload_mm_authvar_mor_entry *)(UINTPTR_MAX - 1U)) == CB_ERR);

	reset_case();
	variable_offset = 1U;
	expect_error();
	reset_case();
	geometry_block_size--;
	expect_error();
	reset_case();
	geometry_block_count--;
	expect_error();
	reset_case();
	variable_size = REGION_SIZE + 1U;
	expect_error();
	reset_case();
	working_offset++;
	expect_error();
	reset_case();
	working_size--;
	expect_error();
	reset_case();
	spare_offset++;
	expect_error();
	reset_case();
	spare_size--;
	expect_error();
	reset_case();
	fv_header_size = BLOCK_SIZE + 1U;
	expect_error();
	reset_case();
	variable_store_size--;
	expect_error();
	reset_case();
	/* An out-of-range record offset is a final independent geometry guard. */
	find_offset = STORE_SIZE;
	expect_error();
}

static void record_shape_and_values(void)
{
	struct payload_mm_authvar_mor_entry output;
	static const uint8_t values[] = { 0U, 1U, 16U, 17U, 255U };

	reset_case();
	find_found = false;
	memset(&output, 0xa5, sizeof(output));
	assert(payload_mm_authvar_mor_probe_entry(&output) == CB_SUCCESS);
	assert(!output.present && !output.value && !output.reserved);
	assert(exact_identity_seen);

	for (size_t i = 0; i < sizeof(values); i++) {
		reset_case();
		media[FV_HEADER_SIZE + find_offset] = values[i];
		memset(&output, 0xa5, sizeof(output));
		assert(payload_mm_authvar_mor_probe_entry(&output) == CB_SUCCESS);
		assert(output.present == 1U && output.value == values[i] &&
			!output.reserved && exact_identity_seen);
	}

	reset_case();
	find_attributes = PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES &
		~PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	expect_error();
	reset_case();
	find_attributes = PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES |
		PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	expect_error();
	reset_case();
	find_size = 0U;
	expect_error();
	reset_case();
	find_size = 2U;
	expect_error();
}

static void identity_edges(void)
{
	uint8_t guid[PAYLOAD_MM_AUTHVAR_MOR_GUID_SIZE];
	uint16_t name[PAYLOAD_MM_AUTHVAR_MOR_CONTROL_NAME_SIZE / sizeof(uint16_t)];
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;

	assert(payload_mm_authvar_mor_classify(identity->vendor_guid, identity->name,
		identity->name_size) == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL);
	memcpy(guid, identity->vendor_guid, sizeof(guid));
	guid[sizeof(guid) - 1U] ^= 1U;
	assert(payload_mm_authvar_mor_classify(guid, identity->name,
		identity->name_size) == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE);
	memcpy(name, identity->name, sizeof(name));
	name[0] ^= 1U;
	assert(payload_mm_authvar_mor_classify(identity->vendor_guid, name,
		sizeof(name)) == PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE);
	assert(payload_mm_authvar_mor_classify(identity->vendor_guid, identity->name,
		identity->name_size - sizeof(uint16_t)) ==
		PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE);
}

int main(void)
{
	infrastructure_failures();
	non_clean_actions();
	geometry_failures();
	record_shape_and_values();
	identity_edges();
	return 0;
}
