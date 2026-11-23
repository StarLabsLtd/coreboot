/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <drivers/efi/efivars.h>
#include <efi_variable_service.h>
#include <fmap_config.h>
#include <string.h>

#include "capsule_store.h"
#include "wire.h"
#include "native_capsule.h"
#include "native_capsule_source.h"
#include "native_smm.h"
#include "native_variables.h"
#if CONFIG(VBOOT_SLOTS_RW_AB)
#include <security/vboot/vbnv.h>
#include <vb2_api.h>
#include <2nvstorage.h>
#endif

#include <Uefi/UefiBaseType.h>
#include <Uefi/UefiMultiPhase.h>

enum service_state { SERVICE_NOT_INITIALIZED, SERVICE_READY, SERVICE_FAILED };

static enum service_state current_state;
static struct efi_variable_service_state *service;
static struct region_device variable_store_device;
static uint8_t variable_store_buffer[MAX_UNSAFE(CONFIG_SMMSTORE_BLOCK_SIZE, EFI_CAPSULE_BLOCK_SIZE)];
static uint8_t capsule_readback[EFI_CAPSULE_BLOCK_SIZE];
static void *native_arena;
static struct ass_mm_capsule native_capsule;

static bool write_enabled(void)
{
	return efi_variable_service_write_device() != NULL;
}

static uint32_t capsule_program(uint32_t offset, const struct efi_capsule_range *ranges,
				uint32_t count, uint8_t *image, uint32_t check_only)
{
	const struct region_device *flash = efi_variable_service_write_device();

	if (!flash || efi_variable_service_program_capsule(flash, offset, ranges, count,
	    image, capsule_readback, check_only))
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	return EFI_VARIABLE_SERVICE_RET_SUCCESS;
}

#if CONFIG(VBOOT_SLOTS_RW_AB)

static uint32_t capsule_commit(uint32_t slot)
{
	struct vb2_context ctx = { 0 };

	if (!write_enabled() || slot > 2 || (!slot && !service->boot_slot) ||
	    (slot && slot != (service->boot_slot == 1 ? 2 : 1)))
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	read_vbnv_flash(ctx.nvdata);
	if (vb2_nv_check_crc(&ctx))
		return EFI_VARIABLE_SERVICE_RET_FAILURE;

	if (slot) {
		/* Preserve the executing slot; give the completed image three tries. */
		vb2_nv_set(&ctx, VB2_NV_TRY_NEXT, slot - 1);
		vb2_nv_set(&ctx, VB2_NV_TRY_COUNT, 3);
	} else {
		/* Only the payload handoff can acknowledge this RW boot. */
		if (vb2_nv_get(&ctx, VB2_NV_FW_TRIED) != service->boot_slot - 1)
			return EFI_VARIABLE_SERVICE_RET_FAILURE;
		vb2_nv_set(&ctx, VB2_NV_FW_RESULT, VB2_FW_RESULT_SUCCESS);
		/* Do not cancel an inactive-slot trial armed during this boot. */
		if (vb2_nv_get(&ctx, VB2_NV_TRY_NEXT) == service->boot_slot - 1)
			vb2_nv_set(&ctx, VB2_NV_TRY_COUNT, 0);
	}
	return save_vbnv_flash(ctx.nvdata) ? EFI_VARIABLE_SERVICE_RET_FAILURE :
		EFI_VARIABLE_SERVICE_RET_SUCCESS;
}

static uint32_t native_capsule_commit(uint32_t slot)
{
	const uint32_t status = capsule_commit(slot);

	if (status && !slot)
		printk(BIOS_ERR, "Vboot: could not acknowledge the running firmware slot\n");
	return status;
}
#endif

static const EFI_GUID efi_coreboot_nv_data_guid = {
	0xceae4c1d, 0x335b, 0x4685, { 0xa4, 0xa0, 0xfc, 0x4a, 0x94, 0xee, 0xa0, 0x85 }
};

