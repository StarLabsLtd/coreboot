/* SPDX-License-Identifier: BSD-2-Clause-Patent AND GPL-2.0-only */
/* Copyright (c) 2026 Star Labs Systems. All rights reserved. */
/* Adapted from DAVE SecureKeyTpmAuthLib, SecureKeyTpmProtectedCommandLib,
 * SecureKeyTpmFactoryLib, SecureKeyEnrollmentLib and SecureKeyStorageTpmLib
 * at 0f425ebcc9e8.
 */

#include <string.h>
#include <mbedtls/aes.h>
#include <mbedtls/constant_time.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/hmac_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

#include "session.h"

#define TPM_NO_SESSIONS 0x8001U
#define TPM_SESSIONS 0x8002U
#define TPM_SRK 0x81000001U
#define TPM_CC_NV_READ 0x0000014eU
#define TPM_CC_FLUSH 0x00000165U
#define TPM_CC_NV_READ_PUBLIC 0x00000169U
#define TPM_CC_READ_PUBLIC 0x00000173U
#define TPM_CC_START_SESSION 0x00000176U
#define TPM_CC_GET_CAPABILITY 0x0000017aU
#define TPM_CC_GET_RANDOM 0x0000017bU
#define TPM_CC_NV_WRITE 0x00000137U
#define TPM_WIRE_MAX 1200U

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] << 8) | p[1];
}

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void put16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)(value >> 8);
	p[1] = (uint8_t)value;
}

static void put32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value >> 24);
	p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8);
	p[3] = (uint8_t)value;
}

static bool all_zero(const uint8_t *p, size_t size)
{
	uint8_t value = 0U;

	while (size-- != 0U)
		value |= *p++;
	return value == 0U;
}

static void header(uint8_t *command, uint16_t tag, size_t size, uint32_t code)
{
	put16(command, tag);
	put32(command + 2U, (uint32_t)size);
	put32(command + 6U, code);
}

/* The transport returns its status separately from the TPM RC.
 * Parse the complete envelope before treating a handle error as absence.
 */
uint64_t dave_tpm_submit(struct tpm_reader *reader, const uint8_t *command,
	size_t command_size, uint8_t *response, size_t capacity, uint16_t tag,
	size_t *size)
{
	uint64_t status;

	memset(response, 0, capacity);
	*size = 0U;
	status = reader->exchange(reader->transport_context, command, command_size, response, capacity);
	if (status != DAVE_TPM_SUCCESS)
		return !dave_tpm_error(status) ? DAVE_TPM_ABORTED :
			status == DAVE_TPM_NOT_FOUND ? DAVE_TPM_DEVICE_ERROR : status;
	if (capacity < 10U || get32(response + 2U) < 10U || get32(response + 2U) > capacity)
		return DAVE_TPM_PROTOCOL_ERROR;
	*size = get32(response + 2U);
	if (get32(response + 6U) != 0U) {
		if (get16(response) != TPM_NO_SESSIONS || *size != 10U)
			return DAVE_TPM_PROTOCOL_ERROR;
		return get32(response + 6U) == 0x18bU ? DAVE_TPM_NOT_FOUND : DAVE_TPM_DEVICE_ERROR;
	}
	return get16(response) == tag ? DAVE_TPM_SUCCESS : DAVE_TPM_PROTOCOL_ERROR;
}

uint64_t dave_tpm_flush_handle(struct tpm_reader *reader, uint32_t handle)
{
	uint8_t command[14], response[10];
	size_t size;
	uint64_t status = DAVE_TPM_SUCCESS;

	if (handle != 0U) {
		header(command, TPM_NO_SESSIONS, sizeof(command), TPM_CC_FLUSH);
		put32(command + 10U, handle);
		status = dave_tpm_submit(reader, command, sizeof(command), response, sizeof(response),
			TPM_NO_SESSIONS, &size);
		if (status == DAVE_TPM_NOT_FOUND)
			status = DAVE_TPM_DEVICE_ERROR;
		if (!dave_tpm_error(status) && size != sizeof(response))
			status = DAVE_TPM_PROTOCOL_ERROR;
	}
	if (dave_tpm_error(status))
		reader->cleanup_failed = true;
	return status;
}

uint64_t dave_tpm_flush_session(struct tpm_reader *reader, struct tpm_session *session)
{
	uint32_t handle = session->handle;
	uint64_t status = dave_tpm_flush_handle(reader, handle);

	mbedtls_platform_zeroize(session, sizeof(*session));
	if (dave_tpm_error(status))
		session->handle = handle;
	return status;
}

uint64_t dave_tpm_finish_session(struct tpm_reader *reader, struct tpm_session *session,
	uint64_t status)
{
	uint64_t cleanup = dave_tpm_flush_session(reader, session);

	if (cleanup != DAVE_TPM_SUCCESS)
		return dave_tpm_error(cleanup) ? cleanup : DAVE_TPM_ABORTED;
	return reader->cleanup_failed ? DAVE_TPM_ABORTED : status;
}

