/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_bootstrap.h>
#include <boot/payload_mm_authvar_presence_backing.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_service_receiver.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>
#include "../../src/lib/bootmem_reservation_receipt_internal.h"
#include <stdlib.h>
#include <string.h>

extern void exit(int status) __noreturn;
extern unsigned long strtoul(const char *text, char **end, int base);
static struct payload_mm_authvar_presence_bootstrap slot;
static struct payload_mm_authvar_presence_bootstrap_receipts receipts;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_topology topology;
static unsigned int scenario, identity_reads;
static unsigned int prepared_owners;
static unsigned int prepare_aborts;

static void require(bool condition)
{
	if (!condition)
		exit(90);
}

/* The actual sole provider is reviewed separately; this boundary consumes the
 * real typed receipt/MAC and checks the actual canonical getter on both sides. */
enum cb_err payload_mm_authvar_service_prepare(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt)
{
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	struct payload_mm_authvar_presence_transaction_binding before;

	require(verifier == &slot.service_verifier && !prepared_owners);
	if (payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS)
		return CB_ERR;
	before = *binding;
	if (scenario == 25 ||
	    bootmem_reservation_receipt_verify_consume_exact_tag(verifier, receipt,
		BM_MEM_TABLE) != CB_SUCCESS)
		return CB_ERR;
	if (scenario == 26)
		instance.loader_instance_nonce.high++;
	if (payload_mm_authvar_presence_bootstrap_binding_get(&binding) != CB_SUCCESS ||
	    memcmp(&before, binding, sizeof(before)))
		return CB_ERR;
	prepared_owners++;
	if (scenario == 27)
		instance.loader_instance_nonce.high++;
	if (scenario == 28)
		slot.state = PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_FAILED;
	return CB_SUCCESS;
}

void payload_mm_authvar_service_prepare_abort(void)
{
	prepare_aborts++;
	prepared_owners = 0;
}

struct payload_mm_authvar_presence_bootstrap *smm_get_payload_mm_authvar_presence_bootstrap(void)
{
	return &slot;
}

