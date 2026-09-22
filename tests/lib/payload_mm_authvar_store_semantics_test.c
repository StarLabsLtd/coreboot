/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <boot/payload_mm_authvar_store_semantics.h>
#include <stdlib.h>
#include <string.h>

#define assert(c) do { if (!(c)) abort(); } while (0)

#define STORE_CAPACITY 1024U
#define MAX_ENTRIES 8U

static uint8_t store[STORE_CAPACITY];
static struct payload_mm_authvar_store_entry entries[MAX_ENTRIES];
static struct payload_mm_authvar_store_index index;
static const struct payload_mm_authvar_store_limits limits = {
	.maximum_store_size = STORE_CAPACITY,
	.maximum_name_size = 128,
	.maximum_data_size = 256,
	.maximum_records = 32,
};
static const uint8_t store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};
static const uint8_t guid_a[16] = { 1 };
static const uint8_t guid_b[16] = { 2 };
static const uint8_t zero_guid[16];
static const uint16_t name_a[] = { 'A', 0 };
static const uint16_t name_b[] = { 'B', 0 };

static void put16(size_t offset, uint16_t value)
{
	store[offset] = (uint8_t)value;
	store[offset + 1U] = (uint8_t)(value >> 8);
}

static void put32(size_t offset, uint32_t value)
{
	for (size_t i = 0; i < 4U; i++)
		store[offset + i] = (uint8_t)(value >> (8U * i));
}

static void init_store(uint32_t store_size)
{
	memset(store, 0xff, sizeof(store));
	memcpy(store, store_guid, sizeof(store_guid));
	put32(16, store_size);
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
	store[offset + 2U] = state;
	put32(offset + 4U, attributes);
	put32(offset + 36U, (uint32_t)name_size);
	put32(offset + 40U, (uint32_t)data_size);
	memcpy(store + offset + 44U, guid, 16);
	memcpy(store + offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, name,
		name_size);
	memset(store + name_end, 0xff, data_offset - name_end);
	memcpy(store + data_offset, data, data_size);
	memset(store + end, 0xff, next - end);
	return next;
}

static void scan_store(void)
{
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_SUCCESS);
}

