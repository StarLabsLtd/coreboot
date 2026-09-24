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

static size_t one_valid_record(const uint16_t *name, size_t words);

static void assert_find_matches_scan(const uint8_t guid[16],
	const uint16_t *name, size_t name_size)
{
	struct payload_mm_authvar_store_entry result;
	struct payload_mm_authvar_store_entry zero = { 0 };
	const struct payload_mm_authvar_store_entry *expected;
	bool found = true;
	const enum cb_err scan_result = scan();
	const enum cb_err find_result = payload_mm_authvar_store_find_one(&result,
		&found, store, sizeof(store), &limits, guid, name, name_size);

	assert(find_result == scan_result);
	if (scan_result != CB_SUCCESS) {
		assert(!found && !memcmp(&result, &zero, sizeof(result)));
		return;
	}
	expected = payload_mm_authvar_store_find(&index, guid, name, name_size);
	assert(found == (expected != NULL));
	if (expected)
		assert(!memcmp(&result, expected, sizeof(result)));
	else
		assert(!memcmp(&result, &zero, sizeof(result)));
}

static void assert_find_matches_scan_with_limits(const uint8_t guid[16],
	const uint16_t *name, size_t name_size,
	const struct payload_mm_authvar_store_limits *test_limits)
{
	struct payload_mm_authvar_store_entry result;
	struct payload_mm_authvar_store_entry zero = { 0 };
	const struct payload_mm_authvar_store_entry *expected;
	bool found = true;
	const enum cb_err scan_result = payload_mm_authvar_store_scan(&index, store,
		sizeof(store), test_limits);
	const enum cb_err find_result = payload_mm_authvar_store_find_one(&result,
		&found, store, sizeof(store), test_limits, guid, name, name_size);

	assert(find_result == scan_result);
	if (scan_result != CB_SUCCESS) {
		assert(!found && !memcmp(&result, &zero, sizeof(result)));
		return;
	}
	expected = payload_mm_authvar_store_find(&index, guid, name, name_size);
	assert(found == (expected != NULL));
	if (expected)
		assert(!memcmp(&result, expected, sizeof(result)));
	else
		assert(!memcmp(&result, &zero, sizeof(result)));
}

static void single_key_differential(void)
{
	static const uint16_t target[] = { 'M', 'O', 'R', 0 };
	static const uint16_t other[] = { 'O', 't', 'h', 'e', 'r', 0 };
	static const uint16_t absent[] = { 'N', 'o', 'n', 'e', 0 };
	static const uint8_t first[] = { 1 };
	static const uint8_t second[] = { 2, 3 };
	struct payload_mm_authvar_store_entry result;
	struct payload_mm_authvar_store_entry zero = { 0 };
	bool found;
	size_t offset;

	init_store();
	assert_find_matches_scan(guid_a, target, sizeof(target));

	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY, 7, guid_a, other,
		ARRAY_SIZE(other), first, sizeof(first));
	offset = add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_b,
		other, ARRAY_SIZE(other), first, sizeof(first));
	offset = add_record(offset,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION, 7, guid_a,
		target, ARRAY_SIZE(target), first, sizeof(first));
	add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a, target,
		ARRAY_SIZE(target), second, sizeof(second));
	assert_find_matches_scan(guid_a, target, sizeof(target));
	assert_find_matches_scan(guid_a, absent, sizeof(absent));

	/* A malformed duplicate for an unrelated key must reject the lookup. */
	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a, target,
		ARRAY_SIZE(target), first, sizeof(first));
	offset = add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_b,
		other, ARRAY_SIZE(other), first, sizeof(first));
	add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_b, other,
		ARRAY_SIZE(other), second, sizeof(second));
	assert_find_matches_scan(guid_a, target, sizeof(target));

	/* A transition may only be replaced by one subsequent added record. */
	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION, 7, guid_b,
		other, ARRAY_SIZE(other), first, sizeof(first));
	offset = add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_b,
		other, ARRAY_SIZE(other), second, sizeof(second));
	add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a, target,
		ARRAY_SIZE(target), first, sizeof(first));
	assert_find_matches_scan(guid_a, target, sizeof(target));

	memset(&result, 0xa5, sizeof(result));
	found = true;
	assert(payload_mm_authvar_store_find_one(&result, &found, store,
		sizeof(store), &limits, guid_a, target, sizeof(target) - 1U) == CB_SUCCESS);
	assert(!found && !memcmp(&result, &zero, sizeof(result)));
	memset(&result, 0xa5, sizeof(result));
	assert(payload_mm_authvar_store_find_one(&result, NULL, store,
		sizeof(store), &limits, guid_a, target, sizeof(target)) == CB_ERR);
	assert(!memcmp(&result, &zero, sizeof(result)));
}

