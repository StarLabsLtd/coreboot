/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_bundle.h>
#include <commonlib/helpers.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define expect(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #condition); \
		abort(); \
	} \
} while (0)

static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t image_guid[16] = {
	0xcb, 0xb2, 0x19, 0xd7, 0x3a, 0x3d, 0x96, 0x45,
	0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f,
};
static const uint8_t vendor_guid[16] = {
	0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
	0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
};
static const uint8_t enable_guid[16] = {
	0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
	0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44,
};
static const uint8_t private_guid[16] = { 0x42 };
static const uint8_t custom_guid[16] = {
	0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
	0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f,
};
static const uint8_t cert_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const uint8_t pk[] = { 'P', 0, 'K', 0, 0, 0 };
static const uint8_t db[] = { 'd', 0, 'b', 0, 0, 0 };
static const uint8_t private_name[] = { 'x', 0, 0, 0 };
static const uint8_t vendor_name[] = {
	'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
	'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
};
static const uint8_t enable_name[] = {
	'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	'o', 0, 't', 0, 'E', 0, 'n', 0, 'a', 0, 'b', 0, 'l', 0, 'e', 0,
	0, 0,
};
static const uint8_t certdb_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
};

static uint8_t store[2048];
static struct payload_mm_authvar_store_entry entries[8];
static struct payload_mm_authvar_store_index store_index;
static uint8_t request_data[8];
static uint8_t target_data = 0xa5;
static uint8_t certdb_workspace[128];
static struct payload_mm_authvar_policy_request request;
static struct payload_mm_authvar_authority_decision decision;
static struct payload_mm_authvar_bundle_snapshot snapshot;
static struct payload_mm_authvar_bundle_plan plan;

static void base(bool user_mode, bool vendor_keys, bool enable);
static void use_private_target(void);

static void expect_empty_plan(void)
{
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));
}

static void put16(uint8_t *p, uint16_t value)
{
	p[0] = value;
	p[1] = value >> 8;
}

static void put32(uint8_t *p, uint32_t value)
{
	for (size_t i = 0U; i < 4U; i++)
		p[i] = value >> (8U * i);
}

static void set_timestamp(uint8_t time[16])
{
	memset(time, 0, 16U);
	put16(time, 2026U);
	time[2] = 9U;
	time[3] = 23U;
}

static void reset_store(void)
{
	memset(store, 0, sizeof(store));
	memset(entries, 0, sizeof(entries));
	store_index = (struct payload_mm_authvar_store_index) {
		.store = store,
		.store_size = sizeof(store),
		.used_size = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		.entries = entries,
		.entry_capacity = 8U,
		.maximum_name_size = 128U,
		.maximum_data_size = 128U,
		.maximum_records = 8U,
	};
}

