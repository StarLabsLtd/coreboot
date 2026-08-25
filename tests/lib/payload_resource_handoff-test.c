/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <commonlib/helpers.h>
#include <crc_byte.h>
#include <device/device.h>
#include <tests/test.h>

const unsigned int coreboot_version_timestamp = 0x12345678;

static uint8_t table_storage[sizeof(struct lb_header) + 512];
static struct lb_header *table;

static struct bus domain_bus;
static struct bus child_bus;
static struct device domain;
static struct device pci_device;
static struct device child_device;
static struct resource resources[5];

struct device *all_devices;

const char *dev_path(const struct device *device)
{
	return device == &pci_device ? "PCI: 00:00:02.0" : "PCI: 00:01:00.0";
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	struct lb_record *record = (void *)((uint8_t *)header + sizeof(*header) +
						 header->table_bytes);

	if (header->table_entries != 0) {
		header->table_bytes += record->size;
		record = (void *)((uint8_t *)header + sizeof(*header) + header->table_bytes);
	}
	header->table_entries++;
	record->tag = LB_TAG_UNUSED;
	record->size = sizeof(*record);
	return record;
}

static int setup(void **state)
{
	memset(table_storage, 0, sizeof(table_storage));
	table = (void *)table_storage;
	memset(&domain_bus, 0, sizeof(domain_bus));
	memset(&child_bus, 0, sizeof(child_bus));
	memset(&domain, 0, sizeof(domain));
	memset(&pci_device, 0, sizeof(pci_device));
	memset(&child_device, 0, sizeof(child_device));
	memset(resources, 0, sizeof(resources));

	domain.enabled = 1;
	domain.path.type = DEVICE_PATH_DOMAIN;
	domain.downstream = &domain_bus;
	domain.next = &pci_device;
	domain_bus.dev = &domain;
	domain_bus.secondary = 0;
	domain_bus.subordinate = 2;
	domain_bus.segment_group = 0;

	pci_device.enabled = 1;
	pci_device.path.type = DEVICE_PATH_PCI;
	pci_device.upstream = &domain_bus;
	pci_device.downstream = &child_bus;
	pci_device.resource_list = &resources[0];
	pci_device.next = &child_device;
	child_bus.dev = &pci_device;
	child_bus.secondary = 1;
	child_bus.subordinate = 1;
	child_bus.segment_group = 0;

	child_device.enabled = 1;
	child_device.path.type = DEVICE_PATH_PCI;
	child_device.upstream = &child_bus;
	child_device.resource_list = &resources[4];

	resources[0] = (struct resource) {
		.base = 0xefa0, .size = 0x20,
		.flags = IORESOURCE_IO | IORESOURCE_ASSIGNED | IORESOURCE_FIXED,
		.index = 0x20, .next = &resources[1],
	};
	resources[1] = (struct resource) {
		.base = 0xaeb8d000, .size = 0x00473000,
		.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED,
		.index = 0x10, .next = &resources[2],
	};
	resources[2] = (struct resource) {
		.base = 0xb0000000, .size = 0x10000000,
		.flags = IORESOURCE_MEM | IORESOURCE_PREFETCH | IORESOURCE_ASSIGNED,
		.index = 0x18, .next = &resources[3],
	};
	resources[3] = (struct resource) {
		.base = 0x3ffe8000000ULL, .size = 0x80000000,
		.flags = IORESOURCE_MEM | IORESOURCE_PREFETCH | IORESOURCE_ASSIGNED,
		.index = 0x344,
	};
	resources[4] = (struct resource) {
		.base = 0xfe02c000, .size = 0x1000,
		.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED | IORESOURCE_FIXED,
		.index = 0x10,
	};

	all_devices = &domain;
	*state = table;
	return 0;
}

static uint32_t record_crc32(const struct lb_payload_resource_handoff *handoff)
{
	const uint8_t *bytes = (const void *)handoff;
	const size_t crc_offset = offsetof(struct lb_payload_resource_handoff, crc32);
	uint32_t crc = 0;

	for (size_t index = 0; index < handoff->size; index++)
		crc = crc32_byte(crc, index >= crc_offset &&
					     index < crc_offset + sizeof(handoff->crc32) ?
					     0 : bytes[index]);
	return crc;
}

static void test_serializes_assigned_root(void **state)
{
	struct lb_header *header = *state;
	const struct lb_payload_resource_handoff *handoff;
	const struct lb_payload_resource_section *section;
	const struct lb_prh_pci_root_bridge *root;

	assert_int_equal(lb_add_payload_resource_handoff(header), CB_SUCCESS);
	assert_int_equal(header->table_entries, 1);
	handoff = (const void *)((const uint8_t *)header + sizeof(*header));
	assert_int_equal(handoff->tag, LB_TAG_PAYLOAD_RESOURCE_HANDOFF);
	assert_int_equal(handoff->revision, LB_PAYLOAD_RESOURCE_HANDOFF_REVISION);
	assert_int_equal(handoff->producer_generation, coreboot_version_timestamp);
	assert_int_equal(handoff->section_count, 1);
	assert_int_equal(record_crc32(handoff), handoff->crc32);

	section = handoff->sections;
	assert_int_equal(section->type, LB_PRH_SECTION_PCI_ROOT_BRIDGES);
	assert_int_equal(section->flags, LB_PRH_SECTION_FLAG_AUTHORITATIVE);
	assert_int_equal(section->entry_count, 1);
	root = (const void *)((const uint8_t *)handoff + section->offset);
	assert_int_equal(root->segment, 0);
	assert_int_equal(root->bus_start, 0);
	assert_int_equal(root->bus_end, 2);
	assert_int_equal(root->io_base, 0xefa0);
	assert_int_equal(root->io_length, 0x20);
	/* Overlapping Mem/PMem envelopes are combined for CDK2's combined aperture. */
	assert_int_equal(root->mem32_base, 0xaeb8d000);
	assert_int_equal(root->mem32_length, 0xfe02d000ULL - 0xaeb8d000ULL);
	assert_int_equal(root->pref_mem32_length, 0);
	assert_int_equal(root->pref_mem64_base, 0x3ffe8000000ULL);
	assert_int_equal(root->pref_mem64_length, 0x80000000);
}

static void test_rejects_overlapping_assignments_without_record(void **state)
{
	struct lb_header *header = *state;

	resources[4].base = resources[1].base;
	assert_int_equal(lb_add_payload_resource_handoff(header), CB_ERR);
	assert_int_equal(header->table_entries, 0);
}

static void test_rejects_resource_overflow_without_record(void **state)
{
	struct lb_header *header = *state;

	resources[3].base = UINT64_MAX - 0xff;
	resources[3].size = 0x1000;
	assert_int_equal(lb_add_payload_resource_handoff(header), CB_ERR);
	assert_int_equal(header->table_entries, 0);
}

static void test_rejects_root_outside_mcfg(void **state)
{
	struct lb_header *header = *state;

	domain_bus.subordinate = 256;
	assert_int_equal(lb_add_payload_resource_handoff(header), CB_ERR);
	assert_int_equal(header->table_entries, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_serializes_assigned_root, setup),
		cmocka_unit_test_setup(test_rejects_overlapping_assignments_without_record, setup),
		cmocka_unit_test_setup(test_rejects_resource_overflow_without_record, setup),
		cmocka_unit_test_setup(test_rejects_root_outside_mcfg, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
