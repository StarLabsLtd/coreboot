/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_update.h>
#include "../../src/lib/capsule_update_internal.h"
#include <string.h>
#include <unistd.h>

#define IMAGE_SIZE 0x01000000U

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static uint8_t blob[sizeof(struct lb_capsule_handoff) +
	2 * sizeof(struct lb_capsule_update_region)];
static struct lb_efi_fw_info firmware;
static size_t bytes;

static void reset_fixture(void)
{
	static const uint8_t image_type[16] = {
		0xe6, 0xd0, 0x5c, 0x97, 0x40, 0xc5, 0x2b, 0x4e,
		0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d,
	};
	struct lb_capsule_handoff *handoff = (void *)blob;

	memset(blob, 0, sizeof(blob));
	*handoff = (struct lb_capsule_handoff) {
		.tag = LB_TAG_CAPSULE_HANDOFF,
		.size = sizeof(*handoff) + sizeof(handoff->regions[0]),
		.revision = LB_CAPSULE_HANDOFF_REVISION,
		.header_size = sizeof(*handoff),
		.flags = LB_CAPSULE_HANDOFF_REQUIRED_FLAGS,
		.broker_type = LB_CAPSULE_BROKER_COREBOOT_UPDATE,
		.capsule_format = LB_CAPSULE_FORMAT_FMP_V3,
		.authentication_format = LB_CAPSULE_AUTH_EFI_PKCS7,
		.board_binding_format =
			LB_CAPSULE_BOARD_BINDING_CBFS_BUILD_INFO_V1,
		.payload_format = LB_CAPSULE_PAYLOAD_MSS1_V1,
		.broker_capabilities = LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES,
		.version = 10,
		.lowest_supported_version = 8,
		.capsule_flags = LB_CAPSULE_FLAGS_PERSIST_RESET,
		.block_size = 0x1000,
		.erase_size = 0x1000,
		.region_count = 1,
		.image_size = IMAGE_SIZE,
		.boot_media_size = IMAGE_SIZE,
		.smmstore_offset = 0x00f00000,
		.smmstore_size = 0x00010000,
	};
	memcpy(handoff->image_type_guid, image_type, sizeof(image_type));
	handoff->regions[0] = (struct lb_capsule_update_region) {
		.image_offset = 0x00800000,
		.flash_offset = 0x00800000,
		.size = 0x00700000,
		.flags = LB_CAPSULE_REGION_BIOS,
	};
	firmware = (struct lb_efi_fw_info) {
		.tag = LB_TAG_EFI_FW_INFO,
		.size = sizeof(firmware),
		.version = 10,
		.lowest_supported_version = 8,
		.fw_size = IMAGE_SIZE,
	};
	memcpy(firmware.guid, image_type, sizeof(image_type));
	bytes = handoff->size;
}

static void valid_fixture(void)
{
	const struct lb_capsule_handoff *handoff;

	reset_fixture();
	handoff = (const void *)blob;
	assert(capsule_handoff_validate(handoff, bytes, &firmware) == CB_SUCCESS);
	assert(handoff->size == 144 && handoff->region_count == 1);
	assert(handoff->regions[0].flash_offset == 0x00800000);
}

static void header_rejections(void)
{
	struct lb_capsule_handoff *handoff;

#define REJECT(member, value) do { \
	reset_fixture(); \
	handoff = (void *)blob; \
	handoff->member = (value); \
	assert(capsule_handoff_validate(handoff, bytes, &firmware) == CB_ERR); \
} while (0)
	REJECT(tag, 0);
	REJECT(size, 112);
	REJECT(revision, 1);
	REJECT(header_size, 108);
	REJECT(flags, LB_CAPSULE_HANDOFF_REQUIRED_FLAGS | (1U << 31));
	REJECT(broker_type, 0);
	REJECT(capsule_format, 2);
	REJECT(authentication_format, 0);
	REJECT(board_binding_format, 0);
	REJECT(payload_format, 0);
	REJECT(broker_capabilities, LB_CAPSULE_BROKER_APPLY_REGIONS);
	REJECT(capsule_flags, 0);
	REJECT(reserved16, 1);
	REJECT(region_count, 0);
	REJECT(region_count, CAPSULE_UPDATE_MAX_REGIONS + 1);
	REJECT(block_size, 0);
	REJECT(erase_size, 0x1800);
