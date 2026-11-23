/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <device/pci.h>
#include <root_bridge_info.h>
#include <string.h>

static uint32_t config[2][64];
static unsigned int hidden_reads;
static uint16_t read_config16(const struct device *dev, uint16_t reg)
{
	if (dev->hidden)
		hidden_reads++;
	return config[dev->path.pci.devfn][reg / 4] >> (8 * (reg % 4));
}

static uint32_t read_config32(const struct device *dev, uint16_t reg)
{
	if (dev->hidden)
		hidden_reads++;
	return config[dev->path.pci.devfn][reg / 4];
}

/* PCI reads are the hardware boundary; run the actual producer below. */
#define main coreboot_main
#define pci_read_config16 read_config16
#define pci_read_config32 read_config32
#include "../../src/lib/root_bridge_info.c"
#undef main

struct device *all_devices;
static struct device domains[2], devices[2];
static struct bus buses[2];
static struct resource resources[3];
static unsigned char storage[512];
static bool allocated, allocation_fails;
static unsigned int allocations, removals;

void *cbmem_find(uint32_t id)
{
	assert_int_equal(id, CBMEM_ID_RB_INFO);
	return allocated ? storage : NULL;
}

void *cbmem_add(uint32_t id, uint64_t size)
{
	assert_int_equal(id, CBMEM_ID_RB_INFO);
	assert_true(size <= sizeof(storage));
	allocations++;
	if (allocation_fails)
		return NULL;
	allocated = true;
	return storage;
}

const struct cbmem_entry *cbmem_entry_find(uint32_t id)
{
	assert_int_equal(id, CBMEM_ID_RB_INFO);
	return (const struct cbmem_entry *)storage;
}

int cbmem_entry_remove(const struct cbmem_entry *entry)
{
	assert_ptr_equal(entry, storage);
	allocated = false;
	removals++;
	return 0;
}

static int setup(void **state)
{
	memset(domains, 0, sizeof(domains));
	memset(devices, 0, sizeof(devices));
	memset(buses, 0, sizeof(buses));
	memset(resources, 0, sizeof(resources));
	memset(config, 0, sizeof(config));
	memset(storage, 0xa5, sizeof(storage));
	hidden_reads = 0;
	allocated = allocation_fails = false;
	allocations = removals = 0;
	for (unsigned int i = 0; i < ARRAY_SIZE(domains); i++) {
		domains[i].path.type = DEVICE_PATH_DOMAIN;
		domains[i].enabled = true;
		domains[i].downstream = &buses[i];
		buses[i].children = &devices[i];
		buses[i].secondary = buses[i].subordinate = i * 16;
		devices[i].path.type = DEVICE_PATH_PCI;
		devices[i].path.pci.devfn = i;
		devices[i].enabled = true;
		devices[i].upstream = &buses[i];
		devices[i].resource_list = &resources[i];
		resources[i].index = PCI_BASE_ADDRESS_0;
		resources[i].base = 0xbe000000 + i * MiB;
		resources[i].size = MiB;
		resources[i].flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED;
		config[i][PCI_COMMAND / 4] = PCI_COMMAND_MEMORY | PCI_COMMAND_IO;
		config[i][PCI_BASE_ADDRESS_0 / 4] = resources[i].base;
	}
	all_devices = domains;
	return 0;
}

static void test_assigned_resources_and_wire_format(void **state)
{
	_Static_assert(sizeof(struct pci_root_bridges_info) == 6, "UPL header");
	_Static_assert(sizeof(struct pci_root_bridge_info) == 182, "UPL root");
	devices[0].class = PCI_CLASS_DISPLAY_VGA << 8;
	publish_root_bridges(NULL);
	assert_true(allocated);
	struct pci_root_bridges_info *info = (void *)storage;
	assert_int_equal(info->revision, 1);
	assert_int_equal(info->length, 188);
	assert_true(info->resource_assigned);
	assert_int_equal(info->count, 1);
	struct pci_root_bridge_info *root = &info->root_bridge[0];
	assert_int_equal(root->mem.base, resources[0].base);
	assert_int_equal(root->mem.limit, resources[0].base + MiB - 1);
	assert_int_equal(root->mem_above4g.base, UINT64_MAX);
	assert_int_equal(root->io.base, UINT64_MAX);
	assert_int_equal(root->attributes, 0x6001c);
	assert_int_equal(root->supports, root->attributes);
	assert_int_equal(root->hid, 0x0a0341d0);
	assert_int_equal(root->uid, 0);
	assert_int_equal(storage[188], 0xa5);
}

