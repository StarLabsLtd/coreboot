/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_authority.h"
#include "payload_resource_policy.h"

#include <assert.h>
#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define ECAM 0xc0000000ULL
#define VENDOR_DEVICE(vendor, device) ((uint32_t)(device) << 16 | (vendor))
#define CLASS_REVISION(class) ((uint32_t)(class) << 8)
#define HEADER(type) ((uint32_t)(type) << 16)
#define BUSES(primary, secondary, subordinate) \
	((uint32_t)(primary) | (uint32_t)(secondary) << 8 | \
	 (uint32_t)(subordinate) << 16)
#define TABLE_PAGES 64U

struct smm_invocation_runtime_view {
	uint8_t opaque;
};

struct test_owner {
	struct starbook_mtl_dma_smm_receipt receipt;
	struct starbook_mtl_dma_smm_authority_workspace workspace;
} __aligned(8);

static const struct smm_invocation_runtime_view runtime_view;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_loader_instance instance;
static struct test_owner owner;
static uint8_t tables[TABLE_PAGES * VTD_TRANSLATION_PAGE_SIZE]
	__aligned(VTD_TRANSLATION_PAGE_SIZE);
static uint8_t mirror[TABLE_PAGES * VTD_TRANSLATION_PAGE_SIZE]
	__aligned(VTD_TRANSLATION_PAGE_SIZE);
static const void *unprotected;
static size_t config_reads;
static size_t mutate_config_at;
static size_t mutate_generation_at;
static size_t mutate_nonce_at;
static size_t mutate_lifecycle_at;

enum cb_err starbook_mtl_dma_smm_authority_config_output_test(
	struct starbook_mtl_dma_smm_authority_workspace *workspace,
	uint32_t *output);
enum cb_err starbook_mtl_dma_smm_authority_binding_output_test(
	struct starbook_mtl_dma_smm_authority_workspace *workspace,
	struct starbook_mtl_dma_requester_binding *output);
