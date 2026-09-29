/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_authority.h"
#include "payload_resource_policy.h"

#include <commonlib/helpers.h>
#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <device/mmio.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "MTL DMA SMM authority composition is SMM-only"
#endif

#if ENV_TEST
#define AUTHORITY_ECAM_BASE 0xc0000000ULL
#define AUTHORITY_ECAM_BUSES 256U
#else
#include <soc/iomap.h>
#define AUTHORITY_ECAM_BASE CONFIG_ECAM_MMCONF_BASE_ADDRESS
#define AUTHORITY_ECAM_BUSES CONFIG_ECAM_MMCONF_BUS_NUMBER
#endif

#define AUTHORITY_CONTEXT_IDENTITY 0x4d544c444d414155ULL

#if ENV_TEST
enum cb_err starbook_mtl_dma_smm_test_read32(uint64_t address,
	uint32_t *value);
#endif

static bool protected_span(
	const struct smm_invocation_runtime_view *runtime_view,
	const void *base, size_t size)
{
	return smm_invocation_runtime_range_is_protected(runtime_view, base,
		size) == CB_SUCCESS;
}

static bool spans_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!first || !second || !first_size || !second_size ||
	    first_base > (uintptr_t)-1 - (first_size - 1U) ||
	    second_base > (uintptr_t)-1 - (second_size - 1U))
		return true;
	return first_base <= second_base ?
		second_base - first_base < first_size :
		first_base - second_base < second_size;
}

static bool span_contains(const void *container, size_t container_size,
	const void *range, size_t range_size)
{
	const uintptr_t container_base = (uintptr_t)container;
	const uintptr_t range_base = (uintptr_t)range;

	return container && range && container_size && range_size &&
		container_base <= (uintptr_t)-1 - (container_size - 1U) &&
		range_base >= container_base &&
		range_base - container_base < container_size &&
		range_size <= container_size - (range_base - container_base);
}

static bool output_valid(
	const struct starbook_mtl_dma_smm_authority_context *owner,
	const void *output, size_t size, size_t alignment)
{
	const uintptr_t address = (uintptr_t)output;

	return owner && owner->runtime_view && output && size && alignment &&
		!(address & (alignment - 1U)) &&
		address <= (uintptr_t)-1 - (size - 1U) &&
		protected_span(owner->runtime_view, output, size) &&
		!spans_overlap(output, size, owner->workspace,
			owner->workspace_size) &&
		!spans_overlap(output, size, owner->retained,
			owner->retained_size);
}

static bool binding_equal(
	const struct starbook_mtl_dma_requester_binding *first,
	const struct starbook_mtl_dma_requester_binding *second)
{
	return first && second &&
		smm_invocation_loader_instance_nonce_equal(
			first->loader_instance_nonce,
			second->loader_instance_nonce) &&
		first->invocation_generation == second->invocation_generation &&
		first->loader_lifecycle == second->loader_lifecycle &&
		!first->reserved && !second->reserved;
}

static enum cb_err read_current_binding(
	const struct smm_invocation_runtime_view *expected_view,
	struct starbook_mtl_dma_requester_binding *value)
{
	const struct smm_invocation_runtime_view *runtime_view;
	struct smm_invocation_runtime_binding binding;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_loader_instance_nonce nonce_before;
	struct smm_invocation_loader_instance_nonce nonce_after;
	uint64_t generation_before;
	uint64_t generation_after;
	uint32_t lifecycle_before;
	uint32_t lifecycle_after;

