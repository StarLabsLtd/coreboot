/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_lifecycle_composition.h>
#include "payload_mm_authvar_presence_lifecycle_registration.h"

#define S3_RESUME_EVIDENCE 0x53335253U

static struct payload_mm_authvar_presence_lifecycle_registration
	s3_resume_registration;

enum cb_err payload_mm_authvar_presence_s3_resume_register(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_issue(
		&s3_resume_registration, S3_RESUME_EVIDENCE);
}

enum cb_err payload_mm_authvar_presence_s3_resume_seal(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_seal(
		&s3_resume_registration, S3_RESUME_EVIDENCE);
}

enum cb_err payload_mm_authvar_presence_s3_resume_consume(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_consume(
		&s3_resume_registration, S3_RESUME_EVIDENCE);
}

bool payload_mm_authvar_presence_s3_resume_consumed(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_consumed(
		&s3_resume_registration);
}

#if ENV_TEST
void payload_mm_authvar_presence_s3_resume_corrupt_test(void)
{
	payload_mm_authvar_presence_lifecycle_registration_slot_corrupt_test(
		&s3_resume_registration);
}

void payload_mm_authvar_presence_s3_resume_reset_test(void)
{
	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(
		&s3_resume_registration);
}
#endif
