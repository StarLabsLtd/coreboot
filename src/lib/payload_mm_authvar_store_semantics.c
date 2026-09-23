/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store_semantics.h>
#include <string.h>

static bool add_u32(uint32_t left, uint32_t right, uint32_t *result)
{
	if (right > UINT32_MAX - left)
		return false;
	*result = left + right;
	return true;
}

static uint32_t read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
		(uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool align4_u32(uint32_t value, uint32_t *result)
{
	if (value > UINT32_MAX - 3U)
		return false;
	*result = (value + 3U) & ~3U;
	return true;
}

static bool name_valid(const uint8_t *name, size_t size)
{
	if (!name || size < 2U || size > UINT32_MAX || (size & 1U) ||
	    name[size - 2U] || name[size - 1U])
		return false;
	for (size_t offset = 0; offset + 2U < size; offset += 2U) {
		if (!name[offset] && !name[offset + 1U])
			return false;
	}
	return true;
}

static bool bytes_zero(const uint8_t *bytes, size_t size)
{
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
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

static bool entry_visible(const struct payload_mm_authvar_store_entry *entry,
	bool at_runtime)
{
	return !at_runtime ||
		(entry->attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
}

static const struct payload_mm_authvar_store_entry *first_entry(
	const struct payload_mm_authvar_store_index *index, bool at_runtime)
{
	const struct payload_mm_authvar_store_entry *transition = NULL;

	/*
	 * EDK2 FindVariableEx() retains the last matching transition while it
	 * walks, but returns immediately on the first added record.
	 */
	for (uint32_t i = 0; i < index->entry_count; i++) {
		const struct payload_mm_authvar_store_entry *entry = &index->entries[i];

		if (!entry_visible(entry, at_runtime))
			continue;
		if (entry->record_offset > index->store_size - 3U)
			return NULL;
		if (index->store[entry->record_offset + 2U] ==
		    PAYLOAD_MM_AUTHVAR_STATE_ADDED)
			return entry;
		transition = entry;
	}
	return transition;
}

static bool entry_size(const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry, uint32_t *size)
{
	uint32_t end;

	if (!entry_member(index, entry) ||
	    !add_u32(entry->data_offset, entry->data_size, &end) ||
	    !align4_u32(end, &end) || end > index->used_size ||
	    end < entry->record_offset)
		return false;
	*size = end - entry->record_offset;
	return *size >= PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;
}

static bool physical_record(const struct payload_mm_authvar_store_index *index,
	uint32_t offset, uint32_t *attributes, uint32_t *size)
{
	uint32_t name_end;
	uint32_t data_offset;
	uint32_t data_end;
	uint32_t next;
	const uint8_t *header;

	if (!index_valid(index) || offset > index->used_size ||
	    index->used_size - offset < PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE)
		return false;
	header = index->store + offset;
	if (!add_u32(offset, PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, &name_end) ||
	    !add_u32(name_end, read_le32(header + 36U), &name_end) ||
	    !align4_u32(name_end, &data_offset) ||
	    !add_u32(data_offset, read_le32(header + 40U), &data_end) ||
	    !align4_u32(data_end, &next) || next > index->used_size)
		return false;
	*attributes = read_le32(header + 4U);
	*size = next - offset;
	return *size >= PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;
}

static bool new_record_size(const struct payload_mm_authvar_new_record *record,
	uint32_t *size)
{
	uint32_t name_end;
	uint32_t data_end;

	if (!record) {
		*size = 0;
		return true;
	}
	if (record->name_size < 4U || (record->name_size & 1U) ||
	    !record->data_size ||
	    !add_u32(PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, record->name_size,
		&name_end) || !align4_u32(name_end, &name_end) ||
	    !add_u32(name_end, record->data_size, &data_end) ||
	    !align4_u32(data_end, size))
		return false;
	return true;
}

uint64_t payload_mm_authvar_store_get(
	const struct payload_mm_authvar_store_index *index,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	uint32_t data_capacity, bool at_runtime,
	struct payload_mm_authvar_get_result *result)
{
	const struct payload_mm_authvar_store_entry *entry;
	const uint8_t *name_bytes = name;

	if (result)
		memset(result, 0, sizeof(*result));
	if (!index_valid(index) || !vendor_guid || !result ||
	    !name_valid(name_bytes, name_size))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (name_size == 2U)
		return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	entry = payload_mm_authvar_store_find(index, vendor_guid, name, name_size);
	if (!entry || !entry_visible(entry, at_runtime))
		return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	if (!entry->data_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	result->required_data_size = entry->data_size;
	result->attributes = entry->attributes;
	if (data_capacity < entry->data_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL;
	result->entry = entry;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t payload_mm_authvar_store_get_next(
	const struct payload_mm_authvar_store_index *index,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	uint32_t name_capacity, bool at_runtime,
	struct payload_mm_authvar_next_result *result)
{
	const struct payload_mm_authvar_store_entry *current = NULL;
	const struct payload_mm_authvar_store_entry *next = NULL;
	uint32_t start = 0;

	if (result)
		memset(result, 0, sizeof(*result));
	if (!index_valid(index) || !vendor_guid || !result ||
	    name_capacity < sizeof(uint16_t) || name_size > name_capacity ||
	    (name_size && !name_valid(name, name_size)) ||
	    (!name_size && !bytes_zero(vendor_guid, 16)))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (name_size) {
		current = payload_mm_authvar_store_find(index, vendor_guid, name,
			name_size);
		if (!current || !entry_visible(current, at_runtime))
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		start = (uint32_t)(current - index->entries) + 1U;
	}
	if (!name_size) {
		next = first_entry(index, at_runtime);
	} else {
		for (uint32_t i = start; i < index->entry_count; i++) {
			if (entry_visible(&index->entries[i], at_runtime)) {
				next = &index->entries[i];
				break;
			}
		}
	}
	if (!next)
		return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	result->required_name_size = next->name_size;
	if (name_capacity < next->name_size)
		return PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL;
	result->entry = next;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t payload_mm_authvar_store_query(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_policy *policy, uint32_t attributes,
	bool at_runtime, struct payload_mm_authvar_query_result *result)
{
	uint64_t used = 0;
	uint64_t remaining;
	uint64_t maximum_variable;

	if (result)
		memset(result, 0, sizeof(*result));
	if (!index_valid(index) || !policy || !result ||
	    !policy->maximum_storage ||
	    policy->maximum_storage >
		index->store_size - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    policy->maximum_record_size < PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!attributes)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (attributes & PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE)
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED))
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if ((attributes & PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED) ==
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE ||
	    ((attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS) &&
	     !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS)) ||
	    (at_runtime && !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) ||
	    ((attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR) &&
	     !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE)))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE) ||
	    (attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR))
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	/* Recovery must canonicalize a torn append before quota is authoritative. */
	if (index->dirty_tail_offset)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (at_runtime) {
		uint32_t offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

		while (offset < index->used_size) {
			uint32_t record_attributes;
			uint32_t size;

			if (!physical_record(index, offset, &record_attributes, &size))
				return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
			if (!(record_attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR))
				used += size;
			offset += size;
		}
	} else {
		for (uint32_t i = 0; i < index->entry_count; i++) {
			uint32_t size;

			if (index->entries[i].attributes &
			    PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR)
				continue;
			if (!entry_size(index, &index->entries[i], &size))
				return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
			used += size;
		}
	}
	remaining = used < policy->maximum_storage ?
		policy->maximum_storage - used : 0;
	maximum_variable = policy->maximum_record_size -
		PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;
	if (remaining <= PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE)
		maximum_variable = 0;
	else if (maximum_variable > remaining - PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE)
		maximum_variable = remaining - PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;
	result->maximum_storage = policy->maximum_storage;
	result->remaining_storage = remaining;
	result->maximum_variable = maximum_variable;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static const struct payload_mm_authvar_store_entry *next_physical_entry(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *replaced, uint8_t state,
	uint32_t after)
{
	const struct payload_mm_authvar_store_entry *next = NULL;

	for (uint32_t i = 0; i < index->entry_count; i++) {
		const struct payload_mm_authvar_store_entry *entry = &index->entries[i];

		if (entry == replaced || entry->record_offset < after ||
		    index->store[entry->record_offset + 2U] != state ||
		    (next && entry->record_offset >= next->record_offset))
			continue;
		next = entry;
	}
	return next;
}

enum cb_err payload_mm_authvar_store_reclaim_plan_forced(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *replaced,
	const struct payload_mm_authvar_new_record *new_record, bool at_runtime,
	bool force_reclaim, struct payload_mm_authvar_reclaim_plan *plan)
{
	struct payload_mm_authvar_reclaim_copy *copies;
	uint32_t copy_capacity;
	uint32_t destination = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	uint32_t record_size;
	uint32_t copy_count = 0;
	uint32_t append_end;

	if (!plan)
		return CB_ERR;
	copies = plan->copies;
	copy_capacity = plan->copy_capacity;
	memset(plan, 0, sizeof(*plan));
	plan->copies = copies;
	plan->copy_capacity = copy_capacity;
	if (!index_valid(index) || (replaced && !entry_member(index, replaced)) ||
	    !new_record_size(new_record, &record_size))
		return CB_ERR;
	plan->record_size = record_size;
	if (!force_reclaim && (!record_size ||
	    (add_u32(index->used_size, record_size, &append_end) &&
	    append_end <= index->store_size))) {
		plan->action = PAYLOAD_MM_AUTHVAR_SPACE_APPEND;
		plan->destination_offset = index->used_size;
		return CB_SUCCESS;
	}
	if (at_runtime) {
		plan->action = PAYLOAD_MM_AUTHVAR_SPACE_OUT_OF_RESOURCES;
		return CB_SUCCESS;
	}
	if (copy_capacity < index->entry_count - (replaced ? 1U : 0U) ||
	    (copy_capacity && !copies))
		return CB_ERR;
	for (uint32_t pass = 0; pass < 2U; pass++) {
		const uint8_t state = pass ?
			PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION :
			PAYLOAD_MM_AUTHVAR_STATE_ADDED;
		uint32_t after = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

		while (true) {
			const struct payload_mm_authvar_store_entry *entry;
			uint32_t size;

			entry = next_physical_entry(index, replaced, state, after);
			if (!entry)
				break;
			if (!entry_size(index, entry, &size) ||
			    !add_u32(destination, size, &destination))
				return CB_ERR;
			copies[copy_count++] = (struct payload_mm_authvar_reclaim_copy) {
				.source_offset = entry->record_offset,
				.destination_offset = destination - size,
				.size = size,
				.promote_transition = pass != 0,
			};
			after = entry->record_offset + 1U;
		}
	}
	if (destination > index->used_size)
		return CB_ERR;
	plan->compacted_used_size = destination;
	plan->reclaimable_size = index->used_size - destination;
	plan->copy_count = copy_count;
	if (add_u32(destination, record_size, &plan->destination_offset) &&
		 plan->destination_offset <= index->store_size) {
		plan->action = PAYLOAD_MM_AUTHVAR_SPACE_RECLAIM;
		plan->destination_offset = destination;
	} else {
		plan->action = PAYLOAD_MM_AUTHVAR_SPACE_OUT_OF_RESOURCES;
		plan->destination_offset = 0;
	}
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_store_reclaim_plan(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *replaced,
	const struct payload_mm_authvar_new_record *new_record, bool at_runtime,
	struct payload_mm_authvar_reclaim_plan *plan)
{
	return payload_mm_authvar_store_reclaim_plan_forced(index, replaced,
		new_record, at_runtime, false, plan);
}
