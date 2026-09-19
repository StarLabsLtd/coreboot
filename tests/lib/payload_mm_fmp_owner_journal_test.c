/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar.h>
#include <commonlib/bsd/helpers.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "payload_mm_fmp_checkpoint_internal.h"
#include "payload_mm_fmp_owner_journal_internal.h"

#define SLOT_SIZE 512U
#define SLOTS 3U
#define DOMAIN_SIZE (SLOT_SIZE * SLOTS)
#define TEST_OWNER_JOURNAL_MAGIC 0x314c4e4a504d4d50ULL
#define FACTORY_FIRST_POST_PROVISION_READ 58U
#define FACTORY_RECOVER_MANIFEST_READ 66U

static uint8_t communication[4096] __aligned(4096);
static uint8_t smram[16384] __aligned(4096);
static const guid_t state_guid = GUID_INIT(0x975cd0e6, 0xc540, 0x4e2b,
	0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d);

enum fault_kind {
	FAULT_NONE,
	FAULT_BEFORE,
	FAULT_PARTIAL,
	FAULT_AFTER,
	FAULT_MUTATE_INPUT,
	FAULT_MUTATE_CONTEXT,
	FAULT_DIGEST,
	FAULT_OUTPUT,
};

struct model {
	uint8_t media[2 * DOMAIN_SIZE];
	struct payload_mm_fmp_owner_journal_anchor anchor;
	enum fault_kind program_fault;
	enum fault_kind erase_fault;
	enum fault_kind sync_fault;
	enum fault_kind advance_fault;
	enum fault_kind hash_fault;
	enum fault_kind read_fault;
	enum fault_kind anchor_read_fault;
	enum fault_kind provision_fault;
	unsigned int program_cut;
	unsigned int erase_cut;
	unsigned int sync_cut;
	unsigned int advance_cut;
	unsigned int read_cut;
	unsigned int hash_cut;
	unsigned int anchor_read_cut;
	unsigned int provision_cut;
	unsigned int programs;
	unsigned int erases;
	unsigned int syncs;
	unsigned int advances;
	unsigned int reads;
	unsigned int hashes;
	unsigned int anchor_reads;
	unsigned int provisions;
	bool reenter;
	bool fault_hit;
	void *mutate_target;
	size_t mutate_size;
	size_t mutate_offset;
	unsigned int mutate_on_read;
	unsigned int mutate_on_anchor_read;
};

struct callback_context {
	struct model *model;
	uint32_t route;
};

struct workspace {
	struct payload_mm_fmp_owner_backend backend;
	struct payload_mm_fmp_owner_record record[5];
	struct payload_mm_fmp_owner_record current;
	struct payload_mm_fmp_owner_record candidate;
	struct payload_mm_fmp_owner_record output;
	struct payload_mm_fmp_checkpoint_workspace checkpoint;
};

static struct model model;
static struct callback_context source_context;
static struct payload_mm_fmp_owner_backend *backend;
static struct payload_mm_fmp_state_identity identity;
static const void *journal_storage;
static size_t journal_storage_size;
static bool grant_called;

bool payload_mm_fmp_dispatch_ready(void)
{
	return true;
}

bool payload_mm_fmp_dispatch_buffer_available(const void *buffer, size_t size)
{
	return payload_mm_fmp_state_staging_buffer(buffer, size);
}

bool capsule_broker_generation_matches(uint64_t generation)
{
	return generation == 9;
}

bool capsule_broker_buffer_available(const void *buffer, size_t size)
{
	return payload_mm_fmp_state_staging_buffer(buffer, size);
}

