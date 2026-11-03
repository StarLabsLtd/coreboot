/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <drivers/efi/efivars.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <efi_variable_service.h>
#include <fmap_config.h>
#include <intelblocks/fast_spi.h>
#include <soc/pci_devs.h>
#include <string.h>

#include "hob.h"
#include "private_store.h"
#include "capsule_store.h"

#include <Uefi/UefiBaseType.h>
#include <Uefi/UefiMultiPhase.h>

enum service_state { SERVICE_NOT_INITIALIZED, SERVICE_READY, SERVICE_FAILED };

struct mm_entry_context {
	uint32_t startup_this_ap;
	uint32_t current_cpu;
	uint32_t cpu_count;
	uint32_t save_state_size;
	uint32_t save_state;
} __packed;

static enum service_state current_state;
static struct efi_variable_service_state *service;
static bool registration_open;
static uintptr_t sealed_spi_bar;
static struct region_device variable_store_device;
static struct efi_private_store variable_store;
static uint8_t variable_store_buffer[CONFIG_SMMSTORE_BLOCK_SIZE];
static uint8_t capsule_readback[CONFIG_SMMSTORE_BLOCK_SIZE];

static uint32_t capsule_read(uint32_t offset, uint32_t size)
{
	if (!size || size > sizeof(variable_store_buffer) || offset >= CONFIG_ROM_SIZE ||
	    size > CONFIG_ROM_SIZE - offset)
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
#if CONFIG(SMMSTORE_SRK_PIN)
	if (offset < FMAP_SECTION_SRK_PIN_V1_START + FMAP_SECTION_SRK_PIN_V1_SIZE &&
	    offset + size > FMAP_SECTION_SRK_PIN_V1_START)
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
#endif
	return rdev_readat(boot_device_ro(), variable_store_buffer, offset, size) == size ?
		EFI_VARIABLE_SERVICE_RET_SUCCESS : EFI_VARIABLE_SERVICE_RET_FAILURE;
}

static uint32_t capsule_resolve(const char name[16], struct efi_capsule_range *range)
{
	char terminated[17];

	memcpy(terminated, name, 16);
	terminated[16] = 0;
	if (!strcmp(terminated, "BIOS")) {
		range->offset = fast_spi_get_bios_region(&range->size);
	} else if (!strcmp(terminated, "FMAP")) {
		range->offset = FMAP_SECTION_FMAP_START;
		range->size = FMAP_SECTION_FMAP_SIZE;
	} else {
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	}
	if (!range->size || range->offset >= CONFIG_ROM_SIZE ||
	    range->size > CONFIG_ROM_SIZE - range->offset)
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	return EFI_VARIABLE_SERVICE_RET_SUCCESS;
}

