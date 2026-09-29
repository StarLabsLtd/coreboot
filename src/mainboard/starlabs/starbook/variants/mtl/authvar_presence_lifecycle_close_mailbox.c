/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_lifecycle_close_mailbox.h"

#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL lifecycle mailbox authority is SMM-only"
#endif

enum mailbox_state { MAILBOX_EMPTY, MAILBOX_VERIFYING, MAILBOX_READY,
	MAILBOX_REPROVING, MAILBOX_FAILED };

static struct {
	uint32_t state;
	uint64_t reserved;
	struct starbook_mtl_lifecycle_mailbox_binding binding;
} owner __aligned(8);

struct mailbox_identity {
	uint32_t revision;
	uint32_t size;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint64_t invocation_generation;
	uint32_t loader_lifecycle;
	uint64_t reserved;
};

static bool span_valid(uint64_t base, size_t size)
{
	return base && size && base <= UINT64_MAX - (size - 1U) &&
		base <= UINTPTR_MAX && size - 1U <= UINTPTR_MAX - (uintptr_t)base;
}

static bool overlap(uint64_t first, size_t first_size,
	uint64_t second, size_t second_size)
{
	if (!span_valid(first, first_size) || !span_valid(second, second_size))
		return true;
	return first <= second ? second - first < first_size :
		first - second < second_size;
}

static bool protected(const struct smm_invocation_runtime_view *view,
	const void *object, size_t size)
{
	return object && smm_invocation_runtime_range_is_protected(view, object,
		size) == CB_SUCCESS;
}

static bool zero(const void *object, size_t size)
{
	const uint8_t *bytes = object;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return !value;
}

static bool authority_identity_equal(
	const struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority *a,
	const struct mailbox_identity *b)
{
	return a->revision == b->revision && a->size == b->size &&
		a->invocation_generation == b->invocation_generation &&
		a->loader_lifecycle == b->loader_lifecycle &&
		!a->reserved && !b->reserved &&
		smm_invocation_loader_instance_nonce_equal(a->loader_instance_nonce,
			b->loader_instance_nonce);
}

static struct mailbox_identity authority_identity(
	const struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority *a)
{
	return (struct mailbox_identity) {
		.revision = a->revision,
		.size = a->size,
		.loader_instance_nonce = a->loader_instance_nonce,
		.invocation_generation = a->invocation_generation,
		.loader_lifecycle = a->loader_lifecycle,
		.reserved = a->reserved,
	};
}

static void fail(struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority
	*authority)
{
	if (authority) {
		bootmem_reservation_receipt_close(&authority->verifier);
		__atomic_store_n(&authority->state,
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_FAILED,
			__ATOMIC_RELEASE);
	}
	memset(&owner.binding, 0, sizeof(owner.binding));
	__atomic_store_n(&owner.state, MAILBOX_FAILED, __ATOMIC_RELEASE);
}

enum cb_err starbook_mtl_lifecycle_mailbox_verify_consume(
	struct bootmem_reservation_receipt *receipt, uint64_t requested_base,
	size_t requested_size, const void *transient_frame, size_t transient_size,
	const struct smm_invocation_save_state_ops *active_ops,
	struct starbook_mtl_lifecycle_mailbox_binding *binding)
{
	struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority
		*authority =
		smm_get_payload_mm_authvar_presence_lifecycle_close_mailbox_authority();
	struct mailbox_identity frozen;
	struct starbook_mtl_dma_smm_binding dma = { 0 };
	const struct smm_invocation_runtime_view *view;
	struct smm_save_state_span save_state;
	uintptr_t smram_base;
	size_t smram_size;
	uint32_t cpus;
	uint32_t expected = MAILBOX_EMPTY;
	uint32_t authority_expected =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_READY;
	enum cb_err status = CB_ERR;

