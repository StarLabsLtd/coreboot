/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_owner.h>
#include <boot/authvar_fixed_policy.h>
#include <boot/authvar_media.h>
#include <boot/authvar_policy.h>
#include <boot/payload_mm_authvar_certdb.h>
#include <boot/payload_mm_authvar_default_store.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/bsd/fmap_serialized.h>
#include <commonlib/endian.h>
#include <commonlib/payload_mm_authvar_fv.h>
#include <efi_variable_service.h>
#include <string.h>
#include <tests/test.h>
#include <drivers/efi_variable_service/native_variables.h>
#include <drivers/efi_variable_service/native_capsule.h>
#include <drivers/efi_variable_service/native_capsule_source.h>
#include <drivers/efi_variable_service/native_media.h>
#include <drivers/efi_variable_service/native_smm.h>
#include <drivers/efi_variable_service/wire.h>

#include "authvar-capsule-fixture.h"
#include "authvar-capsule-bios-fixture.h"
#include "authvar-capsule-ab-fixture.h"

struct test_media {
	uint8_t *store;
	size_t size;
	bool reject;
	unsigned int writes;
};

static uint64_t commit(void *context, const void *store, size_t size,
	const struct payload_mm_authvar_write_plan *append, const void *record)
{
	struct test_media *media = context;

	assert_int_equal(size, media->size);
	media->writes++;
	if (media->reject)
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	memcpy(media->store, store, size);
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static void restore_deleted_secure_boot_preference(void **unused)
{
	static struct {
		struct authvar_owner owner;
		_Alignas(16) uint8_t workspace[128 * 1024];
	} service;
	static uint8_t erased[3 * 4096], volume[sizeof(erased)];
	uint8_t *store = volume + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	const size_t store_size = 4096 - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	struct test_media media = { .store = store, .size = store_size };
	const uint16_t pk[] = u"PK", enable[] = u"SecureBootEnable";
	const uint8_t enable_guid[] = {
		0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
		0x99, 0xc4, 0, 0x10, 0x09, 0xc9, 0x3a, 0x44,
	};
	const struct payload_mm_authvar_record_descriptor platform_key = {
		.name = pk, .name_size = sizeof(pk), .attributes = 0x27,
		.vendor_guid = {
			0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
			0xaa, 0x0d, 0, 0xe0, 0x98, 3, 0x2b, 0x8c,
		},
	};
	/* This loaded record supplies PK presence; no signature is verified here. */
	const uint8_t key_data = 1;
	const struct payload_mm_authvar_record_span span = { &key_data, sizeof(key_data) };
	size_t offset = payload_mm_authvar_default_store_records_end();
	uint32_t length;
	uint8_t value = 0;
	size_t size = sizeof(value);

	memset(erased, 0xff, sizeof(erased));
	assert_int_equal(payload_mm_authvar_default_store_compose(erased, volume,
		sizeof(volume), 4096), PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
	assert_true(payload_mm_authvar_record_encode(&platform_key, &span, 1,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
		store + offset, store_size - offset, &length));
	store[offset + 2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	assert_true(service.owner.modes & PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT);
	assert_int_equal(authvar_owner_get(&service.owner, enable, enable_guid, NULL,
		&size, &value), 0);
	assert_int_equal(value, 1);
	value = 0;
	assert_int_equal(authvar_owner_set(&service.owner, enable, enable_guid, 3, 1,
		&value, true, NULL, NULL), 0);
	assert_true(service.owner.modes & PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT);
	const uint16_t state_name[] = u"SecureBoot";
	_Alignas(8) uint8_t request[sizeof(struct smm_variable_header32) +
		sizeof(struct smm_variable_access32) + sizeof(state_name) + 1] = { 0 };
	struct smm_variable_header32 *message = (void *)request;
	struct smm_variable_access32 *access = (void *)message->data;
	uint8_t *state = (uint8_t *)access->name + sizeof(state_name);
	struct authvar_policy policy = { 0 };

	message->function = SMM_VARIABLE_FUNCTION_SET_VARIABLE;
	memcpy(&access->guid, platform_key.vendor_guid, sizeof(access->guid));
	access->attributes = 6;
	access->name_size = sizeof(state_name);
	access->data_size = 1;
	memcpy(access->name, state_name, sizeof(state_name));
	*state = 1;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000002U);
	*state = 0;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	assert_false(service.owner.modes & PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT);
	enum ass_mm_phase phase = ASS_MM_WAIT_END_OF_DXE;
	_Alignas(8) uint8_t packet[sizeof(struct mm_communication_header32) + 1] = { 0 };
	struct mm_communication_header32 *wire = (void *)packet;
	wire->guid = (EFI_GUID)ASS_END_OF_DXE_GUID;
	wire->message_length = 1;
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase,
		NULL, packet, sizeof(packet)), 0);
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000008U);
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	assert_false(service.owner.modes & PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT);
	assert_int_equal(authvar_owner_set(&service.owner, enable, enable_guid, 0, 0,
		NULL, true, NULL, NULL), 0);
	assert_false(service.owner.modes & PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT);
	assert_int_equal(authvar_owner_get(&service.owner, enable, enable_guid, NULL,
		&size, &value), PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	assert_true(service.owner.modes & PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT);
	assert_int_equal(authvar_owner_get(&service.owner, enable, enable_guid, NULL,
		&size, &value), 0);
	assert_int_equal(value, 1);
	assert_int_equal(media.writes, 4);
}

static void check_uefi_properties_at_runtime(void **unused)
{
	static struct {
		struct authvar_owner owner;
		_Alignas(16) uint8_t workspace[128 * 1024];
	} service;
	static uint8_t erased[3 * 4096], volume[sizeof(erased)];
	uint8_t *store = volume + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	const size_t store_size = 4096 - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	struct test_media media = { .store = store, .size = store_size };
	static struct authvar_policy policy = { .locked = true };
	const uint16_t name[] = u"BootNext";
	_Alignas(8) uint8_t request[sizeof(struct smm_variable_header32) +
		sizeof(struct smm_variable_access32) + sizeof(name) + 2] = { 0 };
	struct smm_variable_header32 *message = (void *)request;
	struct smm_variable_access32 *access = (void *)message->data;
	uint8_t *value = (uint8_t *)access->name + sizeof(name);

	memset(erased, 0xff, sizeof(erased));
	assert_int_equal(payload_mm_authvar_default_store_compose(erased, volume,
		sizeof(volume), 4096), PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	assert_int_equal(authvar_owner_ready(&service.owner, NULL), 0);
	authvar_owner_enter_runtime(&service.owner);
	message->function = SMM_VARIABLE_FUNCTION_SET_VARIABLE;
	access->guid = (EFI_GUID) {
		0x8be4df61, 0x93ca, 0x11d2, { 0xaa, 0x0d, 0, 0xe0, 0x98, 3, 0x2b, 0x8c },
	};
	access->name_size = sizeof(name);
	access->data_size = 1;
	access->attributes = 7;
	memcpy(access->name, name, sizeof(name));
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000002U);
	assert_int_equal(media.writes, 0);
	access->data_size = 2;
	access->attributes = 6;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000002U);
	assert_int_equal(media.writes, 0);
	access->attributes = 7;
	write_le16(value, 0x1234);
	memcpy(access->name, u"BadNameX", sizeof(name));
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000002U);
	assert_int_equal(media.writes, 0);
	memcpy(access->name, name, sizeof(name));
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	assert_int_equal(media.writes, 1);
	message->function = SMM_VARIABLE_FUNCTION_GET_VARIABLE;
	write_le16(value, 0);
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	assert_int_equal(access->data_size, 2);
	assert_int_equal(read_le16(value), 0x1234);
	message->function = SMM_VARIABLE_FUNCTION_SET_VARIABLE;
	access->guid = (EFI_GUID) {
		0xd719b2cb, 0x3d3a, 0x4596, { 0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f },
	};
	access->name_size = sizeof(u"db");
	memcpy(access->name, u"db", sizeof(u"db"));
	access->attributes = 0x23;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000002U);
	access->attributes = 0x27;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x8000001aU);
	assert_int_equal(media.writes, 1);
}

static void factory_defaults_are_sealed_at_handoff(void **unused)
{
	static struct {
		struct authvar_owner owner;
		_Alignas(16) uint8_t workspace[128 * 1024];
	} service;
	static uint8_t erased[3 * 4096], volume[sizeof(erased)];
	uint8_t *store = volume + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	const size_t store_size = 4096 - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	struct test_media media = { .store = store, .size = store_size };
	static struct authvar_policy policy;
	const uint16_t name[] = u"PKDefault";
	_Alignas(8) uint8_t request[sizeof(struct smm_variable_header32) +
		sizeof(struct smm_variable_access32) + sizeof(name) + 1] = { 0 };
	struct smm_variable_header32 *message = (void *)request;
	struct smm_variable_access32 *access = (void *)message->data;
	uint8_t *value = (uint8_t *)access->name + sizeof(name);

	memset(erased, 0xff, sizeof(erased));
	assert_int_equal(payload_mm_authvar_default_store_compose(erased, volume,
		sizeof(volume), 4096), PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	message->function = SMM_VARIABLE_FUNCTION_SET_VARIABLE;
	access->guid = (EFI_GUID) {
		0x8be4df61, 0x93ca, 0x11d2, { 0xaa, 0x0d, 0, 0xe0, 0x98, 3, 0x2b, 0x8c },
	};
	access->name_size = sizeof(name);
	access->data_size = 1;
	access->attributes = 6;
	memcpy(access->name, name, sizeof(name));
	*value = 1;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	enum ass_mm_phase phase = ASS_MM_WAIT_END_OF_DXE;
	_Alignas(8) uint8_t packet[sizeof(struct mm_communication_header32) + 1] = { 0 };
	struct mm_communication_header32 *wire = (void *)packet;
	wire->guid = (EFI_GUID)ASS_END_OF_DXE_GUID;
	wire->message_length = 1;
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase,
		NULL, packet, sizeof(packet)), 0);
	assert_false(service.owner.ready);
	*value = 2;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000008U);
	authvar_owner_enter_runtime(&service.owner);
	access->attributes = 0;
	access->data_size = 0;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000008U);
	message->function = SMM_VARIABLE_FUNCTION_GET_VARIABLE;
	access->data_size = 1;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	assert_int_equal(*value, 1);
	assert_int_equal(media.writes, 0);
}

