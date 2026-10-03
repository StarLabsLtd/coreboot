/* SPDX-License-Identifier: BSD-2-Clause-Patent */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CDK2_SERIAL_IO_TEST
#include "../src/modules/serial_io/serial_io.c"

static uint8_t registers[8];
static unsigned int writes;
static unsigned int reads;
static unsigned int installs;
static unsigned int rbr_reads;
static uint64_t reinstall_status;

struct serial_hob_fixture {
	EFI_HOB_HANDOFF_INFO_TABLE handoff;
	EFI_HOB_GUID_TYPE guid;
	CDK2_SERIAL_PORT_HOB serial;
	EFI_HOB_GENERIC_HEADER end;
};

static void prepare_serial_hob(struct serial_hob_fixture *fixture)
{
	fixture->handoff = (EFI_HOB_HANDOFF_INFO_TABLE) {
		.header = {EFI_HOB_TYPE_HANDOFF, sizeof(fixture->handoff), 0},
		.version = EFI_HOB_HANDOFF_TABLE_VERSION,
		.efi_memory_bottom = (uintptr_t)fixture,
		.efi_memory_top = (uintptr_t)fixture + sizeof(*fixture),
		.efi_free_memory_bottom = (uintptr_t)&fixture->end + sizeof(fixture->end),
		.efi_free_memory_top = (uintptr_t)fixture + sizeof(*fixture),
		.efi_end_of_hob_list = (uintptr_t)&fixture->end,
	};
	fixture->guid.header.hob_type = EFI_HOB_TYPE_GUID_EXTENSION;
	fixture->guid.header.hob_length =
		(sizeof(fixture->guid) + sizeof(fixture->serial) + 7U) & ~(size_t)7U;
	memcpy(&fixture->guid.name, &serial_hob_guid, sizeof(serial_hob_guid));
	fixture->end = (EFI_HOB_GENERIC_HEADER) {EFI_HOB_TYPE_END_OF_HOB_LIST,
		sizeof(fixture->end), 0};
}

static uint64_t CDK2_MS_ABI test_reinstall(void *handle, const struct guid *guid,
	void *old_interface, void *new_interface)
{
	(void)handle;
	(void)guid;
	(void)old_interface;
	(void)new_interface;
	return reinstall_status;
}

uint8_t serial_io_test_read(uint64_t address)
{
	unsigned int reg = (unsigned int)((address - port->register_base) /
		port->register_stride);

	reads++;
	if (reg == UART_RBR)
		rbr_reads++;
	return registers[reg];
}

void serial_io_test_write(uint64_t address, uint8_t value)
{
	registers[(address - port->register_base) / port->register_stride] = value;
	writes++;
}

static uint64_t CDK2_MS_ABI test_stall(size_t microseconds)
{
	(void)microseconds;
	return EFI_SUCCESS;
}

static uint64_t CDK2_MS_ABI test_raise(uint64_t tpl)
{
	return tpl;
}

static void CDK2_MS_ABI test_restore(uint64_t tpl)
{
	(void)tpl;
}

static uint64_t CDK2_MS_ABI test_install(void **handle, ...)
{
	installs++;
	*handle = (void *)1;
	return EFI_SUCCESS;
}

static int expect(int condition, const char *message)
{
	if (!condition)
		fprintf(stderr, "serial io test failed: %s\n", message);
	assert(condition);
	return !condition;
}

