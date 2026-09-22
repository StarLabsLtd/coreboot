/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <boot/payload_mm_authvar_writer.h>
#include <stdlib.h>
#include <string.h>

#define assert(c) do { if (!(c)) abort(); } while (0)
#define STORE_SIZE 1024U
#define MAX_ENTRIES 8U

static uint8_t store[STORE_SIZE];
static uint8_t output[512];
static struct payload_mm_authvar_store_entry entries[MAX_ENTRIES];
static struct payload_mm_authvar_reclaim_copy copies[MAX_ENTRIES];
static struct payload_mm_authvar_store_index index;
static const struct payload_mm_authvar_store_limits limits = {
	.maximum_store_size = STORE_SIZE,
	.maximum_name_size = 128,
	.maximum_data_size = 256,
	.maximum_records = 32,
};
static const struct payload_mm_authvar_write_policy policy = {
	.maximum_name_size = 128,
	.maximum_record_size = 256,
	.maximum_data_size = 128,
	.maximum_records = 32,
};
static const uint8_t store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};
static const uint8_t guid[16] = { 1 };
static const uint16_t name[] = { 'A', 0 };
static const uint32_t attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
	PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
	PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;

static void put16(uint8_t *bytes, size_t offset, uint16_t value)
{
	bytes[offset] = (uint8_t)value;
	bytes[offset + 1U] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, size_t offset, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		bytes[offset + i] = (uint8_t)(value >> (8U * i));
}

static uint32_t get32(const uint8_t *bytes, size_t offset)
{
	return (uint32_t)bytes[offset] | (uint32_t)bytes[offset + 1U] << 8 |
		(uint32_t)bytes[offset + 2U] << 16 |
		(uint32_t)bytes[offset + 3U] << 24;
}

static void init_store(uint32_t size)
{
	memset(store, 0xff, sizeof(store));
	memcpy(store, store_guid, sizeof(store_guid));
	put32(store, 16, size);
	store[20] = 0x5a;
	store[21] = 0xfe;
	put16(store, 22, 0);
	put32(store, 24, 0);
	memset(entries, 0, sizeof(entries));
	index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = MAX_ENTRIES,
	};
}

static size_t add_record_attrs(size_t offset, uint8_t state, const void *data,
	size_t data_size, const uint8_t timestamp[16], uint32_t record_attributes)
{
	const size_t name_end = offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
		sizeof(name);
	const size_t data_offset = (name_end + 3U) & ~(size_t)3U;
	const size_t data_end = data_offset + data_size;
	const size_t next = (data_end + 3U) & ~(size_t)3U;

	assert(next <= sizeof(store));
	memset(store + offset, 0xff, next - offset);
	put16(store, offset, 0x55aa);
	store[offset + 2U] = state;
	store[offset + 3U] = 0;
	put32(store, offset + 4U, record_attributes);
	memset(store + offset + 8U, 0, 8);
	memcpy(store + offset + 16U, timestamp, 16);
	put32(store, offset + 32U, 0);
	put32(store, offset + 36U, sizeof(name));
	put32(store, offset + 40U, (uint32_t)data_size);
	memcpy(store + offset + 44U, guid, sizeof(guid));
	memcpy(store + offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, name,
		sizeof(name));
	memcpy(store + data_offset, data, data_size);
	return next;
}

static size_t add_record(size_t offset, uint8_t state, const void *data,
	size_t data_size, const uint8_t timestamp[16])
{
	return add_record_attrs(offset, state, data, data_size, timestamp, attributes);
}

static void scan(void)
{
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_SUCCESS);
}

static struct payload_mm_authvar_record_source source(const void *data,
	size_t size, uint32_t attrs)
{
	struct payload_mm_authvar_record_source result = {
		.name = name,
		.name_size = sizeof(name),
		.data = data,
		.data_size = size,
		.attributes = attrs,
	};

	memcpy(result.vendor_guid, guid, sizeof(guid));
	return result;
}

static struct payload_mm_authvar_reclaim_plan reclaim_plan(void)
{
	return (struct payload_mm_authvar_reclaim_plan) {
		.copies = copies,
		.copy_capacity = MAX_ENTRIES,
	};
}