static void admit_existing_store(void **unused)
{
	static struct {
		struct authvar_owner owner;
		_Alignas(16) uint8_t workspace[128 * 1024];
	} service;
	static uint8_t erased[3 * 4096], volume[sizeof(erased)];
	uint8_t *store = volume + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	const size_t store_size = 4096 - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	static struct authvar_policy policy;
	enum ass_mm_phase phase = ASS_MM_WAIT_END_OF_DXE;
	struct test_media media = { .store = store, .size = store_size };
	const uint16_t name[] = { 'T', 'e', 's', 't', 0 };
	const uint8_t guid[16] = { 0x11, 0x23, 0x45 };
	const uint32_t value = 0x12abc345;
	uint32_t result = 0, attributes = 0;
	size_t result_size = sizeof(result);
	const uint32_t nv = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;

	memset(erased, 0xff, sizeof(erased));
	assert_int_equal(payload_mm_authvar_default_store_compose(erased, volume,
		sizeof(volume), 4096), PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 1024,
		commit, &media, service.workspace, sizeof(service.workspace), true,
		NULL, NULL), PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert_int_equal(media.writes, 0);
	assert_int_equal(authvar_owner_set(&service.owner, name, guid, nv, sizeof(value),
		&value, false, NULL, NULL), PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert_int_equal(media.writes, 1);
	media.reject = true;
	assert_int_equal(authvar_owner_set(&service.owner, name, guid, nv, sizeof(result),
		&result, false, NULL, NULL), PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert_int_equal(media.writes, 2);
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 1024,
		commit, &media, service.workspace, sizeof(service.workspace), true,
		NULL, NULL), PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert_int_equal(authvar_owner_get(&service.owner, name, guid, &attributes,
		&result_size, &result), PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert_int_equal(result, value);
	assert_int_equal(attributes, nv);

	_Alignas(8) uint8_t request[sizeof(struct smm_variable_header32) +
		sizeof(struct smm_variable_access32) + sizeof(name) + sizeof(value)] = { 0 };
	struct smm_variable_header32 *message = (void *)request;
	struct smm_variable_access32 *access = (void *)message->data;
	message->function = SMM_VARIABLE_FUNCTION_GET_VARIABLE;
	memcpy(&access->guid, guid, sizeof(guid));
	memcpy(access->name, name, sizeof(name));
	access->name_size = sizeof(name);
	access->data_size = sizeof(value);
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	assert_int_equal(access->attributes, nv);
	assert_memory_equal((uint8_t *)access->name + sizeof(name), &value, sizeof(value));
	_Alignas(8) uint8_t packet[sizeof(struct mm_communication_header32) + sizeof(request)];
	struct mm_communication_header32 *wire = (void *)packet;
	wire->guid = (EFI_GUID)ASS_VARIABLE_GUID;
	wire->message_length = sizeof(request);
	memcpy(wire->data, request, sizeof(request));
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		packet, sizeof(packet)), 0);
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		packet, sizeof(packet) - 1), 0x80000002U);
	static _Alignas(8) uint8_t shared[EFI_VARIABLE_SERVICE_BUFFER_SIZE];
	_Alignas(8) uint8_t scratch[sizeof(packet)];
	struct efi_variable_service_buffer *mailbox = (void *)shared;
	void *shared_message = shared + EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE;
	*mailbox = (struct efi_variable_service_buffer) {
		.signature = EFI_VARIABLE_SERVICE_BUFFER_SIGNATURE,
		.revision = EFI_VARIABLE_SERVICE_REVISION,
		.header_size = EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE,
		.state = EFI_VARIABLE_SERVICE_BUFFER_REQUEST,
		.transaction = 7,
		.message_size = sizeof(packet),
	};
	memcpy(shared_message, packet, sizeof(packet));
	assert_int_equal(ass_native_mailbox_dispatch(&service.owner, &policy, NULL,
		&phase, NULL, shared, sizeof(shared), shared_message, sizeof(packet)),
		EFI_VARIABLE_SERVICE_RET_FAILURE);
	assert_int_equal(ass_native_mailbox_dispatch(&service.owner, &policy, NULL,
		&phase, NULL, shared, sizeof(shared), scratch, sizeof(scratch) - 1),
		EFI_VARIABLE_SERVICE_RET_FAILURE);
	assert_int_equal(mailbox->state, EFI_VARIABLE_SERVICE_BUFFER_REQUEST);
	memset(scratch, 0xaa, sizeof(scratch));
	assert_int_equal(ass_native_mailbox_dispatch(&service.owner, &policy, NULL,
		&phase, NULL, shared, sizeof(shared), scratch, sizeof(scratch)),
		EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(mailbox->state, EFI_VARIABLE_SERVICE_BUFFER_RESPONSE);
	assert_int_equal(mailbox->transaction, 7);
	assert_int_equal(mailbox->response_size, sizeof(packet));
	assert_int_equal(mailbox->status, 0);
	assert_memory_equal(shared_message, packet, sizeof(packet));
	assert_memory_equal(scratch, (uint8_t[sizeof(scratch)]) { 0 }, sizeof(scratch));
	wire->guid = (EFI_GUID)ASS_SERVICE_READY_GUID;
	wire->message_length = 1;
	wire->data[0] = 0;
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		packet, sizeof(*wire) + 1), 0);
	wire->data[0] = 1;
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		packet, sizeof(*wire) + 1), 0x80000002U);
	wire->guid = (EFI_GUID){ 0 };
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		packet, sizeof(*wire) + 1), 0x80000003U);

	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request, sizeof(request) - 1));
	assert_int_equal(message->return_status, 0x80000002U);
	access->data_size = 0;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request,
		sizeof(request) - sizeof(value)));
	assert_int_equal(message->return_status, 0x80000005U);
	assert_int_equal(access->data_size, sizeof(value));
	message->function = SMM_VARIABLE_FUNCTION_SET_VARIABLE;
	access->data_size = sizeof(value);
	access->attributes = nv;
	memset((uint8_t *)access->name + sizeof(name), 0, sizeof(value));
	media.reject = false;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	assert_int_equal(media.writes, 3);
	struct {
		struct authvar_policy_entry entry;
		uint16_t name[ARRAY_SIZE(name)];
	} lock = {
		.entry = {
			.version = AUTHVAR_POLICY_REVISION,
			.size = sizeof(lock),
			.offset_to_name = sizeof(lock.entry),
			.max_size = UINT32_MAX,
			.lock_policy_type = 1,
		},
	};
	memcpy(lock.entry.name_space, guid, sizeof(guid));
	memcpy(lock.name, name, sizeof(name));
	static _Alignas(8) uint8_t policy_packet[16 * 1024];
	struct mm_communication_header32 *policy_wire = (void *)policy_packet;
	struct smm_variable_policy32 *policy_message = (void *)policy_wire->data;
	policy_wire->guid = (EFI_GUID)ASS_VARIABLE_POLICY_GUID;
	policy_wire->message_length = sizeof(policy_packet) - sizeof(*policy_wire);
	policy_message->signature = ASS_VARIABLE_POLICY_SIGNATURE;
	policy_message->revision = ASS_VARIABLE_POLICY_REVISION;
	policy_message->command = ASS_POLICY_ENABLED;
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		policy_packet, sizeof(*policy_wire) + policy_wire->message_length), 0);
	assert_int_equal(policy_message->result, 0);
	assert_int_equal(policy_message->data[0], 1);
	policy_message->command = ASS_POLICY_REGISTER;
	policy_wire->message_length = sizeof(*policy_message) + sizeof(lock) - 1;
	memcpy(policy_message->data, &lock, sizeof(lock));
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		policy_packet, sizeof(*policy_wire) + policy_wire->message_length), 0);
	assert_int_equal(policy_message->result, 0x80000002U);
	assert_int_equal(policy.count, 0);
	policy_wire->message_length = sizeof(policy_packet) - sizeof(*policy_wire);
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		policy_packet, sizeof(*policy_wire) + policy_wire->message_length), 0);
	assert_int_equal(policy_message->result, 0);
	assert_int_equal(policy.count, 1);
	memcpy((uint8_t *)access->name + sizeof(name), &value, sizeof(value));
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000008U);
	assert_int_equal(media.writes, 3);
	access->attributes |= PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x8000001aU);
	assert_int_equal(media.writes, 3);

	struct smm_variable_next32 *lock_request = (void *)message->data;
	const size_t lock_size = sizeof(*message) + sizeof(*lock_request) + sizeof(name);
	message->function = SMM_VARIABLE_FUNCTION_LOCK_VARIABLE;
	memcpy(&lock_request->guid, guid, sizeof(guid));
	lock_request->name_size = sizeof(name);
	memcpy(lock_request->name, name, sizeof(name));
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request,
		lock_size - 1));
	assert_int_equal(message->return_status, 0x80000002U);
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request,
		lock_size));
	assert_int_equal(message->return_status, 0);
	assert_int_equal(policy.count, 1);
	lock_request->guid.Data1 ^= 1;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request,
		lock_size));
	assert_int_equal(message->return_status, 0);
	assert_int_equal(policy.count, 2);

	memset(request, 0, sizeof(request));
	struct smm_variable_query32 *query = (void *)message->data;
	message->function = SMM_VARIABLE_FUNCTION_QUERY_VARIABLE_INFO;
	query->attributes = nv;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request,
		sizeof(*message) + sizeof(*query) - 1));
	assert_int_equal(message->return_status, 0x80000002U);
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request,
		sizeof(*message) + sizeof(*query)));
	assert_int_equal(message->return_status, 0);
	assert_true(query->maximum_storage > 0);
	assert_true(query->remaining_storage <= query->maximum_storage);
	assert_int_equal(query->maximum_variable, 1024);
	query->attributes = PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request,
		sizeof(*message) + sizeof(*query)));
	assert_int_equal(message->return_status, 0);
	assert_int_equal(query->maximum_variable, 1024);

	memset(request, 0, sizeof(request));
	struct smm_variable_next32 *next = (void *)message->data;
	message->function = SMM_VARIABLE_FUNCTION_GET_NEXT_VARIABLE_NAME;
	next->name_size = 3;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000002U);
	next->name_size = sizeof(uint16_t);
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request, sizeof(request)));
	assert_int_equal(message->return_status, 0x80000005U);
	assert_true(next->name_size > sizeof(uint16_t));
	_Alignas(8) uint8_t enumeration[128] = { 0 };
	struct smm_variable_header32 *enumerate = (void *)enumeration;
	next = (void *)enumerate->data;
	enumerate->function = SMM_VARIABLE_FUNCTION_GET_NEXT_VARIABLE_NAME;
	next->name_size = sizeof(enumeration) - sizeof(*enumerate) - sizeof(*next);
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, enumeration, sizeof(enumeration)));
	assert_int_equal(enumerate->return_status, 0);
	assert_true(next->name_size > sizeof(uint16_t));
	assert_true(next->name[0] != 0);

	message->function = SMM_VARIABLE_FUNCTION_GET_PAYLOAD_SIZE;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(*message) + sizeof(uint32_t)));
	assert_int_equal(message->return_status, 0);
	assert_true(read_le32(message->data) >= sizeof(*access) + sizeof(name));
	message->function = SMM_VARIABLE_FUNCTION_EXIT_BOOT_SERVICE;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(*message)));
	assert_int_equal(message->return_status, 0x8000000fU);
	assert_false(service.owner.runtime);
	wire->message_length = 1;
	wire->data[0] = 0;
	wire->guid = (EFI_GUID)ASS_READY_TO_LOCK_GUID;
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		packet, sizeof(*wire) + 1), 0x8000000fU);
	assert_int_equal(phase, ASS_MM_WAIT_END_OF_DXE);
	const EFI_GUID events[] = {
		ASS_END_OF_DXE_GUID, ASS_READY_TO_LOCK_GUID,
		ASS_READY_TO_BOOT_GUID, ASS_EXIT_BOOT_SERVICES_GUID,
	};
	const uint16_t custom_mode[] = u"CustomMode";
	_Alignas(8) uint8_t setup_request[sizeof(*message) + sizeof(*access) +
		sizeof(custom_mode) + 1] = { 0 };
	struct smm_variable_header32 *setup = (void *)setup_request;
	struct smm_variable_access32 *setup_access = (void *)setup->data;
	setup->function = SMM_VARIABLE_FUNCTION_SET_VARIABLE;
	setup_access->guid = (EFI_GUID) {
		0xc076ec0c, 0x7028, 0x4399, { 0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f },
	};
	setup_access->attributes = nv;
	setup_access->name_size = sizeof(custom_mode);
	setup_access->data_size = 1;
	memcpy(setup_access->name, custom_mode, sizeof(custom_mode));
	setup_request[sizeof(setup_request) - 1] = 1;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		setup_request, sizeof(setup_request)));
	assert_int_equal(setup->return_status, 0);
	size_t mode_size = 1;
	uint8_t mode;
	assert_int_equal(authvar_owner_get(&service.owner, custom_mode,
		&setup_access->guid, NULL, &mode_size, &mode), 0);
	assert_int_equal(mode, 1);
	policy_message->command = ASS_POLICY_LOCK;
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		policy_packet, sizeof(policy_packet)), 0);
	assert_int_equal(policy_message->result, 0);
	assert_true(policy.locked);
	policy_message->command = ASS_POLICY_REGISTER;
	for (size_t i = 0; i < ARRAY_SIZE(events) - 1; i++) {
		wire->guid = events[i];
		assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
			packet, sizeof(*wire) + 1), 0);
		assert_int_equal(phase, i + 1);
		assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
			packet, sizeof(*wire) + 1), 0);
		assert_int_equal(phase, i + 1);
	}
	message->function = SMM_VARIABLE_FUNCTION_READY_TO_BOOT;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(*message)));
	assert_int_equal(message->return_status, 0);
	assert_true(service.owner.ready);
	setup_request[sizeof(setup_request) - 1] = 0;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		setup_request, sizeof(setup_request)));
	assert_int_equal(setup->return_status, 0x8000001aU);
	assert_true(policy.locked);
	message->function = SMM_VARIABLE_FUNCTION_LOCK_VARIABLE;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL, request,
		lock_size));
	assert_int_equal(message->return_status, 0x8000000fU);
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		policy_packet, sizeof(*policy_wire) + policy_wire->message_length), 0);
	assert_int_equal(policy_message->result, 0x80000008U);
	assert_int_equal(policy.count, 2);
	wire->guid = events[ARRAY_SIZE(events) - 1];
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		packet, sizeof(*wire) + 1), 0);
	assert_int_equal(phase, ASS_MM_RUNTIME);
	message->function = SMM_VARIABLE_FUNCTION_EXIT_BOOT_SERVICE;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(*message)));
	assert_int_equal(message->return_status, 0);
	assert_true(service.owner.runtime);

	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 1024,
		commit, &media, service.workspace, sizeof(service.workspace), true,
		NULL, NULL), PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert_int_equal(authvar_owner_get(&service.owner, custom_mode,
		&setup_access->guid, NULL, &mode_size, &mode), 0);
	assert_int_equal(mode, 0);
	phase = ASS_MM_WAIT_END_OF_DXE;
	for (size_t i = 0; i < 2; i++) {
		wire->guid = events[i];
		assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
			packet, sizeof(*wire) + 1), 0);
	}
	assert_false(service.owner.ready);
	wire->guid = (EFI_GUID)ASS_VARIABLE_GUID;
	wire->message_length = sizeof(*message);
	message->function = SMM_VARIABLE_FUNCTION_EXIT_BOOT_SERVICE;
	memcpy(wire->data, request, sizeof(*message));
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		packet, sizeof(*wire) + sizeof(*message)), 0);
	assert_int_equal(((struct smm_variable_header32 *)wire->data)->return_status, 0);
	assert_true(service.owner.ready);
	assert_true(service.owner.runtime);
	assert_int_equal(phase, ASS_MM_WAIT_READY_TO_BOOT);
	wire->guid = events[ARRAY_SIZE(events) - 1];
	wire->message_length = 1;
	wire->data[0] = 0;
	assert_int_equal(ass_native_mm_dispatch(&service.owner, &policy, NULL, &phase, NULL,
		packet, sizeof(*wire) + 1), 0);
	assert_int_equal(phase, ASS_MM_RUNTIME);
	assert_true(service.owner.ready);
	assert_true(service.owner.runtime);

	unsigned int writes = media.writes;
	store[0] ^= 1;
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 1024,
		commit, &media, service.workspace, sizeof(service.workspace), true,
		NULL, NULL), PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED);
	assert_int_equal(media.writes, writes);
}

