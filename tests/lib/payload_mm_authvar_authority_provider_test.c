/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_private_trust.h>
#include <boot/payload_mm_authvar_trust_store.h>
#include <commonlib/helpers.h>

#include "../../src/lib/payload_mm_authvar_authority_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define expect(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #condition); \
		abort(); \
	} \
} while (0)

static enum payload_mm_verify_status private_status;
static enum payload_mm_verify_status trust_status;
static enum payload_mm_authvar_authority private_authority;
static enum payload_mm_authvar_authority trust_authority;
static unsigned int private_calls;
static unsigned int trust_calls;
static size_t private_binding_size;
static bool private_binding_tail;
static bool mutate_request;
static bool mutate_new_payload;

struct provider_fixture {
	struct payload_mm_authvar_authority_verify_request request;
	struct payload_mm_crypto_owner owner;
	struct payload_mm_crypto_span pkcs7;
	struct payload_mm_crypto_span content[
		PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS];
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_store_entry entries[1];
	struct payload_mm_authvar_route_plan route;
	struct payload_mm_crypto_span new_payload;
	uint8_t pkcs7_data[1];
	uint8_t content_data[PAYLOAD_MM_AUTHVAR_AUTHORITY_SIGNED_SPANS][4];
	uint8_t store[PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE];
};

static struct provider_fixture fixture;

static void fixture_init(void)
{
	memset(&fixture, 0, sizeof(fixture));
	fixture.pkcs7 = (struct payload_mm_crypto_span) {
		.data = fixture.pkcs7_data,
		.size = sizeof(fixture.pkcs7_data),
	};
	for (size_t i = 0U; i < ARRAY_SIZE(fixture.content); i++)
		fixture.content[i] = (struct payload_mm_crypto_span) {
			.data = fixture.content_data[i],
			.size = sizeof(fixture.content_data[i]),
		};
	fixture.new_payload = fixture.content[4];
	fixture.index = (struct payload_mm_authvar_store_index) {
		.store = fixture.store,
		.store_size = sizeof(fixture.store),
		.used_size = sizeof(fixture.store),
		.entries = fixture.entries,
		.entry_capacity = ARRAY_SIZE(fixture.entries),
		.maximum_name_size = 128U,
		.maximum_data_size = 128U,
		.maximum_records = ARRAY_SIZE(fixture.entries),
	};
	fixture.route = (struct payload_mm_authvar_route_plan) {
		.target = PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE,
		.authorities = {
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER,
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE,
		},
		.authority_count = 1U,
	};
	fixture.content_data[2][0] = 0x21U;
	fixture.request = (struct payload_mm_authvar_authority_verify_request) {
		.owner = &fixture.owner,
		.pkcs7 = &fixture.pkcs7,
		.content = fixture.content,
		.content_count = ARRAY_SIZE(fixture.content),
		.index = &fixture.index,
		.route = &fixture.route,
		.new_payload = &fixture.new_payload,
	};
}

enum payload_mm_verify_status payload_mm_authvar_private_trust_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	struct payload_mm_authvar_authority_verification *verification)
{
	(void)owner;
	(void)signed_data;
	(void)content;
	(void)content_count;
	(void)index;
	(void)plan;
	private_calls++;
	expect(owner == fixture.request.owner &&
		signed_data == fixture.request.pkcs7 &&
		content == fixture.request.content &&
		content_count == fixture.request.content_count &&
		index == fixture.request.index && plan == fixture.request.route);
	memset(verification, 0xa5, sizeof(*verification));
	verification->accepted_authority = private_authority;
	verification->new_binding_size = private_binding_size;
	memset(verification->new_binding, 0,
		sizeof(verification->new_binding));
	if (private_binding_size <= sizeof(verification->new_binding))
		memset(verification->new_binding, 0x5a, private_binding_size);
	if (private_binding_tail && private_binding_size <
		sizeof(verification->new_binding))
		verification->new_binding[private_binding_size] = 1U;
	if (mutate_request)
		((struct payload_mm_authvar_authority_verify_request *)(uintptr_t)
			&fixture.request)->content_count--;
	if (mutate_new_payload)
		((struct payload_mm_crypto_span *)(uintptr_t)
			fixture.request.new_payload)->size--;
	return private_status;
}

