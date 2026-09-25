/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include <boot/payload_mm_authvar_service.h>

#include "payload_mm_authvar_fmp_internal.h"
#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_owner_authvar_boot_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authvar FMP boot owner must only be built in SMM"
#endif

#define BOOT_IDENTITY_COUNT \
	(PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION + 1U)

enum boot_phase {
	BOOT_PHASE_NONE,
	BOOT_PHASE_IDENTITIES_READY,
	BOOT_PHASE_PROVING,
	BOOT_PHASE_ACTIVE,
	BOOT_PHASE_POISONED,
};

struct boot_control {
	uint32_t phase;
	uint32_t busy;
	uint32_t proof_allowed;
	uint32_t reserved;
	uint64_t epoch;
};

struct boot_backend_context {
	void *authority;
	void *sealed_authority;
	uint64_t generation;
	uint64_t sealed_generation;
};

struct boot_identity_table {
	struct payload_mm_fmp_state_identity identity[BOOT_IDENTITY_COUNT];
};

struct boot_authority {
	struct boot_control control;
	struct boot_control sealed_control;
	struct payload_mm_authvar_contract contract;
	struct payload_mm_authvar_contract sealed_contract;
	struct boot_identity_table identities;
	struct boot_identity_table sealed_identities;
	struct payload_mm_fmp_owner_record current;
	struct payload_mm_fmp_owner_record sealed_current;
	struct payload_mm_fmp_owner_record pending;
	struct payload_mm_fmp_owner_record sealed_pending;
};

static struct boot_authority boot_authority;

static bool bytes_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	uint8_t bits = 0U;

	for (size_t i = 0; i < size; i++)
		bits |= bytes[i];
	return bits == 0U;
}

static bool phase_valid(uint32_t phase)
{
	return phase >= BOOT_PHASE_IDENTITIES_READY &&
		phase <= BOOT_PHASE_ACTIVE;
}

static bool authority_valid(void)
{
	bool initialized;

	if (memcmp(&boot_authority.control, &boot_authority.sealed_control,
			sizeof(boot_authority.control)) ||
	    !phase_valid(boot_authority.control.phase) ||
	    boot_authority.control.busy > 1U ||
	    boot_authority.control.proof_allowed > 1U ||
	    boot_authority.control.reserved)
		return false;
	initialized = boot_authority.control.phase == BOOT_PHASE_PROVING ||
		boot_authority.control.phase == BOOT_PHASE_ACTIVE;
	return (!initialized ||
		(boot_authority.control.epoch &&
		 boot_authority.contract.generation &&
		 boot_authority.contract.generation != UINT64_MAX)) &&
		!memcmp(&boot_authority.contract, &boot_authority.sealed_contract,
			sizeof(boot_authority.contract)) &&
		!memcmp(&boot_authority.identities,
			&boot_authority.sealed_identities,
			sizeof(boot_authority.identities)) &&
		!memcmp(&boot_authority.current, &boot_authority.sealed_current,
			sizeof(boot_authority.current)) &&
		!memcmp(&boot_authority.pending, &boot_authority.sealed_pending,
			sizeof(boot_authority.pending));
}

static enum cb_err poison(void)
{
	boot_authority.control.phase = BOOT_PHASE_POISONED;
	boot_authority.sealed_control.phase = BOOT_PHASE_POISONED;
	boot_authority.control.busy = 0U;
	boot_authority.sealed_control.busy = 0U;
	payload_mm_authvar_fmp_state_activation_abort();
	return CB_ERR;
}

static enum cb_err poison_activation(void)
{
	return poison();
}

static void set_busy(bool busy)
{
	boot_authority.control.busy = busy;
	boot_authority.sealed_control.busy = busy;
}

static void set_current(const struct payload_mm_fmp_owner_record *current)
{
	boot_authority.current = *current;
	boot_authority.sealed_current = *current;
}

static void set_pending(const struct payload_mm_fmp_owner_record *pending)
{
	if (pending) {
		boot_authority.pending = *pending;
		boot_authority.sealed_pending = *pending;
	} else {
		memset(&boot_authority.pending, 0,
			sizeof(boot_authority.pending));
		memset(&boot_authority.sealed_pending, 0,
			sizeof(boot_authority.sealed_pending));
	}
}

