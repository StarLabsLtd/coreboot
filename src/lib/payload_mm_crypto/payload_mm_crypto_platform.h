/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef PAYLOAD_MM_CRYPTO_PLATFORM_H
#define PAYLOAD_MM_CRYPTO_PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void *payload_mm_crypto_calloc(size_t count, size_t size);
void payload_mm_crypto_free(void *allocation);
int payload_mm_crypto_snprintf(char *buffer, size_t size,
	const char *format, ...);

#endif
