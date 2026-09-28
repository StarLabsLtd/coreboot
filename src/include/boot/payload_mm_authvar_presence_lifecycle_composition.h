/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_COMPOSITION_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_COMPOSITION_H

#include <commonlib/bsd/cb_err.h>
#include <stdbool.h>

/* Each source owns its exact issuance entry; there is no source selector. */
enum cb_err payload_mm_authvar_presence_pre_external_image_register(void);
enum cb_err payload_mm_authvar_presence_payload_failure_or_return_register(void);
enum cb_err payload_mm_authvar_presence_warm_reset_register(void);
enum cb_err payload_mm_authvar_presence_s3_resume_register(void);

/*
 * This proves only that all four source registrations were installed and
 * consumed. It does not prove runtime closure or LIFECYCLE_SEALED.
 */
enum cb_err payload_mm_authvar_presence_installation_evidence_compose(void);
bool payload_mm_authvar_presence_installation_evidence_complete(void);

#if ENV_TEST
typedef void (*payload_mm_authvar_presence_composition_test_action_fn)(void);
void payload_mm_authvar_presence_composition_claimed_action_test(
	payload_mm_authvar_presence_composition_test_action_fn action);
void payload_mm_authvar_presence_installation_evidence_reset_test(void);
void payload_mm_authvar_presence_installation_evidence_corrupt_test(void);
void payload_mm_authvar_presence_installation_evidence_state_corrupt_test(void);
void payload_mm_authvar_presence_pre_external_image_corrupt_test(void);
void payload_mm_authvar_presence_payload_failure_or_return_corrupt_test(void);
void payload_mm_authvar_presence_warm_reset_corrupt_test(void);
void payload_mm_authvar_presence_s3_resume_corrupt_test(void);
#endif

#endif