/* Private MM callback: there is deliberately no independent erase callback. */
static uint32_t capsule_program(uint32_t offset, const struct efi_capsule_range *ranges,
				uint32_t count, uint8_t *image, uint32_t check_only)
{
	bool allow_erase = true;

	if (!fast_spi_wpd_status())
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	_Static_assert(FMAP_SECTION_SMMSTORE_START % CONFIG_SMMSTORE_BLOCK_SIZE == 0 &&
		       FMAP_SECTION_SMMSTORE_SIZE % CONFIG_SMMSTORE_BLOCK_SIZE == 0,
		       "Authenticated variable store must occupy whole capsule erase blocks");
	/* Neither a signed BIOS fallback nor a public command may replace live keys/FTW. */
	if (offset >= FMAP_SECTION_SMMSTORE_START &&
	    offset < FMAP_SECTION_SMMSTORE_START + FMAP_SECTION_SMMSTORE_SIZE)
		return EFI_VARIABLE_SERVICE_RET_SUCCESS;
#if CONFIG(SMMSTORE_SRK_PIN)
	const size_t fmap_start = ALIGN_DOWN(FMAP_SECTION_FMAP_START, CONFIG_SMMSTORE_BLOCK_SIZE);
	const size_t fmap_end = ALIGN_UP(FMAP_SECTION_FMAP_START + FMAP_SECTION_FMAP_SIZE,
				       CONFIG_SMMSTORE_BLOCK_SIZE);
	/* The enrolled pin is platform state, never capsule-owned image data. */
	if (offset < FMAP_SECTION_SRK_PIN_V1_START + FMAP_SECTION_SRK_PIN_V1_SIZE &&
	    FMAP_SECTION_SRK_PIN_V1_START < (uint64_t)offset + CONFIG_SMMSTORE_BLOCK_SIZE)
		return EFI_VARIABLE_SERVICE_RET_SUCCESS;
	if (offset < fmap_end && fmap_start < (uint64_t)offset + CONFIG_SMMSTORE_BLOCK_SIZE)
		allow_erase = false;
#endif
	/* Preflight immutable blocks before the first destructive operation. */
	if (check_only && allow_erase)
		return EFI_VARIABLE_SERVICE_RET_SUCCESS;
	return efi_capsule_program_block(boot_device_rw(), offset, CONFIG_SMMSTORE_BLOCK_SIZE,
					ranges, count, image, capsule_readback, allow_erase) ?
		EFI_VARIABLE_SERVICE_RET_FAILURE : EFI_VARIABLE_SERVICE_RET_SUCCESS;
}

static int prepare_capsule_store(void)
{
	struct capsule_info_hob *info;

	if (!CONFIG(DRIVERS_EFI_UPDATE_CAPSULES))
		return 0;
	if (service->capsule_info < service->hob_base ||
	    service->capsule_info - service->hob_base > service->hob_size ||
	    service->hob_size - (service->capsule_info - service->hob_base) < sizeof(*info))
		return -1;
	info = (void *)(uintptr_t)service->capsule_info;
	if (info->revision != 1 || info->size != sizeof(*info) ||
	    info->flash_size != CONFIG_ROM_SIZE || info->block_size != CONFIG_SMMSTORE_BLOCK_SIZE)
		return -1;
	/* MM retains copied HOBs: resumed bindings must remain identical. */
	if (smm_efi_variable_service_is_s3_resume() &&
	    (info->read != (uintptr_t)capsule_read ||
	     info->resolve != (uintptr_t)capsule_resolve ||
	     info->program != (uintptr_t)capsule_program ||
	     info->buffer != (uintptr_t)variable_store_buffer ||
	     info->buffer_size != sizeof(variable_store_buffer)))
		return -1;
	info->read = (uintptr_t)capsule_read;
	info->resolve = (uintptr_t)capsule_resolve;
	info->program = (uintptr_t)capsule_program;
	info->buffer = (uintptr_t)variable_store_buffer;
	info->buffer_size = sizeof(variable_store_buffer);
	return 0;
}

/* These entry points are published only in the SMRAM-resident service HOB. */
static uint32_t private_read(uint32_t offset, uint32_t size)
{
	return efi_private_store_read(&variable_store, offset, variable_store_buffer, size) ?
		EFI_VARIABLE_SERVICE_RET_FAILURE : EFI_VARIABLE_SERVICE_RET_SUCCESS;
}

static uint32_t private_write(uint32_t offset, uint32_t size)
{
	if (!fast_spi_wpd_status())
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	return efi_private_store_write(&variable_store, offset, variable_store_buffer, size) ?
		EFI_VARIABLE_SERVICE_RET_FAILURE : EFI_VARIABLE_SERVICE_RET_SUCCESS;
}

static uint32_t private_erase(uint32_t offset, uint32_t size)
{
	if (!fast_spi_wpd_status())
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	return efi_private_store_erase(&variable_store, offset, size) ?
		EFI_VARIABLE_SERVICE_RET_FAILURE : EFI_VARIABLE_SERVICE_RET_SUCCESS;
}

