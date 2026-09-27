/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_transaction.h>
#include <commonlib/helpers.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Presence transaction receiver is SMM-only"
#endif

enum transaction_state {
	TRANSACTION_EMPTY, TRANSACTION_PROVISIONING, TRANSACTION_PROVISIONED,
	TRANSACTION_PAGE_CONSUMING, TRANSACTION_PAGE_OWNED,
	TRANSACTION_PREPARING, TRANSACTION_PREPARED,
	TRANSACTION_PREPARED_DISPATCHING, TRANSACTION_FINALIZING,
	TRANSACTION_COMMITTING,
	TRANSACTION_COMMITTED, TRANSACTION_ABORTING,
	TRANSACTION_ABORTED,
	TRANSACTION_POISONED,
};

#if ENV_TEST
void payload_mm_authvar_presence_transaction_test_after_provision_claim(
	const struct payload_mm_authvar_presence_transaction_policy *policy);
void payload_mm_authvar_presence_transaction_test_after_dispatch_owner(
	uint32_t state);
__weak void payload_mm_authvar_presence_transaction_test_after_provision_publish(
	struct payload_mm_authvar_presence_transaction_slot *slot)
{
	(void)slot;
}
void payload_mm_authvar_presence_transaction_test_set_orphan_state(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	unsigned int index)
{
	static const uint32_t states[] = {
		TRANSACTION_PAGE_CONSUMING, TRANSACTION_PAGE_OWNED,
		TRANSACTION_PREPARING, TRANSACTION_PREPARED_DISPATCHING,
		TRANSACTION_FINALIZING, TRANSACTION_COMMITTING,
		TRANSACTION_ABORTING,
	};

	if (index < ARRAY_SIZE(states))
		__atomic_store_n(&slot->state, states[index], __ATOMIC_RELEASE);
}
#endif

__weak struct payload_mm_authvar_presence_transaction_slot *
smm_get_payload_mm_authvar_presence_transaction_slot(void)
{
	return NULL;
}

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;
	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool nonzero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;
	while (size--)
		value |= *bytes++;
	return value != 0;
}

