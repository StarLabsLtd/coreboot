/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_producer.h>
#include <bootmem.h>
#include <random.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static uint8_t backing[PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT);
#define mailbox (*(struct payload_mm_authvar_presence_message *)backing)
static struct bootmem_aligned_reservation_request registered;
static struct payload_mm_authvar_presence_seed installed_seed;
static int register_fail;
static int query_fail;
static int query_bad;
static int random_fail_at;
static int random_zero_mode;
static int random_calls;
static int install_fail;
static int install_calls;
static int close_calls;
static int fail_proof;
static int fail_proof_call;
static int proof_calls[5];
static int mutate_context;
static int mutate_seed;
static int cold_ok;
static int cold_calls;
static int abort_from_install;
static int abort_from_proof;
static int abort_from_cold_boot;

enum {
	PROOF_DMA = 1,
	PROOF_CPU,
	PROOF_RESET,
	PROOF_LIFECYCLE,
	PROOF_PLATFORM,
};

int bootmem_aligned_reservation_register(
	const struct bootmem_aligned_reservation_request *request,
	struct bootmem_aligned_reservation_handle *handle)
{
	registered = *request;
	if (!request->bytes || request->bytes % 4096U ||
	    request->alignment < 4096U ||
	    request->alignment & (request->alignment - 1U))
		return -1;
	handle->opaque[0] = 0x1234;
	return register_fail ? -1 : 0;
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	assert(handle->opaque[0] == 0x1234);
	*reservation = (struct bootmem_aligned_reservation) {
		.base = (uintptr_t)backing,
		.size = sizeof(backing),
		.tag = BM_MEM_RESERVED,
	};
	if (query_bad == 1)
		reservation->size--;
	else if (query_bad == 2)
		reservation->tag = BM_MEM_RAM;
	else if (query_bad == 3)
		reservation->reserved = 1;
	else if (query_bad == 4)
		reservation->base++;
	else if (query_bad == 5)
		reservation->base = UINT64_MAX - 7;
	else if (query_bad == 6)
		reservation->base = (1ULL << 32) - 40;
	return query_fail ? -1 : 0;
}

enum cb_err get_random_number_64(uint64_t *value)
{
	random_calls++;
	if (random_calls == random_fail_at)
		return CB_ERR;
	*value = 0x100U + (uint64_t)random_calls;
	if (random_zero_mode == random_calls ||
	    (random_zero_mode == 7 && random_calls >= 3))
		*value = 0;
	return CB_SUCCESS;
}

static bool cold_boot(void *context)
{
	(void)context;
	cold_calls++;
	if (abort_from_cold_boot && cold_calls >= 2)
		payload_mm_authvar_presence_producer_abort();
	return cold_ok;
}

static enum cb_err install(void *context,
	const struct payload_mm_authvar_presence_seed *seed)
{
	(void)context;
	install_calls++;
	installed_seed = *seed;
	if (abort_from_install)
		payload_mm_authvar_presence_producer_abort();
	if (mutate_seed)
		((struct payload_mm_authvar_presence_seed *)seed)->capability[0] ^= 1;
	if (install_fail)
		memset(backing, 0xa5, sizeof(backing));
	return install_fail ? CB_ERR : CB_SUCCESS;
}

static void close_authority(void *context)
{
	(void)context;
	close_calls++;
}

static bool proof(int which, void *context)
{
	proof_calls[which - 1]++;
	if (abort_from_proof == which)
		payload_mm_authvar_presence_producer_abort();
	if (mutate_context == which)
		*(uint32_t *)context ^= 1;
	return fail_proof != which ||
		(fail_proof_call && proof_calls[which - 1] != fail_proof_call);
}

static bool dma(void *context, uint64_t base, uint64_t size)
{
	assert(base == (uintptr_t)backing && size == sizeof(backing));
	return proof(PROOF_DMA, context);
}

static bool cpu(void *context) { return proof(PROOF_CPU, context); }
static bool reset_ready(void *context) { return proof(PROOF_RESET, context); }
static bool lifecycle(void *context) { return proof(PROOF_LIFECYCLE, context); }
static bool platform(void *context) { return proof(PROOF_PLATFORM, context); }

static struct payload_mm_authvar_presence_composition policy(uint32_t *context)
{
	return (struct payload_mm_authvar_presence_composition) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_REVISION,
		.size = sizeof(struct payload_mm_authvar_presence_composition),
		.trigger_address = 0xb2,
		.trigger_value = 0x77,
		.cold_boot = cold_boot,
		.authority_install = install,
		.authority_close = close_authority,
		.dma_protected = dma,
		.cpu_rendezvous_ready = cpu,
		.cold_reset_ready = reset_ready,
		.lifecycle_sealed = lifecycle,
		.platform_ready = platform,
		.context = context,
		.context_size = sizeof(*context),
	};
}

