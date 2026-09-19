/* SPDX-License-Identifier: GPL-2.0-only */

#include <coreboot_tables.h>
#include <string.h>
#include <unistd.h>

static struct cbuint64 packed(UINT64 value)
{
	return (struct cbuint64) { (UINT32)value, (UINT32)(value >> 32) };
}

int main(void)
{
	static const UINT8 image_type[16] = {
		0xe6, 0xd0, 0x5c, 0x97, 0x40, 0xc5, 0x2b, 0x4e,
		0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d,
	};
	struct {
		struct cb_capsule_handoff header;
		struct cb_capsule_update_region region;
	} fixture = {
		.header = {
			.tag = CB_TAG_CAPSULE_HANDOFF,
			.size = sizeof(fixture),
			.revision = CB_CAPSULE_HANDOFF_REVISION,
			.header_size = sizeof(fixture.header),
			.flags = CB_CAPSULE_HANDOFF_REQUIRED_FLAGS,
			.broker_type = CB_CAPSULE_BROKER_COREBOOT_UPDATE,
			.capsule_format = CB_CAPSULE_FORMAT_FMP_V3,
			.authentication_format = CB_CAPSULE_AUTH_EFI_PKCS7,
			.board_binding_format =
				CB_CAPSULE_BOARD_BINDING_CBFS_BUILD_INFO_V1,
			.payload_format = CB_CAPSULE_PAYLOAD_MSS1_V1,
			.broker_capabilities =
				CB_CAPSULE_BROKER_REQUIRED_CAPABILITIES,
			.version = 10,
			.lowest_supported_version = 8,
			.capsule_flags = CB_CAPSULE_FLAGS_PERSIST_RESET,
			.block_size = 0x1000,
			.erase_size = 0x1000,
			.region_count = 1,
			.image_size = { 0x01000000, 0 },
			.boot_media_size = { 0x01000000, 0 },
			.smmstore_offset = { 0x00f00000, 0 },
			.smmstore_size = { 0x00010000, 0 },
		},
		.region = {
			.image_offset = { 0x00800000, 0 },
			.flash_offset = { 0x00800000, 0 },
			.size = { 0x00700000, 0 },
			.flags = CB_CAPSULE_REGION_BIOS,
		},
	};

	/* Keep the helper type-checked even though this fixture uses constants. */
	fixture.header.image_size = packed(0x01000000);
	memcpy(fixture.header.image_type_guid, image_type, sizeof(image_type));
	return write(STDOUT_FILENO, &fixture, sizeof(fixture)) ==
		(ssize_t)sizeof(fixture) ? 0 : 1;
}
