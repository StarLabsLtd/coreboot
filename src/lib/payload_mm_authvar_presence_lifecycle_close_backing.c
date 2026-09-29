/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_authvar_presence_lifecycle_close_endpoint_internal.h"
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <bootmem.h>
#include <string.h>

enum backing_state { BACKING_EMPTY, BACKING_RESERVED, BACKING_PROVIDER,
	BACKING_ABORT_REQUESTED,
	BACKING_READY, BACKING_PUBLICATION, BACKING_COMMITTED, BACKING_FAILED };
struct backing_owner {
	uint8_t lock;
	uint32_t state;
	struct bootmem_aligned_reservation_handle handle;
	struct payload_mm_authvar_presence_lifecycle_close_backing backing;
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt receipt;
};
static struct backing_owner owner;

static void lock(void)
{
	while (__atomic_test_and_set(&owner.lock, __ATOMIC_ACQUIRE))
		;
}

static void unlock(void)
{
	__atomic_clear(&owner.lock, __ATOMIC_RELEASE);
}

static void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;
	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool receipt_valid(
	const struct payload_mm_authvar_presence_lifecycle_close_ready_receipt *receipt)
{
	return receipt && receipt->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_READY_RECEIPT_REVISION &&
		receipt->size == sizeof(*receipt) && receipt->identity &&
		receipt->nonce && receipt->active == 1 && !receipt->reserved &&
		payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
			&receipt->endpoint) == CB_SUCCESS;
}

static void fail_locked(void)
{
	if (owner.backing.base && owner.backing.bytes ==
	    PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE)
		scrub((void *)(uintptr_t)owner.backing.base, owner.backing.bytes);
	scrub((uint8_t *)&owner + offsetof(struct backing_owner, handle),
		sizeof(owner) - offsetof(struct backing_owner, handle));
	owner.state = BACKING_FAILED;
}

void payload_mm_authvar_presence_lifecycle_close_backing_abort(void)
{
	lock();
	if (owner.state == BACKING_PROVIDER)
		owner.state = BACKING_ABORT_REQUESTED;
	else if (owner.state != BACKING_COMMITTED && owner.state != BACKING_FAILED &&
		 owner.state != BACKING_ABORT_REQUESTED)
		fail_locked();
	unlock();
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_reserve(void)
{
	const struct bootmem_aligned_reservation_request request = {
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION, .size = sizeof(request),
		.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE,
		.alignment = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT,
		.limit_exclusive = 1ULL << 32, .tag = BM_MEM_RESERVED,
	};
	enum cb_err status = CB_ERR;
	lock();
	if (owner.state == BACKING_EMPTY &&
	    !bootmem_aligned_reservation_register(&request, &owner.handle)) {
		owner.state = BACKING_RESERVED;
		status = CB_SUCCESS;
	} else if (owner.state == BACKING_EMPTY) {
		fail_locked();
	}
	unlock();
	return status;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_take(
	struct payload_mm_authvar_presence_lifecycle_close_backing *backing)
{
	struct bootmem_aligned_reservation reservation;
	const uint64_t limit = 1ULL << 32;
	enum cb_err status = CB_ERR;
	if (backing)
		memset(backing, 0, sizeof(*backing));
	lock();
	if (backing && owner.state == BACKING_RESERVED &&
	    !bootmem_aligned_reservation_query(&owner.handle, &reservation) &&
	    reservation.base && reservation.base < limit &&
	    reservation.size == PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE &&
	    reservation.size <= limit - reservation.base &&
	    reservation.tag == BM_MEM_RESERVED && !reservation.reserved &&
	    !(reservation.base &
		(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT - 1U))) {
		owner.backing = (struct payload_mm_authvar_presence_lifecycle_close_backing) {
			.base = reservation.base, .bytes = reservation.size,
			.tag = reservation.tag,
		};
		scrub((void *)(uintptr_t)reservation.base, reservation.size);
		*backing = owner.backing;
		owner.state = BACKING_PROVIDER;
		status = CB_SUCCESS;
	} else if (owner.state == BACKING_RESERVED) {
		fail_locked();
	}
	unlock();
	return status;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_return(
	const struct payload_mm_authvar_presence_lifecycle_close_backing *backing,
	const struct payload_mm_authvar_presence_lifecycle_close_ready_receipt *receipt)
{
	enum cb_err status = CB_ERR;
	lock();
	if (owner.state == BACKING_ABORT_REQUESTED) {
		fail_locked();
	} else if (owner.state == BACKING_PROVIDER && backing &&
	    !memcmp(backing, &owner.backing, sizeof(*backing)) &&
	    receipt_valid(receipt) &&
	    receipt->endpoint.communication_base == backing->base) {
		owner.receipt = *receipt;
		owner.state = BACKING_READY;
		status = CB_SUCCESS;
	} else if (owner.state == BACKING_PROVIDER) {
		fail_locked();
	}
	unlock();
	return status;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_cancel(
	const struct payload_mm_authvar_presence_lifecycle_close_backing *backing)
{
	enum cb_err status = CB_ERR;

	lock();
	if ((owner.state == BACKING_PROVIDER ||
	     owner.state == BACKING_ABORT_REQUESTED) && backing &&
	    !memcmp(backing, &owner.backing, sizeof(*backing))) {
		fail_locked();
		status = CB_SUCCESS;
	}
	unlock();
	return status;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_ready_receipt_consume(
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt *receipt,
	struct lb_authvar_presence_lifecycle_close_endpoint *endpoint)
{
	enum cb_err status = CB_ERR;
	lock();
	if (owner.state == BACKING_READY && receipt_valid(receipt) && endpoint &&
	    receipt->revision == owner.receipt.revision &&
	    receipt->size == owner.receipt.size &&
	    !memcmp(&receipt->endpoint, &owner.receipt.endpoint,
		sizeof(receipt->endpoint)) &&
	    receipt->identity == owner.receipt.identity &&
	    receipt->nonce == owner.receipt.nonce &&
	    receipt->active == owner.receipt.active) {
		owner.state = BACKING_PUBLICATION;
		*endpoint = owner.receipt.endpoint;
		scrub(receipt, sizeof(*receipt));
		status = CB_SUCCESS;
	}
	unlock();
	return status;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_publication_commit(void)
{
	enum cb_err status = CB_ERR;
	lock();
	if (owner.state == BACKING_PUBLICATION) {
		owner.state = BACKING_COMMITTED;
		scrub(&owner.receipt, sizeof(owner.receipt));
		status = CB_SUCCESS;
	}
	unlock();
	return status;
}

#if ENV_TEST
void payload_mm_authvar_presence_lifecycle_close_endpoint_reset_test(void)
{
	memset(&owner, 0, sizeof(owner));
}
#endif