static void add_blob(const uint8_t guid[16], const uint8_t *name,
	size_t name_size, uint32_t attributes, const void *value, size_t value_size)
{
	struct payload_mm_authvar_store_entry *entry =
		&entries[store_index.entry_count++];
	uint32_t record = store_index.used_size;
	uint32_t data;

	entry->record_offset = record;
	entry->name_offset = record + PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE;
	entry->name_size = name_size;
	data = (entry->name_offset + name_size + 3U) & ~3U;
	entry->data_offset = data;
	entry->data_size = value_size;
	entry->attributes = attributes;
	memcpy(entry->vendor_guid, guid, 16U);
	put16(store + record, PAYLOAD_MM_AUTHVAR_RECORD_START_ID);
	store[record + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	put32(store + record + 4U, attributes);
	put32(store + record + 36U, name_size);
	put32(store + record + 40U, value_size);
	memcpy(store + record + 44U, guid, 16U);
	memcpy(store + entry->name_offset, name, name_size);
	memcpy(store + data, value, value_size);
	store_index.record_count = store_index.entry_count;
	store_index.used_size = data + value_size;
}

static void add_record(const uint8_t guid[16], const uint8_t *name,
	size_t name_size, uint32_t attributes, uint8_t value)
{
	add_blob(guid, name, name_size, attributes, &value, sizeof(value));
}

static void replace_last_data(const void *data, size_t size)
{
	struct payload_mm_authvar_store_entry *entry =
		&entries[store_index.entry_count - 1U];

	expect(entry->data_offset + size <= sizeof(store));
	memcpy(store + entry->data_offset, data, size);
	entry->data_size = size;
	put32(store + entry->record_offset + 40U, size);
	store_index.used_size = entry->data_offset + size;
}

static void private_existing_source(void)
{
	static const uint8_t binding[32] = { 0x5a };
	static const uint8_t empty[] = { 4U, 0U, 0U, 0U };
	uint8_t database[128];
	size_t database_size;

	base(false, true, false);
	store_index.entry_count--;
	store_index.record_count--;
	store_index.used_size = entries[store_index.entry_count].record_offset;
	add_record(private_guid, private_name, sizeof(private_name), 0x27U, 0x11U);
	expect(payload_mm_authvar_certdb_compose(empty, sizeof(empty),
		PAYLOAD_MM_AUTHVAR_CERTDB_ADD, private_guid, private_name,
		sizeof(private_name) - 2U, binding, sizeof(binding), database,
		sizeof(database), &database_size) == PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	add_blob(cert_guid, certdb_name, sizeof(certdb_name), 0x27U, database,
		database_size);
	use_private_target();
}

static void base(bool user_mode, bool vendor_keys, bool enable)
{
	reset_store();
	if (user_mode)
		add_record(global_guid, pk, sizeof(pk), 0x27U, 0x44U);
	add_record(vendor_guid, vendor_name, sizeof(vendor_name), 0x23U,
		vendor_keys);
	if (enable)
		add_record(enable_guid, enable_name, sizeof(enable_name), 0x03U, 1U);
	add_blob(cert_guid, certdb_name, sizeof(certdb_name), 0x27U,
		(uint8_t[]) { 4U, 0U, 0U, 0U }, 4U);
	request = (struct payload_mm_authvar_policy_request) {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = 0x27U,
		.name = pk,
		.name_size = sizeof(pk),
		.data = request_data,
		.data_size = sizeof(request_data),
	};
	memcpy(request.vendor_guid, global_guid, 16U);
	decision = (struct payload_mm_authvar_authority_decision) {
		.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION,
		.mutation = {
			.kind = PAYLOAD_MM_AUTHVAR_MUTATION_WRITE,
			.attributes = 0x27U,
			.data_size = 1U,
		},
		.data = &target_data,
		.data_size = 1U,
		.target = PAYLOAD_MM_AUTHVAR_TARGET_PK,
		.accepted_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
	};
	set_timestamp(decision.mutation.timestamp);
	snapshot = (struct payload_mm_authvar_bundle_snapshot) {
		.request = &request,
		.decision = &decision,
		.index = &store_index,
		.certdb_workspace = certdb_workspace,
		.certdb_workspace_size = sizeof(certdb_workspace),
		.facts = {
			.setup_mode = !user_mode,
			.secure_boot = user_mode,
			.vendor_keys = vendor_keys,
		},
	};
	memset(&plan, 0xa5, sizeof(plan));
}

static enum payload_mm_verify_status make_plan(void)
{
	return payload_mm_authvar_bundle_plan(&snapshot, &plan);
}

static void expect_target(unsigned int count)
{
	expect(plan.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION);
	expect(plan.mutation_count == count);
	expect(plan.mutations[0].role == PAYLOAD_MM_AUTHVAR_BUNDLE_TARGET);
	if (decision.mutation.kind == PAYLOAD_MM_AUTHVAR_MUTATION_WRITE) {
		expect(plan.mutations[0].data == &target_data);
		expect(plan.mutations[0].data_size == 1U);
	} else {
		expect(!plan.mutations[0].data && !plan.mutations[0].data_size);
	}
}

static void test_mode_oracle(void)
{
	const struct payload_mm_authvar_store_entry *enable;

	/* Nonboolean persisted values project disabled, never truthy. */
	base(true, true, true);
	enable = payload_mm_authvar_store_find(&store_index, enable_guid,
		enable_name, sizeof(enable_name));
	expect(enable != NULL);
	store[enable->data_offset] = 2U;
	snapshot.facts.secure_boot = false;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	snapshot.facts.secure_boot = true;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);

	base(false, true, false);
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(2U);
	expect(plan.mutations[1].role ==
		PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE);
	expect(plan.mutations[1].mutation.attributes == 0x03U);
	expect(*(const uint8_t *)plan.mutations[1].data == 1U);
	expect(plan.volatile_modes == (PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));

	/* Self-signed setup enrollment can require both fixed derived roles. */
	base(false, true, false);
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE |
		PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(3U);
	expect(plan.mutations[1].role ==
		PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE);
	expect(plan.mutations[2].role ==
		PAYLOAD_MM_AUTHVAR_BUNDLE_VENDOR_KEYS_NV);

	base(false, true, false);
	snapshot.facts.at_runtime = true;
	snapshot.facts.ready_to_boot = true;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(1U);
	expect(plan.volatile_modes == PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS);

	base(true, true, true);
	decision.mutation = (struct payload_mm_authvar_policy_mutation) {
		.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE,
	};
	decision.data = NULL;
	decision.data_size = 0U;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(2U);
	expect(plan.mutations[1].role ==
		PAYLOAD_MM_AUTHVAR_BUNDLE_SECURE_BOOT_ENABLE);
	expect(plan.mutations[1].mutation.kind ==
		PAYLOAD_MM_AUTHVAR_MUTATION_DELETE);
	expect(plan.volatile_modes == (PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));

	base(true, true, true);
	snapshot.facts.at_runtime = true;
	snapshot.facts.ready_to_boot = true;
	decision.mutation = (struct payload_mm_authvar_policy_mutation) {
		.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE,
	};
	decision.data = NULL;
	decision.data_size = 0U;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(1U);
	expect(plan.volatile_modes == (PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
}

static void use_db_target(void)
{
	request.name = db;
	request.name_size = sizeof(db);
	memcpy(request.vendor_guid, image_guid, 16U);
	decision.target = PAYLOAD_MM_AUTHVAR_TARGET_DB;
}

static void test_vendor_oracle(void)
{
	base(true, true, true);
	use_db_target();
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(2U);
	expect(plan.mutations[1].role ==
		PAYLOAD_MM_AUTHVAR_BUNDLE_VENDOR_KEYS_NV);
	expect(plan.mutations[1].mutation.attributes == 0x23U);
	expect(*(const uint8_t *)plan.mutations[1].data == 0U);
	expect(plan.volatile_modes == PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT);

	base(true, true, true);
	use_db_target();
	snapshot.facts.at_runtime = true;
	snapshot.facts.ready_to_boot = true;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS;
	expect(make_plan() == PAYLOAD_MM_VERIFY_REJECTED);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));

	base(true, false, true);
	use_db_target();
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(1U);
}

static void test_runtime_mode_carry(void)
{
	/* Runtime PK deletion retains SecureBoot=1 and the persistent enable. */
	base(false, true, true);
	use_db_target();
	snapshot.facts.at_runtime = snapshot.facts.ready_to_boot = true;
	snapshot.facts.secure_boot = true;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(1U);
	expect(plan.volatile_modes == (PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));

	/* Runtime PK enrollment retains SecureBoot=0 and no persistent enable. */
	base(true, true, false);
	use_db_target();
	snapshot.facts.at_runtime = snapshot.facts.ready_to_boot = true;
	snapshot.facts.secure_boot = false;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(1U);
	expect(plan.volatile_modes == PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS);
}

static void use_private_target(void)
{
	request.name = private_name;
	request.name_size = sizeof(private_name);
	memcpy(request.vendor_guid, private_guid, sizeof(private_guid));
	decision.target = PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE;
}

static void test_private_certdb_roles(void)
{
	struct payload_mm_authvar_certdb_binding binding;
	static const uint8_t empty[] = { 4U, 0U, 0U, 0U };
	static const size_t invalid_sizes[] = { 1U, 31U, 33U, 47U, 49U, 63U };
	const void *workspace_aliases[] = {
		&snapshot, &request, &decision, &store_index, store, entries,
		private_name, &plan,
	};

	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, decision.new_binding_size);
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect_target(2U);
	expect(plan.mutations[1].role == PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB);
	expect(plan.mutations[1].data == certdb_workspace);
	expect(plan.mutations[1].mutation.attributes == 0x27U);
	expect(!memcmp(plan.mutations[1].mutation.timestamp,
		(uint8_t[16]) { 0 }, 16U));
	expect(payload_mm_authvar_certdb_find(plan.mutations[1].data,
		plan.mutations[1].data_size, private_guid, private_name,
		sizeof(private_name) - 2U, &binding) ==
		PAYLOAD_MM_AUTHVAR_CERTDB_OK);
	expect(binding.size == 32U &&
		!memcmp(binding.data, decision.new_binding, 32U));
	expect(plan.private_binding_size == 32U &&
		!memcmp(plan.private_binding, decision.new_binding, 32U));

	for (size_t i = 0U; i < ARRAY_SIZE(invalid_sizes); i++) {
		base(false, true, false);
		use_private_target();
		decision.accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
		decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
		decision.new_binding_size = invalid_sizes[i];
		memset(decision.new_binding, 0x5a, invalid_sizes[i]);
		expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
		expect_empty_plan();
	}

	/* Ordinary existing updates may borrow the append workspace unchanged. */
	base(false, true, false);
	store_index.entry_count--;
	store_index.record_count--;
	store_index.used_size = entries[store_index.entry_count].record_offset;
	add_record(private_guid, private_name, sizeof(private_name), 0x27U, 0x11U);
	add_blob(cert_guid, certdb_name, sizeof(certdb_name), 0x27U,
		(uint8_t[]) { 4U, 0U, 0U, 0U }, 4U);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	decision.data = certdb_workspace;
	decision.data_size = decision.mutation.data_size = 1U;
	certdb_workspace[0] = 0x33U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect(plan.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		plan.mutation_count == 1U &&
		plan.mutations[0].role == PAYLOAD_MM_AUTHVAR_BUNDLE_TARGET &&
		plan.mutations[0].data == certdb_workspace);
	expect(!plan.private_binding_size);

	/* ADD may never overwrite target data through its certdb workspace. */
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	decision.data = certdb_workspace;
	decision.data_size = decision.mutation.data_size = 1U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
	expect_empty_plan();

	/* Private composition workspace is protected from every sealed input. */
	for (size_t i = 0U; i < ARRAY_SIZE(workspace_aliases); i++) {
		base(false, true, false);
		use_private_target();
		decision.accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
		decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
		decision.new_binding_size = 32U;
		memset(decision.new_binding, 0x5a, 32U);
		snapshot.certdb_workspace = (void *)workspace_aliases[i];
		snapshot.certdb_workspace_size = store_index.maximum_data_size;
		expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
		expect(((const uint8_t *)&plan)[0] == 0xa5U);
	}

	private_existing_source();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING;
	decision.mutation = (struct payload_mm_authvar_policy_mutation) {
		.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE,
	};
	decision.data = NULL;
	decision.data_size = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect(plan.mutation_count == 2U &&
		plan.mutations[1].role == PAYLOAD_MM_AUTHVAR_BUNDLE_CERTDB &&
		plan.certdb_operation == PAYLOAD_MM_AUTHVAR_CERTDB_REMOVE &&
		!plan.private_binding_size && plan.mutations[1].data_size ==
			sizeof(empty) &&
		!memcmp(plan.mutations[1].data, empty, sizeof(empty)));

	/* Missing binding, stale ADD and malformed fixed-store metadata fail closed. */
	private_existing_source();
	replace_last_data(empty, sizeof(empty));
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING;
	decision.mutation = (struct payload_mm_authvar_policy_mutation) {
		.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE,
	};
	decision.data = NULL;
	decision.data_size = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_REJECTED);
	expect_empty_plan();
	private_existing_source();
	store[entries[store_index.entry_count - 1U].data_offset] ^= 1U;
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING;
	decision.mutation = (struct payload_mm_authvar_policy_mutation) {
		.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE,
	};
	decision.data = NULL;
	decision.data_size = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_MALFORMED);
	expect_empty_plan();
	private_existing_source();
	entries[store_index.entry_count - 1U].attributes ^= 1U;
	put32(store + entries[store_index.entry_count - 1U].record_offset + 4U,
		entries[store_index.entry_count - 1U].attributes);
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING;
	decision.mutation = (struct payload_mm_authvar_policy_mutation) {
		.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE,
	};
	decision.data = NULL;
	decision.data_size = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_MALFORMED);
	expect_empty_plan();
	private_existing_source();
	store[entries[store_index.entry_count - 1U].record_offset + 16U] = 1U;
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_REMOVE_PRIVATE_BINDING;
	decision.mutation = (struct payload_mm_authvar_policy_mutation) {
		.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE,
	};
	decision.data = NULL;
	decision.data_size = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_MALFORMED);
	expect_empty_plan();
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	{
		uint8_t database[128];
		size_t database_size;

		expect(payload_mm_authvar_certdb_compose(empty, sizeof(empty),
			PAYLOAD_MM_AUTHVAR_CERTDB_ADD, private_guid, private_name,
			sizeof(private_name) - 2U, decision.new_binding, 32U,
			database, sizeof(database), &database_size) ==
			PAYLOAD_MM_AUTHVAR_CERTDB_OK);
		replace_last_data(database, database_size);
	}
	expect(make_plan() == PAYLOAD_MM_VERIFY_REJECTED);
	expect_empty_plan();
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	store[entries[store_index.entry_count - 1U].data_offset] ^= 1U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_MALFORMED);
	expect_empty_plan();
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	entries[store_index.entry_count - 1U].attributes ^= 1U;
	put32(store + entries[store_index.entry_count - 1U].record_offset + 4U,
		entries[store_index.entry_count - 1U].attributes);
	expect(make_plan() == PAYLOAD_MM_VERIFY_MALFORMED);
	expect_empty_plan();
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	store[entries[store_index.entry_count - 1U].record_offset + 16U] = 1U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_MALFORMED);
	expect_empty_plan();

	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	snapshot.certdb_workspace = NULL;
	snapshot.certdb_workspace_size = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
	expect_empty_plan();
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	snapshot.certdb_workspace_size = store_index.maximum_data_size - 1U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
	expect_empty_plan();
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	store_index.maximum_data_size = 63U;
	expect(snapshot.certdb_workspace_size == sizeof(certdb_workspace));
	expect(make_plan() == PAYLOAD_MM_VERIFY_REJECTED);
	expect_empty_plan();
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	store_index.entry_count--;
	store_index.record_count--;
	expect(make_plan() == PAYLOAD_MM_VERIFY_MALFORMED);
	expect_empty_plan();
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	{
		static const uint8_t other_guid[16] = { 0x77 };
		static const uint8_t other_name[] = { 'y', 0 };
		uint8_t database[128];
		uint8_t large_binding[64] = { 0x88 };
		size_t database_size;

		expect(payload_mm_authvar_certdb_compose(empty, sizeof(empty),
			PAYLOAD_MM_AUTHVAR_CERTDB_ADD, other_guid, other_name,
			sizeof(other_name), large_binding, sizeof(large_binding),
			database, sizeof(database), &database_size) ==
			PAYLOAD_MM_AUTHVAR_CERTDB_OK);
		replace_last_data(database, database_size);
	}
	expect(make_plan() == PAYLOAD_MM_VERIFY_REJECTED);
	expect_empty_plan();
	base(false, true, false);
	use_private_target();
	decision.accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING |
		PAYLOAD_MM_AUTHVAR_INTENT_MARK_VENDOR_KEYS;
	decision.new_binding_size = 32U;
	memset(decision.new_binding, 0x5a, 32U);
	expect(make_plan() == PAYLOAD_MM_VERIFY_UNSUPPORTED);
	expect_empty_plan();
}

