/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <commonlib/helpers.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_command.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

void smm_apmc_command_test_set_generation(u64 generation);

#define RESERVED_VALUE(value) value,
static const u8 reserved_commands[] = {
	SMM_APMC_RESERVED_COMMANDS(RESERVED_VALUE)
};
#undef RESERVED_VALUE

#define ENABLED_VALUE(value, claim_owner, claim_role, bindings) \
	{ value, claim_owner, claim_role, bindings, 0, true, true },
static const struct smm_apmc_descriptor enabled_claims[] = {
	SMM_APMC_ENABLED_CLAIMS(ENABLED_VALUE)
};
#undef ENABLED_VALUE

static enum smm_apmc_dispatch_result
dispatch(u8 command, struct smm_apmc_descriptor *descriptor)
{
	struct smm_apmc_selection_receipt receipt;
	enum smm_apmc_dispatch_result result;

	if (smm_apmc_command_select(command, &receipt) !=
	    SMM_APMC_SELECT_ENABLED) {
		if (descriptor)
			memset(descriptor, 0, sizeof(*descriptor));
		return SMM_APMC_CONSUMED_REJECT;
	}
	if (descriptor)
		*descriptor = receipt.descriptor;
	result = smm_apmc_command_consume(command,
		(enum smm_apmc_owner)receipt.descriptor.owner, &receipt);
	if (result != SMM_APMC_CONSUMED_SUCCESS && descriptor)
		memset(descriptor, 0, sizeof(*descriptor));
	return result;
}

static void assert_receipt_zero(const struct smm_apmc_selection_receipt *receipt)
{
	assert(!memcmp(receipt, &(struct smm_apmc_selection_receipt) { 0 },
		       sizeof(*receipt)));
}

static void reserved_namespace_is_unique_and_consumed(void)
{
	bool seen[UINT8_MAX + 1U] = { 0 };

	for (size_t index = 0; index < ARRAY_SIZE(reserved_commands); index++) {
		struct smm_apmc_selection_receipt receipt;
		const u8 command = reserved_commands[index];

		assert(!seen[command]);
		seen[command] = true;
		if (smm_apmc_command_select(command, &receipt) ==
		    SMM_APMC_SELECT_ENABLED) {
			assert(receipt.descriptor.command == command);
			assert(smm_apmc_command_consume(command,
				(enum smm_apmc_owner)receipt.descriptor.owner,
				&receipt) == SMM_APMC_CONSUMED_SUCCESS);
			assert_receipt_zero(&receipt);
		} else {
			assert(smm_apmc_command_select(command, &receipt) ==
				SMM_APMC_SELECT_CONSUMED_REJECT);
		}
	}
}

static void unknown_only_falls_through(void)
{
	struct smm_apmc_selection_receipt receipt;

	memset(&receipt, 0xa5, sizeof(receipt));
	assert(smm_apmc_command_select(0x7aU, &receipt) ==
		SMM_APMC_SELECT_UNKNOWN);
	assert_receipt_zero(&receipt);
	assert(smm_apmc_command_select(0x7aU, NULL) ==
		SMM_APMC_SELECT_UNKNOWN);
	memset(&receipt, 0xa5, sizeof(receipt));
	assert(smm_apmc_command_select(APM_CNT_NOOP_SMI, &receipt) ==
		SMM_APMC_SELECT_CONSUMED_REJECT);
	assert_receipt_zero(&receipt);
}

static void authvar_presence_is_reserved_without_a_route(void)
{
	struct smm_apmc_selection_receipt receipt;

	memset(&receipt, 0xa5, sizeof(receipt));
	assert(smm_apmc_command_select(SMM_APMC_AUTHVAR_PRESENCE, &receipt) ==
		SMM_APMC_SELECT_CONSUMED_REJECT);
	assert_receipt_zero(&receipt);
	assert(smm_apmc_command_consume(SMM_APMC_AUTHVAR_PRESENCE,
					SMM_APMC_OWNER_AUTHVAR_PRESENCE,
					&receipt) ==
		SMM_APMC_CONSUMED_REJECT);
	assert_receipt_zero(&receipt);
}

