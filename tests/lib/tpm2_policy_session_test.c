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
};

tis_sendrecv_fn tlcl_tis_sendrecv;
static enum response_fault response_fault;
static uint8_t last_command[TPM_BUFFER_SIZE];
static size_t last_command_size;
static unsigned int flush_count;
static bool fail_flush;
static bool fault_once;

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
	size_t size_position;

	CHECK(send_size <= sizeof(last_command));
	memcpy(last_command, send, send_size);
	last_command_size = send_size;
	command_code = be32(send + 6);
	if (command_code == 0x00000165)
		flush_count++;
	if (fault == RESPONSE_TRANSPORT ||
	    (command_code == 0x00000165 && fail_flush))
		return TPM_IOERROR;
	if (command_code == 0x00000149 && !response_code)
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
	    command_code == 0x00000176)
		*receive_size = 14;
	if (fault == RESPONSE_TRUNCATED_HANDLE &&
	    command_code == 0x00000176)
		*receive_size = 13;
	if (fault == RESPONSE_OVERSIZED)
		*receive_size = TPM_BUFFER_SIZE + 1;
	if (fault_once && command_code == 0x00000176) {
		response_fault = RESPONSE_OK;
		fault_once = false;
	}
	return TPM_SUCCESS;
}

static struct tlcl2_policy_session valid_session(void)
{
	struct tlcl2_policy_session session = {
		.handle = HR_POLICY_SESSION + 7,
		.hash_algorithm = TPM_ALG_SHA256,
		.nonce_size = SHA256_DIGEST_SIZE,
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
	return 0;
}
