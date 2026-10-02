/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/azalia_device.h>
#include <device/mmio.h>
#include <delay.h>
#include <timer.h>
#include <tests/test.h>

static u8 registers[0x100];
static u32 status;
static uint64_t elapsed;
static u32 command_status;
static bool cleanup_stuck;
static struct {
	size_t offset;
	u32 value;
} writes[16];
static size_t write_count;

static u32 command_read32(const volatile void *address)
{
	assert_ptr_equal(address, registers + HDA_ICII_REG);
	return status;
}

static void command_write32(volatile void *address, u32 value)
{
	size_t offset = (volatile u8 *)address - registers;

	assert_true(write_count < ARRAY_SIZE(writes));
	writes[write_count].offset = offset;
	writes[write_count++].value = value;
	if (offset == HDA_IC_REG) {
		/* A previous response must be acknowledged before a new command. */
		assert_int_equal(status & (HDA_ICII_VALID | HDA_ICII_BUSY), 0);
		return;
	}
	assert_int_equal(offset, HDA_ICII_REG);
	/* VALID is W1C; BUSY starts the command or aborts a timed-out command. */
	if (value & HDA_ICII_VALID)
		status &= ~HDA_ICII_VALID;
	if (value & HDA_ICII_BUSY)
		status = command_status;
	else if (!cleanup_stuck)
		status &= ~HDA_ICII_BUSY;
}

static void __maybe_unused command_setbits32(volatile void *address, u32 bits)
{
	command_write32(address, command_read32(address) | bits);
}

static int command_printk(int level, const char *format, ...)
{
	return 0;
}

void timer_monotonic_get(struct mono_time *time)
{
	time->microseconds = elapsed++;
}

void udelay(unsigned int usecs)
{
	elapsed += usecs;
}

/* Exercise the production implementation, replacing only MMIO/logging. */
#define read32  command_read32
#define write32 command_write32
#undef setbits32
#define setbits32 command_setbits32
#define printk    command_printk
#include "../../src/device/azalia_device.c"

static int setup(void **state)
{
	status = elapsed = write_count = 0;
	command_status = HDA_ICII_VALID;
	cleanup_stuck = false;
	return 0;
}

static void command_order(void **state)
{
	const u32 verbs[] = {0x00172030, 0x0017ff00};

	status = HDA_ICII_VALID;
	assert_int_equal(azalia_program_verb_table(registers, verbs, ARRAY_SIZE(verbs)), 0);
	assert_int_equal(write_count, 6);
	for (size_t i = 0; i < ARRAY_SIZE(verbs); i++) {
		assert_int_equal(writes[3 * i].offset, HDA_ICII_REG);
		assert_int_equal(writes[3 * i].value, HDA_ICII_VALID);
		assert_int_equal(writes[3 * i + 1].offset, HDA_IC_REG);
		assert_int_equal(writes[3 * i + 1].value, verbs[i]);
		assert_int_equal(writes[3 * i + 2].offset, HDA_ICII_REG);
		assert_int_equal(writes[3 * i + 2].value, HDA_ICII_BUSY);
	}
}

static void readiness_timeout(void **state)
{
	const u32 verb = 0x00172030;

	status = HDA_ICII_BUSY;
	assert_int_equal(azalia_program_verb_table(registers, &verb, 1), -1);
	assert_int_equal(write_count, 0);
}

static void response_timeout(void **state)
{
	const u32 verbs[] = {0x00172030, 0x0017ff00};

	command_status = HDA_ICII_BUSY;
	assert_int_equal(azalia_program_verb_table(registers, verbs, ARRAY_SIZE(verbs)), -1);
	assert_int_equal(write_count, 4);
	assert_int_equal(writes[3].offset, HDA_ICII_REG);
	assert_int_equal(writes[3].value, 0);
	assert_int_equal(status, 0);
}

static void response_missing(void **state)
{
	const u32 verb = 0x00172030;

	/* Model the observed zero status, without assuming its physical cause. */
	command_status = 0;
	assert_int_equal(azalia_write_verb(registers, verb), -1);
	assert_int_equal(write_count, 4);
	assert_int_equal(writes[3].value, 0);
	assert_int_equal(status, 0);
}

static void cleanup_timeout(void **state)
{
	const u32 verb = 0x00172030;

	command_status = HDA_ICII_BUSY;
	cleanup_stuck = true;
	assert_int_equal(azalia_write_verb(registers, verb), -2);
	assert_int_equal(write_count, 4);
	assert_int_equal(status, HDA_ICII_BUSY);
}

static void absent_table(void **state)
{
	assert_int_equal(azalia_program_verb_table(registers, NULL, 1), 0);
	assert_int_equal(write_count, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(command_order, setup),
		cmocka_unit_test_setup(readiness_timeout, setup),
		cmocka_unit_test_setup(response_timeout, setup),
		cmocka_unit_test_setup(response_missing, setup),
		cmocka_unit_test_setup(cleanup_timeout, setup),
		cmocka_unit_test_setup(absent_table, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
