/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_pre_lock_dispatch.h>
#include <intelblocks/smm_invocation_cause.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_provision.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_s3_rearm.h"
#include <boot/payload_mm_authvar_presence_s3_backing.h>
#include "authvar_presence_bootstrap_install.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define CPUS 4U

static struct payload_mm_authvar_presence_lifecycle_close_route *installed_route;
static struct payload_mm_authvar_presence_lifecycle_close_install_frame install_frame;
static struct payload_mm_authvar_presence_transaction_slot transaction_slot;
static struct smm_invocation_save_state_ops ops;
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology topology;
static pthread_barrier_t arrivals;
static atomic_uint departure_count;
static atomic_uint classify_count;
static atomic_uint arrive_count;
static atomic_uint arm_count;
static atomic_uint select_count;
static atomic_uint dispatch_count;
static atomic_uint prepare_count;
static atomic_uint retire_count;
static atomic_uint provision_count;
static atomic_uint install_count;
static atomic_uint receipt_count;
static atomic_uint dma_binding_count;
static atomic_uint cold_clear_count;
static atomic_uint cold_activate_count;
static atomic_uint epoch_prepare_count;
static atomic_uint epoch_activate_count;
static atomic_uint rearm_borrow_count;
static atomic_uint rearm_complete_count;
static bool s3_test_mode;
static bool s3_active;
static bool stale_epoch;
static unsigned int fail_stage;
static atomic_uint rearm_event;
static struct payload_mm_authvar_presence_lifecycle_close_s3_route s3_route;
static struct payload_mm_authvar_presence_s3_backing s3_backing;
static enum intel_smm_invocation_cause_result classification;
static uint8_t invocation_command;
static enum smm_apmc_select_result selection_result;
static uint64_t invocation_wire;
static struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
	install_dependencies;
static struct payload_mm_authvar_presence_lifecycle_close_internal_policy
	internal_policy;

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER)
static bool bootstrap_mode, bootstrap_ack;
static unsigned int bootstrap_failure, bootstrap_events;

enum starbook_mtl_presence_bootstrap_result
starbook_mtl_presence_bootstrap_receive(const struct smm_invocation_save_state_ops *active_ops)
{
	assert(active_ops == &ops);
	if (!bootstrap_mode)
		return STARBOOK_MTL_PRESENCE_NOT_BOOTSTRAP;
	assert(atomic_load(&arm_count) == 1 && !bootstrap_ack);
	return bootstrap_failure == 1 ? STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR :
		STARBOOK_MTL_PRESENCE_BOOTSTRAP_IMPORTED;
}

enum cb_err starbook_mtl_presence_bootstrap_route_install(void)
{
	assert(bootstrap_mode && atomic_load(&retire_count) == 1);
	assert(!bootstrap_ack && bootstrap_events++ == 0);
	return bootstrap_failure == 3 ? CB_ERR : CB_SUCCESS;
}

enum cb_err starbook_mtl_presence_bootstrap_response_stage(
	const struct smm_invocation_save_state_ops *active_ops)
{
	assert(active_ops == &ops && atomic_load(&arm_count) == 2);
	assert(atomic_load(&retire_count) == 1 && !bootstrap_ack);
	assert(bootstrap_events++ == 1);
	return bootstrap_failure == 5 ? CB_ERR : CB_SUCCESS;
}

enum cb_err starbook_mtl_presence_bootstrap_response_publish(void)
{
	assert(atomic_load(&retire_count) == 2 && !bootstrap_ack);
	assert(bootstrap_events++ == 2);
	if (bootstrap_failure == 7)
		return CB_ERR;
	bootstrap_ack = true;
	return CB_SUCCESS;
}
#endif

