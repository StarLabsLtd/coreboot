/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/capsule_broker.h>
#include <boot/capsule_platform_facts.h>
#include <boot/coreboot_tables.h>
#include <boot_device.h>
#include <spi_flash.h>
#include <stdlib.h>
#include <string.h>

static struct region_device boot;
static struct spi_flash flash;
static bool mutate_geometry;
static bool enter_again;
static uint32_t scratch_size = 4096;
static uint64_t read_scratch_base = 0x4001000;
static uint64_t communication_base = 0x100000;

void mock_assert(const int value, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!value)
		__builtin_trap();
}

void boot_device_init(void)
{
}

void region_device_init(struct region_device *rdev,
	const struct region_device_ops *ops, size_t offset, size_t size)
{
	*rdev = (struct region_device)REGION_DEV_INIT(ops, offset, size);
}

const struct region_device *boot_device_ro(void)
{
	return &boot;
}

const struct spi_flash *boot_device_spi_flash(void)
{
	return &flash;
}

int fmap_read_inventory(struct fmap_inventory *inventory)
{
	struct capsule_platform_facts nested = { 0 };
	uint16_t count = flash.size == (32U << 20) ? 5 : 3;

	*inventory = (struct fmap_inventory) {
		.size = flash.size,
		.area_count = count,
		.area = {
			{ .offset = 0, .size = flash.size },
			{ .offset = 0x1000, .size = 0x4000 },
			{ .offset = 0x2000, .size = 0x1000,
			  .flags = FMAP_AREA_PRESERVE },
			{ .offset = 0x1000000, .size = 0x800000 },
			{ .offset = 0x1800000, .size = 0x800000,
			  .flags = FMAP_AREA_STATIC },
		},
	};
	if (enter_again)
		assert(capsule_platform_facts_collect(&nested) == CB_ERR);
	if (mutate_geometry)
		flash.sector_size *= 2;
	return 0;
}

bool capsule_broker_buffers_get(
	struct capsule_broker_buffer_reservation *reservation)
{
	*reservation = (struct capsule_broker_buffer_reservation) {
		.communication_base = communication_base,
		.communication_reserved_size = 0x1000,
		.communication_size = CAPSULE_BROKER_TRANSPORT_SIZE,
		.staging_base = 0x200000,
		.staging_size = flash.size,
	};
	return true;
}

enum cb_err capsule_broker_scratch_acquire(uint32_t erase_size,
	struct capsule_broker_scratch_reservation *reservation)
{
	if (erase_size != 4096)
		return CB_ERR;
	*reservation = (struct capsule_broker_scratch_reservation) {
		.write_base = 0x4000000,
		.read_base = read_scratch_base,
		.erase_size = scratch_size,
	};
	return CB_SUCCESS;
}

enum cb_err efi_fw_info_get(struct lb_efi_fw_info *info)
{
	*info = (struct lb_efi_fw_info) {
		.tag = LB_TAG_EFI_FW_INFO,
		.size = sizeof(*info),
		.version = 1,
		.lowest_supported_version = 1,
		.fw_size = flash.size,
		.guid = { 1 },
	};
	return CB_SUCCESS;
}

static void initialize(uint32_t size)
{
	region_device_init(&boot, NULL, 0, size);
	flash = (struct spi_flash) {
		.size = size,
		.sector_size = 4096,
		.page_size = 256,
	};
}

static void expect_unchanged(const struct capsule_platform_facts *facts)
{
	const uint8_t *bytes = (const void *)facts;

	for (size_t i = 0; i < sizeof(*facts); i++)
		assert(bytes[i] == 0xa5);
}

int main(int argc, char **argv)
{
	struct capsule_platform_facts facts;
	uint32_t size;

	assert(argc == 2);
	size = !strcmp(argv[1], "amd") ? 32U << 20 : 16U << 20;
	initialize(size);
	memset(&facts, 0xa5, sizeof(facts));
	if (!strcmp(argv[1], "intel") || !strcmp(argv[1], "amd")) {
		struct capsule_platform_facts replay;

		assert(capsule_platform_facts_collect(&facts) == CB_SUCCESS);
		assert(facts.revision == CAPSULE_PLATFORM_FACTS_REVISION);
		assert(facts.size == sizeof(facts));
		assert(facts.boot_media_size == size);
		assert(facts.block_size == 256);
		assert(facts.erase_size == 4096);
		assert(facts.fmap.area_count == (!strcmp(argv[1], "amd") ? 5 : 3));
		assert(facts.scratch.write_base != facts.scratch.read_base);
		memset(&replay, 0xa5, sizeof(replay));
		assert(capsule_platform_facts_collect(&replay) == CB_ERR);
		expect_unchanged(&replay);
		return 0;
	}
	if (!strcmp(argv[1], "non4k"))
		flash.sector_size = 8192;
	else if (!strcmp(argv[1], "media-mismatch"))
		boot.region.size--;
	else if (!strcmp(argv[1], "scratch-mismatch"))
		scratch_size = 2048;
	else if (!strcmp(argv[1], "scratch-overlap"))
		read_scratch_base = 0x4000000;
	else if (!strcmp(argv[1], "scratch-communication"))
		communication_base = 0x4000000;
	else if (!strcmp(argv[1], "buffer-overlap"))
		communication_base = 0x200000;
	else if (!strcmp(argv[1], "geometry-mutation"))
		mutate_geometry = true;
	else if (!strcmp(argv[1], "reentry")) {
		enter_again = true;
		assert(capsule_platform_facts_collect(&facts) == CB_SUCCESS);
		return 0;
	} else if (!strcmp(argv[1], "flash-alias")) {
		assert(capsule_platform_facts_collect((void *)&flash) == CB_ERR);
		return 0;
	} else if (!strcmp(argv[1], "null")) {
		assert(capsule_platform_facts_collect(NULL) == CB_ERR);
		assert(capsule_platform_facts_collect(&facts) == CB_ERR);
		expect_unchanged(&facts);
		return 0;
	} else
		assert(false);
	assert(capsule_platform_facts_collect(&facts) == CB_ERR);
	expect_unchanged(&facts);
	assert(capsule_platform_facts_collect(&facts) == CB_ERR);
	expect_unchanged(&facts);
	return 0;
}
