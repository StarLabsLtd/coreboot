/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/platform_auth.h>
#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)
#define NV_INDEX (HR_NV_INDEX | 0x20U)

static const uint8_t success_sessions[] = {
	0x80, 0x02, 0, 0, 0, 0x13, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0,
};
static const uint8_t success_property[] = {
	0x80, 0x01, 0, 0, 0, 0x1b, 0, 0, 0, 0,
	0, 0, 0, 0, 0x06, 0, 0, 0, 1, 0, 0x00, 0x02, 0x01,
	0, 0, 0, 0x08,
};
static const uint8_t delivered_error[] = {
	0x80, 0x01, 0, 0, 0, 0x0a, 0, 0, 1, 1,
};

struct test_context {
	const uint8_t *expected;
	size_t expected_size;
	const uint8_t *response;
	size_t response_size;
	enum cb_err transport_result;
	uint8_t *mutate;
	unsigned int calls;
};

struct fixture {
	struct tpm_pre_os_lifecycle lifecycle;
	struct tpm_pre_os_token token;
	struct test_context context;
};

static enum cb_err begin(void *context)
{
	(void)context;
	return CB_SUCCESS;
}

static enum cb_err transmit(void *opaque, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	struct test_context *context = opaque;

	context->calls++;
	CHECK(request_size == context->expected_size);
	CHECK(!memcmp(request, context->expected, request_size));
	if (context->mutate)
		*context->mutate ^= 0xff;
	if (context->transport_result != CB_SUCCESS)
		return context->transport_result;
	CHECK(*response_size >= context->response_size);
	memcpy(response, context->response, context->response_size);
	*response_size = context->response_size;
	return CB_SUCCESS;
}

static enum cb_err quiesce(void *context)
{
	(void)context;
	return CB_SUCCESS;
}

static enum cb_err release(void *context)
{
	(void)context;
	return CB_SUCCESS;
}

static const struct tpm_pre_os_backend backend = {
	.begin = begin,
	.transmit = transmit,
	.quiesce = quiesce,
	.release = release,
};

static void setup(struct fixture *fixture, const uint8_t *expected,
	size_t expected_size, const uint8_t *response, size_t response_size)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->context.expected = expected;
	fixture->context.expected_size = expected_size;
	fixture->context.response = response;
	fixture->context.response_size = response_size;
	CHECK(tpm_pre_os_lifecycle_install(&fixture->lifecycle, &backend,
		&fixture->context, sizeof(fixture->context)) == CB_SUCCESS);
	CHECK(tpm_pre_os_lifecycle_acquire(&fixture->lifecycle,
		&fixture->token) == CB_SUCCESS);
}

static void check_zero(const void *value, size_t size)
{
	const uint8_t *bytes = value;

	for (size_t i = 0; i < size; i++)
		CHECK(bytes[i] == 0);
}

static struct test_context *active_context(struct fixture *fixture)
{
	return (struct test_context *)fixture->lifecycle.backend_context;
}

static void build_write(uint8_t request[75], const uint8_t value[40])
{
	memset(request, 0, 75);
	request[0] = 0x80;
	request[1] = 0x02;
	request[5] = 0x4b;
	request[8] = 0x01;
	request[9] = 0x37;
	request[10] = 0x40;
	request[13] = 0x0c;
	request[14] = 0x01;
	request[17] = 0x20;
	request[21] = 0x09;
	request[22] = 0x40;
	request[25] = 0x09;
	request[32] = 0x28;
	memcpy(request + 33, value, 40);
	request[73] = 0;
	request[74] = 0;
}

static const uint8_t lock_request[] = {
	0x80, 0x02, 0, 0, 0, 0x1f, 0, 0, 1, 0x38,
	0x40, 0, 0, 0x0c, 0x01, 0, 0, 0x20,
	0, 0, 0, 9, 0x40, 0, 0, 9, 0, 0, 0, 0,
	0,
};
static const uint8_t hierarchy_request[] = {
	0x80, 0x02, 0, 0, 0, 0x20, 0, 0, 1, 0x21,
	0x40, 0, 0, 0x0c, 0, 0, 0, 9, 0x40, 0, 0, 9,
	0, 0, 0, 0, 0, 0x40, 0, 0, 0x0c, 0,
};
static const uint8_t property_request[] = {
	0x80, 0x01, 0, 0, 0, 0x16, 0, 0, 1, 0x7a,
	0, 0, 0, 6, 0, 0, 2, 1, 0, 0, 0, 1,
};

enum operation {
	OP_WRITE,
	OP_LOCK,
	OP_CLOSE,
	OP_VERIFY,
};

