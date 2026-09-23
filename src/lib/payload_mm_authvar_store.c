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
		index->store = NULL;
		index->store_size = 0;
		index->used_size = 0;
		index->dirty_tail_offset = 0;
		index->record_count = 0;
		index->entry_count = 0;
		index->maximum_name_size = 0;
		index->maximum_data_size = 0;
		index->maximum_records = 0;
	}
	if (!index || !store || !limits_valid(limits) || !index->entries ||
	    !index->entry_capacity || buffer_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    buffer_size > limits->maximum_store_size ||
	    memcmp(bytes, authenticated_store_guid, sizeof(authenticated_store_guid)) ||
	    bytes[20] != VARIABLE_STORE_FORMATTED || bytes[21] != VARIABLE_STORE_HEALTHY ||
	    read_le16(bytes + 22) || read_le32(bytes + 24))
		return CB_ERR;
	store_size = read_le32(bytes + 16);
	if (store_size < PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE ||
	    store_size > buffer_size || store_size > limits->maximum_store_size)
		return CB_ERR;
	offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	while (offset < store_size) {
		const uint8_t *header = bytes + offset;
		size_t name_offset;
		size_t data_offset;
		size_t record_end;
		size_t next;
		uint32_t attributes;
		uint32_t name_size;
		uint32_t data_size;
		uint8_t state;
		bool visible;

		if (bytes_are(header, store_size - offset, 0xff))
			break;
		if (store_size - offset < PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE ||
		    read_le16(header) != PAYLOAD_MM_AUTHVAR_RECORD_START_ID || header[3]) {
			if (store_size - offset > 2U && header[2] == 0xff) {
				index->dirty_tail_offset = (uint32_t)offset;
				offset = store_size;
				break;
			}
			return CB_ERR;
		}
		state = header[2];
		if (state != PAYLOAD_MM_AUTHVAR_STATE_ERASED && !state_valid(state))
			return CB_ERR;
		visible = state == PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
			state == PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION;
		attributes = read_le32(header + 4);
		name_size = read_le32(header + 36);
		data_size = read_le32(header + 40);
		if (!attributes || attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED ||
		    attributes & PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE ||
		    attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE ||
		    ((attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS) &&
		     !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS)) ||
		    ((visible || state == PAYLOAD_MM_AUTHVAR_STATE_ERASED) && !data_size) ||
		    ((visible || state == PAYLOAD_MM_AUTHVAR_STATE_ERASED) &&
		     !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS)) ||
		    ((attributes & PAYLOAD_MM_AUTHVAR_ATTR_HARDWARE_ERROR) &&
		     (attributes & (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				    PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
				    PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) !=
			    (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
				     PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
				     PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)) ||
		    name_size > limits->maximum_name_size ||
		    data_size > limits->maximum_data_size ||
		    !timestamp_valid(header + 16, attributes) ||
		    read_le32(header + 32) ||
		    (read_le32(header + 12) || read_le32(header + 8)))
			goto malformed_record;
		if (!add_size(offset, PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, &name_offset) ||
		    !add_size(name_offset, name_size, &data_offset) ||
		    !align4(data_offset, &data_offset) ||
		    !add_size(data_offset, data_size, &record_end) ||
		    !align4(record_end, &next) || next > store_size ||
		    !name_valid(bytes + name_offset, name_size) ||
		    !bytes_are(bytes + name_offset + name_size,
			data_offset - name_offset - name_size, 0xff) ||
		    !bytes_are(bytes + record_end, next - record_end, 0xff))
			goto malformed_record;
		/* ERASED is an uncommitted body and cannot authorize traversal. */
		if (state == PAYLOAD_MM_AUTHVAR_STATE_ERASED) {
			index->dirty_tail_offset = (uint32_t)offset;
			offset = store_size;
			break;
		}
		records++;
		if (records > limits->maximum_records)
			return CB_ERR;
		if (visible) {
			struct payload_mm_authvar_store_entry *entry;
			uint32_t duplicate = entry_count;

			for (uint32_t i = 0; i < entry_count; i++) {
				if (entry_key_equal(bytes, &index->entries[i], header + 44,
					bytes + name_offset, name_size)) {
					duplicate = i;
					break;
				}
			}
			if (duplicate < entry_count) {
				const uint8_t previous_state = bytes[
					index->entries[duplicate].record_offset + 2U];

				if (state != PAYLOAD_MM_AUTHVAR_STATE_ADDED ||
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
			*entry = (struct payload_mm_authvar_store_entry) {
				.record_offset = (uint32_t)offset,
				.name_offset = (uint32_t)name_offset,
				.name_size = name_size,
				.data_offset = (uint32_t)data_offset,
				.data_size = data_size,
				.attributes = attributes,
			};
			memcpy(entry->vendor_guid, header + 44, 16);
		}
		offset = next;
		continue;

malformed_record:
		if (state != PAYLOAD_MM_AUTHVAR_STATE_ERASED)
			return CB_ERR;
		index->dirty_tail_offset = (uint32_t)offset;
		offset = store_size;
		break;
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