uint64_t dave_tpm_random32(struct tpm_reader *reader, uint8_t random[32])
{
	uint8_t command[12], response[44];
	size_t size;
	uint64_t status = DAVE_TPM_SUCCESS;
	size_t received = 0;

	header(command, TPM_NO_SESSIONS, sizeof(command), TPM_CC_GET_RANDOM);
	while (received < 32U) {
		size_t count;

		put16(command + 10U, 32U - received);
		status = dave_tpm_submit(reader, command, sizeof(command), response,
			sizeof(response), TPM_NO_SESSIONS, &size);
		if (dave_tpm_error(status))
			break;
		count = size >= 12U ? get16(response + 10U) : 0U;
		if (!count || count > 32U - received || size != 12U + count) {
			status = DAVE_TPM_PROTOCOL_ERROR;
			break;
		}
		memcpy(random + received, response + 12U, count);
		received += count;
	}
	if (!dave_tpm_error(status) && all_zero(random, 32U))
		status = DAVE_TPM_PROTOCOL_ERROR;
	mbedtls_platform_zeroize(response, sizeof(response));
	if (dave_tpm_error(status))
		mbedtls_platform_zeroize(random, 32U);
	return status;
}

uint64_t dave_tpm_public_area(struct tpm_reader *reader, uint32_t handle, bool nv,
	uint8_t *public, size_t capacity, size_t *public_size, uint8_t name[34])
{
	uint8_t command[14], response[TPM_WIRE_MAX], expected[34];
	size_t size, length, offset;
	uint64_t status;

	*public_size = 0U;
	memset(public, 0, capacity);
	memset(name, 0, 34U);
	header(command, TPM_NO_SESSIONS, sizeof(command),
		nv ? TPM_CC_NV_READ_PUBLIC : TPM_CC_READ_PUBLIC);
	put32(command + 10U, handle);
	status = dave_tpm_submit(reader, command, sizeof(command), response, sizeof(response),
		TPM_NO_SESSIONS, &size);
	if (dave_tpm_error(status))
		goto out;
	status = DAVE_TPM_PROTOCOL_ERROR;
	if (size < 12U)
		goto out;
	length = (size_t)get16(response + 10U) + 2U;
	if (length < (nv ? 8U : 6U) || length > capacity || length > size - 10U)
		goto out;
	if (get16(response + 10U + (nv ? 6U : 4U)) != 0x0bU)
		goto out;
	put16(expected, 0x0bU);
	if (mbedtls_sha256(response + 12U, length - 2U, expected + 2U, 0) != 0) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	offset = 10U + length;
	if (size - offset < 36U || get16(response + offset) != 34U ||
	    mbedtls_ct_memcmp(expected, response + offset + 2U, 34U) != 0) {
		status = DAVE_TPM_SECURITY_VIOLATION;
		goto out;
	}
	offset += 36U;
	if (!nv) {
		if (size - offset != 36U || get16(response + offset) != 34U ||
		    get16(response + offset + 2U) != 0x0bU)
			goto out;
		offset += 36U;
	}
	if (offset != size)
		goto out;
	memcpy(public, response + 10U, length);
	memcpy(name, expected, 34U);
	*public_size = length;
	status = DAVE_TPM_SUCCESS;
out:
	mbedtls_platform_zeroize(response, sizeof(response));
	mbedtls_platform_zeroize(expected, sizeof(expected));
	return status;
}

/* KDFa uses the fixed TPM labels ATH and CFB, including their terminating NUL.
 * Parameter encryption includes the entity auth when the command requires it.
 */
int dave_tpm_kdfa(const uint8_t *key, size_t key_size, const char label[4],
	const uint8_t newer[32], const uint8_t older[32], uint8_t output[32])
{
	uint8_t input[76];
	int result;

	put32(input, 1U);
	memcpy(input + 4U, label, 4U);
	memcpy(input + 8U, newer, 32U);
	memcpy(input + 40U, older, 32U);
	put32(input + 72U, 256U);
	result = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
		key, key_size, input, sizeof(input), output);
	mbedtls_platform_zeroize(input, sizeof(input));
	return result;
}

int dave_tpm_session_hmac(const uint8_t *key, size_t key_size, const uint8_t digest[32],
	const uint8_t newer[32], const uint8_t older[32], uint8_t attributes,
	uint8_t output[32])
{
	uint8_t input[97];
	int result;

	memcpy(input, digest, 32U);
	memcpy(input + 32U, newer, 32U);
	memcpy(input + 64U, older, 32U);
	input[96] = attributes;
	result = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
		key, key_size, input, sizeof(input), output);
	mbedtls_platform_zeroize(input, sizeof(input));
	return result;
}

enum { NO_PCR = -1, PCR_COUNT = 24 };

/* Reads and bounded NV writes share authenticated response and cleanup
 * handling. All consume their session except the GetRandom confirmation.
 */
