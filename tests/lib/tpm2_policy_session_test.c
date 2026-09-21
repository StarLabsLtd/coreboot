/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/tss.h>
#include <security/tpm/tss2_policy.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

enum response_fault {
	RESPONSE_OK,
	RESPONSE_TRANSPORT,
	RESPONSE_TRUNCATED,
	RESPONSE_OVERSIZED,
	RESPONSE_ERROR,
	RESPONSE_BAD_TAG,
	RESPONSE_BAD_SIZE,
	RESPONSE_TRAILING,
	RESPONSE_BAD_HANDLE,
	RESPONSE_BAD_NONCE_SIZE,
	RESPONSE_TRUNCATED_AFTER_HANDLE,
	RESPONSE_TRUNCATED_HANDLE,
	RESPONSE_BAD_PARAMETER_SIZE,
	RESPONSE_AUTH_NONCE,
	RESPONSE_AUTH_ATTRIBUTES,
	RESPONSE_AUTH_HMAC,
	RESPONSE_BAD_NAME_SIZE,
	RESPONSE_BAD_NAME_ALGORITHM,
	RESPONSE_BAD_NAME_DIGEST,
	RESPONSE_BAD_TICKET_TAG,
	RESPONSE_BAD_TICKET_HIERARCHY,
	RESPONSE_BAD_TICKET_SIZE,
	RESPONSE_ZERO_TICKET_SIZE,
	RESPONSE_OVERSIZED_TICKET,
};

tis_sendrecv_fn tlcl_tis_sendrecv;
static enum response_fault response_fault;
static uint8_t last_command[1024];
static size_t last_command_size;
static unsigned int flush_count;
static bool fail_flush;
static bool fault_once;
static uint16_t verified_ticket_size = SHA256_DIGEST_SIZE;
static unsigned int scoped_calls;

static struct tlcl2_rsa_public_key valid_rsa_key(size_t size);
static tpm_result_t mutating_object_body(
	const struct tlcl2_external_object *object, void *context);

struct vb2_context;

void vb2ex_printf(const char *function, const char *format, ...)
{
	(void)function;
	(void)format;
}

void vb2api_fail(struct vb2_context *context, uint8_t reason, uint8_t subcode)
{
	(void)context;
	(void)reason;
	(void)subcode;
}

static uint32_t be32(const uint8_t *bytes)
{
	return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
		((uint32_t)bytes[2] << 8) | bytes[3];
}

static void put16(uint8_t **output, uint16_t value)
{
	*(*output)++ = value >> 8;
	*(*output)++ = value;
}

static void put32(uint8_t **output, uint32_t value)
{
	*(*output)++ = value >> 24;
	*(*output)++ = value >> 16;
	*(*output)++ = value >> 8;
	*(*output)++ = value;
}

static tpm_result_t fake_sendrecv(const uint8_t *send, size_t send_size,
	uint8_t *receive, size_t *receive_size)
{
	uint8_t *output = receive;
	uint32_t command_code;
	enum response_fault fault = response_fault;
	uint16_t tag = TPM_ST_NO_SESSIONS;
	uint32_t response_code = fault == RESPONSE_ERROR ? 0x101 : 0;
	uint16_t nonce_size;
	uint16_t ticket_size;
	size_t size_position;
	struct vb2_hash public_hash;

	CHECK(send_size <= sizeof(last_command));
	memcpy(last_command, send, send_size);
	last_command_size = send_size;
	command_code = be32(send + 6);
	if (command_code == 0x00000165)
		flush_count++;
	if (fault == RESPONSE_TRANSPORT ||
	    (command_code == 0x00000165 && fail_flush))
		return TPM_IOERROR;
	if ((command_code == 0x00000149 || command_code == TPM2_NV_Write ||
	     command_code == TPM2_NV_WriteLock) && !response_code)
		tag = TPM_ST_SESSIONS;
	if (fault == RESPONSE_BAD_TAG)
		tag ^= 3;
	put16(&output, tag);
	size_position = output - receive;
	put32(&output, 0);
	put32(&output, response_code);
	if (!response_code && command_code == 0x00000176) {
		nonce_size = ((uint16_t)send[18] << 8) | send[19];
		put32(&output, fault == RESPONSE_BAD_HANDLE ?
			HR_HMAC_SESSION + 7 : HR_POLICY_SESSION + 7);
		if (fault == RESPONSE_BAD_NONCE_SIZE)
			nonce_size--;
		put16(&output, nonce_size);
		for (size_t i = 0; i < nonce_size; i++)
			*output++ = 0xa0 + i;
	} else if (!response_code && command_code == 0x00000167) {
		uint16_t public_size = ((uint16_t)send[12] << 8) | send[13];

		CHECK(vb2_hash_calculate(false, send + 14, public_size,
			VB2_HASH_SHA256, &public_hash) == VB2_SUCCESS);
		put32(&output, fault == RESPONSE_BAD_HANDLE ?
			HR_POLICY_SESSION + 7 : 0x80000005U);
		put16(&output, fault == RESPONSE_BAD_NAME_SIZE ? 33 : 34);
		put16(&output, fault == RESPONSE_BAD_NAME_ALGORITHM ?
			TPM_ALG_SHA1 : TPM_ALG_SHA256);
		memcpy(output, public_hash.raw, SHA256_DIGEST_SIZE);
		if (fault == RESPONSE_BAD_NAME_DIGEST)
			output[0] ^= 1;
		output += fault == RESPONSE_BAD_NAME_SIZE ? 31 : 32;
	} else if (!response_code && command_code == 0x00000177) {
		ticket_size = verified_ticket_size;
		if (fault == RESPONSE_BAD_TICKET_SIZE)
			ticket_size = 31;
		else if (fault == RESPONSE_ZERO_TICKET_SIZE)
			ticket_size = 0;
		else if (fault == RESPONSE_OVERSIZED_TICKET)
			ticket_size = TLCL2_POLICY_DIGEST_MAX_SIZE + 1;
		put16(&output, fault == RESPONSE_BAD_TICKET_TAG ?
			0x8021 : 0x8022);
		put32(&output, fault == RESPONSE_BAD_TICKET_HIERARCHY ?
			TPM_RH_NULL : 0x40000001U);
		put16(&output, ticket_size);
		for (size_t i = 0; i < ticket_size; i++)
			*output++ = 0xc0 + i;
	} else if (!response_code && command_code == 0x00000149) {
		put32(&output,
			fault == RESPONSE_BAD_PARAMETER_SIZE ? 1 : 0);
		put16(&output, fault == RESPONSE_AUTH_NONCE ? 1 : 0);
		if (fault == RESPONSE_AUTH_NONCE)
			*output++ = 0x5a;
		*output++ = fault == RESPONSE_AUTH_ATTRIBUTES ? 1 : 0;
		put16(&output, fault == RESPONSE_AUTH_HMAC ? 1 : 0);
		if (fault == RESPONSE_AUTH_HMAC)
			*output++ = 0xa5;
	} else if (!response_code &&
		   (command_code == TPM2_NV_Write ||
		    command_code == TPM2_NV_WriteLock)) {
		put32(&output,
			fault == RESPONSE_BAD_PARAMETER_SIZE ? 1 : 0);
		put16(&output, fault == RESPONSE_AUTH_NONCE ? 31 : 32);
		for (size_t i = 0;
		     i < (fault == RESPONSE_AUTH_NONCE ? 31 : 32); i++)
			*output++ = 0xd0 + i;
		*output++ = fault == RESPONSE_AUTH_ATTRIBUTES ? 0 : 1;
		put16(&output, fault == RESPONSE_AUTH_HMAC ? 1 : 0);
		if (fault == RESPONSE_AUTH_HMAC)
			*output++ = 0xa5;
	}
	if (fault == RESPONSE_TRAILING)
		*output++ = 0xee;
	*receive_size = output - receive;
	receive[size_position + 0] = *receive_size >> 24;
	receive[size_position + 1] = *receive_size >> 16;
	receive[size_position + 2] = *receive_size >> 8;
	receive[size_position + 3] = *receive_size;
	if (fault == RESPONSE_BAD_SIZE)
		receive[size_position + 3]++;
	if (fault == RESPONSE_TRUNCATED)
		(*receive_size)--;
	if (fault == RESPONSE_TRUNCATED_AFTER_HANDLE &&
	    (command_code == 0x00000176 || command_code == 0x00000167))
		*receive_size = 14;
	if (fault == RESPONSE_TRUNCATED_HANDLE &&
	    (command_code == 0x00000176 || command_code == 0x00000167))
		*receive_size = 13;
	if (fault == RESPONSE_OVERSIZED)
		*receive_size = TPM_BUFFER_SIZE + 1;
	if (fault_once &&
	    (command_code == 0x00000176 || command_code == 0x00000167)) {
		response_fault = RESPONSE_OK;
		fault_once = false;
	}
	return TPM_SUCCESS;
}

