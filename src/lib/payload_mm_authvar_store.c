/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <string.h>

#define VARIABLE_STORE_FORMATTED 0x5aU
#define VARIABLE_STORE_HEALTHY 0xfeU

static const uint8_t authenticated_store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
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

static bool bytes_are(const uint8_t *p, size_t size, uint8_t value)
{
	while (size) {
		if (*p++ != value)
			return false;
		size--;
	}
	return true;
}

static bool add_size(size_t left, size_t right, size_t *result)
{
	if (right > SIZE_MAX - left)
		return false;
	*result = left + right;
	return true;
}

static bool align4(size_t value, size_t *result)
{
	if (value > SIZE_MAX - 3U)
		return false;
	*result = (value + 3U) & ~(size_t)3U;
	return true;
}

static bool name_valid(const uint8_t *name, uint32_t size)
{
	if (size < 2U * sizeof(uint16_t) || (size & 1U) ||
	    name[size - 2U] || name[size - 1U])
		return false;
	for (uint32_t i = 0; i + sizeof(uint16_t) < size; i += sizeof(uint16_t)) {
		if (!name[i] && !name[i + 1U])
			return false;
	}
	return true;
}

static bool state_valid(uint8_t state)
{
	return state == PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
		state == PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY ||
		state == PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED ||
		state == PAYLOAD_MM_AUTHVAR_STATE_TRANSITION_DELETED ||
		state == PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION;
}

static bool timestamp_valid(const uint8_t timestamp[16], uint32_t attributes)
{
	static const uint8_t days_per_month[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
	};
	const bool time_authenticated = attributes &
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	uint16_t year;
	uint8_t maximum_day;

	if (!time_authenticated)
		return bytes_are(timestamp, 16, 0);
	/*
	 * EDK2 initializes some time-authenticated variables without a signed
	 * update, so their stored timestamp is the all-zero EFI_TIME sentinel.
	 */
	if (bytes_are(timestamp, 16, 0))
		return true;
	year = read_le16(timestamp);
	if (year < 1900 || year > 9999 || timestamp[2] < 1 || timestamp[2] > 12)
		return false;
	maximum_day = days_per_month[timestamp[2] - 1U];
	if (timestamp[2] == 2 && (!(year % 4) && ((year % 100) || !(year % 400))))
		maximum_day++;
	if (timestamp[3] < 1 || timestamp[3] > maximum_day || timestamp[4] > 23 ||
	    timestamp[5] > 59 || timestamp[6] > 59 || timestamp[7] ||
	    read_le32(timestamp + 8) || read_le16(timestamp + 12) ||
	    timestamp[14] || timestamp[15])
		return false;
	return true;
}

static bool entry_key_equal(const uint8_t *store,
	const struct payload_mm_authvar_store_entry *entry, const uint8_t guid[16],
	const uint8_t *name, uint32_t name_size)
{
	return entry->name_size == name_size &&
		!memcmp(entry->vendor_guid, guid, 16) &&
		!memcmp(store + entry->name_offset, name, name_size);
}

static bool limits_valid(const struct payload_mm_authvar_store_limits *limits)
{
	return limits && limits->maximum_store_size >=
		PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE && limits->maximum_name_size >= 4U &&
		!(limits->maximum_name_size & 1U) && limits->maximum_data_size &&
		limits->maximum_records;
}

static bool entry_member(const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry)
{
	uintptr_t base;
	uintptr_t address;
	size_t bytes;

	if (!index || !index->entries || !entry ||
	    (index->entry_count && sizeof(index->entries[0]) >
	     SIZE_MAX / index->entry_count))
		return false;
	base = (uintptr_t)index->entries;
	address = (uintptr_t)entry;
	bytes = index->entry_count * sizeof(index->entries[0]);
	return address >= base && address - base < bytes &&
		!((address - base) % sizeof(index->entries[0]));
}

static bool entry_span_valid(const struct payload_mm_authvar_store_index *index,
	uint32_t offset, uint32_t size)
{
	return offset <= index->store_size && size <= index->store_size - offset;
}

