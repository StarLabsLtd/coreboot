/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/platform_auth.h>
#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <string.h>

#if ENV_SMM && !ENV_TEST
#error "The TPM2 platform authorization codecs must not be built in SMM"
#endif

#define TPM2_CC_HIERARCHY_CONTROL 0x00000121U
#define TPM2_PT_STARTUP_CLEAR 0x00000201U
#define TPM2_STARTUP_CLEAR_PH_ENABLE (1U << 0)
#define TPM2_STARTUP_CLEAR_PH_ENABLE_NV (1U << 3)
#define TPM2_PLATFORM_RESPONSE_MAX_SIZE 27U

struct buffer {
	uint8_t *data;
	size_t capacity;
	size_t size;
};

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_start = (uintptr_t)left;
	uintptr_t right_start = (uintptr_t)right;

	if (!left || !right || !left_size || !right_size ||
	    left_start > UINTPTR_MAX - left_size ||
	    right_start > UINTPTR_MAX - right_size)
		return true;
	return left_start < right_start + right_size &&
		right_start < left_start + left_size;
}

static bool append(struct buffer *buffer, const void *data, size_t size)
{
	if (!data || size > buffer->capacity - buffer->size)
		return false;
	memcpy(buffer->data + buffer->size, data, size);
	buffer->size += size;
	return true;
}

static bool append_be16(struct buffer *buffer, uint16_t value)
{
	const uint8_t bytes[] = {
		(uint8_t)(value >> 8), (uint8_t)value,
	};

	return append(buffer, bytes, sizeof(bytes));
}

static bool append_be32(struct buffer *buffer, uint32_t value)
{
	const uint8_t bytes[] = {
		(uint8_t)(value >> 24), (uint8_t)(value >> 16),
		(uint8_t)(value >> 8), (uint8_t)value,
	};

	return append(buffer, bytes, sizeof(bytes));
}

static uint16_t read_be16(const uint8_t *data)
{
	return (uint16_t)data[0] << 8 | data[1];
}

static uint32_t read_be32(const uint8_t *data)
{
	return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
		(uint32_t)data[2] << 8 | data[3];
}

static bool append_header(struct buffer *request, uint16_t tag,
	uint32_t code)
{
	return append_be16(request, tag) && append_be32(request, 0) &&
		append_be32(request, code);
}

static bool append_password_authorization(struct buffer *request)
{
	return append_be32(request, 9) && append_be32(request, TPM_RS_PW) &&
		append_be16(request, 0) &&
		append(request, (const uint8_t[]) { 0 }, 1) &&
		append_be16(request, 0);
}

static bool finish_request(struct buffer *request)
{
	if (request->size > UINT32_MAX || request->size < 10)
		return false;
	request->data[2] = (uint8_t)(request->size >> 24);
	request->data[3] = (uint8_t)(request->size >> 16);
	request->data[4] = (uint8_t)(request->size >> 8);
	request->data[5] = (uint8_t)request->size;
	return true;
}

static bool arguments_valid(struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token,
	const struct tpm2_platform_auth_result *result)
{
	return lifecycle && token && result &&
		!ranges_overlap(result, sizeof(*result), lifecycle,
			sizeof(*lifecycle)) &&
		!ranges_overlap(result, sizeof(*result), token, sizeof(*token));
}

static enum cb_err decode_response(const uint8_t *response,
	size_t response_size, bool sessions,
	struct tpm2_platform_auth_result *result)
{
	uint16_t tag;
	uint32_t size;
	uint32_t code;

	if (response_size < 10)
		return CB_ERR;
	tag = read_be16(response);
	size = read_be32(response + 2);
	code = read_be32(response + 6);
	if (size != response_size)
		return CB_ERR;
	if (code) {
		if (tag != TPM_ST_NO_SESSIONS || response_size != 10)
			return CB_ERR;
		result->response_code = code;
		result->delivered = 1;
		return CB_ERR;
	}
	if (sessions) {
		static const uint8_t empty_session_response[] = {
			0x80, 0x02, 0, 0, 0, 0x13, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0,
		};

		if (response_size != sizeof(empty_session_response) ||
		    memcmp(response, empty_session_response,
			sizeof(empty_session_response)))
			return CB_ERR;
	} else if (tag != TPM_ST_NO_SESSIONS) {
		return CB_ERR;
	}
	result->delivered = 1;
	return CB_SUCCESS;
}

static enum cb_err transmit(struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token, const uint8_t *request,
	size_t request_size, bool sessions, uint8_t *response,
	size_t *response_size, struct tpm2_platform_auth_result *result)
{
	struct tpm2_platform_auth_result decoded = { 0 };

	if (tpm_pre_os_lifecycle_transmit(lifecycle, token, request, request_size,
		response, response_size) != CB_SUCCESS)
		return CB_ERR;
	if (decode_response(response, *response_size, sessions, &decoded) !=
		CB_SUCCESS) {
		if (decoded.delivered)
			*result = decoded;
		else
			tpm_pre_os_lifecycle_fail(lifecycle, token);
		return CB_ERR;
	}
	*result = decoded;
	return CB_SUCCESS;
}

static enum cb_err nv_command(struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token, uint32_t nv_index,
	const uint8_t value[TPM2_PLATFORM_AUTH_NV_VALUE_SIZE], bool write,
	struct tpm2_platform_auth_result *result)
{
	uint8_t request_bytes[75];
	uint8_t response[TPM2_PLATFORM_RESPONSE_MAX_SIZE];
	uint8_t snapshot[TPM2_PLATFORM_AUTH_NV_VALUE_SIZE];
	struct buffer request = {
		.data = request_bytes,
		.capacity = sizeof(request_bytes),
	};
	size_t response_size = sizeof(response);
	enum cb_err status;