static void apply_step(const struct payload_mm_authvar_write_plan *plan,
	uint32_t step)
{
	const struct payload_mm_authvar_write_step *operation = &plan->steps[step];

	if (operation->kind == PAYLOAD_MM_AUTHVAR_WRITE_RECORD) {
		assert(operation->offset + operation->size <= sizeof(store));
		memcpy(store + operation->offset, output, operation->size);
		return;
	}
	assert(operation->size == 1U && operation->offset < sizeof(store));
	assert(store[operation->offset] == operation->expected_state);
	store[operation->offset] = operation->new_state;
}

static void reset_prefixes_are_visible(void)
{
	static const uint8_t old[] = { 1 };
	static const uint8_t fresh[] = { 2 };
	struct payload_mm_authvar_record_source input = source(fresh, sizeof(fresh),
		attributes);
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan plan;
	struct payload_mm_authvar_write_plan saved;
	uint8_t saved_record[sizeof(output)];
	size_t old_offset;

	init_store(STORE_SIZE);
	add_record(28, 0x3f, old, sizeof(old), (const uint8_t[16]) { 0 });
	scan();
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, output, sizeof(output), &reclaim, &plan) == 0);
	saved = plan;
	memcpy(saved_record, output, sizeof(output));
	for (uint32_t prefix = 0; prefix <= saved.step_count; prefix++) {
		init_store(STORE_SIZE);
		old_offset = 28;
		add_record(old_offset, 0x3f, old, sizeof(old),
			(const uint8_t[16]) { 0 });
		memcpy(output, saved_record, sizeof(output));
		for (uint32_t step = 0; step < prefix; step++)
			apply_step(&saved, step);
		scan();
		assert(index.entry_count == 1);
		if (prefix < 4U)
			assert(index.entries[0].record_offset == old_offset);
		else
			assert(index.entries[0].record_offset == saved.record_offset);
	}
}

static void all_plan_prefixes_are_visible(void)
{
	static const uint8_t old[] = { 1 };
	static const uint8_t fresh[] = { 2 };
	struct payload_mm_authvar_record_source input = source(fresh, sizeof(fresh),
		attributes);
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan saved;
	uint8_t saved_record[sizeof(output)];
	size_t current;

	init_store(STORE_SIZE);
	scan();
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, output, sizeof(output), &reclaim, &saved) == 0);
	memcpy(saved_record, output, sizeof(output));
	for (uint32_t prefix = 0; prefix <= saved.step_count; prefix++) {
		init_store(STORE_SIZE);
		memcpy(output, saved_record, sizeof(output));
		for (uint32_t step = 0; step < prefix; step++)
			apply_step(&saved, step);
		scan();
		assert(index.entry_count == (prefix == saved.step_count ? 1U : 0U));
	}

	init_store(STORE_SIZE);
	current = add_record(28, 0x3e, old, sizeof(old),
		(const uint8_t[16]) { 0 });
	add_record(current, 0x3f, old, sizeof(old), (const uint8_t[16]) { 0 });
	scan();
	input.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, output, sizeof(output), &reclaim, &saved) == 0);
	assert(saved.step_count == 6U);
	memcpy(saved_record, output, sizeof(output));
	for (uint32_t prefix = 0; prefix <= saved.step_count; prefix++) {
		init_store(STORE_SIZE);
		current = add_record(28, 0x3e, old, sizeof(old),
			(const uint8_t[16]) { 0 });
		add_record(current, 0x3f, old, sizeof(old),
			(const uint8_t[16]) { 0 });
		memcpy(output, saved_record, sizeof(output));
		for (uint32_t step = 0; step < prefix; step++)
			apply_step(&saved, step);
		scan();
		assert(index.entry_count == 1U);
		assert(index.entries[0].record_offset ==
			(prefix < 5U ? current : saved.record_offset));
	}

	init_store(STORE_SIZE);
	current = add_record(28, 0x3e, old, sizeof(old),
		(const uint8_t[16]) { 0 });
	add_record(current, 0x3f, old, sizeof(old), (const uint8_t[16]) { 0 });
	scan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], NULL, NULL,
		false, NULL, 0, NULL, &saved) == 0);
	assert(saved.step_count == 2U);
	for (uint32_t prefix = 0; prefix <= saved.step_count; prefix++) {
		init_store(STORE_SIZE);
		current = add_record(28, 0x3e, old, sizeof(old),
			(const uint8_t[16]) { 0 });
		add_record(current, 0x3f, old, sizeof(old),
			(const uint8_t[16]) { 0 });
		for (uint32_t step = 0; step < prefix; step++)
			apply_step(&saved, step);
		scan();
		assert(index.entry_count == (prefix == 2U ? 0U : 1U));
	}

}

