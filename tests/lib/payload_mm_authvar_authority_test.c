/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_authority.h>
#include <boot/payload_mm_authvar_signature_db.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define expect(c) do { \
	if (!(c)) { \
		fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #c); \
		abort(); \
	} \
} while (0)
#define AUTH2_SIZE 41U

static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t image_guid[16] = {
	0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
	0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
};
static const uint8_t private_guid[16] = { 1 };
static const uint8_t pk[] = { 'P', 0, 'K', 0, 0, 0 };
static const uint8_t db[] = { 'd', 0, 'b', 0, 0, 0 };
static const uint8_t private_name[] = { 'x', 0, 0, 0 };
static const uint8_t pkcs7_guid[16] = {
	0x9d, 0xd2, 0xaf, 0x4a, 0xdf, 0x68, 0xee, 0x49,
	0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7,
};

static struct payload_mm_crypto_owner owner;
static uint8_t store[256];
static struct payload_mm_authvar_store_entry entry;
static struct payload_mm_authvar_store_index store_index;
static uint8_t workspace[128];
static unsigned int verify_calls;
static unsigned int validate_calls;
static unsigned int filter_calls;
static enum payload_mm_verify_status verify_status;
static enum payload_mm_verify_status filter_status;
static enum payload_mm_authvar_authority verifier_authority;
static bool verify_before_validate;
static uint8_t mutate_verified_input;
static size_t workspace_capacity;

static void put16(uint8_t *p, uint16_t value)
{
	p[0] = value;
	p[1] = value >> 8;
}

static void put32(uint8_t *p, uint32_t value)
{
	for (size_t i = 0U; i < 4U; i++)
		p[i] = value >> (8U * i);
}

static void timestamp(uint8_t *time, uint8_t second)
{
	memset(time, 0, 16U);
	put16(time, 2026U);
	time[2] = 9U;
	time[3] = 23U;
	time[6] = second;
}

static size_t auth2(uint8_t *data, uint8_t second, const void *payload,
	size_t payload_size)
{
	memset(data, 0, 41U + payload_size);
	timestamp(data, second);
	put32(data + 16U, 25U);
	put16(data + 20U, 0x0200U);
	put16(data + 22U, 0x0ef1U);
	memcpy(data + 24U, pkcs7_guid, 16U);
	data[40] = 0x30U;
	if (payload_size)
		memcpy(data + 41U, payload, payload_size);
	return 41U + payload_size;
}

enum payload_mm_verify_status payload_mm_authvar_signature_db_validate(
	struct payload_mm_crypto_owner *crypto_owner, const void *data, size_t size,
	enum payload_mm_authvar_signature_db_profile profile,
	uint32_t maximum_x509, struct payload_mm_authvar_signature_db_stats *stats)
{
	(void)profile;
	(void)maximum_x509;
	(void)stats;
	expect(crypto_owner == &owner && data && size);
	if (verify_before_validate)
		expect(verify_calls == 1U);
	validate_calls++;
	return PAYLOAD_MM_VERIFY_OK;
}

enum payload_mm_verify_status payload_mm_authvar_signature_db_filter_append(
	struct payload_mm_crypto_owner *crypto_owner,
	const void *current, size_t current_size,
	const void *append, size_t append_size,
	void *output, size_t output_capacity, size_t *output_size)
{
	expect(crypto_owner == &owner && current && current_size && append);
	if (filter_status != PAYLOAD_MM_VERIFY_OK)
		return filter_status;
	if (output_capacity < append_size)
		return PAYLOAD_MM_VERIFY_NO_MEMORY;
	memcpy(output, append, append_size);
	*output_size = append_size;
	filter_calls++;
	return PAYLOAD_MM_VERIFY_OK;
}

static enum payload_mm_verify_status verify(void *context,
	const struct payload_mm_authvar_authority_verify_request *request,
	enum payload_mm_authvar_authority *accepted)
{
	const struct payload_mm_authvar_policy_request *original = context;
	const uint8_t *attributes = request->content[2].data;

