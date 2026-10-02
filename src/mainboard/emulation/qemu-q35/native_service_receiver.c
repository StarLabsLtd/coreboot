/* SPDX-License-Identifier: GPL-2.0-only */

#include "native_cause.h"
#include "native_service.h"
#include "public_service.h"
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_service_receiver.h>
#include <boot/payload_mm_authvar_smm_bootstrap.h>
#include <boot/payload_mm_fmp_boot.h>
#include <boot/coreboot_tables.h>
#include <boot_device.h>
#include <cbfs.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_entry.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <cpu/x86/smm_pre_lock_dispatch.h>
#include <fmap.h>
#include <payload_mm_fmp_auth_policy.h>
#include <string.h>

#if !ENV_SMM
#error "Q35 native service receiver is SMM-only"
#endif

enum service_phase { SERVICE_EMPTY, SERVICE_INSTALLING, SERVICE_FINALIZING,
	SERVICE_READY, SERVICE_EXECUTING };

static struct {
	uint32_t phase;
	uint64_t probe_offset;
	uint64_t probe_size;
} service;

#if CONFIG(Q35_SMM_CAPSULE_BROKER_AUTH_POLICY)
static uint8_t capsule_trust_xdr[4 + ALIGN_UP(PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE, 4)];
_Static_assert(sizeof(capsule_trust_xdr) <= PAYLOAD_MM_MAX_TRUST_XDR_SIZE,
	"single certificate must fit the protected trust policy");
#endif

static struct {
	bool active;
	uint64_t request;
	const struct smm_invocation_save_state_ops *ops;
	struct smm_invocation_save_state_ops sealed_ops;
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_topology topology;
	struct smm_invocation_token token;
	struct smm_invocation_entry_ticket ticket;
	struct smm_apmc_selection_receipt selection;
	struct smm_apmc_selection_receipt consumed;
} wave;

static bool protected_storage(const void *base, size_t size)
{
	const struct smm_invocation_runtime_view *view;

	return smm_invocation_runtime_view_get(&view) == CB_SUCCESS &&
		smm_invocation_runtime_range_is_protected(view, base, size) == CB_SUCCESS;
}

static bool protected_fmp_storage(void *context, const void *base, size_t size)
{
	return context == NULL && protected_storage(base, size);
}

static bool claim_current(void)
{
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_topology topology;
	struct smm_invocation_token token;
	uint64_t wire;

	return protected_storage(&wave, sizeof(wave)) && wave.active &&
		protected_storage(&service, sizeof(service)) && q35_native_cause_current() &&
		wave.ops && protected_storage(wave.ops, sizeof(*wave.ops)) &&
		!memcmp(wave.ops, &wave.sealed_ops, sizeof(wave.sealed_ops)) &&
		smm_invocation_runtime_binding_get(&runtime) == CB_SUCCESS &&
		!memcmp(&runtime, &wave.runtime, sizeof(runtime)) &&
		smm_invocation_topology_read(runtime.topology, &topology) == CB_SUCCESS &&
		!memcmp(&topology, &wave.topology, sizeof(topology)) &&
		topology.active_cpus == 1 && !topology.bsp_cpu &&
		wave.ticket.cpu == 0 && wave.ticket.generation == wave.token.smi_generation &&
		wave.ticket.command == SMM_APMC_AUTHVAR_SERVICE &&
		wave.consumed.revision == SMM_APMC_SELECTION_RECEIPT_REVISION &&
		wave.consumed.size == sizeof(wave.consumed) &&
		wave.consumed.identity == (uintptr_t)&wave.selection &&
		wave.consumed.generation && !wave.consumed.reserved &&
		wave.consumed.descriptor.command == SMM_APMC_AUTHVAR_SERVICE &&
		wave.consumed.descriptor.owner == SMM_APMC_OWNER_AUTHVAR_SERVICE &&
		wave.consumed.descriptor.role == SMM_APMC_EXCLUSIVE &&
		wave.consumed.descriptor.binding_count == 1 &&
		!wave.consumed.descriptor.observer_count && wave.consumed.descriptor.enabled &&
		wave.consumed.descriptor.reserved &&
		smm_invocation_evidence_claimed_snapshot(runtime.evidence,
			SMM_APMC_AUTHVAR_SERVICE, wave.request, &token) == CB_SUCCESS &&
		!memcmp(&token, &wave.token, sizeof(token)) && token.bsp == 1 &&
		!token.initiator_cpu && token.active_cpus == 1 &&
		wave.ops->read_value(wave.ops->context, 0, &wire) == CB_SUCCESS &&
		wire == wave.request;
}

