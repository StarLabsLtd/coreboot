/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_transport.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence = { .closed_generation = 9U };
static struct smm_invocation_topology topology;
static struct payload_mm_authvar_presence_transaction_slot slot;
static struct smm_invocation_save_state_ops ops;
static struct payload_mm_authvar_presence_lifecycle_close_internal_policy internal;
static unsigned int provisions;
static uint64_t invocation_wire;
static uint64_t response_wire;
static const struct payload_mm_authvar_presence_lifecycle_close_install_frame
	*observed_frame;
static const struct smm_invocation_save_state_ops *retained_ops;
static uint32_t expected_frame_address;
static unsigned int range_validations;
enum provider_state { PROVIDER_EMPTY, PROVIDER_READY, PROVIDER_ACTIVE };
static enum provider_state provider_state;

static enum smm_invocation_match match_apmc(void *context, uint32_t cpu,
	uint8_t command)
{
	(void)context;
	assert(provider_state == PROVIDER_ACTIVE);
	return cpu == 0U && command ==
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE ?
		SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	(void)context;
	assert(provider_state == PROVIDER_ACTIVE);
	if (cpu)
		return CB_ERR;
	*value = invocation_wire;
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	(void)context;
	assert(provider_state == PROVIDER_ACTIVE);
	if (cpu)
		return CB_ERR;
	response_wire = value;
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
	const uintptr_t protected_objects[] = {
		(uintptr_t)&composition, (uintptr_t)&instance, (uintptr_t)&evidence,
		(uintptr_t)&topology, (uintptr_t)&slot, (uintptr_t)&ops,
		(uintptr_t)&internal,
	};
	const size_t protected_sizes[] = {
		sizeof(composition), sizeof(instance), sizeof(evidence),
		sizeof(topology), sizeof(slot), sizeof(ops), sizeof(internal),
	};

	(void)context;
	assert(provider_state == PROVIDER_ACTIVE);
	assert(base == expected_frame_address);
	assert(size == sizeof(struct
		payload_mm_authvar_presence_lifecycle_close_install_frame));
	assert(base && size && base <= UINT32_MAX - (size - 1U));
	for (size_t index = 0; index < ARRAY_SIZE(protected_objects); index++)
		assert(base + size <= protected_objects[index] ||
			protected_objects[index] + protected_sizes[index] <= base);
	range_validations++;
	return true;
}

enum smm_invocation_try_result intel_smm_invocation_adapter_provider_provision(
	const struct smm_invocation_save_state_ops **output)
{
	if (provider_state == PROVIDER_ACTIVE)
		return SMM_INVOCATION_TRY_RETRY;
	assert(provider_state == PROVIDER_EMPTY);
	ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match_apmc,
		.read_value = read_value,
		.write_value = write_value,
	};
	*output = &ops;
	provider_state = PROVIDER_READY;
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_arm(uint64_t *generation)
{
	assert(provider_state == PROVIDER_READY);
	provider_state = PROVIDER_ACTIVE;
	*generation = 1U;
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum smm_invocation_try_result
intel_smm_invocation_adapter_provider_retire(uint64_t generation)
{
	assert(provider_state == PROVIDER_ACTIVE && generation == 1U);
	provider_state = PROVIDER_READY;
	return SMM_INVOCATION_TRY_SUCCESS;
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
	snapshot->active_cpus = 1U;
	return CB_SUCCESS;
}

struct payload_mm_authvar_presence_transaction_slot *
smm_get_payload_mm_authvar_presence_transaction_slot(void)
{
	return &slot;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_route_provision(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor,
	struct payload_mm_authvar_presence_transaction_slot *actual_slot,
	const struct smm_invocation_loader_composition *actual_composition,
	const struct smm_invocation_loader_instance *actual_instance,
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct smm_invocation_save_state_ops *actual_ops,
	const struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		*actual_internal,
	uint64_t predecessor_generation,
	payload_mm_authvar_protected_storage actual_proof, void *proof_context,
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt *receipt)
{
	assert(provider_state == PROVIDER_ACTIVE);
	assert(route && descriptor && actual_slot == &slot);
	assert(actual_composition == &composition && actual_instance == &instance);
	assert(actual_evidence == &evidence && actual_topology == &topology);
	assert(actual_ops == &ops && actual_internal == &internal);
	assert(predecessor_generation == evidence.closed_generation);
	assert(actual_proof == protected_storage && !proof_context);
	*receipt = (struct payload_mm_authvar_presence_lifecycle_close_install_receipt) {
		.descriptor = *descriptor,
		.protected_route_identity = 1U,
		.route_nonce = 2U,
		.installed = 1U,
	};
	provisions++;
	return CB_SUCCESS;
}

void platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop(void)
{
	abort();
}

uint64_t starbook_mtl_authvar_presence_lifecycle_close_install_trigger_test(
	uint32_t request, uint32_t frame_address)
{
	struct payload_mm_authvar_presence_lifecycle_close_install_frame *frame =
		(void *)(uintptr_t)frame_address;
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		dependencies = {
			.internal = &internal,
			.active_ops = retained_ops,
			.protected_storage = protected_storage,
			.communication_range_valid = communication_range_valid,
		};
	uint64_t generation;
	enum cb_err status;

	assert(request ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST);
	expected_frame_address = frame_address;
	invocation_wire = request | ((uint64_t)frame_address << 32);
	response_wire = 0;
	assert(frame != NULL);
	observed_frame = frame;
	assert(intel_smm_invocation_adapter_provider_arm(&generation) ==
		SMM_INVOCATION_TRY_SUCCESS);
	status = starbook_mtl_authvar_presence_lifecycle_close_install_receive(
		&dependencies);
	assert(intel_smm_invocation_adapter_provider_retire(generation) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(status == CB_SUCCESS);
	return response_wire;
}

int main(void)
{
	struct payload_mm_authvar_presence_lifecycle_close_install_descriptor request = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_REVISION,
		.size = sizeof(request),
		.generation = 7U,
	};
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt receipt = { 0 };

	assert(intel_smm_invocation_adapter_provider_provision(&retained_ops) ==
		SMM_INVOCATION_TRY_SUCCESS);
	assert(platform_payload_mm_authvar_presence_lifecycle_close_route_install(
		&request, &receipt) == CB_SUCCESS);
	assert(provisions == 1U);
	assert(!memcmp(&receipt.descriptor, &request, sizeof(request)));
	assert(receipt.installed == 1U && receipt.protected_route_identity == 1U &&
		receipt.route_nonce == 2U);
	assert(range_validations == 2U);
	for (size_t index = 0; index < sizeof(*observed_frame); index++)
		assert(!((const uint8_t *)observed_frame)[index]);
	return 0;
}
