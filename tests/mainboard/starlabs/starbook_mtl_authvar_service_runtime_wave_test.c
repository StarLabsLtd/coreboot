/* SPDX-License-Identifier: GPL-2.0-only */

#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <cpu/intel/em64t101_save_state.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/msr.h>
#include <intelblocks/smm_save_state_ops.h>

/*
 * Reuse the actual media/executor/CMS/provider fixture. Its install receipt and
 * runtime geometry remain host models; calls now traverse the actual fc wave.
 */
#define TEST_REAL_RUNTIME_WAVE
#define payload_mm_authvar_service_execute runtime_test_execute
#define platform_payload_mm_authvar_service_runtime_admitted boundary_runtime_admitted
#define smm_invocation_runtime_view_get boundary_runtime_view_get
#define smm_invocation_runtime_range_is_protected boundary_runtime_range_is_protected
#define smm_invocation_platform_fail_stop boundary_fail_stop
#include "../../lib/payload_mm_authvar_service_provider_stack_test.c"
#undef payload_mm_authvar_service_execute
#undef platform_payload_mm_authvar_service_runtime_admitted
#undef smm_invocation_runtime_view_get
#undef smm_invocation_runtime_range_is_protected
#undef smm_invocation_platform_fail_stop

#include "authvar_service_runtime_dispatch.h"

#define RUNTIME_CPUS 3U
static struct smm_invocation_evidence runtime_evidence;
static struct smm_invocation_loader_instance runtime_instance;
static struct smm_invocation_topology runtime_topology;
static struct smm_invocation_loader_composition runtime_composition;
static em64t101_smm_state_save_area_t native_states[RUNTIME_CPUS];
static _Thread_local uint32_t current_cpu;
static _Thread_local bool inside_runtime;
static atomic_uint raw_classified;
static atomic_uint protection_reads[RUNTIME_CPUS];
static atomic_uint arrivals;
static atomic_uint provider_arms;
static atomic_uint provider_retires;
static atomic_uint completed_closes;
static atomic_bool fault_injected;
static unsigned int runtime_fault;
static _Thread_local unsigned int virtual_delays;
static atomic_bool virtual_expiry_observed;

unsigned long tsc_freq_mhz(void)
{
	if (runtime_fault == 18) {
		atomic_store(&fault_injected, true);
		return 0;
	}
	return 2400;
}

void udelay(unsigned int microseconds)
{
	assert(microseconds == 100U && current_cpu != 0);
	if (runtime_fault == 17) {
		virtual_delays++;
		if (virtual_delays == 600000U)
			atomic_store(&virtual_expiry_observed, true);
		sched_yield();
		return; /* Virtual calibrated time: exercise the complete policy deadline. */
	}
	const struct timespec delay = { .tv_nsec = (long)microseconds * 1000L };

	assert(nanosleep(&delay, NULL) == 0);
}
static uint32_t runtime_initiator;

enum cb_err payload_mm_authvar_service_execute(void);
bool platform_payload_mm_authvar_service_runtime_admitted(void);
void starbook_mtl_authvar_service_runtime_generation_drift_test(void);
void starbook_mtl_authvar_service_runtime_claim_drift_test(uint32_t field);

void __noreturn smm_invocation_platform_fail_stop(void)
{
	dprintf(2, "runtime fail cpu%u fault%u phase%u arms%u retires%u arrivals%u\n",
		current_cpu, runtime_fault, smm_invocation_evidence_phase(&runtime_evidence),
		atomic_load(&provider_arms), atomic_load(&provider_retires), atomic_load(&arrivals));
	if (deny_after_program && program_count > program_baseline)
		test_real_fail_stop();
	assert(runtime_fault && atomic_load(&fault_injected));
	if (runtime_fault == 17)
		assert(atomic_load(&virtual_expiry_observed) && virtual_delays <= 600000U);
	if (runtime_fault == 18)
		assert(!atomic_load(&provider_arms) && !atomic_load(&arrivals));
	assert(shared_mailbox->completion == UINT32_MAX && shared_mailbox->status == UINT64_MAX);
	assert(!program_count);
	_exit(78);
}

