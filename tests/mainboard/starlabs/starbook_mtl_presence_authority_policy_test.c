/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "../../../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_authority_policy.c"

static struct payload_mm_authvar_presence_transaction_binding bootstrap_binding;
static struct smm_invocation_evidence evidence;
static bool bootstrap_ready, storage_ready, dma_ready, cpu_ready;
static unsigned int installs, restrictions;
static enum cb_err install_result;
static uint32_t claimed_cpus;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		abort();
}

enum cb_err smm_invocation_runtime_view_get(const struct smm_invocation_runtime_view **view)
{
	*view = (const void *)&evidence;
	return storage_ready ? CB_SUCCESS : CB_ERR;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	(void)view;
	return storage_ready && base && size && (uintptr_t)base <= UINTPTR_MAX - size ?
		CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_authvar_presence_bootstrap_binding_get(
	const struct payload_mm_authvar_presence_transaction_binding **binding)
{
	*binding = bootstrap_ready ? &bootstrap_binding : NULL;
	return bootstrap_ready ? CB_SUCCESS : CB_ERR;
}

enum cb_err smm_invocation_runtime_binding_get(struct smm_invocation_runtime_binding *binding)
{
	binding->evidence = &evidence;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_claimed_snapshot(
	const struct smm_invocation_evidence *value, uint8_t command, uint64_t sentinel,
	struct smm_invocation_token *token)
{
	assert(value == &evidence && command == SMM_APMC_AUTHVAR_PRESENCE &&
		sentinel == SMM_APMC_AUTHVAR_PRESENCE_SENTINEL);
	*token = (struct smm_invocation_token) {
		.initiator_cpu = bootstrap_binding.initiator_cpu, .active_cpus = claimed_cpus,
	};
	return cpu_ready ? CB_SUCCESS : CB_ERR;
}

bool starbook_mtl_dma_smm_cold_range_protected(void *context, uint64_t base, uint64_t size)
{
	assert(context == NULL);
	return dma_ready && base == 0x100000 && size == 4096;
}

enum cb_err payload_mm_authvar_presence_authority_install(
	const struct payload_mm_authvar_presence_policy *policy,
	payload_mm_authvar_protected_storage storage, void *context)
{
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];

	installs++;
	assert(storage(context, policy, sizeof(*policy)));
	assert(policy->dma_protected(NULL, 0x100000, 4096));
	assert(policy->cpu_rendezvous_active(NULL));
	assert(policy->provision(NULL, policy->endpoint.generation, owner.capability) == CB_ERR);
	owner.capability[0] ^= 1;
	assert(policy->provision(NULL, policy->endpoint.generation, capability) == CB_ERR);
	owner.capability[0] ^= 1;
	assert(policy->provision(NULL, policy->endpoint.generation, capability) == CB_SUCCESS);
	for (size_t index = 0; index < sizeof(capability); index++)
		assert(capability[index] == 0x5a);
	return install_result;
}

enum cb_err payload_mm_authvar_presence_authority_restrict(uint64_t generation)
{
	assert(generation == 42);
	assert(cpu_rendezvous_active(NULL));
	restrictions++;
	return CB_SUCCESS;
}

void smm_invocation_platform_fail_stop(void) { abort(); }
void do_full_reset(void) { abort(); }

static void reset_fixture(void)
{
	memset(&owner, 0, sizeof(owner));
	bootstrap_binding = (struct payload_mm_authvar_presence_transaction_binding) {
		.generation = 42, .initiator_cpu = 0, .maximum_cpus = 4,
	};
	bootstrap_ready = storage_ready = dma_ready = cpu_ready = true;
	installs = restrictions = 0;
	install_result = CB_SUCCESS;
	claimed_cpus = 4;
}

int main(void)
{
	const struct payload_mm_authvar_presence_route_authority_policy *policy;
	struct payload_mm_authvar_presence_seed seed = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION, .size = sizeof(seed),
		.endpoint.generation = 42,
	};

