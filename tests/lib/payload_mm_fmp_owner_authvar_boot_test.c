/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <boot/payload_mm_authvar_service.h>
#include "payload_mm_authvar_fmp_internal.h"
#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_owner_authvar_boot_internal.h"

enum fault_mode {
	FAULT_NONE,
	FAULT_INIT_ONCE,
	FAULT_INIT_POISON,
	FAULT_INSTALL,
	FAULT_ACTIVATE,
	FAULT_CAS_CANDIDATE,
	FAULT_CAS_CURRENT,
	FAULT_CAS_THIRD,
	FAULT_CAS_IDENTICAL,
	FAULT_CONTRACT,
	FAULT_STORAGE,
	FAULT_REENTRY,
	FAULT_BACKEND_REENTRY,
	FAULT_MUTATE_CURRENT,
	FAULT_MUTATE_CANDIDATE,
	FAULT_PROOF_REPEAT,
	FAULT_PROOF_MISSING,
	FAULT_PROOF_MUTATE_LIVE,
	FAULT_CAS_RESERVED,
	FAULT_CAS_RESERVED2,
	FAULT_RAW_READ_MUTATE_LIVE,
	FAULT_RAW_READ_MUTATE_BOTH,
	FAULT_SNAPSHOT_MUTATE_LIVE,
	FAULT_SNAPSHOT_MUTATE_BOTH,
	FAULT_INITIALIZE_MUTATE_BOTH,
	FAULT_CAS_AUTHORITY_BOTH,
	FAULT_CAS_MALFORMED,
	FAULT_ACTIVATE_MUTATE_BOTH,
	FAULT_INSTALL_MUTATE_BOTH,
	FAULT_INIT_FAIL_MUTATE_BOTH,
};

static enum fault_mode fault;
static struct payload_mm_authvar_contract contract = {
	.revision = 1U,
	.size = sizeof(contract),
	.generation = 17U,
};
static struct payload_mm_fmp_owner_record media_record;
static struct payload_mm_fmp_owner_backend installed_backend;
#ifndef REAL_OWNER
static uint8_t installed_context[PAYLOAD_MM_FMP_OWNER_CONTEXT_SIZE];
#endif
static struct payload_mm_fmp_state_identity identity[5];
static unsigned int identity_install_calls;
static unsigned int initialize_calls;
static unsigned int transaction_calls;
static unsigned int activate_calls;
static unsigned int abort_calls;
static bool executor_aborted;
static bool owner_installed;
static bool identity_source_closed;
static bool reconciliation_retryable = true;
static bool mutate_retryable;
static struct payload_mm_fmp_owner_record *caller_current;
static struct payload_mm_fmp_owner_record *caller_candidate;

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!value)
		abort();
}

static void record_init(struct payload_mm_fmp_owner_record *record,
	uint64_t sequence, uint8_t value)
{
	memset(record, 0, sizeof(*record));
	record->sequence = sequence;
	record->present = 1U;
	record->attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES;
	record->data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE;
	record->data[0] = 1U;
	record->data[4] = value;
}

bool payload_mm_authvar_authority_snapshot(
	struct payload_mm_authvar_contract *snapshot)
{
	if (fault == FAULT_SNAPSHOT_MUTATE_LIVE ||
	    fault == FAULT_SNAPSHOT_MUTATE_BOTH) {
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(false);
		if (fault == FAULT_SNAPSHOT_MUTATE_BOTH)
			payload_mm_fmp_owner_authvar_boot_test_corrupt_current(true);
		fault = FAULT_NONE;
	}
	*snapshot = contract;
	return true;
}

enum cb_err payload_mm_fmp_owner_authvar_identity_install(
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	(void)storage_is_protected;
	(void)context;
	identity_install_calls++;
	for (uint32_t key = 0; key < 5U; key++) {
		memset(&identity[key], 0, sizeof(identity[key]));
		identity[key].namespace_guid.b[0] = 0xa0U + key;
		identity[key].variable_name_bytes = 4U;
		identity[key].variable_name[0] = 'A' + key;
	}
	return CB_SUCCESS;
}

enum cb_err payload_mm_fmp_owner_authvar_identity(uint32_t key,
	struct payload_mm_fmp_state_identity *result)
{
	if (identity_source_closed || key >= 5U)
		return CB_ERR;
	*result = identity[key];
	return CB_SUCCESS;
}

