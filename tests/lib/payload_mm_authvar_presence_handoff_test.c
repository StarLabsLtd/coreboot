/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_handoff.h>
#include "../../src/lib/bootmem_reservation_receipt_internal.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static uint8_t mailbox[PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT);
static uint8_t transfer[PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_ALIGNMENT);
static uint8_t arbitrary[PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_ALIGNMENT);
static struct payload_mm_authvar_presence_handoff_slot slot;
static struct bootmem_aligned_reservation_request registered;
static const struct bootmem_aligned_reservation_handle mailbox_handle = {
	.opaque = { 0x11111111U, 0x22222222U },
};
static const struct bootmem_aligned_reservation_handle transfer_handle = {
	.opaque = { 0x33333333U, 0x44444444U },
};
static const void *allowed_descriptor;
static const void *allowed_result;
static const void *allowed_result_secondary;
static bool cold_ok;
static bool query_fail;
static bool stale_mailbox;
static bool stale_transfer;
static bool alias_transfer;
static bool firmware_retag;
static bool os_retag;
static bool wrong_mailbox_tag;
static bool wrong_transfer_tag;
static bool install_fail;
static bool mutate_seed;
static bool abort_during_trigger;
static bool abort_after_verifier_publish;
static bool block_before_delivery;
static bool delivery_entered;
static bool delivery_release;
static pthread_mutex_t delivery_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t delivery_cond = PTHREAD_COND_INITIALIZER;
static enum cb_err install_thread_result;
static unsigned int receive_cpu;
static unsigned int install_calls;
static unsigned int self_smi_calls;
static bool receiver_scrubbed_full;
static bool mutate_during_verify;
static bool poison_transfer_tail;
static bool swap_receipts;
static bool swap_receipt_handles;
static bool mutate_source_mailbox_query;
static bool mutate_source_transfer_query;
static bool mutate_source_mailbox_emit;
static bool mutate_source_transfer_emit;
static struct payload_mm_authvar_presence_seed *hostile_seed_source;
static bool block_after_receiver_claim;
static bool receiver_claim_entered;
static bool receiver_claim_release;
static bool block_during_cleanup;
static bool cleanup_entered;
static bool cleanup_release;
static pthread_mutex_t receiver_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t receiver_cond = PTHREAD_COND_INITIALIZER;
static struct payload_mm_authvar_presence_seed installed;

struct receive_thread_arguments {
	const struct payload_mm_authvar_presence_handoff_descriptor *descriptor;
	uint64_t *result;
	enum cb_err status;
};

static void mutate_source(void)
{
	if (hostile_seed_source)
		hostile_seed_source->capability[0]++;
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	while (size--)
		combined |= *bytes++;
	return !combined;
}

static bool slot_scrubbed(void)
{
	return bytes_zero(slot.mailbox_verifier.secret,
			sizeof(slot.mailbox_verifier.secret)) &&
		!slot.mailbox_verifier.generation &&
		bytes_zero(&slot.mailbox_verifier.handle,
			sizeof(slot.mailbox_verifier.handle)) &&
		!slot.mailbox_verifier.sequence && !slot.mailbox_verifier.boot_kind &&
		bytes_zero(slot.transfer_verifier.secret,
			sizeof(slot.transfer_verifier.secret)) &&
		!slot.transfer_verifier.generation &&
		bytes_zero(&slot.transfer_verifier.handle,
			sizeof(slot.transfer_verifier.handle)) &&
		!slot.transfer_verifier.sequence && !slot.transfer_verifier.boot_kind &&
		!slot.channel_generation && !slot.channel_identity &&
		!slot.owner_cpu && !slot.maximum_cpus &&
		bytes_zero(slot.channel_capability,
			sizeof(slot.channel_capability)) &&
		bytes_zero(slot.reserved, sizeof(slot.reserved));
}

