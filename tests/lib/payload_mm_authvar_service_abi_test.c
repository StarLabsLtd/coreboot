/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_service.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define MESSAGE_SIZE 4096U
#define NAME_SIZE 256U
#define DATA_SIZE (MESSAGE_SIZE - PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE - NAME_SIZE)

_Static_assert(PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT == (1ULL << 63),
	"authenticated-variable status error bit");
_Static_assert(PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS == 0ULL &&
	PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER == ((1ULL << 63) | 2ULL) &&
	PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED == ((1ULL << 63) | 3ULL) &&
	PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL == ((1ULL << 63) | 5ULL) &&
	PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR == ((1ULL << 63) | 7ULL) &&
	PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED == ((1ULL << 63) | 8ULL) &&
	PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES == ((1ULL << 63) | 9ULL) &&
	PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND == ((1ULL << 63) | 14ULL) &&
	PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION == ((1ULL << 63) | 26ULL),
	"authenticated-variable status values");

static uint8_t request_buffer[MESSAGE_SIZE] __aligned(8);
static uint8_t response_buffer[MESSAGE_SIZE] __aligned(8);

static struct lb_authvar_service_endpoint endpoint = {
	.tag = LB_TAG_AUTHVAR_SERVICE_ENDPOINT,
	.size = sizeof(endpoint),
	.revision = LB_AUTHVAR_SERVICE_ENDPOINT_REVISION,
	.header_size = sizeof(endpoint),
	.flags = LB_AUTHVAR_ENDPOINT_REQUIRED_FLAGS,
	.generation = 7,
	.communication_base = 0x100000,
	.communication_size = MESSAGE_SIZE,
	.message_size = MESSAGE_SIZE,
	.transport = LB_AUTHVAR_ENDPOINT_TRANSPORT_APM_IO8,
	.trigger_width = 1,
	.trigger_address = 0xb2,
	.trigger_value = 0xe7,
	.maximum_name_size = NAME_SIZE,
	.maximum_data_size = DATA_SIZE,
};

static struct payload_mm_authvar_service_frame *new_request(uint32_t operation)
{
	struct payload_mm_authvar_service_frame *frame = (void *)request_buffer;

	memset(request_buffer, 0, sizeof(request_buffer));
	frame->revision = PAYLOAD_MM_AUTHVAR_SERVICE_REVISION;
	frame->header_size = sizeof(*frame);
	frame->operation = operation;
	frame->generation = endpoint.generation;
	frame->request_id = 9;
	frame->status = PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING;
	frame->completion = PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
	return frame;
}

static void set_name(struct payload_mm_authvar_service_frame *frame)
{
	uint16_t *name = (void *)(request_buffer + sizeof(*frame));

	name[0] = 'A';
	name[1] = 0;
	frame->name_size = 2U * sizeof(*name);
	frame->vendor_guid[0] = 1;
}

static void set_response_name(struct payload_mm_authvar_service_frame *frame,
	uint16_t character)
{
	uint16_t *name = (void *)(response_buffer + sizeof(*frame));

	name[0] = character;
	name[1] = 0;
	frame->result_name_size = 2U * sizeof(*name);
	frame->result_vendor_guid[0] = 2;
}

static struct payload_mm_authvar_service_frame *new_response(
	const struct payload_mm_authvar_service_frame *request, uint64_t status)
{
	struct payload_mm_authvar_service_frame *response = (void *)response_buffer;

	memset(response_buffer, 0, sizeof(response_buffer));
	memcpy(response, request, sizeof(*response));
	response->status = status;
	response->completion = PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE;
	return response;
}

static uint8_t *response_data(void)
{
	return response_buffer + PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE + NAME_SIZE;
}

static uint32_t stored_attributes(void)
{
	return PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
}