static bool context_valid(const struct boot_backend_context *context)
{
	return context && context->authority == &boot_authority &&
		context->sealed_authority == &boot_authority &&
		context->generation && context->generation != UINT64_MAX &&
		context->generation == context->sealed_generation &&
		context->generation == boot_authority.contract.generation &&
		authority_valid() &&
		boot_authority.control.phase != BOOT_PHASE_POISONED;
}

static bool identity_equal(uint32_t key,
	const struct payload_mm_fmp_state_identity *identity)
{
	return key < BOOT_IDENTITY_COUNT && identity &&
		!memcmp(identity, &boot_authority.sealed_identities.identity[key],
			sizeof(*identity));
}

static bool record_equal_except_sequence(
	const struct payload_mm_fmp_owner_record *left,
	const struct payload_mm_fmp_owner_record *right)
{
	return !memcmp((const uint8_t *)left + sizeof(left->sequence),
		(const uint8_t *)right + sizeof(right->sequence),
		sizeof(*left) - sizeof(left->sequence));
}

static bool authority_snapshot_unchanged(
	struct payload_mm_authvar_contract *contract)
{
	struct boot_authority snapshot = boot_authority;

	return payload_mm_authvar_authority_snapshot(contract) &&
		!memcmp(&snapshot, &boot_authority, sizeof(boot_authority));
}

static bool contract_unchanged(void)
{
	struct payload_mm_authvar_contract observed;

	return authority_snapshot_unchanged(&observed) &&
		!memcmp(&observed, &boot_authority.sealed_contract,
			sizeof(observed));
}

static bool boot_storage_protected(
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct boot_authority snapshot = boot_authority;

	return storage_is_protected &&
		storage_is_protected(context, &boot_authority,
			sizeof(boot_authority)) &&
		!memcmp(&snapshot, &boot_authority, sizeof(boot_authority));
}

static bool identities_install_unchanged(
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct boot_authority snapshot = boot_authority;

	return payload_mm_fmp_owner_authvar_identity_install(storage_is_protected,
		context) == CB_SUCCESS &&
		!memcmp(&snapshot, &boot_authority, sizeof(boot_authority));
}

static bool identity_get_unchanged(uint32_t key,
	struct payload_mm_fmp_state_identity *identity)
{
	struct boot_authority snapshot = boot_authority;

	return payload_mm_fmp_owner_authvar_identity(key, identity) == CB_SUCCESS &&
		!memcmp(&snapshot, &boot_authority, sizeof(boot_authority));
}

static uint64_t read_combined(struct payload_mm_fmp_owner_record *record)
{
	if (boot_authority.control.phase == BOOT_PHASE_PROVING &&
	    boot_authority.control.proof_allowed)
		return payload_mm_authvar_fmp_state_activation_read(record);
	if (boot_authority.control.phase == BOOT_PHASE_ACTIVE)
		return payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, record);
	return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
}

static uint64_t read_combined_unchanged(
	struct payload_mm_fmp_owner_record *record, bool *unchanged)
{
	struct boot_authority snapshot = boot_authority;
	uint64_t status = read_combined(record);

	*unchanged = !memcmp(&snapshot, &boot_authority,
		sizeof(boot_authority));
	return status;
}

static uint64_t transaction_unchanged(
	enum payload_mm_authvar_fmp_operation operation,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate,
	struct payload_mm_fmp_owner_record *observation, bool *unchanged)
{
	struct boot_authority snapshot = boot_authority;
	uint64_t status = payload_mm_authvar_fmp_state_transaction(operation,
		current, candidate, observation);

	*unchanged = !memcmp(&snapshot, &boot_authority,
		sizeof(boot_authority));
	return status;
}

static uint64_t initialize_unchanged(
	struct payload_mm_fmp_owner_record *observation, bool *unchanged)
{
	struct boot_authority snapshot = boot_authority;
	uint64_t status = payload_mm_authvar_fmp_state_initialize(observation);

