/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_service.h>
extern long write(int fd, const void *buffer, unsigned long count);

int main(void)
{
	const uint64_t values[] = {
		sizeof(struct lb_authvar_service_endpoint),
		offsetof(struct lb_authvar_service_endpoint, generation),
		offsetof(struct lb_authvar_service_endpoint, communication_base),
		offsetof(struct lb_authvar_service_endpoint, transport),
		offsetof(struct lb_authvar_service_endpoint, reserved),
		sizeof(struct payload_mm_authvar_service_frame),
		offsetof(struct payload_mm_authvar_service_frame, generation),
		offsetof(struct payload_mm_authvar_service_frame, vendor_guid),
		offsetof(struct payload_mm_authvar_service_frame, maximum_storage),
		offsetof(struct payload_mm_authvar_service_frame, status),
		offsetof(struct payload_mm_authvar_service_frame, result_vendor_guid),
		offsetof(struct payload_mm_authvar_service_frame, completion),
		LB_TAG_AUTHVAR_SERVICE_ENDPOINT, LB_AUTHVAR_ENDPOINT_REQUIRED_FLAGS,
		PAYLOAD_MM_AUTHVAR_SERVICE_REVISION,
		PAYLOAD_MM_AUTHVAR_SERVICE_HEADER_SIZE,
		PAYLOAD_MM_AUTHVAR_SERVICE_GET, PAYLOAD_MM_AUTHVAR_SERVICE_NEXT,
		PAYLOAD_MM_AUTHVAR_SERVICE_SET, PAYLOAD_MM_AUTHVAR_SERVICE_QUERY,
		PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT,
		PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
		PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED, PAYLOAD_MM_AUTHVAR_SERVICE_PENDING,
		PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT,
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER,
		PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED,
		PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL,
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR,
		PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED,
		PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES,
		PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND,
		PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED,
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION,
	};

	return write(1, values, sizeof(values)) == sizeof(values) ? 0 : 1;
}
