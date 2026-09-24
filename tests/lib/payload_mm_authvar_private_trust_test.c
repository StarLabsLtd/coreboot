/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_certdb.h>
#include <boot/payload_mm_authvar_private_trust.h>

#include "crypto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define assert(c) do { \
	if (!(c)) { \
		fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #c); \
		abort(); \
	} \
} while (0)
#define STORE_SIZE (16U * 1024U)
#define ENTRY_COUNT 8U
#define CERTDB_ATTRIBUTES 0x27U
#define REQUEST_ATTRIBUTES 0x21U

struct buffer { uint8_t *data; size_t size; };

enum attack {
	ATTACK_NONE = 0,
	ATTACK_OUTPUT,
	ATTACK_PLAN,
	ATTACK_STORE,
	ATTACK_ENTRIES,
	ATTACK_INDEX,
	ATTACK_CONTENT,
	ATTACK_OWNER,
};

static const uint8_t vendor_guid[16] = {
	0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
	0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};
static const uint8_t other_guid[16] = {
	0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
	0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
};
static const uint8_t certdb_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const uint8_t name[] = { 'P', 0, 'r', 0, 'i', 0, 'v', 0 };
static const uint8_t stored_name[] = {
	'P', 0, 'r', 0, 'i', 0, 'v', 0, 0, 0,
};
static const uint8_t other_name[] = { 'O', 0, 't', 0, 'h', 0, 'r', 0, 0, 0 };
static const uint8_t certdb_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
};
static const uint8_t empty_certdb[] = { 4, 0, 0, 0 };

static uint8_t store[STORE_SIZE];
static struct payload_mm_authvar_store_entry entries[ENTRY_COUNT];
static struct payload_mm_authvar_store_index store_index;
static enum attack attack;
static struct payload_mm_crypto_span *attacked_content;

void payload_mm_authvar_private_trust_test_before_publish(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_authvar_store_index *store_index,
	const struct payload_mm_authvar_route_plan *plan,
	struct payload_mm_authvar_private_trust_decision *decision)
{
	switch (attack) {
	case ATTACK_OUTPUT:
		decision->accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
		break;
	case ATTACK_PLAN:
		((struct payload_mm_authvar_route_plan *)(uintptr_t)plan)->target =
			PAYLOAD_MM_AUTHVAR_TARGET_PK;
		break;
	case ATTACK_STORE:
		((uint8_t *)(uintptr_t)store_index->store)[0] ^= 1U;
		break;
	case ATTACK_ENTRIES:
		store_index->entries[0].attributes ^= 1U;
		break;
	case ATTACK_INDEX:
		((struct payload_mm_authvar_store_index *)(uintptr_t)store_index)->used_size++;
		break;
	case ATTACK_CONTENT:
		((uint8_t *)(uintptr_t)attacked_content[4].data)[0] ^= 1U;
		break;
	case ATTACK_OWNER:
		owner->arena[0] = 0xa5U;
		owner->arena_used = 1U;
		owner->busy = true;
		break;
	case ATTACK_NONE:
		break;
	}
}

static void put16(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *data, uint32_t value)
{
	for (size_t byte = 0U; byte < 4U; byte++)
		data[byte] = (uint8_t)(value >> (8U * byte));
}

static size_t align4(size_t value)
{
	return (value + 3U) & ~3U;
}

static void reset_index(void)
{
	memset(store, 0xff, sizeof(store));
	memset(entries, 0, sizeof(entries));
	store_index = (struct payload_mm_authvar_store_index) {
		.store = store,
		.store_size = sizeof(store),
		.used_size = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		.entries = entries,
		.entry_capacity = ENTRY_COUNT,
		.maximum_name_size = 4096U,
		.maximum_data_size = 4096U,
		.maximum_records = ENTRY_COUNT,
	};
}

