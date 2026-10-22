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
static bool security_closed;

#if CONFIG(DAVE_MODE) && !defined(FMAP_SECTION_SRK_PIN_V1_START)
#error "DAVE_MODE requires a dedicated SRK_PIN_V1 flash region"
#endif

#if CONFIG(DAVE_MODE) && (!CONFIG(BOOTMEDIA_SMM_BWP) || \
			 CONFIG(BOOTMEDIA_SMM_BWP_RUNTIME_OPTION))
#error "DAVE_MODE requires fixed SMM-only SPI write protection"
#endif

#if defined(FMAP_SECTION_SRK_PIN_V1_START)
_Static_assert(IS_ALIGNED(FMAP_SECTION_SRK_PIN_V1_START, SMM_BLOCK_SIZE),
	       "SRK pin must start at an erase-block boundary");
_Static_assert(FMAP_SECTION_SRK_PIN_V1_SIZE == SMM_BLOCK_SIZE,
	       "SRK pin must occupy one dedicated erase block");
_Static_assert((uint64_t)FMAP_SECTION_SRK_PIN_V1_START + FMAP_SECTION_SRK_PIN_V1_SIZE <=
	       CONFIG_ROM_SIZE && CONFIG_ROM_SIZE <= UINT32_MAX,
	       "SRK pin interface requires 32-bit flash offsets");
_Static_assert(ALIGN_UP((uint64_t)FMAP_SECTION_FMAP_START + FMAP_SECTION_FMAP_SIZE,
			SMM_BLOCK_SIZE) <= CONFIG_ROM_SIZE,
	       "FMAP erase blocks must fit the flash");

static void srk_regions(struct region *pin, struct region *fmap)
{
	*pin = (struct region) {
		.offset = FMAP_SECTION_SRK_PIN_V1_START,
		.size = FMAP_SECTION_SRK_PIN_V1_SIZE,
	};
	*fmap = (struct region) {
		.offset = ALIGN_DOWN(FMAP_SECTION_FMAP_START, SMM_BLOCK_SIZE),
		.size = ALIGN_UP((uint64_t)FMAP_SECTION_FMAP_START + FMAP_SECTION_FMAP_SIZE,
				 SMM_BLOCK_SIZE) - ALIGN_DOWN(FMAP_SECTION_FMAP_START,
							    SMM_BLOCK_SIZE),
	};
}

_Static_assert(FMAP_SECTION_SRK_PIN_V1_START + (uint64_t)FMAP_SECTION_SRK_PIN_V1_SIZE <=
	       ALIGN_DOWN(FMAP_SECTION_FMAP_START, SMM_BLOCK_SIZE) ||
	       FMAP_SECTION_SRK_PIN_V1_START >=
	       ALIGN_UP((uint64_t)FMAP_SECTION_FMAP_START + FMAP_SECTION_FMAP_SIZE,
			SMM_BLOCK_SIZE), "SRK pin and FMAP erase blocks overlap");
_Static_assert(FMAP_SECTION_SRK_PIN_V1_START + (uint64_t)FMAP_SECTION_SRK_PIN_V1_SIZE <=
	       FMAP_SECTION_SMMSTORE_START || FMAP_SECTION_SRK_PIN_V1_START >=
	       (uint64_t)FMAP_SECTION_SMMSTORE_START + FMAP_SECTION_SMMSTORE_SIZE,
	       "SRK pin and variable store overlap");
_Static_assert((uint64_t)FMAP_SECTION_SMMSTORE_START + FMAP_SECTION_SMMSTORE_SIZE <=
	       ALIGN_DOWN(FMAP_SECTION_FMAP_START, SMM_BLOCK_SIZE) ||
	       FMAP_SECTION_SMMSTORE_START >=
	       ALIGN_UP((uint64_t)FMAP_SECTION_FMAP_START + FMAP_SECTION_FMAP_SIZE,
			SMM_BLOCK_SIZE), "FMAP erase blocks and variable store overlap");
#endif

