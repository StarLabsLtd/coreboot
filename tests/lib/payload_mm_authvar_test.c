/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar.h>
#include <string.h>

#define COMM_BYTES 4096U

static uint8_t communication[COMM_BYTES] __aligned(4096);
static struct {
	struct payload_mm_authvar_request request;
	size_t message_size;
	uint8_t message[COMM_BYTES];
	struct payload_mm_fmp_state_command command;
	uint8_t current_state[PAYLOAD_MM_FMP_STATE_WIRE_SIZE + 8] __aligned(8);
} trusted __aligned(4096);

static const guid_t state_guid = GUID_INIT(0x975cd0e6, 0xc540, 0x4e2b,
	0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d);
static const uint8_t state_guid_bytes[16] = {
	0xe6, 0xd0, 0x5c, 0x97, 0x40, 0xc5, 0x2b, 0x4e,
	0x90, 0x6c, 0x72, 0xc0, 0xd0, 0xd1, 0xe4, 0x0d,
};

static bool owns_store;
static bool owns_smm;
static bool restricts_spi;
static bool omits_raw_flash;
static bool reserves_communication;
static unsigned int ownership_calls;
static bool protects_authority = true;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static bool platform_fact(void *context)
{
	assert(context == &ownership_calls);
	return true;
}

static bool smm_owned(void *context)
{
	return platform_fact(context) && owns_smm;
}

static bool spi_restricted(void *context)
{
	return platform_fact(context) && restricts_spi;
}

static bool no_raw_flash(void *context)
{
	return platform_fact(context) && omits_raw_flash;
}

static bool communication_reserved(void *context, uint64_t base, uint64_t size)
{
	assert(context == &ownership_calls);
	assert(base == (uintptr_t)communication);
	assert(size == sizeof(communication));
	return reserves_communication;
}

static bool store_owned(void *context, uint64_t offset, uint64_t size)
{
	assert(context == &ownership_calls);
	assert(offset == 0x600000U);
	assert(size == 0x60000U);
	ownership_calls++;
	return owns_store;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	assert(context == &ownership_calls);
	assert(storage != NULL);
	assert(size >= sizeof(struct payload_mm_authvar_contract) + 2);
	return protects_authority;
}

static bool protected_state_storage(void *context, const void *storage,
	size_t size)
{
	assert(context == &ownership_calls);
	assert(storage != NULL);
	assert(size >= sizeof(struct payload_mm_fmp_state_policy));
	return protects_authority;
}

static struct payload_mm_authvar_platform valid_platform(void)
{
	return (struct payload_mm_authvar_platform) {
		.smm_address_bits = 64,
		.generation = 7,
		.smram = { (uintptr_t)&trusted, sizeof(trusted) },
		.communication = {
			(uintptr_t)communication, sizeof(communication)
		},
		.boot_media_size = 0x1000000,
		.store_offset = 0x600000,
		.store_size = 0x60000,
		.block_size = 0x1000,
		.erase_size = 0x10000,
		.smm_entry_owned = smm_owned,
		.spi_writes_restricted_to_smm = spi_restricted,
		.raw_flash_transport_absent = no_raw_flash,
		.communication_region_reserved = communication_reserved,
		.store_region_owned_by_smm = store_owned,
		.context = &ownership_calls,
	};
}

static void enable_platform_facts(void)
{
	owns_store = true;
	owns_smm = true;
	restricts_spi = true;
	omits_raw_flash = true;
	reserves_communication = true;
}

static void contract_mutations(void)
{
	struct payload_mm_authvar_contract output;
	struct payload_mm_authvar_platform platform = valid_platform();
	struct payload_mm_authvar_contract zero = {0};

#define REJECT(member, value) do { \
	platform = valid_platform(); \
	platform.member = (value); \
	memset(&output, 0xa5, sizeof(output)); \
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR); \
	assert(!memcmp(&output, &zero, sizeof(output))); \
} while (0)
	REJECT(generation, 0);
	REJECT(smm_address_bits, 31);
	REJECT(smm_entry_owned, NULL);
	REJECT(spi_writes_restricted_to_smm, NULL);
	REJECT(raw_flash_transport_absent, NULL);
	REJECT(communication_region_reserved, NULL);
	REJECT(store_region_owned_by_smm, NULL);
	REJECT(communication.base, platform.smram.base);
	REJECT(communication.base, UINT64_MAX - 7U);
	REJECT(communication.size, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES - 1U);
	REJECT(communication.size, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES + 1U);
	REJECT(boot_media_size, platform.store_size);
	REJECT(store_offset, platform.boot_media_size - platform.store_size + 1U);
	REJECT(store_offset, platform.store_offset + 1U);
	REJECT(store_size, platform.erase_size * 2U);
	REJECT(block_size, 0x1800);
	REJECT(erase_size, 0x18000);
