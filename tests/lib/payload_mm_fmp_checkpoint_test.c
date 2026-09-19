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
	struct payload_mm_fmp_checkpoint_record record;
	bool read_fails;
	bool commit_fails;
	bool commit_lies;
	bool readback_fails;
	bool corrupt_readback;
	bool mutate_first_read_identity;
	bool mutate_readback_identity;
	bool mutate_commit_inputs;
	unsigned int reads;
	unsigned int commits;
};

struct backend_context {
	struct store *store;
	uint32_t route;
};

static char trace[16];
static size_t trace_size;
static unsigned int grants;
static uint32_t granted_version;
static bool grant_fails;
static bool grant_valid;
static uint64_t granted_transaction;
static bool protect_ok = true;
static struct backend_context *mutate_source;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static void mark(char event)
{
	assert(trace_size < sizeof(trace));
	trace[trace_size++] = event;
}

static void check_identity(const struct payload_mm_fmp_state_identity *identity)
{
	static const uint16_t name[] = {
		'F', 'm', 'p', 'S', 't', 'a', 't', 'e', 0,
	};

	assert(!memcmp(&identity->namespace_guid, &state_guid, sizeof(state_guid)));
	assert(!identity->hardware_instance);
	assert(identity->trusted_lowest_version == 4);
	assert(identity->variable_name_bytes == sizeof(name));
	assert(!memcmp(identity->variable_name, name, sizeof(name)));
}

static enum cb_err read_state(void *opaque,
	const struct payload_mm_fmp_state_identity *identity,
	struct payload_mm_fmp_checkpoint_record *record)
{
	struct backend_context *context = opaque;
	struct store *store;

	assert(context->route == 0x46504d31U);
	check_identity(identity);
	store = context->store;
	mark('R');
	store->reads++;
	if (store->read_fails || (store->readback_fails && store->reads > 1))
		return CB_ERR;
	*record = store->record;
	if ((store->mutate_first_read_identity && store->reads == 1) ||
	    (store->mutate_readback_identity && store->reads > 1))
		((struct payload_mm_fmp_state_identity *)identity)->hardware_instance++;
	if (store->corrupt_readback && store->reads > 1)
		record->data[16]++;
	return CB_SUCCESS;
}

