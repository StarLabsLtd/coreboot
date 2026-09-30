/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <boot/payload_mm_authvar_presence_backing.h>
#include <boot/payload_mm_authvar_service.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
#include <boot/payload_mm_authvar_service_receiver.h>
#endif
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Presence bootstrap ownership facts are SMM-only"
#endif

__weak struct payload_mm_authvar_presence_bootstrap *
smm_get_payload_mm_authvar_presence_bootstrap(void)
{
	return NULL;
}

static bool bootstrap_identity_valid(
	const struct payload_mm_authvar_presence_bootstrap *slot)
{
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;

	return !slot->reserved && slot->cold_boot_proven == 1U &&
		payload_mm_authvar_presence_transaction_result(&slot->binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE) &&
		smm_invocation_runtime_binding_get(&runtime) == CB_SUCCESS &&
		smm_invocation_loader_instance_read(runtime.instance, &instance) == CB_SUCCESS &&
		smm_invocation_topology_read(runtime.topology, &topology) == CB_SUCCESS &&
		instance.lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD &&
		slot->loader_lifecycle == instance.lifecycle &&
		smm_invocation_loader_instance_nonce_equal(slot->loader_nonce,
			instance.loader_instance_nonce) &&
		slot->binding.initiator_cpu == topology.bsp_cpu &&
		slot->binding.maximum_cpus == topology.active_cpus;
}

enum cb_err payload_mm_authvar_presence_bootstrap_binding_get(
	const struct payload_mm_authvar_presence_transaction_binding **binding)
{
	const struct smm_invocation_runtime_view *view;
	struct payload_mm_authvar_presence_bootstrap *slot;

	if (!binding || (uintptr_t)binding % _Alignof(*binding) ||
	    smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(view, binding, sizeof(*binding)) !=
		CB_SUCCESS)
		return CB_ERR;
	slot = smm_get_payload_mm_authvar_presence_bootstrap();
	if (!slot || (uintptr_t)slot % _Alignof(*slot) ||
	    smm_invocation_runtime_range_is_protected(view, slot, sizeof(*slot)) !=
		CB_SUCCESS)
		goto fail;
	/* Output aliases must not modify the protected ownership record. */
	if ((uintptr_t)binding <= (uintptr_t)slot ?
	    (uintptr_t)slot - (uintptr_t)binding < sizeof(*binding) :
	    (uintptr_t)binding - (uintptr_t)slot < sizeof(*slot))
		return CB_ERR;
	if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY ||
	    !bootstrap_identity_valid(slot))
		goto fail;
	*binding = &slot->binding;
	return CB_SUCCESS;
fail:
	*binding = NULL;
	return CB_ERR;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY)
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
static bool service_overlaps_page(const struct bootmem_reservation_receipt *service,
	uint64_t page)
{
	return service->base <= page ? page - service->base < service->bytes :
		service->base - page < 4096U;
}
#endif

enum cb_err payload_mm_authvar_presence_bootstrap_receipts_import(
	struct payload_mm_authvar_presence_bootstrap_receipts *receipts)
{
	const struct smm_invocation_runtime_view *view;
	struct payload_mm_authvar_presence_bootstrap *slot;
	struct payload_mm_authvar_presence_bootstrap_receipts snapshot;
	struct bootmem_reservation_receipt mailbox_receipt;
	struct payload_mm_authvar_presence_backing backing;
	uint32_t expected = PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_PROVISIONED;