static uint64_t authenticated_command(struct tpm_reader *reader,
	struct tpm_session *session, const struct nv_profile *nv, uint16_t count,
	uint16_t offset, uint8_t *output, bool encrypted, int pcr, const uint8_t *write)
{
	uint8_t selection[] = { 0, 0, 0, 1, 0, 0x0b, 3, 0, 0, 0 };
	uint8_t command[99U + DAVE_TPM_NV_WRITE_MAX], response[TPM_WIRE_MAX];
	uint8_t cp_input[76U + DAVE_TPM_NV_WRITE_MAX], rp_input[1040];
	uint8_t caller[32], cp_hash[32], rp_hash[32], hmac[32], material[32];
	const bool read_pcr = pcr != NO_PCR;
	uint8_t attributes = read_pcr ? 0x80U : nv == NULL ? 0x41U : encrypted ? 0x40U : 0U;
	uint8_t response_attributes = read_pcr ? 0x82U : attributes;
	uint32_t code = write ? TPM_CC_NV_WRITE :
		read_pcr ? 0x0000017eU : nv == NULL ? TPM_CC_GET_RANDOM : TPM_CC_NV_READ;
	size_t returned = count;
	size_t pos = 10U, cp_size = 4U, size, parameter_size, auth;
	mbedtls_aes_context aes;
	uint64_t status = dave_tpm_random32(reader, caller);

	mbedtls_aes_init(&aes);
	if (dave_tpm_error(status))
		goto out;
	if ((read_pcr && (pcr < 0 || pcr >= PCR_COUNT)) ||
	    ((encrypted || nv == NULL || read_pcr) && count != 32U) ||
	    (write && (nv == NULL || read_pcr || encrypted || output || count == 0U ||
	     count > DAVE_TPM_NV_WRITE_MAX || offset > nv->size || count > nv->size - offset)) ||
	    (!write && output == NULL)) {
		status = DAVE_TPM_INVALID_PARAMETER;
		goto out;
	}
	put32(cp_input, code);
	if (nv != NULL) {
		put32(command + pos, nv->index);
		put32(command + pos + 4U, nv->index);
		pos += 8U;
		memcpy(cp_input + cp_size, nv->name, 34U);
		memcpy(cp_input + cp_size + 34U, nv->name, 34U);
		cp_size += 68U;
	}
	if (read_pcr) {
		selection[7 + pcr / 8] = 1U << (pcr % 8);
		memcpy(cp_input + cp_size, selection, sizeof(selection));
		cp_size += sizeof(selection);
	} else {
		put16(cp_input + cp_size, count);
		cp_size += 2U;
		if (write) {
			memcpy(cp_input + cp_size, write, count);
			cp_size += count;
		}
		if (nv != NULL) {
			put16(cp_input + cp_size, offset);
			cp_size += 2U;
		}
	}
	if (mbedtls_sha256(cp_input, cp_size, cp_hash, 0) != 0 ||
	    dave_tpm_session_hmac(session->key, sizeof(session->key), cp_hash,
		caller, session->nonce, attributes, hmac) != 0) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	put32(command + pos, 73U);
	pos += 4U;
	put32(command + pos, session->handle);
	put16(command + pos + 4U, 32U);
	memcpy(command + pos + 6U, caller, 32U);
	command[pos + 38U] = attributes;
	put16(command + pos + 39U, 32U);
	memcpy(command + pos + 41U, hmac, 32U);
	pos += 73U;
	if (read_pcr) {
		memcpy(command + pos, selection, sizeof(selection));
		pos += sizeof(selection);
	} else {
		put16(command + pos, count);
		pos += 2U;
		if (write) {
			memcpy(command + pos, write, count);
			pos += count;
		}
		if (nv != NULL) {
			put16(command + pos, offset);
			pos += 2U;
		}
	}
	header(command, TPM_SESSIONS, pos, code);
	status = dave_tpm_submit(reader, command, pos, response, sizeof(response), TPM_SESSIONS, &size);
	if (dave_tpm_error(status))
		goto out;
	status = DAVE_TPM_PROTOCOL_ERROR;
	if (size < 14U)
		goto out;
	parameter_size = get32(response + 10U);
	if (parameter_size > 1026U ||
	    parameter_size > size - 14U || size - 14U - parameter_size != 69U)
		goto out;
	if (code == TPM_CC_GET_RANDOM) {
		if (parameter_size <= 2U || parameter_size > (size_t)count + 2U)
			goto out;
		returned = parameter_size - 2U;
	} else if (parameter_size != (write ? 0U : read_pcr ? 52U : (size_t)count + 2U)) {
		goto out;
	}
	if (read_pcr) {
		if (memcmp(response + 18U, selection, sizeof(selection)) != 0 ||
		    get32(response + 28U) != 1U || get16(response + 32U) != 32U)
			goto out;
	} else if (!write && get16(response + 14U) != returned) {
		goto out;
	}
	auth = 14U + parameter_size;
	if (get16(response + auth) != 32U || all_zero(response + auth + 2U, 32U) ||
	    response[auth + 34U] != response_attributes || get16(response + auth + 35U) != 32U)
		goto out;
	put32(rp_input, 0U);
	put32(rp_input + 4U, code);
	memcpy(rp_input + 8U, response + 14U, parameter_size);
	if (mbedtls_sha256(rp_input, parameter_size + 8U, rp_hash, 0) != 0 ||
	    dave_tpm_session_hmac(session->key, sizeof(session->key), rp_hash,
		response + auth + 2U, caller,
		response_attributes, hmac) != 0) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	status = DAVE_TPM_SECURITY_VIOLATION;
	if (mbedtls_ct_memcmp(hmac, response + auth + 37U, 32U) != 0)
		goto out;
	if (encrypted || (nv == NULL && !read_pcr)) {
		size_t iv_offset = 0U;

		if (dave_tpm_kdfa(session->key, sizeof(session->key), "CFB", response + auth + 2U, caller, material) != 0 ||
		    mbedtls_aes_setkey_enc(&aes, material, 128U) != 0 ||
		    mbedtls_aes_crypt_cfb128(&aes, MBEDTLS_AES_DECRYPT, returned,
			&iv_offset, material + 16U, response + 16U, response + 16U) != 0) {
			status = DAVE_TPM_DEVICE_ERROR;
			goto out;
		}
	}
	if ((attributes & 1U) != 0U) {
		memcpy(session->nonce, response + auth + 2U, 32U);
	} else {
		/* Authenticated continueSession=0 is the TPM's resource release. */
		mbedtls_platform_zeroize(session, sizeof(*session));
	}
	if (!write)
		memcpy(output, response + (read_pcr ? 34U : 16U), returned);
	status = DAVE_TPM_SUCCESS;
out:
	mbedtls_aes_free(&aes);
	mbedtls_platform_zeroize(command, sizeof(command));
	mbedtls_platform_zeroize(response, sizeof(response));
	mbedtls_platform_zeroize(cp_input, sizeof(cp_input));
	mbedtls_platform_zeroize(rp_input, sizeof(rp_input));
	mbedtls_platform_zeroize(caller, sizeof(caller));
	mbedtls_platform_zeroize(cp_hash, sizeof(cp_hash));
	mbedtls_platform_zeroize(rp_hash, sizeof(rp_hash));
	mbedtls_platform_zeroize(hmac, sizeof(hmac));
	mbedtls_platform_zeroize(material, sizeof(material));
	if (dave_tpm_error(status)) {
		uint64_t cleanup = dave_tpm_flush_session(reader, session);

		if (output != NULL)
			mbedtls_platform_zeroize(output, count);
		if (dave_tpm_error(cleanup))
			status = cleanup;
	}
	return status;
}