int bootmem_aligned_reservation_register(
	const struct bootmem_aligned_reservation_request *request,
	struct bootmem_aligned_reservation_handle *handle)
{
	registered = *request;
	*handle = transfer_handle;
	return 0;
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	if (query_fail)
		return -1;
	if (!memcmp(handle, &mailbox_handle, sizeof(*handle))) {
		if (stale_mailbox)
			return -1;
		*reservation = (struct bootmem_aligned_reservation) {
			.base = (uintptr_t)mailbox,
			.size = sizeof(mailbox),
			.tag = BM_MEM_RESERVED,
		};
		if (firmware_retag)
			reservation->tag = BM_MEM_TABLE;
		if (mutate_source_mailbox_query)
			mutate_source();
		return 0;
	}
	if (!memcmp(handle, &transfer_handle, sizeof(*handle))) {
		if (stale_transfer)
			return -1;
		*reservation = (struct bootmem_aligned_reservation) {
			.base = alias_transfer ? (uintptr_t)mailbox : (uintptr_t)transfer,
			.size = sizeof(transfer),
			.tag = BM_MEM_RESERVED,
		};
		if (os_retag)
			reservation->tag = BM_MEM_TABLE;
		if (mutate_source_transfer_query)
			mutate_source();
		return 0;
	}
	return -1;
}

static enum cb_err emit_receipt(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt, enum bootmem_type expected_tag)
{
	struct bootmem_reservation_receipt_authority claimed = { 0 };
	struct bootmem_aligned_reservation reservation;

	memset(receipt, 0, sizeof(*receipt));
	if (expected_tag != BM_MEM_RESERVED ||
	    bootmem_aligned_reservation_query(handle, &reservation) ||
	    reservation.tag != expected_tag ||
	    !bootmem_reservation_receipt_authority_claim(signer, &claimed))
		return CB_ERR;
	*receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(*receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = claimed.generation,
		.sequence = claimed.sequence,
		.handle = *handle,
		.base = reservation.base,
		.bytes = reservation.size,
		.tag = (!memcmp(handle, &mailbox_handle, sizeof(*handle)) ?
			wrong_mailbox_tag : wrong_transfer_tag) ?
			BM_MEM_TABLE : BM_MEM_RESERVED,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(claimed.secret, receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt->mac) ==
		CB_SUCCESS);
	bootmem_reservation_receipt_close(&claimed);
	if (!memcmp(handle, &mailbox_handle, sizeof(*handle)) &&
	    mutate_source_mailbox_emit)
		mutate_source();
	if (!memcmp(handle, &transfer_handle, sizeof(*handle)) &&
	    mutate_source_transfer_emit)
		mutate_source();
	return CB_SUCCESS;
}

enum cb_err bootmem_aligned_reservation_receipt_emit_exact_tag(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt, enum bootmem_type expected_tag)
{
	return emit_receipt(handle, signer, receipt, expected_tag);
}

enum cb_err bootmem_aligned_reservation_receipt_emit(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt)
{
	return emit_receipt(handle, signer, receipt, BM_MEM_TABLE);
}

bool platform_payload_mm_authvar_presence_handoff_cold_boot(void)
{
	return cold_ok;
}

struct payload_mm_authvar_presence_handoff_slot *
smm_get_payload_mm_authvar_presence_handoff_slot(void)
{
	return &slot;
}

bool platform_payload_mm_authvar_presence_handoff_range_valid(
	uint64_t base, uint64_t size)
{
	return (base == (uintptr_t)transfer && size == sizeof(transfer)) ||
		(base == (uintptr_t)allowed_descriptor &&
		 size == sizeof(struct payload_mm_authvar_presence_handoff_descriptor)) ||
		(base == (uintptr_t)allowed_result && size == sizeof(uint64_t)) ||
		(base == (uintptr_t)allowed_result_secondary &&
		 size == sizeof(uint64_t));
}

enum cb_err platform_payload_mm_authvar_presence_handoff_receiver_install(
	const struct payload_mm_authvar_presence_seed *seed)
{
	install_calls++;
	installed = *seed;
	if (mutate_seed)
		((struct payload_mm_authvar_presence_seed *)(uintptr_t)seed)->size++;
	return install_fail ? CB_ERR : CB_SUCCESS;
}

