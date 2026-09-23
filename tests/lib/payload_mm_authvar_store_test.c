/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_store.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/helpers.h>
#include <stdlib.h>
#include <string.h>

#include "payload_mm_authvar_store_edk2_2609_fixture.h"

#define assert(c) do { if (!(c)) abort(); } while (0)

#define STORE_SIZE 4096U
#define MAX_ENTRIES 8U

static uint8_t store[STORE_SIZE];
static struct payload_mm_authvar_store_entry entries[MAX_ENTRIES];
static struct payload_mm_authvar_store_index index;
static const struct payload_mm_authvar_store_limits limits = {
	.maximum_store_size = STORE_SIZE,
	.maximum_name_size = 256,
	.maximum_data_size = 1024,
	.maximum_records = 16,
};
static const uint8_t store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};
static const uint8_t guid_a[16] = { 1, 2, 3, 4, 5, 6, 7, 8,
	9, 10, 11, 12, 13, 14, 15, 16 };
static const uint8_t guid_b[16] = { 16, 15, 14, 13, 12, 11, 10, 9,
	8, 7, 6, 5, 4, 3, 2, 1 };

static void put16(size_t offset, uint16_t value)
{
	store[offset] = (uint8_t)value;
	store[offset + 1] = (uint8_t)(value >> 8);
}

static void put32(size_t offset, uint32_t value)
{
	for (size_t i = 0; i < 4; i++)
		store[offset + i] = (uint8_t)(value >> (8U * i));
}

static void init_store(void)
{
	memset(store, 0xff, sizeof(store));
	memcpy(store, store_guid, sizeof(store_guid));
	put32(16, sizeof(store));
	store[20] = 0x5a;
	store[21] = 0xfe;
	put16(22, 0);
	put32(24, 0);
	memset(entries, 0xa5, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
}

static size_t add_record(size_t offset, uint8_t state, uint32_t attributes,
	const uint8_t guid[16], const uint16_t *name, size_t name_words,
	const void *data, size_t data_size)
{
	const size_t name_size = name_words * sizeof(*name);
	const size_t name_end = offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
		name_size;
	const size_t data_offset = (name_end + 3U) & ~(size_t)3U;
	const size_t end = data_offset + data_size;
	const size_t next = (end + 3U) & ~(size_t)3U;

	assert(next <= sizeof(store));
	memset(store + offset, 0, next - offset);
	put16(offset, 0x55aa);
	store[offset + 2] = state;
	put32(offset + 4, attributes);
	put32(offset + 36, (uint32_t)name_size);
	put32(offset + 40, (uint32_t)data_size);
	memcpy(store + offset + 44, guid, 16);
	memcpy(store + offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, name, name_size);
	memset(store + name_end, 0xff, data_offset - name_end);
	memcpy(store + data_offset, data, data_size);
	memset(store + end, 0xff, next - end);
	return next;
}

static enum cb_err scan(void)
{
	return payload_mm_authvar_store_scan(&index, store, sizeof(store), &limits);
}

static void valid_store_and_semantics(void)
{
	static const uint16_t alpha[] = { 'A', 'l', 'p', 'h', 'a', 0 };
	static const uint16_t beta[] = { 'B', 'e', 't', 'a', 0 };
	static const uint8_t first[] = { 1, 2, 3 };
	static const uint8_t second[] = { 4, 5, 6, 7 };
	const struct payload_mm_authvar_store_entry *entry;
	size_t offset;
	size_t position = 0;

	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3c, 7, guid_a,
		alpha, 6, first, sizeof(first));
	offset = add_record(offset, 0x3f, 7, guid_a, alpha, 6, second, sizeof(second));
	offset = add_record(offset, 0x3e, 7, guid_b, beta, 5, first, sizeof(first));
	assert(scan() == CB_SUCCESS);
	assert(index.store_size == STORE_SIZE && index.used_size == offset);
	assert(index.record_count == 3 && index.entry_count == 2);
	entry = payload_mm_authvar_store_find(&index, guid_a, alpha, sizeof(alpha));
	assert(entry && entry->data_size == sizeof(second));
	assert(!memcmp(payload_mm_authvar_store_data(&index, entry), second,
		sizeof(second)));
	assert(!memcmp(payload_mm_authvar_store_name(&index, entry), alpha,
		sizeof(alpha)));
	assert(payload_mm_authvar_store_next(&index, &position) == entry);
	assert(payload_mm_authvar_store_next(&index, &position) != NULL);
	assert(payload_mm_authvar_store_next(&index, &position) == NULL);
	assert(payload_mm_authvar_store_find(&index, guid_b, beta, sizeof(beta)) != NULL);
}

