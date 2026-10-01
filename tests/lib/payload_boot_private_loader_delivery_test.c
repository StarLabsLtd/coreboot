/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Actual allocator, receipt, combined sender and protected-slot provision code.
 * Loader identity, topology, transaction seed and entropy are explicit models;
 * this does not establish hardware placement or resumed-CPU authority.
 */
#define main bootmem_fixture_main
#include "bootmem_aligned_reservation_test.c"
#undef main

#include <boot/payload_boot_private_buffer.h>
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <boot/payload_mm_authvar_presence_publication.h>

int dprintf(int descriptor, const char *format, ...);

#undef CHECK
#define CHECK(condition) do { \
	if (!(condition)) { \
		dprintf(2, "BOOT-private loader delivery oracle failure: %d\n", __LINE__); \
		abort(); \
	} \
} while (0)

static unsigned int aborts;
static uint64_t entropy_word = 1;
static bool entropy_failure;

bool mainboard_authvar_presence_cold_boot(void)
{
	return true;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *source,
	struct smm_invocation_loader_instance *snapshot)
{
	*snapshot = *source;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_topology_read(const struct smm_invocation_topology *source,
	struct smm_invocation_topology *snapshot)
{
	*snapshot = *source;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_producer_transaction_binding_take(
	uint32_t cpu, uint32_t maximum_cpus,
	struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_aligned_reservation_handle *mailbox)
{
	const struct bootmem_aligned_reservation_request request = {
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION, .size = sizeof(request),
		.bytes = 4096, .alignment = 4096, .limit_exclusive = 1ULL << 32,
		.tag = BM_MEM_RESERVED,
	};

	*binding = (struct payload_mm_authvar_presence_transaction_binding) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
		.size = sizeof(*binding), .generation = 42, .transaction_id = 43,
		.nonce = 44, .initiator_cpu = cpu, .maximum_cpus = maximum_cpus,
	};
	memset(binding->capability, 0x5a, sizeof(binding->capability));
	return bootmem_aligned_reservation_register(&request, mailbox) ? CB_ERR : CB_SUCCESS;
}

void payload_mm_authvar_presence_producer_abort(void)
{
	aborts++;
}

enum cb_err get_random_number_64(uint64_t *value)
{
	if (entropy_failure && entropy_word == 11)
		return CB_ERR;
	*value = entropy_word++;
	return CB_SUCCESS;
}

uint64_t smm_invocation_tuple_trigger(void)
{
	abort();
}

int main(int argc, char **argv)
{
	struct smm_invocation_loader_instance instance = {
		.state = SMM_INVOCATION_LOADER_INSTANCE_READY,
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION, .size = sizeof(instance),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.loader_instance_nonce = { .low = 0x123, .high = 0x456 },
	};
	struct smm_invocation_topology topology = {
		.state = SMM_INVOCATION_TOPOLOGY_READY,
		.revision = SMM_INVOCATION_TOPOLOGY_REVISION, .size = sizeof(topology),
		.active_cpus = 4, .initial_apic_ids = { 0, 1, 2, 3 },
	};
	struct payload_mm_authvar_presence_bootstrap slot;
	struct payload_mm_authvar_presence_bootstrap_receipts receipts;
	struct payload_mm_authvar_presence_tuple_sender sender;
	struct bootmem_reservation_receipt private_receipt;
	struct lb_payload_boot_private_buffer record;

	CHECK(argc == 2);
	CHECK(payload_mm_authvar_presence_tuple_sender_reserve() == CB_SUCCESS);
	if (!strcmp(argv[1], "s3") || !strcmp(argv[1], "entropy-abort")) {
		if (!strcmp(argv[1], "s3"))
			instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
		else
			entropy_failure = true;
		CHECK(payload_mm_authvar_presence_tuple_sender_loader_provision(&slot,
			&instance, &topology, true) == CB_ERR);
		CHECK(zero(&slot, sizeof(slot)) && aborts == 1);
		CHECK(payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts,
			&sender, &private_receipt) == CB_ERR);
		CHECK(zero(&receipts, sizeof(receipts)) && zero(&sender, sizeof(sender)) &&
			zero(&private_receipt, sizeof(private_receipt)));
		return 0;
	}
	CHECK(payload_mm_authvar_presence_tuple_sender_loader_provision(&slot,
		&instance, &topology, true) == CB_SUCCESS);
	initialize();
	memset(&receipts, 0xa5, sizeof(receipts));
	memset(&sender, 0xa5, sizeof(sender));
	memset(&private_receipt, 0xa5, sizeof(private_receipt));
	if (!strcmp(argv[1], "alias")) {
		CHECK(payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts,
			&sender, (void *)&receipts) == CB_ERR);
		CHECK(((uint8_t *)&receipts)[0] == 0xa5 && ((uint8_t *)&sender)[0] == 0xa5);
		CHECK(payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts,
			&sender, &private_receipt) == CB_ERR);
		CHECK(zero(&receipts, sizeof(receipts)) && zero(&sender, sizeof(sender)) &&
			zero(&private_receipt, sizeof(private_receipt)));
		return 0;
	}
	CHECK(payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts,
		&sender, &private_receipt) == CB_SUCCESS && !aborts);
	CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(&slot.mailbox_verifier,
		&receipts.mailbox, BM_MEM_RESERVED) == CB_SUCCESS);
	CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(&slot.page_verifier,
		&receipts.page, BM_MEM_RESERVED) == CB_SUCCESS);
	CHECK(bootmem_reservation_receipt_verify_consume(&slot.service_verifier,
		&receipts.service) == CB_SUCCESS);
#if CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER)
	const bool hostile_receipt = !strcmp(argv[1], "tampered") ||
		!strcmp(argv[1], "wrong-span") || !strcmp(argv[1], "wrong-revision");

	CHECK(private_receipt.bytes == 196608 && private_receipt.tag == BM_MEM_RESERVED &&
		private_receipt.generation == slot.binding.generation);
	if (!strcmp(argv[1], "tampered"))
		private_receipt.mac[0] ^= 1;
	if (!strcmp(argv[1], "wrong-span"))
		private_receipt.bytes += 4096;
	if (!strcmp(argv[1], "wrong-revision"))
		private_receipt.revision++;
	CHECK(payload_boot_private_buffer_consume(&slot.boot_private_verifier,
		&private_receipt, &record) ==
		(hostile_receipt ? CB_ERR : CB_SUCCESS));
	CHECK(authority_terminal_and_scrubbed(&slot.boot_private_verifier));
	CHECK(zero(&private_receipt, sizeof(private_receipt)));
	if (!hostile_receipt)
		CHECK(record.bytes == 196608 && record.slot_count == 3 && record.slot_size == 65536);
	else
		CHECK(zero(&record, sizeof(record)));
#else
	(void)record;
	CHECK(zero(&private_receipt, sizeof(private_receipt)));
#endif
	CHECK(payload_mm_authvar_presence_tuple_sender_receipts_take(&receipts,
		&sender, &private_receipt) == CB_ERR);
	CHECK(zero(&receipts, sizeof(receipts)) && zero(&sender, sizeof(sender)) &&
		zero(&private_receipt, sizeof(private_receipt)));
	return 0;
}
