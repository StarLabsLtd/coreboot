/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_composition.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../src/lib/payload_mm_authvar_presence_lifecycle_registration.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static void register_all(void)
{
	assert(payload_mm_authvar_presence_pre_external_image_register() ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_payload_failure_or_return_register() ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_warm_reset_register() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_resume_register() == CB_SUCCESS);
}

static void complete(void)
{
	register_all();
	assert(payload_mm_authvar_presence_installation_evidence_compose() ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_installation_evidence_complete());
}

static void missing_source(void)
{
	for (unsigned int missing = 0; missing < 4; missing++) {
		payload_mm_authvar_presence_installation_evidence_reset_test();
		if (missing != 0)
			assert(payload_mm_authvar_presence_pre_external_image_register() ==
				CB_SUCCESS);
		if (missing != 1)
			assert(payload_mm_authvar_presence_payload_failure_or_return_register() ==
				CB_SUCCESS);
		if (missing != 2)
			assert(payload_mm_authvar_presence_warm_reset_register() ==
				CB_SUCCESS);
		if (missing != 3)
			assert(payload_mm_authvar_presence_s3_resume_register() ==
				CB_SUCCESS);
		assert(payload_mm_authvar_presence_installation_evidence_compose() ==
			CB_ERR);
		assert(!payload_mm_authvar_presence_installation_evidence_complete());
	}
}

static void corrupt_source(void)
{
	typedef void (*corrupt_fn)(void);
	static const corrupt_fn corrupt[] = {
		payload_mm_authvar_presence_pre_external_image_corrupt_test,
		payload_mm_authvar_presence_payload_failure_or_return_corrupt_test,
		payload_mm_authvar_presence_warm_reset_corrupt_test,
		payload_mm_authvar_presence_s3_resume_corrupt_test,
	};

	for (size_t source = 0; source < ARRAY_SIZE(corrupt); source++) {
		payload_mm_authvar_presence_installation_evidence_reset_test();
		register_all();
		corrupt[source]();
		assert(payload_mm_authvar_presence_installation_evidence_compose() ==
			CB_ERR);
		assert(!payload_mm_authvar_presence_installation_evidence_complete());
	}
	for (size_t source = 0; source < ARRAY_SIZE(corrupt); source++) {
		payload_mm_authvar_presence_installation_evidence_reset_test();
		complete();
		corrupt[source]();
		assert(!payload_mm_authvar_presence_installation_evidence_complete());
	}
}

static void duplicate_issue(void)
{
	payload_mm_authvar_presence_installation_evidence_reset_test();
	assert(payload_mm_authvar_presence_pre_external_image_register() ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_pre_external_image_register() == CB_ERR);
	assert(payload_mm_authvar_presence_payload_failure_or_return_register() ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_warm_reset_register() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_resume_register() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_installation_evidence_compose() == CB_ERR);
	assert(!payload_mm_authvar_presence_installation_evidence_complete());
}

static void corrupt_claimed_slot(
	struct payload_mm_authvar_presence_lifecycle_registration *slot)
{
	payload_mm_authvar_presence_lifecycle_registration_slot_corrupt_test(slot);
}

static void corrupt_claimed_slot_state(
	struct payload_mm_authvar_presence_lifecycle_registration *slot)
{
	payload_mm_authvar_presence_lifecycle_registration_state_corrupt_test(slot);
}

static void corrupt_claimed_composition(void)
{
	payload_mm_authvar_presence_installation_evidence_corrupt_test();
}

static void corrupt_claimed_composition_state(void)
{
	payload_mm_authvar_presence_installation_evidence_state_corrupt_test();
}

