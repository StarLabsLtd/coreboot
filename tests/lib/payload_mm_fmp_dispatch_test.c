/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "payload_mm_fmp_dispatch_internal.h"

#define COMMUNICATION_SIZE 4096U

static uint8_t communication[COMMUNICATION_SIZE] __aligned(4096);
static struct {
	struct payload_mm_fmp_dispatch_workspace workspace;
	uint8_t current[PAYLOAD_MM_FMP_STATE_WIRE_SIZE] __aligned(8);
} smram __aligned(4096);

static const guid_t state_guid = GUID_INIT(0x975cd0e6, 0xc540, 0x4e2b,
	0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d);
static bool protection_ok = true;
static bool workspace_protection_ok = true;
static bool mutate_workspace_during_proof;
static bool mutate_authority_during_proof;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	assert(storage != NULL && size);
	if (mutate_authority_during_proof && storage != &smram.workspace)
		memset((void *)storage, 0xa5, size);
	if (mutate_workspace_during_proof && storage == &smram.workspace)
		memset(&smram.workspace, 0xa5, sizeof(smram.workspace));
	return protection_ok &&
		(storage != &smram.workspace || workspace_protection_ok);
}

static struct payload_mm_authvar_contract contract(void)
{
	return (struct payload_mm_authvar_contract) {
		.revision = PAYLOAD_MM_AUTHVAR_REVISION,
		.size = sizeof(struct payload_mm_authvar_contract),
		.flags = PAYLOAD_MM_AUTHVAR_REQUIRED_FLAGS,
		.smm_address_bits = 64,
		.generation = 7,
		.smram = { (uintptr_t)&smram, sizeof(smram) },
		.communication = {
			(uintptr_t)communication, sizeof(communication),
		},
		.boot_media_size = 0x1000000,
		.store_offset = 0x600000,
		.store_size = 0x60000,
		.block_size = 0x1000,
		.erase_size = 0x10000,
	};
}

static struct payload_mm_fmp_state_policy policy(void)
{
	return (struct payload_mm_fmp_state_policy) {
		.revision = PAYLOAD_MM_FMP_STATE_POLICY_REVISION,
		.size = sizeof(struct payload_mm_fmp_state_policy),
		.namespace_guid = state_guid,
		.trusted_lowest_version = 4,
	};
}

static struct payload_mm_fmp_state_message message(uint32_t operation,
	uint32_t key, uint64_t transaction)
{
	return (struct payload_mm_fmp_state_message) {
		.revision = PAYLOAD_MM_FMP_STATE_MESSAGE_REVISION,
		.size = sizeof(struct payload_mm_fmp_state_message),
		.operation = operation,
		.key = key,
		.transaction = transaction,
		.data_size = key == PAYLOAD_MM_FMP_STATE_KEY_STATE ?
			PAYLOAD_MM_FMP_STATE_WIRE_SIZE : sizeof(uint32_t),
		.result = PAYLOAD_MM_FMP_STATE_RESULT_PENDING,
	};
}

static uint64_t publish(struct payload_mm_fmp_state_message *state)
{
	struct payload_mm_authvar_request request = {
		.revision = PAYLOAD_MM_AUTHVAR_REQUEST_REVISION,
		.size = sizeof(struct payload_mm_authvar_request),
		.operation = PAYLOAD_MM_AUTHVAR_COMMUNICATE,
		.generation = 7,
		.message_address = (uintptr_t)&communication[512],
		.message_size = sizeof(*state),
	};

	memset(communication, 0, sizeof(communication));
	memcpy(&communication[512], state, sizeof(*state));
	memcpy(communication, &request, sizeof(request));
	return (uintptr_t)communication;
}

static uint64_t publish_at(struct payload_mm_fmp_state_message *state,
	size_t request_offset, size_t message_offset)
{
	struct payload_mm_authvar_request request = {
		.revision = PAYLOAD_MM_AUTHVAR_REQUEST_REVISION,
		.size = sizeof(struct payload_mm_authvar_request),
		.operation = PAYLOAD_MM_AUTHVAR_COMMUNICATE,
		.generation = 7,
		.message_address = (uintptr_t)&communication[message_offset],
		.message_size = sizeof(*state),
	};

	memset(communication, 0, sizeof(communication));
	memcpy(&communication[message_offset], state, sizeof(*state));
	memcpy(&communication[request_offset], &request, sizeof(request));
	return (uintptr_t)&communication[request_offset];
}

static void install_parent(void)
{
	struct payload_mm_authvar_contract authvar = contract();
	struct payload_mm_fmp_state_policy state = policy();

	assert(payload_mm_authvar_authority_install(&authvar, protected_storage,
		NULL) == CB_SUCCESS);
	assert(payload_mm_fmp_state_policy_install(&state, protected_storage,
		NULL) == CB_SUCCESS);
}

static void install_dispatch(void)
{
	install_parent();
	assert(payload_mm_fmp_dispatch_workspace_install(&smram.workspace,
		protected_storage, NULL) == CB_SUCCESS);
}

