/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef TEST_PROVIDER_CONFIRMED_SETUP
#define TEST_PROVIDER_CONFIRMED_SETUP 0
#endif

#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_certdb.h>
#include <boot/payload_mm_authvar_record.h>
#if TEST_PROVIDER_CONFIRMED_SETUP
#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_presence_backing.h>
#include <bootmem.h>
#endif
#include <commonlib/helpers.h>
#include <commonlib/payload_mm_authvar_fv.h>
#include <fcntl.h>
#include <unistd.h>

/* Real bootstrap, authority, media wrapper, executor and service transaction.
 * Receipt identity, current-wave admission and SPI hardware remain host models. */
#define TEST_REAL_BOOTSTRAP_STACK
#include "payload_mm_authvar_service_bootstrap_test.c"

#undef assert
extern int dprintf(int descriptor, const char *format, ...);
#define assert(condition) do { if (!(condition)) { \
	dprintf(2, "provider stack line %d: %s\n", __LINE__, #condition); abort(); \
} } while (0)

static uint8_t flash_bytes[3U * CONFIG_SMMSTORE_BLOCK_SIZE];
static struct lb_authvar_service_endpoint descriptor;
static bool runtime_admitted = true;
static bool mutate_mailbox;
static bool recursive_execute;
static bool deny_after_program;
static unsigned int program_count;
static unsigned int program_baseline;
static struct payload_mm_authvar_service_frame *shared_mailbox;
static bool scrub_guard;
static unsigned int private_scrubs;
static unsigned int body_copies;
static bool scrub_probe_active;
static bool authenticated_set;
static uint64_t expected_reply;
static uint8_t expected_binding[32];
static bool delivery_active;
static unsigned int delivery_checks;
static unsigned int delivery_media;
static unsigned int delivery_denied_stage;
static uint8_t delivery_original[65536];

