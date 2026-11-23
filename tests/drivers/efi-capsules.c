/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>

/* Use the production parser and exercise its validation and copy passes separately. */
#define main coreboot_main
#include "../../src/drivers/efi/capsules.c"
#undef main

#define PAGE_SIZE (4 * KiB)
#define RAM_FLAGS (IORESOURCE_MEM | IORESOURCE_FIXED | IORESOURCE_STORED | \
		   IORESOURCE_ASSIGNED | IORESOURCE_CACHEABLE)

static uint8_t ram[8 * MiB] __aligned(2 * MiB);
static struct range_entry ranges[8];
static struct resource resources[8];
static size_t resource_count;
static uint64_t list_heads[8];
static size_t list_count, list_index;
static void *cbmem_base;
static enum boot_mode_t boot_mode;
static struct memory_range bootmem_capsules;
static struct lb_range records[MAX_CAPSULES];
static size_t record_count;

static void add_resource(void *base, size_t size, unsigned long flags)
{
	assert_true(resource_count < ARRAY_SIZE(resources));
	resources[resource_count++] = (struct resource){
		.base = (uintptr_t)base, .size = size, .flags = flags,
	};
}

void search_global_resources(unsigned long mask, unsigned long match,
			     resource_search_t search, void *context)
{
	for (size_t i = 0; i < resource_count; i++)
		if ((resources[i].flags & mask) == match)
			search(context, NULL, &resources[i]);
}

int cbmem_get_region(void **base, size_t *size)
{
	*base = cbmem_base;
	*size = PAGE_SIZE;
	return 0;
}

int smmstore_lookup_region(struct region_device *store)
{
	return 0;
}

enum cb_err efi_fv_get_option(const struct region_device *store, const EFI_GUID *guid,
			      const char *name, void *dest, uint32_t *size)
{
	char expected[32] = "CapsuleUpdateData";

	if (list_index != 0)
		snprintf(expected, sizeof(expected), "CapsuleUpdateData%zu", list_index);
	assert_string_equal(name, expected);
	assert_memory_equal(guid, &capsule_vendor_guid, sizeof(*guid));
	if (list_index == list_count)
		return CB_ERR;
	assert_int_equal(*size, sizeof(list_heads[0]));
	memcpy(dest, &list_heads[list_index++], *size);
	return CB_SUCCESS;
}

void set_boot_mode(enum boot_mode_t mode)
{
	boot_mode = mode;
}

enum boot_mode_t get_boot_mode(void)
{
	return boot_mode;
}

uint16_t pm_acpi_smi_cmd_port(void)
{
	fail_msg("unexpected capsule SMI");
	return 0;
}

void bootmem_add_range(uint64_t start, uint64_t size, enum bootmem_type tag)
{
	assert_int_equal(tag, BM_MEM_RESERVED);
	bootmem_capsules = (struct memory_range){ .base = start, .len = size };
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	assert_true(record_count < ARRAY_SIZE(records));
	return (void *)&records[record_count++];
}

int init_pae_pagetables(void *pgtbl)
{
	return 0;
}

void paging_disable_pae(void) { }

void pae_map_2M_page(void *pgtbl, uint64_t addr, void *window)
{
	fail_msg("unexpected PAE remap at %#llx", addr);
}

static EFI_CAPSULE_BLOCK_DESCRIPTOR data_block(const void *data, uint64_t size)
{
	return (EFI_CAPSULE_BLOCK_DESCRIPTOR){ .Length = size, .Union.DataBlock = (uintptr_t)data };
}

static EFI_CAPSULE_BLOCK_DESCRIPTOR continuation(const EFI_CAPSULE_BLOCK_DESCRIPTOR *next)
{
	return (EFI_CAPSULE_BLOCK_DESCRIPTOR){ .Union.ContinuationPointer = (uintptr_t)next };
}

