/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootmem.h>
#include <device/device.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)
#define TEST_SYMBOL(symbol, value) \
	asm(".set " #symbol ", " #value "\n\t.globl " #symbol)

TEST_SYMBOL(_program, 0x1000);
TEST_SYMBOL(_eprogram, 0x2000);
TEST_SYMBOL(_program_size, 0x1000);
TEST_SYMBOL(_stack, 0x2000);
TEST_SYMBOL(_estack, 0x3000);
TEST_SYMBOL(_stack_size, 0x1000);

static struct resource domain_resources[] = {
	{
		.base = 0,
		.size = 0x8000,
		.flags = IORESOURCE_MEM | IORESOURCE_CACHEABLE |
			IORESOURCE_ASSIGNED,
		.next = &domain_resources[1],
	},
	{
		.base = 0x100000000ULL,
		.size = 0x6000,
		.flags = IORESOURCE_MEM | IORESOURCE_CACHEABLE |
			IORESOURCE_ASSIGNED,
		.next = &domain_resources[2],
	},
	{
		.base = 0xfffff000ULL,
		.size = 0x2000,
		.flags = IORESOURCE_MEM | IORESOURCE_RESERVE |
			IORESOURCE_ASSIGNED,
		.next = &domain_resources[3],
	},
	{
		.base = 0x100002000ULL,
		.size = 0x1000,
		.flags = IORESOURCE_MEM | IORESOURCE_RESERVE |
			IORESOURCE_ASSIGNED,
		.next = &domain_resources[4],
	},
	{
		.base = 0x100005000ULL,
		.size = 0x2000,
		.flags = IORESOURCE_MEM | IORESOURCE_RESERVE |
			IORESOURCE_ASSIGNED,
		.next = &domain_resources[5],
	},
	{
		.base = 0x500000123ULL,
		.size = 0x234,
		.flags = IORESOURCE_MEM | IORESOURCE_CACHEABLE |
			IORESOURCE_ASSIGNED,
		.next = &domain_resources[6],
	},
	{
		.base = UINT64_MAX,
		.size = 0,
		.flags = IORESOURCE_MEM | IORESOURCE_CACHEABLE |
			IORESOURCE_ASSIGNED,
	},
};

static struct resource non_domain_resources[] = {
	{
		.base = 0x200000000ULL,
		.size = 0x2000,
		.flags = IORESOURCE_MEM | IORESOURCE_CACHEABLE |
			IORESOURCE_ASSIGNED,
		.next = &non_domain_resources[1],
	},
	{
		.base = 0x300000000ULL,
		.size = 0x1000,
		.flags = IORESOURCE_MEM | IORESOURCE_RESERVE |
			IORESOURCE_ASSIGNED,
	},
};

static struct device non_domain_device = {
	.enabled = 1,
	.path.type = DEVICE_PATH_PCI,
	.resource_list = non_domain_resources,
};

static struct device domain_device = {
	.enabled = 1,
	.path.type = DEVICE_PATH_DOMAIN,
	.resource_list = domain_resources,
	.next = &non_domain_device,
};

struct device *all_devices = &domain_device;

bool bootmem_domain_dram_resource_valid_for_test(struct device *dev,
	struct resource *res);

void cbmem_add_bootmem(void)
{
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

struct expected_range {
	uint64_t base;
	uint64_t size;
	enum bootmem_type tag;
};

static const struct expected_range expected[] = {
	{ 0, 0x1000, BM_MEM_RAM },
	{ 0x1000, 0x2000, BM_MEM_RAMSTAGE },
	{ 0x3000, 0x5000, BM_MEM_RAM },
	{ 0x100000000ULL, 0x1000, BM_MEM_RESERVED },
	{ 0x100001000ULL, 0x1000, BM_MEM_RAM },
	{ 0x100002000ULL, 0x1000, BM_MEM_RESERVED },
	{ 0x100003000ULL, 0x2000, BM_MEM_RAM },
	{ 0x100005000ULL, 0x1000, BM_MEM_RESERVED },
	{ 0x500000123ULL, 0x234, BM_MEM_RAM },
};

struct walk_context {
	size_t count;
	size_t stop_after;
};

static bool verify_range(const struct range_entry *range, void *argument)
{
	struct walk_context *context = argument;
	const struct expected_range *want;

	CHECK(context->count < ARRAY_SIZE(expected));
	want = &expected[context->count++];
	CHECK(range_entry_base(range) == want->base);
	CHECK(range_entry_size(range) == want->size);
	CHECK(range_entry_tag(range) == want->tag);
	return !context->stop_after || context->count < context->stop_after;
}

int main(int argc, char **argv)
{
	struct {
		struct lb_memory header;
		struct lb_memory_range ranges[16];
	} table = { .header.size = sizeof(table.header) };
	struct walk_context complete = { 0 };
	struct walk_context stopped = { .stop_after = 2 };
	struct resource overflow = {
		.base = UINT64_MAX,
		.size = 2,
	};

	CHECK(!bootmem_domain_dram_resource_valid_for_test(&domain_device,
		&domain_resources[6]));
	CHECK(!bootmem_domain_dram_resource_valid_for_test(&domain_device,
		&overflow));
	if (argc == 2 && !strcmp(argv[1], "overflow")) {
		domain_resources[6].size = 2;
		bootmem_write_memory_table(&table.header);
		__builtin_trap();
	}
	CHECK(argc == 1);

	bootmem_write_memory_table(&table.header);
	CHECK(bootmem_region_targets_type(0x200000000ULL, 0x2000,
		BM_MEM_RAM));
	CHECK(bootmem_region_targets_type(0x300000000ULL, 0x1000,
		BM_MEM_RESERVED));
	CHECK(!bootmem_walk_dram(verify_range, &complete));
	CHECK(complete.count == ARRAY_SIZE(expected));
	CHECK(bootmem_walk_dram(verify_range, &stopped));
	CHECK(stopped.count == stopped.stop_after);
	return 0;
}