enum cb_err platform_payload_mm_authvar_presence_handoff_self_smi(
	const struct payload_mm_authvar_presence_handoff_descriptor *descriptor,
	uint64_t *status)
{
	self_smi_calls++;
	allowed_descriptor = descriptor;
	allowed_result = status;
	if (poison_transfer_tail)
		transfer[sizeof(transfer) - 1U] = 0xa5U;
	if (swap_receipts) {
		struct payload_mm_authvar_presence_handoff_request *request =
			(void *)transfer;
		struct bootmem_reservation_receipt receipt = request->mailbox_receipt;

		request->mailbox_receipt = request->transfer_receipt;
		request->transfer_receipt = receipt;
	}
	if (swap_receipt_handles) {
		struct payload_mm_authvar_presence_handoff_request *request =
			(void *)transfer;
		struct bootmem_aligned_reservation_handle handle =
			request->mailbox_receipt.handle;

		request->mailbox_receipt.handle = request->transfer_receipt.handle;
		request->transfer_receipt.handle = handle;
	}
	{
		const enum cb_err result =
			payload_mm_authvar_presence_handoff_receive(&slot, descriptor,
				receive_cpu, status);

		receiver_scrubbed_full = bytes_zero(transfer, sizeof(transfer));
		return result;
	}
}

void payload_mm_authvar_presence_handoff_test_after_receiver_claim(void)
{
	if (!block_after_receiver_claim)
		return;
	assert(!pthread_mutex_lock(&receiver_mutex));
	receiver_claim_entered = true;
	assert(!pthread_cond_broadcast(&receiver_cond));
	while (!receiver_claim_release)
		assert(!pthread_cond_wait(&receiver_cond, &receiver_mutex));
	assert(!pthread_mutex_unlock(&receiver_mutex));
}

void payload_mm_authvar_presence_handoff_test_during_receiver_cleanup(
	const struct payload_mm_authvar_presence_handoff_slot *protected_slot)
{
	assert(protected_slot == &slot);
	if (!block_during_cleanup)
		return;
	assert(!pthread_mutex_lock(&receiver_mutex));
	cleanup_entered = true;
	assert(!pthread_cond_broadcast(&receiver_cond));
	while (!cleanup_release)
		assert(!pthread_cond_wait(&receiver_cond, &receiver_mutex));
	assert(!pthread_mutex_unlock(&receiver_mutex));
}

void payload_mm_authvar_presence_handoff_test_before_delivery(void)
{
	if (abort_during_trigger)
		payload_mm_authvar_presence_handoff_abort();
	if (!block_before_delivery)
		return;
	assert(!pthread_mutex_lock(&delivery_mutex));
	delivery_entered = true;
	assert(!pthread_cond_broadcast(&delivery_cond));
	while (!delivery_release)
		assert(!pthread_cond_wait(&delivery_cond, &delivery_mutex));
	assert(!pthread_mutex_unlock(&delivery_mutex));
}

void bootmem_receipt_test_after_verifier_publish(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier)
{
	(void)signer;
	(void)verifier;
	if (abort_after_verifier_publish)
		payload_mm_authvar_presence_handoff_abort();
}

void bootmem_receipt_test_after_claim_cas(
	struct bootmem_reservation_receipt_authority *authority)
{
	if (mutate_during_verify &&
	    (authority == &slot.transfer_verifier ||
	     authority == &slot.mailbox_verifier)) {
		struct payload_mm_authvar_presence_handoff_request *request =
			(void *)transfer;

		request->seed.endpoint.generation++;
		mutate_during_verify = false;
	}
}

static void *install_thread(void *argument)
{
	install_thread_result = payload_mm_authvar_presence_handoff_install(NULL,
		argument);
	return NULL;
}

static void *receive_thread(void *argument)
{
	struct receive_thread_arguments *arguments = argument;

	arguments->status = payload_mm_authvar_presence_handoff_receive(&slot,
		arguments->descriptor, 0, arguments->result);
	return NULL;
}

