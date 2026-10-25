/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot_device.h>
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <fmap.h>
#include <fmap_config.h>
#include <smmstore.h>
#include <string.h>
#include <types.h>
#if CONFIG(SMMSTORE_SRK_PIN) && ENV_SMM
#include <intelblocks/fast_spi.h>
#endif

#define SMMSTORE_REGION "SMMSTORE"

#define SMMSTORE_MIN_SIZE (64 * KiB)

_Static_assert(IS_ALIGNED(FMAP_SECTION_SMMSTORE_START, SMM_BLOCK_SIZE),
	       "SMMSTORE FMAP region not aligned to block size");

_Static_assert(FMAP_SECTION_SMMSTORE_SIZE >= SMMSTORE_MIN_SIZE,
	       "SMMSTORE FMAP region must be at least 64 KiB");

_Static_assert(FMAP_SECTION_SMMSTORE_SIZE >= SMM_BLOCK_SIZE,
	       "SMMSTORE FMAP region must fit at least one logical block");

static int smmstore_use_full_flash;
static int has_capsules = -1;

#if CONFIG(SMMSTORE_SRK_PIN)
_Static_assert(IS_ALIGNED(FMAP_SECTION_SRK_PIN_V1_START, SMM_BLOCK_SIZE),
	       "SRK pin must have its own erase block");
_Static_assert(IS_ALIGNED(FMAP_SECTION_SRK_PIN_V1_SIZE, SMM_BLOCK_SIZE),
	       "SRK pin size must cover complete erase blocks");
_Static_assert(FMAP_SECTION_SRK_PIN_V1_SIZE >= SMM_BLOCK_SIZE,
	       "SRK pin region is missing");

#define PIN_FMAP_START ALIGN_DOWN(FMAP_SECTION_FMAP_START, SMM_BLOCK_SIZE)
#define PIN_FMAP_END ALIGN_UP(FMAP_SECTION_FMAP_START + FMAP_SECTION_FMAP_SIZE, SMM_BLOCK_SIZE)

struct srk_pin_record {
	uint8_t magic[4];
	uint8_t name[34];
	uint16_t committed;
} __packed;

_Static_assert(sizeof(struct srk_pin_record) == 40, "Unexpected SRK pin layout");
_Static_assert(FMAP_SECTION_SRK_PIN_V1_START <= CONFIG_ROM_SIZE &&
	       FMAP_SECTION_SRK_PIN_V1_SIZE <= CONFIG_ROM_SIZE - FMAP_SECTION_SRK_PIN_V1_START,
	       "SRK pin is outside the boot device");
_Static_assert(PIN_FMAP_END <= CONFIG_ROM_SIZE, "FMAP is outside the boot device");
_Static_assert(FMAP_SECTION_SRK_PIN_V1_START >= PIN_FMAP_END ||
	       FMAP_SECTION_SRK_PIN_V1_START + FMAP_SECTION_SRK_PIN_V1_SIZE <= PIN_FMAP_START,
	       "SRK pin and FMAP erase blocks overlap");

static bool overlaps(size_t start, size_t size, size_t protected_start, size_t protected_size)
{
	if (!size || start > SIZE_MAX - size)
		return true;
	return start < protected_start + protected_size && protected_start < start + size;
}

static bool raw_write_protected(size_t store_offset, size_t offset, size_t size)
{
	if (!size || store_offset > CONFIG_ROM_SIZE || offset > CONFIG_ROM_SIZE - store_offset)
		return true;
	offset += store_offset;
	if (size > CONFIG_ROM_SIZE - offset)
		return true;

	/* Exclude the whole erase block, even for a partial write. */
	size_t end = ALIGN_UP(offset + size, SMM_BLOCK_SIZE);
	offset = ALIGN_DOWN(offset, SMM_BLOCK_SIZE);
	return overlaps(offset, end - offset, FMAP_SECTION_SRK_PIN_V1_START,
			FMAP_SECTION_SRK_PIN_V1_SIZE) ||
		overlaps(offset, end - offset, PIN_FMAP_START, PIN_FMAP_END - PIN_FMAP_START);
}

#if ENV_SMM
static const uint8_t pin_magic[] = { 'S', 'R', 'K', '1' };
static bool pin_creation_closed;

static bool pin_erased(const struct region_device *store)
{
	uint8_t bytes[128];
	for (size_t offset = 0; offset < region_device_sz(store); offset += sizeof(bytes)) {
		if (rdev_readat(store, bytes, offset, sizeof(bytes)) != sizeof(bytes))
			return false;
		for (size_t i = 0; i < sizeof(bytes); i++)
			if (bytes[i] != 0xff)
				return false;
	}
	return true;
}