static void pinned_edk2_fixture(void)
{
	static const uint16_t name[] = { 'F', 'x', 0 };
	static const uint8_t zero_guid[16];
	static const uint8_t data[] = { 0xde, 0xad, 0xbe };
	const struct payload_mm_authvar_store_entry *entry;

	init_store();
	memcpy(store, edk2_2609_store_fixture, sizeof(edk2_2609_store_fixture));
	assert(payload_mm_authvar_store_scan(&index, store,
		sizeof(edk2_2609_store_fixture), &limits) == CB_SUCCESS);
	assert(index.store_size == sizeof(edk2_2609_store_fixture));
	assert(index.record_count == 1 && index.entry_count == 1);
	entry = payload_mm_authvar_store_find(&index, zero_guid, name, sizeof(name));
	assert(entry && entry->data_offset == 96 && entry->data_size == sizeof(data));
	assert(!memcmp(payload_mm_authvar_store_data(&index, entry), data, sizeof(data)));
}

static void expect_header_failure(size_t offset, uint8_t value)
{
	init_store();
	store[offset] = value;
	assert(scan() == CB_ERR);
	assert(index.store == NULL && index.entry_count == 0);
}

static void hostile_store_headers(void)
{
	struct payload_mm_authvar_store_limits changed = limits;
	struct payload_mm_authvar_store_index changed_index;

	expect_header_failure(0, 0);
	expect_header_failure(17, 0);
	expect_header_failure(20, 0);
	expect_header_failure(21, 0);
	expect_header_failure(22, 1);
	expect_header_failure(24, 1);
	init_store();
	put32(16, STORE_SIZE + 1);
	assert(scan() == CB_ERR);
	init_store();
	changed_index = index;
	changed_index.entries = NULL;
	assert(payload_mm_authvar_store_scan(&changed_index, store, sizeof(store),
		&limits) == CB_ERR);
	changed_index = index;
	changed.maximum_records = 0;
	assert(payload_mm_authvar_store_scan(&changed_index, store, sizeof(store),
		&changed) == CB_ERR);
}

static size_t one_valid_record(const uint16_t *name, size_t words)
{
	static const uint8_t data[] = { 0x55, 0xaa, 0x5a };

	init_store();
	return add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3f, 7, guid_a,
		name, words, data, sizeof(data));
}

