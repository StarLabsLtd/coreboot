/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_transaction.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Presence transaction receiver is SMM-only"
#endif

enum transaction_state {
	TRANSACTION_EMPTY, TRANSACTION_PROVISIONING, TRANSACTION_PROVISIONED,
	TRANSACTION_PREPARING, TRANSACTION_PREPARED, TRANSACTION_FINALIZING,
	TRANSACTION_COMMITTING, TRANSACTION_COMMITTED, TRANSACTION_ABORTING,
	TRANSACTION_ABORTED, TRANSACTION_POISONED,
};

#if ENV_TEST
void payload_mm_authvar_presence_transaction_test_after_terminal_release(
	const struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_page *page);
void payload_mm_authvar_presence_transaction_test_after_provision_claim(
	const struct payload_mm_authvar_presence_transaction_policy *policy);
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
		policy->publish_and_verify_rax && policy->fail_stop &&
		policy->context_size <= PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX &&
		!!policy->context == !!policy->context_size;
}

static __noreturn void fail_stop(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	uint32_t owned_state)
{
	uint32_t expected = owned_state;
	void (*callback)(void *) = slot->policy.fail_stop;
	void *context = slot->policy.context;
	(void)__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_POISONED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	callback(context);
	__builtin_trap();
}

static __noreturn void provisioning_fail_stop(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	payload_mm_authvar_presence_transaction_fail_stop_fn callback,
	void *context)
{
	scrub(slot, offsetof(struct payload_mm_authvar_presence_transaction_slot,
		state));
	scrub((uint8_t *)slot +
		offsetof(struct payload_mm_authvar_presence_transaction_slot,
			ack_published),
		sizeof(*slot) -
		offsetof(struct payload_mm_authvar_presence_transaction_slot,
			ack_published));
	__atomic_store_n(&slot->state, TRANSACTION_POISONED, __ATOMIC_RELEASE);
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
	*ack = (struct payload_mm_authvar_presence_transaction_ack) {
		.binding = *binding, .decision = decision,
		.transport_status = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED,
		.operation_status = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED,
	};
}

static enum cb_err stage_rax(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision)
{
	const uint64_t rax = payload_mm_authvar_presence_transaction_rax(binding,
		decision);
	if (!rax)
		return CB_ERR;
	if (slot->policy.publish_and_verify_rax(slot->policy.context, invocation,
		rax) != CB_SUCCESS)
		return CB_ERR;
	return CB_SUCCESS;
}

