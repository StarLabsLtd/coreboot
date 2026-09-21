/* SPDX-License-Identifier: GPL-2.0-only */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "payload_mm_fmp_owner_prepared_reader_internal.h"

#define SLOT_SIZE 4096U
#define DOMAIN_SIZE (2U * SLOT_SIZE)
#define MEDIA_SIZE 0x10000U
#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

struct model;

struct test_device {
	struct region_device rdev;
	struct model *model;
};

struct model {
	u8 media[MEDIA_SIZE];
	struct test_device device;
	struct test_device alternate;
	struct region_device state[PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS];
	struct fmp_owner_layout layout;
	struct payload_mm_fmp_owner_journal_anchor current;
	struct payload_mm_fmp_owner_journal_anchor candidate;
	struct capsule_tpm_anchor_grant grant;
	struct capsule_tpm_anchor_binding binding;
	struct payload_mm_fmp_owner_transition_material material;
	unsigned int reads;
	unsigned int short_read;
	unsigned int mutate_read;
	unsigned int mutate_policy_read;
	unsigned int mutate_control_read;
	bool reenter_load;
	bool reenter_legacy_prove;
	bool reenter_authorized_load;
	struct payload_mm_fmp_owner_prepared_reader *reader;
};

struct hash_context {
	struct model *model;
};

static void test_hash(const void *buffer, size_t size, u8 digest[32])
{
	u32 value = 2166136261U;

	for (size_t i = 0; i < size; i++)
		value = (value ^ ((const u8 *)buffer)[i]) * 16777619U;
	for (size_t i = 0; i < 32; i++) {
		value = value * 1103515245U + 12345U + (u32)i;
		digest[i] = (u8)(value >> ((i & 3U) * 8U));
	}
}

static enum cb_err sha256(const void *opaque, const void *buffer, size_t size,
	u8 digest[32])
{
	const struct hash_context *context = opaque;

	CHECK(context && context->model);
	test_hash(buffer, size, digest);
	return CB_SUCCESS;
}

static ssize_t readat(const struct region_device *device, void *buffer,
	size_t offset, size_t size)
{
	struct test_device *test = (void *)((u8 *)device -
		offsetof(struct test_device, rdev));
	struct model *model = test->model;

	model->reads++;
	if (offset > MEDIA_SIZE || size > MEDIA_SIZE - offset)
		return -1;
	memcpy(buffer, model->media + offset, size);
	if (model->mutate_read == model->reads)
		model->media[model->layout.state[0].offset + SLOT_SIZE] ^= 1;
	if (model->mutate_policy_read == model->reads)
		model->device.rdev.region.size--;
	if (model->mutate_control_read == model->reads)
		model->reader->control = 0;
	if (model->reenter_load)
		CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(
			model->reader, &model->binding, &model->material) == CB_ERR);
	if (model->reenter_legacy_prove)
		CHECK(payload_mm_fmp_owner_prepared_reader_prove(model->reader,
			&model->grant) == CB_ERR);
	if (model->reenter_authorized_load)
		CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(
			model->reader, &model->binding, &model->material) == CB_ERR);
	return model->short_read == model->reads ? (ssize_t)size - 1 :
		(ssize_t)size;
}

static const struct region_device_ops test_ops = {
	.readat = readat,
};

static struct payload_mm_fmp_owner_journal_manifest *manifest(
	struct model *model, size_t domain, u32 slot)
{
	return (void *)(model->media + model->layout.state[domain].offset +
		slot * SLOT_SIZE);
}

static struct payload_mm_fmp_owner_prepared *prepared(
	struct model *model, size_t domain, u32 slot)
{
	return (void *)(model->media + model->layout.state[domain].offset +
		slot * SLOT_SIZE +
		sizeof(struct payload_mm_fmp_owner_journal_manifest));
}

static struct payload_mm_fmp_owner_transition_material *material(
	struct model *model, size_t domain, u32 slot)
{
	return (void *)((u8 *)prepared(model, domain, slot) +
		sizeof(struct payload_mm_fmp_owner_prepared));
}