static enum cb_err invoke(struct fixture *fixture, enum operation operation,
	uint8_t value[40], struct tpm2_platform_auth_result *result)
{
	switch (operation) {
	case OP_WRITE:
		return tpm2_platform_auth_nv_write(&fixture->lifecycle,
			&fixture->token, NV_INDEX, value, result);
	case OP_LOCK:
		return tpm2_platform_auth_nv_write_lock(&fixture->lifecycle,
			&fixture->token, NV_INDEX, result);
	case OP_CLOSE:
		return tpm2_platform_auth_close_ph_enable(&fixture->lifecycle,
			&fixture->token, result);
	case OP_VERIFY:
		return tpm2_platform_auth_verify_startup_clear(&fixture->lifecycle,
			&fixture->token, result);
	}
	return CB_ERR;
}

static void normal_transcripts(void)
{
	struct fixture fixture;
	struct tpm2_platform_auth_result result;
	uint8_t value[40];
	uint8_t write_request[75];
	uint8_t property_with_more[sizeof(success_property)];

	for (size_t i = 0; i < sizeof(value); i++)
		value[i] = (uint8_t)i;
	build_write(write_request, value);
	setup(&fixture, write_request, sizeof(write_request), success_sessions,
		sizeof(success_sessions));
	CHECK(invoke(&fixture, OP_WRITE, value, &result) == CB_SUCCESS);
	CHECK(result.delivered && !result.response_code);
	CHECK(tpm_pre_os_lifecycle_end(&fixture.lifecycle, &fixture.token) ==
		CB_SUCCESS);

	setup(&fixture, lock_request, sizeof(lock_request), success_sessions,
		sizeof(success_sessions));
	CHECK(invoke(&fixture, OP_LOCK, value, &result) == CB_SUCCESS);
	setup(&fixture, hierarchy_request, sizeof(hierarchy_request),
		success_sessions, sizeof(success_sessions));
	CHECK(invoke(&fixture, OP_CLOSE, value, &result) == CB_SUCCESS);
	setup(&fixture, property_request, sizeof(property_request),
		success_property, sizeof(success_property));
	CHECK(invoke(&fixture, OP_VERIFY, value, &result) == CB_SUCCESS);
	CHECK(result.delivered && result.startup_clear_valid &&
		result.startup_clear == 8);
	memcpy(property_with_more, success_property, sizeof(property_with_more));
	property_with_more[10] = 1;
	setup(&fixture, property_request, sizeof(property_request),
		property_with_more, sizeof(property_with_more));
	CHECK(invoke(&fixture, OP_VERIFY, value, &result) == CB_SUCCESS);
	CHECK(result.delivered && result.startup_clear_valid &&
		result.startup_clear == 8);
}

static void response_failures(void)
{
	struct fixture fixture;
	struct tpm2_platform_auth_result result;
	uint8_t value[40] = { 0 };
	uint8_t malformed[sizeof(success_property)];
	uint8_t malformed_session[sizeof(success_sessions) + 1];
	uint8_t malformed_error[sizeof(delivered_error)];
	uint8_t write_request[75];

	build_write(write_request, value);
	for (enum operation op = OP_WRITE; op <= OP_VERIFY; op++) {
		const uint8_t *request = op == OP_WRITE ? write_request :
			op == OP_LOCK ? lock_request : op == OP_CLOSE ?
			hierarchy_request : property_request;
		size_t request_size = op == OP_WRITE ? sizeof(write_request) :
			op == OP_LOCK ? sizeof(lock_request) : op == OP_CLOSE ?
			sizeof(hierarchy_request) : sizeof(property_request);

		setup(&fixture, request, request_size, delivered_error,
			sizeof(delivered_error));
		memset(&result, 0xa5, sizeof(result));
		CHECK(invoke(&fixture, op, value, &result) == CB_ERR);
		CHECK(result.delivered && result.response_code == 0x101);
		CHECK(tpm_pre_os_lifecycle_end(&fixture.lifecycle,
			&fixture.token) == CB_SUCCESS);

		setup(&fixture, request, request_size, delivered_error,
			sizeof(delivered_error));
		active_context(&fixture)->transport_result = CB_ERR;
		memset(&result, 0xa5, sizeof(result));
		CHECK(invoke(&fixture, op, value, &result) == CB_ERR);
		check_zero(&result, sizeof(result));
		CHECK(tpm_pre_os_lifecycle_state(&fixture.lifecycle) ==
			TPM_PRE_OS_FAILED);
	}

	for (size_t mutation = 0; mutation < 2; mutation++) {
		memcpy(malformed_error, delivered_error,
			sizeof(malformed_error));
		if (mutation)
			malformed_error[5] = 0x0b;
		else
			malformed_error[1] = 0x02;
		setup(&fixture, lock_request, sizeof(lock_request),
			malformed_error, sizeof(malformed_error));
		memset(&result, 0xa5, sizeof(result));
		CHECK(invoke(&fixture, OP_LOCK, value, &result) == CB_ERR);
		check_zero(&result, sizeof(result));
		CHECK(tpm_pre_os_lifecycle_state(&fixture.lifecycle) ==
			TPM_PRE_OS_FAILED);
	}

	memcpy(malformed, success_property, sizeof(malformed));
	for (size_t mutation = 0; mutation < 8; mutation++) {
		memcpy(malformed, success_property, sizeof(malformed));
		switch (mutation) {
		case 0:
			malformed[0] = 0x80;
			malformed[1] = 2;
			break;
		case 1:
			malformed[5] = 0x1a;
			break;
		case 2:
			malformed[10] = 2;
			break;
		case 3:
			malformed[14] = 7;
			break;
		case 4:
			malformed[18] = 2;
			break;
		case 5:
			malformed[22] = 2;
			break;
		case 6:
			malformed[26] = 9;
			break;
		case 7:
			malformed[26] = 0;
			break;
		}
		setup(&fixture, property_request, sizeof(property_request),
			malformed, sizeof(malformed));
		memset(&result, 0xa5, sizeof(result));
		CHECK(invoke(&fixture, OP_VERIFY, value, &result) == CB_ERR);
		check_zero(&result, sizeof(result));
		CHECK(tpm_pre_os_lifecycle_state(&fixture.lifecycle) ==
			TPM_PRE_OS_FAILED);
	}

	setup(&fixture, hierarchy_request, sizeof(hierarchy_request),
		success_sessions, sizeof(success_sessions) - 1);
	memset(&result, 0xa5, sizeof(result));
	CHECK(invoke(&fixture, OP_CLOSE, value, &result) == CB_ERR);
	check_zero(&result, sizeof(result));
	CHECK(tpm_pre_os_lifecycle_state(&fixture.lifecycle) ==
		TPM_PRE_OS_FAILED);

	for (size_t mutation = 0; mutation < 4; mutation++) {
		size_t response_size = sizeof(success_sessions);

		memcpy(malformed_session, success_sessions,
			sizeof(success_sessions));

		switch (mutation) {
		case 0:
			malformed_session[1] = 1;
			break;
		case 1:
			malformed_session[5] = 0x12;
			break;
		case 2:
			malformed_session[13] = 1;
			break;
		case 3:
			malformed_session[5] = 0x14;
			malformed_session[19] = 0;
			response_size++;
			break;
		}
		setup(&fixture, hierarchy_request, sizeof(hierarchy_request),
			malformed_session, response_size);
		memset(&result, 0xa5, sizeof(result));
		CHECK(invoke(&fixture, OP_CLOSE, value, &result) == CB_ERR);
		check_zero(&result, sizeof(result));
		CHECK(tpm_pre_os_lifecycle_state(&fixture.lifecycle) ==
			TPM_PRE_OS_FAILED);
	}
}