static tpm_result_t scoped_sendrecv(void *context, const uint8_t *send,
	size_t send_size, uint8_t *receive, size_t *receive_size)
{
	CHECK(context == &scoped_calls);
	scoped_calls++;
	return fake_sendrecv(send, send_size, receive, receive_size);
}

static tpm_result_t forbidden_sendrecv(const uint8_t *send, size_t send_size,
	uint8_t *receive, size_t *receive_size)
{
	(void)send;
	(void)send_size;
	(void)receive;
	(void)receive_size;
	__builtin_trap();
}

static struct tlcl2_policy_session valid_session(void)
{
	struct tlcl2_policy_session session = {
		.handle = HR_POLICY_SESSION + 7,
		.hash_algorithm = TPM_ALG_SHA256,
		.nonce_size = SHA256_DIGEST_SIZE,
		.transport = {
			.sendrecv = scoped_sendrecv,
			.context = &scoped_calls,
		},
	};

	memset(session.nonce, 0xa5, session.nonce_size);
	return session;
}

static void check_header(uint16_t tag, uint32_t command_code, size_t size)
{
	CHECK(last_command_size == size);
	CHECK(last_command[0] == (tag >> 8));
	CHECK(last_command[1] == (uint8_t)tag);
	CHECK(be32(last_command + 2) == size);
	CHECK(be32(last_command + 6) == command_code);
}

static void test_start_and_flush(void)
{
	uint8_t nonce[SHA256_DIGEST_SIZE];
	struct tlcl2_policy_session session;

	for (size_t i = 0; i < sizeof(nonce); i++)
		nonce[i] = i;
	memset(&session, 0xa5, sizeof(session));
	CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, nonce,
		sizeof(nonce), &session) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x00000176, 59);
	CHECK(be32(last_command + 10) == TPM_RH_NULL);
	CHECK(be32(last_command + 14) == TPM_RH_NULL);
	CHECK(last_command[18] == 0 && last_command[19] == sizeof(nonce));
	CHECK(!memcmp(last_command + 20, nonce, sizeof(nonce)));
	CHECK(!memcmp(last_command + 52,
		(const uint8_t[]){ 0, 0, 1, 0, 0x10, 0, 0x0b }, 7));
	CHECK(session.handle == HR_POLICY_SESSION + 7);
	CHECK(session.hash_algorithm == TPM_ALG_SHA256);
	CHECK(session.nonce_size == SHA256_DIGEST_SIZE);
	for (size_t i = 0; i < session.nonce_size; i++)
		CHECK(session.nonce[i] == 0xa0 + i);
	CHECK(tlcl2_policy_session_flush(&session) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x00000165, 14);
	CHECK(be32(last_command + 10) == HR_POLICY_SESSION + 7);
	CHECK(!memcmp(&session, &(struct tlcl2_policy_session){ 0 },
		sizeof(session)));
}

