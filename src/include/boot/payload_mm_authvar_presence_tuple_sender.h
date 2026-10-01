/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_H

#include <boot/payload_mm_authvar_presence_transaction.h>
#if CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <cpu/x86/smm_invocation_topology.h>
#endif

#define PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_REVISION 1U

struct payload_mm_authvar_presence_tuple_sender {
	uint32_t revision;
	uint32_t size;
	struct payload_mm_authvar_presence_transaction_page *page;
};

#if CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
/* The canonical transport's early reservation and protected loader boundary. */
enum cb_err payload_mm_authvar_presence_tuple_sender_reserve(void);
enum cb_err payload_mm_authvar_presence_tuple_sender_loader_provision(
	struct payload_mm_authvar_presence_bootstrap *slot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_topology *topology, bool required);
void payload_mm_authvar_presence_tuple_sender_close(void);
/* One post-map take; the bootmem owners authenticate both exact reservations. */
enum cb_err payload_mm_authvar_presence_tuple_sender_receipts_take(
	struct payload_mm_authvar_presence_bootstrap_receipts *receipts,
	struct payload_mm_authvar_presence_tuple_sender *sender);
#endif

enum cb_err payload_mm_authvar_presence_tuple_sender_prepare(
	void *context, const struct payload_mm_authvar_presence_seed *seed,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value);
enum cb_err payload_mm_authvar_presence_tuple_sender_commit(
	void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value);
enum cb_err payload_mm_authvar_presence_tuple_sender_abort(
	void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value);

#endif
