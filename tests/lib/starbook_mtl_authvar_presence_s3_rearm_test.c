/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_s3_backing.h>
#include <bootmem.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_s3_rearm.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_s3_cold.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static uint8_t record[4096] __aligned(8);
static struct payload_mm_authvar_presence_s3_backing platform_backing;
static struct payload_mm_authvar_presence_s3_facts retained;
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology topology;
static struct smm_invocation_save_state_ops ops;
static unsigned int event;
static unsigned int poison_calls;
static unsigned int epoch_poison_calls;
static unsigned int seal_calls;
static unsigned int fail_stage;

bool starbook_mtl_authvar_presence_s3_protected_storage(
	void *unused, const void *storage, size_t size)
{
	(void)unused;
	return storage && size;
}

bool starbook_mtl_authvar_presence_s3_record_storage(void **storage,
	size_t *size)
{
	*storage = record;
	*size = sizeof(record);
	return true;
}

const struct payload_mm_authvar_presence_s3_backing *
smm_get_payload_mm_authvar_presence_s3_backing(void)
{
	return &platform_backing;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *source,
	struct smm_invocation_loader_instance *snapshot)
{
	if (source != &instance)
		return CB_ERR;
	*snapshot = *source;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_s3_record_resume_borrow(
	void *storage, size_t size,
	struct payload_mm_authvar_presence_s3_facts *facts)
{
	assert(storage == record && size == sizeof(record));
	if (fail_stage == 1)
		return CB_ERR;
	*facts = retained;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_s3_record_rearm_commit(
	void *storage, size_t size,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	assert(storage == record && size == sizeof(record));
	assert(!memcmp(facts, &retained, sizeof(*facts)));
	assert(event == 2);
	event = 3;
	return fail_stage == 4 ? CB_ERR : CB_SUCCESS;
}

void payload_mm_authvar_presence_s3_record_poison(void *storage, size_t size)
{
	assert(storage == record && size == sizeof(record));
	poison_calls++;
}

enum cb_err payload_mm_authvar_presence_s3_record_suspend_seal(
	void *storage, size_t size,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	struct starbook_mtl_authvar_presence_s3_binding binding;

	assert(storage == record && size == sizeof(record));
	assert(!memcmp(facts, &retained, sizeof(*facts)) && event == 3);
	assert(starbook_mtl_authvar_presence_s3_binding_get(&binding) != CB_SUCCESS);
	seal_calls++;
	event = 4;
	return fail_stage == 6 ? CB_ERR : CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_authority_closed_snapshot(
	struct lb_authvar_presence_endpoint *endpoint,
	struct payload_mm_authvar_presence_backing *backing,
	payload_mm_authvar_protected_storage storage, void *context,
	size_t context_size)
{
	(void)storage;
	(void)context;
	(void)context_size;
	*endpoint = retained.presence_endpoint;
	*backing = retained.presence_backing;
	return CB_SUCCESS;
}

bool payload_mm_authvar_presence_lifecycle_close_s3_route_idle_exact(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	uint64_t backing_base, uint64_t backing_size)
{
	(void)route;
	return !memcmp(endpoint, &retained.close_endpoint, sizeof(*endpoint)) &&
		backing_base == retained.close_backing_base &&
		backing_size == retained.close_backing_bytes;
}

bool starbook_mtl_dma_smm_epoch_range_protected(
	void *unused, uint64_t base, uint64_t size)
{
	(void)unused;
	return fail_stage != 2 && size == 4096U &&
		(base == retained.presence_backing.base ||
		 base == retained.close_backing_base);
}

enum cb_err starbook_mtl_dma_smm_epoch_retain(void)
{
	assert(event == 2);
	return CB_SUCCESS;
}

void starbook_mtl_dma_smm_epoch_poison(void)
{
	epoch_poison_calls++;
}

enum cb_err payload_mm_authvar_presence_authority_restore_closed(
	const struct lb_authvar_presence_endpoint *endpoint,
	const struct payload_mm_authvar_presence_backing *backing,
	payload_mm_authvar_presence_range_proof_fn dma_protected,
	const void *dma_context,
	size_t dma_context_size,
	payload_mm_authvar_protected_storage storage_protected,
	void *storage_context)
{
	assert(!memcmp(endpoint, &retained.presence_endpoint, sizeof(*endpoint)));
	assert(!memcmp(backing, &retained.presence_backing, sizeof(*backing)));
	assert(dma_context && dma_context_size && storage_protected(storage_context,
		endpoint, sizeof(*endpoint)));
	assert(dma_protected((void *)dma_context, backing->base, backing->bytes));
	assert(!event);
	event = 1;
	return fail_stage == 3 ? CB_ERR : CB_SUCCESS;
}

bool smm_payload_mm_authvar_presence_s3_dram_provenance(
	void *context, uint64_t base, uint64_t size)
{
	return context == &platform_backing && base && size;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_s3_route_provision(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	const struct smm_invocation_loader_composition *actual_composition,
	const struct smm_invocation_loader_instance *actual_instance,
	struct smm_invocation_evidence *actual_evidence,
	const struct smm_invocation_topology *actual_topology,
	const struct smm_invocation_save_state_ops *actual_ops,
	const struct payload_mm_authvar_presence_lifecycle_close_s3_policy *policy,
	payload_mm_authvar_protected_storage storage,
	void *storage_context, size_t storage_context_size)
{
	assert(route && actual_composition == &composition &&
		actual_instance == &instance && actual_evidence == &evidence &&
		actual_topology == &topology && actual_ops == &ops);
	assert(!memcmp(endpoint, &retained.close_endpoint, sizeof(*endpoint)));
	assert(policy->backing_base == retained.close_backing_base &&
		policy->backing_size == retained.close_backing_bytes &&
		policy->dma_protected(policy->dma_context, policy->backing_base,
			policy->backing_size));
	assert(storage(storage_context, route, sizeof(*route)));
	assert(!storage_context_size && event == 1);
	event = 2;
	return fail_stage == 5 ? CB_ERR : CB_SUCCESS;
}

static void initialize(void)
{
	memset(&platform_backing, 0, sizeof(platform_backing));
	memset(&retained, 0, sizeof(retained));
	starbook_mtl_authvar_presence_s3_rearm_reset_test();
	instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	instance.loader_instance_nonce.low = 1;
	retained.presence_endpoint.communication_base = 0x120000;
	retained.presence_endpoint.generation = 7;
	retained.presence_backing = (struct payload_mm_authvar_presence_backing) {
		.base = 0x120000, .bytes = 4096, .generation = 7,
		.tag = BM_MEM_RESERVED,
	};
	retained.close_endpoint.communication_base = 0x121000;
	retained.close_backing_base = 0x121000;
	retained.close_backing_bytes = 4096;
	retained.close_backing_tag = BM_MEM_RESERVED;
	retained.presence_terminal = PAYLOAD_MM_AUTHVAR_PRESENCE_S3_CLOSED;
	retained.lifecycle_close_state =
		PAYLOAD_MM_AUTHVAR_PRESENCE_S3_LIFECYCLE_CLOSE_IDLE;
	platform_backing.presence = retained.presence_endpoint;
	platform_backing.lifecycle_close = retained.close_endpoint;
	event = 0;
	poison_calls = 0;
	epoch_poison_calls = 0;
	seal_calls = 0;
}

static void complete_cycle(struct payload_mm_authvar_presence_s3_facts *facts)
{
	assert(starbook_mtl_authvar_presence_s3_rearm_borrow(&instance, facts) ==
		CB_SUCCESS);
	assert(starbook_mtl_authvar_presence_s3_rearm_complete(&composition,
		&instance, &evidence, &topology, &ops, facts) == CB_SUCCESS);
	assert(event == 3 && !poison_calls && !epoch_poison_calls);
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_presence_s3_facts facts;
	struct starbook_mtl_authvar_presence_s3_binding binding;

	assert(argc == 2);
	assert(argv[1][0] >= '0' && argv[1][0] <= '6' && !argv[1][1]);
	fail_stage = (unsigned int)(argv[1][0] - '0');
	initialize();
	if (fail_stage == 2)
		assert(starbook_mtl_authvar_presence_s3_rearm_borrow(&instance,
			&facts) != CB_SUCCESS);
	else if (fail_stage == 1)
		assert(starbook_mtl_authvar_presence_s3_rearm_borrow(&instance,
			&facts) != CB_SUCCESS);
	else {
		assert(starbook_mtl_authvar_presence_s3_rearm_borrow(&instance,
			&facts) == CB_SUCCESS);
		assert(starbook_mtl_authvar_presence_s3_rearm_complete(&composition,
			&instance, &evidence, &topology, &ops, &facts) ==
			(fail_stage >= 3U && fail_stage <= 5U ? CB_ERR : CB_SUCCESS));
	}
	if (fail_stage == 6U) {
		assert(starbook_mtl_authvar_presence_s3_binding_get(&binding) ==
			CB_SUCCESS);
		assert(starbook_mtl_authvar_presence_s3_rearm_suspend() == CB_ERR);
		assert(seal_calls == 1U && poison_calls == 1U &&
			epoch_poison_calls == 1U);
		assert(starbook_mtl_authvar_presence_s3_binding_get(&binding) !=
			CB_SUCCESS);
		return 0;
	}
	if (fail_stage) {
		assert(poison_calls && epoch_poison_calls &&
			starbook_mtl_authvar_presence_s3_binding_get(&binding) != CB_SUCCESS);
		return 0;
	}
	assert(event == 3 && !poison_calls);
	assert(starbook_mtl_authvar_presence_s3_binding_get(&binding) == CB_SUCCESS);
	assert(binding.retained_ops == &ops && binding.route);
	assert(starbook_mtl_authvar_presence_s3_rearm_suspend() == CB_SUCCESS);
	assert(event == 4 && seal_calls == 1U && !poison_calls && !epoch_poison_calls);
	assert(starbook_mtl_authvar_presence_s3_binding_get(&binding) != CB_SUCCESS);
	assert(starbook_mtl_authvar_presence_s3_rearm_suspend() != CB_SUCCESS);
	assert(seal_calls == 1U && poison_calls == 1U && epoch_poison_calls == 1U);
	initialize();
	retained.presence_endpoint.generation++;
	retained.presence_backing.generation++;
	platform_backing.presence = retained.presence_endpoint;
	instance.loader_instance_nonce.low++;
	complete_cycle(&facts);
	assert(starbook_mtl_authvar_presence_s3_rearm_suspend() == CB_SUCCESS);
	assert(event == 4 && seal_calls == 1U && !poison_calls && !epoch_poison_calls);
	return 0;
}
