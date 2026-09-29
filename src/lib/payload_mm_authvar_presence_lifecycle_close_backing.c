/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_authvar_presence_lifecycle_close_endpoint_internal.h"
#include <boot/payload_mm_authvar_presence_lifecycle_close_backing.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <bootmem.h>
#include <bootmem_reservation_receipt.h>
#include <commonlib/helpers.h>
#include <random.h>
#include <string.h>

enum backing_state { BACKING_EMPTY, BACKING_RESERVED, BACKING_PROVIDER,
	BACKING_ATTESTING,
	BACKING_ABORT_REQUESTED,
	BACKING_READY, BACKING_PUBLICATION, BACKING_COMMITTED, BACKING_FAILED };
struct backing_owner {
	uint8_t lock;
	uint32_t state;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
	uint32_t verifier_delivered;
	uint64_t authority_generation;
	struct bootmem_reservation_receipt_authority signer;
	struct bootmem_reservation_receipt_authority verifier;
#endif
	struct bootmem_aligned_reservation_handle handle;
	struct payload_mm_authvar_presence_lifecycle_close_backing backing;
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt receipt;
};
static struct backing_owner owner;

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}
#endif

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
		scrub((void *)(uintptr_t)owner.backing.base,
			(size_t)owner.backing.bytes);
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
	scrub((uint8_t *)&owner + offsetof(struct backing_owner, verifier_delivered),
		sizeof(owner) - offsetof(struct backing_owner, verifier_delivered));
#else
	scrub((uint8_t *)&owner + offsetof(struct backing_owner, handle),
		sizeof(owner) - offsetof(struct backing_owner, handle));
#endif
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
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
	uint64_t secret_words[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE /
		sizeof(uint64_t)] = { 0 };
	uint8_t *const secret = (uint8_t *)secret_words;
#endif
	lock();
	if (owner.state == BACKING_EMPTY &&
	    !bootmem_aligned_reservation_register(&request, &owner.handle)) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
		for (size_t index = 0; index < ARRAY_SIZE(secret_words); index++)
			if (get_random_number_64(&secret_words[index]) != CB_SUCCESS)
				goto failed;
		if (get_random_number_64(&owner.authority_generation) != CB_SUCCESS ||
		    !owner.authority_generation ||
		    bootmem_reservation_receipt_provision(&owner.signer,
			&owner.verifier, secret,
			BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
			owner.authority_generation, &owner.handle) != CB_SUCCESS)
			goto failed;
#endif
		owner.state = BACKING_RESERVED;
		status = CB_SUCCESS;
	} else if (owner.state == BACKING_EMPTY) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
failed:
#endif
		fail_locked();
	}
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
	scrub(secret_words, sizeof(secret_words));
#endif
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
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
	    owner.verifier_delivered == 1U &&
#endif
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
		scrub((void *)(uintptr_t)reservation.base, (size_t)reservation.size);
		*backing = owner.backing;
		owner.state = BACKING_PROVIDER;
		status = CB_SUCCESS;
	} else if (owner.state == BACKING_RESERVED) {
		fail_locked();
	}
	unlock();
	return status;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT)
bool payload_mm_authvar_presence_lifecycle_close_backing_verifier_take(
	struct bootmem_reservation_receipt_authority *destination)
{
	const uintptr_t destination_base = (uintptr_t)destination;
	const uintptr_t owner_base = (uintptr_t)&owner;
	bool delivered = false;

	if (!destination || destination_base % _Alignof(*destination) ||
	    destination_base > UINTPTR_MAX - (sizeof(*destination) - 1U) ||
	    !((destination_base + sizeof(*destination) <= owner_base) ||
	      (owner_base + sizeof(owner) <= destination_base)))
		return false;
	lock();
	if (owner.state == BACKING_RESERVED && !owner.verifier_delivered &&
	    zero(destination, sizeof(*destination))) {
		*destination = owner.verifier;
		scrub(&owner.verifier, sizeof(owner.verifier));
		owner.verifier_delivered = 1U;
		delivered = true;
	}
	unlock();
	return delivered;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_attest(
	struct bootmem_reservation_receipt *receipt)
{
	enum cb_err status = CB_ERR;

	if (!receipt)
		return CB_ERR;
	memset(receipt, 0, sizeof(*receipt));
	lock();
	if (owner.state != BACKING_PROVIDER || !owner.authority_generation)
		goto out;
	owner.state = BACKING_ATTESTING;
	if (bootmem_aligned_reservation_receipt_emit_exact_tag(&owner.handle,
		&owner.signer, receipt, BM_MEM_RESERVED) != CB_SUCCESS ||
	    receipt->revision != BOOTMEM_RESERVATION_RECEIPT_REVISION ||
	    receipt->size != sizeof(*receipt) ||
	    receipt->boot_kind != BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT ||
	    receipt->reserved || !receipt->sequence ||
	    receipt->generation != owner.authority_generation ||
	    memcmp(&receipt->handle, &owner.handle, sizeof(owner.handle)) ||
	    receipt->base != owner.backing.base ||
	    receipt->bytes != owner.backing.bytes || receipt->tag != BM_MEM_RESERVED ||
	    receipt->use != BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE) {
		fail_locked();
		goto out;
	}
	owner.state = BACKING_PROVIDER;
	status = CB_SUCCESS;
out:
	if (status != CB_SUCCESS)
		scrub(receipt, sizeof(*receipt));
	unlock();
	return status;
}
#endif

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
