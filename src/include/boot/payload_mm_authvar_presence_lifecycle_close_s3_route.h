/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_ROUTE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_ROUTE_H

#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_wire.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_entry.h>
#include <cpu/x86/smm_invocation_loader_composition.h>
#include <cpu/x86/smm_invocation_topology.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_POLICY_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_CONTEXT_MAX 128U

/* Protected, fresh SMM input. It is not persistent S3 state. */
struct payload_mm_authvar_presence_lifecycle_close_s3_policy {
	uint32_t revision;
	uint32_t size;
	uint64_t backing_base;
	uint64_t backing_size;
	uint32_t backing_tag;
	uint32_t reserved;
	payload_mm_authvar_presence_range_proof_fn dram_provenance;
	void *dram_provenance_context;
	size_t dram_provenance_context_size;
	payload_mm_authvar_presence_range_proof_fn dma_protected;
	void *dma_context;
	size_t dma_context_size;
};

struct payload_mm_authvar_presence_lifecycle_close_s3_route {
	uint32_t state;
	uint32_t reserved;
	struct lb_authvar_presence_lifecycle_close_endpoint endpoint;
	struct lb_authvar_presence_lifecycle_close_endpoint sealed_endpoint;
	uint64_t backing_base;
	uint64_t backing_size;
	uint64_t sealed_backing_base;
	uint64_t sealed_backing_size;
	uint32_t backing_tag;
	uint32_t sealed_backing_tag;
	const struct smm_invocation_loader_composition *composition;
	const struct smm_invocation_loader_composition *sealed_composition;
	const struct smm_invocation_loader_instance *instance;
	const struct smm_invocation_loader_instance *sealed_instance;
	struct smm_invocation_evidence *evidence;
	struct smm_invocation_evidence *sealed_evidence;
	const struct smm_invocation_topology *topology;
	const struct smm_invocation_topology *sealed_topology;
	struct smm_invocation_loader_instance instance_snapshot;
	struct smm_invocation_loader_instance sealed_instance_snapshot;
	struct smm_invocation_topology topology_snapshot;
	struct smm_invocation_topology sealed_topology_snapshot;
	struct smm_invocation_save_state_ops ops;
	struct smm_invocation_save_state_ops sealed_ops;
	uint8_t ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX];
	uint8_t sealed_ops_context[SMM_INVOCATION_SAVE_STATE_CONTEXT_MAX];
	payload_mm_authvar_presence_range_proof_fn dma_protected;
	payload_mm_authvar_presence_range_proof_fn sealed_dma_protected;
	uint8_t dma_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_CONTEXT_MAX];
	uint8_t sealed_dma_context[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_CONTEXT_MAX];
	size_t dma_context_size;
	size_t sealed_dma_context_size;
	payload_mm_authvar_protected_storage protected_storage;
	payload_mm_authvar_protected_storage sealed_protected_storage;
	void *protected_storage_context;
	void *sealed_protected_storage_context;
	size_t protected_storage_context_size;
	size_t sealed_protected_storage_context_size;
	uint8_t protected_storage_context_snapshot[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_CONTEXT_MAX];
	uint8_t sealed_protected_storage_context_snapshot[
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_CONTEXT_MAX];
	struct smm_invocation_token token;
	struct smm_invocation_token sealed_token;
	struct smm_invocation_entry_ticket active_ticket;
	struct smm_invocation_entry_ticket sealed_active_ticket;
	uint64_t predecessor_invocation_generation;
	uint64_t sealed_predecessor_invocation_generation;
	struct smm_invocation_loader_instance_nonce predecessor_loader_instance_nonce;
	struct smm_invocation_loader_instance_nonce
		sealed_predecessor_loader_instance_nonce;
	uint32_t predecessor_lifecycle;
	uint32_t sealed_predecessor_lifecycle;
	uint32_t predecessor_eos_consumed;
	uint32_t sealed_predecessor_eos_consumed;
	uint64_t last_invocation_generation;
	uint64_t last_invocation_generation_inverse;
} __aligned(8);

/*
 * protected_storage_context is either NULL for a stateless verifier or a fresh,
 * bounded protected runtime-verifier identity. Its exact bytes remain sealed.
 */
enum cb_err payload_mm_authvar_presence_lifecycle_close_s3_route_provision(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_save_state_ops *ops,
	const struct payload_mm_authvar_presence_lifecycle_close_s3_policy *policy,
	payload_mm_authvar_protected_storage protected_storage,
	void *protected_storage_context, size_t protected_storage_context_size);

enum cb_err payload_mm_authvar_presence_lifecycle_close_s3_route_arrive(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket);
enum smm_apmc_dispatch_result
payload_mm_authvar_presence_lifecycle_close_s3_route_dispatch_locked(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket,
	struct smm_apmc_selection_receipt *selection);
void payload_mm_authvar_presence_lifecycle_close_s3_route_prepare_lock_release(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket);
enum payload_mm_authvar_presence_lifecycle_close_s3_route_departure {
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_PARTICIPANT_DEPARTED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_S3_BSP_EOS_CONSUMED,
};
enum payload_mm_authvar_presence_lifecycle_close_s3_route_departure
payload_mm_authvar_presence_lifecycle_close_s3_route_depart(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct smm_invocation_entry_ticket *ticket);
bool payload_mm_authvar_presence_lifecycle_close_s3_route_idle_exact(
	struct payload_mm_authvar_presence_lifecycle_close_s3_route *route,
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	uint64_t backing_base, uint64_t backing_size);

#endif
