/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_qemu_pflash.h>
#include <boot/payload_mm_authvar_smm_bootstrap.h>
#include <boot_device.h>
#include <commonlib/region.h>
#include <smmstore.h>

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable QEMU binding is SMM-only"
#endif

#define QEMU_PFLASH_ERASE_SIZE 4096U

static enum cb_err media_facts(void *unused,
	struct payload_mm_authvar_smm_media_facts *facts)
{
	const struct region_device *root = boot_device_rw();
	struct region_device store;

	(void)unused;
	if (!facts || !root || !region_device_sz(root) ||
	    smmstore_lookup_read_region(&store) < 0)
		return CB_ERR;
	*facts = (struct payload_mm_authvar_smm_media_facts) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_MEDIA_REVISION,
		.size = sizeof(*facts),
		.boot_media_size = region_device_sz(root),
		.store_offset = region_device_offset(&store),
		.store_size = region_device_sz(&store),
		.block_size = SMM_BLOCK_SIZE,
		.erase_size = QEMU_PFLASH_ERASE_SIZE,
	};
	return CB_SUCCESS;
}

static enum cb_err media_install(void *unused)
{
	(void)unused;
	return payload_mm_authvar_qemu_pflash_install();
}

bool platform_payload_mm_authvar_smm_media_ops(
	struct payload_mm_authvar_smm_media_ops *ops)
{
	if (!ops)
		return false;
	*ops = (struct payload_mm_authvar_smm_media_ops) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_MEDIA_REVISION,
		.size = sizeof(*ops),
		.facts = media_facts,
		.install = media_install,
	};
	return true;
}
