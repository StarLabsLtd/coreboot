/* SPDX-License-Identifier: GPL-2.0-only */

#include "hob.h"

#include <commonlib/bsd/helpers.h>
#include <commonlib/region.h>
#include <boot/coreboot_tables.h>
#include <device/pci_def.h>
#include <device/pci_mmio_cfg.h>
#include <device/pci_ops.h>
#include <cpu/x86/smm.h>
#include <efi_variable_service.h>
#include <fmap.h>
#include <cbmem.h>
#include <smmstore.h>
#include <soc/iomap.h>
#include <soc/pci_devs.h>
#include <soc/pm.h>
#include <string.h>
#include <uuid.h>

#define HOB_HANDOFF			0x0001
#define HOB_MEMORY_ALLOCATION		0x0002
#define HOB_GUID_EXTENSION		0x0004
#define HOB_FIRMWARE_VOLUME		0x0005
#define HOB_END				0xffff
#define HOB_VERSION			0x0009

#define RESOURCE_SYSTEM_MEMORY		0
#define RESOURCE_PRESENT		BIT(0)
#define RESOURCE_INITIALIZED		BIT(1)
#define RESOURCE_TESTED			BIT(2)
#define RESOURCE_WRITE_BACK_CACHEABLE	BIT(13)

#define MMRAM_CACHEABLE			BIT(3)
#define MMRAM_ALLOCATED			BIT(4)
#define EFI_RUNTIME_SERVICES_CODE	5

#define ACPI_ADDRESS_SPACE_PCI_CONFIGURATION	2
#define ACPI_ACCESS_SIZE_DWORD			3
#define SPI_DISABLE_SMM_WRITE_PROTECT		BIT(0)

#define GUID(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
	{ (a), (b), (c), { (d0), (d1), (d2), (d3), (d4), (d5), (d6), (d7) } }

struct hob_handoff {
	struct hob_header header;
	uint32_t version;
	uint32_t boot_mode;
	uint64_t memory_top;
	uint64_t memory_bottom;
	uint64_t free_memory_top;
	uint64_t free_memory_bottom;
	uint64_t end;
} __packed;

struct hob_fv {
	struct hob_header header;
	uint64_t base;
	uint64_t size;
} __packed;

struct hob_module {
	struct hob_header header;
	struct efi_guid allocation_name;
	uint64_t base;
	uint64_t size;
	uint32_t memory_type;
	uint8_t reserved[4];
	struct efi_guid module_name;
	uint64_t entry;
} __packed;

struct hob_guid {
	struct hob_header header;
	struct efi_guid name;
} __packed;

struct mmram_descriptor {
	uint64_t physical_start;
	uint64_t cpu_start;
	uint64_t size;
	uint64_t state;
} __packed;

struct mmram_hob {
	struct hob_guid hob;
	uint32_t count;
	uint32_t reserved;
	struct mmram_descriptor descriptor[2];
} __packed;

struct mm_communication_hob {
	uint64_t physical_start;
	uint64_t number_of_pages;
	uint64_t status;
} __packed;

struct variable_store_hob {
	uint32_t revision;
	uint32_t store_offset;
	uint64_t store_base;
	uint32_t store_size;
	uint32_t block_size;
} __packed;

struct variable_flash_hob {
	uint32_t version;
	uint32_t reserved;
	uint64_t variable_base;
	uint64_t variable_size;
	uint64_t spare_base;
	uint64_t spare_size;
	uint64_t working_base;
	uint64_t working_size;
} __packed;

struct acpi_generic_address {
	uint8_t address_space_id;
	uint8_t register_bit_width;
	uint8_t register_bit_offset;
	uint8_t access_size;
	uint64_t address;
} __packed;

struct spi_flash_info_hob {
	uint8_t revision;
	uint8_t reserved;
	uint16_t flags;
	struct acpi_generic_address spi_address;
} __packed;

