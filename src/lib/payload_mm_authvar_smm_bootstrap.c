/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar.h>
#include <boot/payload_mm_authvar_executor.h>
#include <boot/payload_mm_authvar_mor_seal.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI)
#include <boot/payload_mm_authvar_mor_private_smi.h>
#endif
#include <boot/payload_mm_authvar_smm_bootstrap.h>
#include <boot/payload_mm_authvar_smm_loader.h>
#include <boot/payload_mm_authvar_store.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_service_receiver.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_runtime.h>
#endif
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <smmstore.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable bootstrap is SMM-only"
#endif

#if CONFIG(SMMSTORE) || CONFIG(SMMSTORE_FULL_FLASH_ACCESS)
#error "Authenticated-variable bootstrap forbids the raw SMMSTORE transport"
#endif

#define MINIMUM_RECORD_SIZE 64U

_Static_assert(MINIMUM_RECORD_SIZE ==
	ALIGN_UP(PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + 4U, 4U),
	"minimum authenticated-variable record size changed");

struct bootstrap_policy {
	uint64_t generation;
	uint32_t service;
	struct payload_mm_authvar_smm_arena_receipt receipt;
	struct payload_mm_authvar_range smram;
	struct payload_mm_authvar_range communication;
	struct payload_mm_authvar_range arena;
	struct payload_mm_authvar_range store;
	uint64_t boot_media_size;
	uint32_t block_size;
	uint32_t erase_size;
	struct payload_mm_authvar_executor_limits limits;
	struct payload_mm_authvar_mor_seal_channel channel;
	struct payload_mm_authvar_smm_media_ops media;
	payload_mm_authvar_smm_spi_restricted spi_restricted;
	void *spi_context;
	size_t spi_context_size;
};

#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
enum service_phase {
	SERVICE_EMPTY,
	SERVICE_PREPARING,
	SERVICE_PREPARE_CLOSE_REQUESTED,
	SERVICE_PREPARED,
	SERVICE_INSTALLING,
	SERVICE_INSTALLED,
	SERVICE_FAILED,
};

struct service_communication {
	struct payload_mm_authvar_range range;
	uint64_t generation;
	uint8_t owner[PAYLOAD_MM_AUTHVAR_SMM_ARENA_OWNER_SIZE];
};

enum endpoint_phase {
	ENDPOINT_EMPTY,
	ENDPOINT_FINALIZING,
	ENDPOINT_READY,
	ENDPOINT_EXECUTING,
	ENDPOINT_FAILED,
};
#endif

static struct {
	struct bootstrap_policy policy;
	struct bootstrap_policy sealed;
	uint32_t attempted;
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	struct service_communication service;
	struct service_communication sealed_service;
	uint32_t service_phase;
	uint32_t endpoint_phase;
	struct lb_authvar_service_endpoint endpoint;
	struct lb_authvar_service_endpoint sealed_endpoint;
	/* These are separate from the reusable executor arena and private policy. */
	uint8_t service_request[PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE] __aligned(8);
	uint8_t service_response[PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE] __aligned(8);
#endif
} provider;

static bool spans_overlap(uint64_t left, uint64_t left_size, uint64_t right,
	uint64_t right_size)
{
	uint64_t left_end;
	uint64_t right_end;

	if (!left_size || left > UINT64_MAX - left_size || !right_size ||
	    right > UINT64_MAX - right_size)
		return true;
	left_end = left + left_size;
	right_end = right + right_size;
	return left < right_end && right < left_end;
}

static bool span_within(uint64_t base, uint64_t size, uint64_t outer_base,
	uint64_t outer_size)
{
	uint64_t end;
	uint64_t outer_end;

	return size && base <= UINT64_MAX - size &&
		outer_base <= UINT64_MAX - outer_size &&
		(end = base + size) >= base &&
		(outer_end = outer_base + outer_size) >= outer_base &&
		base >= outer_base && end <= outer_end;
}

static bool policy_equal(void)
{
	if (memcmp(&provider.policy, &provider.sealed, sizeof(provider.policy)))
		return false;
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	uint32_t phase = __atomic_load_n(&provider.service_phase, __ATOMIC_ACQUIRE);

	if (provider.sealed.service) {
		const struct payload_mm_authvar_presence_transaction_binding *binding;

		return provider.sealed.service == 1 &&
			(phase == SERVICE_INSTALLING || phase == SERVICE_INSTALLED) &&
			!memcmp(&provider.service, &provider.sealed_service,
			sizeof(provider.service)) &&
			!memcmp(&provider.sealed.communication, &provider.sealed_service.range,
				sizeof(provider.sealed.communication)) &&
			provider.sealed.generation == provider.sealed_service.generation &&
			payload_mm_authvar_presence_bootstrap_binding_get(&binding) == CB_SUCCESS &&
			binding->generation == provider.sealed_service.generation &&
			!memcmp(binding->capability, provider.sealed_service.owner,
				sizeof(provider.sealed_service.owner)) &&
			(phase != SERVICE_INSTALLING ||
			 (span_within((uintptr_t)platform_payload_mm_authvar_service_bootstrap_admitted,
				1, provider.sealed.smram.base, provider.sealed.smram.size) &&
			  platform_payload_mm_authvar_service_bootstrap_admitted()));
	}
#endif
	return true;
}