static bool protected_write(size_t base, size_t offset, size_t size)
{
#if defined(FMAP_SECTION_SRK_PIN_V1_START)
	struct region pin, fmap, write;
	size_t start = base + offset;
	size_t end;

	if (!size)
		return false;
	if (start < base || size > SIZE_MAX - start ||
	    start + size > SIZE_MAX - (SMM_BLOCK_SIZE - 1))
		return true;
	end = ALIGN_UP(start + size, SMM_BLOCK_SIZE);
	write = (struct region) {
		.offset = ALIGN_DOWN(start, SMM_BLOCK_SIZE),
		.size = end - ALIGN_DOWN(start, SMM_BLOCK_SIZE),
	};
	srk_regions(&pin, &fmap);
	return region_overlap(&write, &pin) || region_overlap(&write, &fmap);
#else
	return false;
#endif
}

struct smmstore_region {
	struct region_device read;
	struct region_device write;
	struct incoherent_rdev device;
};

int smmstore_preprocess_cmd(uint8_t *cmd, void *param)
{
	smmstore_use_full_flash = 0;
	if (CONFIG(SMM_AUTHVAR) && ((*cmd & SMMSTORE_CMD_USE_FULL_FLASH) ||
	    *cmd == SMMSTORE_CMD_RAW_WRITE || *cmd == SMMSTORE_CMD_RAW_CLEAR))
		return -1;
	if (security_closed && (*cmd & SMMSTORE_CMD_USE_FULL_FLASH))
		return -1;
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
		}
	}

	return 0;
}

