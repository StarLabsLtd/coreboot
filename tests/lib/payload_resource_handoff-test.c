/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <boot/coreboot_tables.h>
#include <commonlib/bsd/ipchksum.h>
#include <crc_byte.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <string.h>

/* Exercise the real table allocator and checksums, including following records. */
#define main coreboot_main
#include "../../src/lib/coreboot_table.c"
#undef main

const char coreboot_version[] = "test";
const char coreboot_extra_version[] = "";
const char coreboot_build[] = "";
const char coreboot_compile_time[] = "";

struct device *all_devices;
static struct device domain, devices[4];
static struct bus bus;
static struct resource resources[4];
static uint16_t commands[4];
static bool command_locked[4];
static bool command_changes_after_read;
static unsigned int quiesce_calls;
static struct lb_framebuffer framebuffer;
static bool framebuffer_present;
static uint8_t table[4096] __aligned(16);
static struct lb_header *header;

/* PCI command registers and the global hardware quiesce are the mocked boundary. */
uint16_t payload_resource_read_command(const struct device *device)
{
	uint16_t command;

	assert_in_range(device - devices, 0, ARRAY_SIZE(devices) - 1);
	command = commands[device - devices];
	if (command_changes_after_read && device == &devices[0])
		commands[0] |= PCI_COMMAND_MASTER;
	return command;
}

void payload_resource_write_command(const struct device *device, uint16_t command)
{
	assert_in_range(device - devices, 0, ARRAY_SIZE(devices) - 1);
	if (!command_locked[device - devices])
		commands[device - devices] = command;
}

uint32_t payload_resource_read_bar(const struct device *device, uint8_t bar)
{
	assert_in_range(device - devices, 0, ARRAY_SIZE(devices) - 1);
	assert_int_equal(bar, 0);
	return resources[device - devices].base;
}

void pci_disable_bus_mastering(void)
{
	quiesce_calls++;
	for (size_t i = 0; i < ARRAY_SIZE(devices); i++) {
		if (devices[i].enabled && !devices[i].hidden)
			payload_resource_write_command(&devices[i], commands[i] & ~PCI_COMMAND_MASTER);
	}
}

const struct lb_framebuffer *payload_resource_framebuffer(void)
{
	return framebuffer_present ? &framebuffer : NULL;
}

static int setup(void **state)
{
	memset(devices, 0, sizeof(devices));
	memset(resources, 0, sizeof(resources));
	memset(command_locked, 0, sizeof(command_locked));
	memset(table, 0xa5, sizeof(table));
	quiesce_calls = 0;
	command_changes_after_read = false;
	framebuffer_present = false;
	domain = (struct device) {
		.path.type = DEVICE_PATH_DOMAIN,
		.enabled = true,
		.downstream = &bus,
		.next = devices,
	};
	bus = (struct bus) {
		.dev = &domain,
		.children = devices,
		.secondary = 0,
		.subordinate = 0,
	};
	for (size_t i = 0; i < ARRAY_SIZE(devices); i++) {
		devices[i] = (struct device) {
			.path.type = DEVICE_PATH_PCI,
			.path.pci.devfn = PCI_DEVFN(i + 1, 0),
			.enabled = i < 3,
			.upstream = &bus,
			.vendor = 0x1234,
			.device = 0x5678 + i,
			.hdr_type = PCI_HEADER_TYPE_NORMAL,
			.class = 0x020000,
			.resource_list = &resources[i],
		};
		resources[i] = (struct resource) {
			.index = PCI_BASE_ADDRESS_0,
			.base = 0x90000000 + i * MiB,
			.size = MiB,
			.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED | IORESOURCE_STORED,
		};
		commands[i] = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
		if (i + 1 < ARRAY_SIZE(devices)) {
			devices[i].next = &devices[i + 1];
			devices[i].sibling = &devices[i + 1];
		}
	}
	devices[0].class = 0x010802;
	devices[1].resource_list = NULL;
	devices[2].class = PCI_CLASS_DISPLAY_VGA << 8;
	framebuffer = (struct lb_framebuffer) {
		.physical_address = resources[2].base,
		.x_resolution = 64,
		.y_resolution = 64,
		.bytes_per_line = 256,
		.bits_per_pixel = 32,
		.red_mask_pos = 16,
		.red_mask_size = 8,
		.green_mask_pos = 8,
		.green_mask_size = 8,
		.blue_mask_size = 8,
		.reserved_mask_pos = 24,
		.reserved_mask_size = 8,
	};
	all_devices = &domain;
	header = lb_table_init((uintptr_t)table);
	return 0;
}

