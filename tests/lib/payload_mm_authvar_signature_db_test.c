/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_signature_db.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 28U
#define OWNER_SIZE 16U

static const uint8_t guids[][16] = {
	{ 0x26, 0x16, 0xc4, 0xc1, 0x4c, 0x50, 0x92, 0x40, 0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28 },
	{ 0xe8, 0x66, 0x57, 0x3c, 0x9c, 0x26, 0x34, 0x4e, 0xaa, 0x14, 0xed, 0x77, 0x6e, 0x85, 0xb3, 0xb6 },
	{ 0x90, 0x61, 0xb3, 0xe2, 0x9b, 0x87, 0x3d, 0x4a, 0xad, 0x8d, 0xf2, 0xe7, 0xbb, 0xa3, 0x27, 0x84 },
	{ 0x12, 0xa5, 0x6c, 0x82, 0x10, 0xcf, 0xc9, 0x4a, 0xb1, 0x87, 0xbe, 0x01, 0x49, 0x66, 0x31, 0xbd },
	{ 0x4f, 0x44, 0xf8, 0x67, 0x43, 0x87, 0xf1, 0x48, 0xa3, 0x28, 0x1e, 0xaa, 0xb8, 0x73, 0x60, 0x80 },
	{ 0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a, 0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72 },
	{ 0x33, 0x52, 0x6e, 0x0b, 0x5c, 0xa6, 0xc9, 0x44, 0x94, 0x07, 0xd9, 0xab, 0x83, 0xbf, 0xc8, 0xbd },
	{ 0x07, 0x53, 0x3e, 0xff, 0xd0, 0x9f, 0xc9, 0x48, 0x85, 0xf1, 0x8a, 0xd5, 0x6c, 0x70, 0x1e, 0x01 },
	{ 0xae, 0x0f, 0x3e, 0x09, 0xc4, 0xa6, 0x50, 0x4f, 0x9f, 0x1b, 0xd4, 0x1e, 0x2b, 0x89, 0xc1, 0x9a },
	{ 0x92, 0xa4, 0xd2, 0x3b, 0xc0, 0x96, 0x79, 0x40, 0xb4, 0x20, 0xfc, 0xf9, 0x8e, 0xf1, 0x03, 0xed },
	{ 0x6e, 0x87, 0x76, 0x70, 0xc2, 0x80, 0xe6, 0x4e, 0xaa, 0xd2, 0x28, 0xb3, 0x49, 0xa6, 0x86, 0x5b },
	{ 0x63, 0xbf, 0x6d, 0x44, 0x02, 0x25, 0xda, 0x4c, 0xbc, 0xfa, 0x24, 0x65, 0xd2, 0xb0, 0xfe, 0x9d },
};
static const uint32_t data_sizes[] = { 32, 256, 256, 20, 256, 0, 28, 48, 64, 48, 64, 80 };

static void expect(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		exit(1);
	}
}

static void put32(uint8_t *p, uint32_t value)
{
	p[0] = value; p[1] = value >> 8; p[2] = value >> 16; p[3] = value >> 24;
}

static size_t make_list(uint8_t *out, const uint8_t guid[16],
	const uint8_t *entries, uint32_t stride, uint32_t count)
{
	size_t size = HEADER_SIZE + (size_t)stride * count;

	memcpy(out, guid, 16);
	put32(out + 16, size);
	put32(out + 20, 0);
	put32(out + 24, stride);
	if (count)
		memcpy(out + HEADER_SIZE, entries, (size_t)stride * count);
	return size;
}

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	uint8_t *data;
	long length = 0;

	expect(file != NULL, "open certificate");
	expect(!fseek(file, 0, SEEK_END) && (length = ftell(file)) > 0 &&
		!fseek(file, 0, SEEK_SET), "measure certificate");
	data = malloc(length);
	expect(data && fread(data, 1, length, file) == (size_t)length, "read certificate");
	fclose(file);
	*size = length;
	return data;
}

