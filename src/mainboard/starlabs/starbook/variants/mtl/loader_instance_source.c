/* SPDX-License-Identifier: GPL-2.0-only */

#include "loader_instance_source.h"
#include "mor_cold_boot.h"

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

#define MTL_LOADER_INSTANCE_SEAL_DOMAIN 0x6d746c2d6c647269ULL
#define MTL_PCI_COMMAND_MASTER (1U << 2)

#ifdef __TEST__
void starbook_mtl_loader_instance_source_test_hook(uint32_t point);
#define SOURCE_TEST_HOOK(point) \
	starbook_mtl_loader_instance_source_test_hook(point)
#else
#define SOURCE_TEST_HOOK(point) do { } while (0)
#endif

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

	if (!object_valid(first, first_size, 1) ||
	    !object_valid(second, second_size, 1))
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static bool object_contains(const void *object, size_t size, const void *pointer)
{
	const uintptr_t base = (uintptr_t)object;
	const uintptr_t address = (uintptr_t)pointer;

	return object_valid(object, size, 1) && pointer && address >= base &&
		address - base < size;
}

static bool range_protected(uintptr_t base, size_t size, uint64_t limit)
{
	return size && base <= (uintptr_t)-1 - (size - 1U) &&
		(uint64_t)base < limit && size <= limit - (uint64_t)base;
}

static uint64_t payload_seal(uint32_t lifecycle,
	struct smm_invocation_loader_instance_nonce nonce)
{
	return MTL_LOADER_INSTANCE_SEAL_DOMAIN ^ nonce.low ^
		((nonce.high << 17) | (nonce.high >> 47)) ^
		((uint64_t)lifecycle << 32) ^
		sizeof(struct starbook_mtl_loader_instance_source_payload);
}

static bool payload_valid(
	const struct starbook_mtl_loader_instance_source_payload *payload)
{
	return payload->revision == STARBOOK_MTL_LOADER_INSTANCE_SOURCE_REVISION &&
		payload->size == sizeof(*payload) &&
		(payload->lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD ||
		 payload->lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD) &&
		payload->sealed == 1U && payload->loader_instance_nonce.low &&
		payload->seal == payload_seal(payload->lifecycle,
			payload->loader_instance_nonce);
}

void starbook_mtl_loader_instance_source_capture(
	struct starbook_mtl_loader_instance_source_capture *capture, int s3wake)
{
	if (!object_valid(capture, sizeof(*capture), _Alignof(*capture)))
		return;
	if (__atomic_load_n(&capture->captured, __ATOMIC_ACQUIRE)) {
		memset(capture, 0, sizeof(*capture));
		return;
	}
	capture->lifecycle = s3wake ? SMM_INVOCATION_LOADER_S3_RELOAD :
		SMM_INVOCATION_LOADER_NON_S3_LOAD;
	capture->captured = 1U;
}

static bool common_inputs_valid(
	struct starbook_mtl_loader_instance_source_record *record,
	uintptr_t entry_base, size_t entry_size,
	const struct starbook_mtl_loader_instance_source_ops *ops)
{
	return object_valid(record, sizeof(*record), _Alignof(*record)) &&
		entry_base == (uintptr_t)record && entry_size == sizeof(*record) &&
		object_valid(ops, sizeof(*ops), _Alignof(*ops));
}

static void record_scrub_tail(
	struct starbook_mtl_loader_instance_source_record *record,
	uint32_t final_state)
{
	volatile uint8_t *bytes = (volatile uint8_t *)record;

	for (size_t index = sizeof(record->state); index < sizeof(*record); index++)
		bytes[index] = 0;
	__atomic_store_n(&record->state, final_state, __ATOMIC_RELEASE);
}

static bool record_claim_for_publish(
	struct starbook_mtl_loader_instance_source_record *record)
{
	uint32_t state = __atomic_load_n(&record->state, __ATOMIC_ACQUIRE);
	uint32_t expected;

