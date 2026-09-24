/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <commonlib/helpers.h>
#include <commonlib/payload_mm_authvar_fv.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../util/smmstoretool/fv.h"

struct format_case {
	uint32_t region_size;
	uint32_t block_size;
	uint32_t block_count;
	uint32_t variable_size;
	uint32_t working_offset;
	uint32_t spare_offset;
	uint32_t spare_size;
	uint32_t store_size;
	uint16_t checksum;
};

/* These expected values are fixed independently of the formatter geometry. */
static const struct format_case cases[] = {
	{ 12288U, 4096U, 3U, 4096U, 4096U, 8192U, 4096U, 4024U, 0xba05U },
	{ 16384U, 4096U, 4U, 4096U, 4096U, 8192U, 8192U, 4024U, 0xaa04U },
	{ 20480U, 4096U, 5U, 8192U, 8192U, 12288U, 8192U, 8120U, 0x9a03U },
	{ 32768U, 4096U, 8U, 12288U, 12288U, 16384U, 16384U, 12216U, 0x6a00U },
	{ 196608U, 65536U, 3U, 65536U, 65536U, 131072U, 65536U, 65464U,
		0xfa01U },
	{ 262144U, 65536U, 4U, 65536U, 65536U, 131072U, 131072U, 65464U,
		0xf9ffU },
	{ 327680U, 65536U, 5U, 131072U, 131072U, 196608U, 131072U, 131000U,
		0xf9fdU },
	{ 524288U, 65536U, 8U, 196608U, 196608U, 262144U, 262144U, 196536U,
		0xf9f7U },
};

static const uint8_t system_nv_data_fv_guid[16] = {
	0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
	0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
};
static const uint8_t authenticated_store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};

static uint16_t read_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
		(uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t read_le64(const uint8_t *p)
{
	uint64_t value = 0;

	for (size_t i = 0; i < sizeof(value); i++)
		value |= (uint64_t)p[i] << (8U * i);
	return value;
}

static void expect_bytes(const uint8_t *data, size_t size, uint8_t value)
{
	for (size_t i = 0; i < size; i++)
		assert(data[i] == value);
}

static void check_geometry(const struct format_case *expected)
{
	struct payload_mm_authvar_fv_geometry geometry;

	memset(&geometry, 0xa5, sizeof(geometry));
	assert(payload_mm_authvar_fv_geometry(&geometry, expected->region_size,
		expected->block_size));
	assert(geometry.block_size == expected->block_size);
	assert(geometry.block_count == expected->block_count);
	assert(geometry.variable_offset == 0U);
	assert(geometry.variable_size == expected->variable_size);
	assert(geometry.working_offset == expected->working_offset);
	assert(geometry.working_size == expected->block_size);
	assert(geometry.spare_offset == expected->spare_offset);
	assert(geometry.spare_size == expected->spare_size);
}

static void check_format(const struct format_case *expected, uint8_t *image)
{
	uint16_t checksum = 0;

	expect_bytes(image, 16U, 0);
	assert(!memcmp(image + 16U, system_nv_data_fv_guid,
		sizeof(system_nv_data_fv_guid)));
	assert(read_le64(image + 32U) == expected->region_size);
	assert(read_le32(image + 40U) == 0x4856465fU);
	assert(read_le32(image + 44U) == 0x00000e36U);
	assert(read_le16(image + 48U) == PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE);
	assert(read_le16(image + 50U) == expected->checksum);
	assert(read_le16(image + 52U) == 0U);
	assert(image[54] == 0U);
	assert(image[55] == 2U);
	assert(read_le32(image + 56U) == expected->block_count);
	assert(read_le32(image + 60U) == expected->block_size);
	assert(read_le32(image + 64U) == 0U);
	assert(read_le32(image + 68U) == 0U);
	for (size_t i = 0; i < PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE; i += 2U)
		checksum = (uint16_t)(checksum + read_le16(image + i));
	assert(checksum == 0U);
	assert(!memcmp(image + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE,
		authenticated_store_guid, sizeof(authenticated_store_guid)));
	assert(read_le32(image + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + 16U) ==
		expected->store_size);
	assert(image[PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + 20U] == 0x5aU);
	assert(image[PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + 21U] == 0xfeU);
	expect_bytes(image + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + 22U, 6U, 0);
	expect_bytes(image + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE +
		28U,
		expected->region_size - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE -
		28U, 0xffU);
}

static void format_table(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		const struct format_case *expected = &cases[i];
		uint8_t *direct = malloc(expected->region_size);

		assert(direct);
		memset(direct, 0xa5, expected->region_size);
		check_geometry(expected);
		assert(payload_mm_authvar_fv_format(direct, expected->region_size,
			expected->block_size));
		check_format(expected, direct);
		if (expected->block_size == 65536U) {
			uint8_t *tool = malloc(expected->region_size);
			struct mem_range_t range = {
				.start = tool,
				.length = expected->region_size,
			};

			assert(tool);
			memset(tool, 0x5a, expected->region_size);
			assert(fv_init(range));
			assert(!memcmp(tool, direct, expected->region_size));
			free(tool);
		}
		free(direct);
	}
}

