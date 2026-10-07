/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <boot/capsule_broker_buffers.h>
#include <boot/coreboot_tables.h>
#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <boot/payload_mm_authvar_service_receiver.h>
#include <boot_device.h>
#include <cpu/x86/smm_command.h>
#include <fmap.h>
#include <payload_mm_fmp_auth_policy.h>
#include <string.h>

#include "native_service.h"
#include "q35_dma_cold.h"
#include "../../../lib/capsule_broker_info_internal.h"
#include "../../../lib/capsule_qemu_pflash_internal.h"
#include "../../../lib/payload_mm_crypto/crypto.h"
#include "../../../lib/payload_mm_fmp_checkpoint_internal.h"
#include "../../../lib/payload_mm_fmp_dispatch_internal.h"
#include "../../../lib/payload_mm_fmp_transaction_internal.h"

#if !ENV_SMM
#error "Q35 capsule broker installation is private SMM-only"
#endif

_Static_assert(CAPSULE_BROKER_APM_COMMAND == SMM_APMC_CAPSULE_BROKER,
	"capsule broker publication and dispatcher commands differ");

static struct capsule_broker_policy policy;
static struct fmap_inventory inventory;
static struct capsule_broker_buffer_reservation buffers;
static struct payload_mm_fmp_dispatch_workspace dispatch_workspace;
static struct payload_mm_fmp_checkpoint_workspace checkpoint_workspace;

static bool reservations_current(void *context)
{
	struct capsule_broker_buffer_reservation observed;

	return context && capsule_broker_buffers_get(&observed) &&
		!memcmp(context, &observed, sizeof(observed));
}

static bool communication_reserved(void *context, uint64_t base, uint64_t size)
{
	const struct capsule_broker_buffer_reservation *owned = context;

	return reservations_current(context) && base == owned->communication_base &&
		size == owned->communication_size &&
		owned->communication_reserved_size >= size;
}

static bool staging_reserved(void *context, uint64_t base, uint64_t size)
{
	const struct capsule_broker_buffer_reservation *owned = context;

	return reservations_current(context) && base == owned->staging_base &&
		size == owned->staging_size;
}

static bool execution_owner_current(void *context)
{
	return reservations_current(context) &&
		(platform_payload_mm_authvar_service_finalize_admitted() ||
		 q35_capsule_service_current());
}

static bool dma_protected(void *context, uint64_t base, uint64_t size)
{
	if (!communication_reserved(context, base, size) &&
	    !staging_reserved(context, base, size))
		return false;
	if (platform_payload_mm_authvar_service_finalize_admitted())
		return q35_dma_cold_current() &&
			platform_payload_mm_authvar_service_finalize_admitted();
	return q35_capsule_ram_dma_current();
}

static enum cb_err sha256(void *context, const void *data, size_t size,
	uint8_t digest[CAPSULE_BROKER_DIGEST_SIZE])
{
	return !context && payload_mm_sha256(data, size, digest) == PAYLOAD_MM_VERIFY_OK ?
		CB_SUCCESS : CB_ERR;
}

enum cb_err q35_capsule_broker_boot_install(void)
{
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	struct capsule_broker_scratch_reservation scratch;
	struct lb_efi_fw_info firmware;
	struct capsule_broker_info_policy info;
	const struct region_device *root;
	const struct fmap_area *store = NULL, *core = NULL, *probe = NULL;
	size_t media_size;

