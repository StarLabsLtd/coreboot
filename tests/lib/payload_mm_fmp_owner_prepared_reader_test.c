/* SPDX-License-Identifier: GPL-2.0-only */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "payload_mm_fmp_owner_prepared_reader_internal.h"

#define SLOT_SIZE 512U
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
	unsigned int reads;
	unsigned int short_read;
	unsigned int mutate_read;
	unsigned int mutate_policy_read;
	unsigned int mutate_control_read;
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
			{ .offset = 0x2000, .size = DOMAIN_SIZE },
		},
		.smmstore = { .offset = 0x3000, .size = 0x100 },
		.route = {{
			.image_offset = 0,
			.flash_offset = 0x4000,
			.size = 0x100,
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
	prepared(&model, 0, 1)->current = model.current;
	prepared(&model, 0, 1)->candidate = model.candidate;
	memcpy(&model.grant.current, &model.current, sizeof(model.current));
	memcpy(&model.grant.candidate, &model.candidate,
		sizeof(model.candidate));
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

int main(void)
{
	success_and_replay();
	hostile_cases();
	terminal_epoch();
	invalid_initialization();
	return 0;
}