static uint32_t private_validate(void)
{
	bool authenticated = false;
	enum cb_err ret;

	ret = efi_fv_validate_store(&variable_store_device, service->store_block_size,
				    &authenticated);
	if (ret == CB_SUCCESS && authenticated)
		return EFI_VARIABLE_SERVICE_RET_SUCCESS;
	if (ret == CB_SUCCESS || ret == CB_EFI_FVH_INVALID || ret == CB_EFI_CHECKSUM_INVALID ||
	    ret == CB_EFI_VS_NOT_FORMATTED_INVALID || ret == CB_EFI_VS_CORRUPTED_INVALID)
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	return EFI_VARIABLE_SERVICE_RET_IO_ERROR;
}

static int prepare_private_store(void)
{
	struct service_info_hob *info;
	struct region region;
	uintptr_t buffer_base;
	size_t buffer_size;

	if (service->service_info < service->hob_base ||
	    service->service_info - service->hob_base > service->hob_size ||
	    service->hob_size - (service->service_info - service->hob_base) < sizeof(*info))
		return -1;
	info = (void *)(uintptr_t)service->service_info;
	smm_get_efi_variable_service_buffer(&buffer_base, &buffer_size);
	if (info->buffer != buffer_base || info->buffer_size != buffer_size)
		return -1;
	if (info->revision != EFI_VARIABLE_SERVICE_INFO_REVISION || info->size != sizeof(*info) ||
	    service->store_block_size != CONFIG_SMMSTORE_BLOCK_SIZE ||
	    region_create_untrusted(&region, service->store_offset, service->store_size) ||
	    boot_device_rw_subregion(&region, &variable_store_device))
		return -1;

	if (efi_private_store_init(&variable_store, &variable_store_device,
				   service->store_block_size, (uintptr_t)variable_store_buffer,
				   sizeof(variable_store_buffer)))
		return -1;

	/* MM retains copied HOBs: resumed bindings must remain identical. */
	if (smm_efi_variable_service_is_s3_resume() &&
	    (info->store_read != (uintptr_t)private_read ||
	     info->store_write != (uintptr_t)private_write ||
	     info->store_erase != (uintptr_t)private_erase ||
	     info->store_validate != (uintptr_t)private_validate ||
	     info->store_buffer != (uintptr_t)variable_store_buffer ||
	     info->store_buffer_size != sizeof(variable_store_buffer)))
		return -1;
	info->store_read = (uintptr_t)private_read;
	info->store_write = (uintptr_t)private_write;
	info->store_erase = (uintptr_t)private_erase;
	info->store_validate = (uintptr_t)private_validate;
	info->store_buffer = (uintptr_t)variable_store_buffer;
	info->store_buffer_size = sizeof(variable_store_buffer);
	return 0;
}

static bool entry_in_core(uint32_t entry)
{
	return service && entry >= service->core_base &&
		entry - service->core_base < service->core_size;
}

static uint32_t register_mm_entry(uint32_t entry, uint32_t manage)
{
	if (!registration_open || service->dispatch_entry || service->manage_entry ||
	    !entry_in_core(entry) || !entry_in_core(manage))
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	service->dispatch_entry = entry;
	service->manage_entry = manage;
	registration_open = false;
	return EFI_VARIABLE_SERVICE_RET_SUCCESS;
}

static int get_service(void)
{
	uintptr_t base;
	size_t size;

	smm_get_efi_variable_service(&base, &size);
	if (!base || size != sizeof(*service))
		return -1;
	service = (void *)base;
	if (service->signature != EFI_VARIABLE_SERVICE_STATE_SIGNATURE ||
	    service->revision != EFI_VARIABLE_SERVICE_STATE_REVISION ||
	    service->size != sizeof(*service) || !entry_in_core(service->core_entry))
		return -1;
	return 0;
}

bool efi_variable_service_spi_bar_valid(uintptr_t bar)
{
	/* Before trusted finalization, PCI resource assignment may still change BAR0. */
	return !sealed_spi_bar || bar == sealed_spi_bar;
}