static int service_ready(void)
{
	uintptr_t arena, base, mailbox;
	size_t arena_size, size, mailbox_size;
	const struct region_device *flash;

	if (current_state != SERVICE_NOT_INITIALIZED)
		return current_state == SERVICE_READY ? 0 : -1;
	current_state = SERVICE_FAILED;
	smm_get_efi_variable_service(&base, &size);
	if (smm_subregion(SMM_SUBREGION_EFI_VARIABLE_SERVICE, &arena, &arena_size) ||
	    !base || base != arena || size != sizeof(*service) ||
	    arena_size <= EFI_VARIABLE_SERVICE_STATE_AREA_SIZE ||
	    arena_size > UINTPTR_MAX - arena)
		return -1;
	service = (void *)base;
	smm_get_efi_variable_service_buffer(&mailbox, &mailbox_size);
	if (service->signature != EFI_VARIABLE_SERVICE_STATE_SIGNATURE ||
	    service->revision != EFI_VARIABLE_SERVICE_STATE_REVISION ||
	    service->size != sizeof(*service) || !mailbox || service->mailbox != mailbox ||
	    mailbox_size != EFI_VARIABLE_SERVICE_BUFFER_SIZE ||
	    service->store_offset != FMAP_SECTION_SMMSTORE_START ||
	    service->store_size != FMAP_SECTION_SMMSTORE_SIZE ||
	    (service->store_block_size != CONFIG_SMMSTORE_BLOCK_SIZE &&
	     !(CONFIG_SMMSTORE_BLOCK_SIZE == 0x1000 && !smm_efi_variable_service_is_s3_resume() &&
	       service->store_block_size == 0x10000)) ||
	    efi_variable_service_bind_spi(service))
		return -1;
	flash = efi_variable_service_write_device();
	if (!flash || rdev_chain(&variable_store_device, flash, service->store_offset,
				 service->store_size))
		return -1;
	if (!smm_efi_variable_service_is_s3_resume() && CONFIG_SMMSTORE_BLOCK_SIZE == 0x1000) {
		int migrated = efi_fv_migration_recover(&variable_store_device,
			variable_store_buffer, sizeof(variable_store_buffer));

		if (migrated) {
			if (migrated > 0)
				printk(BIOS_INFO, "EFI variable store migration recovered\n");
			return migrated < 0 ? -1 : EFI_VARIABLE_SERVICE_REBOOT_REQUIRED;
		}
	}
	const struct ass_capsule_platform *platform = NULL;
	if (CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)) {
#if CONFIG(VBOOT_SLOTS_RW_AB)
		if (service->boot_slot > 2)
			return -1;
		platform = ass_native_smm_capsule_platform(service, capsule_program,
			service->boot_slot ? service->boot_slot : 3, native_capsule_commit);
#else
		platform = ass_native_smm_capsule_platform(service, capsule_program, 0, NULL);
#endif
		if (!platform)
			return -1;
	}
	const size_t metadata = EFI_VARIABLE_SERVICE_STATE_AREA_SIZE;
	void *owner_arena = (void *)(arena + metadata);
	const struct authvar_fixed_policy *fixed = NULL;
#if CONFIG(ASS_FIXED_KEYS)
	fixed = ass_native_fixed_policy();
	if (!fixed)
		return -1;
#endif
	if (smm_efi_variable_service_is_s3_resume()) {
		if (!ass_native_smm_resume(owner_arena, arena_size - metadata,
		    &variable_store_device, service->store_block_size, write_enabled, fixed))
			return -1;
	} else {
		bool restart;
		const bool secure_boot = !CONFIG(PAYLOAD_EDIIE) || CONFIG(EDIIE_SECURE_BOOT);

		if (ass_native_smm_open(owner_arena, arena_size - metadata,
		    &variable_store_device, service->store_block_size, 0x10000,
		    write_enabled, secure_boot, fixed, &restart))
			return -1;
		if (restart)
			return EFI_VARIABLE_SERVICE_REBOOT_REQUIRED;
	}
	native_arena = owner_arena;
	if (platform) {
		static struct ass_capsule_source source;
		struct ass_capsule_transaction *transaction = ass_native_smm_capsule_transaction(
			native_arena, platform, smm_efi_variable_service_is_s3_resume());
		if (!transaction)
			return -1;
		native_capsule = (struct ass_mm_capsule) {
			.transaction = transaction, .source = &source,
			.crypto = ass_native_smm_crypto(native_arena),
			.platform = platform, .image = variable_store_buffer,
		};
	}
	current_state = SERVICE_READY;
	return 0;
}

int efi_variable_service_finalize(void)
{
	int ret = service_ready();

	if (ret || CONFIG_SMMSTORE_BLOCK_SIZE != 0x1000 ||
	    smm_efi_variable_service_is_s3_resume() || service->store_block_size != 0x10000)
		return ret;
	/* FTW and the variable driver have now recovered and validated the old store. */
	current_state = SERVICE_FAILED;
	printk(BIOS_INFO, "Converting EFI variable store from 64 KiB to 4 KiB\n");
	ret = efi_fv_migrate_4k(&variable_store_device, variable_store_buffer,
			       sizeof(variable_store_buffer));
	return ret == 1 ? EFI_VARIABLE_SERVICE_REBOOT_REQUIRED : -1;
}

uint32_t efi_variable_service_communicate(void)
{
	uintptr_t buffer_base;
	size_t buffer_size;

	if (current_state != SERVICE_READY || !service)
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	smm_get_efi_variable_service_buffer(&buffer_base, &buffer_size);
	if (!buffer_base || buffer_size != EFI_VARIABLE_SERVICE_BUFFER_SIZE)
		return EFI_VARIABLE_SERVICE_RET_FAILURE;
	return ass_native_smm_communicate(native_arena, (void *)buffer_base, buffer_size,
		native_capsule.platform ? &native_capsule : NULL);
}

