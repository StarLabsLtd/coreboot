/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_SERVICE_RECEIVER_H
#define BOOT_PAYLOAD_MM_AUTHVAR_SERVICE_RECEIVER_H

#include <bootmem_reservation_receipt.h>

/*
 * Trusted canonical bootstrap boundary, not a public SMI. Both inputs must be
 * disjoint protected loader storage. Consume the exact BM_MEM_TABLE receipt
 * once, after the installed canonical binding proves cold loader identity and
 * topology. This prepares communication ownership, not backend readiness or
 * endpoint publication.
 */
enum cb_err payload_mm_authvar_service_prepare(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt);

/* Terminal canonical-import failure before backend install; cannot reopen it. */
void payload_mm_authvar_service_prepare_abort(void);

#endif
