/* SPDX-License-Identifier: GPL-2.0-only */

/* Inspired by OvmfPkg/QemuFlashFvbServicesRuntimeDxe/QemuFlash.c from edk2 */

#include <arch/mmio.h>
#include <boot_device.h>
#include <console/console.h>
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <emulation/qemu_pflash.h>
#include <string.h>

#define WRITE_BYTE_CMD		0x10
#define BLOCK_ERASE_CMD		0x20
#define CLEAR_STATUS_CMD	0x50
#define READ_STATUS_CMD		0x70
#define BLOCK_ERASE_CONFIRM_CMD	0xD0
#define READ_ARRAY_CMD		0xFF

#define CLEARED_ARRAY_STATUS	0x00
#define READY_STATUS		0x80
#define PROGRAM_ERROR_STATUS	0x10
#define ERASE_ERROR_STATUS	0x20
#define STATUS_POLL_LIMIT	100000U

#define QEMU_FLASH_BLOCK_SIZE	0x1000

#define QEMU_PFLASH_LEASE_CHECK_DOMAIN ((uintptr_t)0xc75b942d6e1083a7ULL)

#ifndef QEMU_PFLASH_BASE
#define QEMU_PFLASH_BASE (0x100000000ULL - CONFIG_ROM_SIZE)
#endif

#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
enum qemu_flash_media_type {
	QEMU_FLASH_UNKNOWN = 0,
	QEMU_FLASH_ROM,
	QEMU_FLASH_RAM,
	QEMU_FLASH_PFLASH,
};

enum qemu_pflash_lease_state {
	QEMU_PFLASH_LEASE_IDLE = 0,
	QEMU_PFLASH_LEASE_ACTIVE,
	QEMU_PFLASH_LEASE_BUSY,
	QEMU_PFLASH_LEASE_POISONED,
};

static enum qemu_flash_media_type qemu_flash_media;
#endif

#if CONFIG(ELOG)
#include <southbridge/intel/common/pmutil.h>

/*
 * ELOG and VBOOT options are automatically enabled when building with
 * CHROMEOS=y. While the former allows for logging PCH state (not that there is
 * much to log on QEMU), the latter currently forces 16 MiB ROM size, which in
 * turn doesn't allow mounting as pflash in QEMU. Using pflash is required to
 * have writable flash, which means that the following function will not be
 * able to write to the flash based log until ROM size and layout is changed in
 * Flashmap used when building for vboot.
 */
void pch_log_state(void) {}
#endif

