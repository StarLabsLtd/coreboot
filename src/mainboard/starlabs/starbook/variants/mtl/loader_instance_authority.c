/* SPDX-License-Identifier: GPL-2.0-only */

#include "loader_instance_authority.h"
#include "smm_invocation_loader_instance.h"

#if CONFIG(STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER)
#include <cpu/x86/smm_invocation_loader_composition.h>
#endif

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <boot/payload_mm_authvar_presence_handoff.h>
#include <soc/authvar_presence_boot_classifier.h>

#include "mor_cold_boot.h"
#include "mor_early_dma.h"

#if CONFIG(STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD)
#include "dma_guard.h"
#include "dma_live.h"
#endif

#define EARLY_DMA_SEAL_DOMAIN 0x6d746c2d65646d61ULL
#define MTL_PCI_COMMAND_MASTER (1U << 2)

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && !(base % alignment) &&
		base <= (uintptr_t)-1 - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

#if ENV_RAMSTAGE
static bool range_protected(uintptr_t base, size_t size, uint64_t limit)
{
	return size && base <= (uintptr_t)-1 - (size - 1U) &&
		(uint64_t)base < limit && size <= limit - (uint64_t)base;
}
#endif

static bool snapshot_valid(const struct pci_bme_quiesce_snapshot *snapshot)
{
	if (!snapshot || snapshot->failed || !snapshot->bus_count ||
	    snapshot->bus_count > 256U || !snapshot->count ||
	    snapshot->count > PCI_BME_QUIESCE_MAX_FUNCTIONS)
		return false;
	for (size_t index = 0; index < snapshot->count; index++) {
		const struct pci_bme_quiesce_function *function =
			&snapshot->functions[index];

		if ((function->command & MTL_PCI_COMMAND_MASTER) ||
		    (index && function[-1].bdf >= function->bdf) ||
		    (function->bdf >> 8) >= snapshot->bus_count)
			return false;
	}
	for (size_t index = snapshot->count;
	     index < PCI_BME_QUIESCE_MAX_FUNCTIONS; index++)
		if (memcmp(&snapshot->functions[index],
			&(const struct pci_bme_quiesce_function) { 0 },
			sizeof(snapshot->functions[index])))
			return false;
	return true;
}

static uint64_t mix_bytes(const uint8_t *bytes, size_t size, uint64_t value)
{
	for (size_t index = 0; index < size; index++) {
		value ^= bytes[index];
		value *= 0x100000001b3ULL;
		value ^= value >> 32;
	}
	return value;
}

static void build_identity(uint64_t generation,
	const struct pci_bme_quiesce_snapshot *snapshot, uint8_t identity[32])
{
	for (size_t lane = 0; lane < 4U; lane++) {
		uint64_t value = EARLY_DMA_SEAL_DOMAIN ^ generation ^
			(0x9e3779b97f4a7c15ULL * (lane + 1U));

		value = mix_bytes((const uint8_t *)snapshot, sizeof(*snapshot), value);
		value ^= value >> 33;
		value *= 0xff51afd7ed558ccdULL;
		value ^= value >> 33;
		memcpy(identity + lane * sizeof(value), &value, sizeof(value));
	}
}

static uint64_t payload_seal(
	const struct starbook_mtl_mor_early_dma_payload *payload)
{
	return mix_bytes((const uint8_t *)payload,
		offsetof(struct starbook_mtl_mor_early_dma_payload, seal),
		EARLY_DMA_SEAL_DOMAIN);
}

