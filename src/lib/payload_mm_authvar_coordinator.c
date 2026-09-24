/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_bundle.h>
#include <boot/payload_mm_authvar_candidate.h>
#include <boot/payload_mm_authvar_service.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_authvar_coordinator.h"
#include "payload_mm_authvar_set_preflight.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable coordinator must only be built in SMM"
#endif

static bool range_valid(const void *pointer, size_t size)
{
	return pointer && size && (uintptr_t)pointer <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	const uintptr_t left_start = (uintptr_t)left;
	const uintptr_t right_start = (uintptr_t)right;

	if (!range_valid(left, left_size) || !range_valid(right, right_size))
		return true;
	return left_start <= right_start + right_size - 1U &&
		right_start <= left_start + left_size - 1U;
}

static bool outputs_disjoint(
	const struct payload_mm_authvar_coordinator_policy *coordinator,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_write_policy *policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	void *append_workspace, size_t append_workspace_size, void *candidate,
	size_t candidate_capacity,
	struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	struct payload_mm_authvar_coordinator_result *result,
	bool *invariant_failure)
{
	const struct payload_mm_authvar_policy_request *request =
		coordinator->request;
	const void *inputs[] = {
		coordinator, index, policy, binding, request, coordinator->owner,
		coordinator->verify_context, request->name, request->data,
		index->store, index->entries, append_workspace, candidate,
		scan_entries,
	};
	size_t scan_size;
	size_t index_size;
	size_t sizes[] = {
		sizeof(*coordinator), sizeof(*index), sizeof(*policy), sizeof(*binding),
		sizeof(*request), sizeof(*coordinator->owner),
		coordinator->verify_context_size, request->name_size,
		request->data_size, index->store_size, 0U, append_workspace_size,
		candidate_capacity, 0U,
	};

	if (__builtin_mul_overflow(scan_entry_capacity, sizeof(*scan_entries),
		&scan_size) || __builtin_mul_overflow((size_t)index->entry_count,
		sizeof(*index->entries), &index_size))
		return false;
	sizes[10] = index_size;
	sizes[13] = scan_size;
	if (!range_valid(result, sizeof(*result)) ||
	    !range_valid(invariant_failure, sizeof(*invariant_failure)) ||
	    ranges_overlap(result, sizeof(*result), invariant_failure,
		sizeof(*invariant_failure)))
		return false;
	for (size_t i = 0U; i < ARRAY_SIZE(inputs); i++) {
		if (!sizes[i])
			continue;
		if (!range_valid(inputs[i], sizes[i]) ||
		    ranges_overlap(result, sizeof(*result), inputs[i], sizes[i]) ||
		    ranges_overlap(invariant_failure, sizeof(*invariant_failure),
			inputs[i], sizes[i]))
			return false;
	}
	return true;
}

#if ENV_TEST
static enum payload_mm_authvar_coordinator_test_fault test_fault;

void payload_mm_authvar_coordinator_test_set_fault(
	enum payload_mm_authvar_coordinator_test_fault fault)
{
	test_fault = fault;
}
#endif

bool payload_mm_authvar_coordinator_source_modes(
	const struct payload_mm_authvar_store_index *index, bool at_runtime,
	bool sealed_modes_valid, u8 sealed_modes, u8 *source_modes)
{
	const u32 nv_bs = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	const u32 nv_bs_time = nv_bs |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	const u8 mode_mask = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	const struct payload_mm_authvar_store_entry *pk;
	const struct payload_mm_authvar_store_entry *enable;
	u8 enable_value = 0;
	u8 vendor_value;
	u8 modes;

	if (!payload_mm_authvar_store_index_valid(index) || !source_modes ||
	    sealed_modes & ~mode_mask)
		return false;
	pk = payload_mm_authvar_mode_find(index, PAYLOAD_MM_AUTHVAR_MODE_KEY_PK);
	enable = payload_mm_authvar_mode_find(index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE);
	if (!payload_mm_authvar_mode_value(index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_VENDOR_KEYS_NV, nv_bs_time,
		&vendor_value) ||
	    (enable && !payload_mm_authvar_mode_value(index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE, nv_bs,
		&enable_value)))
		return false;
	modes = (u8)(pk ? 0U : PAYLOAD_MM_AUTHVAR_MODE_SETUP);
	if (pk && !enable)
		return false;
	if (vendor_value)
		modes |= PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	if (at_runtime) {
		if (!sealed_modes_valid ||
		    !!(sealed_modes & PAYLOAD_MM_AUTHVAR_MODE_SETUP) != !pk ||
		    !!(sealed_modes & PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) !=
			!!vendor_value)
			return false;
		modes |= sealed_modes & PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
	} else {
		if (pk && enable_value)
			modes |= PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
		if (sealed_modes_valid && sealed_modes != modes)
			return false;
	}
	*source_modes = modes;
	return true;
}