static void build_manifest(struct model *model,
	struct payload_mm_fmp_owner_journal_manifest *record, u64 epoch,
	const u8 identity_binding[32],
	struct payload_mm_fmp_owner_journal_anchor *anchor)
{
	u8 storage_domain[32];

	test_hash(&model->layout, sizeof(model->layout), storage_domain);
	*record = (struct payload_mm_fmp_owner_journal_manifest) {
		.magic = PAYLOAD_MM_FMP_OWNER_JOURNAL_MAGIC,
		.revision = PAYLOAD_MM_FMP_OWNER_JOURNAL_REVISION,
		.size = sizeof(*record),
		.slot_size = SLOT_SIZE,
		.owner_record_size = sizeof(struct payload_mm_fmp_owner_record),
		.owner_record_count = PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS,
		.format = PAYLOAD_MM_FMP_OWNER_JOURNAL_FORMAT,
		.epoch = epoch,
	};
	memcpy(record->storage_domain, storage_domain,
		sizeof(record->storage_domain));
	memcpy(record->identity_binding, identity_binding,
		sizeof(record->identity_binding));
	for (size_t key = 0; key < PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS; key++)
		record->record[key].sequence = 1;
	anchor->epoch = epoch;
	test_hash(record, sizeof(*record), anchor->digest);
}

static void refresh_authorized_candidate(struct model *model)
{
	struct payload_mm_fmp_owner_authorized_anchor_input input;

	CHECK(payload_mm_fmp_owner_authorized_anchor_input(manifest(model, 0, 1),
		&model->current, model->material.authorization.generation,
		model->material.authorization.transaction, &model->material.receipt,
		&input));
	test_hash(&input, sizeof(input), model->candidate.digest);
	prepared(model, 0, 1)->current = model->current;
	prepared(model, 0, 1)->candidate = model->candidate;
	memcpy(&model->grant.current, &model->current, sizeof(model->current));
	memcpy(&model->grant.candidate, &model->candidate,
		sizeof(model->candidate));
	model->material.authorization.current = model->grant.current;
	model->material.authorization.candidate = model->grant.candidate;
	test_hash(&model->material.authorization,
		sizeof(model->material.authorization),
		model->material.receipt.authorization_digest);
	*material(model, 0, 1) = model->material;
}

static void make_composite_current(struct model *model)
{
	struct payload_mm_fmp_owner_transition_material current_material =
		model->material;
	struct payload_mm_fmp_owner_authorized_anchor_input input;
	struct payload_mm_fmp_owner_journal_anchor predecessor = {
		.epoch = 6,
		.digest = { 0xa5 },
	};
	struct payload_mm_fmp_owner_prepared *current_prepared =
		prepared(model, 0, 0);

	current_material.authorization.generation = 9;
	current_material.authorization.transaction = 10;
	memcpy(&current_material.authorization.current, &predecessor,
		sizeof(predecessor));
	*current_prepared = (struct payload_mm_fmp_owner_prepared) {
		.magic = PAYLOAD_MM_FMP_OWNER_PREPARED_MAGIC,
		.revision = PAYLOAD_MM_FMP_OWNER_PREPARED_REVISION,
		.size = sizeof(*current_prepared),
		.state = PAYLOAD_MM_FMP_OWNER_COMMITTED_STATE,
		.generation = current_material.authorization.generation,
		.transaction = current_material.authorization.transaction,
		.current = predecessor,
		.candidate.epoch = manifest(model, 0, 0)->epoch,
	};
	CHECK(payload_mm_fmp_owner_authorized_anchor_input(manifest(model, 0, 0),
		&predecessor, current_prepared->generation,
		current_prepared->transaction, &current_material.receipt, &input));
	test_hash(&input, sizeof(input), current_prepared->candidate.digest);
	memcpy(&current_material.authorization.candidate,
		&current_prepared->candidate, sizeof(current_prepared->candidate));
	test_hash(&current_material.authorization,
		sizeof(current_material.authorization),
		current_material.receipt.authorization_digest);
	*material(model, 0, 0) = current_material;
	model->current = current_prepared->candidate;
	refresh_authorized_candidate(model);
}