static void enabled_owner_semantics(void)
{
	struct smm_apmc_descriptor descriptor = { 0 };
	bool seen[UINT8_MAX + 1U] = { 0 };

	for (size_t index = 0; index < ARRAY_SIZE(enabled_claims); index++) {
		const struct smm_apmc_descriptor *expected = &enabled_claims[index];

		assert(!seen[expected->command]);
		seen[expected->command] = true;
		assert(dispatch(expected->command, &descriptor) ==
			SMM_APMC_CONSUMED_SUCCESS);
		assert(!memcmp(&descriptor, expected, sizeof(descriptor)));
	}

	assert(dispatch(APM_CNT_ACPI_ENABLE, &descriptor) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.enabled);
	assert(descriptor.binding_count == 1U);
	assert(descriptor.role == SMM_APMC_EXCLUSIVE);
	assert(descriptor.observer_count == 0U);
#if CONFIG(PAYLOAD_SPI_FLASH_CONSOLE)
	assert(dispatch(SMM_APMC_SPI_CONSOLE, &descriptor) ==
#if CONFIG(BOARD_EMULATION_QEMU_X86_Q35)
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.binding_count == 1U);
#else
		SMM_APMC_CONSUMED_REJECT);
	assert(descriptor.binding_count == 0U);
#endif
	assert(descriptor.owner == SMM_APMC_OWNER_SPI_CONSOLE);
#elif !CONFIG(CAPSULE_BROKER_ENDPOINT_PUBLICATION)
	assert(dispatch(SMM_APMC_SPI_CONSOLE, &descriptor) ==
		SMM_APMC_CONSUMED_REJECT);
	assert(!descriptor.enabled);
	assert(descriptor.role == SMM_APMC_ROLE_NONE);
#endif

#if CONFIG(CAPSULE_BROKER_ENDPOINT_PUBLICATION)
	assert(dispatch(SMM_APMC_CAPSULE_BROKER, &descriptor) ==
		SMM_APMC_CONSUMED_REJECT);
	assert(descriptor.owner == SMM_APMC_OWNER_CAPSULE_BROKER);
	assert(descriptor.binding_count == 0U);
#endif

#if CONFIG(STARLABS_SMM_OPTION_HANDLER) && \
	CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
	assert(dispatch(SMM_APMC_STARLABS_EFI_OPTION, &descriptor) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.owner == SMM_APMC_OWNER_STARLABS_EFI_OPTION);
#endif

#if CONFIG(BOARD_ACER_VN7_572G)
	assert(dispatch(SMM_APMC_ACER_BOARD, &descriptor) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.owner == SMM_APMC_OWNER_ACER_BOARD);
#endif
}

static void exact_consume_semantics(void)
{
	struct smm_apmc_selection_receipt receipt;
	struct smm_apmc_selection_receipt snapshot;
	struct smm_apmc_selection_receipt copy;

	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &receipt) ==
		SMM_APMC_SELECT_ENABLED);
	snapshot = receipt;
	assert(smm_apmc_command_consume(APM_CNT_ACPI_ENABLE,
					SMM_APMC_OWNER_ACPI_CONTROL,
					&receipt) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert_receipt_zero(&receipt);
	/* Restoring an already consumed receipt at its original address is replay. */
	receipt = snapshot;
	assert(smm_apmc_command_consume(APM_CNT_ACPI_ENABLE,
					SMM_APMC_OWNER_ACPI_CONTROL,
					&receipt) == SMM_APMC_CONSUMED_REJECT);
	assert_receipt_zero(&receipt);

	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &receipt) ==
		SMM_APMC_SELECT_ENABLED);
	copy = receipt;
	assert(smm_apmc_command_consume(APM_CNT_ACPI_ENABLE,
					SMM_APMC_OWNER_ACPI_CONTROL,
					&copy) == SMM_APMC_CONSUMED_REJECT);
	assert_receipt_zero(&copy);
	assert(smm_apmc_command_consume(APM_CNT_ACPI_ENABLE,
					SMM_APMC_OWNER_ACPI_CONTROL,
					&receipt) == SMM_APMC_CONSUMED_SUCCESS);

	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &receipt) ==
		SMM_APMC_SELECT_ENABLED);
	snapshot = receipt;
	receipt.identity++;
	assert(smm_apmc_command_consume(APM_CNT_ACPI_ENABLE,
					SMM_APMC_OWNER_ACPI_CONTROL,
					&receipt) == SMM_APMC_CONSUMED_REJECT);
	receipt = snapshot;
	assert(smm_apmc_command_consume(APM_CNT_ACPI_ENABLE,
					SMM_APMC_OWNER_ACPI_CONTROL,
					&receipt) == SMM_APMC_CONSUMED_SUCCESS);

