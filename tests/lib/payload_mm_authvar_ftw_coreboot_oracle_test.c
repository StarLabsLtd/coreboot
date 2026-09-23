/* SPDX-License-Identifier: GPL-2.0-only */

#define main payload_mm_authvar_acceptance_embedded_main
#include "payload_mm_authvar_executor_acceptance_test.c"
#undef main

extern int creat(const char *path, unsigned int mode);

static const uint8_t oracle_fv_guid[16] = {
	0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
	0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
};
static const uint8_t oracle_work_guid[16] = {
	0x2b, 0x29, 0x58, 0x9e, 0x68, 0x7c, 0x7d, 0x49,
	0xa0, 0xce, 0x65, 0x00, 0xfd, 0x9f, 0x1b, 0x95,
};
static const uint8_t oracle_coreboot_guid[16] = {
	0xf3, 0x2e, 0xa5, 0xc4, 0x27, 0x4e, 0xe2, 0x47,
	0x95, 0x0b, 0xfd, 0xaa, 0xb5, 0x21, 0xb8, 0x95,
};
static const uint8_t oracle_store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};

static uint64_t oracle_le64(const uint8_t *bytes)
{
	uint64_t value = 0;

	for (size_t i = 0; i < sizeof(value); i++)
		value |= (uint64_t)bytes[i] << (8U * i);
	return value;
}

