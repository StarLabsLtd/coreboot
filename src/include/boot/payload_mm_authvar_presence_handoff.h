/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_H

#include <boot/payload_mm_authvar_presence_producer.h>
#include <bootmem_reservation_receipt.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CAPABILITY_SIZE 32U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE 4096U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_ALIGNMENT 4096U

enum payload_mm_authvar_presence_handoff_status {
	PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_SUCCESS,
	PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_REJECTED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CLOSED,
};

struct payload_mm_authvar_presence_handoff_loader_seed {
	uint32_t revision;
	uint32_t size;
	uint64_t channel_generation;
	struct bootmem_aligned_reservation_handle mailbox_handle;
	uint8_t channel_capability[
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CAPABILITY_SIZE];
	uint8_t mailbox_receipt_secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];
	uint8_t transfer_receipt_secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];
	uint32_t owner_cpu;
	uint32_t maximum_cpus;
	uint32_t reserved[2];
} __aligned(8);

struct payload_mm_authvar_presence_handoff_slot {
	struct bootmem_reservation_receipt_authority mailbox_verifier;
	struct bootmem_reservation_receipt_authority transfer_verifier;
	uint64_t channel_generation;
	uint64_t channel_identity;
	uint32_t owner_cpu;
	uint32_t maximum_cpus;
	uint8_t channel_capability[
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CAPABILITY_SIZE];
	uint8_t state;
	uint8_t reserved[7];
} __aligned(8);

struct payload_mm_authvar_presence_handoff_request {
	uint32_t revision;
	uint32_t size;
	uint64_t channel_generation;
	uint64_t mailbox_base;
	uint64_t mailbox_size;
	uint64_t transfer_base;
	uint64_t transfer_size;
	uint64_t channel_identity;
	uint64_t descriptor_cookie;
	uint32_t owner_cpu;
	uint32_t maximum_cpus;
	uint8_t channel_capability[
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CAPABILITY_SIZE];
	struct bootmem_reservation_receipt mailbox_receipt;
	struct bootmem_reservation_receipt transfer_receipt;
	struct payload_mm_authvar_presence_seed seed;
} __aligned(8);

struct payload_mm_authvar_presence_handoff_descriptor {
	uint64_t channel_identity;
	uint64_t request_base;
	uint64_t descriptor_cookie;
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_presence_handoff_request) <=
	PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_TRANSFER_SIZE,
	"presence handoff request exceeds its private area");

static inline uint64_t payload_mm_authvar_presence_handoff_identity(
	const uint8_t capability[
		PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CAPABILITY_SIZE],
	uint64_t generation)
{
	uint64_t identity = generation ^ 0x50524553454e4345ULL;

	for (size_t index = 0;
	     index < PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_CAPABILITY_SIZE;
	     index++) {
		identity = (identity << 9) | (identity >> 55);
		identity ^= capability[index];
	}
	return identity ? identity : 1U;
}

static inline uint64_t payload_mm_authvar_presence_handoff_cookie(
	uint64_t identity, uint64_t request_base, uint64_t generation,
	uint32_t owner_cpu, uint32_t maximum_cpus)
{
	uint64_t cookie = identity ^
		((request_base << 19) | (request_base >> 45)) ^
		((generation << 37) | (generation >> 27)) ^
		((uint64_t)owner_cpu << 32) ^ maximum_cpus ^ 0x48414e44U;

	return cookie ? cookie : 1U;
}

#if ENV_RAMSTAGE || ENV_TEST
/* Trusted facts; weak defaults fail closed. */
bool platform_payload_mm_authvar_presence_handoff_cold_boot(void);
enum cb_err platform_payload_mm_authvar_presence_handoff_self_smi(
	const struct payload_mm_authvar_presence_handoff_descriptor *descriptor,
	uint64_t *status);

enum cb_err payload_mm_authvar_presence_handoff_reserve(void);
enum cb_err payload_mm_authvar_presence_handoff_loader_provision(
	struct payload_mm_authvar_presence_handoff_slot *protected_slot,
	struct payload_mm_authvar_presence_handoff_loader_seed *seed);
enum cb_err payload_mm_authvar_presence_handoff_install(void *context,
	const struct payload_mm_authvar_presence_seed *seed);
void payload_mm_authvar_presence_handoff_abort(void);
#endif

#if ENV_SMM || ENV_TEST
struct payload_mm_authvar_presence_handoff_slot *
	smm_get_payload_mm_authvar_presence_handoff_slot(void);
enum cb_err platform_payload_mm_authvar_presence_handoff_receiver_install(
	const struct payload_mm_authvar_presence_seed *seed);
/* Protected DRAM/non-MMIO proof for receiver inputs and the transfer page. */
bool platform_payload_mm_authvar_presence_handoff_range_valid(
	uint64_t base, uint64_t size);
enum cb_err payload_mm_authvar_presence_handoff_receive(
	struct payload_mm_authvar_presence_handoff_slot *protected_slot,
	const struct payload_mm_authvar_presence_handoff_descriptor *descriptor,
	unsigned int cpu, uint64_t *status);
void payload_mm_authvar_presence_handoff_receiver_abort(void);
#endif

#if ENV_TEST
void payload_mm_authvar_presence_handoff_sender_reset_test(void);
void payload_mm_authvar_presence_handoff_receiver_reset_test(void);
bool payload_mm_authvar_presence_handoff_sender_scrubbed_test(void);
bool payload_mm_authvar_presence_handoff_slot_terminal_test(
	const struct payload_mm_authvar_presence_handoff_slot *slot);
#endif

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF_H */
