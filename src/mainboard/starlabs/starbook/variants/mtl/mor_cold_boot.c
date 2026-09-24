/* SPDX-License-Identifier: GPL-2.0-only */

#include "mor_cold_boot.h"

#include <rules.h>
#include <stdbool.h>
#include <string.h>

#if ENV_SEPARATE_ROMSTAGE || ENV_RAMSTAGE
#include <cbmem.h>
#include <commonlib/bsd/cbmem_id.h>
#include <device/mmio.h>
#include <device/pci_bme_quiesce.h>
#include <intelblocks/vtd.h>
#include <soc/iomap.h>
#include <soc/vtd.h>
#endif

#if ENV_SEPARATE_ROMSTAGE
#include <random.h>
#endif

#define MTL_MOR_COLD_SEAL_DOMAIN 0x6d746c2d636f6c64ULL

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && !(base % alignment) && base <= (uintptr_t)-1 - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	return first_base < second_base + second_size &&
		second_base < first_base + first_size;
}

static bool object_contains(const void *object, size_t size, const void *pointer)
{
	const uintptr_t base = (uintptr_t)object;
	const uintptr_t address = (uintptr_t)pointer;

	return pointer && address >= base && address < base + size;
}

static bool range_protected(uintptr_t base, size_t size, uint64_t limit)
{
	return size && base <= (uintptr_t)-1 - (size - 1U) &&
		(uint64_t)base < limit && size <= limit - (uint64_t)base;
}

static uint64_t payload_seal(uint32_t boot_kind, uint64_t generation)
{
	return MTL_MOR_COLD_SEAL_DOMAIN ^ generation ^
		((uint64_t)boot_kind << 32) ^ sizeof(struct starbook_mtl_mor_cold_payload);
}

static bool payload_valid(const struct starbook_mtl_mor_cold_payload *payload)
{
	return payload->revision == STARBOOK_MTL_MOR_COLD_REVISION &&
		payload->size == sizeof(*payload) &&
		(payload->boot_kind == STARBOOK_MTL_MOR_BOOT_COLD ||
		 payload->boot_kind == STARBOOK_MTL_MOR_BOOT_S3) &&
		payload->sealed == 1U && payload->generation &&
		payload->seal == payload_seal(payload->boot_kind, payload->generation);
}

void starbook_mtl_mor_cold_capture(
	struct starbook_mtl_mor_cold_capture *capture, int s3wake)
{
	if (!object_valid(capture, sizeof(*capture), _Alignof(*capture)))
		return;
	if (capture->captured) {
		memset(capture, 0, sizeof(*capture));
		return;
	}
	capture->boot_kind = s3wake ? STARBOOK_MTL_MOR_BOOT_S3 :
		STARBOOK_MTL_MOR_BOOT_COLD;
	capture->captured = 1U;
}

static bool common_inputs_valid(struct starbook_mtl_mor_cold_record *record,
	uintptr_t entry_base, size_t entry_size,
	const struct starbook_mtl_mor_cold_ops *ops)
{
	return object_valid(record, sizeof(*record), _Alignof(*record)) &&
		entry_base == (uintptr_t)record && entry_size == sizeof(*record) &&
		object_valid(ops, sizeof(*ops), _Alignof(*ops));
}