uint32_t smmstore_srk_exec(uint8_t command, void *buffer)
{
	const struct region region = {
		.offset = FMAP_SECTION_SRK_PIN_V1_START,
		.size = FMAP_SECTION_SRK_PIN_V1_SIZE,
	};
	struct region_device store;
	struct srk_pin_record record;
	struct smmstore_srk_info info = {
		.revision = 1,
		.offset = FMAP_SECTION_SRK_PIN_V1_START,
		.size = FMAP_SECTION_SRK_PIN_V1_SIZE,
		.fmap_offset = PIN_FMAP_START,
		.fmap_size = PIN_FMAP_END - PIN_FMAP_START,
	};
	uint8_t requested[34];
	bool valid;

	if (command == SMMSTORE_CMD_SECURITY_CLOSE) {
		pin_creation_closed = true;
		has_capsules = 0;
		smmstore_use_full_flash = 0;
		return SMMSTORE_RET_SUCCESS;
	}
	if (!fast_spi_eiss_status())
		return SMMSTORE_RET_FAILURE;

	/* Snapshot the caller's Name before inspecting or changing persistent data. */
	memcpy(requested, buffer, sizeof(requested));
	if (boot_device_rw_subregion(&region, &store) < 0 ||
	    rdev_readat(&store, &record, 0, sizeof(record)) != sizeof(record))
		return SMMSTORE_RET_FAILURE;
	valid = !memcmp(record.magic, pin_magic, sizeof(pin_magic)) && record.committed == 0 &&
		record.name[0] == 0 && record.name[1] == 0x0b;

	if (command == SMMSTORE_CMD_SRK_READ) {
		if (valid)
			memcpy(info.name, record.name, sizeof(info.name));
		memcpy(buffer, &info, sizeof(info));
		if (valid)
			return SMMSTORE_RET_SUCCESS;
		if (CONFIG(SMMSTORE_SRK_PROVISION) && !pin_creation_closed &&
		    !smm_is_s3_resume() && pin_erased(&store))
			return SMMSTORE_RET_NOT_FOUND;
		return SMMSTORE_RET_FAILURE;
	}

	if (!CONFIG(SMMSTORE_SRK_PROVISION) || pin_creation_closed || smm_is_s3_resume())
		return SMMSTORE_RET_DENIED;
	if (requested[0] != 0 || requested[1] != 0x0b)
		return SMMSTORE_RET_FAILURE;
	if (valid)
		return memcmp(record.name, requested, sizeof(requested)) ?
			SMMSTORE_RET_DENIED : SMMSTORE_RET_SUCCESS;
	if (!pin_erased(&store))
		return SMMSTORE_RET_FAILURE;

	struct srk_pin_record readback;
	const uint16_t committed = 0;
	memset(&record, 0xff, sizeof(record));
	memcpy(record.magic, pin_magic, sizeof(pin_magic));
	memcpy(record.name, requested, sizeof(record.name));
	if (rdev_writeat(&store, &record, 0, offsetof(struct srk_pin_record, committed)) !=
	    offsetof(struct srk_pin_record, committed) ||
	    rdev_readat(&store, &readback, 0, sizeof(readback)) != sizeof(readback) ||
	    memcmp(&readback, &record, sizeof(record)) ||
	    rdev_writeat(&store, &committed, offsetof(struct srk_pin_record, committed),
			 sizeof(committed)) != sizeof(committed) ||
	    rdev_readat(&store, &readback, 0, sizeof(readback)) != sizeof(readback))
		return SMMSTORE_RET_FAILURE;
	record.committed = committed;
	return memcmp(&readback, &record, sizeof(record)) ?
		SMMSTORE_RET_FAILURE : SMMSTORE_RET_SUCCESS;
}
#endif
#endif

int smmstore_preprocess_cmd(uint8_t *cmd, void *param)
{
	if (CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)) {
		if (has_capsules == -1 && *cmd == SMMSTORE_CMD_USE_FULL_FLASH) {
			has_capsules = !!(uintptr_t)param;
			/*
			 * If we have capsules, return success, otherwise let smmstore_exec()
			 * fail on !param check, which will be 0 in that case. This informs
			 * the caller whether capsule handling was enabled or not.
			 */
			return has_capsules;
		} else if (has_capsules == 1 && *cmd & SMMSTORE_CMD_USE_FULL_FLASH) {
			smmstore_use_full_flash = 1;
			*cmd &= ~SMMSTORE_CMD_USE_FULL_FLASH;
		} else {
			smmstore_use_full_flash = 0;
		}
	}

	return 0;
}

