/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_ENTRY_H
#define CPU_X86_SMM_INVOCATION_ENTRY_H

#include <cpu/x86/smm_invocation_evidence.h>

#define SMM_INVOCATION_ENTRY_CAUSE_REVISION 2U
#define SMM_INVOCATION_ENTRY_POLICY_REVISION 2U
#define SMM_INVOCATION_ENTRY_MAX_POLLS 10000000U

struct smm_invocation_entry_policy {
	uint32_t revision;
	uint32_t size;
	uint32_t max_polls;
	uint32_t reserved;
};

/*
 * A platform entry adapter produces this immutable fact only after proving a
 * shared, still asserted private APMC cause. It is not a command selector.
 */
struct smm_invocation_entry_cause {
	uint32_t revision;
	uint32_t size;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint32_t lifecycle;
	uint8_t command;
	uint8_t recognized;
	uint8_t reserved[2];
};

struct smm_invocation_entry_ticket {
	uint64_t generation;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint32_t cpu;
	uint32_t lifecycle;
	uint32_t max_polls;
	uint8_t command;
	uint8_t reserved[3];
} __aligned(8);

_Static_assert(sizeof(struct smm_invocation_entry_policy) == 16,
	"SMM invocation entry policy ABI changed");
_Static_assert(sizeof(struct smm_invocation_entry_cause) == 32,
	"SMM invocation entry cause ABI changed");
_Static_assert(_Alignof(struct smm_invocation_entry_cause) == 8,
	"SMM invocation entry cause alignment changed");
_Static_assert(offsetof(struct smm_invocation_entry_cause,
	loader_instance_nonce) == 8,
	"SMM invocation entry cause nonce offset changed");
_Static_assert(sizeof(struct smm_invocation_entry_ticket) == 40,
	"SMM invocation entry ticket ABI changed");
_Static_assert(_Alignof(struct smm_invocation_entry_ticket) == 8,
	"SMM invocation entry ticket alignment changed");
_Static_assert(offsetof(struct smm_invocation_entry_ticket, generation) == 0,
	"SMM invocation entry ticket generation offset changed");
_Static_assert(offsetof(struct smm_invocation_entry_ticket, max_polls) == 32,
	"SMM invocation entry ticket poll-bound offset changed");
_Static_assert(offsetof(struct smm_invocation_entry_ticket, command) == 36,
	"SMM invocation entry ticket command offset changed");
_Static_assert(offsetof(struct smm_invocation_entry_ticket, reserved) == 37,
	"SMM invocation entry ticket reserved offset changed");

enum cb_err smm_invocation_entry_arrive(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	struct smm_invocation_loader_instance_nonce expected_loader_instance_nonce,
	uint8_t expected_command,
	uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket);
enum cb_err smm_invocation_entry_depart(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_ticket *ticket);
bool smm_invocation_entry_eos_ready(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_ticket *ticket);

#endif
