/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <crc_byte.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <string.h>

static uint8_t storage[4096];
static struct device domain, endpoint, second_endpoint;
static struct bus root_bus;
static struct resource bar;
static struct resource aperture, second_resource;
static struct lb_framebuffer framebuffer;
static int framebuffer_present;
static uint16_t command;
static int stuck;
static uint32_t pci_bars[6];
static const struct device *firmware_owned;
struct device *all_devices;

bool payload_resource_firmware_owned(const struct device *device)
{
	return device == firmware_owned;
}

uint32_t payload_resource_read_bar(const struct device *device, uint8_t bar_number)
{
	(void)device;
	return pci_bars[bar_number];
}

const struct lb_framebuffer *payload_resource_framebuffer(void)
{
	return framebuffer_present ? &framebuffer : NULL;
}

const struct lb_framebuffer *get_lb_framebuffer(void)
{
	return payload_resource_framebuffer();
}

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
	memset(&aperture, 0, sizeof(aperture));
	memset(&second_endpoint, 0, sizeof(second_endpoint));
	memset(&second_resource, 0, sizeof(second_resource));
	memset(&framebuffer, 0, sizeof(framebuffer));
	memset(pci_bars, 0, sizeof(pci_bars));
	firmware_owned = NULL;
	domain.enabled = 1;
	domain.path.type = DEVICE_PATH_DOMAIN;
	domain.downstream = &root_bus;
	domain.resource_list = &aperture;
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
	aperture.base = bar.base;
	aperture.size = bar.size;
	aperture.index = 0x1000;
	aperture.flags = IORESOURCE_MEM | IORESOURCE_FIXED | IORESOURCE_RESERVE;
	framebuffer = (struct lb_framebuffer) {
		.physical_address = bar.base,
		.x_resolution = 16,
		.y_resolution = 16,
		.bytes_per_line = 64,
		.bits_per_pixel = 32,
		.red_mask_pos = 16, .red_mask_size = 8,
		.green_mask_pos = 8, .green_mask_size = 8,
		.blue_mask_pos = 0, .blue_mask_size = 8,
		.reserved_mask_pos = 24, .reserved_mask_size = 8,
	};
	framebuffer_present = 0;
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
	uint8_t first_record[4096];
	uint32_t first_size;
	int failures = 0;

