/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#define _GNU_SOURCE
#include <cdk2/acpi_table.h>
#include <cdk2/diagnostic.h>

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

typedef __typeof__(((struct cdk2_acpi_tables *)0)->capacity) native_size;
typedef __typeof__(((struct cdk2_acpi_record *)0)->key) native_key;
typedef __typeof__(cdk2_acpi_install(NULL, NULL, 0, NULL)) native_status;

struct fixture {
	size_t allocations;
	size_t frees;
	native_status failure;
};

static void write32(uint8_t *bytes, uint32_t value)
{
	for (unsigned int index = 0; index < 4; index++)
		bytes[index] = value >> (index * 8);
}

static void write64(uint8_t *bytes, uint64_t value)
{
	write32(bytes, value);
	write32(bytes + 4, value >> 32);
}

static void checksum(uint8_t *bytes, size_t size, size_t offset)
{
	bytes[offset] = 0;
	bytes[offset] = 0U - cdk2_acpi_checksum(bytes, size);
}

static void table(uint8_t *bytes, const char *signature, uint32_t size)
{
	memset(bytes, 0, size);
	memcpy(bytes, signature, 4);
	write32(bytes + 4, size);
	bytes[8] = 2;
	memcpy(bytes + 10, "HOST01", 6);
	memcpy(bytes + 16, "MODELAPI", 8);
	checksum(bytes, size, 9);
}

static native_status allocate(void *context, native_size size,
	native_size alignment, void **buffer)
{
	struct fixture *fixture = context;

	assert(size <= 4096 && alignment <= 16);
	if (fixture->failure != 0)
		return fixture->failure;
	*buffer = (void *)(uintptr_t)(0x12010000 + fixture->allocations++ * 4096);
	memset(*buffer, 0, 4096);
	return EFI_SUCCESS;
}

static void release(void *context, void *buffer)
{
	struct fixture *fixture = context;

	assert(buffer != NULL);
	fixture->frees++;
}

void cdk2_diag_message(enum cdk2_diag_module module, const char *message)
{
	fprintf(stderr, "modeled diagnostic sink %u %s", module, message);
}

void cdk2_diag_value(enum cdk2_diag_module module, const char *label,
	uint64_t value)
{
	fprintf(stderr, "modeled diagnostic sink %u %s %016llx\n", module,
		label, (unsigned long long)value);
}

void cdk2_diag_bytes(enum cdk2_diag_module module, const char *label,
	const void *bytes, size_t size)
{
	(void)module;
	(void)label;
	(void)bytes;
	(void)size;
	assert(!"unused ACPI bytes diagnostic unexpectedly called");
}