void smm_invocation_evidence_test_hook(uint32_t point)
{
	/* Loader provisioning reuses these hook numbers outside the actual fc wave. */
	if (!inside_runtime)
		return;
	if (point == 45) {
		assert(smm_invocation_evidence_phase(&runtime_evidence) == SMM_INVOCATION_PUBLISHING);
		assert(native_states[runtime_initiator].rax == SMM_APMC_AUTHVAR_SERVICE);
		assert(!native_states[runtime_initiator].rcx);
	} else if (point == 48) {
		assert(smm_invocation_evidence_phase(&runtime_evidence) == SMM_INVOCATION_PUBLISHING);
		assert(!native_states[runtime_initiator].rax);
		assert(!native_states[runtime_initiator].rcx);
		atomic_fetch_add(&completed_closes, 1);
	}
}

void smm_invocation_loader_instance_test_hook(uint32_t point)
{
	(void)point;
}

void smm_invocation_topology_test_hook(uint32_t point)
{
	(void)point;
}
void smm_invocation_entry_test_hook(uint32_t point, uint32_t cpu)
{
	(void)cpu;
	if (point == 2)
		atomic_fetch_add(&arrivals, 1);
}
void intel_smm_invocation_adapter_test_hook(uint32_t point)
{
	(void)point;
}
size_t intel_smm_invocation_adapter_test_revision_size(uint32_t revision, size_t size)
{
	(void)revision;
	return size;
}
void intel_smm_invocation_adapter_provider_test_hook(uint32_t point)
{
	if (point == 2)
		atomic_fetch_add(&provider_arms, 1);
	else if (point == 3)
		atomic_fetch_add(&provider_retires, 1);
}
const uint32_t *intel_smm_invocation_adapter_provider_test_revision_table(
	const struct smm_save_state_ops *ops)
{
	return ops->revision_table;
}
void intel_smm_invocation_cause_test_hook(uint32_t point)
{
	if (point == 5)
		atomic_fetch_or(&raw_classified, 1U << current_cpu);
}

enum cb_err smm_invocation_runtime_binding_get(struct smm_invocation_runtime_binding *binding)
{
	*binding = (struct smm_invocation_runtime_binding) {
		.composition = &runtime_composition, .instance = &runtime_instance,
		.evidence = &runtime_evidence, .topology = &runtime_topology,
	};
	return CB_SUCCESS;
}
enum cb_err smm_invocation_runtime_view_get(const struct smm_invocation_runtime_view **view)
{
	return boundary_runtime_view_get(view);
}
enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	return boundary_runtime_range_is_protected(view, base, size);
}
enum cb_err smm_invocation_runtime_geometry_is_contained(
	const struct smm_invocation_runtime_view *view, uintptr_t base, size_t size)
{
	assert(view == &runtime_view && base == 0x80000000U && size == (32U << 20));
	if (runtime_fault == 8) {
		atomic_store(&fault_injected, true);
		return CB_ERR;
	}
	return CB_SUCCESS;
}
enum cb_err smm_invocation_runtime_cpu_count(
	const struct smm_invocation_runtime_view *view, uint32_t *count)
{
	assert(view == &runtime_view);
	*count = RUNTIME_CPUS;
	return CB_SUCCESS;
}
enum cb_err smm_invocation_runtime_save_state_span(
	const struct smm_invocation_runtime_view *view, uint32_t cpu,
	struct smm_save_state_span *span)
{
	assert(view == &runtime_view && cpu < RUNTIME_CPUS);
	*span = (struct smm_save_state_span) {
		.base = (uintptr_t)&native_states[cpu], .size = sizeof(native_states[cpu]),
	};
	return CB_SUCCESS;
}
static const uint32_t revisions[] = { 0x30101U, SMM_REV_INVALID };
const struct smm_save_state_ops em64t101_smm_ops = { .revision_table = revisions };
const struct smm_save_state_ops em64t100_smm_ops = { .revision_table = revisions };
const struct smm_save_state_ops *get_smm_save_state_ops(void)
{
	return &em64t101_smm_ops;
}

