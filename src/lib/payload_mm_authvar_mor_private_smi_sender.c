/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <bootmem.h>
#include <commonlib/helpers.h>
#if !ENV_TEST
#include <cpu/x86/lapic.h>
#include <cpu/x86/lapic_def.h>
#endif
#include <string.h>

#if !ENV_RAMSTAGE && !ENV_TEST
#error "MOR private SMI sender is ramstage-only"
#endif

#define CHANNEL_EMPTY 0U
#define CHANNEL_PROVISIONING 0x33U
#define CHANNEL_READY 0xa5U
#define CHANNEL_TERMINAL 0xc3U

static struct {
	struct bootmem_reservation_receipt_authority signer;
	struct bootmem_aligned_reservation_handle handle;
	uint64_t generation;
	uint64_t identity;
	uint64_t cookie;
	uint8_t capability[PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CAPABILITY_SIZE];
	uint8_t state;
} sender;

#if ENV_TEST
static payload_mm_authvar_mor_private_smi_test_trigger test_trigger;
static void *test_trigger_context;
void payload_mm_authvar_mor_private_smi_test_during_provision(void);
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

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
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

__weak bool platform_payload_mm_authvar_mor_private_smi_seed(
	struct payload_mm_authvar_mor_private_smi_seed *seed)
{
	if (seed)
		scrub(seed, sizeof(*seed));
	return false;
}

__weak bool platform_payload_mm_authvar_mor_private_smi_required(void)
{
	return true;
}

static void close_sender(void)
{
	bootmem_reservation_receipt_close(&sender.signer);
	scrub(&sender.handle, sizeof(sender.handle));
	scrub(sender.capability, sizeof(sender.capability));
	sender.generation = 0;
	sender.identity = 0;
	sender.cookie = 0;
	__atomic_store_n(&sender.state, CHANNEL_TERMINAL, __ATOMIC_RELEASE);
}

enum cb_err payload_mm_authvar_mor_private_smi_loader_provision(
	struct payload_mm_authvar_mor_private_smi_slot *slot, bool required)
{
	struct payload_mm_authvar_mor_private_smi_seed seed = { 0 };
	uint8_t expected = CHANNEL_EMPTY;
	enum cb_err status = CB_ERR;