uint64_t dave_tpm_authenticated_read(struct tpm_reader *reader,
	struct tpm_session *session, const struct nv_profile *nv, uint16_t count,
	uint16_t offset, uint8_t *output, bool encrypted)
{
	return authenticated_command(reader, session, nv, count, offset, output, encrypted,
				     NO_PCR, NULL);
}

uint64_t dave_tpm_authenticated_write(struct tpm_reader *reader,
	struct tpm_session *session, const struct nv_profile *profile, uint16_t count,
	uint16_t offset, const uint8_t *data)
{
	return authenticated_command(reader, session, profile, count, offset, NULL, false,
				     NO_PCR, data);
}

uint64_t dave_tpm_read_pcr(struct tpm_reader *reader, uint8_t pcr, uint8_t value[32])
{
	struct tpm_session session = {0};
	uint64_t status;

	if (!value)
		return DAVE_TPM_INVALID_PARAMETER;
	memset(value, 0, 32U);
	if (!reader || !reader->exchange ||
	    !reader->random || pcr >= PCR_COUNT)
		return DAVE_TPM_INVALID_PARAMETER;
	if (reader->cleanup_failed)
		return DAVE_TPM_ABORTED;
	status = dave_tpm_start_session(reader, &session, false);
	if (status == DAVE_TPM_SUCCESS)
		status = authenticated_command(reader, &session, NULL, 32U, 0U,
			value, false, pcr, NULL);
	return status;
}

static void pcr_selection(uint8_t selection[10], uint32_t mask)
{
	put32(selection, 1U);
	put16(selection + 4, 0x0bU);
	selection[6] = 3;
	for (size_t i = 0; i < 3; i++)
		selection[7 + i] = (uint8_t)(mask >> (8 * i));
}

int dave_tpm_pcr_policy_digest(uint32_t mask, const uint8_t pcr_digest[32], uint8_t policy[32])
{
	uint8_t input[78];

	if (!pcr_digest || !policy || !mask || mask >= (1U << PCR_COUNT))
		return -1;
	memcpy(input, policy, 32);
	put32(input + 32, 0x0000017fU);
	pcr_selection(input + 36, mask);
	memcpy(input + 46, pcr_digest, 32);
	return mbedtls_sha256(input, sizeof(input), policy, 0);
}

