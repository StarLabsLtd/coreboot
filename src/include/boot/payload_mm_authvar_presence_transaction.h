/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_H

#include <boot/payload_mm_authvar.h>
#include <boot/payload_mm_authvar_presence_producer.h>
#include <bootmem_reservation_receipt.h>
#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION 2U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CAPABILITY_SIZE 32U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE 4096U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL UINT64_MAX

enum payload_mm_authvar_presence_transaction_decision {
	PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE = 1U,
	PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT = 2U,
	PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT = 3U,
};

enum payload_mm_authvar_presence_transaction_status {
	PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ACCEPTED = 0x41555448U,
};

enum payload_mm_authvar_presence_backing_status {
	PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_TRANSFERRED = 0x4f574e44U,
	PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_CLEANED = 0x434c454eU,
};

struct payload_mm_authvar_presence_transaction_binding {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint64_t transaction_id;
	uint64_t nonce;
	uint32_t initiator_cpu;
	uint32_t maximum_cpus;
	uint8_t capability[
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CAPABILITY_SIZE];
	uint32_t reserved[2];
} __aligned(8);

struct payload_mm_authvar_presence_transaction_request {
	struct payload_mm_authvar_presence_transaction_binding binding;
	uint32_t decision;
	uint32_t transport_status;
	uint32_t operation_status;
	uint32_t reserved;
	struct payload_mm_authvar_presence_seed seed;
} __aligned(8);

struct payload_mm_authvar_presence_transaction_ack {
	struct payload_mm_authvar_presence_transaction_binding binding;
	uint32_t decision;
	uint32_t transport_status;
	uint32_t operation_status;
	uint32_t backing_status;
} __aligned(8);

struct payload_mm_authvar_presence_transaction_page {
	struct payload_mm_authvar_presence_transaction_request request;
	struct payload_mm_authvar_presence_transaction_ack ack;
	uint8_t reserved[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE -
		sizeof(struct payload_mm_authvar_presence_transaction_request) -
		sizeof(struct payload_mm_authvar_presence_transaction_ack)];
} __aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);

typedef enum cb_err (*payload_mm_authvar_presence_transaction_prepare_fn)(
	void *context, const struct payload_mm_authvar_presence_seed *seed,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_rax);
typedef enum cb_err (*payload_mm_authvar_presence_transaction_decide_fn)(
	void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_rax);

#define PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX 128U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION 2U
typedef enum cb_err (*payload_mm_authvar_presence_transaction_smm_prepare_fn)(
	void *context, const struct payload_mm_authvar_presence_seed *seed,
	uint64_t generation);
typedef enum cb_err (*payload_mm_authvar_presence_transaction_smm_decide_fn)(
	void *context, uint64_t generation);
typedef bool (*payload_mm_authvar_presence_transaction_range_fn)(void *context,
	uint64_t base, uint64_t size);
typedef bool (*payload_mm_authvar_presence_transaction_state_fn)(void *context);
struct payload_mm_authvar_presence_transaction_invocation {
	uint32_t revision;
	uint32_t size;
	uint32_t initiator_cpu;
	uint32_t active_cpus;
	uint64_t smi_generation;
	uint64_t rendezvous_generation;
	uint64_t rendezvous_proof[3];
	uint32_t bsp;
	uint32_t reserved;
} __aligned(8);
#define PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION 1U
typedef enum cb_err (*payload_mm_authvar_presence_transaction_claim_fn)(
	void *context, uint64_t sentinel,
	struct payload_mm_authvar_presence_transaction_invocation *invocation);
/*
 * Success proves one initiating BSP, sentinel write/readback on its exact
 * save-state node, the actual active CPU count, and every active CPU in the
 * reported nonzero SMI/rendezvous generation. The opaque proof is immutable.
 */
typedef enum cb_err (*payload_mm_authvar_presence_transaction_publish_fn)(
	void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value);
typedef void (*payload_mm_authvar_presence_transaction_fail_stop_fn)(
	void *context) __noreturn;

struct payload_mm_authvar_presence_transaction_policy {
	uint32_t revision;
	uint32_t size;
	payload_mm_authvar_presence_transaction_smm_prepare_fn prepare;
	payload_mm_authvar_presence_transaction_smm_decide_fn commit;
	payload_mm_authvar_presence_transaction_smm_decide_fn abort;
	payload_mm_authvar_presence_transaction_range_fn dma_protected;
	payload_mm_authvar_presence_transaction_claim_fn claim_invocation;
	payload_mm_authvar_presence_transaction_publish_fn publish_and_verify_rax;
	payload_mm_authvar_presence_transaction_fail_stop_fn fail_stop;
	void *context;
	size_t context_size;
};

struct payload_mm_authvar_presence_transaction_slot {
	struct payload_mm_authvar_presence_transaction_binding binding;
	struct payload_mm_authvar_presence_transaction_policy policy;
	struct bootmem_reservation_receipt_authority page_verifier;
	struct bootmem_reservation_receipt page_receipt;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX];
	uint64_t page_base;
	uint64_t page_size;
	uint32_t state;
	uint32_t ack_published;
	uint32_t dispatch_owner;
	uint32_t reserved[2];
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_presence_transaction_page) ==
	PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE,
	"presence transaction page layout changed");
_Static_assert(sizeof(struct payload_mm_authvar_presence_transaction_invocation) ==
	64, "presence transaction invocation layout changed");

uint64_t payload_mm_authvar_presence_transaction_rax(
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision);
bool payload_mm_authvar_presence_transaction_ack_valid(
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision,
	const struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t saved_rax);

#if ENV_SMM || ENV_TEST
enum cb_err payload_mm_authvar_presence_transaction_provision(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_policy *policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *page_verifier,
	struct bootmem_reservation_receipt *page_receipt,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context);
/* Called only after a platform-private cause has been recognized and consumed. */
enum cb_err payload_mm_authvar_presence_transaction_dispatch(
	struct payload_mm_authvar_presence_transaction_slot *slot);
bool payload_mm_authvar_presence_transaction_dispatch_enabled(
	const struct payload_mm_authvar_presence_transaction_slot *slot,
	uint64_t generation);
#endif

#endif
