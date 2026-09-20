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

#define TPM2_POLICY_SESSION_TYPE 1U

struct policy_command {
	uint8_t bytes[TPM_BUFFER_SIZE];
	struct obuf output;
};

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

static tpm_result_t command_send(struct policy_command *command,
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
	if (!tlcl_tis_sendrecv ||
	    tlcl_tis_sendrecv(command->bytes, command_size, response,
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

static tpm_result_t send_empty_response(struct policy_command *command,
	uint16_t response_tag)
{
	uint8_t response[TPM_BUFFER_SIZE] = { 0 };
	size_t response_size = sizeof(response);
	struct ibuf parameters;
	tpm_result_t result;

	result = command_send(command, response_tag, response, &response_size,
		&parameters, NULL);
	if (result == TPM_SUCCESS && ibuf_remaining(&parameters))
		result = TPM_CB_CORRUPTED_STATE;
	secure_clear(response, sizeof(response));
	secure_clear(command, sizeof(*command));
	return result;
}

static tpm_result_t flush_policy_handle(uint32_t handle)
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
	return send_empty_response(&command, TPM_ST_NO_SESSIONS);
}

tpm_result_t tlcl2_policy_session_start(uint16_t hash_algorithm,
	const uint8_t *caller_nonce, size_t caller_nonce_size,
	struct tlcl2_policy_session *session)
{
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
	status = command_send(&command, TPM_ST_NO_SESSIONS, response,
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
		flush_status = flush_policy_handle(result.handle);
		if (flush_status != TPM_SUCCESS)
			status = flush_status;
	}
	secure_clear(&result, sizeof(result));
	secure_clear(response, sizeof(response));
	secure_clear(&command, sizeof(command));
	return status;
}

tpm_result_t tlcl2_policy_session_flush(struct tlcl2_policy_session *session)
{
	tpm_result_t result = TPM_CB_RANGE;

	if (!session)
		return TPM_CB_RANGE;
	if (valid_policy_session(session))
		result = flush_policy_handle(session->handle);
	secure_clear(session, sizeof(*session));
	return result;
}

tpm_result_t tlcl2_policy_session_run(uint16_t hash_algorithm,
	const uint8_t *caller_nonce, size_t caller_nonce_size,
	tlcl2_policy_session_fn run, void *context)
{
	struct tlcl2_policy_session session = { 0 };
	uint32_t handle;
	tpm_result_t result;
	tpm_result_t flush_result;

	if (!run)
		return TPM_CB_RANGE;
	result = tlcl2_policy_session_start(hash_algorithm, caller_nonce,
		caller_nonce_size, &session);
	if (result != TPM_SUCCESS)
		return result;
	handle = session.handle;
	result = run(&session, context);
	flush_result = flush_policy_handle(handle);
	secure_clear(&session, sizeof(session));
	return flush_result == TPM_SUCCESS ? result : flush_result;
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
	return send_empty_response(&command, TPM_ST_NO_SESSIONS);
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
	result = command_send(&command, TPM_ST_SESSIONS, response,
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
