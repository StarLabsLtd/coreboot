/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar.h>
#include <commonlib/bsd/helpers.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "payload_mm_fmp_checkpoint_internal.h"

static uint8_t communication[4096] __aligned(4096);
static uint8_t smram[4096] __aligned(4096);
static const guid_t state_guid = GUID_INIT(0x975cd0e6, 0xc540, 0x4e2b,
	0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d);

struct store {
	struct payload_mm_fmp_owner_record record;
	unsigned int reads;
	unsigned int commits;
	unsigned int fail_read;
	bool commit_fails;
	bool commit_lies;
	bool corrupt_readback;
	bool mutate_read_identity;
	bool mutate_commit_inputs;
};

struct backend_context {
	struct store *store;
	uint32_t route;
};

static char trace[32];
static size_t trace_size;
static unsigned int grants;
static uint64_t granted_transaction;
static uint32_t granted_version;
static uint64_t granted_sequence;
static uint8_t granted_digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];
static bool grant_fails;
static bool grant_live;
static bool binding;
static bool protect_authority = true;
static bool protect_workspace = true;
static bool mutate_binding_authority;
static bool mutate_binding_workspace;
static bool dispatch_ready = true;
static bool dispatch_buffer_available = true;
static bool broker_ready = true;
static bool broker_buffer_available = true;
static bool installing_owner;
static const void *owner_storage;
static size_t owner_storage_size;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static struct payload_mm_fmp_checkpoint_workspace *workspace(void)
{
	return (void *)(smram + 2048);
}

static void mark(char event)
{
	assert(trace_size < sizeof(trace));
	trace[trace_size++] = event;
}

static void check_identity(const struct payload_mm_fmp_state_identity *identity,
	uint32_t key)
{
	static const uint16_t name[] = {
		'F', 'm', 'p', 'S', 't', 'a', 't', 'e', 0,
	};

	assert(key == PAYLOAD_MM_FMP_STATE_KEY_STATE);
	assert(!memcmp(&identity->namespace_guid, &state_guid, sizeof(state_guid)));
	assert(!identity->hardware_instance);
	assert(identity->trusted_lowest_version == 4);
	assert(identity->variable_name_bytes == sizeof(name));
	assert(!memcmp(identity->variable_name, name, sizeof(name)));
}

static enum cb_err read_record(const void *opaque,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	const struct backend_context *context = opaque;
	struct store *store = context->store;

	assert(context->route == 0x4f574e52U);
	check_identity(identity, key);
	mark('R');
	store->reads++;
	if (store->reads == store->fail_read)
		return CB_ERR;
	*record = store->record;
	if (store->corrupt_readback && store->commits)
		record->sequence++;
	if (store->mutate_read_identity)
		((struct payload_mm_fmp_state_identity *)identity)->hardware_instance++;
	return CB_SUCCESS;
}

static enum cb_err commit_record(const void *opaque,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate)
{
	const struct backend_context *context = opaque;
	struct store *store = context->store;

	assert(context->route == 0x4f574e52U);
	check_identity(identity, key);
	mark('C');
	store->commits++;
	assert(!memcmp(current, &store->record, sizeof(*current)));
	assert(candidate->sequence == current->sequence + 1);
	if (store->commit_fails)
		return CB_ERR;
	if (!store->commit_lies)
		store->record = *candidate;
	if (store->mutate_commit_inputs) {
		((struct payload_mm_fmp_state_identity *)identity)->hardware_instance++;
		((struct payload_mm_fmp_owner_record *)current)->sequence++;
		((struct payload_mm_fmp_owner_record *)candidate)->sequence++;
	}
	return CB_SUCCESS;
}

static enum cb_err record_grant(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version)
{
	mark('G');
	assert(generation == 9 && transaction && attempted_version >= 4);
	grants++;
	granted_transaction = transaction;
	granted_version = attempted_version;
	if (grant_fails || grant_live)
		return CB_ERR;
	grant_live = true;
	return CB_SUCCESS;
}