#undef REJECT
	reset_fixture();
	handoff = (void *)blob;
	handoff->reserved32[0] = 1;
	assert(capsule_handoff_validate(handoff, bytes, &firmware) == CB_ERR);
	reset_fixture();
	assert(capsule_handoff_validate((const void *)blob, bytes - 1,
		&firmware) == CB_ERR);
	reset_fixture();
	firmware.guid[0] ^= 1;
	assert(capsule_handoff_validate((const void *)blob, bytes,
		&firmware) == CB_ERR);
}

static void region_rejections(void)
{
	struct lb_capsule_handoff *handoff;
	struct lb_capsule_update_region *region;

#define REJECT_REGION(member, value) do { \
	reset_fixture(); \
	handoff = (void *)blob; \
	region = &handoff->regions[0]; \
	region->member = (value); \
	assert(capsule_handoff_validate(handoff, bytes, &firmware) == CB_ERR); \
} while (0)
	REJECT_REGION(size, 0);
	REJECT_REGION(image_offset, IMAGE_SIZE - 0x1000);
	REJECT_REGION(flash_offset, IMAGE_SIZE - 0x1000);
	REJECT_REGION(image_offset, 0x00800001);
	REJECT_REGION(flash_offset, 0x00800001);
	REJECT_REGION(size, 0x006ff001);
	REJECT_REGION(flags, 0);
	REJECT_REGION(flags, 1U << 31);
	REJECT_REGION(reserved, 1);
	REJECT_REGION(flash_offset, 0x00ef0000);
#undef REJECT_REGION
	reset_fixture();
	handoff = (void *)blob;
	handoff->size += sizeof(handoff->regions[1]);
	handoff->region_count = 2;
	bytes = handoff->size;
	handoff->regions[0].size = 0x00100000;
	handoff->regions[1] = (struct lb_capsule_update_region) {
		.image_offset = 0x00880000,
		.flash_offset = 0x00a00000,
		.size = 0x00100000,
	};
	assert(capsule_handoff_validate(handoff, bytes, &firmware) == CB_ERR);
	reset_fixture();
	handoff = (void *)blob;
	handoff->size += sizeof(handoff->regions[1]);
	handoff->region_count = 2;
	bytes = handoff->size;
	handoff->regions[0].size = 0x00100000;
	handoff->regions[1] = (struct lb_capsule_update_region) {
		.image_offset = 0x00a00000,
		.flash_offset = 0x00880000,
		.size = 0x00100000,
	};
	assert(capsule_handoff_validate(handoff, bytes, &firmware) == CB_ERR);
}

static enum cb_err backend_contract(const struct capsule_update_plan *plan)
{
	(void)plan;
	return CB_SUCCESS;
}

static void bounded_backend_contract(void)
{
	struct capsule_update_backend backend = {
		.capabilities = LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES,
		.apply_regions = backend_contract,
	};

	assert(backend.capabilities == (LB_CAPSULE_BROKER_APPLY_REGIONS |
		LB_CAPSULE_BROKER_PRESERVE_UNLISTED |
		LB_CAPSULE_BROKER_VERIFY_READBACK));
	assert(backend.apply_regions == backend_contract);
}

#define TEST_MEDIA_SIZE 0x10000U
struct media_fixture {
	u8 bytes[TEST_MEDIA_SIZE];
	struct lb_capsule_update_region plan_region;
	struct lb_capsule_update_region policy_region;
	size_t reads;
	size_t erases;
	size_t writes;
	bool corrupt_readback;
};

static enum cb_err media_read(void *context, u64 offset, void *buffer,
			      size_t size)
{
	struct media_fixture *media = context;

	media->reads++;
	memcpy(buffer, &media->bytes[offset], size);
	if (media->corrupt_readback)
		((u8 *)buffer)[0] ^= 1;
	return CB_SUCCESS;
}

static enum cb_err media_erase(void *context, u64 offset, size_t size)
{
	struct media_fixture *media = context;

	media->erases++;
	memset(&media->bytes[offset], 0xff, size);
	return CB_SUCCESS;
}

static enum cb_err media_write(void *context, u64 offset,
			       const void *buffer, size_t size)
{
	struct media_fixture *media = context;

	media->writes++;
	memcpy(&media->bytes[offset], buffer, size);
	return CB_SUCCESS;
}

