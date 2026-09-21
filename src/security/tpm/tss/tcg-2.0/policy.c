/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/iobuf.h>
#include <security/tpm/tss.h>
#include <security/tpm/tss2_policy.h>
#include <string.h>

#include "tss_structures.h"

#define TPM2_CC_POLICY_NV 0x00000149U
#define TPM2_CC_POLICY_OR 0x00000171U
#define TPM2_CC_POLICY_COMMAND_CODE 0x0000016cU
#define TPM2_CC_POLICY_CP_HASH 0x0000016eU
#define TPM2_CC_POLICY_NV_WRITTEN 0x0000018fU
#define TPM2_CC_START_AUTH_SESSION 0x00000176U
#define TPM2_CC_FLUSH_CONTEXT 0x00000165U
#define TPM2_CC_LOAD_EXTERNAL 0x00000167U
#define TPM2_CC_POLICY_AUTHORIZE 0x0000016aU
#define TPM2_CC_VERIFY_SIGNATURE 0x00000177U

#define TPM2_POLICY_SESSION_TYPE 1U
#define TPM2_ALG_RSA 0x0001U
#define TPM2_ALG_RSASSA 0x0014U
#define TPM2_ST_VERIFIED 0x8022U
#define TPM2_OBJECT_NODA BIT(10)
#define TPM2_OBJECT_SIGN BIT(18)
#define TPM2_HR_TRANSIENT 0x80000000U
#define TPM2_RH_OWNER 0x40000001U
#define TPM2_POLICY_COMMAND_BUFFER_SIZE 640U

struct policy_command {
	uint8_t bytes[TPM2_POLICY_COMMAND_BUFFER_SIZE];
	struct obuf output;
};

static tpm_result_t legacy_sendrecv(void *context, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	(void)context;
	if (!tlcl_tis_sendrecv)
		return TPM_IOERROR;
	return tlcl_tis_sendrecv(request, request_size, response, response_size);
}

static size_t hash_size(uint16_t algorithm);

static void secure_clear(void *data, size_t size)
{
	volatile uint8_t *byte = data;

	while (size--)
		*byte++ = 0;
}

static int command_begin(struct policy_command *command, uint16_t tag,
	uint32_t command_code)
{
	memset(command, 0, sizeof(*command));
	obuf_init(&command->output, command->bytes, sizeof(command->bytes));
	if (obuf_write_be16(&command->output, tag))
		return -1;
	if (obuf_write_be32(&command->output, 0))
		return -1;
	return obuf_write_be32(&command->output, command_code);
}

static int write_sized_bytes(struct obuf *output, const uint8_t *bytes,
	size_t size)
{
	if (size > UINT16_MAX || (size && !bytes))
		return -1;
	if (obuf_write_be16(output, size))
		return -1;
	return obuf_write(output, bytes, size);
}

static int write_authorization(struct obuf *output,
	const struct tlcl2_policy_authorization *authorization)
{
	struct obuf size_field;
	size_t start;

	if (!authorization ||
	    authorization->nonce_size > sizeof(authorization->nonce) ||
	    authorization->auth_size > sizeof(authorization->auth))
		return -1;
	if (obuf_splice_current(output, &size_field, sizeof(uint32_t)))
		return -1;
	if (obuf_write_be32(output, 0))
		return -1;
	start = obuf_nr_written(output);
	if (obuf_write_be32(output, authorization->handle))
		return -1;
	if (write_sized_bytes(output, authorization->nonce,
		authorization->nonce_size))
		return -1;
	if (obuf_write_be8(output, authorization->attributes))
		return -1;
	if (write_sized_bytes(output, authorization->auth,
		authorization->auth_size))
		return -1;
	return obuf_write_be32(&size_field, obuf_nr_written(output) - start);
}

static tpm_result_t command_send(const struct tlcl2_transport *transport,
	struct policy_command *command,
	uint16_t response_tag, uint8_t *response, size_t *response_size,
	struct ibuf *parameters, bool *received)
{
	struct ibuf input;
	uint16_t tag;
	uint32_t size;
	uint32_t code;
	size_t command_size = obuf_nr_written(&command->output);

	if (received)
		*received = false;

