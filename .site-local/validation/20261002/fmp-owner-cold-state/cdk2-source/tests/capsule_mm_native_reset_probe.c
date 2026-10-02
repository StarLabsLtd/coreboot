/* SPDX-License-Identifier: GPL-2.0-only */

/* Reuse the actual Core/Variable/native transport component, not its test body. */
#define CDK2_NATIVE_SERVICE_PUBLIC_RAM
#define cdk2_native_stage_entry unused_service_stage_entry
#include "protected_variable_native_service_probe.c"
#undef cdk2_native_stage_entry
#include <cdk2/capsule_runtime.h>
#include <cdk2/capsule_delivery_policy.h>
#include <cdk2/reset_system.h>
#if defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
#include <cdk2/capsule_runtime_entry.h>
#include <cdk2/capsule_boot.h>
#endif

extern EFI_STATUS CDK2_MS_ABI cdk2_reset_system_entry(void *, void *);

typedef EFI_STATUS CDK2_MS_ABI allocate_pages_fn(UINT32, UINT32, UINTN, UINT64 *);

struct scatter {
	uint64_t length;
	uint64_t address;
};

static const struct {
	struct cdk2_capsule_header header;
	uint8_t bytes[36];
} retained = {
	.header = {
#if defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
		.guid = { 0x3b8c8162, 0x188c, 0x46a4,
			{ 0xae, 0xc9, 0xbe, 0x43, 0xf1, 0xd6, 0x56, 0x97 } },
#else
		.guid = { 0x6dcbd5ed, 0xe82d, 0x4c44,
			{ 0xbd, 0xa1, 0x71, 0x94, 0x19, 0x9a, 0xd9, 0x2a } },
#endif
		.header_size = sizeof(struct cdk2_capsule_header),
#if defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
		.flags = CDK2_CAPSULE_PERSIST | CDK2_CAPSULE_POPULATE | CDK2_CAPSULE_RESET,
#else
		.flags = 0x10000,
#endif
		.image_size = sizeof(retained),
	},
	.bytes = "actual MM warm-reset retained bytes",
};

#if !defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
static const EFI_GUID capsule_variable_guid = {
	0x711c703f, 0xc285, 0x4b10, {0xa3, 0xb0, 0x36, 0xec, 0xbd, 0x3c, 0x8b, 0xe2}
};
#endif

void cdk2_native_stage_entry(uint32_t parameter)
{
	struct cdk2_coreboot_handoff handoff;
	struct cdk2_authvar_endpoint endpoint;
	struct cdk2_native_context native = {0};
	struct cdk2_capsule_delivery_policy delivery;
	COREBOOT_TABLE_HOB table;
	void *hobs;
	EFI_HOB_GENERIC_HEADER *cursor;
	EFI_PHYSICAL_ADDRESS address = 0;
	struct scatter *scatter;
#if defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
	const struct cdk2_capsule_header *imported = NULL;
#else
	CHAR16 name[] = {'C','a','p','s','u','l','e','U','p','d','a','t','e','D','a','t','a',0};
#endif

	serial("CDK2_MM_RESET_BEGIN\r\n");
	if (EFI_ERROR(cdk2_coreboot_parse(parameter, &handoff)) ||
	    !actual_ram(&handoff, pool, sizeof(pool)) ||
	    !actual_ram(&handoff, hob_storage, sizeof(hob_storage)))
		goto fail;
	serial("CDK2_MM_RESET_ACTUAL_RAM_TABLE\r\n");
	if (EFI_ERROR(cdk2_coreboot_update_acpi_handoff(&handoff)) ||
	    EFI_ERROR(cdk2_coreboot_build_hobs(&handoff, hob_storage,
		hob_storage + sizeof(hob_storage), hob_storage,
		hob_storage + sizeof(hob_storage), FALSE, &hobs)))
		goto fail;
	serial("CDK2_MM_RESET_ACTUAL_HOBS\r\n");
	if (EFI_ERROR(cdk2_coreboot_append_acpi_hobs(hobs, &handoff)))
		goto fail;
	serial("CDK2_MM_RESET_ACTUAL_ACPI_HOB\r\n");
	if (EFI_ERROR(cdk2_coreboot_append_capsule_hobs(hobs, &handoff)))
		goto fail;
	serial("CDK2_MM_RESET_ACTUAL_CAPSULE_HOB_IMPORT\r\n");
	if (EFI_ERROR(cdk2_capsule_delivery_policy_from_hobs(hobs, &delivery)) ||
#if defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
	    delivery.allowed_transports != CB_CAPSULE_DELIVERY_RAM)
		goto fail;
	serial("CDK2_MM_UPDATE_ACTUAL_RAM_POLICY\r\n");
#else
	    delivery.allowed_transports != 0)
		goto fail;
	serial("CDK2_MM_RESET_RAM_DELIVERY_REMAINS_OFF\r\n");