static bool valid_object(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;
	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool overlaps(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t a = (uintptr_t)first, b = (uintptr_t)second;
	if (!valid_object(first, first_size, 1U) ||
	    !valid_object(second, second_size, 1U))
		return true;
	return a <= b ? b - a < first_size : a - b < second_size;
}

static bool callback_protected(payload_mm_authvar_protected_storage proof,
	void *context, const void *callback)
{
	return callback && proof(context, callback, 1U);
}

static bool binding_valid(
	const struct payload_mm_authvar_presence_transaction_binding *binding)
{
	return binding->revision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION &&
		binding->size == sizeof(*binding) && binding->generation &&
		binding->transaction_id && binding->nonce && binding->maximum_cpus &&
		binding->initiator_cpu < binding->maximum_cpus &&
		!binding->reserved[0] && !binding->reserved[1] &&
		nonzero(binding->capability, sizeof(binding->capability));
}

static bool policy_valid(
	const struct payload_mm_authvar_presence_transaction_policy *policy)
{
	return policy->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION &&
		policy->size == sizeof(*policy) && policy->prepare && policy->commit &&
		policy->abort && policy->dma_protected && policy->claim_invocation &&
		policy->complete_invocation && policy->fail_stop &&
		policy->context_size <= PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX &&
		!!policy->context == !!policy->context_size;
}

static bool failure_closure_valid(
	const struct payload_mm_authvar_presence_transaction_failure_closure *failure)
{
	return failure->callback && failure->generation &&
		failure->context_size <=
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX &&
		!failure->reserved[0] && !failure->reserved[1];
}

static void scrub_slot_nonstate(
	struct payload_mm_authvar_presence_transaction_slot *slot)
{
	scrub(slot, offsetof(struct payload_mm_authvar_presence_transaction_slot,
		state));
	__atomic_store_n(&slot->ack_published, 0U, __ATOMIC_RELEASE);
	scrub(slot->reserved, sizeof(slot->reserved));
}

static __noreturn void provisioning_fail_stop(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	payload_mm_authvar_presence_transaction_fail_stop_fn callback,
	void *context)
{
	scrub_slot_nonstate(slot);
	__atomic_store_n(&slot->state, TRANSACTION_POISONED, __ATOMIC_RELEASE);
	callback(context);
	__builtin_trap();
}

static __noreturn void closure_fail_stop(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	uint32_t from,
	payload_mm_authvar_presence_transaction_fail_stop_fn callback,
	void *context)
{
	uint32_t expected = from;

	scrub_slot_nonstate(slot);
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_POISONED, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		callback(context);
	callback(context);
	__builtin_trap();
}

static bool invocation_valid(
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	const struct payload_mm_authvar_presence_transaction_binding *binding)
{
	return invocation->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION &&
		invocation->size == sizeof(*invocation) && invocation->smi_generation &&
		invocation->rendezvous_generation == invocation->smi_generation &&
		nonzero(invocation->rendezvous_proof,
			sizeof(invocation->rendezvous_proof)) &&
		invocation->bsp == 1U && !invocation->reserved &&
		invocation->initiator_cpu == binding->initiator_cpu &&
		invocation->active_cpus == binding->maximum_cpus &&
		invocation->initiator_cpu < invocation->active_cpus;
}

static void make_ack(
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision,
	struct payload_mm_authvar_presence_transaction_ack *ack)
{
	ack->binding = *binding;
	ack->decision = decision;
	ack->transport_status = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED;
	ack->operation_status = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED;
	ack->backing_status =
		decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT ?
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_CLEANED :
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_TRANSFERRED;
}

static enum cb_err complete_invocation(
	const struct payload_mm_authvar_presence_transaction_policy *policy,
	void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision, bool *owned)
{
	const uint64_t value = payload_mm_authvar_presence_transaction_rax(binding,
		decision);
	enum cb_err status;

	if (!*owned || !value)
		return CB_ERR;
	status = policy->complete_invocation(context, invocation, value);
	if (status == CB_SUCCESS)
		*owned = false;
	return status;
}

static void stage_ack(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	struct payload_mm_authvar_presence_transaction_page *page,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision)
{
	__atomic_store_n(&slot->ack_published, 0U, __ATOMIC_RELEASE);
	scrub(page, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
	make_ack(binding, decision, &page->ack);
}

static void publish_ack(
	struct payload_mm_authvar_presence_transaction_slot *slot)
{
	__atomic_thread_fence(__ATOMIC_RELEASE);
	__atomic_store_n(&slot->ack_published, 1U, __ATOMIC_RELEASE);
}

static bool staged_ack_unchanged(
	const struct payload_mm_authvar_presence_transaction_page *page,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision)
{
	return !nonzero(&page->request, sizeof(page->request)) &&
		payload_mm_authvar_presence_transaction_ack_valid(binding, decision,
			&page->ack,
			payload_mm_authvar_presence_transaction_rax(binding, decision)) &&
		!nonzero(page->reserved, sizeof(page->reserved));
}

static bool terminal_snapshot_unchanged(
	const struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t state)
{
	struct payload_mm_authvar_presence_transaction_binding clean = *binding;

	scrub(clean.capability, sizeof(clean.capability));
	return __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == state &&
		__atomic_load_n(&slot->dispatch_owner, __ATOMIC_ACQUIRE) == 1U &&
		!__atomic_load_n(&slot->ack_published, __ATOMIC_ACQUIRE) &&
		!memcmp(&slot->binding, &clean, sizeof(clean)) &&
		!nonzero(&slot->policy, sizeof(slot->policy)) &&
		!nonzero(slot->context, sizeof(slot->context)) &&
		!nonzero(&slot->failure, sizeof(slot->failure)) &&
		!nonzero(&slot->page_verifier, sizeof(slot->page_verifier)) &&
		!nonzero(&slot->page_receipt, sizeof(slot->page_receipt)) &&
		!slot->page_base && !slot->page_size &&
		!slot->reserved[0] && !slot->reserved[1];
}

static void terminal_cleanup(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_binding *binding)
{
	(void)binding;
	scrub(slot->binding.capability, sizeof(slot->binding.capability));
	bootmem_reservation_receipt_close(&slot->page_verifier);
	scrub(&slot->page_verifier, sizeof(slot->page_verifier));
	scrub(&slot->page_receipt, sizeof(slot->page_receipt));
	slot->page_base = 0;
	slot->page_size = 0;
	scrub(&slot->policy, sizeof(slot->policy));
	scrub(slot->context, sizeof(slot->context));
	scrub(&slot->failure, sizeof(slot->failure));
}

enum cb_err payload_mm_authvar_presence_transaction_provision(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_policy *policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *page_verifier,
	struct bootmem_reservation_receipt *page_receipt,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context)
{
	struct payload_mm_authvar_presence_transaction_policy p;
	struct payload_mm_authvar_presence_transaction_binding b;
	struct bootmem_reservation_receipt_authority verifier;
	struct bootmem_reservation_receipt receipt;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX] = { 0 };
	uint32_t expected = TRANSACTION_EMPTY;
	if (!valid_object(slot, sizeof(*slot), _Alignof(*slot)) ||
	    !valid_object(policy, sizeof(*policy), _Alignof(*policy)) ||
	    !valid_object(binding, sizeof(*binding), _Alignof(*binding)) ||
	    !valid_object(page_verifier, sizeof(*page_verifier),
		_Alignof(*page_verifier)) ||
	    !valid_object(page_receipt, sizeof(*page_receipt),
		_Alignof(*page_receipt)) || !storage_is_protected ||
	    overlaps(slot, sizeof(*slot), policy, sizeof(*policy)) ||
	    overlaps(slot, sizeof(*slot), binding, sizeof(*binding)) ||
	    overlaps(slot, sizeof(*slot), page_verifier, sizeof(*page_verifier)) ||
	    overlaps(slot, sizeof(*slot), page_receipt, sizeof(*page_receipt)) ||
	    overlaps(policy, sizeof(*policy), binding, sizeof(*binding)) ||
	    overlaps(policy, sizeof(*policy), page_verifier, sizeof(*page_verifier)) ||
	    overlaps(policy, sizeof(*policy), page_receipt, sizeof(*page_receipt)) ||
	    overlaps(binding, sizeof(*binding), page_verifier,
		sizeof(*page_verifier)) ||
	    overlaps(binding, sizeof(*binding), page_receipt, sizeof(*page_receipt)) ||
	    overlaps(page_verifier, sizeof(*page_verifier), page_receipt,
		sizeof(*page_receipt)) ||
	    slot != smm_get_payload_mm_authvar_presence_transaction_slot())
		return CB_ERR;
	p = *policy;
	b = *binding;
	verifier = *page_verifier;
	receipt = *page_receipt;
	if (!policy_valid(&p) || !binding_valid(&b) ||
	    (p.context_size &&
	     !valid_object(p.context, p.context_size, _Alignof(uint8_t))))
		return CB_ERR;
	if (p.context_size)
		memcpy(context, p.context, p.context_size);
	if (!storage_is_protected(storage_context,
		(const void *)(uintptr_t)storage_is_protected, 1U) ||
	    !storage_is_protected(storage_context, slot, sizeof(*slot)) ||
	    !storage_is_protected(storage_context, policy, sizeof(*policy)) ||
	    !storage_is_protected(storage_context, binding, sizeof(*binding)) ||
	    !storage_is_protected(storage_context, page_verifier,
		sizeof(*page_verifier)) ||
	    !storage_is_protected(storage_context, page_receipt,
		sizeof(*page_receipt)) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)p.prepare) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)p.commit) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)p.abort) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)p.dma_protected) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)p.claim_invocation) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)p.complete_invocation) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)p.fail_stop) ||
	    (p.context_size &&
	     (!storage_is_protected(storage_context, p.context, p.context_size) ||
	      overlaps(slot, sizeof(*slot), p.context, p.context_size) ||
	      overlaps(policy, sizeof(*policy), p.context, p.context_size) ||
	      overlaps(binding, sizeof(*binding), p.context, p.context_size) ||
	      overlaps(page_verifier, sizeof(*page_verifier), p.context,
		p.context_size) ||
	      overlaps(page_receipt, sizeof(*page_receipt), p.context,
		p.context_size))) ||
	    verifier.generation != b.generation ||
	    receipt.generation != b.generation ||
	    receipt.base > UINTPTR_MAX ||
	    receipt.base % PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	    receipt.bytes != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	    receipt.base > UINT64_MAX - receipt.bytes ||
	    overlaps((void *)(uintptr_t)receipt.base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE, slot,
		sizeof(*slot)) ||
	    overlaps((void *)(uintptr_t)receipt.base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE, policy,
		sizeof(*policy)) ||
	    overlaps((void *)(uintptr_t)receipt.base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE, binding,
		sizeof(*binding)) ||
	    overlaps((void *)(uintptr_t)receipt.base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE, page_verifier,
		sizeof(*page_verifier)) ||
	    overlaps((void *)(uintptr_t)receipt.base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE, page_receipt,
		sizeof(*page_receipt)) ||
	    (p.context_size && overlaps((void *)(uintptr_t)receipt.base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE, p.context,
		p.context_size)) ||
	    (p.context_size && memcmp(context, p.context, p.context_size)) ||
	    memcmp(&p, policy, sizeof(p)) || memcmp(&b, binding, sizeof(b)) ||
	    memcmp(&verifier, page_verifier, sizeof(verifier)) ||
	    memcmp(&receipt, page_receipt, sizeof(receipt)))
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_PROVISIONING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return CB_ERR;
#if ENV_TEST
	payload_mm_authvar_presence_transaction_test_after_provision_claim(policy);
