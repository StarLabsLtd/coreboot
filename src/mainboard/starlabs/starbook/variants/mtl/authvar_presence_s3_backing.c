/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_s3_backing.h>
#include <bootmem.h>
#include <cbmem.h>
#include <commonlib/bsd/cbmem_id.h>

bool platform_smm_authvar_presence_s3_backing(
	const struct smm_invocation_loader_instance *loader_instance,
	bool acpi_s3,
	struct payload_mm_authvar_presence_s3_backing *backing)
{
	const struct cbmem_entry *entry = cbmem_entry_find(CBMEM_ID_CBTABLE);

	return entry && payload_mm_authvar_presence_s3_backing_from_table(
		cbmem_entry_start(entry), cbmem_entry_size(entry),
		bootmem_domain_dram_contains, loader_instance, acpi_s3, backing);
}