	if (!platform_payload_mm_authvar_service_finalize_admitted() ||
	    !q35_capsule_storage_is_protected(NULL, &policy, sizeof(policy)) ||
	    !q35_capsule_storage_is_protected(NULL, &inventory, sizeof(inventory)) ||
	    !q35_capsule_storage_is_protected(NULL, &buffers, sizeof(buffers)) ||
	    payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS ||
	    !binding->generation || !capsule_broker_buffers_get(&buffers))
		return CB_ERR;
	boot_device_init();
	root = boot_device_ro();
	if (!root || !(media_size = region_device_sz(root)) ||
	    media_size != CONFIG_ROM_SIZE || media_size % 4096U ||
	    fmap_read_inventory(&inventory) || inventory.size != media_size ||
	    !inventory.area_count || inventory.area_count > CAPSULE_BROKER_MAX_FMAP_AREAS ||
	    efi_fw_info_get(&firmware) != CB_SUCCESS || firmware.fw_size != media_size ||
	    !platform_payload_mm_authvar_service_finalize_admitted())
		return CB_ERR;
	for (size_t i = 0; i < inventory.area_count; i++) {
		const struct fmap_area *area = &inventory.area[i];

		if (!strcmp((const char *)area->name, "SMMSTORE")) {
			if (store)
				return CB_ERR;
			store = area;
		} else if (!strcmp((const char *)area->name, "COREBOOT")) {
			if (core)
				return CB_ERR;
			core = area;
		} else if (!strcmp((const char *)area->name, "SECURE_PROBE")) {
			if (probe)
				return CB_ERR;
			probe = area;
		}
	}
	if (!store || !core || !probe || probe->size != 4096U ||
	    store->offset > media_size || store->size > media_size - store->offset ||
	    probe->offset < store->offset + store->size ||
	    probe->offset > media_size - probe->size ||
	    core->offset < probe->offset + probe->size ||
	    buffers.communication_size != CAPSULE_BROKER_TRANSPORT_SIZE ||
	    buffers.communication_reserved_size < buffers.communication_size ||
	    buffers.staging_size < media_size || buffers.staging_size > SIZE_MAX ||
	    buffers.staging_base > UINTPTR_MAX ||
	    capsule_broker_scratch_acquire(4096U, &scratch) != CB_SUCCESS ||
	    !platform_payload_mm_authvar_service_finalize_admitted())
		return CB_ERR;
	memset(&policy, 0, sizeof(policy));
	policy.revision = CAPSULE_BROKER_POLICY_RAM_REVISION;
	policy.size = sizeof(policy);
	policy.endpoint = (struct lb_capsule_broker_endpoint) {
		.tag = LB_TAG_CAPSULE_BROKER_ENDPOINT,
		.size = sizeof(policy.endpoint),
		.revision = LB_CAPSULE_BROKER_ENDPOINT_RAM_REVISION,
		.header_size = sizeof(policy.endpoint),
		.flags = LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS,
		.generation = binding->generation,
		.communication_base = buffers.communication_base,
		.communication_size = CAPSULE_BROKER_TRANSPORT_SIZE,
		.message_size = CAPSULE_BROKER_TRANSPORT_SIZE,
		.staging_base = buffers.staging_base,
		.staging_size = buffers.staging_size,
		.transport = LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8,
		.trigger_width = sizeof(uint8_t),
		.trigger_address = CAPSULE_BROKER_APM_PORT,
		.trigger_value = CAPSULE_BROKER_APM_COMMAND,
	};
	policy.raw_image_size = media_size;
	policy.boot_media_size = media_size;
	policy.smmstore_offset = store->offset;
	policy.smmstore_size = store->size;
	policy.erase_size = 4096U;
	policy.region_count = 1;
	policy.regions[0] = (struct lb_capsule_update_region) {
		.image_offset = core->offset,
		.flash_offset = core->offset,
		.size = core->size,
		.flags = LB_CAPSULE_REGION_BIOS,
	};
	policy.fmap_area_count = inventory.area_count;
	memcpy(policy.fmap_areas, inventory.area,
		inventory.area_count * sizeof(inventory.area[0]));
	policy.write_layout = (struct capsule_write_layout) {
		.revision = CAPSULE_WRITE_LAYOUT_REVISION,
		.size = sizeof(policy.write_layout),
		.media_size = media_size,
		.erase_size = 4096U,
		.route_count = 1,
		.smmstore = { store->offset, store->size },
		.route = { policy.regions[0] },
	};
	if (capsule_qemu_pflash_backend_build(&policy.write_layout,
		(uintptr_t)buffers.staging_base, buffers.staging_size,
		q35_capsule_storage_is_protected, NULL, &policy.media,
		&policy.media_context_size) != CB_SUCCESS ||
	    !platform_payload_mm_authvar_service_finalize_admitted())
		return CB_ERR;
	policy.write_scratch = (void *)(uintptr_t)scratch.write_base;
	policy.write_scratch_size = scratch.erase_size;
	policy.scratch = (void *)(uintptr_t)scratch.read_base;
	policy.scratch_size = scratch.erase_size;
	policy.sha256 = sha256;
	policy.authenticate = payload_mm_fmp_authenticate_provider;
	policy.proofs = (struct capsule_broker_proofs) {
		.communication_reserved = communication_reserved,
		.staging_reserved = staging_reserved,
		.dma_protected = dma_protected,
		.smm_spi_owned = q35_capsule_writes_are_private,
		.no_raw_flash = q35_capsule_writes_are_private,
		.cpu_rendezvous_active = execution_owner_current,
		.context = &buffers,
		.context_size = sizeof(buffers),
	};
	memset(&info, 0, sizeof(info));
	info.revision = CAPSULE_BROKER_INFO_POLICY_REVISION;
	info.size = sizeof(info);
	memcpy(info.image_type.b, firmware.guid, sizeof(info.image_type.b));
	info.current_version = firmware.version;
	info.lowest_supported_version = firmware.lowest_supported_version;
	info.image_size = firmware.fw_size;
	info.capabilities = LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES;
	if (capsule_broker_policy_install(&policy, q35_capsule_storage_is_protected, NULL) !=
		CB_SUCCESS || !platform_payload_mm_authvar_service_finalize_admitted() ||
	    capsule_broker_info_policy_install(&info, q35_capsule_storage_is_protected, NULL) !=
		CB_SUCCESS || !platform_payload_mm_authvar_service_finalize_admitted() ||
	    payload_mm_fmp_dispatch_workspace_install(&dispatch_workspace,
		q35_capsule_storage_is_protected, NULL) != CB_SUCCESS ||
	    !platform_payload_mm_authvar_service_finalize_admitted() ||
	    payload_mm_fmp_checkpoint_owner_bind(binding->generation, &checkpoint_workspace,
		q35_capsule_storage_is_protected, NULL) != CB_SUCCESS ||
	    !platform_payload_mm_authvar_service_finalize_admitted() ||
	    payload_mm_fmp_transaction_install(q35_capsule_storage_is_protected, NULL) !=
		CB_SUCCESS || !platform_payload_mm_authvar_service_finalize_admitted())
		goto failed_install;
	memset(&info, 0, sizeof(info));
	memset(&firmware, 0, sizeof(firmware));
	memset(&scratch, 0, sizeof(scratch));
	return CB_SUCCESS;
failed_install:
	capsule_broker_close_for_s3();
	memset(&info, 0, sizeof(info));
	memset(&firmware, 0, sizeof(firmware));
	memset(&scratch, 0, sizeof(scratch));
	return CB_ERR;
}
