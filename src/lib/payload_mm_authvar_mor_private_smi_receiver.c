/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar.h>
#include <boot/payload_mm_authvar_executor.h>
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <boot/payload_mm_authvar_service.h>
#include <cpu/x86/save_state.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "MOR private SMI receiver is SMM-only"
#endif

#define CHANNEL_READY 0xa5U
#define CHANNEL_TAKING 0x5aU
#define CHANNEL_TERMINAL 0xc3U

struct receiver_policy {
	struct payload_mm_authvar_mor_private_smi_slot *slot;
	struct payload_mm_authvar_mor_seal_channel seal;
	uint64_t page_base;
	uint64_t generation;
	uint64_t identity;
	uint64_t cookie;
	uint32_t owner_cpu;
	uint32_t maximum_cpus;
	uint8_t capability[PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CAPABILITY_SIZE];
	payload_mm_authvar_mor_seal_range_check protected_storage;
};

static struct {
	struct receiver_policy policy;
	struct receiver_policy sealed;
	uint8_t install_attempted;
	uint8_t installed;
	uint8_t request_attempted;
} receiver;

static __attribute__((noinline)) void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!object_valid(first, first_size, 1) ||
	    !object_valid(second, second_size, 1))
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static bool policy_equal(void)
{
	return !memcmp(&receiver.policy, &receiver.sealed,
		sizeof(receiver.policy));
}

static bool slot_equal(const struct payload_mm_authvar_mor_private_smi_slot *slot,
	const struct payload_mm_authvar_mor_private_smi_slot *snapshot)
{
	return !memcmp(slot, snapshot, sizeof(*slot));
}

static __attribute__((unused)) bool grant_storage_protected(void *context,
	const void *base, size_t size)
{
	(void)context;
	return policy_equal() && receiver.sealed.protected_storage &&
		receiver.sealed.protected_storage(base, size);
}

static void terminal_close(void)
{
	struct receiver_policy policy = receiver.sealed;
	bool stable_targets = policy.slot && policy.protected_storage &&
		policy.slot == receiver.policy.slot &&
		policy.page_base == receiver.policy.page_base &&
		policy.protected_storage == receiver.policy.protected_storage;
	bool slot_safe = stable_targets &&
		policy.protected_storage(policy.slot, sizeof(*policy.slot));

	if (slot_safe) {
		bootmem_reservation_receipt_close(&policy.slot->verifier);
		scrub(policy.slot, sizeof(*policy.slot));
		__atomic_store_n(&policy.slot->state, CHANNEL_TERMINAL,
			__ATOMIC_RELEASE);
	}
	if (stable_targets &&
	    !(policy.page_base % PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE) &&
	    policy.page_base <= UINTPTR_MAX)
		scrub((void *)(uintptr_t)policy.page_base,
			PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE);
	(void)payload_mm_authvar_mor_grant_discard(grant_storage_protected,
		NULL, 0);
	__atomic_store_n(&receiver.install_attempted, 1, __ATOMIC_RELEASE);
	receiver.installed = false;
	receiver.request_attempted = true;
	scrub(&receiver.policy, sizeof(receiver.policy));
	scrub(&receiver.sealed, sizeof(receiver.sealed));
	scrub(&policy, sizeof(policy));
}

static void close_seal_channel(struct receiver_policy *policy)
{
	struct payload_mm_authvar_mor_seal_request *seal =
		(void *)(uintptr_t)policy->seal.transport_base;

	memset(seal, 0, sizeof(*seal));
	seal->revision = PAYLOAD_MM_AUTHVAR_MOR_SEAL_REVISION;
	seal->size = sizeof(*seal);
	seal->command = PAYLOAD_MM_AUTHVAR_MOR_SEAL_CLOSE;
	memcpy(seal->capability, policy->capability, sizeof(seal->capability));
	(void)payload_mm_authvar_mor_seal_receive(seal, sizeof(*seal),
		policy->seal.caller, policy->seal.caller_context);
}