#undef REJECT

	platform = valid_platform();
	owns_smm = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	owns_smm = true;
	platform = valid_platform();
	restricts_spi = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	restricts_spi = true;
	platform = valid_platform();
	omits_raw_flash = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	omits_raw_flash = true;
	platform = valid_platform();
	reserves_communication = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	reserves_communication = true;
	platform = valid_platform();
	owns_store = false;
	assert(payload_mm_authvar_contract_build(&output, &platform) == CB_ERR);
	assert(!memcmp(&output, &zero, sizeof(output)));
	owns_store = true;
}

static uint64_t make_request(size_t request_offset, size_t message_offset)
{
	struct payload_mm_authvar_request request = {
		.revision = PAYLOAD_MM_AUTHVAR_REQUEST_REVISION,
		.size = sizeof(request),
		.operation = PAYLOAD_MM_AUTHVAR_COMMUNICATE,
		.generation = 7,
		.message_address = (uintptr_t)&communication[message_offset],
		.message_size = PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES,
	};

	memset(communication, 0, sizeof(communication));
	memset(&communication[message_offset], 0x5a, request.message_size);
	memcpy(&communication[request_offset], &request, sizeof(request));
	return (uintptr_t)&communication[request_offset];
}

static void expect_request(uint64_t address, enum cb_err expected)
{
	struct payload_mm_authvar_request zero = {0};

	memset(&trusted, 0xa5, sizeof(trusted));
	assert(payload_mm_authvar_request_copy(address, &trusted.request,
		trusted.message, sizeof(trusted.message),
		&trusted.message_size) == expected);
	if (expected == CB_SUCCESS) {
		assert(trusted.message_size == PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES);
		for (size_t i = 0; i < trusted.message_size; i++)
			assert(trusted.message[i] == 0x5a);
		((struct payload_mm_authvar_request *)(uintptr_t)address)->generation++;
		communication[512] = 0;
		assert(trusted.request.generation == 7);
		assert(trusted.message[0] == 0x5a);
	} else {
		assert(!trusted.message_size);
		assert(!memcmp(&trusted.request, &zero, sizeof(zero)));
	}
}

static void request_mutations(void)
{
	struct payload_mm_authvar_request *request;
	uint64_t address = make_request(0, 512);

	expect_request(address, CB_SUCCESS);
	expect_request((uintptr_t)communication - 8U, CB_ERR);
	expect_request((uintptr_t)communication + 1U, CB_ERR);
	expect_request(UINT64_MAX - 7U, CB_ERR);
	expect_request((uintptr_t)&communication[COMM_BYTES] -
		sizeof(*request) + 8U, CB_ERR);
	expect_request((uintptr_t)&trusted, CB_ERR);

	address = make_request(COMM_BYTES - sizeof(*request), 512);
	expect_request(address, CB_SUCCESS);

#define REJECT(member, value) do { \
	address = make_request(0, 512); \
	request = (void *)(uintptr_t)address; \
	request->member = (value); \
	expect_request(address, CB_ERR); \
} while (0)
	REJECT(revision, 2);
	REJECT(size, sizeof(*request) - 1U);
	REJECT(operation, 2);
	REJECT(flags, 1);
	REJECT(generation, 8);
	REJECT(message_address, (uintptr_t)communication - 8U);
	REJECT(message_address, (uintptr_t)communication + 1U);
	REJECT(message_address, UINT64_MAX - 7U);
	REJECT(message_address, (uintptr_t)&communication[COMM_BYTES] -
		PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES + 8U);
	REJECT(message_size, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES - 1U);
	REJECT(message_size, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES + 1U);
	REJECT(reserved, 1);
