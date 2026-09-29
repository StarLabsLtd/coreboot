/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_policy.h"

#include <commonlib/helpers.h>
#include <string.h>

#if ENV_TEST
#define PLATFORM_ECAM_BASE 0xc0000000ULL
#define PLATFORM_ECAM_BUSES 256U
#define PLATFORM_VTVC0_BASE 0xfc801000ULL
#define PLATFORM_GFX_BASE 0xfc800000ULL
#define PLATFORM_GFXVTBAR_REGISTER 0xfedc5410ULL
#define PLATFORM_IGD_BDF 0x10U
#define PLATFORM_IPU_BDF 0x28U
#define PLATFORM_VTBAR_ADDRESS_MASK 0x7ffffff000ULL
#else
#include <soc/iomap.h>
#include <soc/pci_devs.h>
#include <soc/systemagent.h>
#define PLATFORM_ECAM_BASE CONFIG_ECAM_MMCONF_BASE_ADDRESS
#define PLATFORM_ECAM_BUSES CONFIG_ECAM_MMCONF_BUS_NUMBER
#define PLATFORM_VTVC0_BASE VTVC0_BASE_ADDRESS
#define PLATFORM_GFX_BASE GFXVT_BASE_ADDRESS
#define PLATFORM_GFXVTBAR_REGISTER (MCH_BASE_ADDRESS + GFXVTBAR)
#define PLATFORM_IGD_BDF PCI_DEVFN_IGD
#define PLATFORM_IPU_BDF PCI_DEVFN_IPU
#define PLATFORM_VTBAR_ADDRESS_MASK VTBAR_MASK
#endif

#define PAGE_SIZE 0x1000U
#define VTD_GSTS 0x1cU
#define VTD_RTADDR 0x20U
#define VTD_PMEN 0x64U
#define VTD_RTPS (1U << 30)
#define VTD_TES (1U << 31)
#define VTD_PMR_BITS ((1U << 31) | 1U)
#define PCI_VENDOR_DEVICE 0U
#define DMA_PCI_COMMAND 4U
#define DMA_PCI_CLASS_REVISION 8U
#define PCI_BME (1U << 2)
#define DMA_VTBAR_ENABLED 1U
#ifndef STARBOOK_MTL_DMA_SMM_POINTER_MAX
#define STARBOOK_MTL_DMA_SMM_POINTER_MAX ((uintptr_t)-1)
#endif

static bool span64_valid(uint64_t base, uint64_t size)
{
	return base && size && base <= UINT64_MAX - (size - 1U);
}

static bool pointer_span_valid(uint64_t base, uint64_t size)
{
	return span64_valid(base, size) &&
		base <= STARBOOK_MTL_DMA_SMM_POINTER_MAX &&
		size - 1U <= STARBOOK_MTL_DMA_SMM_POINTER_MAX - (uintptr_t)base;
}

static bool page_range_valid(const struct starbook_mtl_dma_smm_range *range)
{
	return span64_valid(range->base, range->size) &&
		!(range->base & (PAGE_SIZE - 1U)) &&
		!(range->size & (PAGE_SIZE - 1U));
}

static bool overlaps(uint64_t base, uint64_t size,
	const struct starbook_mtl_dma_smm_range *range)
{
	return base <= range->base ? range->base - base < size :
		base - range->base < range->size;
}

