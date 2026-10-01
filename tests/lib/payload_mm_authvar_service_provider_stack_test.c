/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_certdb.h>
#include <boot/payload_mm_authvar_record.h>
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
		if (frame->status != expected_reply)
			dprintf(2, "provider reply: expected %llx, observed %llx\n",
				(unsigned long long)expected_reply, (unsigned long long)frame->status);
		assert(frame->completion == UINT32_MAX && frame->status == expected_reply);
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
	if (authenticated_set)
		assert_authenticated_commit();
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

	assert(argc == 2 || argc == 4 || argc == 5);
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
		!strcmp(argv[1], "mailbox-drift"));

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
	assert(payload_mm_authvar_service_finalize() == CB_SUCCESS);
	assert(payload_mm_authvar_service_descriptor_copy(&descriptor) == CB_SUCCESS);
	assert(descriptor.revision == 4);
	assert(descriptor.communication_base == 0x100000U && descriptor.communication_size == 65536U);
	assert(descriptor.trigger_address == APM_CNT && descriptor.trigger_value == 0xfcU);
	assert(descriptor.maximum_name_size == 4096U);
	assert(descriptor.maximum_data_size ==
		(CONFIG_SMMSTORE_BLOCK_SIZE == 4096U ? 8132U : 61296U));
	scrub_guard = true;
#if !defined(TEST_REAL_RUNTIME_WAVE)
	*mailbox = (struct payload_mm_authvar_service_frame) {
		.revision = 3, .header_size = 144, .operation = UINT32_MAX,
		.generation = 9, .request_id = 1, .status = UINT64_MAX, .completion = UINT32_MAX,
	};
	assert(payload_mm_authvar_service_execute() == CB_ERR && !program_count);
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
	assert(payload_mm_authvar_service_execute() == CB_ERR && !program_count);
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
	assert(munmap(mailbox, 65536U) == 0);
	return 0;
}