enum { NOR_BLOCK = 4096, NOR_SIZE = 3 * NOR_BLOCK, LITE_NOR_SIZE = 512 * 1024 };

struct test_nor {
	struct region_device region;
	uint8_t bytes[LITE_NOR_SIZE];
	bool fail_write;
	bool partial_write;
};

static ssize_t nor_read(const struct region_device *region, void *data, size_t offset,
	size_t size)
{
	const struct test_nor *nor = container_of(region, struct test_nor, region);

	assert_true(offset <= region_device_sz(region) && size <= region_device_sz(region) - offset);
	memcpy(data, nor->bytes + offset, size);
	return size;
}

static ssize_t nor_write(const struct region_device *region, const void *data, size_t offset,
	size_t size)
{
	struct test_nor *nor = container_of(region, struct test_nor, region);
	const uint8_t *input = data;

	assert_true(offset <= region_device_sz(region) && size <= region_device_sz(region) - offset);
	for (size_t i = 0; i < size; i++) {
		assert_int_equal(nor->bytes[offset + i] & input[i], input[i]);
		const bool changed = nor->bytes[offset + i] != input[i];
		nor->bytes[offset + i] &= input[i];
		if (nor->fail_write && changed) {
			nor->partial_write = true;
			return -1;
		}
	}
	return size;
}

static ssize_t nor_erase(const struct region_device *region, size_t offset, size_t size)
{
	struct test_nor *nor = container_of(region, struct test_nor, region);

	assert_true(offset <= region_device_sz(region) && size <= region_device_sz(region) - offset);
	assert_int_equal(offset % NOR_BLOCK, 0);
	assert_int_equal(size % NOR_BLOCK, 0);
	memset(nor->bytes + offset, 0xff, size);
	return size;
}

static const struct region_device_ops nor_ops = {
	.readat = nor_read, .writeat = nor_write, .eraseat = nor_erase,
};
static bool allow_program;

static bool nor_write_enabled(void)
{
	return allow_program;
}