static int update_spi_bar(void)
{
	struct hob_resource *resource;
	uintptr_t smram_base;
	size_t smram_size;
	uint32_t bar;
	uint32_t offset;

	if (service->spi_mmio_resource < service->hob_base)
		return -1;
	offset = service->spi_mmio_resource - service->hob_base;
	if (offset > service->hob_size || service->hob_size - offset < sizeof(*resource))
		return -1;

	resource = (void *)(uintptr_t)service->spi_mmio_resource;
	if (resource->header.type != HOB_RESOURCE_DESCRIPTOR ||
	    resource->header.length != sizeof(*resource) ||
	    resource->type != RESOURCE_MEMORY_MAPPED_IO || resource->size != 0x1000)
		return -1;

	bar = pci_read_config32(PCH_DEV_SPI, PCI_BASE_ADDRESS_0);
	if (bar & PCI_BASE_ADDRESS_SPACE_IO)
		return -1;
	bar &= ~0xfff;
	if (!bar)
		return -1;
	smm_region(&smram_base, &smram_size);
	const struct region spi_region = region_create(bar, 0x1000);
	const struct region smram_region = region_create(smram_base, smram_size);
	if (region_overlap(&spi_region, &smram_region))
		return -1;
	if (smm_efi_variable_service_is_s3_resume() && resource->base != bar)
		return -1;

	sealed_spi_bar = bar;
	resource->base = bar;
	return 0;
}

static int initialize_service(void)
{
	struct service_info_hob *info;
	typedef void (*mm_core_entry)(const void *hobs);
	mm_core_entry entry;

	if (service->service_info < service->hob_base ||
	    service->service_info - service->hob_base > service->hob_size ||
	    service->hob_size - (service->service_info - service->hob_base) < sizeof(*info))
		return -1;
	info = (void *)(uintptr_t)service->service_info;
	if (info->revision != EFI_VARIABLE_SERVICE_INFO_REVISION ||
	    info->size != sizeof(*info) || info->register_mm_entry)
		return -1;

	registration_open = true;
	info->register_mm_entry = (uintptr_t)register_mm_entry;
	entry = (void *)(uintptr_t)service->core_entry;
	entry((void *)(uintptr_t)service->hob_base);
	info->register_mm_entry = 0;
	registration_open = false;
	return entry_in_core(service->dispatch_entry) && entry_in_core(service->manage_entry) ?
		0 : -1;
}

struct mm_communication_header32 {
	EFI_GUID guid;
	uint32_t message_length;
	uint8_t data[];
} __packed;

struct smm_variable_header32 {
	uint32_t function;
	uint32_t return_status;
	uint8_t data[];
} __packed;

struct smm_variable_access32 {
	EFI_GUID guid;
	uint32_t data_size;
	uint32_t name_size;
	uint32_t attributes;
	CHAR16 name[];
} __packed;

#define SMM_VARIABLE_FUNCTION_SET_VARIABLE 3

static const EFI_GUID efi_smm_variable_protocol_guid = {
	0xed32d533, 0x99e6, 0x4209, { 0x9c, 0xc0, 0x2d, 0x72, 0xcd, 0xd9, 0x98, 0xa7 }
};

static const EFI_GUID efi_coreboot_nv_data_guid = {
	0xceae4c1d, 0x335b, 0x4685, { 0xa4, 0xa0, 0xfc, 0x4a, 0x94, 0xee, 0xa0, 0x85 }
};

static const EFI_GUID efi_variable_service_ready_guid = {
	0xf4fc98d5, 0xf719, 0x4e49, { 0x88, 0xf4, 0x77, 0xcd, 0xcd, 0xb9, 0xfb, 0x55 }
};

_Static_assert(sizeof(struct mm_communication_header32) == 20,
	       "Unexpected MM communication header size");

_Static_assert(sizeof(struct smm_variable_header32) == 8,
	       "Unexpected MM variable header size");

_Static_assert(sizeof(struct smm_variable_access32) == 28,
	       "Unexpected MM variable access size");