enum cb_err payload_mm_authvar_mor_private_smi_channel_install(
	struct payload_mm_authvar_mor_private_smi_slot *slot,
	const struct payload_mm_authvar_mor_seal_channel *seal_channel,
	payload_mm_authvar_mor_seal_range_check protected_storage)
{
	struct payload_mm_authvar_mor_seal_channel seal;
	struct payload_mm_authvar_mor_private_smi_slot slot_snapshot;
	struct receiver_policy candidate = { 0 };
	uint8_t expected = CHANNEL_READY;
	bool slot_claimed = false;

	if (__atomic_exchange_n(&receiver.install_attempted, 1,
		__ATOMIC_ACQ_REL))
		return CB_ERR;
	if (!object_valid(slot, sizeof(*slot), _Alignof(*slot)) ||
	    !object_valid(seal_channel, sizeof(*seal_channel),
		_Alignof(*seal_channel)) ||
	    !protected_storage ||
	    ranges_overlap(slot, sizeof(*slot), seal_channel,
		sizeof(*seal_channel)) ||
	    ranges_overlap(slot, sizeof(*slot), &receiver, sizeof(receiver)) ||
	    ranges_overlap(seal_channel, sizeof(*seal_channel), &receiver,
		sizeof(receiver)) ||
	    !__atomic_compare_exchange_n(&slot->state, &expected, CHANNEL_TAKING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto fail;
	slot_claimed = true;
	memcpy(&seal, seal_channel, sizeof(seal));
	memcpy(&slot_snapshot, slot, sizeof(slot_snapshot));
	if (!protected_storage(slot, sizeof(*slot)) ||
	    !slot_equal(slot, &slot_snapshot) ||
	    memcmp(&seal, seal_channel, sizeof(seal)) ||
	    !protected_storage(seal_channel, sizeof(*seal_channel)) ||
	    !slot_equal(slot, &slot_snapshot) ||
	    memcmp(&seal, seal_channel, sizeof(seal)) ||
	    !protected_storage(&receiver, sizeof(receiver)) ||
	    !slot_equal(slot, &slot_snapshot) ||
	    memcmp(&seal, seal_channel, sizeof(seal)) ||
	    !protected_storage((const void *)protected_storage, 1) ||
	    !slot_equal(slot, &slot_snapshot) ||
	    memcmp(&seal, seal_channel, sizeof(seal)))
		goto fail;
	if (!slot->cold_boot_generation || !slot->channel_identity ||
	    !slot->descriptor_cookie || slot->owner_cpu ||
	    slot->maximum_cpus != CONFIG_MAX_CPUS ||
	    zero(slot->capability, sizeof(slot->capability)) ||
	    seal.transport_size != sizeof(struct payload_mm_authvar_mor_seal_request) ||
	    seal.transport_base <
		offsetof(struct payload_mm_authvar_mor_private_smi_request, seal) ||
	    (seal.transport_base -
		offsetof(struct payload_mm_authvar_mor_private_smi_request, seal)) %
		PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE ||
	    seal.caller != slot->channel_identity ||
	    seal.caller_context != slot->descriptor_cookie ||
	    memcmp(seal.capability, slot->capability, sizeof(seal.capability)))
		goto fail;
	candidate.slot = slot;
	candidate.seal = seal;
	candidate.page_base = seal.transport_base -
		offsetof(struct payload_mm_authvar_mor_private_smi_request, seal);
	candidate.generation = slot->cold_boot_generation;
	candidate.identity = slot->channel_identity;
	candidate.cookie = slot->descriptor_cookie;
	candidate.owner_cpu = slot->owner_cpu;
	candidate.maximum_cpus = slot->maximum_cpus;
	memcpy(candidate.capability, slot->capability,
		sizeof(candidate.capability));
	candidate.protected_storage = protected_storage;
	if (candidate.page_base > UINTPTR_MAX ||
	    candidate.cookie != payload_mm_authvar_mor_private_smi_cookie(
		candidate.identity, candidate.page_base, candidate.generation,
		candidate.owner_cpu, candidate.maximum_cpus) ||
	    ranges_overlap((const void *)(uintptr_t)candidate.page_base,
		PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
		slot, sizeof(*slot)) ||
	    ranges_overlap((const void *)(uintptr_t)candidate.page_base,
		PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
		seal_channel, sizeof(*seal_channel)) ||
	    ranges_overlap((const void *)(uintptr_t)candidate.page_base,
		PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
		&receiver, sizeof(receiver)))
		goto fail;
	receiver.policy = candidate;
	receiver.sealed = candidate;
	if (!policy_equal() || !slot_equal(slot, &slot_snapshot) ||
	    memcmp(&seal, seal_channel, sizeof(seal)) ||
	    !protected_storage(&receiver, sizeof(receiver)) || !policy_equal() ||
	    !slot_equal(slot, &slot_snapshot) ||
	    memcmp(&seal, seal_channel, sizeof(seal)) ||
	    !protected_storage(slot, sizeof(*slot)) || !policy_equal() ||
	    !slot_equal(slot, &slot_snapshot) ||
	    memcmp(&seal, seal_channel, sizeof(seal)))
		goto fail;
	__atomic_store_n(&slot->state, CHANNEL_READY, __ATOMIC_RELEASE);
	receiver.installed = true;
	scrub(&candidate, sizeof(candidate));
	scrub(&seal, sizeof(seal));
	scrub(&slot_snapshot, sizeof(slot_snapshot));
	return CB_SUCCESS;

fail:
	if (candidate.page_base &&
	    !(candidate.page_base % PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE))
		scrub((void *)(uintptr_t)candidate.page_base,
			PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE);
	scrub(&candidate, sizeof(candidate));
	scrub(&seal, sizeof(seal));
	scrub(&slot_snapshot, sizeof(slot_snapshot));
	if (slot_claimed) {
		bootmem_reservation_receipt_close(&slot->verifier);
		scrub(slot, sizeof(*slot));
		__atomic_store_n(&slot->state, CHANNEL_TERMINAL,
			__ATOMIC_RELEASE);
	}
	terminal_close();
	return CB_ERR;
}

static enum cb_err receive(uint64_t identity, uint64_t page_base,
	uint64_t cookie, unsigned int cpu,
	uint64_t *result)
{
	struct receiver_policy policy;
	struct payload_mm_authvar_mor_private_smi_request candidate;
	struct payload_mm_authvar_mor_private_smi_request *request;
	enum cb_err status = CB_ERR;

	*result = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED;
	if (!receiver.installed || receiver.request_attempted)
		return CB_ERR;
	if (!policy_equal()) {
		terminal_close();
		return CB_ERR;
	}
	receiver.request_attempted = true;
	policy = receiver.sealed;
	request = (void *)(uintptr_t)policy.page_base;
	memcpy(&candidate, request, sizeof(candidate));
	if (identity != policy.identity || page_base != policy.page_base ||
	    cookie != policy.cookie || cpu != policy.owner_cpu ||
	    policy.maximum_cpus != CONFIG_MAX_CPUS ||
	    candidate.revision != PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REVISION ||
	    candidate.size != sizeof(candidate) ||
	    candidate.cold_boot_generation != policy.generation ||
	    candidate.page_base != policy.page_base ||
	    candidate.page_size != PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE ||
	    candidate.channel_identity != policy.identity ||
	    candidate.descriptor_cookie != policy.cookie ||
	    candidate.owner_cpu != policy.owner_cpu ||
	    candidate.maximum_cpus != policy.maximum_cpus ||
	    memcmp(candidate.capability, policy.capability,
		sizeof(candidate.capability)) ||
	    candidate.receipt.base != policy.page_base ||
	    candidate.receipt.bytes != PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE ||
	    memcmp(&candidate, request, sizeof(candidate)) ||
	    !zero((const uint8_t *)request + sizeof(candidate),
		PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE - sizeof(candidate)) ||
	    bootmem_reservation_receipt_verify_consume(&policy.slot->verifier,
		&candidate.receipt) != CB_SUCCESS ||
	    !zero(&candidate.receipt, sizeof(candidate.receipt)) ||
	    memcmp(&candidate.seal, &request->seal, sizeof(candidate.seal)) ||
	    !policy_equal()) {
		close_seal_channel(&policy);
		goto out;
	}
	/* Verification consumed and scrubbed the receipt in the protected copy. */
	if (payload_mm_authvar_mor_seal_receive(&request->seal,
		sizeof(request->seal), identity, cookie) != CB_SUCCESS)
		goto out;
	if (candidate.seal.command == PAYLOAD_MM_AUTHVAR_MOR_SEAL_INSTALL) {
		if (payload_mm_authvar_mor_control_clear_transaction() !=
		    PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
			goto out;
	} else if (candidate.seal.command != PAYLOAD_MM_AUTHVAR_MOR_SEAL_CLOSE) {
		goto out;
	}
	status = CB_SUCCESS;
	*result = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_SUCCESS;

out:
	scrub(request, PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE);
	scrub(&candidate, sizeof(candidate));
	terminal_close();
	return status;
}

bool payload_mm_authvar_mor_private_smi_dispatch(unsigned int cpu)
{
	uintptr_t identity;
	uintptr_t page_base;
	uintptr_t cookie;
	uintptr_t result = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED;
	uint64_t receive_result = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED;
	int node;

	if (cpu >= CONFIG_MAX_CPUS || !receiver.installed ||
	    receiver.request_attempted)
		return false;
	node = (int)cpu;
	if (!policy_equal()) {
		terminal_close();
		(void)set_save_state_reg(RAX, node, &result, sizeof(result));
		return true;
	}
	if (get_save_state_reg(RBX, node, &identity, sizeof(identity)) ||
	    (uint64_t)identity != receiver.sealed.identity)
		return false;
	if (get_save_state_reg(RSI, node, &page_base, sizeof(page_base)) ||
	    get_save_state_reg(RDI, node, &cookie, sizeof(cookie))) {
		terminal_close();
		(void)set_save_state_reg(RAX, node, &result, sizeof(result));
		return true;
	}
	(void)receive((uint64_t)identity, (uint64_t)page_base,
		(uint64_t)cookie, cpu, &receive_result);
	result = (uintptr_t)receive_result;
	(void)set_save_state_reg(RAX, node, &result, sizeof(result));
	return true;
}

void payload_mm_authvar_mor_private_smi_channel_abort(void)
{
	if (receiver.installed && policy_equal()) {
		struct receiver_policy policy = receiver.sealed;

		close_seal_channel(&policy);
		scrub((void *)(uintptr_t)policy.page_base,
			PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE);
		scrub(&policy, sizeof(policy));
	}
	terminal_close();
}

#if ENV_TEST
enum cb_err payload_mm_authvar_mor_private_smi_test_receive(
	uint64_t identity, uint64_t page_base, uint64_t cookie,
	unsigned int cpu, uint64_t *status)
{
	if (!status)
		return CB_ERR_ARG;
	return receive(identity, page_base, cookie, cpu, status);
}

void payload_mm_authvar_mor_private_smi_test_mutate_policy(void)
{
	receiver.policy.generation++;
}
#endif
