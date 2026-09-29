/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_H

#include <bootmem_reservation_receipt.h>
#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_invocation_loader_identity.h>
#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_invocation_loader_composition.h>
#include <cpu/x86/smm_invocation_loader_instance.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_REVISION 1U

enum payload_mm_authvar_presence_lifecycle_close_mailbox_state {
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_EMPTY,
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_READY,
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_VERIFYING,
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_CONSUMED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_FAILED,
};

struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority {
	struct bootmem_reservation_receipt_authority verifier;
	uint32_t revision;
	uint32_t size;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint64_t invocation_generation;
	uint32_t loader_lifecycle;
	uint32_t state;
	uint64_t reserved;
} __aligned(8);

#if ENV_RAMSTAGE || ENV_TEST
enum cb_err payload_mm_authvar_presence_lifecycle_close_mailbox_loader_provision(
	struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority *slot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const struct smm_invocation_loader_composition *composition);
void payload_mm_authvar_presence_lifecycle_close_mailbox_loader_abort(
	struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority *slot);
#endif

#endif