enum cb_err capsule_broker_checkpoint_grant_bound(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version,
	uint64_t authenticated_sequence, uint64_t checkpoint_sequence,
	const uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE])
{
	assert(authenticated_sequence >= 7);
	assert(checkpoint_sequence == authenticated_sequence ||
		checkpoint_sequence == authenticated_sequence + 1);
	assert(digest != NULL);
	granted_sequence = checkpoint_sequence;
	memcpy(granted_digest, digest, sizeof(granted_digest));
	return record_grant(generation, transaction,
		attempted_version);
}

static enum cb_err checkpoint(struct store *store, uint64_t generation,
	uint64_t transaction, uint32_t attempted_version)
{
	uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];

	memset(digest, 0x5a, sizeof(digest));
	return payload_mm_fmp_checkpoint_commit_bound(generation, transaction,
		attempted_version, &store->record, digest);
}

bool payload_mm_fmp_dispatch_ready(void)
{
	return dispatch_ready;
}

bool payload_mm_fmp_dispatch_buffer_available(const void *buffer, size_t size)
{
	(void)buffer;
	(void)size;
	return dispatch_buffer_available;
}

bool capsule_broker_generation_matches(uint64_t generation)
{
	return broker_ready && generation == 9;
}

bool capsule_broker_buffer_available(const void *buffer, size_t size)
{
	(void)buffer;
	(void)size;
	return broker_buffer_available;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	assert(storage != NULL);
	if (installing_owner) {
		owner_storage = storage;
		owner_storage_size = size;
	}
	if (!binding)
		return true;
	if (storage == workspace()) {
		assert(size == sizeof(*workspace()));
		if (mutate_binding_workspace)
			memset((void *)storage, 0xa5, size);
		return protect_workspace;
	}
	if (mutate_binding_authority)
		memset((void *)storage, 0xa5, size);
	return protect_authority;
}

static struct payload_mm_authvar_contract contract(void)
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

static struct payload_mm_fmp_owner_record initial_record(void)
{
	struct payload_mm_fmp_owner_record record = {
		.sequence = 7,
		.present = 1,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE,
	};

	record.data[0] = 1;
	record.data[1] = 1;
	record.data[4] = 3;
	record.data[8] = 4;
	return record;
}

static void install_parent(struct backend_context *context, bool owner)
{
	struct payload_mm_authvar_contract authvar = contract();
	struct payload_mm_fmp_state_policy policy = {
		.revision = PAYLOAD_MM_FMP_STATE_POLICY_REVISION,
		.size = sizeof(policy),
		.namespace_guid = state_guid,
		.trusted_lowest_version = 4,
	};
	struct payload_mm_fmp_owner_backend backend = {
		.revision = PAYLOAD_MM_FMP_OWNER_REVISION,
		.size = sizeof(backend),
		.read = read_record,
		.commit = commit_record,
		.context = context,
		.context_size = sizeof(*context),
	};

	assert(payload_mm_authvar_authority_install(&authvar, protected_storage,
		NULL) == CB_SUCCESS);
	assert(payload_mm_fmp_state_policy_install(&policy, protected_storage,
		NULL) == CB_SUCCESS);
	if (owner) {
		installing_owner = true;
		assert(payload_mm_fmp_owner_install(&backend, protected_storage,
			NULL) == CB_SUCCESS);
		installing_owner = false;
		assert(owner_storage != NULL);
	}
}

static enum cb_err bind(struct payload_mm_fmp_checkpoint_workspace *area,
	uint64_t generation)
{
	enum cb_err status;

	binding = true;
	status = payload_mm_fmp_checkpoint_owner_bind(generation, area,
		protected_storage, NULL);
	binding = false;
	return status;
}

static void expect_trace(const char *expected)
{
	assert(trace_size == strlen(expected));
	assert(!memcmp(trace, expected, trace_size));
}

static void expect_workspace_clear(void)
{
	const uint8_t *bytes = (const void *)workspace();
	uint8_t bits = 0;

	for (size_t i = 0; i < sizeof(*workspace()); i++)
		bits |= bytes[i];
	assert(bits == 0);
}

