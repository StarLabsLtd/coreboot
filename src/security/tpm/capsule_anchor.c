/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor.h>
#include <security/tpm/tss2.h>
#include <string.h>

static bool bytes_zero(const uint8_t *bytes, size_t size)
{
	uint8_t value = 0;

	for (size_t i = 0; i < size; i++)
		value |= bytes[i];
	return value == 0;
}

static bool descriptor_valid(
	const struct capsule_tpm_anchor_descriptor *descriptor)
{
	return descriptor->revision == CAPSULE_TPM_ANCHOR_DESCRIPTOR_REVISION &&
		descriptor->size == sizeof(*descriptor) &&
		descriptor->policy_revision == CAPSULE_TPM_ANCHOR_POLICY_REVISION &&
		(descriptor->nv_index & 0xff000000U) == HR_NV_INDEX &&
		descriptor->attributes == CAPSULE_TPM_ANCHOR_ATTRIBUTES &&
		descriptor->name_algorithm == TPM_ALG_SHA256 &&
		descriptor->data_size == CAPSULE_TPM_ANCHOR_SIZE &&
		descriptor->auth_policy_size == CAPSULE_TPM_ANCHOR_POLICY_SIZE &&
		descriptor->authority_name_size ==
			CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE &&
		descriptor->policy_ref_size &&
		descriptor->policy_ref_size <=
			CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE &&
		!descriptor->reserved && !descriptor->reserved2 &&
		!bytes_zero(descriptor->auth_policy,
			sizeof(descriptor->auth_policy)) &&
		descriptor->authority_name[0] == (TPM_ALG_SHA256 >> 8) &&
		descriptor->authority_name[1] == (TPM_ALG_SHA256 & 0xff) &&
		!bytes_zero(descriptor->authority_name + sizeof(uint16_t),
			CAPSULE_TPM_ANCHOR_POLICY_SIZE) &&
		bytes_zero(descriptor->policy_ref + descriptor->policy_ref_size,
			sizeof(descriptor->policy_ref) - descriptor->policy_ref_size);
}

static bool platform_descriptor_valid(
	const struct capsule_tpm_anchor_descriptor *descriptor)
{
	return descriptor->revision == CAPSULE_TPM_ANCHOR_DESCRIPTOR_REVISION &&
		descriptor->size == sizeof(*descriptor) &&
		descriptor->policy_revision ==
			CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION &&
		(descriptor->nv_index & 0xff000000U) == HR_NV_INDEX &&
		descriptor->attributes == CAPSULE_TPM_ANCHOR_PLATFORM_ATTRIBUTES &&
		descriptor->name_algorithm == TPM_ALG_SHA256 &&
		descriptor->data_size == CAPSULE_TPM_ANCHOR_SIZE &&
		!descriptor->auth_policy_size &&
		!descriptor->authority_name_size && !descriptor->policy_ref_size &&
		!descriptor->reserved && !descriptor->reserved2 &&
		bytes_zero(descriptor->auth_policy,
			sizeof(descriptor->auth_policy)) &&
		bytes_zero(descriptor->authority_name,
			sizeof(descriptor->authority_name)) &&
		bytes_zero(descriptor->policy_ref,
			sizeof(descriptor->policy_ref));
}

enum cb_err capsule_tpm_anchor_validate(
	const struct capsule_tpm_anchor_descriptor *descriptor,
	struct capsule_tpm_anchor_binding *binding)
{
	struct capsule_tpm_anchor_descriptor expected;
	struct tlcl2_nv_public public;
	struct capsule_tpm_anchor_binding result;

	if (!binding)
		return CB_ERR_ARG;
	memset(binding, 0, sizeof(*binding));
	if (!descriptor)
		return CB_ERR_ARG;
	memcpy(&expected, descriptor, sizeof(expected));
	if (!descriptor_valid(&expected))
		return CB_ERR;
	memset(&public, 0, sizeof(public));
	if (tlcl2_read_public(expected.nv_index & 0x00ffffffU, &public) !=
		TPM_SUCCESS ||
	    public.index != (expected.nv_index & 0x00ffffffU) ||
	    public.name_alg != expected.name_algorithm ||
	    (public.attributes & ~(CAPSULE_TPM_ANCHOR_WRITTEN |
		CAPSULE_TPM_ANCHOR_WRITELOCKED)) != expected.attributes ||
	    !(public.attributes & CAPSULE_TPM_ANCHOR_WRITTEN) ||
	    public.auth_policy_size != expected.auth_policy_size ||
	    memcmp(public.auth_policy, expected.auth_policy,
		expected.auth_policy_size) != 0 ||
	    public.data_size != expected.data_size)
		return CB_ERR;
	result = (struct capsule_tpm_anchor_binding) {
		.policy_revision = expected.policy_revision,
		.nv_index = expected.nv_index,
		.policy_ref_size = expected.policy_ref_size,
		.write_locked = !!(public.attributes &
			CAPSULE_TPM_ANCHOR_WRITELOCKED),
	};
	memcpy(result.authority_name, expected.authority_name,
		sizeof(result.authority_name));
	memcpy(result.policy_ref, expected.policy_ref, sizeof(result.policy_ref));
	*binding = result;
	return CB_SUCCESS;
}

enum cb_err capsule_tpm_anchor_platform_validate(
	const struct capsule_tpm_anchor_descriptor *descriptor,
	struct capsule_tpm_anchor_binding *binding)
{
	struct capsule_tpm_anchor_descriptor expected;
	struct tlcl2_nv_public public;
	struct capsule_tpm_anchor_binding result;

	if (!binding)
		return CB_ERR_ARG;
	memset(binding, 0, sizeof(*binding));
	if (!descriptor)
		return CB_ERR_ARG;
	memcpy(&expected, descriptor, sizeof(expected));
	if (!platform_descriptor_valid(&expected))
		return CB_ERR;
	memset(&public, 0, sizeof(public));
	if (tlcl2_read_public(expected.nv_index & 0x00ffffffU, &public) !=
		TPM_SUCCESS ||
	    public.index != (expected.nv_index & 0x00ffffffU) ||
	    public.name_alg != expected.name_algorithm ||
	    (public.attributes & ~(CAPSULE_TPM_ANCHOR_WRITTEN |
		CAPSULE_TPM_ANCHOR_WRITELOCKED)) != expected.attributes ||
	    !(public.attributes & CAPSULE_TPM_ANCHOR_WRITTEN) ||
	    public.auth_policy_size ||
	    !bytes_zero(public.auth_policy, sizeof(public.auth_policy)) ||
	    public.data_size != expected.data_size)
		return CB_ERR;
	result = (struct capsule_tpm_anchor_binding) {
		.policy_revision = expected.policy_revision,
		.nv_index = expected.nv_index,
		.write_locked = !!(public.attributes &
			CAPSULE_TPM_ANCHOR_WRITELOCKED),
	};
	if (!capsule_tpm_anchor_platform_binding_valid(&result))
		return CB_ERR;
	*binding = result;
	return CB_SUCCESS;
}
