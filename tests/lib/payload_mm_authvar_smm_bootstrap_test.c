/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar.h>
#include <boot/payload_mm_authvar_executor.h>
#include <boot/payload_mm_authvar_mor_seal.h>
#include <boot/payload_mm_authvar_smm_bootstrap.h>
#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <spi_flash.h>
#include <smmstore.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#undef assert
#define assert(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static struct payload_mm_authvar_smm_bootstrap seed;
static struct payload_mm_authvar_smm_arena_slot receipt_slot;
#define receipt (receipt_slot.receipt)
static struct payload_mm_authvar_contract installed_contract;
static struct spi_flash flash;
static struct region_device store;
static struct region_device qemu_root;
static uint8_t arena[4U * 1024U * 1024U] __aligned(8);
static uint8_t protected_object[64];
static uint8_t *transport;
static unsigned int step;
static unsigned int mutate_at;
static bool restricted = true;
static bool flash_present = true;
static bool store_failure;
static size_t runtime_smram_size = 16U * 1024U * 1024U;
static bool callback_retake;
static bool mutate_consumed_state;
static unsigned int scrub_calls;
static unsigned int store_lookups;
static unsigned int root_lookups;
static bool store_drift;
static bool root_drift;
static void *media_context;
static size_t media_context_size;
static bool media_facts_mutate_seed;
static bool media_facts_reserved;
static bool media_install_failure;
static bool media_ops_reserved;
static bool media_ops_missing_install;

enum cb_err payload_mm_authvar_smmstore_install(void);

#if !defined(TEST_EXTERNAL_MEDIA_PROVIDER)
static enum cb_err test_media_facts(void *unused,
	struct payload_mm_authvar_smm_media_facts *facts)
{
	assert(unused == media_context);
	if (media_facts_mutate_seed)
		seed.cold_boot_generation++;
	if (!facts || !flash_present || !flash.size || !flash.sector_size ||
	    store_failure)
		return CB_ERR;
	*facts = (struct payload_mm_authvar_smm_media_facts) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_MEDIA_REVISION,
		.size = sizeof(*facts),
		.boot_media_size = flash.size,
		.store_offset = store.region.offset,
		.store_size = store.region.size,
		.block_size = SMM_BLOCK_SIZE,
		.erase_size = flash.sector_size,
	};
	if (media_facts_reserved)
		facts->reserved[0] = 1;
	return CB_SUCCESS;
}

static enum cb_err test_media_install(void *unused)
{
	(void)unused;
	if (payload_mm_authvar_smmstore_install() != CB_SUCCESS ||
	    media_install_failure)
		return CB_ERR;
	return CB_SUCCESS;
}

bool platform_payload_mm_authvar_smm_media_ops(
	struct payload_mm_authvar_smm_media_ops *ops)
{
	if (!ops)
		return false;
	*ops = (struct payload_mm_authvar_smm_media_ops) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_MEDIA_REVISION,
		.size = sizeof(*ops),
		.facts = test_media_facts,
		.install = test_media_install,
		.context = media_context,
		.context_size = media_context_size,
	};
	if (media_ops_reserved)
		ops->reserved[0] = 1;
	if (media_ops_missing_install)
		ops->install = NULL;
	return true;
}
#endif

static void *install_thread(void *argument)
{
	enum cb_err *result = argument;

	*result = payload_mm_authvar_smm_bootstrap_install(&seed);
	return NULL;
}

static void *take_thread(void *argument)
{
	bool *result = argument;
	struct payload_mm_authvar_smm_arena_receipt taken;

	*result = smm_take_payload_mm_authvar_arena_receipt(&taken);
	return NULL;
}