static void make_capsule(void *data, uint32_t size, uint8_t fill)
{
	const EFI_CAPSULE_HEADER header = {
		.CapsuleGuid = EFI_FIRMWARE_MANAGEMENT_CAPSULE_ID_GUID,
		.HeaderSize = sizeof(header),
		.Flags = CAPSULE_FLAGS_PERSIST_ACROSS_RESET,
		.CapsuleImageSize = size,
	};

	memset(data, fill, size);
	memcpy(data, &header, sizeof(header));
}

static bool validate(const EFI_CAPSULE_BLOCK_DESCRIPTOR *list)
{
	unsigned int count = 0;
	uint64_t size = 0;

	bool valid = check_capsule_block((struct block_descr){ .self = (uintptr_t)list },
					 &count, &size);
	if (!valid) {
		assert_int_equal(count, 0);
		assert_int_equal(size, 0);
	}
	return valid;
}

static int setup(void **state)
{
	assert_true((uintptr_t)ram + sizeof(ram) < 4ULL * GiB);
	memset(ram, 0, sizeof(ram));
	memranges_init_empty(&memory_map, ranges, ARRAY_SIZE(ranges));
	memranges_insert(&memory_map, (uintptr_t)ram, sizeof(ram), BM_MEM_RAM);
	uefi_capsule_count = 0;
	return 0;
}

static void test_malformed_descriptors(void **state)
{
	const unsigned int size = 48;
	uint8_t *data = ram + PAGE_SIZE;
	EFI_CAPSULE_BLOCK_DESCRIPTOR *list = (void *)ram;
	assert_true(validate(list));
	make_capsule(data, size, 0x12);
	list[0] = data_block(data, size);
	assert_true(validate(list));
	assert_false(validate((void *)(ram + 1)));

	list[0] = data_block(NULL, size);
	assert_false(validate(list));
	list[0] = data_block(data, sizeof(EFI_CAPSULE_HEADER) - 1);
	assert_false(validate(list));
	list[0] = data_block(data, size - 1);
	assert_false(validate(list));
	list[0] = data_block(data, size + 1);
	assert_false(validate(list));
	list[0] = data_block(data, size);
	list[1] = continuation(&list[2]);
	list[2] = continuation(&list[3]);
	assert_false(validate(list));

	/* The cycle consumes data every two descriptors, so it needs the walk limit. */
	((EFI_CAPSULE_HEADER *)data)->CapsuleImageSize = UINT32_MAX;
	list[1] = continuation(list);
	assert_false(validate(list));
}

static void test_mapping_limits(void **state)
{
	const uint32_t image_size = 48;
	EFI_CAPSULE_BLOCK_DESCRIPTOR *list = (void *)ram;
	memranges_insert(&memory_map, 0, PAGE_SIZE, BM_MEM_RAM);
	assert_false(validate(NULL));
	list[0] = data_block(NULL, image_size);
	assert_false(validate(list));
	assert_false(is_in_unused_ram(UINT64_MAX - 15, image_size));

	const uint64_t boundary = (uint64_t)CONFIG_CPU_PT_ROM_MAP_GB * GiB;
	memranges_insert(&memory_map, boundary - 2 * PAGE_SIZE, 4 * PAGE_SIZE, BM_MEM_RAM);
	if (ENV_X86_64) {
		assert_true(is_in_unused_ram(boundary - image_size, image_size));
		assert_false(is_in_unused_ram(boundary - image_size + 1, image_size));
		assert_false(validate((void *)(uintptr_t)boundary));
		list[0] = data_block((void *)(uintptr_t)boundary, image_size);
		assert_false(validate(list));
	} else {
		struct block_descr head = { .len = image_size };
		head.addr = boundary - sizeof(EFI_CAPSULE_HEADER);
		assert_true(is_good_capsule_head(&head));
		head.addr++;
		assert_false(is_good_capsule_head(&head));
		list[0] = data_block((void *)(uintptr_t)head.addr, image_size);
		assert_false(validate(list));
		head.addr = (uintptr_t)(ram + 2 * MiB - 8);
		assert_true(is_good_capsule_head(&head));
	}

	const struct memory_range buffer = pick_buffer(PAGE_SIZE);
	assert_int_equal(buffer.base, 4ULL * GiB - PAGE_SIZE);
	assert_int_equal(buffer.len, PAGE_SIZE);
}

