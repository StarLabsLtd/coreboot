/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor.h>
#include <security/tpm/tss2.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

enum public_fault {
	PUBLIC_OK,
	PUBLIC_ERROR,
	PUBLIC_TRUNCATED,
	PUBLIC_INDEX,
	PUBLIC_ALGORITHM,
	PUBLIC_ATTRIBUTES,
	PUBLIC_POLICY_SIZE,
	PUBLIC_POLICY,
	PUBLIC_DATA_SIZE,
	PUBLIC_DIRTY_ERROR,
};

static enum public_fault public_fault;
static unsigned int read_public_calls;
static uint32_t public_attribute_xor;
static bool platform_public;

static struct capsule_tpm_anchor_descriptor valid_descriptor(void)
{
	struct capsule_tpm_anchor_descriptor descriptor = {
		.revision = CAPSULE_TPM_ANCHOR_DESCRIPTOR_REVISION,
		.size = sizeof(descriptor),
		.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION,
		.nv_index = HR_NV_INDEX | 0x1234,
		.attributes = CAPSULE_TPM_ANCHOR_ATTRIBUTES,
		.name_algorithm = TPM_ALG_SHA256,
		.data_size = CAPSULE_TPM_ANCHOR_SIZE,
		.auth_policy_size = CAPSULE_TPM_ANCHOR_POLICY_SIZE,
		.authority_name_size = CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE,
		.policy_ref_size = 7,
		.authority_name = { 0, TPM_ALG_SHA256 },
		.policy_ref = { 'c', 'a', 'p', 's', 'u', 'l', 'e' },
	};

	for (size_t i = 0; i < sizeof(descriptor.auth_policy); i++)
		descriptor.auth_policy[i] = 0x20 + i;
	for (size_t i = 2; i < sizeof(descriptor.authority_name); i++)
		descriptor.authority_name[i] = 0x60 + i;
	return descriptor;
}

static struct capsule_tpm_anchor_descriptor valid_platform_descriptor(void)
{
	return (struct capsule_tpm_anchor_descriptor) {
		.revision = CAPSULE_TPM_ANCHOR_DESCRIPTOR_REVISION,
		.size = sizeof(struct capsule_tpm_anchor_descriptor),
		.policy_revision = CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION,
		.nv_index = HR_NV_INDEX | 0x1234,
		.attributes = CAPSULE_TPM_ANCHOR_PLATFORM_ATTRIBUTES,
		.name_algorithm = TPM_ALG_SHA256,
		.data_size = CAPSULE_TPM_ANCHOR_SIZE,
	};
}

tpm_result_t tlcl2_read_public(uint32_t index, struct tlcl2_nv_public *public)
{
	struct capsule_tpm_anchor_descriptor descriptor = platform_public ?
		valid_platform_descriptor() : valid_descriptor();

	read_public_calls++;
	memset(public, public_fault == PUBLIC_DIRTY_ERROR ? 0xa5 : 0,
		sizeof(*public));
	if (public_fault == PUBLIC_ERROR || public_fault == PUBLIC_TRUNCATED ||
	    public_fault == PUBLIC_DIRTY_ERROR)
		return TPM_CB_READ_FAILURE;
	public->index = index;
	public->name_alg = descriptor.name_algorithm;
	public->attributes = descriptor.attributes |
		CAPSULE_TPM_ANCHOR_WRITTEN;
	public->attributes ^= public_attribute_xor;
	public->auth_policy_size = descriptor.auth_policy_size;
	memcpy(public->auth_policy, descriptor.auth_policy,
		descriptor.auth_policy_size);
	public->data_size = descriptor.data_size;
	switch (public_fault) {
	case PUBLIC_INDEX:
		public->index++;
		break;
	case PUBLIC_ALGORITHM:
		public->name_alg = TPM_ALG_SHA384;
		break;
	case PUBLIC_ATTRIBUTES:
		public->attributes ^= BIT(2);
		break;
	case PUBLIC_POLICY_SIZE:
		public->auth_policy_size--;
		break;
	case PUBLIC_POLICY:
		public->auth_policy[31] ^= 1;
		break;
	case PUBLIC_DATA_SIZE:
		public->data_size--;
		break;
	default:
		break;
	}
	return TPM_SUCCESS;
}

static bool bytes_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	uint8_t value = 0;

	for (size_t i = 0; i < size; i++)
		value |= bytes[i];
	return value == 0;
}

static void expect_failure(struct capsule_tpm_anchor_descriptor *descriptor,
	bool reads_public)
{
	struct capsule_tpm_anchor_binding binding;

	memset(&binding, 0xa5, sizeof(binding));
	read_public_calls = 0;
	CHECK(capsule_tpm_anchor_validate(descriptor, &binding) == CB_ERR);
	CHECK(bytes_zero(&binding, sizeof(binding)));
	CHECK(read_public_calls == reads_public);
}

