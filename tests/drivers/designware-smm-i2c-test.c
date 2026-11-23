/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/mmio.h>
#include <device/pci_ops.h>
#include <tests/test.h>
#include <types.h>

static uint32_t read_register(const volatile void *address);
static void write_register(volatile void *address, uint32_t value);
static uint32_t read_pci32(pci_devfn_t dev, unsigned int offset);
static uint16_t read_pci16(pci_devfn_t dev, unsigned int offset);

#define read32 read_register
#define write32 write_register
#define pci_s_read_config32 read_pci32
#define pci_s_read_config16 read_pci16
#include <drivers/i2c/designware/dw_i2c.c>

static struct dw_i2c_regs controller __aligned(4096);
static enum lpss_pwr_state power;
static unsigned int now, tx_ready, rx_delay, rx_ready, disable_at, commands, resets;
static bool reading, stuck_disable, abort_transfer;

static uint32_t read_pci32(pci_devfn_t dev, unsigned int offset)
{
	uint64_t address = (uintptr_t)&controller;
	if (offset == PCI_BASE_ADDRESS_1)
		return address >> 32;
	assert_int_equal(offset, PCI_BASE_ADDRESS_0);
	return (uint32_t)address | PCI_BASE_ADDRESS_MEM_LIMIT_64;
}

static uint16_t read_pci16(pci_devfn_t dev, unsigned int offset)
{
	assert_int_equal(offset, PCI_COMMAND);
	return PCI_COMMAND_MEMORY;
}

static uint32_t read_register(const volatile void *address)
{
	assert_int_equal(power, STATE_D0);
	if (address == &controller.status) {
		now += 100;
		return STATUS_TX_FIFO_EMPTY | (now >= tx_ready ? STATUS_TX_FIFO_NOT_FULL : 0) |
			(reading && now >= rx_ready ? STATUS_RX_FIFO_NOT_EMPTY : 0);
	}
	if (address == &controller.enable_status) {
		now += 100;
		return stuck_disable || now < disable_at ? ENABLE_CONTROLLER : 0;
	}
	if (address == &controller.cmd_data) {
		reading = false;
		return 0x39;
	}
	if (address == &controller.raw_intr_stat)
		return INTR_STAT_STOP_DET | (abort_transfer ? INTR_STAT_TX_ABORT : 0);
	return *(const volatile uint32_t *)address;
}

static void write_register(volatile void *address, uint32_t value)
{
	assert_int_equal(power, STATE_D0);
	*(volatile uint32_t *)address = value;
	if (address == &controller.cmd_data) {
		commands++;
		reading = value & CMD_DATA_CMD;
		rx_ready = now + rx_delay;
	}
}

void timer_monotonic_get(struct mono_time *time)
{
	time->microseconds = now;
}

void udelay(unsigned int usecs)
{
	now += usecs;
}

int dw_i2c_soc_bus_to_devfn(unsigned int bus)
{
	return 0;
}

bool smm_region_overlaps_handler(const struct region *region)
{
	return false;
}

const struct dw_i2c_bus_config *dw_i2c_get_soc_cfg(unsigned int bus)
{
	static const struct dw_i2c_bus_config config = {
		.speed = I2C_SPEED_FAST,
		.speed_config = {{ .speed = I2C_SPEED_FAST, .scl_lcnt = 100, .scl_hcnt = 100 }},
	};
	return &config;
}

enum lpss_pwr_state lpss_get_power_state(pci_devfn_t dev)
{
	return power;
}

void lpss_set_power_state(pci_devfn_t dev, enum lpss_pwr_state state)
{
	power = state;
}

void lpss_reset_release(uintptr_t base)
{
	assert_int_equal(base, (uintptr_t)&controller);
	resets++;
}

static int setup(void **unused)
{
	controller = (struct dw_i2c_regs) { .comp_type = DW_I2C_COMP_TYPE };
	now = tx_ready = rx_delay = rx_ready = disable_at = commands = resets = 0;
	reading = stuck_disable = abort_transfer = false;
	power = STATE_D3;
	smm_i2c_initialized = false;
	dw_i2c_smm_init(0);
	return 0;
}

static int combined_read(uint8_t *result)
{
	uint8_t index = 8;
	struct i2c_msg messages[] = {
		{ .slave = 0x15, .buf = &index, .len = 1 },
		{ .slave = 0x15, .buf = result, .len = 1, .flags = I2C_M_RD },
	};
	return platform_i2c_transfer(0, messages, ARRAY_SIZE(messages));
}

static void complete_combined_read(void **unused)
{
	uint8_t data = 0;
	tx_ready = 2000;
	rx_delay = 3000;
	assert_int_equal(combined_read(&data), 0);
	assert_int_equal(data, 0x39);
	assert_int_equal(commands, 2);
	assert_int_equal(resets, 1);
	assert_int_equal(power, STATE_D3);
}

static void reject_late_combined_read(void **unused)
{
	uint8_t data = 0;
	tx_ready = 6000;
	rx_delay = 5000;
	assert_int_equal(combined_read(&data), -1);
	assert_int_equal(data, 0);
	assert_int_equal(commands, 2);
	assert_int_equal(power, STATE_D3);
	assert_true(now < 11000);
}

static void preparation_consumes_budget(void **unused)
{
	uint8_t data = 0;
	controller.enable = ENABLE_CONTROLLER;
	disable_at = 6000;
	rx_delay = 5000;
	assert_int_equal(combined_read(&data), -1);
	assert_int_equal(data, 0);
	assert_int_equal(power, STATE_D3);
	assert_true(now < 11000);
}

static void report_failed_shutdown(void **unused)
{
	uint8_t data = 0;
	tx_ready = 9000;
	stuck_disable = true;
	assert_int_equal(combined_read(&data), -1);
	assert_int_equal(data, 0x39);
	assert_int_equal(power, STATE_D3);
	assert_true(now >= 19000 && now < 20000);
}

static void preserve_abort_and_active_controller(void **unused)
{
	uint8_t data = 0;
	abort_transfer = true;
	assert_int_equal(combined_read(&data), -1);
	assert_int_equal(power, STATE_D3);
	power = STATE_D0;
	assert_int_equal(combined_read(&data), -1);
	assert_int_equal(power, STATE_D0);
	assert_int_equal(resets, 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(complete_combined_read, setup),
		cmocka_unit_test_setup(reject_late_combined_read, setup),
		cmocka_unit_test_setup(preparation_consumes_budget, setup),
		cmocka_unit_test_setup(report_failed_shutdown, setup),
		cmocka_unit_test_setup(preserve_abort_and_active_controller, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