#endif
	if ((p.context_size && memcmp(context, p.context, p.context_size)) ||
	    memcmp(&p, policy, sizeof(p)) || memcmp(&b, binding, sizeof(b)) ||
	    memcmp(&verifier, page_verifier, sizeof(verifier)) ||
	    memcmp(&receipt, page_receipt, sizeof(receipt)))
		provisioning_fail_stop(slot, p.fail_stop,
			p.context_size ? context : NULL);
	slot->policy = p;
	scrub(slot->context, sizeof(slot->context));
	if (p.context_size)
		memcpy(slot->context, context, p.context_size);
	slot->policy.context = p.context_size ? slot->context : NULL;
	slot->failure.callback = p.fail_stop;
	slot->failure.context_size = p.context_size;
	slot->failure.generation = b.generation;
	scrub(slot->failure.context, sizeof(slot->failure.context));
	if (p.context_size)
		memcpy(slot->failure.context, context, p.context_size);
	scrub(slot->failure.reserved, sizeof(slot->failure.reserved));
	slot->binding = b;
	slot->page_verifier = verifier;
	slot->page_receipt = receipt;
	bootmem_reservation_receipt_close(page_verifier);
	scrub(page_receipt, sizeof(*page_receipt));
	slot->page_base = slot->page_size = 0;
	__atomic_store_n(&slot->ack_published, 0U, __ATOMIC_RELAXED);
	if (__atomic_load_n(&slot->dispatch_owner, __ATOMIC_ACQUIRE) != 0U)
		provisioning_fail_stop(slot, p.fail_stop,
			p.context_size ? context : NULL);
	scrub(slot->reserved, sizeof(slot->reserved));
	expected = TRANSACTION_PROVISIONING;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_PROVISIONED, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		if (p.context_size)
			memcpy(context, slot->context, p.context_size);
		provisioning_fail_stop(slot, p.fail_stop,
			p.context_size ? context : NULL);
	}