static int dispatch_message(uintptr_t buffer_base, size_t buffer_size, size_t message_size)
{
	struct efi_mm_communication_status *status;
	uint32_t save_state_size = 0;
	uint32_t save_state = 0;
	struct mm_entry_context context = {
		.current_cpu = 0, .cpu_count = 1,
		.save_state_size = (uintptr_t)&save_state_size,
		.save_state = (uintptr_t)&save_state,
	};
	typedef void (*mm_entry)(const struct mm_entry_context *context);
	mm_entry entry;

	if (message_size > buffer_size - EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE)
		return -1;

	status = (void *)(buffer_base + sizeof(struct efi_variable_service_buffer));
	status->valid = 1;
	status->return_status = 0;
	status->return_buffer_size = message_size;
	entry = (void *)(uintptr_t)service->dispatch_entry;
	entry(&context);

	if (status->valid ||
	    status->return_buffer_size > buffer_size - EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE)
		return -1;

	return 0;
}

static int variable_service_probe(void)
{
	uintptr_t base;
	size_t size;
	struct mm_communication_header32 *message;
	struct efi_mm_communication_status *status;

	smm_get_efi_variable_service_buffer(&base, &size);
	if (!base || size != EFI_VARIABLE_SERVICE_BUFFER_SIZE)
		return -1;
	message = (void *)(base + EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE);
	message->guid = efi_variable_service_ready_guid;
	message->message_length = 1;
	message->data[0] = 0;
	if (dispatch_message(base, size, sizeof(*message) + 1))
		return -1;
	status = (void *)(base + sizeof(struct efi_variable_service_buffer));
	if (status->return_status)
		return -1;
	/* Probe the installed owner; MM keeps a private copy of its input HOBs. */
	if (CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)) {
		struct capsule_geometry {
			uint32_t revision, command, transaction;
			uint32_t flash_size, block_size, lowest_supported_version;
		} __packed;
		const EFI_GUID capsule_guid = {
			0x3e1d76c8, 0xeafd, 0x4619,
			{ 0xb5, 0x0b, 0xe7, 0x7b, 0xd1, 0x83, 0x9d, 0x9d }
		};
		struct capsule_geometry *geometry = (void *)message->data;
		const struct capsule_info_hob *info = (void *)(uintptr_t)service->capsule_info;

		_Static_assert(sizeof(*geometry) == 24, "Capsule INFO wire ABI");
		message->guid = capsule_guid;
		message->message_length = sizeof(*geometry);
		memset(geometry, 0, sizeof(*geometry));
		geometry->revision = 1;
		geometry->command = 1; /* COREBOOT_CAPSULE_INFO */
		if (dispatch_message(base, size, sizeof(*message) + sizeof(*geometry)) ||
		    status->return_status ||
		    status->return_buffer_size != sizeof(*message) + sizeof(*geometry) ||
		    message->message_length != sizeof(*geometry) ||
		    geometry->revision != 1 || geometry->command != 1 || geometry->transaction ||
		    geometry->flash_size != CONFIG_ROM_SIZE ||
		    geometry->block_size != CONFIG_SMMSTORE_BLOCK_SIZE ||
		    geometry->lowest_supported_version < info->lowest_supported_version)
			return -1;
	}
	return 0;
}

static int service_ready(void)
{
	if (current_state != SERVICE_NOT_INITIALIZED)
		return current_state == SERVICE_READY ? 0 : -1;
	current_state = SERVICE_FAILED;
	if (get_service())
		return -1;
	if (update_spi_bar())
		return -1;
	if (prepare_private_store())
		return -1;
	if (prepare_capsule_store())
		return -1;

	if (!smm_efi_variable_service_is_s3_resume()) {
		service->dispatch_entry = 0;
		service->manage_entry = 0;
		if (initialize_service())
			return -1;
	} else if (!entry_in_core(service->dispatch_entry) ||
		   !entry_in_core(service->manage_entry)) {
		return -1;
	}
	if (variable_service_probe())
		return -1;

	current_state = SERVICE_READY;
	return 0;
}

