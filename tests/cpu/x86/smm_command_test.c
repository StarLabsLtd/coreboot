/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <commonlib/helpers.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_command.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

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
dispatch(u8 command, enum smm_apmc_owner_outcome outcome,
	 struct smm_apmc_descriptor *descriptor)
{
	struct smm_apmc_descriptor selection;
	enum smm_apmc_dispatch_result result;

	if (smm_apmc_command_select(command, &selection) !=
	    SMM_APMC_SELECT_ENABLED) {
		if (descriptor)
			memset(descriptor, 0, sizeof(*descriptor));
		return SMM_APMC_CONSUMED_REJECT;
	}
	if (descriptor)
		*descriptor = selection;
	result = smm_apmc_command_finish(command, &selection, outcome);
	if (result != SMM_APMC_CONSUMED_SUCCESS && descriptor)
		memset(descriptor, 0, sizeof(*descriptor));
	return result;
}

static enum smm_apmc_dispatch_result finish(u8 command,
					    struct smm_apmc_descriptor *selection,
					    enum smm_apmc_owner_outcome outcome)
{
	return smm_apmc_command_finish(command, selection, outcome);
}

static void assert_descriptor_zero(const struct smm_apmc_descriptor *descriptor)
{
	assert(!memcmp(descriptor, &(struct smm_apmc_descriptor) { 0 },
		       sizeof(*descriptor)));
}

static void reserved_namespace_is_unique_and_consumed(void)
{
	bool seen[UINT8_MAX + 1U] = { 0 };

	for (size_t index = 0; index < ARRAY_SIZE(reserved_commands); index++) {
		struct smm_apmc_descriptor selection;
		const u8 command = reserved_commands[index];

		assert(!seen[command]);
		seen[command] = true;
		if (smm_apmc_command_select(command, &selection) ==
		    SMM_APMC_SELECT_ENABLED) {
			assert(selection.command == command);
			assert(finish(command, &selection,
				      SMM_APMC_OWNER_MALFORMED) ==
				SMM_APMC_CONSUMED_REJECT);
			assert_descriptor_zero(&selection);
		} else {
			assert(smm_apmc_command_select(command, &selection) ==
				SMM_APMC_SELECT_CONSUMED_REJECT);
		}
	}
}

static void unknown_only_falls_through(void)
{
	struct smm_apmc_descriptor selection;

	memset(&selection, 0xa5, sizeof(selection));
	assert(smm_apmc_command_select(0x7aU, &selection) ==
		SMM_APMC_SELECT_UNKNOWN);
	assert_descriptor_zero(&selection);
	assert(smm_apmc_command_select(0x7aU, NULL) ==
		SMM_APMC_SELECT_UNKNOWN);
	memset(&selection, 0xa5, sizeof(selection));
	assert(smm_apmc_command_select(APM_CNT_NOOP_SMI, &selection) ==
		SMM_APMC_SELECT_CONSUMED_REJECT);
	assert_descriptor_zero(&selection);
}

static void enabled_owner_semantics(void)
{
	struct smm_apmc_descriptor descriptor = { 0 };
	bool seen[UINT8_MAX + 1U] = { 0 };

	for (size_t index = 0; index < ARRAY_SIZE(enabled_claims); index++) {
		const struct smm_apmc_descriptor *expected = &enabled_claims[index];

		assert(!seen[expected->command]);
		seen[expected->command] = true;
		assert(dispatch(expected->command, SMM_APMC_OWNER_HANDLED,
				&descriptor) ==
			SMM_APMC_CONSUMED_SUCCESS);
		assert(!memcmp(&descriptor, expected, sizeof(descriptor)));
	}

	assert(dispatch(APM_CNT_ACPI_ENABLE, SMM_APMC_OWNER_HANDLED,
			&descriptor) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.enabled);
	assert(descriptor.binding_count == 1U);
	assert(descriptor.role == SMM_APMC_EXCLUSIVE);
	assert(descriptor.observer_count == 0U);
	assert(dispatch(APM_CNT_ACPI_ENABLE, SMM_APMC_OWNER_UNREADY, NULL) ==
		SMM_APMC_CONSUMED_REJECT);
	assert(dispatch(APM_CNT_ACPI_ENABLE, SMM_APMC_OWNER_ERROR, NULL) ==
		SMM_APMC_CONSUMED_REJECT);

#if CONFIG(PAYLOAD_SPI_FLASH_CONSOLE)
	assert(dispatch(SMM_APMC_SPI_CONSOLE, SMM_APMC_OWNER_HANDLED,
			&descriptor) ==
#if CONFIG(BOARD_EMULATION_QEMU_X86_Q35)
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.binding_count == 1U);
#else
		SMM_APMC_CONSUMED_REJECT);
	assert(descriptor.binding_count == 0U);
