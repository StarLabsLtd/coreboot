/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <cpu/x86/smm_invocation_tuple_trigger.h>
#include <string.h>
#if CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
#include <boot/payload_mm_authvar_presence_publication.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/helpers.h>
#include <random.h>
#endif

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool overlaps(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t a = (uintptr_t)first;
	const uintptr_t b = (uintptr_t)second;

	if (!object_valid(first, first_size, 1U) ||
	    !object_valid(second, second_size, 1U))
		return true;
	return a <= b ? b - a < first_size : a - b < second_size;
}

static bool physical_overlap(uintptr_t page, uint64_t base, uint64_t size)
{
	if (!size || base > UINT64_MAX - (size - 1U))
		return size != 0;
	return (uint64_t)page <= base ?
		base - page < PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE :
		(uint64_t)page - base < size;
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

#if CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
enum bootstrap_sender_state {
	BOOTSTRAP_SENDER_EMPTY, BOOTSTRAP_SENDER_RESERVING,
	BOOTSTRAP_SENDER_RESERVED, BOOTSTRAP_SENDER_PROVISIONING,
	BOOTSTRAP_SENDER_READY, BOOTSTRAP_SENDER_EMITTING,
	BOOTSTRAP_SENDER_CLOSED,
};

static struct {
	uint32_t state;
	struct bootmem_aligned_reservation_handle mailbox;
	struct bootmem_aligned_reservation_handle page;
	struct bootmem_aligned_reservation_handle service;
	struct bootmem_reservation_receipt_authority mailbox_signer;
	struct bootmem_reservation_receipt_authority page_signer;
	struct bootmem_reservation_receipt_authority service_signer;
} bootstrap_sender;

__weak bool mainboard_authvar_presence_cold_boot(void)
{
	return false;
}

static bool bootstrap_claim(uint32_t from, uint32_t to)
{
	return __atomic_compare_exchange_n(&bootstrap_sender.state, &from, to,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

void payload_mm_authvar_presence_tuple_sender_close(void)
{
	__atomic_store_n(&bootstrap_sender.state, BOOTSTRAP_SENDER_CLOSED,
		__ATOMIC_RELEASE);
	bootmem_reservation_receipt_close(&bootstrap_sender.mailbox_signer);
	bootmem_reservation_receipt_close(&bootstrap_sender.page_signer);
	bootmem_reservation_receipt_close(&bootstrap_sender.service_signer);
	scrub(&bootstrap_sender.mailbox, sizeof(bootstrap_sender.mailbox));
	scrub(&bootstrap_sender.page, sizeof(bootstrap_sender.page));
	scrub(&bootstrap_sender.service, sizeof(bootstrap_sender.service));
}

enum cb_err payload_mm_authvar_presence_tuple_sender_reserve(void)
{
	const struct bootmem_aligned_reservation_request request = {
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(request),
		.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE,
		.alignment = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE,
		.limit_exclusive = 1ULL << 32,
		.tag = BM_MEM_RESERVED,
	};
	const struct bootmem_aligned_reservation_request service = {
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(service),
		.bytes = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE,
		.alignment = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE,
		.limit_exclusive = 1ULL << 32, .tag = BM_MEM_TABLE,
	};

	if (!bootstrap_claim(BOOTSTRAP_SENDER_EMPTY, BOOTSTRAP_SENDER_RESERVING))
		return CB_ERR;
	if (bootmem_aligned_reservation_register(&request, &bootstrap_sender.page) ||
	    bootmem_aligned_reservation_register(&service, &bootstrap_sender.service) ||
	    !bootstrap_claim(BOOTSTRAP_SENDER_RESERVING, BOOTSTRAP_SENDER_RESERVED)) {
		payload_mm_authvar_presence_tuple_sender_close();
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_tuple_sender_loader_provision(
	struct payload_mm_authvar_presence_bootstrap *slot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_topology *topology)
{
	struct smm_invocation_loader_instance identity;
	struct smm_invocation_loader_instance identity_check;
	struct smm_invocation_topology participants;
	struct smm_invocation_topology participants_check;
	uint64_t random[3][BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE / sizeof(uint64_t)] = { 0 };
	bool required;
	enum cb_err status = CB_ERR;

	if (!object_valid(slot, sizeof(*slot), _Alignof(*slot)) ||
	    (instance && overlaps(slot, sizeof(*slot), instance, sizeof(*instance))) ||
	    (topology && overlaps(slot, sizeof(*slot), topology, sizeof(*topology))))
		return CB_ERR;
	scrub(slot, sizeof(*slot));
	if (payload_mm_authvar_presence_publication_loader_required(&required) != CB_SUCCESS)
		goto fail;
	if (!required)
		return CB_SUCCESS;
	if (!bootstrap_claim(BOOTSTRAP_SENDER_RESERVED, BOOTSTRAP_SENDER_PROVISIONING) ||
	    !object_valid(instance, sizeof(*instance), _Alignof(*instance)) ||
	    !object_valid(topology, sizeof(*topology), _Alignof(*topology)) ||
	    smm_invocation_loader_instance_read(instance, &identity) != CB_SUCCESS ||
	    smm_invocation_topology_read(topology, &participants) != CB_SUCCESS ||
	    identity.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    participants.bsp_cpu || !participants.active_cpus ||
	    participants.active_cpus > CONFIG_MAX_CPUS ||
	    !mainboard_authvar_presence_cold_boot() ||
	    payload_mm_authvar_presence_producer_transaction_binding_take(
		participants.bsp_cpu, participants.active_cpus, &slot->binding,
		&bootstrap_sender.mailbox) != CB_SUCCESS)
		goto fail;
	for (size_t receipt = 0; receipt < ARRAY_SIZE(random); receipt++)
		for (size_t word = 0; word < ARRAY_SIZE(random[receipt]); word++)
			if (get_random_number_64(&random[receipt][word]) != CB_SUCCESS)
				goto fail;
	if (smm_invocation_loader_instance_read(instance, &identity_check) != CB_SUCCESS ||
	    smm_invocation_topology_read(topology, &participants_check) != CB_SUCCESS ||
	    memcmp(&identity, &identity_check, sizeof(identity)) ||
	    memcmp(&participants, &participants_check, sizeof(participants)) ||
	    !mainboard_authvar_presence_cold_boot() ||
	    __atomic_load_n(&bootstrap_sender.state, __ATOMIC_ACQUIRE) !=
		BOOTSTRAP_SENDER_PROVISIONING ||
	    bootmem_reservation_receipt_provision(&bootstrap_sender.mailbox_signer,
		&slot->mailbox_verifier, (uint8_t *)random[0],
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, slot->binding.generation,
		&bootstrap_sender.mailbox) != CB_SUCCESS ||
	    bootmem_reservation_receipt_provision(&bootstrap_sender.page_signer,
		&slot->page_verifier, (uint8_t *)random[1],
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, slot->binding.generation,
		&bootstrap_sender.page) != CB_SUCCESS ||
	    bootmem_reservation_receipt_provision(&bootstrap_sender.service_signer,
		&slot->service_verifier, (uint8_t *)random[2],
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, slot->binding.generation,
		&bootstrap_sender.service) != CB_SUCCESS ||
	    !bootstrap_claim(BOOTSTRAP_SENDER_PROVISIONING, BOOTSTRAP_SENDER_READY))
		goto fail;
	slot->loader_nonce = identity.loader_instance_nonce;
	slot->loader_lifecycle = identity.lifecycle;
	slot->cold_boot_proven = 1U;
	__atomic_store_n(&slot->state, PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_PROVISIONED,
		__ATOMIC_RELEASE);
	status = CB_SUCCESS;
	goto out;
fail:
	payload_mm_authvar_presence_tuple_sender_close();
	payload_mm_authvar_presence_producer_abort();
	bootmem_reservation_receipt_close(&slot->mailbox_verifier);
	bootmem_reservation_receipt_close(&slot->page_verifier);
	bootmem_reservation_receipt_close(&slot->service_verifier);
	scrub(slot, sizeof(*slot));
out:
	scrub(random, sizeof(random));
	scrub(&identity, sizeof(identity));
	scrub(&identity_check, sizeof(identity_check));
	scrub(&participants, sizeof(participants));
	scrub(&participants_check, sizeof(participants_check));
	return status;
}

enum cb_err payload_mm_authvar_presence_tuple_sender_receipts_take(
	struct payload_mm_authvar_presence_bootstrap_receipts *receipts,
	struct payload_mm_authvar_presence_tuple_sender *sender)
{
	struct payload_mm_authvar_presence_bootstrap_receipts snapshot = { 0 };
	struct payload_mm_authvar_presence_tuple_sender transport = { 0 };
	enum cb_err result = CB_ERR;

	if (!object_valid(receipts, sizeof(*receipts), _Alignof(*receipts)) ||
	    !object_valid(sender, sizeof(*sender), _Alignof(*sender)) ||
	    overlaps(receipts, sizeof(*receipts), sender, sizeof(*sender)) ||
	    overlaps(receipts, sizeof(*receipts), &bootstrap_sender,
		sizeof(bootstrap_sender)) ||
	    overlaps(sender, sizeof(*sender), &bootstrap_sender, sizeof(bootstrap_sender)))
		return CB_ERR;
	scrub(receipts, sizeof(*receipts));
	scrub(sender, sizeof(*sender));
	if (!bootstrap_claim(BOOTSTRAP_SENDER_READY, BOOTSTRAP_SENDER_EMITTING) ||
	    bootmem_aligned_reservation_receipt_emit_exact_tag(&bootstrap_sender.mailbox,
		&bootstrap_sender.mailbox_signer, &snapshot.mailbox, BM_MEM_RESERVED) !=
		CB_SUCCESS ||
	    bootmem_aligned_reservation_receipt_emit_exact_tag(&bootstrap_sender.page,
		&bootstrap_sender.page_signer, &snapshot.page, BM_MEM_RESERVED) !=
		CB_SUCCESS ||
	    bootmem_aligned_reservation_receipt_emit_exact_tag(&bootstrap_sender.service,
		&bootstrap_sender.service_signer, &snapshot.service, BM_MEM_TABLE) !=
		CB_SUCCESS ||
	    snapshot.page.base > UINTPTR_MAX ||
	    snapshot.page.bytes != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	    snapshot.page.base % PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE ||
	    physical_overlap((uintptr_t)snapshot.page.base, snapshot.mailbox.base,
		snapshot.mailbox.bytes) ||
	    !snapshot.service.base || snapshot.service.base % 4096U ||
	    snapshot.service.base >
		UINT32_MAX - (PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE - 1U) ||
	    snapshot.service.bytes != PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE ||
	    physical_overlap((uintptr_t)snapshot.page.base, snapshot.service.base,
		snapshot.service.bytes) ||
	    physical_overlap((uintptr_t)snapshot.mailbox.base, snapshot.service.base,
		snapshot.service.bytes) ||
	    __atomic_load_n(&bootstrap_sender.state, __ATOMIC_ACQUIRE) !=
		BOOTSTRAP_SENDER_EMITTING)
		goto out;
	transport = (struct payload_mm_authvar_presence_tuple_sender) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_REVISION,
		.size = sizeof(transport), .page = (void *)(uintptr_t)snapshot.page.base,
	};
	*receipts = snapshot;
	*sender = transport;
	if (__atomic_load_n(&bootstrap_sender.state, __ATOMIC_ACQUIRE) !=
	    BOOTSTRAP_SENDER_EMITTING) {
		scrub(receipts, sizeof(*receipts));
		scrub(sender, sizeof(*sender));
		goto out;
	}
	result = CB_SUCCESS;
out:
	payload_mm_authvar_presence_tuple_sender_close();
	if (result != CB_SUCCESS)
		payload_mm_authvar_presence_producer_abort();
	scrub(&snapshot, sizeof(snapshot));
	scrub(&transport, sizeof(transport));
	return result;
}
#endif

static enum cb_err send(void *context,
	const struct payload_mm_authvar_presence_seed *seed,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value)
{
	struct payload_mm_authvar_presence_tuple_sender sender;
	struct payload_mm_authvar_presence_transaction_binding binding_snapshot;
	struct payload_mm_authvar_presence_seed seed_snapshot;
	struct payload_mm_authvar_presence_transaction_request request = { 0 };
	struct payload_mm_authvar_presence_transaction_request response_request = { 0 };
	struct payload_mm_authvar_presence_transaction_request response_request_check;
	struct payload_mm_authvar_presence_transaction_ack response_ack = { 0 };
	struct payload_mm_authvar_presence_transaction_ack response_ack_check;
	struct payload_mm_authvar_presence_transaction_page *page;
	uint64_t value = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL;
	bool prepare = decision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
	bool response_valid;
	enum cb_err status = CB_ERR;

	if (!object_valid(context, sizeof(sender), _Alignof(typeof(sender))) ||
	    !object_valid(binding, sizeof(*binding), _Alignof(*binding)) ||
	    !object_valid(ack, sizeof(*ack), _Alignof(*ack)) ||
	    !object_valid(saved_value, sizeof(*saved_value),
		_Alignof(*saved_value)) ||
	    (prepare &&
	     !object_valid(seed, sizeof(*seed), _Alignof(*seed))) ||
	    (!prepare && seed))
		return CB_ERR;
	sender = *(const struct payload_mm_authvar_presence_tuple_sender *)context;
	page = sender.page;
	if (!object_valid(page, sizeof(*page), _Alignof(*page)) ||
	    overlaps(context, sizeof(sender), binding, sizeof(*binding)) ||
	    overlaps(context, sizeof(sender), ack, sizeof(*ack)) ||
	    overlaps(context, sizeof(sender), saved_value, sizeof(*saved_value)) ||
	    overlaps(binding, sizeof(*binding), ack, sizeof(*ack)) ||
	    overlaps(binding, sizeof(*binding), saved_value, sizeof(*saved_value)) ||
	    overlaps(ack, sizeof(*ack), saved_value, sizeof(*saved_value)) ||
	    overlaps(page, sizeof(*page), context, sizeof(sender)) ||
	    overlaps(page, sizeof(*page), binding, sizeof(*binding)) ||
	    overlaps(page, sizeof(*page), ack, sizeof(*ack)) ||
	    overlaps(page, sizeof(*page), saved_value, sizeof(*saved_value)) ||
	    (prepare &&
	     (overlaps(seed, sizeof(*seed), context, sizeof(sender)) ||
	      overlaps(seed, sizeof(*seed), binding, sizeof(*binding)) ||
	      overlaps(seed, sizeof(*seed), ack, sizeof(*ack)) ||
	      overlaps(seed, sizeof(*seed), saved_value, sizeof(*saved_value)) ||
	      overlaps(seed, sizeof(*seed), page, sizeof(*page)))))
		return CB_ERR;
	memset(ack, 0, sizeof(*ack));
	*saved_value = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL;
	if (sender.revision !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_REVISION ||
	    sender.size != sizeof(sender))
		return CB_ERR;
	binding_snapshot = *binding;
	memset(&seed_snapshot, 0, sizeof(seed_snapshot));
	if (prepare) {
		seed_snapshot = *seed;
		if (seed_snapshot.revision !=
			PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION ||
		    seed_snapshot.size != sizeof(seed_snapshot) ||
		    seed_snapshot.endpoint.generation != binding_snapshot.generation ||
		    seed_snapshot.backing.generation != binding_snapshot.generation ||
		    physical_overlap((uintptr_t)page,
			seed_snapshot.endpoint.communication_base,
			seed_snapshot.endpoint.communication_size) ||
		    physical_overlap((uintptr_t)page, seed_snapshot.backing.base,
			seed_snapshot.backing.bytes)) {
			scrub(&seed_snapshot, sizeof(seed_snapshot));
			scrub(&binding_snapshot, sizeof(binding_snapshot));
			return CB_ERR;
		}
	}
	if (!payload_mm_authvar_presence_transaction_result(&binding_snapshot,
		decision)) {
		scrub(&seed_snapshot, sizeof(seed_snapshot));
		scrub(&binding_snapshot, sizeof(binding_snapshot));
		return CB_ERR;
	}
	request.binding = binding_snapshot;
	request.decision = decision;
	if (prepare)
		request.seed = seed_snapshot;
	scrub(page, sizeof(*page));
	page->request = request;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	if (memcmp(context, &sender, sizeof(sender)) ||
	    memcmp(binding, &binding_snapshot, sizeof(binding_snapshot)) ||
	    (prepare && memcmp(seed, &seed_snapshot, sizeof(seed_snapshot)))) {
		scrub(page, sizeof(*page));
		goto out;
	}
	value = smm_invocation_tuple_trigger();
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	response_request = page->request;
	response_ack = page->ack;
	response_valid = zero(&response_request, sizeof(response_request)) &&
		zero(page->reserved, sizeof(page->reserved));
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	response_request_check = page->request;
	response_ack_check = page->ack;
	response_valid = response_valid &&
		!memcmp(&response_request, &response_request_check,
			sizeof(response_request)) &&
		!memcmp(&response_ack, &response_ack_check, sizeof(response_ack)) &&
		zero(page->reserved, sizeof(page->reserved));
	scrub(page, sizeof(*page));
	response_valid = response_valid &&
		!memcmp(context, &sender, sizeof(sender)) &&
		!memcmp(binding, &binding_snapshot, sizeof(binding_snapshot)) &&
		(!prepare || !memcmp(seed, &seed_snapshot, sizeof(seed_snapshot)));
	if (response_valid &&
	    payload_mm_authvar_presence_transaction_ack_valid(&binding_snapshot,
		decision, &response_ack, value)) {
		*ack = response_ack;
		*saved_value = value;
		status = CB_SUCCESS;
		goto out;
	}
	if (prepare && response_valid &&
	    payload_mm_authvar_presence_transaction_ack_valid(&binding_snapshot,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT,
		&response_ack, value)) {
		*ack = response_ack;
		*saved_value = value;
	}
out:
	scrub(&response_request, sizeof(response_request));
	scrub(&response_request_check, sizeof(response_request_check));
	scrub(&response_ack, sizeof(response_ack));
	scrub(&response_ack_check, sizeof(response_ack_check));
	scrub(&request, sizeof(request));
	scrub(&seed_snapshot, sizeof(seed_snapshot));
	scrub(&binding_snapshot, sizeof(binding_snapshot));
	scrub(&value, sizeof(value));
	return status;
}

enum cb_err payload_mm_authvar_presence_tuple_sender_prepare(
	void *context, const struct payload_mm_authvar_presence_seed *seed,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value)
{
	return send(context, seed, binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, ack, saved_value);
}

enum cb_err payload_mm_authvar_presence_tuple_sender_commit(
	void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value)
{
	return send(context, NULL, binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, ack, saved_value);
}

enum cb_err payload_mm_authvar_presence_tuple_sender_abort(
	void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value)
{
	return send(context, NULL, binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, ack, saved_value);
}