static enum cb_err lookup_store_region(struct region *region)
{
	if (CONFIG(DRIVERS_EFI_UPDATE_CAPSULES) && smmstore_use_full_flash) {
		const struct region_device *rdev = boot_device_rw();

		if (rdev == NULL)
			return CB_ERR;

		*region = *region_device_region(rdev);
		return CB_SUCCESS;
	}

	if (fmap_locate_area(SMMSTORE_REGION, region)) {
		printk(BIOS_WARNING,
		       "smm store: Unable to find SMM store FMAP region '%s'\n",
		       SMMSTORE_REGION);
		return CB_ERR;
	}

	return CB_SUCCESS;
}

/*
 * Return a region device that points into the store file.
 *
 * It's the image builder's responsibility to make it block aligned so that
 * erase works without destroying other data.
 *
 * It doesn't cache the location to cope with flash changing underneath (eg
 * due to an update)
 *
 * returns 0 on success, -1 on failure
 * outputs the valid store rdev in rstore
 */
static int lookup_store(struct region_device *rstore, size_t *physical_offset)
{
	static struct region_device read_rdev, write_rdev;
	static struct incoherent_rdev store_irdev;
	struct region region;
	const struct region_device *rdev;

	if (lookup_store_region(&region) != CB_SUCCESS)
		return -1;

	if (boot_device_ro_subregion(&region, &read_rdev) < 0)
		return -1;

	if (boot_device_rw_subregion(&region, &write_rdev) < 0)
		return -1;

	rdev = incoherent_rdev_init(&store_irdev, &region, &read_rdev, &write_rdev);

	if (rdev == NULL)
		return -1;

	if (physical_offset)
		*physical_offset = region.offset;

	return rdev_chain(rstore, rdev, 0, region_device_sz(rdev));
}

 /* this function is non reentrant */
int smmstore_lookup_region(struct region_device *rstore)
{
	static int done;
	static int ret;
	static struct region_device rdev;

	if (!done) {
		done = 1;

		if (lookup_store(&rdev, NULL)) {
			printk(BIOS_WARNING,
			       "smm store: Unable to find SMM store FMAP region '%s'\n",
				SMMSTORE_REGION);
			ret = -1;
		} else {
			ret = 0;
		}
	}

	*rstore = rdev;
	return ret;
}
static struct region_device mdev_com_buf;

static int smmstore_rdev_chain(struct region_device *rdev)
{
	return rdev_chain_full(rdev, &mdev_com_buf);
}

/**
 * Call once before using the store. In SMM this must be called through an
 * APM SMI handler providing the communication buffer address and length.
 */
int smmstore_init(void *buf, size_t len)
{
	if (!buf || len < SMM_BLOCK_SIZE)
		return -1;

	if (smm_points_to_smram(buf, len))
		return -1;

	rdev_chain_mem_rw(&mdev_com_buf, buf, len);

	return 0;
}

#if ENV_RAMSTAGE
/**
 * Provide metadata for the coreboot tables.
 * Must only be called in ramstage, but not in SMM.
 */
int smmstore_get_info(struct smmstore_params_info *out)
{
	struct region_device store;

	if (lookup_store(&store, NULL) < 0) {
		printk(BIOS_ERR, "smm store: lookup of store failed\n");
		return -1;
	}

	if (!IS_ALIGNED(region_device_offset(&store), SMM_BLOCK_SIZE)) {
		printk(BIOS_ERR, "smm store: store not aligned to block size\n");
		return -1;
	}

	out->block_size = SMM_BLOCK_SIZE;
	out->num_blocks = region_device_sz(&store) / SMM_BLOCK_SIZE;

	/* FIXME: Broken edk2 always assumes memory mapped Firmware Block Volumes */
	out->mmap_addr = (uintptr_t)rdev_mmap_full(&store);

	printk(BIOS_DEBUG, "smm store: %d # blocks with size 0x%x\n",
	       out->num_blocks, out->block_size);

	return 0;
}
#endif

/* Returns -1 on error, 0 on success */
static int lookup_block_in_store(struct region_device *store, uint32_t block_id,
				 size_t *physical_offset)
{
	if (lookup_store(store, physical_offset) < 0) {
		printk(BIOS_ERR, "smm store: lookup of store failed\n");
		return -1;
	}

	if (block_id >= region_device_sz(store) / SMM_BLOCK_SIZE) {
		printk(BIOS_ERR, "smm store: block ID out of range\n");
		return -1;
	}

	return 0;
}

static int validate_block_range(uint32_t offset, uint32_t bufsize)
{
	if (offset > SMM_BLOCK_SIZE || bufsize > SMM_BLOCK_SIZE - offset) {
		printk(BIOS_ERR, "smm store: block range out of bounds\n");
		return -1;
	}

	return 0;
}