static void initialize_model(struct model *model)
{
	static const u8 identity_binding[32] = {
		1, 2, 3, 4, 5, 6, 7, 8,
	};

	memset(model, 0, sizeof(*model));
	memset(model->media, 0xff, sizeof(model->media));
	model->layout = (struct fmp_owner_layout) {
		.revision = PAYLOAD_MM_FMP_OWNER_LAYOUT_REVISION,
		.size = sizeof(model->layout),
		.media_size = MEDIA_SIZE,
		.erase_size = 0x100,
		.slot_size = SLOT_SIZE,
		.route_count = 1,
		.state = {
			{ .offset = 0x1000, .size = DOMAIN_SIZE },
			{ .offset = 0x4000, .size = DOMAIN_SIZE },
		},
		.smmstore = { .offset = 0x7000, .size = 0x1000 },
		.route = {{
			.image_offset = 0,
			.flash_offset = 0x8000,
			.size = 0x1000,
			.flags = LB_CAPSULE_REGION_BIOS,
		}},
	};
	model->device.rdev = (struct region_device)
		REGION_DEV_INIT(&test_ops, 0, MEDIA_SIZE);
	model->device.model = model;
	model->alternate.rdev = (struct region_device)
		REGION_DEV_INIT(&test_ops, 0, MEDIA_SIZE);
	model->alternate.model = model;
	for (size_t domain = 0;
	     domain < PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS; domain++) {
		model->state[domain] = (struct region_device)REGION_DEV_INIT(NULL,
			model->layout.state[domain].offset,
			model->layout.state[domain].size);
		model->state[domain].root = &model->device.rdev;
		model->state[domain].ops = NULL;
	}
	build_manifest(model, manifest(model, 0, 0), 7, identity_binding,
		&model->current);
	build_manifest(model, manifest(model, 0, 1), 8, identity_binding,
		&model->candidate);
	*prepared(model, 0, 1) = (struct payload_mm_fmp_owner_prepared) {
		.magic = PAYLOAD_MM_FMP_OWNER_PREPARED_MAGIC,
		.revision = PAYLOAD_MM_FMP_OWNER_PREPARED_REVISION,
		.size = sizeof(struct payload_mm_fmp_owner_prepared),
		.state = PAYLOAD_MM_FMP_OWNER_PREPARED_STATE,
		.generation = 11,
		.transaction = 13,
		.current = model->current,
		.candidate = model->candidate,
	};
	model->grant = (struct capsule_tpm_anchor_grant) {
		.revision = CAPSULE_TPM_ANCHOR_GRANT_REVISION,
		.size = sizeof(model->grant),
		.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION,
		.nv_index = 0x01000020,
		.generation = 11,
		.transaction = 13,
	};
	memcpy(&model->grant.current, &model->current, sizeof(model->current));
	memcpy(&model->grant.candidate, &model->candidate,
		sizeof(model->candidate));
	model->binding = (struct capsule_tpm_anchor_binding) {
		.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION,
		.nv_index = model->grant.nv_index,
		.authority_name = { 0, 0x0b, 1 },
		.policy_ref_size = 7,
		.policy_ref = { 'c', 'a', 'p', 's', 'u', 'l', 'e' },
		.write_locked = 1,
	};
	model->material = (struct payload_mm_fmp_owner_transition_material) {
		.authorization = {
			.revision = CAPSULE_TPM_ANCHOR_AUTHORIZATION_REVISION,
			.size = sizeof(struct capsule_tpm_anchor_authorization),
			.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION,
			.nv_index = model->grant.nv_index,
			.generation = model->grant.generation,
			.transaction = model->grant.transaction,
			.current = model->grant.current,
			.candidate = model->grant.candidate,
			.policy_ref_size = 7,
			.modulus_size = 256,
		},
		.receipt = {
			.magic = PAYLOAD_MM_FMP_OWNER_AUTH_RECEIPT_MAGIC,
			.revision = PAYLOAD_MM_FMP_OWNER_AUTH_RECEIPT_REVISION,
			.size = sizeof(struct payload_mm_fmp_owner_auth_receipt),
			.capsule_size = UINT32_MAX,
			.digest_algorithm = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256,
			.digest_size = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE,
			.capsule_digest = { 0x5a },
		},
	};
	memcpy(model->material.authorization.authority_name,
		model->binding.authority_name,
		sizeof(model->material.authorization.authority_name));
	memcpy(model->material.authorization.policy_ref,
		model->binding.policy_ref,
		sizeof(model->material.authorization.policy_ref));
	memset(model->material.authorization.modulus, 0x88, 256);
	memset(model->material.authorization.write.approved_policy, 0x55, 32);
	memset(model->material.authorization.write.cp_hash, 0x66, 32);
	memset(model->material.authorization.write.nonce, 0x77, 32);
	model->material.authorization.write.signature_size = 256;
	memset(model->material.authorization.write.signature, 0x99, 256);
	memset(model->material.authorization.lock.approved_policy, 0x5c, 32);
	memset(model->material.authorization.lock.cp_hash, 0x6c, 32);
	memset(model->material.authorization.lock.nonce, 0x7c, 32);
	model->material.authorization.lock.signature_size = 256;
	memset(model->material.authorization.lock.signature, 0x9c, 256);
	refresh_authorized_candidate(model);
}

