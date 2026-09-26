/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_handoff.h>
#include <bootmem.h>
#include <string.h>

#if !ENV_RAMSTAGE && !ENV_TEST
#error "Authenticated-variable presence handoff sender is ramstage-only"
#endif

#define HANDOFF_EMPTY 0U
#define HANDOFF_RESERVED 0x11U
#define HANDOFF_PROVISIONING 0x33U
#define HANDOFF_READY 0xa5U
#define HANDOFF_TAKING 0x5aU
#define HANDOFF_ABORT_REQUESTED 0x66U
#define HANDOFF_DELIVERING 0x77U
#define HANDOFF_TERMINAL 0xc3U

#if ENV_TEST
void payload_mm_authvar_presence_handoff_test_before_delivery(void);
#endif

static struct {
	struct bootmem_reservation_receipt_authority mailbox_signer;
	struct bootmem_reservation_receipt_authority transfer_signer;
	struct bootmem_aligned_reservation_handle mailbox_handle;
	struct bootmem_aligned_reservation_handle transfer_handle;
	uint64_t generation;
	uint64_t identity;
	uint32_t owner_cpu;
	uint32_t maximum_cpus;
	uint8_t capability[PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CAPABILITY_SIZE];
	uint8_t state;
} sender;

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
	uint8_t combined = 0;

	while (size--)
		combined |= *bytes++;
	return combined;
}

static bool zero(const void *buffer, size_t size)
{
	return !nonzero(buffer, size);
}

static bool provisioning(void)
{
	return __atomic_load_n(&sender.state, __ATOMIC_ACQUIRE) ==
		HANDOFF_PROVISIONING;
}

static bool taking(void)
{
	return __atomic_load_n(&sender.state, __ATOMIC_ACQUIRE) == HANDOFF_TAKING;
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!object_valid(first, first_size, 1U) ||
	    !object_valid(second, second_size, 1U))
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

__weak bool platform_payload_mm_authvar_presence_handoff_cold_boot(void)
{
	return false;
}

__weak enum cb_err platform_payload_mm_authvar_presence_handoff_self_smi(
	const struct payload_mm_authvar_presence_handoff_descriptor *descriptor,
	uint64_t *status)
{
	(void)descriptor;
	if (status)
		*status = PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CLOSED;
	return CB_ERR;
}

static void close_sender(void)
{
	bootmem_reservation_receipt_close(&sender.mailbox_signer);
	bootmem_reservation_receipt_close(&sender.transfer_signer);
	scrub(&sender.mailbox_handle, sizeof(sender.mailbox_handle));
	scrub(&sender.transfer_handle, sizeof(sender.transfer_handle));
	scrub(sender.capability, sizeof(sender.capability));
	sender.generation = 0;
	sender.identity = 0;
	sender.owner_cpu = 0;
	sender.maximum_cpus = 0;
	__atomic_store_n(&sender.state, HANDOFF_TERMINAL, __ATOMIC_RELEASE);
}

enum cb_err payload_mm_authvar_presence_handoff_reserve(void)
{
	const struct bootmem_aligned_reservation_request request = {
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(request),
		.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE,
		.alignment = PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_ALIGNMENT,
		.limit_exclusive = 1ULL << 32,
		.tag = BM_MEM_RESERVED,
	};
	uint8_t expected = HANDOFF_EMPTY;

