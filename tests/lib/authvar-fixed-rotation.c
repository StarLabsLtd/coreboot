/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/authvar_owner.h>
#include <boot/payload_mm_authvar_record.h>
#include <boot/payload_mm_authvar_service.h>
#include <commonlib/endian.h>
#include <drivers/efi_variable_service/native_media.h>
#include <drivers/efi_variable_service/native_smm.h>
#include <Uefi/UefiBaseType.h>
#include <string.h>
#include <tests/test.h>

#include "authvar-owner-nor.h"

void fixed_slot_rotation_keeps_device_revocations(void **unused)
{
	static _Alignas(16) uint8_t arena[4 * 1024 * 1024];
	static struct test_nor nor;
	static uint8_t before[LITE_NOR_SIZE], dbx[64 * 1024], value[64 * 1024];
	static const EFI_GUID global = {
		0x8be4df61, 0x93ca, 0x11d2, { 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c },
	};
	static const EFI_GUID image = {
		0xd719b2cb, 0x3d3a, 0x4596, { 0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f },
	};
	const struct authvar_fixed_policy *current = ass_native_fixed_policy();
	struct payload_mm_authvar_auth2_view baseline;
	struct ass_native_store_memory memory;
	struct ass_native_store store;
	struct authvar_fixed_policy first, second;
	size_t dbx_size = sizeof(dbx);

	assert_non_null(current);
	assert_int_equal(payload_mm_authvar_auth2_parse(current->dbx_baseline.data,
		current->dbx_baseline.size, &baseline), PAYLOAD_MM_AUTHVAR_FORMAT_OK);
	first = *current;
	first.dbx_append = (struct payload_mm_authvar_span) {
		baseline.payload.data, read_le32(baseline.payload.data + 16),
	};
	/* All three lists change. This slot lacks the KEK signing the current dbx update. */
	second = first;
	second.factory_pk = (struct payload_mm_authvar_span) {
		first.factory_db.data, read_le32(first.factory_db.data + 16),
	};
	second.factory_kek = first.factory_pk;
	second.factory_db = first.factory_kek;

	memset(nor.bytes, 0xff, sizeof(nor.bytes));
	region_device_init(&nor.region, &nor_ops, 0, sizeof(nor.bytes));
	assert_true(ass_native_store_layout(&store, &memory, arena, sizeof(arena),
		sizeof(nor.bytes), NOR_BLOCK));
	allow_program = true;
	assert_int_equal(ass_native_store_open(&store, memory.owner, &nor.region,
		memory.scratch, nor_write_enabled, memory.workspace, memory.workspace_size,
		sizeof(dbx), true, &first), 0);
	/* A pre-existing mutable installation may also have its own keys on media.
	 * Save it separately to exercise shadowing after checking the factory image. */
	static uint8_t legacy[LITE_NOR_SIZE];
	memcpy(legacy, nor.bytes, sizeof(legacy));
	memcpy(before, nor.bytes, sizeof(before));
	allow_program = false;
	assert_int_equal(ass_native_store_open(&store, memory.owner, &nor.region,
		memory.scratch, nor_write_enabled, memory.workspace, memory.workspace_size,
		sizeof(dbx), true, &second), 0);
	assert_int_equal(authvar_owner_set(memory.owner, u"dbx", &image, 0x67,
		current->dbx_update.size, current->dbx_update.data, false, &second, NULL),
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert_memory_equal(before, nor.bytes, sizeof(before));

	/* The original authority appends its signed update to the same physical NOR. */
	allow_program = true;
	assert_int_equal(ass_native_store_open(&store, memory.owner, &nor.region,
		memory.scratch, nor_write_enabled, memory.workspace, memory.workspace_size,
		sizeof(dbx), true, current), 0);
	assert_true(memory.owner->restart_required);
	assert_int_equal(authvar_owner_get(memory.owner, u"dbx", &image, NULL, &dbx_size, dbx), 0);
	assert_true(dbx_size > first.dbx_append.size + baseline.payload.size);
	memcpy(before, nor.bytes, sizeof(before));
	allow_program = false;
	const struct authvar_fixed_policy *slots[] = { &second, &first };
	for (size_t slot = 0; slot < ARRAY_SIZE(slots); slot++) {
		const struct authvar_fixed_policy *policy = slots[slot];
		const struct {
			const EFI_GUID *guid;
			const uint16_t *name;
			size_t name_size;
			struct payload_mm_authvar_span data;
		} keys[] = {
			{ &global, u"PK", sizeof(u"PK"), policy->factory_pk },
			{ &global, u"KEK", sizeof(u"KEK"), policy->factory_kek },
			{ &image, u"db", sizeof(u"db"), policy->factory_db },
		};
		assert_int_equal(ass_native_store_open(&store, memory.owner, &nor.region,
			memory.scratch, nor_write_enabled, memory.workspace, memory.workspace_size,
			sizeof(dbx), true, policy), 0);
		assert_false(memory.owner->restart_required);
		for (size_t i = 0; i < ARRAY_SIZE(keys); i++) {
			size_t size = sizeof(value);
			uint32_t attributes;
			assert_null(payload_mm_authvar_store_find(&memory.owner->index,
				(const void *)keys[i].guid, keys[i].name, keys[i].name_size));
			assert_int_equal(authvar_owner_get(memory.owner, keys[i].name, keys[i].guid,
				&attributes, &size, value), 0);
			assert_int_equal(attributes, 0x23);
			assert_int_equal(size, keys[i].data.size);
			assert_memory_equal(value, keys[i].data.data, size);
		}
		uint32_t snapshot_size;
		assert_int_equal(authvar_owner_image_snapshot(memory.owner, value,
			sizeof(value), &snapshot_size), 0);
		size_t offset = AUTHVAR_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE;
		assert_memory_equal(value + offset, policy->factory_pk.data, policy->factory_pk.size);
		offset += ALIGN_UP(policy->factory_pk.size, 8);
		assert_memory_equal(value + offset, policy->factory_db.data, policy->factory_db.size);
		offset += ALIGN_UP(policy->factory_db.size, 8);
		assert_true(offset + dbx_size <= snapshot_size);
		assert_memory_equal(value + offset, dbx, dbx_size);
		size_t size = sizeof(value);
		assert_int_equal(authvar_owner_get(memory.owner, u"dbx", &image, NULL, &size, value), 0);
		assert_int_equal(size, dbx_size);
		assert_memory_equal(value, dbx, size);
		assert_memory_equal(before, nor.bytes, sizeof(before));
	}

	/* The older slot's own floor is satisfied by this image, but the device
	 * already accepted a newer authenticated revocation. */
	authvar_owner_enter_runtime(memory.owner);
	assert_int_equal(authvar_owner_refresh(memory.owner,
		legacy + (store.media.store - store.media.image), store.media.store_size, &first),
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);

	/* Load former variable-managed keys. Neither reads, enumeration nor signature
	 * verification may consult these records or alter them during a slot switch. */
	memcpy(nor.bytes, legacy, sizeof(legacy));
	assert_int_equal(ass_native_store_open(&store, memory.owner, &nor.region,
		memory.scratch, nor_write_enabled, memory.workspace, memory.workspace_size,
		sizeof(dbx), true, &first), 0);
	uint8_t *candidate = store.media.candidate + store.media.plan.fv_header_size;
	memcpy(candidate, store.media.store, store.media.store_size);
	size_t offset = memory.owner->index.used_size;
	const struct {
		const EFI_GUID *guid;
		const uint16_t *name;
		size_t name_size;
		struct payload_mm_authvar_span data;
	} legacy_keys[] = {
		{ &global, u"PK", sizeof(u"PK"), first.factory_pk },
		{ &global, u"KEK", sizeof(u"KEK"), first.factory_kek },
		{ &image, u"db", sizeof(u"db"), first.factory_db },
	};
	for (size_t i = 0; i < ARRAY_SIZE(legacy_keys); i++) {
		struct payload_mm_authvar_record_descriptor record = {
			.name = legacy_keys[i].name, .name_size = legacy_keys[i].name_size,
			.attributes = 0x27,
		};
		struct payload_mm_authvar_record_span span = {
			legacy_keys[i].data.data, legacy_keys[i].data.size,
		};
		uint32_t encoded;
		memcpy(record.vendor_guid, legacy_keys[i].guid, sizeof(record.vendor_guid));
		assert_true(payload_mm_authvar_record_encode(&record, &span, 1,
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO, candidate + offset,
			store.media.store_size - offset, &encoded));
		candidate[offset + 2] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
		offset += encoded;
	}
	allow_program = true;
	assert_int_equal(authvar_media_commit(&store.media, &store.io, candidate,
		store.media.store_size, NULL, NULL), 0);
	memcpy(before, nor.bytes, sizeof(before));
	allow_program = false;
	assert_int_equal(ass_native_store_open(&store, memory.owner, &nor.region,
		memory.scratch, nor_write_enabled, memory.workspace, memory.workspace_size,
		sizeof(dbx), true, &second), 0);
	size_t size = sizeof(value);
	assert_int_equal(authvar_owner_get(memory.owner, u"KEK", &global, NULL, &size, value), 0);
	assert_int_equal(size, second.factory_kek.size);
	assert_memory_equal(value, second.factory_kek.data, size);
	assert_int_equal(authvar_owner_set(memory.owner, u"dbx", &image, 0x67,
		current->dbx_update.size, current->dbx_update.data, false, &second, NULL),
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	EFI_GUID cursor = { 0 };
	uint16_t name[64] = { 0 };
	unsigned int counts[ARRAY_SIZE(legacy_keys)] = { 0 };
	uint64_t status;
	for (size_t entries = 0; ; entries++) {
		size = sizeof(name);
		status = authvar_owner_next(memory.owner, &size, name, &cursor);
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND)
			break;
		assert_int_equal(status, 0);
		assert_true(entries < 32);
		for (size_t i = 0; i < ARRAY_SIZE(legacy_keys); i++)
			if (size == legacy_keys[i].name_size &&
			    !memcmp(&cursor, legacy_keys[i].guid, sizeof(cursor)) &&
			    !memcmp(name, legacy_keys[i].name, size))
				counts[i]++;
	}
	for (size_t i = 0; i < ARRAY_SIZE(legacy_keys); i++)
		assert_int_equal(counts[i], 1);
	assert_memory_equal(before, nor.bytes, sizeof(before));

}