bool payload_mm_authvar_coordinator_reconcile_modes(
	const struct payload_mm_authvar_store_index *index, bool at_runtime,
	u8 sealed_modes, u8 *source_modes)
{
	const u32 nv_bs_time = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	u8 projected = sealed_modes;
	u8 vendor_value;

	if (!at_runtime)
		return payload_mm_authvar_coordinator_source_modes(index, false,
			false, 0U, source_modes);
	if (!payload_mm_authvar_mode_value(index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_VENDOR_KEYS_NV, nv_bs_time,
		&vendor_value))
		return false;
	if (payload_mm_authvar_mode_find(index, PAYLOAD_MM_AUTHVAR_MODE_KEY_PK))
		projected &= (u8)~PAYLOAD_MM_AUTHVAR_MODE_SETUP;
	else
		projected |= PAYLOAD_MM_AUTHVAR_MODE_SETUP;
	if (vendor_value)
		projected |= PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	else
		projected &= (u8)~PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	return payload_mm_authvar_coordinator_source_modes(index, true, true,
		projected, source_modes);
}

static bool custom_mode(
	const struct payload_mm_authvar_store_index *index, bool *enabled)
{
	const u32 attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	const struct payload_mm_authvar_store_entry *entry =
		payload_mm_authvar_mode_find(index,
			PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE);
	u8 value;

	/* EDK2 treats an absent CustomMode variable as StandardMode. */
	if (!entry) {
		*enabled = false;
		return true;
	}
	if (!payload_mm_authvar_mode_value(index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE, attributes, &value))
		return false;
	*enabled = value != 0U;
	return true;
}

