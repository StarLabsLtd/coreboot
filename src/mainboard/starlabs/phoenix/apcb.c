/* SPDX-License-Identifier: GPL-2.0-only */
/* Authenticate only unchanged factory APCBs with an erased MAC. */

#include <acpi/acpi.h>
#include <amdblocks/psp.h>
#include <boot_device.h>
#include <bootstate.h>
#include <cbfs.h>
#include <commonlib/endian.h>
#include <console/console.h>
#include <cpu/x86/cache.h>
#include <fmap.h>
#include <string.h>
#include <vendorcode/amd/psp/psp_fw_parser.h>

#define HMAC_SIZE 32
static uint8_t factory[0xc000] __aligned(32);
static uint8_t data[0xc000] __aligned(32);
static size_t slots[4], slot_count, apcb_size;
static bool prepared, pending[4];

static bool collect(const struct psp_dir_region *dir, size_t level, size_t index,
		    const union psp_dir_entry *entry, void *arg)
{
	struct region_device region;
	if (dir->type != PSP_DIR_BIOS || dir->level != 2 ||
	    entry->bios.type != AMD_BIOS_APCB || entry->bios.inst != 0)
		return true;

	size_t offset = psp_dir_entry_offset(dir, entry);
	for (size_t i = 0; i < slot_count; i++)
		if (slots[i] == offset)
			return true;

	if (!entry->bios.writable || entry->bios.size < apcb_size + HMAC_SIZE ||
	    fmap_locate_area_as_rdev("AMDFW", &region))
		return false;

	size_t start = region_device_offset(&region), size = region_device_sz(&region);
	if (offset < start || entry->bios.size > size ||
	    offset - start > size - entry->bios.size || slot_count == ARRAY_SIZE(slots))
		return false;

	slots[slot_count++] = offset;
	return true;
}

static bool erased_mac(const uint8_t *mac)
{
	for (size_t i = 0; i < HMAC_SIZE; i++)
		if (mac[i] != 0xff)
			return false;
	return true;
}

static void prepare(void *unused)
{
	struct psp_dir_region efs;
	uint32_t status = UINT32_MAX;
	if (acpi_is_wakeup_s3())
		return;

	size_t length = cbfs_load("apcb-factory", factory, sizeof(factory));
	if (length < 160 || memcmp(factory, "APCB", 4) || read_le16(factory + 4) != 128 ||
	    read_le16(factory + 6) != 0x30)
		return;
	apcb_size = read_le32(factory + 8);
	if (apcb_size < 128 || apcb_size > length - HMAC_SIZE ||
	    !erased_mac(factory + apcb_size))
		return;
	uint8_t sum = 0;
	for (size_t i = 0; i < apcb_size; i++)
		sum += factory[i];
	if (sum || psp_fw_read_and_validate_table(boot_device_ro(),
	    CONFIG_AMD_FWM_POSITION, PSP_DIR_EFS, &efs) != CB_SUCCESS ||
	    !psp_dir_walk_recursive(&efs, 0, 0, NULL, collect, NULL) || !slot_count) {
		printk(BIOS_ERR, "APCB init: invalid factory data or directory\n");
		return;
	}

	for (size_t i = 0; i < slot_count; i++) {
		if (rdev_readat(boot_device_ro(), data, slots[i], apcb_size + HMAC_SIZE) !=
		    apcb_size + HMAC_SIZE || memcmp(data, factory, apcb_size)) {
			printk(BIOS_ERR, "APCB init: primary %zx differs from factory; refusing\n", slots[i]);
			return;
		}
		if (erased_mac(data + apcb_size)) {
			pending[i] = true;
			continue;
		}
		int ret = psp_validate_hmac_data(data, apcb_size, &status);
		printk(BIOS_NOTICE, "APCB init: primary %zx validation=%d status=%#x\n",
		       slots[i], ret, status);
		if (ret || status)
			return;
	}

	bool needs_signing = false;
	for (size_t i = 0; i < slot_count; i++)
		needs_signing |= pending[i];
	if (!needs_signing) {
		memset(factory, 0, sizeof(factory));
		memset(data, 0, sizeof(data));
		return;
	}

	/* The PSP authenticates the exact immutable body already checked above. */
	int ret = psp_sign_hmac_data(factory, apcb_size, &status);
	printk(BIOS_NOTICE, "APCB init: sign=%d status=%#x slots=%zu\n", ret, status, slot_count);
	if (ret || status)
		return;
	ret = psp_validate_hmac_data(factory, apcb_size, &status);
	printk(BIOS_NOTICE, "APCB init: signed factory validation=%d status=%#x\n", ret, status);
	prepared = !ret && !status;
}

static void commit(void *unused)
{
	if (!prepared)
		return;
	const struct region_device *rw = boot_device_rw();
	if (!rw)
		return;

	for (size_t i = 0; i < slot_count; i++) {
		if (!pending[i])
			continue;
		/* Recheck through the protected SPI backend. */
		if (rdev_readat(rw, data, slots[i], apcb_size + HMAC_SIZE) !=
		    apcb_size + HMAC_SIZE || memcmp(data, factory, apcb_size) ||
		    !erased_mac(data + apcb_size)) {
			printk(BIOS_ERR, "APCB init: primary changed before write; refusing\n");
			break;
		}
		if (rdev_writeat(rw, factory + apcb_size, slots[i] + apcb_size, HMAC_SIZE) != HMAC_SIZE) {
			printk(BIOS_ERR, "APCB init: MAC write failed\n");
			break;
		}
		/* ROM Armor 3 reads the WP-cached ROM mapping; discard the old line. */
		void *mapped = rdev_mmap(boot_device_ro(), slots[i] + apcb_size, HMAC_SIZE);
		if (!mapped)
			break;
		clflush_region((uintptr_t)mapped, HMAC_SIZE);
		rdev_munmap(boot_device_ro(), mapped);
		if (rdev_readat(rw, data, slots[i] + apcb_size, HMAC_SIZE) != HMAC_SIZE ||
		    memcmp(data, factory + apcb_size, HMAC_SIZE)) {
			printk(BIOS_ERR, "APCB init: primary %zx MAC write/readback failed\n", slots[i]);
			break;
		}
		printk(BIOS_NOTICE, "APCB init: primary %zx MAC initialized and verified\n", slots[i]);
	}
	memset(factory, 0, sizeof(factory));
	memset(data, 0, sizeof(data));
	prepared = false;
}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_ENTRY, prepare, NULL);
BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, commit, NULL);
