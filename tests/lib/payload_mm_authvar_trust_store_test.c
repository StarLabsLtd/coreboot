/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_store.h>
#include <boot/payload_mm_authvar_trust_store.h>

#include "crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORE_SIZE (256U * 1024U)
#define ENTRY_COUNT 4U
#define ATTRIBUTES 0x27U

struct buffer {
	uint8_t *data;
	size_t size;
};

static const uint8_t store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};
static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t x509_guid[16] = {
	0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
	0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72,
};
static const uint8_t hash_guid[16] = {
	0x26, 0x16, 0xc4, 0xc1, 0x4c, 0x50, 0x92, 0x40,
	0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28,
};
static const uint8_t unknown_guid[16] = {
	0xde, 0xad, 0xbe, 0xef, 0x11, 0x22, 0x33, 0x44,
	0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc,
};
static const uint16_t pk_name[] = { 'P', 'K', 0 };
static const uint16_t kek_name[] = { 'K', 'E', 'K', 0 };

static uint8_t store[STORE_SIZE];
static struct payload_mm_authvar_store_entry entries[ENTRY_COUNT];
static struct payload_mm_authvar_store_index store_index;
static int failures;

static void expect(bool condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static void put16(uint8_t *p, uint16_t value)
{
	p[0] = value;
	p[1] = value >> 8;
}

static void put32(uint8_t *p, uint32_t value)
{
	for (size_t byte = 0U; byte < 4U; byte++)
		p[byte] = value >> (8U * byte);
}

static struct buffer read_file(const char *directory, const char *name)
{
	struct buffer result = { 0 };
	char path[4096];
	long size;
	FILE *file;

	if (snprintf(path, sizeof(path), "%s/%s", directory, name) < 0)
		return result;
	file = fopen(path, "rb");
	if (!file || fseek(file, 0L, SEEK_END) || (size = ftell(file)) <= 0L ||
	    fseek(file, 0L, SEEK_SET)) {
		if (file)
			fclose(file);
		return result;
	}
	result.data = malloc((size_t)size);
	if (result.data && fread(result.data, 1U, (size_t)size, file) == (size_t)size)
		result.size = (size_t)size;
	else {
		free(result.data);
		result.data = NULL;
	}
	fclose(file);
	return result;
}

static bool der_item(const uint8_t *data, size_t size, size_t offset,
	size_t *header_size, size_t *body_size)
{
	size_t length_bytes;
	size_t length = 0U;

	if (offset > size || size - offset < 2U)
		return false;
	if (data[offset + 1U] < 0x80U) {
		*header_size = 2U;
		*body_size = data[offset + 1U];
	} else {
		length_bytes = data[offset + 1U] & 0x7fU;
		if (!length_bytes || length_bytes > sizeof(size_t) ||
		    size - offset < 2U + length_bytes)
			return false;
		for (size_t byte = 0U; byte < length_bytes; byte++)
			length = (length << 8) | data[offset + 2U + byte];
		*header_size = 2U + length_bytes;
		*body_size = length;
	}
	return *header_size <= size - offset &&
		*body_size <= size - offset - *header_size;
}

static bool der_increase_length(uint8_t *data, size_t offset, size_t increase)
{
	size_t length_bytes;
	size_t length = 0U;

	if (data[offset + 1U] < 0x80U)
		return false;
	length_bytes = data[offset + 1U] & 0x7fU;
	for (size_t byte = 0U; byte < length_bytes; byte++)
		length = (length << 8) | data[offset + 2U + byte];
	if (increase > SIZE_MAX - length)
		return false;
	length += increase;
	for (size_t byte = length_bytes; byte > 0U; byte--) {
		data[offset + 1U + byte] = (uint8_t)length;
		length >>= 8;
	}
	return length == 0U;
}

static struct buffer duplicate_cms_certificate(const struct buffer *cms,
	const struct buffer *certificate)
{
	struct buffer result = { 0 };
	size_t ancestors[4];
	size_t header;
	size_t body;
	size_t offset;
	size_t insertion = 0U;

	ancestors[0] = 0U;
	if (!der_item(cms->data, cms->size, ancestors[0], &header, &body))
		return result;
	offset = header;
	if (!der_item(cms->data, cms->size, offset, &header, &body))
		return result;
	offset += header + body;
	ancestors[1] = offset;
	if (!der_item(cms->data, cms->size, offset, &header, &body))
		return result;
	ancestors[2] = offset + header;
	if (!der_item(cms->data, cms->size, ancestors[2], &header, &body))
		return result;
	offset = ancestors[2] + header;
	for (size_t item = 0U; item < 3U; item++) {
		if (!der_item(cms->data, cms->size, offset, &header, &body))
			return result;
		offset += header + body;
	}
	ancestors[3] = offset;
	if (!der_item(cms->data, cms->size, offset, &header, &body) ||
	    cms->data[offset] != 0xa0U)
		return result;
	for (size_t candidate = offset + header;
	     candidate + certificate->size <= offset + header + body; candidate++)
		if (!memcmp(cms->data + candidate, certificate->data,
			certificate->size)) {
			insertion = candidate + certificate->size;
			break;
		}
	if (!insertion || certificate->size > SIZE_MAX - cms->size)
		return result;
	result.size = cms->size + certificate->size;
	result.data = malloc(result.size);
	if (!result.data) {
		result.size = 0U;
		return result;
	}
	memcpy(result.data, cms->data, insertion);
	memcpy(result.data + insertion, certificate->data, certificate->size);
	memcpy(result.data + insertion + certificate->size, cms->data + insertion,
		cms->size - insertion);
	for (size_t ancestor = 0U; ancestor < 4U; ancestor++)
		if (!der_increase_length(result.data, ancestors[ancestor],
			certificate->size)) {
			free(result.data);
			result = (struct buffer) { 0 };
			break;
		}
	return result;
}

static struct buffer make_list(const uint8_t type[16],
	const struct buffer *items, size_t count)
{
	struct buffer result = { 0 };
	size_t item_size;

	if (!count || !items[0].size)
		return result;
	item_size = 16U + items[0].size;
	for (size_t item = 1U; item < count; item++)
		if (items[item].size != items[0].size)
			return result;
	result.size = 28U + item_size * count;
	result.data = calloc(1U, result.size);
	if (!result.data) {
		result.size = 0U;
		return result;
	}
	memcpy(result.data, type, 16U);
	put32(result.data + 16U, (uint32_t)result.size);
	put32(result.data + 20U, 0U);
	put32(result.data + 24U, (uint32_t)item_size);
	for (size_t item = 0U; item < count; item++) {
		uint8_t *signature = result.data + 28U + item * item_size;

		memset(signature, (int)(item + 1U), 16U);
		memcpy(signature + 16U, items[item].data, items[item].size);
	}
	return result;
}

static struct buffer join(const struct buffer *left, const struct buffer *right)
{
	struct buffer result = { 0 };

	result.size = left->size + right->size;
	result.data = malloc(result.size);
	if (!result.data) {
		result.size = 0U;
		return result;
	}
	memcpy(result.data, left->data, left->size);
	memcpy(result.data + left->size, right->data, right->size);
	return result;
}

static void init_store(void)
{
	memset(store, 0xff, sizeof(store));
	memcpy(store, store_guid, sizeof(store_guid));
	put32(store + 16U, sizeof(store));
	store[20] = 0x5aU;
	store[21] = 0xfeU;
	put16(store + 22U, 0U);
	put32(store + 24U, 0U);
	memset(entries, 0, sizeof(entries));
	store_index = (struct payload_mm_authvar_store_index) {
		.entries = entries,
		.entry_capacity = ENTRY_COUNT,
	};
}

static size_t add_record(size_t offset, const uint16_t *name, size_t name_size,
	const struct buffer *data)
{
	size_t data_offset = (offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
		name_size + 3U) & ~(size_t)3U;
	size_t next = (data_offset + data->size + 3U) & ~(size_t)3U;

	if (next > sizeof(store))
		abort();
	memset(store + offset, 0, next - offset);
	put16(store + offset, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	store[offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	put32(store + offset + 4U, ATTRIBUTES);
	put32(store + offset + 36U, name_size);
	put32(store + offset + 40U, data->size);
	memcpy(store + offset + 44U, global_guid, sizeof(global_guid));
	memcpy(store + offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, name,
		name_size);
	memset(store + offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + name_size,
		0xff, data_offset - offset - PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE -
		name_size);
	memcpy(store + data_offset, data->data, data->size);
	memset(store + data_offset + data->size, 0xff,
		next - data_offset - data->size);
	return next;
}

static bool scan_store(const struct buffer *pk, const struct buffer *kek)
{
	static const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 64U,
		.maximum_data_size = 128U * 1024U,
		.maximum_records = ENTRY_COUNT,
	};
	size_t offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	init_store();
	if (pk)
		offset = add_record(offset, pk_name, sizeof(pk_name), pk);
	if (kek)
		add_record(offset, kek_name, sizeof(kek_name), kek);
	return payload_mm_authvar_store_scan(&store_index, store, sizeof(store), &limits) ==
		CB_SUCCESS;
}

static struct payload_mm_authvar_route_plan pk_plan(void)
{
	return (struct payload_mm_authvar_route_plan) {
		.target = PAYLOAD_MM_AUTHVAR_TARGET_PK,
		.authorities = { PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK },
		.authority_count = 1U,
	};
}

static struct payload_mm_authvar_route_plan db_plan(void)
{
	return (struct payload_mm_authvar_route_plan) {
		.target = PAYLOAD_MM_AUTHVAR_TARGET_DB,
		.authorities = {
			PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
			PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK,
		},
		.authority_count = 2U,
	};
}

static enum payload_mm_verify_status prepare_cms(
	struct payload_mm_crypto_owner *owner, const struct buffer *cms,
	const struct buffer *content, struct payload_mm_cms_verified_signer *verified)
{
	const struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	const struct payload_mm_crypto_span content_span = {
		content->data, content->size,
	};

	return payload_mm_cms_verify_detached_untrusted(owner, &signed_data,
		&content_span, 1U, verified);
}

static enum payload_mm_verify_status verify(
	struct payload_mm_crypto_owner *owner, const struct buffer *cms,
	const struct buffer *content,
	const struct payload_mm_authvar_route_plan *plan)
{
	const struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	const struct payload_mm_crypto_span content_span = {
		content->data, content->size,
	};

	return payload_mm_authvar_trust_store_verify(owner, &signed_data,
		&content_span, 1U, &store_index, plan);
}

static bool owner_clean(const struct payload_mm_crypto_owner *owner)
{
	if (owner->busy || owner->arena_used)
		return false;
	for (size_t byte = 0U; byte < sizeof(owner->arena); byte++)
		if (owner->arena[byte])
			return false;
	return true;
}

static void normal_policy(const struct buffer *content,
	const struct buffer *pk_cms, const struct buffer *pk_omitted,
	const struct buffer *pk_chain_no_parent, const struct buffer *pk_chain,
	const struct buffer *kek_cms, const struct buffer *pk_cert,
	const struct buffer *kek_cert, const struct buffer *wrong_cert)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_cms_verified_signer pk_verified;
	struct payload_mm_cms_verified_signer omitted_verified;
	struct payload_mm_cms_verified_signer kek_verified;
	struct payload_mm_authvar_route_plan plan;
	struct buffer pk_list = make_list(x509_guid, pk_cert, 1U);
	struct buffer wrong_list = make_list(x509_guid, wrong_cert, 1U);
	struct buffer kek_list;

	memset(&owner, 0, sizeof(owner));
	expect(prepare_cms(&owner, pk_cms, content, &pk_verified) ==
		PAYLOAD_MM_VERIFY_OK, "could not prepare PK CMS");
	memset(&owner, 0, sizeof(owner));
	expect(prepare_cms(&owner, pk_omitted, content, &omitted_verified) ==
		PAYLOAD_MM_VERIFY_OK, "could not prepare omitted-PK CMS");
	memset(&owner, 0, sizeof(owner));
	expect(prepare_cms(&owner, kek_cms, content, &kek_verified) ==
		PAYLOAD_MM_VERIFY_OK, "could not prepare KEK CMS");
	kek_list = make_list(x509_guid, kek_cert, 1U);

	expect(scan_store(&pk_list, NULL), "PK store scan failed");
	plan = pk_plan();
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, pk_cms, content, &plan) == PAYLOAD_MM_VERIFY_OK,
		"embedded current PK was rejected");
	expect(owner_clean(&owner), "PK success retained protected state");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, pk_omitted, content, &plan) ==
		PAYLOAD_MM_VERIFY_REJECTED, "omitted current PK was accepted");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, pk_chain_no_parent, content, &plan) ==
		PAYLOAD_MM_VERIFY_REJECTED,
		"leaf signer chaining through current PK was accepted");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, pk_chain, content, &plan) ==
		PAYLOAD_MM_VERIFY_REJECTED,
		"leaf signer chaining through current PK and parent was accepted");

	expect(scan_store(&wrong_list, &kek_list), "fallback store scan failed");
	plan = db_plan();
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, kek_cms, content, &plan) == PAYLOAD_MM_VERIFY_OK,
		"db did not fall back from wrong PK to current KEK");

	expect(scan_store(NULL, &kek_list), "KEK-only store scan failed");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, kek_cms, content, &plan) == PAYLOAD_MM_VERIFY_OK,
		"db did not fall back from absent PK to KEK");
	expect(scan_store(NULL, NULL), "empty store scan failed");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, pk_cms, content, &plan) ==
		PAYLOAD_MM_VERIFY_REJECTED, "absent authorities did not reject");

	free(kek_list.data);
	free(wrong_list.data);
	free(pk_list.data);
}