static void test_policy_commands(void)
{
	struct tlcl2_policy_session session = valid_session();
	uint8_t digest[2 * SHA256_DIGEST_SIZE];
	struct tlcl2_policy_authorization authorization = {
		.handle = TPM_RS_PW,
	};
	uint8_t operand[] = { 0x12, 0x34, 0x56 };

	for (size_t i = 0; i < sizeof(digest); i++)
		digest[i] = i;
	CHECK(tlcl2_policy_nv_written(&session, true) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x0000018f, 15);
	CHECK(be32(last_command + 10) == session.handle);
	CHECK(last_command[14] == 1);

	CHECK(tlcl2_policy_cp_hash(&session, digest,
		SHA256_DIGEST_SIZE) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x0000016e, 48);
	CHECK(!memcmp(last_command + 14,
		(const uint8_t[]){ 0, SHA256_DIGEST_SIZE }, 2));
	CHECK(!memcmp(last_command + 16, digest, SHA256_DIGEST_SIZE));

	CHECK(tlcl2_policy_command_code(&session, 0x137) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x0000016c, 18);
	CHECK(be32(last_command + 14) == 0x137);

	CHECK(tlcl2_policy_or(&session, digest, 2,
		SHA256_DIGEST_SIZE) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x00000171, 86);
	CHECK(be32(last_command + 14) == 2);
	CHECK(last_command[18] == 0 &&
		last_command[19] == SHA256_DIGEST_SIZE);
	CHECK(!memcmp(last_command + 20, digest, SHA256_DIGEST_SIZE));
	CHECK(last_command[52] == 0 &&
		last_command[53] == SHA256_DIGEST_SIZE);
	CHECK(!memcmp(last_command + 54, digest + SHA256_DIGEST_SIZE,
		SHA256_DIGEST_SIZE));

	CHECK(tlcl2_policy_nv(&session, 0x1234, &authorization, operand,
		sizeof(operand), 9, 0) == TPM_SUCCESS);
	check_header(TPM_ST_SESSIONS, 0x00000149, 44);
	CHECK(be32(last_command + 10) == HR_NV_INDEX + 0x1234);
	CHECK(be32(last_command + 14) == HR_NV_INDEX + 0x1234);
	CHECK(be32(last_command + 18) == session.handle);
	CHECK(be32(last_command + 22) == 9);
	CHECK(be32(last_command + 26) == TPM_RS_PW);
	CHECK(!memcmp(last_command + 30,
		(const uint8_t[]){ 0, 0, 0, 0, 0 }, 5));
	CHECK(last_command[35] == 0 && last_command[36] == sizeof(operand));
	CHECK(!memcmp(last_command + 37, operand, sizeof(operand)));
	CHECK(last_command[40] == 0 && last_command[41] == 9);
	authorization.auth_size = 3;
	memcpy(authorization.auth, (const uint8_t[]){ 0xaa, 0xbb, 0xcc }, 3);
	CHECK(tlcl2_policy_nv(&session, 0x1234, &authorization, NULL, 0,
		0, 0) == TPM_SUCCESS);
	check_header(TPM_ST_SESSIONS, 0x00000149, 44);
	CHECK(be32(last_command + 22) == 12);
	CHECK(last_command[33] == 0 && last_command[34] == 3);
	CHECK(!memcmp(last_command + 35,
		(const uint8_t[]){ 0xaa, 0xbb, 0xcc }, 3));
	CHECK(!memcmp(last_command + 38,
		(const uint8_t[]){ 0, 0, 0, 0, 0, 0 }, 6));
}

static tpm_result_t successful_body(
	const struct tlcl2_policy_session *session, void *context)

{
	(void)session;
	(void)context;
	return TPM_SUCCESS;
}

static tpm_result_t failing_body(
	const struct tlcl2_policy_session *session,
	void *context)
{
	(void)session;
	(void)context;
	return TPM_CB_FAIL;
}

static tpm_result_t mutating_body(
	const struct tlcl2_policy_session *session, void *context)
{
	struct tlcl2_policy_session *mutable_session =
		(struct tlcl2_policy_session *)session;

	(void)context;
	mutable_session->handle = HR_POLICY_SESSION + 0x1234;
	mutable_session->nonce_size = 0;
	return TPM_SUCCESS;
}

static tpm_result_t mutating_transport_body(
	const struct tlcl2_policy_session *session, void *context)
{
	struct tlcl2_policy_session *mutable_session =
		(struct tlcl2_policy_session *)session;

	(void)context;
	memset(&mutable_session->transport, 0,
		sizeof(mutable_session->transport));
	return tlcl2_policy_nv_written(session, true);
}

static void test_scoped_transport(void)
{
	uint8_t nonce[SHA256_DIGEST_SIZE] = { 0 };
	struct tlcl2_transport transport = {
		.sendrecv = scoped_sendrecv,
		.context = &scoped_calls,
	};
	struct tlcl2_rsa_public_key key = valid_rsa_key(256);
	struct tlcl2_external_object object;
	tpm_result_t cleanup_result = TPM_IOERROR;
	unsigned int before;

	tlcl_tis_sendrecv = forbidden_sendrecv;
	scoped_calls = 0;
	before = flush_count;
	CHECK(tlcl2_policy_session_run_on(&transport, TPM_ALG_SHA256, nonce,
		sizeof(nonce), mutating_transport_body, NULL,
		&cleanup_result) == TPM_IOERROR);
	CHECK(cleanup_result == TPM_SUCCESS);
	CHECK(flush_count == before + 1);
	CHECK(scoped_calls == 2);
	CHECK(tlcl2_load_external_rsa_on(&transport, &key, &object) ==
		TPM_SUCCESS);
	CHECK(object.transport.sendrecv == scoped_sendrecv);
	CHECK(object.transport.context == &scoped_calls);
	CHECK(tlcl2_external_object_flush(&object) == TPM_SUCCESS);
	CHECK(scoped_calls == 4);
	cleanup_result = TPM_IOERROR;
	CHECK(tlcl2_external_object_run_on(&transport, &key,
		mutating_object_body, NULL, &cleanup_result) == TPM_SUCCESS);
	CHECK(cleanup_result == TPM_SUCCESS);
	CHECK(scoped_calls == 6);
	tlcl_tis_sendrecv = fake_sendrecv;
}