#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
static ssize_t qemu_writeat_unlocked(const struct region_device *rd, const void *b,
#else
static ssize_t qemu_writeat(const struct region_device *rd, const void *b,
#endif
				size_t offset, size_t size)
{
	const struct mem_region_device *mdev;
	size_t i;
	volatile char *ptr;
	const char *buf = b;

	mdev = container_of(rd, typeof(*mdev), rdev);
	ptr = &mdev->base[offset];

	for (i = 0; i < size; i++) {
		write8(ptr, WRITE_BYTE_CMD);
		write8(ptr, buf[i]);
		ptr++;
	}

	/* Restore flash to read mode. */
	if (size > 0) {
		write8(ptr - 1, READ_ARRAY_CMD);
	}

	return size;
}

#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
static ssize_t qemu_eraseat_unlocked(const struct region_device *rd, size_t offset,
#else
static ssize_t qemu_eraseat(const struct region_device *rd, size_t offset,
#endif
				size_t size)
{
	const struct mem_region_device *mdev;
	size_t i;
	volatile char *ptr;

	mdev = container_of(rd, typeof(*mdev), rdev);
	ptr = &mdev->base[offset];

	if (!IS_ALIGNED(offset, QEMU_FLASH_BLOCK_SIZE)) {
		printk(BIOS_ERR, "%s: erased offset isn't multiple of block size\n",
		       __func__);
		return -1;
	}

	if (!IS_ALIGNED(size, QEMU_FLASH_BLOCK_SIZE)) {
		printk(BIOS_ERR, "%s: erased size isn't multiple of block size\n",
		       __func__);
		return -1;
	}

	for (i = 0; i < size; i += QEMU_FLASH_BLOCK_SIZE) {
		write8(ptr, BLOCK_ERASE_CMD);
		write8(ptr, BLOCK_ERASE_CONFIRM_CMD);
		ptr += QEMU_FLASH_BLOCK_SIZE;
	}

	/* Restore flash to read mode. */
	if (size > 0) {
		write8(ptr - QEMU_FLASH_BLOCK_SIZE, READ_ARRAY_CMD);
	}

	return size;
}

static struct region_device_ops flash_ops;
static const struct mem_region_device boot_dev =
	MEM_REGION_DEV_INIT(QEMU_PFLASH_BASE, CONFIG_ROM_SIZE, &flash_ops);

#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
static struct {
	uint32_t lock;
	uint32_t peer_count;
	bool boundary;
	enum qemu_pflash_lease_state state;
	uintptr_t next_cookie;
	uintptr_t cookie;
	const struct region_device *root;
	const struct region_device_ops *ops;
	const struct qemu_pflash_lease *owner;
	ssize_t (*readat)(const struct region_device *root, void *buffer,
		size_t offset, size_t size);
	ssize_t (*writeat)(const struct region_device *root, const void *buffer,
		size_t offset, size_t size);
	ssize_t (*eraseat)(const struct region_device *root, size_t offset,
		size_t size);
} pflash_lease;

static bool lease_lock(void)
{
	uint32_t expected = 0;

	return __atomic_compare_exchange_n(&pflash_lease.lock, &expected, 1,
		false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void lease_unlock(void)
{
	__atomic_store_n(&pflash_lease.lock, 0, __ATOMIC_RELEASE);
}

static void lease_lock_wait(void)
{
	while (!lease_lock())
		;
}

static bool lease_handle_valid(const struct qemu_pflash_lease *lease)
{
	return lease == pflash_lease.owner &&
		lease->private_data[0] == pflash_lease.cookie &&
		lease->private_data[1] ==
			(pflash_lease.cookie ^ QEMU_PFLASH_LEASE_CHECK_DOMAIN) &&
		lease->private_data[2] == (uintptr_t)pflash_lease.root &&
		lease->private_data[3] == ((uintptr_t)lease ^ pflash_lease.cookie ^
			QEMU_PFLASH_LEASE_CHECK_DOMAIN);
}

static bool lease_callbacks_valid(void)
{
	return qemu_flash_media == QEMU_FLASH_PFLASH &&
		pflash_lease.root == &boot_dev.rdev &&
		pflash_lease.root->ops == pflash_lease.ops &&
		pflash_lease.ops == &flash_ops &&
		pflash_lease.ops->readat == pflash_lease.readat &&
		pflash_lease.ops->writeat == pflash_lease.writeat &&
		pflash_lease.ops->eraseat == pflash_lease.eraseat;
}

static bool lease_span_valid(const struct region_device *root,
	size_t offset, size_t size)
{
	return root == &boot_dev.rdev && size &&
		offset <= region_device_sz(root) &&
		size <= region_device_sz(root) - offset;
}

static int peer_begin(const struct region_device *root)
{
	int result = -1;

	if (!lease_lock())
		return result;
	if (root == &boot_dev.rdev && !pflash_lease.boundary &&
	    pflash_lease.state == QEMU_PFLASH_LEASE_IDLE &&
	    pflash_lease.peer_count != UINT32_MAX) {
		pflash_lease.peer_count++;
		result = 0;
	}
	lease_unlock();
	return result;
}

static int peer_end(void)
{
	int result = -1;

	lease_lock_wait();
	if (pflash_lease.peer_count) {
		pflash_lease.peer_count--;
		result = 0;
	}
	lease_unlock();
	return result;
}

static int lease_enter(const struct region_device *root,
	const struct qemu_pflash_lease *lease)
{
	if (!lease_lock())
		return -1;
	if (pflash_lease.state != QEMU_PFLASH_LEASE_ACTIVE ||
	    root != pflash_lease.root || !lease_handle_valid(lease) ||
	    !lease_callbacks_valid()) {
		if (lease == pflash_lease.owner)
			pflash_lease.state = QEMU_PFLASH_LEASE_POISONED;
		lease_unlock();
		return -1;
	}
	pflash_lease.state = QEMU_PFLASH_LEASE_BUSY;
	lease_unlock();
	return 0;
}

static int lease_leave(int callback_result)
{
	lease_lock_wait();
	if (pflash_lease.state != QEMU_PFLASH_LEASE_BUSY ||
	    !lease_handle_valid(pflash_lease.owner) ||
	    !lease_callbacks_valid()) {
		pflash_lease.state = QEMU_PFLASH_LEASE_POISONED;
		callback_result = -1;
	} else {
		pflash_lease.state = QEMU_PFLASH_LEASE_ACTIVE;
	}
	lease_unlock();
	return callback_result;
}
#endif

#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
static ssize_t qemu_writeat(const struct region_device *rd, const void *buffer,
	size_t offset, size_t size)
{
	ssize_t result;

	if (peer_begin(rd))
		return -1;
	result = qemu_writeat_unlocked(rd, buffer, offset, size);
	if (peer_end())
		return -1;
	return result;
}

static ssize_t qemu_eraseat(const struct region_device *rd, size_t offset,
	size_t size)
{
	ssize_t result;

	if (peer_begin(rd))
		return -1;
	result = qemu_eraseat_unlocked(rd, offset, size);
	if (peer_end())
		return -1;
	return result;
}
#endif

/*
 * Depending on how firmware image was passed to QEMU, it may behave as:
 *
 * - ROM - memory mapped reads, writes are ignored (FW image mounted with
 *   '-bios');
 * - RAM - memory mapped reads and writes (FW image mounted with e.g.
 *   '-device loader');
 * - flash - memory mapped reads, write and erase possible through commands.
 *   Contrary to physical flash devices erase is not required before writing,
 *   but it also doesn't hurt. Flash may be split into read-only and read-write
 *   parts, like OVMF_CODE.fd and OVMF_VARS.fd. Maximal combined size of system
 *   firmware is hardcoded (QEMU < 5.0.0) or set by default to 8 MiB. On QEMU
 *   version 5.0.0 or newer, it is configurable with `max-fw-size` machine
 *   configuration option, up to 16 MiB to not overlap with IOAPIC memory range
 *   (FW image(s) mounted with '-drive if=pflash').
 *
 * This function detects which of the above applies and fills region_device_ops
 * accordingly.
 */
void boot_device_init(void)
{
	volatile char *ptr;
	char original, readback;
	static bool initialized = false;

	if (initialized)
		return;

	/*
	 * mmap, munmap and readat are always identical to mem_rdev_rw_ops, other
	 * functions may vary.
	 */
	flash_ops = mem_rdev_rw_ops;

	/*
	 * Find first byte different than any of the commands, simplified.
	 *
	 * Detection code few lines below writes commands and tries to read back
	 * the response. To make that code simpler, make sure that original byte
	 * is different than any of the commands or expected responses. It is
	 * expected that such byte will always be found - it is virtually
	 * impossible to write valid x86 code with just bytes ending with 0, and
	 * there are also ASCII characters in metadata (CBFS, FMAP) that has bytes
	 * matching those assumptions.
	 */
	ptr = (volatile char *)boot_dev.base;
	original = read8(ptr);
	while (original == (char)0xFF || (original & 0x0F) == 0)
		original = read8(++ptr);

	/*
	 * Detect what type of flash we're dealing with. This also clears any stale
	 * status bits, so the next read of status register should return known
	 * value (if pflash is used).
	 */
	write8(ptr, CLEAR_STATUS_CMD);
	readback = read8(ptr);
	if (readback == CLEAR_STATUS_CMD) {
		printk(BIOS_DEBUG, "QEMU flash behaves as RAM\n");
#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
		qemu_flash_media = QEMU_FLASH_RAM;
#endif
		/* Restore original content. */
		write8(ptr, original);
	} else {
		/* Either ROM or QEMU flash implementation. */
		write8(ptr, READ_STATUS_CMD);
		readback = read8(ptr);
		if (readback == original) {
			printk(BIOS_DEBUG, "QEMU flash behaves as ROM\n");
#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
			qemu_flash_media = QEMU_FLASH_ROM;
#endif
			/* ROM means no writing nor erasing. */
			flash_ops.writeat = NULL;
			flash_ops.eraseat = NULL;
		} else if (readback == CLEARED_ARRAY_STATUS) {
			/* Try writing original value to test whether flash is writable. */
			write8(ptr, WRITE_BYTE_CMD);
			write8(ptr, original);
			write8(ptr, READ_STATUS_CMD);
			readback = read8(ptr);
			if (readback & 0x10 /* programming error */) {
				printk(BIOS_DEBUG,
				       "QEMU flash behaves as write-protected flash\n");
#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
				qemu_flash_media = QEMU_FLASH_ROM;
#endif
				flash_ops.writeat = NULL;
				flash_ops.eraseat = NULL;
			} else {
				printk(BIOS_DEBUG, "QEMU flash behaves as writable flash\n");
#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
				qemu_flash_media = QEMU_FLASH_PFLASH;
#endif
				flash_ops.writeat = qemu_writeat;
				flash_ops.eraseat = qemu_eraseat;
			}
			/* Restore flash to read mode. */
			write8(ptr, READ_ARRAY_CMD);
		} else {
			printk(BIOS_ERR, "Unexpected QEMU flash behavior, assuming ROM\n");
#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
			qemu_flash_media = QEMU_FLASH_ROM;
#endif
			/*
			 * This shouldn't happen and first byte of flash may already be
			 * corrupted by testing, but don't take any further risk.
			 */
			flash_ops.writeat = NULL;
			flash_ops.eraseat = NULL;
		}
	}

	initialized = true;
}

/* boot_device_ro() is defined in arch/x86/mmap_boot.c */
const struct region_device *boot_device_rw(void)
{
	return &boot_dev.rdev;
}

#if CONFIG(QEMU_PFLASH_VOLATILE_LEASE)
int qemu_pflash_lease_begin(const struct region_device *root,
	struct qemu_pflash_lease *lease)
{
	uintptr_t cookie;

	if (!root || !lease ||
	    memcmp(lease, &(struct qemu_pflash_lease){ 0 }, sizeof(*lease)))
		return -1;
	boot_device_init();
	if (!lease_lock())
		return -1;
	if (qemu_flash_media != QEMU_FLASH_PFLASH || root != &boot_dev.rdev ||
	    root->ops != &flash_ops || flash_ops.readat == NULL ||
	    flash_ops.writeat != qemu_writeat || flash_ops.eraseat != qemu_eraseat ||
	    !IS_ALIGNED(region_device_sz(root), QEMU_FLASH_BLOCK_SIZE) ||
	    pflash_lease.state != QEMU_PFLASH_LEASE_IDLE ||
	    pflash_lease.boundary || pflash_lease.peer_count ||
	    pflash_lease.next_cookie == UINTPTR_MAX)
		goto fail;
	pflash_lease.boundary = true;
	cookie = ++pflash_lease.next_cookie;
	pflash_lease.cookie = cookie;
	pflash_lease.root = root;
	pflash_lease.ops = root->ops;
	pflash_lease.owner = lease;
	pflash_lease.readat = root->ops->readat;
	pflash_lease.writeat = root->ops->writeat;
	pflash_lease.eraseat = root->ops->eraseat;
	lease->private_data[0] = cookie;
	lease->private_data[1] = cookie ^ QEMU_PFLASH_LEASE_CHECK_DOMAIN;
	lease->private_data[2] = (uintptr_t)root;
	lease->private_data[3] = (uintptr_t)lease ^ cookie ^
		QEMU_PFLASH_LEASE_CHECK_DOMAIN;
	pflash_lease.state = QEMU_PFLASH_LEASE_ACTIVE;
	lease_unlock();
	return 0;
fail:
	lease_unlock();
	return -1;
}

int qemu_pflash_lease_read(const struct region_device *root,
	const struct qemu_pflash_lease *lease, size_t offset, void *buffer,
	size_t size)
{
	ssize_t result;

	if (!buffer || !lease_span_valid(root, offset, size) ||
	    lease_enter(root, lease))
		return -1;
	result = pflash_lease.readat(root, buffer, offset, size);
	return lease_leave(result == (ssize_t)size ? 0 : -1);
}

static int pflash_status_restore(size_t offset)
{
	volatile uint8_t *address = (volatile uint8_t *)(boot_dev.base + offset);
	uint8_t status = 0;
	unsigned int attempt;
	int result = -1;

	write8(address, READ_STATUS_CMD);
	for (attempt = 0; attempt < STATUS_POLL_LIMIT; attempt++) {
		status = read8(address);
		if (status & READY_STATUS) {
			result = status & (PROGRAM_ERROR_STATUS | ERASE_ERROR_STATUS) ?
				-1 : 0;
			break;
		}
	}
	write8(address, READ_ARRAY_CMD);
	return result;
}

int qemu_pflash_lease_program(const struct region_device *root,
	const struct qemu_pflash_lease *lease, size_t offset,
	const void *buffer, size_t size)
{
	ssize_t result;

	if (!buffer || !lease_span_valid(root, offset, size) ||
	    lease_enter(root, lease))
		return -1;
	result = qemu_writeat_unlocked(root, buffer, offset, size);
	if (result != (ssize_t)size || pflash_status_restore(offset + size - 1) ||
	    memcmp((const void *)(boot_dev.base + offset), buffer, size))
		result = -1;
	else
		result = 0;
	return lease_leave(result);
}

int qemu_pflash_lease_erase(const struct region_device *root,
	const struct qemu_pflash_lease *lease, size_t offset, size_t size)
{
	ssize_t result;
	size_t index;
	const volatile uint8_t *bytes = (const volatile uint8_t *)(boot_dev.base + offset);

	if (!lease_span_valid(root, offset, size) ||
	    !IS_ALIGNED(offset, QEMU_FLASH_BLOCK_SIZE) ||
	    !IS_ALIGNED(size, QEMU_FLASH_BLOCK_SIZE) ||
	    lease_enter(root, lease))
		return -1;
	result = qemu_eraseat_unlocked(root, offset, size);
	if (result != (ssize_t)size || pflash_status_restore(offset))
		result = -1;
	else {
		for (index = 0; index < size; index++) {
			if (read8(bytes + index) != 0xff) {
				result = -1;
				break;
			}
		}
		if (index == size)
			result = 0;
	}
	return lease_leave(result);
}

int qemu_pflash_lease_sync(const struct region_device *root,
	const struct qemu_pflash_lease *lease)
{
	int result;
	volatile uint8_t readback;

	if (lease_enter(root, lease))
		return -1;
	result = pflash_status_restore(0);
	asm volatile("" ::: "memory");
	readback = read8((const volatile void *)boot_dev.base);
	(void)readback;
	asm volatile("" ::: "memory");
	return lease_leave(result);
}

int qemu_pflash_lease_end(struct qemu_pflash_lease *lease)
{
	bool valid;

	if (!lease || !lease_lock())
		return -1;
	if (lease != pflash_lease.owner ||
	    pflash_lease.state == QEMU_PFLASH_LEASE_IDLE ||
	    pflash_lease.state == QEMU_PFLASH_LEASE_BUSY) {
		lease_unlock();
		return -1;
	}
	valid = pflash_lease.state == QEMU_PFLASH_LEASE_ACTIVE &&
		lease_handle_valid(lease) && lease_callbacks_valid();
	memset(lease, 0, sizeof(*lease));
	pflash_lease.cookie = 0;
	pflash_lease.root = NULL;
	pflash_lease.ops = NULL;
	pflash_lease.owner = NULL;
	pflash_lease.readat = NULL;
	pflash_lease.writeat = NULL;
	pflash_lease.eraseat = NULL;
	pflash_lease.state = QEMU_PFLASH_LEASE_IDLE;
	pflash_lease.boundary = false;
	lease_unlock();
	return valid ? 0 : -1;
}
#endif