static enum cb_err commit_state(void *opaque,
	const struct payload_mm_fmp_state_identity *identity,
	const struct payload_mm_fmp_checkpoint_record *current,
	const struct payload_mm_fmp_checkpoint_record *candidate)
{
	struct backend_context *context = opaque;
	struct store *store = context->store;

	assert(context->route == 0x46504d31U);
	check_identity(identity);
	mark('C');
	store->commits++;
	assert(!memcmp(current, &store->record, sizeof(*current)));
	assert(candidate->sequence == current->sequence + 1);
	assert(candidate->attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
	assert(candidate->data_size == PAYLOAD_MM_FMP_STATE_WIRE_SIZE);
	assert(candidate->data[2] == 1 && candidate->data[3] == 1);
	assert(candidate->data[12] == 1 && !candidate->data[13] &&
		!candidate->data[14] && !candidate->data[15]);
	if (store->commit_fails)
		return CB_ERR;
	if (!store->commit_lies)
		store->record = *candidate;
	if (store->mutate_commit_inputs) {
		((struct payload_mm_fmp_state_identity *)identity)->hardware_instance++;
		((struct payload_mm_fmp_checkpoint_record *)current)->sequence++;
		((struct payload_mm_fmp_checkpoint_record *)candidate)->sequence++;
	}
	return CB_SUCCESS;
}

enum cb_err capsule_broker_checkpoint_grant(uint64_t generation,
	uint64_t transaction, uint32_t attempted_version)
{
	mark('G');
	assert(generation == 9);
	assert(transaction);
	assert(attempted_version >= 4);
	granted_version = attempted_version;
	grants++;
	if (grant_fails || grant_valid)
		return CB_ERR;
	grant_valid = true;
	granted_transaction = transaction;
	return CB_SUCCESS;
}

static bool consume_grant(uint64_t generation, uint64_t transaction,
	uint32_t attempted_version)
{
	if (!grant_valid || generation != 9 || transaction != granted_transaction ||
	    attempted_version != granted_version)
		return false;
	grant_valid = false;
	return true;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	assert(storage != NULL);
	assert(size >= sizeof(struct payload_mm_fmp_state_policy));
	if (mutate_source && size > PAYLOAD_MM_FMP_CHECKPOINT_CONTEXT_SIZE)
		mutate_source->route = 0;
	return protect_ok;
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

static struct payload_mm_fmp_checkpoint_record initial_record(void)
{
	struct payload_mm_fmp_checkpoint_record record = {
		.sequence = 7,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE,
	};

	record.data[0] = 1;
	record.data[1] = 1;
	record.data[4] = 3;
	record.data[8] = 4;
	return record;
}

static struct payload_mm_fmp_checkpoint_backend backend(
	struct backend_context *context)
{
	return (struct payload_mm_fmp_checkpoint_backend) {
		.revision = PAYLOAD_MM_FMP_CHECKPOINT_BACKEND_REVISION,
		.size = sizeof(struct payload_mm_fmp_checkpoint_backend),
		.broker_generation = 9,
		.read = read_state,
		.commit = commit_state,
		.context = context,
		.context_size = sizeof(*context),
	};
}

static void install_parent_authority(void)
{
	struct payload_mm_authvar_contract authvar = contract();
	struct payload_mm_fmp_state_policy policy = {
		.revision = PAYLOAD_MM_FMP_STATE_POLICY_REVISION,
		.size = sizeof(policy),
		.namespace_guid = state_guid,
		.trusted_lowest_version = 4,
	};
	assert(payload_mm_authvar_authority_install(&authvar, protected_storage,
		NULL) == CB_SUCCESS);
	assert(payload_mm_fmp_state_policy_install(&policy, protected_storage,
		NULL) == CB_SUCCESS);
}

static void install(struct backend_context *context)
{
	struct payload_mm_fmp_checkpoint_backend port = backend(context);

	install_parent_authority();
	assert(payload_mm_fmp_checkpoint_backend_install(&port, protected_storage,
		NULL) == CB_SUCCESS);
}

static bool install_case(const char *name, struct backend_context *context)
{
	struct payload_mm_fmp_checkpoint_backend port = backend(context);

	if (strncmp(name, "install-", 8))
		return false;
	if (!strcmp(name, "install-no-state")) {
		struct payload_mm_authvar_contract authvar = contract();

		assert(payload_mm_authvar_authority_install(&authvar,
			protected_storage, NULL) == CB_SUCCESS);
	} else {
		install_parent_authority();
	}
	if (!strcmp(name, "install-revision"))
		port.revision++;
	else if (!strcmp(name, "install-size"))
		port.size--;
	else if (!strcmp(name, "install-generation"))
		port.broker_generation = 0;
	else if (!strcmp(name, "install-read"))
		port.read = NULL;
	else if (!strcmp(name, "install-commit"))
		port.commit = NULL;
	else if (!strcmp(name, "install-context-null"))
		port.context = NULL;
	else if (!strcmp(name, "install-context-zero"))
		port.context_size = 0;
	else if (!strcmp(name, "install-context-large"))
		port.context_size = PAYLOAD_MM_FMP_CHECKPOINT_CONTEXT_SIZE + 1U;
	else if (!strcmp(name, "install-protection"))
		protect_ok = false;
	assert(payload_mm_fmp_checkpoint_backend_install(&port, protected_storage,
		NULL) == CB_ERR);
	protect_ok = true;
	assert(payload_mm_fmp_checkpoint_backend_install(&port, protected_storage,
		NULL) == CB_ERR);
	assert(payload_mm_fmp_checkpoint_commit(9, 1, 6) == CB_ERR);
	return true;
}

static void expect_trace(const char *expected)
{
	assert(trace_size == strlen(expected));
	assert(!memcmp(trace, expected, trace_size));
}

static void run_case(const char *name)
{
	struct store store = { .record = initial_record() };
	struct backend_context context = {
		.store = &store,
		.route = 0x46504d31U,
	};
	uint32_t attempted_version = !strcmp(name, "wide-version") ?
		0x12345678U : 6U;

	if (install_case(name, &context))
		return;
	if (!strcmp(name, "source-mutation"))
		mutate_source = &context;
	install(&context);
	context.route = 0;
	if (!strcmp(name, "commit-failure"))
		store.commit_fails = true;
	else if (!strcmp(name, "commit-lie"))
		store.commit_lies = true;
	else if (!strcmp(name, "read-failure"))
		store.read_fails = true;
	else if (!strcmp(name, "readback-failure"))
		store.readback_fails = true;
	else if (!strcmp(name, "readback-corrupt"))
		store.corrupt_readback = true;
	else if (!strcmp(name, "read-input-mutation"))
		store.mutate_first_read_identity = true;
	else if (!strcmp(name, "readback-input-mutation"))
		store.mutate_readback_identity = true;
	else if (!strcmp(name, "commit-input-mutation"))
		store.mutate_commit_inputs = true;
	else if (!strcmp(name, "bad-attributes"))
		store.record.attributes = 0;
	else if (!strcmp(name, "bad-size"))
		store.record.data_size--;
	else if (!strcmp(name, "bad-reserved"))
		store.record.reserved = 1;
	else if (!strcmp(name, "bad-sequence"))
		store.record.sequence = 0;
	else if (!strcmp(name, "sequence-wrap"))
		store.record.sequence = UINT64_MAX;
	else if (!strcmp(name, "bad-validity"))
		store.record.data[0] = 2;
	else if (!strcmp(name, "idempotent") ||
		 !strcmp(name, "idempotent-sequence-max")) {
		if (!strcmp(name, "idempotent-sequence-max"))
			store.record.sequence = UINT64_MAX;
		store.record.data[2] = 1;
		store.record.data[3] = 1;
		store.record.data[12] = 1;
		store.record.data[16] = 6;
	} else if (!strcmp(name, "durable-lsv")) {
		store.record.data[8] = 7;
	}

	if (!strcmp(name, "wrong-generation")) {
		assert(payload_mm_fmp_checkpoint_commit(8, 1, 6) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "zero-transaction")) {
		assert(payload_mm_fmp_checkpoint_commit(9, 0, 6) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "low-version")) {
		assert(payload_mm_fmp_checkpoint_commit(9, 1, 3) == CB_ERR);
		expect_trace("R");
		return;
	}
	if (!strcmp(name, "durable-lsv")) {
		assert(payload_mm_fmp_checkpoint_commit(9, 1, 6) == CB_ERR);
		expect_trace("R");
		return;
	}
	if (!strcmp(name, "grant-failure"))
		grant_fails = true;
	if (!strcmp(name, "outstanding-grant")) {
		struct payload_mm_fmp_checkpoint_record checkpoint;

		assert(payload_mm_fmp_checkpoint_commit(9, 1, 6) == CB_SUCCESS);
		checkpoint = store.record;
		assert(payload_mm_fmp_checkpoint_commit(9, 2, 7) == CB_ERR);
		expect_trace("RCRG");
		assert(store.reads == 2 && store.commits == 1 && grants == 1);
		assert(!memcmp(&store.record, &checkpoint, sizeof(checkpoint)));
		assert(consume_grant(9, 1, 6));
		return;
	}
	if (!strcmp(name, "replay-after-failure")) {
		store.commit_fails = true;
		assert(payload_mm_fmp_checkpoint_commit(9, 1, 6) == CB_ERR);
		store.commit_fails = false;
		assert(payload_mm_fmp_checkpoint_commit(9, 1, 6) == CB_ERR);
		assert(payload_mm_fmp_checkpoint_commit(9, 2, 6) == CB_SUCCESS);
		expect_trace("RCRCRG");
		assert(store.commits == 2 && grants == 1);
		return;
	}

	if (!strcmp(name, "success") || !strcmp(name, "wide-version") ||
	    !strcmp(name, "source-mutation") || !strcmp(name, "grant-failure") ||
	    !strcmp(name, "max-transaction")) {
		uint64_t transaction = !strcmp(name, "max-transaction") ?
			UINT64_MAX : 1;
		enum cb_err result = payload_mm_fmp_checkpoint_commit(9, transaction,
			attempted_version);

		assert(result == (grant_fails ? CB_ERR : CB_SUCCESS));
		expect_trace("RCRG");
		assert(store.commits == 1 && grants == 1);
		assert(store.record.sequence == 8);
		assert(store.record.data[16] == (uint8_t)attempted_version);
		assert(store.record.data[17] == (uint8_t)(attempted_version >> 8));
		assert(store.record.data[18] == (uint8_t)(attempted_version >> 16));
		assert(store.record.data[19] == (uint8_t)(attempted_version >> 24));
		assert(granted_version == attempted_version);
		assert(payload_mm_fmp_checkpoint_commit(9,
			transaction == UINT64_MAX ? transaction : transaction + 1,
			attempted_version) == CB_ERR);
		assert(store.reads == 2 && store.commits == 1 && grants == 1);
		return;
	}
	if (!strcmp(name, "idempotent") ||
	    !strcmp(name, "idempotent-sequence-max")) {
		assert(payload_mm_fmp_checkpoint_commit(9, 1, 6) == CB_SUCCESS);
		expect_trace("RRG");
		assert(!store.commits && grants == 1);
		assert(store.record.sequence ==
			(!strcmp(name, "idempotent-sequence-max") ? UINT64_MAX : 7));
		return;
	}

	assert(payload_mm_fmp_checkpoint_commit(9, 1, 6) == CB_ERR);
	assert(!grants);
	if (!strcmp(name, "commit-failure")) {
		expect_trace("RC");
		assert(store.record.sequence == 7);
		assert(!store.record.data[2] && !store.record.data[3]);
	} else if (!strcmp(name, "commit-lie") ||
		 !strcmp(name, "readback-failure") ||
		 !strcmp(name, "readback-corrupt") ||
		 !strcmp(name, "readback-input-mutation"))
		expect_trace("RCR");
	else if (!strcmp(name, "commit-input-mutation"))
		expect_trace("RC");
	else
		expect_trace("R");
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	run_case(argv[1]);
	return 0;
}