static void single_key_state_and_limit_differential(void)
{
	static const uint16_t target[] = { 'M', 'O', 'R', 0 };
	static const uint16_t near_name[] = { 'M', 'O', 'S', 0 };
	static const uint8_t near_guid[16] = { 1, 2, 3, 4, 5, 6, 7, 8,
		9, 10, 11, 12, 13, 14, 15, 17 };
	static const uint8_t data[] = { 1, 2 };
	static const uint8_t states[] = {
		PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED,
		PAYLOAD_MM_AUTHVAR_STATE_TRANSITION_DELETED,
	};
	struct payload_mm_authvar_store_limits changed = limits;
	struct payload_mm_authvar_store_entry result;
	struct payload_mm_authvar_store_entry zero = { 0 };
	bool found = true;
	size_t offset;

	for (size_t i = 0; i < ARRAY_SIZE(states); i++) {
		init_store();
		add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, states[i], 7, guid_a,
			target, ARRAY_SIZE(target), data, sizeof(data));
		assert_find_matches_scan(guid_a, target, sizeof(target));
	}

	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, near_guid, target,
		ARRAY_SIZE(target), data, sizeof(data));
	add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a, near_name,
		ARRAY_SIZE(near_name), data, sizeof(data));
	assert_find_matches_scan(guid_a, target, sizeof(target));
	assert(payload_mm_authvar_store_find_one(&result, &found, store,
		sizeof(store), &limits, guid_a, target, (size_t)UINT32_MAX + 1U) ==
		CB_SUCCESS);
	assert(!found && !memcmp(&result, &zero, sizeof(result)));

	/* Preserve a winner before either accepted form of dirty tail. */
	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a, target,
		ARRAY_SIZE(target), data, sizeof(data));
	store[offset] = 0;
	assert_find_matches_scan(guid_a, target, sizeof(target));
	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a, target,
		ARRAY_SIZE(target), data, sizeof(data));
	add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ERASED, 7, guid_b, near_name,
		ARRAY_SIZE(near_name), data, sizeof(data));
	put32(offset + 36U, limits.maximum_name_size + 2U);
	assert_find_matches_scan(guid_a, target, sizeof(target));

	/* Deleted records count toward the exact record cap without consuming index. */
	changed.maximum_records = 2;
	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED, 7, guid_a, target,
		ARRAY_SIZE(target), data, sizeof(data));
	add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_TRANSITION_DELETED, 7, guid_b,
		near_name, ARRAY_SIZE(near_name), data, sizeof(data));
	assert_find_matches_scan_with_limits(guid_a, target, sizeof(target), &changed);
	offset = add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_TRANSITION_DELETED, 7,
		near_guid, near_name, ARRAY_SIZE(near_name), data, sizeof(data));
	assert_find_matches_scan_with_limits(guid_a, target, sizeof(target), &changed);

	/* Structural corruption of an unrelated record still invalidates the store. */
	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a, target,
		ARRAY_SIZE(target), data, sizeof(data));
	add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_b, near_name,
		ARRAY_SIZE(near_name), data, sizeof(data));
	store[offset + 3U] = 1;
	assert_find_matches_scan(guid_a, target, sizeof(target));
}