	if (state == STARBOOK_MTL_LOADER_INSTANCE_SOURCE_READY ||
	    state == STARBOOK_MTL_LOADER_INSTANCE_SOURCE_CONSUMED) {
		expected = state;
		if (!__atomic_compare_exchange_n(&record->state, &expected,
			STARBOOK_MTL_LOADER_INSTANCE_SOURCE_TAKING, false,
			__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return false;
		record_scrub_tail(record, STARBOOK_MTL_LOADER_INSTANCE_SOURCE_EMPTY);
	} else if (state != STARBOOK_MTL_LOADER_INSTANCE_SOURCE_EMPTY) {
		return false;
	}
	expected = STARBOOK_MTL_LOADER_INSTANCE_SOURCE_EMPTY;
	return __atomic_compare_exchange_n(&record->state, &expected,
		STARBOOK_MTL_LOADER_INSTANCE_SOURCE_PUBLISHING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static void record_publish_failed(
	struct starbook_mtl_loader_instance_source_record *record)
{
	uint32_t state = __atomic_load_n(&record->state, __ATOMIC_ACQUIRE);

	while (state != STARBOOK_MTL_LOADER_INSTANCE_SOURCE_TAKING &&
	       !__atomic_compare_exchange_n(&record->state, &state,
		STARBOOK_MTL_LOADER_INSTANCE_SOURCE_TAKING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		;
	record_scrub_tail(record, STARBOOK_MTL_LOADER_INSTANCE_SOURCE_EMPTY);
}

enum cb_err starbook_mtl_loader_instance_source_publish(
	struct starbook_mtl_loader_instance_source_capture *capture,
	struct starbook_mtl_loader_instance_source_record *record,
	uintptr_t entry_base, size_t entry_size,
	const struct starbook_mtl_loader_instance_source_ops *ops)
{
	struct starbook_mtl_loader_instance_source_payload payload = { 0 };
	struct starbook_mtl_loader_instance_source_capture captured;
	struct starbook_mtl_loader_instance_source_ops operations;
	uint64_t first_limit;
	uint64_t final_limit;
	uint32_t expected;
	uint32_t expected_capture = 1U;
	bool owns_record = false;
	bool owns_capture = false;

	if (!common_inputs_valid(record, entry_base, entry_size, ops) ||
	    !object_valid(capture, sizeof(*capture), _Alignof(*capture)) ||
	    objects_overlap(capture, sizeof(*capture), record, sizeof(*record)) ||
	    objects_overlap(capture, sizeof(*capture), ops, sizeof(*ops)) ||
	    objects_overlap(record, sizeof(*record), ops, sizeof(*ops)) ||
	    object_contains(capture, sizeof(*capture), ops->context) ||
	    object_contains(record, sizeof(*record), ops->context) ||
	    object_contains(ops, sizeof(*ops), ops->context) ||
	    !ops->protected_limit || !ops->quiesce || !ops->random128)
		return CB_ERR_ARG;
	memcpy(&captured, capture, sizeof(captured));
	memcpy(&operations, ops, sizeof(operations));
	if (captured.captured != 1U ||
	    (captured.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD &&
	     captured.lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD))
		return CB_ERR;
	if (operations.protected_limit(operations.context, &first_limit) != CB_SUCCESS ||
	    !range_protected(entry_base, entry_size, first_limit))
		return CB_ERR;
	if (operations.quiesce(operations.context) != CB_SUCCESS ||
	    operations.protected_limit(operations.context, &final_limit) != CB_SUCCESS ||
	    final_limit != first_limit ||
	    !range_protected(entry_base, entry_size, final_limit))
		goto out;
	if (!__atomic_compare_exchange_n(&capture->captured, &expected_capture,
		2U, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto out;
	owns_capture = true;
	if (!record_claim_for_publish(record))
		goto out;
	owns_record = true;
	if (operations.random128(operations.context,
		&payload.loader_instance_nonce) != CB_SUCCESS ||
	    !payload.loader_instance_nonce.low ||
	    operations.quiesce(operations.context) != CB_SUCCESS ||
	    operations.protected_limit(operations.context, &final_limit) != CB_SUCCESS ||
	    final_limit != first_limit ||
	    !range_protected(entry_base, entry_size, final_limit) ||
	    capture->lifecycle != captured.lifecycle ||
	    __atomic_load_n(&capture->captured, __ATOMIC_ACQUIRE) != 2U ||
	    __atomic_load_n(&record->state, __ATOMIC_ACQUIRE) !=
		STARBOOK_MTL_LOADER_INSTANCE_SOURCE_PUBLISHING || record->reserved ||
	    memcmp((const uint8_t *)record + sizeof(record->state) +
		sizeof(record->reserved),
		(const uint8_t *)&(const struct
			starbook_mtl_loader_instance_source_record) { 0 } +
			sizeof(record->state) + sizeof(record->reserved),
		sizeof(*record) - sizeof(record->state) - sizeof(record->reserved)) ||
	    memcmp(&operations, ops, sizeof(operations)))
		goto out;
	SOURCE_TEST_HOOK(2);
	if (__atomic_load_n(&record->state, __ATOMIC_ACQUIRE) !=
		STARBOOK_MTL_LOADER_INSTANCE_SOURCE_PUBLISHING)
		goto out;
	payload.revision = STARBOOK_MTL_LOADER_INSTANCE_SOURCE_REVISION;
	payload.size = sizeof(payload);
	payload.lifecycle = captured.lifecycle;
	payload.sealed = 1U;
	payload.seal = payload_seal(payload.lifecycle,
		payload.loader_instance_nonce);
	record->primary = payload;
	record->mirror = payload;
	expected = STARBOOK_MTL_LOADER_INSTANCE_SOURCE_PUBLISHING;
	if (!__atomic_compare_exchange_n(&record->state, &expected,
		STARBOOK_MTL_LOADER_INSTANCE_SOURCE_READY, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		goto out;
	memset(&payload, 0, sizeof(payload));
	memset(&captured, 0, sizeof(captured));
	capture->lifecycle = 0;
	__atomic_store_n(&capture->captured, 0, __ATOMIC_RELEASE);
	return CB_SUCCESS;
out:
	if (owns_record)
		record_publish_failed(record);
	if (owns_capture) {
		capture->lifecycle = 0;
		__atomic_store_n(&capture->captured, 0, __ATOMIC_RELEASE);
	}
	memset(&payload, 0, sizeof(payload));
	memset(&captured, 0, sizeof(captured));
	return CB_ERR;
}

enum cb_err starbook_mtl_loader_instance_source_consume(
	struct starbook_mtl_loader_instance_source_record *record,
	uintptr_t entry_base, size_t entry_size,
	const struct starbook_mtl_loader_instance_source_ops *ops,
	uint32_t *lifecycle,
	struct smm_invocation_loader_instance_nonce *loader_instance_nonce)
{
	struct starbook_mtl_loader_instance_source_record saved;
	struct starbook_mtl_loader_instance_source_ops operations;
	uint64_t first_limit;
	uint64_t final_limit;
	struct smm_invocation_loader_instance_nonce saved_nonce = { 0 };
	uint32_t saved_lifecycle = 0;
	uint32_t expected = STARBOOK_MTL_LOADER_INSTANCE_SOURCE_READY;
	enum cb_err result = CB_ERR;
	bool owns_record = false;

	if (!common_inputs_valid(record, entry_base, entry_size, ops) ||
	    !object_valid(lifecycle, sizeof(*lifecycle), _Alignof(*lifecycle)) ||
	    !object_valid(loader_instance_nonce, sizeof(*loader_instance_nonce),
		_Alignof(*loader_instance_nonce)) ||
	    objects_overlap(lifecycle, sizeof(*lifecycle), loader_instance_nonce,
		sizeof(*loader_instance_nonce)) ||
	    objects_overlap(lifecycle, sizeof(*lifecycle), record, sizeof(*record)) ||
	    objects_overlap(lifecycle, sizeof(*lifecycle), ops, sizeof(*ops)) ||
	    objects_overlap(record, sizeof(*record), ops, sizeof(*ops)) ||
	    objects_overlap(loader_instance_nonce, sizeof(*loader_instance_nonce),
		record, sizeof(*record)) ||
	    objects_overlap(loader_instance_nonce, sizeof(*loader_instance_nonce), ops,
		sizeof(*ops)) ||
	    object_contains(record, sizeof(*record), ops->context) ||
	    object_contains(ops, sizeof(*ops), ops->context) ||
	    object_contains(lifecycle, sizeof(*lifecycle), ops->context) ||
	    object_contains(loader_instance_nonce, sizeof(*loader_instance_nonce),
		ops->context) ||
	    !ops->protected_limit || !ops->quiesce)
		return CB_ERR_ARG;
	*lifecycle = 0;
	memset(loader_instance_nonce, 0, sizeof(*loader_instance_nonce));
	memcpy(&operations, ops, sizeof(operations));
	if (operations.protected_limit(operations.context, &first_limit) != CB_SUCCESS ||
	    !range_protected(entry_base, entry_size, first_limit))
		goto out;
	if (operations.quiesce(operations.context) != CB_SUCCESS ||
	    operations.protected_limit(operations.context, &final_limit) != CB_SUCCESS ||
	    final_limit != first_limit ||
	    !range_protected(entry_base, entry_size, final_limit))
		goto out;
	if (!__atomic_compare_exchange_n(&record->state, &expected,
		STARBOOK_MTL_LOADER_INSTANCE_SOURCE_TAKING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto out;
	owns_record = true;
	memcpy(&saved, record, sizeof(saved));
	SOURCE_TEST_HOOK(1);
	if (saved.state != STARBOOK_MTL_LOADER_INSTANCE_SOURCE_TAKING ||
	    saved.reserved || memcmp(&saved, record, sizeof(saved)) ||
	    memcmp(&operations, ops, sizeof(operations)) ||
	    !payload_valid(&saved.primary) ||
	    memcmp(&saved.primary, &saved.mirror, sizeof(saved.primary)))
		goto out;
	saved_nonce = saved.primary.loader_instance_nonce;
	saved_lifecycle = saved.primary.lifecycle;
	result = CB_SUCCESS;
out:
	memset(&saved, 0, sizeof(saved));
	if (owns_record)
		record_scrub_tail(record,
			STARBOOK_MTL_LOADER_INSTANCE_SOURCE_CONSUMED);
	if (result == CB_SUCCESS) {
		*lifecycle = saved_lifecycle;
		*loader_instance_nonce = saved_nonce;
	}
	memset(&saved_nonce, 0, sizeof(saved_nonce));
	return result;
}

void starbook_mtl_mor_cold_capture(
	struct starbook_mtl_mor_cold_capture *capture, int s3wake)
{
	struct starbook_mtl_loader_instance_source_capture source_capture;

	if (!object_valid(capture, sizeof(*capture), _Alignof(*capture)))
		return;
	memcpy(&source_capture, capture, sizeof(source_capture));
	starbook_mtl_loader_instance_source_capture(&source_capture, s3wake);
	memcpy(capture, &source_capture, sizeof(*capture));
}

enum cb_err starbook_mtl_mor_cold_publish(
	struct starbook_mtl_mor_cold_capture *capture,
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops)
{
	struct starbook_mtl_loader_instance_source_capture source_capture;
	enum cb_err status;

	if (!object_valid(capture, sizeof(*capture), _Alignof(*capture)) ||
	    !object_valid(record, sizeof(*record), _Alignof(*record)) ||
	    !object_valid(ops, sizeof(*ops), _Alignof(*ops)) ||
	    objects_overlap(capture, sizeof(*capture), record, sizeof(*record)) ||
	    objects_overlap(capture, sizeof(*capture), ops, sizeof(*ops)) ||
	    object_contains(capture, sizeof(*capture), ops->context))
		return CB_ERR_ARG;
	memcpy(&source_capture, capture, sizeof(source_capture));
	status = starbook_mtl_loader_instance_source_publish(&source_capture,
		record, entry_base, entry_size, ops);
	memcpy(capture, &source_capture, sizeof(*capture));
	return status;
}

enum cb_err starbook_mtl_mor_cold_consume_classified(
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops,
	uint32_t *boot_kind, uint64_t *generation)
{
	struct smm_invocation_loader_instance_nonce nonce;
	uint32_t lifecycle;
	enum cb_err status;

	if (!common_inputs_valid((void *)record, entry_base, entry_size,
		(const void *)ops) ||
	    !object_valid(boot_kind, sizeof(*boot_kind), _Alignof(*boot_kind)) ||
	    !object_valid(generation, sizeof(*generation), _Alignof(*generation)) ||
	    objects_overlap(boot_kind, sizeof(*boot_kind), generation,
		sizeof(*generation)) ||
	    objects_overlap(boot_kind, sizeof(*boot_kind), record, sizeof(*record)) ||
	    objects_overlap(boot_kind, sizeof(*boot_kind), ops, sizeof(*ops)) ||
	    objects_overlap(generation, sizeof(*generation), record,
		sizeof(*record)) ||
	    objects_overlap(generation, sizeof(*generation), ops, sizeof(*ops)) ||
	    object_contains(boot_kind, sizeof(*boot_kind), ops->context) ||
	    object_contains(generation, sizeof(*generation), ops->context))
		return CB_ERR_ARG;
	*boot_kind = STARBOOK_MTL_MOR_BOOT_UNKNOWN;
	*generation = 0;
	status = starbook_mtl_loader_instance_source_consume(record,
		entry_base, entry_size, ops, &lifecycle, &nonce);
	if (status != CB_SUCCESS)
		return status;
	*boot_kind = lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD ?
		STARBOOK_MTL_MOR_BOOT_S3 : STARBOOK_MTL_MOR_BOOT_COLD;
	*generation = nonce.low;
	memset(&nonce, 0, sizeof(nonce));
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_mor_cold_consume(
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops,
	uint64_t *generation)
{
	uint32_t boot_kind = STARBOOK_MTL_MOR_BOOT_UNKNOWN;
	enum cb_err status;

	status = starbook_mtl_mor_cold_consume_classified(record, entry_base,
		entry_size, ops, &boot_kind, generation);
	if (status != CB_SUCCESS)
		return status;
	if (boot_kind != STARBOOK_MTL_MOR_BOOT_COLD) {
		*generation = 0;
		return CB_ERR;
	}
	return CB_SUCCESS;
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

#if ENV_RAMSTAGE
static bool quiesce_snapshot_valid(
	const struct pci_bme_quiesce_snapshot *snapshot)
{
	if (snapshot->failed ||
	    snapshot->bus_count != CONFIG_ECAM_MMCONF_BUS_NUMBER ||
	    !snapshot->count || snapshot->count > PCI_BME_QUIESCE_MAX_FUNCTIONS)
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
#endif
#endif

#if ENV_SEPARATE_ROMSTAGE
static struct starbook_mtl_loader_instance_source_capture romstage_capture;
static struct starbook_mtl_loader_instance_source_record *romstage_record;

static enum cb_err random128(void *unused,
	struct smm_invocation_loader_instance_nonce *nonce)
{
	(void)unused;
	if (!nonce || get_random_number_64(&nonce->low) != CB_SUCCESS ||
	    get_random_number_64(&nonce->high) != CB_SUCCESS) {
		if (nonce)
			memset(nonce, 0, sizeof(*nonce));
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static const struct starbook_mtl_loader_instance_source_ops romstage_ops = {
	.protected_limit = protected_limit,
	.quiesce = quiesce,
	.random128 = random128,
};

static void allocate_record(int is_recovery)
{
	if (is_recovery)
		romstage_record = cbmem_find(CBMEM_ID_MTL_LOADER_INSTANCE);
	else {
		romstage_record = cbmem_add(CBMEM_ID_MTL_LOADER_INSTANCE,
			sizeof(*romstage_record));
		if (romstage_record)
			memset(romstage_record, 0, sizeof(*romstage_record));
	}
}
CBMEM_CREATION_HOOK(allocate_record);

void mainboard_loader_instance_source_capture(int s3wake)
{
	starbook_mtl_loader_instance_source_capture(&romstage_capture, s3wake);
}

enum cb_err mainboard_loader_instance_source_publish(void)
{
	const struct cbmem_entry *entry =
		cbmem_entry_find(CBMEM_ID_MTL_LOADER_INSTANCE);

	if (!romstage_record || !entry || cbmem_entry_start(entry) != romstage_record ||
	    cbmem_entry_size(entry) != sizeof(*romstage_record))
		return CB_ERR;
	return starbook_mtl_loader_instance_source_publish(&romstage_capture,
		romstage_record,
		(uintptr_t)cbmem_entry_start(entry), cbmem_entry_size(entry),
		&romstage_ops);
}
#endif

#if ENV_RAMSTAGE
static const struct starbook_mtl_loader_instance_source_ops ramstage_ops = {
	.protected_limit = protected_limit,
	.quiesce = quiesce,
};

enum cb_err starbook_mtl_loader_instance_source_ramstage_requiesce(
	uint64_t *exclusive_limit)
{
	uint64_t first_limit;
	uint64_t final_limit;
	enum cb_err result = CB_ERR;
	bool scratch_safe = false;
	enum cb_err quiesce_result;

	if (!object_valid(exclusive_limit, sizeof(*exclusive_limit),
		_Alignof(*exclusive_limit)))
		return CB_ERR_ARG;
	*exclusive_limit = 0;
	if (protected_limit(NULL, &first_limit) != CB_SUCCESS ||
	    !range_protected((uintptr_t)&pci_snapshot, sizeof(pci_snapshot),
		first_limit) ||
	    !range_protected((uintptr_t)&pci_workspace, sizeof(pci_workspace),
		first_limit))
		goto out;
	memset(&pci_workspace, 0, sizeof(pci_workspace));
	quiesce_result = quiesce(NULL);
	if (protected_limit(NULL, &final_limit) != CB_SUCCESS ||
	    !range_protected((uintptr_t)&pci_snapshot, sizeof(pci_snapshot),
		final_limit) ||
	    !range_protected((uintptr_t)&pci_workspace, sizeof(pci_workspace),
		final_limit))
		goto out;
	scratch_safe = true;
	if (quiesce_result != CB_SUCCESS || final_limit != first_limit ||
	    !quiesce_snapshot_valid(&pci_snapshot))
		goto out;
	*exclusive_limit = final_limit;
	result = CB_SUCCESS;
out:
	if (scratch_safe) {
		memset(&pci_snapshot, 0, sizeof(pci_snapshot));
		memset(&pci_workspace, 0, sizeof(pci_workspace));
	}
	return result;
}

enum cb_err starbook_mtl_mor_cold_ramstage_consume(uint64_t *generation)
{
	struct starbook_mtl_mor_cold_record *record;
	const struct cbmem_entry *entry;

	if (!object_valid(generation, sizeof(*generation), _Alignof(*generation)))
		return CB_ERR_ARG;
	*generation = 0;
	entry = cbmem_entry_find(CBMEM_ID_MTL_LOADER_INSTANCE);
	record = cbmem_find(CBMEM_ID_MTL_LOADER_INSTANCE);
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

enum cb_err starbook_mtl_mor_cold_ramstage_classify(
	uint32_t *boot_kind, uint64_t *generation,
	struct pci_bme_quiesce_snapshot *snapshot)
{
	struct starbook_mtl_mor_cold_record *record;
	const struct cbmem_entry *entry;

	if (!object_valid(boot_kind, sizeof(*boot_kind), _Alignof(*boot_kind)) ||
	    !object_valid(generation, sizeof(*generation), _Alignof(*generation)) ||
	    !object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)) ||
	    objects_overlap(boot_kind, sizeof(*boot_kind), generation,
		 sizeof(*generation)) ||
	    objects_overlap(boot_kind, sizeof(*boot_kind), snapshot,
		 sizeof(*snapshot)) ||
	    objects_overlap(generation, sizeof(*generation), snapshot,
		 sizeof(*snapshot)))
		return CB_ERR_ARG;
	*boot_kind = STARBOOK_MTL_MOR_BOOT_UNKNOWN;
	*generation = 0;
	memset(snapshot, 0, sizeof(*snapshot));
	entry = cbmem_entry_find(CBMEM_ID_MTL_LOADER_INSTANCE);
	record = cbmem_find(CBMEM_ID_MTL_LOADER_INSTANCE);
	if (!entry || !record || cbmem_entry_start(entry) != record ||
	    cbmem_entry_size(entry) != sizeof(*record) ||
	    starbook_mtl_mor_cold_consume_classified(record,
		(uintptr_t)cbmem_entry_start(entry), cbmem_entry_size(entry),
		&ramstage_ops, boot_kind, generation) != CB_SUCCESS ||
	    pci_snapshot.failed || !pci_snapshot.count ||
	    pci_snapshot.count > PCI_BME_QUIESCE_MAX_FUNCTIONS) {
		*boot_kind = STARBOOK_MTL_MOR_BOOT_UNKNOWN;
		*generation = 0;
		return CB_ERR;
	}
	memcpy(snapshot, &pci_snapshot, sizeof(*snapshot));
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_loader_instance_source_ramstage_take(
	uint32_t *lifecycle,
	struct smm_invocation_loader_instance_nonce *loader_instance_nonce,
	struct pci_bme_quiesce_snapshot *snapshot)
{
	struct starbook_mtl_loader_instance_source_record *record;
	const struct cbmem_entry *entry;
	uint64_t limit;

	if (!object_valid(lifecycle, sizeof(*lifecycle), _Alignof(*lifecycle)) ||
	    !object_valid(loader_instance_nonce, sizeof(*loader_instance_nonce),
		_Alignof(*loader_instance_nonce)) ||
	    !object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)) ||
	    objects_overlap(lifecycle, sizeof(*lifecycle), loader_instance_nonce,
		sizeof(*loader_instance_nonce)) ||
	    objects_overlap(lifecycle, sizeof(*lifecycle), snapshot,
		sizeof(*snapshot)) ||
	    objects_overlap(loader_instance_nonce, sizeof(*loader_instance_nonce),
		snapshot, sizeof(*snapshot)))
		return CB_ERR_ARG;
	entry = cbmem_entry_find(CBMEM_ID_MTL_LOADER_INSTANCE);
	record = cbmem_find(CBMEM_ID_MTL_LOADER_INSTANCE);
	if (!entry || !record || cbmem_entry_start(entry) != record ||
	    cbmem_entry_size(entry) != sizeof(*record) ||
	    protected_limit(NULL, &limit) != CB_SUCCESS ||
	    !range_protected((uintptr_t)lifecycle, sizeof(*lifecycle), limit) ||
	    !range_protected((uintptr_t)loader_instance_nonce,
		sizeof(*loader_instance_nonce), limit) ||
	    !range_protected((uintptr_t)snapshot, sizeof(*snapshot), limit) ||
	    !range_protected((uintptr_t)&pci_snapshot, sizeof(pci_snapshot), limit) ||
	    !range_protected((uintptr_t)&pci_workspace, sizeof(pci_workspace), limit) ||
	    objects_overlap(lifecycle, sizeof(*lifecycle), record, sizeof(*record)) ||
	    objects_overlap(loader_instance_nonce, sizeof(*loader_instance_nonce),
		record, sizeof(*record)) ||
	    objects_overlap(snapshot, sizeof(*snapshot), record, sizeof(*record)) ||
	    objects_overlap(snapshot, sizeof(*snapshot), &pci_snapshot,
		sizeof(pci_snapshot)) ||
	    objects_overlap(snapshot, sizeof(*snapshot), &pci_workspace,
		sizeof(pci_workspace)))
		return CB_ERR_ARG;
	*lifecycle = 0;
	memset(loader_instance_nonce, 0, sizeof(*loader_instance_nonce));
	memset(snapshot, 0, sizeof(*snapshot));
	if (starbook_mtl_loader_instance_source_consume(record,
		(uintptr_t)cbmem_entry_start(entry), cbmem_entry_size(entry),
		&ramstage_ops, lifecycle, loader_instance_nonce) != CB_SUCCESS ||
	    pci_snapshot.failed || !pci_snapshot.count ||
	    pci_snapshot.count > PCI_BME_QUIESCE_MAX_FUNCTIONS) {
		*lifecycle = 0;
		memset(loader_instance_nonce, 0, sizeof(*loader_instance_nonce));
		memset(&pci_snapshot, 0, sizeof(pci_snapshot));
		return CB_ERR;
	}
	memcpy(snapshot, &pci_snapshot, sizeof(*snapshot));
	memset(&pci_snapshot, 0, sizeof(pci_snapshot));
	return CB_SUCCESS;
}
#endif
