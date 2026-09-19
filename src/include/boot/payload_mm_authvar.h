/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define PAYLOAD_MM_AUTHVAR_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_REQUEST_REVISION 1U

#define PAYLOAD_MM_AUTHVAR_COREBOOT_SMM_OWNER (1U << 0)
#define PAYLOAD_MM_AUTHVAR_SMM_ONLY_SPI        (1U << 1)
#define PAYLOAD_MM_AUTHVAR_BOUNDED_STORE       (1U << 2)
#define PAYLOAD_MM_AUTHVAR_VALIDATED_COMM      (1U << 3)
#define PAYLOAD_MM_AUTHVAR_CDK2_POLICY_REQUIRED (1U << 4)
#define PAYLOAD_MM_AUTHVAR_NO_RAW_FLASH        (1U << 5)
#define PAYLOAD_MM_AUTHVAR_REQUIRED_FLAGS \
	(PAYLOAD_MM_AUTHVAR_COREBOOT_SMM_OWNER | \
	 PAYLOAD_MM_AUTHVAR_SMM_ONLY_SPI | \
	 PAYLOAD_MM_AUTHVAR_BOUNDED_STORE | \
	 PAYLOAD_MM_AUTHVAR_VALIDATED_COMM | \
	 PAYLOAD_MM_AUTHVAR_CDK2_POLICY_REQUIRED | \
	 PAYLOAD_MM_AUTHVAR_NO_RAW_FLASH)

#define PAYLOAD_MM_AUTHVAR_COMMUNICATE 1U
#define PAYLOAD_MM_AUTHVAR_MIN_COMM_BYTES 64U
#define PAYLOAD_MM_AUTHVAR_MIN_STORE_BLOCKS 3U

struct payload_mm_authvar_range {
	uint64_t base;
	uint64_t size;
};

/* Immutable facts established by coreboot before any Payload-MM dispatch. */
struct payload_mm_authvar_contract {
	uint32_t revision;
	uint32_t size;
	uint32_t flags;
	uint32_t smm_address_bits;
	uint64_t generation;
	struct payload_mm_authvar_range smram;
	struct payload_mm_authvar_range communication;
	uint64_t boot_media_size;
	uint64_t store_offset;
	uint64_t store_size;
	uint32_t block_size;
	uint32_t erase_size;
	uint32_t reserved[2];
};

struct payload_mm_authvar_platform {
	uint32_t smm_address_bits;
	uint64_t generation;
	struct payload_mm_authvar_range smram;
	struct payload_mm_authvar_range communication;
	uint64_t boot_media_size;
	uint64_t store_offset;
	uint64_t store_size;
	uint32_t block_size;
	uint32_t erase_size;
	bool (*smm_entry_owned)(void *context);
	bool (*spi_writes_restricted_to_smm)(void *context);
	bool (*raw_flash_transport_absent)(void *context);
	bool (*communication_region_reserved)(void *context, uint64_t base,
		uint64_t size);
	bool (*store_region_owned_by_smm)(void *context, uint64_t offset,
		uint64_t size);
	void *context;
};

/*
 * This request carries only an opaque policy message in the pre-approved
 * communication range. It deliberately contains no flash offset or command.
 */
struct payload_mm_authvar_request {
	uint32_t revision;
	uint32_t size;
	uint32_t operation;
	uint32_t flags;
	uint64_t generation;
	uint64_t message_address;
	uint32_t message_size;
	uint32_t reserved;
};

_Static_assert(sizeof(struct payload_mm_authvar_range) == 16,
	"payload_mm_authvar_range ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_contract) == 96,
	"payload_mm_authvar_contract ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_request) == 40,
	"payload_mm_authvar_request ABI changed");

enum cb_err payload_mm_authvar_contract_build(
	struct payload_mm_authvar_contract *contract,
	const struct payload_mm_authvar_platform *platform);

/* Called once by coreboot-controlled SMM initialization before exposure. */
typedef bool (*payload_mm_authvar_protected_storage)(void *context,
	const void *storage, size_t size);
enum cb_err payload_mm_authvar_authority_install(
	const struct payload_mm_authvar_contract *trusted_contract,
	payload_mm_authvar_protected_storage storage_is_protected, void *context);

/*
 * Copy an untrusted request and its opaque message into SMM-owned storage.
 * The caller must parse only the returned copies and never reread shared RAM.
 */
enum cb_err payload_mm_authvar_request_copy(uint64_t request_address,
	struct payload_mm_authvar_request *trusted_request, void *trusted_message,
	size_t trusted_message_capacity, size_t *trusted_message_size);

#endif
