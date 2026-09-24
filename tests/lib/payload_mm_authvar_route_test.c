/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_route.h>
#include <commonlib/helpers.h>
#include <stdlib.h>
#include <string.h>

#define assert(c) do { if (!(c)) abort(); } while (0)

#define SECURE_ATTRIBUTES \
	(PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE | \
	 PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH)

static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t image_security_guid[16] = {
	0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
	0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
};
static const uint8_t private_guid[16] = {
	0x32, 0x43, 0x54, 0x65, 0x76, 0x87, 0x98, 0xa9,
	0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f, 0x10, 0x21,
};
static const uint8_t pk_name[] = { 'P', 0, 'K', 0 };
static const uint8_t kek_name[] = { 'K', 0, 'E', 0, 'K', 0 };
static const uint8_t db_name[] = { 'd', 0, 'b', 0 };
static const uint8_t dbx_name[] = { 'd', 0, 'b', 0, 'x', 0 };
static const uint8_t dbt_name[] = { 'd', 0, 'b', 0, 't', 0 };
static const uint8_t private_name[] = { 'P', 0, 'r', 0, 'i', 0, 'v', 0 };

static struct payload_mm_authvar_route_request request_for(
	const uint8_t guid[16], const void *name, size_t name_size)
{
	struct payload_mm_authvar_route_request request = {
		.name = name,
		.name_size = name_size,
		.attributes = SECURE_ATTRIBUTES,
		.payload_size = 1,
	};

	memcpy(request.vendor_guid, guid, sizeof(request.vendor_guid));
	return request;
}

static struct payload_mm_authvar_route_plan route_ok(
	const struct payload_mm_authvar_route_request *request)
{
	struct payload_mm_authvar_route_plan plan;

	memset(&plan, 0xa5, sizeof(plan));
	assert(payload_mm_authvar_route_plan(request, &plan) ==
		PAYLOAD_MM_AUTHVAR_ROUTE_OK);
	return plan;
}

static void route_fails(const struct payload_mm_authvar_route_request *request,
	enum payload_mm_authvar_route_result result)
{
	const struct payload_mm_authvar_route_plan zero = { 0 };
	struct payload_mm_authvar_route_plan plan;

	memset(&plan, 0xa5, sizeof(plan));
	assert(payload_mm_authvar_route_plan(request, &plan) == result);
	assert(!memcmp(&plan, &zero, sizeof(plan)));
}

static void assert_authorities(const struct payload_mm_authvar_route_plan *plan,
	enum payload_mm_authvar_authority first,
	enum payload_mm_authvar_authority second, uint8_t count)
{
	assert(plan->authority_count == count);
	assert(plan->authorities[0] == first);
	assert(plan->authorities[1] == second);
}

static void exact_target_classification(void)
{
	struct {
		const uint8_t *guid;
		const uint8_t *name;
		size_t name_size;
		enum payload_mm_authvar_target target;
	} const cases[] = {
		{ global_guid, pk_name, sizeof(pk_name), PAYLOAD_MM_AUTHVAR_TARGET_PK },
		{ global_guid, kek_name, sizeof(kek_name), PAYLOAD_MM_AUTHVAR_TARGET_KEK },
		{ image_security_guid, db_name, sizeof(db_name), PAYLOAD_MM_AUTHVAR_TARGET_DB },
		{ image_security_guid, dbx_name, sizeof(dbx_name), PAYLOAD_MM_AUTHVAR_TARGET_DBX },
		{ image_security_guid, dbt_name, sizeof(dbt_name), PAYLOAD_MM_AUTHVAR_TARGET_DBT },
	};
	struct payload_mm_authvar_route_request request;
	struct payload_mm_authvar_route_plan plan;

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		request = request_for(cases[i].guid, cases[i].name,
			cases[i].name_size);
		request.setup_mode = true;
		plan = route_ok(&request);
		assert(plan.target == cases[i].target);
		assert(plan.require_signature_list);
		assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS,
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	}

	/* A wrong GUID, case, length, trailing NUL, or one-byte mutation is private. */
	request = request_for(private_guid, pk_name, sizeof(pk_name));
	plan = route_ok(&request);
	assert(plan.target == PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE);
	assert(!plan.require_signature_list);

	request = request_for(global_guid, db_name, sizeof(db_name));
	plan = route_ok(&request);
	assert(plan.target == PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE);

	{
		uint8_t near_pk[] = { 'p', 0, 'K', 0 };
		request = request_for(global_guid, near_pk, sizeof(near_pk));
		plan = route_ok(&request);
		assert(plan.target == PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE);
	}
	request = request_for(global_guid, pk_name, sizeof(pk_name) - 1U);
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	{
		uint8_t terminated_pk[] = { 'P', 0, 'K', 0, 0, 0 };
		request = request_for(global_guid, terminated_pk, sizeof(terminated_pk));
		route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	}
	{
		uint8_t guid[16];
		memcpy(guid, global_guid, sizeof(guid));
		guid[15] ^= 1U;
		request = request_for(guid, pk_name, sizeof(pk_name));
		plan = route_ok(&request);
		assert(plan.target == PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE);
	}
}