static enum cb_err lookup_store_region(struct region *region, bool full_flash)
{
	if (full_flash) {
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
static int lookup_store(struct region_device *rstore, struct smmstore_region *store,
			bool full_flash, size_t *physical_offset)
{
	struct region region;
	const struct region_device *rdev;

	if (lookup_store_region(&region, full_flash) != CB_SUCCESS)
		return -1;
	if (physical_offset)
		*physical_offset = region.offset;

	if (boot_device_ro_subregion(&region, &store->read) < 0)
		return -1;

	if (boot_device_rw_subregion(&region, &store->write) < 0)
		return -1;

	rdev = incoherent_rdev_init(&store->device, &region, &store->read, &store->write);

	if (rdev == NULL)
		return -1;

	return rdev_chain(rstore, rdev, 0, region_device_sz(rdev));
}

 /* this function is non reentrant */
int smmstore_lookup_region(struct region_device *rstore)
{
	static int done;
	static int ret;
	static struct region_device rdev;
	/* Cached readers must not share the raw capsule command's mutable region. */
	static struct smmstore_region store;

	if (!done) {
		done = 1;

		if (lookup_store(&rdev, &store, false, NULL)) {
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
	static struct smmstore_region backing;
	struct region_device store;

	if (lookup_store(&store, &backing, false, NULL) < 0) {
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
	static struct smmstore_region backing;

	if (lookup_store(store, &backing, smmstore_use_full_flash != 0, physical_offset) < 0) {
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

#if defined(FMAP_SECTION_SRK_PIN_V1_START)
struct srk_pin_record {
	uint8_t name[34];
	uint8_t commit[8];
};

static const uint8_t srk_pin_commit[8] = { 'S', 'R', 'K', 'P', 'I', 'N', '1', 0 };

static bool srk_pin_erased(const struct region_device *pin)
{
	uint8_t bytes[64];

	for (size_t offset = 0; offset < region_device_sz(pin); offset += sizeof(bytes)) {
		if (rdev_readat(pin, bytes, offset, sizeof(bytes)) != sizeof(bytes))
			return false;
		for (size_t i = 0; i < sizeof(bytes); i++)
			if (bytes[i] != 0xff)
				return false;
	}
	return true;
}

static uint32_t read_srk_record(struct region_device *pin, struct srk_pin_record *record)
{
	struct region pin_region, fmap_region;

	srk_regions(&pin_region, &fmap_region);
	/* Read back through the flash device, not an incoherent memory mapping. */
	if (boot_device_rw_subregion(&pin_region, pin) < 0 ||
	    rdev_readat(pin, record, 0, sizeof(*record)) != sizeof(*record))
		return SMMSTORE_RET_FAILURE;
	if (memcmp(record->commit, srk_pin_commit, sizeof(record->commit)) ||
	    record->name[0] != 0 || record->name[1] != 0x0b)
		return srk_pin_erased(pin) ? SMMSTORE_RET_NOT_FOUND : SMMSTORE_RET_FAILURE;
	return SMMSTORE_RET_SUCCESS;
}
#endif

uint32_t smmstore_read_srk_pin(void)
{
#if defined(FMAP_SECTION_SRK_PIN_V1_START)
	struct smmstore_srk_info info = { 0 };
	struct srk_pin_record record;
	struct region_device pin;
	struct region pin_region, fmap_region;
	uint32_t status = read_srk_record(&pin, &record);

	if (status != SMMSTORE_RET_SUCCESS && status != SMMSTORE_RET_NOT_FOUND)
		return status;
	srk_regions(&pin_region, &fmap_region);
	info.revision = 1;
	info.offset = pin_region.offset;
	info.size = pin_region.size;
	info.fmap_offset = fmap_region.offset;
	info.fmap_size = fmap_region.size;
	if (status == SMMSTORE_RET_SUCCESS)
		memcpy(info.name, record.name, sizeof(info.name));
	if (rdev_writeat(&mdev_com_buf, &info, 0, sizeof(info)) != sizeof(info))
		return SMMSTORE_RET_FAILURE;
	return status;
#else
	return SMMSTORE_RET_UNSUPPORTED;
#endif
}

uint32_t smmstore_create_srk_pin(void)
{
#if defined(FMAP_SECTION_SRK_PIN_V1_START)
	struct srk_pin_record record;
	struct region_device pin;
	uint8_t name[34], readback[34];
	uint32_t status;

	if (!CONFIG(DAVE_FACTORY_PROVISIONING) || security_closed)
		return SMMSTORE_RET_DENIED;
	if (rdev_readat(&mdev_com_buf, name, 0, sizeof(name)) != sizeof(name) ||
	    name[0] != 0 || name[1] != 0x0b)
		return SMMSTORE_RET_FAILURE;
	status = read_srk_record(&pin, &record);
	if (status != SMMSTORE_RET_NOT_FOUND)
		return status == SMMSTORE_RET_SUCCESS ? SMMSTORE_RET_DENIED : status;
	/* A torn provisioning write burns the slot; only office recovery may erase it. */
	if (rdev_writeat(&pin, name, 0, sizeof(name)) != sizeof(name) ||
	    rdev_readat(&pin, readback, 0, sizeof(readback)) != sizeof(readback) ||
	    memcmp(name, readback, sizeof(name)) ||
	    rdev_writeat(&pin, srk_pin_commit, offsetof(struct srk_pin_record, commit),
			 sizeof(srk_pin_commit)) != sizeof(srk_pin_commit))
		return SMMSTORE_RET_FAILURE;
	status = read_srk_record(&pin, &record);
	if (status != SMMSTORE_RET_SUCCESS || memcmp(record.name, name, sizeof(name)))
		return SMMSTORE_RET_FAILURE;
	return SMMSTORE_RET_SUCCESS;
#else
	return SMMSTORE_RET_UNSUPPORTED;
#endif
}

uint32_t smmstore_close_security(void)
{
#if defined(FMAP_SECTION_SRK_PIN_V1_START)
	security_closed = true;
	smmstore_use_full_flash = 0;
	has_capsules = 0;
	return SMMSTORE_RET_SUCCESS;
#else
	return SMMSTORE_RET_UNSUPPORTED;
#endif
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
	struct region_device com_buf;
	size_t physical_offset;

	if (lookup_block_in_store(&store, block_id, &physical_offset) < 0)
		return -1;
	if (validate_block_range(offset, bufsize) < 0)
		return -1;
	if (protected_write(physical_offset, (size_t)block_id * SMM_BLOCK_SIZE + offset,
			    bufsize))
		return -1;

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
	if (protected_write(physical_offset, (size_t)block_id * SMM_BLOCK_SIZE, SMM_BLOCK_SIZE))
		return -1;

	ssize_t ret = rdev_eraseat(&store, block_id * SMM_BLOCK_SIZE, SMM_BLOCK_SIZE);
	if (ret != SMM_BLOCK_SIZE) {
		printk(BIOS_ERR, "smm store: erasing block failed\n");
		return -1;
	}

	return 0;
}
