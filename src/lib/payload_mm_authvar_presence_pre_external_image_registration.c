/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_lifecycle_composition.h>
#include "payload_mm_authvar_presence_lifecycle_registration.h"

#define PRE_EXTERNAL_IMAGE_EVIDENCE 0x50455854U

static struct payload_mm_authvar_presence_lifecycle_registration
	pre_external_image_registration;

enum cb_err payload_mm_authvar_presence_pre_external_image_register(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_issue(
		&pre_external_image_registration, PRE_EXTERNAL_IMAGE_EVIDENCE);
}

enum cb_err payload_mm_authvar_presence_pre_external_image_seal(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_seal(
		&pre_external_image_registration, PRE_EXTERNAL_IMAGE_EVIDENCE);
}

enum cb_err payload_mm_authvar_presence_pre_external_image_consume(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_consume(
		&pre_external_image_registration, PRE_EXTERNAL_IMAGE_EVIDENCE);
}

bool payload_mm_authvar_presence_pre_external_image_consumed(void)
{
	return payload_mm_authvar_presence_lifecycle_registration_consumed(
		&pre_external_image_registration);
}

#if ENV_TEST
void payload_mm_authvar_presence_pre_external_image_corrupt_test(void)
{
	payload_mm_authvar_presence_lifecycle_registration_slot_corrupt_test(
		&pre_external_image_registration);
}

void payload_mm_authvar_presence_pre_external_image_reset_test(void)
{
	payload_mm_authvar_presence_lifecycle_registration_slot_reset_test(
		&pre_external_image_registration);
}
#endif
