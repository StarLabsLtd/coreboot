/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#define main original_diagnostic_fixture_main
#include DIAGNOSTIC_FIXTURE_SOURCE
#undef main
#include <assert.h>
#include <stddef.h>

static struct fixture emitted;
static uint8_t console_bytes[sizeof(struct cdk2_cbmem_console) + 2048];
static uint8_t timestamp_bytes[sizeof(struct cdk2_diag_timestamp_table) +
	8 * sizeof(struct cdk2_diag_timestamp_entry)];
static struct handoff_fixture wire_handoff;
static UINT64 shared_owner;
static UINT64 owner_token = 0x100000001ULL;
static uint32_t remaining = 4096;

#define FIELD(type, member) \
	fprintf(stderr, #type "." #member "=%zu/%zu\n", \
		offsetof(struct type, member), sizeof(((struct type *)0)->member))

static void write_bytes(const void *data, size_t size)
{
	assert(fwrite(data, size, 1, stdout) == 1);
}

int main(void)
{
	struct cdk2_cbmem_console *console = (void *)console_bytes;
	struct cdk2_diag_timestamp_table *timestamps = (void *)timestamp_bytes;
	struct cdk2_diag_config config = {0};
	uint64_t no_clock = cdk2_diag_now();
	assert(no_clock == 0);
	write_bytes(&no_clock, sizeof(no_clock));

	fprintf(stderr, "config=%zu/%zu TIME=%zu/%zu entry=%zu/%zu console=%zu/%zu HOB=%zu/%zu\n",
		sizeof(config), _Alignof(struct cdk2_diag_config),
		sizeof(*timestamps), _Alignof(struct cdk2_diag_timestamp_table),
		sizeof(struct cdk2_diag_timestamp_entry), _Alignof(struct cdk2_diag_timestamp_entry),
		sizeof(*console), _Alignof(struct cdk2_cbmem_console),
		sizeof(struct cdk2_diag_handoff), _Alignof(struct cdk2_diag_handoff));
	FIELD(cdk2_diag_config, putc);
	FIELD(cdk2_diag_config, putc_context);
	FIELD(cdk2_diag_config, putc_provenance);
	FIELD(cdk2_diag_config, cbmem_console);
	FIELD(cdk2_diag_config, cbmem_console_bytes);
	FIELD(cdk2_diag_config, spi_console_buffer);
	FIELD(cdk2_diag_config, spi_console_buffer_bytes);
	FIELD(cdk2_diag_config, spi_console_max_chunk);
	FIELD(cdk2_diag_config, spi_console_boot_limit);
	FIELD(cdk2_diag_config, spi_console_apm_cmd);
	FIELD(cdk2_diag_config, spi_console_remaining);
	FIELD(cdk2_diag_config, timestamps);
	FIELD(cdk2_diag_config, timestamp_bytes);
	FIELD(cdk2_diag_config, clock);
	FIELD(cdk2_diag_config, clock_context);
	FIELD(cdk2_diag_config, clock_hz);
	FIELD(cdk2_diag_config, owner);
	FIELD(cdk2_diag_config, owner_context);
	FIELD(cdk2_diag_config, emission_owner);
	write_bytes(&config, sizeof(config));
	console->size = 2048;
	timestamps->base_time = 100;
	timestamps->max_entries = 8;
	timestamps->tick_freq_mhz = 1;
	emitted.clock = 1000;
	config.putc = putc_test;
	config.putc_context = &emitted;
	config.cbmem_console = console;
	config.cbmem_console_bytes = sizeof(console_bytes);
	config.spi_console_buffer_bytes = 0x100000001ULL;
	config.spi_console_max_chunk = 0x80000001U;
	config.spi_console_boot_limit = 0x80000002U;
	config.spi_console_remaining = &remaining;
	config.timestamps = timestamps;
	config.timestamp_bytes = sizeof(timestamp_bytes);
	config.clock = clock_test;
	config.clock_context = &emitted;
	config.clock_hz = 0x100000001ULL;
	config.owner = fixed_owner;
	config.owner_context = &owner_token;
	config.emission_owner = &shared_owner;
	/* Call the actual assigned fixture callbacks through the native types. */
	for (unsigned int value = 0; value < 256; value++) {
		emitted.length = 0;
		config.putc((char)value, config.putc_context);
		assert(emitted.length == 1);
		assert((unsigned char)emitted.output[0] == value);
		write_bytes(emitted.output, 1);
	}
	emitted.clock = UINT64_C(0xfedcba9876543210);
	owner_token = UINT64_C(0x8765432100000001);
	uint64_t callback_clock = config.clock(config.clock_context);
	uint64_t callback_owner = config.owner(config.owner_context);
	assert(callback_clock == emitted.clock);
	assert(callback_owner == owner_token);
	write_bytes(&callback_clock, sizeof(callback_clock));
	write_bytes(&callback_owner, sizeof(callback_owner));
	memset(&emitted, 0, sizeof(emitted));
	emitted.clock = 1000;
	owner_token = 0x100000001ULL;
	for (unsigned int value = 0; value < 256; value++) {
		config.spi_console_apm_cmd = (uint8_t)value;
		assert(config.spi_console_apm_cmd == value);
		write_bytes(&config, sizeof(config));
	}
	cdk2_diag_configure(&config);
	cdk2_diag_emit(CDK2_DIAG_INFO, CDK2_DIAG_NATIVE, CDK2_EVENT_NATIVE_ENTRY,
		EFI_SUCCESS);
	cdk2_diag_phase_event_at(CDK2_EVENT_LINEAR_PHASE_BEGIN(CDK2_LINEAR_CAPSULE_RAM),
		1200, "CAPSULE_RAM", "begin", EFI_SUCCESS, 0);
	write_bytes(console_bytes, sizeof(console_bytes));
	write_bytes(timestamp_bytes, sizeof(timestamp_bytes));
	write_bytes(&emitted, sizeof(emitted));
	memset(console_bytes, 0, sizeof(console_bytes));
	console->size = 2048;
	prepare_handoff(&wire_handoff);
	wire_handoff.diagnostic.cbmem_console_base = (UINTN)console;
	wire_handoff.diagnostic.cbmem_console_bytes = sizeof(console_bytes);
	assert(cdk2_diag_configure_hob(&wire_handoff) == EFI_SUCCESS);
	cdk2_diag_emit_console(CDK2_DIAG_INFO, CDK2_DIAG_NATIVE,
		CDK2_EVENT_NATIVE_ENTRY, EFI_SUCCESS);
	write_bytes(&wire_handoff, sizeof(wire_handoff));
	write_bytes(console_bytes, sizeof(console_bytes));

	/* Actual native APIs: high-bit clock/value and all bytes, not wire models. */
	memset(console_bytes, 0, sizeof(console_bytes));
	console->size = 2048;
	config.timestamps = NULL;
	config.clock = clock_test;
	emitted.clock = UINT64_C(0xfedcba9876543210);
	cdk2_diag_configure(&config);
	uint64_t native_now = cdk2_diag_now();
	assert(native_now == emitted.clock);
	write_bytes(&native_now, sizeof(native_now));
	cdk2_diag_value(CDK2_DIAG_NATIVE, "wide", native_now);
	static const char wide[] =
		"CDK2 | module 0x01 | wide | 0xfedcba9876543210\n";
	assert(console->cursor == sizeof(wide) - 1);
	assert(memcmp(console->body, wide, sizeof(wide) - 1) == 0);
	uint32_t saved_cursor = console->cursor;
	cdk2_diag_value(CDK2_DIAG_NATIVE, NULL, native_now);
	cdk2_diag_bytes(CDK2_DIAG_NATIVE, NULL, console_bytes, 1);
	cdk2_diag_bytes(CDK2_DIAG_NATIVE, "invalid", NULL, 1);
	assert(console->cursor == saved_cursor);
	cdk2_diag_bytes(CDK2_DIAG_NATIVE, "empty", NULL, 0);
	static const char empty[] = "CDK2 | module 0x01 | empty | \n";
	assert(console->cursor == saved_cursor + sizeof(empty) - 1);
	assert(memcmp(console->body + saved_cursor, empty, sizeof(empty) - 1) == 0);
	uint8_t all_bytes[256];
	for (unsigned int value = 0; value < 256; value++)
		all_bytes[value] = (uint8_t)value;
	saved_cursor = console->cursor;
	cdk2_diag_bytes(CDK2_DIAG_NATIVE, "all", all_bytes, sizeof(all_bytes));
	static const char prefix[] = "CDK2 | module 0x01 | all | ";
	static const char hex[] = "0123456789abcdef";
	assert(memcmp(console->body + saved_cursor, prefix, sizeof(prefix) - 1) == 0);
	for (unsigned int value = 0; value < 256; value++) {
		assert(console->body[saved_cursor + sizeof(prefix) - 1 + 2 * value] ==
			(uint8_t)hex[value >> 4]);
		assert(console->body[saved_cursor + sizeof(prefix) + 2 * value] ==
			(uint8_t)hex[value & 15]);
	}
	assert(console->cursor == saved_cursor + sizeof(prefix) - 1 + 513);
	assert(console->body[console->cursor - 1] == '\n');
	write_bytes(console_bytes, sizeof(console_bytes));

	/* All nine native APIs retain actual console and TIME emission. */
	uint8_t phase_table_bytes[sizeof(struct cdk2_diag_timestamp_table) +
		70 * sizeof(struct cdk2_diag_timestamp_entry)] = {0};
	struct cdk2_diag_timestamp_table *phase_table = (void *)phase_table_bytes;
	phase_table->max_entries = 70;
	phase_table->tick_freq_mhz = 1;
	memset(console_bytes, 0, sizeof(console_bytes));
	console->size = 2048;
	config.timestamps = phase_table;
	config.timestamp_bytes = sizeof(phase_table_bytes);
	config.clock_hz = UINT64_C(1000000);
	emitted.clock = UINT64_C(0xfedcba9876543210);
	cdk2_diag_configure(&config);
	cdk2_diag_emit(CDK2_DIAG_WARN, CDK2_DIAG_NATIVE, UINT32_C(0xfedcba98),
		EFI_DEVICE_ERROR);
	assert(phase_table->num_entries == 1);
	assert(phase_table->entries[0].entry_stamp == INT64_MAX);
	cdk2_diag_emit_console(CDK2_DIAG_WARN, CDK2_DIAG_NATIVE,
		UINT32_C(0x87654321), EFI_DEVICE_ERROR);
	assert(phase_table->num_entries == 1);
	cdk2_diag_emit_compact(CDK2_DIAG_NATIVE, UINT32_C(0x98765432), EFI_SUCCESS);
	assert(phase_table->num_entries == 2);
	char message[136] = "[warn] ";
	for (unsigned int value = 128; value < 256; value++)
		message[7 + value - 128] = (char)value;
	cdk2_diag_message(CDK2_DIAG_NATIVE, message);
	cdk2_diag_message_status(CDK2_DIAG_NATIVE, message, EFI_DEVICE_ERROR);
	cdk2_diag_message(CDK2_DIAG_NATIVE, NULL);
	cdk2_diag_message_status(CDK2_DIAG_NATIVE, NULL, EFI_DEVICE_ERROR);
	assert(phase_table->num_entries == 2);
	cdk2_diag_phase("native", "phase", EFI_SUCCESS,
		UINT64_C(0x8000000100000002));
	cdk2_diag_phase_event(UINT32_C(0xe1234567), "native", "phase-event",
		EFI_DEVICE_ERROR, UINT64_C(0x8000000200000003));
	cdk2_diag_phase_event_at(UINT32_C(0xf1234567),
		UINT64_C(0xfedcba9876543211), "native", "phase-at", EFI_SUCCESS,
		UINT64_C(0x8000000300000004));
	assert(phase_table->num_entries == 4);
	assert(phase_table->entries[3].entry_stamp == INT64_MAX);
	assert(cdk2_diag_optional_phase_pair_at(UINT32_C(0xd1234567),
		UINT32_C(0xc1234567), UINT64_C(0xfedcba9876543212),
		UINT64_C(0xfedcba9876543213), "native", UINT64_C(0x8000000400000005),
		EFI_DEVICE_ERROR));
	assert(phase_table->num_entries == 6);
	assert(!cdk2_diag_optional_phase_pair_at(1, 2, 3, 4, NULL, 5, EFI_SUCCESS));
	cdk2_diag_phase(NULL, "absent", EFI_SUCCESS, 0);
	cdk2_diag_phase_event(1, "absent", NULL, EFI_SUCCESS, 0);
	cdk2_diag_phase_event_at(1, 2, NULL, "absent", EFI_SUCCESS, 0);
	assert(phase_table->num_entries == 6);
	write_bytes(console_bytes, sizeof(console_bytes));
	write_bytes(phase_table_bytes, sizeof(phase_table_bytes));
	return 0;
}
