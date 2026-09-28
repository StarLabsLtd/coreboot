/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_lifecycle_composition.h>
#include "payload_mm_authvar_presence_lifecycle_registration.h"

#define PAYLOAD_FAILURE_OR_RETURN_EVIDENCE 0x50464c52U

static struct payload_mm_authvar_presence_lifecycle_registration
	payload_failure_or_return_registration;

enum cb_err payload_mm_authvar_presence_payload_failure_or_return_register(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_issue(
		&payload_failure_or_return_registration,
		PAYLOAD_FAILURE_OR_RETURN_EVIDENCE);
}

enum cb_err payload_mm_authvar_presence_payload_failure_or_return_seal(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_seal(
		&payload_failure_or_return_registration,
		PAYLOAD_FAILURE_OR_RETURN_EVIDENCE);
}

enum cb_err payload_mm_authvar_presence_payload_failure_or_return_consume(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_consume(
		&payload_failure_or_return_registration,
		PAYLOAD_FAILURE_OR_RETURN_EVIDENCE);
}

bool payload_mm_authvar_presence_payload_failure_or_return_consumed(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_consumed(
		&payload_failure_or_return_registration);
}

#if ENV_TEST
void payload_mm_authvar_presence_payload_failure_or_return_corrupt_test(void)
{
	payload_mm_authvar_presence_lifecycle_registration_slot_corrupt_test(
		&payload_failure_or_return_registration);
}

void payload_mm_authvar_presence_payload_failure_or_return_reset_test(void)
{
	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(
		&payload_failure_or_return_registration);
}
#endif
