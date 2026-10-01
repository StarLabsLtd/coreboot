/* SPDX-License-Identifier: GPL-2.0-only */

/* Actual board factory, arm and invocation ledger. Loader mapping, protected
 * placement and the downstream route/policy factory are explicit test models. */
#include <assert.h>
#include <boot/payload_mm_authvar_presence_arm.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_evidence.h>
#include <stdlib.h>
#include <string.h>

#include "authvar_presence_route_composition.h"

extern int dprintf(int descriptor, const char *format, ...);
#undef assert
#define assert(condition) do { \
	if (!(condition)) { \
		dprintf(2, "arm factory line %d: %s\n", __LINE__, #condition); \
		abort(); \
	} \
} while (0)

static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology topology;
static struct payload_mm_authvar_presence_route_authority_policy authority;
static struct payload_mm_authvar_presence_transaction_binding binding;
static struct bootmem_reservation_receipt_authority verifier;
static struct bootmem_reservation_receipt receipt;
static uint64_t saved_value;
static unsigned int factory_calls;
static unsigned int admission_calls;
static unsigned int fault;

void smm_invocation_evidence_test_hook(uint32_t point)
{
	(void)point;
}

void __noreturn smm_invocation_platform_fail_stop(void)
{
	abort();
}

const struct smm_invocation_evidence *smm_invocation_loader_composition_evidence(
	const struct smm_invocation_loader_composition *candidate,
	const struct smm_invocation_evidence *candidate_evidence)
{
	return candidate == &composition && candidate_evidence == &evidence ? &evidence : NULL;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *candidate,
	struct smm_invocation_loader_instance *output)
{
	if (candidate != &instance || !output)
		return CB_ERR;
	*output = instance;
	return CB_SUCCESS;
}

static bool protected_storage(void *context, const void *base, size_t size)
{
	(void)context;
	return base && size;
}

bool platform_payload_mm_authvar_service_bootstrap_admitted(void)
{
	admission_calls++;
	if (fault == 2U || (fault == 3U && admission_calls == 2U))
		return false;
	if (fault == 4U)
		evidence.loader_instance_nonce.low++;
	if (fault == 9U)
		platform_payload_mm_authvar_presence_arm()->sealed_protected_storage = NULL;
	return true;
}

static enum smm_invocation_match match_apmc(void *context, uint32_t cpu, uint8_t command)
{
	(void)context;
	(void)command;
	return cpu ? SMM_INVOCATION_NOT_MATCHED : SMM_INVOCATION_MATCHED;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	(void)context;
	if (cpu || !value)
		return CB_ERR;
	*value = saved_value;
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	(void)context;
	if (cpu)
		return CB_ERR;
	saved_value = value;
	return CB_SUCCESS;
}

enum smm_invocation_try_result intel_smm_invocation_adapter_route_provision(
	struct payload_mm_authvar_presence_route_session *session,
	struct payload_mm_authvar_presence_arm *arm,
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct smm_invocation_loader_composition *actual_composition,
	const struct smm_invocation_loader_instance *actual_instance,
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct payload_mm_authvar_presence_route_authority_policy *actual_authority,
	const struct payload_mm_authvar_presence_transaction_binding *actual_binding,
	struct bootmem_reservation_receipt_authority *actual_verifier,
	struct bootmem_reservation_receipt *actual_receipt,
	payload_mm_authvar_protected_storage actual_protection, void *context)
{
	assert(session && slot && arm == platform_payload_mm_authvar_presence_arm());
	assert(arm->state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_LOADER_READY);
	assert(arm->owner_attempt == 1U && arm->evidence == &evidence);
	assert(actual_composition == &composition && actual_instance == &instance);
	assert(actual_evidence == &evidence && actual_topology == &topology);
	assert(actual_authority == &authority && actual_binding == &binding);
	assert(actual_verifier == &verifier && actual_receipt == &receipt);
	assert(actual_protection == protected_storage && !context);
	factory_calls++;
	return SMM_INVOCATION_TRY_SUCCESS;
}

int main(int argc, char **argv)
{
	const struct smm_invocation_loader_seed seed = {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(seed),
		.active_cpus = 2,
		.bsp_cpu = 0,
		.loader_instance_nonce = { .low = 17, .high = 19 },
		.participant_apic_ids = { 8, 10 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
	const struct smm_invocation_save_state_ops ops = {
		.match_apmc_write = match_apmc,
		.read_value = read_value,
		.write_value = write_value,
	};
	struct smm_invocation_token token;
	struct payload_mm_authvar_presence_arm *arm;
	uint64_t generation;
	enum smm_invocation_try_result result;
	uint8_t command = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE;
	uint64_t sentinel = 0x00100000424f4ffeULL;

	assert(argc == 2);
	assert(argv[1][0] >= '0' && argv[1][0] <= '9' && !argv[1][1]);
	fault = (unsigned int)(argv[1][0] - '0');
	assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
	instance.lifecycle = seed.lifecycle;
	instance.loader_instance_nonce = seed.loader_instance_nonce;
	arm = platform_payload_mm_authvar_presence_arm();
	assert(arm && arm->state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_EMPTY);
	if (fault == 5U) {
		command = SMM_APMC_AUTHVAR_PRESENCE;
		sentinel = UINT64_MAX;
	}
	if (fault != 1U) {
		saved_value = command;
		for (uint32_t cpu = 0; cpu < seed.active_cpus; cpu++) {
			struct smm_invocation_admission_token admission = { 0 };

			assert(smm_invocation_evidence_arrive_try(&evidence, cpu,
				seed.participant_apic_ids[cpu], &generation, &admission) ==
				SMM_INVOCATION_TRY_SUCCESS);
		}
		assert(smm_invocation_evidence_claim(&evidence, command, sentinel,
			&ops, &token) == CB_SUCCESS);
	}
	if (fault == 6U) {
		assert(payload_mm_authvar_presence_arm_provision(arm, &composition,
			&instance, &evidence, protected_storage, NULL) == CB_ERR);
		assert(arm->state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
		assert(!factory_calls && !admission_calls);
		return 0;
	}
	if (fault == 7U)
		evidence.token.rendezvous_digest[0] ^= 1;
	if (fault == 8U) {
		evidence.loader_lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
		instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	}
	result = starbook_mtl_authvar_presence_route_composition_provision(&composition,
		&instance, &evidence, &topology, &authority, &binding, &verifier,
		&receipt, protected_storage, NULL);
	if (CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED) ? !fault : fault == 1U) {
		assert(result == SMM_INVOCATION_TRY_SUCCESS && factory_calls == 1U);
		assert(admission_calls == (CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED) ? 2U : 0U));
		assert(starbook_mtl_authvar_presence_route_composition_provision(&composition,
			&instance, &evidence, &topology, &authority, &binding, &verifier,
			&receipt, protected_storage, NULL) == SMM_INVOCATION_TRY_ERROR);
		assert(factory_calls == 1U);
	} else {
		assert(result == SMM_INVOCATION_TRY_ERROR && !factory_calls);
		assert(arm->state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
	}
	return 0;
}
