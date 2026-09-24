/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_SEAL_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_SEAL_H

#include <boot/payload_mm_authvar_mor_grant.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_MOR_SEAL_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_MOR_SEAL_CAPABILITY_SIZE 32U

enum payload_mm_authvar_mor_seal_command {
	PAYLOAD_MM_AUTHVAR_MOR_SEAL_INSTALL = 1,
	PAYLOAD_MM_AUTHVAR_MOR_SEAL_CLOSE = 2,
};

/* Fixed private message. It is neither an OS ABI nor an SMMSTORE command. */
struct payload_mm_authvar_mor_seal_request {
	uint32_t revision;
	uint32_t size;
	uint32_t command;
	uint32_t reserved;
	uint8_t capability[PAYLOAD_MM_AUTHVAR_MOR_SEAL_CAPABILITY_SIZE];
	struct payload_mm_authvar_mor_grant grant;
} __aligned(8);

/*
 * Provision the same descriptor privately to ramstage and SMM. The capability
 * is delivered through protected SMM initialization, never in a public table.
 * caller and caller_context are platform-defined observations, not request
 * fields. A future platform composition supplies the fixed trigger and the
 * independent observations made by its SMM handler.
 */
struct payload_mm_authvar_mor_seal_channel {
	uint64_t transport_base;
	uint64_t transport_size;
	uint64_t caller;
	uint64_t caller_context;
	uint8_t capability[PAYLOAD_MM_AUTHVAR_MOR_SEAL_CAPABILITY_SIZE];
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_mor_seal_request) == 552,
	"MOR seal request ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_seal_channel) == 64,
	"MOR seal channel ABI changed");
_Static_assert(_Alignof(struct payload_mm_authvar_mor_seal_request) == 8 &&
	_Alignof(struct payload_mm_authvar_mor_seal_channel) == 8,
	"MOR seal ABI alignment changed");

typedef enum cb_err (*payload_mm_authvar_mor_seal_trigger)(void *context);
typedef bool (*payload_mm_authvar_mor_seal_range_check)(const void *base,
	size_t size);

#if !ENV_SMM || ENV_TEST
/* Both calls consume and scrub the mutable ramstage channel copy. */
enum cb_err payload_mm_authvar_mor_seal_send_install(
	struct payload_mm_authvar_mor_seal_channel *channel,
	const struct payload_mm_authvar_mor_grant *grant,
	payload_mm_authvar_mor_seal_trigger trigger, void *context);
enum cb_err payload_mm_authvar_mor_seal_send_close(
	struct payload_mm_authvar_mor_seal_channel *channel,
	payload_mm_authvar_mor_seal_trigger trigger, void *context);
#endif

#if ENV_SMM || ENV_TEST
/* One trusted SMM initialization attempt installs the fixed private channel. */
enum cb_err payload_mm_authvar_mor_seal_channel_install(
	const struct payload_mm_authvar_mor_seal_channel *channel,
	payload_mm_authvar_mor_seal_range_check protected_storage,
	payload_mm_authvar_mor_seal_range_check fixed_transport);

/*
 * A private platform handler supplies independently observed caller facts.
 * The first receive attempt is terminal, including malformed input.
 */
enum cb_err payload_mm_authvar_mor_seal_receive(
	const struct payload_mm_authvar_mor_seal_request *observed_transport,
	size_t observed_size, uint64_t observed_caller,
	uint64_t observed_caller_context);
#endif

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_MOR_SEAL_H */
