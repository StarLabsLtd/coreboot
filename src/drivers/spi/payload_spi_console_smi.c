/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/flash.h>
#include <console/payload_spi_console.h>
#include <cpu/x86/smm.h>
#include <string.h>

static size_t remaining = PAYLOAD_SPI_CONSOLE_BOOT_LIMIT;
static bool busy;

void payload_spi_console_smi(void)
{
	struct payload_spi_console_request header;
	struct payload_spi_console_request *request;
	uint8_t payload[PAYLOAD_SPI_CONSOLE_MAX_CHUNK];
	enum payload_spi_console_status status = PAYLOAD_SPI_CONSOLE_INVALID;
	uintptr_t base;
	size_t size;

	smm_get_payload_spi_console_buffer(&base, &size);
	if (!base || size < sizeof(header))
		return;
	request = (void *)base;
	memcpy(&header, request, sizeof(header));
	if (header.signature != PAYLOAD_SPI_CONSOLE_SIGNATURE ||
	    header.version != PAYLOAD_SPI_CONSOLE_VERSION ||
	    header.header_size != sizeof(header) || header.flags != 0 ||
	    header.status != PAYLOAD_SPI_CONSOLE_PENDING || header.length == 0 ||
	    header.length > sizeof(payload) || header.length > size - sizeof(header))
		goto complete;
	if (busy) {
		status = PAYLOAD_SPI_CONSOLE_BUSY;
		goto complete;
	}
	if (header.length > remaining) {
		status = PAYLOAD_SPI_CONSOLE_LIMIT;
		goto complete;
	}

	busy = true;
	memcpy(payload, request->data, header.length);
	/* Erased bytes delimit the persisted log; they cannot occur in a record. */
	for (size_t i = 0; i < header.length; i++) {
		if (payload[i] == 0xff) {
			busy = false;
			goto complete;
		}
	}
	if (!flashconsole_append(payload, header.length)) {
		status = PAYLOAD_SPI_CONSOLE_IO_ERROR;
	} else {
		remaining -= header.length;
		status = PAYLOAD_SPI_CONSOLE_SUCCESS;
	}
	busy = false;

complete:
	request->status = status;
}