static void add_record(const uint8_t guid[16], const uint8_t *record_name,
	size_t name_size, uint32_t attributes, const void *data, size_t data_size)
{
	struct payload_mm_authvar_store_entry *entry;
	size_t record_offset = align4(store_index.used_size);
	size_t name_offset = record_offset + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;
	size_t data_offset = align4(name_offset + name_size);
	size_t end = align4(data_offset + data_size);

	assert(store_index.entry_count < store_index.entry_capacity && end <= sizeof(store));
	entry = &entries[store_index.entry_count++];
	store_index.record_count++;
	*entry = (struct payload_mm_authvar_store_entry) {
		.record_offset = record_offset,
		.name_offset = name_offset,
		.name_size = name_size,
		.data_offset = data_offset,
		.data_size = data_size,
		.attributes = attributes,
	};
	memcpy(entry->vendor_guid, guid, 16U);
	put16(store + record_offset, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	store[record_offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	store[record_offset + 3U] = 0U;
	put32(store + record_offset + 4U, attributes);
	memset(store + record_offset + 8U, 0, 28U);
	put32(store + record_offset + 36U, (uint32_t)name_size);
	put32(store + record_offset + 40U, (uint32_t)data_size);
	memcpy(store + record_offset + 44U, guid, 16U);
	memcpy(store + name_offset, record_name, name_size);
	memcpy(store + data_offset, data, data_size);
	store_index.used_size = (uint32_t)end;
	assert(payload_mm_authvar_store_index_valid(&store_index));
}

static struct buffer read_file(const char *directory, const char *file_name)
{
	struct buffer result = { 0 };
	char path[4096];
	long size;
	FILE *file;

	assert(snprintf(path, sizeof(path), "%s/%s", directory, file_name) > 0);
	file = fopen(path, "rb");
	assert(file != NULL && fseek(file, 0L, SEEK_END) == 0);
	size = ftell(file);
	assert(size > 0L && fseek(file, 0L, SEEK_SET) == 0);
	result.data = malloc((size_t)size);
	assert(result.data != NULL);
	assert(fread(result.data, 1U, (size_t)size, file) == (size_t)size);
	assert(fclose(file) == 0);
	result.size = (size_t)size;
	return result;
}

static void content_spans(struct buffer *serialized,
	struct payload_mm_crypto_span content[5], bool empty_payload)
{
	assert(serialized->size >= 44U);
	content[0] = (struct payload_mm_crypto_span) { serialized->data, 8U };
	content[1] = (struct payload_mm_crypto_span) { serialized->data + 8U, 16U };
	content[2] = (struct payload_mm_crypto_span) { serialized->data + 24U, 4U };
	content[3] = (struct payload_mm_crypto_span) { serialized->data + 28U, 16U };
	content[4] = (struct payload_mm_crypto_span) {
		serialized->data + 44U, empty_payload ? 0U : serialized->size - 44U,
	};
}

static struct payload_mm_authvar_route_plan plan_for(
	enum payload_mm_authvar_authority authority)
{
	return (struct payload_mm_authvar_route_plan) {
		.target = PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE,
		.authorities = { authority, PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE },
		.authority_count = 1U,
	};
}

static enum payload_mm_verify_status verify(struct payload_mm_crypto_owner *owner,
	const struct buffer *cms, struct payload_mm_crypto_span content[5],
	struct payload_mm_authvar_route_plan *plan,
	struct payload_mm_authvar_private_trust_decision *decision)
{
	struct payload_mm_crypto_span signed_data = { cms->data, cms->size };

	attacked_content = content;
	return payload_mm_authvar_private_trust_verify(owner, &signed_data, content,
		5U, &store_index, plan, decision);
}

static void expect_failure(enum payload_mm_verify_status expected,
	struct payload_mm_crypto_owner *owner, const struct buffer *cms,
	struct payload_mm_crypto_span content[5],
	struct payload_mm_authvar_route_plan *plan)
{
	struct payload_mm_authvar_private_trust_decision decision;
	enum payload_mm_verify_status actual;

	memset(&decision, 0xa5, sizeof(decision));
	actual = verify(owner, cms, content, plan, &decision);
	if (actual != expected)
		fprintf(stderr, "expected %d got %d\n", expected, actual);
	assert(actual == expected);
	assert(decision.accepted_authority == PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE);
	assert(!decision.new_binding_size);
	assert(!memcmp(decision.new_binding,
		(uint8_t[PAYLOAD_MM_MAX_DIGEST_SIZE]) { 0 },
		sizeof(decision.new_binding)));
	assert(payload_mm_crypto_idle() && payload_mm_crypto_owner_is_clean(owner));
}

static size_t certdb_with_binding(const void *binding, size_t binding_size,
	uint8_t *output, size_t capacity)
{
	size_t size = 0U;

	assert(payload_mm_authvar_certdb_compose(empty_certdb,
		sizeof(empty_certdb), PAYLOAD_MM_AUTHVAR_CERTDB_ADD, vendor_guid,
		name, sizeof(name), binding, binding_size, output, capacity, &size) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	return size;
}

static void native_algorithms(const char *directory)
{
	static const char *const files[] = {
		"sha256.der", "sha384.der", "sha512.der",
	};
	static const size_t sizes[] = { 32U, 48U, 64U };
	struct buffer serialized = read_file(directory, "content.bin");
	struct payload_mm_crypto_span content[5];

	content_spans(&serialized, content, false);
	for (size_t algorithm = 0U; algorithm < 3U; algorithm++) {
		struct buffer cms = read_file(directory, files[algorithm]);
		struct payload_mm_crypto_owner owner = { 0 };
		struct payload_mm_authvar_route_plan plan = plan_for(
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER);
		struct payload_mm_authvar_private_trust_decision decision;
		uint8_t native_binding[PAYLOAD_MM_MAX_DIGEST_SIZE];
		uint8_t database[4096];
		size_t database_size;

		reset_index();
		add_record(certdb_guid, certdb_name, sizeof(certdb_name),
			CERTDB_ATTRIBUTES, empty_certdb, sizeof(empty_certdb));
		assert(verify(&owner, &cms, content, &plan, &decision) ==
			PAYLOAD_MM_VERIFY_OK);
		assert(decision.accepted_authority ==
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER);
		assert(decision.new_binding_size == sizes[algorithm]);
		memcpy(native_binding, decision.new_binding,
			decision.new_binding_size);
		if (algorithm == 0U) {
			for (size_t failure = 1U; failure <= 4U; failure++) {
				struct payload_mm_crypto_owner failing = {
					.fail_allocation = failure,
				};

				expect_failure(PAYLOAD_MM_VERIFY_NO_MEMORY, &failing,
					&cms, content, &plan);
			}
		}
		database_size = certdb_with_binding(decision.new_binding,
			decision.new_binding_size, database, sizeof(database));
		reset_index();
		add_record(certdb_guid, certdb_name, sizeof(certdb_name),
			CERTDB_ATTRIBUTES, database, database_size);
		add_record(vendor_guid, stored_name, sizeof(stored_name),
			REQUEST_ATTRIBUTES, "x", 1U);
		plan = plan_for(PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB);
		assert(verify(&owner, &cms, content, &plan, &decision) ==
			PAYLOAD_MM_VERIFY_OK);
		assert(decision.accepted_authority ==
			PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB);
		assert(!decision.new_binding_size);
		database[database_size - 1U] ^= 1U;
		reset_index();
		add_record(certdb_guid, certdb_name, sizeof(certdb_name),
			CERTDB_ATTRIBUTES, database, database_size);
		add_record(vendor_guid, stored_name, sizeof(stored_name),
			REQUEST_ATTRIBUTES, "x", 1U);
		expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &cms, content,
			&plan);
		if (algorithm == 0U) {
			uint8_t wrong_length[48] = { 0 };

			memcpy(wrong_length, native_binding, sizes[algorithm]);
			database_size = certdb_with_binding(wrong_length,
				sizeof(wrong_length), database, sizeof(database));
			reset_index();
			add_record(certdb_guid, certdb_name, sizeof(certdb_name),
				CERTDB_ATTRIBUTES, database, database_size);
			add_record(vendor_guid, stored_name, sizeof(stored_name),
				REQUEST_ATTRIBUTES, "x", 1U);
			expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &cms,
				content, &plan);
		}
		free(cms.data);
	}
	free(serialized.data);
}

static void empty_new_ignores_certdb(const char *directory)
{
	struct buffer serialized = read_file(directory, "empty-content.bin");
	struct buffer cms = read_file(directory, "no-cn.der");
	struct payload_mm_crypto_span content[5];
	struct payload_mm_crypto_owner owner = { 0 };
	struct payload_mm_authvar_route_plan plan = plan_for(
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER);
	struct payload_mm_authvar_private_trust_decision decision;
	uint8_t malformed[] = { 5, 0, 0, 0, 0 };

	content_spans(&serialized, content, true);
	reset_index();
	assert(verify(&owner, &cms, content, &plan, &decision) ==
		PAYLOAD_MM_VERIFY_OK);
	assert(!decision.new_binding_size);
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name), 1U, malformed,
		sizeof(malformed));
	assert(verify(&owner, &cms, content, &plan, &decision) ==
		PAYLOAD_MM_VERIFY_OK);
	assert(!decision.new_binding_size);
	free(cms.data);
	free(serialized.data);
}