static void single_key_many_unique(void)
{
	static const uint8_t data = 1;
	size_t offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	uint16_t names[MAX_ENTRIES][2];

	init_store();
	for (size_t i = 0; i < MAX_ENTRIES; i++) {
		names[i][0] = (uint16_t)('A' + i);
		names[i][1] = 0;
		offset = add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a,
			names[i], ARRAY_SIZE(names[i]), &data, sizeof(data));
	}
	assert_find_matches_scan(guid_a, names[MAX_ENTRIES - 1U],
		sizeof(names[MAX_ENTRIES - 1U]));
}

static void single_key_rejects_aliases(void)
{
	static const uint16_t target[] = { 'M', 'O', 'R', 0 };
	static const uint8_t data = 1;
	union {
		struct payload_mm_authvar_store_entry alignment;
		uint8_t bytes[sizeof(struct payload_mm_authvar_store_entry)];
	} shared;
	union {
		struct payload_mm_authvar_store_entry alignment;
		uint8_t bytes[64];
	} aliased_input;
	uint8_t saved_input[sizeof(aliased_input)];
	struct payload_mm_authvar_store_entry entry;
	uint8_t saved_store[STORE_SIZE];
	bool found;

	init_store();
	add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a, target,
		ARRAY_SIZE(target), &data, sizeof(data));
	memcpy(saved_store, store, sizeof(store));
	memset(&shared, 0xa5, sizeof(shared));
	assert(payload_mm_authvar_store_find_one(
		(struct payload_mm_authvar_store_entry *)shared.bytes,
		(bool *)shared.bytes, store, sizeof(store), &limits, guid_a, target,
		sizeof(target)) == CB_ERR);
	for (size_t i = 0; i < sizeof(shared); i++)
		assert(shared.bytes[i] == 0xa5);

	/* An output within the mapped store must not modify that input. */
	assert(!((uintptr_t)store % _Alignof(struct payload_mm_authvar_store_entry)));
	assert(payload_mm_authvar_store_find_one(
		(struct payload_mm_authvar_store_entry *)store, &found, store,
		sizeof(store), &limits, guid_a, target, sizeof(target)) == CB_ERR);
	assert(!memcmp(store, saved_store, sizeof(store)));
	assert(payload_mm_authvar_store_find_one(&entry, (bool *)store, store,
		sizeof(store), &limits, guid_a, target, sizeof(target)) == CB_ERR);
	assert(!memcmp(store, saved_store, sizeof(store)));

	memset(&aliased_input, 0x5a, sizeof(aliased_input));
	memcpy(saved_input, &aliased_input, sizeof(saved_input));
	assert(payload_mm_authvar_store_find_one(
		(struct payload_mm_authvar_store_entry *)aliased_input.bytes, &found,
		store, sizeof(store),
		(const struct payload_mm_authvar_store_limits *)aliased_input.bytes,
		guid_a, target, sizeof(target)) == CB_ERR);
	assert(!memcmp(&aliased_input, saved_input, sizeof(saved_input)));
	assert(payload_mm_authvar_store_find_one(
		(struct payload_mm_authvar_store_entry *)aliased_input.bytes, &found,
		store, sizeof(store), &limits, aliased_input.bytes, target,
		sizeof(target)) == CB_ERR);
	assert(!memcmp(&aliased_input, saved_input, sizeof(saved_input)));
	assert(payload_mm_authvar_store_find_one(
		(struct payload_mm_authvar_store_entry *)aliased_input.bytes, &found,
		store, sizeof(store), &limits, guid_a, aliased_input.bytes,
		sizeof(target)) == CB_ERR);
	assert(!memcmp(&aliased_input, saved_input, sizeof(saved_input)));

	memset(&entry, 0xa5, sizeof(entry));
	found = true;
	assert(payload_mm_authvar_store_find_one(&entry, NULL, store, sizeof(store),
		&limits, guid_a, target, sizeof(target)) == CB_ERR);
	assert(!memcmp(&entry, &(struct payload_mm_authvar_store_entry) { 0 },
		sizeof(entry)));
	memset(&entry, 0xa5, sizeof(entry));
	assert(payload_mm_authvar_store_find_one(NULL, &found, store, sizeof(store),
		&limits, guid_a, target, sizeof(target)) == CB_ERR);
	assert(!found);
	memset(&entry, 0xa5, sizeof(entry));
	found = true;
	assert(payload_mm_authvar_store_find_one(&entry, &found, store,
		sizeof(store), &limits, NULL, target, sizeof(target)) == CB_ERR);
	assert(!found && !memcmp(&entry,
		&(struct payload_mm_authvar_store_entry) { 0 }, sizeof(entry)));
	memset(&entry, 0xa5, sizeof(entry));
	found = true;
	assert(payload_mm_authvar_store_find_one(&entry, &found, store,
		sizeof(store), &limits, guid_a, NULL, sizeof(target)) == CB_ERR);
	assert(!found && !memcmp(&entry,
		&(struct payload_mm_authvar_store_entry) { 0 }, sizeof(entry)));
}