static void list_policy(const struct buffer *content, const struct buffer *kek_cms,
	const struct buffer *kek_cert)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_cms_verified_signer verified;
	struct payload_mm_authvar_route_plan plan = db_plan();
	struct buffer anchor;
	struct buffer x509;
	struct buffer non_x509;
	struct buffer mixed;
	struct buffer malformed = { (uint8_t *)(uintptr_t)"bad", 3U };
	struct buffer hash = { (uint8_t *)(uintptr_t)"01234567890123456789012345678901", 32U };
	struct buffer short_hash = { hash.data, hash.size - 1U };
	struct buffer unknown;
	struct buffer wrong_size;
	struct buffer anchors[65];
	struct buffer too_many;
	size_t cms_allocations;

	memset(&owner, 0, sizeof(owner));
	expect(prepare_cms(&owner, kek_cms, content, &verified) == PAYLOAD_MM_VERIFY_OK,
		"could not prepare list-policy CMS");
	anchor = *kek_cert;
	x509 = make_list(x509_guid, &anchor, 1U);
	non_x509 = make_list(hash_guid, &hash, 1U);
	mixed = join(&non_x509, &x509);
	expect(scan_store(NULL, &mixed), "mixed KEK store scan failed");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, kek_cms, content, &plan) == PAYLOAD_MM_VERIFY_OK,
		"mixed KEK did not skip non-X509 list");
	expect(scan_store(NULL, &non_x509), "non-X509 KEK scan failed");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, kek_cms, content, &plan) ==
		PAYLOAD_MM_VERIFY_REJECTED, "well-formed KEK without X509 misclassified");
	unknown = make_list(unknown_guid, &hash, 1U);
	expect(scan_store(NULL, &unknown), "unknown-type KEK scan failed");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, kek_cms, content, &plan) ==
		PAYLOAD_MM_VERIFY_MALFORMED, "unknown signature-list GUID accepted");
	wrong_size = make_list(hash_guid, &short_hash, 1U);
	expect(scan_store(NULL, &wrong_size), "wrong-size KEK scan failed");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, kek_cms, content, &plan) ==
		PAYLOAD_MM_VERIFY_MALFORMED,
		"wrong fixed-size signature list accepted");
	for (size_t item = 0U; item < 65U; item++)
		anchors[item] = *kek_cert;
	too_many = make_list(x509_guid, anchors, 65U);
	expect(scan_store(NULL, &too_many), "oversized-anchor KEK scan failed");
	memset(&owner, 0, sizeof(owner));
	expect(prepare_cms(&owner, kek_cms, content, &verified) ==
		PAYLOAD_MM_VERIFY_OK, "anchor-limit CMS baseline failed");
	cms_allocations = owner.allocation_count;
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, kek_cms, content, &plan) ==
		PAYLOAD_MM_VERIFY_UNSUPPORTED,
		"more than 64 KEK anchors was not unsupported");
	expect(owner.allocation_count == cms_allocations,
		"anchor limit entered trust-anchor crypto");
	expect(scan_store(&malformed, &x509), "malformed-PK store scan failed");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, kek_cms, content, &plan) ==
		PAYLOAD_MM_VERIFY_MALFORMED, "malformed PK incorrectly fell back to KEK");

	free(too_many.data);
	free(wrong_size.data);
	free(unknown.data);
	free(mixed.data);
	free(non_x509.data);
	free(x509.data);
}

