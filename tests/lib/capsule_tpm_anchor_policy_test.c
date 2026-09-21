/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/tss2_policy.h>
#include <string.h>

#include "../../src/security/tpm/capsule_anchor_policy.c"

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static struct capsule_tpm_anchor_binding binding;
static struct capsule_tpm_anchor_authorization authorization;
static struct capsule_tpm_anchor_value nv_value;
static uint8_t nv_policy[CAPSULE_TPM_ANCHOR_POLICY_SIZE];
static bool nv_locked;
enum malformed_reply {
	REPLY_VALID,
	REPLY_ERROR,
	REPLY_INDEX,
	REPLY_ALGORITHM,
	REPLY_ATTRIBUTES,
	REPLY_POLICY_SIZE,
	REPLY_POLICY,
	REPLY_DATA_SIZE,
	REPLY_VALUE_ERROR,
};

static enum malformed_reply malformed_reply;
static bool operation_takes_effect;
static tpm_result_t operation_result;
static unsigned int prepared_calls;
static unsigned int install_calls;
static unsigned int write_calls;
static unsigned int lock_calls;
static struct capsule_tpm_anchor_transition_provider *active_provider;
static tpm_result_t session_cleanup_result;
static tpm_result_t object_cleanup_result;
static struct capsule_tpm_anchor_policy_provider *active_state;

enum sabotage_kind {
	SABOTAGE_NONE,
	SABOTAGE_WRITE_ATTEMPT,
	SABOTAGE_LOCK_ATTEMPT,
	SABOTAGE_CALLBACK_COPIES,
	SABOTAGE_BINDING,
	SABOTAGE_AUTHORIZATION,
	SABOTAGE_REENTER_READ,
	SABOTAGE_REENTER_LOCK,
	SABOTAGE_REENTER_INSTALL,
};

static enum sabotage_kind sabotage;

static enum cb_err transport(void *context, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size);

tis_sendrecv_fn tlcl_tis_sendrecv;

vb2_error_t vb2_hash_calculate(bool allow_hwcrypto, const void *data,
	uint32_t size, enum vb2_hash_algorithm algorithm, struct vb2_hash *hash)
{
	struct vb2_sha256_context context;

	(void)allow_hwcrypto;
	CHECK(algorithm == VB2_HASH_SHA256);
	hash->algo = algorithm;
	vb2_sha256_init(&context, algorithm);
	vb2_sha256_update(&context, data, size);
	vb2_sha256_finalize(&context, hash->raw, algorithm);
	return VB2_SUCCESS;
}

