/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "capsule_broker_internal.h"
#include "payload_mm_fmp_checkpoint_internal.h"
#include "payload_mm_fmp_dispatch_internal.h"
#include "payload_mm_fmp_owner_internal.h"
#include "payload_mm_fmp_transaction_internal.h"

static struct payload_mm_fmp_capsule_intent intent;
static struct payload_mm_fmp_owner_record record;
static char trace[32];
static size_t trace_size;
static bool dispatch_active;
static bool ready = true;
static bool protect = true;
static bool prepare_fails;
static bool auth_fails;
static bool checkpoint_fails;
static bool apply_fails;
static bool mutate_owner;
static bool auth_mutates_owner;
static bool auth_observes_aba;
static bool mutate_control;
static bool reenter;
static bool guard = true;
static unsigned int guard_calls;
static unsigned int guard_failure;
static bool guard_reenters;
static bool close_during_auth;
static unsigned int owner_reads;
static unsigned int checkpoint_calls;
static unsigned int apply_calls;
static unsigned int closes;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static void mark(char event)
{
	assert(trace_size < sizeof(trace));
	trace[trace_size++] = event;
}

static void mutate_authority(void)
{
	size_t size;
	uint8_t *authority;

	if (!mutate_control)
		return;
	authority = (uint8_t *)payload_mm_fmp_transaction_test_authority(&size);
	assert(size > 0);
	authority[0] ^= 1;
}

bool payload_mm_fmp_dispatch_ready(void)
{
	return ready;
}

bool payload_mm_fmp_owner_ready(void)
{
	return ready;
}

bool payload_mm_fmp_checkpoint_ready(void)
{
	return ready;
}

bool capsule_broker_execution_ready(void)
{
	mark('G');
	guard_calls++;
	if (guard_reenters)
		assert(payload_mm_fmp_transaction_execute(0x1000) == CB_ERR);
	return guard && guard_calls != guard_failure;
}

