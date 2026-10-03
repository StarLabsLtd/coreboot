/* SPDX-License-Identifier: GPL-2.0-only */
#include <cdk2/qemu_test_fmp.h>
#include <cdk2/system_fmp_transport.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "FAIL:%u: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

enum mutation {
	MUTATE_NONE,
	MUTATE_PENDING,
	MUTATE_GENERATION,
	MUTATE_TRANSACTION,
	MUTATE_OPERATION,
	MUTATE_DIGEST,
	MUTATE_STATUS,
	MUTATE_STAGING,
	MUTATE_REVISION,
	MUTATE_SIZE,
	MUTATE_ATTEMPTED_VERSION,
	MUTATE_RESERVED,
	MUTATE_CAPSULE_SIZE,
	MUTATE_EXECUTION,
	MUTATE_INFO_PENDING,
	MUTATE_INFO_GENERATION,
	MUTATE_INFO_TRANSACTION,
	MUTATE_INFO_REQUEST_OPERATION,
	MUTATE_INFO_REQUEST_FLAGS,
	MUTATE_INFO_REQUEST_REVISION,
	MUTATE_INFO_REQUEST_SIZE,
	MUTATE_INFO_INTENT_SIZE,
	MUTATE_INFO_RESULT_SIZE,
	MUTATE_INFO_REVISION,
	MUTATE_INFO_SIZE,
	MUTATE_INFO_GUID,
	MUTATE_INFO_CURRENT_VERSION,
	MUTATE_INFO_LOWEST_VERSION_RAISED,
	MUTATE_INFO_LOWEST_VERSION_BELOW_POLICY,
	MUTATE_INFO_LOWEST_VERSION_ABOVE_CURRENT,
	MUTATE_INFO_IMAGE_SIZE,
	MUTATE_INFO_CAPABILITIES,
	MUTATE_INFO_STATE_FLAGS,
	MUTATE_INFO_ATTEMPT_VERSION,
	MUTATE_INFO_ATTEMPT_STATUS,
	MUTATE_INFO_RESERVED,
	MUTATE_INFO_EXECUTION_FIELDS,
	MUTATE_INFO_EXECUTION,
	MUTATE_SIGNATURE_REFUSED,
};

struct fixture {
	UINT8 communication[CDK2_SYSTEM_FMP_TRANSPORT_SIZE] __aligned(8);
	UINT8 staging[64] __aligned(8);
	UINT8 source[32];
	struct cdk2_system_fmp_client *client;
	enum mutation mutation;
	UINT32 event;
	UINT32 trigger_count;
	UINT32 communication_publish_count;
	UINT32 staging_publish_count;
	UINT32 publish_call_count;
	UINT64 last_transaction;
	char trace[64];
	UINTN trace_size;
	BOOLEAN fail_publish_call[16];
	BOOLEAN mutate_source;
	BOOLEAN fail_hash;
	BOOLEAN fail_publish;
	BOOLEAN fail_acquire;
	BOOLEAN fail_trigger;
	BOOLEAN reenter;
};

static void trace(struct fixture *fixture, char event)
{
	CHECK(fixture->trace_size + 1U < sizeof(fixture->trace));
	fixture->trace[fixture->trace_size++] = event;
	fixture->trace[fixture->trace_size] = '\0';
}

static void reset_trace(struct fixture *fixture)
{
	fixture->trace_size = 0U;
	fixture->trace[0] = '\0';
}

static EFI_STATUS hash(void *context, const void *data, UINTN size,
	UINT8 digest[CDK2_SYSTEM_FMP_DIGEST_SIZE])
{
	struct fixture *fixture = context;

	if (fixture->fail_hash)
		return EFI_DEVICE_ERROR;
	cdk2_qemu_fmp_sha256(data, size, digest);
	return EFI_SUCCESS;
}

