/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_mor_identity.h>
#include <boot/payload_mm_authvar_mor_probe.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <commonlib/region.h>
#include <smmstore.h>
#include <string.h>

static bool output_valid(const struct payload_mm_authvar_mor_entry *entry)
{
	return entry && !((uintptr_t)entry % _Alignof(*entry)) &&
		(uintptr_t)entry <= UINTPTR_MAX - (sizeof(*entry) - 1U);
}

static enum cb_err probe_mapped_store(const uint8_t *region, size_t region_size,
	struct payload_mm_authvar_mor_entry *entry)
{
	struct payload_mm_authvar_fv_geometry expected;
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_SIZE,
		.maximum_name_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE,
		.maximum_data_size = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_DATA_SIZE,
		.maximum_records = PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_RECORDS,
	};
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	struct payload_mm_authvar_store_entry found_entry;
	struct payload_mm_authvar_ftw_plan plan;
	const uint8_t *store;
	bool found;

	if (payload_mm_authvar_ftw_plan(region, region_size, SMM_BLOCK_SIZE, &plan) !=
		CB_SUCCESS || plan.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN)
		return CB_ERR;
	if (!payload_mm_authvar_fv_geometry(&expected, region_size, SMM_BLOCK_SIZE) ||
	    memcmp(&plan.geometry, &expected, sizeof(expected)) ||
	    expected.spare_offset > region_size ||
	    expected.spare_size != region_size - expected.spare_offset ||
	    plan.fv_header_size > plan.geometry.variable_size ||
	    !plan.variable_store_size ||
	    plan.variable_store_size !=
		plan.geometry.variable_size - plan.fv_header_size)
		return CB_ERR;
	store = region + plan.geometry.variable_offset + plan.fv_header_size;
	if (payload_mm_authvar_store_find_one(&found_entry, &found, store,
		plan.variable_store_size, &limits, identity->vendor_guid,
		identity->name, identity->name_size) != CB_SUCCESS)
		return CB_ERR;
	if (!found)
		return CB_SUCCESS;
	if (found_entry.attributes != PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES ||
	    found_entry.data_size != 1U ||
	    found_entry.data_offset > plan.variable_store_size - 1U)
		return CB_ERR;
	entry->present = 1U;
	entry->value = store[found_entry.data_offset];
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_probe_entry(
	struct payload_mm_authvar_mor_entry *entry)
{
	struct payload_mm_authvar_mor_entry candidate = { 0 };
	struct region_device store;
	const uint8_t *mapping;
	enum cb_err status = CB_ERR;

	if (!output_valid(entry))
		return CB_ERR;
	memset(entry, 0, sizeof(*entry));
	if (smmstore_lookup_read_region(&store) < 0)
		return CB_ERR;
	mapping = rdev_mmap_full(&store);
	if (!mapping)
		return CB_ERR;
	status = probe_mapped_store(mapping, region_device_sz(&store), &candidate);
	if (rdev_munmap(&store, (void *)mapping) < 0)
		status = CB_ERR;
	if (status == CB_SUCCESS)
		*entry = candidate;
	return status;
}