	memset(seed.capability, 0x5a, sizeof(seed.capability));
	reset_fixture();
	bootstrap_ready = false;
	policy = (const void *)1;
	assert(starbook_mtl_authvar_presence_authority_policy_get(&policy) == CB_ERR);
	assert(policy == NULL && owner.state == AUTHORITY_EMPTY);
	bootstrap_ready = true;
	assert(starbook_mtl_authvar_presence_authority_policy_get(
		(void *)((uintptr_t)&policy + 1)) == CB_ERR);
	assert(starbook_mtl_authvar_presence_authority_policy_get(
		(void *)&bootstrap_binding) == CB_ERR);
	assert(bootstrap_binding.generation == 42);
	assert(starbook_mtl_authvar_presence_authority_policy_get(
		(void *)&owner.binding) == CB_ERR);
	assert(owner.state == AUTHORITY_EMPTY);
	assert(starbook_mtl_authvar_presence_authority_policy_get(&policy) == CB_SUCCESS);
	assert(policy->prepare(NULL, &seed, 41) == CB_ERR && installs == 0);
	assert(policy->prepare(NULL, (const void *)((uintptr_t)&seed + 1), 42) ==
		CB_ERR && installs == 0);
	assert(policy->commit(NULL, 42) == CB_ERR);
	assert(policy->prepare(NULL, (const void *)&owner, 42) == CB_ERR && installs == 0);
	assert(policy->prepare(NULL, &seed, 42) == CB_SUCCESS && installs == 1);
	assert(!cpu_rendezvous_active(NULL));
	owner.state = AUTHORITY_COMMITTED;
	assert(!cpu_rendezvous_active(NULL));
	owner.state = AUTHORITY_PREPARED;
	for (size_t index = 0; index < sizeof(owner.capability); index++)
		assert(owner.capability[index] == 0 && owner.sealed_capability[index] == 0);
	assert(policy->prepare(NULL, &seed, 42) == CB_ERR && installs == 1);
	cpu_ready = false;
	assert(policy->commit(NULL, 42) == CB_ERR);
	cpu_ready = true;
	claimed_cpus = 3;
	assert(policy->commit(NULL, 42) == CB_ERR);
	claimed_cpus = 5;
	assert(policy->commit(NULL, 42) == CB_ERR);
	claimed_cpus = 4;
	assert(policy->commit(NULL, 42) == CB_SUCCESS);
	assert(cpu_rendezvous_active(NULL));
	assert(policy->commit(NULL, 42) == CB_ERR);
	bootstrap_binding.nonce++;
	assert(!policy->dma_protected(NULL, 0x100000, 4096));
	assert(policy->abort(NULL, 42) == CB_ERR && restrictions == 0);
	bootstrap_binding.nonce--;
	assert(policy->abort(NULL, 42) == CB_SUCCESS && restrictions == 1);
	assert(policy->abort(NULL, 42) == CB_SUCCESS && restrictions == 1);
	assert(policy->commit(NULL, 42) == CB_ERR);
	reset_fixture();
	assert(starbook_mtl_authvar_presence_authority_policy_get(&policy) == CB_SUCCESS);
	assert(policy->prepare(NULL, &seed, 42) == CB_SUCCESS);
	assert(policy->abort(NULL, 42) == CB_SUCCESS && restrictions == 1);
	assert(!cpu_rendezvous_active(NULL));
	reset_fixture();
	assert(starbook_mtl_authvar_presence_authority_policy_get(&policy) == CB_SUCCESS);
	owner.sealed_binding.transaction_id++;
	assert(policy->prepare(NULL, &seed, 42) == CB_ERR && installs == 0);
	reset_fixture();
	assert(starbook_mtl_authvar_presence_authority_policy_get(&policy) == CB_SUCCESS);
	install_result = CB_ERR;
	assert(policy->prepare(NULL, &seed, 42) == CB_ERR);
	assert(owner.state == AUTHORITY_FAILED && policy->commit(NULL, 42) == CB_ERR);
	assert(policy->abort(NULL, 42) == CB_ERR && restrictions == 0);
	return 0;
}
