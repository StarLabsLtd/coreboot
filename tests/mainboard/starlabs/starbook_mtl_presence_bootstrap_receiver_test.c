/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <cpu/x86/smm.h>
#include "authvar_presence_bootstrap_install.h"
#include "authvar_presence_authority_policy.h"
#include "authvar_presence_route_composition.h"
#include "dma_smm_receipt_provision.h"
#include <stdlib.h>
#include <string.h>

extern void exit(int status) __noreturn;
extern unsigned long strtoul(const char *text, char **end, int base);
static struct starbook_mtl_presence_bootstrap_frame frame;
static struct smm_dma_receipt_memory memory;
static struct payload_mm_authvar_presence_bootstrap slot;
static struct smm_invocation_topology topology;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_evidence evidence;
static struct payload_mm_authvar_presence_route_authority_policy policy;
static struct smm_invocation_save_state_ops ops;
static unsigned int scenario, imports, factories, writes;
static uint32_t initiator;
static uint64_t wire;

static void require(bool condition)
{
	if (!condition)
		exit(90);
}

enum cb_err smm_invocation_runtime_view_get(const struct smm_invocation_runtime_view **view)
{
	*view = (const void *)&slot;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	require(view == (const void *)&slot && base && size);
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

enum cb_err smm_invocation_topology_read(const struct smm_invocation_topology *source,
	struct smm_invocation_topology *snapshot)
{
	require(source == &topology);
	*snapshot = topology;
	return CB_SUCCESS;
}

bool smm_get_dma_receipt_memory(const struct smm_dma_receipt_memory **output)
{
	*output = &memory;
	return scenario != 3;
}

enum cb_err starbook_mtl_dma_smm_binding_get(struct starbook_mtl_dma_smm_binding *binding)
{
	static struct starbook_mtl_dma_smm_receipt receipt;
	binding->receipt = &receipt;
	return scenario == 4 ? CB_ERR : CB_SUCCESS;
}

struct payload_mm_authvar_presence_bootstrap *smm_get_payload_mm_authvar_presence_bootstrap(void)
{
	return &slot;
}

/* This suite tests the actual board receiver boundary, not receipt cryptography. */
enum cb_err payload_mm_authvar_presence_bootstrap_receipts_import(
	struct payload_mm_authvar_presence_bootstrap_receipts *receipts)
{
	imports++;
	require(!memcmp(receipts, &frame.receipts, sizeof(*receipts)));
	if (scenario == 5)
		return CB_ERR;
	slot.state = PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY;
	if (scenario == 6)
		frame.receipts.page.generation++;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_bootstrap_binding_get(
	const struct payload_mm_authvar_presence_transaction_binding **binding)
{
	*binding = &slot.binding;
	return scenario == 7 ? CB_ERR : CB_SUCCESS;
}

enum cb_err starbook_mtl_authvar_presence_authority_policy_get(
	const struct payload_mm_authvar_presence_route_authority_policy **output)
{
	*output = &policy;
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
	factories++;
	require(actual_composition == &composition && actual_instance == &instance &&
		actual_evidence == &evidence && actual_topology == &topology &&
		actual_policy == &policy && binding == &slot.binding &&
		verifier == &slot.page_verifier && receipt == &slot.page_receipt);
	require(protected_storage(context, &slot, sizeof(slot)));
	require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST && !writes);
	return scenario == 8 ? SMM_INVOCATION_TRY_ERROR : SMM_INVOCATION_TRY_SUCCESS;
}

static enum smm_invocation_match match(void *context, uint32_t cpu, uint8_t command)
{
	require(context == &ops && command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);
	return cpu == initiator || scenario == 9 ? SMM_INVOCATION_MATCHED :
		SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	require(context == &ops && cpu == initiator);
	*value = wire;
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	require(context == &ops && cpu == initiator && factories == 1);
	require(value == STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_SUCCESS);
	require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
	writes++;
	return scenario == 10 ? CB_ERR : CB_SUCCESS;
}

int main(int argc, char **argv)
{
	enum starbook_mtl_presence_bootstrap_result result;
	require(argc == 2);
	scenario = (unsigned int)strtoul(argv[1], NULL, 10);
	topology.active_cpus = 4;
	topology.bsp_cpu = 0;
	slot.binding.maximum_cpus = 64;
	ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match, .read_value = read_value,
		.write_value = write_value, .context = &ops, .context_size = sizeof(ops),
	};
	memory.frame.base = (uintptr_t)&frame;
	memory.frame.size = sizeof(frame);
	frame = (struct starbook_mtl_presence_bootstrap_frame) {
		.revision = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REVISION,
		.size = sizeof(frame), .state = STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST,
	};
	wire = ((uint64_t)(uintptr_t)&frame << 32) | STARBOOK_MTL_PRESENCE_BOOTSTRAP_WIRE_REQUEST;
	if (scenario == 1 || scenario == 2)
		initiator = 1;
	if (scenario == 1)
		wire = 0x12345678;
	if (scenario == 11)
		frame.reserved = 1;
	if (scenario == 12)
		memory.frame.base += 8;
	if (scenario == 13)
		memory.frame.size--;
	if (scenario == 17)
		frame.service_endpoint.tag = LB_TAG_AUTHVAR_SERVICE_ENDPOINT;
	if (scenario == 18)
		frame.revision = 1;
	if (scenario == 19)
		frame.size = 312;
	if (scenario == 20)
		frame.boot_private.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION;
	if (scenario == 21)
		frame.revision = 2;
	result = starbook_mtl_presence_bootstrap_receive(&ops);
	if (scenario == 1) {
		require(result == STARBOOK_MTL_PRESENCE_NOT_BOOTSTRAP && !imports && !writes);
		return 0;
	}
	if ((scenario >= 2 && scenario <= 6) || scenario == 9 ||
	    (scenario >= 11 && scenario <= 13) || (scenario >= 17 && scenario <= 21)) {
		require(result == STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR && !writes);
		require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
		return 0;
	}
	require(result == STARBOOK_MTL_PRESENCE_BOOTSTRAP_IMPORTED && imports == 1 && !writes);
	require(starbook_mtl_presence_bootstrap_receive(&ops) == STARBOOK_MTL_PRESENCE_BOOTSTRAP_ERROR);
	if (scenario == 7 || scenario == 8) {
		require(starbook_mtl_presence_bootstrap_route_install() == CB_ERR && !writes);
		require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
		return 0;
	}
	require(starbook_mtl_presence_bootstrap_route_install() == CB_SUCCESS);
	require(starbook_mtl_presence_bootstrap_response_publish() == CB_ERR && !writes);
	if (scenario == 14) {
		struct smm_invocation_save_state_ops other_ops = ops;
		require(starbook_mtl_presence_bootstrap_response_stage(&other_ops) == CB_ERR);
		require(!writes && frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
		return 0;
	}
	if (scenario == 10) {
		require(starbook_mtl_presence_bootstrap_response_stage(&ops) == CB_ERR);
		require(starbook_mtl_presence_bootstrap_response_publish() == CB_ERR);
		require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
		return 0;
	}
	require(starbook_mtl_presence_bootstrap_response_stage(&ops) == CB_SUCCESS);
	require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST && writes == 1);
	if (scenario == 15 || scenario == 16) {
		if (scenario == 15)
			frame.receipts.page.generation++;
		else
			memory.frame.size--;
		require(starbook_mtl_presence_bootstrap_response_publish() == CB_ERR);
		require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_REQUEST);
		return 0;
	}
	require(starbook_mtl_presence_bootstrap_response_publish() == CB_SUCCESS);
	require(frame.state == STARBOOK_MTL_PRESENCE_BOOTSTRAP_ACCEPTED && frame.maximum_cpus == 64);
	require(starbook_mtl_presence_bootstrap_response_publish() == CB_ERR);
	return 0;
}
