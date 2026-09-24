/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_certdb.h>

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define assert(c) do { if (!(c)) abort(); } while (0)

#define SINGLE_SIZE 70U
#define FIRST_NODE_SIZE 66U
#define SECOND_NODE_SIZE 35U
#define TWO_SIZE 105U
#define BINDING_OFFSET 38U

static const uint8_t guid_a[16] = {
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
};
static const uint8_t guid_b[16] = {
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
};
static const uint8_t guid_c[16] = {
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
};
static const uint8_t name_a[] = { 'K', 0, 'e', 0, 'y', 0 };
static const uint8_t name_b[] = { 'Z', 0 };
static const uint8_t name_c[] = { 'L', 0, 'a', 0, 's', 0, 't', 0 };
static const uint8_t name_d[] = { 'B', 0, 'a', 0, 'd', 0 };
static const uint8_t binding_b[] = { 0xf0, 0xf1, 0xf2, 0xf3, 0xf4 };
static const uint8_t binding_c[] = { 1, 3, 5, 7, 9, 11, 13 };
static const uint8_t empty_certdb[] = { 4, 0, 0, 0 };

/* Independently serialized EDK2 26.09 certdb fixtures. */
static const uint8_t single_fixture[SINGLE_SIZE] = {
	0x46, 0, 0, 0,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x42, 0, 0, 0, 3, 0, 0, 0, 0x20, 0, 0, 0,
	'K', 0, 'e', 0, 'y', 0,
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
};

static const uint8_t second_fixture[4U + SECOND_NODE_SIZE] = {
	0x27, 0, 0, 0,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
	0x23, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0,
	'Z', 0, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4,
};

static const uint8_t two_fixture[TWO_SIZE] = {
	0x69, 0, 0, 0,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x42, 0, 0, 0, 3, 0, 0, 0, 0x20, 0, 0, 0,
	'K', 0, 'e', 0, 'y', 0,
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
	0x23, 0, 0, 0, 1, 0, 0, 0, 5, 0, 0, 0,
	'Z', 0, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4,
};

static bool bytes_are(const void *data, size_t size, uint8_t value)
{
	const uint8_t *bytes = data;

	for (size_t i = 0U; i < size; i++)
		if (bytes[i] != value)
			return false;
	return true;
}

static void put32(uint8_t *data, size_t offset, uint32_t value)
{
	data[offset] = (uint8_t)value;
	data[offset + 1U] = (uint8_t)(value >> 8);
	data[offset + 2U] = (uint8_t)(value >> 16);
	data[offset + 3U] = (uint8_t)(value >> 24);
}

static void expect_find(enum payload_mm_authvar_certdb_result expected,
	const void *source, size_t source_size, const uint8_t guid[16],
	const void *name, size_t name_size)
{
	struct payload_mm_authvar_certdb_binding binding;
	struct payload_mm_authvar_certdb_binding sentinel;

	memset(&binding, 0xa5, sizeof(binding));
	sentinel = binding;
	assert(payload_mm_authvar_certdb_find(source, source_size, guid, name,
		name_size, &binding) == expected);
	if (expected != PAYLOAD_MM_AUTHVAR_CERTDB_OK)
		assert(!memcmp(&binding, &sentinel, sizeof(binding)));
}