static uint32_t oracle_crc32(const uint8_t *bytes, size_t size)
{
	uint32_t crc = UINT32_MAX;

	for (size_t i = 0; i < size; i++) {
		crc ^= bytes[i];
		for (unsigned int bit = 0; bit < 8U; bit++)
			crc = (crc >> 1) ^ (0xedb88320U &
				(uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

struct observed_states {
	bool pre_fd;
	bool fd;
	bool f9;
	bool f8;
};

static const char *snapshot_directory;

static void save_snapshot(const char *name, const uint8_t *media)
{
	char path[512];
	int file;

	assert(snapshot_directory);
	assert(snprintf(path, sizeof(path), "%s/%s", snapshot_directory, name) > 0);
	file = creat(path, 0600);
	assert(file >= 0);
	assert(write(file, media, REGION_SIZE) == REGION_SIZE);
	assert(close(file) == 0);
}

static void edk2_fv_oracle(const uint8_t *fv)
{
	uint16_t checksum = 0;
	const uint8_t *store = fv + 72U;

	assert(bytes_are(fv, 16U, 0U));
	assert(!memcmp(fv + 16U, oracle_fv_guid, sizeof(oracle_fv_guid)));
	assert(oracle_le64(fv + 32U) == REGION_SIZE);
	assert(read_le32(fv + 40U) == 0x4856465fU);
	assert(read_le32(fv + 44U) == 0x00000e36U);
	assert(read_le16(fv + 48U) == 72U && read_le16(fv + 52U) == 0U);
	assert(fv[54] == 0U && fv[55] == 2U);
	assert(read_le32(fv + 56U) == 3U && read_le32(fv + 60U) == BLOCK_SIZE);
	assert(read_le64(fv + 64U) == 0U);
	for (size_t i = 0; i < 72U; i += 2U)
		checksum = (uint16_t)(checksum + read_le16(fv + i));
	assert(checksum == 0U);
	assert(!memcmp(store, oracle_store_guid, sizeof(oracle_store_guid)));
	assert(read_le32(store + 16U) == STORE_SIZE);
	assert(store[20] == 0x5aU && store[21] == 0xfeU);
	assert(read_le16(store + 22U) == 0U && read_le32(store + 24U) == 0U);
}

static void edk2_base_oracle(const uint8_t *media)
{
	const uint8_t *workspace = media + BLOCK_SIZE;
	uint8_t canonical[32];

	edk2_fv_oracle(media);
	assert(!memcmp(workspace, oracle_work_guid, sizeof(oracle_work_guid)));
	memcpy(canonical, workspace, sizeof(canonical));
	memset(canonical + 16U, 0xff, 8U);
	assert(read_le32(workspace + 16U) == oracle_crc32(canonical,
		sizeof(canonical)));
	assert(workspace[20] == 0xfeU && oracle_le64(workspace + 24U) == 4064U);
}

static void edk2_queue_oracle(const uint8_t *media,
	struct observed_states *observed)
{
	const uint8_t *workspace = media + BLOCK_SIZE;
	const uint8_t *header = workspace + 32U;
	const uint8_t *record = header + 40U;
	bool tuple;

	edk2_base_oracle(media);
	if (header[0] == 0xffU) {
		if (bytes_are(header, BLOCK_SIZE - 32U, 0xffU))
			return;
		assert(bytes_are(header + 1U, 3U, 0xffU));
		assert(!memcmp(header + 4U, oracle_coreboot_guid,
			sizeof(oracle_coreboot_guid)));
		assert(bytes_are(header + 20U, 4U, 0xffU));
		assert(oracle_le64(header + 24U) == 1U &&
			oracle_le64(header + 32U) == 0U);
		assert(bytes_are(record, BLOCK_SIZE - 72U, 0xffU));
		return;
	}
	assert((header[0] == 0xfeU || header[0] == 0xfcU ||
		header[0] == 0xf8U) &&
		bytes_are(header + 1U, 3U, 0xffU));
	assert(!memcmp(header + 4U, oracle_coreboot_guid,
		sizeof(oracle_coreboot_guid)));
	assert(bytes_are(header + 20U, 4U, 0xffU));
	assert(oracle_le64(header + 24U) == 1U && oracle_le64(header + 32U) == 0U);
	assert((record[0] == 0xffU || record[0] == 0xfdU || record[0] == 0xf9U) &&
		bytes_are(record + 1U, 7U, 0xffU));
	tuple = oracle_le64(record + 8U) == 0U &&
		oracle_le64(record + 16U) == 72U &&
		oracle_le64(record + 24U) == STORE_SIZE &&
		oracle_le64(record + 32U) ==
			(uint64_t)-(int64_t)(2U * BLOCK_SIZE);
	if (record[0] != 0xffU)
		assert(tuple);
	if (record[0] == 0xfdU) {
		edk2_fv_oracle(media + 2U * BLOCK_SIZE);
	} else if (record[0] == 0xf9U) {
		edk2_fv_oracle(media + 2U * BLOCK_SIZE);
		assert(!memcmp(media, media + 2U * BLOCK_SIZE, BLOCK_SIZE));
	}
	assert(bytes_are(workspace + 112U, BLOCK_SIZE - 112U, 0xffU));
	if (header[0] == 0xfcU && record[0] == 0xffU && tuple &&
	    !observed->pre_fd) {
		save_snapshot("coreboot-pre-fd.bin", media);
		observed->pre_fd = true;
	} else if (header[0] == 0xfcU && record[0] == 0xfdU && !observed->fd) {
		assert(memcmp(media, media + 2U * BLOCK_SIZE, BLOCK_SIZE));
		save_snapshot("coreboot-fd.bin", media);
		observed->fd = true;
	} else if (header[0] == 0xfcU && record[0] == 0xf9U && !observed->f9) {
		save_snapshot("coreboot-f9.bin", media);
		observed->f9 = true;
	} else if (header[0] == 0xf8U && record[0] == 0xf9U && !observed->f8) {
		save_snapshot("coreboot-f8.bin", media);
		observed->f8 = true;
	}
}

int main(int argc, char **argv)
{
	struct shared_state *shared = mmap(NULL, sizeof(*shared),
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	uint32_t program_sizes[64];
	uint32_t erase_count;
	uint32_t fault_counts[FAULT_END + 1U] = { 0 };
	uint32_t program_count;
	struct observed_states observed = { 0 };

	assert(argc == 2);
	snapshot_directory = argv[1];
	assert(shared != MAP_FAILED);
	program_count = discover_reclaim_programs(shared, program_sizes,
		ARRAY_SIZE(program_sizes), &erase_count, fault_counts);
	assert(erase_count);
	for (uint32_t occurrence = 1; occurrence <= program_count; occurrence++) {
		prepare_first_value(shared);
		shared->cut_kind = CUT_PROGRAM;
		shared->cut_occurrence = occurrence;
		shared->cut_bytes = program_sizes[occurrence - 1U];
		shared->cut_mask = MASK_PREFIX;
		assert(run_child(shared, CHILD_APPLY_REPLACE) == CHILD_CUT_EXIT);
		edk2_queue_oracle(shared->media, &observed);
	}
	assert(observed.pre_fd && observed.fd && observed.f9 && observed.f8);
	assert(munmap(shared, sizeof(*shared)) == 0);
	return 0;
}
