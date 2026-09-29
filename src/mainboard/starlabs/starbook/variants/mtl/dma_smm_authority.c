/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_authority.h"

#include <commonlib/helpers.h>
#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <device/mmio.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "MTL DMA SMM authority is SMM-only"
#endif

#if ENV_TEST
#ifndef STARBOOK_MTL_DMA_SMM_TEST_ECAM_BASE
#define PLATFORM_ECAM_BASE 0xc0000000ULL
#else
#define PLATFORM_ECAM_BASE STARBOOK_MTL_DMA_SMM_TEST_ECAM_BASE
#endif
#ifndef STARBOOK_MTL_DMA_SMM_TEST_ECAM_BUSES
#define PLATFORM_ECAM_BUSES 256U
#else
#define PLATFORM_ECAM_BUSES STARBOOK_MTL_DMA_SMM_TEST_ECAM_BUSES
#endif
#else
#include <soc/iomap.h>
#define PLATFORM_ECAM_BASE CONFIG_ECAM_MMCONF_BASE_ADDRESS
#define PLATFORM_ECAM_BUSES CONFIG_ECAM_MMCONF_BUS_NUMBER
#endif

#define AUTHORITY_IDENTITY 0x4d544c444d414155ULL

static bool protected_span(
	const struct smm_invocation_runtime_view *runtime_view,
	const void *base, size_t size)
{
	return smm_invocation_runtime_range_is_protected(runtime_view, base,
		size) == CB_SUCCESS;
}

static enum cb_err binding_read(void *context,
	struct starbook_mtl_dma_requester_binding *binding)
{
	struct starbook_mtl_dma_smm_authority_workspace *workspace = context;
	struct smm_invocation_runtime_binding runtime_binding;
	struct smm_invocation_loader_instance instance;
	struct starbook_mtl_dma_requester_binding value;

	if (!workspace || workspace->identity != AUTHORITY_IDENTITY || !binding ||
	    smm_invocation_runtime_binding_get(&runtime_binding) != CB_SUCCESS ||
	    smm_invocation_loader_instance_read(runtime_binding.instance,
		&instance) != CB_SUCCESS || !runtime_binding.evidence->generation ||
	    !smm_invocation_loader_instance_nonce_equal(
		instance.loader_instance_nonce,
		runtime_binding.evidence->loader_instance_nonce) ||
	    instance.lifecycle != runtime_binding.evidence->loader_lifecycle)
		return CB_ERR;
	value = (struct starbook_mtl_dma_requester_binding) {
		.loader_instance_nonce = instance.loader_instance_nonce,
		.invocation_generation = runtime_binding.evidence->generation,
		.loader_lifecycle = instance.lifecycle,
	};
	*binding = value;
	return CB_SUCCESS;
}

static enum cb_err ecam_read32(void *context, uint8_t bus, uint8_t devfn,
	uint16_t offset, uint32_t *value)
{
	struct starbook_mtl_dma_smm_authority_workspace *workspace = context;
	const size_t bus_number = bus;
	uint64_t address;
	uint32_t first;
	uint32_t second;

	if (!workspace || workspace->identity != AUTHORITY_IDENTITY || !value ||
	    bus_number >= PLATFORM_ECAM_BUSES || (offset & 3U) ||
	    offset > 0xffcU)
		return CB_ERR;
	address = PLATFORM_ECAM_BASE + ((uint64_t)bus << 20) +
		((uint64_t)devfn << 12) + offset;
	if (address > (uintptr_t)-1 - sizeof(first))
		return CB_ERR;
	first = read32p((uintptr_t)address);
	second = read32p((uintptr_t)address);
	if (first != second)
		return CB_ERR;
	*value = second;
	return CB_SUCCESS;
}

static enum cb_err table_read64(void *context, uint64_t address,
	uint64_t *value)
{
	struct starbook_mtl_dma_smm_authority_workspace *workspace = context;
	uint32_t high_before;
	uint32_t high_after;
	uint32_t low;
	uint64_t offset;

	if (!workspace || workspace->identity != AUTHORITY_IDENTITY || !value ||
	    (address & 7U) || address < workspace->table.base)
		return CB_ERR;
	offset = address - workspace->table.base;
	if (offset > workspace->table.size ||
	    workspace->table.size - offset < sizeof(*value) ||
	    address > (uintptr_t)-1 - sizeof(*value))
		return CB_ERR;
	high_before = read32p((uintptr_t)address + 4U);
	low = read32p((uintptr_t)address);
	high_after = read32p((uintptr_t)address + 4U);
	if (high_before != high_after)
		return CB_ERR;
	*value = (uint64_t)high_after << 32 | low;
	return CB_SUCCESS;
}