enum payload_mm_verify_status payload_mm_authvar_trust_store_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	enum payload_mm_authvar_authority *accepted_authority)
{
	(void)owner;
	(void)signed_data;
	(void)content;
	(void)content_count;
	(void)index;
	(void)plan;
	trust_calls++;
	expect(owner == fixture.request.owner &&
		signed_data == fixture.request.pkcs7 &&
		content == fixture.request.content &&
		content_count == fixture.request.content_count &&
		index == fixture.request.index && plan == fixture.request.route);
	*accepted_authority = trust_authority;
	if (mutate_request)
		((struct payload_mm_authvar_authority_verify_request *)(uintptr_t)
			&fixture.request)->content_count--;
	if (mutate_new_payload)
		((struct payload_mm_crypto_span *)(uintptr_t)
			fixture.request.new_payload)->size--;
	return trust_status;
}

static void reset(void)
{
	fixture_init();
	private_status = PAYLOAD_MM_VERIFY_OK;
	trust_status = PAYLOAD_MM_VERIFY_OK;
	private_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	trust_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	private_calls = 0U;
	trust_calls = 0U;
	private_binding_size = 32U;
	private_binding_tail = false;
	mutate_request = false;
	mutate_new_payload = false;
}

static bool result_zero(
	const struct payload_mm_authvar_authority_verification *result)
{
	const uint8_t *bytes = (const uint8_t *)result;

	for (size_t i = 0U; i < sizeof(*result); i++)
		if (bytes[i])
			return false;
	return true;
}

static void private_matrix(void)
{
	static const enum payload_mm_verify_status statuses[] = {
		PAYLOAD_MM_VERIFY_OK,
		PAYLOAD_MM_VERIFY_INVALID,
		PAYLOAD_MM_VERIFY_BUSY,
		PAYLOAD_MM_VERIFY_NO_MEMORY,
		PAYLOAD_MM_VERIFY_MALFORMED,
		PAYLOAD_MM_VERIFY_REJECTED,
		PAYLOAD_MM_VERIFY_UNSUPPORTED,
		PAYLOAD_MM_VERIFY_INTERNAL,
		PAYLOAD_MM_VERIFY_CHANGED,
	};
	static const enum payload_mm_verify_status expected[] = {
		PAYLOAD_MM_VERIFY_OK,
		PAYLOAD_MM_VERIFY_INVALID,
		PAYLOAD_MM_VERIFY_BUSY,
		PAYLOAD_MM_VERIFY_REJECTED,
		PAYLOAD_MM_VERIFY_REJECTED,
		PAYLOAD_MM_VERIFY_REJECTED,
		PAYLOAD_MM_VERIFY_REJECTED,
		PAYLOAD_MM_VERIFY_INTERNAL,
		PAYLOAD_MM_VERIFY_CHANGED,
	};
	struct payload_mm_authvar_authority_verification result;

	for (size_t i = 0U; i < ARRAY_SIZE(statuses); i++) {
		reset();
		private_status = statuses[i];
		memset(&result, 0xa5, sizeof(result));
		expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
			&result) ==
			expected[i]);
		expect(private_calls == 1U && trust_calls == 0U);
		if (expected[i] == PAYLOAD_MM_VERIFY_OK)
			expect(result.accepted_authority == private_authority &&
				result.new_binding_size == 32U &&
				result.new_binding[0] == 0x5a);
		else
			expect(result_zero(&result));
	}

	/* The persistent-only profile rejects volatile Auth2 before certdb access. */
	reset();
	fixture.content_data[2][0] = 0x20U;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) ==
		PAYLOAD_MM_VERIFY_REJECTED);
	expect(!private_calls && !trust_calls && result_zero(&result));
	reset();
	fixture.route.authorities[0] =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	fixture.content_data[2][0] = 0x20U;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_REJECTED);
	expect(!private_calls && !trust_calls && result_zero(&result));
}

