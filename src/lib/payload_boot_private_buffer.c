/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_boot_private_buffer.h>
#include <stdint.h>
#include <string.h>

static bool object_valid(const void *object, size_t bytes, size_t alignment)
{
	return object && (uintptr_t)object % alignment == 0 &&
		bytes - 1U <= UINTPTR_MAX - (uintptr_t)object;
}

static bool overlaps(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	return first_base <= second_base ? second_base - first_base < first_size :
		first_base - second_base < second_size;
}

static bool reservation_valid(uint64_t base, uint64_t bytes, uint32_t tag)
{
	return base && !(base & 4095U) &&
		bytes == LB_PAYLOAD_BOOT_PRIVATE_BUFFER_BYTES &&
		base <= UINT64_MAX - (LB_PAYLOAD_BOOT_PRIVATE_BUFFER_BYTES - 1U) &&
		tag == BM_MEM_RESERVED;
}

#if ENV_RAMSTAGE || ENV_TEST
int payload_boot_private_buffer_reserve(struct bootmem_aligned_reservation_handle *handle)
{
	const struct bootmem_aligned_reservation_request request = {
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(request),
		.bytes = LB_PAYLOAD_BOOT_PRIVATE_BUFFER_BYTES,
		.alignment = 4096U,
		.limit_exclusive = 1ULL << 32,
		.tag = BM_MEM_RESERVED,
	};
	struct bootmem_aligned_reservation_handle candidate = { 0 };

	if (!object_valid(handle, sizeof(*handle), _Alignof(*handle)))
		return -1;
	memset(handle, 0, sizeof(*handle));
	if (bootmem_aligned_reservation_register(&request, &candidate))
		return -1;
	*handle = candidate;
	return 0;
}

enum cb_err payload_boot_private_buffer_emit(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt)
{
	struct bootmem_aligned_reservation reservation;

	if (!object_valid(handle, sizeof(*handle), _Alignof(*handle)) ||
	    !object_valid(signer, sizeof(*signer), _Alignof(*signer)) ||
	    !object_valid(receipt, sizeof(*receipt), _Alignof(*receipt)) ||
	    overlaps(handle, sizeof(*handle), signer, sizeof(*signer)) ||
	    overlaps(handle, sizeof(*handle), receipt, sizeof(*receipt)) ||
	    overlaps(signer, sizeof(*signer), receipt, sizeof(*receipt)))
		return CB_ERR;
	memset(receipt, 0, sizeof(*receipt));
	if (bootmem_aligned_reservation_query(handle, &reservation) ||
	    reservation.reserved ||
	    !reservation_valid(reservation.base, reservation.size, reservation.tag)) {
		bootmem_reservation_receipt_close(signer);
		return CB_ERR;
	}
	return bootmem_aligned_reservation_receipt_emit_exact_tag(handle, signer, receipt,
		BM_MEM_RESERVED);
}
#endif

enum cb_err payload_boot_private_buffer_consume(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt,
	struct lb_payload_boot_private_buffer *record)
{
	struct bootmem_reservation_receipt captured;
	struct lb_payload_boot_private_buffer candidate = {
		.tag = LB_TAG_PAYLOAD_BOOT_PRIVATE_BUFFER,
		.size = sizeof(candidate),
		.revision = LB_PAYLOAD_BOOT_PRIVATE_BUFFER_REVISION,
		.header_size = sizeof(candidate),
		.slot_size = LB_PAYLOAD_BOOT_PRIVATE_BUFFER_SLOT_SIZE,
		.bytes = LB_PAYLOAD_BOOT_PRIVATE_BUFFER_BYTES,
		.slot_count = LB_PAYLOAD_BOOT_PRIVATE_BUFFER_SLOT_COUNT,
	};
	enum cb_err result = CB_ERR;
	enum cb_err verification;
	bool geometry_valid;

	if (!object_valid(verifier, sizeof(*verifier), _Alignof(*verifier)) ||
	    !object_valid(receipt, sizeof(*receipt), _Alignof(*receipt)) ||
	    !object_valid(record, sizeof(*record), _Alignof(*record)) ||
	    overlaps(verifier, sizeof(*verifier), receipt, sizeof(*receipt)) ||
	    overlaps(record, sizeof(*record), verifier, sizeof(*verifier)) ||
	    overlaps(record, sizeof(*record), receipt, sizeof(*receipt)))
		return CB_ERR;
	memset(record, 0, sizeof(*record));
	captured = *receipt;
	geometry_valid = reservation_valid(captured.base, captured.bytes, captured.tag);
	candidate.physical_base = captured.base;
	/* Verify the same captured bytes used for metadata, never a second live read. */
	verification = bootmem_reservation_receipt_verify_consume_exact_tag(verifier,
		&captured, BM_MEM_RESERVED);
	memset(receipt, 0, sizeof(*receipt));
	if (verification != CB_SUCCESS || !geometry_valid)
		goto out;
	*record = candidate;
	result = CB_SUCCESS;
out:
	memset(&captured, 0, sizeof(captured));
	return result;
}
