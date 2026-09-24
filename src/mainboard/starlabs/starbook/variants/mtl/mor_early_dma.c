/* SPDX-License-Identifier: GPL-2.0-only */

#include "mor_early_dma.h"

#include <rules.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "dma_guard.h"
#include "dma_live.h"
#include "mor_cold_boot.h"

#define EARLY_DMA_SEAL_DOMAIN 0x6d746c2d65646d61ULL
#define PCI_COMMAND_MASTER (1U << 2)

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

static bool snapshot_valid(const struct pci_bme_quiesce_snapshot *snapshot)
{
	if (!snapshot || snapshot->failed || !snapshot->bus_count ||
	    snapshot->bus_count > 256U || !snapshot->count ||
	    snapshot->count > PCI_BME_QUIESCE_MAX_FUNCTIONS)
		return false;
	for (size_t index = 0; index < snapshot->count; index++) {
		const struct pci_bme_quiesce_function *function =
			&snapshot->functions[index];

		if ((function->command & PCI_COMMAND_MASTER) ||
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

#if ENV_RAMSTAGE
enum cb_err mainboard_mor_early_dma_prepare(void)
{
	struct starbook_mtl_mor_early_dma_record *record =
		cbmem_find(CBMEM_ID_MTL_MOR_EARLY_DMA);
	uint8_t identity[32];
	uint64_t generation = 0;
	uint64_t limit;
	const struct cbmem_entry *entry =
		cbmem_entry_find(CBMEM_ID_MTL_MOR_EARLY_DMA);

	if (!entry || !record || cbmem_entry_start(entry) != record ||
	    cbmem_entry_size(entry) != sizeof(*record) ||
	    starbook_mtl_mor_cold_ramstage_consume_snapshot(&generation,
		&record->mirror.pci) != CB_SUCCESS ||
	    protected_limit(&limit) != CB_SUCCESS ||
	    (uintptr_t)record >= limit ||
	    sizeof(*record) > limit - (uintptr_t)record ||
	    starbook_mtl_mor_early_dma_record_build(generation,
		&record->mirror.pci, record) != CB_SUCCESS ||
	    starbook_mtl_dma_live_prepare_early(&record->primary.pci) ||
	    starbook_mtl_mor_early_dma_record_validate(record, generation,
		&record->primary.pci, identity) != CB_SUCCESS ||
	    starbook_mtl_dma_guard_seed(generation, identity) != CB_SUCCESS)
		return CB_ERR;
	return CB_SUCCESS;
}
#endif