static void test_guaranteed_flush(void)
{
	uint8_t nonce[SHA256_DIGEST_SIZE] = { 0 };
	unsigned int before = flush_count;

	CHECK(tlcl2_policy_session_run(TPM_ALG_SHA256, nonce, sizeof(nonce),
		failing_body, NULL) == TPM_CB_FAIL);
	CHECK(flush_count == before + 1);
	fail_flush = true;
	CHECK(tlcl2_policy_session_run(TPM_ALG_SHA256, nonce, sizeof(nonce),
		successful_body, NULL) == TPM_IOERROR);
	CHECK(tlcl2_policy_session_run(TPM_ALG_SHA256, nonce, sizeof(nonce),
		failing_body, NULL) == TPM_IOERROR);
	CHECK(flush_count == before + 3);
	fail_flush = false;
	CHECK(tlcl2_policy_session_run(TPM_ALG_SHA256, nonce, sizeof(nonce),
		mutating_body, NULL) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x00000165, 14);
	CHECK(be32(last_command + 10) == HR_POLICY_SESSION + 7);
}

static void test_start_cleanup(void)
{
	static const enum response_fault cleanup_faults[] = {
		RESPONSE_BAD_TAG,
		RESPONSE_BAD_SIZE,
		RESPONSE_TRUNCATED_AFTER_HANDLE,
	};
	static const enum response_fault no_cleanup_faults[] = {
		RESPONSE_TRANSPORT,
		RESPONSE_OVERSIZED,
		RESPONSE_ERROR,
	};
	uint8_t nonce[SHA256_DIGEST_SIZE] = { 0 };
	struct tlcl2_policy_session session;
	unsigned int before;

	response_fault = RESPONSE_BAD_HANDLE;
	before = flush_count;
	CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, nonce, sizeof(nonce),
		&session) == TPM_CB_CORRUPTED_STATE);
	CHECK(flush_count == before);
	CHECK(!memcmp(&session, &(struct tlcl2_policy_session){ 0 },
		sizeof(session)));
	for (size_t i = 0; i < ARRAY_SIZE(cleanup_faults); i++) {
		response_fault = cleanup_faults[i];
		fault_once = true;
		before = flush_count;
		CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, nonce,
			sizeof(nonce), &session) == TPM_CB_CORRUPTED_STATE);
		CHECK(flush_count == before + 1);
		CHECK(!memcmp(&session, &(struct tlcl2_policy_session){ 0 },
			sizeof(session)));
		response_fault = cleanup_faults[i];
		fault_once = true;
		fail_flush = true;
		before = flush_count;
		CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, nonce,
			sizeof(nonce), &session) == TPM_IOERROR);
		CHECK(flush_count == before + 1);
		fail_flush = false;
	}
	response_fault = RESPONSE_BAD_NONCE_SIZE;
	fault_once = true;
	before = flush_count;
	CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, nonce, sizeof(nonce),
		&session) == TPM_CB_CORRUPTED_STATE);
	CHECK(flush_count == before + 1);
	response_fault = RESPONSE_TRAILING;
	fault_once = true;
	before = flush_count;
	CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, nonce, sizeof(nonce),
		&session) == TPM_CB_CORRUPTED_STATE);
	CHECK(flush_count == before + 1);
	for (size_t i = 0; i < ARRAY_SIZE(no_cleanup_faults); i++) {
		response_fault = no_cleanup_faults[i];
		before = flush_count;
		CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, nonce,
			sizeof(nonce), &session) != TPM_SUCCESS);
		CHECK(flush_count == before);
	}
	response_fault = RESPONSE_TRUNCATED_HANDLE;
	before = flush_count;
	CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, nonce, sizeof(nonce),
		&session) == TPM_CB_CORRUPTED_STATE);
	CHECK(flush_count == before);
	response_fault = RESPONSE_OK;
}

static void test_flush_zeroes_session(void)
{
	struct tlcl2_policy_session session = valid_session();

	response_fault = RESPONSE_TRANSPORT;
	CHECK(tlcl2_policy_session_flush(&session) == TPM_IOERROR);
	CHECK(!memcmp(&session, &(struct tlcl2_policy_session){ 0 },
		sizeof(session)));
	session = valid_session();
	session.handle = HR_HMAC_SESSION;
	CHECK(tlcl2_policy_session_flush(&session) == TPM_CB_RANGE);
	CHECK(!memcmp(&session, &(struct tlcl2_policy_session){ 0 },
		sizeof(session)));
	response_fault = RESPONSE_OK;
}