static void owner_on_nor(void **unused)
{
	static _Alignas(16) uint8_t arena[sizeof(struct authvar_owner) + 256 * 1024];
	static struct test_nor nor;
	struct ass_native_store store;
	struct ass_native_store_memory memory;
	const uint8_t guid[16] = { 0x47, 0xa9 };
	const uint16_t name[] = { 'M', 'e', 'd', 'i', 'a', 0 };
	const uint32_t nv = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	uint32_t value = 0x4259;

	assert_false(ass_native_store_layout(&store, &memory, arena, NOR_SIZE,
		NOR_SIZE, NOR_BLOCK));
	assert_true(ass_native_store_layout(&store, &memory, arena, sizeof(arena),
		NOR_SIZE, NOR_BLOCK));
	memset(nor.bytes, 0xff, NOR_SIZE);
	region_device_init(&nor.region, &nor_ops, 0, NOR_SIZE);
	assert_true(ass_native_media_bind(&store.binding, &store.io, &nor.region,
		memory.scratch, NOR_BLOCK, nor_write_enabled));
	allow_program = false;
	assert_int_equal(store.io.write(store.io.context, 0, &value, sizeof(value)),
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert_int_equal(nor.bytes[0], 0xff);
	allow_program = true;
	assert_int_equal(ass_native_store_open(&store, memory.owner, &nor.region, memory.scratch,
		nor_write_enabled, memory.workspace, memory.workspace_size, 1024, true, NULL), 0);
	assert_int_equal(authvar_owner_set(memory.owner, name, guid, nv, sizeof(value),
		&value, false, NULL, NULL), 0);

	nor.fail_write = true;
	uint32_t replacement = 0x3741;
	assert_int_equal(authvar_owner_set(memory.owner, name, guid, nv, sizeof(replacement),
		&replacement, false, NULL, NULL), PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert_true(nor.partial_write);
	nor.fail_write = false;

	store.media.failed = false;
	assert_int_equal(ass_native_store_open(&store, memory.owner, &nor.region, memory.scratch,
		nor_write_enabled, memory.workspace, memory.workspace_size, 1024, true, NULL), 0);
	uint32_t result = 0;
	size_t size = sizeof(result);
	assert_int_equal(authvar_owner_get(memory.owner, name, guid, NULL, &size, &result), 0);
	assert_int_equal(result, value);

	const uint16_t lsv[] = { 'F', 'm', 'p', 'L', 's', 'v', 0 };
	const uint16_t state[] = { 'F', 'm', 'p', 'S', 't', 'a', 't', 'e', 0 };
	uint8_t floor_record[20] = { 0 };
	uint32_t floor;

	assert_int_equal(ass_native_capsule_floor(memory.owner, guid, 7, &floor), 0);
	assert_int_equal(floor, 7);
	write_le32(floor_record, 10);
	assert_int_equal(authvar_owner_set(memory.owner, lsv, guid, nv, 4,
		floor_record, false, NULL, NULL), 0);
	assert_int_equal(ass_native_capsule_floor(memory.owner, guid, 7, &floor), 0);
	assert_int_equal(floor, 10);
	floor_record[0] = 0;
	floor_record[1] = 1;
	write_le32(floor_record + 8, 12);
	assert_int_equal(authvar_owner_set(memory.owner, state, guid, nv, sizeof(floor_record),
		floor_record, false, NULL, NULL), 0);
	assert_int_equal(ass_native_capsule_floor(memory.owner, guid, 7, &floor), 0);
	assert_int_equal(floor, 12);
	assert_int_equal(ass_native_capsule_floor(memory.owner, guid, 13, &floor), 0);
	assert_int_equal(floor, 13);
	struct ass_capsule_geometry geometry = {
		.flash_size = 16 * 1024 * 1024,
		.block_size = 64 * 1024,
		.compiled_floor = 13,
	};
	memcpy(geometry.image_guid, guid, sizeof(guid));
	uint8_t info[28] = { 0 };
	write_le32(info, 1);
	write_le32(info + 4, 1);
	assert_int_equal(ass_native_capsule_info(memory.owner, &geometry, info, 24), 0);
	assert_int_equal(read_le32(info + 12), geometry.flash_size);
	assert_int_equal(read_le32(info + 20), 13);
	write_le32(info, 2);
	assert_int_equal(ass_native_capsule_info(memory.owner, &geometry, info, 28), 0);
	assert_int_equal(read_le32(info + 12), 0);
	assert_int_equal(read_le32(info + 16), geometry.flash_size);
	assert_int_equal(read_le32(info + 24), 13);
	assert_int_equal(ass_native_capsule_info(memory.owner, &geometry, info, 24), 0);
	assert_int_equal(read_le32(info + 12), 0x80000002U);
	floor_record[1] = 2;
	assert_int_equal(authvar_owner_set(memory.owner, state, guid, nv, sizeof(floor_record),
		floor_record, false, NULL, NULL), 0);
	assert_int_equal(ass_native_capsule_floor(memory.owner, guid, 7, &floor),
		PAYLOAD_MM_AUTHVAR_STATUS_VOLUME_CORRUPTED);
	assert_int_equal(floor, 13);
	write_le32(info, 2);
	assert_int_equal(ass_native_capsule_info(memory.owner, &geometry, info, 28), 0);
	assert_int_equal(read_le32(info + 12), 0x8000000aU);
}

static void native_smm_opens_before_publication(void **unused)
{
	static _Alignas(16) uint8_t arena[4 * 1024 * 1024];
	static _Alignas(16) uint8_t shared[EFI_VARIABLE_SERVICE_BUFFER_SIZE];
	static struct test_nor nor;
	struct efi_variable_service_buffer *header = (void *)shared;
	struct mm_communication_header32 *message =
		(void *)(shared + EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE);
	const EFI_GUID events[] = {
		ASS_END_OF_DXE_GUID, ASS_READY_TO_LOCK_GUID, ASS_READY_TO_BOOT_GUID,
		ASS_EXIT_BOOT_SERVICES_GUID,
	};
	bool allowed = false;
	bool restart = true;

	memset(nor.bytes, 0xff, NOR_SIZE);
	region_device_init(&nor.region, &nor_ops, 0, NOR_SIZE);
	*header = (struct efi_variable_service_buffer) {
		.signature = EFI_VARIABLE_SERVICE_BUFFER_SIGNATURE,
		.revision = EFI_VARIABLE_SERVICE_REVISION,
		.header_size = EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE,
		.state = EFI_VARIABLE_SERVICE_BUFFER_REQUEST,
		.transaction = 1, .message_size = sizeof(*message) + 1,
	};
	message->guid = (EFI_GUID)ASS_SERVICE_READY_GUID;
	message->message_length = 1;
	assert_false(ass_native_smm_boot_access(arena, &allowed));
	assert_int_equal(ass_native_smm_communicate(arena, shared, sizeof(shared), NULL),
		EFI_VARIABLE_SERVICE_RET_FAILURE);
	allow_program = false;
	assert_int_equal(ass_native_smm_open(arena, sizeof(arena), &nor.region, NOR_BLOCK,
		1024, nor_write_enabled, true, NULL, &restart), PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert_false(restart);
	assert_int_equal(ass_native_smm_communicate(arena, shared, sizeof(shared), NULL),
		EFI_VARIABLE_SERVICE_RET_FAILURE);
	allow_program = true;
	const struct authvar_fixed_policy fixed = { 0 };
	assert_int_equal(ass_native_smm_open(arena, sizeof(arena), &nor.region, NOR_BLOCK,
		1024, nor_write_enabled, true, &fixed, &restart),
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert_false(restart);
	assert_int_equal(ass_native_smm_communicate(arena, shared, sizeof(shared), NULL),
		EFI_VARIABLE_SERVICE_RET_FAILURE);
	assert_int_equal(ass_native_smm_open(arena, sizeof(arena), &nor.region, NOR_BLOCK,
		1024, nor_write_enabled, true, NULL, &restart), 0);
	assert_false(restart);
	assert_int_equal(ass_native_smm_communicate(arena, shared, sizeof(shared), NULL),
		EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(header->status, 0);
	assert_int_equal(header->state, EFI_VARIABLE_SERVICE_BUFFER_RESPONSE);
	assert_true(ass_native_smm_boot_access(arena, &allowed));
	assert_true(allowed);
	const EFI_GUID fixed_guid = {
		0xcc4b3ec3, 0x6cce, 0x4a6a, { 0xa6, 0x73, 0xc4, 0xb4, 0x4f, 0x74, 0x45, 0x61 }
	};
	message->guid = fixed_guid;
	message->message_length = 12;
	memset(message->data, 0, message->message_length);
	header->message_size = sizeof(*message) + message->message_length;
	header->state = EFI_VARIABLE_SERVICE_BUFFER_REQUEST;
	header->transaction++;
	assert_int_equal(ass_native_smm_communicate(arena, shared, sizeof(shared), NULL),
		EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(header->status, 0);
	assert_int_equal(read_le32(message->data + 4), 0x80000003U);
	assert_int_equal(read_le32(message->data + 8), 0);
	message->message_length = 1;
	header->message_size = sizeof(*message) + 1;
	assert_false(ass_native_smm_resume(arena, sizeof(arena), &nor.region,
		NOR_BLOCK, nor_write_enabled, NULL));
	for (size_t i = 0; i < ARRAY_SIZE(events); i++) {
		message->guid = events[i];
		header->state = EFI_VARIABLE_SERVICE_BUFFER_REQUEST;
		header->transaction++;
		assert_int_equal(ass_native_smm_communicate(arena, shared, sizeof(shared), NULL),
			EFI_VARIABLE_SERVICE_RET_SUCCESS);
		assert_int_equal(header->status, 0);
	}
	assert_true(ass_native_smm_boot_access(arena, &allowed));
	assert_false(allowed);
	assert_false(ass_native_smm_resume(arena, sizeof(arena), &nor.region,
		NOR_BLOCK * 2, nor_write_enabled, NULL));
	const struct authvar_fixed_policy different = { 0 };
	assert_false(ass_native_smm_resume(arena, sizeof(arena), &nor.region,
		NOR_BLOCK, nor_write_enabled, &different));
	assert_true(ass_native_smm_resume(arena, sizeof(arena), &nor.region,
		NOR_BLOCK, nor_write_enabled, NULL));
	assert_true(ass_native_smm_boot_access(arena, &allowed));
	assert_false(allowed);
}

static void lite_ass_private_data_budget(void **unused)
{
	const size_t region_size = 0x80000;
	const size_t reserve = 0x400000;
	const uint32_t geometries[] = { 0x1000, 0x10000 };

	for (size_t i = 0; i < ARRAY_SIZE(geometries); i++) {
		struct payload_mm_authvar_fv_geometry geometry;
		const uint32_t block = geometries[i];

		assert_true(payload_mm_authvar_fv_geometry(&geometry, region_size, block));
		const size_t workspace = authvar_owner_workspace_size(geometry.variable_size -
			PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE);
		assert_true(workspace > 0);
		/* Private owner data only; the firmware linker must check the final layout. */
		const size_t data = sizeof(struct authvar_owner) +
			sizeof(struct authvar_policy) + sizeof(struct authvar_media) +
			sizeof(struct ass_native_media_binding) +
			workspace + 2 * region_size + 3 * block +
			EFI_VARIABLE_SERVICE_MESSAGE_CAPACITY;
		assert_true(data <= reserve);
	}
}

static void malformed_capsule_never_authorizes_source(void **unused)
{
	static struct ass_capsule_source source;
	static struct payload_mm_crypto_owner crypto;
	const uint8_t signed_bytes[] = { 1, 2, 3, 4 };
	const uint8_t invalid_der[] = { 0x30, 0x00 };
	const struct payload_mm_crypto_span invalid = {
		.data = invalid_der, .size = sizeof(invalid_der),
	};
	uint8_t output[sizeof(signed_bytes)] = { 0 };

	assert_true(ass_capsule_source_verify(&source, &crypto, signed_bytes,
		sizeof(signed_bytes), &invalid, &invalid) != PAYLOAD_MM_VERIFY_OK);
	assert_false(ass_capsule_source_read(&source, 0, output, sizeof(output)));
	assert_memory_equal(output, (uint8_t[sizeof(output)]) { 0 }, sizeof(output));
}

static unsigned int program_calls;
static unsigned int trial_commits;
static bool reject_preflight;
static const void *mapped_capsule;

static uint32_t record_trial_commit(uint32_t slot)
{
	assert_int_equal(slot, 2);
	trial_commits++;
	return 0;
}

static uint32_t record_layout_preflight(uint32_t offset, const struct efi_capsule_range *ranges,
	uint32_t count, uint8_t *image, uint32_t preflight)
{
	assert_int_equal(offset, 0);
	assert_int_equal(count, 1);
	assert_int_equal(ranges[0].offset, 4096);
	assert_int_equal(ranges[0].size, 15 * 4096);
	assert_true(preflight);
	program_calls++;
	return 0;
}

static const void *map_test_capsule(uint64_t address, size_t size)
{
	return ass_capsule_source_in_ram(address, size, 0x100000,
		0x100000 + ASS_CAPSULE_SOURCE_BLOCK + 64, 0x200000, 4096) ?
		mapped_capsule : NULL;
}

static uint32_t record_capsule_program(uint32_t offset, const struct efi_capsule_range *ranges,
	uint32_t count, uint8_t *image, uint32_t preflight)
{
	assert_int_equal(offset, 0);
	assert_int_equal(count, 1);
	assert_int_equal(ranges[0].offset, 1024);
	assert_int_equal(ranges[0].size, 512);
	assert_memory_equal(image, "__FMAP__", 8);
	assert_int_equal(preflight, program_calls == 0);
	program_calls++;
	return reject_preflight && preflight;
}

static void signed_capsule_source_is_rechecked(void **unused)
{
	static struct ass_capsule_source source;
	static struct payload_mm_crypto_owner crypto;
	static uint8_t body[ASS_CAPSULE_SOURCE_BLOCK + 64] = {
		'M', 'S', 'S', '1', 32, 0, 0, 0, 42, 0, 0, 0, 30, 0, 0, 0, 0x47, 0xa9,
		[32] = '_', '_', 'F', 'M', 'A', 'P', '_', '_', 1, 1,
		[52] = 1, /* Flash size: 64 KiB. */
		[54] = 'F', 'L', 'A', 'S', 'H',
		[86] = 2,
		[88] = 0, 4, 0, 0, 0, 2, 0, 0, 'C', 'O', 'R', 'E', 'B', 'O', 'O', 'T',
		[130] = 0, 6, 0, 0, 0, 2, 0, 0, 'S', 'M', 'M', 'S', 'T', 'O', 'R', 'E',
		[ASS_CAPSULE_SOURCE_BLOCK + 32] = 'C', 'O', 'R', 'E', 'B', 'O', 'O', 'T',
		[ASS_CAPSULE_SOURCE_BLOCK + 48] = 'R', 'M', 'A', 'P', 1, 0, 1, 0,
	};
	const struct payload_mm_crypto_span cms = {
		.data = ass_fixture_signature, .size = sizeof(ass_fixture_signature),
	};
	const struct payload_mm_crypto_span root = {
		.data = ass_fixture_cert, .size = sizeof(ass_fixture_cert),
	};
	uint8_t output[5];
	struct payload_mm_cms_verified_signer signer;
	const struct payload_mm_crypto_span content = { .data = body, .size = sizeof(body) };

	assert_int_equal(payload_mm_cms_verify_detached_untrusted(&crypto, &cms, &content, 1,
		&signer), PAYLOAD_MM_VERIFY_OK);
	assert_int_equal(payload_mm_authvar_trust_anchor_verify(&crypto, &cms, &signer, &root),
		PAYLOAD_MM_VERIFY_OK);
	assert_int_equal(ass_capsule_source_verify(&source, &crypto, body, sizeof(body),
		&cms, &root), PAYLOAD_MM_VERIFY_OK);
	struct ass_capsule_geometry geometry = {
		.image_guid = { 0x47, 0xa9 },
		.flash_size = ASS_CAPSULE_SOURCE_BLOCK,
		.block_size = ASS_CAPSULE_SOURCE_BLOCK,
		.compiled_floor = 30,
	};
	assert_int_equal(ass_native_capsule_target(&source, &geometry, 32, 42), 0);
	const char allowed[][16] = { "EC", "COREBOOT" };
	uint32_t selected = 0;
	assert_int_equal(ass_native_capsule_manifest(&source, &geometry, allowed,
		ARRAY_SIZE(allowed), &selected), 0);
	assert_int_equal(selected, 2);
	assert_int_equal(ass_native_capsule_manifest(&source, &geometry, allowed, 1, &selected),
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert_int_equal(selected, 2);
	const struct ass_capsule_range ranges[] = { { 512, 512 }, { 1024, 512 } };
	static uint8_t image[ASS_CAPSULE_SOURCE_BLOCK];
	struct ass_capsule_transaction transaction = { 0 };
	struct ass_capsule_plan *plan = &transaction.plan;
	const struct ass_capsule_range fmap = { 0, 512 };
	const struct ass_capsule_range store = { 1536, 512 };
	const struct ass_capsule_range moved[] = { { 512, 512 }, { 2048, 512 } };
	const struct ass_capsule_range bios = { 4096, 15 * 4096 };
	struct ass_capsule_plan fallback = { 0 };

	assert_int_equal(ass_native_capsule_begin(&fallback, &source, &geometry, fmap, store,
		bios, allowed, moved, ARRAY_SIZE(moved), 42, image, record_layout_preflight, 0), 0);
	assert_int_equal(program_calls, 1);
	assert_int_equal(fallback.ranges[0].offset, bios.offset);
	program_calls = 0;
	assert_int_equal(ass_native_capsule_begin(&fallback, &source, &geometry, fmap, store,
		bios, allowed, moved, 1, 42, image, record_layout_preflight, 0),
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert_int_equal(program_calls, 0);

	assert_int_equal(ass_native_capsule_begin(plan, &source, &geometry, fmap, store, (struct ass_capsule_range) { 0 },
		allowed, ranges, ARRAY_SIZE(allowed), 43, image, record_capsule_program, 0),
		PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 25U);
	assert_int_equal(program_calls, 0);
	reject_preflight = true;
	assert_int_equal(ass_native_capsule_begin(plan, &source, &geometry, fmap, store, (struct ass_capsule_range) { 0 },
		allowed, ranges, ARRAY_SIZE(allowed), 42, image, record_capsule_program, 0),
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert_int_equal(program_calls, 1);
	assert_int_equal(ass_native_capsule_activate(&transaction), 0);
	program_calls = 0;
	reject_preflight = false;
	assert_int_equal(ass_native_capsule_begin(plan, &source, &geometry, fmap, store, (struct ass_capsule_range) { 0 },
		allowed, ranges, ARRAY_SIZE(allowed), 42, image, record_capsule_program, 0), 0);
	assert_int_equal(program_calls, 1);
	assert_int_equal(ass_native_capsule_activate(&transaction), 1);
	uint32_t program[] = { 2, ASS_CAPSULE_PROGRAM, 2, 0, 0 };
	assert_int_equal(ass_native_capsule_continue(&transaction, &source, program,
		sizeof(program), image, record_capsule_program, NULL), 0);
	assert_int_equal(program[3], 0x8000000f);
	program[2] = 1;
	program[4] = 1;
	assert_int_equal(ass_native_capsule_continue(&transaction, &source, program,
		sizeof(program), image, record_capsule_program, NULL), 0);
	assert_int_equal(program[3], 0x8000000f);
	program[4] = 0;
	assert_int_equal(ass_native_capsule_continue(&transaction, &source, program,
		sizeof(program), image, record_capsule_program, NULL), 0);
	assert_int_equal(program[3], 0);
	assert_int_equal(ass_native_capsule_continue(&transaction, &source, program,
		sizeof(program), image, record_capsule_program, NULL), 0);
	assert_int_equal(program[3], 0x8000000f);
	assert_int_equal(program_calls, 2);
	selected = 0;
	assert_int_equal(ass_native_capsule_select_ranges(&source, &geometry,
		(struct ass_capsule_range) { 0, 512 }, (struct ass_capsule_range) { 1536, 512 },
		allowed, ranges, ARRAY_SIZE(allowed), &selected), 0);
	assert_int_equal(selected, 2);
	assert_int_equal(ass_native_capsule_select_ranges(&source, &geometry,
		(struct ass_capsule_range) { 0, 512 }, (struct ass_capsule_range) { 2048, 512 },
		allowed, ranges, ARRAY_SIZE(allowed), &selected),
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert_int_equal(selected, 2);
	selected = 0;
	assert_int_equal(ass_native_capsule_select_ranges(&source, &geometry,
		(struct ass_capsule_range) { 512, 512 }, (struct ass_capsule_range) { 1536, 512 },
		allowed, ranges, ARRAY_SIZE(allowed), &selected),
		PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA);
	assert_int_equal(selected, 0);
	assert_int_equal(ass_native_capsule_target(&source, &geometry, 16, 42),
		PAYLOAD_MM_AUTHVAR_STATUS_COMPROMISED_DATA);
	assert_int_equal(ass_native_capsule_target(&source, &geometry, 32, 43),
		PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 25U);
	geometry.image_guid[0] ^= 1;
	assert_int_equal(ass_native_capsule_target(&source, &geometry, 32, 42),
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	memset(output, 0xff, sizeof(output));
	assert_true(ass_capsule_source_read(&source, ASS_CAPSULE_SOURCE_BLOCK - 2,
		output, sizeof(output)));
	assert_memory_equal(output, (uint8_t[sizeof(output)]) { 0 }, sizeof(output));
	body[ASS_CAPSULE_SOURCE_BLOCK] = 1;
	memset(output, 0xff, sizeof(output));
	assert_false(ass_capsule_source_read(&source, ASS_CAPSULE_SOURCE_BLOCK, output, 1));
	assert_int_equal(output[0], 0xff);
	assert_false(ass_capsule_source_read(&source, 0, output, 1));
	assert_true(ass_capsule_source_verify(&source, &crypto, body, sizeof(body),
		&cms, &root) != PAYLOAD_MM_VERIFY_OK);
	uint32_t close[] = { 1, ASS_CAPSULE_CLOSE, 1 };
	assert_int_equal(ass_native_capsule_continue(&transaction, &source, close,
		sizeof(close), image, record_capsule_program, NULL), 0);
	assert_int_equal(ass_native_capsule_continue(&transaction, &source, close,
		sizeof(close), image, record_capsule_program, NULL), 0x8000000f);

	static struct {
		struct authvar_owner owner;
		_Alignas(16) uint8_t workspace[128 * 1024];
	} service;
	static uint8_t erased[3 * 4096], volume[sizeof(erased)];
	struct test_media media = {
		.store = volume + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE,
		.size = 4096 - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE,
	};
	memset(erased, 0xff, sizeof(erased));
	assert_int_equal(payload_mm_authvar_default_store_compose(erased, volume,
		sizeof(volume), 4096), PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
	struct authvar_owner *owner = &service.owner;
	assert_int_equal(authvar_owner_init(owner, media.store, media.size, 1024,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	body[ASS_CAPSULE_SOURCE_BLOCK] = 0;
	geometry.image_guid[0] ^= 1;
	mapped_capsule = body;
	const struct ass_capsule_platform platform = {
		.geometry = geometry, .fmap = fmap, .store = store,
		.names = allowed, .ranges = ranges, .count = ARRAY_SIZE(allowed),
		.root = &root, .map_source = map_test_capsule, .program = record_capsule_program,
		.commit = record_trial_commit,
	};
	const struct ass_mm_capsule capsule = {
		.transaction = &transaction, .source = &source, .crypto = &crypto,
		.platform = &platform, .image = image,
	};
	static struct authvar_policy policy;
	enum ass_mm_phase phase = ASS_MM_WAIT_END_OF_DXE;
	const uint8_t capsule_guid[] = ASS_CAPSULE_GUID_BYTES;
	uint8_t info[sizeof(struct mm_communication_header32) + 28] = { 0 };
	struct mm_communication_header32 *wire = (void *)info;
	memcpy(&wire->guid, capsule_guid, sizeof(capsule_guid));
	wire->message_length = 28;
	write_le32(wire->data, 2);
	write_le32(wire->data + 4, ASS_CAPSULE_INFO);
	assert_int_equal(ass_native_mm_dispatch(owner, &policy, NULL, &phase, NULL,
		info, sizeof(info)), 0x80000003);
	assert_int_equal(ass_native_mm_dispatch(owner, &policy, NULL, &phase, &capsule,
		info, sizeof(info)), 0);
	assert_int_equal(read_le32(wire->data + 16), sizeof(image));
	const uint16_t lsv[] = { 'F', 'm', 'p', 'L', 's', 'v', 0 };
	uint32_t version = 35;
	const uint32_t nv = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	assert_int_equal(authvar_owner_set(owner, lsv, geometry.image_guid, nv,
		sizeof(version), &version, false, NULL, NULL), 0);
	assert_int_equal(ass_native_mm_dispatch(owner, &policy, NULL, &phase, &capsule,
		info, sizeof(info)), 0);
	assert_int_equal(read_le32(wire->data + 24), 35);
	version = 31;
	assert_int_equal(authvar_owner_set(owner, lsv, geometry.image_guid, nv,
		sizeof(version), &version, false, NULL, NULL), 0);
	assert_int_equal(ass_native_mm_dispatch(owner, &policy, NULL, &phase, &capsule,
		info, sizeof(info)), 0);
	assert_int_equal(read_le32(wire->data + 24), 35);
	uint8_t begin[36 + sizeof(ass_fixture_signature)] = { 0 };
	size_t reply_size = sizeof(begin);
	write_le32(begin, 2);
	write_le32(begin + 4, ASS_CAPSULE_BEGIN);
	write_le32(begin + 24, sizeof(body));
	write_le32(begin + 28, 32);
	write_le32(begin + 32, sizeof(ass_fixture_signature));
	memcpy(begin + 36, ass_fixture_signature, sizeof(ass_fixture_signature));
	program_calls = 0;
	assert_int_equal(ass_native_capsule_dispatch(&transaction, &source, owner, &crypto,
		&platform, begin, sizeof(begin), image, &reply_size), 0);
	assert_int_equal(read_le32(begin + 12), 0x8000000f);
	assert_int_equal(read_le32(begin + 8), 0);
	assert_int_equal(program_calls, 0);
	assert_false(source.authorized);
	write_le64(begin + 16, 0x100000);
	static _Alignas(8) uint8_t shared[EFI_VARIABLE_SERVICE_BUFFER_SIZE];
	_Alignas(8) uint8_t scratch[sizeof(*wire) + sizeof(begin)];
	struct efi_variable_service_buffer *mailbox = (void *)shared;
	struct mm_communication_header32 *reply =
		(void *)(shared + EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE);
	*mailbox = (struct efi_variable_service_buffer) {
		.signature = EFI_VARIABLE_SERVICE_BUFFER_SIGNATURE,
		.revision = EFI_VARIABLE_SERVICE_REVISION,
		.header_size = EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE,
		.state = EFI_VARIABLE_SERVICE_BUFFER_REQUEST,
		.transaction = 7,
		.message_size = sizeof(scratch),
	};
	memcpy(&reply->guid, &wire->guid, sizeof(reply->guid));
	reply->message_length = sizeof(begin);
	memcpy(reply->data, begin, sizeof(begin));
	assert_int_equal(ass_native_mailbox_dispatch(owner, &policy, NULL, &phase, &capsule,
		shared, sizeof(shared), scratch, sizeof(scratch)), EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(mailbox->status, 0);
	assert_int_equal(mailbox->response_size, sizeof(*reply) + 36);
	assert_int_equal(reply->message_length, 36);
	memcpy(begin, reply->data, 36);
	assert_int_equal(read_le32(begin + 12), 0);
	assert_int_equal(read_le32(begin + 8), 2);
	assert_int_equal(program_calls, 1);
	/* Model the commit boundary after an admitted inactive-slot plan. */
	transaction.plan.trial_slot = 2;
	transaction.plan.blocks = 2;
	program[2] = 2;
	assert_int_equal(ass_native_capsule_dispatch(&transaction, &source, owner, &crypto,
		&platform, program, sizeof(program), image, &reply_size), 0);
	assert_int_equal(program[3], 0);
	assert_int_equal(program_calls, 2);
	assert_int_equal(trial_commits, 0);
	program[4] = 1;
	assert_int_equal(ass_native_capsule_dispatch(&transaction, &source, owner, &crypto,
		&platform, program, sizeof(program), image, &reply_size), 0);
	assert_int_equal(program[3], 0);
	assert_int_equal(trial_commits, 1);
	assert_int_equal(ass_native_capsule_dispatch(&transaction, &source, owner, &crypto,
		&platform, program, sizeof(program), image, &reply_size), 0);
	assert_int_equal(program[3], 0x8000000f);
	assert_int_equal(trial_commits, 1);
	program[4] = 0;
	close[2] = 2;
	assert_int_equal(ass_native_capsule_dispatch(&transaction, &source, owner, &crypto,
		&platform, close, sizeof(close), image, &reply_size), 0);
	assert_false(source.authorized);
	assert_int_equal(transaction.active, 0);

	write_le32(begin + 8, 0);
	program_calls = 0;
	assert_int_equal(ass_native_capsule_dispatch(&transaction, &source, owner, &crypto,
		&platform, begin, sizeof(begin), image, &reply_size), 0);
	assert_int_equal(read_le32(begin + 12), 0);
	program[2] = read_le32(begin + 8);
	body[32 + ranges[1].offset] ^= 1;
	assert_int_equal(ass_native_capsule_dispatch(&transaction, &source, owner, &crypto,
		&platform, program, sizeof(program), image, &reply_size), 0);
	assert_int_equal(program[3], 0x8000001a);
	assert_false(source.authorized);
	assert_int_equal(program_calls, 1);
	body[32 + ranges[1].offset] ^= 1;
	close[2] = program[2];
	assert_int_equal(ass_native_capsule_dispatch(&transaction, &source, owner, &crypto,
		&platform, close, sizeof(close), image, &reply_size), 0);
	write_le32(begin + 8, 0);
	program_calls = 0;
	/* Revision 1 lacks the separate result field. Its signature is unchanged. */
	write_le32(begin, 1);
	memmove(begin + 12, begin + 16, sizeof(begin) - 16);
	mailbox->state = EFI_VARIABLE_SERVICE_BUFFER_REQUEST;
	mailbox->transaction++;
	reply->message_length = sizeof(begin) - sizeof(uint32_t);
	mailbox->message_size = sizeof(*reply) + reply->message_length;
	memcpy(reply->data, begin, reply->message_length);
	assert_int_equal(ass_native_mailbox_dispatch(owner, &policy, NULL, &phase, &capsule,
		shared, sizeof(shared), scratch, sizeof(scratch)), EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(mailbox->status, 0);
	assert_int_equal(mailbox->response_size, sizeof(*reply) + 32);
	assert_int_equal(reply->message_length, 32);
	assert_true(source.authorized);
	const EFI_GUID events[] = {
		ASS_END_OF_DXE_GUID, ASS_READY_TO_LOCK_GUID, ASS_READY_TO_BOOT_GUID,
		ASS_EXIT_BOOT_SERVICES_GUID,
	};
	for (size_t i = 0; i < ARRAY_SIZE(events); i++) {
		wire->guid = events[i];
		wire->message_length = 1;
		wire->data[0] = 0;
		assert_int_equal(ass_native_mm_dispatch(owner, &policy, NULL, &phase, &capsule,
			info, sizeof(*wire) + 1), 0);
	}
	assert_false(source.authorized);
	assert_int_equal(transaction.active, 0);
	assert_int_equal(program_calls, 1);
}

static uint32_t record_bios_program(uint32_t offset, const struct efi_capsule_range *ranges,
	uint32_t count, uint8_t *image, uint32_t preflight)
{
	assert_int_equal(offset, 0);
	assert_int_equal(count, 1);
	assert_int_equal(ranges[0].offset, 4096);
	assert_int_equal(ranges[0].size, 15 * 4096);
	assert_int_equal(read_le32(image + 0x10), 0x0ff0a55a);
	assert_int_equal(preflight, program_calls == 0);
	program_calls++;
	return 0;
}

static void signed_capsule_bios_fallback(void **unused)
{
	static const uint8_t body[ASS_CAPSULE_SOURCE_BLOCK + 40] = {
		'M', 'S', 'S', '1', 32, 0, 0, 0, 42, 0, 0, 0, 30, 0, 0, 0, 0x47, 0xa9,
		[48] = 0x5a, 0xa5, 0xf0, 0x0f, 0, 0, 4, 0,
		[100] = 1, 0, 15, 0,
		[160] = '_', '_', 'F', 'M', 'A', 'P', '_', '_', 1, 1,
		[180] = 1, /* Flash size: 64 KiB. */
		[182] = 'F', 'L', 'A', 'S', 'H',
		[214] = 1,
		[216] = 0, 32, 0, 0, 0, 16, 0, 0, 'S', 'M', 'M', 'S', 'T', 'O', 'R', 'E',
	};
	static struct ass_capsule_source source;
	static struct payload_mm_crypto_owner crypto;
	static uint8_t image[ASS_CAPSULE_SOURCE_BLOCK];
	struct ass_capsule_plan plan = { 0 };
	const struct ass_capsule_geometry geometry = {
		.image_guid = { 0x47, 0xa9 }, .flash_size = sizeof(image),
		.block_size = sizeof(image), .compiled_floor = 30,
	};
	const struct payload_mm_crypto_span cms = {
		.data = ass_bios_signature, .size = sizeof(ass_bios_signature),
	};
	const struct payload_mm_crypto_span root = {
		.data = ass_bios_cert, .size = sizeof(ass_bios_cert),
	};
	const char names[][16] = { "COREBOOT" };
	const struct ass_capsule_range ranges[] = { { 4096, 4096 } };
	/* The signed candidate's map is at 128, independently of the running map. */
	const struct ass_capsule_range fmap = { 64, 512 }, store = { 8192, 4096 };

	assert_int_equal(ass_capsule_source_verify(&source, &crypto, body, sizeof(body),
		&cms, &root), PAYLOAD_MM_VERIFY_OK);
	program_calls = 0;
	assert_int_equal(ass_native_capsule_begin(&plan, &source, &geometry, fmap, store,
		(struct ass_capsule_range) { 4096, 14 * 4096 }, names, ranges, ARRAY_SIZE(ranges),
		42, image, record_bios_program, 0), PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 25U);
	assert_int_equal(program_calls, 0);
	assert_int_equal(plan.blocks, 0);
	assert_int_equal(ass_native_capsule_begin(&plan, &source, &geometry, fmap, store,
		(struct ass_capsule_range) { 4096, 15 * 4096 }, names, ranges, ARRAY_SIZE(ranges),
		42, image, record_bios_program, 1), PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT | 25U);
	assert_int_equal(program_calls, 0);
	assert_int_equal(ass_native_capsule_begin(&plan, &source, &geometry, fmap, store,
		(struct ass_capsule_range) { 4096, 15 * 4096 }, names, ranges, ARRAY_SIZE(ranges),
		42, image, record_bios_program, 0), 0);
	assert_int_equal(ass_native_capsule_program(&plan, &source, 0, image, record_bios_program), 0);
	assert_int_equal(program_calls, 2);
	ass_capsule_source_close(&source);
}

static uint32_t expected_slot_block;

static uint32_t record_slot_program(uint32_t offset, const struct efi_capsule_range *ranges,
	uint32_t count, uint8_t *image, uint32_t preflight)
{
	assert_int_equal(offset, expected_slot_block);
	assert_int_equal(count, 3);
	assert_int_equal(preflight, program_calls == 0);
	for (size_t i = 0; i < count; i++) {
		assert_int_equal(ranges[i].offset, offset + (i + 1) * 4096);
		assert_int_equal(ranges[i].size, 4096);
	}
	program_calls++;
	return 0;
}

static void signed_capsule_selects_inactive_slot(void **unused)
{
	const char names[][16] = {
		"FW_MAIN_A", "RW_FWID_A", "VBLOCK_A", "FW_MAIN_B", "RW_FWID_B", "VBLOCK_B",
	};
	const struct ass_capsule_range ranges[] = {
		{ 4096, 4096 }, { 8192, 4096 }, { 12288, 4096 },
		{ 69632, 4096 }, { 73728, 4096 }, { 77824, 4096 },
	};
	const struct ass_capsule_range fmap = { 0, 512 }, store = { 24576, 4096 };
	static uint8_t body[2 * ASS_CAPSULE_SOURCE_BLOCK + 144] = {
		'M', 'S', 'S', '1', 32, 0, 0, 0, 42, 0, 0, 0, 30, 0, 0, 0, 0x47, 0xa9,
	};
	static struct ass_capsule_source source;
	static struct payload_mm_crypto_owner crypto;
	static uint8_t image[ASS_CAPSULE_SOURCE_BLOCK];
	const struct ass_capsule_geometry geometry = {
		.image_guid = { 0x47, 0xa9 }, .flash_size = 2 * sizeof(image),
		.block_size = sizeof(image), .compiled_floor = 30,
	};
	const struct payload_mm_crypto_span cms = {
		.data = ass_ab_signature, .size = sizeof(ass_ab_signature),
	};
	const struct payload_mm_crypto_span root = {
		.data = ass_ab_cert, .size = sizeof(ass_ab_cert),
	};
	struct fmap *map = (void *)(body + 32);
	uint8_t *manifest = body + 32 + geometry.flash_size;

	memcpy(map->signature, FMAP_SIGNATURE, sizeof(map->signature));
	map->ver_major = FMAP_VER_MAJOR;
	map->ver_minor = FMAP_VER_MINOR;
	write_le32(&map->size, geometry.flash_size);
	memcpy(map->name, "FLASH", 5);
	write_le16(&map->nareas, ARRAY_SIZE(ranges) + 1);
	for (size_t i = 0; i < ARRAY_SIZE(ranges); i++) {
		write_le32(&map->areas[i].offset, ranges[i].offset);
		write_le32(&map->areas[i].size, ranges[i].size);
		memcpy(map->areas[i].name, names[i], sizeof(names[i]));
		memcpy(manifest + i * sizeof(names[i]), names[i], sizeof(names[i]));
	}
	struct fmap_area *last = &map->areas[ARRAY_SIZE(ranges)];
	write_le32(&last->offset, store.offset);
	write_le32(&last->size, store.size);
	memcpy(last->name, "SMMSTORE", 8);
	uint8_t *footer = manifest + sizeof(names);
	memcpy(footer, "RMAP", 4);
	write_le16(footer + 4, 1);
	write_le16(footer + 6, ARRAY_SIZE(ranges));
	assert_int_equal(ass_capsule_source_verify(&source, &crypto, body, sizeof(body),
		&cms, &root), PAYLOAD_MM_VERIFY_OK);

	for (uint32_t running = 1; running <= 3; running++) {
		struct ass_capsule_plan plan = { 0 };
		const uint32_t target = running == 1 ? 2 : 1;

		expected_slot_block = (target - 1) * geometry.block_size;
		program_calls = 0;
		assert_int_equal(ass_native_capsule_begin(&plan, &source, &geometry, fmap, store,
			(struct ass_capsule_range) { 0 }, names, ranges, ARRAY_SIZE(ranges),
			42, image, record_slot_program, running), 0);
		assert_int_equal(plan.trial_slot, target);
		assert_int_equal(program_calls, 1);
		for (uint32_t block = 0; block < 2; block++)
			assert_int_equal(ass_native_capsule_program(&plan, &source, block, image,
				record_slot_program), 0);
		assert_int_equal(program_calls, 2);
	}
	ass_capsule_source_close(&source);
}

static void capsule_ram_excludes_mailbox(void **unused)
{
	assert_true(ass_capsule_source_in_ram(0x100000, 0x1000,
		0x100000, 0x104000, 0x101000, 0x1000));
	assert_true(ass_capsule_source_in_ram(0x102000, 0x2000,
		0x100000, 0x104000, 0x101000, 0x1000));
	assert_false(ass_capsule_source_in_ram(0x100000, 0x1001,
		0x100000, 0x104000, 0x101000, 0x1000));
	assert_false(ass_capsule_source_in_ram(0x101fff, 0x1000,
		0x100000, 0x104000, 0x101000, 0x1000));
	assert_false(ass_capsule_source_in_ram(0x102000, 0x2001,
		0x100000, 0x104000, 0x101000, 0x1000));
	assert_false(ass_capsule_source_in_ram(UINT64_MAX, 2,
		0x100000, 0x104000, 0x101000, 0x1000));
}

static void capsule_wire_uses_revision_header(void **unused)
{
	uint8_t wire[28] = { 0 };
	uint8_t *message = wire + 1; /* A packed MM message need not be word aligned. */
	struct ass_capsule_wire_request request;
	uint8_t *payload;
	size_t size;

	message[0] = 1;
	message[4] = ASS_CAPSULE_PROGRAM;
	message[8] = 0x42;
	assert_true(ass_capsule_wire_payload(message, 16, &request, &payload, &size));
	assert_int_equal(request.command, ASS_CAPSULE_PROGRAM);
	assert_int_equal(request.transaction, 0x42);
	assert_ptr_equal(payload, message + 12);
	assert_int_equal(size, 4);
	message[0] = 2;
	assert_false(ass_capsule_wire_payload(message, 15, &request, &payload, &size));
	assert_true(ass_capsule_wire_payload(message, 20, &request, &payload, &size));
	assert_ptr_equal(payload, message + 16);
	assert_int_equal(size, 4);
	message[0] = 3;
	assert_false(ass_capsule_wire_payload(message, 20, &request, &payload, &size));
}

static void fixed_certificate_asset_requires_canonical_lists(void **unused)
{
	uint8_t list[45] = {
		0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
		0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72,
	};
	struct authvar_fixed_certificate result[1];

	/* One ESL, no extension header, a 16-byte owner and one data byte. */
	write_le32(list + 16, sizeof(list));
	write_le32(list + 24, 17);
	list[28] = 0x47;
	list[44] = 0x30;
	struct payload_mm_authvar_span asset = { list, sizeof(list) };
	assert_int_equal(authvar_fixed_decode_certificates(asset, result, ARRAY_SIZE(result)), 1);
	assert_ptr_equal(result[0].owner_guid, list + 28);
	assert_ptr_equal(result[0].data.data, list + 44);
	assert_int_equal(result[0].data.size, 1);
	asset.size--;
	assert_int_equal(authvar_fixed_decode_certificates(asset, result, ARRAY_SIZE(result)), 0);
	asset.size++;
	assert_int_equal(authvar_fixed_decode_certificates(asset, result, 0), 0);
	list[0] ^= 1;
	assert_int_equal(authvar_fixed_decode_certificates(asset, result, ARRAY_SIZE(result)), 0);
}

#ifdef ASS_FIXED_ASSET_TEST
static struct mm_communication_header32 *fixed_mailbox_call(void *arena,
	void *shared, EFI_GUID guid, uint32_t command, size_t size)
{
	struct efi_variable_service_buffer *header = shared;
	struct mm_communication_header32 *message =
		(void *)((uint8_t *)shared + EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE);

	*header = (struct efi_variable_service_buffer) {
		.signature = EFI_VARIABLE_SERVICE_BUFFER_SIGNATURE,
		.revision = EFI_VARIABLE_SERVICE_REVISION,
		.header_size = EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE,
		.state = EFI_VARIABLE_SERVICE_BUFFER_REQUEST,
		.transaction = header->transaction + 1,
		.message_size = sizeof(*message) + size,
	};
	message->guid = guid;
	message->message_length = size;
	memset(message->data, 0, size);
	if (size >= sizeof(command))
		write_le32(message->data, command);
	assert_int_equal(ass_native_smm_communicate(arena, shared,
		EFI_VARIABLE_SERVICE_BUFFER_SIZE, NULL), EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(header->status, 0);
	assert_int_equal(header->state, EFI_VARIABLE_SERVICE_BUFFER_RESPONSE);
	assert_int_equal(header->response_size, sizeof(*message) + size);
	return message;
}

static void fixed_factory_store_opens_and_reopens(void **unused)
{
	static _Alignas(16) uint8_t arena[4 * 1024 * 1024];
	static _Alignas(16) uint8_t shared[EFI_VARIABLE_SERVICE_BUFFER_SIZE];
	static struct test_nor nor;
	const struct authvar_fixed_policy *fixed = ass_native_fixed_policy();
	bool restart, allowed;

	assert_non_null(fixed);
	memset(nor.bytes, 0xff, sizeof(nor.bytes));
	region_device_init(&nor.region, &nor_ops, 0, sizeof(nor.bytes));
	allow_program = true;
	assert_int_equal(ass_native_smm_open(arena, sizeof(arena), &nor.region, NOR_BLOCK,
		0x10000, nor_write_enabled, true, fixed, &restart), 0);
	assert_false(restart);
	assert_true(ass_native_smm_boot_access(arena, &allowed));
	assert_true(allowed);
	allow_program = false;
	assert_int_equal(ass_native_smm_open(arena, sizeof(arena), &nor.region, NOR_BLOCK,
		0x10000, nor_write_enabled, true, fixed, &restart), 0);
	assert_false(restart);

	const EFI_GUID fixed_guid = {
		0xcc4b3ec3, 0x6cce, 0x4a6a, { 0xa6, 0x73, 0xc4, 0xb4, 0x4f, 0x74, 0x45, 0x61 }
	};
	const EFI_GUID events[] = {
		ASS_END_OF_DXE_GUID, ASS_READY_TO_LOCK_GUID,
		ASS_READY_TO_BOOT_GUID, ASS_EXIT_BOOT_SERVICES_GUID,
	};
	const struct payload_mm_authvar_span keys[] = {
		fixed->factory_pk, fixed->factory_kek, fixed->factory_db,
	};
	struct mm_communication_header32 *message =
		fixed_mailbox_call(arena, shared, fixed_guid, 1, 12);
	assert_int_equal(read_le32(message->data + 4), 0x80000006U);
	message = fixed_mailbox_call(arena, shared, fixed_guid, 0, 12);
	assert_int_equal(read_le32(message->data + 4), 0);
	message = fixed_mailbox_call(arena, shared, fixed_guid, 1, 12);
	assert_int_equal(read_le32(message->data + 4), 0);
	for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
		message = fixed_mailbox_call(arena, shared, fixed_guid, 2 + i, 12);
		assert_int_equal(read_le32(message->data + 4), 0x80000005U);
		assert_int_equal(read_le32(message->data + 8), keys[i].size);
		message = fixed_mailbox_call(arena, shared, fixed_guid, 2 + i, 12 + keys[i].size);
		assert_int_equal(read_le32(message->data + 4), 0);
		assert_memory_equal(message->data + 12, keys[i].data, keys[i].size);
	}
	for (size_t i = 0; i < ARRAY_SIZE(events); i++)
		fixed_mailbox_call(arena, shared, events[i], 0, 1);
	message = fixed_mailbox_call(arena, shared, fixed_guid, 2, 12 + keys[0].size);
	assert_int_equal(read_le32(message->data + 4), 0x8000000eU);
	message = fixed_mailbox_call(arena, shared, fixed_guid, 5, 12 + 64 * 1024);
	assert_int_equal(read_le32(message->data + 4), 0);
	assert_true(read_le32(message->data + 8) >= fixed->dbx_append.size);
}

static void fixed_dbx_update_requires_restart(void **unused)
{
	static _Alignas(16) uint8_t arena[4 * 1024 * 1024];
	static struct test_nor nor;
	static uint8_t before[LITE_NOR_SIZE], corrupted[64 * 1024];
	const struct authvar_fixed_policy *fixed = ass_native_fixed_policy();
	struct payload_mm_authvar_auth2_view baseline;
	struct authvar_fixed_policy prior;
	bool restart, allowed;

	assert_non_null(fixed);
	assert_int_equal(payload_mm_authvar_auth2_parse(fixed->dbx_baseline.data,
		fixed->dbx_baseline.size, &baseline), PAYLOAD_MM_AUTHVAR_FORMAT_OK);
	assert_true(baseline.payload.size >= 28);
	const size_t first_list = read_le32(baseline.payload.data + 16);
	assert_true(first_list >= 28 && first_list <= baseline.payload.size);
	/* Model a valid older store: the first list adds no new revocations. */
	prior = *fixed;
	prior.dbx_append = (struct payload_mm_authvar_span) {
		baseline.payload.data, first_list,
	};
	memset(nor.bytes, 0xff, sizeof(nor.bytes));
	region_device_init(&nor.region, &nor_ops, 0, sizeof(nor.bytes));
	allow_program = true;
	assert_int_equal(ass_native_smm_open(arena, sizeof(arena), &nor.region, NOR_BLOCK,
		0x10000, nor_write_enabled, true, &prior, &restart), 0);
	assert_false(restart);
	memcpy(before, nor.bytes, sizeof(before));
	assert_true(fixed->dbx_update.size && fixed->dbx_update.size <= sizeof(corrupted));
	memcpy(corrupted, fixed->dbx_update.data, fixed->dbx_update.size);
	corrupted[fixed->dbx_update.size - 1] ^= 1;
	struct authvar_fixed_policy tampered = *fixed;
	tampered.dbx_update.data = corrupted;
	assert_int_equal(ass_native_smm_open(arena, sizeof(arena), &nor.region, NOR_BLOCK,
		0x10000, nor_write_enabled, true, &tampered, &restart),
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert_false(restart);
	assert_false(ass_native_smm_boot_access(arena, &allowed));
	assert_memory_equal(before, nor.bytes, sizeof(before));
	assert_int_equal(ass_native_smm_open(arena, sizeof(arena), &nor.region, NOR_BLOCK,
		0x10000, nor_write_enabled, true, fixed, &restart), 0);
	assert_true(restart);
	assert_false(ass_native_smm_boot_access(arena, &allowed));
	allow_program = false;
	assert_int_equal(ass_native_smm_open(arena, sizeof(arena), &nor.region, NOR_BLOCK,
		0x10000, nor_write_enabled, true, fixed, &restart), 0);
	assert_false(restart);
	assert_true(ass_native_smm_boot_access(arena, &allowed));
	assert_true(allowed);
}
#endif

static void reopen_orphan_certdb(void **unused)
{
	static struct {
		struct authvar_owner owner;
		_Alignas(16) uint8_t workspace[128 * 1024];
	} service;
	static uint8_t erased[3 * 4096], volume[sizeof(erased)];
	uint8_t *store = volume + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	const size_t store_size = 4096 - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	struct test_media media = { .store = store, .size = store_size };
	const uint16_t dbname[] = u"certdb", orphan[] = u"TestOrphan", live[] = u"TestLive";
	const uint8_t dbguid[16] = {
		0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
		0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
	};
	const uint8_t guid[16] = { 0x11, 0x23, 0x45 };
	const uint8_t empty[4] = { 4, 0, 0, 0 }, signer[32] = { 1 };
	uint8_t db[256], retained[256], mixed[256], result[256];
	size_t dbsize, retained_size, mixed_size, result_size = sizeof(result);
	uint32_t encoded;
	memset(erased, 0xff, sizeof(erased));
	assert_int_equal(payload_mm_authvar_default_store_compose(erased, volume,
		sizeof(volume), 4096), PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	assert_int_equal(payload_mm_authvar_certdb_compose(empty, sizeof(empty),
		PAYLOAD_MM_AUTHVAR_CERTDB_ADD, guid, orphan, sizeof(orphan) - 2,
		signer, sizeof(signer), db, sizeof(db), &dbsize), PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert_int_equal(payload_mm_authvar_certdb_compose(empty, sizeof(empty),
		PAYLOAD_MM_AUTHVAR_CERTDB_ADD, guid, live, sizeof(live) - 2,
		signer, sizeof(signer), retained, sizeof(retained), &retained_size),
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	assert_int_equal(payload_mm_authvar_certdb_compose(db, dbsize,
		PAYLOAD_MM_AUTHVAR_CERTDB_ADD, guid, live, sizeof(live) - 2,
		signer, sizeof(signer), mixed, sizeof(mixed), &mixed_size),
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	const struct payload_mm_authvar_store_entry *old = payload_mm_authvar_store_find(
		&service.owner.index, dbguid, dbname, sizeof(dbname));
	assert_non_null(old);
	store[old->record_offset + 2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED;
	struct payload_mm_authvar_record_descriptor record = {
		.name = dbname, .name_size = sizeof(dbname), .attributes = 0x27,
	};
	memcpy(record.vendor_guid, dbguid, sizeof(dbguid));
	const struct payload_mm_authvar_record_span span = { mixed, mixed_size };
	size_t offset = payload_mm_authvar_default_store_records_end();
	assert_true(payload_mm_authvar_record_encode(&record, &span, 1,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
		store + offset, store_size - offset, &encoded));
	store[offset + 2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	offset += encoded;
	record.name = live;
	record.name_size = sizeof(live);
	memcpy(record.vendor_guid, guid, sizeof(guid));
	const struct payload_mm_authvar_record_span data = { signer, sizeof(signer) };
	assert_true(payload_mm_authvar_record_encode(&record, &data, 1,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
		store + offset, store_size - offset, &encoded));
	store[offset + 2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	uint8_t before[4096];
	memcpy(before, store, store_size);
	media.reject = true;
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL),
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert_true(service.owner.failed);
	assert_memory_equal(store, before, store_size);
	media.reject = false;
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	assert_int_equal(authvar_owner_get(&service.owner, dbname, dbguid, NULL,
		&result_size, result), 0);
	assert_int_equal(result_size, retained_size);
	assert_memory_equal(result, retained, retained_size);
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	assert_int_equal(media.writes, 2);
}

static void setup_replacement_preserves_timestamp(void **unused)
{
	static struct {
		struct authvar_owner owner;
		_Alignas(16) uint8_t workspace[128 * 1024];
	} service;
	static uint8_t erased[3 * 4096], volume[sizeof(erased)];
	uint8_t *store = volume + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	const size_t store_size = 4096 - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE;
	struct test_media media = { .store = store, .size = store_size };
	static struct authvar_policy policy;
	const uint16_t name[] = u"db";
	_Alignas(8) uint8_t request[sizeof(struct smm_variable_header32) +
		sizeof(struct smm_variable_access32) + sizeof(name) + 116] = { 0 };
	struct smm_variable_header32 *message = (void *)request;
	struct smm_variable_access32 *access = (void *)message->data;
	uint8_t *auth = (uint8_t *)access->name + sizeof(name);
	const uint8_t pkcs7[] = {
		0x9d, 0xd2, 0xaf, 0x4a, 0xdf, 0x68, 0xee, 0x49,
		0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7,
	};
	const uint8_t sha256[] = {
		0x26, 0x16, 0xc4, 0xc1, 0x4c, 0x50, 0x92, 0x40,
		0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28,
	};
	memset(erased, 0xff, sizeof(erased));
	assert_int_equal(payload_mm_authvar_default_store_compose(erased, volume,
		sizeof(volume), 4096), PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED);
	assert_int_equal(authvar_owner_init(&service.owner, store, store_size, 2048,
		commit, &media, service.workspace, sizeof(service.workspace), true, NULL, NULL), 0);
	message->function = SMM_VARIABLE_FUNCTION_SET_VARIABLE;
	access->guid = (EFI_GUID) {
		0xd719b2cb, 0x3d3a, 0x4596, { 0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f },
	};
	access->attributes = 0x27;
	access->name_size = sizeof(name);
	access->data_size = 116;
	memcpy(access->name, name, sizeof(name));
	write_le16(auth, 2026);
	auth[2] = 11;
	auth[3] = 21;
	write_le32(auth + 16, 24);
	write_le16(auth + 20, 0x200);
	write_le16(auth + 22, 0xef1);
	memcpy(auth + 24, pkcs7, sizeof(pkcs7));
	memcpy(auth + 40, sha256, sizeof(sha256));
	write_le32(auth + 56, 76);
	write_le32(auth + 64, 48);
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	auth[115] = 1;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	write_le16(auth, 1970);
	auth[115] = 2;
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0);
	const struct payload_mm_authvar_store_entry *entry = payload_mm_authvar_store_find(
		&service.owner.index, (const void *)&access->guid, name, sizeof(name));
	assert_non_null(entry);
	assert_int_equal(read_le16(service.owner.index.store + entry->record_offset + 16),
		2026);
	assert_int_equal(authvar_owner_ready(&service.owner, NULL), 0);
	assert_true(ass_native_variable_dispatch(&service.owner, &policy, NULL,
		request, sizeof(request)));
	assert_int_equal(message->return_status, 0x8000001aU);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(admit_existing_store),
		cmocka_unit_test(restore_deleted_secure_boot_preference),
		cmocka_unit_test(reopen_orphan_certdb),
		cmocka_unit_test(setup_replacement_preserves_timestamp),
		cmocka_unit_test(check_uefi_properties_at_runtime),
		cmocka_unit_test(factory_defaults_are_sealed_at_handoff),
		cmocka_unit_test(owner_on_nor),
		cmocka_unit_test(native_smm_opens_before_publication),
		cmocka_unit_test(lite_ass_private_data_budget),
		cmocka_unit_test(malformed_capsule_never_authorizes_source),
		cmocka_unit_test(signed_capsule_source_is_rechecked),
		cmocka_unit_test(signed_capsule_bios_fallback),
		cmocka_unit_test(signed_capsule_selects_inactive_slot),
		cmocka_unit_test(capsule_wire_uses_revision_header),
		cmocka_unit_test(capsule_ram_excludes_mailbox),
		cmocka_unit_test(fixed_certificate_asset_requires_canonical_lists),
#ifdef ASS_FIXED_ASSET_TEST
		cmocka_unit_test(fixed_factory_store_opens_and_reopens),
		cmocka_unit_test(fixed_dbx_update_requires_restart),
#endif
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