static void single_key_byte_mutation_differential(void)
{
	static const uint16_t target[] = { 'M', 'O', 'R', 0 };
	static const uint16_t other[] = { 'O', 't', 'h', 'e', 'r', 0 };
	static const uint16_t absent[] = { 'A', 'b', 's', 'e', 'n', 't', 0 };
	static const uint8_t first[] = { 1, 2 };
	static const uint8_t second[] = { 3, 4, 5 };
	uint8_t baseline[STORE_SIZE];
	size_t offset;
	size_t used_end;

	init_store();
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_b, other,
		ARRAY_SIZE(other), first, sizeof(first));
	offset = add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED, 7,
		guid_a, other, ARRAY_SIZE(other), first, sizeof(first));
	offset = add_record(offset,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION, 7, guid_a,
		target, ARRAY_SIZE(target), first, sizeof(first));
	offset = add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, 7, guid_a,
		target, ARRAY_SIZE(target), second, sizeof(second));
	used_end = add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY, 7,
		guid_b, target, ARRAY_SIZE(target), first, sizeof(first));
	memcpy(baseline, store, sizeof(baseline));

	for (size_t position = 0; position < used_end + 4U; position++) {
		const uint8_t values[] = {
			0x00, 0xff, (uint8_t)(baseline[position] ^ 1U),
		};

		for (size_t value = 0; value < ARRAY_SIZE(values); value++) {
			if (values[value] == baseline[position])
				continue;
			memcpy(store, baseline, sizeof(store));
			store[position] = values[value];
			assert_find_matches_scan(guid_a, target, sizeof(target));
			assert_find_matches_scan(guid_a, absent, sizeof(absent));
		}
	}
	memcpy(store, baseline, sizeof(store));
}

static void single_key_every_buffer_boundary(void)
{
	static const uint16_t name[] = { 'B', 0 };
	struct payload_mm_authvar_store_entry result;
	struct payload_mm_authvar_store_entry zero = { 0 };
	bool found;
	size_t record_end;

	one_valid_record(name, ARRAY_SIZE(name));
	for (size_t size = 0; size < sizeof(store); size++) {
		memset(&result, 0xa5, sizeof(result));
		found = true;
		assert(payload_mm_authvar_store_find_one(&result, &found, store, size,
			&limits, guid_a, name, sizeof(name)) == CB_ERR);
		assert(!found && !memcmp(&result, &zero, sizeof(result)));
	}
	record_end = one_valid_record(name, ARRAY_SIZE(name));
	for (size_t size = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE; size <= record_end;
	     size++) {
		one_valid_record(name, ARRAY_SIZE(name));
		put32(16U, (uint32_t)size);
		memset(&result, 0xa5, sizeof(result));
		found = true;
		assert(payload_mm_authvar_store_find_one(&result, &found, store, size,
			&limits, guid_a, name, sizeof(name)) ==
			(size == PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE || size == record_end ?
			 CB_SUCCESS : CB_ERR));
		assert(found == (size == record_end));
	}
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
	single_key_differential();
	single_key_state_and_limit_differential();
	single_key_many_unique();
	single_key_rejects_aliases();
	single_key_byte_mutation_differential();
	single_key_every_buffer_boundary();
	return 0;
}