static void test_outcomes_and_failures(void)
{
	uint8_t before[sizeof(plan)];

	base(false, true, false);
	decision = (struct payload_mm_authvar_authority_decision) {
		.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP,
		.target = PAYLOAD_MM_AUTHVAR_TARGET_PK,
		.accepted_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
	};
	use_db_target();
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect(plan.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP &&
		!plan.mutation_count && !plan.volatile_modes);
	decision.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND;
	request.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
	expect(plan.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND &&
		!plan.mutation_count && !plan.volatile_modes);
	/* Final no-effect outcomes still require canonical current state. */
	snapshot.facts.vendor_keys = false;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));

	base(true, true, true);
	decision = (struct payload_mm_authvar_authority_decision) {
		.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP,
		.target = PAYLOAD_MM_AUTHVAR_TARGET_PK,
		.accepted_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK,
	};
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	decision.outcome = PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND;
	request.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_APPEND;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);

	base(true, true, true);
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));
	base(true, true, true);
	decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ENTER_USER_MODE |
		PAYLOAD_MM_AUTHVAR_INTENT_ENTER_SETUP_MODE;
	expect(make_plan() == PAYLOAD_MM_VERIFY_UNSUPPORTED);

	base(false, true, false);
	store_index.entry_count--;
	store_index.record_count--;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));

	base(false, true, false);
	set_timestamp(store + entries[0].record_offset + 16U);
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));

	base(true, true, true);
	store[entries[2].data_offset] = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	base(true, true, false);
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);

	base(false, true, false);
	request.name = private_name;
	request.name_size = sizeof(private_name);
	memcpy(request.vendor_guid, private_guid, 16U);
	decision.target = PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE;
	decision.mutation = (struct payload_mm_authvar_policy_mutation) {
		.kind = PAYLOAD_MM_AUTHVAR_MUTATION_DELETE,
	};
	decision.data = NULL;
	decision.data_size = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));

	base(false, true, false);
	snapshot.decision = (const void *)((uintptr_t)&decision + 1U);
	memset(&plan, 0x5a, sizeof(plan));
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
	expect(((uint8_t *)&plan)[0] == 0x5aU);
	memcpy(&snapshot, &plan, sizeof(snapshot));
	expect(payload_mm_authvar_bundle_plan(&snapshot, &plan) ==
		PAYLOAD_MM_VERIFY_INVALID);

	base(false, true, false);
	decision.target = (enum payload_mm_authvar_target)-1;
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));
	base(false, true, false);
	decision.accepted_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE;
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
	base(false, true, false);
	snapshot.facts.at_runtime = true;
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);

	base(false, true, false);
	decision.data = NULL;
	decision.data_size = decision.mutation.data_size = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));
	base(false, true, false);
	decision.mutation.attributes ^= PAYLOAD_MM_AUTHVAR_ATTRIBUTE_RUNTIME_ACCESS;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	base(false, true, false);
	request.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_BOOTSERVICE_ACCESS;
	decision.mutation.attributes = request.attributes;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	base(false, true, false);
	snapshot.facts.at_runtime = snapshot.facts.ready_to_boot = true;
	decision.mutation.attributes &=
		~PAYLOAD_MM_AUTHVAR_ATTRIBUTE_RUNTIME_ACCESS;
	request.attributes = decision.mutation.attributes;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	base(false, true, false);
	decision.mutation.timestamp[2] = 0U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_CHANGED);
	base(false, true, false);
	decision.data = request.name;
	decision.data_size = decision.mutation.data_size = 1U;
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
	expect(!memcmp(&plan, &(struct payload_mm_authvar_bundle_plan) { 0 },
		sizeof(plan)));

	/* A valid index descriptor overlapping output is rejected untouched. */
	base(false, true, false);
	memcpy(&plan, &store_index, sizeof(store_index));
	snapshot.index = (const struct payload_mm_authvar_store_index *)&plan;
	memcpy(before, &plan, sizeof(plan));
	expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
	expect(!memcmp(before, &plan, sizeof(plan)));
}