	expect(request->owner == &owner && request->content_count == 5U);
	expect(request->content[0].size == original->name_size - 2U);
	expect(!memcmp(request->content[0].data, original->name,
		original->name_size - 2U));
	expect(request->content[1].size == 16U &&
		!memcmp(request->content[1].data, original->vendor_guid, 16U));
	expect(request->content[2].size == 4U && attributes[0] ==
		(uint8_t)original->attributes && attributes[1] ==
		(uint8_t)(original->attributes >> 8));
	expect(request->content[3].size == 16U);
	expect(!memcmp(request->content[3].data, original->data, 16U));
	expect(request->content[4].size == request->new_payload->size &&
		!memcmp(request->content[4].data,
			(const uint8_t *)original->data + 41U,
			request->content[4].size));
	expect(request->pkcs7->size == 1U && request->pkcs7->data[0] == 0x30U);
	verify_calls++;
	*accepted = verifier_authority;
	if (mutate_verified_input == 1U)
		((uint8_t *)original->name)[0] ^= 1U;
	else if (mutate_verified_input == 2U)
		((struct payload_mm_crypto_span *)request->content)[0].size++;
	else if (mutate_verified_input == 3U)
		((struct payload_mm_authvar_route_plan *)request->route)->target =
			PAYLOAD_MM_AUTHVAR_TARGET_PK;
	else if (mutate_verified_input == 4U)
		((uint8_t *)request->index->store)[0] ^= 1U;
	return verify_status;
}

enum payload_mm_verify_status payload_mm_sha256(const void *message,
	size_t message_size, uint8_t digest[PAYLOAD_MM_SHA256_SIZE])
{
	const uint8_t *bytes = message;
	uint32_t hash = 2166136261U;

	for (size_t i = 0U; i < message_size; i++)
		hash = (hash ^ bytes[i]) * 16777619U;
	for (size_t i = 0U; i < PAYLOAD_MM_SHA256_SIZE; i++)
		digest[i] = (uint8_t)(hash >> ((i & 3U) * 8U)) ^ (uint8_t)i;
	return PAYLOAD_MM_VERIFY_OK;
}

static void reset_index(void)
{
	memset(&owner, 0, sizeof(owner));
	memset(store, 0, sizeof(store));
	memset(&entry, 0, sizeof(entry));
	store_index = (struct payload_mm_authvar_store_index) {
		.store = store,
		.store_size = sizeof(store),
		.used_size = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		.entries = &entry,
		.entry_capacity = 1U,
		.maximum_name_size = 64U,
		.maximum_data_size = 128U,
		.maximum_records = 4U,
	};
	verify_calls = validate_calls = filter_calls = 0U;
	verify_status = PAYLOAD_MM_VERIFY_OK;
	filter_status = PAYLOAD_MM_VERIFY_OK;
	verify_before_validate = false;
	mutate_verified_input = 0U;
	workspace_capacity = sizeof(workspace);
}