static void legacy_and_certdb_edges(const char *directory)
{
	struct buffer serialized = read_file(directory, "content.bin");
	struct buffer empty_serialized = read_file(directory, "empty-content.bin");
	struct buffer cms = read_file(directory, "sha256.der");
	struct buffer empty_cms = read_file(directory, "empty-sha256.der");
	struct buffer signer = read_file(directory, "signer-cert.der");
	struct buffer no_cn_cms = read_file(directory, "no-cn.der");
	struct buffer no_cn_full = read_file(directory, "no-cn-full.der");
	struct buffer no_cn_cert = read_file(directory, "no-cn-cert.der");
	struct payload_mm_crypto_span content[5];
	struct payload_mm_crypto_span empty_content[5];
	struct payload_mm_crypto_owner owner = { 0 };
	struct payload_mm_authvar_route_plan new_plan = plan_for(
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER);
	struct payload_mm_authvar_route_plan old_plan = plan_for(
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB);
	struct payload_mm_authvar_private_trust_decision decision;
	uint8_t legacy[4096];
	uint8_t database[4096];
	uint8_t malformed[] = { 5, 0, 0, 0, 0 };
	size_t database_size;

	content_spans(&serialized, content, false);
	content_spans(&empty_serialized, empty_content, true);
	assert(signer.size + 5U <= sizeof(legacy));
	legacy[0] = 1U;
	put32(legacy + 1U, (uint32_t)signer.size);
	memcpy(legacy + 5U, signer.data, signer.size);
	database_size = certdb_with_binding(legacy, signer.size + 5U, database,
		sizeof(database));
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, database, database_size);
	add_record(vendor_guid, stored_name, sizeof(stored_name),
		REQUEST_ATTRIBUTES, "x", 1U);
	assert(verify(&owner, &cms, content, &old_plan, &decision) ==
		PAYLOAD_MM_VERIFY_OK);
	assert(decision.accepted_authority ==
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB);
	/* Existing delete still authenticates against its binding. */
	assert(verify(&owner, &empty_cms, empty_content, &old_plan, &decision) ==
		PAYLOAD_MM_VERIFY_OK);

	/* A no-CN signer works only through an exact legacy binding. */
	legacy[0] = 1U;
	put32(legacy + 1U, (uint32_t)no_cn_cert.size);
	memcpy(legacy + 5U, no_cn_cert.data, no_cn_cert.size);
	database_size = certdb_with_binding(legacy, no_cn_cert.size + 5U,
		database, sizeof(database));
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, database, database_size);
	add_record(vendor_guid, stored_name, sizeof(stored_name),
		REQUEST_ATTRIBUTES, "x", 1U);
	assert(verify(&owner, &no_cn_cms, empty_content, &old_plan, &decision) ==
		PAYLOAD_MM_VERIFY_OK);
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, empty_certdb, sizeof(empty_certdb));
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &no_cn_full, content,
		&new_plan);

	/* A stale binding cannot be adopted by a supposedly new target. */
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, database, database_size);
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &cms, content,
		&new_plan);
	/* Whole-stream corruption is terminal on both paths that need certdb. */
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, malformed, sizeof(malformed));
	expect_failure(PAYLOAD_MM_VERIFY_MALFORMED, &owner, &cms, content,
		&new_plan);
	add_record(vendor_guid, stored_name, sizeof(stored_name),
		REQUEST_ATTRIBUTES, "x", 1U);
	expect_failure(PAYLOAD_MM_VERIFY_MALFORMED, &owner, &cms, content,
		&old_plan);
	/* Fixed certdb attributes and target attributes are part of trust. */
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name), 0x23U,
		empty_certdb, sizeof(empty_certdb));
	expect_failure(PAYLOAD_MM_VERIFY_MALFORMED, &owner, &cms, content,
		&new_plan);
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, database, database_size);
	add_record(vendor_guid, stored_name, sizeof(stored_name), 0x23U, "x", 1U);
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &cms, content,
		&old_plan);
	/* Missing certdb and route/target disagreement fail closed. */
	reset_index();
	add_record(vendor_guid, stored_name, sizeof(stored_name),
		REQUEST_ATTRIBUTES, "x", 1U);
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &cms, content,
		&old_plan);
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &cms, content,
		&new_plan);
	reset_index();
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &cms, content,
		&old_plan);

	free(no_cn_cert.data);
	free(no_cn_full.data);
	free(no_cn_cms.data);
	free(signer.data);
	free(empty_cms.data);
	free(cms.data);
	free(empty_serialized.data);
	free(serialized.data);
}

