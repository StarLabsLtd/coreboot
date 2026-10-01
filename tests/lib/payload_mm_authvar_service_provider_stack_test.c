/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_record.h>
#include <commonlib/helpers.h>
#include <commonlib/payload_mm_authvar_fv.h>
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

static uint8_t flash_bytes[3U * 65536U];
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

void *__real_memcpy(void *destination, const void *source, size_t size);

void *__wrap_memcpy(void *destination, const void *source, size_t size)
{
	void *result;

	if (destination == shared_mailbox && size == 65536U) {
		const struct payload_mm_authvar_service_frame *frame = source;

		assert(shared_mailbox->completion == UINT32_MAX);
		assert(frame->completion == UINT32_MAX && frame->status == 0);
		body_copies++;
	}
	result = __real_memcpy(destination, source, size);
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
	assert(deny_after_program && program_count > program_baseline);
	assert(shared_mailbox->request_id == 4);
	assert(shared_mailbox->completion == UINT32_MAX);
	assert(shared_mailbox->status == UINT64_MAX);
	_exit(77);
}

static enum payload_mm_authvar_media_result begin(const void *context, uint64_t *generation)
{
	(void)context;
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

int main(int argc, char **argv)
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
	bool proof_drift;

	assert(argc == 2);
	proof_drift = !strcmp(argv[1], "proof-drift");
	assert(proof_drift || !strcmp(argv[1], "normal") || !strcmp(argv[1], "mailbox-drift"));

	runtime_smram_size = UINTPTR_MAX - 0x400000U;
	initialize();
	canonical.generation = 9;
	canonical.capability[0] = 1;
	make_service_receipt();
	mailbox = mmap((void *)0x100000U, 65536U, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	assert(mailbox == (void *)0x100000U);
	shared_mailbox = mailbox;
	assert(payload_mm_authvar_fv_format(flash_bytes, sizeof(flash_bytes), 65536U));
	assert(payload_mm_authvar_record_encode(&vendor_keys, &value, 1,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO, flash_bytes + 100,
		sizeof(flash_bytes) - 100, &encoded_size));
	flash_bytes[102] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	memset(&seed.seal_channel, 0, sizeof(seed.seal_channel));
	assert(payload_mm_authvar_service_prepare(&service_verifier, &service_receipt) == CB_SUCCESS);
	assert(payload_mm_authvar_smm_service_bootstrap_install(&seed) == CB_SUCCESS);
	assert(payload_mm_authvar_service_finalize() == CB_SUCCESS);
	assert(payload_mm_authvar_service_descriptor_copy(&descriptor) == CB_SUCCESS);
	assert(descriptor.communication_base == 0x100000U && descriptor.communication_size == 65536U);
	assert(descriptor.trigger_address == APM_CNT && descriptor.trigger_value == 0xfcU);
	scrub_guard = true;
	*mailbox = (struct payload_mm_authvar_service_frame) {
		.revision = 2, .header_size = 144, .operation = UINT32_MAX,
		.generation = 9, .request_id = 1, .status = UINT64_MAX, .completion = UINT32_MAX,
	};
	assert(payload_mm_authvar_service_execute() == CB_ERR && !program_count);
	assert(mailbox->operation == UINT32_MAX && mailbox->completion == UINT32_MAX);
	assert(private_scrubs == 2 && !body_copies);
	const uint32_t operations[] = { 4, 1, 2, 3, 7, 8, 5, 6 };
	const uint8_t ordinary_name[] = { 'N', 0, 0, 0 };
	const size_t data_offset = 144U + descriptor.maximum_name_size;

	for (size_t index = 0; index < ARRAY_SIZE(operations); index++) {
		private_scrubs = body_copies = 0;
		memset(mailbox, 0, 65536U);
		*mailbox = (struct payload_mm_authvar_service_frame) {
			.revision = 2, .header_size = 144, .operation = operations[index],
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
		assert(mailbox->status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(mailbox->request_id == index + 1 && mailbox->operation == operations[index]);
		assert(private_scrubs == 2 && body_copies == 1);
	}
	assert(!proof_drift && program_count);
	assert(munmap(mailbox, 65536U) == 0);
	return 0;
}