enum cb_err starbook_mtl_mor_early_dma_record_build(
	uint64_t generation, const struct pci_bme_quiesce_snapshot *snapshot,
	struct starbook_mtl_mor_early_dma_record *record)
{
	if (!object_valid(record, sizeof(*record), _Alignof(*record)) ||
	    !object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)) ||
	    !generation ||
	    (objects_overlap(record, sizeof(*record), snapshot, sizeof(*snapshot)) &&
	     snapshot != &record->mirror.pci) || !snapshot_valid(snapshot))
		return CB_ERR_ARG;
	record->primary.pci = *snapshot;
	record->primary.revision = STARBOOK_MTL_MOR_EARLY_DMA_REVISION;
	record->primary.size = sizeof(record->primary);
	record->primary.generation = generation;
	build_identity(generation, &record->primary.pci, record->primary.identity);
	record->primary.seal = payload_seal(&record->primary);
	record->mirror = record->primary;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_mor_early_dma_record_validate(
	const struct starbook_mtl_mor_early_dma_record *record,
	uint64_t generation, const struct pci_bme_quiesce_snapshot *snapshot,
	uint8_t identity[32])
{
	uint8_t expected_identity[32];

	if (!object_valid(record, sizeof(*record), _Alignof(*record)) ||
	    !object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)) ||
	    !object_valid(identity, 32, 1) || !generation ||
	    objects_overlap(identity, 32, record, sizeof(*record)) ||
	    objects_overlap(identity, 32, snapshot, sizeof(*snapshot)) ||
	    (objects_overlap(record, sizeof(*record), snapshot, sizeof(*snapshot)) &&
	     snapshot != &record->primary.pci) || !snapshot_valid(snapshot))
		return CB_ERR_ARG;
	build_identity(generation, snapshot, expected_identity);
	if (record->primary.revision != STARBOOK_MTL_MOR_EARLY_DMA_REVISION ||
	    record->primary.size != sizeof(record->primary) ||
	    record->primary.generation != generation ||
	    record->primary.seal != payload_seal(&record->primary) ||
	    memcmp(&record->primary, &record->mirror, sizeof(record->primary)) ||
	    memcmp(&record->primary.pci, snapshot, sizeof(*snapshot)) ||
	    memcmp(record->primary.identity, expected_identity,
		 sizeof(expected_identity)))
		return CB_ERR;
	memcpy(identity, expected_identity, sizeof(expected_identity));
	return CB_SUCCESS;
}

#if ENV_SEPARATE_ROMSTAGE || ENV_RAMSTAGE
#include <cbmem.h>
#include <commonlib/bsd/cbmem_id.h>
#if ENV_RAMSTAGE
#include <intelblocks/vtd.h>
#include <soc/iomap.h>
#include <soc/ramstage.h>
#include <soc/vtd.h>
#endif

#if ENV_RAMSTAGE
static enum cb_err protected_limit(uint64_t *exclusive_limit)
{
	const uintptr_t base = soc_vtd_iop_base();
	const uint32_t enable = vtd_read32(base, PMEN_REG);
	const uint32_t low_base = vtd_read32(base, PLMBASE_REG);
	const uint32_t low_limit = vtd_read32(base, PLMLIMIT_REG);

	if (!exclusive_limit || !base || low_base || low_limit == UINT32_MAX ||
	    (enable & (PMEN_EPM | PMEN_PRS)) != (PMEN_EPM | PMEN_PRS))
		return CB_ERR;
	*exclusive_limit = (uint64_t)low_limit + 1U;
	return CB_SUCCESS;
}
#endif

#if ENV_SEPARATE_ROMSTAGE
#if CONFIG(STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD)
static void allocate_early_record(int is_recovery)
{
	struct starbook_mtl_mor_early_dma_record *record;

	if (!is_recovery) {
		record = cbmem_add(CBMEM_ID_MTL_MOR_EARLY_DMA, sizeof(*record));
		if (record)
			memset(record, 0, sizeof(*record));
	}
}
CBMEM_CREATION_HOOK(allocate_early_record);
#endif
#endif
#endif

#if ENV_RAMSTAGE
static struct {
	struct starbook_mtl_loader_instance_fanout fanout;
	struct starbook_mtl_loader_instance_owner owner;
	struct pci_bme_quiesce_snapshot source_snapshot;
	uint32_t lifecycle;
	uint32_t lifecycle_inverse;
	uint32_t presence_boot_class;
	uint32_t presence_boot_class_inverse;
} authority_workspace __aligned(8);