int main(int argc, char **argv)
{
	static struct payload_mm_crypto_owner owner;
	uint8_t buffer[8192], current[1024], append[2048], output[2048];
	uint8_t entries[4096] = { 0 };
	struct payload_mm_authvar_signature_db_stats stats, sentinel;
	enum payload_mm_verify_status status;
	uint8_t *der;
	uint8_t *many_entries, *many_list;
	uint8_t *comparison_entries, *comparison_current, *comparison_append;
	size_t der_size, size, current_size, append_size, output_size;

	expect(argc == 2, "certificate argument");
	der = read_file(argv[1], &der_size);
	expect(der_size + OWNER_SIZE < sizeof(entries), "certificate fits test buffer");

	/* Prove all twelve supported types and every fixed stride. */
	for (size_t type = 0; type < 12; type++) {
		uint32_t data_size = data_sizes[type];
		if (type == 5) {
			data_size = der_size;
			memcpy(entries + OWNER_SIZE, der, der_size);
		}
		size = make_list(buffer, guids[type], entries, OWNER_SIZE + data_size, 1);
		memset(&stats, 0, sizeof(stats));
		status = payload_mm_authvar_signature_db_validate(&owner, buffer, size,
			PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, UINT32_MAX, &stats);
		expect(status == PAYLOAD_MM_VERIFY_OK && stats.list_count == 1 &&
			stats.signature_count == 1 && stats.x509_count == (type == 5),
			"supported type validates");
		put32(buffer + 24, OWNER_SIZE + data_size + 1);
		expect(payload_mm_authvar_signature_db_validate(&owner, buffer, size,
			PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, UINT32_MAX, NULL) == PAYLOAD_MM_VERIFY_MALFORMED,
			"wrong stride rejected");
	}

	memcpy(entries + OWNER_SIZE, der, der_size);
	size = make_list(buffer, guids[5], entries, OWNER_SIZE + der_size, 1);
	expect(payload_mm_authvar_signature_db_validate(&owner, buffer, size,
		PAYLOAD_MM_AUTHVAR_PLATFORM_KEY, 1U, NULL) == PAYLOAD_MM_VERIFY_OK,
		"single X509 native PK");
	owner.fail_allocation = 1U;
	owner.allocation_count = 0U;
	expect(payload_mm_authvar_signature_db_validate(&owner, buffer, size,
		PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, 0U, NULL) == PAYLOAD_MM_VERIFY_UNSUPPORTED &&
		owner.allocation_count == 0U, "X509 limit precedes crypto");
	owner.fail_allocation = 0U;
	memcpy(entries + OWNER_SIZE + OWNER_SIZE + der_size, entries,
		OWNER_SIZE + der_size);
	size = make_list(buffer, guids[5], entries, OWNER_SIZE + der_size, 2);
	expect(payload_mm_authvar_signature_db_validate(&owner, buffer, size,
		PAYLOAD_MM_AUTHVAR_PLATFORM_KEY, 1U, NULL) == PAYLOAD_MM_VERIFY_MALFORMED,
		"multi-entry PK rejected");
	buffer[HEADER_SIZE + OWNER_SIZE + OWNER_SIZE + der_size + der_size / 2] ^= 1;
	sentinel = (struct payload_mm_authvar_signature_db_stats){ 7, 8, 9 };
	stats = sentinel;
	expect(payload_mm_authvar_signature_db_validate(&owner, buffer, size,
		PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, UINT32_MAX, &stats) == PAYLOAD_MM_VERIFY_MALFORMED &&
		!memcmp(&stats, &sentinel, sizeof(stats)),
		"every X509 checked and stats failure-atomic");

	/* current=A/owner1; append=A/owner1, A/owner2 twice, B/owner1. */
	memset(entries, 0, sizeof(entries));
	entries[0] = 1; entries[OWNER_SIZE] = 0xa1;
	current_size = make_list(current, guids[0], entries, OWNER_SIZE + 32, 1);
	memcpy(entries + 48, entries, 48);
	entries[48] = 2;
	memcpy(entries + 96, entries + 48, 48);
	memcpy(entries + 144, entries, 48);
	entries[144 + OWNER_SIZE] = 0xb2;
	append_size = make_list(append, guids[0], entries, 48, 4);
	memset(output, 0x5a, sizeof(output));
	output_size = 1234;
	status = payload_mm_authvar_signature_db_filter_append(&owner,
		current, current_size, append, append_size,
		output, sizeof(output), &output_size);
	expect(status == PAYLOAD_MM_VERIFY_OK && output_size == HEADER_SIZE + 3 * 48,
		"filter size");
	expect(output[HEADER_SIZE] == 2 && output[HEADER_SIZE + 48] == 2 &&
		output[HEADER_SIZE + 96] == 1 &&
		output[HEADER_SIZE + 96 + OWNER_SIZE] == 0xb2,
		"filter preserves order and append-local duplicates");
	expect(output[16] == output_size, "filter rewrites list size");

	memset(output, 0x5a, sizeof(output));
	output_size = 1234;
	expect(payload_mm_authvar_signature_db_filter_append(&owner,
		current, current_size, append, append_size, output, 1, &output_size) ==
		PAYLOAD_MM_VERIFY_NO_MEMORY && output_size == 1234 && output[0] == 0x5a,
		"capacity failure is output-atomic");
	expect(payload_mm_authvar_signature_db_filter_append(&owner,
		current, current_size, append, append_size, append, sizeof(append),
		&output_size) == PAYLOAD_MM_VERIFY_INVALID,
		"output alias rejected");
	expect(payload_mm_authvar_signature_db_validate(&owner,
		(const void *)UINTPTR_MAX, 1U, PAYLOAD_MM_AUTHVAR_SIGNATURE_DB,
		UINT32_MAX, NULL) == PAYLOAD_MM_VERIFY_INVALID,
		"wrapping address range rejected");
	expect(payload_mm_authvar_signature_db_validate(&owner, current, current_size,
		(enum payload_mm_authvar_signature_db_profile)-1, UINT32_MAX, NULL) ==
		PAYLOAD_MM_VERIFY_INVALID, "negative profile rejected");
	put32(append + 20, 1);
	memset(output, 0x5a, sizeof(output));
	expect(payload_mm_authvar_signature_db_filter_append(&owner,
		current, current_size, append, append_size, output, sizeof(output),
		&output_size) == PAYLOAD_MM_VERIFY_MALFORMED && output[0] == 0x5a,
		"semantic failure is output-atomic");

	many_entries = calloc(65U, OWNER_SIZE + der_size);
	many_list = malloc(2U * HEADER_SIZE + 65U * (OWNER_SIZE + der_size));
	expect(many_entries && many_list, "allocate X509 limit case");
	for (size_t i = 0U; i < 65U; i++)
		memcpy(many_entries + i * (OWNER_SIZE + der_size) + OWNER_SIZE,
			der, der_size);
	size = make_list(many_list, guids[5], many_entries, OWNER_SIZE + der_size, 65U);
	memset(output, 0x5a, sizeof(output));
	output_size = 1234U;
	expect(payload_mm_authvar_signature_db_filter_append(&owner,
		current, current_size, many_list, size, output, sizeof(output),
		&output_size) == PAYLOAD_MM_VERIFY_UNSUPPORTED &&
		output_size == 1234U && output[0] == 0x5a,
		"filter X509 work limit is output-atomic");
	size = make_list(many_list, guids[5], many_entries,
		OWNER_SIZE + der_size, 64U);
	size += make_list(many_list + size, guids[5],
		many_entries + 64U * (OWNER_SIZE + der_size),
		OWNER_SIZE + der_size, 1U);
	owner.fail_allocation = 1U;
	owner.allocation_count = 0U;
	expect(payload_mm_authvar_signature_db_validate(&owner, many_list, size,
		PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, 64U, NULL) ==
		PAYLOAD_MM_VERIFY_UNSUPPORTED && owner.allocation_count == 0U,
		"aggregate X509 limit precedes crypto across lists");
	owner.fail_allocation = 0U;
	free(many_list);
	free(many_entries);

	comparison_entries = calloc(257U, OWNER_SIZE + 32U);
	comparison_current = malloc(HEADER_SIZE + 257U * (OWNER_SIZE + 32U));
	comparison_append = malloc(HEADER_SIZE + 257U * (OWNER_SIZE + 32U));
	expect(comparison_entries && comparison_current && comparison_append,
		"allocate comparison limit case");
	current_size = make_list(comparison_current, guids[0], comparison_entries,
		OWNER_SIZE + 32U, 257U);
	append_size = make_list(comparison_append, guids[0], comparison_entries,
		OWNER_SIZE + 32U, 257U);
	output_size = 1234U;
	expect(payload_mm_authvar_signature_db_filter_append(&owner,
		comparison_current, current_size, comparison_append, append_size,
		output, sizeof(output), &output_size) == PAYLOAD_MM_VERIFY_UNSUPPORTED &&
		output_size == 1234U, "filter comparison work is bounded");
	free(comparison_append);
	free(comparison_current);
	free(comparison_entries);

	free(der);
	puts("Payload-MM authvar signature-db tests: PASS");
	return 0;
}