static struct payload_mm_authvar_presence_handoff_loader_seed loader_seed(void)
{
	struct payload_mm_authvar_presence_handoff_loader_seed seed = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_REVISION,
		.size = sizeof(seed),
		.channel_generation = 7U,
		.mailbox_handle = mailbox_handle,
		.owner_cpu = 0,
		.maximum_cpus = 4U,
	};

	for (size_t index = 0; index < sizeof(seed.channel_capability); index++)
		seed.channel_capability[index] = (uint8_t)(index + 1U);
	for (size_t index = 0; index < sizeof(seed.mailbox_receipt_secret); index++) {
		seed.mailbox_receipt_secret[index] = (uint8_t)(0x40U + index);
		seed.transfer_receipt_secret[index] = (uint8_t)(0x80U + index);
	}
	return seed;
}

static struct payload_mm_authvar_presence_seed authority_seed(void)
{
	struct payload_mm_authvar_presence_seed seed = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION,
		.size = sizeof(seed),
		.endpoint = {
			.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
			.size = sizeof(struct lb_authvar_presence_endpoint),
			.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
			.header_size = sizeof(struct lb_authvar_presence_endpoint),
			.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
			.generation = 7U,
			.communication_base = (uintptr_t)mailbox,
			.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
			.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
			.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
			.trigger_width = 1,
			.trigger_address = 0xb2,
			.trigger_value = 0xe8,
			.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
			.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
		},
	};

	for (size_t index = 0; index < sizeof(seed.capability); index++)
		seed.capability[index] = (uint8_t)(0xc0U + index);
	return seed;
}

static void reset_fixture(void)
{
	payload_mm_authvar_presence_handoff_sender_reset_test();
	payload_mm_authvar_presence_handoff_receiver_reset_test();
	memset(&slot, 0, sizeof(slot));
	memset(mailbox, 0x7b, sizeof(mailbox));
	memset(transfer, 0, sizeof(transfer));
	memset(arbitrary, 0x5a, sizeof(arbitrary));
	memset(&registered, 0, sizeof(registered));
	memset(&installed, 0, sizeof(installed));
	allowed_descriptor = NULL;
	allowed_result = NULL;
	allowed_result_secondary = NULL;
	cold_ok = true;
	query_fail = false;
	stale_mailbox = false;
	stale_transfer = false;
	alias_transfer = false;
	firmware_retag = false;
	os_retag = false;
	wrong_mailbox_tag = false;
	wrong_transfer_tag = false;
	install_fail = false;
	mutate_seed = false;
	abort_during_trigger = false;
	abort_after_verifier_publish = false;
	block_before_delivery = false;
	delivery_entered = false;
	delivery_release = false;
	install_thread_result = CB_SUCCESS;
	receiver_scrubbed_full = false;
	mutate_during_verify = false;
	poison_transfer_tail = false;
	swap_receipts = false;
	swap_receipt_handles = false;
	mutate_source_mailbox_query = false;
	mutate_source_transfer_query = false;
	mutate_source_mailbox_emit = false;
	mutate_source_transfer_emit = false;
	hostile_seed_source = NULL;
	block_after_receiver_claim = false;
	receiver_claim_entered = false;
	receiver_claim_release = false;
	block_during_cleanup = false;
	cleanup_entered = false;
	cleanup_release = false;
	receive_cpu = 0;
	install_calls = 0;
	self_smi_calls = 0;
}

static void prepare(void)
{
	struct payload_mm_authvar_presence_handoff_loader_seed seed = loader_seed();

	assert(payload_mm_authvar_presence_handoff_reserve() == CB_SUCCESS);
	assert(registered.bytes == sizeof(transfer) &&
		registered.alignment ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_ALIGNMENT &&
		registered.tag == BM_MEM_RESERVED);
	assert(payload_mm_authvar_presence_handoff_loader_provision(&slot, &seed) ==
		CB_SUCCESS);
	assert(bytes_zero(&seed, sizeof(seed)));
}

static void success(void)
{
	struct payload_mm_authvar_presence_seed seed = authority_seed();

	reset_fixture();
	prepare();
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) ==
		CB_SUCCESS);
	assert(install_calls == 1 && !memcmp(&installed, &seed, sizeof(seed)));
	assert(receiver_scrubbed_full);
	assert(bytes_zero(transfer, sizeof(transfer)));
	for (size_t index = 0; index < sizeof(mailbox); index++)
		assert(mailbox[index] == 0x7b);
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(slot_scrubbed());
	assert(payload_mm_authvar_presence_handoff_sender_scrubbed_test());
}