static void valid_requests(void)
{
	struct payload_mm_authvar_service_frame *frame;

	assert(payload_mm_authvar_service_endpoint_validate(&endpoint) == CB_SUCCESS);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(frame);
	frame->data_capacity = 32;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	memset(frame->vendor_guid, 0, sizeof(frame->vendor_guid));
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);

	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT);
	frame->name_capacity = NAME_SIZE;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	set_name(frame);
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	memset(frame->vendor_guid, 0, sizeof(frame->vendor_guid));
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT);
	frame->name_capacity = NAME_SIZE;
	frame->vendor_guid[0] = 1;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);

	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_SET);
	set_name(frame);
	frame->attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	frame->data_size = 4;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	memset(frame->vendor_guid, 0, sizeof(frame->vendor_guid));
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	frame->attributes = 0;
	frame->data_size = 0;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);

	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_QUERY);
	frame->attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT);
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME);
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
}

static void hostile_requests(void)
{
	struct payload_mm_authvar_service_frame *frame;
	struct lb_authvar_service_endpoint changed;
	uint16_t *name;

	changed = endpoint;
	changed.flags ^= LB_AUTHVAR_ENDPOINT_NO_RAW_SMMSTORE;
	assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_ERR);
	changed = endpoint;
	changed.message_size--;
	assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_ERR);
	changed = endpoint;
	changed.communication_base++;
	assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_ERR);
	changed = endpoint;
	changed.maximum_name_size = 0xffffff80U;
	assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_ERR);
	changed = endpoint;
	changed.communication_size = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE;
	changed.message_size = changed.communication_size;
	changed.maximum_name_size = 512;
	changed.maximum_data_size = changed.message_size -
		PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE - changed.maximum_name_size;
	assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_SUCCESS);
	changed.communication_size++;
	changed.message_size++;
	changed.maximum_data_size++;
	assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_ERR);

	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(frame);
	request_buffer[sizeof(*frame) + frame->name_size - 1] = 1;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	frame->vendor_guid[0] = 1;
	frame->name_size = sizeof(uint16_t);
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(frame);
	frame->name_size--;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(frame);
	name = (void *)(request_buffer + sizeof(*frame));
	name[1] = 'B';
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(frame);
	name = (void *)(request_buffer + sizeof(*frame));
	name[1] = 0;
	name[2] = 0x03a9;
	name[3] = 0;
	frame->name_size = 4U * sizeof(*name);
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(frame);
	name = (void *)(request_buffer + sizeof(*frame));
	name[0] = 0x03a9;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT);
	frame->name_capacity = NAME_SIZE;
	set_name(frame);
	frame->name_capacity = frame->name_size - (uint32_t)sizeof(uint16_t);
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_SET);
	set_name(frame);
	frame->attributes = PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
#else
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
#endif
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(frame);
	frame->attributes = PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT);
	frame->name_size = 2;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(0x80000000U);
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_QUERY);
	frame->attributes = PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	frame->request_id = 0;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
}

static void response_order_and_echo(void)
{
	struct payload_mm_authvar_service_frame *request;
	struct payload_mm_authvar_service_frame *response;

	request = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(request);
	request->data_capacity = 32;
	response = new_response(request, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response->result_data_size = 4;
	response->result_attributes = stored_attributes();
	memset(response_data(), 0x5a, response->result_data_size);
	response->completion = PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response->completion = PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_SUCCESS);
	response->request_id++;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);

	request = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT);
	set_name(request);
	request->name_capacity = NAME_SIZE;
	response = new_response(request, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	set_response_name(response, 'B');
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_SUCCESS);
	memset(response->result_vendor_guid, 0, sizeof(response->result_vendor_guid));
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_SUCCESS);
	response->vendor_guid[0] = 2;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
}