static bool probe_current(void)
{
	const struct region_device *root = boot_device_ro();
	struct region_device probe, store, core;
	uint64_t size;
	const volatile uint8_t *byte;

	if (!root || !(size = region_device_sz(root)) || size < 4096 || size > UINT32_MAX ||
	    fmap_locate_area_as_rdev("SECURE_PROBE", &probe) ||
	    fmap_locate_area_as_rdev("SMMSTORE", &store) ||
	    fmap_locate_area_as_rdev("COREBOOT", &core) ||
	    region_device_offset(&store) > size ||
	    region_device_sz(&store) > size - region_device_offset(&store) ||
	    region_device_sz(&probe) != 4096 ||
	    region_device_offset(&probe) > size - 4096 ||
	    region_device_offset(&probe) != service.probe_offset || service.probe_size != 4096 ||
	    region_device_offset(&probe) < region_device_offset(&store) + region_device_sz(&store) ||
	    region_device_offset(&probe) + 4096 > region_device_offset(&core))
		return false;
	byte = (void *)(uintptr_t)(0x100000000ULL - size + service.probe_offset + 4095);
	return *byte == 0xff;
}

static bool writes_are_private(void *unused)
{
	(void)unused;
	/* Controlled secure-pflash launch plus the actual cold denial observation. */
	return !CONFIG(SMMSTORE) && !CONFIG(SMMSTORE_FULL_FLASH_ACCESS) &&
		(claim_current() || (CONFIG(Q35_SMM_INVOCATION_NATIVE_PUBLIC_SERVICE_COMPONENT) &&
		 q35_public_service_current())) && probe_current();
}

bool platform_payload_mm_authvar_service_bootstrap_admitted(void)
{
	return service.phase == SERVICE_INSTALLING &&
		(uint32_t)wave.request == Q35_NATIVE_BOOTSTRAP_WIRE_REQUEST && claim_current();
}

bool platform_payload_mm_authvar_service_finalize_admitted(void)
{
	return service.phase == SERVICE_FINALIZING &&
		(uint32_t)wave.request == Q35_NATIVE_BOOTSTRAP_WIRE_REQUEST && claim_current();
}

bool platform_payload_mm_authvar_service_runtime_admitted(void)
{
	return (service.phase == SERVICE_EXECUTING && wave.request == SMM_APMC_AUTHVAR_SERVICE &&
		claim_current()) ||
		(CONFIG(Q35_SMM_INVOCATION_NATIVE_PUBLIC_SERVICE_COMPONENT) &&
		 service.phase == SERVICE_EXECUTING && q35_public_service_current());
}

static enum cb_err bootstrap(void)
{
	struct q35_native_bootstrap_frame snapshot;
	struct payload_mm_authvar_presence_bootstrap_receipts receipts;
	struct payload_mm_authvar_presence_bootstrap *slot;
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	struct payload_mm_authvar_smm_bootstrap input;
	const struct lb_authvar_service_endpoint empty = { 0 };
	struct q35_native_bootstrap_frame *frame = (void *)(uintptr_t)(wave.request >> 32);
	struct bootmem_reservation_receipt page;
	uintptr_t smram_base;
	size_t smram_size;