static bool workspace_zero(void)
{
	const uint8_t *data = (const uint8_t *)&smram.workspace;
	uint8_t bits = 0;

	for (size_t i = 0; i < sizeof(smram.workspace); i++)
		bits |= data[i];
	return bits == 0;
}

static bool install_case(const char *name)
{
	struct payload_mm_fmp_dispatch_workspace outside;
	struct payload_mm_fmp_dispatch_workspace *workspace = &smram.workspace;
	payload_mm_authvar_protected_storage proof = protected_storage;

	if (strncmp(name, "install-", 8))
		return false;
	if (strcmp(name, "install-no-state"))
		install_parent();
	else {
		struct payload_mm_authvar_contract authvar = contract();

		assert(payload_mm_authvar_authority_install(&authvar,
			protected_storage, NULL) == CB_SUCCESS);
	}
	if (!strcmp(name, "install-null"))
		workspace = NULL;
	else if (!strcmp(name, "install-misaligned"))
		workspace = (void *)((uint8_t *)&smram.workspace + 1);
	else if (!strcmp(name, "install-outside"))
		workspace = &outside;
	else if (!strcmp(name, "install-no-proof"))
		proof = NULL;
	else if (!strcmp(name, "install-unprotected"))
		protection_ok = false;
	else if (!strcmp(name, "install-workspace-unprotected"))
		workspace_protection_ok = false;
	else if (!strcmp(name, "install-proof-mutation"))
		mutate_workspace_during_proof = true;
	else if (!strcmp(name, "install-authority-mutation"))
		mutate_authority_during_proof = true;
	if (!strcmp(name, "install-proof-mutation") ||
	    !strcmp(name, "install-authority-mutation")) {
		struct payload_mm_fmp_state_message state = message(
			PAYLOAD_MM_FMP_STATE_READ,
			PAYLOAD_MM_FMP_STATE_KEY_STATE, 1);

		assert(payload_mm_fmp_dispatch_workspace_install(workspace, proof,
			NULL) == CB_SUCCESS);
		assert(workspace_zero());
		assert(payload_mm_fmp_dispatch_prepare(publish(&state), NULL, 0) ==
			CB_SUCCESS);
		assert(payload_mm_fmp_dispatch_complete(1) == CB_SUCCESS);
		return true;
	}
	assert(payload_mm_fmp_dispatch_workspace_install(workspace, proof, NULL) ==
		CB_ERR);
	protection_ok = true;
	assert(payload_mm_fmp_dispatch_workspace_install(&smram.workspace,
		protected_storage, NULL) == CB_ERR);
	assert(payload_mm_fmp_dispatch_prepare((uintptr_t)communication, NULL, 0) ==
		CB_ERR);
	return true;
}

static void successful_snapshot(void)
{
	struct payload_mm_fmp_state_message state = message(
		PAYLOAD_MM_FMP_STATE_READ, PAYLOAD_MM_FMP_STATE_KEY_STATE, 1);
	const struct payload_mm_fmp_state_command *command;
	uint64_t address = publish(&state);

	assert(payload_mm_fmp_dispatch_prepare(address, NULL, 0) == CB_SUCCESS);
	command = payload_mm_fmp_dispatch_command();
	assert(command == &smram.workspace.command);
	assert(command->message.transaction == 1);
	assert(command->variable_name[0] == 'F');
	memset(communication, 0xa5, sizeof(communication));
	assert(command->message.transaction == 1);
	assert(command->variable_name[0] == 'F');
	state.transaction = 2;
	address = publish(&state);
	assert(payload_mm_fmp_dispatch_prepare(address, NULL, 0) == CB_ERR);
	assert(payload_mm_fmp_dispatch_complete(2) == CB_ERR);
	assert(payload_mm_fmp_dispatch_command() == command);
	assert(payload_mm_fmp_dispatch_complete(1) == CB_SUCCESS);
	assert(payload_mm_fmp_dispatch_command() == NULL && workspace_zero());
	assert(payload_mm_fmp_dispatch_prepare(address, NULL, 0) == CB_SUCCESS);
	assert(payload_mm_fmp_dispatch_complete(2) == CB_SUCCESS);
}

static void write_snapshot(void)
{
	struct payload_mm_fmp_state_message state = message(
		PAYLOAD_MM_FMP_STATE_WRITE_STATE, PAYLOAD_MM_FMP_STATE_KEY_STATE, 1);
	const struct payload_mm_fmp_state_command *command;

	state.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES;
	state.data[0] = 1;
	state.data[1] = 1;
	state.data[4] = 4;
	state.data[8] = 4;
	memcpy(smram.current, state.data, sizeof(state.data));
	assert(payload_mm_fmp_dispatch_prepare(publish(&state), smram.current,
		sizeof(smram.current)) == CB_SUCCESS);
	command = payload_mm_fmp_dispatch_command();
	memset(smram.current, 0xa5, sizeof(smram.current));
	memset(communication, 0xa5, sizeof(communication));
	assert(command->message.data[0] == 1 && command->message.data[8] == 4);
	assert(payload_mm_fmp_dispatch_complete(1) == CB_SUCCESS);
}

