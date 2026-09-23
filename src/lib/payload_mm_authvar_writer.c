/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_writer.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable writer must only be built in SMM"
#endif

static bool multiply_size(size_t left, size_t right, size_t *result)
{
	if (left && right > SIZE_MAX / left)
		return false;
	*result = left * right;
	return true;
}

static bool buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_base = (uintptr_t)left;
	uintptr_t right_base = (uintptr_t)right;

	if (!left_size || !right_size)
		return false;
	if (!left || !right || left_size - 1U > UINTPTR_MAX - left_base ||
	    right_size - 1U > UINTPTR_MAX - right_base)
		return true;
	return left_base <= right_base + right_size - 1U &&
		right_base <= left_base + left_size - 1U;
}

static bool all_zero(const uint8_t *bytes, size_t size)
{
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static bool name_valid(const void *name, size_t size)
{
	const uint8_t *bytes = name;

	if (!bytes || size < 4U || size > UINT32_MAX || (size & 1U) ||
	    bytes[size - 2U] || bytes[size - 1U])
		return false;
	for (size_t offset = 0; offset + 2U < size; offset += 2U) {
		if (!bytes[offset] && !bytes[offset + 1U])
			return false;
	}
	return true;
}

static bool timestamp_valid(const uint8_t timestamp[16], uint32_t attributes)
{
	static const uint8_t days[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
	};
	uint16_t year;
	uint8_t maximum_day;

	if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED))
		return all_zero(timestamp, 16);
	year = (uint16_t)timestamp[0] | (uint16_t)timestamp[1] << 8;
	if (year < 1900 || year > 9999 || timestamp[2] < 1 || timestamp[2] > 12)
		return false;
	maximum_day = days[timestamp[2] - 1U];
	if (timestamp[2] == 2 && (!(year % 4) && ((year % 100) || !(year % 400))))
		maximum_day++;
	return timestamp[3] >= 1 && timestamp[3] <= maximum_day &&
		timestamp[4] <= 23 && timestamp[5] <= 59 && timestamp[6] <= 59 &&
		!timestamp[7] && all_zero(timestamp + 8, 8);
}

static int timestamp_compare(const uint8_t left[16], const uint8_t right[16])
{
	uint16_t left_year = (uint16_t)left[0] | (uint16_t)left[1] << 8;
	uint16_t right_year = (uint16_t)right[0] | (uint16_t)right[1] << 8;

	if (left_year != right_year)
		return left_year > right_year ? 1 : -1;
	return memcmp(left + 2, right + 2, 14);
}

static bool index_valid(const struct payload_mm_authvar_store_index *index)
{
	return index && index->store && index->entries &&
		index->store_size >= PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE &&
		index->used_size >= PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE &&
		index->used_size <= index->store_size &&
		index->maximum_name_size >= 4U && index->maximum_data_size &&
		index->maximum_records &&
		index->record_count <= index->maximum_records &&
		(!index->dirty_tail_offset ||
		 (index->dirty_tail_offset >= PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE &&
		  index->dirty_tail_offset < index->store_size &&
		  index->used_size == index->store_size)) &&
		index->entry_count <= index->entry_capacity;
}

static bool entry_member(const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry)
{
	uintptr_t base;
	uintptr_t address;
	size_t bytes;

	if (!index_valid(index) || !entry ||
	    (index->entry_count && sizeof(index->entries[0]) >
	     SIZE_MAX / index->entry_count))
		return false;
	base = (uintptr_t)index->entries;
	address = (uintptr_t)entry;
	bytes = index->entry_count * sizeof(index->entries[0]);
	return address >= base && address - base < bytes &&
		!((address - base) % sizeof(index->entries[0]));
}

static bool source_valid(const struct payload_mm_authvar_record_source *source,
	bool at_runtime)
{
	uint32_t attributes;

	if (!source || !name_valid(source->name, source->name_size) ||
	    source->data_size > UINT32_MAX ||
	    (source->data_size && !source->data))
		return false;
	attributes = source->attributes;
	if (!attributes || attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED ||
	    attributes & PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE ||
	    !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE) ||
	    !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS) ||
	    (at_runtime && !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)))
		return false;
	return timestamp_valid(source->timestamp, attributes);
}

static bool key_matches(const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry,
	const struct payload_mm_authvar_record_source *source)
{
	const void *name = payload_mm_authvar_store_name(index, entry);

	return name && entry->name_size == source->name_size &&
		!memcmp(entry->vendor_guid, source->vendor_guid, 16) &&
		!memcmp(name, source->name, source->name_size);
}

static bool record_size(const struct payload_mm_authvar_record_source *source,
	size_t data_size, size_t *size, size_t *data_offset)
{
	return payload_mm_authvar_record_layout(source->name_size, data_size, size,
		data_offset);
}