	command->bytes[2] = command_size >> 24;
	command->bytes[3] = command_size >> 16;
	command->bytes[4] = command_size >> 8;
	command->bytes[5] = command_size;
	if (transport) {
		if (!transport->sendrecv ||
		    transport->sendrecv(transport->context, command->bytes,
			command_size, response, response_size))
			return TPM_IOERROR;
	} else if (legacy_sendrecv(NULL, command->bytes, command_size, response,
		response_size))
		return TPM_IOERROR;
	if (received)
		*received = true;
	if (*response_size > TPM_BUFFER_SIZE)
		return TPM_CB_CORRUPTED_STATE;
	ibuf_init(&input, response, *response_size);
	if (ibuf_read_be16(&input, &tag) || ibuf_read_be32(&input, &size) ||
	    ibuf_read_be32(&input, &code) || size != *response_size)
		return TPM_CB_CORRUPTED_STATE;
	if (code != TPM2_RC_SUCCESS) {
		if (tag != TPM_ST_NO_SESSIONS || ibuf_remaining(&input))
			return TPM_CB_CORRUPTED_STATE;
		return TPM_IOERROR;
	}
	if (tag != response_tag)
		return TPM_CB_CORRUPTED_STATE;
	*parameters = input;
	return TPM_SUCCESS;
}

static bool valid_policy_session(const struct tlcl2_policy_session *session)
{
	size_t digest_size;

	if (!session)
		return false;
	digest_size = hash_size(session->hash_algorithm);
	return (session->handle & 0xff000000U) == HR_POLICY_SESSION &&
		digest_size && session->nonce_size == digest_size;
}

static bool valid_policy_handle(uint32_t handle)
{
	return (handle & 0xff000000U) == HR_POLICY_SESSION;
}

static bool valid_transient_handle(uint32_t handle)
{
	return (handle & 0xff000000U) == TPM2_HR_TRANSIENT;
}

static bool response_policy_handle(const uint8_t *response,
	size_t response_size, uint32_t *handle)
{
	struct ibuf input;
	uint16_t tag;
	uint32_t declared_size;
	uint32_t code;
	uint32_t candidate;

	if (response_size > TPM_BUFFER_SIZE)
		return false;
	ibuf_init(&input, response, response_size);
	if (ibuf_read_be16(&input, &tag) ||
	    ibuf_read_be32(&input, &declared_size) ||
	    ibuf_read_be32(&input, &code) || code != TPM2_RC_SUCCESS ||
	    ibuf_read_be32(&input, &candidate) ||
	    !valid_policy_handle(candidate))
		return false;
	*handle = candidate;
	return true;
}

static bool response_transient_handle(const uint8_t *response,
	size_t response_size, uint32_t *handle)
{
	struct ibuf input;
	uint16_t tag;
	uint32_t declared_size;
	uint32_t code;
	uint32_t candidate;

	if (response_size > TPM_BUFFER_SIZE)
		return false;
	ibuf_init(&input, response, response_size);
	if (ibuf_read_be16(&input, &tag) ||
	    ibuf_read_be32(&input, &declared_size) ||
	    ibuf_read_be32(&input, &code) || code != TPM2_RC_SUCCESS ||
	    ibuf_read_be32(&input, &candidate) ||
	    !valid_transient_handle(candidate))
		return false;
	*handle = candidate;
	return true;
}

static size_t hash_size(uint16_t algorithm)
{
	switch (algorithm) {
	case TPM_ALG_SHA1:
		return SHA1_DIGEST_SIZE;
	case TPM_ALG_SHA256:
		return SHA256_DIGEST_SIZE;
	case TPM_ALG_SHA384:
		return SHA384_DIGEST_SIZE;
	case TPM_ALG_SHA512:
		return SHA512_DIGEST_SIZE;
	default:
		return 0;
	}
}

static tpm_result_t send_empty_response(
	const struct tlcl2_transport *transport, struct policy_command *command,
	uint16_t response_tag)
{
	uint8_t response[TPM_BUFFER_SIZE] = { 0 };
	size_t response_size = sizeof(response);
	struct ibuf parameters;
	tpm_result_t result;

	result = command_send(transport, command, response_tag, response,
		&response_size,
		&parameters, NULL);
	if (result == TPM_SUCCESS && ibuf_remaining(&parameters))
		result = TPM_CB_CORRUPTED_STATE;
	secure_clear(response, sizeof(response));
	secure_clear(command, sizeof(*command));
	return result;
}

