/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_authvar_presence_lifecycle_registration.h"
#include <string.h>

enum registration_state {
	REGISTRATION_EMPTY,
	REGISTRATION_ISSUING,
	REGISTRATION_ISSUED,
	REGISTRATION_SEALING,
	REGISTRATION_SEALED,
	REGISTRATION_CONSUMING,
	REGISTRATION_CONSUMED,
	REGISTRATION_FAILED,
};

#if ENV_TEST
static payload_mm_authvar_presence_registration_test_action_fn claimed_action;
static uint32_t evidence_writes;
#endif

struct registration_snapshot {
	uint32_t evidence;
	uint32_t evidence_inverse;
	uint32_t sealed_evidence;
	uint32_t sealed_evidence_inverse;
};

static struct registration_snapshot snapshot(
	const struct payload_mm_authvar_presence_lifecycle_registration *slot)
{
	const struct registration_snapshot value = {
		.evidence = __atomic_load_n(&slot->evidence, __ATOMIC_ACQUIRE),
		.evidence_inverse = __atomic_load_n(&slot->evidence_inverse,
			__ATOMIC_ACQUIRE),
		.sealed_evidence = __atomic_load_n(&slot->sealed_evidence,
			__ATOMIC_ACQUIRE),
		.sealed_evidence_inverse = __atomic_load_n(
			&slot->sealed_evidence_inverse, __ATOMIC_ACQUIRE),
	};

	return value;
}

static bool snapshot_zero(const struct registration_snapshot *value)
{
	return !value->evidence && !value->evidence_inverse &&
		!value->sealed_evidence && !value->sealed_evidence_inverse;
}

static void scrub(
	struct payload_mm_authvar_presence_lifecycle_registration *slot)
{
	__atomic_store_n(&slot->evidence, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&slot->evidence_inverse, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&slot->sealed_evidence, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&slot->sealed_evidence_inverse, 0, __ATOMIC_RELEASE);
	__asm__ __volatile__("" : : "r" (slot) : "memory");
}

static void poison(
	struct payload_mm_authvar_presence_lifecycle_registration *slot)
{
	__atomic_store_n(&slot->state, REGISTRATION_FAILED, __ATOMIC_RELEASE);
}

static bool evidence_valid(
	const struct payload_mm_authvar_presence_lifecycle_registration *slot,
	uint32_t evidence)
{
	const struct registration_snapshot value = snapshot(slot);

	return value.evidence == evidence && value.evidence_inverse == ~evidence &&
		value.sealed_evidence == value.evidence &&
		value.sealed_evidence_inverse == value.evidence_inverse;
}

enum cb_err payload_mm_authvar_presence_lifecycle_registration_issue(
	struct payload_mm_authvar_presence_lifecycle_registration *slot,
	uint32_t evidence)
{
	uint32_t expected = REGISTRATION_EMPTY;
	struct registration_snapshot before;
	struct registration_snapshot after;

	if (!slot || !evidence || evidence == UINT32_MAX)
		return CB_ERR;
	if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		REGISTRATION_EMPTY) {
		poison(slot);
		return CB_ERR;
	}
	before = snapshot(slot);
	if (!snapshot_zero(&before)) {
		poison(slot);
		return CB_ERR;
	}
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		REGISTRATION_ISSUING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		poison(slot);
		return CB_ERR;
	}
#if ENV_TEST
	if (claimed_action)
		claimed_action(slot);
#endif
	after = snapshot(slot);
	if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		REGISTRATION_ISSUING || memcmp(&before, &after, sizeof(before)) ||
	    !snapshot_zero(&after)) {
		poison(slot);
		return CB_ERR;
	}
#if ENV_TEST
	evidence_writes++;
#endif
	__atomic_store_n(&slot->evidence, evidence, __ATOMIC_RELEASE);
	__atomic_store_n(&slot->evidence_inverse, ~evidence, __ATOMIC_RELEASE);
	__atomic_store_n(&slot->sealed_evidence, evidence, __ATOMIC_RELEASE);
	__atomic_store_n(&slot->sealed_evidence_inverse, ~evidence,
		__ATOMIC_RELEASE);
	expected = REGISTRATION_ISSUING;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		REGISTRATION_ISSUED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		scrub(slot);
		poison(slot);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_lifecycle_registration_seal(
	struct payload_mm_authvar_presence_lifecycle_registration *slot,
	uint32_t evidence)
{
	uint32_t expected = REGISTRATION_ISSUED;

	if (!slot || !__atomic_compare_exchange_n(&slot->state, &expected,
		REGISTRATION_SEALING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		if (slot)
			poison(slot);
		return CB_ERR;
	}
	if (!evidence_valid(slot, evidence)) {
		poison(slot);
		return CB_ERR;
	}
	expected = REGISTRATION_SEALING;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		REGISTRATION_SEALED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		poison(slot);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_lifecycle_registration_consume(
	struct payload_mm_authvar_presence_lifecycle_registration *slot,
	uint32_t evidence)
{
	uint32_t expected = REGISTRATION_SEALED;

	if (!slot || !__atomic_compare_exchange_n(&slot->state, &expected,
		REGISTRATION_CONSUMING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		if (slot)
			poison(slot);
		return CB_ERR;
	}
	if (!evidence_valid(slot, evidence)) {
		poison(slot);
		return CB_ERR;
	}
	scrub(slot);
	expected = REGISTRATION_CONSUMING;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		REGISTRATION_CONSUMED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		poison(slot);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

bool payload_mm_authvar_presence_lifecycle_registration_consumed(
	const struct payload_mm_authvar_presence_lifecycle_registration *slot)
{
	struct registration_snapshot value;

	if (!slot || __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		REGISTRATION_CONSUMED)
		return false;
	value = snapshot(slot);
	return slot && __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) ==
		REGISTRATION_CONSUMED && snapshot_zero(&value);
}

#if ENV_TEST
void payload_mm_authvar_presence_registration_claimed_action_test(
	payload_mm_authvar_presence_registration_test_action_fn action)
{
	claimed_action = action;
}

void payload_mm_authvar_presence_lifecycle_registration_slot_corrupt_test(
	struct payload_mm_authvar_presence_lifecycle_registration *slot)
{
	__atomic_fetch_xor(&slot->sealed_evidence, 1U, __ATOMIC_ACQ_REL);
}

void payload_mm_authvar_presence_lifecycle_registration_state_corrupt_test(
	struct payload_mm_authvar_presence_lifecycle_registration *slot)
{
	poison(slot);
}

uint32_t payload_mm_authvar_presence_registration_writes_test(void)
{
	return evidence_writes;
}

void payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(
	struct payload_mm_authvar_presence_lifecycle_registration *slot)
{
	memset(slot, 0, sizeof(*slot));
	claimed_action = NULL;
	evidence_writes = 0;
}
#endif
