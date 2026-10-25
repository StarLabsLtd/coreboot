/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_store.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "storage.h"

static uint32_t read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
		(uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool erased(const uint8_t *data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		if (data[i] != 0xff)
			return false;
	return true;
}

static bool canonical_store(const uint8_t *store, size_t size)
{
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_SIZE,
		.maximum_name_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE,
		.maximum_data_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE,
		.maximum_records = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS,
	};
	struct payload_mm_authvar_store_entry *entries =
		xmalloc(limits.maximum_records * sizeof(*entries));
	struct payload_mm_authvar_store_index index = {
		.entries = entries,
		.entry_capacity = limits.maximum_records,
	};
	bool valid = payload_mm_authvar_store_scan(&index, store, size, &limits) ==
		CB_SUCCESS && index.dirty_tail_offset == 0;

	free(entries);
	return valid;
}

static bool convert_records(const uint8_t *source, uint8_t *destination, size_t size)
{
	size_t input = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	size_t output = input;

	memset(destination + output, 0xff, size - output);
	while (input < size && !erased(source + input, size - input)) {
		const uint8_t *header = source + input;
		size_t name_size, data_size, name_end, data_offset, next, record_size;

		if (size - input < PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE ||
		    header[0] != 0xaa || header[1] != 0x55)
			return false;
		name_size = read_le32(header + 36);
		data_size = read_le32(header + 40);
		if (name_size > size - input - PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE)
			return false;
		name_end = input + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + name_size;
		if (name_end > SIZE_MAX - 3)
			return false;
		data_offset = (name_end + 3) & ~(size_t)3;
		if (data_offset > size || data_size > size - data_offset ||
		    data_offset + data_size > SIZE_MAX - 3)
			return false;
		next = (data_offset + data_size + 3) & ~(size_t)3;
		if (next > size || !erased(source + name_end, data_offset - name_end) ||
		    !erased(source + data_offset + data_size, next - data_offset - data_size))
			return false;
		record_size = (PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + name_size +
			data_size + 3) & ~(size_t)3;
		if (record_size > size - output)
			return false;
		/* Copy authentication metadata and states, not the tool's lossy var_t. */
		memcpy(destination + output, header,
			PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + name_size);
		memcpy(destination + output + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
			name_size, source + data_offset, data_size);
		input = next;
		output += record_size;
	}
	return canonical_store(destination, size);
}

bool storage_migrate_cdk2_padded(const char input[], const char output[])
{
	struct mem_range_t source = map_file(input, false);
	struct payload_mm_authvar_ftw_plan plan;
	uint8_t *candidate = NULL;
	const uint8_t *store;
	size_t written = 0;
	int fd = -1;
	bool success = false;

	if (source.start == NULL)
		return false;
	/* A raw SMMSTORE backup carries its erase geometry in the FV block map. */
	if (source.length < 64 ||
	    payload_mm_authvar_ftw_plan(source.start, source.length,
		read_le32(source.start + 60), &plan) != CB_SUCCESS ||
	    plan.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN ||
	    plan.variable_store_size > PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_SIZE) {
		fprintf(stderr, "Migration requires a valid raw SMMSTORE region with clean FTW state\n");
		goto out;
	}
	store = source.start + plan.fv_header_size;
	if (canonical_store(store, plan.variable_store_size)) {
		fprintf(stderr, "Input is canonical or ambiguous; no migration performed\n");
		goto out;
	}
	candidate = xmalloc(source.length);
	memcpy(candidate, source.start, source.length);
	if (!convert_records(store, candidate + plan.fv_header_size, plan.variable_store_size)) {
		fprintf(stderr, "Invalid legacy records or dirty tail; no migration performed\n");
		goto out;
	}
	fd = open(output, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		fprintf(stderr, "Cannot create output: %s\n", strerror(errno));
		goto out;
	}
	while (written < source.length) {
		ssize_t count = write(fd, candidate + written, source.length - written);

		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			break;
		written += count;
	}
	success = written == source.length && fsync(fd) == 0;
	if (close(fd) != 0)
		success = false;
	if (!success) {
		fprintf(stderr, "Failed to write migration output; removing incomplete file\n");
		if (unlink(output) != 0)
			fprintf(stderr, "Cannot remove incomplete output: %s\n", strerror(errno));
	}
out:
	free(candidate);
	unmap_file(source);
	return success;
}