uint32_t starbook_mtl_authvar_presence_lifecycle_close_install_departures_test(void);
void starbook_mtl_authvar_presence_lifecycle_close_dispatch_reset_test(void);

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *candidate,
	struct smm_invocation_loader_instance *snapshot)
{
	assert(candidate == &instance);
	*snapshot = instance;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_authvar_presence_s3_binding_get(
	struct starbook_mtl_authvar_presence_s3_binding *binding)
{
	if (!s3_active)
		return CB_ERR;
	binding->route = &s3_route;
	binding->retained_ops = &ops;
	return CB_SUCCESS;
}

void starbook_mtl_dma_smm_epoch_poison(void) { }
void starbook_mtl_authvar_presence_s3_rearm_poison(void)
{
	starbook_mtl_dma_smm_epoch_poison();
}

const struct payload_mm_authvar_presence_s3_backing *
smm_get_payload_mm_authvar_presence_s3_backing(void)
{
	return s3_test_mode ? &s3_backing : NULL;
}

enum cb_err starbook_mtl_dma_smm_epoch_prepare(
	const struct smm_invocation_loader_instance *actual_instance,
	const struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct starbook_mtl_dma_smm_epoch_range ranges[2])
{
	assert(actual_instance == &instance && actual_evidence == &evidence);
	assert(actual_topology == &topology && ranges[0].base == 0x700000U);
	assert(ranges[1].base == 0x710000U);
	atomic_fetch_add(&epoch_prepare_count, 1U);
	return (stale_epoch || fail_stage == 1U) ? CB_ERR : CB_SUCCESS;
}

enum cb_err starbook_mtl_dma_smm_epoch_activate(
	const struct smm_invocation_loader_instance *actual_instance,
	const struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct smm_invocation_entry_ticket *ticket,
	const struct starbook_mtl_dma_smm_epoch_range ranges[2])
{
	assert(actual_instance == &instance && actual_evidence == &evidence);
	assert(actual_topology == &topology && ticket->cpu == topology.bsp_cpu);
	assert(ranges[0].base == 0x700000U && ranges[1].base == 0x710000U);
	assert(atomic_load(&departure_count) == CPUS);
	atomic_fetch_add(&epoch_activate_count, 1U);
	if (fail_stage == 2U)
		return CB_ERR;
	assert(atomic_fetch_add(&rearm_event, 1U) == 0U);
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_authvar_presence_s3_rearm_borrow(
	const struct smm_invocation_loader_instance *actual_instance,
	struct payload_mm_authvar_presence_s3_facts *facts)
{
	assert(actual_instance == &instance && facts);
	memset(facts, 0, sizeof(*facts));
	atomic_fetch_add(&rearm_borrow_count, 1U);
	if (fail_stage == 3U)
		return CB_ERR;
	assert(atomic_fetch_add(&rearm_event, 1U) == 1U);
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_authvar_presence_s3_rearm_complete(
	const struct smm_invocation_loader_composition *actual_composition,
	const struct smm_invocation_loader_instance *actual_instance,
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct smm_invocation_save_state_ops *actual_ops,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	assert(actual_composition == &composition && actual_instance == &instance);
	assert(actual_evidence == &evidence && actual_topology == &topology);
	assert(actual_ops == &ops && facts);
	assert(atomic_load(&epoch_activate_count) == 1U);
	if (fail_stage == 4U)
		return CB_ERR;
	assert(atomic_fetch_add(&rearm_event, 1U) == 2U);
	s3_active = true;
	atomic_fetch_add(&rearm_complete_count, 1U);
	return CB_SUCCESS;
}

enum cb_err smm_invocation_entry_arrive(
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	struct smm_invocation_loader_instance_nonce nonce, uint8_t command,
	uint32_t cpu, uint32_t apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	int status;
	assert(actual_evidence == &evidence && cause && policy);
	assert(command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(cpu < CPUS && apic_id == topology.initial_apic_ids[cpu]);
	assert(smm_invocation_loader_instance_nonce_equal(nonce,
		instance.loader_instance_nonce));
	*ticket = (struct smm_invocation_entry_ticket) {
		.generation = 10U, .loader_instance_nonce = nonce,
		.cpu = cpu, .lifecycle = instance.lifecycle,
		.max_polls = policy->max_polls, .command = command,
	};
	atomic_fetch_add(&arrive_count, 1U);
	status = pthread_barrier_wait(&arrivals);
	assert(!status || status == PTHREAD_BARRIER_SERIAL_THREAD);
	return CB_SUCCESS;
}

enum cb_err smm_invocation_entry_depart(
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_entry_ticket *ticket)
{
	assert(actual_evidence == &evidence && ticket);
	atomic_fetch_add_explicit(&departure_count, 1U, memory_order_release);
	return CB_SUCCESS;
}

bool smm_invocation_entry_eos_ready(
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_entry_ticket *ticket)
{
	assert(actual_evidence == &evidence && ticket->cpu == topology.bsp_cpu);
	return atomic_load_explicit(&departure_count, memory_order_acquire) == CPUS;
}

enum smm_apmc_dispatch_result smm_apmc_command_consume(uint8_t command,
	enum smm_apmc_owner expected_owner,
	struct smm_apmc_selection_receipt *receipt)
{
	assert(command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(expected_owner == SMM_APMC_OWNER_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(receipt);
	return SMM_APMC_CONSUMED_SUCCESS;
}

enum cb_err smm_invocation_evidence_claim(
	struct smm_invocation_evidence *actual_evidence, uint8_t command,
	uint64_t sentinel, const struct smm_invocation_save_state_ops *actual_ops,
	struct smm_invocation_token *token)
{
	assert(actual_evidence == &evidence && actual_ops == &ops && token);
	assert(command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(sentinel == STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST);
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_publish_and_request_close(
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_token *token, uint64_t value,
	const struct smm_invocation_save_state_ops *actual_ops)
{
	assert(actual_evidence == &evidence && token && actual_ops == &ops);
	assert(value == STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_CLOSING);
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_s3_route_arrive(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	int status;

	assert(route == &s3_route && cause && policy && cpu < CPUS);
	assert(apic_id == topology.initial_apic_ids[cpu]);
	*ticket = (struct smm_invocation_entry_ticket) {
		.generation = 11U,
		.loader_instance_nonce = instance.loader_instance_nonce,
		.cpu = cpu, .lifecycle = instance.lifecycle,
		.max_polls = policy->max_polls, .command = cause->command,
	};
	atomic_fetch_add(&arrive_count, 1U);
	status = pthread_barrier_wait(&arrivals);
	assert(!status || status == PTHREAD_BARRIER_SERIAL_THREAD);
	return CB_SUCCESS;
}

enum smm_apmc_dispatch_result
payload_mm_authvar_presence_lifecycle_close_s3_route_dispatch_locked(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket,
	struct smm_apmc_selection_receipt *receipt)
{
	assert(route == &s3_route && ticket->cpu == topology.bsp_cpu && receipt);
	atomic_fetch_add(&dispatch_count, 1U);
	return SMM_APMC_CONSUMED_SUCCESS;
}

void payload_mm_authvar_presence_lifecycle_close_s3_route_prepare_lock_release(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	assert(route == &s3_route && ticket->cpu == topology.bsp_cpu);
	atomic_fetch_add_explicit(&prepare_count, 1U, memory_order_release);
}

enum payload_mm_authvar_presence_lifecycle_close_s3_route_departure
payload_mm_authvar_presence_lifecycle_close_s3_route_depart(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket)
{
	unsigned int departed;

	assert(route == &s3_route && ticket);
	departed = atomic_fetch_add_explicit(&departure_count, 1U,
		memory_order_acq_rel) + 1U;
	if (ticket->cpu != topology.bsp_cpu)
		return PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_PARTICIPANT_DEPARTED;
	while (departed < CPUS)
		departed = atomic_load_explicit(&departure_count, memory_order_acquire);
	return PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_BSP_EOS_CONSUMED;
}

enum cb_err starbook_mtl_authvar_presence_s3_cold_install(
	const struct smm_invocation_loader_instance *actual_instance)
{
	assert(actual_instance == &instance);
	assert(atomic_load_explicit(&install_count, memory_order_acquire) == 1U);
	assert(atomic_load_explicit(&retire_count, memory_order_acquire) == 1U);
	assert(starbook_mtl_authvar_presence_lifecycle_close_install_departures_test() ==
		CPUS - 1U);
	if (actual_instance->lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD)
		return CB_ERR;
	atomic_fetch_add_explicit(&cold_clear_count, 1U, memory_order_relaxed);
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_authvar_presence_s3_cold_route_complete(
	struct payload_mm_authvar_presence_lifecycle_close_route *actual_route)
{
	assert(actual_route == installed_route);
	assert(atomic_load_explicit(&departure_count, memory_order_acquire) == CPUS);
	assert(atomic_load_explicit(&prepare_count, memory_order_acquire) == 1U);
	assert(atomic_load_explicit(&retire_count, memory_order_acquire) == 1U);
	assert(atomic_load_explicit(&dispatch_count, memory_order_acquire) == 1U);
	atomic_fetch_add_explicit(&cold_activate_count, 1U, memory_order_relaxed);
	return CB_SUCCESS;
}

void __noreturn
smm_invocation_platform_fail_stop(void)
{
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER)
	if (bootstrap_mode)
		assert(!bootstrap_ack);
#endif
	abort();
}

static enum smm_invocation_match match_apmc(void *context, uint32_t cpu,
	uint8_t command)
{
	(void)context;
	return cpu == 0U && command == invocation_command ?
		SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

enum cb_err starbook_mtl_dma_receipt_provision_receive(
	const struct smm_invocation_save_state_ops *expected_active_ops)
{
	assert(expected_active_ops == &ops);
	atomic_fetch_add_explicit(&receipt_count, 1U, memory_order_relaxed);
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_dma_smm_binding_get(
	struct starbook_mtl_dma_smm_binding *binding)
{
	static const struct starbook_mtl_dma_smm_receipt receipt;

	assert(binding);
	assert(atomic_load_explicit(&arrive_count, memory_order_relaxed) ==
		topology.active_cpus);
	binding->receipt = &receipt;
	atomic_fetch_add_explicit(&dma_binding_count, 1U, memory_order_relaxed);
	return CB_SUCCESS;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	(void)context;
	if (cpu)
		return CB_ERR;
	*value = invocation_wire;
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	(void)context;
	if (cpu)
		return CB_ERR;
	if (s3_test_mode && value == STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_SUCCESS) {
		if (fail_stage == 5U)
			return CB_ERR;
		assert(atomic_fetch_add(&rearm_event, 1U) == 3U);
	}
	invocation_wire = value;
	return CB_SUCCESS;
}

static bool protected_storage(void *context, const void *object, size_t size)
{
	(void)context;
	return object && size;
}

static bool communication_range_valid(void *context, uint64_t base,
	uint64_t size)
{
	(void)context;
	return base == (uintptr_t)&install_frame && size == sizeof(install_frame);
}

enum cb_err starbook_mtl_authvar_presence_lifecycle_close_install_policy(
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		**dependencies,
	const struct smm_invocation_save_state_ops *active_ops)
{
	assert(active_ops == &ops);
	*dependencies = &install_dependencies;
	return CB_SUCCESS;
}

struct payload_mm_authvar_presence_transaction_slot *
smm_get_payload_mm_authvar_presence_transaction_slot(void)
{
	return &transaction_slot;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_route_provision(
	struct payload_mm_authvar_presence_lifecycle_close_route *candidate,
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor,
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct smm_invocation_loader_composition *actual_composition,
	const struct smm_invocation_loader_instance *actual_instance,
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct smm_invocation_save_state_ops *actual_ops,
	const struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		*internal,
	uint64_t predecessor_generation,
	payload_mm_authvar_protected_storage proof, void *proof_context,
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt *receipt)
{
	assert(candidate && descriptor && slot == &transaction_slot);
	assert(actual_composition == &composition && actual_instance == &instance);
	assert(actual_evidence == &evidence && actual_topology == &topology);
	assert(actual_ops == &ops && internal == &internal_policy);
	assert(predecessor_generation == evidence.closed_generation);
	assert(proof == protected_storage && !proof_context);
	candidate->protected_storage = proof;
	candidate->protected_storage_context = proof_context;
	installed_route = candidate;
	*receipt = (struct payload_mm_authvar_presence_lifecycle_close_install_receipt) {
		.descriptor = *descriptor,
		.protected_route_identity = 1U,
		.route_nonce = 2U,
		.installed = 1U,
	};
	atomic_fetch_add_explicit(&install_count, 1U, memory_order_relaxed);
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_binding_get(
	struct smm_invocation_runtime_binding *binding)
{
	*binding = (struct smm_invocation_runtime_binding) {
		.composition = &composition,
		.instance = &instance,
		.evidence = &evidence,
		.topology = &topology,
	};
	return CB_SUCCESS;
}

enum cb_err smm_invocation_topology_read(
	const struct smm_invocation_topology *candidate,
	struct smm_invocation_topology *snapshot)
{
	assert(candidate == &topology);
	*snapshot = topology;
	return CB_SUCCESS;
}

enum intel_smm_invocation_cause_result intel_smm_invocation_private_apmc_cause(
	const struct smm_invocation_loader_composition *actual_composition,
	const struct smm_invocation_topology *actual_topology,
	const struct smm_invocation_loader_instance *actual_instance,
	const struct smm_invocation_evidence *actual_evidence,
	const uint32_t *runtime_cpus, uint8_t expected_command,
	struct smm_invocation_entry_cause *cause)
{
	assert(actual_composition == &composition && actual_topology == &topology);
	assert(actual_instance == &instance && actual_evidence == &evidence);
	assert(*runtime_cpus == topology.active_cpus);
	atomic_fetch_add_explicit(&classify_count, 1U, memory_order_relaxed);
	if (expected_command != invocation_command)
		return INTEL_SMM_INVOCATION_CAUSE_NOT_PRIVATE;
	if (classification == INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID)
		*cause = (struct smm_invocation_entry_cause) {
			.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
			.size = sizeof(*cause),
			.loader_instance_nonce = instance.loader_instance_nonce,
			.lifecycle = instance.lifecycle,
			.command = expected_command,
			.recognized = 1U,
		};
	return classification;
}

enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_provision(
	const struct smm_invocation_save_state_ops **output)
{
	*output = &ops;
	atomic_fetch_add_explicit(&provision_count, 1U, memory_order_relaxed);
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_arm(uint64_t *generation)
{
	const unsigned int prior = atomic_fetch_add_explicit(&arm_count, 1U, memory_order_relaxed);
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER)
	if (bootstrap_mode && prior == 1) {
		assert(bootstrap_events == 1 && !bootstrap_ack);
		if (bootstrap_failure == 4)
			return SMM_INVOCATION_TRY_ERROR;
	} else
#endif
		assert(prior == 0U);
	*generation = 0x1234U;
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_retire(uint64_t generation)
{
	assert(generation == 0x1234U);
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER)
	if (bootstrap_mode) {
		assert(!bootstrap_ack);
		if ((!atomic_load(&retire_count) && bootstrap_failure == 2) ||
		    (atomic_load(&retire_count) == 1 && bootstrap_failure == 6))
			return SMM_INVOCATION_TRY_ERROR;
	}
#endif
	if (atomic_load_explicit(&dispatch_count, memory_order_acquire))
		assert(atomic_load_explicit(&departure_count,
			memory_order_acquire) == topology.active_cpus);
	if (s3_test_mode && atomic_load(&rearm_complete_count))
		assert(atomic_fetch_add(&rearm_event, 1U) == 4U);
	atomic_fetch_add_explicit(&retire_count, 1U, memory_order_relaxed);
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_route_arrive(
	struct payload_mm_authvar_presence_lifecycle_close_route *actual_route,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	int barrier_status;

	assert(actual_route == installed_route && cause->command ==
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	assert(policy->max_polls == SMM_INVOCATION_ENTRY_MAX_POLLS);
	assert(cpu < CPUS && initial_apic_id == topology.initial_apic_ids[cpu]);
	*ticket = (struct smm_invocation_entry_ticket) {
		.generation = 11U,
		.cpu = cpu,
		.max_polls = policy->max_polls,
		.command = cause->command,
	};
	atomic_fetch_add_explicit(&arrive_count, 1U, memory_order_relaxed);
	barrier_status = pthread_barrier_wait(&arrivals);
	assert(!barrier_status || barrier_status == PTHREAD_BARRIER_SERIAL_THREAD);
	return CB_SUCCESS;
}

enum smm_apmc_select_result smm_apmc_command_select(uint8_t command,
	struct smm_apmc_selection_receipt *selection)
{
	assert(command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	atomic_fetch_add_explicit(&select_count, 1U, memory_order_relaxed);
	memset(selection, 0, sizeof(*selection));
	return selection_result;
}

enum smm_apmc_dispatch_result
payload_mm_authvar_presence_lifecycle_close_route_dispatch_locked(
	struct payload_mm_authvar_presence_lifecycle_close_route *actual_route,
	const struct smm_invocation_entry_ticket *ticket,
	struct smm_apmc_selection_receipt *selection)
{
	assert(actual_route == installed_route && ticket->cpu == topology.bsp_cpu);
	assert(selection_result == SMM_APMC_SELECT_ENABLED);
	(void)selection;
	atomic_fetch_add_explicit(&dispatch_count, 1U, memory_order_relaxed);
	return SMM_APMC_CONSUMED_SUCCESS;
}

void payload_mm_authvar_presence_lifecycle_close_route_prepare_lock_release(
	struct payload_mm_authvar_presence_lifecycle_close_route *actual_route,
	const struct smm_invocation_entry_ticket *ticket)
{
	assert(actual_route == installed_route && ticket->cpu == topology.bsp_cpu);
	atomic_fetch_add_explicit(&prepare_count, 1U, memory_order_release);
}

enum payload_mm_authvar_presence_lifecycle_close_route_departure
payload_mm_authvar_presence_lifecycle_close_route_depart(
	struct payload_mm_authvar_presence_lifecycle_close_route *actual_route,
	const struct smm_invocation_entry_ticket *ticket)
{
	unsigned int departed;

	assert(actual_route == installed_route);
	assert(atomic_load_explicit(&prepare_count, memory_order_acquire) == 1U);
	departed = atomic_fetch_add_explicit(&departure_count, 1U,
		memory_order_acq_rel) + 1U;
	if (ticket->cpu != topology.bsp_cpu)
		return PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_PARTICIPANT_DEPARTED;
	while (departed < CPUS) {
		departed = atomic_load_explicit(&departure_count, memory_order_acquire);
		__asm__ __volatile__("pause");
	}
	return PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_BSP_EOS_CONSUMED;
}

struct worker {
	uint32_t cpu;
	enum smm_pre_lock_dispatch_result result;
};

static void *dispatch_thread(void *argument)
{
	struct worker *worker = argument;

	worker->result = smm_pre_lock_dispatch(worker->cpu,
		topology.initial_apic_ids[worker->cpu]);
	return NULL;
}

static void reset(enum intel_smm_invocation_cause_result classify)
{
	starbook_mtl_authvar_presence_lifecycle_close_dispatch_reset_test();
	installed_route = NULL;
	memset(&install_frame, 0, sizeof(install_frame));
	memset(&transaction_slot, 0, sizeof(transaction_slot));
	memset(&instance, 0, sizeof(instance));
	memset(&topology, 0, sizeof(topology));
	instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	instance.loader_instance_nonce.low = 1U;
	topology.active_cpus = CPUS;
	topology.bsp_cpu = 0U;
	for (uint32_t cpu = 0; cpu < CPUS; cpu++)
		topology.initial_apic_ids[cpu] = cpu * 2U;
	classification = classify;
	invocation_command = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE;
	selection_result = SMM_APMC_SELECT_ENABLED;
	evidence.closed_generation = 9U;
	ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match_apmc,
		.read_value = read_value,
		.write_value = write_value,
	};
	install_dependencies =
		(struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies) {
			.internal = &internal_policy,
			.active_ops = &ops,
			.protected_storage = protected_storage,
			.communication_range_valid = communication_range_valid,
		};
	atomic_store(&departure_count, 0U);
	atomic_store(&classify_count, 0U);
	atomic_store(&arrive_count, 0U);
	atomic_store(&arm_count, 0U);
	atomic_store(&select_count, 0U);
	atomic_store(&dispatch_count, 0U);
	atomic_store(&prepare_count, 0U);
	atomic_store(&retire_count, 0U);
	atomic_store(&provision_count, 0U);
	atomic_store(&install_count, 0U);
	atomic_store(&receipt_count, 0U);
	atomic_store(&dma_binding_count, 0U);
	atomic_store(&cold_clear_count, 0U);
	atomic_store(&cold_activate_count, 0U);
	atomic_store(&epoch_prepare_count, 0U);
	atomic_store(&epoch_activate_count, 0U);
	atomic_store(&rearm_borrow_count, 0U);
	atomic_store(&rearm_complete_count, 0U);
	s3_test_mode = false;
	s3_active = false;
	stale_epoch = false;
	fail_stage = 0U;
	atomic_store(&rearm_event, 0U);
	s3_backing = (struct payload_mm_authvar_presence_s3_backing) {
		.presence.communication_base = 0x700000U,
		.lifecycle_close.communication_base = 0x710000U,
	};
	invocation_wire = 0;
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER)
	bootstrap_mode = false;
	bootstrap_ack = false;
	bootstrap_failure = 0;
	bootstrap_events = 0;
#endif
}

static void clear_round_counts(void)
{
	atomic_store(&departure_count, 0U);
	atomic_store(&classify_count, 0U);
	atomic_store(&arrive_count, 0U);
	atomic_store(&arm_count, 0U);
	atomic_store(&select_count, 0U);
	atomic_store(&dispatch_count, 0U);
	atomic_store(&prepare_count, 0U);
	atomic_store(&retire_count, 0U);
	atomic_store(&provision_count, 0U);
	atomic_store(&receipt_count, 0U);
	atomic_store(&dma_binding_count, 0U);
}

static void run_all_cpus(struct worker *workers, pthread_t *threads)
{
	for (uint32_t cpu = 0; cpu < CPUS; cpu++) {
		workers[cpu].cpu = cpu;
		assert(!pthread_create(&threads[cpu], NULL, dispatch_thread,
			&workers[cpu]));
	}
	for (uint32_t cpu = 0; cpu < CPUS; cpu++)
		assert(!pthread_join(threads[cpu], NULL));
}

static void run_aps_first(struct worker *workers, pthread_t *threads)
{
	for (uint32_t cpu = 1; cpu < CPUS; cpu++) {
		workers[cpu].cpu = cpu;
		assert(!pthread_create(&threads[cpu], NULL, dispatch_thread,
			&workers[cpu]));
	}
	usleep(1000);
	assert(!atomic_load_explicit(&arrive_count, memory_order_acquire));
	workers[0].cpu = 0;
	assert(!pthread_create(&threads[0], NULL, dispatch_thread, &workers[0]));
	for (uint32_t cpu = 0; cpu < CPUS; cpu++)
		assert(!pthread_join(threads[cpu], NULL));
}

static void expect_fail_stop(void)
{
	pid_t child = fork();
	int status;

	assert(child >= 0);
	if (!child) {
		(void)smm_pre_lock_dispatch(0U, topology.initial_apic_ids[0]);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

static void expect_all_cpu_fail_stop(void)
{
	pthread_t threads[CPUS];
	struct worker workers[CPUS];
	pid_t child = fork();
	int status;

	assert(child >= 0);
	if (!child) {
		run_all_cpus(workers, threads);
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

int main(void)
{
	pthread_t threads[CPUS];
	struct worker workers[CPUS];

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER)
	reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	invocation_command = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE;
	bootstrap_mode = true;
	run_aps_first(workers, threads);
	assert(bootstrap_ack && bootstrap_events == 3);
	assert(atomic_load(&arm_count) == 2 && atomic_load(&retire_count) == 2);
	assert(workers[0].result == SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED);
	for (uint32_t cpu = 1; cpu < CPUS; cpu++)
		assert(workers[cpu].result == SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED);
	for (unsigned int failure = 1; failure <= 7; failure++) {
		reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
		invocation_command = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE;
		bootstrap_mode = true;
		bootstrap_failure = failure;
		expect_all_cpu_fail_stop();
	}
#endif

	reset(INTEL_SMM_INVOCATION_CAUSE_NOT_PRIVATE);
	invocation_command = 0U;
	assert(smm_pre_lock_dispatch(0U, topology.initial_apic_ids[0]) ==
		SMM_PRE_LOCK_DISPATCH_NOT_HANDLED);
	assert(atomic_load(&classify_count) == 2U && !atomic_load(&arm_count));

	reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	invocation_command = SMM_APMC_STARBOOK_MTL_DMA_RECEIPT;
	run_all_cpus(workers, threads);
	assert(workers[0].result == SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED);
	for (uint32_t cpu = 1; cpu < CPUS; cpu++)
		assert(workers[cpu].result ==
			SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED);
	assert(atomic_load(&classify_count) == CPUS);
	assert(atomic_load(&receipt_count) == 1U);
	assert(atomic_load(&arm_count) == 1U);
	assert(atomic_load(&retire_count) == 1U);
	assert(!atomic_load(&install_count));
	assert(!atomic_load(&arrive_count));

	reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_INVALID);
	invocation_command = SMM_APMC_STARBOOK_MTL_DMA_RECEIPT;
	expect_fail_stop();

	reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	invocation_wire = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL;
	topology.active_cpus = 1U;
	expect_fail_stop();

	reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	install_frame =
		(struct payload_mm_authvar_presence_lifecycle_close_install_frame) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REVISION,
			.size = sizeof(install_frame),
			.state =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REQUEST,
			.request = {
				.revision =
					PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_REVISION,
				.size = sizeof(install_frame.request),
				.generation = 7U,
			},
		};
	invocation_wire = (uint64_t)(uintptr_t)&install_frame << 32 |
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST;
	run_all_cpus(workers, threads);
	assert(workers[0].result == SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED);
	for (uint32_t cpu = 1; cpu < CPUS; cpu++)
		assert(workers[cpu].result ==
			SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED);
	assert(atomic_load(&classify_count) == CPUS * 2U);
	assert(atomic_load(&install_count) == 1U);
	assert(atomic_load(&provision_count) == 1U);
	assert(!atomic_load(&dma_binding_count));
	assert(atomic_load(&arm_count) == 1U);
	assert(atomic_load(&retire_count) == 1U);
	assert(invocation_wire ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_SUCCESS);
	assert(install_frame.state ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_RECEIPT);
	assert(!atomic_load(&arrive_count));
	assert(!atomic_load(&select_count));
	assert(atomic_load(&cold_clear_count) == 1U);
	assert(!atomic_load(&cold_activate_count));

	clear_round_counts();
	invocation_wire = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL;
	assert(!pthread_barrier_init(&arrivals, NULL, CPUS));
	run_all_cpus(workers, threads);
	assert(!pthread_barrier_destroy(&arrivals));
	assert(workers[0].result == SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED);
	for (uint32_t cpu = 1; cpu < CPUS; cpu++)
		assert(workers[cpu].result ==
			SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED);
	assert(atomic_load(&classify_count) == CPUS * 2U);
	assert(atomic_load(&arrive_count) == CPUS);
	assert(atomic_load(&departure_count) == CPUS);
	assert(atomic_load(&arm_count) == 1U);
	assert(atomic_load(&select_count) == 1U);
	assert(atomic_load(&dispatch_count) == 1U);
	assert(atomic_load(&prepare_count) == 1U);
	assert(atomic_load(&retire_count) == 1U);
	assert(atomic_load(&install_count) == 1U);
	assert(atomic_load(&provision_count) == 1U);
	assert(atomic_load(&dma_binding_count) == 1U);
	assert(atomic_load(&cold_clear_count) == 1U);
	assert(atomic_load(&cold_activate_count) == 1U);

	/* AP-first S3 private rearm seals the DMA epoch before any arrival. */
	reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	s3_test_mode = true;
	invocation_wire = STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST;
	assert(!pthread_barrier_init(&arrivals, NULL, CPUS));
	run_aps_first(workers, threads);
	assert(!pthread_barrier_destroy(&arrivals));
	assert(workers[0].result == SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED);
	for (uint32_t cpu = 1; cpu < CPUS; cpu++)
		assert(workers[cpu].result ==
			SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED);
	assert(atomic_load(&epoch_prepare_count) == 1U);
	assert(atomic_load(&arrive_count) == CPUS);
	assert(atomic_load(&departure_count) == CPUS);
	assert(atomic_load(&epoch_activate_count) == 1U);
	assert(atomic_load(&rearm_borrow_count) == 1U);
	assert(atomic_load(&rearm_complete_count) == 1U);
	assert(atomic_load(&retire_count) == 1U);
	assert(invocation_wire == STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_SUCCESS);
	assert(atomic_load(&rearm_event) == 5U);
	assert(!atomic_load(&cold_clear_count));
	assert(!atomic_load(&cold_activate_count));

	/* The committed owner accepts a later public CLOSED_REPROOF route only. */
	clear_round_counts();
	atomic_store(&epoch_prepare_count, 0U);
	atomic_store(&epoch_activate_count, 0U);
	atomic_store(&rearm_borrow_count, 0U);
	atomic_store(&rearm_complete_count, 0U);
	invocation_wire = 0x12345678000000feULL;
	assert(!pthread_barrier_init(&arrivals, NULL, CPUS));
	run_all_cpus(workers, threads);
	assert(!pthread_barrier_destroy(&arrivals));
	assert(atomic_load(&arrive_count) == CPUS);
	assert(atomic_load(&departure_count) == CPUS);
	assert(atomic_load(&dispatch_count) == 1U);
	assert(atomic_load(&prepare_count) == 1U);
	assert(atomic_load(&retire_count) == 1U);
	assert(!atomic_load(&epoch_prepare_count));
	assert(!atomic_load(&epoch_activate_count));
	assert(!atomic_load(&rearm_borrow_count));
	assert(!atomic_load(&rearm_complete_count));

	/* Every private transaction stage is terminal on failure. */
	for (unsigned int stage = 1U; stage <= 5U; stage++) {
		reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
		instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
		s3_test_mode = true;
		fail_stage = stage;
		invocation_wire = STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST;
		assert(!pthread_barrier_init(&arrivals, NULL, CPUS));
		expect_all_cpu_fail_stop();
		assert(!pthread_barrier_destroy(&arrivals));
		assert(!atomic_load(&cold_clear_count));
		assert(!atomic_load(&cold_activate_count));
	}

	/* A fresh reload rejects stale proof first, then accepts its fresh epoch. */
	reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	instance.loader_instance_nonce.low = 2U;
	s3_test_mode = true;
	stale_epoch = true;
	invocation_wire = STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST;
	assert(!pthread_barrier_init(&arrivals, NULL, CPUS));
	expect_all_cpu_fail_stop();
	assert(!pthread_barrier_destroy(&arrivals));
	reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	instance.loader_instance_nonce.low = 2U;
	s3_test_mode = true;
	invocation_wire = STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST;
	assert(!pthread_barrier_init(&arrivals, NULL, CPUS));
	run_aps_first(workers, threads);
	assert(!pthread_barrier_destroy(&arrivals));
	assert(atomic_load(&rearm_event) == 5U);
	assert(invocation_wire == STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_SUCCESS);

	clear_round_counts();
	selection_result = SMM_APMC_SELECT_CONSUMED_REJECT;
	/* One CPU is sufficient: selection failure is terminal after admission. */
	topology.active_cpus = 1U;
	assert(!pthread_barrier_init(&arrivals, NULL, 1U));
	expect_fail_stop();
	assert(!pthread_barrier_destroy(&arrivals));

	/* An S3 loader instance must never fall back to the cold-clear path. */
	reset(INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	install_frame =
		(struct payload_mm_authvar_presence_lifecycle_close_install_frame) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REVISION,
			.size = sizeof(install_frame),
			.state =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REQUEST,
			.request = {
				.revision =
					PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_REVISION,
				.size = sizeof(install_frame.request),
				.generation = 7U,
			},
		};
	invocation_wire = (uint64_t)(uintptr_t)&install_frame << 32 |
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST;
	expect_all_cpu_fail_stop();
	assert(!atomic_load(&cold_clear_count));
	assert(!atomic_load(&cold_activate_count));
	return 0;
}