static void test_success(void)
{
	struct capsule_tpm_anchor_descriptor descriptor = valid_descriptor();
	struct capsule_tpm_anchor_binding binding;

	public_fault = PUBLIC_OK;
	for (unsigned int locked = 0; locked <= 1; locked++) {
		public_attribute_xor = locked ?
			CAPSULE_TPM_ANCHOR_WRITELOCKED : 0;
		memset(&binding, 0xa5, sizeof(binding));
		CHECK(capsule_tpm_anchor_validate(&descriptor, &binding) ==
			CB_SUCCESS);
		CHECK(binding.policy_revision == descriptor.policy_revision);
		CHECK(binding.nv_index == descriptor.nv_index);
		CHECK(binding.policy_ref_size == descriptor.policy_ref_size);
		CHECK(binding.write_locked == locked);
		CHECK(bytes_zero(binding.reserved, sizeof(binding.reserved)));
		CHECK(!memcmp(binding.authority_name,
			descriptor.authority_name,
			sizeof(binding.authority_name)));
		CHECK(!memcmp(binding.policy_ref, descriptor.policy_ref,
			sizeof(binding.policy_ref)));
	}
}

static void test_arguments(void)
{
	struct capsule_tpm_anchor_descriptor descriptor = valid_descriptor();
	struct capsule_tpm_anchor_binding binding;

	memset(&binding, 0xa5, sizeof(binding));
	CHECK(capsule_tpm_anchor_validate(NULL, &binding) == CB_ERR_ARG);
	CHECK(bytes_zero(&binding, sizeof(binding)));
	CHECK(capsule_tpm_anchor_validate(&descriptor, NULL) == CB_ERR_ARG);
}

static void test_descriptor_shape(void)
{
	struct capsule_tpm_anchor_descriptor descriptor;

#define BAD_FIELD(field, value) do { \
	descriptor = valid_descriptor(); \
	descriptor.field = (value); \
	expect_failure(&descriptor, false); \
} while (0)

	public_fault = PUBLIC_OK;
	public_attribute_xor = 0;
	BAD_FIELD(revision, 0);
	BAD_FIELD(revision, 2);
	BAD_FIELD(size, sizeof(descriptor) - 1);
	BAD_FIELD(size, sizeof(descriptor) + 1);
	BAD_FIELD(policy_revision, 0);
	BAD_FIELD(policy_revision, 2);
	BAD_FIELD(nv_index, 0x00123456);
	BAD_FIELD(nv_index, 0x02123456);
	BAD_FIELD(attributes, CAPSULE_TPM_ANCHOR_ATTRIBUTES | BIT(0));
	BAD_FIELD(attributes, CAPSULE_TPM_ANCHOR_ATTRIBUTES & ~BIT(3));
	BAD_FIELD(name_algorithm, TPM_ALG_SHA384);
	BAD_FIELD(data_size, CAPSULE_TPM_ANCHOR_SIZE - 1);
	BAD_FIELD(data_size, CAPSULE_TPM_ANCHOR_SIZE + 1);
	BAD_FIELD(auth_policy_size, CAPSULE_TPM_ANCHOR_POLICY_SIZE - 1);
	BAD_FIELD(auth_policy_size, CAPSULE_TPM_ANCHOR_POLICY_SIZE + 1);
	BAD_FIELD(authority_name_size,
		CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE - 1);
	BAD_FIELD(authority_name_size,
		CAPSULE_TPM_ANCHOR_AUTHORITY_NAME_SIZE + 1);
	BAD_FIELD(policy_ref_size, 0);
	BAD_FIELD(policy_ref_size, CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE + 1);
	BAD_FIELD(reserved, 1);
	BAD_FIELD(reserved2, 1);
	for (size_t bit = 0; bit < 32; bit++) {
		descriptor = valid_descriptor();
		descriptor.attributes ^= BIT(bit);
		expect_failure(&descriptor, false);
	}

	descriptor = valid_descriptor();
	memset(descriptor.auth_policy, 0, sizeof(descriptor.auth_policy));
	expect_failure(&descriptor, false);
	descriptor = valid_descriptor();
	descriptor.authority_name[0] = 1;
	expect_failure(&descriptor, false);
	descriptor = valid_descriptor();
	descriptor.authority_name[1]++;
	expect_failure(&descriptor, false);
	descriptor = valid_descriptor();
	memset(descriptor.authority_name + 2, 0,
		sizeof(descriptor.authority_name) - 2);
	expect_failure(&descriptor, false);
	descriptor = valid_descriptor();
	descriptor.policy_ref[descriptor.policy_ref_size] = 1;
	expect_failure(&descriptor, false);
#undef BAD_FIELD
}