static void test_reserved_keys(void)
{
	static const struct {
		const uint8_t *guid;
		const char *ascii;
	} keys[] = {
		{ global_guid, "SetupMode" }, { global_guid, "SignatureSupport" },
		{ global_guid, "SecureBoot" }, { global_guid, "KEKDefault" },
		{ global_guid, "PKDefault" }, { global_guid, "dbDefault" },
		{ global_guid, "dbxDefault" }, { global_guid, "dbtDefault" },
		{ global_guid, "VendorKeys" }, { vendor_guid, "VendorKeysNv" },
		{ enable_guid, "SecureBootEnable" },
		{ global_guid, "AuditMode" }, { global_guid, "DeployedMode" },
		{ custom_guid, "CustomMode" }, { cert_guid, "certdb" },
		{ cert_guid, "certdbv" },
	};
	uint8_t name[64];

	for (size_t key = 0U; key < ARRAY_SIZE(keys); key++) {
		size_t length = strlen(keys[key].ascii);

		base(false, true, false);
		for (size_t i = 0U; i < length; i++) {
			name[2U * i] = keys[key].ascii[i];
			name[2U * i + 1U] = 0U;
		}
		name[2U * length] = name[2U * length + 1U] = 0U;
		request.name = name;
		request.name_size = 2U * (length + 1U);
		memcpy(request.vendor_guid, keys[key].guid, 16U);
		decision.target = PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE;
		expect(make_plan() == PAYLOAD_MM_VERIFY_INVALID);
		/* Exact key matching: the same name under a private GUID is ordinary. */
		memcpy(request.vendor_guid, private_guid, 16U);
		decision.accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
		decision.intents = PAYLOAD_MM_AUTHVAR_INTENT_ADD_PRIVATE_BINDING;
		decision.new_binding_size = 32U;
		memset(decision.new_binding, 0x5a, 32U);
		expect(make_plan() == PAYLOAD_MM_VERIFY_OK);
		expect_target(2U);
	}
}

int main(void)
{
	test_mode_oracle();
	test_vendor_oracle();
	test_runtime_mode_carry();
	test_private_certdb_roles();
	test_outcomes_and_failures();
	test_reserved_keys();
	puts("payload-mm authenticated-variable bundle tests passed");
	return 0;
}
