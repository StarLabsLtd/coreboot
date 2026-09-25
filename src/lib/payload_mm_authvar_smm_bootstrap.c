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

static struct {
	struct bootstrap_policy policy;
	struct bootstrap_policy sealed;
	uint32_t attempted;
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
	return !memcmp(&provider.policy, &provider.sealed,
		sizeof(provider.policy));
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
		(uintptr_t)base == provider.sealed.communication.base &&
		size == provider.sealed.communication.size &&
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
		fixed_transport((const void *)(uintptr_t)base, (size_t)size);
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

enum cb_err payload_mm_authvar_smm_bootstrap_install(
	const struct payload_mm_authvar_smm_bootstrap *bootstrap)
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

	if (!__atomic_compare_exchange_n(&provider.attempted, &expected, 1, false,
		__ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
		goto cleanup;
	owns_attempt = true;
	if (!bootstrap)
		goto cleanup;
	smm_region(&smram_base, &smram_size);
	if (!smram_size || !span_within((uintptr_t)bootstrap, sizeof(*bootstrap),
		smram_base, smram_size))
		goto cleanup;
	memcpy(&input, bootstrap, sizeof(input));
	if (input.revision != PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION ||
	    input.size != sizeof(input) || !input.cold_boot_generation ||
	    input.reserved[0] || input.reserved[1] ||
	    !input.spi_writes_restricted_to_smm ||
	    (!!input.spi_context != !!input.spi_context_size) ||
	    input.seal_channel.transport_size !=
		sizeof(struct payload_mm_authvar_mor_seal_request) ||
	    input.seal_channel.transport_base %
		_Alignof(struct payload_mm_authvar_mor_seal_request) ||
	    !input.seal_channel.caller || !input.seal_channel.caller_context ||
	    !bytes_nonzero(input.seal_channel.capability,
		sizeof(input.seal_channel.capability)) ||
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
	    memcmp(receipt.owner, input.seal_channel.capability,
		sizeof(receipt.owner)) ||
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
		.receipt = receipt,
		.smram = { .base = smram_base, .size = smram_size },
		.communication = {
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
	    !fixed_transport((const void *)(uintptr_t)
		input.seal_channel.transport_base,
		(size_t)input.seal_channel.transport_size))
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
	    payload_mm_authvar_mor_private_smi_channel_install(
		smm_get_payload_mm_authvar_mor_private_smi_slot(),
		&provider.sealed.channel, protected_storage) != CB_SUCCESS ||
	    !policy_matches(&frozen) ||
	    memcmp(&input, bootstrap, sizeof(input)) ||
#endif
	    payload_mm_authvar_mor_seal_channel_install(&provider.sealed.channel,
		protected_storage, fixed_transport) != CB_SUCCESS ||
	    !policy_matches(&frozen) ||
	    !smm_payload_mm_authvar_arena_receipt_consumed() ||
	    memcmp(&input, bootstrap, sizeof(input)))
		goto cleanup;
	result = CB_SUCCESS;

cleanup:
	if (owns_attempt && result != CB_SUCCESS) {
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI)
		payload_mm_authvar_mor_private_smi_channel_abort();
#endif
		scrub(&provider.policy, sizeof(provider.policy));
		scrub(&provider.sealed, sizeof(provider.sealed));
	}
	scrub(&receipt, sizeof(receipt));
	scrub(&facts, sizeof(facts));
	scrub(&media, sizeof(media));
	scrub(&input, sizeof(input));
	scrub(&frozen, sizeof(frozen));
#if ENV_TEST
	payload_mm_authvar_smm_bootstrap_scrub_observe(&receipt, sizeof(receipt));
	payload_mm_authvar_smm_bootstrap_scrub_observe(&facts, sizeof(facts));
	payload_mm_authvar_smm_bootstrap_scrub_observe(&media, sizeof(media));
	payload_mm_authvar_smm_bootstrap_scrub_observe(&input, sizeof(input));
	payload_mm_authvar_smm_bootstrap_scrub_observe(&frozen, sizeof(frozen));
#endif
	return result;
}
