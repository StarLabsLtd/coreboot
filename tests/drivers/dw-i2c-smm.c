/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <device/i2c_simple.h>
#include <device/pci.h>
#include <device/pci_def.h>
#include <drivers/i2c/designware/dw_i2c.h>
#include <intelblocks/lpss.h>
#include <tests/test.h>
#include <timer.h>

#define TEST_MMIO_BASE		0xfe000000
#define TEST_MMIO_SIZE		0x100

#define REG_CMD_DATA		0x10
#define REG_RAW_INTR_STAT	0x34
#define REG_CLEAR_INTR		0x40
#define REG_ENABLE		0x6c
#define REG_STATUS		0x70
#define REG_ENABLE_STATUS	0x9c
#define REG_COMP_TYPE		0xfc

#define ENABLE_CONTROLLER	BIT(0)
#define STATUS_TX_FIFO_NOT_FULL	BIT(1)
#define STATUS_TX_FIFO_EMPTY	BIT(2)
#define STATUS_RX_FIFO_NOT_EMPTY	BIT(3)
#define INTR_STAT_STOP_DET	BIT(9)
#define DW_I2C_COMP_TYPE		0x44570140

enum event {
	EVENT_POWER_D0,
	EVENT_RESET_RELEASE,
	EVENT_ENABLE,
	EVENT_COMMAND,
	EVENT_CLEAR,
	EVENT_DISABLE,
	EVENT_POWER_D3,
};

struct status_step {
	uint32_t value;
	uint32_t advance_us;
};

static const struct dw_i2c_bus_config bus_config = {
	.speed = I2C_SPEED_FAST,
	.speed_config[0] = {
		.speed = I2C_SPEED_FAST,
		.scl_lcnt = 100,
		.scl_hcnt = 100,
		.sda_hold = 30,
	},
};

static union pci_bank config_space;
u8 *const pci_mmconf = (u8 *)&config_space;
static enum lpss_pwr_state power_state;
static int bus_devfn;
static bool overlaps_smram;
static uint32_t mmio[TEST_MMIO_SIZE / sizeof(uint32_t)];
static struct status_step status_steps[4];
static size_t status_step_count;
static size_t status_step_index;
static enum event events[64];
static size_t event_count;
static unsigned int command_writes;
static uint32_t command_values[4];
static unsigned int enable_reads;
static unsigned int expire_on_enable_read;
static bool expire_after_first_command;
static bool fail_disable;
static int64_t now_us;

void dw_i2c_smm_reset_for_test(void);

static size_t mmio_offset(const volatile void *addr)
{
	uintptr_t address = (uintptr_t)addr;

	assert_uint_in_range(address, TEST_MMIO_BASE,
			     TEST_MMIO_BASE + TEST_MMIO_SIZE - 1);
	return address - TEST_MMIO_BASE;
}

static void record_event(enum event event)
{
	assert_true(event_count < ARRAY_SIZE(events));
	events[event_count++] = event;
}

static size_t find_event_after(enum event event, size_t after)
{
	size_t i;

	for (i = after + 1; i < event_count; i++)
		if (events[i] == event)
			return i;

	fail_msg("event %d not found after index %zu", event, after);
	return event_count;
}

uint32_t dw_i2c_read32_for_test(const volatile void *addr)
{
	size_t offset = mmio_offset(addr);

	if (offset == REG_ENABLE) {
		enable_reads++;
		if (expire_on_enable_read == enable_reads)
			now_us += 10001;
	}

	if (offset == REG_STATUS && status_step_index < status_step_count) {
		const struct status_step *step = &status_steps[status_step_index++];

		now_us += step->advance_us;
		return step->value;
	}

	if (offset == REG_ENABLE_STATUS && fail_disable)
		now_us += 10001;

	if (offset == REG_CLEAR_INTR)
		record_event(EVENT_CLEAR);

	return mmio[offset / sizeof(uint32_t)];
}

void dw_i2c_write32_for_test(volatile void *addr, uint32_t value)
{
	size_t offset = mmio_offset(addr);

	if (offset == REG_CMD_DATA) {
		assert_true(command_writes < ARRAY_SIZE(command_values));
		command_values[command_writes] = value;
		command_writes++;
		record_event(EVENT_COMMAND);
		if (expire_after_first_command && command_writes == 1)
			now_us += 10001;
	}

	if (offset == REG_ENABLE) {
		if (value & ENABLE_CONTROLLER) {
			record_event(EVENT_ENABLE);
			mmio[REG_ENABLE_STATUS / sizeof(uint32_t)] = ENABLE_CONTROLLER;
		} else {
			record_event(EVENT_DISABLE);
			if (!fail_disable)
				mmio[REG_ENABLE_STATUS / sizeof(uint32_t)] = 0;
		}
	}

	mmio[offset / sizeof(uint32_t)] = value;
}