#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_COLD_CLASSIFICATION)
bool platform_payload_mm_authvar_presence_handoff_cold_boot(void)
{
	uint32_t lifecycle;
	const uint32_t classification = authority_workspace.presence_boot_class;

	return mainboard_loader_instance_authority_lifecycle(&lifecycle) == CB_SUCCESS &&
		lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD &&
		classification == ~authority_workspace.presence_boot_class_inverse &&
		classification == MTL_AUTHVAR_PRESENCE_BOOT_COLD;
}
#endif

enum cb_err mainboard_loader_instance_authority_lifecycle(uint32_t *lifecycle)
{
	const uint32_t value = authority_workspace.lifecycle;

	if (!lifecycle || value != ~authority_workspace.lifecycle_inverse ||
	    (value != SMM_INVOCATION_LOADER_NON_S3_LOAD &&
	     value != SMM_INVOCATION_LOADER_S3_RELOAD))
		return CB_ERR;
	*lifecycle = value;
	return CB_SUCCESS;
}

enum cb_err mainboard_loader_instance_authority_prepare(void)
{
	struct smm_invocation_loader_instance_nonce nonce = { 0 };
	uint32_t lifecycle;
	uint32_t presence_boot_class;
	uint64_t initial_limit;
	uint64_t final_limit;
	uint64_t abort_limit;

#if CONFIG(STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD)
	struct starbook_mtl_mor_early_dma_record *record = NULL;
	const struct cbmem_entry *entry = NULL;
	uint8_t identity[32];
	bool early_record_safe = false;
#endif

	authority_workspace.lifecycle = 0;
	authority_workspace.lifecycle_inverse = 0;
	authority_workspace.presence_boot_class = MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN;
	authority_workspace.presence_boot_class_inverse = 0;
	if (protected_limit(&initial_limit) != CB_SUCCESS ||
	    !range_protected((uintptr_t)&authority_workspace,
		sizeof(authority_workspace), initial_limit) ||
	    starbook_mtl_loader_instance_fanout_begin(&authority_workspace.fanout,
		(uintptr_t)&authority_workspace, sizeof(authority_workspace),
		initial_limit, &authority_workspace.owner) != CB_SUCCESS)
		return CB_ERR;
	if (starbook_mtl_loader_instance_source_ramstage_take(&lifecycle, &nonce,
		&authority_workspace.source_snapshot, &presence_boot_class) != CB_SUCCESS)
		goto fail;
#if CONFIG(STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD)
	{
		entry = cbmem_entry_find(CBMEM_ID_MTL_MOR_EARLY_DMA);
		record = cbmem_find(CBMEM_ID_MTL_MOR_EARLY_DMA);
		if (!entry || !record || cbmem_entry_start(entry) != record ||
		    cbmem_entry_size(entry) != sizeof(*record) ||
		    !range_protected((uintptr_t)record, sizeof(*record), initial_limit))
			goto fail;
		early_record_safe = true;
		memset(record, 0, sizeof(*record));
		if (lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD &&
		    (starbook_mtl_mor_early_dma_record_build(nonce.low,
			&authority_workspace.source_snapshot, record) != CB_SUCCESS ||
		     starbook_mtl_dma_live_prepare_early(&record->primary.pci) ||
		     starbook_mtl_mor_early_dma_record_validate(record, nonce.low,
			&record->primary.pci, identity) != CB_SUCCESS ||
		     starbook_mtl_dma_guard_seed(nonce.low, identity) != CB_SUCCESS))
			goto fail;
	}
#endif
	memset(&authority_workspace.source_snapshot, 0,
		sizeof(authority_workspace.source_snapshot));
	if (protected_limit(&final_limit) != CB_SUCCESS ||
	    final_limit != initial_limit ||
	    !range_protected((uintptr_t)&authority_workspace,
		sizeof(authority_workspace), final_limit) ||
	    starbook_mtl_loader_instance_fanout_commit(&authority_workspace.fanout,
		(uintptr_t)&authority_workspace, sizeof(authority_workspace),
		final_limit, lifecycle, nonce,
		&authority_workspace.owner) != CB_SUCCESS)
		goto fail;
	authority_workspace.lifecycle = lifecycle;
	authority_workspace.lifecycle_inverse = ~lifecycle;
	authority_workspace.presence_boot_class = presence_boot_class;
	authority_workspace.presence_boot_class_inverse = ~presence_boot_class;
	memset(&nonce, 0, sizeof(nonce));
	return CB_SUCCESS;
fail:
	authority_workspace.lifecycle = 0;
	authority_workspace.lifecycle_inverse = 0;
	authority_workspace.presence_boot_class = MTL_AUTHVAR_PRESENCE_BOOT_UNKNOWN;
	authority_workspace.presence_boot_class_inverse = 0;
#if CONFIG(STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD)
	if (early_record_safe)
		memset(record, 0, sizeof(*record));
#endif
	memset(&authority_workspace.source_snapshot, 0,
		sizeof(authority_workspace.source_snapshot));
	if (protected_limit(&abort_limit) != CB_SUCCESS)
		abort_limit = 0;
	starbook_mtl_loader_instance_fanout_abort(&authority_workspace.fanout,
		(uintptr_t)&authority_workspace, sizeof(authority_workspace),
		abort_limit,
		&authority_workspace.owner);
	memset(&nonce, 0, sizeof(nonce));
	return CB_ERR;
}

