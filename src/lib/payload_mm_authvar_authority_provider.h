/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER_H
#define PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER_H

#include <boot/payload_mm_authvar_authority.h>

/* Concrete verifier for the coordinator's protected authority callback. */
enum payload_mm_verify_status payload_mm_authvar_authority_provider_verify(
	void *context,
	const struct payload_mm_authvar_authority_verify_request *request,
	struct payload_mm_authvar_authority_verification *verification);

#endif
