/* SPDX-License-Identifier: GPL-2.0-only */
#define main prefix_model_main
#include "payload_spi_native_prefix_test.c"
#undef main
#define QEMU_PFLASH_BASE ((uintptr_t)media)
#include "../../src/mainboard/emulation/qemu-i440fx/rom_media.c"

enum command_mode { ARRAY, STATUS, PROGRAM, ERASE };
static enum command_mode command;
static uint8_t status_value;
static size_t programmed_bytes, erased_blocks;

static size_t native_offset(const volatile void *address)
{
	uintptr_t value = (uintptr_t)address;
	assert(value >= (uintptr_t)media && value < (uintptr_t)media + sizeof(media));
	return value - (uintptr_t)media;
}

uint8_t native_flash_read8(const volatile void *address)
{
	return command == STATUS ? status_value : media[native_offset(address)];
}

void native_flash_write8(volatile void *address, uint8_t value)
{
	size_t position = native_offset(address);
	if (command == PROGRAM) {
		media[position] = value;
		programmed_bytes++;
		status_value = READY_STATUS;
		command = STATUS;
		return;
	}
	if (command == ERASE) {
		assert(value == BLOCK_ERASE_CONFIRM_CMD);
		memset(media + position, 0xff, QEMU_FLASH_BLOCK_SIZE);
		erased_blocks++;
		status_value = READY_STATUS;
		command = ARRAY;
		return;
	}
	if (value == WRITE_BYTE_CMD) command = PROGRAM;
	else if (value == BLOCK_ERASE_CMD) command = ERASE;
	else if (value == READ_ARRAY_CMD) command = ARRAY;
	else if (value == READ_STATUS_CMD) command = STATUS;
	else if (value == CLEAR_STATUS_CMD) { command = STATUS; status_value = 0; }
}

int main(void)
{
	struct qemu_pflash_lease original = {0};
	static uint8_t before[sizeof(media)];
	memset(media, 0xa5, sizeof(media));
	reset_boot();
	boot_device_init();
	admitted_media = boot_device_rw();
	assert(qemu_flash_media == QEMU_FLASH_PFLASH);
	memcpy(before, media, sizeof(media));
	size_t original_programs = programmed_bytes;
	assert(qemu_pflash_lease_begin(admitted_media, &original) == 0);
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_IO_ERROR);
	assert(programmed_bytes == original_programs && erased_blocks == 0);
	assert(!memcmp(before, media, sizeof(media)));
	assert(qemu_pflash_lease_sync(admitted_media, &original) == 0);
	assert(qemu_pflash_lease_end(&original) == 0);
	/* A poisoned initialization cannot erase on a same-boot retry. */
	request.header.status = PAYLOAD_SPI_CONSOLE_PENDING;
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_IO_ERROR && erased_blocks == 0);
	assert(!memcmp(before, media, sizeof(media)));
	reset_boot();
	payload_spi_console_smi();
	assert(request.header.status == PAYLOAD_SPI_CONSOLE_SUCCESS);
	assert(erased_blocks == 0x20000 / QEMU_FLASH_BLOCK_SIZE);
	assert(!memcmp(media + 0x80000, current_prefix, sizeof(current_prefix) - 1));
	assert(!memcmp(media + 0x80000 + sizeof(current_prefix) - 1, record, sizeof(record) - 1));
	assert_outside_preserved();
	puts("PASS actual rom_media peer guard refuses console under original lease, no MMIO/erase/write, original owner intact");
	puts("PASS released owner allows complete prefix through actual SMM flash/region/MMIO command model");
	return 0;
}