enum cb_err capsule_broker_checkpoint_grant_bound(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version,
	uint64_t authenticated_sequence, uint64_t checkpoint_sequence,
	const uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE])
{
	uint8_t bits = 0;

	for (size_t i = 0; i < PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE; i++)
		bits |= digest[i];
	assert(generation == 9 && transaction == 1 && attempted_version == 5);
	assert(authenticated_sequence == 1 && checkpoint_sequence == 2 && bits);
	grant_called = true;
	return CB_SUCCESS;
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static bool anchors_equal(
	const struct payload_mm_fmp_owner_journal_anchor *left,
	const struct payload_mm_fmp_owner_journal_anchor *right)
{
	return left->epoch == right->epoch &&
		!memcmp(left->digest, right->digest, sizeof(left->digest));
}

static struct workspace *workspace(void)
{
	return (void *)(smram + 2048);
}

static void check_context(const struct callback_context *context)
{
	assert(context->model == &model);
	assert(context->route == 0x4a4e4c31U);
}

static void try_reentry(struct model *state)
{
	if (!state->reenter || backend == NULL)
		return;
	assert(backend->read(NULL, &identity, 0, &workspace()->output) == CB_ERR);
	assert(backend->commit(NULL, &identity, 0, &workspace()->current,
		&workspace()->candidate) == CB_ERR);
}

static enum cb_err media_read(const void *opaque, uint64_t offset,
	void *buffer, size_t size)
{
	const struct callback_context *context = opaque;
	struct model *state;

	check_context(context);
	state = context->model;
	state->reads++;
	try_reentry(state);
	if (state->mutate_target && state->reads == state->mutate_on_read) {
		assert(state->mutate_offset < state->mutate_size);
		((uint8_t *)state->mutate_target)[state->mutate_offset] ^= 1;
		state->fault_hit = true;
	}
	if (state->read_cut && state->reads == state->read_cut)
		state->fault_hit = true;
	if (state->read_cut && state->reads == state->read_cut &&
	    state->read_fault == FAULT_BEFORE)
		return CB_ERR;
	assert(offset <= sizeof(state->media));
	assert(size <= sizeof(state->media) - offset);
	memcpy(buffer, state->media + offset, size);
	if (state->read_cut && state->reads == state->read_cut &&
	    state->read_fault == FAULT_OUTPUT)
		((uint8_t *)buffer)[0] ^= 1;
	if (state->read_cut && state->reads == state->read_cut &&
	    state->read_fault == FAULT_MUTATE_CONTEXT)
		((struct callback_context *)context)->route = 0;
	return state->read_cut && state->reads == state->read_cut &&
		state->read_fault == FAULT_AFTER ? CB_ERR : CB_SUCCESS;
}

static enum cb_err media_program(const void *opaque, uint64_t offset,
	const void *buffer, size_t size)
{
	struct callback_context *context = (void *)opaque;
	struct model *state;
	bool fault;

	check_context(context);
	state = context->model;
	state->programs++;
	fault = state->program_cut && state->programs == state->program_cut;
	state->fault_hit |= fault;
	try_reentry(state);
	assert(offset <= sizeof(state->media));
	assert(size <= sizeof(state->media) - offset);
	if (fault && state->program_fault == FAULT_BEFORE)
		return CB_ERR;
	if (fault && state->program_fault == FAULT_PARTIAL) {
		memcpy(state->media + offset, buffer, size / 2);
		return CB_ERR;
	}
	for (size_t i = 0; i < size; i++) {
		const uint8_t value = ((const uint8_t *)buffer)[i];

		assert((state->media[offset + i] & value) == value);
		state->media[offset + i] = value;
	}
	if (fault && state->program_fault == FAULT_MUTATE_INPUT)
		((uint8_t *)buffer)[0] ^= 1;
	if (fault && state->program_fault == FAULT_MUTATE_CONTEXT)
		context->route = 0;
	return fault && state->program_fault == FAULT_AFTER ? CB_ERR : CB_SUCCESS;
}

static enum cb_err media_erase(const void *opaque, uint64_t offset,
	size_t size)
{
	struct callback_context *context = (void *)opaque;
	struct model *state;
	bool fault;

	check_context(context);
	state = context->model;
	state->erases++;
	fault = state->erase_cut && state->erases == state->erase_cut;
	state->fault_hit |= fault;
	try_reentry(state);
	assert(offset <= sizeof(state->media));
	assert(size <= sizeof(state->media) - offset);
	if (fault && state->erase_fault == FAULT_BEFORE)
		return CB_ERR;
	if (fault && state->erase_fault == FAULT_PARTIAL) {
		memset(state->media + offset, 0xff, size / 2);
		return CB_ERR;
	}
	memset(state->media + offset, 0xff, size);
	if (fault && state->erase_fault == FAULT_MUTATE_CONTEXT)
		context->route = 0;
	return fault && state->erase_fault == FAULT_AFTER ? CB_ERR : CB_SUCCESS;
}

static enum cb_err media_sync(const void *opaque)
{
	struct callback_context *context = (void *)opaque;
	struct model *state;
	bool fault;

	check_context(context);
	state = context->model;
	state->syncs++;
	fault = state->sync_cut && state->syncs == state->sync_cut;
	state->fault_hit |= fault;
	try_reentry(state);
	if (fault && state->sync_fault == FAULT_MUTATE_CONTEXT)
		context->route = 0;
	return fault ? CB_ERR : CB_SUCCESS;
}

static void test_hash(const void *data, size_t size, uint8_t digest[32])
{
	uint32_t value = 2166136261U;

	for (size_t i = 0; i < size; i++)
		value = (value ^ ((const uint8_t *)data)[i]) * 16777619U;
	for (size_t i = 0; i < 32; i++) {
		value = value * 1103515245U + 12345U + (uint32_t)i;
		digest[i] = (uint8_t)(value >> ((i & 3U) * 8U));
	}
}

static enum cb_err sha256(const void *opaque, const void *buffer, size_t size,
	uint8_t digest[32])
{
	struct callback_context *context = (void *)opaque;
	struct model *state;
	bool fault;

	check_context(context);
	state = context->model;
	state->hashes++;
	fault = state->hash_cut && state->hashes == state->hash_cut;
	state->fault_hit |= fault;
	try_reentry(state);
	if (fault && state->hash_fault == FAULT_BEFORE)
		return CB_ERR;
	test_hash(buffer, size, digest);
	if (fault && state->hash_fault == FAULT_DIGEST)
		digest[0] ^= 1;
	if (fault && state->hash_fault == FAULT_MUTATE_INPUT)
		((uint8_t *)buffer)[0] ^= 1;
	if (fault && state->hash_fault == FAULT_MUTATE_CONTEXT)
		context->route = 0;
	return fault && state->hash_fault == FAULT_AFTER ? CB_ERR : CB_SUCCESS;
}

static enum cb_err anchor_read(const void *opaque,
	struct payload_mm_fmp_owner_journal_anchor *anchor)
{
	struct callback_context *context = (void *)opaque;
	struct model *state;
	bool fault;

	check_context(context);
	state = context->model;
	state->anchor_reads++;
	fault = state->anchor_read_cut &&
		state->anchor_reads == state->anchor_read_cut;
	state->fault_hit |= fault;
	try_reentry(state);
	if (state->mutate_target &&
	    state->anchor_reads == state->mutate_on_anchor_read) {
		assert(state->mutate_offset < state->mutate_size);
		((uint8_t *)state->mutate_target)[state->mutate_offset] ^= 1;
		state->fault_hit = true;
	}
	if (fault && state->anchor_read_fault == FAULT_BEFORE)
		return CB_ERR;
	*anchor = state->anchor;
	if (fault && state->anchor_read_fault == FAULT_OUTPUT)
		anchor->digest[0] ^= 1;
	if (fault && state->anchor_read_fault == FAULT_MUTATE_CONTEXT)
		context->route = 0;
	return fault && state->anchor_read_fault == FAULT_AFTER ? CB_ERR :
		CB_SUCCESS;
}

static enum cb_err anchor_advance(const void *opaque,
	const struct payload_mm_fmp_owner_journal_anchor *current,
	const struct payload_mm_fmp_owner_journal_anchor *candidate)
{
	struct callback_context *context = (void *)opaque;
	struct model *state;
	bool fault;

	check_context(context);
	state = context->model;
	state->advances++;
	fault = state->advance_cut && state->advances == state->advance_cut;
	state->fault_hit |= fault;
	try_reentry(state);
	if (!anchors_equal(&state->anchor, current))
		return CB_ERR;
	if (fault && state->advance_fault == FAULT_BEFORE)
		return CB_ERR;
	state->anchor = *candidate;
	if (fault && state->advance_fault == FAULT_MUTATE_INPUT)
		((struct payload_mm_fmp_owner_journal_anchor *)current)->epoch++;
	if (fault && state->advance_fault == FAULT_MUTATE_CONTEXT)
		context->route = 0;
	return fault && state->advance_fault == FAULT_AFTER ? CB_ERR : CB_SUCCESS;
}

static enum cb_err anchor_provision(const void *opaque,
	const struct payload_mm_fmp_owner_journal_anchor *candidate)
{
	struct callback_context *context = (void *)opaque;
	struct model *state;
	bool fault;

	check_context(context);
	state = context->model;
	state->provisions++;
	fault = state->provision_cut && state->provisions == state->provision_cut;
	state->fault_hit |= fault;
	try_reentry(state);
	if (state->anchor.epoch ||
	    (fault && state->provision_fault == FAULT_BEFORE))
		return CB_ERR;
	state->anchor = *candidate;
	if (fault && state->provision_fault == FAULT_MUTATE_INPUT)
		((struct payload_mm_fmp_owner_journal_anchor *)candidate)->epoch++;
	if (fault && state->provision_fault == FAULT_MUTATE_CONTEXT)
		context->route = 0;
	return fault && state->provision_fault == FAULT_AFTER ? CB_ERR : CB_SUCCESS;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	assert(storage != NULL);
	if (size > PAYLOAD_MM_FMP_OWNER_JOURNAL_CONTEXT_SIZE) {
		journal_storage = storage;
		journal_storage_size = size;
	}
	return true;
}

static struct payload_mm_authvar_contract authvar_contract(void)
{
	return (struct payload_mm_authvar_contract) {
		.revision = PAYLOAD_MM_AUTHVAR_REVISION,
		.size = sizeof(struct payload_mm_authvar_contract),
		.flags = PAYLOAD_MM_AUTHVAR_REQUIRED_FLAGS,
		.smm_address_bits = 64,
		.generation = 1,
		.smram = { (uintptr_t)smram, sizeof(smram) },
		.communication = { (uintptr_t)communication, sizeof(communication) },
		.boot_media_size = 0x1000000,
		.store_offset = 0x600000,
		.store_size = 0x60000,
		.block_size = 0x1000,
		.erase_size = 0x10000,
	};
}

static struct payload_mm_fmp_owner_record record(uint32_t key,
	uint64_t sequence)
{
	struct payload_mm_fmp_owner_record value = {
		.sequence = sequence,
		.present = 1,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = key == 0 ? PAYLOAD_MM_FMP_STATE_WIRE_SIZE :
			sizeof(uint32_t),
	};

	if (key == 0) {
		value.data[0] = 1;
		value.data[1] = 1;
		value.data[4] = 3;
		value.data[8] = 4;
	} else {
		value.data[0] = (uint8_t)(key + 3U);
	}
	return value;
}

static struct payload_mm_fmp_owner_journal_port port(void)
{
	struct payload_mm_fmp_owner_journal_port value = {
		.revision = PAYLOAD_MM_FMP_OWNER_JOURNAL_REVISION,
		.size = sizeof(value),
		.slot_size = SLOT_SIZE,
		.domain = {
			{ .offset = 0, .size = DOMAIN_SIZE },
			{ .offset = DOMAIN_SIZE, .size = DOMAIN_SIZE },
		},
		.read = media_read,
		.program = media_program,
		.erase = media_erase,
		.sync = media_sync,
		.sha256 = sha256,
		.anchor_read = anchor_read,
		.anchor_advance = anchor_advance,
		.anchor_provision = anchor_provision,
		.context = &source_context,
		.context_size = sizeof(source_context),
	};

	for (size_t i = 0; i < sizeof(value.storage_domain); i++)
		value.storage_domain[i] = (uint8_t)(i + 1U);
	return value;
}

static void install(bool provision)
{
	struct payload_mm_authvar_contract authvar = authvar_contract();
	struct payload_mm_fmp_state_policy policy = {
		.revision = PAYLOAD_MM_FMP_STATE_POLICY_REVISION,
		.size = sizeof(policy),
		.namespace_guid = state_guid,
		.trusted_lowest_version = 4,
	};
	struct payload_mm_fmp_owner_journal_port journal_port = port();

	memset(&model, 0, sizeof(model));
	memset(model.media, 0xff, sizeof(model.media));
	source_context = (struct callback_context) {
		.model = &model,
		.route = 0x4a4e4c31U,
	};
	assert(payload_mm_authvar_authority_install(&authvar, protected_storage,
		NULL) == CB_SUCCESS);
	assert(payload_mm_fmp_state_policy_install(&policy, protected_storage,
		NULL) == CB_SUCCESS);
	assert(payload_mm_fmp_owner_journal_install(&journal_port,
		protected_storage, NULL) == CB_SUCCESS);
	assert(journal_storage != NULL && journal_storage_size != 0);
	backend = &workspace()->backend;
	assert(payload_mm_fmp_owner_journal_backend(backend) == CB_SUCCESS);
	for (uint32_t key = 0; key < ARRAY_SIZE(workspace()->record); key++)
		workspace()->record[key] = record(key, 1);
	if (provision)
		assert(payload_mm_fmp_owner_journal_factory_provision(
			workspace()->record) == CB_SUCCESS);
	assert(payload_mm_fmp_state_identity_get_for_key(0, &identity) ==
		CB_SUCCESS);
}

static void reset_counts(void)
{
	model.programs = 0;
	model.erases = 0;
	model.syncs = 0;
	model.advances = 0;
	model.reads = 0;
	model.hashes = 0;
	model.anchor_reads = 0;
	model.provisions = 0;
	model.fault_hit = false;
}

static void owner_install(void)
{
	assert(payload_mm_fmp_owner_install(backend, protected_storage, NULL) ==
		CB_SUCCESS);
}

static void expect_sequence(uint64_t sequence)
{
	assert(payload_mm_fmp_owner_read(0, &workspace()->output) == CB_SUCCESS);
	assert(workspace()->output.sequence == sequence);
}

static enum cb_err commit_next(void)
{
	assert(payload_mm_fmp_owner_read(0, &workspace()->current) == CB_SUCCESS);
	workspace()->candidate = workspace()->current;
	workspace()->candidate.sequence++;
	workspace()->candidate.data[2] = 1;
	return payload_mm_fmp_owner_commit_state(&workspace()->current,
		&workspace()->candidate);
}

static void fill_domain(void)
{
	assert(commit_next() == CB_SUCCESS);
	assert(commit_next() == CB_SUCCESS);
	expect_sequence(3);
}

static enum fault_kind fault_from_name(const char *name)
{
	if (strstr(name, "before"))
		return FAULT_BEFORE;
	if (strstr(name, "partial"))
		return FAULT_PARTIAL;
	if (strstr(name, "after"))
		return FAULT_AFTER;
	if (strstr(name, "input"))
		return FAULT_MUTATE_INPUT;
	if (strstr(name, "context"))
		return FAULT_MUTATE_CONTEXT;
	if (strstr(name, "digest"))
		return FAULT_DIGEST;
	if (strstr(name, "output"))
		return FAULT_OUTPUT;
	return FAULT_NONE;
}

static bool manifest_for_anchor(
	const struct payload_mm_fmp_owner_journal_anchor *anchor,
	struct payload_mm_fmp_owner_journal_manifest *manifest)
{
	bool found = false;

	for (size_t domain = 0; domain < 2; domain++) {
		for (size_t slot = 0; slot < SLOTS; slot++) {
			struct payload_mm_fmp_owner_journal_manifest candidate;
			uint8_t digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
			const uint8_t *address = model.media + domain * DOMAIN_SIZE +
				slot * SLOT_SIZE;

			if (address[0] == 0xff)
				continue;
			memcpy(&candidate, address, sizeof(candidate));
			test_hash(&candidate, sizeof(candidate), digest);
			if (candidate.epoch != anchor->epoch ||
			    memcmp(digest, anchor->digest, sizeof(digest)) != 0)
				continue;
			if (found) {
				assert(!memcmp(manifest, &candidate, sizeof(candidate)));
			} else {
				*manifest = candidate;
			}
			found = true;
		}
	}
	return found;
}

static void factory_expected(
	struct payload_mm_fmp_owner_journal_manifest *manifest,
	struct payload_mm_fmp_owner_journal_anchor *anchor)
{
	struct payload_mm_fmp_state_identity identities[5];

	*manifest = (struct payload_mm_fmp_owner_journal_manifest) {
		.magic = TEST_OWNER_JOURNAL_MAGIC,
		.revision = PAYLOAD_MM_FMP_OWNER_JOURNAL_REVISION,
		.size = sizeof(*manifest),
		.slot_size = SLOT_SIZE,
		.owner_record_size = sizeof(struct payload_mm_fmp_owner_record),
		.owner_record_count = PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS,
		.format = PAYLOAD_MM_FMP_OWNER_JOURNAL_FORMAT,
		.epoch = 1,
	};
	for (uint32_t key = 0; key < ARRAY_SIZE(identities); key++)
		assert(payload_mm_fmp_state_identity_get_for_key(key,
			&identities[key]) == CB_SUCCESS);
	for (size_t i = 0; i < sizeof(manifest->storage_domain); i++)
		manifest->storage_domain[i] = (uint8_t)(i + 1U);
	test_hash(identities, sizeof(identities), manifest->identity_binding);
	memcpy(manifest->record, workspace()->record, sizeof(manifest->record));
	anchor->epoch = manifest->epoch;
	test_hash(manifest, sizeof(*manifest), anchor->digest);
}

static unsigned int callback_count(void)
{
	return model.programs + model.erases + model.syncs + model.advances +
		model.reads + model.hashes + model.anchor_reads + model.provisions;
}

static unsigned int run_factory_cut(const char *name, unsigned int cut)
{
	struct payload_mm_fmp_owner_journal_manifest expected;
	struct payload_mm_fmp_owner_journal_manifest recovered;
	struct payload_mm_fmp_owner_journal_anchor expected_anchor;
	enum cb_err status;
	bool cleared;
	bool candidate;

	install(false);
	/* Factory provisioning must replace arbitrary unauthoritative media. */
	memset(model.media, 0, sizeof(model.media));
	factory_expected(&expected, &expected_anchor);
	reset_counts();
	if (strstr(name, "program")) {
		model.program_fault = fault_from_name(name);
		model.program_cut = cut;
	} else if (strstr(name, "erase")) {
		model.erase_fault = fault_from_name(name);
		model.erase_cut = cut;
	} else if (strstr(name, "sync")) {
		model.sync_fault = fault_from_name(name);
		model.sync_cut = cut;
	} else if (strstr(name, "anchor-read")) {
		model.anchor_read_fault = fault_from_name(name);
		model.anchor_read_cut = cut;
	} else if (strstr(name, "hash")) {
		model.hash_fault = fault_from_name(name);
		model.hash_cut = cut;
	} else if (strstr(name, "read")) {
		model.read_fault = fault_from_name(name);
		model.read_cut = cut;
	} else if (strstr(name, "provision")) {
		model.provision_fault = fault_from_name(name);
		model.provision_cut = cut;
	}
	status = payload_mm_fmp_owner_journal_factory_provision(
		workspace()->record);
	if (strstr(name, "factory-count-") == name) {
		assert(status == CB_SUCCESS);
		if (strstr(name, "anchor-read"))
			return model.anchor_reads;
		if (strstr(name, "program"))
			return model.programs;
		if (strstr(name, "erase"))
			return model.erases;
		if (strstr(name, "sync"))
			return model.syncs;
		if (strstr(name, "hash"))
			return model.hashes;
		if (strstr(name, "read"))
			return model.reads;
		if (strstr(name, "provision"))
			return model.provisions;
		assert(false);
	}
	assert(model.fault_hit);
	cleared = !model.anchor.epoch &&
		!memcmp(model.anchor.digest,
			(uint8_t[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE]) { 0 },
			sizeof(model.anchor.digest));
	candidate = anchors_equal(&model.anchor, &expected_anchor);
	assert(cleared != candidate);
	if (candidate) {
		assert(manifest_for_anchor(&model.anchor, &recovered));
		assert(!memcmp(&recovered, &expected, sizeof(recovered)));
		bool successful_cut =
			!strcmp(name, "factory-cut-provision-after") ||
			!strcmp(name, "factory-cut-erase-after") ||
			(!strcmp(name, "factory-cut-read-output") &&
			 cut >= FACTORY_FIRST_POST_PROVISION_READ &&
			 cut != FACTORY_RECOVER_MANIFEST_READ);

		if (successful_cut) {
			assert(status == CB_SUCCESS);
		} else {
			assert(status == CB_ERR);
		}
	} else {
		assert(status == CB_ERR);
		assert(!manifest_for_anchor(&model.anchor, &recovered));
	}
	if (strstr(name, "context")) {
		unsigned int callbacks = callback_count();

		assert(payload_mm_fmp_owner_journal_factory_provision(
			workspace()->record) == CB_ERR);
		assert(callback_count() == callbacks);
	}
	return 0;
}

static unsigned int run_cut(const char *name, unsigned int cut)
{
	struct payload_mm_fmp_owner_journal_manifest old_manifest;
	struct payload_mm_fmp_owner_journal_manifest candidate_manifest;
	struct payload_mm_fmp_owner_journal_manifest recovered;
	struct payload_mm_fmp_owner_journal_anchor old_anchor;
	struct payload_mm_fmp_owner_journal_anchor candidate_anchor;
	enum cb_err status;
	bool old_outcome;
	bool candidate_outcome;

	install(true);
	owner_install();
	fill_domain();
	assert(payload_mm_fmp_owner_read(0, &workspace()->current) == CB_SUCCESS);
	workspace()->candidate = workspace()->current;
	workspace()->candidate.sequence++;
	workspace()->candidate.data[2] = 1;
	old_anchor = model.anchor;
	assert(manifest_for_anchor(&old_anchor, &old_manifest));
	candidate_manifest = old_manifest;
	candidate_manifest.epoch++;
	candidate_manifest.record[0] = workspace()->candidate;
	candidate_anchor.epoch = candidate_manifest.epoch;
	test_hash(&candidate_manifest, sizeof(candidate_manifest),
		candidate_anchor.digest);
	reset_counts();
	if (strstr(name, "program")) {
		model.program_fault = fault_from_name(name);
		model.program_cut = cut;
	} else if (strstr(name, "erase")) {
		model.erase_fault = fault_from_name(name);
		model.erase_cut = cut;
	} else if (strstr(name, "sync")) {
		model.sync_fault = fault_from_name(name);
		model.sync_cut = cut;
	} else if (strstr(name, "advance")) {
		model.advance_fault = fault_from_name(name);
		model.advance_cut = cut;
	} else if (strstr(name, "anchor-read")) {
		model.anchor_read_fault = fault_from_name(name);
		model.anchor_read_cut = cut;
	} else if (strstr(name, "hash")) {
		model.hash_fault = fault_from_name(name);
		model.hash_cut = cut;
	} else if (strstr(name, "read")) {
		model.read_fault = fault_from_name(name);
		model.read_cut = cut;
	}
	status = payload_mm_fmp_owner_commit_state(&workspace()->current,
		&workspace()->candidate);
	if (strstr(name, "count-") == name) {
		assert(status == CB_SUCCESS);
		if (strstr(name, "anchor-read"))
			return model.anchor_reads;
		if (strstr(name, "hash"))
			return model.hashes;
		if (strstr(name, "read"))
			return model.reads;
	}
	assert(model.fault_hit);
	old_outcome = anchors_equal(&model.anchor, &old_anchor);
	candidate_outcome = anchors_equal(&model.anchor, &candidate_anchor);
	assert(old_outcome != candidate_outcome);
	assert(manifest_for_anchor(&model.anchor, &recovered));
	if (old_outcome) {
		assert(status == CB_ERR);
		assert(!memcmp(&recovered, &old_manifest, sizeof(recovered)));
	} else {
		bool ambiguous_status_allowed = strstr(name, "cut-read-") == name ||
			strstr(name, "cut-anchor-read-") == name ||
			strstr(name, "cut-hash-") == name ||
			!strcmp(name, "cut-advance-context");

		assert(!memcmp(&recovered, &candidate_manifest, sizeof(recovered)));
		assert(status == CB_SUCCESS ||
			(ambiguous_status_allowed && status == CB_ERR));
	}
	return 0;
}

static unsigned int parse_cut(const char *text)
{
	unsigned int value = 0;

	assert(text && *text);
	while (*text) {
		assert(*text >= '0' && *text <= '9');
		value = value * 10U + (unsigned int)(*text - '0');
		text++;
	}
	return value;
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "success";
	unsigned int cut = argc > 2 ? parse_cut(argv[2]) : 1;

	if (!strcmp(name, "absent")) {
		install(false);
		owner_install();
		assert(payload_mm_fmp_owner_read(0, &workspace()->output) == CB_ERR);
		return 0;
	}
	if (!strcmp(name, "success")) {
		install(true);
		owner_install();
		expect_sequence(1);
		assert(commit_next() == CB_SUCCESS);
		expect_sequence(2);
		for (uint32_t key = 1; key < 5; key++) {
			assert(payload_mm_fmp_owner_read(key, &workspace()->output) ==
				CB_SUCCESS);
			assert(workspace()->output.sequence == 1);
		}
		return 0;
	}
	if (!strcmp(name, "checkpoint")) {
		uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];

		install(true);
		owner_install();
		assert(payload_mm_fmp_checkpoint_owner_bind(9,
			&workspace()->checkpoint, protected_storage, NULL) ==
			CB_SUCCESS);
		assert(payload_mm_fmp_owner_read(0, &workspace()->current) ==
			CB_SUCCESS);
		memset(digest, 0x5a, sizeof(digest));
		assert(payload_mm_fmp_checkpoint_commit_bound(9, 1, 5,
			&workspace()->current, digest) == CB_SUCCESS);
		assert(grant_called);
		expect_sequence(2);
		return 0;
	}
	if (!strcmp(name, "reentry")) {
		install(true);
		owner_install();
		model.reenter = true;
		expect_sequence(1);
		assert(commit_next() == CB_SUCCESS);
		return 0;
	}
	if (!strcmp(name, "wrong-identity")) {
		struct payload_mm_fmp_state_identity wrong;

		install(true);
		wrong = identity;
		wrong.hardware_instance++;
		assert(backend->read(NULL, &wrong, 0, &workspace()->output) ==
			CB_ERR);
		return 0;
	}
	if (!strcmp(name, "candidate-toctou")) {
		struct payload_mm_fmp_owner_journal_anchor old_anchor;
		struct payload_mm_fmp_owner_journal_manifest old_manifest;
		struct payload_mm_fmp_owner_journal_manifest recovered;

		install(true);
		owner_install();
		fill_domain();
		assert(payload_mm_fmp_owner_read(0, &workspace()->current) ==
			CB_SUCCESS);
		workspace()->candidate = workspace()->current;
		workspace()->candidate.sequence++;
		old_anchor = model.anchor;
		assert(manifest_for_anchor(&old_anchor, &old_manifest));
		reset_counts();
		model.mutate_target = &workspace()->candidate;
		model.mutate_size = sizeof(workspace()->candidate);
		model.mutate_offset =
			offsetof(struct payload_mm_fmp_owner_record, data) + 3;
		model.mutate_on_read = 1;
		assert(backend->commit(NULL, &identity, 0, &workspace()->current,
			&workspace()->candidate) == CB_ERR);
		assert(model.fault_hit && workspace()->candidate.data[3] == 1);
		assert(payload_mm_fmp_owner_record_valid(0,
			&workspace()->candidate));
		assert(anchors_equal(&model.anchor, &old_anchor));
		assert(manifest_for_anchor(&model.anchor, &recovered));
		assert(!memcmp(&recovered, &old_manifest, sizeof(recovered)));
		return 0;
	}
	if (!strcmp(name, "read-fault")) {
		install(true);
		owner_install();
		reset_counts();
		model.read_cut = cut;
		assert(payload_mm_fmp_owner_read(0, &workspace()->output) == CB_ERR);
		assert(model.fault_hit);
		model.read_cut = 0;
		expect_sequence(1);
		return 0;
	}
	if (!strcmp(name, "wrong-digest")) {
		install(true);
		owner_install();
		model.anchor.digest[0] ^= 1;
		assert(payload_mm_fmp_owner_read(0, &workspace()->output) == CB_ERR);
		return 0;
	}
	if (!strcmp(name, "duplicate")) {
		install(true);
		owner_install();
		memcpy(model.media + DOMAIN_SIZE, model.media, SLOT_SIZE);
		expect_sequence(1);
		return 0;
	}
	if (!strcmp(name, "newer")) {
		struct payload_mm_fmp_owner_journal_manifest *copy;

		install(true);
		owner_install();
		copy = (void *)(model.media + SLOT_SIZE);
		memcpy(copy, model.media, sizeof(*copy));
		copy->epoch++;
		expect_sequence(1);
		return 0;
	}
	if (!strcmp(name, "wrong-domain") || !strcmp(name, "wrong-key")) {
		struct payload_mm_fmp_owner_journal_manifest *stored;

		install(true);
		owner_install();
		stored = (void *)model.media;
		if (!strcmp(name, "wrong-domain"))
			stored->storage_domain[0] ^= 1;
		else
			stored->record[1].data[7] = 1;
		test_hash(stored, sizeof(*stored), model.anchor.digest);
		assert(payload_mm_fmp_owner_read(0, &workspace()->output) == CB_ERR);
		return 0;
	}
	if (!strcmp(name, "exhaustion")) {
		struct payload_mm_fmp_owner_journal_manifest *stored;

		install(true);
		owner_install();
		stored = (void *)model.media;
		stored->epoch = UINT64_MAX;
		test_hash(stored, sizeof(*stored), model.anchor.digest);
		model.anchor.epoch = UINT64_MAX;
		expect_sequence(1);
		assert(commit_next() == CB_ERR);
		return 0;
	}
	if (!strcmp(name, "hash-input")) {
		install(true);
		owner_install();
		reset_counts();
		model.hash_fault = FAULT_MUTATE_INPUT;
		model.hash_cut = 1;
		assert(payload_mm_fmp_owner_read(0, &workspace()->output) == CB_ERR);
		return 0;
	}
	if (!strcmp(name, "program-context")) {
		install(true);
		owner_install();
		fill_domain();
		reset_counts();
		model.program_fault = FAULT_MUTATE_CONTEXT;
		model.program_cut = 1;
		assert(commit_next() == CB_ERR);
		assert(model.fault_hit);
		assert(payload_mm_fmp_owner_read(0, &workspace()->output) == CB_ERR);
		return 0;
	}
	if (!strcmp(name, "source-mutation")) {
		install(true);
		source_context.route = 0;
		owner_install();
		expect_sequence(1);
		return 0;
	}
	if (!strcmp(name, "factory-twice")) {
		install(true);
		assert(payload_mm_fmp_owner_journal_factory_provision(
			workspace()->record) == CB_ERR);
		return 0;
	}
	if (!strcmp(name, "factory-toctou")) {
		install(false);
		reset_counts();
		model.mutate_target = workspace()->record;
		model.mutate_size = sizeof(workspace()->record);
		model.mutate_offset =
			4 * sizeof(struct payload_mm_fmp_owner_record) +
			offsetof(struct payload_mm_fmp_owner_record, data);
		model.mutate_on_anchor_read = 1;
		assert(payload_mm_fmp_owner_journal_factory_provision(
			workspace()->record) == CB_ERR);
		assert(model.fault_hit && workspace()->record[4].data[0] == 6);
		assert(payload_mm_fmp_owner_record_valid(4,
			&workspace()->record[4]));
		assert(!model.anchor.epoch);
		for (size_t i = 0; i < sizeof(model.media); i++)
			assert(model.media[i] == 0xff);
		return 0;
	}
	if (strstr(name, "factory-cut-") == name)
		return (int)run_factory_cut(name, cut);
	if (strstr(name, "factory-count-") == name)
		return (int)run_factory_cut(name, 0);
	if (strstr(name, "cut-") == name) {
		return (int)run_cut(name, cut);
	}
	if (strstr(name, "count-") == name) {
		return (int)run_cut(name, 0);
	}
	return 2;
}
