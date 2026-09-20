/* SPDX-License-Identifier: GPL-2.0-only */

#include "policy.h"

#include <openssl/evp.h>
#include <string.h>

#define TPM_ALG_RSA 0x0001U
#define TPM_ALG_SHA256 0x000bU
#define TPM_ALG_NULL 0x0010U
#define TPM_ALG_RSASSA 0x0014U

#define TPM_CC_NV_WRITE 0x00000137U
#define TPM_CC_NV_WRITE_LOCK 0x00000138U
#define TPM_CC_POLICY_OR 0x00000171U
#define TPM_CC_POLICY_COMMAND_CODE 0x0000016cU
#define TPM_CC_POLICY_CP_HASH 0x0000016eU
#define TPM_CC_POLICY_AUTHORIZE 0x0000016aU
#define TPM_CC_POLICY_NV_WRITTEN 0x0000018fU

#define TPM_OBJECT_NODA (1U << 10)
#define TPM_OBJECT_SIGN (1U << 18)
#define TPM_NV_INDEX_TYPE 0x01000000U
#define TPM_NV_INDEX_MASK 0xff000000U

#define CAPSULE_TPM_ANCHOR_ATTRIBUTES \
	((1U << 3) | (1U << 12) | (1U << 14) | (1U << 18) | \
	 (1U << 25) | (1U << 30))
#define CAPSULE_TPM_ANCHOR_WRITTEN (1U << 29)

struct byte_buffer {
	uint8_t *bytes;
	size_t capacity;
	size_t size;
};

static void secure_clear(void *data, size_t size)
{
	volatile uint8_t *byte = data;

	while (size--)
		*byte++ = 0;
}

static bool bytes_zero(const uint8_t *bytes, size_t size)
{
	uint8_t value = 0;

	for (size_t i = 0; i < size; i++)
		value |= bytes[i];
	return value == 0;
}

static bool append_bytes(struct byte_buffer *buffer, const void *bytes,
	size_t size)
{
	if (size > buffer->capacity - buffer->size)
		return false;
	memcpy(buffer->bytes + buffer->size, bytes, size);
	buffer->size += size;
	return true;
}

static bool append_be16(struct byte_buffer *buffer, uint16_t value)
{
	const uint8_t bytes[] = { value >> 8, value };

	return append_bytes(buffer, bytes, sizeof(bytes));
}

static bool append_be32(struct byte_buffer *buffer, uint32_t value)
{
	const uint8_t bytes[] = {
		value >> 24, value >> 16, value >> 8, value,
	};

	return append_bytes(buffer, bytes, sizeof(bytes));
}

static void write_le16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = value;
	bytes[1] = value >> 8;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = value >> 24;
	bytes[1] = value >> 16;
	bytes[2] = value >> 8;
	bytes[3] = value;
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = value;
	bytes[1] = value >> 8;
	bytes[2] = value >> 16;
	bytes[3] = value >> 24;
}

static void write_le64(uint8_t *bytes, uint64_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		bytes[i] = value >> (i * 8);
}

static bool sha256_spans(const void *const *data, const size_t *sizes,
	size_t count, uint8_t digest[CAPSULE_TPM_PROVISION_DIGEST_SIZE])
{
	EVP_MD_CTX *context;
	unsigned int digest_size = 0;
	bool result = false;

	context = EVP_MD_CTX_new();
	if (!context) {
		secure_clear(digest, CAPSULE_TPM_PROVISION_DIGEST_SIZE);
		return false;
	}
	if (EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1)
		goto out;
	for (size_t i = 0; i < count; i++) {
		if (sizes[i] && !data[i])
			goto out;
		if (sizes[i] &&
		    EVP_DigestUpdate(context, data[i], sizes[i]) != 1)
			goto out;
	}
	if (EVP_DigestFinal_ex(context, digest, &digest_size) != 1 ||
	    digest_size != CAPSULE_TPM_PROVISION_DIGEST_SIZE)
		goto out;
	result = true;
out:
	EVP_MD_CTX_free(context);
	if (!result)
		secure_clear(digest, CAPSULE_TPM_PROVISION_DIGEST_SIZE);
	return result;
}

static bool sha256(const void *data, size_t size,
	uint8_t digest[CAPSULE_TPM_PROVISION_DIGEST_SIZE])
{
	return sha256_spans(&data, &size, 1, digest);
}

