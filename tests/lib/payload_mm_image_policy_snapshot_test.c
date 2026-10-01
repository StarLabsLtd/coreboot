/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_record.h>
#include <boot/payload_mm_image_policy_snapshot.h>
#include <commonlib/payload_mm_authvar_fv.h>

/*
 * Actual bootstrap, index, media, executor, coordinator and fixed transaction.
 * SPI register access and BOOT/runtime protected placement remain host models.
 */
#define TEST_REAL_BOOTSTRAP_STACK
#include "payload_mm_authvar_service_bootstrap_test.c"

#undef assert
#define assert(condition) do { if (!(condition)) { \
	dprintf(2, "snapshot fixture line %d: %s\n", __LINE__, #condition); abort(); \
} } while (0)

static uint8_t flash_bytes[3U * CONFIG_SMMSTORE_BLOCK_SIZE];
static struct payload_mm_authvar_service_frame *mailbox;
static uint64_t service_generation;
static unsigned int begin_count;
static unsigned int end_count;
static unsigned int baseline_begin;
static unsigned int baseline_end;
static bool fail_end;
static bool drift_at_end;
static bool live_proof = true;

bool platform_payload_mm_authvar_service_finalize_admitted(void)
{
	return wave_admitted;
}

bool platform_payload_mm_authvar_service_runtime_admitted(void)
{
	return live_proof;
}

void test_real_scrub_observe(const void *buffer, size_t size)
{
	(void)buffer;
	(void)size;
}

void __noreturn test_real_fail_stop(void)
{
	assert((drift_at_end || fail_end) && begin_count == baseline_begin + 1 &&
		end_count == baseline_end + 1);
	assert(mailbox->status == UINT64_MAX && mailbox->completion == UINT32_MAX);
	for (size_t offset = 144; offset < 65536; offset++)
		assert(((const uint8_t *)mailbox)[offset] == 0);
	_exit(77);
}