static void trust_matrix(void)
{
	struct payload_mm_authvar_authority_verification result;

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_PK;
	fixture.route.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	fixture.route.require_signature_list = true;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) ==
		PAYLOAD_MM_VERIFY_OK);
	expect(!private_calls && trust_calls == 1U &&
		result.accepted_authority == trust_authority &&
		!result.new_binding_size && !result.new_binding[0]);

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_PK;
	fixture.route.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	fixture.route.require_signature_list = true;
	trust_status = PAYLOAD_MM_VERIFY_NO_MEMORY;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) ==
		PAYLOAD_MM_VERIFY_NO_MEMORY);
	expect(!private_calls && trust_calls == 1U && result_zero(&result));
}

static void private_tuple_matrix(void)
{
	static const size_t good_sizes[] = { 32U, 48U, 64U };
	static const size_t bad_sizes[] = { 1U, 31U, 33U, 47U, 49U, 63U, 65U };
	struct payload_mm_authvar_authority_verification result;

	for (size_t i = 0U; i < ARRAY_SIZE(good_sizes); i++) {
		reset();
		private_binding_size = good_sizes[i];
		memset(&result, 0xa5, sizeof(result));
		expect(payload_mm_authvar_authority_provider_verify(NULL,
			&fixture.request, &result) == PAYLOAD_MM_VERIFY_OK);
		expect(private_calls == 1U && result.new_binding_size ==
			good_sizes[i] && result.new_binding[good_sizes[i] - 1U] == 0x5a);
	}
	for (size_t i = 0U; i < ARRAY_SIZE(bad_sizes); i++) {
		reset();
		private_binding_size = bad_sizes[i];
		memset(&result, 0xa5, sizeof(result));
		expect(payload_mm_authvar_authority_provider_verify(NULL,
			&fixture.request, &result) == PAYLOAD_MM_VERIFY_INTERNAL);
		expect(private_calls == 1U && !trust_calls && result_zero(&result));
	}
	reset();
	private_binding_tail = true;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INTERNAL);
	expect(private_calls == 1U && result_zero(&result));

	reset();
	private_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INTERNAL);
	expect(private_calls == 1U && result_zero(&result));

	reset();
	fixture.route.authorities[0] =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	private_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	private_binding_size = 0U;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_OK);
	expect(private_calls == 1U && result.accepted_authority ==
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB &&
		!result.new_binding_size && !result.new_binding[0]);

	reset();
	fixture.content[4].size = 0U;
	fixture.new_payload = fixture.content[4];
	private_binding_size = 32U;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INTERNAL);
	expect(private_calls == 1U && result_zero(&result));

	reset();
	fixture.content[4].size = 0U;
	fixture.new_payload = fixture.content[4];
	private_binding_size = 0U;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_OK);
	expect(private_calls == 1U && !result.new_binding_size);
}

