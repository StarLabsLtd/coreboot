/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar.h>
#include <commonlib/bsd/helpers.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "payload_mm_fmp_owner_internal.h"

static uint8_t communication[4096] __aligned(4096);
static uint8_t smram[4096] __aligned(4096);
static const guid_t state_guid = GUID_INIT(0x975cd0e6, 0xc540, 0x4e2b,
	0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d);

struct store {
	struct payload_mm_fmp_owner_record record[5];
	bool read_fails;
	bool commit_fails;
	bool commit_lies;
	bool corrupt_readback;
	bool mutate_inputs;
	unsigned int reads;
	unsigned int commits;
};

struct backend_context {
	struct store *store;
	uint32_t route;
};

static char trace[16];
static size_t trace_size;
static bool protect_ok = true;
static struct backend_context *mutate_source;
static bool mutate_authority;

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

static void check_identity(const struct payload_mm_fmp_state_identity *identity,
	uint32_t key)
{
	static const char *const names[] = {
		"FmpState", "FmpVersion", "FmpLsv", "LastAttemptStatus",
		"LastAttemptVersion",
	};
	size_t name_length;

	assert(!memcmp(&identity->namespace_guid, &state_guid, sizeof(state_guid)));
	assert(!identity->hardware_instance);
	assert(identity->trusted_lowest_version == 4);
	assert(key < ARRAY_SIZE(names));
	name_length = strlen(names[key]);
	assert(identity->variable_name_bytes ==
		(name_length + 1) * sizeof(uint16_t));
	for (size_t i = 0; i < name_length; i++)
		assert(identity->variable_name[i] == (uint16_t)names[key][i]);
	assert(!identity->variable_name[name_length]);
}