#define REJECT_CONSUME(statement) do { \
	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &receipt) == \
		SMM_APMC_SELECT_ENABLED); \
	statement; \
	assert(smm_apmc_command_consume(APM_CNT_ACPI_ENABLE, \
		SMM_APMC_OWNER_ACPI_CONTROL, &receipt) == \
		SMM_APMC_CONSUMED_REJECT); \
	assert_receipt_zero(&receipt); \
} while (0)
	REJECT_CONSUME(receipt.revision++);
	REJECT_CONSUME(receipt.size--);
	REJECT_CONSUME(receipt.generation++);
	REJECT_CONSUME(receipt.descriptor.command = APM_CNT_ACPI_DISABLE);
	REJECT_CONSUME(receipt.descriptor.owner = SMM_APMC_OWNER_FINALIZE);
	REJECT_CONSUME(receipt.descriptor.role = SMM_APMC_ROLE_NONE);
	REJECT_CONSUME(receipt.descriptor.binding_count = 2U);
	REJECT_CONSUME(receipt.descriptor.observer_count = 1U);
	REJECT_CONSUME(receipt.descriptor.reserved = false);
	REJECT_CONSUME(receipt.descriptor.enabled = false);
	REJECT_CONSUME(receipt.reserved = 1U);
#undef REJECT_CONSUME

	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &receipt) ==
		SMM_APMC_SELECT_ENABLED);
	assert(smm_apmc_command_consume(APM_CNT_ACPI_DISABLE,
					SMM_APMC_OWNER_ACPI_CONTROL,
					&receipt) ==
		SMM_APMC_CONSUMED_REJECT);
	assert_receipt_zero(&receipt);
	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &receipt) ==
		SMM_APMC_SELECT_ENABLED);
	assert(smm_apmc_command_consume(APM_CNT_ACPI_ENABLE,
					SMM_APMC_OWNER_FINALIZE,
					&receipt) ==
		SMM_APMC_CONSUMED_REJECT);
	assert_receipt_zero(&receipt);
	assert(smm_apmc_command_consume(APM_CNT_ACPI_ENABLE,
					SMM_APMC_OWNER_ACPI_CONTROL,
					NULL) ==
		SMM_APMC_CONSUMED_REJECT);
	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, NULL) ==
		SMM_APMC_SELECT_CONSUMED_REJECT);
	assert(smm_apmc_command_select(APM_CNT_FINALIZE, NULL) ==
		SMM_APMC_SELECT_CONSUMED_REJECT);
}

struct concurrent_consume_call {
	struct smm_apmc_selection_receipt receipt;
	pthread_barrier_t *barrier;
	enum smm_apmc_dispatch_result result;
};

static void *concurrent_consume(void *argument)
{
	struct concurrent_consume_call *call = argument;
	const int status = pthread_barrier_wait(call->barrier);

	assert(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);
	call->result = smm_apmc_command_consume(APM_CNT_ACPI_ENABLE,
		SMM_APMC_OWNER_ACPI_CONTROL, &call->receipt);
	return NULL;
}

static void concurrent_consumers_are_exact(void)
{
	pthread_barrier_t barrier;
	pthread_t threads[2];
	struct concurrent_consume_call calls[2] = {
		{ .barrier = &barrier }, { .barrier = &barrier },
	};

	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE,
		&calls[0].receipt) == SMM_APMC_SELECT_ENABLED);
	calls[1].receipt = calls[0].receipt;
	assert(!pthread_barrier_init(&barrier, NULL, ARRAY_SIZE(threads)));
	for (size_t index = 0; index < ARRAY_SIZE(threads); ++index)
		assert(!pthread_create(&threads[index], NULL, concurrent_consume,
			&calls[index]));
	for (size_t index = 0; index < ARRAY_SIZE(threads); ++index)
		assert(!pthread_join(threads[index], NULL));
	assert(!pthread_barrier_destroy(&barrier));
	assert(calls[0].result == SMM_APMC_CONSUMED_SUCCESS);
	assert(calls[1].result == SMM_APMC_CONSUMED_REJECT);
}

static void exhausted_generation_fails_closed(void)
{
	struct smm_apmc_selection_receipt receipt;

	smm_apmc_command_test_set_generation(UINT64_MAX);
	memset(&receipt, 0xa5, sizeof(receipt));
	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &receipt) ==
		SMM_APMC_SELECT_CONSUMED_REJECT);
	assert_receipt_zero(&receipt);
	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &receipt) ==
		SMM_APMC_SELECT_CONSUMED_REJECT);
}

int main(void)
{
	reserved_namespace_is_unique_and_consumed();
	unknown_only_falls_through();
	authvar_presence_is_reserved_without_a_route();
	enabled_owner_semantics();
	exact_consume_semantics();
	concurrent_consumers_are_exact();
	exhausted_generation_fails_closed();
	return 0;
}