static EFI_STATUS publish(void *context, void *base, UINTN size)
{
	struct fixture *fixture = context;
	UINT32 call = fixture->publish_call_count++;
	BOOLEAN fail = call < sizeof(fixture->fail_publish_call) /
		sizeof(fixture->fail_publish_call[0]) &&
		fixture->fail_publish_call[call];

	if (base == fixture->communication) {
		trace(fixture, 'C');
		fixture->communication_publish_count++;
	} else if (base == fixture->staging) {
		trace(fixture, 'S');
		fixture->staging_publish_count++;
	}
	if (fixture->fail_publish || fail)
		return EFI_DEVICE_ERROR;
	if (base == fixture->staging && size != 0U && fixture->event == 1U) {
		fixture->event = 2U;
		if (fixture->mutate_source)
			fixture->source[0] ^= 0xffU;
	} else if (base == fixture->communication &&
		   fixture->event == 2U) {
		fixture->event = 3U;
	} else if (base == fixture->communication &&
		   fixture->event == 1U) {
		fixture->event = 3U;
	}
	return EFI_SUCCESS;
}

static EFI_STATUS acquire(void *context, void *base, UINTN size)
{
	struct fixture *fixture = context;

	(void)size;
	if (fixture->fail_acquire)
		return EFI_DEVICE_ERROR;
	if (base == fixture->communication) {
		trace(fixture, 'c');
		CHECK(fixture->event == 5U);
		fixture->event = 6U;
	} else if (base == fixture->staging) {
		trace(fixture, 's');
		CHECK(fixture->event == 6U);
		fixture->event = 7U;
	}
	return EFI_SUCCESS;
}

static void barrier(void *context)
{
	struct fixture *fixture = context;

	trace(fixture, 'B');
	if (fixture->event == 0U)
		fixture->event = 1U;
	else if (fixture->event == 3U)
		fixture->event = 4U;
	else if (fixture->event == 4U)
		fixture->event = 5U;
}