static struct lb_payload_resource_handoff *handoff(void)
{
	return (void *)(table + sizeof(*header));
}

static const struct lb_payload_resource_section *section(uint16_t type)
{
	const struct lb_payload_resource_handoff *record = handoff();

	for (size_t i = 0; i < record->section_count; i++) {
		const struct lb_payload_resource_section *entry = &record->sections[i];

		assert_true(entry->offset <= record->size);
		assert_true(entry->length <= record->size - entry->offset);
		if (entry->type == type)
			return entry;
	}
	fail_msg("Missing resource section %u", type);
	return NULL;
}

static void assert_rejected(void)
{
	assert_int_equal(lb_add_payload_resource_handoff(header), CB_ERR);
	assert_false(payload_resource_revision4_published());
	assert_int_equal(header->table_entries, 0);
	assert_int_equal(table[sizeof(*header)], 0xa5);
}

static void assert_checksums(void)
{
	uint8_t copy[sizeof(table)];
	struct lb_payload_resource_handoff *record = handoff();
	struct lb_payload_resource_handoff *copied = (void *)copy;

	assert_true(record->size <= sizeof(copy));
	memcpy(copy, record, record->size);
	copied->crc32 = 0;
	assert_int_equal(record->crc32, CRC(copy, record->size, crc32_byte));
	assert_int_equal(header->table_checksum,
		ipchksum(table + header->header_bytes, header->table_bytes));
	assert_int_equal(ipchksum(header, sizeof(*header)), 0);
}

static void test_native_inventory(void **state)
{
	assert_int_equal(lb_add_payload_resource_handoff(header), CB_SUCCESS);
	lb_table_fini(header);
	assert_true(payload_resource_revision4_published());
	assert_int_equal(handoff()->revision, LB_PAYLOAD_RESOURCE_HANDOFF_REVISION_4);
	assert_int_equal(payload_resource_revision4_boot_count(), 1);
	assert_true(payload_resource_revision4_boot_requester(0, PCI_DEVFN(1, 0)));
	assert_false(payload_resource_revision4_boot_requester(0, PCI_DEVFN(3, 0)));
	assert_int_equal(section(LB_PRH_SECTION_PCI_ASSIGNMENTS)->entry_count, 2);
	assert_int_equal(section(LB_PRH_SECTION_PCI_TOPOLOGY)->entry_count, 2);
	assert_int_equal(quiesce_calls, 1);
	assert_false(commands[0] & PCI_COMMAND_MASTER);
	assert_false(commands[1] & PCI_COMMAND_MASTER);
	assert_checksums();
}

static void test_invalid_assignments(void **state)
{
	uint64_t base = resources[0].base;

	resources[0].base = UINT64_MAX - 1;
	assert_rejected();
	resources[0].base = CONFIG_ECAM_MMCONF_BASE_ADDRESS;
	assert_rejected();
	resources[0].base = resources[2].base;
	assert_rejected();
	resources[0].base = base;
	resources[0].flags &= ~IORESOURCE_STORED;
	assert_rejected();
}

static void test_missing_boot_assignment_and_invalid_header(void **state)
{
	devices[0].resource_list = NULL;
	assert_rejected();
	devices[0].resource_list = &resources[0];
	devices[0].hdr_type = PCI_HEADER_TYPE_BRIDGE;
	assert_rejected();
}

static void test_framebuffer_ownership(void **state)
{
	framebuffer_present = true;
	framebuffer.physical_address = resources[2].base + resources[2].size - 1;
	assert_rejected();
	framebuffer.physical_address = resources[2].base;
	framebuffer.green_mask_pos = framebuffer.red_mask_pos;
	assert_rejected();
	framebuffer.green_mask_pos = 8;
	assert_int_equal(lb_add_payload_resource_handoff(header), CB_SUCCESS);
	lb_table_fini(header);
	assert_int_equal(section(LB_PRH_SECTION_FRAMEBUFFER)->entry_count, 1);
	assert_checksums();
}

static void test_unassigned_dma_requester_must_quiesce(void **state)
{
	command_locked[1] = true;
	assert_rejected();
}