#ifndef REAL_OWNER
bool payload_mm_fmp_owner_observed_record_valid(uint32_t key,
	const struct payload_mm_fmp_owner_record *record)
{
	uint32_t size = key == PAYLOAD_MM_FMP_STATE_KEY_STATE ?
		PAYLOAD_MM_FMP_STATE_WIRE_SIZE : sizeof(uint32_t);

	return key < 5U && record && record->sequence && record->present <= 1U &&
		(!record->present ||
		 (record->attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
		  record->data_size == size));
}

bool payload_mm_fmp_owner_record_valid(uint32_t key,
	const struct payload_mm_fmp_owner_record *record)
{
	return payload_mm_fmp_owner_observed_record_valid(key, record);
}
#endif

uint64_t payload_mm_authvar_fmp_state_initialize(
	struct payload_mm_fmp_owner_record *observation)
{
	initialize_calls++;
	if (fault == FAULT_REENTRY) {
		assert(payload_mm_fmp_owner_authvar_boot_install(NULL, NULL) == CB_ERR);
		fault = FAULT_NONE;
	}
	if (fault == FAULT_INIT_ONCE) {
		fault = FAULT_NONE;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	if (fault == FAULT_INIT_POISON) {
		reconciliation_retryable = false;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	if (fault == FAULT_INIT_FAIL_MUTATE_BOTH) {
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(false);
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(true);
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	if (fault == FAULT_CONTRACT)
		contract.generation++;
	if (fault == FAULT_INITIALIZE_MUTATE_BOTH) {
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(false);
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(true);
		fault = FAULT_NONE;
	}
	*observation = media_record;
	observation->sequence = 0U;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

bool payload_mm_authvar_fmp_state_reconciliation_retryable(void)
{
	if (mutate_retryable) {
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(false);
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(true);
		mutate_retryable = false;
	}
	return reconciliation_retryable;
}

uint64_t payload_mm_authvar_fmp_state_activation_read(
	struct payload_mm_fmp_owner_record *observation)
{
	*observation = media_record;
	observation->sequence = 0U;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t payload_mm_authvar_fmp_state_transaction(
	enum payload_mm_authvar_fmp_operation operation,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate,
	struct payload_mm_fmp_owner_record *observation)
{
	transaction_calls++;
	if (operation == PAYLOAD_MM_AUTHVAR_FMP_READ_STATE) {
		if (fault == FAULT_RAW_READ_MUTATE_LIVE ||
		    fault == FAULT_RAW_READ_MUTATE_BOTH) {
			payload_mm_fmp_owner_authvar_boot_test_corrupt_current(false);
			if (fault == FAULT_RAW_READ_MUTATE_BOTH)
				payload_mm_fmp_owner_authvar_boot_test_corrupt_current(true);
			fault = FAULT_NONE;
			return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		}
		*observation = media_record;
		observation->sequence = 0U;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
	assert(operation == PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE);
	assert(current && candidate);
	if (fault == FAULT_BACKEND_REENTRY) {
		struct payload_mm_fmp_owner_record nested;

#ifdef REAL_OWNER
		assert(payload_mm_fmp_owner_read(0U, &nested) == CB_ERR);
#else
		assert(installed_backend.read(installed_backend.context, &identity[0],
			0U, &nested) == CB_ERR);
#endif
	}
	if (fault == FAULT_MUTATE_CURRENT)
		caller_current->data[4] ^= 0x40U;
	if (fault == FAULT_MUTATE_CANDIDATE)
		caller_candidate->data[4] ^= 0x40U;
	if (fault == FAULT_CAS_AUTHORITY_BOTH) {
		payload_mm_fmp_owner_authvar_boot_test_corrupt_pending(false);
		payload_mm_fmp_owner_authvar_boot_test_corrupt_pending(true);
	}
	if (fault == FAULT_CAS_CANDIDATE) {
		media_record = *candidate;
		media_record.sequence = 0U;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	if (fault == FAULT_CAS_CURRENT || fault == FAULT_CAS_IDENTICAL)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	if (fault == FAULT_CAS_THIRD) {
		media_record.data[4] ^= 0x80U;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	if (fault == FAULT_CAS_MALFORMED) {
		media_record.reserved = 1U;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	media_record = *candidate;
	media_record.sequence = 0U;
	*observation = media_record;
	if (fault == FAULT_CAS_RESERVED)
		observation->reserved = 1U;
	if (fault == FAULT_CAS_RESERVED2)
		observation->reserved2 = 1U;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

enum cb_err payload_mm_authvar_fmp_state_activate(void)
{
	activate_calls++;
	if (fault == FAULT_ACTIVATE_MUTATE_BOTH) {
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(false);
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(true);
		return CB_SUCCESS;
	}
	return fault == FAULT_ACTIVATE ? CB_ERR : CB_SUCCESS;
}

void payload_mm_authvar_fmp_state_activation_abort(void)
{
	if (!executor_aborted) {
		executor_aborted = true;
		abort_calls++;
	}
}

#ifndef REAL_OWNER
enum cb_err payload_mm_fmp_owner_install_checked(
	const struct payload_mm_fmp_owner_backend *backend,
	payload_mm_authvar_protected_storage storage_is_protected, void *context,
	uint32_t initial_key,
	const struct payload_mm_fmp_owner_record *initial_record)
{
	struct payload_mm_fmp_owner_record observed;

	(void)storage_is_protected;
	(void)context;
	if (fault == FAULT_INSTALL_MUTATE_BOTH) {
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(false);
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(true);
		return CB_ERR;
	}
	if (fault == FAULT_INSTALL)
		return CB_ERR;
	assert(initial_key == PAYLOAD_MM_FMP_STATE_KEY_STATE);
	assert(initial_record);
	installed_backend = *backend;
	assert(backend->context_size <= sizeof(installed_context));
	memcpy(installed_context, backend->context, backend->context_size);
	installed_backend.context = installed_context;
	memset(&observed, 0, sizeof(observed));
	if (fault != FAULT_PROOF_MISSING) {
		assert(installed_backend.read(installed_backend.context,
			&identity[initial_key], initial_key, &observed) == CB_SUCCESS);
		assert(!memcmp(&observed, initial_record, sizeof(observed)));
	}
	if (fault == FAULT_PROOF_MUTATE_LIVE)
		payload_mm_fmp_owner_authvar_boot_test_corrupt_proof(false);
	if (fault == FAULT_PROOF_REPEAT)
		assert(installed_backend.read(installed_backend.context,
			&identity[initial_key], initial_key, &observed) == CB_ERR);
	owner_installed = true;
	return CB_SUCCESS;
}
#else
bool payload_mm_fmp_state_authority_ready(void)
{
	return true;
}

bool payload_mm_fmp_state_staging_buffer(const void *buffer, size_t size)
{
	return buffer && size &&
		(uintptr_t)buffer % _Alignof(struct payload_mm_fmp_owner_record) == 0;
}

bool payload_mm_fmp_state_data_valid(
	const uint8_t state[PAYLOAD_MM_FMP_STATE_WIRE_SIZE])
{
	return state && state[0] <= 1U && state[1] <= 1U && state[2] <= 1U &&
		state[3] <= 1U;
}

bool payload_mm_fmp_state_transition_valid(const uint8_t *current,
	const uint8_t candidate[PAYLOAD_MM_FMP_STATE_WIRE_SIZE])
{
	(void)current;
	return payload_mm_fmp_state_data_valid(candidate);
}

enum cb_err payload_mm_fmp_state_identity_get_for_key(uint32_t key,
	struct payload_mm_fmp_state_identity *result)
{
	return payload_mm_fmp_owner_authvar_identity(key, result);
}

bool payload_mm_authvar_buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_base = (uintptr_t)left;
	uintptr_t right_base = (uintptr_t)right;

	return left_size && right_size && left_base < right_base + right_size &&
		right_base < left_base + left_size;
}
#endif

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	assert(storage);
	assert(size);
	if (fault == FAULT_STORAGE) {
		((uint8_t *)(uintptr_t)storage)[0] ^= 1U;
		fault = FAULT_NONE;
	}
	return true;
}

static void install_ok(void)
{
	assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage, NULL) ==
		CB_SUCCESS);
#ifdef REAL_OWNER
	assert(payload_mm_fmp_owner_ready());
#else
	assert(owner_installed);
#endif
	assert(activate_calls == 1U);
	assert(abort_calls == 0U);
}

static void read_current(struct payload_mm_fmp_owner_record *record)
{
	memset(record, 0, sizeof(*record));
#ifdef REAL_OWNER
	assert(payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE,
		record) == CB_SUCCESS);
#else
	assert(installed_backend.read(installed_backend.context,
		&identity[PAYLOAD_MM_FMP_STATE_KEY_STATE],
		PAYLOAD_MM_FMP_STATE_KEY_STATE, record) == CB_SUCCESS);
#endif
}

static enum cb_err read_status(struct payload_mm_fmp_owner_record *record)
{
#ifdef REAL_OWNER
	return payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE, record);
#else
	return installed_backend.read(installed_backend.context,
		&identity[PAYLOAD_MM_FMP_STATE_KEY_STATE],
		PAYLOAD_MM_FMP_STATE_KEY_STATE, record);
#endif
}

static void test_corruption(const char *name)
{
	struct payload_mm_fmp_owner_record record;
	bool sealed = strstr(name, "sealed") != NULL;

	install_ok();
	if (strstr(name, "phase") && strstr(name, "both")) {
		payload_mm_fmp_owner_authvar_boot_test_corrupt_phase(false);
		payload_mm_fmp_owner_authvar_boot_test_corrupt_phase(true);
	} else if (strstr(name, "phase"))
		payload_mm_fmp_owner_authvar_boot_test_corrupt_phase(sealed);
	else if (strstr(name, "control"))
		payload_mm_fmp_owner_authvar_boot_test_corrupt_control(sealed);
	else if (strstr(name, "contract"))
		payload_mm_fmp_owner_authvar_boot_test_corrupt_contract(sealed);
	else if (strstr(name, "identity"))
		payload_mm_fmp_owner_authvar_boot_test_corrupt_identity(sealed);
	else if (strstr(name, "current"))
		payload_mm_fmp_owner_authvar_boot_test_corrupt_current(sealed);
	else if (strstr(name, "pending"))
		payload_mm_fmp_owner_authvar_boot_test_corrupt_pending(sealed);
	else
		abort();
	assert(read_status(&record) == CB_ERR);
	assert(abort_calls == 1U);
	assert(transaction_calls == 0U);
}

static void test_active_read_fault(enum fault_mode mode)
{
	struct payload_mm_fmp_owner_record record;
	struct payload_mm_fmp_owner_record sentinel;

	install_ok();
	fault = mode;
	memset(&record, 0xa5, sizeof(record));
	sentinel = record;
	assert(read_status(&record) == CB_ERR);
	assert(!memcmp(&record, &sentinel, sizeof(record)));
	assert(abort_calls == 1U);
	assert(read_status(&record) == CB_ERR);
}

static void test_preinstall_corruption(bool sealed, bool phase)
{
	if (phase)
		payload_mm_fmp_owner_authvar_boot_test_corrupt_phase(sealed);
	else
		payload_mm_fmp_owner_authvar_boot_test_corrupt_control(sealed);
	assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage, NULL) ==
		CB_ERR);
	assert(identity_install_calls == 0U);
	assert(abort_calls == 1U);
}

static void test_retry_corruption(bool sealed)
{
	fault = FAULT_INIT_ONCE;
	assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage, NULL) ==
		CB_ERR);
	payload_mm_fmp_owner_authvar_boot_test_corrupt_control(sealed);
	assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage, NULL) ==
		CB_ERR);
	assert(identity_install_calls == 1U);
	assert(abort_calls == 1U);
}