#undef REJECT

	address = make_request(0, 512);
	assert(payload_mm_authvar_request_copy(address, &trusted.request,
		trusted.message, PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES - 1U,
		&trusted.message_size) == CB_ERR);
	assert(payload_mm_authvar_request_copy(address,
		(struct payload_mm_authvar_request *)communication,
		trusted.message, sizeof(trusted.message),
		&trusted.message_size) == CB_ERR);
	assert(payload_mm_authvar_request_copy(address, &trusted.request,
		&trusted.request, sizeof(trusted.request),
		&trusted.message_size) == CB_ERR);
}

static void write32(uint8_t *data, uint32_t value)
{
	data[0] = value;
	data[1] = value >> 8;
	data[2] = value >> 16;
	data[3] = value >> 24;
}

static struct payload_mm_fmp_state_policy state_policy(uint64_t instance)
{
	struct payload_mm_fmp_state_policy policy = {
		.revision = PAYLOAD_MM_FMP_STATE_POLICY_REVISION,
		.size = sizeof(policy),
		.hardware_instance = instance,
		.trusted_lowest_version = 7,
	};

	policy.namespace_guid = state_guid;
	return policy;
}

static struct payload_mm_fmp_state_message state_message(uint32_t operation,
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

static enum cb_err prepare_state(const struct payload_mm_fmp_state_message *message,
	bool current)
{
	memcpy(trusted.message, message, sizeof(*message));
	memset(&trusted.command, 0xa5, sizeof(trusted.command));
	return payload_mm_fmp_state_command_prepare(trusted.message,
		sizeof(*message), current ? trusted.current_state : NULL,
		current ? PAYLOAD_MM_FMP_STATE_WIRE_SIZE : 0, &trusted.command);
}

static void expect_name(const char *expected)
{
	size_t length = strlen(expected);

	assert(trusted.command.variable_name_bytes ==
		(length + 1) * sizeof(uint16_t));
	for (size_t i = 0; i < length; i++)
		assert(trusted.command.variable_name[i] == (uint8_t)expected[i]);
	assert(!trusted.command.variable_name[length]);
}

static void state_message_mutations(uint64_t transaction)
{
	struct payload_mm_fmp_state_message message;
	struct payload_mm_fmp_state_command outside;

#define REJECT(member, value) do { \
	message = state_message(PAYLOAD_MM_FMP_STATE_READ, \
		PAYLOAD_MM_FMP_STATE_KEY_STATE, transaction); \
	message.member = (value); \
	assert(prepare_state(&message, false) == CB_ERR); \
} while (0)
	REJECT(revision, 2);
	REJECT(size, sizeof(message) - 1);
	REJECT(operation, 0);
	REJECT(key, PAYLOAD_MM_FMP_STATE_KEY_NONE);
	REJECT(transaction, 0);
	REJECT(attributes, 1);
	REJECT(data_size, sizeof(uint32_t));
	REJECT(result, 0);
	REJECT(reserved, 1);
#undef REJECT

	message = state_message(PAYLOAD_MM_FMP_STATE_READ,
		PAYLOAD_MM_FMP_STATE_KEY_STATE, transaction);
	message.data[19] = 1;
	assert(prepare_state(&message, false) == CB_ERR);
	memcpy(trusted.message, &message, sizeof(message));
	assert(payload_mm_fmp_state_command_prepare(trusted.message,
		sizeof(message) - 1, NULL, 0, &trusted.command) == CB_ERR);
	assert(payload_mm_fmp_state_command_prepare(trusted.message + 1,
		sizeof(message), NULL, 0, &trusted.command) == CB_ERR);
	assert(payload_mm_fmp_state_command_prepare(&message, sizeof(message),
		NULL, 0, &trusted.command) == CB_ERR);
	assert(payload_mm_fmp_state_command_prepare(trusted.message,
		sizeof(message), NULL, 0, &outside) == CB_ERR);
	assert(payload_mm_fmp_state_command_prepare(trusted.message,
		sizeof(message), NULL, 0,
		(struct payload_mm_fmp_state_command *)trusted.message) == CB_ERR);
	assert(payload_mm_fmp_state_command_prepare(trusted.message,
		sizeof(message), trusted.message,
		PAYLOAD_MM_FMP_STATE_WIRE_SIZE, &trusted.command) == CB_ERR);
	assert(payload_mm_fmp_state_command_prepare(trusted.message,
		sizeof(message), &message, PAYLOAD_MM_FMP_STATE_WIRE_SIZE,
		&trusted.command) == CB_ERR);
	assert(payload_mm_fmp_state_command_prepare(trusted.message,
		sizeof(message), trusted.current_state + 1,
		PAYLOAD_MM_FMP_STATE_WIRE_SIZE, &trusted.command) == CB_ERR);
	assert(payload_mm_fmp_state_command_prepare(trusted.message,
		sizeof(message), (uint8_t *)&trusted.command + 8,
		PAYLOAD_MM_FMP_STATE_WIRE_SIZE, &trusted.command) == CB_ERR);
}