msr_t rdmsr(unsigned int index)
{
	assert(current_cpu < RUNTIME_CPUS);
	if (index == 0xfeU) {
		const unsigned int read_count = atomic_fetch_add(&protection_reads[current_cpu], 1U);

		if (!current_cpu && runtime_fault == 17 && read_count >= 2U) {
			atomic_store(&fault_injected, true);
			for (;;)
				sched_yield(); /* No private transaction or durable mutation began. */
		}

		if (!current_cpu && !atomic_load(&fault_injected) &&
		    ((runtime_fault == 6 && read_count >= 2U) ||
		     (runtime_fault == 7 && deny_after_program && program_count > program_baseline))) {
			atomic_store(&fault_injected, true);
			starbook_mtl_authvar_service_runtime_generation_drift_test();
		}
		if (!current_cpu && ((runtime_fault >= 10 && runtime_fault <= 12) ||
		    runtime_fault == 15 || runtime_fault == 16) &&
		    read_count >= 2U && !atomic_exchange(&fault_injected, true))
			starbook_mtl_authvar_service_runtime_claim_drift_test(runtime_fault);
		return (msr_t) { .lo = (1U << 11) | (1U << 14) };
	}
	assert(index == 0x1f2U || index == 0x1f3U);
	uint32_t value = index == 0x1f2U ? 0x80000006U : 0xfe000c00U;
	if (index == 0x1f3U &&
	    ((runtime_fault == 5 && current_cpu == 2U) ||
	     (!current_cpu && runtime_fault != 7 && deny_after_program &&
	      program_count > program_baseline))) {
		atomic_store(&fault_injected, true);
		value &= ~0x400U;
	}
	return (msr_t) { .lo = value };
}
uint32_t pci_read_config32(unsigned int device, unsigned int index)
{
	assert(!device && (index == 0xb8U || index == 0xb4U));
	return index == 0xb8U ? 0x80000001U : 0x82000001U;
}
uint32_t inl(uint16_t port)
{
	assert(port == 0x1834U);
	return 1U << 5;
}

uint8_t inb(uint16_t port)
{
	assert(port == APM_CNT);
	return SMM_APMC_AUTHVAR_SERVICE;
}

static void *runtime_cpu_entry(void *argument)
{
	const uint32_t cpu = (uint32_t)(uintptr_t)argument;

	current_cpu = cpu;
	inside_runtime = true;
	const enum smm_pre_lock_dispatch_result result =
		starbook_mtl_authvar_service_runtime_dispatch(cpu,
			runtime_topology.initial_apic_ids[cpu] ^
			(runtime_fault == 14 && cpu == 2 ? 1U : 0U));
	assert(result == (cpu ? SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED :
		SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED));
	return NULL;
}

enum cb_err runtime_test_execute(void)
{
	pthread_t threads[RUNTIME_CPUS];