static bool workspace_protected(
	struct starbook_mtl_dma_smm_authority_workspace *workspace,
	const struct smm_invocation_runtime_view *runtime_view)
{
	return workspace && runtime_view &&
		protected_span(runtime_view, workspace, sizeof(*workspace)) &&
		protected_span(runtime_view,
			(const void *)(uintptr_t)ecam_read32, 1U) &&
		protected_span(runtime_view,
			(const void *)(uintptr_t)binding_read, 1U) &&
		protected_span(runtime_view,
			(const void *)(uintptr_t)table_read64, 1U);
}

enum cb_err starbook_mtl_dma_smm_authority_verify(void *context,
	const struct starbook_mtl_dma_smm_receipt *receipt)
{
	struct starbook_mtl_dma_smm_authority_workspace *workspace = context;
	const struct smm_invocation_runtime_view *runtime_view;
	struct starbook_mtl_dma_requester_binding before;
	struct starbook_mtl_dma_requester_binding after;
	struct starbook_mtl_dma_requester_authority authority;
	struct vtd_translation_requester requesters[
		STARBOOK_MTL_DMA_REQUESTER_ROLE_COUNT];
	struct vtd_translation_view view;

	if (!workspace || !receipt ||
	    smm_invocation_runtime_view_get(&runtime_view) != CB_SUCCESS ||
	    !workspace_protected(workspace, runtime_view) ||
	    !protected_span(runtime_view, receipt, sizeof(*receipt)))
		return CB_ERR;
	workspace->identity = AUTHORITY_IDENTITY;
	if (binding_read(workspace, &before) != CB_SUCCESS ||
	    !smm_invocation_loader_instance_nonce_equal(
		before.loader_instance_nonce, receipt->loader_instance_nonce) ||
	    before.invocation_generation != receipt->invocation_generation ||
	    before.loader_lifecycle != receipt->loader_lifecycle)
		return CB_ERR;

	workspace->runtime_view = runtime_view;
	workspace->binding = before;
	workspace->table = receipt->tables;
	workspace->io = (struct starbook_mtl_dma_requester_authority_io) {
		.context = workspace,
		.read_config32 = ecam_read32,
		.read_binding = binding_read,
	};
	if (!workspace_protected(workspace, runtime_view) ||
	    starbook_mtl_dma_requester_authority_derive(&workspace->io,
		&workspace->binding, &authority) != CB_SUCCESS)
		return CB_ERR;
	for (size_t index = 0; index < ARRAY_SIZE(requesters); index++) {
		if (!receipt->arenas[index].size ||
		    receipt->arenas[index].size % VTD_TRANSLATION_PAGE_SIZE ||
		    receipt->arenas[index].size / VTD_TRANSLATION_PAGE_SIZE >
			UINT32_MAX)
			return CB_ERR;
		requesters[index] = (struct vtd_translation_requester) {
			.bdf = authority.requester[index].bdf,
			.domain = authority.requester[index].domain,
			.cpu_base = receipt->arenas[index].base,
			.device_base = receipt->arenas[index].base,
			.pages = (uint32_t)(receipt->arenas[index].size /
				VTD_TRANSLATION_PAGE_SIZE),
		};
	}
	if (receipt->tables.size / VTD_TRANSLATION_PAGE_SIZE > SIZE_MAX ||
	    receipt->table_used_bytes / VTD_TRANSLATION_PAGE_SIZE > SIZE_MAX)
		return CB_ERR;
	view = (struct vtd_translation_view) {
		.context = workspace,
		.physical_base = receipt->tables.base,
		.capacity_pages = (size_t)(receipt->tables.size /
			VTD_TRANSLATION_PAGE_SIZE),
		.used_pages = (size_t)(receipt->table_used_bytes /
			VTD_TRANSLATION_PAGE_SIZE),
		.read64 = table_read64,
	};
	workspace->authority = authority;
	memcpy(workspace->requesters, requesters, sizeof(requesters));
	workspace->view = view;
	if (!workspace_protected(workspace, runtime_view) ||
	    memcmp(&authority, &workspace->authority, sizeof(authority)) ||
	    memcmp(requesters, workspace->requesters, sizeof(requesters)) ||
	    memcmp(&view, &workspace->view, sizeof(view)) ||
	    vtd_translation_verify(&workspace->view, workspace->requesters,
		ARRAY_SIZE(workspace->requesters)) ||
	    binding_read(workspace, &after) != CB_SUCCESS ||
	    memcmp(&before, &after, sizeof(before)) ||
	    !workspace_protected(workspace, runtime_view) ||
	    memcmp(&authority, &workspace->authority, sizeof(authority)) ||
	    memcmp(requesters, workspace->requesters, sizeof(requesters)) ||
	    memcmp(&view, &workspace->view, sizeof(view)))
		return CB_ERR;
	return CB_SUCCESS;
}