static EFI_STATUS trigger(void *context, UINT16 port, UINT8 value)
{
	struct fixture *fixture = context;
	struct cdk2_system_fmp_request *request =
		(void *)(fixture->communication + CDK2_SYSTEM_FMP_REQUEST_OFFSET);
	struct cdk2_system_fmp_intent *intent =
		(void *)(fixture->communication + CDK2_SYSTEM_FMP_INTENT_OFFSET);
	struct cdk2_system_fmp_result *result =
		(void *)(fixture->communication + CDK2_SYSTEM_FMP_RESULT_OFFSET);
	struct cdk2_system_fmp_transport_info *info =
		(void *)(fixture->communication + CDK2_SYSTEM_FMP_INFO_OFFSET);
	struct cdk2_system_fmp_info ignored_info;
	struct cdk2_system_fmp_outcome ignored;
	UINT8 digest[CDK2_SYSTEM_FMP_DIGEST_SIZE];

	CHECK(fixture->event == 4U);
	trace(fixture, 'T');
	CHECK(port == 0xb2U && value == 0x77U);
	fixture->trigger_count++;
	if (fixture->fail_trigger)
		return EFI_DEVICE_ERROR;
	if (fixture->reenter)
		CHECK(cdk2_system_fmp_client_execute(fixture->client,
			CDK2_SYSTEM_FMP_CHECK, fixture->source,
			sizeof(fixture->source), 3U, &ignored) ==
			CDK2_SYSTEM_FMP_TRANSPORT_BUSY);
	if (fixture->reenter)
		CHECK(cdk2_system_fmp_client_read_info(fixture->client,
			&ignored_info) == CDK2_SYSTEM_FMP_TRANSPORT_BUSY);
	if (fixture->reenter)
		CHECK(cdk2_system_fmp_client_close(fixture->client) ==
			CDK2_SYSTEM_FMP_TRANSPORT_BUSY);
	CHECK(request->revision == (fixture->client->endpoint.revision ==
		CB_CAPSULE_BROKER_ENDPOINT_RAM_REVISION ?
		CDK2_SYSTEM_FMP_TRANSPORT_RAM_REVISION :
		CDK2_SYSTEM_FMP_TRANSPORT_REVISION) &&
		request->size == sizeof(*request) && request->flags == 0U);
	CHECK(request->transaction > fixture->last_transaction);
	fixture->last_transaction = request->transaction;
	if (request->operation == CDK2_SYSTEM_FMP_TRANSPORT_CLOSE_RAM) {
		CHECK(request->intent_size == 0U &&
			request->result_size == sizeof(*result));
		memset(result, 0, sizeof(*result));
		result->revision = request->revision;
		result->size = sizeof(*result);
		result->generation = request->generation;
		result->transaction = request->transaction;
		switch (fixture->mutation) {
		case MUTATE_NONE:
			break;
		case MUTATE_PENDING:
			result->result = CDK2_SYSTEM_FMP_RESULT_PENDING;
			break;
		case MUTATE_GENERATION:
			result->generation++;
			break;
		case MUTATE_TRANSACTION:
			result->transaction--;
			break;
		case MUTATE_OPERATION:
			request->operation = CDK2_SYSTEM_FMP_TRANSPORT_EXECUTE;
			break;
		case MUTATE_STATUS:
			result->last_attempt_status = 1U;
			break;
		case MUTATE_REVISION:
			result->revision--;
			break;
		case MUTATE_SIZE:
			result->size--;
			break;
		case MUTATE_ATTEMPTED_VERSION:
			result->attempted_version = 1U;
			break;
		case MUTATE_RESERVED:
			result->reserved = 1U;
			break;
		case MUTATE_EXECUTION:
			result->result = CDK2_SYSTEM_FMP_RESULT_EXECUTION;
			break;
		default:
			CHECK(FALSE);
			break;
		}
		return EFI_SUCCESS;
	}
	if (request->operation == CDK2_SYSTEM_FMP_TRANSPORT_READ_INFO) {
		CHECK(request->intent_size == 0U &&
			request->result_size == sizeof(*info));
		memset(info, 0, sizeof(*info));
		info->revision = request->revision;
		info->size = sizeof(*info);
		info->generation = request->generation;
		info->transaction = request->transaction;
		info->image_type = fixture->client->endpoint.image_type;
		info->hardware_instance = 0x1122334455667788ULL;
		info->current_version = fixture->client->endpoint.current_version;
		info->lowest_supported_version =
			fixture->client->endpoint.lowest_supported_version;
		info->image_size = fixture->client->endpoint.image_size;
		info->capabilities = fixture->client->endpoint.capabilities;
		info->state_flags = CDK2_SYSTEM_FMP_INFO_STATE_VALID_FLAGS;
		info->last_attempt_version = 8U;
		info->last_attempt_status =
			CDK2_SYSTEM_FMP_LAST_ATTEMPT_SUCCESS;
		switch (fixture->mutation) {
		case MUTATE_INFO_PENDING:
			info->result = CDK2_SYSTEM_FMP_RESULT_PENDING;
			break;
		case MUTATE_INFO_GENERATION:
			info->generation++;
			break;
		case MUTATE_INFO_TRANSACTION:
			info->transaction--;
			break;
		case MUTATE_INFO_REQUEST_OPERATION:
			request->operation = CDK2_SYSTEM_FMP_TRANSPORT_EXECUTE;
			break;
		case MUTATE_INFO_REQUEST_FLAGS:
			request->flags = 1U;
			break;
		case MUTATE_INFO_REQUEST_REVISION:
			request->revision--;
			break;
		case MUTATE_INFO_REQUEST_SIZE:
			request->size--;
			break;
		case MUTATE_INFO_INTENT_SIZE:
			request->intent_size = 1U;
			break;
		case MUTATE_INFO_RESULT_SIZE:
			request->result_size--;
			break;
		case MUTATE_INFO_REVISION:
			info->revision--;
			break;
		case MUTATE_INFO_SIZE:
			info->size--;
			break;
		case MUTATE_INFO_GUID:
			((UINT8 *)&info->image_type)[0] ^= 1U;
			break;
		case MUTATE_INFO_CURRENT_VERSION:
			info->current_version++;
			break;
		case MUTATE_INFO_LOWEST_VERSION_RAISED:
			info->lowest_supported_version++;
			break;
		case MUTATE_INFO_LOWEST_VERSION_BELOW_POLICY:
			info->lowest_supported_version =
				fixture->client->endpoint.lowest_supported_version - 1U;
			break;
		case MUTATE_INFO_LOWEST_VERSION_ABOVE_CURRENT:
			info->lowest_supported_version = info->current_version + 1U;
			break;
		case MUTATE_INFO_IMAGE_SIZE:
			info->image_size++;
			break;
		case MUTATE_INFO_CAPABILITIES:
			info->capabilities = CB_CAPSULE_BROKER_APPLY_REGIONS;
			break;
		case MUTATE_INFO_STATE_FLAGS:
			info->state_flags |= 1U << 2;
			break;
		case MUTATE_INFO_ATTEMPT_VERSION:
			info->state_flags &=
				~CDK2_SYSTEM_FMP_INFO_STATE_LAST_ATTEMPT_VERSION_VALID;
			break;
		case MUTATE_INFO_ATTEMPT_STATUS:
			info->last_attempt_status = 2U;
			break;
		case MUTATE_INFO_RESERVED:
			info->reserved[11] = 1U;
			break;
		case MUTATE_INFO_EXECUTION_FIELDS:
			info->result = CDK2_SYSTEM_FMP_RESULT_EXECUTION;
			break;
		case MUTATE_INFO_EXECUTION:
			memset(&info->image_type, 0,
				offsetof(struct cdk2_system_fmp_transport_info,
					reserved) -
				offsetof(struct cdk2_system_fmp_transport_info,
					image_type));
			info->result = CDK2_SYSTEM_FMP_RESULT_EXECUTION;
			break;
		default:
			break;
		}
		return EFI_SUCCESS;
	}
	CHECK(request->operation == CDK2_SYSTEM_FMP_TRANSPORT_EXECUTE);
	CHECK(intent->revision == CDK2_SYSTEM_FMP_INTENT_REVISION &&
		intent->size == sizeof(*intent) &&
		intent->broker_generation == request->generation &&
		intent->transaction == request->transaction);
	cdk2_qemu_fmp_sha256(fixture->staging, (UINTN)intent->capsule_size,
		digest);
	CHECK(memcmp(intent->digest, digest, sizeof(digest)) == 0);
	memset(result, 0, sizeof(*result));
	result->revision = request->revision;
	result->size = sizeof(*result);
	result->generation = request->generation;
	result->transaction = request->transaction;
	result->attempted_version = intent->attempted_version;
	switch (fixture->mutation) {
	case MUTATE_PENDING:
		result->result = MAX_UINT32;
		result->last_attempt_status = MAX_UINT32;
		break;
	case MUTATE_GENERATION:
		result->generation++;
		break;
	case MUTATE_TRANSACTION:
		result->transaction--;
		break;
	case MUTATE_OPERATION:
		intent->operation = 99U;
		break;
	case MUTATE_DIGEST:
		intent->digest[0] ^= 1U;
		break;
	case MUTATE_STATUS:
		result->last_attempt_status = 2U;
		break;
	case MUTATE_STAGING:
		fixture->staging[0] ^= 1U;
		break;
	case MUTATE_REVISION:
		result->revision++;
		break;
	case MUTATE_SIZE:
		request->size--;
		break;
	case MUTATE_ATTEMPTED_VERSION:
		result->attempted_version++;
		break;
	case MUTATE_RESERVED:
		intent->reserved = 1U;
		break;
	case MUTATE_CAPSULE_SIZE:
		intent->capsule_size++;
		break;
	case MUTATE_EXECUTION:
		result->last_attempt_status = 1U;
		result->result = 1U;
		break;
	case MUTATE_SIGNATURE_REFUSED:
		result->last_attempt_status = 1U;
		result->result = 2U;
		break;
	case MUTATE_NONE:
		break;
	default:
		CHECK(FALSE);
		break;
	}
	return EFI_SUCCESS;
}