static tpm_result_t flush_policy_handle(
	const struct tlcl2_transport *transport, uint32_t handle)
{
	struct policy_command command;

	if (!valid_policy_handle(handle)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (command_begin(&command, TPM_ST_NO_SESSIONS,
		TPM2_CC_FLUSH_CONTEXT)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (obuf_write_be32(&command.output, handle)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	return send_empty_response(transport, &command, TPM_ST_NO_SESSIONS);
}

static tpm_result_t flush_transient_handle(
	const struct tlcl2_transport *transport, uint32_t handle)
{
	struct policy_command command;

	if (!valid_transient_handle(handle)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (command_begin(&command, TPM_ST_NO_SESSIONS,
		TPM2_CC_FLUSH_CONTEXT)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (obuf_write_be32(&command.output, handle)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	return send_empty_response(transport, &command, TPM_ST_NO_SESSIONS);
}

static tpm_result_t policy_session_start(
	const struct tlcl2_transport *transport, uint16_t hash_algorithm,
	const uint8_t *caller_nonce, size_t caller_nonce_size,
	struct tlcl2_policy_session *session)
{
	const struct tlcl2_transport legacy = {
		.sendrecv = legacy_sendrecv,
	};
	const struct tlcl2_transport *effective = transport ? transport : &legacy;
	struct policy_command command;
	uint8_t response[TPM_BUFFER_SIZE] = { 0 };
	size_t response_size = sizeof(response);
	struct ibuf parameters;
	struct tlcl2_policy_session result = { 0 };
	size_t digest_size;
	tpm_result_t status = TPM_CB_RANGE;
	tpm_result_t flush_status;
	bool received = false;
	bool flush = false;

	if (!session)
		return TPM_CB_RANGE;
	memset(session, 0, sizeof(*session));
	digest_size = hash_size(hash_algorithm);
	if (!digest_size || caller_nonce_size != digest_size || !caller_nonce)
		return TPM_CB_RANGE;
	result.transport = *effective;
	if (command_begin(&command, TPM_ST_NO_SESSIONS,
		TPM2_CC_START_AUTH_SESSION))
		goto out;
	if (obuf_write_be32(&command.output, TPM_RH_NULL))
		goto out;
	if (obuf_write_be32(&command.output, TPM_RH_NULL))
		goto out;
	if (write_sized_bytes(&command.output, caller_nonce,
		caller_nonce_size))
		goto out;
	if (write_sized_bytes(&command.output, NULL, 0))
		goto out;
	if (obuf_write_be8(&command.output, TPM2_POLICY_SESSION_TYPE))
		goto out;
	if (obuf_write_be16(&command.output, TPM_ALG_NULL))
		goto out;
	if (obuf_write_be16(&command.output, hash_algorithm))
		goto out;
	status = command_send(effective, &command, TPM_ST_NO_SESSIONS, response,
		&response_size, &parameters, &received);
	if (status != TPM_SUCCESS) {
		if (received && response_policy_handle(response, response_size,
			&result.handle))
			flush = true;
		goto out;
	}
	result.hash_algorithm = hash_algorithm;
	if (ibuf_read_be32(&parameters, &result.handle) ||
	    !valid_policy_handle(result.handle)) {
		status = TPM_CB_CORRUPTED_STATE;
		goto out;
	}
	flush = true;
	if (ibuf_read_be16(&parameters, &result.nonce_size) ||
	    result.nonce_size != caller_nonce_size ||
	    ibuf_read(&parameters, result.nonce, result.nonce_size) ||
	    ibuf_remaining(&parameters)) {
		status = TPM_CB_CORRUPTED_STATE;
		goto out;
	}
	*session = result;
	flush = false;
out:
	if (flush) {
		flush_status = flush_policy_handle(&result.transport, result.handle);
		if (flush_status != TPM_SUCCESS)
			status = flush_status;
	}
	secure_clear(&result, sizeof(result));
	secure_clear(response, sizeof(response));
	secure_clear(&command, sizeof(command));
	return status;
}

tpm_result_t tlcl2_policy_session_start(uint16_t hash_algorithm,
	const uint8_t *caller_nonce, size_t caller_nonce_size,
	struct tlcl2_policy_session *session)
{
	return policy_session_start(NULL, hash_algorithm, caller_nonce,
		caller_nonce_size, session);
}

tpm_result_t tlcl2_policy_session_flush(struct tlcl2_policy_session *session)
{
	tpm_result_t result = TPM_CB_RANGE;

	if (!session)
		return TPM_CB_RANGE;
	if (valid_policy_session(session))
		result = flush_policy_handle(&session->transport, session->handle);
	secure_clear(session, sizeof(*session));
	return result;
}

static tpm_result_t policy_session_run(const struct tlcl2_transport *transport,
	uint16_t hash_algorithm,
	const uint8_t *caller_nonce, size_t caller_nonce_size,
	tlcl2_policy_session_fn run, void *context, tpm_result_t *cleanup_result)
{
	struct tlcl2_policy_session session = { 0 };
	struct tlcl2_transport cleanup_transport;
	uint32_t handle;
	tpm_result_t result;
	tpm_result_t flush_result;

	if (!run)
		return TPM_CB_RANGE;
	if (cleanup_result)
		*cleanup_result = TPM_SUCCESS;
	result = policy_session_start(transport, hash_algorithm, caller_nonce,
		caller_nonce_size, &session);
	if (result != TPM_SUCCESS)
		return result;
	handle = session.handle;
	cleanup_transport = session.transport;
	result = run(&session, context);
	flush_result = flush_policy_handle(&cleanup_transport, handle);
	if (cleanup_result)
		*cleanup_result = flush_result;
	secure_clear(&session, sizeof(session));
	return flush_result == TPM_SUCCESS ? result : flush_result;
}

tpm_result_t tlcl2_policy_session_run(uint16_t hash_algorithm,
	const uint8_t *caller_nonce, size_t caller_nonce_size,
	tlcl2_policy_session_fn run, void *context)
{
	return policy_session_run(NULL, hash_algorithm, caller_nonce,
		caller_nonce_size, run, context, NULL);
}

tpm_result_t tlcl2_policy_session_run_on(
	const struct tlcl2_transport *transport, uint16_t hash_algorithm,
	const uint8_t *caller_nonce, size_t caller_nonce_size,
	tlcl2_policy_session_fn run, void *context, tpm_result_t *cleanup_result)
{
	if (!transport || !transport->sendrecv)
		return TPM_CB_RANGE;
	return policy_session_run(transport, hash_algorithm, caller_nonce,
		caller_nonce_size, run, context, cleanup_result);
}

static tpm_result_t send_policy_handle_command(
	const struct tlcl2_policy_session *session, uint32_t command_code,
	const uint8_t *bytes, size_t size)
{
	struct policy_command command;

	if (!valid_policy_session(session) || (size && !bytes)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (command_begin(&command, TPM_ST_NO_SESSIONS, command_code)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (obuf_write_be32(&command.output, session->handle)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (obuf_write(&command.output, bytes, size)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	return send_empty_response(&session->transport, &command,
		TPM_ST_NO_SESSIONS);
}

tpm_result_t tlcl2_policy_nv_written(
	const struct tlcl2_policy_session *session, bool written)
{
	uint8_t value = written;

	return send_policy_handle_command(session, TPM2_CC_POLICY_NV_WRITTEN,
		&value, sizeof(value));
}

tpm_result_t tlcl2_policy_cp_hash(
	const struct tlcl2_policy_session *session, const uint8_t *digest,
	size_t digest_size)
{
	uint8_t buffer[2 + TLCL2_POLICY_DIGEST_MAX_SIZE];
	struct obuf output;
	tpm_result_t result;

	if (!valid_policy_session(session) ||
	    digest_size != hash_size(session->hash_algorithm) || !digest)
		return TPM_CB_RANGE;
	obuf_init(&output, buffer, sizeof(buffer));
	if (write_sized_bytes(&output, digest, digest_size))
		return TPM_CB_RANGE;
	result = send_policy_handle_command(session, TPM2_CC_POLICY_CP_HASH,
		buffer, obuf_nr_written(&output));
	secure_clear(buffer, sizeof(buffer));
	return result;
}

tpm_result_t tlcl2_policy_command_code(
	const struct tlcl2_policy_session *session, uint32_t command_code)
{
	uint8_t buffer[4];
	struct obuf output;

	obuf_init(&output, buffer, sizeof(buffer));
	if (obuf_write_be32(&output, command_code))
		return TPM_CB_RANGE;
	return send_policy_handle_command(session,
		TPM2_CC_POLICY_COMMAND_CODE, buffer, sizeof(buffer));
}

tpm_result_t tlcl2_policy_or(const struct tlcl2_policy_session *session,
	const uint8_t *digests, size_t digest_count, size_t digest_size)
{
	uint8_t buffer[4 + TLCL2_POLICY_OR_MAX_DIGESTS *
		(2 + TLCL2_POLICY_DIGEST_MAX_SIZE)];
	struct obuf output;
	tpm_result_t result = TPM_CB_RANGE;

	if (!valid_policy_session(session) || digest_count < 2 ||
	    digest_count > TLCL2_POLICY_OR_MAX_DIGESTS || !digests ||
	    digest_size != hash_size(session->hash_algorithm) ||
	    digest_count > (TPM_BUFFER_SIZE - 18) / (2 + digest_size))
		return TPM_CB_RANGE;
	obuf_init(&output, buffer, sizeof(buffer));
	if (obuf_write_be32(&output, digest_count))
		goto out;
	for (size_t i = 0; i < digest_count; i++)
		if (write_sized_bytes(&output, digests + i * digest_size,
			digest_size))
			goto out;
	result = send_policy_handle_command(session, TPM2_CC_POLICY_OR,
		buffer, obuf_nr_written(&output));
out:
	secure_clear(buffer, sizeof(buffer));
	return result;
}

tpm_result_t tlcl2_policy_nv(const struct tlcl2_policy_session *session,
	uint32_t nv_index, const struct tlcl2_policy_authorization *authorization,
	const uint8_t *operand, size_t operand_size, uint16_t offset,
	uint16_t operation)
{
	struct policy_command command;
	uint8_t response[TPM_BUFFER_SIZE] = { 0 };
	size_t response_size = sizeof(response);
	struct ibuf parameters;
	uint32_t parameter_size;
	uint16_t field_size;
	uint8_t attributes;
	tpm_result_t result = TPM_CB_RANGE;

	if (!valid_policy_session(session) || nv_index > 0x00ffffffU ||
	    operand_size > TLCL2_POLICY_OPERAND_MAX_SIZE ||
	    operation > 0x000bU || offset + operand_size > UINT16_MAX ||
	    (operand_size && !operand) || !authorization ||
	    authorization->handle != TPM_RS_PW ||
	    authorization->nonce_size || authorization->attributes)
		goto out;
	if (command_begin(&command, TPM_ST_SESSIONS, TPM2_CC_POLICY_NV))
		goto out;
	if (obuf_write_be32(&command.output, HR_NV_INDEX + nv_index))
		goto out;
	if (obuf_write_be32(&command.output, HR_NV_INDEX + nv_index))
		goto out;
	if (obuf_write_be32(&command.output, session->handle))
		goto out;
	if (write_authorization(&command.output, authorization))
		goto out;
	if (write_sized_bytes(&command.output, operand, operand_size))
		goto out;
	if (obuf_write_be16(&command.output, offset))
		goto out;
	if (obuf_write_be16(&command.output, operation))
		goto out;
	result = command_send(&session->transport, &command, TPM_ST_SESSIONS,
		response,
		&response_size, &parameters, NULL);
	if (result != TPM_SUCCESS)
		goto out;
	/* No command parameters; require one bounded authorization response. */
	if (ibuf_read_be32(&parameters, &parameter_size) || parameter_size ||
	    ibuf_read_be16(&parameters, &field_size) ||
	    field_size ||
	    ibuf_read_be8(&parameters, &attributes) ||
	    attributes ||
	    ibuf_read_be16(&parameters, &field_size) ||
	    field_size ||
	    ibuf_remaining(&parameters))
		result = TPM_CB_CORRUPTED_STATE;
out:
	secure_clear(response, sizeof(response));
	secure_clear(&command, sizeof(command));
	return result;
}

static bool valid_rsa_size(size_t size)
{
	return size == 256 || size == 384 || size == 512;
}

static bool valid_external_object(const struct tlcl2_external_object *object)
{
	return object && valid_transient_handle(object->handle) &&
		object->name_size == SHA256_DIGEST_SIZE + sizeof(uint16_t) &&
		object->name[0] == 0 && object->name[1] == TPM_ALG_SHA256 &&
		valid_rsa_size(object->rsa_modulus_size);
}

static bool valid_digest_size(size_t size)
{
	return size == SHA1_DIGEST_SIZE || size == SHA256_DIGEST_SIZE ||
		size == SHA384_DIGEST_SIZE || size == SHA512_DIGEST_SIZE;
}

static int marshal_external_rsa_public(struct obuf *output,
	const struct tlcl2_rsa_public_key *public_key)
{
	if (!public_key || !valid_rsa_size(public_key->modulus_size) ||
	    !(public_key->modulus[0] & 0x80) ||
	    !(public_key->modulus[public_key->modulus_size - 1] & 1))
		return -1;
	if (obuf_write_be16(output, TPM2_ALG_RSA))
		return -1;
	if (obuf_write_be16(output, TPM_ALG_SHA256))
		return -1;
	if (obuf_write_be32(output, TPM2_OBJECT_NODA | TPM2_OBJECT_SIGN))
		return -1;
	if (write_sized_bytes(output, NULL, 0))
		return -1;
	if (obuf_write_be16(output, TPM_ALG_NULL))
		return -1;
	if (obuf_write_be16(output, TPM2_ALG_RSASSA))
		return -1;
	if (obuf_write_be16(output, TPM_ALG_SHA256))
		return -1;
	if (obuf_write_be16(output, public_key->modulus_size * 8))
		return -1;
	if (obuf_write_be32(output, 0))
		return -1;
	return write_sized_bytes(output, public_key->modulus,
		public_key->modulus_size);
}

static tpm_result_t load_external_rsa(
	const struct tlcl2_transport *transport,
	const struct tlcl2_rsa_public_key *public_key,
	struct tlcl2_external_object *object)
{
	const struct tlcl2_transport legacy = {
		.sendrecv = legacy_sendrecv,
	};
	const struct tlcl2_transport *effective = transport ? transport : &legacy;
	struct policy_command command;
	uint8_t response[TPM_BUFFER_SIZE] = { 0 };
	size_t response_size = sizeof(response);
	struct ibuf parameters;
	struct tlcl2_external_object result = { 0 };
	struct vb2_hash public_hash;
	tpm_result_t status = TPM_CB_RANGE;
	tpm_result_t flush_status;
	size_t public_size;
	size_t public_start;
	bool received = false;
	bool flush = false;

	if (!object)
		return TPM_CB_RANGE;
	memset(object, 0, sizeof(*object));
	result.transport = *effective;
	if (!public_key || !valid_rsa_size(public_key->modulus_size))
		goto out;
	public_size = 24 + public_key->modulus_size;
	if (command_begin(&command, TPM_ST_NO_SESSIONS,
		TPM2_CC_LOAD_EXTERNAL))
		goto out;
	if (write_sized_bytes(&command.output, NULL, 0))
		goto out;
	if (obuf_write_be16(&command.output, public_size))
		goto out;
	public_start = obuf_nr_written(&command.output);
	if (marshal_external_rsa_public(&command.output, public_key) ||
	    obuf_nr_written(&command.output) - public_start != public_size)
		goto out;
	if (vb2_hash_calculate(false, command.bytes + public_start, public_size,
		VB2_HASH_SHA256, &public_hash) != VB2_SUCCESS)
		goto out;
	if (obuf_write_be32(&command.output, TPM2_RH_OWNER))
		goto out;
	status = command_send(effective, &command, TPM_ST_NO_SESSIONS, response,
		&response_size, &parameters, &received);
	if (status != TPM_SUCCESS) {
		if (received && response_transient_handle(response, response_size,
			&result.handle))
			flush = true;
		goto out;
	}
	if (ibuf_read_be32(&parameters, &result.handle) ||
	    !valid_transient_handle(result.handle)) {
		status = TPM_CB_CORRUPTED_STATE;
		goto out;
	}
	flush = true;
	if (ibuf_read_be16(&parameters, &result.name_size) ||
	    result.name_size != SHA256_DIGEST_SIZE + sizeof(uint16_t) ||
	    ibuf_read(&parameters, result.name, result.name_size) ||
	    ibuf_remaining(&parameters) || result.name[0] ||
	    result.name[1] != TPM_ALG_SHA256 ||
	    memcmp(result.name + sizeof(uint16_t), public_hash.raw,
		SHA256_DIGEST_SIZE)) {
		status = TPM_CB_CORRUPTED_STATE;
		goto out;
	}
	result.rsa_modulus_size = public_key->modulus_size;
	*object = result;
	flush = false;
out:
	if (flush) {
		flush_status = flush_transient_handle(&result.transport,
			result.handle);
		if (flush_status != TPM_SUCCESS)
			status = flush_status;
	}
	secure_clear(&public_hash, sizeof(public_hash));
	secure_clear(&result, sizeof(result));
	secure_clear(response, sizeof(response));
	secure_clear(&command, sizeof(command));
	return status;
}

tpm_result_t tlcl2_load_external_rsa(
	const struct tlcl2_rsa_public_key *public_key,
	struct tlcl2_external_object *object)
{
	return load_external_rsa(NULL, public_key, object);
}

tpm_result_t tlcl2_load_external_rsa_on(
	const struct tlcl2_transport *transport,
	const struct tlcl2_rsa_public_key *public_key,
	struct tlcl2_external_object *object)
{
	if (!object)
		return TPM_CB_RANGE;
	memset(object, 0, sizeof(*object));
	if (!transport || !transport->sendrecv)
		return TPM_CB_RANGE;
	return load_external_rsa(transport, public_key, object);
}

tpm_result_t tlcl2_external_object_flush(
	struct tlcl2_external_object *object)
{
	tpm_result_t result = TPM_CB_RANGE;

	if (!object)
		return TPM_CB_RANGE;
	if (valid_external_object(object))
		result = flush_transient_handle(&object->transport, object->handle);
	secure_clear(object, sizeof(*object));
	return result;
}

static tpm_result_t external_object_run(
	const struct tlcl2_transport *transport,
	const struct tlcl2_rsa_public_key *public_key,
	tlcl2_external_object_fn run, void *context, tpm_result_t *cleanup_result)
{
	struct tlcl2_external_object object = { 0 };
	struct tlcl2_transport cleanup_transport;
	uint32_t handle;
	tpm_result_t result;
	tpm_result_t flush_result;

	if (!run)
		return TPM_CB_RANGE;
	if (cleanup_result)
		*cleanup_result = TPM_SUCCESS;
	result = load_external_rsa(transport, public_key, &object);
	if (result != TPM_SUCCESS)
		return result;
	handle = object.handle;
	cleanup_transport = object.transport;
	result = run(&object, context);
	flush_result = flush_transient_handle(&cleanup_transport, handle);
	if (cleanup_result)
		*cleanup_result = flush_result;
	secure_clear(&object, sizeof(object));
	return flush_result == TPM_SUCCESS ? result : flush_result;
}

tpm_result_t tlcl2_external_object_run(
	const struct tlcl2_rsa_public_key *public_key,
	tlcl2_external_object_fn run, void *context)
{
	return external_object_run(NULL, public_key, run, context, NULL);
}

tpm_result_t tlcl2_external_object_run_on(
	const struct tlcl2_transport *transport,
	const struct tlcl2_rsa_public_key *public_key,
	tlcl2_external_object_fn run, void *context,
	tpm_result_t *cleanup_result)
{
	if (!transport || !transport->sendrecv)
		return TPM_CB_RANGE;
	return external_object_run(transport, public_key, run, context,
		cleanup_result);
}

tpm_result_t tlcl2_verify_rsa_signature(
	const struct tlcl2_external_object *object, const uint8_t *digest,
	size_t digest_size, const uint8_t *signature, size_t signature_size,
	struct tlcl2_verified_ticket *ticket)
{
	struct policy_command command;
	uint8_t response[TPM_BUFFER_SIZE] = { 0 };
	size_t response_size = sizeof(response);
	struct ibuf parameters;
	struct tlcl2_verified_ticket result = { 0 };
	tpm_result_t status = TPM_CB_RANGE;

	if (!ticket)
		return TPM_CB_RANGE;
	memset(ticket, 0, sizeof(*ticket));
	if (!valid_external_object(object) || !digest ||
	    digest_size != SHA256_DIGEST_SIZE || !signature ||
	    signature_size != object->rsa_modulus_size)
		goto out;
	if (command_begin(&command, TPM_ST_NO_SESSIONS,
		TPM2_CC_VERIFY_SIGNATURE))
		goto out;
	if (obuf_write_be32(&command.output, object->handle))
		goto out;
	if (write_sized_bytes(&command.output, digest, digest_size))
		goto out;
	if (obuf_write_be16(&command.output, TPM2_ALG_RSASSA))
		goto out;
	if (obuf_write_be16(&command.output, TPM_ALG_SHA256))
		goto out;
	if (write_sized_bytes(&command.output, signature, signature_size))
		goto out;
	status = command_send(&object->transport, &command, TPM_ST_NO_SESSIONS,
		response,
		&response_size, &parameters, NULL);
	if (status != TPM_SUCCESS)
		goto out;
	if (ibuf_read_be16(&parameters, &result.tag) ||
	    result.tag != TPM2_ST_VERIFIED ||
	    ibuf_read_be32(&parameters, &result.hierarchy) ||
	    result.hierarchy != TPM2_RH_OWNER ||
	    ibuf_read_be16(&parameters, &result.digest_size) ||
	    !valid_digest_size(result.digest_size) ||
	    ibuf_read(&parameters, result.digest, result.digest_size) ||
	    ibuf_remaining(&parameters)) {
		status = TPM_CB_CORRUPTED_STATE;
		goto out;
	}
	*ticket = result;
out:
	secure_clear(&result, sizeof(result));
	secure_clear(response, sizeof(response));
	secure_clear(&command, sizeof(command));
	return status;
}

static bool valid_verified_ticket(const struct tlcl2_verified_ticket *ticket)
{
	return ticket && ticket->tag == TPM2_ST_VERIFIED &&
		ticket->hierarchy == TPM2_RH_OWNER &&
		valid_digest_size(ticket->digest_size);
}

tpm_result_t tlcl2_policy_authorize(
	const struct tlcl2_policy_session *session,
	const uint8_t *approved_policy, size_t approved_policy_size,
	const uint8_t *policy_ref, size_t policy_ref_size,
	const uint8_t *key_name, size_t key_name_size,
	const struct tlcl2_verified_ticket *ticket)
{
	struct policy_command command;

	if (!valid_policy_session(session) || !approved_policy ||
	    approved_policy_size != hash_size(session->hash_algorithm) ||
	    policy_ref_size > TLCL2_POLICY_OPERAND_MAX_SIZE ||
	    (policy_ref_size && !policy_ref) || !key_name ||
	    key_name_size != SHA256_DIGEST_SIZE + sizeof(uint16_t) ||
	    key_name[0] || key_name[1] != TPM_ALG_SHA256 ||
	    !valid_verified_ticket(ticket)) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (command_begin(&command, TPM_ST_NO_SESSIONS,
		TPM2_CC_POLICY_AUTHORIZE))
		goto fail;
	if (obuf_write_be32(&command.output, session->handle))
		goto fail;
	if (write_sized_bytes(&command.output, approved_policy,
		approved_policy_size))
		goto fail;
	if (write_sized_bytes(&command.output, policy_ref, policy_ref_size))
		goto fail;
	if (write_sized_bytes(&command.output, key_name, key_name_size))
		goto fail;
	if (obuf_write_be16(&command.output, ticket->tag))
		goto fail;
	if (obuf_write_be32(&command.output, ticket->hierarchy))
		goto fail;
	if (write_sized_bytes(&command.output, ticket->digest,
		ticket->digest_size))
		goto fail;
	return send_empty_response(&session->transport, &command,
		TPM_ST_NO_SESSIONS);
fail:
	secure_clear(&command, sizeof(command));
	return TPM_CB_RANGE;
}

static int write_policy_authorization(struct obuf *output,
	const struct tlcl2_policy_session *session)
{
	const struct tlcl2_policy_authorization authorization = {
		.handle = session->handle,
		.attributes = 1,
	};

	return write_authorization(output, &authorization);
}

static tpm_result_t send_policy_authorized(struct policy_command *command,
	const struct tlcl2_policy_session *session)
{
	uint8_t response[TPM_BUFFER_SIZE] = { 0 };
	size_t response_size = sizeof(response);
	struct ibuf parameters;
	uint32_t parameter_size;
	uint16_t field_size;
	uint8_t attributes;
	tpm_result_t result;

	result = command_send(&session->transport, command, TPM_ST_SESSIONS,
		response,
		&response_size, &parameters, NULL);
	if (result != TPM_SUCCESS)
		goto out;
	/*
	 * These mutations are intended as the terminal use of this session. Keep
	 * it alive so a scoped caller can deterministically flush it afterward.
	 */
	if (ibuf_read_be32(&parameters, &parameter_size) || parameter_size ||
	    ibuf_read_be16(&parameters, &field_size) ||
	    field_size != hash_size(session->hash_algorithm) ||
	    !ibuf_oob_drain(&parameters, field_size) ||
	    ibuf_read_be8(&parameters, &attributes) || attributes != 1 ||
	    ibuf_read_be16(&parameters, &field_size) || field_size ||
	    ibuf_remaining(&parameters))
		result = TPM_CB_CORRUPTED_STATE;
out:
	secure_clear(response, sizeof(response));
	secure_clear(command, sizeof(*command));
	return result;
}

tpm_result_t tlcl2_policy_nv_write(
	const struct tlcl2_policy_session *session, uint32_t nv_index,
	const uint8_t *data, size_t data_size, uint16_t offset)
{
	struct policy_command command;

	if (!valid_policy_session(session) || nv_index > 0x00ffffffU ||
	    !data || !data_size || data_size > TLCL2_POLICY_OPERAND_MAX_SIZE ||
	    offset + data_size > UINT16_MAX) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (command_begin(&command, TPM_ST_SESSIONS, TPM2_NV_Write))
		goto fail;
	if (obuf_write_be32(&command.output, HR_NV_INDEX + nv_index))
		goto fail;
	if (obuf_write_be32(&command.output, HR_NV_INDEX + nv_index))
		goto fail;
	if (write_policy_authorization(&command.output, session))
		goto fail;
	if (write_sized_bytes(&command.output, data, data_size))
		goto fail;
	if (obuf_write_be16(&command.output, offset))
		goto fail;
	return send_policy_authorized(&command, session);
fail:
	secure_clear(&command, sizeof(command));
	return TPM_CB_RANGE;
}

tpm_result_t tlcl2_policy_nv_write_lock(
	const struct tlcl2_policy_session *session, uint32_t nv_index)
{
	struct policy_command command;

	if (!valid_policy_session(session) || nv_index > 0x00ffffffU) {
		secure_clear(&command, sizeof(command));
		return TPM_CB_RANGE;
	}
	if (command_begin(&command, TPM_ST_SESSIONS, TPM2_NV_WriteLock))
		goto fail;
	if (obuf_write_be32(&command.output, HR_NV_INDEX + nv_index))
		goto fail;
	if (obuf_write_be32(&command.output, HR_NV_INDEX + nv_index))
		goto fail;
	if (write_policy_authorization(&command.output, session))
		goto fail;
	return send_policy_authorized(&command, session);
fail:
	secure_clear(&command, sizeof(command));
	return TPM_CB_RANGE;
}