	*unchanged = !memcmp(&snapshot, &boot_authority,
		sizeof(boot_authority));
	return status;
}

static bool reconciliation_retryable_unchanged(void)
{
	struct boot_authority snapshot = boot_authority;
	bool retryable =
		payload_mm_authvar_fmp_state_reconciliation_retryable();

	return retryable && !memcmp(&snapshot, &boot_authority,
		sizeof(boot_authority));
}

static enum cb_err activate_unchanged(void)
{
	struct boot_authority snapshot = boot_authority;
	enum cb_err result = payload_mm_authvar_fmp_state_activate();

	return result == CB_SUCCESS &&
		!memcmp(&snapshot, &boot_authority, sizeof(boot_authority)) ?
		CB_SUCCESS : CB_ERR;
}

static bool owner_install_expected(
	const struct payload_mm_fmp_owner_backend *backend,
	const struct payload_mm_fmp_owner_record *seeded,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct boot_authority expected = boot_authority;

	expected.control.proof_allowed = 0U;
	expected.sealed_control.proof_allowed = 0U;
	return payload_mm_fmp_owner_install_checked(backend, storage_is_protected,
		context, PAYLOAD_MM_FMP_STATE_KEY_STATE, seeded) == CB_SUCCESS &&
		!memcmp(&expected, &boot_authority, sizeof(boot_authority));
}

static enum cb_err authvar_read(const void *trusted_context,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	const struct boot_backend_context *context = trusted_context;
	struct payload_mm_fmp_owner_record observed;
	bool unchanged;

	if (!context_valid(context) || boot_authority.control.busy || !record ||
	    !identity_equal(key, identity))
		return poison();
	if (boot_authority.control.phase == BOOT_PHASE_PROVING &&
	    !boot_authority.control.proof_allowed)
		return poison();
	set_busy(true);
	memset(&observed, 0, sizeof(observed));
	if (key == PAYLOAD_MM_FMP_STATE_KEY_STATE) {
		if (read_combined_unchanged(&observed, &unchanged) !=
		    PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			if (!unchanged || !contract_unchanged())
				return poison();
			set_busy(false);
			return CB_ERR;
		}
		if (!unchanged || !contract_unchanged())
			return poison();
		if (boot_authority.control.phase == BOOT_PHASE_PROVING) {
			boot_authority.control.proof_allowed = 0U;
			boot_authority.sealed_control.proof_allowed = 0U;
		}
	} else if (key >= BOOT_IDENTITY_COUNT) {
		return poison();
	}
	if (!authority_valid() || !contract_unchanged())
		return poison();
	observed.sequence = boot_authority.control.epoch;
	if (!payload_mm_fmp_owner_observed_record_valid(key, &observed))
		return poison();
	if (key == PAYLOAD_MM_FMP_STATE_KEY_STATE &&
	    !record_equal_except_sequence(&observed,
		&boot_authority.sealed_current))
		return poison();
	set_busy(false);
	*record = observed;
	return CB_SUCCESS;
}

static enum cb_err authvar_commit(const void *trusted_context,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate)
{
	const struct boot_backend_context *context = trusted_context;
	struct payload_mm_fmp_owner_record current_snapshot;
	struct payload_mm_fmp_owner_record candidate_snapshot;
	struct payload_mm_fmp_owner_record expected_current;
	struct payload_mm_fmp_owner_record expected_candidate;
	struct payload_mm_fmp_owner_record observed = { 0 };
	uint64_t status;
	bool identical;
	bool unchanged;