static void torn_tail_boundaries(void)
{
	static const uint8_t old[] = { 1 };
	static const uint8_t fresh[] = { 2, 3, 4, 5, 6 };
	struct payload_mm_authvar_record_source input = source(fresh, sizeof(fresh),
		attributes);
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan plan;
	uint8_t candidate[sizeof(output)];
	uint32_t candidate_size;
	size_t tail;

	init_store(STORE_SIZE);
	scan();
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, candidate, sizeof(candidate), &reclaim, &plan) == 0);
	candidate_size = plan.record_size;
	for (uint32_t cut = 0; cut <= candidate_size; cut++) {
		init_store(STORE_SIZE);
		tail = add_record(28, 0x3e, old, sizeof(old),
			(const uint8_t[16]) { 0 });
		memcpy(store + tail, candidate, cut);
		scan();
		assert(index.entry_count == 1 &&
			index.entries[0].record_offset == 28U);
		if (!cut) {
			assert(!index.dirty_tail_offset);
			continue;
		}
		if (index.dirty_tail_offset)
			assert(index.dirty_tail_offset == tail &&
				index.used_size == STORE_SIZE);
		else
			assert(index.record_count == 2U &&
				index.used_size == tail + candidate_size);
	}

	init_store(STORE_SIZE);
	tail = add_record(28, 0x3e, old, sizeof(old),
		(const uint8_t[16]) { 0 });
	memcpy(store + tail, candidate, candidate_size);
	store[tail + 2U] = 0x7f;
	scan();
	assert(index.entry_count == 1 && index.record_count == 2U &&
		index.entries[0].record_offset == 28U);
	store[tail + 2U] = 0x3f;
	store[tail + 36U] = 3;
	assert(payload_mm_authvar_store_scan(&index, store, sizeof(store),
		&limits) == CB_ERR);
}

static void create_and_update_order(void)
{
	static const uint8_t old[] = { 1, 2 };
	static const uint8_t fresh[] = { 3, 4, 5 };
	struct payload_mm_authvar_record_source input = source(fresh, sizeof(fresh),
		attributes);
	struct payload_mm_authvar_reclaim_plan reclaim = reclaim_plan();
	struct payload_mm_authvar_write_plan plan;

	init_store(STORE_SIZE);
	scan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_WRITE_TAIL_APPEND &&
		plan.step_count == 3 && plan.record_offset == 28U);
	assert(plan.steps[0].kind == PAYLOAD_MM_AUTHVAR_WRITE_RECORD &&
		plan.steps[0].offset == plan.record_offset &&
		plan.steps[0].size == plan.record_size &&
		output[2] == PAYLOAD_MM_AUTHVAR_STATE_ERASED &&
		plan.steps[1].expected_state == 0xff &&
		plan.steps[1].new_state == 0x7f &&
		plan.steps[2].expected_state == 0x7f &&
		plan.steps[2].new_state == 0x3f);
	assert(output[2] == 0xff && get32(output, 4) == attributes &&
		get32(output, 36) == sizeof(name) && get32(output, 40) == sizeof(fresh));

	init_store(STORE_SIZE);
	add_record(28, 0x3f, old, sizeof(old), (const uint8_t[16]) { 0 });
	scan();
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(plan.step_count == 5 && plan.steps[0].expected_state == 0x3f &&
		plan.steps[0].new_state == 0x3e &&
		plan.steps[1].kind == PAYLOAD_MM_AUTHVAR_WRITE_RECORD &&
		plan.steps[2].new_state == 0x7f && plan.steps[3].new_state == 0x3f &&
		plan.steps[4].expected_state == 0x3e &&
		plan.steps[4].new_state == 0x3c);
}

