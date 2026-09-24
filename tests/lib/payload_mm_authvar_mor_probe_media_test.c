/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_identity.h>
#include <boot/payload_mm_authvar_mor_probe.h>
#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_store.h>
#include <commonlib/region.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern long write(int fd, const void *buffer, unsigned long size);

static void assertion_failed(const char *expression, const char *file, int line)
{
	char message[256];
	const int length = snprintf(message, sizeof(message), "%s:%d: %s\n",
		file, line, expression);

	if (length > 0)
		(void)write(2, message, (unsigned long)length);
	abort();
}

#undef assert
#define assert(c) do { if (!(c)) assertion_failed(#c, __FILE__, __LINE__); } while (0)
#define BLOCK_SIZE 4096U
#define REGION_SIZE (4U * BLOCK_SIZE)
#define FV_HEADER_SIZE 72U
#define STORE_SIZE (BLOCK_SIZE - FV_HEADER_SIZE)

static uint8_t media[REGION_SIZE];
static size_t exposed_size = REGION_SIZE;
static bool map_fails;
static int unmap_status;

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

static void put16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		bytes[i] = (uint8_t)(value >> (8U * i));
}

static void put64(uint8_t *bytes, uint64_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		bytes[i] = (uint8_t)(value >> (8U * i));
}

