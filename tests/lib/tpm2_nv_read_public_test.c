/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <security/tpm/tss.h>
#include <string.h>

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

enum response_kind {
	RESPONSE_SUCCESS,
	RESPONSE_TPM_ERROR,
	RESPONSE_TRANSPORT_ERROR,
	RESPONSE_BAD_PUBLIC_SIZE,
	RESPONSE_LARGE_POLICY,
	RESPONSE_BAD_POLICY_SIZE,
	RESPONSE_ALG_ERROR,
	RESPONSE_BAD_NAME_ALG,
	RESPONSE_BAD_NAME_DIGEST,
	RESPONSE_BAD_NAME_SIZE,
	RESPONSE_BAD_TAG,
	RESPONSE_WRONG_INDEX,
	RESPONSE_OVERSIZED_RETURN,
};

struct vb2_context;

static enum response_kind response_kind;
static bool auth_read_expected;
static unsigned int read_calls;

tis_sendrecv_fn tlcl_tis_sendrecv;
enum tpm_family tlcl_tpm_family = TPM_2;

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

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

static void put16(uint8_t **cursor, uint16_t value)
{
	*(*cursor)++ = value >> 8;
	*(*cursor)++ = value;
}

static void put32(uint8_t **cursor, uint32_t value)
{
	*(*cursor)++ = value >> 24;
	*(*cursor)++ = value >> 16;
	*(*cursor)++ = value >> 8;
	*(*cursor)++ = value;
}

static tpm_result_t sendrecv(const uint8_t *send, size_t send_size,
	uint8_t *receive, size_t *receive_size)
{
	static const uint8_t expected[] = {
		0x80, 0x01, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00,
		0x01, 0x69, 0x01, 0x00, 0x12, 0x34,
	};
	uint8_t response[TPM_BUFFER_SIZE] = {0};
	uint8_t *cursor = response;
	uint8_t *size_field;
	uint8_t *public_size_field;
	uint8_t *public_start;
	uint32_t index = response_kind == RESPONSE_WRONG_INDEX ?
		0x01001235 : 0x01001234;
	uint16_t name_alg = TPM_ALG_SHA256;
	uint16_t name_size = sizeof(name_alg) + SHA256_DIGEST_SIZE;
	uint16_t policy_size = SHA256_DIGEST_SIZE;
	const uint8_t *name_digest;
	size_t size;
	static const uint8_t public_digest[SHA256_DIGEST_SIZE] = {
		0xc7, 0x9a, 0x11, 0x68, 0xda, 0x61, 0x38, 0x21,
		0x5b, 0x88, 0x10, 0x3e, 0x35, 0x4e, 0xdf, 0x9e,
		0x67, 0x25, 0xb9, 0xf5, 0x66, 0x57, 0xd7, 0xdb,
		0x99, 0xb6, 0x60, 0x57, 0xf2, 0x76, 0x8d, 0x86,
	};
	static const uint8_t wrong_index_digest[SHA256_DIGEST_SIZE] = {
		0x08, 0x5f, 0x65, 0xc1, 0xce, 0xe0, 0x15, 0xb8,
		0xea, 0x2f, 0x4c, 0xc0, 0x81, 0xec, 0xcd, 0xdf,
		0x52, 0xb4, 0x5b, 0xf5, 0x9e, 0xd2, 0x69, 0x84,
		0x13, 0xd3, 0x96, 0x02, 0x0a, 0xd3, 0xdf, 0x06,
	};

	if (response_kind == RESPONSE_ALG_ERROR) {
		name_alg = TPM_ALG_ERROR;
		name_size = sizeof(name_alg) + 1;
		policy_size = 0;
	}
	name_digest = response_kind == RESPONSE_WRONG_INDEX ?
		wrong_index_digest : public_digest;

	CHECK(send_size == sizeof(expected));
	CHECK(memcmp(send, expected, sizeof(expected)) == 0);
	if (response_kind == RESPONSE_TRANSPORT_ERROR)
		return TPM_IOERROR;
	put16(&cursor, response_kind == RESPONSE_BAD_TAG ? TPM_ST_SESSIONS :
		TPM_ST_NO_SESSIONS);
	size_field = cursor;
	put32(&cursor, 0);
	put32(&cursor, response_kind == RESPONSE_TPM_ERROR ? 0x18b : 0);
	if (response_kind == RESPONSE_TPM_ERROR)
		goto finish;
	public_size_field = cursor;
	put16(&cursor, 0);
	public_start = cursor;
	put32(&cursor, index);
	put16(&cursor, name_alg);
	put32(&cursor, 0x42042042);
	if (response_kind == RESPONSE_LARGE_POLICY)
		policy_size = SHA512_DIGEST_SIZE + 1;
	if (response_kind == RESPONSE_BAD_POLICY_SIZE)
		policy_size--;
	put16(&cursor, policy_size);
	for (uint16_t i = 0; i < policy_size; i++)
		*cursor++ = i;
	put16(&cursor, 40);
	public_size_field[0] = (cursor - public_start) >> 8;
	public_size_field[1] = cursor - public_start;
	if (response_kind == RESPONSE_BAD_PUBLIC_SIZE)
		public_size_field[1]++;
	if (response_kind == RESPONSE_BAD_NAME_SIZE)
		name_size--;
	put16(&cursor, name_size);
	put16(&cursor, response_kind == RESPONSE_BAD_NAME_ALG ?
		TPM_ALG_SHA1 : name_alg);
	if (response_kind == RESPONSE_ALG_ERROR)
		*cursor++ = 0xa5;
	else
		for (size_t i = 0; i < sizeof(public_digest); i++)
			*cursor++ = name_digest[i] ^
				(response_kind == RESPONSE_BAD_NAME_DIGEST && !i);
finish:
	size = cursor - response;
	size_field[0] = size >> 24;
	size_field[1] = size >> 16;
	size_field[2] = size >> 8;
	size_field[3] = size;
	CHECK(*receive_size >= size);
	memcpy(receive, response, size);
	*receive_size = size;
	if (response_kind == RESPONSE_OVERSIZED_RETURN)
		*receive_size = TPM_BUFFER_SIZE + 1;
	return TPM_SUCCESS;
}

