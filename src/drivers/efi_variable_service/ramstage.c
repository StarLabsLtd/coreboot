/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <boot/coreboot_tables.h>
#include <cbmem.h>
#include <cbfs.h>
#include <commonlib/coreboot_tables.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <efi_variable_service.h>
#include <string.h>

#include "ipl.h"
#include "hob.h"

#define SERVICE_STATE_AREA_SIZE	0x1000

static bool service_prepared;

static bool entry_in_core(const struct efi_variable_service_state *state, uint32_t entry)
{
	return entry >= state->core_base && entry - state->core_base < state->core_size;
}

void efi_variable_service_load(struct smm_runtime *runtime)
{
	uintptr_t base;
	size_t region_size;
	size_t fv_size;
	void *fv;
	struct efi_variable_service_state *state;
	struct efi_variable_service_image image;
	uintptr_t fv_base;
	uintptr_t core_base;
	uintptr_t hob_base;
	size_t core_space;
	const size_t buffer_size = EFI_VARIABLE_SERVICE_BUFFER_SIZE;
	void *buffer;
	const struct cbmem_entry *entry;
	const struct service_info_hob *info;

	service_prepared = false;
	if (smm_subregion(SMM_SUBREGION_EFI_VARIABLE_SERVICE, &base, &region_size) ||
	    !base || base > UINT32_MAX || region_size < SERVICE_STATE_AREA_SIZE)
		return;
	state = (void *)base;
	fv = cbfs_map("efi_variable_service_fv", &fv_size);
	if (!fv) {
		printk(BIOS_ERR, "EFI variable service FV is missing\n");
		return;
	}
	fv_base = ALIGN_UP(base + SERVICE_STATE_AREA_SIZE, 8);
	core_base = ALIGN_UP(fv_base + fv_size, 0x1000);
	if (core_base < fv_base || core_base > base + region_size) {
		printk(BIOS_ERR, "EFI variable service FV does not fit in SMRAM\n");
		goto out;
	}
	core_space = base + region_size - core_base;

	if (!acpi_is_wakeup_s3()) {
		memset((void *)base, 0, region_size);
		memcpy((void *)fv_base, fv, fv_size);
		if (efi_variable_service_load_mm_core((void *)fv_base, fv_size,
					      (void *)core_base, core_space, &image)) {
			printk(BIOS_ERR, "EFI variable service MM core failed to load\n");
			goto out;
		}

		hob_base = ALIGN_UP((uintptr_t)image.base + image.size, 8);
		buffer = cbmem_add(CBMEM_ID_EFI_VARIABLE_SERVICE, buffer_size);
		entry = cbmem_entry_find(CBMEM_ID_EFI_VARIABLE_SERVICE);
		*state = (struct efi_variable_service_state) {
			.signature = EFI_VARIABLE_SERVICE_STATE_SIGNATURE,
			.revision = EFI_VARIABLE_SERVICE_STATE_REVISION,
			.size = sizeof(*state),
			.fv_base = fv_base,
			.fv_size = fv_size,
			.core_base = (uintptr_t)image.base,
			.core_size = image.size,
			.core_entry = (uintptr_t)image.entry,
		};
		if (!buffer || !entry || cbmem_entry_size(entry) != buffer_size ||
		    efi_variable_service_build_hobs(hob_base, base + region_size,
				fv_base, fv_size, &image, (uintptr_t)buffer, buffer_size, state)) {
			printk(BIOS_ERR, "EFI variable service handoff failed\n");
			goto out;
		}
	} else if (state->signature != EFI_VARIABLE_SERVICE_STATE_SIGNATURE ||
		   state->revision != EFI_VARIABLE_SERVICE_STATE_REVISION ||
		   state->size != sizeof(*state) || state->fv_base != fv_base ||
		   state->fv_size != fv_size || state->core_base != core_base ||
		   state->core_size > core_space ||
		   !entry_in_core(state, state->core_entry) ||
		   !entry_in_core(state, state->dispatch_entry) ||
		   state->hob_base < state->core_base + state->core_size ||
		   state->hob_base > base + region_size ||
		   state->hob_size > base + region_size - state->hob_base ||
		   state->service_info < state->hob_base ||
		   state->service_info - state->hob_base > state->hob_size ||
		   state->hob_size - (state->service_info - state->hob_base) < sizeof(*info)) {
		printk(BIOS_ERR, "EFI variable service state is invalid after S3\n");
		goto out;
	}

	buffer = cbmem_add(CBMEM_ID_EFI_VARIABLE_SERVICE, buffer_size);
	entry = cbmem_entry_find(CBMEM_ID_EFI_VARIABLE_SERVICE);
	if (!buffer || !entry || cbmem_entry_size(entry) != buffer_size) {
		printk(BIOS_ERR, "EFI variable service communication buffer unavailable\n");
		goto out;
	}
	info = (void *)(uintptr_t)state->service_info;
	if (info->buffer != (uintptr_t)buffer || info->buffer_size != buffer_size) {
		printk(BIOS_ERR, "EFI variable service communication buffer changed\n");
		goto out;
	}
	struct efi_variable_service_buffer *header = buffer;
	struct efi_mm_communication_status *status =
		(void *)((uint8_t *)buffer + sizeof(*header));
	memset(buffer, 0, EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE);
	header->signature = EFI_VARIABLE_SERVICE_BUFFER_SIGNATURE;
	header->revision = EFI_VARIABLE_SERVICE_REVISION;
	header->header_size = EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE;
	header->state = EFI_VARIABLE_SERVICE_BUFFER_IDLE;
	status->return_buffer_size = EFI_VARIABLE_SERVICE_MESSAGE_CAPACITY;

	runtime->efi_variable_service_base = (uintptr_t)state;
	runtime->efi_variable_service_size = sizeof(*state);
	runtime->efi_variable_service_buffer = (uintptr_t)buffer;
	runtime->efi_variable_service_buffer_size = buffer_size;
	runtime->efi_variable_service_s3_resume = acpi_is_wakeup_s3();
	service_prepared = true;

out:
	cbfs_unmap(fv);
}

void lb_efi_variable_service(struct lb_header *header)
{
	const struct cbmem_entry *entry;

	if (!service_prepared)
		return;
	entry = cbmem_entry_find(CBMEM_ID_EFI_VARIABLE_SERVICE);
	if (!entry)
		return;

	struct lb_efi_variable_service *service = (void *)lb_new_record(header);
	service->tag = LB_TAG_EFI_VARIABLE_SERVICE;
	service->size = sizeof(*service);
	service->revision = EFI_VARIABLE_SERVICE_REVISION;
	service->apm_cmd = EFI_VARIABLE_SERVICE_APM_CMD;
	service->flags = 0;
	service->communication_buffer = (uintptr_t)cbmem_entry_start(entry);
	service->communication_buffer_size = cbmem_entry_size(entry);
}