static void fail_closed_and_sealed(const char *directory)
{
	struct buffer serialized = read_file(directory, "content.bin");
	struct buffer cms = read_file(directory, "sha256.der");
	struct buffer wrong = read_file(directory, "wrong.der");
	struct payload_mm_crypto_span content[5];
	struct payload_mm_crypto_owner owner = { 0 };
	struct payload_mm_authvar_route_plan plan = plan_for(
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER);
	struct payload_mm_authvar_private_trust_decision decision;
	uint8_t database[4096];
	size_t database_size;

	content_spans(&serialized, content, false);
	reset_index();
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &cms, content, &plan);
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, empty_certdb, sizeof(empty_certdb));
	assert(verify(&owner, &cms, content, &plan, &decision) ==
		PAYLOAD_MM_VERIFY_OK);
	database_size = certdb_with_binding(decision.new_binding,
		decision.new_binding_size, database, sizeof(database));
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, database, database_size);
	add_record(vendor_guid, stored_name, sizeof(stored_name),
		REQUEST_ATTRIBUTES, "x", 1U);
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &cms, content, &plan);
	plan = plan_for(PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB);
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &wrong, content, &plan);
	for (attack = ATTACK_OUTPUT; attack <= ATTACK_OWNER; attack++) {
		uint8_t saved_store[STORE_SIZE];
		struct payload_mm_authvar_store_entry saved_entries[ENTRY_COUNT];
		struct payload_mm_authvar_store_index saved_index = store_index;
		uint8_t saved_payload = content[4].data[0];
		struct payload_mm_authvar_route_plan saved_plan = plan;

		memcpy(saved_store, store, sizeof(store));
		memcpy(saved_entries, entries, sizeof(entries));
		expect_failure(attack == ATTACK_OWNER ? PAYLOAD_MM_VERIFY_INTERNAL :
			PAYLOAD_MM_VERIFY_CHANGED, &owner, &cms, content, &plan);
		memcpy(store, saved_store, sizeof(store));
		memcpy(entries, saved_entries, sizeof(entries));
		store_index = saved_index;
		((uint8_t *)(uintptr_t)content[4].data)[0] = saved_payload;
		plan = saved_plan;
	}
	attack = ATTACK_NONE;
	free(wrong.data);
	free(cms.data);
	free(serialized.data);
}