struct acpi_board_info_hob {
	uint8_t revision;
	uint8_t reserved[2];
	uint8_t reset_value;
	uint32_t alignment;
	uint64_t pm_event_base;
	uint64_t pm_gpe_enable_base;
	uint64_t pm_control_base;
	uint64_t pm_timer_base;
	uint64_t reset_address;
	uint64_t pcie_base;
	uint64_t pcie_size;
	uint8_t tpm2_present;
	uint8_t tpm12_present;
	uint8_t tail_padding[6];
} __packed;

struct hob_builder {
	uint8_t *current;
	uint8_t *end;
};

static const struct efi_guid zero_guid;
static const struct efi_guid module_allocation_guid =
	GUID(0xf8e21975, 0x0899, 0x4f58, 0xa4, 0xbe, 0x55, 0x25, 0xa9, 0xc6, 0xd7, 0x7a);
static const struct efi_guid mmram_guid =
	GUID(0x6dadf1d1, 0xd4cc, 0x4910, 0xbb, 0x6e, 0x82, 0xb1, 0xfd, 0x80, 0xff, 0x3d);
static const struct efi_guid communication_guid =
	GUID(0x6c2a2520, 0x0131, 0x4aee, 0xa7, 0x50, 0xcc, 0x38, 0x4a, 0xac, 0xe8, 0xc6);
static const struct efi_guid service_info_guid =
	GUID(0x0efdb114, 0x82fd, 0x4cf3, 0xb5, 0xc7, 0x43, 0xe5, 0x55, 0x23, 0xaf, 0xa4);
static const struct efi_guid variable_store_guid =
	GUID(0xc71135de, 0x0aef, 0x49bf, 0x90, 0xee, 0xea, 0x36, 0xb0, 0xd8, 0x53, 0xe5);
static const struct efi_guid variable_flash_guid =
	GUID(0x5d11c653, 0x8154, 0x4ac3, 0xa8, 0xc2, 0xfb, 0xa2, 0x89, 0x20, 0xfc, 0x90);
static const struct efi_guid spi_flash_info_guid =
	GUID(0x2d4aac1b, 0x91a5, 0x4cd5, 0x9b, 0x5c, 0xb4, 0x0f, 0x5d, 0x28, 0x51, 0xa1);
static const struct efi_guid acpi_board_info_guid =
	GUID(0x0ad3d31b, 0xb3d8, 0x4506, 0xae, 0x71, 0x2e, 0xf1, 0x10, 0x06, 0xd9, 0x0f);
static const struct efi_guid capsule_ram_guid =
	GUID(0xaeb3440c, 0xf957, 0x488b, 0x93, 0x50, 0x9a, 0xbc, 0xa6, 0x6a, 0x98, 0x1f);
static const struct efi_guid capsule_info_guid =
	GUID(0x787f43b2, 0x9a19, 0x43d1, 0x83, 0x64, 0x10, 0x79, 0xf1, 0xd4, 0x7e, 0x30);

_Static_assert(sizeof(struct hob_handoff) == 56, "Unexpected PHIT size");
_Static_assert(sizeof(struct hob_module) == 72, "Unexpected module HOB size");
_Static_assert(sizeof(struct mmram_descriptor) == 32, "Unexpected MMRAM descriptor size");
_Static_assert(sizeof(struct service_info_hob) == 48, "Unexpected service HOB size");
_Static_assert(offsetof(struct service_info_hob, store_read) == 24,
	       "Unexpected private store callback offset");
_Static_assert(offsetof(struct service_info_hob, store_buffer) == 40,
	       "Unexpected private store buffer offset");
_Static_assert(sizeof(struct variable_store_hob) == 24, "Unexpected store HOB size");
_Static_assert(sizeof(struct variable_flash_hob) == 56, "Unexpected flash HOB size");
_Static_assert(sizeof(struct spi_flash_info_hob) == 16, "Unexpected SPI HOB size");
_Static_assert(sizeof(struct acpi_board_info_hob) == 72, "Unexpected ACPI HOB size");
_Static_assert(offsetof(struct acpi_board_info_hob, pm_timer_base) == 32,
	       "Unexpected ACPI timer offset");

