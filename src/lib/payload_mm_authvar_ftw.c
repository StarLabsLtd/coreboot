/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <string.h>

#define FV_HEADER_MIN_SIZE 64U
#define FV_BLOCK_MAP_OFFSET 56U
#define FV_BLOCK_MAP_ENTRY_SIZE 8U
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
const uint8_t payload_mm_authvar_ftw_working_block_guid[16] = {
	0x2b, 0x29, 0x58, 0x9e, 0x68, 0x7c, 0x7d, 0x49,
	0xa0, 0xce, 0x65, 0x00, 0xfd, 0x9f, 0x1b, 0x95,
};
const uint8_t payload_mm_authvar_ftw_coreboot_caller_guid[16] = {
	0xf3, 0x2e, 0xa5, 0xc4, 0x27, 0x4e, 0xe2, 0x47,
	0x95, 0x0b, 0xfd, 0xaa, 0xb5, 0x21, 0xb8, 0x95,
};
static const uint8_t ftw_caller_guids[][16] = {
	{
		0x76, 0xea, 0x5c, 0xfe, 0x72, 0x4f, 0xe8, 0x49,
		0x98, 0x6f, 0x2c, 0xd8, 0x99, 0xdf, 0xfe, 0x5d,
	},
	{
		0x48, 0xb2, 0x0c, 0x47, 0xac, 0xe8, 0x3c, 0x47,
		0xbb, 0x4f, 0x81, 0x06, 0x9a, 0x1f, 0xe6, 0xfd,
	},
	{
		0xec, 0xe4, 0xad, 0x3a, 0xcc, 0x63, 0x48, 0x4a,
		0xa9, 0x28, 0x5a, 0x37, 0x4d, 0xd4, 0x63, 0xeb,
	},
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

static bool bytes_are(const uint8_t *p, size_t size, uint8_t value)
{
	for (size_t i = 0; i < size; i++) {
		if (p[i] != value)
			return false;
	}
	return true;
}

static bool caller_guid_valid(const uint8_t *guid)
{
	if (!memcmp(guid, payload_mm_authvar_ftw_coreboot_caller_guid,
		sizeof(payload_mm_authvar_ftw_coreboot_caller_guid)))
		return true;
	for (size_t i = 0; i < sizeof(ftw_caller_guids) /
	     sizeof(ftw_caller_guids[0]); i++) {
		if (!memcmp(guid, ftw_caller_guids[i], sizeof(ftw_caller_guids[i])))
			return true;
	}
	return false;
}

static uint32_t crc32(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_MAX;

	for (size_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320U &
				(uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

enum cb_err payload_mm_authvar_ftw_geometry(
	struct payload_mm_authvar_ftw_geometry *geometry, size_t region_size,
	size_t block_size)
{
	size_t blocks;
	size_t spare_blocks;
	size_t variable_blocks;

	if (geometry)
		memset(geometry, 0, sizeof(*geometry));
	if (!geometry || !block_size || block_size > UINT32_MAX ||
	    region_size > UINT32_MAX || region_size % block_size)
		return CB_ERR;
	blocks = region_size / block_size;
	if (blocks < PAYLOAD_MM_AUTHVAR_FTW_MIN_BLOCKS || blocks > UINT32_MAX)
		return CB_ERR;
	spare_blocks = blocks / 2U;
	variable_blocks = blocks - spare_blocks - 1U;
	if (!variable_blocks || spare_blocks < variable_blocks)
		return CB_ERR;
	*geometry = (struct payload_mm_authvar_ftw_geometry) {
		.block_size = (uint32_t)block_size,
		.block_count = (uint32_t)blocks,
		.variable_size = (uint32_t)(variable_blocks * block_size),
		.working_offset = (uint32_t)(variable_blocks * block_size),
		.working_size = (uint32_t)block_size,
		.spare_offset = (uint32_t)((variable_blocks + 1U) * block_size),
		.spare_size = (uint32_t)(spare_blocks * block_size),
	};
	return CB_SUCCESS;
}

static bool fv_valid(const uint8_t *fv, size_t available, size_t complete_size,
	const struct payload_mm_authvar_ftw_geometry *geometry,
	uint32_t *header_size, uint32_t *store_size)
{
	uint16_t checksum = 0;
	uint16_t length;
	size_t offset;
	uint64_t described_blocks = 0;

	if (available < FV_HEADER_MIN_SIZE || memcmp(fv + 16, system_nv_data_fv_guid, 16) ||
	    read_le64(fv + 32) != complete_size || read_le32(fv + 40) != FV_SIGNATURE ||
	    read_le32(fv + 44) != FV_SMMSTORE_ATTRIBUTES ||
	    fv[55] != FV_REVISION || read_le16(fv + 52) || fv[54])
		return false;
	length = read_le16(fv + 48);
	if (length < FV_HEADER_MIN_SIZE + FV_BLOCK_MAP_ENTRY_SIZE ||
	    (length & 1U) || length > available || length > geometry->variable_size)
		return false;
	for (size_t i = 0; i < length; i += 2U)
		checksum = (uint16_t)(checksum + read_le16(fv + i));
	if (checksum)
		return false;
	for (offset = FV_BLOCK_MAP_OFFSET; offset + FV_BLOCK_MAP_ENTRY_SIZE <= length;
	     offset += FV_BLOCK_MAP_ENTRY_SIZE) {
		const uint32_t count = read_le32(fv + offset);
		const uint32_t size = read_le32(fv + offset + 4U);

		if (!count && !size)
			break;
		if (!count || size != geometry->block_size ||
		    count > (UINT64_MAX - described_blocks) / size)
			return false;
		described_blocks += (uint64_t)count * size;
	}
	if (offset + FV_BLOCK_MAP_ENTRY_SIZE > length || described_blocks != complete_size ||
	    length > available - VARIABLE_STORE_HEADER_SIZE)
		return false;
	if (memcmp(fv + length, authenticated_store_guid, 16) ||
	    fv[length + 20U] != VARIABLE_STORE_FORMATTED ||
	    fv[length + 21U] != VARIABLE_STORE_HEALTHY ||
	    read_le16(fv + length + 22U) || read_le32(fv + length + 24U))
		return false;
	*store_size = read_le32(fv + length + 16U);
	if (*store_size != geometry->variable_size - length ||
	    *store_size < VARIABLE_STORE_HEADER_SIZE)
		return false;
	*header_size = length;
	return true;
}

static bool workspace_header_valid(const uint8_t *workspace, size_t size,
	uint8_t expected_state)
{
	uint8_t canonical[PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE];

	if (size < PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE ||
	    memcmp(workspace, payload_mm_authvar_ftw_working_block_guid,
		sizeof(payload_mm_authvar_ftw_working_block_guid)) ||
	    workspace[PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET] != expected_state ||
	    !bytes_are(workspace + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET + 1U,
		3, PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED) ||
	    read_le64(workspace + 24) != size - PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE)
		return false;
	memcpy(canonical, workspace, sizeof(canonical));
	memset(canonical + 16, 0xff, 8);
	return read_le32(workspace + 16) == crc32(canonical, sizeof(canonical));
}

static bool workspace_uncommitted(const uint8_t *workspace, size_t size)
{
	uint8_t canonical[PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE];
	uint64_t queue_size;
	uint32_t checksum;
	bool has_data = false;

	if (size < PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE)
		return false;
	queue_size = size - PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE;
	memset(canonical, 0xff, sizeof(canonical));
	memcpy(canonical, payload_mm_authvar_ftw_working_block_guid,
		sizeof(payload_mm_authvar_ftw_working_block_guid));
	for (size_t i = 0; i < sizeof(queue_size); i++)
		canonical[24U + i] = (uint8_t)(queue_size >> (8U * i));
	checksum = crc32(canonical, sizeof(canonical));
	for (size_t i = 0; i < sizeof(checksum); i++)
		canonical[16U + i] = (uint8_t)(checksum >> (8U * i));
	for (size_t i = 0; i < size; i++) {
		const uint8_t expected = i < sizeof(canonical) ? canonical[i] : 0xffU;

		if (workspace[i] == 0xffU)
			continue;
		if ((workspace[i] & expected) != expected)
			return false;
		has_data = true;
	}
	return has_data;
}

static bool workspace_erased_subset(const uint8_t *authoritative,
	const uint8_t *candidate, size_t size)
{
	bool has_data = false;

	for (size_t i = 0; i < size; i++) {
		if (candidate[i] == 0xffU)
			continue;
		if ((candidate[i] & authoritative[i]) != authoritative[i])
			return false;
		has_data = true;
	}
	return has_data;
}

static bool record_bounds_valid(const uint8_t *record,
	const struct payload_mm_authvar_ftw_geometry *geometry,
	uint32_t fv_header_size, uint32_t store_size)
{
	const uint8_t state = record[0];
	const int64_t relative = (int64_t)read_le64(record + 32);

	if ((state & 0xf8U) != 0xf8U || !bytes_are(record + 1, 7, 0xff) ||
	    !(state & 1U) || read_le64(record + 8) ||
	    read_le64(record + 16) != fv_header_size ||
	    read_le64(record + 24) != store_size ||
	    relative != -(int64_t)geometry->spare_offset)
		return false;
	return true;
}

static enum payload_mm_authvar_ftw_action classify_queue(const uint8_t *workspace,
	bool active_valid, bool spare_valid, bool restoring_workspace,
	const struct payload_mm_authvar_ftw_geometry *geometry,
	uint32_t fv_header_size, uint32_t store_size, uint32_t *queue_offset,
	uint32_t *queue_entry_size,
	enum payload_mm_authvar_ftw_queue_disposition *queue_disposition)
{
	size_t offset = PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE;
	const size_t size = geometry->working_size;
	bool completed_history = false;

	while (offset < size) {
		const uint8_t *header = workspace + offset;
		uint64_t writes;
		uint64_t private_size;
		size_t entry_size;
		const uint8_t *record;
		uint8_t header_state;
		uint8_t record_state;

		if (bytes_are(header, size - offset, 0xff)) {
			if (!active_valid)
				return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
			*queue_offset = (uint32_t)offset;
			if (size - offset < PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
			    PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE) {
				return PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE;
			}
			if (!completed_history)
				*queue_disposition = PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY;
			return PAYLOAD_MM_AUTHVAR_FTW_CLEAN;
		}
		if (header[0] == PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED) {
			*queue_offset = (uint32_t)offset;
			return active_valid ? PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE :
				PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		}
		if (size - offset < PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE)
			return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		header_state = header[0];
		writes = read_le64(header + 24);
		private_size = read_le64(header + 32);
		if ((header_state != PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED &&
		     header_state != PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED &&
		     header_state != PAYLOAD_MM_AUTHVAR_FTW_HEADER_ABORTED &&
		     header_state != PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE) ||
		    !bytes_are(header + 1, 3, 0xff) ||
		    !caller_guid_valid(header + 4) ||
		    !bytes_are(header + 20, 4, 0xff) ||
		    !writes || writes > (size - offset) / PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE ||
		    private_size > size ||
		    writes > (SIZE_MAX - PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE) /
			(PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE + private_size))
			return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		entry_size = PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
			(size_t)writes * (PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE + (size_t)private_size);
		if (entry_size > size - offset)
			return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		for (uint64_t i = 0; i < writes; i++) {
			record = header + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
				(size_t)i * (PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE + (size_t)private_size);
			if ((record[0] != PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED &&
			     record[0] != PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE &&
			     record[0] !=
				PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE) ||
			    !bytes_are(record + 1, 7, 0xff))
				return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		}
		if (header_state == PAYLOAD_MM_AUTHVAR_FTW_HEADER_ABORTED ||
		    header_state == PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE) {
			if (private_size || writes != 1U)
				return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
			record = header + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE;
			if (header_state == PAYLOAD_MM_AUTHVAR_FTW_HEADER_ABORTED) {
				if (!bytes_are(record, PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE, 0xff))
					return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
			} else if (record[0] == PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED) {
				if (!bytes_are(record + 1, 7, 0xff))
					return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
			} else if (record[0] !=
				   PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE ||
				   !record_bounds_valid(record, geometry, fv_header_size,
					store_size)) {
				return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
			}
			offset += entry_size;
			completed_history = true;
			continue;
		}
		if (writes != 1U || private_size)
			return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		record = header + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE;
		if (header_state == PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED) {
			if (!bytes_are(record, PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE, 0xff))
				return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
			*queue_offset = (uint32_t)offset;
			*queue_entry_size = (uint32_t)entry_size;
			*queue_disposition = PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD;
			return active_valid ? PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD :
				PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		}
		if (record[0] == PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED) {
			*queue_offset = (uint32_t)offset;
			*queue_entry_size = (uint32_t)entry_size;
			*queue_disposition = PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD;
			return active_valid ? PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD :
				PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		}
		if (!record_bounds_valid(record, geometry, fv_header_size, store_size))
			return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		*queue_offset = (uint32_t)offset;
		*queue_entry_size = (uint32_t)entry_size;
		record_state = record[0];
		if (restoring_workspace) {
			*queue_disposition = PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD;
			return active_valid ? PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD :
				PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		}
		if (record_state == PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE)
			return spare_valid ? PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE :
				PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		if (record_state ==
		    PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE)
			return active_valid ? PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW :
				PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
	}
	if (active_valid) {
		*queue_offset = (uint32_t)offset;
		return PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE;
	}
	return PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
}

enum cb_err payload_mm_authvar_ftw_plan(const void *region, size_t region_size,
	size_t block_size, struct payload_mm_authvar_ftw_plan *plan)
{
	struct payload_mm_authvar_ftw_plan candidate = { 0 };
	const uint8_t *bytes = region;
	const uint8_t *working;
	const uint8_t *spare;
	uint32_t active_header_size = 0;
	uint32_t active_store_size = 0;
	uint32_t spare_header_size = 0;
	uint32_t spare_store_size = 0;
	uint32_t header_size;
	uint32_t store_size;
	bool active_valid;
	bool spare_valid;
	bool working_valid;
	bool staged_valid;

	if (plan)
		memset(plan, 0, sizeof(*plan));
	if (!region || !plan || payload_mm_authvar_ftw_geometry(&candidate.geometry,
		region_size, block_size) != CB_SUCCESS)
		return CB_ERR;
	working = bytes + candidate.geometry.working_offset;
	spare = bytes + candidate.geometry.spare_offset;
	active_valid = fv_valid(bytes, candidate.geometry.variable_size, region_size,
		&candidate.geometry, &active_header_size, &active_store_size);
	spare_valid = fv_valid(spare, candidate.geometry.variable_size, region_size,
		&candidate.geometry, &spare_header_size, &spare_store_size);
	if (active_valid) {
		header_size = active_header_size;
		store_size = active_store_size;
	} else if (spare_valid) {
		header_size = spare_header_size;
		store_size = spare_store_size;
	} else {
		return CB_ERR;
	}
	if (active_valid && spare_valid &&
	    (active_header_size != spare_header_size ||
	     active_store_size != spare_store_size))
		return CB_ERR;
	candidate.fv_header_size = header_size;
	candidate.variable_store_size = store_size;
	working_valid = workspace_header_valid(working,
		candidate.geometry.working_size, PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);
	staged_valid = active_valid && workspace_header_valid(spare,
		candidate.geometry.working_size, PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);
	if (working_valid && staged_valid) {
		uint32_t old_queue_offset = 0;
		uint32_t old_queue_entry_size = 0;
		enum payload_mm_authvar_ftw_queue_disposition old_disposition =
			PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE;
		const enum payload_mm_authvar_ftw_action old_action = classify_queue(
			working, active_valid, false, true, &candidate.geometry,
			header_size, store_size, &old_queue_offset,
			&old_queue_entry_size, &old_disposition);
		const enum payload_mm_authvar_ftw_action staged_action = classify_queue(
			spare, true, false, true, &candidate.geometry, header_size, store_size,
			&candidate.queue_offset, &candidate.queue_entry_size,
			&candidate.queue_disposition);

		if (old_action == PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED ||
		    !((staged_action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN &&
		       candidate.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY) ||
		      (staged_action == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD &&
		       candidate.queue_disposition ==
			PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD))) {
			candidate.action = PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		} else if (!memcmp(working, spare, candidate.geometry.working_size) &&
			   candidate.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY) {
			candidate.action = PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED;
			candidate.workspace = PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE;
			candidate.queue_disposition = PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE;
		} else {
			candidate.action = PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE;
			candidate.workspace = PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE;
		}
	} else if (working_valid) {
		bool spare_tail_erased;
		bool spare_fv_erased_subset;

		candidate.action = classify_queue(working, active_valid, spare_valid, false,
			&candidate.geometry, header_size, store_size,
			&candidate.queue_offset, &candidate.queue_entry_size,
			&candidate.queue_disposition);
		candidate.workspace = PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING;
		spare_tail_erased = bytes_are(spare + candidate.geometry.variable_size,
			candidate.geometry.spare_size - candidate.geometry.variable_size,
			0xffU);
		spare_fv_erased_subset = memcmp(bytes, spare,
			candidate.geometry.variable_size) &&
			workspace_erased_subset(bytes, spare,
				candidate.geometry.variable_size) && spare_tail_erased;
		if ((candidate.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
		     candidate.action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE) &&
		    !bytes_are(spare, candidate.geometry.spare_size, 0xffU)) {
			if ((spare_valid && spare_tail_erased &&
			     !memcmp(bytes, spare, candidate.geometry.variable_size)) ||
			    spare_fv_erased_subset) {
				candidate.action = PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE;
				candidate.workspace = PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE;
				candidate.queue_offset = 0;
				candidate.queue_entry_size = 0;
				candidate.queue_disposition =
					PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE;
			} else if ((workspace_uncommitted(spare,
				    candidate.geometry.working_size) &&
			     bytes_are(spare + candidate.geometry.working_size,
				candidate.geometry.spare_size -
					candidate.geometry.working_size, 0xffU)) ||
			    (workspace_erased_subset(working, spare,
				candidate.geometry.working_size) &&
			     bytes_are(spare + candidate.geometry.working_size,
				candidate.geometry.spare_size -
					candidate.geometry.working_size, 0xffU))) {
				candidate.action = PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED;
				candidate.workspace = PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE;
				candidate.queue_disposition = PAYLOAD_MM_AUTHVAR_FTW_QUEUE_NONE;
			} else {
				candidate.action = PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
			}
		}
	} else if (staged_valid) {
		const enum payload_mm_authvar_ftw_action staged_action = classify_queue(
			spare, true, false, true, &candidate.geometry, header_size, store_size,
			&candidate.queue_offset, &candidate.queue_entry_size,
			&candidate.queue_disposition);

		if ((staged_action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN &&
		     candidate.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_EMPTY) ||
		    (staged_action == PAYLOAD_MM_AUTHVAR_FTW_ABORT_OLD &&
		     candidate.queue_disposition == PAYLOAD_MM_AUTHVAR_FTW_QUEUE_ABORT_OLD)) {
			candidate.action = PAYLOAD_MM_AUTHVAR_FTW_RESTORE_WORKSPACE;
			candidate.workspace = PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE;
		} else {
			candidate.action = PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
		}
	} else if (active_valid &&
		   bytes_are(working, candidate.geometry.working_size, 0xff) &&
		   bytes_are(spare, candidate.geometry.spare_size, 0xff)) {
		candidate.action = PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE;
		candidate.workspace = PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING;
	} else if (active_valid && workspace_uncommitted(working,
		   candidate.geometry.working_size) &&
		   bytes_are(spare, candidate.geometry.spare_size, 0xff)) {
		candidate.action = PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED;
		candidate.workspace = PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_WORKING;
	} else if (active_valid &&
		   bytes_are(working, candidate.geometry.working_size, 0xff) &&
		   workspace_uncommitted(spare, candidate.geometry.working_size) &&
		   bytes_are(spare + candidate.geometry.working_size,
			candidate.geometry.spare_size - candidate.geometry.working_size,
			0xffU)) {
		candidate.action = PAYLOAD_MM_AUTHVAR_FTW_DISCARD_UNCOMMITTED;
		candidate.workspace = PAYLOAD_MM_AUTHVAR_FTW_WORKSPACE_SPARE;
	} else {
		candidate.action = PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED;
	}
	if (candidate.action == PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED)
		return CB_ERR;
	*plan = candidate;
	return CB_SUCCESS;
}
