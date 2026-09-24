/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOTMEM_RESERVATION_RECEIPT_INTERNAL_H
#define BOOTMEM_RESERVATION_RECEIPT_INTERNAL_H

#include <bootmem_reservation_receipt.h>

enum cb_err bootmem_reservation_receipt_mac(const uint8_t key[32],
	const void *message, size_t message_size, uint8_t mac[32]);
bool bootmem_reservation_receipt_authority_claim(
	struct bootmem_reservation_receipt_authority *authority,
	struct bootmem_reservation_receipt_authority *snapshot);
void bootmem_reservation_receipt_scrub(void *buffer, size_t size);

#endif
