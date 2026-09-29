/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_H

#include "dma_smm_receipt_provision.h"
#include <boot/payload_mm_authvar_presence_lifecycle_close_mailbox.h>
#include <cpu/x86/smm_invocation_entry.h>

struct starbook_mtl_lifecycle_mailbox_binding {
	uint64_t base;
	uint64_t size;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint64_t invocation_generation;
	uint32_t loader_lifecycle;
	uint32_t reserved;
};

enum cb_err starbook_mtl_lifecycle_mailbox_verify_consume(
	struct bootmem_reservation_receipt *receipt, uint64_t requested_base,
	size_t requested_size, const void *transient_frame, size_t transient_size,
	const struct smm_invocation_save_state_ops *active_ops,
	struct starbook_mtl_lifecycle_mailbox_binding *binding);
bool starbook_mtl_lifecycle_mailbox_dma_protected(void *context,
	uint64_t base, size_t size);
void *starbook_mtl_lifecycle_mailbox_dma_context(void);
size_t starbook_mtl_lifecycle_mailbox_dma_context_size(void);
void starbook_mtl_lifecycle_mailbox_poison(void);
#if ENV_TEST
void starbook_mtl_lifecycle_mailbox_reset_test(void);
#endif

#endif