static void small_fixture(struct capsule_update_plan *plan,
			  struct capsule_media_policy *policy,
			  struct capsule_media_backend *media,
			  struct media_fixture *fixture,
			  u8 image[TEST_MEDIA_SIZE])
{
	const struct lb_capsule_update_region region = {
		.image_offset = 0x2000,
		.flash_offset = 0x4000,
		.size = 0x2000,
		.flags = LB_CAPSULE_REGION_BIOS,
	};

	memset(fixture, 0, sizeof(*fixture));
	memset(fixture->bytes, 0x5a, sizeof(fixture->bytes));
	fixture->plan_region = region;
	fixture->policy_region = region;
	for (size_t i = 0; i < TEST_MEDIA_SIZE; i++)
		image[i] = (u8)i;
	*plan = (struct capsule_update_plan) {
		.image = image,
		.image_bytes = TEST_MEDIA_SIZE,
		.regions = &fixture->plan_region,
		.region_count = 1,
	};
	*policy = (struct capsule_media_policy) {
		.media_size = TEST_MEDIA_SIZE,
		.erase_size = 0x1000,
		.smmstore_offset = 0xe000,
		.smmstore_size = 0x1000,
		.regions = &fixture->policy_region,
		.region_count = 1,
	};
	*media = (struct capsule_media_backend) {
		.context = fixture,
		.size = TEST_MEDIA_SIZE,
		.erase_size = 0x1000,
		.read = media_read,
		.erase = media_erase,
		.write = media_write,
	};
}

static void verified_apply_contract(void)
{
	struct capsule_update_plan plan;
	struct capsule_media_policy policy;
	struct capsule_media_backend media;
	struct media_fixture fixture;
	u8 image[TEST_MEDIA_SIZE];
	u8 before[TEST_MEDIA_SIZE];
	u8 scratch[0x1000];

	small_fixture(&plan, &policy, &media, &fixture, image);
	memcpy(before, fixture.bytes, sizeof(before));
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch)) == CB_SUCCESS);
	assert(fixture.erases == 2 && fixture.writes == 2 && fixture.reads == 2);
	assert(!memcmp(&fixture.bytes[0x4000], &image[0x2000], 0x2000));
	assert(!memcmp(fixture.bytes, before, 0x4000));
	assert(!memcmp(&fixture.bytes[0x6000], &before[0x6000],
		       TEST_MEDIA_SIZE - 0x6000));

	small_fixture(&plan, &policy, &media, &fixture, image);
	media.size--;
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch)) == CB_ERR);
	assert(fixture.erases == 0 && fixture.writes == 0 && fixture.reads == 0);
	small_fixture(&plan, &policy, &media, &fixture, image);
	media.erase_size <<= 1;
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch)) == CB_ERR);
	assert(fixture.erases == 0 && fixture.writes == 0 && fixture.reads == 0);
	small_fixture(&plan, &policy, &media, &fixture, image);
	plan.image_bytes = 0x3fff;
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch)) == CB_ERR);
	assert(fixture.erases == 0 && fixture.writes == 0 && fixture.reads == 0);
	small_fixture(&plan, &policy, &media, &fixture, image);
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch) - 1) == CB_ERR);
	assert(fixture.erases == 0 && fixture.writes == 0 && fixture.reads == 0);
	small_fixture(&plan, &policy, &media, &fixture, image);
	media.write = NULL;
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch)) == CB_ERR);
	assert(fixture.erases == 0 && fixture.writes == 0 && fixture.reads == 0);
	small_fixture(&plan, &policy, &media, &fixture, image);
	fixture.plan_region.flash_offset++;
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch)) == CB_ERR);
	assert(fixture.erases == 0 && fixture.writes == 0 && fixture.reads == 0);
	small_fixture(&plan, &policy, &media, &fixture, image);
	fixture.plan_region.flash_offset = 0xe000;
	fixture.policy_region.flash_offset = 0xe000;
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch)) == CB_ERR);
	assert(fixture.erases == 0 && fixture.writes == 0 && fixture.reads == 0);
	small_fixture(&plan, &policy, &media, &fixture, image);
	fixture.plan_region.flags = 0;
	fixture.policy_region.flags = 0;
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch)) == CB_ERR);
	assert(fixture.erases == 0 && fixture.writes == 0 && fixture.reads == 0);
	small_fixture(&plan, &policy, &media, &fixture, image);
	fixture.corrupt_readback = true;
	assert(capsule_apply_policy_verified(&plan, &policy, &media, scratch,
					     sizeof(scratch)) == CB_ERR);
	assert(fixture.erases == 1 && fixture.writes == 1 && fixture.reads == 1);
}

int main(int argc, char **argv)
{
	valid_fixture();
	header_rejections();
	region_rejections();
	bounded_backend_contract();
	verified_apply_contract();
	if (argc == 2 && !strcmp(argv[1], "--fixture")) {
		reset_fixture();
		return write(STDOUT_FILENO, blob, bytes) == (ssize_t)bytes ? 0 : 1;
	}
	return 0;
}
