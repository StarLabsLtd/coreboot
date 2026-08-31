/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/payload_spi_console.h>
#include <string.h>

enum payload_spi_console_status payload_spi_console_process(void *buffer,
	size_t buffer_size, size_t *remaining, bool *busy,
	payload_spi_console_sink sink, void *context)
{
	struct payload_spi_console_request header;
	struct payload_spi_console_request *request = buffer;
	uint8_t payload[PAYLOAD_SPI_CONSOLE_MAX_CHUNK];
	enum payload_spi_console_status status = PAYLOAD_SPI_CONSOLE_INVALID;

	if (!buffer || !remaining || !busy || !sink ||
	    buffer_size < sizeof(header))
		return status;

	memcpy(&header, request, sizeof(header));
	if (header.signature != PAYLOAD_SPI_CONSOLE_SIGNATURE ||
	    header.version != PAYLOAD_SPI_CONSOLE_VERSION ||
	    header.header_size != sizeof(header) || header.flags != 0 ||
	    header.status != PAYLOAD_SPI_CONSOLE_PENDING || header.length == 0 ||
	    header.length > sizeof(payload) ||
	    header.length > buffer_size - sizeof(header))
		goto complete;
	if (*busy) {
		status = PAYLOAD_SPI_CONSOLE_BUSY;
		goto complete;
	}
	if (header.length > *remaining) {
		status = PAYLOAD_SPI_CONSOLE_LIMIT;
		goto complete;
	}

	*busy = true;
	memcpy(payload, request->data, header.length);
	sink(payload, header.length, context);
	*remaining -= header.length;
	*busy = false;
	status = PAYLOAD_SPI_CONSOLE_SUCCESS;

complete:
	request->status = status;
	return status;
}