static enum cb_err read_record(const void *opaque,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	const struct backend_context *context = opaque;
	struct store *store;

	assert(context->route == 0x4f574e52U);
	assert(key < ARRAY_SIZE(context->store->record));
	check_identity(identity, key);
	store = context->store;
	mark('R');
	store->reads++;
	if (store->read_fails)
		return CB_ERR;
	*record = store->record[key];
	if (store->corrupt_readback && store->commits)
		record->sequence++;
	if (store->mutate_inputs)
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
	assert(!memcmp(current, &store->record[key], sizeof(*current)));
	if (store->commit_fails)
		return CB_ERR;
	if (!store->commit_lies)
		store->record[key] = *candidate;
	if (store->mutate_inputs) {
		((struct payload_mm_fmp_state_identity *)identity)->hardware_instance++;
		((struct payload_mm_fmp_owner_record *)current)->sequence++;
		((struct payload_mm_fmp_owner_record *)candidate)->sequence++;
	}
	return CB_SUCCESS;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	assert(storage != NULL);
	assert(size >= sizeof(struct payload_mm_fmp_state_policy));
	if (mutate_source && size > PAYLOAD_MM_FMP_OWNER_CONTEXT_SIZE)
		mutate_source->route = 0;
	if (mutate_authority && size > PAYLOAD_MM_FMP_OWNER_CONTEXT_SIZE)
		memset((void *)storage, 0xa5, size);
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

static struct payload_mm_fmp_owner_record state_record(uint64_t sequence)
{
	struct payload_mm_fmp_owner_record record = {
		.sequence = sequence,
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

static struct payload_mm_fmp_owner_record legacy_record(uint64_t sequence)
{
	struct payload_mm_fmp_owner_record record = {
		.sequence = sequence,
		.present = 1,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = sizeof(uint32_t),
	};

	record.data[0] = 7;
	return record;
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

static struct payload_mm_fmp_owner_backend backend(
	struct backend_context *context)
{
	return (struct payload_mm_fmp_owner_backend) {
		.revision = PAYLOAD_MM_FMP_OWNER_REVISION,
		.size = sizeof(struct payload_mm_fmp_owner_backend),
		.read = read_record,
		.commit = commit_record,
		.context = context,
		.context_size = sizeof(*context),
	};
}

static void install(struct backend_context *context)
{
	struct payload_mm_fmp_owner_backend port = backend(context);

	install_parent_authority();
	assert(payload_mm_fmp_owner_install(&port, protected_storage, NULL) ==
		CB_SUCCESS);
}

static void expect_trace(const char *expected)
{
	assert(trace_size == strlen(expected));
	assert(!memcmp(trace, expected, trace_size));
}

static void run_install_case(const char *name, struct backend_context *context)
{
	struct payload_mm_fmp_owner_backend port = backend(context);

	if (strcmp(name, "install-no-state"))
		install_parent_authority();
	else {
		struct payload_mm_authvar_contract authvar = contract();

		assert(payload_mm_authvar_authority_install(&authvar,
			protected_storage, NULL) == CB_SUCCESS);
	}
	if (!strcmp(name, "install-revision"))
		port.revision++;
	else if (!strcmp(name, "install-size"))
		port.size--;
	else if (!strcmp(name, "install-read"))
		port.read = NULL;
	else if (!strcmp(name, "install-commit"))
		port.commit = NULL;
	else if (!strcmp(name, "install-context-null"))
		port.context = NULL;
	else if (!strcmp(name, "install-context-zero"))
		port.context_size = 0;
	else if (!strcmp(name, "install-context-large"))
		port.context_size = PAYLOAD_MM_FMP_OWNER_CONTEXT_SIZE + 1U;
	else if (!strcmp(name, "install-protection"))
		protect_ok = false;
	else if (!strcmp(name, "install-authority-failure")) {
		mutate_authority = true;
		protect_ok = false;
	}
	assert(payload_mm_fmp_owner_install(&port, protected_storage, NULL) ==
		CB_ERR);
	protect_ok = true;
	assert(payload_mm_fmp_owner_install(&port, protected_storage, NULL) ==
		CB_ERR);
}

static bool is_install_case(const char *name)
{
	return !strncmp(name, "install-", 8);
}

static void run_case(const char *name)
{
	struct store store = { 0 };
	struct backend_context context = {
		.store = &store,
		.route = 0x4f574e52U,
	};
	struct payload_mm_fmp_owner_record *current =
		(void *)(smram + 512);
	struct payload_mm_fmp_owner_record *candidate =
		(void *)(smram + 1024);
	struct payload_mm_fmp_owner_record *output =
		(void *)(smram + 1536);

	store.record[PAYLOAD_MM_FMP_STATE_KEY_STATE] = state_record(7);
	for (size_t i = PAYLOAD_MM_FMP_STATE_KEY_VERSION;
	     i <= PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION; i++)
		store.record[i] = legacy_record(10U + i);
	if (is_install_case(name)) {
		run_install_case(name, &context);
		return;
	}
	if (!strcmp(name, "source-mutation"))
		mutate_source = &context;
	if (!strcmp(name, "authority-mutation"))
		mutate_authority = true;
	install(&context);
	context.route = 0;
	if (!strcmp(name, "read-failure"))
		store.read_fails = true;
	else if (!strcmp(name, "bad-sequence"))
		store.record[0].sequence = 0;
	else if (!strcmp(name, "bad-present"))
		store.record[0].present = 2;
	else if (!strcmp(name, "bad-attributes"))
		store.record[0].attributes = 0;
	else if (!strcmp(name, "bad-size"))
		store.record[0].data_size--;
	else if (!strcmp(name, "bad-reserved"))
		store.record[0].reserved = 1;
	else if (!strcmp(name, "bad-reserved2"))
		store.record[0].reserved2 = 1;
	else if (!strcmp(name, "bad-validity"))
		store.record[0].data[0] = 2;
	else if (!strcmp(name, "bad-absent"))
		store.record[0].present = 0;
	else if (!strcmp(name, "bad-legacy-tail"))
		store.record[1].data[4] = 1;
	else if (!strcmp(name, "commit-failure"))
		store.commit_fails = true;
	else if (!strcmp(name, "commit-lie"))
		store.commit_lies = true;
	else if (!strcmp(name, "readback-corrupt"))
		store.corrupt_readback = true;
	else if (!strcmp(name, "input-mutation"))
		store.mutate_inputs = true;

	if (!strcmp(name, "read") || !strcmp(name, "source-mutation") ||
	    !strcmp(name, "authority-mutation")) {
		assert(payload_mm_fmp_owner_read(0, output) == CB_SUCCESS);
		assert(!memcmp(output, &store.record[0], sizeof(*output)));
		expect_trace("R");
		return;
	}
	if (!strcmp(name, "read-absent")) {
		store.record[0] = (struct payload_mm_fmp_owner_record) {
			.sequence = 9,
		};
		assert(payload_mm_fmp_owner_read(0, output) == CB_SUCCESS);
		assert(!memcmp(output, &store.record[0], sizeof(*output)));
		expect_trace("R");
		return;
	}
	if (!strcmp(name, "read-legacy")) {
		for (uint32_t key = PAYLOAD_MM_FMP_STATE_KEY_VERSION;
		     key <= PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION; key++) {
			assert(payload_mm_fmp_owner_read(key, output) == CB_SUCCESS);
			assert(!memcmp(output, &store.record[key], sizeof(*output)));
		}
		expect_trace("RRRR");
		return;
	}
	if (!strcmp(name, "read-outside")) {
		assert(payload_mm_fmp_owner_read(0, (void *)communication) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "read-key")) {
		assert(payload_mm_fmp_owner_read(5, output) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "read-misaligned")) {
		assert(payload_mm_fmp_owner_read(0, (void *)(smram + 1537)) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "bad-legacy-tail")) {
		assert(payload_mm_fmp_owner_read(1, output) == CB_ERR);
		expect_trace("R");
		return;
	}
	if (!strcmp(name, "read-failure") || !strncmp(name, "bad-", 4)) {
		assert(payload_mm_fmp_owner_read(0, output) == CB_ERR);
		expect_trace("R");
		return;
	}

	*current = store.record[0];
	*candidate = *current;
	candidate->sequence++;
	candidate->data[2] = 1;
	candidate->data[3] = 1;
	if (!strcmp(name, "state-regression"))
		candidate->data[1] = 0;
	else if (!strcmp(name, "lsv-regression"))
		candidate->data[8] = 3;
	else if (!strcmp(name, "trusted-floor")) {
		memset(current->data, 0, sizeof(current->data));
		current->present = 0;
		current->attributes = 0;
		current->data_size = 0;
		candidate->data[8] = 3;
	}
	if (!strcmp(name, "validity-regressions")) {
		for (size_t i = 0; i < 4; i++) {
			memset(current->data, 0, sizeof(current->data));
			current->data[i] = 1;
			if (i == 1)
				current->data[8] = 4;
			*candidate = *current;
			candidate->sequence++;
			candidate->data[i] = 0;
			assert(payload_mm_fmp_owner_commit_state(current, candidate) ==
				CB_ERR);
		}
		expect_trace("");
		return;
	}
	if (!strcmp(name, "sequence-gap"))
		candidate->sequence++;
	else if (!strcmp(name, "sequence-wrap")) {
		current->sequence = UINT64_MAX;
		candidate->sequence = 0;
	}

	if (!strcmp(name, "remove")) {
		*current = store.record[1];
		assert(payload_mm_fmp_owner_remove_legacy(1, current) == CB_SUCCESS);
		assert(!store.record[1].present && store.record[1].sequence == 12);
		expect_trace("CR");
		return;
	}
	if (!strcmp(name, "remove-state") || !strcmp(name, "remove-absent") ||
	    !strcmp(name, "remove-wrap")) {
		*current = store.record[1];
		if (!strcmp(name, "remove-absent"))
			current->present = 0;
		if (!strcmp(name, "remove-wrap"))
			current->sequence = UINT64_MAX;
		assert(payload_mm_fmp_owner_remove_legacy(
			!strcmp(name, "remove-state") ? 0 : 1, current) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "outside")) {
		assert(payload_mm_fmp_owner_commit_state((void *)communication,
			candidate) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "misaligned")) {
		assert(payload_mm_fmp_owner_commit_state((void *)(smram + 513),
			candidate) == CB_ERR);
		expect_trace("");
		return;
	}
	if (!strcmp(name, "alias")) {
		assert(payload_mm_fmp_owner_commit_state(current, current) == CB_ERR);
		expect_trace("");
		return;
	}

	if (!strcmp(name, "success")) {
		assert(payload_mm_fmp_owner_commit_state(current, candidate) ==
			CB_SUCCESS);
		expect_trace("CR");
		assert(store.record[0].sequence == 8);
		return;
	}
	assert(payload_mm_fmp_owner_commit_state(current, candidate) == CB_ERR);
	if (!strcmp(name, "commit-failure") || !strcmp(name, "input-mutation"))
		expect_trace("C");
	else if (!strcmp(name, "commit-lie") ||
		 !strcmp(name, "readback-corrupt"))
		expect_trace("CR");
	else
		expect_trace("");
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	run_case(argv[1]);
	return 0;
}
