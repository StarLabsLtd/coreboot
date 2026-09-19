/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_update.h>
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

int main(int argc, char **argv)
{
	valid_fixture();
	header_rejections();
	region_rejections();
	bounded_backend_contract();
	if (argc == 2 && !strcmp(argv[1], "--fixture")) {
		reset_fixture();
		return write(STDOUT_FILENO, blob, bytes) == (ssize_t)bytes ? 0 : 1;
	}
	return 0;
}