static bool input_valid(const struct capsule_tpm_provision_input *input)
{
	if (!input || (input->nv_index & TPM_NV_INDEX_MASK) !=
		TPM_NV_INDEX_TYPE || !(input->nv_index & ~TPM_NV_INDEX_MASK) ||
	    !input->epoch || bytes_zero(input->manifest_digest,
		CAPSULE_TPM_PROVISION_DIGEST_SIZE) ||
	    (input->rsa_modulus_size != 256 &&
	     input->rsa_modulus_size != 384 &&
	     input->rsa_modulus_size != 512) ||
	    !(input->rsa_modulus[0] & 0x80) ||
	    !(input->rsa_modulus[input->rsa_modulus_size - 1] & 1) ||
	    !input->policy_ref_size ||
	    input->policy_ref_size > CAPSULE_TPM_PROVISION_POLICY_REF_MAX_SIZE ||
	    !bytes_zero(input->rsa_modulus + input->rsa_modulus_size,
		CAPSULE_TPM_PROVISION_RSA_MAX_SIZE - input->rsa_modulus_size) ||
	    !bytes_zero(input->policy_ref + input->policy_ref_size,
		CAPSULE_TPM_PROVISION_POLICY_REF_MAX_SIZE -
		input->policy_ref_size))
		return false;
	return true;
}

static bool make_rsa_public(const struct capsule_tpm_provision_input *input,
	struct capsule_tpm_provision_artifacts *artifacts)
{
	struct byte_buffer public = {
		.bytes = artifacts->rsa_public + sizeof(uint16_t),
		.capacity = sizeof(artifacts->rsa_public) - sizeof(uint16_t),
	};
	uint8_t digest[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	bool result = false;

	if (!append_be16(&public, TPM_ALG_RSA) ||
	    !append_be16(&public, TPM_ALG_SHA256) ||
	    !append_be32(&public, TPM_OBJECT_NODA | TPM_OBJECT_SIGN) ||
	    !append_be16(&public, 0) || !append_be16(&public, TPM_ALG_NULL) ||
	    !append_be16(&public, TPM_ALG_RSASSA) ||
	    !append_be16(&public, TPM_ALG_SHA256) ||
	    !append_be16(&public, input->rsa_modulus_size * 8) ||
	    !append_be32(&public, 0) ||
	    !append_be16(&public, input->rsa_modulus_size) ||
	    !append_bytes(&public, input->rsa_modulus,
		input->rsa_modulus_size) || public.size > UINT16_MAX ||
	    !sha256(public.bytes, public.size, digest))
		goto out;
	artifacts->rsa_public[0] = public.size >> 8;
	artifacts->rsa_public[1] = public.size;
	artifacts->rsa_public_size = public.size + sizeof(uint16_t);
	artifacts->authority_name[0] = TPM_ALG_SHA256 >> 8;
	artifacts->authority_name[1] = TPM_ALG_SHA256;
	memcpy(artifacts->authority_name + sizeof(uint16_t), digest,
		sizeof(digest));
	result = true;
out:
	secure_clear(digest, sizeof(digest));
	return result;
}

static bool policy_update(const uint8_t old[CAPSULE_TPM_PROVISION_DIGEST_SIZE],
	uint32_t command_code, const void *argument, size_t argument_size,
	uint8_t digest[CAPSULE_TPM_PROVISION_DIGEST_SIZE])
{
	const uint8_t command[] = {
		command_code >> 24, command_code >> 16,
		command_code >> 8, command_code,
	};
	const void *spans[] = { old, command, argument };
	const size_t sizes[] = {
		CAPSULE_TPM_PROVISION_DIGEST_SIZE, sizeof(command), argument_size,
	};

	return sha256_spans(spans, sizes, 3, digest);
}

static bool make_auth_policy(
	const struct capsule_tpm_provision_input *input,
	struct capsule_tpm_provision_artifacts *artifacts)
{
	const uint8_t zero[CAPSULE_TPM_PROVISION_DIGEST_SIZE] = { 0 };
	uint8_t authorized[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	uint8_t command[sizeof(uint32_t)];
	bool result = false;

	if (!policy_update(zero, TPM_CC_POLICY_AUTHORIZE,
		artifacts->authority_name, sizeof(artifacts->authority_name),
		authorized))
		goto out;
	{
		const void *spans[] = { authorized, input->policy_ref };
		const size_t sizes[] = {
			sizeof(authorized), input->policy_ref_size,
		};

		if (!sha256_spans(spans, sizes, 2, authorized))
			goto out;
	}
	write_be32(command, TPM_CC_NV_WRITE);
	if (!policy_update(authorized, TPM_CC_POLICY_COMMAND_CODE, command,
		sizeof(command), artifacts->write_branch))
		goto out;
	write_be32(command, TPM_CC_NV_WRITE_LOCK);
	if (!policy_update(authorized, TPM_CC_POLICY_COMMAND_CODE, command,
		sizeof(command), artifacts->lock_branch))
		goto out;
	if (memcmp(artifacts->write_branch, artifacts->lock_branch,
		sizeof(artifacts->write_branch)) < 0) {
		memcpy(artifacts->policy_or_digests, artifacts->write_branch,
			sizeof(artifacts->write_branch));
		memcpy(artifacts->policy_or_digests +
			sizeof(artifacts->write_branch), artifacts->lock_branch,
			sizeof(artifacts->lock_branch));
	} else {
		memcpy(artifacts->policy_or_digests, artifacts->lock_branch,
			sizeof(artifacts->lock_branch));
		memcpy(artifacts->policy_or_digests +
			sizeof(artifacts->lock_branch), artifacts->write_branch,
			sizeof(artifacts->write_branch));
	}
	if (!policy_update(zero, TPM_CC_POLICY_OR,
		artifacts->policy_or_digests,
		sizeof(artifacts->policy_or_digests),
		artifacts->auth_policy))
		goto out;
	result = true;
out:
	secure_clear(authorized, sizeof(authorized));
	return result;
}

static bool make_nv_public(const struct capsule_tpm_provision_input *input,
	struct capsule_tpm_provision_artifacts *artifacts)
{
	struct byte_buffer public = {
		.bytes = artifacts->nv_public + sizeof(uint16_t),
		.capacity = sizeof(artifacts->nv_public) - sizeof(uint16_t),
	};
	uint8_t digest[CAPSULE_TPM_PROVISION_DIGEST_SIZE];
	bool result = false;

