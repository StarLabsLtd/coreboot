/* SPDX-License-Identifier: GPL-2.0-only */

/* Existing FV/FTW fixture creates modeled media, not native authority. */
#define main ftw_fixture_main
#include "../lib/payload_mm_authvar_ftw_test.c"
#undef main
#undef assert

#define main coreboot_main
#ifndef CAPSULE_MM_READ_SOURCE
#define CAPSULE_MM_READ_SOURCE "../../src/drivers/efi/capsules.c"
#endif
#include CAPSULE_MM_READ_SOURCE
#undef main
#undef assert
#define assert(c) do { if (!(c)) assertion_failed(#c, __FILE__, __LINE__); } while (0)

static uint8_t owned_snapshot[256 * KiB];
static size_t owned_size;
static bool owned;
static unsigned int reads;
static unsigned int mode_changes;
static unsigned int scenario;
static struct mem_region_device media;

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

void die(const char *message, ...)
{
	(void)message;
	abort();
}

const struct cbmem_entry *cbmem_entry_find(u32 id)
{
	assert(id == CBMEM_ID_CAPSULE_READ_SNAPSHOT);
	return owned ? (const void *)&owned_size : NULL;
}

const struct cbmem_entry *cbmem_entry_add(u32 id, u64 size)
{
	assert(id == CBMEM_ID_CAPSULE_READ_SNAPSHOT && !owned);
	assert(size <= sizeof(owned_snapshot));
	owned_size = size;
	owned = true;
	memset(owned_snapshot, 0xa5, owned_size);
	return (const void *)&owned_size;
}

void *cbmem_entry_start(const struct cbmem_entry *entry)
{
	assert(entry == (const void *)&owned_size && owned);
	return owned_snapshot;
}

u64 cbmem_entry_size(const struct cbmem_entry *entry)
{
	assert(entry == (const void *)&owned_size && owned);
	return owned_size;
}

int cbmem_entry_remove(const struct cbmem_entry *entry)
{
	assert(entry == (const void *)&owned_size && owned);
	assert(!capsule_store.store && !capsule_store.entries);
	for (size_t i = 0; i < owned_size; i++)
		assert(!owned_snapshot[i]);
	owned = false;
	return 0;
}

int smmstore_lookup_read_region(struct region_device *rdev)
{
	return rdev_chain(rdev, &media.rdev, 0, sizeof(region));
}

ssize_t __real_rdev_readat(const struct region_device *, void *, size_t, size_t);
ssize_t __wrap_rdev_readat(const struct region_device *rdev, void *dest,
	size_t offset, size_t size)
{
	ssize_t result = __real_rdev_readat(rdev, dest, offset, size);

	reads++;
	assert(reads == 1 && offset == 0 && size == sizeof(region));
	if (scenario == 3)
		memset(region, 0, sizeof(region));
	if (scenario == 4)
		return result - 1;
	return result;
}

enum cb_err efi_fv_get_option(const struct region_device *rdev, const EFI_GUID *guid,
	const char *name, void *dest, uint32_t *size)
{
	(void)rdev; (void)guid; (void)name; (void)dest; (void)size;
	assert(false); /* A current MM reader must never fall back to raw FV lookup. */
	return CB_ERR;
}

void set_boot_mode(const enum boot_mode_t mode)
{
	assert(mode == LB_BOOT_MODE_FLASH_UPDATE);
	mode_changes++;
}

/* The chosen positive is the real disk-pending early exit, not SG execution. */
void memranges_init_with_alignment(struct memranges *ranges, unsigned long mask,
	unsigned long match, unsigned long tag, unsigned char align)
{
	(void)mask; (void)match; (void)tag; (void)align;
	memset(ranges, 0, sizeof(*ranges));
}

void memranges_add_resources(struct memranges *ranges, unsigned long mask,
	unsigned long match, unsigned long tag)
{
	(void)ranges; (void)mask; (void)match; (void)tag;
}

void memranges_teardown(struct memranges *ranges) { (void)ranges; }
void memranges_insert(struct memranges *ranges, resource_t base, resource_t size,
	unsigned long tag)
{
	(void)ranges; (void)base; (void)size; (void)tag;
	assert(false);
}

int cbmem_get_region(void **base, size_t *size)
{
	(void)base; (void)size;
	assert(false);
	return -1;
}

void bootmem_add_range(uint64_t base, uint64_t size, enum bootmem_type type)
{
	(void)base; (void)size; (void)type;
	assert(false);
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	(void)header;
	assert(false);
	return NULL;
}

void pae_map_2M_page(void *table, uint64_t address, void *window)
{
	(void)table; (void)address; (void)window;
	assert(false);
}

int main(int argc, char **argv)
{
	const uint16_t name[] = { 'O', 's', 'I', 'n', 'd', 'i', 'c', 'a', 't', 'i',
		'o', 'n', 's', 0 };
	uint8_t *record = region + FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	assert(argc == 2 && argv[1][0] >= '0' && argv[1][0] <= '8' && !argv[1][1]);
	scenario = argv[1][0] - '0';
	make_media();
	make_workspace(working(), PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);
	memset(record, 0, PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE);
	put16(record, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	record[2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	put32(record + 4, 7);
	put32(record + 36, sizeof(name));
	put32(record + 40, sizeof(uint64_t));
	memcpy(record + 44, &efi_global_variable_guid, 16);
	memcpy(record + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE, name, sizeof(name));
	put64(record + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + sizeof(name),
		EFI_OS_INDICATIONS_FILE_CAPSULE_DELIVERY_SUPPORTED);
	assert(plan() == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	if (scenario == 1)
		make_write(0xff, PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
	if (scenario == 2)
		region[0] ^= 1;
	if (scenario == 5)
		put32(record + 4, 6);
	if (scenario == 6) {
		uint8_t *tail = record + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
			sizeof(name) + sizeof(uint64_t);

		put16(tail, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	}
	if (scenario == 7) {
		uint8_t *successor = record + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
			sizeof(name) + sizeof(uint64_t);

		memcpy(successor, record, PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE +
			sizeof(name) + sizeof(uint64_t));
		record[2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION;
		put64(successor + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + sizeof(name), 0);
	}
	if (scenario == 8)
		record[2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED;
	mem_region_device_ro_init(&media, region, sizeof(region));
	efi_parse_capsules();
	assert(reads == 1 && !owned && !capsule_store.store && !uefi_capsule_count);
	assert(mode_changes == (scenario == 0 || scenario == 3));
	return 0;
}
