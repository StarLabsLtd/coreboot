/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */
/* Copyright (c) 2026 Star Labs Systems. All rights reserved. */

#include <acpi/acpi.h>
#include <bootstate.h>
#include <console/console.h>
#include <security/tpm/tspi.h>
#include <vb2_sha.h>

#if !CONFIG(TPM2) || !CONFIG(TPM_MEASURED_BOOT) || !CONFIG(TPM_LOG_TPM2) || \
	!CONFIG(TPM_HASH_SHA256)
#error "DAVE requires TPM2 SHA256 measured boot and its native event log"
#endif

static void dave_open_phase(void *unused)
{
	static const char domain[] = "Star Labs pre-OS authentication phase open v1";
	struct vb2_sha256_context hash;
	uint8_t digest[VB2_SHA256_DIGEST_SIZE];

	/* A missing OS wake vector can fall through the S3 resume path. The
	 * payload must not treat retained TPM authority as a fresh cold boot.
	 */
	if (acpi_is_wakeup_s3())
		die("DAVE: cannot enter authentication after failed S3 resume\n");

	/* Logging a deferred measurement is insufficient: the payload must prove
	 * the actual PCR value through its pinned, authenticated TPM session.
	 */
	vb2_sha256_init(&hash, VB2_HASH_SHA256);
	vb2_sha256_update(&hash, (const uint8_t *)domain, sizeof(domain) - 1);
	vb2_sha256_finalize(&hash, digest, sizeof(digest));
	if (tlcl_lib_init() != TPM_SUCCESS || tlcl_get_family() != TPM_2 ||
	    tlcl_extend(6, digest, VB2_HASH_SHA256) != TPM_SUCCESS)
		die("DAVE: cannot open pre-OS authentication phase\n");
	tpm_log_add_table_entry(domain, 6, VB2_HASH_SHA256, digest, sizeof(digest));
}

/* Include the event before table publication snapshots the log extent. */
BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, dave_open_phase, NULL);