static void duplicate_pk(const struct buffer *content, const struct buffer *cms,
	const struct buffer *pk_cert)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_authvar_route_plan plan = pk_plan();
	struct buffer pk_list = make_list(x509_guid, pk_cert, 1U);
	struct buffer duplicated = duplicate_cms_certificate(cms, pk_cert);
	enum payload_mm_verify_status status = PAYLOAD_MM_VERIFY_INTERNAL;

	expect(duplicated.data != NULL, "could not duplicate PK in CMS");
	expect(scan_store(&pk_list, NULL), "duplicate-PK store scan failed");
	memset(&owner, 0, sizeof(owner));
	if (duplicated.data)
		status = verify(&owner, &duplicated, content, &plan);
	expect(status != PAYLOAD_MM_VERIFY_OK,
		"duplicate embedded current PK was accepted");
	free(duplicated.data);
	free(pk_list.data);
}

static void alias_inputs(const struct buffer *cms, const struct buffer *content)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_authvar_route_plan *inside_plan;
	struct payload_mm_crypto_span signed_data;
	struct payload_mm_crypto_span content_span = { content->data, content->size };
	size_t plan_offset = (cms->size + _Alignof(*inside_plan) - 1U) &
		~(size_t)(_Alignof(*inside_plan) - 1U);
	size_t size = plan_offset + sizeof(*inside_plan);
	uint8_t *aliased = calloc(1U, size);

	expect(aliased != NULL, "could not allocate aliased descriptors");
	if (!aliased)
		return;
	memcpy(aliased, cms->data, cms->size);
	inside_plan = (void *)(aliased + plan_offset);
	*inside_plan = pk_plan();
	signed_data = (struct payload_mm_crypto_span) {
		aliased, plan_offset + sizeof(*inside_plan),
	};
	memset(&owner, 0, sizeof(owner));
	expect(payload_mm_authvar_trust_store_verify(&owner, &signed_data,
		&content_span, 1U, &store_index, inside_plan) ==
		PAYLOAD_MM_VERIFY_INVALID,
		"plan descriptor inside CMS accepted");
	free(aliased);
}