/* Returns NULL on error, pointer from rdev_mmap on success */
static void *mmap_com_buf(struct region_device *com_buf, uint32_t offset, uint32_t bufsize)
{
	if (smmstore_rdev_chain(com_buf) < 0) {
		printk(BIOS_ERR, "smm store: lookup of com buffer failed\n");
		return NULL;
	}

	if (offset > region_device_sz(com_buf) ||
	    bufsize > region_device_sz(com_buf) - offset) {
		printk(BIOS_ERR, "smm store: offset out of range\n");
		return NULL;
	}

	void *ptr = rdev_mmap(com_buf, offset, bufsize);
	if (!ptr)
		printk(BIOS_ERR, "smm store: not enough space for new data\n");

	return ptr;
}

/**
 * Reads the specified block of the SMMSTORE and places it in the communication
 * buffer.
 * @param block_id The id of the block to operate on
 * @param offset Offset within the block.
 *               Must be smaller than the block size.
 * @param bufsize Size of chunk to read within the block.
 *                Must be smaller than the block size.

 * @return Returns -1 on error, 0 on success.
 */
int smmstore_rawread_region(uint32_t block_id, uint32_t offset, uint32_t bufsize)
{
	struct region_device store;
	struct region_device com_buf;

	if (lookup_block_in_store(&store, block_id, NULL) < 0)
		return -1;
	if (validate_block_range(offset, bufsize) < 0)
		return -1;

	void *ptr = mmap_com_buf(&com_buf, offset, bufsize);
	if (!ptr)
		return -1;

	printk(BIOS_DEBUG, "smm store: reading %p block %d, offset=0x%x, size=%x\n",
	       ptr, block_id, offset, bufsize);

	ssize_t ret = rdev_readat(&store, ptr, block_id * SMM_BLOCK_SIZE + offset, bufsize);
	rdev_munmap(&com_buf, ptr);
	if (ret != bufsize)
		return -1;

	return 0;
}

/**
 * Writes the specified block of the SMMSTORE by reading it from the communication
 * buffer.
 * @param block_id The id of the block to operate on
 * @param offset Offset within the block.
 *               Must be smaller than the block size.
 * @param bufsize Size of chunk to read within the block.
 *                Must be smaller than the block size.

 * @return Returns -1 on error, 0 on success.
 */
int smmstore_rawwrite_region(uint32_t block_id, uint32_t offset, uint32_t bufsize)
{
	struct region_device store;
	size_t physical_offset;
	struct region_device com_buf;

	if (lookup_block_in_store(&store, block_id, &physical_offset) < 0)
		return -1;
	if (validate_block_range(offset, bufsize) < 0)
		return -1;

#if CONFIG(SMMSTORE_SRK_PIN)
	if (raw_write_protected(physical_offset, (size_t)block_id * SMM_BLOCK_SIZE + offset, bufsize))
		return -1;
#endif

	if (rdev_chain(&store, &store, (size_t)block_id * SMM_BLOCK_SIZE + offset,
			       bufsize)) {
		printk(BIOS_ERR, "smm store: not enough space for new data\n");
		return -1;
	}

	void *ptr = mmap_com_buf(&com_buf, offset, bufsize);
	if (!ptr)
		return -1;

	printk(BIOS_DEBUG, "smm store: writing %p block %d, offset=0x%x, size=%x\n",
	       ptr, block_id, offset, bufsize);

	ssize_t ret = rdev_writeat(&store, ptr, 0, bufsize);
	rdev_munmap(&com_buf, ptr);
	if (ret != bufsize)
		return -1;

	return 0;
}

/**
 * Erases the specified block of the SMMSTORE. The communication buffer remains untouched.
 *
 * @param block_id The id of the block to operate on
 *
 * @return Returns -1 on error, 0 on success.
 */
int smmstore_rawclear_region(uint32_t block_id)
{
	struct region_device store;
	size_t physical_offset;

	if (lookup_block_in_store(&store, block_id, &physical_offset) < 0)
		return -1;

#if CONFIG(SMMSTORE_SRK_PIN)
	if (raw_write_protected(physical_offset, (size_t)block_id * SMM_BLOCK_SIZE, SMM_BLOCK_SIZE))
		return -1;
#endif

	ssize_t ret = rdev_eraseat(&store, block_id * SMM_BLOCK_SIZE, SMM_BLOCK_SIZE);
	if (ret != SMM_BLOCK_SIZE) {
		printk(BIOS_ERR, "smm store: erasing block failed\n");
		return -1;
	}

	return 0;
}