static bool add_step(struct payload_mm_authvar_write_plan *plan,
	enum payload_mm_authvar_write_step_kind kind, uint32_t offset, uint32_t size,
	uint8_t expected_state, uint8_t new_state)
{
	if (plan->step_count >= PAYLOAD_MM_AUTHVAR_WRITE_MAX_STEPS)
		return false;
	plan->steps[plan->step_count++] = (struct payload_mm_authvar_write_step) {
		.kind = kind,
		.offset = offset,
		.size = size,
		.expected_state = expected_state,
		.new_state = new_state,
	};
	return true;
}

static bool add_state(struct payload_mm_authvar_write_plan *plan,
	uint32_t record_offset, uint8_t expected_state, uint8_t new_state)
{
	return add_step(plan, PAYLOAD_MM_AUTHVAR_WRITE_STATE, record_offset + 2U, 1,
		expected_state, new_state);
}

static bool find_prior_transition(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *replaced, uint32_t *offset)
{
	if (!replaced) {
		*offset = UINT32_MAX;
		return true;
	}
	const uint32_t end = index->dirty_tail_offset ? index->dirty_tail_offset :
		index->used_size;

	for (uint32_t at = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE; at < end;) {
		const uint8_t *header = index->store + at;
		uint32_t name_size = (uint32_t)header[36] |
			(uint32_t)header[37] << 8 | (uint32_t)header[38] << 16 |
			(uint32_t)header[39] << 24;
		uint32_t data_size = (uint32_t)header[40] |
			(uint32_t)header[41] << 8 | (uint32_t)header[42] << 16 |
			(uint32_t)header[43] << 24;
		size_t data_at;
		size_t next;

		if (!record_size(&(struct payload_mm_authvar_record_source) {
			.name_size = name_size,
		}, data_size, &next, &data_at) || next > end - at)
			return false;
		if (at != replaced->record_offset && header[2] ==
		    PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION &&
		    name_size == replaced->name_size &&
		    !memcmp(header + 44, replaced->vendor_guid, 16) &&
		    !memcmp(header + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE,
			index->store + replaced->name_offset, name_size)) {
			*offset = at;
			return true;
		}
		at += (uint32_t)next;
	}
	*offset = UINT32_MAX;
	return true;
}

static bool build_record(const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *replaced,
	const struct payload_mm_authvar_record_source *source, void *record,
	size_t capacity, uint32_t *output_size)
{
	const bool append = source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	const uint8_t *old_data = NULL;
	size_t old_size = 0;
	const uint8_t *timestamp = source->timestamp;
	struct payload_mm_authvar_record_descriptor descriptor;
	struct payload_mm_authvar_record_span spans[2];
	size_t span_count = 0U;

	if (append && replaced) {
		old_data = payload_mm_authvar_store_data(index, replaced);
		if (!old_data)
			return false;
		old_size = replaced->data_size;
		if ((source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) &&
		    timestamp_compare(index->store + replaced->record_offset + 16U,
			source->timestamp) > 0)
			timestamp = index->store + replaced->record_offset + 16U;
	}
	if (source->data_size > SIZE_MAX - old_size)
		return false;
	descriptor = (struct payload_mm_authvar_record_descriptor) {
		.name = source->name,
		.name_size = source->name_size,
		.attributes = source->attributes &
			~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE,
	};
	memcpy(descriptor.vendor_guid, source->vendor_guid,
		sizeof(descriptor.vendor_guid));
	memcpy(descriptor.timestamp, timestamp, sizeof(descriptor.timestamp));
	if (old_size)
		spans[span_count++] = (struct payload_mm_authvar_record_span) {
			.data = old_data,
			.size = old_size,
		};
	if (source->data_size)
		spans[span_count++] = (struct payload_mm_authvar_record_span) {
			.data = source->data,
			.size = source->data_size,
		};
	return payload_mm_authvar_record_encode(&descriptor, spans, span_count,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED, record, capacity,
		output_size);
}

uint64_t payload_mm_authvar_write_plan_build(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *replaced,
	const struct payload_mm_authvar_record_source *source,
	const struct payload_mm_authvar_write_policy *policy, bool at_runtime,
	void *record, size_t record_capacity,
	struct payload_mm_authvar_reclaim_plan *reclaim,
	struct payload_mm_authvar_write_plan *plan)
{
	struct payload_mm_authvar_new_record candidate;
	struct payload_mm_authvar_write_plan draft = { 0 };
	size_t candidate_data_size;
	size_t required_size = 0;
	size_t data_offset;
	size_t reclaim_copy_bytes = 0;
	size_t entry_bytes;
	bool force_reclaim;
	uint32_t prior_transition;
	uint8_t old_state;

