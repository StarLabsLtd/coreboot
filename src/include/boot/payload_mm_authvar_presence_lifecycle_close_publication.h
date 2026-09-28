/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PUBLICATION_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PUBLICATION_H

#include <boot/coreboot_tables.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

/* Opaque, one-use correlation for a provider-sealed ready endpoint. */
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_READY_RECEIPT_REVISION 1U
struct payload_mm_authvar_presence_lifecycle_close_ready_receipt {
	uint32_t revision;
	uint32_t size;
	struct lb_authvar_presence_lifecycle_close_endpoint endpoint;
	uintptr_t identity;
	uint64_t nonce;
	uint32_t active;
	uint32_t reserved;
};

_Static_assert(offsetof(
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt, endpoint) == 8,
	"lifecycle-close ready receipt header layout");

enum cb_err payload_mm_authvar_presence_lifecycle_close_endpoint_reserve(void);
enum cb_err lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint(
	struct lb_header *header, uintptr_t table_end);

/* Hidden capability: the default implementation never supplies a receipt. */
bool platform_payload_mm_authvar_presence_lifecycle_close_ready_receipt(
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt *receipt);

#if ENV_TEST
void payload_mm_authvar_presence_lifecycle_close_publication_reset_test(void);
#endif

#endif