static void *append_hob(struct hob_builder *builder, uint16_t type, size_t size)
{
	struct hob_header *hob;
	size = ALIGN_UP(size, 8);
	if (size > UINT16_MAX || size > (size_t)(builder->end - builder->current))
		return NULL;
	hob = (void *)builder->current;
	memset(hob, 0, size);
	hob->type = type;
	hob->length = size;
	builder->current += size;
	return hob;
}

static void *append_guid_hob(struct hob_builder *builder, const struct efi_guid *guid,
			     size_t data_size)
{
	struct hob_guid *hob = append_hob(builder, HOB_GUID_EXTENSION, sizeof(*hob) + data_size);
	if (!hob)
		return NULL;
	hob->name = *guid;
	return hob + 1;
}

static int append_capsule_ram(struct hob_builder *builder)
{
	struct hob_resource *resource;
	uintptr_t smram_base;
	size_t smram_size;
	uintptr_t end = (uintptr_t)cbmem_top();

	/* CPU/SMM setup precedes PCI resource enumeration. Use the already trained
	 * low-DRAM boundary, not a resource map which is still being populated. */
	smm_region(&smram_base, &smram_size);
	if (!smram_size || end > smram_base || end <= 1 * MiB)
		return -1;
	resource = append_hob(builder, HOB_RESOURCE_DESCRIPTOR, sizeof(*resource));
	if (!resource)
		return -1;
	*resource = (struct hob_resource) {
		.header = { HOB_RESOURCE_DESCRIPTOR, sizeof(*resource), 0 },
		.owner = capsule_ram_guid, .type = RESOURCE_SYSTEM_MEMORY,
		.attributes = RESOURCE_PRESENT | RESOURCE_INITIALIZED | RESOURCE_TESTED |
			RESOURCE_WRITE_BACK_CACHEABLE,
		.base = 1 * MiB, .size = end - 1 * MiB,
	};
	return 0;
}

int efi_variable_service_build_hobs(uintptr_t hob_base, uintptr_t region_end,
				    uintptr_t fv_base, size_t fv_size,
				    const struct efi_variable_service_image *image,
				    uintptr_t buffer, size_t buffer_size,
				    struct efi_variable_service_state *state)
{
	struct hob_builder builder = { (void *)hob_base, (void *)region_end };
	struct hob_handoff *handoff;
	struct hob_resource *resource;
	struct hob_resource *spi_config_resource;
	struct hob_resource *spi_mmio_resource;
	struct hob_resource *store_resource;
	struct hob_fv *fv;
	struct hob_module *module;
	struct mmram_hob *mmram;
	struct mm_communication_hob *communication;
	struct service_info_hob *service;
	struct variable_store_hob *store_hob;
	struct variable_flash_hob *flash;
	struct spi_flash_info_hob *spi;
	struct acpi_board_info_hob *acpi;
	struct capsule_info_hob *capsule = NULL;
	struct smmstore_params_info info;
	struct region store;
	struct hob_header *end;
	uint64_t store_size, spare_size, variable_size;
	uintptr_t spi_config_base;
	uintptr_t heap_base;

	if (buffer_size != EFI_VARIABLE_SERVICE_BUFFER_SIZE ||
	    !IS_ALIGNED(buffer, 0x1000) || !IS_ALIGNED(buffer_size, 0x1000) ||
	    smmstore_get_info(&info) || fmap_locate_area("SMMSTORE", &store))
		return -1;
	store_size = (uint64_t)info.num_blocks * info.block_size;
	spare_size = (info.num_blocks / 2) * (uint64_t)info.block_size;
	if (store_size <= spare_size + info.block_size)
		return -1;
	if (region_sz(&store) != store_size || store_size > UINT32_MAX ||
	    region_offset(&store) > UINT32_MAX ||
	    !IS_ALIGNED(region_offset(&store), info.block_size))
		return -1;
	variable_size = store_size - spare_size - info.block_size;
	spi_config_base = (uintptr_t)pci_mmio_config32_addr(PCI_BDF(PCH_DEV_SPI), 0);