static int setup(struct fixture *fixture,
	struct cdk2_system_fmp_client *client)
{
	struct cdk2_system_fmp_transport endpoint = {
		.revision = CB_CAPSULE_BROKER_ENDPOINT_REVISION,
		.generation = 9U,
		.image_type = { 0x12345678U, 0x1234U, 0xabcdU,
			{ 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U } },
		.current_version = 9U,
		.lowest_supported_version = 7U,
		.image_size = sizeof(fixture->staging),
		.capabilities = CB_CAPSULE_BROKER_REQUIRED_CAPABILITIES,
		.communication_base = (UINTN)fixture->communication,
		.communication_size = sizeof(fixture->communication),
		.message_size = sizeof(fixture->communication),
		.staging_base = (UINTN)fixture->staging,
		.staging_size = sizeof(fixture->staging),
		.trigger_port = 0xb2U,
		.trigger_value = 0x77U,
		.valid = TRUE,
	};
	struct cdk2_system_fmp_transport_port port = {
		.context = fixture,
		.sha256 = hash,
		.publish = publish,
		.acquire = acquire,
		.barrier = barrier,
		.trigger_apm_io8 = trigger,
	};

	memset(fixture, 0, sizeof(*fixture));
	memset(fixture->source, 0xa5, sizeof(fixture->source));
	fixture->client = client;
	CHECK(cdk2_system_fmp_client_init(client, &endpoint, &port) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	return 0;
}

static BOOLEAN all_zero(const void *memory, UINTN size)
{
	const UINT8 *bytes = memory;

	while (size-- != 0U)
		if (*bytes++ != 0U)
			return FALSE;
	return TRUE;
}

static int execute_once(enum mutation mutation,
	enum cdk2_system_fmp_transport_status expected)
{
	struct cdk2_system_fmp_client client;
	struct cdk2_system_fmp_outcome outcome = {0};
	struct fixture fixture;

