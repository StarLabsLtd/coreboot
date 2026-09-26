/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_producer.h>
#include <bootmem.h>
#include <bootmem_reservation_receipt.h>
#include <device/device.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static volatile int failure_line;

#define CHECK(condition) do { \
	if (!(condition)) { \
		failure_line = __LINE__; \
		__builtin_trap(); \
	} \
} while (0)
#define TEST_SYMBOL(symbol, value) \
	asm(".set " #symbol ", " #value "\n\t.globl " #symbol)

TEST_SYMBOL(_program, 0x100000);
TEST_SYMBOL(_eprogram, 0x110000);
TEST_SYMBOL(_program_size, 0x10000);
TEST_SYMBOL(_stack, 0x110000);
TEST_SYMBOL(_estack, 0x120000);
TEST_SYMBOL(_stack_size, 0x10000);

static struct resource resources[] = {
	{
		.base = 0x100000,
		.size = 0x2000000,
		.flags = IORESOURCE_MEM | IORESOURCE_CACHEABLE | IORESOURCE_ASSIGNED,
	},
};

static struct device domain = {
	.enabled = 1,
	.path.type = DEVICE_PATH_DOMAIN,
	.resource_list = resources,
};

struct device *all_devices = &domain;
struct resource *free_resources;

void cbmem_add_bootmem(void)
{
	/* Deliberately differ from the OS view with a final firmware-only hole. */
	bootmem_add_range(0x1c00000, 0x200000, BM_MEM_RAMSTAGE);
}

void bootmem_arch_add_ranges(void)
{
}

void efi_add_capsules_to_bootmem(void)
{
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	CHECK(result);
}

int printk(int message_level, const char *format, ...)
{
	(void)message_level;
	(void)format;
	return 0;
}

int vprintk(int message_level, const char *format, va_list arguments)
{
	(void)message_level;
	(void)format;
	(void)arguments;
	return 0;
}

int console_log_level(int message_level)
{
	(void)message_level;
	return 0;
}

void die(const char *format, ...)
{
	(void)format;
	__builtin_trap();
}

static struct bootmem_aligned_reservation_request request(uint64_t bytes,
	uint64_t alignment, uint64_t limit, enum bootmem_type tag)
{
	return (struct bootmem_aligned_reservation_request) {
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(struct bootmem_aligned_reservation_request),
		.bytes = bytes,
		.alignment = alignment,
		.limit_exclusive = limit,
		.tag = tag,
	};
}

static struct {
	struct lb_memory header;
	struct lb_memory_range ranges[32];
} emitted_table;

static void initialize(void)
{
	memset(&emitted_table, 0, sizeof(emitted_table));
	emitted_table.header.size = sizeof(emitted_table.header);
	bootmem_write_memory_table(&emitted_table.header);
}

static bool os_range_exact(uint64_t base, uint64_t size, uint32_t type)
{
	const size_t count = (emitted_table.header.size -
		sizeof(emitted_table.header)) / sizeof(emitted_table.ranges[0]);

	for (size_t index = 0; index < count; index++)
		if (emitted_table.ranges[index].start == base &&
		    emitted_table.ranges[index].size == size &&
		    emitted_table.ranges[index].type == type)
			return true;
	return false;
}

static void invalid_requests(void)
{
	struct bootmem_aligned_reservation_handle handle = { .opaque = { 1, 1 } };
	struct bootmem_aligned_reservation_request value = request(0x5000, 0x1000,
		1ULL << 32, BM_MEM_RESERVED);

	value.bytes = 0;
	CHECK(bootmem_aligned_reservation_register(&value, &handle));
	CHECK(!handle.opaque[0] && !handle.opaque[1]);
	value = request(0x5001, 0x1000, 1ULL << 32, BM_MEM_RESERVED);
	CHECK(bootmem_aligned_reservation_register(&value, &handle));
	value = request(0x5000, 0x1800, 1ULL << 32, BM_MEM_RESERVED);
	CHECK(bootmem_aligned_reservation_register(&value, &handle));
	value = request(0x5000, 0x1000, 0, BM_MEM_RESERVED);
	CHECK(bootmem_aligned_reservation_register(&value, &handle));
	value = request(0x5000, 0x1000, (1ULL << 32) + 1U, BM_MEM_RESERVED);
	CHECK(bootmem_aligned_reservation_register(&value, &handle));
	value = request(UINT64_MAX & ~0xfffULL, 0x1000, 1ULL << 32,
		BM_MEM_RESERVED);
	CHECK(bootmem_aligned_reservation_register(&value, &handle));
	value = request(0x5000, 0x1000, 0x4000, BM_MEM_RESERVED);
	CHECK(bootmem_aligned_reservation_register(&value, &handle));
	value = request(0x5000, 0x1000, 1ULL << 32, BM_MEM_RAM);
	CHECK(bootmem_aligned_reservation_register(&value, &handle));
	value = request(0x5000, 0x1000, 1ULL << 32, BM_MEM_RESERVED);
	value.reserved = 1;
	CHECK(bootmem_aligned_reservation_register(&value, &handle));
}

static void success(void)
{
	struct bootmem_aligned_reservation_request tables = request(0x5000,
		0x1000, 0x2000000, BM_MEM_TABLE);
	struct bootmem_aligned_reservation_request aperture = request(0x200000,
		0x200000, 0x2000000, BM_MEM_RESERVED);
	struct bootmem_aligned_reservation_request aligned_page = request(0x1000,
		0x200000, 0x2000000, BM_MEM_TABLE);
	struct bootmem_aligned_reservation_handle table_handle;
	struct bootmem_aligned_reservation_handle aperture_handle;
	struct bootmem_aligned_reservation_handle aligned_page_handle;
	struct bootmem_aligned_reservation_handle forged;
	struct bootmem_aligned_reservation result = { .base = 1 };

	invalid_requests();
	CHECK(!bootmem_aligned_reservation_register(&tables, &table_handle));
	CHECK(bootmem_aligned_reservation_register(&tables, &forged));
	CHECK(!bootmem_aligned_reservation_register(&aperture, &aperture_handle));
	CHECK(!bootmem_aligned_reservation_register(&aligned_page,
		&aligned_page_handle));
	/* Registration copied the request rather than retaining caller storage. */
	tables.bytes = 0x9000;
	aperture.limit_exclusive = 0x400000;
	CHECK(bootmem_aligned_reservation_query(&table_handle, &result));
	CHECK(!result.base && !result.size && !result.tag && !result.reserved);
	initialize();
	CHECK(!bootmem_aligned_reservation_query(&table_handle, &result));
	CHECK(result.size == 0x5000 && result.tag == BM_MEM_TABLE &&
		result.base == 0x1ffb000);
	CHECK(bootmem_region_targets_type(result.base, result.size, BM_MEM_TABLE));
	const uint64_t table_base = result.base;
	CHECK(os_range_exact(result.base, result.size, LB_MEM_TABLE));
	CHECK(os_range_exact(0x1e01000, result.base - 0x1e01000, LB_MEM_RAM));
	CHECK(os_range_exact(result.base + result.size, 0x100000, LB_MEM_RAM));
	CHECK(!bootmem_aligned_reservation_query(&aperture_handle, &result));
	CHECK(result.size == 0x200000 && result.tag == BM_MEM_RESERVED &&
		result.base == 0x1a00000);
	CHECK(bootmem_region_targets_type(result.base, result.size,
		BM_MEM_RESERVED));
	CHECK(os_range_exact(result.base, result.size, LB_MEM_RESERVED));
	CHECK(os_range_exact(0x100000, result.base - 0x100000, LB_MEM_RAM));
	CHECK(os_range_exact(result.base + result.size, 0x200000, LB_MEM_RAM));
	CHECK(result.base + result.size <= table_base);
	/* The RAMSTAGE-only hole differs between maps but cannot redirect allocation. */
	CHECK(result.base != 0x1c00000);
	CHECK(bootmem_region_targets_type(result.base - 0x1000, 0x1000,
		BM_MEM_RAM));
	CHECK(bootmem_region_targets_type(table_base - 0x1000, 0x1000,
		BM_MEM_RAM));
	CHECK(!bootmem_aligned_reservation_query(&aligned_page_handle, &result));
	CHECK(result.base == 0x1e00000 && result.size == 0x1000 &&
		result.tag == BM_MEM_TABLE);
	CHECK(os_range_exact(result.base, result.size, LB_MEM_TABLE));
	CHECK(os_range_exact(result.base - 0x200000, 0x200000, LB_MEM_RAM));
	CHECK(os_range_exact(result.base + result.size,
		table_base - result.base - result.size, LB_MEM_RAM));
	CHECK(bootmem_region_targets_type(result.base + result.size, 0x1000,
		BM_MEM_RAM));
	forged = aperture_handle;
	forged.opaque[1] ^= 1U;
	CHECK(bootmem_aligned_reservation_query(&forged, &result));
	CHECK(!result.base && !result.size && !result.tag && !result.reserved);
	CHECK(bootmem_aligned_reservation_register(&tables, &forged));
}

static void presence_producer_contract(void)
{
	struct bootmem_aligned_reservation_request value = request(
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE,
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT, 1ULL << 32,
		BM_MEM_RESERVED);
	struct bootmem_aligned_reservation_handle handle;
	struct bootmem_aligned_reservation result;

	CHECK(!bootmem_aligned_reservation_register(&value, &handle));
	initialize();
	CHECK(!bootmem_aligned_reservation_query(&handle, &result));
	CHECK(result.base && result.size == PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE &&
		result.tag == BM_MEM_RESERVED && !result.reserved &&
		!(result.base % PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT) &&
		result.base + PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE <=
		result.base + result.size);
}

static void bounded(void)
{
	struct bootmem_aligned_reservation_handle handles[
		BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS];
	struct bootmem_aligned_reservation result;

	for (size_t index = 0;
	     index < BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS; index++) {
		struct bootmem_aligned_reservation_request value = request(
			0x1000U * (index + 1U), 0x1000, 0x2100000,
			BM_MEM_RESERVED);

		CHECK(!bootmem_aligned_reservation_register(&value, &handles[index]));
	}
	struct bootmem_aligned_reservation_request excess = request(0x9000,
		0x1000, 0x2100000, BM_MEM_RESERVED);
	struct bootmem_aligned_reservation_handle excess_handle = {
		.opaque = { 1, 1 },
	};
	CHECK(bootmem_aligned_reservation_register(&excess, &excess_handle));
	CHECK(!excess_handle.opaque[0] && !excess_handle.opaque[1]);
	initialize();
	for (size_t index = 0;
	     index < BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS; index++) {
		CHECK(!bootmem_aligned_reservation_query(&handles[index], &result));
		CHECK(result.size == 0x1000U * (index + 1U));
	}
}

static void atomic_capacity(void)
{
	struct bootmem_aligned_reservation_handle singles[
		BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS];
	struct bootmem_aligned_reservation_request batch[2] = {
		request(0x9000, 0x1000, 0x2100000, BM_MEM_RESERVED),
		request(0xa000, 0x1000, 0x2100000, BM_MEM_TABLE),
	};
	struct bootmem_aligned_reservation_handle batch_handles[2] = {
		{ .opaque = { 1, 1 } }, { .opaque = { 2, 2 } },
	};
	struct bootmem_aligned_reservation result;

	for (size_t index = 0;
	     index < BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS - 1U; index++) {
		struct bootmem_aligned_reservation_request value = request(
			0x1000U * (index + 1U), 0x1000, 0x2100000,
			BM_MEM_RESERVED);

		CHECK(!bootmem_aligned_reservation_register(&value, &singles[index]));
	}
	CHECK(bootmem_aligned_reservations_register(batch, 2, batch_handles));
	CHECK(!batch_handles[0].opaque[0] && !batch_handles[0].opaque[1] &&
		!batch_handles[1].opaque[0] && !batch_handles[1].opaque[1]);
	CHECK(!bootmem_aligned_reservation_register(&batch[0],
		&singles[BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS - 1U]));
	batch_handles[0].opaque[0] = 1;
	batch_handles[1].opaque[0] = 2;
	CHECK(bootmem_aligned_reservations_register(&batch[1], 1,
		batch_handles));
	CHECK(!batch_handles[0].opaque[0] && !batch_handles[0].opaque[1]);
	initialize();
	for (size_t index = 0;
	     index < BOOTMEM_ALIGNED_RESERVATION_MAX_REQUESTS; index++)
		CHECK(!bootmem_aligned_reservation_query(&singles[index], &result));
}

static void capacity_failure(void)
{
	struct bootmem_aligned_reservation_request first = request(0x200000,
		0x200000, 0x2100000, BM_MEM_RESERVED);
	struct bootmem_aligned_reservation_request impossible = request(0x2000000,
		0x1000, 0x2100000, BM_MEM_TABLE);
	struct bootmem_aligned_reservation_handle first_handle;
	struct bootmem_aligned_reservation_handle impossible_handle;

	CHECK(!bootmem_aligned_reservation_register(&first, &first_handle));
	CHECK(!bootmem_aligned_reservation_register(&impossible,
		&impossible_handle));
	initialize();
	__builtin_trap();
}

#if CONFIG(BOOTMEM_ALIGNED_RESERVATION_RECEIPT)
void bootmem_receipt_test_outside_authority_spans(uintptr_t receipt_start,
	size_t receipt_size, uintptr_t signer_start, size_t signer_size,
	size_t spans[3]);
void bootmem_receipt_test_retag_map(bool os_map, uint64_t base, uint64_t size,
	enum bootmem_type tag);

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static bool authority_terminal_and_scrubbed(
	const struct bootmem_reservation_receipt_authority *authority)
{
	struct bootmem_reservation_receipt_authority copy = *authority;

	CHECK(copy.state == 4);
	copy.state = 0;
	return zero(&copy, sizeof(copy));
}

static void receipt_alias(bool handle_in_signer)
{
	struct bootmem_aligned_reservation_request value = request(0x1000,
		0x1000, 0x2000000, BM_MEM_TABLE);
	struct bootmem_aligned_reservation_handle handle;
	struct bootmem_reservation_receipt_authority signer = {0};
	struct bootmem_reservation_receipt_authority verifier = {0};
	struct bootmem_reservation_receipt receipt = {0};
	uint8_t key[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];

	memset(key, 0x5a, sizeof(key));
	CHECK(!bootmem_aligned_reservation_register(&value, &handle));
	initialize();
	CHECK(bootmem_reservation_receipt_provision(&signer, &verifier, key,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 1, &handle) == CB_SUCCESS);
	CHECK(zero(key, sizeof(key)));
	if (handle_in_signer) {
		CHECK(bootmem_aligned_reservation_receipt_emit(&signer.handle,
			&signer, &receipt) != CB_SUCCESS);
		CHECK(zero(&receipt, sizeof(receipt)));
	} else {
		receipt.handle = handle;
		CHECK(bootmem_aligned_reservation_receipt_emit(&receipt.handle,
			&signer, &receipt) != CB_SUCCESS);
		CHECK(zero(&receipt, sizeof(receipt)));
	}
	CHECK(authority_terminal_and_scrubbed(&signer));
	bootmem_reservation_receipt_close(&verifier);
	CHECK(authority_terminal_and_scrubbed(&verifier));
}

static void signer_receipt_alias(bool receipt_first)
{
	struct bootmem_aligned_reservation_request value = request(0x1000,
		0x1000, 0x2000000, BM_MEM_TABLE);
	struct bootmem_aligned_reservation_handle handle;
	uint8_t storage[128] __aligned(8);
	struct bootmem_reservation_receipt_authority *signer =
		(void *)(storage + (receipt_first ? 32 : 0));
	struct bootmem_reservation_receipt *receipt =
		(void *)(storage + (receipt_first ? 0 : 32));
	const size_t union_size = receipt_first ? sizeof(*receipt) :
		32 + sizeof(*receipt);
	struct bootmem_reservation_receipt_authority verifier = {0};
	uint8_t key[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];

	memset(storage, 0xa5, sizeof(storage));
	memset(signer, 0, sizeof(*signer));
	memset(key, 0x5a, sizeof(key));
	CHECK(!bootmem_aligned_reservation_register(&value, &handle));
	initialize();
	CHECK(bootmem_reservation_receipt_provision(signer, &verifier, key,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 1, &handle) == CB_SUCCESS);
	CHECK(bootmem_aligned_reservation_receipt_emit(&handle, signer,
		receipt) != CB_SUCCESS);
	for (size_t index = 0; index < union_size; index++) {
		const uint8_t expected = &storage[index] == &signer->state ? 4 : 0;

		CHECK(storage[index] == expected);
	}
	bootmem_reservation_receipt_close(&verifier);
	CHECK(authority_terminal_and_scrubbed(&verifier));
}

static void receipt_boundary_arithmetic(void)
{
	size_t spans[3];

	bootmem_receipt_test_outside_authority_spans(UINTPTR_MAX - 95U, 96,
		UINTPTR_MAX - 63U, 64, spans);
	CHECK(spans[0] == 32 && spans[1] == 0 && spans[2] == 0);
	bootmem_receipt_test_outside_authority_spans(UINTPTR_MAX - 95U, 96,
		UINTPTR_MAX - 95U, 64, spans);
	CHECK(spans[0] == 0 && spans[1] == 64 && spans[2] == 32);
	bootmem_receipt_test_outside_authority_spans(UINTPTR_MAX - 127U, 96,
		UINTPTR_MAX - 95U, 64, spans);
	CHECK(spans[0] == 32 && spans[1] == 0 && spans[2] == 0);
}

static void receipt_exact_tags(void)
{
	struct bootmem_aligned_reservation_request table_request = request(0x1000,
		0x1000, 0x2000000, BM_MEM_TABLE);
	struct bootmem_aligned_reservation_request reserved_request = request(0x1000,
		0x1000, 0x2000000, BM_MEM_RESERVED);
	struct bootmem_aligned_reservation_handle table_handle;
	struct bootmem_aligned_reservation_handle reserved_handle;
	struct bootmem_reservation_receipt_authority table_signer = { 0 };
	struct bootmem_reservation_receipt_authority table_verifier = { 0 };
	struct bootmem_reservation_receipt_authority exact_table_signer = { 0 };
	struct bootmem_reservation_receipt_authority exact_table_verifier = { 0 };
	struct bootmem_reservation_receipt_authority reserved_signer = { 0 };
	struct bootmem_reservation_receipt_authority reserved_verifier = { 0 };
	struct bootmem_reservation_receipt_authority wrong_signer = { 0 };
	struct bootmem_reservation_receipt_authority wrong_verifier = { 0 };
	struct bootmem_reservation_receipt table_receipt = { 0 };
	struct bootmem_reservation_receipt exact_table_receipt = { 0 };
	struct bootmem_reservation_receipt reserved_receipt = { 0 };
	struct bootmem_reservation_receipt wrong_receipt = { 0 };
	uint8_t key[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];

	CHECK(!bootmem_aligned_reservation_register(&table_request, &table_handle));
	CHECK(!bootmem_aligned_reservation_register(&reserved_request,
		&reserved_handle));
	initialize();

	memset(key, 0x11, sizeof(key));
	CHECK(bootmem_reservation_receipt_provision(&table_signer,
		&table_verifier, key, BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 1,
		&table_handle) == CB_SUCCESS);
	CHECK(bootmem_aligned_reservation_receipt_emit(&table_handle,
		&table_signer, &table_receipt) == CB_SUCCESS);
	CHECK(table_receipt.tag == BM_MEM_TABLE);
	memset(key, 0x11, sizeof(key));
	CHECK(bootmem_reservation_receipt_provision(&exact_table_signer,
		&exact_table_verifier, key, BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 1,
		&table_handle) == CB_SUCCESS);
	CHECK(bootmem_aligned_reservation_receipt_emit_exact_tag(&table_handle,
		&exact_table_signer, &exact_table_receipt, BM_MEM_TABLE) == CB_SUCCESS);
	CHECK(!memcmp(&table_receipt, &exact_table_receipt,
		sizeof(table_receipt)));
	CHECK(bootmem_reservation_receipt_verify_consume(&table_verifier,
		&table_receipt) == CB_SUCCESS);
	CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(
		&exact_table_verifier, &exact_table_receipt,
		BM_MEM_TABLE) == CB_SUCCESS);

	memset(key, 0x22, sizeof(key));
	CHECK(bootmem_reservation_receipt_provision(&reserved_signer,
		&reserved_verifier, key, BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 2,
		&reserved_handle) == CB_SUCCESS);
	CHECK(bootmem_aligned_reservation_receipt_emit_exact_tag(&reserved_handle,
		&reserved_signer, &reserved_receipt, BM_MEM_RESERVED) == CB_SUCCESS);
	CHECK(reserved_receipt.tag == BM_MEM_RESERVED);
	CHECK(bootmem_reservation_receipt_verify_consume_exact_tag(
		&reserved_verifier, &reserved_receipt, BM_MEM_RESERVED) == CB_SUCCESS);

	memset(key, 0x33, sizeof(key));
	CHECK(bootmem_reservation_receipt_provision(&wrong_signer,
		&wrong_verifier, key, BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 3,
		&table_handle) == CB_SUCCESS);
	CHECK(bootmem_aligned_reservation_receipt_emit_exact_tag(&table_handle,
		&wrong_signer, &wrong_receipt, BM_MEM_RESERVED) != CB_SUCCESS);
	CHECK(zero(&wrong_receipt, sizeof(wrong_receipt)));
	CHECK(authority_terminal_and_scrubbed(&wrong_signer));
	bootmem_reservation_receipt_close(&wrong_verifier);
	CHECK(authority_terminal_and_scrubbed(&wrong_verifier));
}