static void test_invalid_inputs(void)
{
	struct tlcl2_policy_session session = valid_session();
	struct tlcl2_policy_authorization authorization = {
		.handle = TPM_RS_PW,
	};
	uint8_t bytes[TLCL2_POLICY_DIGEST_MAX_SIZE + 1] = { 0 };
	uint8_t digests[TLCL2_POLICY_OR_MAX_DIGESTS * SHA256_DIGEST_SIZE] = { 0 };
	struct tlcl2_policy_session unknown_session = {
		.handle = HR_POLICY_SESSION + 7,
		.hash_algorithm = 0xffff,
	};
	unsigned int before = flush_count;

	CHECK(tlcl2_policy_session_start(0xffff, bytes, 0, &session) ==
		TPM_CB_RANGE);
	CHECK(tlcl2_policy_session_start(0xffff, bytes, 32, &session) ==
		TPM_CB_RANGE);
	CHECK(flush_count == before);
	CHECK(tlcl2_policy_nv_written(&unknown_session, true) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_command_code(&unknown_session, 0x137) ==
		TPM_CB_RANGE);
	CHECK(tlcl2_policy_cp_hash(&unknown_session, bytes, 0) ==
		TPM_CB_RANGE);
	CHECK(tlcl2_policy_or(&unknown_session, bytes, 2, 0) == TPM_CB_RANGE);

	CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, bytes, 31,
		&session) == TPM_CB_RANGE);
	CHECK(!memcmp(&session, &(struct tlcl2_policy_session){ 0 },
		sizeof(session)));
	session = valid_session();
	CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, bytes, 0,
		&session) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_session_start(TPM_ALG_SHA512, bytes, 64,
		&session) == TPM_SUCCESS);
	CHECK(tlcl2_policy_session_flush(&session) == TPM_SUCCESS);
	CHECK(tlcl2_policy_session_start(TPM_ALG_SHA512, bytes, 65,
		&session) == TPM_CB_RANGE);
	session = valid_session();
	CHECK(tlcl2_policy_cp_hash(&session, bytes, 0) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_cp_hash(&session, bytes, 31) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_cp_hash(&session, bytes, 33) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_cp_hash(&session, bytes, 32) == TPM_SUCCESS);
	CHECK(tlcl2_policy_or(&session, digests, 0, 32) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_or(&session, bytes, 1, 32) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_or(&session, bytes, 2, 31) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_or(&session, digests, 2, 32) == TPM_SUCCESS);
	CHECK(tlcl2_policy_or(&session, digests, 7, 32) == TPM_SUCCESS);
	CHECK(tlcl2_policy_or(&session, digests, 8, 32) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_or(&session, digests, 9, 32) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_nv(&session, 0x1000000, &authorization, NULL, 0,
		0, 0) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_nv(&session, 0, &authorization, bytes,
		TLCL2_POLICY_OPERAND_MAX_SIZE + 1, 0, 0) == TPM_CB_RANGE);
	authorization.auth_size = sizeof(authorization.auth) + 1;
	CHECK(tlcl2_policy_nv(&session, 0, &authorization, NULL, 0, 0, 0) ==
		TPM_CB_RANGE);
	authorization.auth_size = 0;
	authorization.nonce_size = 1;
	CHECK(tlcl2_policy_nv(&session, 0, &authorization, NULL, 0, 0, 0) ==
		TPM_CB_RANGE);
	authorization.nonce_size = 0;
	authorization.attributes = 1;
	CHECK(tlcl2_policy_nv(&session, 0, &authorization, NULL, 0, 0, 0) ==
		TPM_CB_RANGE);
	authorization.attributes = 0;
	authorization.handle = 0x80000000U;
	CHECK(tlcl2_policy_nv(&session, 0, &authorization, NULL, 0, 0, 0) ==
		TPM_CB_RANGE);
	authorization.handle = TPM_RS_PW;
	CHECK(tlcl2_policy_nv(&session, 0, &authorization, NULL, 0, 0,
		0x000c) == TPM_CB_RANGE);
}

static void test_response_failures(void)
{
	uint8_t nonce[SHA256_DIGEST_SIZE] = { 0 };
	struct tlcl2_policy_session session;

	for (response_fault = RESPONSE_TRANSPORT;
	     response_fault <= RESPONSE_BAD_NONCE_SIZE; response_fault++) {
		memset(&session, 0xa5, sizeof(session));
		CHECK(tlcl2_policy_session_start(TPM_ALG_SHA256, nonce,
			sizeof(nonce), &session) != TPM_SUCCESS);
		CHECK(!memcmp(&session, &(struct tlcl2_policy_session){ 0 },
			sizeof(session)));
	}
	response_fault = RESPONSE_OK;
	session = valid_session();
	response_fault = RESPONSE_BAD_TAG;
	CHECK(tlcl2_policy_nv_written(&session, true) != TPM_SUCCESS);
	response_fault = RESPONSE_TRAILING;
	CHECK(tlcl2_policy_nv_written(&session, true) != TPM_SUCCESS);
	response_fault = RESPONSE_BAD_PARAMETER_SIZE;
	CHECK(tlcl2_policy_nv(&session, 0,
		&(struct tlcl2_policy_authorization){ .handle = TPM_RS_PW },
		NULL, 0, 0, 0) != TPM_SUCCESS);
	for (response_fault = RESPONSE_AUTH_NONCE;
	     response_fault <= RESPONSE_AUTH_HMAC; response_fault++)
		CHECK(tlcl2_policy_nv(&session, 0,
			&(struct tlcl2_policy_authorization){
				.handle = TPM_RS_PW,
			}, NULL, 0, 0, 0) == TPM_CB_CORRUPTED_STATE);
	response_fault = RESPONSE_OK;
}

static struct tlcl2_rsa_public_key valid_rsa_key(size_t modulus_size)
{
	struct tlcl2_rsa_public_key key = {
		.modulus_size = modulus_size,
	};

	for (size_t i = 0; i < key.modulus_size; i++)
		key.modulus[i] = 0x80 + i;
	key.modulus[key.modulus_size - 1] |= 1;
	return key;
}

static struct tlcl2_external_object valid_external_object_for_test(void)
{
	struct tlcl2_external_object object = {
		.handle = 0x80000005U,
		.name_size = 34,
		.name = { 0, TPM_ALG_SHA256 },
		.rsa_modulus_size = 256,
		.transport = {
			.sendrecv = scoped_sendrecv,
			.context = &scoped_calls,
		},
	};

	return object;
}

static struct tlcl2_verified_ticket valid_ticket(void)
{
	struct tlcl2_verified_ticket ticket = {
		.tag = 0x8022,
		.hierarchy = 0x40000001U,
		.digest_size = SHA256_DIGEST_SIZE,
	};

	memset(ticket.digest, 0xc5, ticket.digest_size);
	return ticket;
}

static void test_load_external(void)
{
	struct tlcl2_rsa_public_key key = valid_rsa_key(256);
	struct tlcl2_external_object object;

	CHECK(tlcl2_load_external_rsa(&key, &object) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x00000167, 298);
	CHECK(!memcmp(last_command + 10,
		(const uint8_t[]){ 0, 0, 1, 0x18, 0, 1, 0, 0x0b,
			0, 4, 4, 0, 0, 0, 0, 0x10, 0, 0x14, 0, 0x0b,
			8, 0, 0, 0, 0, 0, 1, 0 }, 28));
	CHECK(!memcmp(last_command + 38, key.modulus, key.modulus_size));
	CHECK(be32(last_command + 294) == 0x40000001U);
	CHECK(object.handle == 0x80000005U);
	CHECK(object.name_size == 34);
	CHECK(object.name[0] == 0 && object.name[1] == TPM_ALG_SHA256);
	CHECK(object.rsa_modulus_size == 256);
	CHECK(tlcl2_external_object_flush(&object) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x00000165, 14);
	CHECK(be32(last_command + 10) == 0x80000005U);
	CHECK(!memcmp(&object, &(struct tlcl2_external_object){ 0 },
		sizeof(object)));
	for (size_t size = 384; size <= 512; size += 128) {
		key = valid_rsa_key(size);
		CHECK(tlcl2_load_external_rsa(&key, &object) == TPM_SUCCESS);
		check_header(TPM_ST_NO_SESSIONS, 0x00000167, 42 + size);
		CHECK(!memcmp(last_command + 38, key.modulus, size));
		CHECK(be32(last_command + 38 + size) == 0x40000001U);
		CHECK(object.rsa_modulus_size == size);
		CHECK(tlcl2_external_object_flush(&object) == TPM_SUCCESS);
	}
}

