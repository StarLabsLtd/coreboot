/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */
/* Copyright (c) 2026 Star Labs Systems. All rights reserved. */

#ifndef SECURITY_TPM_DAVE_SESSION_H
#define SECURITY_TPM_DAVE_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Keep the native caller's status representation without an EFI dependency. */
#define DAVE_TPM_ERROR_BIT (1ULL << 63)
#define DAVE_TPM_SUCCESS 0ULL
#define DAVE_TPM_INVALID_PARAMETER (DAVE_TPM_ERROR_BIT | 2U)
#define DAVE_TPM_DEVICE_ERROR (DAVE_TPM_ERROR_BIT | 7U)
#define DAVE_TPM_NOT_FOUND (DAVE_TPM_ERROR_BIT | 14U)
#define DAVE_TPM_ABORTED (DAVE_TPM_ERROR_BIT | 21U)
#define DAVE_TPM_PROTOCOL_ERROR (DAVE_TPM_ERROR_BIT | 24U)
#define DAVE_TPM_SECURITY_VIOLATION (DAVE_TPM_ERROR_BIT | 26U)

#define DAVE_TPM_NV_WRITE_MAX 284U

static inline bool dave_tpm_error(uint64_t status)
{
	return (status & DAVE_TPM_ERROR_BIT) != 0U;
}

/* Success means the complete framed response is present within capacity.
 * Transport errors never prove a missing TPM handle. Callers own all buffers.
 */
typedef uint64_t dave_tpm_exchange_fn(void *, const uint8_t *, size_t, uint8_t *, size_t);
typedef int dave_tpm_random_fn(void *, unsigned char *, size_t);

struct tpm_reader {
	dave_tpm_exchange_fn *exchange;
	void *transport_context;
	uint8_t pin[34];
	dave_tpm_random_fn *random;
	void *random_context;
	/* A later successful flush cannot restore authorization usability. */
	bool cleanup_failed;
};

struct tpm_session {
	uint32_t handle;
	uint8_t key[32], nonce[32];
};

struct nv_profile {
	uint32_t index, attributes;
	uint16_t size;
	uint8_t policy[32], name[34];
};

struct dave_tpm_lockout {
	uint32_t max_tries;
	uint32_t recovery_seconds;
	uint32_t lockout_recovery_seconds;
};

uint64_t dave_tpm_submit(struct tpm_reader *reader, const uint8_t *command,
	size_t command_size, uint8_t *response, size_t capacity, uint16_t tag, size_t *size);
uint64_t dave_tpm_public_area(struct tpm_reader *reader, uint32_t handle, bool nv,
	uint8_t *public, size_t capacity, size_t *public_size, uint8_t name[34]);
uint64_t dave_tpm_start_session(struct tpm_reader *reader,
	struct tpm_session *session, bool policy);
/* Only the trusted office identity owner supplies an unpinned primary here. */
uint64_t dave_tpm_start_identity_session(struct tpm_reader *reader,
	struct tpm_session *session, uint32_t handle, const uint8_t public[92],
	const uint8_t name[34]);
uint64_t dave_tpm_random32(struct tpm_reader *reader, uint8_t random[32]);
int dave_tpm_kdfa(const uint8_t *key, size_t key_size, const char label[4],
	const uint8_t newer[32], const uint8_t older[32], uint8_t output[32]);
int dave_tpm_session_hmac(const uint8_t *key, size_t key_size, const uint8_t digest[32],
	const uint8_t newer[32], const uint8_t older[32], uint8_t attributes, uint8_t output[32]);
uint64_t dave_tpm_flush_handle(struct tpm_reader *reader, uint32_t handle);
uint64_t dave_tpm_flush_session(struct tpm_reader *reader, struct tpm_session *session);
uint64_t dave_tpm_finish_session(struct tpm_reader *reader, struct tpm_session *session,
	uint64_t status);
uint64_t dave_tpm_authenticated_read(struct tpm_reader *reader,
	struct tpm_session *session, const struct nv_profile *nv, uint16_t count,
	uint16_t offset, uint8_t *output, bool encrypted);
uint64_t dave_tpm_authenticated_write(struct tpm_reader *reader,
	struct tpm_session *session, const struct nv_profile *profile, uint16_t count,
	uint16_t offset, const uint8_t *data);
uint64_t dave_tpm_read_pcr(struct tpm_reader *reader, uint8_t pcr, uint8_t value[32]);
uint64_t dave_tpm_audited_capability(struct tpm_reader *reader, uint32_t capability,
	uint32_t first, uint32_t requested, uint8_t output[73], size_t *output_size);
uint64_t dave_tpm_permanent_attributes(struct tpm_reader *reader, uint32_t *attributes);
uint64_t dave_tpm_lockout_parameters(struct tpm_reader *reader,
	struct dave_tpm_lockout *parameters);

#endif
