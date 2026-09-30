/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_s3_rearm.h"
#include "authvar_presence_s3_cold.h"
#include "dma_smm_receipt_provision.h"

#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_presence_s3_backing.h>
#include <bootmem.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL authenticated-variable S3 rearm is SMM-only"
#endif

enum rearm_state { REARM_EMPTY, REARM_BORROWING, REARM_BORROWED,
	REARM_COMPLETING, REARM_ACTIVE, REARM_SEALING, REARM_SEALED,
	REARM_POISONED };

struct rearm_dma_context {
	uint64_t presence_base;
	uint64_t close_base;
};

static struct {
	uint32_t state;
	uint32_t reserved;
	struct payload_mm_authvar_presence_s3_facts facts;
	struct payload_mm_authvar_presence_s3_facts sealed_facts;
	struct rearm_dma_context dma;
	struct rearm_dma_context sealed_dma;
	struct payload_mm_authvar_presence_lifecycle_close_s3_route route;
	const struct smm_invocation_save_state_ops *retained_ops;
} owner __aligned(8);

static __noinline void scrub(void *object, size_t size)
{
	volatile uint8_t *bytes = object;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool owner_protected(void)
{
	return starbook_mtl_authvar_presence_s3_protected_storage(
		NULL, &owner, sizeof(owner));
}

static bool facts_match_backing(
	const struct payload_mm_authvar_presence_s3_facts *facts,
	const struct payload_mm_authvar_presence_s3_backing *backing)
{
	return facts && backing &&
		!memcmp(&facts->presence_endpoint, &backing->presence,
			sizeof(facts->presence_endpoint)) &&
		!memcmp(&facts->close_endpoint, &backing->lifecycle_close,
			sizeof(facts->close_endpoint)) &&
		facts->presence_backing.base == backing->presence.communication_base &&
		facts->presence_backing.bytes ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE &&
		facts->presence_backing.generation == backing->presence.generation &&
		facts->presence_backing.tag == BM_MEM_RESERVED &&
		facts->close_backing_base ==
			backing->lifecycle_close.communication_base &&
		facts->close_backing_bytes ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE &&
		facts->close_backing_tag == BM_MEM_RESERVED &&
		facts->presence_terminal == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_CLOSED &&
		facts->lifecycle_close_state ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_S3_LIFECYCLE_CLOSE_IDLE;
}

static bool dma_protected(void *context, uint64_t base, uint64_t size)
{
	const struct payload_mm_authvar_presence_s3_facts *facts = &owner.facts;
	const struct rearm_dma_context *dma = context;
	const uint32_t state = __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE);

	return dma && owner_protected() &&
		(state == REARM_COMPLETING || state == REARM_ACTIVE) &&
		!memcmp(dma, &owner.dma, sizeof(*dma)) &&
		!memcmp(dma, &owner.sealed_dma, sizeof(*dma)) &&
		!memcmp(facts, &owner.sealed_facts, sizeof(*facts)) &&
		starbook_mtl_dma_smm_epoch_range_protected(NULL, base, size) &&
		size == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE &&
		((base == dma->presence_base &&
		  base == facts->presence_backing.base) ||
		 (base == dma->close_base && base == facts->close_backing_base)) &&
		__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) == state;
}

void starbook_mtl_authvar_presence_s3_rearm_poison(void)
{
	void *storage;
	size_t size;

	if (starbook_mtl_authvar_presence_s3_record_storage(&storage, &size))
		payload_mm_authvar_presence_s3_record_poison(storage, size);
	starbook_mtl_dma_smm_epoch_poison();
	__atomic_store_n(&owner.state, REARM_POISONED, __ATOMIC_RELEASE);
}

