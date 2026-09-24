/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_MOR_CLEAR_INTERNAL_H
#define PAYLOAD_MM_AUTHVAR_MOR_CLEAR_INTERNAL_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static inline bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static inline bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	const uintptr_t left_base = (uintptr_t)left;
	const uintptr_t right_base = (uintptr_t)right;

	if (!object_valid(left, left_size, 1) ||
	    !object_valid(right, right_size, 1))
		return true;
	if (left_base <= right_base)
		return right_base - left_base < left_size;
	return left_base - right_base < right_size;
}

#endif /* PAYLOAD_MM_AUTHVAR_MOR_CLEAR_INTERNAL_H */
