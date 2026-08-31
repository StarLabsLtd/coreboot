/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef ACPI_QEMU_RSDP_H
#define ACPI_QEMU_RSDP_H

#include <acpi/acpi.h>
#include <string.h>

static inline bool acpi_qemu_rsdp_needs_xsdt(const acpi_rsdp_t *rsdp)
{
	/* ACPI 1.x ends before length and xsdt_address.  Keep these checks
	 * short-circuited so stale or inaccessible trailing bytes are irrelevant. */
	return rsdp->revision < 2 || rsdp->length < sizeof(*rsdp) ||
		rsdp->xsdt_address == 0;
}

static inline void acpi_qemu_clear_xsdt(acpi_xsdt_t *xsdt)
{
	memset(xsdt, 0, sizeof(*xsdt));
}

#endif