static void append_delete_and_noop(void)
{
	static const uint8_t old[] = { 1, 2 };
	static const uint8_t suffix[] = { 3, 4 };
	struct payload_mm_authvar_record_source input = source(suffix,
		sizeof(suffix), attributes | PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE);
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan plan;
	size_t prior;
	size_t current;

	init_store(STORE_SIZE);
	prior = add_record(28, 0x3e, old, sizeof(old), (const uint8_t[16]) { 0 });
	current = prior;
	add_record(current, 0x3f, old, sizeof(old), (const uint8_t[16]) { 0 });
	scan();
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, output, sizeof(output), &reclaim, &plan) == 0);
	assert(plan.step_count == 6 && plan.steps[0].offset == 30U &&
		plan.steps[0].new_state == 0x3c);
	assert(get32(output, 4) == attributes && get32(output, 40) == 4U);
	assert(!memcmp(output + 64U, (const uint8_t[]) { 1, 2, 3, 4 }, 4));

	input.data_size = 0;
	input.data = NULL;
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, NULL, 0, NULL, &plan) == 0);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_WRITE_NOOP && !plan.step_count);
	init_store(STORE_SIZE);
	scan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, NULL, 0, NULL, &plan) == 0);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_WRITE_NOOP && !plan.step_count);
	init_store(STORE_SIZE);
	prior = add_record(28, 0x3e, old, sizeof(old),
		(const uint8_t[16]) { 0 });
	current = prior;
	add_record(current, 0x3f, old, sizeof(old), (const uint8_t[16]) { 0 });
	scan();

	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], NULL, NULL,
		false, NULL, 0, NULL, &plan) == 0);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_WRITE_DELETE &&
		plan.step_count == 2 && plan.steps[0].offset == 30U &&
		plan.steps[1].offset == current + 2U);

	input = source(old, sizeof(old), attributes);
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, NULL, 0, NULL, &plan) == 0);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_WRITE_NOOP && !plan.record_size);
}

static void timestamp_and_limits(void)
{
	static const uint8_t value[] = { 1 };
	static const uint8_t old_time[16] = { 0xe8, 0x07, 12, 1, 2, 3, 4 };
	struct payload_mm_authvar_record_source input = source(value, sizeof(value),
		attributes | PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED |
		PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE);
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan plan;
	struct payload_mm_authvar_write_policy small = policy;

	init_store(STORE_SIZE);
	add_record_attrs(28, 0x3f, value, sizeof(value), old_time,
		attributes | PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED);
	scan();
	input.timestamp[0] = 0xe8;
	input.timestamp[1] = 0x07;
	input.timestamp[2] = 11;
	input.timestamp[3] = 1;
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, output, sizeof(output), &reclaim, &plan) == 0);
	assert(!memcmp(output + 16, old_time, 16));
	memcpy(input.timestamp, old_time, 16);
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, output, sizeof(output), &reclaim, &plan) == 0);
	assert(!memcmp(output + 16, old_time, 16));
	input.timestamp[3] = 2;
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, output, sizeof(output), &reclaim, &plan) == 0);
	assert(!memcmp(output + 16, input.timestamp, 16));

	small.maximum_data_size = 1;
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&small, false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!plan.step_count && plan.record_size);
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, output, 1, &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	assert(!plan.step_count && plan.record_size > 1U);
}