static tpm_result_t mutating_object_body(
	const struct tlcl2_external_object *object, void *context)
{
	struct tlcl2_external_object *mutable_object =
		(struct tlcl2_external_object *)object;

	(void)context;
	mutable_object->handle = 0x80001234U;
	memset(&mutable_object->transport, 0,
		sizeof(mutable_object->transport));
	return TPM_SUCCESS;
}

static void test_external_cleanup(void)
{
	static const enum response_fault cleanup_faults[] = {
		RESPONSE_BAD_TAG,
		RESPONSE_BAD_SIZE,
		RESPONSE_TRUNCATED_AFTER_HANDLE,
		RESPONSE_BAD_NAME_SIZE,
		RESPONSE_BAD_NAME_ALGORITHM,
		RESPONSE_BAD_NAME_DIGEST,
		RESPONSE_TRAILING,
	};
	static const enum response_fault no_cleanup_faults[] = {
		RESPONSE_TRANSPORT,
		RESPONSE_OVERSIZED,
		RESPONSE_ERROR,
		RESPONSE_BAD_HANDLE,
		RESPONSE_TRUNCATED_HANDLE,
	};
	struct tlcl2_rsa_public_key key = valid_rsa_key(256);
	struct tlcl2_external_object object;
	unsigned int before;

	for (size_t i = 0; i < ARRAY_SIZE(cleanup_faults); i++) {
		response_fault = cleanup_faults[i];
		fault_once = true;
		before = flush_count;
		CHECK(tlcl2_load_external_rsa(&key, &object) != TPM_SUCCESS);
		CHECK(flush_count == before + 1);
		CHECK(!memcmp(&object,
			&(struct tlcl2_external_object){ 0 }, sizeof(object)));
		response_fault = cleanup_faults[i];
		fault_once = true;
		fail_flush = true;
		before = flush_count;
		CHECK(tlcl2_load_external_rsa(&key, &object) == TPM_IOERROR);
		CHECK(flush_count == before + 1);
		fail_flush = false;
	}
	for (size_t i = 0; i < ARRAY_SIZE(no_cleanup_faults); i++) {
		response_fault = no_cleanup_faults[i];
		before = flush_count;
		CHECK(tlcl2_load_external_rsa(&key, &object) != TPM_SUCCESS);
		CHECK(flush_count == before);
	}
	response_fault = RESPONSE_OK;
	CHECK(tlcl2_external_object_run(&key, mutating_object_body, NULL) ==
		TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x00000165, 14);
	CHECK(be32(last_command + 10) == 0x80000005U);
	fail_flush = true;
	CHECK(tlcl2_external_object_run(&key, mutating_object_body, NULL) ==
		TPM_IOERROR);
	fail_flush = false;
}

static void test_external_inputs(void)
{
	struct tlcl2_rsa_public_key key = valid_rsa_key(256);
	struct tlcl2_external_object object = valid_external_object_for_test();

	for (size_t size = 0; size <= TLCL2_RSA_MODULUS_MAX_SIZE; size++) {
		if (size == 256 || size == 384 || size == 512)
			continue;
		key.modulus_size = size;
		CHECK(tlcl2_load_external_rsa(&key, &object) == TPM_CB_RANGE);
		CHECK(!memcmp(&object,
			&(struct tlcl2_external_object){ 0 }, sizeof(object)));
	}
	key = valid_rsa_key(256);
	key.modulus[0] = 0x7f;
	CHECK(tlcl2_load_external_rsa(&key, &object) == TPM_CB_RANGE);
	key = valid_rsa_key(256);
	key.modulus[key.modulus_size - 1] &= ~1U;
	CHECK(tlcl2_load_external_rsa(&key, &object) == TPM_CB_RANGE);
	object.handle = HR_POLICY_SESSION;
	CHECK(tlcl2_external_object_flush(&object) == TPM_CB_RANGE);
	CHECK(!memcmp(&object, &(struct tlcl2_external_object){ 0 },
		sizeof(object)));
}