	if (!expected_view || !value ||
	    smm_invocation_runtime_view_get(&runtime_view) != CB_SUCCESS ||
	    runtime_view != expected_view ||
	    smm_invocation_runtime_binding_get(&binding) != CB_SUCCESS ||
	    !binding.evidence || !binding.instance ||
	    !protected_span(runtime_view, binding.evidence,
		sizeof(*binding.evidence)) ||
	    !protected_span(runtime_view, binding.instance,
		sizeof(*binding.instance)))
		return CB_ERR;
	nonce_before.low = __atomic_load_n(
		&binding.evidence->loader_instance_nonce.low, __ATOMIC_ACQUIRE);
	nonce_before.high = __atomic_load_n(
		&binding.evidence->loader_instance_nonce.high, __ATOMIC_ACQUIRE);
	generation_before = __atomic_load_n(&binding.evidence->generation,
		__ATOMIC_ACQUIRE);
	lifecycle_before = __atomic_load_n(&binding.evidence->loader_lifecycle,
		__ATOMIC_ACQUIRE);
	if (smm_invocation_loader_instance_read(binding.instance, &instance) !=
		CB_SUCCESS)
		return CB_ERR;
	nonce_after.low = __atomic_load_n(
		&binding.evidence->loader_instance_nonce.low, __ATOMIC_ACQUIRE);
	nonce_after.high = __atomic_load_n(
		&binding.evidence->loader_instance_nonce.high, __ATOMIC_ACQUIRE);
	generation_after = __atomic_load_n(&binding.evidence->generation,
		__ATOMIC_ACQUIRE);
	lifecycle_after = __atomic_load_n(&binding.evidence->loader_lifecycle,
		__ATOMIC_ACQUIRE);
	if (!generation_before || generation_before != generation_after ||
	    lifecycle_before != lifecycle_after ||
	    !smm_invocation_loader_instance_nonce_equal(nonce_before,
		nonce_after) ||
	    smm_invocation_loader_instance_nonce_is_zero(
		instance.loader_instance_nonce) ||
	    !smm_invocation_loader_instance_nonce_equal(
		instance.loader_instance_nonce,
		nonce_after) || instance.lifecycle != lifecycle_after)
		return CB_ERR;
	*value = (struct starbook_mtl_dma_requester_binding) {
		.loader_instance_nonce = instance.loader_instance_nonce,
		.invocation_generation = generation_after,
		.loader_lifecycle = instance.lifecycle,
	};
	memset(&binding, 0, sizeof(binding));
	memset(&instance, 0, sizeof(instance));
	return CB_SUCCESS;
}

static enum cb_err authority_read_binding(void *context,
	struct starbook_mtl_dma_requester_binding *binding)
{
	const struct starbook_mtl_dma_smm_authority_context *owner = context;

	if (!owner || owner->identity != AUTHORITY_CONTEXT_IDENTITY ||
	    !output_valid(owner, binding, sizeof(*binding), _Alignof(*binding)))
		return CB_ERR;
	return read_current_binding(owner->runtime_view, binding);
}

static enum cb_err stable_read32(uint64_t address, uint32_t *value)
{
	uint32_t first;
	uint32_t second;

	if (!value || address > (uintptr_t)-1 - sizeof(*value))
		return CB_ERR;
#if ENV_TEST
	if (starbook_mtl_dma_smm_test_read32(address, &first) != CB_SUCCESS ||
	    starbook_mtl_dma_smm_test_read32(address, &second) != CB_SUCCESS)
		return CB_ERR;
#else
	first = read32p((uintptr_t)address);
	second = read32p((uintptr_t)address);
#endif
	if (first != second)
		return CB_ERR;
	*value = second;
	return CB_SUCCESS;
}

static enum cb_err authority_read_config32(void *context, uint8_t bus,
	uint8_t devfn, uint16_t offset, uint32_t *value)
{
	const struct starbook_mtl_dma_smm_authority_context *owner = context;
	uint64_t address;

	if (!owner || owner->identity != AUTHORITY_CONTEXT_IDENTITY ||
	    !output_valid(owner, value, sizeof(*value), _Alignof(*value)) ||
	    (offset & 3U) || offset > 0xffcU)
		return CB_ERR;
	address = AUTHORITY_ECAM_BASE + ((uint64_t)bus << 20) +
		((uint64_t)devfn << 12) + offset;
	if (address < AUTHORITY_ECAM_BASE ||
	    address - AUTHORITY_ECAM_BASE >=
		((uint64_t)AUTHORITY_ECAM_BUSES << 20))
		return CB_ERR;
	return stable_read32(address, value);
}

static int authority_read_table64(void *context, uint64_t address,
	uint64_t *value)
{
	const struct starbook_mtl_dma_smm_authority_context *owner = context;
	uint32_t high_before;
	uint32_t high_after;
	uint32_t low;

	if (!owner || owner->identity != AUTHORITY_CONTEXT_IDENTITY ||
	    !output_valid(owner, value, sizeof(*value), _Alignof(*value)) ||
	    (address & (sizeof(*value) - 1U)) ||
	    address < owner->table_logical_base ||
	    address - owner->table_logical_base > owner->table_size ||
	    sizeof(*value) > owner->table_size -
		(address - owner->table_logical_base) ||
	    owner->table_read_base > UINT64_MAX -
		(address - owner->table_logical_base))
		return -1;
	address = owner->table_read_base +
		(address - owner->table_logical_base);
	if (stable_read32(address + 4U, &high_before) != CB_SUCCESS ||
	    stable_read32(address, &low) != CB_SUCCESS ||
	    stable_read32(address + 4U, &high_after) != CB_SUCCESS ||
	    high_before != high_after)
		return -1;
	*value = (uint64_t)high_after << 32 | low;
	return 0;
}