static void invalid_contract_and_cms(const char *directory)
{
	struct buffer serialized = read_file(directory, "content.bin");
	struct buffer empty_serialized = read_file(directory, "empty-content.bin");
	struct buffer cms = read_file(directory, "sha256.der");
	struct buffer empty_cms = read_file(directory, "empty-sha256.der");
	struct payload_mm_crypto_span content[5];
	struct payload_mm_crypto_span empty_content[5];
	struct payload_mm_crypto_span signed_data = { cms.data, cms.size };
	struct payload_mm_crypto_owner owner = { 0 };
	struct payload_mm_authvar_route_plan plan = plan_for(
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER);
	struct payload_mm_authvar_private_trust_decision decision;
	struct payload_mm_crypto_span saved;
	uint8_t saved_byte;
	enum payload_mm_verify_status status;

	content_spans(&serialized, content, false);
	content_spans(&empty_serialized, empty_content, true);
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, empty_certdb, sizeof(empty_certdb));
	memset(&decision, 0xa5, sizeof(decision));
	assert(payload_mm_authvar_private_trust_verify(&owner, &signed_data,
		content, 4U, &store_index, &plan, &decision) ==
		PAYLOAD_MM_VERIFY_INVALID);
	saved = content[1];
	content[1] = content[0];
	memset(&decision, 0xa5, sizeof(decision));
	assert(payload_mm_authvar_private_trust_verify(&owner, &signed_data,
		content, 5U, &store_index, &plan, &decision) ==
		PAYLOAD_MM_VERIFY_INVALID);
	content[1] = saved;
	saved_byte = serialized.data[2];
	serialized.data[2] = 0U;
	serialized.data[3] = 0U;
	memset(&decision, 0xa5, sizeof(decision));
	assert(payload_mm_authvar_private_trust_verify(&owner, &signed_data,
		content, 5U, &store_index, &plan, &decision) ==
		PAYLOAD_MM_VERIFY_INVALID);
	serialized.data[2] = saved_byte;
	serialized.data[3] = 0U;
	/* RT without BS is outside the admitted persistent profile. */
	put32(serialized.data + 24U, 0x25U);
	memset(&decision, 0xa5, sizeof(decision));
	assert(payload_mm_authvar_private_trust_verify(&owner, &signed_data,
		content, 5U, &store_index, &plan, &decision) ==
		PAYLOAD_MM_VERIFY_INVALID);
	put32(serialized.data + 24U, REQUEST_ATTRIBUTES);

	/* Even NEW+empty must authenticate CMS. */
	empty_cms.data[empty_cms.size - 1U] ^= 1U;
	{
		struct payload_mm_crypto_span empty_signed = {
			empty_cms.data, empty_cms.size,
		};

		reset_index();
		memset(&decision, 0xa5, sizeof(decision));
		status = payload_mm_authvar_private_trust_verify(&owner,
			&empty_signed, empty_content, 5U, &store_index, &plan,
			&decision);
		assert(status != PAYLOAD_MM_VERIFY_OK);
		assert(!decision.accepted_authority && !decision.new_binding_size);
		assert(!memcmp(decision.new_binding,
			(uint8_t[PAYLOAD_MM_MAX_DIGEST_SIZE]) { 0 },
			sizeof(decision.new_binding)));
	}
	free(empty_cms.data);
	free(cms.data);
	free(empty_serialized.data);
	free(serialized.data);
}