static void exact_lookup_and_one_bit_walk(void)
{
	uint8_t unaligned[SINGLE_SIZE + 16U];
	uint8_t changed[SINGLE_SIZE];
	struct payload_mm_authvar_certdb_binding binding;

	assert(payload_mm_authvar_certdb_find(single_fixture, sizeof(single_fixture),
		guid_a, name_a, sizeof(name_a), &binding) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(binding.data == single_fixture + BINDING_OFFSET);
	assert(binding.size == 32U);
	assert(!memcmp(binding.data, single_fixture + BINDING_OFFSET, binding.size));
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND, single_fixture,
		sizeof(single_fixture), guid_b, name_b, sizeof(name_b));
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND, single_fixture,
		sizeof(single_fixture), guid_a, name_d, sizeof(name_d));
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND, empty_certdb,
		sizeof(empty_certdb), guid_a, name_a, sizeof(name_a));

	for (size_t shift = 0U; shift < 16U; shift++) {
		memcpy(unaligned + shift, single_fixture, sizeof(single_fixture));
		assert(payload_mm_authvar_certdb_find(unaligned + shift,
			sizeof(single_fixture), guid_a, name_a, sizeof(name_a),
			&binding) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
		assert(binding.data == unaligned + shift + BINDING_OFFSET);
	}

	for (size_t offset = 0U; offset < sizeof(single_fixture); offset++) {
		for (uint8_t bit = 0U; bit < 8U; bit++) {
			enum payload_mm_authvar_certdb_result expected;

			memcpy(changed, single_fixture, sizeof(changed));
			changed[offset] ^= (uint8_t)(1U << bit);
			if (offset < 4U || (offset >= 20U && offset < 32U))
				expected = PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED;
			else if (offset < BINDING_OFFSET)
				expected = PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND;
			else
				expected = PAYLOAD_MM_AUTHVAR_CERTDB_OK;
			memset(&binding, 0xa5, sizeof(binding));
			assert(payload_mm_authvar_certdb_find(changed, sizeof(changed),
				guid_a, name_a, sizeof(name_a), &binding) == expected);
			if (expected == PAYLOAD_MM_AUTHVAR_CERTDB_OK) {
				assert(binding.data == changed + BINDING_OFFSET);
				assert(binding.size == 32U);
				assert(!memcmp(binding.data, changed + BINDING_OFFSET, 32U));
			}
		}
	}
}