static void state_write_mutations(uint64_t transaction)
{
	struct payload_mm_fmp_state_message message = state_message(
		PAYLOAD_MM_FMP_STATE_WRITE_STATE, PAYLOAD_MM_FMP_STATE_KEY_STATE,
		transaction);

	message.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES;
	message.data[0] = 2;
	assert(prepare_state(&message, false) == CB_ERR);
	message.data[0] = 0;
	message.attributes = 0;
	assert(prepare_state(&message, false) == CB_ERR);
	message.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES;
	message.data_size--;
	assert(prepare_state(&message, false) == CB_ERR);
	message.data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE;
	message.key = PAYLOAD_MM_FMP_STATE_KEY_VERSION;
	assert(prepare_state(&message, false) == CB_ERR);
}

static void state_control_mutations(uint64_t transaction)
{
	struct payload_mm_fmp_state_message message = state_message(
		PAYLOAD_MM_FMP_STATE_REMOVE_LEGACY,
		PAYLOAD_MM_FMP_STATE_KEY_VERSION, transaction);

	message.data_size = 0;
	message.key = PAYLOAD_MM_FMP_STATE_KEY_STATE;
	assert(prepare_state(&message, true) == CB_ERR);
	message.key = PAYLOAD_MM_FMP_STATE_KEY_VERSION;
	message.attributes = 1;
	assert(prepare_state(&message, true) == CB_ERR);
	message.attributes = 0;
	message.data_size = sizeof(uint32_t);
	assert(prepare_state(&message, true) == CB_ERR);
	message.data_size = 0;
	message.data[0] = 1;
	assert(prepare_state(&message, true) == CB_ERR);

	message = state_message(PAYLOAD_MM_FMP_STATE_CLOSE_STATE,
		PAYLOAD_MM_FMP_STATE_KEY_NONE, transaction);
	message.data_size = 0;
	message.key = PAYLOAD_MM_FMP_STATE_KEY_STATE;
	assert(prepare_state(&message, false) == CB_ERR);
	message.key = PAYLOAD_MM_FMP_STATE_KEY_NONE;
	message.attributes = 1;
	assert(prepare_state(&message, false) == CB_ERR);
	message.attributes = 0;
	message.data_size = sizeof(uint32_t);
	assert(prepare_state(&message, false) == CB_ERR);
	message.data_size = 0;
	message.data[19] = 1;
	assert(prepare_state(&message, false) == CB_ERR);
}

static void state_max_transaction_test(void)
{
	struct payload_mm_fmp_state_policy policy = state_policy(0);
	struct payload_mm_fmp_state_message message = state_message(
		PAYLOAD_MM_FMP_STATE_READ, PAYLOAD_MM_FMP_STATE_KEY_STATE,
		UINT64_MAX);

	assert(payload_mm_fmp_state_policy_install(&policy,
		protected_state_storage, &ownership_calls) == CB_SUCCESS);
	assert(prepare_state(&message, false) == CB_SUCCESS);
	assert(prepare_state(&message, false) == CB_ERR);
	message.transaction = UINT64_MAX - 1;
	assert(prepare_state(&message, false) == CB_ERR);
	message.transaction = 1;
	assert(prepare_state(&message, false) == CB_ERR);
}