void timer_monotonic_get(struct mono_time *mt)
{
	mt->microseconds = now_us;
}

void udelay(unsigned int usecs)
{
	now_us += usecs;
}

int dw_i2c_soc_bus_to_devfn(unsigned int bus)
{
	return bus_devfn;
}

const struct dw_i2c_bus_config *dw_i2c_get_soc_cfg(unsigned int bus)
{
	return &bus_config;
}

bool smm_region_overlaps_handler(const struct region *r)
{
	return overlaps_smram;
}

enum lpss_pwr_state lpss_get_power_state(pci_devfn_t devfn)
{
	return power_state;
}

void lpss_set_power_state(pci_devfn_t devfn, enum lpss_pwr_state state)
{
	power_state = state;
	record_event(state == STATE_D0 ? EVENT_POWER_D0 : EVENT_POWER_D3);
}

void lpss_reset_release(uintptr_t base)
{
	assert_int_equal(base, TEST_MMIO_BASE);
	record_event(EVENT_RESET_RELEASE);
}

static void authorize_controller(void)
{
	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = TEST_MMIO_BASE;
	config_space.reg16[PCI_COMMAND / sizeof(uint16_t)] = PCI_COMMAND_MEMORY;
	assert_int_equal(dw_i2c_smm_init(0), CB_SUCCESS);
}

static int transfer(uint16_t flags, uint8_t *buffer, size_t length)
{
	struct i2c_msg message = {
		.slave = 0x2c,
		.flags = flags,
		.buf = buffer,
		.len = length,
	};

	return platform_i2c_transfer(0, &message, 1);
}

static int setup(void **state)
{
	memset(&config_space, 0, sizeof(config_space));
	memset(mmio, 0, sizeof(mmio));
	memset(status_steps, 0, sizeof(status_steps));
	memset(events, 0, sizeof(events));
	memset(command_values, 0, sizeof(command_values));
	bus_devfn = PCI_DEVFN(0, 0);
	power_state = STATE_D3;
	overlaps_smram = false;
	status_step_count = 0;
	status_step_index = 0;
	event_count = 0;
	command_writes = 0;
	enable_reads = 0;
	expire_on_enable_read = 0;
	expire_after_first_command = false;
	fail_disable = false;
	now_us = 0;
	mmio[REG_COMP_TYPE / sizeof(uint32_t)] = DW_I2C_COMP_TYPE;
	mmio[REG_STATUS / sizeof(uint32_t)] =
		STATUS_TX_FIFO_NOT_FULL | STATUS_TX_FIFO_EMPTY;
	mmio[REG_RAW_INTR_STAT / sizeof(uint32_t)] = INTR_STAT_STOP_DET;
	dw_i2c_smm_reset_for_test();
	return 0;
}

static void invalid_and_protected_bar_ranges_are_rejected(void **state)
{
	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = 0;
	assert_int_equal(dw_i2c_smm_init(0), CB_ERR);

	dw_i2c_smm_reset_for_test();
	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = TEST_MMIO_BASE;
	overlaps_smram = true;
	assert_int_equal(dw_i2c_smm_init(0), CB_ERR);
}

static void active_controller_is_refused_before_transfer(void **state)
{
	uint8_t value = 0x5a;

	authorize_controller();
	power_state = STATE_D0;
	assert_int_equal(transfer(0, &value, 1), -1);
	assert_int_equal(command_writes, 0);
	assert_int_equal(event_count, 0);
}

static void failed_initialization_is_explicit_and_not_retried(void **state)
{
	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] =
		PCI_BASE_ADDRESS_SPACE_IO;
	assert_int_equal(dw_i2c_smm_init(0), CB_ERR);

	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = TEST_MMIO_BASE;
	assert_int_equal(dw_i2c_smm_init(0), CB_ERR);
	assert_false(dw_i2c_smm_bus_is_runtime_suspended(0));
}

