/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <boot/coreboot_tables.h>
#include <cbmem.h>
#include <cbfs.h>
#include <commonlib/coreboot_tables.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <efi_variable_service.h>
#include <stdlib.h>
#include <string.h>

#include "ipl.h"
#include "hob.h"

#ifdef ASS_NATIVE_LINK_CHECK
#include <drivers/efi/efivars.h>
#include <fmap.h>
#include <smmstore.h>
#include <uuid.h>
#if CONFIG(VBOOT_SLOTS_RW_AB)
#include <security/vboot/misc.h>
#include <vb2_api.h>
#endif
#endif

#define SERVICE_STATE_AREA_SIZE	0x1000

static bool service_prepared;

#ifdef ASS_NATIVE_LINK_CHECK
struct native_handoff {
	struct efi_variable_service_state state;
	struct hob_resource spi;
	struct service_info_hob store;
	struct capsule_vboot_hob capsule;
};

static bool prepare_native_handoff(struct native_handoff *handoff, void *buffer)
{
	struct smmstore_params_info info;
	struct region store;
	uintptr_t smram_base;
	size_t smram_size;
	uintptr_t ram_end = (uintptr_t)cbmem_top();

	if (CONFIG(SOC_AMD_PHOENIX_OPENSIL)) {
		struct efi_variable_service_platform platform;

		if (efi_variable_service_get_platform_info(&platform))
			return false;
		ram_end = region_offset(&platform.runtime_ram) + region_sz(&platform.runtime_ram);
	}
	smm_region(&smram_base, &smram_size);
	if (!smram_size || ram_end <= 1 * MiB || ram_end > smram_base ||
	    ram_end > UINT32_MAX || smmstore_get_info(&info) ||
	    fmap_locate_area("SMMSTORE", &store) || !info.block_size ||
	    (uint64_t)info.num_blocks * info.block_size != region_sz(&store) ||
	    region_offset(&store) > UINT32_MAX || region_sz(&store) > UINT32_MAX ||
	    !IS_ALIGNED(region_offset(&store), info.block_size))
		return false;
	if (CONFIG_SMMSTORE_BLOCK_SIZE == 0x1000) {
		struct mem_region_device mapped;
		size_t block_size;

		mem_region_device_ro_init(&mapped, (void *)(uintptr_t)info.mmap_addr,
			region_sz(&store));
		if (efi_fv_store_block_size(&mapped.rdev, info.block_size, &block_size) !=
		    CB_SUCCESS)
			return false;
		info.block_size = block_size;
	}
	*handoff = (struct native_handoff) {
		.state = {
			.signature = EFI_VARIABLE_SERVICE_STATE_SIGNATURE,
			.revision = EFI_VARIABLE_SERVICE_STATE_REVISION,
			.size = sizeof(handoff->state),
			.hob_base = (uintptr_t)&handoff->spi,
			.hob_size = sizeof(*handoff) - offsetof(struct native_handoff, spi),
			.service_info = (uintptr_t)&handoff->store,
			.spi_mmio_resource = CONFIG(SOC_AMD_PHOENIX_OPENSIL) ?
				0 : (uintptr_t)&handoff->spi,
			.store_offset = region_offset(&store), .store_size = region_sz(&store),
			.store_block_size = info.block_size, .capsule_ram_end = ram_end,
		},
		.spi = {
			.header = { HOB_RESOURCE_DESCRIPTOR, sizeof(handoff->spi), 0 },
			.type = RESOURCE_MEMORY_MAPPED_IO, .size = 0x1000,
		},
		.store = {
			.revision = EFI_VARIABLE_SERVICE_INFO_REVISION,
			.size = sizeof(handoff->store), .buffer = (uintptr_t)buffer,
			.buffer_size = EFI_VARIABLE_SERVICE_BUFFER_SIZE,
		},
	};
#if CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)
	{
		uint32_t version, lowest;

		efi_fw_versions(&version, &lowest);
		handoff->state.capsule_info = (uintptr_t)&handoff->capsule;
		handoff->capsule.base = (struct capsule_info_hob) {
			.revision = CONFIG(VBOOT_SLOTS_RW_AB) ? 2 : 1,
			.size = CONFIG(VBOOT_SLOTS_RW_AB) ?
				sizeof(handoff->capsule) : sizeof(handoff->capsule.base),
			.flash_size = CONFIG_ROM_SIZE, .block_size = EFI_CAPSULE_BLOCK_SIZE,
			.lowest_supported_version = lowest,
		};
		if (parse_uuid(handoff->capsule.base.image_guid, CONFIG_DRIVERS_EFI_MAIN_FW_GUID))
			return false;
#if CONFIG(VBOOT_SLOTS_RW_AB)
		handoff->capsule.boot_slot = vboot_recovery_mode_enabled() ? 0 :
			(vboot_get_context()->flags & VB2_CONTEXT_FW_SLOT_B) ? 2 : 1;
#endif
	}
#endif
	return true;
}
#endif

