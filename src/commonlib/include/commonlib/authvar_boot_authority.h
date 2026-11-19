/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */

#ifndef COMMONLIB_AUTHVAR_BOOT_AUTHORITY_H
#define COMMONLIB_AUTHVAR_BOOT_AUTHORITY_H

#include <commonlib/bsd/compiler.h>
#include <stddef.h>
#include <stdint.h>

#define LB_TAG_AUTHVAR_PRESENCE_ENDPOINT 0x0056U
#define LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION 2U
#define LB_AUTHVAR_PRESENCE_COREBOOT_SMM_OWNER (1U << 0)
#define LB_AUTHVAR_PRESENCE_FIXED_SEED (1U << 1)
#define LB_AUTHVAR_PRESENCE_DMA_PROTECTED (1U << 2)
#define LB_AUTHVAR_PRESENCE_CPU_RENDEZVOUS (1U << 3)
#define LB_AUTHVAR_PRESENCE_ONE_SHOT_DELIVERY (1U << 4)
#define LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED (1U << 5)
#define LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS \
	(LB_AUTHVAR_PRESENCE_COREBOOT_SMM_OWNER | \
	 LB_AUTHVAR_PRESENCE_FIXED_SEED | \
	 LB_AUTHVAR_PRESENCE_CPU_RENDEZVOUS | \
	 LB_AUTHVAR_PRESENCE_ONE_SHOT_DELIVERY | \
	 LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED)
#define LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE 32U
#define AUTHVAR_BOOT_AUTHORITY_PAGE_SIZE 4096U
#define AUTHVAR_BOOT_AUTHORITY_BACKING_SIZE (2U * AUTHVAR_BOOT_AUTHORITY_PAGE_SIZE)

#define LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT 0x0057U
#define LB_AUTHVAR_CLOSE_ENDPOINT_REVISION 1U
#define LB_AUTHVAR_CLOSE_SMM_OWNER (1U << 0)
#define LB_AUTHVAR_CLOSE_FIXED_COMMUNICATION (1U << 1)
#define LB_AUTHVAR_CLOSE_DMA_PROTECTED (1U << 2)
#define LB_AUTHVAR_CLOSE_CPU_RENDEZVOUS (1U << 3)
#define LB_AUTHVAR_CLOSE_EXACT_GENERATION (1U << 4)
#define LB_AUTHVAR_CLOSE_BOOT_EPOCH_SCOPED (1U << 5)
#define LB_AUTHVAR_CLOSE_SOURCE_SCOPED (1U << 6)
#define LB_AUTHVAR_CLOSE_SYNCHRONOUS_CLOSED_PROOF (1U << 7)
#define LB_AUTHVAR_CLOSE_ONE_SHOT_SOURCES (1U << 8)
#define LB_AUTHVAR_CLOSE_TERMINAL_BEFORE_EBS (1U << 9)
#define LB_AUTHVAR_CLOSE_REQUIRED_FLAGS \
	(LB_AUTHVAR_CLOSE_SMM_OWNER | LB_AUTHVAR_CLOSE_FIXED_COMMUNICATION | \
	 LB_AUTHVAR_CLOSE_CPU_RENDEZVOUS | LB_AUTHVAR_CLOSE_EXACT_GENERATION | \
	 LB_AUTHVAR_CLOSE_BOOT_EPOCH_SCOPED | LB_AUTHVAR_CLOSE_SOURCE_SCOPED | \
	 LB_AUTHVAR_CLOSE_SYNCHRONOUS_CLOSED_PROOF | LB_AUTHVAR_CLOSE_ONE_SHOT_SOURCES | \
	 LB_AUTHVAR_CLOSE_TERMINAL_BEFORE_EBS)
#define LB_AUTHVAR_CLOSE_SOURCE_MASK 7U
#define LB_AUTHVAR_CLOSE_PRE_EXTERNAL_IMAGE 1U
#define LB_AUTHVAR_CLOSE_FAILURE_OR_RETURN 2U
#define LB_AUTHVAR_CLOSE_CLOSED_REPROOF 4U
#define AUTHVAR_CLOSE_REVISION 1U
#define AUTHVAR_CLOSE_PENDING UINT32_MAX
#define AUTHVAR_CLOSE_STATUS_PENDING UINT64_MAX
#define AUTHVAR_CLOSE_WIRE_SENTINEL 0xfffffffffffffffeULL
#define AUTHVAR_CLOSE_WIRE_SUCCESS 0x434c4f5345414bfeULL

/* Metadata only. The seed is delivered once in a separate reserved page. */
struct lb_authvar_presence_endpoint {
	uint32_t tag;
	uint32_t size;
	uint16_t revision;
	uint16_t header_size;
	uint32_t flags;
	uint64_t generation;
	uint64_t seed_base;
	uint32_t seed_size;
	uint32_t capability_size;
} __packed __aligned(4);

struct authvar_boot_seed {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];
} __aligned(8);

struct lb_authvar_presence_lifecycle_close_endpoint {
	uint32_t tag;
	uint32_t size;
	uint16_t revision;
	uint16_t header_size;
	uint32_t flags;
	uint64_t generation;
	uint64_t communication_base;
	uint32_t communication_size;
	uint32_t message_size;
	uint16_t transport;
	uint16_t trigger_width;
	uint32_t trigger_address;
	uint32_t trigger_value;
	uint32_t source_mask;
	uint32_t reserved[2];
} __packed __aligned(4);

struct authvar_close_message {
	uint32_t revision;
	uint32_t size;
	uint32_t source;
	uint32_t flags;
	uint64_t generation;
	uint64_t request_id;
	uint64_t status;
	uint32_t reserved[5];
	uint32_t completion;
} __aligned(8);

_Static_assert(sizeof(struct lb_authvar_presence_endpoint) == 40U &&
	offsetof(struct lb_authvar_presence_endpoint, generation) == 16U &&
	offsetof(struct lb_authvar_presence_endpoint, seed_base) == 24U &&
	offsetof(struct lb_authvar_presence_endpoint, seed_size) == 32U,
	"authenticated-variable boot authority record ABI");
_Static_assert(sizeof(struct authvar_boot_seed) == 48U &&
	offsetof(struct authvar_boot_seed, capability) == 16U,
	"authenticated-variable boot seed ABI");
_Static_assert(sizeof(struct lb_authvar_presence_lifecycle_close_endpoint) == 64U &&
	offsetof(struct lb_authvar_presence_lifecycle_close_endpoint, generation) == 16U &&
	offsetof(struct lb_authvar_presence_lifecycle_close_endpoint, source_mask) == 52U,
	"authenticated-variable terminal-close record ABI");
_Static_assert(sizeof(struct authvar_close_message) == 64U &&
	offsetof(struct authvar_close_message, status) == 32U &&
	offsetof(struct authvar_close_message, completion) == 60U,
	"authenticated-variable terminal-close message ABI");

#endif
