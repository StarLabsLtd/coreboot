/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_default_store.h>
#include <commonlib/helpers.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define assert(condition) do { if (!(condition)) __builtin_abort(); } while (0)

#define ORACLE_HEADER_SIZE 100U
#define ORACLE_RECORD_SIZE 260U
#define SMALL_BLOCK_SIZE 512U
#define SMALL_REGION_SIZE (3U * SMALL_BLOCK_SIZE)

static const uint8_t system_nv_data_fv_guid[16] = {
	0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
	0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
};
static const uint8_t authenticated_store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};

/* Fixed independently from the production record encoder. */
static const uint8_t default_records[ORACLE_RECORD_SIZE] = {
	0xaa, 0x55, 0x3f, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x16, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0c, 0xec, 0x76, 0xc0,
	0x28, 0x70, 0x99, 0x43, 0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f,
	0x43, 0x00, 0x75, 0x00, 0x73, 0x00, 0x74, 0x00, 0x6f, 0x00, 0x6d, 0x00,
	0x4d, 0x00, 0x6f, 0x00, 0x64, 0x00, 0x65, 0x00, 0x00, 0x00, 0xff, 0xff,
	0x00, 0xff, 0xff, 0xff, 0xaa, 0x55, 0x3f, 0x00, 0x27, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49, 0xb4, 0xd7, 0xb5, 0x34,
	0x21, 0x0f, 0x63, 0x7a, 0x63, 0x00, 0x65, 0x00, 0x72, 0x00, 0x74, 0x00,
	0x64, 0x00, 0x62, 0x00, 0x00, 0x00, 0xff, 0xff, 0x04, 0x00, 0x00, 0x00,
	0xaa, 0x55, 0x3f, 0x00, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x1a, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xe0, 0xe4, 0x73, 0x90,
	0xec, 0x60, 0x6e, 0x4b, 0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
	0x56, 0x00, 0x65, 0x00, 0x6e, 0x00, 0x64, 0x00, 0x6f, 0x00, 0x72, 0x00,
	0x4b, 0x00, 0x65, 0x00, 0x79, 0x00, 0x73, 0x00, 0x4e, 0x00, 0x76, 0x00,
	0x00, 0x00, 0xff, 0xff, 0x01, 0xff, 0xff, 0xff,
};