static void helper_transitions(void)
{
	struct payload_mm_authvar_presence_lifecycle_registration slot = { 0 };
	const uint32_t evidence = 0x12345678U;

	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(&slot);
	payload_mm_authvar_presence_lifecycle_registration_slot_corrupt_test(&slot);
	assert(payload_mm_authvar_presence_lifecycle_registration_issue(&slot,
		evidence) == CB_ERR);
	assert(!payload_mm_authvar_presence_lifecycle_registration_consumed(&slot));
	assert(!payload_mm_authvar_presence_registration_writes_test());

	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(&slot);
	payload_mm_authvar_presence_registration_claimed_action_test(
		corrupt_claimed_slot_state);
	assert(payload_mm_authvar_presence_lifecycle_registration_issue(&slot,
		evidence) == CB_ERR);
	assert(!payload_mm_authvar_presence_lifecycle_registration_consumed(&slot));
	assert(!payload_mm_authvar_presence_registration_writes_test());

	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(&slot);
	payload_mm_authvar_presence_registration_claimed_action_test(
		corrupt_claimed_slot);
	assert(payload_mm_authvar_presence_lifecycle_registration_issue(&slot,
		evidence) == CB_ERR);
	assert(!payload_mm_authvar_presence_lifecycle_registration_consumed(&slot));

	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(&slot);
	assert(payload_mm_authvar_presence_lifecycle_registration_issue(&slot,
		evidence) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_registration_seal(&slot,
		evidence) == CB_SUCCESS);
	payload_mm_authvar_presence_lifecycle_registration_slot_corrupt_test(&slot);
	assert(payload_mm_authvar_presence_lifecycle_registration_consume(&slot,
		evidence) == CB_ERR);
	assert(!payload_mm_authvar_presence_lifecycle_registration_consumed(&slot));

	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(&slot);
	assert(payload_mm_authvar_presence_lifecycle_registration_issue(&slot,
		evidence) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_registration_seal(&slot,
		evidence) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_registration_seal(&slot,
		evidence) == CB_ERR);
	assert(payload_mm_authvar_presence_lifecycle_registration_consume(&slot,
		evidence) == CB_ERR);

	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(&slot);
	assert(payload_mm_authvar_presence_lifecycle_registration_issue(&slot,
		evidence) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_registration_seal(&slot,
		evidence) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_registration_consume(&slot,
		evidence) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_registration_consumed(&slot));
	payload_mm_authvar_presence_lifecycle_registration_slot_corrupt_test(&slot);
	assert(!payload_mm_authvar_presence_lifecycle_registration_consumed(&slot));
}

static void *issue_pre_external(void *unused)
{
	(void)unused;
	return (void *)(uintptr_t)
		payload_mm_authvar_presence_pre_external_image_register();
}

static void *compose(void *unused)
{
	(void)unused;
	return (void *)(uintptr_t)
		payload_mm_authvar_presence_installation_evidence_compose();
}

static void concurrent_issue(void)
{
	pthread_t first;
	pthread_t second;

	payload_mm_authvar_presence_installation_evidence_reset_test();
	assert(!pthread_create(&first, NULL, issue_pre_external, NULL));
	assert(!pthread_create(&second, NULL, issue_pre_external, NULL));
	assert(!pthread_join(first, NULL));
	assert(!pthread_join(second, NULL));
	assert(payload_mm_authvar_presence_payload_failure_or_return_register() ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_warm_reset_register() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_s3_resume_register() == CB_SUCCESS);
	assert(payload_mm_authvar_presence_installation_evidence_compose() == CB_ERR);
	assert(!payload_mm_authvar_presence_installation_evidence_complete());
}

static void concurrent_compose(void)
{
	pthread_t first;
	pthread_t second;

	payload_mm_authvar_presence_installation_evidence_reset_test();
	register_all();
	assert(!pthread_create(&first, NULL, compose, NULL));
	assert(!pthread_create(&second, NULL, compose, NULL));
	assert(!pthread_join(first, NULL));
	assert(!pthread_join(second, NULL));
	assert(!payload_mm_authvar_presence_installation_evidence_complete());
}

int main(void)
{
	payload_mm_authvar_presence_installation_evidence_reset_test();
	assert(!payload_mm_authvar_presence_installation_evidence_complete());
	complete();
	payload_mm_authvar_presence_installation_evidence_corrupt_test();
	assert(!payload_mm_authvar_presence_installation_evidence_complete());
	payload_mm_authvar_presence_installation_evidence_reset_test();
	register_all();
	payload_mm_authvar_presence_installation_evidence_corrupt_test();
	assert(payload_mm_authvar_presence_installation_evidence_compose() == CB_ERR);
	assert(!payload_mm_authvar_presence_installation_evidence_complete());
	payload_mm_authvar_presence_installation_evidence_reset_test();
	register_all();
	payload_mm_authvar_presence_composition_claimed_action_test(
		corrupt_claimed_composition);
	assert(payload_mm_authvar_presence_installation_evidence_compose() == CB_ERR);
	assert(!payload_mm_authvar_presence_installation_evidence_complete());
	payload_mm_authvar_presence_installation_evidence_reset_test();
	register_all();
	payload_mm_authvar_presence_composition_claimed_action_test(
		corrupt_claimed_composition_state);
	assert(payload_mm_authvar_presence_installation_evidence_compose() == CB_ERR);
	assert(!payload_mm_authvar_presence_pre_external_image_consumed());
	assert(!payload_mm_authvar_presence_payload_failure_or_return_consumed());
	assert(!payload_mm_authvar_presence_warm_reset_consumed());
	assert(!payload_mm_authvar_presence_s3_resume_consumed());
	assert(!payload_mm_authvar_presence_installation_evidence_complete());
	missing_source();
	corrupt_source();
	duplicate_issue();
	helper_transitions();
	concurrent_issue();
	concurrent_compose();
	return 0;
}