static bool policy_matches(const struct bootstrap_policy *snapshot)
{
	return snapshot && policy_equal() &&
		!memcmp(snapshot, &provider.sealed, sizeof(*snapshot));
}

static void scrub(void *buffer, size_t size);

static bool media_ops_valid(const struct payload_mm_authvar_smm_media_ops *ops)
{
	return ops->revision == PAYLOAD_MM_AUTHVAR_SMM_MEDIA_REVISION &&
		ops->size == sizeof(*ops) && ops->facts && ops->install &&
		!ops->reserved[0] && !ops->reserved[1] &&
		(!!ops->context == !!ops->context_size);
}

static bool media_facts_valid(
	const struct payload_mm_authvar_smm_media_facts *facts,
	const struct region_device *store)
{
	return facts->revision == PAYLOAD_MM_AUTHVAR_SMM_MEDIA_REVISION &&
		facts->size == sizeof(*facts) && facts->boot_media_size &&
		facts->store_size && facts->block_size && facts->erase_size &&
		!facts->reserved[0] && !facts->reserved[1] &&
		facts->store_offset == region_device_offset(store) &&
		facts->store_size == region_device_sz(store) &&
		facts->store_offset <= facts->boot_media_size &&
		facts->store_size <= facts->boot_media_size - facts->store_offset;
}

static bool media_revalidate(const struct bootstrap_policy *snapshot)
{
	struct payload_mm_authvar_smm_media_facts facts = { 0 };
	const struct payload_mm_authvar_smm_media_ops media =
		provider.sealed.media;
	bool valid;

	if (!policy_matches(snapshot) || !media_ops_valid(&media))
		return false;
	valid = media.facts(media.context, &facts) == CB_SUCCESS &&
		facts.revision == PAYLOAD_MM_AUTHVAR_SMM_MEDIA_REVISION &&
		facts.size == sizeof(facts) &&
		facts.boot_media_size == provider.sealed.boot_media_size &&
		facts.store_offset == provider.sealed.store.base &&
		facts.store_size == provider.sealed.store.size &&
		facts.block_size == provider.sealed.block_size &&
		facts.erase_size == provider.sealed.erase_size &&
		!facts.reserved[0] && !facts.reserved[1] &&
		policy_matches(snapshot) &&
		!memcmp(&media, &provider.sealed.media, sizeof(media));
	scrub(&facts, sizeof(facts));
	return valid;
}

static bool bytes_nonzero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return combined;
}

static __attribute__((noinline)) void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
__weak bool platform_payload_mm_authvar_service_bootstrap_admitted(void)
{
	return false;
}