	if (!arguments_valid(lifecycle, token, result))
		return CB_ERR_ARG;
	if (write && value &&
	    (ranges_overlap(result, sizeof(*result), value,
		TPM2_PLATFORM_AUTH_NV_VALUE_SIZE) ||
	     ranges_overlap(lifecycle, sizeof(*lifecycle), value,
		TPM2_PLATFORM_AUTH_NV_VALUE_SIZE) ||
	     ranges_overlap(token, sizeof(*token), value,
		TPM2_PLATFORM_AUTH_NV_VALUE_SIZE)))
		return CB_ERR_ARG;
	memset(result, 0, sizeof(*result));
	if ((nv_index & 0xff000000U) != HR_NV_INDEX ||
	    (write && !value))
		return CB_ERR_ARG;
	memset(snapshot, 0, sizeof(snapshot));
	if (write)
		memcpy(snapshot, value, sizeof(snapshot));
	if (!append_header(&request, TPM_ST_SESSIONS,
		write ? TPM2_NV_Write : TPM2_NV_WriteLock) ||
	    !append_be32(&request, TPM_RH_PLATFORM) ||
	    !append_be32(&request, nv_index) ||
	    !append_password_authorization(&request) ||
	    (write && (!append_be16(&request, sizeof(snapshot)) ||
		!append(&request, snapshot, sizeof(snapshot)) ||
		!append_be16(&request, 0))) ||
	    !finish_request(&request) || request.size != (write ? 75 : 31))
		return CB_ERR;
	status = transmit(lifecycle, token, request.data, request.size, true,
		response, &response_size, result);
	if (write && memcmp(value, snapshot, sizeof(snapshot))) {
		memset(result, 0, sizeof(*result));
		tpm_pre_os_lifecycle_fail(lifecycle, token);
		return CB_ERR;
	}
	return status;
}

enum cb_err tpm2_platform_auth_nv_write(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token, uint32_t nv_index,
	const uint8_t value[TPM2_PLATFORM_AUTH_NV_VALUE_SIZE],
	struct tpm2_platform_auth_result *result)
{
	return nv_command(lifecycle, token, nv_index, value, true, result);
}

enum cb_err tpm2_platform_auth_nv_write_lock(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token, uint32_t nv_index,
	struct tpm2_platform_auth_result *result)
{
	return nv_command(lifecycle, token, nv_index, NULL, false, result);
}

enum cb_err tpm2_platform_auth_close_ph_enable(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token,
	struct tpm2_platform_auth_result *result)
{
	uint8_t request_bytes[32];
	uint8_t response[TPM2_PLATFORM_RESPONSE_MAX_SIZE];
	struct buffer request = {
		.data = request_bytes,
		.capacity = sizeof(request_bytes),
	};
	size_t response_size = sizeof(response);

	if (!arguments_valid(lifecycle, token, result))
		return CB_ERR_ARG;
	memset(result, 0, sizeof(*result));
	if (!append_header(&request, TPM_ST_SESSIONS,
		TPM2_CC_HIERARCHY_CONTROL) ||
	    !append_be32(&request, TPM_RH_PLATFORM) ||
	    !append_password_authorization(&request) ||
	    !append_be32(&request, TPM_RH_PLATFORM) ||
	    !append(&request, (const uint8_t[]) { 0 }, 1) ||
	    !finish_request(&request) || request.size != sizeof(request_bytes))
		return CB_ERR;
	return transmit(lifecycle, token, request.data, request.size, true,
		response, &response_size, result);
}

enum cb_err tpm2_platform_auth_verify_startup_clear(
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct tpm_pre_os_token *token,
	struct tpm2_platform_auth_result *result)
{
	uint8_t request_bytes[22];
	uint8_t response[TPM2_PLATFORM_RESPONSE_MAX_SIZE];
	struct buffer request = {
		.data = request_bytes,
		.capacity = sizeof(request_bytes),
	};
	size_t response_size = sizeof(response);
	uint32_t startup_clear;
	enum cb_err status;

	if (!arguments_valid(lifecycle, token, result))
		return CB_ERR_ARG;
	memset(result, 0, sizeof(*result));
	if (!append_header(&request, TPM_ST_NO_SESSIONS,
		TPM2_GetCapability) ||
	    !append_be32(&request, TPM_CAP_TPM_PROPERTIES) ||
	    !append_be32(&request, TPM2_PT_STARTUP_CLEAR) ||
	    !append_be32(&request, 1) || !finish_request(&request) ||
	    request.size != sizeof(request_bytes))
		return CB_ERR;
	status = transmit(lifecycle, token, request.data, request.size, false,
		response, &response_size, result);
	if (status != CB_SUCCESS)
		return status;
	if (response_size != sizeof(response) || response[10] > 1 ||
	    read_be32(response + 11) != TPM_CAP_TPM_PROPERTIES ||
	    read_be32(response + 15) != 1 ||
	    read_be32(response + 19) != TPM2_PT_STARTUP_CLEAR) {
		memset(result, 0, sizeof(*result));
		tpm_pre_os_lifecycle_fail(lifecycle, token);
		return CB_ERR;
	}
	startup_clear = read_be32(response + 23);
	if ((startup_clear & TPM2_STARTUP_CLEAR_PH_ENABLE) ||
	    !(startup_clear & TPM2_STARTUP_CLEAR_PH_ENABLE_NV)) {
		memset(result, 0, sizeof(*result));
		tpm_pre_os_lifecycle_fail(lifecycle, token);
		return CB_ERR;
	}
	result->startup_clear = startup_clear;
	result->startup_clear_valid = 1;
	return CB_SUCCESS;
}