static tpm_result_t scoped_sendrecv(void *context, const uint8_t *send,
	size_t send_size, uint8_t *receive, size_t *receive_size)
{
	CHECK(context == &response_kind);
	return sendrecv(send, send_size, receive, receive_size);
}

struct nested_context {
	struct tlcl2_transport inner_transport;
	struct tlcl2_nv_public inner_public;
	bool entered;
};

static tpm_result_t nested_sendrecv(void *opaque, const uint8_t *send,
	size_t send_size, uint8_t *receive, size_t *receive_size)
{
	struct nested_context *context = opaque;

	CHECK(!context->entered);
	context->entered = true;
	CHECK(tlcl2_read_public_on(&context->inner_transport, 0x1234,
		&context->inner_public) == TPM_SUCCESS);
	return sendrecv(send, send_size, receive, receive_size);
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

static tpm_result_t read_sendrecv(void *context, const uint8_t *send,
	size_t send_size, uint8_t *receive, size_t *receive_size)
{
	static const uint8_t platform_expected[] = {
		0x80, 0x02, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00,
		0x01, 0x4e, 0x40, 0x00, 0x00, 0x0c, 0x01, 0x00,
		0x12, 0x34, 0x00, 0x00, 0x00, 0x09, 0x40, 0x00,
		0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x28, 0x00, 0x00,
	};
	static const uint8_t auth_expected[] = {
		0x80, 0x02, 0x00, 0x00, 0x00, 0x23, 0x00, 0x00,
		0x01, 0x4e, 0x01, 0x00, 0x12, 0x34, 0x01, 0x00,
		0x12, 0x34, 0x00, 0x00, 0x00, 0x09, 0x40, 0x00,
		0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x28, 0x00, 0x00,
	};
	const uint8_t *expected = auth_read_expected ? auth_expected :
		platform_expected;
	uint8_t *cursor = receive;
	uint8_t *size_field;
	size_t size;

	read_calls++;
	CHECK(send_size == sizeof(platform_expected));
	CHECK(!memcmp(send, expected, sizeof(platform_expected)));
	CHECK(*receive_size >= 61);
	put16(&cursor, TPM_ST_SESSIONS);
	size_field = cursor;
	put32(&cursor, 0);
	put32(&cursor, 0);
	put32(&cursor, 42);
	put16(&cursor, 40);
	for (uint8_t i = 0; i < 40; i++)
		*cursor++ = i;
	put16(&cursor, 0);
	*cursor++ = 0;
	put16(&cursor, 0);
	size = cursor - receive;
	size_field[0] = size >> 24;
	size_field[1] = size >> 16;
	size_field[2] = size >> 8;
	size_field[3] = size;
	*receive_size = context ? TPM_BUFFER_SIZE + 1 : size;
	return TPM_SUCCESS;
}

static tpm_result_t legacy_read_sendrecv(const uint8_t *send,
	size_t send_size, uint8_t *receive, size_t *receive_size)
{
	return read_sendrecv(NULL, send, send_size, receive, receive_size);
}

static void expect_failure(enum response_kind kind)
{
	struct tlcl2_nv_public public;

	memset(&public, 0xa5, sizeof(public));
	response_kind = kind;
	CHECK(tlcl2_read_public(0x1234, &public) != TPM_SUCCESS);
	for (size_t i = 0; i < sizeof(public); i++)
		CHECK(((uint8_t *)&public)[i] == 0);
}

int main(void)
{
	struct tlcl2_nv_public public;
	const struct tlcl2_transport transport = {
		.sendrecv = scoped_sendrecv,
		.context = &response_kind,
	};
	struct tlcl2_transport read_transport = {
		.sendrecv = read_sendrecv,
	};
	struct nested_context nested = {
		.inner_transport = transport,
	};
	struct tlcl2_transport nested_transport = {
		.sendrecv = nested_sendrecv,
		.context = &nested,
	};
	union {
		struct tlcl2_transport transport;
		struct tlcl2_nv_public public;
	} alias;
	union {
		struct tlcl2_transport transport;
		uint8_t value[40];
	} read_alias;
	uint8_t value[40];

	tlcl_tis_sendrecv = sendrecv;
	response_kind = RESPONSE_SUCCESS;
	CHECK(tlcl2_read_public(0x1234, &public) == TPM_SUCCESS);
	CHECK(public.index == 0x1234);
	tlcl_tis_sendrecv = forbidden_sendrecv;
	CHECK(tlcl2_read_public_on(&nested_transport, 0x1234, &public) ==
		TPM_SUCCESS);
	CHECK(nested.entered && nested.inner_public.index == 0x1234);
	alias.transport = transport;
	CHECK(tlcl2_read_public_on(&alias.transport, 0x1234,
		&alias.public) == TPM_SUCCESS);
	CHECK(alias.public.index == 0x1234);
	response_kind = RESPONSE_OVERSIZED_RETURN;
	memset(&public, 0xa5, sizeof(public));
	CHECK(tlcl2_read_public_on(&transport, 0x1234, &public) != TPM_SUCCESS);
	CHECK(!memcmp(&public, &(struct tlcl2_nv_public){ 0 }, sizeof(public)));
	response_kind = RESPONSE_SUCCESS;
	CHECK(tlcl2_read_public_on(&transport, 0x1234, &public) == TPM_SUCCESS);
	CHECK(public.name_alg == TPM_ALG_SHA256);
	CHECK(public.attributes == 0x42042042);
	CHECK(public.auth_policy_size == SHA256_DIGEST_SIZE);
	CHECK(public.auth_policy[0] == 0 &&
		public.auth_policy[SHA256_DIGEST_SIZE - 1] ==
		SHA256_DIGEST_SIZE - 1);
	CHECK(public.data_size == 40);
	CHECK(tlcl2_read_public_on(&transport, 0x1234, &public) == TPM_SUCCESS);
	CHECK(public.index == 0x1234);
	CHECK(tlcl2_read_on(&read_transport, 0x1234, value,
		sizeof(value)) == TPM_SUCCESS);
	for (uint8_t i = 0; i < sizeof(value); i++)
		CHECK(value[i] == i);
	read_transport.context = &public;
	CHECK(tlcl2_read_on(&read_transport, 0x1234, value,
		sizeof(value)) != TPM_SUCCESS);
	read_transport.context = NULL;
	auth_read_expected = true;
	CHECK(tlcl2_read_auth_on(&read_transport, 0x1234, value,
		sizeof(value)) == TPM_SUCCESS);
	read_alias.transport = read_transport;
	CHECK(tlcl2_read_auth_on(&read_alias.transport, 0x1234,
		read_alias.value, sizeof(read_alias.value)) == TPM_SUCCESS);
	for (uint8_t i = 0; i < sizeof(read_alias.value); i++)
		CHECK(read_alias.value[i] == i);
	{
		unsigned int calls = read_calls;
		struct tlcl2_transport invalid = { 0 };

		CHECK(tlcl2_read_auth_on(NULL, 0x1234, value,
			sizeof(value)) == TPM_CB_RANGE);
		CHECK(tlcl2_read_auth_on(&invalid, 0x1234, value,
			sizeof(value)) == TPM_CB_RANGE);
		CHECK(tlcl2_read_on(&read_transport, 0x1234, NULL,
			sizeof(value)) == TPM_CB_RANGE);
		CHECK(tlcl2_read_auth_on(&read_transport, 0x1234, NULL,
			sizeof(value)) == TPM_CB_RANGE);
		CHECK(tlcl2_read_auth_on(&read_transport, 0x01000000, value,
			sizeof(value)) == TPM_CB_RANGE);
		CHECK(read_calls == calls);
	}
	auth_read_expected = false;
	tlcl_tis_sendrecv = legacy_read_sendrecv;
	CHECK(tlcl2_read(0x1234, value, sizeof(value)) == TPM_SUCCESS);
	tlcl_tis_sendrecv = sendrecv;

	expect_failure(RESPONSE_TPM_ERROR);
	expect_failure(RESPONSE_TRANSPORT_ERROR);
	expect_failure(RESPONSE_BAD_PUBLIC_SIZE);
	expect_failure(RESPONSE_LARGE_POLICY);
	expect_failure(RESPONSE_BAD_POLICY_SIZE);
	expect_failure(RESPONSE_ALG_ERROR);
	expect_failure(RESPONSE_BAD_NAME_ALG);
	expect_failure(RESPONSE_BAD_NAME_DIGEST);
	expect_failure(RESPONSE_BAD_NAME_SIZE);
	expect_failure(RESPONSE_BAD_TAG);
	expect_failure(RESPONSE_WRONG_INDEX);

	memset(&public, 0xa5, sizeof(public));
	CHECK(tlcl2_read_public(0x01000000, &public) == TPM_CB_RANGE);
	for (size_t i = 0; i < sizeof(public); i++)
		CHECK(((uint8_t *)&public)[i] == 0);
	CHECK(tlcl2_read_public(0, NULL) == TPM_CB_RANGE);
	return 0;
}