#endif
	assert(descriptor.owner == SMM_APMC_OWNER_SPI_CONSOLE);
#elif !CONFIG(CAPSULE_BROKER_ENDPOINT_PUBLICATION)
	assert(dispatch(SMM_APMC_SPI_CONSOLE, SMM_APMC_OWNER_HANDLED,
			&descriptor) ==
		SMM_APMC_CONSUMED_REJECT);
	assert(!descriptor.enabled);
	assert(descriptor.role == SMM_APMC_ROLE_NONE);
#endif

#if CONFIG(CAPSULE_BROKER_ENDPOINT_PUBLICATION)
	assert(dispatch(SMM_APMC_CAPSULE_BROKER, SMM_APMC_OWNER_HANDLED,
			&descriptor) ==
		SMM_APMC_CONSUMED_REJECT);
	assert(descriptor.owner == SMM_APMC_OWNER_CAPSULE_BROKER);
	assert(descriptor.binding_count == 0U);
#endif

#if CONFIG(STARLABS_SMM_OPTION_HANDLER) && \
	CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
	assert(dispatch(SMM_APMC_STARLABS_EFI_OPTION, SMM_APMC_OWNER_HANDLED,
			&descriptor) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.owner == SMM_APMC_OWNER_STARLABS_EFI_OPTION);
#endif

#if CONFIG(BOARD_ACER_VN7_572G)
	assert(dispatch(SMM_APMC_ACER_BOARD, SMM_APMC_OWNER_HANDLED,
			&descriptor) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.owner == SMM_APMC_OWNER_ACER_BOARD);
#endif
}

static void selection_is_exact_and_consumed(void)
{
	struct smm_apmc_descriptor selection;
	struct smm_apmc_descriptor snapshot;

	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &selection) ==
		SMM_APMC_SELECT_ENABLED);
	snapshot = selection;
	selection.owner ^= 1U;
	assert(finish(APM_CNT_ACPI_ENABLE, &selection, SMM_APMC_OWNER_HANDLED) ==
		SMM_APMC_CONSUMED_REJECT);
	assert_descriptor_zero(&selection);
	/* A copied selection is correlation data, not replay authority. */
	assert(finish(APM_CNT_ACPI_ENABLE, &snapshot, SMM_APMC_OWNER_HANDLED) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert_descriptor_zero(&snapshot);
	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, &selection) ==
		SMM_APMC_SELECT_ENABLED);
	snapshot = selection;

	selection = snapshot;
	selection.command = APM_CNT_ACPI_DISABLE;
	assert(finish(APM_CNT_ACPI_ENABLE, &selection, SMM_APMC_OWNER_HANDLED) ==
		SMM_APMC_CONSUMED_REJECT);

#define REJECT_MUTATION(statement) do { \
	selection = snapshot; \
	statement; \
	assert(finish(APM_CNT_ACPI_ENABLE, &selection, \
		      SMM_APMC_OWNER_HANDLED) == SMM_APMC_CONSUMED_REJECT); \
} while (0)
	REJECT_MUTATION(selection.role = SMM_APMC_ROLE_NONE);
	REJECT_MUTATION(selection.binding_count = 2U);
	REJECT_MUTATION(selection.observer_count = 1U);
	REJECT_MUTATION(selection.reserved = false);
	REJECT_MUTATION(selection.enabled = false);
#undef REJECT_MUTATION

	selection = snapshot;
	assert(finish(APM_CNT_ACPI_DISABLE, &selection, SMM_APMC_OWNER_HANDLED) ==
		SMM_APMC_CONSUMED_REJECT);
	selection = snapshot;
	assert(finish(APM_CNT_ACPI_ENABLE, &selection,
		      (enum smm_apmc_owner_outcome)UINT8_MAX) ==
		SMM_APMC_CONSUMED_REJECT);
	assert_descriptor_zero(&selection);
	assert(finish(APM_CNT_ACPI_ENABLE, NULL, SMM_APMC_OWNER_HANDLED) ==
		SMM_APMC_CONSUMED_REJECT);
	assert(smm_apmc_command_select(APM_CNT_ACPI_ENABLE, NULL) ==
		SMM_APMC_SELECT_CONSUMED_REJECT);
	assert(smm_apmc_command_select(APM_CNT_FINALIZE, NULL) ==
		SMM_APMC_SELECT_CONSUMED_REJECT);
}

int main(void)
{
	reserved_namespace_is_unique_and_consumed();
	unknown_only_falls_through();
	enabled_owner_semantics();
	selection_is_exact_and_consumed();
	return 0;
}
