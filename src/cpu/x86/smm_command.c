/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <cpu/x86/smm_command.h>

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

static bool selection_matches(const struct smm_apmc_descriptor *selection,
			      u8 expected_command,
			      const struct smm_apmc_descriptor *claim)
{
	return selection->command == expected_command &&
		selection->owner == claim->owner && selection->role == claim->role &&
		selection->binding_count == claim->binding_count &&
		selection->observer_count == claim->observer_count &&
		selection->reserved == claim->reserved &&
		selection->enabled == claim->enabled;
}

enum smm_apmc_select_result
smm_apmc_command_select(u8 command, struct smm_apmc_descriptor *selection)
{
	struct smm_apmc_descriptor claim = { 0 };
	const bool enabled = command_enabled(command, &claim);

	if (selection)
		*selection = (struct smm_apmc_descriptor) { 0 };
	if (!enabled)
		return command_reserved(command) ?
			SMM_APMC_SELECT_CONSUMED_REJECT : SMM_APMC_SELECT_UNKNOWN;
	if (!selection)
		return SMM_APMC_SELECT_CONSUMED_REJECT;
	*selection = claim;
	return SMM_APMC_SELECT_ENABLED;
}

enum smm_apmc_dispatch_result
smm_apmc_command_finish(u8 expected_command,
			struct smm_apmc_descriptor *selection,
			enum smm_apmc_owner_outcome outcome)
{
	struct smm_apmc_descriptor snapshot;
	struct smm_apmc_descriptor claim = { 0 };
	bool valid;

	if (!selection)
		return SMM_APMC_CONSUMED_REJECT;
	snapshot = *selection;
	*selection = (struct smm_apmc_descriptor) { 0 };
	valid = command_enabled(expected_command, &claim) &&
		selection_matches(&snapshot, expected_command, &claim);
	if (!valid || outcome != SMM_APMC_OWNER_HANDLED)
		return SMM_APMC_CONSUMED_REJECT;
	return SMM_APMC_CONSUMED_SUCCESS;
}