	handoff = append_hob(&builder, HOB_HANDOFF, sizeof(*handoff));
	resource = append_hob(&builder, HOB_RESOURCE_DESCRIPTOR, sizeof(*resource));
	spi_config_resource = append_hob(&builder, HOB_RESOURCE_DESCRIPTOR,
					  sizeof(*spi_config_resource));
	spi_mmio_resource = append_hob(&builder, HOB_RESOURCE_DESCRIPTOR,
					sizeof(*spi_mmio_resource));
	store_resource = append_hob(&builder, HOB_RESOURCE_DESCRIPTOR,
				    sizeof(*store_resource));
	fv = append_hob(&builder, HOB_FIRMWARE_VOLUME, sizeof(*fv));
	module = append_hob(&builder, HOB_MEMORY_ALLOCATION, sizeof(*module));
	mmram = append_hob(&builder, HOB_GUID_EXTENSION, sizeof(*mmram));
	communication = append_guid_hob(&builder, &communication_guid, sizeof(*communication));
	service = append_guid_hob(&builder, &service_info_guid, sizeof(*service));
	store_hob = append_guid_hob(&builder, &variable_store_guid, sizeof(*store_hob));
	flash = append_guid_hob(&builder, &variable_flash_guid, sizeof(*flash));
	spi = append_guid_hob(&builder, &spi_flash_info_guid, sizeof(*spi));
	acpi = append_guid_hob(&builder, &acpi_board_info_guid, sizeof(*acpi));
	if (CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)) {
		uint32_t version, lowest;
		capsule = append_guid_hob(&builder, &capsule_info_guid, sizeof(*capsule));
		if (!capsule)
			return -1;
		efi_fw_versions(&version, &lowest);
		*capsule = (struct capsule_info_hob) {
			.revision = 1, .size = sizeof(*capsule), .flash_size = CONFIG_ROM_SIZE,
			.block_size = CONFIG_SMMSTORE_BLOCK_SIZE,
			.lowest_supported_version = lowest,
		};
		if (parse_uuid(capsule->image_guid, CONFIG_DRIVERS_EFI_MAIN_FW_GUID))
			return -1;
	}
	if (CONFIG(DRIVERS_EFI_UPDATE_CAPSULES) && append_capsule_ram(&builder))
		return -1;
	end = append_hob(&builder, HOB_END, sizeof(*end));
	if (!handoff || !resource || !spi_config_resource || !spi_mmio_resource ||
	    !store_resource || !fv || !module || !mmram || !communication || !service ||
	    !store_hob || !flash || !spi || !acpi || !end)
		return -1;
	heap_base = ALIGN_UP((uintptr_t)builder.current, 0x1000);
	if (heap_base >= region_end)
		return -1;

	*handoff = (struct hob_handoff) {
		.header = { HOB_HANDOFF, sizeof(*handoff), 0 }, .version = HOB_VERSION,
		.memory_top = region_end, .memory_bottom = (uintptr_t)state,
		.free_memory_top = region_end, .free_memory_bottom = heap_base,
		.end = (uintptr_t)end,
	};
	*resource = (struct hob_resource) {
		.header = { HOB_RESOURCE_DESCRIPTOR, sizeof(*resource), 0 }, .owner = zero_guid,
		.type = RESOURCE_SYSTEM_MEMORY,
		.attributes = RESOURCE_PRESENT | RESOURCE_INITIALIZED | RESOURCE_TESTED |
			RESOURCE_WRITE_BACK_CACHEABLE,
		.base = buffer, .size = buffer_size,
	};
	*spi_config_resource = (struct hob_resource) {
		.header = { HOB_RESOURCE_DESCRIPTOR, sizeof(*spi_config_resource), 0 },
		.owner = zero_guid, .type = RESOURCE_MEMORY_MAPPED_IO,
		.attributes = RESOURCE_PRESENT | RESOURCE_INITIALIZED,
		.base = spi_config_base, .size = 0x1000,
	};
	*spi_mmio_resource = (struct hob_resource) {
		.header = { HOB_RESOURCE_DESCRIPTOR, sizeof(*spi_mmio_resource), 0 },
		.owner = zero_guid, .type = RESOURCE_MEMORY_MAPPED_IO,
		.attributes = RESOURCE_PRESENT | RESOURCE_INITIALIZED,
		/* PCI assigns this BAR later; SMM fills it before MM core initialization. */
		.base = 0, .size = 0x1000,
	};
	*store_resource = (struct hob_resource) {
		.header = { HOB_RESOURCE_DESCRIPTOR, sizeof(*store_resource), 0 },
		.owner = zero_guid, .type = RESOURCE_MEMORY_MAPPED_IO,
		.attributes = RESOURCE_PRESENT | RESOURCE_INITIALIZED,
		.base = info.mmap_addr, .size = store_size,
	};
	*fv = (struct hob_fv) {
		.header = { HOB_FIRMWARE_VOLUME, sizeof(*fv), 0 }, .base = fv_base, .size = fv_size,
	};
	*module = (struct hob_module) {
		.header = { HOB_MEMORY_ALLOCATION, sizeof(*module), 0 },
		.allocation_name = module_allocation_guid, .base = (uintptr_t)image->base,
		.size = image->size, .memory_type = EFI_RUNTIME_SERVICES_CODE,
		.entry = (uintptr_t)image->entry,
	};
	memcpy(&module->module_name, image->file_guid, sizeof(module->module_name));
	mmram->hob.header = (struct hob_header) { HOB_GUID_EXTENSION, sizeof(*mmram), 0 };
	mmram->hob.name = mmram_guid;
	mmram->count = 2;
	mmram->descriptor[0] = (struct mmram_descriptor) {
		.physical_start = (uintptr_t)state, .cpu_start = (uintptr_t)state,
		.size = heap_base - (uintptr_t)state, .state = MMRAM_CACHEABLE | MMRAM_ALLOCATED,
	};
	mmram->descriptor[1] = (struct mmram_descriptor) {
		.physical_start = heap_base, .cpu_start = heap_base,
		.size = region_end - heap_base, .state = MMRAM_CACHEABLE,
	};
	*communication = (struct mm_communication_hob) {
		.physical_start = buffer + EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE,
		.number_of_pages = (buffer_size - EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE) / 0x1000,
		.status = buffer + sizeof(struct efi_variable_service_buffer),
	};
	*service = (struct service_info_hob) {
		.revision = EFI_VARIABLE_SERVICE_INFO_REVISION, .size = sizeof(*service),
		.buffer = buffer,
		.buffer_size = buffer_size,
	};
	*store_hob = (struct variable_store_hob) {
		.revision = 1, .store_offset = region_offset(&store),
		.store_base = info.mmap_addr, .store_size = store_size, .block_size = info.block_size,
	};
	*flash = (struct variable_flash_hob) {
		.version = 1, .variable_base = info.mmap_addr, .variable_size = variable_size,
		.working_base = info.mmap_addr + variable_size, .working_size = info.block_size,
		.spare_base = info.mmap_addr + variable_size + info.block_size,
		.spare_size = spare_size,
	};
	*spi = (struct spi_flash_info_hob) {
		.flags = CONFIG(HAVE_INSMM_STS) ? SPI_DISABLE_SMM_WRITE_PROTECT : 0,
		.spi_address = {
			.address_space_id = ACPI_ADDRESS_SPACE_PCI_CONFIGURATION,
			.register_bit_width = 32,
			.access_size = ACPI_ACCESS_SIZE_DWORD,
			.address = (uintptr_t)pci_mmio_config32_addr(PCI_BDF(PCH_DEV_SPI), 0),
		},
	};
	*acpi = (struct acpi_board_info_hob) {
		.pm_timer_base = ACPI_BASE_ADDRESS + PM1_TMR,
	};

	state->hob_base = hob_base;
	state->hob_size = builder.current - (uint8_t *)hob_base;
	state->service_info = (uintptr_t)service;
	state->spi_mmio_resource = (uintptr_t)spi_mmio_resource;
	state->store_offset = region_offset(&store);
	state->store_size = store_size;
	state->store_block_size = info.block_size;
	state->capsule_info = (uintptr_t)capsule;
	return 0;
}