void payload_mm_authvar_service_prepare_abort(void)
{
	uint32_t phase = __atomic_load_n(&provider.service_phase, __ATOMIC_ACQUIRE);

	for (;;) {
		uint32_t replacement;

		if (phase == SERVICE_FAILED || phase == SERVICE_PREPARE_CLOSE_REQUESTED)
			return;
		/* Import has ended once the sole backend initializer owns this state. */
		if (phase == SERVICE_INSTALLING || phase == SERVICE_INSTALLED)
			return;
		/* The active preparer owns its writes and performs the terminal scrub. */
		replacement = phase == SERVICE_PREPARING ?
			SERVICE_PREPARE_CLOSE_REQUESTED : SERVICE_FAILED;
		if (!__atomic_compare_exchange_n(&provider.service_phase, &phase,
			replacement, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			continue;
		if (replacement == SERVICE_PREPARE_CLOSE_REQUESTED)
			return;
		scrub(&provider.service, sizeof(provider.service));
		scrub(&provider.sealed_service, sizeof(provider.sealed_service));
		scrub(provider.service_request, sizeof(provider.service_request));
		scrub(provider.service_response, sizeof(provider.service_response));
		return;
	}
}

enum cb_err payload_mm_authvar_service_prepare(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt)
{
	const struct smm_invocation_runtime_view *view;
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	const struct payload_mm_authvar_presence_transaction_binding *check;
	struct payload_mm_authvar_presence_transaction_binding identity;
	struct bootmem_reservation_receipt snapshot;
	uintptr_t smram_base;
	size_t smram_size;
	uint32_t expected = SERVICE_EMPTY;
	enum cb_err result = CB_ERR;

	if (!verifier || !receipt ||
	    (uintptr_t)verifier % _Alignof(*verifier) ||
	    (uintptr_t)receipt % _Alignof(*receipt) ||
	    smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(view, &provider,
		sizeof(provider)) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(view, verifier,
		sizeof(*verifier)) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(view, receipt,
		sizeof(*receipt)) != CB_SUCCESS ||
	    spans_overlap((uintptr_t)verifier, sizeof(*verifier),
		(uintptr_t)receipt, sizeof(*receipt)) ||
	    spans_overlap((uintptr_t)verifier, sizeof(*verifier),
		(uintptr_t)&provider, sizeof(provider)) ||
	    spans_overlap((uintptr_t)receipt, sizeof(*receipt),
		(uintptr_t)&provider, sizeof(provider)) ||
	    payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS ||
	    spans_overlap((uintptr_t)verifier, sizeof(*verifier),
		(uintptr_t)binding, sizeof(*binding)) ||
	    spans_overlap((uintptr_t)receipt, sizeof(*receipt),
		(uintptr_t)binding, sizeof(*binding)) ||
	    __atomic_load_n(&provider.attempted, __ATOMIC_ACQUIRE) ||
	    !__atomic_compare_exchange_n(&provider.service_phase, &expected,
		SERVICE_PREPARING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	identity = *binding;
	snapshot = *receipt;
	smm_region(&smram_base, &smram_size);
	if (!smram_size || !snapshot.base || (snapshot.base & 4095U) ||
	    snapshot.base > UINT32_MAX - (PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE - 1U) ||
	    snapshot.bytes != PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE ||
	    snapshot.generation != identity.generation || snapshot.tag != BM_MEM_TABLE ||
	    spans_overlap(snapshot.base, snapshot.bytes, smram_base, smram_size) ||
	    bootmem_reservation_receipt_verify_consume_exact_tag(verifier, receipt,
		BM_MEM_TABLE) != CB_SUCCESS ||
	    bytes_nonzero(receipt, sizeof(*receipt)) ||
	    payload_mm_authvar_presence_bootstrap_binding_get(&check) != CB_SUCCESS ||
	    memcmp(&identity, check, sizeof(identity)))
		goto cleanup;
	provider.service = (struct service_communication) {
		.range = { .base = snapshot.base, .size = snapshot.bytes },
		.generation = identity.generation,
	};
	memcpy(provider.service.owner, identity.capability, sizeof(provider.service.owner));
	provider.sealed_service = provider.service;
	expected = SERVICE_PREPARING;
	if (!__atomic_compare_exchange_n(&provider.service_phase, &expected,
		SERVICE_PREPARED, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto cleanup;
	result = CB_SUCCESS;
cleanup:
	if (result != CB_SUCCESS) {
		bootmem_reservation_receipt_close(verifier);
		scrub(receipt, sizeof(*receipt));
		scrub(&provider.service, sizeof(provider.service));
		scrub(&provider.sealed_service, sizeof(provider.sealed_service));
		__atomic_store_n(&provider.service_phase, SERVICE_FAILED, __ATOMIC_RELEASE);
	}
	scrub(&identity, sizeof(identity));
	scrub(&snapshot, sizeof(snapshot));
	return result;
}
#endif

#if ENV_TEST
void payload_mm_authvar_smm_bootstrap_scrub_observe(const void *buffer,
	size_t size);
#endif

static bool protected_storage(const void *base, size_t size)
{
	return base && policy_equal() && span_within((uintptr_t)base, size,
		provider.sealed.smram.base, provider.sealed.smram.size);
}

static bool authority_storage(void *unused, const void *base, size_t size)
{
	(void)unused;
	return protected_storage(base, size);
}

static bool fixed_transport(const void *base, size_t size)
{
	return base && policy_equal() &&
		(uintptr_t)base == provider.sealed.channel.transport_base &&
		size == provider.sealed.channel.transport_size &&
		!spans_overlap((uintptr_t)base, size,
			provider.sealed.smram.base, provider.sealed.smram.size);
}

static bool smm_entry_owned(void *unused)
{
	(void)unused;
	return policy_equal() && protected_storage(&provider, sizeof(provider));
}

static bool spi_writes_restricted(void *unused)
{
	payload_mm_authvar_smm_spi_restricted proof =
		provider.sealed.spi_restricted;
	void *context = provider.sealed.spi_context;
	bool restricted;

	(void)unused;
	if (!policy_equal() || !proof || !protected_storage((const void *)proof, 1) ||
	    (context && !protected_storage(context,
		provider.sealed.spi_context_size)))
		return false;
	restricted = proof(context);
	return restricted && policy_equal() &&
		proof == provider.sealed.spi_restricted &&
		context == provider.sealed.spi_context;
}

static bool raw_flash_transport_absent(void *unused)
{
	(void)unused;
	return policy_equal() && !CONFIG(SMMSTORE) &&
		!CONFIG(SMMSTORE_FULL_FLASH_ACCESS);
}

static bool communication_reserved(void *unused, uint64_t base, uint64_t size)
{
	(void)unused;
	return policy_equal() && base == provider.sealed.communication.base &&
		size == provider.sealed.communication.size && size <= SIZE_MAX &&
		!spans_overlap(base, size, provider.sealed.smram.base,
			provider.sealed.smram.size);
}

static bool store_owned(void *unused, uint64_t offset, uint64_t size)
{
	(void)unused;
	return policy_equal() && offset == provider.sealed.store.base &&
		size == provider.sealed.store.size;
}

static uint32_t aligned_record_size(uint32_t name_size, uint32_t data_size)
{
	uint64_t size = PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;

	size = (size + name_size + 3U) & ~3ULL;
	size = (size + data_size + 3U) & ~3ULL;
	return size <= UINT32_MAX ? (uint32_t)size : 0;
}

static bool limits_build(uint64_t store_size, uint32_t block_size,
	struct payload_mm_authvar_executor_limits *limits)
{
	uint32_t bounded_store;
	uint32_t name_size;
	uint32_t data_size;
	uint32_t record_size;
	uint32_t available;

	if (!limits || store_size > UINT32_MAX ||
	    !block_size ||
	    store_size < PAYLOAD_MM_AUTHVAR_MIN_STORE_BLOCKS * block_size ||
	    store_size % block_size)
		return false;
	bounded_store = (uint32_t)store_size;
	available = bounded_store - PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;
	name_size = MIN(available & ~1U,
		PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE);
	if (name_size < 4U)
		return false;
	available -= name_size;
	data_size = MIN(available, PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE);
	record_size = aligned_record_size(name_size, data_size);
	while (record_size > bounded_store && data_size) {
		data_size--;
		record_size = aligned_record_size(name_size, data_size);
	}
	if (!data_size || !record_size)
		return false;
	*limits = (struct payload_mm_authvar_executor_limits) {
		.maximum_store_size = bounded_store,
		.maximum_name_size = name_size,
		.maximum_data_size = data_size,
		.maximum_record_size = record_size,
		.maximum_records = bounded_store / MINIMUM_RECORD_SIZE,
	};
	return true;
}

enum cb_err payload_mm_authvar_smm_bootstrap_arena_size(uint64_t store_size,
	size_t *arena_size)
{
	struct payload_mm_authvar_executor_limits limits;

	if (!arena_size)
		return CB_ERR;
	*arena_size = 0;
	if (!limits_build(store_size, SMM_BLOCK_SIZE, &limits))
		return CB_ERR;
	return payload_mm_authvar_executor_required_size(&limits, arena_size);
}

static enum cb_err bootstrap_install(
	const struct payload_mm_authvar_smm_bootstrap *bootstrap, bool service)
{
	struct payload_mm_authvar_smm_bootstrap input = { 0 };
	struct payload_mm_authvar_smm_media_ops media = { 0 };
	struct payload_mm_authvar_smm_media_facts facts = { 0 };
	struct payload_mm_authvar_contract contract;
	struct payload_mm_authvar_platform platform;
	struct bootstrap_policy frozen = { 0 };
	struct payload_mm_authvar_smm_arena_receipt receipt = { 0 };
	struct region_device store;
	uintptr_t smram_base;
	size_t smram_size;
	size_t required_size;
	uint32_t expected = 0;
	enum cb_err result = CB_ERR;
	bool owns_attempt = false;
	bool have_channel = false;
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	struct service_communication communication = { 0 };
#endif

	if (!__atomic_compare_exchange_n(&provider.attempted, &expected, 1, false,
		__ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		goto cleanup;
	owns_attempt = true;
	if (!bootstrap)
		goto cleanup;
	smm_region(&smram_base, &smram_size);
	if (!smram_size || !span_within((uintptr_t)bootstrap, sizeof(*bootstrap),
		smram_base, smram_size) || spans_overlap((uintptr_t)bootstrap,
		sizeof(*bootstrap), (uintptr_t)&provider, sizeof(provider)))
		goto cleanup;
	memcpy(&input, bootstrap, sizeof(input));
	have_channel = bytes_nonzero(&input.seal_channel, sizeof(input.seal_channel));
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	if (service) {
		expected = SERVICE_PREPARED;
		if (!__atomic_compare_exchange_n(&provider.service_phase, &expected,
			SERVICE_INSTALLING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
		    memcmp(&provider.service, &provider.sealed_service,
			sizeof(provider.service)) ||
		    payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS ||
		    provider.sealed_service.generation != binding->generation ||
		    memcmp(provider.sealed_service.owner, binding->capability,
			sizeof(provider.sealed_service.owner)) ||
		    !span_within((uintptr_t)platform_payload_mm_authvar_service_bootstrap_admitted,
			1, smram_base, smram_size) ||
		    !platform_payload_mm_authvar_service_bootstrap_admitted())
			goto cleanup;
		communication = provider.sealed_service;
	} else if (__atomic_load_n(&provider.service_phase, __ATOMIC_ACQUIRE) !=
		SERVICE_EMPTY)
		goto cleanup;
	else
#endif
	if (service || !have_channel)
		goto cleanup;
	if (input.revision != PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION ||
	    input.size != sizeof(input) || !input.cold_boot_generation ||
	    input.reserved[0] || input.reserved[1] ||
	    !input.spi_writes_restricted_to_smm ||
	    (!!input.spi_context != !!input.spi_context_size) ||
	    (have_channel && (input.seal_channel.transport_size !=
		sizeof(struct payload_mm_authvar_mor_seal_request) ||
	    input.seal_channel.transport_base %
		_Alignof(struct payload_mm_authvar_mor_seal_request) ||
	    !input.seal_channel.caller || !input.seal_channel.caller_context ||
	    !bytes_nonzero(input.seal_channel.capability,
		sizeof(input.seal_channel.capability)))) ||
	    !smm_take_payload_mm_authvar_arena_receipt(&receipt) ||
	    receipt.revision != PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION ||
	    receipt.size != sizeof(receipt) || receipt.reserved[0] ||
	    receipt.reserved[1] ||
	    receipt.cold_boot_generation != input.cold_boot_generation ||
	    !span_within(receipt.smram.base, receipt.smram.size,
		smram_base, smram_size) ||
	    !receipt.arena.size || receipt.arena.base % __BIGGEST_ALIGNMENT__ ||
	    !span_within(receipt.arena.base, receipt.arena.size,
		receipt.smram.base, receipt.smram.size) ||
	    memcmp(receipt.owner,
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
		service ? communication.owner :
#endif
		input.seal_channel.capability, sizeof(receipt.owner)) ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	    (service && input.cold_boot_generation != communication.generation) ||
#endif
	    smmstore_lookup_read_region(&store) < 0 ||
	    !platform_payload_mm_authvar_smm_media_ops(&media) ||
	    !media_ops_valid(&media) ||
	    !span_within((uintptr_t)media.facts, 1, smram_base, smram_size) ||
	    !span_within((uintptr_t)media.install, 1, smram_base, smram_size) ||
	    (media.context &&
	     !span_within((uintptr_t)media.context, media.context_size,
		smram_base, smram_size)) ||
	    (media.context &&
	     (spans_overlap((uintptr_t)media.context, media.context_size,
		(uintptr_t)bootstrap, sizeof(*bootstrap)) ||
	      spans_overlap((uintptr_t)media.context, media.context_size,
		(uintptr_t)&input, sizeof(input)) ||
	      spans_overlap((uintptr_t)media.context, media.context_size,
		(uintptr_t)&media, sizeof(media)) ||
	      spans_overlap((uintptr_t)media.context, media.context_size,
		(uintptr_t)&facts, sizeof(facts)) ||
	      spans_overlap((uintptr_t)media.context, media.context_size,
		(uintptr_t)&receipt, sizeof(receipt)))) ||
	    media.facts(media.context, &facts) != CB_SUCCESS ||
	    !media_ops_valid(&media) ||
	    !media_facts_valid(&facts, &store) ||
	    memcmp(&input, bootstrap, sizeof(input)))
		goto cleanup;
	provider.policy = (struct bootstrap_policy) {
		.generation = input.cold_boot_generation,
		.service = service ? 1 : 0,
		.receipt = receipt,
		.smram = { .base = smram_base, .size = smram_size },
		.communication = {
			/* Legacy MOR retains its original independent transport. */
			.base = input.seal_channel.transport_base,
			.size = input.seal_channel.transport_size,
		},
		.arena = receipt.arena,
		.store = {
			.base = region_device_offset(&store),
			.size = region_device_sz(&store),
		},
		.boot_media_size = facts.boot_media_size,
		.block_size = facts.block_size,
		.erase_size = facts.erase_size,
		.channel = input.seal_channel,
		.media = media,
		.spi_restricted = input.spi_writes_restricted_to_smm,
		.spi_context = input.spi_context,
		.spi_context_size = input.spi_context_size,
	};
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	if (service)
		provider.policy.communication = communication.range;
#endif
	if (!limits_build(provider.policy.store.size, provider.policy.block_size,
		&provider.policy.limits) ||
	    payload_mm_authvar_executor_required_size(&provider.policy.limits,
		&required_size) != CB_SUCCESS ||
	    required_size > provider.policy.arena.size)
		goto cleanup;
	provider.policy.arena.size = required_size;
	provider.sealed = provider.policy;
	frozen = provider.sealed;
	if (!policy_matches(&frozen) || memcmp(&input, bootstrap, sizeof(input)) ||
	    !protected_storage(&provider, sizeof(provider)) ||
	    !protected_storage(bootstrap, sizeof(*bootstrap)) ||
	    !smm_payload_mm_authvar_arena_receipt_consumed() ||
	    !protected_storage((const void *)(uintptr_t)provider.sealed.arena.base,
		provider.sealed.arena.size) ||
	    !protected_storage((const void *)input.spi_writes_restricted_to_smm, 1) ||
	    !protected_storage((const void *)media.facts, 1) ||
	    !protected_storage((const void *)media.install, 1) ||
	    memcmp(&media, &provider.sealed.media, sizeof(media)) ||
	    (media.context && !protected_storage(media.context,
		media.context_size)) ||
	    (media.context &&
	     (spans_overlap((uintptr_t)media.context, media.context_size,
		(uintptr_t)&provider, sizeof(provider)) ||
	      spans_overlap((uintptr_t)media.context, media.context_size,
		provider.sealed.arena.base, provider.sealed.arena.size))) ||
	    (input.spi_context && !protected_storage(input.spi_context,
		input.spi_context_size)) ||
	    spans_overlap(provider.sealed.arena.base, provider.sealed.arena.size,
		(uintptr_t)&provider, sizeof(provider)) ||
	    spans_overlap(provider.sealed.arena.base, provider.sealed.arena.size,
		(uintptr_t)bootstrap, sizeof(*bootstrap)) ||
	    (input.spi_context &&
		(spans_overlap((uintptr_t)input.spi_context,
			input.spi_context_size,
			(uintptr_t)&provider, sizeof(provider)) ||
		 spans_overlap((uintptr_t)input.spi_context,
			input.spi_context_size,
			(uintptr_t)bootstrap, sizeof(*bootstrap)) ||
		 spans_overlap((uintptr_t)input.spi_context,
			input.spi_context_size, provider.sealed.arena.base,
			provider.sealed.arena.size))) ||
	    (have_channel && !fixed_transport((const void *)(uintptr_t)
		input.seal_channel.transport_base,
		(size_t)input.seal_channel.transport_size)))
		goto cleanup;
	platform = (struct payload_mm_authvar_platform) {
		.smm_address_bits = sizeof(uintptr_t) * 8U,
		.generation = provider.sealed.generation,
		.smram = provider.sealed.smram,
		.communication = provider.sealed.communication,
		.boot_media_size = provider.sealed.boot_media_size,
		.store_offset = provider.sealed.store.base,
		.store_size = provider.sealed.store.size,
		.block_size = provider.sealed.block_size,
		.erase_size = provider.sealed.erase_size,
		.smm_entry_owned = smm_entry_owned,
		.spi_writes_restricted_to_smm = spi_writes_restricted,
		.raw_flash_transport_absent = raw_flash_transport_absent,
		.communication_region_reserved = communication_reserved,
		.store_region_owned_by_smm = store_owned,
	};
	if (payload_mm_authvar_contract_build(&contract, &platform) != CB_SUCCESS ||
	    !policy_matches(&frozen) ||
	    !media_revalidate(&frozen) ||
	    !smm_payload_mm_authvar_arena_receipt_consumed() ||
	    memcmp(&input, bootstrap, sizeof(input)) ||
	    payload_mm_authvar_authority_install(&contract, authority_storage,
		NULL) != CB_SUCCESS || !policy_matches(&frozen) ||
	    !smm_payload_mm_authvar_arena_receipt_consumed() ||
	    memcmp(&input, bootstrap, sizeof(input)) ||
	    !media_revalidate(&frozen) ||
	    provider.sealed.media.install(provider.sealed.media.context) !=
		CB_SUCCESS ||
	    !policy_matches(&frozen) ||
	    !media_revalidate(&frozen) ||
	    !smm_payload_mm_authvar_arena_receipt_consumed() ||
	    memcmp(&input, bootstrap, sizeof(input)) ||
	    memcmp(&media, &provider.sealed.media, sizeof(media)) ||
	    payload_mm_authvar_executor_install(
		(void *)(uintptr_t)provider.sealed.arena.base,
		provider.sealed.arena.size,
		&provider.sealed.limits) != CB_SUCCESS ||
	    !policy_matches(&frozen) ||
	    !smm_payload_mm_authvar_arena_receipt_consumed() ||
	    memcmp(&input, bootstrap, sizeof(input)) ||
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI)
	    (have_channel && payload_mm_authvar_mor_private_smi_channel_install(
		smm_get_payload_mm_authvar_mor_private_smi_slot(),
		&provider.sealed.channel, protected_storage) != CB_SUCCESS) ||
	    !policy_matches(&frozen) ||
	    memcmp(&input, bootstrap, sizeof(input)) ||
#endif
	    (have_channel && payload_mm_authvar_mor_seal_channel_install(&provider.sealed.channel,
		protected_storage, fixed_transport) != CB_SUCCESS) ||
	    !policy_matches(&frozen) ||
	    !smm_payload_mm_authvar_arena_receipt_consumed() ||
	    memcmp(&input, bootstrap, sizeof(input)))
		goto cleanup;
	result = CB_SUCCESS;
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	if (service) {
		if (memcmp(&provider.service, &communication, sizeof(communication)) ||
		    memcmp(&provider.sealed_service, &communication, sizeof(communication)))
			result = CB_ERR;
		else {
			expected = SERVICE_INSTALLING;
			if (!__atomic_compare_exchange_n(&provider.service_phase, &expected,
				SERVICE_INSTALLED, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
				result = CB_ERR;
		}
	}
#endif

cleanup:
	if (owns_attempt && result != CB_SUCCESS) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI)
		payload_mm_authvar_mor_private_smi_channel_abort();
#endif
		scrub(&provider.policy, sizeof(provider.policy));
		scrub(&provider.sealed, sizeof(provider.sealed));
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
		if (service) {
			scrub(&provider.service, sizeof(provider.service));
			scrub(&provider.sealed_service, sizeof(provider.sealed_service));
			scrub(provider.service_request, sizeof(provider.service_request));
			scrub(provider.service_response, sizeof(provider.service_response));
			__atomic_store_n(&provider.service_phase, SERVICE_FAILED, __ATOMIC_RELEASE);
		}
#endif
	}
	scrub(&receipt, sizeof(receipt));
	scrub(&facts, sizeof(facts));
	scrub(&media, sizeof(media));
	scrub(&input, sizeof(input));
	scrub(&frozen, sizeof(frozen));
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	scrub(&communication, sizeof(communication));
#endif
#if ENV_TEST
	payload_mm_authvar_smm_bootstrap_scrub_observe(&receipt, sizeof(receipt));
	payload_mm_authvar_smm_bootstrap_scrub_observe(&facts, sizeof(facts));
	payload_mm_authvar_smm_bootstrap_scrub_observe(&media, sizeof(media));
	payload_mm_authvar_smm_bootstrap_scrub_observe(&input, sizeof(input));
	payload_mm_authvar_smm_bootstrap_scrub_observe(&frozen, sizeof(frozen));
#endif
	return result;
}

enum cb_err payload_mm_authvar_smm_bootstrap_install(
	const struct payload_mm_authvar_smm_bootstrap *bootstrap)
{
	return bootstrap_install(bootstrap, false);
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
enum cb_err payload_mm_authvar_smm_service_bootstrap_install(
	const struct payload_mm_authvar_smm_bootstrap *bootstrap)
{
	return bootstrap_install(bootstrap, true);
}

__weak bool platform_payload_mm_authvar_service_finalize_admitted(void)
{
	return false;
}

__weak bool platform_payload_mm_authvar_service_runtime_admitted(void)
{
	return false;
}

enum cb_err payload_mm_authvar_service_finalize(void)
{
	struct lb_authvar_service_endpoint descriptor;
	uint32_t expected = ENDPOINT_EMPTY;
	uint32_t data_offset;

	if (__atomic_load_n(&provider.service_phase, __ATOMIC_ACQUIRE) != SERVICE_INSTALLED ||
	    !protected_storage(&provider, sizeof(provider)) || !spi_writes_restricted(NULL) ||
	    memcmp(&provider.service, &provider.sealed_service, sizeof(provider.service)) ||
	    !protected_storage((const void *)platform_payload_mm_authvar_service_finalize_admitted,
		1) || !platform_payload_mm_authvar_service_finalize_admitted() ||
	    !__atomic_compare_exchange_n(&provider.endpoint_phase, &expected,
		ENDPOINT_FINALIZING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	data_offset = ALIGN_UP(PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE +
		provider.sealed.limits.maximum_name_size, 8U);
	if (data_offset >= sizeof(provider.service_request))
		goto failed;
	descriptor = (struct lb_authvar_service_endpoint) {
		.tag = LB_TAG_AUTHVAR_SERVICE_ENDPOINT,
		.size = sizeof(descriptor),
		.revision = LB_AUTHVAR_SERVICE_ENDPOINT_REVISION,
		.header_size = sizeof(descriptor),
		/* Established by the sole install and genuine current hardware/wave proof. */
		.flags = LB_AUTHVAR_ENDPOINT_REQUIRED_FLAGS,
		.generation = provider.sealed_service.generation,
		.communication_base = provider.sealed_service.range.base,
		.communication_size = sizeof(provider.service_request),
		.message_size = sizeof(provider.service_request),
		.transport = LB_AUTHVAR_ENDPOINT_TRANSPORT_APM_IO8,
		.trigger_width = 1,
		.trigger_address = APM_CNT,
		.trigger_value = SMM_APMC_AUTHVAR_SERVICE,
		.maximum_name_size = provider.sealed.limits.maximum_name_size,
		.maximum_data_size = (uint32_t)sizeof(provider.service_request) - data_offset,
	};
	provider.endpoint = descriptor;
	provider.sealed_endpoint = descriptor;
	if (payload_mm_authvar_service_endpoint_validate(&provider.endpoint) != CB_SUCCESS ||
	    payload_mm_authvar_executor_service_admit(&provider.endpoint,
		provider.service_request, provider.service_response,
		sizeof(provider.service_request)) != CB_SUCCESS ||
	    !spi_writes_restricted(NULL) || !platform_payload_mm_authvar_service_finalize_admitted() ||
	    memcmp(&provider.endpoint, &descriptor, sizeof(descriptor)) ||
	    memcmp(&provider.sealed_endpoint, &descriptor, sizeof(descriptor)))
		goto failed;
	__atomic_store_n(&provider.endpoint_phase, ENDPOINT_READY, __ATOMIC_RELEASE);
	return CB_SUCCESS;

failed:
	scrub(&provider.endpoint, sizeof(provider.endpoint));
	scrub(&provider.sealed_endpoint, sizeof(provider.sealed_endpoint));
	__atomic_store_n(&provider.endpoint_phase, ENDPOINT_FAILED, __ATOMIC_RELEASE);
	return CB_ERR;
}

enum cb_err payload_mm_authvar_service_descriptor_copy(
	struct lb_authvar_service_endpoint *endpoint)
{
	if (!endpoint || (uintptr_t)endpoint % _Alignof(struct lb_authvar_service_endpoint) ||
	    __atomic_load_n(&provider.endpoint_phase, __ATOMIC_ACQUIRE) != ENDPOINT_READY ||
	    !protected_storage(endpoint, sizeof(*endpoint)) ||
	    spans_overlap((uintptr_t)endpoint, sizeof(*endpoint), (uintptr_t)&provider,
		sizeof(provider)) ||
	    spans_overlap((uintptr_t)endpoint, sizeof(*endpoint), provider.sealed.arena.base,
		provider.sealed.arena.size) ||
	    memcmp(&provider.endpoint, &provider.sealed_endpoint, sizeof(provider.endpoint)))
		return CB_ERR;
	*endpoint = provider.sealed_endpoint;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_service_execute(void)
{
	struct payload_mm_authvar_service_frame *response = (void *)provider.service_response;
	struct payload_mm_authvar_service_frame *mailbox;
	uint32_t expected = ENDPOINT_READY;
	enum cb_err result = CB_ERR;

	if (!protected_storage(&provider, sizeof(provider)) || !spi_writes_restricted(NULL) ||
	    __atomic_load_n(&provider.service_phase, __ATOMIC_ACQUIRE) != SERVICE_INSTALLED ||
	    memcmp(&provider.service, &provider.sealed_service, sizeof(provider.service)) ||
	    !protected_storage((const void *)platform_payload_mm_authvar_service_runtime_admitted,
		1) || !platform_payload_mm_authvar_service_runtime_admitted() ||
	    memcmp(&provider.endpoint, &provider.sealed_endpoint, sizeof(provider.endpoint)) ||
	    !__atomic_compare_exchange_n(&provider.endpoint_phase, &expected,
		ENDPOINT_EXECUTING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	mailbox = (void *)(uintptr_t)provider.sealed_service.range.base;
	/* The shared mailbox is untrusted. Never compare it again after this snapshot. */
	memcpy(provider.service_request, mailbox, sizeof(provider.service_request));
	if (payload_mm_authvar_service_request_validate(&provider.sealed_endpoint,
		provider.service_request, sizeof(provider.service_request)) != CB_SUCCESS)
		goto out;
	if (!platform_payload_mm_authvar_service_runtime_admitted() || !spi_writes_restricted(NULL) ||
	    payload_mm_authvar_service_transaction(&provider.endpoint, provider.service_request,
		provider.service_response, sizeof(provider.service_request)) != CB_SUCCESS ||
	    memcmp(&provider.endpoint, &provider.sealed_endpoint, sizeof(provider.endpoint)) ||
	    memcmp(&provider.service, &provider.sealed_service, sizeof(provider.service)) ||
	    payload_mm_authvar_service_response_validate(&provider.sealed_endpoint,
		provider.service_request, provider.service_response,
		sizeof(provider.service_response)) != CB_SUCCESS ||
	    !platform_payload_mm_authvar_service_runtime_admitted() || !spi_writes_restricted(NULL))
		smm_invocation_platform_fail_stop();
	/* Publish the validated body first. The sole completion store is last. */
	response->completion = PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
	memcpy(mailbox, provider.service_response, sizeof(provider.service_response));
	__atomic_store_n(&mailbox->completion, PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE,
		__ATOMIC_RELEASE);
	result = CB_SUCCESS;
out:
	scrub(provider.service_request, sizeof(provider.service_request));
	scrub(provider.service_response, sizeof(provider.service_response));
	__atomic_store_n(&provider.endpoint_phase, ENDPOINT_READY, __ATOMIC_RELEASE);
	return result;
}
#endif
