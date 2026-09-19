/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_broker.h>
#include <boot/payload_mm_authvar.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t communication[CAPSULE_BROKER_TRANSPORT_SIZE] __aligned(8);
static uint8_t alternate[CAPSULE_BROKER_TRANSPORT_SIZE] __aligned(8);
static uint64_t generation = 7;
static void *selected_buffer = communication;
static bool buffer_available = true;
static bool ready = true;
static bool execute_success = true;
static bool mutate_input;
static bool mutate_authority;
static bool recurse;
static unsigned int execute_count;
static unsigned int close_count;

_Static_assert(__builtin_types_compatible_p(
	typeof(&capsule_broker_transport_dispatch), enum cb_err (*)(void)),
	"capsule broker transport accepts no caller address");

void *capsule_broker_transport_test_authority(size_t *size);

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!value)
		__builtin_trap();
}

static struct capsule_broker_transport_request *request(void)
{
	return (void *)&communication[CAPSULE_BROKER_TRANSPORT_REQUEST_OFFSET];
}

static struct payload_mm_fmp_capsule_intent *intent(void)
{
	return (void *)&communication[CAPSULE_BROKER_TRANSPORT_INTENT_OFFSET];
}

static struct capsule_broker_transport_result *result(void)
{
	return (void *)&communication[CAPSULE_BROKER_TRANSPORT_RESULT_OFFSET];
}

bool capsule_broker_transport_buffer(void **buffer, size_t *size,
	uint64_t *candidate_generation)
{
	if (!buffer_available)
		return false;
	*buffer = selected_buffer;
	*size = sizeof(communication);
	*candidate_generation = generation;
	return true;
}

bool capsule_broker_generation_matches(uint64_t candidate_generation)
{
	return candidate_generation == generation;
}

bool capsule_broker_intent_matches(uint64_t candidate_generation,
	uint64_t capsule_size)
{
	return capsule_broker_generation_matches(candidate_generation) &&
		capsule_size && capsule_size <= 4096;
}

bool capsule_broker_execution_ready(void)
{
	return ready;
}

enum cb_err payload_mm_fmp_transaction_execute_intent(
	const struct payload_mm_fmp_capsule_intent *staged)
{
	struct capsule_broker_transport_result *shared = result();

	execute_count++;
	assert(staged != intent());
	assert(shared->result == CAPSULE_BROKER_RESULT_PENDING);
	assert(shared->last_attempt_status == CAPSULE_BROKER_STATUS_PENDING);
	if (mutate_input) {
		memset(request(), 0xa5, sizeof(*request()));
		memset(intent(), 0x5a, sizeof(*intent()));
		memset(result(), 0, sizeof(*result()));
	}
	if (mutate_authority) {
		size_t size;
		uint8_t *authority = capsule_broker_transport_test_authority(&size);

		assert(size > 0);
		authority[0] ^= 1;
	}
	if (recurse)
		assert(capsule_broker_transport_dispatch() == CB_ERR);
	return execute_success ? CB_SUCCESS : CB_ERR;
}

void payload_mm_fmp_transaction_close(void)
{
	close_count++;
}

static void publish(uint64_t transaction)
{
	memset(communication, 0, sizeof(communication));
	*request() = (struct capsule_broker_transport_request) {
		.revision = CAPSULE_BROKER_TRANSPORT_REVISION,
		.size = sizeof(*request()),
		.operation = CAPSULE_BROKER_TRANSPORT_EXECUTE,
		.generation = generation,
		.transaction = transaction,
		.intent_size = sizeof(*intent()),
		.result_size = sizeof(*result()),
	};
	*intent() = (struct payload_mm_fmp_capsule_intent) {
		.revision = PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION,
		.size = sizeof(*intent()),
		.operation = PAYLOAD_MM_FMP_CAPSULE_CHECK,
		.broker_generation = generation,
		.transaction = transaction,
		.capsule_size = 4096,
		.digest_algorithm = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256,
		.digest_size = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE,
		.attempted_version = 11,
	};
	memset(result(), 0xcc, sizeof(*result()));
}

static void expect_rejected(void)
{
	uint8_t before[sizeof(*result())];

	memcpy(before, result(), sizeof(before));
	assert(capsule_broker_transport_dispatch() == CB_ERR);
	assert(!execute_count);
	assert(!memcmp(before, result(), sizeof(before)));
}