	CHECK(setup(&fixture, &client) == 0);
	fixture.mutation = mutation;
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 3U, &outcome) == expected);
	if (expected == CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS)
		CHECK(outcome.attempted_version == 3U &&
			outcome.last_attempt_status == 0U &&
			outcome.broker_result == 0U && outcome.trigger_invoked &&
			outcome.response_authenticated);
	else if (expected == CDK2_SYSTEM_FMP_TRANSPORT_EXECUTION_FAILED)
		CHECK(outcome.attempted_version == 3U &&
			outcome.last_attempt_status == 1U &&
			outcome.broker_result == 1U && outcome.trigger_invoked &&
			outcome.response_authenticated);
	else
		CHECK(outcome.attempted_version == 0U &&
			outcome.last_attempt_status == 0U &&
			outcome.broker_result == 0U && outcome.trigger_invoked &&
			!outcome.response_authenticated);
	CHECK(fixture.trigger_count == 1U);
	CHECK(all_zero(fixture.communication, sizeof(fixture.communication)));
	CHECK(all_zero(fixture.staging, sizeof(fixture.source)));
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	return 0;
}

static int cleanup_retry(BOOLEAN fail_communication, BOOLEAN fail_staging)
{
	struct cdk2_system_fmp_client client;
	struct cdk2_system_fmp_outcome outcome;
	struct fixture fixture;

	CHECK(setup(&fixture, &client) == 0);
	fixture.fail_publish_call[2] = fail_communication;
	fixture.fail_publish_call[3] = fail_staging;
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 3U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CACHE_FAILED);
	CHECK(outcome.trigger_invoked && outcome.response_authenticated);
	CHECK(strcmp(fixture.trace, "BSCBTBcsBBBCSB") == 0);
	CHECK(fixture.publish_call_count == 4U);
	CHECK(fixture.communication_publish_count == 2U);
	CHECK(fixture.staging_publish_count == 2U);
	CHECK(client.initialized && !client.busy);
	CHECK(client.communication_dirty == fail_communication);
	CHECK(client.staging_dirty == fail_staging);
	CHECK(client.staged_size == (fail_staging ? sizeof(fixture.source) : 0U));
	CHECK(all_zero(fixture.communication, sizeof(fixture.communication)));
	CHECK(all_zero(fixture.staging, sizeof(fixture.source)));

	reset_trace(&fixture);
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	if (fail_communication && fail_staging)
		CHECK(strcmp(fixture.trace, "BCSB") == 0);
	else if (fail_communication)
		CHECK(strcmp(fixture.trace, "BCB") == 0);
	else
		CHECK(strcmp(fixture.trace, "BSB") == 0);
	CHECK(fixture.publish_call_count ==
		4U + (fail_communication ? 1U : 0U) +
		(fail_staging ? 1U : 0U));
	CHECK(all_zero(&client, sizeof(client)));
	return 0;
}

static int cleanup_repeated_failure(void)
{
	struct cdk2_system_fmp_client client;
	struct cdk2_system_fmp_outcome outcome;
	struct fixture fixture;

	CHECK(setup(&fixture, &client) == 0);
	fixture.fail_publish_call[2] = TRUE;
	fixture.fail_publish_call[3] = TRUE;
	fixture.fail_publish_call[5] = TRUE;
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 3U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CACHE_FAILED);
	CHECK(client.communication_dirty && client.staging_dirty);
	CHECK(client.staged_size == sizeof(fixture.source));

	reset_trace(&fixture);
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CACHE_FAILED);
	CHECK(strcmp(fixture.trace, "BCSB") == 0);
	CHECK(client.initialized && !client.communication_dirty &&
		client.staging_dirty);
	CHECK(client.staged_size == sizeof(fixture.source));
	CHECK(fixture.publish_call_count == 6U);

	reset_trace(&fixture);
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	CHECK(strcmp(fixture.trace, "BSB") == 0);
	CHECK(fixture.publish_call_count == 7U);
	CHECK(all_zero(&client, sizeof(client)));
	return 0;
}