#if ENV_TEST
	payload_mm_authvar_presence_transaction_test_after_provision_publish(slot);
#endif
	scrub(context, sizeof(context));
	return CB_SUCCESS;
}

static enum cb_err claim_invocation(
	const struct payload_mm_authvar_presence_transaction_policy *policy,
	void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_invocation *invocation,
	bool *owned)
{
	memset(invocation, 0, sizeof(*invocation));
	if (policy->claim_invocation(context,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL,
		invocation) != CB_SUCCESS)
		return CB_ERR;
	*owned = true;
	return invocation_valid(invocation, binding) ? CB_SUCCESS : CB_ERR;
}

static __noreturn void dispatch_fail_stop(
	payload_mm_authvar_presence_transaction_fail_stop_fn callback,
	void *failure_context)
{
	callback(failure_context);
	__builtin_trap();
}

static bool dispatch_snapshot_unchanged(
	const struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_policy *policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	const struct payload_mm_authvar_presence_transaction_failure_closure *failure,
	const struct bootmem_reservation_receipt_authority *page_verifier,
	const struct bootmem_reservation_receipt *page_receipt,
	const uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX],
	uint64_t page_base, uint64_t page_size, uint32_t ack_published,
	uint32_t state)
{
	return __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) == state &&
		__atomic_load_n(&slot->dispatch_owner, __ATOMIC_ACQUIRE) == 1U &&
		!memcmp(&slot->policy, policy, sizeof(*policy)) &&
		!memcmp(&slot->binding, binding, sizeof(*binding)) &&
		!memcmp(&slot->failure, failure, sizeof(*failure)) &&
		!memcmp(&slot->page_verifier, page_verifier,
			sizeof(*page_verifier)) &&
		!memcmp(&slot->page_receipt, page_receipt,
			sizeof(*page_receipt)) &&
		(context ? !memcmp(slot->context, context, sizeof(slot->context)) :
		 !nonzero(slot->context, sizeof(slot->context))) &&
		slot->page_base == page_base && slot->page_size == page_size &&
		__atomic_load_n(&slot->ack_published, __ATOMIC_ACQUIRE) ==
			ack_published &&
		!slot->reserved[0] && !slot->reserved[1];
}

static __noreturn void terminal_fail_stop(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_policy *policy,
	payload_mm_authvar_presence_transaction_fail_stop_fn failure_callback,
	void *working_context,
	void *failure_context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_page *page,
	uint32_t from, bool abort_authority)
{
	uint32_t expected = from;

	if (__atomic_load_n(&slot->dispatch_owner, __ATOMIC_ACQUIRE) != 1U)
		dispatch_fail_stop(failure_callback, failure_context);
	if (page)
		scrub(page, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_ABORTING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		scrub_slot_nonstate(slot);
		dispatch_fail_stop(failure_callback, failure_context);
	}
	if (abort_authority) {
		if (policy->context_size)
			memcpy(working_context, failure_context, policy->context_size);
		(void)policy->abort(working_context,
			binding->generation);
		if (__atomic_load_n(&slot->dispatch_owner, __ATOMIC_ACQUIRE) != 1U)
			dispatch_fail_stop(failure_callback, failure_context);
	}
	terminal_cleanup(slot, binding);
	expected = TRANSACTION_ABORTING;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_POISONED, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		dispatch_fail_stop(failure_callback, failure_context);
	failure_callback(failure_context);
	__builtin_trap();
}

