/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_H

#include <bootmem_reservation_receipt.h>
#include <stdbool.h>

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
/* One-way loader boundary: ramstage may deliver this authority exactly once. */
bool payload_mm_authvar_presence_lifecycle_close_backing_verifier_take(
	struct bootmem_reservation_receipt_authority *destination);
enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_attest(
	struct bootmem_reservation_receipt *receipt);

#if ENV_SMM || ENV_TEST
struct bootmem_reservation_receipt_authority *
	smm_get_payload_mm_authvar_presence_lifecycle_close_backing_verifier(void);
#endif
#endif

#endif