enum cb_err starbook_mtl_authvar_presence_s3_rearm_borrow(
	const struct smm_invocation_loader_instance *instance,
	struct payload_mm_authvar_presence_s3_facts *facts)
{
	const struct payload_mm_authvar_presence_s3_backing *backing;
	struct smm_invocation_loader_instance snapshot;
	void *storage;
	size_t size;
	uint32_t expected = REARM_EMPTY;
	enum cb_err result = CB_ERR;

	if (!instance || !facts || !owner_protected() ||
	    !starbook_mtl_authvar_presence_s3_protected_storage(
		NULL, facts, sizeof(*facts)))
		goto out;
	scrub(facts, sizeof(*facts));
	if (!__atomic_compare_exchange_n(&owner.state, &expected, REARM_BORROWING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    smm_invocation_loader_instance_read(instance, &snapshot) != CB_SUCCESS ||
	    snapshot.lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD ||
	    !starbook_mtl_authvar_presence_s3_record_storage(&storage, &size))
		goto out;
	backing = smm_get_payload_mm_authvar_presence_s3_backing();
	if (!backing ||
	    payload_mm_authvar_presence_s3_record_resume_borrow(storage, size,
		&owner.facts) != CB_SUCCESS ||
	    !facts_match_backing(&owner.facts, backing) ||
	    !starbook_mtl_dma_smm_epoch_range_protected(NULL,
		owner.facts.presence_backing.base,
		owner.facts.presence_backing.bytes) ||
	    !starbook_mtl_dma_smm_epoch_range_protected(NULL,
		owner.facts.close_backing_base, owner.facts.close_backing_bytes))
		goto out;
	owner.sealed_facts = owner.facts;
	owner.dma = owner.sealed_dma = (struct rearm_dma_context) {
		.presence_base = owner.facts.presence_backing.base,
		.close_base = owner.facts.close_backing_base,
	};
	*facts = owner.facts;
	if (!owner_protected() || memcmp(facts, &owner.facts, sizeof(*facts)))
		goto out;
	__atomic_store_n(&owner.state, REARM_BORROWED, __ATOMIC_RELEASE);
	result = CB_SUCCESS;
out:
	scrub(&snapshot, sizeof(snapshot));
	if (result != CB_SUCCESS)
		starbook_mtl_authvar_presence_s3_rearm_poison();
	return result;
}

enum cb_err starbook_mtl_authvar_presence_s3_rearm_complete(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	const struct payload_mm_authvar_presence_s3_backing *backing;
	struct payload_mm_authvar_presence_lifecycle_close_s3_policy policy;
	void *storage;
	size_t size;
	uint32_t expected = REARM_BORROWED;
	enum cb_err result = CB_ERR;

	if (!composition || !instance || !evidence || !topology || !ops || !facts ||
	    !owner_protected() || memcmp(facts, &owner.facts, sizeof(*facts)) ||
	    memcmp(facts, &owner.sealed_facts, sizeof(*facts)) ||
	    !__atomic_compare_exchange_n(&owner.state, &expected, REARM_COMPLETING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    !starbook_mtl_authvar_presence_s3_record_storage(&storage, &size))
		goto out;
	backing = smm_get_payload_mm_authvar_presence_s3_backing();
	if (!backing || !facts_match_backing(facts, backing) ||
	    payload_mm_authvar_presence_authority_restore_closed(
		&facts->presence_endpoint, &facts->presence_backing,
		dma_protected, &owner.dma, sizeof(owner.dma),
		starbook_mtl_authvar_presence_s3_protected_storage, NULL) != CB_SUCCESS)
		goto out;
	policy = (struct payload_mm_authvar_presence_lifecycle_close_s3_policy) {
		.revision =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_POLICY_REVISION,
		.size = sizeof(policy),
		.backing_base = facts->close_backing_base,
		.backing_size = facts->close_backing_bytes,
		.backing_tag = facts->close_backing_tag,
		.dram_provenance =
			smm_payload_mm_authvar_presence_s3_dram_provenance,
		.dram_provenance_context = (void *)backing,
		.dram_provenance_context_size = sizeof(*backing),
		.dma_protected = dma_protected,
		.dma_context = &owner.dma,
		.dma_context_size = sizeof(owner.dma),
	};
	if (payload_mm_authvar_presence_lifecycle_close_s3_route_provision(
		&owner.route, &facts->close_endpoint, composition, instance,
		evidence, topology, ops, &policy,
		starbook_mtl_authvar_presence_s3_protected_storage, NULL, 0U) !=
		CB_SUCCESS || starbook_mtl_dma_smm_epoch_retain() != CB_SUCCESS ||
	    payload_mm_authvar_presence_s3_record_rearm_commit(storage, size,
		facts) != CB_SUCCESS || !owner_protected())
		goto out;
	owner.retained_ops = ops;
	__atomic_store_n(&owner.state, REARM_ACTIVE, __ATOMIC_RELEASE);
	result = CB_SUCCESS;
out:
	scrub(&policy, sizeof(policy));
	if (result != CB_SUCCESS)
		starbook_mtl_authvar_presence_s3_rearm_poison();
	return result;
}

enum cb_err starbook_mtl_authvar_presence_s3_binding_get(
	struct starbook_mtl_authvar_presence_s3_binding *binding)
{
	struct starbook_mtl_authvar_presence_s3_binding value;

	if (!binding || !owner_protected() ||
	    !starbook_mtl_authvar_presence_s3_protected_storage(
		NULL, binding, sizeof(*binding)) ||
	    __atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) != REARM_ACTIVE ||
	    !owner.retained_ops)
		return CB_ERR;
	value = (struct starbook_mtl_authvar_presence_s3_binding) {
		.route = &owner.route,
		.retained_ops = owner.retained_ops,
	};
	*binding = value;
	return owner_protected() &&
		__atomic_load_n(&owner.state, __ATOMIC_ACQUIRE) == REARM_ACTIVE ?
		CB_SUCCESS : CB_ERR;
}

enum cb_err starbook_mtl_authvar_presence_s3_rearm_suspend(void)
{
	struct lb_authvar_presence_endpoint endpoint;
	struct payload_mm_authvar_presence_backing backing;
	void *storage;
	size_t size;
	uint32_t expected = REARM_ACTIVE;
	enum cb_err result = CB_ERR;