static bool contract_valid(
	const struct starbook_mtl_dma_smm_authority_workspace *workspace,
	const struct smm_invocation_runtime_view *runtime_view)
{
	return workspace && runtime_view &&
		protected_span(runtime_view, workspace, sizeof(*workspace)) &&
		workspace->context.identity == AUTHORITY_CONTEXT_IDENTITY &&
		workspace->context.runtime_view == runtime_view &&
		workspace->context.workspace == workspace &&
		workspace->context.workspace_size == sizeof(*workspace) &&
		workspace->context.retained && workspace->context.retained_size &&
		workspace->io.context == &workspace->context &&
		workspace->io.read_config32 == authority_read_config32 &&
		workspace->io.read_binding == authority_read_binding &&
		workspace->view.context == &workspace->context &&
		workspace->view.read64 == authority_read_table64 &&
		protected_span(runtime_view,
			(const void *)(uintptr_t)workspace->io.read_config32, 1U) &&
		protected_span(runtime_view,
			(const void *)(uintptr_t)workspace->io.read_binding, 1U) &&
		protected_span(runtime_view,
			(const void *)(uintptr_t)workspace->view.read64, 1U);
}

static bool authority_matches_receipt(
	const struct starbook_mtl_dma_requester_authority *authority,
	const struct starbook_mtl_dma_smm_receipt *receipt)
{
	for (size_t role = 0; role < STARBOOK_MTL_DMA_REQUESTER_ROLE_COUNT;
	     role++) {
		const struct starbook_mtl_dma_requester *requester =
			&authority->requester[role];
		size_t matches = 0;

		for (size_t function = 0; function < receipt->function_count;
		     function++) {
			const struct pci_bme_quiesce_function *entry =
				&receipt->functions[function];

			if (entry->bdf != requester->bdf)
				continue;
			if (entry->vendor != requester->vendor ||
			    entry->device != requester->device ||
			    entry->class != requester->class)
				return false;
			matches++;
		}
		if (matches != 1U)
			return false;
	}
	return true;
}

static int verify_graph_views(
	struct starbook_mtl_dma_smm_authority_workspace *workspace)
{
	workspace->context.table_read_base = workspace->receipt.tables.base;
	if (vtd_translation_verify(&workspace->view, workspace->requesters,
		ARRAY_SIZE(workspace->requesters)))
		return -1;
	workspace->context.table_read_base =
		workspace->receipt.table_mirror.base;
	if (vtd_translation_verify(&workspace->view, workspace->requesters,
		ARRAY_SIZE(workspace->requesters)))
		return -1;
	workspace->context.table_read_base = workspace->receipt.tables.base;
	return 0;
}

enum cb_err starbook_mtl_dma_smm_authority_verify(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct smm_invocation_runtime_view *runtime_view,
	const void *retained, size_t retained_size,
	struct starbook_mtl_dma_smm_authority_workspace *workspace)
{
	struct starbook_mtl_dma_requester_authority first;
	struct starbook_mtl_dma_requester_binding current;