	if (!append_be32(&public, input->nv_index) ||
	    !append_be16(&public, TPM_ALG_SHA256) ||
	    !append_be32(&public, CAPSULE_TPM_ANCHOR_ATTRIBUTES) ||
	    !append_be16(&public, sizeof(artifacts->auth_policy)) ||
	    !append_bytes(&public, artifacts->auth_policy,
		sizeof(artifacts->auth_policy)) ||
	    !append_be16(&public, CAPSULE_TPM_PROVISION_ANCHOR_SIZE) ||
	    public.size + sizeof(uint16_t) != sizeof(artifacts->nv_public) ||
	    !sha256(public.bytes, public.size, digest))
		goto out;
	artifacts->nv_public[0] = public.size >> 8;
	artifacts->nv_public[1] = public.size;
	artifacts->nv_name[0] = TPM_ALG_SHA256 >> 8;
	artifacts->nv_name[1] = TPM_ALG_SHA256;
	memcpy(artifacts->nv_name + sizeof(uint16_t), digest, sizeof(digest));
	write_be32(public.bytes + 6, CAPSULE_TPM_ANCHOR_ATTRIBUTES |
		CAPSULE_TPM_ANCHOR_WRITTEN);
	if (!sha256(public.bytes, public.size, digest))
		goto out;
	artifacts->written_nv_name[0] = TPM_ALG_SHA256 >> 8;
	artifacts->written_nv_name[1] = TPM_ALG_SHA256;
	memcpy(artifacts->written_nv_name + sizeof(uint16_t), digest,
		sizeof(digest));
	write_be32(public.bytes + 6, CAPSULE_TPM_ANCHOR_ATTRIBUTES);
	result = true;
out:
	secure_clear(digest, sizeof(digest));
	return result;
}

static bool make_cp_hash(uint32_t command_code,
	const uint8_t nv_name[CAPSULE_TPM_PROVISION_NAME_SIZE],
	const void *parameters, size_t parameter_size,
	uint8_t digest[CAPSULE_TPM_PROVISION_DIGEST_SIZE])
{
	const uint8_t command[] = {
		command_code >> 24, command_code >> 16,
		command_code >> 8, command_code,
	};
	const void *spans[] = {
		command, nv_name, nv_name, parameters,
	};
	const size_t sizes[] = {
		sizeof(command), CAPSULE_TPM_PROVISION_NAME_SIZE,
		CAPSULE_TPM_PROVISION_NAME_SIZE, parameter_size,
	};

	return sha256_spans(spans, sizes, 4, digest);
}

static bool make_initial_authorization(bool written,
	const uint8_t cp_hash[CAPSULE_TPM_PROVISION_DIGEST_SIZE],
	const struct capsule_tpm_provision_input *input,
	uint8_t approved[CAPSULE_TPM_PROVISION_DIGEST_SIZE],
	uint8_t authorization_hash[CAPSULE_TPM_PROVISION_DIGEST_SIZE])
{
	const uint8_t zero[CAPSULE_TPM_PROVISION_DIGEST_SIZE] = { 0 };
	const uint8_t written_value = written;
	const void *spans[] = { approved, input->policy_ref };
	const size_t sizes[] = {
		CAPSULE_TPM_PROVISION_DIGEST_SIZE, input->policy_ref_size,
	};

	if (!policy_update(zero, TPM_CC_POLICY_NV_WRITTEN, &written_value,
		sizeof(written_value), approved) ||
	    !policy_update(approved, TPM_CC_POLICY_CP_HASH, cp_hash,
		CAPSULE_TPM_PROVISION_DIGEST_SIZE, approved))
		return false;
	return sha256_spans(spans, sizes, 2, authorization_hash);
}

static void make_descriptor(const struct capsule_tpm_provision_input *input,
	struct capsule_tpm_provision_artifacts *artifacts)
{
	uint8_t *descriptor = artifacts->descriptor;

