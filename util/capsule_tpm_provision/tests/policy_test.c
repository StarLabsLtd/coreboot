/* SPDX-License-Identifier: GPL-2.0-only */

#include "../policy.h"

#include <assert.h>
#include <string.h>

static const uint8_t expected_auth_policy[32] = {
	0xb5, 0x5b, 0x16, 0xc9, 0x70, 0x29, 0xaf, 0x43,
	0x7b, 0xaa, 0x7e, 0x5e, 0xf1, 0x78, 0xa0, 0xb8,
	0x60, 0x1c, 0x9d, 0x4e, 0xd0, 0x2c, 0xb5, 0xc3,
	0xe3, 0xee, 0x80, 0x4f, 0x10, 0x65, 0xb8, 0xdd,
};

static const uint8_t expected_write_approved[32] = {
	0x68, 0x17, 0x9f, 0xfb, 0x9f, 0xb3, 0xde, 0x6d,
	0xe1, 0x64, 0x39, 0x3c, 0xa9, 0xca, 0xf2, 0xda,
	0xc0, 0x09, 0x0b, 0x5b, 0xc9, 0xf2, 0xad, 0x0d,
	0x41, 0xcb, 0x76, 0x4e, 0xd4, 0x51, 0x93, 0x0f,
};

static const uint8_t expected_lock_approved[32] = {
	0xc8, 0x46, 0x97, 0xf8, 0xda, 0x5c, 0xd6, 0xd6,
	0xc9, 0xf3, 0xdc, 0x3f, 0x71, 0xef, 0x61, 0xed,
	0x82, 0x48, 0xfd, 0x71, 0xf5, 0xd9, 0x70, 0x4e,
	0x29, 0x81, 0x87, 0xd8, 0x86, 0xd8, 0x12, 0x01,
};

static void fill_input(struct capsule_tpm_provision_input *input,
	size_t modulus_size)
{
	memset(input, 0, sizeof(*input));
	input->nv_index = 0x01001234;
	input->epoch = 1;
	memset(input->manifest_digest, 0x11,
		sizeof(input->manifest_digest));
	input->rsa_modulus_size = modulus_size;
	memset(input->rsa_modulus, 1, modulus_size);
	input->rsa_modulus[0] = 0x80;
	input->rsa_modulus[modulus_size - 1] = 3;
	input->policy_ref_size = 7;
	memcpy(input->policy_ref, "capsule", 7);
}

static void assert_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;

	for (size_t i = 0; i < size; i++)
		assert(bytes[i] == 0);
}

static void assert_rejected(struct capsule_tpm_provision_input *input)
{
	struct capsule_tpm_provision_artifacts output;

	memset(&output, 0xa5, sizeof(output));
	assert(!capsule_tpm_provision_generate(input, &output));
	assert_zero(&output, sizeof(output));
}

static void test_known_vector(void)
{
	struct capsule_tpm_provision_input input;
	struct capsule_tpm_provision_artifacts output;

	fill_input(&input, 256);
	assert(capsule_tpm_provision_generate(&input, &output));
	assert(!memcmp(output.auth_policy, expected_auth_policy,
		sizeof(expected_auth_policy)));
	assert(!memcmp(output.write_approved_policy, expected_write_approved,
		sizeof(expected_write_approved)));
	assert(!memcmp(output.lock_approved_policy, expected_lock_approved,
		sizeof(expected_lock_approved)));
	assert(output.rsa_public_size == 282);
	assert(output.nv_public[8] == 0x42 && output.nv_public[9] == 0x04 &&
		output.nv_public[10] == 0x50 && output.nv_public[11] == 0x08);
	assert(memcmp(output.nv_name, output.written_nv_name,
		sizeof(output.nv_name)));
	assert(memcmp(output.write_branch, output.lock_branch,
		sizeof(output.write_branch)));
	assert(memcmp(output.policy_or_digests,
		output.policy_or_digests + sizeof(output.write_branch),
		sizeof(output.write_branch)) < 0);
}

static void test_modulus_boundaries(void)
{
	struct capsule_tpm_provision_input input;
	struct capsule_tpm_provision_artifacts output;

	for (size_t size = 0; size <= CAPSULE_TPM_PROVISION_RSA_MAX_SIZE;
	     size++) {
		if (size == 256 || size == 384 || size == 512) {
			fill_input(&input, size);
			assert(capsule_tpm_provision_generate(&input, &output));
			continue;
		}
		memset(&input, 0, sizeof(input));
		input.rsa_modulus_size = size;
		assert_rejected(&input);
	}
	fill_input(&input, 256);
	input.rsa_modulus[0] &= 0x7f;
	assert_rejected(&input);
	fill_input(&input, 256);
	input.rsa_modulus[255] &= ~1;
	assert_rejected(&input);
	fill_input(&input, 256);
	input.rsa_modulus[256] = 1;
	assert_rejected(&input);
}

static void test_input_boundaries(void)
{
	struct capsule_tpm_provision_input input;
	struct capsule_tpm_provision_artifacts output;

	assert(!capsule_tpm_provision_generate(NULL, &output));
	assert_zero(&output, sizeof(output));
	fill_input(&input, 256);
	assert(!capsule_tpm_provision_generate(&input, NULL));

	fill_input(&input, 256);
	input.nv_index = 0x02001234;
	assert_rejected(&input);
	fill_input(&input, 256);
	input.nv_index = 0x01000000;
	assert_rejected(&input);
	fill_input(&input, 256);
	input.epoch = 0;
	assert_rejected(&input);
	fill_input(&input, 256);
	memset(input.manifest_digest, 0, sizeof(input.manifest_digest));
	assert_rejected(&input);
	fill_input(&input, 256);
	input.policy_ref_size = 0;
	assert_rejected(&input);
	fill_input(&input, 256);
	input.policy_ref_size = CAPSULE_TPM_PROVISION_POLICY_REF_MAX_SIZE + 1;
	assert_rejected(&input);
	fill_input(&input, 256);
	input.policy_ref[7] = 1;
	assert_rejected(&input);
}

static void test_every_input_is_bound(void)
{
	struct capsule_tpm_provision_input input;
	struct capsule_tpm_provision_artifacts baseline;
	struct capsule_tpm_provision_artifacts changed;

	fill_input(&input, 256);
	assert(capsule_tpm_provision_generate(&input, &baseline));
	input.nv_index++;
	assert(capsule_tpm_provision_generate(&input, &changed));
	assert(memcmp(&baseline, &changed, sizeof(baseline)));
	fill_input(&input, 256);
	input.epoch++;
	assert(capsule_tpm_provision_generate(&input, &changed));
	assert(memcmp(&baseline, &changed, sizeof(baseline)));
	fill_input(&input, 256);
	input.manifest_digest[0] ^= 1;
	assert(capsule_tpm_provision_generate(&input, &changed));
	assert(memcmp(&baseline, &changed, sizeof(baseline)));
	fill_input(&input, 256);
	input.rsa_modulus[1] ^= 1;
	assert(capsule_tpm_provision_generate(&input, &changed));
	assert(memcmp(&baseline, &changed, sizeof(baseline)));
	fill_input(&input, 256);
	input.policy_ref[0] ^= 1;
	assert(capsule_tpm_provision_generate(&input, &changed));
	assert(memcmp(&baseline, &changed, sizeof(baseline)));
}

int main(void)
{
	test_known_vector();
	test_modulus_boundaries();
	test_input_boundaries();
	test_every_input_is_bound();
	return 0;
}