	smm_region(&smram_base, &smram_size);
	if (service.phase != SERVICE_EMPTY || !claim_current() || !smram_size ||
	    (uintptr_t)frame < 0x100000 || (uintptr_t)frame % 4096 ||
	    smram_base < sizeof(snapshot) ||
	    (uintptr_t)frame > smram_base - sizeof(snapshot) ||
	    !protected_storage(&snapshot, sizeof(snapshot)))
		return CB_ERR;
	/* The transport is untrusted; authority comes from the existing loader receipts. */
	snapshot = *frame;
	if (snapshot.revision != Q35_NATIVE_BOOTSTRAP_REVISION ||
	    snapshot.size != sizeof(snapshot) || snapshot.state != Q35_NATIVE_BOOTSTRAP_REQUEST ||
	    snapshot.reserved || snapshot.maximum_cpus || snapshot.write_denied != 1 ||
	    memcmp(&snapshot.endpoint, &empty, sizeof(empty)) ||
	    snapshot.receipts.page.base != (uintptr_t)frame ||
	    snapshot.receipts.page.bytes != 4096)
		return CB_ERR;
	receipts = snapshot.receipts;
	if (payload_mm_authvar_presence_bootstrap_receipts_import(&receipts) != CB_SUCCESS ||
	    !(slot = smm_get_payload_mm_authvar_presence_bootstrap()) ||
	    !protected_storage(slot, sizeof(*slot)))
		return CB_ERR;
	page = slot->page_receipt;
	if (memcmp(&page, &snapshot.receipts.page, sizeof(page)) ||
	    bootmem_reservation_receipt_verify_consume_exact_tag(&slot->page_verifier,
		&slot->page_receipt, BM_MEM_RESERVED) != CB_SUCCESS ||
	    !claim_current() ||
	    payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS)
		return CB_ERR;
	service.probe_offset = snapshot.probe_offset;
	service.probe_size = snapshot.probe_size;
	service.phase = SERVICE_INSTALLING;
	input = (struct payload_mm_authvar_smm_bootstrap) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP_REVISION, .size = sizeof(input),
		.cold_boot_generation = binding->generation,
		.spi_writes_restricted_to_smm = writes_are_private,
	};
	if (!probe_current() || payload_mm_authvar_smm_service_bootstrap_install(&input) !=
		CB_SUCCESS || !claim_current())
		return CB_ERR;
	printk(BIOS_DEBUG, "Q35 native service: protected backend installed\n");
	service.phase = SERVICE_FINALIZING;
	if (payload_mm_authvar_service_finalize() != CB_SUCCESS ||
	    memcmp(frame, &snapshot, sizeof(snapshot)) ||
	    payload_mm_authvar_service_descriptor_copy(&snapshot.endpoint) != CB_SUCCESS ||
	    !claim_current())
		return CB_ERR;
	printk(BIOS_DEBUG, "Q35 native service: endpoint finalized\n");
	if (CONFIG(Q35_SMM_CAPSULE_BROKER)) {
		struct lb_efi_fw_info firmware;
		struct payload_mm_fmp_state_policy state = {
			.revision = PAYLOAD_MM_FMP_STATE_POLICY_REVISION,
			.size = sizeof(state),
		};

		/* The immutable private cold bootstrap trusts this preinstalled image.
		 * Never import identity from the descriptive payload-visible table. */
		printk(BIOS_DEBUG, "Q35 capsule owner: private bootstrap ready\n");
		if (!claim_current() || efi_fw_info_get(&firmware) != CB_SUCCESS)
			return CB_ERR;
		memcpy(state.namespace_guid.b, firmware.guid, sizeof(state.namespace_guid.b));
		state.trusted_lowest_version = firmware.lowest_supported_version;
		if (payload_mm_fmp_state_policy_install(&state, protected_fmp_storage, NULL) !=
		    CB_SUCCESS)
			return CB_ERR;
		printk(BIOS_DEBUG, "Q35 capsule owner: trusted state policy installed\n");
		if (payload_mm_fmp_owner_authvar_boot_install(protected_fmp_storage, NULL) !=
		    CB_SUCCESS)
			return CB_ERR;
		printk(BIOS_DEBUG, "Q35 capsule owner: protected store reconciled\n");
		if (!claim_current())
			return CB_ERR;
#if CONFIG(Q35_SMM_CAPSULE_BROKER_AUTH_POLICY)
		{
			enum cbfs_type type = CBFS_TYPE_RAW;
			size_t certificate_size = 0;
			enum cb_err status = CB_ERR;
			struct payload_mm_fmp_auth_policy auth = {
				.revision = PAYLOAD_MM_FMP_AUTH_POLICY_REVISION,
				.size = sizeof(auth),
				.trusted_lowest_version = firmware.lowest_supported_version,
				.image_size = firmware.fw_size,
				.trust_xdr = capsule_trust_xdr,
				.mainboard_vendor = CONFIG_MAINBOARD_VENDOR,
				.mainboard_vendor_size = sizeof(CONFIG_MAINBOARD_VENDOR) - 1,
				.mainboard_part = CONFIG_MAINBOARD_PART_NUMBER,
				.mainboard_part_size = sizeof(CONFIG_MAINBOARD_PART_NUMBER) - 1,
			};

			memcpy(auth.image_type.b, firmware.guid, sizeof(auth.image_type.b));
			memset(capsule_trust_xdr, 0, sizeof(capsule_trust_xdr));
			if (protected_storage(capsule_trust_xdr, sizeof(capsule_trust_xdr)) &&
			    claim_current())
				certificate_size = cbfs_ro_type_load("capsule/trust.der",
					capsule_trust_xdr + 4, PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE,
					&type);
			if (type == CBFS_TYPE_RAW && certificate_size &&
			    certificate_size <= PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE &&
			    claim_current()) {
				capsule_trust_xdr[0] = certificate_size >> 24;
				capsule_trust_xdr[1] = certificate_size >> 16;
				capsule_trust_xdr[2] = certificate_size >> 8;
				capsule_trust_xdr[3] = certificate_size;
				auth.trust_xdr_size = 4 + ALIGN_UP(certificate_size, 4);
				if (claim_current())
					status = payload_mm_fmp_auth_policy_install(&auth,
						protected_fmp_storage, NULL);
			}
			memset(capsule_trust_xdr, 0, sizeof(capsule_trust_xdr));
			memset(&auth, 0, sizeof(auth));
			if (status != CB_SUCCESS || !claim_current())
				return CB_ERR;
			printk(BIOS_DEBUG, "Q35 capsule owner: protected authentication installed\n");
		}
#endif
		memset(&state, 0, sizeof(state));
		memset(&firmware, 0, sizeof(firmware));
	}
	snapshot.maximum_cpus = 1;
	snapshot.state = Q35_NATIVE_BOOTSTRAP_COMPLETE;
	*frame = snapshot;
	service.phase = SERVICE_READY;
	memset(&snapshot, 0, sizeof(snapshot));
	memset(&receipts, 0, sizeof(receipts));
	memset(&page, 0, sizeof(page));
	memset(&input, 0, sizeof(input));
	return CB_SUCCESS;
}