static void cold_and_resolution_failures(void)
{
	struct payload_mm_authvar_presence_handoff_loader_seed loader;
	struct payload_mm_authvar_presence_seed seed;

	reset_fixture();
	assert(payload_mm_authvar_presence_handoff_reserve() == CB_SUCCESS);
	loader = loader_seed();
	cold_ok = false;
	assert(payload_mm_authvar_presence_handoff_loader_provision(&slot,
		&loader) == CB_ERR);
	assert(bytes_zero(&loader, sizeof(loader)));

	reset_fixture();
	prepare();
	seed = authority_seed();
	stale_mailbox = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && bytes_zero(transfer, sizeof(transfer)));

	reset_fixture();
	prepare();
	seed = authority_seed();
	stale_transfer = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && bytes_zero(transfer, sizeof(transfer)));
}

static void tag_and_seed_failures(void)
{
	struct payload_mm_authvar_presence_seed seed;

	reset_fixture();
	prepare();
	seed = authority_seed();
	firmware_retag = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls);

	reset_fixture();
	prepare();
	seed = authority_seed();
	os_retag = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls);

	reset_fixture();
	prepare();
	seed = authority_seed();
	wrong_mailbox_tag = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && bytes_zero(transfer, sizeof(transfer)));

	reset_fixture();
	prepare();
	seed = authority_seed();
	wrong_transfer_tag = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && bytes_zero(transfer, sizeof(transfer)));

	reset_fixture();
	prepare();
	seed = authority_seed();
	seed.endpoint.generation++;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && !self_smi_calls);
}

static void receiver_failures(void)
{
	struct payload_mm_authvar_presence_handoff_loader_seed loader;
	struct payload_mm_authvar_presence_handoff_descriptor descriptor;
	struct payload_mm_authvar_presence_seed seed;
	uint64_t result = 0;

	reset_fixture();
	prepare();
	seed = authority_seed();
	receive_cpu = 1;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && bytes_zero(transfer, sizeof(transfer)));

	reset_fixture();
	prepare();
	seed = authority_seed();
	mutate_seed = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(install_calls == 1 && bytes_zero(transfer, sizeof(transfer)));
	assert(slot_scrubbed());

	reset_fixture();
	prepare();
	seed = authority_seed();
	install_fail = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(install_calls == 1 && slot_scrubbed() &&
		payload_mm_authvar_presence_handoff_sender_scrubbed_test());

	reset_fixture();
	assert(payload_mm_authvar_presence_handoff_reserve() == CB_SUCCESS);
	loader = loader_seed();
	assert(payload_mm_authvar_presence_handoff_loader_provision(&slot,
		&loader) == CB_SUCCESS);
	descriptor = (struct payload_mm_authvar_presence_handoff_descriptor) {
		.channel_identity = slot.channel_identity,
		.request_base = (uintptr_t)arbitrary,
		.descriptor_cookie = 1U,
	};
	allowed_descriptor = &descriptor;
	allowed_result = &result;
	assert(payload_mm_authvar_presence_handoff_receive(&slot, &descriptor, 0,
		&result) == CB_ERR);
	for (size_t index = 0; index < sizeof(arbitrary); index++)
		assert(arbitrary[index] == 0x5a);

	reset_fixture();
	assert(payload_mm_authvar_presence_handoff_reserve() == CB_SUCCESS);
	loader = loader_seed();
	assert(payload_mm_authvar_presence_handoff_loader_provision(&slot,
		&loader) == CB_SUCCESS);
	descriptor.channel_identity = slot.channel_identity;
	descriptor.request_base = 0x1000U;
	descriptor.descriptor_cookie = 1U;
	allowed_descriptor = &descriptor;
	allowed_result = &result;
	assert(payload_mm_authvar_presence_handoff_receive(&slot, &descriptor, 0,
		&result) == CB_ERR);

	reset_fixture();
	prepare();
	seed = authority_seed();
	mutate_during_verify = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && bytes_zero(transfer, sizeof(transfer)));

	reset_fixture();
	prepare();
	seed = authority_seed();
	poison_transfer_tail = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && receiver_scrubbed_full &&
		bytes_zero(transfer, sizeof(transfer)));

	reset_fixture();
	prepare();
	assert(payload_mm_authvar_presence_handoff_install(NULL,
		(const void *)transfer) == CB_ERR);

	reset_fixture();
	prepare();
	seed = authority_seed();
	swap_receipts = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && slot_scrubbed());

	reset_fixture();
	prepare();
	seed = authority_seed();
	swap_receipt_handles = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && slot_scrubbed());
}