enum cb_err starbook_mtl_mor_cold_publish(
	struct starbook_mtl_mor_cold_capture *capture,
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops)
{
	struct starbook_mtl_mor_cold_payload payload = { 0 };
	struct starbook_mtl_mor_cold_capture captured;
	struct starbook_mtl_mor_cold_ops operations;
	uint64_t first_limit;
	uint64_t final_limit;

	if (!common_inputs_valid(record, entry_base, entry_size, ops) ||
	    !object_valid(capture, sizeof(*capture), _Alignof(*capture)) ||
	    objects_overlap(capture, sizeof(*capture), record, sizeof(*record)) ||
	    objects_overlap(capture, sizeof(*capture), ops, sizeof(*ops)) ||
	    objects_overlap(record, sizeof(*record), ops, sizeof(*ops)) ||
	    object_contains(capture, sizeof(*capture), ops->context) ||
	    object_contains(record, sizeof(*record), ops->context) ||
	    object_contains(ops, sizeof(*ops), ops->context) ||
	    !ops->protected_limit || !ops->quiesce || !ops->random64)
		return CB_ERR_ARG;
	memcpy(&captured, capture, sizeof(captured));
	memcpy(&operations, ops, sizeof(operations));
	if (captured.captured != 1U ||
	    (captured.boot_kind != STARBOOK_MTL_MOR_BOOT_COLD &&
	     captured.boot_kind != STARBOOK_MTL_MOR_BOOT_S3))
		return CB_ERR;
	if (operations.protected_limit(operations.context, &first_limit) != CB_SUCCESS ||
	    !range_protected(entry_base, entry_size, first_limit))
		return CB_ERR;
	memset(capture, 0, sizeof(*capture));
	if (operations.random64(operations.context, &payload.generation) != CB_SUCCESS ||
	    !payload.generation ||
	    operations.quiesce(operations.context) != CB_SUCCESS ||
	    operations.protected_limit(operations.context, &final_limit) != CB_SUCCESS ||
	    final_limit != first_limit ||
	    !range_protected(entry_base, entry_size, final_limit) ||
	    memcmp(capture, &(const struct starbook_mtl_mor_cold_capture) { 0 },
		sizeof(*capture)) ||
	    memcmp(&operations, ops, sizeof(operations)))
		return CB_ERR;
	payload.revision = STARBOOK_MTL_MOR_COLD_REVISION;
	payload.size = sizeof(payload);
	payload.boot_kind = captured.boot_kind;
	payload.sealed = 1U;
	payload.seal = payload_seal(payload.boot_kind, payload.generation);
	record->primary = payload;
	record->mirror = payload;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_mor_cold_consume(
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops,
	uint64_t *generation)
{
	struct starbook_mtl_mor_cold_record saved;
	struct starbook_mtl_mor_cold_ops operations;
	uint64_t first_limit;
	uint64_t final_limit;
	uint64_t saved_generation = 0;
	enum cb_err result = CB_ERR;
	bool safe_to_wipe = false;

	if (!common_inputs_valid(record, entry_base, entry_size, ops) ||
	    !object_valid(generation, sizeof(*generation), _Alignof(*generation)) ||
	    objects_overlap(record, sizeof(*record), ops, sizeof(*ops)) ||
	    objects_overlap(generation, sizeof(*generation), record, sizeof(*record)) ||
	    objects_overlap(generation, sizeof(*generation), ops, sizeof(*ops)) ||
	    object_contains(record, sizeof(*record), ops->context) ||
	    object_contains(ops, sizeof(*ops), ops->context) ||
	    object_contains(generation, sizeof(*generation), ops->context) ||
	    !ops->protected_limit || !ops->quiesce)
		return CB_ERR_ARG;
	*generation = 0;
	memcpy(&operations, ops, sizeof(operations));
	if (operations.protected_limit(operations.context, &first_limit) != CB_SUCCESS ||
	    !range_protected(entry_base, entry_size, first_limit))
		goto out;
	memcpy(&saved, record, sizeof(saved));
	if (operations.quiesce(operations.context) != CB_SUCCESS ||
	    operations.protected_limit(operations.context, &final_limit) != CB_SUCCESS ||
	    final_limit != first_limit ||
	    !range_protected(entry_base, entry_size, final_limit))
		goto out;
	safe_to_wipe = true;
	if (memcmp(&saved, record, sizeof(saved)) ||
	    memcmp(&operations, ops, sizeof(operations)) ||
	    !payload_valid(&saved.primary) ||
	    memcmp(&saved.primary, &saved.mirror, sizeof(saved.primary)) ||
	    saved.primary.boot_kind != STARBOOK_MTL_MOR_BOOT_COLD)
		goto out;
	saved_generation = saved.primary.generation;
	result = CB_SUCCESS;
out:
	memset(&saved, 0, sizeof(saved));
	if (safe_to_wipe)
		memset(record, 0, sizeof(*record));
	if (result == CB_SUCCESS)
		*generation = saved_generation;
	return result;
}

#if ENV_SEPARATE_ROMSTAGE || ENV_RAMSTAGE
static struct pci_bme_quiesce_snapshot pci_snapshot;
static struct pci_bme_quiesce_snapshot pci_workspace;

static uint32_t pci_read32(void *unused, uint8_t bus, uint8_t devfn,
	uint16_t offset)
{
	(void)unused;
	return read32p(CONFIG_ECAM_MMCONF_BASE_ADDRESS +
		((uintptr_t)bus << 20) + ((uintptr_t)devfn << 12) + offset);
}

static void pci_write16(void *unused, uint8_t bus, uint8_t devfn,
	uint16_t offset, uint16_t value)
{
	(void)unused;
	write16p(CONFIG_ECAM_MMCONF_BASE_ADDRESS +
		((uintptr_t)bus << 20) + ((uintptr_t)devfn << 12) + offset, value);
}

static enum cb_err protected_limit(void *unused, uint64_t *exclusive_limit)
{
	const uintptr_t base = soc_vtd_iop_base();
	const uint32_t enable = vtd_read32(base, PMEN_REG);
	const uint32_t low_base = vtd_read32(base, PLMBASE_REG);
	const uint32_t low_limit = vtd_read32(base, PLMLIMIT_REG);

	(void)unused;
	if (!exclusive_limit || !base || low_base || low_limit == UINT32_MAX ||
	    (enable & (PMEN_EPM | PMEN_PRS)) != (PMEN_EPM | PMEN_PRS))
		return CB_ERR;
	*exclusive_limit = (uint64_t)low_limit + 1U;
	return CB_SUCCESS;
}

static enum cb_err quiesce(void *unused)
{
	const struct pci_bme_quiesce_io io = {
		.read32 = pci_read32,
		.write16 = pci_write16,
	};

	(void)unused;
	memset(&pci_snapshot, 0, sizeof(pci_snapshot));
	pci_snapshot.failed = 1U;
	return pci_bme_quiesce(&io, CONFIG_ECAM_MMCONF_BUS_NUMBER,
		&pci_snapshot, &pci_workspace);
}
#endif

#if ENV_SEPARATE_ROMSTAGE
static struct starbook_mtl_mor_cold_capture romstage_capture;
static struct starbook_mtl_mor_cold_record *romstage_record;

static enum cb_err random64(void *unused, uint64_t *value)
{
	(void)unused;
	return get_random_number_64(value);
}

static const struct starbook_mtl_mor_cold_ops romstage_ops = {
	.protected_limit = protected_limit,
	.quiesce = quiesce,
	.random64 = random64,
};

static void allocate_record(int is_recovery)
{
	if (is_recovery)
		romstage_record = cbmem_find(CBMEM_ID_MTL_MOR_COLD);
	else {
		romstage_record = cbmem_add(CBMEM_ID_MTL_MOR_COLD,
			sizeof(*romstage_record));
		if (romstage_record)
			memset(romstage_record, 0, sizeof(*romstage_record));
	}
}
CBMEM_CREATION_HOOK(allocate_record);

void mainboard_mor_cold_capture(int s3wake)
{
	starbook_mtl_mor_cold_capture(&romstage_capture, s3wake);
}

enum cb_err mainboard_mor_cold_publish(void)
{
	const struct cbmem_entry *entry = cbmem_entry_find(CBMEM_ID_MTL_MOR_COLD);

	if (!romstage_record || !entry || cbmem_entry_start(entry) != romstage_record ||
	    cbmem_entry_size(entry) != sizeof(*romstage_record))
		return CB_ERR;
	return starbook_mtl_mor_cold_publish(&romstage_capture, romstage_record,
		(uintptr_t)cbmem_entry_start(entry), cbmem_entry_size(entry),
		&romstage_ops);
}
#endif

#if ENV_RAMSTAGE
static const struct starbook_mtl_mor_cold_ops ramstage_ops = {
	.protected_limit = protected_limit,
	.quiesce = quiesce,
};

enum cb_err starbook_mtl_mor_cold_ramstage_consume(uint64_t *generation)
{
	struct starbook_mtl_mor_cold_record *record;
	const struct cbmem_entry *entry;

	if (!object_valid(generation, sizeof(*generation), _Alignof(*generation)))
		return CB_ERR_ARG;
	*generation = 0;
	entry = cbmem_entry_find(CBMEM_ID_MTL_MOR_COLD);
	record = cbmem_find(CBMEM_ID_MTL_MOR_COLD);
	if (!entry || !record || cbmem_entry_start(entry) != record ||
	    cbmem_entry_size(entry) != sizeof(*record))
		return CB_ERR;
	return starbook_mtl_mor_cold_consume(record,
		(uintptr_t)cbmem_entry_start(entry), cbmem_entry_size(entry),
		&ramstage_ops, generation);
}

enum cb_err starbook_mtl_mor_cold_ramstage_consume_snapshot(
	uint64_t *generation, struct pci_bme_quiesce_snapshot *snapshot)
{
	if (!object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)))
		return CB_ERR_ARG;
	memset(snapshot, 0, sizeof(*snapshot));
	if (!object_valid(generation, sizeof(*generation), _Alignof(*generation)) ||
	    objects_overlap(generation, sizeof(*generation), snapshot,
		 sizeof(*snapshot)))
		return CB_ERR_ARG;
	if (starbook_mtl_mor_cold_ramstage_consume(generation) != CB_SUCCESS ||
	    pci_snapshot.failed || !pci_snapshot.count ||
	    pci_snapshot.count > PCI_BME_QUIESCE_MAX_FUNCTIONS) {
		*generation = 0;
		return CB_ERR;
	}
	memcpy(snapshot, &pci_snapshot, sizeof(*snapshot));
	return CB_SUCCESS;
}
#endif