/* digest is SHA-256 of the selected SHA-256 PCR values in index order. */
uint64_t dave_tpm_policy_pcr(struct tpm_reader *reader, const struct tpm_session *session,
	uint32_t mask, const uint8_t digest[32])
{
	uint8_t command[58], response[10];
	size_t size;
	uint64_t status;

	if (!reader || !reader->exchange || !session || session->handle >> 24 != 3U || !digest ||
	    !mask || mask >= (1U << PCR_COUNT))
		return DAVE_TPM_INVALID_PARAMETER;
	if (reader->cleanup_failed)
		return DAVE_TPM_ABORTED;
	header(command, TPM_NO_SESSIONS, sizeof(command), 0x0000017fU);
	put32(command + 10U, session->handle);
	put16(command + 14U, 32U);
	memcpy(command + 16U, digest, 32U);
	pcr_selection(command + 48, mask);
	status = dave_tpm_submit(reader, command, sizeof(command), response, sizeof(response),
				 TPM_NO_SESSIONS, &size);
	return !dave_tpm_error(status) && size != sizeof(response) ?
		DAVE_TPM_PROTOCOL_ERROR : status;
}

static int authority_public(const uint8_t authority[65], uint8_t public[88], uint8_t name[34])
{
	static const uint8_t public_header[] = {
		0, 0x23, 0, 0x0b, 0, 4, 0, 0, 0, 0,
		0, 0x10, 0, 0x18, 0, 0x0b, 0, 3, 0, 0x10,
	};

	if (!authority || authority[0] != 4U)
		return -1;
	memcpy(public, public_header, sizeof(public_header));
	put16(public + 20, 32);
	memcpy(public + 22, authority + 1, 32);
	put16(public + 54, 32);
	memcpy(public + 56, authority + 33, 32);
	put16(name, 0x0b);
	return mbedtls_sha256(public, 88, name + 2, 0);
}

int dave_tpm_authority_policy(const uint8_t authority[65], const uint8_t ref[32],
	uint8_t policy[32])
{
	uint8_t public[88], input[70] = {0}, digest[64];

	if (!ref || !policy || authority_public(authority, public, input + 36))
		return -1;
	put32(input + 32, 0x0000016aU);
	if (mbedtls_sha256(input, sizeof(input), digest, 0))
		return -1;
	memcpy(digest + 32, ref, 32);
	return mbedtls_sha256(digest, sizeof(digest), policy, 0);
}

uint64_t dave_tpm_policy_authorize(struct tpm_reader *reader, const struct tpm_session *session,
	const uint8_t authority[65], const uint8_t approved[32], const uint8_t ref[32],
	const uint8_t signature[64])
{
	uint8_t response[82], command[118 + sizeof(response) - 10];
	uint8_t name[34], approval[64], digest[32];
	uint32_t key = 0;
	size_t size, ticket_size;
	uint64_t status, cleanup;

	if (!reader || !reader->exchange || !session || session->handle >> 24 != 3U ||
	    !authority || authority[0] != 4U || !approved || !ref || !signature)
		return DAVE_TPM_INVALID_PARAMETER;
	if (reader->cleanup_failed)
		return DAVE_TPM_ABORTED;
	memcpy(approval, approved, 32);
	memcpy(approval + 32, ref, 32);
	if (mbedtls_sha256(approval, sizeof(approval), digest, 0))
		return DAVE_TPM_DEVICE_ERROR;

	/* A public key loaded under the owner hierarchy can issue a verified ticket. */
	header(command, TPM_NO_SESSIONS, 106, 0x00000167U);
	put16(command + 10, 0);
	put16(command + 12, 88);
	if (authority_public(authority, command + 14, name))
		return DAVE_TPM_DEVICE_ERROR;
	put32(command + 102, 0x40000001U);
	status = dave_tpm_submit(reader, command, 106, response, sizeof(response),
				 TPM_NO_SESSIONS, &size);
	if (size >= 14 && get32(response + 6) == 0 && (response[10] == 0x80))
		key = get32(response + 10);
	if (dave_tpm_error(status))
		goto out;
	status = DAVE_TPM_PROTOCOL_ERROR;
	if (!key || size != 50 || get16(response + 14) != sizeof(name) ||
	    mbedtls_ct_memcmp(response + 16, name, sizeof(name)))
		goto out;

	header(command, TPM_NO_SESSIONS, 120, 0x00000177U);
	put32(command + 10, key);
	put16(command + 14, 32);
	memcpy(command + 16, digest, 32);
	put16(command + 48, 0x18);
	put16(command + 50, 0x0b);
	put16(command + 52, 32);
	memcpy(command + 54, signature, 32);
	put16(command + 86, 32);
	memcpy(command + 88, signature + 32, 32);
	status = dave_tpm_submit(reader, command, 120, response, sizeof(response),
				 TPM_NO_SESSIONS, &size);
	if (dave_tpm_error(status))
		goto out;
	status = DAVE_TPM_PROTOCOL_ERROR;
	if (size < 18 || get16(response + 10) != 0x8022 ||
	    get32(response + 12) != 0x40000001U)
		goto out;
	ticket_size = get16(response + 16);
	if (!ticket_size || size != 18 + ticket_size)
		goto out;

	header(command, TPM_NO_SESSIONS, 118 + size - 10, 0x0000016aU);
	put32(command + 10, session->handle);
	put16(command + 14, 32);
	memcpy(command + 16, approval, 32);
	put16(command + 48, 32);
	memcpy(command + 50, approval + 32, 32);
	put16(command + 82, sizeof(name));
	memcpy(command + 84, name, sizeof(name));
	memcpy(command + 118, response + 10, size - 10);
	status = dave_tpm_submit(reader, command, 118 + size - 10, response, sizeof(response),
				 TPM_NO_SESSIONS, &size);
	if (!dave_tpm_error(status) && size != 10)
		status = DAVE_TPM_PROTOCOL_ERROR;
out:
	cleanup = dave_tpm_flush_handle(reader, key);
	return dave_tpm_error(cleanup) ? cleanup : status;
}