void mock_assert(const int result, const char *expression, const char *file,
	const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

void payload_mm_authvar_smm_bootstrap_scrub_observe(const void *buffer,
	size_t size)
{
	const volatile uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	assert(!combined);
	__atomic_fetch_add(&scrub_calls, 1U, __ATOMIC_RELAXED);
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = 0x400000U;
	*size = runtime_smram_size;
}

const struct spi_flash *boot_device_spi_flash(void)
{
	return flash_present ? &flash : NULL;
}

const struct region_device *boot_device_rw(void)
{
	root_lookups++;
	if (root_drift && root_lookups > 1U)
		qemu_root.region.size--;
	return flash_present ? &qemu_root : NULL;
}

bool smm_take_payload_mm_authvar_arena_receipt(
	struct payload_mm_authvar_smm_arena_receipt *output)
{
	return payload_mm_authvar_smm_arena_slot_take(&receipt_slot, output);
}

bool smm_payload_mm_authvar_arena_receipt_consumed(void)
{
	return payload_mm_authvar_smm_arena_slot_consumed(&receipt_slot);
}

int smmstore_lookup_read_region(struct region_device *output)
{
	*output = store;
	store_lookups++;
	if (store_drift && store_lookups > 1U)
		output->region.offset++;
	return store_failure ? -1 : 0;
}

static void maybe_mutate(void)
{
	step++;
	if (step == mutate_at)
		seed.cold_boot_generation++;
}

static bool spi_restricted(void *context)
{
	struct payload_mm_authvar_smm_arena_receipt replay;

	assert(context == protected_object);
	if (callback_retake)
		assert(!smm_take_payload_mm_authvar_arena_receipt(&replay));
	if (mutate_consumed_state)
		__atomic_store_n(&receipt_slot.state,
			PAYLOAD_MM_AUTHVAR_SMM_ARENA_READY, __ATOMIC_RELEASE);
	maybe_mutate();
	return restricted;
}

enum cb_err payload_mm_authvar_authority_install(
	const struct payload_mm_authvar_contract *contract,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	assert(step == 1);
	step++;
	assert(storage_is_protected(context, protected_object,
		sizeof(protected_object)));
	installed_contract = *contract;
	if (mutate_at == step)
		seed.spi_context_size++;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_smmstore_install(void)
{
	assert(step == 2);
	step++;
	if (mutate_at == step)
		seed.seal_channel.caller++;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_qemu_pflash_install(void)
{
	assert(step == 2);
	step++;
	if (mutate_at == step)
		seed.seal_channel.caller++;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_executor_install(void *installed_arena,
	size_t arena_size,
	const struct payload_mm_authvar_executor_limits *limits)
{
	assert(step == 3);
	step++;
	assert(installed_arena == arena &&
		arena_size == sizeof(arena));
	assert(limits->maximum_store_size == store.region.size);
	if (mutate_at == step)
		seed.spi_context = NULL;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_executor_required_size(
	const struct payload_mm_authvar_executor_limits *limits, size_t *size)
{
	assert(limits && size);
	assert(limits->maximum_records == limits->maximum_store_size / 64U);
	*size = sizeof(arena);
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_seal_channel_install(
	const struct payload_mm_authvar_mor_seal_channel *channel,
	payload_mm_authvar_mor_seal_range_check protected_storage,
	payload_mm_authvar_mor_seal_range_check fixed_transport)
{
	assert(step == 4);
	step++;
	assert(channel->transport_base == (uintptr_t)transport);
	assert(protected_storage(protected_object, sizeof(protected_object)));
	assert(fixed_transport(transport, channel->transport_size));
	if (mutate_at == step)
		seed.reserved[0] = 1;
	return CB_SUCCESS;
}

static void initialize(void)
{
	transport = mmap(NULL, sizeof(struct payload_mm_authvar_mor_seal_request),
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	assert(transport != MAP_FAILED);
	memset(transport, 0, sizeof(struct payload_mm_authvar_mor_seal_request));
	flash.size = 16U * 1024U * 1024U;
	flash.sector_size = 4096U;
	store.region.offset = 12U * 1024U * 1024U;
	store.region.size = 3U * 64U * 1024U;
	qemu_root.region.size = flash.size;
	seed = (struct payload_mm_authvar_smm_bootstrap) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION,
		.size = sizeof(seed),
		.cold_boot_generation = 9,
		.seal_channel = {
			.transport_base = (uintptr_t)transport,
			.transport_size =
				sizeof(struct payload_mm_authvar_mor_seal_request),
			.caller = 0x55,
			.caller_context = 0xaa,
			.capability = { 1 },
		},
		.spi_writes_restricted_to_smm = spi_restricted,
		.spi_context = protected_object,
		.spi_context_size = sizeof(protected_object),
	};
	receipt = (struct payload_mm_authvar_smm_arena_receipt) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION,
		.size = sizeof(receipt),
		.cold_boot_generation = seed.cold_boot_generation,
		.smram = { .base = 0x400000U, .size = runtime_smram_size },
		.arena = { .base = (uintptr_t)arena, .size = sizeof(arena) },
	};
	memcpy(receipt.owner, seed.seal_channel.capability,
		sizeof(receipt.owner));
	receipt_slot.state = PAYLOAD_MM_AUTHVAR_SMM_ARENA_READY;
}

int main(int argc, char **argv)
{
	size_t required;

	assert(argc == 2);
	initialize();
	assert(payload_mm_authvar_smm_bootstrap_arena_size(store.region.size,
		&required) == CB_SUCCESS && required == sizeof(arena));
	if (!strcmp(argv[1], "success")) {
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_SUCCESS);
		assert(step == 5 && installed_contract.generation == 9);
	} else if (!strcmp(argv[1], "repeat")) {
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_SUCCESS);
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
	} else if (!strcmp(argv[1], "restricted")) {
		restricted = false;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 1);
	} else if (!strcmp(argv[1], "short")) {
		receipt.arena.size--;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "alias")) {
		receipt.arena.base = (uintptr_t)&seed;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "unaligned")) {
		receipt.arena.base++;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "overflow")) {
		receipt.arena.base = UINTPTR_MAX - 7U;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "receipt-revision")) {
		receipt.revision++;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "receipt-size")) {
		receipt.size--;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "receipt-reserved")) {
		receipt.reserved[0] = 1;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "receipt-smram")) {
		receipt.smram.base--;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "arena-empty")) {
		receipt.arena.size = 0;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "arena-outside")) {
		receipt.arena.base = receipt.smram.base - sizeof(arena);
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "store-failure")) {
		store_failure = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "flash-absent")) {
		flash_present = false;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "flash-size")) {
		flash.size = 0;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "flash-sector")) {
		flash.sector_size = 0;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "media-store-drift")) {
		store_drift = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "media-size-drift")) {
		root_drift = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 1);
	} else if (!strcmp(argv[1], "media-context-alias")) {
		media_context = arena;
		media_context_size = sizeof(protected_object);
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "media-facts-mutation")) {
		media_facts_mutate_seed = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "media-facts-reserved")) {
		media_facts_reserved = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "media-ops-reserved")) {
		media_ops_reserved = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "media-ops-missing-install")) {
		media_ops_missing_install = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "media-install-failure")) {
		media_install_failure = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 3);
	} else if (!strcmp(argv[1], "small-smram")) {
		runtime_smram_size = (uintptr_t)arena + sizeof(arena) - 0x400000U - 1U;
		receipt.smram.size = runtime_smram_size;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "transport-alias")) {
		seed.seal_channel.transport_base = (uintptr_t)arena;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "context-overlap")) {
		seed.spi_context = arena;
		seed.spi_context_size = 64;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "pre-take")) {
		struct payload_mm_authvar_smm_arena_receipt taken;

		assert(smm_take_payload_mm_authvar_arena_receipt(&taken));
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "post-take")) {
		struct payload_mm_authvar_smm_arena_receipt replay;

		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_SUCCESS);
		assert(!smm_take_payload_mm_authvar_arena_receipt(&replay));
	} else if (!strcmp(argv[1], "callback-retake")) {
		callback_retake = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_SUCCESS);
	} else if (!strcmp(argv[1], "state-mutation")) {
		mutate_consumed_state = true;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 1);
	} else if (!strcmp(argv[1], "torn-publication")) {
		receipt.owner[0] = 0;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "receipt-owner")) {
		receipt.owner[0] ^= 1U;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "receipt-generation")) {
		receipt.cold_boot_generation++;
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == 0);
	} else if (!strcmp(argv[1], "concurrent")) {
		pthread_t first;
		pthread_t second;
		enum cb_err first_result = CB_ERR;
		enum cb_err second_result = CB_ERR;

		assert(!pthread_create(&first, NULL, install_thread, &first_result));
		assert(!pthread_create(&second, NULL, install_thread, &second_result));
		assert(!pthread_join(first, NULL));
		assert(!pthread_join(second, NULL));
		assert((first_result == CB_SUCCESS && second_result == CB_ERR) ||
			(first_result == CB_ERR && second_result == CB_SUCCESS));
		assert(step == 5);
	} else if (!strcmp(argv[1], "two-taker")) {
		pthread_t first;
		pthread_t second;
		bool first_result = false;
		bool second_result = false;

		assert(!pthread_create(&first, NULL, take_thread, &first_result));
		assert(!pthread_create(&second, NULL, take_thread, &second_result));
		assert(!pthread_join(first, NULL));
		assert(!pthread_join(second, NULL));
		assert(first_result != second_result);
		assert(smm_payload_mm_authvar_arena_receipt_consumed());
	} else if (!strncmp(argv[1], "mutate", 6)) {
		mutate_at = (unsigned int)(argv[1][6] - '0');
		assert(mutate_at >= 1 && mutate_at <= 5);
		assert(payload_mm_authvar_smm_bootstrap_install(&seed) == CB_ERR);
		assert(step == mutate_at);
	} else {
		return 2;
	}
	assert(!munmap(transport,
		sizeof(struct payload_mm_authvar_mor_seal_request)));
	if (!strcmp(argv[1], "two-taker"))
		assert(scrub_calls == 0);
	else if (!strcmp(argv[1], "concurrent") || !strcmp(argv[1], "repeat"))
		assert(scrub_calls == 10);
	else
		assert(scrub_calls == 5);
	return 0;
}