tpm_result_t tlcl2_read_public(uint32_t index,
	struct tlcl2_nv_public *public)
{
	if (malformed_reply == REPLY_ERROR)
		return TPM_CB_READ_FAILURE;
	memset(public, 0, sizeof(*public));
	public->index = malformed_reply == REPLY_INDEX ? index + 1 : index;
	public->name_alg = malformed_reply == REPLY_ALGORITHM ? TPM_ALG_SHA1 :
		TPM_ALG_SHA256;
	public->attributes = CAPSULE_TPM_ANCHOR_ATTRIBUTES |
		CAPSULE_TPM_ANCHOR_WRITTEN |
		(nv_locked ? CAPSULE_TPM_ANCHOR_WRITELOCKED : 0);
	if (malformed_reply == REPLY_ATTRIBUTES)
		public->attributes ^= BIT(0);
	public->auth_policy_size = malformed_reply == REPLY_POLICY_SIZE ?
		sizeof(nv_policy) - 1 : sizeof(nv_policy);
	memcpy(public->auth_policy, nv_policy, sizeof(nv_policy));
	if (malformed_reply == REPLY_POLICY)
		public->auth_policy[0] ^= 1;
	public->data_size = malformed_reply == REPLY_DATA_SIZE ?
		sizeof(nv_value) - 1 : sizeof(nv_value);
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_read_public_on(const struct tlcl2_transport *scoped_transport,
	uint32_t index, struct tlcl2_nv_public *public)
{
	struct capsule_tpm_anchor_binding observed;
	struct capsule_tpm_anchor_binding locked_binding;
	struct capsule_tpm_anchor_value value;
	struct capsule_tpm_anchor_grant grant;
	enum sabotage_kind action = sabotage;

	CHECK(scoped_transport && scoped_transport->sendrecv);
	sabotage = SABOTAGE_NONE;
	switch (action) {
	case SABOTAGE_NONE:
		break;
	case SABOTAGE_WRITE_ATTEMPT:
		active_state->write_attempted ^= 2;
		break;
	case SABOTAGE_LOCK_ATTEMPT:
		active_state->lock_attempted ^= 2;
		break;
	case SABOTAGE_CALLBACK_COPIES:
		active_state->callbacks.install = NULL;
		active_state->sealed_callbacks.install = NULL;
		break;
	case SABOTAGE_BINDING:
		binding.policy_revision++;
		break;
	case SABOTAGE_AUTHORIZATION:
		authorization.generation++;
		break;
	case SABOTAGE_REENTER_READ:
		CHECK(active_provider->read(active_provider->context, transport,
			NULL, &binding, &observed, &value) == CB_ERR);
		break;
	case SABOTAGE_REENTER_LOCK:
		CHECK(active_provider->lock(active_provider->context, transport,
			NULL, &binding, &authorization) == CB_ERR);
		break;
	case SABOTAGE_REENTER_INSTALL:
		memset(&grant, 0, sizeof(grant));
		locked_binding = binding;
		locked_binding.write_locked = 1;
		CHECK(active_provider->install(active_provider->context, &grant,
			&locked_binding) == CB_ERR);
		break;
	}
	return tlcl2_read_public(index, public);
}

tpm_result_t tlcl2_read(uint32_t index, void *data, uint32_t length)
{
	if (malformed_reply == REPLY_VALUE_ERROR)
		return TPM_CB_READ_FAILURE;
	CHECK(index == (binding.nv_index & 0x00ffffffU));
	CHECK(length == sizeof(nv_value));
	memcpy(data, &nv_value, sizeof(nv_value));
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_read_on(const struct tlcl2_transport *transport,
	uint32_t index, void *data, uint32_t length)
{
	CHECK(transport && transport->sendrecv);
	return tlcl2_read(index, data, length);
}

tpm_result_t tlcl2_read_auth_on(const struct tlcl2_transport *transport,
	uint32_t index, void *data, uint32_t length)
{
	CHECK(transport && transport->sendrecv);
	return tlcl2_read(index, data, length);
}

tpm_result_t tlcl2_policy_session_run(uint16_t algorithm,
	const uint8_t *nonce, size_t nonce_size, tlcl2_policy_session_fn run,
	void *context)
{
	struct tlcl2_policy_session session = {
		.handle = 0x03000000,
		.hash_algorithm = algorithm,
		.nonce_size = nonce_size,
	};

	CHECK(algorithm == TPM_ALG_SHA256);
	CHECK(nonce_size == CAPSULE_TPM_ANCHOR_NONCE_SIZE);
	memcpy(session.nonce, nonce, nonce_size);
	return run(&session, context);
}

tpm_result_t tlcl2_policy_session_run_on(
	const struct tlcl2_transport *transport, uint16_t algorithm,
	const uint8_t *nonce, size_t nonce_size, tlcl2_policy_session_fn run,
	void *context, tpm_result_t *cleanup_result)
{
	CHECK(transport && transport->sendrecv);
	*cleanup_result = session_cleanup_result;
	return tlcl2_policy_session_run(algorithm, nonce, nonce_size, run,
		context);
}

tpm_result_t tlcl2_policy_nv_written(
	const struct tlcl2_policy_session *session, bool written)
{
	CHECK(session->handle == 0x03000000);
	CHECK(written);
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_policy_nv(const struct tlcl2_policy_session *session,
	uint32_t index, const struct tlcl2_policy_authorization *password,
	const uint8_t *operand, size_t operand_size, uint16_t offset,
	uint16_t operation)
{
	CHECK(session->handle == 0x03000000);
	CHECK(index == (binding.nv_index & 0x00ffffffU));
	CHECK(password->handle == TPM_RS_PW);
	CHECK(operand_size == sizeof(nv_value));
	CHECK(!memcmp(operand, &nv_value, sizeof(nv_value)));
	CHECK(offset == 0);
	CHECK(operation == 0);
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_policy_cp_hash(
	const struct tlcl2_policy_session *session, const uint8_t *digest,
	size_t digest_size)
{
	CHECK(session->handle == 0x03000000);
	CHECK(digest_size == CAPSULE_TPM_ANCHOR_POLICY_SIZE);
	CHECK(!memcmp(digest,
		write_calls ? authorization.write.cp_hash :
			authorization.lock.cp_hash, digest_size));
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_load_external_rsa(
	const struct tlcl2_rsa_public_key *key,
	struct tlcl2_external_object *object)
{
	CHECK(key->modulus_size == authorization.modulus_size);
	CHECK(!memcmp(key->modulus, authorization.modulus, key->modulus_size));
	object->handle = 0x80000000;
	object->name_size = sizeof(binding.authority_name);
	memcpy(object->name, binding.authority_name, object->name_size);
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_verify_rsa_signature(
	const struct tlcl2_external_object *object, const uint8_t *digest,
	size_t digest_size, const uint8_t *signature, size_t signature_size,
	struct tlcl2_verified_ticket *ticket)
{
	const struct capsule_tpm_anchor_policy_authorization *policy =
		write_calls ? &authorization.write : &authorization.lock;

	CHECK(object->handle == 0x80000000);
	CHECK(digest_size == CAPSULE_TPM_ANCHOR_POLICY_SIZE);
	CHECK(!bytes_zero(digest, digest_size));
	CHECK(signature_size == authorization.modulus_size);
	CHECK(!memcmp(signature, policy->signature, signature_size));
	memset(ticket, 0x5a, sizeof(*ticket));
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_policy_authorize(
	const struct tlcl2_policy_session *session,
	const uint8_t *approved_policy, size_t approved_policy_size,
	const uint8_t *policy_ref, size_t policy_ref_size,
	const uint8_t *key_name, size_t key_name_size,
	const struct tlcl2_verified_ticket *ticket)
{
	const struct capsule_tpm_anchor_policy_authorization *policy =
		write_calls ? &authorization.write : &authorization.lock;

	CHECK(session->handle == 0x03000000);
	CHECK(approved_policy_size == sizeof(policy->approved_policy));
	CHECK(!memcmp(approved_policy, policy->approved_policy,
		approved_policy_size));
	CHECK(policy_ref_size == binding.policy_ref_size);
	CHECK(!memcmp(policy_ref, binding.policy_ref, policy_ref_size));
	CHECK(key_name_size == sizeof(binding.authority_name));
	CHECK(!memcmp(key_name, binding.authority_name, key_name_size));
	CHECK(ticket->tag == 0x5a5a);
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_policy_command_code(
	const struct tlcl2_policy_session *session, uint32_t code)
{
	CHECK(session->handle == 0x03000000);
	CHECK(code == (write_calls ? TPM2_NV_Write : TPM2_NV_WriteLock));
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_policy_or(const struct tlcl2_policy_session *session,
	const uint8_t *digests, size_t count, size_t digest_size)
{
	CHECK(session->handle == 0x03000000);
	CHECK(digests != NULL);
	CHECK(count == 2);
	CHECK(digest_size == CAPSULE_TPM_ANCHOR_POLICY_SIZE);
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_policy_nv_write(
	const struct tlcl2_policy_session *session, uint32_t index,
	const uint8_t *data, size_t size, uint16_t offset)
{
	CHECK(session->handle == 0x03000000);
	CHECK(index == (binding.nv_index & 0x00ffffffU));
	CHECK(size == sizeof(authorization.candidate));
	CHECK(!memcmp(data, &authorization.candidate, size));
	CHECK(offset == 0);
	if (operation_takes_effect)
		nv_value = authorization.candidate;
	return operation_result;
}

tpm_result_t tlcl2_policy_nv_write_lock(
	const struct tlcl2_policy_session *session, uint32_t index)
{
	CHECK(session->handle == 0x03000000);
	CHECK(index == (binding.nv_index & 0x00ffffffU));
	if (operation_takes_effect)
		nv_locked = true;
	return operation_result;
}

tpm_result_t tlcl2_external_object_flush(
	struct tlcl2_external_object *object)
{
	CHECK(object->handle == 0x80000000);
	memset(object, 0, sizeof(*object));
	return TPM_SUCCESS;
}

tpm_result_t tlcl2_external_object_run_on(
	const struct tlcl2_transport *transport,
	const struct tlcl2_rsa_public_key *key,
	tlcl2_external_object_fn run, void *context,
	tpm_result_t *cleanup_result)
{
	struct tlcl2_external_object object;
	tpm_result_t result;

	CHECK(transport && transport->sendrecv);
	CHECK(tlcl2_load_external_rsa(key, &object) == TPM_SUCCESS);
	result = run(&object, context);
	CHECK(tlcl2_external_object_flush(&object) == TPM_SUCCESS);
	*cleanup_result = object_cleanup_result;
	if (object_cleanup_result != TPM_SUCCESS)
		return object_cleanup_result;
	return result;
}

static enum cb_err transport(void *context, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	(void)context;
	(void)request;
	(void)request_size;
	(void)response;
	(void)response_size;
	return CB_SUCCESS;
}

static enum cb_err prepared_ok(const void *context,
	const struct capsule_tpm_anchor_grant *grant)
{
	(void)context;
	CHECK(grant != NULL);
	prepared_calls++;
	return CB_SUCCESS;
}

static enum cb_err prepared_mutate(const void *context,
	const struct capsule_tpm_anchor_grant *grant)
{
	(void)context;
	((struct capsule_tpm_anchor_grant *)grant)->flags ^= 1;
	return CB_SUCCESS;
}

static enum cb_err prepared_reenter(const void *context,
	const struct capsule_tpm_anchor_grant *grant)
{
	struct capsule_tpm_anchor_binding observed;
	struct capsule_tpm_anchor_value value;

	(void)context;
	(void)grant;
	CHECK(active_provider->read(active_provider->context, transport, NULL,
		&binding, &observed, &value) == CB_ERR);
	return CB_SUCCESS;
}

static enum cb_err prepared_mutate_attempt(const void *context,
	const struct capsule_tpm_anchor_grant *grant)
{
	(void)context;
	(void)grant;
	active_state->write_attempted = 1;
	return CB_SUCCESS;
}

static enum cb_err install_ok(const void *context,
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *installed_binding)
{
	(void)context;
	CHECK(grant != NULL);
	CHECK(installed_binding->write_locked);
	install_calls++;
	return CB_SUCCESS;
}

static enum cb_err install_mutate(const void *context,
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *installed_binding)
{
	(void)context;
	((struct capsule_tpm_anchor_grant *)grant)->flags ^= 1;
	((struct capsule_tpm_anchor_binding *)installed_binding)->write_locked = 0;
	return CB_SUCCESS;
}

static enum cb_err install_mutate_attempt(const void *context,
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *installed_binding)
{
	(void)context;
	(void)grant;
	(void)installed_binding;
	active_state->lock_attempted ^= 2;
	return CB_SUCCESS;
}

static void initialize_inputs(void)
{
	static const uint8_t expected_policy[] = {
		0xe5, 0xd9, 0x31, 0xfd, 0x85, 0xab, 0x1d, 0x76,
		0xb6, 0xed, 0x7f, 0x12, 0xdf, 0x3b, 0x89, 0x6c,
		0xec, 0x3a, 0xcd, 0x3b, 0x91, 0xb1, 0x12, 0x78,
		0xd2, 0xc8, 0x9c, 0x02, 0x9b, 0x70, 0xc1, 0x88,
	};
	static const uint8_t authority_name[] = {
		0x00, 0x0b, 0xa1, 0xa7, 0x4a, 0x60, 0xbb, 0x0d,
		0x20, 0x28, 0xb8, 0xbe, 0x7b, 0xa6, 0xe1, 0x02,
		0x92, 0xde, 0x7f, 0xb0, 0x0e, 0x6b, 0xf8, 0xa4,
		0x68, 0x58, 0x94, 0xf5, 0xb1, 0x04, 0x3e, 0xcb,
		0xcb, 0xe2,
	};
	static const uint8_t write_policy[] = {
		0x1d, 0x05, 0x95, 0x2d, 0x95, 0xe3, 0x1c, 0xea,
		0xab, 0x81, 0x4d, 0x5a, 0xa5, 0x23, 0xe3, 0x48,
		0xf6, 0x61, 0x75, 0x5d, 0x13, 0xef, 0x63, 0x3f,
		0x18, 0xcc, 0x87, 0x3b, 0x8e, 0xbc, 0x56, 0x3d,
	};
	static const uint8_t lock_policy[] = {
		0xfc, 0x02, 0x7a, 0xda, 0x38, 0x98, 0x06, 0xda,
		0xdf, 0x78, 0x91, 0x0b, 0xd3, 0x3a, 0xfa, 0x23,
		0xf4, 0x40, 0x25, 0xf7, 0x3b, 0x6d, 0x8f, 0x40,
		0x2f, 0x90, 0xa5, 0x95, 0x1c, 0xab, 0x4b, 0x8b,
	};
	static const uint8_t write_cp_hash[] = {
		0x12, 0x9a, 0xbe, 0x39, 0x61, 0x32, 0x96, 0x13,
		0xd3, 0x26, 0xcb, 0x69, 0xa6, 0xbd, 0xc4, 0x10,
		0x62, 0xf2, 0x13, 0x4f, 0x68, 0x58, 0x6a, 0xcb,
		0xca, 0x6b, 0x58, 0xcf, 0xb6, 0xd8, 0x5a, 0x4b,
	};
	static const uint8_t lock_cp_hash[] = {
		0x03, 0xc7, 0xde, 0x39, 0x8c, 0x0a, 0x99, 0x65,
		0x8f, 0x2f, 0x65, 0xd0, 0xe4, 0x79, 0xb6, 0xa6,
		0x15, 0x11, 0xb4, 0xa5, 0xd7, 0x6d, 0x50, 0xe0,
		0x1f, 0xd8, 0xe8, 0x0a, 0xd2, 0x54, 0x84, 0xf5,
	};
	memset(&binding, 0, sizeof(binding));
	binding.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION;
	binding.nv_index = HR_NV_INDEX | 0x150020;
	memcpy(binding.authority_name, authority_name, sizeof(authority_name));
	binding.policy_ref_size = 8;
	memset(binding.policy_ref, 0x32, binding.policy_ref_size);

	memset(&authorization, 0, sizeof(authorization));
	authorization.revision = CAPSULE_TPM_ANCHOR_AUTHORIZATION_REVISION;
	authorization.size = sizeof(authorization);
	authorization.policy_revision = binding.policy_revision;
	authorization.nv_index = binding.nv_index;
	authorization.generation = 7;
	authorization.transaction = 9;
	authorization.current.epoch = 10;
	memset(authorization.current.digest, 0x43,
		sizeof(authorization.current.digest));
	authorization.candidate.epoch = 11;
	memset(authorization.candidate.digest, 0x54,
		sizeof(authorization.candidate.digest));
	memcpy(authorization.authority_name, binding.authority_name,
		sizeof(binding.authority_name));
	authorization.policy_ref_size = binding.policy_ref_size;
	memcpy(authorization.policy_ref, binding.policy_ref,
		sizeof(binding.policy_ref));
	authorization.modulus_size = 256;
	memset(authorization.modulus, 0x01, authorization.modulus_size);
	authorization.modulus[0] = 0x80;
	authorization.modulus[authorization.modulus_size - 1] = 0x03;
	memcpy(authorization.write.approved_policy, write_policy,
		sizeof(write_policy));
	memset(authorization.write.nonce, 0x87,
		sizeof(authorization.write.nonce));
	authorization.write.signature_size = authorization.modulus_size;
	memset(authorization.write.signature, 0x98,
		authorization.write.signature_size);
	memcpy(authorization.lock.approved_policy, lock_policy,
		sizeof(lock_policy));
	memset(authorization.lock.nonce, 0xba,
		sizeof(authorization.lock.nonce));
	authorization.lock.signature_size = authorization.modulus_size;
	memset(authorization.lock.signature, 0xcb,
		authorization.lock.signature_size);
	CHECK(make_static_policy(&binding, nv_policy,
		(uint8_t[2 * CAPSULE_TPM_ANCHOR_POLICY_SIZE]) { 0 }));
	CHECK(!memcmp(nv_policy, expected_policy, sizeof(expected_policy)));
	memcpy(authorization.write.cp_hash, write_cp_hash,
		sizeof(write_cp_hash));
	memcpy(authorization.lock.cp_hash, lock_cp_hash,
		sizeof(lock_cp_hash));
	nv_value = authorization.current;
	nv_locked = false;
	malformed_reply = REPLY_VALID;
	operation_takes_effect = true;
	operation_result = TPM_SUCCESS;
	session_cleanup_result = TPM_SUCCESS;
	object_cleanup_result = TPM_SUCCESS;
	sabotage = SABOTAGE_NONE;
	active_state = NULL;
	prepared_calls = 0;
	install_calls = 0;
	write_calls = 0;
	lock_calls = 0;
}

static void initialize_provider(
	struct capsule_tpm_anchor_policy_provider *state,
	struct capsule_tpm_anchor_transition_provider *provider)
{
	const struct capsule_tpm_anchor_policy_callbacks callbacks = {
		.prepared = prepared_ok,
		.install = install_ok,
	};
	const struct capsule_tpm_anchor_grant grant = { 0 };

	memset(state, 0, sizeof(*state));
	CHECK(capsule_tpm_anchor_policy_provider_init(state, &callbacks,
		provider) == CB_SUCCESS);
	active_state = state;
	active_provider = provider;
	CHECK(provider->prepared(provider->context, &grant) == CB_SUCCESS);
	CHECK(prepared_calls == 1);
}

static struct capsule_tpm_anchor_grant valid_grant(void)
{
	return (struct capsule_tpm_anchor_grant) {
		.revision = CAPSULE_TPM_ANCHOR_GRANT_REVISION,
		.size = sizeof(struct capsule_tpm_anchor_grant),
		.policy_revision = binding.policy_revision,
		.nv_index = binding.nv_index,
		.generation = authorization.generation,
		.transaction = authorization.transaction,
		.flags = CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS,
		.current = authorization.current,
		.candidate = authorization.candidate,
	};
}

static void test_success_and_advisory_readback(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct capsule_tpm_anchor_binding observed;
	struct capsule_tpm_anchor_value value;
	struct capsule_tpm_anchor_grant grant;

	initialize_inputs();
	grant = valid_grant();
	initialize_provider(&state, &provider);
	CHECK(provider.read(provider.context, transport, NULL, &binding,
		&observed, &value) == CB_SUCCESS);
	CHECK(!memcmp(&value, &authorization.current, sizeof(value)));
	operation_result = TPM_IOERROR;
	write_calls = 1;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	CHECK(provider.read(provider.context, transport, NULL, &binding,
		&observed, &value) == CB_SUCCESS);
	CHECK(!memcmp(&value, &authorization.candidate, sizeof(value)));
	operation_result = TPM_IOERROR;
	write_calls = 0;
	lock_calls = 1;
	CHECK(provider.lock(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	CHECK(provider.read(provider.context, transport, NULL, &binding,
		&observed, &value) == CB_SUCCESS);
	CHECK(observed.write_locked);
	CHECK(provider.install(provider.context, &grant, &observed) == CB_SUCCESS);
	CHECK(install_calls == 1);
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
}

static void test_recovery_and_malformed_public(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct capsule_tpm_anchor_binding observed;
	struct capsule_tpm_anchor_value value;

	initialize_inputs();
	nv_value = authorization.candidate;
	initialize_provider(&state, &provider);
	CHECK(provider.read(provider.context, transport, NULL, &binding,
		&observed, &value) == CB_SUCCESS);
	CHECK(!observed.write_locked);
	lock_calls = 1;
	CHECK(provider.lock(provider.context, transport, NULL, &binding,
		&authorization) == CB_SUCCESS);

	for (enum malformed_reply mode = REPLY_ERROR;
	     mode <= REPLY_VALUE_ERROR; mode++) {
		initialize_inputs();
		initialize_provider(&state, &provider);
		malformed_reply = mode;
		CHECK(provider.read(provider.context, transport, NULL, &binding,
			&observed, &value) == CB_ERR);
		CHECK(bytes_zero(&observed, sizeof(observed)));
		CHECK(bytes_zero(&value, sizeof(value)));
	}
}

static void test_tails_transcripts_and_replay(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;

	initialize_inputs();
	initialize_provider(&state, &provider);
	authorization.write.reserved[0] = 1;
	write_calls = 1;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	authorization.write.reserved[0] = 0;
	authorization.write.reserved2 = 1;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	authorization.write.reserved2 = 0;
	authorization.write.signature[authorization.write.signature_size] = 1;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	authorization.write.signature[authorization.write.signature_size] = 0;
	authorization.modulus[authorization.modulus_size] = 1;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	authorization.modulus[authorization.modulus_size] = 0;
	authorization.policy_ref[binding.policy_ref_size] = 1;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	authorization.policy_ref[binding.policy_ref_size] = 0;
	memcpy(authorization.lock.cp_hash, authorization.write.cp_hash,
		sizeof(authorization.lock.cp_hash));
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	initialize_inputs();
	memcpy(authorization.lock.approved_policy,
		authorization.write.approved_policy,
		sizeof(authorization.lock.approved_policy));
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);

	initialize_inputs();
	initialize_provider(&state, &provider);
	write_calls = 1;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_SUCCESS);
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
}

static void test_descriptor_mutation(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct capsule_tpm_anchor_binding observed;
	struct capsule_tpm_anchor_value value;

	initialize_inputs();
	initialize_provider(&state, &provider);
	state.callbacks.install = NULL;
	CHECK(provider.read(provider.context, transport, NULL, &binding,
		&observed, &value) == CB_ERR);
}

static void test_callback_mutation_and_reentry(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct capsule_tpm_anchor_policy_callbacks callbacks = {
		.prepared = prepared_mutate,
		.install = install_ok,
	};
	struct capsule_tpm_anchor_grant grant;

	initialize_inputs();
	grant = valid_grant();
	memset(&state, 0, sizeof(state));
	CHECK(capsule_tpm_anchor_policy_provider_init(&state, &callbacks,
		&provider) == CB_SUCCESS);
	CHECK(provider.prepared(provider.context, &grant) == CB_ERR);
	CHECK(grant.flags == CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS);

	callbacks.prepared = prepared_reenter;
	memset(&state, 0, sizeof(state));
	memset(&provider, 0, sizeof(provider));
	CHECK(capsule_tpm_anchor_policy_provider_init(&state, &callbacks,
		&provider) == CB_SUCCESS);
	active_provider = &provider;
	CHECK(provider.prepared(provider.context, &grant) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);
	active_provider = NULL;
}

static void test_install_argument_mutation(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	const struct capsule_tpm_anchor_policy_callbacks callbacks = {
		.prepared = prepared_ok,
		.install = install_mutate,
	};
	struct capsule_tpm_anchor_grant grant = { 0 };
	struct capsule_tpm_anchor_binding locked_binding;

	initialize_inputs();
	memset(&state, 0, sizeof(state));
	CHECK(capsule_tpm_anchor_policy_provider_init(&state, &callbacks,
		&provider) == CB_SUCCESS);
	CHECK(provider.prepared(provider.context, &grant) == CB_SUCCESS);
	grant = valid_grant();
	locked_binding = binding;
	locked_binding.write_locked = 1;
	CHECK(provider.install(provider.context, &grant, &locked_binding) ==
		CB_ERR);
	CHECK(grant.flags == CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS);
	CHECK(locked_binding.write_locked);
}

static void test_cleanup_failures_are_terminal(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;

	initialize_inputs();
	initialize_provider(&state, &provider);
	write_calls = 1;
	object_cleanup_result = TPM_IOERROR;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	initialize_provider(&state, &provider);
	write_calls = 1;
	session_cleanup_result = TPM_IOERROR;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);
}

static void test_attempt_sealing_and_reentry(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct capsule_tpm_anchor_policy_callbacks callbacks = {
		.prepared = prepared_mutate_attempt,
		.install = install_ok,
	};
	struct capsule_tpm_anchor_binding observed;
	struct capsule_tpm_anchor_binding locked_binding;
	struct capsule_tpm_anchor_value value;
	struct capsule_tpm_anchor_grant grant = { 0 };

	initialize_inputs();
	memset(&state, 0, sizeof(state));
	active_state = &state;
	CHECK(capsule_tpm_anchor_policy_provider_init(&state, &callbacks,
		&provider) == CB_SUCCESS);
	CHECK(provider.prepared(provider.context, &grant) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	initialize_provider(&state, &provider);
	sabotage = SABOTAGE_WRITE_ATTEMPT;
	CHECK(provider.read(provider.context, transport, NULL, &binding,
		&observed, &value) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	initialize_provider(&state, &provider);
	write_calls = 1;
	sabotage = SABOTAGE_LOCK_ATTEMPT;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	initialize_provider(&state, &provider);
	sabotage = SABOTAGE_REENTER_READ;
	CHECK(provider.read(provider.context, transport, NULL, &binding,
		&observed, &value) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	initialize_provider(&state, &provider);
	write_calls = 1;
	sabotage = SABOTAGE_REENTER_LOCK;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	initialize_provider(&state, &provider);
	write_calls = 1;
	sabotage = SABOTAGE_REENTER_INSTALL;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	callbacks.prepared = prepared_ok;
	callbacks.install = install_mutate_attempt;
	memset(&state, 0, sizeof(state));
	CHECK(capsule_tpm_anchor_policy_provider_init(&state, &callbacks,
		&provider) == CB_SUCCESS);
	active_state = &state;
	CHECK(provider.prepared(provider.context, &grant) == CB_SUCCESS);
	grant = valid_grant();
	locked_binding = binding;
	locked_binding.write_locked = 1;
	CHECK(provider.install(provider.context, &grant, &locked_binding) ==
		CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);
}

static void test_input_mutation_and_init_consumption(void)
{
	struct capsule_tpm_anchor_policy_provider state;
	struct capsule_tpm_anchor_transition_provider provider;
	struct capsule_tpm_anchor_binding observed;
	struct capsule_tpm_anchor_value value;
	struct capsule_tpm_anchor_policy_callbacks callbacks = {
		.prepared = prepared_ok,
		.install = install_ok,
	};

	initialize_inputs();
	initialize_provider(&state, &provider);
	write_calls = 1;
	sabotage = SABOTAGE_AUTHORIZATION;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	initialize_provider(&state, &provider);
	sabotage = SABOTAGE_BINDING;
	CHECK(provider.read(provider.context, transport, NULL, &binding,
		&observed, &value) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	initialize_provider(&state, &provider);
	write_calls = 1;
	sabotage = SABOTAGE_BINDING;
	CHECK(provider.write(provider.context, transport, NULL, &binding,
		&authorization) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	initialize_inputs();
	initialize_provider(&state, &provider);
	sabotage = SABOTAGE_CALLBACK_COPIES;
	CHECK(provider.read(provider.context, transport, NULL, &binding,
		&(struct capsule_tpm_anchor_binding) { 0 },
		&(struct capsule_tpm_anchor_value) { 0 }) == CB_ERR);
	CHECK(state.control == PROVIDER_FAILED);

	memset(&state, 0, sizeof(state));
	state.size = 1;
	CHECK(capsule_tpm_anchor_policy_provider_init(&state, &callbacks,
		&provider) == CB_ERR_ARG);
	CHECK(state.control == PROVIDER_FAILED);

	memset(&state, 0, sizeof(state));
	callbacks.prepared = NULL;
	CHECK(capsule_tpm_anchor_policy_provider_init(&state, &callbacks,
		&provider) == CB_ERR_ARG);
	CHECK(state.control == PROVIDER_FAILED);
	CHECK(capsule_tpm_anchor_policy_provider_init(&state, &callbacks,
		&provider) == CB_ERR);
}

int main(void)
{
	test_success_and_advisory_readback();
	test_recovery_and_malformed_public();
	test_tails_transcripts_and_replay();
	test_descriptor_mutation();
	test_callback_mutation_and_reentry();
	test_install_argument_mutation();
	test_cleanup_failures_are_terminal();
	test_attempt_sealing_and_reentry();
	test_input_mutation_and_init_consumption();
	return 0;
}