static void test_multiple_domains_and_64bit_bar(void **state)
{
	domains[0].next = &domains[1];
	resources[1].base = 0x100000000ULL;
	config[1][PCI_BASE_ADDRESS_0 / 4] = PCI_BASE_ADDRESS_MEM_LIMIT_64;
	config[1][PCI_BASE_ADDRESS_1 / 4] = 1;
	publish_root_bridges(NULL);
	assert_true(allocated);
	struct pci_root_bridges_info *info = (void *)storage;
	assert_int_equal(info->count, 2);
	assert_int_equal(info->length, 370);
	struct pci_root_bridge_info *root = &info->root_bridge[1];
	assert_int_equal(root->segment, 0);
	assert_int_equal(root->bus.base, 16);
	assert_int_equal(root->bus.limit, 16);
	assert_int_equal(root->uid, 16);
	assert_int_equal(root->mem.base, UINT64_MAX);
	assert_int_equal(root->mem_above4g.base, 0x100000000ULL);
	assert_int_equal(root->mem_above4g.limit, 0x1000fffffULL);
}

static void test_inconsistent_bar_retains_scan(void **state)
{
	config[0][PCI_BASE_ADDRESS_0 / 4] += MiB;
	publish_root_bridges(NULL);
	assert_false(allocated);
	assert_int_equal(removals, 1);
}

static void test_invalid_resource_extent_retains_scan(void **state)
{
	resources[0].base = UINT64_MAX - 0xfff;
	resources[0].size = 0x2000;
	config[0][PCI_BASE_ADDRESS_0 / 4] = 0xfffff004;
	config[0][PCI_BASE_ADDRESS_1 / 4] = UINT32_MAX;
	publish_root_bridges(NULL);
	assert_false(allocated);
	assert_int_equal(removals, 1);
}

static void test_existing_handoff_is_preserved(void **state)
{
	allocated = true;
	publish_root_bridges(NULL);
	assert_int_equal(allocations, 0);
	assert_int_equal(storage[0], 0xa5);
}

static void test_allocation_failure_retains_scan(void **state)
{
	allocation_fails = true;
	publish_root_bridges(NULL);
	assert_false(allocated);
	assert_int_equal(allocations, 1);
	assert_int_equal(removals, 0);
}

static void test_bridge_windows_and_legacy_decoding(void **state)
{
	devices[0].hdr_type = PCI_HEADER_TYPE_BRIDGE;
	devices[0].class = PCI_CLASS_BRIDGE_PCI << 8;
	buses[0].subordinate = 1;
	config[0][PCI_BASE_ADDRESS_0 / 4] = 0;
	config[0][PCI_BRIDGE_CONTROL / 4] =
		(PCI_BRIDGE_CTL_ISA | PCI_BRIDGE_CTL_VGA | PCI_BRIDGE_CTL_VGA16) << 16;
	resources[0].index = PCI_MEMORY_BASE;
	resources[0].flags |= IORESOURCE_PCI_BRIDGE;
	resources[0].next = &resources[2];
	resources[2] = (struct resource) {
		.index = PCI_IO_BASE, .base = 0xefa0, .size = 0x20,
		.flags = IORESOURCE_IO | IORESOURCE_ASSIGNED | IORESOURCE_PCI_BRIDGE,
	};
	publish_root_bridges(NULL);
	assert_true(allocated);
	struct pci_root_bridge_info *root =
		&((struct pci_root_bridges_info *)storage)->root_bridge[0];
	assert_int_equal(root->bus.limit, 1);
	assert_int_equal(root->io.base, 0xefa0);
	assert_int_equal(root->io.limit, 0xefbf);
	assert_int_equal(root->mem.base, resources[0].base);
	assert_int_equal(root->mem.limit, resources[0].base + MiB - 1);
	assert_int_equal(root->attributes, 0x7001f);
}

