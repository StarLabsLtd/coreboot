/* SPDX-License-Identifier: GPL-2.0-only */
#include <limits.h>
#include <stdint.h>

#include <mbedtls/asn1.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform.h>
#include <mbedtls/rsa.h>

#include "pk_wrap.h"

/*
 * The capsule verifier only accepts RSA public keys.  Mbed TLS' general RSA
 * PK vtable also roots private-key, signing, encryption and decryption code.
 * Publish the deliberately smaller vtable needed by X.509 parsing and
 * signature verification instead of linking those unreachable operations.
 */
static int rsa_can_do(mbedtls_pk_type_t type)
{
	return type == MBEDTLS_PK_RSA || type == MBEDTLS_PK_RSASSA_PSS;
}

static size_t rsa_get_bitlen(mbedtls_pk_context *pk)
{
	return mbedtls_rsa_get_bitlen(mbedtls_pk_rsa(*pk));
}

static int rsa_verify(mbedtls_pk_context *pk, mbedtls_md_type_t md_alg,
		      const unsigned char *hash, size_t hash_len,
		      const unsigned char *signature, size_t signature_len)
{
	mbedtls_rsa_context *rsa = mbedtls_pk_rsa(*pk);
	size_t rsa_len = mbedtls_rsa_get_len(rsa);
	int status;

#if SIZE_MAX > UINT_MAX
	if (md_alg == MBEDTLS_MD_NONE && hash_len > UINT_MAX)
		return MBEDTLS_ERR_PK_BAD_INPUT_DATA;
#endif
	if (signature_len < rsa_len)
		return MBEDTLS_ERR_RSA_VERIFY_FAILED;
	status = mbedtls_rsa_pkcs1_verify(rsa, md_alg, (unsigned int)hash_len,
					 hash, signature);
	if (status != 0)
		return status;
	if (signature_len != rsa_len)
		return MBEDTLS_ERR_PK_SIG_LEN_MISMATCH;
	return 0;
}

static void *rsa_alloc(void)
{
	mbedtls_rsa_context *rsa;

	rsa = mbedtls_calloc(1, sizeof(*rsa));
	if (rsa != NULL)
		mbedtls_rsa_init(rsa);
	return rsa;
}

static void rsa_free(void *context)
{
	mbedtls_rsa_free(context);
	mbedtls_free(context);
}

const mbedtls_pk_info_t mbedtls_rsa_info = {
	.type = MBEDTLS_PK_RSA,
	.name = "RSA",
	.get_bitlen = rsa_get_bitlen,
	.can_do = rsa_can_do,
	.verify_func = rsa_verify,
	.sign_func = NULL,
	.decrypt_func = NULL,
	.encrypt_func = NULL,
	.check_pair_func = NULL,
	.ctx_alloc_func = rsa_alloc,
	.ctx_free_func = rsa_free,
	.debug_func = NULL,
};

#if defined(MBEDTLS_PK_HAVE_ECC_KEYS)
/* EC public-key containers adapted from Mbed TLS pk_wrap.c.
 * Copyright The Mbed TLS Contributors.
 * The union configuration enables EC parsing, not EC certificate trust.
 * Certificate admission still requires RSA; no private-key operations belong here.
 */
static size_t ec_get_bitlen(mbedtls_pk_context *pk)
{
	return mbedtls_pk_ec(*pk)->grp.pbits;
}

static int ec_can_do(mbedtls_pk_type_t type)
{
	return type == MBEDTLS_PK_ECKEY || type == MBEDTLS_PK_ECKEY_DH;
}

static void *ec_alloc(void)
{
	mbedtls_ecp_keypair *key = mbedtls_calloc(1, sizeof(*key));

	if (key)
		mbedtls_ecp_keypair_init(key);
	return key;
}

static void ec_free(void *context)
{
	mbedtls_ecp_keypair_free(context);
	mbedtls_free(context);
}

const mbedtls_pk_info_t mbedtls_eckey_info = {
	.type = MBEDTLS_PK_ECKEY,
	.name = "EC",
	.get_bitlen = ec_get_bitlen,
	.can_do = ec_can_do,
	.ctx_alloc_func = ec_alloc,
	.ctx_free_func = ec_free,
};

const mbedtls_pk_info_t mbedtls_eckeydh_info = {
	.type = MBEDTLS_PK_ECKEY_DH,
	.name = "EC_DH",
	.get_bitlen = ec_get_bitlen,
	.can_do = ec_can_do,
	.ctx_alloc_func = ec_alloc,
	.ctx_free_func = ec_free,
};
#endif

int __wrap_mbedtls_rsa_parse_pubkey(mbedtls_rsa_context *rsa,
	const unsigned char *key, size_t key_len);

int __wrap_mbedtls_rsa_parse_pubkey(mbedtls_rsa_context *rsa,
	const unsigned char *key, size_t key_len)
{
	unsigned char *cursor = (unsigned char *)key;
	unsigned char *end = cursor + key_len;
	size_t length;
	int status;

	status = mbedtls_asn1_get_tag(&cursor, end, &length,
		MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE);
	if (status != 0)
		return status;
	if (end != cursor + length)
		return MBEDTLS_ERR_RSA_BAD_INPUT_DATA;
	status = mbedtls_asn1_get_tag(&cursor, end, &length,
		MBEDTLS_ASN1_INTEGER);
	if (status != 0)
		return status;
	if (mbedtls_rsa_import_raw(rsa, cursor, length, NULL, 0, NULL, 0,
		NULL, 0, NULL, 0) != 0)
		return MBEDTLS_ERR_RSA_BAD_INPUT_DATA;
	cursor += length;
	status = mbedtls_asn1_get_tag(&cursor, end, &length,
		MBEDTLS_ASN1_INTEGER);
	if (status != 0)
		return status;
	if (mbedtls_rsa_import_raw(rsa, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
		cursor, length) != 0)
		return MBEDTLS_ERR_RSA_BAD_INPUT_DATA;
	cursor += length;
	if (mbedtls_rsa_check_pubkey(rsa) != 0)
		return MBEDTLS_ERR_RSA_BAD_INPUT_DATA;
	if (cursor != end)
		return MBEDTLS_ERR_ASN1_LENGTH_MISMATCH;
	return 0;
}