static void test_retry_callback_mutation(void)
{
	fault = FAULT_INIT_ONCE;
	mutate_retryable = true;
	assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage, NULL) ==
		CB_ERR);
	assert(abort_calls == 1U);
	assert(identity_install_calls == 1U);
}

static enum cb_err commit_value(struct payload_mm_fmp_owner_record *current,
	uint8_t value, bool identical)
{
	struct payload_mm_fmp_owner_record candidate = *current;
	enum cb_err result;

	candidate.sequence++;
	if (!identical)
		candidate.data[4] = value;
	caller_current = current;
	caller_candidate = &candidate;
#ifdef REAL_OWNER
	result = payload_mm_fmp_owner_commit_state(current, &candidate);
#else
	result = installed_backend.commit(installed_backend.context,
		&identity[PAYLOAD_MM_FMP_STATE_KEY_STATE],
		PAYLOAD_MM_FMP_STATE_KEY_STATE, current, &candidate);
#endif
	caller_current = NULL;
	caller_candidate = NULL;
	return result;
}

static void test_retry(void)
{
	fault = FAULT_INIT_ONCE;
	assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage, NULL) ==
		CB_ERR);
	assert(identity_install_calls == 1U);
	assert(abort_calls == 0U);
	install_ok();
	assert(identity_install_calls == 1U);
	assert(initialize_calls == 2U);
}

