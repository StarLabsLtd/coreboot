/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_H

#include <commonlib/coreboot_tables.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_S3_BACKING_PAGE_SIZE 4096U

struct payload_mm_authvar_presence_s3_backing {
	uint32_t revision;
	uint32_t size;
	uint32_t loader_lifecycle;
	uint32_t reserved;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	struct lb_authvar_presence_endpoint presence;
	struct lb_authvar_presence_lifecycle_close_endpoint lifecycle_close;
} __aligned(8);

typedef bool (*payload_mm_authvar_presence_s3_dram_contains_fn)(
	uint64_t base, uint64_t size);

bool payload_mm_authvar_presence_s3_backing_from_table(
	const void *table, size_t table_capacity,
	payload_mm_authvar_presence_s3_dram_contains_fn dram_contains,
	const struct smm_invocation_loader_instance *loader_instance,
	bool acpi_s3,
	struct payload_mm_authvar_presence_s3_backing *backing);

bool payload_mm_authvar_presence_s3_backing_validate(
	const struct payload_mm_authvar_presence_s3_backing *backing,
	const struct smm_invocation_loader_instance *loader_instance);
bool payload_mm_authvar_presence_s3_backing_dram_provenance(
	const struct payload_mm_authvar_presence_s3_backing *backing,
	const struct smm_invocation_loader_instance *loader_instance,
	void *context, uint64_t base, uint64_t size);

/* Called while the SMM module parameters remain writable in ramstage. */
bool platform_smm_authvar_presence_s3_backing(
	const struct smm_invocation_loader_instance *loader_instance,
	bool acpi_s3,
	struct payload_mm_authvar_presence_s3_backing *backing);

#if ENV_TEST
void payload_mm_authvar_presence_s3_backing_test_hook(unsigned int point);
#endif

#if ENV_SMM || ENV_TEST
const struct payload_mm_authvar_presence_s3_backing *
smm_get_payload_mm_authvar_presence_s3_backing(void);
bool smm_payload_mm_authvar_presence_s3_dram_provenance(
	void *context, uint64_t base, uint64_t size);
#endif

#endif
