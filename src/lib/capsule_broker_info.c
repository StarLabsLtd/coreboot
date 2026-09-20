/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/capsule_broker.h>
#include <stdint.h>
#include <string.h>

#include "capsule_broker_info_internal.h"
#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_owner_internal.h"
#include "payload_mm_fmp_state_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Capsule broker info policy must only be built in SMM"
#endif

struct info_authority {
	struct capsule_broker_info_policy policy;
	bool installed;
	bool install_attempted;
	bool busy;
};

static struct info_authority info_authority;

static void poison_authority(void)
{
	memset(&info_authority, 0, sizeof(info_authority));
	info_authority.install_attempted = true;
}

static bool guid_present(const guid_t *guid)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < sizeof(guid->b); i++)
		bits |= guid->b[i];
	return bits != 0;
}

static bool bytes_zero(const uint8_t *data, size_t size)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < size; i++)
		bits |= data[i];
	return bits == 0;
}

static uint32_t read32(const uint8_t *data)
{
	return data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 |
		(uint32_t)data[3] << 24;
}

static bool policy_valid(const struct capsule_broker_info_policy *policy)
{
	return policy->revision == CAPSULE_BROKER_INFO_POLICY_REVISION &&
		policy->size == sizeof(*policy) && guid_present(&policy->image_type) &&
		policy->current_version &&
		policy->lowest_supported_version <= policy->current_version &&
		policy->image_size &&
		policy->capabilities == LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES &&
		bytes_zero((const uint8_t *)policy->reserved,
			sizeof(policy->reserved));
}

enum cb_err capsule_broker_info_policy_install(
	const struct capsule_broker_info_policy *trusted_policy,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct capsule_broker_info_policy snapshot;
	struct info_authority expected;
	bool protected;

	if (info_authority.install_attempted)
		return CB_ERR;
	info_authority.install_attempted = true;
	if (!payload_mm_fmp_owner_ready() || !trusted_policy ||
	    !storage_is_protected)
		return CB_ERR;
	memcpy(&snapshot, trusted_policy, sizeof(snapshot));
	if (!policy_valid(&snapshot) ||
	    payload_mm_authvar_buffers_overlap(trusted_policy, sizeof(snapshot),
		&info_authority, sizeof(info_authority)))
		return CB_ERR;
	info_authority.policy = snapshot;
	expected = info_authority;
	protected = storage_is_protected(context, &info_authority,
		sizeof(info_authority));
	if (!protected || memcmp(trusted_policy, &snapshot, sizeof(snapshot)) ||
	    memcmp(&info_authority, &expected, sizeof(expected))) {
		poison_authority();
		return CB_ERR;
	}
	info_authority.installed = true;
	return CB_SUCCESS;
}

enum cb_err capsule_broker_info_read(
	struct capsule_broker_info_snapshot *snapshot)
{
	struct capsule_broker_info_policy policy;
	struct info_authority expected;
	struct payload_mm_fmp_owner_record record;
	uint32_t durable_version;
	uint32_t durable_lowest_version;
	bool authority_corrupted;
	enum cb_err owner_status;
	enum cb_err status = CB_ERR;

	if (!snapshot || (uintptr_t)snapshot % _Alignof(*snapshot) ||
	    !payload_mm_fmp_state_staging_buffer(snapshot, sizeof(*snapshot)) ||
	    payload_mm_authvar_buffers_overlap(snapshot, sizeof(*snapshot),
		&info_authority, sizeof(info_authority)))
		return CB_ERR;
	memset(snapshot, 0, sizeof(*snapshot));
	if (!info_authority.installed || info_authority.busy)
		return CB_ERR;
	policy = info_authority.policy;
	memset(&record, 0, sizeof(record));
	info_authority.busy = true;
	expected = info_authority;
	owner_status = payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE,
		&record);
	authority_corrupted = memcmp(&info_authority, &expected,
		sizeof(expected)) != 0;
	if (authority_corrupted) {
		poison_authority();
		goto out;
	}
	if (owner_status != CB_SUCCESS ||
	    !payload_mm_fmp_owner_record_valid(PAYLOAD_MM_FMP_STATE_KEY_STATE,
		&record) ||
	    !record.present || !record.data[0])
		goto out;
	durable_version = read32(record.data + 4);
	durable_lowest_version = policy.lowest_supported_version;
	if (durable_version < policy.current_version)
		goto out;
	if (record.data[1]) {
		uint32_t recorded_lowest_version = read32(record.data + 8);

		if (recorded_lowest_version > durable_version)
			goto out;
		if (recorded_lowest_version > durable_lowest_version)
			durable_lowest_version = recorded_lowest_version;
	}
	*snapshot = (struct capsule_broker_info_snapshot) {
		.image_type = policy.image_type,
		.hardware_instance = policy.hardware_instance,
		.current_version = policy.current_version,
		.lowest_supported_version = durable_lowest_version,
		.image_size = policy.image_size,
		.capabilities = policy.capabilities,
		.state_flags =
			(record.data[2] ?
			 CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_STATUS_VALID : 0) |
			(record.data[3] ?
			 CAPSULE_BROKER_INFO_STATE_LAST_ATTEMPT_VERSION_VALID : 0),
		.last_attempt_status = record.data[2] ?
			read32(record.data + 12) : 0,
		.last_attempt_version = record.data[3] ?
			read32(record.data + 16) : 0,
	};
	status = CB_SUCCESS;
out:
	if (!authority_corrupted)
		info_authority.busy = false;
	memset(&policy, 0, sizeof(policy));
	memset(&record, 0, sizeof(record));
	if (status != CB_SUCCESS)
		memset(snapshot, 0, sizeof(*snapshot));
	return status;
}

#if ENV_TEST
const void *capsule_broker_info_test_authority(size_t *size)
{
	*size = sizeof(info_authority);
	return &info_authority;
}
#endif
