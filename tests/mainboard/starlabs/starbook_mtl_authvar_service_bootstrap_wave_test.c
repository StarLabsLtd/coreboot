/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_smm_bootstrap.h>
#include <boot/payload_mm_authvar_service_receiver.h>
#include <cpu/intel/smm_invocation_adapter_provider.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <cpu/x86/smm_pre_lock_dispatch.h>
#include <cpu/x86/msr.h>
#include <intelblocks/smm_invocation_cause.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "authvar_presence_bootstrap_install.h"
#include "authvar_presence_lifecycle_close_install.h"
#include "dma_smm_receipt_provision.h"
#include "authvar_protected_region.h"

#undef assert
extern int dprintf(int descriptor, const char *format, ...);
#define assert(condition) do { if (!(condition)) { dprintf(2, "assertion line %d: %s\n", __LINE__, #condition); abort(); } } while (0)
#define CPUS 3U
extern void _Exit(int status) __noreturn;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_topology topology;
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_save_state_ops ops;
static atomic_uint arrivals, stages, installs, ack, arm_count, retire_count;
static atomic_bool lease;
static atomic_bool injected;
static atomic_uint classified_cpus;
static _Thread_local uint32_t current_cpu;
static atomic_uint protection_reads[CPUS];
#define BOOT_WIRE (((uint64_t)0x801000U << 32) | STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST)
static uint64_t wire = BOOT_WIRE;
static unsigned int fault;
static unsigned int view_identity;
static unsigned int foreign_view_identity;
static atomic_bool view_drift;
static bool owner_drift;
void starbook_mtl_authvar_service_bootstrap_owner_drift_test(bool generation);

void smm_invocation_evidence_test_hook(uint32_t point) { (void)point; }
void smm_invocation_loader_instance_test_hook(uint32_t point)
{
	if (point == 3 && owner_drift) {
		owner_drift = false;
		atomic_store(&injected, true);
		starbook_mtl_authvar_service_bootstrap_owner_drift_test(fault == 15);
	}
}
void smm_invocation_topology_test_hook(uint32_t point) { (void)point; }
void smm_invocation_entry_test_hook(uint32_t point, uint32_t cpu)
{
	(void)cpu;
	if (point == 2) atomic_fetch_add(&arrivals, 1);
	if (point == 7 && fault == 19) {
		atomic_store(&injected, true);
		evidence.closed_loader_instance_nonce.high++;
	}
}

void __noreturn smm_invocation_platform_fail_stop(void)
{
	assert(fault && fault != 17 && atomic_load(&injected));
	assert(fault == 19 ? atomic_load(&ack) == 1 : !atomic_load(&ack));
	if (fault >= 21) {
		assert(atomic_load(&installs) ==
			(fault == 23 || fault == 24 || fault >= 26 ? 1U : 0U));
		_Exit(0);
	}
	if (fault == 14 || fault == 12) {
		assert(!atomic_load(&arrivals) && !atomic_load(&installs));
		assert(atomic_load(&arm_count) == 1 && !atomic_load(&retire_count));
	} else {
		assert(atomic_load(&arrivals) == CPUS);
		const unsigned int expected_installs =
			fault == 1 || fault == 2 || fault == 11 || fault == 12 ? 0 : 1;
		assert(atomic_load(&installs) == expected_installs);
		const unsigned int expected_retires = fault == 1 || fault == 2 ? 0 :
			fault == 19 ? 2 : 1;
		assert(atomic_load(&retire_count) == expected_retires);
		const unsigned int expected_stages =
			fault == 6 || fault == 7 || fault == 8 || fault == 18 || fault == 19 ? 1 : 0;
		assert(atomic_load(&stages) == expected_stages);
	}
	_Exit(0);
}

static enum smm_invocation_match match(void *context, uint32_t cpu, uint8_t command)
{
	(void)context;
	assert(atomic_load(&lease));
	return !cpu && command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE ?
		SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}
