/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>

#include "../../src/soc/intel/common/block/fast_spi/fast_spi_flash.c"

static uint32_t spibar[0x100 / sizeof(uint32_t)];

void *fast_spi_get_bar(void)
{
	return spibar;
}

static void test_fpr_range(void **state)
{
	struct region region = region_create(0x12000, 0x3000);
	u32 expected = SPI_FPR(0x12000, 0x14fff) | SPI_FPR_WPE;

	memset(spibar, 0, sizeof(spibar));
	assert_int_equal(fast_spi_flash_protect(NULL, &region, WRITE_PROTECT), 0);
	assert_int_equal(spibar[SPIBAR_FPR_BASE / sizeof(uint32_t)], expected);
}

static void test_fpr_rejects_unencodable_range(void **state)
{
	struct region unaligned = region_create(0x12001, 0x3000);
	struct region too_large = region_create(0, 0x8001000);

	memset(spibar, 0, sizeof(spibar));
	assert_int_equal(fast_spi_flash_protect(NULL, &unaligned, WRITE_PROTECT), -1);
	assert_int_equal(fast_spi_flash_protect(NULL, &too_large, WRITE_PROTECT), -1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_fpr_range),
		cmocka_unit_test(test_fpr_rejects_unencodable_range),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