static void receipt_map_mismatch(bool os_map)
{
	struct bootmem_aligned_reservation_request value = request(0x1000, 0x1000,
		0x2000000, BM_MEM_RESERVED);
	struct bootmem_aligned_reservation_handle handle;
	struct bootmem_aligned_reservation result;
	struct bootmem_reservation_receipt_authority signer = { 0 };
	struct bootmem_reservation_receipt_authority verifier = { 0 };
	struct bootmem_reservation_receipt receipt = { 0 };
	uint8_t key[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];

	CHECK(!bootmem_aligned_reservation_register(&value, &handle));
	initialize();
	CHECK(!bootmem_aligned_reservation_query(&handle, &result));
	CHECK(result.tag == BM_MEM_RESERVED);
	memset(key, 0x44, sizeof(key));
	CHECK(bootmem_reservation_receipt_provision(&signer, &verifier, key,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 4, &handle) == CB_SUCCESS);
	bootmem_receipt_test_retag_map(os_map, result.base, result.size,
		BM_MEM_TABLE);
	CHECK(bootmem_aligned_reservation_receipt_emit_exact_tag(&handle, &signer,
		&receipt, BM_MEM_RESERVED) != CB_SUCCESS);
	CHECK(zero(&receipt, sizeof(receipt)));
	CHECK(authority_terminal_and_scrubbed(&signer));
	bootmem_reservation_receipt_close(&verifier);
	CHECK(authority_terminal_and_scrubbed(&verifier));
}
#endif

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	if (!strcmp(argv[1], "success"))
		success();
	else if (!strcmp(argv[1], "bounded"))
		bounded();
	else if (!strcmp(argv[1], "capacity"))
		capacity_failure();
	else if (!strcmp(argv[1], "atomic-capacity"))
		atomic_capacity();
	else if (!strcmp(argv[1], "presence-producer-contract"))
		presence_producer_contract();
#if CONFIG(BOOTMEM_ALIGNED_RESERVATION_RECEIPT)
	else if (!strcmp(argv[1], "receipt-handle-signer-alias"))
		receipt_alias(true);
	else if (!strcmp(argv[1], "receipt-handle-receipt-alias"))
		receipt_alias(false);
	else if (!strcmp(argv[1], "receipt-signer-first-alias"))
		signer_receipt_alias(false);
	else if (!strcmp(argv[1], "receipt-receipt-first-alias"))
		signer_receipt_alias(true);
	else if (!strcmp(argv[1], "receipt-boundary-arithmetic"))
		receipt_boundary_arithmetic();
	else if (!strcmp(argv[1], "receipt-exact-tags"))
		receipt_exact_tags();
	else if (!strcmp(argv[1], "receipt-os-map-mismatch"))
		receipt_map_mismatch(true);
	else if (!strcmp(argv[1], "receipt-firmware-map-mismatch"))
		receipt_map_mismatch(false);
#endif
	else
		CHECK(false);
	return 0;
}
