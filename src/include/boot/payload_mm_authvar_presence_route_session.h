/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION_H

#include <boot/payload_mm_authvar_presence_arm.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_entry.h>
#include <cpu/x86/smm_invocation_topology.h>

enum payload_mm_authvar_presence_route_state {
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_EMPTY,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PROVISIONING,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PREPARE_IDLE,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_OWNING,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DISPATCHING,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_INVOCATION_CLAIMING,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_INVOCATION_CLAIMED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_COMPLETING,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_EVIDENCE_CLOSING,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_ACKED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DEPARTING,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_DECISION_IDLE,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_CLOSED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POISONED,
};

struct payload_mm_authvar_presence_route_session;

struct payload_mm_authvar_presence_route_context {
	struct payload_mm_authvar_presence_route_session *session;
	uintptr_t identity;
};

#define PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_POLICY_REVISION 1U
struct payload_mm_authvar_presence_route_authority_policy {
	uint32_t revision;
	uint32_t size;
	payload_mm_authvar_presence_transaction_smm_prepare_fn prepare;
	payload_mm_authvar_presence_transaction_smm_decide_fn commit;
	payload_mm_authvar_presence_transaction_smm_decide_fn abort;
	payload_mm_authvar_presence_transaction_range_fn dma_protected;
	payload_mm_authvar_presence_transaction_fail_stop_fn fail_stop;
	void *context;
	size_t context_size;
};

enum payload_mm_authvar_presence_route_departure {
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_PARTICIPANT_DEPARTED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_BSP_EOS_CONSUMED,
};

struct payload_mm_authvar_presence_route_session {
	uint32_t state;
	uint32_t owner_attempt;
	uint32_t round;
	uint32_t decision;
	uint64_t prepare_invocation_generation;
	uint64_t active_invocation_generation;
	uint64_t completion_rax;
	uintptr_t identity;
	struct payload_mm_authvar_presence_arm *arm;
	struct payload_mm_authvar_presence_transaction_slot *slot;
	struct payload_mm_authvar_presence_transaction_page *page;
	struct smm_invocation_evidence *evidence;
	const struct smm_invocation_loader_composition *composition;
	const struct smm_invocation_loader_instance *instance;
	const struct smm_invocation_topology *topology;
	struct payload_mm_authvar_presence_arm *sealed_arm;
	struct payload_mm_authvar_presence_transaction_slot *sealed_slot;
	struct payload_mm_authvar_presence_transaction_page *sealed_page;
	struct smm_invocation_evidence *sealed_evidence;
	const struct smm_invocation_loader_composition *sealed_composition;
	const struct smm_invocation_loader_instance *sealed_instance;
	const struct smm_invocation_topology *sealed_topology;
	struct smm_invocation_loader_instance instance_snapshot;
	struct smm_invocation_loader_instance sealed_instance_snapshot;
	struct smm_invocation_topology topology_snapshot;
	struct smm_invocation_topology sealed_topology_snapshot;
	struct payload_mm_authvar_presence_transaction_binding binding;
	struct payload_mm_authvar_presence_transaction_binding sealed_binding;
	struct payload_mm_authvar_presence_route_authority_policy authority_policy;
	struct payload_mm_authvar_presence_route_authority_policy
		sealed_authority_policy;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_save_state_ops sealed_ops;
	struct smm_invocation_token token;
	struct payload_mm_authvar_presence_transaction_invocation invocation;
	struct smm_invocation_entry_ticket active_ticket;
	struct smm_invocation_entry_ticket sealed_active_ticket;
	uint8_t authority_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX];
	uint8_t sealed_authority_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX];
	payload_mm_authvar_protected_storage protected_storage;
	void *protected_storage_context;
	payload_mm_authvar_protected_storage sealed_protected_storage;
	void *sealed_protected_storage_context;
	struct payload_mm_authvar_presence_route_context callback_context;
	struct payload_mm_authvar_presence_route_context sealed_callback_context;
	payload_mm_authvar_presence_transaction_fail_stop_fn failure_callback;
	size_t failure_context_size;
	uint8_t failure_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX];
	payload_mm_authvar_presence_transaction_fail_stop_fn sealed_failure_callback;
	size_t sealed_failure_context_size;
	uint8_t sealed_failure_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX];
} __aligned(8);

enum cb_err payload_mm_authvar_presence_route_session_provision(
	struct payload_mm_authvar_presence_route_session *session,
	struct payload_mm_authvar_presence_arm *arm,
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct payload_mm_authvar_presence_route_authority_policy
		*authority_policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *page_verifier,
	struct bootmem_reservation_receipt *page_receipt,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context);

enum cb_err payload_mm_authvar_presence_route_session_arrive(
	struct payload_mm_authvar_presence_route_session *session,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket);

enum smm_apmc_dispatch_result
payload_mm_authvar_presence_route_session_dispatch_locked(
	struct payload_mm_authvar_presence_route_session *session,
	const struct smm_invocation_entry_ticket *ticket,
	struct smm_apmc_selection_receipt *receipt);

/* On success, the caller's literal next action releases the handler lock. */
void payload_mm_authvar_presence_route_session_prepare_lock_release(
	struct payload_mm_authvar_presence_route_session *session,
	const struct smm_invocation_entry_ticket *ticket);

enum payload_mm_authvar_presence_route_departure
payload_mm_authvar_presence_route_session_depart(
	struct payload_mm_authvar_presence_route_session *session,
	const struct smm_invocation_entry_ticket *ticket);

#endif