static enum cb_err boot_access_status(bool *allowed)
{
	return ass_native_smm_boot_access(native_arena, allowed) ? CB_SUCCESS : CB_EFI_ACCESS_ERROR;
}

bool efi_variable_service_boot_access_allowed(void)
{
	bool allowed;

	return boot_access_status(&allowed) == CB_SUCCESS && allowed;
}

enum cb_err efi_variable_service_get_uint(const char *name, uint32_t *value)
{
	union {
		uint64_t alignment;
		uint8_t bytes[256];
	} request;
	struct smm_variable_header32 *variable = (void *)request.bytes;
	struct smm_variable_access32 *access = (void *)variable->data;
	size_t max_name = (sizeof(request.bytes) - sizeof(*variable) - sizeof(*access) -
			   sizeof(*value)) / sizeof(CHAR16);
	size_t name_length, name_size, request_size;
	bool boot_access;

	if (!name || !value)
		return CB_ERR_ARG;
	/* Keep the flash reader before initialization and after boot admission closes. */
	if (current_state != SERVICE_READY)
		return CB_ERR_NOT_IMPLEMENTED;
	if (boot_access_status(&boot_access) != CB_SUCCESS)
		return CB_EFI_ACCESS_ERROR;
	if (!boot_access)
		return CB_ERR_NOT_IMPLEMENTED;

	name_length = strnlen(name, max_name);
	if (!name_length || name_length == max_name)
		return CB_ERR_NOT_IMPLEMENTED;
	name_size = (name_length + 1) * sizeof(CHAR16);
	request_size = sizeof(*variable) + sizeof(*access) + name_size + sizeof(*value);
	memset(&request, 0, sizeof(request));
	variable->function = SMM_VARIABLE_FUNCTION_GET_VARIABLE;
	variable->return_status = UINT32_MAX;
	access->guid = efi_coreboot_nv_data_guid;
	access->data_size = sizeof(*value);
	access->name_size = name_size;
	for (size_t i = 0; i < name_length; i++)
		access->name[i] = (uint8_t)name[i];

	if (!ass_native_smm_variable(native_arena, variable, request_size))
		return CB_EFI_ACCESS_ERROR;
	if (variable->function != SMM_VARIABLE_FUNCTION_GET_VARIABLE)
		return CB_EFI_ACCESS_ERROR;
	if (access->name_size != name_size ||
	    memcmp(&access->guid, &efi_coreboot_nv_data_guid, sizeof(access->guid)))
		return CB_EFI_ACCESS_ERROR;
	for (size_t i = 0; i <= name_length; i++) {
		if (access->name[i] != (uint8_t)name[i])
			return CB_EFI_ACCESS_ERROR;
	}
	if (variable->return_status == ASS_WIRE_NOT_FOUND)
		return CB_EFI_OPTION_NOT_FOUND;
	if (variable->return_status && variable->return_status != ASS_WIRE_BUFFER_TOO_SMALL)
		return CB_EFI_ACCESS_ERROR;
	if ((!variable->return_status && access->data_size > sizeof(*value)) ||
	    (variable->return_status == ASS_WIRE_BUFFER_TOO_SMALL &&
	     access->data_size <= sizeof(*value)))
		return CB_EFI_ACCESS_ERROR;
	/* The legacy reader sees only flash-backed variables, not volatile MM state. */
	if (!(access->attributes & EFI_VARIABLE_NON_VOLATILE))
		return CB_ERR_NOT_IMPLEMENTED;
	if (variable->return_status == ASS_WIRE_BUFFER_TOO_SMALL)
		return CB_EFI_BUFFER_TOO_SMALL;
	if (access->data_size != sizeof(*value))
		return CB_EFI_ACCESS_ERROR;
	memcpy(value, (uint8_t *)access->name + name_size, sizeof(*value));
	return CB_SUCCESS;
}

enum cb_err efi_variable_service_set_uint(const char *name, uint32_t value)
{
	union {
		uint64_t alignment;
		uint8_t bytes[256];
	} request;
	struct smm_variable_header32 *variable = (void *)request.bytes;
	struct smm_variable_access32 *access = (void *)variable->data;
	size_t max_name = (sizeof(request.bytes) - sizeof(*variable) - sizeof(*access) -
			   sizeof(value)) / sizeof(CHAR16);
	size_t name_length;
	size_t name_size;
	size_t request_size;

	if (!name || current_state != SERVICE_READY)
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
	if (!ass_native_smm_variable(native_arena, variable, request_size))
		return CB_EFI_ACCESS_ERROR;
	if (request_size < sizeof(*variable) || request_size > sizeof(request.bytes) ||
	    variable->return_status)
		return CB_EFI_ACCESS_ERROR;

	return CB_SUCCESS;
}