enum cb_err smm_invocation_runtime_view_get(const struct smm_invocation_runtime_view **view)
{
	*view = (const void *)&slot;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	require(view == (const void *)&slot && base && size);
	return scenario == 15 && base == &receipts ? CB_ERR : CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_binding_get(struct smm_invocation_runtime_binding *binding)
{
	*binding = (struct smm_invocation_runtime_binding) {
		.instance = &instance, .topology = &topology,
	};
	return CB_SUCCESS;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *source,
	struct smm_invocation_loader_instance *snapshot)
{
	require(source == &instance);
	identity_reads++;
	*snapshot = instance;
	if (scenario == 16 && identity_reads == 2)
		snapshot->loader_instance_nonce.high++;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_topology_read(const struct smm_invocation_topology *source,
	struct smm_invocation_topology *snapshot)
{
	require(source == &topology);
	*snapshot = topology;
	return CB_SUCCESS;
}

static void receipt_make(struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt, uint64_t base, uint32_t handle_index)
{
	struct bootmem_reservation_receipt_authority signer = {0};
	uint8_t secret[32], key[32];
	const struct bootmem_aligned_reservation_handle handle = {
		.opaque = {handle_index, 0x42524d51},
	};
	memset(key, (int)handle_index, sizeof(key));
	memcpy(secret, key, sizeof(secret));
	require(bootmem_reservation_receipt_provision(&signer, verifier, secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, slot.binding.generation,
		&handle) == CB_SUCCESS);
	*receipt = (struct bootmem_reservation_receipt) {
		.revision = 1, .size = sizeof(*receipt), .boot_kind = 1,
		.generation = slot.binding.generation, .sequence = 1, .handle = handle,
		.base = base, .bytes = 4096, .tag = BM_MEM_RESERVED, .use = 1,
	};
	if (handle_index == 5) {
		receipt->tag = BM_MEM_TABLE;
		receipt->bytes = PAYLOAD_MM_AUTHVAR_SERVICE_MAX_MESSAGE_SIZE;
	}
	require(bootmem_reservation_receipt_mac(key, receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt->mac) == CB_SUCCESS);
	bootmem_reservation_receipt_close(&signer);
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_presence_bootstrap before;
	struct payload_mm_authvar_presence_bootstrap_receipts original, zero = {0};
	const struct payload_mm_authvar_presence_transaction_binding *binding = NULL;
	struct payload_mm_authvar_presence_backing backing;
	enum cb_err result;
	require(argc == 2);
	scenario = (unsigned int)strtoul(argv[1], NULL, 10);
	slot.state = PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_PROVISIONED;
	slot.cold_boot_proven = 1;
	slot.binding = (struct payload_mm_authvar_presence_transaction_binding) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
		.size = sizeof(slot.binding), .generation = 5, .transaction_id = 9,
		.nonce = 17, .maximum_cpus = 4, .capability = {1, 2, 3},
	};
	instance.lifecycle = slot.loader_lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	instance.loader_instance_nonce = slot.loader_nonce =
		(struct smm_invocation_loader_instance_nonce) {.low = 1, .high = 2};
	topology.active_cpus = 4;
	receipt_make(&slot.mailbox_verifier, &receipts.mailbox, 0x100000, 3);
	receipt_make(&slot.page_verifier, &receipts.page, 0x200000, 4);
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	receipt_make(&slot.service_verifier, &receipts.service, 0x300000, 5);
#endif
	original = receipts;
	backing = (struct payload_mm_authvar_presence_backing) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION,
		.size = sizeof(backing), .base = receipts.mailbox.base, .bytes = 4096,
		.generation = 5, .tag = BM_MEM_RESERVED,
	};
	switch (scenario) {
	case 1: receipts.mailbox.mac[0] ^= 1; break;
	case 2: receipts.mailbox.tag = BM_MEM_TABLE; break;
	case 3: receipts.mailbox.generation++; break;
	case 4: receipts.page.generation++; break;
	case 5: receipts.page.handle.opaque[0]++; break;
	case 6: receipts.page.base = receipts.mailbox.base; break;
	case 7: receipts.mailbox.bytes--; break;
	case 8: receipts.page.base++; break;
	case 9: receipts.page.base = 1ULL << 32; break;
	case 10: slot.cold_boot_proven = 0; break;
	case 11: instance.loader_instance_nonce.high++; break;
	case 12: topology.active_cpus--; break;
	case 13: slot.state = PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_EMPTY; break;
	case 17: receipts.service.tag = BM_MEM_RESERVED; break;
	case 18: receipts.service.generation++; break;
	case 19: receipts.service.handle.opaque[0]++; break;
	case 20: receipts.service.bytes--; break;
	case 21: receipts.service.base = receipts.mailbox.base - 4096; break;
	case 22: receipts.service.base = receipts.page.base - 4096; break;
	case 23: receipts.service.base = UINT32_MAX - 4095U; break;
	case 24: receipts.service.mac[0] ^= 1; break;
	case 14:
		before = slot;
		require(payload_mm_authvar_presence_bootstrap_receipts_import((void *)&slot) == CB_ERR);
		require(!memcmp(&before, &slot, sizeof(slot)));
		return 0;
	}
	before = slot;
	result = payload_mm_authvar_presence_bootstrap_receipts_import(&receipts);
	if (scenario) {
		require(result == CB_ERR);
		require(!prepared_owners);
		if ((scenario >= 10 && scenario <= 13) || scenario == 15)
			require(!memcmp(&before, &slot, sizeof(slot)));
		else {
			require(slot.state == PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_FAILED);
			require(!memcmp(&receipts, &zero, sizeof(zero)));
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
			require(prepare_aborts == 1);
#endif
		}
		require(payload_mm_authvar_presence_backing_evidence_take(&backing) == CB_ERR);
		return 0;
	}
	require(result == CB_SUCCESS && slot.state == PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_READY);
	require(!memcmp(&receipts, &zero, sizeof(zero)));
	require(!memcmp(&slot.page_receipt, &original.page, sizeof(original.page)));
	require(payload_mm_authvar_presence_bootstrap_binding_get(&binding) == CB_SUCCESS);
	require(binding == &slot.binding);
	require(payload_mm_authvar_presence_backing_evidence_take(&backing) == CB_SUCCESS);
	require(payload_mm_authvar_presence_backing_evidence_take(&backing) == CB_ERR);
	require(bootmem_reservation_receipt_verify_consume_exact_tag(&slot.page_verifier,
		&slot.page_receipt, BM_MEM_RESERVED) == CB_SUCCESS);
#if CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
	require(prepared_owners == 1);
	require(bootmem_reservation_receipt_verify_consume_exact_tag(&slot.service_verifier,
		&original.service, BM_MEM_TABLE) == CB_ERR);
#else
	require(!prepared_owners);
#endif
	receipts = original;
	require(payload_mm_authvar_presence_bootstrap_receipts_import(&receipts) == CB_ERR);
	return 0;
}