static void deterministic_request_mutations(void)
{
	for (size_t iteration = 0; iteration < 10000; iteration++) {
		struct payload_mm_authvar_service_frame *frame =
			new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);

		set_name(frame);
		frame->data_capacity = 32;
		switch (iteration % 17U) {
		case 0:
			frame->revision++;
			break;
		case 1:
			frame->header_size--;
			break;
		case 2:
			frame->operation = 0;
			break;
		case 3:
			frame->flags = 1;
			break;
		case 4:
			frame->generation++;
			break;
		case 5:
			frame->request_id = 0;
			break;
		case 6:
			request_buffer[sizeof(*frame)] = 0;
			break;
		case 7:
			frame->attributes = UINT32_MAX;
			break;
		case 8:
			frame->name_size = endpoint.maximum_name_size + 2U;
			break;
		case 9:
			frame->data_size = 1;
			break;
		case 10:
			frame->name_capacity = 1;
			break;
		case 11:
			frame->data_capacity = endpoint.maximum_data_size + 1U;
			break;
		case 12:
			frame->reserved0 = 1;
			break;
		case 13:
			frame->status = 0;
			break;
		case 14:
			frame->result_vendor_guid[0] = 1;
			break;
		case 15:
			frame->reserved[0] = 1;
			break;
		default:
			frame->completion = PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE;
			break;
		}
		assert(payload_mm_authvar_service_request_validate(&endpoint,
			request_buffer, sizeof(request_buffer)) == CB_ERR);
	}
}

static struct payload_mm_authvar_service_frame *matrix_response(
	uint32_t operation, uint64_t status)
{
	struct payload_mm_authvar_service_frame *request = new_request(operation);
	struct payload_mm_authvar_service_frame *response;

	switch (operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
		set_name(request);
		request->data_capacity = 32;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
		request->name_capacity = 32;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_SET:
		set_name(request);
		request->attributes = stored_attributes();
		request->data_size = 4;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		request->attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT:
	case PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME:
		break;
	default:
		abort();
	}
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	response = new_response(request, status);
	if (operation == PAYLOAD_MM_AUTHVAR_SERVICE_GET &&
	    (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
	     status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)) {
		response->result_data_size = status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ?
			4U : request->data_capacity + 1U;
		response->result_attributes = stored_attributes();
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
			memset(response_data(), 0x5a, response->result_data_size);
	} else if (operation == PAYLOAD_MM_AUTHVAR_SERVICE_NEXT &&
		   status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		set_response_name(response, 'B');
	} else if (operation == PAYLOAD_MM_AUTHVAR_SERVICE_NEXT &&
		   status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL) {
		response->result_name_size = request->name_capacity + 2U;
	} else if (operation == PAYLOAD_MM_AUTHVAR_SERVICE_QUERY &&
		   status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		response->maximum_storage = 1024;
		response->remaining_storage = 512;
		response->maximum_variable = 256;
	}
	return response;
}

static bool matrix_status_allowed(uint32_t operation, uint64_t status)
{
	switch (operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
		return status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT:
		return status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	case PAYLOAD_MM_AUTHVAR_SERVICE_SET:
		return status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		return status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	case PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT:
	case PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME:
		return status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	default:
		return false;
	}
}