static void add_existing(const uint8_t guid[16], const uint8_t *name,
	size_t name_size, uint32_t attributes, uint8_t second)
{
	entry = (struct payload_mm_authvar_store_entry) {
		.record_offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		.name_offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
			PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE,
		.name_size = name_size,
		.data_offset = (PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
			PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + name_size + 3U) &
			~3U,
		.data_size = 8U,
		.attributes = attributes & ~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND,
	};
	memcpy(entry.vendor_guid, guid, 16U);
	put16(store + entry.record_offset, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	store[entry.record_offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	put32(store + entry.record_offset + 4U, entry.attributes);
	timestamp(store + entry.record_offset + 16U, second);
	put32(store + entry.record_offset + 36U, entry.name_size);
	put32(store + entry.record_offset + 40U, entry.data_size);
	memcpy(store + entry.record_offset + 44U, guid, 16U);
	memcpy(store + entry.name_offset, name, name_size);
	memset(store + entry.data_offset, 0x55, entry.data_size);
	store_index.entry_count = store_index.record_count = 1U;
	store_index.used_size = entry.data_offset + entry.data_size;
}

static enum payload_mm_verify_status decide(
	struct payload_mm_authvar_policy_request *request,
	struct payload_mm_authvar_authority_facts facts,
	struct payload_mm_authvar_authority_decision *decision)
{
	const struct payload_mm_authvar_authority_snapshot snapshot = {
		.request = request,
		.index = &store_index,
		.owner = &owner,
		.facts = facts,
		.verify = verify,
		.verify_context = request,
		.append_workspace = workspace,
		.append_workspace_size = workspace_capacity,
	};

	return payload_mm_authvar_authority_decide(&snapshot, decision);
}

static struct payload_mm_authvar_policy_request request_for(
	const uint8_t guid[16], const uint8_t *name, size_t name_size,
	uint32_t attributes, void *data, size_t data_size)
{
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = attributes,
		.name = name,
		.name_size = name_size,
		.data = data,
		.data_size = data_size,
	};

	memcpy(request.vendor_guid, guid, 16U);
	return request;
}

int main(void)
{
	const uint32_t secure = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH;
	uint8_t data[128], payload[8] = { 1, 2, 3, 4 };
	struct payload_mm_authvar_policy_request request;
	struct payload_mm_authvar_authority_decision decision;
	size_t size;

	/* Setup-mode bypass still validates payload format but skips trust. */
	reset_index();
	size = auth2(data, 1U, payload, sizeof(payload));
	request = request_for(image_guid, db, sizeof(db), secure, data, size);
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) {
		.setup_mode = true,
	}, &decision) == PAYLOAD_MM_VERIFY_OK);
	expect(!verify_calls && validate_calls == 1U);
	expect(decision.mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_WRITE &&
		decision.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		decision.data_size == sizeof(payload) && !decision.intents &&
		decision.accepted_authority == PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS);

	/* A real custom+presence bypass marks vendor keys only after mutation. */
	reset_index();
	size = auth2(data, 1U, payload, sizeof(payload));
	request = request_for(image_guid, db, sizeof(db), secure, data, size);
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) {
		.custom_mode = true, .trusted_physical_presence = true,
	}, &decision) == PAYLOAD_MM_VERIFY_OK);
	expect(!verify_calls && decision.outcome ==
		PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		decision.intents == PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS);

	/* User db permits the ordered KEK fallback and signs exact five spans. */
	reset_index();
	verify_before_validate = true;
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK;
	request = request_for(image_guid, db, sizeof(db), secure, data, size);
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_OK);
	expect(verify_calls == 1U && decision.accepted_authority ==
		PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK);
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_REJECTED);

	/* Existing non-append requires a strictly later timestamp. */
	reset_index();
	add_existing(image_guid, db, sizeof(db), secure, 4U);
	size = auth2(data, 4U, payload, sizeof(payload));
	request = request_for(image_guid, db, sizeof(db), secure, data, size);
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_REJECTED && !verify_calls);

	/* APPEND skips replay, retains max timestamp, and filters databases. */
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_OK);
	expect(filter_calls == 1U && decision.mutation.timestamp[6] == 4U &&
		decision.data == workspace &&
		!(decision.mutation.attributes & PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND) &&
		decision.data_size == entry.data_size + sizeof(payload));

	/* Insufficient append tail capacity changes neither workspace nor output. */
	reset_index();
	add_existing(image_guid, db, sizeof(db), secure, 1U);
	size = auth2(data, 2U, payload, sizeof(payload));
	request = request_for(image_guid, db, sizeof(db),
		secure | PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND, data, size);
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	workspace_capacity = entry.data_size + sizeof(payload) - 1U;
	memset(workspace, 0xa5, sizeof(workspace));
	memset(&decision, 0xa5, sizeof(decision));
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_NO_MEMORY);
	expect(!decision.outcome && !decision.mutation.kind &&
		!memcmp(workspace, (uint8_t[128]) { [0 ... 127] = 0xa5 },
			sizeof(workspace)));

	/* A filter error is failure-atomic for both decision and workspace. */
	reset_index();
	add_existing(image_guid, db, sizeof(db), secure, 1U);
	size = auth2(data, 2U, payload, sizeof(payload));
	request = request_for(image_guid, db, sizeof(db),
		secure | PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND, data, size);
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	filter_status = PAYLOAD_MM_VERIFY_INTERNAL;
	memset(workspace, 0x3c, sizeof(workspace));
	memset(&decision, 0xa5, sizeof(decision));
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_INTERNAL);
	expect(!decision.outcome && !decision.mutation.kind && !decision.data &&
		!memcmp(workspace, (uint8_t[128]) { [0 ... 127] = 0x3c },
			sizeof(workspace)));

	/* Empty APPEND is success with no mutation or derived-state intent. */
	reset_index();
	size = auth2(data, 2U, NULL, 0U);
	request = request_for(global_guid, pk, sizeof(pk),
		secure | PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND, data, size);
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) {
		.setup_mode = true,
	}, &decision) == PAYLOAD_MM_VERIFY_REJECTED); /* Native PK hardening. */
	request = request_for(image_guid, db, sizeof(db),
		secure | PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND, data, size);
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) {
		.custom_mode = true, .trusted_physical_presence = true,
	}, &decision) == PAYLOAD_MM_VERIFY_OK);
	expect(decision.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP &&
		!decision.mutation.kind && !decision.intents && !verify_calls);

	/* Absent non-append empty payload authenticates but cannot create a record. */
	reset_index();
	size = auth2(data, 2U, NULL, 0U);
	request = request_for(private_guid, private_name, sizeof(private_name),
		secure, data, size);
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	memset(&decision, 0xa5, sizeof(decision));
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_OK && verify_calls == 1U);
	expect(decision.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND &&
		!decision.mutation.kind && !decision.intents && !decision.data);

	/* Existing empty APPEND preserves value and advances only the timestamp. */
	reset_index();
	add_existing(image_guid, db, sizeof(db), secure, 1U);
	size = auth2(data, 3U, NULL, 0U);
	request = request_for(image_guid, db, sizeof(db),
		secure | PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND, data, size);
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_OK);
	expect(decision.mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_WRITE &&
		decision.mutation.timestamp[6] == 3U &&
		decision.data_size == entry.data_size &&
		!memcmp(decision.data, store + entry.data_offset, entry.data_size));

	/* Self-signed setup PK enrollment emits only the later atomic mode intent. */
	reset_index();
	size = auth2(data, 3U, payload, sizeof(payload));
	request = request_for(global_guid, pk, sizeof(pk), secure, data, size);
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PAYLOAD_CERT;
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) {
		.setup_mode = true, .require_self_signed_pk = true,
	}, &decision) == PAYLOAD_MM_VERIFY_OK);
	expect(decision.intents == PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE);

	/* Signed PK deletion returns target delete plus enter-setup intent. */
	reset_index();
	add_existing(global_guid, pk, sizeof(pk), secure, 1U);
	size = auth2(data, 2U, NULL, 0U);
	request = request_for(global_guid, pk, sizeof(pk), secure, data, size);
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_OK);
	expect(decision.mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_DELETE &&
		decision.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		!decision.mutation.attributes && !decision.mutation.data_size &&
		!memcmp(decision.mutation.timestamp, (uint8_t[16]) { 0 }, 16U) &&
		decision.intents == PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE);

	/* Existing private delete removes its certdb binding in the same commit. */
	reset_index();
	add_existing(private_guid, private_name, sizeof(private_name), secure, 1U);
	size = auth2(data, 2U, NULL, 0U);
	request = request_for(private_guid, private_name, sizeof(private_name),
		secure, data, size);
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_OK);
	expect(decision.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		decision.mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_DELETE &&
		!decision.mutation.attributes && !decision.mutation.data_size &&
		!memcmp(decision.mutation.timestamp, (uint8_t[16]) { 0 }, 16U) &&
		decision.intents ==
			PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING);

	/* Private authenticated variables carry certdb composition intent. */
	reset_index();
	size = auth2(data, 2U, payload, sizeof(payload));
	request = request_for(private_guid, private_name, sizeof(private_name),
		secure, data, size);
	verifier_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_OK);
	expect(decision.intents == PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING &&
		!validate_calls);

	/* Zero timestamp, malformed metadata, and verifier failure stay closed. */
	reset_index();
	size = auth2(data, 2U, payload, sizeof(payload));
	memset(data, 0, 16U);
	request = request_for(private_guid, private_name, sizeof(private_name),
		secure, data, size);
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_MALFORMED);
	timestamp(data, 2U);
	data[24] ^= 1U;
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_MALFORMED);
	data[24] ^= 1U;
	verify_status = PAYLOAD_MM_VERIFY_NO_MEMORY;
	memset(&decision, 0xa5, sizeof(decision));
	expect(decide(&request, (struct payload_mm_authvar_authority_facts) { 0 },
		&decision) == PAYLOAD_MM_VERIFY_NO_MEMORY);
	expect(!decision.mutation.kind && !decision.intents && !decision.data);

	/* Every protected input/output span must be pairwise disjoint. */
	verify_status = PAYLOAD_MM_VERIFY_OK;
	/* A verifier cannot alter protected bytes or its local descriptors. */
	{
		uint8_t mutable_name[sizeof(private_name)];

		memcpy(mutable_name, private_name, sizeof(mutable_name));
		for (uint8_t mutation = 1U; mutation <= 4U; mutation++) {
			reset_index();
			size = auth2(data, 2U, payload, sizeof(payload));
			request = request_for(private_guid, mutable_name,
				sizeof(mutable_name), secure, data, size);
			verifier_authority =
				PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
			mutate_verified_input = mutation;
			memset(&decision, 0xa5, sizeof(decision));
			expect(decide(&request,
				(struct payload_mm_authvar_authority_facts) { 0 },
				&decision) == PAYLOAD_MM_VERIFY_CHANGED);
			expect(!decision.outcome && !decision.mutation.kind);
			if (mutation == 1U)
				mutable_name[0] ^= 1U;
			else if (mutation == 4U)
				store[0] ^= 1U;
		}
		for (size_t failure = 0U; failure < 2U; failure++) {
			static const enum payload_mm_verify_status failures[] = {
				PAYLOAD_MM_VERIFY_REJECTED,
				PAYLOAD_MM_VERIFY_NO_MEMORY,
			};

			reset_index();
			size = auth2(data, 2U, payload, sizeof(payload));
			request = request_for(private_guid, mutable_name,
				sizeof(mutable_name), secure, data, size);
			verifier_authority =
				PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
			mutate_verified_input = 1U;
			verify_status = failures[failure];
			expect(decide(&request,
				(struct payload_mm_authvar_authority_facts) { 0 },
				&decision) == PAYLOAD_MM_VERIFY_CHANGED);
			mutable_name[0] ^= 1U;
		}
	}

	{
		struct payload_mm_authvar_authority_snapshot snapshot = {
			.request = &request, .index = &store_index, .owner = &owner,
			.verify = verify, .verify_context = &request,
			.append_workspace = workspace,
			.append_workspace_size = sizeof(workspace),
		};
		struct payload_mm_authvar_policy_request aliased = request;

		expect(payload_mm_authvar_authority_decide(&snapshot,
			(void *)&snapshot) == PAYLOAD_MM_VERIFY_INVALID);
		snapshot.append_workspace = (void *)aliased.data;
		snapshot.append_workspace_size = aliased.data_size;
		snapshot.request = &aliased;
		expect(payload_mm_authvar_authority_decide(&snapshot, &decision) ==
			PAYLOAD_MM_VERIFY_INVALID);
		snapshot.append_workspace = workspace;
		snapshot.append_workspace_size = sizeof(workspace);
		aliased.name = aliased.data;
		aliased.name_size = 4U;
		expect(payload_mm_authvar_authority_decide(&snapshot, &decision) ==
			PAYLOAD_MM_VERIFY_INVALID);
		aliased = request;
		aliased.data = (const void *)(UINTPTR_MAX - 1U);
		aliased.data_size = 4U;
		expect(payload_mm_authvar_authority_decide(&snapshot, &decision) ==
			PAYLOAD_MM_VERIFY_INVALID);
		aliased = request;
		aliased.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET;
		expect(payload_mm_authvar_authority_decide(&snapshot, &decision) ==
			PAYLOAD_MM_VERIFY_INVALID);
		aliased.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET;
		aliased.attributes |= PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE;
		expect(payload_mm_authvar_authority_decide(&snapshot, &decision) ==
			PAYLOAD_MM_VERIFY_UNSUPPORTED);
		aliased = request;
		{
			struct payload_mm_authvar_store_index overlapping_index =
				store_index;

			overlapping_index.store = (const uint8_t *)&overlapping_index;
			overlapping_index.store_size = sizeof(overlapping_index);
			overlapping_index.used_size = 0U;
			overlapping_index.entry_count = 0U;
			snapshot.index = &overlapping_index;
			expect(payload_mm_authvar_authority_decide(&snapshot, &decision) ==
				PAYLOAD_MM_VERIFY_INVALID);
		}
		snapshot.index = &store_index;
		{
			struct payload_mm_authvar_authority_snapshot *overlapping_snapshot =
				(void *)&owner;

			*overlapping_snapshot = snapshot;
			expect(payload_mm_authvar_authority_decide(overlapping_snapshot,
				&decision) == PAYLOAD_MM_VERIFY_INVALID);
		}
	}

	puts("Payload-MM authenticated-variable authority tests: PASS");
	return 0;
}