static void source_mutation_failures(void)
{
	struct payload_mm_authvar_presence_seed seed;

	for (unsigned int stage = 0; stage < 4U; stage++) {
		reset_fixture();
		prepare();
		seed = authority_seed();
		hostile_seed_source = &seed;
		switch (stage) {
		case 0:
			mutate_source_mailbox_query = true;
			break;
		case 1:
			mutate_source_transfer_query = true;
			break;
		case 2:
			mutate_source_mailbox_emit = true;
			break;
		default:
			mutate_source_transfer_emit = true;
			break;
		}
		assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) ==
			CB_ERR);
		assert(!self_smi_calls && !install_calls &&
			payload_mm_authvar_presence_handoff_sender_scrubbed_test());
	}
}

static void abort_and_replay(void)
{
	struct payload_mm_authvar_presence_seed seed;
	struct payload_mm_authvar_presence_handoff_descriptor descriptor;
	uint64_t result;

	reset_fixture();
	prepare();
	seed = authority_seed();
	abort_during_trigger = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!install_calls && bytes_zero(transfer, sizeof(transfer)));

	reset_fixture();
	prepare();
	seed = authority_seed();
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) ==
		CB_SUCCESS);
	descriptor = (struct payload_mm_authvar_presence_handoff_descriptor) {
		.channel_identity = slot.channel_identity,
		.request_base = (uintptr_t)transfer,
		.descriptor_cookie = 1U,
	};
	allowed_descriptor = &descriptor;
	allowed_result = &result;
	assert(payload_mm_authvar_presence_handoff_receive(&slot, &descriptor, 0,
		&result) == CB_ERR);
}

static void alias_and_overflow_failures(void)
{
	struct payload_mm_authvar_presence_handoff_loader_seed loader;
	struct payload_mm_authvar_presence_handoff_descriptor descriptor = { 0 };
	struct payload_mm_authvar_presence_handoff_slot slot_snapshot;
	struct payload_mm_authvar_presence_seed seed;
	uint64_t result = 0;

	reset_fixture();
	assert(payload_mm_authvar_presence_handoff_reserve() == CB_SUCCESS);
	loader = loader_seed();
	assert(payload_mm_authvar_presence_handoff_loader_provision(&slot,
		(void *)&slot) == CB_ERR);
	assert(payload_mm_authvar_presence_handoff_loader_provision(&slot,
		&loader) == CB_SUCCESS);

	reset_fixture();
	assert(payload_mm_authvar_presence_handoff_reserve() == CB_SUCCESS);
	loader = loader_seed();
	assert(payload_mm_authvar_presence_handoff_loader_provision(NULL,
		&loader) == CB_ERR);
	assert(bytes_zero(&loader, sizeof(loader)));
	assert(payload_mm_authvar_presence_handoff_loader_provision(&slot,
		NULL) == CB_ERR);

	reset_fixture();
	prepare();
	loader = loader_seed();
	assert(payload_mm_authvar_presence_handoff_loader_provision(&slot,
		&loader) == CB_ERR);
	assert(bytes_zero(&loader, sizeof(loader)));

	reset_fixture();
	prepare();
	seed = authority_seed();
	alias_transfer = true;
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) == CB_ERR);
	assert(!self_smi_calls);

	reset_fixture();
	prepare();
	seed = authority_seed();
	assert(payload_mm_authvar_presence_handoff_install(NULL,
		(const void *)(UINTPTR_MAX - sizeof(seed) + 2U)) == CB_ERR);
	assert(payload_mm_authvar_presence_handoff_install(NULL, &seed) ==
		CB_SUCCESS);

	reset_fixture();
	prepare();
	descriptor.channel_identity = slot.channel_identity;
	allowed_descriptor = &descriptor;
	allowed_result = &descriptor;
	assert(payload_mm_authvar_presence_handoff_receive(&slot, &descriptor, 0,
		(uint64_t *)&descriptor) == CB_ERR);

	reset_fixture();
	prepare();
	slot_snapshot = slot;
	allowed_descriptor = &slot;
	allowed_result = &result;
	assert(payload_mm_authvar_presence_handoff_receive(&slot,
		(const void *)&slot, 0, &result) == CB_ERR);
	assert(!memcmp(&slot, &slot_snapshot, sizeof(slot)));

	reset_fixture();
	prepare();
	descriptor.channel_identity = slot.channel_identity;
	allowed_descriptor = &descriptor;
	allowed_result = &slot;
	assert(payload_mm_authvar_presence_handoff_receive(&slot, &descriptor, 0,
		(uint64_t *)&slot) == CB_ERR);

	reset_fixture();
	prepare();
	assert(payload_mm_authvar_presence_handoff_receive(&slot,
		(const void *)(UINTPTR_MAX - sizeof(descriptor) + 2U), 0,
		&result) == CB_ERR);
}

