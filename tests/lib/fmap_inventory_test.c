/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot_device.h>
#include <console/console.h>
#include <endian.h>
#include <fmap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t media[0x10000];
static struct region_device boot = REGION_DEV_INIT(NULL, 0, sizeof(media));
static bool short_read;
char _fmap_cache[FMAP_SIZE];

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

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

const struct region_device *boot_device_ro(void)
{
	return &boot;
}

void *rdev_mmap(const struct region_device *rd, size_t offset, size_t size)
{
	if (offset > rd->region.size || size > rd->region.size - offset)
		return NULL;
	return media + rd->region.offset + offset;
}

int rdev_munmap(const struct region_device *rd, void *mapping)
{
	(void)rd;
	(void)mapping;
	return 0;
}

ssize_t rdev_readat(const struct region_device *rd, void *buffer,
	size_t offset, size_t size)
{
	if (offset > rd->region.size || size > rd->region.size - offset)
		return -1;
	if (short_read && rd->region.offset == FMAP_OFFSET &&
	    offset == sizeof(struct fmap) && size)
		size--;
	memcpy(buffer, media + rd->region.offset + offset, size);
	return (ssize_t)size;
}

int rdev_chain(struct region_device *child, const struct region_device *parent,
	size_t offset, size_t size)
{
	if (offset > parent->region.size || size > parent->region.size - offset)
		return -1;
	*child = (struct region_device)REGION_DEV_INIT(NULL,
		parent->region.offset + offset, size);
	return 0;
}

int rdev_chain_mem(struct region_device *child, const void *base, size_t size)
{
	(void)child;
	(void)base;
	(void)size;
	return -1;
}

static struct fmap_area area(uint32_t offset, uint32_t size,
	const char *name, uint16_t flags)
{
	struct fmap_area result = {
		.offset = htole32(offset),
		.size = htole32(size),
		.flags = htole16(flags),
	};

	strncpy((char *)result.name, name, sizeof(result.name) - 1);
	return result;
}

static struct fmap *build_fmap(uint16_t count)
{
	struct fmap *map = (void *)(media + FMAP_OFFSET);

	memset(media, 0, sizeof(media));
	memcpy(map->signature, FMAP_SIGNATURE, sizeof(map->signature));
	map->ver_major = FMAP_VER_MAJOR;
	map->ver_minor = FMAP_VER_MINOR;
	map->size = htole32(sizeof(media));
	strcpy((char *)map->name, "TEST");
	map->nareas = htole16(count);
	map->areas[0] = area(0, sizeof(media), "FLASH", 0);
	map->areas[1] = area(0x1000, 0x8000, "COREBOOT", 0);
	map->areas[2] = area(0x2000, 0x1000, "PRESERVE", FMAP_AREA_PRESERVE);
	for (uint16_t i = 3; i < count; i++) {
		char name[FMAP_STRLEN];

		snprintf(name, sizeof(name), "N%u", i);
		map->areas[i] = area(0x9000U + (uint32_t)i * 0x100U,
			0x80, name, 0);
	}
	return map;
}

int main(int argc, char **argv)
{
	struct fmap_inventory inventory;
	struct fmap *map;

	assert(argc == 2);
	map = build_fmap(!strcmp(argv[1], "count") ? 33 : 3);
	if (!strcmp(argv[1], "valid"))
		map->areas[2] = area(0xa000, 0x1000, "DATA", 0);
	else if (!strcmp(argv[1], "exact-span"))
		map->areas[2] = area(0x1000, 0x8000, "CHILD", 0);
	memset(&inventory, 0xa5, sizeof(inventory));
	if (!strcmp(argv[1], "valid") || !strcmp(argv[1], "nested") ||
	    !strcmp(argv[1], "exact-span")) {
		assert(fmap_read_inventory(&inventory) == 0);
		assert(inventory.size == sizeof(media));
		assert(inventory.area_count == 3);
		assert(inventory.area[2].flags ==
			(!strcmp(argv[1], "nested") ? FMAP_AREA_PRESERVE : 0));
		return 0;
	}
	if (!strcmp(argv[1], "partial"))
		map->areas[2] = area(0x8000, 0x2000, "PARTIAL", 0);
	else if (!strcmp(argv[1], "duplicate-name"))
		map->areas[2] = area(0xa000, 0x1000, "COREBOOT", 0);
	else if (!strcmp(argv[1], "flags"))
		map->areas[2].flags = htole16(0x8000);
	else if (!strcmp(argv[1], "overflow"))
		map->areas[2] = area(0xfffffff0, 0x100, "OVERFLOW", 0);
	else if (!strcmp(argv[1], "short"))
		short_read = true;
	else if (strcmp(argv[1], "count"))
		assert(false);
	assert(fmap_read_inventory(&inventory) < 0);
	return 0;
}
