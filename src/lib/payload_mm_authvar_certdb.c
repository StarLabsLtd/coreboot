/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_certdb.h>

#include <commonlib/helpers.h>

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define CERTDB_LIST_HEADER_SIZE 4U
#define CERTDB_NODE_HEADER_SIZE 28U

struct certdb_match {
	size_t node_offset;
	size_t node_size;
	size_t binding_offset;
	size_t binding_size;
	uint8_t count;
};

static bool range_valid(const void *data, size_t size)
{
	return size == 0U ||
		(data != NULL && (uintptr_t)data <= UINTPTR_MAX - size);
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	if (!range_valid(left, left_size) || !range_valid(right, right_size))
		return true;
	if (!left_size || !right_size)
		return false;
	if (left_address <= right_address)
		return right_address - left_address < left_size;
	return left_address - right_address < right_size;
}

static bool spans_disjoint(const void *const *data, const size_t *sizes,
	size_t count)
{
	for (size_t left = 0U; left < count; left++) {
		if (!range_valid(data[left], sizes[left]))
			return false;
		for (size_t right = left + 1U; right < count; right++)
			if (ranges_overlap(data[left], sizes[left], data[right], sizes[right]))
				return false;
	}
	return true;
}

static uint32_t read_le32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
		((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_le32(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static bool name_valid(const void *name, size_t size)
{
	const uint8_t *bytes = name;

	if (!size || size % sizeof(uint16_t) ||
	    size / sizeof(uint16_t) > UINT32_MAX || !range_valid(name, size))
		return false;
	for (size_t offset = 0U; offset < size; offset += sizeof(uint16_t))
		if (!bytes[offset] && !bytes[offset + 1U])
			return false;
	return true;
}

static enum payload_mm_authvar_certdb_result scan_certdb(
	const uint8_t *source, size_t source_size, const uint8_t vendor_guid[16],
	const void *name, size_t name_size, struct certdb_match *match)
{
	const uint8_t *key_name = name;
	size_t offset = CERTDB_LIST_HEADER_SIZE;
	struct certdb_match found = { 0 };

	if (source_size < CERTDB_LIST_HEADER_SIZE ||
	    read_le32(source) != source_size)
		return PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED;
	while (offset < source_size) {
		size_t node_size;
		size_t packed_name_size;
		size_t binding_size;
		size_t expected_size;
		const uint8_t *node;
		const uint8_t *packed_name;

		if (source_size - offset < CERTDB_NODE_HEADER_SIZE)
			return PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED;
		node = source + offset;
		node_size = read_le32(node + 16U);
		if (__builtin_mul_overflow((size_t)read_le32(node + 20U),
			sizeof(uint16_t), &packed_name_size))
			return PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED;
		binding_size = read_le32(node + 24U);
		if (!binding_size ||
		    __builtin_add_overflow(CERTDB_NODE_HEADER_SIZE,
			packed_name_size, &expected_size) ||
		    __builtin_add_overflow(expected_size, binding_size, &expected_size) ||
		    node_size != expected_size || node_size > source_size - offset)
			return PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED;
		packed_name = node + CERTDB_NODE_HEADER_SIZE;
		if (!name_valid(packed_name, packed_name_size))
			return PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED;
		if (!memcmp(node, vendor_guid, 16U) && name_size == packed_name_size &&
		    !memcmp(packed_name, key_name, name_size)) {
			if (found.count == UINT8_MAX)
				return PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED;
			found.count++;
			found.node_offset = offset;
			found.node_size = node_size;
			found.binding_offset = offset + CERTDB_NODE_HEADER_SIZE +
				packed_name_size;
			found.binding_size = binding_size;
		}
		offset += node_size;
	}
	if (found.count > 1U)
		return PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED;
	if (!found.count)
		return PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND;
	*match = found;
	return PAYLOAD_MM_AUTHVAR_CERTDB_OK;
}

enum payload_mm_authvar_certdb_result payload_mm_authvar_certdb_find(
	const void *source, size_t source_size, const uint8_t vendor_guid[16],
	const void *name, size_t name_size,
	struct payload_mm_authvar_certdb_binding *binding)
{
	const void *spans[] = { source, vendor_guid, name, binding };
	const size_t sizes[] = { source_size, 16U, name_size, sizeof(*binding) };
	struct certdb_match match;
	enum payload_mm_authvar_certdb_result result;

	if (!source || !vendor_guid || !binding ||
	    (uintptr_t)binding % _Alignof(*binding) ||
	    source_size > UINT32_MAX ||
	    !name_valid(name, name_size) ||
	    !spans_disjoint(spans, sizes, ARRAY_SIZE(spans)))
		return PAYLOAD_MM_AUTHVAR_CERTDB_INVALID;
	result = scan_certdb(source, source_size, vendor_guid, name, name_size, &match);
	if (result != PAYLOAD_MM_AUTHVAR_CERTDB_OK)
		return result;
	binding->data = (const uint8_t *)source + match.binding_offset;
	binding->size = match.binding_size;
	return PAYLOAD_MM_AUTHVAR_CERTDB_OK;
}

enum payload_mm_authvar_certdb_result payload_mm_authvar_certdb_compose(
	const void *source, size_t source_size,
	enum payload_mm_authvar_certdb_operation operation,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	const void *binding, size_t binding_size,
	void *output, size_t output_capacity, size_t *output_size)
{
	const void *spans[] = {
		source, vendor_guid, name, binding, output, output_size,
	};
	const size_t sizes[] = {
		source_size, 16U, name_size, binding_size, output_capacity,
		sizeof(*output_size),
	};
	struct certdb_match match;
	enum payload_mm_authvar_certdb_result result;
	uint8_t *bytes = output;
	size_t final_size;

	if (!source || !vendor_guid || !output || !output_size ||
	    (uintptr_t)output_size % _Alignof(*output_size) ||
	    source_size > UINT32_MAX || !output_capacity ||
	    !name_valid(name, name_size) ||
	    (operation != PAYLOAD_MM_AUTHVAR_CERTDB_ADD &&
	     operation != PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE) ||
	    (operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD &&
	     (!binding || !binding_size)) ||
	    (operation == PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE &&
	     (binding || binding_size)) ||
	    !spans_disjoint(spans, sizes, ARRAY_SIZE(spans)))
		return PAYLOAD_MM_AUTHVAR_CERTDB_INVALID;

	result = scan_certdb(source, source_size, vendor_guid, name, name_size, &match);
	if (operation == PAYLOAD_MM_AUTHVAR_CERTDB_ADD) {
		size_t node_size;

		if (result == PAYLOAD_MM_AUTHVAR_CERTDB_OK)
			return PAYLOAD_MM_AUTHVAR_CERTDB_EXISTS;
		if (result != PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND)
			return result;
		if (__builtin_add_overflow(CERTDB_NODE_HEADER_SIZE, name_size, &node_size) ||
		    __builtin_add_overflow(node_size, binding_size, &node_size) ||
		    __builtin_add_overflow(source_size, node_size, &final_size) ||
		    node_size > UINT32_MAX || final_size > UINT32_MAX ||
		    final_size > output_capacity)
			return PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE;
		memcpy(bytes, source, source_size);
		write_le32(bytes, (uint32_t)final_size);
		memcpy(bytes + source_size, vendor_guid, 16U);
		write_le32(bytes + source_size + 16U, (uint32_t)node_size);
		write_le32(bytes + source_size + 20U,
			(uint32_t)(name_size / sizeof(uint16_t)));
		write_le32(bytes + source_size + 24U, (uint32_t)binding_size);
		memcpy(bytes + source_size + CERTDB_NODE_HEADER_SIZE, name, name_size);
		memcpy(bytes + source_size + CERTDB_NODE_HEADER_SIZE + name_size,
			binding, binding_size);
	} else {
		if (result != PAYLOAD_MM_AUTHVAR_CERTDB_OK)
			return result;
		final_size = source_size - match.node_size;
		if (final_size > output_capacity)
			return PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE;
		memcpy(bytes, source, match.node_offset);
		write_le32(bytes, (uint32_t)final_size);
		memcpy(bytes + match.node_offset,
			(const uint8_t *)source + match.node_offset + match.node_size,
			source_size - match.node_offset - match.node_size);
	}
	*output_size = final_size;
	return PAYLOAD_MM_AUTHVAR_CERTDB_OK;
}
