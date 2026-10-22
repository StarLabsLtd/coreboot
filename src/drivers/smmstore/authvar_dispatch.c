/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */
#include "authvar_dispatch.h"
#include <boot/authvar_image_policy.h>
#include <boot/payload_mm_authvar_format.h>
#include <string.h>
#if CONFIG(DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT)
#include "capsule.h"
#endif

static bool state_predicate(const struct authvar_policy *policy,
	const void *guid, const void *name, size_t name_size)
{
	for (size_t i = 0; i < policy->count; i++) {
		const struct authvar_policy_entry *entry = (const void *)policy->entries[i];
		const uint8_t *state = (const uint8_t *)(entry + 1);

		if (entry->lock_policy_type == 3 && entry->offset_to_name == 62U + name_size &&
		    !memcmp(state, guid, 16) && !memcmp(state + 18, name, name_size))
			return true;
	}
	return false;
}

static uint64_t image_snapshot(struct authvar_owner *owner, void *output,
	size_t capacity, uint32_t *size)
{
	static const uint8_t global_guid[16] = {
		0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
		0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
	};
	static const uint8_t image_guid[16] = {
		0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
		0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
	};
	static const uint16_t names[4][4] = {
		{ 'P', 'K', 0 }, { 'd', 'b', 0 },
		{ 'd', 'b', 'x', 0 }, { 'd', 'b', 't', 0 },
	};
	struct payload_mm_authvar_view view;
	struct payload_mm_authvar_view_value values[4] = { 0 };
	uint32_t header[24] = { 0 };
	size_t total = sizeof(header);
	uint64_t status;

	if (owner->runtime)
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	status = authvar_owner_view(owner, &view);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return status;
	for (size_t i = 0; i < 4; i++) {
		status = payload_mm_authvar_view_get(&view, i ? image_guid : global_guid,
			names[i], i < 2 ? 6 : 8, UINT32_MAX, &values[i]);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND) {
			memset(&values[i], 0, sizeof(values[i]));
			continue;
		}
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		    values[i].data_size > AUTHVAR_IMAGE_POLICY_SNAPSHOT_MAX_VARIABLE_SIZE)
			return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		header[8 + 4 * i] = 1;
		header[9 + 4 * i] = values[i].attributes;
		header[10 + 4 * i] = values[i].data_size;
		total += (values[i].data_size + 7U) & ~(size_t)7U;
	}
	*size = total;
	if (total > capacity)
		return PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL;
	header[0] = AUTHVAR_IMAGE_POLICY_SNAPSHOT_REVISION;
	header[1] = sizeof(header);
	header[2] = total;
	header[3] = 4;
	header[4] = view.volatile_modes;
	header[5] = 6;
	memcpy(output, header, sizeof(header));
	total = sizeof(header);
	for (size_t i = 0; i < 4; i++) {
		if (!values[i].data_size)
			continue;
		memcpy((uint8_t *)output + total, values[i].data, values[i].data_size);
		total += (values[i].data_size + 7U) & ~(size_t)7U;
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint64_t policy_get(void *context, const uint16_t *name, const void *guid,
	uint32_t *attributes, size_t *size, void *data)
{
	struct authvar_owner *owner = context;
	bool runtime = owner->runtime;

	/* Internal predicates can inspect BS-only state after the runtime transition. */
	owner->runtime = false;
	uint64_t status = authvar_owner_get(owner, name, guid, attributes, size, data);
	owner->runtime = runtime;
	return status;
}

static uint64_t set_variable(struct smm_authvar_dispatch *service,
	const uint16_t *name, const void *guid, uint32_t attributes,
	size_t size, const void *data)
{
	struct payload_mm_authvar_auth2_view auth;
	const struct authvar_policy_entry *rejected;
	size_t payload_size = size;
	size_t reclaimed;
	uint64_t status;

#if CONFIG(DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT)
	/* Only the authenticated broker can publish durable update outcomes. */
	if (smm_capsule_state_name(name, guid))
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
#endif
	if (attributes & PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED) {
		if (payload_mm_authvar_auth2_parse(data, size, &auth) !=
		    PAYLOAD_MM_AUTHVAR_FORMAT_OK)
			return PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
		payload_size = auth.payload.size;
	}
	status = authvar_policy_check(&service->policy, name, guid,
		attributes, payload_size, policy_get, &service->owner, &rejected);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		return status;
	return authvar_owner_set(&service->owner, name, guid,
		attributes, size, data, false, service->fixed, &reclaimed);
}

uint64_t smm_authvar_dispatch_option(struct smm_authvar_dispatch *service,
	const uint16_t *name, uint32_t value)
{
	static const uint8_t guid[16] = {
		0x1d, 0x4c, 0xae, 0xce, 0x5b, 0x33, 0x85, 0x46,
		0xa4, 0xa0, 0xfc, 0x4a, 0x94, 0xee, 0xa0, 0x85,
	};

	return set_variable(service, name, guid,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE | PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS, sizeof(value), &value);
}

enum cb_err smm_authvar_dispatch(struct smm_authvar_dispatch *service,
	const struct lb_authvar_service_endpoint *endpoint,
	const void *request, void *response, size_t size)
{
	const struct payload_mm_authvar_service_frame *in = request;
	struct payload_mm_authvar_service_frame *out = response;
	const uint16_t *name;
	const void *data;
	void *result;
	size_t offset, length;
	uint64_t status;
	uint32_t attributes = 0;

	if (!service || !response || response == request ||
	    payload_mm_authvar_service_request_validate(endpoint, request, size) != CB_SUCCESS)
		return CB_ERR_ARG;
	/* Confirmed Setup needs a separate producer-owned confirmation lifecycle. */
	if (in->revision != PAYLOAD_MM_AUTHVAR_SERVICE_REVISION)
		return CB_ERR_ARG;
	offset = (sizeof(*in) + endpoint->maximum_name_size + 7U) & ~(size_t)7U;
	name = (const void *)((const uint8_t *)request + sizeof(*in));
	data = (const uint8_t *)request + offset;
	memset(response, 0, size);
	memcpy(response, request, sizeof(*in));
	result = (uint8_t *)response + offset;
	status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	if (service->owner.failed || service->owner.restart_required) {
		status = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		goto complete;
	}

	switch (in->operation) {
	case PAYLOAD_MM_AUTHVAR_SERVICE_GET:
		length = in->data_capacity;
		status = authvar_owner_get(&service->owner, name, in->vendor_guid,
			&attributes, &length, result);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		    status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL) {
			out->result_data_size = length;
			out->result_attributes = attributes;
		}
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_NEXT: {
		void *next = (uint8_t *)response + sizeof(*out);

		memcpy(next, name, in->name_size);
		memcpy(out->result_vendor_guid, in->vendor_guid, sizeof(in->vendor_guid));
		length = in->name_capacity;
		status = authvar_owner_next(&service->owner, &length, next,
			out->result_vendor_guid);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		    status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)
			out->result_name_size = length;
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			memset(next, 0, offset - sizeof(*out));
			memset(out->result_vendor_guid, 0, sizeof(out->result_vendor_guid));
		} else {
			memset((uint8_t *)next + length, 0, offset - sizeof(*out) - length);
		}
		break;
	}
	case PAYLOAD_MM_AUTHVAR_SERVICE_SET:
		status = set_variable(service, name, in->vendor_guid, in->attributes,
			in->data_size, data);
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_QUERY:
		status = authvar_owner_query(&service->owner, in->attributes,
			&out->maximum_storage, &out->remaining_storage, &out->maximum_variable);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		    out->maximum_variable > endpoint->maximum_data_size)
			out->maximum_variable = endpoint->maximum_data_size;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT:
		authvar_owner_ready(&service->owner);
		status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME:
		service->policy.locked = true;
		authvar_owner_enter_runtime(&service->owner);
		status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_REGISTER_POLICY: {
		const struct authvar_policy_entry *entry = data;

		if (service->owner.runtime)
			status = PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
		else if (entry->size != in->data_size)
			status = PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		else if (entry->lock_policy_type == 3 &&
			 !(endpoint->flags & LB_AUTHVAR_ENDPOINT_STATE_PREDICATE_PINNED))
			status = PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
		else
			status = authvar_policy_register(&service->policy, entry);
		break;
	}
	case PAYLOAD_MM_AUTHVAR_SERVICE_LOCK_POLICY:
		service->policy.locked = true;
		status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_IMAGE_POLICY_SNAPSHOT:
		status = image_snapshot(&service->owner, result, in->data_capacity,
			&out->result_data_size);
		break;
	case PAYLOAD_MM_AUTHVAR_SERVICE_KEY_CLASSIFY: {
		struct payload_mm_authvar_key_classification classification;
		uint32_t fields[4];
		bool reserved = false;

#if CONFIG(DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT)
		reserved = smm_capsule_state_name(name, in->vendor_guid);
#endif

		status = authvar_owner_classify(&service->owner, name, in->vendor_guid,
			&classification);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND &&
		    (reserved || state_predicate(&service->policy, in->vendor_guid, name, in->name_size) ||
		     authvar_policy_has_match(&service->policy, name, in->name_size,
			in->vendor_guid))) {
			classification = (struct payload_mm_authvar_key_classification) {
				.kind = PAYLOAD_MM_AUTHVAR_KEY_VOLATILE_PINNED,
			};
			status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		}
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
			break;
		out->result_data_size = sizeof(fields);
		if (in->data_capacity < sizeof(fields)) {
			status = PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL;
			break;
		}
		fields[0] = PAYLOAD_MM_AUTHVAR_KEY_CLASSIFICATION_REVISION;
		fields[1] = classification.kind;
		fields[2] = classification.attributes;
		fields[3] = classification.flags;
		memcpy(result, fields, sizeof(fields));
		break;
	}
	default:
		break;
	}

complete:
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
		memset(result, 0, size - offset);
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
	    status != PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL) {
		out->maximum_storage = 0;
		out->remaining_storage = 0;
		out->maximum_variable = 0;
		out->result_name_size = 0;
		out->result_data_size = 0;
		out->result_attributes = 0;
		memset(out->result_vendor_guid, 0, sizeof(out->result_vendor_guid));
	}
	out->status = status;
	out->completion = PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE;
	return payload_mm_authvar_service_response_validate(endpoint, request, response, size);
}