	write_le32(descriptor, 1);
	write_le32(descriptor + 4, sizeof(artifacts->descriptor));
	write_le32(descriptor + 8, 1);
	write_le32(descriptor + 12, input->nv_index);
	write_le32(descriptor + 16, CAPSULE_TPM_ANCHOR_ATTRIBUTES);
	write_le16(descriptor + 20, TPM_ALG_SHA256);
	write_le16(descriptor + 22, CAPSULE_TPM_PROVISION_ANCHOR_SIZE);
	write_le16(descriptor + 24, CAPSULE_TPM_PROVISION_DIGEST_SIZE);
	write_le16(descriptor + 26, CAPSULE_TPM_PROVISION_NAME_SIZE);
	write_le16(descriptor + 28, input->policy_ref_size);
	memcpy(descriptor + 32, artifacts->auth_policy,
		sizeof(artifacts->auth_policy));
	memcpy(descriptor + 64, artifacts->authority_name,
		sizeof(artifacts->authority_name));
	memcpy(descriptor + 98, input->policy_ref, input->policy_ref_size);
}

bool capsule_tpm_provision_generate(
	const struct capsule_tpm_provision_input *input,
	struct capsule_tpm_provision_artifacts *artifacts)
{
	uint8_t write_parameters[sizeof(uint16_t) +
		CAPSULE_TPM_PROVISION_ANCHOR_SIZE + sizeof(uint16_t)];
	struct byte_buffer parameters = {
		.bytes = write_parameters,
		.capacity = sizeof(write_parameters),
	};
	struct capsule_tpm_provision_artifacts result = { 0 };
	bool valid = false;

	if (!artifacts)
		return false;
	memset(artifacts, 0, sizeof(*artifacts));
	if (!input_valid(input))
		goto out;
	write_le64(result.anchor, input->epoch);
	memcpy(result.anchor + sizeof(input->epoch), input->manifest_digest,
		sizeof(input->manifest_digest));
	if (!make_rsa_public(input, &result) ||
	    !make_auth_policy(input, &result) ||
	    !make_nv_public(input, &result) ||
	    !append_be16(&parameters, sizeof(result.anchor)) ||
	    !append_bytes(&parameters, result.anchor, sizeof(result.anchor)) ||
	    !append_be16(&parameters, 0) ||
	    !make_cp_hash(TPM_CC_NV_WRITE, result.nv_name, parameters.bytes,
		parameters.size, result.write_cp_hash) ||
	    !make_cp_hash(TPM_CC_NV_WRITE_LOCK, result.written_nv_name, NULL, 0,
		result.lock_cp_hash) ||
	    !make_initial_authorization(false, result.write_cp_hash, input,
		result.write_approved_policy,
		result.write_authorization_hash) ||
	    !make_initial_authorization(true, result.lock_cp_hash, input,
		result.lock_approved_policy,
		result.lock_authorization_hash))
		goto out;
	make_descriptor(input, &result);
	*artifacts = result;
	valid = true;
out:
	secure_clear(write_parameters, sizeof(write_parameters));
	secure_clear(&result, sizeof(result));
	return valid;
}