	if (!context_valid(context) || boot_authority.control.busy || !current ||
	    !candidate || boot_authority.control.phase != BOOT_PHASE_ACTIVE ||
	    key != PAYLOAD_MM_FMP_STATE_KEY_STATE || !identity_equal(key, identity) ||
	    !payload_mm_fmp_owner_observed_record_valid(key, current) ||
	    current->sequence != boot_authority.control.epoch ||
	    !record_equal_except_sequence(current,
		&boot_authority.sealed_current) ||
	    !bytes_zero(&boot_authority.sealed_pending,
		sizeof(boot_authority.sealed_pending)))
		return poison();
	if (current->sequence == UINT64_MAX)
		return CB_ERR;
	if (!payload_mm_fmp_owner_record_valid(key, candidate) ||
	    candidate->sequence != current->sequence + 1U)
		return poison();
	expected_current = *current;
	expected_candidate = *candidate;
	current_snapshot = expected_current;
	candidate_snapshot = expected_candidate;
	identical = record_equal_except_sequence(&current_snapshot,
		&candidate_snapshot);
	set_pending(&candidate_snapshot);
	set_busy(true);
	status = transaction_unchanged(
		PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current_snapshot,
		&candidate_snapshot, &observed, &unchanged);
	if (!unchanged || !authority_valid() || !contract_unchanged() ||
	    memcmp(&current_snapshot, &expected_current,
		sizeof(current_snapshot)) ||
	    memcmp(&candidate_snapshot, &expected_candidate,
		sizeof(candidate_snapshot)) ||
	    memcmp(current, &expected_current, sizeof(expected_current)) ||
	    memcmp(candidate, &expected_candidate, sizeof(expected_candidate)))
		return poison();
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		if (transaction_unchanged(PAYLOAD_MM_AUTHVAR_FMP_READ_STATE,
			NULL, NULL, &observed, &unchanged) !=
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS || !unchanged ||
		    !authority_valid() ||
		    !contract_unchanged())
			return poison();
	}
	observed.sequence = candidate->sequence;
	if (!payload_mm_fmp_owner_observed_record_valid(key, &observed))
		return poison();
	if (record_equal_except_sequence(&observed, candidate) &&
	    (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS || !identical)) {
		boot_authority.control.epoch = candidate->sequence;
		boot_authority.sealed_control.epoch = candidate->sequence;
		set_current(candidate);
		set_pending(NULL);
		set_busy(false);
		return CB_SUCCESS;
	}
	observed.sequence = current->sequence;
	if (record_equal_except_sequence(&observed, current)) {
		set_pending(NULL);
		set_busy(false);
		return CB_ERR;
	}
	return poison();
}

