/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <efi_variable_service.h>
#include <soc/pci_devs.h>
#include <string.h>

#include "hob.h"

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
	return status->return_status ? -1 : 0;
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

	if (service_ready() || !service || !entry_in_core(service->dispatch_entry))
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

	if (!name || service_ready() || !entry_in_core(service->manage_entry))
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