	if (!__atomic_compare_exchange_n(&sender.state, &expected,
		HANDOFF_PROVISIONING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return CB_ERR;
	if (bootmem_aligned_reservation_register(&request,
		&sender.transfer_handle) || !provisioning()) {
		close_sender();
		return CB_ERR;
	}
	expected = HANDOFF_PROVISIONING;
	if (!__atomic_compare_exchange_n(&sender.state, &expected,
		HANDOFF_RESERVED, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		close_sender();
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_handoff_loader_provision(
	struct payload_mm_authvar_presence_handoff_slot *slot,
	struct payload_mm_authvar_presence_handoff_loader_seed *seed)
{
	struct payload_mm_authvar_presence_handoff_loader_seed candidate = { 0 };
	uint8_t expected = HANDOFF_RESERVED;
	enum cb_err status = CB_ERR;

	if (!object_valid(seed, sizeof(*seed), _Alignof(*seed)) ||
	    overlap(seed, sizeof(*seed), &sender, sizeof(sender)))
		return CB_ERR;
	if (!object_valid(slot, sizeof(*slot), _Alignof(*slot)) ||
	    overlap(slot, sizeof(*slot), &sender, sizeof(sender))) {
		scrub(seed, sizeof(*seed));
		return CB_ERR;
	}
	if (overlap(seed, sizeof(*seed), slot, sizeof(*slot)))
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&sender.state, &expected,
		HANDOFF_PROVISIONING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE)) {
		scrub(seed, sizeof(*seed));
		return CB_ERR;
	}
	scrub(slot, sizeof(*slot));
	memcpy(&candidate, seed, sizeof(candidate));
	if (!platform_payload_mm_authvar_presence_handoff_cold_boot() ||
	    !provisioning() ||
	    memcmp(&candidate, seed, sizeof(candidate)) ||
	    candidate.revision != PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_REVISION ||
	    candidate.size != sizeof(candidate) || !candidate.channel_generation ||
	    !candidate.mailbox_handle.opaque[0] ||
	    !candidate.mailbox_handle.opaque[1] ||
	    !nonzero(candidate.channel_capability,
		sizeof(candidate.channel_capability)) ||
	    !nonzero(candidate.mailbox_receipt_secret,
		sizeof(candidate.mailbox_receipt_secret)) ||
	    !nonzero(candidate.transfer_receipt_secret,
		sizeof(candidate.transfer_receipt_secret)) ||
	    candidate.owner_cpu || !candidate.maximum_cpus ||
	    candidate.maximum_cpus > CONFIG_MAX_CPUS ||
	    candidate.reserved[0] || candidate.reserved[1])
		goto out;
	sender.mailbox_handle = candidate.mailbox_handle;
	sender.generation = candidate.channel_generation;
	sender.owner_cpu = candidate.owner_cpu;
	sender.maximum_cpus = candidate.maximum_cpus;
	memcpy(sender.capability, candidate.channel_capability,
		sizeof(sender.capability));
	sender.identity = payload_mm_authvar_presence_handoff_identity(
		sender.capability, sender.generation);
	if (bootmem_reservation_receipt_provision(&sender.mailbox_signer,
		&slot->mailbox_verifier, candidate.mailbox_receipt_secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, sender.generation,
		&sender.mailbox_handle) != CB_SUCCESS || !provisioning() ||
	    bootmem_reservation_receipt_provision(&sender.transfer_signer,
		&slot->transfer_verifier, candidate.transfer_receipt_secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, sender.generation,
		&sender.transfer_handle) != CB_SUCCESS || !provisioning())
		goto out;
	slot->channel_generation = sender.generation;
	slot->channel_identity = sender.identity;
	slot->owner_cpu = sender.owner_cpu;
	slot->maximum_cpus = sender.maximum_cpus;
	memcpy(slot->channel_capability, sender.capability,
		sizeof(slot->channel_capability));
	__atomic_store_n(&slot->state, HANDOFF_READY, __ATOMIC_RELEASE);
	expected = HANDOFF_PROVISIONING;
	if (!__atomic_compare_exchange_n(&sender.state, &expected, HANDOFF_READY,
		false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto out;
	status = CB_SUCCESS;

out:
	scrub(seed, sizeof(*seed));
	scrub(&candidate, sizeof(candidate));
	if (status != CB_SUCCESS) {
		bootmem_reservation_receipt_close(&slot->mailbox_verifier);
		bootmem_reservation_receipt_close(&slot->transfer_verifier);
		scrub(slot, sizeof(*slot));
		__atomic_store_n(&slot->state, HANDOFF_TERMINAL,
			__ATOMIC_RELEASE);
		close_sender();
	}
	return status;
}

static enum cb_err resolved_page(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation, uint64_t size,
	uint64_t alignment)
{
	if (!sender.generation || !sender.identity ||
	    bootmem_aligned_reservation_query(handle, reservation) ||
	    reservation->tag != BM_MEM_RESERVED || reservation->reserved ||
	    reservation->size != size || reservation->base % alignment ||
	    reservation->base > UINTPTR_MAX - reservation->size ||
	    reservation->base + reservation->size < reservation->base)
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_handoff_install(void *context,
	const struct payload_mm_authvar_presence_seed *seed)
{
	struct payload_mm_authvar_presence_handoff_request candidate = { 0 };
	struct payload_mm_authvar_presence_handoff_descriptor descriptor = { 0 };
	struct payload_mm_authvar_presence_handoff_descriptor sealed_descriptor;
	struct payload_mm_authvar_presence_seed sealed_seed = { 0 };
	struct bootmem_aligned_reservation mailbox = { 0 };
	struct bootmem_aligned_reservation transfer = { 0 };
	uint8_t expected = HANDOFF_READY;
	uint64_t trigger_status = PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_REJECTED;
	uint8_t *request = NULL;
	enum cb_err status = CB_ERR;

	(void)context;
	if (!object_valid(seed, sizeof(*seed), _Alignof(*seed)) ||
	    overlap(seed, sizeof(*seed), &sender, sizeof(sender)) ||
	    !__atomic_compare_exchange_n(&sender.state, &expected, HANDOFF_TAKING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	memcpy(&sealed_seed, seed, sizeof(sealed_seed));
	if (memcmp(seed, &sealed_seed, sizeof(sealed_seed)) ||
	    resolved_page(&sender.mailbox_handle, &mailbox,
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE,
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT) != CB_SUCCESS ||
	    !taking() || memcmp(seed, &sealed_seed, sizeof(sealed_seed)) ||
	    resolved_page(&sender.transfer_handle, &transfer,
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE,
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_ALIGNMENT) != CB_SUCCESS ||
	    !taking() || memcmp(seed, &sealed_seed, sizeof(sealed_seed)) ||
	    mailbox.base == transfer.base)
		goto out;
	request = (void *)(uintptr_t)transfer.base;
	if (overlap(seed, sizeof(*seed), request,
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE) ||
	    overlap(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE,
		&sender, sizeof(sender)) ||
	    sealed_seed.revision != PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION ||
	    sealed_seed.size != sizeof(sealed_seed) ||
	    sealed_seed.endpoint.generation != sender.generation ||
	    sealed_seed.endpoint.communication_base != mailbox.base ||
	    sealed_seed.endpoint.communication_size !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE ||
	    payload_mm_authvar_presence_endpoint_validate(&sealed_seed.endpoint) !=
		CB_SUCCESS || memcmp(seed, &sealed_seed, sizeof(sealed_seed)) ||
	    !nonzero(sealed_seed.capability, sizeof(sealed_seed.capability)))
		goto out;
	candidate.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_REVISION;
	candidate.size = sizeof(candidate);
	candidate.channel_generation = sender.generation;
	candidate.mailbox_base = mailbox.base;
	candidate.mailbox_size = mailbox.size;
	candidate.transfer_base = transfer.base;
	candidate.transfer_size = transfer.size;
	candidate.channel_identity = sender.identity;
	candidate.owner_cpu = sender.owner_cpu;
	candidate.maximum_cpus = sender.maximum_cpus;
	candidate.descriptor_cookie = payload_mm_authvar_presence_handoff_cookie(
		sender.identity, (uintptr_t)request, sender.generation,
		candidate.owner_cpu, candidate.maximum_cpus);
	memcpy(candidate.channel_capability, sender.capability,
		sizeof(candidate.channel_capability));
	if (memcmp(seed, &sealed_seed, sizeof(sealed_seed)) ||
	    bootmem_aligned_reservation_receipt_emit_exact_tag(
		&sender.mailbox_handle, &sender.mailbox_signer,
		&candidate.mailbox_receipt, BM_MEM_RESERVED) != CB_SUCCESS ||
	    !taking() || memcmp(seed, &sealed_seed, sizeof(sealed_seed)) ||
	    bootmem_aligned_reservation_receipt_emit_exact_tag(
		&sender.transfer_handle, &sender.transfer_signer,
		&candidate.transfer_receipt, BM_MEM_RESERVED) != CB_SUCCESS ||
	    !taking() || memcmp(seed, &sealed_seed, sizeof(sealed_seed)))
		goto out;
	candidate.seed = sealed_seed;
	descriptor = (struct payload_mm_authvar_presence_handoff_descriptor) {
		.channel_identity = sender.identity,
		.request_base = (uintptr_t)request,
		.descriptor_cookie = candidate.descriptor_cookie,
	};
	sealed_descriptor = descriptor;
	if (!taking() || memcmp(seed, &sealed_seed, sizeof(sealed_seed)))
		goto out;
	scrub(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE);
	memcpy(request, &candidate, sizeof(candidate));
	__atomic_thread_fence(__ATOMIC_RELEASE);
#if ENV_TEST
	payload_mm_authvar_presence_handoff_test_before_delivery();
#endif
	expected = HANDOFF_TAKING;
	if (memcmp(seed, &sealed_seed, sizeof(sealed_seed)) ||
	    !__atomic_compare_exchange_n(&sender.state, &expected,
		HANDOFF_DELIVERING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    memcmp(request, &candidate, sizeof(candidate)) ||
	    memcmp(seed, &sealed_seed, sizeof(sealed_seed)) ||
	    platform_payload_mm_authvar_presence_handoff_self_smi(&descriptor,
		&trigger_status) != CB_SUCCESS ||
	    __atomic_load_n(&sender.state, __ATOMIC_ACQUIRE) != HANDOFF_DELIVERING ||
	    memcmp(seed, &sealed_seed, sizeof(sealed_seed)) ||
	    memcmp(&descriptor, &sealed_descriptor, sizeof(descriptor)) ||
	    trigger_status != PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_SUCCESS ||
	    !zero(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE))
		goto out;
	status = CB_SUCCESS;

out:
	if (request)
		scrub(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE);
	scrub(&candidate, sizeof(candidate));
	scrub(&descriptor, sizeof(descriptor));
	scrub(&sealed_descriptor, sizeof(sealed_descriptor));
	scrub(&sealed_seed, sizeof(sealed_seed));
	scrub(&mailbox, sizeof(mailbox));
	scrub(&transfer, sizeof(transfer));
	close_sender();
	return status;
}

void payload_mm_authvar_presence_handoff_abort(void)
{
	uint8_t state = __atomic_load_n(&sender.state, __ATOMIC_ACQUIRE);

	for (;;) {
		uint8_t target;

		if (state == HANDOFF_TERMINAL || state == HANDOFF_ABORT_REQUESTED ||
		    state == HANDOFF_DELIVERING)
			return;
		target = state == HANDOFF_PROVISIONING || state == HANDOFF_TAKING ?
			HANDOFF_ABORT_REQUESTED : HANDOFF_TERMINAL;
		if (__atomic_compare_exchange_n(&sender.state, &state, target, false,
			__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			if (target == HANDOFF_TERMINAL)
				close_sender();
			return;
		}
	}
}

#if ENV_TEST
void payload_mm_authvar_presence_handoff_sender_reset_test(void)
{
	scrub(&sender, sizeof(sender));
}

bool payload_mm_authvar_presence_handoff_sender_scrubbed_test(void)
{
	return zero(sender.mailbox_signer.secret,
			sizeof(sender.mailbox_signer.secret)) &&
		!sender.mailbox_signer.generation &&
		zero(&sender.mailbox_signer.handle,
			sizeof(sender.mailbox_signer.handle)) &&
		!sender.mailbox_signer.sequence && !sender.mailbox_signer.boot_kind &&
		zero(sender.mailbox_signer.reserved,
			sizeof(sender.mailbox_signer.reserved)) &&
		zero(sender.transfer_signer.secret,
			sizeof(sender.transfer_signer.secret)) &&
		!sender.transfer_signer.generation &&
		zero(&sender.transfer_signer.handle,
			sizeof(sender.transfer_signer.handle)) &&
		!sender.transfer_signer.sequence && !sender.transfer_signer.boot_kind &&
		zero(sender.transfer_signer.reserved,
			sizeof(sender.transfer_signer.reserved)) &&
		zero(&sender.mailbox_handle, sizeof(sender.mailbox_handle)) &&
		zero(&sender.transfer_handle, sizeof(sender.transfer_handle)) &&
		!sender.generation && !sender.identity && !sender.owner_cpu &&
		!sender.maximum_cpus && zero(sender.capability,
			sizeof(sender.capability)) && sender.state == HANDOFF_TERMINAL;
}
#endif