static void secure_target_routes(void)
{
	struct payload_mm_authvar_route_request request;
	struct payload_mm_authvar_route_plan plan;

	/* User-mode PK and KEK updates chain to the current PK. */
	request = request_for(global_guid, pk_name, sizeof(pk_name));
	request.target_exists = true;
	plan = route_ok(&request);
	assert(plan.target == PAYLOAD_MM_AUTHVAR_TARGET_PK);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(!plan.mark_vendor_keys_modified);
	request = request_for(global_guid, kek_name, sizeof(kek_name));
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(!plan.mark_vendor_keys_modified);

	/* Image databases try PK first and KEK second, exactly in that order. */
	request = request_for(image_security_guid, db_name, sizeof(db_name));
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK, 2);
	assert(!plan.mark_vendor_keys_modified);
	request = request_for(image_security_guid, dbx_name, sizeof(dbx_name));
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK, 2);
	assert(!plan.mark_vendor_keys_modified);
	request = request_for(image_security_guid, dbt_name, sizeof(dbt_name));
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK, 2);
	assert(!plan.mark_vendor_keys_modified);

	/* Setup mode bypasses all secure targets, except self-signed PK policy. */
	request = request_for(global_guid, pk_name, sizeof(pk_name));
	request.setup_mode = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(plan.enter_user_mode && !plan.enter_setup_mode);
	request.require_self_signed_pk = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PAYLOAD_CERT,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(plan.enter_user_mode);
	request.custom_mode = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PAYLOAD_CERT,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(!plan.mark_vendor_keys_modified);
	request.custom_mode = false;
	request.trusted_physical_presence = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PAYLOAD_CERT,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	request.custom_mode = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(plan.mark_vendor_keys_modified);
	request = request_for(global_guid, kek_name, sizeof(kek_name));
	request.setup_mode = true;
	request.require_self_signed_pk = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(!plan.mark_vendor_keys_modified);

	/* Custom mode bypass needs trusted presence and records VendorKeys intent. */
	request = request_for(global_guid, kek_name, sizeof(kek_name));
	request.custom_mode = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(!plan.mark_vendor_keys_modified);
	request.custom_mode = false;
	request.trusted_physical_presence = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(!plan.mark_vendor_keys_modified);
	request.custom_mode = true;
	request.trusted_physical_presence = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(plan.mark_vendor_keys_modified);

	request = request_for(image_security_guid, db_name, sizeof(db_name));
	request.custom_mode = true;
	request.trusted_physical_presence = true;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_BYPASS,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(plan.mark_vendor_keys_modified);

	/* PK enrollment/deletion carries only the requested platform-mode intent. */
	request = request_for(global_guid, pk_name, sizeof(pk_name));
	request.setup_mode = true;
	request.payload_size = 0;
	plan = route_ok(&request);
	assert(plan.enter_user_mode && !plan.enter_setup_mode);
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	plan = route_ok(&request);
	assert(plan.enter_user_mode && !plan.enter_setup_mode);
	request.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	request.setup_mode = false;
	request.target_exists = true;
	plan = route_ok(&request);
	assert(!plan.enter_user_mode && plan.enter_setup_mode);
	request.target_exists = false;
	plan = route_ok(&request);
	assert(!plan.enter_user_mode && !plan.enter_setup_mode);
	request.payload_size = 1;
	plan = route_ok(&request);
	assert(!plan.enter_user_mode && !plan.enter_setup_mode);
	request.setup_mode = true;
	request.payload_size = 0;
	plan = route_ok(&request);
	assert(plan.enter_user_mode && !plan.enter_setup_mode);
}

static void secure_attribute_rules(void)
{
	struct payload_mm_authvar_route_request request =
		request_for(global_guid, pk_name, sizeof(pk_name));
	struct payload_mm_authvar_route_plan plan;

	request.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	request.attributes = SECURE_ATTRIBUTES |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	request.attributes = SECURE_ATTRIBUTES |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	plan = route_ok(&request);
	assert(plan.target == PAYLOAD_MM_AUTHVAR_TARGET_PK);
	assert(plan.require_signature_list);
}