static void test_public_mutations(void)
{
	struct capsule_tpm_anchor_descriptor descriptor = valid_descriptor();

	public_attribute_xor = 0;
	for (public_fault = PUBLIC_ERROR; public_fault <= PUBLIC_DIRTY_ERROR;
	     public_fault++)
		expect_failure(&descriptor, true);
}

static void test_public_attribute_lifecycle(void)
{
	struct capsule_tpm_anchor_descriptor descriptor = valid_descriptor();

	public_fault = PUBLIC_OK;
	public_attribute_xor = CAPSULE_TPM_ANCHOR_WRITTEN;
	expect_failure(&descriptor, true);
	public_attribute_xor = BIT(28);
	expect_failure(&descriptor, true);
	for (size_t bit = 0; bit < 32; bit++) {
		if (bit == 11 || bit == 29)
			continue;
		public_attribute_xor = BIT(bit);
		expect_failure(&descriptor, true);
	}
	public_attribute_xor = 0;
}

static void test_exact_public_bytes(void)
{
	struct capsule_tpm_anchor_descriptor descriptor = valid_descriptor();

	public_fault = PUBLIC_OK;
	public_attribute_xor = 0;
	for (size_t i = 0; i < sizeof(descriptor.auth_policy); i++) {
		descriptor = valid_descriptor();
		descriptor.auth_policy[i] ^= 1;
		expect_failure(&descriptor, true);
	}
}

static void test_platform_mode(void)
{
	struct capsule_tpm_anchor_descriptor descriptor =
		valid_platform_descriptor();
	struct capsule_tpm_anchor_binding binding;
	struct capsule_tpm_anchor_descriptor legacy = valid_descriptor();

	platform_public = true;
	public_fault = PUBLIC_OK;
	for (unsigned int locked = 0; locked <= 1; locked++) {
		public_attribute_xor = locked ? CAPSULE_TPM_ANCHOR_WRITELOCKED : 0;
		CHECK(capsule_tpm_anchor_platform_validate(&descriptor, &binding) ==
			CB_SUCCESS);
		CHECK(binding.policy_revision ==
			CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION);
		CHECK(binding.nv_index == descriptor.nv_index);
		CHECK(binding.write_locked == locked);
		CHECK(bytes_zero(binding.authority_name,
			sizeof(binding.authority_name)));
		CHECK(bytes_zero(binding.policy_ref, sizeof(binding.policy_ref)));
	}
	read_public_calls = 0;
	CHECK(capsule_tpm_anchor_validate(&descriptor, &binding) == CB_ERR);
	CHECK(read_public_calls == 0);
	CHECK(capsule_tpm_anchor_platform_validate(&legacy, &binding) == CB_ERR);
	CHECK(read_public_calls == 0);

#define BAD_PLATFORM(field, value) do { \
	descriptor = valid_platform_descriptor(); \
	descriptor.field = (value); \
	CHECK(capsule_tpm_anchor_platform_validate(&descriptor, &binding) == \
		CB_ERR); \
} while (0)
	BAD_PLATFORM(policy_revision, CAPSULE_TPM_ANCHOR_POLICY_REVISION);
	BAD_PLATFORM(attributes, CAPSULE_TPM_ANCHOR_PLATFORM_ATTRIBUTES | BIT(11));
	BAD_PLATFORM(attributes, CAPSULE_TPM_ANCHOR_PLATFORM_ATTRIBUTES | BIT(29));
	BAD_PLATFORM(auth_policy_size, 1);
	BAD_PLATFORM(authority_name_size, 1);
	BAD_PLATFORM(policy_ref_size, 1);
#undef BAD_PLATFORM
	descriptor = valid_platform_descriptor();
	descriptor.auth_policy[31] = 1;
	CHECK(capsule_tpm_anchor_platform_validate(&descriptor, &binding) == CB_ERR);
	descriptor = valid_platform_descriptor();
	descriptor.authority_name[33] = 1;
	CHECK(capsule_tpm_anchor_platform_validate(&descriptor, &binding) == CB_ERR);
	descriptor = valid_platform_descriptor();
	descriptor.policy_ref[31] = 1;
	CHECK(capsule_tpm_anchor_platform_validate(&descriptor, &binding) == CB_ERR);

	descriptor = valid_platform_descriptor();
	public_attribute_xor = BIT(2);
	CHECK(capsule_tpm_anchor_platform_validate(&descriptor, &binding) == CB_ERR);
	public_attribute_xor = 0;
	public_fault = PUBLIC_POLICY;
	CHECK(capsule_tpm_anchor_platform_validate(&descriptor, &binding) == CB_ERR);
	public_fault = PUBLIC_OK;
	platform_public = false;
}

int main(void)
{
	test_success();
	test_arguments();
	test_descriptor_shape();
	test_public_mutations();
	test_public_attribute_lifecycle();
	test_exact_public_bytes();
	test_platform_mode();
	return 0;
}
