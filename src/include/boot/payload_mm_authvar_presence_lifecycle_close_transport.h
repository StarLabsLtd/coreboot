/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT_H

#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <boot/payload_mm_authvar_presence_producer.h>
#include <commonlib/bsd/cb_err.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL \
	0xfffffffffffffffeULL
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS \
	0x434c4f5345414bfeULL
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REQUEST \
	0x494e5354U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_RECEIPT \
	0x52435054U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_FRAME_REJECTED \
	0x52454a54U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_REQUEST \
	0x494e53feU
#define PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_WIRE_SUCCESS \
	0x434c4f53454b4ffeULL

struct payload_mm_authvar_presence_lifecycle_close_install_descriptor {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint64_t backing_base;
	uint64_t backing_size;
	uint64_t challenge[4];
	uint32_t message_size;
	uint32_t trigger_address;
	uint32_t trigger_value;
	uint32_t source_mask;
	uint32_t reserved;
} __aligned(8);

struct payload_mm_authvar_presence_lifecycle_close_install_receipt {
	struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		descriptor;
	uint64_t protected_route_identity;
	uint64_t route_nonce;
	uint32_t installed;
	uint32_t reserved;
} __aligned(8);

struct payload_mm_authvar_presence_lifecycle_close_install_frame {
	uint32_t revision;
	uint32_t size;
	uint32_t state;
	uint32_t reserved;
	struct payload_mm_authvar_presence_lifecycle_close_install_descriptor request;
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt receipt;
} __aligned(8);

/* Error proves no installation mutation; ambiguity must fail-stop internally. */
enum cb_err platform_payload_mm_authvar_presence_lifecycle_close_route_install(
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor,
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt *receipt);
void __noreturn
platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop(void);
enum cb_err payload_mm_authvar_presence_lifecycle_close_send(
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	uint32_t source);

#if ENV_TEST
void payload_mm_authvar_presence_lifecycle_close_provider_reset_test(void);
void payload_mm_authvar_presence_lifecycle_close_sender_reset_test(void);
#endif

#endif