static void test_multi(void)
{
	struct payload_mm_fmp_owner_record current;

	install_ok();
	read_current(&current);
	assert(current.sequence == contract.generation);
	assert(commit_value(&current, 2U, false) == CB_SUCCESS);
	read_current(&current);
	assert(current.sequence == contract.generation + 1U);
	assert(commit_value(&current, 3U, false) == CB_SUCCESS);
	read_current(&current);
	assert(current.sequence == contract.generation + 2U);
	assert(current.data[4] == 3U);
}

static void test_near_wrap(void)
{
	struct payload_mm_fmp_owner_record current;
	unsigned int before;

	contract.generation = UINT64_MAX - 1U;
	install_ok();
	read_current(&current);
	assert(commit_value(&current, 2U, false) == CB_SUCCESS);
	read_current(&current);
	assert(current.sequence == UINT64_MAX);
	before = transaction_calls;
	assert(commit_value(&current, 3U, false) == CB_ERR);
	assert(transaction_calls == before);
	read_current(&current);
	assert(current.sequence == UINT64_MAX);
	assert(current.data[4] == 2U);
}

static void test_advisory(enum fault_mode mode, bool identical,
	bool expect_success, bool expect_poison)
{
	struct payload_mm_fmp_owner_record current;

	install_ok();
	read_current(&current);
	fault = mode;
	assert((commit_value(&current, 9U, identical) == CB_SUCCESS) ==
		expect_success);
	if (expect_poison) {
		assert(abort_calls == 1U);
#ifdef REAL_OWNER
		assert(payload_mm_fmp_owner_read(0U, &current) == CB_ERR);
#else
		assert(installed_backend.read(installed_backend.context,
			&identity[0], 0U, &current) == CB_ERR);
#endif
		return;
	}
	read_current(&current);
	assert(current.sequence == contract.generation + expect_success);
}

