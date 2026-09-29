/* SPDX-License-Identifier: GPL-2.0-only */

#include "dma_smm_policy.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define ECAM 0xc0000000ULL
#define VTVC0 0xfc801000ULL
#define GFX 0xfc800000ULL
#define GFXBAR 0xfedc5410ULL

struct smm_invocation_runtime_view {
	uint8_t opaque;
};

static const struct smm_invocation_runtime_view runtime_view;
static const void *unprotected;

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base,
	size_t size)
{
	const uintptr_t address = (uintptr_t)base;

	if (view != &runtime_view || !base || !size ||
	    address > (uintptr_t)-1 - (size - 1U))
		return CB_ERR_ARG;
	return base == unprotected ? CB_ERR : CB_SUCCESS;
}

struct context {
	struct starbook_mtl_dma_smm_receipt *source;
	struct starbook_mtl_dma_smm_observer *observer;
	bool table_bad;
	bool gfx_active;
	bool pci_bme;
	bool live_drift;
	bool live_aba;
	bool pci_drift;
	bool mutate_receipt;
	bool mutate_observer;
};

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		abort();
}

static enum cb_err read32(void *opaque, uint64_t address, uint32_t *value)
{
	struct context *context = opaque;

	if (context->mutate_receipt) {
		context->source->table_used_bytes ^= 0x1000U;
		context->mutate_receipt = false;
	}
	if (context->mutate_observer) {
		context->observer->context_size++;
		context->mutate_observer = false;
	}
	*value = 0;
	if (address == VTVC0 + 0x1c)
		*value = (1U << 30) | (1U << 31);
	else if (address == VTVC0 + 0x20)
		*value = 0x200000;
	else if (address == VTVC0 + 0x24)
		*value = 0;
	else if (address == VTVC0 + 0x64)
		*value = 0;
	else if (address == GFX + 0x1c) {
		*value = context->gfx_active ? 1U << 31 : 0;
		if (context->live_aba && context->gfx_active) {
			context->gfx_active = false;
			context->live_aba = false;
		}
	} else if (address == GFX + 0x64)
		*value = 0;
	else if (address == GFXBAR)
		*value = GFX | 1U;
	else if (address == GFXBAR + 4U)
		*value = 0;
	else if (address >= ECAM && address < ECAM + (1ULL << 28)) {
		const uint16_t bdf = (address - ECAM) >> 12;
		const uint16_t offset = address & 0xfffU;

		if (bdf != 0x10 && bdf != 0x28)
			*value = UINT32_MAX;
		else if (!offset)
			*value = bdf == 0x10 ? 0x11118086U : 0x22228086U;
		else if (offset == 4U)
			*value = 3U | (context->pci_bme ? 4U : 0U);
		else if (offset == 8U)
			*value = bdf == 0x10 ? 0x03000000U : 0x04800000U;
	} else {
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static enum cb_err sha256(void *opaque, const void *data, size_t size,
	uint8_t digest[32])
{
	struct context *context = opaque;

	if (size != 0x4000U)
		return CB_ERR;
	if (context->live_drift || context->live_aba) {
		context->gfx_active = true;
		context->live_drift = false;
	}
	if (context->pci_drift) {
		context->pci_bme = true;
		context->pci_drift = false;
	}
	memset(digest,
		data == (void *)(uintptr_t)context->source->tables.base &&
		context->table_bad ? 0x5b : 0x5a, 32);
	return CB_SUCCESS;
}

static enum cb_err verify_translation(void *opaque,
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct smm_invocation_runtime_view *view)
{
	struct context *context = opaque;

	return context && receipt && view == &runtime_view ?
		CB_SUCCESS : CB_ERR;
}

static struct starbook_mtl_dma_smm_receipt valid_receipt(void)
{
	struct starbook_mtl_dma_smm_receipt receipt = {
		.revision = STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION,
		.size = sizeof(receipt),
		.bus_count = 256,
		.function_count = 2,
		.gfx_mode = STARBOOK_MTL_DMA_SMM_GFX_QUIESCED,
		.ecam_base = ECAM,
		.vtvc0_base = VTVC0,
		.vtvc0_rtaddr = 0x200000,
		.gfx_base = GFX,
		.gfxvtbar_register = GFXBAR,
		.gfxvtbar_value = GFX | 1U,
		.handoff = { 0x100000, 0x1000 },
		.tables = { 0x200000, 0x4000 },
		.table_mirror = { 0x300000, 0x4000 },
		.arenas = {
			{ 0x400000, 0x1000 },
			{ 0x500000, 0x1000 },
			{ 0x600000, 0x1000 },
		},
		.table_used_bytes = 0x3000,
		.table_digest = { [0 ... 31] = 0x5a },
		.loader_instance_nonce = { .low = 1 },
		.invocation_generation = 1,
		.loader_lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.functions = {
			{ .bdf = 0x10, .vendor = 0x8086, .device = 0x1111,
			  .command = 3, .class = 0x030000 },
			{ .bdf = 0x28, .vendor = 0x8086, .device = 0x2222,
			  .command = 3, .class = 0x048000 },
		},
	};

	return receipt;
}

int main(int argc, char **argv)
{
	struct starbook_mtl_dma_smm_receipt receipt = valid_receipt();
	struct starbook_mtl_dma_smm_workspace workspace;
	struct context context = { .source = &receipt };
	struct starbook_mtl_dma_smm_observer observer = {
		.context = &context,
		.context_size = sizeof(context),
		.read32 = read32,
		.sha256 = sha256,
		.verify_translation = verify_translation,
	};
	const struct smm_invocation_runtime_view *view = &runtime_view;
	uint64_t lifecycle_base = 0x700000;

	context.observer = &observer;
	assert(argc == 2);
	if (!strcmp(argv[1], "valid")) {
		/* No mutation. */
	} else if (!strcmp(argv[1], "lifecycle-arena")) {
		receipt.arenas[1].base = lifecycle_base;
	} else if (!strcmp(argv[1], "rtaddr-mode")) {
		receipt.vtvc0_rtaddr |= 1U;
	} else if (!strcmp(argv[1], "partial-bus")) {
		receipt.bus_count = 1;
	} else if (!strcmp(argv[1], "bme")) {
		receipt.functions[0].command |= 4U;
	} else if (!strcmp(argv[1], "missing-igd")) {
		receipt.functions[0].bdf = 0x11;
	} else if (!strcmp(argv[1], "tail")) {
		receipt.functions[511].vendor = 1;
	} else if (!strcmp(argv[1], "digest")) {
		context.table_bad = true;
	} else if (!strcmp(argv[1], "gfx-active")) {
		context.gfx_active = true;
	} else if (!strcmp(argv[1], "gfx-mode")) {
		receipt.gfx_mode = 2;
	} else if (!strcmp(argv[1], "live-drift")) {
		context.live_drift = true;
	} else if (!strcmp(argv[1], "live-aba")) {
		context.live_aba = true;
	} else if (!strcmp(argv[1], "pci-drift")) {
		context.pci_drift = true;
	} else if (!strcmp(argv[1], "misaligned-range")) {
		receipt.handoff.base++;
	} else if (!strcmp(argv[1], "misaligned-size")) {
		receipt.arenas[0].size++;
	} else if (!strcmp(argv[1], "misaligned-used")) {
		receipt.table_used_bytes++;
	} else if (!strcmp(argv[1], "range-overflow")) {
		receipt.arenas[2].base = UINT64_MAX - 0x7ffU;
	} else if (!strcmp(argv[1], "null-table")) {
		receipt.tables.base = 0;
		receipt.vtvc0_rtaddr = 0;
	} else if (!strcmp(argv[1], "above-4g")) {
		receipt.tables.base = 0x100000000ULL;
		receipt.vtvc0_rtaddr = receipt.tables.base;
	} else if (!strcmp(argv[1], "huge-table")) {
		receipt.tables.size = UINT64_MAX;
		receipt.table_mirror.size = UINT64_MAX;
	} else if (!strcmp(argv[1], "mmio-table")) {
		receipt.tables.base = ECAM;
		receipt.vtvc0_rtaddr = receipt.tables.base;
	} else if (!strcmp(argv[1], "forbidden-table")) {
		receipt.tables.base = lifecycle_base;
		receipt.vtvc0_rtaddr = receipt.tables.base;
	} else if (!strcmp(argv[1], "zero-loader-nonce")) {
		receipt.loader_instance_nonce.low = 0;
	} else if (!strcmp(argv[1], "zero-generation")) {
		receipt.invocation_generation = 0;
	} else if (!strcmp(argv[1], "bad-lifecycle")) {
		receipt.loader_lifecycle = 3;
	} else if (!strcmp(argv[1], "identity-reserved")) {
		receipt.identity_reserved = 1;
	} else if (!strcmp(argv[1], "pointer32")) {
		receipt.tables.base = 0x100000000ULL;
		receipt.vtvc0_rtaddr = receipt.tables.base;
	} else if (!strcmp(argv[1], "unprotected-receipt")) {
		unprotected = &receipt;
	} else if (!strcmp(argv[1], "unprotected-read")) {
		unprotected = (const void *)(uintptr_t)read32;
	} else if (!strcmp(argv[1], "unprotected-sha")) {
		unprotected = (const void *)(uintptr_t)sha256;
	} else if (!strcmp(argv[1], "unprotected-translation")) {
		unprotected = (const void *)(uintptr_t)verify_translation;
	} else if (!strcmp(argv[1], "unprotected-context")) {
		unprotected = &context;
	} else if (!strcmp(argv[1], "unprotected-workspace")) {
		unprotected = &workspace;
	} else if (!strcmp(argv[1], "wrong-runtime-view")) {
		view = (const void *)1;
	} else if (!strcmp(argv[1], "workspace-context-alias")) {
		observer.context = &workspace;
		observer.context_size = sizeof(workspace);
	} else if (!strcmp(argv[1], "receipt-toctou")) {
		context.mutate_receipt = true;
	} else if (!strcmp(argv[1], "observer-toctou")) {
		context.mutate_observer = true;
	} else {
		return 2;
	}

	if (!strcmp(argv[1], "valid")) {
		assert(starbook_mtl_dma_smm_verify(&receipt, lifecycle_base,
			0x1000, &observer, view, &workspace) == CB_SUCCESS);
	} else {
		assert(starbook_mtl_dma_smm_verify(&receipt, lifecycle_base,
			0x1000, &observer, view, &workspace) != CB_SUCCESS);
	}
	if (!strcmp(argv[1], "live-aba"))
		assert(!context.gfx_active);
	return 0;
}