static void attribute_policy(const struct buffer *content,
	const struct buffer *pk_cms, const struct buffer *kek_cms,
	const struct buffer *pk_cert, const struct buffer *kek_cert)
{
	static struct payload_mm_crypto_owner owner;
	struct payload_mm_authvar_route_plan plan = pk_plan();
	struct buffer pk_list = make_list(x509_guid, pk_cert, 1U);
	struct buffer kek_list = make_list(x509_guid, kek_cert, 1U);

	expect(scan_store(&pk_list, NULL), "wrong-attribute PK store scan failed");
	put32(store + entries[0].record_offset + 4U, 0x7U);
	entries[0].attributes = 0x7U;
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, pk_cms, content, &plan) ==
		PAYLOAD_MM_VERIFY_MALFORMED, "wrong PK attributes accepted");

	expect(scan_store(NULL, &kek_list), "wrong-attribute KEK store scan failed");
	put32(store + entries[0].record_offset + 4U, 0x7U);
	entries[0].attributes = 0x7U;
	plan = db_plan();
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, kek_cms, content, &plan) ==
		PAYLOAD_MM_VERIFY_MALFORMED, "wrong KEK attributes accepted");
	free(kek_list.data);
	free(pk_list.data);
}

static enum payload_mm_verify_status verify_direct(
	struct payload_mm_crypto_owner *owner, const struct buffer *cms,
	const struct buffer *content,
	const struct payload_mm_authvar_store_index *candidate,
	const struct payload_mm_authvar_route_plan *plan)
{
	const struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	const struct payload_mm_crypto_span content_span = {
		content->data, content->size,
	};

	return payload_mm_authvar_trust_store_verify(owner, &signed_data,
		&content_span, 1U, candidate, plan);
}