static void expose_ack(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	struct payload_mm_authvar_presence_transaction_page *page,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision)
{
	struct payload_mm_authvar_presence_transaction_ack ack;
	make_ack(binding, decision, &ack);
	__atomic_store_n(&slot->ack_published, 0U, __ATOMIC_RELEASE);
	scrub(page, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
	page->ack = ack;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	__atomic_store_n(&slot->ack_published, 1U, __ATOMIC_RELEASE);
	scrub(&ack, sizeof(ack));
}

static void terminal_cleanup(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_binding *binding)
{
	(void)binding;
	scrub(slot->binding.capability, sizeof(slot->binding.capability));
	bootmem_reservation_receipt_close(&slot->page_verifier);
	scrub(&slot->page_receipt, sizeof(slot->page_receipt));
	slot->page_base = 0;
	slot->page_size = 0;
	scrub(&slot->policy, sizeof(slot->policy));
	scrub(slot->context, sizeof(slot->context));
}

static void terminal_publish(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	struct payload_mm_authvar_presence_transaction_page *page,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision, uint32_t terminal_state)
{
	struct payload_mm_authvar_presence_transaction_ack ack;
	make_ack(binding, decision, &ack);
	__atomic_store_n(&slot->ack_published, 0U, __ATOMIC_RELEASE);
	scrub(page, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
	terminal_cleanup(slot, binding);
	__atomic_store_n(&slot->state, terminal_state, __ATOMIC_RELEASE);
#if ENV_TEST
	payload_mm_authvar_presence_transaction_test_after_terminal_release(slot,
		page);
#endif
	page->ack = ack;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	__atomic_store_n(&slot->ack_published, 1U, __ATOMIC_RELEASE);
	scrub(&ack, sizeof(ack));
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
	    !storage_is_protected(storage_context, slot, sizeof(*slot)) ||
	    !storage_is_protected(storage_context, policy, sizeof(*policy)) ||
	    !storage_is_protected(storage_context, binding, sizeof(*binding)) ||
	    !storage_is_protected(storage_context, page_verifier,
		sizeof(*page_verifier)) ||
	    slot != smm_get_payload_mm_authvar_presence_transaction_slot() ||
	    !storage_is_protected(storage_context, page_receipt,
		sizeof(*page_receipt)))
		return CB_ERR;
	p = *policy;
	b = *binding;
	verifier = *page_verifier;
	receipt = *page_receipt;
	if (!policy_valid(&p) || !binding_valid(&b) ||
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
	    overlaps((void *)(uintptr_t)receipt.base, receipt.bytes, slot,
		sizeof(*slot)) ||
	    overlaps((void *)(uintptr_t)receipt.base, receipt.bytes, policy,
		sizeof(*policy)) ||
	    overlaps((void *)(uintptr_t)receipt.base, receipt.bytes, binding,
		sizeof(*binding)) ||
	    overlaps((void *)(uintptr_t)receipt.base, receipt.bytes, page_verifier,
		sizeof(*page_verifier)) ||
	    overlaps((void *)(uintptr_t)receipt.base, receipt.bytes, page_receipt,
		sizeof(*page_receipt)) ||
	    (p.context_size && overlaps((void *)(uintptr_t)receipt.base,
		receipt.bytes, p.context, p.context_size)) ||
	    memcmp(&p, policy, sizeof(p)) || memcmp(&b, binding, sizeof(b)) ||
	    memcmp(&verifier, page_verifier, sizeof(verifier)) ||
	    memcmp(&receipt, page_receipt, sizeof(receipt)))
		return CB_ERR;
	if (p.context_size) {
		memcpy(context, p.context, p.context_size);
		if (memcmp(context, p.context, p.context_size))
			return CB_ERR;
	}
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
	slot->binding = b;
	slot->page_verifier = verifier;
	slot->page_receipt = receipt;
	bootmem_reservation_receipt_close(page_verifier);
	scrub(page_receipt, sizeof(*page_receipt));
	scrub(context, sizeof(context));
	slot->page_base = slot->page_size = 0;
	__atomic_store_n(&slot->ack_published, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&slot->dispatch_owner, 0U, __ATOMIC_RELAXED);
	scrub(slot->reserved, sizeof(slot->reserved));
	expected = TRANSACTION_PROVISIONING;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_PROVISIONED, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		fail_stop(slot, TRANSACTION_PROVISIONING);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static bool claim_invocation(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	memset(invocation, 0, sizeof(*invocation));
	return slot->policy.claim_invocation(slot->policy.context,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL, invocation) ==
		CB_SUCCESS && invocation_valid(invocation, &slot->binding);
}

static enum cb_err abort_prepared(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t from)
{
	uint32_t expected = from;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		TRANSACTION_ABORTING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (slot->policy.abort(slot->policy.context, binding->generation) !=
	    CB_SUCCESS) {
		fail_stop(slot, TRANSACTION_ABORTING);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_transaction_dispatch(
	struct payload_mm_authvar_presence_transaction_slot *slot)
{
	struct payload_mm_authvar_presence_transaction_invocation invocation;
	struct payload_mm_authvar_presence_transaction_request request;
	struct payload_mm_authvar_presence_transaction_binding binding;
	struct bootmem_reservation_receipt receipt;
	struct payload_mm_authvar_presence_transaction_page *page = NULL;
	uint32_t state, expected, owner = 0;
	uint64_t verified_base, verified_size;
	enum cb_err status = CB_ERR;
	if (!valid_object(slot, sizeof(*slot), _Alignof(*slot)) ||
	    slot != smm_get_payload_mm_authvar_presence_transaction_slot())
		return CB_ERR;
	state = __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
	if (state == TRANSACTION_COMMITTED || state == TRANSACTION_ABORTED ||
	    state == TRANSACTION_POISONED)
		return CB_ERR;
	if (
	    !__atomic_compare_exchange_n(&slot->dispatch_owner, &owner, 1U,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	state = __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE);
	if ((state != TRANSACTION_PROVISIONED && state != TRANSACTION_PREPARED) ||
	    !policy_valid(&slot->policy) || !claim_invocation(slot, &invocation))
		goto out;
	if (state == TRANSACTION_PROVISIONED) {
		receipt = slot->page_receipt;
		verified_base = receipt.base;
		verified_size = receipt.bytes;
		if (receipt.base != slot->page_receipt.base ||
		    receipt.bytes != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
		    bootmem_reservation_receipt_verify_consume_exact_tag(
			&slot->page_verifier, &receipt, BM_MEM_RESERVED) != CB_SUCCESS)
			goto out;
		slot->page_base = verified_base;
		slot->page_size = verified_size;
	} else if (state < TRANSACTION_PREPARED || state > TRANSACTION_ABORTING) {
		goto out;
	}
	if (!slot->policy.dma_protected(slot->policy.context, slot->page_base,
		slot->page_size))
		goto out;
	page = (void *)(uintptr_t)slot->page_base;
	request = page->request;
	binding = slot->binding;
	if (request.transport_status || request.operation_status || request.reserved ||
	    !binding_valid(&request.binding) ||
	    memcmp(&request.binding, &binding, sizeof(binding)))
		goto malformed;
	if (request.decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE) {
		expected = TRANSACTION_PROVISIONED;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_PREPARING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			goto malformed;
		status = request.seed.endpoint.generation == binding.generation ?
			slot->policy.prepare(slot->policy.context, &request.seed,
				binding.generation) : CB_ERR;
		if (status != CB_SUCCESS ||
		    memcmp(&request, &page->request, sizeof(request))) {
			if (abort_prepared(slot, &binding, TRANSACTION_PREPARING) !=
			    CB_SUCCESS ||
			    stage_rax(slot, &invocation, &binding,
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT) !=
			    CB_SUCCESS)
				fail_stop(slot, TRANSACTION_ABORTING);
			terminal_publish(slot, page, &binding,
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT,
				TRANSACTION_ABORTED);
			goto out;
		}
		expected = TRANSACTION_PREPARING;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_PREPARED, false, __ATOMIC_RELEASE,
			__ATOMIC_ACQUIRE))
			fail_stop(slot, TRANSACTION_PREPARING);
		if (stage_rax(slot, &invocation, &binding, request.decision) !=
		    CB_SUCCESS) {
			fail_stop(slot, TRANSACTION_PREPARED);
		}
		expose_ack(slot, page, &binding, request.decision);
		scrub(&request, sizeof(request));
		scrub(&binding, sizeof(binding));
		status = CB_SUCCESS;
		goto out;
	}
	if (nonzero(&request.seed, sizeof(request.seed)))
		goto malformed;
	if (request.decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT) {
		__atomic_store_n(&slot->ack_published, 0U, __ATOMIC_RELEASE);
		expected = TRANSACTION_PREPARED;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_FINALIZING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			goto malformed;
		status = slot->policy.commit(slot->policy.context, binding.generation);
		if (status != CB_SUCCESS ||
		    memcmp(&request, &page->request, sizeof(request))) {
			fail_stop(slot, TRANSACTION_FINALIZING);
			goto out;
		}
		expected = TRANSACTION_FINALIZING;
		if (!__atomic_compare_exchange_n(&slot->state, &expected,
			TRANSACTION_COMMITTING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE)) {
			fail_stop(slot, TRANSACTION_FINALIZING);
			goto out;
		}
		if (stage_rax(slot, &invocation, &binding, request.decision) !=
		    CB_SUCCESS)
			fail_stop(slot, TRANSACTION_COMMITTING);
		terminal_publish(slot, page, &binding, request.decision,
			TRANSACTION_COMMITTED);
		scrub(&request, sizeof(request));
		scrub(&binding, sizeof(binding));
		status = CB_SUCCESS;
		goto out;
	}
	if (request.decision == PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT) {
		if (abort_prepared(slot, &binding, TRANSACTION_PREPARED) != CB_SUCCESS)
			goto out;
		if (stage_rax(slot, &invocation, &binding, request.decision) !=
		    CB_SUCCESS)
			fail_stop(slot, TRANSACTION_ABORTING);
		terminal_publish(slot, page, &binding, request.decision,
			TRANSACTION_ABORTED);
		scrub(&request, sizeof(request));
		scrub(&binding, sizeof(binding));
		status = CB_SUCCESS;
		goto out;
	}
malformed:
	scrub(page, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
out:
	scrub(&request, sizeof(request));
	scrub(&binding, sizeof(binding));
	scrub(&invocation, sizeof(invocation));
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
