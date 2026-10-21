/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_SERVICE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_SERVICE_H

#include <commonlib/authvar_service.h>
#include <commonlib/bsd/cb_err.h>

enum cb_err payload_mm_authvar_service_endpoint_validate(
	const struct lb_authvar_service_endpoint *endpoint);
enum cb_err payload_mm_authvar_service_request_validate(
	const struct lb_authvar_service_endpoint *endpoint, const void *message,
	size_t message_size);
enum cb_err payload_mm_authvar_service_response_validate(
	const struct lb_authvar_service_endpoint *endpoint, const void *request,
	const void *response, size_t message_size);

#endif
