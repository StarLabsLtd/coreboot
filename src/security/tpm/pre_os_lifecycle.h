/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_PRE_OS_LIFECYCLE_H
#define SECURITY_TPM_PRE_OS_LIFECYCLE_H

#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TPM_PRE_OS_BACKEND_CONTEXT_SIZE 64U
#define TPM_PRE_OS_LIFECYCLE_REVISION 1U

enum tpm_pre_os_state {
	TPM_PRE_OS_UNBOUND,
	TPM_PRE_OS_AVAILABLE,
	TPM_PRE_OS_OWNED,
	TPM_PRE_OS_BUSY,
	TPM_PRE_OS_HANDING_OFF,
	TPM_PRE_OS_HANDED_OFF,
	TPM_PRE_OS_FAILED,
};

struct tpm_pre_os_token {
	uintptr_t lifecycle;
	uint64_t generation;
};

struct tpm_pre_os_backend {
	/* Claim locality/bus access and establish exclusive pre-OS use. */
	enum cb_err (*begin)(void *context);
	/* Complete exactly one synchronous command; no work may remain pending. */
	enum cb_err (*transmit)(void *context, const uint8_t *request,
		size_t request_size, uint8_t *response, size_t *response_size);
	/* Prove command and transport buffers idle before handoff. */
	enum cb_err (*quiesce)(void *context);
	/* Relinquish locality, bus and transport ownership. */
	enum cb_err (*release)(void *context);
};

struct tpm_pre_os_lifecycle {
	uint32_t control;
	uint32_t revision;
	uint32_t context_size;
	uint64_t generation;
	struct tpm_pre_os_backend backend;
	uint8_t backend_context[TPM_PRE_OS_BACKEND_CONTEXT_SIZE] __aligned(8);
};

/* Install once into an all-zero object and begin exclusive pre-OS use. */
enum cb_err tpm_pre_os_lifecycle_install(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_backend *backend,
	const void *backend_context, size_t backend_context_size);

/* Acquire the sole logical owner; the token detects stale and reentrant use. */
enum cb_err tpm_pre_os_lifecycle_acquire(
	struct tpm_pre_os_lifecycle *lifecycle,
	struct tpm_pre_os_token *token);

/* Transmit through the installed provider. Errors are terminal and ambiguous. */
enum cb_err tpm_pre_os_lifecycle_transmit(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token,
	const uint8_t *request, size_t request_size,
	uint8_t *response, size_t *response_size);

/* End logical ownership without relinquishing the pre-OS transport. */
enum cb_err tpm_pre_os_lifecycle_end(
	struct tpm_pre_os_lifecycle *lifecycle,
	struct tpm_pre_os_token *token);

/* Quiesce and relinquish exactly once before transferring control to an OS. */
enum cb_err tpm_pre_os_lifecycle_handoff(
	struct tpm_pre_os_lifecycle *lifecycle);

enum tpm_pre_os_state tpm_pre_os_lifecycle_state(
	const struct tpm_pre_os_lifecycle *lifecycle);

bool tpm_pre_os_lifecycle_os_access_allowed(
	const struct tpm_pre_os_lifecycle *lifecycle);

#endif /* SECURITY_TPM_PRE_OS_LIFECYCLE_H */