#if CONFIG(STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD)
enum cb_err starbook_mtl_mor_early_dma_classify(uint32_t *boot_kind,
	uint64_t *generation, struct starbook_mtl_dma_guard_snapshot *guard)
{
	uint32_t lifecycle;
	uint64_t limit;

	if (!object_valid(boot_kind, sizeof(*boot_kind), _Alignof(*boot_kind)) ||
	    !object_valid(generation, sizeof(*generation), _Alignof(*generation)) ||
	    !object_valid(guard, sizeof(*guard), _Alignof(*guard)) ||
	    objects_overlap(boot_kind, sizeof(*boot_kind), generation,
		 sizeof(*generation)) ||
	    objects_overlap(boot_kind, sizeof(*boot_kind), guard, sizeof(*guard)) ||
	    objects_overlap(generation, sizeof(*generation), guard, sizeof(*guard)))
		return CB_ERR_ARG;
	*boot_kind = STARBOOK_MTL_MOR_BOOT_UNKNOWN;
	*generation = 0;
	memset(guard, 0, sizeof(*guard));
	if (protected_limit(&limit) != CB_SUCCESS ||
	    !range_protected((uintptr_t)&authority_workspace,
		sizeof(authority_workspace), limit) ||
	    starbook_mtl_loader_instance_fanout_read_legacy(
		&authority_workspace.fanout, (uintptr_t)&authority_workspace,
		sizeof(authority_workspace), limit, &lifecycle, generation) != CB_SUCCESS)
		return CB_ERR;
	*boot_kind = lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD ?
		STARBOOK_MTL_MOR_BOOT_S3 : STARBOOK_MTL_MOR_BOOT_COLD;
	if (*boot_kind == STARBOOK_MTL_MOR_BOOT_COLD &&
	    (starbook_mtl_dma_guard_prepare(guard) != CB_SUCCESS ||
	     guard->generation != *generation)) {
		memset(guard, 0, sizeof(*guard));
		*boot_kind = STARBOOK_MTL_MOR_BOOT_UNKNOWN;
		*generation = 0;
		return CB_ERR;
	}
	return CB_SUCCESS;
}
#endif

#if CONFIG(STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER)
enum cb_err smm_invocation_platform_loader_instance_take(
	struct smm_invocation_loader_instance_seed *seed)
{
	/* FSP-S has returned: establish a fresh all-function BME boundary here. */
	return starbook_mtl_loader_instance_fanout_take_requiesced(
		&authority_workspace.fanout, (uintptr_t)&authority_workspace,
		sizeof(authority_workspace), seed,
		starbook_mtl_loader_instance_source_ramstage_requiesce);
}
#endif
#endif
