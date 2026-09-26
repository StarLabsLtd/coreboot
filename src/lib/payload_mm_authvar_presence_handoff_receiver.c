/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_handoff.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Authenticated-variable presence handoff receiver is SMM-only"
#endif

#define HANDOFF_READY 0xa5U
#define HANDOFF_TAKING 0x5aU
#define HANDOFF_CLOSING 0x99U
#define HANDOFF_TERMINAL 0xc3U

enum receiver_phase {
	RECEIVER_EMPTY,
	RECEIVER_TAKING,
	RECEIVER_TERMINAL,
};

struct slot_metadata {
	uint64_t channel_generation;
	uint64_t channel_identity;
	uint32_t owner_cpu;
	uint32_t maximum_cpus;
	uint8_t channel_capability[
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CAPABILITY_SIZE];
};

static uint8_t receiver_phase;

#if ENV_TEST
void payload_mm_authvar_presence_handoff_test_after_receiver_claim(void);
void payload_mm_authvar_presence_handoff_test_during_receiver_cleanup(
	const struct payload_mm_authvar_presence_handoff_slot *slot);
#endif

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

__weak struct payload_mm_authvar_presence_handoff_slot *
smm_get_payload_mm_authvar_presence_handoff_slot(void)
{
	return NULL;
}

__weak enum cb_err platform_payload_mm_authvar_presence_handoff_receiver_install(
	const struct payload_mm_authvar_presence_seed *seed)
{
	(void)seed;
	return CB_ERR;
}

__weak bool platform_payload_mm_authvar_presence_handoff_range_valid(
	uint64_t base, uint64_t size)
{
	(void)base;
	(void)size;
	return false;
}

static struct slot_metadata slot_metadata(
	const struct payload_mm_authvar_presence_handoff_slot *slot)
{
	struct slot_metadata metadata = {
		.channel_generation = slot->channel_generation,
		.channel_identity = slot->channel_identity,
		.owner_cpu = slot->owner_cpu,
		.maximum_cpus = slot->maximum_cpus,
	};

	memcpy(metadata.channel_capability, slot->channel_capability,
		sizeof(metadata.channel_capability));
	return metadata;
}

static bool metadata_equal(
	const struct payload_mm_authvar_presence_handoff_slot *slot,
	const struct slot_metadata *metadata)
{
	const struct slot_metadata observed = slot_metadata(slot);

	return !memcmp(&observed, metadata, sizeof(observed));
}

static void terminal_close(
	struct payload_mm_authvar_presence_handoff_slot *slot)
{
	uint8_t expected = HANDOFF_TAKING;
	uint8_t phase_expected = RECEIVER_TAKING;

