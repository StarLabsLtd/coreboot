/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_lifecycle_composition.h>
#include "payload_mm_authvar_presence_lifecycle_registration.h"

#define WARM_RESET_EVIDENCE 0x57525354U

static struct payload_mm_authvar_presence_lifecycle_registration
	warm_reset_registration;

enum cb_err payload_mm_authvar_presence_warm_reset_register(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_issue(
		&warm_reset_registration, WARM_RESET_EVIDENCE);
}

enum cb_err payload_mm_authvar_presence_warm_reset_seal(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_seal(
		&warm_reset_registration, WARM_RESET_EVIDENCE);
}

enum cb_err payload_mm_authvar_presence_warm_reset_consume(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_consume(
		&warm_reset_registration, WARM_RESET_EVIDENCE);
}

bool payload_mm_authvar_presence_warm_reset_consumed(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_consumed(
		&warm_reset_registration);
}

#if ENV_TEST
void payload_mm_authvar_presence_warm_reset_corrupt_test(void)
{
	payload_mm_authvar_presence_lifecycle_registration_slot_corrupt_test(
		&warm_reset_registration);
}

void payload_mm_authvar_presence_warm_reset_reset_test(void)
{
	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(
		&warm_reset_registration);
}
#endif