enum cb_err payload_mm_fmp_dispatch_prepare(uint64_t request_address,
	const void *current_state, size_t current_state_size)
{
	mark('D');
	assert(request_address == 0x1000 && !current_state && !current_state_size);
	if (prepare_fails)
		return CB_ERR;
	dispatch_active = true;
	mutate_authority();
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_dispatch_prepare_intent(
	const struct payload_mm_fmp_capsule_intent *direct_intent)
{
	assert(direct_intent);
	intent = *direct_intent;
	return payload_mm_fmp_dispatch_prepare(0x1000, NULL, 0);
}

const struct payload_mm_fmp_capsule_intent *
payload_mm_fmp_dispatch_capsule_intent(void)
{
	return dispatch_active ? &intent : NULL;
}

const struct payload_mm_fmp_state_command *payload_mm_fmp_dispatch_command(void)
{
	return NULL;
}

enum cb_err payload_mm_fmp_dispatch_complete(uint64_t transaction)
{
	mark('X');
	if (!dispatch_active || transaction != intent.transaction)
		return CB_ERR;
	dispatch_active = false;
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_owner_read(uint32_t key,
	struct payload_mm_fmp_owner_record *output)
{
	mark('R');
	assert(key == PAYLOAD_MM_FMP_STATE_KEY_STATE);
	owner_reads++;
	*output = record;
	if (mutate_owner && owner_reads == 2)
		output->data[0] ^= 1;
	mutate_authority();
	return CB_SUCCESS;
}

enum cb_err capsule_broker_authenticate_intent_bound(
	const struct payload_mm_fmp_capsule_intent *staged,
	const struct payload_mm_fmp_owner_record *owner_record)
{
	mark('A');
	assert(staged == &intent &&
		!memcmp(owner_record, &record, sizeof(record)));
	if (reenter)
		assert(payload_mm_fmp_transaction_execute(0x1000) == CB_ERR);
	if (close_during_auth)
		payload_mm_fmp_transaction_close();
	if (auth_mutates_owner)
		record.data[0] ^= 1;
	if (auth_observes_aba) {
		record.data[0] ^= 1;
		assert(memcmp(owner_record, &record, sizeof(record)) != 0);
		record.data[0] ^= 1;
		assert(!memcmp(owner_record, &record, sizeof(record)));
		return CB_ERR;
	}
	mutate_authority();
	return auth_fails ? CB_ERR : CB_SUCCESS;
}

enum cb_err payload_mm_fmp_checkpoint_commit_bound(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version,
	const struct payload_mm_fmp_owner_record *authenticated_record,
	const uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE])
{
	mark('C');
	checkpoint_calls++;
	assert(generation == intent.broker_generation);
	assert(transaction == intent.transaction);
	assert(attempted_version == intent.attempted_version);
	assert(!memcmp(authenticated_record, &record, sizeof(record)));
	assert(!memcmp(digest, intent.digest, sizeof(intent.digest)));
	mutate_authority();
	return checkpoint_fails ? CB_ERR : CB_SUCCESS;
}

enum cb_err capsule_broker_apply_intent(
	const struct payload_mm_fmp_capsule_intent *staged)
{
	mark('P');
	apply_calls++;
	assert(staged == &intent);
	mutate_authority();
	return apply_fails ? CB_ERR : CB_SUCCESS;
}

void capsule_broker_close_for_s3(void)
{
	mark('L');
	closes++;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	assert(storage != NULL && size > 0);
	if (mutate_control)
		memset((void *)storage, 0xa5, size);
	return protect;
}

static void setup(uint32_t operation)
{
	intent = (struct payload_mm_fmp_capsule_intent) {
		.revision = PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION,
		.size = sizeof(intent),
		.operation = operation,
		.broker_generation = 9,
		.transaction = 7,
		.capsule_size = 4096,
		.digest_algorithm = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256,
		.digest_size = sizeof(intent.digest),
		.attempted_version = 12,
	};
	memset(intent.digest, 0x5a, sizeof(intent.digest));
	record = (struct payload_mm_fmp_owner_record) {
		.sequence = 11,
		.present = 1,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE,
	};
}

static void expect_trace(const char *expected)
{
	assert(trace_size == strlen(expected));
	assert(!memcmp(trace, expected, trace_size));
}

int main(int argc, char **argv)
{
	const char *test = argc > 1 ? argv[1] : "set";
	enum cb_err expected = CB_ERR;

	setup(!strncmp(test, "check", 5) ? PAYLOAD_MM_FMP_CAPSULE_CHECK :
		PAYLOAD_MM_FMP_CAPSULE_SET);
	if (!strcmp(test, "install-not-ready")) {
		ready = false;
		assert(payload_mm_fmp_transaction_install(protected_storage, NULL) ==
			CB_ERR);
		return 0;
	}
	if (!strcmp(test, "install-unprotected")) {
		protect = false;
		assert(payload_mm_fmp_transaction_install(protected_storage, NULL) ==
			CB_ERR);
		return 0;
	}
	if (!strcmp(test, "install-mutation")) {
		mutate_control = true;
		assert(payload_mm_fmp_transaction_install(protected_storage, NULL) ==
			CB_ERR);
		return 0;
	}
	assert(payload_mm_fmp_transaction_install(protected_storage, NULL) ==
		CB_SUCCESS);
	if (!strcmp(test, "prepare-failure"))
		prepare_fails = true;
	else if (!strcmp(test, "auth-failure"))
		auth_fails = true;
	else if (!strcmp(test, "owner-interleave"))
		mutate_owner = true;
	else if (!strcmp(test, "check-same-sequence"))
		mutate_owner = true;
	else if (!strcmp(test, "check-mutation"))
		auth_mutates_owner = true;
	else if (!strcmp(test, "check-aba"))
		auth_observes_aba = true;
	else if (!strcmp(test, "set-mutation"))
		auth_mutates_owner = true;
	else if (!strcmp(test, "set-aba"))
		auth_observes_aba = true;
	else if (!strcmp(test, "checkpoint-failure"))
		checkpoint_fails = true;
	else if (!strcmp(test, "apply-failure"))
		apply_fails = true;
	else if (!strcmp(test, "callback-mutation"))
		mutate_control = true;
	else if (!strcmp(test, "reentry"))
		reenter = true;
	else if (!strcmp(test, "lifecycle-close"))
		close_during_auth = true;
	else if (!strcmp(test, "guard-entry"))
		guard_failure = 2;
	else if (!strcmp(test, "guard-owner"))
		guard_failure = 3;
	else if (!strcmp(test, "guard-auth"))
		guard_failure = 4;
	else if (!strcmp(test, "guard-readback"))
		guard_failure = 5;
	else if (!strcmp(test, "guard-reentry"))
		guard_reenters = true;
	else if (!strcmp(test, "set") || !strcmp(test, "check") ||
		 !strcmp(test, "replay") || !strcmp(test, "close") ||
		 !strcmp(test, "guard-reentry"))
		expected = CB_SUCCESS;
	else
		return 2;
	assert(payload_mm_fmp_transaction_execute(0x1000) == expected);
	if (!strcmp(test, "set")) {
		expect_trace("GGDRGAGRGCPXL");
		assert(checkpoint_calls == 1 && apply_calls == 1 && closes == 1);
	} else if (!strcmp(test, "check")) {
		expect_trace("GGDRGAGRGX");
		assert(!checkpoint_calls && !apply_calls && !closes);
	} else if (!strncmp(test, "check-", 6) ||
		   !strncmp(test, "set-", 4)) {
		assert(!checkpoint_calls && !apply_calls && closes == 1);
		assert(payload_mm_fmp_transaction_execute(0x1000) == CB_ERR);
	} else if (!strcmp(test, "replay")) {
		assert(payload_mm_fmp_transaction_execute(0x1000) == CB_ERR);
	} else if (!strcmp(test, "close")) {
		payload_mm_fmp_transaction_close();
		assert(payload_mm_fmp_transaction_execute(0x1000) == CB_ERR);
	} else if (!strcmp(test, "lifecycle-close")) {
		assert(closes == 2);
		assert(payload_mm_fmp_transaction_execute(0x1000) == CB_ERR);
	} else {
		assert(closes == 1);
		assert(payload_mm_fmp_transaction_execute(0x1000) == CB_ERR);
	}
	return 0;
}