static int read_info_once(enum mutation mutation,
	enum cdk2_system_fmp_transport_status expected)
{
	struct cdk2_system_fmp_client client;
	struct cdk2_system_fmp_info info = {0};
	struct fixture fixture;
	enum cdk2_system_fmp_transport_status status;
	UINT32 expected_floor;

	CHECK(setup(&fixture, &client) == 0);
	fixture.mutation = mutation;
	status = cdk2_system_fmp_client_read_info(&client, &info);
	if (mutation == MUTATE_INFO_LOWEST_VERSION_ABOVE_CURRENT)
		assert(status == CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	CHECK(status == expected);
	if (expected == CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS) {
		expected_floor = client.endpoint.lowest_supported_version;
		CHECK(!memcmp(&info.image_type, &client.endpoint.image_type,
			sizeof(info.image_type)));
		CHECK(info.hardware_instance == 0x1122334455667788ULL);
		CHECK(info.current_version == client.endpoint.current_version);
		if (mutation == MUTATE_INFO_LOWEST_VERSION_RAISED)
			expected_floor++;
		else if (mutation == MUTATE_INFO_LOWEST_VERSION_ABOVE_CURRENT)
			expected_floor = client.endpoint.current_version + 1U;
		CHECK(info.lowest_supported_version == expected_floor);
		CHECK(info.image_size == client.endpoint.image_size);
		CHECK(info.capabilities == client.endpoint.capabilities);
		CHECK(info.state_flags ==
			CDK2_SYSTEM_FMP_INFO_STATE_VALID_FLAGS);
		CHECK(info.last_attempt_version == 8U);
		CHECK(info.last_attempt_status ==
			CDK2_SYSTEM_FMP_LAST_ATTEMPT_SUCCESS);
	} else {
		CHECK(all_zero(&info, sizeof(info)));
	}
	CHECK(fixture.trigger_count == 1U);
	if (mutation == MUTATE_INFO_PENDING)
		CHECK(strcmp(fixture.trace, "BCBTBcBBCB") == 0);
	else
		CHECK(strcmp(fixture.trace, "BCBTBcBBBCB") == 0);
	CHECK(fixture.communication_publish_count == 2U);
	CHECK(fixture.staging_publish_count == 0U);
	CHECK(all_zero(fixture.communication, sizeof(fixture.communication)));
	CHECK(all_zero(fixture.staging, sizeof(fixture.staging)));
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	return 0;
}

static int read_info_scrub_retry(void)
{
	struct cdk2_system_fmp_client client;
	struct cdk2_system_fmp_info info;
	struct fixture fixture;

	CHECK(setup(&fixture, &client) == 0);
	fixture.fail_publish_call[1] = TRUE;
	CHECK(cdk2_system_fmp_client_read_info(&client, &info) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CACHE_FAILED);
	CHECK(strcmp(fixture.trace, "BCBTBcBBBCB") == 0);
	CHECK(all_zero(&info, sizeof(info)));
	CHECK(client.initialized && !client.busy &&
		client.communication_dirty && !client.staging_dirty);
	CHECK(all_zero(fixture.communication, sizeof(fixture.communication)));

	reset_trace(&fixture);
	CHECK(cdk2_system_fmp_client_read_info(&client, &info) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CACHE_FAILED);
	CHECK(fixture.trace_size == 0U);
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	CHECK(strcmp(fixture.trace, "BCB") == 0);
	CHECK(all_zero(&client, sizeof(client)));
	return 0;
}

static int read_info_cache_failure(BOOLEAN fail_publish,
	BOOLEAN fail_acquire, BOOLEAN fail_trigger)
{
	struct cdk2_system_fmp_client client;
	struct cdk2_system_fmp_info info;
	struct fixture fixture;
	enum cdk2_system_fmp_transport_status expected =
		fail_trigger ? CDK2_SYSTEM_FMP_TRANSPORT_TRIGGER_FAILED :
		CDK2_SYSTEM_FMP_TRANSPORT_CACHE_FAILED;

	CHECK(setup(&fixture, &client) == 0);
	fixture.fail_publish = fail_publish;
	fixture.fail_acquire = fail_acquire;
	fixture.fail_trigger = fail_trigger;
	CHECK(cdk2_system_fmp_client_read_info(&client, &info) == expected);
	CHECK(all_zero(&info, sizeof(info)));
	fixture.fail_publish = FALSE;
	fixture.fail_acquire = FALSE;
	fixture.fail_trigger = FALSE;
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	return 0;
}

static int ram_close_case(enum mutation mutation,
	enum cdk2_system_fmp_transport_status expected)
{
	struct cdk2_system_fmp_client client;
	struct cdk2_system_fmp_outcome outcome;
	struct cdk2_system_fmp_info info;
	struct fixture fixture;
	struct cdk2_system_fmp_transport endpoint;
	struct cdk2_system_fmp_transport_port port;