/* Capability data controls admission as well as provisioning. Authenticate it
 * with a fresh salted audit session; its first response must be exclusive and
 * consume the session. No plain GetCapability response grants authority.
 */
uint64_t dave_tpm_audited_capability(struct tpm_reader *reader, uint32_t capability,
	uint32_t first, uint32_t requested, uint8_t output[73], size_t *output_size)
{
	struct tpm_session session = {0};
	uint8_t command[99] = {0}, response[156], cp[16], rp[81];
	uint8_t caller[32], digest[32], hmac[32];
	size_t size, parameters = 0U, auth;
	uint64_t status = dave_tpm_start_session(reader, &session, false), cleanup;

	*output_size = 0U;
	memset(output, 0, 73U);
	if (dave_tpm_error(status))
		goto out;
	status = dave_tpm_random32(reader, caller);
	if (dave_tpm_error(status))
		goto out;
	put32(cp, TPM_CC_GET_CAPABILITY);
	put32(cp + 4U, capability);
	put32(cp + 8U, first);
	put32(cp + 12U, requested);
	if (mbedtls_sha256(cp, sizeof(cp), digest, 0) != 0 ||
	    dave_tpm_session_hmac(session.key, sizeof(session.key), digest,
		caller, session.nonce, 0x80U, hmac) != 0) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	header(command, TPM_SESSIONS, sizeof(command), TPM_CC_GET_CAPABILITY);
	put32(command + 10U, 73U);
	put32(command + 14U, session.handle);
	put16(command + 18U, 32U);
	memcpy(command + 20U, caller, 32U);
	command[52] = 0x80U; /* audit; do not continue the session */
	put16(command + 53U, 32U);
	memcpy(command + 55U, hmac, 32U);
	memcpy(command + 87U, cp + 4U, 12U);
	status = dave_tpm_submit(reader, command, sizeof(command), response,
		sizeof(response), TPM_SESSIONS, &size);
	if (dave_tpm_error(status))
		goto out;
	status = DAVE_TPM_PROTOCOL_ERROR;
	if (size < 92U)
		goto out;
	parameters = get32(response + 10U);
	if (parameters < 9U || parameters > 73U || size != 14U + parameters + 69U)
		goto out;
	auth = 14U + parameters;
	if (get16(response + auth) != 32U || all_zero(response + auth + 2U, 32U) ||
	    response[auth + 34U] != 0x82U || get16(response + auth + 35U) != 32U)
		goto out;
	put32(rp, 0U);
	put32(rp + 4U, TPM_CC_GET_CAPABILITY);
	memcpy(rp + 8U, response + 14U, parameters);
	if (mbedtls_sha256(rp, parameters + 8U, digest, 0) != 0 ||
	    dave_tpm_session_hmac(session.key, sizeof(session.key), digest,
		response + auth + 2U, caller, 0x82U, hmac) != 0) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	status = DAVE_TPM_SECURITY_VIOLATION;
	if (mbedtls_ct_memcmp(hmac, response + auth + 37U, 32U) != 0)
		goto out;
	/* Authenticated continueSession=0 proves that the TPM released it. */
	mbedtls_platform_zeroize(&session, sizeof(session));
	status = DAVE_TPM_PROTOCOL_ERROR;
	if (response[14] > 1U || get32(response + 15U) != capability)
		goto out;
	status = DAVE_TPM_SUCCESS;
out:
	cleanup = dave_tpm_flush_session(reader, &session);
	if (dave_tpm_error(cleanup))
		status = cleanup;
	if (reader->cleanup_failed)
		status = DAVE_TPM_ABORTED;
	if (!dave_tpm_error(status)) {
		memcpy(output, response + 14U, parameters);
		*output_size = parameters;
	}
	mbedtls_platform_zeroize(&session, sizeof(session));
	mbedtls_platform_zeroize(command, sizeof(command));
	mbedtls_platform_zeroize(response, sizeof(response));
	mbedtls_platform_zeroize(cp, sizeof(cp));
	mbedtls_platform_zeroize(rp, sizeof(rp));
	mbedtls_platform_zeroize(caller, sizeof(caller));
	mbedtls_platform_zeroize(digest, sizeof(digest));
	mbedtls_platform_zeroize(hmac, sizeof(hmac));
	return status;
}

