/* SPDX-License-Identifier: GPL-2.0-only */

#include "q35_mor_fixture.h"

#include <boot/payload_mm_authvar_mor_identity.h>
#include <boot/payload_mm_authvar_mor_probe.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <boot/payload_mm_authvar_writer.h>
#include <commonlib/region.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static uint8_t first_images[Q35_MOR_FIXTURE_COUNT][Q35_MOR_FIXTURE_SIZE];
static uint8_t second_images[Q35_MOR_FIXTURE_COUNT][Q35_MOR_FIXTURE_SIZE];
static uint8_t *active_media;
static unsigned int replay_count;

static void *fixture_mmap(const struct region_device *rdev, size_t offset,
	size_t size)
{
	(void)rdev;
	return !offset && size == Q35_MOR_FIXTURE_SIZE ? active_media : NULL;
}

static int fixture_munmap(const struct region_device *rdev, void *mapping)
{
	(void)rdev;
	return mapping == active_media ? 0 : -1;
}

static const struct region_device_ops fixture_ops = {
	.mmap = fixture_mmap,
	.munmap = fixture_munmap,
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
	*rstore = (struct region_device)REGION_DEV_INIT(&fixture_ops, 0,
		Q35_MOR_FIXTURE_SIZE);
	return 0;
}

static void oracle_le16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void oracle_le32(uint8_t *bytes, uint32_t value)
{
	for (size_t index = 0; index < sizeof(value); index++)
		bytes[index] = (uint8_t)(value >> (8U * index));
}

static void oracle_le64(uint8_t *bytes, uint64_t value)
{
	for (size_t index = 0; index < sizeof(value); index++)
		bytes[index] = (uint8_t)(value >> (8U * index));
}