static bool install_case(const char *name, struct backend_context *context)
{
	struct payload_mm_fmp_checkpoint_workspace *area = workspace();
	const bool checkpoint_range_case =
		!strncmp(name, "install-checkpoint-", 19);

	if (strncmp(name, "install-", 8))
		return false;
	if (!strcmp(name, "install-no-owner"))
		install_parent(context, false);
	else
		install_parent(context, true);
	if (checkpoint_range_case) {
		size_t authority_size;
		const uint8_t *authority =
			payload_mm_fmp_checkpoint_test_authority(&authority_size);
		uint8_t snapshot[64];
		bool overlap = true;

		assert(authority_size <= sizeof(snapshot));
		if (!strcmp(name, "install-checkpoint-full"))
			area = (void *)authority;
		else if (!strcmp(name, "install-checkpoint-leading"))
			area = (void *)((uintptr_t)authority - 8);
		else if (!strcmp(name, "install-checkpoint-trailing"))
			area = (void *)((uintptr_t)authority + authority_size - 8);
		else if (!strcmp(name, "install-checkpoint-adjacent-before")) {
			area = (void *)((uintptr_t)authority - sizeof(*area));
			overlap = false;
		} else if (!strcmp(name, "install-checkpoint-adjacent-after")) {
			area = (void *)((uintptr_t)authority + authority_size);
			overlap = false;
		} else {
			assert(false);
		}
		assert(payload_mm_fmp_checkpoint_test_storage_overlaps(area,
			sizeof(*area)) == overlap);
		memcpy(snapshot, authority, authority_size);
		assert(bind(area, 9) == CB_ERR);
		if (overlap) {
			assert(!memcmp(snapshot, authority, authority_size));
			assert(bind(workspace(), 9) == CB_SUCCESS);
			expect_workspace_clear();
		} else {
			assert(bind(workspace(), 9) == CB_ERR);
		}
		expect_trace("");
		assert(!context->store->reads && !context->store->commits && !grants);
		return true;
	}
	if (!strcmp(name, "install-zero-generation")) {
		assert(bind(area, 0) == CB_ERR);
	} else if (!strcmp(name, "install-generation-mismatch")) {
		assert(bind(area, 8) == CB_ERR);
	} else if (!strcmp(name, "install-null")) {
		assert(bind(NULL, 9) == CB_ERR);
	} else if (!strcmp(name, "install-misaligned")) {
		assert(bind((void *)((uint8_t *)area + 1), 9) == CB_ERR);
	} else if (!strcmp(name, "install-outside")) {
		assert(bind((void *)communication, 9) == CB_ERR);
	} else if (!strcmp(name, "install-no-proof")) {
		binding = true;
		assert(payload_mm_fmp_checkpoint_owner_bind(9, area, NULL, NULL) ==
			CB_ERR);
		binding = false;
	} else if (!strcmp(name, "install-authority-unprotected")) {
		protect_authority = false;
		assert(bind(area, 9) == CB_ERR);
	} else if (!strcmp(name, "install-workspace-unprotected")) {
		protect_workspace = false;
		assert(bind(area, 9) == CB_ERR);
	} else if (!strcmp(name, "install-authority-mutation-failure")) {
		mutate_binding_authority = true;
		protect_authority = false;
		assert(bind(area, 9) == CB_ERR);
	} else if (!strcmp(name, "install-workspace-mutation-failure")) {
		mutate_binding_workspace = true;
		protect_workspace = false;
		assert(bind(area, 9) == CB_ERR);
	} else if (!strcmp(name, "install-dispatch-not-ready")) {
		dispatch_ready = false;
		assert(bind(area, 9) == CB_ERR);
	} else if (!strcmp(name, "install-broker-not-ready")) {
		broker_ready = false;
		assert(bind(area, 9) == CB_ERR);
	} else if (!strcmp(name, "install-dispatch-overlap")) {
		dispatch_buffer_available = false;
		assert(bind(area, 9) == CB_ERR);
	} else if (!strcmp(name, "install-broker-overlap")) {
		broker_buffer_available = false;
		assert(bind(area, 9) == CB_ERR);
	} else {
		assert(!strcmp(name, "install-no-owner"));
		assert(bind(area, 9) == CB_ERR);
	}
	protect_authority = true;
	protect_workspace = true;
	dispatch_ready = true;
	dispatch_buffer_available = true;
	broker_ready = true;
	broker_buffer_available = true;
	assert(bind(area, 9) == CB_ERR);
	assert(checkpoint(context->store, 9, 1, 6) == CB_ERR);
	expect_trace("");
	assert(!context->store->reads && !context->store->commits && !grants);
	return true;
}

