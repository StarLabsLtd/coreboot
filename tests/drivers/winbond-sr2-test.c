/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>

#include "../../src/drivers/spi/winbond.c"

static int sr1_result;
static int sr2_result;
static uint8_t sr1_value;
static uint8_t sr2_value;

int spi_flash_status(const struct spi_flash *flash, uint8_t *reg)
{
	(void)flash;
	*reg = sr1_value;
	return sr1_result;
}

int spi_flash_read_status(const struct spi_flash *flash, uint8_t opcode, uint8_t *reg)
{
	(void)flash;
	assert_int_equal(opcode, CMD_W25_RDSR2);
	*reg = sr2_value;
	return sr2_result;
}

static void test_sr2_read_takes_precedence(void **state)
{
	const struct spi_flash flash = { .part = &flash_table[0] };
	struct status_regs regs;
	struct status_regs_valid valid;

	sr1_result = 0;
	sr2_result = 0;
	sr1_value = 0x04;
	sr2_value = 0x40;
	assert_int_equal(winbond_read_status_regs(&flash, &regs, &valid), 0);
	assert_int_equal(regs.reg1.u, sr1_value);
	assert_int_equal(regs.reg2.u, sr2_value);
	assert_true(valid.reg2);
}

static void test_sr2_fallback(void **state)
{
	const struct spi_flash flash = { .part = &flash_table[0] };
	struct status_regs regs;
	struct status_regs_valid valid;
	int expected = CONFIG_SPI_FLASH_WINBOND_SR2_PROVISIONED_VALUE;

	sr1_result = 0;
	sr2_result = -1;
	sr1_value = 0x04;
	sr2_value = 0;
	if (expected > UINT8_MAX) {
		assert_int_not_equal(winbond_read_status_regs(&flash, &regs, &valid), 0);
		return;
	}

	assert_int_equal(winbond_read_status_regs(&flash, &regs, &valid), 0);
	assert_int_equal(regs.reg1.u, sr1_value);
	assert_int_equal(regs.reg2.u, expected);
	assert_false(valid.reg2);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_sr2_read_takes_precedence),
		cmocka_unit_test(test_sr2_fallback),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