static void receiver_ownership_races(void)
{
	struct payload_mm_authvar_presence_handoff_descriptor descriptor;
	struct payload_mm_authvar_presence_handoff_slot snapshot;
	struct receive_thread_arguments first;
	struct receive_thread_arguments second;
	uint64_t first_result = UINT64_MAX;
	uint64_t second_result = UINT64_MAX;
	pthread_t first_thread;
	pthread_t second_thread;

	reset_fixture();
	prepare();
	descriptor = (struct payload_mm_authvar_presence_handoff_descriptor) {
		.channel_identity = slot.channel_identity,
		.request_base = (uintptr_t)transfer,
		.descriptor_cookie = 1U,
	};
	allowed_descriptor = &descriptor;
	allowed_result = &first_result;
	block_after_receiver_claim = true;
	first = (struct receive_thread_arguments) {
		.descriptor = &descriptor,
		.result = &first_result,
	};
	snapshot = slot;
	assert(!pthread_create(&first_thread, NULL, receive_thread, &first));
	assert(!pthread_mutex_lock(&receiver_mutex));
	while (!receiver_claim_entered)
		assert(!pthread_cond_wait(&receiver_cond, &receiver_mutex));
	assert(!pthread_mutex_unlock(&receiver_mutex));
	payload_mm_authvar_presence_handoff_receiver_abort();
	assert(!memcmp(&slot, &snapshot, sizeof(slot)) &&
		first_result == UINT64_MAX);
	assert(!pthread_mutex_lock(&receiver_mutex));
	receiver_claim_release = true;
	assert(!pthread_cond_broadcast(&receiver_cond));
	assert(!pthread_mutex_unlock(&receiver_mutex));
	assert(!pthread_join(first_thread, NULL));
	assert(first.status == CB_ERR && slot_scrubbed());

	reset_fixture();
	prepare();
	descriptor.channel_identity = slot.channel_identity;
	descriptor.request_base = (uintptr_t)transfer;
	descriptor.descriptor_cookie = 1U;
	allowed_descriptor = &descriptor;
	allowed_result = &first_result;
	allowed_result_secondary = &second_result;
	first_result = UINT64_MAX;
	second_result = UINT64_MAX;
	block_after_receiver_claim = true;
	first = (struct receive_thread_arguments) {
		.descriptor = &descriptor,
		.result = &first_result,
	};
	second = (struct receive_thread_arguments) {
		.descriptor = &descriptor,
		.result = &second_result,
	};
	assert(!pthread_create(&first_thread, NULL, receive_thread, &first));
	assert(!pthread_mutex_lock(&receiver_mutex));
	while (!receiver_claim_entered)
		assert(!pthread_cond_wait(&receiver_cond, &receiver_mutex));
	assert(!pthread_mutex_unlock(&receiver_mutex));
	assert(!pthread_create(&second_thread, NULL, receive_thread, &second));
	assert(!pthread_join(second_thread, NULL));
	assert(second.status == CB_ERR && second_result == UINT64_MAX);
	assert(!pthread_mutex_lock(&receiver_mutex));
	receiver_claim_release = true;
	assert(!pthread_cond_broadcast(&receiver_cond));
	assert(!pthread_mutex_unlock(&receiver_mutex));
	assert(!pthread_join(first_thread, NULL));
	assert(first.status == CB_ERR && slot_scrubbed());

	reset_fixture();
	prepare();
	payload_mm_authvar_presence_handoff_receiver_abort();
	assert(slot_scrubbed());
	payload_mm_authvar_presence_handoff_abort();
	assert(payload_mm_authvar_presence_handoff_sender_scrubbed_test());
}