static void adjacent_snapshot(bool message_before)
{
	struct payload_mm_fmp_state_message state = message(
		PAYLOAD_MM_FMP_STATE_READ, PAYLOAD_MM_FMP_STATE_KEY_STATE, 1);
	size_t request_offset = 128;
	size_t message_offset = message_before ?
		request_offset - sizeof(state) :
		request_offset + sizeof(struct payload_mm_authvar_request);

	assert(payload_mm_fmp_dispatch_prepare(publish_at(&state, request_offset,
		message_offset), NULL, 0) == CB_SUCCESS);
	assert(payload_mm_fmp_dispatch_complete(1) == CB_SUCCESS);
}

static void reject_case(const char *name)
{
	struct payload_mm_fmp_state_message state = message(
		PAYLOAD_MM_FMP_STATE_READ, PAYLOAD_MM_FMP_STATE_KEY_STATE, 1);
	struct payload_mm_authvar_request *request;
	uint64_t address = publish(&state);
	const void *current = NULL;
	size_t current_size = 0;

	request = (void *)(uintptr_t)address;
	if (!strcmp(name, "request-before"))
		address -= 8;
	else if (!strcmp(name, "request-misaligned"))
		address++;
	else if (!strcmp(name, "request-generation"))
		request->generation++;
	else if (!strcmp(name, "request-flags"))
		request->flags = 1;
	else if (!strcmp(name, "message-before"))
		request->message_address = (uintptr_t)communication - 8;
	else if (!strcmp(name, "message-misaligned"))
		request->message_address++;
	else if (!strcmp(name, "message-short"))
		request->message_size--;
	else if (!strcmp(name, "message-large"))
		request->message_size += 8;
	else if (!strcmp(name, "message-request-alias"))
		request->message_address = (uintptr_t)communication;
	else if (!strcmp(name, "message-request-overlap-before"))
		address = publish_at(&state, 128, 88);
	else if (!strcmp(name, "message-request-overlap-after"))
		address = publish_at(&state, 128, 152);
	else if (!strcmp(name, "message-revision")) {
		state.revision++;
		address = publish(&state);
	} else if (!strcmp(name, "current-null-size"))
		current_size = sizeof(smram.current);
	else if (!strcmp(name, "current-no-size"))
		current = smram.current;
	else if (!strcmp(name, "current-workspace-alias")) {
		current = &smram.workspace;
		current_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE;
	} else if (!strcmp(name, "current-misaligned")) {
		current = smram.current + 1;
		current_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE;
	} else if (!strcmp(name, "current-noncanonical")) {
		smram.current[0] = 2;
		current = smram.current;
		current_size = sizeof(smram.current);
	} else if (!strcmp(name, "current-outside")) {
		current = &state;
		current_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE;
	} else
		assert(false);
	assert(payload_mm_fmp_dispatch_prepare(address, current, current_size) ==
		CB_ERR);
	assert(payload_mm_fmp_dispatch_command() == NULL && workspace_zero());
}

static void close_state(void)
{
	struct payload_mm_fmp_state_message state = message(
		PAYLOAD_MM_FMP_STATE_CLOSE_STATE, PAYLOAD_MM_FMP_STATE_KEY_NONE, 1);

	state.data_size = 0;
	assert(payload_mm_fmp_dispatch_prepare(publish(&state), NULL, 0) ==
		CB_SUCCESS);
	assert(payload_mm_fmp_dispatch_complete(1) == CB_SUCCESS);
	state = message(PAYLOAD_MM_FMP_STATE_READ,
		PAYLOAD_MM_FMP_STATE_KEY_STATE, 2);
	assert(payload_mm_fmp_dispatch_prepare(publish(&state), NULL, 0) == CB_ERR);
}

static void max_transaction(void)
{
	struct payload_mm_fmp_state_message state = message(
		PAYLOAD_MM_FMP_STATE_READ, PAYLOAD_MM_FMP_STATE_KEY_STATE,
		UINT64_MAX);

	assert(payload_mm_fmp_dispatch_prepare(publish(&state), NULL, 0) ==
		CB_SUCCESS);
	assert(payload_mm_fmp_dispatch_complete(UINT64_MAX) == CB_SUCCESS);
	state.transaction--;
	assert(payload_mm_fmp_dispatch_prepare(publish(&state), NULL, 0) == CB_ERR);
}

int main(int argc, char **argv)
{
	assert(argc == 2);
	if (install_case(argv[1]))
		return 0;
	install_dispatch();
	if (!strcmp(argv[1], "success"))
		successful_snapshot();
	else if (!strcmp(argv[1], "write"))
		write_snapshot();
	else if (!strcmp(argv[1], "adjacent-before"))
		adjacent_snapshot(true);
	else if (!strcmp(argv[1], "adjacent-after"))
		adjacent_snapshot(false);
	else if (!strcmp(argv[1], "close"))
		close_state();
	else if (!strcmp(argv[1], "max-transaction"))
		max_transaction();
	else
		reject_case(argv[1]);
	return 0;
}
