/* SPDX-License-Identifier: GPL-2.0-only */

/* Keep the existing hostile-wave stubs for unrelated lifecycle paths. The
 * coupled lane links the actual classifier, adapter, receiver and ledger;
 * provider ownership and receipt/policy cryptography remain test boundaries. */
#define main boundary_wave_main
#define smm_invocation_platform_fail_stop boundary_fail_stop
#define intel_smm_invocation_adapter_provider_provision boundary_provision
#define intel_smm_invocation_adapter_provider_arm boundary_arm
#define intel_smm_invocation_adapter_provider_retire boundary_retire
#define starbook_mtl_presence_bootstrap_receive boundary_receive
#define starbook_mtl_presence_bootstrap_route_install boundary_install
#define starbook_mtl_presence_bootstrap_response_stage boundary_stage
#define starbook_mtl_presence_bootstrap_response_publish boundary_publish
#define starbook_mtl_dma_smm_binding_get boundary_dma_binding
#include "starbook_mtl_authvar_service_bootstrap_wave_test.c"
#undef main
#undef smm_invocation_platform_fail_stop
#undef intel_smm_invocation_adapter_provider_provision
#undef intel_smm_invocation_adapter_provider_arm
#undef intel_smm_invocation_adapter_provider_retire
#undef starbook_mtl_presence_bootstrap_receive
#undef starbook_mtl_presence_bootstrap_route_install
#undef starbook_mtl_presence_bootstrap_response_stage
#undef starbook_mtl_presence_bootstrap_response_publish
#undef starbook_mtl_dma_smm_binding_get

#include <cpu/intel/em64t101_save_state.h>
#include <cpu/intel/smm_invocation_adapter.h>
#include <cpu/x86/smm.h>
#include "authvar_presence_authority_policy.h"
#include "authvar_presence_route_composition.h"

static em64t101_smm_state_save_area_t native_states[CPUS];
static struct intel_smm_invocation_adapter adapter;
static struct starbook_mtl_presence_bootstrap_frame frame;
static struct smm_dma_receipt_memory memory;
static struct payload_mm_authvar_presence_bootstrap slot;
static struct payload_mm_authvar_presence_route_authority_policy authority_policy;
static uintptr_t save_state_tops[CPUS];
static unsigned int coupled_fault;

