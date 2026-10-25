/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <uuid.h>

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

#define PAYLOAD_MM_FMP_STATE_POLICY_REVISION 1U
#define PAYLOAD_MM_FMP_STATE_MESSAGE_REVISION 1U
#define PAYLOAD_MM_FMP_STATE_WIRE_SIZE 20U
#define PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES 0x00000003U
#define PAYLOAD_MM_FMP_STATE_RESULT_PENDING UINT64_MAX
#define PAYLOAD_MM_FMP_STATE_NAME_CAPACITY 35U
#define PAYLOAD_MM_FMP_CAPSULE_INTENT_REVISION 1U
#define PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256 1U
#define PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE 32U

enum payload_mm_fmp_state_operation {
	PAYLOAD_MM_FMP_STATE_READ = 1,
	PAYLOAD_MM_FMP_STATE_WRITE_STATE = 2,
	PAYLOAD_MM_FMP_STATE_REMOVE_LEGACY = 3,
	PAYLOAD_MM_FMP_STATE_CLOSE_STATE = 4,
};

enum payload_mm_fmp_state_key {
	PAYLOAD_MM_FMP_STATE_KEY_STATE = 0,
	PAYLOAD_MM_FMP_STATE_KEY_VERSION = 1,
	PAYLOAD_MM_FMP_STATE_KEY_LOWEST_VERSION = 2,
	PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_STATUS = 3,
	PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION = 4,
	PAYLOAD_MM_FMP_STATE_KEY_NONE = UINT32_MAX,
};

enum payload_mm_fmp_capsule_operation {
	PAYLOAD_MM_FMP_CAPSULE_CHECK = 1,
	PAYLOAD_MM_FMP_CAPSULE_SET = 2,
};

/* Installed once from trusted coreboot state and retained only in SMRAM. */
struct payload_mm_fmp_state_policy {
	uint32_t revision;
	uint32_t size;
	guid_t namespace_guid;
	uint64_t hardware_instance;
	uint32_t trusted_lowest_version;
	uint32_t reserved;
};

/* Fixed request ABI carried inside payload_mm_authvar_request.message. */
struct payload_mm_fmp_state_message {
	uint32_t revision;
	uint32_t size;
	uint32_t operation;
	uint32_t key;
	uint64_t transaction;
	uint32_t attributes;
	uint32_t data_size;
	uint64_t result;
	uint8_t data[PAYLOAD_MM_FMP_STATE_WIRE_SIZE];
	uint32_t reserved;
} __aligned(8);

/* Fixed intent carrying the occupied capsule-envelope size and its digest. */
struct payload_mm_fmp_capsule_intent {
	uint32_t revision;
	uint32_t size;
	uint32_t operation;
	uint32_t flags;
	uint64_t broker_generation;
	uint64_t transaction;
	uint64_t capsule_size;
	uint32_t digest_algorithm;
	uint32_t digest_size;
	uint8_t digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE];
	uint32_t attempted_version;
	uint32_t reserved;
} __aligned(8);

/* SMM-owned parsed output. It is not a shared-memory ABI. */
struct payload_mm_fmp_state_command {
	struct payload_mm_fmp_state_message message;
	guid_t namespace_guid;
	uint64_t hardware_instance;
	uint32_t trusted_lowest_version;
	uint32_t variable_name_bytes;
	uint16_t variable_name[PAYLOAD_MM_FMP_STATE_NAME_CAPACITY];
};

struct payload_mm_authvar_range {
	uint64_t base;
	uint64_t size;
};

/*
 * The block size describes the logical FV/FVB layout block, not an SPI page.
 * The erase size is the physical erase granularity. Both are powers of two,
 * block_size is an integral multiple of erase_size, store_offset is erase
 * aligned, and store_size contains at least three integral logical blocks.
 */
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
_Static_assert(sizeof(struct payload_mm_fmp_state_policy) == 40,
	"payload_mm_fmp_state_policy ABI changed");
_Static_assert(sizeof(struct payload_mm_fmp_state_message) == 64,
	"payload_mm_fmp_state_message ABI changed");
_Static_assert(_Alignof(struct payload_mm_fmp_state_message) == 8,
	"payload_mm_fmp_state_message alignment changed");
_Static_assert(offsetof(struct payload_mm_fmp_state_message, transaction) == 16 &&
	offsetof(struct payload_mm_fmp_state_message, data) == 40 &&
	offsetof(struct payload_mm_fmp_state_message, reserved) == 60,
	"payload_mm_fmp_state_message layout changed");
_Static_assert(sizeof(struct payload_mm_fmp_capsule_intent) == 88,
	"payload_mm_fmp_capsule_intent ABI changed");
_Static_assert(_Alignof(struct payload_mm_fmp_capsule_intent) == 8,
	"payload_mm_fmp_capsule_intent alignment changed");
_Static_assert(offsetof(struct payload_mm_fmp_capsule_intent,
	broker_generation) == 16 &&
	offsetof(struct payload_mm_fmp_capsule_intent, capsule_size) == 32 &&
	offsetof(struct payload_mm_fmp_capsule_intent, digest_algorithm) == 40 &&
	offsetof(struct payload_mm_fmp_capsule_intent, digest) == 48 &&
	offsetof(struct payload_mm_fmp_capsule_intent, attempted_version) == 80,
	"payload_mm_fmp_capsule_intent layout changed");

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

/* Called once after payload_mm_authvar_authority_install() from trusted init. */
enum cb_err payload_mm_fmp_state_policy_install(
	const struct payload_mm_fmp_state_policy *trusted_policy,
	payload_mm_authvar_protected_storage storage_is_protected, void *context);

/*
 * Snapshot and validate one already-copied SMM message. current_state is either
 * NULL/zero for no combined value or one protected 20-byte combined value.
 * This validates policy only; it performs no variable or flash operation.
 */
enum cb_err payload_mm_fmp_state_command_prepare(const void *trusted_message,
	size_t trusted_message_size, const void *current_state,
	size_t current_state_size, struct payload_mm_fmp_state_command *command);

#endif