static void changed_bar_and_disabled_decoding_are_rejected(void **state)
{
	authorize_controller();
	assert_true(dw_i2c_smm_bus_is_runtime_suspended(0));

	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = TEST_MMIO_BASE + 0x1000;
	assert_false(dw_i2c_smm_bus_is_runtime_suspended(0));

	config_space.reg32[PCI_BASE_ADDRESS_0 / sizeof(uint32_t)] = TEST_MMIO_BASE;
	config_space.reg16[PCI_COMMAND / sizeof(uint16_t)] = 0;
	assert_false(dw_i2c_smm_bus_is_runtime_suspended(0));
}

static void expired_deadline_blocks_ready_tx_command(void **state)
{
	uint8_t value = 0x5a;

	authorize_controller();
	/* The third enable read occurs after the shared transfer stopwatch starts. */
	expire_on_enable_read = 3;
	assert_int_equal(transfer(0, &value, 1), -1);
	assert_int_equal(command_writes, 0);
}

static void expired_deadline_blocks_rx_command(void **state)
{
	uint8_t value = 0;

	authorize_controller();
	expire_on_enable_read = 3;
	assert_int_equal(transfer(I2C_M_RD, &value, 1), -1);
	assert_int_equal(command_writes, 0);
}

static void expiry_between_bytes_blocks_second_command(void **state)
{
	uint8_t values[] = { 0x5a, 0xa5 };

	authorize_controller();
	expire_after_first_command = true;
	assert_int_equal(transfer(0, values, ARRAY_SIZE(values)), -1);
	assert_int_equal(command_writes, 1);
	assert_int_equal(command_values[0], values[0]);
}

static void readiness_after_expiry_does_not_issue_command(void **state)
{
	uint8_t value = 0x5a;

	authorize_controller();
	status_steps[0] = (struct status_step) { .value = 0, .advance_us = 9999 };
	status_steps[1] = (struct status_step) {
		.value = STATUS_TX_FIFO_NOT_FULL,
		.advance_us = 2,
	};
	status_step_count = 2;
	assert_int_equal(transfer(0, &value, 1), -1);
	assert_int_equal(command_writes, 0);
}

static void transfer_failure_runs_cleanup_and_restores_power(void **state)
{
	uint8_t value = 0x5a;
	size_t enabled;
	size_t cleared;
	size_t disabled;
	size_t powered_down;

	authorize_controller();
	expire_on_enable_read = 3;
	assert_int_equal(transfer(0, &value, 1), -1);

	enabled = find_event_after(EVENT_ENABLE, 0);
	cleared = find_event_after(EVENT_CLEAR, enabled);
	disabled = find_event_after(EVENT_DISABLE, cleared);
	powered_down = find_event_after(EVENT_POWER_D3, disabled);
	assert_true(enabled < cleared);
	assert_true(cleared < disabled);
	assert_true(disabled < powered_down);
	assert_int_equal(power_state, STATE_D3);
}

static void cleanup_failure_overrides_success(void **state)
{
	uint8_t value = 0x5a;
	size_t command;
	size_t cleared;
	size_t disabled;
	size_t powered_down;

	authorize_controller();
	fail_disable = true;
	assert_int_equal(transfer(0, &value, 1), -1);
	assert_int_equal(command_writes, 1);
	assert_int_equal(command_values[0], value | BIT(9));

	command = find_event_after(EVENT_COMMAND, 0);
	cleared = find_event_after(EVENT_CLEAR, command);
	disabled = find_event_after(EVENT_DISABLE, cleared);
	powered_down = find_event_after(EVENT_POWER_D3, disabled);
	assert_true(command < cleared);
	assert_true(cleared < disabled);
	assert_true(disabled < powered_down);
	assert_int_equal(power_state, STATE_D3);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(failed_initialization_is_explicit_and_not_retried, setup),
		cmocka_unit_test_setup(invalid_and_protected_bar_ranges_are_rejected, setup),
		cmocka_unit_test_setup(active_controller_is_refused_before_transfer, setup),
		cmocka_unit_test_setup(changed_bar_and_disabled_decoding_are_rejected, setup),
		cmocka_unit_test_setup(expired_deadline_blocks_ready_tx_command, setup),
		cmocka_unit_test_setup(expired_deadline_blocks_rx_command, setup),
		cmocka_unit_test_setup(expiry_between_bytes_blocks_second_command, setup),
		cmocka_unit_test_setup(readiness_after_expiry_does_not_issue_command, setup),
		cmocka_unit_test_setup(transfer_failure_runs_cleanup_and_restores_power, setup),
		cmocka_unit_test_setup(cleanup_failure_overrides_success, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