void __noreturn smm_invocation_platform_fail_stop(void)
{
	assert(atomic_load(&injected));
	if (coupled_fault == 3) {
		assert(!atomic_load(&arrivals) && !atomic_load(&installs));
		assert(!atomic_load(&arm_count) && !atomic_load(&retire_count));
	} else if (coupled_fault == 4) {
		assert(atomic_load(&arrivals) == CPUS && !atomic_load(&installs));
		assert(atomic_load(&arm_count) == 1 && !atomic_load(&retire_count));
	} else {
		assert(coupled_fault == 1);
		assert(atomic_load(&arrivals) == CPUS && atomic_load(&installs) == 1);
		assert(atomic_load(&arm_count) == 2 && atomic_load(&retire_count) == 1);
	}
	assert(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
	assert(native_states[0].rax == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST);
	_Exit(0);
}

void intel_smm_invocation_adapter_test_hook(uint32_t point) { (void)point; }
size_t intel_smm_invocation_adapter_test_revision_size(uint32_t revision, size_t size)
{
	(void)revision;
	return size;
}

enum smm_invocation_try_result intel_smm_invocation_adapter_provider_provision(
	const struct smm_invocation_save_state_ops **output)
{
	*output = &ops;
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum smm_invocation_try_result intel_smm_invocation_adapter_provider_arm(uint64_t *generation)
{
	assert(!atomic_exchange(&lease, true));
	*generation = atomic_fetch_add(&arm_count, 1) + 1U;
	assert(intel_smm_invocation_adapter_init(&adapter, CPUS, save_state_tops,
		sizeof(native_states[0]), 0x30101U) == CB_SUCCESS);
	adapter.invocation_nonce = *generation;
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	if (*generation == 2 && coupled_fault == 1) {
		atomic_store(&injected, true);
		native_states[0].rcx += 8U;
	}
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum smm_invocation_try_result intel_smm_invocation_adapter_provider_retire(uint64_t generation)
{
	assert(generation == atomic_load(&arm_count));
	assert(atomic_exchange(&lease, false));
	atomic_fetch_add(&retire_count, 1);
	return SMM_INVOCATION_TRY_SUCCESS;
}

bool smm_get_dma_receipt_memory(const struct smm_dma_receipt_memory **output)
{
	*output = &memory;
	return true;
}

enum cb_err starbook_mtl_dma_smm_binding_get(struct starbook_mtl_dma_smm_binding *binding)
{
	static struct starbook_mtl_dma_smm_receipt receipt;

	binding->receipt = &receipt;
	return CB_SUCCESS;
}

struct payload_mm_authvar_presence_bootstrap *smm_get_payload_mm_authvar_presence_bootstrap(void)
{
	return &slot;
}

enum cb_err payload_mm_authvar_presence_bootstrap_receipts_import(
	struct payload_mm_authvar_presence_bootstrap_receipts *receipts)
{
	assert(!memcmp(receipts, &frame.receipts, sizeof(*receipts)));
	assert(atomic_load(&arrivals) == CPUS && atomic_load(&lease));
	slot.state = PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_bootstrap_binding_get(
	const struct payload_mm_authvar_presence_transaction_binding **binding)
{
	*binding = &slot.binding;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_authvar_presence_authority_policy_get(
	const struct payload_mm_authvar_presence_route_authority_policy **output)
{
	*output = &authority_policy;
	return CB_SUCCESS;
}

enum smm_invocation_try_result starbook_mtl_authvar_presence_route_composition_provision(
	const struct smm_invocation_loader_composition *actual_composition,
	const struct smm_invocation_loader_instance *actual_instance,
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct payload_mm_authvar_presence_route_authority_policy *actual_policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt,
	payload_mm_authvar_protected_storage protected_storage, void *context)
{
	assert(actual_composition == &composition && actual_instance == &instance &&
		actual_evidence == &evidence && actual_topology == &topology &&
		actual_policy == &authority_policy && binding == &slot.binding &&
		verifier == &slot.page_verifier && receipt == &slot.page_receipt);
	assert(protected_storage(context, &slot, sizeof(slot)));
	assert(!atomic_load(&lease) && atomic_load(&retire_count) == 1);
	assert(platform_payload_mm_authvar_service_bootstrap_admitted());
	atomic_fetch_add(&installs, 1);
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum cb_err starbook_mtl_authvar_service_bootstrap_install(void)
{
	assert(platform_payload_mm_authvar_service_bootstrap_admitted());
	assert(!atomic_load(&lease) && atomic_load(&retire_count) == 1);
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_service_finalize(void)
{
	assert(atomic_load(&installs) == 1 && !atomic_load(&lease));
	assert(platform_payload_mm_authvar_service_bootstrap_admitted());
	if (!platform_payload_mm_authvar_service_finalize_admitted())
		return CB_ERR;
	return platform_payload_mm_authvar_service_finalize_admitted() ? CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_authvar_service_descriptor_copy(struct lb_authvar_service_endpoint *output)
{
	assert(output && atomic_load(&installs) == 1 && atomic_load(&lease));
	assert(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
	/* This fixture composes the actual tuple path, not the provider authority. */
	*output = (struct lb_authvar_service_endpoint) { .tag = LB_TAG_AUTHVAR_SERVICE_ENDPOINT };
	return CB_SUCCESS;
}

int main(int argc, char **argv)
{
	pthread_t threads[CPUS];
	struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION, .size = sizeof(seed),
		.active_cpus = CPUS, .bsp_cpu = 0,
		.loader_instance_nonce = { .low = 17, .high = 19 },
		.participant_apic_ids = { 8, 10, 12 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_loader_instance_seed identity = {
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION, .size = sizeof(identity),
		.lifecycle = seed.lifecycle, .loader_instance_nonce = seed.loader_instance_nonce,
	};
	struct smm_invocation_topology_builder builder;
	uint32_t installed;

	assert(argc == 2);
	coupled_fault = (unsigned int)atol(argv[1]);
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	composition.owner_attempt = 1;
	composition.evidence_identity = (uintptr_t)&evidence;
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_loader_instance_publish(&instance, &identity) == CB_SUCCESS);
	assert(smm_invocation_topology_begin(&builder, &topology, CPUS, 8) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < CPUS; cpu++) {
		assert(smm_invocation_topology_append(&builder, &installed,
			seed.participant_apic_ids[cpu]) == CB_SUCCESS);
		native_states[cpu].smm_revision = 0x30101U;
		save_state_tops[cpu] = (uintptr_t)&native_states[cpu] + sizeof(native_states[cpu]);
	}
	assert(smm_invocation_topology_publish(&builder, seed.participant_apic_ids,
		CPUS, CPUS) == CB_SUCCESS);
	frame = (struct starbook_mtl_presence_bootstrap_frame) {
		.revision = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION,
		.size = sizeof(frame), .state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST,
	};
	memory.frame.base = (uintptr_t)&frame;
	memory.frame.size = sizeof(frame);
	if (coupled_fault == 4) {
		frame.reserved = 1;
		atomic_store(&injected, true);
	}
	slot.binding.maximum_cpus = CPUS;
	native_states[0].rax = STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST;
	native_states[0].rcx = (uintptr_t)&frame;
	native_states[0].io_misc_info = 0x00b20003U;
	assert(native_states[0].rcx && native_states[0].rcx <= UINT32_MAX);
	assert(intel_smm_invocation_adapter_init(&adapter, CPUS, save_state_tops,
		sizeof(native_states[0]), 0x30101U) == CB_SUCCESS);
	assert(intel_smm_invocation_adapter_ops(&adapter, &ops) == CB_SUCCESS);
	if (coupled_fault == 3) {
		assert(!pthread_create(&threads[1], NULL, cpu_entry, (void *)(uintptr_t)1));
		while (!(atomic_load(&classified_cpus) & 2U)) __asm__ __volatile__("pause");
		atomic_store(&injected, true);
		assert(!pthread_create(&threads[2], NULL, cpu_entry, (void *)(uintptr_t)1));
		assert(!pthread_join(threads[1], NULL));
		abort();
	}
	if (coupled_fault == 2) {
		for (uint32_t cpu = 1; cpu < CPUS; cpu++)
			assert(!pthread_create(&threads[cpu], NULL, cpu_entry, (void *)(uintptr_t)cpu));
		while ((atomic_load(&classified_cpus) & 6U) != 6U)
			__asm__ __volatile__("pause");
		assert(!atomic_load(&arrivals) && !atomic_load(&installs));
	}
	assert(!pthread_create(&threads[0], NULL, cpu_entry, NULL));
	if (coupled_fault != 2) {
		while (!(atomic_load(&classified_cpus) & 1U)) __asm__ __volatile__("pause");
		assert(!atomic_load(&arrivals) && !atomic_load(&installs));
		for (uint32_t cpu = 1; cpu < CPUS; cpu++)
			assert(!pthread_create(&threads[cpu], NULL, cpu_entry, (void *)(uintptr_t)cpu));
	}
	for (uint32_t cpu = 0; cpu < CPUS; cpu++) assert(!pthread_join(threads[cpu], NULL));
	assert(atomic_load(&installs) == 1 && atomic_load(&arm_count) == 2 &&
		atomic_load(&retire_count) == 2 && !atomic_load(&lease));
	assert(native_states[0].rax == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS &&
		!native_states[0].rcx && frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED);
	assert(smm_invocation_evidence_phase(&evidence) == SMM_INVOCATION_READY);
	assert(!platform_payload_mm_authvar_service_bootstrap_admitted());
	return 0;
}