static void test_finalization_rejects_assigned_dma(void **state)
{
	assert_int_equal(lb_add_payload_resource_handoff(header), CB_SUCCESS);
	lb_table_fini(header);
	commands[0] |= PCI_COMMAND_MASTER;
	assert_int_equal(lb_finalize_payload_resource_handoff(), CB_ERR);
}

static void test_finalization_rejects_unassigned_dma(void **state)
{
	assert_int_equal(lb_add_payload_resource_handoff(header), CB_SUCCESS);
	lb_table_fini(header);
	commands[1] |= PCI_COMMAND_MASTER;
	assert_int_equal(lb_finalize_payload_resource_handoff(), CB_ERR);
}

static void test_finalization_allows_host_bridge_master(void **state)
{
	devices[1].class = PCI_CLASS_BRIDGE_HOST << 8;
	assert_int_equal(lb_add_payload_resource_handoff(header), CB_SUCCESS);
	lb_table_fini(header);
	commands[1] |= PCI_COMMAND_MASTER;
	assert_int_equal(lb_finalize_payload_resource_handoff(), CB_SUCCESS);
	assert_checksums();
}

static void test_finalization_never_publishes_active_command(void **state)
{
	const struct lb_payload_resource_section *topology;
	const struct lb_prh_pci_topology *entries;
	enum cb_err ret;

	assert_int_equal(lb_add_payload_resource_handoff(header), CB_SUCCESS);
	lb_table_fini(header);
	command_changes_after_read = true;
	ret = lb_finalize_payload_resource_handoff();
	/* An earlier clear snapshot or rejection is safe; publishing BME=1 is not. */
	if (ret != CB_SUCCESS) {
		assert_int_equal(ret, CB_ERR);
		return;
	}
	topology = section(LB_PRH_SECTION_PCI_TOPOLOGY);
	entries = (const void *)((uint8_t *)handoff() + topology->offset);
	for (size_t i = 0; i < topology->entry_count; i++)
		assert_false(entries[i].command & PCI_COMMAND_MASTER);
}

static void test_finalization_rejects_growth(void **state)
{
	uint8_t previous[sizeof(table)];

	assert_int_equal(lb_add_payload_resource_handoff(header), CB_SUCCESS);
	lb_table_fini(header);
	memcpy(previous, table, sizeof(previous));
	devices[3].enabled = true;
	commands[3] &= ~PCI_COMMAND_MASTER;
	assert_int_equal(lb_finalize_payload_resource_handoff(), CB_ERR);
	assert_memory_equal(previous, table, sizeof(table));
}

static void test_finalization_preserves_following_records(void **state)
{
	struct lb_console previous;
	const struct lb_console *console;
	size_t reserved_size;

	assert_int_equal(lb_add_payload_resource_handoff(header), CB_SUCCESS);
	reserved_size = handoff()->size;
	lb_add_console(LB_TAG_CONSOLE_SERIAL8250, header);
	console = (const void *)((uint8_t *)handoff() + reserved_size);
	previous = *console;
	lb_table_fini(header);
	/* A finalizer can remove a device from payload ownership. */
	devices[2].hidden = true;
	commands[2] |= PCI_COMMAND_MASTER;
	assert_int_equal(lb_finalize_payload_resource_handoff(), CB_SUCCESS);
	assert_int_equal(handoff()->size, reserved_size);
	assert_int_equal(section(LB_PRH_SECTION_PCI_ASSIGNMENTS)->entry_count, 1);
	assert_int_equal(section(LB_PRH_SECTION_PCI_TOPOLOGY)->entry_count, 1);
	assert_memory_equal(&previous, console, sizeof(previous));
	assert_int_equal(quiesce_calls, 1);
	assert_checksums();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_native_inventory, setup),
		cmocka_unit_test_setup(test_invalid_assignments, setup),
		cmocka_unit_test_setup(test_missing_boot_assignment_and_invalid_header, setup),
		cmocka_unit_test_setup(test_framebuffer_ownership, setup),
		cmocka_unit_test_setup(test_unassigned_dma_requester_must_quiesce, setup),
		cmocka_unit_test_setup(test_finalization_rejects_assigned_dma, setup),
		cmocka_unit_test_setup(test_finalization_rejects_unassigned_dma, setup),
		cmocka_unit_test_setup(test_finalization_allows_host_bridge_master, setup),
		cmocka_unit_test_setup(test_finalization_never_publishes_active_command, setup),
		cmocka_unit_test_setup(test_finalization_rejects_growth, setup),
		cmocka_unit_test_setup(test_finalization_preserves_following_records, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
