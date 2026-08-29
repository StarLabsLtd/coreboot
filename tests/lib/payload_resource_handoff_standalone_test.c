/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <crc_byte.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <string.h>

static uint8_t storage[4096];
static struct device domain, endpoint;
static struct bus root_bus;
static struct resource bar;
static uint16_t command;
static int stuck;
struct device *all_devices;

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

const char *dev_path(const struct device *device)
{
	(void)device;
	return "fixture";
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	struct lb_record *record = (void *)((uint8_t *)header + sizeof(*header));

	header->table_entries++;
	record->size = sizeof(*record);
	return record;
}

uint16_t payload_resource_read_command(const struct device *device)
{
	(void)device;
	return command | (stuck ? PCI_COMMAND_MASTER : 0);
}

void payload_resource_write_command(const struct device *device, uint16_t value)
{
	(void)device;
	command = value;
}

static uint32_t record_crc(const struct lb_payload_resource_handoff *handoff)
{
	const uint8_t *bytes = (const void *)handoff;
	const size_t offset = offsetof(struct lb_payload_resource_handoff, crc32);
	uint32_t crc = 0;

	for (size_t index = 0; index < handoff->size; index++)
		crc = crc32_byte(crc, index >= offset &&
			index < offset + sizeof(handoff->crc32) ? 0 : bytes[index]);
	return crc;
}

static void reset_fixture(void)
{
	memset(storage, 0, sizeof(storage));
	memset(&domain, 0, sizeof(domain));
	memset(&endpoint, 0, sizeof(endpoint));
	memset(&root_bus, 0, sizeof(root_bus));
	memset(&bar, 0, sizeof(bar));
	domain.enabled = 1;
	domain.path.type = DEVICE_PATH_DOMAIN;
	domain.downstream = &root_bus;
	domain.next = &endpoint;
	root_bus.dev = &domain;
	root_bus.subordinate = 0xff;
	endpoint.enabled = 1;
	endpoint.path.type = DEVICE_PATH_PCI;
	endpoint.path.pci.devfn = PCI_DEVFN(2, 0);
	endpoint.upstream = &root_bus;
	endpoint.resource_list = &bar;
	bar.base = 0x10000000;
	bar.size = 0x1000;
	bar.index = PCI_BASE_ADDRESS_0;
	bar.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED | IORESOURCE_STORED;
	all_devices = &domain;
	command = PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
	stuck = 0;
}

static int check(int condition, const char *message)
{
	(void)message;
	return !condition;
}

int main(void)
{
	struct lb_header *header = (void *)storage;
	const struct lb_payload_resource_handoff *handoff;
	int failures = 0;

	reset_fixture();
	failures += check(lb_add_payload_resource_handoff(header) == CB_SUCCESS,
		"valid assigned BAR rejected");
	handoff = (const void *)(storage + sizeof(*header));
	failures += check(handoff->producer_generation == 1 &&
		handoff->lifetime_flags == (LB_PRH_LIFETIME_COLD_BOOT |
		LB_PRH_LIFETIME_EXIT_BOOT_SERVICES), "provenance/lifetime mismatch");
	failures += check(handoff->section_count == 2 && handoff->sections[0].flags ==
		LB_PRH_SECTION_FLAG_AUTHORITATIVE && handoff->sections[1].flags ==
		LB_PRH_SECTION_FLAG_AUTHORITATIVE, "section ownership mismatch");
	failures += check(record_crc(handoff) == handoff->crc32,
		"record CRC mismatch");
	failures += check(!(command & PCI_COMMAND_MASTER),
		"bus mastering remained enabled");

	reset_fixture();
	bar.flags &= ~IORESOURCE_STORED;
	failures += check(lb_add_payload_resource_handoff(header) == CB_ERR &&
		header->table_entries == 0, "unstored assignment was published");

	reset_fixture();
	stuck = 1;
	failures += check(lb_add_payload_resource_handoff(header) == CB_ERR &&
		header->table_entries == 0, "failed bus-master readback published a record");

	return failures != 0;
}