static void policy_and_runtime_limits(void)
{
	static const uint8_t old[] = { 1 };
	static const uint8_t fresh[] = { 2 };
	static const uint16_t long_name[] = { 'A', 'B', 0 };
	struct payload_mm_authvar_record_source input = source(fresh, sizeof(fresh),
		attributes);
	struct payload_mm_authvar_write_policy bounded = policy;
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan plan;

	init_store(STORE_SIZE);
	add_record(28, 0x3f, old, sizeof(old), (const uint8_t[16]) { 0 });
	scan();
	bounded.maximum_records = 1;
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&bounded, false, output, sizeof(output), &reclaim, &plan) == 0);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM && !plan.step_count);
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&bounded, true, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES);
	assert(!plan.step_count);

	input.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
		&policy, false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!plan.step_count);
	init_store(STORE_SIZE);
	add_record_attrs(28, 0x3f, old, sizeof(old),
		(const uint8_t[16]) { 0 },
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS);
	scan();
	assert(payload_mm_authvar_write_plan_build(&index, &entries[0], NULL, NULL,
		true, NULL, 0, NULL, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!plan.step_count);
	input = source(fresh, sizeof(fresh),
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	input = source(fresh, sizeof(fresh), attributes |
		PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);

	input = source(fresh, sizeof(fresh), attributes);
	input.name = long_name;
	input.name_size = sizeof(long_name);
	bounded = policy;
	bounded.maximum_name_size = sizeof(name);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &bounded,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	bounded = policy;
	bounded.maximum_name_size = limits.maximum_name_size + 2U;
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &bounded,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	bounded = policy;
	bounded.maximum_data_size = limits.maximum_data_size + 1U;
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &bounded,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	bounded = policy;
	bounded.maximum_records = limits.maximum_records + 1U;
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &bounded,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);

	init_store(STORE_SIZE);
	add_record(28, 0x3f, old, sizeof(old), (const uint8_t[16]) { 0 });
	scan();
	input = source(fresh, sizeof(fresh), attributes);
	input.vendor_guid[0] = 2;
	bounded = policy;
	bounded.maximum_records = 1;
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &bounded,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES);
	assert(!plan.step_count);
}

static void recovery_and_reclaim(void)
{
	static const uint8_t value[] = { 1 };
	static const uint8_t fresh[] = { 2 };
	struct payload_mm_authvar_record_source input = source(fresh, sizeof(fresh),
		attributes);
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan plan;
	size_t tail;

	init_store(STORE_SIZE);
	tail = add_record(28, 0x3f, value, sizeof(value),
		(const uint8_t[16]) { 0 });
	put16(store, tail, 0x55aa);
	store[tail + 2U] = 0xff;
	store[tail + 3U] = 0;
	put32(store, tail + 4U, attributes);
	scan();
	assert(index.dirty_tail_offset == tail && index.used_size == STORE_SIZE &&
		index.entry_count == 1);
	store[30] = 0x3e;
	scan();
	assert(index.entries[0].record_offset == 28U);
	input.vendor_guid[0] = 2;
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&policy, false, output, sizeof(output), &reclaim, &plan) == 0);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM && !plan.step_count &&
		plan.record_offset == 96U && plan.record_size == 68U &&
		reclaim.compacted_used_size == 96U && reclaim.destination_offset == 96U &&
		reclaim.copy_count == 1U && reclaim.copies[0].source_offset == 28U &&
		reclaim.copies[0].destination_offset == 28U &&
		reclaim.copies[0].promote_transition);

	init_store(STORE_SIZE);
	tail = add_record(28, 0x3f, value, sizeof(value),
		(const uint8_t[16]) { 0 });
	add_record(tail, 0xff, value, sizeof(value), (const uint8_t[16]) { 0 });
	scan();
	assert(!index.dirty_tail_offset && index.record_count == 2 &&
		index.entry_count == 1 && index.used_size > tail);
}

