/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <boot/coreboot_tables.h>
#include <cbmem.h>
#include <commonlib/coreboot_tables.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <efi_variable_service.h>
#include <string.h>

#include <drivers/efi/efivars.h>
#include <fmap.h>
#include <smmstore.h>
#include <uuid.h>
#if CONFIG(VBOOT_SLOTS_RW_AB)
#include <security/vboot/misc.h>
#include <vb2_api.h>
#endif

static bool service_prepared;

#if !CONFIG(HAVE_PHOENIX_ASS)
uintptr_t efi_variable_service_ram_end(void)
{
	return (uintptr_t)cbmem_top();
}
#endif

static bool prepare_state(struct efi_variable_service_state *state, void *buffer)
{
	struct smmstore_params_info info;
	struct region store;
	uintptr_t smram_base;
	size_t smram_size;
	const uintptr_t ram_end = efi_variable_service_ram_end();

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
	*state = (struct efi_variable_service_state) {
		.signature = EFI_VARIABLE_SERVICE_STATE_SIGNATURE,
		.revision = EFI_VARIABLE_SERVICE_STATE_REVISION,
		.size = sizeof(*state),
		.store_offset = region_offset(&store),
		.store_size = region_sz(&store),
		.store_block_size = info.block_size,
		.mailbox = (uintptr_t)buffer,
		.capsule_ram_end = ram_end,
	};
#if CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)
	uint32_t version, floor;

	efi_fw_versions(&version, &floor);
	state->lowest_version = floor;
	if (parse_uuid(state->image_guid, CONFIG_DRIVERS_EFI_MAIN_FW_GUID))
		return false;
#if CONFIG(VBOOT_SLOTS_RW_AB)
	state->boot_slot = vboot_recovery_mode_enabled() ? 0 :
		(vboot_get_context()->flags & VB2_CONTEXT_FW_SLOT_B) ? 2 : 1;
#endif
#endif
	return true;
}

void efi_variable_service_load(struct smm_runtime *runtime)
{
	uintptr_t base;
	size_t region_size;
	struct efi_variable_service_state *state;
	const size_t buffer_size = EFI_VARIABLE_SERVICE_BUFFER_SIZE;
	void *buffer;
	const struct cbmem_entry *entry;

	service_prepared = false;
	if (smm_subregion(SMM_SUBREGION_EFI_VARIABLE_SERVICE, &base, &region_size) ||
	    !base || base > UINT32_MAX || region_size < EFI_VARIABLE_SERVICE_STATE_AREA_SIZE)
		return;
	state = (void *)base;
	_Static_assert(sizeof(*state) <= EFI_VARIABLE_SERVICE_STATE_AREA_SIZE,
		       "ASS metadata exceeds its reserved page");
	buffer = cbmem_add(CBMEM_ID_EFI_VARIABLE_SERVICE, buffer_size);
	entry = cbmem_entry_find(CBMEM_ID_EFI_VARIABLE_SERVICE);
	if (!buffer || !entry || cbmem_entry_size(entry) != buffer_size) {
		printk(BIOS_ERR, "EFI variable service communication buffer unavailable\n");
		return;
	}
	if (acpi_is_wakeup_s3()) {
		if (state->signature != EFI_VARIABLE_SERVICE_STATE_SIGNATURE ||
		    state->revision != EFI_VARIABLE_SERVICE_STATE_REVISION ||
		    state->size != sizeof(*state) ||
		    state->capsule_ram_end != efi_variable_service_ram_end() ||
		    state->mailbox != (uintptr_t)buffer) {
			printk(BIOS_ERR, "EFI variable service state is invalid after S3\n");
			return;
		}
	} else if (!prepare_state(state, buffer)) {
		printk(BIOS_ERR, "EFI variable service handoff failed\n");
		return;
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