static void hostile_inputs(void)
{
	struct payload_mm_authvar_fv_geometry geometry;
	const struct payload_mm_authvar_fv_geometry zero = { 0 };
	uint8_t image[4096];
	uint8_t unchanged[sizeof(image)];
	uint8_t geometry_storage[sizeof(geometry) + _Alignof(geometry)];
	uint8_t geometry_unchanged[sizeof(geometry_storage)];
	uintptr_t aligned;

	memset(image, 0xa5, sizeof(image));
	memcpy(unchanged, image, sizeof(image));
	assert(!payload_mm_authvar_fv_geometry(NULL, 12288U, 4096U));
	assert(!payload_mm_authvar_fv_geometry(&geometry, 8192U, 4096U));
	assert(!memcmp(&geometry, &zero, sizeof(geometry)));
	assert(!payload_mm_authvar_fv_geometry(&geometry, 12287U, 4096U));
	assert(!payload_mm_authvar_fv_geometry(&geometry, 12288U, 0));
	assert(!payload_mm_authvar_fv_format(NULL, 12288U, 4096U));
	assert(!payload_mm_authvar_fv_format(image, sizeof(image), 1365U));
	assert(!memcmp(image, unchanged, sizeof(image)));
	memset(geometry_storage, 0x5a, sizeof(geometry_storage));
	memcpy(geometry_unchanged, geometry_storage, sizeof(geometry_storage));
	aligned = ((uintptr_t)geometry_storage + _Alignof(geometry) - 1U) &
		~(uintptr_t)(_Alignof(geometry) - 1U);
	assert(!payload_mm_authvar_fv_geometry(
		(struct payload_mm_authvar_fv_geometry *)(aligned + 1U),
		12288U, 4096U));
	assert(!memcmp(geometry_storage, geometry_unchanged,
		sizeof(geometry_storage)));
#if SIZE_MAX > UINT32_MAX
	assert(!payload_mm_authvar_fv_geometry(&geometry,
		(size_t)UINT32_MAX + 1U, 4096U));
	assert(!memcmp(&geometry, &zero, sizeof(geometry)));
#endif
}

static void geometry_wrap(void)
{
	struct payload_mm_authvar_fv_geometry geometry;

	assert(!payload_mm_authvar_fv_geometry(
		(struct payload_mm_authvar_fv_geometry *)(uintptr_t)
			(UINTPTR_MAX - sizeof(geometry) + 1U), 12288U, 4096U));
}

static void region_wrap(void)
{
	assert(!payload_mm_authvar_fv_format(
		(void *)(uintptr_t)(UINTPTR_MAX - 4095U), 12288U, 4096U));
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "geometry-wrap")) {
		geometry_wrap();
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "region-wrap")) {
		region_wrap();
		return 0;
	}
	assert(argc == 1);
	format_table();
	hostile_inputs();
	geometry_wrap();
	region_wrap();
	return 0;
}