static void state_contract_tests(bool nonzero_instance)
{
	static const char *const base_names[] = {
		"FmpState", "FmpVersion", "FmpLsv", "LastAttemptStatus",
		"LastAttemptVersion",
	};
	struct payload_mm_fmp_state_policy policy = state_policy(nonzero_instance ?
		0x1234567812345678ULL : 0);
	struct payload_mm_fmp_state_message message;
	uint64_t transaction = 1;

	assert(!memcmp(state_guid.b, state_guid_bytes, sizeof(state_guid_bytes)));
	assert(payload_mm_fmp_state_policy_install(&policy,
		protected_state_storage, &ownership_calls) == CB_SUCCESS);
	policy.hardware_instance++;
	assert(payload_mm_fmp_state_policy_install(&policy,
		protected_state_storage, &ownership_calls) == CB_ERR);
	state_message_mutations(transaction);
	for (uint32_t key = PAYLOAD_MM_FMP_STATE_KEY_STATE;
	     key <= PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION; key++) {
		char expected[40];

		message = state_message(PAYLOAD_MM_FMP_STATE_READ, key, transaction++);
		assert(prepare_state(&message, false) == CB_SUCCESS);
		if (nonzero_instance) {
			size_t length = strlen(base_names[key]);

			assert(length + 17 <= sizeof(expected));
			memcpy(expected, base_names[key], length);
			memcpy(expected + length, "1234567812345678", 17);
			expect_name(expected);
		} else {
			expect_name(base_names[key]);
		}
		assert(!guidcmp(&trusted.command.namespace_guid, &state_guid));
		assert(trusted.command.hardware_instance ==
			(nonzero_instance ? 0x1234567812345678ULL : 0));
		assert(trusted.command.trusted_lowest_version == 7);
		if (key == PAYLOAD_MM_FMP_STATE_KEY_STATE) {
			trusted.message[0] = 0;
			assert(trusted.command.message.revision ==
				PAYLOAD_MM_FMP_STATE_MESSAGE_REVISION);
		}
	}
	message = state_message(PAYLOAD_MM_FMP_STATE_READ,
		PAYLOAD_MM_FMP_STATE_KEY_STATE, transaction - 1);
	assert(prepare_state(&message, false) == CB_ERR);

	state_write_mutations(transaction);
	message = state_message(PAYLOAD_MM_FMP_STATE_WRITE_STATE,
		PAYLOAD_MM_FMP_STATE_KEY_STATE, transaction++);
	message.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES;
	message.data[0] = 1;
	message.data[1] = 1;
	write32(message.data + 4, 7);
	write32(message.data + 8, 7);
	assert(prepare_state(&message, false) == CB_SUCCESS);
	memcpy(trusted.current_state, message.data,
		PAYLOAD_MM_FMP_STATE_WIRE_SIZE);

	message.transaction = transaction;
	memset(trusted.current_state, 0, PAYLOAD_MM_FMP_STATE_WIRE_SIZE);
	memset(message.data, 0, sizeof(message.data));
	for (size_t i = 0; i < 4; i++) {
		trusted.current_state[i] = 1;
		message.data[i] = 1;
	}
	write32(trusted.current_state + 4, 7);
	write32(trusted.current_state + 8, 7);
	write32(message.data + 4, 7);
	write32(message.data + 8, 7);
	for (size_t i = 0; i < 4; i++) {
		message.data[i] = 0;
		assert(prepare_state(&message, true) == CB_ERR);
		message.data[i] = 1;
	}
	trusted.current_state[2] = 2;
	assert(prepare_state(&message, true) == CB_ERR);
	trusted.current_state[2] = 1;
	write32(message.data + 8, 6);
	assert(prepare_state(&message, true) == CB_ERR);
	write32(message.data + 8, 8);
	write32(message.data + 4, 8);
	assert(prepare_state(&message, true) == CB_SUCCESS);
	transaction++;
	memcpy(trusted.current_state, message.data,
		PAYLOAD_MM_FMP_STATE_WIRE_SIZE);
	state_control_mutations(transaction);

	message = state_message(PAYLOAD_MM_FMP_STATE_REMOVE_LEGACY,
		PAYLOAD_MM_FMP_STATE_KEY_VERSION, transaction);
	message.data_size = 0;
	assert(prepare_state(&message, false) == CB_ERR);
	assert(prepare_state(&message, true) == CB_SUCCESS);
	transaction++;
	message = state_message(PAYLOAD_MM_FMP_STATE_REMOVE_LEGACY,
		PAYLOAD_MM_FMP_STATE_KEY_STATE, transaction);
	message.data_size = 0;
	assert(prepare_state(&message, true) == CB_ERR);

	message = state_message(PAYLOAD_MM_FMP_STATE_CLOSE_STATE,
		PAYLOAD_MM_FMP_STATE_KEY_NONE, transaction++);
	message.data_size = 0;
	assert(prepare_state(&message, false) == CB_SUCCESS);
	assert(!trusted.command.variable_name_bytes);
	assert(prepare_state(&message, false) == CB_ERR);
	message = state_message(PAYLOAD_MM_FMP_STATE_READ,
		PAYLOAD_MM_FMP_STATE_KEY_STATE, transaction);
	assert(prepare_state(&message, false) == CB_ERR);
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_contract contract;
	struct payload_mm_authvar_platform platform = valid_platform();
	struct payload_mm_fmp_state_policy policy = state_policy(0);

	enable_platform_facts();
	contract_mutations();
	ownership_calls = 0;
	assert(payload_mm_authvar_contract_build(&contract, &platform) == CB_SUCCESS);
	assert(contract.flags == PAYLOAD_MM_AUTHVAR_REQUIRED_FLAGS);
	assert(ownership_calls == 1);
	if (argc > 1 && !strcmp(argv[1], "state-before-authority")) {
		assert(payload_mm_fmp_state_policy_install(&policy,
			protected_state_storage, &ownership_calls) == CB_ERR);
		assert(payload_mm_authvar_authority_install(&contract,
			protected_storage, &ownership_calls) == CB_SUCCESS);
		assert(payload_mm_fmp_state_policy_install(&policy,
			protected_state_storage, &ownership_calls) == CB_ERR);
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "reject-authority")) {
		protects_authority = false;
		assert(payload_mm_authvar_authority_install(&contract,
			protected_storage, &ownership_calls) == CB_ERR);
		protects_authority = true;
		assert(payload_mm_authvar_authority_install(&contract,
			protected_storage, &ownership_calls) == CB_ERR);
		return 0;
	}
	assert(payload_mm_authvar_authority_install(&contract, protected_storage,
		&ownership_calls) == CB_SUCCESS);
	contract.generation++;
	assert(payload_mm_authvar_authority_install(&contract, protected_storage,
		&ownership_calls) == CB_ERR);
	if (argc > 1) {
		if (!strcmp(argv[1], "state-bad-revision"))
			policy.revision++;
		else if (!strcmp(argv[1], "state-bad-size"))
			policy.size--;
		else if (!strcmp(argv[1], "state-zero-guid"))
			memset(&policy.namespace_guid, 0, sizeof(policy.namespace_guid));
		else if (!strcmp(argv[1], "state-reserved"))
			policy.reserved = 1;
		else if (!strcmp(argv[1], "state-unprotected"))
			protects_authority = false;
		else if (strcmp(argv[1], "instance") &&
			 strcmp(argv[1], "max-transaction"))
			assert(false);
		if (strcmp(argv[1], "instance") &&
		    strcmp(argv[1], "max-transaction")) {
			assert(payload_mm_fmp_state_policy_install(&policy,
				protected_state_storage, &ownership_calls) == CB_ERR);
			protects_authority = true;
			policy = state_policy(0);
			assert(payload_mm_fmp_state_policy_install(&policy,
				protected_state_storage, &ownership_calls) == CB_ERR);
			return 0;
		}
	}
	request_mutations();
	if (argc > 1 && !strcmp(argv[1], "max-transaction")) {
		state_max_transaction_test();
		return 0;
	}
	state_contract_tests(argc > 1);
	return 0;
}