static uint64_t audited_properties(struct tpm_reader *reader, uint32_t first,
	size_t count, uint32_t *values)
{
	uint8_t properties[73];
	size_t size;
	uint64_t status;

	memset(values, 0, count * sizeof(*values));
	status = dave_tpm_audited_capability(reader, 6U, first, (uint32_t)count, properties, &size);
	if (dave_tpm_error(status))
		return status;
	if (size != 9U + count * 8U || get32(properties + 5U) != count)
		return DAVE_TPM_PROTOCOL_ERROR;
	for (size_t i = 0; i < count; i++) {
		if (get32(properties + 9U + i * 8U) != first + i)
			return DAVE_TPM_PROTOCOL_ERROR;
	}
	for (size_t i = 0; i < count; i++)
		values[i] = get32(properties + 13U + i * 8U);
	return DAVE_TPM_SUCCESS;
}

uint64_t dave_tpm_permanent_attributes(struct tpm_reader *reader, uint32_t *attributes)
{
	return audited_properties(reader, 0x00000200U, 1U, attributes);
}

uint64_t dave_tpm_lockout_parameters(struct tpm_reader *reader,
	struct dave_tpm_lockout *parameters)
{
	uint32_t values[3];
	uint64_t status;

	memset(parameters, 0, sizeof(*parameters));
	status = audited_properties(reader, 0x0000020fU, 3U, values);
	if (!dave_tpm_error(status)) {
		parameters->max_tries = values[0];
		parameters->recovery_seconds = values[1];
		parameters->lockout_recovery_seconds = values[2];
	}
	return status;
}

static uint64_t start_session_at(struct tpm_reader *reader,
	struct tpm_session *session, bool policy, uint32_t handle,
	const uint8_t public[92], const uint8_t name[34])
{
	uint8_t expected[34], point_wire[65], entropy[32], cpu[32], salt_input[107];
	uint8_t salt[32], command[160], response[48], caller[32], confirmation[32];
	mbedtls_ecp_group group;
	mbedtls_ecp_point srk, ephemeral;
	mbedtls_mpi private, shared;
	mbedtls_hmac_drbg_context rng;
	size_t size, point_size, pos;
	uint64_t status;

	memset(session, 0, sizeof(*session));
	mbedtls_ecp_group_init(&group);
	mbedtls_ecp_point_init(&srk);
	mbedtls_ecp_point_init(&ephemeral);
	mbedtls_mpi_init(&private);
	mbedtls_mpi_init(&shared);
	mbedtls_hmac_drbg_init(&rng);
	status = DAVE_TPM_SECURITY_VIOLATION;
	if (get16(public) != 90U || get16(public + 2U) != 0x23U ||
	    get16(public + 4U) != 0x0bU ||
	    get32(public + 6U) != 0x00030472U || get16(public + 10U) != 0U ||
	    get16(public + 12U) != 6U || get16(public + 14U) != 128U ||
	    get16(public + 16U) != 0x43U || get16(public + 18U) != 0x10U ||
	    get16(public + 20U) != 3U || get16(public + 22U) != 0x10U ||
	    get16(public + 24U) != 32U || get16(public + 58U) != 32U ||
	    all_zero(public + 26U, 32U) || all_zero(public + 60U, 32U))
		goto out;
	/* CreatePrimary has no shared ReadPublic parser to establish its Name. */
	put16(expected, 0x0bU);
	if (mbedtls_sha256(public + 2U, 90U, expected + 2U, 0) != 0 ||
	    mbedtls_ct_memcmp(expected, name, sizeof(expected)) != 0)
		goto out;
	point_wire[0] = 4U;
	memcpy(point_wire + 1U, public + 26U, 32U);
	memcpy(point_wire + 33U, public + 60U, 32U);
	if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) != 0 ||
	    mbedtls_ecp_point_read_binary(&group, &srk, point_wire, sizeof(point_wire)) != 0 ||
	    mbedtls_ecp_check_pubkey(&group, &srk) != 0)
		goto out;
	status = dave_tpm_random32(reader, entropy);
	if (dave_tpm_error(status))
		goto out;
	if (reader->random(reader->random_context, cpu, sizeof(cpu)) != 0) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	for (size_t i = 0U; i < sizeof(entropy); i++)
		entropy[i] ^= cpu[i];
	if (all_zero(entropy, sizeof(entropy))) {
		status = DAVE_TPM_SECURITY_VIOLATION;
		goto out;
	}
	if (mbedtls_hmac_drbg_seed_buf(&rng, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
		entropy, sizeof(entropy)) != 0 ||
	    mbedtls_ecdh_gen_public(&group, &private, &ephemeral,
		mbedtls_hmac_drbg_random, &rng) != 0 ||
	    mbedtls_ecp_point_write_binary(&group, &ephemeral, MBEDTLS_ECP_PF_UNCOMPRESSED,
		&point_size, point_wire, sizeof(point_wire)) != 0 || point_size != 65U ||
	    mbedtls_ecdh_compute_shared(&group, &shared, &srk, &private,
		mbedtls_hmac_drbg_random, &rng) != 0) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	/* KDFe: counter || Z || "SECRET" NUL || ephemeralX || SRK X. */
	put32(salt_input, 1U);
	if (mbedtls_mpi_write_binary(&shared, salt_input + 4U, 32U) != 0) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	memcpy(salt_input + 36U, "SECRET", 7U);
	memcpy(salt_input + 43U, point_wire + 1U, 32U);
	memcpy(salt_input + 75U, public + 26U, 32U);
	if (mbedtls_sha256(salt_input, sizeof(salt_input), salt, 0) != 0) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	status = dave_tpm_random32(reader, caller);
	if (dave_tpm_error(status))
		goto out;
	put32(command + 10U, handle);
	put32(command + 14U, 0x40000007U);
	put16(command + 18U, 32U);
	memcpy(command + 20U, caller, 32U);
	put16(command + 52U, 68U);
	put16(command + 54U, 32U);
	memcpy(command + 56U, point_wire + 1U, 32U);
	put16(command + 88U, 32U);
	memcpy(command + 90U, point_wire + 33U, 32U);
	pos = 122U;
	command[pos++] = policy ? 1U : 0U;
	put16(command + pos, 6U);
	put16(command + pos + 2U, 128U);
	put16(command + pos + 4U, 0x43U);
	put16(command + pos + 6U, 0x0bU);
	pos += 8U;
	header(command, TPM_NO_SESSIONS, pos, TPM_CC_START_SESSION);
	status = dave_tpm_submit(reader, command, pos, response, sizeof(response), TPM_NO_SESSIONS, &size);
	if (size >= 14U && get32(response + 6U) == 0U &&
	    (get32(response + 10U) >> 24) == (policy ? 3U : 2U))
		session->handle = get32(response + 10U);
	if (dave_tpm_error(status))
		goto out;
	status = DAVE_TPM_PROTOCOL_ERROR;
	if (size != sizeof(response) || session->handle == 0U ||
	    get16(response + 14U) != 32U || all_zero(response + 16U, 32U))
		goto out;
	memcpy(session->nonce, response + 16U, 32U);
	if (dave_tpm_kdfa(salt, sizeof(salt), "ATH", session->nonce, caller, session->key) != 0 ||
	    all_zero(session->key, sizeof(session->key))) {
		status = DAVE_TPM_DEVICE_ERROR;
		goto out;
	}
	/* StartAuthSession has no HMAC: confirm the salt-derived key before reads. */
	status = dave_tpm_authenticated_read(reader, session, NULL, 32U, 0U, confirmation, true);