static void read_semantics(void)
{
	static const uint8_t value_a[] = { 1, 2, 3 };
	static const uint8_t value_b[] = { 4, 5 };
	static const uint16_t missing[] = { 'Z', 0 };
	static const uint16_t empty[] = { 0 };
	struct payload_mm_authvar_get_result get;
	struct payload_mm_authvar_next_result next;
	const struct payload_mm_authvar_new_record candidate = {
		.name_size = sizeof(name_a),
		.data_size = 1,
	};
	struct payload_mm_authvar_reclaim_plan plan = { 0 };
	size_t offset;

	init_store(STORE_CAPACITY);
	scan_store();
	assert(payload_mm_authvar_store_get_next(&index, zero_guid, NULL, 0, 4, false,
		&next) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);

	init_store(STORE_CAPACITY);
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3f,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
		guid_a, name_a, 2, value_a, sizeof(value_a));
	add_record(offset, 0x3f,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		guid_b, name_b, 2, value_b, sizeof(value_b));
	scan_store();

	assert(payload_mm_authvar_store_get(&index, guid_a, name_a, sizeof(name_a),
		2, false, &get) == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	assert(!get.entry && get.required_data_size == sizeof(value_a) &&
		get.attributes == 3U);
	assert(payload_mm_authvar_store_get(&index, guid_a, name_a, sizeof(name_a),
		3, false, &get) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(get.entry == &index.entries[0]);
	assert(payload_mm_authvar_store_get(&index, guid_a, name_a, sizeof(name_a),
		3, true, &get) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	assert(payload_mm_authvar_store_get(&index, guid_a, empty, sizeof(empty),
		0, false, &get) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	index.entries[0].data_size = 0;
	assert(payload_mm_authvar_store_get(&index, guid_a, name_a, sizeof(name_a),
		3, false, &get) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	index.entries[0].data_size = sizeof(value_a);

	assert(payload_mm_authvar_store_get_next(&index, zero_guid, NULL, 0, 0, false,
		&next) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_store_get_next(&index, zero_guid, NULL, 0, 1, false,
		&next) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_store_get_next(&index, zero_guid, NULL, 0, 2, false,
		&next) == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	assert(!next.entry && next.required_name_size == sizeof(name_a));
	assert(payload_mm_authvar_store_get_next(&index, zero_guid, NULL, 0, 4, false,
		&next) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(next.entry == &index.entries[0]);
	assert(payload_mm_authvar_store_get_next(&index, guid_a, NULL, 0, 4, false,
		&next) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_store_get_next(&index, guid_a, name_a,
		sizeof(name_a), 4, false, &next) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(next.entry == &index.entries[1]);
	assert(payload_mm_authvar_store_get_next(&index, guid_a, name_a,
		sizeof(name_a), 2, false, &next) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_store_get_next(&index, guid_b, name_b,
		sizeof(name_b), 4, false, &next) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	assert(payload_mm_authvar_store_get_next(&index, guid_a, missing,
		sizeof(missing), 4, false, &next) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_store_get_next(&index, zero_guid, NULL, 0, 4, true,
		&next) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(next.entry == &index.entries[1]);
	assert(payload_mm_authvar_store_reclaim_plan(&index, NULL, &candidate,
		false, &plan) == CB_SUCCESS);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_SPACE_APPEND &&
		plan.destination_offset == index.used_size && !plan.copy_count);
}

static void query_and_space_plan(void)
{
	static const uint8_t value = 1;
	static const uint16_t dead_name[] = { 'D', 'x', 0 };
	const uint32_t attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	const struct payload_mm_authvar_store_policy policy = {
		.maximum_storage = STORE_CAPACITY - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		.maximum_record_size = 256,
	};
	const struct payload_mm_authvar_new_record candidate = {
		.name_size = sizeof(name_b),
		.data_size = sizeof(value),
	};
	const struct payload_mm_authvar_new_record zero_data_candidate = {
		.name_size = sizeof(name_b),
	};
	struct payload_mm_authvar_reclaim_copy copies[MAX_ENTRIES];
	struct payload_mm_authvar_reclaim_plan plan = {
		.copies = copies,
		.copy_capacity = MAX_ENTRIES,
	};
	struct payload_mm_authvar_query_result query;
	size_t offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	init_store(STORE_CAPACITY);
	for (size_t i = 0; i < 12U; i++)
		offset = add_record(offset, 0x3d, attributes, guid_a, dead_name, 3,
			&value, sizeof(value));
	add_record(offset, 0x3f, attributes, guid_a, name_a, 2, &value,
		sizeof(value));
	scan_store();
	assert(index.entry_count == 1 && index.used_size == 960U);
	assert(payload_mm_authvar_store_query(&index, &policy, attributes, false,
		&query) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(query.maximum_storage == 996U && query.remaining_storage == 928U &&
		query.maximum_variable == 196U);
	assert(payload_mm_authvar_store_query(&index, &policy, attributes, true,
		&query) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(query.remaining_storage == 64U && query.maximum_variable == 4U);
	assert(payload_mm_authvar_store_query(&index, &policy,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE, false, &query) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_store_query(&index, &policy, 0, false, &query) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_store_query(&index, &policy,
		attributes | PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE, false, &query) ==
		PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	assert(payload_mm_authvar_store_reclaim_plan(&index, NULL,
		&zero_data_candidate, false, &plan) == CB_ERR);

	assert(payload_mm_authvar_store_reclaim_plan(&index, NULL, &candidate,
		false, &plan) == CB_SUCCESS);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_SPACE_RECLAIM &&
		plan.record_size == 68U && plan.compacted_used_size == 96U &&
		plan.reclaimable_size == 864U && plan.destination_offset == 96U &&
		plan.copy_count == 1);
	assert(plan.copies[0].source_offset == 892U &&
		plan.copies[0].destination_offset == 28U &&
		!plan.copies[0].promote_transition);
	assert(payload_mm_authvar_store_reclaim_plan(&index, NULL, &candidate,
		true, &plan) == CB_SUCCESS);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_SPACE_OUT_OF_RESOURCES);
}

static void reclaim_order_and_replacement(void)
{
	static const uint8_t old = 1;
	static const uint8_t fresh = 2;
	const uint32_t attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	const struct payload_mm_authvar_new_record candidate = {
		.name_size = sizeof(name_a),
		.data_size = 780,
	};
	struct payload_mm_authvar_reclaim_copy copies[MAX_ENTRIES];
	struct payload_mm_authvar_reclaim_plan plan = {
		.copies = copies,
		.copy_capacity = MAX_ENTRIES,
	};
	size_t transition_a;
	size_t transition_b;
	size_t added_a;

	init_store(STORE_CAPACITY);
	transition_a = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	transition_b = add_record(transition_a, 0x3e, attributes, guid_a, name_a, 2,
		&old, sizeof(old));
	added_a = add_record(transition_b, 0x3e, attributes, guid_b, name_b, 2,
		&old, sizeof(old));
	add_record(added_a, 0x3f, attributes, guid_a, name_a, 2, &fresh,
		sizeof(fresh));
	scan_store();
	assert(index.entry_count == 2 && index.entries[0].record_offset == transition_b &&
		index.entries[1].record_offset == added_a);
	assert(payload_mm_authvar_store_reclaim_plan(&index, NULL, &candidate,
		false, &plan) == CB_SUCCESS);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_SPACE_RECLAIM);
	assert(plan.copy_count == 2);
	assert(plan.copies[0].source_offset == added_a &&
		!plan.copies[0].promote_transition);
	assert(plan.copies[1].source_offset == transition_b &&
		plan.copies[1].promote_transition);
	assert(payload_mm_authvar_store_reclaim_plan(&index, &index.entries[1],
		&candidate, false, &plan) == CB_SUCCESS);
	assert(plan.copy_count == 1 && plan.copies[0].source_offset == transition_b);

	plan.copy_capacity = 0;
	assert(payload_mm_authvar_store_reclaim_plan(&index, NULL, &candidate,
		false, &plan) == CB_ERR);
}

static void query_class_isolation(void)
{
	static const uint8_t value = 1;
	const uint32_t common_attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	const uint32_t hardware_attributes = common_attributes |
		PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR;
	const struct payload_mm_authvar_store_policy policy = {
		.maximum_storage = STORE_CAPACITY - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		.maximum_record_size = 256,
	};
	struct payload_mm_authvar_query_result query;
	size_t offset;

	init_store(STORE_CAPACITY);
	offset = add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3f,
		common_attributes, guid_a, name_a, 2, &value, sizeof(value));
	add_record(offset, 0x3f, hardware_attributes, guid_b, name_b, 2, &value,
		sizeof(value));
	scan_store();
	assert(payload_mm_authvar_store_query(&index, &policy, common_attributes,
		false, &query) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(query.remaining_storage == 928U);
	assert(payload_mm_authvar_store_query(&index, &policy, common_attributes,
		true, &query) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(query.remaining_storage == 928U);
	assert(payload_mm_authvar_store_query(&index, &policy, hardware_attributes,
		false, &query) == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
}

static void hostile_inputs_fail_closed(void)
{
	static const uint8_t value = 1;
	static const uint8_t odd_name[] = { 'A', 0, 0 };
	const uint32_t attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	struct payload_mm_authvar_store_policy bad_policy = {
		.maximum_storage = STORE_CAPACITY,
		.maximum_record_size = 256,
	};
	struct payload_mm_authvar_store_entry fake_entry = { 0 };
	const struct payload_mm_authvar_new_record overflowing = {
		.name_size = UINT32_MAX,
		.data_size = UINT32_MAX,
	};
	struct payload_mm_authvar_query_result query = {
		.maximum_storage = UINT64_MAX,
	};
	struct payload_mm_authvar_get_result get;
	struct payload_mm_authvar_reclaim_plan plan = { 0 };

	init_store(STORE_CAPACITY);
	add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3f, attributes, guid_a,
		name_a, 2, &value, sizeof(value));
	scan_store();
	assert(payload_mm_authvar_store_get(&index, guid_a, odd_name,
		sizeof(odd_name), 1, false, &get) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_store_query(&index, &bad_policy, attributes, false,
		&query) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!query.maximum_storage && !query.remaining_storage &&
		!query.maximum_variable);
	assert(payload_mm_authvar_store_reclaim_plan(&index, &fake_entry, NULL,
		false, &plan) == CB_ERR);
	assert(payload_mm_authvar_store_reclaim_plan(&index, NULL, &overflowing,
		false, &plan) == CB_ERR);
}

static void zero_guid_is_a_legal_key(void)
{
	static const uint8_t value = 1;
	const uint32_t attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	struct payload_mm_authvar_get_result get;
	struct payload_mm_authvar_next_result next;

	init_store(STORE_CAPACITY);
	add_record(PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, 0x3f, attributes, zero_guid,
		name_a, 2, &value, sizeof(value));
	scan_store();
	assert(payload_mm_authvar_store_get(&index, zero_guid, name_a,
		sizeof(name_a), sizeof(value), false, &get) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(payload_mm_authvar_store_get_next(&index, zero_guid, NULL, 0,
		sizeof(name_a), false, &next) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(next.entry == &index.entries[0] &&
		!memcmp(next.entry->vendor_guid, zero_guid, sizeof(zero_guid)));
}

int main(void)
{
	read_semantics();
	query_and_space_plan();
	reclaim_order_and_replacement();
	query_class_isolation();
	hostile_inputs_fail_closed();
	zero_guid_is_a_legal_key();
	return 0;
}