static void test_virtual_chipset_resources_are_not_bars(void **state)
{
	/* A host bridge's synthetic resource at index 0x10 is not a BAR. */
	config[0][PCI_BASE_ADDRESS_0 / 4] = 0;
	publish_root_bridges(NULL);
	assert_true(allocated);
	struct pci_root_bridge_info *root =
		&((struct pci_root_bridges_info *)storage)->root_bridge[0];
	assert_int_equal(root->mem.base, UINT64_MAX);
	assert_int_equal(root->mem.limit, 0);
}

static void test_disabled_decode_is_not_exported(void **state)
{
	config[0][PCI_COMMAND / 4] = 0;
	publish_root_bridges(NULL);
	assert_true(allocated);
	struct pci_root_bridge_info *root =
		&((struct pci_root_bridges_info *)storage)->root_bridge[0];
	assert_int_equal(root->mem.base, UINT64_MAX);
}

static void test_invalid_bus_range_retains_scan(void **state)
{
	buses[0].subordinate = 256;
	publish_root_bridges(NULL);
	assert_false(allocated);
	assert_int_equal(removals, 1);
}

static void test_missing_domains_do_not_allocate(void **state)
{
	all_devices = NULL;
	publish_root_bridges(NULL);
	assert_false(allocated);
	assert_int_equal(allocations, 0);
}

static void test_empty_domain_retains_scan(void **state)
{
	buses[0].children = NULL;
	publish_root_bridges(NULL);
	assert_false(allocated);
	assert_int_equal(removals, 1);
}

static void test_unsupported_header_retains_scan(void **state)
{
	devices[0].hdr_type = PCI_HEADER_TYPE_CARDBUS;
	publish_root_bridges(NULL);
	assert_false(allocated);
	assert_int_equal(removals, 1);
}

static void test_nonzero_segment_retains_scan(void **state)
{
	/* Segment is not encoded in the existing PciRoot(HID, UID) path. */
	buses[0].segment_group = 1;
	publish_root_bridges(NULL);
	assert_false(allocated);
	assert_int_equal(removals, 1);
}

static void test_hidden_device_does_not_discard_visible_root(void **state)
{
	devices[0].sibling = &devices[1];
	devices[1].upstream = &buses[0];
	devices[1].hidden = true;
	memset(config[1], 0xff, sizeof(config[1]));
	publish_root_bridges(NULL);
	assert_true(allocated);
	assert_int_equal(hidden_reads, 0);
	struct pci_root_bridges_info *info = (void *)storage;
	assert_int_equal(info->count, 1);
	assert_int_equal(info->root_bridge[0].mem.base, resources[0].base);
	assert_int_equal(info->root_bridge[0].mem.limit,
		resources[0].base + resources[0].size - 1);
}

static void test_hidden_only_domain_retains_scan(void **state)
{
	devices[0].hidden = true;
	publish_root_bridges(NULL);
	assert_false(allocated);
	assert_int_equal(hidden_reads, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_hidden_device_does_not_discard_visible_root, setup),
		cmocka_unit_test_setup(test_hidden_only_domain_retains_scan, setup),
		cmocka_unit_test_setup(test_nonzero_segment_retains_scan, setup),
		cmocka_unit_test_setup(test_empty_domain_retains_scan, setup),
		cmocka_unit_test_setup(test_unsupported_header_retains_scan, setup),
		cmocka_unit_test_setup(test_bridge_windows_and_legacy_decoding, setup),
		cmocka_unit_test_setup(test_virtual_chipset_resources_are_not_bars, setup),
		cmocka_unit_test_setup(test_disabled_decode_is_not_exported, setup),
		cmocka_unit_test_setup(test_invalid_bus_range_retains_scan, setup),
		cmocka_unit_test_setup(test_missing_domains_do_not_allocate, setup),
		cmocka_unit_test_setup(test_assigned_resources_and_wire_format, setup),
		cmocka_unit_test_setup(test_multiple_domains_and_64bit_bar, setup),
		cmocka_unit_test_setup(test_inconsistent_bar_retains_scan, setup),
		cmocka_unit_test_setup(test_invalid_resource_extent_retains_scan, setup),
		cmocka_unit_test_setup(test_existing_handoff_is_preserved, setup),
		cmocka_unit_test_setup(test_allocation_failure_retains_scan, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