out:
	if (dave_tpm_error(status)) {
		uint64_t cleanup = dave_tpm_flush_session(reader, session);

		if (dave_tpm_error(cleanup))
			status = cleanup;
	}
	mbedtls_hmac_drbg_free(&rng);
	mbedtls_mpi_free(&shared);
	mbedtls_mpi_free(&private);
	mbedtls_ecp_point_free(&ephemeral);
	mbedtls_ecp_point_free(&srk);
	mbedtls_ecp_group_free(&group);
	mbedtls_platform_zeroize(entropy, sizeof(entropy));
	mbedtls_platform_zeroize(cpu, sizeof(cpu));
	mbedtls_platform_zeroize(salt_input, sizeof(salt_input));
	mbedtls_platform_zeroize(salt, sizeof(salt));
	mbedtls_platform_zeroize(command, sizeof(command));
	mbedtls_platform_zeroize(response, sizeof(response));
	mbedtls_platform_zeroize(caller, sizeof(caller));
	mbedtls_platform_zeroize(confirmation, sizeof(confirmation));
	return status;
}

uint64_t dave_tpm_start_session(struct tpm_reader *reader,
	struct tpm_session *session, bool policy)
{
	uint8_t public[92], name[34];
	size_t size;
	uint64_t status;

	memset(session, 0, sizeof(*session));
	status = dave_tpm_public_area(reader, TPM_SRK, false, public, sizeof(public), &size, name);
	if (!dave_tpm_error(status) &&
	    (size != sizeof(public) || mbedtls_ct_memcmp(name, reader->pin, sizeof(name))))
		status = DAVE_TPM_SECURITY_VIOLATION;
	if (!dave_tpm_error(status))
		status = start_session_at(reader, session, policy, TPM_SRK, public, name);
	return status;
}

uint64_t dave_tpm_start_identity_session(struct tpm_reader *reader,
	struct tpm_session *session, uint32_t handle, const uint8_t public[92],
	const uint8_t name[34])
{
	if (handle != TPM_SRK && (handle >> 24) != 0x80U)
		return DAVE_TPM_INVALID_PARAMETER;
	return start_session_at(reader, session, false, handle, public, name);
}
