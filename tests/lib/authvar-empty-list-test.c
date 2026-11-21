/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include "lib/payload_mm_authvar_signature_db.c"
#include "lib/authvar_fixed_policy.c"

/* These hash-only fixtures must never enter certificate verification. */
void mbedtls_x509_crt_init(mbedtls_x509_crt *crt)
{
	(void)crt;
	fail();
}

void mbedtls_x509_crt_free(mbedtls_x509_crt *crt)
{
	(void)crt;
	fail();
}

int mbedtls_x509_crt_parse_der_nocopy(mbedtls_x509_crt *crt,
	const unsigned char *data, size_t size)
{
	(void)crt;
	(void)data;
	(void)size;
	fail();
	return -1;
}

int mbedtls_pk_can_do(const mbedtls_pk_context *ctx, mbedtls_pk_type_t type)
{
	(void)ctx;
	(void)type;
	fail();
	return 0;
}

size_t mbedtls_pk_get_bitlen(const mbedtls_pk_context *ctx)
{
	(void)ctx;
	fail();
	return 0;
}

static void empty_list(void **state)
{
	enum {
		header_size = SIGNATURE_LIST_HEADER_SIZE,
		signature_size = SIGNATURE_OWNER_SIZE + 32,
		populated_size = header_size + signature_size,
	};
	struct payload_mm_crypto_owner owner = { 0 };
	struct payload_mm_authvar_signature_db_stats stats;
	uint8_t database[header_size + populated_size] = { 0 };
	struct payload_mm_authvar_span required = { database + header_size, populated_size };
	struct payload_mm_authvar_span actual = { database, sizeof(database) };

	(void)state;

	memcpy(database, signature_types[0].guid, 16);
	write_le32(database + 16, header_size);
	write_le32(database + 24, signature_size);
	memcpy(database + header_size, database, header_size);
	write_le32(database + header_size + 16, populated_size);
	assert_int_equal(payload_mm_authvar_signature_db_validate(&owner, database,
		sizeof(database), PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, 0, &stats),
		PAYLOAD_MM_VERIFY_OK);
	assert_int_equal(stats.signature_count, 1);
	assert_int_equal(require_revocations(required, actual), PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert_int_equal(payload_mm_authvar_signature_db_validate(&owner, database,
		sizeof(database), PAYLOAD_MM_AUTHVAR_PLATFORM_KEY, 0, NULL),
		PAYLOAD_MM_VERIFY_MALFORMED);

	/* Empty lists still carry a type, header and stride contract. */
	database[0] ^= 1;
	assert_int_equal(payload_mm_authvar_signature_db_validate(&owner, database,
		sizeof(database), PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, 0, NULL),
		PAYLOAD_MM_VERIFY_MALFORMED);
	database[0] ^= 1;
	write_le32(database + 24, signature_size - 1);
	assert_int_equal(payload_mm_authvar_signature_db_validate(&owner, database,
		sizeof(database), PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, 0, NULL),
		PAYLOAD_MM_VERIFY_MALFORMED);
	write_le32(database + 24, signature_size);
	write_le32(database + 20, 1);
	assert_int_equal(payload_mm_authvar_signature_db_validate(&owner, database,
		sizeof(database), PAYLOAD_MM_AUTHVAR_SIGNATURE_DB, 0, NULL),
		PAYLOAD_MM_VERIFY_MALFORMED);
	/* Even a correctly typed empty X509 list cannot serve as PK. */
	write_le32(database + 20, 0);
	write_le32(database + 24, SIGNATURE_OWNER_SIZE + 1);
	memcpy(database, x509_guid, sizeof(x509_guid));
	assert_int_equal(payload_mm_authvar_signature_db_validate(&owner, database,
		header_size, PAYLOAD_MM_AUTHVAR_PLATFORM_KEY, 0, NULL),
		PAYLOAD_MM_VERIFY_MALFORMED);
}

static void append_to_empty(void **state)
{
	enum {
		header_size = SIGNATURE_LIST_HEADER_SIZE,
		signature_size = SIGNATURE_OWNER_SIZE + 32,
	};
	struct payload_mm_crypto_owner owner = { 0 };
	uint8_t current[header_size] = { 0 };
	uint8_t append[header_size + signature_size] = { 0 };
	uint8_t output[sizeof(append)];
	size_t output_size = 0;

	(void)state;
	memcpy(current, signature_types[0].guid, 16);
	write_le32(current + 16, sizeof(current));
	write_le32(current + 24, signature_size);
	memcpy(append, current, sizeof(current));
	write_le32(append + 16, sizeof(append));
	assert_int_equal(payload_mm_authvar_signature_db_filter_append(&owner,
		current, sizeof(current), append, sizeof(append), output, sizeof(output),
		&output_size), PAYLOAD_MM_VERIFY_OK);
	assert_int_equal(output_size, sizeof(append));
	assert_memory_equal(output, append, sizeof(append));
	/* Appending no entries must not publish bytes or poison the arena owner. */
	assert_int_equal(payload_mm_authvar_signature_db_filter_append(&owner,
		append, sizeof(append), current, sizeof(current), output, sizeof(output),
		&output_size), PAYLOAD_MM_VERIFY_OK);
	assert_int_equal(output_size, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(empty_list),
		cmocka_unit_test(append_to_empty),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