static void exact_add_remove_and_order(void)
{
	uint8_t output[256];
	uint8_t unaligned[TWO_SIZE + 16U];
	uint8_t triple[256];
	uint8_t expected[256];
	uint8_t digest[64];
	size_t output_size;
	size_t triple_size;
	struct payload_mm_authvar_certdb_binding found;

	memset(output, 0x5a, sizeof(output));
	output_size = SIZE_MAX;
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
		output, sizeof(output), &output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(output_size == sizeof(two_fixture));
	assert(!memcmp(output, two_fixture, sizeof(two_fixture)));
	assert(bytes_are(output + output_size, sizeof(output) - output_size, 0x5a));
	for (size_t shift = 0U; shift < 16U; shift++) {
		memset(unaligned, 0x5a, sizeof(unaligned));
		output_size = SIZE_MAX;
		assert(payload_mm_authvar_certdb_compose(single_fixture,
			sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
			guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
			unaligned + shift, sizeof(two_fixture), &output_size) ==
			PAYLOAD_MM_AUTHVAR_CERTDB_OK);
		assert(output_size == sizeof(two_fixture));
		assert(!memcmp(unaligned + shift, two_fixture, sizeof(two_fixture)));
	}

	memset(output, 0x5a, sizeof(output));
	output_size = SIZE_MAX;
	assert(payload_mm_authvar_certdb_compose(two_fixture, sizeof(two_fixture),
		PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE, guid_a, name_a, sizeof(name_a),
		NULL, 0U, output, sizeof(output), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(output_size == sizeof(second_fixture));
	assert(!memcmp(output, second_fixture, sizeof(second_fixture)));
	assert(bytes_are(output + output_size, sizeof(output) - output_size, 0x5a));

	memset(output, 0x5a, sizeof(output));
	output_size = SIZE_MAX;
	assert(payload_mm_authvar_certdb_compose(two_fixture, sizeof(two_fixture),
		PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE, guid_b, name_b, sizeof(name_b),
		NULL, 0U, output, sizeof(output), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(output_size == sizeof(single_fixture));
	assert(!memcmp(output, single_fixture, sizeof(single_fixture)));

	memset(output, 0x5a, sizeof(output));
	output_size = SIZE_MAX;
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE,
		guid_a, name_a, sizeof(name_a), NULL, 0U, output, sizeof(output),
		&output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(output_size == sizeof(empty_certdb));
	assert(!memcmp(output, empty_certdb, sizeof(empty_certdb)));

	/* First, middle and last lookups, followed by middle compaction. */
	assert(payload_mm_authvar_certdb_compose(two_fixture, sizeof(two_fixture),
		PAYLOAD_MM_AUTHVAR_CERTDB_ADD, guid_c, name_c, sizeof(name_c),
		binding_c, sizeof(binding_c), triple, sizeof(triple), &triple_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(payload_mm_authvar_certdb_find(triple, triple_size, guid_a, name_a,
		sizeof(name_a), &found) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(found.size == 32U);
	assert(payload_mm_authvar_certdb_find(triple, triple_size, guid_b, name_b,
		sizeof(name_b), &found) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(found.size == sizeof(binding_b));
	assert(payload_mm_authvar_certdb_find(triple, triple_size, guid_c, name_c,
		sizeof(name_c), &found) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(found.size == sizeof(binding_c));
	put32(expected, 0U,
		(uint32_t)(triple_size - SECOND_NODE_SIZE));
	memcpy(expected + 4U, two_fixture + 4U, FIRST_NODE_SIZE);
	memcpy(expected + 4U + FIRST_NODE_SIZE,
		triple + TWO_SIZE, triple_size - TWO_SIZE);
	assert(payload_mm_authvar_certdb_compose(triple, triple_size,
		PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE, guid_b, name_b, sizeof(name_b),
		NULL, 0U, output, sizeof(output), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert(output_size == triple_size - SECOND_NODE_SIZE);
	assert(!memcmp(output, expected, output_size));

	/* Native digest lengths and an arbitrary legacy stack remain opaque. */
	for (size_t size = 32U; size <= sizeof(digest); size += 16U) {
		for (size_t i = 0U; i < size; i++)
			digest[i] = (uint8_t)(i + size);
		assert(payload_mm_authvar_certdb_compose(empty_certdb,
			sizeof(empty_certdb), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
			guid_a, name_a, sizeof(name_a), digest, size, output,
			sizeof(output), &output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
		assert(payload_mm_authvar_certdb_find(output, output_size, guid_a,
			name_a, sizeof(name_a), &found) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
		assert(found.size == size && !memcmp(found.data, digest, size));
	}
}

static void malformed_streams(void)
{
	uint8_t data[256];
	uint8_t duplicate[4U + 2U * FIRST_NODE_SIZE];
	long page_size;
	uint8_t *guarded;
	uint8_t *short_node;

	for (size_t size = 0U; size < 4U; size++)
		expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, single_fixture, size,
			guid_a, name_a, sizeof(name_a));

	for (size_t tail = 1U; tail < 28U; tail++) {
		memset(data, 0x77, sizeof(data));
		memcpy(data, empty_certdb, sizeof(empty_certdb));
		put32(data, 0U, (uint32_t)(4U + tail));
		expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data, 4U + tail,
			guid_a, name_a, sizeof(name_a));
	}
	page_size = sysconf(_SC_PAGESIZE);
	assert(page_size > 0);
	guarded = mmap(NULL, (size_t)page_size * 2U, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(guarded != MAP_FAILED);
	assert(mprotect(guarded + page_size, (size_t)page_size, PROT_NONE) == 0);
	short_node = guarded + page_size - 5U;
	put32(short_node, 0U, 5U);
	short_node[4U] = 0x55U;
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, short_node, 5U,
		guid_a, name_a, sizeof(name_a));
	assert(munmap(guarded, (size_t)page_size * 2U) == 0);

	for (uint32_t node_size = 0U; node_size < 28U; node_size++) {
		memcpy(data, single_fixture, sizeof(single_fixture));
		put32(data, 20U, node_size);
		expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
			sizeof(single_fixture), guid_a, name_a, sizeof(name_a));
	}

	memcpy(data, single_fixture, sizeof(single_fixture));
	put32(data, 0U, sizeof(single_fixture) - 1U);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a));
	memcpy(data, single_fixture, sizeof(single_fixture));
	put32(data, 20U, FIRST_NODE_SIZE + 1U);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a));
	memcpy(data, single_fixture, sizeof(single_fixture));
	put32(data, 20U, 100U);
	put32(data, 28U, 66U);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a));
	memcpy(data, single_fixture, sizeof(single_fixture));
	put32(data, 24U, 0U);
	put32(data, 28U, 38U);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a));
	memcpy(data, single_fixture, BINDING_OFFSET);
	put32(data, 0U, BINDING_OFFSET);
	put32(data, 20U, BINDING_OFFSET - 4U);
	put32(data, 28U, 0U);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		BINDING_OFFSET, guid_a, name_a, sizeof(name_a));
	memcpy(data, single_fixture, sizeof(single_fixture));
	put32(data, 24U, UINT32_MAX);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a));
	memcpy(data, single_fixture, sizeof(single_fixture));
	put32(data, 28U, UINT32_MAX);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a));
	memcpy(data, single_fixture, sizeof(single_fixture));
	data[32U] = 0U;
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a));

	put32(duplicate, 0U, (uint32_t)sizeof(duplicate));
	memcpy(duplicate + 4U, single_fixture + 4U, FIRST_NODE_SIZE);
	memcpy(duplicate + 4U + FIRST_NODE_SIZE, single_fixture + 4U,
		FIRST_NODE_SIZE);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, duplicate,
		sizeof(duplicate), guid_a, name_a, sizeof(name_a));

	/* A match never hides corruption before or after it. */
	memcpy(data, two_fixture, sizeof(two_fixture));
	put32(data, TWO_SIZE - SECOND_NODE_SIZE + 16U, 0U);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		sizeof(two_fixture), guid_a, name_a, sizeof(name_a));
	memcpy(data, two_fixture, sizeof(two_fixture));
	put32(data, 20U, 0U);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED, data,
		sizeof(two_fixture), guid_b, name_b, sizeof(name_b));
}