#ifndef ASS_NATIVE_LINK_CHECK
static bool entry_in_core(const struct efi_variable_service_state *state, uint32_t entry)
{
	return entry >= state->core_base && entry - state->core_base < state->core_size;
}

static void *allocate_fv(void *arg_unused, size_t size,
			 const union cbfs_mdata *mdata_unused)
{
	return malloc(size);
}
#endif

void efi_variable_service_load(struct smm_runtime *runtime)
{
	uintptr_t base;
	size_t region_size;
	struct efi_variable_service_state *state;
#ifndef ASS_NATIVE_LINK_CHECK
	void *fv = NULL;
	size_t fv_size;
	struct efi_variable_service_image image;
	uintptr_t fv_base;
	uintptr_t core_base;
	uintptr_t hob_base;
	size_t core_space;
	const struct service_info_hob *info;
#endif
	const size_t buffer_size = EFI_VARIABLE_SERVICE_BUFFER_SIZE;
	void *buffer;
	const struct cbmem_entry *entry;

	service_prepared = false;
	if (smm_subregion(SMM_SUBREGION_EFI_VARIABLE_SERVICE, &base, &region_size) ||
	    !base || base > UINT32_MAX || region_size < SERVICE_STATE_AREA_SIZE ||
	    region_size > (uint64_t)UINT32_MAX + 1 - base)
		return;
	state = (void *)base;
#ifdef ASS_NATIVE_LINK_CHECK
	_Static_assert(sizeof(struct native_handoff) <= SERVICE_STATE_AREA_SIZE,
		"Native ASS metadata exceeds its reserved page");
	buffer = cbmem_add(CBMEM_ID_EFI_VARIABLE_SERVICE, buffer_size);
	entry = cbmem_entry_find(CBMEM_ID_EFI_VARIABLE_SERVICE);
	if (!buffer || !entry || cbmem_entry_size(entry) != buffer_size) {
		printk(BIOS_ERR, "EFI variable service communication buffer unavailable\n");
		return;
	}
	if (acpi_is_wakeup_s3()) {
		const struct native_handoff *handoff = (const void *)base;

		if (state->signature != EFI_VARIABLE_SERVICE_STATE_SIGNATURE ||
		    state->revision != EFI_VARIABLE_SERVICE_STATE_REVISION ||
		    state->size != sizeof(*state) ||
		    state->capsule_ram_end != (uintptr_t)cbmem_top() ||
		    state->hob_base != (uintptr_t)&handoff->spi ||
		    state->hob_size != sizeof(*handoff) - offsetof(struct native_handoff, spi) ||
		    state->service_info != (uintptr_t)&handoff->store ||
		    state->spi_mmio_resource != (uintptr_t)&handoff->spi ||
		    handoff->store.buffer != (uintptr_t)buffer ||
		    handoff->store.buffer_size != buffer_size) {
			printk(BIOS_ERR, "EFI variable service state is invalid after S3\n");
			return;
		}
	} else if (!prepare_native_handoff((void *)base, buffer)) {
		printk(BIOS_ERR, "EFI variable service handoff failed\n");
		return;
	}
#else
	if (acpi_is_wakeup_s3()) {
		/* The FV and its layout are retained in SMRAM across S3. */
		fv_size = state->fv_size;
		if (!fv_size)
			return;
	} else {
		fv = cbfs_alloc("efi_variable_service_fv", allocate_fv, NULL, &fv_size);
		if (!fv) {
			printk(BIOS_ERR, "EFI variable service FV is missing\n");
			return;
		}
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
		   state->capsule_ram_end != (uintptr_t)cbmem_top() ||
		   state->fv_size != fv_size || state->core_base != core_base ||
		   state->core_size > core_space ||
		   !entry_in_core(state, state->core_entry) ||
		   !entry_in_core(state, state->dispatch_entry) ||
		   !entry_in_core(state, state->manage_entry) ||
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
	if (info->revision != EFI_VARIABLE_SERVICE_INFO_REVISION ||
	    info->buffer != (uintptr_t)buffer || info->buffer_size != buffer_size) {
		printk(BIOS_ERR, "EFI variable service communication buffer changed\n");
		goto out;
	}
#endif
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

#ifndef ASS_NATIVE_LINK_CHECK
out:
	free(fv);
#endif
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