static void argument_and_mutation_failures(void)
{
	struct fixture fixture;
	struct tpm2_platform_auth_result result;
	uint8_t value[40] = { 0 };
	uint8_t write_request[75];

	build_write(write_request, value);
	setup(&fixture, write_request, sizeof(write_request), success_sessions,
		sizeof(success_sessions));
	memset(&result, 0xa5, sizeof(result));
	CHECK(tpm2_platform_auth_nv_write(&fixture.lifecycle, &fixture.token,
		0x20, value, &result) == CB_ERR_ARG);
	check_zero(&result, sizeof(result));
	CHECK(!active_context(&fixture)->calls);
	CHECK(tpm2_platform_auth_nv_write(&fixture.lifecycle, &fixture.token,
		NV_INDEX, NULL, &result) == CB_ERR_ARG);
	CHECK(tpm2_platform_auth_nv_write(&fixture.lifecycle, &fixture.token,
		NV_INDEX, value, (void *)value) == CB_ERR_ARG);
	CHECK(tpm2_platform_auth_nv_write(&fixture.lifecycle, &fixture.token,
		NV_INDEX, (const void *)&fixture.lifecycle, &result) ==
		CB_ERR_ARG);
	CHECK(tpm2_platform_auth_nv_write(NULL, &fixture.token, NV_INDEX,
		value, &result) == CB_ERR_ARG);
	CHECK(tpm2_platform_auth_nv_write(&fixture.lifecycle, NULL, NV_INDEX,
		value, &result) == CB_ERR_ARG);
	CHECK(tpm2_platform_auth_nv_write(&fixture.lifecycle, &fixture.token,
		NV_INDEX, value, NULL) == CB_ERR_ARG);

	setup(&fixture, write_request, sizeof(write_request), success_sessions,
		sizeof(success_sessions));
	active_context(&fixture)->mutate = &value[0];
	CHECK(invoke(&fixture, OP_WRITE, value, &result) == CB_ERR);
	check_zero(&result, sizeof(result));
	CHECK(tpm_pre_os_lifecycle_state(&fixture.lifecycle) ==
		TPM_PRE_OS_FAILED);

	setup(&fixture, lock_request, sizeof(lock_request), success_sessions,
		sizeof(success_sessions));
	fixture.token.generation++;
	memset(&result, 0xa5, sizeof(result));
	CHECK(invoke(&fixture, OP_LOCK, value, &result) == CB_ERR);
	check_zero(&result, sizeof(result));
	CHECK(!active_context(&fixture)->calls);
}

int main(void)
{
	normal_transcripts();
	response_failures();
	argument_and_mutation_failures();
	return 0;
}
