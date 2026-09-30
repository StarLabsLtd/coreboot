/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

/* Real loader provision code; these external owners are independent boundaries. */
static unsigned int calls, fail_at, aborts;
static bool required = true;

static bool accepted(void)
{
	return ++calls != fail_at;
}

int bootmem_aligned_reservation_register(
	const struct bootmem_aligned_reservation_request *request,
	struct bootmem_aligned_reservation_handle *handle)
{
	assert(request->bytes == 4096 && request->alignment == 4096 &&
		request->limit_exclusive == 1ULL << 32 && request->tag == BM_MEM_RESERVED);
	if (!accepted())
		return -1;
	*handle = (struct bootmem_aligned_reservation_handle) { .opaque = { 1, 2 } };
	return 0;
}

enum cb_err payload_mm_authvar_presence_publication_loader_required(bool *value)
{
	*value = required;
	return accepted() ? CB_SUCCESS : CB_ERR;
}

bool mainboard_authvar_presence_cold_boot(void)
{
	return accepted();
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_loader_instance *snapshot)
{
	*snapshot = *instance;
	return accepted() ? CB_SUCCESS : CB_ERR;
}

enum cb_err smm_invocation_topology_read(const struct smm_invocation_topology *topology,
	struct smm_invocation_topology *snapshot)
{
	*snapshot = *topology;
	return accepted() ? CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_authvar_presence_producer_transaction_binding_take(
	uint32_t cpu, uint32_t maximum_cpus,
	struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_aligned_reservation_handle *mailbox)
{
	*binding = (struct payload_mm_authvar_presence_transaction_binding) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
		.size = sizeof(*binding), .generation = 42, .transaction_id = 43,
		.nonce = 44, .initiator_cpu = cpu, .maximum_cpus = maximum_cpus,
	};
	memset(binding->capability, 0x5a, sizeof(binding->capability));
	*mailbox = (struct bootmem_aligned_reservation_handle) { .opaque = { 3, 4 } };
	return accepted() ? CB_SUCCESS : CB_ERR;
}

void payload_mm_authvar_presence_producer_abort(void)
{
	aborts++;
}

enum cb_err get_random_number_64(uint64_t *value)
{
	*value = calls + 1U;
	return accepted() ? CB_SUCCESS : CB_ERR;
}

enum cb_err bootmem_reservation_receipt_provision(
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt_authority *verifier,
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE],
	uint32_t boot_kind, uint64_t generation,
	const struct bootmem_aligned_reservation_handle *handle)
{
	assert(boot_kind == BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT && generation == 42);
	assert(handle->opaque[0] && handle->opaque[1]);
	memset(signer, 0xa5, sizeof(*signer));
	*verifier = *signer;
	memset(secret, 0, BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE);
	return accepted() ? CB_SUCCESS : CB_ERR;
}

void bootmem_reservation_receipt_close(struct bootmem_reservation_receipt_authority *owner)
{
	memset(owner, 0, sizeof(*owner));
}

enum cb_err bootmem_aligned_reservation_receipt_emit_exact_tag(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_reservation_receipt_authority *signer,
	struct bootmem_reservation_receipt *receipt, enum bootmem_type tag)
{
	assert(tag == BM_MEM_RESERVED && handle->opaque[0]);
	*receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(*receipt), .generation = 42,
		.base = handle->opaque[0] == 3 ? 0x1000 : 0x2000,
		.bytes = 4096, .tag = BM_MEM_RESERVED,
	};
	bootmem_reservation_receipt_close(signer);
	return accepted() ? CB_SUCCESS : CB_ERR;
}

uint64_t smm_invocation_tuple_trigger(void)
{
	abort();
}

static bool empty(const void *object, size_t size)
{
	const uint8_t *bytes = object;

	for (size_t index = 0; index < size; index++)
		if (bytes[index])
			return false;
	return true;
}

int main(int argc, char **argv)
{
	struct smm_invocation_loader_instance instance = {
		.state = SMM_INVOCATION_LOADER_INSTANCE_READY,
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION,
		.size = sizeof(instance),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.loader_instance_nonce = { .low = 0x123, .high = 0x456 },
	};
	struct smm_invocation_topology topology = {
		.state = SMM_INVOCATION_TOPOLOGY_READY,
		.revision = SMM_INVOCATION_TOPOLOGY_REVISION,
		.size = sizeof(topology), .active_cpus = 4,
		.initial_apic_ids = { 0, 1, 2, 3 },
	};
	struct payload_mm_authvar_presence_bootstrap slot;
	struct payload_mm_authvar_presence_bootstrap_receipts receipts;
	struct payload_mm_authvar_presence_tuple_sender sender;
	enum cb_err result;

	assert(argc == 2);
	if (!strcmp(argv[1], "disabled")) {
		required = false;
		memset(&slot, 0xa5, sizeof(slot));
		assert(payload_mm_authvar_presence_tuple_sender_loader_provision(
			&slot, NULL, NULL) == CB_SUCCESS);
		assert(empty(&slot, sizeof(slot)) && !aborts && calls == 1);
		return 0;
	}
	for (const char *digit = argv[1]; *digit; digit++) {
		assert(*digit >= '0' && *digit <= '9');
		fail_at = fail_at * 10U + (unsigned int)(*digit - '0');
	}
	if (payload_mm_authvar_presence_tuple_sender_reserve() != CB_SUCCESS) {
		assert(fail_at == 1);
		return 0;
	}
	memset(&slot, 0xa5, sizeof(slot));
	result = payload_mm_authvar_presence_tuple_sender_loader_provision(
		&slot, &instance, &topology);
	if (fail_at && fail_at <= 19) {
		assert(result == CB_ERR && empty(&slot, sizeof(slot)) && aborts == 1);
		return 0;
	}
	assert(result == CB_SUCCESS && calls == 19 && !aborts);
	assert(slot.state == PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_PROVISIONED &&
		slot.cold_boot_proven == 1 && slot.binding.maximum_cpus == 4);
	assert(smm_invocation_loader_instance_nonce_equal(slot.loader_nonce,
		instance.loader_instance_nonce));
	memset(&receipts, 0xa5, sizeof(receipts));
	memset(&sender, 0xa5, sizeof(sender));
	result = payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts, &sender);
	if (fail_at) {
		assert(result == CB_ERR && aborts == 1 && empty(&receipts, sizeof(receipts)) &&
			empty(&sender, sizeof(sender)));
		return 0;
	}
	assert(result == CB_SUCCESS && calls == 21 && sender.page == (void *)0x2000);
	assert(receipts.mailbox.base == 0x1000 && receipts.page.base == 0x2000);
	assert(payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts, &sender) == CB_ERR);
	assert(empty(&receipts, sizeof(receipts)) && empty(&sender, sizeof(sender)) && aborts == 1);
	assert(payload_mm_authvar_presence_tuple_sender_loader_provision(
		&slot, &instance, &topology) == CB_ERR);
	assert(empty(&slot, sizeof(slot)) && aborts == 2);
	return 0;
}
