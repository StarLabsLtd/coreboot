/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <cpu/x86/smm_command.h>
#include <string.h>

_Static_assert(!CONFIG(SMM_APMC_COMMAND_REGISTRY) ||
	       CONFIG(SMM_APMC_COMPOSITION_ATTESTED),
	"APMC registry requires an attested dispatcher composition");

_Static_assert(!CONFIG(SMM_APMC_ROUTE_AUTHVAR_SERVICE) ||
	CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED),
	"authenticated-variable service requires its fixed dispatcher");

#define ASSERT_ONE_BINDING(value, owner, role, bindings) \
	_Static_assert((bindings) == 1, "enabled APMC owner needs one dispatcher");
SMM_APMC_ENABLED_CLAIMS(ASSERT_ONE_BINDING)
#undef ASSERT_ONE_BINDING

enum selection_state {
	SELECTION_IDLE,
	SELECTION_WRITING,
	SELECTION_ACTIVE,
	SELECTION_CONSUMING,
	SELECTION_POISONED,
};

static struct {
	u32 state;
	u32 reserved;
	u64 generation;
	u64 receipt_identity;
	struct smm_apmc_descriptor descriptor;
} selection_owner;

#if defined(__TEST__)
void smm_apmc_command_test_set_generation(u64 generation)
{
	if (__atomic_load_n(&selection_owner.state, __ATOMIC_ACQUIRE) !=
	    SELECTION_IDLE)
		__builtin_trap();
	selection_owner.generation = generation;
}
#endif

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

static bool receipt_matches(const struct smm_apmc_selection_receipt *receipt,
			    const typeof(selection_owner) *owner,
			    u8 expected_command)
{
	return receipt->revision == SMM_APMC_SELECTION_RECEIPT_REVISION &&
		receipt->size == sizeof(*receipt) &&
		receipt->identity == owner->receipt_identity &&
		receipt->generation == owner->generation && receipt->generation &&
		receipt->descriptor.command == expected_command &&
		!memcmp(&receipt->descriptor, &owner->descriptor,
			sizeof(receipt->descriptor)) && !receipt->reserved &&
		!owner->reserved;
}

enum smm_apmc_select_result
smm_apmc_command_select(u8 command,
			struct smm_apmc_selection_receipt *receipt)
{
	struct smm_apmc_descriptor claim = { 0 };
	const bool enabled = command_enabled(command, &claim);
	u32 expected = SELECTION_IDLE;
	u64 generation;

	if (receipt)
		*receipt = (struct smm_apmc_selection_receipt) { 0 };
	if (!enabled)
		return command_reserved(command) ?
			SMM_APMC_SELECT_CONSUMED_REJECT : SMM_APMC_SELECT_UNKNOWN;
	if (!receipt)
		return SMM_APMC_SELECT_CONSUMED_REJECT;
	if (!__atomic_compare_exchange_n(&selection_owner.state, &expected,
		SELECTION_WRITING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return SMM_APMC_SELECT_CONSUMED_REJECT;
	generation = selection_owner.generation;
	if (generation == UINT64_MAX) {
		__atomic_store_n(&selection_owner.state, SELECTION_POISONED,
			__ATOMIC_RELEASE);
		return SMM_APMC_SELECT_CONSUMED_REJECT;
	}
	generation++;
	selection_owner.generation = generation;
	selection_owner.receipt_identity = (u64)(uintptr_t)receipt;
	selection_owner.descriptor = claim;
	*receipt = (struct smm_apmc_selection_receipt) {
		.revision = SMM_APMC_SELECTION_RECEIPT_REVISION,
		.size = sizeof(*receipt),
		.identity = (u64)(uintptr_t)receipt,
		.generation = generation,
		.descriptor = claim,
	};
	__atomic_store_n(&selection_owner.state, SELECTION_ACTIVE,
		__ATOMIC_RELEASE);
	return SMM_APMC_SELECT_ENABLED;
}

enum smm_apmc_dispatch_result
smm_apmc_command_consume(u8 expected_command,
			 enum smm_apmc_owner expected_owner,
			 struct smm_apmc_selection_receipt *receipt)
{
	struct smm_apmc_selection_receipt snapshot;
	struct smm_apmc_descriptor claim = { 0 };
	typeof(selection_owner) owner;
	u32 expected = SELECTION_ACTIVE;
	bool valid;

	if (!receipt)
		return SMM_APMC_CONSUMED_REJECT;
	snapshot = *receipt;
	*receipt = (struct smm_apmc_selection_receipt) { 0 };
	if (snapshot.identity != (u64)(uintptr_t)receipt)
		return SMM_APMC_CONSUMED_REJECT;
	if (!__atomic_compare_exchange_n(&selection_owner.state, &expected,
		SELECTION_CONSUMING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return SMM_APMC_CONSUMED_REJECT;
	memcpy(&owner, &selection_owner, sizeof(owner));
	if (owner.state != SELECTION_CONSUMING || owner.reserved ||
	    !owner.generation ||
	    owner.receipt_identity != (u64)(uintptr_t)receipt ||
	    !command_enabled(owner.descriptor.command, &claim) ||
	    memcmp(&owner.descriptor, &claim, sizeof(claim))) {
		__atomic_store_n(&selection_owner.state, SELECTION_POISONED,
			__ATOMIC_RELEASE);
		return SMM_APMC_CONSUMED_REJECT;
	}
	claim = (struct smm_apmc_descriptor) { 0 };
	valid = command_enabled(expected_command, &claim) &&
		claim.owner == expected_owner &&
		receipt_matches(&snapshot, &owner, expected_command) &&
		owner.state == SELECTION_CONSUMING &&
		selection_owner.generation == owner.generation &&
		selection_owner.receipt_identity == owner.receipt_identity &&
		!memcmp(&selection_owner.descriptor, &owner.descriptor,
			sizeof(owner.descriptor));
	selection_owner.receipt_identity = 0;
	selection_owner.descriptor = (struct smm_apmc_descriptor) { 0 };
	__atomic_store_n(&selection_owner.state, SELECTION_IDLE, __ATOMIC_RELEASE);
	return valid ? SMM_APMC_CONSUMED_SUCCESS : SMM_APMC_CONSUMED_REJECT;
}