static bool range_valid(const void *data, size_t size)
{
	return size == 0U ||
		(data && (uintptr_t)data <= UINTPTR_MAX - size);
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	const uintptr_t left_address = (uintptr_t)left;
	const uintptr_t right_address = (uintptr_t)right;

	return left_size && right_size && left_address < right_address + right_size &&
		right_address < left_address + left_size;
}

bool payload_mm_authvar_store_index_valid(
	const struct payload_mm_authvar_store_index *index)
{
	size_t entry_bytes;

	if (!index || (uintptr_t)index % _Alignof(*index) ||
	    !range_valid(index, sizeof(*index)) || !index->store || !index->entries ||
	    (uintptr_t)index->entries % _Alignof(index->entries[0]) ||
	    index->store_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    index->store_size > PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_SIZE ||
	    index->used_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    index->used_size > index->store_size ||
	    index->record_count > index->maximum_records ||
	    index->entry_count > index->entry_capacity ||
	    index->entry_count > index->record_count ||
	    index->entry_count > index->maximum_records ||
	    index->maximum_name_size < 2U * sizeof(uint16_t) ||
	    index->maximum_name_size >
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE ||
	    (index->maximum_name_size & 1U) || !index->maximum_data_size ||
	    index->maximum_data_size >
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE ||
	    !index->maximum_records || index->maximum_records >
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS ||
	    __builtin_mul_overflow((size_t)index->entry_count,
		sizeof(index->entries[0]), &entry_bytes) ||
	    !range_valid(index->store, index->store_size) ||
	    !range_valid(index->entries, entry_bytes))
		return false;
	for (uint32_t i = 0U; i < index->entry_count; i++) {
		const struct payload_mm_authvar_store_entry *entry = &index->entries[i];
		const uint8_t *header;
		size_t data_offset;

		if (!entry_span_valid(index, entry->record_offset,
			PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE) ||
		    entry->record_offset >= index->used_size ||
		    PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE >
			index->used_size - entry->record_offset ||
		    entry->name_offset != entry->record_offset +
			PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE ||
		    !entry_span_valid(index, entry->name_offset, entry->name_size) ||
		    !entry_span_valid(index, entry->data_offset, entry->data_size) ||
		    entry->name_offset > index->used_size ||
		    entry->name_size > index->used_size - entry->name_offset ||
		    entry->data_offset > index->used_size ||
		    entry->data_size > index->used_size - entry->data_offset ||
		    entry->name_size > index->maximum_name_size ||
		    entry->data_size > index->maximum_data_size)
			return false;
		header = index->store + entry->record_offset;
		data_offset = (size_t)entry->name_offset + entry->name_size;
		if (!align4(data_offset, &data_offset) ||
		    data_offset != entry->data_offset ||
		    read_le16(header) != PAYLOAD_MM_AUTHVAR_RECORD_START_ID ||
		    header[3] ||
		    (header[2] != PAYLOAD_MM_AUTHVAR_STATE_ADDED &&
		     header[2] !=
			PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION) ||
		    read_le32(header + 4U) != entry->attributes ||
		    read_le32(header + 36U) != entry->name_size ||
		    read_le32(header + 40U) != entry->data_size ||
		    memcmp(header + 44U, entry->vendor_guid,
			sizeof(entry->vendor_guid)) ||
		    !name_valid(index->store + entry->name_offset, entry->name_size))
			return false;
	}
	return true;
}

enum record_result {
	RECORD_VALID,
	RECORD_END,
	RECORD_DIRTY_TAIL,
	RECORD_INVALID,
};

struct record_view {
	size_t offset;
	size_t name_offset;
	size_t data_offset;
	size_t next;
	uint32_t attributes;
	uint32_t name_size;
	uint32_t data_size;
	uint8_t state;
	bool visible;
};