enum cb_err payload_mm_fmp_owner_authvar_boot_install(
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct payload_mm_authvar_contract before;
	struct payload_mm_authvar_contract after;
	struct payload_mm_fmp_owner_record reconciled;
	struct payload_mm_fmp_owner_record seeded;
	struct payload_mm_fmp_state_identity identity;
	struct boot_backend_context backend_context;
	bool unchanged;
	struct payload_mm_fmp_owner_backend backend = {
		.revision = PAYLOAD_MM_FMP_OWNER_REVISION,
		.size = sizeof(backend),
		.read = authvar_read,
		.commit = authvar_commit,
	};

	if (!bytes_zero(&boot_authority, sizeof(boot_authority))) {
		if (!authority_valid())
			return poison();
		if (boot_authority.control.phase != BOOT_PHASE_IDENTITIES_READY ||
		    boot_authority.control.busy)
			return CB_ERR;
	} else {
		set_busy(true);
		if (!boot_storage_protected(storage_is_protected, context) ||
		    !identities_install_unchanged(storage_is_protected, context))
			return poison();
		for (uint32_t key = 0; key < BOOT_IDENTITY_COUNT; key++) {
			if (!identity_get_unchanged(key, &identity))
				return poison();
			boot_authority.identities.identity[key] = identity;
		}
		boot_authority.sealed_identities = boot_authority.identities;
		boot_authority.control.phase = BOOT_PHASE_IDENTITIES_READY;
		boot_authority.sealed_control.phase =
			BOOT_PHASE_IDENTITIES_READY;
		set_busy(false);
		if (!authority_valid())
			return poison();
	}
	if (!authority_valid() || boot_authority.control.busy)
		return poison();
	set_busy(true);
	if (!authority_valid() ||
	    !boot_storage_protected(storage_is_protected, context) ||
	    !authority_valid() ||
	    !authority_snapshot_unchanged(&before) ||
	    !before.generation || before.generation == UINT64_MAX)
		return poison();
	memset(&reconciled, 0, sizeof(reconciled));
	if (initialize_unchanged(&reconciled, &unchanged) !=
	    PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		if (!unchanged || !authority_valid() ||
		    !reconciliation_retryable_unchanged() || !authority_valid())
			return poison_activation();
		set_busy(false);
		return CB_ERR;
	}
	if (!unchanged || !authority_snapshot_unchanged(&after) ||
	    memcmp(&before, &after, sizeof(before)) || !authority_valid())
		return poison_activation();
	seeded = reconciled;
	seeded.sequence = before.generation;
	if (!seeded.present ||
	    !payload_mm_fmp_owner_observed_record_valid(
		PAYLOAD_MM_FMP_STATE_KEY_STATE, &seeded))
		return poison_activation();
	boot_authority.contract = before;
	boot_authority.sealed_contract = before;
	boot_authority.control.epoch = before.generation;
	boot_authority.sealed_control.epoch = before.generation;
	set_current(&seeded);
	set_pending(NULL);
	boot_authority.control.phase = BOOT_PHASE_PROVING;
	boot_authority.sealed_control.phase = BOOT_PHASE_PROVING;
	set_busy(false);
	boot_authority.control.proof_allowed = 1U;
	boot_authority.sealed_control.proof_allowed = 1U;
	backend_context = (struct boot_backend_context) {
		.authority = &boot_authority,
		.sealed_authority = &boot_authority,
		.generation = before.generation,
		.sealed_generation = before.generation,
	};
	backend.context = &backend_context;
	backend.context_size = sizeof(backend_context);
	if (!owner_install_expected(&backend, &seeded, storage_is_protected,
		context))
		return poison_activation();
	if (!authority_valid() ||
	    boot_authority.control.proof_allowed ||
	    boot_authority.sealed_control.proof_allowed ||
	    !contract_unchanged())
		return poison_activation();
	if (activate_unchanged() != CB_SUCCESS ||
	    !authority_valid() || !contract_unchanged())
		return poison_activation();
	boot_authority.control.phase = BOOT_PHASE_ACTIVE;
	boot_authority.sealed_control.phase = BOOT_PHASE_ACTIVE;
	return CB_SUCCESS;
}

#if ENV_TEST
void payload_mm_fmp_owner_authvar_boot_test_corrupt_control(bool sealed)
{
	struct boot_control *control = sealed ? &boot_authority.sealed_control :
		&boot_authority.control;

	control->reserved ^= 1U;
}

void payload_mm_fmp_owner_authvar_boot_test_corrupt_phase(bool sealed)
{
	struct boot_control *control = sealed ? &boot_authority.sealed_control :
		&boot_authority.control;

	control->phase ^= BOOT_PHASE_ACTIVE;
}

void payload_mm_fmp_owner_authvar_boot_test_corrupt_proof(bool sealed)
{
	struct boot_control *control = sealed ? &boot_authority.sealed_control :
		&boot_authority.control;

	control->proof_allowed ^= 1U;
}

void payload_mm_fmp_owner_authvar_boot_test_corrupt_contract(bool sealed)
{
	struct payload_mm_authvar_contract *target = sealed ?
		&boot_authority.sealed_contract : &boot_authority.contract;

	target->generation ^= 1U;
}

void payload_mm_fmp_owner_authvar_boot_test_corrupt_identity(bool sealed)
{
	struct boot_identity_table *target = sealed ?
		&boot_authority.sealed_identities : &boot_authority.identities;

	target->identity[0].variable_name[0] ^= 1U;
}

void payload_mm_fmp_owner_authvar_boot_test_corrupt_current(bool sealed)
{
	struct payload_mm_fmp_owner_record *target = sealed ?
		&boot_authority.sealed_current : &boot_authority.current;

	target->data[0] ^= 1U;
}

void payload_mm_fmp_owner_authvar_boot_test_corrupt_pending(bool sealed)
{
	struct payload_mm_fmp_owner_record *target = sealed ?
		&boot_authority.sealed_pending : &boot_authority.pending;

	target->reserved ^= 1U;
}
#endif