#define REJECT_FRAMEBUFFER(mutation, message) do { \
	reset_fixture(); \
	framebuffer_present = 1; \
	mutation; \
	failures += check(lb_add_payload_resource_handoff(header) == CB_ERR && \
		header->table_entries == 0 && (command & PCI_COMMAND_MASTER), message); \
} while (0)

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
	failures += check(handoff->size == sizeof(*handoff) +
		2 * sizeof(struct lb_payload_resource_section) +
		sizeof(struct lb_prh_pci_root_bridge) +
		sizeof(struct lb_prh_pci_assignment) && handoff->section_count == 2 &&
		handoff->sections[0].type == LB_PRH_SECTION_PCI_ROOT_BRIDGES &&
		handoff->sections[1].type == LB_PRH_SECTION_PCI_ASSIGNMENTS &&
		handoff->sections[0].offset == sizeof(*handoff) +
			2 * sizeof(struct lb_payload_resource_section) &&
		handoff->sections[1].offset == handoff->sections[0].offset +
			handoff->sections[0].length,
		"no-framebuffer record changed legacy layout");
	first_size = handoff->size;
	memcpy(first_record, handoff, first_size);

	reset_fixture();
	bar.flags |= IORESOURCE_FIXED;
	pci_bars[0] = PCI_BASE_ADDRESS_MEM_LIMIT_64;
	failures += check(lb_add_payload_resource_handoff(header) == CB_SUCCESS,
		"fixed live 64-bit BAR was rejected");
	handoff = (const void *)(storage + sizeof(*header));
	{
		const struct lb_prh_pci_assignment *assignment =
			(const void *)((const uint8_t *)handoff + handoff->sections[1].offset);

		failures += check(assignment->flags == LB_PRH_PCI_ASSIGNMENT_64BIT,
			"fixed live 64-bit BAR lost its encoding");
	}

	reset_fixture();
	failures += check(lb_add_payload_resource_handoff(header) == CB_SUCCESS &&
		((const struct lb_payload_resource_handoff *)(const void *)
		 (storage + sizeof(*header)))->size == first_size &&
		memcmp(first_record, storage + sizeof(*header), first_size) == 0,
		"no-framebuffer serialization is not deterministic");

	reset_fixture();
	framebuffer_present = 1;
	failures += check(lb_add_payload_resource_handoff(header) == CB_SUCCESS,
		"valid framebuffer was rejected");
	handoff = (const void *)(storage + sizeof(*header));
	failures += check(handoff->section_count == 4 &&
		handoff->sections[2].type == LB_PRH_SECTION_MEMORY_POLICY &&
		handoff->sections[3].type == LB_PRH_SECTION_FRAMEBUFFER &&
		handoff->sections[2].entry_count == 1 &&
		handoff->sections[3].entry_count == 1 &&
		record_crc(handoff) == handoff->crc32,
		"framebuffer ownership sections are malformed");
	{
		const struct lb_prh_memory_policy *memory =
			(const void *)((const uint8_t *)handoff + handoff->sections[2].offset);
		const struct lb_prh_framebuffer *output =
			(const void *)((const uint8_t *)handoff + handoff->sections[3].offset);

		failures += check(memory->base == framebuffer.physical_address &&
			memory->length == 1024 &&
			memory->gcd_type == LB_PRH_GCD_MEMORY_TYPE_MMIO &&
			memory->owner_flags == LB_PRH_MEMORY_GCD_AUTHORITATIVE &&
			memory->capabilities == 0 && memory->attributes == 0 &&
			memory->efi_memory_type == 0 && memory->reserved == 0 &&
			output->physical_address == framebuffer.physical_address &&
			output->size == 1024 &&
			output->x_resolution == framebuffer.x_resolution &&
			output->y_resolution == framebuffer.y_resolution &&
			output->bytes_per_line == framebuffer.bytes_per_line &&
			output->owner_flags ==
				(LB_PRH_FRAMEBUFFER_GEOMETRY_AUTHORITATIVE |
				 LB_PRH_FRAMEBUFFER_MEMORY_DELEGATED),
			"framebuffer ownership content mismatch");
	}
	failures += check(handoff->sections[2].flags ==
		LB_PRH_SECTION_FLAG_AUTHORITATIVE, "memory section flags mismatch");
	failures += check(handoff->sections[2].header_length == sizeof(handoff->sections[2]),
		"memory section header mismatch");
	failures += check(handoff->sections[2].entry_size == sizeof(struct lb_prh_memory_policy),
		"memory section entry mismatch");
	failures += check(handoff->sections[2].length == sizeof(struct lb_prh_memory_policy),
		"memory section length mismatch");
	failures += check(handoff->sections[3].flags == LB_PRH_SECTION_FLAG_AUTHORITATIVE,
		"framebuffer section flags mismatch");
	failures += check(handoff->sections[3].header_length == sizeof(handoff->sections[3]),
		"framebuffer section header mismatch");
	failures += check(handoff->sections[3].entry_size == sizeof(struct lb_prh_framebuffer),
		"framebuffer section entry mismatch");
	failures += check(handoff->sections[3].length == sizeof(struct lb_prh_framebuffer),
		"framebuffer section length mismatch");
	failures += check(handoff->sections[2].offset + handoff->sections[2].length ==
		handoff->sections[3].offset, "framebuffer section is not contiguous");
	failures += check(handoff->sections[3].offset + handoff->sections[3].length ==
		handoff->size, "framebuffer section does not end record");
	((uint8_t *)(void *)handoff)[handoff->sections[3].offset] ^= 1U;
	failures += check(record_crc(handoff) != handoff->crc32,
		"framebuffer record mutation retained a valid CRC");
	((uint8_t *)(void *)handoff)[handoff->sections[3].offset] ^= 1U;
	first_size = handoff->size;
	memcpy(first_record, handoff, first_size);
	reset_fixture();
	framebuffer_present = 1;
	failures += check(lb_add_payload_resource_handoff(header) == CB_SUCCESS &&
		memcmp(first_record, storage + sizeof(*header), first_size) == 0,
		"framebuffer serialization is not deterministic");

	reset_fixture();
	framebuffer_present = 1;
	framebuffer.bytes_per_line = 1;
	failures += check(lb_add_payload_resource_handoff(header) == CB_ERR &&
		header->table_entries == 0 && (command & PCI_COMMAND_MASTER),
		"invalid framebuffer mutated publication or device ownership");

	reset_fixture();
	framebuffer_present = 1;
	domain.resource_list = NULL;
	failures += check(lb_add_payload_resource_handoff(header) == CB_SUCCESS,
		"valid framebuffer without a redundant platform aperture was rejected");

	reset_fixture();
	framebuffer_present = 1;
	aperture.base += 512;
	aperture.size = 1024;
	failures += check(lb_add_payload_resource_handoff(header) == CB_ERR &&
		header->table_entries == 0 && (command & PCI_COMMAND_MASTER),
		"framebuffer BAR overlapping a reserved platform resource was published");

	reset_fixture();
	framebuffer_present = 1;
	framebuffer.bits_per_pixel = 16;
	framebuffer.bytes_per_line = 32;
	framebuffer.red_mask_pos = 11; framebuffer.red_mask_size = 5;
	framebuffer.green_mask_pos = 5; framebuffer.green_mask_size = 6;
	framebuffer.blue_mask_pos = 0; framebuffer.blue_mask_size = 5;
	framebuffer.reserved_mask_pos = 0; framebuffer.reserved_mask_size = 0;
	failures += check(lb_add_payload_resource_handoff(header) == CB_SUCCESS,
		"valid internal 16bpp framebuffer without reserved mask was rejected");

	REJECT_FRAMEBUFFER(framebuffer.x_resolution = 0, "zero framebuffer width accepted");
	REJECT_FRAMEBUFFER(framebuffer.y_resolution = 0, "zero framebuffer height accepted");
	REJECT_FRAMEBUFFER(framebuffer.bits_per_pixel = 0, "zero framebuffer bpp accepted");
	REJECT_FRAMEBUFFER(framebuffer.bits_per_pixel = 33, "oversized framebuffer bpp accepted");
	REJECT_FRAMEBUFFER(framebuffer.red_mask_size = 0, "missing red mask accepted");
	REJECT_FRAMEBUFFER(framebuffer.red_mask_pos = 31,
		"past-bpp framebuffer mask accepted");
	REJECT_FRAMEBUFFER(framebuffer.green_mask_pos = 16,
		"overlapping framebuffer masks accepted");
	REJECT_FRAMEBUFFER(framebuffer.reserved_mask_size = 0;
		framebuffer.reserved_mask_pos = 1, "absent mask with position accepted");
	REJECT_FRAMEBUFFER(framebuffer.orientation = LB_FB_ORIENTATION_BOTTOM_UP,
		"unsupported framebuffer orientation accepted");
	REJECT_FRAMEBUFFER(framebuffer.pad = 1, "nonzero framebuffer pad accepted");
	REJECT_FRAMEBUFFER(framebuffer.flags.reserved = 1,
		"reserved framebuffer flags accepted");
	REJECT_FRAMEBUFFER(framebuffer.physical_address = UINT64_MAX - 512,
		"wrapping framebuffer range accepted");
	REJECT_FRAMEBUFFER(framebuffer.x_resolution = UINT32_MAX;
		framebuffer.bytes_per_line = UINT32_MAX,
		"undersized rounded framebuffer stride accepted");
	REJECT_FRAMEBUFFER(bar.size = 512, "partial framebuffer BAR accepted");
	REJECT_FRAMEBUFFER(bar.flags &= ~IORESOURCE_ASSIGNED,
		"unassigned framebuffer BAR accepted");
	REJECT_FRAMEBUFFER(bar.flags &= ~IORESOURCE_STORED,
		"unstored framebuffer BAR accepted");
	REJECT_FRAMEBUFFER(bar.flags = IORESOURCE_IO | IORESOURCE_ASSIGNED |
		IORESOURCE_STORED, "I/O framebuffer BAR accepted");
	REJECT_FRAMEBUFFER(second_endpoint = endpoint;
		second_resource = bar;
		second_resource.index = PCI_BASE_ADDRESS_1;
		second_endpoint.path.pci.devfn = PCI_DEVFN(3, 0);
		second_endpoint.resource_list = &second_resource;
		endpoint.next = &second_endpoint,
		"duplicate framebuffer assignment accepted");

#undef REJECT_FRAMEBUFFER

	reset_fixture();
	bar.flags &= ~IORESOURCE_STORED;
	failures += check(lb_add_payload_resource_handoff(header) == CB_ERR &&
		header->table_entries == 0, "unstored assignment was published");

	reset_fixture();
	firmware_owned = &endpoint;
	failures += check(lb_add_payload_resource_handoff(header) == CB_ERR &&
		header->table_entries == 0, "firmware-owned assignment was published");

	reset_fixture();
	stuck = 1;
	failures += check(lb_add_payload_resource_handoff(header) == CB_ERR &&
		header->table_entries == 0, "failed bus-master readback published a record");

	return failures != 0;
}