static void reset_mocks(void)
{
	payload_mm_authvar_presence_producer_reset_test();
	memset(backing, 0x5a, sizeof(backing));
	memset(&registered, 0, sizeof(registered));
	memset(&installed_seed, 0, sizeof(installed_seed));
	register_fail = query_fail = query_bad = random_fail_at = random_calls = 0;
	random_zero_mode = 0;
	install_fail = install_calls = close_calls = fail_proof = 0;
	fail_proof_call = 0;
	memset(proof_calls, 0, sizeof(proof_calls));
	mutate_context = 0;
	mutate_seed = 0;
	cold_ok = 1;
	cold_calls = 0;
	abort_from_install = abort_from_proof = 0;
	abort_from_cold_boot = 0;
}

static void reserve(void)
{
	assert(payload_mm_authvar_presence_producer_reserve() == CB_SUCCESS);
	assert(registered.revision == BOOTMEM_ALIGNED_RESERVATION_REVISION);
	assert(registered.size == sizeof(registered));
	assert(registered.bytes == PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE);
	assert(registered.alignment == PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT);
	assert(registered.limit_exclusive == (1ULL << 32));
	assert(registered.tag == BM_MEM_RESERVED && registered.reserved == 0);
	assert(payload_mm_authvar_presence_producer_reserve() == CB_ERR);
}

static bool producer_storage_cleared(uint32_t expected_state);

static void success(void)
{
	uint32_t context = 0xfeedbeef;
	struct payload_mm_authvar_presence_composition composition = policy(&context);
	struct lb_authvar_presence_endpoint record;

	reset_mocks();
	reserve();
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_SUCCESS);
	assert(install_calls == 1 && close_calls == 0 && random_calls == 6);
	assert(installed_seed.revision == PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION);
	assert(installed_seed.size == sizeof(installed_seed));
	assert(installed_seed.endpoint.flags == LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS);
	assert(mailbox.revision == PAYLOAD_MM_AUTHVAR_PRESENCE_REVISION);
	assert(mailbox.generation == installed_seed.endpoint.generation);
	assert(!memcmp(mailbox.capability, installed_seed.capability,
		sizeof(mailbox.capability)));
	assert(mailbox.status == PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING);
	assert(mailbox.completion == PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING);
	memset(&record, 0xcc, sizeof(record));
	assert(payload_mm_authvar_presence_producer_publication_take(&record) ==
		CB_SUCCESS);
	assert(record.tag == LB_TAG_AUTHVAR_PRESENCE_ENDPOINT);
	assert(record.flags == LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS);
	assert(record.communication_base == (uintptr_t)backing);
	assert(record.communication_size == sizeof(mailbox));
	assert(producer_storage_cleared(4U));
	for (size_t i = sizeof(mailbox); i < sizeof(backing); i++)
		assert(backing[i] == 0);
	memset(&record, 0xcc, sizeof(record));
	assert(payload_mm_authvar_presence_producer_publication_take(&record) == CB_ERR);
	for (size_t i = 0; i < sizeof(record); i++)
		assert(((uint8_t *)&record)[i] == 0);

}

static bool producer_storage_cleared(uint32_t expected_state)
{
	size_t size;
	const uint8_t *state = payload_mm_authvar_presence_producer_test_state(&size);
	uint8_t combined = 0;

	assert(size > sizeof(uint32_t));
	assert(*(const uint32_t *)state == expected_state);
	for (size_t i = sizeof(uint32_t); i < size; i++)
		combined |= state[i];
	return combined == 0;
}

static void proof_failures(void)
{
	for (int which = PROOF_DMA; which <= PROOF_PLATFORM; which++) {
		uint32_t context = 0xfeedbeef;
		struct payload_mm_authvar_presence_composition composition = policy(&context);
		struct lb_authvar_presence_endpoint record;

		reset_mocks();
		reserve();
		fail_proof = which;
		assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
		assert(close_calls == 1);
		for (size_t i = 0; i < sizeof(backing); i++)
			assert(backing[i] == 0);
		memset(&record, 0xcc, sizeof(record));
		assert(payload_mm_authvar_presence_producer_publication_take(&record) == CB_ERR);
		for (size_t i = 0; i < sizeof(record); i++)
			assert(((uint8_t *)&record)[i] == 0);
	}
	for (int which = PROOF_DMA; which <= PROOF_PLATFORM; which++) {
		uint32_t context = 0xfeedbeef;
		struct payload_mm_authvar_presence_composition composition = policy(&context);

		reset_mocks();
		reserve();
		fail_proof = which;
		fail_proof_call = 2;
		assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
		assert(close_calls == 1 && proof_calls[which - 1] == 2);
		for (size_t i = 0; i < sizeof(backing); i++)
			assert(backing[i] == 0);
	}
}

