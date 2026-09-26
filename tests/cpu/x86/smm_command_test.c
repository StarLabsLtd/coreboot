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

static void reserved_namespace_is_unique_and_consumed(void)
{
	bool seen[UINT8_MAX + 1U] = { 0 };

	for (size_t index = 0; index < ARRAY_SIZE(reserved_commands); index++) {
		struct smm_apmc_descriptor descriptor = { 0 };
		const u8 command = reserved_commands[index];

		assert(!seen[command]);
		seen[command] = true;
		assert(smm_apmc_command_classify(command,
			SMM_APMC_OWNER_MALFORMED, &descriptor) ==
			SMM_APMC_CONSUMED_REJECT);
		assert(descriptor.command == command);
		assert(descriptor.reserved);
	}
}

static void unknown_only_falls_through(void)
{
	struct smm_apmc_descriptor descriptor;

	memset(&descriptor, 0xa5, sizeof(descriptor));
	assert(smm_apmc_command_classify(0x7aU, SMM_APMC_OWNER_HANDLED,
		&descriptor) == SMM_APMC_UNKNOWN);
	assert(!memcmp(&descriptor, &(struct smm_apmc_descriptor) { 0 },
		sizeof(descriptor)));
}

static void enabled_owner_semantics(void)
{
	struct smm_apmc_descriptor descriptor = { 0 };
	bool seen[UINT8_MAX + 1U] = { 0 };

	for (size_t index = 0; index < ARRAY_SIZE(enabled_claims); index++) {
		const struct smm_apmc_descriptor *expected = &enabled_claims[index];

		assert(!seen[expected->command]);
		seen[expected->command] = true;
		assert(smm_apmc_command_classify(expected->command,
			SMM_APMC_OWNER_HANDLED, &descriptor) ==
			SMM_APMC_CONSUMED_SUCCESS);
		assert(!memcmp(&descriptor, expected, sizeof(descriptor)));
	}

	assert(smm_apmc_command_classify(APM_CNT_ACPI_ENABLE,
		SMM_APMC_OWNER_HANDLED, &descriptor) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.enabled);
	assert(descriptor.binding_count == 1U);
	assert(descriptor.role == SMM_APMC_EXCLUSIVE);
	assert(descriptor.observer_count == 0U);
	assert(smm_apmc_command_classify(APM_CNT_ACPI_ENABLE,
		SMM_APMC_OWNER_UNREADY, NULL) == SMM_APMC_CONSUMED_REJECT);
	assert(smm_apmc_command_classify(APM_CNT_ACPI_ENABLE,
		SMM_APMC_OWNER_ERROR, NULL) == SMM_APMC_CONSUMED_REJECT);

#if CONFIG(PAYLOAD_SPI_FLASH_CONSOLE)
	assert(smm_apmc_command_classify(SMM_APMC_SPI_CONSOLE,
		SMM_APMC_OWNER_HANDLED, &descriptor) ==
#if CONFIG(BOARD_EMULATION_QEMU_X86_Q35)
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.binding_count == 1U);
#else
		SMM_APMC_CONSUMED_REJECT);
	assert(descriptor.binding_count == 0U);
#endif
	assert(descriptor.owner == SMM_APMC_OWNER_SPI_CONSOLE);
#elif !CONFIG(CAPSULE_BROKER_ENDPOINT_PUBLICATION)
	assert(smm_apmc_command_classify(SMM_APMC_SPI_CONSOLE,
		SMM_APMC_OWNER_HANDLED, &descriptor) ==
		SMM_APMC_CONSUMED_REJECT);
	assert(!descriptor.enabled);
	assert(descriptor.role == SMM_APMC_ROLE_NONE);
#endif

#if CONFIG(CAPSULE_BROKER_ENDPOINT_PUBLICATION)
	assert(smm_apmc_command_classify(SMM_APMC_CAPSULE_BROKER,
		SMM_APMC_OWNER_HANDLED, &descriptor) ==
		SMM_APMC_CONSUMED_REJECT);
	assert(descriptor.owner == SMM_APMC_OWNER_CAPSULE_BROKER);
	assert(descriptor.binding_count == 0U);
#endif

#if CONFIG(STARLABS_SMM_OPTION_HANDLER) && \
	CONFIG(STARLABS_ACPI_EFI_OPTION_SMI)
	assert(smm_apmc_command_classify(SMM_APMC_STARLABS_EFI_OPTION,
		SMM_APMC_OWNER_HANDLED, &descriptor) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.owner == SMM_APMC_OWNER_STARLABS_EFI_OPTION);
#endif

#if CONFIG(BOARD_ACER_VN7_572G)
	assert(smm_apmc_command_classify(SMM_APMC_ACER_BOARD,
		SMM_APMC_OWNER_HANDLED, &descriptor) ==
		SMM_APMC_CONSUMED_SUCCESS);
	assert(descriptor.owner == SMM_APMC_OWNER_ACER_BOARD);
#endif
}

int main(void)
{
	reserved_namespace_is_unique_and_consumed();
	unknown_only_falls_through();
	enabled_owner_semantics();
	return 0;
}
