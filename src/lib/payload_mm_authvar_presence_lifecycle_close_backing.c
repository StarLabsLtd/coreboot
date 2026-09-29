/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_authvar_presence_lifecycle_close_endpoint_internal.h"
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
#include <boot/payload_mm_authvar_presence_lifecycle_close_mailbox.h>
#endif
#include <bootmem.h>
#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <random.h>
#include <string.h>

enum backing_state { BACKING_EMPTY, BACKING_RESERVED, BACKING_PROVIDER,
	BACKING_ABORT_REQUESTED,
	BACKING_READY, BACKING_PUBLICATION, BACKING_COMMITTED, BACKING_FAILED };
struct backing_owner {
	uint8_t lock;
	uint32_t state;
	struct bootmem_aligned_reservation_handle handle;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	struct bootmem_reservation_receipt_authority signer;
#endif
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

static enum cb_err backing_take(
	struct payload_mm_authvar_presence_lifecycle_close_backing *backing,
	struct bootmem_reservation_receipt *reservation_receipt)
{
	struct bootmem_aligned_reservation reservation;
	const uint64_t limit = 1ULL << 32;
	enum cb_err status = CB_ERR;
#if !CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	(void)reservation_receipt;
#endif
	if (backing)
		memset(backing, 0, sizeof(*backing));
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	if (reservation_receipt)
		memset(reservation_receipt, 0, sizeof(*reservation_receipt));
#endif
	lock();
	if (backing &&
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	    (!reservation_receipt || owner.signer.state) &&
#endif
	    owner.state == BACKING_RESERVED &&
	    !bootmem_aligned_reservation_query(&owner.handle, &reservation) &&
	    reservation.base && reservation.base < limit &&
	    reservation.size == PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE &&
	    reservation.size <= limit - reservation.base &&
	    reservation.tag == BM_MEM_RESERVED && !reservation.reserved &&
	    !(reservation.base &
		(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT - 1U))
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
	    && (!reservation_receipt ||
	    bootmem_aligned_reservation_receipt_emit_exact_tag(&owner.handle,
		&owner.signer, reservation_receipt, BM_MEM_RESERVED) == CB_SUCCESS)
#endif
	    ) {
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

enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_take(
	struct payload_mm_authvar_presence_lifecycle_close_backing *backing)
{
	return backing_take(backing, NULL);
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_take_authenticated(
	struct payload_mm_authvar_presence_lifecycle_close_backing *backing,
	struct bootmem_reservation_receipt *reservation_receipt)
{
	if (!reservation_receipt)
		return CB_ERR;
	return backing_take(backing, reservation_receipt);
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_mailbox_loader_provision(
	struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority *slot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const struct smm_invocation_loader_composition *composition)
{
	struct evidence_identity {
		uint32_t state;
		uint32_t loader_lifecycle;
		struct smm_invocation_loader_instance_nonce loader_instance_nonce;
		uint64_t generation;
	};
	struct smm_invocation_loader_instance snapshot;
	struct smm_invocation_loader_instance rechecked;
	struct smm_invocation_loader_instance final_instance;
	struct evidence_identity evidence_snapshot;
	struct evidence_identity evidence_rechecked;
	struct evidence_identity final_evidence;
	uint64_t random[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE /
		sizeof(uint64_t)] = { 0 };
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE] = { 0 };
	const uintptr_t slot_base = (uintptr_t)slot;
	enum cb_err status = CB_ERR;

	if (!slot || !instance || !evidence || !composition ||
	    slot_base % _Alignof(*slot) ||
	    slot_base > UINTPTR_MAX - (sizeof(*slot) - 1U) ||
	    (slot_base <= (uintptr_t)&owner ?
		(uintptr_t)&owner - slot_base < sizeof(*slot) :
		slot_base - (uintptr_t)&owner < sizeof(owner)) ||
	    (slot_base <= (uintptr_t)instance ?
		(uintptr_t)instance - slot_base < sizeof(*slot) :
		slot_base - (uintptr_t)instance < sizeof(*instance)) ||
	    (slot_base <= (uintptr_t)evidence ?
		(uintptr_t)evidence - slot_base < sizeof(*slot) :
		slot_base - (uintptr_t)evidence < sizeof(*evidence)) ||
	    (slot_base <= (uintptr_t)composition ?
		(uintptr_t)composition - slot_base < sizeof(*slot) :
		slot_base - (uintptr_t)composition < sizeof(*composition)))
		return CB_ERR;
	evidence_snapshot = (struct evidence_identity) {
		.state = evidence->state,
		.loader_lifecycle = evidence->loader_lifecycle,
		.loader_instance_nonce = evidence->loader_instance_nonce,
		.generation = evidence->generation,
	};
	if (smm_invocation_loader_instance_read(instance, &snapshot) != CB_SUCCESS ||
	    snapshot.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    smm_invocation_loader_instance_nonce_is_zero(
		snapshot.loader_instance_nonce) ||
	    smm_invocation_loader_composition_evidence(composition, evidence) !=
		evidence ||
	    smm_invocation_evidence_phase(evidence) != SMM_INVOCATION_READY ||
	    !evidence_snapshot.generation ||
	    evidence_snapshot.loader_lifecycle != snapshot.lifecycle ||
	    !smm_invocation_loader_instance_nonce_equal(
		evidence_snapshot.loader_instance_nonce,
		snapshot.loader_instance_nonce) ||
	    evidence_snapshot.state != evidence->state ||
	    evidence_snapshot.loader_lifecycle != evidence->loader_lifecycle ||
	    evidence_snapshot.generation != evidence->generation ||
	    !smm_invocation_loader_instance_nonce_equal(
		evidence_snapshot.loader_instance_nonce,
		evidence->loader_instance_nonce))
		goto out;
	for (size_t index = 0; index < ARRAY_SIZE(random); index++)
		if (get_random_number_64(&random[index]) != CB_SUCCESS)
			goto out;
	memcpy(secret, random, sizeof(secret));
	evidence_rechecked = (struct evidence_identity) {
		.state = evidence->state,
		.loader_lifecycle = evidence->loader_lifecycle,
		.loader_instance_nonce = evidence->loader_instance_nonce,
		.generation = evidence->generation,
	};
	lock();
	if (owner.state == BACKING_RESERVED && !slot->revision && !slot->size &&
	    !slot->state && !slot->reserved &&
	    smm_invocation_loader_instance_read(instance, &rechecked) == CB_SUCCESS &&
	    !memcmp(&snapshot, &rechecked, sizeof(snapshot)) &&
	    !memcmp(&evidence_snapshot, &evidence_rechecked,
		sizeof(evidence_snapshot)) &&
	    smm_invocation_loader_composition_evidence(composition, evidence) ==
		evidence &&
	    bootmem_reservation_receipt_provision(&owner.signer, &slot->verifier,
		secret, BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		evidence_snapshot.generation, &owner.handle) == CB_SUCCESS) {
		final_evidence = (struct evidence_identity) {
			.state = evidence->state,
			.loader_lifecycle = evidence->loader_lifecycle,
			.loader_instance_nonce = evidence->loader_instance_nonce,
			.generation = evidence->generation,
		};
		if (smm_invocation_loader_instance_read(instance, &final_instance) ==
			CB_SUCCESS &&
		    !memcmp(&snapshot, &final_instance, sizeof(snapshot)) &&
		    !memcmp(&evidence_snapshot, &final_evidence,
			sizeof(evidence_snapshot)) &&
		    smm_invocation_loader_composition_evidence(composition,
			evidence) == evidence) {
			slot->revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_REVISION;
			slot->size = sizeof(*slot);
			slot->loader_instance_nonce = snapshot.loader_instance_nonce;
			slot->invocation_generation = evidence_snapshot.generation;
			slot->loader_lifecycle = snapshot.lifecycle;
			__atomic_store_n(&slot->state,
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_READY,
				__ATOMIC_RELEASE);
			status = CB_SUCCESS;
		} else {
			bootmem_reservation_receipt_close(&owner.signer);
			bootmem_reservation_receipt_close(&slot->verifier);
			fail_locked();
		}
	} else if (owner.state == BACKING_RESERVED) {
		bootmem_reservation_receipt_close(&owner.signer);
		fail_locked();
	}
	unlock();
out:
	scrub(random, sizeof(random));
	scrub(secret, sizeof(secret));
	scrub(&snapshot, sizeof(snapshot));
	scrub(&rechecked, sizeof(rechecked));
	scrub(&final_instance, sizeof(final_instance));
	scrub(&evidence_snapshot, sizeof(evidence_snapshot));
	scrub(&evidence_rechecked, sizeof(evidence_rechecked));
	scrub(&final_evidence, sizeof(final_evidence));
	if (status != CB_SUCCESS) {
		lock();
		if (owner.state == BACKING_RESERVED) {
			bootmem_reservation_receipt_close(&owner.signer);
			fail_locked();
		}
		unlock();
		bootmem_reservation_receipt_close(&slot->verifier);
		memset(slot, 0, sizeof(*slot));
		slot->state =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_FAILED;
	}
	return status;
}

void payload_mm_authvar_presence_lifecycle_close_mailbox_loader_abort(
	struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority *slot)
{
	if (!slot)
		return;
	lock();
	bootmem_reservation_receipt_close(&owner.signer);
	bootmem_reservation_receipt_close(&slot->verifier);
	if (owner.state == BACKING_RESERVED)
		fail_locked();
	memset(slot, 0, sizeof(*slot));
	slot->state = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_FAILED;
	unlock();
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
