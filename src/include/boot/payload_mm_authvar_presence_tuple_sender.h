/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_H

#include <boot/payload_mm_authvar_presence_transaction.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_REVISION 1U

struct payload_mm_authvar_presence_tuple_sender {
	uint32_t revision;
	uint32_t size;
	struct payload_mm_authvar_presence_transaction_page *page;
};

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