static void test_verify_and_authorize(void)
{
	struct tlcl2_external_object object = valid_external_object_for_test();
	struct tlcl2_policy_session session = valid_session();
	struct tlcl2_verified_ticket ticket;
	uint8_t digest[SHA256_DIGEST_SIZE];
	uint8_t signature[256];
	uint8_t name[34] = { 0, TPM_ALG_SHA256 };
	uint8_t policy_ref[] = { 1, 2, 3 };

	memset(digest, 0x5a, sizeof(digest));
	memset(signature, 0xa5, sizeof(signature));
	CHECK(tlcl2_verify_rsa_signature(&object, digest, sizeof(digest),
		signature, sizeof(signature), &ticket) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x00000177, 310);
	CHECK(be32(last_command + 10) == object.handle);
	CHECK(last_command[14] == 0 && last_command[15] == sizeof(digest));
	CHECK(!memcmp(last_command + 16, digest, sizeof(digest)));
	CHECK(!memcmp(last_command + 48,
		(const uint8_t[]){ 0, 0x14, 0, 0x0b, 1, 0 }, 6));
	CHECK(!memcmp(last_command + 54, signature, sizeof(signature)));
	CHECK(ticket.tag == 0x8022 && ticket.hierarchy == 0x40000001U);
	CHECK(ticket.digest_size == SHA256_DIGEST_SIZE);
	CHECK(tlcl2_policy_authorize(&session, digest, sizeof(digest),
		policy_ref, sizeof(policy_ref), name, sizeof(name),
		&ticket) == TPM_SUCCESS);
	check_header(TPM_ST_NO_SESSIONS, 0x0000016a, 129);
	CHECK(be32(last_command + 10) == session.handle);
	CHECK(!memcmp(last_command + 14,
		(const uint8_t[]){ 0, SHA256_DIGEST_SIZE }, 2));
	CHECK(!memcmp(last_command + 16, digest, sizeof(digest)));
	CHECK(last_command[48] == 0 && last_command[49] == 3);
	CHECK(!memcmp(last_command + 50, policy_ref, sizeof(policy_ref)));
	CHECK(last_command[53] == 0 && last_command[54] == sizeof(name));
	CHECK(!memcmp(last_command + 55, name, sizeof(name)));
	CHECK(be32(last_command + 91) == ticket.hierarchy);
	CHECK(last_command[89] == 0x80 && last_command[90] == 0x22);
	CHECK(last_command[95] == 0 &&
		last_command[96] == ticket.digest_size);
	CHECK(!memcmp(last_command + 97, ticket.digest, ticket.digest_size));
	for (size_t size = 384; size <= 512; size += 128) {
		uint8_t large_signature[TLCL2_RSA_MODULUS_MAX_SIZE];

		object.rsa_modulus_size = size;
		memset(large_signature, size, sizeof(large_signature));
		CHECK(tlcl2_verify_rsa_signature(&object, digest,
			sizeof(digest), large_signature, size, &ticket) ==
			TPM_SUCCESS);
		check_header(TPM_ST_NO_SESSIONS, 0x00000177, 54 + size);
		CHECK(!memcmp(last_command + 54, large_signature, size));
	}
	for (size_t size = SHA1_DIGEST_SIZE;
	     size <= SHA512_DIGEST_SIZE; size += 4) {
		if (size != SHA1_DIGEST_SIZE && size != SHA256_DIGEST_SIZE &&
		    size != SHA384_DIGEST_SIZE && size != SHA512_DIGEST_SIZE)
			continue;
		verified_ticket_size = size;
		object.rsa_modulus_size = sizeof(signature);
		CHECK(tlcl2_verify_rsa_signature(&object, digest,
			sizeof(digest), signature, sizeof(signature), &ticket) ==
			TPM_SUCCESS);
		CHECK(ticket.digest_size == size);
		CHECK(tlcl2_policy_authorize(&session, digest, sizeof(digest),
			policy_ref, sizeof(policy_ref), name, sizeof(name),
			&ticket) == TPM_SUCCESS);
	}
	verified_ticket_size = SHA256_DIGEST_SIZE;
}

static void test_verify_failures(void)
{
	struct tlcl2_external_object object = valid_external_object_for_test();
	struct tlcl2_verified_ticket ticket;
	uint8_t digest[SHA256_DIGEST_SIZE] = { 0 };
	uint8_t signature[256] = { 0 };
	static const enum response_fault faults[] = {
		RESPONSE_TRANSPORT,
		RESPONSE_ERROR,
		RESPONSE_BAD_TAG,
		RESPONSE_BAD_SIZE,
		RESPONSE_TRAILING,
		RESPONSE_BAD_TICKET_TAG,
		RESPONSE_BAD_TICKET_HIERARCHY,
		RESPONSE_BAD_TICKET_SIZE,
		RESPONSE_ZERO_TICKET_SIZE,
		RESPONSE_OVERSIZED_TICKET,
	};

	for (size_t i = 0; i < ARRAY_SIZE(faults); i++) {
		response_fault = faults[i];
		memset(&ticket, 0xa5, sizeof(ticket));
		CHECK(tlcl2_verify_rsa_signature(&object, digest, sizeof(digest),
			signature, sizeof(signature), &ticket) != TPM_SUCCESS);
		CHECK(!memcmp(&ticket, &(struct tlcl2_verified_ticket){ 0 },
			sizeof(ticket)));
	}
	response_fault = RESPONSE_OK;
	CHECK(tlcl2_verify_rsa_signature(&object, digest, 31, signature,
		sizeof(signature), &ticket) == TPM_CB_RANGE);
	CHECK(tlcl2_verify_rsa_signature(&object, digest, sizeof(digest),
		signature, 255, &ticket) == TPM_CB_RANGE);
	object.handle = HR_POLICY_SESSION;
	CHECK(tlcl2_verify_rsa_signature(&object, digest, sizeof(digest),
		signature, sizeof(signature), &ticket) == TPM_CB_RANGE);
	CHECK(!memcmp(&ticket, &(struct tlcl2_verified_ticket){ 0 },
		sizeof(ticket)));
}