static enum payload_mm_authvar_media_result media_begin(const void *context,
	uint64_t *generation)
{
	(void)context;
	begin_count++;
	*generation = 1;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result media_read(const void *context, uint32_t offset,
	void *buffer, size_t size, size_t *completed)
{
	(void)context;
	assert(offset <= sizeof(flash_bytes) && size <= sizeof(flash_bytes) - offset);
	memcpy(buffer, flash_bytes + offset, size);
	*completed = size;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result media_program(const void *context, uint32_t offset,
	const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;

	(void)context;
	assert(offset <= sizeof(flash_bytes) && size <= sizeof(flash_bytes) - offset);
	for (size_t index = 0; index < size; index++)
		flash_bytes[offset + index] &= bytes[index];
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result media_erase(const void *context, uint32_t offset,
	size_t size)
{
	(void)context;
	assert(offset <= sizeof(flash_bytes) && size <= sizeof(flash_bytes) - offset);
	memset(flash_bytes + offset, 0xff, size);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result media_sync(const void *context)
{
	(void)context;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result media_end(const void *context)
{
	(void)context;
	end_count++;
	if (drift_at_end)
		live_proof = false;
	return fail_end ? PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR : PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum cb_err test_real_media_install(void)
{
	const struct payload_mm_authvar_media_port port = {
		.revision = PAYLOAD_MM_AUTHVAR_MEDIA_PORT_REVISION, .size = sizeof(port),
		.begin = media_begin, .read = media_read, .program = media_program,
		.erase = media_erase, .sync = media_sync, .end = media_end,
	};

	return payload_mm_authvar_media_install(&port);
}

static uint32_t append_record(uint32_t offset, const uint8_t guid[16],
	const void *name, size_t name_size, const void *data, size_t size, uint32_t attributes)
{
	struct payload_mm_authvar_record_descriptor record = {
		.name = name, .name_size = name_size, .attributes = attributes,
	};
	const struct payload_mm_authvar_record_span value = { data, size };
	uint32_t encoded;

	memcpy(record.vendor_guid, guid, sizeof(record.vendor_guid));
	assert(payload_mm_authvar_record_encode(&record, &value, 1,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO, flash_bytes + offset,
		CONFIG_SMMSTORE_BLOCK_SIZE - offset, &encoded));
	flash_bytes[offset + 2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	return ALIGN_UP(offset + encoded, 4U);
}

static void request_init(uint32_t operation, uint32_t capacity)
{
	memset(mailbox, 0, 65536);
	*mailbox = (struct payload_mm_authvar_service_frame) {
		.revision = 3, .header_size = 144, .operation = operation,
		.generation = service_generation, .request_id = 1, .data_capacity = capacity,
		.status = UINT64_MAX, .completion = UINT32_MAX,
	};
}

static uint32_t get32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8U |
		(uint32_t)bytes[2] << 16U | (uint32_t)bytes[3] << 24U;
}

int main(int argc, char **argv)
{
	static const uint8_t vendor_guid[16] = {
		0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
		0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
	};
	static const uint8_t global_guid[16] = {
		0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
		0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
	};
	static const uint8_t image_guid[16] = {
		0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
		0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
	};
	static const uint8_t enable_guid[16] = {
		0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
		0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44,
	};
	static const uint8_t vendor_name[] = {
		'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
		'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
	};
	static const uint8_t enable_name[] = {
		'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0,
		'o', 0, 'o', 0, 't', 0, 'E', 0, 'n', 0, 'a', 0, 'b', 0,
		'l', 0, 'e', 0, 0, 0,
	};
	static const uint8_t names[4][8] = {
		{ 'P', 0, 'K', 0, 0, 0 }, { 'd', 0, 'b', 0, 0, 0 },
		{ 'd', 0, 'b', 0, 'x', 0, 0, 0 }, { 'd', 0, 'b', 0, 't', 0, 0, 0 },
	};
	const uint8_t one = 1;
	struct lb_authvar_service_endpoint descriptor;
	struct payload_mm_authvar_fv_geometry geometry;
	uint8_t value[21000];
	uint8_t saved_request[65536] __aligned(8);
	uint32_t offset = 100;
	uint32_t required = 96;
	size_t value_size = 0;
	bool user;
	bool oversized;

	assert(argc == 2);
	user = !strcmp(argv[1], "user") || !strcmp(argv[1], "capacity");
	oversized = !strcmp(argv[1], "oversized");
	assert(user || oversized || !strcmp(argv[1], "setup") ||
		!strcmp(argv[1], "runtime") || !strcmp(argv[1], "end-error") ||
		!strcmp(argv[1], "proof-drift") || !strcmp(argv[1], "wrong-namespace"));
	runtime_smram_size = UINTPTR_MAX - 0x400000U;
	initialize();
	store.region.size = sizeof(flash_bytes);
	canonical.generation = 9;
	canonical.capability[0] = 1;
	make_service_receipt();
	mailbox = mmap((void *)0x100000U, 65536, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	assert(mailbox == (void *)0x100000U);
	assert(payload_mm_authvar_fv_format(flash_bytes, sizeof(flash_bytes),
		CONFIG_SMMSTORE_BLOCK_SIZE));
	assert(payload_mm_authvar_fv_geometry(&geometry, sizeof(flash_bytes),
		CONFIG_SMMSTORE_BLOCK_SIZE));
	assert(geometry.variable_size == CONFIG_SMMSTORE_BLOCK_SIZE);
	offset = append_record(offset, vendor_guid, vendor_name, sizeof(vendor_name), &one, 1, 0x23);
	if (user)
		offset = append_record(offset, enable_guid, enable_name, sizeof(enable_name), &one, 1, 3);
	memset(value, 0x5a, sizeof(value));
	if (user || oversized || !strcmp(argv[1], "wrong-namespace")) {
		value_size = oversized ? sizeof(value) : 3;
		for (size_t key = user ? 0 : 1; key < 4; key++) {
			const uint8_t *guid = !key || !strcmp(argv[1], "wrong-namespace") ?
				global_guid : image_guid;

			offset = append_record(offset, guid, names[key], key < 2 ? 6 : 8,
				value, value_size, 0x27);
			if (strcmp(argv[1], "wrong-namespace"))
				required += ALIGN_UP((uint32_t)value_size, 8U);
		}
	}
	assert(offset < geometry.variable_size);
	if (oversized)
		assert(required == 63096 && required > 61296 &&
			geometry.variable_size - 72 > required);
	memset(&seed.seal_channel, 0, sizeof(seed.seal_channel));
	assert(payload_mm_authvar_service_prepare(&service_verifier, &service_receipt) == CB_SUCCESS);
	assert(payload_mm_authvar_smm_service_bootstrap_install(&seed) == CB_SUCCESS);
	assert(payload_mm_authvar_service_finalize() == CB_SUCCESS);
	assert(payload_mm_authvar_service_descriptor_copy(&descriptor) == CB_SUCCESS);
	service_generation = descriptor.generation;
	assert(descriptor.revision == 4 && descriptor.maximum_name_size == 4096);
	if (!strcmp(argv[1], "runtime")) {
		request_init(6, 0);
		assert(payload_mm_authvar_service_execute() == CB_SUCCESS && mailbox->status == 0);
	}
	baseline_begin = begin_count;
	baseline_end = end_count;
	fail_end = !strcmp(argv[1], "end-error");
	drift_at_end = !strcmp(argv[1], "proof-drift");
	request_init(9, !strcmp(argv[1], "capacity") ? required - 1 : descriptor.maximum_data_size);
	memcpy(saved_request, mailbox, sizeof(saved_request));
	assert(payload_mm_authvar_service_request_validate(&descriptor, mailbox, 65536) == CB_SUCCESS);
	assert(payload_mm_authvar_service_execute() == CB_SUCCESS);
	assert(payload_mm_authvar_service_response_validate(&descriptor, saved_request, mailbox,
		65536) == CB_SUCCESS);
	if (!strcmp(argv[1], "runtime")) {
		assert(mailbox->status == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED &&
			!mailbox->result_data_size && begin_count == baseline_begin &&
			end_count == baseline_end);
	} else if (fail_end) {
		assert(mailbox->status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!mailbox->result_data_size && begin_count == baseline_begin + 1 &&
			end_count == baseline_end + 1);
	} else {
		assert(begin_count == baseline_begin + 1 && end_count == baseline_end + 1);
		assert(mailbox->result_data_size == required);
		if (!strcmp(argv[1], "capacity") || oversized)
			assert(mailbox->status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
		else {
			const uint8_t *body = (const uint8_t *)mailbox + 4240;

			assert(mailbox->status == 0 && get32(body) == 1 && get32(body + 4) == 96 &&
				get32(body + 8) == required && get32(body + 12) == 4 &&
				get32(body + 16) == (user ? 6U : 5U) && get32(body + 20) == 6);
			assert(payload_mm_image_policy_snapshot_shape_valid(body, required));
		}
	}
	if (mailbox->status)
		for (size_t index = 144; index < 65536; index++)
			assert(((const uint8_t *)mailbox)[index] == 0);
	puts("Single-index image-policy producer, modeled hardware only: PASS");
	return 0;
}
