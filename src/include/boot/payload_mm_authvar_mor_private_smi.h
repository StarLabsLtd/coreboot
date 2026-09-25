/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_H

#include <boot/payload_mm_authvar_mor_seal.h>
#include <bootmem_reservation_receipt.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE 4096U
#define PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CAPABILITY_SIZE 32U
#define PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_IDENTITY 0x4d4f5253UL

static inline uint64_t payload_mm_authvar_mor_private_smi_cookie(
	uint64_t identity, uint64_t page_base, uint64_t generation,
	uint32_t owner_cpu, uint32_t maximum_cpus)
{
	uint64_t cookie = identity ^ (page_base << 17 | page_base >> 47) ^
		(generation << 31 | generation >> 33) ^
		((uint64_t)owner_cpu << 32) ^ maximum_cpus ^ 0x43484e4cU;

	cookie &= UINTPTR_MAX;
	return cookie ? cookie : 1;
}

enum payload_mm_authvar_mor_private_smi_status {
	PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_SUCCESS = 0,
	PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REJECTED = 1,
	PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CLOSED = 2,
};

/* Private platform-to-loader input. Both secrets are consumed by provision. */
struct payload_mm_authvar_mor_private_smi_seed {
	uint32_t revision;
	uint32_t size;
	uint64_t cold_boot_generation;
	struct bootmem_aligned_reservation_handle page_handle;
	uint8_t capability[PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CAPABILITY_SIZE];
	uint8_t receipt_secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];
	uint32_t reserved[2];
} __aligned(8);

/* Protected loader-to-SMM state. This is never a coreboot-table record. */
struct payload_mm_authvar_mor_private_smi_slot {
	struct bootmem_reservation_receipt_authority verifier;
	uint64_t cold_boot_generation;
	uint64_t channel_identity;
	uint64_t descriptor_cookie;
	uint32_t owner_cpu;
	uint32_t maximum_cpus;
	uint8_t capability[PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CAPABILITY_SIZE];
	uint8_t state;
	uint8_t reserved[7];
} __aligned(8);

/* Exactly one firmware-reserved page; seal is at a fixed, checked offset. */
struct payload_mm_authvar_mor_private_smi_request {
	uint32_t revision;
	uint32_t size;
	uint64_t cold_boot_generation;
	uint64_t page_base;
	uint64_t page_size;
	uint64_t channel_identity;
	uint64_t descriptor_cookie;
	uint32_t owner_cpu;
	uint32_t maximum_cpus;
	uint8_t capability[PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CAPABILITY_SIZE];
	struct bootmem_reservation_receipt receipt;
	struct payload_mm_authvar_mor_seal_request seal;
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_authvar_mor_private_smi_seed) == 96,
	"MOR private SMI seed ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_private_smi_slot) == 136,
	"MOR private SMI slot ABI changed");
_Static_assert(sizeof(struct payload_mm_authvar_mor_private_smi_request) <=
	PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
	"MOR private SMI request no longer fits its page");

#if ENV_RAMSTAGE || ENV_TEST
bool platform_payload_mm_authvar_mor_private_smi_seed(
	struct payload_mm_authvar_mor_private_smi_seed *seed);
enum cb_err payload_mm_authvar_mor_private_smi_loader_provision(
	struct payload_mm_authvar_mor_private_smi_slot *protected_slot);
enum cb_err payload_mm_authvar_mor_private_smi_seal_channel_resolve(
	struct payload_mm_authvar_mor_seal_channel *channel);
enum cb_err payload_mm_authvar_mor_private_smi_send_install(
	const struct payload_mm_authvar_mor_grant *grant);
void payload_mm_authvar_mor_private_smi_close_unused(void);
#endif

#if ENV_SMM || ENV_TEST
/* Called by bootstrap after the fixed seal transport has been resolved. */
enum cb_err payload_mm_authvar_mor_private_smi_channel_install(
	struct payload_mm_authvar_mor_private_smi_slot *protected_slot,
	const struct payload_mm_authvar_mor_seal_channel *seal_channel,
	payload_mm_authvar_mor_seal_range_check protected_storage);

/* Pre-dispatch hook. True means this SMI was recognized and consumed. */
bool payload_mm_authvar_mor_private_smi_dispatch(unsigned int cpu);
void payload_mm_authvar_mor_private_smi_channel_abort(void);
#endif

#if ENV_TEST
struct payload_mm_authvar_mor_private_smi_descriptor {
	uint64_t identity;
	uint64_t page_base;
	uint64_t cookie;
};
typedef enum cb_err (*payload_mm_authvar_mor_private_smi_test_trigger)(
	const struct payload_mm_authvar_mor_private_smi_descriptor *descriptor,
	uint64_t *status, void *context);
void payload_mm_authvar_mor_private_smi_test_set_trigger(
	payload_mm_authvar_mor_private_smi_test_trigger trigger, void *context);
enum cb_err payload_mm_authvar_mor_private_smi_test_receive(
	uint64_t identity, uint64_t page_base, uint64_t cookie,
	unsigned int cpu, uint64_t *status);
void payload_mm_authvar_mor_private_smi_test_mutate_policy(void);
#endif

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_H */