static void test_policy_authorize_inputs(void)
{
	struct tlcl2_policy_session session = valid_session();
	struct tlcl2_verified_ticket ticket = valid_ticket();
	uint8_t digest[TLCL2_POLICY_DIGEST_MAX_SIZE] = { 0 };
	uint8_t name[34] = { 0, TPM_ALG_SHA256 };
	uint8_t reference[TLCL2_POLICY_OPERAND_MAX_SIZE + 1] = { 0 };
	static const struct {
		uint16_t algorithm;
		uint16_t size;
	} hashes[] = {
		{ TPM_ALG_SHA1, SHA1_DIGEST_SIZE },
		{ TPM_ALG_SHA256, SHA256_DIGEST_SIZE },
		{ TPM_ALG_SHA384, SHA384_DIGEST_SIZE },
		{ TPM_ALG_SHA512, SHA512_DIGEST_SIZE },
	};

	CHECK(tlcl2_policy_authorize(&session, digest, 31, NULL, 0, name,
		sizeof(name), &ticket) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_authorize(&session, digest, SHA256_DIGEST_SIZE,
		reference, sizeof(reference), name, sizeof(name), &ticket) ==
		TPM_CB_RANGE);
	CHECK(tlcl2_policy_authorize(&session, digest, SHA256_DIGEST_SIZE,
		NULL, 0,
		name, 33, &ticket) == TPM_CB_RANGE);
	name[1] = TPM_ALG_SHA1;
	CHECK(tlcl2_policy_authorize(&session, digest, SHA256_DIGEST_SIZE,
		NULL, 0,
		name, sizeof(name), &ticket) == TPM_CB_RANGE);
	name[1] = TPM_ALG_SHA256;
	ticket.tag ^= 1;
	CHECK(tlcl2_policy_authorize(&session, digest, SHA256_DIGEST_SIZE,
		NULL, 0,
		name, sizeof(name), &ticket) == TPM_CB_RANGE);
	ticket = valid_ticket();
	ticket.hierarchy = TPM_RH_NULL;
	CHECK(tlcl2_policy_authorize(&session, digest, SHA256_DIGEST_SIZE,
		NULL, 0,
		name, sizeof(name), &ticket) == TPM_CB_RANGE);
	ticket = valid_ticket();
	for (size_t size = 0; size <= TLCL2_POLICY_DIGEST_MAX_SIZE; size++) {
		if (size == SHA1_DIGEST_SIZE || size == SHA256_DIGEST_SIZE ||
		    size == SHA384_DIGEST_SIZE || size == SHA512_DIGEST_SIZE)
			continue;
		ticket.digest_size = size;
		CHECK(tlcl2_policy_authorize(&session, digest,
			SHA256_DIGEST_SIZE, NULL, 0, name, sizeof(name),
			&ticket) == TPM_CB_RANGE);
	}
	ticket = valid_ticket();
	for (size_t i = 0; i < ARRAY_SIZE(hashes); i++) {
		session.hash_algorithm = hashes[i].algorithm;
		session.nonce_size = hashes[i].size;
		CHECK(tlcl2_policy_authorize(&session, digest, hashes[i].size,
			NULL, 0, name, sizeof(name), &ticket) == TPM_SUCCESS);
		CHECK(tlcl2_policy_authorize(&session, digest,
			hashes[i].size - 1, NULL, 0, name, sizeof(name),
			&ticket) == TPM_CB_RANGE);
	}
}

static void test_policy_nv_mutation(void)
{
	struct tlcl2_policy_session session = valid_session();
	uint8_t data[] = { 0xde, 0xad, 0xbe, 0xef };
	static const enum response_fault faults[] = {
		RESPONSE_TRANSPORT,
		RESPONSE_ERROR,
		RESPONSE_BAD_TAG,
		RESPONSE_BAD_SIZE,
		RESPONSE_TRAILING,
		RESPONSE_TRUNCATED,
	};

	CHECK(tlcl2_policy_nv_write(&session, 0x1234, data, sizeof(data), 7) ==
		TPM_SUCCESS);
	check_header(TPM_ST_SESSIONS, TPM2_NV_Write, 39);
	CHECK(be32(last_command + 10) == HR_NV_INDEX + 0x1234);
	CHECK(be32(last_command + 14) == HR_NV_INDEX + 0x1234);
	CHECK(be32(last_command + 18) == 9);
	CHECK(be32(last_command + 22) == session.handle);
	CHECK(!memcmp(last_command + 26,
		(const uint8_t[]){ 0, 0, 1, 0, 0 }, 5));
	CHECK(last_command[31] == 0 && last_command[32] == sizeof(data));
	CHECK(!memcmp(last_command + 33, data, sizeof(data)));
	CHECK(last_command[37] == 0 && last_command[38] == 7);
	CHECK(tlcl2_policy_nv_write_lock(&session, 0x1234) == TPM_SUCCESS);
	check_header(TPM_ST_SESSIONS, TPM2_NV_WriteLock, 31);
	CHECK(be32(last_command + 18) == 9);
	CHECK(be32(last_command + 22) == session.handle);
	for (response_fault = RESPONSE_BAD_PARAMETER_SIZE;
	     response_fault <= RESPONSE_AUTH_HMAC; response_fault++) {
		CHECK(tlcl2_policy_nv_write(&session, 0x1234, data,
			sizeof(data), 0) != TPM_SUCCESS);
		CHECK(tlcl2_policy_nv_write_lock(&session, 0x1234) != TPM_SUCCESS);
	}
	for (size_t i = 0; i < ARRAY_SIZE(faults); i++) {
		response_fault = faults[i];
		CHECK(tlcl2_policy_nv_write(&session, 0x1234, data,
			sizeof(data), 0) != TPM_SUCCESS);
		CHECK(tlcl2_policy_nv_write_lock(&session, 0x1234) !=
			TPM_SUCCESS);
	}
	response_fault = RESPONSE_OK;
	CHECK(tlcl2_policy_nv_write(&session, 0x1000000, data,
		sizeof(data), 0) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_nv_write(&session, 0, NULL, 0, 0) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_nv_write(&session, 0, data,
		TLCL2_POLICY_OPERAND_MAX_SIZE + 1, 0) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_nv_write(&session, 0, data, sizeof(data),
		UINT16_MAX - sizeof(data)) == TPM_SUCCESS);
	CHECK(tlcl2_policy_nv_write(&session, 0, data, sizeof(data),
		UINT16_MAX - sizeof(data) + 1) == TPM_CB_RANGE);
	CHECK(tlcl2_policy_nv_write_lock(&session, 0x1000000) == TPM_CB_RANGE);
	session.hash_algorithm = TPM_ALG_NULL;
	CHECK(tlcl2_policy_nv_write(&session, 0, data, sizeof(data), 0) ==
		TPM_CB_RANGE);
	CHECK(tlcl2_policy_nv_write_lock(&session, 0) == TPM_CB_RANGE);
}

int main(void)
{
	tlcl_tis_sendrecv = fake_sendrecv;
	test_start_and_flush();
	test_policy_commands();
	test_guaranteed_flush();
	test_start_cleanup();
	test_flush_zeroes_session();
	test_invalid_inputs();
	test_response_failures();
	test_load_external();
	test_external_cleanup();
	test_external_inputs();
	test_verify_and_authorize();
	test_verify_failures();
	test_policy_authorize_inputs();
	test_policy_nv_mutation();
	test_scoped_transport();
	return 0;
}