enum smm_pre_lock_dispatch_result smm_pre_lock_dispatch(uint32_t cpu, uint32_t initial_apic_id)
{
	const struct smm_invocation_entry_policy policy = {
		.revision = SMM_INVOCATION_ENTRY_POLICY_REVISION, .size = sizeof(policy),
		.max_polls = SMM_INVOCATION_ENTRY_MAX_POLLS,
	};
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_entry_cause cause;
	uint64_t result;

	if (apm_get_apmc() != SMM_APMC_AUTHVAR_SERVICE)
		return SMM_PRE_LOCK_DISPATCH_NOT_HANDLED;
	if (CONFIG(Q35_SMM_INVOCATION_NATIVE_PUBLIC_SERVICE_COMPONENT) &&
	    service.phase == SERVICE_READY) {
		enum q35_public_service_result public =
			q35_public_service_begin(cpu, initial_apic_id);

		if (wave.active || !protected_storage(&service, sizeof(service)) ||
		    public == Q35_PUBLIC_SERVICE_ERROR)
			smm_invocation_platform_fail_stop();
		if (public == Q35_PUBLIC_SERVICE_HELD) {
			enum cb_err status;

			service.phase = SERVICE_EXECUTING;
			/* A malformed or already-completed public mailbox is a refusal. */
			status = payload_mm_authvar_service_execute();
			if ((status != CB_SUCCESS && status != CB_ERR_ARG) ||
			    !q35_public_service_current())
				smm_invocation_platform_fail_stop();
			service.phase = SERVICE_READY;
			q35_public_service_end();
		}
		return SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED;
	}
	if (cpu || wave.active || !protected_storage(&wave, sizeof(wave)) ||
	    smm_invocation_runtime_binding_get(&wave.runtime) != CB_SUCCESS ||
	    smm_invocation_topology_read(wave.runtime.topology, &wave.topology) != CB_SUCCESS ||
	    wave.topology.active_cpus != 1 || wave.topology.bsp_cpu ||
	    wave.topology.initial_apic_ids[0] != initial_apic_id ||
	    smm_invocation_loader_instance_read(wave.runtime.instance, &instance) != CB_SUCCESS ||
	    instance.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    q35_native_cause_begin(SMM_APMC_AUTHVAR_SERVICE, &wave.ops) != CB_SUCCESS ||
	    wave.ops->read_value(wave.ops->context, 0, &wave.request) != CB_SUCCESS)
		smm_invocation_platform_fail_stop();
	cause = (struct smm_invocation_entry_cause) {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION, .size = sizeof(cause),
		.loader_instance_nonce = instance.loader_instance_nonce,
		.lifecycle = instance.lifecycle, .command = SMM_APMC_AUTHVAR_SERVICE,
		.recognized = 1,
	};
	if (smm_invocation_entry_arrive(wave.runtime.evidence, &cause, &policy,
		instance.loader_instance_nonce, SMM_APMC_AUTHVAR_SERVICE, cpu, initial_apic_id,
		&wave.ticket) != CB_SUCCESS ||
	    smm_invocation_evidence_claim(wave.runtime.evidence, SMM_APMC_AUTHVAR_SERVICE,
		wave.request, wave.ops, &wave.token) != CB_SUCCESS ||
	    smm_apmc_command_select(SMM_APMC_AUTHVAR_SERVICE, &wave.selection) !=
		SMM_APMC_SELECT_ENABLED)
		smm_invocation_platform_fail_stop();
	wave.sealed_ops = *wave.ops;
	wave.consumed = wave.selection;
	if (smm_apmc_command_consume(SMM_APMC_AUTHVAR_SERVICE, SMM_APMC_OWNER_AUTHVAR_SERVICE,
		&wave.selection) != SMM_APMC_CONSUMED_SUCCESS)
		smm_invocation_platform_fail_stop();
	wave.active = true;
	if (!claim_current())
		smm_invocation_platform_fail_stop();
	if ((uint32_t)wave.request == Q35_NATIVE_BOOTSTRAP_WIRE_REQUEST) {
		if (bootstrap() != CB_SUCCESS)
			smm_invocation_platform_fail_stop();
		result = Q35_NATIVE_BOOTSTRAP_WIRE_SUCCESS;
	} else if (wave.request == SMM_APMC_AUTHVAR_SERVICE && service.phase == SERVICE_READY) {
		service.phase = SERVICE_EXECUTING;
		if (payload_mm_authvar_service_execute() != CB_SUCCESS || !claim_current())
			smm_invocation_platform_fail_stop();
		service.phase = SERVICE_READY;
		result = 0;
	} else {
		smm_invocation_platform_fail_stop();
	}
	if (!claim_current() ||
	    smm_invocation_evidence_publish_and_request_close(wave.runtime.evidence,
		&wave.token, result, wave.ops) != CB_SUCCESS ||
	    smm_invocation_entry_depart(wave.runtime.evidence, &wave.ticket) != CB_SUCCESS ||
	    !smm_invocation_entry_eos_ready(wave.runtime.evidence, &wave.ticket))
		smm_invocation_platform_fail_stop();
	q35_native_cause_end();
	memset(&wave, 0, sizeof(wave));
	return SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED;
}