static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	(void)context;
	assert(atomic_load(&lease) && !cpu);
	*value = wire;
	return CB_SUCCESS;
}
static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	(void)context;
	assert(atomic_load(&lease) && !cpu);
	if ((uint32_t)value == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST) {
		assert(value == BOOT_WIRE);
		assert(!atomic_load(&stages) && !atomic_load(&installs));
		wire = value;
		return CB_SUCCESS;
	}
	assert(atomic_load(&stages) == 1);
	assert(value == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS);
	if (fault == 7) { atomic_store(&injected, true); return CB_ERR; }
	if (fault == 18) {
		atomic_store(&injected, true);
		struct smm_invocation_token nested;
		assert(smm_invocation_evidence_claim(&evidence,
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			BOOT_WIRE, &ops, &nested) == CB_ERR);
	}
	wire = value;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_binding_get(struct smm_invocation_runtime_binding *binding)
{
	*binding = (struct smm_invocation_runtime_binding) {
		.composition = &composition, .instance = &instance,
		.evidence = &evidence, .topology = &topology,
	};
	return CB_SUCCESS;
}
enum cb_err smm_invocation_runtime_view_get(const struct smm_invocation_runtime_view **view)
{
	*view = atomic_load(&view_drift) ? (const void *)&foreign_view_identity :
		(const void *)&view_identity;
	return CB_SUCCESS;
}
enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	assert(base && size);
	if (view != (const void *)&view_identity)
		return CB_ERR;
	if (fault == 12) { atomic_store(&injected, true); return CB_ERR; }
	return CB_SUCCESS;
}
enum cb_err smm_invocation_runtime_geometry_is_contained(
	const struct smm_invocation_runtime_view *view, uintptr_t base, size_t size)
{
	assert(view == (const void *)&view_identity && base == 0x80000000U &&
		(size == (32U << 20) || size == (64U << 20)));
	if (fault == 25 && current_cpu == 2U) {
		atomic_store(&injected, true);
		return CB_ERR;
	}
	return CB_SUCCESS;
}
msr_t rdmsr(unsigned int index)
{
	assert(current_cpu < CPUS);
	if (index == 0xfeU) {
		atomic_fetch_add(&protection_reads[current_cpu], 1U);
		return (msr_t){ .lo = (1U << 11) | (1U << 14) };
	}
	assert(index == 0x1f2U || index == 0x1f3U);
	const bool changed = (fault == 29 && !current_cpu &&
		atomic_load(&protection_reads[0]) > 4U) ||
		(fault == 22 && current_cpu == 2U) ||
		(fault == 24 && current_cpu == 2U &&
		 atomic_load(&protection_reads[current_cpu]) > 2U);
	if (changed)
		atomic_store(&injected, true);
	uint32_t value = index == 0x1f2U ? 0x80000006U :
		(changed ? 0xfc000c00U : 0xfe000c00U);
	if (index == 0x1f3U && current_cpu == 1U &&
	    (fault == 21 || (fault == 23 && atomic_load(&protection_reads[1]) > 2U))) {
		atomic_store(&injected, true);
		value &= ~0x400U;
	}
	return (msr_t){ .lo = value };
}
uint32_t pci_read_config32(unsigned int device, unsigned int index)
{
	assert(!device && (index == 0xb8U || index == 0xb4U));
	const bool changed = (fault == 29 && !current_cpu &&
		atomic_load(&protection_reads[0]) > 4U) ||
		(fault == 22 && current_cpu == 2U) ||
		(fault == 24 && current_cpu == 2U &&
		 atomic_load(&protection_reads[current_cpu]) > 2U);
	return index == 0xb8U ? 0x80000001U : (changed ? 0x84000001U : 0x82000001U);
}
uint32_t inl(uint16_t port)
{
	assert(port == 0x1834U);
	return 1U << 5;
}
uint8_t inb(uint16_t port)
{
	assert(port == 0xb2U);
	return SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE;
}
void intel_smm_invocation_cause_test_hook(uint32_t point)
{
	if (point == 5) atomic_fetch_or(&classified_cpus, 1U << current_cpu);
}
enum smm_invocation_try_result intel_smm_invocation_adapter_provider_provision(
	const struct smm_invocation_save_state_ops **output)
{
	if (fault == 11 && atomic_load(&retire_count)) {
		atomic_store(&injected, true);
		return SMM_INVOCATION_TRY_ERROR;
	}
	*output = &ops;
	return SMM_INVOCATION_TRY_SUCCESS;
}
enum smm_invocation_try_result intel_smm_invocation_adapter_provider_arm(uint64_t *generation)
{
	assert(!atomic_exchange(&lease, true));
	*generation = atomic_fetch_add(&arm_count, 1) + 1;
	if (fault == 5 && *generation == 2) {
		atomic_store(&injected, true);
		return SMM_INVOCATION_TRY_ERROR;
	}
	return SMM_INVOCATION_TRY_SUCCESS;
}
enum smm_invocation_try_result intel_smm_invocation_adapter_provider_retire(uint64_t generation)
{
	assert(generation == atomic_load(&arm_count));
	if ((fault == 2 && generation == 1) || (fault == 8 && generation == 2)) {
		atomic_store(&injected, true);
		return SMM_INVOCATION_TRY_ERROR;
	}
	assert(atomic_exchange(&lease, false));
	atomic_fetch_add(&retire_count, 1);
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum starbook_mtl_presence_bootstrap_result starbook_mtl_presence_bootstrap_receive(
	const struct smm_invocation_save_state_ops *active_ops)
{
	struct smm_invocation_token token;
	assert(active_ops == &ops && atomic_load(&arrivals) == CPUS);
	assert(smm_invocation_evidence_claimed_snapshot(&evidence,
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		BOOT_WIRE, &token) == CB_SUCCESS);
	assert(wire == BOOT_WIRE);
	if (fault == 1) {
		atomic_store(&injected, true);
		return STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR;
	}
	return STARBOOK_MTL_PRESENCE_BOOTSTRAP_IMPORTED;
}
enum cb_err starbook_mtl_presence_bootstrap_route_install(void)
{
	assert(!atomic_load(&lease) && atomic_load(&retire_count) == 1);
	assert(platform_payload_mm_authvar_service_bootstrap_admitted());
	atomic_fetch_add(&installs, 1);
	if (fault == 3) { atomic_store(&injected, true); return CB_ERR; }
	if (fault == 4) { atomic_store(&injected, true); evidence.token.smi_generation++; }
	if (fault == 9) { atomic_store(&injected, true); instance.loader_instance_nonce.high++; }
	if (fault == 10) { atomic_store(&injected, true); topology.initial_apic_ids[1]++; }
	if (fault == 13) {
		atomic_store(&injected, true);
		assert(smm_invocation_evidence_shutdown(&evidence) == CB_SUCCESS);
	}
	if (fault == 15 || fault == 16) owner_drift = true;
	if (fault == 17) {
		atomic_store(&injected, true);
		struct smm_invocation_token nested;
		assert(smm_invocation_evidence_claim(&evidence,
			SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
			BOOT_WIRE, &ops, &nested) == CB_ERR);
	}
	if (!platform_payload_mm_authvar_service_finalize_admitted())
		return CB_ERR;
	if (fault >= 26 && fault <= 28) {
		atomic_store(&injected, true);
		if (fault == 26)
			starbook_mtl_authvar_service_bootstrap_owner_drift_test(true);
		if (fault == 27)
			evidence.token.smi_generation++;
		if (fault == 28)
			atomic_store(&view_drift, true);
	}
	return platform_payload_mm_authvar_service_finalize_admitted() ? CB_SUCCESS : CB_ERR;
}
enum cb_err starbook_mtl_presence_bootstrap_response_stage(
	const struct smm_invocation_save_state_ops *active_ops)
{
	assert(active_ops == &ops && atomic_load(&lease) && atomic_load(&installs) == 1);
	assert(wire == BOOT_WIRE && !atomic_load(&ack));
	atomic_fetch_add(&stages, 1);
	if (fault == 6) { atomic_store(&injected, true); return CB_ERR; }
	return CB_SUCCESS;
}
enum cb_err starbook_mtl_presence_bootstrap_response_publish(void)
{
	assert(!atomic_load(&lease) && atomic_load(&retire_count) == 2);
	assert(wire == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS);
	atomic_fetch_add(&ack, 1);
	return CB_SUCCESS;
}

/* Normal lifecycle/DMA paths must not run in this genuine BOOT-wave fixture. */
enum cb_err starbook_mtl_dma_smm_binding_get(struct starbook_mtl_dma_smm_binding *binding)
{
	(void)binding; abort();
}
enum cb_err starbook_mtl_dma_receipt_provision_receive(const struct smm_invocation_save_state_ops *active_ops)
{
	(void)active_ops; abort();
}
enum cb_err starbook_mtl_authvar_presence_lifecycle_close_installed_route(
	struct starbook_mtl_authvar_presence_lifecycle_close_installed_route *route)
{
	(void)route; abort();
}
enum cb_err starbook_mtl_authvar_presence_lifecycle_close_install_receive(
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies *dependencies,
	const struct smm_invocation_save_state_ops *active_ops)
{
	(void)dependencies; (void)active_ops; abort();
}
enum cb_err payload_mm_authvar_presence_lifecycle_close_route_arrive(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy, uint32_t cpu, uint32_t apic,
	struct smm_invocation_entry_ticket *ticket)
{
	(void)route; (void)cause; (void)policy; (void)cpu; (void)apic; (void)ticket; abort();
}
enum smm_apmc_select_result smm_apmc_command_select(uint8_t command,
	struct smm_apmc_selection_receipt *receipt)
{
	(void)command; (void)receipt; abort();
}
enum smm_apmc_dispatch_result payload_mm_authvar_presence_lifecycle_close_route_dispatch_locked(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket, struct smm_apmc_selection_receipt *receipt)
{
	(void)route; (void)ticket; (void)receipt; abort();
}
void payload_mm_authvar_presence_lifecycle_close_route_prepare_lock_release(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	(void)route; (void)ticket; abort();
}
enum payload_mm_authvar_presence_lifecycle_close_route_departure
payload_mm_authvar_presence_lifecycle_close_route_depart(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	(void)route; (void)ticket; abort();
}

static void *cpu_entry(void *argument)
{
	const uint32_t cpu = (uint32_t)(uintptr_t)argument;
	current_cpu = cpu;
	enum smm_pre_lock_dispatch_result result =
		smm_pre_lock_dispatch(cpu, topology.initial_apic_ids[cpu]);
	assert(result == (cpu ? SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED :
		SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED));
	return NULL;
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
	fault = (unsigned int)atol(argv[1]);
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	composition.owner_attempt = 1;
	composition.evidence_identity = (uintptr_t)&evidence;
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	assert(smm_invocation_loader_instance_publish(&instance, &identity) == CB_SUCCESS);
	assert(smm_invocation_topology_begin(&builder, &topology, CPUS, 8) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < CPUS; cpu++)
		assert(smm_invocation_topology_append(&builder, &installed,
			seed.participant_apic_ids[cpu]) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, seed.participant_apic_ids,
		CPUS, CPUS) == CB_SUCCESS);
	ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match, .read_value = read_value, .write_value = write_value,
	};
	if (fault == 14) atomic_store(&injected, true);
	if (fault == 20) {
		for (uint32_t cpu = 1; cpu < CPUS; cpu++)
			assert(!pthread_create(&threads[cpu], NULL, cpu_entry, (void *)(uintptr_t)cpu));
		/* APs classified the real private request but have no fabricated arrival. */
		while ((atomic_load(&classified_cpus) & 6U) != 6U)
			__asm__ __volatile__("pause");
		assert(!atomic_load(&arrivals) && !atomic_load(&installs) && !atomic_load(&ack));
	}
	assert(!pthread_create(&threads[0], NULL, cpu_entry, NULL));
	if (fault != 20) {
		while (!(atomic_load(&classified_cpus) & 1U)) __asm__ __volatile__("pause");
		/* BSP classified the cause; missing APs cannot fabricate arrivals. */
		assert(!atomic_load(&arrivals) && !atomic_load(&installs) && !atomic_load(&ack));
		if (fault == 14) {
			assert(!pthread_join(threads[0], NULL));
			abort();
		}
		for (uint32_t cpu = 1; cpu < CPUS; cpu++)
			assert(!pthread_create(&threads[cpu], NULL, cpu_entry,
				(void *)(uintptr_t)cpu));
	}
	for (uint32_t cpu = 0; cpu < CPUS; cpu++) assert(!pthread_join(threads[cpu], NULL));
	assert((!fault || fault == 17 || fault == 20) &&
		atomic_load(&installs) == 1 && atomic_load(&ack) == 1);
	assert(fault == 17 ? atomic_load(&injected) : !atomic_load(&injected));
	assert(smm_invocation_evidence_phase(&evidence) == SMM_INVOCATION_READY);
	assert(!platform_payload_mm_authvar_service_bootstrap_admitted());
	for (uint32_t cpu = 0; cpu < CPUS; cpu++)
		assert(atomic_load(&protection_reads[cpu]) == (cpu ? 4U : 6U));
	return 0;
}