#endif

	for (cursor = hobs; cursor->hob_type != EFI_HOB_TYPE_END_OF_HOB_LIST;
	     cursor = (void *)((uint8_t *)cursor + cursor->hob_length)) {
		if (cursor->hob_type == EFI_HOB_TYPE_UEFI_CAPSULE) {
			EFI_HOB_UEFI_CAPSULE *capsule = (void *)cursor;

			if (capsule->length != sizeof(retained) ||
			    memcmp((void *)(uintptr_t)capsule->base_address, &retained,
				sizeof(retained)))
				goto fail;
			serial("CDK2_MM_RESET_ACTUAL_CAPSULE_HOB_BYTES_PASS\r\n");
#if defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
			if (imported != NULL)
				goto fail;
			imported = (void *)(uintptr_t)capsule->base_address;
#else
			finish(true);
#endif
		}
	}

	table = (COREBOOT_TABLE_HOB) { .address = (uintptr_t)handoff.header,
		.size = handoff.table_size };
	if (EFI_ERROR(cdk2_coreboot_append_guid_hob(hobs, &table_guid, &table, sizeof(table))) ||
	    EFI_ERROR(cdk2_coreboot_append_authvar_mailbox_hob(hobs, &handoff)) ||
	    EFI_ERROR(cdk2_authvar_endpoint_from_hob(hobs, &endpoint)) ||
	    EFI_ERROR(cdk2_coreboot_initialize_floating_point(&native)) ||
	    EFI_ERROR(cdk2_coreboot_mask_legacy_interrupts(&native)) ||
	    EFI_ERROR(cdk2_dxe_core_initialize(&core, pool, sizeof(pool) / EFI_PAGE_SIZE)) ||
	    EFI_ERROR(security_stub_initialize(NULL, &core.runtime_storage->system_table)) ||
	    EFI_ERROR(((install_table_fn *)core.boot_services.install_configuration_table)(
		(EFI_GUID *)&hob_guid, hobs)))
		goto fail;
	serial("CDK2_MM_RESET_ACTUAL_CORE_READY\r\n");
#if CONFIG_CDK2_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPOSITION
	if (EFI_ERROR(cdk2_authvar_presence_lifecycle_close_composition_initialize(
	    hobs, &core, &core.runtime_storage->runtime_services)))
		goto fail;
#endif
	serial("CDK2_MM_RESET_ACTUAL_LIFECYCLE_READY\r\n");
	if (EFI_ERROR(cdk2_variable_runtime_entry(NULL, &core.runtime_storage->system_table)) ||
	    EFI_ERROR(cdk2_reset_system_entry(NULL, &core.runtime_storage->system_table)))
		goto fail;
	serial("CDK2_MM_RESET_ACTUAL_VARIABLE_RESET_READY\r\n");
#if defined(CDK2_MM_FMP_OWNER_CHECK)
	{
		const void *record;
		const struct lb_efi_fw_info *firmware;
		CHAR16 name[] = {'F', 'm', 'p', 'S', 't', 'a', 't', 'e', 0};
		EFI_GUID guid;
		UINT32 attributes = 0;
		UINTN bytes = 20;
		uint8_t state[20];
		const uint8_t initial[20] = {0};

		if (EFI_ERROR(cdk2_coreboot_find_unique_record(&handoff, CB_TAG_FW_INFO,
		    sizeof(*firmware), &record)))
			goto fail;
		firmware = (const void *)record;
		if (firmware->size != sizeof(*firmware))
			goto fail;
		memcpy(&guid, firmware->guid, sizeof(guid));
		memset(state, 0xff, sizeof(state));
		if (((get_fn *)core.runtime_storage->runtime_services.get_variable)(
		    name, &guid, &attributes, &bytes, state) != EFI_SUCCESS ||
		    attributes != 3 || bytes != sizeof(state) ||
		    memcmp(state, initial, sizeof(state)))
			goto fail;
		serial("CDK2_FMP_OWNER_ACTUAL_COMBINED_STATE_PASS\r\n");
		finish(true);
	}