static void test_install_failure(enum fault_mode mode)
{
	fault = mode;
	assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage, NULL) ==
		CB_ERR);
	assert(abort_calls == 1U);
	assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage, NULL) ==
		CB_ERR);
	if (owner_installed) {
		struct payload_mm_fmp_owner_record record;

		assert(installed_backend.read(installed_backend.context, &identity[0],
			0U, &record) == CB_ERR);
	}
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	record_init(&media_record, 0U, 1U);
	if (!strcmp(argv[1], "retry"))
		test_retry();
	else if (!strcmp(argv[1], "multi"))
		test_multi();
	else if (!strcmp(argv[1], "near-wrap"))
		test_near_wrap();
	else if (!strcmp(argv[1], "candidate"))
		test_advisory(FAULT_CAS_CANDIDATE, false, true, false);
	else if (!strcmp(argv[1], "current"))
		test_advisory(FAULT_CAS_CURRENT, false, false, false);
	else if (!strcmp(argv[1], "third"))
		test_advisory(FAULT_CAS_THIRD, false, false, true);
	else if (!strcmp(argv[1], "reserved"))
		test_advisory(FAULT_CAS_RESERVED, false, false, true);
	else if (!strcmp(argv[1], "reserved2"))
		test_advisory(FAULT_CAS_RESERVED2, false, false, true);
	else if (!strcmp(argv[1], "authority-both"))
		test_advisory(FAULT_CAS_AUTHORITY_BOTH, false, false, true);
	else if (!strcmp(argv[1], "identical-success"))
		test_advisory(FAULT_NONE, true, true, false);
	else if (!strcmp(argv[1], "identical-error"))
		test_advisory(FAULT_CAS_IDENTICAL, true, false, false);
	else if (!strcmp(argv[1], "identical-third"))
		test_advisory(FAULT_CAS_THIRD, true, false, true);
	else if (!strcmp(argv[1], "identical-malformed"))
		test_advisory(FAULT_CAS_MALFORMED, true, false, true);
	else if (!strcmp(argv[1], "backend-reentry"))
		test_advisory(FAULT_BACKEND_REENTRY, false, false, true);
	else if (!strcmp(argv[1], "mutate-current"))
		test_advisory(FAULT_MUTATE_CURRENT, false, false, true);
	else if (!strcmp(argv[1], "mutate-candidate"))
		test_advisory(FAULT_MUTATE_CANDIDATE, false, false, true);
	else if (!strcmp(argv[1], "install-fail"))
		test_install_failure(FAULT_INSTALL);
	else if (!strcmp(argv[1], "activate-fail"))
		test_install_failure(FAULT_ACTIVATE);
	else if (!strcmp(argv[1], "init-poison"))
		test_install_failure(FAULT_INIT_POISON);
	else if (!strcmp(argv[1], "proof-repeat"))
		test_install_failure(FAULT_PROOF_REPEAT);
	else if (!strcmp(argv[1], "proof-missing"))
		test_install_failure(FAULT_PROOF_MISSING);
	else if (!strcmp(argv[1], "proof-mutate-live"))
		test_install_failure(FAULT_PROOF_MUTATE_LIVE);
	else if (!strcmp(argv[1], "initialize-mutate-both"))
		test_install_failure(FAULT_INITIALIZE_MUTATE_BOTH);
	else if (!strcmp(argv[1], "initialize-fail-mutate-both"))
		test_install_failure(FAULT_INIT_FAIL_MUTATE_BOTH);
	else if (!strcmp(argv[1], "install-fail-mutate-both"))
		test_install_failure(FAULT_INSTALL_MUTATE_BOTH);
	else if (!strcmp(argv[1], "activate-mutate-both"))
		test_install_failure(FAULT_ACTIVATE_MUTATE_BOTH);
	else if (!strcmp(argv[1], "contract"))
		test_install_failure(FAULT_CONTRACT);
	else if (!strcmp(argv[1], "storage")) {
		fault = FAULT_STORAGE;
		assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage,
			NULL) == CB_ERR);
		assert(!owner_installed);
	} else if (!strcmp(argv[1], "reentry")) {
		fault = FAULT_REENTRY;
		install_ok();
	} else if (!strcmp(argv[1], "generation-zero") ||
		   !strcmp(argv[1], "generation-max")) {
		contract.generation = !strcmp(argv[1], "generation-zero") ? 0U :
			UINT64_MAX;
		assert(payload_mm_fmp_owner_authvar_boot_install(protected_storage,
			NULL) == CB_ERR);
		assert(!owner_installed);
	} else if (!strcmp(argv[1], "legacy-logical-absent")) {
		struct payload_mm_fmp_owner_record record;
		unsigned int before;

		install_ok();
		before = transaction_calls;
		memset(&record, 0xff, sizeof(record));
#ifdef REAL_OWNER
		assert(payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_VERSION,
			&record) == CB_SUCCESS);