static void response_status_matrix(void)
{
	static const uint32_t operations[] = {
		PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
		PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
		PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT,
		PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
	};
	static const uint64_t statuses[] = {
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER,
		PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED,
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL,
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR,
		PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED,
		PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES,
		PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND,
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION,
	};

	for (size_t operation = 0; operation < ARRAY_SIZE(operations); operation++) {
		for (size_t status = 0; status < ARRAY_SIZE(statuses); status++) {
			matrix_response(operations[operation], statuses[status]);
			assert((payload_mm_authvar_service_response_validate(&endpoint,
				request_buffer, response_buffer, sizeof(response_buffer)) ==
				CB_SUCCESS) == matrix_status_allowed(operations[operation],
					statuses[status]));
		}
		matrix_response(operations[operation],
			PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING);
		assert(payload_mm_authvar_service_response_validate(&endpoint,
			request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
		matrix_response(operations[operation], (1ULL << 63) | 0x1234ULL);
		assert(payload_mm_authvar_service_response_validate(&endpoint,
			request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	}
}

static void hostile_response_results(void)
{
	struct payload_mm_authvar_service_frame *response;

	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response->result_data_size = 0;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response->result_data_size = 33;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	response->result_data_size = 32;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response->result_attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	response_data()[0] = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response_buffer[sizeof(*response)] = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response_data()[response->result_data_size] = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);

	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response->result_name_size = 34;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	response->result_name_size = 33;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	response->result_name_size = 32;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL);
	response_buffer[sizeof(*response)] = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response_buffer[sizeof(*response) + response->result_name_size] = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response_data()[0] = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);

	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response->remaining_storage = response->maximum_storage + 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response->maximum_variable = response->remaining_storage + 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response->maximum_storage = 0;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
		PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	response->maximum_storage = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response->result_data_size = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response_buffer[sizeof(*response)] = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response = matrix_response(PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	response_data()[0] = 1;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
}

static void deterministic_endpoint_mutations(void)
{
	for (size_t iteration = 0; iteration < 10000; iteration++) {
		struct lb_authvar_service_endpoint changed = endpoint;

		switch (iteration % 18U) {
		case 0:
			changed.tag++;
			break;
		case 1:
			changed.size--;
			break;
		case 2:
			changed.revision++;
			break;
		case 3:
			changed.header_size--;
			break;
		case 4:
			changed.flags ^= LB_AUTHVAR_ENDPOINT_DMA_PROTECTED;
			break;
		case 5:
			changed.generation = 0;
			break;
		case 6:
			changed.communication_base = 0;
			break;
		case 7:
			changed.communication_base++;
			break;
		case 8:
			changed.communication_size =
				PAYLOAD_MM_AUTHVAR_SERVICE_MIN_MESSAGE_SIZE - 1U;
			break;
		case 9:
			changed.communication_size =
				PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE + 1U;
			break;
		case 10:
			changed.message_size--;
			break;
		case 11:
			changed.transport++;
			break;
		case 12:
			changed.trigger_width++;
			break;
		case 13:
			changed.trigger_address = 0;
			break;
		case 14:
			changed.trigger_value = 0;
			break;
		case 15:
			changed.maximum_name_size = 0xffffff80U;
			break;
		case 16:
			changed.maximum_data_size = 0;
			break;
		default:
			changed.reserved = 1;
			break;
		}
		assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_ERR);
	}
}

static void deterministic_response_mutations(void)
{
	for (size_t iteration = 0; iteration < 10000; iteration++) {
		struct payload_mm_authvar_service_frame *request =
			new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
		struct payload_mm_authvar_service_frame *response;

		set_name(request);
		request->data_capacity = 32;
		response = new_response(request, PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		response->result_attributes = stored_attributes();
		switch (iteration % 12U) {
		case 0:
			response->generation++;
			break;
		case 1:
			response->request_id++;
			break;
		case 2:
			response->reserved0 = 1;
			break;
		case 3:
			response->status = PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING;
			break;
		case 4:
			response->result_name_size = endpoint.maximum_name_size + 2U;
			break;
		case 5:
			response->result_data_size = endpoint.maximum_data_size + 1U;
			break;
		case 6:
			response->result_attributes = UINT32_MAX;
			break;
		case 7:
			response->result_vendor_guid[0] = 1;
			break;
		case 8:
			response->reserved[0] = 1;
			break;
		case 9:
			response->completion = PAYLOAD_MM_AUTHVAR_SERVICE_PENDING;
			break;
		case 10:
			response->vendor_guid[0]++;
			break;
		default:
			response->data_capacity++;
			break;
		}
		assert(payload_mm_authvar_service_response_validate(&endpoint,
			request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	}
}

int main(void)
{
	valid_requests();
	hostile_requests();
	response_order_and_echo();
	response_status_matrix();
	hostile_response_results();
	deterministic_endpoint_mutations();
	deterministic_request_mutations();
	deterministic_response_mutations();
	return 0;
}
