/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/acpi/qemu_rsdp.h"

#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int expect(bool condition, const char *message)
{
	if (condition)
		return 0;
	(void)message;
	return 1;
}

int main(void)
{
	long page_size = sysconf(_SC_PAGESIZE);
	unsigned char *mapping;
	acpi_rsdp_t rsdp;
	acpi_rsdp_t *short_rsdp;
	acpi_xsdt_t xsdt;
	int failures = 0;

	mapping = mmap(NULL, (size_t)page_size * 2U, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (mapping == MAP_FAILED)
		return 1;
	if (mprotect(mapping + page_size, page_size, PROT_NONE) != 0)
		return 1;
	short_rsdp = (acpi_rsdp_t *)(void *)(mapping + page_size - 20U);
	memset(short_rsdp, 0, 20U);
	short_rsdp->revision = 0;
	failures += expect(acpi_qemu_rsdp_needs_xsdt(short_rsdp),
		"revision 0 read beyond its 20-byte object");
	short_rsdp->revision = 1;
	failures += expect(acpi_qemu_rsdp_needs_xsdt(short_rsdp),
		"revision 1 read beyond its 20-byte object");
	rsdp = (acpi_rsdp_t){0};
	rsdp.revision = 2;
	rsdp.length = sizeof(rsdp);
	rsdp.xsdt_address = 0x12345000U;
	failures += expect(!acpi_qemu_rsdp_needs_xsdt(&rsdp),
		"valid ACPI 2.0 XSDT was rebuilt");
	rsdp.length = sizeof(rsdp) - 1U;
	failures += expect(acpi_qemu_rsdp_needs_xsdt(&rsdp),
		"short ACPI 2.0 RSDP was reused");
	rsdp.length = sizeof(rsdp);
	rsdp.xsdt_address = 0;
	failures += expect(acpi_qemu_rsdp_needs_xsdt(&rsdp),
		"ACPI 2.0 RSDP without XSDT was reused");
	memset(&xsdt, 0xff, sizeof(xsdt));
	acpi_qemu_clear_xsdt(&xsdt);
	failures += expect(xsdt.header.length == 0 && xsdt.entry[0] == 0,
		"fresh XSDT retained prior entries");
	xsdt.entry[0] = 0x12345000U;
	acpi_qemu_clear_xsdt(&xsdt);
	failures += expect(xsdt.entry[0] == 0,
		"second XSDT construction retained its first entry");
	(void)munmap(mapping, (size_t)page_size * 2U);
	return failures != 0;
}
