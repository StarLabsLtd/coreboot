/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TPM_TRANSPORT_RELEASE_H
#define SECURITY_TPM_TRANSPORT_RELEASE_H

#include <commonlib/bsd/cb_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TPM2_TRANSPORT_MAX_POLL_ATTEMPTS 1000000U
#define TPM2_TRANSPORT_MAX_POLL_DELAY_US 1000000U
#define TPM2_TRANSPORT_MAX_POLL_TIME_US 10000000ULL

enum tpm2_transport_interface {
	TPM2_TRANSPORT_CRB,
	TPM2_TRANSPORT_FIFO,
};

/*
 * Register offsets are relative to the selected locality. The caller owns
 * address translation and the underlying bus transaction.
 */
struct tpm2_transport_io {
	enum cb_err (*read8)(void *context, uint32_t offset, uint8_t *value);
	enum cb_err (*write8)(void *context, uint32_t offset, uint8_t value);
	enum cb_err (*read32)(void *context, uint32_t offset, uint32_t *value);
	enum cb_err (*write32)(void *context, uint32_t offset, uint32_t value);
	void (*delay_us)(void *context, uint32_t delay_us);
	void *context;
};

struct tpm2_transport_release {
	enum tpm2_transport_interface interface;
	uint8_t locality;
	uint32_t poll_attempts;
	uint32_t poll_delay_us;
	struct tpm2_transport_io io;
};

/*
 * These helpers never request or seize a locality. The caller must already
 * own the configured locality and must prevent concurrent transport access.
 */
enum cb_err tpm2_transport_quiesce(
	const struct tpm2_transport_release *transport);

/* Release only when the transport is already observably quiescent. */
enum cb_err tpm2_transport_release_locality(
	const struct tpm2_transport_release *transport);

/* Quiesce first; never release locality after an ambiguous or failed wait. */
enum cb_err tpm2_transport_quiesce_and_release(
	const struct tpm2_transport_release *transport);

#endif /* SECURITY_TPM_TRANSPORT_RELEASE_H */
