/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_BOOT_PRIVATE_BUFFER_H
#define BOOT_PAYLOAD_BOOT_PRIVATE_BUFFER_H

#include <bootmem_reservation_receipt.h>
#include <commonlib/coreboot_tables.h>

/*
 * Dormant allocation prerequisite. Slot order is request, response, policy.
 * No API establishes protected verifier placement, a DMA-origin lease, or
 * permission to use the buffer after external code, EBS, or S3.
 */
#if ENV_RAMSTAGE || ENV_TEST
int payload_boot_private_buffer_reserve(struct bootmem_aligned_reservation_handle *handle);
enum cb_err payload_boot_private_buffer_emit(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt);
#endif

/* Consume an existing one-shot verifier; export metadata only after success. */
enum cb_err payload_boot_private_buffer_consume(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt,
	struct lb_payload_boot_private_buffer *record);

#endif
