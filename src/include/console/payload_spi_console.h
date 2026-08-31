/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CONSOLE_PAYLOAD_SPI_CONSOLE_H
#define CONSOLE_PAYLOAD_SPI_CONSOLE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <commonlib/bsd/compiler.h>

#define PAYLOAD_SPI_CONSOLE_SIGNATURE 0x434c5053U
#define PAYLOAD_SPI_CONSOLE_VERSION 1U
#define PAYLOAD_SPI_CONSOLE_APM_CMD 0xe8U
#define PAYLOAD_SPI_CONSOLE_MAX_CHUNK 256U
#define PAYLOAD_SPI_CONSOLE_BUFFER_SIZE 512U
#define PAYLOAD_SPI_CONSOLE_BOOT_LIMIT (64U * 1024U)

enum payload_spi_console_status {
	PAYLOAD_SPI_CONSOLE_PENDING = 0,
	PAYLOAD_SPI_CONSOLE_SUCCESS = 1,
	PAYLOAD_SPI_CONSOLE_INVALID = 2,
	PAYLOAD_SPI_CONSOLE_BUSY = 3,
	PAYLOAD_SPI_CONSOLE_LIMIT = 4,
};

struct payload_spi_console_request {
	uint32_t signature;
	uint16_t version;
	uint16_t header_size;
	uint32_t sequence;
	uint16_t length;
	uint16_t flags;
	uint32_t status;
	uint8_t data[];
} __packed;

typedef void (*payload_spi_console_sink)(const uint8_t *data, size_t length,
	void *context);

enum payload_spi_console_status payload_spi_console_process(void *buffer,
	size_t buffer_size, size_t *remaining, bool *busy,
	payload_spi_console_sink sink, void *context);
void payload_spi_console_smi(void);

#endif
