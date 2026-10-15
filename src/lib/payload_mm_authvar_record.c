/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_record.h>
#include <boot/payload_mm_authvar_store.h>
#include <string.h>

static void write_le16(u8 *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void write_le32(u8 *p, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static bool bytes_are_zero(const u8 *bytes, size_t size)
{
	u8 combined = 0U;

	while (size--)
		combined |= *bytes++;
	return combined == 0U;
}

static bool range_valid(const void *data, size_t size)
{
	return !size || (data && (uintptr_t)data <= UINTPTR_MAX - size);
}

static bool ranges_overlap(const void *left, size_t left_size,
			   const void *right, size_t right_size)
{
	const uintptr_t left_address = (uintptr_t)left;
	const uintptr_t right_address = (uintptr_t)right;

	if (!range_valid(left, left_size) || !range_valid(right, right_size))
		return true;
	if (!left_size || !right_size)
		return false;
	return left_address <= right_address ?
		right_address - left_address < left_size :
		left_address - right_address < right_size;
}

static bool timestamp_valid(const u8 timestamp[16], uint32_t attributes)
{
	static const u8 days[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
	};
	u16 year;
	u8 maximum_day;

	if (!(attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED))
		return bytes_are_zero(timestamp, 16U);
	if (bytes_are_zero(timestamp, 16U))
		return false;
	year = (uint16_t)timestamp[0] | (uint16_t)timestamp[1] << 8;
	if (year < 1900 || year > 9999 || timestamp[2] < 1U ||
	    timestamp[2] > 12U)
		return false;
	maximum_day = days[timestamp[2] - 1U];
	if (timestamp[2] == 2U &&
	    (!(year % 4U) && ((year % 100U) || !(year % 400U))))
		maximum_day++;
	return timestamp[3] >= 1U && timestamp[3] <= maximum_day &&
		timestamp[4] <= 23U && timestamp[5] <= 59U &&
		timestamp[6] <= 59U && !timestamp[7] &&
		bytes_are_zero(timestamp + 8U, 8U);
}

static bool canonical_name(const void *name, size_t size)
{
	const u8 *bytes = name;

	if (!range_valid(name, size) || size < 2U * sizeof(uint16_t) ||
	    size > UINT32_MAX ||
	    size % sizeof(uint16_t) || bytes[size - 2U] || bytes[size - 1U])
		return false;
	for (size_t offset = 0U; offset + sizeof(uint16_t) < size;
	     offset += sizeof(uint16_t))
		if (!bytes[offset] && !bytes[offset + 1U])
			return false;
	return true;
}

bool payload_mm_authvar_record_layout(size_t name_size, size_t data_size,
				      size_t *record_size, size_t *data_offset)
{
	size_t name_end;
	size_t aligned_name_end;
	size_t data_end;
	size_t aligned_data_end;

	if (!record_size || !data_offset || record_size == data_offset ||
	    (uintptr_t)record_size % _Alignof(*record_size) ||
	    (uintptr_t)data_offset % _Alignof(*data_offset) ||
	    !range_valid(record_size, sizeof(*record_size)) ||
	    !range_valid(data_offset, sizeof(*data_offset)) ||
	    name_size > UINT32_MAX || data_size > UINT32_MAX ||
	    __builtin_add_overflow((size_t)PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE,
				   name_size, &name_end) ||
	    __builtin_add_overflow(name_end, 3U, &aligned_name_end))
		return false;
	aligned_name_end &= ~(size_t)3U;
	if (__builtin_add_overflow(aligned_name_end, data_size, &data_end) ||
	    __builtin_add_overflow(data_end, 3U, &aligned_data_end))
		return false;
	aligned_data_end &= ~(size_t)3U;
	if (aligned_data_end > UINT32_MAX)
		return false;
	*record_size = aligned_data_end;
	*data_offset = aligned_name_end;
	return true;
}

bool payload_mm_authvar_record_encode(
	const struct payload_mm_authvar_record_descriptor *source,
	const struct payload_mm_authvar_record_span *spans, size_t span_count,
	enum payload_mm_authvar_record_timestamp timestamp_mode,
	void *record, size_t capacity, uint32_t *output_size)
{
	u8 *bytes = record;
	size_t data_size = 0U;
	size_t data_offset;
	size_t record_size;
	size_t at;

	if (!source || !spans || !span_count ||
	    span_count > PAYLOAD_MM_AUTHVAR_RECORD_MAX_DATA_SPANS || !bytes ||
	    !output_size || (uintptr_t)source % _Alignof(*source) ||
	    (uintptr_t)spans % _Alignof(*spans) ||
	    (uintptr_t)output_size % _Alignof(*output_size) ||
	    !range_valid(source, sizeof(*source)) ||
	    !range_valid(spans, span_count * sizeof(*spans)) ||
	    !range_valid(output_size, sizeof(*output_size)) ||
	    !canonical_name(source->name, source->name_size) ||
	    !source->attributes ||
	    source->attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED ||
	    source->attributes & (PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE |
		PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) ||
	    !(source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS) ||
	    ((source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS) &&
	     !(source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS)) ||
	    timestamp_mode < PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED ||
	    timestamp_mode > PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO)
		return false;
	for (size_t i = 0U; i < span_count; i++) {
		if (!spans[i].size || !spans[i].data ||
		    __builtin_add_overflow(data_size, spans[i].size, &data_size))
			return false;
	}
	if (timestamp_mode == PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO &&
	    (!(source->attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) ||
	     !bytes_are_zero(source->timestamp, sizeof(source->timestamp))))
		return false;
	if (timestamp_mode == PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED &&
	    !timestamp_valid(source->timestamp, source->attributes))
		return false;
	if (!payload_mm_authvar_record_layout(source->name_size, data_size,
					      &record_size, &data_offset) || capacity < record_size ||
	    !range_valid(record, record_size) ||
	    ranges_overlap(record, record_size, source, sizeof(*source)) ||
	    ranges_overlap(record, record_size, spans,
			   span_count * sizeof(*spans)) ||
	    ranges_overlap(record, record_size, source->name, source->name_size) ||
	    ranges_overlap(record, record_size, output_size, sizeof(*output_size)) ||
	    ranges_overlap(output_size, sizeof(*output_size), source,
			   sizeof(*source)) ||
	    ranges_overlap(output_size, sizeof(*output_size), spans,
			   span_count * sizeof(*spans)) ||
	    ranges_overlap(output_size, sizeof(*output_size), source->name,
			   source->name_size))
		return false;
	for (size_t i = 0U; i < span_count; i++)
		if (ranges_overlap(record, record_size, spans[i].data, spans[i].size) ||
		    ranges_overlap(output_size, sizeof(*output_size), spans[i].data,
				   spans[i].size))
			return false;

	memset(bytes, PAYLOAD_MM_AUTHVAR_STATE_ERASED, record_size);
	write_le16(bytes, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	bytes[2] = PAYLOAD_MM_AUTHVAR_STATE_ERASED;
	bytes[3] = 0U;
	write_le32(bytes + 4U, source->attributes);
	memset(bytes + 8U, 0, 8U);
	memcpy(bytes + 16U, source->timestamp, sizeof(source->timestamp));
	write_le32(bytes + 32U, 0U);
	write_le32(bytes + 36U, (uint32_t)source->name_size);
	write_le32(bytes + 40U, (uint32_t)data_size);
	memcpy(bytes + 44U, source->vendor_guid, sizeof(source->vendor_guid));
	memcpy(bytes + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, source->name,
	       source->name_size);
	at = data_offset;
	for (size_t i = 0U; i < span_count; i++) {
		memcpy(bytes + at, spans[i].data, spans[i].size);
		at += spans[i].size;
	}
	*output_size = (uint32_t)record_size;
	return true;
}