#if TEST_PROVIDER_CONFIRMED_SETUP
static uint8_t confirmed_capability[32];
static uint8_t *confirmed_private_request;
static size_t confirmed_tamper_offset;
static bool confirmed_tamper_armed;
static bool confirmed_tampered;
static uint8_t confirmed_original[65536];
static uint8_t confirmed_flash_before[sizeof(flash_bytes)];
static const uint8_t key_guids[2][16] = {
	{ 0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	  0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c },
	{ 0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
	  0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f },
};
static const uint8_t key_names[5][8] = {
	{ 'P', 0, 'K', 0, 0, 0 }, { 'K', 0, 'E', 0, 'K', 0, 0, 0 },
	{ 'd', 0, 'b', 0, 0, 0 }, { 'd', 0, 'b', 0, 'x', 0, 0, 0 },
	{ 'd', 0, 'b', 0, 't', 0, 0, 0 },
};
static const uint8_t key_delete[40] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	24, 0, 0, 0, 0, 2, 0xf1, 0x0e,
	0x9d, 0xd2, 0xaf, 0x4a, 0xdf, 0x68, 0xee, 0x49,
	0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7,
};
static const uint8_t confirmed_guids[2][16] = {
	{ 0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
	  0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44 },
	{ 0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
	  0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f },
};
static const uint8_t confirmed_names[2][34] = {
	{ 'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	  'o', 0, 't', 0, 'E', 0, 'n', 0, 'a', 0, 'b', 0, 'l', 0, 'e', 0, 0, 0 },
	{ 'C', 0, 'u', 0, 's', 0, 't', 0, 'o', 0, 'm', 0, 'M', 0, 'o', 0,
	  'd', 0, 'e', 0, 0, 0 },
};

/* Hardware placement/protection is modeled; receipt and authority owners are real. */
static bool confirmed_storage(void *unused, const void *base, size_t size)
{
	(void)unused;
	return smm_invocation_runtime_range_is_protected(&runtime_view, base, size) == CB_SUCCESS;
}

static bool confirmed_dma(void *unused, uint64_t base, uint64_t size)
{
	(void)unused;
	return wave_admitted && base == 0x200000U && size == 4096U;
}

static bool confirmed_rendezvous(void *unused)
{
	(void)unused;
	return wave_admitted;
}

static enum cb_err confirmed_provision(void *unused, uint64_t generation,
	uint8_t capability[32])
{
	(void)unused;
	assert(generation == 9U);
	for (size_t index = 0; index < 32U; index++)
		capability[index] = (uint8_t)(index + 1U);
	memcpy(confirmed_capability, capability, sizeof(confirmed_capability));
	return CB_SUCCESS;
}

static void confirmed_reset(void *unused)
{
	(void)unused;
	abort();
}

static void __noreturn confirmed_fail_stop(void *unused)
{
	(void)unused;
	abort();
}

static void confirmed_install(void)
{
	struct bootmem_reservation_receipt_authority signer = {0};
	struct bootmem_aligned_reservation_handle handle = { .opaque = { 3, 8 } };
	struct bootmem_reservation_receipt backing_receipt;
	struct payload_mm_authvar_presence_policy policy = {
		.revision = 2, .size = sizeof(policy),
		.endpoint = {
			.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT, .size = 64,
			.revision = 1, .header_size = 64,
			.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS, .generation = 9,
			.communication_base = 0x200000U, .communication_size = 80,
			.message_size = 80, .transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
			.trigger_width = 1, .trigger_address = 0xb2, .trigger_value = 0xe8,
			.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE, .capability_size = 32,
		},
		.backing = {
			.revision = 1, .size = sizeof(policy.backing), .base = 0x200000U,
			.bytes = 4096U, .generation = 9, .tag = BM_MEM_RESERVED,
		},
		.provision = confirmed_provision, .dma_protected = confirmed_dma,
		.cpu_rendezvous_active = confirmed_rendezvous,
		.cold_reset = confirmed_reset, .fail_stop = confirmed_fail_stop,
	};
	uint8_t key[32], secret[32], raw[96] __aligned(8) = {0};
	uint32_t value32;
	uint64_t value64;

	assert(mmap((void *)0x200000U, 4096U, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)0x200000U);
	for (size_t index = 0; index < sizeof(key); index++)
		key[index] = (uint8_t)(0x80U + index);
	memcpy(secret, key, sizeof(secret));
	assert(bootmem_reservation_receipt_provision(&signer, &presence_backing_verifier,
		secret, 1, 9, &handle) == CB_SUCCESS);
	value32 = 1; memcpy(raw, &value32, 4);
	value32 = 96; memcpy(raw + 4, &value32, 4);
	value32 = 1; memcpy(raw + 8, &value32, 4);
	value64 = 9; memcpy(raw + 16, &value64, 8);
	value64 = 1; memcpy(raw + 24, &value64, 8);
	value32 = 3; memcpy(raw + 32, &value32, 4);
	value32 = 8; memcpy(raw + 36, &value32, 4);
	value64 = 0x200000U; memcpy(raw + 40, &value64, 8);
	value64 = 4096U; memcpy(raw + 48, &value64, 8);
	value32 = 0x10002U; memcpy(raw + 56, &value32, 4);
	value32 = 1; memcpy(raw + 60, &value32, 4);
	assert(bootmem_reservation_receipt_mac(key, raw, 64, raw + 64) == CB_SUCCESS);
	memcpy(&backing_receipt, raw, sizeof(raw));
	bootmem_reservation_receipt_close(&signer);
	assert(payload_mm_authvar_presence_backing_evidence_publish(&presence_backing_verifier,
		&backing_receipt, &policy.backing) == CB_SUCCESS);
	for (size_t index = 0; index < sizeof(backing_receipt); index++)
		assert(((const uint8_t *)&backing_receipt)[index] == 0);
	assert(payload_mm_authvar_presence_authority_install(&policy,
		confirmed_storage, NULL) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_backing_evidence_take(&policy.backing) == CB_ERR);
}

static void confirmed_send(uint64_t request_id, uint32_t action, uint32_t value,
	uint64_t status, bool wrong_capability)
{
	struct payload_mm_authvar_confirmed_frame *frame = (void *)shared_mailbox;

	private_scrubs = body_copies = 0;
	expected_reply = status;
	memset(shared_mailbox, 0, 65536U);
	*frame = (struct payload_mm_authvar_confirmed_frame) {
		.service = {
			.revision = 4, .header_size = 184, .operation = 11, .flags = action,
			.generation = 9, .request_id = request_id,
			.status = UINT64_MAX, .completion = UINT32_MAX,
		},
		.value = value,
	};
	memcpy((uint8_t *)frame + 144, confirmed_capability, 32);
	if (wrong_capability)
		((uint8_t *)frame)[144] ^= 1;
	assert(((uint8_t *)frame)[176] == value && !frame->result_flags);
	assert(payload_mm_authvar_service_request_validate(&descriptor, frame, 65536U) == CB_SUCCESS);
	assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
	assert(frame->service.status == status && frame->service.completion == 0);
	assert(frame->value == value && !frame->result_flags && frame->service.flags == action);
	assert(private_scrubs == 2 && body_copies == 1);
	for (size_t index = 144; index < 176; index++)
		assert(((uint8_t *)frame)[index] == 0);
	for (size_t index = 184; index < 65536; index++)
		assert(((uint8_t *)frame)[index] == 0);
}

static void confirmed_variable(uint32_t key, uint64_t request_id, bool write, uint8_t value)
{
	const uint32_t name_size = key ? 22U : 34U;
	const size_t data_offset = 144U + descriptor.maximum_name_size;

	assert(key < 2);
	private_scrubs = body_copies = 0;
	expected_reply = write ? PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION :
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	memset(shared_mailbox, 0, 65536U);
	*shared_mailbox = (struct payload_mm_authvar_service_frame) {
		.revision = 3, .header_size = 144, .operation = write ? 3U : 1U,
		.generation = 9, .request_id = request_id, .name_size = name_size,
		.attributes = write ? 3U : 0U, .data_size = write ? 1U : 0U,
		.data_capacity = write ? 0U : descriptor.maximum_data_size,
		.status = UINT64_MAX, .completion = UINT32_MAX,
	};
	memcpy(shared_mailbox->vendor_guid, confirmed_guids[key], 16);
	memcpy((uint8_t *)shared_mailbox + 144, confirmed_names[key], name_size);
	if (write)
		((uint8_t *)shared_mailbox)[data_offset] = value;
	assert(payload_mm_authvar_service_request_validate(&descriptor, shared_mailbox, 65536U) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
	assert(shared_mailbox->status == expected_reply && shared_mailbox->completion == 0);
	assert(private_scrubs == 2 && body_copies == 1);
	if (!write)
		assert(shared_mailbox->result_data_size == 1 &&
			shared_mailbox->result_attributes == 3 &&
			((uint8_t *)shared_mailbox)[data_offset] == value);
}

static void confirmed_scenario(const char *scenario)
{
	struct lb_authvar_service_endpoint unknown = descriptor;
	struct payload_mm_authvar_confirmed_frame *frame = (void *)shared_mailbox;

	unknown.flags |= 1U << 11;
	assert(payload_mm_authvar_service_endpoint_validate(&unknown) == CB_ERR);
	memset(shared_mailbox, 0, 65536U);
	*frame = (struct payload_mm_authvar_confirmed_frame) {
		.service = {
			.revision = 4, .header_size = 184, .operation = 11, .flags = 1,
			.generation = 9, .request_id = 1,
			.status = UINT64_MAX, .completion = UINT32_MAX,
		},
	};
	memcpy((uint8_t *)frame + 144, confirmed_capability, 32);
	assert(payload_mm_authvar_service_request_validate(&descriptor, frame, 65536U) == CB_SUCCESS);
	frame->service.flags = 4;
	assert(payload_mm_authvar_service_execute() == CB_ERR_ARG);
	frame->service.flags = 1;
	frame->value = 2;
	assert(payload_mm_authvar_service_execute() == CB_ERR_ARG);
	frame->value = 0;
	frame->service.reserved0 = 1;
	assert(payload_mm_authvar_service_execute() == CB_ERR_ARG);
	frame->service.reserved0 = 0;
	frame->service.request_id = 0;
	assert(payload_mm_authvar_service_execute() == CB_ERR_ARG);
	assert(frame->service.status == UINT64_MAX && frame->service.completion == UINT32_MAX);
	assert(!program_count && !body_copies);

	if (!strcmp(scenario, "confirmed-closed")) {
		assert(payload_mm_authvar_presence_authority_restrict(9) == CB_SUCCESS);
		confirmed_send(1, 1, 0, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
	} else if (!strcmp(scenario, "confirmed-wrong-cap")) {
		confirmed_send(1, 1, 0, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		confirmed_send(2, 1, 0, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
	} else if (!strcmp(scenario, "confirmed-runtime")) {
		for (uint32_t operation = 5; operation <= 6; operation++) {
			memset(shared_mailbox, 0, 65536U);
			*shared_mailbox = (struct payload_mm_authvar_service_frame) {
				.revision = 3, .header_size = 144, .operation = operation,
				.generation = 9, .request_id = operation,
				.status = UINT64_MAX, .completion = UINT32_MAX,
			};
			expected_reply = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
			assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
			assert(shared_mailbox->status == expected_reply && !shared_mailbox->completion);
		}
		confirmed_send(7, 1, 0, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
	} else {
		confirmed_variable(0, 1, true, 0);
		confirmed_variable(1, 2, true, 1);
		confirmed_send(3, 1, 0, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		confirmed_variable(0, 4, false, 0);
		confirmed_send(5, 1, 1, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		confirmed_variable(0, 6, false, 1);
		confirmed_send(7, 2, 1, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		confirmed_variable(1, 8, false, 1);
		confirmed_send(9, 2, 0, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		confirmed_variable(1, 10, false, 0);
		if (!strcmp(scenario, "confirmed-replay")) {
			confirmed_send(9, 2, 0, PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, false);
			confirmed_send(11, 2, 0, PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
		}
	}
	assert(munmap((void *)0x200000U, 4096U) == 0);
}
#endif

/* The fixed platform binding is modeled here; the actual walker has its own gate. */
enum cb_err platform_payload_mm_authvar_service_delivery_held(
	enum payload_mm_authvar_delivery_stage stage,
	const struct payload_mm_authvar_range *communication)
{
	assert(delivery_active && communication);
	assert(communication->base == 0x100000U && communication->size == 65536U);
	assert(delivery_checks == (unsigned int)stage);
	assert(stage != PAYLOAD_MM_AUTHVAR_DELIVERY_BEGIN || !delivery_media);
	assert(stage != PAYLOAD_MM_AUTHVAR_DELIVERY_RECHECK || delivery_media);
	delivery_checks++;
	return delivery_checks == delivery_denied_stage ? CB_ERR : CB_SUCCESS;
}

static size_t read_fixture(const char *path, uint8_t *buffer, size_t capacity)
{
	const int input = open(path, O_RDONLY);
	uint8_t extra;
	size_t used = 0;
	ssize_t count = 0;

	assert(input >= 0);
	while (used < capacity && (count = read(input, buffer + used, capacity - used)) > 0)
		used += count;
	assert(count >= 0 && used && (used < capacity || read(input, &extra, 1) == 0));
	assert(close(input) == 0);
	return used;
}

#if TEST_PROVIDER_CONFIRMED_SETUP
static size_t key_fixture(const char *directory, const char *label, uint32_t key,
	uint8_t *data, size_t capacity)
{
	char path[512];
	const int count = snprintf(path, sizeof(path), "%s/key-%u-%s.auth2",
		directory, key, label);

	assert(count > 0 && (size_t)count < sizeof(path));
	return read_fixture(path, data, capacity);
}

/* Literal wire offsets deliberately do not use the native v5 structure. */
static void key_request(uint64_t request_id, uint32_t key, uint32_t mutation,
	const uint8_t *data, size_t size, bool wrong_capability)
{
	uint8_t *bytes = (void *)shared_mailbox;
	uint32_t value32;
	uint64_t value64;

	assert(key >= 1 && key <= 5 && size <= descriptor.maximum_data_size);
	memset(bytes, 0, 65536U);
	value32 = 5; memcpy(bytes, &value32, 4);
	value32 = 192; memcpy(bytes + 4, &value32, 4);
	value32 = 11; memcpy(bytes + 8, &value32, 4);
	value32 = 4; memcpy(bytes + 12, &value32, 4);
	value64 = 9; memcpy(bytes + 16, &value64, 8);
	memcpy(bytes + 24, &request_id, 8);
	value32 = mutation == 2 ? 0x67U : 0x27U; memcpy(bytes + 48, &value32, 4);
	value32 = size; memcpy(bytes + 56, &value32, 4);
	value64 = UINT64_MAX; memcpy(bytes + 96, &value64, 8);
	value32 = UINT32_MAX; memcpy(bytes + 140, &value32, 4);
	memcpy(bytes + 144, confirmed_capability, 32);
	memcpy(bytes + 184, &key, 4);
	memcpy(bytes + 188, &mutation, 4);
	memcpy(bytes + 192, data, size);
	if (wrong_capability)
		bytes[144] ^= 1;
	private_scrubs = body_copies = 0;
	confirmed_private_request = NULL;
}

static void key_send(uint64_t request_id, uint32_t key, uint32_t mutation,
	const uint8_t *data, size_t size, uint64_t status, bool wrong_capability)
{
	const struct payload_mm_authvar_confirmed_key_frame *frame = (void *)shared_mailbox;
	const unsigned int programs = program_count;

	key_request(request_id, key, mutation, data, size, wrong_capability);
	expected_reply = status;
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		memcpy(confirmed_flash_before, flash_bytes, sizeof(flash_bytes));
	assert(payload_mm_authvar_service_request_validate(&descriptor, shared_mailbox,
		65536U) == CB_SUCCESS);
	assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
	assert(shared_mailbox->status == status && shared_mailbox->completion == 0);
	assert(shared_mailbox->revision == 5 && shared_mailbox->header_size == 192 &&
		shared_mailbox->operation == 11 && shared_mailbox->flags == 4 &&
		shared_mailbox->generation == 9 && shared_mailbox->request_id == request_id &&
		shared_mailbox->attributes == (mutation == 2 ? 0x67U : 0x27U) &&
		shared_mailbox->data_size == size && frame->key_id == key &&
		frame->mutation == mutation && !frame->confirmed.value &&
		!frame->confirmed.result_flags);
	assert(private_scrubs == 2 && body_copies == 1);
	for (size_t index = 144; index < 176; index++)
		assert(((const uint8_t *)frame)[index] == 0);
	for (size_t index = 192; index < 65536; index++)
		assert(((const uint8_t *)frame)[index] == 0);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		assert(program_count == programs);
		assert(!memcmp(confirmed_flash_before, flash_bytes, sizeof(flash_bytes)));
	}
}

static void key_ordinary(uint64_t request_id, const uint8_t guid[16],
	const uint8_t *name, uint32_t name_size, bool write, const uint8_t *data,
	size_t size, uint32_t attributes, uint64_t status)
{
	const size_t data_offset = 144U + descriptor.maximum_name_size;
	const unsigned int programs = program_count;

	assert(size <= descriptor.maximum_data_size);
	memset(shared_mailbox, 0, 65536U);
	*shared_mailbox = (struct payload_mm_authvar_service_frame) {
		.revision = 3, .header_size = 144, .operation = write ? 3U : 1U,
		.generation = 9, .request_id = request_id, .name_size = name_size,
		.attributes = write ? attributes : 0U, .data_size = write ? size : 0U,
		.data_capacity = write ? 0U : descriptor.maximum_data_size,
		.status = UINT64_MAX, .completion = UINT32_MAX,
	};
	memcpy(shared_mailbox->vendor_guid, guid, 16);
	memcpy((uint8_t *)shared_mailbox + 144, name, name_size);
	if (write) {
		memcpy((uint8_t *)shared_mailbox + data_offset, data, size);
		memcpy(confirmed_flash_before, flash_bytes, sizeof(flash_bytes));
	}
	expected_reply = status;
	private_scrubs = body_copies = 0;
	assert(payload_mm_authvar_service_request_validate(&descriptor, shared_mailbox,
		65536U) == CB_SUCCESS);
	assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
	assert(shared_mailbox->status == status && shared_mailbox->completion == 0);
	assert(private_scrubs == 2 && body_copies == 1);
	if (write) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(program_count == programs);
		assert(!memcmp(confirmed_flash_before, flash_bytes, sizeof(flash_bytes)));
	} else if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		assert(shared_mailbox->result_data_size == size &&
			shared_mailbox->result_attributes == attributes);
		assert(!memcmp((uint8_t *)shared_mailbox + data_offset, data, size));
	}
}

static void key_get(uint64_t request_id, uint32_t key, const uint8_t *data, size_t size)
{
	assert(key >= 1 && key <= 5);
	key_ordinary(request_id, key_guids[key > 2], key_names[key - 1],
		key == 1 || key == 3 ? 6U : 8U, false, data, size, 0x27,
		data ? PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS : PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
}

static void key_mode(uint64_t request_id, const char *name, uint8_t value)
{
	uint8_t encoded[32] = {0};
	const size_t length = strlen(name);

	assert(length < sizeof(encoded) / 2);
	for (size_t index = 0; index < length; index++)
		encoded[index * 2] = name[index];
	key_ordinary(request_id, key_guids[0], encoded, (length + 1U) * 2U,
		false, &value, 1, 6, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
}

static void key_timestamp(uint32_t key, const uint8_t timestamp[16])
{
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = CONFIG_SMMSTORE_BLOCK_SIZE,
		.maximum_name_size = 4096, .maximum_data_size = CONFIG_SMMSTORE_BLOCK_SIZE,
		.maximum_records = 3072,
	};
	struct payload_mm_authvar_store_entry entry;
	bool found;

	assert(payload_mm_authvar_store_find_one(&entry, &found, flash_bytes + 72,
		CONFIG_SMMSTORE_BLOCK_SIZE - 72, &limits, key_guids[key > 2],
		key_names[key - 1], key == 1 || key == 3 ? 6U : 8U) == CB_SUCCESS && found);
	assert(!memcmp(flash_bytes + 72 + entry.record_offset + 16, timestamp, 16));
}

static void confirmed_keys(const char *scenario, const char *directory)
{
	const uint8_t vendor_guid[16] = {
		0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
		0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
	};
	const uint8_t vendor_name[] = {
		'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
		'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
	};
	uint8_t data[8192], first[4096], second[4096], combined[8192];
	uint8_t vendor_value = 1;
	char path[512];
	size_t first_size, second_size, size;
	uint64_t request_id = 1;
	int count;

	count = snprintf(path, sizeof(path), "%s/key-a.esl", directory);
	assert(count > 0 && (size_t)count < sizeof(path));
	first_size = read_fixture(path, first, sizeof(first));
	count = snprintf(path, sizeof(path), "%s/key-b.esl", directory);
	assert(count > 0 && (size_t)count < sizeof(path));
	second_size = read_fixture(path, second, sizeof(second));
	assert(first_size + second_size <= sizeof(combined));
	assert(first_size != second_size || memcmp(first, second, first_size));
	memcpy(combined, first, first_size);
	memcpy(combined + first_size, second, second_size);
	confirmed_send(request_id++, 2, 1, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
	key_mode(request_id++, "SetupMode", 1);
	key_mode(request_id++, "SecureBoot", 0);
	key_mode(request_id++, "VendorKeys", 1);
	key_ordinary(request_id++, vendor_guid, vendor_name, sizeof(vendor_name), false,
		&vendor_value, 1, 0x23, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	size = key_fixture(directory, "replace-a", 1, data, sizeof(data));
	if (!strcmp(scenario, "confirmed-keys-pbk")) {
		static const char *const invalid[] = {"pbk-short", "pbk-long", "pbk-type"};
		const uint8_t raw_type[16] = {
			0xe8, 0x66, 0x57, 0x3c, 0x9c, 0x26, 0x34, 0x4e,
			0xaa, 0x14, 0xed, 0x77, 0x6e, 0x85, 0xb3, 0xb6,
		};
		const uint8_t owner_a[16] = {
			0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
			0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		};
		const uint8_t owner_b[16] = {
			0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
			0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
		};
		uint8_t raw_first[300], raw_second[300], raw_combined[600];
		unsigned int programs;

		count = snprintf(path, sizeof(path), "%s/key-pbk-a.esl", directory);
		assert(count > 0 && (size_t)count < sizeof(path));
		assert(read_fixture(path, raw_first, sizeof(raw_first)) == sizeof(raw_first));
		count = snprintf(path, sizeof(path), "%s/key-pbk-b.esl", directory);
		assert(count > 0 && (size_t)count < sizeof(path));
		assert(read_fixture(path, raw_second, sizeof(raw_second)) == sizeof(raw_second));
		assert(!memcmp(raw_first, raw_type, sizeof(raw_type)) &&
			!memcmp(raw_second, raw_type, sizeof(raw_type)));
		assert(!memcmp(raw_first + 28, owner_a, sizeof(owner_a)) &&
			!memcmp(raw_second + 28, owner_b, sizeof(owner_b)) &&
			!memcmp(raw_first + 44, raw_second + 44, 256));
		memcpy(raw_combined, raw_first, sizeof(raw_first));
		memcpy(raw_combined + sizeof(raw_first), raw_second, sizeof(raw_second));
		/* Raw RSA is supported for KEK, but the PK profile still requires X.509. */
		size = key_fixture(directory, "pbk-a", 1, data, sizeof(data));
		assert(size == 340 && !memcmp(data + 40, raw_first, sizeof(raw_first)));
		key_send(request_id++, 1, 1, data, size,
			PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
		key_get(request_id++, 1, NULL, 0);
		size = key_fixture(directory, "replace-a", 1, data, sizeof(data));
		key_send(request_id++, 1, 1, data, size, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		key_get(request_id++, 1, first, first_size);
		key_mode(request_id++, "SetupMode", 0);
		size = key_fixture(directory, "pbk-a", 2, data, sizeof(data));
		assert(size == 340 && !memcmp(data + 40, raw_first, sizeof(raw_first)));
		/* Empty CMS cannot obtain the ordinary User-mode authentication grant. */
		key_ordinary(request_id++, key_guids[0], key_names[1], 8, true,
			data, size, 0x27, PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		key_get(request_id++, 2, NULL, 0);
		programs = program_count;
		key_send(request_id++, 2, 1, data, size, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		assert(program_count > programs);
		key_get(request_id++, 2, raw_first, sizeof(raw_first));
		key_timestamp(2, data);
		programs = program_count;
		size = key_fixture(directory, "pbk-b", 2, data, sizeof(data));
		assert(size == 340 && !memcmp(data + 40, raw_second, sizeof(raw_second)));
		key_send(request_id++, 2, 2, data, size, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		assert(program_count > programs);
		key_get(request_id++, 2, raw_combined, sizeof(raw_combined));
		key_timestamp(2, data);
		for (size_t index = 0; index < ARRAY_SIZE(invalid); index++) {
			size = key_fixture(directory, invalid[index], 2, data, sizeof(data));
			assert(size == (index == 0 ? 339U : index == 1 ? 341U : 340U));
			key_send(request_id++, 2, 1, data, size,
				PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
			key_get(request_id++, 2, raw_combined, sizeof(raw_combined));
		}
		size = key_fixture(directory, "pbk-a", 2, data, sizeof(data));
		key_send(request_id++, 2, 1, data, size, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		key_get(request_id++, 2, raw_first, sizeof(raw_first));
		key_timestamp(2, data);
		key_mode(request_id++, "VendorKeys", 0);
		key_get(request_id++, 1, first, first_size);
	} else if (!strcmp(scenario, "confirmed-keys-closed")) {
		assert(payload_mm_authvar_presence_authority_restrict(9) == CB_SUCCESS);
		key_send(request_id++, 1, 1, data, size,
			PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
	} else if (!strcmp(scenario, "confirmed-keys-wrong-cap")) {
		key_send(request_id++, 1, 1, data, size,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		key_send(request_id++, 1, 1, data, size,
			PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
	} else if (!strcmp(scenario, "confirmed-keys-payload-drift") ||
		   !strcmp(scenario, "confirmed-keys-tail-drift")) {
		/* Warm recovery before proving no writes during the isolated tampered request. */
		key_get(request_id++, 1, NULL, 0);
		key_request(request_id++, 1, 1, data, size, false);
		confirmed_tamper_offset = !strcmp(scenario, "confirmed-keys-tail-drift") ?
			65535U : 192U + size - 1U;
		memcpy(confirmed_original, shared_mailbox, sizeof(confirmed_original));
		memcpy(confirmed_flash_before, flash_bytes, sizeof(flash_bytes));
		program_baseline = program_count;
		confirmed_tamper_armed = true;
		assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
		assert(false); /* Only the real bootstrap's fail-stop observer may accept this lane. */
	} else if (!strcmp(scenario, "confirmed-keys-der-refusals")) {
		const char *labels[] = { "rsa1024", "p256", "truncated" };

		for (size_t index = 0; index < ARRAY_SIZE(labels); index++) {
			size = key_fixture(directory, labels[index], 1, data, sizeof(data));
			key_send(request_id++, 1, 1, data, size,
				PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
			key_get(request_id++, 1, NULL, 0);
		}
		size = key_fixture(directory, "replace-a", 1, data, sizeof(data));
		key_send(request_id++, 1, 1, data, size, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		key_get(request_id++, 1, first, first_size);
		key_timestamp(1, data);
	} else {
		key_send(request_id++, 1, 1, data, size, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
		key_get(request_id++, 1, first, first_size);
		if (!strcmp(scenario, "confirmed-keys-replay")) {
			key_send(request_id - 2, 1, 1, data, size,
				PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, false);
			key_send(request_id++, 1, 1, data, size,
				PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
		} else if (!strcmp(scenario, "confirmed-keys-runtime")) {
			for (uint32_t operation = 5; operation <= 6; operation++) {
				memset(shared_mailbox, 0, 65536U);
				*shared_mailbox = (struct payload_mm_authvar_service_frame) {
					.revision = 3, .header_size = 144, .operation = operation,
					.generation = 9, .request_id = request_id++,
					.status = UINT64_MAX, .completion = UINT32_MAX,
				};
				expected_reply = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
				assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
				assert(shared_mailbox->status == expected_reply &&
					shared_mailbox->completion == 0);
			}
			key_send(request_id++, 1, 1, data, size,
				PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
		} else if (!strcmp(scenario, "confirmed-keys-selective")) {
			/* Deleting non-PK keys must retain PK in User mode. */
			key_mode(request_id++, "SetupMode", 0);
			for (uint32_t key = 2; key <= 5; key++) {
				unsigned int programs = program_count;

				size = key_fixture(directory, "selective-seed", key, data, sizeof(data));
				assert(size > 40 && !memcmp(data + 40, first, 16));
				key_send(request_id++, key, 1, data, size,
					PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
				assert(program_count > programs);
				key_get(request_id++, key, data + 40, size - 40);
				key_timestamp(key, data);
				programs = program_count;
				size = key_fixture(directory, "selective-first", key, data, sizeof(data));
				assert(size == 40 + first_size + second_size &&
					!memcmp(data + 40, combined, first_size + second_size));
				key_send(request_id++, key, 1, data, size,
					PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
				assert(program_count > programs);
				key_get(request_id++, key, combined, first_size + second_size);
				key_timestamp(key, data);
				programs = program_count;
				size = key_fixture(directory, "selective-list", key, data, sizeof(data));
				assert(size == 40 + second_size && !memcmp(data + 40, second, second_size));
				key_send(request_id++, key, 1, data, size,
					PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
				assert(program_count > programs);
				key_get(request_id++, key, second, second_size);
				key_timestamp(key, data);
				programs = program_count;
				key_send(request_id++, key, 3, key_delete, sizeof(key_delete),
					PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
				assert(program_count > programs);
				key_get(request_id++, key, NULL, 0);
				key_mode(request_id++, "SetupMode", 0);
				key_mode(request_id++, "VendorKeys", 0);
			}
			key_get(request_id++, 1, first, first_size);
		} else {
			assert(!strcmp(scenario, "confirmed-keys"));
			for (uint32_t key = 1; key <= 5; key++) {
				if (key != 1) {
					size = key_fixture(directory, "replace-a", key, data, sizeof(data));
					key_send(request_id++, key, 1, data, size,
						PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
				}
				key_get(request_id++, key, first, first_size);
				key_ordinary(request_id++, key_guids[key > 2], key_names[key - 1],
					key == 1 || key == 3 ? 6U : 8U, true, data, size, 0x27,
					PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
				size = key_fixture(directory, "replace-old", key, data, sizeof(data));
				key_ordinary(request_id++, key_guids[key > 2], key_names[key - 1],
					key == 1 || key == 3 ? 6U : 8U, true, data, size, 0x27,
					PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
				key_get(request_id++, key, first, first_size);
				size = key_fixture(directory, "replace-b", key, data, sizeof(data));
				key_send(request_id++, key, 1, data, size,
					PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
				key_get(request_id++, key, second, second_size);
				key_timestamp(key, data);
				size = key_fixture(directory, "replace-old", key, data, sizeof(data));
				key_send(request_id++, key, 1, data, size,
					PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
				key_get(request_id++, key, first, first_size);
				key_timestamp(key, data);
				size = key_fixture(directory, "append-b", key, data, sizeof(data));
				if (key == 1) {
					const unsigned int programs = program_count;

					key_request(request_id++, key, 2, data, size, false);
					memcpy(confirmed_flash_before, flash_bytes, sizeof(flash_bytes));
					assert(payload_mm_authvar_service_request_validate(&descriptor,
						shared_mailbox, 65536U) == CB_ERR);
					assert(payload_mm_authvar_service_execute() == CB_ERR_ARG);
					assert(shared_mailbox->status == UINT64_MAX &&
						shared_mailbox->completion == UINT32_MAX);
					assert(private_scrubs == 2 && !body_copies &&
						program_count == programs);
					assert(!memcmp(confirmed_flash_before, flash_bytes,
						sizeof(flash_bytes)));
					key_get(request_id++, key, first, first_size);
				} else {
					key_send(request_id++, key, 2, data, size,
						PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
					key_get(request_id++, key, combined, first_size + second_size);
					key_timestamp(key, data);
				}
			}
			key_mode(request_id++, "SetupMode", 0);
			key_mode(request_id++, "VendorKeys", 0);
			vendor_value = 0;
			key_ordinary(request_id++, vendor_guid, vendor_name, sizeof(vendor_name),
				false, &vendor_value, 1, 0x23, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
			for (uint32_t key = 2; key <= 6; key++) {
				const uint32_t target = key == 6 ? 1U : key;

				key_send(request_id++, target, 3, key_delete, sizeof(key_delete),
					PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS, false);
				key_get(request_id++, target, NULL, 0);
			}
			key_mode(request_id++, "SetupMode", 1);
			key_mode(request_id++, "SecureBoot", 0);
			key_mode(request_id++, "VendorKeys", 0);
		}
	}
	assert(munmap((void *)0x200000U, 4096U) == 0);
}
#endif

static void assert_authenticated_commit(void)
{
	const uint8_t private_guid[] = {
		0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	};
	const uint8_t private_name[] = { 'P', 0, 'r', 0, 'i', 0, 'v', 0, 0, 0 };
	const uint8_t certdb_guid[] = {
		0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
		0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
	};
	const uint8_t certdb_name[] = {
		'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
	};
	const uint8_t timestamp[] = { 0xea, 7, 10, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = CONFIG_SMMSTORE_BLOCK_SIZE,
		.maximum_name_size = 4096, .maximum_data_size = CONFIG_SMMSTORE_BLOCK_SIZE,
		.maximum_records = 3072,
	};
	struct payload_mm_authvar_store_entry entry;
	struct payload_mm_authvar_certdb_binding binding;
	const uint8_t *primary = flash_bytes + 72;
	const size_t primary_size = CONFIG_SMMSTORE_BLOCK_SIZE - 72;
	bool found;

	assert(payload_mm_authvar_store_find_one(&entry, &found, primary, primary_size,
		&limits, private_guid, private_name, sizeof(private_name)) == CB_SUCCESS && found);
	assert(entry.attributes == 0x23 && entry.data_size == 7);
	assert(!memcmp(primary + entry.data_offset, "payload", 7));
	assert(!memcmp(primary + entry.record_offset + 16, timestamp, sizeof(timestamp)));
	assert(payload_mm_authvar_store_find_one(&entry, &found, primary, primary_size,
		&limits, certdb_guid, certdb_name, sizeof(certdb_name)) == CB_SUCCESS && found);
	assert(entry.attributes == 0x27 && entry.data_size > 4);
	assert(payload_mm_authvar_certdb_find(primary + entry.data_offset, entry.data_size,
		private_guid, private_name, sizeof(private_name) - 2, &binding) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_OK && binding.size == 32);
	assert(!memcmp(binding.data, expected_binding, sizeof(expected_binding)));
}

void *__real_memcpy(void *destination, const void *source, size_t size);

void *__wrap_memcpy(void *destination, const void *source, size_t size)
{
	void *result;

	if (destination == shared_mailbox && size == 65536U) {
		const struct payload_mm_authvar_service_frame *frame = source;

		assert(shared_mailbox->completion == UINT32_MAX);
		assert(!delivery_active || !CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) || delivery_checks == 2);
		if (frame->status != expected_reply)
			dprintf(2, "provider reply: expected %llx, observed %llx\n",
				(unsigned long long)expected_reply, (unsigned long long)frame->status);
		assert(frame->completion == UINT32_MAX && frame->status == expected_reply);
		body_copies++;
	}
	result = __real_memcpy(destination, source, size);
#if TEST_PROVIDER_CONFIRMED_SETUP
	if (confirmed_tamper_armed && source == shared_mailbox && size == 65536U) {
		assert(destination != shared_mailbox);
		assert(destination != confirmed_original);
		assert(smm_invocation_runtime_range_is_protected(&runtime_view,
			destination, size) == CB_SUCCESS);
		confirmed_private_request = destination;
	}
#endif
	if (destination == shared_mailbox && size == 65536U)
		assert(shared_mailbox->completion == UINT32_MAX);
	return result;
}

void test_real_scrub_observe(const void *buffer, size_t size)
{
	(void)buffer;
	if (scrub_guard && size == 65536U) {
		private_scrubs++;
		assert(!scrub_probe_active);
		scrub_probe_active = true;
		assert(payload_mm_authvar_service_execute() == CB_ERR);
		scrub_probe_active = false;
	}
}

bool platform_payload_mm_authvar_service_finalize_admitted(void)
{
	return wave_admitted;
}

bool platform_payload_mm_authvar_service_runtime_admitted(void)
{
	return runtime_admitted && (!deny_after_program || program_count == program_baseline);
}

void __noreturn test_real_fail_stop(void)
{
#if TEST_PROVIDER_CONFIRMED_SETUP
	if (confirmed_tamper_armed) {
		assert(confirmed_tampered && confirmed_private_request &&
			confirmed_private_request != (uint8_t *)shared_mailbox);
		assert(confirmed_private_request[confirmed_tamper_offset] ==
			(uint8_t)(confirmed_original[confirmed_tamper_offset] ^ 1U));
		assert(program_count == program_baseline && !body_copies && !private_scrubs);
		assert(!memcmp(flash_bytes, confirmed_flash_before, sizeof(flash_bytes)));
		assert(shared_mailbox->completion == UINT32_MAX &&
			shared_mailbox->status == UINT64_MAX);
		assert(!memcmp(shared_mailbox, confirmed_original, sizeof(confirmed_original)));
		_exit(79);
	}
#endif
	if (delivery_active) {
		assert(delivery_denied_stage && delivery_checks == delivery_denied_stage);
		assert(!body_copies && shared_mailbox->completion == UINT32_MAX);
		assert(!memcmp(shared_mailbox, delivery_original, sizeof(delivery_original)));
		assert(delivery_denied_stage != 1 || !delivery_media);
		assert(delivery_denied_stage != 2 || delivery_media);
		_exit(78);
	}
	assert(deny_after_program && program_count > program_baseline);
	assert(shared_mailbox->request_id == 4);
	assert(shared_mailbox->completion == UINT32_MAX);
	assert(shared_mailbox->status == UINT64_MAX);
	if (authenticated_set)
		assert_authenticated_commit();
	_exit(77);
}

static enum payload_mm_authvar_media_result begin(const void *context, uint64_t *generation)
{
	(void)context;
#if TEST_PROVIDER_CONFIRMED_SETUP
	if (confirmed_tamper_armed && !confirmed_tampered) {
		assert(confirmed_private_request && confirmed_tamper_offset < 65536U);
		assert(!memcmp(confirmed_private_request, confirmed_original,
			sizeof(confirmed_original)));
		assert(program_count == program_baseline);
		confirmed_private_request[confirmed_tamper_offset] ^= 1;
		confirmed_tampered = true;
	}
#endif
	if (delivery_active) {
		assert(!CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) || delivery_checks == 1);
		delivery_media++;
	}
	if (recursive_execute) {
		recursive_execute = false;
		assert(payload_mm_authvar_service_execute() == CB_ERR);
	}
	if (mutate_mailbox) {
		shared_mailbox->request_id = UINT64_MAX;
		shared_mailbox->operation = UINT32_MAX;
	}
	*generation = 1;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result read_bytes(const void *context, uint32_t offset,
	void *buffer, size_t size, size_t *completed)
{
	(void)context;
	assert(offset <= sizeof(flash_bytes) && size <= sizeof(flash_bytes) - offset);
	memcpy(buffer, flash_bytes + offset, size);
	*completed = size;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result program(const void *context, uint32_t offset,
	const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;

	(void)context;
	program_count++;
	assert(offset <= sizeof(flash_bytes) && size <= sizeof(flash_bytes) - offset);
	for (size_t index = 0; index < size; index++)
		flash_bytes[offset + index] &= bytes[index];
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result erase(const void *context, uint32_t offset,
	size_t size)
{
	(void)context;
	assert(offset <= sizeof(flash_bytes) && size <= sizeof(flash_bytes) - offset);
	memset(flash_bytes + offset, 0xff, size);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result complete(const void *context)
{
	(void)context;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum cb_err test_real_media_install(void)
{
	const struct payload_mm_authvar_media_port port = {
		.revision = PAYLOAD_MM_AUTHVAR_MEDIA_PORT_REVISION, .size = sizeof(port),
		.begin = begin, .read = read_bytes, .program = program, .erase = erase,
		.sync = complete, .end = complete,
	};

	return payload_mm_authvar_media_install(&port);
}

#if defined(TEST_REAL_RUNTIME_WAVE)
int runtime_provider_fixture_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
	struct payload_mm_authvar_service_frame *mailbox;
	const uint8_t vendor_keys_name[] = {
		'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
		'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
	};
	const uint8_t vendor_keys_value = 1;
	const struct payload_mm_authvar_record_descriptor vendor_keys = {
		.vendor_guid = { 0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
			0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c },
		.name = vendor_keys_name, .name_size = sizeof(vendor_keys_name), .attributes = 0x23,
	};
	const struct payload_mm_authvar_record_span value = { &vendor_keys_value, 1 };
	uint32_t encoded_size;
	uint8_t authenticated_data[8192];
	size_t authenticated_size = 0;
	bool proof_drift;
	bool wrong_content;
	bool capacity_edge;

	assert(argc == 2 || argc == 4 || argc == 5 ||
		(TEST_PROVIDER_CONFIRMED_SETUP && argc == 3 &&
		 !strncmp(argv[1], "confirmed-keys", 14)));
	authenticated_set = argc >= 4;
	wrong_content = argc == 5 && !strcmp(argv[4], "wrong-content");
	assert(argc != 5 || wrong_content);
	if (authenticated_set) {
		authenticated_size = read_fixture(argv[2], authenticated_data,
			sizeof(authenticated_data));
		assert(read_fixture(argv[3], expected_binding, sizeof(expected_binding)) ==
			sizeof(expected_binding));
		if (wrong_content)
			authenticated_data[authenticated_size - 1] ^= 1;
	}
	proof_drift = !strcmp(argv[1], "proof-drift");
	capacity_edge = !strcmp(argv[1], "capacity-edge");
	assert(proof_drift || capacity_edge || !strcmp(argv[1], "normal") ||
#if TEST_PROVIDER_CONFIRMED_SETUP
		!strcmp(argv[1], "confirmed") || !strcmp(argv[1], "confirmed-replay") ||
		!strcmp(argv[1], "confirmed-wrong-cap") || !strcmp(argv[1], "confirmed-closed") ||
		!strcmp(argv[1], "confirmed-runtime") ||
		!strcmp(argv[1], "confirmed-keys") || !strcmp(argv[1], "confirmed-keys-replay") ||
		!strcmp(argv[1], "confirmed-keys-wrong-cap") ||
		!strcmp(argv[1], "confirmed-keys-closed") ||
		!strcmp(argv[1], "confirmed-keys-runtime") ||
		!strcmp(argv[1], "confirmed-keys-der-refusals") ||
		!strcmp(argv[1], "confirmed-keys-selective") ||
		!strcmp(argv[1], "confirmed-keys-pbk") ||
		!strcmp(argv[1], "confirmed-keys-payload-drift") ||
		!strcmp(argv[1], "confirmed-keys-tail-drift") ||
#endif
		!strcmp(argv[1], "mailbox-drift") || !strcmp(argv[1], "delivery") ||
		!strcmp(argv[1], "delivery-begin-denied") ||
		!strcmp(argv[1], "delivery-recheck-denied"));

	runtime_smram_size = UINTPTR_MAX - 0x400000U;
	initialize();
	store.region.size = sizeof(flash_bytes);
	canonical.generation = 9;
	canonical.capability[0] = 1;
	make_service_receipt();
	mailbox = mmap((void *)0x100000U, 65536U, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	assert(mailbox == (void *)0x100000U);
	shared_mailbox = mailbox;
	assert(payload_mm_authvar_fv_format(flash_bytes, sizeof(flash_bytes),
		CONFIG_SMMSTORE_BLOCK_SIZE));
	assert(payload_mm_authvar_record_encode(&vendor_keys, &value, 1,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO, flash_bytes + 100,
		sizeof(flash_bytes) - 100, &encoded_size));
	flash_bytes[102] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	if (authenticated_size) {
		const uint8_t certdb_name[] = {
			'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
		};
		const uint8_t empty_certdb[] = { 4, 0, 0, 0 };
		const struct payload_mm_authvar_record_descriptor certdb = {
			.vendor_guid = { 0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
				0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a },
			.name = certdb_name, .name_size = sizeof(certdb_name), .attributes = 0x27,
		};
		const struct payload_mm_authvar_record_span certdb_value = { empty_certdb, 4 };
		const uint32_t offset = ALIGN_UP(100U + encoded_size, 4U);

		assert(payload_mm_authvar_record_encode(&certdb, &certdb_value, 1,
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO, flash_bytes + offset,
			sizeof(flash_bytes) - offset, &encoded_size));
		flash_bytes[offset + 2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	}
	memset(&seed.seal_channel, 0, sizeof(seed.seal_channel));
	assert(payload_mm_authvar_service_prepare(&service_verifier, &service_receipt) == CB_SUCCESS);
	assert(payload_mm_authvar_smm_service_bootstrap_install(&seed) == CB_SUCCESS);
#if TEST_PROVIDER_CONFIRMED_SETUP
	if (!strncmp(argv[1], "confirmed", 9))
		confirmed_install();
#endif
	assert(payload_mm_authvar_service_finalize() == CB_SUCCESS);
	assert(payload_mm_authvar_service_descriptor_copy(&descriptor) == CB_SUCCESS);
	assert(descriptor.revision == 5);
	assert(!!(descriptor.flags & LB_AUTHVAR_ENDPOINT_CONFIRMED_SETUP) ==
		(TEST_PROVIDER_CONFIRMED_SETUP && !strncmp(argv[1], "confirmed", 9)));
	assert(!!(descriptor.flags & LB_AUTHVAR_ENDPOINT_IMAGE_POLICY_GENERAL) ==
		!CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER));
	assert(descriptor.communication_base == 0x100000U && descriptor.communication_size == 65536U);
	assert(descriptor.trigger_address == APM_CNT && descriptor.trigger_value == 0xfcU);
	assert(descriptor.maximum_name_size == 4096U);
	assert(descriptor.maximum_data_size ==
		(CONFIG_SMMSTORE_BLOCK_SIZE == 4096U ? 8132U : 61296U));
	scrub_guard = true;
#if TEST_PROVIDER_CONFIRMED_SETUP
	if (!strncmp(argv[1], "confirmed", 9)) {
		if (!strncmp(argv[1], "confirmed-keys", 14)) {
			assert(argc == 3);
			confirmed_keys(argv[1], argv[2]);
		} else {
			confirmed_scenario(argv[1]);
		}
		assert(munmap(mailbox, 65536U) == 0);
		return 0;
	}
#endif
	if (!strncmp(argv[1], "delivery", 8)) {
		delivery_active = true;
		delivery_denied_stage = !strcmp(argv[1], "delivery-begin-denied") ? 1U :
			!strcmp(argv[1], "delivery-recheck-denied") ? 2U : 0U;
		expected_reply = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		memset(mailbox, 0, 65536U);
		*mailbox = (struct payload_mm_authvar_service_frame) {
			.revision = 2, .header_size = 144, .operation = 9,
			.generation = 9, .request_id = 1, .data_capacity = 65536U - 4240U,
			.status = UINT64_MAX, .completion = UINT32_MAX,
		};
		assert(payload_mm_authvar_service_execute() == CB_ERR_ARG);
		assert(!delivery_checks && !delivery_media && !body_copies && private_scrubs == 2);
		mailbox->revision = 3;
		mailbox->data_capacity = descriptor.maximum_data_size;
		assert(payload_mm_authvar_service_request_validate(&descriptor, mailbox, 65536U) ==
			CB_SUCCESS);
		memcpy(delivery_original, mailbox, sizeof(delivery_original));
		private_scrubs = 0;
		assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
		assert(!delivery_denied_stage);
		assert(delivery_checks == (CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) ? 2U : 0U));
		assert(delivery_media && body_copies == 1 && private_scrubs == 2);
		assert(mailbox->completion == 0 && mailbox->operation == 9 &&
			mailbox->result_data_size == 96);
		const uint8_t *snapshot = (const uint8_t *)mailbox + 4240;

		assert(snapshot[0] == 1 && snapshot[4] == 96);
		assert(munmap(mailbox, 65536U) == 0);
		return 0;
	}
#if !defined(TEST_REAL_RUNTIME_WAVE)
	*mailbox = (struct payload_mm_authvar_service_frame) {
		.revision = 3, .header_size = 144, .operation = UINT32_MAX,
		.generation = 9, .request_id = 1, .status = UINT64_MAX, .completion = UINT32_MAX,
	};
	runtime_admitted = false;
	assert(payload_mm_authvar_service_execute() == CB_ERR && !program_count);
	assert(!private_scrubs && !body_copies);
	runtime_admitted = true;
	assert(payload_mm_authvar_service_execute() == CB_ERR_ARG && !program_count);
	assert(mailbox->operation == UINT32_MAX && mailbox->completion == UINT32_MAX);
	assert(private_scrubs == 2 && !body_copies);
	private_scrubs = 0;
	memset(mailbox, 0, 65536U);
	*mailbox = (struct payload_mm_authvar_service_frame) {
		.revision = 3, .header_size = 144, .operation = 4,
		.generation = 9, .request_id = 1, .attributes = 7,
		.status = UINT64_MAX, .completion = UINT32_MAX,
	};
	assert(payload_mm_authvar_service_request_validate(&descriptor, mailbox, 65536U) == CB_SUCCESS);
	mailbox->revision = 2;
	assert(payload_mm_authvar_service_request_validate(&descriptor, mailbox, 65536U) == CB_ERR);
	assert(payload_mm_authvar_service_execute() == CB_ERR_ARG && !program_count);
	assert(mailbox->revision == 2 && mailbox->operation == 4 &&
		mailbox->completion == UINT32_MAX && mailbox->status == UINT64_MAX);
	assert(private_scrubs == 2 && !body_copies);
#endif
	const uint32_t operations[] = { 4, 1, 2, 3, 7, 8, 5, 6 };
	const uint8_t ordinary_name[] = { 'N', 0, 0, 0 };
	const size_t data_offset = 144U + descriptor.maximum_name_size;

	if (capacity_edge && CONFIG_SMMSTORE_BLOCK_SIZE == 4096U) {
		const uint32_t name_sizes[] = { 4, descriptor.maximum_name_size };

		/* Complete first-use FTW recovery before measuring rejected SET writes. */
		private_scrubs = body_copies = 0;
		expected_reply = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		memset(mailbox, 0, 65536U);
		*mailbox = (struct payload_mm_authvar_service_frame) {
			.revision = 3, .header_size = 144, .operation = 4,
			.generation = 9, .request_id = 99,
			.status = UINT64_MAX, .completion = UINT32_MAX, .attributes = 7,
		};
		assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
		assert(mailbox->status == expected_reply &&
			mailbox->completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
		assert(private_scrubs == 2 && body_copies == 1);
		for (size_t index = 0; index < ARRAY_SIZE(name_sizes); index++) {
			const unsigned int programs_before = program_count;
			uint8_t *name = (uint8_t *)mailbox + 144;

			private_scrubs = body_copies = 0;
			expected_reply = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
			memset(mailbox, 0, 65536U);
			*mailbox = (struct payload_mm_authvar_service_frame) {
				.revision = 3, .header_size = 144, .operation = 3,
				.generation = 9, .request_id = index + 100,
				.status = UINT64_MAX, .completion = UINT32_MAX,
				.attributes = 7, .name_size = name_sizes[index],
				.data_size = descriptor.maximum_data_size,
			};
			memcpy(mailbox->vendor_guid, vendor_keys.vendor_guid, 16);
			for (size_t offset = 0; offset + 2 < name_sizes[index]; offset += 2)
				name[offset] = 'Z';
			memset((uint8_t *)mailbox + data_offset, 0x5a, descriptor.maximum_data_size);
			assert(payload_mm_authvar_service_request_validate(&descriptor, mailbox,
				65536U) == CB_SUCCESS);
			assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
			assert(mailbox->completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
			assert(mailbox->status == expected_reply && program_count == programs_before);
			assert(private_scrubs == 2 && body_copies == 1);
		}
	}

	for (size_t index = 0; index < ARRAY_SIZE(operations); index++) {
		const unsigned int programs_before = program_count;

		private_scrubs = body_copies = 0;
		expected_reply = wrong_content && operations[index] == 3 ?
			PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION : PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		memset(mailbox, 0, 65536U);
		*mailbox = (struct payload_mm_authvar_service_frame) {
			.revision = 3, .header_size = 144, .operation = operations[index],
			.generation = 9, .request_id = index + 1,
			.status = UINT64_MAX, .completion = UINT32_MAX,
		};
		switch (operations[index]) {
		case 4:
			mailbox->attributes = 7;
			break;
		case 1:
			memcpy(mailbox->vendor_guid, vendor_keys.vendor_guid, 16);
			mailbox->name_size = sizeof(vendor_keys_name);
			mailbox->data_capacity = 1;
			memcpy((uint8_t *)mailbox + 144, vendor_keys_name, sizeof(vendor_keys_name));
			break;
		case 2:
			mailbox->name_capacity = descriptor.maximum_name_size;
			break;
		case 3:
			memcpy(mailbox->vendor_guid, vendor_keys.vendor_guid, 16);
			mailbox->name_size = sizeof(ordinary_name);
			mailbox->attributes = 7;
			mailbox->data_size = 1;
			memcpy((uint8_t *)mailbox + 144, ordinary_name, sizeof(ordinary_name));
			*((uint8_t *)mailbox + data_offset) = 0x5a;
			if (authenticated_size) {
				const uint8_t private_guid[] = {
					0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
					0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
				};
				const uint8_t private_name[] = {
					'P', 0, 'r', 0, 'i', 0, 'v', 0, 0, 0,
				};

				memcpy(mailbox->vendor_guid, private_guid, sizeof(private_guid));
				mailbox->name_size = sizeof(private_name);
				mailbox->attributes = 0x23;
				mailbox->data_size = authenticated_size;
				memset((uint8_t *)mailbox + 144, 0, descriptor.maximum_name_size);
				memcpy((uint8_t *)mailbox + 144, private_name, sizeof(private_name));
				memcpy((uint8_t *)mailbox + data_offset, authenticated_data,
					authenticated_size);
			}
			break;
		case 7: {
			/* Valid literal policy v1 header covering every key in one namespace. */
			uint8_t *policy = (uint8_t *)mailbox + data_offset;

			mailbox->data_size = 44;
			policy[2] = 1;
			policy[4] = policy[6] = 44;
			memcpy(policy + 8, vendor_keys.vendor_guid, 16);
			policy[28] = 1;
			break;
		}
		default:
			break;
		}
		assert(payload_mm_authvar_service_request_validate(&descriptor, mailbox, 65536U) == CB_SUCCESS);
		mutate_mailbox = !strcmp(argv[1], "mailbox-drift");
		recursive_execute = index == 0;
		if (proof_drift && operations[index] == 3) {
			program_baseline = program_count;
			deny_after_program = true;
		}
		assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
		assert(mailbox->completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
		assert(mailbox->status == expected_reply);
		assert(mailbox->request_id == index + 1 && mailbox->operation == operations[index]);
		assert(private_scrubs == 2 && body_copies == 1);
		if (authenticated_set && operations[index] == 3) {
			if (wrong_content)
				assert(program_count == programs_before);
			else
				assert_authenticated_commit();
		}
	}
	assert(!proof_drift && (program_count || wrong_content));
#if !defined(TEST_REAL_RUNTIME_WAVE)
	const uint32_t completed_programs = program_count;
	const unsigned int completed_copies = body_copies, completed_scrubs = private_scrubs;

	memcpy(delivery_original, mailbox, sizeof(delivery_original));
	assert(payload_mm_authvar_service_execute() == CB_ERR_ARG);
	assert(program_count == completed_programs && body_copies == completed_copies &&
		private_scrubs == completed_scrubs + 2);
	assert(!memcmp(delivery_original, mailbox, sizeof(delivery_original)));
#endif
	assert(munmap(mailbox, 65536U) == 0);
	return 0;
}