static void owner_overlap(const struct buffer *content, const struct buffer *cms)
{
	struct payload_mm_crypto_owner *owner = aligned_alloc(16U, sizeof(*owner));
	struct payload_mm_authvar_store_index candidate;
	struct payload_mm_authvar_store_index *inside_index;
	struct payload_mm_authvar_store_entry *inside_entries;
	struct payload_mm_authvar_route_plan plan = pk_plan();
	struct payload_mm_authvar_route_plan *inside_plan;
	size_t entry_bytes = store_index.entry_count * sizeof(entries[0]);

	expect(owner != NULL, "could not allocate overlapping crypto owner");
	if (!owner)
		return;

	memset(owner, 0, sizeof(*owner));
	candidate = store_index;
	memcpy(owner->arena, store, sizeof(store));
	candidate.store = owner->arena;
	expect(verify_direct(owner, cms, content, &candidate, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "owner/store overlap accepted");

	memset(owner, 0, sizeof(*owner));
	candidate = store_index;
	inside_entries = (void *)owner->arena;
	memcpy(inside_entries, entries, entry_bytes);
	candidate.entries = inside_entries;
	expect(verify_direct(owner, cms, content, &candidate, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "owner/entries overlap accepted");

	memset(owner, 0, sizeof(*owner));
	inside_index = (void *)owner->arena;
	*inside_index = store_index;
	expect(verify_direct(owner, cms, content, inside_index, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "owner/index overlap accepted");

	memset(owner, 0, sizeof(*owner));
	inside_plan = (void *)owner->arena;
	*inside_plan = plan;
	expect(verify_direct(owner, cms, content, &store_index, inside_plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "owner/plan overlap accepted");
	free(owner);
}

static void hostile_inputs(const struct buffer *content, const struct buffer *cms,
	const struct buffer *pk_cert)
{
	static struct payload_mm_crypto_owner owner;
	static struct payload_mm_crypto_owner blocker;
	struct payload_mm_authvar_route_plan plan = pk_plan();
	struct payload_mm_authvar_route_plan bad;
	struct payload_mm_authvar_store_index forged;
	struct buffer pk_list = make_list(x509_guid, pk_cert, 1U);
	struct payload_mm_crypto_span signed_data = { cms->data, cms->size };
	struct payload_mm_crypto_span content_span = { content->data, content->size };
	size_t allocations;

	expect(scan_store(&pk_list, NULL), "hostile-input store scan failed");
	alias_inputs(cms, content);
	memset(&owner, 0, sizeof(owner));
	expect(payload_mm_authvar_trust_store_verify(&owner, NULL, &content_span, 1U,
		&store_index, &plan) == PAYLOAD_MM_VERIFY_INVALID,
		"NULL CMS span accepted");
	expect(payload_mm_authvar_trust_store_verify(&owner, &signed_data, NULL, 1U,
		&store_index, &plan) == PAYLOAD_MM_VERIFY_INVALID,
		"NULL content span accepted");
	expect(payload_mm_authvar_trust_store_verify(&owner, &signed_data,
		&content_span, 1U, NULL, &plan) == PAYLOAD_MM_VERIFY_INVALID,
		"NULL index accepted");
	expect(payload_mm_authvar_trust_store_verify(&owner, &signed_data,
		&content_span, 1U, &store_index, NULL) == PAYLOAD_MM_VERIFY_INVALID,
		"NULL plan accepted");

	bad = plan;
	bad.target = PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE;
	expect(verify(&owner, cms, content, &bad) == PAYLOAD_MM_VERIFY_INVALID,
		"private target accepted");
	bad = plan;
	bad.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS;
	expect(verify(&owner, cms, content, &bad) == PAYLOAD_MM_VERIFY_INVALID,
		"bypass authority accepted");
	bad = db_plan();
	bad.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK;
	expect(verify(&owner, cms, content, &bad) == PAYLOAD_MM_VERIFY_INVALID,
		"reordered db authorities accepted");

	forged = store_index;
	forged.entry_count = forged.entry_capacity + 1U;
	expect(payload_mm_authvar_trust_store_verify(&owner, &signed_data,
		&content_span, 1U, &forged, &plan) == PAYLOAD_MM_VERIFY_INVALID,
		"oversized index accepted");
	forged = store_index;
	forged.entries[0].data_offset = forged.store_size;
	expect(payload_mm_authvar_trust_store_verify(&owner, &signed_data,
		&content_span, 1U, &forged, &plan) == PAYLOAD_MM_VERIFY_INVALID,
		"forged entry accepted");
	expect(scan_store(&pk_list, NULL), "could not restore hostile store");
	forged = store_index;
	forged.used_size = forged.entries[0].data_offset;
	expect(verify_direct(&owner, cms, content, &forged, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "record beyond used size accepted");
	forged = store_index;
	forged.record_count = forged.maximum_records + 1U;
	expect(verify_direct(&owner, cms, content, &forged, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "record count above limit accepted");
	forged = store_index;
	forged.record_count = forged.entry_count - 1U;
	expect(verify_direct(&owner, cms, content, &forged, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "more entries than records accepted");
	forged = store_index;
	forged.maximum_name_size =
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE + 2U;
	expect(verify_direct(&owner, cms, content, &forged, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "excessive name limit accepted");
	forged = store_index;
	forged.maximum_data_size =
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE + 1U;
	expect(verify_direct(&owner, cms, content, &forged, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "excessive data limit accepted");
	forged = store_index;
	forged.maximum_records =
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS + 1U;
	expect(verify_direct(&owner, cms, content, &forged, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID, "excessive record limit accepted");
	owner_overlap(content, cms);

	memset(&owner, 0, sizeof(owner));
	memset(&blocker, 0, sizeof(blocker));
	expect(payload_mm_crypto_begin(&blocker) == PAYLOAD_MM_VERIFY_OK,
		"could not acquire crypto blocker");
	expect(verify(&owner, cms, content, &plan) == PAYLOAD_MM_VERIFY_BUSY,
		"nested store verification was not busy");
	expect(payload_mm_crypto_end(&blocker, PAYLOAD_MM_VERIFY_OK) ==
		PAYLOAD_MM_VERIFY_OK, "crypto blocker did not unwind");
	memset(&owner, 0, sizeof(owner));
	expect(verify(&owner, cms, content, &plan) == PAYLOAD_MM_VERIFY_OK,
		"allocation baseline failed");
	allocations = owner.allocation_count;
	expect(allocations != 0U, "trust-store verification made no allocations");
	for (size_t fail_at = 1U; fail_at <= allocations; fail_at++) {
		memset(&owner, 0, sizeof(owner));
		owner.fail_allocation = fail_at;
		expect(verify(&owner, cms, content, &plan) ==
			PAYLOAD_MM_VERIFY_NO_MEMORY, "allocation failure not reported");
		expect(owner_clean(&owner), "allocation failure retained protected state");
	}
	free(pk_list.data);
}

int main(int argc, char **argv)
{
	struct buffer content;
	struct buffer pk_cms;
	struct buffer pk_omitted;
	struct buffer pk_chain_no_parent;
	struct buffer pk_chain;
	struct buffer kek_cms;
	struct buffer pk_cert;
	struct buffer kek_cert;
	struct buffer wrong_cert;

	if (argc != 2)
		return 2;
	content = read_file(argv[1], "content.bin");
	pk_cms = read_file(argv[1], "pk-embedded.der");
	pk_omitted = read_file(argv[1], "pk-omitted.der");
	pk_chain_no_parent = read_file(argv[1], "pk-chain-no-parent.der");
	pk_chain = read_file(argv[1], "pk-chain.der");
	kek_cms = read_file(argv[1], "kek-embedded.der");
	pk_cert = read_file(argv[1], "pk.der");
	kek_cert = read_file(argv[1], "kek.der");
	wrong_cert = read_file(argv[1], "wrong.der");
	expect(content.data && pk_cms.data && pk_omitted.data &&
		pk_chain_no_parent.data && pk_chain.data && kek_cms.data &&
		pk_cert.data && kek_cert.data && wrong_cert.data,
		"could not read generated fixtures");
	if (!failures) {
		normal_policy(&content, &pk_cms, &pk_omitted, &pk_chain_no_parent,
			&pk_chain, &kek_cms, &pk_cert, &kek_cert, &wrong_cert);
		list_policy(&content, &kek_cms, &kek_cert);
		duplicate_pk(&content, &pk_cms, &pk_cert);
		attribute_policy(&content, &pk_cms, &kek_cms, &pk_cert, &kek_cert);
		hostile_inputs(&content, &pk_cms, &pk_cert);
	}
	free(wrong_cert.data);
	free(kek_cert.data);
	free(pk_cert.data);
	free(kek_cms.data);
	free(pk_chain.data);
	free(pk_chain_no_parent.data);
	free(pk_omitted.data);
	free(pk_cms.data);
	free(content.data);
	return failures != 0;
}