static void hostile_records(void)
{
	static const uint16_t name[] = { 'V', 'a', 'r', 0 };
	static const uint16_t padded_name[] = { 'P', 'd', 0 };
	static const uint8_t dummy;
	const size_t base = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	size_t end = one_valid_record(name, 4);
	const struct { size_t offset; uint8_t value; } mutations[] = {
		{ 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 1 }, { 4, 0x80 },
		{ 8, 1 }, { 12, 1 }, { 16, 1 }, { 32, 1 }, { 36, 3 },
	};

	assert(scan() == CB_SUCCESS && index.used_size == end);
	for (size_t i = 0; i < ARRAY_SIZE(mutations); i++) {
		one_valid_record(name, 4);
		store[base + mutations[i].offset] = mutations[i].value;
		assert(scan() == CB_ERR);
	}
	one_valid_record(name, 4);
	store[base + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + 2] = 0;
	store[base + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + 3] = 0;
	assert(scan() == CB_ERR);
	one_valid_record(name, 4);
	store[end - 1] = 0;
	assert(scan() == CB_ERR);
	one_valid_record(name, 4);
	put32(base + 36, limits.maximum_name_size + 2);
	assert(scan() == CB_ERR);
	one_valid_record(name, 4);
	put32(base + 40, limits.maximum_data_size + 1);
	assert(scan() == CB_ERR);
	init_store();
	add_record(base, 0x3f, 7, guid_a, name, 4, &dummy, 0);
	assert(scan() == CB_ERR);
	one_valid_record(name, 4);
	put32(base + 36, UINT32_MAX - 1U);
	put32(base + 40, UINT32_MAX);
	assert(scan() == CB_ERR);
	one_valid_record(name, 4);
	store[end] = 0;
	assert(scan() == CB_SUCCESS);
	assert(index.dirty_tail_offset == end && index.used_size == sizeof(store));
	init_store();
	end = add_record(base, PAYLOAD_MM_AUTHVAR_STATE_ERASED, 7, guid_a,
		name, 4, (const uint8_t[]) { 0xff, 0xff, 0xff }, 3);
	add_record(end, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_b,
		name, 4, (const uint8_t[]) { 1 }, 1);
	assert(scan() == CB_SUCCESS);
	assert(index.dirty_tail_offset == base && index.used_size == sizeof(store) &&
		!index.record_count && !index.entry_count);
	init_store();
	end = add_record(base,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION, 7, guid_a,
		name, 4, (const uint8_t[]) { 2 }, 1);
	{
		size_t erased = end;

		end = add_record(erased, PAYLOAD_MM_AUTHVAR_STATE_ERASED, 7, guid_b,
			name, 4, (const uint8_t[]) { 0xff, 0xff }, 2);
		add_record(end, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_b,
			name, 4, (const uint8_t[]) { 3 }, 1);
		assert(scan() == CB_SUCCESS);
		assert(index.dirty_tail_offset == erased &&
			index.used_size == sizeof(store) && index.record_count == 1 &&
			index.entry_count == 1);
		assert(payload_mm_authvar_store_find(&index, guid_a, name,
			sizeof(name)) != NULL);
		assert(payload_mm_authvar_store_find(&index, guid_b, name,
			sizeof(name)) == NULL);
	}
	one_valid_record(padded_name, ARRAY_SIZE(padded_name));
	store[base + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
		sizeof(padded_name)] = 0;
	assert(scan() == CB_ERR);
}

static void duplicate_and_resource_caps(void)
{
	static const uint16_t name[] = { 'D', 'u', 'p', 0 };
	static const uint8_t data = 1;
	struct payload_mm_authvar_store_limits changed = limits;
	size_t offset;

	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3f, 7, guid_a,
		name, 4, &data, 1);
	add_record(offset, 0x3f, 7, guid_a, name, 4, &data, 1);
	assert(scan() == CB_ERR);
	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3c, 7, guid_a,
		name, 4, &data, 1);
	add_record(offset, 0x3f, 7, guid_a, name, 4, &data, 1);
	assert(scan() == CB_SUCCESS && index.entry_count == 1);
	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3e, 7, guid_a,
		name, 4, &data, 1);
	add_record(offset, 0x3f, 7, guid_a, name, 4, &data, 1);
	assert(scan() == CB_SUCCESS && index.entry_count == 1 &&
		index.entries[0].record_offset == offset);
	changed.maximum_records = 1;
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store), &changed) == CB_ERR);
	index.entry_capacity = 0;
	assert(scan() == CB_ERR);
}

static void legal_guid_values(void)
{
	static const uint16_t name[] = { 'G', 0 };
	static const uint8_t all_ones_guid[16] = {
		[0 ... 15] = 0xff,
	};
	static const uint8_t data = 1;

	init_store();
	add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3f, 7,
		all_ones_guid, name, ARRAY_SIZE(name), &data, sizeof(data));
	assert(scan() == CB_SUCCESS && index.entry_count == 1);
	assert(payload_mm_authvar_store_find(&index, all_ones_guid, name,
		sizeof(name)) != NULL);
}