	if (!receipts || (uintptr_t)receipts % _Alignof(*receipts) ||
	    smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(view, receipts, sizeof(*receipts)) !=
		CB_SUCCESS || !(slot = smm_get_payload_mm_authvar_presence_bootstrap()) ||
	    (uintptr_t)slot % _Alignof(*slot) ||
	    smm_invocation_runtime_range_is_protected(view, slot, sizeof(*slot)) != CB_SUCCESS ||
	    ((uintptr_t)receipts <= (uintptr_t)slot ?
		(uintptr_t)slot - (uintptr_t)receipts < sizeof(*receipts) :
		(uintptr_t)receipts - (uintptr_t)slot < sizeof(*slot)) ||
	    !bootstrap_identity_valid(slot) ||
	    !__atomic_compare_exchange_n(&slot->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_IMPORTING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	snapshot = *receipts;
	mailbox_receipt = snapshot.mailbox;
	backing = (struct payload_mm_authvar_presence_backing) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION,
		.size = sizeof(backing), .base = snapshot.mailbox.base,
		.bytes = snapshot.mailbox.bytes, .generation = snapshot.mailbox.generation,
		.tag = BM_MEM_RESERVED,
	};
	if (!snapshot.page.base || (snapshot.page.base & 4095U) ||
	    snapshot.page.base > UINT32_MAX - 4095U || snapshot.page.bytes != 4096U ||
	    !snapshot.mailbox.base || (snapshot.mailbox.base & 4095U) ||
	    snapshot.mailbox.base > UINT32_MAX - 4095U || snapshot.mailbox.bytes != 4096U ||
	    snapshot.mailbox.base == snapshot.page.base ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	    !snapshot.service.base || (snapshot.service.base & 4095U) ||
	    snapshot.service.base >
		UINT32_MAX - (PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE - 1U) ||
	    snapshot.service.bytes != PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE ||
	    snapshot.service.generation != slot->binding.generation ||
	    snapshot.service.tag != BM_MEM_TABLE ||
	    memcmp(&snapshot.service.handle, &slot->service_verifier.handle,
		sizeof(snapshot.service.handle)) ||
	    service_overlaps_page(&snapshot.service, snapshot.page.base) ||
	    service_overlaps_page(&snapshot.service, snapshot.mailbox.base) ||
#else
	    memcmp(&snapshot.service, &(struct bootmem_reservation_receipt) { 0 },
		sizeof(snapshot.service)) ||
#endif
	    snapshot.page.generation != slot->binding.generation ||
	    snapshot.mailbox.generation != slot->binding.generation ||
	    memcmp(&snapshot.page.handle, &slot->page_verifier.handle,
		sizeof(snapshot.page.handle)) ||
	    payload_mm_authvar_presence_backing_evidence_publish(&slot->mailbox_verifier,
		&mailbox_receipt, &backing) != CB_SUCCESS ||
	    memcmp(receipts, &snapshot, sizeof(snapshot)) ||
	    !bootstrap_identity_valid(slot))
		goto fail;
	/* PREPARE consumes/authenticates the page receipt through the canonical slot. */
	slot->page_receipt = snapshot.page;
	expected = PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_IMPORTING;
	if (!__atomic_compare_exchange_n(&slot->state, &expected,
		PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto fail;
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	/* The sole backend provider takes this owner, not a retained receipt copy. */
	if (payload_mm_authvar_service_prepare(&slot->service_verifier,
		&snapshot.service) != CB_SUCCESS || !bootstrap_identity_valid(slot) ||
	    __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY)
		goto fail;
#endif
	memset(receipts, 0, sizeof(*receipts));
	memset(&snapshot, 0, sizeof(snapshot));
	memset(&backing, 0, sizeof(backing));
	memset(&mailbox_receipt, 0, sizeof(mailbox_receipt));
	return CB_SUCCESS;
fail:
	bootmem_reservation_receipt_close(&slot->mailbox_verifier);
	bootmem_reservation_receipt_close(&slot->page_verifier);
	bootmem_reservation_receipt_close(&slot->service_verifier);
	payload_mm_authvar_presence_backing_evidence_close();
	memset(&slot->page_receipt, 0, sizeof(slot->page_receipt));
	__atomic_store_n(&slot->state, PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_FAILED,
		__ATOMIC_RELEASE);
	memset(receipts, 0, sizeof(*receipts));
	memset(&snapshot, 0, sizeof(snapshot));
	memset(&backing, 0, sizeof(backing));
	memset(&mailbox_receipt, 0, sizeof(mailbox_receipt));
	return CB_ERR;
}
#endif