static enum cb_err store_size_valid(const uint8_t *store, size_t buffer_size,
	const struct payload_mm_authvar_store_limits *limits, size_t *store_size)
{
	if (!store || !limits_valid(limits) ||
	    buffer_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    buffer_size > limits->maximum_store_size ||
	    memcmp(store, authenticated_store_guid, sizeof(authenticated_store_guid)) ||
	    store[20] != VARIABLE_STORE_FORMATTED || store[21] != VARIABLE_STORE_HEALTHY ||
	    read_le16(store + 22U) || read_le32(store + 24U))
		return CB_ERR;
	*store_size = read_le32(store + 16U);
	if (*store_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    *store_size > buffer_size || *store_size > limits->maximum_store_size)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum record_result decode_record(const uint8_t *store, size_t store_size,
	size_t offset, const struct payload_mm_authvar_store_limits *limits,
	struct record_view *record)
{
	const uint8_t *header = store + offset;
	size_t record_end;

	memset(record, 0, sizeof(*record));
	if (bytes_are(header, store_size - offset, 0xff))
		return RECORD_END;
	if (store_size - offset < PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE ||
	    read_le16(header) != PAYLOAD_MM_AUTHVAR_RECORD_START_ID || header[3])
		return store_size - offset > 2U && header[2] == 0xff ?
			RECORD_DIRTY_TAIL : RECORD_INVALID;
	record->state = header[2];
	if (record->state != PAYLOAD_MM_AUTHVAR_STATE_ERASED &&
	    !state_valid(record->state))
		return RECORD_INVALID;
	record->offset = offset;
	record->visible = record->state == PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
		record->state == PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION;
	record->attributes = read_le32(header + 4U);
	record->name_size = read_le32(header + 36U);
	record->data_size = read_le32(header + 40U);
	if (!record->attributes ||
	    record->attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED ||
	    record->attributes & PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE ||
	    record->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE ||
	    ((record->attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS) &&
	     !(record->attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS)) ||
	    ((record->visible || record->state == PAYLOAD_MM_AUTHVAR_STATE_ERASED) &&
	     !record->data_size) ||
	    ((record->visible || record->state == PAYLOAD_MM_AUTHVAR_STATE_ERASED) &&
	     !(record->attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS)) ||
	    ((record->attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR) &&
	     (record->attributes & (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				   PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
				   PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) !=
			   (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				    PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
				    PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) ||
	    record->name_size > limits->maximum_name_size ||
	    record->data_size > limits->maximum_data_size ||
	    !timestamp_valid(header + 16U, record->attributes) ||
	    read_le32(header + 32U) || read_le32(header + 12U) || read_le32(header + 8U) ||
	    !add_size(offset, PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE,
		&record->name_offset) ||
	    !add_size(record->name_offset, record->name_size, &record->data_offset) ||
	    !align4(record->data_offset, &record->data_offset) ||
	    !add_size(record->data_offset, record->data_size, &record_end) ||
	    !align4(record_end, &record->next) || record->next > store_size ||
	    !name_valid(store + record->name_offset, record->name_size) ||
	    !bytes_are(store + record->name_offset + record->name_size,
		record->data_offset - record->name_offset - record->name_size, 0xff) ||
	    !bytes_are(store + record_end, record->next - record_end, 0xff))
		return record->state == PAYLOAD_MM_AUTHVAR_STATE_ERASED ?
			RECORD_DIRTY_TAIL : RECORD_INVALID;
	/* ERASED is an uncommitted body and cannot authorize traversal. */
	if (record->state == PAYLOAD_MM_AUTHVAR_STATE_ERASED)
		return RECORD_DIRTY_TAIL;
	return RECORD_VALID;
}

static void copy_record(struct payload_mm_authvar_store_entry *entry,
	const uint8_t *store, const struct record_view *record)
{
	*entry = (struct payload_mm_authvar_store_entry) {
		.record_offset = (uint32_t)record->offset,
		.name_offset = (uint32_t)record->name_offset,
		.name_size = record->name_size,
		.data_offset = (uint32_t)record->data_offset,
		.data_size = record->data_size,
		.attributes = record->attributes,
	};
	memcpy(entry->vendor_guid, store + record->offset + 44U, 16U);
}

enum cb_err payload_mm_authvar_store_scan(
	struct payload_mm_authvar_store_index *index, const void *store,
	size_t buffer_size, const struct payload_mm_authvar_store_limits *limits)
{
	const uint8_t *bytes = store;
	size_t store_size;
	size_t offset;
	uint32_t records = 0;
	uint32_t entry_count = 0;

	if (index) {
		struct payload_mm_authvar_store_entry *entries = index->entries;
		uint32_t capacity = index->entry_capacity;

		*index = (struct payload_mm_authvar_store_index) {
			.entries = entries,
			.entry_capacity = capacity,
		};
	}
	if (!index || !index->entries || !index->entry_capacity ||
	    store_size_valid(bytes, buffer_size, limits, &store_size) != CB_SUCCESS)
		return CB_ERR;
	offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	while (offset < store_size) {
		struct record_view record;
		const enum record_result result = decode_record(bytes, store_size, offset,
			limits, &record);

		if (result == RECORD_END)
			break;
		if (result == RECORD_DIRTY_TAIL) {
			index->dirty_tail_offset = (uint32_t)offset;
			offset = store_size;
			break;
		}
		if (result != RECORD_VALID || ++records > limits->maximum_records)
			return CB_ERR;
		if (record.visible) {
			struct payload_mm_authvar_store_entry *entry;
			uint32_t duplicate = entry_count;

			for (uint32_t i = 0; i < entry_count; i++) {
				if (entry_key_equal(bytes, &index->entries[i],
					bytes + record.offset + 44U,
					bytes + record.name_offset, record.name_size)) {
					duplicate = i;
					break;
				}
			}
			if (duplicate < entry_count) {
				const uint8_t previous_state =
					bytes[index->entries[duplicate].record_offset + 2U];

				if (record.state != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
				    previous_state !=
					PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION)
					return CB_ERR;
				memmove(&index->entries[duplicate],
					&index->entries[duplicate + 1U],
					(entry_count - duplicate - 1U) *
						sizeof(index->entries[0]));
				entry_count--;
			}
			if (entry_count >= index->entry_capacity)
				return CB_ERR;
			entry = &index->entries[entry_count++];
			copy_record(entry, bytes, &record);
		}
		offset = record.next;
	}
	index->store = bytes;
	index->store_size = (uint32_t)store_size;
	index->used_size = (uint32_t)offset;
	index->record_count = records;
	index->entry_count = entry_count;
	index->maximum_name_size = limits->maximum_name_size;
	index->maximum_data_size = limits->maximum_data_size;
	index->maximum_records = limits->maximum_records;
	return CB_SUCCESS;
}

static enum cb_err previous_visible(const uint8_t *store, size_t limit,
	const struct payload_mm_authvar_store_limits *limits,
	const struct record_view *wanted, bool *found, uint8_t *state)
{
	size_t offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	*found = false;
	*state = 0;
	while (offset < limit) {
		struct record_view record;

		if (decode_record(store, limit, offset, limits, &record) != RECORD_VALID)
			return CB_ERR;
		if (record.visible && record.name_size == wanted->name_size &&
		    !memcmp(store + record.offset + 44U,
			store + wanted->offset + 44U, 16U) &&
		    !memcmp(store + record.name_offset, store + wanted->name_offset,
			record.name_size)) {
			if (*found && (record.state != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
					*state != PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION))
				return CB_ERR;
			*found = true;
			*state = record.state;
		}
		offset = record.next;
	}
	return offset == limit ? CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_authvar_store_find_one(
	struct payload_mm_authvar_store_entry *entry, bool *found,
	const void *store, size_t buffer_size,
	const struct payload_mm_authvar_store_limits *limits,
	const uint8_t vendor_guid[16], const void *name, size_t name_size)
{
	const uint8_t *bytes = store;
	size_t store_size;
	size_t offset;
	uint32_t records = 0;
	const bool entry_valid = entry && !((uintptr_t)entry % _Alignof(*entry)) &&
		range_valid(entry, sizeof(*entry));
	const bool found_valid = found && !((uintptr_t)found % _Alignof(*found)) &&
		range_valid(found, sizeof(*found));
	const bool inputs_valid = range_valid(store, buffer_size) &&
		range_valid(limits, sizeof(*limits)) && range_valid(vendor_guid, 16U) &&
		range_valid(name, name_size);

	if (!entry_valid || !found_valid) {
		if (entry_valid)
			memset(entry, 0, sizeof(*entry));
		if (found_valid)
			*found = false;
		return CB_ERR;
	}
	if (!inputs_valid) {
		memset(entry, 0, sizeof(*entry));
		*found = false;
		return CB_ERR;
	}
	if (ranges_overlap(entry, sizeof(*entry), found,
		sizeof(*found)) || ranges_overlap(entry, sizeof(*entry), store,
		buffer_size) || ranges_overlap(found, sizeof(*found), store, buffer_size) ||
	    ranges_overlap(entry, sizeof(*entry), limits, sizeof(*limits)) ||
	    ranges_overlap(found, sizeof(*found), limits, sizeof(*limits)) ||
	    ranges_overlap(entry, sizeof(*entry), vendor_guid, 16U) ||
	    ranges_overlap(found, sizeof(*found), vendor_guid, 16U) ||
	    ranges_overlap(entry, sizeof(*entry), name, name_size) ||
	    ranges_overlap(found, sizeof(*found), name, name_size))
		return CB_ERR;
	if (entry_valid)
		memset(entry, 0, sizeof(*entry));
	if (found_valid)
		*found = false;
	if (!vendor_guid || !name ||
	    store_size_valid(bytes, buffer_size, limits, &store_size) != CB_SUCCESS)
		return CB_ERR;
	offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	while (offset < store_size) {
		struct record_view record;
		bool predecessor;
		uint8_t predecessor_state;
		const enum record_result result = decode_record(bytes, store_size, offset,
			limits, &record);

		if (result == RECORD_END || result == RECORD_DIRTY_TAIL)
			break;
		if (result != RECORD_VALID || ++records > limits->maximum_records)
			goto error;
		if (record.visible) {
			if (previous_visible(bytes, offset, limits, &record, &predecessor,
				&predecessor_state) != CB_SUCCESS ||
			    (predecessor &&
			     (record.state != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
			      predecessor_state !=
				PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION)))
				goto error;
			if (name_size <= UINT32_MAX && record.name_size == name_size &&
			    !memcmp(bytes + record.offset + 44U, vendor_guid, 16U) &&
			    !memcmp(bytes + record.name_offset, name, name_size)) {
				copy_record(entry, bytes, &record);
				*found = true;
			}
		}
		offset = record.next;
	}
	return CB_SUCCESS;

error:
	memset(entry, 0, sizeof(*entry));
	*found = false;
	return CB_ERR;
}

const struct payload_mm_authvar_store_entry *payload_mm_authvar_store_find(
	const struct payload_mm_authvar_store_index *index,
	const uint8_t vendor_guid[16], const void *name, size_t name_size)
{
	if (!index || !index->store || !vendor_guid || !name || name_size > UINT32_MAX)
		return NULL;
	for (uint32_t i = 0; i < index->entry_count; i++) {
		if (entry_key_equal(index->store, &index->entries[i], vendor_guid, name,
			(uint32_t)name_size))
			return &index->entries[i];
	}
	return NULL;
}

const struct payload_mm_authvar_store_entry *payload_mm_authvar_store_next(
	const struct payload_mm_authvar_store_index *index, size_t *position)
{
	if (!index || !index->store || !position || *position >= index->entry_count)
		return NULL;
	return &index->entries[(*position)++];
}

const void *payload_mm_authvar_store_name(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry)
{
	if (!index || !index->store || !entry_member(index, entry) ||
	    !entry_span_valid(index, entry->name_offset, entry->name_size))
		return NULL;
	return index->store + entry->name_offset;
}

const void *payload_mm_authvar_store_data(
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_store_entry *entry)
{
	if (!index || !index->store || !entry_member(index, entry) ||
	    !entry_span_valid(index, entry->data_offset, entry->data_size))
		return NULL;
	return index->store + entry->data_offset;
}