	if (inside_runtime)
		return payload_mm_authvar_service_execute();
	assert(!platform_payload_mm_authvar_service_runtime_admitted());
	atomic_store(&raw_classified, 0);
	atomic_store(&arrivals, 0);
	atomic_store(&provider_arms, 0);
	atomic_store(&provider_retires, 0);
	atomic_store(&completed_closes, 0);
	memset(native_states, 0, sizeof(native_states));
	for (uint32_t cpu = 0; cpu < RUNTIME_CPUS; cpu++) {
		native_states[cpu].smm_revision = 0x30101U;
		atomic_store(&protection_reads[cpu], 0);
	}
	native_states[runtime_initiator].rax = SMM_APMC_AUTHVAR_SERVICE;
	native_states[runtime_initiator].io_misc_info = ((uint32_t)APM_CNT << 16) | 3U;
	if (runtime_fault == 9) {
		shared_mailbox->operation = UINT32_MAX;
		atomic_store(&fault_injected, true);
	}
	if (runtime_fault == 13 || runtime_fault == 14) {
		atomic_store(&fault_injected, true);
		if (runtime_fault == 13)
			runtime_topology.active_cpus = SMM_INVOCATION_EVIDENCE_MAX_CPUS + 1;
	}
	if (runtime_fault >= 1 && runtime_fault <= 4) {
		atomic_store(&fault_injected, true);
		if (runtime_fault == 1) {
			const uint32_t duplicate = runtime_initiator ? 0 : 1;

			native_states[duplicate].rax = SMM_APMC_AUTHVAR_SERVICE;
			native_states[duplicate].io_misc_info = ((uint32_t)APM_CNT << 16) | 3U;
		} else if (runtime_fault == 2) {
			native_states[runtime_initiator].rax |= 1ULL << 32;
		} else if (runtime_fault == 3) {
			native_states[runtime_initiator].rcx = 1;
		} else {
			native_states[runtime_initiator].io_misc_info ^= 1U << 16;
		}
	}
	/* AP-first classification cannot create arrivals or erase its published facts. */
	for (uint32_t cpu = 1; cpu < RUNTIME_CPUS; cpu++)
		assert(!pthread_create(&threads[cpu], NULL, runtime_cpu_entry, (void *)(uintptr_t)cpu));
	while ((atomic_load(&raw_classified) & 6U) != 6U)
		__asm__ __volatile__("pause");
	assert(!atomic_load(&arrivals));
	assert(!pthread_create(&threads[0], NULL, runtime_cpu_entry, NULL));
	for (uint32_t cpu = 0; cpu < RUNTIME_CPUS; cpu++)
		assert(!pthread_join(threads[cpu], NULL));
	assert(atomic_load(&arrivals) == RUNTIME_CPUS);
	assert(atomic_load(&provider_arms) == 1 && atomic_load(&provider_retires) == 1);
	assert(atomic_load(&completed_closes) == 1);
	assert(!native_states[runtime_initiator].rax && !native_states[runtime_initiator].rcx);
	assert(smm_invocation_evidence_phase(&runtime_evidence) == SMM_INVOCATION_READY);
	assert(!platform_payload_mm_authvar_service_runtime_admitted());
	return CB_SUCCESS;
}

int main(int argc, char **argv)
{
	struct smm_invocation_loader_seed invocation_seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION, .size = sizeof(invocation_seed),
		.active_cpus = RUNTIME_CPUS, .bsp_cpu = 0,
		.loader_instance_nonce = { .low = 17, .high = 19 },
		.participant_apic_ids = { 8, 10, 12 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	struct smm_invocation_loader_instance_seed identity = {
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION, .size = sizeof(identity),
		.lifecycle = invocation_seed.lifecycle,
		.loader_instance_nonce = invocation_seed.loader_instance_nonce,
	};
	struct smm_invocation_topology_builder builder;
	uint32_t installed;
	const char *initiator = getenv("RUNTIME_INITIATOR");
	const char *fault = getenv("RUNTIME_FAULT");

	runtime_initiator = initiator ? (uint32_t)atoi(initiator) : 0;
	runtime_fault = fault ? (unsigned int)atoi(fault) : 0;
	assert(runtime_initiator < RUNTIME_CPUS);
	runtime_composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	runtime_composition.owner_attempt = 1;
	runtime_composition.evidence_identity = (uintptr_t)&runtime_evidence;
	assert(smm_invocation_evidence_provision(&runtime_evidence, &invocation_seed) == CB_SUCCESS);
	assert(smm_invocation_loader_instance_publish(&runtime_instance, &identity) == CB_SUCCESS);
	assert(smm_invocation_topology_begin(&builder, &runtime_topology, RUNTIME_CPUS, 8) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < RUNTIME_CPUS; cpu++)
		assert(smm_invocation_topology_append(&builder, &installed,
			invocation_seed.participant_apic_ids[cpu]) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, invocation_seed.participant_apic_ids,
		RUNTIME_CPUS, RUNTIME_CPUS) == CB_SUCCESS);
	return runtime_provider_fixture_main(argc, argv);
}