static void initialize_platform_model(struct model *model)
{
	struct payload_mm_fmp_owner_platform_anchor_input input;
	struct payload_mm_fmp_owner_platform_receipt receipt = {
		.magic = PAYLOAD_MM_FMP_OWNER_PLATFORM_RECEIPT_MAGIC,
		.revision = PAYLOAD_MM_FMP_OWNER_PLATFORM_RECEIPT_REVISION,
		.size = sizeof(receipt),
		.capsule_size = UINT32_MAX,
		.digest_algorithm = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256,
		.digest_size = PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE,
		.capsule_digest = { 0x5a },
	};
	struct payload_mm_fmp_owner_prepared *candidate;
	u8 *tail;

	initialize_model(model);
	candidate = prepared(model, 0, 1);
	model->grant.policy_revision = CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION;
	CHECK(payload_mm_fmp_owner_platform_anchor_input(manifest(model, 0, 1),
		&model->current, candidate->generation, candidate->transaction,
		&receipt, &input));
	test_hash(&input, sizeof(input), model->candidate.digest);
	candidate->candidate = model->candidate;
	memcpy(&model->grant.current, &model->current, sizeof(model->current));
	memcpy(&model->grant.candidate, &model->candidate,
		sizeof(model->candidate));
	tail = (u8 *)candidate + sizeof(*candidate);
	memset(tail, 0xff, SLOT_SIZE -
		sizeof(struct payload_mm_fmp_owner_journal_manifest) -
		sizeof(*candidate));
	memcpy(tail, &receipt, sizeof(receipt));
}

static enum cb_err initialize_reader(struct model *model,
	struct payload_mm_fmp_owner_prepared_reader *reader)
{
	const struct hash_context context = { .model = model };

	return payload_mm_fmp_owner_prepared_reader_init(reader, &model->layout,
		model->state, &model->current, sha256, &context, sizeof(context));
}

static void success_and_replay(void)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };
	struct model model;

	initialize_model(&model);
	CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
		&model.grant) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
		&model.grant) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
		&model.binding, &model.material) == CB_ERR);
}

static void expect_failure(struct model *model)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };

	model->reader = &reader;
	CHECK(initialize_reader(model, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
		&model->grant) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
		&model->grant) == CB_ERR);
}

