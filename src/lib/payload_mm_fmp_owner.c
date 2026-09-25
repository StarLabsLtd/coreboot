/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_owner_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP owner must only be built in SMM"
#endif

struct owner_policy {
	struct payload_mm_fmp_owner_backend backend;
	uint8_t context[PAYLOAD_MM_FMP_OWNER_CONTEXT_SIZE] __aligned(8);
};

struct owner_control {
	uint32_t installed;
	uint32_t install_attempted;
	uint32_t poisoned;
};

#define FMP_OWNER_KEY_COUNT \
	(PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION + 1U)

static struct {
	struct owner_policy policy;
	struct owner_policy sealed_policy;
	struct payload_mm_fmp_state_identity identity[FMP_OWNER_KEY_COUNT];
	struct payload_mm_fmp_state_identity sealed_identity[FMP_OWNER_KEY_COUNT];
	struct owner_control control;
	struct owner_control sealed_control;
	uint32_t operation;
} owner_authority;

_Static_assert(__atomic_always_lock_free(sizeof(owner_authority.operation), 0),
	"Payload-MM FMP owner operation guard must be lock-free");

static bool bytes_zero(const uint8_t *data, size_t size)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < size; i++)
		bits |= data[i];
	return bits == 0;
}

static bool key_valid(uint32_t key)
{
	return key <= PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION;
}

static void poison(void)
{
	owner_authority.control.poisoned = 1U;
	owner_authority.sealed_control.poisoned = 1U;
}

static bool bindings_valid(void)
{
	struct payload_mm_fmp_owner_backend policy = owner_authority.policy.backend;
	struct payload_mm_fmp_owner_backend sealed =
		owner_authority.sealed_policy.backend;
	bool valid;

	policy.context = NULL;
	sealed.context = NULL;
	valid = !memcmp(&policy, &sealed, sizeof(policy)) &&
		((!owner_authority.policy.backend.context_size &&
		  !owner_authority.policy.backend.context &&
		  !owner_authority.sealed_policy.backend.context) ||
		 (owner_authority.policy.backend.context_size &&
		  owner_authority.policy.backend.context ==
			owner_authority.policy.context &&
		  owner_authority.sealed_policy.backend.context ==
			owner_authority.sealed_policy.context)) &&
		!memcmp(owner_authority.policy.context,
			owner_authority.sealed_policy.context,
			owner_authority.policy.backend.context_size) &&
		!memcmp(owner_authority.identity,
			owner_authority.sealed_identity,
			sizeof(owner_authority.identity));
	if (!valid)
		poison();
	return valid;
}

static bool authority_valid(void)
{
	bool valid = !memcmp(&owner_authority.control,
			&owner_authority.sealed_control,
			sizeof(owner_authority.control)) &&
		owner_authority.control.installed == 1U &&
		owner_authority.control.install_attempted == 1U &&
		owner_authority.control.poisoned == 0U && bindings_valid();

	if (!valid)
		poison();
	return valid;
}

static bool install_control_valid(void)
{
	return !memcmp(&owner_authority.control,
			&owner_authority.sealed_control,
			sizeof(owner_authority.control)) &&
		owner_authority.control.install_attempted == 1U &&
		owner_authority.control.installed == 0U &&
		owner_authority.control.poisoned == 0U;
}

