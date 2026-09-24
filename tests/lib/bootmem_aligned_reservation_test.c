/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootmem.h>
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

int main(int argc, char **argv)
{
	CHECK(argc == 2);
	if (!strcmp(argv[1], "success"))
		success();
	else if (!strcmp(argv[1], "bounded"))
		bounded();
	else if (!strcmp(argv[1], "capacity"))
		capacity_failure();
	else
		CHECK(false);
	return 0;
}
