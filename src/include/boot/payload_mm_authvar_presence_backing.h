/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_H

#include <boot/payload_mm_authvar_presence.h>
#include <bootmem_reservation_receipt.h>

enum cb_err payload_mm_authvar_presence_backing_evidence_publish(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt,
	const struct payload_mm_authvar_presence_backing *backing);
enum cb_err payload_mm_authvar_presence_backing_evidence_take(
	const struct payload_mm_authvar_presence_backing *backing);
void payload_mm_authvar_presence_backing_evidence_close(void);
bool payload_mm_authvar_presence_backing_evidence_consumed(void);

#if ENV_TEST
typedef void (*payload_mm_authvar_presence_backing_test_hook_fn)(void);

void payload_mm_authvar_presence_backing_evidence_reset_test(void);
void payload_mm_authvar_presence_backing_close_test_hook(
	payload_mm_authvar_presence_backing_test_hook_fn hook);
bool payload_mm_authvar_presence_backing_terminal_test(void);
bool payload_mm_authvar_presence_backing_scrubbed_test(void);
#endif

#endif
