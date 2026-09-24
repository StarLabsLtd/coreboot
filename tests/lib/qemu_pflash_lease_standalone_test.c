/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TEST_FLASH_SIZE (64 * 1024)

uint8_t qemu_pflash_test_region[TEST_FLASH_SIZE];

uint8_t qemu_pflash_test_read8(const volatile void *address);
void qemu_pflash_test_write8(volatile void *address, uint8_t value);

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		abort();
}

#include "../../src/mainboard/emulation/qemu-i440fx/rom_media.c"

enum mock_mode {
	MOCK_ARRAY,
	MOCK_STATUS,
	MOCK_PROGRAM,
	MOCK_ERASE_CONFIRM,
};

static enum mock_mode mock_mode;
static uint8_t mock_status;
static uint8_t injected_status_error;
static uint32_t status_delay_reads;
static uint32_t array_restore_count;
static bool never_ready;
static volatile uint32_t reenter;
static struct qemu_pflash_lease *reenter_owner;

static size_t address_offset(const volatile void *address)
{
	uintptr_t value = (uintptr_t)address;
	uintptr_t base = (uintptr_t)qemu_pflash_test_region;

	assert(value >= base && value < base + sizeof(qemu_pflash_test_region));
	return value - base;
}

uint8_t qemu_pflash_test_read8(const volatile void *address)
{
	if (mock_mode == MOCK_STATUS)
		return never_ready ||
			(status_delay_reads && status_delay_reads--) ? 0 : mock_status;
	return qemu_pflash_test_region[address_offset(address)];
}

void qemu_pflash_test_write8(volatile void *address, uint8_t value)
{
	size_t offset = address_offset(address);

	if (__atomic_exchange_n(&reenter, 0, __ATOMIC_ACQ_REL))
		assert(qemu_pflash_lease_sync(boot_device_rw(), reenter_owner) < 0);
	switch (mock_mode) {
	case MOCK_PROGRAM:
		qemu_pflash_test_region[offset] = value;
		mock_status = READY_STATUS | injected_status_error;
		mock_mode = MOCK_ARRAY;
		return;
	case MOCK_ERASE_CONFIRM:
		if (value == BLOCK_ERASE_CONFIRM_CMD)
			memset(&qemu_pflash_test_region[offset], 0xff,
				QEMU_FLASH_BLOCK_SIZE);
		else
			mock_status |= ERASE_ERROR_STATUS;
		mock_status = READY_STATUS | injected_status_error;
		mock_mode = MOCK_ARRAY;
		return;
	default:
		break;
	}
	if (value == WRITE_BYTE_CMD)
		mock_mode = MOCK_PROGRAM;
	else if (value == BLOCK_ERASE_CMD)
		mock_mode = MOCK_ERASE_CONFIRM;
	else if (value == CLEAR_STATUS_CMD) {
		mock_status = CLEARED_ARRAY_STATUS;
		mock_mode = MOCK_STATUS;
	} else if (value == READ_STATUS_CMD)
		mock_mode = MOCK_STATUS;
	else if (value == READ_ARRAY_CMD) {
		array_restore_count++;
		mock_mode = MOCK_ARRAY;
	}
}

static void reset_flash(void)
{
	size_t index;

	for (index = 0; index < sizeof(qemu_pflash_test_region); index++)
		qemu_pflash_test_region[index] = (uint8_t)(index * 13U + 3U);
	mock_mode = MOCK_ARRAY;
	mock_status = 0;
	injected_status_error = 0;
	status_delay_reads = 0;
	array_restore_count = 0;
	never_ready = false;
	reenter = 0;
	reenter_owner = NULL;
}

static void normal_session(void)
{
	const struct region_device *root = boot_device_rw();
	struct qemu_pflash_lease owner = { 0 };
	struct qemu_pflash_lease second = { 0 };
	uint8_t program[5] = { 0xa1, 0xb2, 0xc3, 0xd4, 0xe5 };
	uint8_t readback[sizeof(program)] = { 0 };
	uint8_t peer = 0x55;
	size_t index;

	assert(qemu_pflash_lease_begin(root, &owner) == 0);
	assert(qemu_pflash_lease_begin(root, &second) < 0);
	assert(rdev_writeat(root, &peer, 0x20, 1) < 0);
	assert(rdev_eraseat(root, 0, QEMU_FLASH_BLOCK_SIZE) < 0);
	assert(qemu_pflash_lease_read(root, &owner, 0x20, readback,
		sizeof(readback)) == 0);
	assert(qemu_pflash_lease_program(root, &owner, 0x20, program,
		sizeof(program)) == 0);
	assert(!memcmp(&qemu_pflash_test_region[0x20], program, sizeof(program)));
	assert(qemu_pflash_lease_sync(root, &owner) == 0);
	assert(qemu_pflash_lease_erase(root, &owner, QEMU_FLASH_BLOCK_SIZE,
		QEMU_FLASH_BLOCK_SIZE) == 0);
	for (index = QEMU_FLASH_BLOCK_SIZE; index < 2 * QEMU_FLASH_BLOCK_SIZE;
	     index++)
		assert(qemu_pflash_test_region[index] == 0xff);
	assert(qemu_pflash_lease_end(&owner) == 0);
	assert(rdev_writeat(root, &peer, 0x20, 1) == 1);
}