static void write_le16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *p, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static void write_le64(uint8_t *p, uint64_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static uint16_t read_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static void oracle_image(uint8_t *image, size_t region_size, size_t block_size)
{
	const size_t blocks = region_size / block_size;
	const size_t variable_blocks = blocks - blocks / 2U - 1U;
	uint16_t checksum = 0;

	assert(blocks >= 3U && region_size % block_size == 0U);
	memset(image, 0xff, region_size);
	memset(image, 0, 16U);
	memcpy(image + 16U, system_nv_data_fv_guid,
		sizeof(system_nv_data_fv_guid));
	write_le64(image + 32U, region_size);
	write_le32(image + 40U, 0x4856465fU);
	write_le32(image + 44U, 0x00000e36U);
	write_le16(image + 48U, 72U);
	write_le16(image + 50U, 0U);
	write_le16(image + 52U, 0U);
	image[54] = 0U;
	image[55] = 2U;
	write_le32(image + 56U, (uint32_t)blocks);
	write_le32(image + 60U, (uint32_t)block_size);
	memset(image + 64U, 0, 8U);
	for (size_t i = 0; i < 72U; i += 2U)
		checksum = (uint16_t)(checksum + read_le16(image + i));
	write_le16(image + 50U, (uint16_t)(0U - checksum));

	memcpy(image + 72U, authenticated_store_guid,
		sizeof(authenticated_store_guid));
	write_le32(image + 88U, (uint32_t)(variable_blocks * block_size - 72U));
	image[92] = 0x5aU;
	image[93] = 0xfeU;
	memset(image + 94U, 0, 6U);
	memcpy(image + ORACLE_HEADER_SIZE, default_records,
		sizeof(default_records));
}

static void expect_invalid_untouched(const void *source, void *candidate,
	size_t region_size, size_t block_size, size_t candidate_size)
{
	uint8_t *before = malloc(candidate_size);

	assert(before);
	memcpy(before, candidate, candidate_size);
	assert(payload_mm_authvar_default_store_compose(source, candidate,
		region_size, block_size) == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID);
	assert(!memcmp(candidate, before, candidate_size));
	free(before);
}

static void exact_images(void)
{
	static const struct {
		size_t block_size;
		size_t blocks;
	} cases[] = {
		{ 512U, 3U }, { 4096U, 3U }, { 4096U, 4U }, { 4096U, 5U },
		{ 65536U, 3U }, { 65536U, 5U },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		const size_t size = cases[i].block_size * cases[i].blocks;
		uint8_t *source = malloc(size);
		uint8_t *candidate = malloc(size);
		uint8_t *oracle = malloc(size);

		assert(source && candidate && oracle);
		memset(source, 0xff, size);
		memset(candidate, 0xa5, size);
		oracle_image(oracle, size, cases[i].block_size);
		assert(payload_mm_authvar_default_store_compose(source, candidate,
			size, cases[i].block_size) ==
			PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
		assert(!memcmp(candidate, oracle, size));
		assert(payload_mm_authvar_default_store_compose(oracle, candidate,
			size, cases[i].block_size) ==
			PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_COMPLETE);
		assert(!memcmp(candidate, oracle, size));
		memset(oracle + ORACLE_HEADER_SIZE, 0xff, ORACLE_RECORD_SIZE);
		assert(payload_mm_authvar_default_store_compose(oracle, candidate,
			size, cases[i].block_size) ==
			PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET);
		free(oracle);
		free(candidate);
		free(source);
	}
}

static void exhaustive_progress(void)
{
	uint8_t source[SMALL_REGION_SIZE];
	uint8_t candidate[SMALL_REGION_SIZE];
	uint8_t oracle[SMALL_REGION_SIZE];

	oracle_image(oracle, sizeof(oracle), SMALL_BLOCK_SIZE);
	for (size_t cut = 0; cut <= sizeof(source); cut++) {
		enum payload_mm_authvar_default_store_source expected;

		memset(source, 0xff, sizeof(source));
		memcpy(source, oracle, cut);
		expected = cut == 0U ? PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED :
			!memcmp(source, oracle, sizeof(source)) ?
			PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_COMPLETE :
			PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET;
		assert(payload_mm_authvar_default_store_compose(source, candidate,
			sizeof(source), SMALL_BLOCK_SIZE) == expected);
		assert(!memcmp(candidate, oracle, sizeof(candidate)));
	}
	for (size_t i = 0; i < sizeof(oracle); i++) {
		for (unsigned int bit = 0; bit < 8U; bit++) {
			const uint8_t mask = (uint8_t)(1U << bit);

			if (!(oracle[i] & mask)) {
				memset(source, 0xff, sizeof(source));
				source[i] &= (uint8_t)~mask;
				assert(payload_mm_authvar_default_store_compose(source,
					candidate, sizeof(source), SMALL_BLOCK_SIZE) ==
					PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET);
			} else {
				memcpy(source, oracle, sizeof(source));
				source[i] &= (uint8_t)~mask;
				assert(payload_mm_authvar_default_store_compose(source,
					candidate, sizeof(source), SMALL_BLOCK_SIZE) ==
					PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_FOREIGN);
			}
			assert(!memcmp(candidate, oracle, sizeof(candidate)));
		}
	}
}

static void hostile_admission(void)
{
	uint8_t source[SMALL_REGION_SIZE];
	uint8_t candidate[SMALL_REGION_SIZE];
	uint8_t adjacent[2U * SMALL_REGION_SIZE];
	uint8_t overlap[SMALL_REGION_SIZE + 1U];
	uint8_t exact_fit_source[1080U];
	uint8_t exact_fit_candidate[1080U];

	memset(source, 0xff, sizeof(source));
	memset(candidate, 0xa5, sizeof(candidate));
	expect_invalid_untouched(NULL, candidate, sizeof(source), SMALL_BLOCK_SIZE,
		sizeof(candidate));
	assert(payload_mm_authvar_default_store_compose(source, NULL, sizeof(source),
		SMALL_BLOCK_SIZE) == PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID);
	memset(source, 0xa5, sizeof(source));
	expect_invalid_untouched(source, source, sizeof(source), SMALL_BLOCK_SIZE,
		sizeof(source));
	memset(overlap, 0xa5, sizeof(overlap));
	expect_invalid_untouched(overlap, overlap + 1U, SMALL_REGION_SIZE,
		SMALL_BLOCK_SIZE, SMALL_REGION_SIZE);
	memset(candidate, 0xa5, sizeof(candidate));
	expect_invalid_untouched((const void *)(UINTPTR_MAX - 100U), candidate,
		SMALL_REGION_SIZE, SMALL_BLOCK_SIZE, sizeof(candidate));
	assert(payload_mm_authvar_default_store_compose(source,
		(void *)(UINTPTR_MAX - 100U), SMALL_REGION_SIZE, SMALL_BLOCK_SIZE) ==
		PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID);
	memset(candidate, 0xa5, sizeof(candidate));
	expect_invalid_untouched(source, candidate, sizeof(source) - 1U,
		SMALL_BLOCK_SIZE, sizeof(candidate));
	expect_invalid_untouched(source, candidate, sizeof(source), 0U,
		sizeof(candidate));
	memset(candidate, 0xa5, 384U);
	expect_invalid_untouched(source, candidate, 384U, 128U, 384U);

	memset(adjacent, 0xff, sizeof(adjacent));
	assert(payload_mm_authvar_default_store_compose(adjacent,
		adjacent + SMALL_REGION_SIZE, SMALL_REGION_SIZE, SMALL_BLOCK_SIZE) ==
		PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
	memset(exact_fit_source, 0xff, sizeof(exact_fit_source));
	assert(payload_mm_authvar_default_store_compose(exact_fit_source,
		exact_fit_candidate, sizeof(exact_fit_source), 360U) ==
		PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
}

int main(void)
{
	exact_images();
	exhaustive_progress();
	hostile_admission();
	return 0;
}