static void run_case(const char *name)
{
	struct store store = { .record = initial_record() };
	struct backend_context context = {
		.store = &store,
		.route = 0x4f574e52U,
	};
	uint32_t version = !strcmp(name, "wide-version") ? 0x12345678U : 6U;

	if (install_case(name, &context))
		return;
	install_parent(&context, true);
	if (!strcmp(name, "bind-authority-mutation"))
		mutate_binding_authority = true;
	if (!strcmp(name, "bind-workspace-mutation"))
		mutate_binding_workspace = true;
	assert(bind(workspace(), 9) == CB_SUCCESS);
	context.route = 0;
	expect_workspace_clear();

	if (!strcmp(name, "commit-failure"))
		store.commit_fails = true;
	else if (!strcmp(name, "commit-lie"))
		store.commit_lies = true;
	else if (!strcmp(name, "read-failure"))
		store.fail_read = 1;
	else if (!strcmp(name, "owner-readback-failure"))
		store.fail_read = 2;
	else if (!strcmp(name, "checkpoint-readback-failure"))
		store.fail_read = 3;
	else if (!strcmp(name, "readback-corrupt"))
		store.corrupt_readback = true;
	else if (!strcmp(name, "read-input-mutation"))
		store.mutate_read_identity = true;
	else if (!strcmp(name, "commit-input-mutation"))
		store.mutate_commit_inputs = true;
	else if (!strcmp(name, "bad-attributes"))
		store.record.attributes = 0;
	else if (!strcmp(name, "bad-size"))
		store.record.data_size--;
	else if (!strcmp(name, "bad-reserved"))
		store.record.reserved = 1;
	else if (!strcmp(name, "bad-reserved2"))
		store.record.reserved2 = 1;
	else if (!strcmp(name, "bad-sequence"))
		store.record.sequence = 0;
	else if (!strcmp(name, "bad-present"))
		store.record.present = 2;
	else if (!strcmp(name, "absent")) {
		memset(&store.record, 0, sizeof(store.record));
		store.record.sequence = 8;
	} else if (!strcmp(name, "bad-validity"))
		store.record.data[0] = 2;
	else if (!strcmp(name, "sequence-wrap"))
		store.record.sequence = UINT64_MAX;
	else if (!strcmp(name, "durable-lsv"))
		store.record.data[8] = 7;
	else if (!strcmp(name, "idempotent") ||
		 !strcmp(name, "idempotent-sequence-max")) {
		store.record.data[2] = 1;
		store.record.data[3] = 1;
		store.record.data[12] = 1;
		store.record.data[16] = 6;
		if (!strcmp(name, "idempotent-sequence-max"))
			store.record.sequence = UINT64_MAX;
	}

	if (!strcmp(name, "wrong-generation")) {
		assert(checkpoint(&store, 8, 1, 6) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "bound-sequence")) {
		uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE] = { 1 };

		assert(payload_mm_fmp_checkpoint_commit_bound(9, 1, 6,
			&(struct payload_mm_fmp_owner_record) {
				.sequence = store.record.sequence + 1,
			}, digest) == CB_ERR);
		expect_trace("R");
		assert(!grants);
		expect_workspace_clear();
		return;
	}
	if (!strcmp(name, "bound-record")) {
		struct payload_mm_fmp_owner_record expected = store.record;
		uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE] = { 1 };

		expected.data[0] ^= 1;
		assert(payload_mm_fmp_checkpoint_commit_bound(9, 1, 6,
			&expected, digest) == CB_ERR);
		expect_trace("R");
		assert(!grants);
		expect_workspace_clear();
		return;
	}
	if (!strcmp(name, "bound-success")) {
		uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];

		memset(digest, 0x5a, sizeof(digest));
		assert(payload_mm_fmp_checkpoint_commit_bound(9, 1, 6,
			&store.record, digest) == CB_SUCCESS);
		expect_trace("RCRRG");
		assert(granted_sequence == store.record.sequence);
		assert(!memcmp(granted_digest, digest, sizeof(digest)));
		expect_workspace_clear();
		return;
	}
	if (!strcmp(name, "zero-transaction")) {
		assert(checkpoint(&store, 9, 0, 6) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "low-version") || !strcmp(name, "durable-lsv")) {
		uint32_t low = !strcmp(name, "low-version") ? 3 : 6;

		assert(checkpoint(&store, 9, 1, low) == CB_ERR);
		expect_trace("R");
		expect_workspace_clear();
		return;
	}
	if (!strcmp(name, "grant-failure"))
		grant_fails = true;
	if (!strcmp(name, "replay-after-failure")) {
		store.fail_read = 1;
		assert(checkpoint(&store, 9, 1, 6) == CB_ERR);
		store.fail_read = 0;
		assert(checkpoint(&store, 9, 1, 6) == CB_ERR);
		assert(checkpoint(&store, 9, 2, 6) == CB_SUCCESS);
		expect_trace("RRCRRG");
		expect_workspace_clear();
		return;
	}
	if (!strcmp(name, "outstanding-grant")) {
		struct payload_mm_fmp_owner_record committed;

		assert(checkpoint(&store, 9, 1, 6) == CB_SUCCESS);
		committed = store.record;
		assert(checkpoint(&store, 9, 2, 7) == CB_ERR);
		assert(!memcmp(&committed, &store.record, sizeof(committed)));
		expect_trace("RCRRG");
		assert(grants == 1 && granted_transaction == 1 && granted_version == 6);
		expect_workspace_clear();
		return;
	}
	if (!strcmp(name, "success") || !strcmp(name, "wide-version") ||
	    !strcmp(name, "bind-authority-mutation") ||
	    !strcmp(name, "bind-workspace-mutation") ||
	    !strcmp(name, "grant-failure") || !strcmp(name, "max-transaction")) {
		uint64_t transaction = !strcmp(name, "max-transaction") ?
			UINT64_MAX : 1;
		enum cb_err status = checkpoint(&store, 9, transaction, version);

		assert(status == (grant_fails ? CB_ERR : CB_SUCCESS));
		expect_trace("RCRRG");
		assert(store.commits == 1 && grants == 1);
		assert(store.record.sequence == 8);
		assert(!memcmp(store.record.data + 16, &version, sizeof(version)));
		assert(checkpoint(&store, 9, transaction, version) ==
			CB_ERR);
		expect_workspace_clear();
		return;
	}
	if (!strcmp(name, "idempotent") ||
	    !strcmp(name, "idempotent-sequence-max")) {
		assert(checkpoint(&store, 9, 1, 6) == CB_SUCCESS);
		expect_trace("RRG");
		assert(!store.commits && grants == 1);
		expect_workspace_clear();
		return;
	}

	assert(checkpoint(&store, 9, 1, 6) == CB_ERR);
	assert(!grants);
	if (!strcmp(name, "commit-failure") ||
	    !strcmp(name, "commit-input-mutation"))
		expect_trace("RC");
	else if (!strcmp(name, "commit-lie") ||
		 !strcmp(name, "owner-readback-failure") ||
		 !strcmp(name, "readback-corrupt"))
		expect_trace("RCR");
	else if (!strcmp(name, "checkpoint-readback-failure"))
		expect_trace("RCRR");
	else if (!strcmp(name, "bad-sequence"))
		expect_trace("");
	else
		expect_trace("R");
	expect_workspace_clear();
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	run_case(argv[1]);
	return 0;
}