static void receiver_cleanup_publication_order(void)
{
	struct payload_mm_authvar_presence_handoff_descriptor descriptor;
	struct receive_thread_arguments arguments;
	uint64_t result = UINT64_MAX;
	pthread_t thread;

	reset_fixture();
	prepare();
	descriptor = (struct payload_mm_authvar_presence_handoff_descriptor) {
		.channel_identity = slot.channel_identity,
		.request_base = (uintptr_t)transfer,
		.descriptor_cookie = 1U,
	};
	allowed_descriptor = &descriptor;
	allowed_result = &result;
	block_during_cleanup = true;
	arguments = (struct receive_thread_arguments) {
		.descriptor = &descriptor,
		.result = &result,
	};
	assert(!pthread_create(&thread, NULL, receive_thread, &arguments));
	assert(!pthread_mutex_lock(&receiver_mutex));
	while (!cleanup_entered)
		assert(!pthread_cond_wait(&receiver_cond, &receiver_mutex));
	assert(!pthread_mutex_unlock(&receiver_mutex));
	assert(!payload_mm_authvar_presence_handoff_slot_terminal_test(&slot));
	assert(!slot_scrubbed());
	assert(!pthread_mutex_lock(&receiver_mutex));
	cleanup_release = true;
	assert(!pthread_cond_broadcast(&receiver_cond));
	assert(!pthread_mutex_unlock(&receiver_mutex));
	assert(!pthread_join(thread, NULL));
	assert(arguments.status == CB_ERR);
	assert(payload_mm_authvar_presence_handoff_slot_terminal_test(&slot));
	assert(slot_scrubbed());
}

static void provision_and_delivery_races(void)
{
	struct payload_mm_authvar_presence_handoff_loader_seed loader;
	struct payload_mm_authvar_presence_seed seed;
	pthread_t thread;

	reset_fixture();
	assert(payload_mm_authvar_presence_handoff_reserve() == CB_SUCCESS);
	loader = loader_seed();
	abort_after_verifier_publish = true;
	assert(payload_mm_authvar_presence_handoff_loader_provision(&slot,
		&loader) == CB_ERR);
	assert(bytes_zero(&loader, sizeof(loader)));

	reset_fixture();
	prepare();
	seed = authority_seed();
	block_before_delivery = true;
	assert(!pthread_create(&thread, NULL, install_thread, &seed));
	assert(!pthread_mutex_lock(&delivery_mutex));
	while (!delivery_entered)
		assert(!pthread_cond_wait(&delivery_cond, &delivery_mutex));
	payload_mm_authvar_presence_handoff_abort();
	delivery_release = true;
	assert(!pthread_cond_broadcast(&delivery_cond));
	assert(!pthread_mutex_unlock(&delivery_mutex));
	assert(!pthread_join(thread, NULL));
	assert(install_thread_result == CB_ERR && !install_calls);
	assert(bytes_zero(transfer, sizeof(transfer)));
}

int main(void)
{
	success();
	cold_and_resolution_failures();
	tag_and_seed_failures();
	receiver_failures();
	source_mutation_failures();
	abort_and_replay();
	alias_and_overflow_failures();
	provision_and_delivery_races();
	receiver_ownership_races();
	receiver_cleanup_publication_order();
	return 0;
}