int starbook_mtl_dma_smm_authority_table_output_test(
	struct starbook_mtl_dma_smm_authority_workspace *workspace,
	uint64_t *output);

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view)
{
	*view = &runtime_view;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_binding_get(
	struct smm_invocation_runtime_binding *binding)
{
	*binding = (struct smm_invocation_runtime_binding) {
		.instance = &instance,
		.evidence = &evidence,
	};
	return CB_SUCCESS;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *source,
	struct smm_invocation_loader_instance *snapshot)
{
	if (source != &instance || !snapshot)
		return CB_ERR;
	*snapshot = instance;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base,
	size_t size)
{
	const uintptr_t address = (uintptr_t)base;

	if (view != &runtime_view || !base || !size ||
	    address > (uintptr_t)-1 - (size - 1U) || base == unprotected)
		return CB_ERR;
	return CB_SUCCESS;
}

static uint32_t config_value(uint8_t bus, uint8_t devfn, uint16_t offset)
{
	if (bus == 0 && devfn == STARBOOK_MTL_RP10_DEVFN) {
		if (offset == 0)
			return VENDOR_DEVICE(0x8086, 0x7e4d);
		if (offset == 8)
			return CLASS_REVISION(0x060400);
		if (offset == 0x0c)
			return HEADER(1);
		if (offset == 0x18)
			return BUSES(0, 2, 2);
	}
	if (bus == 2 && devfn == 0) {
		if (offset == 0)
			return VENDOR_DEVICE(0x144d, 0xa80b);
		if (offset == 8)
			return CLASS_REVISION(0x010802);
		if (offset == 0x0c)
			return HEADER(0);
	}
	if (bus == 0 && devfn == STARBOOK_MTL_PCH_XHCI_DEVFN) {
		if (offset == 0)
			return VENDOR_DEVICE(0x8086, 0x7e7d);
		if (offset == 8)
			return CLASS_REVISION(0x0c0330);
		if (offset == 0x0c)
			return HEADER(0);
	}
	if (bus == 0 && devfn == STARBOOK_MTL_TCSS_XHCI_DEVFN) {
		if (offset == 0)
			return VENDOR_DEVICE(0x8086, 0x7ec0);
		if (offset == 8)
			return CLASS_REVISION(0x0c0330);
		if (offset == 0x0c)
			return HEADER(0);
	}
	return UINT32_MAX;
}

enum cb_err starbook_mtl_dma_smm_test_read32(uint64_t address,
	uint32_t *value)
{
	if (!value)
		return CB_ERR;
	if (address >= ECAM && address - ECAM < (1ULL << 28)) {
		const uint8_t bus = (uint8_t)((address - ECAM) >> 20);
		const uint8_t devfn = (address >> 12) & 0xffU;
		const uint16_t reg = address & 0xfffU;

		config_reads++;
		if (mutate_generation_at && config_reads == mutate_generation_at)
			evidence.generation++;
		if (mutate_nonce_at && config_reads == mutate_nonce_at) {
			evidence.loader_instance_nonce.low++;
			instance.loader_instance_nonce = evidence.loader_instance_nonce;
		}
		if (mutate_lifecycle_at && config_reads == mutate_lifecycle_at) {
			evidence.loader_lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
			instance.lifecycle = evidence.loader_lifecycle;
		}
		*value = config_value(bus, devfn, reg);
		if (mutate_config_at && config_reads >= mutate_config_at &&
		    bus == 0 && devfn == STARBOOK_MTL_PCH_XHCI_DEVFN && !reg)
			*value ^= 0x10000U;
		return CB_SUCCESS;
	}
	if (address >= (uintptr_t)tables &&
	    address - (uintptr_t)tables < sizeof(tables)) {
		memcpy(value, (const void *)(uintptr_t)address, sizeof(*value));
		return CB_SUCCESS;
	}
	if (address >= (uintptr_t)mirror &&
	    address - (uintptr_t)mirror < sizeof(mirror)) {
		memcpy(value, (const void *)(uintptr_t)address, sizeof(*value));
		return CB_SUCCESS;
	}
	return CB_ERR;
}

static void initialize(void)
{
	struct vtd_translation_requester requesters[] = {
		{ .bdf = 0x200, .domain = 1, .cpu_base = 0x100000,
		  .device_base = 0x100000, .pages = 2 },
		{ .bdf = STARBOOK_MTL_PCH_XHCI_DEVFN, .domain = 2,
		  .cpu_base = 0x200000, .device_base = 0x200000, .pages = 2 },
		{ .bdf = STARBOOK_MTL_TCSS_XHCI_DEVFN, .domain = 3,
		  .cpu_base = 0x300000, .device_base = 0x300000, .pages = 2 },
	};
	struct vtd_translation_image image = {
		.memory = tables,
		.physical_base = (uintptr_t)tables,
		.capacity_pages = TABLE_PAGES,
	};

	memset(&owner, 0, sizeof(owner));
	memset(tables, 0, sizeof(tables));
	memset(mirror, 0, sizeof(mirror));
	assert(!vtd_translation_build(&image, requesters, 3));
	memcpy(mirror, tables, sizeof(mirror));
	instance.loader_instance_nonce =
		(struct smm_invocation_loader_instance_nonce) { .low = 1, .high = 2 };
	instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	evidence.loader_instance_nonce = instance.loader_instance_nonce;
	evidence.loader_lifecycle = instance.lifecycle;
	evidence.generation = 9;
	owner.receipt = (struct starbook_mtl_dma_smm_receipt) {
		.revision = STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION,
		.size = sizeof(owner.receipt),
		.function_count = 3,
		.tables = { (uintptr_t)tables, sizeof(tables) },
		.table_mirror = { (uintptr_t)mirror, sizeof(mirror) },
		.arenas = {
			{ 0x100000, 0x2000 }, { 0x200000, 0x2000 },
			{ 0x300000, 0x2000 },
		},
		.table_used_bytes = image.used_pages * VTD_TRANSLATION_PAGE_SIZE,
		.loader_instance_nonce = instance.loader_instance_nonce,
		.invocation_generation = evidence.generation,
		.loader_lifecycle = instance.lifecycle,
		.functions = {
			{ .bdf = 0x200, .vendor = 0x144d, .device = 0xa80b,
			  .class = 0x010802 },
			{ .bdf = STARBOOK_MTL_PCH_XHCI_DEVFN, .vendor = 0x8086,
			  .device = 0x7e7d, .class = 0x0c0330 },
			{ .bdf = STARBOOK_MTL_TCSS_XHCI_DEVFN, .vendor = 0x8086,
			  .device = 0x7ec0, .class = 0x0c0330 },
		},
	};
	unprotected = NULL;
	config_reads = 0;
	mutate_config_at = 0;
	mutate_generation_at = 0;
	mutate_nonce_at = 0;
	mutate_lifecycle_at = 0;
}

static enum cb_err verify(void)
{
	return starbook_mtl_dma_smm_authority_verify(&owner.receipt,
		&runtime_view, &owner, sizeof(owner), &owner.workspace);
}

int main(int argc, char **argv)
{
	uint32_t config_output;
	uint64_t table_output;
	struct starbook_mtl_dma_requester_binding binding_output;

	assert(argc == 2);
	initialize();
	if (!strcmp(argv[1], "valid")) {
		assert(verify() == CB_SUCCESS);
		assert(owner.workspace.context.table_read_base ==
			owner.receipt.tables.base);
		assert(starbook_mtl_dma_smm_authority_config_output_test(
			&owner.workspace, &config_output) == CB_SUCCESS);
		assert(starbook_mtl_dma_smm_authority_binding_output_test(
			&owner.workspace, &binding_output) == CB_SUCCESS);
		assert(!starbook_mtl_dma_smm_authority_table_output_test(
			&owner.workspace, &table_output));
	} else if (!strcmp(argv[1], "primary-mutation")) {
		tables[0] ^= 1U;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "mirror-mutation")) {
		mirror[0] ^= 1U;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "forged-inventory")) {
		owner.receipt.functions[0].bdf++;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "forged-identity")) {
		owner.receipt.functions[1].device++;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "swapped-arenas")) {
		const struct starbook_mtl_dma_smm_range first =
			owner.receipt.arenas[0];

		owner.receipt.arenas[0] = owner.receipt.arenas[1];
		owner.receipt.arenas[1] = first;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "swapped-graph-roles")) {
		struct vtd_translation_requester requesters[] = {
			{ .bdf = STARBOOK_MTL_PCH_XHCI_DEVFN, .domain = 1,
			  .cpu_base = 0x100000, .device_base = 0x100000, .pages = 2 },
			{ .bdf = 0x200, .domain = 2, .cpu_base = 0x200000,
			  .device_base = 0x200000, .pages = 2 },
			{ .bdf = STARBOOK_MTL_TCSS_XHCI_DEVFN, .domain = 3,
			  .cpu_base = 0x300000, .device_base = 0x300000, .pages = 2 },
		};
		struct vtd_translation_image image = {
			.memory = tables, .physical_base = (uintptr_t)tables,
			.capacity_pages = TABLE_PAGES,
		};

		memset(tables, 0, sizeof(tables));
		assert(!vtd_translation_build(&image, requesters, 3));
		memcpy(mirror, tables, sizeof(mirror));
		owner.receipt.table_used_bytes = image.used_pages *
			VTD_TRANSLATION_PAGE_SIZE;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "generation-rollback")) {
		evidence.generation--;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "generation-advance")) {
		evidence.generation++;
		assert(verify() == CB_SUCCESS);
	} else if (!strcmp(argv[1], "stale-lifecycle")) {
		instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
		evidence.loader_lifecycle = instance.lifecycle;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "topology-drift")) {
		mutate_config_at = 600;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "binding-drift")) {
		mutate_generation_at = 600;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "nonce-drift")) {
		mutate_nonce_at = 600;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "lifecycle-drift")) {
		mutate_lifecycle_at = 600;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "unprotected-workspace")) {
		unprotected = &owner.workspace;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "unprotected-receipt")) {
		unprotected = &owner.receipt;
		assert(verify() == CB_ERR);
	} else if (!strcmp(argv[1], "retained-underflow")) {
		assert(starbook_mtl_dma_smm_authority_verify(&owner.receipt,
			&runtime_view, &owner.receipt, sizeof(owner.receipt),
			&owner.workspace) == CB_ERR);
	} else if (!strcmp(argv[1], "receipt-workspace-alias")) {
		assert(starbook_mtl_dma_smm_authority_verify(&owner.receipt,
			&runtime_view, &owner, sizeof(owner),
			(void *)&owner.receipt) == CB_ERR);
	} else if (!strcmp(argv[1], "unprotected-config-output")) {
		assert(verify() == CB_SUCCESS);
		unprotected = &config_output;
		assert(starbook_mtl_dma_smm_authority_config_output_test(
			&owner.workspace, &config_output) == CB_ERR);
	} else if (!strcmp(argv[1], "unprotected-binding-output")) {
		assert(verify() == CB_SUCCESS);
		unprotected = &binding_output;
		assert(starbook_mtl_dma_smm_authority_binding_output_test(
			&owner.workspace, &binding_output) == CB_ERR);
	} else if (!strcmp(argv[1], "unprotected-table-output")) {
		assert(verify() == CB_SUCCESS);
		unprotected = &table_output;
		assert(starbook_mtl_dma_smm_authority_table_output_test(
			&owner.workspace, &table_output) != 0);
	} else if (!strcmp(argv[1], "aliased-config-output")) {
		assert(verify() == CB_SUCCESS);
		assert(starbook_mtl_dma_smm_authority_config_output_test(
			&owner.workspace, (uint32_t *)&owner.workspace.binding) == CB_ERR);
	} else if (!strcmp(argv[1], "aliased-binding-output")) {
		assert(verify() == CB_SUCCESS);
		assert(starbook_mtl_dma_smm_authority_binding_output_test(
			&owner.workspace, &owner.workspace.binding) == CB_ERR);
	} else if (!strcmp(argv[1], "aliased-table-output")) {
		assert(verify() == CB_SUCCESS);
		assert(starbook_mtl_dma_smm_authority_table_output_test(
			&owner.workspace,
			(uint64_t *)&owner.workspace.binding) != 0);
	} else {
		return 2;
	}
	return 0;
}