static void expect_success(struct model *model)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };

	model->reader = &reader;
	CHECK(initialize_reader(model, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
		&model->grant) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
		&model->grant) == CB_ERR);
}

static void hostile_cases(void)
{
	struct model model;

	initialize_model(&model);
	prepared(&model, 0, 1)->state = 0xfffffffaU;
	expect_failure(&model);

	initialize_model(&model);
	prepared(&model, 0, 1)->transaction++;
	expect_failure(&model);

	initialize_model(&model);
	manifest(&model, 0, 0)->record[0].reserved = 1;
	test_hash(manifest(&model, 0, 0), sizeof(*manifest(&model, 0, 0)),
		model.current.digest);
	prepared(&model, 0, 1)->current = model.current;
	memcpy(&model.grant.current, &model.current, sizeof(model.current));
	expect_failure(&model);

	initialize_model(&model);
	manifest(&model, 0, 1)->record[4].sequence = 0;
	test_hash(manifest(&model, 0, 1), sizeof(*manifest(&model, 0, 1)),
		model.candidate.digest);
	prepared(&model, 0, 1)->candidate = model.candidate;
	memcpy(&model.grant.candidate, &model.candidate,
		sizeof(model.candidate));
	expect_failure(&model);

	initialize_model(&model);
	manifest(&model, 0, 1)->epoch++;
	expect_failure(&model);

	initialize_model(&model);
	manifest(&model, 0, 1)->identity_binding[0] ^= 1;
	test_hash(manifest(&model, 0, 1), sizeof(*manifest(&model, 0, 1)),
		model.candidate.digest);
	prepared(&model, 0, 1)->candidate = model.candidate;
	memcpy(&model.grant.candidate, &model.candidate,
		sizeof(model.candidate));
	expect_failure(&model);

	initialize_model(&model);
	*prepared(&model, 1, 0) = *prepared(&model, 0, 1);
	*manifest(&model, 1, 0) = *manifest(&model, 0, 1);
	expect_failure(&model);

	initialize_model(&model);
	model.current.epoch--;
	expect_failure(&model);

	initialize_model(&model);
	model.short_read = 2;
	expect_failure(&model);

	initialize_model(&model);
	model.mutate_read = 5;
	expect_failure(&model);

	initialize_model(&model);
	model.mutate_policy_read = 2;
	expect_failure(&model);

	initialize_model(&model);
	model.mutate_control_read = 2;
	expect_failure(&model);

	initialize_model(&model);
	prepared(&model, 0, 1)->state = PAYLOAD_MM_FMP_OWNER_COMMITTED_STATE;
	expect_failure(&model);
}

static void terminal_epoch(void)
{
	struct model model;

	initialize_model(&model);
	manifest(&model, 0, 0)->epoch = UINT64_MAX - 1;
	model.current.epoch = UINT64_MAX - 1;
	test_hash(manifest(&model, 0, 0), sizeof(*manifest(&model, 0, 0)),
		model.current.digest);
	manifest(&model, 0, 1)->epoch = UINT64_MAX;
	model.candidate.epoch = UINT64_MAX;
	test_hash(manifest(&model, 0, 1), sizeof(*manifest(&model, 0, 1)),
		model.candidate.digest);
	refresh_authorized_candidate(&model);
	expect_success(&model);

	initialize_model(&model);
	model.current.epoch = UINT64_MAX;
	{
		struct payload_mm_fmp_owner_prepared_reader reader = { 0 };

		CHECK(initialize_reader(&model, &reader) == CB_ERR);
	}
}