static void hostile_session(void)
{
	const struct region_device *root = boot_device_rw();
	struct qemu_pflash_lease owner = { 0 };
	struct qemu_pflash_lease forged;
	uint8_t byte = 0x5a;
	ssize_t (*saved_write)(const struct region_device *root,
		const void *buffer, size_t offset, size_t size);

	assert(qemu_pflash_lease_begin(root, &owner) == 0);
	forged = owner;
	assert(qemu_pflash_lease_program(root, &forged, 0, &byte, 1) < 0);
	assert(qemu_pflash_lease_sync(root, &owner) == 0);
	owner.private_data[1] ^= 1;
	assert(qemu_pflash_lease_sync(root, &owner) < 0);
	assert(qemu_pflash_lease_end(&owner) < 0);

	owner = (struct qemu_pflash_lease){ 0 };
	assert(qemu_pflash_lease_begin(root, &owner) == 0);
	saved_write = flash_ops.writeat;
	flash_ops.writeat = NULL;
	assert(qemu_pflash_lease_sync(root, &owner) < 0);
	flash_ops.writeat = saved_write;
	assert(qemu_pflash_lease_end(&owner) < 0);

	owner = (struct qemu_pflash_lease){ 0 };
	assert(qemu_pflash_lease_begin(root, &owner) == 0);
	reenter_owner = &owner;
	__atomic_store_n(&reenter, 1, __ATOMIC_RELEASE);
	assert(qemu_pflash_lease_program(root, &owner, 8, &byte, 1) < 0);
	assert(qemu_pflash_lease_end(&owner) < 0);
}

static void bounds_session(void)
{
	const struct region_device *root = boot_device_rw();
	struct qemu_pflash_lease owner = { 0 };
	uint8_t byte = 0;

	assert(qemu_pflash_lease_begin(root, &owner) == 0);
	assert(qemu_pflash_lease_read(root, &owner, SIZE_MAX, &byte, 1) < 0);
	assert(qemu_pflash_lease_read(root, &owner, TEST_FLASH_SIZE, &byte, 1) < 0);
	assert(qemu_pflash_lease_read(root, &owner, 0, &byte, 0) < 0);
	assert(qemu_pflash_lease_erase(root, &owner, 1,
		QEMU_FLASH_BLOCK_SIZE) < 0);
	assert(qemu_pflash_lease_erase(root, &owner, 0,
		QEMU_FLASH_BLOCK_SIZE - 1) < 0);
	assert(qemu_pflash_lease_end(&owner) == 0);
}

static void status_session(void)
{
	const struct region_device *root = boot_device_rw();
	struct qemu_pflash_lease owner = { 0 };
	uint8_t byte = 0x9a;
	uint32_t restores;

	assert(qemu_pflash_lease_begin(root, &owner) == 0);
	status_delay_reads = 3;
	restores = array_restore_count;
	assert(qemu_pflash_lease_program(root, &owner, 0x40, &byte, 1) == 0);
	assert(array_restore_count == restores + 2);

	injected_status_error = PROGRAM_ERROR_STATUS;
	restores = array_restore_count;
	assert(qemu_pflash_lease_program(root, &owner, 0x41, &byte, 1) < 0);
	assert(array_restore_count == restores + 2);
	injected_status_error = ERASE_ERROR_STATUS;
	restores = array_restore_count;
	assert(qemu_pflash_lease_erase(root, &owner, 2 * QEMU_FLASH_BLOCK_SIZE,
		QEMU_FLASH_BLOCK_SIZE) < 0);
	assert(array_restore_count == restores + 2);

	injected_status_error = 0;
	never_ready = true;
	restores = array_restore_count;
	assert(qemu_pflash_lease_sync(root, &owner) < 0);
	assert(array_restore_count == restores + 1);
	never_ready = false;
	mock_status = READY_STATUS;
	assert(qemu_pflash_lease_end(&owner) == 0);
}

static void *lease_thread(void *unused)
{
	struct qemu_pflash_lease owner = { 0 };
	const struct region_device *root = boot_device_rw();

	(void)unused;
	if (!qemu_pflash_lease_begin(root, &owner))
		assert(qemu_pflash_lease_end(&owner) == 0);
	return NULL;
}

static void concurrency_session(void)
{
	pthread_t first;
	pthread_t second;

	assert(!pthread_create(&first, NULL, lease_thread, NULL));
	assert(!pthread_create(&second, NULL, lease_thread, NULL));
	assert(!pthread_join(first, NULL));
	assert(!pthread_join(second, NULL));
}

int main(void)
{
	reset_flash();
	boot_device_init();
	assert(qemu_flash_media == QEMU_FLASH_PFLASH);
	normal_session();
	hostile_session();
	bounds_session();
	status_session();
	concurrency_session();
	return 0;
}