static void physical_and_logical_caps_are_independent(void)
{
	static const uint8_t old[] = { 1 };
	static const uint8_t fresh[] = { 2 };
	struct payload_mm_authvar_record_source input = source(fresh, sizeof(fresh),
		attributes);
	struct payload_mm_authvar_write_policy bounded = policy;
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan plan;
	size_t offset;

	bounded.maximum_records = 12;
	for (uint32_t dead = 10; dead <= 11; dead++) {
		init_store(STORE_SIZE);
		offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
		for (uint32_t i = 0; i < dead; i++)
			offset = add_record(offset,
				PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED, old, sizeof(old),
				(const uint8_t[16]) { 0 });
		add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, old, sizeof(old),
			(const uint8_t[16]) { 0 });
		scan();
		assert(index.record_count == dead + 1U && index.entry_count == 1U &&
			index.entry_capacity == 8U);
		reclaim = reclaim_plan();
		assert(payload_mm_authvar_write_plan_build(&index, &entries[0], &input,
			&bounded, false, output, sizeof(output), &reclaim, &plan) == 0);
		assert(plan.action == (dead == 10U ?
			PAYLOAD_MM_AUTHVAR_WRITE_TAIL_APPEND :
			PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM));
		if (dead == 11U) {
			reclaim = reclaim_plan();
			assert(payload_mm_authvar_write_plan_build(&index, &entries[0],
				&input, &bounded, true, output, sizeof(output), &reclaim,
				&plan) == PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES);
		}
	}

	init_store(STORE_SIZE);
	offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	for (uint8_t i = 1; i <= MAX_ENTRIES; i++) {
		size_t record = offset;

		offset = add_record(offset, PAYLOAD_MM_AUTHVAR_STATE_ADDED, old,
			sizeof(old), (const uint8_t[16]) { 0 });
		store[record + 44U] = i;
	}
	scan();
	assert(index.entry_count == index.entry_capacity &&
		index.record_count < bounded.maximum_records);
	input.vendor_guid[0] = MAX_ENTRIES + 1U;
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &bounded,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES);
}

static void hostile_inputs(void)
{
	static const uint8_t value[] = { 1 };
	struct payload_mm_authvar_record_source input = source(value, sizeof(value),
		attributes);
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan plan;
	struct payload_mm_authvar_store_entry fake = { 0 };

	init_store(STORE_SIZE);
	scan();
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, output, sizeof(output), &reclaim, &plan) == 0);
	input.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE;
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	assert(!plan.step_count);
	input = source(value, sizeof(value), attributes);
	assert(payload_mm_authvar_write_plan_build(&index, &fake, &input, &policy,
		false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, store, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, output, sizeof(output), (void *)&input, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input, &policy,
		false, output, sizeof(output), &reclaim, (void *)store) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
}

static void hostile_alias_matrix(void)
{
	uint8_t value[] = { 1 };
	struct payload_mm_authvar_record_source input = source(value, sizeof(value),
		attributes);
	struct payload_mm_authvar_write_policy local_policy = policy;
	struct payload_mm_authvar_reclaim_plan reclaim;
	struct payload_mm_authvar_write_plan plan;

	init_store(STORE_SIZE);
	scan();
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, &index, sizeof(index), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), (void *)&index, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	reclaim = reclaim_plan();
	reclaim.copies = (void *)&index;
	reclaim.copy_capacity = 1;
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), &reclaim,
		(void *)&index) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), &reclaim,
		(void *)&input) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), &reclaim,
		(void *)input.name) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), &reclaim,
		(void *)input.data) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), &reclaim,
		(void *)&local_policy) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, &local_policy, sizeof(local_policy), &reclaim,
		&plan) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), &reclaim,
		(void *)output) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), (void *)&input, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	reclaim = reclaim_plan();
	reclaim.copies = (void *)&input;
	reclaim.copy_capacity = 1;
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	reclaim = reclaim_plan();
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, (void *)(UINTPTR_MAX - 1U), 4U, &reclaim,
		&plan) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	input.data = (void *)(UINTPTR_MAX - 1U);
	input.data_size = 4U;
	assert(payload_mm_authvar_write_plan_build(&index, NULL, &input,
		&local_policy, false, output, sizeof(output), &reclaim, &plan) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
}

int main(void)
{
	create_and_update_order();
	append_delete_and_noop();
	reset_prefixes_are_visible();
	all_plan_prefixes_are_visible();
	torn_tail_boundaries();
	timestamp_and_limits();
	policy_and_runtime_limits();
	recovery_and_reclaim();
	physical_and_logical_caps_are_independent();
	hostile_inputs();
	hostile_alias_matrix();
	return 0;
}