static void invalid_initialization(void)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };
	struct model model;

	initialize_model(&model);
	model.layout.slot_size =
		sizeof(struct payload_mm_fmp_owner_journal_manifest) +
		sizeof(struct payload_mm_fmp_owner_prepared) - 1;
	CHECK(initialize_reader(&model, &reader) == CB_ERR);
	initialize_model(&model);
	model.state[0].region.size--;
	CHECK(initialize_reader(&model, &reader) == CB_ERR);
	memset(&reader, 0, sizeof(reader));
	initialize_model(&model);
	model.state[0].region.offset++;
	CHECK(initialize_reader(&model, &reader) == CB_ERR);
	memset(&reader, 0, sizeof(reader));
	initialize_model(&model);
	model.state[1].root = &model.alternate.rdev;
	CHECK(initialize_reader(&model, &reader) == CB_ERR);
	memset(&reader, 0, sizeof(reader));
	initialize_model(&model);
	model.device.rdev.region.offset = 1;
	CHECK(initialize_reader(&model, &reader) == CB_ERR);
	memset(&reader, 0, sizeof(reader));
	initialize_model(&model);
	model.device.rdev.region.size--;
	CHECK(initialize_reader(&model, &reader) == CB_ERR);
}

static void authorized_success_and_replay(void)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };
	struct payload_mm_fmp_owner_transition_material output = { 0 };
	struct model model;

	initialize_model(&model);
	model.reader = &reader;
	CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
		&model.binding, &output) == CB_SUCCESS);
	CHECK(!memcmp(&output, &model.material, sizeof(output)));
	CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
		&model.binding, &output) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
		&model.grant) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_loaded(&reader,
		&model.grant) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_loaded(&reader,
		&model.grant) == CB_ERR);
}

static void expect_authorized_load_failure(struct model *model)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };
	struct payload_mm_fmp_owner_transition_material output;

	memset(&output, 0xa5, sizeof(output));
	model->reader = &reader;
	CHECK(initialize_reader(model, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
		&model->binding, &output) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
		&model->binding, &output) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
		&model->grant) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_loaded(&reader,
		&model->grant) == CB_ERR);
}

static void authorized_mutations(void)
{
	struct model model;

	initialize_model(&model);
	material(&model, 0, 1)->receipt.revision = 1;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->receipt.capsule_size--;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->receipt.capsule_digest[0] ^= 1;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->receipt.magic ^= 1;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->receipt.capsule_size = 0;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->receipt.capsule_size =
		(uint64_t)UINT32_MAX + 1;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->receipt.digest_algorithm++;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	memset(material(&model, 0, 1)->receipt.capsule_digest, 0,
		PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE);
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->receipt.authorization_digest[0] ^= 1;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->authorization.write.reserved[0] = 1;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->authorization.modulus[256] = 1;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	material(&model, 0, 1)->authorization.candidate.digest[0] ^= 1;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	model.binding.nv_index++;
	expect_authorized_load_failure(&model);
	initialize_model(&model);
	model.reenter_load = true;
	{
		struct payload_mm_fmp_owner_prepared_reader reader = { 0 };
		struct payload_mm_fmp_owner_transition_material output;

		model.reader = &reader;
		CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
		CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(
			&reader, &model.binding, &output) == CB_SUCCESS);
	}
	initialize_model(&model);
	model.reenter_legacy_prove = true;
	{
		struct payload_mm_fmp_owner_prepared_reader reader = { 0 };
		struct payload_mm_fmp_owner_transition_material output;

		model.reader = &reader;
		CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
		CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(
			&reader, &model.binding, &output) == CB_SUCCESS);
	}
	initialize_model(&model);
	model.reenter_authorized_load = true;
	{
		struct payload_mm_fmp_owner_prepared_reader reader = { 0 };

		model.reader = &reader;
		CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
		CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
			&model.grant) == CB_SUCCESS);
	}
}

