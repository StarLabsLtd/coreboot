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

static void valid_requests(void)
{
	struct payload_mm_authvar_service_frame *frame;

	assert(payload_mm_authvar_service_endpoint_validate(&endpoint) == CB_SUCCESS);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(frame);
	frame->data_capacity = 32;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);

	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_NEXT);
	frame->name_capacity = NAME_SIZE;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);
	set_name(frame);
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_SUCCESS);

	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_SET);
	set_name(frame);
	frame->attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	frame->data_size = 4;
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

	changed = endpoint;
	changed.flags ^= LB_AUTHVAR_ENDPOINT_NO_RAW_SMMSTORE;
	assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_ERR);
	changed = endpoint;
	changed.message_size--;
	assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_ERR);
	changed = endpoint;
	changed.communication_base++;
	assert(payload_mm_authvar_service_endpoint_validate(&changed) == CB_ERR);

	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_GET);
	set_name(frame);
	request_buffer[sizeof(*frame) + frame->name_size - 1] = 1;
	assert(payload_mm_authvar_service_request_validate(&endpoint,
		request_buffer, sizeof(request_buffer)) == CB_ERR);
	frame = new_request(PAYLOAD_MM_AUTHVAR_SERVICE_SET);
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
	memcpy(response_buffer, request_buffer, sizeof(response_buffer));
	response = (void *)response_buffer;
	response->status = 0;
	response->result_data_size = 4;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
	response->completion = PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_SUCCESS);
	response->request_id++;
	assert(payload_mm_authvar_service_response_validate(&endpoint,
		request_buffer, response_buffer, sizeof(response_buffer)) == CB_ERR);
}

static void deterministic_fuzz(void)
{
	unsigned int seed = 1;

	for (size_t iteration = 0; iteration < 10000; iteration++) {
		struct payload_mm_authvar_service_frame *frame = (void *)request_buffer;

		for (size_t i = 0; i < sizeof(*frame); i++) {
			seed = seed * 1103515245U + 12345U;
			request_buffer[i] = (uint8_t)(seed >> 16);
		}
		(void)payload_mm_authvar_service_request_validate(&endpoint,
			request_buffer, sizeof(request_buffer));
	}
}

int main(void)
{
	valid_requests();
	hostile_requests();
	response_order_and_echo();
	deterministic_fuzz();
	return 0;
}