#endif
#if defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
	if (EFI_ERROR(cdk2_capsule_runtime_entry(NULL,
	    (void *)&core.runtime_storage->system_table)))
		goto fail;
	serial("CDK2_MM_UPDATE_ACTUAL_CAPSULE_RUNTIME_READY\r\n");
	if (imported != NULL) {
		const EFI_GUID boot_guid = { 0x2890d42b, 0x7f9b, 0x4a66,
			{ 0x9a, 0x12, 0x54, 0x8d, 0x6b, 0x57, 0xd8, 0x20 } };
		struct cdk2_capsule_boot_protocol *boot = NULL;
		typedef EFI_STATUS CDK2_MS_ABI locate_fn(const EFI_GUID *, void *, void **);
		struct populated { UINT32 count, reserved; const void *capsules[1]; };
		bool found = false;

		if (((allocate_pages_fn *)core.boot_services.allocate_pages)(0, 4, 1,
		    &address) != EFI_SUCCESS ||
		    !actual_ram(&handoff, (void *)(uintptr_t)address, 4096))
			goto fail;
		scatter = (void *)(uintptr_t)address;
		memcpy(scatter + 2, imported, sizeof(retained));
		scatter[0] = (struct scatter) { sizeof(retained), (uintptr_t)(scatter + 2) };
		scatter[1] = (struct scatter) {0, 0};
		if (((locate_fn *)core.boot_services.locate_protocol)(&boot_guid, NULL,
		    (void **)&boot) != EFI_SUCCESS || boot == NULL ||
		    boot->process_scatter(boot, address) != EFI_SUCCESS)
			goto fail;
		serial("CDK2_MM_UPDATE_ACTUAL_BOOT_PROCESS_RETURNED\r\n");
		for (UINTN i = 0; i < core.runtime_storage->system_table.number_of_table_entries; i++) {
			const struct cdk2_dxe_configuration_table *item =
				&core.runtime_storage->configuration_tables[i];
			const struct populated *published = item->table;

			if (memcmp(&item->guid, &retained.header.guid, sizeof(EFI_GUID)))
				continue;
			if (found || published == NULL || published->count != 1 ||
			    published->reserved != 0 || published->capsules[0] == NULL ||
			    memcmp(published->capsules[0], &retained, sizeof(retained)))
				goto fail;
			found = true;
		}
		if (!found)
			goto fail;
		serial("CDK2_MM_UPDATE_ACTUAL_POPULATE_PASS\r\n");
		finish(true);
	}
#endif
	if (((allocate_pages_fn *)core.boot_services.allocate_pages)(0,
#if defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
	    4,
#else
	    0,
#endif
	    1,
	    &address) != EFI_SUCCESS || !actual_ram(&handoff, (void *)(uintptr_t)address, 4096))
		goto fail;
	scatter = (void *)(uintptr_t)address;
	memcpy(scatter + 2, &retained, sizeof(retained));
	scatter[0] = (struct scatter) { sizeof(retained), (uintptr_t)(scatter + 2) };
	scatter[1] = (struct scatter) {0, 0};
#if defined(CDK2_MM_UPDATE_CAPSULE_ROUNDTRIP)
	{
		const struct cdk2_capsule_header *capsules[] = { (void *)(scatter + 2) };
		typedef EFI_STATUS CDK2_MS_ABI query_fn(const struct cdk2_capsule_header *const *,
			UINTN, UINT64 *, UINT32 *);
		typedef EFI_STATUS CDK2_MS_ABI update_fn(const struct cdk2_capsule_header *const *,
			UINTN, UINT64);
		UINT64 maximum = 0;
		UINT32 reset_type = 0;

		if (((query_fn *)core.runtime_storage->runtime_services.query_capsule_capabilities)(
		    capsules, 1, &maximum, &reset_type) != EFI_SUCCESS ||
		    maximum < sizeof(retained) || reset_type != cdk2_reset_warm)
			goto fail;
		serial("CDK2_MM_UPDATE_ACTUAL_QUERY_PASS\r\n");
		serial("CDK2_MM_UPDATE_REAL_UPDATE_CAPSULE_INITIATE_RESET\r\n");
		((update_fn *)core.runtime_storage->runtime_services.update_capsule)(capsules, 1,
			address);
	}
#else
	if (((set_fn *)core.runtime_storage->runtime_services.set_variable)(name,
	    &capsule_variable_guid, 7, sizeof(address), &address) != EFI_SUCCESS)
		goto fail;
	serial("CDK2_MM_RESET_ACTUAL_MM_POINTER_WRITTEN\r\n");
	serial("CDK2_MM_RESET_REAL_RESET_SYSTEM_WARM\r\n");
	((cdk2_reset_fn)core.runtime_storage->runtime_services.reset_system)(
		cdk2_reset_warm, EFI_SUCCESS, 0, NULL);
#endif
fail:
	serial("CDK2_MM_RESET_FAIL\r\n");
	finish(false);
}
