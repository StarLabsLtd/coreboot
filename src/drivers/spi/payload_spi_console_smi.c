/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/flash.h>
#include <console/cbmem_console.h>
#include <console/payload_spi_console.h>
#include <cpu/x86/smm.h>
#include <string.h>

static size_t remaining = PAYLOAD_SPI_CONSOLE_BOOT_LIMIT;
static bool busy;

#if CONFIG(PAYLOAD_SPI_FLASH_CONSOLE_NATIVE_PREFIX)
/* The SMM-owned fixed snapshot does not consume the native boot stack. */
static uint8_t prefix[PAYLOAD_SPI_CONSOLE_BOOT_LIMIT];
static bool prefix_attempted, prefix_ready;

static bool persist_native_prefix(const uint8_t *data, size_t length)
{
	struct {
		uint32_t size, cursor;
		uint8_t body[];
	} *console;
	uint32_t original_size, original_cursor;
	size_t span, prefix_size;

	if (prefix_attempted)
		return prefix_ready;
	prefix_attempted = true;
	/* This span comes from the existing fixed SMM loader record, never from
	 * a request field or a payload-supplied pointer. Wrapped logs cannot prove
	 * a complete prefix and must not be accepted as latest-boot evidence. */
	smm_get_cbmemc_buffer((void **)&console, &span);
	if (!console || span < sizeof(*console))
		return false;
	original_size = console->size;
	original_cursor = console->cursor;
	if (original_size != span - sizeof(*console) ||
	    original_cursor > original_size || original_cursor < length ||
	    original_cursor - length > sizeof(prefix))
		return false;
	prefix_size = original_cursor - length;
	/* Payload diagnostics tee to CBMEM first. Prove the exact pending suffix
	 * before removing it; append below will persist that record exactly once. */
	if (memcmp(console->body + prefix_size, data, length))
		return false;
	memcpy(prefix, console->body, prefix_size);
	if (console->size != original_size || console->cursor != original_cursor ||
	    memcmp(console->body, prefix, prefix_size) ||
	    memcmp(console->body + prefix_size, data, length))
		return false;
	if (!flashconsole_reclaim_latest() ||
	    (prefix_size && !flashconsole_append(prefix, prefix_size)) ||
	    console->size != original_size || console->cursor != original_cursor ||
	    memcmp(console->body, prefix, prefix_size) ||
	    memcmp(console->body + prefix_size, data, length))
		return false;
	prefix_ready = true;
	return true;
}
#endif

static bool write_flash_console(const uint8_t *data, size_t length,
	void *context)
{
	(void)context;
#if CONFIG(PAYLOAD_SPI_FLASH_CONSOLE_NATIVE_PREFIX)
	if (!persist_native_prefix(data, length))
		return false;
#endif
	return flashconsole_append(data, length);
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
