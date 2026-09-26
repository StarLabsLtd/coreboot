/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <cpu/x86/smm_command.h>
#include <string.h>

_Static_assert(!CONFIG(SMM_APMC_COMMAND_REGISTRY) ||
	       CONFIG(SMM_APMC_COMPOSITION_ATTESTED),
	"APMC registry requires an attested dispatcher composition");

#define ASSERT_ONE_BINDING(value, owner, role, bindings) \
	_Static_assert((bindings) == 1, "enabled APMC owner needs one dispatcher");
SMM_APMC_ENABLED_CLAIMS(ASSERT_ONE_BINDING)
#undef ASSERT_ONE_BINDING

static bool command_reserved(u8 command)
{
	switch (command) {
#define RESERVED_CASE(value) case value: return true;
	SMM_APMC_RESERVED_COMMANDS(RESERVED_CASE)
#undef RESERVED_CASE
	default:
		return false;
	}
}

static bool command_enabled(u8 command,
			    struct smm_apmc_descriptor *descriptor)
{
	switch (command) {
#define ENABLED_CASE(value, command_owner, command_role, bindings) \
	case value: \
		*descriptor = (struct smm_apmc_descriptor) { \
			.command = value, \
			.owner = command_owner, \
			.role = command_role, \
			.binding_count = bindings, \
			.reserved = true, \
			.enabled = true, \
		}; \
		return true;
	SMM_APMC_ENABLED_CLAIMS(ENABLED_CASE)
#undef ENABLED_CASE
	default:
		return false;
	}
}

enum smm_apmc_dispatch_result smm_apmc_command_classify(u8 command,
	enum smm_apmc_owner_outcome outcome,
	struct smm_apmc_descriptor *descriptor)
{
	struct smm_apmc_descriptor claim = { 0 };
	const bool enabled = command_enabled(command, &claim);

	if (!enabled && !command_reserved(command)) {
		if (descriptor)
			memset(descriptor, 0, sizeof(*descriptor));
		return SMM_APMC_UNKNOWN;
	}
	if (!enabled) {
		claim.command = command;
		claim.reserved = true;
	}
	if (descriptor)
		*descriptor = claim;
	if (!enabled || claim.binding_count != 1U ||
	    outcome != SMM_APMC_OWNER_HANDLED)
		return SMM_APMC_CONSUMED_REJECT;
	return SMM_APMC_CONSUMED_SUCCESS;
}
