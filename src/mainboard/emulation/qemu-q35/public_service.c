/* SPDX-License-Identifier: GPL-2.0-only */

#include "public_service.h"
#include <cpu/amd/amd64_save_state.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <southbridge/intel/common/pmbase.h>
#include <southbridge/intel/common/pmutil.h>
#include <string.h>

#if !ENV_SMM
#error "Q35 public service ownership is SMM-only"
#endif

enum public_service_phase { PUBLIC_EMPTY, PUBLIC_ACQUIRING, PUBLIC_HELD };

static struct {
	uint32_t phase;
	uint8_t command;
	const struct smm_invocation_runtime_view *view;
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_topology topology;
	struct smm_invocation_loader_instance instance;
	amd64_smm_state_save_area_t *node;
	amd64_smm_state_save_area_t snapshot;
	struct smm_apmc_selection_receipt selection;
	struct smm_apmc_selection_receipt consumed;
} owner;

bool q35_public_service_current(uint8_t command)
{
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_topology topology;
	struct smm_invocation_loader_instance instance;
	const struct smm_invocation_runtime_view *view;

	return __atomic_load_n(&owner.phase, __ATOMIC_ACQUIRE) == PUBLIC_HELD &&
		owner.command == command &&
		(command == SMM_APMC_AUTHVAR_SERVICE ||
		 (CONFIG(SMM_APMC_ROUTE_CAPSULE_BROKER) && command == SMM_APMC_CAPSULE_BROKER)) &&
		(read_pmbase32(SMI_EN) & ~EOS) == (GBL_SMI_EN | APMC_EN) &&
		apm_get_apmc() == command &&
		smm_invocation_runtime_view_get(&view) == CB_SUCCESS && view == owner.view &&
		smm_invocation_runtime_range_is_protected(view, &owner, sizeof(owner)) ==
			CB_SUCCESS &&
		smm_invocation_runtime_binding_get(&runtime) == CB_SUCCESS &&
		!memcmp(&runtime, &owner.runtime, sizeof(runtime)) &&
		smm_invocation_topology_read(runtime.topology, &topology) == CB_SUCCESS &&
		!memcmp(&topology, &owner.topology, sizeof(topology)) &&
		smm_invocation_loader_instance_read(runtime.instance, &instance) == CB_SUCCESS &&
		!memcmp(&instance, &owner.instance, sizeof(instance)) &&
		smm_get_save_state(0) == owner.node &&
		!memcmp(owner.node, &owner.snapshot, sizeof(owner.snapshot)) &&
		owner.consumed.revision == SMM_APMC_SELECTION_RECEIPT_REVISION &&
		owner.consumed.size == sizeof(owner.consumed) &&
		owner.consumed.identity == (uintptr_t)&owner.selection &&
		owner.consumed.generation && !owner.consumed.reserved &&
		owner.consumed.descriptor.command == command &&
		owner.consumed.descriptor.owner == (command == SMM_APMC_AUTHVAR_SERVICE ?
			SMM_APMC_OWNER_AUTHVAR_SERVICE : SMM_APMC_OWNER_CAPSULE_BROKER) &&
		owner.consumed.descriptor.role == SMM_APMC_EXCLUSIVE &&
		owner.consumed.descriptor.binding_count == 1 &&
		!owner.consumed.descriptor.observer_count &&
		owner.consumed.descriptor.enabled && owner.consumed.descriptor.reserved;
}

enum q35_public_service_result q35_public_service_begin(uint32_t cpu,
	uint32_t initial_apic_id, uint8_t command)
{
	struct smm_save_state_span span;
	uint32_t expected = PUBLIC_EMPTY, cpus;

	if ((command != SMM_APMC_AUTHVAR_SERVICE &&
	     !(CONFIG(SMM_APMC_ROUTE_CAPSULE_BROKER) && command == SMM_APMC_CAPSULE_BROKER)) ||
	    cpu || !__atomic_compare_exchange_n(&owner.phase, &expected, PUBLIC_ACQUIRING,
	    false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return Q35_PUBLIC_SERVICE_ERROR;
	owner.command = command;
	if ((read_pmbase32(SMI_EN) & ~EOS) != (GBL_SMI_EN | APMC_EN) ||
	    apm_get_apmc() != command ||
	    smm_invocation_runtime_view_get(&owner.view) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(owner.view, &owner, sizeof(owner)) !=
		CB_SUCCESS ||
	    smm_invocation_runtime_cpu_count(owner.view, &cpus) != CB_SUCCESS || cpus != 1 ||
	    smm_invocation_runtime_binding_get(&owner.runtime) != CB_SUCCESS ||
	    smm_invocation_topology_read(owner.runtime.topology, &owner.topology) != CB_SUCCESS ||
	    owner.topology.active_cpus != 1 || owner.topology.bsp_cpu ||
	    owner.topology.initial_apic_ids[0] != initial_apic_id ||
	    smm_invocation_loader_instance_read(owner.runtime.instance, &owner.instance) !=
		CB_SUCCESS || owner.instance.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    smm_invocation_runtime_save_state_span(owner.view, 0, &span) != CB_SUCCESS)
		goto failed;
	owner.node = smm_get_save_state(0);
	if (!owner.node || span.base != (uintptr_t)owner.node || span.size != sizeof(*owner.node))
		goto failed;
	owner.snapshot = *owner.node;
	if (owner.snapshot.smm_revision != 0x20064)
		goto failed;
	/* Saved arguments validate the public wire, not the instruction's origin. */
	if (owner.snapshot.rax != command ||
	    owner.snapshot.rdx != 0xb2 || owner.snapshot.rcx)
		goto refused;
	if (smm_apmc_command_select(command, &owner.selection) !=
		SMM_APMC_SELECT_ENABLED)
		goto failed;
	owner.consumed = owner.selection;
	if (smm_apmc_command_consume(command, command == SMM_APMC_AUTHVAR_SERVICE ?
	    SMM_APMC_OWNER_AUTHVAR_SERVICE : SMM_APMC_OWNER_CAPSULE_BROKER,
	    &owner.selection) != SMM_APMC_CONSUMED_SUCCESS)
		goto failed;
	__atomic_store_n(&owner.phase, PUBLIC_HELD, __ATOMIC_RELEASE);
	if (!q35_public_service_current(command))
		smm_invocation_platform_fail_stop();
	return Q35_PUBLIC_SERVICE_HELD;
refused:
	memset(&owner, 0, sizeof(owner));
	return Q35_PUBLIC_SERVICE_REFUSED;
failed:
	memset(&owner, 0, sizeof(owner));
	return Q35_PUBLIC_SERVICE_ERROR;
}

void q35_public_service_end(void)
{
	if (!q35_public_service_current(owner.command))
		smm_invocation_platform_fail_stop();
	memset(&owner, 0, sizeof(owner));
}