static void private_routes(void)
{
	struct payload_mm_authvar_route_request request =
		request_for(private_guid, private_name, sizeof(private_name));
	struct payload_mm_authvar_route_plan plan;

	/* The first time-authenticated write establishes the private signer. */
	plan = route_ok(&request);
	assert(plan.target == PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	assert(!plan.require_signature_list);

	/* Existing time-authenticated variables use their certdb binding. */
	request.target_exists = true;
	request.existing_attributes = SECURE_ATTRIBUTES;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);

	/* Every existing time-authenticated target uses its certdb binding. */
	request.existing_attributes = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);

	/* Counter authentication is deliberately outside the supported profile. */
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_UNSUPPORTED);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE |
		PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_UNSUPPORTED);

	/* An authenticated existing variable cannot be overwritten unsigned. */
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE;
	request.existing_attributes = SECURE_ATTRIBUTES;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_WRITE_PROTECTED);
	request.existing_attributes = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_AUTH_WRITE;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_WRITE_PROTECTED);

	/* Ordinary unsigned variables need no authority. */
	request.target_exists = false;
	request.existing_attributes = 0;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
	request.target_exists = true;
	request.existing_attributes = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_NON_VOLATILE;
	plan = route_ok(&request);
	assert_authorities(&plan, PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE,
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE, 1);
}

static void invalid_inputs(void)
{
	const struct payload_mm_authvar_route_plan zero = { 0 };
	const uint8_t poison = 0xa5;
	struct payload_mm_authvar_route_request request =
		request_for(private_guid, private_name, sizeof(private_name));
	struct payload_mm_authvar_route_plan plan;

	memset(&plan, 0xa5, sizeof(plan));
	assert(payload_mm_authvar_route_plan(NULL, &plan) ==
		PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	assert(!memcmp(&plan, &zero, sizeof(plan)));
	memset(&plan, 0xa5, sizeof(plan));
	assert(payload_mm_authvar_route_plan((const void *)((uintptr_t)-1 &
		~(uintptr_t)(_Alignof(request) - 1U)), &plan) ==
		PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	assert(!memcmp(&plan, &zero, sizeof(plan)));
	memset(&plan, 0xa5, sizeof(plan));
	request.name = NULL;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	request.name = private_name;
	request.name_size = 0;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	request.name_size = sizeof(private_name);
	request.target_exists = false;
	request.existing_attributes = PAYLOAD_MM_AUTHVAR_ATTRIBUTE_TIME_AUTH;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	request.existing_attributes = 0;
	{
		uint8_t embedded_nul[] = { 'A', 0, 0, 0, 'B', 0 };
		request.name = embedded_nul;
		request.name_size = sizeof(embedded_nul);
		route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	}
	request.name = private_name;
	request.name_size = PAYLOAD_MM_AUTHVAR_ROUTE_MAX_NAME_SIZE + 2U;
	route_fails(&request, PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	request.name_size = sizeof(private_name);
	memset(&plan, 0xa5, sizeof(plan));
	assert(payload_mm_authvar_route_plan(&request, NULL) ==
		PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
	/* The NULL-output failure cannot publish; retain a real zero sanity check. */
	memset(&plan, 0, sizeof(plan));
	assert(!memcmp(&plan, &zero, sizeof(plan)));

	/* Aliasing outputs are rejected without modifying their storage. */
	{
		union {
			struct payload_mm_authvar_route_request request;
			struct payload_mm_authvar_route_plan plan;
		} shared;
		uint8_t saved[sizeof(shared)];

		memset(&shared, poison, sizeof(shared));
		shared.request = request_for(private_guid, private_name,
			sizeof(private_name));
		memcpy(saved, &shared, sizeof(shared));
		assert(payload_mm_authvar_route_plan(&shared.request, &shared.plan) ==
			PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
		assert(!memcmp(&shared, saved, sizeof(shared)));
	}
	memset(&plan, poison, sizeof(plan));
	request = request_for(private_guid, &plan, sizeof(uint16_t));
	{
		struct payload_mm_authvar_route_plan saved = plan;

		assert(payload_mm_authvar_route_plan(&request, &plan) ==
			PAYLOAD_MM_AUTHVAR_ROUTE_INVALID);
		assert(!memcmp(&plan, &saved, sizeof(plan)));
	}
}

int main(void)
{
	exact_target_classification();
	secure_target_routes();
	secure_attribute_rules();
	private_routes();
	invalid_inputs();
	return 0;
}
