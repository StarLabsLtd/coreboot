/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <payload_mm_fmp_auth_policy.h>
#include "../../src/lib/capsule_broker_internal.h"
#include "../../src/lib/payload_mm_fmp_owner_internal.h"

#define GENERATION 0x8877665544332211ULL
#define STAGING_SIZE (64U * 1024U)
#define ROM_SIZE 4096U
#define MEDIA_SIZE (32U * 1024U)
#define ERASE_SIZE 4096U

static uint8_t communication[CAPSULE_BROKER_TRANSPORT_SIZE]
	__aligned(8);
static uint8_t staging[STAGING_SIZE] __aligned(8);
static uint8_t media[MEDIA_SIZE];
static uint8_t original[MEDIA_SIZE];
static uint8_t expected_rom[ROM_SIZE];
static uint8_t write_scratch[ERASE_SIZE] __aligned(8);
static uint8_t scratch[ERASE_SIZE] __aligned(8);
static uint8_t trust[PAYLOAD_MM_MAX_TRUST_XDR_SIZE];
static struct payload_mm_fmp_capsule_intent intent;
static const struct payload_mm_fmp_capsule_intent *staged_intent;
static struct payload_mm_fmp_owner_record owner;
static size_t capsule_size;
static size_t trust_size;

static size_t read_file(const char *path, uint8_t *data, size_t capacity)
{
	ssize_t count = 0;
	size_t total = 0;
	int descriptor = open(path, O_RDONLY);

	assert(descriptor >= 0);
	while (total < capacity &&
	       (count = read(descriptor, data + total, capacity - total)) > 0)
		total += count;
	if (total == capacity) {
		uint8_t extra;

		count = read(descriptor, &extra, sizeof(extra));
	}
	assert(count == 0 && close(descriptor) == 0);
	return total;
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

const struct payload_mm_fmp_capsule_intent *
payload_mm_fmp_dispatch_capsule_intent(void)
{
	return staged_intent;
}

bool payload_mm_fmp_owner_ready(void)
{
	return true;
}

bool payload_mm_fmp_owner_storage_overlaps(const void *buffer, size_t size)
{
	(void)buffer;
	(void)size;
	return false;
}

enum cb_err payload_mm_fmp_owner_read(uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	assert(key == PAYLOAD_MM_FMP_STATE_KEY_STATE);
	*record = owner;
	return CB_SUCCESS;
}

bool payload_mm_authvar_buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	if (!left_size || !right_size)
		return false;
	return left_address <= right_address + right_size - 1 &&
		right_address <= left_address + left_size - 1;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	(void)storage;
	return size != 0;
}

static bool range_proof(void *context, uint64_t base, uint64_t size)
{
	(void)context;
	return (base == (uintptr_t)communication && size == sizeof(communication)) ||
		(base == (uintptr_t)staging && size == sizeof(staging));
}

static bool state_proof(void *context)
{
	(void)context;
	return true;
}

static void digest(const void *data, size_t size,
	uint8_t output[CAPSULE_BROKER_DIGEST_SIZE])
{
	const uint8_t *bytes = data;

	memset(output, 0, CAPSULE_BROKER_DIGEST_SIZE);
	for (size_t i = 0; i < size; i++)
		output[i % CAPSULE_BROKER_DIGEST_SIZE] ^= bytes[i] + (uint8_t)i;
}

static enum cb_err sha256(void *context, const void *data, size_t size,
	uint8_t output[CAPSULE_BROKER_DIGEST_SIZE])
{
	(void)context;
	digest(data, size, output);
	return CB_SUCCESS;
}

static enum cb_err media_read(void *context, u64 offset, void *data,
	size_t size)
{
	(void)context;
	memcpy(data, media + offset, size);
	return CB_SUCCESS;
}

static enum cb_err media_erase(void *context, u64 offset, size_t size)
{
	(void)context;
	memset(media + offset, 0xff, size);
	return CB_SUCCESS;
}

static enum cb_err media_write(void *context, u64 offset, const void *data,
	size_t size)
{
	(void)context;
	memcpy(media + offset, data, size);
	return CB_SUCCESS;
}

static enum cb_err media_sync(void *context)
{
	(void)context;
	return CB_SUCCESS;
}