	if (!owner_protected() ||
	    memcmp(&owner.facts, &owner.sealed_facts, sizeof(owner.facts)) ||
	    !starbook_mtl_authvar_presence_s3_record_storage(&storage, &size) ||
	    payload_mm_authvar_presence_authority_closed_snapshot(&endpoint,
		&backing, starbook_mtl_authvar_presence_s3_protected_storage,
		NULL, 0U) != CB_SUCCESS ||
	    memcmp(&endpoint, &owner.facts.presence_endpoint, sizeof(endpoint)) ||
	    memcmp(&backing, &owner.facts.presence_backing, sizeof(backing)) ||
	    !payload_mm_authvar_presence_lifecycle_close_s3_route_idle_exact(
		&owner.route, &owner.facts.close_endpoint,
		owner.facts.close_backing_base,
		owner.facts.close_backing_bytes) ||
	    !__atomic_compare_exchange_n(&owner.state, &expected, REARM_SEALING,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    payload_mm_authvar_presence_s3_record_suspend_seal(storage, size,
		&owner.facts) != CB_SUCCESS || !owner_protected())
		goto out;
	__atomic_store_n(&owner.state, REARM_SEALED, __ATOMIC_RELEASE);
	result = CB_SUCCESS;
out:
	scrub(&endpoint, sizeof(endpoint));
	scrub(&backing, sizeof(backing));
	if (result != CB_SUCCESS)
		starbook_mtl_authvar_presence_s3_rearm_poison();
	return result;
}

#if ENV_TEST
void starbook_mtl_authvar_presence_s3_rearm_reset_test(void)
{
	scrub(&owner, sizeof(owner));
}
#endif