static void attribute_combinations(void)
{
	static const uint16_t name[] = { 'A', 0 };
	static const uint8_t data = 1;
	const uint32_t invalid_visible[] = {
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE,
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE,
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR,
	};

	for (size_t i = 0; i < ARRAY_SIZE(invalid_visible); i++) {
		init_store();
		add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3f,
			invalid_visible[i], guid_a, name, ARRAY_SIZE(name), &data,
			sizeof(data));
		assert(scan() == CB_ERR);
	}
	init_store();
	add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3c,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE, guid_a, name,
		ARRAY_SIZE(name), &data, sizeof(data));
	assert(scan() == CB_SUCCESS && !index.entry_count && index.record_count == 1);
	init_store();
	add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x7f,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE, guid_a, name,
		ARRAY_SIZE(name), &data, sizeof(data));
	assert(scan() == CB_SUCCESS && !index.entry_count && index.record_count == 1);
}

static void valid_time_record(const uint16_t *name, size_t name_words)
{
	const size_t base = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	one_valid_record(name, name_words);
	put32(base + 4, 7 | (1U << 5));
	put16(base + 16, 2026);
	store[base + 18] = 9;
	store[base + 19] = 22;
	store[base + 20] = 12;
}

static void time_authentication_metadata(void)
{
	static const uint16_t name[] = { 'T', 0 };
	const size_t base = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	/* EDK2 certdb/VendorKeys initialization stores this zero sentinel. */
	one_valid_record(name, ARRAY_SIZE(name));
	put32(base + 4, 7 | PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED);
	assert(scan() == CB_SUCCESS);
	valid_time_record(name, ARRAY_SIZE(name));
	assert(scan() == CB_SUCCESS);
	valid_time_record(name, ARRAY_SIZE(name));
	put32(base + 24, 1);
	assert(scan() == CB_ERR);
	valid_time_record(name, ARRAY_SIZE(name));
	put16(base + 28, 1);
	assert(scan() == CB_ERR);
	valid_time_record(name, ARRAY_SIZE(name));
	store[base + 30] = 1;
	assert(scan() == CB_ERR);
	valid_time_record(name, ARRAY_SIZE(name));
	store[base + 18] = 13;
	assert(scan() == CB_ERR);
	one_valid_record(name, 2);
	store[base + 16] = 1;
	assert(scan() == CB_ERR);
}

static void every_buffer_boundary(void)
{
	static const uint16_t name[] = { 'B', 0 };
	size_t record_end;

	record_end = one_valid_record(name, 2);
	for (size_t size = 0; size < sizeof(store); size++) {
		assert(payload_mm_authvar_store_scan(&index, store, size, &limits) ==
			CB_ERR);
		assert(index.store == NULL && !index.entry_count && !index.record_count);
	}
	for (size_t size = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE; size < record_end;
	     size++) {
		one_valid_record(name, 2);
		put32(16, (uint32_t)size);
		if (size == PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE) {
			assert(payload_mm_authvar_store_scan(&index, store, size, &limits) ==
				CB_SUCCESS);
		} else {
			assert(payload_mm_authvar_store_scan(&index, store, size, &limits) ==
				CB_ERR);
		}
	}
	one_valid_record(name, 2);
	put32(16, (uint32_t)record_end);
	assert(payload_mm_authvar_store_scan(&index, store, record_end, &limits) ==
		CB_SUCCESS);
}

int main(void)
{
	valid_store_and_semantics();
	pinned_edk2_fixture();
	hostile_store_headers();
	hostile_records();
	duplicate_and_resource_caps();
	legal_guid_values();
	attribute_combinations();
	time_authentication_metadata();
	every_buffer_boundary();
	return 0;
}