static uint32_t crc32(const uint8_t *bytes, size_t size)
{
	uint32_t crc = UINT32_MAX;

	for (size_t i = 0; i < size; i++) {
		crc ^= bytes[i];
		for (unsigned int bit = 0; bit < 8U; bit++)
			crc = (crc >> 1) ^
				(0xedb88320U & (uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

static void make_clean_media(void)
{
	uint8_t *workspace = media + BLOCK_SIZE;
	uint16_t checksum = 0;

	memset(media, 0xff, sizeof(media));
	memset(media, 0, FV_HEADER_SIZE);
	memcpy(media + 16U, fv_guid, sizeof(fv_guid));
	put64(media + 32U, REGION_SIZE);
	put32(media + 40U, 0x4856465fU);
	put32(media + 44U, 0x00000e36U);
	put16(media + 48U, FV_HEADER_SIZE);
	media[55U] = 2U;
	put32(media + 56U, 4U);
	put32(media + 60U, BLOCK_SIZE);
	for (size_t i = 0; i < FV_HEADER_SIZE; i += 2U)
		checksum = (uint16_t)(checksum + (uint16_t)media[i] +
			((uint16_t)media[i + 1U] << 8));
	put16(media + 50U, (uint16_t)-checksum);

	memset(media + FV_HEADER_SIZE, 0, PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE);
	memcpy(media + FV_HEADER_SIZE, store_guid, sizeof(store_guid));
	put32(media + FV_HEADER_SIZE + 16U, STORE_SIZE);
	media[FV_HEADER_SIZE + 20U] = 0x5aU;
	media[FV_HEADER_SIZE + 21U] = 0xfeU;

	memset(workspace, 0xff, BLOCK_SIZE);
	memcpy(workspace, work_guid, sizeof(work_guid));
	put64(workspace + 24U, BLOCK_SIZE - 32U);
	put32(workspace + 16U, crc32(workspace, 32U));
	workspace[20U] = 0xfeU;
	exposed_size = sizeof(media);
	map_fails = false;
	unmap_status = 0;
}

static size_t add_record(size_t offset, const uint8_t guid[16],
	const uint16_t *name, size_t name_size, uint32_t attributes,
	const uint8_t *data, size_t data_size)
{
	uint8_t *store = media + FV_HEADER_SIZE;
	const size_t name_end = offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
		name_size;
	const size_t data_offset = (name_end + 3U) & ~(size_t)3U;
	const size_t end = data_offset + data_size;
	const size_t next = (end + 3U) & ~(size_t)3U;

	assert(next <= STORE_SIZE);
	memset(store + offset, 0, next - offset);
	put16(store + offset, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	store[offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	put32(store + offset + 4U, attributes);
	put32(store + offset + 36U, (uint32_t)name_size);
	put32(store + offset + 40U, (uint32_t)data_size);
	memcpy(store + offset + 44U, guid, 16U);
	memcpy(store + offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, name,
		name_size);
	memset(store + name_end, 0xff, data_offset - name_end);
	memcpy(store + data_offset, data, data_size);
	memset(store + end, 0xff, next - end);
	return next;
}

static void *test_mmap(const struct region_device *rdev, size_t offset, size_t size)
{
	(void)rdev;
	if (map_fails || offset || size != exposed_size)
		return NULL;
	return media;
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
	*rstore = (struct region_device)REGION_DEV_INIT(&test_ops, 0, exposed_size);
	return 0;
}

static void expect_error(void)
{
	static unsigned int error_case;
	struct payload_mm_authvar_mor_entry result = { 0xa5U, 0xa5U, 0xa5a5U };
	const struct payload_mm_authvar_mor_entry zero = { 0 };

	error_case++;
	if (payload_mm_authvar_mor_probe_entry(&result) != CB_ERR) {
		char message[64];
		const int length = snprintf(message, sizeof(message),
			"unexpected success in error case %u\n", error_case);
		if (length > 0)
			(void)write(2, message, (unsigned long)length);
		abort();
	}
	assert(!memcmp(&result, &zero, sizeof(result)));
}

static void absent_and_values(void)
{
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	static const uint8_t values[] = { 0U, 1U, 16U, 17U, 255U };
	struct payload_mm_authvar_mor_entry result;
	struct payload_mm_authvar_ftw_plan plan;

	make_clean_media();
	assert(payload_mm_authvar_ftw_plan(media, sizeof(media), BLOCK_SIZE, &plan) ==
		CB_SUCCESS);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	assert(payload_mm_authvar_mor_probe_entry(&result) == CB_SUCCESS);
	assert(!result.present && !result.value && !result.reserved);
	for (size_t i = 0; i < sizeof(values); i++) {
		make_clean_media();
		add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, identity->vendor_guid,
			identity->name, identity->name_size,
			PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES, &values[i], 1U);
		assert(payload_mm_authvar_mor_probe_entry(&result) == CB_SUCCESS);
		assert(result.present == 1U && result.value == values[i] &&
			!result.reserved);
	}
}

static void identity_shape_and_store_failures(void)
{
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	static const uint16_t other_name[] = { 'O', 't', 'h', 'e', 'r', 0U };
	uint16_t near_name[PAYLOAD_MM_AUTHVAR_MOR_CONTROL_NAME_SIZE / 2U];
	uint8_t near_guid[16];
	const uint8_t value = 1U;
	struct payload_mm_authvar_mor_entry result;
	size_t offset;

	memcpy(near_guid, identity->vendor_guid, sizeof(near_guid));
	near_guid[15] ^= 1U;
	memcpy(near_name, identity->name, sizeof(near_name));
	near_name[0] ^= 1U;
	make_clean_media();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, near_guid,
		identity->name, identity->name_size, PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES,
		&value, 1U);
	add_record(offset, identity->vendor_guid, near_name, sizeof(near_name),
		PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES, &value, 1U);
	assert(payload_mm_authvar_mor_probe_entry(&result) == CB_SUCCESS &&
		!result.present);

	make_clean_media();
	add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, identity->vendor_guid,
		identity->name, identity->name_size,
		PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES &
			~PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS, &value, 1U);
	expect_error();
	make_clean_media();
	add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, identity->vendor_guid,
		identity->name, identity->name_size, PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES,
		(const uint8_t[]){ 1U, 2U }, 2U);
	expect_error();

	/* Ambiguous history for an unrelated key invalidates the whole store. */
	make_clean_media();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, near_guid,
		other_name, sizeof(other_name), 7U, &value, 1U);
	add_record(offset, near_guid, other_name, sizeof(other_name), 7U, &value, 1U);
	expect_error();

	make_clean_media();
	exposed_size--;
	expect_error();
	make_clean_media();
	memset(media + BLOCK_SIZE, 0xff, BLOCK_SIZE);
	expect_error();
	make_clean_media();
	media[2U * BLOCK_SIZE] = work_guid[0];
	expect_error();
	make_clean_media();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, near_guid,
		other_name, sizeof(other_name), 7U, &value, 1U);
	(void)offset;
	put32(media + FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 36U,
		UINT32_MAX);
	expect_error();
	make_clean_media();
	map_fails = true;
	expect_error();
	make_clean_media();
	unmap_status = -1;
	expect_error();
}

int main(void)
{
	absent_and_values();
	identity_shape_and_store_failures();
	return 0;
}