#define LAYOUT(type, member) \
	fprintf(stderr, #type "." #member " %zu\n", offsetof(type, member))

static void snapshot(const struct cdk2_acpi_tables *tables,
	const struct cdk2_acpi_record *records)
{
	struct cdk2_acpi_tables copy = *tables;

	/* Only process-specific context/code/record pointers are omitted. */
	copy.allocator.context = NULL;
	copy.allocator.allocate = NULL;
	copy.allocator.free = NULL;
	copy.records = NULL;
	assert(fwrite(&copy, sizeof(copy), 1, stdout) == 1);
	assert(fwrite(records, sizeof(*records), 4, stdout) == 4);
}

int main(void)
{
	uint8_t *region = mmap((void *)0x12000000, 0x200000,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	struct fixture fixture = {0};
	struct cdk2_acpi_allocator allocator = {&fixture, allocate, release};
	struct cdk2_acpi_record records[4] = {0};
	struct cdk2_acpi_tables tables;
	uint8_t *rsdp;
	uint8_t *xsdt;
	uint8_t *rsdt;
	uint8_t *child;
	uint8_t replacement[48];
	native_key key = 0;

	assert(region == (void *)0x12000000);
	fprintf(stderr, "allocator %zu %zu record %zu %zu tables %zu %zu\n",
		sizeof(allocator), _Alignof(struct cdk2_acpi_allocator),
		sizeof(records[0]), _Alignof(struct cdk2_acpi_record),
		sizeof(tables), _Alignof(struct cdk2_acpi_tables));
	LAYOUT(struct cdk2_acpi_allocator, context);
	LAYOUT(struct cdk2_acpi_allocator, allocate);
	LAYOUT(struct cdk2_acpi_allocator, free);
	LAYOUT(struct cdk2_acpi_record, table);
	LAYOUT(struct cdk2_acpi_record, length);
	LAYOUT(struct cdk2_acpi_record, key);
	LAYOUT(struct cdk2_acpi_record, owned);
	LAYOUT(struct cdk2_acpi_tables, allocator);
	LAYOUT(struct cdk2_acpi_tables, records);
	LAYOUT(struct cdk2_acpi_tables, capacity);
	LAYOUT(struct cdk2_acpi_tables, count);
	LAYOUT(struct cdk2_acpi_tables, next_key);
	LAYOUT(struct cdk2_acpi_tables, replacement_index);
	LAYOUT(struct cdk2_acpi_tables, replacement_pending);
	LAYOUT(struct cdk2_acpi_tables, rsdp);
	LAYOUT(struct cdk2_acpi_tables, rsdp_length);
	LAYOUT(struct cdk2_acpi_tables, rsdp10);
	LAYOUT(struct cdk2_acpi_tables, rsdt);
	LAYOUT(struct cdk2_acpi_tables, xsdt);

	rsdp = region;
	xsdt = region + 0x1000;
	rsdt = region + 0x2000;
	child = region + 0x3000;
	table(child, "FACP", 48);
	table(xsdt, "XSDT", 44);
	write64(xsdt + 36, (uintptr_t)child);
	checksum(xsdt, 44, 9);
	table(rsdt, "RSDT", 40);
	write32(rsdt + 36, (uintptr_t)child);
	checksum(rsdt, 40, 9);
	memcpy(rsdp, "RSD PTR ", 8);
	memcpy(rsdp + 9, "HOST01", 6);
	rsdp[15] = 2;
	write32(rsdp + 16, (uintptr_t)rsdt);
	write32(rsdp + 20, 36);
	write64(rsdp + 24, (uintptr_t)xsdt);
	checksum(rsdp, 20, 8);
	checksum(rsdp, 36, 32);
	assert(cdk2_acpi_tables_initialize(&tables, &allocator, records,
		4, rsdp) == EFI_SUCCESS);
	snapshot(&tables, records);
	table(replacement, "TPM2", sizeof(replacement));
	for (unsigned int flag = 0; flag < 256; flag++) {
		struct cdk2_acpi_tables copy = tables;
		struct cdk2_acpi_record copies[4];
		uint8_t root[52], extended[68], pointer[36];
		size_t before = fixture.frees;

		memcpy(copies, records, sizeof(copies));
		memcpy(root, tables.rsdt, sizeof(root));
		memcpy(extended, tables.xsdt, sizeof(extended));
		memcpy(pointer, tables.rsdp, sizeof(pointer));
		copy.records = copies;
		copy.rsdt = root;
		copy.xsdt = extended;
		copy.rsdp = pointer;
		copy.replacement_pending = flag;
		copies[0].owned = flag;
		assert(cdk2_acpi_uninstall(&copy, copies[0].key) == EFI_SUCCESS);
		assert(fixture.frees == before + (flag != 0));
		assert(copy.replacement_pending != 0);
		/* Preserve every stored byte, including padding, before predicates. */
		copies[0].owned = flag;
		copy.replacement_pending = flag;
		copy.capacity = (native_size)UINT64_C(0xfedcba9876543210);
		copy.next_key = UINT64_C(0x8abcdeff12345678);
		copy.rsdt = tables.rsdt;
		copy.xsdt = tables.xsdt;
		copy.rsdp = tables.rsdp;
		snapshot(&copy, copies);
	}
	tables.next_key = UINT64_MAX;
	assert(cdk2_acpi_install(&tables, replacement, sizeof(replacement),
		&key) == EFI_SUCCESS);
	assert(key == UINT64_MAX && tables.next_key == 1);
	snapshot(&tables, records);
	assert(cdk2_acpi_uninstall(&tables, key) == EFI_SUCCESS);
	for (unsigned int index = 0; index < 3; index++) {
		static const uint64_t errors[] = {
			UINT64_C(0x8000000000000001),
			UINT64_C(0xabcdef0012345678), UINT64_MAX
		};
		fixture.failure = errors[index];
		key = UINT64_C(0x8abcdeff12345678);
		assert(cdk2_acpi_install(&tables, replacement,
			sizeof(replacement), &key) == errors[index]);
		assert(key == UINT64_C(0x8abcdeff12345678));
	}
	assert(cdk2_acpi_checksum(tables.rsdp, 20) == 0);
	assert(cdk2_acpi_checksum(tables.rsdp, 36) == 0);
	assert(fwrite(tables.rsdp, 36, 1, stdout) == 1);
	assert(fwrite(tables.rsdt, 52, 1, stdout) == 1);
	assert(fwrite(tables.xsdt, 68, 1, stdout) == 1);
	snapshot(&tables, records);
	cdk2_acpi_tables_destroy(&tables);
	assert(munmap(region, 0x200000) == 0);
	return 0;
}