	if (!slot || !__atomic_compare_exchange_n(&slot->state, &expected,
		HANDOFF_CLOSING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto terminal_receiver;
#if ENV_TEST
	payload_mm_authvar_presence_handoff_test_during_receiver_cleanup(slot);
#endif
	bootmem_reservation_receipt_close(&slot->mailbox_verifier);
	bootmem_reservation_receipt_close(&slot->transfer_verifier);
	scrub(slot, offsetof(struct payload_mm_authvar_presence_handoff_slot, state));
	scrub(slot->reserved, sizeof(slot->reserved));
	__atomic_store_n(&slot->state, HANDOFF_TERMINAL, __ATOMIC_RELEASE);

terminal_receiver:
	(void)__atomic_compare_exchange_n(&receiver_phase, &phase_expected,
		RECEIVER_TERMINAL, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
}

static bool seed_valid(const struct payload_mm_authvar_presence_seed *seed,
	uint64_t mailbox_base, uint64_t generation)
{
	return seed->revision == PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION &&
		seed->size == sizeof(*seed) &&
		seed->endpoint.generation == generation &&
		seed->endpoint.communication_base == mailbox_base &&
		seed->endpoint.communication_size ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE &&
		payload_mm_authvar_presence_endpoint_validate(&seed->endpoint) ==
			CB_SUCCESS && nonzero(seed->capability, sizeof(seed->capability));
}

enum cb_err payload_mm_authvar_presence_handoff_receive(
	struct payload_mm_authvar_presence_handoff_slot *slot,
	const struct payload_mm_authvar_presence_handoff_descriptor *descriptor,
	unsigned int cpu, uint64_t *result)
{
	struct payload_mm_authvar_presence_handoff_request candidate = { 0 };
	struct payload_mm_authvar_presence_handoff_request observed = { 0 };
	struct payload_mm_authvar_presence_handoff_descriptor frozen = { 0 };
	struct payload_mm_authvar_presence_seed sealed_seed = { 0 };
	struct slot_metadata metadata = { 0 };
	uint8_t *request = NULL;
	uint8_t expected = HANDOFF_READY;
	uint8_t phase_expected = RECEIVER_EMPTY;
	bool authenticated = false;
	enum cb_err status = CB_ERR;

	if (!object_valid(slot, sizeof(*slot), _Alignof(*slot)) ||
	    !object_valid(descriptor, sizeof(*descriptor), _Alignof(*descriptor)) ||
	    !object_valid(result, sizeof(*result), _Alignof(*result)) ||
	    slot != smm_get_payload_mm_authvar_presence_handoff_slot() ||
	    !platform_payload_mm_authvar_presence_handoff_range_valid(
		(uintptr_t)descriptor, sizeof(*descriptor)) ||
	    !platform_payload_mm_authvar_presence_handoff_range_valid(
		(uintptr_t)result, sizeof(*result)) ||
	    overlap(slot, sizeof(*slot), descriptor, sizeof(*descriptor)) ||
	    overlap(slot, sizeof(*slot), result, sizeof(*result)) ||
	    overlap(slot, sizeof(*slot), &receiver_phase, sizeof(receiver_phase)) ||
	    overlap(descriptor, sizeof(*descriptor), result, sizeof(*result)) ||
	    overlap(descriptor, sizeof(*descriptor), &receiver_phase,
		sizeof(receiver_phase)) ||
	    overlap(result, sizeof(*result), &receiver_phase,
		sizeof(receiver_phase)))
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&receiver_phase, &phase_expected,
		RECEIVER_TAKING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
#if ENV_TEST
	payload_mm_authvar_presence_handoff_test_after_receiver_claim();
#endif
	if (!__atomic_compare_exchange_n(&slot->state, &expected, HANDOFF_TAKING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		terminal_close(NULL);
		return CB_ERR;
	}
	*result = PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CLOSED;
	frozen = *descriptor;
	metadata = slot_metadata(slot);
	if (descriptor->channel_identity != metadata.channel_identity ||
	    descriptor->channel_identity !=
		payload_mm_authvar_presence_handoff_identity(
			metadata.channel_capability, metadata.channel_generation) ||
	    !metadata.channel_generation || !metadata.channel_identity ||
	    metadata.owner_cpu || !metadata.maximum_cpus ||
	    metadata.maximum_cpus > CONFIG_MAX_CPUS ||
	    cpu != metadata.owner_cpu || cpu >= metadata.maximum_cpus ||
	    !nonzero(metadata.channel_capability,
		sizeof(metadata.channel_capability)) ||
	    descriptor->request_base > UINTPTR_MAX ||
	    descriptor->request_base % _Alignof(
		struct payload_mm_authvar_presence_handoff_request) ||
	    !platform_payload_mm_authvar_presence_handoff_range_valid(
		descriptor->request_base,
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE) ||
	    memcmp(&frozen, descriptor, sizeof(frozen)) ||
	    !metadata_equal(slot, &metadata) ||
	    __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != HANDOFF_TAKING)
		goto out;
	request = (void *)(uintptr_t)descriptor->request_base;
	if (overlap(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE,
		slot, sizeof(*slot)) ||
	    overlap(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE,
		descriptor, sizeof(*descriptor)) ||
	    overlap(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE,
		result, sizeof(*result)) ||
		overlap(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE,
		&receiver_phase, sizeof(receiver_phase)))
		goto out;
	memcpy(&candidate, request, sizeof(candidate));
	observed = candidate;
	if (candidate.transfer_base != descriptor->request_base ||
	    candidate.transfer_size !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE ||
	    candidate.transfer_receipt.base != candidate.transfer_base ||
	    candidate.transfer_receipt.bytes != candidate.transfer_size ||
	    bootmem_reservation_receipt_verify_consume_exact_tag(
		&slot->transfer_verifier, &candidate.transfer_receipt,
		BM_MEM_RESERVED) != CB_SUCCESS ||
	    !zero(&candidate.transfer_receipt,
		sizeof(candidate.transfer_receipt)))
		goto out;
	authenticated = true;
	if (candidate.mailbox_size != PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE ||
	    candidate.mailbox_receipt.base != candidate.mailbox_base ||
	    candidate.mailbox_receipt.bytes != candidate.mailbox_size ||
	    bootmem_reservation_receipt_verify_consume_exact_tag(
		&slot->mailbox_verifier, &candidate.mailbox_receipt,
		BM_MEM_RESERVED) != CB_SUCCESS ||
	    !zero(&candidate.mailbox_receipt, sizeof(candidate.mailbox_receipt)))
		goto out;
	if (__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != HANDOFF_TAKING ||
	    candidate.revision != PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_REVISION ||
	    candidate.size != sizeof(candidate) ||
	    candidate.channel_generation != metadata.channel_generation ||
	    candidate.channel_identity != metadata.channel_identity ||
	    candidate.descriptor_cookie != descriptor->descriptor_cookie ||
	    candidate.descriptor_cookie !=
		payload_mm_authvar_presence_handoff_cookie(
			metadata.channel_identity, descriptor->request_base,
			metadata.channel_generation, metadata.owner_cpu,
			metadata.maximum_cpus) ||
	    candidate.owner_cpu != metadata.owner_cpu ||
	    candidate.maximum_cpus != metadata.maximum_cpus ||
	    memcmp(candidate.channel_capability, metadata.channel_capability,
		sizeof(candidate.channel_capability)) ||
	    memcmp(&observed, request, sizeof(observed)) ||
	    !zero(request + sizeof(observed),
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE -
			sizeof(observed)) ||
	    memcmp(&frozen, descriptor, sizeof(frozen)) ||
	    !metadata_equal(slot, &metadata) ||
	    !seed_valid(&candidate.seed, candidate.mailbox_base,
		metadata.channel_generation))
		goto out;
	sealed_seed = candidate.seed;
	if (platform_payload_mm_authvar_presence_handoff_receiver_install(
		&candidate.seed) != CB_SUCCESS ||
	    memcmp(&candidate.seed, &sealed_seed, sizeof(sealed_seed)) ||
	    memcmp(&observed, request, sizeof(observed)) ||
	    memcmp(&frozen, descriptor, sizeof(frozen)) ||
	    !metadata_equal(slot, &metadata) ||
	    __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) != HANDOFF_TAKING)
		goto out;
	*result = PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_SUCCESS;
	status = CB_SUCCESS;

out:
	if (authenticated)
		scrub(request, PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE);
	terminal_close(slot);
	scrub(&candidate, sizeof(candidate));
	scrub(&observed, sizeof(observed));
	scrub(&frozen, sizeof(frozen));
	scrub(&sealed_seed, sizeof(sealed_seed));
	scrub(&metadata, sizeof(metadata));
	return status;
}

void payload_mm_authvar_presence_handoff_receiver_abort(void)
{
	struct payload_mm_authvar_presence_handoff_slot *slot =
		smm_get_payload_mm_authvar_presence_handoff_slot();
	uint8_t phase_expected = RECEIVER_EMPTY;
	uint8_t slot_expected = HANDOFF_READY;

	if (!__atomic_compare_exchange_n(&receiver_phase, &phase_expected,
		RECEIVER_TAKING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return;
	if (slot && __atomic_compare_exchange_n(&slot->state, &slot_expected,
		HANDOFF_TAKING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		terminal_close(slot);
	else
		terminal_close(NULL);
}

#if ENV_TEST
void payload_mm_authvar_presence_handoff_receiver_reset_test(void)
{
	scrub(&receiver_phase, sizeof(receiver_phase));
}

bool payload_mm_authvar_presence_handoff_slot_terminal_test(
	const struct payload_mm_authvar_presence_handoff_slot *slot)
{
	return slot && __atomic_load_n(&slot->state, __ATOMIC_ACQUIRE) ==
		HANDOFF_TERMINAL;
}
#endif