static uint64_t verify_status(enum payload_mm_verify_status status,
	bool *invariant_failure)
{
	switch (status) {
	case PAYLOAD_MM_VERIFY_OK:
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	case PAYLOAD_MM_VERIFY_INVALID:
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	case PAYLOAD_MM_VERIFY_BUSY:
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	case PAYLOAD_MM_VERIFY_NO_MEMORY:
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	case PAYLOAD_MM_VERIFY_MALFORMED:
	case PAYLOAD_MM_VERIFY_REJECTED:
		return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	case PAYLOAD_MM_VERIFY_UNSUPPORTED:
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	case PAYLOAD_MM_VERIFY_INTERNAL:
	case PAYLOAD_MM_VERIFY_CHANGED:
	default:
		*invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
}

uint64_t payload_mm_authvar_coordinator_prepare(
	const struct payload_mm_authvar_coordinator_policy *coordinator,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_write_policy *policy,
	const struct payload_mm_authvar_candidate_binding *binding,
	bool ready_to_boot, void *append_workspace, size_t append_workspace_size,
	void *candidate, size_t candidate_capacity,
	struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	struct payload_mm_authvar_coordinator_result *result,
	bool *invariant_failure)
{
	struct payload_mm_authvar_authority_decision decision;
	struct payload_mm_authvar_bundle_plan bundle;
	struct payload_mm_authvar_coordinator_result draft = { 0 };
	struct payload_mm_authvar_authority_snapshot authority;
	struct payload_mm_authvar_bundle_snapshot bundle_snapshot;
	struct payload_mm_authvar_set_snapshot set_snapshot;
	struct payload_mm_authvar_set_plan set_plan;
	enum payload_mm_verify_status status;
	bool in_custom_mode;
	u8 derived_modes;
	u64 efi_status;

	if (!coordinator || !index || !policy || !binding || !result ||
	    !invariant_failure || !append_workspace || !append_workspace_size ||
	    !candidate || !candidate_capacity || !scan_entries ||
	    !scan_entry_capacity ||
	    (uintptr_t)coordinator % _Alignof(*coordinator) ||
	    (uintptr_t)index % _Alignof(*index) ||
	    (uintptr_t)policy % _Alignof(*policy) ||
	    (uintptr_t)binding % _Alignof(*binding) ||
	    (uintptr_t)scan_entries % _Alignof(*scan_entries) ||
	    (uintptr_t)result % _Alignof(*result) ||
	    (uintptr_t)invariant_failure % _Alignof(*invariant_failure) ||
	    !range_valid(coordinator, sizeof(*coordinator)) ||
	    !range_valid(index, sizeof(*index)) ||
	    !range_valid(policy, sizeof(*policy)) ||
	    !range_valid(binding, sizeof(*binding)))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (!coordinator->request || !coordinator->owner || !coordinator->verify ||
	    (uintptr_t)coordinator->request % _Alignof(*coordinator->request) ||
	    (uintptr_t)coordinator->owner % _Alignof(*coordinator->owner) ||
	    (uintptr_t)index->entries % _Alignof(*index->entries) ||
	    !range_valid(coordinator->request, sizeof(*coordinator->request)) ||
	    !outputs_disjoint(coordinator, index, policy, binding,
		append_workspace, append_workspace_size, candidate,
		candidate_capacity, scan_entries, scan_entry_capacity, result,
		invariant_failure))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	memset(result, 0, sizeof(*result));
	set_snapshot = (struct payload_mm_authvar_set_snapshot) {
		.request = coordinator->request,
		.index = index,
		.at_runtime = binding->at_runtime,
	};
	efi_status = payload_mm_authvar_set_preflight(&set_snapshot, &set_plan);
	if (efi_status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return efi_status;
	if (set_plan.kind != PAYLOAD_MM_AUTHVAR_SET_AUTH2)
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (!payload_mm_authvar_coordinator_source_modes(index,
		binding->at_runtime, true, binding->source_volatile_modes,
		&derived_modes) || derived_modes != binding->source_volatile_modes ||
	    !custom_mode(index, &in_custom_mode)) {
		*invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	authority = (struct payload_mm_authvar_authority_snapshot) {
		.request = coordinator->request,
		.index = index,
		.owner = coordinator->owner,
		.facts = {
			.setup_mode = binding->source_volatile_modes &
				PAYLOAD_MM_AUTHVAR_MODE_SETUP,
			.custom_mode = in_custom_mode,
			.trusted_physical_presence =
				coordinator->trusted_physical_presence,
			.require_self_signed_pk =
				CONFIG(PAYLOAD_MM_AUTHVAR_REQUIRE_SELF_SIGNED_PK),
		},
		.verify = coordinator->verify,
		.verify_context = coordinator->verify_context,
		.append_workspace = append_workspace,
		.append_workspace_size = append_workspace_size,
	};
	status = payload_mm_authvar_authority_decide(&authority, &decision);
	efi_status = verify_status(status, invariant_failure);
	if (efi_status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return efi_status;
	if (set_plan.post_auth_status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) {
		draft.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND;
		draft.volatile_modes = binding->source_volatile_modes;
		*result = draft;
		return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	}
	if (set_plan.post_auth_status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return set_plan.post_auth_status;
	bundle_snapshot = (struct payload_mm_authvar_bundle_snapshot) {
		.request = coordinator->request,
		.decision = &decision,
		.index = index,
		.certdb_workspace = append_workspace,
		.certdb_workspace_size = append_workspace_size,
		.facts = {
			.ready_to_boot = ready_to_boot,
			.at_runtime = binding->at_runtime,
			.setup_mode = binding->source_volatile_modes &
				PAYLOAD_MM_AUTHVAR_MODE_SETUP,
			.secure_boot = binding->source_volatile_modes &
				PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT,
			.vendor_keys = binding->source_volatile_modes &
				PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS,
		},
	};
	status = payload_mm_authvar_bundle_plan(&bundle_snapshot, &bundle);
#if ENV_TEST
	switch (test_fault) {
	case PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_INVALID:
		status = PAYLOAD_MM_VERIFY_INVALID;
		break;
	case PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_INTERNAL:
		status = PAYLOAD_MM_VERIFY_INTERNAL;
		break;
	case PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_CHANGED:
		status = PAYLOAD_MM_VERIFY_CHANGED;
		break;
	case PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_UNSUPPORTED:
		status = PAYLOAD_MM_VERIFY_UNSUPPORTED;
		break;
	case PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_REJECTED:
		status = PAYLOAD_MM_VERIFY_REJECTED;
		break;
	default:
		break;
	}
#endif
	if (status == PAYLOAD_MM_VERIFY_INVALID) {
		*invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	efi_status = verify_status(status, invariant_failure);
	if (efi_status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return efi_status;
#if ENV_TEST
	if (test_fault ==
		PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_OUTCOME_NONE)
		bundle.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NONE;
	else if (test_fault ==
		PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_OUTCOME_RANGE)
		bundle.outcome = (enum payload_mm_authvar_authority_outcome)99;
	else if (test_fault == PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_NOOP_NOT_FOUND)
		bundle.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP;
#endif
	draft.outcome = bundle.outcome;
#if ENV_TEST
	if (test_fault == PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_OUTCOME_NONE)
		draft.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NONE;
	if (test_fault == PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_NOT_FOUND_SUCCESS) {
		draft.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND;
		*result = draft;
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	}
#endif
	draft.volatile_modes = binding->source_volatile_modes;
	if (bundle.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP ||
	    bundle.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND) {
		*result = draft;
#if ENV_TEST
		if (test_fault == PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_NOOP_NOT_FOUND &&
		    bundle.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP)
			return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
#endif
		return bundle.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP ?
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS :
			PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	}
	if (bundle.outcome != PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION) {
		*invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	efi_status = payload_mm_authvar_candidate_build(index, &bundle, policy,
		binding, candidate, candidate_capacity, scan_entries,
		scan_entry_capacity, &draft.candidate);
#if ENV_TEST
	if (test_fault == PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_CANDIDATE_INVALID)
		efi_status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	else if (test_fault ==
		PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_CANDIDATE_SECURITY)
		efi_status = PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	else if (test_fault ==
		PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_CANDIDATE_NO_MEMORY)
		efi_status = PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
#endif
	if (efi_status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER ||
	    efi_status == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION) {
		*invariant_failure = true;
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
	if (efi_status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return efi_status;
	draft.volatile_modes = draft.candidate.volatile_modes;
	*result = draft;
#if ENV_TEST
	if (test_fault == PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_MUTATION_NOT_FOUND)
		return PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
#endif
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