	if (!receipt || (uintptr_t)receipt % _Alignof(*receipt) ||
	    (uintptr_t)receipt > UINTPTR_MAX - (sizeof(*receipt) - 1U) ||
	    !active_ops || !binding ||
	    (uintptr_t)binding % _Alignof(*binding) ||
	    (uintptr_t)binding > UINTPTR_MAX - (sizeof(*binding) - 1U) ||
	    !span_valid(requested_base, requested_size) ||
	    requested_size !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE ||
	    requested_base &
		(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT - 1U) ||
	    !span_valid((uintptr_t)transient_frame, transient_size) ||
	    !__atomic_compare_exchange_n(&owner.state, &expected,
		MAILBOX_VERIFYING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    !protected(view, &owner, sizeof(owner)) ||
	    !protected(view, authority, sizeof(*authority)) ||
	    !protected(view, active_ops, sizeof(*active_ops)) ||
	    !protected(view, receipt, sizeof(*receipt)) ||
	    !protected(view, binding, sizeof(*binding)) ||
	    overlap(requested_base, requested_size, (uintptr_t)transient_frame,
		transient_size) ||
	    overlap(requested_base, requested_size, (uintptr_t)&owner,
		sizeof(owner)) ||
	    overlap(requested_base, requested_size, (uintptr_t)authority,
		sizeof(*authority)) ||
	    overlap(requested_base, requested_size, (uintptr_t)active_ops,
		sizeof(*active_ops)) ||
	    overlap(requested_base, requested_size, (uintptr_t)receipt,
		sizeof(*receipt)) ||
	    overlap(requested_base, requested_size, (uintptr_t)binding,
		sizeof(*binding)) ||
	    overlap((uintptr_t)receipt, sizeof(*receipt),
		(uintptr_t)transient_frame, transient_size) ||
	    overlap((uintptr_t)receipt, sizeof(*receipt), (uintptr_t)active_ops,
		sizeof(*active_ops)) ||
	    overlap((uintptr_t)receipt, sizeof(*receipt), (uintptr_t)&owner,
		sizeof(owner)) ||
	    overlap((uintptr_t)receipt, sizeof(*receipt), (uintptr_t)authority,
		sizeof(*authority)) ||
	    overlap((uintptr_t)binding, sizeof(*binding), (uintptr_t)receipt,
		sizeof(*receipt)) ||
	    overlap((uintptr_t)binding, sizeof(*binding),
		(uintptr_t)transient_frame, transient_size) ||
	    overlap((uintptr_t)binding, sizeof(*binding), (uintptr_t)active_ops,
		sizeof(*active_ops)) ||
	    overlap((uintptr_t)binding, sizeof(*binding), (uintptr_t)&owner,
		sizeof(owner)) ||
	    overlap((uintptr_t)binding, sizeof(*binding), (uintptr_t)authority,
		sizeof(*authority)) ||
	    (active_ops->context_size &&
	     (overlap(requested_base, requested_size,
		(uintptr_t)active_ops->context, active_ops->context_size) ||
	      overlap((uintptr_t)receipt, sizeof(*receipt),
		(uintptr_t)active_ops->context, active_ops->context_size) ||
	      overlap((uintptr_t)binding, sizeof(*binding),
		(uintptr_t)active_ops->context, active_ops->context_size))))
		goto out;
	smm_region(&smram_base, &smram_size);
	if (!span_valid(smram_base, smram_size) ||
	    overlap(requested_base, requested_size, smram_base, smram_size) ||
	    smm_invocation_runtime_cpu_count(view, &cpus) != CB_SUCCESS)
		goto out;
	for (uint32_t cpu = 0; cpu < cpus; cpu++) {
		if (smm_invocation_runtime_save_state_span(view, cpu, &save_state) !=
		    CB_SUCCESS || overlap(requested_base, requested_size,
			save_state.base, save_state.size))
			goto out;
	}
	frozen = authority_identity(authority);
	if (frozen.revision !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_REVISION ||
	    frozen.size != sizeof(*authority) || frozen.reserved ||
	    frozen.loader_lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    !frozen.invocation_generation ||
	    smm_invocation_loader_instance_nonce_is_zero(
		frozen.loader_instance_nonce) ||
	    receipt->base != requested_base || receipt->bytes != requested_size ||
	    receipt->tag != BM_MEM_RESERVED ||
	    !__atomic_compare_exchange_n(&authority->state, &authority_expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_VERIFYING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    bootmem_reservation_receipt_verify_consume_exact_tag(
		&authority->verifier, receipt, BM_MEM_RESERVED) != CB_SUCCESS ||
	    !zero(receipt, sizeof(*receipt)) ||
	    !authority_identity_equal(authority, &frozen) ||
	    __atomic_load_n(&authority->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_VERIFYING ||
	    starbook_mtl_dma_smm_binding_get(&dma) != CB_SUCCESS || !dma.receipt ||
	    dma.current.loader_lifecycle != frozen.loader_lifecycle ||
	    dma.current.invocation_generation < frozen.invocation_generation ||
	    !smm_invocation_loader_instance_nonce_equal(
		dma.current.loader_instance_nonce, frozen.loader_instance_nonce) ||
	    !starbook_mtl_dma_smm_receipt_geometry_valid(dma.receipt,
		requested_base, requested_size) ||
	    !protected(view, &owner, sizeof(owner)) ||
	    !protected(view, authority, sizeof(*authority)) ||
	    !protected(view, receipt, sizeof(*receipt)) ||
	    !protected(view, binding, sizeof(*binding)) ||
	    !authority_identity_equal(authority, &frozen) ||
	    __atomic_load_n(&authority->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_VERIFYING)
		goto out;
	owner.binding = (struct starbook_mtl_lifecycle_mailbox_binding) {
		.base = requested_base,
		.size = requested_size,
		.loader_instance_nonce = frozen.loader_instance_nonce,
		.invocation_generation = frozen.invocation_generation,
		.loader_lifecycle = frozen.loader_lifecycle,
	};
	*binding = owner.binding;
	__atomic_store_n(&authority->state,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_CONSUMED,
		__ATOMIC_RELEASE);
	__atomic_store_n(&owner.state, MAILBOX_READY, __ATOMIC_RELEASE);
	status = CB_SUCCESS;
out:
	memset(&dma, 0, sizeof(dma));
	memset(&frozen, 0, sizeof(frozen));
	if (status != CB_SUCCESS)
		fail(authority);
	return status;
}

bool starbook_mtl_lifecycle_mailbox_dma_protected(void *context,
	uint64_t base, size_t size)
{
	struct starbook_mtl_lifecycle_mailbox_binding frozen;
	struct starbook_mtl_lifecycle_mailbox_binding supplied = { 0 };
	struct starbook_mtl_dma_smm_binding dma = { 0 };
	const struct smm_invocation_runtime_view *view;
	uint32_t expected = MAILBOX_READY;
	bool valid;

	if (!context || (uintptr_t)context % _Alignof(supplied) ||
	    (uintptr_t)context > UINTPTR_MAX - (sizeof(supplied) - 1U) ||
	    !__atomic_compare_exchange_n(&owner.state, &expected,
		MAILBOX_REPROVING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
	frozen = owner.binding;
	valid = smm_invocation_runtime_view_get(&view) == CB_SUCCESS &&
		protected(view, &owner, sizeof(owner)) &&
		protected(view, context, sizeof(supplied));
	if (valid)
		supplied = *(const struct starbook_mtl_lifecycle_mailbox_binding *)context;
	valid = valid && !memcmp(&supplied, &frozen, sizeof(frozen)) &&
		base == frozen.base && size == frozen.size &&
		starbook_mtl_dma_smm_binding_get(&dma) == CB_SUCCESS && dma.receipt &&
		base == frozen.base && size == frozen.size &&
		dma.current.invocation_generation >= frozen.invocation_generation &&
		dma.current.loader_lifecycle == frozen.loader_lifecycle &&
		smm_invocation_loader_instance_nonce_equal(
			dma.current.loader_instance_nonce,
			frozen.loader_instance_nonce) &&
		starbook_mtl_dma_smm_receipt_geometry_valid(dma.receipt, base, size) &&
		protected(view, &owner, sizeof(owner)) &&
		protected(view, context, sizeof(supplied)) &&
		!memcmp(context, &supplied, sizeof(supplied)) &&
		!memcmp(&owner.binding, &frozen, sizeof(frozen)) &&
		__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) == MAILBOX_REPROVING;
	memset(&frozen, 0, sizeof(frozen));
	memset(&supplied, 0, sizeof(supplied));
	if (!valid) {
		memset(&dma, 0, sizeof(dma));
		__atomic_store_n(&owner.state, MAILBOX_FAILED, __ATOMIC_RELEASE);
		return false;
	}
	memset(&dma, 0, sizeof(dma));
	__atomic_store_n(&owner.state, MAILBOX_READY, __ATOMIC_RELEASE);
	return true;
}

void *starbook_mtl_lifecycle_mailbox_dma_context(void)
{
	return &owner.binding;
}

size_t starbook_mtl_lifecycle_mailbox_dma_context_size(void)
{
	return sizeof(owner.binding);
}

void starbook_mtl_lifecycle_mailbox_poison(void)
{
	memset(&owner.binding, 0, sizeof(owner.binding));
	__atomic_store_n(&owner.state, MAILBOX_FAILED, __ATOMIC_RELEASE);
}

#if ENV_TEST
void starbook_mtl_lifecycle_mailbox_reset_test(void)
{
	memset(&owner, 0, sizeof(owner));
}
#endif