static void authorized_domain_vector(void)
{
	static const u8 domain[32] = "PAYLOAD-MM-FMP-AUTH-ANCHOR-V2";
	struct payload_mm_fmp_owner_authorized_anchor_input input;
	struct model model;

	initialize_model(&model);
	CHECK(payload_mm_fmp_owner_authorized_anchor_input(
		manifest(&model, 0, 1), &model.current,
		model.material.authorization.generation,
		model.material.authorization.transaction, &model.material.receipt,
		&input));
	CHECK(!memcmp(input.bytes, domain, sizeof(domain)));
	CHECK(!memcmp(input.bytes + 32, manifest(&model, 0, 1),
		sizeof(*manifest(&model, 0, 1))));
	CHECK(input.bytes[376] == (u8)model.current.epoch);
	CHECK(!memcmp(input.bytes + 384, model.current.digest,
		sizeof(model.current.digest)));
	CHECK(input.bytes[416] == 11 && input.bytes[424] == 13);
	CHECK(input.bytes[432] ==
		(u8)PAYLOAD_MM_FMP_OWNER_AUTH_RECEIPT_MAGIC);
	CHECK(input.bytes[440] == PAYLOAD_MM_FMP_OWNER_AUTH_RECEIPT_REVISION);
	CHECK(input.bytes[444] ==
		sizeof(struct payload_mm_fmp_owner_auth_receipt));
	CHECK(input.bytes[448] == 0xff && input.bytes[451] == 0xff &&
		input.bytes[452] == 0 && input.bytes[455] == 0);
	CHECK(input.bytes[456] == PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256);
	CHECK(input.bytes[460] == PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE);
	CHECK(!memcmp(input.bytes + 464, model.material.receipt.capsule_digest,
		PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE));
}

static void authorized_double_read_mutation(void)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };
	struct payload_mm_fmp_owner_transition_material output;
	struct model model;

	initialize_model(&model);
	model.reader = &reader;
	CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
		&model.binding, &output) == CB_SUCCESS);
	material(&model, 0, 1)->receipt.capsule_digest[0] ^= 1;
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_loaded(&reader,
		&model.grant) == CB_ERR);
}

static void divergent_composite_current_duplicate(void)
{
	struct payload_mm_fmp_owner_transition_material *duplicate_material;
	struct model model;

	initialize_model(&model);
	make_composite_current(&model);
	memcpy(model.media + model.layout.state[1].offset,
		model.media + model.layout.state[0].offset, SLOT_SIZE);
	duplicate_material = material(&model, 1, 0);
	duplicate_material->authorization.write.nonce[0] ^= 1;
	test_hash(&duplicate_material->authorization,
		sizeof(duplicate_material->authorization),
		duplicate_material->receipt.authorization_digest);
	expect_authorized_load_failure(&model);
}

static void authorized_aliases(void)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };
	struct model model;

	initialize_model(&model);
	CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
		&model.binding, (void *)&reader.loaded_material) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
		&model.binding, (void *)&model.binding) == CB_ERR);
}

static void authorized_interruption_matrix(void)
{
	struct payload_mm_fmp_owner_prepared_reader reader;
	struct payload_mm_fmp_owner_transition_material output;
	struct model baseline;
	unsigned int reads;

	initialize_model(&baseline);
	memset(&reader, 0, sizeof(reader));
	baseline.reader = &reader;
	CHECK(initialize_reader(&baseline, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
		&baseline.binding, &output) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_loaded(&reader,
		&baseline.grant) == CB_SUCCESS);
	reads = baseline.reads;
	for (unsigned int cut = 1; cut <= reads; cut++) {
		struct model model;

		initialize_model(&model);
		memset(&reader, 0, sizeof(reader));
		model.reader = &reader;
		model.short_read = cut;
		CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
		if (payload_mm_fmp_owner_prepared_reader_load_authorization(&reader,
			&model.binding, &output) == CB_SUCCESS)
			CHECK(payload_mm_fmp_owner_prepared_reader_prove_loaded(&reader,
				&model.grant) == CB_ERR);
		else
			CHECK(payload_mm_fmp_owner_prepared_reader_prove_loaded(&reader,
				&model.grant) == CB_ERR);
	}
}