static uint32_t oracle_crc32(const uint8_t *bytes, size_t size)
{
	uint32_t crc = UINT32_MAX;

	for (size_t index = 0; index < size; index++) {
		crc ^= bytes[index];
		for (unsigned int bit = 0; bit < 8U; bit++)
			crc = (crc >> 1) ^
				(0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

static void oracle_record(uint8_t *image, uint32_t offset, uint8_t state,
	uint8_t value, uint32_t attributes)
{
	static const uint8_t vendor[16] = {
		0xbe, 0x39, 0x09, 0xe2, 0xd4, 0x32, 0xbe, 0x41,
		0xa1, 0x50, 0x89, 0x7f, 0x85, 0xd4, 0x98, 0x29,
	};
	static const uint8_t name_utf16le[] = {
		0x4d, 0x00, 0x65, 0x00, 0x6d, 0x00, 0x6f, 0x00,
		0x72, 0x00, 0x79, 0x00, 0x4f, 0x00, 0x76, 0x00,
		0x65, 0x00, 0x72, 0x00, 0x77, 0x00, 0x72, 0x00,
		0x69, 0x00, 0x74, 0x00, 0x65, 0x00, 0x52, 0x00,
		0x65, 0x00, 0x71, 0x00, 0x75, 0x00, 0x65, 0x00,
		0x73, 0x00, 0x74, 0x00, 0x43, 0x00, 0x6f, 0x00,
		0x6e, 0x00, 0x74, 0x00, 0x72, 0x00, 0x6f, 0x00,
		0x6c, 0x00, 0x00, 0x00,
	};
	uint8_t *record = image + 72U + offset;

	memset(record, 0xff, 124U);
	oracle_le16(record, 0x55aaU);
	record[2U] = state;
	record[3U] = 0U;
	oracle_le32(record + 4U, attributes);
	memset(record + 8U, 0, 28U);
	oracle_le32(record + 36U, sizeof(name_utf16le));
	oracle_le32(record + 40U, 1U);
	memcpy(record + 44U, vendor, sizeof(vendor));
	memcpy(record + 60U, name_utf16le, sizeof(name_utf16le));
	record[120U] = value;
}

static void oracle_image(enum q35_mor_fixture_kind kind,
	uint8_t image[Q35_MOR_FIXTURE_SIZE])
{
	static const uint8_t fv_guid[16] = {
		0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
		0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
	};
	static const uint8_t store_guid[16] = {
		0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
		0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
	};
	static const uint8_t work_guid[16] = {
		0x2b, 0x29, 0x58, 0x9e, 0x68, 0x7c, 0x7d, 0x49,
		0xa0, 0xce, 0x65, 0x00, 0xfd, 0x9f, 0x1b, 0x95,
	};
	uint8_t *workspace;
	uint16_t checksum = 0;
	uint8_t value = 0x11U;
	uint32_t attributes = 7U;
	bool clean = true;
	bool record = true;

	memset(image, 0xff, Q35_MOR_FIXTURE_SIZE);
	memset(image, 0, 72U);
	memcpy(image + 16U, fv_guid, sizeof(fv_guid));
	oracle_le64(image + 32U, Q35_MOR_FIXTURE_SIZE);
	oracle_le32(image + 40U, 0x4856465fU);
	oracle_le32(image + 44U, 0x00000e36U);
	oracle_le16(image + 48U, 72U);
	image[55U] = 2U;
	oracle_le32(image + 56U, 4U);
	oracle_le32(image + 60U, 4096U);
	for (size_t index = 0; index < 72U; index += 2U)
		checksum = (uint16_t)(checksum + (uint16_t)image[index] +
			((uint16_t)image[index + 1U] << 8));
	oracle_le16(image + 50U, (uint16_t)-checksum);
	memset(image + 72U, 0, 28U);
	memcpy(image + 72U, store_guid, sizeof(store_guid));
	oracle_le32(image + 88U, 4024U);
	image[92U] = 0x5aU;
	image[93U] = 0xfeU;

	if (kind == Q35_MOR_FIXTURE_NO_REQUEST)
		value = 0x10U;
	else if (kind == Q35_MOR_FIXTURE_ABSENT)
		record = false;
	else if (kind == Q35_MOR_FIXTURE_MALFORMED)
		attributes = 3U;
	else if (kind == Q35_MOR_FIXTURE_RECOVERY)
		clean = false;
	else if (kind == Q35_MOR_FIXTURE_IDEMPOTENT_AFTER) {
		oracle_record(image, 28U, 0x3cU, 0x11U, 7U);
		oracle_record(image, 152U, 0x3fU, 0x10U, 7U);
		record = false;
	}
	if (record)
		oracle_record(image, 28U, 0x3fU, value, attributes);
	if (clean) {
		workspace = image + 4096U;
		memcpy(workspace, work_guid, sizeof(work_guid));
		oracle_le64(workspace + 24U, 4064U);
		oracle_le32(workspace + 16U, oracle_crc32(workspace, 32U));
		workspace[20U] = 0xfeU;
	}
	if (kind == Q35_MOR_FIXTURE_FAULT)
		image[50U] ^= 1U;
}

static void apply_write_plan(uint8_t *image,
	const struct payload_mm_authvar_write_plan *plan, const uint8_t *record)
{
	uint8_t *store = image + 72U;

	for (uint32_t index = 0; index < plan->step_count; index++) {
		const struct payload_mm_authvar_write_step *step = &plan->steps[index];

		assert(step->offset <= 4024U && step->size <= 4024U - step->offset);
		if (step->kind == PAYLOAD_MM_AUTHVAR_WRITE_RECORD) {
			for (uint32_t byte = 0; byte < step->size; byte++)
				assert(store[step->offset + byte] == 0xffU);
			memcpy(store + step->offset, record, step->size);
		} else {
			assert(step->kind == PAYLOAD_MM_AUTHVAR_WRITE_STATE &&
				step->size == 1U &&
				store[step->offset] == step->expected_state);
			store[step->offset] = step->new_state;
		}
	}
}

static enum payload_mm_authvar_write_action writer_clear(uint8_t *image,
	bool apply)
{
	static const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = 4024U,
		.maximum_name_size = 128U,
		.maximum_data_size = 128U,
		.maximum_records = 8U,
	};
	static const struct payload_mm_authvar_write_policy policy = {
		.maximum_name_size = 128U,
		.maximum_record_size = 256U,
		.maximum_data_size = 128U,
		.maximum_records = 8U,
	};
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	const uint8_t cleared = 0x10U;
	struct payload_mm_authvar_store_entry entries[8];
	struct payload_mm_authvar_reclaim_copy copies[8];
	struct payload_mm_authvar_store_index index = {
		.entries = entries,
		.entry_capacity = 8U,
	};
	struct payload_mm_authvar_record_source source = {
		.name = identity->name,
		.name_size = identity->name_size,
		.data = &cleared,
		.data_size = sizeof(cleared),
		.attributes = 7U,
	};
	struct payload_mm_authvar_reclaim_plan reclaim = {
		.copies = copies,
		.copy_capacity = 8U,
	};
	struct payload_mm_authvar_write_plan plan;
	uint8_t record[256];
	const struct payload_mm_authvar_store_entry *entry;

	replay_count++;
	memcpy(source.vendor_guid, identity->vendor_guid,
		sizeof(source.vendor_guid));
	assert(payload_mm_authvar_store_scan(&index, image + 72U, 4024U,
		&limits) == CB_SUCCESS);
	entry = payload_mm_authvar_store_find(&index, identity->vendor_guid,
		identity->name, identity->name_size);
	assert(entry);
	assert(payload_mm_authvar_write_plan_build(&index, entry, &source,
		&policy, false, record, sizeof(record), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	if (apply)
		apply_write_plan(image, &plan, record);
	return plan.action;
}

static uint64_t verify_fixture(enum q35_mor_fixture_kind kind, uint8_t *image)
{
	struct q35_mor_fixture_expectation expected;
	struct payload_mm_authvar_mor_entry entry = { 0xa5U, 0xa5U, 0xa5a5U };
	struct payload_mm_authvar_ftw_plan plan;
	uint8_t oracle[Q35_MOR_FIXTURE_SIZE];
	uint64_t hash = 0xcbf29ce484222325ULL;

	assert(q35_mor_fixture_generate(kind, image, &expected));
	if (kind != Q35_MOR_FIXTURE_ABSENT) {
		assert(image[160U] == 0x4dU);
		assert(image[161U] == 0x00U);
		assert(image[218U] == 0x00U);
		assert(image[219U] == 0x00U);
	}
	oracle_image(kind, oracle);
	assert(!memcmp(image, oracle, sizeof(oracle)));
	active_media = image;
	assert(payload_mm_authvar_ftw_plan(image, Q35_MOR_FIXTURE_SIZE,
		Q35_MOR_FIXTURE_BLOCK_SIZE, &plan) == expected.ftw_status);
	if (expected.ftw_status == CB_SUCCESS)
		assert(plan.action == expected.ftw_action);
	else
		assert(plan.action == PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED);
	assert(payload_mm_authvar_mor_probe_entry(&entry) ==
		expected.probe_status);
	assert(!memcmp(&entry, &expected.entry, sizeof(entry)));
	assert(expected.clear_allowed ==
		(expected.probe_status == CB_SUCCESS && !expected.resume_from_s3 &&
		 expected.entry.present && (expected.entry.value & 1U)));
	for (size_t index = 0; index < Q35_MOR_FIXTURE_SIZE; index++) {
		hash ^= image[index];
		hash *= 0x100000001b3ULL;
	}
	return hash;
}

int main(void)
{
	uint64_t first_hash[Q35_MOR_FIXTURE_COUNT];
	uint64_t second_hash[Q35_MOR_FIXTURE_COUNT];
	uint8_t transformed[Q35_MOR_FIXTURE_SIZE];
	uint8_t replay_snapshot[Q35_MOR_FIXTURE_SIZE];
	uint8_t rejected_media[Q35_MOR_FIXTURE_SIZE];
	struct q35_mor_fixture_expectation rejected_expectation;

	for (enum q35_mor_fixture_kind kind = 0;
	     kind < Q35_MOR_FIXTURE_COUNT; kind++)
		first_hash[kind] = verify_fixture(kind, first_images[kind]);
	for (enum q35_mor_fixture_kind kind = 0;
	     kind < Q35_MOR_FIXTURE_COUNT; kind++)
		second_hash[kind] = verify_fixture(kind, second_images[kind]);
	assert(!memcmp(first_images, second_images, sizeof(first_images)));
	assert(!memcmp(first_hash, second_hash, sizeof(first_hash)));

	memset(rejected_media, 0xa5, sizeof(rejected_media));
	memset(&rejected_expectation, 0xa5, sizeof(rejected_expectation));
	assert(!q35_mor_fixture_generate((enum q35_mor_fixture_kind)-1,
		rejected_media, &rejected_expectation));
	for (size_t index = 0; index < sizeof(rejected_media); index++)
		assert(rejected_media[index] == 0xa5U);
	for (size_t index = 0; index < sizeof(rejected_expectation); index++)
		assert(((const uint8_t *)&rejected_expectation)[index] == 0xa5U);

	memcpy(transformed, first_images[Q35_MOR_FIXTURE_IDEMPOTENT_BEFORE],
		sizeof(transformed));
	assert(writer_clear(transformed, true) ==
		PAYLOAD_MM_AUTHVAR_WRITE_TAIL_APPEND);
	assert(!memcmp(transformed,
		first_images[Q35_MOR_FIXTURE_IDEMPOTENT_AFTER], sizeof(transformed)));
	memcpy(replay_snapshot, transformed, sizeof(replay_snapshot));
	assert(writer_clear(transformed, true) == PAYLOAD_MM_AUTHVAR_WRITE_NOOP);
	assert(!memcmp(transformed, replay_snapshot, sizeof(transformed)));
	assert(replay_count == 2U);
	return 0;
}
