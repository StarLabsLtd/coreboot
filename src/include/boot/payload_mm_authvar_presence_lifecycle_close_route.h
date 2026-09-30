/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_H

#include <boot/payload_mm_authvar_presence_lifecycle_close.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_transport.h>
#include <cpu/x86/smm_invocation_entry.h>
#include <cpu/x86/smm_invocation_loader_composition.h>
#include <cpu/x86/smm_invocation_topology.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_POLICY_REVISION 1U

/* Terminal semantic identity only; no route pointer or live state. */
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_IDLE_SNAPSHOT)
struct payload_mm_authvar_presence_lifecycle_close_snapshot {
	struct lb_authvar_presence_lifecycle_close_endpoint endpoint;
	uint64_t backing_base;
	uint64_t backing_bytes;
};
#endif

struct payload_mm_authvar_presence_lifecycle_close_internal_policy {
	uint32_t revision;
	uint32_t size;
	payload_mm_authvar_presence_transaction_claim_fn claim;
	payload_mm_authvar_presence_transaction_complete_fn complete;
	payload_mm_authvar_presence_transaction_range_fn dma_protected;
	void *context;
	size_t context_size;
};

struct payload_mm_authvar_presence_lifecycle_close_route {
	uint32_t state;
	uint32_t active_source;
	uint64_t generation;
	uint64_t sealed_generation;
	uint64_t predecessor_invocation_generation;
	uint64_t sealed_predecessor_invocation_generation;
	uint64_t backing_base;
	uint64_t backing_size;
	uint64_t sealed_backing_base;
	uint64_t sealed_backing_size;
	struct smm_invocation_evidence *evidence;
	struct smm_invocation_evidence *sealed_evidence;
	const struct smm_invocation_loader_composition *composition;
	const struct smm_invocation_loader_composition *sealed_composition;
	const struct smm_invocation_loader_instance *instance;
	const struct smm_invocation_loader_instance *sealed_instance;
	const struct smm_invocation_topology *topology;
	const struct smm_invocation_topology *sealed_topology;
	struct payload_mm_authvar_presence_transaction_slot *transaction_slot;
	struct payload_mm_authvar_presence_transaction_slot *sealed_transaction_slot;
	struct smm_invocation_loader_instance instance_snapshot;
	struct smm_invocation_loader_instance sealed_instance_snapshot;
	struct smm_invocation_topology topology_snapshot;
	struct smm_invocation_topology sealed_topology_snapshot;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_save_state_ops sealed_ops;
	struct payload_mm_authvar_presence_lifecycle_close_internal_policy internal;
	struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		sealed_internal;
	uint8_t internal_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX];
	uint8_t sealed_internal_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX];
	uint8_t ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX];
	uint8_t sealed_ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX];
	payload_mm_authvar_protected_storage protected_storage;
	payload_mm_authvar_protected_storage sealed_protected_storage;
	void *protected_storage_context;
	void *sealed_protected_storage_context;
	struct smm_invocation_token token;
	struct smm_invocation_token sealed_token;
	struct payload_mm_authvar_presence_transaction_invocation invocation;
	struct payload_mm_authvar_presence_transaction_invocation sealed_invocation;
	struct smm_invocation_entry_ticket active_ticket;
	struct smm_invocation_entry_ticket sealed_active_ticket;
	uint64_t last_invocation_generation;
	uint64_t last_invocation_generation_inverse;
} __aligned(8);

enum payload_mm_authvar_presence_lifecycle_close_route_departure {
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_PARTICIPANT_DEPARTED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_BSP_EOS_CONSUMED,
};

enum cb_err payload_mm_authvar_presence_lifecycle_close_route_provision(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor,
	struct payload_mm_authvar_presence_transaction_slot *transaction_slot,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		*internal,
	uint64_t predecessor_invocation_generation,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context,
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt *receipt);

enum cb_err payload_mm_authvar_presence_lifecycle_close_route_arrive(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket);
enum smm_apmc_dispatch_result
payload_mm_authvar_presence_lifecycle_close_route_dispatch_locked(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket,
	struct smm_apmc_selection_receipt *selection);
void payload_mm_authvar_presence_lifecycle_close_route_prepare_lock_release(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket);
enum payload_mm_authvar_presence_lifecycle_close_route_departure
payload_mm_authvar_presence_lifecycle_close_route_depart(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	const struct smm_invocation_entry_ticket *ticket);
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_IDLE_SNAPSHOT)
enum cb_err payload_mm_authvar_presence_lifecycle_close_route_idle_snapshot(
	struct payload_mm_authvar_presence_lifecycle_close_route *route,
	struct payload_mm_authvar_presence_lifecycle_close_snapshot *snapshot,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context, size_t storage_context_size);
#endif

#endif