static void admission_capacity_and_aliasing(void)
{
	uint8_t output[256];
	uint8_t malformed[SINGLE_SIZE + 1U];
	uint8_t shared[32];
	uint8_t misaligned[sizeof(struct payload_mm_authvar_certdb_binding) + 1U];
	struct payload_mm_authvar_certdb_binding binding;
	size_t output_size;
	size_t sentinel_size;

	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_INVALID, NULL, 0U, guid_a,
		name_a, sizeof(name_a));
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_INVALID, single_fixture,
		sizeof(single_fixture), NULL, name_a, sizeof(name_a));
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_INVALID, single_fixture,
		sizeof(single_fixture), guid_a, NULL, 0U);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_INVALID, single_fixture,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a) - 1U);
	memset(shared, 1, sizeof(shared));
	shared[2] = 0U;
	shared[3] = 0U;
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_INVALID, single_fixture,
		sizeof(single_fixture), guid_a, shared, 6U);
	#if SIZE_MAX > UINT32_MAX
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_INVALID, single_fixture,
		sizeof(single_fixture), guid_a, (const void *)0x1000000000ULL,
		(size_t)UINT32_MAX * sizeof(uint16_t) + sizeof(uint16_t));
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_INVALID,
		(const void *)0x1000000000ULL,
		(size_t)UINT32_MAX + 1U, guid_a, name_a, sizeof(name_a));
