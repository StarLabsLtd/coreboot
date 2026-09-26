/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOTMEM_RESERVATION_RECEIPT_H
#define BOOTMEM_RESERVATION_RECEIPT_H

#include <bootmem.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stddef.h>
#include <stdint.h>

#define BOOTMEM_RESERVATION_RECEIPT_REVISION 1U
#define BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE 32U
#define BOOTMEM_RESERVATION_RECEIPT_MAC_SIZE 32U
#define BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE 1U
#define BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT 1U

struct bootmem_reservation_receipt {
	uint32_t revision;
	uint32_t size;
	uint32_t boot_kind;
	uint32_t reserved;
	uint64_t generation;
	uint64_t sequence;
	struct bootmem_aligned_reservation_handle handle;
	uint64_t base;
	uint64_t bytes;
	uint32_t tag;
	uint32_t use;
	uint8_t mac[BOOTMEM_RESERVATION_RECEIPT_MAC_SIZE];
} __aligned(8);

struct bootmem_reservation_receipt_authority {
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];
	uint64_t generation;
	struct bootmem_aligned_reservation_handle handle;
	uint64_t sequence;
	uint32_t boot_kind;
	uint8_t state;
	uint8_t reserved[3];
} __aligned(8);

_Static_assert(sizeof(struct bootmem_reservation_receipt) == 96,
	"bootmem reservation receipt ABI changed");
_Static_assert(sizeof(struct bootmem_reservation_receipt_authority) == 64,
	"bootmem reservation receipt authority ABI changed");

/*
 * Provision equal one-shot authorities while the verifier destination is in
 * protected memory. The caller must independently establish that placement.
 * The source secret is consumed and scrubbed on every call, including failure.
 */
enum cb_err bootmem_reservation_receipt_provision(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier,
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE],
	uint32_t boot_kind, uint64_t generation,
	const struct bootmem_aligned_reservation_handle *handle);

/* A verification attempt is terminal and scrubs both receipt and authority. */
enum cb_err bootmem_reservation_receipt_verify_consume(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt);

/*
 * Verify and consume a receipt only for the exact supported bootmem tag.
 * BM_MEM_TABLE and BM_MEM_RESERVED are the only admitted tags. The legacy
 * wrapper above remains BM_MEM_TABLE-only.
 */
enum cb_err bootmem_reservation_receipt_verify_consume_exact_tag(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt, enum bootmem_type expected_tag);

void bootmem_reservation_receipt_close(
	struct bootmem_reservation_receipt_authority *authority);

#if CONFIG(BOOTMEM_ALIGNED_RESERVATION_RECEIPT)
/* Sign only the exact result committed by bootmem for this opaque handle. */
enum cb_err bootmem_aligned_reservation_receipt_emit(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt);

/* Emit only when both committed bootmem maps have the exact supported tag. */
enum cb_err bootmem_aligned_reservation_receipt_emit_exact_tag(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt,
	enum bootmem_type expected_tag);
#endif

#endif /* BOOTMEM_RESERVATION_RECEIPT_H */