	if (reclaim && !multiply_size(reclaim->copy_capacity,
		sizeof(reclaim->copies[0]), &reclaim_copy_bytes))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!plan || !index_valid(index) ||
	    !multiply_size(index->entry_capacity, sizeof(index->entries[0]),
		&entry_bytes) ||
	    (replaced && !entry_member(index, replaced)) ||
	    buffers_overlap(plan, sizeof(*plan), index, sizeof(*index)) ||
	    buffers_overlap(plan, sizeof(*plan), index->store, index->store_size) ||
	    buffers_overlap(plan, sizeof(*plan), index->entries, entry_bytes) ||
	    (reclaim && buffers_overlap(reclaim, sizeof(*reclaim), plan,
		sizeof(*plan))) ||
	    (reclaim && buffers_overlap(reclaim->copies, reclaim_copy_bytes, plan,
		sizeof(*plan))) ||
	    (source && (buffers_overlap(plan, sizeof(*plan), source,
		sizeof(*source)) ||
		buffers_overlap(plan, sizeof(*plan), source->name,
			source->name_size) ||
		buffers_overlap(plan, sizeof(*plan), source->data,
			source->data_size) ||
		buffers_overlap(plan, sizeof(*plan), policy, sizeof(*policy)) ||
		buffers_overlap(plan, sizeof(*plan), record, record_capacity))) ||
	    !find_prior_transition(index, replaced, &prior_transition))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(plan, 0, sizeof(*plan));
	if (!source) {
		if (!replaced)
			return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
		if (!(replaced->attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE) ||
		    (replaced->attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR))
			return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		if (at_runtime &&
		    (replaced->attributes & (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) !=
		    (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		draft.action = PAYLOAD_MM_AUTHVAR_WRITE_DELETE;
		if (prior_transition != UINT32_MAX &&
		    !add_state(&draft, prior_transition,
			PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION,
			PAYLOAD_MM_AUTHVAR_STATE_TRANSITION_DELETED))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		old_state = index->store[replaced->record_offset + 2U];
		if (!add_state(&draft, replaced->record_offset, old_state,
			old_state & PAYLOAD_MM_AUTHVAR_STATE_DELETED))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		*plan = draft;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	if ((source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE) ||
	    (source->attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED) ||
	    (source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR) ||
	    !(source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE))
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (!policy || policy->maximum_name_size < 4U ||
	    (policy->maximum_name_size & 1U) ||
	    policy->maximum_records == 0 ||
	    policy->maximum_name_size > index->maximum_name_size ||
	    policy->maximum_data_size > index->maximum_data_size ||
	    policy->maximum_records > index->maximum_records ||
	    policy->maximum_record_size <
		PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE || !policy->maximum_data_size ||
	    policy->maximum_record_size > index->store_size ||
	    policy->maximum_data_size > policy->maximum_record_size ||
	    !source_valid(source, at_runtime) ||
	    (replaced && !key_matches(index, replaced, source)) ||
	    (replaced && (source->attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) !=
		replaced->attributes) ||
	    buffers_overlap(record, record_capacity, index->store,
		index->store_size) ||
	    buffers_overlap(record, record_capacity, index, sizeof(*index)) ||
	    buffers_overlap(record, record_capacity, index->entries, entry_bytes) ||
	    buffers_overlap(record, record_capacity, source, sizeof(*source)) ||
	    buffers_overlap(record, record_capacity, source->name,
		source->name_size) ||
	    buffers_overlap(record, record_capacity, source->data,
		source->data_size) ||
	    buffers_overlap(record, record_capacity, policy, sizeof(*policy)) ||
	    buffers_overlap(record, record_capacity, plan, sizeof(*plan)) ||
	    (reclaim && buffers_overlap(record, record_capacity, reclaim,
		sizeof(*reclaim))))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (source->name_size > policy->maximum_name_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!source->data_size &&
	    (source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE)) {
		plan->action = PAYLOAD_MM_AUTHVAR_WRITE_NOOP;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	if (!source->data_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	candidate_data_size = source->data_size;
	if ((source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) && replaced) {
		if (candidate_data_size > SIZE_MAX - replaced->data_size)
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		candidate_data_size += replaced->data_size;
	}
	if (candidate_data_size > UINT32_MAX)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!record_size(source, candidate_data_size, &required_size, &data_offset))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	plan->record_size = (uint32_t)required_size;
	if (candidate_data_size > policy->maximum_data_size ||
	    required_size > policy->maximum_record_size) {
		plan->record_size = required_size <= UINT32_MAX ?
			(uint32_t)required_size : UINT32_MAX;
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}
	if (replaced && !(source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) &&
	    !(source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) &&
	    replaced->data_size == source->data_size &&
	    payload_mm_authvar_store_data(index, replaced) &&
	    !memcmp(payload_mm_authvar_store_data(index, replaced), source->data,
		source->data_size)) {
		plan->action = PAYLOAD_MM_AUTHVAR_WRITE_NOOP;
		plan->record_size = 0;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	if (!record || record_capacity < required_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL;
	candidate = (struct payload_mm_authvar_new_record) {
		.name_size = (uint32_t)source->name_size,
		.data_size = (uint32_t)candidate_data_size,
	};
	if (!replaced && (index->entry_count >= policy->maximum_records ||
	    index->entry_count >= index->entry_capacity)) {
		plan->record_size = 0;
		plan->action = PAYLOAD_MM_AUTHVAR_WRITE_OUT_OF_RESOURCES;
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	}
	force_reclaim = index->record_count >= policy->maximum_records;
	if (force_reclaim && at_runtime) {
		plan->record_size = 0;
		plan->action = PAYLOAD_MM_AUTHVAR_WRITE_OUT_OF_RESOURCES;
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	}
	if (!reclaim)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (buffers_overlap(reclaim, sizeof(*reclaim), index->store,
		index->store_size) ||
	    buffers_overlap(reclaim, sizeof(*reclaim), index, sizeof(*index)) ||
	    buffers_overlap(reclaim, sizeof(*reclaim), index->entries, entry_bytes) ||
	    buffers_overlap(reclaim, sizeof(*reclaim), source, sizeof(*source)) ||
	    buffers_overlap(reclaim, sizeof(*reclaim), source->name,
		source->name_size) ||
	    buffers_overlap(reclaim, sizeof(*reclaim), source->data,
		source->data_size) ||
	    buffers_overlap(reclaim, sizeof(*reclaim), policy, sizeof(*policy)) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, index->store,
		index->store_size) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, index,
		sizeof(*index)) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, index->entries,
		entry_bytes) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, reclaim,
		sizeof(*reclaim)) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, source,
		sizeof(*source)) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, source->name,
		source->name_size) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, source->data,
		source->data_size) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, policy,
		sizeof(*policy)) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, plan,
		sizeof(*plan)) ||
	    buffers_overlap(reclaim->copies, reclaim_copy_bytes, record,
		record_capacity) ||
	    payload_mm_authvar_store_reclaim_plan_forced(index, replaced,
		&candidate, at_runtime, force_reclaim, reclaim) != CB_SUCCESS)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (reclaim->action == PAYLOAD_MM_AUTHVAR_SPACE_RECLAIM &&
	    (reclaim->copy_count >= policy->maximum_records ||
	     reclaim->copy_count >= index->entry_capacity))
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	if (reclaim->action == PAYLOAD_MM_AUTHVAR_SPACE_OUT_OF_RESOURCES) {
		plan->action = PAYLOAD_MM_AUTHVAR_WRITE_OUT_OF_RESOURCES;
		plan->record_size = 0;
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	}
	if (!build_record(index, replaced, source, record, record_capacity,
		&draft.record_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	draft.record_offset = reclaim->destination_offset;
	if (reclaim->action == PAYLOAD_MM_AUTHVAR_SPACE_RECLAIM) {
		draft.action = PAYLOAD_MM_AUTHVAR_WRITE_RECLAIM;
		*plan = draft;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	draft.action = PAYLOAD_MM_AUTHVAR_WRITE_TAIL_APPEND;
	if (prior_transition != UINT32_MAX &&
	    !add_state(&draft, prior_transition,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION,
		PAYLOAD_MM_AUTHVAR_STATE_TRANSITION_DELETED))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (replaced) {
		old_state = index->store[replaced->record_offset + 2U];
		if (old_state == PAYLOAD_MM_AUTHVAR_STATE_ADDED &&
		    !add_state(&draft, replaced->record_offset, old_state,
			old_state & PAYLOAD_MM_AUTHVAR_STATE_IN_DELETED_TRANSITION))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}
	if (!add_step(&draft, PAYLOAD_MM_AUTHVAR_WRITE_RECORD, draft.record_offset,
		draft.record_size, PAYLOAD_MM_AUTHVAR_STATE_ERASED,
		PAYLOAD_MM_AUTHVAR_STATE_ERASED) ||
	    !add_state(&draft, draft.record_offset, PAYLOAD_MM_AUTHVAR_STATE_ERASED,
		PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY) ||
	    !add_state(&draft, draft.record_offset,
		PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY,
		PAYLOAD_MM_AUTHVAR_STATE_ADDED))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (replaced) {
		old_state = index->store[replaced->record_offset + 2U];
		if (!add_state(&draft, replaced->record_offset,
			old_state == PAYLOAD_MM_AUTHVAR_STATE_ADDED ?
				PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION :
				old_state,
			old_state & PAYLOAD_MM_AUTHVAR_STATE_IN_DELETED_TRANSITION &
				PAYLOAD_MM_AUTHVAR_STATE_DELETED))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}
	*plan = draft;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