static void terminal_failures(void)
{
	uint32_t context;
	struct payload_mm_authvar_presence_composition composition;

	reset_mocks();
	register_fail = 1;
	assert(payload_mm_authvar_presence_producer_reserve() == CB_ERR);
	assert(payload_mm_authvar_presence_producer_reserve() == CB_ERR);
	assert(producer_storage_cleared(5U));

	reset_mocks(); reserve();
	context = 0;
	composition = policy(&context);
	cold_ok = 0;
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
	assert(random_calls == 0 && install_calls == 0);
	for (size_t i = 0; i < sizeof(backing); i++)
		assert(backing[i] == 0x5a);
	assert(producer_storage_cleared(5U));

	reset_mocks(); reserve();
	context = 0xfeedbeef;
	composition = policy(&context);
	for (int which = 1; which <= 6; which++) {
		query_bad = which;
		assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
		assert(random_calls == 0 && install_calls == 0);
		for (size_t i = 0; i < sizeof(backing); i++)
			assert(backing[i] == 0x5a);
		assert(producer_storage_cleared(5U));
		if (which != 6) {
			reset_mocks(); reserve();
			context = 0xfeedbeef;
			composition = policy(&context);
		}
	}

	reset_mocks(); reserve();
	context = 0xfeedbeef;
	composition = policy(&context);
	random_fail_at = 4;
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
	assert(install_calls == 0);

	for (int mode = 1; mode <= 7; mode += mode == 2 ? 5 : 1) {
		reset_mocks(); reserve();
		context = 0xfeedbeef;
		composition = policy(&context);
		random_zero_mode = mode;
		assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
		assert(install_calls == 0);
	}

	reset_mocks(); reserve();
	context = 0xfeedbeef;
	composition = policy(&context);
	install_fail = 1;
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
	assert(close_calls == 1);
	assert(producer_storage_cleared(5U));
	for (size_t i = 0; i < sizeof(backing); i++)
		assert(backing[i] == 0);

	reset_mocks(); reserve();
	context = 0xfeedbeef;
	composition = policy(&context);
	mutate_context = PROOF_DMA;
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
	assert(close_calls == 1);
	assert(producer_storage_cleared(5U));

	reset_mocks(); reserve();
	context = 0xfeedbeef;
	composition = policy(&context);
	mutate_seed = 1;
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
	assert(close_calls == 1);
	assert(producer_storage_cleared(5U));
}

static void reentry_is_terminal(void)
{
	uint32_t context = 0xfeedbeef;
	struct payload_mm_authvar_presence_composition composition = policy(&context);
	struct lb_authvar_presence_endpoint record;

	reset_mocks(); reserve();
	abort_from_install = 1;
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
	assert(install_calls == 1 && close_calls == 1);
	assert(producer_storage_cleared(5U));
	for (size_t i = 0; i < sizeof(backing); i++)
		assert(backing[i] == 0);

	reset_mocks(); reserve();
	abort_from_proof = PROOF_CPU;
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
	assert(close_calls == 1 && producer_storage_cleared(5U));
	memset(&record, 0xcc, sizeof(record));
	assert(payload_mm_authvar_presence_producer_publication_take(&record) == CB_ERR);
	for (size_t i = 0; i < sizeof(record); i++)
		assert(((uint8_t *)&record)[i] == 0);

	reset_mocks(); reserve();
	abort_from_cold_boot = 1;
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_ERR);
	assert(cold_calls == 2 && proof_calls[PROOF_DMA - 1] == 0 &&
		close_calls == 1 && producer_storage_cleared(5U));
	for (size_t i = 0; i < sizeof(backing); i++)
		assert(backing[i] == 0);
}

static void handoff_revalidates(void)
{
	uint32_t context = 0xfeedbeef;
	struct payload_mm_authvar_presence_composition composition = policy(&context);
	struct lb_authvar_presence_endpoint record;

	reset_mocks(); reserve();
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_SUCCESS);
	fail_proof = PROOF_LIFECYCLE;
	memset(&record, 0xcc, sizeof(record));
	assert(payload_mm_authvar_presence_producer_publication_take(&record) == CB_ERR);
	assert(close_calls == 1);
	for (size_t i = 0; i < sizeof(record); i++)
		assert(((uint8_t *)&record)[i] == 0);
	for (size_t i = 0; i < sizeof(backing); i++)
		assert(backing[i] == 0);
}

static void null_handoff_is_terminal(void)
{
	uint32_t context = 0xfeedbeef;
	struct payload_mm_authvar_presence_composition composition = policy(&context);
	struct lb_authvar_presence_endpoint record;

	reset_mocks(); reserve();
	assert(payload_mm_authvar_presence_producer_compose(&composition) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_producer_publication_take(NULL) == CB_ERR);
	assert(close_calls == 1);
	assert(payload_mm_authvar_presence_producer_publication_take(&record) == CB_ERR);
	for (size_t i = 0; i < sizeof(record); i++)
		assert(((uint8_t *)&record)[i] == 0);

	reset_mocks();
	payload_mm_authvar_presence_producer_abort();
	assert(payload_mm_authvar_presence_producer_reserve() == CB_ERR);
}

int main(void)
{
	success();
	proof_failures();
	terminal_failures();
	handoff_revalidates();
	null_handoff_is_terminal();
	reentry_is_terminal();
	return 0;
}