	if (!object_valid(slot, sizeof(*slot), _Alignof(*slot)) ||
	    ranges_overlap(slot, sizeof(*slot), &sender, sizeof(sender)) ||
	    !__atomic_compare_exchange_n(&sender.state, &expected,
		CHANNEL_PROVISIONING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
#if ENV_TEST
	payload_mm_authvar_mor_private_smi_test_during_provision();
#endif
	memset(slot, 0, sizeof(*slot));
	if (!required) {
		close_sender();
		return CB_SUCCESS;
	}
	if (!platform_payload_mm_authvar_mor_private_smi_seed(&seed) ||
	    seed.revision != PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REVISION ||
	    seed.size != sizeof(seed) || !seed.cold_boot_generation ||
	    !seed.page_handle.opaque[0] || !seed.page_handle.opaque[1] ||
	    !nonzero(seed.capability, sizeof(seed.capability)) ||
	    !nonzero(seed.receipt_secret, sizeof(seed.receipt_secret)) ||
	    seed.reserved[0] || seed.reserved[1])
		goto out;
	sender.handle = seed.page_handle;
	sender.generation = seed.cold_boot_generation;
	memcpy(sender.capability, seed.capability, sizeof(sender.capability));
	sender.identity = payload_mm_authvar_mor_private_smi_identity(
		sender.capability, sender.generation);
	if (bootmem_reservation_receipt_provision(&sender.signer,
		&slot->verifier, seed.receipt_secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, sender.generation,
		&sender.handle) != CB_SUCCESS)
		goto out;
	slot->cold_boot_generation = sender.generation;
	slot->channel_identity = sender.identity;
	slot->descriptor_cookie = 0;
	slot->owner_cpu = 0;
	slot->maximum_cpus = CONFIG_MAX_CPUS;
	memcpy(slot->capability, sender.capability, sizeof(slot->capability));
	__atomic_store_n(&slot->state, CHANNEL_READY, __ATOMIC_RELEASE);
	expected = CHANNEL_PROVISIONING;
	if (!__atomic_compare_exchange_n(&sender.state, &expected, CHANNEL_READY,
		false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto out;
	status = CB_SUCCESS;

out:
	scrub(&seed, sizeof(seed));
	if (status != CB_SUCCESS) {
		bootmem_reservation_receipt_close(&slot->verifier);
		scrub(slot, sizeof(*slot));
		close_sender();
	}
	return status;
}

static enum cb_err resolve(struct bootmem_aligned_reservation *page)
{
	if (!sender.generation || !sender.identity ||
	    bootmem_aligned_reservation_query(&sender.handle, page) ||
	    page->tag != BM_MEM_TABLE ||
	    page->size != PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE ||
	    page->base % PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE ||
	    page->base > UINTPTR_MAX ||
	    page->base > UINT64_MAX - page->size)
		return CB_ERR;
	sender.cookie = payload_mm_authvar_mor_private_smi_cookie(sender.identity,
		page->base, sender.generation, 0, CONFIG_MAX_CPUS);
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_private_smi_seal_channel_resolve(
	struct payload_mm_authvar_mor_seal_channel *channel)
{
	struct bootmem_aligned_reservation page = { 0 };
	struct payload_mm_authvar_mor_seal_channel candidate = { 0 };

	if (!object_valid(channel, sizeof(*channel), _Alignof(*channel)) ||
	    __atomic_load_n(&sender.state, __ATOMIC_ACQUIRE) != CHANNEL_READY ||
	    resolve(&page) != CB_SUCCESS ||
	    ranges_overlap(channel, sizeof(*channel), &sender, sizeof(sender)) ||
	    ranges_overlap(channel, sizeof(*channel),
		(const void *)(uintptr_t)page.base, page.size))
		return CB_ERR;
	candidate.transport_base = page.base +
		offsetof(struct payload_mm_authvar_mor_private_smi_request, seal);
	candidate.transport_size = sizeof(struct payload_mm_authvar_mor_seal_request);
	candidate.caller = sender.identity;
	candidate.caller_context = sender.cookie;
	memcpy(candidate.capability, sender.capability,
		sizeof(candidate.capability));
	*channel = candidate;
	return CB_SUCCESS;
}

static enum cb_err trigger(uint64_t identity, uint64_t page_base,
	uint64_t cookie, uint64_t *status)
{
#if ENV_TEST
	const struct payload_mm_authvar_mor_private_smi_descriptor descriptor = {
		.identity = identity,
		.page_base = page_base,
		.cookie = cookie,
	};
	if (!test_trigger)
		return CB_ERR;
	return test_trigger(&descriptor, status, test_trigger_context);
#else
	uintptr_t result;
	const uintptr_t owner = (uintptr_t)identity;
	const uintptr_t page = (uintptr_t)page_base;
	const uintptr_t descriptor_cookie = (uintptr_t)cookie;
	const uint32_t command = LAPIC_INT_ASSERT | LAPIC_MT_SMI;
	const uint32_t apic = lapicid();

	if (is_x2apic_mode()) {
		__asm__ __volatile__("wrmsr"
			: "=a" (result)
			: "0" (command), "b" (owner),
			  "S" (page), "D" (descriptor_cookie),
			  "c" (X2APIC_MSR_ICR_ADDRESS), "d" (apic)
			: "memory");
	} else {
		volatile uint32_t *const icr = (void *)(uintptr_t)
			(LAPIC_DEFAULT_BASE + LAPIC_ICR);

		xapic_write(LAPIC_ICR2, SET_LAPIC_DEST_FIELD(apic));
		result = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REJECTED;
		__asm__ __volatile__("movl %%ecx, (%%edx)"
			: "+a" (result), [icr] "=m" (*icr)
			: "b" (owner), "S" (page), "D" (descriptor_cookie),
			  "c" (command), "d" (icr)
			: "memory");
	}
	*status = (uint64_t)result;
	return CB_SUCCESS;
#endif
}

static enum cb_err send(uint32_t command,
	const struct payload_mm_authvar_mor_grant *grant)
{
	struct bootmem_aligned_reservation page = { 0 };
	struct payload_mm_authvar_mor_private_smi_request candidate = { 0 };
	struct payload_mm_authvar_mor_private_smi_request *request;
	uint8_t expected = CHANNEL_READY;
	uint64_t status = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REJECTED;
	enum cb_err result = CB_ERR;

	if ((command != PAYLOAD_MM_AUTHVAR_MOR_SEAL_INSTALL &&
	     command != PAYLOAD_MM_AUTHVAR_MOR_SEAL_CLOSE) ||
	    (command == PAYLOAD_MM_AUTHVAR_MOR_SEAL_INSTALL &&
	     !object_valid(grant, sizeof(*grant), _Alignof(*grant))))
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&sender.state, &expected, CHANNEL_TERMINAL,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (resolve(&page) != CB_SUCCESS) {
		close_sender();
		return CB_ERR;
	}
	request = (void *)(uintptr_t)page.base;
	if (grant && (ranges_overlap(grant, sizeof(*grant), request, page.size) ||
	    ranges_overlap(grant, sizeof(*grant), &sender, sizeof(sender))))
		goto scrub_page;
	candidate.revision = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REVISION;
	candidate.size = sizeof(candidate);
	candidate.cold_boot_generation = sender.generation;
	candidate.page_base = page.base;
	candidate.page_size = page.size;
	candidate.channel_identity = sender.identity;
	candidate.descriptor_cookie = sender.cookie;
	candidate.owner_cpu = 0;
	candidate.maximum_cpus = CONFIG_MAX_CPUS;
	memcpy(candidate.capability, sender.capability,
		sizeof(candidate.capability));
	if (bootmem_aligned_reservation_receipt_emit(&sender.handle,
		&sender.signer, &candidate.receipt) != CB_SUCCESS)
		goto scrub_page;
	candidate.seal.revision = PAYLOAD_MM_AUTHVAR_MOR_SEAL_REVISION;
	candidate.seal.size = sizeof(candidate.seal);
	candidate.seal.command = command;
	memcpy(candidate.seal.capability, sender.capability,
		sizeof(candidate.seal.capability));
	if (grant)
		memcpy(&candidate.seal.grant, grant,
			sizeof(candidate.seal.grant));
	memset(request, 0, page.size);
	memcpy(request, &candidate, sizeof(candidate));
	if (memcmp(request, &candidate, sizeof(candidate)) ||
	    trigger(sender.identity, page.base, sender.cookie, &status) != CB_SUCCESS ||
	    status != PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_SUCCESS ||
	    !zero(request, page.size))
		goto scrub_page;
	result = CB_SUCCESS;

scrub_page:
	scrub(request, page.size);
	scrub(&candidate, sizeof(candidate));
	scrub(&page, sizeof(page));
	close_sender();
	return result;
}

enum cb_err payload_mm_authvar_mor_private_smi_send_install(
	const struct payload_mm_authvar_mor_grant *grant)
{
	return send(PAYLOAD_MM_AUTHVAR_MOR_SEAL_INSTALL, grant);
}

enum cb_err payload_mm_authvar_mor_private_smi_close_unused(void)
{
	const enum cb_err status = send(PAYLOAD_MM_AUTHVAR_MOR_SEAL_CLOSE, NULL);

	return status;
}

#if ENV_TEST
void payload_mm_authvar_mor_private_smi_test_set_trigger(
	payload_mm_authvar_mor_private_smi_test_trigger callback, void *context)
{
	test_trigger = callback;
	test_trigger_context = context;
}
#endif