#endif
	assert(payload_mm_authvar_certdb_find(single_fixture,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a),
		(struct payload_mm_authvar_certdb_binding *)(misaligned + 1U)) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_find(
		(const void *)(UINTPTR_MAX - 1U), 4U, guid_a, name_a,
		sizeof(name_a), &binding) == PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_find(single_fixture,
		sizeof(single_fixture), (const uint8_t *)(UINTPTR_MAX - 7U),
		name_a, sizeof(name_a), &binding) == PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_find(single_fixture,
		sizeof(single_fixture), guid_a, (const void *)(UINTPTR_MAX - 1U),
		4U, &binding) == PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);

	/* Descriptor and immutable input aliases are rejected. */
	assert(payload_mm_authvar_certdb_find(single_fixture,
		sizeof(single_fixture), guid_a, name_a, sizeof(name_a),
		(struct payload_mm_authvar_certdb_binding *)(uintptr_t)single_fixture) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_INVALID, single_fixture,
		sizeof(single_fixture), single_fixture + 4U, name_a, sizeof(name_a));
	expect_find(PAYLOAD_MM_AUTHVAR_CERTDB_INVALID, single_fixture,
		sizeof(single_fixture), guid_a, single_fixture + 32U, sizeof(name_a));
	memset(shared, 1, sizeof(shared));
	assert(payload_mm_authvar_certdb_find(single_fixture,
		sizeof(single_fixture), shared, shared, 6U, &binding) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);

	memset(output, 0x5a, sizeof(output));
	output_size = 0x1122334455667788ULL;
	sentinel_size = output_size;
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
		output, sizeof(two_fixture) - 1U, &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE);
	assert(output_size == sentinel_size && bytes_are(output, sizeof(output), 0x5a));
	assert(payload_mm_authvar_certdb_compose(two_fixture, sizeof(two_fixture),
		PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE, guid_a, name_a, sizeof(name_a),
		NULL, 0U, output, sizeof(second_fixture) - 1U, &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE);
	assert(output_size == sentinel_size && bytes_are(output, sizeof(output), 0x5a));

	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_a, name_a, sizeof(name_a), binding_b, sizeof(binding_b),
		output, sizeof(output), &output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_EXISTS);
	assert(output_size == sentinel_size && bytes_are(output, sizeof(output), 0x5a));
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE,
		guid_b, name_b, sizeof(name_b), NULL, 0U, output, sizeof(output),
		&output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND);
	assert(output_size == sentinel_size && bytes_are(output, sizeof(output), 0x5a));

	/* Every mutable/input alias class is rejected before output publication. */
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
		(void *)(uintptr_t)single_fixture, sizeof(single_fixture), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
		(void *)(uintptr_t)guid_b, sizeof(guid_b), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
		(void *)(uintptr_t)name_b, sizeof(name_b), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
		(void *)(uintptr_t)binding_b, sizeof(binding_b), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), single_fixture + BINDING_OFFSET, 32U,
		output, sizeof(output), &output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
		output, sizeof(output), (size_t *)(void *)(output + 8U)) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
		(void *)(UINTPTR_MAX - 1U), 4U, &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), (const void *)(UINTPTR_MAX - 1U), 4U,
		output, sizeof(output), &output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), binding_b, sizeof(binding_b),
		output, sizeof(output), (size_t *)(void *)(misaligned + 1U)) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
#if SIZE_MAX > UINT32_MAX
	assert(payload_mm_authvar_certdb_compose(empty_certdb,
		sizeof(empty_certdb), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_a, name_a, sizeof(name_a), (const void *)0x1000000000ULL,
		(size_t)UINT32_MAX, output, sizeof(output), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE);
	assert(output_size == sentinel_size && bytes_are(output, sizeof(output), 0x5a));
	assert(payload_mm_authvar_certdb_compose(empty_certdb,
		sizeof(empty_certdb), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_a, name_a, sizeof(name_a), (const void *)0x1000000000ULL,
		(size_t)UINT32_MAX, (void *)0x3000000000ULL,
		(size_t)UINT32_MAX + 128U, &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE);
#endif

	/* Operation-specific unused/required binding parameters are exact. */
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_ADD,
		guid_b, name_b, sizeof(name_b), NULL, 0U, output, sizeof(output),
		&output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE,
		guid_a, name_a, sizeof(name_a), binding_b, sizeof(binding_b),
		output, sizeof(output), &output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);
	assert(payload_mm_authvar_certdb_compose(single_fixture,
		sizeof(single_fixture), (enum payload_mm_authvar_certdb_operation)2,
		guid_a, name_a, sizeof(name_a), NULL, 0U, output, sizeof(output),
		&output_size) == PAYLOAD_MM_AUTHVAR_CERTDB_INVALID);

	memcpy(malformed, single_fixture, sizeof(single_fixture));
	malformed[sizeof(single_fixture)] = 0x77U;
	put32(malformed, 0U, (uint32_t)sizeof(malformed));
	output_size = sentinel_size;
	memset(output, 0x5a, sizeof(output));
	assert(payload_mm_authvar_certdb_compose(malformed, sizeof(malformed),
		PAYLOAD_MM_AUTHVAR_CERTDB_ADD, guid_b, name_b, sizeof(name_b),
		binding_b, sizeof(binding_b), output, sizeof(output), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED);
	assert(output_size == sentinel_size && bytes_are(output, sizeof(output), 0x5a));
	assert(payload_mm_authvar_certdb_compose(malformed, sizeof(malformed),
		PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE, guid_a, name_a, sizeof(name_a),
		NULL, 0U, output, sizeof(output), &output_size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_MALFORMED);
	assert(output_size == sentinel_size && bytes_are(output, sizeof(output), 0x5a));
}

int main(void)
{
	exact_lookup_and_one_bit_walk();
	exact_add_remove_and_order();
	malformed_streams();
	admission_capacity_and_aliasing();
	return 0;
}