static void test_output_capacity(void **state)
{
	/* The data block spans a 2 MiB window even though its header fits. */
	uint8_t *source = ram + 2 * MiB - sizeof(EFI_CAPSULE_HEADER);
	uint8_t *dest = ram + 3 * MiB;
	EFI_CAPSULE_BLOCK_DESCRIPTOR *list = (void *)ram;
	const uint32_t size = 47;
	make_capsule(source, size, 0xa5);
	list[0] = data_block(source, size);
	assert_true(validate(list));

	struct block_descr first = { .self = (uintptr_t)list };
	coalesce_buffer = (struct memory_range){ .base = (uintptr_t)dest, .len = size };
	uint32_t used = 0;
	assert_false(coalesce_capsules(first, &used));
	assert_int_equal(used, 0);
	assert_int_equal(uefi_capsule_count, 0);
	assert_int_equal(dest[0], 0);

	coalesce_buffer.len = ALIGN_UP(size, CAPSULE_ALIGNMENT);
	assert_true(coalesce_capsules(first, &used));
	assert_int_equal(used, coalesce_buffer.len);
	assert_memory_equal(dest, source, size);
}

static void test_validate_capsules_past_limit(void **state)
{
	const unsigned int image_size = 47;
	uint8_t *data = ram + PAGE_SIZE;
	EFI_CAPSULE_BLOCK_DESCRIPTOR *list = (void *)ram;
	make_capsule(data, image_size, 0xa5);
	list[0] = data_block(data, image_size);
	list[1] = list[0];
	list[2] = data_block(NULL, image_size);

	const struct block_descr first = { .self = (uintptr_t)list };
	unsigned int count = MAX_CAPSULES - 1;
	uint64_t size = 128;
	assert_false(check_capsule_block(first, &count, &size));
	assert_int_equal(count, MAX_CAPSULES - 1);
	assert_int_equal(size, 128);

	list[2] = (EFI_CAPSULE_BLOCK_DESCRIPTOR){ 0 };
	assert_true(check_capsule_block(first, &count, &size));
	assert_int_equal(count, MAX_CAPSULES + 1);
	assert_int_equal(size, 128 + ALIGN_UP(image_size, CAPSULE_ALIGNMENT));
}