	CHECK(setup(&fixture, &client) == 0);
	CHECK(cdk2_system_fmp_client_close_ram(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_INVALID);
	CHECK(fixture.trigger_count == 0U);
	endpoint = client.endpoint;
	endpoint.revision = CB_CAPSULE_BROKER_ENDPOINT_RAM_REVISION;
	port = client.port;
	port.sha256 = NULL;
	CHECK(cdk2_system_fmp_client_init(&client, &endpoint, &port) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 1U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_INVALID);
	CHECK(fixture.trigger_count == 0U &&
		all_zero(fixture.staging, sizeof(fixture.staging)));
	fixture.mutation = mutation;
	CHECK(cdk2_system_fmp_client_close_ram(&client) == expected);
	CHECK(client.ram_closed == (expected == CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS));
	CHECK(fixture.trigger_count == 1U &&
		fixture.staging_publish_count == 0U &&
		all_zero(fixture.communication, sizeof(fixture.communication)));
	if (expected == CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS) {
		CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
			fixture.source, sizeof(fixture.source), 1U, &outcome) ==
			CDK2_SYSTEM_FMP_TRANSPORT_CLOSED);
		fixture.event = 0U;
		fixture.mutation = MUTATE_NONE;
		CHECK(cdk2_system_fmp_client_close_ram(&client) == expected);
		CHECK(fixture.trigger_count == 2U);
		fixture.event = 0U;
		CHECK(cdk2_system_fmp_client_read_info(&client, &info) == expected);
		CHECK(fixture.trigger_count == 3U);
	}
	return 0;
}

