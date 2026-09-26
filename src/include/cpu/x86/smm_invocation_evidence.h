/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_EVIDENCE_H
#define CPU_X86_SMM_INVOCATION_EVIDENCE_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define SMM_INVOCATION_EVIDENCE_REVISION 1U
#define SMM_INVOCATION_EVIDENCE_MAX_CPUS 64U
#define SMM_INVOCATION_EVIDENCE_CONTEXT_MAX 64U
#define SMM_INVOCATION_TOKEN_REVISION 1U

enum smm_invocation_loader_lifecycle {
	SMM_INVOCATION_LOADER_COLD = 1,
	SMM_INVOCATION_LOADER_RESUME_FRESH,
};

enum smm_invocation_match {
	SMM_INVOCATION_MATCH_ERROR = -1,
	SMM_INVOCATION_NOT_MATCHED,
	SMM_INVOCATION_MATCHED,
};

struct smm_invocation_loader_seed {
	uint32_t revision;
	uint32_t size;
	uint32_t active_cpus;
	uint32_t bsp_cpu;
	uint64_t boot_generation;
	uint32_t participant_apic_ids[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
	uint32_t lifecycle;
	uint32_t reserved;
};

struct smm_invocation_token {
	uint32_t revision;
	uint32_t size;
	uint32_t initiator_cpu;
	uint32_t active_cpus;
	uint64_t smi_generation;
	uint64_t rendezvous_generation;
	uint64_t rendezvous_digest[3];
	uint32_t bsp;
	uint32_t reserved;
} __aligned(8);

/*
 * These callbacks are one platform-trusted adapter. match_apmc_write() must
 * revision-specifically snapshot and recheck the complete synchronous-I/O
 * tuple for this node. read_rax() and write_rax() must address the exact same
 * sealed save-state node; a fresh lookup or first-match helper is invalid.
 */
typedef enum smm_invocation_match (*smm_invocation_match_fn)(void *context,
	uint32_t cpu, uint8_t command);
typedef enum cb_err (*smm_invocation_read_rax_fn)(void *context,
	uint32_t cpu, uint64_t *value);
typedef enum cb_err (*smm_invocation_write_rax_fn)(void *context,
	uint32_t cpu, uint64_t value);
typedef void (*smm_invocation_fail_stop_fn)(void *context) __noreturn;

struct smm_invocation_save_state_ops {
	smm_invocation_match_fn match_apmc_write;
	smm_invocation_read_rax_fn read_rax;
	smm_invocation_write_rax_fn write_rax;
	void *context;
	size_t context_size;
};

enum smm_invocation_evidence_phase {
	SMM_INVOCATION_EMPTY,
	SMM_INVOCATION_PROVISIONING,
	SMM_INVOCATION_READY,
	SMM_INVOCATION_OPENING,
	SMM_INVOCATION_COLLECTING,
	SMM_INVOCATION_ARRIVAL_ADMITTING,
	SMM_INVOCATION_CLAIMING,
	SMM_INVOCATION_CLAIMED,
	SMM_INVOCATION_ABORTING,
	SMM_INVOCATION_PUBLISHING,
	SMM_INVOCATION_PUBLISHED,
	SMM_INVOCATION_COMPLETING,
	SMM_INVOCATION_CLOSING,
	SMM_INVOCATION_DEPARTURE_ADMITTING,
	SMM_INVOCATION_CLOSE_CLEANING,
	SMM_INVOCATION_CLOSE_SCRUBBING,
	SMM_INVOCATION_POISONING,
	SMM_INVOCATION_POISON_CLEANING,
	SMM_INVOCATION_CLOSED,
	SMM_INVOCATION_POISONED,
};

enum smm_invocation_participant_phase {
	SMM_INVOCATION_PARTICIPANT_EMPTY,
	SMM_INVOCATION_PARTICIPANT_WRITING,
	SMM_INVOCATION_PARTICIPANT_READY,
};

struct smm_invocation_participant {
	uint32_t phase;
	uint32_t apic_id;
	uint64_t generation;
};

struct smm_invocation_evidence {
	uint32_t phase;
	uint32_t active_cpus;
	uint32_t bsp_cpu;
	uint32_t shutdown_requested;
	uint32_t reentry_detected;
	uint32_t arrival_failed;
	uint32_t arrival_writers;
	uint32_t departure_writers;
	uint32_t writer_reserved;
	uint64_t boot_generation;
	uint64_t generation;
	uint64_t expected_cpus;
	uint64_t arrived_cpus;
	uint64_t departed_cpus;
	uint64_t sentinel;
	uint64_t original_rax;
	uint32_t command;
	uint32_t command_reserved;
	uint32_t participant_apic_ids[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
	struct smm_invocation_participant
		participants[SMM_INVOCATION_EVIDENCE_MAX_CPUS];
	struct smm_invocation_token token;
	uint32_t close_requested;
	uint32_t reserved;
	smm_invocation_fail_stop_fn fail_stop;
	uint8_t fail_context[SMM_INVOCATION_EVIDENCE_CONTEXT_MAX];
	size_t fail_context_size;
} __aligned(8);

enum cb_err smm_invocation_evidence_provision(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_loader_seed *seed,
	smm_invocation_fail_stop_fn fail_stop, const void *fail_context,
	size_t fail_context_size);
enum cb_err smm_invocation_evidence_arrive(
	struct smm_invocation_evidence *evidence, uint32_t cpu, uint32_t apic_id,
	uint64_t *generation);
enum cb_err smm_invocation_evidence_claim(
	struct smm_invocation_evidence *evidence, uint8_t command,
	uint64_t sentinel, const struct smm_invocation_save_state_ops *ops,
	struct smm_invocation_token *token);
enum cb_err smm_invocation_evidence_publish(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_token *token, uint64_t value,
	const struct smm_invocation_save_state_ops *ops);
enum cb_err smm_invocation_evidence_complete(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_token *token);
enum cb_err smm_invocation_evidence_abort(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_token *token,
	const struct smm_invocation_save_state_ops *ops);
enum cb_err smm_invocation_evidence_depart(
	struct smm_invocation_evidence *evidence, uint32_t cpu,
	uint64_t generation);
enum cb_err smm_invocation_evidence_shutdown(
	struct smm_invocation_evidence *evidence);

#endif