static void route_and_seal_matrix(void)
{
	struct payload_mm_authvar_authority_verification result;
	struct payload_mm_authvar_authority_verify_request saved_request;
	struct payload_mm_crypto_span saved_payload;

	reset();
	private_status = (enum payload_mm_verify_status)99;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INTERNAL);
	expect(private_calls == 1U && result_zero(&result));

	reset();
	mutate_request = true;
	saved_request = fixture.request;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_CHANGED);
	expect(private_calls == 1U && result_zero(&result));
	fixture.request = saved_request;

	reset();
	mutate_new_payload = true;
	saved_payload = fixture.new_payload;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_CHANGED);
	expect(private_calls == 1U && result_zero(&result));
	fixture.new_payload = saved_payload;

	reset();
	fixture.route.require_signature_list = true;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls && result_zero(&result));

	reset();
	fixture.route.authority_count = 2U;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls && result_zero(&result));

	reset();
	fixture.route.authorities[1] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls && result_zero(&result));

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_PK;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls && result_zero(&result));

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_PK;
	fixture.route.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	/* Missing the canonical signature-list flag is an invalid trusted route. */
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls && result_zero(&result));

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_PK;
	fixture.route.authorities[0] =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PAYLOAD_CERT;
	fixture.route.require_signature_list = true;
	fixture.route.enter_user_mode = true;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_UNSUPPORTED);
	expect(!private_calls && !trust_calls && result_zero(&result));

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_DB;
	fixture.route.authorities[0] =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PAYLOAD_CERT;
	fixture.route.require_signature_list = true;
	fixture.route.enter_user_mode = true;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls && result_zero(&result));

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_PK;
	fixture.route.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	fixture.route.require_signature_list = true;
	trust_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INTERNAL);
	expect(!private_calls && trust_calls == 1U && result_zero(&result));

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_PK;
	fixture.route.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	fixture.route.require_signature_list = true;
	mutate_request = true;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_CHANGED);
	expect(!private_calls && trust_calls == 1U && result_zero(&result));

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_DB;
	fixture.route.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	fixture.route.authorities[1] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK;
	fixture.route.authority_count = 2U;
	fixture.route.require_signature_list = true;
	trust_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_OK);
	expect(trust_calls == 1U && result.accepted_authority == trust_authority);

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_DB;
	fixture.route.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK;
	fixture.route.authorities[1] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	fixture.route.authority_count = 2U;
	fixture.route.require_signature_list = true;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls && result_zero(&result));

	reset();
	fixture.route.target = PAYLOAD_MM_AUTHVAR_TARGET_PK;
	fixture.route.authorities[0] = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	fixture.route.require_signature_list = true;
	fixture.content[4].size = 0U;
	fixture.new_payload = fixture.content[4];
	fixture.route.enter_setup_mode = true;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_OK);
	expect(trust_calls == 1U);
}

static void alias_matrix(void)
{
	struct payload_mm_authvar_authority_verification result;
	uint8_t before[sizeof(fixture.request)];

	reset();
	memcpy(before, &fixture.request, sizeof(before));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		(struct payload_mm_authvar_authority_verification *)(void *)
			&fixture.request) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!memcmp(before, &fixture.request, sizeof(before)) &&
		!private_calls && !trust_calls);

	reset();
	fixture.content[1].data = fixture.content[0].data;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls);
	/* Unsafe/alias admission leaves the caller's output untouched. */
	for (size_t i = 0U; i < sizeof(result); i++)
		expect(((const uint8_t *)&result)[i] == 0xa5U);

	reset();
	fixture.content[1].data = fixture.content[0].data + 1U;
	fixture.content[1].size = fixture.content[0].size - 1U;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls);
	for (size_t i = 0U; i < sizeof(result); i++)
		expect(((const uint8_t *)&result)[i] == 0xa5U);

	reset();
	fixture.new_payload.data = fixture.content_data[3];
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls);
	for (size_t i = 0U; i < sizeof(result); i++)
		expect(((const uint8_t *)&result)[i] == 0xa5U);

	reset();
	fixture.new_payload.size--;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls);
	for (size_t i = 0U; i < sizeof(result); i++)
		expect(((const uint8_t *)&result)[i] == 0xa5U);
}

static void invalid_inputs(void)
{
	struct payload_mm_authvar_authority_verification result;

	reset();
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(&fixture.request,
		&fixture.request, &result) ==
		PAYLOAD_MM_VERIFY_INVALID);
	expect(result_zero(&result));
	reset();
	fixture.content_data[2][0] = 0x20U;
	fixture.index.maximum_records = 0U;
	memset(&result, 0xa5, sizeof(result));
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		&result) == PAYLOAD_MM_VERIFY_INVALID);
	expect(result_zero(&result) && !private_calls && !trust_calls);
	expect(payload_mm_authvar_authority_provider_verify(NULL, NULL, &result) ==
		PAYLOAD_MM_VERIFY_INVALID);
	expect(payload_mm_authvar_authority_provider_verify(NULL, &fixture.request,
		NULL) ==
		PAYLOAD_MM_VERIFY_INVALID);
	expect(!private_calls && !trust_calls);
}

int main(void)
{
	private_matrix();
	trust_matrix();
	private_tuple_matrix();
	route_and_seal_matrix();
	alias_matrix();
	invalid_inputs();
	puts("Payload-MM authenticated-variable provider tests: PASS");
	return 0;
}
