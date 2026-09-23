/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_POLICY_TEST_PROVIDER_H
#define PAYLOAD_MM_AUTHVAR_POLICY_TEST_PROVIDER_H

#include <boot/payload_mm_authvar_policy.h>
#include <boot/payload_mm_authvar_writer.h>

/* Deliberately unauthenticated fixture provider, never linked into firmware. */
static uint64_t test_policy_authorize(
	const struct payload_mm_authvar_policy_request *request,
	const struct payload_mm_authvar_policy_view *view,
	struct payload_mm_authvar_policy_mutation *mutation,
	void *data, size_t capacity)
{
	(void)view;
	if (request->data_size > capacity)
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	mutation->kind = !request->data_size &&
		!(request->attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) ?
		PAYLOAD_MM_AUTHVAR_MUTATION_DELETE : PAYLOAD_MM_AUTHVAR_MUTATION_WRITE;
	if (mutation->kind == PAYLOAD_MM_AUTHVAR_MUTATION_WRITE) {
		mutation->attributes = request->attributes;
		mutation->data_size = (uint32_t)request->data_size;
		if (request->data_size)
			memcpy(data, request->data, request->data_size);
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static enum cb_err test_policy_install(void)
{
	const struct payload_mm_authvar_policy_provider provider = {
		.revision = PAYLOAD_MM_AUTHVAR_POLICY_REVISION,
		.size = sizeof(provider),
		.authorize = test_policy_authorize,
	};

	return payload_mm_authvar_policy_install(&provider);
}

static uint64_t test_policy_apply(const struct payload_mm_authvar_record_source *source)
{
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = source->attributes,
		.name = source->name,
		.name_size = source->name_size,
		.data = source->data,
		.data_size = source->data_size,
	};
	struct payload_mm_authvar_policy_result result;

	memcpy(request.vendor_guid, source->vendor_guid, sizeof(request.vendor_guid));
	return payload_mm_authvar_policy_transaction(&request, &result);
}

#endif