static __always_inline enum cb_err terminal_abort(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_policy *policy,
	payload_mm_authvar_presence_transaction_fail_stop_fn failure_callback,
	void *working_context,
	void *failure_context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	const struct payload_mm_authvar_presence_transaction_failure_closure *failure,
	const struct bootmem_reservation_receipt_authority *page_verifier,
	const struct bootmem_reservation_receipt *page_receipt,
	struct payload_mm_authvar_presence_transaction_page *page,
	const struct payload_mm_authvar_presence_transaction_request *request,
	uint64_t page_base, uint64_t page_size,
	struct payload_mm_authvar_presence_transaction_invocation *invocation,
	bool *invocation_owned, uint32_t from, bool abort_authority)
{
	uint32_t expected = from;
	struct payload_mm_authvar_presence_transaction_invocation before;
	enum cb_err status;

	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_ABORTING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		scrub(page, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
		scrub_slot_nonstate(slot);
		dispatch_fail_stop(failure_callback, failure_context);
	}
	__atomic_store_n(&slot->ack_published, 0U, __ATOMIC_RELEASE);
	if (abort_authority && policy->abort(working_context,
		binding->generation) != CB_SUCCESS)
		terminal_fail_stop(slot, policy, failure_callback, working_context,
			failure_context, binding, page, TRANSACTION_ABORTING, false);
	if (!dispatch_snapshot_unchanged(slot, policy, binding, failure,
		page_verifier, page_receipt, failure_context,
		page_base, page_size, 0U, TRANSACTION_ABORTING) ||
	    (policy->context_size &&
	     memcmp(working_context, failure_context, policy->context_size)) ||
	    memcmp(request, &page->request, sizeof(*request)))
		terminal_fail_stop(slot, policy, failure_callback, working_context,
			failure_context, binding, page, TRANSACTION_ABORTING, false);
	stage_ack(slot, page, binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	terminal_cleanup(slot, binding);
	expected = TRANSACTION_ABORTING;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_ABORTED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		dispatch_fail_stop(failure_callback, failure_context);
	if (!staged_ack_unchanged(page, binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT))
		dispatch_fail_stop(failure_callback, failure_context);
	before = *invocation;
	status = complete_invocation(policy, working_context, invocation, binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, invocation_owned);
	if (__atomic_load_n(&slot->dispatch_owner, __ATOMIC_ACQUIRE) != 1U)
		dispatch_fail_stop(failure_callback, failure_context);
	if (!terminal_snapshot_unchanged(slot, binding, TRANSACTION_ABORTED) ||
	    memcmp(&before, invocation, sizeof(before)) ||
	    (policy->context_size &&
	     memcmp(working_context, failure_context, policy->context_size)))
		dispatch_fail_stop(failure_callback, failure_context);
	if (status != CB_SUCCESS)
		terminal_fail_stop(slot, policy, failure_callback, working_context,
			failure_context, binding, page,
			TRANSACTION_ABORTED, false);
	publish_ack(slot);
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_transaction_dispatch(
	struct payload_mm_authvar_presence_transaction_slot *slot)
{
	struct payload_mm_authvar_presence_transaction_invocation invocation = { 0 };
	struct payload_mm_authvar_presence_transaction_invocation before;
	struct payload_mm_authvar_presence_transaction_request request = { 0 };
	struct payload_mm_authvar_presence_transaction_binding binding = { 0 };
	struct payload_mm_authvar_presence_transaction_policy policy = { 0 };
	struct payload_mm_authvar_presence_transaction_failure_closure failure = { 0 };
	struct bootmem_reservation_receipt_authority page_verifier = { 0 };
	struct bootmem_reservation_receipt page_receipt = { 0 };
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX] = { 0 };
	payload_mm_authvar_presence_transaction_fail_stop_fn failure_callback = NULL;
	struct payload_mm_authvar_presence_transaction_page *page = NULL;
	uint32_t dispatch_state = TRANSACTION_EMPTY, state, expected, owner = 0;
	uint64_t verified_base = 0, verified_size = 0;
	uint32_t expected_ack = 0;
	bool invocation_owned = false;
	bool authority_owned = false;
	bool prepare_called = false;
	bool dispatch_owned = false;
	void *working_context = NULL;
	void *fail_context = NULL;
	enum cb_err status = CB_ERR;

	if (!valid_object(slot, sizeof(*slot), _Alignof(*slot)) ||
	    slot != smm_get_payload_mm_authvar_presence_transaction_slot())
		return CB_ERR;
	state = __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
	if (state == TRANSACTION_EMPTY || state == TRANSACTION_PROVISIONING ||
	    state == TRANSACTION_COMMITTED || state == TRANSACTION_ABORTED ||
	    state == TRANSACTION_POISONED)
		return CB_ERR;
	if (
	    !__atomic_compare_exchange_n(&slot->dispatch_owner, &owner, 1U,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	dispatch_owned = true;
#if ENV_TEST
	payload_mm_authvar_presence_transaction_test_after_dispatch_owner(state);
#endif
	state = __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
	if (state == TRANSACTION_EMPTY || state == TRANSACTION_PROVISIONING ||
	    state == TRANSACTION_COMMITTED || state == TRANSACTION_ABORTED ||
	    state == TRANSACTION_POISONED)
		goto out;
	failure = slot->failure;
	if (!failure_closure_valid(&failure) ||
	    memcmp(&failure, &slot->failure, sizeof(failure)))
		__builtin_trap();
	failure_callback = failure.callback;
	fail_context = failure.context_size ? failure.context : NULL;
	if (state != TRANSACTION_PROVISIONED && state != TRANSACTION_PREPARED)
		closure_fail_stop(slot, state, failure_callback, fail_context);
	policy = slot->policy;
	binding = slot->binding;
	page_verifier = slot->page_verifier;
	page_receipt = slot->page_receipt;
	verified_base = slot->page_base;
	verified_size = slot->page_size;
	memcpy(context, slot->context, sizeof(context));
	if (!policy_valid(&policy) || policy.fail_stop != failure_callback ||
	    policy.context_size != failure.context_size ||
	    policy.context != (policy.context_size ? slot->context : NULL) ||
	    memcmp(context, failure.context, sizeof(context)))
		closure_fail_stop(slot, state, failure_callback, fail_context);
	working_context = policy.context_size ? context : NULL;
	if (!binding_valid(&binding) ||
	    failure.generation != binding.generation ||
	    !dispatch_snapshot_unchanged(slot, &policy, &binding, &failure,
		&page_verifier, &page_receipt, failure.context,
		verified_base, verified_size,
		state == TRANSACTION_PREPARED ? 1U : 0U, state))
		closure_fail_stop(slot, state, failure_callback, fail_context);
	authority_owned = state == TRANSACTION_PREPARED;
	expected_ack = authority_owned ? 1U : 0U;
	if (authority_owned) {
		if (nonzero(&page_verifier, sizeof(page_verifier)) ||
		    nonzero(&page_receipt, sizeof(page_receipt)))
			closure_fail_stop(slot, state, failure_callback, fail_context);
	} else if (page_verifier.generation != binding.generation ||
		   page_receipt.generation != binding.generation ||
		   page_receipt.base > UINTPTR_MAX ||
		   page_receipt.base %
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
		   page_receipt.bytes !=
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
		   page_receipt.base > UINT64_MAX - page_receipt.bytes) {
		closure_fail_stop(slot, state, failure_callback, fail_context);
	}
	if ((authority_owned &&
	     (verified_base > UINTPTR_MAX ||
	      verified_base % PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	      verified_size != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	      verified_base > UINT64_MAX - verified_size)) ||
	    (!authority_owned && (verified_base || verified_size)))
		closure_fail_stop(slot, state, failure_callback, fail_context);
	dispatch_state = authority_owned ? TRANSACTION_PREPARED_DISPATCHING :
		TRANSACTION_PAGE_CONSUMING;
	expected = state;
	if (!__atomic_compare_exchange_n(&slot->state, &expected, dispatch_state,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		dispatch_fail_stop(failure_callback, fail_context);
	if (claim_invocation(&policy, working_context, &binding, &invocation,
		&invocation_owned) != CB_SUCCESS ||
	    memcmp(context, failure.context, policy.context_size) ||
	    !dispatch_snapshot_unchanged(slot, &policy, &binding, &failure,
		&page_verifier, &page_receipt, failure.context,
		verified_base, verified_size, expected_ack, dispatch_state))
		terminal_fail_stop(slot, &policy, failure_callback, working_context,
			fail_context, &binding, NULL, dispatch_state,
			authority_owned);

	if (state == TRANSACTION_PROVISIONED) {
		verified_base = page_receipt.base;
		verified_size = page_receipt.bytes;
		status = bootmem_reservation_receipt_verify_consume_exact_tag(
			&slot->page_verifier, &page_receipt, BM_MEM_RESERVED);
		scrub(&slot->page_verifier, sizeof(slot->page_verifier));
		scrub(&slot->page_receipt, sizeof(slot->page_receipt));
		scrub(&page_verifier, sizeof(page_verifier));
		scrub(&page_receipt, sizeof(page_receipt));
		if (status != CB_SUCCESS)
			terminal_fail_stop(slot, &policy, failure_callback,
				working_context, fail_context, &binding, NULL,
				TRANSACTION_PAGE_CONSUMING, false);
		slot->page_base = verified_base;
		slot->page_size = verified_size;
		expected = TRANSACTION_PAGE_CONSUMING;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_PAGE_OWNED, false, __ATOMIC_RELEASE,
			__ATOMIC_ACQUIRE))
			terminal_fail_stop(slot, &policy, failure_callback,
				working_context, fail_context, &binding, NULL,
				TRANSACTION_PAGE_CONSUMING, false);
		dispatch_state = TRANSACTION_PAGE_OWNED;
	}
	if (!policy.dma_protected(working_context, verified_base, verified_size) ||
	    memcmp(context, failure.context, policy.context_size) ||
	    !dispatch_snapshot_unchanged(slot, &policy, &binding, &failure,
		&page_verifier, &page_receipt, failure.context,
		verified_base, verified_size, expected_ack, dispatch_state))
		terminal_fail_stop(slot, &policy, failure_callback, working_context,
			fail_context, &binding, NULL, dispatch_state,
			authority_owned);
	page = (void *)(uintptr_t)verified_base;
	request = page->request;
	if (request.transport_status || request.operation_status || request.reserved ||
	    !binding_valid(&request.binding) ||
	    memcmp(&request.binding, &binding, sizeof(binding)))
		goto malformed;
	if (request.decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE) {
		expected = TRANSACTION_PAGE_OWNED;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_PREPARING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			terminal_fail_stop(slot, &policy, failure_callback,
				working_context, fail_context, &binding, page,
				TRANSACTION_PAGE_OWNED, false);
		status = CB_ERR;
		if (request.seed.endpoint.generation == binding.generation) {
			prepare_called = true;
			status = policy.prepare(working_context, &request.seed,
				binding.generation);
		}
		if (!dispatch_snapshot_unchanged(slot, &policy, &binding, &failure,
			&page_verifier, &page_receipt, failure.context,
			verified_base, verified_size,
			expected_ack, TRANSACTION_PREPARING) ||
		    memcmp(context, failure.context, policy.context_size) ||
		    memcmp(&request, &page->request, sizeof(request)))
			terminal_fail_stop(slot, &policy, failure_callback,
				working_context, fail_context, &binding, page,
				TRANSACTION_PREPARING, prepare_called);
		if (status != CB_SUCCESS) {
			status = terminal_abort(slot, &policy, failure_callback,
				working_context, fail_context, &binding, &failure,
				&page_verifier, &page_receipt, page, &request,
				verified_base, verified_size, &invocation,
				&invocation_owned,
				TRANSACTION_PREPARING, prepare_called);
			goto out;
		}
		stage_ack(slot, page, &binding, request.decision);
		if (!staged_ack_unchanged(page, &binding, request.decision))
			dispatch_fail_stop(failure_callback, fail_context);
		before = invocation;
		status = complete_invocation(&policy, working_context, &invocation,
			&binding, request.decision, &invocation_owned);
		if (__atomic_load_n(&slot->dispatch_owner, __ATOMIC_ACQUIRE) != 1U)
			dispatch_fail_stop(failure_callback, fail_context);
		if (!dispatch_snapshot_unchanged(slot, &policy, &binding, &failure,
			&page_verifier, &page_receipt, failure.context,
			verified_base, verified_size, expected_ack,
			TRANSACTION_PREPARING) ||
		    memcmp(&before, &invocation, sizeof(before)) ||
		    (policy.context_size &&
		     memcmp(working_context, fail_context, policy.context_size)))
			dispatch_fail_stop(failure_callback, fail_context);
		if (status != CB_SUCCESS)
			terminal_fail_stop(slot, &policy, failure_callback,
				working_context, fail_context, &binding, page,
				TRANSACTION_PREPARING, true);
		expected = TRANSACTION_PREPARING;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_PREPARED, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			dispatch_fail_stop(failure_callback, fail_context);
		publish_ack(slot);
		scrub(&request, sizeof(request));
		status = CB_SUCCESS;
		goto out;
	}
	if (nonzero(&request.seed, sizeof(request.seed)))
		goto malformed;
	if (request.decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT) {
		__atomic_store_n(&slot->ack_published, 0U, __ATOMIC_RELEASE);
		expected_ack = 0U;
		expected = TRANSACTION_PREPARED_DISPATCHING;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_FINALIZING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			terminal_fail_stop(slot, &policy, failure_callback,
				working_context, fail_context, &binding, page,
				TRANSACTION_PREPARED_DISPATCHING, true);
		status = policy.commit(working_context, binding.generation);
		if (status != CB_SUCCESS ||
		    !dispatch_snapshot_unchanged(slot, &policy, &binding, &failure,
			&page_verifier, &page_receipt, failure.context,
			verified_base, verified_size,
			expected_ack, TRANSACTION_FINALIZING) ||
		    memcmp(context, failure.context, policy.context_size) ||
		    memcmp(&request, &page->request, sizeof(request)))
			terminal_fail_stop(slot, &policy, failure_callback,
				working_context, fail_context, &binding, page,
				TRANSACTION_FINALIZING, false);
		expected = TRANSACTION_FINALIZING;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_COMMITTING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			dispatch_fail_stop(failure_callback, fail_context);
		stage_ack(slot, page, &binding, request.decision);
		terminal_cleanup(slot, &binding);
		expected = TRANSACTION_COMMITTING;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_COMMITTED, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			dispatch_fail_stop(failure_callback, fail_context);
		if (!staged_ack_unchanged(page, &binding, request.decision))
			dispatch_fail_stop(failure_callback, fail_context);
		before = invocation;
		status = complete_invocation(&policy, working_context, &invocation,
			&binding, request.decision, &invocation_owned);
		if (__atomic_load_n(&slot->dispatch_owner, __ATOMIC_ACQUIRE) != 1U)
			dispatch_fail_stop(failure_callback, fail_context);
		if (!terminal_snapshot_unchanged(slot, &binding,
			TRANSACTION_COMMITTED) ||
		    memcmp(&before, &invocation, sizeof(before)) ||
		    (policy.context_size &&
		     memcmp(working_context, fail_context, policy.context_size)))
			dispatch_fail_stop(failure_callback, fail_context);
		if (status != CB_SUCCESS)
			terminal_fail_stop(slot, &policy, failure_callback,
				working_context, fail_context, &binding, page,
				TRANSACTION_COMMITTED, false);
		publish_ack(slot);
		scrub(&request, sizeof(request));
		status = CB_SUCCESS;
		goto out;
	}
	if (request.decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT) {
		status = terminal_abort(slot, &policy, failure_callback,
			working_context, fail_context, &binding, &failure,
			&page_verifier, &page_receipt, page, &request,
			verified_base, verified_size,
			&invocation, &invocation_owned,
			TRANSACTION_PREPARED_DISPATCHING, true);
		scrub(&request, sizeof(request));
		goto out;
	}
malformed:
	status = terminal_abort(slot, &policy, failure_callback, working_context,
		fail_context, &binding, &failure, &page_verifier, &page_receipt,
		page, &request, verified_base,
		verified_size, &invocation, &invocation_owned,
		dispatch_state,
		authority_owned);
out:
	if (invocation_owned)
		terminal_fail_stop(slot, &policy, failure_callback, working_context,
			fail_context, &binding, page, dispatch_state,
			authority_owned);
	scrub(&request, sizeof(request));
	scrub(&binding, sizeof(binding));
	scrub(&invocation, sizeof(invocation));
	scrub(&policy, sizeof(policy));
	scrub(context, sizeof(context));
	scrub(&failure, sizeof(failure));
	if (dispatch_owned)
		__atomic_store_n(&slot->dispatch_owner, 0U, __ATOMIC_RELEASE);
	return status;
}

bool payload_mm_authvar_presence_transaction_dispatch_enabled(
	const struct payload_mm_authvar_presence_transaction_slot *slot,
	uint64_t generation)
{
	if (!slot || slot != smm_get_payload_mm_authvar_presence_transaction_slot() ||
	    __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
	    TRANSACTION_COMMITTED)
		return false;
	return generation && slot->binding.generation == generation &&
		__atomic_load_n(&slot->ack_published, __ATOMIC_ACQUIRE);
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION)
bool payload_mm_authvar_presence_transaction_dispatch_ack_valid(
	const struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision,
	const struct payload_mm_authvar_presence_transaction_page *page)
{
	struct payload_mm_authvar_presence_transaction_slot slot_snapshot;
	struct payload_mm_authvar_presence_transaction_request request_snapshot;
	struct payload_mm_authvar_presence_transaction_ack ack_snapshot;
	struct payload_mm_authvar_presence_transaction_binding clean;
	uint32_t expected_state;
	bool valid;

	if (!valid_object(slot, sizeof(*slot), _Alignof(*slot)) ||
	    slot != smm_get_payload_mm_authvar_presence_transaction_slot() ||
	    !valid_object(binding, sizeof(*binding), _Alignof(*binding)) ||
	    !binding_valid(binding) ||
	    !valid_object(page, sizeof(*page), _Alignof(*page)) ||
	    overlaps(slot, sizeof(*slot), binding, sizeof(*binding)) ||
	    overlaps(slot, sizeof(*slot), page, sizeof(*page)) ||
	    overlaps(binding, sizeof(*binding), page, sizeof(*page)))
		return false;
	if (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE)
		expected_state = TRANSACTION_PREPARED;
	else if (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT)
		expected_state = TRANSACTION_COMMITTED;
	else if (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT)
		expected_state = TRANSACTION_ABORTED;
	else
		return false;
	if (__atomic_load_n(&slot->ack_published, __ATOMIC_ACQUIRE) != 1U)
		return false;
	slot_snapshot = *slot;
	request_snapshot = page->request;
	ack_snapshot = page->ack;
	clean = *binding;
	if (decision != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE)
		scrub(clean.capability, sizeof(clean.capability));
	valid = slot_snapshot.state == expected_state &&
		!slot_snapshot.dispatch_owner && slot_snapshot.ack_published == 1U &&
		!memcmp(&slot_snapshot.binding, &clean, sizeof(clean)) &&
		!nonzero(&request_snapshot, sizeof(request_snapshot)) &&
		!nonzero(page->reserved, sizeof(page->reserved)) &&
		payload_mm_authvar_presence_transaction_ack_valid(binding, decision,
			&ack_snapshot,
			payload_mm_authvar_presence_transaction_rax(binding, decision));
	if (decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE)
		valid = valid && policy_valid(&slot_snapshot.policy) &&
			failure_closure_valid(&slot_snapshot.failure) &&
			slot_snapshot.failure.generation == binding->generation &&
			slot_snapshot.policy.fail_stop ==
				slot_snapshot.failure.callback &&
			slot_snapshot.policy.context_size ==
				slot_snapshot.failure.context_size &&
			slot_snapshot.policy.context ==
				(slot_snapshot.policy.context_size ?
				 (void *)slot->context : NULL) &&
			!memcmp(slot_snapshot.context,
				slot_snapshot.failure.context,
				sizeof(slot_snapshot.context)) &&
			!nonzero(&slot_snapshot.page_verifier,
				sizeof(slot_snapshot.page_verifier)) &&
			!nonzero(&slot_snapshot.page_receipt,
				sizeof(slot_snapshot.page_receipt)) &&
			slot_snapshot.page_base == (uintptr_t)page &&
			slot_snapshot.page_size == sizeof(*page);
	else
		valid = valid && !nonzero(&slot_snapshot.policy,
			sizeof(slot_snapshot.policy)) &&
			!nonzero(slot_snapshot.context,
				sizeof(slot_snapshot.context)) &&
			!nonzero(&slot_snapshot.failure,
				sizeof(slot_snapshot.failure)) &&
			!nonzero(&slot_snapshot.page_verifier,
				sizeof(slot_snapshot.page_verifier)) &&
			!nonzero(&slot_snapshot.page_receipt,
				sizeof(slot_snapshot.page_receipt)) &&
			!slot_snapshot.page_base && !slot_snapshot.page_size;
	valid = valid && !slot_snapshot.reserved[0] &&
		!slot_snapshot.reserved[1];
	valid = valid && !memcmp(&slot_snapshot, slot, sizeof(slot_snapshot)) &&
		!memcmp(&request_snapshot, &page->request,
			sizeof(request_snapshot)) &&
		!memcmp(&ack_snapshot, &page->ack, sizeof(ack_snapshot)) &&
		!nonzero(page->reserved, sizeof(page->reserved)) &&
		__atomic_load_n(&slot->ack_published, __ATOMIC_ACQUIRE) == 1U;
	scrub(&slot_snapshot, sizeof(slot_snapshot));
	scrub(&request_snapshot, sizeof(request_snapshot));
	scrub(&ack_snapshot, sizeof(ack_snapshot));
	scrub(&clean, sizeof(clean));
	return valid;
}
#endif