int main(void)
{
	CDK2_SERIAL_PORT_HOB test_port = {
		.header = { CDK2_SERIAL_PORT_HOB_REVISION, 0, sizeof(test_port) },
		.use_mmio = 0, .register_stride = 1, .baud_rate = 115200,
		.input_hertz = 3686400,
		.register_base = 0x3f8,
	};
	struct boot_services_view services = { 0 };
	uint8_t output[] = { 'o', 'k' };
	size_t size = sizeof(output);
	uint32_t control;
	int failures = 0;
	struct serial_hob_fixture hob_fixture = {0};
	struct serial_hob_fixture rejected_fixture;
	struct config_table configuration = {0};
	struct system_table system = {0};

	port = &test_port;
	boot_services = &services;
	services.stall = test_stall;
	services.raise_tpl = test_raise;
	services.restore_tpl = test_restore;
	services.reinstall = test_reinstall;
	services.install_multiple = test_install;
	failures += expect(configure(115200, 16, CDK2_NO_PARITY, 8,
		CDK2_ONE_STOP_BIT) == EFI_SUCCESS, "configure 8N1");
	failures += expect(registers[UART_LCR] == 3, "8N1 line control");
	failures += expect(registers[UART_DLL] == 2 && registers[UART_DLM] == 0,
		"divisor uses platform UART clock");
	failures += expect(writes >= 6, "UART programmed");
	failures += expect(configure(0xffffffffU, 16, CDK2_NO_PARITY, 8,
		CDK2_ONE_STOP_BIT) == EFI_INVALID_PARAMETER, "reject impossible baud");
	failures += expect(configure(100000, 16, CDK2_NO_PARITY, 8,
		CDK2_ONE_STOP_BIT) == EFI_INVALID_PARAMETER, "reject inexact baud");
	test_port.input_hertz = 24000000;
	failures += expect(configure(115200, 16, CDK2_NO_PARITY, 8,
		CDK2_ONE_STOP_BIT) == EFI_SUCCESS, "accept representable baud within tolerance");
	test_port.input_hertz = 3686400;
	failures += expect(configure(115200, 8, CDK2_NO_PARITY, 8,
		CDK2_ONE_STOP_BIT) == EFI_INVALID_PARAMETER, "reject unsupported FIFO depth");
	failures += expect(configure(115200, 16, (enum cdk2_serial_parity)-1, 8,
		CDK2_ONE_STOP_BIT) == EFI_INVALID_PARAMETER, "reject negative parity");
	failures += expect(configure(115200, 16, CDK2_NO_PARITY, 8,
		(enum cdk2_serial_stop_bits)-1) == EFI_INVALID_PARAMETER,
		"reject negative stop bits");
	writes = 0;
	failures += expect(serial_set_attributes(&serial_io, 115200, 1,
		UART_DEFAULT_TIMEOUT, CDK2_NO_PARITY, 8, CDK2_ONE_STOP_BIT) == EFI_SUCCESS &&
		writes != 0 && registers[UART_FCR] == 0,
		"FIFO-only attribute change reprograms hardware");
	writes = 0;
	pending_receive_errors = UART_LSR_ERROR;
	failures += expect(serial_reset(&serial_io) == EFI_SUCCESS && writes != 0,
		"reset always reinitializes UART and FIFOs");
	failures += expect(registers[UART_MCR] == 0 && pending_receive_errors == 0,
		"reset clears modem control and latched errors");
	serial_handle = (void *)1;
	reinstall_status = EFI_DEVICE_ERROR;
	failures += expect(serial_set_attributes(&serial_io, 57600, 16,
		UART_DEFAULT_TIMEOUT, CDK2_NO_PARITY, 8, CDK2_ONE_STOP_BIT) ==
		EFI_DEVICE_ERROR && serial_mode.baud_rate == 115200 &&
		serial_path.uart.baud_rate == 115200 && registers[UART_DLL] == 2,
		"failed device-path reinstall rolls back mode and hardware");
	serial_handle = NULL;
	reinstall_status = EFI_SUCCESS;
	registers[UART_LSR] = UART_LSR_THR_EMPTY | UART_LSR_TX_EMPTY;
	failures += expect(serial_write(&serial_io, &size, output) == EFI_SUCCESS,
		"write succeeds");
	failures += expect(size == sizeof(output) && registers[UART_THR] == 'k',
		"write consumes all bytes");
	failures += expect(serial_set_control(&serial_io,
		CDK2_SERIAL_DATA_TERMINAL_READY | CDK2_SERIAL_REQUEST_TO_SEND) == EFI_SUCCESS,
		"set modem control");
	failures += expect(serial_get_control(&serial_io, &control) == EFI_SUCCESS,
		"get modem control");
	failures += expect((control & CDK2_SERIAL_OUTPUT_BUFFER_EMPTY) != 0,
		"reports empty output buffer");
	failures += expect((serial_mode.control_mask & (CDK2_SERIAL_INPUT_BUFFER_EMPTY |
		CDK2_SERIAL_OUTPUT_BUFFER_EMPTY)) == (CDK2_SERIAL_INPUT_BUFFER_EMPTY |
		CDK2_SERIAL_OUTPUT_BUFFER_EMPTY), "advertises mandatory buffer status");
	failures += expect((serial_mode.control_mask & 0xf0U) == 0xf0U,
		"advertises modem input status");
	failures += expect(serial_set_control(&serial_io,
		CDK2_SERIAL_INPUT_BUFFER_EMPTY) == EFI_UNSUPPORTED,
		"read-only buffer status is not accepted by SetControl");
	failures += expect(serial_get_control(&serial_io, NULL) == EFI_INVALID_PARAMETER,
		"reject NULL control");
	registers[UART_LSR] = UART_LSR_DATA_READY | 0x04U;
	registers[UART_RBR] = 0xa5;
	rbr_reads = 0;
	size = 1;
	failures += expect(serial_read(&serial_io, &size, output) == EFI_DEVICE_ERROR &&
		size == 0 && rbr_reads == 1,
		"receive errors consume and reject corrupted bytes");
	registers[UART_LSR] = UART_LSR_DATA_READY | 0x04U;
	(void)serial_get_control(&serial_io, &control);
	registers[UART_LSR] = UART_LSR_DATA_READY;
	rbr_reads = 0;
	size = 1;
	failures += expect(serial_read(&serial_io, &size, output) == EFI_DEVICE_ERROR &&
		size == 0 && rbr_reads == 1,
		"GetControl preserves receive errors for Read");
	registers[UART_LSR] = UART_LSR_THR_EMPTY | 0x04U;
	size = 1;
	failures += expect(serial_write(&serial_io, &size, output) == EFI_SUCCESS,
		"write polling incorrectly returned a receive error");
	registers[UART_LSR] = UART_LSR_DATA_READY;
	size = 1;
	failures += expect(serial_read(&serial_io, &size, output) == EFI_DEVICE_ERROR &&
		size == 0, "write polling discarded a receive error");
	/* Exercise actual entry validation, not just private register helpers. */
	prepare_serial_hob(&hob_fixture);
	hob_fixture.serial = test_port;
	configuration.guid = hob_list_guid;
	configuration.table = &hob_fixture;
	system.boot_services = &services;
	system.config_count = 1;
	system.config_tables = &configuration;
	for (uint8_t stride = 1; stride <= 4; stride *= 2) {
		for (uint8_t mmio = 0; mmio <= 1; mmio++) {
			unsigned int previous_installs = installs;

			hob_fixture.serial.use_mmio = mmio;
			hob_fixture.serial.register_stride = stride;
			hob_fixture.serial.register_base = mmio ?
				UINTPTR_MAX - 7U * stride + 1U : UINT16_MAX - 6U * stride;
			failures += expect(cdk2_serial_io_entry(NULL, &system) == EFI_SUCCESS &&
				installs == previous_installs + 1,
				"last valid complete register span rejected by entry");
			rejected_fixture = hob_fixture;
			prepare_serial_hob(&rejected_fixture);
			rejected_fixture.serial.register_base += mmio ? stride : 1U;
			configuration.table = &rejected_fixture;
			reads = writes = 0;
			previous_installs = installs;
			failures += expect(cdk2_serial_io_entry(NULL, &system) == EFI_NOT_FOUND &&
				reads == 0 && writes == 0 && installs == previous_installs,
				"overflowing register span reached hardware or install");
			configuration.table = &hob_fixture;
		}
	}
	{
		CDK2_SERIAL_PORT_HOB invalid = test_port;
		const CDK2_SERIAL_PORT_HOB *previous_port = port;
		struct boot_services_view *previous_services = boot_services;
		struct boot_services_view alternate_services = services;
		unsigned int previous_installs = installs;

		invalid.use_mmio = 1;
		invalid.register_stride = 3;
		rejected_fixture = hob_fixture;
		prepare_serial_hob(&rejected_fixture);
		rejected_fixture.serial = invalid;
		configuration.table = &rejected_fixture;
		system.boot_services = &alternate_services;
		reads = writes = 0;
		failures += expect(cdk2_serial_io_entry(NULL, &system) == EFI_NOT_FOUND &&
			reads == 0 && writes == 0 && installs == previous_installs &&
			port == previous_port && boot_services == previous_services,
			"invalid entry changed the installed protocol context");
	}
	system.boot_services = &services;
	for (unsigned int scenario = 0; scenario < 18; scenario++) {
		unsigned int previous_installs = installs;
		const CDK2_SERIAL_PORT_HOB *previous_port = port;

		rejected_fixture = hob_fixture;
		prepare_serial_hob(&rejected_fixture);
		configuration.table = &rejected_fixture;
		system.config_count = 1;
		system.config_tables = &configuration;
		switch (scenario) {
		case 0:
			rejected_fixture.guid.header.hob_length = 0;
			break;
		case 1:
			rejected_fixture.guid.header.hob_length--;
			break;
		case 2:
			rejected_fixture.guid.header.hob_length += 8;
			break;
		case 3:
			rejected_fixture.handoff.efi_end_of_hob_list += 8;
			break;
		case 4:
			rejected_fixture.end.hob_type = 0;
			break;
		case 5:
			rejected_fixture.end.hob_length += 8;
			break;
		case 6:
			rejected_fixture.handoff.version = 0;
			break;
		case 7:
			rejected_fixture.serial.header.length++;
			break;
		case 8:
			rejected_fixture.serial.header.reserved = 1;
			break;
		case 9:
			rejected_fixture.serial.register_stride = 0;
			break;
		case 10:
			rejected_fixture.serial.register_base = 0;
			break;
		case 11:
			rejected_fixture.serial.register_base = 1;
			break;
		case 12:
			rejected_fixture.serial.input_hertz = 0;
			break;
		case 13:
			configuration.table = (uint8_t *)&rejected_fixture + 1;
			break;
		case 14:
			system.config_count = SIZE_MAX / sizeof(configuration) + 1;
			break;
		case 15:
			system.config_tables = NULL;
			break;
		case 16:
			system.config_tables = (void *)(UINTPTR_MAX &
				~(uintptr_t)(_Alignof(struct config_table) - 1U));
			break;
		case 17:
			rejected_fixture.serial.use_mmio = 2;
			break;
		}
		reads = writes = 0;
		failures += expect(cdk2_serial_io_entry(NULL, &system) == EFI_NOT_FOUND &&
			reads == 0 && writes == 0 && installs == previous_installs &&
			port == previous_port && boot_services == &services,
			"malformed HOB/configuration reached I/O or changed live context");
	}
	{
		struct config_table tables[MAX_CONFIGURATION_TABLES + 1] = {0};
		unsigned int previous_installs = installs;

		tables[MAX_CONFIGURATION_TABLES - 1] = configuration;
		tables[MAX_CONFIGURATION_TABLES - 1].table = &hob_fixture;
		system.config_tables = tables;
		system.config_count = MAX_CONFIGURATION_TABLES;
		failures += expect(cdk2_serial_io_entry(NULL, &system) == EFI_SUCCESS &&
			installs == previous_installs + 1,
			"last supported configuration-table entry rejected");
		previous_installs = installs;
		system.config_count++;
		reads = writes = 0;
		failures += expect(cdk2_serial_io_entry(NULL, &system) == EFI_NOT_FOUND &&
			reads == 0 && writes == 0 && installs == previous_installs &&
			port == &hob_fixture.serial && boot_services == &services,
			"excess configuration-table count reached I/O or changed live context");
	}
	{
		struct config_table duplicates[2] = {configuration, configuration};
		unsigned int previous_installs = installs;

		duplicates[0].table = duplicates[1].table = &hob_fixture;
		system.config_tables = duplicates;
		system.config_count = 2;
		reads = writes = 0;
		failures += expect(cdk2_serial_io_entry(NULL, &system) == EFI_NOT_FOUND &&
			reads == 0 && writes == 0 && installs == previous_installs,
			"duplicate HOB-list configuration GUID accepted");
	}
	{
		uint8_t duplicate[sizeof(hob_fixture) +
			((sizeof(hob_fixture.guid) + sizeof(hob_fixture.serial) + 7U) & ~7U)]
			__aligned(8);
		EFI_HOB_HANDOFF_INFO_TABLE *handoff = (void *)duplicate;
		size_t record_size = hob_fixture.guid.header.hob_length;
		EFI_HOB_GENERIC_HEADER *end = (void *)(duplicate + sizeof(*handoff) +
			2 * record_size);
		unsigned int previous_installs = installs;

		memcpy(duplicate, &hob_fixture, sizeof(hob_fixture));
		memcpy(duplicate + sizeof(*handoff) + record_size, &hob_fixture.guid,
			record_size);
		*end = hob_fixture.end;
		handoff->efi_memory_bottom = (uintptr_t)duplicate;
		handoff->efi_memory_top = (uintptr_t)duplicate + sizeof(duplicate);
		handoff->efi_free_memory_bottom = (uintptr_t)end + sizeof(*end);
		handoff->efi_free_memory_top = handoff->efi_memory_top;
		handoff->efi_end_of_hob_list = (uintptr_t)end;
		configuration.table = duplicate;
		system.config_count = 1;
		system.config_tables = &configuration;
		failures += expect(cdk2_hob_list_validate(duplicate) == EFI_SUCCESS,
			"duplicate serial fixture is not structurally valid");
		reads = writes = 0;
		failures += expect(cdk2_serial_io_entry(NULL, &system) == EFI_NOT_FOUND &&
			reads == 0 && writes == 0 && installs == previous_installs,
			"duplicate serial GUID accepted");
	}
	return failures != 0;
}