static bool memory_overlaps(const void *first, size_t first_size,
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

static bool protected_span(
	const struct smm_invocation_runtime_view *runtime_view,
	const void *base, size_t size)
{
	return smm_invocation_runtime_range_is_protected(runtime_view, base,
		size) == CB_SUCCESS;
}

static enum cb_err read64_stable(
	const struct starbook_mtl_dma_smm_observer *observer,
	uint64_t address, uint64_t *value)
{
	uint32_t high_before;
	uint32_t high_after;
	uint32_t low;

	if (address > UINT64_MAX - sizeof(uint32_t) ||
	    observer->read32(observer->context, address + 4U, &high_before) !=
		CB_SUCCESS ||
	    observer->read32(observer->context, address, &low) != CB_SUCCESS ||
	    observer->read32(observer->context, address + 4U, &high_after) !=
		CB_SUCCESS || high_before != high_after)
		return CB_ERR;
	*value = (uint64_t)high_after << 32 | low;
	return CB_SUCCESS;
}

static enum cb_err verify_engine(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct starbook_mtl_dma_smm_observer *observer)
{
	uint32_t status;
	uint32_t pmen;
	uint64_t rtaddr;
	uint64_t gfxvtbar;

	if (observer->read32(observer->context,
		receipt->vtvc0_base + VTD_GSTS, &status) != CB_SUCCESS ||
	    read64_stable(observer, receipt->vtvc0_base + VTD_RTADDR,
		&rtaddr) != CB_SUCCESS ||
	    observer->read32(observer->context,
		receipt->vtvc0_base + VTD_PMEN, &pmen) != CB_SUCCESS ||
	    (status & (VTD_RTPS | VTD_TES)) != (VTD_RTPS | VTD_TES) ||
	    (pmen & VTD_PMR_BITS) || rtaddr != receipt->vtvc0_rtaddr)
		return CB_ERR;

	/* GFX is accepted only as an explicitly quiesced, inactive engine. */
	if (receipt->gfx_mode != STARBOOK_MTL_DMA_SMM_GFX_QUIESCED ||
	    observer->read32(observer->context,
		receipt->gfx_base + VTD_GSTS, &status) != CB_SUCCESS ||
	    observer->read32(observer->context,
		receipt->gfx_base + VTD_PMEN, &pmen) != CB_SUCCESS ||
	    (status & (VTD_RTPS | VTD_TES)) || (pmen & VTD_PMR_BITS) ||
	    read64_stable(observer, receipt->gfxvtbar_register,
		&gfxvtbar) != CB_SUCCESS ||
	    gfxvtbar != receipt->gfxvtbar_value ||
	    !(gfxvtbar & DMA_VTBAR_ENABLED) ||
	    (gfxvtbar & PLATFORM_VTBAR_ADDRESS_MASK) != receipt->gfx_base)
		return CB_ERR;
	return CB_SUCCESS;
}

static enum cb_err verify_pci(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct starbook_mtl_dma_smm_observer *observer)
{
	size_t expected = 0;
	bool igd_found = false;
	bool ipu_found = false;

	for (uint16_t bus = 0; bus < PLATFORM_ECAM_BUSES; bus++) {
		for (uint16_t devfn = 0; devfn < 256U; devfn++) {
			const uint16_t bdf = bus << 8 | devfn;
			const uint64_t address = PLATFORM_ECAM_BASE +
				((uint64_t)bus << 20) + ((uint64_t)devfn << 12);
			uint32_t class_revision;
			uint32_t command;
			uint32_t id;

			if (observer->read32(observer->context,
				address + PCI_VENDOR_DEVICE, &id) != CB_SUCCESS)
				return CB_ERR;
			if (id == UINT32_MAX)
				continue;
			if (expected >= receipt->function_count ||
			    observer->read32(observer->context,
				address + DMA_PCI_COMMAND, &command) != CB_SUCCESS ||
			    observer->read32(observer->context,
				address + DMA_PCI_CLASS_REVISION,
				&class_revision) != CB_SUCCESS ||
			    receipt->functions[expected].bdf != bdf ||
			    receipt->functions[expected].vendor != (uint16_t)id ||
			    receipt->functions[expected].device !=
				(uint16_t)(id >> 16) ||
			    receipt->functions[expected].command !=
				(uint16_t)command ||
			    receipt->functions[expected].class != class_revision >> 8 ||
			    (command & PCI_BME))
				return CB_ERR;
			igd_found |= bdf == PLATFORM_IGD_BDF;
			ipu_found |= bdf == PLATFORM_IPU_BDF;
			expected++;
		}
	}
	return expected == receipt->function_count && igd_found && ipu_found ?
		CB_SUCCESS : CB_ERR;
}

static bool receipt_valid(const struct starbook_mtl_dma_smm_receipt *receipt,
	uint64_t lifecycle_base, size_t lifecycle_size)
{
	const struct starbook_mtl_dma_smm_range *const ranges[] = {
		&receipt->handoff,
		&receipt->tables,
		&receipt->table_mirror,
		&receipt->arenas[0],
		&receipt->arenas[1],
		&receipt->arenas[2],
	};
	const uint64_t ecam_size = (uint64_t)PLATFORM_ECAM_BUSES << 20;

	if (receipt->revision != STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION ||
	    receipt->size != sizeof(*receipt) ||
	    receipt->bus_count != PLATFORM_ECAM_BUSES ||
	    !receipt->function_count ||
	    receipt->function_count > PCI_BME_QUIESCE_MAX_FUNCTIONS ||
	    receipt->ecam_base != PLATFORM_ECAM_BASE ||
	    receipt->vtvc0_base != PLATFORM_VTVC0_BASE ||
	    receipt->gfx_base != PLATFORM_GFX_BASE ||
	    receipt->gfxvtbar_register != PLATFORM_GFXVTBAR_REGISTER ||
	    receipt->gfx_mode != STARBOOK_MTL_DMA_SMM_GFX_QUIESCED ||
	    !span64_valid(PLATFORM_ECAM_BASE, ecam_size) ||
	    !pointer_span_valid(PLATFORM_ECAM_BASE, ecam_size) ||
	    receipt->vtvc0_base > UINT64_MAX - VTD_PMEN - sizeof(uint32_t) ||
	    !pointer_span_valid(receipt->vtvc0_base,
		VTD_PMEN + sizeof(uint32_t)) ||
	    receipt->gfx_base > UINT64_MAX - VTD_PMEN - sizeof(uint32_t) ||
	    !pointer_span_valid(receipt->gfx_base,
		VTD_PMEN + sizeof(uint32_t)) ||
	    receipt->gfxvtbar_register > UINT64_MAX - sizeof(uint64_t) ||
	    !pointer_span_valid(receipt->gfxvtbar_register, sizeof(uint64_t)) ||
	    !lifecycle_base || !lifecycle_size ||
	    lifecycle_base > UINT64_MAX - (lifecycle_size - 1U) ||
	    receipt->vtvc0_rtaddr != receipt->tables.base ||
	    receipt->gfxvtbar_value !=
		(PLATFORM_GFX_BASE | DMA_VTBAR_ENABLED) ||
	    !receipt->table_used_bytes ||
	    (receipt->table_used_bytes & (PAGE_SIZE - 1U)) ||
	    receipt->table_used_bytes > receipt->tables.size ||
	    receipt->tables.size != receipt->table_mirror.size ||
	    !pointer_span_valid(receipt->tables.base, receipt->tables.size) ||
	    !pointer_span_valid(receipt->table_mirror.base,
		receipt->table_mirror.size))
		return false;

	for (size_t index = 0; index < ARRAY_SIZE(ranges); index++) {
		if (!page_range_valid(ranges[index]) ||
		    overlaps(lifecycle_base, lifecycle_size, ranges[index]))
			return false;
		for (size_t prior = 0; prior < index; prior++)
			if (overlaps(ranges[index]->base, ranges[index]->size,
				ranges[prior]))
				return false;
	}
	for (size_t index = receipt->function_count;
	     index < PCI_BME_QUIESCE_MAX_FUNCTIONS; index++)
		if (memcmp(&receipt->functions[index],
			&(const struct pci_bme_quiesce_function){ 0 },
			sizeof(receipt->functions[index])))
			return false;
	return true;
}

static bool protected_contract_valid(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct starbook_mtl_dma_smm_observer *observer,
	const struct smm_invocation_runtime_view *runtime_view,
	struct starbook_mtl_dma_smm_workspace *workspace)
{
	if (!runtime_view || !observer || !workspace ||
	    !protected_span(runtime_view, receipt, sizeof(*receipt)) ||
	    !protected_span(runtime_view, observer, sizeof(*observer)) ||
	    !protected_span(runtime_view, workspace, sizeof(*workspace)))
		return false;
	if (!observer->context || !observer->context_size || !observer->read32 ||
	    !observer->sha256 ||
	    !protected_span(runtime_view,
		(const void *)(uintptr_t)observer->read32, 1U) ||
	    !protected_span(runtime_view,
		(const void *)(uintptr_t)observer->sha256, 1U) ||
	    !protected_span(runtime_view, observer->context,
		observer->context_size))
		return false;
	return !memory_overlaps(workspace, sizeof(*workspace), receipt,
			sizeof(*receipt)) &&
		!memory_overlaps(workspace, sizeof(*workspace), observer,
			sizeof(*observer)) &&
		!memory_overlaps(workspace, sizeof(*workspace), observer->context,
			observer->context_size);
}

static enum cb_err verify_table_copies(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct starbook_mtl_dma_smm_observer *observer, uint8_t digest[32])
{
	if (observer->sha256(observer->context,
		(const void *)(uintptr_t)receipt->tables.base,
		receipt->tables.size, digest) != CB_SUCCESS ||
	    memcmp(digest, receipt->table_digest, 32) ||
	    observer->sha256(observer->context,
		(const void *)(uintptr_t)receipt->table_mirror.base,
		receipt->table_mirror.size, digest) != CB_SUCCESS ||
	    memcmp(digest, receipt->table_digest, 32))
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_dma_smm_verify(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	uint64_t lifecycle_base, size_t lifecycle_size,
	const struct starbook_mtl_dma_smm_observer *observer,
	const struct smm_invocation_runtime_view *runtime_view,
	struct starbook_mtl_dma_smm_workspace *workspace)
{
	if (!receipt || !workspace ||
	    !protected_contract_valid(receipt, observer, runtime_view, workspace))
		return CB_ERR;

	memcpy(&workspace->receipt, receipt, sizeof(workspace->receipt));
	memcpy(&workspace->observer, observer, sizeof(workspace->observer));
	if (!receipt_valid(&workspace->receipt, lifecycle_base, lifecycle_size) ||
	    verify_engine(&workspace->receipt, &workspace->observer) !=
		CB_SUCCESS ||
	    verify_pci(&workspace->receipt, &workspace->observer) != CB_SUCCESS ||
	    verify_table_copies(&workspace->receipt, &workspace->observer,
		workspace->digest) != CB_SUCCESS ||
	    !protected_contract_valid(receipt, observer, runtime_view, workspace) ||
	    memcmp(receipt, &workspace->receipt, sizeof(*receipt)) ||
	    memcmp(observer, &workspace->observer, sizeof(*observer)) ||
	    !receipt_valid(&workspace->receipt, lifecycle_base, lifecycle_size) ||
	    verify_table_copies(&workspace->receipt, &workspace->observer,
		workspace->digest) != CB_SUCCESS ||
	    verify_engine(&workspace->receipt, &workspace->observer) !=
		CB_SUCCESS ||
	    verify_pci(&workspace->receipt, &workspace->observer) != CB_SUCCESS)
		return CB_ERR;
	return CB_SUCCESS;
}
