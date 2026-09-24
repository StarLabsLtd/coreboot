/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_grant.h>
#include <limits.h>
#include <string.h>

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	for (size_t i = 0; i < size; i++)
		value |= bytes[i];
	return value == 0;
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && !(base % alignment) && size &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool add_u64(uint64_t *total, uint64_t value)
{
	if (*total > UINT64_MAX - value)
		return false;
	*total += value;
	return true;
}

static bool identity_valid(const uint8_t identity[32])
{
	return !bytes_zero(identity, 32);
}

static enum cb_err grant_snapshot_valid(
	const struct payload_mm_authvar_mor_grant *snapshot)
{
	uint64_t cleared_bytes = 0;
	uint64_t excluded_bytes = 0;
	uint64_t previous_end = 0;
	uint32_t cleared_spans = 0;
	uint32_t excluded_spans = 0;

	if (snapshot->revision != PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION ||
	    snapshot->size != sizeof(*snapshot) || !snapshot->cold_boot_generation ||
	    snapshot->entry.present != 1 || !(snapshot->entry.value & 1U) ||
	    snapshot->entry.reserved ||
	    snapshot->flags != PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS ||
	    !snapshot->dma_policy_generation ||
	    !identity_valid(snapshot->dma_policy_identity) ||
	    !snapshot->inventory_generation ||
	    !identity_valid(snapshot->inventory_identity) ||
	    !snapshot->total_bytes ||
	    (!snapshot->cleared_bytes || !snapshot->cleared_spans) ||
	    !snapshot->total_spans ||
	    snapshot->total_spans > PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS ||
	    snapshot->reserved)
		return CB_ERR;

	for (size_t i = 0; i < snapshot->total_spans; i++) {
		const struct payload_mm_authvar_mor_grant_span *span = &snapshot->spans[i];
		uint64_t end;

		if (!span->size || span->base > UINT64_MAX - span->size)
			return CB_ERR;
		end = span->base + span->size;
		if (i && (span->base < previous_end ||
		    (span->base == previous_end &&
			     span->span_class == snapshot->spans[i - 1].span_class &&
			     span->exclusion_reason ==
				snapshot->spans[i - 1].exclusion_reason)))
			return CB_ERR;
		previous_end = end;
		if (span->span_class == PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED) {
			if (span->exclusion_reason !=
			    PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE ||
			    !add_u64(&cleared_bytes, span->size))
				return CB_ERR;
			cleared_spans++;
		} else if (span->span_class ==
			   PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED) {
			if (span->exclusion_reason <
			    PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE ||
			    span->exclusion_reason >
			    PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED ||
			    !add_u64(&excluded_bytes, span->size))
				return CB_ERR;
			excluded_spans++;
		} else {
			return CB_ERR;
		}
	}
	for (size_t i = snapshot->total_spans;
	     i < PAYLOAD_MM_AUTHVAR_MOR_GRANT_MAX_SPANS; i++) {
		if (!bytes_zero(&snapshot->spans[i], sizeof(snapshot->spans[i])))
			return CB_ERR;
	}
	if (cleared_bytes != snapshot->cleared_bytes ||
	    excluded_bytes != snapshot->excluded_bytes ||
	    cleared_spans != snapshot->cleared_spans ||
	    excluded_spans != snapshot->excluded_spans ||
	    cleared_spans + excluded_spans != snapshot->total_spans ||
	    cleared_bytes > UINT64_MAX - excluded_bytes ||
	    cleared_bytes + excluded_bytes != snapshot->total_bytes)
		return CB_ERR;
	return CB_SUCCESS;
}

#if !ENV_SMM || ENV_TEST
enum cb_err payload_mm_authvar_mor_grant_validate(
	const struct payload_mm_authvar_mor_grant *grant)
{
	struct payload_mm_authvar_mor_grant snapshot;

	if (!object_valid(grant, sizeof(*grant), _Alignof(*grant)))
		return CB_ERR_ARG;
	memcpy(&snapshot, grant, sizeof(snapshot));
	if (grant_snapshot_valid(&snapshot) != CB_SUCCESS ||
	    memcmp(&snapshot, grant, sizeof(snapshot)))
		return CB_ERR;
	return CB_SUCCESS;
}
#endif

