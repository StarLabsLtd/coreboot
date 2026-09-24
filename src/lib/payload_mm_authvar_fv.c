/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/payload_mm_authvar_fv.h>
#include <limits.h>
#include <string.h>

#define FV_SIGNATURE 0x4856465fU
#define FV_REVISION 2U
#define FV_SMMSTORE_ATTRIBUTES 0x00000e36U
#define VARIABLE_STORE_HEADER_SIZE 28U
#define VARIABLE_STORE_FORMATTED 0x5aU
#define VARIABLE_STORE_HEALTHY 0xfeU

static const uint8_t system_nv_data_fv_guid[16] = {
	0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
	0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
};
static const uint8_t authenticated_store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
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

static bool span_valid(const void *data, size_t size)
{
	return !size || (data && (uintptr_t)data <= UINTPTR_MAX - size);
}

bool payload_mm_authvar_fv_geometry(struct payload_mm_authvar_fv_geometry *geometry,
	size_t region_size, size_t block_size)
{
	size_t blocks;
	size_t spare_blocks;
	size_t variable_blocks;

	if (!span_valid(geometry, sizeof(*geometry)) ||
	    (uintptr_t)geometry % _Alignof(*geometry))
		return false;
	memset(geometry, 0, sizeof(*geometry));
	if (!block_size || block_size > UINT32_MAX ||
	    region_size > UINT32_MAX || region_size % block_size)
		return false;
	blocks = region_size / block_size;
	if (blocks < PAYLOAD_MM_AUTHVAR_FV_MIN_BLOCKS || blocks > UINT32_MAX)
		return false;
	/*
	 * The lower half is spare, one block is the working area, and every
	 * remaining block is the active variable FV.  Rounding the spare down
	 * gives odd-sized regions equal active and spare extents while preserving
	 * the established even-sized layout.
	 */
	spare_blocks = blocks / 2U;
	variable_blocks = blocks - spare_blocks - 1U;
	if (!variable_blocks || spare_blocks < variable_blocks)
		return false;
	*geometry = (struct payload_mm_authvar_fv_geometry) {
		.block_size = (uint32_t)block_size,
		.block_count = (uint32_t)blocks,
		.variable_size = (uint32_t)(variable_blocks * block_size),
		.working_offset = (uint32_t)(variable_blocks * block_size),
		.working_size = (uint32_t)block_size,
		.spare_offset = (uint32_t)((variable_blocks + 1U) * block_size),
		.spare_size = (uint32_t)(spare_blocks * block_size),
	};
	return true;
}

bool payload_mm_authvar_fv_format(void *region, size_t region_size,
	size_t block_size)
{
	struct payload_mm_authvar_fv_geometry geometry;
	uint8_t *fv = region;
	uint16_t checksum = 0;

	if (!span_valid(fv, region_size) ||
	    !payload_mm_authvar_fv_geometry(&geometry, region_size, block_size) ||
	    geometry.variable_size < PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE +
		VARIABLE_STORE_HEADER_SIZE)
		return false;

	memset(fv, 0xff, region_size);
	memset(fv, 0, 16U);
	memcpy(fv + 16U, system_nv_data_fv_guid, sizeof(system_nv_data_fv_guid));
	write_le64(fv + 32U, region_size);
	write_le32(fv + 40U, FV_SIGNATURE);
	write_le32(fv + 44U, FV_SMMSTORE_ATTRIBUTES);
	write_le16(fv + 48U, PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE);
	write_le16(fv + 50U, 0);
	write_le16(fv + 52U, 0);
	fv[54] = 0;
	fv[55] = FV_REVISION;
	write_le32(fv + 56U, geometry.block_count);
	write_le32(fv + 60U, geometry.block_size);
	memset(fv + 64U, 0, 8U);
	for (size_t i = 0; i < PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE; i += 2U)
		checksum = (uint16_t)(checksum + read_le16(fv + i));
	write_le16(fv + 50U, (uint16_t)(0U - checksum));

	memcpy(fv + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE, authenticated_store_guid,
		sizeof(authenticated_store_guid));
	write_le32(fv + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + 16U,
		geometry.variable_size - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE);
	fv[PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + 20U] = VARIABLE_STORE_FORMATTED;
	fv[PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + 21U] = VARIABLE_STORE_HEALTHY;
	memset(fv + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE + 22U, 0, 6U);
	return true;
}