static void expect_success(void)
{
	assert(capsule_broker_transport_dispatch() == CB_SUCCESS);
	assert(execute_count == 1);
	assert(result()->revision == CAPSULE_BROKER_TRANSPORT_REVISION);
	assert(result()->size == sizeof(*result()));
	assert(result()->generation == generation);
	assert(result()->transaction == 1);
	assert(result()->attempted_version == 11);
	assert(!result()->reserved);
	assert(result()->last_attempt_status ==
		CAPSULE_BROKER_LAST_ATTEMPT_SUCCESS);
	assert(result()->result == CAPSULE_BROKER_RESULT_SUCCESS);
}

int main(int argc, char **argv)
{
	const char *name;

	assert(argc == 2);
	name = argv[1];
	publish(1);
	if (!strcmp(name, "happy")) {
		expect_success();
	} else if (!strcmp(name, "buffer-low") ||
		   !strcmp(name, "buffer-unmapped") ||
		   !strcmp(name, "buffer-alternate")) {
		uint8_t before[sizeof(communication)];

		memcpy(before, communication, sizeof(before));
		selected_buffer = !strcmp(name, "buffer-low") ? (void *)0x1000 :
			!strcmp(name, "buffer-unmapped") ?
			(void *)(uintptr_t)0x7fff00000000ULL : alternate;
		buffer_available = false;
		assert(capsule_broker_transport_dispatch() == CB_ERR);
		assert(!execute_count);
		assert(!memcmp(before, communication, sizeof(before)));
	} else if (!strcmp(name, "request-revision")) {
		request()->revision++;
		expect_rejected();
	} else if (!strcmp(name, "request-size")) {
		request()->size--;
		expect_rejected();
	} else if (!strcmp(name, "request-operation")) {
		request()->operation++;
		expect_rejected();
	} else if (!strcmp(name, "request-flags")) {
		request()->flags = 1;
		expect_rejected();
	} else if (!strcmp(name, "intent-size-field")) {
		request()->intent_size--;
		expect_rejected();
	} else if (!strcmp(name, "result-size-field")) {
		request()->result_size--;
		expect_rejected();
	} else if (!strcmp(name, "generation")) {
		request()->generation++;
		expect_rejected();
	} else if (!strcmp(name, "intent-generation")) {
		intent()->broker_generation++;
		expect_rejected();
	} else if (!strcmp(name, "transaction")) {
		intent()->transaction++;
		expect_rejected();
	} else if (!strcmp(name, "intent-revision")) {
		intent()->revision++;
		expect_rejected();
	} else if (!strcmp(name, "intent-size")) {
		intent()->size--;
		expect_rejected();
	} else if (!strcmp(name, "intent-operation")) {
		intent()->operation = 3;
		expect_rejected();
	} else if (!strcmp(name, "intent-flags")) {
		intent()->flags = 1;
		expect_rejected();
	} else if (!strcmp(name, "intent-zero-size")) {
		intent()->capsule_size = 0;
		expect_rejected();
	} else if (!strcmp(name, "intent-large")) {
		intent()->capsule_size++;
		expect_rejected();
	} else if (!strcmp(name, "intent-algorithm")) {
		intent()->digest_algorithm++;
		expect_rejected();
	} else if (!strcmp(name, "intent-digest-size")) {
		intent()->digest_size--;
		expect_rejected();
	} else if (!strcmp(name, "intent-reserved")) {
		intent()->reserved = 1;
		expect_rejected();
	} else if (!strcmp(name, "not-ready")) {
		ready = false;
		expect_rejected();
	} else if (!strcmp(name, "failure")) {
		execute_success = false;
		assert(capsule_broker_transport_dispatch() == CB_ERR);
		assert(execute_count == 1);
		assert(result()->generation == generation &&
			result()->transaction == 1);
		assert(result()->last_attempt_status ==
			CAPSULE_BROKER_LAST_ATTEMPT_UNSUCCESSFUL);
		assert(result()->result == CAPSULE_BROKER_RESULT_EXECUTION);
	} else if (!strcmp(name, "mutation")) {
		mutate_input = true;
		expect_success();
	} else if (!strcmp(name, "authority-mutation")) {
		mutate_authority = true;
		assert(capsule_broker_transport_dispatch() == CB_ERR);
		assert(execute_count == 1 && close_count == 1);
		assert(result()->result == CAPSULE_BROKER_RESULT_EXECUTION);
	} else if (!strcmp(name, "reentry")) {
		recurse = true;
		expect_success();
	} else if (!strcmp(name, "replay")) {
		expect_success();
		execute_count = 0;
		memset(result(), 0xcc, sizeof(*result()));
		expect_rejected();
	} else if (!strcmp(name, "maximum")) {
		publish(UINT64_MAX);
		assert(capsule_broker_transport_dispatch() == CB_SUCCESS);
		assert(result()->transaction == UINT64_MAX);
	} else {
		return 1;
	}
	return 0;
}