int main(int argc, char **argv)
{
	static const char vendor[] = "Star Labs";
	static const char part[] = "Lite";
	struct payload_mm_fmp_auth_policy auth = {
		.revision = PAYLOAD_MM_FMP_AUTH_POLICY_REVISION,
		.size = sizeof(auth),
		.trusted_lowest_version = 2,
		.image_size = ROM_SIZE,
		.trust_xdr = trust,
		.mainboard_vendor = vendor,
		.mainboard_vendor_size = sizeof(vendor) - 1,
		.mainboard_part = part,
		.mainboard_part_size = sizeof(part) - 1,
	};
	struct capsule_broker_policy broker_policy = {
		.revision = CAPSULE_BROKER_POLICY_REVISION,
		.size = sizeof(broker_policy),
		.endpoint = {
			.tag = LB_TAG_CAPSULE_BROKER_ENDPOINT,
			.size = sizeof(struct lb_capsule_broker_endpoint),
			.revision = LB_CAPSULE_BROKER_ENDPOINT_REVISION,
			.header_size = sizeof(struct lb_capsule_broker_endpoint),
			.flags = LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS,
			.generation = GENERATION,
			.communication_base = (uintptr_t)communication,
			.communication_size = sizeof(communication),
			.message_size = sizeof(communication),
			.staging_base = (uintptr_t)staging,
			.staging_size = sizeof(staging),
			.transport = LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8,
			.trigger_width = 1,
			.trigger_address = 0xb2,
			.trigger_value = 0x91,
		},
		.raw_image_size = ROM_SIZE,
		.boot_media_size = MEDIA_SIZE,
		.smmstore_offset = 6 * ERASE_SIZE,
		.smmstore_size = ERASE_SIZE,
		.erase_size = ERASE_SIZE,
		.region_count = 1,
		.regions = {{
			.image_offset = 0,
			.flash_offset = ERASE_SIZE,
			.size = ROM_SIZE,
			.flags = LB_CAPSULE_REGION_BIOS,
		}},
		.fmap_area_count = 4,
		.fmap_areas = {
			{ .offset = ERASE_SIZE, .size = ROM_SIZE, .name = "FW_MAIN" },
			{ .offset = 2 * ERASE_SIZE, .size = 2 * ERASE_SIZE,
			  .name = "FMP_STATE_A", .flags = FMAP_AREA_PRESERVE },
			{ .offset = 4 * ERASE_SIZE, .size = 2 * ERASE_SIZE,
			  .name = "FMP_STATE_B", .flags = FMAP_AREA_PRESERVE },
			{ .offset = 6 * ERASE_SIZE, .size = ERASE_SIZE,
			  .name = "SMMSTORE", .flags = FMAP_AREA_PRESERVE },
		},
		.owner_layout = {
			.revision = PAYLOAD_MM_FMP_OWNER_LAYOUT_REVISION,
			.size = sizeof(struct fmp_owner_layout),
			.media_size = MEDIA_SIZE,
			.erase_size = ERASE_SIZE,
			.slot_size = ERASE_SIZE,
			.route_count = 1,
			.state = {
				{ .offset = 2 * ERASE_SIZE, .size = 2 * ERASE_SIZE },
				{ .offset = 4 * ERASE_SIZE, .size = 2 * ERASE_SIZE },
			},
			.smmstore = {
				.offset = 6 * ERASE_SIZE,
				.size = ERASE_SIZE,
			},
			.route = {{
				.image_offset = 0,
				.flash_offset = ERASE_SIZE,
				.size = ROM_SIZE,
				.flags = LB_CAPSULE_REGION_BIOS,
			}},
		},
		.media = {
			.size = MEDIA_SIZE,
			.erase_size = ERASE_SIZE,
			.read = media_read,
			.erase = media_erase,
			.write = media_write,
			.sync = media_sync,
		},
		.write_scratch = write_scratch,
		.write_scratch_size = sizeof(write_scratch),
		.scratch = scratch,
		.scratch_size = sizeof(scratch),
		.sha256 = sha256,
		.authenticate = payload_mm_fmp_authenticate_provider,
		.proofs = {
			.communication_reserved = range_proof,
			.staging_reserved = range_proof,
			.dma_protected = range_proof,
			.smm_spi_owned = state_proof,
			.no_raw_flash = state_proof,
			.cpu_rendezvous_active = state_proof,
		},
	};
	struct capsule_broker_success success;

	assert(argc == 4);
	capsule_size = read_file(argv[1], staging, sizeof(staging));
	trust_size = read_file(argv[2], trust, sizeof(trust));
	assert(read_file(argv[3], expected_rom, sizeof(expected_rom)) == ROM_SIZE);
	assert(capsule_size > ROM_SIZE && capsule_size < sizeof(staging));
	auth.trust_xdr_size = trust_size;
	auth.image_type.b[0] = 1;
	owner = (struct payload_mm_fmp_owner_record) {
		.sequence = 1,
		.present = 1,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE,
	};
	owner.data[0] = 1;
	owner.data[1] = 1;
	owner.data[4] = 3;
	owner.data[8] = 2;
	memset(media, 0x5a, sizeof(media));
	memcpy(original, media, sizeof(media));
	assert(payload_mm_fmp_auth_policy_install(&auth, protected_storage, NULL) ==
		CB_SUCCESS);
	assert(capsule_broker_policy_install(&broker_policy, protected_storage,
		NULL) == CB_SUCCESS);
	intent = (struct payload_mm_fmp_capsule_intent) {
		.revision = PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION,
		.size = sizeof(intent),
		.operation = PAYLOAD_MM_FMP_CAPSULE_SET,
		.broker_generation = GENERATION,
		.transaction = 1,
		.capsule_size = capsule_size,
		.digest_algorithm = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256,
		.digest_size = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE,
		.attempted_version = 3,
	};
	digest(staging, capsule_size, intent.digest);
	staged_intent = &intent;
	assert(capsule_broker_authenticate_intent_bound(staged_intent, &owner) ==
		CB_SUCCESS);
	assert(capsule_broker_checkpoint_grant_bound(GENERATION, 1, 3, 1, 2,
		intent.digest) == CB_SUCCESS);
	assert(capsule_broker_apply_intent(staged_intent) == CB_SUCCESS);
	assert(capsule_broker_success_claim_bound(GENERATION, 1, 2,
		intent.digest, &success) == CB_SUCCESS);
	assert(success.version == 3 && success.lowest_supported_version == 2);
	assert(!memcmp(media + ERASE_SIZE, expected_rom, ROM_SIZE));
	assert(!memcmp(media, original, ERASE_SIZE));
	assert(!memcmp(media + 2 * ERASE_SIZE, original + 2 * ERASE_SIZE,
		MEDIA_SIZE - 2 * ERASE_SIZE));
	return 0;
}