#if ENV_SMM || ENV_TEST

static struct {
	struct payload_mm_authvar_mor_grant grant;
	struct payload_mm_authvar_mor_grant candidate;
	bool installed;
	bool install_attempted;
	bool consumed;
	bool poisoned;
} authority;

static enum cb_err install_fail(void)
{
	memset(&authority, 0, sizeof(authority));
	authority.install_attempted = true;
	authority.poisoned = true;
	return CB_ERR;
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	const uintptr_t left_base = (uintptr_t)left;
	const uintptr_t right_base = (uintptr_t)right;

	if (!object_valid(left, left_size, 1) ||
	    !object_valid(right, right_size, 1))
		return true;
	if (left_base <= right_base)
		return right_base - left_base < left_size;
	return left_base - right_base < right_size;
}

enum cb_err payload_mm_authvar_mor_grant_install(
	const struct payload_mm_authvar_mor_grant *trusted_grant,
	payload_mm_authvar_mor_grant_protected_storage storage_is_protected,
	void *context)
{
	bool protected;

	if (authority.install_attempted)
		return CB_ERR;
	authority.install_attempted = true;
	if (!object_valid(trusted_grant, sizeof(*trusted_grant),
		_Alignof(*trusted_grant)) || !storage_is_protected ||
	    ranges_overlap(trusted_grant, sizeof(*trusted_grant), &authority,
		 sizeof(authority)))
		return install_fail();
	memcpy(&authority.candidate, trusted_grant, sizeof(authority.candidate));
	if (grant_snapshot_valid(&authority.candidate) != CB_SUCCESS)
		return install_fail();
	protected = storage_is_protected(context, &authority, sizeof(authority));
	if (!protected || !authority.install_attempted || authority.installed ||
	    authority.consumed || authority.poisoned ||
	    !bytes_zero(&authority.grant, sizeof(authority.grant)) ||
	    grant_snapshot_valid(&authority.candidate) != CB_SUCCESS ||
	    memcmp(&authority.candidate, trusted_grant,
		 sizeof(authority.candidate))) {
		return install_fail();
	}
	authority.grant = authority.candidate;
	memset(&authority.candidate, 0, sizeof(authority.candidate));
	authority.installed = true;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_mor_grant_close(void)
{
	if (authority.install_attempted || authority.poisoned)
		return CB_ERR;
	authority.install_attempted = true;
	authority.poisoned = true;
	memset(&authority.grant, 0, sizeof(authority.grant));
	memset(&authority.candidate, 0, sizeof(authority.candidate));
	return CB_SUCCESS;
}

bool payload_mm_authvar_mor_grant_ready(void)
{
	return authority.installed && !authority.consumed && !authority.poisoned;
}

static enum cb_err consume_finish(bool valid)
{
	memset(&authority.grant, 0, sizeof(authority.grant));
	memset(&authority.candidate, 0, sizeof(authority.candidate));
	if (!valid)
		authority.poisoned = true;
	return valid ? CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_authvar_mor_grant_consume(
	const struct payload_mm_authvar_mor_grant *expected_grant)
{
	bool valid;

	if (!payload_mm_authvar_mor_grant_ready())
		return CB_ERR;
	authority.consumed = true;
	if (!object_valid(expected_grant, sizeof(*expected_grant),
		_Alignof(*expected_grant)) ||
	    ranges_overlap(expected_grant, sizeof(*expected_grant), &authority,
		 sizeof(authority)))
		return consume_finish(false);
	memcpy(&authority.candidate, expected_grant, sizeof(authority.candidate));
	valid = grant_snapshot_valid(&authority.candidate) == CB_SUCCESS &&
		!memcmp(&authority.candidate, &authority.grant,
			sizeof(authority.candidate)) &&
		!memcmp(expected_grant, &authority.candidate,
			sizeof(authority.candidate));
	return consume_finish(valid);
}

#endif
