/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef COMMONLIB_CAPSULE_SERVICE_H
#define COMMONLIB_CAPSULE_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#define LB_TAG_CAPSULE_HANDOFF 0x0052U
#define LB_TAG_CAPSULE_BROKER_ENDPOINT 0x0054U
#define LB_CAPSULE_HANDOFF_REVISION 2U

#define LB_CAPSULE_HANDOFF_AUTHENTICATED (1U << 0)
#define LB_CAPSULE_HANDOFF_RESET_REQUIRED (1U << 1)
#define LB_CAPSULE_HANDOFF_REQUIRED_FLAGS \
	(LB_CAPSULE_HANDOFF_AUTHENTICATED | \
	 LB_CAPSULE_HANDOFF_RESET_REQUIRED)

#define LB_CAPSULE_BROKER_COREBOOT_UPDATE 1U
#define LB_CAPSULE_BROKER_APPLY_REGIONS     (1U << 0)
#define LB_CAPSULE_BROKER_PRESERVE_UNLISTED (1U << 1)
#define LB_CAPSULE_BROKER_VERIFY_READBACK   (1U << 2)
#define LB_CAPSULE_BROKER_REQUIRED_CAPABILITIES \
	(LB_CAPSULE_BROKER_APPLY_REGIONS | \
	 LB_CAPSULE_BROKER_PRESERVE_UNLISTED | \
	 LB_CAPSULE_BROKER_VERIFY_READBACK)

#define LB_CAPSULE_FORMAT_FMP_V3 3U
#define LB_CAPSULE_AUTH_EFI_PKCS7 1U
#define LB_CAPSULE_BOARD_BINDING_CBFS_BUILD_INFO_V1 1U
#define LB_CAPSULE_PAYLOAD_MSS1_V1 1U
#define LB_CAPSULE_FLAGS_PERSIST_RESET 0x00050000U

#define LB_CAPSULE_REGION_BIOS (1U << 0)
#define LB_CAPSULE_REGION_VALID_FLAGS LB_CAPSULE_REGION_BIOS

struct lb_capsule_update_region {
	uint64_t image_offset;
	uint64_t flash_offset;
	uint64_t size;
	uint32_t flags;
	uint32_t reserved;
} __attribute__((packed));

struct lb_capsule_handoff {
	uint32_t tag;
	uint32_t size;
	uint16_t revision;
	uint16_t header_size;
	uint32_t flags;
	uint16_t broker_type;
	uint16_t capsule_format;
	uint16_t authentication_format;
	uint16_t board_binding_format;
	uint16_t payload_format;
	uint16_t reserved16;
	uint32_t broker_capabilities;
	uint8_t image_type_guid[16];
	uint32_t version;
	uint32_t lowest_supported_version;
	uint32_t capsule_flags;
	uint32_t block_size;
	uint32_t erase_size;
	uint32_t region_count;
	uint64_t image_size;
	uint64_t boot_media_size;
	uint64_t smmstore_offset;
	uint64_t smmstore_size;
	uint32_t reserved32[2];
	struct lb_capsule_update_region regions[];
} __attribute__((packed));

_Static_assert(sizeof(struct lb_capsule_update_region) == 32U,
	"capsule update region ABI");
_Static_assert(sizeof(struct lb_capsule_handoff) == 112U,
	"capsule handoff header ABI");
_Static_assert(offsetof(struct lb_capsule_handoff, broker_type) == 16U &&
	offsetof(struct lb_capsule_handoff, broker_capabilities) == 28U,
	"capsule broker ABI");
_Static_assert(offsetof(struct lb_capsule_handoff, image_type_guid) == 32U &&
	offsetof(struct lb_capsule_handoff, version) == 48U,
	"capsule identity ABI");
_Static_assert(offsetof(struct lb_capsule_handoff, image_size) == 72U &&
	offsetof(struct lb_capsule_handoff, regions) == 112U,
	"capsule bounds ABI");

#define LB_CAPSULE_BROKER_ENDPOINT_REVISION 1U
#define LB_CAPSULE_BROKER_ENDPOINT_RAM_REVISION 2U

#define LB_CAPSULE_ENDPOINT_COREBOOT_SMM_OWNER  (1U << 0)
#define LB_CAPSULE_ENDPOINT_SMM_ONLY_SPI        (1U << 1)
#define LB_CAPSULE_ENDPOINT_FIXED_COMMUNICATION (1U << 2)
#define LB_CAPSULE_ENDPOINT_DMA_PROTECTED       (1U << 3)
#define LB_CAPSULE_ENDPOINT_CPU_RENDEZVOUS      (1U << 4)
#define LB_CAPSULE_ENDPOINT_ONE_SHOT            (1U << 5)
#define LB_CAPSULE_ENDPOINT_NO_RAW_FLASH         (1U << 6)
#define LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS \
	(LB_CAPSULE_ENDPOINT_COREBOOT_SMM_OWNER | \
	 LB_CAPSULE_ENDPOINT_SMM_ONLY_SPI | \
	 LB_CAPSULE_ENDPOINT_FIXED_COMMUNICATION | \
	 LB_CAPSULE_ENDPOINT_DMA_PROTECTED | \
	 LB_CAPSULE_ENDPOINT_CPU_RENDEZVOUS | \
	 LB_CAPSULE_ENDPOINT_ONE_SHOT | \
	 LB_CAPSULE_ENDPOINT_NO_RAW_FLASH)

#define LB_CAPSULE_ENDPOINT_CONTENT_SNAPSHOT (1U << 7)
#define LB_CAPSULE_ENDPOINT_SNAPSHOT_FLAGS \
	((LB_CAPSULE_ENDPOINT_REQUIRED_FLAGS & ~LB_CAPSULE_ENDPOINT_DMA_PROTECTED) | \
	 LB_CAPSULE_ENDPOINT_CONTENT_SNAPSHOT)

#define LB_CAPSULE_ENDPOINT_TRANSPORT_APM_IO8 1U

struct lb_capsule_broker_endpoint {
	uint32_t tag;
	uint32_t size;
	uint16_t revision;
	uint16_t header_size;
	uint32_t flags;
	uint64_t generation;
	uint64_t communication_base;
	uint32_t communication_size;
	uint32_t message_size;
	uint64_t staging_base;
	uint64_t staging_size;
	uint16_t transport;
	uint16_t trigger_width;
	uint32_t trigger_address;
	uint32_t trigger_value;
	uint32_t reserved[3];
} __attribute__((packed));

_Static_assert(sizeof(struct lb_capsule_broker_endpoint) == 80U,
	"capsule broker endpoint ABI");
_Static_assert(offsetof(struct lb_capsule_broker_endpoint, generation) == 16U &&
	offsetof(struct lb_capsule_broker_endpoint, communication_base) == 24U &&
	offsetof(struct lb_capsule_broker_endpoint, staging_base) == 40U &&
	offsetof(struct lb_capsule_broker_endpoint, transport) == 56U &&
	offsetof(struct lb_capsule_broker_endpoint, reserved) == 68U,
	"capsule broker endpoint layout");


#endif