#else
		assert(installed_backend.read(installed_backend.context,
			&identity[PAYLOAD_MM_FMP_STATE_KEY_VERSION],
			PAYLOAD_MM_FMP_STATE_KEY_VERSION, &record) == CB_SUCCESS);
#endif
		assert(record.sequence == contract.generation);
		assert(record.present == 0U);
		assert(transaction_calls == before);
	} else if (!strcmp(argv[1], "close-survival")) {
		struct payload_mm_fmp_owner_record record;

		install_ok();
		identity_source_closed = true;
		read_current(&record);
		assert(commit_value(&record, 7U, false) == CB_SUCCESS);
		read_current(&record);
		assert(record.data[4] == 7U);
	} else if (!strncmp(argv[1], "corrupt-", 8U)) {
		test_corruption(argv[1]);
	} else if (!strcmp(argv[1], "raw-read-mutate-live")) {
		test_active_read_fault(FAULT_RAW_READ_MUTATE_LIVE);
	} else if (!strcmp(argv[1], "raw-read-mutate-both")) {
		test_active_read_fault(FAULT_RAW_READ_MUTATE_BOTH);
	} else if (!strcmp(argv[1], "snapshot-mutate-live")) {
		test_active_read_fault(FAULT_SNAPSHOT_MUTATE_LIVE);
	} else if (!strcmp(argv[1], "snapshot-mutate-both")) {
		test_active_read_fault(FAULT_SNAPSHOT_MUTATE_BOTH);
	} else if (!strcmp(argv[1], "preinstall-live")) {
		test_preinstall_corruption(false, false);
	} else if (!strcmp(argv[1], "preinstall-sealed")) {
		test_preinstall_corruption(true, false);
	} else if (!strcmp(argv[1], "preinstall-phase-live")) {
		test_preinstall_corruption(false, true);
	} else if (!strcmp(argv[1], "preinstall-phase-sealed")) {
		test_preinstall_corruption(true, true);
	} else if (!strcmp(argv[1], "retry-corrupt-live")) {
		test_retry_corruption(false);
	} else if (!strcmp(argv[1], "retry-corrupt-sealed")) {
		test_retry_corruption(true);
	} else if (!strcmp(argv[1], "retry-callback-mutate-both")) {
		test_retry_callback_mutation();
	} else {
		abort();
	}
	return 0;
}