int efi_variable_service_finalize(void)
{
	return service_ready();
}

uint32_t efi_variable_service_communicate(void)
{
	uintptr_t buffer_base;
	size_t buffer_size;
	struct efi_variable_service_buffer *buffer;
	struct efi_mm_communication_status *status;
	struct mm_communication_header32 *message;

	if (current_state != SERVICE_READY || !service || !entry_in_core(service->dispatch_entry))
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	smm_get_efi_variable_service_buffer(&buffer_base, &buffer_size);
	if (!buffer_base || buffer_size != EFI_VARIABLE_SERVICE_BUFFER_SIZE)
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	buffer = (void *)buffer_base;
	status = (void *)(buffer_base + sizeof(*buffer));
	message = (void *)(buffer_base + EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE);
	if (buffer->signature != EFI_VARIABLE_SERVICE_BUFFER_SIGNATURE ||
	    buffer->revision != EFI_VARIABLE_SERVICE_REVISION ||
	    buffer->header_size != EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE ||
	    buffer->state != EFI_VARIABLE_SERVICE_BUFFER_REQUEST ||
	    buffer->message_size < sizeof(*message) ||
	    buffer->message_size > buffer_size - buffer->header_size ||
	    message->message_length > buffer->message_size - sizeof(*message))
		return EFI_VARIABLE_SERVICE_RET_FAILURE;

	if (dispatch_message(buffer_base, buffer_size, buffer->message_size))
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	buffer->status = status->return_status;
	buffer->response_size = status->return_buffer_size;
	__atomic_store_n(&buffer->state, EFI_VARIABLE_SERVICE_BUFFER_RESPONSE, __ATOMIC_RELEASE);
	return EFI_VARIABLE_SERVICE_RET_SUCCESS;
}

enum cb_err efi_variable_service_set_uint(const char *name, uint32_t value)
{
	union {
		uint64_t alignment;
		uint8_t bytes[256];
	} request;
	struct smm_variable_header32 *variable = (void *)request.bytes;
	struct smm_variable_access32 *access = (void *)variable->data;
	typedef uint32_t (__attribute__((cdecl)) *mm_manage)(const EFI_GUID *, const void *,
							    void *, size_t *);
	size_t max_name = (sizeof(request.bytes) - sizeof(*variable) - sizeof(*access) -
			   sizeof(value)) / sizeof(CHAR16);
	size_t name_length;
	size_t name_size;
	size_t request_size;
	mm_manage manage;

	if (!name || current_state != SERVICE_READY || !entry_in_core(service->manage_entry))
		return CB_EFI_ACCESS_ERROR;

	name_length = strnlen(name, max_name);
	if (name_length == max_name)
		return CB_ERR_ARG;
	name_size = (name_length + 1) * sizeof(CHAR16);
	request_size = sizeof(*variable) + sizeof(*access) + name_size + sizeof(value);

	memset(&request, 0, sizeof(request));
	variable->function = SMM_VARIABLE_FUNCTION_SET_VARIABLE;
	variable->return_status = UINT32_MAX;
	access->guid = efi_coreboot_nv_data_guid;
	access->data_size = sizeof(value);
	access->name_size = name_size;
	access->attributes = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
		EFI_VARIABLE_RUNTIME_ACCESS;
	for (size_t i = 0; i < name_length; i++)
		access->name[i] = (uint8_t)name[i];
	memcpy((uint8_t *)access->name + name_size, &value, sizeof(value));

	/* Dispatch a firmware-owned request through the untrusted variable handler. */
	manage = (void *)(uintptr_t)service->manage_entry;
	if (manage(&efi_smm_variable_protocol_guid, NULL, variable, &request_size) ||
	    request_size < sizeof(*variable) || request_size > sizeof(request.bytes) ||
	    variable->return_status)
		return CB_EFI_ACCESS_ERROR;

	return CB_SUCCESS;
}