	if (!receipt || !runtime_view || !retained || !retained_size ||
	    !workspace ||
	    !protected_span(runtime_view, receipt, sizeof(*receipt)) ||
	    !protected_span(runtime_view, retained, retained_size) ||
	    !protected_span(runtime_view, workspace, sizeof(*workspace)) ||
	    spans_overlap(receipt, sizeof(*receipt), workspace,
		sizeof(*workspace)) ||
	    !span_contains(retained, retained_size, receipt, sizeof(*receipt)) ||
	    !span_contains(retained, retained_size, workspace,
		sizeof(*workspace)))
		return CB_ERR;
	memset(workspace, 0, sizeof(*workspace));
	workspace->receipt = *receipt;
	workspace->context = (struct starbook_mtl_dma_smm_authority_context) {
		.identity = AUTHORITY_CONTEXT_IDENTITY,
		.runtime_view = runtime_view,
		.workspace = workspace,
		.workspace_size = sizeof(*workspace),
		.retained = retained,
		.retained_size = retained_size,
		.table_logical_base = workspace->receipt.tables.base,
		.table_read_base = workspace->receipt.tables.base,
		.table_size = workspace->receipt.tables.size,
	};
	workspace->io = (struct starbook_mtl_dma_requester_authority_io) {
		.context = &workspace->context,
		.read_config32 = authority_read_config32,
		.read_binding = authority_read_binding,
	};
	workspace->view = (struct vtd_translation_view) {
		.context = &workspace->context,
		.physical_base = workspace->receipt.tables.base,
		.capacity_pages = workspace->receipt.tables.size /
			VTD_TRANSLATION_PAGE_SIZE,
		.used_pages = workspace->receipt.table_used_bytes /
			VTD_TRANSLATION_PAGE_SIZE,
		.read64 = authority_read_table64,
	};
	if (!contract_valid(workspace, runtime_view) ||
	    read_current_binding(runtime_view, &workspace->binding) != CB_SUCCESS ||
	    !smm_invocation_loader_instance_nonce_equal(
		workspace->binding.loader_instance_nonce,
		workspace->receipt.loader_instance_nonce) ||
	    workspace->binding.invocation_generation <
		workspace->receipt.invocation_generation ||
	    workspace->binding.loader_lifecycle !=
		workspace->receipt.loader_lifecycle ||
	    starbook_mtl_dma_requester_authority_derive(&workspace->io,
		&workspace->binding, &workspace->authority) != CB_SUCCESS ||
	    !authority_matches_receipt(&workspace->authority,
		&workspace->receipt))
		return CB_ERR;
	first = workspace->authority;
	for (size_t index = 0;
	     index < STARBOOK_MTL_DMA_REQUESTER_ROLE_COUNT; index++) {
		const struct starbook_mtl_dma_smm_range *arena =
			&workspace->receipt.arenas[index];

		if (!arena->size || (arena->size &
			(VTD_TRANSLATION_PAGE_SIZE - 1U)) ||
		    arena->size / VTD_TRANSLATION_PAGE_SIZE > UINT32_MAX)
			return CB_ERR;
		workspace->requesters[index] =
			(struct vtd_translation_requester) {
				.bdf = workspace->authority.requester[index].bdf,
				.domain = workspace->authority.requester[index].domain,
				.cpu_base = arena->base,
				.device_base = arena->base,
				.pages = (uint32_t)(arena->size /
					VTD_TRANSLATION_PAGE_SIZE),
			};
	}
	if (!contract_valid(workspace, runtime_view) ||
	    verify_graph_views(workspace) ||
	    read_current_binding(runtime_view, &current) != CB_SUCCESS ||
	    !binding_equal(&current, &workspace->binding) ||
	    starbook_mtl_dma_requester_authority_derive(&workspace->io,
		&workspace->binding, &workspace->authority) != CB_SUCCESS ||
	    memcmp(&first, &workspace->authority, sizeof(first)) ||
	    !authority_matches_receipt(&workspace->authority,
		&workspace->receipt) ||
	    !contract_valid(workspace, runtime_view) ||
	    verify_graph_views(workspace) ||
	    read_current_binding(runtime_view, &current) != CB_SUCCESS ||
	    !binding_equal(&current, &workspace->binding) ||
	    memcmp(receipt, &workspace->receipt, sizeof(*receipt)) ||
	    !contract_valid(workspace, runtime_view))
		return CB_ERR;
	workspace->context.table_read_base = workspace->receipt.tables.base;
	return CB_SUCCESS;
}

_Static_assert(STARBOOK_MTL_DMA_REQUESTER_ROLE_COUNT ==
	STARBOOK_MTL_DMA_SMM_ARENAS, "requester roles and DMA arenas diverged");
_Static_assert(!(AUTHORITY_ECAM_BASE & ((1ULL << 20) - 1U)),
	"ECAM base must be bus aligned");
_Static_assert(AUTHORITY_ECAM_BUSES && AUTHORITY_ECAM_BUSES <= 256U,
	"ECAM bus count is invalid");

#if ENV_TEST
enum cb_err starbook_mtl_dma_smm_authority_config_output_test(
	struct starbook_mtl_dma_smm_authority_workspace *workspace,
	uint32_t *output)
{
	return authority_read_config32(&workspace->context, 0,
		STARBOOK_MTL_PCH_XHCI_DEVFN, 0, output);
}

enum cb_err starbook_mtl_dma_smm_authority_binding_output_test(
	struct starbook_mtl_dma_smm_authority_workspace *workspace,
	struct starbook_mtl_dma_requester_binding *output)
{
	return authority_read_binding(&workspace->context, output);
}

int starbook_mtl_dma_smm_authority_table_output_test(
	struct starbook_mtl_dma_smm_authority_workspace *workspace,
	uint64_t *output)
{
	return authority_read_table64(&workspace->context,
		workspace->view.physical_base, output);
}
#endif
