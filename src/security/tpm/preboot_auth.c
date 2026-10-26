/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <bootstate.h>
#include <console/console.h>
#include <security/tpm/tspi.h>
#include <vb2_api.h>

/* Public domain separator; the payload closes this static PCR before OS hand-off. */
static const uint8_t preboot_auth_open[VB2_SHA256_DIGEST_SIZE] = {
	0x1f, 0x2b, 0x62, 0xd1, 0xa5, 0x2b, 0xde, 0x2a,
	0x4f, 0xf7, 0x1a, 0x99, 0x5d, 0xf8, 0x82, 0xd4,
	0x15, 0x3f, 0xc0, 0x1c, 0x91, 0x2d, 0x71, 0x7a,
	0xd6, 0x9e, 0xd5, 0x83, 0x8e, 0x42, 0xa3, 0x6e,
};

static void open_preboot_auth_phase(void *unused)
{
	/* A failed S3 wake must not reuse retained authority as a cold boot. */
	if (acpi_is_wakeup_s3())
		die("Cannot open authentication after failed S3 resume\n");

	if (tpm_extend_pcr_compact_hash(6, VB2_HASH_SHA256, preboot_auth_open,
				      sizeof(preboot_auth_open),
				      "Star Labs pre-OS authentication phase open v1") != TPM_SUCCESS)
		die("Unable to open TPM pre-OS authentication phase\n");
}

/* Native table publication snapshots the event-log extent. */
#if CONFIG(PAYLOAD_CDK2)
BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, open_preboot_auth_phase, NULL);
#else
BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, open_preboot_auth_phase, NULL);
#endif
