/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_H

#include <boot/payload_mm_authvar_presence_transaction.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <stdbool.h>

enum payload_mm_authvar_presence_bootstrap_state {
	PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_EMPTY,
	PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_PROVISIONED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_IMPORTING,
	PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY,
	PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_FAILED,
};

/* Ownership receipts only: PREPARE remains the sole authority seed channel. */
struct payload_mm_authvar_presence_bootstrap_receipts {
	struct bootmem_reservation_receipt mailbox;
	struct bootmem_reservation_receipt page;
} __aligned(8);

/* Loader-owned protected storage, never a public or payload-supplied ABI. */
struct payload_mm_authvar_presence_bootstrap {
	uint32_t state;
	uint32_t reserved;
	struct payload_mm_authvar_presence_transaction_binding binding;
	struct smm_invocation_loader_instance_nonce loader_nonce;
	uint32_t loader_lifecycle;
	uint32_t cold_boot_proven;
	struct bootmem_reservation_receipt_authority mailbox_verifier;
	struct bootmem_reservation_receipt_authority page_verifier;
	struct bootmem_reservation_receipt page_receipt;
} __aligned(8);

bool mainboard_authvar_presence_cold_boot(void);

#if ENV_SMM || ENV_TEST
struct payload_mm_authvar_presence_bootstrap *
smm_get_payload_mm_authvar_presence_bootstrap(void);
/* Protected snapshots copied by the recognized private bootstrap dispatcher. */
enum cb_err payload_mm_authvar_presence_bootstrap_receipts_import(
	struct payload_mm_authvar_presence_bootstrap_receipts *receipts);
/*
 * Only an installed binding may be returned. The implementation checks
 * protected placement, the retained full loader identity and the actual CPU
 * topology. Invalid pointers/aliases are untouched; other failures clear the
 * output. Mailbox ownership lives solely in the existing backing evidence.
 */
enum cb_err payload_mm_authvar_presence_bootstrap_binding_get(
	const struct payload_mm_authvar_presence_transaction_binding **binding);
#endif

#endif
