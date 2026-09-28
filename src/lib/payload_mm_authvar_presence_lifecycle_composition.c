/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_lifecycle_composition.h>
#include <stddef.h>
#include "payload_mm_authvar_presence_lifecycle_registration.h"

enum composition_state {
	COMPOSITION_EMPTY,
	COMPOSITION_BUSY,
	COMPOSITION_READY,
	COMPOSITION_FAILED,
};

static uint32_t composition_state;
static uint32_t composition_state_inverse = ~(uint32_t)COMPOSITION_EMPTY;
#if ENV_TEST
static payload_mm_authvar_presence_composition_test_action_fn claimed_action;
#endif

static void fail(void)
{
	__atomic_store_n(&composition_state_inverse,
		~(uint32_t)COMPOSITION_FAILED,
		__ATOMIC_RELEASE);
	__atomic_store_n(&composition_state, COMPOSITION_FAILED, __ATOMIC_RELEASE);
}

enum cb_err payload_mm_authvar_presence_installation_evidence_compose(void)
{
	uint32_t expected = COMPOSITION_EMPTY;
	uint32_t inverse;

	if (__atomic_load_n(&composition_state, __ATOMIC_ACQUIRE) !=
		COMPOSITION_EMPTY) {
		fail();
		return CB_ERR;
	}
	inverse = __atomic_load_n(&composition_state_inverse, __ATOMIC_ACQUIRE);
	if (inverse != ~(uint32_t)COMPOSITION_EMPTY) {
		fail();
		return CB_ERR;
	}
	if (!__atomic_compare_exchange_n(&composition_state, &expected,
		COMPOSITION_BUSY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		fail();
		return CB_ERR;
	}
#if ENV_TEST
	if (claimed_action)
		claimed_action();
#endif
	if (__atomic_load_n(&composition_state, __ATOMIC_ACQUIRE) !=
		COMPOSITION_BUSY ||
	    __atomic_load_n(&composition_state_inverse, __ATOMIC_ACQUIRE) !=
		inverse) {
		fail();
		return CB_ERR;
	}
	__atomic_store_n(&composition_state_inverse, ~(uint32_t)COMPOSITION_BUSY,
		__ATOMIC_RELEASE);
	if (payload_mm_authvar_presence_pre_external_image_seal() != CB_SUCCESS ||
	    payload_mm_authvar_presence_payload_failure_or_return_seal() != CB_SUCCESS ||
	    payload_mm_authvar_presence_warm_reset_seal() != CB_SUCCESS ||
	    payload_mm_authvar_presence_s3_resume_seal() != CB_SUCCESS ||
	    payload_mm_authvar_presence_pre_external_image_consume() != CB_SUCCESS ||
	    payload_mm_authvar_presence_payload_failure_or_return_consume() != CB_SUCCESS ||
	    payload_mm_authvar_presence_warm_reset_consume() != CB_SUCCESS ||
	    payload_mm_authvar_presence_s3_resume_consume() != CB_SUCCESS) {
		fail();
		return CB_ERR;
	}
	expected = COMPOSITION_BUSY;
	__atomic_store_n(&composition_state_inverse, ~(uint32_t)COMPOSITION_READY,
		__ATOMIC_RELEASE);
	if (!__atomic_compare_exchange_n(&composition_state, &expected,
		COMPOSITION_READY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		fail();
		return CB_ERR;
	}
	return CB_SUCCESS;
}

bool payload_mm_authvar_presence_installation_evidence_complete(void)
{
	return __atomic_load_n(&composition_state, __ATOMIC_ACQUIRE) ==
		COMPOSITION_READY &&
		__atomic_load_n(&composition_state_inverse, __ATOMIC_ACQUIRE) ==
		~(uint32_t)COMPOSITION_READY &&
		payload_mm_authvar_presence_pre_external_image_consumed() &&
		payload_mm_authvar_presence_payload_failure_or_return_consumed() &&
		payload_mm_authvar_presence_warm_reset_consumed() &&
		payload_mm_authvar_presence_s3_resume_consumed();
}

#if ENV_TEST
void payload_mm_authvar_presence_composition_claimed_action_test(
	payload_mm_authvar_presence_composition_test_action_fn action)
{
	claimed_action = action;
}

void payload_mm_authvar_presence_installation_evidence_reset_test(void)
{
	payload_mm_authvar_presence_pre_external_image_reset_test();
	payload_mm_authvar_presence_payload_failure_or_return_reset_test();
	payload_mm_authvar_presence_warm_reset_reset_test();
	payload_mm_authvar_presence_s3_resume_reset_test();
	__atomic_store_n(&composition_state, COMPOSITION_EMPTY, __ATOMIC_RELEASE);
	__atomic_store_n(&composition_state_inverse, ~(uint32_t)COMPOSITION_EMPTY,
		__ATOMIC_RELEASE);
	claimed_action = NULL;
}

void payload_mm_authvar_presence_installation_evidence_corrupt_test(void)
{
	__atomic_fetch_xor(&composition_state_inverse, 1U, __ATOMIC_ACQ_REL);
}

void payload_mm_authvar_presence_installation_evidence_state_corrupt_test(void)
{
	__atomic_store_n(&composition_state, COMPOSITION_FAILED, __ATOMIC_RELEASE);
}
#endif
