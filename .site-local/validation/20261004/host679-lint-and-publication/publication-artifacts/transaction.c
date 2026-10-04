/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <cdk2/dxe_core.h>
#include <cdk2/config.h>
#if CONFIG_CDK2_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPOSITION
#include <cdk2/authvar_presence_lifecycle_close_composition.h>
#endif
#if !CONFIG_CDK2_LINEAR_BOOT
#error "DXE payload startup requires CONFIG_CDK2_LINEAR_BOOT=1"
#endif
#include <cdk2/capsule_runtime.h>
#include <cdk2/cpu_arch.h>
#include <cdk2/capsule_boot.h>
#include <cdk2/capsule_report.h>
#include <cdk2/coreboot_hob.h>
#include <cdk2/system_fmp_transport.h>
#include <cdk2/software_hash.h>
#include <cdk2/esrt_abi.h>
#include <cdk2/authvar_native_x86.h>
#include "../../boot/coreboot.h"
#include <cdk2/capsule_disk.h>
#include <cdk2/boot_logo.h>
#include <cdk2/con_splitter_entry.h>
#include <cdk2/diagnostic.h>
#include <cdk2/disk_io.h>
#include <cdk2/deadline.h>
#include <cdk2/dxe_lifecycle.h>
#include <cdk2/hob_payload.h>
#include <guid/deadline_tsc_info.h>
#include <cdk2/graphics_output_driver.h>
#include <cdk2/linear_boot.h>
#include <cdk2/linear_tcg2.h>
#include <cdk2/lvgl_ui.h>
#include <cdk2/pci_host_bridge.h>
#include <cdk2/pci_io_abi.h>
#include <cdk2/pci_topology_handoff.h>
#include <cdk2/partition.h>
#include <industry_standard/acpi.h>
#include <cdk2/tpm2_acpi_hob.h>
#include <cdk2/xhci_control.h>
#include <pi/firmware_file.h>
#include <pi/hob.h>
#include <protocol/tcg2.h>

#if CONFIG_CDK2_LINEAR_BOOT
#include "private_control.h"
#endif
#include "private_image.h"
#include "diagnostic.h"

#define CDK2_STRICT_DIRECT_RUNTIME CONFIG_CDK2_STRICT_DIRECT_RUNTIME

_Static_assert(sizeof(struct cdk2_runtime_services_view) ==
	sizeof(struct cdk2_dxe_runtime_services) &&
	offsetof(struct cdk2_runtime_services_view, get_time) ==
	offsetof(struct cdk2_dxe_runtime_services, get_time) &&
	offsetof(struct cdk2_runtime_services_view, get_variable) ==
	offsetof(struct cdk2_dxe_runtime_services, get_variable) &&
	offsetof(struct cdk2_runtime_services_view, set_variable) ==
	offsetof(struct cdk2_dxe_runtime_services, set_variable),
	"capsule report Runtime service view ABI");

typedef char dxe_tpm_probe_row_size_check[
	(sizeof(struct cdk2_dxe_configuration_table) ==
	 sizeof(struct cdk2_tpm2_config_table)) ? 1 : -1];
typedef char dxe_tpm_probe_row_guid_offset_check[
	(offsetof(struct cdk2_dxe_configuration_table, guid) ==
	 offsetof(struct cdk2_tpm2_config_table, guid)) ? 1 : -1];
typedef char dxe_tpm_probe_row_table_offset_check[
	(offsetof(struct cdk2_dxe_configuration_table, table) ==
	 offsetof(struct cdk2_tpm2_config_table, table)) ? 1 : -1];
static struct cdk2_dxe_core core;
#if CDK2_STRICT_DIRECT_RUNTIME
static void *linked_primary_handle;
#endif
#if CDK2_STRICT_DIRECT_RUNTIME || defined(CDK2_HOST_TEST)
static const EFI_GUID device_path_guid = { 0x09576e91, 0x6d3f, 0x11d2,
	{ 0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const struct {
	UINT8 type;
	UINT8 subtype;
	UINT16 length;
	EFI_GUID guid;
	UINT8 end[4];
} __packed linked_primary_path = {
	.type = 0x01,
	.subtype = 0x04,
	.length = 20U,
	.guid = { 0x11e3a725, 0x8b31, 0x4f83,
		{ 0x89, 0x7b, 0x15, 0x33, 0x0f, 0x47, 0x4c, 0xc2 } },
	.end = { 0x7f, 0xff, 0x04, 0x00 },
};
_Static_assert(sizeof(linked_primary_path) == 24U,
	"linked-primary device path ABI changed");

typedef EFI_STATUS CDK2_MS_ABI linked_handle_protocol_fn(void *,
	const EFI_GUID *, void **);
typedef EFI_STATUS CDK2_MS_ABI linked_locate_device_path_fn(const EFI_GUID *,
	void **, void **);
struct linked_loaded_image_view {
	UINT32 revision, padding;
	void *parent, *system, *device;
};

static EFI_STATUS validate_linked_primary_owner(void *cached_handle,
	linked_handle_protocol_fn *handle_protocol,
	linked_locate_device_path_fn *locate_device_path)
{
	void *installed_path = NULL;
	void *remaining = (void *)&linked_primary_path;
	void *resolved_handle = NULL;
	EFI_STATUS status;

	if (cached_handle == NULL || handle_protocol == NULL ||
	    locate_device_path == NULL)
		return EFI_INVALID_PARAMETER;
	status = handle_protocol(cached_handle, &device_path_guid,
		&installed_path);
	if (EFI_ERROR(status) || installed_path != &linked_primary_path ||
	    memcmp(installed_path, &linked_primary_path,
		sizeof(linked_primary_path)) != 0)
		return EFI_COMPROMISED_DATA;
	status = locate_device_path(&device_path_guid, &remaining,
		&resolved_handle);
	if (EFI_ERROR(status) || resolved_handle != cached_handle ||
	    remaining != (const UINT8 *)&linked_primary_path +
		sizeof(linked_primary_path) - sizeof(linked_primary_path.end))
		return EFI_COMPROMISED_DATA;
	return EFI_SUCCESS;
}

#ifdef CDK2_HOST_TEST
static EFI_STATUS validate_linked_loaded_owner(
	const struct linked_loaded_image_view *loaded, void *cached_handle)
{
	return loaded != NULL && cached_handle != NULL &&
		loaded->device == cached_handle ? EFI_SUCCESS :
		EFI_COMPROMISED_DATA;
}

EFI_STATUS cdk2_dxe_test_validate_linked_primary_owner(void *cached_handle,
	linked_handle_protocol_fn *handle_protocol,
	linked_locate_device_path_fn *locate_device_path)
{
	return validate_linked_primary_owner(cached_handle, handle_protocol,
		locate_device_path);
}

const void *cdk2_dxe_test_linked_primary_path(UINTN *size)
{
	if (size != NULL)
		*size = sizeof(linked_primary_path);
	return &linked_primary_path;
}

EFI_STATUS cdk2_dxe_test_validate_linked_loaded_owner(const void *loaded,
	void *cached_handle)
{
	return validate_linked_loaded_owner(loaded, cached_handle);
}
#endif
#endif
#if CONFIG_CDK2_LINEAR_BOOT
enum storage_backend { STORAGE_NONE, STORAGE_NVME, STORAGE_USB, STORAGE_AHCI };
struct staged_storage_provider {
	void *controller;
	void *image, *agent;
	BOOLEAN connected;
};
static enum storage_backend selected_storage_backend;
static void *selected_storage_controller;
static void *selected_storage_binding;
static void *selected_storage_agent;
static struct staged_storage_provider staged_nvme, staged_xhci, staged_ahci;
static struct staged_storage_provider staged_sata;
static void *selected_esp;
static CHAR16 selected_loader_path[CDK2_LINEAR_LOADER_PATH_CHARS];
static UINT32 selected_loader_size;
static CHAR16 *const linear_loader_paths[] = {
	L"\\EFI\\fedora\\shimx64.efi",
	L"\\EFI\\fedora\\grubx64.efi",
	L"\\EFI\\ubuntu\\shimx64.efi",
	L"\\EFI\\ubuntu\\grubx64.efi",
	L"\\EFI\\BOOT\\BOOTX64.EFI",
};
static UINT32 boot_device_path_generation;
static struct cdk2_capsule_list disk_capsules;
static struct cdk2_graphics_child adopted_display;
static struct cdk2_deadline usb_discovery_deadline;
static void *usb_deadline_controller;
#endif
static const EFI_GUID hob_list_guid = { 0x7739f24c, 0x93d7, 0x11d4,
	{ 0x9a, 0x3a, 0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
static const EFI_GUID dxe_services_guid = { 0x05ad34ba, 0x6f02, 0x4214,
	{ 0x95, 0x2e, 0x4d, 0xa0, 0x39, 0x8e, 0x2b, 0xb9 } };
#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
static const EFI_GUID bds_guid = { 0x665e3ff6, 0x46cc, 0x11d4,
	{ 0x9a, 0x38, 0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
#endif
static const EFI_GUID loaded_image_guid = { 0x5b1b31a1, 0x9562, 0x11d2,
	{ 0x8e, 0x3f, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
#if CONFIG_CDK2_LINEAR_BOOT
static const EFI_GUID deadline_protocol_guid = CDK2_DEADLINE_PROTOCOL_GUID;
static const EFI_GUID deadline_tsc_info_guid = CDK2_DEADLINE_TSC_INFO_GUID;
static const EFI_GUID simple_fs_protocol = { 0x964e5b22, 0x6459, 0x11d2,
	{ 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
#endif
static void *core_image_handle;
#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME
static BOOLEAN direct_storage_phase_test_entry;
#if CONFIG_CDK2_NATIVE_TCG2
static BOOLEAN direct_tpm_absent_test_entry;
#endif
#endif

#if CONFIG_CDK2_LINEAR_BOOT
struct required_driver {
	EFI_GUID guid;
	const CHAR8 *name;
};

static UINTN protocol_instance_count(const EFI_GUID *guid);
static void *find_protocol(const EFI_GUID *guid);
static BOOLEAN configuration_table_present(const EFI_GUID *guid);
static uint64_t dxe_linear_clock(void *context);

static const EFI_GUID variable_policy_protocol_guid = { 0x81d1675c, 0x86f6, 0x48df,
	{ 0xbd, 0x95, 0x9a, 0x6e, 0x4f, 0x09, 0x25, 0xc3 } };
static const EFI_GUID firmware_management_protocol_guid = { 0x86c77a67, 0x0b97, 0x4633,
	{ 0xa1, 0x87, 0x49, 0x10, 0x4d, 0x06, 0x85, 0xc7 } };
static const EFI_GUID esrt_management_protocol_guid = { 0xa340c064, 0x723c, 0x4a9c,
	{ 0xa4, 0xdd, 0xd5, 0xb4, 0x7a, 0x26, 0xfb, 0xb0 } };
static const EFI_GUID esrt_configuration_table_guid = { 0xb122a263, 0x3661, 0x4f68,
	{ 0x99, 0x29, 0x78, 0xf8, 0xb0, 0xd6, 0x21, 0x80 } };
static const EFI_GUID acpi_table_protocol_guid = { 0xffe06bdd, 0x6107, 0x46a6,
	{ 0x7b, 0xb2, 0x5a, 0x9c, 0x7e, 0xc5, 0x27, 0x5c } };

static const struct required_driver security_bootstrap[] = {
	{ { 0xf80697e9, 0x7fd6, 0x4665,
		{ 0x86, 0x46, 0x88, 0xe3, 0x3e, 0xf7, 0x1d, 0xfc } }, "SecurityStubDxe" },
};


#if CONFIG_CDK2_NATIVE_TCG2
static const struct required_driver measured_boot_drivers[] = {
	{ { 0xfdff263d, 0x5f68, 0x4591,
		{ 0x87, 0xba, 0xb7, 0x68, 0xf4, 0x45, 0xa9, 0xaf } }, "Tcg2Dxe" },
};

static const EFI_GUID tcg2_protocol_guid = { 0x607f766c, 0x7455, 0x42be,
	{ 0x93, 0x0b, 0xe4, 0xd7, 0x6d, 0xb2, 0x72, 0x0f } };
static const EFI_GUID tcg2_final_events_guid = { 0x1e2ed096, 0x30e2, 0x4254,
	{ 0xbd, 0x89, 0x86, 0x3b, 0xbe, 0xf8, 0x23, 0x25 } };

typedef char tcg2_protocol_abi_size[
	(sizeof(EFI_TCG2_PROTOCOL) == 7U * sizeof(void *)) ? 1 : -1];
typedef char tcg2_protocol_submit_offset[
	(offsetof(EFI_TCG2_PROTOCOL, submit_command) == 3U * sizeof(void *)) ? 1 : -1];

static EFI_STATUS require_linear_tcg2_publication(void)
{
	struct cdk2_linear_tcg2_publication view = { 0 };

	for (UINTN index = 0U; index < core.database.protocol_count; index++) {
		struct cdk2_dxe_protocol *record = &core.database.protocols[index];
		if (memcmp(&record->guid, &tcg2_protocol_guid,
		    sizeof(record->guid)) != 0)
			continue;
		view.protocol_count++;
		view.protocol = record->interface;
		view.interface_size = record->interface_size;
		view.interface_generation = record->interface_generation;
		view.interface_dynamic = record->interface_dynamic;
		view.owner_present = record->owner_image != NULL ||
			record->owner_generation != 0U;
		if (record->owner_image != NULL && record->owner_generation != 0U)
			for (UINTN image = 0U; image < core.images.count; image++) {
				struct cdk2_dxe_image *owner = &core.images.images[image];
				view.owner_valid = cdk2_linear_tcg2_static_owner_span(
					record->interface, record->owner_image,
					record->owner_generation, owner->handle, owner->base,
					owner->size, owner->load_generation,
					owner->protocol_pins, &view.protocol_span,
					&view.protocol_generation);
				if (view.owner_valid)
					break;
			}
	}
	if (view.interface_dynamic && view.protocol != NULL &&
	    EFI_ERROR(cdk2_dxe_memory_readable_span(&core.memory, view.protocol,
	    &view.protocol_span, &view.protocol_generation)))
		view.protocol_span = view.protocol_generation = 0U;
	for (UINTN index = 0U;
	     index < core.runtime_storage->system_table.number_of_table_entries;
	     index++) {
		struct cdk2_dxe_configuration_table *table =
			&core.runtime_storage->configuration_tables[index];
		if (memcmp(&table->guid, &tcg2_final_events_guid,
		    sizeof(table->guid)) == 0) {
			view.final_count++;
			view.final = table->table;
		}
	}
	if (view.final != NULL &&
	    EFI_ERROR(cdk2_dxe_memory_readable_span(&core.memory, view.final,
	    &view.final_span, &view.final_generation)))
		view.final_span = view.final_generation = 0U;
	return cdk2_linear_tcg2_validate(&view);
}

static EFI_STATUS classify_linear_tcg2_presence(EFI_STATUS probe_status,
	UINT64 tpm_base, UINTN protocol_count, BOOLEAN final_table_present,
	BOOLEAN *required)
{
	if (required == NULL)
		return EFI_INVALID_PARAMETER;
	*required = FALSE;
	if (probe_status == EFI_NOT_FOUND)
		return protocol_count == 0U && !final_table_present ? EFI_SUCCESS :
			EFI_COMPROMISED_DATA;
	if (EFI_ERROR(probe_status))
		return probe_status;
	if (tpm_base == 0U)
		return EFI_COMPROMISED_DATA;
	*required = TRUE;
	return EFI_SUCCESS;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_tcg2_presence(EFI_STATUS probe_status,
	UINT64 tpm_base, UINTN protocol_count, BOOLEAN final_table_present,
	BOOLEAN *required)
{
	return classify_linear_tcg2_presence(probe_status, tpm_base, protocol_count,
		final_table_present, required);
}
#endif
#endif

static const struct required_driver arch_drivers[] = {
	{ { 0x1a1e4886, 0x9517, 0x440e,
		{ 0x9f, 0xde, 0x3b, 0xe4, 0x4c, 0xee, 0x21, 0x36 } }, "CpuDxe" },
	{ { 0xb601f8c4, 0x43b7, 0x4784,
		{ 0x95, 0xb1, 0xf4, 0x22, 0x6c, 0xb4, 0x0c, 0xee } }, "RuntimeDxe" },
	{ { 0xa19b1fe7, 0xc1bc, 0x49f8,
		{ 0x87, 0x5f, 0x54, 0xa5, 0xd5, 0x42, 0x44, 0x3f } }, "CpuIo2Dxe" },
	{ { 0xc8339973, 0xa563, 0x4561,
		{ 0xb8, 0x58, 0xd8, 0x47, 0x6f, 0x9d, 0xef, 0xc4 } }, "Metronome" },
	{ { 0x4b28e4c7, 0xff36, 0x4e10,
		{ 0x93, 0xcf, 0xa8, 0x21, 0x59, 0xe7, 0x77, 0xc5 } }, "ResetSystemRuntimeDxe" },
	{ { 0x52fe8196, 0xf9de, 0x4d07,
		{ 0xb2, 0x2f, 0x51, 0xf7, 0x7a, 0x0e, 0x7c, 0x41 } }, "LocalApicTimerDxe" },
	{ { 0xf099d67f, 0x71ae, 0x4c36,
		{ 0xb2, 0xa3, 0xdc, 0xeb, 0x0e, 0xb2, 0xb7, 0xd8 } }, "WatchdogTimer" },
};


static const struct required_driver variable_drivers[] = {
#if !CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME
	{ { 0xa0402fca, 0x6b25, 0x4cea,
		{ 0xb7, 0xdd, 0xc0, 0x8f, 0x99, 0x71, 0x4b, 0x29 } }, "SmmStoreFvbRuntimeDxe" },
	{ { 0xfe5cea76, 0x4f72, 0x49e8,
		{ 0x98, 0x6f, 0x2c, 0xd8, 0x99, 0xdf, 0xfe, 0x5d } }, "FaultTolerantWriteDxe" },
#endif
	{ { 0xcbd2e4d5, 0x7068, 0x4ff5,
		{ 0xb4, 0x62, 0x98, 0x22, 0xb4, 0xad, 0x8d, 0x60 } }, "VariableRuntimeDxe" },
	{ { 0x378d7b65, 0x8da9, 0x4773,
		{ 0xb6, 0xe4, 0xa4, 0x78, 0x26, 0xa8, 0x33, 0xe1 } }, "PcRtc" },
	{ { 0xad608272, 0xd07f, 0x4964,
		{ 0x80, 0x1e, 0x7b, 0xd3, 0xb7, 0x88, 0x86, 0x52 } }, "MonotonicCounterRuntimeDxe" },
};

static const struct required_driver platform_table_drivers[] = {
	{ { 0x9622e42c, 0x8e38, 0x4a08,
		{ 0x9e, 0x8f, 0x54, 0xf7, 0x84, 0x65, 0x2f, 0x6b } }, "AcpiTableDxe" },
};

static const struct required_driver pci_root_drivers[] = {
	{ { 0x128fb770, 0x5e79, 0x4176,
		{ 0x9e, 0x51, 0x9b, 0xb2, 0x68, 0xa1, 0x7d, 0xd1 } },
		"PciHostBridgeDxe" },
};

static const struct required_driver pci_enumerate_drivers[] = {
	{ { 0x93b80004, 0x9fb3, 0x11d4,
		{ 0x9a, 0x3a, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }, "PciBusDxe" },
};

static const struct required_driver storage_controller_drivers[] = {
	{ { 0x5be3bdf4, 0x53cf, 0x46a3,
		{ 0xa6, 0xa9, 0x73, 0xc3, 0x4a, 0x6e, 0x5e, 0xe3 } },
		"NvmExpressDxe" },
};
static const struct required_driver ahci_controller_drivers[] = {
	{ { 0x820c59bb, 0x274c, 0x43b2,
		{ 0x83, 0xea, 0xda, 0xc6, 0x73, 0x03, 0x5a, 0x59 } }, "SataController" },
	{ { 0x5e523cb4, 0xd397, 0x4986,
		{ 0x87, 0xbd, 0xa6, 0xdd, 0x8b, 0x22, 0xf4, 0x55 } }, "AtaAtapiPassThruDxe" },
};
static const struct required_driver ata_bus_driver = {
	{ 0x19df145a, 0xb1d4, 0x453f,
		{ 0x85, 0x07, 0x38, 0x81, 0x66, 0x76, 0xd7, 0xf6 } }, "AtaBusDxe"
};
static const struct required_driver xhci_controller_driver = {
	{ 0xb7f50e91, 0xa759, 0x412c,
		{ 0xad, 0xe4, 0xdc, 0xd0, 0x3e, 0x7f, 0x7c, 0x28 } }, "XhciDxe"
};
static const struct required_driver usb_bus_driver = {
	{ 0x240612b7, 0xa063, 0x11d4,
		{ 0x9a, 0x3a, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }, "UsbBusDxe"
};
static const struct required_driver usb_mass_driver = {
	{ 0x9fb4b4a7, 0x42c0, 0x4bcd,
		{ 0x85, 0x40, 0x9b, 0xcc, 0x67, 0x11, 0xf8, 0x3e } },
		"UsbMassStorageDxe"
};

static const struct required_driver block_discovery_drivers[] = {
	{ { 0x6b38f7b4, 0xad98, 0x40e9,
		{ 0x90, 0x93, 0xac, 0xa2, 0xb5, 0xa2, 0x53, 0xc4 } }, "DiskIoDxe" },
	{ { 0x1fa1f39e, 0xfeff, 0x4aae,
		{ 0xbd, 0x7b, 0x38, 0xa0, 0x70, 0xa3, 0xb6, 0x09 } }, "PartitionDxe" },
};

static const struct required_driver filesystem_drivers[] = {
	{ { 0xcd3bafb6, 0x50fb, 0x4fe8,
		{ 0x8e, 0x4e, 0xab, 0x74, 0xd2, 0xc1, 0xa6, 0x00 } }, "EnglishDxe" },
	{ { 0x961578fe, 0xb6b7, 0x44c3,
		{ 0xaf, 0x35, 0x6b, 0xc7, 0x05, 0xcd, 0x2b, 0x1f } }, "Fat" },
};

static const struct required_driver capsule_disk_drivers[] = {
	{ { 0x42857f0a, 0x13f2, 0x4b21,
		{ 0x8a, 0x23, 0x53, 0xd3, 0xf7, 0x14, 0xb8, 0x40 } }, "CapsuleRuntimeDxe" },
#if CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE
#if !CONFIG_CDK2_NATIVE_QEMU_TEST_FMP && !CONFIG_CDK2_NATIVE_SYSTEM_FMP
#error "coreboot capsule profile requires exactly one FMP provider"
#endif
#if CONFIG_CDK2_NATIVE_QEMU_TEST_FMP
	{ { 0x18e6b8cc, 0x63d1, 0x447c,
		{ 0xb3, 0x2d, 0x3d, 0xbd, 0x51, 0xd3, 0x29, 0x22 } }, "QemuTestFmpDxe" },
#else
	{ { 0x975cd0e6, 0xc540, 0x4e2b,
		{ 0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d } }, "FmpDxe" },
#endif
	{ { 0x999bd818, 0x7df7, 0x4a9a,
		{ 0xa5, 0x02, 0x9b, 0x75, 0x03, 0x3e, 0x6a, 0x0f } }, "EsrtDxe" },
#endif
};

static const struct required_driver console_splitter_driver = {
	{ 0x408edcec, 0xcf6d, 0x477c,
		{ 0xa5, 0xa8, 0xb4, 0x84, 0x4e, 0x3d, 0xe2, 0x81 } }, "ConSplitterDxe"
};
#if CONFIG_CDK2_LINEAR_SETUP_HOTKEY
static const struct required_driver input_ui_drivers[] = {
	{ { 0x87f4dfc1, 0xa2f1, 0x4c90,
		{ 0x95, 0x2d, 0x4a, 0x88, 0x1e, 0xd9, 0x52, 0x31 } }, "LvglUiDxe" },
};
#endif

#if CONFIG_CDK2_NATIVE_USB_KEYBOARD || CDK2_STRICT_DIRECT_RUNTIME
static const struct required_driver usb_keyboard_driver = {
	{ 0x2d2e62cf, 0x9ecf, 0x43b7,
		{ 0x82, 0x19, 0x94, 0xe7, 0xfc, 0x71, 0x3d, 0xfe } }, "UsbKbDxe"
};
#endif

#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
static const struct required_driver boot_policy_drivers[] = {
	{ { 0x6d33944a, 0xec75, 0x4855,
		{ 0xa5, 0x4d, 0x80, 0x9c, 0x75, 0x24, 0x1f, 0x6c } }, "BdsDxe" },
};
#endif

static const EFI_GUID driver_binding_protocol = { 0x18a031ab, 0xb443, 0x4d1a,
	{ 0xa5, 0xc0, 0x0c, 0x09, 0x26, 0x1e, 0x9f, 0x71 } };
static const EFI_GUID pci_io_protocol = { 0x4cf5b200, 0x68b8, 0x4ca5,
	{ 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } };
static const EFI_GUID device_path_protocol = { 0x09576e91, 0x6d3f, 0x11d2,
	{ 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID graphics_hob_guid = { 0x39f62cce, 0x6825, 0x4669,
	{ 0xbb, 0x56, 0x54, 0x1a, 0xba, 0x75, 0x3a, 0x07 } };
static const EFI_GUID graphics_output_protocol = { 0x9042a9de, 0x23dc, 0x4a38,
	{ 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };
#if CONFIG_CDK2_LVGL_RENDERER
static const EFI_GUID lvgl_status_protocol_guid =
	CDK2_LVGL_STATUS_PROTOCOL_GUID;
#endif

struct acpi_table_protocol;
typedef EFI_STATUS CDK2_MS_ABI install_acpi_table_fn(
	struct acpi_table_protocol *this, const void *table, UINTN size,
	UINTN *key);
struct acpi_table_protocol {
	install_acpi_table_fn *install;
	void *uninstall;
};

struct boot_graphics_resource_table {
	EFI_ACPI_DESCRIPTION_HEADER header;
	UINT16 version;
	UINT8 status;
	UINT8 image_type;
	UINT64 image_address;
	UINT32 image_offset_x;
	UINT32 image_offset_y;
} __packed;
static const EFI_GUID simple_text_input_protocol = {
	0x387477c1, 0x69c7, 0x11d2,
	{ 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
static const EFI_GUID simple_text_output_protocol = {
	0x387477c2, 0x69c7, 0x11d2,
	{ 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b }
};
static const EFI_GUID simple_text_input_ex_protocol = {
	0xdd9e7534, 0x7762, 0x4698,
	{ 0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa }
};
static const EFI_GUID loader_service_protocols[] = {
	{ 0x26baccb1, 0x6f42, 0x11d4,
		{ 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } },
	{ 0x26baccb2, 0x6f42, 0x11d4,
		{ 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } },
	{ 0x26baccb3, 0x6f42, 0x11d4,
		{ 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } },
	{ 0xb7dfb4e1, 0x052f, 0x449f,
		{ 0x87, 0xbe, 0x98, 0x18, 0xfc, 0x91, 0xb7, 0x33 } },
	{ 0x665e3ff5, 0x46cc, 0x11d4,
		{ 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } },
	{ 0x27cfac88, 0x46cc, 0x11d4,
		{ 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } },
};

static const EFI_GUID pci_root_protocols[] = {
	{ 0xcf8034be, 0x6768, 0x4d8b,
		{ 0xb7, 0x39, 0x7c, 0xce, 0x68, 0x3a, 0x9f, 0xbe } },
	{ 0x2f707ebb, 0x4a1a, 0x11d4,
		{ 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } },
};
static const EFI_GUID nvme_pass_protocol = { 0x52c78312, 0x8edc, 0x4233,
	{ 0x98, 0xf2, 0x1a, 0x1a, 0xa5, 0xe3, 0x88, 0xa5 } };
static const EFI_GUID ata_pass_protocol = { 0x1d3de7f0, 0x0807, 0x424f,
	{ 0xaa, 0x69, 0x11, 0xa5, 0x4e, 0x19, 0xa4, 0x6f } };
static const EFI_GUID ide_init_protocol = { 0xa1e37052, 0x80d9, 0x4e65,
	{ 0xa3, 0x17, 0x3e, 0x9a, 0x55, 0xc4, 0x3e, 0xc9 } };
static const EFI_GUID block_io_protocol = { 0x964e5b21, 0x6459, 0x11d2,
	{ 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
static const EFI_GUID disk_io_protocol = { 0xce345171, 0xba0b, 0x11d2,
	{ 0x8e, 0x4f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };

static const EFI_GUID dxe_service_protocols[] = {
	{ 0xa46423e3, 0x4617, 0x49f1,
		{ 0xb9, 0xff, 0xd1, 0xbf, 0xa9, 0x11, 0x58, 0x39 } },
	{ 0x94ab2f58, 0x1438, 0x4ef1,
		{ 0x91, 0x52, 0x18, 0x94, 0x1a, 0x3a, 0x0e, 0x68 } },
};
static const EFI_GUID usb2_hc_protocol = { 0x3e745226, 0x9818, 0x45b6,
	{ 0xa2, 0xac, 0xd7, 0xcd, 0x0e, 0x8b, 0xa2, 0xbc } };
static const EFI_GUID usb_io_protocol = { 0x2b2f68d6, 0x0cd2, 0x44cf,
	{ 0x8e, 0x8b, 0xbb, 0xa2, 0x0b, 0x1b, 0x5b, 0x75 } };

static const EFI_GUID arch_protocols[] = {
	{ 0x26baccb1, 0x6f42, 0x11d4,
		{ 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } },
	{ 0x26baccb2, 0x6f42, 0x11d4,
		{ 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } },
	{ 0xb7dfb4e1, 0x052f, 0x449f,
		{ 0x87, 0xbe, 0x98, 0x18, 0xfc, 0x91, 0xb7, 0x33 } },
	{ 0xad61f191, 0xae5f, 0x4c0e,
		{ 0xb9, 0xfa, 0xe8, 0x69, 0xd2, 0x88, 0xc6, 0x4f } },
	{ 0x26baccb3, 0x6f42, 0x11d4,
		{ 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } },
	{ 0x665e3ff5, 0x46cc, 0x11d4,
		{ 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } },
	{ 0x27cfac88, 0x46cc, 0x11d4,
		{ 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } },
};

static const EFI_GUID variable_protocols[] = {
#if !CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME
	{ 0x8f644fa9, 0xe850, 0x4db1,
		{ 0x9c, 0xe2, 0x0b, 0x44, 0x69, 0x8e, 0x8d, 0xa4 } },
	{ 0x3ebd9e82, 0x2c78, 0x4de6,
		{ 0x97, 0x86, 0x8d, 0x4b, 0xfc, 0xb7, 0xc8, 0x81 } },
#endif
	{ 0x1e5668e2, 0x8481, 0x11d4,
		{ 0xbc, 0xf1, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } },
	{ 0x6441f818, 0x6362, 0x4e44,
		{ 0xb5, 0x70, 0x7d, 0xba, 0x31, 0xdd, 0x24, 0x53 } },
	{ 0x27cfac87, 0x46cc, 0x11d4,
		{ 0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } },
	{ 0x1da97072, 0xbddc, 0x4b30,
		{ 0x99, 0xf1, 0x72, 0xa0, 0xb5, 0x6f, 0xff, 0x2a } },
};
#endif

typedef void CDK2_MS_ABI bds_entry_fn(void *context);
typedef EFI_STATUS CDK2_MS_ABI boot_next_pending_fn(BOOLEAN *pending);
typedef EFI_STATUS CDK2_MS_ABI input_poll_fn(void *context);
typedef EFI_STATUS CDK2_MS_ABI enter_setup_fn(UINTN timeout_seconds,
	input_poll_fn *poll_input, void *poll_context);
struct bds_arch_protocol {
	bds_entry_fn *entry;
	boot_next_pending_fn *boot_next_pending;
	enter_setup_fn *enter_setup;
};

#if !CDK2_STRICT_DIRECT_RUNTIME
typedef EFI_STATUS CDK2_MS_ABI decompress_info_fn(void *, void *, UINT32,
	UINT32 *, UINT32 *);
typedef EFI_STATUS CDK2_MS_ABI decompress_fn(void *, void *, UINT32,
	void *, UINT32, void *, UINT32);
struct decompress_protocol {
	decompress_info_fn *get_info;
	decompress_fn *decompress;
};
typedef EFI_STATUS CDK2_MS_ABI guided_extract_fn(void *, const void *, void **,
	UINTN *, UINT32 *);
struct guided_extraction_protocol {
	guided_extract_fn *extract;
};
static const EFI_GUID decompress_guid = { 0xd8117cfe, 0x94a6, 0x11d4,
	{ 0x9a, 0x3a, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
#endif

EFI_STATUS cdk2_dxe_validate_guided_section(EFI_STATUS status,
	UINT32 authentication, const void *output, UINTN output_size)
{
	if (EFI_ERROR(status))
		return status;
	if ((authentication & 0x8U) != 0U)
		return EFI_SECURITY_VIOLATION;
	return output == NULL || output_size == 0U ?
		EFI_COMPROMISED_DATA : EFI_SUCCESS;
}

#if !CDK2_STRICT_DIRECT_RUNTIME
static EFI_STATUS scan_service(const void *volume, UINTN size)
{
	return cdk2_dxe_dispatcher_scan(&core.dispatcher, volume, size);
}

static EFI_STATUS mount_service(const void *volume, UINTN size, void **handle)
{
	EFI_STATUS status = cdk2_dxe_fv_mount(&core, volume, size);

	if (!EFI_ERROR(status)) {
		if (core.firmware_volume == volume)
			*handle = core.firmware_volume_handle;
		else {
			UINTN index;

			for (index = 0U; index < core.additional_firmware_volume_count;
			     index++)
				if (core.additional_firmware_volumes[index].volume == volume)
					break;
			*handle = index == core.additional_firmware_volume_count ? NULL :
				core.additional_firmware_volumes[index].handle;
			if (*handle == NULL)
				return EFI_NOT_FOUND;
		}
	}
	return status;
}
#endif

#ifdef CDK2_DEBUG
static void trace_hobs(EFI_HOB_HANDOFF_INFO_TABLE *handoff)
{
	EFI_HOB_GENERIC_HEADER *hob = (void *)handoff;
	UINTN end = handoff->efi_end_of_hob_list;

	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_156, EFI_SUCCESS);
	while ((UINTN)hob < end && hob->hob_type != EFI_HOB_TYPE_END_OF_HOB_LIST) {
		if (hob->hob_length < sizeof(*hob) || hob->hob_length >
		    end - (UINTN)hob)
			break;
		if (hob->hob_type == EFI_HOB_TYPE_MEMORY_ALLOCATION)
			CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_157,
				EFI_SUCCESS);
		else if (hob->hob_type == EFI_HOB_TYPE_FV)
			CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_158,
				EFI_SUCCESS);
		hob = (void *)((UINT8 *)hob + hob->hob_length);
	}
}
#endif

static BOOLEAN protocol_present(const EFI_GUID *guid, void *context)
{
	UINTN index;

	(void)context;
	for (index = 0; index < core.database.protocol_count; index++)
		if (memcmp(guid, &core.database.protocols[index].guid,
			   sizeof(*guid)) == 0)
			return TRUE;
	return FALSE;
}

#if CONFIG_CDK2_LINEAR_BOOT
static UINTN protocol_instance_count(const EFI_GUID *guid)
{
	UINTN count = 0U;

	for (UINTN index = 0U; index < core.database.protocol_count; index++)
		if (memcmp(guid, &core.database.protocols[index].guid,
		    sizeof(*guid)) == 0)
			count++;
	return count;
}
#endif

static void *find_protocol(const EFI_GUID *guid)
{
	UINTN index;

	for (index = 0U; index < core.database.protocol_count; index++)
		if (memcmp(guid, &core.database.protocols[index].guid,
		    sizeof(*guid)) == 0)
			return core.database.protocols[index].interface;
	return NULL;
}

#if !CDK2_STRICT_DIRECT_RUNTIME
static void release_section(void *buffer, void *context)
{
	(void)context;
	(void)cdk2_dxe_free_pool(&core.memory, buffer);
}

static EFI_STATUS extract_section(UINT8 type, const void *input,
	UINTN input_size, void **output, UINTN *output_size, void *context)
{
	const EFI_COMMON_SECTION_HEADER *common = input;
	UINTN common_size;
	EFI_STATUS status;

	(void)context;
	if (input == NULL || output == NULL || output_size == NULL)
		return EFI_INVALID_PARAMETER;
	*output = NULL;
	*output_size = 0U;
	common_size = common->size[0] == 0xffU && common->size[1] == 0xffU &&
		common->size[2] == 0xffU ? sizeof(EFI_COMMON_SECTION_HEADER2) :
		sizeof(EFI_COMMON_SECTION_HEADER);
	if (input_size < common_size)
		return EFI_COMPROMISED_DATA;
	if (type == EFI_SECTION_COMPRESSION) {
		const UINT8 *source;
		UINTN source_size;
		UINT32 uncompressed_length;
		UINT8 compression_type;

		if (input_size - common_size < sizeof(uncompressed_length) + 1U)
			return EFI_COMPROMISED_DATA;
		memcpy(&uncompressed_length, (const UINT8 *)input + common_size,
			sizeof(uncompressed_length));
		compression_type = *((const UINT8 *)input + common_size +
			sizeof(uncompressed_length));
		source = (const UINT8 *)input + common_size +
			sizeof(uncompressed_length) + 1U;
		source_size = input_size - common_size -
			sizeof(uncompressed_length) - 1U;
		if (compression_type == 0U) {
			if (uncompressed_length != source_size)
				return EFI_COMPROMISED_DATA;
			status = cdk2_dxe_allocate_pool(&core.memory, 4U, source_size,
				output);
			if (!EFI_ERROR(status)) {
				memcpy(*output, source, source_size);
				*output_size = source_size;
			}
			return status;
		} else if (compression_type == 1U) {
			struct decompress_protocol *protocol =
				find_protocol(&decompress_guid);
			UINT32 destination_size, scratch_size;
			void *scratch = NULL;

			if (protocol == NULL || protocol->get_info == NULL ||
			    protocol->decompress == NULL || source_size > MAX_UINT32)
				return EFI_UNSUPPORTED;
			status = protocol->get_info(protocol, (void *)source,
				(UINT32)source_size, &destination_size, &scratch_size);
			if (EFI_ERROR(status))
				return status;
			if (destination_size != uncompressed_length ||
			    destination_size == 0U)
				return EFI_COMPROMISED_DATA;
			status = cdk2_dxe_allocate_pool(&core.memory, 4U,
				destination_size, output);
			if (!EFI_ERROR(status) && scratch_size != 0U)
				status = cdk2_dxe_allocate_pool(&core.memory, 4U,
					scratch_size, &scratch);
			if (!EFI_ERROR(status))
				status = protocol->decompress(protocol, (void *)source,
					(UINT32)source_size, *output, destination_size,
					scratch, scratch_size);
			if (scratch != NULL)
				(void)cdk2_dxe_free_pool(&core.memory, scratch);
			if (EFI_ERROR(status)) {
				if (*output != NULL)
					(void)cdk2_dxe_free_pool(&core.memory, *output);
				*output = NULL;
				return status;
			}
			*output_size = destination_size;
			return EFI_SUCCESS;
		}
		return EFI_UNSUPPORTED;
	} else if (type == EFI_SECTION_GUID_DEFINED) {
		struct guided_extraction_protocol *protocol;
		EFI_GUID definition;
		UINT16 data_offset;
		UINT16 attributes;
		UINT32 authentication = 0U;

		if (input_size - common_size < sizeof(definition) + 2U * sizeof(UINT16))
			return EFI_COMPROMISED_DATA;
		memcpy(&definition, (const UINT8 *)input + common_size,
			sizeof(definition));
		memcpy(&data_offset, (const UINT8 *)input + common_size +
			sizeof(definition), sizeof(data_offset));
		memcpy(&attributes, (const UINT8 *)input + common_size +
			sizeof(definition) + sizeof(data_offset), sizeof(attributes));
		if (data_offset < common_size + sizeof(definition) + 2U * sizeof(UINT16) ||
		    data_offset > input_size)
			return EFI_COMPROMISED_DATA;
		if ((attributes & EFI_GUIDED_SECTION_PROCESSING_REQUIRED) == 0U) {
			*output_size = input_size - data_offset;
			if (*output_size == 0U)
				return EFI_COMPROMISED_DATA;
			status = cdk2_dxe_allocate_pool(&core.memory, 4U, *output_size,
				output);
			if (!EFI_ERROR(status))
				memcpy(*output, (const UINT8 *)input + data_offset,
					*output_size);
			return status;
		}
		protocol = find_protocol(&definition);
		if (protocol == NULL || protocol->extract == NULL)
			return EFI_UNSUPPORTED;
		status = protocol->extract(protocol, common, output, output_size,
			&authentication);
		if (EFI_ERROR(status)) {
			*output = NULL;
			*output_size = 0U;
			return status;
		}
		status = cdk2_dxe_validate_guided_section(status, authentication,
			*output, *output_size);
		if (EFI_ERROR(status)) {
			if (*output != NULL)
				(void)cdk2_dxe_free_pool(&core.memory, *output);
			*output = NULL;
			*output_size = 0U;
			return status;
		}
		return EFI_SUCCESS;
	}
	return EFI_UNSUPPORTED;
}
#endif

static EFI_STATUS start_driver(const EFI_GUID *guid, const void *image,
	UINTN image_size, enum cdk2_dxe_driver_origin origin, void *context)
{
#if CDK2_STRICT_DIRECT_RUNTIME
	typedef EFI_STATUS CDK2_MS_ABI handle_protocol_fn(void *, const EFI_GUID *,
		void **);
	typedef EFI_STATUS CDK2_MS_ABI locate_device_path_fn(const EFI_GUID *,
		void **, void **);
#endif
	typedef EFI_STATUS CDK2_MS_ABI start_fn(void *, UINTN *,
		void *exit_data[]);
	typedef EFI_STATUS CDK2_MS_ABI load_fn(BOOLEAN, void *, void *, void *,
		UINTN, void **);
	load_fn *load = core.boot_services.load_image;
	start_fn *start = core.boot_services.start_image;
	void *handle = NULL;
	void *path = NULL;
	UINTN exit_size = 0;
	CHAR16 *exit_data = NULL;
	EFI_STATUS status;

	(void)context;
#if !CDK2_STRICT_DIRECT_RUNTIME
	(void)guid;
#endif
	if (origin == CDK2_DXE_DRIVER_LINKED_PRIMARY) {
#if CDK2_STRICT_DIRECT_RUNTIME
		if (linked_primary_handle == core.firmware_volume_handle)
			return EFI_COMPROMISED_DATA;
		status = validate_linked_primary_owner(linked_primary_handle,
			(handle_protocol_fn *)core.boot_services.handle_protocol,
			(locate_device_path_fn *)
			core.boot_services.locate_device_path);
		if (EFI_ERROR(status))
			return status;
		path = (void *)&linked_primary_path;
		status = cdk2_dxe_start_linked_primary(&core, guid, core_image_handle,
			linked_primary_handle, path, image, image_size, &exit_size, &exit_data);
		if (exit_data != NULL)
			(void)cdk2_dxe_free_pool(&core.memory, exit_data);
		return status;
#else
		return EFI_COMPROMISED_DATA;
#endif
	} else if (origin != CDK2_DXE_DRIVER_FIRMWARE_VOLUME) {
		return EFI_COMPROMISED_DATA;
	}
	status = load(FALSE, core_image_handle, path, (void *)image, image_size,
			      &handle);
	if (EFI_ERROR(status))
		return status;
	status = start(handle, &exit_size, (void **)&exit_data);
	if (exit_data != NULL)
		(void)cdk2_dxe_free_pool(&core.memory, exit_data);
	if (EFI_ERROR(status))
		(void)cdk2_dxe_discard_failed_image(&core, handle);
	return status;
}

#if !CDK2_STRICT_DIRECT_RUNTIME
static EFI_STATUS find_volume(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
			      const void **volume, UINTN *volume_size)
{
	EFI_HOB_GENERIC_HEADER *hob = (void *)handoff;
	UINTN end = handoff->efi_end_of_hob_list;

	while ((UINTN)hob < end) {
		if (hob->hob_length < sizeof(*hob) || (hob->hob_length & 7U) != 0 ||
		    hob->hob_length > end - (UINTN)hob)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_type == EFI_HOB_TYPE_FV && hob->hob_length >=
		    sizeof(EFI_HOB_FIRMWARE_VOLUME)) {
			EFI_HOB_FIRMWARE_VOLUME *fv = (void *)hob;

			if (fv->base_address == 0 || fv->length > MAX_UINTN)
				return EFI_COMPROMISED_DATA;
			*volume = (void *)(UINTN)fv->base_address;
			*volume_size = (UINTN)fv->length;
			return EFI_SUCCESS;
		}
		hob = (void *)((UINT8 *)hob + hob->hob_length);
	}
	return EFI_NOT_FOUND;
}
#endif

static EFI_STATUS seed_gcd(EFI_HOB_HANDOFF_INFO_TABLE *handoff)
{
	typedef EFI_STATUS CDK2_MS_ABI add_memory_fn(UINT32, UINT64, UINT64,
		UINT64);
	typedef EFI_STATUS CDK2_MS_ABI add_io_fn(UINT32, UINT64, UINT64);
	EFI_HOB_GENERIC_HEADER *hob = (void *)handoff;
	UINTN end = handoff->efi_end_of_hob_list;
	add_memory_fn *add_memory = core.dxe_services.add_memory_space;
	add_io_fn *add_io = core.dxe_services.add_io_space;
	EFI_STATUS status;
	UINT64 allocator_start = (UINTN)core.memory.base;
	UINT64 allocator_end = allocator_start + core.memory.total_pages * 4096U;
	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_080, EFI_SUCCESS);
	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_081, EFI_SUCCESS);
	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_082, EFI_SUCCESS);

	/* The PI handoff describes physical memory but does not necessarily carry
	 * an I/O resource descriptor.  The processor-visible x86 I/O domain is the
	 * complete 16-bit port space; seed it as usable I/O before reserving the
	 * decoded child apertures through the PCI host bridge. */
	status = add_io(2, 0, 0x10000U);
	if (EFI_ERROR(status) && status != CDK2_DXE_ACCESS_DENIED)
		return status;

	while ((UINTN)hob < end) {
		if (hob->hob_length < sizeof(*hob) || (hob->hob_length & 7U) != 0 ||
		    hob->hob_length > end - (UINTN)hob)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_type == EFI_HOB_TYPE_RESOURCE_DESCRIPTOR &&
		    hob->hob_length >= sizeof(EFI_HOB_RESOURCE_DESCRIPTOR)) {
			EFI_HOB_RESOURCE_DESCRIPTOR *resource = (void *)hob;
			UINT64 start = resource->physical_start;
			UINT64 end;
			UINT32 memory_type = resource->resource_type ==
				EFI_RESOURCE_SYSTEM_MEMORY ? 7U :
				resource->resource_type == EFI_RESOURCE_MEMORY_MAPPED_IO ? 11U : 0U;
			UINT64 memory_attribute = 0U;

			if ((resource->resource_attribute &
			    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE) != 0U)
				memory_attribute |= EFI_MEMORY_WB;
			if ((resource->resource_attribute &
			    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE) != 0U)
				memory_attribute |= EFI_MEMORY_WT;
			if ((resource->resource_attribute &
			    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE) != 0U)
				memory_attribute |= EFI_MEMORY_UC;
			status = EFI_SUCCESS;
			CDK2_DXE_DIAG(CDK2_DIAG_DEBUG,
				resource->resource_type == 2U ? CDK2_DXE_088 :
				CDK2_DXE_083, resource->resource_type);
			if (start > UINT64_MAX - resource->resource_length)
				return EFI_COMPROMISED_DATA;
			end = start + resource->resource_length;

			if (end > start && resource->resource_type != 2U) {
				UINT64 left_end = end < allocator_start ? end : allocator_start;
				UINT64 right_start = start > allocator_end ? start : allocator_end;

				if (start < left_end && core.external_memory_count <
				    ARRAY_SIZE(core.external_memory))
					core.external_memory[core.external_memory_count++] =
						(struct cdk2_dxe_memory_descriptor){
							.type = memory_type, .physical_start = start,
							.number_of_pages = (left_end - start) / 4096U,
							.attribute = memory_attribute };
				if (right_start < end && core.external_memory_count <
				    ARRAY_SIZE(core.external_memory))
					core.external_memory[core.external_memory_count++] =
						(struct cdk2_dxe_memory_descriptor){
							.type = memory_type, .physical_start = right_start,
							.number_of_pages = (end - right_start) / 4096U,
							.attribute = memory_attribute };
			}

			if (resource->resource_type == EFI_RESOURCE_SYSTEM_MEMORY)
				status = add_memory(2, resource->physical_start,
					resource->resource_length, memory_attribute);
			else if (resource->resource_type == EFI_RESOURCE_MEMORY_MAPPED_IO)
				status = add_memory(3, resource->physical_start,
					resource->resource_length, memory_attribute);
			else if (resource->resource_type == 2U)
				status = add_io(2, resource->physical_start,
					resource->resource_length);
			if (EFI_ERROR(status) && status != CDK2_DXE_ACCESS_DENIED)
				return status;
			if (!EFI_ERROR(status) &&
			    (resource->resource_type == EFI_RESOURCE_SYSTEM_MEMORY ||
			    resource->resource_type == EFI_RESOURCE_MEMORY_MAPPED_IO ||
			    resource->resource_type == 2U))
				CDK2_DXE_DIAG(CDK2_DIAG_DEBUG,
					resource->resource_type == 2U ? CDK2_DXE_088 :
					CDK2_DXE_083, resource->resource_type);
		}
		hob = (void *)((UINT8 *)hob + hob->hob_length);
	}
	/* Allocation HOBs refine resource descriptors with the ownership/type that
	 * must be exposed to loaders and the OS. */
	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_084, EFI_SUCCESS);
	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_085, EFI_SUCCESS);
	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_086, EFI_SUCCESS);
	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_087, EFI_SUCCESS);
	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_089, EFI_SUCCESS);
	hob = (void *)handoff;
	while ((UINTN)hob < end) {
		if (hob->hob_type == EFI_HOB_TYPE_MEMORY_ALLOCATION &&
		    hob->hob_length >= sizeof(EFI_HOB_MEMORY_ALLOCATION)) {
			EFI_HOB_MEMORY_ALLOCATION *allocation = (void *)hob;
			UINT64 start = allocation->alloc_descriptor.memory_base_address;
			EFI_STATUS refine = cdk2_dxe_memory_refine_external_allocation(
				core.external_memory, &core.external_memory_count, start,
				allocation->alloc_descriptor.memory_length,
				allocation->alloc_descriptor.memory_type);

			if (EFI_ERROR(refine))
				return refine;
		}
		hob = (void *)((UINT8 *)hob + hob->hob_length);
	}
	(void)cdk2_dxe_memory_set_external(&core.memory, core.external_memory,
		core.external_memory_count);
	return EFI_SUCCESS;
}

struct linear_pci_roots_state {
	void *handles[CDK2_PCI_HOST_MAX_ROOTS];
	UINTN count;
	BOOLEAN valid;
};

static EFI_STATUS linear_pci_roots_ready(
	const struct linear_pci_roots_state *roots)
{
	if (roots == NULL || !roots->valid || roots->count == 0U)
		return EFI_NOT_READY;
	if (roots->count > ARRAY_SIZE(roots->handles))
		return EFI_COMPROMISED_DATA;
	for (UINTN index = 0U; index < roots->count; index++)
		if (roots->handles[index] == NULL)
			return EFI_COMPROMISED_DATA;
	return EFI_SUCCESS;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_linear_pci_roots(void *const *handles, UINTN count,
	BOOLEAN valid)
{
	struct linear_pci_roots_state roots = { .count = count, .valid = valid };

	if (count <= ARRAY_SIZE(roots.handles) && handles != NULL)
		for (UINTN index = 0U; index < count; index++)
			roots.handles[index] = handles[index];
	return linear_pci_roots_ready(&roots);
}
#endif

struct boot_graphics_snapshot {
	struct cdk2_boot_logo_surface framebuffer;
	struct cdk2_boot_logo_layout rectangle;
	void *pixels;
	size_t size;
};

#if CONFIG_CDK2_LVGL_RENDERER
struct linear_status_output {
	void *image;
	uint64_t generation;
	struct cdk2_lvgl_status_protocol *interface;
	cdk2_lvgl_show_status_fn *show_status;
};
#endif

struct dxe_services_context {
	EFI_HOB_HANDOFF_INFO_TABLE *handoff;
	struct cdk2_system_fmp_client capsule_client;
	const void *volume;
	UINTN volume_size;
#if CONFIG_CDK2_LINEAR_BOOT || defined(CDK2_HOST_TEST)
	/* coreboot's immutable HOB list is validated once per services context. */
	EFI_HOB_HANDOFF_INFO_TABLE *topology_source;
	const struct cdk2_pci_topology_handoff *topology;
	EFI_STATUS topology_status;
#endif
#if CONFIG_CDK2_LINEAR_BOOT
	struct linear_pci_roots_state pci_roots;
	struct cdk2_linear_state *linear_state;
	struct boot_graphics_snapshot splash_snapshot;
	struct boot_graphics_snapshot status_snapshot;
#if CONFIG_CDK2_LVGL_RENDERER
	struct linear_status_output status_output;
#endif
	enum cdk2_linear_capsule_outcome capsule_outcome;
	struct cdk2_cpu_arch *loader_cpu;
	struct cdk2_xhci_control *loader_xhci;
	void *capsule_runtime_owner;
	void *capsule_fmp_owner;
	uint64_t capsule_fmp_generation;
	const struct cdk2_dxe_driver *capsule_fmp_source;
	uint64_t capsule_fmp_transaction_generation;
	const void *capsule_fmp_source_image;
	size_t capsule_fmp_source_size;
	BOOLEAN loader_interrupts_changed;
	BOOLEAN loader_xhci_polling;
	struct cdk2_dxe_lifecycle lifecycle;
#endif
};

#if CONFIG_CDK2_LINEAR_BOOT
static EFI_STATUS dispatch_required(struct dxe_services_context *context,
	const struct required_driver *drivers, UINTN count);
#if CDK2_STRICT_DIRECT_RUNTIME
static EFI_STATUS skip_direct_required(struct cdk2_dxe_dispatcher *dispatcher,
	const struct required_driver *drivers,
	UINTN count, enum cdk2_dxe_direct_disposition disposition,
	const CHAR8 *reason);
#endif

static EFI_STATUS lifecycle_create(void *context, const EFI_GUID *group,
	void **event)
{
	typedef EFI_STATUS CDK2_MS_ABI create_event_ex_fn(UINT32, UINTN,
		void *, const void *, const EFI_GUID *, void **);
	create_event_ex_fn *create_event_ex =
		(create_event_ex_fn *)core.boot_services.create_event_ex;

	(void)context;
	return create_event_ex == NULL ? EFI_UNSUPPORTED :
		create_event_ex(0U, 0U, NULL, NULL, group, event);
}

static EFI_STATUS lifecycle_signal(void *context, void *event)
{
	typedef EFI_STATUS CDK2_MS_ABI event_fn(void *);
	event_fn *signal = (event_fn *)core.boot_services.signal_event;

	(void)context;
	return signal == NULL ? EFI_UNSUPPORTED : signal(event);
}

static EFI_STATUS lifecycle_close(void *context, void *event)
{
	typedef EFI_STATUS CDK2_MS_ABI event_fn(void *);
	event_fn *close = (event_fn *)core.boot_services.close_event;

	(void)context;
	return close == NULL ? EFI_UNSUPPORTED : close(event);
}


static struct cdk2_dxe_lifecycle_ops lifecycle_ops(
	struct dxe_services_context *context)
{
	return (struct cdk2_dxe_lifecycle_ops) {
		.create = lifecycle_create,
		.signal = lifecycle_signal,
		.close = lifecycle_close,
		.context = context,
	};
}
#endif

EFI_STATUS cdk2_dxe_linear_require_protocols(const EFI_GUID *guids,
	UINTN count, cdk2_dxe_protocol_present_fn *present, void *context)
{
	UINTN index;

	if (guids == NULL || count == 0U || present == NULL)
		return EFI_INVALID_PARAMETER;
	for (index = 0U; index < count; index++)
		if (!present(&guids[index], context))
			return EFI_NOT_FOUND;
	return EFI_SUCCESS;
}

#if CONFIG_CDK2_LINEAR_BOOT
static EFI_STATUS dispatch_required(struct dxe_services_context *context,
	const struct required_driver *drivers, UINTN count)
{
	UINTN index;

	(void)context;
	for (index = 0U; index < count; index++) {
		EFI_STATUS status = cdk2_dxe_dispatch_one(&core.dispatcher,
			&drivers[index].guid);

		cdk2_diag_phase(drivers[index].name, "required driver", status, 0U);
		if (EFI_ERROR(status))
			return status;
	}
	return EFI_SUCCESS;
}

#if CDK2_STRICT_DIRECT_RUNTIME
static EFI_STATUS skip_direct_required(struct cdk2_dxe_dispatcher *dispatcher,
	const struct required_driver *drivers,
	UINTN count, enum cdk2_dxe_direct_disposition disposition,
	const CHAR8 *reason)
{
	if (dispatcher == NULL || drivers == NULL || count == 0U || reason == NULL)
		return EFI_INVALID_PARAMETER;
	for (UINTN index = 0U; index < count; index++) {
		EFI_STATUS status = cdk2_dxe_dispatcher_skip_direct(dispatcher,
			&drivers[index].guid, disposition);

		if (status == EFI_ALREADY_STARTED)
			continue;
		cdk2_diag_phase(drivers[index].name, reason, status, disposition);
		if (EFI_ERROR(status))
			return status;
	}
	return EFI_SUCCESS;
}

static EFI_STATUS disposition_storage_route(
	struct cdk2_dxe_dispatcher *dispatcher, enum storage_backend backend)
{
	EFI_STATUS status = EFI_SUCCESS;

#if CONFIG_CDK2_NATIVE_NVME
	if (backend != STORAGE_NVME)
		status = skip_direct_required(dispatcher, storage_controller_drivers,
			ARRAY_SIZE(storage_controller_drivers),
			CDK2_DXE_DIRECT_SKIPPED_BACKEND_UNSELECTED,
			"route not selected");
#endif
#if CONFIG_CDK2_NATIVE_SATA_CONTROLLER
	if (!EFI_ERROR(status) && backend != STORAGE_AHCI)
		status = skip_direct_required(dispatcher, ahci_controller_drivers,
			ARRAY_SIZE(ahci_controller_drivers),
			CDK2_DXE_DIRECT_SKIPPED_BACKEND_UNSELECTED,
			"route not selected");
#endif
#if CONFIG_CDK2_NATIVE_ATA_BUS
	if (!EFI_ERROR(status) && backend != STORAGE_AHCI)
		status = skip_direct_required(dispatcher, &ata_bus_driver, 1U,
			CDK2_DXE_DIRECT_SKIPPED_BACKEND_UNSELECTED,
			"route not selected");
#endif
	return status;
}

static EFI_STATUS disposition_usb_route(
	struct cdk2_dxe_dispatcher *dispatcher, enum storage_backend backend,
	BOOLEAN keyboard_enabled)
{
	EFI_STATUS status = EFI_SUCCESS;

	if (backend == STORAGE_USB)
		return EFI_SUCCESS;
#if CONFIG_CDK2_NATIVE_USB_MASS_STORAGE
	status = skip_direct_required(dispatcher, &usb_mass_driver, 1U,
		CDK2_DXE_DIRECT_SKIPPED_BACKEND_UNSELECTED,
		"route not selected");
#endif
#if CONFIG_CDK2_NATIVE_XHCI
	if (!EFI_ERROR(status))
		status = skip_direct_required(dispatcher, &xhci_controller_driver, 1U,
			CDK2_DXE_DIRECT_SKIPPED_BACKEND_UNSELECTED,
			"route not selected");
#endif
#if CONFIG_CDK2_NATIVE_USB_BUS
	if (!EFI_ERROR(status))
		status = skip_direct_required(dispatcher, &usb_bus_driver, 1U,
			CDK2_DXE_DIRECT_SKIPPED_BACKEND_UNSELECTED,
			"route not selected");
	if (!EFI_ERROR(status) && keyboard_enabled)
		status = skip_direct_required(dispatcher, &usb_keyboard_driver, 1U,
			CDK2_DXE_DIRECT_SKIPPED_HARDWARE_ABSENT,
			"hardware absent");
#else
	(void)keyboard_enabled;
#endif
	return status;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_disposition_storage(
	struct cdk2_dxe_dispatcher *dispatcher, UINTN backend)
{
	if (backend > STORAGE_AHCI)
		return EFI_INVALID_PARAMETER;
	return disposition_storage_route(dispatcher, (enum storage_backend)backend);
}

EFI_STATUS cdk2_dxe_test_disposition_usb(
	struct cdk2_dxe_dispatcher *dispatcher, UINTN backend,
	BOOLEAN keyboard_enabled)
{
	if (backend > STORAGE_AHCI)
		return EFI_INVALID_PARAMETER;
	return disposition_usb_route(dispatcher, (enum storage_backend)backend,
		keyboard_enabled);
}

EFI_STATUS cdk2_dxe_test_disposition_tcg2(
	struct cdk2_dxe_dispatcher *dispatcher, BOOLEAN hardware_present)
{
#if CONFIG_CDK2_NATIVE_TCG2
	if (!hardware_present)
		return skip_direct_required(dispatcher, measured_boot_drivers,
			ARRAY_SIZE(measured_boot_drivers),
			CDK2_DXE_DIRECT_SKIPPED_HARDWARE_ABSENT,
			"hardware absent");
#else
	(void)dispatcher;
	(void)hardware_present;
#endif
	return EFI_SUCCESS;
}

EFI_STATUS cdk2_dxe_test_direct_complete(
	const struct cdk2_dxe_dispatcher *dispatcher)
{
	return cdk2_dxe_dispatcher_direct_complete(dispatcher);
}
#endif
#endif
#endif

static EFI_STATUS install_core_image_handle(struct cdk2_dxe_core *dxe_core,
	void **handle, void *system_table)
{
	struct cdk2_dxe_image *core_image;
	EFI_STATUS status;

	if (dxe_core == NULL || handle == NULL || system_table == NULL)
		return EFI_INVALID_PARAMETER;
	core_image = &dxe_core->images.images[0];
	memset(core_image, 0, sizeof(*core_image));
	core_image->started = TRUE;
	core_image->loaded_image.system_table = system_table;
	status = cdk2_dxe_install(&dxe_core->database, handle,
		&loaded_image_guid, &core_image->loaded_image);
	if (!EFI_ERROR(status)) {
		core_image->handle = *handle;
		dxe_core->images.count = 1U;
	}
	return status;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_install_core_image(struct cdk2_dxe_core *dxe_core,
	void **handle, void *system_table)
{
	return install_core_image_handle(dxe_core, handle, system_table);
}
#endif

static uint64_t initialize_dxe_services(void *opaque)
{
	typedef EFI_STATUS CDK2_MS_ABI install_table_fn(const EFI_GUID *,
		void *);
	struct dxe_services_context *context = opaque;
	EFI_HOB_HANDOFF_INFO_TABLE *handoff = context->handoff;
#if CDK2_STRICT_DIRECT_RUNTIME
	struct cdk2_direct_image_table_view direct_table;
#endif
	UINTN memory_base, memory_top, pages;
	install_table_fn *install_table;
	EFI_STATUS status;

	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_155,
		handoff->efi_free_memory_top - handoff->efi_free_memory_bottom);
#ifdef CDK2_DEBUG
	trace_hobs(handoff);
#endif
	cdk2_diag_emit(CDK2_DIAG_INFO, CDK2_DIAG_DXE_MAIN,
		CDK2_EVENT_DXE_ENTRY, EFI_SUCCESS);
	memory_base = (handoff->efi_free_memory_bottom + 4095U) & ~(UINTN)4095U;
	memory_top = handoff->efi_free_memory_top & ~(UINTN)4095U;
	if (memory_top <= memory_base)
		return EFI_OUT_OF_RESOURCES;
	pages = (memory_top - memory_base) / 4096U;
	CDK2_DXE_DIAG(CDK2_DIAG_INFO, CDK2_DXE_130, EFI_SUCCESS);
	status = cdk2_dxe_core_initialize(&core, (void *)memory_base, pages);
	if (!EFI_ERROR(status)) {
		core_image_handle = NULL;
		status = install_core_image_handle(&core, &core_image_handle,
			&core.runtime_storage->system_table);
	}
	if (EFI_ERROR(status))
		return status;
#if CDK2_STRICT_DIRECT_RUNTIME
	linked_primary_handle = NULL;
	status = cdk2_dxe_install(&core.database, &linked_primary_handle,
		&device_path_guid, (void *)&linked_primary_path);
	if (!EFI_ERROR(status))
		status = cdk2_dxe_protocol_span_set(&core.database,
			linked_primary_handle, &device_path_guid,
			(void *)&linked_primary_path, sizeof(linked_primary_path));
	if (EFI_ERROR(status) || linked_primary_handle == NULL)
		return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
#endif
#if !CDK2_STRICT_DIRECT_RUNTIME
	cdk2_dxe_gcd_set_fv_ops(scan_service, mount_service);
#endif
	CDK2_DXE_DIAG(CDK2_DIAG_INFO, CDK2_DXE_131, core.memory.pages);
	cdk2_diag_emit(CDK2_DIAG_INFO, CDK2_DIAG_DXE_MAIN,
		CDK2_EVENT_DXE_CORE_READY, EFI_SUCCESS);
#if CDK2_STRICT_DIRECT_RUNTIME
	status = cdk2_dxe_direct_images_from_hob(handoff, &direct_table);
#else
	status = find_volume(handoff, &context->volume, &context->volume_size);
#endif
	if (EFI_ERROR(status)) {
		cdk2_diag_emit(CDK2_DIAG_ERROR, CDK2_DIAG_DXE_MAIN,
			CDK2_EVENT_DXE_FV_INVALID, status);
		return status;
	}
	install_table = core.boot_services.install_configuration_table;
	status = install_table(&hob_list_guid, handoff);
	if (!EFI_ERROR(status))
		status = install_table(&dxe_services_guid, &core.dxe_services);
	/* Presence authority must be installed before any driver or externally
	 * supplied image can be dispatched. */
#if CONFIG_CDK2_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPOSITION
	if (!EFI_ERROR(status))
		status =
			cdk2_authvar_presence_lifecycle_close_composition_initialize(
				handoff, &core,
				&core.runtime_storage->runtime_services);
	if (EFI_ERROR(status))
		return status;
#endif
#if !CDK2_STRICT_DIRECT_RUNTIME
	if (!EFI_ERROR(status))
		status = cdk2_dxe_fv_mount(&core, context->volume,
			context->volume_size);
	if (EFI_ERROR(status)) {
		cdk2_diag_emit(CDK2_DIAG_ERROR, CDK2_DIAG_DXE_MAIN,
			CDK2_EVENT_DXE_FV_INVALID, status);
		return status;
	}
	cdk2_diag_emit(CDK2_DIAG_INFO, CDK2_DIAG_DXE_MAIN,
		CDK2_EVENT_DXE_FV_FOUND, EFI_SUCCESS);
#endif
	status = seed_gcd(handoff);
	if (!EFI_ERROR(status))
		status = cdk2_dxe_dispatcher_initialize(&core.dispatcher,
			protocol_present, start_driver, NULL);
	/* Firmware-volume extraction belongs to the retained compatibility path.
	 * Strict startup admits only the bounded direct-image inventory. */
#if !CDK2_STRICT_DIRECT_RUNTIME
	if (!EFI_ERROR(status))
		status = cdk2_dxe_dispatcher_set_extractor(&core.dispatcher,
			extract_section, release_section, NULL);
#endif
#if CDK2_STRICT_DIRECT_RUNTIME
	if (!EFI_ERROR(status))
		status = cdk2_dxe_dispatcher_seed_direct(&core.dispatcher,
			&direct_table);
#else
	if (!EFI_ERROR(status)) {
		cdk2_diag_emit(CDK2_DIAG_INFO, CDK2_DIAG_DXE_DISPATCH,
			CDK2_EVENT_DXE_DISPATCH_BEGIN, EFI_SUCCESS);
		status = cdk2_dxe_dispatcher_scan(&core.dispatcher, context->volume,
			context->volume_size);
	}
#endif
#if CONFIG_CDK2_LINEAR_BOOT
	if (!EFI_ERROR(status))
		status = dispatch_required(context, security_bootstrap,
			ARRAY_SIZE(security_bootstrap));
	if (!EFI_ERROR(status))
		status = cdk2_dxe_linear_require_protocols(dxe_service_protocols,
			ARRAY_SIZE(dxe_service_protocols), protocol_present, NULL);
#endif
	return status;
}

EFI_STATUS cdk2_dxe_linear_unimplemented(
	const struct cdk2_linear_state *state, BOOLEAN *compatibility_allowed)
{
	if (compatibility_allowed == NULL)
		return EFI_INVALID_PARAMETER;
	*compatibility_allowed = FALSE;
	if (EFI_ERROR(cdk2_linear_state_validate(state)) ||
	    state->next_phase != CDK2_LINEAR_PCI_ENUMERATE)
		return EFI_COMPROMISED_DATA;
	return EFI_UNSUPPORTED;
}

EFI_STATUS cdk2_dxe_linear_validate_platform_hobs(void *hob_list)
{
	static const EFI_GUID acpi_hob_guid = { 0x9f9a9506, 0x5597, 0x4515,
		{ 0xba, 0xb6, 0x8b, 0xcd, 0xe7, 0x84, 0xba, 0x87 } };
	static const EFI_GUID smbios_hob_guid = { 0x590a0d26, 0x06e5, 0x4d20,
		{ 0x8a, 0x82, 0x59, 0xea, 0x1b, 0x34, 0x98, 0x2d } };
	static const EFI_GUID smbios3_hob_guid = { 0x92b7896c, 0x3362, 0x46ce,
		{ 0x99, 0xb3, 0x4f, 0x5e, 0x3c, 0x34, 0xeb, 0x42 } };
	EFI_HOB_HANDOFF_INFO_TABLE *handoff = hob_list;
	EFI_HOB_GENERIC_HEADER *hob;
	UINTN end;
	UINTN acpi_count = 0U, smbios_count = 0U;

	if (handoff == NULL || ((UINTN)handoff & 7U) != 0U)
		return EFI_INVALID_PARAMETER;
	if (handoff->header.hob_type != EFI_HOB_TYPE_HANDOFF ||
	    handoff->header.hob_length != sizeof(*handoff) ||
	    handoff->efi_end_of_hob_list <= (UINTN)handoff ||
	    (handoff->efi_end_of_hob_list & 7U) != 0U)
		return EFI_INVALID_PARAMETER;
	end = handoff->efi_end_of_hob_list;
	for (hob = (void *)handoff; (UINTN)hob <= end;
	     hob = (void *)((UINT8 *)hob + hob->hob_length)) {
		EFI_HOB_GUID_TYPE *guid;
		UINTN payload_size;

		if (((UINTN)hob & 7U) != 0U || hob->hob_length < sizeof(*hob) ||
		    (hob->hob_length & 7U) != 0U)
			return EFI_COMPROMISED_DATA;
		if ((UINTN)hob == end) {
			if (hob->hob_type != EFI_HOB_TYPE_END_OF_HOB_LIST ||
			    hob->hob_length != sizeof(*hob))
				return EFI_COMPROMISED_DATA;
			return acpi_count == 1U && smbios_count == 1U ? EFI_SUCCESS :
				EFI_NOT_FOUND;
		}
		if (hob->hob_type == EFI_HOB_TYPE_END_OF_HOB_LIST ||
		    hob->hob_length > end - (UINTN)hob)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_type != EFI_HOB_TYPE_GUID_EXTENSION)
			continue;
		if (hob->hob_length < sizeof(*guid))
			return EFI_COMPROMISED_DATA;
		guid = (void *)hob;
		payload_size = hob->hob_length - sizeof(*guid);
		if (memcmp(&guid->name, &acpi_hob_guid, sizeof(guid->name)) == 0) {
			const CDK2_ACPI_TABLE_HOB *payload = (void *)(guid + 1);

			acpi_count++;
			if (acpi_count != 1U)
				return EFI_COMPROMISED_DATA;
			if (payload_size < sizeof(*payload) ||
			    payload->header.revision != CDK2_ACPI_TABLE_HOB_REVISION ||
			    payload->header.reserved != 0U ||
			    payload->header.length != sizeof(*payload) ||
			    payload->rsdp == 0U)
				return EFI_COMPROMISED_DATA;
		} else if (memcmp(&guid->name, &smbios_hob_guid,
		    sizeof(guid->name)) == 0 || memcmp(&guid->name,
		    &smbios3_hob_guid, sizeof(guid->name)) == 0) {
			const CDK2_SMBIOS_TABLE_HOB_V3 *payload = (void *)(guid + 1);

			smbios_count++;
			if (smbios_count != 1U)
				return EFI_COMPROMISED_DATA;
			if (payload_size < sizeof(*payload) ||
			    payload->header.revision != CDK2_SMBIOS_TABLE_HOB_V3_REVISION ||
			    payload->header.reserved != 0U ||
			    payload->header.length != sizeof(*payload) ||
			    payload->smbios_region_base == 0U ||
			    payload->smbios_region_size == 0U ||
			    payload->smbios_region_base > MAX_UINT64 -
				payload->smbios_region_size)
				return EFI_COMPROMISED_DATA;
			if (payload->smbios2_entry_point == 0U &&
			    payload->smbios3_entry_point == 0U)
				return EFI_COMPROMISED_DATA;
			if ((payload->smbios2_entry_point != 0U &&
			     (payload->smbios2_entry_point < payload->smbios_region_base ||
			      payload->smbios2_entry_point >= payload->smbios_region_base +
				payload->smbios_region_size)) ||
			    (payload->smbios3_entry_point != 0U &&
			     (payload->smbios3_entry_point < payload->smbios_region_base ||
			      payload->smbios3_entry_point >= payload->smbios_region_base +
				payload->smbios_region_size)))
				return EFI_COMPROMISED_DATA;
		}
	}
	return EFI_COMPROMISED_DATA;
}

#if CONFIG_CDK2_LINEAR_BOOT || defined(CDK2_HOST_TEST)
struct capsule_scatter_block {
	UINT64 length, address;
};

static EFI_STATUS capsule_batch(
	struct cdk2_capsule_boot_protocol *processor,
	const struct cdk2_capsule_image *images, UINTN count,
	BOOLEAN *reset_required, BOOLEAN *provider_invoked)
{
	struct capsule_scatter_block scatter[33];
	EFI_STATUS status;

	if (reset_required == NULL)
		return EFI_INVALID_PARAMETER;
	*reset_required = FALSE;
	if (provider_invoked != NULL)
		*provider_invoked = FALSE;
	if (processor == NULL || images == NULL || count == 0U || count > 32U)
		return EFI_INVALID_PARAMETER;
	if (processor->process_scatter == NULL)
		return EFI_UNSUPPORTED;
	for (UINTN index = 0U; index < count; index++)
		scatter[index] = (struct capsule_scatter_block) {
			.length = images[index].size,
			.address = (UINT64)(UINTN)images[index].data };
	scatter[count] = (struct capsule_scatter_block) {0};
	if (provider_invoked != NULL)
		*provider_invoked = TRUE;
	status = processor->process_scatter(processor, (UINT64)(UINTN)scatter);
	if (status == CDK2_CAPSULE_WARN_RESET_REQUIRED) {
		*reset_required = TRUE;
		return EFI_SUCCESS;
	}
	if (status == EFI_SUCCESS)
		*reset_required = processor->reset_required;
	return status;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_capsule_batch(
	struct cdk2_capsule_boot_protocol *processor,
	const struct cdk2_capsule_image *images, UINTN count,
	BOOLEAN *reset_required)
{
	return capsule_batch(processor, images, count, reset_required, NULL);
}
#endif
#endif

#if CONFIG_CDK2_LINEAR_BOOT || defined(CDK2_HOST_TEST)
static UINTN topology_function_location(
	const struct cdk2_pci_topology_function *function)
{
	return ((UINTN)function->segment << 16U) | ((UINTN)function->bus << 8U) |
		((UINTN)function->device << 3U) | function->function;
}

#ifdef CDK2_HOST_TEST
static UINTN topology_parse_count;
#endif

/* EFI_NOT_FOUND means the GUID is absent, never an unowned or malformed HOB. */
static EFI_STATUS find_pci_topology(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
	const struct cdk2_pci_topology_handoff **result)
{
	static const EFI_GUID topology_guid = CDK2_PCI_TOPOLOGY_HANDOFF_HOB_GUID;
	const struct cdk2_pci_topology_handoff *topology = NULL;
	const struct cdk2_pci_boot_controller *boot;
	const EFI_HOB_GENERIC_HEADER *topology_hob = NULL;
	EFI_HOB_GENERIC_HEADER *hob;
	UINTN end, expected, aligned;

	if (handoff == NULL || result == NULL)
		return EFI_INVALID_PARAMETER;
	*result = NULL;
#ifdef CDK2_HOST_TEST
	topology_parse_count++;
#endif
	if (handoff->header.hob_type != EFI_HOB_TYPE_HANDOFF ||
	    handoff->header.hob_length != sizeof(*handoff) ||
	    ((UINTN)handoff & 7U) != 0U)
		return EFI_COMPROMISED_DATA;
	end = handoff->efi_end_of_hob_list;
	if (end <= (UINTN)handoff || (end & 7U) != 0U ||
	    end > MAX_UINTN - sizeof(*hob))
		return EFI_COMPROMISED_DATA;
	for (hob = (void *)handoff; (UINTN)hob < end;
	     hob = (void *)((UINT8 *)hob + hob->hob_length)) {
		EFI_HOB_GUID_TYPE *guid;

		if (((UINTN)hob & 7U) != 0U || hob->hob_length < sizeof(*hob) ||
		    (hob->hob_length & 7U) != 0U || hob->hob_length > end - (UINTN)hob ||
		    hob->hob_type == EFI_HOB_TYPE_END_OF_HOB_LIST)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_type != EFI_HOB_TYPE_GUID_EXTENSION)
			continue;
		if (hob->hob_length < sizeof(*guid))
			return EFI_COMPROMISED_DATA;
		guid = (void *)hob;
		if (memcmp(&guid->name, &topology_guid, sizeof(topology_guid)) != 0)
			continue;
		if (topology != NULL || hob->hob_length < sizeof(*guid) + sizeof(*topology))
			return EFI_COMPROMISED_DATA;
		topology = (const void *)(guid + 1);
		topology_hob = hob;
	}
	hob = (void *)end;
	if (hob->hob_type != EFI_HOB_TYPE_END_OF_HOB_LIST ||
	    hob->hob_length != sizeof(*hob))
		return EFI_COMPROMISED_DATA;
	if (topology == NULL)
		return EFI_NOT_FOUND;
	if (topology->header.revision != CDK2_PCI_TOPOLOGY_HANDOFF_REVISION ||
	    topology->producer_generation == 0U ||
	    topology->function_count > CDK2_PCI_TOPOLOGY_HANDOFF_MAX_FUNCTIONS ||
	    topology->boot_controller_count > topology->function_count ||
	    (topology->flags &
	     ~CDK2_PCI_TOPOLOGY_HANDOFF_FRAMEBUFFER_OWNER_VALID) != 0U)
		return EFI_COMPROMISED_DATA;
	expected = sizeof(*topology) + topology->function_count *
		sizeof(topology->function[0]) + topology->boot_controller_count *
		sizeof(struct cdk2_pci_boot_controller);
	if (expected > MAX_UINT16 || topology->header.length != expected ||
	    sizeof(EFI_HOB_GUID_TYPE) + expected > MAX_UINT16 - 7U)
		return EFI_COMPROMISED_DATA;
	aligned = (sizeof(EFI_HOB_GUID_TYPE) + expected + 7U) & ~(UINTN)7U;
	if (topology_hob->hob_length != aligned)
		return EFI_COMPROMISED_DATA;
	for (UINTN offset = sizeof(EFI_HOB_GUID_TYPE) + expected; offset < aligned;
	     offset++)
		if (((const UINT8 *)(const void *)topology_hob)[offset] != 0U)
			return EFI_COMPROMISED_DATA;
	for (UINTN index = 0U; index < topology->function_count; index++) {
		const struct cdk2_pci_topology_function *function =
			&topology->function[index];

		if (function->device > 31U || function->function > 7U ||
		    function->reserved != 0U ||
		    (function->flags & ~CDK2_PCI_TOPOLOGY_PATH_ONLY) != 0U ||
		    (function->parent_index != CDK2_PCI_TOPOLOGY_PARENT_ROOT &&
		     function->parent_index >= index) ||
		    (function->class_code == 6U && function->subclass == 4U &&
		     ((function->header_type & 0x7fU) != 1U ||
		      function->secondary_bus <= function->bus ||
		      function->subordinate_bus < function->secondary_bus)) ||
		    ((function->class_code != 6U || function->subclass != 4U) &&
		     (function->secondary_bus != 0U || function->subordinate_bus != 0U)))
			return EFI_COMPROMISED_DATA;
		for (UINTN prior = 0U; prior < index; prior++)
			if (topology_function_location(&topology->function[prior]) ==
			    topology_function_location(function))
				return EFI_COMPROMISED_DATA;
	}
	if ((topology->flags &
	     CDK2_PCI_TOPOLOGY_HANDOFF_FRAMEBUFFER_OWNER_VALID) != 0U) {
		if (topology->framebuffer_topology_index >= topology->function_count ||
		    topology->framebuffer_bar > 5U ||
		    (topology->function[topology->framebuffer_topology_index].flags &
		     CDK2_PCI_TOPOLOGY_PATH_ONLY) != 0U)
			return EFI_COMPROMISED_DATA;
	} else if (topology->framebuffer_topology_index !=
	    CDK2_PCI_TOPOLOGY_PARENT_ROOT || topology->framebuffer_bar != 0xffU) {
		return EFI_COMPROMISED_DATA;
	}
	boot = cdk2_pci_topology_boot_controllers(topology);
	for (UINTN index = 0U; index < topology->boot_controller_count; index++) {
		if (boot[index].flags != 0U || boot[index].reserved != 0U ||
		    boot[index].topology_index >= topology->function_count ||
		    (topology->function[boot[index].topology_index].flags &
		     CDK2_PCI_TOPOLOGY_PATH_ONLY) != 0U)
			return EFI_COMPROMISED_DATA;
		for (UINTN prior = 0U; prior < index; prior++)
			if (boot[prior].topology_index == boot[index].topology_index)
				return EFI_COMPROMISED_DATA;
	}
	*result = topology;
	return EFI_SUCCESS;
}

static EFI_STATUS services_pci_topology(struct dxe_services_context *context,
	const struct cdk2_pci_topology_handoff **topology)
{
	if (context == NULL || context->handoff == NULL || topology == NULL)
		return EFI_INVALID_PARAMETER;
	if (context->topology_source != context->handoff) {
		context->topology_status = find_pci_topology(context->handoff,
			&context->topology);
		context->topology_source = context->handoff;
	}
	*topology = context->topology;
	return context->topology_status;
}

static EFI_STATUS framebuffer_bar_contains(const UINT8 resource[48],
	UINT64 framebuffer, UINT64 framebuffer_size, BOOLEAN *contains)
{
	UINT64 base, size;

	if (resource == NULL || contains == NULL || framebuffer_size == 0U ||
	    framebuffer > MAX_UINT64 - framebuffer_size)
		return EFI_INVALID_PARAMETER;
	*contains = FALSE;
	if (resource[0] != 0x8aU || resource[1] != 0x2bU || resource[2] != 0U ||
	    resource[46] != 0x79U)
		return EFI_COMPROMISED_DATA;
	memcpy(&base, resource + 14U, sizeof(base));
	memcpy(&size, resource + 38U, sizeof(size));
	if (size != 0U && base > MAX_UINT64 - size)
		return EFI_COMPROMISED_DATA;
	*contains = resource[3] == 0U && size != 0U && framebuffer >= base &&
		framebuffer + framebuffer_size <= base + size;
	return EFI_SUCCESS;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_framebuffer_bar(const UINT8 resource[48],
	UINT64 framebuffer, UINT64 size, BOOLEAN *contains)
{
	return framebuffer_bar_contains(resource, framebuffer, size, contains);
}
#endif
#endif

#if CONFIG_CDK2_LINEAR_BOOT
static uint64_t initialize_arch_protocols(void *context)
{
	EFI_STATUS status = dispatch_required(context, arch_drivers,
		ARRAY_SIZE(arch_drivers));

	return EFI_ERROR(status) ? status : cdk2_dxe_linear_require_protocols(
		arch_protocols, ARRAY_SIZE(arch_protocols), protocol_present, NULL);
}

static uint64_t initialize_capsule_reports(struct dxe_services_context *services)
{
#if CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE && CONFIG_CDK2_NATIVE_SYSTEM_FMP
	struct cdk2_dxe_protocol *record;
	struct cdk2_dxe_image *owner;
	struct cdk2_variable_policy_protocol *policy;
	struct cdk2_runtime_services_view runtime;

	if (services->handoff == NULL || core.runtime_storage == NULL ||
	    private_control_record(&core, &variable_policy_protocol_guid,
		 sizeof(*policy), &record, &owner) != EFI_SUCCESS ||
	    record->interface_dynamic || owner == NULL)
		return EFI_COMPROMISED_DATA;
	policy = record->interface;
	if (private_control_callback(&core, policy->register_policy, &owner) != EFI_SUCCESS)
		return EFI_COMPROMISED_DATA;
	memcpy(&runtime, core.runtime_storage->system_table.runtime_services,
		sizeof(runtime));
	return cdk2_capsule_report_initialize(&runtime, policy, services->handoff->boot_mode);
#else
	(void)services;
	return EFI_SUCCESS;
#endif
}

static uint64_t initialize_minimal_variables(void *context)
{
	struct dxe_services_context *services = context;
	EFI_STATUS status;

#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME && \
	CONFIG_CDK2_NATIVE_TCG2
	if (direct_tpm_absent_test_entry) {
		status = EFI_SUCCESS;
		goto tcg_host_probe;
	}
#endif
	status = dispatch_required(context, variable_drivers,
		ARRAY_SIZE(variable_drivers));

	if (!EFI_ERROR(status))
		status = cdk2_dxe_linear_require_protocols(variable_protocols,
			ARRAY_SIZE(variable_protocols), protocol_present, NULL);
	if (!EFI_ERROR(status))
		status = initialize_capsule_reports(services);
	/* UpdateCapsule and the system-firmware FMP/ESRT identity are runtime
	 * services, not properties of a capsule source.  Publish them on every
	 * boot so the OS can submit both forward and recovery updates. */
	if (!EFI_ERROR(status)) {
		UINTN image_count = core.images.count;

		status = dispatch_required(context, capsule_disk_drivers,
			ARRAY_SIZE(capsule_disk_drivers));
		if (!EFI_ERROR(status) &&
		    (core.images.count != image_count + ARRAY_SIZE(capsule_disk_drivers) ||
		     !core.images.images[image_count].started))
			status = EFI_COMPROMISED_DATA;
		if (!EFI_ERROR(status)) {
			services->capsule_runtime_owner =
				core.images.images[image_count].handle;
#if CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE && CONFIG_CDK2_NATIVE_SYSTEM_FMP
			if (!core.images.images[image_count + 1U].started)
				status = EFI_COMPROMISED_DATA;
			else {
				struct cdk2_dxe_image *image =
					&core.images.images[image_count + 1U];

				services->capsule_fmp_owner =
					core.images.images[image_count + 1U].handle;
				services->capsule_fmp_generation =
					core.images.images[image_count + 1U].load_generation;
				for (UINTN index = 0U; index < core.dispatcher.count; index++) {
					const struct cdk2_dxe_driver *driver =
						&core.dispatcher.drivers[index];

					if (memcmp(&driver->guid, &capsule_disk_drivers[1].guid,
						   sizeof(driver->guid)) != 0)
						continue;
					if (services->capsule_fmp_source != NULL) {
						status = EFI_COMPROMISED_DATA;
						break;
					}
					services->capsule_fmp_source = driver;
				}
				if (services->capsule_fmp_source == NULL ||
				    FALSE /* publication transaction mutant */)
					status = EFI_COMPROMISED_DATA;
				else {
					services->capsule_fmp_transaction_generation =
						image->transaction->generation;
					services->capsule_fmp_source_image =
						services->capsule_fmp_source->image;
					services->capsule_fmp_source_size =
						services->capsule_fmp_source->image_size;
				}
			}
#endif
		}
	}
	cdk2_diag_value(CDK2_DIAG_DXE_MAIN, "variable policy protocols",
		protocol_instance_count(&variable_policy_protocol_guid));
	cdk2_diag_value(CDK2_DIAG_DXE_MAIN, "firmware management protocols",
		protocol_instance_count(&firmware_management_protocol_guid));
	cdk2_diag_value(CDK2_DIAG_DXE_MAIN, "ESRT management protocols",
		protocol_instance_count(&esrt_management_protocol_guid));
#if CONFIG_CDK2_NATIVE_TCG2
	UINT64 tpm_base = 0U;
	BOOLEAN tcg2_required = FALSE;
#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME
tcg_host_probe:
	if (direct_tpm_absent_test_entry) {
		tpm_base = 0U;
		tcg2_required = FALSE;
		status = EFI_SUCCESS;
		goto tcg_absent_disposition;
	}
#endif
	if (!EFI_ERROR(status)) {
		status = cdk2_tpm2_acpi_hob_probe(
			(const struct cdk2_tpm2_config_table *)
			core.runtime_storage->configuration_tables,
			core.runtime_storage->system_table.number_of_table_entries,
			&tpm_base);
		EFI_STATUS probe_status = status;
		status = classify_linear_tcg2_presence(probe_status, tpm_base,
			protocol_instance_count(&tcg2_protocol_guid),
			configuration_table_present(&tcg2_final_events_guid),
			&tcg2_required);
#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME
tcg_absent_disposition:
#endif
		if (!EFI_ERROR(status) && !tcg2_required)
			cdk2_diag_phase("Tcg2Dxe", "hardware absent",
				EFI_NOT_FOUND, 0U);
	#if CDK2_STRICT_DIRECT_RUNTIME
		if (!EFI_ERROR(status) && !tcg2_required)
			status = skip_direct_required(&core.dispatcher,
				measured_boot_drivers,
				ARRAY_SIZE(measured_boot_drivers),
				CDK2_DXE_DIRECT_SKIPPED_HARDWARE_ABSENT,
				"hardware absent");
	#endif
	}
	if (!EFI_ERROR(status) && tcg2_required)
		status = dispatch_required(context, measured_boot_drivers,
			ARRAY_SIZE(measured_boot_drivers));
	if (!EFI_ERROR(status) && tcg2_required)
		status = require_linear_tcg2_publication();
#endif
	return status;
}

#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME && \
	CONFIG_CDK2_NATIVE_TCG2
EFI_STATUS cdk2_dxe_test_initialize_tpm_absent(
	struct cdk2_dxe_dispatcher *dispatcher)
{
	struct dxe_services_context context = {0};
	struct cdk2_dxe_dispatcher saved_dispatcher = core.dispatcher;
	EFI_STATUS status;

	if (dispatcher == NULL)
		return EFI_INVALID_PARAMETER;
	core.dispatcher = *dispatcher;
	direct_tpm_absent_test_entry = TRUE;
	status = initialize_minimal_variables(&context);
	direct_tpm_absent_test_entry = FALSE;
	*dispatcher = core.dispatcher;
	core.dispatcher = saved_dispatcher;
	return status;
}
#endif

static BOOLEAN configuration_table_present(const EFI_GUID *guid)
{
	UINTN index;

	for (index = 0U;
	     index < core.runtime_storage->system_table.number_of_table_entries;
	     index++)
		if (memcmp(guid, &core.runtime_storage->configuration_tables[index].guid,
		    sizeof(*guid)) == 0 &&
		    core.runtime_storage->configuration_tables[index].table != NULL)
			return TRUE;
	return FALSE;
}

static void *configuration_table_value(const EFI_GUID *guid)
{
	UINTN index;

	for (index = 0U;
	     index < core.runtime_storage->system_table.number_of_table_entries;
	     index++)
		if (memcmp(guid, &core.runtime_storage->configuration_tables[index].guid,
		    sizeof(*guid)) == 0)
			return core.runtime_storage->configuration_tables[index].table;
	return NULL;
}

static UINT8 smbios_checksum(const UINT8 *bytes, UINTN size)
{
	UINT8 checksum = 0U;

	for (UINTN index = 0U; index < size; index++)
		checksum = (UINT8)(checksum + bytes[index]);
	return checksum;
}

static UINT32 smbios_read32(const UINT8 *bytes)
{
	return (UINT32)bytes[0] | (UINT32)bytes[1] << 8 |
		(UINT32)bytes[2] << 16 | (UINT32)bytes[3] << 24;
}

static UINT64 smbios_read64(const UINT8 *bytes)
{
	return smbios_read32(bytes) | (UINT64)smbios_read32(bytes + 4U) << 32;
}

static BOOLEAN smbios_range_contains(const CDK2_SMBIOS_TABLE_HOB_V3 *handoff,
	EFI_PHYSICAL_ADDRESS address, UINTN size)
{
	EFI_PHYSICAL_ADDRESS end;

	if (handoff == NULL || address == 0U || size == 0U ||
	    handoff->smbios_region_base > MAX_UINT64 - handoff->smbios_region_size ||
	    address > MAX_UINT64 - size)
		return FALSE;
	end = handoff->smbios_region_base + handoff->smbios_region_size;
	return address >= handoff->smbios_region_base && address + size <= end;
}

static EFI_STATUS validate_smbios2_entry(const CDK2_SMBIOS_TABLE_HOB_V3 *handoff)
{
	const UINT8 *entry = (const void *)(UINTN)handoff->smbios2_entry_point;
	UINTN table_length;
	EFI_PHYSICAL_ADDRESS table_address;

	if (handoff->smbios2_entry_point == 0U)
		return EFI_SUCCESS;
	if (!smbios_range_contains(handoff, handoff->smbios2_entry_point, 31U) ||
	    memcmp(entry, "_SM_", 4U) != 0 || entry[5] != 31U || entry[6] < 2U ||
	    memcmp(entry + 16U, "_DMI_", 5U) != 0 ||
	    smbios_checksum(entry, 31U) != 0U ||
	    smbios_checksum(entry + 16U, 15U) != 0U)
		return EFI_COMPROMISED_DATA;
	table_length = (UINTN)entry[22] | (UINTN)entry[23] << 8;
	table_address = smbios_read32(entry + 24U);
	return smbios_range_contains(handoff, table_address, table_length) ?
		EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

static EFI_STATUS validate_smbios3_entry(const CDK2_SMBIOS_TABLE_HOB_V3 *handoff)
{
	const UINT8 *entry = (const void *)(UINTN)handoff->smbios3_entry_point;
	UINT32 table_length;
	EFI_PHYSICAL_ADDRESS table_address;

	if (handoff->smbios3_entry_point == 0U)
		return EFI_SUCCESS;
	if (!smbios_range_contains(handoff, handoff->smbios3_entry_point, 24U) ||
	    memcmp(entry, "_SM3_", 5U) != 0 || entry[6] != 24U || entry[7] < 3U ||
	    smbios_checksum(entry, 24U) != 0U)
		return EFI_COMPROMISED_DATA;
	table_length = smbios_read32(entry + 12U);
	table_address = smbios_read64(entry + 16U);
	return smbios_range_contains(handoff, table_address, table_length) ?
		EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

static const CDK2_SMBIOS_TABLE_HOB_V3 *find_smbios_handoff(
	const EFI_HOB_HANDOFF_INFO_TABLE *handoff)
{
	static const EFI_GUID smbios_hob_guid = { 0x590a0d26, 0x06e5, 0x4d20,
		{ 0x8a, 0x82, 0x59, 0xea, 0x1b, 0x34, 0x98, 0x2d } };
	static const EFI_GUID smbios3_hob_guid = { 0x92b7896c, 0x3362, 0x46ce,
		{ 0x99, 0xb3, 0x4f, 0x5e, 0x3c, 0x34, 0xeb, 0x42 } };
	EFI_HOB_GENERIC_HEADER *hob;
	UINTN end;

	if (handoff == NULL)
		return NULL;
	end = handoff->efi_end_of_hob_list;
	for (hob = (void *)handoff; (UINTN)hob < end;
	     hob = (void *)((UINT8 *)hob + hob->hob_length)) {
		EFI_HOB_GUID_TYPE *guid;

		if (hob->hob_length < sizeof(*hob) ||
		    hob->hob_length > end - (UINTN)hob)
			return NULL;
		if (hob->hob_type != EFI_HOB_TYPE_GUID_EXTENSION ||
		    hob->hob_length < sizeof(*guid))
			continue;
		guid = (void *)hob;
		if (memcmp(&guid->name, &smbios_hob_guid, sizeof(guid->name)) == 0 ||
		    memcmp(&guid->name, &smbios3_hob_guid, sizeof(guid->name)) == 0)
			return (const void *)(guid + 1);
	}
	return NULL;
}

static EFI_STATUS publish_coreboot_smbios(
	const EFI_HOB_HANDOFF_INFO_TABLE *handoff)
{
	static const EFI_GUID smbios2_table_guid = { 0xeb9d2d31, 0x2d88, 0x11d3,
		{ 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } };
	static const EFI_GUID smbios3_table_guid = { 0xf2fd1544, 0x9794, 0x4a2c,
		{ 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 } };
	typedef EFI_STATUS CDK2_MS_ABI install_configuration_table_fn(
		const EFI_GUID *, void *);
	const CDK2_SMBIOS_TABLE_HOB_V3 *smbios = find_smbios_handoff(handoff);
	install_configuration_table_fn *install =
		(install_configuration_table_fn *)core.boot_services.install_configuration_table;
	void *old_smbios2;
	EFI_STATUS status;

	if (smbios == NULL || install == NULL)
		return EFI_COMPROMISED_DATA;
	status = validate_smbios2_entry(smbios);
	if (!EFI_ERROR(status))
		status = validate_smbios3_entry(smbios);
	if (EFI_ERROR(status))
		return status;
	old_smbios2 = configuration_table_value(&smbios2_table_guid);
	if (smbios->smbios2_entry_point != 0U)
		status = install(&smbios2_table_guid,
			(void *)(UINTN)smbios->smbios2_entry_point);
	if (!EFI_ERROR(status) && smbios->smbios3_entry_point != 0U)
		status = install(&smbios3_table_guid,
			(void *)(UINTN)smbios->smbios3_entry_point);
	if (EFI_ERROR(status) && smbios->smbios2_entry_point != 0U)
		(void)install(&smbios2_table_guid, old_smbios2);
	return status;
}

static uint64_t initialize_platform_tables(void *opaque)
{
	static const EFI_GUID acpi_protocols[] = {
		{ 0xffe06bdd, 0x6107, 0x46a6,
			{ 0x7b, 0xb2, 0x5a, 0x9c, 0x7e, 0xc5, 0x27, 0x5c } },
		{ 0xeb97088e, 0xcfdf, 0x49c6,
			{ 0xbe, 0x4b, 0xd9, 0x06, 0xa5, 0xb2, 0x0e, 0x86 } },
	};
	static const EFI_GUID acpi_tables[] = {
		{ 0xeb9d2d30, 0x2d88, 0x11d3,
			{ 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } },
		{ 0x8868e871, 0xe4f1, 0x11d3,
			{ 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } },
	};
	struct dxe_services_context *context = opaque;
	EFI_STATUS status;
	UINTN index;

	if (context == NULL || context->linear_state == NULL ||
	    (context->capsule_outcome != CDK2_LINEAR_CAPSULE_RESULT_NONE &&
	     context->capsule_outcome != CDK2_LINEAR_CAPSULE_RESULT_DISK_DEFERRED))
		return EFI_COMPROMISED_DATA;
	status = cdk2_dxe_linear_validate_platform_hobs(context->handoff);
	if (!EFI_ERROR(status))
		status = dispatch_required(context, &platform_table_drivers[0], 1U);
	if (!EFI_ERROR(status))
		status = cdk2_dxe_linear_require_protocols(acpi_protocols,
			ARRAY_SIZE(acpi_protocols), protocol_present, NULL);
	for (index = 0U; !EFI_ERROR(status) && index < ARRAY_SIZE(acpi_tables);
	     index++)
		if (!configuration_table_present(&acpi_tables[index]))
			status = EFI_NOT_FOUND;
	if (!EFI_ERROR(status))
		status = publish_coreboot_smbios(context->handoff);
	return status;
}

static uint64_t initialize_pci_roots(void *opaque)
{
	static const EFI_GUID root_hob_guid = { 0xec4ebacb, 0x2638, 0x416e,
		{ 0xbe, 0x80, 0xe5, 0xfa, 0x4b, 0x51, 0x19, 0x01 } };
	struct dxe_services_context *context = opaque;
	EFI_HOB_GENERIC_HEADER *hob = (void *)context->handoff;
	UINTN end = context->handoff->efi_end_of_hob_list;
	UINTN expected_roots = 0U;
	EFI_STATUS status;

	context->pci_roots = (struct linear_pci_roots_state){0};
	while ((UINTN)hob < end) {
		if (hob->hob_length < sizeof(*hob) || hob->hob_length > end - (UINTN)hob)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_type == EFI_HOB_TYPE_GUID_EXTENSION &&
		    hob->hob_length >= sizeof(EFI_HOB_GUID_TYPE) +
			    sizeof(struct cdk2_pci_root_bridges_hob)) {
			EFI_HOB_GUID_TYPE *guid = (void *)hob;
			struct cdk2_pci_root_bridges_hob *roots = (void *)(guid + 1);

			if (memcmp(&guid->name, &root_hob_guid, sizeof(root_hob_guid)) == 0) {
				if (expected_roots != 0U || roots->count == 0U)
					return EFI_COMPROMISED_DATA;
				expected_roots = roots->count;
			}
		}
		hob = (void *)((UINT8 *)hob + hob->hob_length);
	}
	if (expected_roots == 0U)
		return EFI_NOT_FOUND;

	status = dispatch_required(context, pci_root_drivers,
		ARRAY_SIZE(pci_root_drivers));
	if (!EFI_ERROR(status))
		status = cdk2_dxe_linear_require_protocols(pci_root_protocols,
			ARRAY_SIZE(pci_root_protocols), protocol_present, NULL);
	if (!EFI_ERROR(status) &&
	    protocol_instance_count(&pci_root_protocols[1]) != expected_roots)
		status = EFI_COMPROMISED_DATA;
	if (!EFI_ERROR(status)) {
		for (UINTN index = 0U; index < core.database.protocol_count; index++) {
			struct cdk2_dxe_protocol *record = &core.database.protocols[index];

			if (memcmp(&record->guid, &pci_root_protocols[1],
			    sizeof(pci_root_protocols[1])) != 0)
				continue;
			if (context->pci_roots.count ==
			    ARRAY_SIZE(context->pci_roots.handles))
				return EFI_OUT_OF_RESOURCES;
			context->pci_roots.handles[context->pci_roots.count++] =
				record->handle;
		}
		context->pci_roots.valid = context->pci_roots.count == expected_roots;
		if (!context->pci_roots.valid)
			status = EFI_COMPROMISED_DATA;
	}
	return status;
}

typedef EFI_STATUS CDK2_MS_ABI connect_controller_fn(void *, void **, void *,
	BOOLEAN);
typedef EFI_STATUS CDK2_MS_ABI disconnect_controller_fn(void *, void *, void *);
struct driver_binding_view {
	void *supported, *start, *stop;
	UINT32 version, padding;
	void *image, *binding;
};
struct dispatched_binding {
	void *image;
	void *binding;
};
_Static_assert(offsetof(struct driver_binding_view, supported) == 0U,
	"DriverBinding Supported offset");
_Static_assert(offsetof(struct driver_binding_view, start) == sizeof(void *),
	"DriverBinding Start offset");
_Static_assert(offsetof(struct driver_binding_view, stop) == 2U * sizeof(void *),
	"DriverBinding Stop offset");
_Static_assert(offsetof(struct driver_binding_view, version) == 3U * sizeof(void *),
	"DriverBinding Version offset");
_Static_assert(offsetof(struct driver_binding_view, image) == 4U * sizeof(void *),
	"DriverBinding ImageHandle offset");
_Static_assert(offsetof(struct driver_binding_view, binding) == 5U * sizeof(void *),
	"DriverBinding DriverBindingHandle offset");
_Static_assert(sizeof(struct driver_binding_view) == 6U * sizeof(void *),
	"DriverBinding ABI size");

static EFI_STATUS pci_assignment_hob(struct dxe_services_context *context,
	const struct cdk2_pci_root_bridges_hob **result, UINTN *size)
{
	static const EFI_GUID root_hob_guid = { 0xec4ebacb, 0x2638, 0x416e,
		{ 0xbe, 0x80, 0xe5, 0xfa, 0x4b, 0x51, 0x19, 0x01 } };
	EFI_HOB_GENERIC_HEADER *hob = (void *)context->handoff;
	UINTN end = context->handoff->efi_end_of_hob_list;

	*result = NULL;
	while ((UINTN)hob < end) {
		if (hob->hob_length < sizeof(*hob) || hob->hob_length > end - (UINTN)hob)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_type == EFI_HOB_TYPE_GUID_EXTENSION &&
		    hob->hob_length >= sizeof(EFI_HOB_GUID_TYPE) +
			    sizeof(struct cdk2_pci_root_bridges_hob)) {
			EFI_HOB_GUID_TYPE *guid = (void *)hob;

			if (memcmp(&guid->name, &root_hob_guid, sizeof(root_hob_guid)) == 0) {
				if (*result != NULL)
					return EFI_COMPROMISED_DATA;
				*result = (const void *)(guid + 1);
				*size = hob->hob_length - sizeof(*guid);
			}
		}
		hob = (void *)((UINT8 *)hob + hob->hob_length);
	}
	return *result == NULL ? EFI_NOT_FOUND : EFI_SUCCESS;
}

static EFI_STATUS validate_pci_children(struct dxe_services_context *context)
{
	const struct cdk2_pci_root_bridges_hob *roots;
	const struct cdk2_pci_assignment_record *assignments;
	struct published_pci {
		UINT16 segment;
		UINT8 bus, device, function, secondary, subordinate;
		BOOLEAN assigned, bridge;
	} published[CDK2_DXE_MAX_PROTOCOLS];
	UINTN hob_size, bridge_bytes, published_count = 0U;
	EFI_STATUS status = pci_assignment_hob(context, &roots, &hob_size);

	if (EFI_ERROR(status))
		return status;
	bridge_bytes = sizeof(*roots) + roots->count * sizeof(roots->bridge[0]);
	if (bridge_bytes > roots->header.length || roots->header.length > hob_size ||
	    roots->assignment_count > (roots->header.length - bridge_bytes) /
		sizeof(*assignments))
		return EFI_COMPROMISED_DATA;
	assignments = (const void *)((const UINT8 *)roots + bridge_bytes);
	for (UINTN protocol = 0U; protocol < core.database.protocol_count; protocol++) {
		struct cdk2_dxe_protocol *record = &core.database.protocols[protocol];
		struct cdk2_efi_pci_io_protocol *io;
		struct published_pci *child;
		UINT8 class_code[3];
		UINT32 buses;
		UINTN segment, bus, device, function;
		void *path;

		if (memcmp(&record->guid, &pci_io_protocol, sizeof(pci_io_protocol)) != 0)
			continue;
		if (published_count == ARRAY_SIZE(published))
			return EFI_OUT_OF_RESOURCES;
		io = record->interface;
		child = &published[published_count];
		memset(child, 0, sizeof(*child));
		if (io == NULL || io->get_location == NULL)
			return EFI_COMPROMISED_DATA;
		status = io->get_location(io, &segment, &bus, &device, &function);
		if (!EFI_ERROR(status))
			status = cdk2_dxe_handle(&core.database, record->handle,
						 &device_path_protocol, &path);
		if (EFI_ERROR(status) || segment > UINT16_MAX || bus > UINT8_MAX ||
		    device > 31U || function > 7U || path == NULL)
			return EFI_COMPROMISED_DATA;
		child->segment = (UINT16)segment;
		child->bus = (UINT8)bus;
		child->device = (UINT8)device;
		child->function = (UINT8)function;
		for (UINTN prior = 0U; prior < published_count; prior++)
			if (published[prior].segment == child->segment &&
			    published[prior].bus == child->bus &&
			    published[prior].device == child->device &&
			    published[prior].function == child->function)
				return EFI_COMPROMISED_DATA;
		for (UINTN assignment = 0U; assignment < roots->assignment_count;
		     assignment++)
			if (assignments[assignment].segment == child->segment &&
			    assignments[assignment].bus == child->bus &&
			    assignments[assignment].device == child->device &&
			    assignments[assignment].function == child->function)
				child->assigned = TRUE;
		if (!child->assigned) {
			if (io->pci.read == NULL)
				return EFI_COMPROMISED_DATA;
			status = io->pci.read(io, 0U, 9U, 3U, class_code);
			if (EFI_ERROR(status) || class_code[2] != 6U ||
			    class_code[1] != 4U ||
			    EFI_ERROR(io->pci.read(io, 2U, 0x18U, 1U, &buses)) ||
			    (UINT8)buses != child->bus || (UINT8)(buses >> 8) == 0U ||
			    (UINT8)(buses >> 8) > (UINT8)(buses >> 16))
				return EFI_COMPROMISED_DATA;
			child->secondary = (UINT8)(buses >> 8);
			child->subordinate = (UINT8)(buses >> 16);
			for (UINTN assignment = 0U; assignment < roots->assignment_count;
			     assignment++)
				if (assignments[assignment].segment == child->segment &&
				    assignments[assignment].bus >= child->secondary &&
				    assignments[assignment].bus <= child->subordinate)
					child->bridge = TRUE;
			if (!child->bridge)
				return EFI_COMPROMISED_DATA;
		}
		published_count++;
	}
	for (UINTN left = 0U; left < published_count; left++) {
		if (!published[left].bridge)
			continue;
		for (UINTN right = left + 1U; right < published_count; right++) {
			BOOLEAN left_contains, right_contains;
			const struct published_pci *outer, *inner;

			if (!published[right].bridge ||
			    published[left].segment != published[right].segment ||
			    published[left].subordinate < published[right].secondary ||
			    published[right].subordinate < published[left].secondary)
				continue;
			left_contains = published[left].secondary <=
				published[right].secondary && published[left].subordinate >=
				published[right].subordinate;
			right_contains = published[right].secondary <=
				published[left].secondary && published[right].subordinate >=
				published[left].subordinate;
			if (left_contains == right_contains)
				return EFI_COMPROMISED_DATA;
			outer = left_contains ? &published[left] : &published[right];
			inner = left_contains ? &published[right] : &published[left];
			if (inner->bus < outer->secondary || inner->bus > outer->subordinate)
				return EFI_COMPROMISED_DATA;
		}
	}
	for (UINTN assignment = 0U; assignment < roots->assignment_count; assignment++) {
		BOOLEAN found = FALSE;

		for (UINTN child = 0U; child < published_count; child++)
			if (published[child].segment == assignments[assignment].segment &&
			    published[child].bus == assignments[assignment].bus &&
			    published[child].device == assignments[assignment].device &&
			    published[child].function == assignments[assignment].function)
				found = TRUE;
		if (!found)
			return EFI_COMPROMISED_DATA;
	}
	return EFI_SUCCESS;
}

static uint64_t initialize_pci_enumeration(void *opaque)
{
	struct dxe_services_context *context = opaque;
	void *binding_handles[CDK2_DXE_MAX_DRIVERS];
	UINTN old_binding_count = 0U, binding_count = 0U;
	void *binding_handle = NULL;
	connect_controller_fn *connect = (connect_controller_fn *)
		core.boot_services.connect_controller;
	EFI_STATUS status;

	for (UINTN index = 0U; index < core.database.protocol_count; index++)
		if (memcmp(&core.database.protocols[index].guid,
		    &driver_binding_protocol, sizeof(driver_binding_protocol)) == 0 &&
		    old_binding_count < ARRAY_SIZE(binding_handles))
			binding_handles[old_binding_count++] =
				core.database.protocols[index].handle;
	status = dispatch_required(context, pci_enumerate_drivers,
		ARRAY_SIZE(pci_enumerate_drivers));
	if (EFI_ERROR(status) || connect == NULL)
		return EFI_ERROR(status) ? status : EFI_UNSUPPORTED;
	for (UINTN index = 0U; index < core.database.protocol_count; index++) {
		struct cdk2_dxe_protocol *record = &core.database.protocols[index];
		BOOLEAN old = FALSE;

		if (memcmp(&record->guid, &driver_binding_protocol,
		    sizeof(driver_binding_protocol)) != 0)
			continue;
		for (UINTN prior = 0U; prior < old_binding_count; prior++)
			if (binding_handles[prior] == record->handle)
				old = TRUE;
		if (!old) {
			binding_handle = record->handle;
			binding_count++;
		}
	}
	if (binding_count != 1U)
		return EFI_COMPROMISED_DATA;
	status = linear_pci_roots_ready(&context->pci_roots);
	if (EFI_ERROR(status))
		return status;
	for (UINTN index = 0U; index < context->pci_roots.count; index++) {
		void *drivers[2] = { binding_handle, NULL };

		status = connect(context->pci_roots.handles[index], drivers, NULL, FALSE);
		if (EFI_ERROR(status))
			return status;
	}
	return validate_pci_children(context);
}

static EFI_STATUS dispatch_one_binding(struct dxe_services_context *context,
	const struct required_driver *driver, struct dispatched_binding *result);

struct storage_candidate {
	void *handle;
	UINTN location;
	UINTN intent_order;
	enum storage_backend backend;
};

struct storage_candidates {
	struct storage_candidate candidate[CDK2_DXE_MAX_HANDLES];
	UINTN count;
	BOOLEAN invalid;
};
static struct storage_candidates storage_candidates;

struct storage_driver_bindings {
	struct dispatched_binding nvme, xhci, sata, ahci;
	struct dispatched_binding usb_bus, usb_mass, ata_bus;
	struct dispatched_binding disk, partition, fat;
	BOOLEAN english_ready;
};
static struct storage_driver_bindings storage_bindings;

struct storage_device {
	void *handle;
	const UINT8 *path;
	UINTN path_size;
};

static EFI_STATUS get_storage_binding(struct dxe_services_context *context,
	const struct required_driver *driver, struct dispatched_binding *cache,
	struct dispatched_binding *result);

static enum storage_backend storage_backend_from_class(const UINT8 class_code[3])
{
	if (class_code[2] == 1U && class_code[1] == 8U && class_code[0] == 2U)
		return STORAGE_NVME;
	if (class_code[2] == 0x0cU && class_code[1] == 0x03U &&
	    class_code[0] == 0x30U)
		return STORAGE_USB;
	if (class_code[2] == 1U && class_code[1] == 6U && class_code[0] == 1U)
		return STORAGE_AHCI;
	return STORAGE_NONE;
}

static void consider_storage_candidate(struct storage_candidates *candidates,
	void *handle, const UINT8 class_code[3], UINTN location)
{
	enum storage_backend backend = storage_backend_from_class(class_code);

	if (backend == STORAGE_NONE)
		return;
	for (UINTN index = 0U; index < candidates->count; index++)
		if (candidates->candidate[index].location == location) {
			candidates->invalid = TRUE;
			return;
		}
	if (candidates->count == ARRAY_SIZE(candidates->candidate)) {
		candidates->invalid = TRUE;
		return;
	}
	candidates->candidate[candidates->count++] = (struct storage_candidate) {
		.handle = handle, .location = location,
		.intent_order = MAX_UINTN, .backend = backend };
}

static BOOLEAN storage_candidate_before(const struct storage_candidate *left,
	const struct storage_candidate *right)
{
	if (left->intent_order != right->intent_order)
		return left->intent_order < right->intent_order;
	if (left->intent_order != MAX_UINTN)
		return FALSE;
	if (left->backend != right->backend)
		return left->backend < right->backend;
	return left->location < right->location;
}

static void sort_storage_candidates(struct storage_candidates *candidates)
{
	for (UINTN index = 1U; index < candidates->count; index++) {
		struct storage_candidate selected = candidates->candidate[index];
		UINTN cursor = index;

		while (cursor != 0U && storage_candidate_before(&selected,
		    &candidates->candidate[cursor - 1U])) {
			candidates->candidate[cursor] = candidates->candidate[cursor - 1U];
			cursor--;
		}
		candidates->candidate[cursor] = selected;
	}
}

static enum storage_backend topology_storage_backend(
	const struct cdk2_pci_topology_function *function)
{
	UINT8 class_code[3] = { function->programming_interface,
		function->subclass, function->class_code };

	return storage_backend_from_class(class_code);
}

static EFI_STATUS order_storage_candidates(struct dxe_services_context *context,
	struct storage_candidates *candidates)
{
	const struct cdk2_pci_topology_handoff *topology = NULL;
	EFI_STATUS status;

	if (context == NULL || context->handoff == NULL || candidates == NULL)
		return EFI_INVALID_PARAMETER;
	status = services_pci_topology(context, &topology);
	if (status == EFI_NOT_FOUND) {
		sort_storage_candidates(candidates);
		return EFI_SUCCESS;
	}
	if (status != EFI_SUCCESS)
		return status;
	for (UINTN index = 0U; index < candidates->count; index++) {
		UINTN match;

		for (match = 0U; match < topology->function_count; match++)
			if (topology_function_location(&topology->function[match]) ==
			    candidates->candidate[index].location)
				break;
		if (match == topology->function_count ||
		    (topology->function[match].flags &
		     CDK2_PCI_TOPOLOGY_PATH_ONLY) != 0U ||
		    topology_storage_backend(&topology->function[match]) !=
		    candidates->candidate[index].backend)
			return EFI_COMPROMISED_DATA;
	}
	for (UINTN index = 0U; index < topology->function_count; index++) {
		enum storage_backend backend =
			topology_storage_backend(&topology->function[index]);
		UINTN candidate;

		if (backend == STORAGE_NONE ||
		    (topology->function[index].flags &
		     CDK2_PCI_TOPOLOGY_PATH_ONLY) != 0U)
			continue;
		for (candidate = 0U; candidate < candidates->count; candidate++)
			if (candidates->candidate[candidate].location ==
			    topology_function_location(&topology->function[index]))
				break;
		if (candidate == candidates->count)
			return EFI_COMPROMISED_DATA;
	}
	{
		const struct cdk2_pci_boot_controller *boot = (const void *)
			&topology->function[topology->function_count];

		for (UINTN index = 0U; index < topology->boot_controller_count; index++) {
			const struct cdk2_pci_topology_function *function;
			enum storage_backend backend;
			UINTN location, candidate;

			function = &topology->function[boot[index].topology_index];
			backend = topology_storage_backend(function);
			if (backend == STORAGE_NONE)
				continue;
			location = topology_function_location(function);
			for (candidate = 0U; candidate < candidates->count; candidate++)
				if (candidates->candidate[candidate].location == location)
					break;
			if (candidate == candidates->count ||
			    candidates->candidate[candidate].backend != backend ||
			    candidates->candidate[candidate].intent_order != MAX_UINTN)
				return EFI_COMPROMISED_DATA;
			candidates->candidate[candidate].intent_order = index;
		}
	}
	sort_storage_candidates(candidates);
	return EFI_SUCCESS;
}

static BOOLEAN binding_owns_controller(void *controller, void *binding)
{
	for (UINTN index = 0U; index < core.database.open_count; index++) {
		const struct cdk2_dxe_open *open = &core.database.opens[index];

		if (open->handle == controller && open->agent == binding &&
		    (open->attributes & 0x18U) != 0U)
			return TRUE;
	}
	return FALSE;
}

static EFI_STATUS rollback_disconnect(disconnect_controller_fn *disconnect,
	void *controller, const struct dispatched_binding *binding, EFI_STATUS original)
{
	EFI_STATUS rollback = disconnect(controller, binding->image, NULL);

	if (rollback == EFI_SUCCESS)
		return original;
	/* A rejected Start may leave neither a protocol open nor a connection.
	 * Only that proven absence makes NOT_FOUND a successful rollback. */
	if (rollback == EFI_NOT_FOUND &&
	    !binding_owns_controller(controller, binding->binding) &&
	    !cdk2_dxe_connection_present(controller, binding->image))
		return original;
	return EFI_COMPROMISED_DATA;
}

static EFI_STATUS stage_controller_provider(struct dxe_services_context *context,
	void *controller, const struct required_driver *driver,
	const EFI_GUID *published, const EFI_GUID *ownership,
	struct dispatched_binding *cache, struct staged_storage_provider *staged)
{
	connect_controller_fn *connect = (connect_controller_fn *)
		core.boot_services.connect_controller;
	disconnect_controller_fn *disconnect = (disconnect_controller_fn *)
		core.boot_services.disconnect_controller;
	struct dispatched_binding binding = {0};
	void *interface, *path;
	UINTN path_size, path_span;
	EFI_STATUS status;
	UINTN owned = 0U;

	if (controller == NULL)
		return EFI_NOT_FOUND;
	if (connect == NULL || disconnect == NULL)
		return EFI_UNSUPPORTED;
	status = cdk2_dxe_handle_span(&core.database, controller,
		&device_path_protocol, &path, &path_span);
	if (status != EFI_SUCCESS || path == NULL)
		return EFI_COMPROMISED_DATA;
	status = cdk2_dxe_device_path_size(path,
		path_span < 4096U ? path_span : 4096U, &path_size);
	if (EFI_ERROR(status))
		return EFI_COMPROMISED_DATA;
	status = cdk2_dxe_handle(&core.database, controller, published, &interface);
	if (status == EFI_SUCCESS)
		return EFI_COMPROMISED_DATA;
	if (status != EFI_UNSUPPORTED)
		return status;
	status = get_storage_binding(context, driver, cache, &binding);
	if (EFI_ERROR(status))
		return status;
	{
		void *drivers[2] = { binding.image, NULL };
		status = connect(controller, drivers, NULL, FALSE);
	}
	if (EFI_ERROR(status) || cdk2_dxe_handle(&core.database, controller,
	    published, &interface) != EFI_SUCCESS || interface == NULL) {
		if (!EFI_ERROR(status))
			status = EFI_COMPROMISED_DATA;
		return rollback_disconnect(disconnect, controller, &binding, status);
	}
	for (UINTN index = 0U; index < core.database.open_count; index++) {
		const struct cdk2_dxe_open *open = &core.database.opens[index];

		if (open->handle != controller || open->controller != controller ||
		    open->attributes != 0x10U || memcmp(&open->guid, ownership,
		    sizeof(*ownership)) != 0)
			continue;
		if (open->agent != binding.binding || open->count != 1U)
			return rollback_disconnect(disconnect, controller, &binding,
				EFI_COMPROMISED_DATA);
		owned++;
	}
	if (owned != 1U)
		return rollback_disconnect(disconnect, controller, &binding,
			EFI_COMPROMISED_DATA);
	*staged = (struct staged_storage_provider) {
		controller, binding.image, binding.binding, TRUE };
	return EFI_SUCCESS;
}

static EFI_STATUS unstage_provider(struct staged_storage_provider *provider,
	EFI_STATUS status)
{
	disconnect_controller_fn *disconnect = (disconnect_controller_fn *)
		core.boot_services.disconnect_controller;

	if (provider->controller != NULL && provider->connected) {
		EFI_STATUS cleanup = disconnect == NULL ? EFI_UNSUPPORTED :
			disconnect(provider->controller, provider->image, NULL);
		if (EFI_ERROR(cleanup))
			status = EFI_COMPROMISED_DATA;
	}
	*provider = (struct staged_storage_provider) {0};
	return status;
}

static EFI_STATUS unwind_all_staged_providers(EFI_STATUS status)
{
	status = unstage_provider(&staged_ahci, status);
	status = unstage_provider(&staged_sata, status);
	status = unstage_provider(&staged_xhci, status);
	return unstage_provider(&staged_nvme, status);
}

static uint64_t initialize_storage_controllers(void *opaque)
{
	struct dxe_services_context *context = opaque;
	EFI_STATUS status;

	if (context == NULL)
		return EFI_INVALID_PARAMETER;

	selected_storage_backend = STORAGE_NONE;
	selected_storage_controller = NULL;
	selected_storage_binding = NULL;
	selected_storage_agent = NULL;
	selected_esp = NULL;
	selected_loader_size = 0U;
	memset(selected_loader_path, 0, sizeof(selected_loader_path));
	staged_nvme = staged_xhci = staged_ahci = staged_sata =
		(struct staged_storage_provider) {0};
	storage_candidates = (struct storage_candidates) {0};
	storage_bindings = (struct storage_driver_bindings) {0};

	for (UINTN index = 0U; index < core.database.protocol_count; index++) {
		struct cdk2_dxe_protocol *record = &core.database.protocols[index];
		struct cdk2_efi_pci_io_protocol *io;
		UINT8 class_code[3];
		UINTN segment, bus, device, function, location;

		if (memcmp(&record->guid, &pci_io_protocol, sizeof(pci_io_protocol)) != 0)
			continue;
		io = record->interface;
		if (io == NULL || io->pci.read == NULL || io->get_location == NULL ||
		    EFI_ERROR(io->pci.read(io, 0U, 9U, 3U, class_code)) ||
		    EFI_ERROR(io->get_location(io, &segment, &bus, &device, &function)) ||
		    segment > UINT16_MAX || bus > UINT8_MAX || device > 31U || function > 7U)
			return EFI_COMPROMISED_DATA;
		/* This is only live identity discovery.  The authoritative handoff orders
		 * matching candidates without causing another PCI probe or assignment. */
		location = (segment << 16) | (bus << 8) | (device << 3) | function;
		consider_storage_candidate(&storage_candidates, record->handle, class_code,
			location);
	}
	if (storage_candidates.invalid)
		return EFI_COMPROMISED_DATA;
	status = order_storage_candidates(context, &storage_candidates);
	if (EFI_ERROR(status))
		return status;
	return storage_candidates.count != 0U ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static BOOLEAN binding_was_present(void **handles, UINTN count, void *handle)
{
	for (UINTN index = 0U; index < count; index++)
		if (handles[index] == handle)
			return TRUE;
	return FALSE;
}

static BOOLEAN admitted_driver_image(void *handle)
{
	for (UINTN index = 0U; index < core.images.count; index++) {
		struct cdk2_dxe_image *image = &core.images.images[index];

		if (image->handle == handle && image->load_generation != 0U &&
		    image->base != NULL && image->pages != 0U)
			return TRUE;
	}
	return FALSE;
}

static EFI_STATUS dispatch_binding_set(struct dxe_services_context *context,
	const struct required_driver *driver, UINTN expected, void **image,
	void **sole_binding)
{
	void *old[CDK2_DXE_MAX_DRIVERS];
	UINTN old_count = 0U, found = 0U;
	EFI_STATUS status;

	if (driver == NULL || expected == 0U || image == NULL ||
	    (sole_binding != NULL && expected != 1U))
		return EFI_INVALID_PARAMETER;
	*image = NULL;
	if (sole_binding != NULL)
		*sole_binding = NULL;

	for (UINTN index = 0U; index < core.database.protocol_count; index++)
		if (memcmp(&core.database.protocols[index].guid,
		    &driver_binding_protocol, sizeof(driver_binding_protocol)) == 0) {
			if (old_count == ARRAY_SIZE(old))
				return EFI_OUT_OF_RESOURCES;
			old[old_count++] = core.database.protocols[index].handle;
		}
	status = dispatch_required(context, driver, 1U);
	if (EFI_ERROR(status))
		return status;
	for (UINTN index = 0U; index < core.database.protocol_count; index++) {
		struct cdk2_dxe_protocol *record = &core.database.protocols[index];

		if (memcmp(&record->guid, &driver_binding_protocol,
		    sizeof(driver_binding_protocol)) == 0 &&
		    !binding_was_present(old, old_count, record->handle)) {
			struct driver_binding_view *view = record->interface;

			if (view == NULL || view->supported == NULL || view->start == NULL ||
			    view->stop == NULL || view->image == NULL ||
			    view->binding != record->handle ||
			    !admitted_driver_image(view->image))
				return EFI_COMPROMISED_DATA;
			if (*image != NULL && *image != view->image)
				return EFI_COMPROMISED_DATA;
			*image = view->image;
			if (sole_binding != NULL)
				*sole_binding = view->binding;
			found++;
		}
	}
	return found == expected ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

static EFI_STATUS dispatch_one_binding(struct dxe_services_context *context,
	const struct required_driver *driver, struct dispatched_binding *result)
{
	if (result == NULL)
		return EFI_INVALID_PARAMETER;
	return dispatch_binding_set(context, driver, 1U, &result->image,
		&result->binding);
}

static EFI_STATUS validate_storage_binding(
	const struct dispatched_binding *binding)
{
	struct driver_binding_view *view;
	EFI_STATUS status;

	if (binding == NULL || binding->image == NULL || binding->binding == NULL)
		return EFI_COMPROMISED_DATA;
	status = cdk2_dxe_handle(&core.database, binding->binding,
		&driver_binding_protocol, (void **)&view);
	if (status != EFI_SUCCESS || view == NULL || view->supported == NULL ||
	    view->start == NULL || view->stop == NULL ||
	    view->image != binding->image || view->binding != binding->binding ||
	    !admitted_driver_image(binding->image))
		return EFI_COMPROMISED_DATA;
	return EFI_SUCCESS;
}

static EFI_STATUS get_storage_binding(struct dxe_services_context *context,
	const struct required_driver *driver, struct dispatched_binding *cache,
	struct dispatched_binding *result)
{
	EFI_STATUS status;

	if (cache == NULL || result == NULL)
		return EFI_INVALID_PARAMETER;
	if (cache->image == NULL && cache->binding == NULL) {
		status = dispatch_one_binding(context, driver, cache);
		if (EFI_ERROR(status))
			return status;
	} else if (cache->image == NULL || cache->binding == NULL) {
		return EFI_COMPROMISED_DATA;
	}
	status = validate_storage_binding(cache);
	if (!EFI_ERROR(status))
		*result = *cache;
	return status;
}

static BOOLEAN storage_device_before(const struct storage_device *left,
	const struct storage_device *right)
{
	UINTN compared = left->path_size < right->path_size ?
		left->path_size : right->path_size;
	int order = memcmp(left->path, right->path, compared);

	return order != 0 ? order < 0 : left->path_size < right->path_size;
}

static EFI_STATUS order_storage_devices(void **handles, UINTN count,
	struct storage_device *devices)
{
	if (handles == NULL || count == 0U || devices == NULL)
		return EFI_INVALID_PARAMETER;
	for (UINTN index = 0U; index < count; index++) {
		void *path;
		UINTN path_size, path_span;
		EFI_STATUS status;

		if (handles[index] == NULL)
			return EFI_COMPROMISED_DATA;
		status = cdk2_dxe_handle_span(&core.database, handles[index],
			&device_path_protocol, &path, &path_span);
		if (status != EFI_SUCCESS || path == NULL || path_span < 4U)
			return EFI_COMPROMISED_DATA;
		status = cdk2_dxe_device_path_size(path,
			path_span < 4096U ? path_span : 4096U, &path_size);
		if (EFI_ERROR(status))
			return EFI_COMPROMISED_DATA;
		devices[index] = (struct storage_device) {
			.handle = handles[index], .path = path, .path_size = path_size };
		for (UINTN prior = 0U; prior < index; prior++)
			if (path_size == devices[prior].path_size &&
			    memcmp(path, devices[prior].path, path_size) == 0)
				return EFI_COMPROMISED_DATA;
	}
	for (UINTN index = 1U; index < count; index++) {
		struct storage_device selected = devices[index];
		UINTN position = index;

		while (position != 0U && storage_device_before(&selected,
		    &devices[position - 1U])) {
			devices[position] = devices[position - 1U];
			position--;
		}
		devices[position] = selected;
	}
	return EFI_SUCCESS;
}

static EFI_STATUS prepare_filesystem_binding(struct dxe_services_context *context,
	struct dispatched_binding *binding)
{
	EFI_STATUS status;

	if (!storage_bindings.english_ready) {
		status = dispatch_required(context, &filesystem_drivers[0], 1U);
		if (EFI_ERROR(status))
			return status;
		storage_bindings.english_ready = TRUE;
	}
	return get_storage_binding(context, &filesystem_drivers[1],
		&storage_bindings.fat, binding);
}

static EFI_STATUS select_allowed_loader(
	struct cdk2_fat_simple_fs_protocol *filesystem, CHAR16 *selected_path,
	UINT32 *selected_size)
{
	if (filesystem == NULL || selected_path == NULL || selected_size == NULL)
		return EFI_INVALID_PARAMETER;
	*selected_size = 0U;
	for (UINTN index = 0U; index < CDK2_LINEAR_LOADER_PATH_CHARS; index++)
		selected_path[index] = 0U;
	for (UINTN path = 0U; path < ARRAY_SIZE(linear_loader_paths); path++) {
		EFI_STATUS status = cdk2_dxe_validate_boot_file(filesystem,
			linear_loader_paths[path], selected_size);
		UINTN index = 0U;

		if (status == EFI_NOT_FOUND || status == EFI_UNSUPPORTED)
			continue;
		if (EFI_ERROR(status))
			return status;
		while (index + 1U < CDK2_LINEAR_LOADER_PATH_CHARS &&
		       linear_loader_paths[path][index] != 0U) {
			selected_path[index] = linear_loader_paths[path][index];
			index++;
		}
		if (linear_loader_paths[path][index] != 0U)
			return EFI_COMPROMISED_DATA;
		return EFI_SUCCESS;
	}
	return EFI_NOT_FOUND;
}

struct usb_media_selection {
	struct dispatched_binding bus, mass;
	void *devices[CDK2_DXE_MAX_HANDLES];
	BOOLEAN mass_connected[CDK2_DXE_MAX_HANDLES];
	UINTN device_count;
};

static EFI_STATUS find_deadline_tsc_info(EFI_HOB_HANDOFF_INFO_TABLE *handoff,
	const struct cdk2_deadline_tsc_info **result)
{
	EFI_HOB_GENERIC_HEADER *hob;
	UINTN end;

	if (result == NULL)
		return EFI_INVALID_PARAMETER;
	*result = NULL;
	if (handoff == NULL || handoff->header.hob_type != EFI_HOB_TYPE_HANDOFF ||
	    handoff->header.hob_length != sizeof(*handoff) ||
	    ((UINTN)handoff & 7U) != 0U ||
	    handoff->efi_end_of_hob_list <= (UINTN)handoff ||
	    (handoff->efi_end_of_hob_list & 7U) != 0U)
		return EFI_COMPROMISED_DATA;
	end = handoff->efi_end_of_hob_list;
	for (hob = (void *)handoff; (UINTN)hob <= end;
	     hob = (void *)((UINT8 *)hob + hob->hob_length)) {
		EFI_HOB_GUID_TYPE *guid;

		if (((UINTN)hob & 7U) != 0U || hob->hob_length < sizeof(*hob) ||
		    (hob->hob_length & 7U) != 0U)
			return EFI_COMPROMISED_DATA;
		if ((UINTN)hob == end)
			return hob->hob_type == EFI_HOB_TYPE_END_OF_HOB_LIST &&
				hob->hob_length == sizeof(*hob) ?
				(*result == NULL ? EFI_NOT_FOUND : EFI_SUCCESS) :
				EFI_COMPROMISED_DATA;
		if (hob->hob_type == EFI_HOB_TYPE_END_OF_HOB_LIST ||
		    hob->hob_length > end - (UINTN)hob)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_type != EFI_HOB_TYPE_GUID_EXTENSION)
			continue;
		if (hob->hob_length < sizeof(*guid))
			return EFI_COMPROMISED_DATA;
		guid = (void *)hob;
		if (memcmp(&guid->name, &deadline_tsc_info_guid,
		    sizeof(deadline_tsc_info_guid)) != 0)
			continue;
		if (*result != NULL || hob->hob_length != sizeof(*guid) +
		    sizeof(struct cdk2_deadline_tsc_info))
			return EFI_COMPROMISED_DATA;
		*result = (const void *)(guid + 1);
	}
	return EFI_COMPROMISED_DATA;
}

static EFI_STATUS deadline_start_efi_status(enum cdk2_deadline_start_result result)
{
	switch (result) {
	case CDK2_DEADLINE_START_SUCCESS:
		return EFI_SUCCESS;
	case CDK2_DEADLINE_START_INVALID_ARGUMENT:
		return EFI_INVALID_PARAMETER;
	case CDK2_DEADLINE_START_INVALID_CLOCK:
	case CDK2_DEADLINE_START_OVERFLOW:
		return EFI_COMPROMISED_DATA;
	}
	return EFI_COMPROMISED_DATA;
}

static EFI_STATUS start_usb_discovery_deadline_at(
	struct dxe_services_context *context, void *controller, UINT64 now)
{
	const struct cdk2_deadline_tsc_info *deadline_info = NULL;
	struct cdk2_tsc_info tsc_info;
	enum cdk2_deadline_start_result deadline_result;
	EFI_STATUS status;

	if (context == NULL || controller == NULL)
		return EFI_INVALID_PARAMETER;
	if (usb_deadline_controller != NULL)
		return EFI_ALREADY_STARTED;
	if (CONFIG_CDK2_USB_SCAN_TIMEOUT != 0U) {
		status = find_deadline_tsc_info(context->handoff, &deadline_info);
		if (EFI_ERROR(status)) {
			(void)cdk2_deadline_start(&usb_discovery_deadline, NULL,
				CONFIG_CDK2_USB_SCAN_TIMEOUT, 0U);
			return status;
		}
		if (deadline_info->revision != CDK2_DEADLINE_TSC_INFO_REVISION ||
		    deadline_info->length != sizeof(*deadline_info) ||
		    (deadline_info->source !=
			CDK2_DEADLINE_TSC_SOURCE_COREBOOT_CONSTANT_TSC &&
		     deadline_info->source !=
			CDK2_DEADLINE_TSC_SOURCE_COREBOOT_CBMEM_MEASURED_TSC) ||
		    deadline_info->frequency_hz == 0U) {
			(void)cdk2_deadline_start(&usb_discovery_deadline, NULL,
				CONFIG_CDK2_USB_SCAN_TIMEOUT, 0U);
			return EFI_COMPROMISED_DATA;
		}
		tsc_info = (struct cdk2_tsc_info) {
			.revision = CDK2_TSC_INFO_REVISION,
			.frequency_hz = deadline_info->frequency_hz,
		};
	}
	if (deadline_info != NULL && deadline_info->source ==
	    CDK2_DEADLINE_TSC_SOURCE_COREBOOT_CBMEM_MEASURED_TSC)
		deadline_result = cdk2_deadline_start_measured_tsc(
			&usb_discovery_deadline, &tsc_info,
			CONFIG_CDK2_USB_SCAN_TIMEOUT, now);
	else
		deadline_result = cdk2_deadline_start(
			&usb_discovery_deadline,
			deadline_info == NULL ? NULL : &tsc_info,
			CONFIG_CDK2_USB_SCAN_TIMEOUT, now);
	status = deadline_start_efi_status(deadline_result);
	if (EFI_ERROR(status))
		return status;
	status = cdk2_dxe_restore(&core.database, controller,
		&deadline_protocol_guid, &usb_discovery_deadline);
	if (!EFI_ERROR(status)) {
		usb_deadline_controller = controller;
	} else {
		(void)cdk2_deadline_start(&usb_discovery_deadline, NULL, 0U, 0U);
	}
	return status;
}

static EFI_STATUS start_usb_discovery_deadline(
	struct dxe_services_context *context, void *controller)
{
	return start_usb_discovery_deadline_at(context, controller,
		dxe_linear_clock(NULL));
}

static EFI_STATUS stop_usb_discovery_deadline(void)
{
	void *controller = usb_deadline_controller;
	enum cdk2_deadline_start_result deadline_result;
	EFI_STATUS status;

	/* Disable first: XHCI retains this value pointer while Stop quiesces it. */
	deadline_result = cdk2_deadline_start(
		&usb_discovery_deadline, NULL, 0U, 0U);
	status = deadline_start_efi_status(deadline_result);
	if (controller == NULL)
		return status;
	if (EFI_ERROR(status))
		return status;
	status = cdk2_dxe_uninstall(&core.database, controller,
		&deadline_protocol_guid, &usb_discovery_deadline);
	if (!EFI_ERROR(status))
		usb_deadline_controller = NULL;
	return status;
}

static EFI_STATUS rollback_usb_selection(struct usb_media_selection *selection,
	disconnect_controller_fn *disconnect, EFI_STATUS status)
{
	for (UINTN index = selection->device_count; index != 0U; index--)
		if (selection->mass_connected[index - 1U] &&
		    EFI_ERROR(disconnect(selection->devices[index - 1U],
		    selection->mass.image, NULL)))
			status = EFI_COMPROMISED_DATA;
	if (selection->bus.image != NULL &&
	    EFI_ERROR(disconnect(staged_xhci.controller, selection->bus.image, NULL)))
		status = EFI_COMPROMISED_DATA;
	return status;
}

static EFI_STATUS select_usb_media_impl(struct dxe_services_context *context,
	void **blocks, UINTN *block_count, struct usb_media_selection *selection)
{
	connect_controller_fn *connect = (connect_controller_fn *)
		core.boot_services.connect_controller;
	disconnect_controller_fn *disconnect = (disconnect_controller_fn *)
		core.boot_services.disconnect_controller;
	EFI_STATUS status;
	BOOLEAN timed_out = FALSE;

	cdk2_diag_emit(CDK2_DIAG_INFO, CDK2_DIAG_BDS,
		CDK2_EVENT_BDS_USB_SCAN_BEGIN, EFI_SUCCESS);

	if (staged_xhci.controller == NULL)
		return EFI_NOT_FOUND;
	status = get_storage_binding(context, &usb_bus_driver,
		&storage_bindings.usb_bus, &selection->bus);
	if (EFI_ERROR(status))
		return status;
	{
		void *drivers[2] = { selection->bus.image, NULL };
		status = connect(staged_xhci.controller, drivers, NULL, FALSE);
	}
	if (EFI_ERROR(status))
		return rollback_usb_selection(selection, disconnect, status);
	{
		struct cdk2_xhci_control *control = NULL;

		status = private_xhci_control(&core, staged_xhci.controller,
			staged_xhci.image, &control);
		if (status != EFI_SUCCESS)
			return rollback_usb_selection(selection, disconnect,
				EFI_COMPROMISED_DATA);
		status = control->poll_once(control, staged_xhci.controller);
		if (status != EFI_SUCCESS)
			return rollback_usb_selection(selection, disconnect,
				EFI_ERROR(status) ? status : EFI_DEVICE_ERROR);
	}
	for (UINTN index = 0U; index < core.database.open_count; index++) {
		const struct cdk2_dxe_open *open = &core.database.opens[index];
		void *usb;

		if (open->handle == staged_xhci.controller && open->controller != NULL &&
		    open->attributes == 0x08U && memcmp(&open->guid, &usb2_hc_protocol,
		    sizeof(usb2_hc_protocol)) == 0) {
			if (open->agent != selection->bus.binding ||
			    cdk2_dxe_handle(&core.database, open->controller,
			    &usb_io_protocol, &usb) != EFI_SUCCESS || usb == NULL ||
			    selection->device_count == ARRAY_SIZE(selection->devices))
				return rollback_usb_selection(selection, disconnect,
					EFI_COMPROMISED_DATA);
			for (UINTN prior = 0U; prior < selection->device_count; prior++)
				if (selection->devices[prior] == open->controller)
					return rollback_usb_selection(selection, disconnect,
						EFI_COMPROMISED_DATA);
			selection->devices[selection->device_count++] = open->controller;
		}
	}
	if (selection->device_count == 0U)
		return rollback_usb_selection(selection, disconnect, EFI_NOT_FOUND);
	status = get_storage_binding(context, &usb_mass_driver,
		&storage_bindings.usb_mass, &selection->mass);
	if (EFI_ERROR(status))
		return rollback_usb_selection(selection, disconnect, status);
	for (UINTN device = 0U; device < selection->device_count; device++) {
		void *drivers[2] = { selection->mass.image, NULL };
		status = connect(selection->devices[device], drivers, NULL, FALSE);
		if (status == EFI_TIMEOUT) {
			timed_out = TRUE;
			break;
		}
		if (status == EFI_UNSUPPORTED || status == EFI_NOT_FOUND)
			continue;
		if (EFI_ERROR(status)) {
			if (EFI_ERROR(disconnect(selection->devices[device],
			    selection->mass.image, NULL)))
				status = EFI_COMPROMISED_DATA;
			return rollback_usb_selection(selection, disconnect, status);
		}
		selection->mass_connected[device] = TRUE;
	}
	for (UINTN index = 0U; index < core.database.open_count; index++) {
		const struct cdk2_dxe_open *open = &core.database.opens[index];
		struct cdk2_block_io *block;
		BOOLEAN parent = FALSE;

		if (open->attributes != 0x08U || open->controller == NULL ||
		    memcmp(&open->guid, &usb_io_protocol, sizeof(usb_io_protocol)) != 0)
			continue;
		for (UINTN device = 0U; device < selection->device_count; device++)
			if (open->handle == selection->devices[device])
				parent = TRUE;
		if (!parent)
			continue;
		if (open->agent != selection->mass.binding ||
		    cdk2_dxe_handle(&core.database, open->controller, &block_io_protocol,
		    (void **)&block) != EFI_SUCCESS || block == NULL || block->media == NULL)
			return rollback_usb_selection(selection, disconnect,
				EFI_COMPROMISED_DATA);
		if (!block->media->media_present)
			continue;
		if (*block_count == CDK2_DXE_MAX_HANDLES)
			return rollback_usb_selection(selection, disconnect,
				EFI_OUT_OF_RESOURCES);
		for (UINTN prior = 0U; prior < *block_count; prior++)
			if (blocks[prior] == open->controller)
				return rollback_usb_selection(selection, disconnect,
					EFI_COMPROMISED_DATA);
		blocks[(*block_count)++] = open->controller;
	}
	if (*block_count != 0U) {
		cdk2_diag_emit(CDK2_DIAG_INFO, CDK2_DIAG_BDS,
			CDK2_EVENT_BDS_USB_SCAN_END, EFI_SUCCESS);
		return EFI_SUCCESS;
	}
	return rollback_usb_selection(selection, disconnect,
		timed_out ? EFI_TIMEOUT : EFI_NOT_FOUND);
}

static EFI_STATUS select_usb_media(struct dxe_services_context *context,
	void **blocks, UINTN *block_count, struct usb_media_selection *selection)
{
	EFI_STATUS status;
	UINT64 start = cdk2_diag_now();
	status = select_usb_media_impl(context, blocks, block_count, selection);
	{
		UINT64 end = cdk2_diag_now();
		(void)cdk2_diag_optional_phase_pair_at(CDK2_EVENT_SUBPHASE_USB_SCAN_BEGIN,
			CDK2_EVENT_SUBPHASE_USB_SCAN_END, start, end, "USB_SCAN",
			end >= start ? end - start : 0U, status);
	}
	return status;
}

static EFI_STATUS select_nvme_media(void **blocks, UINTN *block_count)
{
	EFI_STATUS status = cdk2_dxe_validate_nvme_children(&core.database,
		staged_nvme.controller, staged_nvme.agent);

	if (EFI_ERROR(status))
		return status;
	for (UINTN index = 0U; index < core.database.open_count; index++) {
		const struct cdk2_dxe_open *open = &core.database.opens[index];
		struct cdk2_block_io *block;

		if (open->handle != staged_nvme.controller ||
		    open->agent != staged_nvme.agent || open->controller == NULL ||
		    open->attributes != 0x08U || memcmp(&open->guid, &pci_io_protocol,
		    sizeof(pci_io_protocol)) != 0)
			continue;
		if (cdk2_dxe_handle(&core.database, open->controller, &block_io_protocol,
		    (void **)&block) != EFI_SUCCESS || block == NULL || block->media == NULL)
			return EFI_COMPROMISED_DATA;
		if (!block->media->media_present)
			continue;
		if (*block_count == CDK2_DXE_MAX_HANDLES)
			return EFI_OUT_OF_RESOURCES;
		blocks[(*block_count)++] = open->controller;
	}
	return *block_count != 0U ? EFI_SUCCESS : EFI_NOT_FOUND;
}

struct storage_admission {
	void *esp;
	CHAR16 loader_path[CDK2_LINEAR_LOADER_PATH_CHARS];
	UINT32 loader_size;
};

static EFI_STATUS try_storage_device(struct dxe_services_context *context,
	void *device, const struct dispatched_binding *disk_binding,
	const struct dispatched_binding *partition_binding,
	struct storage_admission *admission)
{
	connect_controller_fn *connect = (connect_controller_fn *)
		core.boot_services.connect_controller;
	disconnect_controller_fn *disconnect = (disconnect_controller_fn *)
		core.boot_services.disconnect_controller;
	struct dispatched_binding fat_binding = {0};
	void *esps[CDK2_DXE_MAX_HANDLES];
	UINTN esp_count = 0U;
	BOOLEAN disk_connected = FALSE, partition_connected = FALSE;
	EFI_STATUS status;

	if (context == NULL || device == NULL || disk_binding == NULL ||
	    partition_binding == NULL || admission == NULL || connect == NULL ||
	    disconnect == NULL)
		return EFI_INVALID_PARAMETER;
	*admission = (struct storage_admission) {0};
	{
		void *drivers[2] = { disk_binding->image, NULL };

		status = connect(device, drivers, NULL, FALSE);
	}
	if (status == EFI_NOT_FOUND || status == EFI_UNSUPPORTED) {
		status = EFI_NOT_FOUND;
		goto rollback_device;
	}
	if (EFI_ERROR(status))
		goto rollback_device;
	disk_connected = TRUE;
	{
		void *interface;

		if (cdk2_dxe_handle(&core.database, device, &disk_io_protocol,
		    &interface) != EFI_SUCCESS || interface == NULL) {
			status = EFI_COMPROMISED_DATA;
			goto rollback_device;
		}
	}
	{
		void *drivers[2] = { partition_binding->image, NULL };

		status = connect(device, drivers, NULL, FALSE);
	}
	if (status == EFI_NOT_FOUND || status == EFI_UNSUPPORTED) {
		status = EFI_NOT_FOUND;
		goto rollback_device;
	}
	if (EFI_ERROR(status))
		goto rollback_device;
	partition_connected = TRUE;
	status = cdk2_dxe_device_esps(&core.database, device,
		partition_binding->binding, esps, ARRAY_SIZE(esps), &esp_count);
	if (status == EFI_NOT_FOUND || status == EFI_NO_MEDIA) {
		status = EFI_NOT_FOUND;
		goto rollback_device;
	}
	if (EFI_ERROR(status))
		goto rollback_device;
	status = prepare_filesystem_binding(context, &fat_binding);
	if (EFI_ERROR(status))
		goto rollback_device;
	for (UINTN index = 0U; index < esp_count; index++) {
		struct cdk2_fat_simple_fs_protocol *filesystem;
		void *drivers[2] = { fat_binding.image, NULL };

		status = connect(esps[index], drivers, NULL, FALSE);
		if (status == EFI_NOT_FOUND || status == EFI_UNSUPPORTED)
			continue;
		if (EFI_ERROR(status))
			goto rollback_device;
		status = cdk2_dxe_handle(&core.database, esps[index],
			&simple_fs_protocol, (void **)&filesystem);
		if (status != EFI_SUCCESS || filesystem == NULL) {
			status = EFI_COMPROMISED_DATA;
		} else {
			status = select_allowed_loader(filesystem,
				admission->loader_path, &admission->loader_size);
		}
		if (status == EFI_SUCCESS) {
			admission->esp = esps[index];
			return EFI_SUCCESS;
		}
		status = rollback_disconnect(disconnect, esps[index],
			&fat_binding, status);
		if (status == EFI_NOT_FOUND)
			continue;
		goto rollback_device;
	}
	status = EFI_NOT_FOUND;

rollback_device:
	if (partition_connected)
		status = rollback_disconnect(disconnect, device,
			partition_binding, status);
	if (disk_connected)
		status = rollback_disconnect(disconnect, device, disk_binding,
			status);
	return status;
}

static EFI_STATUS try_storage_candidate(struct dxe_services_context *context,
	const struct storage_candidate *candidate)
{
	enum storage_backend backend = STORAGE_NONE;
	void *selected = NULL;
	void *provider_image = NULL;
	void *provider_agent = NULL;
	void *namespaces[CDK2_DXE_MAX_HANDLES];
	struct storage_device devices[CDK2_DXE_MAX_HANDLES];
	UINTN namespace_count = 0U;
	struct dispatched_binding disk_binding = {0}, partition_binding = {0};
	struct dispatched_binding ata_bus_binding = {0};
	BOOLEAN ata_bus_connected = FALSE;
	struct usb_media_selection usb_selection = {0};
	disconnect_controller_fn *disconnect = (disconnect_controller_fn *)
		core.boot_services.disconnect_controller;
	EFI_STATUS status;

	selected_storage_backend = STORAGE_NONE;
	selected_storage_controller = NULL;
	selected_storage_binding = NULL;
	selected_storage_agent = NULL;
	if (candidate == NULL)
		return EFI_INVALID_PARAMETER;
	if (disconnect == NULL)
		return EFI_UNSUPPORTED;
	/* Media, not mere controller presence, selects the backend.  Only an exact
	 * empty result may fall through to the next candidate.  Stage one provider
	 * at a time so an unused controller never owns hardware or bus mastering. */
	if (candidate->backend == STORAGE_NVME) {
		status = stage_controller_provider(context,
			candidate->handle, storage_controller_drivers,
			&nvme_pass_protocol, &pci_io_protocol,
			&storage_bindings.nvme, &staged_nvme);
		if (EFI_ERROR(status))
			return unwind_all_staged_providers(status);
		status = select_nvme_media(namespaces, &namespace_count);
		if (!EFI_ERROR(status)) {
			backend = STORAGE_NVME;
			selected = staged_nvme.controller;
			provider_image = staged_nvme.image;
			provider_agent = staged_nvme.agent;
		} else if (status != EFI_NOT_FOUND)
			return unwind_all_staged_providers(status);
		else if (unstage_provider(&staged_nvme, EFI_SUCCESS) != EFI_SUCCESS)
			return unwind_all_staged_providers(EFI_COMPROMISED_DATA);
	} else if (candidate->backend == STORAGE_USB) {
		status = start_usb_discovery_deadline(context,
			candidate->handle);
		if (EFI_ERROR(status)) {
			cdk2_diag_emit(CDK2_DIAG_WARN, CDK2_DIAG_BDS,
				CDK2_EVENT_BDS_USB_SCAN_TIMEOUT, status);
		} else {
			status = stage_controller_provider(context,
				candidate->handle, &xhci_controller_driver,
				&usb2_hc_protocol, &pci_io_protocol,
				&storage_bindings.xhci, &staged_xhci);
			if (!EFI_ERROR(status))
				status = select_usb_media(context, namespaces, &namespace_count,
					&usb_selection);
			{
				BOOLEAN timed_out = status == EFI_TIMEOUT ||
					cdk2_deadline_expired_at(&usb_discovery_deadline,
						dxe_linear_clock(NULL));
				EFI_STATUS cleanup = stop_usb_discovery_deadline();

				if (EFI_ERROR(cleanup))
					return unwind_all_staged_providers(EFI_COMPROMISED_DATA);
				if (timed_out)
					cdk2_diag_emit(CDK2_DIAG_WARN, CDK2_DIAG_BDS,
						CDK2_EVENT_BDS_USB_SCAN_TIMEOUT, EFI_TIMEOUT);
			}
			if (!EFI_ERROR(status)) {
				backend = STORAGE_USB;
				selected = staged_xhci.controller;
				provider_image = usb_selection.mass.image;
				provider_agent = usb_selection.mass.binding;
			} else if (status != EFI_NOT_FOUND && status != EFI_TIMEOUT)
				return unwind_all_staged_providers(status);
			else if (unstage_provider(&staged_xhci, EFI_SUCCESS) != EFI_SUCCESS)
				return unwind_all_staged_providers(EFI_COMPROMISED_DATA);
		}
	} else if (candidate->backend == STORAGE_AHCI) {
		status = stage_controller_provider(context,
			candidate->handle, &ahci_controller_drivers[0],
			&ide_init_protocol, &pci_io_protocol,
			&storage_bindings.sata, &staged_sata);
		if (EFI_ERROR(status))
			return unwind_all_staged_providers(status);
		status = stage_controller_provider(context,
			candidate->handle, &ahci_controller_drivers[1],
			&ata_pass_protocol, &ide_init_protocol,
			&storage_bindings.ahci, &staged_ahci);
		if (EFI_ERROR(status))
			return unwind_all_staged_providers(status);
		backend = STORAGE_AHCI;
		selected = staged_ahci.controller;
		provider_image = staged_ahci.image;
		provider_agent = staged_ahci.agent;
	} else
		return EFI_COMPROMISED_DATA;
	if (selected == NULL || provider_image == NULL || provider_agent == NULL)
		return EFI_NOT_FOUND;
	if (backend == STORAGE_AHCI) {
		EFI_STATUS cleanup;
		BOOLEAN owns_controller;

		status = get_storage_binding(context, &ata_bus_driver,
			&storage_bindings.ata_bus, &ata_bus_binding);
		if (EFI_ERROR(status))
			goto rollback_bus;
		status = cdk2_dxe_connect_exact(selected, ata_bus_binding.image, NULL);
		if (status == EFI_SUCCESS) {
			ata_bus_connected = TRUE;
		} else if (status == EFI_ALREADY_STARTED) {
			status = EFI_COMPROMISED_DATA;
		} else {
			owns_controller = binding_owns_controller(selected,
				ata_bus_binding.binding);
			if ((status == EFI_NOT_FOUND || status == EFI_UNSUPPORTED) &&
			    !owns_controller) {
				status = EFI_NOT_FOUND;
			} else {
				cleanup = disconnect(selected, ata_bus_binding.image, NULL);
				if ((cleanup != EFI_SUCCESS && cleanup != EFI_NOT_STARTED) ||
				    binding_owns_controller(selected,
					ata_bus_binding.binding))
					status = EFI_COMPROMISED_DATA;
				else if (status == EFI_UNSUPPORTED)
					status = EFI_NOT_FOUND;
			}
		}
		if (EFI_ERROR(status))
			goto rollback_bus;
	}
	for (UINTN index = 0U; backend == STORAGE_AHCI &&
	    index < core.database.open_count; index++) {
		const struct cdk2_dxe_open *open = &core.database.opens[index];
		struct cdk2_block_io *block_interface;

		const EFI_GUID *parent_protocol = backend == STORAGE_NVME ?
			&pci_io_protocol : &ata_pass_protocol;
		void *expected_agent = backend == STORAGE_NVME ?
			provider_agent : ata_bus_binding.binding;
		if (backend == STORAGE_AHCI && open->handle == selected &&
		    open->attributes == 0x08U && memcmp(&open->guid, &ata_pass_protocol,
		    sizeof(ata_pass_protocol)) == 0 &&
		    open->agent != ata_bus_binding.binding) {
			status = EFI_COMPROMISED_DATA;
			goto rollback_bus;
		}

		if (open->handle != selected || open->controller == NULL ||
		    open->attributes != 0x08U || memcmp(&open->guid, parent_protocol,
			sizeof(*parent_protocol)) != 0)
			continue;
		if (expected_agent != open->agent)
			continue;
		status = cdk2_dxe_handle(&core.database, open->controller,
			&block_io_protocol, (void **)&block_interface);
		if (status != EFI_SUCCESS || block_interface == NULL ||
		    block_interface->media == NULL) {
			status = EFI_COMPROMISED_DATA;
			goto rollback_bus;
		}
		if (!block_interface->media->media_present)
			continue;
		if (namespace_count == ARRAY_SIZE(namespaces)) {
			status = EFI_OUT_OF_RESOURCES;
			goto rollback_bus;
		}
		for (UINTN child = 0U; child < namespace_count; child++)
			if (namespaces[child] == open->controller) {
				status = EFI_COMPROMISED_DATA;
				goto rollback_bus;
			}
		namespaces[namespace_count++] = open->controller;
	}
	if (namespace_count == 0U) {
		status = EFI_NOT_FOUND;
		goto rollback_bus;
	}
	status = order_storage_devices(namespaces, namespace_count, devices);
	if (EFI_ERROR(status))
		goto rollback_bus;
	status = get_storage_binding(context, &block_discovery_drivers[0],
		&storage_bindings.disk, &disk_binding);
	if (EFI_ERROR(status))
		goto rollback_bus;
	status = get_storage_binding(context, &block_discovery_drivers[1],
		&storage_bindings.partition, &partition_binding);
	if (EFI_ERROR(status))
		goto rollback_bus;
	for (UINTN index = 0U; index < namespace_count; index++) {
		struct storage_admission admission;

		status = try_storage_device(context, devices[index].handle,
			&disk_binding, &partition_binding, &admission);
		if (status == EFI_NOT_FOUND)
			continue;
		if (status != EFI_SUCCESS)
			goto rollback_bus;
		selected_storage_backend = backend;
		selected_storage_controller = selected;
		selected_storage_binding = provider_image;
		selected_storage_agent = provider_agent;
		selected_esp = admission.esp;
		selected_loader_size = admission.loader_size;
		for (UINTN character = 0U;
		     character < ARRAY_SIZE(selected_loader_path); character++)
			selected_loader_path[character] = admission.loader_path[character];
		return EFI_SUCCESS;
	}
	status = EFI_NOT_FOUND;
rollback_bus:
	if (ata_bus_binding.image != NULL && ata_bus_connected)
		status = rollback_disconnect(disconnect, selected, &ata_bus_binding,
			status);
	if (backend == STORAGE_AHCI) {
		status = unstage_provider(&staged_ahci, status);
		status = unstage_provider(&staged_sata, status);
	}
	if (backend == STORAGE_USB) {
		status = rollback_usb_selection(&usb_selection, disconnect, status);
		status = unstage_provider(&staged_xhci, status);
	}
	if (backend == STORAGE_NVME) {
		status = unstage_provider(&staged_nvme, status);
	}
	selected_storage_backend = STORAGE_NONE;
	selected_storage_controller = NULL;
	selected_storage_binding = NULL;
	selected_storage_agent = NULL;
	selected_esp = NULL;
	selected_loader_size = 0U;
	memset(selected_loader_path, 0, sizeof(selected_loader_path));
	return status;
}

static BOOLEAN removable_discovery_allowed(
	const struct dxe_services_context *context)
{
	return context != NULL && context->handoff != NULL &&
		context->handoff->boot_mode != BOOT_ON_S4_RESUME;
}

#ifdef CDK2_HOST_TEST
BOOLEAN cdk2_dxe_test_removable_discovery_allowed(EFI_BOOT_MODE boot_mode)
{
	EFI_HOB_HANDOFF_INFO_TABLE handoff = { .boot_mode = boot_mode };
	struct dxe_services_context context = { .handoff = &handoff };

	return removable_discovery_allowed(&context);
}
#endif

static uint64_t initialize_block_discovery(void *opaque)
{
	struct dxe_services_context *context = opaque;
	EFI_STATUS status = EFI_NOT_FOUND;

	if (context == NULL)
		return EFI_INVALID_PARAMETER;
#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME
	if (direct_storage_phase_test_entry)
		goto selected;
#endif
	for (UINTN index = 0U; index < storage_candidates.count; index++) {
		if (storage_candidates.candidate[index].backend == STORAGE_USB &&
		    !removable_discovery_allowed(context))
			continue;
		status = try_storage_candidate(context,
			&storage_candidates.candidate[index]);
		if (status == EFI_SUCCESS) {
#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME
selected:
#endif
#if CDK2_STRICT_DIRECT_RUNTIME
			status = disposition_storage_route(&core.dispatcher,
				selected_storage_backend);
			if (EFI_ERROR(status))
				return status;
#endif
			return EFI_SUCCESS;
		}
		if (status != EFI_NOT_FOUND)
			return status;
	}
	return status;
}

#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME
EFI_STATUS cdk2_dxe_test_initialize_block_discovery(
	struct cdk2_dxe_dispatcher *dispatcher, enum storage_backend backend)
{
	struct dxe_services_context context = {0};
	struct cdk2_dxe_dispatcher saved_dispatcher = core.dispatcher;
	EFI_STATUS status;

	if (dispatcher == NULL)
		return EFI_INVALID_PARAMETER;
	core.dispatcher = *dispatcher;
	selected_storage_backend = backend;
	direct_storage_phase_test_entry = TRUE;
	status = initialize_block_discovery(&context);
	direct_storage_phase_test_entry = FALSE;
	*dispatcher = core.dispatcher;
	core.dispatcher = saved_dispatcher;
	return status;
}
#endif

static uint64_t initialize_filesystems(void *opaque)
{
	struct dxe_services_context *context = opaque;
	struct cdk2_fat_simple_fs_protocol *filesystem;

	if (context == NULL)
		return EFI_INVALID_PARAMETER;
	if (selected_storage_backend == STORAGE_NONE ||
	    selected_storage_controller == NULL || selected_storage_binding == NULL ||
	    selected_storage_agent == NULL || selected_esp == NULL ||
	    selected_loader_size < 512U ||
	    selected_loader_size > 64U * 1024U * 1024U ||
	    selected_loader_path[0] != L'\\' ||
	    selected_loader_path[ARRAY_SIZE(selected_loader_path) - 1U] != 0U)
		return EFI_COMPROMISED_DATA;
	if (cdk2_dxe_handle(&core.database, selected_esp, &simple_fs_protocol,
	    (void **)&filesystem) != EFI_SUCCESS || filesystem == NULL ||
	    filesystem->open_volume == NULL)
		return EFI_COMPROMISED_DATA;
	return EFI_SUCCESS;
}

static EFI_STATUS revalidate_selected_loader(
	struct cdk2_fat_simple_fs_protocol *filesystem)
{
	UINT32 loader_size = 0U;
	EFI_STATUS status;

	if (filesystem == NULL || selected_loader_size == 0U ||
	    selected_loader_path[0] != L'\\' ||
	    selected_loader_path[ARRAY_SIZE(selected_loader_path) - 1U] != 0U)
		return EFI_COMPROMISED_DATA;
	status = cdk2_dxe_validate_boot_file(filesystem, selected_loader_path,
		&loader_size);
	if (status != EFI_SUCCESS)
		return status;
	return loader_size == selected_loader_size ? EFI_SUCCESS :
		EFI_COMPROMISED_DATA;
}

typedef EFI_STATUS CDK2_MS_ABI capsule_get_variable_fn(CHAR16 *, EFI_GUID *,
	UINT32 *, UINTN *, void *);
typedef EFI_STATUS CDK2_MS_ABI capsule_set_variable_fn(CHAR16 *, EFI_GUID *,
	UINT32, UINTN, void *);

static uint64_t read_disk_capsule_request(uint64_t *indications)
{
	static CHAR16 name[] = L"OsIndications";
	EFI_GUID global = { 0x8be4df61, 0x93ca, 0x11d2,
		{ 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c } };
	struct cdk2_dxe_runtime_services *runtime;
	capsule_get_variable_fn *get;
	UINTN size = sizeof(*indications);
	UINT32 attributes = 0U;
	EFI_STATUS status;

	*indications = 0U;
	if (core.runtime_storage == NULL)
		return EFI_UNSUPPORTED;
	runtime = core.runtime_storage->system_table.runtime_services;
	if (runtime == NULL || runtime->get_variable == NULL)
		return EFI_UNSUPPORTED;
	get = runtime->get_variable;
	status = get(name, &global, &attributes, &size, indications);
	if (status != EFI_SUCCESS)
		return status;
	if (size != sizeof(*indications) || attributes != 7U)
		return EFI_COMPROMISED_DATA;
	return (*indications & 4ULL) != 0U ? EFI_SUCCESS : EFI_NOT_FOUND;
}

struct capsule_scan_runtime {
	struct cdk2_capsule_boot_protocol *processor;
	struct cdk2_capsule_disk_result *result;
	struct dxe_services_context *context;
	bool teardown_begun;
	BOOLEAN reset_required;
	BOOLEAN provider_invoked;
};

static uint64_t stage_disk_capsule(struct capsule_scan_runtime *runtime,
	const struct cdk2_capsule_image *images, size_t count);

static EFI_STATUS prefer_failure_status(EFI_STATUS status,
	EFI_STATUS candidate)
{
	if (candidate != EFI_SUCCESS &&
	    (status == EFI_SUCCESS || (!EFI_ERROR(status) && EFI_ERROR(candidate))))
		return candidate;
	return status;
}

static BOOLEAN definitive_capsule_rejection(EFI_STATUS status)
{
	return status == EFI_INVALID_PARAMETER || status == EFI_UNSUPPORTED ||
		status == EFI_BAD_BUFFER_SIZE ||
		status == EFI_INCOMPATIBLE_VERSION ||
		status == EFI_SECURITY_VIOLATION ||
		status == EFI_COMPROMISED_DATA;
}

static EFI_STATUS capsule_disk_completion_status(EFI_STATUS discovery,
	const struct cdk2_capsule_disk_result *result, EFI_STATUS completion,
	BOOLEAN provider_invoked)
{
	/*
	 * CapsuleBoot validation is deliberately structural; the FMP provider
	 * performs target and authentication checks while processing the batch.
	 * A definitive provider rejection is therefore equivalent to finding no
	 * applicable disk capsule.  Keep the file for fwupd to replace or inspect,
	 * retain the original diagnostic, and continue the normal boot.  A scan
	 * ambiguity, verification failure, or transient processing error remains
	 * fatal because it cannot be attributed to a rejected transaction.  Use
	 * the scanner's explicit provenance: equal numeric statuses do not prove
	 * that a later scan error came from the provider.
	 */
	if (result != NULL && result->scan_status == EFI_SUCCESS &&
	    result->completion_status == EFI_SUCCESS &&
	    result->processing_attempted &&
	    provider_invoked && discovery == result->processing_status &&
	    completion == result->processing_status &&
	    definitive_capsule_rejection(result->processing_status))
		return EFI_NOT_FOUND;
	return prefer_failure_status(discovery, completion);
}

static void *capsule_scan_allocate(UINTN size, void *opaque)
{
	typedef EFI_STATUS CDK2_MS_ABI allocate_pool_fn(UINT32, UINTN, void **);
	allocate_pool_fn *allocate = (allocate_pool_fn *)core.boot_services.allocate_pool;
	void *buffer = NULL;

	(void)opaque;
	return allocate != NULL && allocate(4U, size, &buffer) == EFI_SUCCESS ?
		buffer : NULL;
}

static void capsule_scan_free(void *buffer, void *opaque)
{
	typedef EFI_STATUS CDK2_MS_ABI free_pool_fn(void *);
	free_pool_fn *release = (free_pool_fn *)core.boot_services.free_pool;

	(void)opaque;
	if (buffer != NULL && release != NULL)
		(void)release(buffer);
}

static EFI_STATUS capsule_scan_validate(const void *image, UINTN size,
	void *opaque)
{
	struct capsule_scan_runtime *runtime = opaque;

	if (runtime == NULL || runtime->processor == NULL ||
	    runtime->processor->validate_capsule == NULL)
		return EFI_UNSUPPORTED;
	return runtime->processor->validate_capsule(runtime->processor, image, size);
}

static EFI_STATUS capsule_scan_process(const struct cdk2_capsule_image *images,
	UINTN count, void *opaque)
{
	static CHAR16 name[] = L"OsIndications";
	EFI_GUID global = { 0x8be4df61, 0x93ca, 0x11d2,
		{ 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c } };
	struct capsule_scan_runtime *runtime = opaque;
	struct cdk2_dxe_runtime_services *services;
	capsule_set_variable_fn *set;
	uint64_t indications;
	EFI_STATUS status;

	if (runtime == NULL || runtime->result == NULL)
		return EFI_UNSUPPORTED;
	status = read_disk_capsule_request(&indications);
	if (status != EFI_SUCCESS)
		return status == EFI_NOT_FOUND ? EFI_NOT_READY : status;
	/* The scanner may supply a valid prefix after a later read failure.
	 * Preserve the request in that case so the remainder can be retried. */
	if (runtime->result->loaded == runtime->result->candidates &&
	    !runtime->result->cleanup_failed) {
		services = core.runtime_storage->system_table.runtime_services;
		set = services->set_variable;
		if (set == NULL)
			return EFI_UNSUPPORTED;
		indications &= ~4ULL;
		status = set(name, &global, 7U, sizeof(indications), &indications);
		if (status != EFI_SUCCESS)
			return status;
	}
	if (runtime->processor != NULL &&
	    (runtime->processor->delivery_policy.allowed_transports & CB_CAPSULE_DELIVERY_RAM))
		return stage_disk_capsule(runtime, images, count);
	return capsule_batch(runtime->processor, images, count,
		&runtime->reset_required, &runtime->provider_invoked);
}

static void capsule_scan_report(enum cdk2_capsule_disk_event event,
	UINTN source, UINTN file, UINT64 value, EFI_STATUS status, void *opaque)
{
	(void)opaque;
	(void)source;
	(void)file;
	cdk2_diag_emit(EFI_ERROR(status) ? CDK2_DIAG_ERROR : CDK2_DIAG_INFO,
		CDK2_DIAG_DXE_MAIN, 0x1700U + (UINT32)event,
		EFI_ERROR(status) ? status : value);
	CDK2_DXE_DIAG(CDK2_DIAG_DEBUG, CDK2_DXE_155,
		(source << 16) | (file & 0xffffU));
}

static EFI_STATUS reset_after_disk_capsules(
	struct cdk2_dxe_system_table *system)
{
	typedef void CDK2_MS_ABI reset_fn(UINT32, EFI_STATUS, UINTN, void *);
	struct cdk2_dxe_runtime_services *runtime;
	reset_fn *reset;

	if (system == NULL || system->runtime_services == NULL)
		return EFI_COMPROMISED_DATA;
	runtime = system->runtime_services;
	reset = (reset_fn *)runtime->reset_system;
	if (reset == NULL)
		return EFI_UNSUPPORTED;
	reset(1U, EFI_SUCCESS, 0U, NULL);
	return EFI_DEVICE_ERROR;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_capsule_reset(
	struct cdk2_dxe_system_table *system)
{
	return reset_after_disk_capsules(system);
}

EFI_STATUS cdk2_dxe_test_capsule_disk_completion_status(
	EFI_STATUS discovery, const struct cdk2_capsule_disk_result *result,
	EFI_STATUS completion, BOOLEAN provider_invoked)
{
	return capsule_disk_completion_status(discovery, result, completion,
		provider_invoked);
}
#endif

static const EFI_GUID capsule_boot_protocol_guid = { 0x2890d42b, 0x7f9b, 0x4a66,
	{ 0x9a, 0x12, 0x54, 0x8d, 0x6b, 0x57, 0xd8, 0x20 } };

static uint64_t process_disk_capsules(void *opaque)
{
	struct dxe_services_context *context = opaque;
	struct capsule_scan_runtime runtime = {0};
	struct cdk2_fat_simple_fs_protocol *filesystem;
	struct cdk2_capsule_disk_result result = {0};
	uint64_t indications;
	uint8_t disk_required;
	BOOLEAN completion_succeeded = FALSE;
	EFI_STATUS discovery, status;

	if (context == NULL || context->linear_state == NULL)
		return EFI_COMPROMISED_DATA;
	status = cdk2_linear_capsule_disk_required(context->linear_state,
		&disk_required);
	if (EFI_ERROR(status))
		return status;
	if (!disk_required)
		return EFI_SUCCESS;
	status = read_disk_capsule_request(&indications);
	if (status == EFI_NOT_FOUND)
		return cdk2_linear_capsule_finalize_disk(context->linear_state,
			EFI_NOT_FOUND, &context->capsule_outcome);
	if (status != EFI_SUCCESS)
		return status;
	if (selected_esp == NULL)
		return EFI_COMPROMISED_DATA;
	if (cdk2_dxe_handle(&core.database, selected_esp, &simple_fs_protocol,
	    (void **)&filesystem) != EFI_SUCCESS)
		return EFI_COMPROMISED_DATA;
	for (UINTN index = 0U; index < core.database.protocol_count; index++)
		if (memcmp(&core.database.protocols[index].guid, &capsule_boot_protocol_guid,
		    sizeof(capsule_boot_protocol_guid)) == 0) {
			if (runtime.processor != NULL)
				return EFI_COMPROMISED_DATA;
			runtime.processor = core.database.protocols[index].interface;
		}
	if (runtime.processor == NULL)
		return EFI_NOT_FOUND;
	runtime.context = context;
	if (!(runtime.processor->delivery_policy.allowed_transports & CB_CAPSULE_DELIVERY_DISK))
		return EFI_UNSUPPORTED;
	if (runtime.processor->validate_capsule == NULL ||
	    runtime.processor->process_scatter == NULL)
		return EFI_COMPROMISED_DATA;
	runtime.result = &result;
	discovery = cdk2_capsule_disk_scan_esp(filesystem,
		runtime.processor->delivery_policy.max_nonpopulate,
		runtime.processor->delivery_policy.max_populate,
		capsule_scan_validate, capsule_scan_process,
		capsule_scan_allocate, capsule_scan_free, capsule_scan_report, &runtime,
		&disk_capsules, &result);
	/* An irreversible controller stop forbids reopening the source filesystem.
	 * Successful INITIATE_RESET never returns; every returned stage is fatal. */
	if (runtime.teardown_begun)
		return EFI_ERROR(discovery) ? discovery : EFI_DEVICE_ERROR;
	if (result.transaction != NULL) {
		EFI_STATUS completion = cdk2_capsule_disk_complete_esp(filesystem,
			capsule_scan_allocate, capsule_scan_free, capsule_scan_report,
			&runtime, &disk_capsules, &result);

		completion_succeeded = completion == EFI_SUCCESS;
		discovery = capsule_disk_completion_status(discovery,
			&result, completion, runtime.provider_invoked);
	}
	if (runtime.reset_required && completion_succeeded)
		result.reset_required = TRUE;
	if (result.reset_required && completion_succeeded)
		return reset_after_disk_capsules(core.runtime_storage == NULL ? NULL :
			&core.runtime_storage->system_table);
	status = cdk2_linear_capsule_finalize_disk(context->linear_state, discovery,
		&context->capsule_outcome);
	if (status != EFI_SUCCESS)
		return status;
	return EFI_SUCCESS;
}

static void *CDK2_MS_ABI display_allocate(UINTN size)
{
	typedef EFI_STATUS CDK2_MS_ABI allocate_pool_fn(UINT32, UINTN, void **);
	void *buffer = NULL;
	allocate_pool_fn *allocate = (allocate_pool_fn *)core.boot_services.allocate_pool;

	return allocate != NULL && allocate(4U, size, &buffer) == EFI_SUCCESS ?
		buffer : NULL;
}

static void CDK2_MS_ABI display_free(void *buffer)
{
	typedef EFI_STATUS CDK2_MS_ABI free_pool_fn(void *);
	free_pool_fn *release = (free_pool_fn *)core.boot_services.free_pool;

	if (release != NULL)
		(void)release(buffer);
}

#if defined(__x86_64__)
#define DISPLAY_VA_LIST __builtin_ms_va_list
#define DISPLAY_VA_START(list, last) __builtin_ms_va_start(list, last)
#define DISPLAY_VA_END(list) __builtin_ms_va_end(list)
#define DISPLAY_VA_ARG(list, type) __builtin_va_arg(list, type)
#else
#define DISPLAY_VA_LIST va_list
#define DISPLAY_VA_START(list, last) va_start(list, last)
#define DISPLAY_VA_END(list) va_end(list)
#define DISPLAY_VA_ARG(list, type) va_arg(list, type)
#endif

static EFI_STATUS CDK2_MS_ABI display_install(void **handle,
	const EFI_GUID *first_guid, void *first_interface, ...)
{
	const EFI_GUID *second_guid;
	void *second_interface;
	EFI_STATUS status;
	DISPLAY_VA_LIST arguments;

	DISPLAY_VA_START(arguments, first_interface);
	second_guid = DISPLAY_VA_ARG(arguments, const EFI_GUID *);
	second_interface = DISPLAY_VA_ARG(arguments, void *);
	DISPLAY_VA_END(arguments);
	if (second_guid == NULL)
		return EFI_INVALID_PARAMETER;
	status = cdk2_dxe_install(&core.database, handle, first_guid,
		first_interface);
	if (!EFI_ERROR(status) && memcmp(first_guid, &device_path_protocol,
	    sizeof(*first_guid)) == 0)
		status = cdk2_dxe_protocol_span_set_owned(&core.database, *handle,
			first_guid, first_interface);
	if (!EFI_ERROR(status)) {
		status = cdk2_dxe_install(&core.database, handle, second_guid,
			second_interface);
		if (EFI_ERROR(status))
			(void)cdk2_dxe_uninstall(&core.database, *handle,
				first_guid, first_interface);
	} else {
		(void)cdk2_dxe_uninstall(&core.database, *handle, first_guid,
			first_interface);
	}
	return status;
}

#ifdef CDK2_HOST_TEST
static EFI_STATUS display_test_first_remove_failure;
static EFI_STATUS display_test_rollback_failure;
static UINTN display_test_first_removals;
static UINTN display_test_second_removals;
static UINTN display_test_rollbacks;
#endif

static EFI_STATUS display_remove(void *handle, const EFI_GUID *guid,
	void *interface, BOOLEAN first)
{
	EFI_STATUS status;

#ifndef CDK2_HOST_TEST
	(void)first;
#endif
#ifdef CDK2_HOST_TEST
	if (first && EFI_ERROR(display_test_first_remove_failure))
		return display_test_first_remove_failure;
#endif
	status = cdk2_dxe_uninstall(&core.database, handle, guid, interface);
#ifdef CDK2_HOST_TEST
	if (!EFI_ERROR(status)) {
		if (first)
			display_test_first_removals++;
		else
			display_test_second_removals++;
	}
#endif
	return status;
}

static EFI_STATUS display_restore(void **handle, const EFI_GUID *guid,
	void *interface)
{
#ifdef CDK2_HOST_TEST
	display_test_rollbacks++;
	if (EFI_ERROR(display_test_rollback_failure))
		return display_test_rollback_failure;
#endif
	return cdk2_dxe_restore(&core.database, *handle, guid, interface);
}

static EFI_STATUS CDK2_MS_ABI display_uninstall(void *handle,
	const EFI_GUID *first_guid, void *first_interface, ...)
{
	const EFI_GUID *second_guid;
	void *second_interface;
	EFI_STATUS rollback;
	BOOLEAN second_removed;
	EFI_STATUS status;
	DISPLAY_VA_LIST arguments;

	DISPLAY_VA_START(arguments, first_interface);
	second_guid = DISPLAY_VA_ARG(arguments, const EFI_GUID *);
	second_interface = DISPLAY_VA_ARG(arguments, void *);
	DISPLAY_VA_END(arguments);
	status = display_remove(handle, second_guid, second_interface, FALSE);
	second_removed = !EFI_ERROR(status);
	if (EFI_ERROR(status) && status != EFI_NOT_FOUND)
		return status;
	status = display_remove(handle, first_guid, first_interface, TRUE);
	if (!EFI_ERROR(status) || !second_removed)
		return status;
	rollback = display_restore(&handle, second_guid, second_interface);
	return EFI_ERROR(rollback) ? rollback : status;
}

#ifdef CDK2_HOST_TEST
static EFI_STATUS display_test_span(const void *address, UINTN *size,
	UINT64 *generation, void *context)
{
	struct cdk2_dxe_core *dxe_core = context;

	return cdk2_dxe_memory_readable_span(&dxe_core->memory, address, size,
		generation);
}

EFI_STATUS cdk2_dxe_display_variadic_test(void)
{
	typedef EFI_STATUS CDK2_MS_ABI install_fn(void **, const EFI_GUID *,
		void *, ...);
	typedef EFI_STATUS CDK2_MS_ABI uninstall_fn(void *, const EFI_GUID *,
		void *, ...);
	static UINT8 device_path_storage[8192] __aligned(4096);
	void *device_path_interface;
	static UINT8 graphics_interface;
	void *interface = NULL;
	void *handle = NULL;
	EFI_STATUS status;

#define DISPLAY_PRESENT(guid, expected) \
	(cdk2_dxe_handle(&core.database, handle, (guid), &interface) == \
	 EFI_SUCCESS && interface == (expected))
#define DISPLAY_ABSENT(guid) \
	(cdk2_dxe_handle(&core.database, handle, (guid), &interface) == \
	 EFI_UNSUPPORTED)

	status = cdk2_dxe_database_initialize(&core.database);
	if (EFI_ERROR(status) || EFI_ERROR(cdk2_dxe_memory_initialize(&core.memory,
	    device_path_storage, 2U)) || EFI_ERROR(cdk2_dxe_database_set_span_resolver(
	    &core.database, display_test_span, &core)) || EFI_ERROR(cdk2_dxe_allocate_pool(
	    &core.memory, 4U, 16U, &device_path_interface)))
		return status;
	status = ((install_fn *)display_install)(&handle, &device_path_protocol,
		device_path_interface, &graphics_output_protocol, &graphics_interface,
		NULL);
	if (EFI_ERROR(status))
		return status;
	status = cdk2_dxe_handle(&core.database, handle, &device_path_protocol,
		&interface);
	if (EFI_ERROR(status) || interface != device_path_interface)
		return EFI_DEVICE_ERROR;
	status = cdk2_dxe_handle(&core.database, handle, &graphics_output_protocol,
		&interface);
	if (EFI_ERROR(status) || interface != &graphics_interface)
		return EFI_DEVICE_ERROR;
	display_test_first_remove_failure = EFI_DEVICE_ERROR;
	display_test_rollback_failure = EFI_SUCCESS;
	display_test_first_removals = 0U;
	display_test_second_removals = 0U;
	display_test_rollbacks = 0U;
	status = ((uninstall_fn *)display_uninstall)(handle, &device_path_protocol,
		device_path_interface, &graphics_output_protocol, &graphics_interface,
		NULL);
	if (status != EFI_DEVICE_ERROR ||
	    !DISPLAY_PRESENT(&device_path_protocol, device_path_interface) ||
	    !DISPLAY_PRESENT(&graphics_output_protocol, &graphics_interface) ||
	    display_test_first_removals != 0U ||
	    display_test_second_removals != 1U || display_test_rollbacks != 1U)
		return EFI_DEVICE_ERROR;
	display_test_first_remove_failure = EFI_SUCCESS;
	status = ((uninstall_fn *)display_uninstall)(handle, &device_path_protocol,
		device_path_interface, &graphics_output_protocol, &graphics_interface,
		NULL);
	if (EFI_ERROR(status) || !DISPLAY_ABSENT(&graphics_output_protocol) ||
	    !DISPLAY_ABSENT(&device_path_protocol) ||
	    display_test_first_removals != 1U ||
	    display_test_second_removals != 2U)
		return EFI_DEVICE_ERROR;

	status = cdk2_dxe_database_initialize(&core.database);
	handle = NULL;
	if (EFI_ERROR(status))
		return status;
	status = ((install_fn *)display_install)(&handle, &device_path_protocol,
		device_path_interface, &graphics_output_protocol, &graphics_interface,
		NULL);
	if (EFI_ERROR(status))
		return status;
	display_test_first_remove_failure = EFI_DEVICE_ERROR;
	display_test_rollback_failure = EFI_OUT_OF_RESOURCES;
	display_test_first_removals = 0U;
	display_test_second_removals = 0U;
	display_test_rollbacks = 0U;
	status = ((uninstall_fn *)display_uninstall)(handle, &device_path_protocol,
		device_path_interface, &graphics_output_protocol, &graphics_interface,
		NULL);
	if (status != EFI_OUT_OF_RESOURCES ||
	    !DISPLAY_PRESENT(&device_path_protocol, device_path_interface) ||
	    !DISPLAY_ABSENT(&graphics_output_protocol) ||
	    display_test_second_removals != 1U || display_test_rollbacks != 1U)
		return EFI_DEVICE_ERROR;
	display_test_first_remove_failure = EFI_SUCCESS;
	display_test_rollback_failure = EFI_SUCCESS;
	status = ((uninstall_fn *)display_uninstall)(handle, &device_path_protocol,
		device_path_interface, &graphics_output_protocol, &graphics_interface,
		NULL);
	if (EFI_ERROR(status) || !DISPLAY_ABSENT(&graphics_output_protocol) ||
	    !DISPLAY_ABSENT(&device_path_protocol) ||
	    display_test_first_removals != 1U ||
	    display_test_second_removals != 1U || display_test_rollbacks != 1U)
		return EFI_DEVICE_ERROR;
#undef DISPLAY_ABSENT
#undef DISPLAY_PRESENT
	return EFI_SUCCESS;
}
#endif

static EFI_STATUS CDK2_MS_ABI display_close_event(void *event)
{
	(void)event;
	return EFI_SUCCESS;
}

/* cdk2_graphics_adopt() retains this table for the adopted child's lifetime. */
static struct cdk2_graphics_services display_services = {
	.install = display_install,
	.uninstall = display_uninstall,
	.close_event = display_close_event,
	.allocate = display_allocate,
	.free = display_free,
};

static EFI_STATUS find_delegated_framebuffer(EFI_HOB_HANDOFF_INFO_TABLE handoff[],
	const struct cdk2_linear_state *state,
	const EFI_PEI_GRAPHICS_INFO_HOB **graphics)
{
	EFI_HOB_GENERIC_HEADER *hob = (void *)handoff;
	UINTN end = handoff->efi_end_of_hob_list;

	*graphics = NULL;
	while ((UINTN)hob < end) {
		if (hob->hob_length < sizeof(*hob) || (hob->hob_length & 7U) != 0U ||
		    hob->hob_length > end - (UINTN)hob)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_type == EFI_HOB_TYPE_GUID_EXTENSION &&
		    hob->hob_length >= sizeof(EFI_HOB_GUID_TYPE) +
			    sizeof(EFI_PEI_GRAPHICS_INFO_HOB)) {
			EFI_HOB_GUID_TYPE *guid = (void *)hob;
			const EFI_PEI_GRAPHICS_INFO_HOB *candidate = (void *)(guid + 1);

			if (memcmp(&guid->name, &graphics_hob_guid,
			    sizeof(guid->name)) == 0) {
				if (*graphics != NULL)
					return EFI_COMPROMISED_DATA;
				*graphics = candidate;
			}
		}
		hob = (void *)((UINT8 *)hob + hob->hob_length);
	}
	if (*graphics == NULL)
		return EFI_NOT_FOUND;
	if ((*graphics)->frame_buffer_base != state->splash_framebuffer ||
	    (*graphics)->graphics_mode.horizontal_resolution !=
		state->splash_x_resolution ||
	    (*graphics)->graphics_mode.vertical_resolution !=
		state->splash_y_resolution ||
	    (*graphics)->graphics_mode.pixels_per_scan_line * 4ULL !=
		state->splash_bytes_per_line)
		return EFI_COMPROMISED_DATA;
	return EFI_SUCCESS;
}

static EFI_STATUS pixel_mask_layout(UINT32 mask, UINT8 *position, UINT8 *size)
{
	UINT8 shift = 0U;

	if (mask == 0U || position == NULL || size == NULL)
		return EFI_UNSUPPORTED;
	while ((mask & 1U) == 0U) {
		mask >>= 1;
		shift++;
	}
	*position = shift;
	*size = 0U;
	while ((mask & 1U) != 0U) {
		mask >>= 1;
		(*size)++;
	}
	return mask == 0U && *size <= 8U ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

static EFI_STATUS boot_logo_surface(const EFI_PEI_GRAPHICS_INFO_HOB *graphics,
	struct cdk2_boot_logo_surface *surface)
{
	const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode;
	EFI_STATUS status;

	if (graphics == NULL || surface == NULL ||
	    graphics->frame_buffer_base > MAX_UINTN ||
	    graphics->graphics_mode.horizontal_resolution == 0U ||
	    graphics->graphics_mode.vertical_resolution == 0U ||
	    graphics->graphics_mode.pixels_per_scan_line <
		graphics->graphics_mode.horizontal_resolution)
		return EFI_COMPROMISED_DATA;
	mode = &graphics->graphics_mode;
	*surface = (struct cdk2_boot_logo_surface) {
		.pixels = (UINT32 *)(UINTN)graphics->frame_buffer_base,
		.pixels_per_line = mode->pixels_per_scan_line,
		.width = mode->horizontal_resolution,
		.height = mode->vertical_resolution,
		.reserved_mask_pos = 24U,
		.reserved_mask_size = 8U,
	};
	switch (mode->pixel_format) {
	case pixel_blue_green_red_reserved8_bit_per_color:
		surface->red_mask_pos = 16U;
		surface->green_mask_pos = 8U;
		surface->blue_mask_pos = 0U;
		surface->red_mask_size = 8U;
		surface->green_mask_size = 8U;
		surface->blue_mask_size = 8U;
		return EFI_SUCCESS;
	case pixel_red_green_blue_reserved8_bit_per_color:
		surface->red_mask_pos = 0U;
		surface->green_mask_pos = 8U;
		surface->blue_mask_pos = 16U;
		surface->red_mask_size = 8U;
		surface->green_mask_size = 8U;
		surface->blue_mask_size = 8U;
		return EFI_SUCCESS;
	case pixel_bit_mask:
		status = pixel_mask_layout(mode->pixel_information.red_mask,
			&surface->red_mask_pos, &surface->red_mask_size);
		if (!EFI_ERROR(status))
			status = pixel_mask_layout(mode->pixel_information.green_mask,
				&surface->green_mask_pos, &surface->green_mask_size);
		if (!EFI_ERROR(status))
			status = pixel_mask_layout(mode->pixel_information.blue_mask,
				&surface->blue_mask_pos, &surface->blue_mask_size);
		if (!EFI_ERROR(status) && mode->pixel_information.reserved_mask != 0U)
			status = pixel_mask_layout(mode->pixel_information.reserved_mask,
				&surface->reserved_mask_pos, &surface->reserved_mask_size);
		return status;
	default:
		return EFI_UNSUPPORTED;
	}
}

static bool boot_graphics_region_valid(const struct cdk2_boot_logo_surface *surface,
	const struct cdk2_boot_logo_layout *rectangle)
{
	uint64_t row_size, bytes;
	uintptr_t base;

	if (surface == NULL || rectangle == NULL || surface->pixels == NULL ||
	    surface->width == 0 || surface->height == 0 ||
	    surface->pixels_per_line < surface->width || rectangle->width == 0 ||
	    rectangle->height == 0 || rectangle->x >= surface->width ||
	    rectangle->width > surface->width - rectangle->x ||
	    rectangle->y >= surface->height ||
	    rectangle->height > surface->height - rectangle->y)
		return false;
	row_size = (uint64_t)surface->pixels_per_line * sizeof(uint32_t);
	if (surface->height > UINT64_MAX / row_size)
		return false;
	bytes = row_size * surface->height;
	base = (uintptr_t)surface->pixels;
	return (base & (_Alignof(uint32_t) - 1U)) == 0 &&
		bytes - 1U <= UINTPTR_MAX - base;
}

static bool boot_graphics_surface_matches(const struct cdk2_boot_logo_surface *left,
	const struct cdk2_boot_logo_surface *right)
{
	return left->pixels == right->pixels && left->width == right->width &&
		left->height == right->height && left->pixels_per_line == right->pixels_per_line &&
		left->red_mask_pos == right->red_mask_pos && left->red_mask_size == right->red_mask_size &&
		left->green_mask_pos == right->green_mask_pos &&
		left->green_mask_size == right->green_mask_size &&
		left->blue_mask_pos == right->blue_mask_pos && left->blue_mask_size == right->blue_mask_size &&
		left->reserved_mask_pos == right->reserved_mask_pos &&
		left->reserved_mask_size == right->reserved_mask_size;
}

static EFI_STATUS restore_boot_graphics_region(const struct cdk2_boot_logo_surface *surface,
	const struct boot_graphics_snapshot *snapshot)
{
	const struct cdk2_boot_logo_layout *rectangle;
	size_t row_size;
	bool changed = false;

	if (snapshot == NULL || snapshot->pixels == NULL)
		return EFI_INVALID_PARAMETER;
	rectangle = &snapshot->rectangle;
	if (!boot_graphics_region_valid(surface, rectangle) ||
	    !boot_graphics_surface_matches(surface, &snapshot->framebuffer) ||
	    sizeof(uint32_t) > SIZE_MAX / rectangle->width)
		return EFI_COMPROMISED_DATA;
	row_size = (size_t)rectangle->width * sizeof(uint32_t);
	if (rectangle->height > SIZE_MAX / row_size ||
	    row_size * rectangle->height != snapshot->size)
		return EFI_COMPROMISED_DATA;
	for (uint32_t row = 0; row < rectangle->height; row++)
		if (memcmp(surface->pixels + (size_t)(rectangle->y + row) *
		    surface->pixels_per_line + rectangle->x,
		    (uint8_t *)snapshot->pixels + (size_t)row * row_size, row_size)) {
			changed = true;
			break;
		}
	if (changed)
		for (uint32_t row = 0; row < rectangle->height; row++)
			memcpy(surface->pixels + (size_t)(rectangle->y + row) *
				surface->pixels_per_line + rectangle->x,
				(uint8_t *)snapshot->pixels + (size_t)row * row_size, row_size);
	return EFI_SUCCESS;
}

static EFI_STATUS release_boot_graphics_snapshot(struct boot_graphics_snapshot *snapshot)
{
	typedef EFI_STATUS CDK2_MS_ABI free_pool_fn(void *);
	free_pool_fn *release = (free_pool_fn *)core.boot_services.free_pool;
	EFI_STATUS status;

	if (snapshot->pixels == NULL)
		return EFI_SUCCESS;
	if (release == NULL)
		return EFI_INVALID_PARAMETER;
	status = release(snapshot->pixels);
	*snapshot = (struct boot_graphics_snapshot) {0};
	return status;
}

static EFI_STATUS release_boot_graphics_snapshots(struct dxe_services_context *context)
{
	EFI_STATUS splash = release_boot_graphics_snapshot(&context->splash_snapshot);
	EFI_STATUS status = release_boot_graphics_snapshot(&context->status_snapshot);

	return EFI_ERROR(splash) ? splash : status;
}

#if CONFIG_CDK2_LINEAR_BDS_FAILOVER || CONFIG_CDK2_LVGL_RENDERER
static EFI_STATUS capture_boot_graphics_region(const struct cdk2_boot_logo_surface *surface,
	const struct cdk2_boot_logo_layout *rectangle, struct boot_graphics_snapshot *snapshot)
{
	typedef EFI_STATUS CDK2_MS_ABI allocate_pool_fn(UINT32, UINTN, void **);
	allocate_pool_fn *allocate = (allocate_pool_fn *)core.boot_services.allocate_pool;
	struct boot_graphics_snapshot captured;
	size_t row_size;
	EFI_STATUS status;

	if (snapshot == NULL || allocate == NULL || !boot_graphics_region_valid(surface, rectangle) ||
	    sizeof(uint32_t) > SIZE_MAX / rectangle->width)
		return EFI_INVALID_PARAMETER;
	row_size = (size_t)rectangle->width * sizeof(uint32_t);
	if (rectangle->height > SIZE_MAX / row_size)
		return EFI_BAD_BUFFER_SIZE;
	if (snapshot->pixels != NULL)
		return boot_graphics_surface_matches(surface, &snapshot->framebuffer) &&
			rectangle->x == snapshot->rectangle.x && rectangle->y == snapshot->rectangle.y &&
			rectangle->width == snapshot->rectangle.width &&
			rectangle->height == snapshot->rectangle.height &&
			snapshot->size == row_size * rectangle->height ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
	captured = (struct boot_graphics_snapshot) { .framebuffer = *surface,
		.rectangle = *rectangle, .size = row_size * rectangle->height };
	status = allocate(4U, captured.size, &captured.pixels);
	if (EFI_ERROR(status))
		return status;
	if (captured.pixels == NULL)
		return EFI_COMPROMISED_DATA;
	for (uint32_t row = 0; row < rectangle->height; row++)
		memcpy((uint8_t *)captured.pixels + (size_t)row * row_size,
			surface->pixels + (size_t)(rectangle->y + row) *
				surface->pixels_per_line + rectangle->x, row_size);
	*snapshot = captured;
	return EFI_SUCCESS;
}
#endif

static UINT8 acpi_checksum(const void *buffer, UINTN size)
{
	const UINT8 *bytes = buffer;
	UINT8 sum = 0U;

	for (UINTN index = 0U; index < size; index++)
		sum = (UINT8)(sum + bytes[index]);
	return (UINT8)(0U - sum);
}

struct boot_logo_publication {
	EFI_STATUS display;
	EFI_STATUS bgrt;
	EFI_STATUS snapshot;
};

#ifdef CDK2_HOST_TEST
static EFI_STATUS boot_logo_display_test_status = EFI_SUCCESS;
static BOOLEAN handoff_boot_logo_test_entry;
static BOOLEAN handoff_direct_gate_test_entry;
#if CDK2_STRICT_DIRECT_RUNTIME
static BOOLEAN direct_input_phase_test_entry;
#endif
static UINTN loader_activation_test_calls;
#endif

static struct boot_logo_publication publish_boot_logo(
	struct dxe_services_context *context)
{
	typedef EFI_STATUS CDK2_MS_ABI locate_protocol_fn(const EFI_GUID *,
		void *, void **);
	typedef EFI_STATUS CDK2_MS_ABI allocate_pool_fn(UINT32, UINTN, void **);
	typedef EFI_STATUS CDK2_MS_ABI free_pool_fn(void *);
	const EFI_PEI_GRAPHICS_INFO_HOB *graphics = NULL;
	struct cdk2_boot_logo_surface surface;
	struct cdk2_boot_logo_layout layout;
	struct boot_graphics_resource_table table = { 0 };
	struct acpi_table_protocol *acpi = NULL;
	locate_protocol_fn *locate =
		(locate_protocol_fn *)core.boot_services.locate_protocol;
	allocate_pool_fn *allocate =
		(allocate_pool_fn *)core.boot_services.allocate_pool;
	free_pool_fn *release = (free_pool_fn *)core.boot_services.free_pool;
	void *bmp = NULL;
	UINTN capacity = 0U, size = 0U, key;
	BOOLEAN coreboot_splash;
	BOOLEAN allocated_bmp = FALSE;
	struct boot_logo_publication publication = {
		.display = EFI_UNSUPPORTED,
		.bgrt = EFI_NOT_STARTED,
		.snapshot = EFI_SUCCESS,
	};
	EFI_STATUS status;

#ifdef CDK2_HOST_TEST
	if (EFI_ERROR(boot_logo_display_test_status)) {
		publication.display = boot_logo_display_test_status;
		return publication;
	}
#endif
	if (context == NULL || context->linear_state == NULL || locate == NULL ||
	    allocate == NULL || release == NULL)
		return publication;
	status = find_delegated_framebuffer(context->handoff, context->linear_state,
		&graphics);
	if (!EFI_ERROR(status))
		status = boot_logo_surface(graphics, &surface);
	if (!EFI_ERROR(status) && context->status_snapshot.pixels != NULL)
		status = restore_boot_graphics_region(&surface, &context->status_snapshot);
	coreboot_splash = (context->linear_state->flags &
		CDK2_LINEAR_STATE_COREBOOT_SPLASH) != 0U;
	if (!EFI_ERROR(status) && coreboot_splash) {
		layout = (struct cdk2_boot_logo_layout) {
			.x = context->linear_state->splash_image_offset_x,
			.y = context->linear_state->splash_image_offset_y,
			.width = context->linear_state->splash_image_width,
			.height = context->linear_state->splash_image_height,
		};
		if (!boot_graphics_region_valid(&surface, &layout))
			status = EFI_COMPROMISED_DATA;
		else if (context->splash_snapshot.pixels != NULL) {
			const struct cdk2_boot_logo_layout *original = &context->splash_snapshot.rectangle;

			status = layout.x != original->x || layout.y != original->y ||
				layout.width != original->width || layout.height != original->height ?
				EFI_COMPROMISED_DATA :
				restore_boot_graphics_region(&surface, &context->splash_snapshot);
		}
	} else if (!EFI_ERROR(status)) {
		status = cdk2_boot_logo_draw(&surface, &layout);
	}
	if (!EFI_ERROR(status) && layout.width == 0U)
		status = EFI_COMPROMISED_DATA;
	publication.display = status;
	if (EFI_ERROR(status))
		goto release_snapshot;
	if (layout.width > MAX_UINT32 / 3U) {
		status = EFI_BAD_BUFFER_SIZE;
		goto release_snapshot;
	}
	if (coreboot_splash) {
		bmp = (void *)(UINTN)context->linear_state->splash_bmp_address;
		size = context->linear_state->splash_bmp_size;
		status = bmp == NULL || size < 54U ? EFI_COMPROMISED_DATA : EFI_SUCCESS;
	} else {
		capacity = ALIGN_VALUE((UINTN)layout.width * 3U, 4U);
		if (layout.height > (MAX_UINTN - 54U) / capacity) {
			status = EFI_BAD_BUFFER_SIZE;
			goto release_snapshot;
		}
		capacity = 54U + capacity * layout.height;
		status = allocate(4U, capacity, &bmp);
		allocated_bmp = !EFI_ERROR(status);
	}
	if (!EFI_ERROR(status) && !coreboot_splash) {
		status = cdk2_boot_logo_bmp(&layout, bmp, capacity, &size);
	}
	if (!EFI_ERROR(status))
		status = locate(&acpi_table_protocol_guid, NULL, (void **)&acpi);
	if (!EFI_ERROR(status) && (acpi == NULL || acpi->install == NULL))
		status = EFI_COMPROMISED_DATA;
	if (!EFI_ERROR(status)) {
		table.header.signature = SIGNATURE_32('B', 'G', 'R', 'T');
		table.header.length = sizeof(table);
		table.header.revision = 1U;
		memcpy(table.header.oem_id, "CDK2  ", sizeof(table.header.oem_id));
		table.header.oem_table_id = SIGNATURE_64('C', 'D', 'K', '2', 'B', 'G', 'R', 'T');
		table.header.oem_revision = 1U;
		table.header.creator_id = SIGNATURE_32('C', 'D', 'K', '2');
		table.header.creator_revision = 1U;
		table.version = 1U;
		table.status = 1U;
		table.image_type = 0U;
		table.image_address = (UINTN)bmp;
		table.image_offset_x = layout.x;
		table.image_offset_y = layout.y;
		table.header.checksum = acpi_checksum(&table, sizeof(table));
		status = acpi->install(acpi, &table, sizeof(table), &key);
	}
	if (EFI_ERROR(status) && allocated_bmp)
		(void)release(bmp);
	publication.bgrt = status;
release_snapshot:
	if (!EFI_ERROR(publication.display) &&
	    publication.bgrt == EFI_NOT_STARTED)
		publication.bgrt = status;
	publication.snapshot = release_boot_graphics_snapshots(context);
	return publication;
}

#endif

#if CONFIG_CDK2_LINEAR_BOOT || defined(CDK2_HOST_TEST)
typedef EFI_STATUS CDK2_MS_ABI framebuffer_free_pool_fn(void *);

/* Revision 3 has no topology HOB. Do not use this inference with revision 4. */
static EFI_STATUS find_legacy_framebuffer_controller(
	struct cdk2_dxe_database *database, framebuffer_free_pool_fn *free_pool,
	const EFI_PEI_GRAPHICS_INFO_HOB *graphics, const void **controller_path,
	UINTN *controller_path_size)
{
	static const EFI_GUID pci_io = { 0x4cf5b200, 0x68b8, 0x4ca5,
		{ 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } };
	static const EFI_GUID device_path = { 0x09576e91, 0x6d3f, 0x11d2,
		{ 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
	const void *selected_path = NULL;
	UINTN selected_size = 0U;
	if (database == NULL || graphics == NULL || controller_path == NULL ||
	    controller_path_size == NULL || free_pool == NULL ||
	    graphics->frame_buffer_size == 0U ||
	    graphics->frame_buffer_base > MAX_UINT64 - graphics->frame_buffer_size)
		return EFI_INVALID_PARAMETER;
	*controller_path = NULL;
	*controller_path_size = 0U;
	for (UINTN protocol = 0U; protocol < database->protocol_count; protocol++) {
		struct cdk2_dxe_protocol *record = &database->protocols[protocol];
		struct cdk2_efi_pci_io_protocol *pci;

		if (memcmp(&record->guid, &pci_io, sizeof(pci_io)) != 0)
			continue;
		pci = record->interface;
		if (pci == NULL || pci->get_bar_attributes == NULL)
			return EFI_COMPROMISED_DATA;
		for (UINT8 bar = 0U; bar < 6U; bar++) {
			UINT8 *resource = NULL;
			UINT64 attributes;
			BOOLEAN contains;
			void *path;
			UINTN path_span, path_size;
			EFI_STATUS status = pci->get_bar_attributes(pci, bar, &attributes,
				(void **)&resource);

			(void)attributes;
			if (status == EFI_UNSUPPORTED)
				continue;
			if (EFI_ERROR(status) || resource == NULL)
				return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
			status = framebuffer_bar_contains(resource,
				graphics->frame_buffer_base, graphics->frame_buffer_size,
				&contains);
			{
				EFI_STATUS free_status = free_pool(resource);

				if (!EFI_ERROR(status) && (EFI_ERROR(free_status) ||
				    (status == EFI_SUCCESS && free_status != EFI_SUCCESS)))
					status = free_status;
			}
			if (EFI_ERROR(status))
				return status;
			if (!contains)
				continue;
			status = cdk2_dxe_handle_span(database, record->handle,
				&device_path, &path, &path_span);
			if (EFI_ERROR(status) || path == NULL)
				return EFI_COMPROMISED_DATA;
			status = cdk2_dxe_device_path_size(path,
				path_span < 4096U ? path_span : 4096U, &path_size);
			if (EFI_ERROR(status))
				return status;
			if (selected_path != NULL)
				return EFI_COMPROMISED_DATA;
			selected_path = path;
			selected_size = path_size;
		}
	}
	if (selected_path == NULL)
		return EFI_NOT_FOUND;
	*controller_path = selected_path;
	*controller_path_size = selected_size;
	return EFI_SUCCESS;
}

static EFI_STATUS find_framebuffer_controller(
	struct dxe_services_context *context, struct cdk2_dxe_database *database,
	framebuffer_free_pool_fn *free_pool,
	const EFI_PEI_GRAPHICS_INFO_HOB *graphics, const void **controller_path,
	UINTN *controller_path_size)
{
	static const EFI_GUID pci_io = { 0x4cf5b200, 0x68b8, 0x4ca5,
		{ 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } };
	static const EFI_GUID device_path = { 0x09576e91, 0x6d3f, 0x11d2,
		{ 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
	const struct cdk2_pci_topology_handoff *topology = NULL;
	const struct cdk2_pci_topology_function *owner;
	struct cdk2_dxe_protocol *selected = NULL;
	struct cdk2_efi_pci_io_protocol *pci;
	UINT8 *resource = NULL;
	UINT64 attributes;
	BOOLEAN contains;
	void *path;
	UINTN path_span, path_size;
	EFI_STATUS status, free_status;

	if (controller_path == NULL || controller_path_size == NULL)
		return EFI_INVALID_PARAMETER;
	*controller_path = NULL;
	*controller_path_size = 0U;
	if (context == NULL || context->handoff == NULL || database == NULL ||
	    graphics == NULL || free_pool == NULL || graphics->frame_buffer_size == 0U ||
	    graphics->frame_buffer_base > MAX_UINT64 - graphics->frame_buffer_size)
		return EFI_INVALID_PARAMETER;
	status = services_pci_topology(context, &topology);
	if (status == EFI_NOT_FOUND)
		return find_legacy_framebuffer_controller(database, free_pool, graphics,
			controller_path, controller_path_size);
	if (status != EFI_SUCCESS)
		return status;
	if ((topology->flags &
	     CDK2_PCI_TOPOLOGY_HANDOFF_FRAMEBUFFER_OWNER_VALID) == 0U)
		return EFI_UNSUPPORTED;
	owner = &topology->function[topology->framebuffer_topology_index];
	for (UINTN protocol = 0U; protocol < database->protocol_count; protocol++) {
		struct cdk2_dxe_protocol *record = &database->protocols[protocol];
		UINTN segment, bus, device, function;

		if (memcmp(&record->guid, &pci_io, sizeof(pci_io)) != 0)
			continue;
		pci = record->interface;
		if (pci == NULL || pci->get_location == NULL)
			return EFI_COMPROMISED_DATA;
		status = pci->get_location(pci, &segment, &bus, &device, &function);
		if (status != EFI_SUCCESS)
			return status;
		if (segment > UINT16_MAX || bus > UINT8_MAX || device > 31U || function > 7U)
			return EFI_COMPROMISED_DATA;
		if (segment != owner->segment || bus != owner->bus ||
		    device != owner->device || function != owner->function)
			continue;
		if (selected != NULL)
			return EFI_COMPROMISED_DATA;
		selected = record;
	}
	if (selected == NULL)
		return EFI_COMPROMISED_DATA;
	pci = selected->interface;
	if (pci->get_bar_attributes == NULL)
		return EFI_COMPROMISED_DATA;
	status = pci->get_bar_attributes(pci, topology->framebuffer_bar, &attributes,
		(void **)&resource);
	if (status != EFI_SUCCESS)
		return status;
	if (resource == NULL)
		return EFI_COMPROMISED_DATA;
	status = framebuffer_bar_contains(resource, graphics->frame_buffer_base,
		graphics->frame_buffer_size, &contains);
	free_status = free_pool(resource);
	if (status != EFI_SUCCESS)
		return status;
	if (free_status != EFI_SUCCESS)
		return free_status;
	if (!contains)
		return EFI_COMPROMISED_DATA;
	status = cdk2_dxe_handle_span(database, selected->handle, &device_path,
		&path, &path_span);
	if (status != EFI_SUCCESS || path == NULL)
		return EFI_COMPROMISED_DATA;
	status = cdk2_dxe_device_path_size(path,
		path_span < 4096U ? path_span : 4096U, &path_size);
	if (status != EFI_SUCCESS)
		return status;
	*controller_path = path;
	*controller_path_size = path_size;
	return EFI_SUCCESS;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_find_framebuffer_controller(
	EFI_HOB_HANDOFF_INFO_TABLE *handoff, struct cdk2_dxe_database *database,
	framebuffer_free_pool_fn *free_pool,
	const EFI_PEI_GRAPHICS_INFO_HOB *graphics, const void **path,
	UINTN *path_size)
{
	struct dxe_services_context context = { .handoff = handoff };

	return find_framebuffer_controller(&context, database, free_pool, graphics, path,
		path_size);
}
#endif

#endif

#if CONFIG_CDK2_LINEAR_BOOT

static const struct cdk2_graphics_policy adopted_graphics_policy = {
	.hidpi_scale = CONFIG_CDK2_GOP_HIDPI ? 2U : 1U,
	.threshold_horizontal = CONFIG_CDK2_GOP_HIDPI_H_THRESHOLD,
	.threshold_vertical = CONFIG_CDK2_GOP_HIDPI_V_THRESHOLD,
	.aspect_width = CONFIG_CDK2_GOP_HIDPI_ASPECT_CAP ?
		CONFIG_CDK2_GOP_HIDPI_ASPECT_W : 0U,
	.aspect_height = CONFIG_CDK2_GOP_HIDPI_ASPECT_CAP ?
		CONFIG_CDK2_GOP_HIDPI_ASPECT_H : 0U,
};

static uint64_t adopt_display(void *opaque)
{
	struct dxe_services_context *context = opaque;
	const EFI_PEI_GRAPHICS_INFO_HOB *graphics = NULL;
	const void *controller_path = NULL;
	UINTN controller_path_size = 0U;
	framebuffer_free_pool_fn *free_pool =
		(framebuffer_free_pool_fn *)core.boot_services.free_pool;
	EFI_STATUS status;

	if (protocol_instance_count(&graphics_output_protocol) != 0U)
		return EFI_COMPROMISED_DATA;
	status = find_delegated_framebuffer(context->handoff,
		context->linear_state, &graphics);
	if (status == EFI_SUCCESS)
		status = find_framebuffer_controller(context,
			&core.database, free_pool, graphics,
			&controller_path, &controller_path_size);
	if (status == EFI_SUCCESS)
		status = cdk2_graphics_adopt(&adopted_display, &display_services,
			graphics, controller_path, controller_path_size,
			&adopted_graphics_policy);
	if (status == EFI_SUCCESS &&
	    (protocol_instance_count(&graphics_output_protocol) != 1U ||
	     protocol_instance_count(&device_path_protocol) == 0U))
		status = EFI_COMPROMISED_DATA;
	if (EFI_ERROR(status) && adopted_display.started) {
		EFI_STATUS rollback = cdk2_graphics_stop_child(&adopted_display);

		if (EFI_ERROR(rollback))
			status = rollback;
	}
	if (EFI_ERROR(status)) {
		EFI_STATUS cleanup = release_boot_graphics_snapshots(context);

		if (EFI_ERROR(cleanup))
			status = cleanup;
	}
	CDK2_DXE_DIAG(EFI_ERROR(status) ? CDK2_DIAG_ERROR : CDK2_DIAG_INFO,
		CDK2_DXE_155, graphics == NULL ? 0U : graphics->frame_buffer_base);
	return status;
}

#if CONFIG_CDK2_NATIVE_USB_KEYBOARD
static EFI_STATUS connect_usb_keyboard(struct dxe_services_context *context,
	UINTN *connected_keyboards)
{
	connect_controller_fn *connect = (connect_controller_fn *)
		core.boot_services.connect_controller;
	struct dispatched_binding bus = { 0 };
	struct dispatched_binding keyboard = { 0 };
	UINTN connected = 0U;
	EFI_STATUS status;
	BOOLEAN staged_for_input = FALSE;

	if (connected_keyboards == NULL)
		return EFI_INVALID_PARAMETER;
	*connected_keyboards = 0U;
	if (connect == NULL)
		return EFI_UNSUPPORTED;
	if (protocol_instance_count(&usb_io_protocol) == 0U) {
		struct cdk2_xhci_control *control = NULL;
		void *drivers[2];

		if (staged_xhci.controller == NULL) {
			UINTN candidate;

			for (candidate = 0U; candidate < storage_candidates.count;
			     candidate++)
				if (storage_candidates.candidate[candidate].backend ==
				    STORAGE_USB)
					break;
			if (candidate == storage_candidates.count)
				return EFI_SUCCESS;
			status = stage_controller_provider(context,
				storage_candidates.candidate[candidate].handle,
				&xhci_controller_driver, &usb2_hc_protocol,
				&pci_io_protocol, &storage_bindings.xhci,
				&staged_xhci);
			if (EFI_ERROR(status))
				return status;
			staged_for_input = TRUE;
		}
		status = get_storage_binding(context, &usb_bus_driver,
			&storage_bindings.usb_bus, &bus);
		if (EFI_ERROR(status))
			goto rollback_input_xhci;
		drivers[0] = bus.image;
		drivers[1] = NULL;
		status = connect(staged_xhci.controller, drivers, NULL, FALSE);
		if (EFI_ERROR(status))
			goto rollback_input_xhci;
		status = private_xhci_control(&core, staged_xhci.controller,
			staged_xhci.image, &control);
		if (EFI_ERROR(status) || control == NULL || control->poll_once == NULL) {
			status = EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
			goto rollback_input_xhci;
		}
		status = control->poll_once(control, staged_xhci.controller);
		if (EFI_ERROR(status))
			goto rollback_input_xhci;
	}
	status = dispatch_one_binding(context, &usb_keyboard_driver, &keyboard);
	if (EFI_ERROR(status))
		goto rollback_input_xhci;
	for (UINTN index = 0U; index < core.database.protocol_count; index++) {
		struct cdk2_dxe_protocol *record = &core.database.protocols[index];
		void *drivers[2] = { keyboard.image, NULL };

		if (memcmp(&record->guid, &usb_io_protocol,
		    sizeof(usb_io_protocol)) != 0)
			continue;
		status = connect(record->handle, drivers, NULL, FALSE);
		if (!EFI_ERROR(status))
			connected++;
		if (status != EFI_UNSUPPORTED && status != EFI_NOT_FOUND &&
		    EFI_ERROR(status))
			goto rollback_input_xhci;
	}
	cdk2_diag_value(CDK2_DIAG_DXE_DISPATCH,
		"USB keyboard controllers", connected);
	*connected_keyboards = connected;
	return EFI_SUCCESS;

rollback_input_xhci:
	return staged_for_input ? unstage_provider(&staged_xhci, status) : status;
}
#endif

#if CONFIG_CDK2_LVGL_RENDERER
static uint64_t validate_linear_status_output(const struct linear_status_output *output)
{
	const struct cdk2_dxe_protocol *record = NULL;
	struct cdk2_dxe_image *owner = NULL;
	struct cdk2_dxe_image *callback_owner;
	size_t handles = 0U;

	if (output->image == NULL || output->generation == 0U ||
	    output->interface == NULL || output->show_status == NULL ||
	    (uintptr_t)output->interface % _Alignof(struct cdk2_lvgl_status_protocol) != 0U ||
	    core.images.count > CDK2_DXE_MAX_IMAGES ||
	    core.database.protocol_count > CDK2_DXE_MAX_PROTOCOLS ||
	    core.database.handle_count > CDK2_DXE_MAX_HANDLES)
		return EFI_COMPROMISED_DATA;
	for (size_t index = 0U; index < core.database.handle_count; index++)
		if (core.database.handles[index] == output->image)
			handles++;
	if (handles != 1U)
		return EFI_COMPROMISED_DATA;
	for (size_t index = 0U; index < core.images.count; index++) {
		struct cdk2_dxe_image *image = &core.images.images[index];

		if (image->handle != output->image)
			continue;
		if (owner != NULL)
			return EFI_COMPROMISED_DATA;
		owner = image;
	}
	if (owner == NULL || owner->load_generation != output->generation ||
	    !owner->started || !owner->exited || owner->exit_active ||
	    EFI_ERROR(owner->exit_status) || owner->protocol_pins == 0U ||
	    private_control_image(&core, output->interface, sizeof(*output->interface)) != owner)
		return EFI_COMPROMISED_DATA;
	for (size_t index = 0U; index < core.database.protocol_count; index++) {
		const struct cdk2_dxe_protocol *candidate = &core.database.protocols[index];

		if (candidate->handle != output->image ||
		    memcmp(&candidate->guid, &lvgl_status_protocol_guid,
		    sizeof(candidate->guid)) != 0)
			continue;
		if (record != NULL)
			return EFI_COMPROMISED_DATA;
		record = candidate;
	}
	if (record == NULL || record->interface != output->interface ||
	    record->interface_dynamic || record->interface_size != 0U ||
	    record->interface_generation != 0U || record->owner_image != output->image ||
	    record->owner_generation != output->generation ||
	    output->interface->revision != CDK2_LVGL_STATUS_PROTOCOL_REVISION ||
	    output->interface->size != sizeof(*output->interface) ||
	    output->interface->show_status != output->show_status)
		return EFI_COMPROMISED_DATA;
	callback_owner = owner;
	return private_control_callback(&core, output->show_status, &callback_owner);
}
#endif

static uint64_t initialize_input_ui(void *opaque)
{
	struct dxe_services_context *context = opaque;
	struct cdk2_dxe_system_table *system;
	struct cdk2_dxe_system_table checked;
	struct cdk2_split_text_in_protocol *input;
	struct cdk2_split_text_out_protocol *output, *error;
	typedef EFI_STATUS CDK2_MS_ABI crc32_fn(void *, UINTN, UINT32 *);
	crc32_fn *calculate_crc32;
	UINT32 published_crc, checked_crc;
	UINTN outputs, inputs, extended_inputs;
#if CONFIG_CDK2_NATIVE_USB_KEYBOARD
	UINTN connected_inputs = 0U, usb_keyboards;
#if CONFIG_CDK2_NATIVE_CON_SPLITTER
	connect_controller_fn *connect_input =
		(connect_controller_fn *)core.boot_services.connect_controller;
#endif
#endif
#if CONFIG_CDK2_NATIVE_CON_SPLITTER
	void *splitter_image = NULL;
#endif
	void *interface;
	EFI_STATUS status;
#if CONFIG_CDK2_LINEAR_SETUP_HOTKEY
	struct cdk2_dxe_protocol *status_record;
	struct cdk2_dxe_image *status_owner;
	uint64_t ui_generation;
	struct linear_status_output status_output = {0};
#if CDK2_STRICT_DIRECT_RUNTIME
	const void *ui_source = NULL;
	size_t ui_source_size = 0U;
#endif
#endif

	if (context == NULL || context->linear_state == NULL)
		return EFI_INVALID_PARAMETER;
#if CONFIG_CDK2_NATIVE_USB_KEYBOARD
	status = connect_usb_keyboard(context, &usb_keyboards);
	if (EFI_ERROR(status))
		return status;
#endif
#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME
	if (direct_input_phase_test_entry) {
		status = EFI_SUCCESS;
		outputs = inputs = extended_inputs = 0U;
		goto direct_disposition;
	}
#endif
#if CONFIG_CDK2_NATIVE_CON_SPLITTER
	status = dispatch_binding_set(context, &console_splitter_driver, 5U,
		&splitter_image, NULL);
	if (EFI_ERROR(status))
		return status;
#else
	(void)console_splitter_driver;
#endif
#if CONFIG_CDK2_LINEAR_SETUP_HOTKEY
	ui_generation = core.images.next_handle;
#if CDK2_STRICT_DIRECT_RUNTIME
	if (core.dispatcher.count > CDK2_DXE_MAX_DRIVERS)
		return EFI_COMPROMISED_DATA;
	for (size_t index = 0U; index < core.dispatcher.count; index++) {
		const struct cdk2_dxe_driver *driver = &core.dispatcher.drivers[index];

		if (memcmp(&driver->guid, &input_ui_drivers[0].guid, sizeof(driver->guid)) != 0)
			continue;
		if (ui_source != NULL || !driver->direct || driver->started || driver->failed ||
		    driver->image == NULL || driver->image_size == 0U)
			return EFI_COMPROMISED_DATA;
		ui_source = driver->image;
		ui_source_size = driver->image_size;
	}
	if (ui_source == NULL)
		return EFI_NOT_FOUND;
#endif
	if (context->status_output.image != NULL || ui_generation == 0U ||
	    ui_generation == MAX_UINTN)
		return EFI_COMPROMISED_DATA;
	status = dispatch_required(context, input_ui_drivers,
		ARRAY_SIZE(input_ui_drivers));
	if (EFI_ERROR(status))
		return status;
	status = private_control_record(&core, &lvgl_status_protocol_guid,
		sizeof(*status_output.interface), &status_record, &status_owner);
	if (status != EFI_SUCCESS || status_owner == NULL ||
	    status_owner->load_generation != ui_generation ||
	    status_record->handle != status_owner->handle ||
	    EFI_ERROR(cdk2_dxe_image_transaction_validate_owned(&core.images,
		status_owner->transaction, status_owner->transaction_allocation_generation)))
		return EFI_COMPROMISED_DATA;
#if CDK2_STRICT_DIRECT_RUNTIME
	if (status_owner->transaction->source_size != ui_source_size ||
	    memcmp(status_owner->transaction->source, ui_source, ui_source_size) != 0)
		return EFI_COMPROMISED_DATA;
#endif
	status_output.image = status_owner->handle;
	status_output.generation = status_owner->load_generation;
	status_output.interface = status_record->interface;
	status_output.show_status = status_output.interface->show_status;
	status = validate_linear_status_output(&status_output);
	if (EFI_ERROR(status))
		return status;
#endif
	if (core.runtime_storage == NULL)
		return EFI_COMPROMISED_DATA;
	system = &core.runtime_storage->system_table;
#if CONFIG_CDK2_NATIVE_USB_KEYBOARD && CONFIG_CDK2_NATIVE_CON_SPLITTER
	if (connect_input == NULL)
		return EFI_UNSUPPORTED;
	for (UINTN index = 0U; index < core.database.protocol_count; index++) {
		struct cdk2_dxe_protocol *record = &core.database.protocols[index];
		void *drivers[2] = { splitter_image, NULL };

		if (memcmp(&record->guid, &simple_text_input_protocol,
		    sizeof(simple_text_input_protocol)) != 0 ||
		    record->handle == system->console_in_handle)
			continue;
		status = connect_input(record->handle, drivers, NULL, FALSE);
		if (EFI_ERROR(status))
			return status;
		connected_inputs++;
	}
	if (connected_inputs != usb_keyboards)
		return EFI_COMPROMISED_DATA;
#endif
	if (system->console_in_handle == NULL || system->console_in == NULL ||
	    system->console_out_handle == NULL || system->console_out == NULL ||
	    system->standard_error_handle == NULL ||
	    system->standard_error == NULL || system->header.crc32 == 0U ||
	    system->console_in_handle == system->console_out_handle ||
	    system->console_in_handle == system->standard_error_handle ||
	    system->console_out_handle == system->standard_error_handle)
		return EFI_COMPROMISED_DATA;
	input = system->console_in;
	output = system->console_out;
	error = system->standard_error;
	if (input->reset == NULL || input->read == NULL ||
	    input->wait_for_key == NULL || output->reset == NULL ||
	    output->output == NULL || output->test == NULL || output->query == NULL ||
	    output->set_mode == NULL || output->set_attribute == NULL ||
	    output->clear == NULL || output->set_cursor == NULL ||
	    output->enable_cursor == NULL || output->mode == NULL ||
	    error->reset == NULL || error->output == NULL || error->test == NULL ||
	    error->query == NULL || error->set_mode == NULL ||
	    error->set_attribute == NULL || error->clear == NULL ||
	    error->set_cursor == NULL || error->enable_cursor == NULL ||
	    error->mode == NULL || system->boot_services == NULL ||
	    system->boot_services->calculate_crc32 == NULL ||
	    system->header.header_size != sizeof(*system))
		return EFI_COMPROMISED_DATA;
	if (cdk2_dxe_handle(&core.database, system->console_in_handle,
		&simple_text_input_protocol, &interface) != EFI_SUCCESS ||
	    interface != system->console_in ||
	    cdk2_dxe_handle(&core.database, system->console_in_handle,
		&simple_text_input_ex_protocol, &interface) != EFI_SUCCESS ||
	    cdk2_dxe_handle(&core.database, system->console_out_handle,
		&simple_text_output_protocol, &interface) != EFI_SUCCESS ||
	    interface != system->console_out ||
	    cdk2_dxe_handle(&core.database, system->console_out_handle,
		&graphics_output_protocol, &interface) != EFI_SUCCESS ||
	    cdk2_dxe_handle(&core.database, system->standard_error_handle,
		&simple_text_output_protocol, &interface) != EFI_SUCCESS ||
	    interface != system->standard_error)
		return EFI_COMPROMISED_DATA;
	checked = *system;
	published_crc = checked.header.crc32;
	checked.header.crc32 = 0U;
	calculate_crc32 = (crc32_fn *)system->boot_services->calculate_crc32;
	status = calculate_crc32(&checked, sizeof(checked), &checked_crc);
	if (EFI_ERROR(status) || checked_crc != published_crc)
		return EFI_COMPROMISED_DATA;
	outputs = protocol_instance_count(&graphics_output_protocol);
	inputs = protocol_instance_count(&simple_text_input_protocol);
	extended_inputs = protocol_instance_count(&simple_text_input_ex_protocol);
	if (outputs != 2U ||
#if CONFIG_CDK2_NATIVE_USB_KEYBOARD
	    inputs != usb_keyboards + 1U ||
	    extended_inputs != usb_keyboards + 1U)
#else
	    inputs != 1U || extended_inputs != 1U)
#endif
		return EFI_COMPROMISED_DATA;
	status = cdk2_linear_input_ui_validate(
		context->linear_state, outputs - 1U, 0U, 0U, 0U);
#if CDK2_STRICT_DIRECT_RUNTIME

#ifdef CDK2_HOST_TEST
direct_disposition:
#endif
	/* USB input is the route's last possible consumer.  Only now may an
	 * unselected USB storage/input branch receive its final disposition. */
	if (!EFI_ERROR(status))
		status = disposition_usb_route(&core.dispatcher,
			selected_storage_backend,
			CONFIG_CDK2_NATIVE_USB_KEYBOARD != 0);
#endif
#if CONFIG_CDK2_LINEAR_SETUP_HOTKEY
	if (!EFI_ERROR(status) && status_output.image != NULL)
		context->status_output = status_output;
#endif
	CDK2_DXE_DIAG(EFI_ERROR(status) ? CDK2_DIAG_ERROR : CDK2_DIAG_INFO,
		CDK2_DXE_155,
		outputs | (inputs << 8U) | (extended_inputs << 16U));
	return status;
}

#if defined(CDK2_HOST_TEST) && CDK2_STRICT_DIRECT_RUNTIME
EFI_STATUS cdk2_dxe_test_initialize_input_ui(
	struct dxe_services_context *context, enum storage_backend backend)
{
	EFI_STATUS status;

	selected_storage_backend = backend;
	direct_input_phase_test_entry = TRUE;
	status = initialize_input_ui(context);
	direct_input_phase_test_entry = FALSE;
	return status;
}
#endif

#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
static EFI_STATUS CDK2_MS_ABI poll_linear_input(void *opaque)
{
#if CONFIG_CDK2_NATIVE_USB_KEYBOARD
	struct cdk2_xhci_control *control = NULL;
	EFI_STATUS status;

	(void)opaque;
	if (staged_xhci.controller == NULL)
		return EFI_SUCCESS;
	status = private_xhci_control(&core, staged_xhci.controller,
		staged_xhci.image, &control);
	if (EFI_ERROR(status))
		return status;
	if (control == NULL || control->poll_once == NULL)
		return EFI_COMPROMISED_DATA;
	return control->poll_once(control, staged_xhci.controller);
#else
	(void)opaque;
	return EFI_SUCCESS;
#endif
}
#endif

#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
static EFI_STATUS prepare_bds_display(struct dxe_services_context *context)
{
	const EFI_PEI_GRAPHICS_INFO_HOB *graphics = NULL;
	struct cdk2_boot_logo_surface surface;
	struct cdk2_boot_logo_layout rectangle;
	const struct cdk2_linear_state *state;
	EFI_STATUS status;

	if (context == NULL || context->linear_state == NULL)
		return EFI_INVALID_PARAMETER;
	if ((context->linear_state->flags & CDK2_LINEAR_STATE_COREBOOT_SPLASH) == 0U)
		return EFI_SUCCESS;
	if (context->splash_snapshot.pixels != NULL)
		return EFI_SUCCESS;
	status = find_delegated_framebuffer(context->handoff,
		context->linear_state, &graphics);
	if (EFI_ERROR(status))
		return status;
	state = context->linear_state;
	rectangle = (struct cdk2_boot_logo_layout) {
		.x = state->splash_image_offset_x, .y = state->splash_image_offset_y,
		.width = state->splash_image_width, .height = state->splash_image_height,
	};
	status = boot_logo_surface(graphics, &surface);
	return EFI_ERROR(status) ? status :
		capture_boot_graphics_region(&surface, &rectangle, &context->splash_snapshot);
}
#endif

#if CONFIG_CDK2_LVGL_RENDERER
static EFI_STATUS show_linear_status(struct dxe_services_context *context,
	enum cdk2_lvgl_status_message message)
{
	struct cdk2_lvgl_status_protocol *status_ui;
	const EFI_PEI_GRAPHICS_INFO_HOB *graphics = NULL;
	struct cdk2_boot_logo_surface surface;
	struct cdk2_boot_logo_layout layout;
	const struct cdk2_linear_state *state;
	struct cdk2_boot_logo_layout status_rectangle;
	EFI_STATUS status;

	if (context == NULL || context->linear_state == NULL ||
	    (context->linear_state->flags &
	     CDK2_LINEAR_STATE_SPLASH_VISIBLE) == 0U)
		return EFI_UNSUPPORTED;
	if (context->status_output.image == NULL)
		return EFI_NOT_FOUND;
	status = validate_linear_status_output(&context->status_output);
	if (EFI_ERROR(status))
		return status;
	status_ui = context->status_output.interface;
	state = context->linear_state;
	status = find_delegated_framebuffer(context->handoff, state, &graphics);
	if (!EFI_ERROR(status))
		status = boot_logo_surface(graphics, &surface);
	if (EFI_ERROR(status))
		return status;
	if ((state->flags & CDK2_LINEAR_STATE_COREBOOT_SPLASH) != 0U) {
		layout = (struct cdk2_boot_logo_layout) {
			.x = state->splash_image_offset_x,
			.y = state->splash_image_offset_y,
			.width = state->splash_image_width,
			.height = state->splash_image_height,
		};
	} else {
		status = cdk2_boot_logo_layout_for_surface(&surface, &layout);
		if (EFI_ERROR(status))
			return status;
	}
	if (!boot_graphics_region_valid(&surface, &layout))
		return EFI_COMPROMISED_DATA;
	if (surface.height - layout.y - layout.height < 8U + 48U)
		return EFI_UNSUPPORTED;
	status_rectangle = (struct cdk2_boot_logo_layout) {
		.x = layout.x, .y = layout.y + layout.height + 8U,
		.width = layout.width, .height = 48U,
	};
	status = (state->flags & CDK2_LINEAR_STATE_COREBOOT_SPLASH) != 0U ?
		capture_boot_graphics_region(&surface, &layout, &context->splash_snapshot) : EFI_SUCCESS;
	if (EFI_ERROR(status))
		return status;
	status = capture_boot_graphics_region(&surface, &status_rectangle, &context->status_snapshot);
	if (EFI_ERROR(status))
		return status;
	status = context->status_output.show_status(status_ui, &(const struct cdk2_lvgl_status) {
		.x = status_rectangle.x, .y = status_rectangle.y,
		.width = status_rectangle.width, .height = status_rectangle.height,
		.message = message,
	});
	if (EFI_ERROR(status)) {
		EFI_STATUS restored = restore_boot_graphics_region(&surface, &context->status_snapshot);

		if (EFI_ERROR(restored))
			return restored;
	}
	return status;
}
#endif

#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
static EFI_STATUS enter_linear_setup(struct bds_arch_protocol *bds,
	UINTN timeout_seconds)
{
	EFI_STATUS status;

	if (bds == NULL || bds->enter_setup == NULL)
		return EFI_UNSUPPORTED;
	status = bds->enter_setup(timeout_seconds, poll_linear_input, NULL);
	/* An expired setup window is the normal, deterministic boot path. */
	return status == EFI_NOT_READY ? EFI_SUCCESS : status;
}
#endif

static EFI_STATUS finish_loader_services(
	struct dxe_services_context *context, EFI_STATUS status)
{
	struct cdk2_cpu_arch *cpu;
	EFI_STATUS poll_status = EFI_SUCCESS;
	EFI_STATUS rollback;

	if (context == NULL || (!context->loader_interrupts_changed &&
	    !context->loader_xhci_polling))
		return status;
	if (status == EFI_SUCCESS)
		return EFI_SUCCESS;
	if (context->loader_xhci_polling) {
		struct cdk2_xhci_control *xhci = context->loader_xhci;

		poll_status = xhci == NULL || xhci->stop_polling == NULL ?
			EFI_COMPROMISED_DATA :
			xhci->stop_polling(xhci, xhci->controller);
		if (!EFI_ERROR(poll_status)) {
			context->loader_xhci = NULL;
			context->loader_xhci_polling = FALSE;
		}
	}
	rollback = EFI_SUCCESS;
	if (context->loader_interrupts_changed) {
		cpu = context->loader_cpu;
		context->loader_cpu = NULL;
		context->loader_interrupts_changed = FALSE;
		rollback = cpu == NULL || cpu->disable_interrupt == NULL ?
			EFI_COMPROMISED_DATA : cpu->disable_interrupt(cpu);
	}
	CDK2_DXE_DIAG(rollback != EFI_SUCCESS ? CDK2_DIAG_ERROR : CDK2_DIAG_INFO,
		CDK2_DXE_155, rollback);
	if (EFI_ERROR(poll_status))
		return poll_status;
	return rollback != EFI_SUCCESS ? rollback : status;
}

/* UEFI loaders require timer, input and event delivery while StartImage owns
 * execution.  Keep interrupts disabled throughout firmware discovery and
 * transfer that single resource explicitly at the loader boundary. */
static EFI_STATUS activate_loader_services(struct dxe_services_context *context)
{
	struct cdk2_cpu_arch *cpu;
	BOOLEAN enabled = FALSE;
	EFI_STATUS status;

#ifdef CDK2_HOST_TEST
	loader_activation_test_calls++;
#endif
	UINTN index;

	if (context == NULL || context->linear_state == NULL)
		return EFI_INVALID_PARAMETER;
	for (index = 0U; index < ARRAY_SIZE(loader_service_protocols); index++)
		if (protocol_instance_count(&loader_service_protocols[index]) != 1U)
			return EFI_COMPROMISED_DATA;
	cpu = find_protocol(&loader_service_protocols[0]);
	if (cpu == NULL || cpu->get_interrupt_state == NULL ||
	    cpu->enable_interrupt == NULL || cpu->disable_interrupt == NULL)
		return EFI_COMPROMISED_DATA;
	if (staged_xhci.controller != NULL) {
		struct cdk2_xhci_control *xhci = NULL;

		status = private_xhci_control(&core, staged_xhci.controller,
			staged_xhci.image, &xhci);
		if (EFI_ERROR(status) || xhci == NULL || xhci->start_polling == NULL ||
		    xhci->stop_polling == NULL)
			return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
		status = xhci->start_polling(xhci, xhci->controller);
		if (EFI_ERROR(status)) {
			EFI_STATUS cleanup = xhci->stop_polling(xhci, xhci->controller);

			if (cleanup == EFI_NOT_STARTED)
				return status;
			if (!EFI_ERROR(cleanup))
				return status;
			context->loader_xhci = xhci;
			context->loader_xhci_polling = TRUE;
			return cleanup;
		}
		context->loader_xhci = xhci;
		context->loader_xhci_polling = TRUE;
	}
	status = cpu->get_interrupt_state(cpu, &enabled);
	if (status == EFI_SUCCESS && !enabled) {
		/* Enabling may partially change hardware before reporting failure. */
		context->loader_cpu = cpu;
		context->loader_interrupts_changed = TRUE;
		status = cpu->enable_interrupt(cpu);
	}
	if (status == EFI_SUCCESS) {
		enabled = FALSE;
		status = cpu->get_interrupt_state(cpu, &enabled);
		if (status == EFI_SUCCESS && !enabled)
			status = EFI_DEVICE_ERROR;
	}
	if (status != EFI_SUCCESS)
		status = finish_loader_services(context, status);
	CDK2_DXE_DIAG(status != EFI_SUCCESS ? CDK2_DIAG_ERROR : CDK2_DIAG_INFO,
		CDK2_DXE_155, (UINT64)context->loader_interrupts_changed |
			((UINT64)ARRAY_SIZE(loader_service_protocols) << 32U));
	return status;
}

static uint64_t select_boot_policy(void *opaque)
{
	struct dxe_services_context *context = opaque;
	struct cdk2_fat_simple_fs_protocol *filesystem = NULL;
	void *device_path = NULL;
	UINTN device_path_size = 0U, device_path_span = 0U;
	EFI_STATUS status;

	if (context == NULL || context->linear_state == NULL ||
	    selected_esp == NULL || selected_loader_size == 0U ||
	    selected_loader_path[0] != L'\\')
		return EFI_COMPROMISED_DATA;
#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
	status = dispatch_required(context, boot_policy_drivers,
		ARRAY_SIZE(boot_policy_drivers));
	if (EFI_ERROR(status))
		return status;
	if (protocol_instance_count(&bds_guid) != 1U ||
	    find_protocol(&bds_guid) == NULL)
		return EFI_COMPROMISED_DATA;
	if (CONFIG_CDK2_LINEAR_SETUP_HOTKEY) {
		struct bds_arch_protocol *bds = find_protocol(&bds_guid);

		status = prepare_bds_display(context);
		if (!EFI_ERROR(status))
			status = enter_linear_setup(bds, CONFIG_CDK2_BOOT_TIMEOUT);
		if (EFI_ERROR(status))
			return status;
	}
#else
	if (CONFIG_CDK2_BOOT_TIMEOUT != 0)
		return EFI_COMPROMISED_DATA;
#endif
	status = cdk2_dxe_handle(&core.database, selected_esp, &simple_fs_protocol,
		(void **)&filesystem);
	if (status != EFI_SUCCESS || filesystem == NULL)
		status = EFI_COMPROMISED_DATA;
	else
		status = revalidate_selected_loader(filesystem);
	if (!EFI_ERROR(status))
		status = cdk2_dxe_handle_span(&core.database, selected_esp,
			&device_path_protocol, &device_path, &device_path_span);
	if (!EFI_ERROR(status) && boot_device_path_generation == MAX_UINT32)
		status = EFI_OUT_OF_RESOURCES;
	if (!EFI_ERROR(status))
		status = cdk2_dxe_device_path_size(device_path,
			device_path_span < 4096U ? device_path_span : 4096U,
			&device_path_size);
	if (!EFI_ERROR(status) && device_path_size > CDK2_LINEAR_BOOT_PATH_BYTES)
		status = EFI_BAD_BUFFER_SIZE;
	if (!EFI_ERROR(status)) {
		boot_device_path_generation++;
		status = cdk2_linear_boot_path_set(context->linear_state,
			device_path, (UINT32)device_path_size,
			boot_device_path_generation);
	}
	if (!EFI_ERROR(status)) {
		context->linear_state->boot_loader_size = selected_loader_size;
		for (UINTN index = 0U; index < CDK2_LINEAR_LOADER_PATH_CHARS;
		     index++)
			context->linear_state->boot_loader_path[index] =
				selected_loader_path[index];
		status = cdk2_linear_state_update_hob(context->handoff,
			context->linear_state);
	}
	if (!EFI_ERROR(status)) {
		struct cdk2_dxe_lifecycle_ops ops = lifecycle_ops(context);

		status = cdk2_dxe_lifecycle_end_of_dxe(&context->lifecycle, &ops);
		cdk2_diag_phase("EndOfDxe", "signal and lock", status, 0U);
	}
	CDK2_DXE_DIAG(EFI_ERROR(status) ? CDK2_DIAG_ERROR : CDK2_DIAG_INFO,
		CDK2_DXE_155, ((UINT64)!EFI_ERROR(status) << 63U) |
			(UINT64)(UINTN)selected_esp);
	return status;
}

static uint64_t handoff_operating_system(void *opaque)
{
	typedef EFI_STATUS CDK2_MS_ABI load_image_fn(BOOLEAN, void *, void *,
		void *, UINTN, void **);
	typedef EFI_STATUS CDK2_MS_ABI start_image_fn(void *, UINTN *,
		void *exit_data[]);
	typedef EFI_STATUS CDK2_MS_ABI free_pool_fn(void *);
	typedef EFI_STATUS CDK2_MS_ABI allocate_pool_fn(UINT32, UINTN, void **);
	struct loaded_image_view {
		UINT32 revision, padding;
		void *parent, *system, *device, *file_path, *reserved;
		UINT32 options_size, options_padding;
		void *options, *image_base;
		UINT64 image_size;
		UINT32 code_type, data_type;
		void *unload;
	};
	static const EFI_GUID security2 = { 0x94ab2f58, 0x1438, 0x4ef1,
		{ 0x91, 0x52, 0x18, 0x94, 0x1a, 0x3a, 0x0e, 0x68 } };
	struct dxe_services_context *context = opaque;
	struct cdk2_linear_state persisted;
	struct loaded_image_view *loaded = NULL;
	UINT8 path[CDK2_LINEAR_BOOT_PATH_BYTES +
		CDK2_LINEAR_LOADER_PATH_CHARS * sizeof(CHAR16) + 8U];
	CHAR16 loader_path[CDK2_LINEAR_LOADER_PATH_CHARS];
	UINTN checked_size = 0U;
	UINTN path_size = 0U, file_path_offset = 0U, file_path_size = 0U;
	UINTN exit_size = 0U;
	CHAR16 *exit_data = NULL;
	void *image = NULL, *esp = NULL, *loader = NULL;
	BOOLEAN start_returned = FALSE;
	BOOLEAN fatal_display_failure = FALSE;
	load_image_fn *load = (load_image_fn *)core.boot_services.load_image;
	start_image_fn *start = (start_image_fn *)core.boot_services.start_image;
	cdk2_dxe_unload_image_fn *unload =
		(cdk2_dxe_unload_image_fn *)core.boot_services.unload_image;
	free_pool_fn *release = (free_pool_fn *)core.boot_services.free_pool;
	allocate_pool_fn *allocate =
		(allocate_pool_fn *)core.boot_services.allocate_pool;
	struct cdk2_fat_simple_fs_protocol *filesystem = NULL;
	EFI_STATUS status = EFI_SUCCESS, loader_cleanup = EFI_SUCCESS;
	EFI_STATUS exit_cleanup = EFI_SUCCESS, unload_status = EFI_SUCCESS;
	EFI_STATUS ready_to_boot_cleanup_status;

#ifdef CDK2_HOST_TEST
	if (handoff_boot_logo_test_entry)
		goto publish_logo;
#endif

	if (context == NULL || context->linear_state == NULL ||
	    EFI_ERROR(cdk2_linear_state_validate(context->linear_state)))
		return EFI_COMPROMISED_DATA;
#if CDK2_STRICT_DIRECT_RUNTIME
	status = cdk2_dxe_dispatcher_direct_complete(&core.dispatcher);
	if (EFI_ERROR(status))
		return status;
#endif
#ifdef CDK2_HOST_TEST
	if (handoff_direct_gate_test_entry) {
		if (load == NULL || start == NULL)
			return EFI_COMPROMISED_DATA;
		status = load(FALSE, core_image_handle, NULL, NULL, 0U, &image);
		return EFI_ERROR(status) ? status :
			start(image, &exit_size, (void **)&exit_data);
	}
#endif
#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
	/* BootNext is a one-shot request and must precede the deterministic normal
	 * loader.  fwupd relies on this ordering to run fwupdx64.efi; bypassing it
	 * leaves a valid capsule staged forever while normal Linux keeps booting. */
	{
		BOOLEAN pending;
		struct bds_arch_protocol *bds = find_protocol(&bds_guid);
		EFI_STATUS boot_next_status = bds == NULL ||
			bds->boot_next_pending == NULL ? EFI_NOT_FOUND :
			bds->boot_next_pending(&pending);

		cdk2_diag_phase("BootNext", "probe", boot_next_status,
			!EFI_ERROR(boot_next_status) && pending ? 1U : 0U);

		if (!EFI_ERROR(boot_next_status) && pending) {
			cdk2_diag_phase("BootNext", "primary", bds == NULL ||
				bds->entry == NULL ? EFI_NOT_FOUND : EFI_SUCCESS, 0U);
			if (bds == NULL || bds->entry == NULL)
				return EFI_NOT_FOUND;
			boot_next_status = prepare_bds_display(context);
			if (EFI_ERROR(boot_next_status))
				return boot_next_status;
			bds->entry(bds);
			return EFI_DEVICE_ERROR;
		}
		if (EFI_ERROR(boot_next_status))
			return boot_next_status;
	}
#endif
	status = cdk2_linear_state_update_hob(context->handoff,
		context->linear_state);
	if (EFI_ERROR(status))
		return status;
	if (EFI_ERROR(cdk2_linear_state_from_hob(context->handoff, &persisted)) ||
	    persisted.boot_device_path_generation !=
		context->linear_state->boot_device_path_generation ||
	    EFI_ERROR(cdk2_dxe_device_path_size(persisted.boot_device_path,
		persisted.boot_device_path_size, &checked_size)) ||
	    checked_size != persisted.boot_device_path_size)
		return EFI_SECURITY_VIOLATION;
	if ((persisted.flags & CDK2_LINEAR_STATE_BOOT_ATTEMPTED) != 0U)
		return EFI_ALREADY_STARTED;
	if (protocol_instance_count(&security2) != 1U || load == NULL ||
	    start == NULL || unload == NULL || allocate == NULL || release == NULL ||
	    persisted.boot_loader_size < 512U ||
	    persisted.boot_loader_size > 64U * 1024U * 1024U)
		return EFI_COMPROMISED_DATA;
	memcpy(loader_path, persisted.boot_loader_path, sizeof(loader_path));
	status = cdk2_dxe_build_file_path(persisted.boot_device_path,
		persisted.boot_device_path_size, loader_path, path, sizeof(path),
		&path_size);
	if (!EFI_ERROR(status)) {
		file_path_offset = persisted.boot_device_path_size - 4U;
		file_path_size = path_size - file_path_offset;
	}
	if (!EFI_ERROR(status))
		status = cdk2_dxe_resolve_device_path(&core.database,
			&device_path_protocol, persisted.boot_device_path,
			persisted.boot_device_path_size, &esp);
	if (!EFI_ERROR(status) && esp != selected_esp)
		status = EFI_SECURITY_VIOLATION;
	if (!EFI_ERROR(status))
		status = cdk2_dxe_handle(&core.database, esp, &simple_fs_protocol,
			(void **)&filesystem);
	if (!EFI_ERROR(status))
		status = allocate(4U, persisted.boot_loader_size, &loader);
	if (!EFI_ERROR(status))
		status = cdk2_dxe_read_boot_file(filesystem, loader_path,
			persisted.boot_loader_size, loader);
	if (EFI_ERROR(status))
		if (loader != NULL)
			(void)release(loader);
	if (EFI_ERROR(status))
		return status;
	context->linear_state->flags |= CDK2_LINEAR_STATE_BOOT_ATTEMPTED;
	status = cdk2_linear_state_update_hob(context->handoff,
		context->linear_state);
	if (EFI_ERROR(status)) {
		(void)release(loader);
		return status;
	}
	status = cdk2_dxe_lifecycle_load_image((cdk2_dxe_lifecycle_load_image_fn *)load,
		TRUE, core_image_handle,
		path, loader, persisted.boot_loader_size, &image);
	loader_cleanup = release(loader);
	loader = NULL;
	if (EFI_ERROR(loader_cleanup))
		CDK2_DXE_DIAG(CDK2_DIAG_ERROR, CDK2_DXE_155,
			(UINT64)(UINTN)loader_cleanup);
	if (EFI_ERROR(status))
		return status;
	if (!EFI_ERROR(status))
		status = cdk2_dxe_handle(&core.database, image, &loaded_image_guid,
			(void **)&loaded);
	if (!EFI_ERROR(status) && (loaded == NULL ||
	    loaded->parent != core_image_handle || loaded->device != esp ||
	    loaded->file_path == NULL ||
	    EFI_ERROR(cdk2_dxe_device_path_size(loaded->file_path, file_path_size,
		&checked_size)) ||
	    checked_size != file_path_size || memcmp(loaded->file_path,
		path + file_path_offset, file_path_size) != 0))
		status = EFI_SECURITY_VIOLATION;
	if (!EFI_ERROR(status)) {
		struct cdk2_dxe_lifecycle_ops ops = lifecycle_ops(context);

		status = cdk2_dxe_lifecycle_ready_to_boot(&context->lifecycle, &ops);
		cdk2_diag_phase("ReadyToBoot", "signal", status, 0U);
		cdk2_diag_value(CDK2_DIAG_DXE_MAIN,
			"firmware management protocols after ReadyToBoot",
			protocol_instance_count(&firmware_management_protocol_guid));
		cdk2_diag_value(CDK2_DIAG_DXE_MAIN,
			"ESRT management protocols after ReadyToBoot",
			protocol_instance_count(&esrt_management_protocol_guid));
		cdk2_diag_value(CDK2_DIAG_DXE_MAIN,
			"ESRT tables after ReadyToBoot",
			configuration_table_present(&esrt_configuration_table_guid));
		ready_to_boot_cleanup_status =
			context->lifecycle.ready_to_boot_cleanup_status;
		if (EFI_ERROR(ready_to_boot_cleanup_status))
			CDK2_DXE_DIAG(CDK2_DIAG_ERROR, CDK2_DXE_155,
				(UINT64)(UINTN)ready_to_boot_cleanup_status);
	}
#ifdef CDK2_HOST_TEST
publish_logo:
#endif
	if (!EFI_ERROR(status)) {
		struct boot_logo_publication publication = publish_boot_logo(context);
		EFI_STATUS boot_logo_status = EFI_ERROR(publication.display) ?
			publication.display : publication.bgrt;

		cdk2_diag_phase("BootLogo", "restore and BGRT", boot_logo_status, 0U);
		if (EFI_ERROR(publication.snapshot))
			CDK2_DXE_DIAG(CDK2_DIAG_ERROR, CDK2_DXE_155,
				(UINT64)(UINTN)publication.snapshot);
		fatal_display_failure = EFI_ERROR(publication.display);
		if (fatal_display_failure)
			status = publication.display;
		else
			status = activate_loader_services(context);
		if (!EFI_ERROR(status)) {
			status = start(image, &exit_size, (void **)&exit_data);
			start_returned = TRUE;
		}
	}

	if (exit_data != NULL)
		exit_cleanup = release == NULL ? EFI_COMPROMISED_DATA : release(exit_data);
	status = cdk2_dxe_cleanup_returned_image(image, unload, start_returned,
		status, exit_cleanup, &unload_status);
	if (EFI_ERROR(unload_status))
		CDK2_DXE_DIAG(CDK2_DIAG_ERROR, CDK2_DXE_155,
			(UINT64)(UINTN)unload_status);
	CDK2_DXE_DIAG(EFI_ERROR(status) ? CDK2_DIAG_ERROR : CDK2_DIAG_INFO,
		CDK2_DXE_155, ((UINT64)exit_size << 32U) |
			(UINT32)(UINTN)status);
#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
	if (EFI_ERROR(status) && !fatal_display_failure) {
		struct bds_arch_protocol *bds = find_protocol(&bds_guid);

		cdk2_diag_phase("BootOrder", "failover", bds == NULL ||
			bds->entry == NULL ? EFI_NOT_FOUND : EFI_SUCCESS, 0U);
		if (bds != NULL && bds->entry != NULL) {
			EFI_STATUS display_status = prepare_bds_display(context);

#if CONFIG_CDK2_LVGL_RENDERER
			if (!EFI_ERROR(display_status)) {
				EFI_STATUS splash_status = show_linear_status(context,
					CDK2_LVGL_STATUS_BOOT_FAILED);

				cdk2_diag_phase("SplashStatus", "failover error",
					splash_status, CDK2_LINEAR_OS_HANDOFF);
			}
#endif
			if (EFI_ERROR(display_status))
				return display_status;
			bds->entry(bds);
			return EFI_DEVICE_ERROR;
		}
	}
#endif
	return status;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_handoff_direct_gate(
	const struct cdk2_dxe_dispatcher *dispatcher, void *load_image,
	void *start_image)
{
	struct cdk2_linear_state state;
	struct dxe_services_context context = { .linear_state = &state };
	struct cdk2_dxe_dispatcher saved_dispatcher = core.dispatcher;
	void *saved_load = core.boot_services.load_image;
	void *saved_start = core.boot_services.start_image;
	EFI_STATUS status;

	if (dispatcher == NULL || load_image == NULL || start_image == NULL)
		return EFI_INVALID_PARAMETER;
	cdk2_linear_state_init(&state);
	core.dispatcher = *dispatcher;
	core.boot_services.load_image = load_image;
	core.boot_services.start_image = start_image;
	handoff_direct_gate_test_entry = TRUE;
	status = handoff_operating_system(&context);
	handoff_direct_gate_test_entry = FALSE;
	core.boot_services.load_image = saved_load;
	core.boot_services.start_image = saved_start;
	core.dispatcher = saved_dispatcher;
	return status;
}

EFI_STATUS cdk2_dxe_test_handoff_boot_logo_failure(
	EFI_STATUS failure, void *start_image, bds_entry_fn *failover_entry,
	UINTN *activation_calls)
{
	struct cdk2_linear_state state = { 0 };
	struct dxe_services_context context = { .linear_state = &state };
#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
	struct bds_arch_protocol bds = { .entry = failover_entry };
	void *bds_handle = NULL;
#endif
	void *saved_start = core.boot_services.start_image;
	EFI_STATUS status;

	if (!EFI_ERROR(failure) || start_image == NULL || failover_entry == NULL ||
	    activation_calls == NULL)
		return EFI_INVALID_PARAMETER;
#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
	status = cdk2_dxe_install(&core.database, &bds_handle, &bds_guid, &bds);
	if (EFI_ERROR(status))
		return status;
#endif
	boot_logo_display_test_status = failure;
	handoff_boot_logo_test_entry = TRUE;
	loader_activation_test_calls = 0U;
	core.boot_services.start_image = start_image;
	status = handoff_operating_system(&context);
	core.boot_services.start_image = saved_start;
	*activation_calls = loader_activation_test_calls;
	handoff_boot_logo_test_entry = FALSE;
	boot_logo_display_test_status = EFI_SUCCESS;
#if CONFIG_CDK2_LINEAR_BDS_FAILOVER
	if (EFI_ERROR(cdk2_dxe_uninstall(&core.database, bds_handle, &bds_guid,
	    &bds)) && !EFI_ERROR(status))
		status = EFI_DEVICE_ERROR;
#endif
	return status;
}
#endif

#endif

EFI_STATUS cdk2_dxe_linear_ram_capsule_present(void *hob_list,
	BOOLEAN *present)
{
	EFI_HOB_HANDOFF_INFO_TABLE *handoff = hob_list;
	EFI_HOB_GENERIC_HEADER *hob;
	static const EFI_GUID fmp_capsule_guid = { 0x6dcbd5ed, 0xe82d, 0x4c44,
		{ 0xbd, 0xa1, 0x71, 0x94, 0x19, 0x9a, 0xd9, 0x2a } };
	UINTN end;
	UINTN capsule_count = 0U;

	if (present != NULL)
		*present = FALSE;
	if (handoff == NULL || present == NULL ||
	    handoff->header.hob_type != EFI_HOB_TYPE_HANDOFF ||
	    handoff->header.hob_length != sizeof(*handoff) ||
	    handoff->efi_end_of_hob_list <= (UINTN)handoff)
		return EFI_INVALID_PARAMETER;
	end = handoff->efi_end_of_hob_list;
	if (end > MAX_UINTN - sizeof(*hob))
		return EFI_COMPROMISED_DATA;
	for (hob = (void *)handoff; (UINTN)hob < end;
	     hob = (void *)((UINT8 *)hob + hob->hob_length)) {
		EFI_HOB_UEFI_CAPSULE *capsule_hob;
		EFI_HOB_GENERIC_HEADER *resource_hob;
		const struct cdk2_capsule_header *capsule;
		BOOLEAN in_ram = FALSE;
		UINT64 reserved_length;

		if (hob->hob_length < sizeof(*hob) ||
		    (hob->hob_length & 7U) != 0U ||
		    hob->hob_length > end - (UINTN)hob)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_type != EFI_HOB_TYPE_UEFI_CAPSULE)
			continue;
		capsule_count++;
		if (capsule_count != 1U)
			return EFI_COMPROMISED_DATA;
		if (hob->hob_length != sizeof(*capsule_hob))
			return EFI_COMPROMISED_DATA;
		capsule_hob = (void *)hob;
		if (capsule_hob->length == 0U ||
		    capsule_hob->base_address > MAX_UINT64 - capsule_hob->length ||
		    ((UINTN)capsule_hob->base_address & 7U) != 0U ||
		    (capsule_hob->base_address < end + sizeof(*hob) &&
		     capsule_hob->base_address + capsule_hob->length > (UINTN)handoff))
			return EFI_COMPROMISED_DATA;
		if (capsule_hob->length > MAX_UINT64 - 0xfffU)
			return EFI_COMPROMISED_DATA;
		reserved_length = (capsule_hob->length + 0xfffU) & ~0xfffULL;
		if (capsule_hob->base_address > MAX_UINT64 - reserved_length)
			return EFI_COMPROMISED_DATA;
		for (resource_hob = (void *)handoff; (UINTN)resource_hob < end;
		     resource_hob = (void *)((UINT8 *)resource_hob +
			resource_hob->hob_length)) {
			EFI_HOB_RESOURCE_DESCRIPTOR *resource;

			if (resource_hob->hob_length < sizeof(*resource_hob) ||
			    (resource_hob->hob_length & 7U) != 0U ||
			    resource_hob->hob_length > end - (UINTN)resource_hob)
				return EFI_COMPROMISED_DATA;
			if (resource_hob->hob_type != EFI_HOB_TYPE_RESOURCE_DESCRIPTOR)
				continue;
			if (resource_hob->hob_length != sizeof(*resource))
				return EFI_COMPROMISED_DATA;
			resource = (void *)resource_hob;
			if (resource->resource_length == 0U ||
			    resource->physical_start >
				MAX_UINT64 - resource->resource_length)
				return EFI_COMPROMISED_DATA;
			if (resource->physical_start <
				capsule_hob->base_address + capsule_hob->length &&
			    resource->physical_start + resource->resource_length >
				capsule_hob->base_address) {
				BOOLEAN system_memory;
				BOOLEAN reserved_capsule;

				system_memory = resource->resource_type ==
					EFI_RESOURCE_SYSTEM_MEMORY &&
					(resource->resource_attribute &
			     (EFI_RESOURCE_ATTRIBUTE_PRESENT |
			      EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
			      EFI_RESOURCE_ATTRIBUTE_TESTED)) ==
			     (EFI_RESOURCE_ATTRIBUTE_PRESENT |
			      EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
			      EFI_RESOURCE_ATTRIBUTE_TESTED);
				reserved_capsule = resource->resource_type ==
					EFI_RESOURCE_MEMORY_RESERVED &&
					resource->resource_attribute ==
					(EFI_RESOURCE_ATTRIBUTE_PRESENT |
					 EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
					 EFI_RESOURCE_ATTRIBUTE_TESTED |
					 EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
					 EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
					 EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
					 EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE) &&
					(capsule_hob->base_address & 0xfffU) == 0U &&
					(resource->physical_start & 0xfffU) == 0U &&
					(resource->resource_length & 0xfffU) == 0U;
				if ((!system_memory && !reserved_capsule) || in_ram ||
				    resource->physical_start > capsule_hob->base_address ||
				    resource->physical_start + resource->resource_length <
					capsule_hob->base_address +
					(reserved_capsule ? reserved_length :
					 capsule_hob->length))
					return EFI_COMPROMISED_DATA;
				in_ram = TRUE;
			}
		}
		if (!in_ram || capsule_hob->length < sizeof(*capsule))
			return EFI_COMPROMISED_DATA;
		capsule = (const void *)(UINTN)capsule_hob->base_address;
		if (capsule->header_size < sizeof(*capsule) ||
		    capsule->image_size < capsule->header_size ||
		    capsule->image_size != capsule_hob->length ||
		    (capsule->flags & ~CDK2_CAPSULE_FLAGS) != 0U ||
		    (capsule->flags & CDK2_CAPSULE_PERSIST) == 0U ||
		    ((capsule->flags & CDK2_CAPSULE_POPULATE) != 0U &&
		     memcmp(&capsule->guid, &fmp_capsule_guid,
			sizeof(capsule->guid)) == 0) ||
		    ((capsule->flags & CDK2_CAPSULE_RESET) != 0U &&
		     (capsule->flags & CDK2_CAPSULE_PERSIST) == 0U))
			return EFI_COMPROMISED_DATA;
	}
	hob = (void *)end;
	if (hob->hob_type != EFI_HOB_TYPE_END_OF_HOB_LIST ||
	    hob->hob_length != sizeof(*hob))
		return EFI_COMPROMISED_DATA;
	*present = capsule_count == 1U;
	return EFI_SUCCESS;
}

#if CONFIG_CDK2_LINEAR_BOOT
static uint64_t decide_capsule_path(void *opaque)
{
	struct dxe_services_context *context = opaque;
	BOOLEAN ram_present = FALSE;
	BOOLEAN disk_supported = FALSE;
	struct cdk2_capsule_delivery_policy delivery = { 0 };
	EFI_STATUS status;

	if (context == NULL)
		return EFI_INVALID_PARAMETER;
	if (core.runtime_storage == NULL)
		return EFI_UNSUPPORTED;
	(void)cdk2_capsule_delivery_policy_from_hobs(context->handoff, &delivery);
	{
		static CHAR16 name[] = L"OsIndicationsSupported";
		EFI_GUID global = { 0x8be4df61, 0x93ca, 0x11d2,
			{ 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c } };
		struct cdk2_dxe_runtime_services *runtime =
			core.runtime_storage->system_table.runtime_services;
		capsule_get_variable_fn *get;
		capsule_set_variable_fn *set;
		UINT64 supported = 0, original;
		UINTN size = sizeof(supported);
		UINT32 attributes = 0;

		if (runtime == NULL || runtime->get_variable == NULL || runtime->set_variable == NULL)
			return EFI_UNSUPPORTED;
		get = runtime->get_variable;
		set = runtime->set_variable;
		status = get(name, &global, &attributes, &size, &supported);
		if (status == EFI_NOT_FOUND)
			supported = 0;
		else if (EFI_ERROR(status))
			return status;
		else if (size != sizeof(supported) || attributes != 6U)
			return EFI_COMPROMISED_DATA;
		original = supported;
		if (delivery.allowed_transports & CB_CAPSULE_DELIVERY_DISK)
			supported |= 4ULL;
		else
			supported &= ~4ULL;
		if (status == EFI_NOT_FOUND || supported != original) {
			status = set(name, &global, 6U, sizeof(supported), &supported);
			if (EFI_ERROR(status))
				return status;
		}
	}
	if ((delivery.allowed_transports & CB_CAPSULE_DELIVERY_RAM) != 0U) {
		status = cdk2_dxe_linear_ram_capsule_present(context->handoff, &ram_present);
		if (EFI_ERROR(status))
			return status;
	}
	/* This is capability only.  CAPSULE_DISK finalizes DISK versus NONE after
	 * the ordered storage and filesystem phases perform real discovery.  An S4
	 * resume must not consume the pending disk request or perturb its memory map;
	 * the following normal boot performs the update. */
	disk_supported = (delivery.allowed_transports & CB_CAPSULE_DELIVERY_DISK) != 0U &&
		removable_discovery_allowed(context);
	return cdk2_linear_capsule_gate_early(context->linear_state, ram_present,
		disk_supported,
		&context->capsule_outcome);
}

static EFI_STATUS reset_after_disk_capsules(
	struct cdk2_dxe_system_table *system);

struct ram_capsule_protocol_view {
	struct cdk2_capsule_boot_protocol *interface;
	UINTN count, interface_size, interface_span;
	UINT64 interface_generation, span_generation;
	BOOLEAN dynamic, exact_owner, owner_live;
};

static EFI_STATUS validate_ram_capsule_protocol(
	const struct ram_capsule_protocol_view *view)
{
	if (view == NULL || view->count != 1U || view->interface == NULL)
		return EFI_COMPROMISED_DATA;
	if (view->dynamic || view->interface_size != 0U ||
		   view->interface_generation != 0U || !view->exact_owner ||
		   !view->owner_live || view->interface_span < sizeof(*view->interface))
		return EFI_COMPROMISED_DATA;
	if (view->interface->validate_capsule == NULL ||
	    view->interface->process_capsule == NULL ||
	    view->interface->process_scatter == NULL ||
	    view->interface->reset_required)
		return EFI_COMPROMISED_DATA;
	return EFI_SUCCESS;
}

static EFI_STATUS ram_capsule_protocol(
	void *expected_owner, struct cdk2_capsule_boot_protocol **processor)
{
	struct cdk2_dxe_protocol *match = NULL;
	struct ram_capsule_protocol_view view = { 0 };
	UINTN span = 0U, generation = 0U;
	BOOLEAN owned = FALSE;

	if (expected_owner == NULL || processor == NULL)
		return EFI_INVALID_PARAMETER;
	*processor = NULL;
	if (core.database.protocol_count > CDK2_DXE_MAX_PROTOCOLS ||
	    core.database.handle_count > CDK2_DXE_MAX_HANDLES ||
	    core.database.open_count > ARRAY_SIZE(core.database.opens) ||
	    core.images.count > CDK2_DXE_MAX_IMAGES)
		return EFI_COMPROMISED_DATA;
	for (UINTN index = 0U; index < core.database.protocol_count; index++) {
		struct cdk2_dxe_protocol *record = &core.database.protocols[index];

		if (memcmp(&record->guid, &capsule_boot_protocol_guid,
		    sizeof(capsule_boot_protocol_guid)) != 0)
			continue;
		view.count++;
		if (match != NULL)
			return EFI_COMPROMISED_DATA;
		match = record;
	}
	if (match == NULL || match->interface == NULL)
		return EFI_NOT_FOUND;
	view.interface = match->interface;
	view.dynamic = match->interface_dynamic;
	view.interface_size = match->interface_size;
	view.interface_generation = match->interface_generation;
	if (match->interface_dynamic) {
		if (match->owner_image != NULL || match->owner_generation != 0U ||
		    match->interface_size < sizeof(**processor) ||
		    match->interface_generation == 0U ||
		    EFI_ERROR(cdk2_dxe_memory_readable_span(&core.memory,
			match->interface, &span, &generation)) ||
		    span < sizeof(**processor) ||
		    generation != match->interface_generation)
			return EFI_COMPROMISED_DATA;
		view.interface_span = span;
		view.span_generation = generation;
	} else {
		if (match->interface_size != 0U || match->interface_generation != 0U ||
		    match->owner_image == NULL || match->owner_generation == 0U)
			return EFI_COMPROMISED_DATA;
		for (UINTN index = 0U; index < core.images.count; index++) {
			struct cdk2_dxe_image *image = &core.images.images[index];
			UINTN address = (UINTN)match->interface;
			UINTN image_base = (UINTN)image->base;

			if (image->handle == expected_owner &&
			    image->handle == match->owner_image && image->load_generation ==
			    match->owner_generation && image_base != 0U &&
			    image->size >= sizeof(**processor) && image->protocol_pins != 0U &&
			    image_base <= MAX_UINTN - image->size && address >= image_base &&
			    address <= MAX_UINTN -
				sizeof(**processor) && address + sizeof(**processor) <=
				image_base + image->size) {
				owned = TRUE;
				view.exact_owner = TRUE;
				view.owner_live = TRUE;
				view.interface_span = image_base + image->size - address;
				break;
			}
		}
		if (!owned)
			return EFI_COMPROMISED_DATA;
	}
	if (EFI_ERROR(validate_ram_capsule_protocol(&view)))
		return EFI_COMPROMISED_DATA;
	*processor = match->interface;
	return EFI_SUCCESS;
}

static EFI_STATUS ram_capsule_image(void *hob_list,
	const struct cdk2_capsule_header **capsule, UINTN *size)
{
	EFI_HOB_HANDOFF_INFO_TABLE *handoff = hob_list;
	EFI_HOB_GENERIC_HEADER *hob;
	BOOLEAN present;
	EFI_STATUS status;

	if (capsule == NULL || size == NULL)
		return EFI_INVALID_PARAMETER;
	*capsule = NULL;
	*size = 0U;
	status = cdk2_dxe_linear_ram_capsule_present(hob_list, &present);
	if (EFI_ERROR(status) || !present)
		return EFI_ERROR(status) ? status : EFI_NOT_FOUND;
	for (hob = hob_list; (UINTN)hob < handoff->efi_end_of_hob_list;
	     hob = (void *)((UINT8 *)hob + hob->hob_length))
		if (hob->hob_type == EFI_HOB_TYPE_UEFI_CAPSULE) {
			EFI_HOB_UEFI_CAPSULE *capsule_hob = (void *)hob;

			*capsule = (const void *)(UINTN)capsule_hob->base_address;
			*size = (UINTN)capsule_hob->length;
			return EFI_SUCCESS;
		}
	return EFI_COMPROMISED_DATA;
}

static EFI_STATUS apply_ram_capsule(
	struct cdk2_capsule_boot_protocol *processor,
	const struct cdk2_capsule_header *capsule, UINTN size)
{
	EFI_STATUS status;
	BOOLEAN expected_reset;

	if (processor == NULL || capsule == NULL ||
	    processor->validate_capsule == NULL ||
	    processor->process_capsule == NULL || processor->reset_required)
		return EFI_COMPROMISED_DATA;
	expected_reset = (capsule->flags & CDK2_CAPSULE_RESET) != 0U;
	memset(&processor->outcome, 0, sizeof(processor->outcome));
	status = processor->validate_capsule(processor, capsule, size);
	if (!EFI_ERROR(status))
		status = processor->process_capsule(processor, capsule, size);
	if (EFI_ERROR(status))
		return status;
	return expected_reset && !processor->reset_required ?
		EFI_COMPROMISED_DATA : EFI_SUCCESS;
}

typedef EFI_STATUS CDK2_MS_ABI capsule_next_variable_fn(UINTN *, CHAR16 *,
	EFI_GUID *);

static const EFI_GUID capsule_variable_guid = { 0x711c703f, 0xc285, 0x4b10,
	{ 0xa3, 0xb0, 0x36, 0xec, 0xbd, 0x3c, 0x8b, 0xe2 } };

static EFI_STATUS ram_capsule_source(struct cdk2_dxe_system_table *system,
	UINT64 *scatter)
{
	static const CHAR16 base[] = L"CapsuleUpdateData";
	CHAR16 name[128] = { 0 };
	EFI_GUID guid = { 0 };
	capsule_next_variable_fn *next;
	capsule_get_variable_fn *get;
	struct cdk2_dxe_runtime_services *runtime;
	UINTN matches = 0U;

	if (system == NULL || system->runtime_services == NULL || scatter == NULL)
		return EFI_COMPROMISED_DATA;
	runtime = system->runtime_services;
	next = runtime->get_next_variable_name;
	get = runtime->get_variable;
	if (next == NULL || get == NULL)
		return EFI_COMPROMISED_DATA;
	*scatter = 0U;
	for (;;) {
		UINTN bytes = sizeof(name), index, characters;
		EFI_STATUS status;

		status = next(&bytes, name, &guid);

		if (status == EFI_NOT_FOUND)
			break;
		if (EFI_ERROR(status) || bytes < sizeof(CHAR16) ||
		    (bytes & (sizeof(CHAR16) - 1U)) != 0U || bytes > sizeof(name))
			return EFI_COMPROMISED_DATA;
		characters = bytes / sizeof(CHAR16);
		if (name[characters - 1U] != 0U)
			return EFI_COMPROMISED_DATA;
		if (memcmp(&guid, &capsule_variable_guid, sizeof(guid)) != 0)
			continue;
		if (characters < ARRAY_SIZE(base))
			return EFI_COMPROMISED_DATA;
		index = 0U;
		while (index < ARRAY_SIZE(base) - 1U && name[index] == base[index])
			index++;
		if (index != ARRAY_SIZE(base) - 1U)
			continue;
		/* Every vendor variable in this namespace must have the exact grammar;
		 * the early path intentionally admits only the canonical sequence zero. */
		if (name[index] != 0U)
			return EFI_COMPROMISED_DATA;
		matches++;
	}
	if (matches == 0U)
		return EFI_NOT_FOUND;
	if (matches != 1U)
		return EFI_COMPROMISED_DATA;
	{
		UINTN size = sizeof(*scatter);
		UINT32 attributes = 0U;
		EFI_STATUS status = get((CHAR16 *)base,
			(EFI_GUID *)&capsule_variable_guid, &attributes, &size, scatter);

		if (EFI_ERROR(status) || attributes != 7U ||
		    size != sizeof(*scatter) || *scatter == 0U)
			return EFI_COMPROMISED_DATA;
	}
	return EFI_SUCCESS;
}

#if CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE && CONFIG_CDK2_NATIVE_SYSTEM_FMP && \
	CONFIG_PAYLOAD_DMA_HANDOFF
struct stage_storage_binding {
	struct driver_binding_view *interface;
	struct driver_binding_view callbacks;
	struct cdk2_dxe_image *owner;
	uint64_t load_generation, registration, transaction_allocation_generation;
	struct cdk2_dxe_image_transaction *transaction;
	struct cdk2_dxe_image_transaction transaction_snapshot;
};

struct stage_control_owner {
	struct cdk2_dxe_protocol record;
	struct cdk2_dxe_image *owner;
	uint64_t load_generation, transaction_allocation_generation;
	struct cdk2_dxe_image_transaction *transaction;
	struct cdk2_dxe_image_transaction transaction_snapshot;
};

/* Keep the original publication and resident code lifetime across callbacks. */
static uint64_t stage_control_current(const EFI_GUID *guid, size_t size,
	const void *interface, const void *callback,
	const struct stage_control_owner *expected, struct stage_control_owner *captured)
{
	struct cdk2_dxe_protocol *record;
	struct cdk2_dxe_image *owner;

	if (core.database.open_count > ARRAY_SIZE(core.database.opens) ||
	    private_control_record(&core, guid, size, &record, &owner) != EFI_SUCCESS ||
	    record->interface != interface ||
	    private_control_callback(&core, callback, &owner) != EFI_SUCCESS ||
	    !owner->started || !owner->exited || owner->exit_active ||
	    EFI_ERROR(owner->exit_status) || !owner->protocol_pins ||
	    EFI_ERROR(cdk2_dxe_image_transaction_validate_owned(&core.images,
		owner->transaction, owner->transaction_allocation_generation)) ||
	    owner->transaction->state != CDK2_DXE_IMAGE_PUBLISHED)
		return EFI_COMPROMISED_DATA;
	if (expected != NULL && (memcmp(&expected->record, record, sizeof(*record)) ||
	    expected->owner != owner || expected->load_generation != owner->load_generation ||
	    expected->transaction != owner->transaction ||
	    expected->transaction_allocation_generation != owner->transaction_allocation_generation ||
	    memcmp(&expected->transaction_snapshot, owner->transaction,
		sizeof(*owner->transaction))))
		return EFI_COMPROMISED_DATA;
	if (captured != NULL) {
		memcpy(&captured->record, record, sizeof(*record));
		captured->owner = owner;
		captured->load_generation = owner->load_generation;
		captured->transaction = owner->transaction;
		captured->transaction_allocation_generation = owner->transaction_allocation_generation;
		memcpy(&captured->transaction_snapshot, owner->transaction,
			sizeof(*owner->transaction));
	}
	return EFI_SUCCESS;
}

static uint64_t stage_storage_binding_current(const struct dispatched_binding *binding,
	const struct stage_storage_binding *expected, struct stage_storage_binding *captured)
{
	struct cdk2_dxe_protocol *record = NULL;
	struct cdk2_dxe_image *owner;
	struct driver_binding_view *view;
	size_t handles = 0;

	if (binding == NULL || binding->image == NULL || binding->binding == NULL ||
	    core.database.protocol_count > CDK2_DXE_MAX_PROTOCOLS ||
	    core.database.handle_count > CDK2_DXE_MAX_HANDLES ||
	    core.database.open_count > ARRAY_SIZE(core.database.opens) ||
	    core.images.count > CDK2_DXE_MAX_IMAGES)
		return EFI_COMPROMISED_DATA;
	for (size_t index = 0; index < core.database.protocol_count; index++) {
		struct cdk2_dxe_protocol *candidate = &core.database.protocols[index];

		if (candidate->handle != binding->binding ||
		    memcmp(&candidate->guid, &driver_binding_protocol, sizeof(candidate->guid)))
			continue;
		if (record != NULL)
			return EFI_COMPROMISED_DATA;
		record = candidate;
	}
	for (size_t index = 0; index < core.database.handle_count; index++)
		if (core.database.handles[index] == binding->binding)
			handles++;
	if (handles != 1 || record == NULL ||
	    (uintptr_t)record->interface % _Alignof(*view) ||
	    record->interface_dynamic || record->interface_size ||
	    record->interface_generation || record->owner_image != binding->image)
		return EFI_COMPROMISED_DATA;
	owner = private_control_image(&core, record->interface, sizeof(*view));
	if (owner == NULL || owner->handle != binding->image ||
	    owner->load_generation != record->owner_generation || !owner->protocol_pins ||
	    !owner->started || !owner->exited || owner->exit_active || EFI_ERROR(owner->exit_status) ||
	    EFI_ERROR(cdk2_dxe_image_transaction_validate_owned(&core.images,
		owner->transaction, owner->transaction_allocation_generation)) ||
	    owner->transaction->state != CDK2_DXE_IMAGE_PUBLISHED)
		return EFI_COMPROMISED_DATA;
	view = record->interface;
	if (view->image != binding->image || view->binding != binding->binding ||
	    private_control_callback(&core, view->supported, &owner) != EFI_SUCCESS ||
	    private_control_callback(&core, view->start, &owner) != EFI_SUCCESS ||
	    private_control_callback(&core, view->stop, &owner) != EFI_SUCCESS)
		return EFI_COMPROMISED_DATA;
	if (expected != NULL && (expected->interface != view || expected->owner != owner ||
	    expected->load_generation != owner->load_generation ||
	    expected->registration != record->registration ||
	    expected->transaction != owner->transaction ||
	    expected->transaction_allocation_generation != owner->transaction_allocation_generation ||
	    memcmp(&expected->callbacks, view, sizeof(*view)) ||
	    memcmp(&expected->transaction_snapshot, owner->transaction, sizeof(*owner->transaction))))
		return EFI_COMPROMISED_DATA;
	if (captured != NULL) {
		captured->interface = view;
		captured->owner = owner;
		captured->load_generation = owner->load_generation;
		captured->registration = record->registration;
		captured->transaction = owner->transaction;
		captured->transaction_allocation_generation = owner->transaction_allocation_generation;
		memcpy(&captured->callbacks, view, sizeof(*view));
		memcpy(&captured->transaction_snapshot, owner->transaction, sizeof(*owner->transaction));
	}
	return EFI_SUCCESS;
}
#endif

static uint64_t stage_disk_capsule(struct capsule_scan_runtime *runtime,
	const struct cdk2_capsule_image *images, size_t count)
{
#if CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE && CONFIG_CDK2_NATIVE_SYSTEM_FMP && \
	CONFIG_PAYLOAD_DMA_HANDOFF
	typedef EFI_STATUS CDK2_MS_ABI allocate_pages_fn(UINTN, UINT32, UINTN, UINT64 *);
	typedef EFI_STATUS CDK2_MS_ABI free_pages_fn(UINT64, UINTN);
	typedef EFI_STATUS CDK2_MS_ABI query_fn(
		const struct cdk2_capsule_header *const *, UINTN, UINT64 *, UINT32 *);
	typedef EFI_STATUS CDK2_MS_ABI update_fn(
		const struct cdk2_capsule_header *const *, UINTN, UINT64);
	struct cdk2_dxe_system_table *system;
	struct cdk2_dxe_runtime_services *services;
	struct cdk2_capsule_boot_protocol *current, *processor;
	struct cdk2_dxe_image *owner;
	struct cdk2_local_apic_timer_control *timer;
	struct cdk2_xhci_control *xhci = NULL;
	struct cdk2_pci_dma_control *pci;
	struct cdk2_local_apic_timer_control timer_snapshot;
	struct cdk2_xhci_control xhci_snapshot;
	struct cdk2_pci_dma_control pci_snapshot;
	static const EFI_GUID timer_guid = CDK2_LOCAL_APIC_TIMER_CONTROL_GUID;
	static const EFI_GUID pci_guid = CDK2_PCI_DMA_CONTROL_GUID;
	static const EFI_GUID xhci_guid = CDK2_XHCI_CONTROL_GUID;
	struct stage_control_owner timer_owner, pci_owner, xhci_owner, runtime_identity;
	struct dispatched_binding bindings[4];
	struct stage_storage_binding binding_snapshots[4];
	void *controllers[4], *device = NULL;
	const struct cdk2_capsule_header *capsule, *array[1];
	struct capsule_scatter_block *scatter;
	struct cdk2_tcg2_span span;
	uint8_t digest[32], current_digest[32];
	UINT64 pending, address = 0, maximum = 0;
	uint64_t owner_generation;
	void *runtime_owner;
	size_t offset, pages, size, matches = 0, namespace_matches = 0;
	uint32_t reset_type = 0;
	UINTN previous_tpl;
	allocate_pages_fn *allocate = core.boot_services.allocate_pages;
	free_pages_fn *release = core.boot_services.free_pages;
	disconnect_controller_fn *disconnect = core.boot_services.disconnect_controller;
	query_fn *query;
	update_fn *update;
	uint64_t status;

	if (runtime == NULL || runtime->context == NULL || runtime->result == NULL ||
	    images == NULL || count != 1 || runtime->teardown_begun ||
	    runtime->result->loaded != runtime->result->candidates ||
	    runtime->result->cleanup_failed || selected_storage_backend != STORAGE_NVME)
		return EFI_UNSUPPORTED;
	if (core.runtime_storage == NULL || allocate == NULL || release == NULL ||
	    disconnect == NULL || core.database.open_count > ARRAY_SIZE(core.database.opens) ||
	    core.database.protocol_count > CDK2_DXE_MAX_PROTOCOLS ||
	    core.database.handle_count > CDK2_DXE_MAX_HANDLES ||
	    core.images.count > CDK2_DXE_MAX_IMAGES)
		return EFI_COMPROMISED_DATA;
	system = &core.runtime_storage->system_table;
	services = system->runtime_services;
	runtime_owner = runtime->context->capsule_runtime_owner;
	processor = runtime->processor;
	if (services == NULL || services->query_capsule_capabilities == NULL ||
	    services->update_capsule == NULL ||
	    EFI_ERROR(ram_capsule_protocol(runtime_owner, &current)) ||
	    current != processor ||
	    (current->delivery_policy.allowed_transports &
	     (CB_CAPSULE_DELIVERY_RAM | CB_CAPSULE_DELIVERY_DISK)) !=
	     (CB_CAPSULE_DELIVERY_RAM | CB_CAPSULE_DELIVERY_DISK))
		return EFI_COMPROMISED_DATA;
	owner = private_control_image(&core, current, sizeof(*current));
	query = services->query_capsule_capabilities;
	update = services->update_capsule;
	if (owner == NULL || owner->handle != runtime_owner || owner->exit_active ||
	    private_control_callback(&core, query, &owner) != EFI_SUCCESS ||
	    private_control_callback(&core, update, &owner) != EFI_SUCCESS ||
	    stage_control_current(&capsule_boot_protocol_guid, sizeof(*processor), processor, update,
		NULL, &runtime_identity) != EFI_SUCCESS)
		return EFI_COMPROMISED_DATA;
	owner_generation = owner->load_generation;
	status = ram_capsule_source(system, &pending);
	if (status != EFI_NOT_FOUND)
		return status == EFI_SUCCESS ? EFI_ALREADY_STARTED : status;
	size = images[0].size;
	capsule = images[0].data;
	if (capsule == NULL || (uintptr_t)capsule % _Alignof(*capsule) ||
	    size < sizeof(*capsule) || size > UINT32_MAX ||
	    (uintptr_t)capsule > UINTPTR_MAX - size || capsule->image_size != size ||
	    memcmp(&capsule->guid, &images[0].guid, sizeof(capsule->guid)) ||
	    capsule->flags != images[0].flags ||
	    capsule->flags != (CDK2_CAPSULE_PERSIST | CDK2_CAPSULE_RESET) ||
	    size > SIZE_MAX - 7 - 2 * sizeof(*scatter))
		return EFI_UNSUPPORTED;
	offset = (size + 7) & ~(size_t)7;
	if (offset + 2 * sizeof(*scatter) > SIZE_MAX - 4095)
		return EFI_OUT_OF_RESOURCES;
	pages = (offset + 2 * sizeof(*scatter) + 4095) / 4096;
	span = (struct cdk2_tcg2_span) { .data = (const void *)capsule, .size = size };
	status = cdk2_software_hash_spans(NULL, TPM_ALG_SHA256, &span, 1, digest, sizeof(digest));
	if (status != EFI_SUCCESS)
		return status;
	status = allocate(0, 0, pages, &address);
	if (status != EFI_SUCCESS)
		return status;
	if (!address || address % 4096 || address > UINTPTR_MAX - pages * 4096) {
		return EFI_COMPROMISED_DATA;
	}
	memcpy((void *)(uintptr_t)address, capsule, size);
	array[0] = (const void *)(uintptr_t)address;
	scatter = (void *)(uintptr_t)(address + offset);
	scatter[0] = (struct capsule_scatter_block) { size, address };
	scatter[1] = (struct capsule_scatter_block) { 0 };
	status = query(array, 1, &maximum, &reset_type);
	if (status != EFI_SUCCESS || maximum < size || reset_type != 1) {
		(void)release(address, pages);
		return status != EFI_SUCCESS ? status : EFI_COMPROMISED_DATA;
	}
	status = ram_capsule_source(system, &pending);
	if (status != EFI_NOT_FOUND)
		goto compromised;
	span.data = (const void *)(uintptr_t)address;
	status = cdk2_software_hash_spans(NULL, TPM_ALG_SHA256, &span, 1,
		current_digest, sizeof(current_digest));
	if (status != EFI_SUCCESS || memcmp(digest, current_digest, sizeof(digest)) ||
	    array[0] != (const void *)(uintptr_t)address ||
	    runtime->context->capsule_runtime_owner != runtime_owner ||
	    owner->load_generation != owner_generation ||
	    system->runtime_services != services || services->query_capsule_capabilities != query ||
	    services->update_capsule != update ||
	    EFI_ERROR(ram_capsule_protocol(runtime_owner, &current)) ||
	    current != processor || runtime->processor != processor ||
	    stage_control_current(&capsule_boot_protocol_guid, sizeof(*processor), processor, update,
		&runtime_identity, NULL) != EFI_SUCCESS)
		goto compromised;
	bindings[0] = storage_bindings.fat;
	bindings[1] = storage_bindings.partition;
	bindings[2] = storage_bindings.disk;
	bindings[3] = storage_bindings.nvme;
	for (size_t index = 0; index < core.database.open_count; index++) {
		const struct cdk2_dxe_open *open = &core.database.opens[index];

		if (open->controller != selected_esp || open->agent != bindings[1].binding ||
		    open->attributes != 0x08 || memcmp(&open->guid, &disk_io_protocol, sizeof(open->guid)))
			continue;
		if (open->count != 1 || ++matches != 1)
			goto compromised;
		device = open->handle;
	}
	if (matches != 1 || device == NULL || selected_storage_controller == NULL ||
	    selected_storage_binding != bindings[3].image ||
	    selected_storage_agent != bindings[3].binding ||
	    private_timer_control(&core, &timer) != EFI_SUCCESS ||
	    private_pci_dma_control(&core, &pci) != EFI_SUCCESS)
		goto compromised;
	if (cdk2_dxe_validate_nvme_children(&core.database, selected_storage_controller,
	    selected_storage_agent) != EFI_SUCCESS)
		goto compromised;
	for (size_t index = 0; index < core.database.open_count; index++) {
		const struct cdk2_dxe_open *open = &core.database.opens[index];

		if (open->handle != selected_storage_controller ||
		    open->agent != selected_storage_agent || open->controller != device ||
		    open->attributes != 0x08 || memcmp(&open->guid, &pci_io_protocol, sizeof(open->guid)))
			continue;
		if (open->count != 1 || ++namespace_matches != 1)
			goto compromised;
	}
	if (namespace_matches != 1)
		goto compromised;
	status = private_xhci_control(&core, NULL, NULL, &xhci);
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		goto compromised;
	memcpy(&timer_snapshot, timer, sizeof(timer_snapshot));
	memcpy(&pci_snapshot, pci, sizeof(pci_snapshot));
	if (xhci != NULL)
		memcpy(&xhci_snapshot, xhci, sizeof(xhci_snapshot));
	if (stage_control_current(&timer_guid, sizeof(*timer), timer, timer->deactivate,
	    NULL, &timer_owner) != EFI_SUCCESS ||
	    stage_control_current(&pci_guid, sizeof(*pci), pci, pci->quiesce_all,
	    NULL, &pci_owner) != EFI_SUCCESS ||
	    (xhci != NULL && stage_control_current(&xhci_guid, sizeof(*xhci), xhci,
		xhci->quiesce, NULL, &xhci_owner) != EFI_SUCCESS))
		goto compromised;
	controllers[0] = selected_esp;
	controllers[1] = device;
	controllers[2] = device;
	controllers[3] = selected_storage_controller;
	for (size_t index = 0; index < ARRAY_SIZE(bindings); index++)
		if (stage_storage_binding_current(&bindings[index], NULL,
		    &binding_snapshots[index]) != EFI_SUCCESS)
			goto compromised;
	/* Scanner file handles are already closed. No later source completion is safe. */
	runtime->teardown_begun = true;
	for (size_t index = 0; index < ARRAY_SIZE(bindings); index++) {
		status = stage_storage_binding_current(&bindings[index], &binding_snapshots[index], NULL);
		if (status == EFI_SUCCESS)
			status = disconnect(controllers[index], bindings[index].image, NULL);
		if (status == EFI_SUCCESS)
			status = stage_storage_binding_current(&bindings[index], &binding_snapshots[index], NULL);
		if (status != EFI_SUCCESS)
			return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
	}
	previous_tpl = core.events.current_tpl;
	core.events.current_tpl = 31;
	{
		struct cdk2_local_apic_timer_control *current_timer;
		struct cdk2_xhci_control *current_xhci = NULL;
		struct cdk2_pci_dma_control *current_pci;

		status = private_timer_control(&core, &current_timer);
		if (status == EFI_SUCCESS && (current_timer != timer ||
		    memcmp(timer, &timer_snapshot, sizeof(timer_snapshot))))
			status = EFI_COMPROMISED_DATA;
		if (status == EFI_SUCCESS)
			status = stage_control_current(&timer_guid, sizeof(*timer), timer,
				timer->deactivate, &timer_owner, NULL);
		if (status == EFI_SUCCESS)
			status = timer->deactivate(timer);
		if (status == EFI_SUCCESS)
			status = stage_control_current(&timer_guid, sizeof(*timer), timer,
				timer_snapshot.deactivate, &timer_owner, NULL);
		if (status == EFI_SUCCESS) {
			status = private_xhci_control(&core, NULL, NULL, &current_xhci);
			if (status == EFI_NOT_FOUND && xhci == NULL)
				status = EFI_SUCCESS;
			if (status == EFI_SUCCESS && (current_xhci != xhci ||
			    (xhci != NULL && memcmp(xhci, &xhci_snapshot, sizeof(xhci_snapshot)))))
				status = EFI_COMPROMISED_DATA;
		}
		if (status == EFI_SUCCESS && xhci != NULL) {
			status = stage_control_current(&xhci_guid, sizeof(*xhci), xhci,
				xhci->quiesce, &xhci_owner, NULL);
			if (status == EFI_SUCCESS)
				status = xhci->quiesce(xhci, xhci->controller);
			if (status == EFI_SUCCESS)
				status = stage_control_current(&xhci_guid, sizeof(*xhci), xhci,
					xhci_snapshot.quiesce, &xhci_owner, NULL);
		}
		if (status == EFI_SUCCESS)
			status = private_pci_dma_control(&core, &current_pci);
		if (status == EFI_SUCCESS && (current_pci != pci ||
		    memcmp(pci, &pci_snapshot, sizeof(pci_snapshot))))
			status = EFI_COMPROMISED_DATA;
		if (status == EFI_SUCCESS)
			status = stage_control_current(&pci_guid, sizeof(*pci), pci,
				pci->quiesce_all, &pci_owner, NULL);
		if (status == EFI_SUCCESS)
			status = pci->quiesce_all(pci);
		if (status == EFI_SUCCESS)
			status = stage_control_current(&pci_guid, sizeof(*pci), pci,
				pci_snapshot.quiesce_all, &pci_owner, NULL);
	}
	core.events.current_tpl = previous_tpl;
	if (status != EFI_SUCCESS)
		return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
	status = ram_capsule_source(system, &pending);
	if (status != EFI_NOT_FOUND)
		return EFI_COMPROMISED_DATA;
	span.data = (const void *)(uintptr_t)address;
	status = cdk2_software_hash_spans(NULL, TPM_ALG_SHA256, &span, 1,
		current_digest, sizeof(current_digest));
	if (status != EFI_SUCCESS || memcmp(digest, current_digest, sizeof(digest)) ||
	    array[0] != (const void *)(uintptr_t)address ||
	    scatter[0].length != size || scatter[0].address != address ||
	    scatter[1].length || scatter[1].address ||
	    system->runtime_services != services || services->query_capsule_capabilities != query ||
	    services->update_capsule != update ||
	    runtime->context->capsule_runtime_owner != runtime_owner ||
	    owner->handle != runtime_owner || owner->load_generation != owner_generation ||
	    owner->exit_active ||
	    EFI_ERROR(ram_capsule_protocol(runtime_owner, &current)) ||
	    current != processor || runtime->processor != processor ||
	    private_control_callback(&core, update, &owner) != EFI_SUCCESS ||
	    stage_control_current(&capsule_boot_protocol_guid, sizeof(*processor), processor, update,
		&runtime_identity, NULL) != EFI_SUCCESS)
		return EFI_COMPROMISED_DATA;
	/* The real public service owns publication and reset. A return proves neither. */
	(void)update(array, 1, (uint64_t)(uintptr_t)scatter);
	return EFI_DEVICE_ERROR;
compromised:
	(void)release(address, pages);
	return EFI_COMPROMISED_DATA;
#else
	(void)runtime;
	(void)images;
	(void)count;
	return EFI_UNSUPPORTED;
#endif
}

static EFI_STATUS retire_ram_capsule_source(struct cdk2_dxe_system_table *system,
	UINT64 snapshot)
{
	static CHAR16 base[] = L"CapsuleUpdateData";
	capsule_get_variable_fn *get;
	capsule_set_variable_fn *set;
	struct cdk2_dxe_runtime_services *runtime;
	UINT64 current = 0U;
	UINTN size = sizeof(current);
	UINT32 attributes = 0U;
	EFI_STATUS status;

	if (system == NULL || system->runtime_services == NULL)
		return EFI_COMPROMISED_DATA;
	runtime = system->runtime_services;
	get = runtime->get_variable;
	set = runtime->set_variable;
	if (get == NULL || set == NULL)
		return EFI_COMPROMISED_DATA;
	status = get(base, (EFI_GUID *)&capsule_variable_guid, &attributes, &size,
		&current);
	if (EFI_ERROR(status) || attributes != 7U ||
	    size != sizeof(current) || current != snapshot)
		return EFI_COMPROMISED_DATA;
	status = set(base, (EFI_GUID *)&capsule_variable_guid, 0U, 0U, NULL);
	if (EFI_ERROR(status))
		return status;
	size = sizeof(current);
	status = get(base, (EFI_GUID *)&capsule_variable_guid, &attributes, &size,
		&current);
	if (status != EFI_NOT_FOUND)
		return EFI_COMPROMISED_DATA;
	status = ram_capsule_source(system, &current);
	return status == EFI_NOT_FOUND ? EFI_SUCCESS : EFI_COMPROMISED_DATA;
}

static EFI_STATUS close_ram_window(struct dxe_services_context *context)
{
	if (!context->capsule_client.initialized)
		return EFI_SUCCESS;
	return cdk2_system_fmp_client_close_ram(&context->capsule_client) ==
		CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

static bool certified_ram_check_refusal(struct dxe_services_context *context,
	struct cdk2_capsule_boot_protocol *processor,
	const struct cdk2_capsule_header *capsule, size_t size, EFI_STATUS failure)
{
#if CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE && CONFIG_CDK2_NATIVE_SYSTEM_FMP
	static const EFI_GUID check_guid = CDK2_CAPSULE_CHECK_OUTCOME_GUID;
	struct cdk2_capsule_boot_protocol *current = NULL;
	struct cdk2_capsule_process_outcome process;
	struct cdk2_capsule_check_outcome check;
	struct cdk2_dxe_protocol *fmp_record = NULL, *check_record = NULL;
	struct cdk2_dxe_image *owner = NULL;
	struct cdk2_software_hash hash;
	uint8_t digest[32];
	uintptr_t outer = (uintptr_t)capsule, inner, base, fmp, outcome;
	bool source_current = false;

	if (context == NULL ||
	    (failure != EFI_DEVICE_ERROR && failure != EFI_UNSUPPORTED) ||
	    core.database.protocol_count > CDK2_DXE_MAX_PROTOCOLS ||
	    core.images.count > CDK2_DXE_MAX_IMAGES ||
	    core.dispatcher.count > CDK2_DXE_MAX_DRIVERS ||
	    !context->capsule_client.initialized ||
	    EFI_ERROR(ram_capsule_protocol(context->capsule_runtime_owner, &current)) ||
	    current != processor || processor->reset_required)
		return false;
	process = processor->outcome;
	if (process.active || !process.completed || process.set_begun ||
	    process.unproved || process.matching_checks != 1U ||
	    process.capsule != capsule || process.capsule_size != size ||
	    process.failure != failure || process.fmp_handle == NULL ||
	    process.fmp_protocol == NULL || process.check == NULL ||
	    process.image == NULL || process.image_size == 0U)
		return false;
	inner = (uintptr_t)process.image;
	if (outer > UINTPTR_MAX - size || inner < outer || inner - outer > size ||
	    process.image_size > size - (inner - outer))
		return false;
	for (UINTN index = 0U; index < core.database.protocol_count; index++) {
		struct cdk2_dxe_protocol *record = &core.database.protocols[index];
		struct cdk2_dxe_protocol **match;

		if (record->handle != process.fmp_handle)
			continue;
		if (memcmp(&record->guid, &firmware_management_protocol_guid,
			   sizeof(record->guid)) == 0)
			match = &fmp_record;
		else if (memcmp(&record->guid, &check_guid, sizeof(check_guid)) == 0)
			match = &check_record;
		else
			continue;
		if (*match != NULL || record->interface_dynamic ||
		    record->interface_size != 0U || record->interface_generation != 0U ||
		    record->owner_image != context->capsule_fmp_owner ||
		    record->owner_generation != context->capsule_fmp_generation)
			return false;
		*match = record;
	}
	if (fmp_record == NULL || check_record == NULL ||
	    fmp_record->interface != process.fmp_protocol ||
	    check_record->interface != process.check)
		return false;
	for (UINTN index = 0U; index < core.images.count; index++) {
		struct cdk2_dxe_image *image = &core.images.images[index];

		if (image->handle != context->capsule_fmp_owner)
			continue;
		if (owner != NULL || !image->started || !image->exited ||
		    EFI_ERROR(image->exit_status) ||
		    image->load_generation != context->capsule_fmp_generation ||
		    image->protocol_pins < 2U || image->base == NULL || image->size == 0U ||
		    (uintptr_t)image->base > UINTPTR_MAX - image->size)
			return false;
		owner = image;
	}
	for (UINTN index = 0U; index < core.dispatcher.count; index++) {
		const struct cdk2_dxe_driver *driver = &core.dispatcher.drivers[index];

		if (driver == context->capsule_fmp_source && driver->started &&
		    !driver->failed && memcmp(&driver->guid, &capsule_disk_drivers[1].guid,
					     sizeof(driver->guid)) == 0)
			source_current = true;
	}
	if (owner == NULL || !source_current ||
	    EFI_ERROR(cdk2_dxe_image_transaction_validate_owned(&core.images,
		owner->transaction, owner->transaction_allocation_generation)))
		return false;
	if (owner->transaction->state != CDK2_DXE_IMAGE_PUBLISHED ||
	    owner->transaction->generation != context->capsule_fmp_transaction_generation ||
	    context->capsule_fmp_source->image != context->capsule_fmp_source_image ||
	    context->capsule_fmp_source->image_size != context->capsule_fmp_source_size ||
	    context->capsule_fmp_source_size == 0U ||
	    owner->transaction->source_size != context->capsule_fmp_source->image_size ||
	    context->capsule_fmp_source->image == NULL ||
	    memcmp(owner->transaction->source, context->capsule_fmp_source->image,
		   owner->transaction->source_size) != 0)
		return false;
	base = (uintptr_t)owner->base;
	fmp = (uintptr_t)process.fmp_protocol;
	outcome = (uintptr_t)process.check;

	if (fmp < base || fmp - base > owner->size ||
	    fmp % _Alignof(struct cdk2_fmp_protocol) != 0U ||
	    sizeof(struct cdk2_fmp_protocol) > owner->size - (fmp - base) ||
	    outcome < base || outcome - base > owner->size ||
	    outcome % _Alignof(struct cdk2_capsule_check_outcome) != 0U ||
	    sizeof(check) > owner->size - (outcome - base))
		return false;
	check = *process.check;
	if (check.protocol != process.fmp_protocol ||
	    check.image != process.image || check.image_size != process.image_size ||
	    check.image_index != process.image_index ||
	    memcmp(&check.image_type, &context->capsule_client.endpoint.image_type,
		   sizeof(check.image_type)) != 0 ||
	    check.generation != context->capsule_client.endpoint.generation)
		return false;
	if (failure == EFI_DEVICE_ERROR) {
		if (!check.signature_refused || check.local_floor_refused ||
		    check.transaction == 0U)
			return false;
	} else {
		/* This is sealed local version policy, not a CMS or MM verdict. */
		if (check.signature_refused || !check.local_floor_refused ||
		    check.transaction != 0U ||
		    check.sealed_floor < context->capsule_client.endpoint.lowest_supported_version ||
		    check.attempted_version >= check.sealed_floor)
			return false;
	}
	if (EFI_ERROR(cdk2_software_hash_init(&hash, TPM_ALG_SHA256)) ||
	    EFI_ERROR(cdk2_software_hash_update(&hash, process.image, process.image_size)) ||
	    EFI_ERROR(cdk2_software_hash_final(&hash, digest, sizeof(digest))) ||
	    memcmp(digest, check.digest, sizeof(digest)) != 0)
		return false;
	return true;
#else
	(void)context;
	(void)processor;
	(void)capsule;
	(void)size;
	(void)failure;
	return false;
#endif
}

static EFI_STATUS finish_ram_capsule(struct cdk2_dxe_system_table *system,
	struct cdk2_capsule_boot_protocol *processor,
	const struct cdk2_capsule_header *capsule, UINTN size, UINT64 source,
	struct dxe_services_context *context)
{
	struct cdk2_capsule_header header;
	bool header_valid = capsule != NULL && size >= sizeof(header) &&
		(uintptr_t)capsule <= UINTPTR_MAX - size;

	if (header_valid)
		memcpy(&header, capsule, sizeof(header));
	EFI_STATUS status = apply_ram_capsule(processor, capsule, size);

	if (EFI_ERROR(status) && header_valid &&
	    memcmp(&header, capsule, sizeof(header)) == 0 &&
	    certified_ram_check_refusal(context, processor, capsule, size, status)) {
		EFI_STATUS rejected = status;
		struct cdk2_capsule_process_outcome process;
		struct cdk2_capsule_check_outcome check;

		memcpy(&process, &processor->outcome, sizeof(process));
		memcpy(&check, process.check, sizeof(check));

		status = close_ram_window(context);
		if (EFI_ERROR(status) ||
		    !certified_ram_check_refusal(context, processor, capsule, size, rejected) ||
		    memcmp(&header, capsule, sizeof(header)) != 0 ||
		    memcmp(&process, &processor->outcome, sizeof(process)) != 0 ||
		    memcmp(&check, process.check, sizeof(check)) != 0)
			return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
		struct cdk2_runtime_services_view runtime;

		if (system == NULL || system->runtime_services == NULL)
			return EFI_COMPROMISED_DATA;
		memcpy(&runtime, system->runtime_services, sizeof(runtime));
		status = cdk2_capsule_report_fmp(&runtime, &header.guid,
			&check.image_type, check.image_index, process.payload_index, rejected);
		if (EFI_ERROR(status) ||
		    !certified_ram_check_refusal(context, processor, capsule, size, rejected) ||
		    memcmp(&header, capsule, sizeof(header)) != 0 ||
		    memcmp(&process, &processor->outcome, sizeof(process)) != 0 ||
		    memcmp(&check, process.check, sizeof(check)) != 0)
			return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
		status = retire_ram_capsule_source(system, source);
		if (EFI_ERROR(status) ||
		    !certified_ram_check_refusal(context, processor, capsule, size, rejected) ||
		    memcmp(&header, capsule, sizeof(header)) != 0 ||
		    memcmp(&process, &processor->outcome, sizeof(process)) != 0 ||
		    memcmp(&check, process.check, sizeof(check)) != 0)
			return EFI_ERROR(status) ? status : EFI_COMPROMISED_DATA;
		status = cdk2_linear_capsule_finalize_ram(context->linear_state,
			&context->capsule_outcome);
		if (EFI_ERROR(status))
			return status;
		cdk2_diag_value(CDK2_DIAG_DXE_MAIN, rejected == EFI_DEVICE_ERROR ?
			"RAM capsule signature refused" : "RAM capsule local floor refused", rejected);
		return EFI_SUCCESS;
	}
	if (!EFI_ERROR(status))
		status = retire_ram_capsule_source(system, source);
	if (EFI_ERROR(status))
		return status;
	if (context != NULL) {
		status = close_ram_window(context);
		if (EFI_ERROR(status))
			return status;
	}
	return processor->reset_required ? reset_after_disk_capsules(system) :
		EFI_SUCCESS;
}

#ifdef CDK2_HOST_TEST
EFI_STATUS cdk2_dxe_test_ram_capsule_source(
	struct cdk2_dxe_system_table *system, UINT64 *scatter)
{
	return ram_capsule_source(system, scatter);
}

EFI_STATUS cdk2_dxe_test_retire_ram_capsule_source(
	struct cdk2_dxe_system_table *system, UINT64 scatter)
{
	return retire_ram_capsule_source(system, scatter);
}

EFI_STATUS cdk2_dxe_test_finish_ram_capsule(
	struct cdk2_dxe_system_table *system,
	struct cdk2_capsule_boot_protocol *processor,
	const struct cdk2_capsule_header *capsule, UINTN size, UINT64 source)
{
	return finish_ram_capsule(system, processor, capsule, size, source, NULL);
}

EFI_STATUS cdk2_dxe_test_validate_ram_capsule_protocol(
	struct cdk2_capsule_boot_protocol *interface, UINTN count,
	BOOLEAN dynamic, UINTN interface_size, UINT64 interface_generation,
	UINTN span, UINT64 span_generation, BOOLEAN exact_owner,
	BOOLEAN owner_live)
{
	const struct ram_capsule_protocol_view view = { interface, count,
		interface_size, span, interface_generation, span_generation,
		dynamic, exact_owner, owner_live };

	return validate_ram_capsule_protocol(&view);
}

EFI_STATUS cdk2_dxe_test_apply_ram_capsule(
	struct cdk2_capsule_boot_protocol *processor,
	const struct cdk2_capsule_header *capsule, UINTN size)
{
	return apply_ram_capsule(processor, capsule, size);
}
#endif

static uint64_t gate_ram_capsule(void *opaque)
{
	struct dxe_services_context *context = opaque;
	struct cdk2_capsule_boot_protocol *processor;
	const struct cdk2_capsule_header *capsule;
	UINTN size;
	UINTN image_count;
	void *runtime_owner;
	UINT64 capsule_source;
	EFI_STATUS status;

	if (cdk2_linear_capsule_outcome(context->linear_state) !=
	    context->capsule_outcome) {
		status = EFI_COMPROMISED_DATA;
		goto out;
	}
	if (context->capsule_outcome != CDK2_LINEAR_CAPSULE_RESULT_RAM) {
		status = EFI_SUCCESS;
		goto out;
	}
	status = ram_capsule_source(core.runtime_storage == NULL ? NULL :
		&core.runtime_storage->system_table, &capsule_source);
	if (EFI_ERROR(status))
		goto out;
	image_count = core.images.count;
	runtime_owner = context->capsule_runtime_owner;
	status = runtime_owner == NULL ? EFI_NOT_FOUND : EFI_SUCCESS;
	if (!EFI_ERROR(status) && core.images.count != image_count)
		status = EFI_COMPROMISED_DATA;
	if (!EFI_ERROR(status))
		status = ram_capsule_protocol(runtime_owner, &processor);
	if (!EFI_ERROR(status))
		status = ram_capsule_image(context->handoff, &capsule, &size);
	if (!EFI_ERROR(status))
		status = finish_ram_capsule(core.runtime_storage == NULL ? NULL :
			&core.runtime_storage->system_table, processor, capsule, size,
			capsule_source, context);
out:
	{
		EFI_STATUS closed = close_ram_window(context);

		if (EFI_ERROR(closed))
			return closed;
	}
	return status;
}

static uint64_t dxe_linear_clock(void *context)
{
	uint32_t low, high;

	(void)context;
	__asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
	return ((uint64_t)high << 32) | low;
}

static void dxe_linear_report(enum cdk2_linear_phase_id phase,
	const char *name, uint64_t status, uint64_t elapsed, uint8_t required,
	uint8_t begin, void *context)
{
	struct dxe_services_context *services = context;
	enum cdk2_lvgl_status_message message = CDK2_LVGL_STATUS_MESSAGE_COUNT;

	(void)required;
#if CONFIG_CDK2_LVGL_RENDERER
	/* On-screen status is independent of diagnostic output. */
	if (!begin && EFI_ERROR(status))
		message = CDK2_LVGL_STATUS_BOOT_FAILED;
	else if (begin && phase == CDK2_LINEAR_BOOT_POLICY)
		message = CDK2_LVGL_STATUS_SELECTING_BOOT;
	else if (begin && phase == CDK2_LINEAR_OS_HANDOFF)
		message = CDK2_LVGL_STATUS_STARTING_OS;
	else if (!begin && phase == CDK2_LINEAR_INPUT_UI)
		message = CDK2_LVGL_STATUS_PREPARING;
	if (message != CDK2_LVGL_STATUS_MESSAGE_COUNT) {
		EFI_STATUS splash_status = show_linear_status(services, message);

		cdk2_diag_phase("SplashStatus",
			message == CDK2_LVGL_STATUS_BOOT_FAILED ? "error" : "phase",
			splash_status, phase);
	}
#else
	(void)services;
	(void)message;
#endif
	cdk2_diag_phase_event(begin ? CDK2_EVENT_LINEAR_PHASE_BEGIN(phase) :
		CDK2_EVENT_LINEAR_PHASE_END(phase), name,
		begin ? "begin" : (status != EFI_SUCCESS ? "failed" : "complete"),
		status, elapsed);
}
#endif

EFI_STATUS CDK2_MS_ABI cdk2_dxe_core_entry(void *hob_list)
{
	EFI_HOB_HANDOFF_INFO_TABLE *handoff = hob_list;
	struct dxe_services_context services = { .handoff = handoff };
	EFI_STATUS status;
#if CONFIG_CDK2_LINEAR_BOOT
	struct cdk2_linear_state linear_state;
	static const struct cdk2_linear_phase phases[] = {
		{ CDK2_LINEAR_DXE_SERVICES, "DXE_SERVICES",
			initialize_dxe_services, TRUE },
		{ CDK2_LINEAR_ARCH_PROTOCOLS, "ARCH_PROTOCOLS",
			initialize_arch_protocols, TRUE },
		{ CDK2_LINEAR_VARIABLES_MIN, "VARIABLES_MIN",
			initialize_minimal_variables, TRUE },
		{ CDK2_LINEAR_CAPSULE_DECIDE, "CAPSULE_EARLY_GATE",
			decide_capsule_path, TRUE },
		{ CDK2_LINEAR_CAPSULE_RAM, "CAPSULE_RAM",
			gate_ram_capsule, TRUE },
		{ CDK2_LINEAR_PLATFORM_TABLES, "PLATFORM_TABLES",
			initialize_platform_tables, TRUE },
		{ CDK2_LINEAR_PCI_ROOTS, "PCI_ROOTS",
			initialize_pci_roots, TRUE },
		{ CDK2_LINEAR_PCI_ENUMERATE, "PCI_ENUMERATE",
			initialize_pci_enumeration, TRUE },
		{ CDK2_LINEAR_STORAGE_CONTROLLERS, "STORAGE_CONTROLLERS",
			initialize_storage_controllers, TRUE },
		{ CDK2_LINEAR_BLOCK_DISCOVERY, "BLOCK_DISCOVERY",
			initialize_block_discovery, TRUE },
		{ CDK2_LINEAR_FILESYSTEMS, "FILESYSTEMS",
			initialize_filesystems, TRUE },
		{ CDK2_LINEAR_CAPSULE_DISK, "CAPSULE_DISK",
			process_disk_capsules, TRUE },
		{ CDK2_LINEAR_DISPLAY_ADOPT, "DISPLAY_ADOPT",
			adopt_display, TRUE },
		{ CDK2_LINEAR_INPUT_UI, "INPUT_UI",
			initialize_input_ui, TRUE },
		{ CDK2_LINEAR_BOOT_POLICY, "BOOT_POLICY",
			select_boot_policy, TRUE },
		{ CDK2_LINEAR_OS_HANDOFF, "OS_HANDOFF",
			handoff_operating_system, TRUE },
	};
	const struct cdk2_linear_plan plan = {
		.phases = phases,
		.count = ARRAY_SIZE(phases),
		.clock = dxe_linear_clock,
		.report = dxe_linear_report,
		.context = &services,
	};
	size_t completed;
#endif

	if (handoff == NULL || handoff->header.hob_type != EFI_HOB_TYPE_HANDOFF ||
	    handoff->header.hob_length != sizeof(*handoff) ||
	    handoff->efi_free_memory_top <= handoff->efi_free_memory_bottom)
		return EFI_INVALID_PARAMETER;
#if CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME && \
	(!CONFIG_CDK2_STRICT_DIRECT_RUNTIME || !CONFIG_CDK2_LINEAR_BOOT || \
	 !CONFIG_CDK2_NATIVE_VARIABLE_RUNTIME || !CONFIG_CDK2_NATIVE_SECURITY_STUB)
	/*
	 * Only the source-closed native linear graph may reach the mandatory
	 * protected owner and deny-capable Security2 authorizer installation.
	 * Build shape is not authority: their real validation must still succeed
	 * before the required variable phase or OS image dispatch can complete.
	 */
	return EFI_UNSUPPORTED;
#endif
	/* Absence or rejection preserves the allocation-free COM1 fallback. */
	(void)cdk2_diag_configure_hob(handoff);
#if CONFIG_CDK2_LINEAR_BOOT
	{
		struct cdk2_coreboot_handoff imported = {0};
		struct cdk2_system_fmp_transport endpoint = {0};
		const void *descriptor;
		const struct cdk2_system_fmp_transport_port port = {
			.publish = cdk2_authvar_native_x86_cache,
			.acquire = cdk2_authvar_native_x86_cache,
			.barrier = cdk2_authvar_native_x86_barrier,
			.trigger_apm_io8 = cdk2_authvar_native_x86_trigger,
		};

		status = cdk2_coreboot_handoff_from_hobs(handoff, &imported);
		if (EFI_ERROR(status))
			return status;
		status = cdk2_coreboot_find_unique_record(&imported,
			CB_TAG_CAPSULE_BROKER_ENDPOINT,
			sizeof(struct cb_capsule_broker_endpoint), &descriptor);
		if (status != EFI_NOT_FOUND && EFI_ERROR(status))
			return status;
		if (!EFI_ERROR(status)) {
			status = cdk2_system_fmp_transport_from_coreboot(&imported, &endpoint);
			if (EFI_ERROR(status))
				return status == EFI_NOT_FOUND ? EFI_COMPROMISED_DATA : status;
		}
		if (!EFI_ERROR(status) &&
		    endpoint.revision == CB_CAPSULE_BROKER_ENDPOINT_RAM_REVISION &&
		    cdk2_system_fmp_client_init(&services.capsule_client, &endpoint,
			&port) != CDK2_SYSTEM_FMP_TRANSPORT_SUCCESS)
			return EFI_COMPROMISED_DATA;
	}
	status = cdk2_linear_state_from_hob(handoff, &linear_state);
	if (EFI_ERROR(status) ||
	    linear_state.next_phase != CDK2_LINEAR_DXE_SERVICES) {
		EFI_STATUS closed = close_ram_window(&services);

		return EFI_ERROR(closed) ? closed : EFI_COMPROMISED_DATA;
	}
	services.linear_state = &linear_state;
	status = cdk2_linear_run(&plan, &linear_state, &completed);
	{
		EFI_STATUS closed = close_ram_window(&services);

		if (EFI_ERROR(closed))
			return closed;
	}
	status = finish_loader_services(&services, status);
	{
		EFI_STATUS cleanup = release_boot_graphics_snapshots(&services);

		if (EFI_ERROR(cleanup))
			CDK2_DXE_DIAG(CDK2_DIAG_ERROR, CDK2_DXE_155, cleanup);
	}
	if (status != EFI_SUCCESS)
		return status;
	return finish_loader_services(&services, EFI_COMPROMISED_DATA);
#endif
}