static bool operation_begin(void)
{
	uint32_t expected = 0;

	return __atomic_compare_exchange_n(&owner_authority.operation, &expected,
		1U, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void operation_end(void)
{
	__atomic_store_n(&owner_authority.operation, 0U, __ATOMIC_RELEASE);
}

static bool record_valid(uint32_t key,
	const struct payload_mm_fmp_owner_record *record, bool emitted)
{
	uint32_t expected_size = key == PAYLOAD_MM_FMP_STATE_KEY_STATE ?
		PAYLOAD_MM_FMP_STATE_WIRE_SIZE : sizeof(uint32_t);

	if (!key_valid(key) || !record->sequence || record->present > 1 ||
	    record->reserved || record->reserved2)
		return false;
	if (!record->present)
		return !record->attributes && !record->data_size &&
			bytes_zero(record->data, sizeof(record->data));
	if (record->attributes != PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES ||
	    record->data_size != expected_size ||
	    (key != PAYLOAD_MM_FMP_STATE_KEY_STATE &&
	     !bytes_zero(record->data + sizeof(uint32_t),
		sizeof(record->data) - sizeof(uint32_t))))
		return false;
	return key != PAYLOAD_MM_FMP_STATE_KEY_STATE || !emitted ||
		payload_mm_fmp_state_data_valid(record->data);
}

bool payload_mm_fmp_owner_record_valid(uint32_t key,
	const struct payload_mm_fmp_owner_record *record)
{
	return record_valid(key, record, true);
}

bool payload_mm_fmp_owner_observed_record_valid(uint32_t key,
	const struct payload_mm_fmp_owner_record *record)
{
	return record_valid(key, record, false);
}

static bool owner_buffer(const void *buffer, size_t size)
{
	return buffer &&
		(uintptr_t)buffer %
			_Alignof(struct payload_mm_fmp_owner_record) == 0 &&
		payload_mm_fmp_state_staging_buffer(buffer, size) &&
		!payload_mm_authvar_buffers_overlap(buffer, size, &owner_authority,
			sizeof(owner_authority));
}

static enum cb_err owner_install(
	const struct payload_mm_fmp_owner_backend *trusted_backend,
	payload_mm_authvar_protected_storage storage_is_protected, void *context,
	uint32_t initial_key,
	const struct payload_mm_fmp_owner_record *initial_record, bool seed_required)
{
	struct payload_mm_fmp_owner_backend snapshot;
	uint8_t context_snapshot[PAYLOAD_MM_FMP_OWNER_CONTEXT_SIZE];
	struct payload_mm_fmp_owner_record expected_initial;
	struct payload_mm_fmp_owner_record observed;
	struct payload_mm_fmp_state_identity initial_identity;
	struct payload_mm_fmp_state_identity expected_identity;
	bool protected;

	if (owner_authority.control.install_attempted ||
	    owner_authority.sealed_control.install_attempted)
		return CB_ERR;
	owner_authority.control.install_attempted = 1U;
	owner_authority.sealed_control.install_attempted = 1U;
	if (!payload_mm_fmp_state_authority_ready() || !trusted_backend ||
	    !storage_is_protected || (seed_required && !initial_record) ||
	    (initial_record && !key_valid(initial_key)))
		return CB_ERR;
	if (initial_record) {
		expected_initial = *initial_record;
		if (!payload_mm_fmp_owner_observed_record_valid(initial_key,
			&expected_initial))
			return CB_ERR;
	}
	memcpy(&snapshot, trusted_backend, sizeof(snapshot));
	if (snapshot.revision != PAYLOAD_MM_FMP_OWNER_REVISION ||
	    snapshot.size != sizeof(snapshot) || !snapshot.read || !snapshot.commit ||
	    ((snapshot.context == NULL) != (snapshot.context_size == 0)) ||
	    snapshot.context_size > sizeof(context_snapshot) ||
	    (snapshot.context != NULL &&
	     payload_mm_authvar_buffers_overlap(snapshot.context,
		snapshot.context_size, &owner_authority,
		sizeof(owner_authority))))
		return CB_ERR;
	if (snapshot.context_size)
		memcpy(context_snapshot, snapshot.context, snapshot.context_size);
	protected = storage_is_protected(context, &owner_authority,
		sizeof(owner_authority));
	if (!protected || !install_control_valid()) {
		memset(&owner_authority, 0, sizeof(owner_authority));
		owner_authority.control.install_attempted = 1U;
		owner_authority.sealed_control.install_attempted = 1U;
		return CB_ERR;
	}
	memset(&owner_authority.policy, 0, sizeof(owner_authority.policy));
	memset(&owner_authority.sealed_policy, 0,
		sizeof(owner_authority.sealed_policy));
	for (uint32_t key = 0; key < FMP_OWNER_KEY_COUNT; key++)
		if (payload_mm_fmp_state_identity_get_for_key(key,
			&owner_authority.identity[key]) != CB_SUCCESS)
			goto fail;
	owner_authority.policy.backend = snapshot;
	if (snapshot.context_size) {
		memcpy(owner_authority.policy.context, context_snapshot,
			snapshot.context_size);
		memcpy(owner_authority.sealed_policy.context, context_snapshot,
			snapshot.context_size);
		owner_authority.policy.backend.context =
			owner_authority.policy.context;
	}
	owner_authority.sealed_policy.backend = owner_authority.policy.backend;
	if (snapshot.context_size)
		owner_authority.sealed_policy.backend.context =
			owner_authority.sealed_policy.context;
	memcpy(owner_authority.sealed_identity, owner_authority.identity,
		sizeof(owner_authority.identity));
	if (initial_record) {
		initial_identity = owner_authority.sealed_identity[initial_key];
		expected_identity = initial_identity;
		memset(&observed, 0, sizeof(observed));
		if (!bindings_valid() || !operation_begin())
			goto fail;
		if (owner_authority.policy.backend.read(
			owner_authority.policy.context, &initial_identity, initial_key,
			&observed) != CB_SUCCESS ||
		    memcmp(&initial_identity, &expected_identity,
			sizeof(initial_identity)) || !bindings_valid() ||
		    !install_control_valid() ||
		    memcmp(&observed, &expected_initial, sizeof(observed))) {
			operation_end();
			goto fail;
		}
		operation_end();
	}
	owner_authority.control.installed = 1U;
	owner_authority.sealed_control.installed = 1U;
	return CB_SUCCESS;

fail:
	memset(&owner_authority.policy, 0, sizeof(owner_authority.policy));
	memset(&owner_authority.sealed_policy, 0,
		sizeof(owner_authority.sealed_policy));
	memset(owner_authority.identity, 0, sizeof(owner_authority.identity));
	memset(owner_authority.sealed_identity, 0,
		sizeof(owner_authority.sealed_identity));
	poison();
	return CB_ERR;
}

enum cb_err payload_mm_fmp_owner_install_checked(
	const struct payload_mm_fmp_owner_backend *trusted_backend,
	payload_mm_authvar_protected_storage storage_is_protected, void *context,
	uint32_t initial_key,
	const struct payload_mm_fmp_owner_record *initial_record)
{
	return owner_install(trusted_backend, storage_is_protected, context,
		initial_key, initial_record, true);
}

#if ENV_TEST
enum cb_err payload_mm_fmp_owner_install(
	const struct payload_mm_fmp_owner_backend *trusted_backend,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	return owner_install(trusted_backend, storage_is_protected, context, 0U,
		NULL, false);
}
#endif

bool payload_mm_fmp_owner_ready(void)
{
	if (!owner_authority.control.installed &&
	    !owner_authority.sealed_control.installed)
		return false;
	return authority_valid();
}

bool payload_mm_fmp_owner_storage_overlaps(const void *buffer, size_t size)
{
	return payload_mm_authvar_buffers_overlap(buffer, size, &owner_authority,
		sizeof(owner_authority));
}

bool payload_mm_fmp_owner_buffer_available(const void *buffer, size_t size)
{
	return payload_mm_fmp_state_staging_buffer(buffer, size) &&
		!payload_mm_fmp_owner_storage_overlaps(buffer, size);
}

#if ENV_TEST
const void *payload_mm_fmp_owner_test_storage(size_t *size)
{
	if (size)
		*size = sizeof(owner_authority);
	return &owner_authority;
}

void payload_mm_fmp_owner_test_corrupt_backend(bool sealed)
{
	struct owner_policy *policy = sealed ? &owner_authority.sealed_policy :
		&owner_authority.policy;

	policy->backend.revision ^= 1U;
}

void payload_mm_fmp_owner_test_corrupt_control(bool sealed)
{
	struct owner_control *control = sealed ? &owner_authority.sealed_control :
		&owner_authority.control;

	control->installed ^= 1U;
}

void payload_mm_fmp_owner_test_corrupt_identity(bool sealed)
{
	struct payload_mm_fmp_state_identity *identity = sealed ?
		owner_authority.sealed_identity : owner_authority.identity;

	identity[0].variable_name[0] ^= 1U;
}
#endif

enum cb_err payload_mm_fmp_owner_read(uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	struct payload_mm_fmp_owner_record snapshot;
	struct payload_mm_fmp_owner_record original;
	struct payload_mm_fmp_state_identity identity;
	struct payload_mm_fmp_state_identity expected_identity;
	enum cb_err result = CB_ERR;

	if (!key_valid(key) || !record ||
	    !owner_buffer(record, sizeof(*record)))
		return CB_ERR;
	original = *record;
	if (!operation_begin())
		return CB_ERR;
	if (!authority_valid())
		goto out;
	identity = owner_authority.sealed_identity[key];
	expected_identity = identity;
	memset(&snapshot, 0, sizeof(snapshot));
	if (owner_authority.policy.backend.read(
		owner_authority.policy.context, &identity,
		key, &snapshot) != CB_SUCCESS ||
	    memcmp(&identity, &expected_identity, sizeof(identity)) != 0 ||
	    !payload_mm_fmp_owner_observed_record_valid(key, &snapshot) ||
	    !authority_valid())
		goto out;
	*record = snapshot;
	result = CB_SUCCESS;
out:
	if (result != CB_SUCCESS)
		*record = original;
	operation_end();
	return result;
}

static enum cb_err commit(uint32_t key,
	const struct payload_mm_fmp_owner_record *current_record,
	const struct payload_mm_fmp_owner_record *candidate_record)
{
	struct payload_mm_fmp_owner_record current = *current_record;
	struct payload_mm_fmp_owner_record candidate = *candidate_record;
	const struct payload_mm_fmp_owner_record expected_current = current;
	const struct payload_mm_fmp_owner_record expected_candidate = candidate;
	struct payload_mm_fmp_owner_record verified;
	struct payload_mm_fmp_state_identity identity;
	struct payload_mm_fmp_state_identity expected_identity;
	bool inputs_unchanged;
	bool readback_valid;

	if (!payload_mm_fmp_owner_observed_record_valid(key, &current) ||
	    !payload_mm_fmp_owner_record_valid(key, &candidate) ||
	    current.sequence == UINT64_MAX ||
	    candidate.sequence != current.sequence + 1)
		return CB_ERR;
	if (!operation_begin())
		return CB_ERR;
	if (!authority_valid())
		goto fail;
	identity = owner_authority.sealed_identity[key];
	expected_identity = identity;
	(void)owner_authority.policy.backend.commit(
		owner_authority.policy.context,
		&identity, key, &current, &candidate);
	inputs_unchanged =
		memcmp(&identity, &expected_identity, sizeof(identity)) == 0 &&
		memcmp(&current, &expected_current, sizeof(current)) == 0 &&
		memcmp(&candidate, &expected_candidate, sizeof(candidate)) == 0;
	if (!authority_valid())
		goto fail;
	identity = expected_identity;
	memset(&verified, 0, sizeof(verified));
	readback_valid =
		owner_authority.policy.backend.read(
			owner_authority.policy.context,
			&identity, key, &verified) == CB_SUCCESS &&
		memcmp(&identity, &expected_identity, sizeof(identity)) == 0 &&
		payload_mm_fmp_owner_observed_record_valid(key, &verified);
	if (!authority_valid())
		readback_valid = false;
	operation_end();
	if (!inputs_unchanged || !readback_valid ||
	    memcmp(&verified, &expected_candidate, sizeof(verified)) != 0)
		return CB_ERR;
	return CB_SUCCESS;

fail:
	operation_end();
	return CB_ERR;
}

enum cb_err payload_mm_fmp_owner_commit_state(
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate)
{
	struct payload_mm_fmp_owner_record current_snapshot;
	struct payload_mm_fmp_owner_record candidate_snapshot;

	if (!current || !candidate || !owner_buffer(current, sizeof(*current)) ||
	    !owner_buffer(candidate, sizeof(*candidate)) ||
	    payload_mm_authvar_buffers_overlap(current, sizeof(*current), candidate,
		sizeof(*candidate)))
		return CB_ERR;
	current_snapshot = *current;
	candidate_snapshot = *candidate;
	if (!candidate_snapshot.present ||
	    !payload_mm_fmp_state_transition_valid(
		current_snapshot.present ? current_snapshot.data : NULL,
		candidate_snapshot.data))
		return CB_ERR;
	return commit(PAYLOAD_MM_FMP_STATE_KEY_STATE, &current_snapshot,
		&candidate_snapshot);
}

enum cb_err payload_mm_fmp_owner_remove_legacy(uint32_t key,
	const struct payload_mm_fmp_owner_record *current)
{
	struct payload_mm_fmp_owner_record current_snapshot;
	struct payload_mm_fmp_owner_record candidate;

	if (key < PAYLOAD_MM_FMP_STATE_KEY_VERSION ||
	    key > PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION || !current ||
	    !owner_buffer(current, sizeof(*current)))
		return CB_ERR;
	current_snapshot = *current;
	if (!payload_mm_fmp_owner_observed_record_valid(key, &current_snapshot) ||
	    !current_snapshot.present ||
	    current_snapshot.sequence == UINT64_MAX)
		return CB_ERR;
	candidate = (struct payload_mm_fmp_owner_record) {
		.sequence = current_snapshot.sequence + 1,
	};
	return commit(key, &current_snapshot, &candidate);
}