static void platform_success_and_replay(void)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };
	struct payload_mm_fmp_owner_prepared_reader wrong_mode = { 0 };
	struct model model;

	initialize_platform_model(&model);
	model.reader = &reader;
	CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
	CHECK(initialize_reader(&model, &wrong_mode) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&wrong_mode,
		&model.grant) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_platform(&reader,
		&model.grant) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_platform(&reader,
		&model.grant) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove(&reader,
		&model.grant) == CB_ERR);
	initialize_model(&model);
	memset(&wrong_mode, 0, sizeof(wrong_mode));
	CHECK(initialize_reader(&model, &wrong_mode) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_platform(&wrong_mode,
		&model.grant) == CB_ERR);
}

static void expect_platform_failure(struct model *model)
{
	struct payload_mm_fmp_owner_prepared_reader reader = { 0 };

	model->reader = &reader;
	CHECK(initialize_reader(model, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_platform(&reader,
		&model->grant) == CB_ERR);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_platform(&reader,
		&model->grant) == CB_ERR);
}

static void platform_mutations(void)
{
	struct model model;
	struct payload_mm_fmp_owner_platform_receipt *receipt;

	initialize_platform_model(&model);
	receipt = (void *)material(&model, 0, 1);
	receipt->revision = 1;
	expect_platform_failure(&model);
	initialize_platform_model(&model);
	receipt = (void *)material(&model, 0, 1);
	receipt->capsule_size--;
	expect_platform_failure(&model);
	initialize_platform_model(&model);
	receipt = (void *)material(&model, 0, 1);
	receipt->digest_algorithm++;
	expect_platform_failure(&model);
	initialize_platform_model(&model);
	receipt = (void *)material(&model, 0, 1);
	receipt->capsule_digest[0] ^= 1;
	expect_platform_failure(&model);
	initialize_platform_model(&model);
	model.media[model.layout.state[0].offset + SLOT_SIZE +
		PAYLOAD_MM_FMP_OWNER_PLATFORM_MIN_SLOT_SIZE] = 0;
	expect_platform_failure(&model);
	initialize_platform_model(&model);
	model.grant.policy_revision = CAPSULE_TPM_ANCHOR_POLICY_REVISION;
	expect_platform_failure(&model);
	initialize_platform_model(&model);
	model.short_read = 3;
	expect_platform_failure(&model);
	initialize_platform_model(&model);
	model.mutate_read = 5;
	expect_platform_failure(&model);
	initialize_platform_model(&model);
	memcpy(model.media + model.layout.state[1].offset,
		model.media + model.layout.state[0].offset + SLOT_SIZE, SLOT_SIZE);
	expect_platform_failure(&model);
}

static void platform_interruption_matrix(void)
{
	struct payload_mm_fmp_owner_prepared_reader reader;
	struct model baseline;
	unsigned int reads;

	initialize_platform_model(&baseline);
	memset(&reader, 0, sizeof(reader));
	baseline.reader = &reader;
	CHECK(initialize_reader(&baseline, &reader) == CB_SUCCESS);
	CHECK(payload_mm_fmp_owner_prepared_reader_prove_platform(&reader,
		&baseline.grant) == CB_SUCCESS);
	reads = baseline.reads;
	for (unsigned int cut = 1; cut <= reads; cut++) {
		struct model model;

		initialize_platform_model(&model);
		memset(&reader, 0, sizeof(reader));
		model.reader = &reader;
		model.short_read = cut;
		CHECK(initialize_reader(&model, &reader) == CB_SUCCESS);
		CHECK(payload_mm_fmp_owner_prepared_reader_prove_platform(&reader,
			&model.grant) == CB_ERR);
	}
}

int main(void)
{
	success_and_replay();
	hostile_cases();
	terminal_epoch();
	invalid_initialization();
	authorized_success_and_replay();
	authorized_mutations();
	authorized_domain_vector();
	authorized_double_read_mutation();
	divergent_composite_current_duplicate();
	authorized_aliases();
	authorized_interruption_matrix();
	platform_success_and_replay();
	platform_mutations();
	platform_interruption_matrix();
	return 0;
}