static void test_parse_capsules(void **state)
{
	enum { SHARED_CAPSULES = 4, LAST_LIST_CAPSULES = MAX_CAPSULES - SHARED_CAPSULES };
	struct {
		EFI_CAPSULE_BLOCK_DESCRIPTOR soft_reserved[2];
		EFI_CAPSULE_BLOCK_DESCRIPTOR cbmem[2];
		EFI_CAPSULE_BLOCK_DESCRIPTOR damaged[3];
		EFI_CAPSULE_BLOCK_DESCRIPTOR split[3];
		EFI_CAPSULE_BLOCK_DESCRIPTOR second[2];
		EFI_CAPSULE_BLOCK_DESCRIPTOR shared[2];
		EFI_CAPSULE_BLOCK_DESCRIPTOR many[LAST_LIST_CAPSULES + 2];
	} *lists = (void *)ram;
	uint8_t *data = ram + PAGE_SIZE;
	uint8_t *large = ram + 2 * PAGE_SIZE;
	uint8_t *dest = ram + 2 * MiB;
	EFI_CAPSULE_BLOCK_DESCRIPTOR *reserved = (void *)(dest + PAGE_SIZE);
	uint8_t *soft_reserved = dest + 2 * PAGE_SIZE;
	cbmem_base = ram + 7 * MiB;

	/* Only the single destination page should be left after exclusions. */
	add_resource(ram, 4 * PAGE_SIZE, RAM_FLAGS);
	add_resource(dest, 3 * PAGE_SIZE, RAM_FLAGS);
	add_resource(reserved, PAGE_SIZE, RAM_FLAGS | IORESOURCE_RESERVE);
	add_resource(soft_reserved, PAGE_SIZE, RAM_FLAGS | IORESOURCE_SOFT_RESERVE);
	add_resource(ram + 6 * MiB, PAGE_SIZE, RAM_FLAGS);
	add_resource(cbmem_base, PAGE_SIZE, RAM_FLAGS);

	make_capsule(data, 55, 0x11);
	make_capsule(data + 64, 49, 0x22);
	make_capsule(data + 128, 41, 0x33);
	make_capsule(soft_reserved, 32, 0x44);
	make_capsule(cbmem_base, 32, 0x55);
	reserved[0] = data_block(data, 55);
	lists->soft_reserved[0] = data_block(soft_reserved, 32);
	lists->cbmem[0] = data_block(cbmem_base, 32);
	lists->damaged[0] = data_block(data, 55);
	lists->damaged[1] = data_block(NULL, 32);

	memcpy(data + 192, data + 40, 15);
	lists->split[0] = data_block(data, 40);
	lists->split[1] = data_block(data + 192, 15);
	lists->split[2] = continuation(lists->shared);
	lists->second[0] = data_block(data + 64, 49);
	lists->second[1] = continuation(lists->shared);
	lists->shared[0] = data_block(data + 128, 41);
	for (unsigned int i = 0; i < LAST_LIST_CAPSULES; i++) {
		make_capsule(data + 256 + i * 64, 33, i);
		lists->many[i] = data_block(data + 256 + i * 64, 33);
	}
	make_capsule(large, 2 * PAGE_SIZE, 0x66);
	lists->many[LAST_LIST_CAPSULES] = data_block(large, 2 * PAGE_SIZE);

	const EFI_CAPSULE_BLOCK_DESCRIPTOR *heads[] = {
		reserved, lists->soft_reserved, lists->cbmem, lists->damaged,
		lists->split, lists->second, lists->many,
	};
	list_count = ARRAY_SIZE(heads);
	for (size_t i = 0; i < list_count; i++)
		list_heads[i] = (uintptr_t)heads[i];

	uint8_t original[sizeof(*lists)];
	memcpy(original, lists, sizeof(original));
	memset(dest, 0xcc, PAGE_SIZE);
	efi_parse_capsules();
	efi_add_capsules_to_bootmem();
	struct lb_header header;
	lb_efi_capsules(&header);
	assert_int_equal(boot_mode, LB_BOOT_MODE_FLASH_UPDATE);
	assert_int_equal(bootmem_capsules.base, (uintptr_t)dest);
	assert_int_equal(bootmem_capsules.len, PAGE_SIZE);
	assert_int_equal(record_count, MAX_CAPSULES);
	assert_memory_equal(lists, original, sizeof(original));

	const unsigned int first_sizes[] = { 55, 41, 49, 41 };
	const uint8_t *first_sources[] = { data, data + 128, data + 64, data + 128 };
	uint32_t used = 0;
	for (size_t i = 0; i < record_count; i++) {
		const bool first = i < SHARED_CAPSULES;
		const uint32_t size = first ? first_sizes[i] : 33;
		const uint8_t *source = first ? first_sources[i] :
			data + 256 + (i - SHARED_CAPSULES) * 64;
		assert_int_equal(records[i].tag, LB_TAG_CAPSULE);
		assert_int_equal(records[i].range_start, (uintptr_t)dest + used);
		assert_int_equal(records[i].range_size, size);
		assert_memory_equal(dest + used, source, size);
		used += ALIGN_UP(size, CAPSULE_ALIGNMENT);
	}
	for (uint32_t i = used; i < PAGE_SIZE; i++)
		assert_int_equal(dest[i], 0xcc);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_malformed_descriptors, setup),
		cmocka_unit_test_setup(test_mapping_limits, setup),
		cmocka_unit_test_setup(test_output_capacity, setup),
		cmocka_unit_test_setup(test_validate_capsules_past_limit, setup),
		cmocka_unit_test_setup(test_parse_capsules, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