int main(void)
{
	struct cdk2_system_fmp_client client;
	struct cdk2_system_fmp_info info;
	struct cdk2_system_fmp_outcome outcome;
	struct fixture fixture;
	static const enum mutation close_mutations[] = {
		MUTATE_PENDING, MUTATE_GENERATION, MUTATE_TRANSACTION,
		MUTATE_OPERATION, MUTATE_STATUS, MUTATE_REVISION, MUTATE_SIZE,
		MUTATE_ATTEMPTED_VERSION, MUTATE_RESERVED,
	};

	CHECK(ram_close_case(MUTATE_NONE, CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS) == 0);
	CHECK(ram_close_case(MUTATE_EXECUTION,
		CDK2_SYSTEM_FMP_TRANSPORT_EXECUTION_FAILED) == 0);
	for (UINTN i = 0U; i < ARRAY_SIZE(close_mutations); i++)
		CHECK(ram_close_case(close_mutations[i],
			CDK2_SYSTEM_FMP_TRANSPORT_BAD_RESPONSE) == 0);
	UINT8 oversized[65] = {0};
	UINT32 index;

	CHECK(execute_once(MUTATE_NONE,
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS) == 0);
	for (index = MUTATE_PENDING; index <= MUTATE_CAPSULE_SIZE; index++)
		CHECK(execute_once(index,
			CDK2_SYSTEM_FMP_TRANSPORT_BAD_RESPONSE) == 0);
	CHECK(execute_once(MUTATE_EXECUTION,
		CDK2_SYSTEM_FMP_TRANSPORT_EXECUTION_FAILED) == 0);
	CHECK(execute_once(MUTATE_SIGNATURE_REFUSED,
		CDK2_SYSTEM_FMP_TRANSPORT_BAD_RESPONSE) == 0);
	CHECK(cleanup_retry(TRUE, FALSE) == 0);
	CHECK(cleanup_retry(FALSE, TRUE) == 0);
	CHECK(cleanup_retry(TRUE, TRUE) == 0);
	CHECK(cleanup_repeated_failure() == 0);
	CHECK(read_info_once(MUTATE_NONE,
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS) == 0);
	CHECK(read_info_once(MUTATE_INFO_LOWEST_VERSION_RAISED,
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS) == 0);
	CHECK(read_info_once(MUTATE_INFO_LOWEST_VERSION_ABOVE_CURRENT,
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS) == 0);
	for (index = MUTATE_INFO_PENDING;
	     index <= MUTATE_INFO_EXECUTION_FIELDS; index++)
		if (index != MUTATE_INFO_LOWEST_VERSION_RAISED &&
		    index != MUTATE_INFO_LOWEST_VERSION_ABOVE_CURRENT)
			CHECK(read_info_once(index,
				CDK2_SYSTEM_FMP_TRANSPORT_BAD_RESPONSE) == 0);
	CHECK(read_info_once(MUTATE_INFO_EXECUTION,
		CDK2_SYSTEM_FMP_TRANSPORT_EXECUTION_FAILED) == 0);
	CHECK(read_info_scrub_retry() == 0);
	CHECK(read_info_cache_failure(TRUE, FALSE, FALSE) == 0);
	CHECK(read_info_cache_failure(FALSE, TRUE, FALSE) == 0);
	CHECK(read_info_cache_failure(FALSE, FALSE, TRUE) == 0);

	CHECK(setup(&fixture, &client) == 0);
	CHECK(cdk2_system_fmp_client_read_info(&client, NULL) ==
		CDK2_SYSTEM_FMP_TRANSPORT_INVALID);
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, 0U, 3U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_INVALID);
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		oversized, sizeof(oversized), 3U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CAPACITY);
	CHECK(!outcome.trigger_invoked && !outcome.response_authenticated);
	fixture.reenter = TRUE;
	fixture.mutate_source = TRUE;
	CHECK(fixture.source[0] == 0xa5U);
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_SET,
		fixture.source, sizeof(fixture.source), 4U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	CHECK(fixture.source[0] == 0x5aU);
	fixture.event = 0U;
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 5U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	CHECK(client.transaction == 2U && fixture.last_transaction == 2U);
	fixture.event = 0U;
	fixture.reenter = TRUE;
	CHECK(cdk2_system_fmp_client_read_info(&client, &info) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	CHECK(client.transaction == 3U && fixture.last_transaction == 3U);
	client.transaction = MAX_UINT64;
	CHECK(cdk2_system_fmp_client_read_info(&client, &info) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CLOSED);
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 5U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CLOSED);
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 5U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CLOSED);
	CHECK(cdk2_system_fmp_client_read_info(&client, &info) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CLOSED);
	CHECK(cdk2_system_fmp_client_read_info(NULL, &info) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CLOSED);

	CHECK(setup(&fixture, &client) == 0);
	fixture.fail_hash = TRUE;
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 3U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_HASH_FAILED);
	CHECK(!outcome.trigger_invoked && !outcome.response_authenticated);
	fixture.fail_hash = FALSE;
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);

	CHECK(setup(&fixture, &client) == 0);
	fixture.fail_trigger = TRUE;
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 3U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_TRIGGER_FAILED);
	CHECK(outcome.trigger_invoked && !outcome.response_authenticated);
	fixture.fail_trigger = FALSE;
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);

	CHECK(setup(&fixture, &client) == 0);
	fixture.fail_publish = TRUE;
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 3U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CACHE_FAILED);
	CHECK(!outcome.trigger_invoked && !outcome.response_authenticated);
	fixture.fail_publish = FALSE;
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);

	CHECK(setup(&fixture, &client) == 0);
	fixture.fail_acquire = TRUE;
	CHECK(cdk2_system_fmp_client_execute(&client, CDK2_SYSTEM_FMP_CHECK,
		fixture.source, sizeof(fixture.source), 3U, &outcome) ==
		CDK2_SYSTEM_FMP_TRANSPORT_CACHE_FAILED);
	CHECK(outcome.trigger_invoked && !outcome.response_authenticated);
	fixture.fail_acquire = FALSE;
	CHECK(cdk2_system_fmp_client_close(&client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS);
	puts("SystemFmp typed transport client tests: PASS");
	return 0;
}
