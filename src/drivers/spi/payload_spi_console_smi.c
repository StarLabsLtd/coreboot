/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/flash.h>
#include <console/payload_spi_console.h>
#include <cpu/x86/smm.h>

static size_t remaining = PAYLOAD_SPI_CONSOLE_BOOT_LIMIT;
static bool busy;

static void write_flash_console(const uint8_t *data, size_t length,
	void *context)
{
	(void)context;
	while (length--)
		flashconsole_tx_byte(*data++);
	flashconsole_tx_flush();
}

void payload_spi_console_smi(void)
{
	uintptr_t base;
	size_t size;

	smm_get_payload_spi_console_buffer(&base, &size);
	if (!base || size < sizeof(struct payload_spi_console_request))
		return;
	payload_spi_console_process((void *)base, size, &remaining, &busy,
		write_flash_console, NULL);
}