static void append_empty_and_near_keys(const char *directory)
{
	struct buffer serialized = read_file(directory, "content.bin");
	struct buffer cms = read_file(directory, "sha256.der");
	struct buffer append_serialized = read_file(directory,
		"append-empty-content.bin");
	struct buffer append_cms = read_file(directory, "append-empty-sha256.der");
	struct buffer no_cn_append = read_file(directory, "no-cn-append.der");
	struct payload_mm_crypto_span content[5];
	struct payload_mm_crypto_span append_content[5];
	struct payload_mm_crypto_owner owner = { 0 };
	struct payload_mm_authvar_route_plan new_plan = plan_for(
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER);
	struct payload_mm_authvar_route_plan old_plan = plan_for(
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB);
	struct payload_mm_authvar_private_trust_decision decision;
	uint8_t database[4096];
	uint8_t malformed[] = { 5, 0, 0, 0, 0 };
	size_t database_size;

	content_spans(&serialized, content, false);
	content_spans(&append_serialized, append_content, true);
	/* Absent empty APPEND authenticates but ignores absent or broken certdb. */
	reset_index();
	assert(verify(&owner, &no_cn_append, append_content, &new_plan,
		&decision) == PAYLOAD_MM_VERIFY_OK);
	assert(!decision.new_binding_size);
	add_record(certdb_guid, certdb_name, sizeof(certdb_name), 1U, malformed,
		sizeof(malformed));
	assert(verify(&owner, &no_cn_append, append_content, &new_plan,
		&decision) == PAYLOAD_MM_VERIFY_OK);
	assert(!decision.new_binding_size);

	/* Existing empty APPEND still proves the fixed signer binding. */
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, empty_certdb, sizeof(empty_certdb));
	assert(verify(&owner, &cms, content, &new_plan, &decision) ==
		PAYLOAD_MM_VERIFY_OK);
	database_size = certdb_with_binding(decision.new_binding,
		decision.new_binding_size, database, sizeof(database));
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, database, database_size);
	add_record(vendor_guid, stored_name, sizeof(stored_name),
		REQUEST_ATTRIBUTES, "x", 1U);
	assert(verify(&owner, &append_cms, append_content, &old_plan,
		&decision) == PAYLOAD_MM_VERIFY_OK);
	database[database_size - 1U] ^= 1U;
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, database, database_size);
	add_record(vendor_guid, stored_name, sizeof(stored_name),
		REQUEST_ATTRIBUTES, "x", 1U);
	expect_failure(PAYLOAD_MM_VERIFY_REJECTED, &owner, &append_cms,
		append_content, &old_plan);

	/* Target lookup is the complete GUID/name pair, including stored NUL. */
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, empty_certdb, sizeof(empty_certdb));
	add_record(other_guid, stored_name, sizeof(stored_name),
		REQUEST_ATTRIBUTES, "x", 1U);
	assert(verify(&owner, &cms, content, &new_plan, &decision) ==
		PAYLOAD_MM_VERIFY_OK);
	reset_index();
	add_record(certdb_guid, certdb_name, sizeof(certdb_name),
		CERTDB_ATTRIBUTES, empty_certdb, sizeof(empty_certdb));
	add_record(vendor_guid, other_name, sizeof(other_name), REQUEST_ATTRIBUTES,
		"x", 1U);
	assert(verify(&owner, &cms, content, &new_plan, &decision) ==
		PAYLOAD_MM_VERIFY_OK);

	free(no_cn_append.data);
	free(append_cms.data);
	free(append_serialized.data);
	free(cms.data);
	free(serialized.data);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	native_algorithms(argv[1]);
	empty_new_ignores_certdb(argv[1]);
	legacy_and_certdb_edges(argv[1]);
	fail_closed_and_sealed(argv[1]);
	invalid_contract_and_cms(argv[1]);
	append_empty_and_near_keys(argv[1]);
	return 0;
}
